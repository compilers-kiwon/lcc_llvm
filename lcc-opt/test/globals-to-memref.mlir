// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// RUN: lcc-opt --lcc-globals-to-memref --split-input-file %s | FileCheck %s
// RUN: lcc-opt --lcc-globals-to-memref=remarks=true --split-input-file \
// RUN:   --mlir-print-op-on-diagnostic=false %s 2>&1 >/dev/null \
// RUN:   | FileCheck %s --check-prefix=REMARK
// RUN: lcc-opt --lcc-globals-to-memref --split-input-file \
// RUN:   --mlir-print-elementsattrs-with-hex-if-larger=1 %s \
// RUN:   | FileCheck %s --check-prefix=HEX
// RUN: lcc-opt --lcc-globals-to-memref --split-input-file %s \
// RUN:   | lcc-opt --split-input-file \
// RUN:     --mlir-print-elementsattrs-with-hex-if-larger=1 \
// RUN:   | FileCheck %s --check-prefix=HEX
//
// lcc-globals-to-memref turns lcc's global arrays into 1-D memref.global,
// and their loads and stores into memref accesses where it can show the
// element index.  The globals are written as lcc writes them; the C they
// come from is in the comments.  The remarks say why a global stays.

// int a[4]; int get(int i) { return a[i]; } and a store of a[2]: the index
// of a[i] is i, sign-extended.

// CHECK-LABEL: func.func @get(
//  CHECK-SAME:     %[[I:.*]]: i32
//       CHECK:   %[[M:.*]] = memref.get_global @a : memref<4xi32>
//       CHECK:   %[[X:.*]] = arith.index_cast %[[I]] : i32 to index
//       CHECK:   %[[V:.*]] = memref.load %[[M]][%[[X]]] : memref<4xi32>
//       CHECK:   %[[C2:.*]] = arith.constant 2 : index
//       CHECK:   memref.store %[[V]], %[[M]][%[[C2]]] : memref<4xi32>
//       CHECK:   return %[[V]]
//       CHECK: memref.global @a : memref<4xi32> = dense<0> {alignment = 16 : i64}
// CHECK-NOT: llvm.mlir.global
func.func @get(%i: i32) -> i32 {
  %c2_i64 = arith.constant 2 : i64
  %a = llvm.mlir.addressof @a : !llvm.ptr
  %e = arith.extsi %i : i32 to i64
  %o = arith.shli %e, %c2_i64 : i64
  %p = llvm.getelementptr %a[%o] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %v = llvm.load %p : !llvm.ptr -> i32
  %q = llvm.getelementptr %a[8] : (!llvm.ptr) -> !llvm.ptr, i8
  llvm.store %v, %q : i32, !llvm.ptr
  return %v : i32
}
// REMARK: remark: lcc-globals-to-memref: @a becomes 'memref<4xi32>'
llvm.mlir.global external @a() {alignment = 16 : i64} : !llvm.array<16 x i8> {
  %0 = llvm.mlir.zero : !llvm.array<16 x i8>
  llvm.return %0 : !llvm.array<16 x i8>
}

// -----

// a[k + 1]: lcc adds in int, which may wrap, then extends; the index is
// the extended sum, not k + 1 in index.  An int offset scaled in int, as
// on 32-bit targets, may wrap and need not be a multiple of 4: the access
// keeps the pointer.

// CHECK-LABEL: func.func @plus1(
//  CHECK-SAME:     %[[K:.*]]: i32
//       CHECK:   %[[P:.*]] = llvm.inttoptr
//       CHECK:   %[[T:.*]] = arith.addi %[[K]], %{{.*}} : i32
//       CHECK:   %[[X:.*]] = arith.index_cast %[[T]] : i32 to index
//       CHECK:   memref.load %{{.*}}[%[[X]]] : memref<4xi32>
//       CHECK:   %[[Q:.*]] = llvm.getelementptr %[[P]]
//       CHECK:   llvm.load %[[Q]] : !llvm.ptr -> i32
func.func @plus1(%k: i32) -> i32 {
  %c1 = arith.constant 1 : i32
  %c2 = arith.constant 2 : i32
  %c2_i64 = arith.constant 2 : i64
  %a = llvm.mlir.addressof @a1 : !llvm.ptr
  %t = arith.addi %k, %c1 : i32
  %e = arith.extsi %t : i32 to i64
  %o = arith.shli %e, %c2_i64 : i64
  %p = llvm.getelementptr %a[%o] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %v = llvm.load %p : !llvm.ptr -> i32
  %s = arith.shli %k, %c2 : i32
  %se = arith.extsi %s : i32 to i64
  %q = llvm.getelementptr %a[%se] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %w = llvm.load %q : !llvm.ptr -> i32
  %r = arith.addi %v, %w : i32
  return %r : i32
}
// REMARK: remark: lcc-globals-to-memref: @a1 becomes 'memref<4xi32>'
llvm.mlir.global external @a1() {alignment = 16 : i64} : !llvm.array<16 x i8> {
  %0 = llvm.mlir.zero : !llvm.array<16 x i8>
  llvm.return %0 : !llvm.array<16 x i8>
}

