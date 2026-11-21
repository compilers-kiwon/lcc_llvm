//===- ScfToAffine.cpp - Raise scf.for loop nests on memrefs to affine ----===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// lcc-scf-to-affine: see Passes.td.  Also LinearAnalysis (see Passes.h),
// which lcc-index-loops and lcc-globals-to-memref share.
//
// A nest is analyzed in its scf form: the bounds of its loops and the
// indices of its accesses become affine expressions of global dimension
// numbers (the position of a loop in the nest) and symbol numbers, which
// the conversion, outer loops first, maps to the operands at hand: the
// induction variables of the new affine.for loops and the symbol values.
// upstream affine-raise-from-memref does not do the accesses: it takes
// arith.addi and arith.muli of index values for affine, but not the
// arith.shli, the extensions and the index casts of lcc's address
// arithmetic, and it leaves the accesses it does not raise as memref.load
// and memref.store in the affine loop, which affine analysis does not see.
//
//===----------------------------------------------------------------------===//

#include "lcc-opt/Passes.h"

#include "mlir/Dialect/Affine/IR/AffineOps.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/IR/AffineExpr.h"
#include "mlir/IR/AffineMap.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "llvm/Support/CheckedArithmetic.h"

#include <string>

namespace lcc {
#define GEN_PASS_DEF_LCCSCFTOAFFINE
#include "lcc-opt/Passes.h.inc"
} // namespace lcc

using namespace mlir;
using lcc::IntRange;
using lcc::LinearForm;

//===----------------------------------------------------------------------===//
// LinearAnalysis
//===----------------------------------------------------------------------===//

unsigned lcc::getIndexWidth(Operation *op) {
  DataLayout layout = DataLayout::closest(op);
  return layout.getTypeSizeInBits(IndexType::get(op->getContext()));
}

/// The deepest LinearAnalysis looks into operands; values deeper down are
/// leaves.
static constexpr unsigned kMaxDepth = 64;

static int64_t minSigned(unsigned width) {
  return width >= 64 ? INT64_MIN : -(int64_t(1) << (width - 1));
}

static int64_t maxSigned(unsigned width) {
  return width >= 64 ? INT64_MAX : (int64_t(1) << (width - 1)) - 1;
}

static LinearForm leafForm(Value v) {
  LinearForm form;
  form.terms.push_back({v, 1});
  return form;
}

/// a + sign * b, or nothing on overflow.
static std::optional<LinearForm> addForms(const LinearForm &a,
                                          const LinearForm &b, int64_t sign) {
  LinearForm r = a;
  std::optional<int64_t> c = llvm::checkedMul(b.constant, sign);
  c = c ? llvm::checkedAdd(r.constant, *c) : std::nullopt;
  if (!c)
    return std::nullopt;
  r.constant = *c;
  for (const auto &term : b.terms) {
    // Not a structured binding: C++17 lambdas cannot capture those.
    Value v = term.first;
    std::optional<int64_t> sk = llvm::checkedMul(term.second, sign);
    if (!sk)
      return std::nullopt;
    auto it = llvm::find_if(r.terms, [&](auto &t) { return t.first == v; });
    if (it == r.terms.end()) {
      r.terms.push_back({v, *sk});
      continue;
    }
    std::optional<int64_t> s = llvm::checkedAdd(it->second, *sk);
    if (!s)
      return std::nullopt;
    it->second = *s;
    if (!*s)
      r.terms.erase(it);
  }
  return r;
}

/// a * k, or nothing on overflow.
static std::optional<LinearForm> scaleForm(const LinearForm &a, int64_t k) {
  LinearForm r;
  if (!k)
    return r;
  std::optional<int64_t> c = llvm::checkedMul(a.constant, k);
  if (!c)
    return std::nullopt;
  r.constant = *c;
  for (auto [v, x] : a.terms) {
    std::optional<int64_t> m = llvm::checkedMul(x, k);
    if (!m)
      return std::nullopt;
    r.terms.push_back({v, *m});
  }
  return r;
}

unsigned lcc::LinearAnalysis::getWidth(Type type) const {
  if (type.isIndex())
    return indexWidth;
  if (auto it = dyn_cast<IntegerType>(type))
    return it.getWidth() <= 64 ? it.getWidth() : 0;
  return 0;
}

