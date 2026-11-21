// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// RUN: lcc-opt --lcc-byval-copy %s | FileCheck %s --check-prefix=COPY
// RUN: lcc-opt --lcc-byval-copy --lcc-byval-copy %s \
// RUN:   | FileCheck %s --check-prefix=COPY
// RUN: lcc-opt --inline --lcc-hoist-allocas %s | FileCheck %s --check-prefix=HOIST
// RUN: lcc-opt --lcc-prepare %s | FileCheck %s --check-prefix=PREP
//
// The inliner does not copy an llvm.byval argument of a func.func, nor move
// the allocas of an inlined func.func into the entry block of the caller:
// sum would change *p of byval, and the array of local would take more stack
// in every iteration of loop.  lcc-byval-copy and lcc-hoist-allocas, around
// the inliner in lcc-prepare, do that.  The input is what lcc f2445d8
// (mlir/x86_64-linux) gives for inline.c,
//
//   struct big {
//   	int v[8];
//   };
//
//   static int sum(struct big b)
//   {
//   	int i, s;
//
//   	s = 0;
//   	for (i = 0; i < 8; i++) {
//   		s += b.v[i];
//   		b.v[i] = 0;
//   	}
//   	return s;
//   }
//
//   int byval(struct big *p, int n)
//   {
//   	int i, s;
//
//   	s = 0;
//   	for (i = 0; i < n; i++)
//   		s += sum(*p);
//   	return s;
//   }
//
//   void use(int *);
//
//   static int local(int x)
//   {
//   	int a[4];
//
//   	a[0] = x;
//   	use(a);
//   	return a[1];
//   }
//
//   int loop(int n)
//   {
//   	int i, s;
//
//   	s = 0;
//   	for (i = 0; i < n; i++)
//   		s += local(i);
//   	return s;
//   }
//
// made in the top directory of lcc with
//
//   make -r BUILDDIR=$B HOSTFILE=etc/mlir-x86_64-linux.c rcc lcc cpp headers
//   LCCDIR=$B $B/lcc -S inline.c

// sum uses a copy of its llvm.byval argument, made once (a second run of the
// pass sees the copy and leaves it alone).
// COPY-LABEL: func.func @sum(
//  COPY-SAME:     %[[B:.*]]: !llvm.ptr {llvm.align = 8 : i64, llvm.byval = !llvm.array<32 x i8>})
//  COPY-NEXT:   %[[ONE:.*]] = llvm.mlir.constant(1 : i64) : i64
//  COPY-NEXT:   %[[C:.*]] = llvm.alloca %[[ONE]] x !llvm.array<32 x i8> {alignment = 8 : i64} : (i64) -> !llvm.ptr
//  COPY-NEXT:   %[[N:.*]] = llvm.mlir.constant(32 : i64) : i64
//  COPY-NEXT:   "llvm.intr.memcpy"(%[[C]], %[[B]], %[[N]]) <{isVolatile = false}>
//   COPY-NOT:   llvm.intr.memcpy
//   COPY-NOT:   %[[B]]
//       COPY:   return
// COPY-LABEL: func.func @byval(
//   COPY-NOT:   llvm.intr.memcpy
// COPY-LABEL: func.func @local(

// After the inliner, the allocas of local, among them its array, are in the
// entry block of loop.
// HOIST-LABEL: func.func @loop(
//       HOIST:   llvm.alloca %{{.*}} x !llvm.array<16 x i8>
//       HOIST:   cf.br
//   HOIST-NOT:   llvm.alloca
//       HOIST:   call @use
//   HOIST-NOT:   llvm.alloca
//       HOIST:   return

