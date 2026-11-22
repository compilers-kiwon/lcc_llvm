// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// RUN: lcc-opt --lcc-distribute-reductions --split-input-file %s \
// RUN:   | FileCheck %s
// RUN: lcc-opt --lcc-distribute-reductions=remarks=true --split-input-file \
// RUN:   --mlir-print-op-on-diagnostic=false %s 2>&1 >/dev/null \
// RUN:   | FileCheck %s --check-prefix=REMARK
// RUN: lcc-opt "--lcc-distribute-reductions=remarks=true max-buffer-bytes=4096" \
// RUN:   --split-input-file --mlir-print-op-on-diagnostic=false %s 2>&1 \
// RUN:   >/dev/null | FileCheck %s --check-prefix=LIMIT
//
// lcc-distribute-reductions expands the iter_args of a reduction loop into
// a buffer indexed by the loops around it and splits those loops into a
// nest that stores the initial values, a nest that reduces in the buffer
// and a nest that uses the results, when affine dependence analysis shows
// that no dependence is carried by those loops; likewise, without a
// buffer, for a loop that accumulates in memory.  The inputs are in the
// form lcc-to-affine and lcc-expand-shapes give.

// The nest of convfix.c: bias into the buffer, the products added to it in
// the order of i, and the clamped results into out.

// CHECK-LABEL: func.func @conv()
//       CHECK:   %[[BUF:.*]] = memref.alloca() : memref<64x32xi32>
//       CHECK:   affine.for %[[P:.*]] = 0 to 64 {
//  CHECK-NEXT:     affine.for %[[O:.*]] = 0 to 32 {
//  CHECK-NEXT:       %[[B:.*]] = affine.load %{{.*}}[%[[O]]] : memref<32xi32>
//  CHECK-NEXT:       affine.store %[[B]], %[[BUF]][%[[P]], %[[O]]] : memref<64x32xi32>
//  CHECK-NEXT:     }
//  CHECK-NEXT:   }
//  CHECK-NEXT:   affine.for %[[P:.*]] = 0 to 64 {
//  CHECK-NEXT:     affine.for %[[O:.*]] = 0 to 32 {
//  CHECK-NEXT:       affine.for %[[I:.*]] = 0 to 16 {
//  CHECK-NEXT:         %[[A:.*]] = affine.load %[[BUF]][%[[P]], %[[O]]] : memref<64x32xi32>
//  CHECK-NEXT:         %[[WV:.*]] = affine.load %{{.*}}[%[[O]], %[[I]]] : memref<32x16xi8>
//  CHECK-NEXT:         %[[WE:.*]] = arith.extsi %[[WV]] : i8 to i32
//  CHECK-NEXT:         %[[IV:.*]] = affine.load %{{.*}}[%[[P]], %[[I]]] : memref<64x16xi8>
//  CHECK-NEXT:         %[[IE:.*]] = arith.extsi %[[IV]] : i8 to i32
//  CHECK-NEXT:         %[[M:.*]] = arith.muli %[[WE]], %[[IE]] : i32
//  CHECK-NEXT:         %[[S:.*]] = arith.addi %[[A]], %[[M]] : i32
//  CHECK-NEXT:         affine.store %[[S]], %[[BUF]][%[[P]], %[[O]]] : memref<64x32xi32>
//  CHECK-NEXT:       }
//  CHECK-NEXT:     }
//  CHECK-NEXT:   }
//  CHECK-NEXT:   affine.for %[[P:.*]] = 0 to 64 {
//  CHECK-NEXT:     affine.for %[[O:.*]] = 0 to 32 {
//  CHECK-NEXT:       %[[R:.*]] = affine.load %[[BUF]][%[[P]], %[[O]]] : memref<64x32xi32>
//  CHECK-NEXT:       %[[LO:.*]] = arith.maxsi %[[R]], %{{.*}} : i32
//  CHECK-NEXT:       %[[HI:.*]] = arith.minsi %[[LO]], %{{.*}} : i32
//  CHECK-NEXT:       %[[T:.*]] = arith.trunci %[[HI]] : i32 to i8
//  CHECK-NEXT:       affine.store %[[T]], %{{.*}}[%[[P]], %[[O]]] : memref<64x32xi8>
//  CHECK-NEXT:     }
//  CHECK-NEXT:   }
//  CHECK-NEXT:   return
// REMARK: remark: lcc-distribute-reductions: distributed 2 loops around it with 1 buffer of 8192 bytes
// LIMIT: remark: lcc-distribute-reductions: reduction stays: buffers of 8192 bytes, more than the 4096 left
module {
  memref.global @bias : memref<32xi32> = dense<0>
  memref.global @w : memref<32x16xi8> = dense<0>
  memref.global @in : memref<64x16xi8> = dense<0>
  memref.global @out : memref<64x32xi8> = dense<0>
  func.func @conv() {
    %c127_i32 = arith.constant 127 : i32
    %c-128_i32 = arith.constant -128 : i32
    %0 = memref.get_global @bias : memref<32xi32>
    %1 = memref.get_global @w : memref<32x16xi8>
    %2 = memref.get_global @in : memref<64x16xi8>
    %3 = memref.get_global @out : memref<64x32xi8>
    affine.for %p = 0 to 64 {
      affine.for %o = 0 to 32 {
        %4 = affine.load %0[%o] : memref<32xi32>
        %5 = affine.for %i = 0 to 16 iter_args(%a = %4) -> (i32) {
          %9 = affine.load %1[%o, %i] : memref<32x16xi8>
          %10 = arith.extsi %9 : i8 to i32
          %11 = affine.load %2[%p, %i] : memref<64x16xi8>
          %12 = arith.extsi %11 : i8 to i32
          %13 = arith.muli %10, %12 : i32
          %14 = arith.addi %a, %13 : i32
          affine.yield %14 : i32
        }
        %6 = arith.maxsi %5, %c-128_i32 : i32
        %7 = arith.minsi %6, %c127_i32 : i32
        %8 = arith.trunci %7 : i32 to i8
        affine.store %8, %3[%p, %o] : memref<64x32xi8>
      }
    }
    return
  }
}

