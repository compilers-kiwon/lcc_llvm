//===- AffineToLinalg.cpp - Turn affine loop nests into linalg.generic ----===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// lcc-affine-to-linalg: see Passes.td.  The pass analyzes every affine.for
// that is not in a nest it raises, outer loops first, before it changes
// anything, then replaces each nest it may raise with memref.subview views
// and a linalg.generic.
//
// The iteration dimension j_k of loop k counts its iterations from 0: the
// loop variable is lb_k + step_k * j_k.  The indices of each access become
// affine expressions of the j_k, and an index c + a * j_k of a memref
// dimension becomes the index j_k of a subview with offset c, size the
// iteration count and stride a, so that linalg infers the iteration counts
// from the shapes of the operands, as its verifier requires.
//
//===----------------------------------------------------------------------===//

#include "lcc-opt/Passes.h"

#include "mlir/Dialect/Affine/IR/AffineOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/Linalg/IR/Linalg.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/IR/AffineExpr.h"
#include "mlir/IR/AffineMap.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Interfaces/ViewLikeInterface.h"
#include "llvm/Support/CheckedArithmetic.h"

#include <string>

namespace lcc {
#define GEN_PASS_DEF_LCCAFFINETOLINALG
#include "lcc-opt/Passes.h.inc"
} // namespace lcc

using namespace mlir;
using affine::AffineForOp;

namespace {

/// An operand of the linalg.generic: a memref, the part of it the nest
/// accesses (a subview when that is not all of it) and the indexing map.
struct Operand {
  Value memref;
  /// The access map in the iteration dimensions.
  AffineMap access;
  SmallVector<int64_t> offsets, sizes, strides;
  bool subview = false;
  AffineMap map;
};

/// What a load of the nest reads in the linalg.generic.
struct Load {
  enum Kind { Input, Output, Stored } kind;
  /// The input or output.
  unsigned index;
};

/// A nest to raise.
struct Nest {
  /// The loops, outermost first.
  SmallVector<AffineForOp> loops;
  SmallVector<int64_t> lbs, steps, counts;
  SmallVector<Operand> inputs, outputs;
  /// For each load, the input or output it reads, or, when it follows the
  /// store of its output, the stored value.
  DenseMap<Operation *, Load> loads;
  /// The store of each output, and the output of each store.
  SmallVector<Operation *> storeOps;
  DenseMap<Operation *, unsigned> stores;
  SmallVector<utils::IteratorType> iterators;
};

class Analyzer {
public:
  explicit Analyzer(MLIRContext *ctx) : ctx(ctx) {}

  /// Whether the nest of `root` may be raised; fills `nest`, or sets
  /// `reason`.
  bool analyze(AffineForOp root, Nest &nest);

  std::string reason;

private:
  bool fail(const Twine &why) {
    reason = why.str();
    return false;
  }
  /// The access map of `op` in the iteration dimensions, or nothing.
  std::optional<AffineMap> getAccessMap(Operation *op, Nest &nest);
  /// Fills the subview and the indexing map of `operand`.
  bool place(Operand &operand, Nest &nest, bool output);

  MLIRContext *ctx;
};

} // namespace

