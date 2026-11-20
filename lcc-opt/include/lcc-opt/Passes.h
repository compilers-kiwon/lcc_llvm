//===- Passes.h - Passes and pipelines of lcc-opt ---------------*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LCC_OPT_PASSES_H
#define LCC_OPT_PASSES_H

namespace mlir {
class OpPassManager;
} // namespace mlir

namespace lcc {

/// Adds the lcc-prepare pipeline to `pm`: mem2reg, sroa, canonicalize,
/// inline, mem2reg, sroa, canonicalize, cse.  It turns the allocas lcc gives
/// every local and parameter into SSA values and inlines calls.  The first
/// mem2reg comes before inline: an inlined callee's allocas land in the
/// block of the call, often a loop body, and mem2reg does not promote an
/// llvm.alloca outside the entry block.
void buildPreparePipeline(mlir::OpPassManager &pm);

/// Registers the passes and pipelines of lcc-opt with the global registry.
void registerLccOptPasses();

} // namespace lcc

#endif // LCC_OPT_PASSES_H