// lcc-prepare: byval copies *p in every iteration into one alloca of its
// entry block and works on the copy; loop has the array of local in its
// entry block and no other alloca.
// PREP-LABEL: func.func @byval(
//  PREP-SAME:     %[[P:[^:]*]]: !llvm.ptr,
//       PREP:   %[[C:.*]] = llvm.alloca %{{.*}} x !llvm.array<32 x i8> {alignment = 8 : i64}
//   PREP-NOT:   llvm.alloca
//       PREP: ^bb1:
//  PREP-NEXT:   "llvm.intr.memcpy"(%[[C]], %[[P]], %{{.*}})
//       PREP:   %[[E:.*]] = llvm.getelementptr %[[C]][%{{.*}}]
//       PREP:   llvm.store %{{.*}}, %[[E]] : i32, !llvm.ptr
//   PREP-NOT:   call
//   PREP-NOT:   llvm.alloca
//       PREP:   return
// PREP-LABEL: func.func @loop(
//       PREP:   %[[A:.*]] = llvm.alloca %{{.*}} x !llvm.array<16 x i8>
//   PREP-NOT:   llvm.alloca
//       PREP:   call @use(%[[A]])
//   PREP-NOT:   llvm.alloca
//       PREP:   return

module attributes {llvm.data_layout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128", llvm.target_triple = "x86_64-pc-linux-gnu"} {
func.func @"sum"(%a0: !llvm.ptr {llvm.byval = !llvm.array<32 x i8>, llvm.align = 8 : i64}) -> i32 attributes {llvm.linkage = #llvm.linkage<internal>} {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %c2_i64 = arith.constant 2 : i64
  %c8_i32 = arith.constant 8 : i32
  %retval = llvm.alloca %c1_i32 x i32 : (i32) -> !llvm.ptr
  %i.1 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %s.2 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %c0_i32, %s.2 : i32, !llvm.ptr
  llvm.store %c0_i32, %i.1 : i32, !llvm.ptr
  cf.br ^L2
^L2:
  %t1 = llvm.load %s.2 : !llvm.ptr -> i32
  %t3 = llvm.load %i.1 : !llvm.ptr -> i32
  %t4 = arith.extsi %t3 : i32 to i64
  %t5 = arith.shli %t4, %c2_i64 : i64
  %t2 = llvm.getelementptr %a0[%t5] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t6 = llvm.load %t2 : !llvm.ptr -> i32
  %t7 = arith.addi %t1, %t6 : i32
  llvm.store %t7, %s.2 : i32, !llvm.ptr
  llvm.store %c0_i32, %t2 : i32, !llvm.ptr
  cf.br ^L3
^L3:
  %t8 = llvm.load %i.1 : !llvm.ptr -> i32
  %t9 = arith.addi %t8, %c1_i32 : i32
  llvm.store %t9, %i.1 : i32, !llvm.ptr
  %t10 = llvm.load %i.1 : !llvm.ptr -> i32
  %t11 = arith.cmpi slt, %t10, %c8_i32 : i32
  cf.cond_br %t11, ^L2, ^b1
^b1:
  %t12 = llvm.load %s.2 : !llvm.ptr -> i32
  llvm.store %t12, %retval : i32, !llvm.ptr
  cf.br ^L1
^L1:
  %t13 = llvm.load %retval : !llvm.ptr -> i32
  func.return %t13 : i32
}
func.func @"byval"(%a0: !llvm.ptr, %a1: i32) -> i32 {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %retval = llvm.alloca %c1_i32 x i32 : (i32) -> !llvm.ptr
  %p.3 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  llvm.store %a0, %p.3 : !llvm.ptr, !llvm.ptr
  %p.4 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  %n.5 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %a1, %n.5 : i32, !llvm.ptr
  %n.6 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %i.7 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %s.8 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %t1 = llvm.load %p.3 : !llvm.ptr -> !llvm.ptr
  llvm.store %t1, %p.4 : !llvm.ptr, !llvm.ptr
  %t2 = llvm.load %n.5 : !llvm.ptr -> i32
  llvm.store %t2, %n.6 : i32, !llvm.ptr
  llvm.store %c0_i32, %s.8 : i32, !llvm.ptr
  llvm.store %c0_i32, %i.7 : i32, !llvm.ptr
  cf.br ^L10
^L7:
  %t3 = llvm.load %p.4 : !llvm.ptr -> !llvm.ptr
  %t4 = func.call @"sum"(%t3) {arg_attrs = [{llvm.byval = !llvm.array<32 x i8>, llvm.align = 8 : i64}]} : (!llvm.ptr) -> i32
  %t5 = llvm.load %s.8 : !llvm.ptr -> i32
  %t6 = arith.addi %t5, %t4 : i32
  llvm.store %t6, %s.8 : i32, !llvm.ptr
  cf.br ^L8
^L8:
  %t7 = llvm.load %i.7 : !llvm.ptr -> i32
  %t8 = arith.addi %t7, %c1_i32 : i32
  llvm.store %t8, %i.7 : i32, !llvm.ptr
  cf.br ^L10
^L10:
  %t9 = llvm.load %i.7 : !llvm.ptr -> i32
  %t10 = llvm.load %n.6 : !llvm.ptr -> i32
  %t11 = arith.cmpi slt, %t9, %t10 : i32
  cf.cond_br %t11, ^L7, ^b1
^b1:
  %t12 = llvm.load %s.8 : !llvm.ptr -> i32
  llvm.store %t12, %retval : i32, !llvm.ptr
  cf.br ^L6
^L6:
  %t13 = llvm.load %retval : !llvm.ptr -> i32
  func.return %t13 : i32
}
func.func @"local"(%a0: i32) -> i32 attributes {llvm.linkage = #llvm.linkage<internal>} {
  %c1_i32 = arith.constant 1 : i32
  %retval = llvm.alloca %c1_i32 x i32 : (i32) -> !llvm.ptr
  %x.9 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %a0, %x.9 : i32, !llvm.ptr
  %a.10 = llvm.alloca %c1_i32 x !llvm.array<16 x i8> {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %la11 = llvm.getelementptr %a.10[4] : (!llvm.ptr) -> !llvm.ptr, i8
  %t1 = llvm.load %x.9 : !llvm.ptr -> i32
  llvm.store %t1, %a.10 : i32, !llvm.ptr
  func.call @"use"(%a.10) : (!llvm.ptr) -> ()
  %t2 = llvm.load %la11 : !llvm.ptr -> i32
  llvm.store %t2, %retval : i32, !llvm.ptr
  cf.br ^L11
^L11:
  %t3 = llvm.load %retval : !llvm.ptr -> i32
  func.return %t3 : i32
}
func.func @"loop"(%a0: i32) -> i32 {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %retval = llvm.alloca %c1_i32 x i32 : (i32) -> !llvm.ptr
  %n.12 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %a0, %n.12 : i32, !llvm.ptr
  %n.13 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %i.14 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %s.15 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %t1 = llvm.load %n.12 : !llvm.ptr -> i32
  llvm.store %t1, %n.13 : i32, !llvm.ptr
  llvm.store %c0_i32, %s.15 : i32, !llvm.ptr
  llvm.store %c0_i32, %i.14 : i32, !llvm.ptr
  cf.br ^L17
^L14:
  %t2 = llvm.load %i.14 : !llvm.ptr -> i32
  %t3 = func.call @"local"(%t2) : (i32) -> i32
  %t4 = llvm.load %s.15 : !llvm.ptr -> i32
  %t5 = arith.addi %t4, %t3 : i32
  llvm.store %t5, %s.15 : i32, !llvm.ptr
  cf.br ^L15
^L15:
  %t6 = llvm.load %i.14 : !llvm.ptr -> i32
  %t7 = arith.addi %t6, %c1_i32 : i32
  llvm.store %t7, %i.14 : i32, !llvm.ptr
  cf.br ^L17
^L17:
  %t8 = llvm.load %i.14 : !llvm.ptr -> i32
  %t9 = llvm.load %n.13 : !llvm.ptr -> i32
  %t10 = arith.cmpi slt, %t8, %t9 : i32
  cf.cond_br %t10, ^L14, ^b1
^b1:
  %t11 = llvm.load %s.15 : !llvm.ptr -> i32
  llvm.store %t11, %retval : i32, !llvm.ptr
  cf.br ^L13
^L13:
  %t12 = llvm.load %retval : !llvm.ptr -> i32
  func.return %t12 : i32
}
func.func private @"use"(!llvm.ptr)
}
