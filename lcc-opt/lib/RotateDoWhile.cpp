//===- RotateDoWhile.cpp - Rotate do-while shaped scf.while ---------------===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// lcc-rotate-do-while: see Passes.td.  With `x` the before block arguments,
// `v` the condition arguments, `y` the after block arguments and the after
// region yielding `y[fwd[j]]` for `x[j]`, a do-while loop
//
//   while (x = init) { body(x); condition(next(x) < ub) v } do (y) { yield y[fwd] }
//
// becomes
//
//   while (x = init) { condition(x[iv] < ub) x } do (x) { body(x); yield v[fwd] }
//
// where next(x) is v[fwd[iv]], so every test after the first compares the
// value the old loop compared, and the first test, init[iv] < ub, is true
// as in the old loop, which always runs its body once.  A result of the old
// loop is v[i] of the last iteration; for i = fwd[j] that is the new result
// j.  The other v[i] are dealt with in rotate().
//
//===----------------------------------------------------------------------===//

#include "lcc-opt/Passes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "llvm/ADT/SetVector.h"

#include <optional>

namespace lcc {
#define GEN_PASS_DEF_LCCROTATEDOWHILE
#include "lcc-opt/Passes.h.inc"
} // namespace lcc

using namespace mlir;

namespace {

/// A do-while loop that rotate() may rotate.
struct DoWhile {
  /// The number of the before block argument that is the induction variable.
  unsigned iv;
  /// The upper bound, defined outside the loop.
  Value ub;
  /// The old comparison `next < ub`.
  arith::CmpIOp cmp;
  /// fwd[j]: the after block argument the after region yields for before
  /// block argument j, that is, the condition argument passed on to it.
  SmallVector<unsigned> fwd;
};

} // namespace

/// Returns the do-while loop `loop` is, or nothing when it is not one that
/// may be rotated.
static std::optional<DoWhile> matchDoWhile(scf::WhileOp loop) {
  Block *before = loop.getBeforeBody();
  Block *after = loop.getAfterBody();
  scf::ConditionOp cond = loop.getConditionOp();
  scf::YieldOp yield = loop.getYieldOp();

  // The after region yields its block arguments and does nothing else.
  if (&after->front() != yield.getOperation())
    return std::nullopt;
  DoWhile dw;
  for (Value v : yield.getResults()) {
    auto arg = dyn_cast<BlockArgument>(v);
    if (!arg || arg.getOwner() != after)
      return std::nullopt;
    dw.fwd.push_back(arg.getArgNumber());
  }

  // The condition is `cmpi slt, next, ub` with ub defined outside the loop.
  dw.cmp = cond.getCondition().getDefiningOp<arith::CmpIOp>();
  if (!dw.cmp || dw.cmp->getBlock() != before ||
      dw.cmp.getPredicate() != arith::CmpIPredicate::slt)
    return std::nullopt;
  dw.ub = dw.cmp.getRhs();
  if (!loop.isDefinedOutsideOfLoop(dw.ub))
    return std::nullopt;

  // next is `addi iv, step` or `addi step, iv`, with iv a before block
  // argument and step a positive constant defined outside the loop.
  auto add = dw.cmp.getLhs().getDefiningOp<arith::AddIOp>();
  if (!add)
    return std::nullopt;
  for (auto [ivValue, step] : {std::pair(add.getLhs(), add.getRhs()),
                               std::pair(add.getRhs(), add.getLhs())}) {
    auto iv = dyn_cast<BlockArgument>(ivValue);
    if (!iv || iv.getOwner() != before)
      continue;
    APInt stepValue;
    if (!loop.isDefinedOutsideOfLoop(step) ||
        !matchPattern(step, m_ConstantInt(&stepValue)) ||
        !stepValue.isStrictlyPositive())
      continue;
    // next is the value passed on to iv.
    unsigned j = iv.getArgNumber();
    if (cond.getArgs()[dw.fwd[j]] != add.getResult())
      continue;
    // The first test folds to true.
    APInt init, ub;
    if (!matchPattern(loop.getInits()[j], m_ConstantInt(&init)) ||
        !matchPattern(dw.ub, m_ConstantInt(&ub)) || !init.slt(ub))
      continue;
    dw.iv = j;
    return dw;
  }
  return std::nullopt;
}

/// Adds to `ops`, operands first, the operations of the before block of
/// `loop` that compute `v` from values defined outside the loop, and returns
/// true, when `v` is defined outside the loop or such operations, pure and
/// without regions, compute it.  Otherwise returns false and leaves `ops`
/// alone.
static bool collectInvariant(Value v, scf::WhileOp loop,
                             llvm::SetVector<Operation *> &ops) {
  if (loop.isDefinedOutsideOfLoop(v))
    return true;
  Operation *op = v.getDefiningOp();
  if (!op || op->getBlock() != loop.getBeforeBody() ||
      op->getNumRegions() != 0 || !isPure(op))
    return false;
  if (ops.contains(op))
    return true;
  llvm::SetVector<Operation *> more = ops;
  for (Value operand : op->getOperands())
    if (!collectInvariant(operand, loop, more))
      return false;
  more.insert(op);
  ops = std::move(more);
  return true;
}

