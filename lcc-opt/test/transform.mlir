// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// RUN: lcc-opt --transform-interpreter --split-input-file %s | FileCheck %s
// RUN: lcc-opt --transform-interpreter --lcc-lower --split-input-file %s \
// RUN:   | FileCheck %s --check-prefix=LOWER
//
// Transform dialect scripts tile and vectorize the linalg operations of
// lcc-to-linalg, and lcc-lower lowers the result.  The payload is what
// lcc-to-linalg makes of convfix.c (see to-linalg.mlir); each module also
// holds its script.  To apply a script kept in a file of its own to the
// output of lcc-to-linalg, give lcc-opt
// --transform-preload-library=transform-library-paths=FILE before
// --transform-interpreter.

// Tiling: the matmul in tiles of 4 values of o, 8 of p and 4 of i, the
// element-wise operations in tiles of 4x4.  Each tile adds the products of
// its 4 values of i to the buffer in their order, and the tiles of i come
// in their order, so the sum is the same, also in float.

// CHECK-LABEL: func.func @conv()
//       CHECK:   scf.for %[[P:.*]] = %{{.*}} to %{{.*}} step %{{.*}} {
//       CHECK:     scf.for %[[O:.*]] = %{{.*}} to %{{.*}} step %{{.*}} {
//       CHECK:       linalg.broadcast ins(%{{.*}} : memref<4xi32, strided<[1], offset: ?>>) outs(%{{.*}} : memref<4x4xi32, strided<[32, 1], offset: ?>>)
//       CHECK:   scf.for
//       CHECK:     scf.for
//       CHECK:       scf.for
//       CHECK:         linalg.matmul indexing_maps = [#{{.*}}, #{{.*}}, #{{.*}}] ins(%{{.*}}, %{{.*}} : memref<4x4xi8, strided<[16, 1], offset: ?>>, memref<8x4xi8, strided<[16, 1], offset: ?>>) outs(%{{.*}} : memref<8x4xi32, strided<[32, 1], offset: ?>>)
//       CHECK:   scf.for
//       CHECK:     scf.for
//       CHECK:       linalg.generic {{.*}} ins(%{{.*}} : memref<4x4xi32, strided<[32, 1], offset: ?>>) outs(%{{.*}} : memref<4x4xi8, strided<[32, 1], offset: ?>>)
// LOWER-LABEL: llvm.func @conv()
//   LOWER-NOT:   {{(linalg|vector|scf|memref|affine)\.}}
//       LOWER:   llvm.return

module {
  memref.global @bias : memref<32xi32> = dense<0> {alignment = 16 : i64}
  memref.global @out : memref<2048xi8> = dense<0> {alignment = 16 : i64}
  memref.global @w : memref<512xi8> = dense<0> {alignment = 16 : i64}
  memref.global @in : memref<1024xi8> = dense<0> {alignment = 16 : i64}
  func.func @conv() {
    %c-128_i32 = arith.constant -128 : i32
    %c127_i32 = arith.constant 127 : i32
    %alloca = memref.alloca() : memref<64x32xi32>
    %0 = memref.get_global @w : memref<512xi8>
    %w = memref.expand_shape %0 [[0, 1]] output_shape [32, 16] : memref<512xi8> into memref<32x16xi8>
    %1 = memref.get_global @in : memref<1024xi8>
    %in = memref.expand_shape %1 [[0, 1]] output_shape [64, 16] : memref<1024xi8> into memref<64x16xi8>
    %2 = memref.get_global @out : memref<2048xi8>
    %out = memref.expand_shape %2 [[0, 1]] output_shape [64, 32] : memref<2048xi8> into memref<64x32xi8>
    %bias = memref.get_global @bias : memref<32xi32>
    linalg.broadcast ins(%bias : memref<32xi32>) outs(%alloca : memref<64x32xi32>) dimensions = [0]
    linalg.matmul indexing_maps = [affine_map<(d0, d1, d2) -> (d0, d2)>, affine_map<(d0, d1, d2) -> (d1, d2)>, affine_map<(d0, d1, d2) -> (d1, d0)>] ins(%w, %in : memref<32x16xi8>, memref<64x16xi8>) outs(%alloca : memref<64x32xi32>)
    linalg.generic {indexing_maps = [affine_map<(d0, d1) -> (d0, d1)>, affine_map<(d0, d1) -> (d0, d1)>], iterator_types = ["parallel", "parallel"]} ins(%alloca : memref<64x32xi32>) outs(%out : memref<64x32xi8>) {
    ^bb0(%x: i32, %y: i8):
      %4 = arith.maxsi %x, %c-128_i32 : i32
      %5 = arith.minsi %4, %c127_i32 : i32
      %6 = arith.trunci %5 : i32 to i8
      linalg.yield %6 : i8
    }
    return
  }

  module attributes {transform.with_named_sequence} {
    transform.named_sequence @__transform_main(%root: !transform.any_op {transform.readonly}) {
      %mm = transform.structured.match ops{["linalg.matmul"]} in %root : (!transform.any_op) -> !transform.any_op
      %t, %l:3 = transform.structured.tile_using_for %mm tile_sizes [4, 8, 4] : (!transform.any_op) -> (!transform.any_op, !transform.any_op, !transform.any_op, !transform.any_op)
      %ew = transform.structured.match ops{["linalg.broadcast", "linalg.generic"]} in %root : (!transform.any_op) -> !transform.any_op
      %t2, %l2:2 = transform.structured.tile_using_for %ew tile_sizes [4, 4] : (!transform.any_op) -> (!transform.any_op, !transform.any_op, !transform.any_op)
      transform.yield
    }
  }
}