LinearForm lcc::LinearAnalysis::getForm(Value v) {
  auto it = forms.find(v);
  if (it != forms.end())
    return it->second;
  LinearForm form = computeForm(v);
  forms[v] = form;
  return form;
}

std::optional<IntRange> lcc::LinearAnalysis::getRange(Value v) {
  return getRange(getForm(v));
}

std::optional<IntRange> lcc::LinearAnalysis::getRange(const LinearForm &form) {
  int64_t lo = form.constant, hi = form.constant;
  for (auto [v, k] : form.terms) {
    std::optional<IntRange> r = getLeafRange(v);
    if (!r)
      return std::nullopt;
    std::optional<int64_t> a = llvm::checkedMul(k, r->min);
    std::optional<int64_t> b = llvm::checkedMul(k, r->max);
    if (!a || !b)
      return std::nullopt;
    std::optional<int64_t> l = llvm::checkedAdd(lo, std::min(*a, *b));
    std::optional<int64_t> h = llvm::checkedAdd(hi, std::max(*a, *b));
    if (!l || !h)
      return std::nullopt;
    lo = *l;
    hi = *h;
  }
  return IntRange{lo, hi};
}

std::optional<IntRange> lcc::LinearAnalysis::getLeafRange(Value v) {
  auto it = leafRanges.find(v);
  if (it != leafRanges.end())
    return it->second;
  std::optional<IntRange> r = computeLeafRange(v);
  leafRanges[v] = r;
  return r;
}

std::optional<IntRange> lcc::LinearAnalysis::computeLeafRange(Value v) {
  unsigned width = getWidth(v.getType());
  if (!width)
    return std::nullopt;
  IntRange full{minSigned(width), maxSigned(width)};
  if (depth > kMaxDepth)
    return full;
  // Operations whose results are non-negative, but not linear forms of
  // their operands: zero extensions of values that may be negative, masks
  // and logical right shifts.
  if (Operation *def = v.getDefiningOp()) {
    auto below = [&](unsigned bits) -> IntRange {
      if (bits >= width)
        return full;
      return IntRange{0, maxSigned(bits + 1)};
    };
    if (auto op = dyn_cast<arith::ExtUIOp>(def))
      return below(getWidth(op.getIn().getType()));
    if (auto op = dyn_cast<arith::IndexCastUIOp>(def)) {
      unsigned from = getWidth(op.getIn().getType());
      return from && from < width ? below(from) : full;
    }
    APInt c;
    if (auto op = dyn_cast<arith::AndIOp>(def)) {
      if ((matchPattern(op.getRhs(), m_ConstantInt(&c)) ||
           matchPattern(op.getLhs(), m_ConstantInt(&c))) &&
          !c.isNegative())
        return IntRange{0, c.getSExtValue()};
      return full;
    }
    if (auto op = dyn_cast<arith::ShRUIOp>(def)) {
      if (matchPattern(op.getRhs(), m_ConstantInt(&c)) && c.ugt(0) &&
          c.ult(width))
        return below(width - c.getZExtValue());
      return full;
    }
    return full;
  }
  auto arg = cast<BlockArgument>(v);
  // An induction variable lies in [lb, ub - 1] whenever the body runs; the
  // values it takes when the body does not run do not matter.
  auto inLoop = [&](std::optional<IntRange> lb,
                    std::optional<IntRange> ub) -> IntRange {
    if (!lb || !ub || ub->max == INT64_MIN)
      return full;
    int64_t hi = ub->max - 1;
    return IntRange{lb->min, std::max(lb->min, hi)};
  };
  Operation *parent = arg.getOwner()->getParentOp();
  if (auto loop = dyn_cast<scf::ForOp>(parent)) {
    if (arg != loop.getInductionVar())
      return full;
    depth++;
    std::optional<IntRange> lb = getRange(loop.getLowerBound());
    std::optional<IntRange> ub = getRange(loop.getUpperBound());
    depth--;
    // Unsigned bounds compare as signed ones when both are non-negative.
    if (loop.getUnsignedCmp() && (!lb || !ub || lb->min < 0 || ub->min < 0))
      return full;
    return inLoop(lb, ub);
  }
  if (auto loop = dyn_cast<affine::AffineForOp>(parent)) {
    if (arg != loop.getInductionVar() || !loop.hasConstantBounds())
      return full;
    return inLoop(IntRange{loop.getConstantLowerBound(),
                           loop.getConstantLowerBound()},
                  IntRange{loop.getConstantUpperBound(),
                           loop.getConstantUpperBound()});
  }
  return full;
}

