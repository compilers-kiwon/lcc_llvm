// What mlir-opt does not accept is reported with its error
module attributes {llvm.data_layout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128", llvm.target_triple = "x86_64-pc-linux-gnu"} {
func.func @"f"(%a0: i32) -> i64 {
  func.return %a0 : i32  // mlir-lint: error: type of return operand 0 ('i32') doesn't match
}
}
