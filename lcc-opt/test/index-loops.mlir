// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// RUN: lcc-opt --lcc-index-loops --split-input-file %s | FileCheck %s
//
// lcc-index-loops makes index loops of integer scf.for loops whose values
// index holds, and leaves the others alone.

// A signed i32 loop: the bounds are cast to index; the body extends the
// induction variable to i64, which becomes a cast of the index one, and
// uses it as an i32, which takes the index one cast back.

// CHECK-LABEL: func.func @signed32(
//  CHECK-SAME:     %[[N:.*]]: i32
//   CHECK-DAG:   %[[LB:.*]] = arith.index_cast %{{.*}} : i32 to index
//   CHECK-DAG:   %[[UB:.*]] = arith.index_cast %[[N]] : i32 to index
//       CHECK:   %[[R:.*]] = scf.for %[[IV:.*]] = %[[LB]] to %[[UB]] step %{{.*}} iter_args(%[[A:.*]] = %{{.*}}) -> (i32) {
//   CHECK-DAG:     %[[I32:.*]] = arith.index_cast %[[IV]] : index to i32
//   CHECK-DAG:     %[[I64:.*]] = arith.index_cast %[[IV]] : index to i64
//       CHECK:     llvm.getelementptr %{{.*}}[%[[I64]]]
//       CHECK:     arith.addi %[[A]], %[[I32]] : i32
//       CHECK:   return %[[R]] : i32
func.func @signed32(%n: i32, %p: !llvm.ptr) -> i32 {
  %c0 = arith.constant 0 : i32
  %c1 = arith.constant 1 : i32
  %r = scf.for %i = %c0 to %n step %c1 iter_args(%a = %c0) -> (i32) : i32 {
    %e = arith.extsi %i : i32 to i64
    %q = llvm.getelementptr %p[%e] : (!llvm.ptr, i64) -> !llvm.ptr, i8
    llvm.store %c0, %q : i32, !llvm.ptr
    %s = arith.addi %a, %i : i32
    scf.yield %s : i32
  }
  return %r : i32
}

// -----

// An unsigned i32 loop: the bounds are zero-extended and the loop is a
// signed index loop; extui of the induction variable becomes index_castui.

// CHECK-LABEL: func.func @unsigned32(
//  CHECK-SAME:     %[[N:.*]]: i32
//       CHECK:   %[[UB:.*]] = arith.index_castui %[[N]] : i32 to index
//       CHECK:   scf.for %[[IV:.*]] = %{{.*}} to %[[UB]] step %{{.*}} {
//       CHECK:     %[[E:.*]] = arith.index_castui %[[IV]] : index to i64
//       CHECK:     llvm.getelementptr %{{.*}}[%[[E]]]
func.func @unsigned32(%n: i32, %p: !llvm.ptr) {
  %c0 = arith.constant 0 : i32
  %c1 = arith.constant 1 : i32
  scf.for unsigned %i = %c0 to %n step %c1 : i32 {
    %e = arith.extui %i : i32 to i64
    %q = llvm.getelementptr %p[%e] : (!llvm.ptr, i64) -> !llvm.ptr, i8
    llvm.store %c0, %q : i32, !llvm.ptr
  }
  return
}

// -----

// Unsigned i64 loops with bounds known to be non-negative as signed i64:
// constants, and a zero-extended i32.

// CHECK-LABEL: func.func @unsigned64(
//   CHECK-NOT:   scf.for unsigned
//       CHECK:   scf.for %{{.*}} = %{{.*}} to %{{.*}} step %{{.*}} {
//       CHECK:   scf.for %{{.*}} = %{{.*}} to %{{.*}} step %{{.*}} {
//   CHECK-NOT:   scf.for unsigned
//       CHECK:   return
func.func @unsigned64(%n: i32, %p: !llvm.ptr) {
  %c0 = arith.constant 0 : i64
  %c1 = arith.constant 1 : i64
  %c100 = arith.constant 100 : i64
  %v = arith.constant 0 : i8
  scf.for unsigned %i = %c0 to %c100 step %c1 : i64 {
    %q = llvm.getelementptr %p[%i] : (!llvm.ptr, i64) -> !llvm.ptr, i8
    llvm.store %v, %q : i8, !llvm.ptr
  }
  %m = arith.extui %n : i32 to i64
  scf.for unsigned %i = %c0 to %m step %c1 : i64 {
    %q = llvm.getelementptr %p[%i] : (!llvm.ptr, i64) -> !llvm.ptr, i8
    llvm.store %v, %q : i8, !llvm.ptr
  }
  return
}

