// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// RUN: lcc-opt --lcc-memcpy-intrinsic --lcc-split-aggregates %s \
// RUN:   | FileCheck %s --check-prefix=SPLIT
// RUN: lcc-opt --lcc-prepare %s | FileCheck %s --check-prefix=PREP
//
// lcc-split-aggregates splits lcc's byte array allocas of structures into
// scalar allocas, one per member, which mem2reg promotes; a structure copy
// becomes a load and a store per member and per filler of the bytes no
// member covers.  The input is what lcc f2445d8 (mlir/x86_64-linux) gives
// for split.c,
//
//   struct pt {
//   	int x;
//   	double y;
//   	char c;
//   };
//
//   double members(int a)
//   {
//   	struct pt p;
//
//   	p.x = a;
//   	p.y = a * 0.5;
//   	p.c = 3;
//   	return p.x + p.y + p.c;
//   }
//
//   int copied(struct pt *q)
//   {
//   	struct pt p;
//
//   	p = *q;
//   	return p.x + p.c;
//   }
//
//   void through(struct pt *d, struct pt *q, int a)
//   {
//   	struct pt p;
//
//   	p = *q;
//   	p.x += a;
//   	*d = p;
//   }
//
//   union u {
//   	int i;
//   	float f;
//   };
//
//   int punned(float f)
//   {
//   	union u v;
//
//   	v.f = f;
//   	return v.i;
//   }
//
//   void use(struct pt *);
//
//   int escapes(int a)
//   {
//   	struct pt p;
//
//   	p.x = a;
//   	use(&p);
//   	return p.x;
//   }
//
//   int indexed(int i)
//   {
//   	int t[4];
//
//   	t[0] = 1; t[1] = 2; t[2] = 3; t[3] = 4;
//   	return t[i & 3];
//   }
//
//   int vcopy(volatile struct pt *q)
//   {
//   	struct pt p;
//
//   	p = *q;
//   	return p.x;
//   }
//
// made in the top directory of lcc with
//
//   make -r BUILDDIR=$B HOSTFILE=etc/mlir-x86_64-linux.c rcc lcc cpp headers
//   LCCDIR=$B $B/lcc -S split.c

// members: x, y and c become an i32, an f64 and an i8.
// SPLIT-LABEL: func.func @members(
//   SPLIT-NOT:   !llvm.array<24 x i8>
//       SPLIT:   %[[X:.*]] = llvm.alloca %{{.*}} x i32 : (i32) -> !llvm.ptr
//  SPLIT-NEXT:   %[[Y:.*]] = llvm.alloca %{{.*}} x f64 : (i32) -> !llvm.ptr
//  SPLIT-NEXT:   %[[C:.*]] = llvm.alloca %{{.*}} x i8 : (i32) -> !llvm.ptr
//       SPLIT:   llvm.store %{{.*}}, %[[X]] : i32, !llvm.ptr
//       SPLIT:   llvm.store %{{.*}}, %[[Y]] : f64, !llvm.ptr
//       SPLIT:   llvm.store %{{.*}}, %[[C]] : i8, !llvm.ptr
//   SPLIT-NOT:   llvm.getelementptr

// copied: the 24 bytes of *q come in as x, 8 bytes and 4 bytes of filler
// (with y and the padding after x), c and 7 bytes of filler, each load
// aligned as far as the offset from an 8-aligned q allows.
// SPLIT-LABEL: func.func @copied(
//   SPLIT-NOT:   llvm.intr.memcpy
//       SPLIT:   %[[Q:.*]] = llvm.load %{{.*}} : !llvm.ptr -> !llvm.ptr
//  SPLIT-NEXT:   llvm.load %[[Q]] {alignment = 8 : i64} : !llvm.ptr -> i32
//       SPLIT:   llvm.getelementptr %[[Q]][4]
//  SPLIT-NEXT:   llvm.load %{{.*}} {alignment = 4 : i64} : !llvm.ptr -> i64
//       SPLIT:   llvm.getelementptr %[[Q]][12]
//  SPLIT-NEXT:   llvm.load %{{.*}} {alignment = 4 : i64} : !llvm.ptr -> i32
//       SPLIT:   llvm.getelementptr %[[Q]][16]
//  SPLIT-NEXT:   llvm.load %{{.*}} {alignment = 8 : i64} : !llvm.ptr -> i8
//       SPLIT:   llvm.getelementptr %[[Q]][17]
//  SPLIT-NEXT:   llvm.load %{{.*}} {alignment = 1 : i64} : !llvm.ptr -> i56
//   SPLIT-NOT:   llvm.intr.memcpy

