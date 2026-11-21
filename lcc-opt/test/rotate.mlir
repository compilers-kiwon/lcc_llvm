// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// RUN: lcc-opt --lcc-rotate-do-while %s | FileCheck %s
//
// lcc-rotate-do-while moves the body of a do-while shaped scf.while into
// the after region when the first test folds to true, whatever the
// predicate and the update, and leaves every other loop alone.

func.func private @sink(i32)
func.func private @sink64(i64)

// for (i = 0; i < 16; i++) sink(i);
// CHECK-LABEL: func.func @basic(
//       CHECK:   %[[C0:.*]] = arith.constant 0 : i32
//       CHECK:   %[[C1:.*]] = arith.constant 1 : i32
//       CHECK:   %[[C16:.*]] = arith.constant 16 : i32
//       CHECK:   %[[R:.*]] = scf.while (%[[I:.*]] = %[[C0]]) : (i32) -> i32 {
//  CHECK-NEXT:     %[[T:.*]] = arith.cmpi slt, %[[I]], %[[C16]] : i32
//  CHECK-NEXT:     scf.condition(%[[T]]) %[[I]] : i32
//  CHECK-NEXT:   } do {
//  CHECK-NEXT:   ^bb0(%[[A:.*]]: i32):
//  CHECK-NEXT:     func.call @sink(%[[A]]) : (i32) -> ()
//  CHECK-NEXT:     %[[N:.*]] = arith.addi %[[A]], %[[C1]] : i32
//  CHECK-NEXT:     scf.yield %[[N]] : i32
//  CHECK-NEXT:   }
//  CHECK-NEXT:   return %[[R]] : i32
func.func @basic() -> i32 {
  %c0 = arith.constant 0 : i32
  %c1 = arith.constant 1 : i32
  %c16 = arith.constant 16 : i32
  %r = scf.while (%i = %c0) : (i32) -> i32 {
    func.call @sink(%i) : (i32) -> ()
    %n = arith.addi %i, %c1 : i32
    %c = arith.cmpi slt, %n, %c16 : i32
    scf.condition(%c) %n : i32
  } do {
  ^bb0(%a: i32):
    scf.yield %a : i32
  }
  return %r : i32
}

// Three loop-carried values, passed on in another order, and the step on
// the left of the addi.  The results follow the values: the old result 0
// (s + i) is the new result 1 (s).
// CHECK-LABEL: func.func @several(
//  CHECK-SAME:     %[[X:.*]]: i32)
//       CHECK:   %[[C0:.*]] = arith.constant 0 : i32
//       CHECK:   %[[C1:.*]] = arith.constant 1 : i32
//       CHECK:   %[[C2:.*]] = arith.constant 2 : i32
//       CHECK:   %[[C3:.*]] = arith.constant 3 : i32
//       CHECK:   %[[C9:.*]] = arith.constant 9 : i32
//       CHECK:   %[[R:.*]]:3 = scf.while (%[[I:.*]] = %[[C0]], %[[S:.*]] = %[[X]], %[[P:.*]] = %[[C1]]) : (i32, i32, i32) -> (i32, i32, i32) {
//  CHECK-NEXT:     %[[T:.*]] = arith.cmpi slt, %[[I]], %[[C9]] : i32
//  CHECK-NEXT:     scf.condition(%[[T]]) %[[I]], %[[S]], %[[P]] : i32, i32, i32
//  CHECK-NEXT:   } do {
//  CHECK-NEXT:   ^bb0(%[[I2:.*]]: i32, %[[S2:.*]]: i32, %[[P2:.*]]: i32):
//  CHECK-NEXT:     %[[SN:.*]] = arith.addi %[[S2]], %[[I2]] : i32
//  CHECK-NEXT:     %[[PN:.*]] = arith.muli %[[P2]], %[[C3]] : i32
//  CHECK-NEXT:     %[[IN:.*]] = arith.addi %[[C2]], %[[I2]] : i32
//  CHECK-NEXT:     scf.yield %[[IN]], %[[SN]], %[[PN]] : i32, i32, i32
//  CHECK-NEXT:   }
//  CHECK-NEXT:   return %[[R]]#1, %[[R]]#0, %[[R]]#2 : i32, i32, i32
func.func @several(%x: i32) -> (i32, i32, i32) {
  %c0 = arith.constant 0 : i32
  %c1 = arith.constant 1 : i32
  %c2 = arith.constant 2 : i32
  %c3 = arith.constant 3 : i32
  %c9 = arith.constant 9 : i32
  %r:3 = scf.while (%i = %c0, %s = %x, %p = %c1) : (i32, i32, i32) -> (i32, i32, i32) {
    %s2 = arith.addi %s, %i : i32
    %p2 = arith.muli %p, %c3 : i32
    %n = arith.addi %c2, %i : i32
    %c = arith.cmpi slt, %n, %c9 : i32
    scf.condition(%c) %s2, %n, %p2 : i32, i32, i32
  } do {
  ^bb0(%a: i32, %b: i32, %d: i32):
    scf.yield %b, %a, %d : i32, i32, i32
  }
  return %r#0, %r#1, %r#2 : i32, i32, i32
}

