//===- GlobalsToMemref.cpp - Turn lcc's global arrays into memref.global --===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// lcc-globals-to-memref: see Passes.td.  The pass looks at all globals
// before it changes anything, then changes the globals it may, one by one:
// the new memref.global, then for each llvm.mlir.addressof of the global a
// memref.get_global in its place, memref accesses for the loads and stores
// it may change, and a pointer made from the memref for the remaining uses.
//
// The address of an access is the global's plus the indices of a chain of
// llvm.getelementptr of i8, each sign-extended or truncated to the index
// width P of pointers, added modulo 2^P.  When LinearAnalysis shows every
// index and their sum to be exact linear forms that fit in P bits, the
// address is the global's plus that sum, and the access is at element
// sum / sizeof(T) of the memref.
//
//===----------------------------------------------------------------------===//

#include "lcc-opt/Passes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/LLVMIR/LLVMTypes.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/CheckedArithmetic.h"

#include <string>
#include <vector>

namespace lcc {
#define GEN_PASS_DEF_LCCGLOBALSTOMEMREF
#include "lcc-opt/Passes.h.inc"
} // namespace lcc

using namespace mlir;

namespace {

/// The data layout facts the pass needs, from the llvm.data_layout string
/// of the module (LLVM's defaults without one).
struct Layout {
  bool bigEndian = false;
  /// The index width of pointers of address space 0.
  unsigned pointerIndexWidth = 64;
};

/// A global and its uses, as found before any change.
struct Global {
  LLVM::GlobalOp op;
  /// The size in bytes.
  uint64_t size = 0;
  /// The llvm.mlir.addressof of the global in function bodies.
  SmallVector<LLVM::AddressOfOp> addrs;
  /// For each of addrs, the loads and stores and the getelementptrs of i8
  /// reached from it, in the order found.
  SmallVector<SmallVector<Operation *>> accesses;
  SmallVector<SmallVector<LLVM::GEPOp>> geps;
  /// Why the global stays, empty if it changes.
  std::string reason;
  /// The element type, and the initializer: none (a declaration), all zero,
  /// or bytes.
  Type elemType;
  bool declaration = false, zero = false;
  std::vector<uint8_t> bytes;
};

} // namespace

static Layout getLayout(ModuleOp module) {
  Layout layout;
  auto dl = module->getAttrOfType<StringAttr>("llvm.data_layout");
  if (!dl)
    return layout;
  SmallVector<StringRef> specs;
  dl.getValue().split(specs, '-');
  for (StringRef spec : specs) {
    if (spec == "E")
      layout.bigEndian = true;
    else if (spec == "e")
      layout.bigEndian = false;
    // p[0]:size:abi[:pref[:idx]], for address space 0
    else if (spec.starts_with("p:") || spec.starts_with("p0:")) {
      SmallVector<StringRef> fields;
      spec.split(fields, ':');
      unsigned size = 0, idx = 0;
      if (fields.size() >= 2 && !fields[1].getAsInteger(10, size) && size)
        layout.pointerIndexWidth = size;
      if (fields.size() >= 5 && !fields[4].getAsInteger(10, idx) && idx)
        layout.pointerIndexWidth = idx;
    }
  }
  return layout;
}

/// The size in bytes of a byte array or packed structure of them, lcc's
/// types of globals and of the pieces of their initializers; 0 for any
/// other type.
static uint64_t byteSize(Type type) {
  if (auto array = dyn_cast<LLVM::LLVMArrayType>(type)) {
    auto elem = dyn_cast<IntegerType>(array.getElementType());
    return elem && elem.getWidth() == 8 ? array.getNumElements() : 0;
  }
  if (auto st = dyn_cast<LLVM::LLVMStructType>(type)) {
    if (!st.isPacked() || st.isOpaque())
      return 0;
    uint64_t size = 0;
    for (Type field : st.getBody()) {
      uint64_t n = byteSize(field);
      if (!n)
        return 0;
      size += n;
    }
    return size;
  }
  return 0;
}

/// Whether `type` is a pointer or has pointers in it.
static bool hasPointer(Type type) {
  if (isa<LLVM::LLVMPointerType>(type))
    return true;
  if (auto array = dyn_cast<LLVM::LLVMArrayType>(type))
    return hasPointer(array.getElementType());
  if (auto st = dyn_cast<LLVM::LLVMStructType>(type))
    return !st.isOpaque() && llvm::any_of(st.getBody(), hasPointer);
  return false;
}

