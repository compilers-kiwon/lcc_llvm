//===- MemcpyIntrinsic.cpp - lcc's memcpy as llvm.intr.memcpy -------------===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// lcc-memcpy-intrinsic: see Passes.td.  lcc copies structures with
//
//   llvm.call_intrinsic "llvm.memcpy.p0.p0.i64"(%dst, %src, %len, %volatile)
//     : (!llvm.ptr {llvm.align = A : i64}, !llvm.ptr {llvm.align = A : i64},
//        i64, i1) -> ()
//
// because the llvm.intr.memcpy of MLIR 21 took no alignments.  mem2reg and
// sroa know llvm.intr.memcpy, not a call_intrinsic, whose pointers they take
// for escaping; in MLIR 23 llvm.intr.memcpy keeps the alignments in its
// arg_attrs.
//
//===----------------------------------------------------------------------===//

#include "lcc-opt/Passes.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Matchers.h"

namespace lcc {
#define GEN_PASS_DEF_LCCMEMCPYINTRINSIC
#include "lcc-opt/Passes.h.inc"
} // namespace lcc

using namespace mlir;

namespace {

struct MemcpyIntrinsicPass
    : public lcc::impl::LccMemcpyIntrinsicBase<MemcpyIntrinsicPass> {
  void runOnOperation() override {
    SmallVector<LLVM::CallIntrinsicOp> calls;
    getOperation()->walk([&](LLVM::CallIntrinsicOp call) {
      // llvm.memcpy.p0.p0.i64 and .i32: (ptr, ptr, iN, i1) -> ().
      if (call.getIntrin().starts_with("llvm.memcpy.p0.p0.i") &&
          call.getArgs().size() == 4 && call->getNumResults() == 0 &&
          call.getOpBundleOperands().empty() &&
          matchPattern(call.getArgs()[3], m_Constant()))
        calls.push_back(call);
    });
    if (calls.empty())
      return markAllAnalysesPreserved();
    for (LLVM::CallIntrinsicOp call : calls) {
      OpBuilder builder(call);
      APInt isVolatile;
      if (!matchPattern(call.getArgs()[3], m_ConstantInt(&isVolatile)))
        continue;
      // The attributes of dst, src and len.
      ArrayAttr argAttrs;
      if (ArrayAttr attrs = call.getArgAttrsAttr())
        argAttrs = builder.getArrayAttr(attrs.getValue().take_front(3));
      LLVM::MemcpyOp::create(builder, call.getLoc(), call.getArgs()[0],
                             call.getArgs()[1], call.getArgs()[2],
                             builder.getBoolAttr(!isVolatile.isZero()),
                             /*access_groups=*/nullptr,
                             /*alias_scopes=*/nullptr,
                             /*noalias_scopes=*/nullptr, /*tbaa=*/nullptr,
                             argAttrs, /*res_attrs=*/nullptr);
      call.erase();
    }
  }
};

} // namespace
