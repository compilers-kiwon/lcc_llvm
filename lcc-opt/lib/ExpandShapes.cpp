//===- ExpandShapes.cpp - View 1-D global memrefs with their shape --------===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// lcc-expand-shapes: see Passes.td.  Also getLinearExpr (see Passes.h),
// which lcc-distribute-reductions and lcc-affine-to-linalg share.
//
// For each func.func and each global it reaches through memref.get_global,
// the pass collects the accesses of the global, finds the strides of their
// affine indices and splits every affine index into the indices of the
// shape those strides make, before it changes anything.  Then it creates
// the view at the start of the function and replaces the accesses.
//
//===----------------------------------------------------------------------===//

#include "lcc-opt/Passes.h"

#include "mlir/Dialect/Affine/IR/AffineOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/IR/AffineExpr.h"
#include "mlir/IR/AffineMap.h"
#include "mlir/IR/Builders.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/Support/CheckedArithmetic.h"

#include <string>

namespace lcc {
#define GEN_PASS_DEF_LCCEXPANDSHAPES
#include "lcc-opt/Passes.h.inc"
} // namespace lcc

using namespace mlir;

//===----------------------------------------------------------------------===//
// getLinearExpr
//===----------------------------------------------------------------------===//

/// Adds `k * e` to `r`; fails on what is not linear and on overflow.
static bool addLinear(lcc::LinearExpr &r, AffineExpr e, int64_t k) {
  auto add = [&](int64_t &to, int64_t c) {
    std::optional<int64_t> m = llvm::checkedMul(c, k);
    std::optional<int64_t> s = m ? llvm::checkedAdd(to, *m) : std::nullopt;
    if (!s)
      return false;
    to = *s;
    return true;
  };
  if (auto c = dyn_cast<AffineConstantExpr>(e))
    return add(r.constant, c.getValue());
  if (auto d = dyn_cast<AffineDimExpr>(e))
    return d.getPosition() < r.dims.size() && add(r.dims[d.getPosition()], 1);
  if (auto s = dyn_cast<AffineSymbolExpr>(e))
    return s.getPosition() < r.symbols.size() &&
           add(r.symbols[s.getPosition()], 1);
  auto bin = dyn_cast<AffineBinaryOpExpr>(e);
  if (!bin)
    return false;
  if (e.getKind() == AffineExprKind::Add)
    return addLinear(r, bin.getLHS(), k) && addLinear(r, bin.getRHS(), k);
  if (e.getKind() != AffineExprKind::Mul)
    return false;
  AffineExpr x = bin.getLHS(), y = bin.getRHS();
  if (!isa<AffineConstantExpr>(y))
    std::swap(x, y);
  auto c = dyn_cast<AffineConstantExpr>(y);
  if (!c)
    return false;
  std::optional<int64_t> m = llvm::checkedMul(c.getValue(), k);
  return m && addLinear(r, x, *m);
}

std::optional<lcc::LinearExpr>
lcc::getLinearExpr(AffineExpr expr, unsigned numDims, unsigned numSymbols) {
  LinearExpr r;
  r.dims.assign(numDims, 0);
  r.symbols.assign(numSymbols, 0);
  if (!addLinear(r, expr, 1))
    return std::nullopt;
  return r;
}

//===----------------------------------------------------------------------===//
// lcc-expand-shapes
//===----------------------------------------------------------------------===//

namespace {

/// A term of an affine index: a dimension or symbol of the access map, its
/// coefficient and the range of its operand.
struct Term {
  AffineExpr atom;
  int64_t coef;
  lcc::IntRange range;
};

/// An affine access of a global and its index.
struct AffineAccess {
  Operation *op;
  /// The outermost affine.for around the access.
  Operation *nest;
  int64_t constant = 0;
  SmallVector<Term> terms;
  /// The indices in the new shape.
  SmallVector<AffineExpr> exprs;
};

/// A global in a function.
struct Global {
  StringAttr name;
  MemRefType type;
  SmallVector<memref::GetGlobalOp> gets;
  SmallVector<AffineAccess> affineAccesses;
  /// memref.load and memref.store.
  SmallVector<Operation *> otherAccesses;
  /// The strides, from the largest, and the shape.
  SmallVector<int64_t> strides, shape;
  /// Why the global stays 1-D, empty if it changes.
  std::string reason;
};

} // namespace

