//===- SplitAggregates.cpp - Scalar allocas for lcc's byte arrays ---------===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// lcc-split-aggregates: see Passes.td.  lcc gives a local structure or
// array an `llvm.alloca 1 x !llvm.array<N x i8>` and reaches its members
// with `llvm.getelementptr %a[off] ... i8` and scalar loads and stores.
// sroa splits an alloca only along its element type, here bytes, which
// these loads and stores cross, so it leaves them; this pass splits such an
// alloca into one scalar alloca per member that mem2reg can promote.
//
// An alloca qualifies when every use, through getelementptrs of i8 with a
// constant index, is
//   - a non-volatile, non-atomic load or store of an integer, float or
//     pointer at a constant offset (a store of the pointer itself does not
//     count), or
//   - a non-volatile llvm.intr.memcpy of a constant length into or out of
//     it from another pointer,
// all within the alloca, and the loads and stores either overlap in no byte
// or are at the same offset with the same type.  Each such offset and type
// becomes a slot, a scalar alloca, unless a memcpy range covers it in part.
// The bytes of a memcpy range that no load or store covers become filler
// slots, integers of at most 8 bytes, so that a memcpy, replaced by a load
// and a store per slot in its range, still copies every byte.  An alloca
// with no load or store, only copied, is left alone.
//
//===----------------------------------------------------------------------===//

#include "lcc-opt/Passes.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/LLVMIR/LLVMTypes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Matchers.h"
#include "llvm/ADT/STLExtras.h"

#include <algorithm>
#include <map>
#include <optional>

namespace lcc {
#define GEN_PASS_DEF_LCCSPLITAGGREGATES
#include "lcc-opt/Passes.h.inc"
} // namespace lcc

using namespace mlir;

namespace {

/// A load or store at a constant offset.
struct Access {
  Operation *op;
  int64_t offset;
  Type type;
  int64_t size;
};

/// A memcpy into or out of the alloca at a constant offset.
struct Copy {
  LLVM::MemcpyOp op;
  int64_t offset;
  int64_t size;
  bool into;
};

/// A slot of the split alloca.
struct Slot {
  int64_t offset;
  Type type;
  int64_t size;
  Value alloca;
};

/// What the uses of an alloca are.
struct Uses {
  SmallVector<Access> accesses;
  SmallVector<Copy> copies;
  /// The getelementptrs, each before those based on it.
  SmallVector<Operation *> geps;
};

} // namespace

/// Returns the size in bytes of a pointer by the llvm.data_layout of the
/// module of `op` ("p:32:32", "p0:64:64"), or 8.
static int64_t pointerSize(Operation *op) {
  auto module = op->getParentOfType<ModuleOp>();
  auto layout =
      module ? module->getAttrOfType<StringAttr>("llvm.data_layout") : nullptr;
  if (layout) {
    SmallVector<StringRef> specs;
    layout.getValue().split(specs, '-');
    for (StringRef spec : specs) {
      if (!spec.consume_front("p:") && !spec.consume_front("p0:"))
        continue;
      unsigned bits;
      if (!spec.split(':').first.getAsInteger(10, bits))
        return (bits + 7) / 8;
    }
  }
  return 8;
}

/// Returns the number of bytes a load or store of `type` touches, for the
/// types a slot may have.
static std::optional<int64_t> storeSize(Type type, int64_t pointer) {
  if (auto i = dyn_cast<IntegerType>(type))
    return (i.getWidth() + 7) / 8;
  if (auto f = dyn_cast<FloatType>(type))
    return (f.getWidth() + 7) / 8;
  if (isa<LLVM::LLVMPointerType>(type))
    return pointer;
  return std::nullopt;
}

/// Returns the value `p` is a getelementptr of, through any number of them.
static Value rootOf(Value p) {
  while (auto gep = p.getDefiningOp<LLVM::GEPOp>())
    p = gep.getBase();
  return p;
}