// -----

// A float reduction adds in the same order, starting from the bias.  A
// loop that does not start at 0 or steps by more than 1 indexes the buffer
// by its iteration number.  Values computed before the reduction loop: a
// computation of the loop variables is recomputed in the nest that uses
// it, a load is expanded into a buffer too.  (The first nest keeps the
// computation, which canonicalize removes.)

// CHECK-LABEL: func.func @fdot()
//   CHECK-DAG:   %[[BUF:.*]] = memref.alloca() : memref<5xf32>
//   CHECK-DAG:   %[[SBUF:.*]] = memref.alloca() : memref<5xf32>
//       CHECK:   affine.for %[[O:.*]] = 2 to 12 step 2 {
//  CHECK-NEXT:     %[[B:.*]] = affine.load %{{.*}}[%[[O]]] : memref<16xf32>
//  CHECK-NEXT:     %[[SC:.*]] = affine.load %{{.*}}[%[[O]]] : memref<16xf32>
//  CHECK-NEXT:     arith.index_cast
//  CHECK-NEXT:     arith.sitofp
//  CHECK-NEXT:     affine.store %[[B]], %[[BUF]][%[[O]] floordiv 2 - 1] : memref<5xf32>
//  CHECK-NEXT:     affine.store %[[SC]], %[[SBUF]][%[[O]] floordiv 2 - 1] : memref<5xf32>
//  CHECK-NEXT:   }
//  CHECK-NEXT:   affine.for %[[O:.*]] = 2 to 12 step 2 {
//  CHECK-NEXT:     affine.for %[[I:.*]] = 0 to 8 {
//  CHECK-NEXT:       %[[OI:.*]] = arith.index_cast %[[O]] : index to i32
//  CHECK-NEXT:       %[[OF:.*]] = arith.sitofp %[[OI]] : i32 to f32
//  CHECK-NEXT:       %[[A:.*]] = affine.load %[[BUF]][%[[O]] floordiv 2 - 1] : memref<5xf32>
//  CHECK-NEXT:       %[[X:.*]] = affine.load %{{.*}}[%[[O]], %[[I]]] : memref<16x8xf32>
//  CHECK-NEXT:       %[[Y:.*]] = arith.mulf %[[X]], %[[OF]] : f32
//  CHECK-NEXT:       %[[S:.*]] = arith.addf %[[A]], %[[Y]] : f32
//  CHECK-NEXT:       affine.store %[[S]], %[[BUF]][%[[O]] floordiv 2 - 1] : memref<5xf32>
//  CHECK-NEXT:     }
//  CHECK-NEXT:   }
//  CHECK-NEXT:   affine.for %[[O:.*]] = 2 to 12 step 2 {
//  CHECK-NEXT:     %[[R:.*]] = affine.load %[[BUF]][%[[O]] floordiv 2 - 1] : memref<5xf32>
//  CHECK-NEXT:     %[[SC:.*]] = affine.load %[[SBUF]][%[[O]] floordiv 2 - 1] : memref<5xf32>
//  CHECK-NEXT:     %[[Z:.*]] = arith.mulf %[[R]], %[[SC]] : f32
//  CHECK-NEXT:     affine.store %[[Z]], %{{.*}}[%[[O]]] : memref<16xf32>
//  CHECK-NEXT:   }
//  CHECK-NEXT:   return
// REMARK: remark: lcc-distribute-reductions: distributed 1 loop around it with 2 buffers of 40 bytes
module {
  memref.global @fb : memref<16xf32> = dense<0.0>
  memref.global @fs : memref<16xf32> = dense<0.0>
  memref.global @fx : memref<16x8xf32> = dense<0.0>
  memref.global @fy : memref<16xf32> = dense<0.0>
  func.func @fdot() {
    %0 = memref.get_global @fb : memref<16xf32>
    %1 = memref.get_global @fs : memref<16xf32>
    %2 = memref.get_global @fx : memref<16x8xf32>
    %3 = memref.get_global @fy : memref<16xf32>
    affine.for %o = 2 to 12 step 2 {
      %b = affine.load %0[%o] : memref<16xf32>
      %s = affine.load %1[%o] : memref<16xf32>
      %oi = arith.index_cast %o : index to i32
      %of = arith.sitofp %oi : i32 to f32
      %r = affine.for %i = 0 to 8 iter_args(%a = %b) -> (f32) {
        %x = affine.load %2[%o, %i] : memref<16x8xf32>
        %y = arith.mulf %x, %of : f32
        %z = arith.addf %a, %y : f32
        affine.yield %z : f32
      }
      %t = arith.mulf %r, %s : f32
      affine.store %t, %3[%o] : memref<16xf32>
    }
    return
  }
}