/// Appends the bytes of the initializer value `v`, a byte string, a zero
/// or a packed structure filled field by field with llvm.insertvalue, to
/// `out`; fails on anything else, as an address.
static LogicalResult appendBytes(Value v, std::vector<uint8_t> &out,
                                 std::string &reason) {
  Operation *def = v.getDefiningOp();
  if (auto zero = dyn_cast_or_null<LLVM::ZeroOp>(def)) {
    uint64_t n = byteSize(zero.getType());
    if (!n) {
      reason = "initializer of an unexpected type";
      return failure();
    }
    out.insert(out.end(), n, 0);
    return success();
  }
  if (auto cst = dyn_cast_or_null<LLVM::ConstantOp>(def)) {
    auto str = dyn_cast<StringAttr>(cst.getValue());
    uint64_t n = byteSize(cst.getType());
    if (!str || !n || str.getValue().size() != n) {
      reason = "initializer with a constant other than bytes";
      return failure();
    }
    out.insert(out.end(), str.getValue().bytes_begin(),
               str.getValue().bytes_end());
    return success();
  }
  if (isa_and_nonnull<LLVM::InsertValueOp>(def)) {
    // insertvalue(piece_k, ... insertvalue(piece_0, undef)[0] ...)[k]
    auto st = dyn_cast<LLVM::LLVMStructType>(v.getType());
    if (!st || !byteSize(st)) {
      reason = "initializer of an unexpected type";
      return failure();
    }
    SmallVector<Value> pieces(st.getBody().size());
    Value acc = v;
    while (auto ins = acc.getDefiningOp<LLVM::InsertValueOp>()) {
      ArrayRef<int64_t> pos = ins.getPosition();
      if (pos.size() != 1 || pos[0] < 0 ||
          pos[0] >= (int64_t)pieces.size() || pieces[pos[0]]) {
        reason = "initializer of an unexpected form";
        return failure();
      }
      pieces[pos[0]] = ins.getValue();
      acc = ins.getContainer();
    }
    if (!acc.getDefiningOp<LLVM::UndefOp>() &&
        !acc.getDefiningOp<LLVM::PoisonOp>()) {
      reason = "initializer of an unexpected form";
      return failure();
    }
    for (Value piece : pieces) {
      if (!piece) {
        reason = "initializer with an unset piece";
        return failure();
      }
      if (failed(appendBytes(piece, out, reason)))
        return failure();
    }
    return success();
  }
  if (isa_and_nonnull<LLVM::AddressOfOp, LLVM::GEPOp>(def))
    reason = "initializer with an address";
  else
    reason = "initializer of an unexpected form";
  return failure();
}

/// Finds the initializer of `g` (none, zero or bytes); fails, with a
/// reason, when it is something else.
static LogicalResult getInitializer(Global &g) {
  LLVM::GlobalOp op = g.op;
  if (Attribute value = op.getValueAttr()) {
    auto str = dyn_cast<StringAttr>(value);
    if (!str || str.getValue().size() != g.size) {
      g.reason = "initializer other than bytes";
      return failure();
    }
    g.bytes.assign(str.getValue().bytes_begin(), str.getValue().bytes_end());
    return success();
  }
  Region &init = op.getInitializerRegion();
  if (init.empty()) {
    if (op.getLinkage() != LLVM::Linkage::External) {
      g.reason = "declaration that is not external";
      return failure();
    }
    g.declaration = true;
    return success();
  }
  if (!llvm::hasSingleElement(init)) {
    g.reason = "initializer of an unexpected form";
    return failure();
  }
  auto ret = dyn_cast<LLVM::ReturnOp>(init.front().getTerminator());
  if (!ret || ret.getNumOperands() != 1) {
    g.reason = "initializer of an unexpected form";
    return failure();
  }
  if (ret.getOperand(0).getDefiningOp<LLVM::ZeroOp>()) {
    g.zero = true;
    return success();
  }
  if (failed(appendBytes(ret.getOperand(0), g.bytes, g.reason)))
    return failure();
  if (g.bytes.size() != g.size) {
    g.reason = "initializer of another size";
    return failure();
  }
  return success();
}