// A result the after region does not pass on, computed from values outside
// the loop by pure operations (lcc's row offset of an outer loop): the
// operations move in front of the loop and the result is their value.
// CHECK-LABEL: func.func @hoist(
//  CHECK-SAME:     %[[X:.*]]: i32)
//       CHECK:   %[[C8:.*]] = arith.constant 8 : i32
//       CHECK:   %[[C4:.*]] = arith.constant 4 : i64
//  CHECK-NEXT:   %[[E:.*]] = arith.extsi %[[X]] : i32 to i64
//  CHECK-NEXT:   %[[F:.*]] = arith.shli %[[E]], %[[C4]] : i64
//  CHECK-NEXT:   %[[R:.*]] = scf.while (%[[I:.*]] = %{{.*}}) : (i32) -> i32 {
//  CHECK-NEXT:     arith.cmpi slt, %[[I]], %[[C8]] : i32
//  CHECK-NEXT:     scf.condition
//  CHECK-NEXT:   } do {
//  CHECK-NEXT:   ^bb0(%[[A:.*]]: i32):
//  CHECK-NEXT:     %[[G:.*]] = arith.extsi %[[A]] : i32 to i64
//  CHECK-NEXT:     %[[H:.*]] = arith.addi %[[F]], %[[G]] : i64
//  CHECK-NEXT:     func.call @sink64(%[[H]]) : (i64) -> ()
//  CHECK-NEXT:     %[[N:.*]] = arith.addi %[[A]], %{{.*}} : i32
//  CHECK-NEXT:     scf.yield %[[N]] : i32
//  CHECK-NEXT:   }
//  CHECK-NEXT:   return %[[R]], %[[F]] : i32, i64
func.func @hoist(%x: i32) -> (i32, i64) {
  %c0 = arith.constant 0 : i32
  %c1 = arith.constant 1 : i32
  %c8 = arith.constant 8 : i32
  %c4_i64 = arith.constant 4 : i64
  %r:2 = scf.while (%i = %c0) : (i32) -> (i32, i64) {
    %e = arith.extsi %x : i32 to i64
    %f = arith.shli %e, %c4_i64 : i64
    %g = arith.extsi %i : i32 to i64
    %h = arith.addi %f, %g : i64
    func.call @sink64(%h) : (i64) -> ()
    %n = arith.addi %i, %c1 : i32
    %c = arith.cmpi slt, %n, %c8 : i32
    scf.condition(%c) %n, %f : i32, i64
  } do {
  ^bb0(%a: i32, %b: i64):
    scf.yield %a : i32
  }
  return %r#0, %r#1 : i32, i64
}