LinearForm lcc::LinearAnalysis::computeForm(Value v) {
  LinearForm leaf = leafForm(v);
  unsigned width = getWidth(v.getType());
  if (!width)
    return leaf;
  APInt c;
  if (matchPattern(v, m_ConstantInt(&c))) {
    LinearForm form;
    form.constant = c.getSExtValue();
    return form;
  }
  Operation *def = v.getDefiningOp();
  if (!def || depth > kMaxDepth)
    return leaf;
  depth++;
  auto done = [&](std::optional<LinearForm> form) {
    depth--;
    return form ? *form : leaf;
  };
  // `form` if its range fits in the type of v, so that v does not wrap.
  auto ifFits =
      [&](std::optional<LinearForm> form) -> std::optional<LinearForm> {
    if (!form)
      return std::nullopt;
    std::optional<IntRange> r = getRange(*form);
    if (!r || r->min < minSigned(width) || r->max > maxSigned(width))
      return std::nullopt;
    return form;
  };
  // `form`, the form of an operand of `width` bits, if its range is
  // non-negative.
  auto ifNonNegative = [&](LinearForm form) -> std::optional<LinearForm> {
    std::optional<IntRange> r = getRange(form);
    if (!r || r->min < 0)
      return std::nullopt;
    return form;
  };

  if (auto op = dyn_cast<arith::AddIOp>(def))
    return done(
        ifFits(addForms(getForm(op.getLhs()), getForm(op.getRhs()), 1)));
  if (auto op = dyn_cast<arith::SubIOp>(def))
    return done(
        ifFits(addForms(getForm(op.getLhs()), getForm(op.getRhs()), -1)));
  if (auto op = dyn_cast<arith::MulIOp>(def)) {
    LinearForm a = getForm(op.getLhs()), b = getForm(op.getRhs());
    if (b.isConstant())
      return done(ifFits(scaleForm(a, b.constant)));
    if (a.isConstant())
      return done(ifFits(scaleForm(b, a.constant)));
    return done(std::nullopt);
  }
  if (auto op = dyn_cast<arith::ShLIOp>(def)) {
    LinearForm a = getForm(op.getLhs()), b = getForm(op.getRhs());
    if (!b.isConstant() || b.constant < 0 || b.constant >= width ||
        b.constant >= 63)
      return done(std::nullopt);
    return done(ifFits(scaleForm(a, int64_t(1) << b.constant)));
  }
  if (auto op = dyn_cast<arith::ExtSIOp>(def))
    return done(getForm(op.getIn()));
  if (auto op = dyn_cast<arith::ExtUIOp>(def))
    return done(ifNonNegative(getForm(op.getIn())));
  if (auto op = dyn_cast<arith::TruncIOp>(def))
    return done(ifFits(getForm(op.getIn())));
  if (auto op = dyn_cast<arith::IndexCastOp>(def)) {
    unsigned from = getWidth(op.getIn().getType());
    if (!from)
      return done(std::nullopt);
    if (from <= width) // sign-extends
      return done(getForm(op.getIn()));
    return done(ifFits(getForm(op.getIn())));
  }
  if (auto op = dyn_cast<arith::IndexCastUIOp>(def)) {
    unsigned from = getWidth(op.getIn().getType());
    if (!from)
      return done(std::nullopt);
    if (from < width) // zero-extends
      return done(ifNonNegative(getForm(op.getIn())));
    if (from == width) // the same bits
      return done(getForm(op.getIn()));
    return done(ifFits(getForm(op.getIn())));
  }
  return done(std::nullopt);
}

//===----------------------------------------------------------------------===//
// lcc-scf-to-affine
//===----------------------------------------------------------------------===//

