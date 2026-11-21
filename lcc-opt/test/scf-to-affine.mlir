// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// RUN: lcc-opt --lcc-scf-to-affine --split-input-file %s | FileCheck %s
// RUN: lcc-opt --lcc-scf-to-affine=remarks=true --split-input-file \
// RUN:   --mlir-print-op-on-diagnostic=false %s 2>&1 >/dev/null \
// RUN:   | FileCheck %s --check-prefix=REMARK
//
// lcc-scf-to-affine raises loop nests that only compute and access memrefs
// of memref.get_global at affine indices within the memref to affine.for,
// affine.load and affine.store, and leaves the others alone.  The inputs
// are what lcc-index-loops and lcc-globals-to-memref make of lcc's loops.

// The nest of convfix.c (see to-affine.mlir): the indices are index
// arithmetic and, on 32-bit targets, i32 arithmetic of the induction
// variables, scaled by shifts, which do not wrap for these bounds.

// CHECK-LABEL: func.func @conv()
//   CHECK-DAG:   %[[BIAS:.*]] = memref.get_global @bias : memref<32xi32>
//   CHECK-DAG:   %[[W:.*]] = memref.get_global @w : memref<512xi8>
//   CHECK-DAG:   %[[IN:.*]] = memref.get_global @in : memref<1024xi8>
//   CHECK-DAG:   %[[OUT:.*]] = memref.get_global @out : memref<2048xi8>
//       CHECK:   affine.for %[[P:.*]] = 0 to 64 {
//       CHECK:     affine.for %[[O:.*]] = 0 to 32 {
//       CHECK:       %[[B:.*]] = affine.load %[[BIAS]][%[[O]]] : memref<32xi32>
//       CHECK:       %[[ACC:.*]] = affine.for %[[I:.*]] = 0 to 16 iter_args(%[[A:.*]] = %[[B]]) -> (i32) {
//       CHECK:         affine.load %[[W]][%[[O]] * 16 + %[[I]]] : memref<512xi8>
//       CHECK:         affine.load %[[IN]][%[[P]] * 16 + %[[I]]] : memref<1024xi8>
//       CHECK:         affine.yield %{{.*}} : i32
//       CHECK:       }
//       CHECK:       affine.store %{{.*}}, %[[OUT]][%[[P]] * 32 + %[[O]]] : memref<2048xi8>
//   CHECK-NOT:   scf.for
//   CHECK-NOT:   memref.load
//   CHECK-NOT:   memref.store
//       CHECK:   return
// REMARK: remark: lcc-scf-to-affine: raised a nest of 3 loops
module {
  memref.global @bias : memref<32xi32> = dense<0>
  memref.global @w : memref<512xi8> = dense<0>
  memref.global @in : memref<1024xi8> = dense<0>
  memref.global @out : memref<2048xi8> = dense<0>
  func.func @conv() {
    %c0 = arith.constant 0 : index
    %c1 = arith.constant 1 : index
    %c16 = arith.constant 16 : index
    %c32 = arith.constant 32 : index
    %c64 = arith.constant 64 : index
    %c4_i32 = arith.constant 4 : i32
    %c5_i32 = arith.constant 5 : i32
    %c-128_i32 = arith.constant -128 : i32
    %c127_i32 = arith.constant 127 : i32
    %bias = memref.get_global @bias : memref<32xi32>
    %w = memref.get_global @w : memref<512xi8>
    %in = memref.get_global @in : memref<1024xi8>
    %out = memref.get_global @out : memref<2048xi8>
    scf.for %p = %c0 to %c64 step %c1 {
      %p32 = arith.index_cast %p : index to i32
      %prow = arith.shli %p32, %c4_i32 : i32
      %orow = arith.shli %p32, %c5_i32 : i32
      scf.for %o = %c0 to %c32 step %c1 {
        %b = memref.load %bias[%o] : memref<32xi32>
        %o16 = arith.muli %o, %c16 : index
        %acc = scf.for %i = %c0 to %c16 step %c1 iter_args(%a = %b) -> (i32) {
          %wi = arith.addi %o16, %i : index
          %wv = memref.load %w[%wi] : memref<512xi8>
          %we = arith.extsi %wv : i8 to i32
          %i32 = arith.index_cast %i : index to i32
          %x = arith.addi %prow, %i32 : i32
          %xe = arith.extsi %x : i32 to i64
          %xi = arith.index_cast %xe : i64 to index
          %iv = memref.load %in[%xi] : memref<1024xi8>
          %ie = arith.extsi %iv : i8 to i32
          %m = arith.muli %we, %ie : i32
          %s = arith.addi %a, %m : i32
          scf.yield %s : i32
        }
        %lo = arith.maxsi %acc, %c-128_i32 : i32
        %hi = arith.minsi %lo, %c127_i32 : i32
        %t = arith.trunci %hi : i32 to i8
        %o32 = arith.index_cast %o : index to i32
        %y = arith.addi %orow, %o32 : i32
        %yi = arith.index_cast %y : i32 to index
        memref.store %t, %out[%yi] : memref<2048xi8>
      }
    }
    return
  }
}

