//===- DistributeReductions.cpp - Split nests around a reduction loop -----===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// lcc-distribute-reductions: see Passes.td.  The pass finds the reduction
// loops it may distribute around before it changes anything, then builds
// for each three copies of the loops around it (the band): the first holds
// the operations before the reduction loop and stores the initial values
// and the values the later nests need into buffers, the second the
// reduction loop without iter_args, which loads its accumulators from the
// buffers and stores them back in every iteration, and the third loads the
// results and holds the operations after the reduction loop.  Then it
// erases the original loops.
//
//===----------------------------------------------------------------------===//

#include "lcc-opt/Passes.h"

#include "mlir/Dialect/Affine/Analysis/AffineAnalysis.h"
#include "mlir/Dialect/Affine/Analysis/Utils.h"
#include "mlir/Dialect/Affine/IR/AffineOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Support/CheckedArithmetic.h"

#include <string>

namespace lcc {
#define GEN_PASS_DEF_LCCDISTRIBUTEREDUCTIONS
#include "lcc-opt/Passes.h.inc"
} // namespace lcc

using namespace mlir;
using affine::AffineForOp;

namespace {

/// A reduction loop to distribute the loops around.
struct Candidate {
  AffineForOp reduction;
  /// The reduction loop and the reduction loops perfectly nested in it
  /// that carry its iter_args on: its chain, outermost first.
  SmallVector<AffineForOp> chain;
  /// The loops around it, outermost first; the band.
  SmallVector<AffineForOp> band;
  /// The operations before and after the reduction loop in the innermost
  /// loop of the band.
  SmallVector<Operation *> pre, post;
  /// The values of `pre` the reduction loop (except as its initial values)
  /// and `post` use: recomputed by cloning the operations of `remat`, or
  /// stored into buffers.
  llvm::SetVector<Value> reductionUses, postUses;
  /// The operations of `pre` to clone before the reduction loop and before
  /// `post`, in their order.
  SmallVector<Operation *> rematReduction, rematPost;
  /// The values of `pre` that get a buffer.
  llvm::SetVector<Value> expanded;
  /// The iteration counts of the band.
  SmallVector<int64_t> counts;
  int64_t bytes = 0;
};

} // namespace

/// Whether `v` is defined in the block `block`, directly.
static bool isDefinedInBlock(Value v, Block *block) {
  if (auto arg = dyn_cast<BlockArgument>(v))
    return arg.getOwner() == block;
  return v.getDefiningOp()->getBlock() == block;
}

/// The memref `v` is a view of: the source of view-like operations.
static Value getRoot(Value v) {
  while (Operation *def = v.getDefiningOp()) {
    if (auto view = dyn_cast<ViewLikeOpInterface>(def)) {
      v = view.getViewSource();
      continue;
    }
    break;
  }
  return v;
}

/// Whether the memory of the memref root `v` is known: a global or an
/// alloca, which no other memref of the function aliases unless it is a
/// view of the same memref or another memref.get_global of the same global.
static bool isKnownMemory(Value v) {
  return v.getDefiningOp<memref::GetGlobalOp>() ||
         v.getDefiningOp<memref::AllocaOp>();
}

/// Whether two memref roots are the same memory: the same value, or
/// memref.get_global of the same global.
static bool sameMemory(Value a, Value b) {
  if (a == b)
    return true;
  auto ga = a.getDefiningOp<memref::GetGlobalOp>();
  auto gb = b.getDefiningOp<memref::GetGlobalOp>();
  return ga && gb && ga.getNameAttr() == gb.getNameAttr();
}

/// The name of `type`, for the remarks.
static std::string typeName(Type type) {
  std::string s;
  llvm::raw_string_ostream os(s);
  os << type;
  return s;
}

/// Whether values of `type` may be stored in a buffer.
static bool isBufferType(Type type) {
  return isa<IntegerType, IndexType, FloatType>(type);
}

namespace {

class Analyzer {
public:
  Analyzer(int64_t budget, const DataLayout &layout)
      : budget(budget), layout(layout) {}

  /// Whether the loops around `reduction` may be distributed; fills `c`, or
  /// sets `reason`.
  bool analyze(AffineForOp reduction, Candidate &c);