/// Whether `type` may be the element type of a memref global: an integer
/// of 8, 16, 32 or 64 bits, f32 or f64.
static bool isElementType(Type type) {
  if (auto it = dyn_cast<IntegerType>(type))
    return it.isSignless() && llvm::is_contained({8u, 16u, 32u, 64u},
                                                  it.getWidth());
  return type.isF32() || type.isF64();
}

static unsigned elementSize(Type type) {
  return type.getIntOrFloatBitWidth() / 8;
}

/// Whether `gep` is lcc's: one index into i8.
static bool isByteGEP(LLVM::GEPOp gep) {
  auto elem = dyn_cast<IntegerType>(gep.getElemType());
  return elem && elem.getWidth() == 8 && gep.getIndices().size() == 1;
}

/// Collects the loads, stores and byte getelementptrs reached from `addr`.
static void collectAccesses(LLVM::AddressOfOp addr,
                            SmallVector<Operation *> &accesses,
                            SmallVector<LLVM::GEPOp> &geps) {
  SmallVector<Value> work{addr.getResult()};
  while (!work.empty()) {
    Value ptr = work.pop_back_val();
    for (OpOperand &use : ptr.getUses()) {
      Operation *user = use.getOwner();
      if (auto gep = dyn_cast<LLVM::GEPOp>(user)) {
        if (gep.getBase() == ptr && isByteGEP(gep)) {
          geps.push_back(gep);
          work.push_back(gep.getResult());
        }
      } else if (auto load = dyn_cast<LLVM::LoadOp>(user)) {
        accesses.push_back(load);
      } else if (auto store = dyn_cast<LLVM::StoreOp>(user)) {
        if (store.getAddr() == ptr && store.getValue() != ptr)
          accesses.push_back(store);
      }
    }
  }
}

static Type accessType(Operation *access) {
  if (auto load = dyn_cast<LLVM::LoadOp>(access))
    return load.getType();
  return cast<LLVM::StoreOp>(access).getValue().getType();
}

/// Decides whether `g` changes, finding its element type and initializer.
static void analyze(Global &g, const Layout &layout) {
  LLVM::GlobalOp op = g.op;
  if (!g.reason.empty())
    return;
  g.size = byteSize(op.getGlobalType());
  if (!g.size) {
    // lcc gives a scalar variable its own type, an array of pointers
    // initialized with addresses an array of pointers, and other
    // initializers with addresses packed structures with pointer fields.
    Type type = op.getGlobalType();
    auto array = dyn_cast<LLVM::LLVMArrayType>(type);
    if (isa<IntegerType, FloatType, LLVM::LLVMPointerType>(type))
      g.reason = "a scalar";
    else if (array && array.getNumElements() == 0)
      g.reason = "no size (an incomplete array)";
    else if (array && isa<LLVM::LLVMPointerType>(array.getElementType()))
      g.reason = "an array of pointers";
    else if (hasPointer(type))
      g.reason = "initializer with an address";
    else
      g.reason = "not an array of bytes";
    return;
  }
  if (op.getLinkage() != LLVM::Linkage::External &&
      op.getLinkage() != LLVM::Linkage::Internal) {
    g.reason = "linkage " +
               LLVM::linkage::stringifyLinkage(op.getLinkage()).str();
    return;
  }
  if (op.getAddrSpace() != 0 || op.getSection() || op.getComdat() ||
      op.getThreadLocal_() || op.getExternallyInitialized() ||
      op.getVisibility_() != LLVM::Visibility::Default ||
      op.getDbgExprs() || op.getTargetSpecificAttrs()) {
    g.reason = "attributes memref.global does not have";
    return;
  }

  // The element type: that of every load and store.
  SmallVector<Type> types;
  for (auto [addr, accesses] : llvm::zip(g.addrs, g.accesses))
    for (Operation *access : accesses)
      if (!llvm::is_contained(types, accessType(access)))
        types.push_back(accessType(access));
  if (types.empty()) {
    g.reason = "no load or store";
    return;
  }
  if (types.size() > 1) {
    g.reason = "loads and stores of different types";
    return;
  }
  g.elemType = types[0];
  if (!isElementType(g.elemType)) {
    g.reason = "loads and stores of an unsupported type";
    return;
  }
  if (g.size % elementSize(g.elemType)) {
    g.reason = "size not a multiple of the element size";
    return;
  }

  if (failed(getInitializer(g)))
    return;
  if (!g.declaration && !g.zero) {
    bool allZero = llvm::all_of(g.bytes, [](uint8_t b) { return b == 0; });
    if (allZero) {
      g.zero = true;
      g.bytes.clear();
    } else if (layout.bigEndian) {
      g.reason = "big-endian initializer";
    }
  }
}