/// The strides of the terms of `accesses` and 1, from the largest, when each
/// divides the one before and the first divides `size`; nothing otherwise,
/// with the reason.
static std::optional<SmallVector<int64_t>>
getStrides(ArrayRef<const AffineAccess *> accesses, int64_t size,
           std::string &reason) {
  SmallVector<int64_t> strides{1};
  for (const AffineAccess *a : accesses)
    for (const Term &t : a->terms) {
      if (t.coef == INT64_MIN) {
        reason = "index with a coefficient out of range";
        return std::nullopt;
      }
      int64_t s = std::abs(t.coef);
      if (!llvm::is_contained(strides, s))
        strides.push_back(s);
    }
  llvm::sort(strides, std::greater<int64_t>());
  if (strides[0] >= size) {
    reason = "index with a coefficient of " + std::to_string(strides[0]) +
             ", not less than the size";
    return std::nullopt;
  }
  if (size % strides[0]) {
    reason = "index with a coefficient of " + std::to_string(strides[0]) +
             ", not a divisor of the size";
    return std::nullopt;
  }
  for (unsigned i = 1; i < strides.size(); i++)
    if (strides[i - 1] % strides[i]) {
      reason = "indices with coefficients " + std::to_string(strides[i - 1]) +
               " and " + std::to_string(strides[i]) + ", which do not nest";
      return std::nullopt;
    }
  return strides;
}

/// The shape of `size` elements with `strides`.
static SmallVector<int64_t> getShape(int64_t size, ArrayRef<int64_t> strides) {
  SmallVector<int64_t> shape{size / strides[0]};
  for (unsigned i = 1; i < strides.size(); i++)
    shape.push_back(strides[i - 1] / strides[i]);
  return shape;
}

/// The indices of `a` in `shape` with `strides`: each term goes to the
/// outermost dimension whose stride divides its coefficient, and the
/// constant is split so that every dimension but the first stays in
/// [0, size) over the ranges of the terms; and the first must too.  Nothing
/// when that cannot be shown, with the reason.
static std::optional<SmallVector<AffineExpr>>
split(const AffineAccess &a, ArrayRef<int64_t> shape,
      ArrayRef<int64_t> strides, MLIRContext *ctx, std::string &reason) {
  reason = "an index of too large a range";
  unsigned rank = shape.size();
  SmallVector<AffineExpr> exprs(rank, getAffineConstantExpr(0, ctx));
  SmallVector<int64_t> lo(rank, 0), hi(rank, 0);
  for (const Term &t : a.terms) {
    unsigned d = 0;
    while (t.coef % strides[d])
      d++;
    int64_t b = t.coef / strides[d];
    std::optional<int64_t> x = llvm::checkedMul(b, t.range.min);
    std::optional<int64_t> y = llvm::checkedMul(b, t.range.max);
    if (!x || !y)
      return std::nullopt;
    std::optional<int64_t> l = llvm::checkedAdd(lo[d], std::min(*x, *y));
    std::optional<int64_t> h = llvm::checkedAdd(hi[d], std::max(*x, *y));
    if (!l || !h)
      return std::nullopt;
    lo[d] = *l;
    hi[d] = *h;
    exprs[d] = exprs[d] + t.atom * b;
  }
  // The constant, from the innermost dimension: there c must be congruent
  // to what is left of the constant modulo the size, and the smallest such
  // c with c + lo >= 0 must have c + hi < size.
  int64_t rest = a.constant;
  for (unsigned d = rank - 1; d > 0; d--) {
    int64_t n = shape[d];
    if (lo[d] == INT64_MIN)
      return std::nullopt;
    int64_t from = -lo[d];
    std::optional<int64_t> to = llvm::checkedSub(n - 1, hi[d]);
    std::optional<int64_t> diff = llvm::checkedSub(rest, from);
    if (!to || !diff || from > *to)
      return std::nullopt;
    int64_t m = *diff % n;
    if (m < 0)
      m += n;
    std::optional<int64_t> c = llvm::checkedAdd(from, m);
    if (!c)
      return std::nullopt;
    if (*c > *to) {
      reason = "an index may leave its row";
      return std::nullopt;
    }
    std::optional<int64_t> left = llvm::checkedSub(rest, *c);
    if (!left)
      return std::nullopt;
    exprs[d] = exprs[d] + *c;
    rest = *left / n;
  }
  std::optional<int64_t> l = llvm::checkedAdd(lo[0], rest);
  std::optional<int64_t> h = llvm::checkedAdd(hi[0], rest);
  if (!l || !h || *l < 0 || *h >= shape[0]) {
    reason = "an index may be out of bounds";
    return std::nullopt;
  }
  exprs[0] = exprs[0] + rest;
  return exprs;
}

