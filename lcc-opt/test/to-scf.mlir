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
// loops.  stride stays one: with the step 4 and n not a constant, i may
// wrap past INT_MAX, which the scf.while does and scf.for leaves undefined.
// The final i of fixed, whose loop lcc gives as a do-while loop, is 12.

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
//   CHECK-NOT:   scf.for
//       CHECK:   %[[R:.*]]:2 = scf.while (%[[I:.*]] = %[[C0]], %[[S:.*]] = %[[C0]]) : (i32, i32) -> (i32, i32) {
//  CHECK-NEXT:     %[[T:.*]] = arith.cmpi slt, %[[I]], %[[N]] : i32
//  CHECK-NEXT:     scf.condition(%[[T]]) %[[I]], %[[S]] : i32, i32
//       CHECK:     %[[IN:.*]] = arith.addi %{{.*}}, %[[C4]] : i32
//       CHECK:     scf.yield %[[IN]], %{{.*}} : i32, i32
//       CHECK:   }
//       CHECK:   %[[E:.*]] = arith.addi %[[R]]#0, %[[R]]#1 : i32
//       CHECK:   return %[[E]] : i32

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

// -----

// forms.c, loops counting with <=, unsigned <, down, and !=:
//
//   #include <limits.h>
//
//   int le(int *x)
//   {
//   	int i, s;
//
//   	s = 0;
//   	for (i = 0; i <= 9; i++)
//   		s += x[i];
//   	return s * 100 + i;
//   }
//
//   int le_max(void)
//   {
//   	int i, s;
//
//   	s = 0;
//   	for (i = INT_MAX - 5; i <= INT_MAX - 1; i++)
//   		s += i & 7;
//   	return s * 100 + (i - (INT_MAX - 10));
//   }
//
//   unsigned ult(unsigned n, int *x)
//   {
//   	unsigned u, s;
//
//   	s = 0;
//   	for (u = 0; u < n; u++)
//   		s += x[u & 7];
//   	return s * 100 + u;
//   }
//
//   unsigned ult_max(unsigned n)
//   {
//   	unsigned u, s;
//
//   	s = 0;
//   	for (u = n; u < UINT_MAX - 2; u++)
//   		s += u & 15;
//   	return s * 1000 + (u - (UINT_MAX - 20));
//   }
//
//   int down(int n, int *x)
//   {
//   	int i, s;
//
//   	s = 0;
//   	for (i = n; i > 0; i--)
//   		s += x[i & 7];
//   	return s * 100 + i;
//   }
//
//   int down_ge(int n, int *x)
//   {
//   	int i, s;
//
//   	s = 0;
//   	for (i = n; i >= 0; i--)
//   		s += x[i & 7] * i;
//   	return s * 100 + i;
//   }
//
//   int down_min(int n)
//   {
//   	int i, s;
//
//   	s = 0;
//   	for (i = n; i > INT_MIN; i--)
//   		s += i & 7;
//   	return s * 100 + (i - INT_MIN);
//   }
//
//   int down3(int n)
//   {
//   	int i, s;
//
//   	s = 0;
//   	for (i = n; i >= -5; i -= 3)
//   		s += i;
//   	return s * 100 + i;
//   }
//
//   unsigned udown(unsigned n)
//   {
//   	unsigned u, s;
//
//   	s = 0;
//   	for (u = n; u > 0; u--)
//   		s += u;
//   	return s * 100 + u;
//   }
//
//   int ne_up(int *x)
//   {
//   	int i, s;
//
//   	s = 0;
//   	for (i = 3; i != 7; i++)
//   		s += x[i];
//   	return s * 100 + i;
//   }
//
//   int ne_down(int *x)
//   {
//   	int i, s;
//
//   	s = 0;
//   	for (i = 7; i != 2; i--)
//   		s += x[i] * i;
//   	return s * 100 + i;
//   }
//
//   unsigned ne_cross(void)
//   {
//   	unsigned u, s;
//
//   	s = 0;
//   	for (u = 0x7ffffffd; u != 0x80000002; u++)
//   		s += u & 255;
//   	return s * 1000 + (u & 255);
//   }
//
//   int none(int *x)
//   {
//   	int i, s;
//
//   	s = 0;
//   	for (i = 10; i < 5; i++)
//   		s += x[i];
//   	return s * 100 + i;
//   }
//
//   unsigned ne_cross2(void)
//   {
//   	unsigned u, s;
//
//   	s = 0;
//   	for (u = 0x7ffffffdu; u != 0x80000002u; u++)
//   		s += u & 255;
//   	return s * 1000 + (u & 255);
//   }
//
// made with `LCCDIR=$B $B/lcc -S forms.c`.  Each becomes an scf.for with no
// reliance on C's signed overflow: the bounds near INT_MAX, UINT_MAX and
// INT_MIN are constants that keep the induction variable from wrapping.
// Counting down, k counts the trips.  ne_cross stays a while loop: lcc
// types 0x80000002 as long on LP64 (C89 says unsigned int) and compares
// the zero-extended u; ne_cross2, with 0x80000002u, does not.