// -----

// Tables, unpacked little-endian into their elements:
//
//   static const signed char w[8] = { -1, 2, -128, 127, 0, 5, -7, 3 };
//   int tab[4] = { 1, -2, 300000, -5 };
//   short sh[3] = { -1, 2 };      (bytes and a zero run)
//   long lg[2] = { 1, -1 };

// CHECK: memref.global "private" constant @w : memref<8xi8> = dense<[-1, 2, -128, 127, 0, 5, -7, 3]> {alignment = 1 : i64}
// CHECK: memref.global @tab : memref<4xi32> = dense<[1, -2, 300000, -5]> {alignment = 16 : i64}
// CHECK: memref.global @sh : memref<3xi16> = dense<[-1, 2, 0]> {alignment = 2 : i64}
// CHECK: memref.global @lg : memref<2xi64> = dense<[1, -1]> {alignment = 16 : i64}
// CHECK-LABEL: func.func @sum(
//   CHECK-DAG:   memref.get_global @w : memref<8xi8>
//   CHECK-DAG:   memref.get_global @tab : memref<4xi32>
//   CHECK-DAG:   memref.get_global @sh : memref<3xi16>
//   CHECK-DAG:   memref.get_global @lg : memref<2xi64>
//   CHECK-NOT:   llvm.load
//       CHECK:   return
!s1 = !llvm.struct<packed (array<4 x i8>, array<2 x i8>)>
// REMARK: remark: lcc-globals-to-memref: @w becomes 'memref<8xi8>'
llvm.mlir.global internal constant @w("\FF\02\80\7F\00\05\F9\03") {alignment = 1 : i64} : !llvm.array<8 x i8>
// REMARK: remark: lcc-globals-to-memref: @tab becomes 'memref<4xi32>'
llvm.mlir.global external @tab("\01\00\00\00\FE\FF\FF\FF\E0\93\04\00\FB\FF\FF\FF") {alignment = 16 : i64} : !llvm.array<16 x i8>
// REMARK: remark: lcc-globals-to-memref: @sh becomes 'memref<3xi16>'
llvm.mlir.global external @sh() {alignment = 2 : i64} : !s1 {
  %v1 = llvm.mlir.undef : !s1
  %v2 = llvm.mlir.constant("\FF\FF\02\00") : !llvm.array<4 x i8>
  %v3 = llvm.insertvalue %v2, %v1[0] : !s1
  %v4 = llvm.mlir.zero : !llvm.array<2 x i8>
  %v5 = llvm.insertvalue %v4, %v3[1] : !s1
  llvm.return %v5 : !s1
}
// REMARK: remark: lcc-globals-to-memref: @lg becomes 'memref<2xi64>'
llvm.mlir.global external @lg("\01\00\00\00\00\00\00\00\FF\FF\FF\FF\FF\FF\FF\FF") {alignment = 16 : i64} : !llvm.array<16 x i8>
func.func @sum(%i: i32) -> i64 {
  %c1_i64 = arith.constant 1 : i64
  %c2_i64 = arith.constant 2 : i64
  %c3_i64 = arith.constant 3 : i64
  %w = llvm.mlir.addressof @w : !llvm.ptr
  %tab = llvm.mlir.addressof @tab : !llvm.ptr
  %sh = llvm.mlir.addressof @sh : !llvm.ptr
  %lg = llvm.mlir.addressof @lg : !llvm.ptr
  %e = arith.extsi %i : i32 to i64
  %p1 = llvm.getelementptr %w[%e] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %v1 = llvm.load %p1 : !llvm.ptr -> i8
  %o2 = arith.shli %e, %c2_i64 : i64
  %p2 = llvm.getelementptr %tab[%o2] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %v2 = llvm.load %p2 : !llvm.ptr -> i32
  %o3 = arith.shli %e, %c1_i64 : i64
  %p3 = llvm.getelementptr %sh[%o3] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %v3 = llvm.load %p3 : !llvm.ptr -> i16
  %o4 = arith.shli %e, %c3_i64 : i64
  %p4 = llvm.getelementptr %lg[%o4] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %v4 = llvm.load %p4 : !llvm.ptr -> i64
  %x1 = arith.extsi %v1 : i8 to i64
  %x2 = arith.extsi %v2 : i32 to i64
  %x3 = arith.extsi %v3 : i16 to i64
  %s1 = arith.addi %x1, %x2 : i64
  %s2 = arith.addi %s1, %x3 : i64
  %s3 = arith.addi %s2, %v4 : i64
  return %s3 : i64
}

