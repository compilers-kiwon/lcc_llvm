// Strings and comments may hold anything: a C string with what looks like
// a decimal constant or a forbidden op, also after an escaped quote,
// breaks no rule
module attributes {llvm.data_layout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128", llvm.target_triple = "x86_64-pc-linux-gnu"} {
llvm.mlir.global private constant @".L1"("x = 2.5 : f64\0A\00") {alignment = 1 : i64} : !llvm.array<15 x i8>
llvm.mlir.global private constant @".L2"("llvm.blockaddress =ub.poison \22 1.5 : f32\00") {alignment = 1 : i64} : !llvm.array<41 x i8>
llvm.mlir.global private constant @".L3"("a \"llvm.call @g\" \\") {alignment = 1 : i64} : !llvm.array<18 x i8>
func.func @"f"() -> !llvm.ptr {
  // %t = llvm.mul %a, %b : i32 and llvm.call @"g"() are not here
  %.L2.addr = llvm.mlir.addressof @".L2" : !llvm.ptr
  func.return %.L2.addr : !llvm.ptr
}
}
