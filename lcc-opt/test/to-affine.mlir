// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// RUN: lcc-opt --lcc-to-affine --split-input-file %s | FileCheck %s
// RUN: lcc-opt --lcc-to-affine=remarks=true --split-input-file \
// RUN:   --mlir-print-op-on-diagnostic=false %s 2>&1 >/dev/null \
// RUN:   | FileCheck %s --check-prefix=REMARK
//
// lcc-to-affine raises lcc's loop nests on global arrays to affine.  The
// inputs are what lcc cfb2383 gives for the C files in the comments, made
// in the top directory of lcc with
//
//   make -r BUILDDIR=$B HOSTFILE=etc/mlir-x86_64-linux.c rcc lcc cpp headers
//   LCCDIR=$B $B/lcc -S convfix.c convw.c
//
// and, for the second input, likewise with etc/mlir-armhf-linux.c.
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
// The arrays become 1-D memref.global, bias of i32 (its loads are), the
// others of i8, and the three loops an affine nest whose accesses are at
// the row-major indices of the C arrays: p*16+i, o*16+i, p*32+o and o.

// CHECK-LABEL: func.func @conv()
//   CHECK-DAG:   %[[BIAS:.*]] = memref.get_global @bias : memref<32xi32>
//   CHECK-DAG:   %[[W:.*]] = memref.get_global @w : memref<512xi8>
//   CHECK-DAG:   %[[IN:.*]] = memref.get_global @in : memref<1024xi8>
//   CHECK-DAG:   %[[OUT:.*]] = memref.get_global @out : memref<2048xi8>
//       CHECK:   affine.for %[[P:.*]] = 0 to 64 {
//  CHECK-NEXT:     affine.for %[[O:.*]] = 0 to 32 {
//  CHECK-NEXT:       %[[B:.*]] = affine.load %[[BIAS]][%[[O]]] : memref<32xi32>
//  CHECK-NEXT:       %[[ACC:.*]] = affine.for %[[I:.*]] = 0 to 16 iter_args(%[[A:.*]] = %[[B]]) -> (i32) {
//  CHECK-NEXT:         %[[WV:.*]] = affine.load %[[W]][%[[O]] * 16 + %[[I]]] : memref<512xi8>
//  CHECK-NEXT:         %[[WE:.*]] = arith.extsi %[[WV]] : i8 to i32
//  CHECK-NEXT:         %[[IV:.*]] = affine.load %[[IN]][%[[P]] * 16 + %[[I]]] : memref<1024xi8>
//  CHECK-NEXT:         %[[IE:.*]] = arith.extsi %[[IV]] : i8 to i32
//  CHECK-NEXT:         %[[M:.*]] = arith.muli %[[WE]], %[[IE]] : i32
//  CHECK-NEXT:         %[[S:.*]] = arith.addi %[[A]], %[[M]] : i32
//  CHECK-NEXT:         affine.yield %[[S]] : i32
//  CHECK-NEXT:       }
//  CHECK-NEXT:       %[[LO:.*]] = arith.maxsi %[[ACC]], %{{.*}} : i32
//  CHECK-NEXT:       %[[HI:.*]] = arith.minsi %[[LO]], %{{.*}} : i32
//  CHECK-NEXT:       %[[T:.*]] = arith.trunci %[[HI]] : i32 to i8
//  CHECK-NEXT:       affine.store %[[T]], %[[OUT]][%[[P]] * 32 + %[[O]]] : memref<2048xi8>
//  CHECK-NEXT:     }
//  CHECK-NEXT:   }
//  CHECK-NEXT:   return
//   CHECK-DAG: memref.global @bias : memref<32xi32> = dense<0> {alignment = 16 : i64}
//   CHECK-DAG: memref.global @out : memref<2048xi8> = dense<0> {alignment = 16 : i64}
//   CHECK-DAG: memref.global @w : memref<512xi8> = dense<0> {alignment = 16 : i64}
//   CHECK-DAG: memref.global @in : memref<1024xi8> = dense<0> {alignment = 16 : i64}
//   CHECK-NOT: {{llvm\.(load|store|getelementptr|mlir)}}
// REMARK: remark: lcc-globals-to-memref: @bias becomes 'memref<32xi32>'
// REMARK: remark: lcc-globals-to-memref: @out becomes 'memref<2048xi8>'
// REMARK: remark: lcc-globals-to-memref: @w becomes 'memref<512xi8>'
// REMARK: remark: lcc-globals-to-memref: @in becomes 'memref<1024xi8>'
// REMARK: remark: lcc-scf-to-affine: raised a nest of 3 loops

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