// -----

// A global read before the reduction loop and written after it at the same
// element: the dependence is within an iteration, from the first nest to
// the last, and the loop is distributed.  Written at the next element, the
// value read in the next iteration comes from the last nest of this one:
// the loop is carried, and the loop stays.  So does the anti dependence of
// reading the next element and writing this one, which distributing would
// keep, as the pass takes any carried dependence for one it may not.

// CHECK-LABEL: func.func @same()
//       CHECK:   memref.alloca() : memref<32xi32>
//   CHECK-NOT:   iter_args
// CHECK-LABEL: func.func @flow()
//       CHECK:   iter_args
// CHECK-LABEL: func.func @anti()
//       CHECK:   iter_args
// REMARK: remark: lcc-distribute-reductions: distributed 1 loop around it with 1 buffer of 128 bytes
// REMARK: remark: lcc-distribute-reductions: reduction stays: a dependence carried by loop 1 of 1 around it
// REMARK: remark: lcc-distribute-reductions: reduction stays: a dependence carried by loop 1 of 1 around it
module {
  memref.global @g : memref<33xi32> = dense<0>
  memref.global @x : memref<32x8xi32> = dense<0>
  func.func @same() {
    %0 = memref.get_global @g : memref<33xi32>
    %1 = memref.get_global @x : memref<32x8xi32>
    affine.for %o = 0 to 32 {
      %b = affine.load %0[%o] : memref<33xi32>
      %r = affine.for %i = 0 to 8 iter_args(%a = %b) -> (i32) {
        %v = affine.load %1[%o, %i] : memref<32x8xi32>
        %s = arith.addi %a, %v : i32
        affine.yield %s : i32
      }
      affine.store %r, %0[%o] : memref<33xi32>
    }
    return
  }
  func.func @flow() {
    %0 = memref.get_global @g : memref<33xi32>
    %1 = memref.get_global @x : memref<32x8xi32>
    affine.for %o = 0 to 32 {
      %b = affine.load %0[%o] : memref<33xi32>
      %r = affine.for %i = 0 to 8 iter_args(%a = %b) -> (i32) {
        %v = affine.load %1[%o, %i] : memref<32x8xi32>
        %s = arith.addi %a, %v : i32
        affine.yield %s : i32
      }
      affine.store %r, %0[%o + 1] : memref<33xi32>
    }
    return
  }
  func.func @anti() {
    %0 = memref.get_global @g : memref<33xi32>
    %1 = memref.get_global @x : memref<32x8xi32>
    affine.for %o = 0 to 32 {
      %b = affine.load %0[%o + 1] : memref<33xi32>
      %r = affine.for %i = 0 to 8 iter_args(%a = %b) -> (i32) {
        %v = affine.load %1[%o, %i] : memref<32x8xi32>
        %s = arith.addi %a, %v : i32
        affine.yield %s : i32
      }
      affine.store %r, %0[%o] : memref<33xi32>
    }
    return
  }
}

