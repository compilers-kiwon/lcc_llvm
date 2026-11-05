// A label may have blanks before and after it, the punctuation of MLIR
// names, arguments, and an operation after it on its line: its block is
// still named when it cannot be reached
module attributes {llvm.data_layout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128", llvm.target_triple = "x86_64-pc-linux-gnu"} {
func.func @"f"(%a0: i32) -> i32 {
  cf.br ^L1
  ^L5.x_1$-y :  // mlir-lint: block ^L5.x_1$-y is unreachable
  cf.br ^L1
  ^b2(%t1: i32) :  // mlir-lint: block ^b2 is unreachable
  cf.br ^L1
^L6: cf.br ^L1  // mlir-lint: block ^L6 is unreachable
^L1:
  func.return %a0 : i32
}
}
