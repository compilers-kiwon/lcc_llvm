//===- Pipelines.cpp - Pass pipelines of lcc-opt --------------------------===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lcc-opt/Passes.h"

#include "mlir/Conversion/AffineToStandard/AffineToStandard.h"
#include "mlir/Conversion/ArithToLLVM/ArithToLLVM.h"
#include "mlir/Conversion/ControlFlowToLLVM/ControlFlowToLLVM.h"
#include "mlir/Conversion/ControlFlowToSCF/ControlFlowToSCF.h"
#include "mlir/Conversion/FuncToLLVM/ConvertFuncToLLVMPass.h"
#include "mlir/Conversion/MemRefToLLVM/MemRefToLLVM.h"
#include "mlir/Conversion/ReconcileUnrealizedCasts/ReconcileUnrealizedCasts.h"
#include "mlir/Conversion/SCFToControlFlow/SCFToControlFlow.h"
#include "mlir/Conversion/UBToLLVM/UBToLLVM.h"
#include "mlir/Conversion/VectorToLLVM/ConvertVectorToLLVMPass.h"
#include "mlir/Conversion/VectorToSCF/VectorToSCF.h"
#include "mlir/Dialect/MemRef/Transforms/Passes.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Pass/PassRegistry.h"
#include "mlir/Transforms/Passes.h"

namespace lcc {
#define GEN_PASS_REGISTRATION
#include "lcc-opt/Passes.h.inc"
} // namespace lcc

using namespace mlir;

void lcc::buildPreparePipeline(OpPassManager &pm) {
  pm.addPass(createLccMemcpyIntrinsic());
  pm.addPass(createMem2Reg());
  pm.addPass(createSROA());
  pm.addPass(createMem2Reg());
  pm.addPass(createCanonicalizerPass());
  pm.addPass(createLccByvalCopy());
  pm.addPass(createInlinerPass());
  pm.addPass(createLccHoistAllocas());
  pm.addPass(createLccSplitAggregates());
  pm.addPass(createMem2Reg());
  pm.addPass(createSROA());
  pm.addPass(createMem2Reg());
  pm.addPass(createCanonicalizerPass());
  pm.addPass(createCSEPass());
}

void lcc::buildToSCFPipeline(OpPassManager &pm) {
  buildPreparePipeline(pm);
  pm.addPass(createLiftControlFlowToSCFPass());
  pm.addPass(createLccLiftLLVMFunc());
  pm.addPass(createCanonicalizerPass());
  pm.addPass(createCSEPass());
  pm.addPass(createLccRotateDoWhile());
  pm.addPass(createCanonicalizerPass());
  pm.addPass(createLccUpliftWhile());
  pm.addPass(createCanonicalizerPass());
  pm.addPass(createLoopInvariantCodeMotionPass());
  pm.addPass(createCanonicalizerPass());
  pm.addPass(createCSEPass());
}

void lcc::buildToAffinePipeline(OpPassManager &pm,
                                const ToAffineOptions &options) {
  buildToSCFPipeline(pm);
  pm.addPass(createLccIndexLoops());
  LccGlobalsToMemrefOptions globals;
  globals.remarks = options.remarks;
  pm.addPass(createLccGlobalsToMemref(globals));
  pm.addPass(createCanonicalizerPass());
  pm.addPass(createCSEPass());
  LccScfToAffineOptions affine;
  affine.remarks = options.remarks;
  pm.addPass(createLccScfToAffine(affine));
  pm.addPass(createCanonicalizerPass());
  pm.addPass(createCSEPass());
}

void lcc::buildLowerPipeline(OpPassManager &pm) {
  pm.addPass(createConvertVectorToSCFPass());
  // Early: convert-vector-to-llvm first rewrites the whole module greedily.
  // After convert-scf-to-cf that would fold blocks so that the cases of a
  // switch pass block arguments to the same block, which needs the blocks
  // of lcc-distinct-successors; before, the LLVM IR of code without vectors
  // is that of lcc-lower without these passes.
  pm.addPass(createConvertVectorToLLVMPass());
  pm.addPass(memref::createExpandStridedMetadataPass());
  pm.addPass(createLowerAffinePass());
  pm.addPass(createSCFToControlFlowPass());
  pm.addPass(createUBToLLVMConversionPass());
  pm.addPass(createFinalizeMemRefToLLVMConversionPass());
  pm.addPass(createArithToLLVMConversionPass());
  pm.addPass(createConvertControlFlowToLLVMPass());
  pm.addPass(createConvertFuncToLLVMPass());
  pm.addPass(createReconcileUnrealizedCastsPass());
  pm.addPass(createLccDistinctSuccessors());
}

void lcc::registerLccOptPasses() {
  registerLccPasses();
  PassPipelineRegistration<>(
      "lcc-prepare",
      "Promote lcc's allocas to SSA values and inline calls "
      "(lcc-memcpy-intrinsic, mem2reg, sroa, mem2reg, canonicalize, "
      "lcc-byval-copy, inline, lcc-hoist-allocas, lcc-split-aggregates, "
      "mem2reg, sroa, mem2reg, canonicalize, cse)",
      buildPreparePipeline);
  PassPipelineRegistration<>(
      "lcc-to-scf",
      "Lift lcc's loops to scf.while and scf.for (lcc-prepare, "
      "lift-cf-to-scf, lcc-lift-llvm-func, canonicalize, cse, "
      "lcc-rotate-do-while, canonicalize, "
      "lcc-uplift-while, canonicalize, loop-invariant-code-motion, "
      "canonicalize, cse)",
      buildToSCFPipeline);
  PassPipelineRegistration<ToAffineOptions>(
      "lcc-to-affine",
      "Raise lcc's loop nests on global arrays to affine (lcc-to-scf, "
      "lcc-index-loops, lcc-globals-to-memref, canonicalize, cse, "
      "lcc-scf-to-affine, canonicalize, cse)",
      buildToAffinePipeline);
  PassPipelineRegistration<>(
      "lcc-lower",
      "Lower the output of lcc-to-scf or lcc-to-affine to the LLVM dialect "
      "for mlir-translate (convert-vector-to-scf, convert-vector-to-llvm, "
      "expand-strided-metadata, lower-affine, convert-scf-to-cf, "
      "convert-ub-to-llvm, finalize-memref-to-llvm, convert-arith-to-llvm, "
      "convert-cf-to-llvm, convert-func-to-llvm, "
      "reconcile-unrealized-casts, lcc-distinct-successors)",
      buildLowerPipeline);
}