namespace {

/// A loop nest to raise, as analyzed in its scf form.
struct Nest {
  scf::ForOp root;
  /// The loops, outer loops before the loops in them.
  SmallVector<scf::ForOp> loops;
  DenseMap<Value, unsigned> dims;
  /// The symbols and their numbers.
  SmallVector<Value> symbols;
  DenseMap<Value, unsigned> symbolNumbers;
  /// The bounds and the step of each loop.
  DenseMap<Operation *, std::pair<AffineExpr, AffineExpr>> bounds;
  DenseMap<Operation *, int64_t> steps;
  /// The accesses and their indices in the order of the IR, the order in
  /// which raise() replaces them: in the order of a map keyed by pointers,
  /// the use lists of the memrefs and induction variables would change from
  /// run to run.
  SmallVector<std::pair<Operation *, AffineExpr>, 0> indices;
};

class Analyzer {
public:
  Analyzer(lcc::LinearAnalysis &la, MLIRContext *ctx) : la(la), ctx(ctx) {}

  /// Whether the nest of `root` may be raised; fills `nest`, or sets
  /// `reason`.
  bool analyze(scf::ForOp root, Nest &nest);

  std::string reason;

private:
  bool fail(Operation *op, const Twine &why) {
    reason = why.str();
    return false;
  }
  bool analyzeLoop(scf::ForOp loop, Nest &nest);
  bool analyzeBlock(Block &block, Nest &nest);
  bool analyzeAccess(Operation *op, Value memref, Value index, Nest &nest);
  std::optional<AffineExpr> toExpr(const LinearForm &form, Nest &nest,
                                   Operation *user);

  lcc::LinearAnalysis &la;
  MLIRContext *ctx;
};

} // namespace

/// Whether `v` is defined in `op`.
static bool isDefinedIn(Value v, Operation *op) {
  Operation *def = v.getDefiningOp();
  if (!def)
    def = cast<BlockArgument>(v).getOwner()->getParentOp();
  return op->isAncestor(def);
}

std::optional<AffineExpr> Analyzer::toExpr(const LinearForm &form, Nest &nest,
                                           Operation *user) {
  AffineExpr e = getAffineConstantExpr(form.constant, ctx);
  for (auto [v, k] : form.terms) {
    AffineExpr x;
    if (auto it = nest.dims.find(v); it != nest.dims.end()) {
      x = getAffineDimExpr(it->second, ctx);
    } else if (!isDefinedIn(v, nest.root)) {
      // A symbol: a valid one of index type, or an integer defined at the
      // top level of the function, which the conversion casts to index
      // after its definition (sign-extending: the value stays).
      bool ok = v.getType().isIndex()
                    ? affine::isValidSymbol(v)
                    : la.getWidth(v.getType()) &&
                          la.getWidth(v.getType()) <=
                              la.getWidth(IndexType::get(ctx)) &&
                          affine::isTopLevelValue(v);
      if (!ok) {
        fail(user, "uses a value of an enclosing region");
        return std::nullopt;
      }
      auto [it, inserted] =
          nest.symbolNumbers.try_emplace(v, nest.symbols.size());
      if (inserted)
        nest.symbols.push_back(v);
      x = getAffineSymbolExpr(it->second, ctx);
    } else {
      fail(user, "not affine");
      return std::nullopt;
    }
    e = e + x * k;
  }
  return e;
}

bool Analyzer::analyzeAccess(Operation *op, Value memref, Value index,
                             Nest &nest) {
  if (!memref.getDefiningOp<memref::GetGlobalOp>())
    return fail(op, "memref not from memref.get_global");
  auto type = cast<MemRefType>(memref.getType());
  if (type.getRank() != 1 || !type.hasStaticShape() ||
      !type.getLayout().isIdentity())
    return fail(op, "memref not 1-D of a static size");
  LinearForm form = la.getForm(index);
  std::optional<AffineExpr> e = toExpr(form, nest, op);
  if (!e)
    return false;
  std::optional<IntRange> r = la.getRange(form);
  if (!r || r->min < 0 || r->max >= type.getDimSize(0))
    return fail(op, "index may be out of bounds");
  nest.indices.push_back({op, *e});
  return true;
}

