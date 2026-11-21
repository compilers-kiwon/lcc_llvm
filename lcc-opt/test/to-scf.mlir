// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// RUN: lcc-opt --lcc-to-scf --split-input-file %s \
// RUN:   | FileCheck %s --implicit-check-not=scf.while
//
// lcc-to-scf turns lcc's loops into scf.for.  The inputs are what lcc
// f2445d8 (mlir/x86_64-linux) gives for the C files in the comments, made in
// the top directory of lcc with
//
//   make -r BUILDDIR=$B HOSTFILE=etc/mlir-x86_64-linux.c rcc lcc cpp headers
//   LCCDIR=$B $B/lcc -S convfix.c bounds.c
//   LCCDIR=$B $B/lcc -S -Wf-llvmfunc llvmfunc.c
//
// convfix.c, a 1x1 int8 convolution on fixed size global arrays:
//
//   #define HW 64
//   #define IC 16
//   #define OC 32
//   signed char in[HW][IC], w[OC][IC], out[HW][OC];
//   int bias[OC];
//
//   void conv(void)
//   {
//   	int p, o, i;
//
//   	for (p = 0; p < HW; p++)
//   		for (o = 0; o < OC; o++) {
//   			int acc = bias[o];
//   			for (i = 0; i < IC; i++)
//   				acc += w[o][i] * in[p][i];
//   			if (acc < -128) acc = -128;
//   			if (acc > 127) acc = 127;
//   			out[p][o] = (signed char)acc;
//   		}
//   }
//
// lcc leaves out the first test of the three loops, which it can show true,
// so they come as do-while loops: lcc-rotate-do-while makes them while
// loops and lcc-uplift-while scf.for, with the bounds of the C loops.  The
// row offsets p*16 and p*32, which the inner loops compute and the middle
// loop uses after them, end up in the outer loop.

// CHECK-LABEL: func.func @conv()
//   CHECK-DAG:   %[[C0:.*]] = arith.constant 0 : i32
//   CHECK-DAG:   %[[C1:.*]] = arith.constant 1 : i32
//   CHECK-DAG:   %[[C16:.*]] = arith.constant 16 : i32
//   CHECK-DAG:   %[[C32:.*]] = arith.constant 32 : i32
//   CHECK-DAG:   %[[C64:.*]] = arith.constant 64 : i32
//       CHECK:   scf.for %[[P:.*]] = %[[C0]] to %[[C64]] step %[[C1]] : i32 {
//       CHECK:     arith.extsi %[[P]] : i32 to i64
//       CHECK:     scf.for %[[O:.*]] = %[[C0]] to %[[C32]] step %[[C1]] : i32 {
//       CHECK:       %[[BIAS:.*]] = llvm.load %{{.*}} : !llvm.ptr -> i32
//       CHECK:       %[[ACC:.*]] = scf.for %[[I:.*]] = %[[C0]] to %[[C16]] step %[[C1]] iter_args(%[[A:.*]] = %[[BIAS]]) -> (i32) : i32 {
//       CHECK:         %[[M:.*]] = arith.muli
//       CHECK:         %[[AN:.*]] = arith.addi %[[A]], %[[M]] : i32
//       CHECK:         scf.yield %[[AN]] : i32
//       CHECK:       }
//       CHECK:       %[[LO:.*]] = arith.maxsi %[[ACC]], %{{.*}} : i32
//       CHECK:       %[[HI:.*]] = arith.minsi %[[LO]], %{{.*}} : i32
//       CHECK:       %[[B:.*]] = arith.trunci %[[HI]] : i32 to i8
//       CHECK:       llvm.store %[[B]], %{{.*}} : i8, !llvm.ptr
//       CHECK:     }
//       CHECK:   }
//   CHECK-NOT:   cf.
//       CHECK:   return

