// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// RUN: lcc-opt --lcc-affine-to-linalg --split-input-file %s | FileCheck %s
// RUN: lcc-opt --lcc-affine-to-linalg=remarks=true --split-input-file \
// RUN:   --mlir-print-op-on-diagnostic=false %s 2>&1 >/dev/null \
// RUN:   | FileCheck %s --check-prefix=REMARK
//
// lcc-affine-to-linalg turns perfect affine nests that load and store
// memrefs at affine indices of their own loops into linalg.generic.  The
// inputs are in the form lcc-distribute-reductions gives.

// The three nests of convfix.c after lcc-distribute-reductions: bias into
// the buffer, the reduction into the buffer (outputs read where they
// accumulate are the output's block argument, and the loop not in the
// output's indices is a reduction), and the clamp.

//   CHECK-DAG: #[[$O:.*]] = affine_map<(d0, d1) -> (d1)>
//   CHECK-DAG: #[[$PO:.*]] = affine_map<(d0, d1) -> (d0, d1)>
//   CHECK-DAG: #[[$OI:.*]] = affine_map<(d0, d1, d2) -> (d1, d2)>
//   CHECK-DAG: #[[$PI:.*]] = affine_map<(d0, d1, d2) -> (d0, d2)>
//   CHECK-DAG: #[[$PO3:.*]] = affine_map<(d0, d1, d2) -> (d0, d1)>
// CHECK-LABEL: func.func @conv()
//       CHECK:   %[[BUF:.*]] = memref.alloca() : memref<64x32xi32>
//       CHECK:   linalg.generic {indexing_maps = [#[[$O]], #[[$PO]]], iterator_types = ["parallel", "parallel"]}
//  CHECK-SAME:       ins(%{{.*}} : memref<32xi32>) outs(%[[BUF]] : memref<64x32xi32>) {
//  CHECK-NEXT:   ^bb0(%[[B:.*]]: i32, %{{.*}}: i32):
//  CHECK-NEXT:     linalg.yield %[[B]] : i32
//       CHECK:   linalg.generic {indexing_maps = [#[[$OI]], #[[$PI]], #[[$PO3]]], iterator_types = ["parallel", "parallel", "reduction"]}
//  CHECK-SAME:       ins(%{{.*}}, %{{.*}} : memref<32x16xi8>, memref<64x16xi8>) outs(%[[BUF]] : memref<64x32xi32>) {
//  CHECK-NEXT:   ^bb0(%[[W:.*]]: i8, %[[X:.*]]: i8, %[[A:.*]]: i32):
//  CHECK-NEXT:     %[[WE:.*]] = arith.extsi %[[W]] : i8 to i32
//  CHECK-NEXT:     %[[XE:.*]] = arith.extsi %[[X]] : i8 to i32
//  CHECK-NEXT:     %[[M:.*]] = arith.muli %[[WE]], %[[XE]] : i32
//  CHECK-NEXT:     %[[S:.*]] = arith.addi %[[A]], %[[M]] : i32
//  CHECK-NEXT:     linalg.yield %[[S]] : i32
//       CHECK:   linalg.generic {indexing_maps = [#[[$PO]], #[[$PO]]], iterator_types = ["parallel", "parallel"]}
//  CHECK-SAME:       ins(%[[BUF]] : memref<64x32xi32>) outs(%{{.*}} : memref<64x32xi8>) {
//  CHECK-NEXT:   ^bb0(%[[R:.*]]: i32, %{{.*}}: i8):
//  CHECK-NEXT:     %[[LO:.*]] = arith.maxsi %[[R]], %{{.*}} : i32
//  CHECK-NEXT:     %[[HI:.*]] = arith.minsi %[[LO]], %{{.*}} : i32
//  CHECK-NEXT:     %[[T:.*]] = arith.trunci %[[HI]] : i32 to i8
//  CHECK-NEXT:     linalg.yield %[[T]] : i8
//   CHECK-NOT:   affine.for
// REMARK: remark: lcc-affine-to-linalg: raised a nest of 2 loops (2 parallel, 0 reduction)
// REMARK: remark: lcc-affine-to-linalg: raised a nest of 3 loops (2 parallel, 1 reduction)
// REMARK: remark: lcc-affine-to-linalg: raised a nest of 2 loops (2 parallel, 0 reduction)
module {
  memref.global @bias : memref<32xi32> = dense<0>
  memref.global @w : memref<32x16xi8> = dense<0>
  memref.global @in : memref<64x16xi8> = dense<0>
  memref.global @out : memref<64x32xi8> = dense<0>
  func.func @conv() {
    %c-128_i32 = arith.constant -128 : i32
    %c127_i32 = arith.constant 127 : i32
    %alloca = memref.alloca() : memref<64x32xi32>
    %0 = memref.get_global @bias : memref<32xi32>
    %1 = memref.get_global @w : memref<32x16xi8>
    %2 = memref.get_global @in : memref<64x16xi8>
    %3 = memref.get_global @out : memref<64x32xi8>
    affine.for %p = 0 to 64 {
      affine.for %o = 0 to 32 {
        %4 = affine.load %0[%o] : memref<32xi32>
        affine.store %4, %alloca[%p, %o] : memref<64x32xi32>
      }
    }
    affine.for %p = 0 to 64 {
      affine.for %o = 0 to 32 {
        affine.for %i = 0 to 16 {
          %4 = affine.load %alloca[%p, %o] : memref<64x32xi32>
          %5 = affine.load %1[%o, %i] : memref<32x16xi8>
          %6 = arith.extsi %5 : i8 to i32
          %7 = affine.load %2[%p, %i] : memref<64x16xi8>
          %8 = arith.extsi %7 : i8 to i32
          %9 = arith.muli %6, %8 : i32
          %10 = arith.addi %4, %9 : i32
          affine.store %10, %alloca[%p, %o] : memref<64x32xi32>
        }
      }
    }
    affine.for %p = 0 to 64 {
      affine.for %o = 0 to 32 {
        %4 = affine.load %alloca[%p, %o] : memref<64x32xi32>
        %5 = arith.maxsi %4, %c-128_i32 : i32
        %6 = arith.minsi %5, %c127_i32 : i32
        %7 = arith.trunci %6 : i32 to i8
        affine.store %7, %3[%p, %o] : memref<64x32xi8>
      }
    }
    return
  }
}

