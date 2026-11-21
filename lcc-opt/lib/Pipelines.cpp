//===- Pipelines.cpp - Pass pipelines of lcc-opt --------------------------===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lcc-opt/Passes.h"

#include "mlir/Conversion/ArithToLLVM/ArithToLLVM.h"
#include "mlir/Conversion/ControlFlowToLLVM/ControlFlowToLLVM.h"
#include "mlir/Conversion/ControlFlowToSCF/ControlFlowToSCF.h"
#include "mlir/Conversion/FuncToLLVM/ConvertFuncToLLVMPass.h"
#include "mlir/Conversion/ReconcileUnrealizedCasts/ReconcileUnrealizedCasts.h"
#include "mlir/Conversion/SCFToControlFlow/SCFToControlFlow.h"
#include "mlir/Conversion/UBToLLVM/UBToLLVM.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Pass/PassRegistry.h"
#include "mlir/Transforms/Passes.h"

namespace lcc {
#define GEN_PASS_REGISTRATION
#include "lcc-opt/Passes.h.inc"
} // namespace lcc

using namespace mlir;

void lcc::buildPreparePipeline(OpPassManager &pm) {
  pm.addPass(createMem2Reg());
  pm.addPass(createSROA());
  pm.addPass(createCanonicalizerPass());
  pm.addPass(createLccByvalCopy());
  pm.addPass(createInlinerPass());
  pm.addPass(createLccHoistAllocas());
  pm.addPass(createMem2Reg());
  pm.addPass(createSROA());
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

void lcc::buildLowerPipeline(OpPassManager &pm) {
  pm.addPass(createSCFToControlFlowPass());
  pm.addPass(createUBToLLVMConversionPass());
  pm.addPass(createArithToLLVMConversionPass());
  pm.addPass(createConvertControlFlowToLLVMPass());
  pm.addPass(createConvertFuncToLLVMPass());
  pm.addPass(createReconcileUnrealizedCastsPass());
}

void lcc::registerLccOptPasses() {
  registerLccPasses();
  PassPipelineRegistration<>(
      "lcc-prepare",
      "Promote lcc's allocas to SSA values and inline calls (mem2reg, sroa, "
      "canonicalize, lcc-byval-copy, inline, lcc-hoist-allocas, mem2reg, "
      "sroa, canonicalize, cse)",
      buildPreparePipeline);
  PassPipelineRegistration<>(
      "lcc-to-scf",
      "Lift lcc's loops to scf.while and scf.for (lcc-prepare, "
      "lift-cf-to-scf, lcc-lift-llvm-func, canonicalize, cse, "
      "lcc-rotate-do-while, canonicalize, "
      "lcc-uplift-while, canonicalize, loop-invariant-code-motion, "
      "canonicalize, cse)",
      buildToSCFPipeline);
  PassPipelineRegistration<>(
      "lcc-lower",
      "Lower the output of lcc-to-scf to the LLVM dialect for mlir-translate "
      "(convert-scf-to-cf, convert-ub-to-llvm, convert-arith-to-llvm, "
      "convert-cf-to-llvm, convert-func-to-llvm, reconcile-unrealized-casts)",
      buildLowerPipeline);
}