module attributes {llvm.data_layout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128", llvm.target_triple = "x86_64-pc-linux-gnu"} {
func.func @"conv"() {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %bias.addr = llvm.mlir.addressof @"bias" : !llvm.ptr
  %c2_i64 = arith.constant 2 : i64
  %w.addr = llvm.mlir.addressof @"w" : !llvm.ptr
  %c4_i64 = arith.constant 4 : i64
  %in.addr = llvm.mlir.addressof @"in" : !llvm.ptr
  %c16_i32 = arith.constant 16 : i32
  %c-128_i32 = arith.constant -128 : i32
  %c127_i32 = arith.constant 127 : i32
  %out.addr = llvm.mlir.addressof @"out" : !llvm.ptr
  %c5_i64 = arith.constant 5 : i64
  %c32_i32 = arith.constant 32 : i32
  %c64_i32 = arith.constant 64 : i32
  %i.1 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %o.2 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %p.3 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %acc.4 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %c0_i32, %p.3 : i32, !llvm.ptr
  cf.br ^L2
^L2:
  llvm.store %c0_i32, %o.2 : i32, !llvm.ptr
  cf.br ^L6
^L6:
  %t2 = llvm.load %o.2 : !llvm.ptr -> i32
  %t3 = arith.extsi %t2 : i32 to i64
  %t4 = arith.shli %t3, %c2_i64 : i64
  %t1 = llvm.getelementptr %bias.addr[%t4] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t5 = llvm.load %t1 : !llvm.ptr -> i32
  llvm.store %t5, %acc.4 : i32, !llvm.ptr
  llvm.store %c0_i32, %i.1 : i32, !llvm.ptr
  cf.br ^L10
^L10:
  %t6 = llvm.load %acc.4 : !llvm.ptr -> i32
  %t9 = llvm.load %o.2 : !llvm.ptr -> i32
  %t10 = arith.extsi %t9 : i32 to i64
  %t11 = arith.shli %t10, %c4_i64 : i64
  %t8 = llvm.getelementptr %w.addr[%t11] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t12 = llvm.load %i.1 : !llvm.ptr -> i32
  %t13 = arith.extsi %t12 : i32 to i64
  %t7 = llvm.getelementptr %t8[%t13] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t14 = llvm.load %t7 : !llvm.ptr -> i8
  %t15 = arith.extsi %t14 : i8 to i32
  %t18 = llvm.load %p.3 : !llvm.ptr -> i32
  %t19 = arith.extsi %t18 : i32 to i64
  %t20 = arith.shli %t19, %c4_i64 : i64
  %t17 = llvm.getelementptr %in.addr[%t20] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t16 = llvm.getelementptr %t17[%t13] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t21 = llvm.load %t16 : !llvm.ptr -> i8
  %t22 = arith.extsi %t21 : i8 to i32
  %t23 = arith.muli %t15, %t22 : i32
  %t24 = arith.addi %t6, %t23 : i32
  llvm.store %t24, %acc.4 : i32, !llvm.ptr
  cf.br ^L11
^L11:
  %t25 = llvm.load %i.1 : !llvm.ptr -> i32
  %t26 = arith.addi %t25, %c1_i32 : i32
  llvm.store %t26, %i.1 : i32, !llvm.ptr
  %t27 = llvm.load %i.1 : !llvm.ptr -> i32
  %t28 = arith.cmpi slt, %t27, %c16_i32 : i32
  cf.cond_br %t28, ^L10, ^b1
^b1:
  %t29 = llvm.load %acc.4 : !llvm.ptr -> i32
  %t30 = arith.cmpi sge, %t29, %c-128_i32 : i32
  cf.cond_br %t30, ^L14, ^b2
^b2:
  llvm.store %c-128_i32, %acc.4 : i32, !llvm.ptr
  cf.br ^L14
^L14:
  %t31 = llvm.load %acc.4 : !llvm.ptr -> i32
  %t32 = arith.cmpi sle, %t31, %c127_i32 : i32
  cf.cond_br %t32, ^L16, ^b3
^b3:
  llvm.store %c127_i32, %acc.4 : i32, !llvm.ptr
  cf.br ^L16
^L16:
  %t35 = llvm.load %p.3 : !llvm.ptr -> i32
  %t36 = arith.extsi %t35 : i32 to i64
  %t37 = arith.shli %t36, %c5_i64 : i64
  %t34 = llvm.getelementptr %out.addr[%t37] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t38 = llvm.load %o.2 : !llvm.ptr -> i32
  %t39 = arith.extsi %t38 : i32 to i64
  %t33 = llvm.getelementptr %t34[%t39] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t40 = llvm.load %acc.4 : !llvm.ptr -> i32
  %t41 = arith.trunci %t40 : i32 to i8
  llvm.store %t41, %t33 : i8, !llvm.ptr
  cf.br ^L7
^L7:
  %t42 = llvm.load %o.2 : !llvm.ptr -> i32
  %t43 = arith.addi %t42, %c1_i32 : i32
  llvm.store %t43, %o.2 : i32, !llvm.ptr
  %t44 = llvm.load %o.2 : !llvm.ptr -> i32
  %t45 = arith.cmpi slt, %t44, %c32_i32 : i32
  cf.cond_br %t45, ^L6, ^b4
^b4:
  cf.br ^L3
^L3:
  %t46 = llvm.load %p.3 : !llvm.ptr -> i32
  %t47 = arith.addi %t46, %c1_i32 : i32
  llvm.store %t47, %p.3 : i32, !llvm.ptr
  %t48 = llvm.load %p.3 : !llvm.ptr -> i32
  %t49 = arith.cmpi slt, %t48, %c64_i32 : i32
  cf.cond_br %t49, ^L2, ^b5
^b5:
  cf.br ^L1
^L1:
  func.return
}
llvm.mlir.global external @"bias"() {alignment = 16 : i64} : !llvm.array<128 x i8> {
  %v1 = llvm.mlir.zero : !llvm.array<128 x i8>
  llvm.return %v1 : !llvm.array<128 x i8>
}
llvm.mlir.global external @"out"() {alignment = 16 : i64} : !llvm.array<2048 x i8> {
  %v1 = llvm.mlir.zero : !llvm.array<2048 x i8>
  llvm.return %v1 : !llvm.array<2048 x i8>
}
llvm.mlir.global external @"w"() {alignment = 16 : i64} : !llvm.array<512 x i8> {
  %v1 = llvm.mlir.zero : !llvm.array<512 x i8>
  llvm.return %v1 : !llvm.array<512 x i8>
}
llvm.mlir.global external @"in"() {alignment = 16 : i64} : !llvm.array<1024 x i8> {
  %v1 = llvm.mlir.zero : !llvm.array<1024 x i8>
  llvm.return %v1 : !llvm.array<1024 x i8>
}
}