// -----

// Unsigned i64 loops that stay: an upper bound that may be 2^63 or more
// (a parameter, or -1, the largest unsigned value), and a constant upper
// bound below 2^63 with a step that would take the last value past it.

// CHECK-LABEL: func.func @unsigned64_stays(
//       CHECK:   scf.for unsigned %{{.*}} = %{{.*}} to %{{.*}} step %{{.*}} : i64 {
//       CHECK:   scf.for unsigned %{{.*}} = %{{.*}} to %{{.*}} step %{{.*}} : i64 {
//       CHECK:   scf.for unsigned %{{.*}} = %{{.*}} to %{{.*}} step %{{.*}} : i64 {
//   CHECK-NOT:   arith.index_cast
func.func @unsigned64_stays(%n: i64, %p: !llvm.ptr) {
  %c0 = arith.constant 0 : i64
  %c1 = arith.constant 1 : i64
  %c16 = arith.constant 16 : i64
  %cm1 = arith.constant -1 : i64
  %big = arith.constant 9223372036854775800 : i64
  %v = arith.constant 0 : i8
  scf.for unsigned %i = %c0 to %n step %c1 : i64 {
    %q = llvm.getelementptr %p[%i] : (!llvm.ptr, i64) -> !llvm.ptr, i8
    llvm.store %v, %q : i8, !llvm.ptr
  }
  scf.for unsigned %i = %c0 to %cm1 step %c1 : i64 {
    %q = llvm.getelementptr %p[%i] : (!llvm.ptr, i64) -> !llvm.ptr, i8
    llvm.store %v, %q : i8, !llvm.ptr
  }
  scf.for unsigned %i = %c0 to %big step %c16 : i64 {
    %q = llvm.getelementptr %p[%i] : (!llvm.ptr, i64) -> !llvm.ptr, i8
    llvm.store %v, %q : i8, !llvm.ptr
  }
  return
}

// -----

// A signed i64 loop becomes an index loop (index is 64 bits); an i128 loop
// stays.

// CHECK-LABEL: func.func @widths(
//       CHECK:   scf.for %{{.*}} = %{{.*}} to %{{.*}} step %{{.*}} {
//       CHECK:   scf.for %{{.*}} = %{{.*}} to %{{.*}} step %{{.*}} : i128 {
func.func @widths(%n: i64, %m: i128, %p: !llvm.ptr) {
  %c0 = arith.constant 0 : i64
  %c1 = arith.constant 1 : i64
  %d0 = arith.constant 0 : i128
  %d1 = arith.constant 1 : i128
  %v = arith.constant 0 : i8
  scf.for %i = %c0 to %n step %c1 : i64 {
    %q = llvm.getelementptr %p[%i] : (!llvm.ptr, i64) -> !llvm.ptr, i8
    llvm.store %v, %q : i8, !llvm.ptr
  }
  scf.for %i = %d0 to %m step %d1 : i128 {
    %t = arith.trunci %i : i128 to i64
    %q = llvm.getelementptr %p[%t] : (!llvm.ptr, i64) -> !llvm.ptr, i8
    llvm.store %v, %q : i8, !llvm.ptr
  }
  return
}

// -----

// A triangular nest: the inner loop starts at the outer induction
// variable, whose cast to index becomes the index induction variable.

// CHECK-LABEL: func.func @triangle(
//       CHECK:   scf.for %[[I:.*]] = %{{.*}} to %{{.*}} step %{{.*}} {
//       CHECK:     scf.for %{{.*}} = %[[I]] to %{{.*}} step %{{.*}} {
func.func @triangle(%p: !llvm.ptr) {
  %c0 = arith.constant 0 : i32
  %c1 = arith.constant 1 : i32
  %c8 = arith.constant 8 : i32
  %v = arith.constant 0 : i8
  scf.for %i = %c0 to %c8 step %c1 : i32 {
    scf.for %j = %i to %c8 step %c1 : i32 {
      %e = arith.extsi %j : i32 to i64
      %q = llvm.getelementptr %p[%e] : (!llvm.ptr, i64) -> !llvm.ptr, i8
      llvm.store %v, %q : i8, !llvm.ptr
    }
  }
  return
}