// convfix.c for mlir/armhf-linux: lcc scales the indices in i32 before
// extending them, which wraps for no value of these loops; the nest is the
// same.  The 32-bit target aligns only bias.

// CHECK-LABEL: func.func @conv()
//       CHECK:   affine.for %[[P:.*]] = 0 to 64 {
//  CHECK-NEXT:     affine.for %[[O:.*]] = 0 to 32 {
//  CHECK-NEXT:       affine.load %{{.*}}[%[[O]]] : memref<32xi32>
//  CHECK-NEXT:       affine.for %[[I:.*]] = 0 to 16 iter_args
//  CHECK-NEXT:         affine.load %{{.*}}[%[[O]] * 16 + %[[I]]] : memref<512xi8>
//       CHECK:         affine.load %{{.*}}[%[[P]] * 16 + %[[I]]] : memref<1024xi8>
//       CHECK:       affine.store %{{.*}}, %{{.*}}[%[[P]] * 32 + %[[O]]] : memref<2048xi8>
//   CHECK-DAG: memref.global @bias : memref<32xi32> = dense<0> {alignment = 4 : i64}
//   CHECK-DAG: memref.global @in : memref<1024xi8> = dense<0> {alignment = 1 : i64}
//   CHECK-NOT: {{llvm\.(load|store|getelementptr|mlir)}}
// REMARK: remark: lcc-scf-to-affine: raised a nest of 3 loops

