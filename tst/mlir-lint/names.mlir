// Locals are named %<name>.<n>, and temporaries, whose names are numbers,
// %T<name>.<n>, as in llvm.c.  Before a float type such a name looks like
// a decimal constant (%f1.318 of tst/cq.c, %T817.29 of tst/paranoia.c,
// %d1.4, %T6.5, %x_1.5), as do names with the other punctuation of MLIR
// names, and %ub.11 of tst/sort.c looks like the ub dialect; none breaks
// a rule
module attributes {llvm.data_layout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128", llvm.target_triple = "x86_64-pc-linux-gnu"} {
func.func @"f"(%a0: f64, %a1: f32) -> f64 {
  %c1_i32 = arith.constant 1 : i32
  %retval = llvm.alloca %c1_i32 x f64 : (i32) -> !llvm.ptr
  %d1.4 = llvm.alloca %c1_i32 x f64 {alignment = 8 : i64} : (i32) -> !llvm.ptr
  %f1.318 = llvm.alloca %c1_i32 x f32 {alignment = 4 : i64} : (i32) -> !llvm.ptr
  %T6.5 = llvm.alloca %c1_i32 x f64 {alignment = 8 : i64} : (i32) -> !llvm.ptr
  %T817.29 = llvm.alloca %c1_i32 x f64 {alignment = 8 : i64} : (i32) -> !llvm.ptr
  %x_1.5 = llvm.alloca %c1_i32 x f64 {alignment = 8 : i64} : (i32) -> !llvm.ptr
  %x-1.5 = llvm.alloca %c1_i32 x f64 {alignment = 8 : i64} : (i32) -> !llvm.ptr
  %x$1.5 = llvm.alloca %c1_i32 x f64 {alignment = 8 : i64} : (i32) -> !llvm.ptr
  %x.1.5 = llvm.alloca %c1_i32 x f64 {alignment = 8 : i64} : (i32) -> !llvm.ptr
  %ub.11 = llvm.alloca %c1_i32 x !llvm.ptr {alignment = 8 : i64} : (i32) -> !llvm.ptr
  llvm.store %a0, %d1.4 : f64, !llvm.ptr
  llvm.store %a1, %f1.318 : f32, !llvm.ptr
  %t1 = llvm.load %d1.4 : !llvm.ptr -> f64
  llvm.store %t1, %T6.5 : f64, !llvm.ptr
  llvm.store %t1, %T817.29 : f64, !llvm.ptr
  llvm.store %t1, %x_1.5 : f64, !llvm.ptr
  llvm.store %t1, %x-1.5 : f64, !llvm.ptr
  llvm.store %t1, %x$1.5 : f64, !llvm.ptr
  llvm.store %t1, %x.1.5 : f64, !llvm.ptr
  llvm.store %T6.5, %ub.11 : !llvm.ptr, !llvm.ptr
  %t2 = llvm.load %ub.11 : !llvm.ptr -> !llvm.ptr
  %t3 = llvm.load %t2 : !llvm.ptr -> f64
  %t4 = llvm.load %f1.318 : !llvm.ptr -> f32
  %t5 = arith.extf %t4 : f32 to f64
  %t6 = arith.addf %t3, %t5 : f64
  %t7 = llvm.load %T817.29 : !llvm.ptr -> f64
  %t8 = arith.addf %t6, %t7 : f64
  llvm.store %t8, %retval : f64, !llvm.ptr
  %t9 = llvm.load %retval : !llvm.ptr -> f64
  func.return %t9 : f64
}
}