// -----

// A bound that is a parameter, an integer at the top level of the
// function: the pass casts it to index right after its definition, here
// at the start of the function, and uses it as a symbol.  The index does
// not depend on that loop, and an scf.if stays in the raised loop.

// CHECK-LABEL: func.func @rows(
//  CHECK-SAME:     %[[N:.*]]: i32
//       CHECK:   %[[NI:.*]] = arith.index_cast %[[N]] : i32 to index
//       CHECK:   affine.for %{{.*}} = 0 to %[[NI]] {
//       CHECK:     affine.for %[[J:.*]] = 0 to 8 {
//       CHECK:       %[[V:.*]] = affine.load %{{.*}}[%[[J]]] : memref<8xi32>
//       CHECK:       scf.if
//       CHECK:       affine.store %{{.*}}, %{{.*}}[%[[J]]] : memref<8xi32>
// REMARK: remark: lcc-scf-to-affine: raised a nest of 2 loops
module {
  memref.global @g : memref<8xi32> = dense<0>
  func.func @rows(%n: i32) {
    %c0 = arith.constant 0 : index
    %c1 = arith.constant 1 : index
    %c8 = arith.constant 8 : index
    %one = arith.constant 1 : i32
    %zero = arith.constant 0 : i32
    %g = memref.get_global @g : memref<8xi32>
    %ni = arith.index_cast %n : i32 to index
    scf.for %i = %c0 to %ni step %c1 {
      scf.for %j = %c0 to %c8 step %c1 {
        %v = memref.load %g[%j] : memref<8xi32>
        %neg = arith.cmpi slt, %v, %zero : i32
        %w = scf.if %neg -> (i32) {
          scf.yield %zero : i32
        } else {
          %u = arith.addi %v, %one : i32
          scf.yield %u : i32
        }
        memref.store %w, %g[%j] : memref<8xi32>
      }
    }
    return
  }
}

// -----

// A triangular nest: the inner loop starts at the outer induction variable,
// a dimension of its lower bound map; the index stays in 0..7.

// CHECK-LABEL: func.func @triangle()
//       CHECK:   affine.for %[[I:.*]] = 0 to 8 {
//       CHECK:     affine.for %[[J:.*]] = #{{.*}}(%[[I]]) to 8 {
//       CHECK:       affine.store %{{.*}}, %{{.*}}[%[[J]]] : memref<8xi8>
// REMARK: remark: lcc-scf-to-affine: raised a nest of 2 loops
module {
  memref.global @t : memref<8xi8> = dense<0>
  func.func @triangle() {
    %c0 = arith.constant 0 : index
    %c1 = arith.constant 1 : index
    %c8 = arith.constant 8 : index
    %v = arith.constant 7 : i8
    %t = memref.get_global @t : memref<8xi8>
    scf.for %i = %c0 to %c8 step %c1 {
      scf.for %j = %i to %c8 step %c1 {
        memref.store %v, %t[%j] : memref<8xi8>
      }
    }
    return
  }
}