// -----

// A store in the reduction loop to an element that does not depend on the
// loops around it is a dependence they carry.

// CHECK-LABEL: func.func @scratch()
//       CHECK:   iter_args
// REMARK: remark: lcc-distribute-reductions: reduction stays: a dependence carried by loop 1 of 2 around it
module {
  memref.global @t : memref<8xi32> = dense<0>
  memref.global @x : memref<4x4x8xi32> = dense<0>
  memref.global @y : memref<4x4xi32> = dense<0>
  func.func @scratch() {
    %c0 = arith.constant 0 : i32
    %0 = memref.get_global @t : memref<8xi32>
    %1 = memref.get_global @x : memref<4x4x8xi32>
    %2 = memref.get_global @y : memref<4x4xi32>
    affine.for %p = 0 to 4 {
      affine.for %o = 0 to 4 {
        %r = affine.for %i = 0 to 8 iter_args(%a = %c0) -> (i32) {
          %v = affine.load %1[%p, %o, %i] : memref<4x4x8xi32>
          affine.store %v, %0[%i] : memref<8xi32>
          %s = arith.addi %a, %v : i32
          affine.yield %s : i32
        }
        affine.store %r, %2[%p, %o] : memref<4x4xi32>
      }
    }
    return
  }
}

// -----

// The loops around a reduction loop must have constant bounds, no other
// loop beside it, and reach memory through one memref value each.