  std::string reason;

private:
  bool fail(const Twine &why) {
    reason = why.str();
    return false;
  }
  bool findBand(Candidate &c);
  bool findUses(Candidate &c);
  bool checkDependences(Candidate &c);

  int64_t budget;
  const DataLayout &layout;
};

} // namespace

bool Analyzer::findBand(Candidate &c) {
  auto inner = dyn_cast<AffineForOp>(c.reduction->getParentOp());
  if (!inner)
    return fail("not in an affine.for");
  c.band.push_back(inner);
  // Outer loops whose bodies hold only side effect free operations without
  // regions and the next loop.
  while (auto outer = dyn_cast<AffineForOp>(c.band.front()->getParentOp())) {
    bool perfect = llvm::all_of(
        outer.getBody()->without_terminator(), [&](Operation &op) {
          return &op == c.band.front().getOperation() ||
                 (op.getNumRegions() == 0 && isMemoryEffectFree(&op));
        });
    if (!perfect)
      break;
    c.band.insert(c.band.begin(), outer);
  }
  for (AffineForOp loop : c.band) {
    if (loop.getNumIterOperands())
      return fail("a loop around it has iter_args");
    if (!loop.hasConstantBounds())
      return fail("a loop around it has bounds that are not constant");
    int64_t lb = loop.getConstantLowerBound();
    int64_t ub = loop.getConstantUpperBound();
    int64_t step = loop.getStepAsInt();
    if (ub <= lb)
      return fail("a loop around it has no iterations");
    std::optional<int64_t> range = llvm::checkedSub(ub, lb);
    std::optional<int64_t> up =
        range ? llvm::checkedAdd(*range, step - 1) : std::nullopt;
    if (!up)
      return fail("a loop around it has too many iterations");
    c.counts.push_back(*up / step);
  }

  // The operations of the innermost loop of the band.
  bool before = true;
  for (Operation &op : inner.getBody()->without_terminator()) {
    if (&op == c.reduction.getOperation()) {
      before = false;
      continue;
    }
    if (isa<affine::AffineForOp, affine::AffineParallelOp>(op))
      return fail("another loop beside it");
    if (!isa<affine::AffineLoadOp, affine::AffineStoreOp>(op) &&
        !isMemoryEffectFree(&op))
      return fail("an operation beside it with side effects: " +
                  op.getName().getStringRef());
    (before ? c.pre : c.post).push_back(&op);
  }
  return true;
}

/// The reduction loops perfectly nested in `r` that take its iter_args as
/// their initial values and whose results it yields, as in
/// `for (r...) for (s...) t += ...`: their accumulators are the same.
static SmallVector<AffineForOp> getChain(AffineForOp r) {
  SmallVector<AffineForOp> chain{r};
  while (chain.back().getNumIterOperands()) {
    AffineForOp cur = chain.back();
    Block *body = cur.getBody();
    if (!llvm::hasSingleElement(body->without_terminator()))
      break;
    auto next = dyn_cast<AffineForOp>(body->front());
    if (!next || next.getInits() != ValueRange(cur.getRegionIterArgs()) ||
        body->getTerminator()->getOperands() != ValueRange(next.getResults()))
      break;
    chain.push_back(next);
  }
  return chain;
}