// CHECK-LABEL: func.func @le(
//       CHECK:   scf.for %{{.*}} = %c0_i32 to %c10_i32 step %c1_i32
// CHECK-LABEL: func.func @le_max(
//       CHECK:   scf.for %{{.*}} = %c2147483642_i32 to %c2147483647_i32 step %c1_i32
// CHECK-LABEL: func.func @ult(
//       CHECK:   scf.for unsigned %{{.*}} = %c0_i32 to %arg0 step %c1_i32
// CHECK-LABEL: func.func @ult_max(
//       CHECK:   scf.for unsigned %{{.*}} = %arg0 to %c-3_i32 step %c1_i32
// CHECK-LABEL: func.func @down(
//       CHECK:   scf.for unsigned %[[K:.*]] = %c0_i32{{.*}} to %{{.*}} step %c1_i32
//  CHECK-NEXT:     arith.subi %arg0, %[[K]] : i32
// CHECK-LABEL: func.func @down_ge(
//       CHECK:   scf.for unsigned
// CHECK-LABEL: func.func @down_min(
//       CHECK:   scf.for unsigned
// CHECK-LABEL: func.func @down3(
//       CHECK:   scf.for unsigned %[[K:.*]] = %c0_i32{{.*}} to %{{.*}} step %c1_i32
//  CHECK-NEXT:     %[[M:.*]] = arith.muli %[[K]], %c3_i32 : i32
//  CHECK-NEXT:     arith.subi %arg0, %[[M]] : i32
// CHECK-LABEL: func.func @udown(
//       CHECK:   scf.for unsigned
// CHECK-LABEL: func.func @ne_up(
//       CHECK:   scf.for %{{.*}} = %c3_i32 to %c7_i32 step %c1_i32
// CHECK-LABEL: func.func @ne_down(
//       CHECK:   scf.for unsigned %{{.*}} = %c0_i32 to %c5_i32 step %c1_i32
// CHECK-LABEL: func.func @ne_cross(
//       CHECK:   scf.while
//       CHECK:     arith.cmpi ne, %{{.*}}, %c2147483650_i64 : i64
// CHECK-LABEL: func.func @none(
//       CHECK:   arith.constant 10 : i32
//       CHECK:   return
// CHECK-LABEL: func.func @ne_cross2(
//       CHECK:   scf.for unsigned %{{.*}} = %c2147483645_i32 to %c-2147483646_i32 step %c1_i32

