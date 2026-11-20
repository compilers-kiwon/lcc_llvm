//===- Pipelines.cpp - Pass pipelines of lcc-opt --------------------------===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lcc-opt/Passes.h"

#include "mlir/Pass/PassManager.h"
#include "mlir/Pass/PassRegistry.h"
#include "mlir/Transforms/Passes.h"

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

void lcc::registerLccOptPasses() {
  PassPipelineRegistration<>(
      "lcc-prepare",
      "Promote lcc's allocas to SSA values and inline calls (mem2reg, sroa, "
      "canonicalize, inline, mem2reg, sroa, canonicalize, cse)",
      buildPreparePipeline);
}
