// A function calls an llvm.func through its address; a direct llvm.call
// breaks rule 2.  A violation is reported with its line
module attributes {llvm.data_layout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128", llvm.target_triple = "x86_64-pc-linux-gnu"} {
llvm.func @"g"(...) -> i32
func.func @"f"() -> i32 {
  %g.addr = llvm.mlir.addressof @"g" : !llvm.ptr
  %t1 = llvm.call @"g"() vararg(!llvm.func<i32 (...)>) : () -> i32  // mlir-lint: direct llvm.call: %t1 = llvm.call @"g"()
  %t2 = llvm.call %g.addr() vararg(!llvm.func<i32 (...)>) : !llvm.ptr, () -> i32
  %t3 = arith.addi %t1, %t2 : i32
  func.return %t3 : i32
}
}