bool Analyzer::findUses(Candidate &c) {
  Block *body = c.band.back().getBody();
  llvm::SmallPtrSet<Operation *, 16> pre(c.pre.begin(), c.pre.end());
  auto fromPre = [&](Value v) {
    return !isa<BlockArgument>(v) && isDefinedInBlock(v, body) &&
           pre.contains(v.getDefiningOp());
  };
  c.chain = getChain(c.reduction);
  for (AffineForOp loop : c.chain) {
    for (Value v : loop.getLowerBoundOperands())
      if (fromPre(v))
        return fail("a bound of it is computed before it");
    for (Value v : loop.getUpperBoundOperands())
      if (fromPre(v))
        return fail("a bound of it is computed before it");
  }
  c.reduction.getBody()->walk([&](Operation *op) {
    for (Value v : op->getOperands())
      if (fromPre(v))
        c.reductionUses.insert(v);
  });
  for (Operation *post : c.post)
    post->walk([&](Operation *op) {
      for (Value v : op->getOperands())
        if (fromPre(v))
          c.postUses.insert(v);
    });

  // Recompute a value when it and the values it uses from `pre` are
  // results of side effect free operations without regions; expand it
  // into a buffer otherwise.
  DenseMap<Operation *, bool> rematerializable;
  std::function<bool(Operation *)> canRemat = [&](Operation *op) -> bool {
    auto it = rematerializable.find(op);
    if (it != rematerializable.end())
      return it->second;
    bool ok = op->getNumRegions() == 0 && isMemoryEffectFree(op) &&
              llvm::all_of(op->getOperands(), [&](Value v) {
                return !fromPre(v) || canRemat(v.getDefiningOp());
              });
    rematerializable[op] = ok;
    return ok;
  };
  auto collect = [&](const llvm::SetVector<Value> &uses,
                     SmallVector<Operation *> &remat) {
    llvm::SmallPtrSet<Operation *, 16> needed;
    SmallVector<Operation *> work;
    for (Value v : uses) {
      Operation *def = v.getDefiningOp();
      if (canRemat(def))
        work.push_back(def);
      else
        c.expanded.insert(v);
    }
    while (!work.empty()) {
      Operation *op = work.pop_back_val();
      if (!needed.insert(op).second)
        continue;
      for (Value v : op->getOperands())
        if (fromPre(v))
          work.push_back(v.getDefiningOp());
    }
    for (Operation *op : c.pre)
      if (needed.contains(op))
        remat.push_back(op);
  };
  collect(c.reductionUses, c.rematReduction);
  collect(c.postUses, c.rematPost);

  // The buffers: one for each iter_arg and each expanded value.
  int64_t elements = 1;
  for (int64_t n : c.counts) {
    std::optional<int64_t> m = llvm::checkedMul(elements, n);
    if (!m)
      return fail("buffers too large");
    elements = *m;
  }
  SmallVector<Type> types(c.reduction.getResultTypes());
  for (Value v : c.expanded)
    types.push_back(v.getType());
  for (Type type : types) {
    if (!isBufferType(type))
      return fail("a value of type " + typeName(type) +
                  " would need a buffer");
    std::optional<int64_t> size =
        llvm::checkedMul(elements, (int64_t)layout.getTypeSize(type));
    std::optional<int64_t> sum =
        size ? llvm::checkedAdd(c.bytes, *size) : std::nullopt;
    if (!sum)
      return fail("buffers too large");
    c.bytes = *sum;
  }
  if (c.bytes > budget)
    return fail("buffers of " + Twine(c.bytes) + " bytes, more than the " +
                Twine(budget) + " left");
  return true;
}

bool Analyzer::checkDependences(Candidate &c) {
  // Every memory access in the band: affine loads and stores only, in
  // regions of affine.for, affine.if and scf.if.
  SmallVector<Operation *> accesses;
  bool other = false;
  c.band.back().getBody()->walk([&](Operation *op) {
    if (isa<affine::AffineLoadOp, affine::AffineStoreOp>(op)) {
      accesses.push_back(op);
      return;
    }
    if (isa<AffineForOp, affine::AffineIfOp, scf::IfOp>(op))
      return;
    if (!isMemoryEffectFree(op))
      other = true;
  });
  if (other)
    return fail("an operation with side effects other than affine.load "
                "and affine.store");

  // Affine analysis takes different memref values for disjoint memory.
  auto memrefOf = [](Operation *op) {
    if (auto load = dyn_cast<affine::AffineLoadOp>(op))
      return load.getMemRef();
    return cast<affine::AffineStoreOp>(op).getMemRef();
  };
  for (Operation *op : accesses)
    if (!isKnownMemory(getRoot(memrefOf(op))))
      return fail("a memref that is not a view of a global or an alloca");
  for (unsigned i = 0; i < accesses.size(); i++)
    for (unsigned j = i + 1; j < accesses.size(); j++) {
      Value a = memrefOf(accesses[i]), b = memrefOf(accesses[j]);
      if (a != b && sameMemory(getRoot(a), getRoot(b)))
        return fail("one memory reached through different memref values");
    }

  // No dependence carried by a loop of the band, between any two accesses,
  // one of them a store, in either direction.
  unsigned outer = affine::getNestingDepth(c.band.front());
  for (unsigned i = 0; i < accesses.size(); i++)
    for (unsigned j = i; j < accesses.size(); j++) {
      Operation *x = accesses[i], *y = accesses[j];
      if (memrefOf(x) != memrefOf(y))
        continue;
      if (!isa<affine::AffineStoreOp>(x) && !isa<affine::AffineStoreOp>(y))
        continue;
      affine::MemRefAccess ax(x), ay(y);
      for (unsigned depth = outer + 1; depth <= outer + c.band.size();
           depth++) {
        for (int dir = 0; dir < (i == j ? 1 : 2); dir++) {
          affine::DependenceResult r =
              dir == 0 ? affine::checkMemrefAccessDependence(ax, ay, depth)
                       : affine::checkMemrefAccessDependence(ay, ax, depth);
          if (r.value == affine::DependenceResult::Failure)
            return fail("dependence analysis failed");
          if (r.value == affine::DependenceResult::HasDependence)
            return fail("a dependence carried by loop " +
                        Twine(depth - outer) + " of " +
                        Twine(c.band.size()) + " around it");
        }
      }
    }
  return true;
}