// A result the after region does not pass on that changes in every
// iteration: the new loop carries it, from a zero that is never read.
// CHECK-LABEL: func.func @last(
//  CHECK-SAME:     %[[X:.*]]: i32)
//       CHECK:   %[[C0:.*]] = arith.constant 0 : i32
//       CHECK:   %[[C5:.*]] = arith.constant 5 : i32
//       CHECK:   %[[U:.*]] = arith.constant 0 : i32
//       CHECK:   %[[R:.*]]:3 = scf.while (%[[I:.*]] = %[[C0]], %[[S:.*]] = %[[C0]], %[[L:.*]] = %[[U]]) : (i32, i32, i32) -> (i32, i32, i32) {
//  CHECK-NEXT:     %[[T:.*]] = arith.cmpi slt, %[[I]], %[[C5]] : i32
//  CHECK-NEXT:     scf.condition(%[[T]]) %[[I]], %[[S]], %[[L]] : i32, i32, i32
//  CHECK-NEXT:   } do {
//  CHECK-NEXT:   ^bb0(%[[I2:.*]]: i32, %[[S2:.*]]: i32, %{{.*}}: i32):
//  CHECK-NEXT:     %[[M:.*]] = arith.muli %[[I2]], %[[X]] : i32
//  CHECK-NEXT:     %[[SN:.*]] = arith.addi %[[S2]], %[[M]] : i32
//  CHECK-NEXT:     %[[IN:.*]] = arith.addi %[[I2]], %{{.*}} : i32
//  CHECK-NEXT:     scf.yield %[[IN]], %[[SN]], %[[M]] : i32, i32, i32
//  CHECK-NEXT:   }
//  CHECK-NEXT:   return %[[R]]#0, %[[R]]#2, %[[R]]#1 : i32, i32, i32
func.func @last(%x: i32) -> (i32, i32, i32) {
  %c0 = arith.constant 0 : i32
  %c1 = arith.constant 1 : i32
  %c5 = arith.constant 5 : i32
  %r:3 = scf.while (%i = %c0, %s = %c0) : (i32, i32) -> (i32, i32, i32) {
    %t = arith.muli %i, %x : i32
    %s2 = arith.addi %s, %t : i32
    %n = arith.addi %i, %c1 : i32
    %c = arith.cmpi slt, %n, %c5 : i32
    scf.condition(%c) %n, %t, %s2 : i32, i32, i32
  } do {
  ^bb0(%a: i32, %b: i32, %d: i32):
    scf.yield %a, %d : i32, i32
  }
  return %r#0, %r#1, %r#2 : i32, i32, i32
}

// A pointer the after region does not pass on starts from llvm.mlir.zero.
// CHECK-LABEL: func.func @last_ptr(
//       CHECK:   %[[Z:.*]] = llvm.mlir.zero : !llvm.ptr
//       CHECK:   %[[R:.*]]:2 = scf.while (%{{.*}} = %{{.*}}, %{{.*}} = %[[Z]]) : (i32, !llvm.ptr) -> (i32, !llvm.ptr) {
//  CHECK-NEXT:     arith.cmpi slt
//       CHECK:   } do {
//  CHECK-NEXT:   ^bb0(%[[A:.*]]: i32, %{{.*}}: !llvm.ptr):
//  CHECK-NEXT:     %[[Q:.*]] = llvm.getelementptr %{{.*}}[%[[A]]]
//       CHECK:     scf.yield %{{.*}}, %[[Q]] : i32, !llvm.ptr
//       CHECK:   return %[[R]]#0, %[[R]]#1 : i32, !llvm.ptr
func.func @last_ptr(%p: !llvm.ptr) -> (i32, !llvm.ptr) {
  %c0 = arith.constant 0 : i32
  %c1 = arith.constant 1 : i32
  %c4 = arith.constant 4 : i32
  %r:2 = scf.while (%i = %c0) : (i32) -> (i32, !llvm.ptr) {
    %q = llvm.getelementptr %p[%i] : (!llvm.ptr, i32) -> !llvm.ptr, i8
    %n = arith.addi %i, %c1 : i32
    %c = arith.cmpi slt, %n, %c4 : i32
    scf.condition(%c) %n, %q : i32, !llvm.ptr
  } do {
  ^bb0(%a: i32, %b: !llvm.ptr):
    scf.yield %a : i32
  }
  return %r#0, %r#1 : i32, !llvm.ptr
}

