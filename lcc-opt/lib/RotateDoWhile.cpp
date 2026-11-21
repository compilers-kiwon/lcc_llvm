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
//   while (x = init) { body(x); condition(P(next(x), ub)) v } do (y) { yield y[fwd] }
//
// becomes
//
//   while (x = init) { condition(P(x[iv], ub)) x } do (x) { body(x); yield v[fwd] }
//
// where next(x) is v[fwd[iv]], so every test after the first compares the
// very values the old loop compared, whatever P and next are, and the first
// test, P(init[iv], ub), is true as in the old loop, which always runs its
// body once.  (ub may be on the left of P as well.)  A result of the old
// loop is v[i] of the last iteration; for i = fwd[j] that is the new result
// j.  The other v[i] are dealt with in rotate().
//
//===----------------------------------------------------------------------===//

#include "lcc-opt/Passes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/LLVMIR/LLVMTypes.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
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
  /// The bound, defined outside the loop.
  Value ub;
  /// The old comparison, `P(next, ub)` or `P(ub, next)`.
  arith::CmpIOp cmp;
  /// Whether next is on the left of the comparison.
  bool left;
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

  // The condition is `cmpi P, next, ub` or `cmpi P, ub, next`, with ub
  // defined outside the loop and next the value passed on to a before block
  // argument iv, and P(init[iv], ub) folds to true.
  dw.cmp = cond.getCondition().getDefiningOp<arith::CmpIOp>();
  if (!dw.cmp || dw.cmp->getBlock() != before)
    return std::nullopt;
  for (bool left : {true, false}) {
    Value next = left ? dw.cmp.getLhs() : dw.cmp.getRhs();
    Value ub = left ? dw.cmp.getRhs() : dw.cmp.getLhs();
    APInt bound;
    if (!loop.isDefinedOutsideOfLoop(ub) ||
        !matchPattern(ub, m_ConstantInt(&bound)))
      continue;
    for (unsigned j = 0; j < dw.fwd.size(); j++) {
      APInt init;
      if (cond.getArgs()[dw.fwd[j]] != next ||
          !matchPattern(loop.getInits()[j], m_ConstantInt(&init)))
        continue;
      arith::CmpIPredicate p = dw.cmp.getPredicate();
      if (!(left ? arith::applyCmpPredicate(p, init, bound)
                 : arith::applyCmpPredicate(p, bound, init)))
        continue;
      dw.iv = j;
      dw.ub = ub;
      dw.left = left;
      return dw;
    }
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

/// Returns whether zeroValue() can make a value of type `type`.
static bool hasZero(Type type) {
  return isa<IntegerType, IndexType, FloatType>(type) ||
         LLVM::isCompatibleType(type);
}

/// Returns a zero of type `type`: arith.constant for integers and floats,
/// llvm.mlir.zero for the other LLVM types (pointers, structs, arrays).
static Value zeroValue(OpBuilder &builder, Location loc, Type type) {
  if (isa<IntegerType, IndexType, FloatType>(type))
    return arith::ConstantOp::create(
        builder, loc, cast<TypedAttr>(builder.getZeroAttr(type)));
  return LLVM::ZeroOp::create(builder, loc, type);
}

/// Rotates the do-while loop `loop` described by `dw`, and returns true, or
/// returns false and leaves the IR alone.
static bool rotate(scf::WhileOp loop, DoWhile &dw, RewriterBase &rewriter) {
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
  // Otherwise the new loop carries it, from a zero that is never read: the
  // body runs at least once.
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
  for (unsigned i : carried)
    if (!hasZero(cond.getArgs()[i].getType()))
      return false;
  for (Operation *op : hoist)
    rewriter.moveOpBefore(op, loop);

  Location loc = loop.getLoc();
  rewriter.setInsertionPoint(loop);
  SmallVector<Value> inits(loop.getInits());
  for (unsigned i : carried)
    inits.push_back(zeroValue(rewriter, loc, cond.getArgs()[i].getType()));
  SmallVector<Type> types(ValueRange(inits).getTypes());
  auto newLoop = scf::WhileOp::create(rewriter, loc, types, inits,
                                      /*beforeBuilder=*/nullptr,
                                      /*afterBuilder=*/nullptr);

  // before: test the induction variable and pass everything on.
  Block *newBefore = newLoop.getBeforeBody();
  rewriter.setInsertionPointToStart(newBefore);
  Value iv = newBefore->getArgument(dw.iv);
  Value test = arith::CmpIOp::create(rewriter, dw.cmp.getLoc(),
                                     dw.cmp.getPredicate(),
                                     dw.left ? iv : dw.ub, dw.left ? dw.ub : iv);
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
  return true;
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
      if (dw && rotate(loop, *dw, rewriter))
        changed = true;
    }
    if (!changed)
      markAllAnalysesPreserved();
  }
};

} // namespace
