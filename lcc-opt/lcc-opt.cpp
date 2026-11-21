//===- lcc-opt.cpp - mlir-opt for the output of lcc -----------------------===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The upstream dialects, extensions and passes of MLIR 23, as in mlir-opt,
// and the passes and pipelines of lcc-opt.  Unlike mlir-opt, when it writes
// text it runs lcc-check-text last, which fails if the text would read back
// as other values (see CheckText.cpp); --lcc-allow-lossy-text makes that a
// warning.  main does what MlirOptMain(argc, argv, toolName, registry) does,
// with that pass added to the pipeline of the command line.
//
//===----------------------------------------------------------------------===//

#include "lcc-opt/Passes.h"

#include "mlir/IR/DialectRegistry.h"
#include "mlir/InitAllDialects.h"
#include "mlir/InitAllExtensions.h"
#include "mlir/InitAllPasses.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Support/FileUtilities.h"
#include "mlir/Tools/mlir-opt/MlirOptMain.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/ToolOutputFile.h"

#include <cstdio>

static llvm::cl::opt<bool> allowLossyText(
    "lcc-allow-lossy-text",
    llvm::cl::desc("Write text even where it would read back as other values "
                   "(lcc-check-text warns instead of failing)"),
    llvm::cl::init(false));

int main(int argc, char **argv) {
  mlir::DialectRegistry registry;
  mlir::registerAllDialects(registry);
  mlir::registerAllExtensions(registry);
  mlir::registerAllPasses();
  lcc::registerLccOptPasses();

  auto [inputFilename, outputFilename] = mlir::registerAndParseCLIOptions(
      argc, argv, "lcc-opt: MLIR optimizer driver for lcc\n", registry);
  mlir::MlirOptMainConfig config = mlir::MlirOptMainConfig::createFromCLOptions();
  if (config.shouldShowDialects() || config.shouldListPasses())
    return mlir::asMainReturnCode(mlir::MlirOptMain(
        argc, argv, inputFilename, outputFilename, registry));

  llvm::InitLLVM y(argc, argv);
  if (!config.shouldEmitBytecode()) {
    mlir::MlirOptMainConfig commandLine = config;
    config.setPassPipelineSetupFn([commandLine](mlir::PassManager &pm) {
      if (mlir::failed(commandLine.setupPassPipeline(pm)))
        return mlir::failure();
      lcc::LccCheckTextOptions options;
      options.allowLossy = allowLossyText;
      pm.addPass(lcc::createLccCheckText(options));
      return mlir::success();
    });
  }

  // When reading from stdin and the input is a tty, it is often a user
  // mistake and the process "appears to be stuck"; say so, like mlir-opt.
  if (inputFilename == "-" &&
      llvm::sys::Process::FileDescriptorIsDisplayed(fileno(stdin)))
    llvm::errs() << "(processing input from stdin now, hit ctrl-c/ctrl-d to "
                    "interrupt)\n";
  std::string errorMessage;
  auto file = mlir::openInputFile(inputFilename, &errorMessage);
  if (!file) {
    llvm::errs() << errorMessage << "\n";
    return 1;
  }
  auto output = mlir::openOutputFile(outputFilename, &errorMessage);
  if (!output) {
    llvm::errs() << errorMessage << "\n";
    return 1;
  }
  if (mlir::failed(
          mlir::MlirOptMain(output->os(), std::move(file), registry, config)))
    return 1;
  // Keep the output file only when MlirOptMain succeeded.
  output->keep();
  return 0;
}