/// Finds the strides, the shape and the new indices of `accesses`, with
/// the reason when there are none.
static bool plan(ArrayRef<AffineAccess *> accesses, int64_t size,
                 SmallVector<int64_t> &strides, SmallVector<int64_t> &shape,
                 std::string &reason, MLIRContext *ctx) {
  SmallVector<const AffineAccess *> all(accesses.begin(), accesses.end());
  std::optional<SmallVector<int64_t>> s = getStrides(all, size, reason);
  if (!s)
    return false;
  strides = *s;
  shape = getShape(size, strides);
  for (AffineAccess *a : accesses) {
    std::optional<SmallVector<AffineExpr>> exprs =
        split(*a, shape, strides, ctx, reason);
    if (!exprs)
      return false;
    a->exprs = *exprs;
  }
  return true;
}

/// Collects the accesses of `g` and the terms of its affine indices, or
/// sets the reason.
static void analyze(Global &g, lcc::LinearAnalysis &la) {
  for (memref::GetGlobalOp get : g.gets) {
    for (OpOperand &use : get.getResult().getUses()) {
      Operation *user = use.getOwner();
      if (isa<memref::ExtractAlignedPointerAsIndexOp>(user))
        continue;
      if (auto load = dyn_cast<memref::LoadOp>(user);
          load && load.getIndices().size() == 1) {
        g.otherAccesses.push_back(user);
        continue;
      }
      if (auto store = dyn_cast<memref::StoreOp>(user);
          store && store.getMemRef() == get.getResult() &&
          store.getValueToStore() != get.getResult() &&
          store.getIndices().size() == 1) {
        g.otherAccesses.push_back(user);
        continue;
      }
      AffineMap map;
      ValueRange operands;
      if (auto load = dyn_cast<affine::AffineLoadOp>(user)) {
        map = load.getAffineMap();
        operands = load.getMapOperands();
      } else if (auto store = dyn_cast<affine::AffineStoreOp>(user);
                 store && store.getMemRef() == get.getResult() &&
                 store.getValueToStore() != get.getResult()) {
        map = store.getAffineMap();
        operands = store.getMapOperands();
      } else {
        g.reason = "used by " + user->getName().getStringRef().str();
        return;
      }
      AffineAccess a;
      a.op = user;
      a.nest = user;
      for (Operation *p = user->getParentOp(); p; p = p->getParentOp())
        if (isa<affine::AffineForOp>(p))
          a.nest = p;
      if (map.getNumResults() != 1) {
        g.reason = "not a 1-D access";
        return;
      }
      std::optional<lcc::LinearExpr> lin = lcc::getLinearExpr(
          map.getResult(0), map.getNumDims(), map.getNumSymbols());
      if (!lin) {
        g.reason = "an index that is not linear";
        return;
      }
      a.constant = lin->constant;
      unsigned numDims = map.getNumDims();
      auto addTerm = [&](AffineExpr atom, int64_t coef, Value operand) {
        if (!coef)
          return true;
        std::optional<lcc::IntRange> r = la.getRange(operand);
        if (!r)
          return false;
        a.terms.push_back({atom, coef, *r});
        return true;
      };
      MLIRContext *ctx = user->getContext();
      for (unsigned i = 0; i < numDims; i++)
        if (!addTerm(getAffineDimExpr(i, ctx), lin->dims[i], operands[i])) {
          g.reason = "an index of unknown range";
          return;
        }
      for (unsigned i = 0; i < map.getNumSymbols(); i++)
        if (!addTerm(getAffineSymbolExpr(i, ctx), lin->symbols[i],
                     operands[numDims + i])) {
          g.reason = "an index of unknown range";
          return;
        }
      g.affineAccesses.push_back(std::move(a));
    }
  }
}