module attributes {llvm.data_layout = "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64", llvm.target_triple = "armv7-unknown-linux-gnueabihf"} {
func.func @"conv"() {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %bias.addr = llvm.mlir.addressof @"bias" : !llvm.ptr
  %c2_i32 = arith.constant 2 : i32
  %w.addr = llvm.mlir.addressof @"w" : !llvm.ptr
  %c4_i32 = arith.constant 4 : i32
  %in.addr = llvm.mlir.addressof @"in" : !llvm.ptr
  %c16_i32 = arith.constant 16 : i32
  %c-128_i32 = arith.constant -128 : i32
  %c127_i32 = arith.constant 127 : i32
  %out.addr = llvm.mlir.addressof @"out" : !llvm.ptr
  %c5_i32 = arith.constant 5 : i32
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
  %t3 = arith.shli %t2, %c2_i32 : i32
  %t4 = arith.extsi %t3 : i32 to i64
  %t1 = llvm.getelementptr %bias.addr[%t4] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t5 = llvm.load %t1 : !llvm.ptr -> i32
  llvm.store %t5, %acc.4 : i32, !llvm.ptr
  llvm.store %c0_i32, %i.1 : i32, !llvm.ptr
  cf.br ^L10
^L10:
  %t6 = llvm.load %acc.4 : !llvm.ptr -> i32
  %t9 = llvm.load %o.2 : !llvm.ptr -> i32
  %t10 = arith.shli %t9, %c4_i32 : i32
  %t11 = arith.extsi %t10 : i32 to i64
  %t8 = llvm.getelementptr %w.addr[%t11] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t12 = llvm.load %i.1 : !llvm.ptr -> i32
  %t13 = arith.extsi %t12 : i32 to i64
  %t7 = llvm.getelementptr %t8[%t13] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t14 = llvm.load %t7 : !llvm.ptr -> i8
  %t15 = arith.extsi %t14 : i8 to i32
  %t18 = llvm.load %p.3 : !llvm.ptr -> i32
  %t19 = arith.shli %t18, %c4_i32 : i32
  %t20 = arith.extsi %t19 : i32 to i64
  %t17 = llvm.getelementptr %in.addr[%t20] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t21 = arith.extsi %t12 : i32 to i64
  %t16 = llvm.getelementptr %t17[%t21] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t22 = llvm.load %t16 : !llvm.ptr -> i8
  %t23 = arith.extsi %t22 : i8 to i32
  %t24 = arith.muli %t15, %t23 : i32
  %t25 = arith.addi %t6, %t24 : i32
  llvm.store %t25, %acc.4 : i32, !llvm.ptr
  cf.br ^L11
^L11:
  %t26 = llvm.load %i.1 : !llvm.ptr -> i32
  %t27 = arith.addi %t26, %c1_i32 : i32
  llvm.store %t27, %i.1 : i32, !llvm.ptr
  %t28 = llvm.load %i.1 : !llvm.ptr -> i32
  %t29 = arith.cmpi slt, %t28, %c16_i32 : i32
  cf.cond_br %t29, ^L10, ^b1
^b1:
  %t30 = llvm.load %acc.4 : !llvm.ptr -> i32
  %t31 = arith.cmpi sge, %t30, %c-128_i32 : i32
  cf.cond_br %t31, ^L14, ^b2
^b2:
  llvm.store %c-128_i32, %acc.4 : i32, !llvm.ptr
  cf.br ^L14
^L14:
  %t32 = llvm.load %acc.4 : !llvm.ptr -> i32
  %t33 = arith.cmpi sle, %t32, %c127_i32 : i32
  cf.cond_br %t33, ^L16, ^b3
^b3:
  llvm.store %c127_i32, %acc.4 : i32, !llvm.ptr
  cf.br ^L16
^L16:
  %t36 = llvm.load %p.3 : !llvm.ptr -> i32
  %t37 = arith.shli %t36, %c5_i32 : i32
  %t38 = arith.extsi %t37 : i32 to i64
  %t35 = llvm.getelementptr %out.addr[%t38] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t39 = llvm.load %o.2 : !llvm.ptr -> i32
  %t40 = arith.extsi %t39 : i32 to i64
  %t34 = llvm.getelementptr %t35[%t40] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t41 = llvm.load %acc.4 : !llvm.ptr -> i32
  %t42 = arith.trunci %t41 : i32 to i8
  llvm.store %t42, %t34 : i8, !llvm.ptr
  cf.br ^L7
^L7:
  %t43 = llvm.load %o.2 : !llvm.ptr -> i32
  %t44 = arith.addi %t43, %c1_i32 : i32
  llvm.store %t44, %o.2 : i32, !llvm.ptr
  %t45 = llvm.load %o.2 : !llvm.ptr -> i32
  %t46 = arith.cmpi slt, %t45, %c32_i32 : i32
  cf.cond_br %t46, ^L6, ^b4
^b4:
  cf.br ^L3
^L3:
  %t47 = llvm.load %p.3 : !llvm.ptr -> i32
  %t48 = arith.addi %t47, %c1_i32 : i32
  llvm.store %t48, %p.3 : i32, !llvm.ptr
  %t49 = llvm.load %p.3 : !llvm.ptr -> i32
  %t50 = arith.cmpi slt, %t49, %c64_i32 : i32
  cf.cond_br %t50, ^L2, ^b5
^b5:
  cf.br ^L1
^L1:
  func.return
}
llvm.mlir.global external @"bias"() {alignment = 4 : i64} : !llvm.array<128 x i8> {
  %v1 = llvm.mlir.zero : !llvm.array<128 x i8>
  llvm.return %v1 : !llvm.array<128 x i8>
}
llvm.mlir.global external @"out"() {alignment = 1 : i64} : !llvm.array<2048 x i8> {
  %v1 = llvm.mlir.zero : !llvm.array<2048 x i8>
  llvm.return %v1 : !llvm.array<2048 x i8>
}
llvm.mlir.global external @"w"() {alignment = 1 : i64} : !llvm.array<512 x i8> {
  %v1 = llvm.mlir.zero : !llvm.array<512 x i8>
  llvm.return %v1 : !llvm.array<512 x i8>
}
llvm.mlir.global external @"in"() {alignment = 1 : i64} : !llvm.array<1024 x i8> {
  %v1 = llvm.mlir.zero : !llvm.array<1024 x i8>
  llvm.return %v1 : !llvm.array<1024 x i8>
}
}

// -----

// convw.c, convfix.c with constant tables, smaller:
//
//   #define HW 8
//   #define IC 8
//   #define OC 4
//   signed char in[HW][IC], out[HW][OC];
//   static const signed char w[OC][IC] = {
//   	{ 1, -2, 3, -4, 5, -6, 7, -8 },
//   	{ -128, 127, 0, -1, 2, -3, 4, -5 },
//   	{ 9, -10, 11, -12, 13, -14, 15, -16 },
//   	{ 17, 18, -19, 20, -21, 22, -23, 24 }
//   };
//   static const int bias[OC] = { -300, 0, 70000, -1 };
//
// with the function conv of convfix.c.  The tables become private constant
// memref.global with their values.