// through: only x is a member here, so *d gets x and 20 bytes of filler,
// all 24 bytes, back.
// SPLIT-LABEL: func.func @through(
//   SPLIT-NOT:   llvm.intr.memcpy
//       SPLIT:   llvm.store %{{.*}} {alignment = 8 : i64} : i32, !llvm.ptr
//  SPLIT-NEXT:   llvm.getelementptr %{{.*}}[4]
//       SPLIT:   llvm.store %{{.*}} {alignment = 4 : i64} : i64, !llvm.ptr
//  SPLIT-NEXT:   llvm.getelementptr %{{.*}}[12]
//       SPLIT:   llvm.store %{{.*}} {alignment = 4 : i64} : i64, !llvm.ptr
//  SPLIT-NEXT:   llvm.getelementptr %{{.*}}[20]
//       SPLIT:   llvm.store %{{.*}} {alignment = 4 : i64} : i32, !llvm.ptr
//   SPLIT-NOT:   llvm.intr.memcpy

// The others keep their byte array: an f32 and an i32 at the same bytes,
// an address passed to use, an index not constant, a volatile copy.
// SPLIT-LABEL: func.func @punned(
//       SPLIT:   llvm.alloca %{{.*}} x !llvm.array<4 x i8>
// SPLIT-LABEL: func.func @escapes(
//       SPLIT:   llvm.alloca %{{.*}} x !llvm.array<24 x i8>
// SPLIT-LABEL: func.func @indexed(
//       SPLIT:   llvm.alloca %{{.*}} x !llvm.array<16 x i8>
// SPLIT-LABEL: func.func @vcopy(
//       SPLIT:   llvm.alloca %{{.*}} x !llvm.array<24 x i8>
//       SPLIT:   llvm.intr.memcpy{{.*}}isVolatile = true

// lcc-prepare: no alloca is left where the split applies.
// PREP-LABEL: func.func @members(
//   PREP-NOT:   llvm.alloca
//       PREP:   return
// PREP-LABEL: func.func @copied(
//   PREP-NOT:   llvm.alloca
//       PREP:   return
// PREP-LABEL: func.func @through(
//   PREP-NOT:   llvm.alloca
//       PREP:   return
// PREP-LABEL: func.func @punned(
//       PREP:   llvm.alloca
// PREP-LABEL: func.func @escapes(
//       PREP:   llvm.alloca
// PREP-LABEL: func.func @indexed(
//       PREP:   llvm.alloca
// PREP-LABEL: func.func @vcopy(
//       PREP:   llvm.alloca