bool Analyzer::analyze(AffineForOp reduction, Candidate &c) {
  c.reduction = reduction;
  for (Type type : reduction.getResultTypes())
    if (!isBufferType(type))
      return fail("a value of type " + typeName(type) +
                  " would need a buffer");
  return findBand(c) && findUses(c) && checkDependences(c);
}

namespace {

/// Builds the three nests of a candidate.
class Distributor {
public:
  Distributor(Candidate &c, OpBuilder &allocas) : c(c), allocas(allocas) {}

  void run();

private:
  /// Clones the band before the original, with the side effect free
  /// operations of the outer loops; returns the innermost new loop.
  AffineForOp cloneBand(IRMapping &map);
  /// A new buffer.
  Value buffer(Type type);
  /// The map of a buffer access in the innermost loop of a clone of the
  /// band, and its operands.
  void load(OpBuilder &b, Value buf, Value v, IRMapping &map,
            ArrayRef<Value> ivs);
  void store(OpBuilder &b, Value value, Value buf, ArrayRef<Value> ivs);

  Candidate &c;
  OpBuilder &allocas;
  AffineMap bufferMap;
};

} // namespace

Value Distributor::buffer(Type type) {
  auto memrefType = MemRefType::get(c.counts, type);
  Value v = memref::AllocaOp::create(allocas, c.reduction.getLoc(),
                                     memrefType);
  allocas.setInsertionPointAfter(v.getDefiningOp());
  return v;
}

AffineForOp Distributor::cloneBand(IRMapping &map) {
  OpBuilder b(c.band.front());
  AffineForOp inner;
  for (auto [i, loop] : llvm::enumerate(c.band)) {
    SmallVector<Value> lbs, ubs;
    for (Value v : loop.getLowerBoundOperands())
      lbs.push_back(map.lookupOrDefault(v));
    for (Value v : loop.getUpperBoundOperands())
      ubs.push_back(map.lookupOrDefault(v));
    auto newLoop = AffineForOp::create(b, loop.getLoc(), lbs,
                                       loop.getLowerBoundMap(), ubs,
                                       loop.getUpperBoundMap(),
                                       loop.getStepAsInt());
    map.map(loop.getInductionVar(), newLoop.getInductionVar());
    b.setInsertionPoint(newLoop.getBody()->getTerminator());
    if (i + 1 < c.band.size())
      for (Operation &op : loop.getBody()->without_terminator())
        if (&op != c.band[i + 1].getOperation())
          b.clone(op, map);
    inner = newLoop;
  }
  return inner;
}

void Distributor::load(OpBuilder &b, Value buf, Value v, IRMapping &map,
                       ArrayRef<Value> ivs) {
  Value x = affine::AffineLoadOp::create(b, v.getLoc(), buf, bufferMap, ivs);
  map.map(v, x);
}

void Distributor::store(OpBuilder &b, Value value, Value buf,
                        ArrayRef<Value> ivs) {
  affine::AffineStoreOp::create(b, value.getLoc(), value, buf, bufferMap,
                                ivs);
}