// -----

// bounds.c, loops whose bound is a parameter:
//
//   void axpy(int n, int a, int *x, int *y)
//   {
//   	int i;
//
//   	for (i = 0; i < n; i++)
//   		y[i] += a * x[i];
//   }
//
//   int stride(int n, int *x)
//   {
//   	int i, s;
//
//   	s = 0;
//   	for (i = 0; i < n; i += 4)
//   		s += x[i];
//   	return i + s;
//   }
//
//   int fixed(int *x)
//   {
//   	int i, s;
//
//   	s = 0;
//   	for (i = 0; i < 10; i += 3)
//   		s += x[i];
//   	return i + s;
//   }
//
// lcc keeps the first test of these loops, which lift-cf-to-scf makes while
// loops.  The final value of i, used in stride and fixed, is i + 4 carried
// by the scf.for in stride and the constant 12 in fixed, whose loop lcc
// gives as a do-while loop.

// CHECK-LABEL: func.func @axpy(
//  CHECK-SAME:     %[[N:.*]]: i32, %[[A:.*]]: i32, %[[X:.*]]: !llvm.ptr, %[[Y:.*]]: !llvm.ptr)
//   CHECK-DAG:   %[[C0:.*]] = arith.constant 0 : i32
//   CHECK-DAG:   %[[C1:.*]] = arith.constant 1 : i32
//       CHECK:   scf.for %[[I:.*]] = %[[C0]] to %[[N]] step %[[C1]] : i32 {
//       CHECK:     llvm.store
//       CHECK:   }
//   CHECK-NOT:   cf.
//       CHECK:   return

// CHECK-LABEL: func.func @stride(
//  CHECK-SAME:     %[[N:.*]]: i32, %[[X:.*]]: !llvm.ptr) -> i32
//   CHECK-DAG:   %[[C0:.*]] = arith.constant 0 : i32
//   CHECK-DAG:   %[[C4:.*]] = arith.constant 4 : i32
//       CHECK:   %[[R:.*]]:2 = scf.for %[[I:.*]] = %[[C0]] to %[[N]] step %[[C4]] iter_args(%[[S:.*]] = %[[C0]], %{{.*}} = %[[C0]]) -> (i32, i32) : i32 {
//       CHECK:     %[[SN:.*]] = arith.addi %[[S]], %{{.*}} : i32
//       CHECK:     %[[IN:.*]] = arith.addi %[[I]], %[[C4]] : i32
//       CHECK:     scf.yield %[[SN]], %[[IN]] : i32, i32
//       CHECK:   }
//       CHECK:   %[[T:.*]] = arith.addi %[[R]]#1, %[[R]]#0 : i32
//       CHECK:   return %[[T]] : i32

