// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// RUN: lcc-opt --lcc-to-linalg --split-input-file %s | FileCheck %s
// RUN: lcc-opt --lcc-to-linalg=remarks=true --split-input-file \
// RUN:   --mlir-print-op-on-diagnostic=false %s 2>&1 >/dev/null \
// RUN:   | FileCheck %s --check-prefix=REMARK
//
// lcc-to-linalg raises lcc's loop nests on global arrays to linalg.  The
// inputs are what lcc d9d291c gives for the C files in the comments, made
// in the top directory of lcc with
//
//   make -r BUILDDIR=$B HOSTFILE=etc/mlir-x86_64-linux.c rcc lcc cpp headers
//   LCCDIR=$B $B/lcc -S convfix.c convw.c convff.c mm.c conv3.c
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
// The arrays get views of their C shapes, the sum a buffer of 64x32 i32:
// the bias is broadcast into it, the products are added to it by a matmul
// (whose operands specialize-generic-ops puts in the order of the C
// product, w then in, with the dimensions of o, p and i), and the clamp is
// an element-wise linalg.generic into out.

//   CHECK-DAG: #[[$A:.*]] = affine_map<(d0, d1, d2) -> (d0, d2)>
//   CHECK-DAG: #[[$B:.*]] = affine_map<(d0, d1, d2) -> (d1, d2)>
//   CHECK-DAG: #[[$C:.*]] = affine_map<(d0, d1, d2) -> (d1, d0)>
//   CHECK-DAG: #[[$E:.*]] = affine_map<(d0, d1) -> (d0, d1)>
// CHECK-LABEL: func.func @conv()
//   CHECK-DAG:   %[[BUF:.*]] = memref.alloca() : memref<64x32xi32>
//   CHECK-DAG:   %[[W:.*]] = memref.expand_shape %{{.*}} {{\[}}[0, 1]] output_shape [32, 16] : memref<512xi8> into memref<32x16xi8>
//   CHECK-DAG:   %[[IN:.*]] = memref.expand_shape %{{.*}} {{\[}}[0, 1]] output_shape [64, 16] : memref<1024xi8> into memref<64x16xi8>
//   CHECK-DAG:   %[[OUT:.*]] = memref.expand_shape %{{.*}} {{\[}}[0, 1]] output_shape [64, 32] : memref<2048xi8> into memref<64x32xi8>
//   CHECK-DAG:   %[[BIAS:.*]] = memref.get_global @bias : memref<32xi32>
//       CHECK:   linalg.broadcast ins(%[[BIAS]] : memref<32xi32>) outs(%[[BUF]] : memref<64x32xi32>) dimensions = [0]
//  CHECK-NEXT:   linalg.matmul indexing_maps = [#[[$A]], #[[$B]], #[[$C]]] ins(%[[W]], %[[IN]] : memref<32x16xi8>, memref<64x16xi8>) outs(%[[BUF]] : memref<64x32xi32>)
//  CHECK-NEXT:   linalg.generic {indexing_maps = [#[[$E]], #[[$E]]], iterator_types = ["parallel", "parallel"]} ins(%[[BUF]] : memref<64x32xi32>) outs(%[[OUT]] : memref<64x32xi8>) {
//  CHECK-NEXT:   ^bb0(%[[X:.*]]: i32, %{{.*}}: i8):
//  CHECK-NEXT:     %[[LO:.*]] = arith.maxsi %[[X]], %{{.*}} : i32
//  CHECK-NEXT:     %[[HI:.*]] = arith.minsi %[[LO]], %{{.*}} : i32
//  CHECK-NEXT:     %[[T:.*]] = arith.trunci %[[HI]] : i32 to i8
//  CHECK-NEXT:     linalg.yield %[[T]] : i8
//  CHECK-NEXT:   }
//  CHECK-NEXT:   return
//   CHECK-NOT: {{affine\.|llvm\.(load|store|getelementptr)}}
// REMARK: remark: lcc-scf-to-affine: raised a nest of 3 loops
// REMARK: remark: lcc-expand-shapes: @w becomes 'memref<32x16xi8>' in @conv
// REMARK: remark: lcc-expand-shapes: @in becomes 'memref<64x16xi8>' in @conv
// REMARK: remark: lcc-expand-shapes: @out becomes 'memref<64x32xi8>' in @conv
// REMARK: remark: lcc-distribute-reductions: distributed 2 loops around it with 1 buffer of 8192 bytes
// REMARK: remark: lcc-affine-to-linalg: raised a nest of 2 loops (2 parallel, 0 reduction)
// REMARK: remark: lcc-affine-to-linalg: raised a nest of 3 loops (2 parallel, 1 reduction)
// REMARK: remark: lcc-affine-to-linalg: raised a nest of 2 loops (2 parallel, 0 reduction)

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

// convfix.c for mlir/armhf-linux: the same operations.

// CHECK-LABEL: func.func @conv()
//       CHECK:   linalg.broadcast ins(%{{.*}} : memref<32xi32>) outs(%{{.*}} : memref<64x32xi32>) dimensions = [0]
//  CHECK-NEXT:   linalg.matmul indexing_maps = [#{{.*}}, #{{.*}}, #{{.*}}] ins(%{{.*}}, %{{.*}} : memref<32x16xi8>, memref<64x16xi8>) outs(%{{.*}} : memref<64x32xi32>)
//  CHECK-NEXT:   linalg.generic {{.*}} ins(%{{.*}} : memref<64x32xi32>) outs(%{{.*}} : memref<64x32xi8>)
// REMARK: remark: lcc-distribute-reductions: distributed 2 loops around it with 1 buffer of 8192 bytes
// REMARK-COUNT-3: remark: lcc-affine-to-linalg: raised a nest

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
// The constant tables are operands like the other arrays.

