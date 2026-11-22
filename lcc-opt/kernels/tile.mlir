// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Tiles the linalg operations that lcc-to-linalg makes of the kernels of
// this directory: the matmuls in tiles of 4x8x4 iterations (for convfix.c
// 4 values of o, 8 of p and 4 of i), the 3x3 convolutions in tiles of 4x8
// output elements and 2x2 window elements (partial tiles at the ends of
// the window), and the operations of two loops (broadcast, element-wise
// generic, fill, copy) in tiles of 4x4.  The tiles of a reduction come in
// its order.  Apply it with
//
//   lcc-opt --lcc-to-linalg \
//     --transform-preload-library=transform-library-paths=tile.mlir \
//     --transform-interpreter --lcc-lower --emit-bytecode k.mlir -o k.mlirbc

module attributes {transform.with_named_sequence} {
  transform.named_sequence @__transform_main(%root: !transform.any_op {transform.readonly}) {
    %mm = transform.structured.match ops{["linalg.matmul"]} in %root
      : (!transform.any_op) -> !transform.any_op
    %tmm, %lmm:3 = transform.structured.tile_using_for %mm tile_sizes [4, 8, 4]
      : (!transform.any_op) -> (!transform.any_op, !transform.any_op, !transform.any_op, !transform.any_op)
    %cv = transform.structured.match ops{["linalg.conv_2d"]} in %root
      : (!transform.any_op) -> !transform.any_op
    %tcv, %lcv:4 = transform.structured.tile_using_for %cv tile_sizes [4, 8, 2, 2]
      : (!transform.any_op) -> (!transform.any_op, !transform.any_op, !transform.any_op, !transform.any_op, !transform.any_op)
    %ew = transform.structured.match ops{["linalg.broadcast", "linalg.generic", "linalg.fill", "linalg.copy"]} in %root
      : (!transform.any_op) -> !transform.any_op
    %tew, %lew:2 = transform.structured.tile_using_for %ew tile_sizes [4, 4]
      : (!transform.any_op) -> (!transform.any_op, !transform.any_op, !transform.any_op)
    transform.yield
  }
}