/// Plans the shape of `g`, or sets the reason.  When the accesses of all
/// nests together make no shape, finds whether the accesses of each nest
/// alone do, to say why.
static void plan(Global &g, MLIRContext *ctx) {
  int64_t size = g.type.getDimSize(0);
  SmallVector<AffineAccess *> all;
  for (AffineAccess &a : g.affineAccesses)
    all.push_back(&a);
  std::string reason;
  if (plan(all, size, g.strides, g.shape, reason, ctx))
    return;
  // Each nest alone.
  llvm::MapVector<Operation *, SmallVector<AffineAccess *>> nests;
  for (AffineAccess &a : g.affineAccesses)
    nests[a.nest].push_back(&a);
  bool each = nests.size() > 1;
  for (auto &[nest, accesses] : nests) {
    SmallVector<int64_t> strides, shape;
    std::string r;
    if (!plan(accesses, size, strides, shape, r, ctx))
      each = false;
  }
  g.reason = each ? "nests need different shapes" : reason;
}

/// The indices of element `x` of a 1-D memref in `shape` with `strides`,
/// created before `op`: x floordiv s_1, (x mod s_1) floordiv s_2, ...,
/// x mod s_(k-1), which are the element for every x.
static SmallVector<Value> splitIndex(OpBuilder &b, Location loc, Value x,
                                     ArrayRef<int64_t> strides) {
  SmallVector<Value> indices;
  MLIRContext *ctx = b.getContext();
  unsigned rank = strides.size();
  for (unsigned d = 0; d < rank; d++) {
    AffineExpr e = getAffineDimExpr(0, ctx);
    if (d > 0)
      e = e % strides[d - 1];
    if (d + 1 < rank)
      e = e.floorDiv(strides[d]);
    indices.push_back(affine::AffineApplyOp::create(
        b, loc, AffineMap::get(1, 0, e), ValueRange{x}));
  }
  return indices;
}