//       CHECK: memref.global "private" constant @w : memref<32xi8> = dense<[1, -2, 3, -4, 5, -6, 7, -8, -128, 127, 0, -1, 2, -3, 4, -5, 9, -10, 11, -12, 13, -14, 15, -16, 17, 18, -19, 20, -21, 22, -23, 24]>
//       CHECK: memref.global "private" constant @bias : memref<4xi32> = dense<[-300, 0, 70000, -1]>
// CHECK-LABEL: func.func @conv()
//   CHECK-DAG:   %[[BUF:.*]] = memref.alloca() : memref<8x4xi32>
//   CHECK-DAG:   %[[W:.*]] = memref.expand_shape %{{.*}} {{\[}}[0, 1]] output_shape [4, 8] : memref<32xi8> into memref<4x8xi8>
//   CHECK-DAG:   %[[BIAS:.*]] = memref.get_global @bias : memref<4xi32>
//       CHECK:   linalg.broadcast ins(%[[BIAS]] : memref<4xi32>) outs(%[[BUF]] : memref<8x4xi32>) dimensions = [0]
//  CHECK-NEXT:   linalg.matmul indexing_maps = [#{{.*}}, #{{.*}}, #{{.*}}] ins(%[[W]], %{{.*}} : memref<4x8xi8>, memref<8x8xi8>) outs(%[[BUF]] : memref<8x4xi32>)
//  CHECK-NEXT:   linalg.generic
// REMARK: remark: lcc-distribute-reductions: distributed 2 loops around it with 1 buffer of 128 bytes
// REMARK-COUNT-3: remark: lcc-affine-to-linalg: raised a nest

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

// -----

// convff.c, convfix.c in float, smaller:
//
//   #define HW 4
//   #define IC 4
//   #define OC 2
//   float in[HW][IC], out[HW][OC];
//   static const float w[OC][IC] = { { -0.0f, 1e-45f, 1.5f, -2.25f }, { 3e38f, -1.75f, 0.5f, 0.125f } };
//   static const float bias[OC] = { -7.0f, 6.5f };
//
//   void conv(void)
//   {
//   	int p, o, i;
//
//   	for (p = 0; p < HW; p++)
//   		for (o = 0; o < OC; o++) {
//   			float acc = bias[o];
//   			for (i = 0; i < IC; i++)
//   				acc += w[o][i] * in[p][i];
//   			if (acc < -128) acc = -128;
//   			if (acc > 127) acc = 127;
//   			out[p][o] = acc;
//   		}
//   }
//
// The float sum is a matmul of f32 too, which adds the products to the
// bias in the order of i when lowered; the clamp compares and selects.

// CHECK-LABEL: func.func @conv()
//       CHECK:   linalg.broadcast ins(%{{.*}} : memref<2xf32>) outs(%{{.*}} : memref<4x2xf32>) dimensions = [0]
//  CHECK-NEXT:   linalg.matmul indexing_maps = [#{{.*}}, #{{.*}}, #{{.*}}] ins(%{{.*}}, %{{.*}} : memref<2x4xf32>, memref<4x4xf32>) outs(%{{.*}} : memref<4x2xf32>)
//  CHECK-NEXT:   linalg.generic {{.*}} ins(%{{.*}} : memref<4x2xf32>) outs(%{{.*}} : memref<4x2xf32>) {
//  CHECK-NEXT:   ^bb0(%[[X:.*]]: f32, %{{.*}}: f32):
//  CHECK-NEXT:     %[[C1:.*]] = arith.cmpf uge, %[[X]], %{{.*}} : f32
//  CHECK-NEXT:     %[[S1:.*]] = arith.select %[[C1]], %[[X]], %{{.*}} : f32
//  CHECK-NEXT:     %[[C2:.*]] = arith.cmpf ule, %[[S1]], %{{.*}} : f32
//  CHECK-NEXT:     %[[S2:.*]] = arith.select %[[C2]], %[[S1]], %{{.*}} : f32
//  CHECK-NEXT:     linalg.yield %[[S2]] : f32
// REMARK: remark: lcc-distribute-reductions: distributed 2 loops around it with 1 buffer of 32 bytes
// REMARK-COUNT-3: remark: lcc-affine-to-linalg: raised a nest

