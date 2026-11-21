// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// RUN: lcc-opt --lcc-lower %s | FileCheck %s --implicit-check-not=scf. \
// RUN:   --implicit-check-not=ub.poison --implicit-check-not=arith. \
// RUN:   --implicit-check-not=cf. --implicit-check-not=func. \
// RUN:   --implicit-check-not=' affine.' --implicit-check-not=' memref.' \
// RUN:   --implicit-check-not=' vector.'
//
// lcc-lower lowers what lcc-to-scf leaves, scf and the ub.poison of
// lift-cf-to-scf among it, and what lcc-to-affine leaves, also after
// affine-loop-tile and affine-super-vectorize (affine, memref globals,
// vector transfers and reductions), to the LLVM dialect, which
// mlir-translate takes.

// CHECK-LABEL: llvm.func @sum(
//       CHECK:   llvm.mlir.poison : i32
//       CHECK:   llvm.cond_br
//       CHECK:   llvm.return
// CHECK-LABEL: llvm.func @pick(
//       CHECK:   llvm.switch
// CHECK-LABEL: llvm.func @twice(
//       CHECK:   llvm.call @sum(
//       CHECK: llvm.mlir.global private constant @tab(dense<[1, -2, 3, -4, 5, -6, 7, -8, 9, -10, 11, -12, 13, -14, 15, -16]> : tensor<16xi8>) {addr_space = 0 : i32, alignment = 16 : i64} : !llvm.array<16 x i8>
//       CHECK: llvm.mlir.global external @acc(dense<0> : tensor<4xi32>) {addr_space = 0 : i32, alignment = 16 : i64} : !llvm.array<4 x i32>
// CHECK-LABEL: llvm.func @dot(
//       CHECK:   llvm.mlir.addressof @tab
//       CHECK:   llvm.load %{{.*}} {alignment = 1 : i64} : !llvm.ptr -> vector<16xi8>
//       CHECK:   llvm.intr.vector.reduce.add
//       CHECK:   llvm.intr.smin
//       CHECK:   llvm.ptrtoint
//       CHECK:   llvm.inttoptr

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

// The output of lcc-to-affine for a dot product of a constant table, after
// affine-super-vectorize and affine-loop-tile of another loop, and a
// pointer made from a memref.
memref.global "private" constant @tab : memref<16xi8> = dense<[1, -2, 3, -4, 5, -6, 7, -8, 9, -10, 11, -12, 13, -14, 15, -16]> {alignment = 16 : i64}
memref.global @acc : memref<4xi32> = dense<0> {alignment = 16 : i64}
func.func @dot(%n: index) -> !llvm.ptr {
  %t = memref.get_global @tab : memref<16xi8>
  %m = memref.get_global @acc : memref<4xi32>
  %z = arith.constant dense<0> : vector<16xi32>
  %r = affine.for %i = 0 to 16 step 16 iter_args(%a = %z) -> (vector<16xi32>) {
    %pad = ub.poison : i8
    %v = vector.transfer_read %t[%i], %pad {in_bounds = [true]} : memref<16xi8>, vector<16xi8>
    %e = arith.extsi %v : vector<16xi8> to vector<16xi32>
    %s = arith.addi %a, %e : vector<16xi32>
    affine.yield %s : vector<16xi32>
  }
  %x = vector.reduction <add>, %r : vector<16xi32> into i32
  affine.for %i = 0 to 4 step 2 {
    affine.for %j = affine_map<(d0) -> (d0)>(%i) to min affine_map<(d0)[s0] -> (d0 + 2, s0)>(%i)[%n] {
      affine.store %x, %m[%j] : memref<4xi32>
    }
  }
  %p = memref.extract_aligned_pointer_as_index %m : memref<4xi32> -> index
  %q = arith.index_cast %p : index to i64
  %ptr = llvm.inttoptr %q : i64 to !llvm.ptr
  return %ptr : !llvm.ptr
}
