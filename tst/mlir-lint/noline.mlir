// When mlir-opt reports no error at a line of the file, as for a
// location in the C source, its first message is reported
// mlir-lint: mlir-opt fails: x.c:3:5: error: type of return operand 0 ('i32') doesn't match
module attributes {llvm.data_layout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128", llvm.target_triple = "x86_64-pc-linux-gnu"} {
func.func @"f"(%a0: i32) -> i64 {
  func.return %a0 : i32 loc("x.c":3:5)
}
}
