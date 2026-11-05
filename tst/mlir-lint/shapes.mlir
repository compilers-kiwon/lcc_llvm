// The shapes of the back end's output: llvm ops for memory, globals,
// calls through a pointer and va_start; arith, cf and func for the rest;
// constants in hex; a loop, a switch and a block argument.  None breaks a
// rule
!s1 = !llvm.struct<packed (array<4 x i8>, ptr, array<4 x i8>)>
module attributes {llvm.data_layout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128", llvm.target_triple = "x86_64-pc-linux-gnu"} {
func.func private @"abs"(i32) -> i32
func.func private @"setjmp"(!llvm.ptr) -> i32 attributes {passthrough = ["returns_twice"]}
llvm.func @"printf"(!llvm.ptr, ...) -> i32
llvm.mlir.global private constant @".L5"("%d %g\0A\00") {alignment = 1 : i64} : !llvm.array<7 x i8>
llvm.mlir.global external @"d"(0x3FB999999999999A : f64) {alignment = 8 : i64} : f64
llvm.mlir.global external @"f"(0x3DCCCCCD : f32) {alignment = 4 : i64} : f32
llvm.mlir.global external @"ld"(0x3FFBCCCCCCCCCCCCCCCD : f80) {alignment = 16 : i64} : f80
llvm.mlir.global external @"z"() {alignment = 8 : i64} : !llvm.array<16 x i8> {
  %t1 = llvm.mlir.zero : !llvm.array<16 x i8>
  llvm.return %t1 : !llvm.array<16 x i8>
}
llvm.mlir.global internal @"tab"() {alignment = 8 : i64} : !s1 {
  %t1 = llvm.mlir.undef : !s1
  %t2 = llvm.mlir.constant("\01\00\00\00") : !llvm.array<4 x i8>
  %t3 = llvm.insertvalue %t2, %t1[0] : !s1
  %t4 = llvm.mlir.addressof @"z" : !llvm.ptr
  %t5 = llvm.getelementptr %t4[4] : (!llvm.ptr) -> !llvm.ptr, i8
  %t6 = llvm.insertvalue %t5, %t3[1] : !s1
  %t7 = llvm.mlir.zero : !llvm.array<4 x i8>
  %t8 = llvm.insertvalue %t7, %t6[2] : !s1
  llvm.return %t8 : !s1
}
func.func @"sq"(%a0: i32) -> i32 attributes {llvm.linkage = #llvm.linkage<internal>} {
  %c1_i32 = arith.constant 1 : i32
  %retval = llvm.alloca %c1_i32 x i32 : (i32) -> !llvm.ptr
  %x.1 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  llvm.store %a0, %x.1 : i32, !llvm.ptr
  %t1 = llvm.load %x.1 : !llvm.ptr -> i32
  %t2 = arith.muli %t1, %t1 : i32
  llvm.store %t2, %retval : i32, !llvm.ptr
  cf.br ^L1
^L1:
  %t3 = llvm.load %retval : !llvm.ptr -> i32
  func.return %t3 : i32
}
func.func @"main"() -> i32 {
  %c0_i32 = arith.constant 0 : i32
  %c1_i32 = arith.constant 1 : i32
  %c3_i32 = arith.constant 3 : i32
  %c10_i32 = arith.constant 10 : i32
  %c-1_i32 = arith.constant -1 : i32
  %c16_i64 = arith.constant 16 : i64
  %false = arith.constant false
  %cst0 = arith.constant 0x3FB999999999999A : f64
  %cst1 = arith.constant 0x3FFBCCCCCCCCCCCCCCCD : f80
  %null = llvm.mlir.zero : !llvm.ptr
  %.L5.addr = llvm.mlir.addressof @".L5" : !llvm.ptr
  %z.addr = llvm.mlir.addressof @"z" : !llvm.ptr
  %ld.addr = llvm.mlir.addressof @"ld" : !llvm.ptr
  %printf.addr = llvm.mlir.addressof @"printf" : !llvm.ptr
  %sq.fn = func.constant @"sq" : (i32) -> i32
  %sq.addr = builtin.unrealized_conversion_cast %sq.fn : (i32) -> i32 to !llvm.ptr
  %retval = llvm.alloca %c1_i32 x i32 : (i32) -> !llvm.ptr
  %i.2 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %b.3 = llvm.alloca %c1_i32 x !llvm.array<16 x i8> {alignment = 8 : i64} : (i32) -> !llvm.ptr
  llvm.call_intrinsic "llvm.memcpy.p0.p0.i64"(%b.3, %z.addr, %c16_i64, %false) : (!llvm.ptr {llvm.align = 8 : i64}, !llvm.ptr {llvm.align = 8 : i64}, i64, i1) -> ()
  llvm.store volatile %c0_i32, %i.2 : i32, !llvm.ptr
  cf.br ^L2
^L2:
  %t1 = llvm.load volatile %i.2 : !llvm.ptr -> i32
  %t2 = func.call @"sq"(%t1) : (i32) -> i32
  %t3 = arith.sitofp %t2 : i32 to f64
  %t4 = arith.mulf %t3, %cst0 : f64
  %t5 = llvm.call %printf.addr(%.L5.addr, %t2, %t4) vararg(!llvm.func<i32 (ptr, ...)>) : !llvm.ptr, (!llvm.ptr, i32, f64) -> i32
  %t6 = arith.addi %t1, %c1_i32 : i32
  llvm.store volatile %t6, %i.2 : i32, !llvm.ptr
  %t7 = arith.cmpi slt, %t6, %c10_i32 : i32
  cf.cond_br %t7, ^L2, ^b1
^b1:
  %t8 = llvm.call %sq.addr(%c3_i32) vararg(!llvm.func<i32 (...)>) : !llvm.ptr, (i32) -> i32
  %t9 = func.call @"abs"(%c-1_i32) : (i32) -> i32
  cf.switch %t8 : i32, [
    default: ^L4,
    1: ^L3,
    9: ^b2(%ld.addr : !llvm.ptr)
  ]
^b2(%t10: !llvm.ptr):
  %t11 = llvm.load %t10 : !llvm.ptr -> f80
  %t12 = arith.cmpf une, %t11, %cst1 : f80
  cf.cond_br %t12, ^L4, ^L3
^L3:
  %t13 = llvm.ptrtoint %null : !llvm.ptr to i64
  %t14 = arith.trunci %t13 : i64 to i32
  llvm.store %t14, %retval : i32, !llvm.ptr
  cf.br ^L1
^L4:
  llvm.store %t9, %retval : i32, !llvm.ptr
  cf.br ^L1
^L1:
  %t15 = llvm.load %retval : !llvm.ptr -> i32
  func.return %t15 : i32
}
llvm.func @"first"(%a0: i32, ...) -> i32 {
  %c1_i32 = arith.constant 1 : i32
  %ap.4 = llvm.alloca %c1_i32 x !llvm.array<24 x i8> {alignment = 8 : i64} : (i32) -> !llvm.ptr
  llvm.intr.vastart %ap.4 : !llvm.ptr
  llvm.intr.vaend %ap.4 : !llvm.ptr
  llvm.return %a0 : i32
}
}