// CHECK-LABEL: func.func @fixed(
//   CHECK-DAG:   %[[C12:.*]] = arith.constant 12 : i32
//   CHECK-DAG:   %[[C0:.*]] = arith.constant 0 : i32
//   CHECK-DAG:   %[[C3:.*]] = arith.constant 3 : i32
//   CHECK-DAG:   %[[C10:.*]] = arith.constant 10 : i32
//       CHECK:   %[[S:.*]] = scf.for %{{.*}} = %[[C0]] to %[[C10]] step %[[C3]] iter_args(
//       CHECK:   }
//       CHECK:   %[[T:.*]] = arith.addi %[[S]], %[[C12]] : i32
//       CHECK:   return %[[T]] : i32

module attributes {llvm.data_layout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128", llvm.target_triple = "x86_64-pc-linux-gnu"} {
func.func @"axpy"(%a0: i32, %a1: i32, %a2: !llvm.ptr, %a3: !llvm.ptr) {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %c2_i64 = arith.constant 2 : i64
  %n.1 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %a0, %n.1 : i32, !llvm.ptr
  %n.2 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %a.3 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %a1, %a.3 : i32, !llvm.ptr
  %a.4 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %x.5 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  llvm.store %a2, %x.5 : !llvm.ptr, !llvm.ptr
  %x.6 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  %y.7 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  llvm.store %a3, %y.7 : !llvm.ptr, !llvm.ptr
  %y.8 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  %i.9 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %t1 = llvm.load %n.1 : !llvm.ptr -> i32
  llvm.store %t1, %n.2 : i32, !llvm.ptr
  %t2 = llvm.load %a.3 : !llvm.ptr -> i32
  llvm.store %t2, %a.4 : i32, !llvm.ptr
  %t3 = llvm.load %x.5 : !llvm.ptr -> !llvm.ptr
  llvm.store %t3, %x.6 : !llvm.ptr, !llvm.ptr
  %t4 = llvm.load %y.7 : !llvm.ptr -> !llvm.ptr
  llvm.store %t4, %y.8 : !llvm.ptr, !llvm.ptr
  llvm.store %c0_i32, %i.9 : i32, !llvm.ptr
  cf.br ^L5
^L2:
  %t6 = llvm.load %y.8 : !llvm.ptr -> !llvm.ptr
  %t7 = llvm.load %i.9 : !llvm.ptr -> i32
  %t8 = arith.extsi %t7 : i32 to i64
  %t9 = arith.shli %t8, %c2_i64 : i64
  %t5 = llvm.getelementptr %t6[%t9] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t10 = llvm.load %t5 : !llvm.ptr -> i32
  %t11 = llvm.load %a.4 : !llvm.ptr -> i32
  %t13 = llvm.load %x.6 : !llvm.ptr -> !llvm.ptr
  %t12 = llvm.getelementptr %t13[%t9] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t14 = llvm.load %t12 : !llvm.ptr -> i32
  %t15 = arith.muli %t11, %t14 : i32
  %t16 = arith.addi %t10, %t15 : i32
  llvm.store %t16, %t5 : i32, !llvm.ptr
  cf.br ^L3
^L3:
  %t17 = llvm.load %i.9 : !llvm.ptr -> i32
  %t18 = arith.addi %t17, %c1_i32 : i32
  llvm.store %t18, %i.9 : i32, !llvm.ptr
  cf.br ^L5
^L5:
  %t19 = llvm.load %i.9 : !llvm.ptr -> i32
  %t20 = llvm.load %n.2 : !llvm.ptr -> i32
  %t21 = arith.cmpi slt, %t19, %t20 : i32
  cf.cond_br %t21, ^L2, ^b1
^b1:
  cf.br ^L1
^L1:
  func.return
}
func.func @"stride"(%a0: i32, %a1: !llvm.ptr) -> i32 {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %c2_i64 = arith.constant 2 : i64
  %c4_i32 = arith.constant 4 : i32
  %retval = llvm.alloca %c1_i32 x i32 : (i32) -> !llvm.ptr
  %n.10 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %a0, %n.10 : i32, !llvm.ptr
  %n.11 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %x.12 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  llvm.store %a1, %x.12 : !llvm.ptr, !llvm.ptr
  %x.13 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  %i.14 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %s.15 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %t1 = llvm.load %n.10 : !llvm.ptr -> i32
  llvm.store %t1, %n.11 : i32, !llvm.ptr
  %t2 = llvm.load %x.12 : !llvm.ptr -> !llvm.ptr
  llvm.store %t2, %x.13 : !llvm.ptr, !llvm.ptr
  llvm.store %c0_i32, %s.15 : i32, !llvm.ptr
  llvm.store %c0_i32, %i.14 : i32, !llvm.ptr
  cf.br ^L10
^L7:
  %t3 = llvm.load %s.15 : !llvm.ptr -> i32
  %t5 = llvm.load %x.13 : !llvm.ptr -> !llvm.ptr
  %t6 = llvm.load %i.14 : !llvm.ptr -> i32
  %t7 = arith.extsi %t6 : i32 to i64
  %t8 = arith.shli %t7, %c2_i64 : i64
  %t4 = llvm.getelementptr %t5[%t8] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t9 = llvm.load %t4 : !llvm.ptr -> i32
  %t10 = arith.addi %t3, %t9 : i32
  llvm.store %t10, %s.15 : i32, !llvm.ptr
  cf.br ^L8
^L8:
  %t11 = llvm.load %i.14 : !llvm.ptr -> i32
  %t12 = arith.addi %t11, %c4_i32 : i32
  llvm.store %t12, %i.14 : i32, !llvm.ptr
  cf.br ^L10
^L10:
  %t13 = llvm.load %i.14 : !llvm.ptr -> i32
  %t14 = llvm.load %n.11 : !llvm.ptr -> i32
  %t15 = arith.cmpi slt, %t13, %t14 : i32
  cf.cond_br %t15, ^L7, ^b1
^b1:
  %t16 = llvm.load %i.14 : !llvm.ptr -> i32
  %t17 = llvm.load %s.15 : !llvm.ptr -> i32
  %t18 = arith.addi %t16, %t17 : i32
  llvm.store %t18, %retval : i32, !llvm.ptr
  cf.br ^L6
^L6:
  %t19 = llvm.load %retval : !llvm.ptr -> i32
  func.return %t19 : i32
}
func.func @"fixed"(%a0: !llvm.ptr) -> i32 {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %c2_i64 = arith.constant 2 : i64
  %c3_i32 = arith.constant 3 : i32
  %c10_i32 = arith.constant 10 : i32
  %retval = llvm.alloca %c1_i32 x i32 : (i32) -> !llvm.ptr
  %x.16 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  llvm.store %a0, %x.16 : !llvm.ptr, !llvm.ptr
  %x.17 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  %i.18 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %s.19 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %t1 = llvm.load %x.16 : !llvm.ptr -> !llvm.ptr
  llvm.store %t1, %x.17 : !llvm.ptr, !llvm.ptr
  llvm.store %c0_i32, %s.19 : i32, !llvm.ptr
  llvm.store %c0_i32, %i.18 : i32, !llvm.ptr
  cf.br ^L12
^L12:
  %t2 = llvm.load %s.19 : !llvm.ptr -> i32
  %t4 = llvm.load %x.17 : !llvm.ptr -> !llvm.ptr
  %t5 = llvm.load %i.18 : !llvm.ptr -> i32
  %t6 = arith.extsi %t5 : i32 to i64
  %t7 = arith.shli %t6, %c2_i64 : i64
  %t3 = llvm.getelementptr %t4[%t7] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t8 = llvm.load %t3 : !llvm.ptr -> i32
  %t9 = arith.addi %t2, %t8 : i32
  llvm.store %t9, %s.19 : i32, !llvm.ptr
  cf.br ^L13
^L13:
  %t10 = llvm.load %i.18 : !llvm.ptr -> i32
  %t11 = arith.addi %t10, %c3_i32 : i32
  llvm.store %t11, %i.18 : i32, !llvm.ptr
  %t12 = llvm.load %i.18 : !llvm.ptr -> i32
  %t13 = arith.cmpi slt, %t12, %c10_i32 : i32
  cf.cond_br %t13, ^L12, ^b1
^b1:
  %t14 = llvm.load %i.18 : !llvm.ptr -> i32
  %t15 = llvm.load %s.19 : !llvm.ptr -> i32
  %t16 = arith.addi %t14, %t15 : i32
  llvm.store %t16, %retval : i32, !llvm.ptr
  cf.br ^L11
^L11:
  %t17 = llvm.load %retval : !llvm.ptr -> i32
  func.return %t17 : i32
}
}

