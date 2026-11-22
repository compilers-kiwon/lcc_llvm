// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// RUN: lcc-opt --lcc-expand-shapes --split-input-file %s | FileCheck %s
// RUN: lcc-opt --lcc-expand-shapes=remarks=true --split-input-file \
// RUN:   --mlir-print-op-on-diagnostic=false %s 2>&1 >/dev/null \
// RUN:   | FileCheck %s --check-prefix=REMARK
//
// lcc-expand-shapes gives each 1-D global memref of a function the shape
// its affine indices imply, as one memref.expand_shape view at the start of
// the function, and makes every access of the global in the function an
// access of the view.  The inputs are in the form lcc-to-affine gives.

// The nest of convfix.c (see to-affine.mlir): `p * 16 + i` of 1024
// elements is [p, i] of 64x16.  bias, indexed by `o` alone, stays 1-D.

// CHECK-LABEL: func.func @conv()
//   CHECK-DAG:   %[[W1:.*]] = memref.get_global @w : memref<512xi8>
//   CHECK-DAG:   %[[W:.*]] = memref.expand_shape %[[W1]] {{\[}}[0, 1]] output_shape [32, 16] : memref<512xi8> into memref<32x16xi8>
//   CHECK-DAG:   %[[IN1:.*]] = memref.get_global @in : memref<1024xi8>
//   CHECK-DAG:   %[[IN:.*]] = memref.expand_shape %[[IN1]] {{\[}}[0, 1]] output_shape [64, 16] : memref<1024xi8> into memref<64x16xi8>
//   CHECK-DAG:   %[[OUT1:.*]] = memref.get_global @out : memref<2048xi8>
//   CHECK-DAG:   %[[OUT:.*]] = memref.expand_shape %[[OUT1]] {{\[}}[0, 1]] output_shape [64, 32] : memref<2048xi8> into memref<64x32xi8>
//   CHECK-DAG:   %[[BIAS:.*]] = memref.get_global @bias : memref<32xi32>
//       CHECK:   affine.for %[[P:.*]] = 0 to 64 {
//       CHECK:     affine.for %[[O:.*]] = 0 to 32 {
//       CHECK:       affine.load %[[BIAS]][%[[O]]] : memref<32xi32>
//       CHECK:       affine.for %[[I:.*]] = 0 to 16 iter_args
//       CHECK:         affine.load %[[W]][%[[O]], %[[I]]] : memref<32x16xi8>
//       CHECK:         affine.load %[[IN]][%[[P]], %[[I]]] : memref<64x16xi8>
//       CHECK:       affine.store %{{.*}}, %[[OUT]][%[[P]], %[[O]]] : memref<64x32xi8>
// REMARK: remark: lcc-expand-shapes: @w becomes 'memref<32x16xi8>' in @conv
// REMARK: remark: lcc-expand-shapes: @in becomes 'memref<64x16xi8>' in @conv
// REMARK: remark: lcc-expand-shapes: @out becomes 'memref<64x32xi8>' in @conv
// REMARK-NOT: @bias
module {
  memref.global @bias : memref<32xi32> = dense<0>
  memref.global @w : memref<512xi8> = dense<0>
  memref.global @in : memref<1024xi8> = dense<0>
  memref.global @out : memref<2048xi8> = dense<0>
  func.func @conv() {
    %c127_i32 = arith.constant 127 : i32
    %c-128_i32 = arith.constant -128 : i32
    %0 = memref.get_global @bias : memref<32xi32>
    %1 = memref.get_global @w : memref<512xi8>
    %2 = memref.get_global @in : memref<1024xi8>
    %3 = memref.get_global @out : memref<2048xi8>
    affine.for %p = 0 to 64 {
      affine.for %o = 0 to 32 {
        %4 = affine.load %0[%o] : memref<32xi32>
        %5 = affine.for %i = 0 to 16 iter_args(%a = %4) -> (i32) {
          %9 = affine.load %1[%o * 16 + %i] : memref<512xi8>
          %10 = arith.extsi %9 : i8 to i32
          %11 = affine.load %2[%p * 16 + %i] : memref<1024xi8>
          %12 = arith.extsi %11 : i8 to i32
          %13 = arith.muli %10, %12 : i32
          %14 = arith.addi %a, %13 : i32
          affine.yield %14 : i32
        }
        %6 = arith.maxsi %5, %c-128_i32 : i32
        %7 = arith.minsi %6, %c127_i32 : i32
        %8 = arith.trunci %7 : i32 to i8
        affine.store %8, %3[%p * 32 + %o] : memref<2048xi8>
      }
    }
    return
  }
}

// -----

