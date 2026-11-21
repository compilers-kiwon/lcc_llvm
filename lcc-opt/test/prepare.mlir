// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// RUN: lcc-opt --lcc-prepare %s | FileCheck %s --implicit-check-not=llvm.alloca
//
// lcc-prepare turns every llvm.alloca into SSA values and inlines the call
// of the static function.  The input is what lcc 568f806 (mlir/x86_64-linux)
// gives for prepare.c,
//
//   static int sq(int x)
//   {
//   	return x * x;
//   }
//
//   int sumsq(int n)
//   {
//   	int i, s;
//
//   	s = 0;
//   	for (i = 0; i < n; i++)
//   		s += sq(i);
//   	return s;
//   }
//
// made in the top directory of lcc with
//
//   make -r BUILDDIR=$B HOSTFILE=etc/mlir-x86_64-linux.c rcc lcc cpp headers
//   LCCDIR=$B $B/lcc -S prepare.c
//
// The inlined sq lands in the loop body of sumsq; its allocas, promoted by
// the first mem2reg or moved to the entry block by lcc-hoist-allocas, are
// gone.  inline.mlir has allocas that stay.

// CHECK-LABEL: func.func @sq(
// CHECK:         arith.muli
// CHECK-LABEL: func.func @sumsq(
// CHECK-NOT:     call
// CHECK:         arith.muli %[[I:.*]], %[[I]] : i32
// CHECK-NOT:     call
// CHECK:         return

module attributes {llvm.data_layout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128", llvm.target_triple = "x86_64-pc-linux-gnu"} {
func.func @"sq"(%a0: i32) -> i32 attributes {llvm.linkage = #llvm.linkage<internal>} {
  %c1_i32 = arith.constant 1 : i32
  %retval = llvm.alloca %c1_i32 x i32 : (i32) -> !llvm.ptr
  %x.1 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %a0, %x.1 : i32, !llvm.ptr
  %t1 = llvm.load %x.1 : !llvm.ptr -> i32
  %t2 = arith.muli %t1, %t1 : i32
  llvm.store %t2, %retval : i32, !llvm.ptr
  cf.br ^L1
^L1:
  %t3 = llvm.load %retval : !llvm.ptr -> i32
  func.return %t3 : i32
}
func.func @"sumsq"(%a0: i32) -> i32 {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %retval = llvm.alloca %c1_i32 x i32 : (i32) -> !llvm.ptr
  %n.2 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %a0, %n.2 : i32, !llvm.ptr
  %n.3 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %i.4 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %s.5 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %t1 = llvm.load %n.2 : !llvm.ptr -> i32
  llvm.store %t1, %n.3 : i32, !llvm.ptr
  llvm.store %c0_i32, %s.5 : i32, !llvm.ptr
  llvm.store %c0_i32, %i.4 : i32, !llvm.ptr
  cf.br ^L6
^L3:
  %t2 = llvm.load %i.4 : !llvm.ptr -> i32
  %t3 = func.call @"sq"(%t2) : (i32) -> i32
  %t4 = llvm.load %s.5 : !llvm.ptr -> i32
  %t5 = arith.addi %t4, %t3 : i32
  llvm.store %t5, %s.5 : i32, !llvm.ptr
  cf.br ^L4
^L4:
  %t6 = llvm.load %i.4 : !llvm.ptr -> i32
  %t7 = arith.addi %t6, %c1_i32 : i32
  llvm.store %t7, %i.4 : i32, !llvm.ptr
  cf.br ^L6
^L6:
  %t8 = llvm.load %i.4 : !llvm.ptr -> i32
  %t9 = llvm.load %n.3 : !llvm.ptr -> i32
  %t10 = arith.cmpi slt, %t8, %t9 : i32
  cf.cond_br %t10, ^L3, ^b1
^b1:
  %t11 = llvm.load %s.5 : !llvm.ptr -> i32
  llvm.store %t11, %retval : i32, !llvm.ptr
  cf.br ^L2
^L2:
  %t12 = llvm.load %retval : !llvm.ptr -> i32
  func.return %t12 : i32
}
}
