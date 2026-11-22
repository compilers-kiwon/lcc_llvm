//===- Passes.h - Passes and pipelines of lcc-opt ---------------*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LCC_OPT_PASSES_H
#define LCC_OPT_PASSES_H

#include "mlir/IR/AffineExpr.h"
#include "mlir/IR/Value.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassOptions.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace mlir {
class OpPassManager;
} // namespace mlir

namespace lcc {

#define GEN_PASS_DECL
#include "lcc-opt/Passes.h.inc"

/// A range [min, max] of signed values.
struct IntRange {
  int64_t min, max;
};

/// An integer or index value as `constant + sum of coef * leaf`, exact over
/// the integers: with every value taken as signed, the value is that sum for
/// every execution, wraparound included, so nothing relies on an operation
/// not overflowing.
struct LinearForm {
  llvm::SmallVector<std::pair<mlir::Value, int64_t>, 4> terms;
  int64_t constant = 0;
  bool isConstant() const { return terms.empty(); }
};

/// Exact linear forms and ranges of integer (up to 64 bits) and index values,
/// for lcc-index-loops, lcc-globals-to-memref and lcc-scf-to-affine (defined
/// in ScfToAffine.cpp).  It looks through arith.addi, subi, muli and shli by
/// constants, extsi, extui, trunci, index_cast and index_castui, and takes
/// an operation for a leaf where its result may wrap or the cast may change
/// the value, as shown by the ranges of the operands.  A leaf has the full
/// range of its type, but the induction variable of an scf.for (or of an
/// affine.for with constant bounds) lies in [lower bound, upper bound - 1]
/// whenever the body runs, and arith.extui, index_castui (from a narrower
/// type), andi with a non-negative constant and shrui by a positive constant
/// give non-negative numbers below a power of two or the constant.  The
/// results are cached: an analysis is for one state of the IR.
class LinearAnalysis {
public:
  explicit LinearAnalysis(unsigned indexWidth) : indexWidth(indexWidth) {}

  /// The form of `v`; `v` itself, with coefficient 1, when nothing better.
  LinearForm getForm(mlir::Value v);
  /// A range of every value of `v`, or nothing when it is not an integer of
  /// at most 64 bits or an index, or the bounds do not fit in int64_t.
  std::optional<IntRange> getRange(mlir::Value v);
  std::optional<IntRange> getRange(const LinearForm &form);
  /// The bit width of an integer or index type, or 0 for any other type
  /// and for integers of more than 64 bits.
  unsigned getWidth(mlir::Type type) const;

private:
  LinearForm computeForm(mlir::Value v);
  std::optional<IntRange> computeLeafRange(mlir::Value v);
  std::optional<IntRange> getLeafRange(mlir::Value v);

