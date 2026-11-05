// A block that nothing branches to breaks rule 5
module attributes {llvm.data_layout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128", llvm.target_triple = "x86_64-pc-linux-gnu"} {
func.func @"f"(%a0: i32) -> i32 {
  cf.br ^L1
^L9:  // mlir-lint: block ^L9 is unreachable
  cf.br ^L1
^L1:
  func.return %a0 : i32
}
}
