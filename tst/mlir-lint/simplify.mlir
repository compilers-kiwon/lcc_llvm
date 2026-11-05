// A branch and a switch on a constant, blocks that are the same and an
// unused alloca: canonicalize with no rewrites and region-simplify=normal
// erases only the alloca, so every block is reachable, and none breaks a
// rule
module attributes {llvm.data_layout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128", llvm.target_triple = "x86_64-pc-linux-gnu"} {
func.func @"f"(%a0: i32) -> i32 {
  %true = arith.constant true
  %c1_i32 = arith.constant 1 : i32
  %x.1 = llvm.alloca %c1_i32 x i32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  cf.cond_br %true, ^L1, ^L2
^L1:
  cf.switch %c1_i32 : i32, [
    default: ^L3,
    1: ^L4
  ]
^L2:
  func.return %a0 : i32
^L3:
  func.return %a0 : i32
^L4:
  func.return %c1_i32 : i32
}
}