module attributes {llvm.data_layout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128", llvm.target_triple = "x86_64-pc-linux-gnu"} {
func.func @"members"(%a0: i32) -> f64 {
  %c1_i32 = arith.constant 1 : i32
  %cst1 = arith.constant 0x3FE0000000000000 : f64
  %c3_i8 = arith.constant 3 : i8
  %retval = llvm.alloca %c1_i32 x f64 : (i32) -> !llvm.ptr
  %a.1 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %a0, %a.1 : i32, !llvm.ptr
  %p.2 = llvm.alloca %c1_i32 x !llvm.array<24 x i8> {alignment = 8 : i64} : (i32) -> !llvm.ptr
  %la3 = llvm.getelementptr %p.2[8] : (!llvm.ptr) -> !llvm.ptr, i8
  %la4 = llvm.getelementptr %p.2[16] : (!llvm.ptr) -> !llvm.ptr, i8
  %la5 = llvm.getelementptr %p.2[8] : (!llvm.ptr) -> !llvm.ptr, i8
  %la6 = llvm.getelementptr %p.2[16] : (!llvm.ptr) -> !llvm.ptr, i8
  %t1 = llvm.load %a.1 : !llvm.ptr -> i32
  llvm.store %t1, %p.2 : i32, !llvm.ptr
  %t2 = arith.sitofp %t1 : i32 to f64
  %t3 = arith.mulf %cst1, %t2 : f64
  llvm.store %t3, %la3 : f64, !llvm.ptr
  llvm.store %c3_i8, %la4 : i8, !llvm.ptr
  %t4 = llvm.load %p.2 : !llvm.ptr -> i32
  %t5 = arith.sitofp %t4 : i32 to f64
  %t6 = llvm.load %la5 : !llvm.ptr -> f64
  %t7 = arith.addf %t5, %t6 : f64
  %t8 = llvm.load %la6 : !llvm.ptr -> i8
  %t9 = arith.extsi %t8 : i8 to i32
  %t10 = arith.sitofp %t9 : i32 to f64
  %t11 = arith.addf %t7, %t10 : f64
  llvm.store %t11, %retval : f64, !llvm.ptr
  cf.br ^L1
^L1:
  %t12 = llvm.load %retval : !llvm.ptr -> f64
  func.return %t12 : f64
}
func.func @"copied"(%a0: !llvm.ptr) -> i32 {
  %c1_i32 = arith.constant 1 : i32
  %c24_i64 = arith.constant 24 : i64
  %false = arith.constant false
  %retval = llvm.alloca %c1_i32 x i32 : (i32) -> !llvm.ptr
  %q.7 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  llvm.store %a0, %q.7 : !llvm.ptr, !llvm.ptr
  %p.8 = llvm.alloca %c1_i32 x !llvm.array<24 x i8> {alignment = 8 : i64} : (i32) -> !llvm.ptr
  %la9 = llvm.getelementptr %p.8[16] : (!llvm.ptr) -> !llvm.ptr, i8
  %t1 = llvm.load %q.7 : !llvm.ptr -> !llvm.ptr
  llvm.call_intrinsic "llvm.memcpy.p0.p0.i64"(%p.8, %t1, %c24_i64, %false) : (!llvm.ptr {llvm.align = 8 : i64}, !llvm.ptr {llvm.align = 8 : i64}, i64, i1) -> ()
  %t2 = llvm.load %p.8 : !llvm.ptr -> i32
  %t3 = llvm.load %la9 : !llvm.ptr -> i8
  %t4 = arith.extsi %t3 : i8 to i32
  %t5 = arith.addi %t2, %t4 : i32
  llvm.store %t5, %retval : i32, !llvm.ptr
  cf.br ^L6
^L6:
  %t6 = llvm.load %retval : !llvm.ptr -> i32
  func.return %t6 : i32
}
func.func @"through"(%a0: !llvm.ptr, %a1: !llvm.ptr, %a2: i32) {
  %c1_i32 = arith.constant 1 : i32
  %c24_i64 = arith.constant 24 : i64
  %false = arith.constant false
  %d.10 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  llvm.store %a0, %d.10 : !llvm.ptr, !llvm.ptr
  %q.11 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  llvm.store %a1, %q.11 : !llvm.ptr, !llvm.ptr
  %a.12 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %a2, %a.12 : i32, !llvm.ptr
  %p.13 = llvm.alloca %c1_i32 x !llvm.array<24 x i8> {alignment = 8 : i64} : (i32) -> !llvm.ptr
  %t1 = llvm.load %q.11 : !llvm.ptr -> !llvm.ptr
  llvm.call_intrinsic "llvm.memcpy.p0.p0.i64"(%p.13, %t1, %c24_i64, %false) : (!llvm.ptr {llvm.align = 8 : i64}, !llvm.ptr {llvm.align = 8 : i64}, i64, i1) -> ()
  %t2 = llvm.load %p.13 : !llvm.ptr -> i32
  %t3 = llvm.load %a.12 : !llvm.ptr -> i32
  %t4 = arith.addi %t2, %t3 : i32
  llvm.store %t4, %p.13 : i32, !llvm.ptr
  %t5 = llvm.load %d.10 : !llvm.ptr -> !llvm.ptr
  llvm.call_intrinsic "llvm.memcpy.p0.p0.i64"(%t5, %p.13, %c24_i64, %false) : (!llvm.ptr {llvm.align = 8 : i64}, !llvm.ptr {llvm.align = 8 : i64}, i64, i1) -> ()
  cf.br ^L8
^L8:
  func.return
}
func.func @"punned"(%a0: f32) -> i32 {
  %c1_i32 = arith.constant 1 : i32
  %retval = llvm.alloca %c1_i32 x i32 : (i32) -> !llvm.ptr
  %f.14 = llvm.alloca %c1_i32 x f32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %a0, %f.14 : f32, !llvm.ptr
  %v.15 = llvm.alloca %c1_i32 x !llvm.array<4 x i8> {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %t1 = llvm.load %f.14 : !llvm.ptr -> f32
  llvm.store %t1, %v.15 : f32, !llvm.ptr
  %t2 = llvm.load %v.15 : !llvm.ptr -> i32
  llvm.store %t2, %retval : i32, !llvm.ptr
  cf.br ^L9
^L9:
  %t3 = llvm.load %retval : !llvm.ptr -> i32
  func.return %t3 : i32
}
func.func @"escapes"(%a0: i32) -> i32 {
  %c1_i32 = arith.constant 1 : i32
  %retval = llvm.alloca %c1_i32 x i32 : (i32) -> !llvm.ptr
  %a.16 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %a0, %a.16 : i32, !llvm.ptr
  %p.17 = llvm.alloca %c1_i32 x !llvm.array<24 x i8> {alignment = 8 : i64} : (i32) -> !llvm.ptr
  %t1 = llvm.load %a.16 : !llvm.ptr -> i32
  llvm.store %t1, %p.17 : i32, !llvm.ptr
  func.call @"use"(%p.17) : (!llvm.ptr) -> ()
  %t2 = llvm.load %p.17 : !llvm.ptr -> i32
  llvm.store %t2, %retval : i32, !llvm.ptr
  cf.br ^L10
^L10:
  %t3 = llvm.load %retval : !llvm.ptr -> i32
  func.return %t3 : i32
}
func.func @"indexed"(%a0: i32) -> i32 {
  %c1_i32 = arith.constant 1 : i32
  %c2_i32 = arith.constant 2 : i32
  %c3_i32 = arith.constant 3 : i32
  %c4_i32 = arith.constant 4 : i32
  %c2_i64 = arith.constant 2 : i64
  %retval = llvm.alloca %c1_i32 x i32 : (i32) -> !llvm.ptr
  %i.18 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %a0, %i.18 : i32, !llvm.ptr
  %t.19 = llvm.alloca %c1_i32 x !llvm.array<16 x i8> {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %la20 = llvm.getelementptr %t.19[4] : (!llvm.ptr) -> !llvm.ptr, i8
  %la21 = llvm.getelementptr %t.19[8] : (!llvm.ptr) -> !llvm.ptr, i8
  %la22 = llvm.getelementptr %t.19[12] : (!llvm.ptr) -> !llvm.ptr, i8
  llvm.store %c1_i32, %t.19 : i32, !llvm.ptr
  llvm.store %c2_i32, %la20 : i32, !llvm.ptr
  llvm.store %c3_i32, %la21 : i32, !llvm.ptr
  llvm.store %c4_i32, %la22 : i32, !llvm.ptr
  %t2 = llvm.load %i.18 : !llvm.ptr -> i32
  %t3 = arith.andi %t2, %c3_i32 : i32
  %t4 = arith.extsi %t3 : i32 to i64
  %t5 = arith.shli %t4, %c2_i64 : i64
  %t1 = llvm.getelementptr %t.19[%t5] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t6 = llvm.load %t1 : !llvm.ptr -> i32
  llvm.store %t6, %retval : i32, !llvm.ptr
  cf.br ^L11
^L11:
  %t7 = llvm.load %retval : !llvm.ptr -> i32
  func.return %t7 : i32
}
func.func @"vcopy"(%a0: !llvm.ptr) -> i32 {
  %c1_i32 = arith.constant 1 : i32
  %c24_i64 = arith.constant 24 : i64
  %true = arith.constant true
  %retval = llvm.alloca %c1_i32 x i32 : (i32) -> !llvm.ptr
  %q.23 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  llvm.store %a0, %q.23 : !llvm.ptr, !llvm.ptr
  %p.24 = llvm.alloca %c1_i32 x !llvm.array<24 x i8> {alignment = 8 : i64} : (i32) -> !llvm.ptr
  %t1 = llvm.load %q.23 : !llvm.ptr -> !llvm.ptr
  llvm.call_intrinsic "llvm.memcpy.p0.p0.i64"(%p.24, %t1, %c24_i64, %true) : (!llvm.ptr {llvm.align = 8 : i64}, !llvm.ptr {llvm.align = 8 : i64}, i64, i1) -> ()
  %t2 = llvm.load %p.24 : !llvm.ptr -> i32
  llvm.store %t2, %retval : i32, !llvm.ptr
  cf.br ^L15
^L15:
  %t3 = llvm.load %retval : !llvm.ptr -> i32
  func.return %t3 : i32
}
func.func private @"use"(!llvm.ptr)
}