/// The initial value of the memref global of `g`.
static Attribute initialValue(const Global &g, RankedTensorType type) {
  if (g.declaration)
    return {};
  Type elem = g.elemType;
  if (g.zero) {
    if (isa<FloatType>(elem))
      return DenseElementsAttr::get(type, FloatAttr::get(elem, 0.0));
    return DenseElementsAttr::get(type, IntegerAttr::get(elem, 0));
  }
  unsigned size = elementSize(elem), bits = 8 * size;
  SmallVector<APInt> values;
  values.reserve(g.bytes.size() / size);
  for (uint64_t i = 0; i < g.bytes.size(); i += size) {
    APInt v(bits, 0);
    for (unsigned j = 0; j < size; j++)
      v |= APInt(bits, g.bytes[i + j]) << (8 * j);
    values.push_back(v);
  }
  if (auto ft = dyn_cast<FloatType>(elem)) {
    // The bits as they are: -0.0, NaN payloads and signs, infinities and
    // subnormals stay.
    SmallVector<APFloat> floats;
    floats.reserve(values.size());
    for (const APInt &v : values)
      floats.push_back(APFloat(ft.getFloatSemantics(), v));
    return DenseElementsAttr::get(type, floats);
  }
  return DenseElementsAttr::get(type, values);
}

namespace {

/// Changes the accesses of one llvm.mlir.addressof of a global.
class Rewriter {
public:
  Rewriter(const Global &g, const Layout &layout, unsigned indexWidth,
           OpBuilder &builder)
      : g(g), layout(layout), indexWidth(indexWidth), builder(builder) {}

  /// Makes `access` a memref access of `memref`, if it may.
  void convert(Operation *access, LLVM::AddressOfOp addr, Value memref);

private:
  std::optional<Value> index(Operation *access, LLVM::AddressOfOp addr);

  const Global &g;
  const Layout &layout;
  unsigned indexWidth;
  OpBuilder &builder;
};

} // namespace

static bool fitsSigned(const lcc::IntRange &r, unsigned width) {
  if (width >= 64)
    return true;
  int64_t max = (int64_t(1) << (width - 1)) - 1;
  return r.min >= -max - 1 && r.max <= max;
}

/// Adds `b` to `a`; fails when a coefficient or the constant overflows.
static bool addForm(lcc::LinearForm &a, const lcc::LinearForm &b) {
  std::optional<int64_t> c = llvm::checkedAdd(a.constant, b.constant);
  if (!c)
    return false;
  a.constant = *c;
  for (const auto &term : b.terms) {
    // Not a structured binding: C++17 lambdas cannot capture those.
    Value v = term.first;
    int64_t k = term.second;
    auto it = llvm::find_if(a.terms, [&](auto &t) { return t.first == v; });
    if (it == a.terms.end()) {
      a.terms.push_back({v, k});
      continue;
    }
    std::optional<int64_t> s = llvm::checkedAdd(it->second, k);
    if (!s)
      return false;
    it->second = *s;
    if (!*s)
      a.terms.erase(it);
  }
  return true;
}