// The constant goes to the dimensions so that the inner ones stay in
// range: `p * 16 + i - 1` with i in [1, 17) is [p, i - 1], and
// `(8 - q) * 16 + j - 16` with q in [4, 8) is [-q + 7, j]: coefficients
// may be negative.  A function has its own view of a global, also when
// another function has another one.

// CHECK-LABEL: func.func @offset()
//       CHECK:   %[[G:.*]] = memref.expand_shape %{{.*}} output_shape [4, 16] : memref<64xi32> into memref<4x16xi32>
//       CHECK:   affine.for %[[P:.*]] = 0 to 4 {
//       CHECK:     affine.for %[[I:.*]] = 1 to 17 {
//       CHECK:       affine.store %{{.*}}, %[[G]][%[[P]], %[[I]] - 1] : memref<4x16xi32>
//       CHECK:   affine.for %[[Q:.*]] = 4 to 8 {
//       CHECK:     affine.for %[[J:.*]] = 0 to 16 {
//       CHECK:       affine.load %[[G]][-%[[Q]] + 7, %[[J]]] : memref<4x16xi32>
//       CHECK:       affine.store %{{.*}}, %[[G]][0, %[[J]]] : memref<4x16xi32>
// CHECK-LABEL: func.func @other()
//       CHECK:   %[[G2:.*]] = memref.expand_shape %{{.*}} output_shape [8, 8] : memref<64xi32> into memref<8x8xi32>
//       CHECK:       affine.load %[[G2]][%{{.*}}, %{{.*}}] : memref<8x8xi32>
// REMARK: remark: lcc-expand-shapes: @g becomes 'memref<4x16xi32>' in @offset
// REMARK: remark: lcc-expand-shapes: @g becomes 'memref<8x8xi32>' in @other
module {
  memref.global @g : memref<64xi32> = dense<0>
  func.func @offset() {
    %c0 = arith.constant 0 : i32
    %0 = memref.get_global @g : memref<64xi32>
    affine.for %p = 0 to 4 {
      affine.for %i = 1 to 17 {
        affine.store %c0, %0[%p * 16 + %i - 1] : memref<64xi32>
      }
    }
    affine.for %q = 4 to 8 {
      affine.for %j = 0 to 16 {
        %v = affine.load %0[(8 - %q) * 16 + %j - 16] : memref<64xi32>
        affine.store %v, %0[%j] : memref<64xi32>
      }
    }
    return
  }
  func.func @other() -> i32 {
    %c0 = arith.constant 0 : i32
    %0 = memref.get_global @g : memref<64xi32>
    %s = affine.for %a = 0 to 8 iter_args(%x = %c0) -> (i32) {
      %t = affine.for %b = 0 to 8 iter_args(%y = %x) -> (i32) {
        %v = affine.load %0[%a * 8 + %b] : memref<64xi32>
        %z = arith.addi %y, %v : i32
        affine.yield %z : i32
      }
      affine.yield %t : i32
    }
    return %s : i32
  }
}

// -----

// An index that may leave its row, `p * 16 + i + 1` with i in [0, 16):
// the global stays 1-D in the function.

// CHECK-LABEL: func.func @cross()
//   CHECK-NOT:   memref.expand_shape
//       CHECK:   affine.load %{{.*}}[%{{.*}} * 16 + %{{.*}} + 1] : memref<80xi32>
// REMARK: remark: lcc-expand-shapes: @h stays 1-D in @cross: an index may leave its row
module {
  memref.global @h : memref<80xi32> = dense<0>
  func.func @cross() {
    %0 = memref.get_global @h : memref<80xi32>
    affine.for %p = 0 to 4 {
      affine.for %i = 0 to 16 {
        %v = affine.load %0[%p * 16 + %i + 1] : memref<80xi32>
        affine.store %v, %0[%p * 16 + %i] : memref<80xi32>
      }
    }
    return
  }
}

// -----

// Two nests that need different shapes, 4x16 and 8x8: one view would have
// to be shared, so the global stays 1-D.  Coefficients 6 and 4 make no
// shape at all.

