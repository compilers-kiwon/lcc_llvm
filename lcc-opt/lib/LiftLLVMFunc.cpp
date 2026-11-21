//===- LiftLLVMFunc.cpp - lift-cf-to-scf for llvm.func --------------------===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// lcc-lift-llvm-func: see Passes.td.  It runs the transformation of the
// upstream lift-cf-to-scf, ControlFlowToSCFTransformation, on the body of
// every llvm.func, which lift-cf-to-scf leaves alone: it walks only
// func.func, and its terminator for statically infinite loops is a
// func.return.  The terminator here is an llvm.return, of a poison value
// unless the function returns void, so that the function keeps one kind of
// exit.
//
//===----------------------------------------------------------------------===//

#include "lcc-opt/Passes.h"

#include "mlir/Conversion/ControlFlowToSCF/ControlFlowToSCF.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/IR/Dominance.h"
#include "mlir/Transforms/CFGToSCF.h"

namespace lcc {
#define GEN_PASS_DEF_LCCLIFTLLVMFUNC
#include "lcc-opt/Passes.h.inc"
} // namespace lcc

using namespace mlir;

namespace {

/// ControlFlowToSCFTransformation with the unreachable terminator of an
/// llvm.func.
struct LLVMFuncTransformation : public ControlFlowToSCFTransformation {
  FailureOr<Operation *> createUnreachableTerminator(Location loc,
                                                     OpBuilder &builder,
                                                     Region &region) override {
    auto func = dyn_cast<LLVM::LLVMFuncOp>(region.getParentOp());
    if (!func)
      return emitError(loc, "cannot create an unreachable terminator for '")
             << region.getParentOp()->getName() << "'";
    Type result = func.getFunctionType().getReturnType();
    if (isa<LLVM::LLVMVoidType>(result))
      return LLVM::ReturnOp::create(builder, loc, ValueRange{}).getOperation();
    return LLVM::ReturnOp::create(builder, loc,
                                  getUndefValue(loc, builder, result))
        .getOperation();
  }
};

/// Returns true when every branch in `body` is one of cf, which
/// ControlFlowToSCFTransformation knows.
static bool hasOnlyCFBranches(Region &body) {
  for (Block &block : body)
    if (block.getNumSuccessors() != 0 &&
        !isa<cf::BranchOp, cf::CondBranchOp, cf::SwitchOp>(
            block.getTerminator()))
      return false;
  return true;
}

struct LiftLLVMFuncPass
    : public lcc::impl::LccLiftLLVMFuncBase<LiftLLVMFuncPass> {
  void runOnOperation() override {
    LLVMFuncTransformation transformation;
    Operation *op = getOperation();
    bool changed = false;
    WalkResult result = op->walk([&](LLVM::LLVMFuncOp func) {
      Region &body = func.getBody();
      if (body.empty() || !hasOnlyCFBranches(body))
        return WalkResult::advance();
      DominanceInfo &dominance = func != op
                                     ? getChildAnalysis<DominanceInfo>(func)
                                     : getAnalysis<DominanceInfo>();
      FailureOr<bool> lifted =
          transformCFGToSCF(body, transformation, dominance);
      if (failed(lifted))
        return WalkResult::interrupt();
      changed |= *lifted;
      return WalkResult::advance();
    });
    if (result.wasInterrupted())
      return signalPassFailure();
    if (!changed)
      markAllAnalysesPreserved();
  }
};

} // namespace