// -----

// A long initializer, which lcc writes as packed structures of at most 256
// pieces, nested; and an extern declaration, which becomes a memref.global
// declaration.

// CHECK: memref.global @long : memref<6xi16> = dense<[1, 0, 0, 0, 2, 3]> {alignment = 2 : i64}
// CHECK-LABEL: func.func @pair(
// CHECK: memref.global @ext : memref<10xi32> {alignment = 4 : i64}
!t1 = !llvm.struct<packed (array<2 x i8>, array<6 x i8>)>
!t2 = !llvm.struct<packed (array<4 x i8>)>
!t3 = !llvm.struct<packed (!t1, !t2)>
// REMARK: remark: lcc-globals-to-memref: @long becomes 'memref<6xi16>'
llvm.mlir.global external @long() {alignment = 2 : i64} : !t3 {
  %v1 = llvm.mlir.undef : !t3
  %v2 = llvm.mlir.undef : !t1
  %v3 = llvm.mlir.constant("\01\00") : !llvm.array<2 x i8>
  %v4 = llvm.insertvalue %v3, %v2[0] : !t1
  %v5 = llvm.mlir.zero : !llvm.array<6 x i8>
  %v6 = llvm.insertvalue %v5, %v4[1] : !t1
  %v7 = llvm.insertvalue %v6, %v1[0] : !t3
  %v8 = llvm.mlir.undef : !t2
  %v9 = llvm.mlir.constant("\02\00\03\00") : !llvm.array<4 x i8>
  %v10 = llvm.insertvalue %v9, %v8[0] : !t2
  %v11 = llvm.insertvalue %v10, %v7[1] : !t3
  llvm.return %v11 : !t3
}
func.func @pair() -> i32 {
  %l = llvm.mlir.addressof @long : !llvm.ptr
  %p = llvm.getelementptr %l[10] : (!llvm.ptr) -> !llvm.ptr, i8
  %v = llvm.load %p : !llvm.ptr -> i16
  %x = arith.extsi %v : i16 to i32
  %e = llvm.mlir.addressof @ext : !llvm.ptr
  %q = llvm.getelementptr %e[36] : (!llvm.ptr) -> !llvm.ptr, i8
  %w = llvm.load %q : !llvm.ptr -> i32
  %s = arith.addi %x, %w : i32
  return %s : i32
}
// REMARK: remark: lcc-globals-to-memref: @ext becomes 'memref<10xi32>'
llvm.mlir.global external @ext() {alignment = 4 : i64} : !llvm.array<40 x i8>

// -----

// Uses that are not loads or stores at known element indices keep a
// pointer, made from the memref: a call argument, a volatile load, a load
// two bytes into an element, and a load through a pointer the global's
// address was stored to.  The other accesses become memref accesses.

// CHECK-LABEL: func.func @others(
//       CHECK:   %[[M:.*]] = memref.get_global @b : memref<8xi32>
//       CHECK:   %[[A:.*]] = memref.extract_aligned_pointer_as_index %[[M]] : memref<8xi32> -> index
//       CHECK:   %[[I:.*]] = arith.index_cast %[[A]] : index to i64
//       CHECK:   %[[P:.*]] = llvm.inttoptr %[[I]] : i64 to !llvm.ptr
//       CHECK:   call @use(%[[P]])
//       CHECK:   llvm.load volatile %[[P]] : !llvm.ptr -> i32
//       CHECK:   %[[Q:.*]] = llvm.getelementptr %[[P]][2] : (!llvm.ptr) -> !llvm.ptr, i8
//       CHECK:   llvm.load %[[Q]] : !llvm.ptr -> i32
//       CHECK:   llvm.store %[[P]], %{{.*}} : !llvm.ptr, !llvm.ptr
//       CHECK:   memref.load %[[M]][%{{.*}}] : memref<8xi32>
//       CHECK: memref.global @b : memref<8xi32> = dense<0>
func.func private @use(!llvm.ptr)
func.func @others(%slot: !llvm.ptr) -> i32 {
  %b = llvm.mlir.addressof @b : !llvm.ptr
  call @use(%b) : (!llvm.ptr) -> ()
  %v1 = llvm.load volatile %b : !llvm.ptr -> i32
  %q = llvm.getelementptr %b[2] : (!llvm.ptr) -> !llvm.ptr, i8
  %v2 = llvm.load %q : !llvm.ptr -> i32
  llvm.store %b, %slot : !llvm.ptr, !llvm.ptr
  %r = llvm.getelementptr %b[4] : (!llvm.ptr) -> !llvm.ptr, i8
  %v3 = llvm.load %r : !llvm.ptr -> i32
  %s = arith.addi %v1, %v2 : i32
  %t = arith.addi %s, %v3 : i32
  return %t : i32
}
// REMARK: remark: lcc-globals-to-memref: @b becomes 'memref<8xi32>'
llvm.mlir.global external @b() {alignment = 16 : i64} : !llvm.array<32 x i8> {
  %0 = llvm.mlir.zero : !llvm.array<32 x i8>
  llvm.return %0 : !llvm.array<32 x i8>
}

