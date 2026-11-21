// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// RUN: lcc-opt --lcc-distinct-successors --split-input-file %s | FileCheck %s
// RUN: lcc-opt --lcc-lower --split-input-file %s | FileCheck %s --check-prefix=LOWER
//
// lcc-distinct-successors gives the second and later occurrences of a
// successor with arguments their own blocks, appended in the order of the
// successors, which mlir-translate (MLIR 23) would create in an order that
// changes from run to run.

// A switch whose cases pass values to two blocks twice each: case 1 gets
// the first new block, case 3 the second, each branching on with its
// arguments; the default, without arguments, stays.

// CHECK-LABEL: llvm.func @two(
//       CHECK:   llvm.switch %{{.*}} : i32, ^[[D:bb[0-9]+]] [
//  CHECK-NEXT:     0: ^[[A:bb[0-9]+]](%{{.*}} : i32),
//  CHECK-NEXT:     1: ^[[A2:bb[0-9]+]](%{{.*}} : i32),
//  CHECK-NEXT:     2: ^[[B:bb[0-9]+]](%{{.*}} : i32),
//  CHECK-NEXT:     3: ^[[B2:bb[0-9]+]](%{{.*}} : i32),
//  CHECK-NEXT:     4: ^[[D]]
//  CHECK-NEXT:   ]
//       CHECK: ^[[A2]](%[[X:.*]]: i32):
//  CHECK-NEXT:   llvm.br ^[[A]](%[[X]] : i32)
//       CHECK: ^[[B2]](%[[Y:.*]]: i32):
//  CHECK-NEXT:   llvm.br ^[[B]](%[[Y]] : i32)
llvm.func @two(%x: i32) -> i32 {
  %c1 = llvm.mlir.constant(1 : i32) : i32
  %c2 = llvm.mlir.constant(2 : i32) : i32
  %c3 = llvm.mlir.constant(3 : i32) : i32
  %c4 = llvm.mlir.constant(4 : i32) : i32
  llvm.switch %x : i32, ^d [
    0: ^a(%c1 : i32),
    1: ^a(%c2 : i32),
    2: ^b(%c3 : i32),
    3: ^b(%c4 : i32),
    4: ^d
  ]
^a(%p: i32):
  llvm.return %p : i32
^b(%q: i32):
  %r = llvm.add %q, %c1 : i32
  llvm.return %r : i32
^d:
  llvm.return %c4 : i32
}

// -----

// Three occurrences of one block, and a conditional branch to one block
// twice: new blocks for all but the first, in order.

// CHECK-LABEL: llvm.func @three(
//       CHECK:   llvm.switch %{{.*}} : i32, ^[[A:bb[0-9]+]](%{{.*}} : i32) [
//  CHECK-NEXT:     0: ^[[A1:bb[0-9]+]](%{{.*}} : i32),
//  CHECK-NEXT:     1: ^[[A2:bb[0-9]+]](%{{.*}} : i32)
//  CHECK-NEXT:   ]
//       CHECK:   llvm.cond_br %{{.*}}, ^[[E:bb[0-9]+]](%{{.*}} : i32), ^[[E1:bb[0-9]+]](%{{.*}} : i32)
//       CHECK: ^[[A1]](%[[X:.*]]: i32):
//  CHECK-NEXT:   llvm.br ^[[A]](%[[X]] : i32)
//       CHECK: ^[[A2]](%[[Y:.*]]: i32):
//  CHECK-NEXT:   llvm.br ^[[A]](%[[Y]] : i32)
//       CHECK: ^[[E1]](%[[Z:.*]]: i32):
//  CHECK-NEXT:   llvm.br ^[[E]](%[[Z]] : i32)
llvm.func @three(%x: i32, %c: i1) -> i32 {
  %c1 = llvm.mlir.constant(1 : i32) : i32
  %c2 = llvm.mlir.constant(2 : i32) : i32
  %c3 = llvm.mlir.constant(3 : i32) : i32
  llvm.switch %x : i32, ^a(%c1 : i32) [
    0: ^a(%c2 : i32),
    1: ^a(%c3 : i32)
  ]
^a(%p: i32):
  llvm.cond_br %c, ^e(%p : i32), ^e(%c1 : i32)
^e(%q: i32):
  llvm.return %q : i32
}

// -----

// Repeated successors without arguments, and distinct ones, stay.

// CHECK-LABEL: llvm.func @same(
//       CHECK:   llvm.cond_br %{{.*}}, ^[[R:bb[0-9]+]], ^[[R]]
//       CHECK:   llvm.switch %{{.*}} : i32, ^{{bb[0-9]+}}(%{{.*}} : i32) [
//  CHECK-NEXT:     0: ^{{bb[0-9]+}}(%{{.*}} : i32)
//  CHECK-NEXT:   ]
//   CHECK-NOT:   llvm.br
llvm.func @same(%x: i32, %c: i1) -> i32 {
  %c1 = llvm.mlir.constant(1 : i32) : i32
  llvm.cond_br %c, ^r, ^r
^r:
  llvm.switch %x : i32, ^s(%c1 : i32) [
    0: ^t(%c1 : i32)
  ]
^s(%p: i32):
  llvm.return %p : i32
^t(%q: i32):
  llvm.return %q : i32
}

// -----

// lcc-lower runs it last: a cf.switch of a func.func, as lcc-prepare leaves
// lcc's switches with the values of the cases as block arguments.

// CHECK-LABEL: func.func @lowered(
// LOWER-LABEL: llvm.func @lowered(
//       LOWER:   llvm.switch %{{.*}} : i32, ^{{bb[0-9]+}} [
//  LOWER-NEXT:     0: ^[[A:bb[0-9]+]](%{{.*}} : i32),
//  LOWER-NEXT:     1: ^[[A2:bb[0-9]+]](%{{.*}} : i32),
//  LOWER-NEXT:     2: ^[[B:bb[0-9]+]](%{{.*}} : i32),
//  LOWER-NEXT:     3: ^[[B2:bb[0-9]+]](%{{.*}} : i32)
//  LOWER-NEXT:   ]
//       LOWER: ^[[A2]](%[[X:.*]]: i32):
//  LOWER-NEXT:   llvm.br ^[[A]](%[[X]] : i32)
//       LOWER: ^[[B2]](%[[Y:.*]]: i32):
//  LOWER-NEXT:   llvm.br ^[[B]](%[[Y]] : i32)
func.func @lowered(%x: i32) -> i32 {
  %c1 = arith.constant 1 : i32
  %c2 = arith.constant 2 : i32
  %c3 = arith.constant 3 : i32
  %c4 = arith.constant 4 : i32
  cf.switch %x : i32, [
    default: ^d,
    0: ^a(%c1 : i32),
    1: ^a(%c2 : i32),
    2: ^b(%c3 : i32),
    3: ^b(%c4 : i32)
  ]
^a(%p: i32):
  return %p : i32
^b(%q: i32):
  %r = arith.addi %q, %c1 : i32
  return %r : i32
^d:
  return %c4 : i32
}
