// Block addresses and indirect branches, as in a switch table, and the ub
// dialect, also at the start of a line as an op with no results would be,
// break rule 3 wherever they are.  No block of @f is unreachable, though
// mlir-opt prints the successor of llvm.indirectbr at the start of a line
// and canonicalize moves llvm.blockaddress out of its block
module attributes {llvm.data_layout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128", llvm.target_triple = "x86_64-pc-linux-gnu"} {
llvm.mlir.global private constant @".L9"() {alignment = 8 : i64} : !llvm.ptr {
  %t1 = llvm.blockaddress <function = @"f", tag = <id = 1>> : !llvm.ptr  // mlir-lint: forbidden op
  llvm.return %t1 : !llvm.ptr
}
llvm.func @"f"(%a0: i32) -> i32 {
  %c1_i32 = arith.constant 1 : i32
  cf.br ^L2
^L2:
  %t1 = arith.addi %a0, %c1_i32 : i32
  %t2 = llvm.blockaddress <function = @"f", tag = <id = 1>> : !llvm.ptr  // mlir-lint: forbidden op
  llvm.indirectbr %t2 : !llvm.ptr, [  // mlir-lint: forbidden op
  ^L1
  ]
^L1:
  llvm.blocktag <id = 1>  // mlir-lint: forbidden op
  llvm.return %t1 : i32
}
func.func @"g"() -> i32 {
  %t1 = ub.poison : i32  // mlir-lint: forbidden op
  %t2 =
    ub.poison : i32  // mlir-lint: forbidden op
  %t3 = arith.addi %t1, %t2 : i32
  func.return %t3 : i32
}
}