// -----

// On a 32-bit target lcc scales the index in i32, which may wrap, before
// extending it: c[k] with any int k keeps the pointer, c[i] in a loop
// over 0..3 becomes a memref access.  A double array of zeros becomes
// dense<0.0>.  Addresses wrap at 32 bits: two offsets below 2^31 whose sum
// may not be keep the pointer, an index truncated to int is extended.

// CHECK-LABEL: func.func @wrap2(
//  CHECK-SAME:     %{{.*}}: i64, %[[N:.*]]: index
//       CHECK:   %[[P:.*]] = llvm.inttoptr
//       CHECK:   %[[Q:.*]] = llvm.getelementptr %[[P]]
//       CHECK:   %[[R:.*]] = llvm.getelementptr %[[Q]]
//       CHECK:   llvm.load %[[R]] : !llvm.ptr -> i8
//       CHECK:   %[[T:.*]] = arith.index_cast %[[N]] : index to i32
//       CHECK:   %[[X:.*]] = arith.index_cast %[[T]] : i32 to index
//       CHECK:   memref.load %{{.*}}[%[[X]]] : memref<16xi8>
//       CHECK: memref.global @e : memref<16xi8> = dense<0>
// CHECK-LABEL: func.func @wrap(
//       CHECK:   %[[M:.*]] = memref.get_global @c : memref<4xi32>
//       CHECK:   llvm.load %{{.*}} : !llvm.ptr -> i32
//       CHECK:   scf.for %[[IV:.*]] =
//       CHECK:     memref.store %{{.*}}, %[[M]][%[[IV]]] : memref<4xi32>
//       CHECK:   memref.load %{{.*}}[%{{.*}}] : memref<2xf64>
//       CHECK: memref.global @c : memref<4xi32> = dense<0>
//       CHECK: memref.global @d : memref<2xf64> = dense<0.000000e+00>
module attributes {llvm.data_layout = "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64"} {
  func.func @wrap2(%k: i64, %n: index) -> i8 {
    %c33 = arith.constant 33 : i64
    %e = llvm.mlir.addressof @e : !llvm.ptr
    %h = arith.shrui %k, %c33 : i64
    %p = llvm.getelementptr %e[%h] : (!llvm.ptr, i64) -> !llvm.ptr, i8
    %q = llvm.getelementptr %p[%h] : (!llvm.ptr, i64) -> !llvm.ptr, i8
    %v = llvm.load %q : !llvm.ptr -> i8
    %t = arith.index_cast %n : index to i32
    %x = arith.extsi %t : i32 to i64
    %r = llvm.getelementptr %e[%x] : (!llvm.ptr, i64) -> !llvm.ptr, i8
    %w = llvm.load %r : !llvm.ptr -> i8
    %s = arith.addi %v, %w : i8
    return %s : i8
  }
  // REMARK: remark: lcc-globals-to-memref: @e becomes 'memref<16xi8>'
  llvm.mlir.global external @e() {alignment = 1 : i64} : !llvm.array<16 x i8> {
    %0 = llvm.mlir.zero : !llvm.array<16 x i8>
    llvm.return %0 : !llvm.array<16 x i8>
  }
  func.func @wrap(%k: i32) -> f64 {
    %c0 = arith.constant 0 : index
    %c1 = arith.constant 1 : index
    %c4 = arith.constant 4 : index
    %c2_i32 = arith.constant 2 : i32
    %c = llvm.mlir.addressof @c : !llvm.ptr
    %o = arith.shli %k, %c2_i32 : i32
    %e = arith.extsi %o : i32 to i64
    %p = llvm.getelementptr %c[%e] : (!llvm.ptr, i64) -> !llvm.ptr, i8
    %v = llvm.load %p : !llvm.ptr -> i32
    scf.for %i = %c0 to %c4 step %c1 {
      %x = arith.index_cast %i : index to i32
      %y = arith.shli %x, %c2_i32 : i32
      %z = arith.extsi %y : i32 to i64
      %q = llvm.getelementptr %c[%z] : (!llvm.ptr, i64) -> !llvm.ptr, i8
      llvm.store %v, %q : i32, !llvm.ptr
    }
    %d = llvm.mlir.addressof @d : !llvm.ptr
    %r = llvm.getelementptr %d[8] : (!llvm.ptr) -> !llvm.ptr, i8
    %f = llvm.load %r : !llvm.ptr -> f64
    return %f : f64
  }
  // REMARK: remark: lcc-globals-to-memref: @c becomes 'memref<4xi32>'
  llvm.mlir.global external @c() {alignment = 4 : i64} : !llvm.array<16 x i8> {
    %0 = llvm.mlir.zero : !llvm.array<16 x i8>
    llvm.return %0 : !llvm.array<16 x i8>
  }
  // REMARK: remark: lcc-globals-to-memref: @d becomes 'memref<2xf64>'
  llvm.mlir.global external @d() {alignment = 8 : i64} : !llvm.array<16 x i8> {
    %0 = llvm.mlir.zero : !llvm.array<16 x i8>
    llvm.return %0 : !llvm.array<16 x i8>
  }
}