// -----

// Loops that stay, each for one reason; the clean loop inside a loop that
// stays is raised on its own when it does not use the outer loop.

// CHECK-LABEL: func.func @stay(
//       CHECK:   scf.for
//       CHECK:     func.call @f()
//       CHECK:     affine.for %{{.*}} = 0 to 4 {
//       CHECK:       affine.store %{{.*}}, %{{.*}}[%{{.*}}] : memref<16xi32>
// CHECK-COUNT-13:   scf.for
//   CHECK-NOT:   affine.
//       CHECK:   return
// REMARK: remark: lcc-scf-to-affine: loop stays: func.call
// REMARK: remark: lcc-scf-to-affine: raised an inner nest of 1 loop
// REMARK: remark: lcc-scf-to-affine: inner loop stays: uses a value of an enclosing region
// REMARK: remark: lcc-scf-to-affine: inner loop stays: uses a value of an enclosing region
// REMARK: remark: lcc-scf-to-affine: loop stays: llvm.load
// REMARK: remark: lcc-scf-to-affine: loop stays: llvm.store
// REMARK: remark: lcc-scf-to-affine: loop stays: index may be out of bounds
// REMARK: remark: lcc-scf-to-affine: loop stays: index may be out of bounds
// REMARK: remark: lcc-scf-to-affine: loop stays: not affine
// REMARK: remark: lcc-scf-to-affine: loop stays: not affine
// REMARK: remark: lcc-scf-to-affine: loop stays: unsigned loop
// REMARK: remark: lcc-scf-to-affine: loop stays: step not a positive constant
// REMARK: remark: lcc-scf-to-affine: loop stays: not an index loop
// REMARK: remark: lcc-scf-to-affine: loop stays: memref not from memref.get_global
// REMARK: remark: lcc-scf-to-affine: loop stays: memref.alloca
module {
  memref.global @h : memref<16xi32> = dense<0>
  func.func private @f()
  func.func private @g() -> i32
  func.func @stay(%p: !llvm.ptr, %k: i32, %s: index) {
    %c0 = arith.constant 0 : index
    %c1 = arith.constant 1 : index
    %c4 = arith.constant 4 : index
    %c16 = arith.constant 16 : index
    %c17 = arith.constant 17 : index
    %c0_i32 = arith.constant 0 : i32
    %c1_i32 = arith.constant 1 : i32
    %c16_i32 = arith.constant 16 : i32
    %v = arith.constant 1 : i32
    %h = memref.get_global @h : memref<16xi32>
    // A call: affine analysis does not see what it accesses.  The inner
    // loop alone qualifies; the next one uses the outer induction variable
    // and the last a bound computed in the outer loop.
    scf.for %i = %c0 to %c4 step %c1 {
      func.call @f() : () -> ()
      scf.for %j = %c0 to %c4 step %c1 {
        memref.store %v, %h[%j] : memref<16xi32>
      }
      scf.for %j = %c0 to %c4 step %c1 {
        %x = arith.addi %i, %j : index
        memref.store %v, %h[%x] : memref<16xi32>
      }
      %n = func.call @g() : () -> i32
      %ni = arith.index_cast %n : i32 to index
      %r = scf.for %j = %c0 to %ni step %c1 iter_args(%a = %c0_i32) -> (i32) {
        %s1 = arith.addi %a, %v : i32
        scf.yield %s1 : i32
      }
      memref.store %r, %h[%c0] : memref<16xi32>
    }
    // Accesses through pointers.
    scf.for %i = %c0 to %c16 step %c1 {
      %e = llvm.load %p : !llvm.ptr -> i32
      memref.store %e, %h[%i] : memref<16xi32>
    }
    scf.for %i = %c0 to %c16 step %c1 {
      llvm.store %v, %p : i32, !llvm.ptr
    }
    // Indices that may be out of the memref: i in 0..16, and a parameter.
    scf.for %i = %c0 to %c17 step %c1 {
      memref.store %v, %h[%i] : memref<16xi32>
    }
    scf.for %i = %c0 to %c4 step %c1 {
      %ki = arith.index_cast %k : i32 to index
      %x = arith.addi %i, %ki : index
      memref.store %v, %h[%x] : memref<16xi32>
    }
    // Indices that are not affine: i * i, and an index loaded from memory.
    scf.for %i = %c0 to %c4 step %c1 {
      %x = arith.muli %i, %i : index
      memref.store %v, %h[%x] : memref<16xi32>
    }
    scf.for %i = %c0 to %c16 step %c1 {
      %e = memref.load %h[%i] : memref<16xi32>
      %b = arith.andi %e, %c16_i32 : i32
      %x = arith.index_cast %b : i32 to index
      memref.store %v, %h[%x] : memref<16xi32>
    }
    // An unsigned loop, a step that is not a constant, an i32 loop.
    scf.for unsigned %i = %c0 to %c16 step %c1 {
      memref.store %v, %h[%i] : memref<16xi32>
    }
    scf.for %i = %c0 to %c16 step %s {
      memref.store %v, %h[%i] : memref<16xi32>
    }
    scf.for %i = %c0_i32 to %c16_i32 step %c1_i32 : i32 {
      %x = arith.index_cast %i : i32 to index
      memref.store %v, %h[%x] : memref<16xi32>
    }
    // A memref that is not a global, and an allocation.
    %a = memref.alloca() : memref<16xi32>
    scf.for %i = %c0 to %c16 step %c1 {
      memref.store %v, %a[%i] : memref<16xi32>
    }
    scf.for %i = %c0 to %c16 step %c1 {
      %b = memref.alloca() : memref<16xi32>
      memref.store %v, %h[%i] : memref<16xi32>
    }
    return
  }
}

