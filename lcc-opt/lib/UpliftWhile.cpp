//===- UpliftWhile.cpp - Uplift while shaped scf.while to scf.for ---------===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// lcc-uplift-while: see Passes.td.  The pattern finds the induction
// variable and the step as scf::upliftWhileToForLoop does, checks that the
// step is a positive constant, lets upliftWhileToForLoop do the work and
// then replaces its value of the final induction variable.
//
//===----------------------------------------------------------------------===//

#include "lcc-opt/Passes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/SCF/Transforms/Transforms.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Interfaces/LoopLikeInterface.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"

namespace lcc {
#define GEN_PASS_DEF_LCCUPLIFTWHILE
#include "lcc-opt/Passes.h.inc"
} // namespace lcc

using namespace mlir;

namespace {

struct UpliftWhile : public OpRewritePattern<scf::WhileOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(scf::WhileOp loop,
                                PatternRewriter &rewriter) const override {
    // The before block is `cmpi slt, iv, ub` or `cmpi sgt, ub, iv` and the
    // condition, with iv a before block argument.
    Block *before = loop.getBeforeBody();
    if (!llvm::hasSingleElement(before->without_terminator()))
      return rewriter.notifyMatchFailure(loop, "not a single cmpi");
    auto cmp = dyn_cast<arith::CmpIOp>(before->front());
    if (!cmp)
      return rewriter.notifyMatchFailure(loop, "not a single cmpi");
    Value ivValue, ub;
    if (cmp.getPredicate() == arith::CmpIPredicate::slt) {
      ivValue = cmp.getLhs();
      ub = cmp.getRhs();
    } else if (cmp.getPredicate() == arith::CmpIPredicate::sgt) {
      ivValue = cmp.getRhs();
      ub = cmp.getLhs();
    } else {
      return rewriter.notifyMatchFailure(loop, "not slt or sgt");
    }
    auto iv = dyn_cast<BlockArgument>(ivValue);
    if (!iv || iv.getOwner() != before)
      return rewriter.notifyMatchFailure(loop, "no induction variable");

    // The result of iv is the condition argument that passes iv on, and
    // the after block argument of that number is iv in the body.
    scf::ConditionOp cond = loop.getConditionOp();
    auto pos = llvm::find(cond.getArgs(), iv);
    if (pos == cond.getArgs().end())
      return rewriter.notifyMatchFailure(loop, "iv not passed on");
    unsigned result = pos - cond.getArgs().begin();
    Value ivAfter = loop.getAfterBody()->getArgument(result);

    // The after region yields `addi iv, step` for iv; step must be a
    // positive constant defined outside the loop.
    auto add = loop.getYieldOp()
                   .getResults()[iv.getArgNumber()]
                   .getDefiningOp<arith::AddIOp>();
    if (!add)
      return rewriter.notifyMatchFailure(loop, "no addi");
    Value step = add.getLhs() == ivAfter   ? add.getRhs()
                 : add.getRhs() == ivAfter ? add.getLhs()
                                           : Value();
    APInt stepValue;
    if (!step || !loop.isDefinedOutsideOfLoop(step) ||
        !matchPattern(step, m_ConstantInt(&stepValue)) ||
        !stepValue.isStrictlyPositive())
      return rewriter.notifyMatchFailure(loop, "step not a positive constant");
    Value lb = loop.getInits()[iv.getArgNumber()];

    SmallVector<OpOperand *> uses;
    for (OpOperand &use : loop.getResult(result).getUses())
      uses.push_back(&use);

    FailureOr<scf::ForOp> forOp = scf::upliftWhileToForLoop(rewriter, loop);
    if (failed(forOp))
      return failure();
    if (uses.empty())
      return success();

    // upliftWhileToForLoop has given the uses of the final iv
    // lb + ((ub - lb + step - 1) / step - 1) * step, the iv of the last
    // iteration.  The scf.while gave the first iv not less than ub, or lb.
    Value exit;
    rewriter.setInsertionPointAfter(*forOp);
    APInt lbValue, ubValue;
    if (matchPattern(lb, m_ConstantInt(&lbValue)) &&
        matchPattern(ub, m_ConstantInt(&ubValue))) {
      // With w bits, n * step < 2^(2w); compute with 2w + 2 bits.
      unsigned width = lbValue.getBitWidth();
      unsigned wide = 2 * width + 2;
      APInt l = lbValue.sext(wide), u = ubValue.sext(wide);
      APInt s = stepValue.sext(wide), e = l;
      if (l.slt(u)) {
        APInt n = (u - l + s - 1).sdiv(s);
        e = l + n * s;
      }
      // Otherwise the loop overflows: scf.for leaves that undefined.
      if (e.isSignedIntN(width)) {
        exit = arith::ConstantOp::create(
            rewriter, loop.getLoc(),
            rewriter.getIntegerAttr(lb.getType(), e.trunc(width)));
      }
    }
    if (!exit) {
      // Carry `iv + step` from lb.  The addi is now in the scf.for body.
      // scf.for always takes another iter_arg, and the IR has changed: a
      // failure here could not be undone.
      Value next = add.getResult();
      FailureOr<LoopLikeOpInterface> newLoop = forOp->replaceWithAdditionalYields(
          rewriter, ValueRange{lb}, /*replaceInitOperandUsesInLoop=*/false,
          [&](OpBuilder &, Location, ArrayRef<BlockArgument>) {
            return SmallVector<Value>{next};
          });
      if (failed(newLoop))
        llvm::report_fatal_error("lcc-uplift-while: scf.for refused an "
                                 "iter_arg");
      exit = newLoop->getOperation()->getResults().back();
    }
    for (OpOperand *use : uses)
      rewriter.modifyOpInPlace(use->getOwner(), [&] { use->set(exit); });
    return success();
  }
};

struct UpliftWhilePass
    : public lcc::impl::LccUpliftWhileBase<UpliftWhilePass> {
  void runOnOperation() override {
    RewritePatternSet patterns(&getContext());
    patterns.add<UpliftWhile>(&getContext());
    // Only uplift: no folding, no region simplification.  The pipeline
    // canonicalizes afterwards.
    GreedyRewriteConfig config;
    config.enableFolding(false)
        .enableConstantCSE(false)
        .setRegionSimplificationLevel(GreedySimplifyRegionLevel::Disabled);
    if (failed(applyPatternsGreedily(getOperation(), std::move(patterns),
                                     config)))
      return signalPassFailure();
  }
};

} // namespace