// CHECK-LABEL: func.func @bounds(
//       CHECK:   iter_args
// CHECK-LABEL: func.func @two()
//       CHECK:   iter_args
//       CHECK:   iter_args
// CHECK-LABEL: func.func @values()
//       CHECK:   iter_args
// REMARK: remark: lcc-distribute-reductions: reduction stays: a loop around it has bounds that are not constant
// REMARK: remark: lcc-distribute-reductions: reduction stays: another loop beside it
// REMARK: remark: lcc-distribute-reductions: reduction stays: another loop beside it
// REMARK: remark: lcc-distribute-reductions: reduction stays: one memory reached through different memref values
module {
  memref.global @x : memref<32x8xi32> = dense<0>
  memref.global @y : memref<32xi32> = dense<0>
  func.func @bounds(%n: index) {
    %c0 = arith.constant 0 : i32
    %0 = memref.get_global @x : memref<32x8xi32>
    %1 = memref.get_global @y : memref<32xi32>
    affine.for %o = 0 to min affine_map<()[s0] -> (s0, 32)>()[%n] {
      %r = affine.for %i = 0 to 8 iter_args(%a = %c0) -> (i32) {
        %v = affine.load %0[%o, %i] : memref<32x8xi32>
        %s = arith.addi %a, %v : i32
        affine.yield %s : i32
      }
      affine.store %r, %1[%o] : memref<32xi32>
    }
    return
  }
  func.func @two() {
    %c0 = arith.constant 0 : i32
    %0 = memref.get_global @x : memref<32x8xi32>
    %1 = memref.get_global @y : memref<32xi32>
    affine.for %o = 0 to 32 {
      %r = affine.for %i = 0 to 8 iter_args(%a = %c0) -> (i32) {
        %v = affine.load %0[%o, %i] : memref<32x8xi32>
        %s = arith.addi %a, %v : i32
        affine.yield %s : i32
      }
      %q = affine.for %i = 0 to 8 iter_args(%a = %r) -> (i32) {
        %v = affine.load %0[%o, %i] : memref<32x8xi32>
        %s = arith.muli %a, %v : i32
        affine.yield %s : i32
      }
      affine.store %q, %1[%o] : memref<32xi32>
    }
    return
  }
  func.func @values() {
    %c0 = arith.constant 0 : i32
    %0 = memref.get_global @x : memref<32x8xi32>
    %1 = memref.get_global @y : memref<32xi32>
    affine.for %o = 0 to 32 {
      %2 = memref.get_global @y : memref<32xi32>
      %b = affine.load %2[%o] : memref<32xi32>
      %r = affine.for %i = 0 to 8 iter_args(%a = %b) -> (i32) {
        %v = affine.load %0[%o, %i] : memref<32x8xi32>
        %s = arith.addi %a, %v : i32
        affine.yield %s : i32
      }
      affine.store %r, %1[%o] : memref<32xi32>
    }
    return
  }
}

// -----

// A chain of reduction loops, `for (r...) for (s...) t += ...` of a 3x3
// convolution with a local sum, shares one buffer: both loops lose their
// iter_args and the innermost loads and stores the buffer.  The band is
// inside another loop whose body has a store: the loops are distributed
// inside it.

// CHECK-LABEL: func.func @conv3()
//       CHECK:   %[[BUF:.*]] = memref.alloca() : memref<8x8xi32>
//       CHECK:   affine.for %[[N:.*]] = 0 to 2 {
//       CHECK:     affine.store
//  CHECK-NEXT:     affine.for %[[P:.*]] = 0 to 8 {
//  CHECK-NEXT:       affine.for %[[Q:.*]] = 0 to 8 {
//  CHECK-NEXT:         affine.store %{{.*}}, %[[BUF]][%[[P]], %[[Q]]] : memref<8x8xi32>
//  CHECK-NEXT:       }
//  CHECK-NEXT:     }
//  CHECK-NEXT:     affine.for %[[P:.*]] = 0 to 8 {
//  CHECK-NEXT:       affine.for %[[Q:.*]] = 0 to 8 {
//  CHECK-NEXT:         affine.for %[[R:.*]] = 0 to 3 {
//  CHECK-NEXT:           affine.for %[[S:.*]] = 0 to 3 {
//  CHECK-NEXT:             %[[A:.*]] = affine.load %[[BUF]][%[[P]], %[[Q]]] : memref<8x8xi32>
//  CHECK-NEXT:             affine.load %{{.*}}[%[[N]], %[[P]] + %[[R]], %[[Q]] + %[[S]]] : memref<2x10x10xi32>
//       CHECK:             affine.store %{{.*}}, %[[BUF]][%[[P]], %[[Q]]] : memref<8x8xi32>
//       CHECK:     affine.for %[[P:.*]] = 0 to 8 {
//  CHECK-NEXT:       affine.for %[[Q:.*]] = 0 to 8 {
//  CHECK-NEXT:         affine.load %[[BUF]][%[[P]], %[[Q]]] : memref<8x8xi32>
//  CHECK-NEXT:         affine.store %{{.*}}, %{{.*}}[%[[N]], %[[P]], %[[Q]]] : memref<2x8x8xi32>
//   CHECK-NOT:   iter_args
// REMARK: remark: lcc-distribute-reductions: distributed 2 loops around it with 1 buffer of 256 bytes
// REMARK-NOT: remark
module {
  memref.global @img : memref<2x10x10xi32> = dense<0>
  memref.global @k : memref<3x3xi32> = dense<0>
  memref.global @res : memref<2x8x8xi32> = dense<0>
  memref.global @done : memref<2xi32> = dense<0>
  func.func @conv3() {
    %c0 = arith.constant 0 : i32
    %c1 = arith.constant 1 : i32
    %0 = memref.get_global @img : memref<2x10x10xi32>
    %1 = memref.get_global @k : memref<3x3xi32>
    %2 = memref.get_global @res : memref<2x8x8xi32>
    %3 = memref.get_global @done : memref<2xi32>
    affine.for %n = 0 to 2 {
      affine.store %c1, %3[%n] : memref<2xi32>
      affine.for %p = 0 to 8 {
        affine.for %q = 0 to 8 {
          %t = affine.for %r = 0 to 3 iter_args(%a = %c0) -> (i32) {
            %u = affine.for %s = 0 to 3 iter_args(%b = %a) -> (i32) {
              %x = affine.load %0[%n, %p + %r, %q + %s] : memref<2x10x10xi32>
              %y = affine.load %1[%r, %s] : memref<3x3xi32>
              %m = arith.muli %x, %y : i32
              %z = arith.addi %b, %m : i32
              affine.yield %z : i32
            }
            affine.yield %u : i32
          }
          affine.store %t, %2[%n, %p, %q] : memref<2x8x8xi32>
        }
      }
    }
    return
  }
}

