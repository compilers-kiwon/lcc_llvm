//===- Pipelines.cpp - Pass pipelines of lcc-opt --------------------------===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lcc-opt/Passes.h"

#include "mlir/Conversion/ControlFlowToSCF/ControlFlowToSCF.h"
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
  pm.addPass(createInlinerPass());
  pm.addPass(createMem2Reg());
  pm.addPass(createSROA());
  pm.addPass(createCanonicalizerPass());
  pm.addPass(createCSEPass());
}

void lcc::buildToSCFPipeline(OpPassManager &pm) {
  buildPreparePipeline(pm);
  pm.addPass(createLiftControlFlowToSCFPass());
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

void lcc::registerLccOptPasses() {
  registerLccPasses();
  PassPipelineRegistration<>(
      "lcc-prepare",
      "Promote lcc's allocas to SSA values and inline calls (mem2reg, sroa, "
      "canonicalize, inline, mem2reg, sroa, canonicalize, cse)",
      buildPreparePipeline);
  PassPipelineRegistration<>(
      "lcc-to-scf",
      "Lift lcc's loops to scf.while and scf.for (lcc-prepare, "
      "lift-cf-to-scf, canonicalize, cse, lcc-rotate-do-while, canonicalize, "
      "lcc-uplift-while, canonicalize, loop-invariant-code-motion, "
      "canonicalize, cse)",
      buildToSCFPipeline);
}