/// Adds to `uses` the uses of `ptr`, `offset` bytes into an alloca of `size`
/// bytes, and returns true, or returns false when a use does not qualify.
static bool collect(Value ptr, int64_t offset, int64_t size, Value alloca,
                    int64_t pointer, Uses &uses) {
  auto inside = [&](int64_t off, int64_t len) {
    return off >= 0 && len >= 0 && off + len <= size;
  };
  for (OpOperand &use : ptr.getUses()) {
    Operation *user = use.getOwner();
    if (auto gep = dyn_cast<LLVM::GEPOp>(user)) {
      if (use.getOperandNumber() != 0 || !gep.getElemType().isInteger(8))
        return false;
      ArrayRef<int32_t> raw = gep.getRawConstantIndices();
      if (raw.size() != 1)
        return false;
      int64_t index = raw[0];
      if (index == LLVM::GEPOp::kDynamicIndex) {
        APInt value;
        if (!matchPattern(gep.getDynamicIndices()[0], m_ConstantInt(&value)))
          return false;
        index = value.getSExtValue();
      }
      uses.geps.push_back(gep);
      if (!collect(gep.getRes(), offset + index, size, alloca, pointer, uses))
        return false;
      continue;
    }
    if (auto load = dyn_cast<LLVM::LoadOp>(user)) {
      std::optional<int64_t> n = storeSize(load.getType(), pointer);
      if (load.getVolatile_() ||
          load.getOrdering() != LLVM::AtomicOrdering::not_atomic || !n ||
          !inside(offset, *n))
        return false;
      uses.accesses.push_back({load, offset, load.getType(), *n});
      continue;
    }
    if (auto store = dyn_cast<LLVM::StoreOp>(user)) {
      Type type = store.getValue().getType();
      std::optional<int64_t> n = storeSize(type, pointer);
      if (store.getValue() == ptr || store.getVolatile_() ||
          store.getOrdering() != LLVM::AtomicOrdering::not_atomic || !n ||
          !inside(offset, *n))
        return false;
      uses.accesses.push_back({store, offset, type, *n});
      continue;
    }
    if (auto copy = dyn_cast<LLVM::MemcpyOp>(user)) {
      APInt length;
      bool into = copy.getDst() == ptr, from = copy.getSrc() == ptr;
      if (copy.getIsVolatile() || into == from ||
          !matchPattern(copy.getLen(), m_ConstantInt(&length)) ||
          rootOf(into ? copy.getSrc() : copy.getDst()) == alloca ||
          !inside(offset, length.getSExtValue()))
        return false;
      uses.copies.push_back({copy, offset, length.getSExtValue(), into});
      continue;
    }
    return false;
  }
  return true;
}

/// Returns the slots for `uses` of an alloca, or nothing when the loads and
/// stores overlap or a memcpy covers a slot in part.
static std::optional<SmallVector<Slot>> plan(const Uses &uses,
                                             MLIRContext *context) {
  // Slots of the loads and stores, by offset.
  std::map<int64_t, Slot> slots;
  for (const Access &a : uses.accesses) {
    auto [it, added] =
        slots.insert({a.offset, Slot{a.offset, a.type, a.size, Value()}});
    if (!added && (it->second.type != a.type))
      return std::nullopt;
  }
  int64_t end = INT64_MIN;
  for (auto &[offset, slot] : slots) {
    if (offset < end)
      return std::nullopt;
    end = offset + slot.size;
  }
  // No memcpy may cover a slot in part.
  for (const Copy &c : uses.copies)
    for (auto &[offset, slot] : slots)
      if (offset < c.offset + c.size && offset + slot.size > c.offset &&
          (offset < c.offset || offset + slot.size > c.offset + c.size))
        return std::nullopt;
  // Filler slots for the bytes of memcpy ranges no slot covers, cut at
  // every bound of a range or slot, so that each lies in a range or out of
  // it.
  SmallVector<int64_t> bounds;
  for (const Copy &c : uses.copies) {
    bounds.push_back(c.offset);
    bounds.push_back(c.offset + c.size);
  }
  for (auto &[offset, slot] : slots) {
    bounds.push_back(offset);
    bounds.push_back(offset + slot.size);
  }
  llvm::sort(bounds);
  bounds.erase(std::unique(bounds.begin(), bounds.end()), bounds.end());
  SmallVector<Slot> fillers;
  for (size_t i = 0; i + 1 < bounds.size(); i++) {
    int64_t lo = bounds[i], hi = bounds[i + 1];
    bool copied = llvm::any_of(uses.copies, [&](const Copy &c) {
      return c.offset <= lo && hi <= c.offset + c.size;
    });
    bool covered = false;
    for (auto &[offset, slot] : slots)
      covered |= offset <= lo && hi <= offset + slot.size;
    if (!copied || covered)
      continue;
    for (int64_t at = lo; at < hi; at += 8) {
      int64_t n = std::min<int64_t>(8, hi - at);
      fillers.push_back(
          Slot{at, IntegerType::get(context, 8 * n), n, Value()});
    }
  }
  SmallVector<Slot> result;
  for (auto &[offset, slot] : slots)
    result.push_back(slot);
  llvm::append_range(result, fillers);
  llvm::sort(result,
             [](const Slot &a, const Slot &b) { return a.offset < b.offset; });
  return result;
}