/// Rotates the do-while loop `loop` described by `dw`.
static void rotate(scf::WhileOp loop, DoWhile &dw, RewriterBase &rewriter) {
  Block *before = loop.getBeforeBody();
  scf::ConditionOp cond = loop.getConditionOp();
  unsigned numArgs = before->getNumArguments();
  unsigned numResults = loop.getNumResults();

  // from[i]: the new result for old result i, when the after region passes
  // condition argument i on.
  SmallVector<int> from(numResults, -1);
  for (unsigned j = 0; j < numArgs; j++)
    if (from[dw.fwd[j]] < 0)
      from[dw.fwd[j]] = j;

  // A condition argument that is not passed on is the value of the last
  // iteration.  When it is defined outside the loop or computed from such
  // values by pure operations, which then compute it in every iteration, it
  // is that value, with the operations moved in front of the loop.
  // Otherwise the new loop carries it, from ub.poison: the body runs at
  // least once.
  llvm::SetVector<Operation *> hoist;
  SmallVector<Value> invariant(numResults);
  SmallVector<unsigned> carried;
  for (unsigned i = 0; i < numResults; i++) {
    if (from[i] >= 0)
      continue;
    Value v = cond.getArgs()[i];
    if (collectInvariant(v, loop, hoist))
      invariant[i] = v;
    else
      carried.push_back(i);
  }
  for (Operation *op : hoist)
    rewriter.moveOpBefore(op, loop);

  Location loc = loop.getLoc();
  rewriter.setInsertionPoint(loop);
  SmallVector<Value> inits(loop.getInits());
  for (unsigned i : carried)
    inits.push_back(
        ub::PoisonOp::create(rewriter, loc, cond.getArgs()[i].getType()));
  SmallVector<Type> types(ValueRange(inits).getTypes());
  auto newLoop = scf::WhileOp::create(rewriter, loc, types, inits,
                                      /*beforeBuilder=*/nullptr,
                                      /*afterBuilder=*/nullptr);

  // before: test the induction variable and pass everything on.
  Block *newBefore = newLoop.getBeforeBody();
  rewriter.setInsertionPointToStart(newBefore);
  Value test = arith::CmpIOp::create(rewriter, dw.cmp.getLoc(),
                                     arith::CmpIPredicate::slt,
                                     newBefore->getArgument(dw.iv), dw.ub);
  scf::ConditionOp::create(rewriter, cond.getLoc(), test,
                           newBefore->getArguments());

  // after: the old body, yielding what the old after region passed on.
  Block *newAfter = newLoop.getAfterBody();
  rewriter.inlineBlockBefore(before, newAfter, newAfter->end(),
                             newAfter->getArguments().take_front(numArgs));
  auto oldCond = cast<scf::ConditionOp>(newAfter->getTerminator());
  SmallVector<Value> yields;
  for (unsigned j = 0; j < numArgs; j++)
    yields.push_back(oldCond.getArgs()[dw.fwd[j]]);
  for (unsigned i : carried)
    yields.push_back(oldCond.getArgs()[i]);
  rewriter.setInsertionPoint(oldCond);
  scf::YieldOp::create(rewriter, oldCond.getLoc(), yields);
  rewriter.eraseOp(oldCond);
  if (dw.cmp->use_empty())
    rewriter.eraseOp(dw.cmp);

  SmallVector<Value> results(numResults);
  unsigned next = numArgs;
  for (unsigned i = 0; i < numResults; i++) {
    if (from[i] >= 0)
      results[i] = newLoop.getResult(from[i]);
    else if (invariant[i])
      results[i] = invariant[i];
    else
      results[i] = newLoop.getResult(next++);
  }
  rewriter.replaceOp(loop, results);
}

namespace {

struct RotateDoWhilePass
    : public lcc::impl::LccRotateDoWhileBase<RotateDoWhilePass> {
  void runOnOperation() override {
    // Inner loops first, so that the pointers to the outer ones stay valid.
    SmallVector<scf::WhileOp> loops;
    getOperation()->walk([&](scf::WhileOp loop) { loops.push_back(loop); });
    IRRewriter rewriter(&getContext());
    bool changed = false;
    for (scf::WhileOp loop : loops) {
      std::optional<DoWhile> dw = matchDoWhile(loop);
      if (!dw)
        continue;
      rotate(loop, *dw, rewriter);
      changed = true;
    }
    if (!changed)
      markAllAnalysesPreserved();
  }
};

} // namespace