// -----

// Tiling and vectorizing: each tile becomes vector transfers, and the
// reduction of a matmul tile a vector.multi_reduction, which lcc-lower
// lowers to additions of the 4 vectors of products to the accumulator in
// the order of i.

// CHECK-LABEL: func.func @conv()
//   CHECK-NOT:   linalg.
//       CHECK:   vector.transfer_read
//       CHECK:   vector.multi_reduction <add>, %{{.*}}, %{{.*}} [2] : vector<4x8x4xi32> to vector<4x8xi32>
//       CHECK:   vector.transfer_write
//   CHECK-NOT:   linalg.
//       CHECK:   return
// LOWER-LABEL: llvm.func @conv()
//   LOWER-NOT:   {{(linalg|vector|scf|memref|affine)\.}}
//       LOWER:   llvm.return

module {
  memref.global @bias : memref<32xi32> = dense<0> {alignment = 16 : i64}
  memref.global @out : memref<2048xi8> = dense<0> {alignment = 16 : i64}
  memref.global @w : memref<512xi8> = dense<0> {alignment = 16 : i64}
  memref.global @in : memref<1024xi8> = dense<0> {alignment = 16 : i64}
  func.func @conv() {
    %c-128_i32 = arith.constant -128 : i32
    %c127_i32 = arith.constant 127 : i32
    %alloca = memref.alloca() : memref<64x32xi32>
    %0 = memref.get_global @w : memref<512xi8>
    %w = memref.expand_shape %0 [[0, 1]] output_shape [32, 16] : memref<512xi8> into memref<32x16xi8>
    %1 = memref.get_global @in : memref<1024xi8>
    %in = memref.expand_shape %1 [[0, 1]] output_shape [64, 16] : memref<1024xi8> into memref<64x16xi8>
    %2 = memref.get_global @out : memref<2048xi8>
    %out = memref.expand_shape %2 [[0, 1]] output_shape [64, 32] : memref<2048xi8> into memref<64x32xi8>
    %bias = memref.get_global @bias : memref<32xi32>
    linalg.broadcast ins(%bias : memref<32xi32>) outs(%alloca : memref<64x32xi32>) dimensions = [0]
    linalg.matmul indexing_maps = [affine_map<(d0, d1, d2) -> (d0, d2)>, affine_map<(d0, d1, d2) -> (d1, d2)>, affine_map<(d0, d1, d2) -> (d1, d0)>] ins(%w, %in : memref<32x16xi8>, memref<64x16xi8>) outs(%alloca : memref<64x32xi32>)
    linalg.generic {indexing_maps = [affine_map<(d0, d1) -> (d0, d1)>, affine_map<(d0, d1) -> (d0, d1)>], iterator_types = ["parallel", "parallel"]} ins(%alloca : memref<64x32xi32>) outs(%out : memref<64x32xi8>) {
    ^bb0(%x: i32, %y: i8):
      %4 = arith.maxsi %x, %c-128_i32 : i32
      %5 = arith.minsi %4, %c127_i32 : i32
      %6 = arith.trunci %5 : i32 to i8
      linalg.yield %6 : i8
    }
    return
  }

  module attributes {transform.with_named_sequence} {
    transform.named_sequence @__transform_main(%root: !transform.any_op {transform.readonly}) {
      %mm = transform.structured.match ops{["linalg.matmul"]} in %root : (!transform.any_op) -> !transform.any_op
      %t, %l:3 = transform.structured.tile_using_for %mm tile_sizes [4, 8, 4] : (!transform.any_op) -> (!transform.any_op, !transform.any_op, !transform.any_op, !transform.any_op)
      %ew = transform.structured.match ops{["linalg.broadcast", "linalg.generic"]} in %root : (!transform.any_op) -> !transform.any_op
      %t2, %l2:2 = transform.structured.tile_using_for %ew tile_sizes [4, 4] : (!transform.any_op) -> (!transform.any_op, !transform.any_op, !transform.any_op)
      transform.structured.vectorize %t : !transform.any_op
      transform.structured.vectorize %t2 : !transform.any_op
      %f = transform.structured.match ops{["func.func"]} in %root : (!transform.any_op) -> !transform.any_op
      transform.apply_patterns to %f {
        transform.apply_patterns.vector.transfer_permutation_patterns
        transform.apply_patterns.canonicalization
      } : !transform.any_op
      transform.yield
    }
  }
}
