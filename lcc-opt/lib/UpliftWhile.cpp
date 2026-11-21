//===- UpliftWhile.cpp - Uplift while shaped scf.while to scf.for ---------===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// lcc-uplift-while: see Passes.td.  A while shaped scf.while
//
//   while (x = init) { condition(P(x[iv], bound)) x } do (x) { yield f(x) }
//
// with f(x)[iv] = x[iv] + s or x[iv] - s for a constant s counts x[iv]
// from init towards bound.  scf.for leaves undefined a loop whose final
// induction variable does not fit in its type, so the loop becomes an
// scf.for only when that can be shown, on constants, not to happen, and
// the final value of x[iv], a result of the scf.while, is computed exactly:
//
// - counting up, P(iv, end) is `iv < end` (slt, ult; sle, ule with
//   end = bound + 1; ne with s = 1 and init <= bound on constants, or bound
//   the largest value of its type), and the loop is
//   `scf.for [unsigned] iv = init to end step s`.  With s = 1 the final iv
//   is max(init, end) and fits; with s > 1 it is below end + s, which must
//   fit.  It is init + n * s for the trip count n = (end - init - 1) / s + 1
//   in unsigned arithmetic of the width of iv, exact because the result
//   fits, or init when init >= end.
// - counting down, P(iv, end) is `iv > end` (sgt, ugt; sge, uge with
//   end = bound - 1; ne with s = 1 and init >= bound on constants, or
//   bound the smallest value, as in `u != 0`), and the loop is
//   `scf.for unsigned k = 0 to n step 1` with iv = init - k * s, n =
//   (init - end - 1) / s + 1 or 0, and final iv init - n * s.  init - end
//   fits as an unsigned number; with s > 1, end - s + 1 must not wrap.
//
//===----------------------------------------------------------------------===//

#include "lcc-opt/Passes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"

#include <optional>

namespace lcc {
#define GEN_PASS_DEF_LCCUPLIFTWHILE
#include "lcc-opt/Passes.h.inc"
} // namespace lcc

using namespace mlir;
using arith::CmpIPredicate;

namespace {

/// How a while loop counts.
struct Counting {
  /// The before block argument that counts, and the condition argument,
  /// after block argument and result of it.
  unsigned iv, result;
  /// Up (+step) or down (-step); the comparisons are unsigned.
  bool up, isUnsigned;
  /// The step, positive.
  APInt step;
  /// The bound the loop stops at: iv < end counting up, iv > end counting
  /// down; a value defined outside the loop or a constant.
  Value end;
  std::optional<APInt> endConst;
};

} // namespace

/// Returns `p` with the operands swapped.
static CmpIPredicate swapped(CmpIPredicate p) {
  switch (p) {
  case CmpIPredicate::slt: return CmpIPredicate::sgt;
  case CmpIPredicate::sle: return CmpIPredicate::sge;
  case CmpIPredicate::sgt: return CmpIPredicate::slt;
  case CmpIPredicate::sge: return CmpIPredicate::sle;
  case CmpIPredicate::ult: return CmpIPredicate::ugt;
  case CmpIPredicate::ule: return CmpIPredicate::uge;
  case CmpIPredicate::ugt: return CmpIPredicate::ult;
  case CmpIPredicate::uge: return CmpIPredicate::ule;
  default: return p;
  }
}

/// `v` as a signed or unsigned number in 2 more bits than it has.
static APInt widen(const APInt &v, bool isUnsigned) {
  return isUnsigned ? v.zext(v.getBitWidth() + 2) : v.sext(v.getBitWidth() + 2);
}