// The pass works in llvm.func too.
// CHECK-LABEL: llvm.func @in_llvm_func(
//       CHECK:   scf.while
//  CHECK-NEXT:     arith.cmpi slt
//  CHECK-NEXT:     scf.condition
//  CHECK-NEXT:   } do {
//  CHECK-NEXT:   ^bb0(%[[A:.*]]: i32):
//  CHECK-NEXT:     llvm.call @lsink(%[[A]]) : (i32) -> ()
llvm.func @lsink(i32)
llvm.func @in_llvm_func() -> i32 {
  %c0 = arith.constant 0 : i32
  %c1 = arith.constant 1 : i32
  %c4 = arith.constant 4 : i32
  %r = scf.while (%i = %c0) : (i32) -> i32 {
    llvm.call @lsink(%i) : (i32) -> ()
    %n = arith.addi %i, %c1 : i32
    %c = arith.cmpi slt, %n, %c4 : i32
    scf.condition(%c) %n : i32
  } do {
  ^bb0(%a: i32):
    scf.yield %a : i32
  }
  llvm.return %r : i32
}

// The loops below stay as they are, with the body in the before region,
// except those marked rotated: any predicate and any update of i rotate.

// The bound is an argument: the first test cannot be shown true.  This is
// also what a C do-while loop looks like, whose body runs at least once.
// CHECK-LABEL: func.func @bound_arg(
//       CHECK:   scf.while
//  CHECK-NEXT:     func.call @sink
func.func @bound_arg(%ub: i32) -> i32 {
  %c0 = arith.constant 0 : i32
  %c1 = arith.constant 1 : i32
  %r = scf.while (%i = %c0) : (i32) -> i32 {
    func.call @sink(%i) : (i32) -> ()
    %n = arith.addi %i, %c1 : i32
    %c = arith.cmpi slt, %n, %ub : i32
    scf.condition(%c) %n : i32
  } do {
  ^bb0(%a: i32):
    scf.yield %a : i32
  }
  return %r : i32
}

// The first test folds to false.
// CHECK-LABEL: func.func @first_false(
//       CHECK:   scf.while
//  CHECK-NEXT:     func.call @sink
func.func @first_false() -> i32 {
  %c1 = arith.constant 1 : i32
  %c16 = arith.constant 16 : i32
  %r = scf.while (%i = %c16) : (i32) -> i32 {
    func.call @sink(%i) : (i32) -> ()
    %n = arith.addi %i, %c1 : i32
    %c = arith.cmpi slt, %n, %c16 : i32
    scf.condition(%c) %n : i32
  } do {
  ^bb0(%a: i32):
    scf.yield %a : i32
  }
  return %r : i32
}

// The initial value is an argument.
// CHECK-LABEL: func.func @init_arg(
//       CHECK:   scf.while
//  CHECK-NEXT:     func.call @sink
func.func @init_arg(%x: i32) -> i32 {
  %c1 = arith.constant 1 : i32
  %c16 = arith.constant 16 : i32
  %r = scf.while (%i = %x) : (i32) -> i32 {
    func.call @sink(%i) : (i32) -> ()
    %n = arith.addi %i, %c1 : i32
    %c = arith.cmpi slt, %n, %c16 : i32
    scf.condition(%c) %n : i32
  } do {
  ^bb0(%a: i32):
    scf.yield %a : i32
  }
  return %r : i32
}

