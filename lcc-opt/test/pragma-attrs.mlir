// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// RUN: lcc-opt --lcc-to-linalg %s | FileCheck %s --check-prefix=LINALG
// RUN: lcc-opt --lcc-to-linalg --lcc-lower %s | FileCheck %s
// RUN: lcc-opt --lcc-globals-to-memref=remarks=true \
// RUN:   --mlir-print-op-on-diagnostic=false %s 2>&1 >/dev/null \
// RUN:   | FileCheck %s --check-prefix=REMARK
//
// lcc -Wf-pragma=knpu gives a function with #pragma knpu the attributes
// knpu.op and knpu.args (README.md of lcc).  They go through lcc-opt as
// they are, but lcc-globals-to-memref leaves the globals whose names are
// in them, as it leaves any global whose name is used other than by
// llvm.mlir.addressof: so the loops on those globals are not raised.  The
// input is what lcc gives for this C, made in the top directory of lcc
// with
//
//   make -r BUILDDIR=$B HOSTFILE=etc/mlir-x86_64-linux.c rcc lcc cpp headers
//   LCCDIR=$B $B/lcc -S -Wf-pragma=knpu layer.c
//
// layer.c:
//
//   signed char act0[4][8], act1[4][8], other[16];
//   static const signed char w[8] = { 1, -1, 2, -2, 3, -3, 4, -4 };
//
//   static void layer1(void)
//   {
//   #pragma knpu add in=act0 w=w out=act1 shape=(4,8) zp=-3 mode="sat"
//   	int p, i;
//
//   	for (p = 0; p < 4; p++)
//   		for (i = 0; i < 8; i++)
//   			act1[p][i] = act0[p][i] + w[i];
//   }
//
//   void clear(void)
//   {
//   	int i;
//
//   	for (i = 0; i < 16; i++)
//   		other[i] = 0;
//   }
//
//   void model(void)
//   {
//   	layer1();
//   	clear();
//   }

// LINALG-LABEL: func.func @layer1()
//  LINALG-SAME:   attributes {knpu.args = {in = @act0, mode = "sat", out = @act1, shape = array<i64: 4, 8>, w = @w, zp = -3 : i64}, knpu.op = "add", llvm.linkage = #llvm.linkage<internal>}
//   LINALG-NOT:   memref.get_global
//       LINALG:   scf.for
//       LINALG:     scf.for
//       LINALG:       llvm.load
//       LINALG:       llvm.store
// LINALG-LABEL: func.func @clear()
//       LINALG:   memref.get_global @other : memref<16xi8>
//       LINALG:   linalg.fill
//       LINALG: memref.global @other : memref<16xi8>
//       LINALG: llvm.mlir.global external @act1()
//       LINALG: llvm.mlir.global external @act0()

// CHECK-LABEL: llvm.func internal @layer1()
//  CHECK-SAME:   attributes {knpu.args = {in = @act0, mode = "sat", out = @act1, shape = array<i64: 4, 8>, w = @w, zp = -3 : i64}, knpu.op = "add"}
// CHECK-LABEL: llvm.func @clear()

// REMARK: remark: lcc-globals-to-memref: @w stays: name used by func.func
// REMARK: remark: lcc-globals-to-memref: @other becomes 'memref<16xi8>'
// REMARK: remark: lcc-globals-to-memref: @act1 stays: name used by func.func
// REMARK: remark: lcc-globals-to-memref: @act0 stays: name used by func.func

