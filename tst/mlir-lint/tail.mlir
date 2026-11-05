// The last block of a region, as the exit of a function that loops
// forever, and a block with an argument, as where va_arg joins, break
// rule 5 too when they cannot be reached
module attributes {llvm.data_layout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128", llvm.target_triple = "x86_64-pc-linux-gnu"} {
func.func @"main"() -> i32 {
  %c1_i32 = arith.constant 1 : i32
  %retval = llvm.alloca %c1_i32 x i32 : (i32) -> !llvm.ptr
  cf.br ^L2
^L2:
  cf.br ^L2
^b1(%t1: !llvm.ptr):  // mlir-lint: block ^b1 is unreachable
  cf.br ^L1
^L1:  // mlir-lint: block ^L1 is unreachable
  %t2 = llvm.load %retval : !llvm.ptr -> i32
  func.return %t2 : i32
}
}