// -----

// Globals that stay.

// CHECK-LABEL: func.func @stay(
//   CHECK-NOT:   memref.
//       CHECK:   return
//   CHECK-NOT:   memref.global
func.func private @take(!llvm.ptr)
func.func @stay(%i: i64) -> i64 {
  // int u; read as an int and as its first byte
  %u = llvm.mlir.addressof @u : !llvm.ptr
  %u1 = llvm.load %u : !llvm.ptr -> i32
  %u2 = llvm.load %u : !llvm.ptr -> i8
  // 6 bytes read as ints
  %six = llvm.mlir.addressof @six : !llvm.ptr
  %s1 = llvm.load %six : !llvm.ptr -> i32
  // int x, whose address int *px = &x holds
  %x = llvm.mlir.addressof @x : !llvm.ptr
  %x1 = llvm.load %x : !llvm.ptr -> i32
  %px = llvm.mlir.addressof @px : !llvm.ptr
  %px1 = llvm.load %px : !llvm.ptr -> !llvm.ptr
  // struct { char *s; long n; } sn = { "", 3 }, read as longs, and the
  // same nested as in long initializers
  %sn = llvm.mlir.addressof @sn : !llvm.ptr
  %sn1 = llvm.getelementptr %sn[8] : (!llvm.ptr) -> !llvm.ptr, i8
  %sn2 = llvm.load %sn1 : !llvm.ptr -> i64
  %dp = llvm.mlir.addressof @deep : !llvm.ptr
  %dp1 = llvm.load %dp : !llvm.ptr -> i32
  // char buf[8], only passed on
  %buf = llvm.mlir.addressof @buf : !llvm.ptr
  call @take(%buf) : (!llvm.ptr) -> ()
  // int n, char *names[2] = { "", "" }, extern int z0[]; thread_local,
  // weak, and long double
  %tl = llvm.mlir.addressof @tl : !llvm.ptr
  %tl1 = llvm.load %tl : !llvm.ptr -> i32
  %wk = llvm.mlir.addressof @wk : !llvm.ptr
  %wk1 = llvm.load %wk : !llvm.ptr -> i32
  %ld = llvm.mlir.addressof @ld : !llvm.ptr
  %ld1 = llvm.load %ld : !llvm.ptr -> f80
  %n = llvm.mlir.addressof @n : !llvm.ptr
  %n1 = llvm.load %n : !llvm.ptr -> i32
  %nm = llvm.mlir.addressof @names : !llvm.ptr
  %nm1 = llvm.load %nm : !llvm.ptr -> !llvm.ptr
  %z0 = llvm.mlir.addressof @z0 : !llvm.ptr
  %z1 = llvm.load %z0 : !llvm.ptr -> i32
  %r = arith.extsi %u1 : i32 to i64
  return %r : i64
}
// REMARK: remark: lcc-globals-to-memref: @u stays: loads and stores of different types
llvm.mlir.global external @u() {alignment = 4 : i64} : !llvm.array<4 x i8> {
  %0 = llvm.mlir.zero : !llvm.array<4 x i8>
  llvm.return %0 : !llvm.array<4 x i8>
}
// REMARK: remark: lcc-globals-to-memref: @six stays: size not a multiple of the element size
llvm.mlir.global external @six() {alignment = 4 : i64} : !llvm.array<6 x i8> {
  %0 = llvm.mlir.zero : !llvm.array<6 x i8>
  llvm.return %0 : !llvm.array<6 x i8>
}
// REMARK: remark: lcc-globals-to-memref: @x stays: address used by an initializer
llvm.mlir.global external @x() {alignment = 4 : i64} : !llvm.array<4 x i8> {
  %0 = llvm.mlir.zero : !llvm.array<4 x i8>
  llvm.return %0 : !llvm.array<4 x i8>
}
// REMARK: remark: lcc-globals-to-memref: @px stays: initializer with an address
llvm.mlir.global external @px() {alignment = 8 : i64} : !llvm.struct<packed (ptr)> {
  %0 = llvm.mlir.undef : !llvm.struct<packed (ptr)>
  %1 = llvm.mlir.addressof @x : !llvm.ptr
  %2 = llvm.insertvalue %1, %0[0] : !llvm.struct<packed (ptr)>
  llvm.return %2 : !llvm.struct<packed (ptr)>
}
// REMARK: remark: lcc-globals-to-memref: @str stays: address used by an initializer
llvm.mlir.global internal constant @str("\00") {alignment = 1 : i64} : !llvm.array<1 x i8>
// REMARK: remark: lcc-globals-to-memref: @sn stays: initializer with an address
llvm.mlir.global external @sn() {alignment = 8 : i64} : !llvm.struct<packed (ptr, array<8 x i8>)> {
  %0 = llvm.mlir.undef : !llvm.struct<packed (ptr, array<8 x i8>)>
  %1 = llvm.mlir.addressof @str : !llvm.ptr
  %2 = llvm.insertvalue %1, %0[0] : !llvm.struct<packed (ptr, array<8 x i8>)>
  %3 = llvm.mlir.constant("\03\00\00\00\00\00\00\00") : !llvm.array<8 x i8>
  %4 = llvm.insertvalue %3, %2[1] : !llvm.struct<packed (ptr, array<8 x i8>)>
  llvm.return %4 : !llvm.struct<packed (ptr, array<8 x i8>)>
}
// REMARK: remark: lcc-globals-to-memref: @deep stays: initializer with an address
llvm.mlir.global external @deep() {alignment = 8 : i64} : !llvm.struct<packed (struct<packed (array<4 x i8>, ptr)>)> {
  %0 = llvm.mlir.undef : !llvm.struct<packed (struct<packed (array<4 x i8>, ptr)>)>
  %1 = llvm.mlir.undef : !llvm.struct<packed (array<4 x i8>, ptr)>
  %2 = llvm.mlir.constant("\01\00\00\00") : !llvm.array<4 x i8>
  %3 = llvm.insertvalue %2, %1[0] : !llvm.struct<packed (array<4 x i8>, ptr)>
  %4 = llvm.mlir.addressof @str : !llvm.ptr
  %5 = llvm.insertvalue %4, %3[1] : !llvm.struct<packed (array<4 x i8>, ptr)>
  %6 = llvm.insertvalue %5, %0[0] : !llvm.struct<packed (struct<packed (array<4 x i8>, ptr)>)>
  llvm.return %6 : !llvm.struct<packed (struct<packed (array<4 x i8>, ptr)>)>
}
// REMARK: remark: lcc-globals-to-memref: @buf stays: no load or store
llvm.mlir.global external @buf() {alignment = 1 : i64} : !llvm.array<8 x i8> {
  %0 = llvm.mlir.zero : !llvm.array<8 x i8>
  llvm.return %0 : !llvm.array<8 x i8>
}
// REMARK: remark: lcc-globals-to-memref: @tl stays: attributes memref.global does not have
llvm.mlir.global external thread_local @tl() {alignment = 4 : i64} : !llvm.array<4 x i8> {
  %0 = llvm.mlir.zero : !llvm.array<4 x i8>
  llvm.return %0 : !llvm.array<4 x i8>
}
// REMARK: remark: lcc-globals-to-memref: @wk stays: linkage weak
llvm.mlir.global weak @wk() {alignment = 4 : i64} : !llvm.array<4 x i8> {
  %0 = llvm.mlir.zero : !llvm.array<4 x i8>
  llvm.return %0 : !llvm.array<4 x i8>
}
// REMARK: remark: lcc-globals-to-memref: @ld stays: loads and stores of an unsupported type
llvm.mlir.global external @ld() {alignment = 16 : i64} : !llvm.array<16 x i8> {
  %0 = llvm.mlir.zero : !llvm.array<16 x i8>
  llvm.return %0 : !llvm.array<16 x i8>
}
// REMARK: remark: lcc-globals-to-memref: @n stays: a scalar
llvm.mlir.global external @n(0 : i32) {alignment = 4 : i64} : i32
// REMARK: remark: lcc-globals-to-memref: @names stays: an array of pointers
llvm.mlir.global external @names() {alignment = 16 : i64} : !llvm.array<2 x ptr> {
  %0 = llvm.mlir.undef : !llvm.array<2 x ptr>
  %1 = llvm.mlir.addressof @empty : !llvm.ptr
  %2 = llvm.insertvalue %1, %0[0] : !llvm.array<2 x ptr>
  %3 = llvm.insertvalue %1, %2[1] : !llvm.array<2 x ptr>
  llvm.return %3 : !llvm.array<2 x ptr>
}
// REMARK: remark: lcc-globals-to-memref: @empty stays: address used by an initializer
llvm.mlir.global private constant @empty("\00") {alignment = 1 : i64} : !llvm.array<1 x i8>
// REMARK: remark: lcc-globals-to-memref: @z0 stays: no size (an incomplete array)
llvm.mlir.global external @z0() {alignment = 4 : i64} : !llvm.array<0 x i8>