// -----

// A loop that accumulates in memory after a store of the initial value, as
// in `c[i][j] = 0; for (k...) c[i][j] += a[i][k] * b[k][j]`: the loops
// around it are distributed the same way, without a buffer.  When the next
// iteration reads what this one stores, they stay.

// CHECK-LABEL: func.func @mm()
//   CHECK-NOT:   memref.alloca
//       CHECK:   affine.for %[[I:.*]] = 0 to 4 {
//  CHECK-NEXT:     affine.for %[[J:.*]] = 0 to 4 {
//  CHECK-NEXT:       affine.store %{{.*}}, %[[C:.*]][%[[I]], %[[J]]] : memref<4x4xi32>
//  CHECK-NEXT:     }
//  CHECK-NEXT:   }
//  CHECK-NEXT:   affine.for %[[I:.*]] = 0 to 4 {
//  CHECK-NEXT:     affine.for %[[J:.*]] = 0 to 4 {
//  CHECK-NEXT:       affine.for %[[K:.*]] = 0 to 8 {
//  CHECK-NEXT:         affine.load %[[C]][%[[I]], %[[J]]] : memref<4x4xi32>
//       CHECK:         affine.store %{{.*}}, %[[C]][%[[I]], %[[J]]] : memref<4x4xi32>
// CHECK-LABEL: func.func @shift()
//       CHECK:   affine.for
//  CHECK-NEXT:     affine.store
//  CHECK-NEXT:     affine.for
// REMARK: remark: lcc-distribute-reductions: distributed 2 loops around it with 0 buffers of 0 bytes
// REMARK: remark: lcc-distribute-reductions: loop stays: a dependence carried by loop 1 of 1 around it
module {
  memref.global @a : memref<4x8xi32> = dense<0>
  memref.global @b : memref<8x4xi32> = dense<0>
  memref.global @c : memref<4x4xi32> = dense<0>
  memref.global @x : memref<9xi32> = dense<0>
  func.func @mm() {
    %c0 = arith.constant 0 : i32
    %0 = memref.get_global @a : memref<4x8xi32>
    %1 = memref.get_global @b : memref<8x4xi32>
    %2 = memref.get_global @c : memref<4x4xi32>
    affine.for %i = 0 to 4 {
      affine.for %j = 0 to 4 {
        affine.store %c0, %2[%i, %j] : memref<4x4xi32>
        affine.for %k = 0 to 8 {
          %s = affine.load %2[%i, %j] : memref<4x4xi32>
          %u = affine.load %0[%i, %k] : memref<4x8xi32>
          %v = affine.load %1[%k, %j] : memref<8x4xi32>
          %m = arith.muli %u, %v : i32
          %t = arith.addi %s, %m : i32
          affine.store %t, %2[%i, %j] : memref<4x4xi32>
        }
      }
    }
    return
  }
  func.func @shift() {
    %c0 = arith.constant 0 : i32
    %0 = memref.get_global @a : memref<4x8xi32>
    %3 = memref.get_global @x : memref<9xi32>
    affine.for %i = 0 to 4 {
      affine.store %c0, %3[%i] : memref<9xi32>
      affine.for %k = 0 to 8 {
        %s = affine.load %3[%i + 1] : memref<9xi32>
        %u = affine.load %0[%i, %k] : memref<4x8xi32>
        %t = arith.addi %s, %u : i32
        affine.store %t, %3[%i + 1] : memref<9xi32>
      }
    }
    return
  }
}

