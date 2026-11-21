//===- Passes.h - Passes and pipelines of lcc-opt ---------------*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LCC_OPT_PASSES_H
#define LCC_OPT_PASSES_H

#include "mlir/Pass/Pass.h"

#include <memory>

namespace mlir {
class OpPassManager;
} // namespace mlir

namespace lcc {

#define GEN_PASS_DECL
#include "lcc-opt/Passes.h.inc"

/// Adds the lcc-prepare pipeline to `pm`: mem2reg, sroa, canonicalize,
/// lcc-byval-copy, inline, lcc-hoist-allocas, mem2reg, sroa, canonicalize,
/// cse.  It turns the allocas lcc gives every local and parameter into SSA
/// values and inlines calls.  For func.func the inliner neither copies an
/// llvm.byval argument nor moves the allocas of the callee out of the block
/// of the call, often a loop body, where they would take stack in every
/// iteration and mem2reg would not promote them; lcc-byval-copy and
/// lcc-hoist-allocas do that.  The first mem2reg, from before these two
/// passes, promotes the allocas of every function before inlining.
void buildPreparePipeline(mlir::OpPassManager &pm);

/// Adds the lcc-to-scf pipeline to `pm`: lcc-prepare, lift-cf-to-scf,
/// canonicalize, cse, lcc-rotate-do-while, canonicalize, lcc-uplift-while,
/// canonicalize, loop-invariant-code-motion, canonicalize, cse.  It turns
/// the loops of lcc into scf.while and, where it can, scf.for.  The
/// canonicalize after lift-cf-to-scf moves the loop body, which
/// lift-cf-to-scf leaves in an scf.if of the `before` region, into the
/// `after` region (WhileMoveIfDown of MLIR 23).
void buildToSCFPipeline(mlir::OpPassManager &pm);

/// Registers the passes and pipelines of lcc-opt with the global registry.
void registerLccOptPasses();

} // namespace lcc

#endif // LCC_OPT_PASSES_H