module attributes {llvm.data_layout = "e-m:e-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128", llvm.target_triple = "aarch64-unknown-linux-gnu"} {
func.func @"le"(%a0: !llvm.ptr) -> i32 {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %c2_i64 = arith.constant 2 : i64
  %c9_i32 = arith.constant 9 : i32
  %c100_i32 = arith.constant 100 : i32
  %retval = llvm.alloca %c1_i32 x i32 : (i32) -> !llvm.ptr
  %x.1 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  llvm.store %a0, %x.1 : !llvm.ptr, !llvm.ptr
  %x.2 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  %i.3 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %s.4 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %t1 = llvm.load %x.1 : !llvm.ptr -> !llvm.ptr
  llvm.store %t1, %x.2 : !llvm.ptr, !llvm.ptr
  llvm.store %c0_i32, %s.4 : i32, !llvm.ptr
  llvm.store %c0_i32, %i.3 : i32, !llvm.ptr
  cf.br ^L2
^L2:
  %t2 = llvm.load %s.4 : !llvm.ptr -> i32
  %t4 = llvm.load %x.2 : !llvm.ptr -> !llvm.ptr
  %t5 = llvm.load %i.3 : !llvm.ptr -> i32
  %t6 = arith.extsi %t5 : i32 to i64
  %t7 = arith.shli %t6, %c2_i64 : i64
  %t3 = llvm.getelementptr %t4[%t7] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t8 = llvm.load %t3 : !llvm.ptr -> i32
  %t9 = arith.addi %t2, %t8 : i32
  llvm.store %t9, %s.4 : i32, !llvm.ptr
  cf.br ^L3
^L3:
  %t10 = llvm.load %i.3 : !llvm.ptr -> i32
  %t11 = arith.addi %t10, %c1_i32 : i32
  llvm.store %t11, %i.3 : i32, !llvm.ptr
  %t12 = llvm.load %i.3 : !llvm.ptr -> i32
  %t13 = arith.cmpi sle, %t12, %c9_i32 : i32
  cf.cond_br %t13, ^L2, ^b1
^b1:
  %t14 = llvm.load %s.4 : !llvm.ptr -> i32
  %t15 = arith.muli %c100_i32, %t14 : i32
  %t16 = llvm.load %i.3 : !llvm.ptr -> i32
  %t17 = arith.addi %t15, %t16 : i32
  llvm.store %t17, %retval : i32, !llvm.ptr
  cf.br ^L1
^L1:
  %t18 = llvm.load %retval : !llvm.ptr -> i32
  func.return %t18 : i32
}
func.func @"le_max"() -> i32 {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %c2147483642_i32 = arith.constant 2147483642 : i32
  %c7_i32 = arith.constant 7 : i32
  %c2147483646_i32 = arith.constant 2147483646 : i32
  %c100_i32 = arith.constant 100 : i32
  %c2147483637_i32 = arith.constant 2147483637 : i32
  %retval = llvm.alloca %c1_i32 x i32 : (i32) -> !llvm.ptr
  %i.5 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %s.6 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %c0_i32, %s.6 : i32, !llvm.ptr
  llvm.store %c2147483642_i32, %i.5 : i32, !llvm.ptr
  cf.br ^L7
^L7:
  %t1 = llvm.load %s.6 : !llvm.ptr -> i32
  %t2 = llvm.load %i.5 : !llvm.ptr -> i32
  %t3 = arith.andi %t2, %c7_i32 : i32
  %t4 = arith.addi %t1, %t3 : i32
  llvm.store %t4, %s.6 : i32, !llvm.ptr
  cf.br ^L8
^L8:
  %t5 = llvm.load %i.5 : !llvm.ptr -> i32
  %t6 = arith.addi %t5, %c1_i32 : i32
  llvm.store %t6, %i.5 : i32, !llvm.ptr
  %t7 = llvm.load %i.5 : !llvm.ptr -> i32
  %t8 = arith.cmpi sle, %t7, %c2147483646_i32 : i32
  cf.cond_br %t8, ^L7, ^b1
^b1:
  %t9 = llvm.load %s.6 : !llvm.ptr -> i32
  %t10 = arith.muli %c100_i32, %t9 : i32
  %t11 = llvm.load %i.5 : !llvm.ptr -> i32
  %t12 = arith.subi %t11, %c2147483637_i32 : i32
  %t13 = arith.addi %t10, %t12 : i32
  llvm.store %t13, %retval : i32, !llvm.ptr
  cf.br ^L6
^L6:
  %t14 = llvm.load %retval : !llvm.ptr -> i32
  func.return %t14 : i32
}
func.func @"ult"(%a0: i32, %a1: !llvm.ptr) -> i32 {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %c7_i32 = arith.constant 7 : i32
  %c2_i64 = arith.constant 2 : i64
  %c100_i32 = arith.constant 100 : i32
  %retval = llvm.alloca %c1_i32 x i32 : (i32) -> !llvm.ptr
  %n.7 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %a0, %n.7 : i32, !llvm.ptr
  %n.8 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %x.9 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  llvm.store %a1, %x.9 : !llvm.ptr, !llvm.ptr
  %x.10 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  %u.11 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %s.12 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %t1 = llvm.load %n.7 : !llvm.ptr -> i32
  llvm.store %t1, %n.8 : i32, !llvm.ptr
  %t2 = llvm.load %x.9 : !llvm.ptr -> !llvm.ptr
  llvm.store %t2, %x.10 : !llvm.ptr, !llvm.ptr
  llvm.store %c0_i32, %s.12 : i32, !llvm.ptr
  llvm.store %c0_i32, %u.11 : i32, !llvm.ptr
  cf.br ^L15
^L12:
  %t3 = llvm.load %s.12 : !llvm.ptr -> i32
  %t5 = llvm.load %x.10 : !llvm.ptr -> !llvm.ptr
  %t6 = llvm.load %u.11 : !llvm.ptr -> i32
  %t7 = arith.andi %t6, %c7_i32 : i32
  %t8 = arith.extui %t7 : i32 to i64
  %t9 = arith.shli %t8, %c2_i64 : i64
  %t4 = llvm.getelementptr %t5[%t9] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t10 = llvm.load %t4 : !llvm.ptr -> i32
  %t11 = arith.addi %t3, %t10 : i32
  llvm.store %t11, %s.12 : i32, !llvm.ptr
  cf.br ^L13
^L13:
  %t12 = llvm.load %u.11 : !llvm.ptr -> i32
  %t13 = arith.addi %t12, %c1_i32 : i32
  llvm.store %t13, %u.11 : i32, !llvm.ptr
  cf.br ^L15
^L15:
  %t14 = llvm.load %u.11 : !llvm.ptr -> i32
  %t15 = llvm.load %n.8 : !llvm.ptr -> i32
  %t16 = arith.cmpi ult, %t14, %t15 : i32
  cf.cond_br %t16, ^L12, ^b1
^b1:
  %t17 = llvm.load %s.12 : !llvm.ptr -> i32
  %t18 = arith.muli %c100_i32, %t17 : i32
  %t19 = llvm.load %u.11 : !llvm.ptr -> i32
  %t20 = arith.addi %t18, %t19 : i32
  llvm.store %t20, %retval : i32, !llvm.ptr
  cf.br ^L11
^L11:
  %t21 = llvm.load %retval : !llvm.ptr -> i32
  func.return %t21 : i32
}
func.func @"ult_max"(%a0: i32) -> i32 {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %c15_i32 = arith.constant 15 : i32
  %c-3_i32 = arith.constant -3 : i32
  %c1000_i32 = arith.constant 1000 : i32
  %c-21_i32 = arith.constant -21 : i32
  %retval = llvm.alloca %c1_i32 x i32 : (i32) -> !llvm.ptr
  %n.13 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %a0, %n.13 : i32, !llvm.ptr
  %u.14 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %s.15 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %c0_i32, %s.15 : i32, !llvm.ptr
  %t1 = llvm.load %n.13 : !llvm.ptr -> i32
  llvm.store %t1, %u.14 : i32, !llvm.ptr
  cf.br ^L20
^L17:
  %t2 = llvm.load %s.15 : !llvm.ptr -> i32
  %t3 = llvm.load %u.14 : !llvm.ptr -> i32
  %t4 = arith.andi %t3, %c15_i32 : i32
  %t5 = arith.addi %t2, %t4 : i32
  llvm.store %t5, %s.15 : i32, !llvm.ptr
  cf.br ^L18
^L18:
  %t6 = llvm.load %u.14 : !llvm.ptr -> i32
  %t7 = arith.addi %t6, %c1_i32 : i32
  llvm.store %t7, %u.14 : i32, !llvm.ptr
  cf.br ^L20
^L20:
  %t8 = llvm.load %u.14 : !llvm.ptr -> i32
  %t9 = arith.cmpi ult, %t8, %c-3_i32 : i32
  cf.cond_br %t9, ^L17, ^b1
^b1:
  %t10 = llvm.load %s.15 : !llvm.ptr -> i32
  %t11 = arith.muli %c1000_i32, %t10 : i32
  %t12 = llvm.load %u.14 : !llvm.ptr -> i32
  %t13 = arith.subi %t12, %c-21_i32 : i32
  %t14 = arith.addi %t11, %t13 : i32
  llvm.store %t14, %retval : i32, !llvm.ptr
  cf.br ^L16
^L16:
  %t15 = llvm.load %retval : !llvm.ptr -> i32
  func.return %t15 : i32
}
func.func @"down"(%a0: i32, %a1: !llvm.ptr) -> i32 {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %c7_i32 = arith.constant 7 : i32
  %c2_i64 = arith.constant 2 : i64
  %c100_i32 = arith.constant 100 : i32
  %retval = llvm.alloca %c1_i32 x i32 : (i32) -> !llvm.ptr
  %n.16 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %a0, %n.16 : i32, !llvm.ptr
  %x.17 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  llvm.store %a1, %x.17 : !llvm.ptr, !llvm.ptr
  %x.18 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  %i.19 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %s.20 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %t1 = llvm.load %x.17 : !llvm.ptr -> !llvm.ptr
  llvm.store %t1, %x.18 : !llvm.ptr, !llvm.ptr
  llvm.store %c0_i32, %s.20 : i32, !llvm.ptr
  %t2 = llvm.load %n.16 : !llvm.ptr -> i32
  llvm.store %t2, %i.19 : i32, !llvm.ptr
  cf.br ^L25
^L22:
  %t3 = llvm.load %s.20 : !llvm.ptr -> i32
  %t5 = llvm.load %x.18 : !llvm.ptr -> !llvm.ptr
  %t6 = llvm.load %i.19 : !llvm.ptr -> i32
  %t7 = arith.andi %t6, %c7_i32 : i32
  %t8 = arith.extsi %t7 : i32 to i64
  %t9 = arith.shli %t8, %c2_i64 : i64
  %t4 = llvm.getelementptr %t5[%t9] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t10 = llvm.load %t4 : !llvm.ptr -> i32
  %t11 = arith.addi %t3, %t10 : i32
  llvm.store %t11, %s.20 : i32, !llvm.ptr
  cf.br ^L23
^L23:
  %t12 = llvm.load %i.19 : !llvm.ptr -> i32
  %t13 = arith.subi %t12, %c1_i32 : i32
  llvm.store %t13, %i.19 : i32, !llvm.ptr
  cf.br ^L25
^L25:
  %t14 = llvm.load %i.19 : !llvm.ptr -> i32
  %t15 = arith.cmpi sgt, %t14, %c0_i32 : i32
  cf.cond_br %t15, ^L22, ^b1
^b1:
  %t16 = llvm.load %s.20 : !llvm.ptr -> i32
  %t17 = arith.muli %c100_i32, %t16 : i32
  %t18 = llvm.load %i.19 : !llvm.ptr -> i32
  %t19 = arith.addi %t17, %t18 : i32
  llvm.store %t19, %retval : i32, !llvm.ptr
  cf.br ^L21
^L21:
  %t20 = llvm.load %retval : !llvm.ptr -> i32
  func.return %t20 : i32
}
func.func @"down_ge"(%a0: i32, %a1: !llvm.ptr) -> i32 {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %c7_i32 = arith.constant 7 : i32
  %c2_i64 = arith.constant 2 : i64
  %c100_i32 = arith.constant 100 : i32
  %retval = llvm.alloca %c1_i32 x i32 : (i32) -> !llvm.ptr
  %n.21 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %a0, %n.21 : i32, !llvm.ptr
  %x.22 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  llvm.store %a1, %x.22 : !llvm.ptr, !llvm.ptr
  %x.23 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  %i.24 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %s.25 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %t1 = llvm.load %x.22 : !llvm.ptr -> !llvm.ptr
  llvm.store %t1, %x.23 : !llvm.ptr, !llvm.ptr
  llvm.store %c0_i32, %s.25 : i32, !llvm.ptr
  %t2 = llvm.load %n.21 : !llvm.ptr -> i32
  llvm.store %t2, %i.24 : i32, !llvm.ptr
  cf.br ^L30
^L27:
  %t3 = llvm.load %s.25 : !llvm.ptr -> i32
  %t5 = llvm.load %x.23 : !llvm.ptr -> !llvm.ptr
  %t6 = llvm.load %i.24 : !llvm.ptr -> i32
  %t7 = arith.andi %t6, %c7_i32 : i32
  %t8 = arith.extsi %t7 : i32 to i64
  %t9 = arith.shli %t8, %c2_i64 : i64
  %t4 = llvm.getelementptr %t5[%t9] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t10 = llvm.load %t4 : !llvm.ptr -> i32
  %t11 = arith.muli %t10, %t6 : i32
  %t12 = arith.addi %t3, %t11 : i32
  llvm.store %t12, %s.25 : i32, !llvm.ptr
  cf.br ^L28
^L28:
  %t13 = llvm.load %i.24 : !llvm.ptr -> i32
  %t14 = arith.subi %t13, %c1_i32 : i32
  llvm.store %t14, %i.24 : i32, !llvm.ptr
  cf.br ^L30
^L30:
  %t15 = llvm.load %i.24 : !llvm.ptr -> i32
  %t16 = arith.cmpi sge, %t15, %c0_i32 : i32
  cf.cond_br %t16, ^L27, ^b1
^b1:
  %t17 = llvm.load %s.25 : !llvm.ptr -> i32
  %t18 = arith.muli %c100_i32, %t17 : i32
  %t19 = llvm.load %i.24 : !llvm.ptr -> i32
  %t20 = arith.addi %t18, %t19 : i32
  llvm.store %t20, %retval : i32, !llvm.ptr
  cf.br ^L26
^L26:
  %t21 = llvm.load %retval : !llvm.ptr -> i32
  func.return %t21 : i32
}
func.func @"down_min"(%a0: i32) -> i32 {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %c7_i32 = arith.constant 7 : i32
  %c-2147483648_i32 = arith.constant -2147483648 : i32
  %c100_i32 = arith.constant 100 : i32
  %retval = llvm.alloca %c1_i32 x i32 : (i32) -> !llvm.ptr
  %n.26 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %a0, %n.26 : i32, !llvm.ptr
  %i.27 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %s.28 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %c0_i32, %s.28 : i32, !llvm.ptr
  %t1 = llvm.load %n.26 : !llvm.ptr -> i32
  llvm.store %t1, %i.27 : i32, !llvm.ptr
  cf.br ^L35
^L32:
  %t2 = llvm.load %s.28 : !llvm.ptr -> i32
  %t3 = llvm.load %i.27 : !llvm.ptr -> i32
  %t4 = arith.andi %t3, %c7_i32 : i32
  %t5 = arith.addi %t2, %t4 : i32
  llvm.store %t5, %s.28 : i32, !llvm.ptr
  cf.br ^L33
^L33:
  %t6 = llvm.load %i.27 : !llvm.ptr -> i32
  %t7 = arith.subi %t6, %c1_i32 : i32
  llvm.store %t7, %i.27 : i32, !llvm.ptr
  cf.br ^L35
^L35:
  %t8 = llvm.load %i.27 : !llvm.ptr -> i32
  %t9 = arith.cmpi sgt, %t8, %c-2147483648_i32 : i32
  cf.cond_br %t9, ^L32, ^b1
^b1:
  %t10 = llvm.load %s.28 : !llvm.ptr -> i32
  %t11 = arith.muli %c100_i32, %t10 : i32
  %t12 = llvm.load %i.27 : !llvm.ptr -> i32
  %t13 = arith.subi %t12, %c-2147483648_i32 : i32
  %t14 = arith.addi %t11, %t13 : i32
  llvm.store %t14, %retval : i32, !llvm.ptr
  cf.br ^L31
^L31:
  %t15 = llvm.load %retval : !llvm.ptr -> i32
  func.return %t15 : i32
}
func.func @"down3"(%a0: i32) -> i32 {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %c3_i32 = arith.constant 3 : i32
  %c-5_i32 = arith.constant -5 : i32
  %c100_i32 = arith.constant 100 : i32
  %retval = llvm.alloca %c1_i32 x i32 : (i32) -> !llvm.ptr
  %n.29 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %a0, %n.29 : i32, !llvm.ptr
  %i.30 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %s.31 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %c0_i32, %s.31 : i32, !llvm.ptr
  %t1 = llvm.load %n.29 : !llvm.ptr -> i32
  llvm.store %t1, %i.30 : i32, !llvm.ptr
  cf.br ^L40
^L37:
  %t2 = llvm.load %s.31 : !llvm.ptr -> i32
  %t3 = llvm.load %i.30 : !llvm.ptr -> i32
  %t4 = arith.addi %t2, %t3 : i32
  llvm.store %t4, %s.31 : i32, !llvm.ptr
  cf.br ^L38
^L38:
  %t5 = llvm.load %i.30 : !llvm.ptr -> i32
  %t6 = arith.subi %t5, %c3_i32 : i32
  llvm.store %t6, %i.30 : i32, !llvm.ptr
  cf.br ^L40
^L40:
  %t7 = llvm.load %i.30 : !llvm.ptr -> i32
  %t8 = arith.cmpi sge, %t7, %c-5_i32 : i32
  cf.cond_br %t8, ^L37, ^b1
^b1:
  %t9 = llvm.load %s.31 : !llvm.ptr -> i32
  %t10 = arith.muli %c100_i32, %t9 : i32
  %t11 = llvm.load %i.30 : !llvm.ptr -> i32
  %t12 = arith.addi %t10, %t11 : i32
  llvm.store %t12, %retval : i32, !llvm.ptr
  cf.br ^L36
^L36:
  %t13 = llvm.load %retval : !llvm.ptr -> i32
  func.return %t13 : i32
}
func.func @"udown"(%a0: i32) -> i32 {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %c100_i32 = arith.constant 100 : i32
  %retval = llvm.alloca %c1_i32 x i32 : (i32) -> !llvm.ptr
  %n.32 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %a0, %n.32 : i32, !llvm.ptr
  %u.33 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %s.34 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %c0_i32, %s.34 : i32, !llvm.ptr
  %t1 = llvm.load %n.32 : !llvm.ptr -> i32
  llvm.store %t1, %u.33 : i32, !llvm.ptr
  cf.br ^L45
^L42:
  %t2 = llvm.load %s.34 : !llvm.ptr -> i32
  %t3 = llvm.load %u.33 : !llvm.ptr -> i32
  %t4 = arith.addi %t2, %t3 : i32
  llvm.store %t4, %s.34 : i32, !llvm.ptr
  cf.br ^L43
^L43:
  %t5 = llvm.load %u.33 : !llvm.ptr -> i32
  %t6 = arith.subi %t5, %c1_i32 : i32
  llvm.store %t6, %u.33 : i32, !llvm.ptr
  cf.br ^L45
^L45:
  %t7 = llvm.load %u.33 : !llvm.ptr -> i32
  %t8 = arith.cmpi ne, %t7, %c0_i32 : i32
  cf.cond_br %t8, ^L42, ^b1
^b1:
  %t9 = llvm.load %s.34 : !llvm.ptr -> i32
  %t10 = arith.muli %c100_i32, %t9 : i32
  %t11 = llvm.load %u.33 : !llvm.ptr -> i32
  %t12 = arith.addi %t10, %t11 : i32
  llvm.store %t12, %retval : i32, !llvm.ptr
  cf.br ^L41
^L41:
  %t13 = llvm.load %retval : !llvm.ptr -> i32
  func.return %t13 : i32
}
func.func @"ne_up"(%a0: !llvm.ptr) -> i32 {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %c3_i32 = arith.constant 3 : i32
  %c2_i64 = arith.constant 2 : i64
  %c7_i32 = arith.constant 7 : i32
  %c100_i32 = arith.constant 100 : i32
  %retval = llvm.alloca %c1_i32 x i32 : (i32) -> !llvm.ptr
  %x.35 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  llvm.store %a0, %x.35 : !llvm.ptr, !llvm.ptr
  %x.36 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  %i.37 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %s.38 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %t1 = llvm.load %x.35 : !llvm.ptr -> !llvm.ptr
  llvm.store %t1, %x.36 : !llvm.ptr, !llvm.ptr
  llvm.store %c0_i32, %s.38 : i32, !llvm.ptr
  llvm.store %c3_i32, %i.37 : i32, !llvm.ptr
  cf.br ^L47
^L47:
  %t2 = llvm.load %s.38 : !llvm.ptr -> i32
  %t4 = llvm.load %x.36 : !llvm.ptr -> !llvm.ptr
  %t5 = llvm.load %i.37 : !llvm.ptr -> i32
  %t6 = arith.extsi %t5 : i32 to i64
  %t7 = arith.shli %t6, %c2_i64 : i64
  %t3 = llvm.getelementptr %t4[%t7] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t8 = llvm.load %t3 : !llvm.ptr -> i32
  %t9 = arith.addi %t2, %t8 : i32
  llvm.store %t9, %s.38 : i32, !llvm.ptr
  cf.br ^L48
^L48:
  %t10 = llvm.load %i.37 : !llvm.ptr -> i32
  %t11 = arith.addi %t10, %c1_i32 : i32
  llvm.store %t11, %i.37 : i32, !llvm.ptr
  %t12 = llvm.load %i.37 : !llvm.ptr -> i32
  %t13 = arith.cmpi ne, %t12, %c7_i32 : i32
  cf.cond_br %t13, ^L47, ^b1
^b1:
  %t14 = llvm.load %s.38 : !llvm.ptr -> i32
  %t15 = arith.muli %c100_i32, %t14 : i32
  %t16 = llvm.load %i.37 : !llvm.ptr -> i32
  %t17 = arith.addi %t15, %t16 : i32
  llvm.store %t17, %retval : i32, !llvm.ptr
  cf.br ^L46
^L46:
  %t18 = llvm.load %retval : !llvm.ptr -> i32
  func.return %t18 : i32
}
func.func @"ne_down"(%a0: !llvm.ptr) -> i32 {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %c7_i32 = arith.constant 7 : i32
  %c2_i64 = arith.constant 2 : i64
  %c2_i32 = arith.constant 2 : i32
  %c100_i32 = arith.constant 100 : i32
  %retval = llvm.alloca %c1_i32 x i32 : (i32) -> !llvm.ptr
  %x.39 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  llvm.store %a0, %x.39 : !llvm.ptr, !llvm.ptr
  %x.40 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  %i.41 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %s.42 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %t1 = llvm.load %x.39 : !llvm.ptr -> !llvm.ptr
  llvm.store %t1, %x.40 : !llvm.ptr, !llvm.ptr
  llvm.store %c0_i32, %s.42 : i32, !llvm.ptr
  llvm.store %c7_i32, %i.41 : i32, !llvm.ptr
  cf.br ^L52
^L52:
  %t2 = llvm.load %s.42 : !llvm.ptr -> i32
  %t4 = llvm.load %x.40 : !llvm.ptr -> !llvm.ptr
  %t5 = llvm.load %i.41 : !llvm.ptr -> i32
  %t6 = arith.extsi %t5 : i32 to i64
  %t7 = arith.shli %t6, %c2_i64 : i64
  %t3 = llvm.getelementptr %t4[%t7] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t8 = llvm.load %t3 : !llvm.ptr -> i32
  %t9 = arith.muli %t8, %t5 : i32
  %t10 = arith.addi %t2, %t9 : i32
  llvm.store %t10, %s.42 : i32, !llvm.ptr
  cf.br ^L53
^L53:
  %t11 = llvm.load %i.41 : !llvm.ptr -> i32
  %t12 = arith.subi %t11, %c1_i32 : i32
  llvm.store %t12, %i.41 : i32, !llvm.ptr
  %t13 = llvm.load %i.41 : !llvm.ptr -> i32
  %t14 = arith.cmpi ne, %t13, %c2_i32 : i32
  cf.cond_br %t14, ^L52, ^b1
^b1:
  %t15 = llvm.load %s.42 : !llvm.ptr -> i32
  %t16 = arith.muli %c100_i32, %t15 : i32
  %t17 = llvm.load %i.41 : !llvm.ptr -> i32
  %t18 = arith.addi %t16, %t17 : i32
  llvm.store %t18, %retval : i32, !llvm.ptr
  cf.br ^L51
^L51:
  %t19 = llvm.load %retval : !llvm.ptr -> i32
  func.return %t19 : i32
}
func.func @"ne_cross"() -> i32 {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %c2147483645_i32 = arith.constant 2147483645 : i32
  %c255_i32 = arith.constant 255 : i32
  %c2147483650_i64 = arith.constant 2147483650 : i64
  %c1000_i32 = arith.constant 1000 : i32
  %retval = llvm.alloca %c1_i32 x i32 : (i32) -> !llvm.ptr
  %u.43 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %s.44 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %c0_i32, %s.44 : i32, !llvm.ptr
  llvm.store %c2147483645_i32, %u.43 : i32, !llvm.ptr
  cf.br ^L60
^L57:
  %t1 = llvm.load %s.44 : !llvm.ptr -> i32
  %t2 = llvm.load %u.43 : !llvm.ptr -> i32
  %t3 = arith.andi %t2, %c255_i32 : i32
  %t4 = arith.addi %t1, %t3 : i32
  llvm.store %t4, %s.44 : i32, !llvm.ptr
  cf.br ^L58
^L58:
  %t5 = llvm.load %u.43 : !llvm.ptr -> i32
  %t6 = arith.addi %t5, %c1_i32 : i32
  llvm.store %t6, %u.43 : i32, !llvm.ptr
  cf.br ^L60
^L60:
  %t7 = llvm.load %u.43 : !llvm.ptr -> i32
  %t8 = arith.extui %t7 : i32 to i64
  %t9 = arith.cmpi ne, %t8, %c2147483650_i64 : i64
  cf.cond_br %t9, ^L57, ^b1
^b1:
  %t10 = llvm.load %s.44 : !llvm.ptr -> i32
  %t11 = arith.muli %c1000_i32, %t10 : i32
  %t12 = llvm.load %u.43 : !llvm.ptr -> i32
  %t13 = arith.andi %t12, %c255_i32 : i32
  %t14 = arith.addi %t11, %t13 : i32
  llvm.store %t14, %retval : i32, !llvm.ptr
  cf.br ^L56
^L56:
  %t15 = llvm.load %retval : !llvm.ptr -> i32
  func.return %t15 : i32
}
func.func @"none"(%a0: !llvm.ptr) -> i32 {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %c10_i32 = arith.constant 10 : i32
  %c2_i64 = arith.constant 2 : i64
  %c5_i32 = arith.constant 5 : i32
  %c100_i32 = arith.constant 100 : i32
  %retval = llvm.alloca %c1_i32 x i32 : (i32) -> !llvm.ptr
  %x.45 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  llvm.store %a0, %x.45 : !llvm.ptr, !llvm.ptr
  %x.46 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  %i.47 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %s.48 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %t1 = llvm.load %x.45 : !llvm.ptr -> !llvm.ptr
  llvm.store %t1, %x.46 : !llvm.ptr, !llvm.ptr
  llvm.store %c0_i32, %s.48 : i32, !llvm.ptr
  llvm.store %c10_i32, %i.47 : i32, !llvm.ptr
  cf.br ^L65
^L62:
  %t2 = llvm.load %s.48 : !llvm.ptr -> i32
  %t4 = llvm.load %x.46 : !llvm.ptr -> !llvm.ptr
  %t5 = llvm.load %i.47 : !llvm.ptr -> i32
  %t6 = arith.extsi %t5 : i32 to i64
  %t7 = arith.shli %t6, %c2_i64 : i64
  %t3 = llvm.getelementptr %t4[%t7] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t8 = llvm.load %t3 : !llvm.ptr -> i32
  %t9 = arith.addi %t2, %t8 : i32
  llvm.store %t9, %s.48 : i32, !llvm.ptr
  cf.br ^L63
^L63:
  %t10 = llvm.load %i.47 : !llvm.ptr -> i32
  %t11 = arith.addi %t10, %c1_i32 : i32
  llvm.store %t11, %i.47 : i32, !llvm.ptr
  cf.br ^L65
^L65:
  %t12 = llvm.load %i.47 : !llvm.ptr -> i32
  %t13 = arith.cmpi slt, %t12, %c5_i32 : i32
  cf.cond_br %t13, ^L62, ^b1
^b1:
  %t14 = llvm.load %s.48 : !llvm.ptr -> i32
  %t15 = arith.muli %c100_i32, %t14 : i32
  %t16 = llvm.load %i.47 : !llvm.ptr -> i32
  %t17 = arith.addi %t15, %t16 : i32
  llvm.store %t17, %retval : i32, !llvm.ptr
  cf.br ^L61
^L61:
  %t18 = llvm.load %retval : !llvm.ptr -> i32
  func.return %t18 : i32
}
func.func @"ne_cross2"() -> i32 {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %c2147483645_i32 = arith.constant 2147483645 : i32
  %c255_i32 = arith.constant 255 : i32
  %c-2147483646_i32 = arith.constant -2147483646 : i32
  %c1000_i32 = arith.constant 1000 : i32
  %retval = llvm.alloca %c1_i32 x i32 : (i32) -> !llvm.ptr
  %u.49 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %s.50 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %c0_i32, %s.50 : i32, !llvm.ptr
  llvm.store %c2147483645_i32, %u.49 : i32, !llvm.ptr
  cf.br ^L70
^L67:
  %t1 = llvm.load %s.50 : !llvm.ptr -> i32
  %t2 = llvm.load %u.49 : !llvm.ptr -> i32
  %t3 = arith.andi %t2, %c255_i32 : i32
  %t4 = arith.addi %t1, %t3 : i32
  llvm.store %t4, %s.50 : i32, !llvm.ptr
  cf.br ^L68
^L68:
  %t5 = llvm.load %u.49 : !llvm.ptr -> i32
  %t6 = arith.addi %t5, %c1_i32 : i32
  llvm.store %t6, %u.49 : i32, !llvm.ptr
  cf.br ^L70
^L70:
  %t7 = llvm.load %u.49 : !llvm.ptr -> i32
  %t8 = arith.cmpi ne, %t7, %c-2147483646_i32 : i32
  cf.cond_br %t8, ^L67, ^b1
^b1:
  %t9 = llvm.load %s.50 : !llvm.ptr -> i32
  %t10 = arith.muli %c1000_i32, %t9 : i32
  %t11 = llvm.load %u.49 : !llvm.ptr -> i32
  %t12 = arith.andi %t11, %c255_i32 : i32
  %t13 = arith.addi %t10, %t12 : i32
  llvm.store %t13, %retval : i32, !llvm.ptr
  cf.br ^L66
^L66:
  %t14 = llvm.load %retval : !llvm.ptr -> i32
  func.return %t14 : i32
}
}
