// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// RUN: lcc-opt --lcc-lower %s | FileCheck %s --implicit-check-not=scf. \
// RUN:   --implicit-check-not=ub.poison --implicit-check-not=arith. \
// RUN:   --implicit-check-not=cf. --implicit-check-not=func.
//
// lcc-lower lowers what lcc-to-scf leaves, scf and the ub.poison of
// lift-cf-to-scf among it, to the LLVM dialect, which mlir-translate takes.

// CHECK-LABEL: llvm.func @sum(
//       CHECK:   llvm.mlir.poison : i32
//       CHECK:   llvm.cond_br
//       CHECK:   llvm.return
// CHECK-LABEL: llvm.func @pick(
//       CHECK:   llvm.switch
// CHECK-LABEL: llvm.func @twice(
//       CHECK:   llvm.call @sum(

func.func @sum(%n: i32, %p: !llvm.ptr) -> i32 {
  %c0 = arith.constant 0 : i32
  %c1 = arith.constant 1 : i32
  %u = ub.poison : i32
  %s = scf.for %i = %c0 to %n step %c1 iter_args(%a = %c0) -> (i32) : i32 {
    %q = llvm.getelementptr %p[%i] : (!llvm.ptr, i32) -> !llvm.ptr, i32
    %v = llvm.load %q : !llvm.ptr -> i32
    %b = arith.addi %a, %v : i32
    scf.yield %b : i32
  }
  %pos = arith.cmpi sgt, %n, %c0 : i32
  %r = scf.if %pos -> (i32) {
    scf.yield %s : i32
  } else {
    scf.yield %u : i32
  }
  return %r : i32
}

func.func @pick(%k: index) -> i32 {
  %r = scf.index_switch %k -> i32
  case 1 {
    %c7 = arith.constant 7 : i32
    scf.yield %c7 : i32
  }
  default {
    %c9 = arith.constant 9 : i32
    scf.yield %c9 : i32
  }
  return %r : i32
}

llvm.func @twice(%n: i32, %p: !llvm.ptr) -> i32 {
  %r = func.call @sum(%n, %p) : (i32, !llvm.ptr) -> i32
  %d = arith.addi %r, %r : i32
  llvm.return %d : i32
}