void Distributor::run() {
  MLIRContext *ctx = c.reduction.getContext();
  // The buffers are indexed by the iteration numbers of the band.
  SmallVector<AffineExpr> exprs;
  for (auto [i, loop] : llvm::enumerate(c.band)) {
    AffineExpr d = getAffineDimExpr(i, ctx);
    exprs.push_back((d - loop.getConstantLowerBound())
                        .floorDiv(loop.getStepAsInt()));
  }
  bufferMap = AffineMap::get(c.band.size(), 0, exprs, ctx);
  SmallVector<Value> accumulators;
  for (Type type : c.reduction.getResultTypes())
    accumulators.push_back(buffer(type));
  DenseMap<Value, Value> buffers;
  for (Value v : c.expanded)
    buffers[v] = buffer(v.getType());
  auto ivsOf = [&](IRMapping &map) {
    SmallVector<Value> ivs;
    for (AffineForOp loop : c.band)
      ivs.push_back(map.lookup(loop.getInductionVar()));
    return ivs;
  };

  // 1: the operations before the reduction loop, and the initial values
  // and expanded values into the buffers.
  if (!c.pre.empty() || !accumulators.empty()) {
    IRMapping map;
    AffineForOp inner = cloneBand(map);
    OpBuilder b(inner.getBody()->getTerminator());
    for (Operation *op : c.pre)
      b.clone(*op, map);
    SmallVector<Value> ivs = ivsOf(map);
    for (auto [init, buf] : llvm::zip(c.reduction.getInits(), accumulators))
      store(b, map.lookupOrDefault(init), buf, ivs);
    for (Value v : c.expanded)
      store(b, map.lookup(v), buffers[v], ivs);
  }

  // 2: the reduction loop and its chain, without iter_args, on the
  // buffers: the innermost loads and stores them.
  {
    IRMapping map;
    AffineForOp inner = cloneBand(map);
    OpBuilder b(inner.getBody()->getTerminator());
    SmallVector<Value> ivs = ivsOf(map);
    for (AffineForOp r : c.chain) {
      SmallVector<Value> lbs, ubs;
      for (Value v : r.getLowerBoundOperands())
        lbs.push_back(map.lookupOrDefault(v));
      for (Value v : r.getUpperBoundOperands())
        ubs.push_back(map.lookupOrDefault(v));
      auto loop = AffineForOp::create(b, r.getLoc(), lbs,
                                      r.getLowerBoundMap(), ubs,
                                      r.getUpperBoundMap(), r.getStepAsInt());
      map.map(r.getInductionVar(), loop.getInductionVar());
      b.setInsertionPoint(loop.getBody()->getTerminator());
    }
    AffineForOp r = c.chain.back();
    for (Value v : c.reductionUses)
      if (c.expanded.contains(v))
        load(b, buffers[v], v, map, ivs);
    for (Operation *op : c.rematReduction)
      b.clone(*op, map);
    for (auto [arg, buf] : llvm::zip(r.getRegionIterArgs(), accumulators))
      load(b, buf, arg, map, ivs);
    for (Operation &op : r.getBody()->without_terminator())
      b.clone(op, map);
    auto yield = cast<affine::AffineYieldOp>(r.getBody()->getTerminator());
    for (auto [v, buf] : llvm::zip(yield.getOperands(), accumulators))
      store(b, map.lookupOrDefault(v), buf, ivs);
  }

  // 3: the results and the operations after the reduction loop.
  if (!c.post.empty()) {
    IRMapping map;
    AffineForOp inner = cloneBand(map);
    OpBuilder b(inner.getBody()->getTerminator());
    SmallVector<Value> ivs = ivsOf(map);
    for (auto [result, buf] : llvm::zip(c.reduction.getResults(), accumulators))
      if (!result.use_empty())
        load(b, buf, result, map, ivs);
    for (Value v : c.postUses)
      if (c.expanded.contains(v))
        load(b, buffers[v], v, map, ivs);
    for (Operation *op : c.rematPost)
      b.clone(*op, map);
    for (Operation *op : c.post)
      b.clone(*op, map);
  }

  c.band.front().erase();
}