bool Analyzer::analyzeBlock(Block &block, Nest &nest) {
  for (Operation &op : block) {
    if (isa<scf::YieldOp>(op))
      continue;
    if (auto loop = dyn_cast<scf::ForOp>(op)) {
      if (!analyzeLoop(loop, nest))
        return false;
      continue;
    }
    if (auto ifOp = dyn_cast<scf::IfOp>(op)) {
      for (Region &region : ifOp->getRegions())
        for (Block &b : region)
          if (!analyzeBlock(b, nest))
            return false;
      continue;
    }
    if (auto load = dyn_cast<memref::LoadOp>(op)) {
      if (load.getIndices().size() != 1)
        return fail(&op, "memref not 1-D of a static size");
      if (!analyzeAccess(&op, load.getMemRef(), load.getIndices()[0], nest))
        return false;
      continue;
    }
    if (auto store = dyn_cast<memref::StoreOp>(op)) {
      if (store.getIndices().size() != 1)
        return fail(&op, "memref not 1-D of a static size");
      if (!analyzeAccess(&op, store.getMemRef(), store.getIndices()[0], nest))
        return false;
      continue;
    }
    bool isArith = isa<arith::ArithDialect>(op.getDialect());
    if ((isArith || op.hasTrait<OpTrait::ConstantLike>() ||
         isa<ub::PoisonOp>(op)) &&
        op.getNumRegions() == 0 && isMemoryEffectFree(&op))
      continue;
    return fail(&op, op.getName().getStringRef());
  }
  return true;
}

bool Analyzer::analyzeLoop(scf::ForOp loop, Nest &nest) {
  if (!loop.getInductionVar().getType().isIndex())
    return fail(loop, "not an index loop");
  if (loop.getUnsignedCmp())
    return fail(loop, "unsigned loop");
  APInt step;
  if (!matchPattern(loop.getStep(), m_ConstantInt(&step)) ||
      !step.isStrictlyPositive() || step.getActiveBits() > 63)
    return fail(loop, "step not a positive constant");
  std::optional<AffineExpr> lb = toExpr(la.getForm(loop.getLowerBound()),
                                        nest, loop);
  if (!lb)
    return false;
  std::optional<AffineExpr> ub = toExpr(la.getForm(loop.getUpperBound()),
                                        nest, loop);
  if (!ub)
    return false;
  nest.bounds[loop] = {*lb, *ub};
  nest.steps[loop] = step.getSExtValue();
  nest.dims[loop.getInductionVar()] = nest.loops.size();
  nest.loops.push_back(loop);
  return analyzeBlock(*loop.getBody(), nest);
}

bool Analyzer::analyze(scf::ForOp root, Nest &nest) {
  nest.root = root;
  return analyzeLoop(root, nest);
}

/// A map of the dimensions and symbols `e` uses, and their operands from
/// `dims` and `symbols`.
static std::pair<AffineMap, SmallVector<Value>>
makeMap(AffineExpr e, ArrayRef<Value> dims, ArrayRef<Value> symbols) {
  MLIRContext *ctx = e.getContext();
  SmallVector<AffineExpr> dimReplacements, symReplacements;
  SmallVector<Value> operands;
  unsigned numDims = 0, numSyms = 0;
  for (unsigned i = 0; i < dims.size(); i++) {
    if (e.isFunctionOfDim(i)) {
      dimReplacements.push_back(getAffineDimExpr(numDims++, ctx));
      operands.push_back(dims[i]);
    } else {
      dimReplacements.push_back(getAffineConstantExpr(0, ctx));
    }
  }
  SmallVector<Value> symOperands;
  for (unsigned i = 0; i < symbols.size(); i++) {
    if (e.isFunctionOfSymbol(i)) {
      symReplacements.push_back(getAffineSymbolExpr(numSyms++, ctx));
      symOperands.push_back(symbols[i]);
    } else {
      symReplacements.push_back(getAffineConstantExpr(0, ctx));
    }
  }
  AffineExpr r = e.replaceDimsAndSymbols(dimReplacements, symReplacements);
  llvm::append_range(operands, symOperands);
  return {AffineMap::get(numDims, numSyms, r), operands};
}

