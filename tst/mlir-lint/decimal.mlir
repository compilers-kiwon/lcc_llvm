// Floating-point constants in decimal break rule 4 wherever they are, in
// every float type, also at the start of a line
module attributes {llvm.data_layout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128", llvm.target_triple = "x86_64-pc-linux-gnu"} {
llvm.mlir.global external @"g"(1.0E300 : f64) {alignment = 8 : i64} : f64  // mlir-lint: decimal float literal
llvm.mlir.global external @"h"() {alignment = 4 : i64} : f32 {
  %t1 = llvm.mlir.constant(3. : f32) : f32  // mlir-lint: decimal float literal
  llvm.return %t1 : f32
}
func.func @"f"() -> f80 {
  %cst0 = arith.constant 2.5 : f64  // mlir-lint: decimal float literal
  %cst1 = arith.constant -1.000000e-01 : f80  // mlir-lint: decimal float literal
  %cst2 = arith.constant 1.5 : f16  // mlir-lint: decimal float literal
  %cst3 = arith.constant
2.5e+3 : f128  // mlir-lint: decimal float literal
  %t1 = arith.extf %cst0 : f64 to f80
  %t2 = arith.addf %t1, %cst1 : f80
  func.return %t2 : f80
}
}