/// The element index of `access`, created before it, or nothing when its
/// offset is not an exact linear form of multiples of the element size.
std::optional<Value> Rewriter::index(Operation *access,
                                     LLVM::AddressOfOp addr) {
  // A new analysis: the IR has changed since the last access.
  lcc::LinearAnalysis la(indexWidth);
  unsigned p = layout.pointerIndexWidth;
  lcc::LinearForm offset;
  Value ptr = isa<LLVM::LoadOp>(access) ? cast<LLVM::LoadOp>(access).getAddr()
                                        : cast<LLVM::StoreOp>(access).getAddr();
  while (ptr != addr.getResult()) {
    auto gep = ptr.getDefiningOp<LLVM::GEPOp>();
    if (!gep)
      return std::nullopt;
    auto idx = *gep.getIndices().begin();
    lcc::LinearForm form;
    if (auto v = dyn_cast<Value>(idx)) {
      form = la.getForm(v);
      // Sign-extended or truncated to p bits: the same number if it fits.
      std::optional<lcc::IntRange> r = la.getRange(form);
      if (!r || !fitsSigned(*r, p))
        return std::nullopt;
    } else {
      form.constant = cast<IntegerAttr>(idx).getValue().getSExtValue();
    }
    if (!addForm(offset, form))
      return std::nullopt;
    ptr = gep.getBase();
  }
  std::optional<lcc::IntRange> r = la.getRange(offset);
  if (!r || !fitsSigned(*r, p))
    return std::nullopt;

  // offset / size, every term exact, and computed in index without
  // overflow: the sum of the magnitudes of the terms fits.
  int64_t size = elementSize(g.elemType);
  if (offset.constant % size || offset.constant == INT64_MIN)
    return std::nullopt;
  int64_t bound = std::abs(offset.constant / size);
  for (auto [v, k] : offset.terms) {
    unsigned width = la.getWidth(v.getType());
    std::optional<lcc::IntRange> vr = la.getRange(v);
    if (k % size || !width || width > indexWidth || !vr ||
        vr->min == INT64_MIN)
      return std::nullopt;
    int64_t mag = std::max(-vr->min, std::abs(vr->max));
    std::optional<int64_t> t = llvm::checkedMul(std::abs(k / size), mag);
    std::optional<int64_t> s = t ? llvm::checkedAdd(bound, *t) : std::nullopt;
    if (!s)
      return std::nullopt;
    bound = *s;
  }
  if (!fitsSigned({-bound, bound}, indexWidth))
    return std::nullopt;

  builder.setInsertionPoint(access);
  Location loc = access->getLoc();
  Type indexType = builder.getIndexType();
  auto constant = [&](int64_t c) -> Value {
    return arith::ConstantIndexOp::create(builder, loc, c);
  };
  Value sum;
  for (auto [v, k] : offset.terms) {
    Value x = v;
    if (!v.getType().isIndex())
      x = arith::IndexCastOp::create(builder, loc, indexType, v);
    if (k / size != 1)
      x = arith::MulIOp::create(builder, loc, x, constant(k / size));
    sum = sum ? arith::AddIOp::create(builder, loc, sum, x).getResult() : x;
  }
  if (!sum)
    return constant(offset.constant / size);
  if (offset.constant)
    sum = arith::AddIOp::create(builder, loc, sum,
                                constant(offset.constant / size));
  return sum;
}

void Rewriter::convert(Operation *access, LLVM::AddressOfOp addr,
                       Value memref) {
  unsigned size = elementSize(g.elemType);
  if (auto load = dyn_cast<LLVM::LoadOp>(access)) {
    if (load.getVolatile_() ||
        load.getOrdering() != LLVM::AtomicOrdering::not_atomic ||
        load.getSyncscope() || load.getNontemporal() || load.getInvariant() ||
        load.getInvariantGroup() || load.getDereferenceable() ||
        load.getAccessGroups() || load.getAliasScopes() ||
        load.getNoaliasScopes() || load.getTbaa() ||
        (load.getAlignment() && *load.getAlignment() < size))
      return;
    std::optional<Value> idx = index(access, addr);
    if (!idx)
      return;
    auto newLoad = memref::LoadOp::create(builder, load.getLoc(), memref,
                                          ValueRange{*idx});
    load.getResult().replaceAllUsesWith(newLoad.getResult());
    load.erase();
    return;
  }
  auto store = cast<LLVM::StoreOp>(access);
  if (store.getVolatile_() ||
      store.getOrdering() != LLVM::AtomicOrdering::not_atomic ||
      store.getSyncscope() || store.getNontemporal() ||
      store.getInvariantGroup() || store.getAccessGroups() ||
      store.getAliasScopes() || store.getNoaliasScopes() || store.getTbaa() ||
      (store.getAlignment() && *store.getAlignment() < size))
    return;
  std::optional<Value> idx = index(access, addr);
  if (!idx)
    return;
  memref::StoreOp::create(builder, store.getLoc(), store.getValue(), memref,
                          ValueRange{*idx});
  store.erase();
}