// Rotated: i <= 16.
// CHECK-LABEL: func.func @sle(
//       CHECK:   scf.while
//  CHECK-NEXT:     arith.cmpi sle, %{{.*}}, %{{.*}} : i32
//  CHECK-NEXT:     scf.condition
//  CHECK-NEXT:   } do {
//  CHECK-NEXT:   ^bb0(
//  CHECK-NEXT:     func.call @sink
func.func @sle() -> i32 {
  %c0 = arith.constant 0 : i32
  %c1 = arith.constant 1 : i32
  %c16 = arith.constant 16 : i32
  %r = scf.while (%i = %c0) : (i32) -> i32 {
    func.call @sink(%i) : (i32) -> ()
    %n = arith.addi %i, %c1 : i32
    %c = arith.cmpi sle, %n, %c16 : i32
    scf.condition(%c) %n : i32
  } do {
  ^bb0(%a: i32):
    scf.yield %a : i32
  }
  return %r : i32
}

// Rotated: i != 16.
// CHECK-LABEL: func.func @ne(
//       CHECK:   scf.while
//  CHECK-NEXT:     arith.cmpi ne, %{{.*}}, %{{.*}} : i32
//  CHECK-NEXT:     scf.condition
//  CHECK-NEXT:   } do {
//  CHECK-NEXT:   ^bb0(
//  CHECK-NEXT:     func.call @sink
func.func @ne() -> i32 {
  %c0 = arith.constant 0 : i32
  %c1 = arith.constant 1 : i32
  %c16 = arith.constant 16 : i32
  %r = scf.while (%i = %c0) : (i32) -> i32 {
    func.call @sink(%i) : (i32) -> ()
    %n = arith.addi %i, %c1 : i32
    %c = arith.cmpi ne, %n, %c16 : i32
    scf.condition(%c) %n : i32
  } do {
  ^bb0(%a: i32):
    scf.yield %a : i32
  }
  return %r : i32
}

// Rotated: unsigned i < 16.
// CHECK-LABEL: func.func @ult(
//       CHECK:   scf.while
//  CHECK-NEXT:     arith.cmpi ult, %{{.*}}, %{{.*}} : i32
//  CHECK-NEXT:     scf.condition
//  CHECK-NEXT:   } do {
//  CHECK-NEXT:   ^bb0(
//  CHECK-NEXT:     func.call @sink
func.func @ult() -> i32 {
  %c0 = arith.constant 0 : i32
  %c1 = arith.constant 1 : i32
  %c16 = arith.constant 16 : i32
  %r = scf.while (%i = %c0) : (i32) -> i32 {
    func.call @sink(%i) : (i32) -> ()
    %n = arith.addi %i, %c1 : i32
    %c = arith.cmpi ult, %n, %c16 : i32
    scf.condition(%c) %n : i32
  } do {
  ^bb0(%a: i32):
    scf.yield %a : i32
  }
  return %r : i32
}

// Stays: the after region changes the value it passes on.
// CHECK-LABEL: func.func @after_changes(
//       CHECK:   scf.while
//  CHECK-NEXT:     func.call @sink
func.func @after_changes() -> i32 {
  %c0 = arith.constant 0 : i32
  %c1 = arith.constant 1 : i32
  %c16 = arith.constant 16 : i32
  %r = scf.while (%i = %c0) : (i32) -> i32 {
    func.call @sink(%i) : (i32) -> ()
    %n = arith.addi %i, %c1 : i32
    %c = arith.cmpi slt, %n, %c16 : i32
    scf.condition(%c) %n : i32
  } do {
  ^bb0(%a: i32):
    %b = arith.addi %a, %c1 : i32
    scf.yield %b : i32
  }
  return %r : i32
}

// Rotated: the step is an argument; the test is the same.
// CHECK-LABEL: func.func @step_arg(
//       CHECK:   scf.while
//  CHECK-NEXT:     arith.cmpi slt, %{{.*}}, %{{.*}} : i32
//  CHECK-NEXT:     scf.condition
//  CHECK-NEXT:   } do {
//  CHECK-NEXT:   ^bb0(
//  CHECK-NEXT:     func.call @sink
func.func @step_arg(%s: i32) -> i32 {
  %c0 = arith.constant 0 : i32
  %c16 = arith.constant 16 : i32
  %r = scf.while (%i = %c0) : (i32) -> i32 {
    func.call @sink(%i) : (i32) -> ()
    %n = arith.addi %i, %s : i32
    %c = arith.cmpi slt, %n, %c16 : i32
    scf.condition(%c) %n : i32
  } do {
  ^bb0(%a: i32):
    scf.yield %a : i32
  }
  return %r : i32
}