// -----

// Candidates in candidates: the loop over k accumulates u[i][m] inside the
// loop over j, which accumulates s[i].  The first round distributes the
// loop over m around the inner one, the second the loop over i around the
// outer one, holding the nests of the first.

// CHECK-LABEL: func.func @rounds()
//       CHECK:   affine.for %[[I:.*]] = 0 to 4 {
//  CHECK-NEXT:     affine.store %{{.*}}, %{{.*}}[%[[I]]] : memref<4xi32>
//  CHECK-NEXT:   }
//  CHECK-NEXT:   affine.for %[[I:.*]] = 0 to 4 {
//  CHECK-NEXT:     affine.for %[[J:.*]] = 0 to 4 {
//       CHECK:       affine.store %{{.*}}, %{{.*}}[%[[I]]] : memref<4xi32>
//  CHECK-NEXT:       affine.for %[[M:.*]] = 0 to 4 {
//  CHECK-NEXT:         affine.store %{{.*}}, %{{.*}}[%[[I]], %[[M]]] : memref<4x4xi32>
//  CHECK-NEXT:       }
//  CHECK-NEXT:       affine.for %[[M:.*]] = 0 to 4 {
//  CHECK-NEXT:         affine.for %[[K:.*]] = 0 to 4 {
// REMARK: remark: lcc-distribute-reductions: distributed 1 loop around it with 0 buffers of 0 bytes
// REMARK: remark: lcc-distribute-reductions: distributed 1 loop around it with 0 buffers of 0 bytes
// REMARK-NOT: remark
module {
  memref.global @s : memref<4xi32> = dense<0>
  memref.global @u : memref<4x4xi32> = dense<0>
  memref.global @x : memref<4xi32> = dense<0>
  func.func @rounds() {
    %c0 = arith.constant 0 : i32
    %c1 = arith.constant 1 : i32
    %0 = memref.get_global @s : memref<4xi32>
    %1 = memref.get_global @u : memref<4x4xi32>
    %2 = memref.get_global @x : memref<4xi32>
    affine.for %i = 0 to 4 {
      affine.store %c0, %0[%i] : memref<4xi32>
      affine.for %j = 0 to 4 {
        %a = affine.load %0[%i] : memref<4xi32>
        %b = arith.addi %a, %c1 : i32
        affine.store %b, %0[%i] : memref<4xi32>
        affine.for %m = 0 to 4 {
          affine.store %c0, %1[%i, %m] : memref<4x4xi32>
          affine.for %k = 0 to 4 {
            %v = affine.load %1[%i, %m] : memref<4x4xi32>
            %w = affine.load %2[%k] : memref<4xi32>
            %z = arith.addi %v, %w : i32
            affine.store %z, %1[%i, %m] : memref<4x4xi32>
          }
        }
      }
    }
    return
  }
}
