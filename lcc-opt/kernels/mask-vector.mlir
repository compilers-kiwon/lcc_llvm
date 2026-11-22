// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Tiles the linalg operations of the kernels by sizes that divide none of
// their loops, the matmuls 5x7x3 and the operations of two loops 5x7, and
// vectorizes the tiles with vectors of those sizes: the tiles at the ends
// are smaller, so the vector operations are masked (vector.mask, which
// lcc-lower lowers).  The convolutions are only tiled, by 5x7 output
// elements (see tile-vector.mlir).

module attributes {transform.with_named_sequence} {
  transform.named_sequence @__transform_main(%root: !transform.any_op {transform.readonly}) {
    %mm = transform.structured.match ops{["linalg.matmul"]} in %root
      : (!transform.any_op) -> !transform.any_op
    %tmm, %lmm:3 = transform.structured.tile_using_for %mm tile_sizes [5, 7, 3]
      : (!transform.any_op) -> (!transform.any_op, !transform.any_op, !transform.any_op, !transform.any_op)
    %cv = transform.structured.match ops{["linalg.conv_2d"]} in %root
      : (!transform.any_op) -> !transform.any_op
    %tcv, %lcv:2 = transform.structured.tile_using_for %cv tile_sizes [5, 7, 0, 0]
      : (!transform.any_op) -> (!transform.any_op, !transform.any_op, !transform.any_op)
    %ew = transform.structured.match ops{["linalg.broadcast", "linalg.generic", "linalg.fill", "linalg.copy"]} in %root
      : (!transform.any_op) -> !transform.any_op
    %tew, %lew:2 = transform.structured.tile_using_for %ew tile_sizes [5, 7]
      : (!transform.any_op) -> (!transform.any_op, !transform.any_op, !transform.any_op)
    transform.structured.vectorize %tmm vector_sizes [5, 7, 3] : !transform.any_op
    transform.structured.vectorize %tew vector_sizes [5, 7] : !transform.any_op
    %f = transform.structured.match ops{["func.func"]} in %root
      : (!transform.any_op) -> !transform.any_op
    transform.apply_patterns to %f {
      transform.apply_patterns.vector.transfer_permutation_patterns
      transform.apply_patterns.canonicalization
    } : !transform.any_op
    transform.yield
  }
}