namespace {

struct DistributeReductionsPass
    : public lcc::impl::LccDistributeReductionsBase<DistributeReductionsPass> {
  using LccDistributeReductionsBase::LccDistributeReductionsBase;

  void runOnOperation() override {
    getOperation()->walk([&](func::FuncOp func) { runOnFunction(func); });
  }

  void runOnFunction(func::FuncOp func);
};

} // namespace

/// Whether `loop` accumulates in memory: it stores, in its body or in
/// loops in it, at indices that do not depend on its loop variable.
static bool accumulatesInMemory(AffineForOp loop) {
  Value iv = loop.getInductionVar();
  bool found = false;
  loop.getBody()->walk([&](affine::AffineStoreOp store) {
    AffineMap map = store.getAffineMap();
    bool uses = false;
    for (auto [i, v] : llvm::enumerate(store.getMapOperands()))
      if (v == iv)
        uses |= i < map.getNumDims()
                    ? map.isFunctionOfDim(i)
                    : map.isFunctionOfSymbol(i - map.getNumDims());
    found |= !uses;
  });
  return found;
}

/// Whether the loops around `loop` are worth distributing: it is a
/// reduction loop with iter_args, or a loop that accumulates in memory and
/// that the innermost loop around it holds with other operations, as the
/// loop of `c[i][j] = 0; for (k...) c[i][j] += ...`.
static bool isCandidate(AffineForOp loop) {
  if (loop.getNumIterOperands())
    return true;
  auto parent = dyn_cast<AffineForOp>(loop->getParentOp());
  return parent &&
         !llvm::hasSingleElement(parent.getBody()->without_terminator()) &&
         accumulatesInMemory(loop);
}

void DistributeReductionsPass::runOnFunction(func::FuncOp func) {
  if (func.getBody().empty())
    return;
  DataLayout layout = DataLayout::closest(func);
  OpBuilder allocas(func.getContext());
  allocas.setInsertionPointToStart(&func.getBody().front());
  int64_t left = maxBufferBytes;
  // In rounds: the candidates are analyzed innermost first, and those
  // whose loops do not overlap the loops of one already taken are
  // distributed; then the loops of the next round, which may hold the new
  // nests, are analyzed again.  A distributed nest leaves no candidate
  // behind (the reduction loop of the second nest has no iter_args and
  // nothing beside it), so the rounds end.
  while (true) {
    SmallVector<AffineForOp> loops;
    func.walk([&](AffineForOp loop) {
      if (isCandidate(loop))
        loops.push_back(loop);
    });
    SmallVector<Candidate, 0> candidates;
    SmallVector<std::pair<AffineForOp, std::string>> failures;
    for (AffineForOp r : loops) {
      // A loop of the chain of another goes with it.
      if (auto parent = dyn_cast<AffineForOp>(r->getParentOp());
          parent && parent.getNumIterOperands() &&
          llvm::is_contained(getChain(parent), r))
        continue;
      Analyzer analyzer(left, layout);
      Candidate c;
      if (!analyzer.analyze(r, c)) {
        failures.push_back({r, analyzer.reason});
        continue;
      }
      Operation *root = c.band.front();
      if (llvm::any_of(candidates, [&](const Candidate &other) {
            Operation *o = other.band.front();
            return o->isAncestor(root) || root->isAncestor(o);
          }))
        continue;
      left -= c.bytes;
      candidates.push_back(std::move(c));
    }
    if (candidates.empty()) {
      if (remarks)
        for (auto &[r, reason] : failures)
          r.emitRemark() << "lcc-distribute-reductions: "
                         << (r.getNumIterOperands() ? "reduction" : "loop")
                         << " stays: " << reason;
      break;
    }
    for (Candidate &c : candidates) {
      if (remarks) {
        unsigned n = c.reduction.getNumResults() + c.expanded.size();
        c.reduction.emitRemark()
            << "lcc-distribute-reductions: distributed " << c.band.size()
            << " loop" << (c.band.size() > 1 ? "s" : "")
            << " around it with " << n << " buffer" << (n == 1 ? "" : "s")
            << " of " << c.bytes << " bytes";
      }
      Distributor(c, allocas).run();
    }
  }
}
