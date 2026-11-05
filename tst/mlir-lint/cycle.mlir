// Blocks that branch to each other but cannot be reached from the entry
// block break rule 5; a loop that can be reached does not
module attributes {llvm.data_layout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128", llvm.target_triple = "x86_64-pc-linux-gnu"} {
func.func @"f"(%a0: i32) -> i32 {
  %c1_i32 = arith.constant 1 : i32
  cf.br ^L1
^L2:  // mlir-lint: block ^L2 is unreachable
  %t1 = arith.addi %a0, %c1_i32 : i32
  cf.br ^L3
^L3:  // mlir-lint: block ^L3 is unreachable
  cf.br ^L2
^L1:
  %t2 = arith.cmpi slt, %a0, %c1_i32 : i32
  cf.cond_br %t2, ^L1, ^L4
^L4:
  func.return %a0 : i32
}
}