/// Returns the alignment known at `offset` bytes from a pointer aligned to
/// `align`.
static uint64_t alignAt(uint64_t align, int64_t offset) {
  if (offset == 0)
    return align;
  uint64_t low = uint64_t(offset) & -uint64_t(offset);
  return std::min(align, low);
}

/// Returns the llvm.align of argument `n` of `copy`, or 1.
static uint64_t copyAlign(LLVM::MemcpyOp copy, unsigned n) {
  if (ArrayAttr attrs = copy.getArgAttrsAttr())
    if (n < attrs.size())
      if (auto dict = dyn_cast<DictionaryAttr>(attrs[n]))
        if (auto align = dict.getAs<IntegerAttr>(
                LLVM::LLVMDialect::getAlignAttrName()))
          return align.getValue().getZExtValue();
  return 1;
}

/// Splits `alloca` if it qualifies; returns true if it did.
static bool split(LLVM::AllocaOp alloca, int64_t pointer) {
  auto array = dyn_cast<LLVM::LLVMArrayType>(alloca.getElemType());
  if (!array || !array.getElementType().isInteger(8) ||
      !matchPattern(alloca.getArraySize(), m_One()))
    return false;
  Uses uses;
  if (!collect(alloca.getResult(), 0, array.getNumElements(),
               alloca.getResult(), pointer, uses) ||
      uses.accesses.empty())
    return false;
  std::optional<SmallVector<Slot>> slots =
      plan(uses, alloca.getContext());
  if (!slots || slots->size() > 64)
    return false;

  Location loc = alloca.getLoc();
  OpBuilder builder(alloca);
  auto ptrType = LLVM::LLVMPointerType::get(alloca.getContext());
  for (Slot &slot : *slots)
    slot.alloca = LLVM::AllocaOp::create(builder, loc, ptrType, slot.type,
                                         alloca.getArraySize());
  auto slotAt = [&](int64_t offset) -> Slot & {
    return *llvm::find_if(*slots,
                          [&](const Slot &s) { return s.offset == offset; });
  };
  for (const Access &a : uses.accesses) {
    Value slot = slotAt(a.offset).alloca;
    if (auto load = dyn_cast<LLVM::LoadOp>(a.op))
      load.getAddrMutable().assign(slot);
    else
      cast<LLVM::StoreOp>(a.op).getAddrMutable().assign(slot);
  }
  for (const Copy &c : uses.copies) {
    LLVM::MemcpyOp copy = c.op;
    builder.setInsertionPoint(copy);
    Value other = c.into ? copy.getSrc() : copy.getDst();
    uint64_t align = copyAlign(copy, c.into ? 1 : 0);
    for (Slot &slot : *slots) {
      if (slot.offset < c.offset || slot.offset + slot.size > c.offset + c.size)
        continue;
      int64_t at = slot.offset - c.offset;
      Value p = other;
      if (at != 0)
        p = LLVM::GEPOp::create(builder, copy.getLoc(), ptrType,
                                builder.getI8Type(), other,
                                ArrayRef<LLVM::GEPArg>{int32_t(at)});
      unsigned a = alignAt(align, at);
      if (c.into) {
        Value v = LLVM::LoadOp::create(builder, copy.getLoc(), slot.type, p, a);
        LLVM::StoreOp::create(builder, copy.getLoc(), v, slot.alloca);
      } else {
        Value v = LLVM::LoadOp::create(builder, copy.getLoc(), slot.type,
                                       slot.alloca);
        LLVM::StoreOp::create(builder, copy.getLoc(), v, p, a);
      }
    }
    copy.erase();
  }
  for (Operation *gep : llvm::reverse(uses.geps))
    gep->erase();
  alloca.erase();
  return true;
}

namespace {

struct SplitAggregatesPass
    : public lcc::impl::LccSplitAggregatesBase<SplitAggregatesPass> {
  void runOnOperation() override {
    int64_t pointer = pointerSize(getOperation());
    bool changed = false;
    // A memcpy replaced by loads and stores may let another alloca qualify.
    for (bool again = true; again;) {
      again = false;
      SmallVector<LLVM::AllocaOp> allocas;
      getOperation()->walk(
          [&](LLVM::AllocaOp alloca) { allocas.push_back(alloca); });
      for (LLVM::AllocaOp alloca : allocas)
        if (split(alloca, pointer))
          again = changed = true;
    }
    if (!changed)
      markAllAnalysesPreserved();
  }
};

} // namespace