module attributes {llvm.data_layout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128", llvm.target_triple = "x86_64-pc-linux-gnu"} {
llvm.mlir.global internal constant @"w"("\00\00\00\80\01\00\00\00\00\00\C0?\00\00\10\C0\E6\B1a\7F\00\00\E0\BF\00\00\00?\00\00\00>") {alignment = 16 : i64} : !llvm.array<32 x i8>
llvm.mlir.global internal constant @"bias"("\00\00\E0\C0\00\00\D0@") {alignment = 4 : i64} : !llvm.array<8 x i8>
func.func @"conv"() {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %bias.addr = llvm.mlir.addressof @"bias" : !llvm.ptr
  %c2_i64 = arith.constant 2 : i64
  %w.addr = llvm.mlir.addressof @"w" : !llvm.ptr
  %c4_i64 = arith.constant 4 : i64
  %in.addr = llvm.mlir.addressof @"in" : !llvm.ptr
  %c4_i32 = arith.constant 4 : i32
  %cst1 = arith.constant 0xC3000000 : f32
  %cst2 = arith.constant 0x42FE0000 : f32
  %out.addr = llvm.mlir.addressof @"out" : !llvm.ptr
  %c3_i64 = arith.constant 3 : i64
  %c2_i32 = arith.constant 2 : i32
  %i.1 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %o.2 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %p.3 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %acc.4 = llvm.alloca %c1_i32 x f32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
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
  %t5 = llvm.load %t1 : !llvm.ptr -> f32
  llvm.store %t5, %acc.4 : f32, !llvm.ptr
  llvm.store %c0_i32, %i.1 : i32, !llvm.ptr
  cf.br ^L10
^L10:
  %t6 = llvm.load %acc.4 : !llvm.ptr -> f32
  %t9 = llvm.load %o.2 : !llvm.ptr -> i32
  %t10 = arith.extsi %t9 : i32 to i64
  %t11 = arith.shli %t10, %c4_i64 : i64
  %t8 = llvm.getelementptr %w.addr[%t11] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t12 = llvm.load %i.1 : !llvm.ptr -> i32
  %t13 = arith.extsi %t12 : i32 to i64
  %t14 = arith.shli %t13, %c2_i64 : i64
  %t7 = llvm.getelementptr %t8[%t14] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t15 = llvm.load %t7 : !llvm.ptr -> f32
  %t18 = llvm.load %p.3 : !llvm.ptr -> i32
  %t19 = arith.extsi %t18 : i32 to i64
  %t20 = arith.shli %t19, %c4_i64 : i64
  %t17 = llvm.getelementptr %in.addr[%t20] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t16 = llvm.getelementptr %t17[%t14] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t21 = llvm.load %t16 : !llvm.ptr -> f32
  %t22 = arith.mulf %t15, %t21 : f32
  %t23 = arith.addf %t6, %t22 : f32
  llvm.store %t23, %acc.4 : f32, !llvm.ptr
  cf.br ^L11
^L11:
  %t24 = llvm.load %i.1 : !llvm.ptr -> i32
  %t25 = arith.addi %t24, %c1_i32 : i32
  llvm.store %t25, %i.1 : i32, !llvm.ptr
  %t26 = llvm.load %i.1 : !llvm.ptr -> i32
  %t27 = arith.cmpi slt, %t26, %c4_i32 : i32
  cf.cond_br %t27, ^L10, ^b1
^b1:
  %t28 = llvm.load %acc.4 : !llvm.ptr -> f32
  %t29 = arith.cmpf uge, %t28, %cst1 : f32
  cf.cond_br %t29, ^L14, ^b2
^b2:
  llvm.store %cst1, %acc.4 : f32, !llvm.ptr
  cf.br ^L14
^L14:
  %t30 = llvm.load %acc.4 : !llvm.ptr -> f32
  %t31 = arith.cmpf ule, %t30, %cst2 : f32
  cf.cond_br %t31, ^L16, ^b3
^b3:
  llvm.store %cst2, %acc.4 : f32, !llvm.ptr
  cf.br ^L16
^L16:
  %t34 = llvm.load %p.3 : !llvm.ptr -> i32
  %t35 = arith.extsi %t34 : i32 to i64
  %t36 = arith.shli %t35, %c3_i64 : i64
  %t33 = llvm.getelementptr %out.addr[%t36] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t37 = llvm.load %o.2 : !llvm.ptr -> i32
  %t38 = arith.extsi %t37 : i32 to i64
  %t39 = arith.shli %t38, %c2_i64 : i64
  %t32 = llvm.getelementptr %t33[%t39] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t40 = llvm.load %acc.4 : !llvm.ptr -> f32
  llvm.store %t40, %t32 : f32, !llvm.ptr
  cf.br ^L7
^L7:
  %t41 = llvm.load %o.2 : !llvm.ptr -> i32
  %t42 = arith.addi %t41, %c1_i32 : i32
  llvm.store %t42, %o.2 : i32, !llvm.ptr
  %t43 = llvm.load %o.2 : !llvm.ptr -> i32
  %t44 = arith.cmpi slt, %t43, %c2_i32 : i32
  cf.cond_br %t44, ^L6, ^b4
^b4:
  cf.br ^L3
^L3:
  %t45 = llvm.load %p.3 : !llvm.ptr -> i32
  %t46 = arith.addi %t45, %c1_i32 : i32
  llvm.store %t46, %p.3 : i32, !llvm.ptr
  %t47 = llvm.load %p.3 : !llvm.ptr -> i32
  %t48 = arith.cmpi slt, %t47, %c4_i32 : i32
  cf.cond_br %t48, ^L2, ^b5
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

// -----

// mm.c, matrix products that accumulate in memory, in a local variable,
// and in memory after setting the element to 0:
//
//   /* matrix product accumulating in memory, with a local sum, and in memory
//      after setting the element to 0 */
//   #define M 16
//   #define N 8
//   #define K 12
//   int a[M][K], b[K][N], c[M][N], d[M][N], e[M][N];
//
//   void mm(void)
//   {
//   	int i, j, k;
//
//   	for (i = 0; i < M; i++)
//   		for (j = 0; j < N; j++)
//   			for (k = 0; k < K; k++)
//   				c[i][j] += a[i][k] * b[k][j];
//   }
//
//   void mm2(void)
//   {
//   	int i, j, k;
//
//   	for (i = 0; i < M; i++)
//   		for (j = 0; j < N; j++) {
//   			int s = 0;
//   			for (k = 0; k < K; k++)
//   				s += a[i][k] * b[k][j];
//   			d[i][j] = s;
//   		}
//   }
//
//   void mm3(void)
//   {
//   	int i, j, k;
//
//   	for (i = 0; i < M; i++)
//   		for (j = 0; j < N; j++) {
//   			e[i][j] = 0;
//   			for (k = 0; k < K; k++)
//   				e[i][j] += a[i][k] * b[k][j];
//   		}
//   }
//
// The first is a matmul into c; the second fills a buffer with 0, adds the
// products to it and copies it to d; the third fills e with 0 and adds the
// products to it.

// CHECK-LABEL: func.func @mm()
//       CHECK:   linalg.matmul ins(%{{.*}}, %{{.*}} : memref<16x12xi32>, memref<12x8xi32>) outs(%{{.*}} : memref<16x8xi32>)
//  CHECK-NEXT:   return
// CHECK-LABEL: func.func @mm2()
//       CHECK:   %[[BUF:.*]] = memref.alloca() : memref<16x8xi32>
//       CHECK:   linalg.fill ins(%{{.*}} : i32) outs(%[[BUF]] : memref<16x8xi32>)
//  CHECK-NEXT:   linalg.matmul ins(%{{.*}}, %{{.*}} : memref<16x12xi32>, memref<12x8xi32>) outs(%[[BUF]] : memref<16x8xi32>)
//  CHECK-NEXT:   linalg.copy ins(%[[BUF]] : memref<16x8xi32>) outs(%{{.*}} : memref<16x8xi32>)
//  CHECK-NEXT:   return
// CHECK-LABEL: func.func @mm3()
//   CHECK-NOT:   memref.alloca
//       CHECK:   linalg.fill ins(%{{.*}} : i32) outs(%[[E:.*]] : memref<16x8xi32>)
//  CHECK-NEXT:   linalg.matmul ins(%{{.*}}, %{{.*}} : memref<16x12xi32>, memref<12x8xi32>) outs(%[[E]] : memref<16x8xi32>)
//  CHECK-NEXT:   return
// REMARK: remark: lcc-distribute-reductions: distributed 2 loops around it with 1 buffer of 512 bytes
// REMARK: remark: lcc-distribute-reductions: distributed 2 loops around it with 0 buffers of 0 bytes
// REMARK-COUNT-6: remark: lcc-affine-to-linalg: raised a nest

module attributes {llvm.data_layout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128", llvm.target_triple = "x86_64-pc-linux-gnu"} {
func.func @"mm"() {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %c.addr = llvm.mlir.addressof @"c" : !llvm.ptr
  %c5_i64 = arith.constant 5 : i64
  %c2_i64 = arith.constant 2 : i64
  %a.addr = llvm.mlir.addressof @"a" : !llvm.ptr
  %c48_i64 = arith.constant 48 : i64
  %b.addr = llvm.mlir.addressof @"b" : !llvm.ptr
  %c12_i32 = arith.constant 12 : i32
  %c8_i32 = arith.constant 8 : i32
  %c16_i32 = arith.constant 16 : i32
  %k.1 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %j.2 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %i.3 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %c0_i32, %i.3 : i32, !llvm.ptr
  cf.br ^L2
^L2:
  llvm.store %c0_i32, %j.2 : i32, !llvm.ptr
  cf.br ^L6
^L6:
  llvm.store %c0_i32, %k.1 : i32, !llvm.ptr
  cf.br ^L10
^L10:
  %t3 = llvm.load %i.3 : !llvm.ptr -> i32
  %t4 = arith.extsi %t3 : i32 to i64
  %t5 = arith.shli %t4, %c5_i64 : i64
  %t2 = llvm.getelementptr %c.addr[%t5] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t6 = llvm.load %j.2 : !llvm.ptr -> i32
  %t7 = arith.extsi %t6 : i32 to i64
  %t8 = arith.shli %t7, %c2_i64 : i64
  %t1 = llvm.getelementptr %t2[%t8] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t9 = llvm.load %t1 : !llvm.ptr -> i32
  %t12 = arith.muli %c48_i64, %t4 : i64
  %t11 = llvm.getelementptr %a.addr[%t12] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t13 = llvm.load %k.1 : !llvm.ptr -> i32
  %t14 = arith.extsi %t13 : i32 to i64
  %t15 = arith.shli %t14, %c2_i64 : i64
  %t10 = llvm.getelementptr %t11[%t15] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t16 = llvm.load %t10 : !llvm.ptr -> i32
  %t19 = arith.shli %t14, %c5_i64 : i64
  %t18 = llvm.getelementptr %b.addr[%t19] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t17 = llvm.getelementptr %t18[%t8] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t20 = llvm.load %t17 : !llvm.ptr -> i32
  %t21 = arith.muli %t16, %t20 : i32
  %t22 = arith.addi %t9, %t21 : i32
  llvm.store %t22, %t1 : i32, !llvm.ptr
  cf.br ^L11
^L11:
  %t23 = llvm.load %k.1 : !llvm.ptr -> i32
  %t24 = arith.addi %t23, %c1_i32 : i32
  llvm.store %t24, %k.1 : i32, !llvm.ptr
  %t25 = llvm.load %k.1 : !llvm.ptr -> i32
  %t26 = arith.cmpi slt, %t25, %c12_i32 : i32
  cf.cond_br %t26, ^L10, ^b1
^b1:
  cf.br ^L7
^L7:
  %t27 = llvm.load %j.2 : !llvm.ptr -> i32
  %t28 = arith.addi %t27, %c1_i32 : i32
  llvm.store %t28, %j.2 : i32, !llvm.ptr
  %t29 = llvm.load %j.2 : !llvm.ptr -> i32
  %t30 = arith.cmpi slt, %t29, %c8_i32 : i32
  cf.cond_br %t30, ^L6, ^b2
^b2:
  cf.br ^L3
^L3:
  %t31 = llvm.load %i.3 : !llvm.ptr -> i32
  %t32 = arith.addi %t31, %c1_i32 : i32
  llvm.store %t32, %i.3 : i32, !llvm.ptr
  %t33 = llvm.load %i.3 : !llvm.ptr -> i32
  %t34 = arith.cmpi slt, %t33, %c16_i32 : i32
  cf.cond_br %t34, ^L2, ^b3
^b3:
  cf.br ^L1
^L1:
  func.return
}
func.func @"mm2"() {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %a.addr = llvm.mlir.addressof @"a" : !llvm.ptr
  %c48_i64 = arith.constant 48 : i64
  %c2_i64 = arith.constant 2 : i64
  %b.addr = llvm.mlir.addressof @"b" : !llvm.ptr
  %c5_i64 = arith.constant 5 : i64
  %c12_i32 = arith.constant 12 : i32
  %d.addr = llvm.mlir.addressof @"d" : !llvm.ptr
  %c8_i32 = arith.constant 8 : i32
  %c16_i32 = arith.constant 16 : i32
  %k.4 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %j.5 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %i.6 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %s.7 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %c0_i32, %i.6 : i32, !llvm.ptr
  cf.br ^L15
^L15:
  llvm.store %c0_i32, %j.5 : i32, !llvm.ptr
  cf.br ^L19
^L19:
  llvm.store %c0_i32, %s.7 : i32, !llvm.ptr
  llvm.store %c0_i32, %k.4 : i32, !llvm.ptr
  cf.br ^L23
^L23:
  %t1 = llvm.load %s.7 : !llvm.ptr -> i32
  %t4 = llvm.load %i.6 : !llvm.ptr -> i32
  %t5 = arith.extsi %t4 : i32 to i64
  %t6 = arith.muli %c48_i64, %t5 : i64
  %t3 = llvm.getelementptr %a.addr[%t6] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t7 = llvm.load %k.4 : !llvm.ptr -> i32
  %t8 = arith.extsi %t7 : i32 to i64
  %t9 = arith.shli %t8, %c2_i64 : i64
  %t2 = llvm.getelementptr %t3[%t9] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t10 = llvm.load %t2 : !llvm.ptr -> i32
  %t13 = arith.shli %t8, %c5_i64 : i64
  %t12 = llvm.getelementptr %b.addr[%t13] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t14 = llvm.load %j.5 : !llvm.ptr -> i32
  %t15 = arith.extsi %t14 : i32 to i64
  %t16 = arith.shli %t15, %c2_i64 : i64
  %t11 = llvm.getelementptr %t12[%t16] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t17 = llvm.load %t11 : !llvm.ptr -> i32
  %t18 = arith.muli %t10, %t17 : i32
  %t19 = arith.addi %t1, %t18 : i32
  llvm.store %t19, %s.7 : i32, !llvm.ptr
  cf.br ^L24
^L24:
  %t20 = llvm.load %k.4 : !llvm.ptr -> i32
  %t21 = arith.addi %t20, %c1_i32 : i32
  llvm.store %t21, %k.4 : i32, !llvm.ptr
  %t22 = llvm.load %k.4 : !llvm.ptr -> i32
  %t23 = arith.cmpi slt, %t22, %c12_i32 : i32
  cf.cond_br %t23, ^L23, ^b1
^b1:
  %t26 = llvm.load %i.6 : !llvm.ptr -> i32
  %t27 = arith.extsi %t26 : i32 to i64
  %t28 = arith.shli %t27, %c5_i64 : i64
  %t25 = llvm.getelementptr %d.addr[%t28] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t29 = llvm.load %j.5 : !llvm.ptr -> i32
  %t30 = arith.extsi %t29 : i32 to i64
  %t31 = arith.shli %t30, %c2_i64 : i64
  %t24 = llvm.getelementptr %t25[%t31] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t32 = llvm.load %s.7 : !llvm.ptr -> i32
  llvm.store %t32, %t24 : i32, !llvm.ptr
  cf.br ^L20
^L20:
  %t33 = llvm.load %j.5 : !llvm.ptr -> i32
  %t34 = arith.addi %t33, %c1_i32 : i32
  llvm.store %t34, %j.5 : i32, !llvm.ptr
  %t35 = llvm.load %j.5 : !llvm.ptr -> i32
  %t36 = arith.cmpi slt, %t35, %c8_i32 : i32
  cf.cond_br %t36, ^L19, ^b2
^b2:
  cf.br ^L16
^L16:
  %t37 = llvm.load %i.6 : !llvm.ptr -> i32
  %t38 = arith.addi %t37, %c1_i32 : i32
  llvm.store %t38, %i.6 : i32, !llvm.ptr
  %t39 = llvm.load %i.6 : !llvm.ptr -> i32
  %t40 = arith.cmpi slt, %t39, %c16_i32 : i32
  cf.cond_br %t40, ^L15, ^b3
^b3:
  cf.br ^L14
^L14:
  func.return
}
func.func @"mm3"() {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %e.addr = llvm.mlir.addressof @"e" : !llvm.ptr
  %c5_i64 = arith.constant 5 : i64
  %c2_i64 = arith.constant 2 : i64
  %a.addr = llvm.mlir.addressof @"a" : !llvm.ptr
  %c48_i64 = arith.constant 48 : i64
  %b.addr = llvm.mlir.addressof @"b" : !llvm.ptr
  %c12_i32 = arith.constant 12 : i32
  %c8_i32 = arith.constant 8 : i32
  %c16_i32 = arith.constant 16 : i32
  %k.8 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %j.9 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %i.10 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %c0_i32, %i.10 : i32, !llvm.ptr
  cf.br ^L28
^L28:
  llvm.store %c0_i32, %j.9 : i32, !llvm.ptr
  cf.br ^L32
^L32:
  %t3 = llvm.load %i.10 : !llvm.ptr -> i32
  %t4 = arith.extsi %t3 : i32 to i64
  %t5 = arith.shli %t4, %c5_i64 : i64
  %t2 = llvm.getelementptr %e.addr[%t5] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t6 = llvm.load %j.9 : !llvm.ptr -> i32
  %t7 = arith.extsi %t6 : i32 to i64
  %t8 = arith.shli %t7, %c2_i64 : i64
  %t1 = llvm.getelementptr %t2[%t8] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  llvm.store %c0_i32, %t1 : i32, !llvm.ptr
  llvm.store %c0_i32, %k.8 : i32, !llvm.ptr
  cf.br ^L36
^L36:
  %t11 = llvm.load %i.10 : !llvm.ptr -> i32
  %t12 = arith.extsi %t11 : i32 to i64
  %t13 = arith.shli %t12, %c5_i64 : i64
  %t10 = llvm.getelementptr %e.addr[%t13] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t14 = llvm.load %j.9 : !llvm.ptr -> i32
  %t15 = arith.extsi %t14 : i32 to i64
  %t16 = arith.shli %t15, %c2_i64 : i64
  %t9 = llvm.getelementptr %t10[%t16] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t17 = llvm.load %t9 : !llvm.ptr -> i32
  %t20 = arith.muli %c48_i64, %t12 : i64
  %t19 = llvm.getelementptr %a.addr[%t20] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t21 = llvm.load %k.8 : !llvm.ptr -> i32
  %t22 = arith.extsi %t21 : i32 to i64
  %t23 = arith.shli %t22, %c2_i64 : i64
  %t18 = llvm.getelementptr %t19[%t23] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t24 = llvm.load %t18 : !llvm.ptr -> i32
  %t27 = arith.shli %t22, %c5_i64 : i64
  %t26 = llvm.getelementptr %b.addr[%t27] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t25 = llvm.getelementptr %t26[%t16] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t28 = llvm.load %t25 : !llvm.ptr -> i32
  %t29 = arith.muli %t24, %t28 : i32
  %t30 = arith.addi %t17, %t29 : i32
  llvm.store %t30, %t9 : i32, !llvm.ptr
  cf.br ^L37
^L37:
  %t31 = llvm.load %k.8 : !llvm.ptr -> i32
  %t32 = arith.addi %t31, %c1_i32 : i32
  llvm.store %t32, %k.8 : i32, !llvm.ptr
  %t33 = llvm.load %k.8 : !llvm.ptr -> i32
  %t34 = arith.cmpi slt, %t33, %c12_i32 : i32
  cf.cond_br %t34, ^L36, ^b1
^b1:
  cf.br ^L33
^L33:
  %t35 = llvm.load %j.9 : !llvm.ptr -> i32
  %t36 = arith.addi %t35, %c1_i32 : i32
  llvm.store %t36, %j.9 : i32, !llvm.ptr
  %t37 = llvm.load %j.9 : !llvm.ptr -> i32
  %t38 = arith.cmpi slt, %t37, %c8_i32 : i32
  cf.cond_br %t38, ^L32, ^b2
^b2:
  cf.br ^L29
^L29:
  %t39 = llvm.load %i.10 : !llvm.ptr -> i32
  %t40 = arith.addi %t39, %c1_i32 : i32
  llvm.store %t40, %i.10 : i32, !llvm.ptr
  %t41 = llvm.load %i.10 : !llvm.ptr -> i32
  %t42 = arith.cmpi slt, %t41, %c16_i32 : i32
  cf.cond_br %t42, ^L28, ^b3
^b3:
  cf.br ^L27
^L27:
  func.return
}
llvm.mlir.global external @"e"() {alignment = 16 : i64} : !llvm.array<512 x i8> {
  %v1 = llvm.mlir.zero : !llvm.array<512 x i8>
  llvm.return %v1 : !llvm.array<512 x i8>
}
llvm.mlir.global external @"d"() {alignment = 16 : i64} : !llvm.array<512 x i8> {
  %v1 = llvm.mlir.zero : !llvm.array<512 x i8>
  llvm.return %v1 : !llvm.array<512 x i8>
}
llvm.mlir.global external @"c"() {alignment = 16 : i64} : !llvm.array<512 x i8> {
  %v1 = llvm.mlir.zero : !llvm.array<512 x i8>
  llvm.return %v1 : !llvm.array<512 x i8>
}
llvm.mlir.global external @"b"() {alignment = 16 : i64} : !llvm.array<384 x i8> {
  %v1 = llvm.mlir.zero : !llvm.array<384 x i8>
  llvm.return %v1 : !llvm.array<384 x i8>
}
llvm.mlir.global external @"a"() {alignment = 16 : i64} : !llvm.array<768 x i8> {
  %v1 = llvm.mlir.zero : !llvm.array<768 x i8>
  llvm.return %v1 : !llvm.array<768 x i8>
}
}

// -----

// conv3.c, a 3x3 convolution that accumulates in memory and one with a
// local sum, whose loops r and s carry the sum on:
//
//   /* 3x3 convolution of a 34x34 image into 32x32, accumulating in memory,
//      and with a local sum */
//   #define H 34
//   #define W 34
//   int img[H][W], k3[3][3], res[H - 2][W - 2], res2[H - 2][W - 2];
//
//   void conv3(void)
//   {
//   	int p, q, r, s;
//
//   	for (p = 0; p < H - 2; p++)
//   		for (q = 0; q < W - 2; q++)
//   			for (r = 0; r < 3; r++)
//   				for (s = 0; s < 3; s++)
//   					res[p][q] += img[p + r][q + s] * k3[r][s];
//   }
//
//   void conv3b(void)
//   {
//   	int p, q, r, s;
//
//   	for (p = 0; p < H - 2; p++)
//   		for (q = 0; q < W - 2; q++) {
//   			int t = 0;
//   			for (r = 0; r < 3; r++)
//   				for (s = 0; s < 3; s++)
//   					t += img[p + r][q + s] * k3[r][s];
//   			res2[p][q] = t;
//   		}
//   }
//
// Both are linalg.conv_2d; the second on a buffer the reduction chain of r
// and s shares.

// CHECK-LABEL: func.func @conv3()
//       CHECK:   linalg.conv_2d ins(%{{.*}}, %{{.*}} : memref<34x34xi32>, memref<3x3xi32>) outs(%{{.*}} : memref<32x32xi32>)
//  CHECK-NEXT:   return
// CHECK-LABEL: func.func @conv3b()
//       CHECK:   %[[BUF:.*]] = memref.alloca() : memref<32x32xi32>
//       CHECK:   linalg.fill ins(%{{.*}} : i32) outs(%[[BUF]] : memref<32x32xi32>)
//  CHECK-NEXT:   linalg.conv_2d ins(%{{.*}}, %{{.*}} : memref<34x34xi32>, memref<3x3xi32>) outs(%[[BUF]] : memref<32x32xi32>)
//  CHECK-NEXT:   linalg.copy ins(%[[BUF]] : memref<32x32xi32>) outs(%{{.*}} : memref<32x32xi32>)
//  CHECK-NEXT:   return
// REMARK: remark: lcc-distribute-reductions: distributed 2 loops around it with 1 buffer of 4096 bytes
// REMARK: remark: lcc-affine-to-linalg: raised a nest of 4 loops (2 parallel, 2 reduction)
// REMARK: remark: lcc-affine-to-linalg: raised a nest of 2 loops (2 parallel, 0 reduction)
// REMARK: remark: lcc-affine-to-linalg: raised a nest of 4 loops (2 parallel, 2 reduction)
// REMARK: remark: lcc-affine-to-linalg: raised a nest of 2 loops (2 parallel, 0 reduction)

module attributes {llvm.data_layout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128", llvm.target_triple = "x86_64-pc-linux-gnu"} {
func.func @"conv3"() {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %res.addr = llvm.mlir.addressof @"res" : !llvm.ptr
  %c7_i64 = arith.constant 7 : i64
  %c2_i64 = arith.constant 2 : i64
  %img.addr = llvm.mlir.addressof @"img" : !llvm.ptr
  %c136_i64 = arith.constant 136 : i64
  %k3.addr = llvm.mlir.addressof @"k3" : !llvm.ptr
  %c12_i64 = arith.constant 12 : i64
  %c3_i32 = arith.constant 3 : i32
  %c32_i32 = arith.constant 32 : i32
  %s.1 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %r.2 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %q.3 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %p.4 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %c0_i32, %p.4 : i32, !llvm.ptr
  cf.br ^L2
^L2:
  llvm.store %c0_i32, %q.3 : i32, !llvm.ptr
  cf.br ^L6
^L6:
  llvm.store %c0_i32, %r.2 : i32, !llvm.ptr
  cf.br ^L10
^L10:
  llvm.store %c0_i32, %s.1 : i32, !llvm.ptr
  cf.br ^L14
^L14:
  %t3 = llvm.load %p.4 : !llvm.ptr -> i32
  %t4 = arith.extsi %t3 : i32 to i64
  %t5 = arith.shli %t4, %c7_i64 : i64
  %t2 = llvm.getelementptr %res.addr[%t5] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t6 = llvm.load %q.3 : !llvm.ptr -> i32
  %t7 = arith.extsi %t6 : i32 to i64
  %t8 = arith.shli %t7, %c2_i64 : i64
  %t1 = llvm.getelementptr %t2[%t8] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t9 = llvm.load %t1 : !llvm.ptr -> i32
  %t12 = llvm.load %r.2 : !llvm.ptr -> i32
  %t13 = arith.addi %t3, %t12 : i32
  %t14 = arith.extsi %t13 : i32 to i64
  %t15 = arith.muli %c136_i64, %t14 : i64
  %t11 = llvm.getelementptr %img.addr[%t15] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t16 = llvm.load %s.1 : !llvm.ptr -> i32
  %t17 = arith.addi %t6, %t16 : i32
  %t18 = arith.extsi %t17 : i32 to i64
  %t19 = arith.shli %t18, %c2_i64 : i64
  %t10 = llvm.getelementptr %t11[%t19] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t20 = llvm.load %t10 : !llvm.ptr -> i32
  %t23 = arith.extsi %t12 : i32 to i64
  %t24 = arith.muli %c12_i64, %t23 : i64
  %t22 = llvm.getelementptr %k3.addr[%t24] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t25 = arith.extsi %t16 : i32 to i64
  %t26 = arith.shli %t25, %c2_i64 : i64
  %t21 = llvm.getelementptr %t22[%t26] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t27 = llvm.load %t21 : !llvm.ptr -> i32
  %t28 = arith.muli %t20, %t27 : i32
  %t29 = arith.addi %t9, %t28 : i32
  llvm.store %t29, %t1 : i32, !llvm.ptr
  cf.br ^L15
^L15:
  %t30 = llvm.load %s.1 : !llvm.ptr -> i32
  %t31 = arith.addi %t30, %c1_i32 : i32
  llvm.store %t31, %s.1 : i32, !llvm.ptr
  %t32 = llvm.load %s.1 : !llvm.ptr -> i32
  %t33 = arith.cmpi slt, %t32, %c3_i32 : i32
  cf.cond_br %t33, ^L14, ^b1
^b1:
  cf.br ^L11
^L11:
  %t34 = llvm.load %r.2 : !llvm.ptr -> i32
  %t35 = arith.addi %t34, %c1_i32 : i32
  llvm.store %t35, %r.2 : i32, !llvm.ptr
  %t36 = llvm.load %r.2 : !llvm.ptr -> i32
  %t37 = arith.cmpi slt, %t36, %c3_i32 : i32
  cf.cond_br %t37, ^L10, ^b2
^b2:
  cf.br ^L7
^L7:
  %t38 = llvm.load %q.3 : !llvm.ptr -> i32
  %t39 = arith.addi %t38, %c1_i32 : i32
  llvm.store %t39, %q.3 : i32, !llvm.ptr
  %t40 = llvm.load %q.3 : !llvm.ptr -> i32
  %t41 = arith.cmpi slt, %t40, %c32_i32 : i32
  cf.cond_br %t41, ^L6, ^b3
^b3:
  cf.br ^L3
^L3:
  %t42 = llvm.load %p.4 : !llvm.ptr -> i32
  %t43 = arith.addi %t42, %c1_i32 : i32
  llvm.store %t43, %p.4 : i32, !llvm.ptr
  %t44 = llvm.load %p.4 : !llvm.ptr -> i32
  %t45 = arith.cmpi slt, %t44, %c32_i32 : i32
  cf.cond_br %t45, ^L2, ^b4
^b4:
  cf.br ^L1
^L1:
  func.return
}
func.func @"conv3b"() {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %img.addr = llvm.mlir.addressof @"img" : !llvm.ptr
  %c136_i64 = arith.constant 136 : i64
  %c2_i64 = arith.constant 2 : i64
  %k3.addr = llvm.mlir.addressof @"k3" : !llvm.ptr
  %c12_i64 = arith.constant 12 : i64
  %c3_i32 = arith.constant 3 : i32
  %res2.addr = llvm.mlir.addressof @"res2" : !llvm.ptr
  %c7_i64 = arith.constant 7 : i64
  %c32_i32 = arith.constant 32 : i32
  %s.5 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %r.6 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %q.7 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %p.8 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %t.9 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %c0_i32, %p.8 : i32, !llvm.ptr
  cf.br ^L19
^L19:
  llvm.store %c0_i32, %q.7 : i32, !llvm.ptr
  cf.br ^L23
^L23:
  llvm.store %c0_i32, %t.9 : i32, !llvm.ptr
  llvm.store %c0_i32, %r.6 : i32, !llvm.ptr
  cf.br ^L27
^L27:
  llvm.store %c0_i32, %s.5 : i32, !llvm.ptr
  cf.br ^L31
^L31:
  %t1 = llvm.load %t.9 : !llvm.ptr -> i32
  %t4 = llvm.load %p.8 : !llvm.ptr -> i32
  %t5 = llvm.load %r.6 : !llvm.ptr -> i32
  %t6 = arith.addi %t4, %t5 : i32
  %t7 = arith.extsi %t6 : i32 to i64
  %t8 = arith.muli %c136_i64, %t7 : i64
  %t3 = llvm.getelementptr %img.addr[%t8] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t9 = llvm.load %q.7 : !llvm.ptr -> i32
  %t10 = llvm.load %s.5 : !llvm.ptr -> i32
  %t11 = arith.addi %t9, %t10 : i32
  %t12 = arith.extsi %t11 : i32 to i64
  %t13 = arith.shli %t12, %c2_i64 : i64
  %t2 = llvm.getelementptr %t3[%t13] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t14 = llvm.load %t2 : !llvm.ptr -> i32
  %t17 = arith.extsi %t5 : i32 to i64
  %t18 = arith.muli %c12_i64, %t17 : i64
  %t16 = llvm.getelementptr %k3.addr[%t18] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t19 = arith.extsi %t10 : i32 to i64
  %t20 = arith.shli %t19, %c2_i64 : i64
  %t15 = llvm.getelementptr %t16[%t20] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t21 = llvm.load %t15 : !llvm.ptr -> i32
  %t22 = arith.muli %t14, %t21 : i32
  %t23 = arith.addi %t1, %t22 : i32
  llvm.store %t23, %t.9 : i32, !llvm.ptr
  cf.br ^L32
^L32:
  %t24 = llvm.load %s.5 : !llvm.ptr -> i32
  %t25 = arith.addi %t24, %c1_i32 : i32
  llvm.store %t25, %s.5 : i32, !llvm.ptr
  %t26 = llvm.load %s.5 : !llvm.ptr -> i32
  %t27 = arith.cmpi slt, %t26, %c3_i32 : i32
  cf.cond_br %t27, ^L31, ^b1
^b1:
  cf.br ^L28
^L28:
  %t28 = llvm.load %r.6 : !llvm.ptr -> i32
  %t29 = arith.addi %t28, %c1_i32 : i32
  llvm.store %t29, %r.6 : i32, !llvm.ptr
  %t30 = llvm.load %r.6 : !llvm.ptr -> i32
  %t31 = arith.cmpi slt, %t30, %c3_i32 : i32
  cf.cond_br %t31, ^L27, ^b2
^b2:
  %t34 = llvm.load %p.8 : !llvm.ptr -> i32
  %t35 = arith.extsi %t34 : i32 to i64
  %t36 = arith.shli %t35, %c7_i64 : i64
  %t33 = llvm.getelementptr %res2.addr[%t36] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t37 = llvm.load %q.7 : !llvm.ptr -> i32
  %t38 = arith.extsi %t37 : i32 to i64
  %t39 = arith.shli %t38, %c2_i64 : i64
  %t32 = llvm.getelementptr %t33[%t39] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t40 = llvm.load %t.9 : !llvm.ptr -> i32
  llvm.store %t40, %t32 : i32, !llvm.ptr
  cf.br ^L24
^L24:
  %t41 = llvm.load %q.7 : !llvm.ptr -> i32
  %t42 = arith.addi %t41, %c1_i32 : i32
  llvm.store %t42, %q.7 : i32, !llvm.ptr
  %t43 = llvm.load %q.7 : !llvm.ptr -> i32
  %t44 = arith.cmpi slt, %t43, %c32_i32 : i32
  cf.cond_br %t44, ^L23, ^b3
^b3:
  cf.br ^L20
^L20:
  %t45 = llvm.load %p.8 : !llvm.ptr -> i32
  %t46 = arith.addi %t45, %c1_i32 : i32
  llvm.store %t46, %p.8 : i32, !llvm.ptr
  %t47 = llvm.load %p.8 : !llvm.ptr -> i32
  %t48 = arith.cmpi slt, %t47, %c32_i32 : i32
  cf.cond_br %t48, ^L19, ^b4
^b4:
  cf.br ^L18
^L18:
  func.return
}
llvm.mlir.global external @"res2"() {alignment = 16 : i64} : !llvm.array<4096 x i8> {
  %v1 = llvm.mlir.zero : !llvm.array<4096 x i8>
  llvm.return %v1 : !llvm.array<4096 x i8>
}
llvm.mlir.global external @"res"() {alignment = 16 : i64} : !llvm.array<4096 x i8> {
  %v1 = llvm.mlir.zero : !llvm.array<4096 x i8>
  llvm.return %v1 : !llvm.array<4096 x i8>
}
llvm.mlir.global external @"k3"() {alignment = 16 : i64} : !llvm.array<36 x i8> {
  %v1 = llvm.mlir.zero : !llvm.array<36 x i8>
  llvm.return %v1 : !llvm.array<36 x i8>
}
llvm.mlir.global external @"img"() {alignment = 16 : i64} : !llvm.array<4624 x i8> {
  %v1 = llvm.mlir.zero : !llvm.array<4624 x i8>
  llvm.return %v1 : !llvm.array<4624 x i8>
}
}
