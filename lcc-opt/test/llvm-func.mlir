// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// RUN: lcc-opt --lcc-to-scf --split-input-file %s | FileCheck %s
// RUN: lcc-opt --lcc-lift-llvm-func --split-input-file %s \
// RUN:   | FileCheck %s --check-prefix=LIFT
//
// lcc-lift-llvm-func lifts the control flow of llvm.func, which
// lift-cf-to-scf leaves alone.  The input is what lcc f2445d8
// (mlir/x86_64-linux) gives for llvmfunc2.c,
//
//   #include <stdarg.h>
//
//   int sum(int n, ...)
//   {
//   	va_list ap;
//   	int i, s;
//
//   	s = 0;
//   	va_start(ap, n);
//   	for (i = 0; i < n; i++)
//   		s += va_arg(ap, int);
//   	va_end(ap);
//   	return s;
//   }
//
//   void spin(int *p)
//   {
//   	for (;;)
//   		*p = 0;
//   }
//
//   int count(int *p)
//   {
//   	for (;;)
//   		*p += 1;
//   }
//
//   int classify(int n)
//   {
//   	int i, s;
//
//   	s = 0;
//   	for (i = 0; i < n; i++)
//   		switch (i & 3) {
//   		case 0: s += 1; break;
//   		case 1: s += 3; break;
//   		default: s -= 1; break;
//   		}
//   	return s;
//   }
//
//   int fixed(int *x)
//   {
//   	int i, s;
//
//   	s = 0;
//   	for (i = 0; i < 8; i++)
//   		s += x[i];
//   	return s;
//   }
//
// made in the top directory of lcc with
//
//   make -r BUILDDIR=$B HOSTFILE=etc/mlir-x86_64-linux.c rcc lcc cpp headers
//   LCCDIR=$B $B/lcc -S -Wf-llvmfunc llvmfunc2.c
//
// -Wf-llvmfunc makes every function an llvm.func; sum, variadic, is one in
// any case.  A statically infinite loop ends with an llvm.return, of poison
// when the function returns a value.

// CHECK-LABEL: llvm.func @sum(
//       CHECK:   scf.for
//       CHECK:   llvm.return
// CHECK-LABEL: llvm.func @spin(
//       CHECK:   scf.while
//       CHECK:   llvm.return{{$}}
// CHECK-LABEL: llvm.func @count(
//       CHECK:   %[[U:.*]] = ub.poison : i32
//       CHECK:   scf.while
//       CHECK:   llvm.return %[[U]] : i32
// CHECK-LABEL: llvm.func @classify(
//       CHECK:   scf.for
//       CHECK:     scf.if
//       CHECK:   llvm.return
// CHECK-LABEL: llvm.func @fixed(
//       CHECK:   scf.for %{{.*}} = %{{.*}} to %c8_i32 step %c1_i32
//       CHECK:   llvm.return
//   CHECK-NOT:   {{ cf\.}}

// Alone, on lcc's output with its allocas, the pass makes the loops
// scf.while.
// LIFT-LABEL: llvm.func @sum(
//   LIFT-NOT:   {{ cf\.}}
//       LIFT:   scf.while
// LIFT-LABEL: llvm.func @fixed(
//   LIFT-NOT:   {{ cf\.}}
//       LIFT:   scf.while
//   LIFT-NOT:   {{ cf\.}}