/// Returns how `loop` counts, or nothing when it does not, as described
/// above, or the bounds cannot be shown to keep it from wrapping.
static std::optional<Counting> matchCounting(scf::WhileOp loop) {
  Block *before = loop.getBeforeBody();
  if (!llvm::hasSingleElement(before->without_terminator()))
    return std::nullopt;
  auto cmp = dyn_cast<arith::CmpIOp>(before->front());
  scf::ConditionOp cond = loop.getConditionOp();
  if (!cmp || cond.getCondition() != cmp.getResult())
    return std::nullopt;
  // The condition passes on every before block argument once.
  unsigned numArgs = before->getNumArguments();
  if (cond.getArgs().size() != numArgs)
    return std::nullopt;
  SmallVector<int> pos(numArgs, -1);
  for (auto [j, v] : llvm::enumerate(cond.getArgs())) {
    auto arg = dyn_cast<BlockArgument>(v);
    if (!arg || arg.getOwner() != before || pos[arg.getArgNumber()] >= 0)
      return std::nullopt;
    pos[arg.getArgNumber()] = j;
  }

  // P(iv, bound), iv a before block argument, bound defined outside.
  BlockArgument iv;
  Value bound;
  CmpIPredicate p;
  for (bool left : {true, false}) {
    auto arg = dyn_cast<BlockArgument>(left ? cmp.getLhs() : cmp.getRhs());
    Value other = left ? cmp.getRhs() : cmp.getLhs();
    if (arg && arg.getOwner() == before && loop.isDefinedOutsideOfLoop(other)) {
      iv = arg;
      bound = other;
      p = left ? cmp.getPredicate() : swapped(cmp.getPredicate());
      break;
    }
  }
  if (!iv || !isa<IntegerType>(iv.getType()))
    return std::nullopt;
  unsigned width = iv.getType().getIntOrFloatBitWidth();

  Counting c;
  c.iv = iv.getArgNumber();
  c.result = pos[c.iv];
  Value ivAfter = loop.getAfterBody()->getArgument(c.result);

  // The after region yields iv + s or iv - s for iv.
  Value next = loop.getYieldOp().getResults()[c.iv];
  APInt k;
  if (auto add = next.getDefiningOp<arith::AddIOp>()) {
    Value other = add.getLhs() == ivAfter   ? add.getRhs()
                  : add.getRhs() == ivAfter ? add.getLhs()
                                            : Value();
    if (!other || !matchPattern(other, m_ConstantInt(&k)))
      return std::nullopt;
  } else if (auto sub = next.getDefiningOp<arith::SubIOp>()) {
    if (sub.getLhs() != ivAfter || !matchPattern(sub.getRhs(), m_ConstantInt(&k)))
      return std::nullopt;
    if (k.isMinSignedValue())
      return std::nullopt;
    k = -k;
  } else {
    return std::nullopt;
  }
  if (k.isZero())
    return std::nullopt;
  c.up = k.isStrictlyPositive();
  c.step = c.up ? k : -k;

  APInt init, b;
  bool initConst = matchPattern(loop.getInits()[c.iv], m_ConstantInt(&init));
  bool boundConst = matchPattern(bound, m_ConstantInt(&b));
  APInt one(width, 1);
  if (c.up) {
    switch (p) {
    case CmpIPredicate::slt:
    case CmpIPredicate::ult:
      c.isUnsigned = p == CmpIPredicate::ult;
      c.end = bound;
      if (boundConst)
        c.endConst = b;
      break;
    case CmpIPredicate::sle:
    case CmpIPredicate::ule:
      c.isUnsigned = p == CmpIPredicate::ule;
      if (!boundConst || (c.isUnsigned ? b.isMaxValue() : b.isMaxSignedValue()))
        return std::nullopt;
      c.endConst = b + one;
      break;
    case CmpIPredicate::ne:
      // init <= bound, signed or unsigned, on constants or because bound
      // is the largest value.
      if (!c.step.isOne() || !boundConst)
        return std::nullopt;
      if (initConst ? init.sle(b) : b.isMaxSignedValue())
        c.isUnsigned = false;
      else if (initConst ? init.ule(b) : b.isMaxValue())
        c.isUnsigned = true;
      else
        return std::nullopt;
      c.endConst = b;
      break;
    default:
      return std::nullopt;
    }
    // With s > 1, end + s - 1 must fit.
    if (!c.step.isOne()) {
      if (!c.endConst)
        return std::nullopt;
      APInt last = widen(*c.endConst, c.isUnsigned) +
                   widen(c.step, true) - widen(one, true);
      if (c.isUnsigned ? !last.isIntN(width) : !last.isSignedIntN(width))
        return std::nullopt;
    }
  } else {
    switch (p) {
    case CmpIPredicate::sgt:
    case CmpIPredicate::ugt:
      c.isUnsigned = p == CmpIPredicate::ugt;
      c.end = bound;
      if (boundConst)
        c.endConst = b;
      break;
    case CmpIPredicate::sge:
    case CmpIPredicate::uge:
      c.isUnsigned = p == CmpIPredicate::uge;
      if (!boundConst || (c.isUnsigned ? b.isZero() : b.isMinSignedValue()))
        return std::nullopt;
      c.endConst = b - one;
      break;
    case CmpIPredicate::ne:
      // init >= bound, signed or unsigned, on constants or because bound
      // is the smallest value (u != 0 counting down).
      if (!c.step.isOne() || !boundConst)
        return std::nullopt;
      if (initConst ? init.sge(b) : b.isMinSignedValue())
        c.isUnsigned = false;
      else if (initConst ? init.uge(b) : b.isZero())
        c.isUnsigned = true;
      else
        return std::nullopt;
      c.endConst = b;
      break;
    default:
      return std::nullopt;
    }
    // With s > 1, end - s + 1 must not wrap.
    if (!c.step.isOne()) {
      if (!c.endConst)
        return std::nullopt;
      APInt last = widen(*c.endConst, c.isUnsigned) -
                   widen(c.step, true) + widen(one, true);
      if (c.isUnsigned ? last.isNegative() : !last.isSignedIntN(width))
        return std::nullopt;
    }
  }
  return c;
}