// -----

// An llvm.func (lcc's variadic functions) is not an affine scope.

// CHECK-LABEL: llvm.func @vararg(
//       CHECK:   scf.for
// REMARK: remark: lcc-scf-to-affine: loop stays: in llvm.func
module {
  memref.global @l : memref<4xi32> = dense<0>
  llvm.func @vararg(%n: i32, ...) {
    %c0 = arith.constant 0 : index
    %c1 = arith.constant 1 : index
    %c4 = arith.constant 4 : index
    %v = arith.constant 1 : i32
    %l = memref.get_global @l : memref<4xi32>
    scf.for %i = %c0 to %c4 step %c1 {
      memref.store %v, %l[%i] : memref<4xi32>
    }
    llvm.return
  }
}

// -----

// A nest that uses the result of an earlier one as a bound: raised last
// first, the later one takes the result of the new affine.for.

// CHECK-LABEL: func.func @chain()
//       CHECK:   %[[R:.*]] = affine.for %{{.*}} = 0 to 4 iter_args
//       CHECK:   %[[RI:.*]] = arith.index_cast %[[R]] : i32 to index
//       CHECK:   affine.for %{{.*}} = 0 to %[[RI]] {
// REMARK: remark: lcc-scf-to-affine: raised a nest of 1 loop
// REMARK: remark: lcc-scf-to-affine: raised a nest of 1 loop
module {
  memref.global @c : memref<4xi32> = dense<0>
  func.func @chain() {
    %c0 = arith.constant 0 : index
    %c1 = arith.constant 1 : index
    %c4 = arith.constant 4 : index
    %z = arith.constant 0 : i32
    %cm = memref.get_global @c : memref<4xi32>
    %r = scf.for %i = %c0 to %c4 step %c1 iter_args(%a = %z) -> (i32) {
      %e = memref.load %cm[%i] : memref<4xi32>
      %s = arith.addi %a, %e : i32
      scf.yield %s : i32
    }
    %ri = arith.index_cast %r : i32 to index
    scf.for %i = %c0 to %ri step %c1 {
      %e = memref.load %cm[%c0] : memref<4xi32>
      memref.store %e, %cm[%c1] : memref<4xi32>
    }
    return
  }
}
