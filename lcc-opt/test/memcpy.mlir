// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// RUN: lcc-opt --lcc-memcpy-intrinsic %s | FileCheck %s
//
// lcc-memcpy-intrinsic turns lcc's llvm.call_intrinsic of llvm.memcpy into
// llvm.intr.memcpy, keeping the operands, the volatility and the
// alignments, which mem2reg and sroa then know.

// CHECK-LABEL: func.func @copies(
//  CHECK-SAME:     %[[D:.*]]: !llvm.ptr, %[[S:.*]]: !llvm.ptr, %[[V:.*]]: i1)
//   CHECK-NOT:   llvm.call_intrinsic "llvm.memcpy
//       CHECK:   "llvm.intr.memcpy"(%[[D]], %[[S]], %{{.*}}) <{arg_attrs = [{llvm.align = 8 : i64}, {llvm.align = 4 : i64}, {}], isVolatile = false}> : (!llvm.ptr, !llvm.ptr, i64) -> ()
//       CHECK:   "llvm.intr.memcpy"(%[[D]], %[[S]], %{{.*}}) <{arg_attrs = [{llvm.align = 4 : i64}, {llvm.align = 4 : i64}, {}], isVolatile = true}> : (!llvm.ptr, !llvm.ptr, i64) -> ()
//       CHECK:   "llvm.intr.memcpy"(%[[D]], %[[S]], %{{.*}}) <{isVolatile = false}> : (!llvm.ptr, !llvm.ptr, i32) -> ()
// The volatility is not a constant: left alone.
//       CHECK:   llvm.call_intrinsic "llvm.memcpy.p0.p0.i64"(%[[D]], %[[S]], %{{.*}}, %[[V]])
// Not a memcpy: left alone.
//       CHECK:   llvm.call_intrinsic "llvm.memmove.p0.p0.i64"
func.func @copies(%d: !llvm.ptr, %s: !llvm.ptr, %v: i1) {
  %n = arith.constant 16 : i64
  %n32 = arith.constant 16 : i32
  %false = arith.constant false
  %true = arith.constant true
  llvm.call_intrinsic "llvm.memcpy.p0.p0.i64"(%d, %s, %n, %false) : (!llvm.ptr {llvm.align = 8 : i64}, !llvm.ptr {llvm.align = 4 : i64}, i64, i1) -> ()
  llvm.call_intrinsic "llvm.memcpy.p0.p0.i64"(%d, %s, %n, %true) : (!llvm.ptr {llvm.align = 4 : i64}, !llvm.ptr {llvm.align = 4 : i64}, i64, i1) -> ()
  llvm.call_intrinsic "llvm.memcpy.p0.p0.i32"(%d, %s, %n32, %false) : (!llvm.ptr, !llvm.ptr, i32, i1) -> ()
  llvm.call_intrinsic "llvm.memcpy.p0.p0.i64"(%d, %s, %n, %v) : (!llvm.ptr, !llvm.ptr, i64, i1) -> ()
  llvm.call_intrinsic "llvm.memmove.p0.p0.i64"(%d, %s, %n, %false) : (!llvm.ptr, !llvm.ptr, i64, i1) -> ()
  return
}