/// Raises the nest analyzed in `nest`.
static void raise(Nest &nest, IRRewriter &rewriter) {
  // The symbols as index values: integers are cast after their definition,
  // at the top level of the function.
  SmallVector<Value> symbols;
  for (Value v : nest.symbols) {
    if (v.getType().isIndex()) {
      symbols.push_back(v);
      continue;
    }
    if (auto arg = dyn_cast<BlockArgument>(v))
      rewriter.setInsertionPointToStart(arg.getOwner());
    else
      rewriter.setInsertionPointAfter(v.getDefiningOp());
    symbols.push_back(arith::IndexCastOp::create(
        rewriter, v.getLoc(), rewriter.getIndexType(), v));
  }

  // The loops, outer loops first, so that the induction variables of the
  // enclosing loops are there for the bounds of the inner ones.
  SmallVector<Value> ivs(nest.loops.size());
  for (auto [i, loop] : llvm::enumerate(nest.loops)) {
    auto [lbExpr, ubExpr] = nest.bounds[loop];
    auto [lbMap, lbOperands] = makeMap(lbExpr, ivs, symbols);
    auto [ubMap, ubOperands] = makeMap(ubExpr, ivs, symbols);
    rewriter.setInsertionPoint(loop);
    auto newLoop = affine::AffineForOp::create(
        rewriter, loop.getLoc(), lbOperands, lbMap, ubOperands, ubMap,
        nest.steps[loop], loop.getInitArgs());
    Block *body = newLoop.getBody();
    if (!body->empty())
      rewriter.eraseOp(body->getTerminator());
    rewriter.mergeBlocks(loop.getBody(), body, body->getArguments());
    auto yield = cast<scf::YieldOp>(body->getTerminator());
    rewriter.setInsertionPoint(yield);
    rewriter.replaceOpWithNewOp<affine::AffineYieldOp>(yield,
                                                       yield.getOperands());
    rewriter.replaceOp(loop, newLoop.getResults());
    ivs[i] = newLoop.getInductionVar();
  }

  // The accesses.
  for (auto &[op, e] : nest.indices) {
    auto [map, operands] = makeMap(e, ivs, symbols);
    rewriter.setInsertionPoint(op);
    if (auto load = dyn_cast<memref::LoadOp>(op))
      rewriter.replaceOpWithNewOp<affine::AffineLoadOp>(
          load, load.getMemRef(), map, operands);
    else if (auto store = dyn_cast<memref::StoreOp>(op))
      rewriter.replaceOpWithNewOp<affine::AffineStoreOp>(
          store, store.getValueToStore(), store.getMemRef(), map, operands);
  }
}

namespace {

struct ScfToAffinePass
    : public lcc::impl::LccScfToAffineBase<ScfToAffinePass> {
  using LccScfToAffineBase::LccScfToAffineBase;

  /// Whether `loop` is in another loop, for the remarks.
  static bool inner(scf::ForOp loop) {
    return loop->getParentOfType<scf::ForOp>() ||
           loop->getParentOfType<scf::WhileOp>();
  }

  void runOnOperation() override {
    // The nests, found before any change.  They are disjoint, but a later
    // nest may use a result of an earlier one, as a bound say, which
    // raising the earlier one replaces: they are raised last first.
    SmallVector<Nest> nests;
    if (remarks)
      getOperation()->walk([&](LLVM::LLVMFuncOp func) {
        func.walk<WalkOrder::PreOrder>([&](scf::ForOp loop) {
          loop.emitRemark() << "lcc-scf-to-affine: loop stays: in llvm.func";
          return WalkResult::skip();
        });
      });
    getOperation()->walk([&](func::FuncOp func) {
      lcc::LinearAnalysis la(lcc::getIndexWidth(func));
      func.walk<WalkOrder::PreOrder>([&](scf::ForOp loop) {
        Analyzer analyzer(la, &getContext());
        Nest nest;
        if (analyzer.analyze(loop, nest)) {
          if (remarks)
            loop.emitRemark() << "lcc-scf-to-affine: raised "
                              << (inner(loop) ? "an inner" : "a") << " nest of "
                              << nest.loops.size() << " loop"
                              << (nest.loops.size() > 1 ? "s" : "");
          nests.push_back(std::move(nest));
          return WalkResult::skip();
        }
        if (remarks)
          loop.emitRemark() << "lcc-scf-to-affine: "
                            << (inner(loop) ? "inner loop" : "loop")
                            << " stays: " << analyzer.reason;
        return WalkResult::advance();
      });
    });
    IRRewriter rewriter(&getContext());
    for (Nest &nest : llvm::reverse(nests))
      raise(nest, rewriter);
  }
};

} // namespace