namespace {

struct GlobalsToMemrefPass
    : public lcc::impl::LccGlobalsToMemrefBase<GlobalsToMemrefPass> {
  using LccGlobalsToMemrefBase::LccGlobalsToMemrefBase;

  void runOnOperation() override;
};

} // namespace

void GlobalsToMemrefPass::runOnOperation() {
  ModuleOp module = getOperation();
  Layout layout = getLayout(module);
  unsigned indexWidth = lcc::getIndexWidth(module);

  // The globals, and the uses of their names.
  llvm::MapVector<StringAttr, Global> globals;
  for (auto op : module.getOps<LLVM::GlobalOp>())
    globals[op.getSymNameAttr()].op = op;
  if (globals.empty())
    return;
  if (std::optional<SymbolTable::UseRange> uses =
          SymbolTable::getSymbolUses(&module.getBodyRegion())) {
    for (const SymbolTable::SymbolUse &use : *uses) {
      auto it = globals.find(use.getSymbolRef().getRootReference());
      if (it == globals.end())
        continue;
      Global &g = it->second;
      Operation *user = use.getUser();
      auto addr = dyn_cast<LLVM::AddressOfOp>(user);
      if (!addr)
        g.reason = "name used by " + user->getName().getStringRef().str();
      else if (addr->getParentOfType<LLVM::GlobalOp>())
        g.reason = "address used by an initializer";
      else
        g.addrs.push_back(addr);
    }
  } else {
    // Unknown uses (in an attribute of an unregistered operation, say).
    return;
  }
  for (auto &[name, g] : globals) {
    for (LLVM::AddressOfOp addr : g.addrs) {
      g.accesses.emplace_back();
      g.geps.emplace_back();
      collectAccesses(addr, g.accesses.back(), g.geps.back());
    }
    analyze(g, layout);
  }

  MLIRContext *ctx = &getContext();
  OpBuilder builder(ctx);
  for (auto &[name, g] : globals) {
    if (!g.reason.empty()) {
      if (remarks)
        g.op.emitRemark() << "lcc-globals-to-memref: @" << name.getValue()
                          << " stays: " << g.reason;
      continue;
    }
    int64_t n = g.size / elementSize(g.elemType);
    auto type = MemRefType::get({n}, g.elemType);
    builder.setInsertionPoint(g.op);
    StringAttr visibility;
    if (g.op.getLinkage() == LLVM::Linkage::Internal)
      visibility = builder.getStringAttr("private");
    IntegerAttr alignment;
    if (std::optional<uint64_t> a = g.op.getAlignment(); a && *a)
      alignment = builder.getI64IntegerAttr(*a);
    auto newGlobal = memref::GlobalOp::create(
        builder, g.op.getLoc(), name.getValue(), visibility, type,
        initialValue(g, RankedTensorType::get({n}, g.elemType)),
        g.op.getConstant(), alignment);
    if (remarks)
      newGlobal.emitRemark() << "lcc-globals-to-memref: @" << name.getValue()
                             << " becomes " << type;
    g.op.erase();

    Rewriter rewriter(g, layout, indexWidth, builder);
    for (auto [addr, accesses, geps] :
         llvm::zip(g.addrs, g.accesses, g.geps)) {
      builder.setInsertionPoint(addr);
      Location loc = addr.getLoc();
      Value memref = memref::GetGlobalOp::create(
          builder, loc, type, FlatSymbolRefAttr::get(name));
      for (Operation *access : accesses)
        rewriter.convert(access, addr, memref);
      for (LLVM::GEPOp gep : llvm::reverse(geps))
        if (gep->use_empty())
          gep.erase();
      if (!addr->use_empty()) {
        // The other uses take a pointer made from the memref.
        builder.setInsertionPoint(addr);
        Value p = memref::ExtractAlignedPointerAsIndexOp::create(
            builder, loc, memref);
        p = arith::IndexCastOp::create(builder, loc, builder.getI64Type(), p);
        p = LLVM::IntToPtrOp::create(builder, loc, addr.getType(), p);
        addr.getResult().replaceAllUsesWith(p);
      }
      addr.erase();
      if (memref.use_empty())
        memref.getDefiningOp()->erase();
    }
  }
}