// -----

// A loop over part of a dimension, an index with a stride and a constant
// index make subviews, so that every dimension a loop indexes alone has
// the size of its iteration count; loops that do not start at 0 or step by
// more than 1 count their iterations from 0.  A sum of loop variables
// indexes a subview of the size of its range.

//   CHECK-DAG: #[[$ID:.*]] = affine_map<(d0) -> (d0)>
//   CHECK-DAG: #[[$ROW:.*]] = affine_map<(d0) -> (0, d0)>
//   CHECK-DAG: #[[$SUM:.*]] = affine_map<(d0, d1) -> (d0 + d1)>
//   CHECK-DAG: #[[$R:.*]] = affine_map<(d0, d1) -> (d1)>
//   CHECK-DAG: #[[$I:.*]] = affine_map<(d0, d1) -> (d0)>
// CHECK-LABEL: func.func @parts()
//   CHECK-DAG:   %[[A:.*]] = memref.get_global @a : memref<16xi32>
//   CHECK-DAG:   %[[B:.*]] = memref.get_global @b : memref<16xi32>
//       CHECK:   %[[A0:.*]] = memref.subview %[[A]][0] [14] [1] : memref<16xi32> to memref<14xi32, strided<[1]>>
//       CHECK:   %[[A2:.*]] = memref.subview %[[A]][2] [14] [1] : memref<16xi32> to memref<14xi32, strided<[1], offset: 2>>
//       CHECK:   %[[B1:.*]] = memref.subview %[[B]][1] [14] [1] : memref<16xi32> to memref<14xi32, strided<[1], offset: 1>>
//       CHECK:   linalg.generic {indexing_maps = [#[[$ID]], #[[$ID]], #[[$ID]]], iterator_types = ["parallel"]}
//  CHECK-SAME:       ins(%[[A0]], %[[A2]] : {{.*}}) outs(%[[B1]] : {{.*}}) {
//       CHECK:   %[[X:.*]] = memref.subview %{{.*}}[1] [6] [2] : memref<20xi32> to memref<6xi32, strided<[2], offset: 1>>
//       CHECK:   %[[Y:.*]] = memref.subview %{{.*}}[2] [6] [3] : memref<20xi32> to memref<6xi32, strided<[3], offset: 2>>
//       CHECK:   linalg.generic {indexing_maps = [#[[$ID]], #[[$ID]]], iterator_types = ["parallel"]}
//  CHECK-SAME:       ins(%[[X]] : {{.*}}) outs(%[[Y]] : {{.*}}) {
//       CHECK:   %[[M:.*]] = memref.subview %{{.*}}[3, 0] [1, 8] [1, 1] : memref<4x8xi32> to memref<1x8xi32, strided<[8, 1], offset: 24>>
//       CHECK:   linalg.generic {indexing_maps = [#[[$ROW]], #[[$ID]]], iterator_types = ["parallel"]}
//  CHECK-SAME:       ins(%[[M]] : {{.*}}) outs(%{{.*}} : memref<8xi32>) {
//       CHECK:   %[[C:.*]] = memref.subview %{{.*}}[0] [9] [1] : memref<16xi32> to memref<9xi32, strided<[1]>>
//       CHECK:   linalg.generic {indexing_maps = [#[[$SUM]], #[[$R]], #[[$I]]], iterator_types = ["parallel", "reduction"]}
//  CHECK-SAME:       ins(%[[C]], %{{.*}} : memref<9xi32, strided<[1]>>, memref<3xi32>) outs(%{{.*}} : memref<7xi32>) {
// REMARK-COUNT-4: remark: lcc-affine-to-linalg: raised a nest
module {
  memref.global @a : memref<16xi32> = dense<0>
  memref.global @b : memref<16xi32> = dense<0>
  memref.global @x : memref<20xi32> = dense<0>
  memref.global @y : memref<20xi32> = dense<0>
  memref.global @m : memref<4x8xi32> = dense<0>
  memref.global @n : memref<8xi32> = dense<0>
  memref.global @k : memref<3xi32> = dense<0>
  memref.global @o : memref<7xi32> = dense<0>
  func.func @parts() {
    %0 = memref.get_global @a : memref<16xi32>
    %1 = memref.get_global @b : memref<16xi32>
    %2 = memref.get_global @x : memref<20xi32>
    %3 = memref.get_global @y : memref<20xi32>
    %4 = memref.get_global @m : memref<4x8xi32>
    %5 = memref.get_global @n : memref<8xi32>
    %6 = memref.get_global @k : memref<3xi32>
    %7 = memref.get_global @o : memref<7xi32>
    affine.for %i = 1 to 15 {
      %u = affine.load %0[%i - 1] : memref<16xi32>
      %v = affine.load %0[%i + 1] : memref<16xi32>
      %s = arith.addi %u, %v : i32
      affine.store %s, %1[%i] : memref<16xi32>
    }
    affine.for %i = 2 to 20 step 3 {
      %u = affine.load %2[(%i - 2) floordiv 3 * 2 + 1] : memref<20xi32>
      affine.store %u, %3[%i] : memref<20xi32>
    }
    affine.for %j = 0 to 8 {
      %u = affine.load %4[3, %j] : memref<4x8xi32>
      affine.store %u, %5[%j] : memref<8xi32>
    }
    affine.for %i = 0 to 7 {
      affine.for %r = 0 to 3 {
        %u = affine.load %0[%i + %r] : memref<16xi32>
        %v = affine.load %6[%r] : memref<3xi32>
        %w = affine.load %7[%i] : memref<7xi32>
        %m = arith.muli %u, %v : i32
        %s = arith.addi %w, %m : i32
        affine.store %s, %7[%i] : memref<7xi32>
      }
    }
    return
  }
}