// -----

// Float tables, bit for bit.  f32: 1.5, -0.0, +inf, -inf, a negative quiet
// NaN with payload 1, a signalling NaN, the smallest subnormal, a negative
// subnormal, 0.1.  f64: 1 + 2^-52, -0.0, +inf, a negative quiet NaN with a
// payload, a signalling NaN, the smallest subnormal, 0.1.  In hexadecimal
// (HEX) the dense values are the bytes of the initializers, also after the
// text form is read back; NaNs and infinities print in hexadecimal, the
// other values in decimals that read back exactly, so lcc-check-text,
// which lcc-opt runs when it writes text, lets them through.  A double
// table with a zero run in it, and a float table also read as an int,
// which stays.

// CHECK: memref.global "private" constant @ff : memref<9xf32> = dense<[1.500000e+00, -0.000000e+00, 0x7F800000, 0xFF800000, 0xFFC00001, 0x7F800001, 1.401300e-45, -1.17549421E-38, 1.000000e-01]> {alignment = 16 : i64}
// CHECK: memref.global @fd : memref<7xf64> = dense<[1.0000000000000002, -0.000000e+00, 0x7FF0000000000000, 0xFFF8000000000123, 0x7FF0000000000001, 4.940660e-324, 1.000000e-01]> {alignment = 16 : i64}
// CHECK: memref.global @dz : memref<4xf64> = dense<[2.500000e+00, 0.000000e+00, 0.000000e+00, -1.000000e+00]> {alignment = 16 : i64}
// CHECK-LABEL: func.func @floats(
//   CHECK-DAG:   memref.load %{{.*}}[%{{.*}}] : memref<9xf32>
//   CHECK-DAG:   memref.load %{{.*}}[%{{.*}}] : memref<7xf64>
//   CHECK-DAG:   memref.load %{{.*}}[%{{.*}}] : memref<4xf64>
//   CHECK-DAG:   llvm.load %{{.*}} : !llvm.ptr -> f32
//   CHECK-DAG:   llvm.load %{{.*}} : !llvm.ptr -> i32
//       CHECK:   return
// HEX: memref.global "private" constant @ff : memref<9xf32> = dense<"0x0000C03F000000800000807F000080FF0100C0FF0100807F01000000FFFF7F80CDCCCC3D">
// HEX: memref.global @fd : memref<7xf64> = dense<"0x010000000000F03F0000000000000080000000000000F07F230100000000F8FF010000000000F07F01000000000000009A9999999999B93F">
// HEX: memref.global @dz : memref<4xf64> = dense<"0x000000000000044000000000000000000000000000000000000000000000F0BF">
!z1 = !llvm.struct<packed (array<8 x i8>, array<16 x i8>, array<8 x i8>)>
// REMARK: remark: lcc-globals-to-memref: @ff becomes 'memref<9xf32>'
llvm.mlir.global internal constant @ff("\00\00\C0\3F\00\00\00\80\00\00\80\7F\00\00\80\FF\01\00\C0\FF\01\00\80\7F\01\00\00\00\FF\FF\7F\80\CD\CC\CC\3D") {alignment = 16 : i64} : !llvm.array<36 x i8>
// REMARK: remark: lcc-globals-to-memref: @fd becomes 'memref<7xf64>'
llvm.mlir.global external @fd("\01\00\00\00\00\00\F0\3F\00\00\00\00\00\00\00\80\00\00\00\00\00\00\F0\7F\23\01\00\00\00\00\F8\FF\01\00\00\00\00\00\F0\7F\01\00\00\00\00\00\00\00\9A\99\99\99\99\99\B9\3F") {alignment = 16 : i64} : !llvm.array<56 x i8>
// REMARK: remark: lcc-globals-to-memref: @dz becomes 'memref<4xf64>'
llvm.mlir.global external @dz() {alignment = 16 : i64} : !z1 {
  %0 = llvm.mlir.undef : !z1
  %1 = llvm.mlir.constant("\00\00\00\00\00\00\04\40") : !llvm.array<8 x i8>
  %2 = llvm.insertvalue %1, %0[0] : !z1
  %3 = llvm.mlir.zero : !llvm.array<16 x i8>
  %4 = llvm.insertvalue %3, %2[1] : !z1
  %5 = llvm.mlir.constant("\00\00\00\00\00\00\F0\BF") : !llvm.array<8 x i8>
  %6 = llvm.insertvalue %5, %4[2] : !z1
  llvm.return %6 : !z1
}
// REMARK: remark: lcc-globals-to-memref: @fp stays: loads and stores of different types
llvm.mlir.global external @fp("\00\00\C0\3F\00\00\00\80") {alignment = 4 : i64} : !llvm.array<8 x i8>
func.func @floats(%i: i32) -> f64 {
  %c2_i64 = arith.constant 2 : i64
  %c3_i64 = arith.constant 3 : i64
  %e = arith.extsi %i : i32 to i64
  %o4 = arith.shli %e, %c2_i64 : i64
  %o8 = arith.shli %e, %c3_i64 : i64
  %ff = llvm.mlir.addressof @ff : !llvm.ptr
  %p1 = llvm.getelementptr %ff[%o4] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %v1 = llvm.load %p1 : !llvm.ptr -> f32
  %fd = llvm.mlir.addressof @fd : !llvm.ptr
  %p2 = llvm.getelementptr %fd[%o8] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %v2 = llvm.load %p2 : !llvm.ptr -> f64
  %dz = llvm.mlir.addressof @dz : !llvm.ptr
  %p3 = llvm.getelementptr %dz[%o8] : (!llvm.ptr, i64) -> !llvm.ptr, i8
  %v3 = llvm.load %p3 : !llvm.ptr -> f64
  // the f32 table of fp read as a float and as its bits
  %fp = llvm.mlir.addressof @fp : !llvm.ptr
  %v4 = llvm.load %fp : !llvm.ptr -> f32
  %v5 = llvm.load %fp : !llvm.ptr -> i32
  %x1 = arith.extf %v1 : f32 to f64
  %x4 = arith.extf %v4 : f32 to f64
  %x5 = arith.sitofp %v5 : i32 to f64
  %s1 = arith.addf %x1, %v2 : f64
  %s2 = arith.addf %s1, %v3 : f64
  %s3 = arith.addf %s2, %x4 : f64
  %s4 = arith.addf %s3, %x5 : f64
  return %s4 : f64
}

