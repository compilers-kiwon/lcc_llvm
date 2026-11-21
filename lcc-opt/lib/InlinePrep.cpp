//===- InlinePrep.cpp - What inlining func.func needs for lcc -------------===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// lcc-byval-copy and lcc-hoist-allocas: see Passes.td.  The inliner leaves
// the handling of an argument to the dialect of the callee and the handling
// of the inlined blocks to the dialect of the call.  The LLVM dialect copies
// an llvm.byval argument and moves the static allocas of the callee into the
// entry block of the caller, but only for llvm.func and llvm.call; lcc gives
// most functions as func.func, called by func.call, and the func dialect
// does neither.  These two passes, around the inliner, do it instead.
//
//===----------------------------------------------------------------------===//

#include "lcc-opt/Passes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Matchers.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "mlir/Interfaces/FunctionInterfaces.h"

#include <algorithm>

namespace lcc {
#define GEN_PASS_DEF_LCCBYVALCOPY
#define GEN_PASS_DEF_LCCHOISTALLOCAS
#include "lcc-opt/Passes.h.inc"
} // namespace lcc

using namespace mlir;

/// Returns true when the only use of `arg` is a memcpy from it into an
/// alloca of the entry block, which this pass would have made.
static bool isCopied(BlockArgument arg) {
  if (!arg.hasOneUse())
    return false;
  auto memcpy = dyn_cast<LLVM::MemcpyOp>(*arg.getUsers().begin());
  if (!memcpy || memcpy.getSrc() != arg)
    return false;
  auto alloca = memcpy.getDst().getDefiningOp<LLVM::AllocaOp>();
  return alloca && alloca->getBlock() == arg.getOwner();
}

namespace {

struct ByvalCopyPass : public lcc::impl::LccByvalCopyBase<ByvalCopyPass> {
  void runOnOperation() override {
    bool changed = false;
    getOperation()->walk([&](func::FuncOp func) {
      if (func.isExternal())
        return;
      Block &entry = func.getBody().front();
      DataLayout layout = DataLayout::closest(func);
      OpBuilder builder(&entry, entry.begin());
      for (BlockArgument arg : entry.getArguments()) {
        unsigned n = arg.getArgNumber();
        auto byval = func.getArgAttrOfType<TypeAttr>(
            n, LLVM::LLVMDialect::getByValAttrName());
        if (!byval || arg.use_empty() || isCopied(arg))
          continue;
        // Like the LLVM dialect: the larger of the ABI alignment of the type
        // and the llvm.align of the argument.
        Type type = byval.getValue();
        uint64_t align = layout.getTypeABIAlignment(type);
        if (auto attr = func.getArgAttrOfType<IntegerAttr>(
                n, LLVM::LLVMDialect::getAlignAttrName()))
          align = std::max<uint64_t>(align, attr.getValue().getZExtValue());
        Location loc = arg.getLoc();
        Value one = LLVM::ConstantOp::create(builder, loc, builder.getI64Type(),
                                             builder.getI64IntegerAttr(1));
        Value copy = LLVM::AllocaOp::create(builder, loc, arg.getType(), type,
                                            one, align);
        Value size = LLVM::ConstantOp::create(
            builder, loc, builder.getI64Type(),
            builder.getI64IntegerAttr(layout.getTypeSize(type)));
        auto memcpy = LLVM::MemcpyOp::create(builder, loc, copy, arg, size,
                                             /*isVolatile=*/false);
        arg.replaceAllUsesExcept(copy, memcpy);
        changed = true;
      }
    });
    if (!changed)
      markAllAnalysesPreserved();
  }
};

struct HoistAllocasPass
    : public lcc::impl::LccHoistAllocasBase<HoistAllocasPass> {
  void runOnOperation() override {
    bool changed = false;
    getOperation()->walk([&](FunctionOpInterface func) {
      Region &body = func.getFunctionBody();
      if (body.empty())
        return;
      // Only blocks of the function body: an alloca in a nested region may
      // belong to another allocation scope.
      SmallVector<LLVM::AllocaOp> allocas;
      for (Block &block : llvm::drop_begin(body))
        for (auto alloca : block.getOps<LLVM::AllocaOp>())
          allocas.push_back(alloca);
      Block &entry = body.front();
      OpBuilder builder(&entry, entry.begin());
      for (LLVM::AllocaOp alloca : allocas) {
        IntegerAttr count;
        if (!matchPattern(alloca.getArraySize(), m_Constant(&count)))
          continue;
        Value size = LLVM::ConstantOp::create(
            builder, alloca.getLoc(), alloca.getArraySize().getType(), count);
        alloca->moveAfter(size.getDefiningOp());
        alloca.getArraySizeMutable().assign(size);
        builder.setInsertionPointAfter(alloca);
        changed = true;
      }
    });
    if (!changed)
      markAllAnalysesPreserved();
  }
};

} // namespace