/// Whether `op` may be in the body of a linalg.generic: side effect free
/// and without loops.
static bool isComputation(Operation *op) {
  if (!isMemoryEffectFree(op))
    return false;
  bool loop = false;
  op->walk([&](Operation *inner) {
    if (isa<AffineForOp, affine::AffineParallelOp, affine::AffineIfOp>(inner))
      loop = true;
  });
  return !loop;
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

static Value getMemRef(Operation *op) {
  if (auto load = dyn_cast<affine::AffineLoadOp>(op))
    return load.getMemRef();
  return cast<affine::AffineStoreOp>(op).getMemRef();
}

std::optional<AffineMap> Analyzer::getAccessMap(Operation *op, Nest &nest) {
  AffineMap map;
  ValueRange operands;
  if (auto load = dyn_cast<affine::AffineLoadOp>(op)) {
    map = load.getAffineMap();
    operands = load.getMapOperands();
  } else {
    auto store = cast<affine::AffineStoreOp>(op);
    map = store.getAffineMap();
    operands = store.getMapOperands();
  }
  // Every operand is a loop variable of the nest: lb + step * j.
  unsigned numLoops = nest.loops.size();
  SmallVector<AffineExpr> replacements;
  for (Value v : operands) {
    auto it = llvm::find_if(nest.loops, [&](AffineForOp loop) {
      return loop.getInductionVar() == v;
    });
    if (it == nest.loops.end()) {
      fail("an index uses a value from outside the nest");
      return std::nullopt;
    }
    unsigned k = it - nest.loops.begin();
    replacements.push_back(getAffineDimExpr(k, ctx) * nest.steps[k] +
                           nest.lbs[k]);
  }
  ArrayRef<AffineExpr> all(replacements);
  return map.replaceDimsAndSymbols(all.take_front(map.getNumDims()),
                                   all.drop_front(map.getNumDims()),
                                   numLoops, 0);
}

bool Analyzer::place(Operand &operand, Nest &nest, bool output) {
  auto type = cast<MemRefType>(operand.memref.getType());
  unsigned numLoops = nest.loops.size();
  SmallVector<AffineExpr> results;
  SmallVector<unsigned> outputDims;
  for (auto [d, expr] : llvm::enumerate(operand.access.getResults())) {
    std::optional<lcc::LinearExpr> lin = lcc::getLinearExpr(expr, numLoops, 0);
    if (!lin)
      return fail("an index that is not linear");
    int64_t offset = lin->constant, size = 1, stride = 1;
    AffineExpr result = getAffineConstantExpr(0, ctx);
    unsigned terms = 0, last = 0;
    for (unsigned k = 0; k < numLoops; k++) {
      int64_t a = lin->dims[k];
      if (!a)
        continue;
      if (a < 0)
        return fail("an index that decreases with a loop variable");
      terms++;
      last = k;
      result = result + getAffineDimExpr(k, ctx) * a;
    }
    if (terms == 1) {
      // c + a * j_k: a subview with stride a, indexed by j_k.
      stride = lin->dims[last];
      size = nest.counts[last];
      result = getAffineDimExpr(last, ctx);
      outputDims.push_back(last);
    } else if (terms > 1) {
      if (output)
        return fail("an output index that is a sum of loop variables");
      // The range of the sum.
      int64_t max = 0;
      for (unsigned k = 0; k < numLoops; k++) {
        std::optional<int64_t> m =
            llvm::checkedMul(lin->dims[k], nest.counts[k] - 1);
        std::optional<int64_t> s =
            m ? llvm::checkedAdd(max, *m) : std::nullopt;
        if (!s)
          return fail("an index out of range");
        max = *s;
      }
      size = max + 1;
    }
    // The subview must lie in the memref.
    std::optional<int64_t> span = llvm::checkedMul(size - 1, stride);
    std::optional<int64_t> end =
        span ? llvm::checkedAdd(offset, *span) : std::nullopt;
    if (offset < 0 || !end || *end >= type.getDimSize(d))
      return fail("an index may be out of bounds");
    operand.offsets.push_back(offset);
    operand.sizes.push_back(size);
    operand.strides.push_back(stride);
    if (offset != 0 || size != type.getDimSize(d) || stride != 1)
      operand.subview = true;
    results.push_back(result);
  }
  if (output) {
    llvm::SmallDenseSet<unsigned> seen;
    for (unsigned k : outputDims)
      if (!seen.insert(k).second)
        return fail("an output index that repeats a loop variable");
  }
  operand.map = AffineMap::get(numLoops, 0, results, ctx);
  return true;
}

bool Analyzer::analyze(AffineForOp root, Nest &nest) {
  // The band: loops whose bodies hold the next loop and side effect free
  // operations without regions.
  AffineForOp loop = root;
  while (true) {
    nest.loops.push_back(loop);
    if (loop.getNumIterOperands())
      return fail("a loop with iter_args");
    if (!loop.hasConstantBounds())
      return fail("a loop with bounds that are not constant");
    int64_t lb = loop.getConstantLowerBound();
    int64_t ub = loop.getConstantUpperBound();
    int64_t step = loop.getStepAsInt();
    if (ub <= lb)
      return fail("a loop without iterations");
    std::optional<int64_t> range = llvm::checkedSub(ub, lb);
    std::optional<int64_t> up =
        range ? llvm::checkedAdd(*range, step - 1) : std::nullopt;
    if (!up)
      return fail("a loop with too many iterations");
    nest.lbs.push_back(lb);
    nest.steps.push_back(step);
    nest.counts.push_back(*up / step);
    AffineForOp next;
    unsigned numLoops = 0;
    for (Operation &op : loop.getBody()->without_terminator())
      if (auto inner = dyn_cast<AffineForOp>(op)) {
        next = inner;
        numLoops++;
      }
    if (!numLoops)
      break;
    if (numLoops > 1)
      return fail("more than one loop in a loop");
    for (Operation &op : loop.getBody()->without_terminator())
      if (&op != next.getOperation() &&
          (op.getNumRegions() || !isMemoryEffectFree(&op)))
        return fail("an operation beside an inner loop: " +
                    op.getName().getStringRef());
    loop = next;
  }

  // The innermost body: loads, stores and computations.
  Block *body = nest.loops.back().getBody();
  SmallVector<Operation *> accesses;
  for (Operation &op : body->without_terminator()) {
    if (isa<affine::AffineLoadOp, affine::AffineStoreOp>(op)) {
      accesses.push_back(&op);
      continue;
    }
    if (!isComputation(&op))
      return fail("an operation that is not a computation: " +
                  op.getName().getStringRef());
  }
  for (Operation *op : accesses) {
    Value memref = getMemRef(op);
    auto type = cast<MemRefType>(memref.getType());
    if (!type.hasStaticShape() || !type.getLayout().isIdentity())
      return fail("a memref that is not of a static shape and identity "
                  "layout");
    if (Operation *def = memref.getDefiningOp();
        def && root->isAncestor(def))
      return fail("a memref defined in the nest");
    if (!isKnownMemory(getRoot(memref)))
      return fail("a memref that is not a view of a global or an alloca");
  }
  for (unsigned i = 0; i < accesses.size(); i++)
    for (unsigned j = i + 1; j < accesses.size(); j++) {
      Value a = getMemRef(accesses[i]), b = getMemRef(accesses[j]);
      if (a != b && sameMemory(getRoot(a), getRoot(b)))
        return fail("one memory reached through different memref values");
    }

  // The outputs: one store each.
  DenseMap<Value, unsigned> outputOf;
  for (Operation *op : accesses) {
    auto store = dyn_cast<affine::AffineStoreOp>(op);
    if (!store)
      continue;
    auto [it, inserted] =
        outputOf.try_emplace(store.getMemRef(), nest.outputs.size());
    if (!inserted)
      return fail("two stores to one memref");
    std::optional<AffineMap> map = getAccessMap(op, nest);
    if (!map)
      return false;
    Operand out;
    out.memref = store.getMemRef();
    out.access = *map;
    if (!place(out, nest, /*output=*/true))
      return false;
    nest.stores[op] = it->second;
    nest.storeOps.push_back(op);
    nest.outputs.push_back(out);
  }
  if (nest.outputs.empty())
    return fail("no store");

  // The inputs, and the loads of outputs.
  SmallVector<Operand> inputs;
  DenseMap<Operation *, unsigned> inputOf;
  for (Operation *op : accesses) {
    auto load = dyn_cast<affine::AffineLoadOp>(op);
    if (!load)
      continue;
    std::optional<AffineMap> map = getAccessMap(op, nest);
    if (!map)
      return false;
    auto out = outputOf.find(load.getMemRef());
    if (out != outputOf.end()) {
      Operand &o = nest.outputs[out->second];
      if (*map != o.access)
        return fail("a stored memref read at other indices");
      // Before the store it reads the output, after it the stored value.
      Operation *store = nest.storeOps[out->second];
      nest.loads[op] = {store->isBeforeInBlock(op) ? Load::Stored
                                                   : Load::Output,
                        out->second};
      continue;
    }
    auto it = llvm::find_if(inputs, [&](const Operand &in) {
      return in.memref == load.getMemRef() && in.access == *map;
    });
    if (it == inputs.end()) {
      Operand in;
      in.memref = load.getMemRef();
      in.access = *map;
      if (!place(in, nest, /*output=*/false))
        return false;
      inputs.push_back(in);
      it = std::prev(inputs.end());
    }
    nest.loads[op] = {Load::Input, unsigned(it - inputs.begin())};
  }
  nest.inputs = std::move(inputs);

  // linalg infers each iteration count from an index that is that
  // dimension alone.
  unsigned numLoops = nest.loops.size();
  for (unsigned k = 0; k < numLoops; k++) {
    AffineExpr d = getAffineDimExpr(k, ctx);
    auto alone = [&](const Operand &o) {
      return llvm::is_contained(o.map.getResults(), d);
    };
    if (llvm::none_of(nest.inputs, alone) &&
        llvm::none_of(nest.outputs, alone))
      return fail("no index is loop " + Twine(k + 1) + " alone");
  }
  // A dimension is parallel when every output has it.
  for (unsigned k = 0; k < numLoops; k++) {
    AffineExpr d = getAffineDimExpr(k, ctx);
    bool parallel = llvm::all_of(nest.outputs, [&](const Operand &o) {
      return llvm::is_contained(o.map.getResults(), d);
    });
    nest.iterators.push_back(parallel ? utils::IteratorType::parallel
                                      : utils::IteratorType::reduction);
  }
  return true;
}

/// Replaces the nest analyzed in `nest` with a linalg.generic.
static void raise(Nest &nest) {
  AffineForOp root = nest.loops.front();
  Location loc = root.getLoc();
  OpBuilder b(root);
  auto view = [&](Operand &o) -> Value {
    if (!o.subview)
      return o.memref;
    return memref::SubViewOp::create(b, loc, o.memref, o.offsets, o.sizes,
                                     o.strides);
  };
  SmallVector<Value> inputs, outputs;
  SmallVector<AffineMap> maps;
  for (Operand &o : nest.inputs) {
    inputs.push_back(view(o));
    maps.push_back(o.map);
  }
  for (Operand &o : nest.outputs) {
    outputs.push_back(view(o));
    maps.push_back(o.map);
  }

  Block *body = nest.loops.back().getBody();
  unsigned numInputs = nest.inputs.size();
  auto build = [&](OpBuilder &nb, Location nl, ValueRange args) {
    IRMapping map;
    // The loop variables the computations use: lb + step * linalg.index.
    for (auto [k, loop] : llvm::enumerate(nest.loops)) {
      Value iv = loop.getInductionVar();
      bool used = llvm::any_of(iv.getUsers(), [&](Operation *user) {
        return !isa<affine::AffineLoadOp, affine::AffineStoreOp>(user);
      });
      if (!used)
        continue;
      Value j = linalg::IndexOp::create(nb, nl, k);
      if (nest.lbs[k] != 0 || nest.steps[k] != 1) {
        AffineExpr d = getAffineDimExpr(0, nb.getContext());
        j = affine::AffineApplyOp::create(
            nb, nl, AffineMap::get(1, 0, d * nest.steps[k] + nest.lbs[k]),
            ValueRange{j});
      }
      map.map(iv, j);
    }
    // The computations of the outer loops' bodies.
    for (auto loop : ArrayRef<AffineForOp>(nest.loops).drop_back())
      for (Operation &op : loop.getBody()->without_terminator())
        if (!isa<AffineForOp>(op))
          nb.clone(op, map);
    SmallVector<Value> yields(nest.outputs.size());
    for (Operation &op : body->without_terminator()) {
      if (auto load = dyn_cast<affine::AffineLoadOp>(op)) {
        Load l = nest.loads.lookup(&op);
        Value v = l.kind == Load::Input    ? args[l.index]
                  : l.kind == Load::Output ? args[numInputs + l.index]
                                           : yields[l.index];
        map.map(load.getResult(), v);
        continue;
      }
      if (auto store = dyn_cast<affine::AffineStoreOp>(op)) {
        yields[nest.stores.lookup(&op)] =
            map.lookupOrDefault(store.getValueToStore());
        continue;
      }
      nb.clone(op, map);
    }
    linalg::YieldOp::create(nb, nl, yields);
  };
  linalg::GenericOp::create(b, loc, inputs, outputs, maps, nest.iterators,
                            build);
  root.erase();
}

namespace {

struct AffineToLinalgPass
    : public lcc::impl::LccAffineToLinalgBase<AffineToLinalgPass> {
  using LccAffineToLinalgBase::LccAffineToLinalgBase;

  void runOnOperation() override {
    SmallVector<Nest, 0> nests;
    getOperation()->walk([&](func::FuncOp func) {
      func.walk<WalkOrder::PreOrder>([&](AffineForOp loop) {
        Analyzer analyzer(&getContext());
        Nest nest;
        bool inner = loop->getParentOfType<AffineForOp>() != nullptr;
        if (analyzer.analyze(loop, nest)) {
          if (remarks) {
            unsigned reductions = llvm::count(nest.iterators,
                                              utils::IteratorType::reduction);
            loop.emitRemark()
                << "lcc-affine-to-linalg: raised " << (inner ? "an inner" : "a")
                << " nest of " << nest.loops.size() << " loop"
                << (nest.loops.size() > 1 ? "s" : "") << " ("
                << nest.loops.size() - reductions << " parallel, "
                << reductions << " reduction)";
          }
          nests.push_back(std::move(nest));
          return WalkResult::skip();
        }
        if (remarks)
          loop.emitRemark() << "lcc-affine-to-linalg: "
                            << (inner ? "inner nest" : "nest")
                            << " stays: " << analyzer.reason;
        return WalkResult::advance();
      });
    });
    for (Nest &nest : nests)
      raise(nest);
  }
};

} // namespace