// -----

// The loop variables the computations use become lb + step * linalg.index;
// a load after the store of its output reads the stored value; an output
// that is only stored is not read.

//   CHECK-DAG: #[[$LB:.*]] = affine_map<(d0) -> (d0 * 2 + 1)>
// CHECK-LABEL: func.func @values()
//       CHECK:   linalg.generic {{.*}} outs(%{{.*}}, %{{.*}} : memref<4xi32, strided<[2], offset: 1>>, memref<4xi32, strided<[2], offset: 1>>) {
//  CHECK-NEXT:   ^bb0(%{{.*}}: i32, %{{.*}}: i32):
//  CHECK-NEXT:     %[[J:.*]] = linalg.index 0 : index
//  CHECK-NEXT:     %[[I:.*]] = affine.apply #[[$LB]](%[[J]])
//  CHECK-NEXT:     %[[V:.*]] = arith.index_cast %[[I]] : index to i32
//  CHECK-NEXT:     %[[W:.*]] = arith.addi %[[V]], %[[V]] : i32
//  CHECK-NEXT:     linalg.yield %[[V]], %[[W]] : i32, i32
module {
  memref.global @e : memref<8xi32> = dense<0>
  memref.global @f : memref<8xi32> = dense<0>
  func.func @values() {
    %0 = memref.get_global @e : memref<8xi32>
    %1 = memref.get_global @f : memref<8xi32>
    affine.for %i = 1 to 8 step 2 {
      %v = arith.index_cast %i : index to i32
      affine.store %v, %0[%i] : memref<8xi32>
      %u = affine.load %0[%i] : memref<8xi32>
      %w = arith.addi %u, %v : i32
      affine.store %w, %1[%i] : memref<8xi32>
    }
    return
  }
}