// CHECK-LABEL: func.func @shapes()
//   CHECK-NOT:   memref.expand_shape
// CHECK-LABEL: func.func @nest()
//   CHECK-NOT:   memref.expand_shape
// REMARK: remark: lcc-expand-shapes: @a stays 1-D in @shapes: nests need different shapes
// REMARK: remark: lcc-expand-shapes: @b stays 1-D in @nest: indices with coefficients 6 and 4, which do not nest
module {
  memref.global @a : memref<64xi32> = dense<0>
  memref.global @b : memref<48xi32> = dense<0>
  func.func @shapes() {
    %c1 = arith.constant 1 : i32
    %0 = memref.get_global @a : memref<64xi32>
    affine.for %x = 0 to 4 {
      affine.for %y = 0 to 16 {
        affine.store %c1, %0[%x * 16 + %y] : memref<64xi32>
      }
    }
    affine.for %z = 0 to 8 {
      affine.for %w = 0 to 8 {
        affine.store %c1, %0[%z * 8 + %w] : memref<64xi32>
      }
    }
    return
  }
  func.func @nest() {
    %c1 = arith.constant 1 : i32
    %0 = memref.get_global @b : memref<48xi32>
    affine.for %i = 0 to 8 {
      affine.for %j = 0 to 4 {
        affine.store %c1, %0[%i * 6 + %j] : memref<48xi32>
        affine.store %c1, %0[%j * 4 + %i] : memref<48xi32>
      }
    }
    return
  }
}

// -----

// memref.load and memref.store outside affine nests, at any index x, become
// accesses at [x floordiv 8, x mod 8], the same element for every x; a
// second memref.get_global of the global in a loop gets the same view;
// memref.extract_aligned_pointer_as_index stays on the 1-D memref.

//   CHECK-DAG: #[[$DIV:.*]] = affine_map<(d0) -> (d0 floordiv 8)>
//   CHECK-DAG: #[[$MOD:.*]] = affine_map<(d0) -> (d0 mod 8)>
// CHECK-LABEL: func.func @outside(
//  CHECK-SAME:     %[[K:.*]]: index
//       CHECK:   %[[G1:.*]] = memref.get_global @c : memref<64xi32>
//       CHECK:   %[[G:.*]] = memref.expand_shape %[[G1]] {{\[}}[0, 1]] output_shape [8, 8] : memref<64xi32> into memref<8x8xi32>
//   CHECK-NOT:   memref.expand_shape
//       CHECK:   %[[PTR:.*]] = memref.extract_aligned_pointer_as_index %{{.*}} : memref<64xi32> -> index
//       CHECK:   %[[A:.*]] = affine.apply #[[$DIV]](%[[K]])
//       CHECK:   %[[B:.*]] = affine.apply #[[$MOD]](%[[K]])
//       CHECK:   %[[V:.*]] = memref.load %[[G]][%[[A]], %[[B]]] : memref<8x8xi32>
//       CHECK:   affine.for %[[I:.*]] = 0 to 8 {
//       CHECK:     affine.for %[[J:.*]] = 0 to 8 {
//       CHECK:       affine.store %[[V]], %[[G]][%[[I]], %[[J]]] : memref<8x8xi32>
//       CHECK:   %[[C:.*]] = affine.apply #[[$DIV]](%{{.*}})
//       CHECK:   %[[D:.*]] = affine.apply #[[$MOD]](%{{.*}})
//       CHECK:   memref.store %{{.*}}, %[[G]][%[[C]], %[[D]]] {nontemporal = true} : memref<8x8xi32>
// REMARK: remark: lcc-expand-shapes: @c becomes 'memref<8x8xi32>' in @outside
module {
  memref.global @c : memref<64xi32> = dense<0>
  func.func @outside(%k: index) -> index {
    %c13 = arith.constant 13 : index
    %0 = memref.get_global @c : memref<64xi32>
    %ptr = memref.extract_aligned_pointer_as_index %0 : memref<64xi32> -> index
    %v = memref.load %0[%k] : memref<64xi32>
    affine.for %i = 0 to 8 {
      %1 = memref.get_global @c : memref<64xi32>
      affine.for %j = 0 to 8 {
        affine.store %v, %1[%i * 8 + %j] : memref<64xi32>
      }
    }
    memref.store %v, %0[%c13] {nontemporal = true} : memref<64xi32>
    return %ptr : index
  }
}

// -----

// Any other use of the memref leaves the global 1-D in the function.

// CHECK-LABEL: func.func @used()
//   CHECK-NOT:   memref.expand_shape
// REMARK: remark: lcc-expand-shapes: @d stays 1-D in @used: used by memref.cast
module {
  memref.global @d : memref<64xi32> = dense<0>
  func.func @used() -> memref<?xi32> {
    %c1 = arith.constant 1 : i32
    %0 = memref.get_global @d : memref<64xi32>
    affine.for %i = 0 to 8 {
      affine.for %j = 0 to 8 {
        affine.store %c1, %0[%i * 8 + %j] : memref<64xi32>
      }
    }
    %1 = memref.cast %0 : memref<64xi32> to memref<?xi32>
    return %1 : memref<?xi32>
  }
}