  unsigned indexWidth;
  unsigned depth = 0;
  llvm::DenseMap<mlir::Value, LinearForm> forms;
  llvm::DenseMap<mlir::Value, std::optional<IntRange>> leafRanges;
};

/// The bit width of `index` in the data layout of `op` (64 without a
/// data layout specification).
unsigned getIndexWidth(mlir::Operation *op);

/// An affine expression as `constant + sum of dims[i] * d_i + sum of
/// symbols[j] * s_j`.
struct LinearExpr {
  int64_t constant = 0;
  llvm::SmallVector<int64_t, 4> dims, symbols;
};

/// The linear form of `expr`, an expression of `numDims` dimensions and
/// `numSymbols` symbols, or nothing when it has a floordiv, ceildiv or mod,
/// a product of two non-constants, or a value that does not fit in int64_t
/// (defined in ExpandShapes.cpp, for lcc-expand-shapes,
/// lcc-distribute-reductions and lcc-affine-to-linalg).
std::optional<LinearExpr> getLinearExpr(mlir::AffineExpr expr,
                                        unsigned numDims,
                                        unsigned numSymbols);

/// Adds the lcc-prepare pipeline to `pm`: lcc-memcpy-intrinsic, mem2reg,
/// sroa, mem2reg, canonicalize, lcc-byval-copy, inline, lcc-hoist-allocas,
/// lcc-split-aggregates, mem2reg, sroa, mem2reg, canonicalize, cse.  It
/// turns the allocas lcc gives every local and parameter into SSA values and
/// inlines calls.  sroa splits allocas that only a following mem2reg
/// promotes; lcc-memcpy-intrinsic and lcc-split-aggregates let mem2reg and
/// sroa see lcc's structure copies and members.  For func.func the inliner neither copies an
/// llvm.byval argument nor moves the allocas of the callee out of the block
/// of the call, often a loop body, where they would take stack in every
/// iteration and mem2reg would not promote them; lcc-byval-copy and
/// lcc-hoist-allocas do that.  The first mem2reg, from before these two
/// passes, promotes the allocas of every function before inlining.
void buildPreparePipeline(mlir::OpPassManager &pm);

/// Adds the lcc-to-scf pipeline to `pm`: lcc-prepare, lift-cf-to-scf,
/// lcc-lift-llvm-func, canonicalize, cse, lcc-rotate-do-while,
/// canonicalize, lcc-uplift-while, canonicalize, loop-invariant-code-motion,
/// canonicalize, cse.  It turns the loops of lcc, in func.func and in
/// llvm.func, into scf.while and, where it can, scf.for.  The
/// canonicalize after lift-cf-to-scf moves the loop body, which
/// lift-cf-to-scf leaves in an scf.if of the `before` region, into the
/// `after` region (WhileMoveIfDown of MLIR 23).
void buildToSCFPipeline(mlir::OpPassManager &pm);

/// Options of the lcc-to-affine pipeline.
struct ToAffineOptions : public mlir::PassPipelineOptions<ToAffineOptions> {
  Option<bool> remarks{
      *this, "remarks",
      llvm::cl::desc("Report each global and loop nest lcc-globals-to-memref "
                     "and lcc-scf-to-affine change or leave, as remarks"),
      llvm::cl::init(false)};
};

/// Adds the lcc-to-affine pipeline to `pm`: lcc-to-scf, lcc-index-loops,
/// lcc-globals-to-memref, canonicalize, cse, lcc-scf-to-affine,
/// canonicalize, cse.  It turns counting loops into index scf.for loops,
/// global arrays into 1-D memref.global with memref.load and memref.store,
/// and loop nests that only compute and access such memrefs at affine
/// indices into affine.for with affine.load and affine.store.
void buildToAffinePipeline(mlir::OpPassManager &pm,
                           const ToAffineOptions &options);

/// Options of the lcc-to-linalg pipeline.
struct ToLinalgOptions : public mlir::PassPipelineOptions<ToLinalgOptions> {
  Option<bool> remarks{
      *this, "remarks",
      llvm::cl::desc("Report each global and loop nest the passes of "
                     "lcc-to-affine, lcc-expand-shapes, "
                     "lcc-distribute-reductions and lcc-affine-to-linalg "
                     "change or leave, as remarks"),
      llvm::cl::init(false)};
  Option<int64_t> maxBufferBytes{
      *this, "max-buffer-bytes",
      llvm::cl::desc("The most bytes of buffers lcc-distribute-reductions "
                     "gives a function"),
      llvm::cl::init(65536)};
};

/// Adds the lcc-to-linalg pipeline to `pm`: lcc-to-affine,
/// lcc-expand-shapes, lcc-distribute-reductions, canonicalize, cse,
/// lcc-affine-to-linalg, linalg-specialize-generic-ops, canonicalize, cse.
/// It gives the 1-D global memrefs of a function the shape of their C
/// arrays, splits loop nests around a reduction into a nest that sets a
/// buffer, a nest that reduces into it and a nest that uses it, and turns
/// the loop nests that only load and store at affine indices of their own
/// loops into linalg.generic, and those that are named operations (matmul,
/// broadcast, copy, ...) into those.
void buildToLinalgPipeline(mlir::OpPassManager &pm,
                           const ToLinalgOptions &options);

/// Adds the lcc-lower pipeline to `pm`: convert-linalg-to-loops,
/// lower-vector-mask, lower-vector-multi-reduction,
/// convert-vector-to-scf{full-unroll}, convert-vector-to-llvm,
/// expand-strided-metadata, lower-affine, convert-scf-to-cf,
/// convert-ub-to-llvm, finalize-memref-to-llvm, convert-arith-to-llvm,
/// convert-cf-to-llvm, convert-func-to-llvm, reconcile-unrealized-casts,
/// lcc-distinct-successors.  It lowers the
/// output of lcc-to-scf, lcc-to-affine and lcc-to-linalg, also after
/// upstream affine transformations such as affine-loop-tile and
/// affine-super-vectorize and after transform dialect scripts that tile
/// and vectorize linalg operations, to the LLVM dialect for mlir-translate:
/// lcc's driver lowers only arith, cf and func, but these leave scf,
/// ub.poison (from lift-cf-to-scf), affine, linalg, memref and vector.
/// convert-vector-to-scf unrolls fully: its buffers would be allocas in
/// the loops of tiled code, which convert-scf-to-cf does not free.
/// lcc-distinct-successors makes the LLVM IR of
/// mlir-translate the same from run to run.  convert-vector-to-llvm comes
/// early: its greedy rewriting of the whole module, on cf, would make
/// switches whose cases pass block arguments to the same block.
void buildLowerPipeline(mlir::OpPassManager &pm);

/// Registers the passes and pipelines of lcc-opt with the global registry.
void registerLccOptPasses();

} // namespace lcc

#endif // LCC_OPT_PASSES_H
