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
/// inline, mem2reg, sroa, canonicalize, cse.  It turns the allocas lcc gives
/// every local and parameter into SSA values and inlines calls.  The first
/// mem2reg comes before inline: an inlined callee's allocas land in the
/// block of the call, often a loop body, and mem2reg does not promote an
/// llvm.alloca outside the entry block.
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