// Rotated: the step is negative; the test is the same.
// CHECK-LABEL: func.func @step_negative(
//       CHECK:   scf.while
//  CHECK-NEXT:     arith.cmpi slt, %{{.*}}, %{{.*}} : i32
//  CHECK-NEXT:     scf.condition
//  CHECK-NEXT:   } do {
//  CHECK-NEXT:   ^bb0(
//  CHECK-NEXT:     func.call @sink
func.func @step_negative() -> i32 {
  %c0 = arith.constant 0 : i32
  %cm1 = arith.constant -1 : i32
  %c16 = arith.constant 16 : i32
  %r = scf.while (%i = %c0) : (i32) -> i32 {
    func.call @sink(%i) : (i32) -> ()
    %n = arith.addi %i, %cm1 : i32
    %c = arith.cmpi slt, %n, %c16 : i32
    scf.condition(%c) %n : i32
  } do {
  ^bb0(%a: i32):
    scf.yield %a : i32
  }
  return %r : i32
}

// A result the after region does not pass on whose type has no zero the
// pass can make (a tensor): the loop stays as it is.
// CHECK-LABEL: func.func @last_tensor(
//       CHECK:   scf.while
//  CHECK-NEXT:     tensor.from_elements
func.func @last_tensor() -> (i32, tensor<2xi32>) {
  %c0 = arith.constant 0 : i32
  %c1 = arith.constant 1 : i32
  %c4 = arith.constant 4 : i32
  %r:2 = scf.while (%i = %c0) : (i32) -> (i32, tensor<2xi32>) {
    %t = tensor.from_elements %i, %i : tensor<2xi32>
    %n = arith.addi %i, %c1 : i32
    %c = arith.cmpi slt, %n, %c4 : i32
    scf.condition(%c) %n, %t : i32, tensor<2xi32>
  } do {
  ^bb0(%a: i32, %b: tensor<2xi32>):
    scf.yield %a : i32
  }
  return %r#0, %r#1 : i32, tensor<2xi32>
}

// Rotated: the bound on the left, 16 > i.
// CHECK-LABEL: func.func @ub_left(
//       CHECK:   %[[C16:.*]] = arith.constant 16 : i32
//       CHECK:   scf.while (%[[I:.*]] = %{{.*}}) : (i32) -> i32 {
//  CHECK-NEXT:     arith.cmpi sgt, %[[C16]], %[[I]] : i32
//  CHECK-NEXT:     scf.condition
func.func @ub_left() -> i32 {
  %c0 = arith.constant 0 : i32
  %c1 = arith.constant 1 : i32
  %c16 = arith.constant 16 : i32
  %r = scf.while (%i = %c0) : (i32) -> i32 {
    func.call @sink(%i) : (i32) -> ()
    %n = arith.addi %i, %c1 : i32
    %c = arith.cmpi sgt, %c16, %n : i32
    scf.condition(%c) %n : i32
  } do {
  ^bb0(%a: i32):
    scf.yield %a : i32
  }
  return %r : i32
}

// Stays: the value tested is not the one passed on to i.
// CHECK-LABEL: func.func @other_value(
//       CHECK:   scf.while
//  CHECK-NEXT:     func.call @sink
func.func @other_value() -> i32 {
  %c0 = arith.constant 0 : i32
  %c1 = arith.constant 1 : i32
  %c16 = arith.constant 16 : i32
  %r = scf.while (%i = %c0) : (i32) -> i32 {
    func.call @sink(%i) : (i32) -> ()
    %n = arith.addi %i, %c1 : i32
    %c = arith.cmpi slt, %n, %c16 : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    scf.yield %a : i32
  }
  return %r : i32
}
