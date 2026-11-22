// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Tiles the linalg operations of the kernels as tile.mlir does and
// vectorizes the tiles, whose sizes divide the loops of every kernel here:
// the matmuls (4x8x4) and the operations of two loops (4x4).  The
// reduction of a matmul tile becomes a vector.multi_reduction, which
// lcc-lower adds to the accumulator in the order of the reduction, so a
// float sum keeps the order of C.  MLIR 23 vectorizes neither
// linalg.conv_2d on memrefs nor what transform.structured.decompose would
// make of it (it fails on memrefs), so the convolutions are only tiled,
// by 4x8 output elements.

module attributes {transform.with_named_sequence} {
  transform.named_sequence @__transform_main(%root: !transform.any_op {transform.readonly}) {
    %mm = transform.structured.match ops{["linalg.matmul"]} in %root
      : (!transform.any_op) -> !transform.any_op
    %tmm, %lmm:3 = transform.structured.tile_using_for %mm tile_sizes [4, 8, 4]
      : (!transform.any_op) -> (!transform.any_op, !transform.any_op, !transform.any_op, !transform.any_op)
    %cv = transform.structured.match ops{["linalg.conv_2d"]} in %root
      : (!transform.any_op) -> !transform.any_op
    %tcv, %lcv:2 = transform.structured.tile_using_for %cv tile_sizes [4, 8, 0, 0]
      : (!transform.any_op) -> (!transform.any_op, !transform.any_op, !transform.any_op)
    %ew = transform.structured.match ops{["linalg.broadcast", "linalg.generic", "linalg.fill", "linalg.copy"]} in %root
      : (!transform.any_op) -> !transform.any_op
    %tew, %lew:2 = transform.structured.tile_using_for %ew tile_sizes [4, 4]
      : (!transform.any_op) -> (!transform.any_op, !transform.any_op, !transform.any_op)
    transform.structured.vectorize %tmm : !transform.any_op
    transform.structured.vectorize %tew : !transform.any_op
    %f = transform.structured.match ops{["func.func"]} in %root
      : (!transform.any_op) -> !transform.any_op
    transform.apply_patterns to %f {
      transform.apply_patterns.vector.transfer_permutation_patterns
      transform.apply_patterns.canonicalization
    } : !transform.any_op
    transform.yield
  }
}