// -----

// Nests that stay: an output also read at other indices, two stores to one
// memref, an output index that is a sum of loop variables or repeats one,
// an index that decreases, a loop that no index has alone (linalg could
// not infer its count from the shapes), a loop with iter_args, bounds that
// are not constant, a memref defined in the nest, one memory through two
// memref values, and a nest inside a loop with other operations, whose
// indices use the outer loop.

// CHECK-LABEL: func.func @stays(
//   CHECK-NOT:   linalg.generic
// REMARK: remark: lcc-affine-to-linalg: nest stays: a stored memref read at other indices
// REMARK: remark: lcc-affine-to-linalg: nest stays: two stores to one memref
// REMARK: remark: lcc-affine-to-linalg: nest stays: an output index that is a sum of loop variables
// REMARK: remark: lcc-affine-to-linalg: nest stays: an output index that repeats a loop variable
// REMARK: remark: lcc-affine-to-linalg: nest stays: an index that decreases with a loop variable
// REMARK: remark: lcc-affine-to-linalg: nest stays: no index is loop 2 alone
// REMARK: remark: lcc-affine-to-linalg: nest stays: a loop with iter_args
// REMARK: remark: lcc-affine-to-linalg: nest stays: a loop with bounds that are not constant
// REMARK: remark: lcc-affine-to-linalg: nest stays: a memref defined in the nest
// REMARK: remark: lcc-affine-to-linalg: nest stays: one memory reached through different memref values
// REMARK: remark: lcc-affine-to-linalg: nest stays: an operation beside an inner loop: affine.load
// REMARK: remark: lcc-affine-to-linalg: inner nest stays: an index uses a value from outside the nest
module {
  memref.global @h : memref<17xi32> = dense<0>
  memref.global @s : memref<8x8xi32> = dense<0>
  memref.global @t : memref<16xi32> = dense<0>
  func.func @stays(%n: index) {
    %c1 = arith.constant 1 : i32
    %0 = memref.get_global @h : memref<17xi32>
    %1 = memref.get_global @s : memref<8x8xi32>
    %2 = memref.get_global @t : memref<16xi32>
    affine.for %i = 0 to 16 {
      %v = affine.load %0[%i + 1] : memref<17xi32>
      affine.store %v, %0[%i] : memref<17xi32>
    }
    affine.for %i = 0 to 16 {
      affine.store %c1, %2[%i] : memref<16xi32>
      affine.store %c1, %2[%i] : memref<16xi32>
    }
    affine.for %i = 0 to 4 {
      affine.for %j = 0 to 4 {
        affine.store %c1, %2[%i + %j] : memref<16xi32>
      }
    }
    affine.for %i = 0 to 8 {
      affine.store %c1, %1[%i, %i] : memref<8x8xi32>
    }
    affine.for %i = 0 to 16 {
      affine.store %c1, %2[15 - %i] : memref<16xi32>
    }
    affine.for %i = 0 to 8 {
      affine.for %j = 0 to 8 {
        %v = affine.load %0[%i + %j] : memref<17xi32>
        affine.store %v, %1[%i, 0] : memref<8x8xi32>
      }
    }
    %r = affine.for %i = 0 to 16 iter_args(%a = %c1) -> (i32) {
      %v = affine.load %2[%i] : memref<16xi32>
      %b = arith.addi %a, %v : i32
      affine.yield %b : i32
    }
    affine.store %r, %0[0] : memref<17xi32>
    affine.for %i = 0 to %n {
      affine.store %c1, %2[%i] : memref<16xi32>
    }
    affine.for %i = 0 to 16 {
      %3 = memref.get_global @t : memref<16xi32>
      affine.store %c1, %3[%i] : memref<16xi32>
    }
    %4 = memref.get_global @t : memref<16xi32>
    affine.for %i = 0 to 16 {
      %v = affine.load %4[%i] : memref<16xi32>
      affine.store %v, %2[%i] : memref<16xi32>
    }
    affine.for %i = 0 to 8 {
      %v = affine.load %0[%i] : memref<17xi32>
      affine.for %j = 0 to 8 {
        affine.store %v, %1[%i, %j] : memref<8x8xi32>
      }
    }
    return
  }
}