module attributes {llvm.data_layout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128", llvm.target_triple = "x86_64-pc-linux-gnu"} {
llvm.func @"sum"(%a0: i32, ...) -> i32 {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %c40_i32 = arith.constant 40 : i32
  %c8_i32 = arith.constant 8 : i32
  %retval = llvm.alloca %c1_i32 x i32 : (i32) -> !llvm.ptr
  %n.1 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %a0, %n.1 : i32, !llvm.ptr
  %n.2 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %i.3 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %ap.4 = llvm.alloca %c1_i32 x !llvm.array<24 x i8> {alignment = 8 : i64} : (i32) -> !llvm.ptr
  %s.5 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %t1 = llvm.load %n.1 : !llvm.ptr -> i32
  llvm.store %t1, %n.2 : i32, !llvm.ptr
  llvm.store %c0_i32, %s.5 : i32, !llvm.ptr
  llvm.intr.vastart %ap.4 : !llvm.ptr
  llvm.store %c0_i32, %i.3 : i32, !llvm.ptr
  cf.br ^L5
^L2:
  %t2 = llvm.load %ap.4 : !llvm.ptr -> i32
  %t3 = arith.cmpi ule, %t2, %c40_i32 : i32
  cf.cond_br %t3, ^b3, ^b1
^b3:
  %t6 = llvm.getelementptr %ap.4[16] : (!llvm.ptr) -> !llvm.ptr, i8
  %t5 = llvm.load %t6 : !llvm.ptr -> !llvm.ptr
  %t7 = llvm.getelementptr %t5[%t2] : (!llvm.ptr, i32) -> !llvm.ptr, i8
  %t8 = arith.addi %t2, %c8_i32 : i32
  llvm.store %t8, %ap.4 : i32, !llvm.ptr
  cf.br ^b2(%t7 : !llvm.ptr)
^b1:
  %t9 = llvm.getelementptr %ap.4[8] : (!llvm.ptr) -> !llvm.ptr, i8
  %t10 = llvm.load %t9 : !llvm.ptr -> !llvm.ptr
  %t11 = llvm.getelementptr %t10[8] : (!llvm.ptr) -> !llvm.ptr, i8
  llvm.store %t11, %t9 : !llvm.ptr, !llvm.ptr
  cf.br ^b2(%t10 : !llvm.ptr)
^b2(%t4: !llvm.ptr):
  %t12 = llvm.load %s.5 : !llvm.ptr -> i32
  %t13 = llvm.load %t4 : !llvm.ptr -> i32
  %t14 = arith.addi %t12, %t13 : i32
  llvm.store %t14, %s.5 : i32, !llvm.ptr
  cf.br ^L3
^L3:
  %t15 = llvm.load %i.3 : !llvm.ptr -> i32
  %t16 = arith.addi %t15, %c1_i32 : i32
  llvm.store %t16, %i.3 : i32, !llvm.ptr
  cf.br ^L5
^L5:
  %t17 = llvm.load %i.3 : !llvm.ptr -> i32
  %t18 = llvm.load %n.2 : !llvm.ptr -> i32
  %t19 = arith.cmpi slt, %t17, %t18 : i32
  cf.cond_br %t19, ^L2, ^b4
^b4:
  llvm.intr.vaend %ap.4 : !llvm.ptr
  %t20 = llvm.load %s.5 : !llvm.ptr -> i32
  llvm.store %t20, %retval : i32, !llvm.ptr
  cf.br ^L1
^L1:
  %t21 = llvm.load %retval : !llvm.ptr -> i32
  llvm.return %t21 : i32
}
llvm.func @"spin"(%a0: !llvm.ptr) {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %p.6 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  llvm.store %a0, %p.6 : !llvm.ptr, !llvm.ptr
  %p.7 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  %t1 = llvm.load %p.6 : !llvm.ptr -> !llvm.ptr
  llvm.store %t1, %p.7 : !llvm.ptr, !llvm.ptr
  cf.br ^L7
^L7:
  %t2 = llvm.load %p.7 : !llvm.ptr -> !llvm.ptr
  llvm.store %c0_i32, %t2 : i32, !llvm.ptr
  cf.br ^L7
}
llvm.func @"count"(%a0: !llvm.ptr) -> i32 {
  %c1_i32 = arith.constant 1 : i32
  %retval = llvm.alloca %c1_i32 x i32 : (i32) -> !llvm.ptr
  %p.8 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  llvm.store %a0, %p.8 : !llvm.ptr, !llvm.ptr
  %p.9 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  %t1 = llvm.load %p.8 : !llvm.ptr -> !llvm.ptr
  llvm.store %t1, %p.9 : !llvm.ptr, !llvm.ptr
  cf.br ^L12
^L12:
  %t2 = llvm.load %p.9 : !llvm.ptr -> !llvm.ptr
  %t3 = llvm.load %t2 : !llvm.ptr -> i32
  %t4 = arith.addi %t3, %c1_i32 : i32
  llvm.store %t4, %t2 : i32, !llvm.ptr
  cf.br ^L12
}
llvm.func @"classify"(%a0: i32) -> i32 {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %c3_i32 = arith.constant 3 : i32
  %retval = llvm.alloca %c1_i32 x i32 : (i32) -> !llvm.ptr
  %n.10 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %a0, %n.10 : i32, !llvm.ptr
  %n.11 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %i.12 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %s.13 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %T23.14 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %t1 = llvm.load %n.10 : !llvm.ptr -> i32
  llvm.store %t1, %n.11 : i32, !llvm.ptr
  llvm.store %c0_i32, %s.13 : i32, !llvm.ptr
  llvm.store %c0_i32, %i.12 : i32, !llvm.ptr
  cf.br ^L20
^L17:
  %t2 = llvm.load %i.12 : !llvm.ptr -> i32
  %t3 = arith.andi %t2, %c3_i32 : i32
  llvm.store %t3, %T23.14 : i32, !llvm.ptr
  %t4 = llvm.load %T23.14 : !llvm.ptr -> i32
  %t5 = arith.cmpi eq, %t4, %c0_i32 : i32
  cf.cond_br %t5, ^L24, ^b1
^b1:
  %t6 = arith.cmpi eq, %t4, %c1_i32 : i32
  cf.cond_br %t6, ^L25, ^b2
^b2:
  cf.br ^L21
^L24:
  %t7 = llvm.load %s.13 : !llvm.ptr -> i32
  %t8 = arith.addi %t7, %c1_i32 : i32
  llvm.store %t8, %s.13 : i32, !llvm.ptr
  cf.br ^L22
^L25:
  %t9 = llvm.load %s.13 : !llvm.ptr -> i32
  %t10 = arith.addi %t9, %c3_i32 : i32
  llvm.store %t10, %s.13 : i32, !llvm.ptr
  cf.br ^L22
^L21:
  %t11 = llvm.load %s.13 : !llvm.ptr -> i32
  %t12 = arith.subi %t11, %c1_i32 : i32
  llvm.store %t12, %s.13 : i32, !llvm.ptr
  cf.br ^L22
^L22:
  cf.br ^L18
^L18:
  %t13 = llvm.load %i.12 : !llvm.ptr -> i32
  %t14 = arith.addi %t13, %c1_i32 : i32
  llvm.store %t14, %i.12 : i32, !llvm.ptr
  cf.br ^L20
^L20:
  %t15 = llvm.load %i.12 : !llvm.ptr -> i32
  %t16 = llvm.load %n.11 : !llvm.ptr -> i32
  %t17 = arith.cmpi slt, %t15, %t16 : i32
  cf.cond_br %t17, ^L17, ^b3
^b3:
  %t18 = llvm.load %s.13 : !llvm.ptr -> i32
  llvm.store %t18, %retval : i32, !llvm.ptr
  cf.br ^L16
^L16:
  %t19 = llvm.load %retval : !llvm.ptr -> i32
  llvm.return %t19 : i32
}
llvm.func @"fixed"(%a0: !llvm.ptr) -> i32 {
  %c1_i32 = arith.constant 1 : i32
  %c0_i32 = arith.constant 0 : i32
  %c2_i64 = arith.constant 2 : i64
  %c8_i32 = arith.constant 8 : i32
  %retval = llvm.alloca %c1_i32 x i32 : (i32) -> !llvm.ptr
  %x.15 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  llvm.store %a0, %x.15 : !llvm.ptr, !llvm.ptr
  %x.16 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  %i.17 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %s.18 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %t1 = llvm.load %x.15 : !llvm.ptr -> !llvm.ptr
  llvm.store %t1, %x.16 : !llvm.ptr, !llvm.ptr
  llvm.store %c0_i32, %s.18 : i32, !llvm.ptr
  llvm.store %c0_i32, %i.17 : i32, !llvm.ptr
  cf.br ^L27
^L27:
  %t2 = llvm.load %s.18 : !llvm.ptr -> i32
  %t4 = llvm.load %x.16 : !llvm.ptr -> !llvm.ptr
  %t5 = llvm.load %i.17 : !llvm.ptr -> i32
  %t6 = arith.extsi %t5 : i32 to i64
  %t7 = arith.shli %t6, %c2_i64 : i64
  %t3 = llvm.getelementptr %t4[%t7] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %t8 = llvm.load %t3 : !llvm.ptr -> i32
  %t9 = arith.addi %t2, %t8 : i32
  llvm.store %t9, %s.18 : i32, !llvm.ptr
  cf.br ^L28
^L28:
  %t10 = llvm.load %i.17 : !llvm.ptr -> i32
  %t11 = arith.addi %t10, %c1_i32 : i32
  llvm.store %t11, %i.17 : i32, !llvm.ptr
  %t12 = llvm.load %i.17 : !llvm.ptr -> i32
  %t13 = arith.cmpi slt, %t12, %c8_i32 : i32
  cf.cond_br %t13, ^L27, ^b1
^b1:
  %t14 = llvm.load %s.18 : !llvm.ptr -> i32
  llvm.store %t14, %retval : i32, !llvm.ptr
  cf.br ^L26
^L26:
  %t15 = llvm.load %retval : !llvm.ptr -> i32
  llvm.return %t15 : i32
}
}

// -----

// A function with branches other than cf (llvm.br here) is left alone.
// CHECK-LABEL: llvm.func @raw(
//   CHECK-NOT:   scf.
//       CHECK:   llvm.br
//       CHECK:   llvm.cond_br
// LIFT-LABEL: llvm.func @raw(
//   LIFT-NOT:   scf.
//       LIFT:   llvm.br
//       LIFT:   llvm.cond_br
llvm.func @raw(%n: i32) -> i32 {
  %c0 = llvm.mlir.constant(0 : i32) : i32
  %c1 = llvm.mlir.constant(1 : i32) : i32
  llvm.br ^bb1(%c0 : i32)
^bb1(%i: i32):
  %c = llvm.icmp "slt" %i, %n : i32
  llvm.cond_br %c, ^bb2, ^bb3
^bb2:
  %j = llvm.add %i, %c1 : i32
  llvm.br ^bb1(%j : i32)
^bb3:
  llvm.return %i : i32
}