/// Makes every access of `g` an access of a view of its shape.
static void expand(Global &g, func::FuncOp func) {
  Block &entry = func.getBody().front();
  OpBuilder b(func.getContext());
  b.setInsertionPointToStart(&entry);
  Location loc = g.gets.front().getLoc();
  Value memref = memref::GetGlobalOp::create(b, loc, g.type, g.name);
  auto viewType = MemRefType::get(g.shape, g.type.getElementType());
  SmallVector<ReassociationIndices> reassociation(1);
  for (unsigned d = 0; d < g.shape.size(); d++)
    reassociation[0].push_back(d);
  Value view = memref::ExpandShapeOp::create(b, loc, viewType, memref,
                                             reassociation);

  for (AffineAccess &a : g.affineAccesses) {
    b.setInsertionPoint(a.op);
    if (auto load = dyn_cast<affine::AffineLoadOp>(a.op)) {
      AffineMap old = load.getAffineMap();
      AffineMap map = AffineMap::get(old.getNumDims(), old.getNumSymbols(),
                                     a.exprs, b.getContext());
      Value v = affine::AffineLoadOp::create(b, load.getLoc(), view, map,
                                             load.getMapOperands());
      load.getResult().replaceAllUsesWith(v);
      load.erase();
    } else {
      auto store = cast<affine::AffineStoreOp>(a.op);
      AffineMap old = store.getAffineMap();
      AffineMap map = AffineMap::get(old.getNumDims(), old.getNumSymbols(),
                                     a.exprs, b.getContext());
      affine::AffineStoreOp::create(b, store.getLoc(),
                                    store.getValueToStore(), view, map,
                                    store.getMapOperands());
      store.erase();
    }
  }
  // memref.load and memref.store keep their attributes: the operands of a
  // clone change.
  for (Operation *op : g.otherAccesses) {
    b.setInsertionPoint(op);
    SmallVector<Value> operands;
    Value index;
    if (auto load = dyn_cast<memref::LoadOp>(op)) {
      index = load.getIndices()[0];
    } else {
      auto store = cast<memref::StoreOp>(op);
      index = store.getIndices()[0];
      operands.push_back(store.getValueToStore());
    }
    operands.push_back(view);
    llvm::append_range(operands,
                       splitIndex(b, op->getLoc(), index, g.strides));
    Operation *access = b.clone(*op);
    access->setOperands(operands);
    op->replaceAllUsesWith(access);
    op->erase();
  }
  for (memref::GetGlobalOp get : g.gets)
    if (get->use_empty())
      get.erase();
}

namespace {

struct ExpandShapesPass
    : public lcc::impl::LccExpandShapesBase<ExpandShapesPass> {
  using LccExpandShapesBase::LccExpandShapesBase;

  void runOnOperation() override {
    getOperation()->walk([&](func::FuncOp func) { runOnFunction(func); });
  }

  void runOnFunction(func::FuncOp func);
};

} // namespace

void ExpandShapesPass::runOnFunction(func::FuncOp func) {
  if (func.getBody().empty())
    return;
  lcc::LinearAnalysis la(lcc::getIndexWidth(func));
  // The globals, in the order of their first memref.get_global.
  llvm::MapVector<StringAttr, Global> globals;
  func.walk([&](memref::GetGlobalOp get) {
    Global &g = globals[get.getNameAttr().getAttr()];
    g.name = get.getNameAttr().getAttr();
    g.type = get.getType();
    g.gets.push_back(get);
  });
  for (auto &[name, g] : globals) {
    if (g.type.getRank() != 1 || !g.type.hasStaticShape() ||
        !g.type.getLayout().isIdentity())
      continue;
    analyze(g, la);
    if (!g.reason.empty()) {
      if (remarks)
        func.emitRemark() << "lcc-expand-shapes: @" << name.getValue()
                          << " stays 1-D in @" << func.getSymName() << ": "
                          << g.reason;
      continue;
    }
    if (g.affineAccesses.empty())
      continue;
    plan(g, &getContext());
    if (!g.reason.empty()) {
      if (remarks)
        func.emitRemark() << "lcc-expand-shapes: @" << name.getValue()
                          << " stays 1-D in @" << func.getSymName() << ": "
                          << g.reason;
      continue;
    }
    if (g.shape.size() < 2)
      continue;
    if (remarks)
      func.emitRemark() << "lcc-expand-shapes: @" << name.getValue()
                        << " becomes "
                        << MemRefType::get(g.shape, g.type.getElementType())
                        << " in @" << func.getSymName();
  }
  // The changes, last global first, so that the views are at the start of
  // the function in the order of the globals.
  for (auto &[name, g] : llvm::reverse(globals))
    if (g.reason.empty() && g.shape.size() >= 2)
      expand(g, func);
}