// -----

// llvmfunc.c, axpy of bounds.c as llvm.func (-Wf-llvmfunc; lcc also makes
// variadic functions and functions without a prototype llvm.func):
//
//   void axpy(int n, int a, int *x, int *y)
//   {
//   	int i;
//
//   	for (i = 0; i < n; i++)
//   		y[i] += a * x[i];
//   }
//
// lift-cf-to-scf lifts only func.func; lcc-lift-llvm-func lifts llvm.func,
// and the loop becomes the same scf.for as in func.func.

// CHECK-LABEL: llvm.func @axpy(
//  CHECK-SAME:     %[[N:.*]]: i32, %[[A:.*]]: i32, %[[X:.*]]: !llvm.ptr, %[[Y:.*]]: !llvm.ptr)
//   CHECK-NOT:   llvm.alloca
//   CHECK-DAG:   %[[C0:.*]] = arith.constant 0 : i32
//   CHECK-DAG:   %[[C1:.*]] = arith.constant 1 : i32
//       CHECK:   scf.for %[[I:.*]] = %[[C0]] to %[[N]] step %[[C1]] : i32 {
//       CHECK:     llvm.store
//       CHECK:   }
//   CHECK-NOT:   cf.
//       CHECK:   llvm.return

module attributes {llvm.data_layout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128", llvm.target_triple = "x86_64-pc-linux-gnu"} {
llvm.func @"axpy"(%a0: i32, %a1: i32, %a2: !llvm.ptr, %a3: !llvm.ptr) {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %c2_i64 = arith.constant 2 : i64
  %n.1 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %a0, %n.1 : i32, !llvm.ptr
  %n.2 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %a.3 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %a1, %a.3 : i32, !llvm.ptr
  %a.4 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %x.5 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  llvm.store %a2, %x.5 : !llvm.ptr, !llvm.ptr
  %x.6 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  %y.7 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  llvm.store %a3, %y.7 : !llvm.ptr, !llvm.ptr
  %y.8 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  %i.9 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %t1 = llvm.load %n.1 : !llvm.ptr -> i32
  llvm.store %t1, %n.2 : i32, !llvm.ptr
  %t2 = llvm.load %a.3 : !llvm.ptr -> i32
  llvm.store %t2, %a.4 : i32, !llvm.ptr
  %t3 = llvm.load %x.5 : !llvm.ptr -> !llvm.ptr
  llvm.store %t3, %x.6 : !llvm.ptr, !llvm.ptr
  %t4 = llvm.load %y.7 : !llvm.ptr -> !llvm.ptr
  llvm.store %t4, %y.8 : !llvm.ptr, !llvm.ptr
  llvm.store %c0_i32, %i.9 : i32, !llvm.ptr
  cf.br ^L5
^L2:
  %t6 = llvm.load %y.8 : !llvm.ptr -> !llvm.ptr
  %t7 = llvm.load %i.9 : !llvm.ptr -> i32
  %t8 = arith.extsi %t7 : i32 to i64
  %t9 = arith.shli %t8, %c2_i64 : i64
  %t5 = llvm.getelementptr %t6[%t9] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t10 = llvm.load %t5 : !llvm.ptr -> i32
  %t11 = llvm.load %a.4 : !llvm.ptr -> i32
  %t13 = llvm.load %x.6 : !llvm.ptr -> !llvm.ptr
  %t12 = llvm.getelementptr %t13[%t9] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t14 = llvm.load %t12 : !llvm.ptr -> i32
  %t15 = arith.muli %t11, %t14 : i32
  %t16 = arith.addi %t10, %t15 : i32
  llvm.store %t16, %t5 : i32, !llvm.ptr
  cf.br ^L3
^L3:
  %t17 = llvm.load %i.9 : !llvm.ptr -> i32
  %t18 = arith.addi %t17, %c1_i32 : i32
  llvm.store %t18, %i.9 : i32, !llvm.ptr
  cf.br ^L5
^L5:
  %t19 = llvm.load %i.9 : !llvm.ptr -> i32
  %t20 = llvm.load %n.2 : !llvm.ptr -> i32
  %t21 = arith.cmpi slt, %t19, %t20 : i32
  cf.cond_br %t21, ^L2, ^b1
^b1:
  cf.br ^L1
^L1:
  llvm.return
}
}