//       CHECK: memref.global "private" constant @w : memref<32xi8> = dense<[1, -2, 3, -4, 5, -6, 7, -8, -128, 127, 0, -1, 2, -3, 4, -5, 9, -10, 11, -12, 13, -14, 15, -16, 17, 18, -19, 20, -21, 22, -23, 24]> {alignment = 16 : i64}
//       CHECK: memref.global "private" constant @bias : memref<4xi32> = dense<[-300, 0, 70000, -1]> {alignment = 16 : i64}
// CHECK-LABEL: func.func @conv()
//       CHECK:   affine.for %[[P:.*]] = 0 to 8 {
//  CHECK-NEXT:     affine.for %[[O:.*]] = 0 to 4 {
//  CHECK-NEXT:       affine.load %{{.*}}[%[[O]]] : memref<4xi32>
//  CHECK-NEXT:       affine.for %[[I:.*]] = 0 to 8 iter_args
//  CHECK-NEXT:         affine.load %{{.*}}[%[[O]] * 8 + %[[I]]] : memref<32xi8>
//       CHECK:         affine.load %{{.*}}[%[[P]] * 8 + %[[I]]] : memref<64xi8>
//       CHECK:       affine.store %{{.*}}, %{{.*}}[%[[P]] * 4 + %[[O]]] : memref<32xi8>
//   CHECK-NOT: {{llvm\.(load|store|getelementptr|mlir)}}
// REMARK: remark: lcc-globals-to-memref: @w becomes 'memref<32xi8>'
// REMARK: remark: lcc-globals-to-memref: @bias becomes 'memref<4xi32>'
// REMARK: remark: lcc-scf-to-affine: raised a nest of 3 loops

module attributes {llvm.data_layout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128", llvm.target_triple = "x86_64-pc-linux-gnu"} {
llvm.mlir.global internal constant @"w"("\01\FE\03\FC\05\FA\07\F8\80\7F\00\FF\02\FD\04\FB\09\F6\0B\F4\0D\F2\0F\F0\11\12\ED\14\EB\16\E9\18") {alignment = 16 : i64} : !llvm.array<32 x i8>
llvm.mlir.global internal constant @"bias"("\D4\FE\FF\FF\00\00\00\00p\11\01\00\FF\FF\FF\FF") {alignment = 16 : i64} : !llvm.array<16 x i8>
func.func @"conv"() {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %bias.addr = llvm.mlir.addressof @"bias" : !llvm.ptr
  %c2_i64 = arith.constant 2 : i64
  %w.addr = llvm.mlir.addressof @"w" : !llvm.ptr
  %c3_i64 = arith.constant 3 : i64
  %in.addr = llvm.mlir.addressof @"in" : !llvm.ptr
  %c8_i32 = arith.constant 8 : i32
  %c-128_i32 = arith.constant -128 : i32
  %c127_i32 = arith.constant 127 : i32
  %out.addr = llvm.mlir.addressof @"out" : !llvm.ptr
  %c4_i32 = arith.constant 4 : i32
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
  %t11 = arith.shli %t10, %c3_i64 : i64
  %t8 = llvm.getelementptr %w.addr[%t11] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t12 = llvm.load %i.1 : !llvm.ptr -> i32
  %t13 = arith.extsi %t12 : i32 to i64
  %t7 = llvm.getelementptr %t8[%t13] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t14 = llvm.load %t7 : !llvm.ptr -> i8
  %t15 = arith.extsi %t14 : i8 to i32
  %t18 = llvm.load %p.3 : !llvm.ptr -> i32
  %t19 = arith.extsi %t18 : i32 to i64
  %t20 = arith.shli %t19, %c3_i64 : i64
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
  %t28 = arith.cmpi slt, %t27, %c8_i32 : i32
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
  %t37 = arith.shli %t36, %c2_i64 : i64
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
  %t45 = arith.cmpi slt, %t44, %c4_i32 : i32
  cf.cond_br %t45, ^L6, ^b4
^b4:
  cf.br ^L3
^L3:
  %t46 = llvm.load %p.3 : !llvm.ptr -> i32
  %t47 = arith.addi %t46, %c1_i32 : i32
  llvm.store %t47, %p.3 : i32, !llvm.ptr
  %t48 = llvm.load %p.3 : !llvm.ptr -> i32
  %t49 = arith.cmpi slt, %t48, %c8_i32 : i32
  cf.cond_br %t49, ^L2, ^b5
^b5:
  cf.br ^L1
^L1:
  func.return
}
llvm.mlir.global external @"out"() {alignment = 16 : i64} : !llvm.array<32 x i8> {
  %v1 = llvm.mlir.zero : !llvm.array<32 x i8>
  llvm.return %v1 : !llvm.array<32 x i8>
}
llvm.mlir.global external @"in"() {alignment = 16 : i64} : !llvm.array<64 x i8> {
  %v1 = llvm.mlir.zero : !llvm.array<64 x i8>
  llvm.return %v1 : !llvm.array<64 x i8>
}
}