module attributes {llvm.data_layout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128", llvm.target_triple = "x86_64-pc-linux-gnu"} {
llvm.mlir.global internal constant @"w"("\01\FF\02\FE\03\FD\04\FC") {alignment = 1 : i64} : !llvm.array<8 x i8>
func.func @"layer1"() attributes {llvm.linkage = #llvm.linkage<internal>, knpu.op = "add", knpu.args = {in = @"act0", w = @"w", out = @"act1", shape = array<i64: 4, 8>, zp = -3 : i64, mode = "sat"}} {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %act1.addr = llvm.mlir.addressof @"act1" : !llvm.ptr
  %c3_i64 = arith.constant 3 : i64
  %act0.addr = llvm.mlir.addressof @"act0" : !llvm.ptr
  %w.addr = llvm.mlir.addressof @"w" : !llvm.ptr
  %c8_i32 = arith.constant 8 : i32
  %c4_i32 = arith.constant 4 : i32
  %i.1 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %p.2 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %c0_i32, %p.2 : i32, !llvm.ptr
  cf.br ^L2
^L2:
  llvm.store %c0_i32, %i.1 : i32, !llvm.ptr
  cf.br ^L6
^L6:
  %t3 = llvm.load %p.2 : !llvm.ptr -> i32
  %t4 = arith.extsi %t3 : i32 to i64
  %t5 = arith.shli %t4, %c3_i64 : i64
  %t2 = llvm.getelementptr %act1.addr[%t5] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t6 = llvm.load %i.1 : !llvm.ptr -> i32
  %t7 = arith.extsi %t6 : i32 to i64
  %t1 = llvm.getelementptr %t2[%t7] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t9 = llvm.getelementptr %act0.addr[%t5] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t8 = llvm.getelementptr %t9[%t7] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t10 = llvm.load %t8 : !llvm.ptr -> i8
  %t11 = arith.extsi %t10 : i8 to i32
  %t12 = llvm.getelementptr %w.addr[%t7] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t13 = llvm.load %t12 : !llvm.ptr -> i8
  %t14 = arith.extsi %t13 : i8 to i32
  %t15 = arith.addi %t11, %t14 : i32
  %t16 = arith.trunci %t15 : i32 to i8
  llvm.store %t16, %t1 : i8, !llvm.ptr
  cf.br ^L7
^L7:
  %t17 = llvm.load %i.1 : !llvm.ptr -> i32
  %t18 = arith.addi %t17, %c1_i32 : i32
  llvm.store %t18, %i.1 : i32, !llvm.ptr
  %t19 = llvm.load %i.1 : !llvm.ptr -> i32
  %t20 = arith.cmpi slt, %t19, %c8_i32 : i32
  cf.cond_br %t20, ^L6, ^b1
^b1:
  cf.br ^L3
^L3:
  %t21 = llvm.load %p.2 : !llvm.ptr -> i32
  %t22 = arith.addi %t21, %c1_i32 : i32
  llvm.store %t22, %p.2 : i32, !llvm.ptr
  %t23 = llvm.load %p.2 : !llvm.ptr -> i32
  %t24 = arith.cmpi slt, %t23, %c4_i32 : i32
  cf.cond_br %t24, ^L2, ^b2
^b2:
  cf.br ^L1
^L1:
  func.return
}
func.func @"clear"() {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %other.addr = llvm.mlir.addressof @"other" : !llvm.ptr
  %c0_i8 = arith.constant 0 : i8
  %c16_i32 = arith.constant 16 : i32
  %i.3 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %c0_i32, %i.3 : i32, !llvm.ptr
  cf.br ^L11
^L11:
  %t2 = llvm.load %i.3 : !llvm.ptr -> i32
  %t3 = arith.extsi %t2 : i32 to i64
  %t1 = llvm.getelementptr %other.addr[%t3] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  llvm.store %c0_i8, %t1 : i8, !llvm.ptr
  cf.br ^L12
^L12:
  %t4 = llvm.load %i.3 : !llvm.ptr -> i32
  %t5 = arith.addi %t4, %c1_i32 : i32
  llvm.store %t5, %i.3 : i32, !llvm.ptr
  %t6 = llvm.load %i.3 : !llvm.ptr -> i32
  %t7 = arith.cmpi slt, %t6, %c16_i32 : i32
  cf.cond_br %t7, ^L11, ^b1
^b1:
  cf.br ^L10
^L10:
  func.return
}
func.func @"model"() {
  func.call @"layer1"() : () -> ()
  func.call @"clear"() : () -> ()
  cf.br ^L15
^L15:
  func.return
}
llvm.mlir.global external @"other"() {alignment = 16 : i64} : !llvm.array<16 x i8> {
  %v1 = llvm.mlir.zero : !llvm.array<16 x i8>
  llvm.return %v1 : !llvm.array<16 x i8>
}
llvm.mlir.global external @"act1"() {alignment = 16 : i64} : !llvm.array<32 x i8> {
  %v1 = llvm.mlir.zero : !llvm.array<32 x i8>
  llvm.return %v1 : !llvm.array<32 x i8>
}
llvm.mlir.global external @"act0"() {alignment = 16 : i64} : !llvm.array<32 x i8> {
  %v1 = llvm.mlir.zero : !llvm.array<32 x i8>
  llvm.return %v1 : !llvm.array<32 x i8>
}
}