namespace {

struct UpliftWhile : public OpRewritePattern<scf::WhileOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(scf::WhileOp loop,
                                PatternRewriter &rewriter) const override {
    std::optional<Counting> c = matchCounting(loop);
    if (!c)
      return rewriter.notifyMatchFailure(loop, "not a counting loop");
    Location loc = loop.getLoc();
    Value init = loop.getInits()[c->iv];
    Type type = init.getType();
    auto constant = [&](const APInt &v) -> Value {
      return arith::ConstantOp::create(rewriter, loc,
                                       rewriter.getIntegerAttr(type, v));
    };
    unsigned width = type.getIntOrFloatBitWidth();

    // The iter_args: the before block arguments other than iv, in order.
    unsigned numArgs = loop.getBeforeBody()->getNumArguments();
    SmallVector<Value> inits;
    SmallVector<int> slot(numArgs, -1);
    for (unsigned i = 0; i < numArgs; i++)
      if (i != c->iv) {
        slot[i] = inits.size();
        inits.push_back(loop.getInits()[i]);
      }

    rewriter.setInsertionPoint(loop);
    Value end = c->endConst ? constant(*c->endConst) : c->end;
    Value step = constant(c->step);
    Value zero = constant(APInt(width, 0));
    Value one = constant(APInt(width, 1));
    auto pred = [&](CmpIPredicate s, CmpIPredicate u) {
      return c->isUnsigned ? u : s;
    };
    // n = (a - b - 1) / s + 1 if a > b, else 0, unsigned in the width of iv.
    auto tripCount = [&](Value a, Value b, CmpIPredicate gt) -> Value {
      Value runs = arith::CmpIOp::create(rewriter, loc, gt, a, b);
      Value diff = arith::SubIOp::create(rewriter, loc, a, b);
      Value n = diff;
      if (!c->step.isOne()) {
        Value d = arith::SubIOp::create(rewriter, loc, diff, one);
        d = arith::DivUIOp::create(rewriter, loc, d, step);
        n = arith::AddIOp::create(rewriter, loc, d, one);
      }
      return arith::SelectOp::create(rewriter, loc, runs, n, zero);
    };

    auto empty = [](OpBuilder &, Location, Value, ValueRange) {};
    scf::ForOp forOp;
    Value ivInBody, trip;
    if (c->up) {
      forOp = scf::ForOp::create(rewriter, loc, init, end, step, inits, empty,
                                 c->isUnsigned);
      ivInBody = forOp.getInductionVar();
    } else {
      trip = tripCount(init, end, pred(CmpIPredicate::sgt, CmpIPredicate::ugt));
      forOp = scf::ForOp::create(rewriter, loc, zero, trip, one, inits, empty,
                                 /*unsignedCmp=*/true);
      rewriter.setInsertionPointToStart(forOp.getBody());
      Value k = forOp.getInductionVar();
      if (!c->step.isOne())
        k = arith::MulIOp::create(rewriter, loc, k, step);
      ivInBody = arith::SubIOp::create(rewriter, loc, init, k);
    }

    // The after block, with iv and the iter_args for its arguments, becomes
    // the body; its yield drops iv.
    Block *body = forOp.getBody();
    Block *after = loop.getAfterBody();
    scf::ConditionOp cond = loop.getConditionOp();
    SmallVector<Value> args(after->getNumArguments());
    for (auto [j, v] : llvm::enumerate(cond.getArgs())) {
      unsigned i = cast<BlockArgument>(v).getArgNumber();
      args[j] = i == c->iv ? ivInBody : forOp.getRegionIterArgs()[slot[i]];
    }
    rewriter.inlineBlockBefore(after, body, body->end(), args);
    auto oldYield = cast<scf::YieldOp>(body->getTerminator());
    SmallVector<Value> yields;
    for (unsigned i = 0; i < numArgs; i++)
      if (i != c->iv)
        yields.push_back(oldYield.getResults()[i]);
    rewriter.setInsertionPoint(oldYield);
    rewriter.replaceOpWithNewOp<scf::YieldOp>(oldYield, yields);

    // The final iv.
    rewriter.setInsertionPointAfter(forOp);
    Value last;
    if (c->up) {
      Value n = tripCount(end, init, pred(CmpIPredicate::sgt, CmpIPredicate::ugt));
      Value m = c->step.isOne() ? n : arith::MulIOp::create(rewriter, loc, n, step);
      last = arith::AddIOp::create(rewriter, loc, init, m);
    } else {
      Value m = c->step.isOne() ? trip : arith::MulIOp::create(rewriter, loc, trip, step);
      last = arith::SubIOp::create(rewriter, loc, init, m);
    }

    SmallVector<Value> results;
    for (Value v : cond.getArgs()) {
      unsigned i = cast<BlockArgument>(v).getArgNumber();
      results.push_back(i == c->iv ? last : forOp.getResult(slot[i]));
    }
    rewriter.replaceOp(loop, results);
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
