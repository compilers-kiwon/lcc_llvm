// A block is known by the line of its terminator, and named by its label
// if that starts a line: an unreachable block that ends on the line of
// another block, or whose label is after something else on its line, is
// only counted
// mlir-lint: 2 of 9 blocks unreachable
module attributes {llvm.data_layout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128", llvm.target_triple = "x86_64-pc-linux-gnu"} {
func.func @"f"(%a0: i32) -> i32 {
  cf.br ^L1
^L1:
  cf.br ^L2  ^L9: cf.br ^L2
^L2:
  func.return %a0 : i32
}
func.func @"g"(%a0: i32) -> i32 {
  cf.br ^L1
^L1:
  cf.br ^L2  ^L9:
  cf.br ^L2
^L2:
  func.return %a0 : i32
}
}