// -----

// A big-endian target: initializer bytes stay, zeros do not.

// CHECK: memref.global @z : memref<2xi32> = dense<0>
module attributes {llvm.data_layout = "E-m:e-p:32:32-i64:64-n32-S64"} {
  func.func @bigend() -> i32 {
    %be = llvm.mlir.addressof @be : !llvm.ptr
    %v = llvm.load %be : !llvm.ptr -> i32
    %z = llvm.mlir.addressof @z : !llvm.ptr
    %w = llvm.load %z : !llvm.ptr -> i32
    %bf = llvm.mlir.addressof @bef : !llvm.ptr
    %f = llvm.load %bf : !llvm.ptr -> f32
    %fi = arith.fptosi %f : f32 to i32
    %s = arith.addi %v, %w : i32
    %t = arith.addi %s, %fi : i32
    return %t : i32
  }
  // REMARK: remark: lcc-globals-to-memref: @be stays: big-endian initializer
  llvm.mlir.global external @be("\00\00\00\01") {alignment = 4 : i64} : !llvm.array<4 x i8>
  // REMARK: remark: lcc-globals-to-memref: @bef stays: big-endian initializer
  llvm.mlir.global external @bef("\3F\C0\00\00") {alignment = 4 : i64} : !llvm.array<4 x i8>
  // REMARK: remark: lcc-globals-to-memref: @z becomes 'memref<2xi32>'
  llvm.mlir.global external @z() {alignment = 4 : i64} : !llvm.array<8 x i8> {
    %0 = llvm.mlir.zero : !llvm.array<8 x i8>
    llvm.return %0 : !llvm.array<8 x i8>
  }
}
