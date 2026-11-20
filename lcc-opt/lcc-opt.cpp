//===- lcc-opt.cpp - mlir-opt for the output of lcc -----------------------===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The upstream dialects, extensions and passes of MLIR 23, as in mlir-opt,
// and the passes and pipelines of lcc-opt.
//
//===----------------------------------------------------------------------===//

#include "lcc-opt/Passes.h"

#include "mlir/IR/DialectRegistry.h"
#include "mlir/InitAllDialects.h"
#include "mlir/InitAllExtensions.h"
#include "mlir/InitAllPasses.h"
#include "mlir/Tools/mlir-opt/MlirOptMain.h"

int main(int argc, char **argv) {
  mlir::DialectRegistry registry;
  mlir::registerAllDialects(registry);
  mlir::registerAllExtensions(registry);
  mlir::registerAllPasses();
  lcc::registerLccOptPasses();
  return mlir::asMainReturnCode(mlir::MlirOptMain(
      argc, argv, "lcc-opt: MLIR optimizer driver for lcc\n", registry));
}
