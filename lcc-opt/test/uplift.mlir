// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// RUN: lcc-opt --lcc-uplift-while %s | FileCheck %s
//
// lcc-uplift-while turns a while shaped scf.while into scf.for with
// scf::upliftWhileToForLoop.  Unlike the upstream pattern it requires a
// positive constant step and gives the final induction variable the value
// the scf.while gives: the first one not less than the bound, or the lower
// bound when the loop does not run.  The upstream pattern gives the value of
// the last iteration, 9 instead of 12 in @exit_const and -1 instead of 5 in
// @exit_none.

func.func private @sink(i32)

// CHECK-LABEL: func.func @unused(
//  CHECK-SAME:     %[[LB:.*]]: i32, %[[UB:.*]]: i32)
//       CHECK:   %[[C1:.*]] = arith.constant 1 : i32
//  CHECK-NEXT:   scf.for %[[I:.*]] = %[[LB]] to %[[UB]] step %[[C1]] : i32 {
//  CHECK-NEXT:     func.call @sink(%[[I]]) : (i32) -> ()
//  CHECK-NEXT:   }
//  CHECK-NEXT:   return
func.func @unused(%lb: i32, %ub: i32) {
  %c1 = arith.constant 1 : i32
  %r = scf.while (%i = %lb) : (i32) -> i32 {
    %c = arith.cmpi slt, %i, %ub : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    func.call @sink(%a) : (i32) -> ()
    %n = arith.addi %a, %c1 : i32
    scf.yield %n : i32
  }
  return
}

// The final value of i in 0, 3, 6, 9 is 12.
// CHECK-LABEL: func.func @exit_const(
//       CHECK:   scf.for
//       CHECK:   }
//  CHECK-NEXT:   %[[E:.*]] = arith.constant 12 : i32
//  CHECK-NEXT:   return %[[E]] : i32
func.func @exit_const() -> i32 {
  %c0 = arith.constant 0 : i32
  %c3 = arith.constant 3 : i32
  %c10 = arith.constant 10 : i32
  %r = scf.while (%i = %c0) : (i32) -> i32 {
    %c = arith.cmpi slt, %i, %c10 : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    func.call @sink(%a) : (i32) -> ()
    %n = arith.addi %a, %c3 : i32
    scf.yield %n : i32
  }
  return %r : i32
}

// No iteration: the final value is the lower bound, 5.
// CHECK-LABEL: func.func @exit_none(
//       CHECK:   scf.for
//       CHECK:   }
//  CHECK-NEXT:   %[[E:.*]] = arith.constant 5 : i32
//  CHECK-NEXT:   return %[[E]] : i32
func.func @exit_none() -> i32 {
  %c5 = arith.constant 5 : i32
  %c1 = arith.constant 1 : i32
  %c0 = arith.constant 0 : i32
  %r = scf.while (%i = %c5) : (i32) -> i32 {
    %c = arith.cmpi slt, %i, %c0 : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    func.call @sink(%a) : (i32) -> ()
    %n = arith.addi %a, %c1 : i32
    scf.yield %n : i32
  }
  return %r : i32
}

// Bounds not constant, another loop-carried value, sgt and the step on the
// left: the scf.for carries i + 2 from the lower bound.
// CHECK-LABEL: func.func @exit_carried(
//  CHECK-SAME:     %[[LB:.*]]: i32, %[[UB:.*]]: i32)
//       CHECK:   %[[C0:.*]] = arith.constant 0 : i32
//       CHECK:   %[[C2:.*]] = arith.constant 2 : i32
//  CHECK-NEXT:   %[[R:.*]]:2 = scf.for %[[I:.*]] = %[[LB]] to %[[UB]] step %[[C2]] iter_args(%[[S:.*]] = %[[C0]], %{{.*}} = %[[LB]]) -> (i32, i32) : i32 {
//  CHECK-NEXT:     %[[SN:.*]] = arith.addi %[[S]], %[[I]] : i32
//  CHECK-NEXT:     %[[IN:.*]] = arith.addi %[[C2]], %[[I]] : i32
//  CHECK-NEXT:     scf.yield %[[SN]], %[[IN]] : i32, i32
//  CHECK-NEXT:   }
//  CHECK-NEXT:   return %[[R]]#0, %[[R]]#1 : i32, i32
func.func @exit_carried(%lb: i32, %ub: i32) -> (i32, i32) {
  %c0 = arith.constant 0 : i32
  %c2 = arith.constant 2 : i32
  %r:2 = scf.while (%s = %c0, %i = %lb) : (i32, i32) -> (i32, i32) {
    %c = arith.cmpi sgt, %ub, %i : i32
    scf.condition(%c) %s, %i : i32, i32
  } do {
  ^bb0(%t: i32, %a: i32):
    %t2 = arith.addi %t, %a : i32
    %n = arith.addi %c2, %a : i32
    scf.yield %t2, %n : i32, i32
  }
  return %r#0, %r#1 : i32, i32
}

// The final value, 2^31, does not fit in i32: scf.for leaves the loop
// undefined, as C leaves int overflow.  The scf.for carries i + 2.
// CHECK-LABEL: func.func @exit_overflow(
//       CHECK:   %[[R:.*]] = scf.for %[[I:.*]] = {{.*}} iter_args(
//       CHECK:   return %[[R]] : i32
func.func @exit_overflow() -> i32 {
  %c0 = arith.constant 0 : i32
  %c2 = arith.constant 2 : i32
  %max = arith.constant 2147483647 : i32
  %r = scf.while (%i = %c0) : (i32) -> i32 {
    %c = arith.cmpi slt, %i, %max : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    %n = arith.addi %a, %c2 : i32
    scf.yield %n : i32
  }
  return %r : i32
}

// The pass works in llvm.func too.
// CHECK-LABEL: llvm.func @in_llvm_func(
//       CHECK:   scf.for
//  CHECK-NEXT:     llvm.call @lsink
llvm.func @lsink(i32)
llvm.func @in_llvm_func(%ub: i32) -> i32 {
  %c0 = arith.constant 0 : i32
  %c1 = arith.constant 1 : i32
  %r = scf.while (%i = %c0) : (i32) -> i32 {
    %c = arith.cmpi slt, %i, %ub : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    llvm.call @lsink(%a) : (i32) -> ()
    %n = arith.addi %a, %c1 : i32
    scf.yield %n : i32
  }
  llvm.return %r : i32
}

// The step is an argument, which may be zero or negative at run time:
// scf.for requires a positive step.
// CHECK-LABEL: func.func @step_arg(
//   CHECK-NOT:   scf.for
//       CHECK:   scf.while
func.func @step_arg(%s: i32, %ub: i32) {
  %c0 = arith.constant 0 : i32
  %r = scf.while (%i = %c0) : (i32) -> i32 {
    %c = arith.cmpi slt, %i, %ub : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    func.call @sink(%a) : (i32) -> ()
    %n = arith.addi %a, %s : i32
    scf.yield %n : i32
  }
  return
}

// The step is negative.
// CHECK-LABEL: func.func @step_negative(
//   CHECK-NOT:   scf.for
//       CHECK:   scf.while
func.func @step_negative(%ub: i32) {
  %c0 = arith.constant 0 : i32
  %cm1 = arith.constant -1 : i32
  %r = scf.while (%i = %c0) : (i32) -> i32 {
    %c = arith.cmpi slt, %i, %ub : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    func.call @sink(%a) : (i32) -> ()
    %n = arith.addi %a, %cm1 : i32
    scf.yield %n : i32
  }
  return
}
