//===- IndexLoops.cpp - Turn integer scf.for loops into index loops -------===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// lcc-index-loops: see Passes.td.  By the semantics of scf.for, the
// induction variable of `scf.for %iv = %lb to %ub step %s : iN` takes the
// values lb + j * s, j = 0, ..., n - 1 with n = max(0, ceil((ub - lb) / s)),
// computed exactly, and lb + n * s must fit in iN.  The values, taken as
// signed (or, for `scf.for unsigned`, as unsigned) numbers, do not change
// when the bounds and the step are sign-extended (zero-extended) to index:
// the loop runs the same iterations with the same values, and lb + n * s
// fits in index, as long as index holds every value of iN (of the unsigned
// bounds, as signed numbers).
//
//===----------------------------------------------------------------------===//

#include "lcc-opt/Passes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/PatternMatch.h"
#include "llvm/Support/CheckedArithmetic.h"

namespace lcc {
#define GEN_PASS_DEF_LCCINDEXLOOPS
#include "lcc-opt/Passes.h.inc"
} // namespace lcc

using namespace mlir;

/// Whether the unsigned loop `loop` of iN, N the width of index, has bounds
/// and step that are non-negative as signed numbers and `ub + step - 1`
/// that does not wrap, so that it may become a signed index loop.
static bool unsignedFits(scf::ForOp loop, unsigned width,
                         lcc::LinearAnalysis &la) {
  std::optional<lcc::IntRange> lb = la.getRange(loop.getLowerBound());
  std::optional<lcc::IntRange> ub = la.getRange(loop.getUpperBound());
  std::optional<lcc::IntRange> step = la.getRange(loop.getStep());
  if (!lb || !ub || !step || lb->min < 0 || ub->min < 0 || step->min < 1)
    return false;
  std::optional<int64_t> last = llvm::checkedAdd(ub->max, step->max - 1);
  int64_t max = width >= 64 ? INT64_MAX : (int64_t(1) << (width - 1)) - 1;
  return last && *last <= max;
}

/// Makes `loop`, of iN, an index loop.
static void convert(scf::ForOp loop, bool isUnsigned, IRRewriter &rewriter) {
  Location loc = loop.getLoc();
  Type indexType = rewriter.getIndexType();
  Type type = loop.getInductionVar().getType();
  rewriter.setInsertionPoint(loop);
  auto toIndex = [&](Value v) -> Value {
    if (isUnsigned)
      return arith::IndexCastUIOp::create(rewriter, loc, indexType, v);
    return arith::IndexCastOp::create(rewriter, loc, indexType, v);
  };
  Value lb = toIndex(loop.getLowerBound());
  Value ub = toIndex(loop.getUpperBound());
  Value step = toIndex(loop.getStep());
  auto newLoop = scf::ForOp::create(rewriter, loc, lb, ub, step,
                                    loop.getInitArgs());
  Block *body = newLoop.getBody();
  if (!body->empty())
    rewriter.eraseOp(body->getTerminator());

  // The body, with the induction variable cast back to iN.
  rewriter.setInsertionPointToStart(body);
  Value iv = newLoop.getInductionVar();
  Value cast;
  if (isUnsigned)
    cast = arith::IndexCastUIOp::create(rewriter, loc, type, iv);
  else
    cast = arith::IndexCastOp::create(rewriter, loc, type, iv);
  SmallVector<Value> args{cast};
  llvm::append_range(args, newLoop.getRegionIterArgs());
  rewriter.mergeBlocks(loop.getBody(), body, args);
  rewriter.replaceOp(loop, newLoop.getResults());

  // Extensions and index casts of the induction variable take the index
  // induction variable itself: its values fit in iN, so extending them
  // from iN or casting them from index gives the same numbers.
  for (OpOperand &use : llvm::make_early_inc_range(cast.getUses())) {
    Operation *user = use.getOwner();
    Value same;
    rewriter.setInsertionPoint(user);
    if (!isUnsigned && isa<arith::ExtSIOp>(user))
      same = arith::IndexCastOp::create(rewriter, user->getLoc(),
                                        user->getResult(0).getType(), iv);
    else if (isUnsigned && isa<arith::ExtUIOp>(user))
      same = arith::IndexCastUIOp::create(rewriter, user->getLoc(),
                                          user->getResult(0).getType(), iv);
    else if ((!isUnsigned && isa<arith::IndexCastOp>(user)) ||
             (isUnsigned && isa<arith::IndexCastUIOp>(user)))
      same = iv;
    if (same)
      rewriter.replaceOp(user, same);
  }
  if (cast.use_empty())
    rewriter.eraseOp(cast.getDefiningOp());
}

namespace {

struct IndexLoopsPass : public lcc::impl::LccIndexLoopsBase<IndexLoopsPass> {
  void runOnOperation() override {
    unsigned width = lcc::getIndexWidth(getOperation());
    // The loops of an integer type, inner loops first; converting one does
    // not change the bounds or the body of another, only replaces the
    // users of its induction variable.
    SmallVector<scf::ForOp> loops;
    getOperation()->walk([&](scf::ForOp loop) {
      if (isa<IntegerType>(loop.getInductionVar().getType()))
        loops.push_back(loop);
    });
    IRRewriter rewriter(&getContext());
    for (scf::ForOp loop : loops) {
      unsigned n = loop.getInductionVar().getType().getIntOrFloatBitWidth();
      bool isUnsigned = loop.getUnsignedCmp();
      if (n > width)
        continue;
      if (isUnsigned && n == width) {
        // The ranges of the bounds, in the IR as it is now.
        lcc::LinearAnalysis la(width);
        if (!unsignedFits(loop, width, la))
          continue;
      }
      convert(loop, isUnsigned, rewriter);
    }
  }
};

} // namespace
