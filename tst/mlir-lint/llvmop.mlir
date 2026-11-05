// Each llvm op of rule 1 breaks it in a function, but not in the
// initializer of a global before or after one, also after a declaration;
// an op whose name only starts like one of them does not
module attributes {llvm.data_layout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128", llvm.target_triple = "x86_64-pc-linux-gnu"} {
llvm.func @"abs"(i32) -> i32
llvm.mlir.global external @"g"() {alignment = 4 : i64} : i32 {
  %t1 = llvm.mlir.constant(7 : i32) : i32
  llvm.return %t1 : i32
}
func.func @"f"(%a0: i32, %a1: f64) -> i32 {
  %one = llvm.mlir.constant(1 : i64) : i64  // mlir-lint: llvm op instead of arith or cf
  %retval = llvm.alloca %one x i32 : (i64) -> !llvm.ptr
  %t1 = llvm.add %a0, %a0 : i32  // mlir-lint: llvm op instead of arith or cf
  %t2 = llvm.sub %t1, %a0 : i32  // mlir-lint: llvm op instead of arith or cf
  %t3 = llvm.mul %t2, %a0 : i32  // mlir-lint: llvm op instead of arith or cf
  %t4 = llvm.sdiv %t3, %a0 : i32  // mlir-lint: llvm op instead of arith or cf
  %t5 = llvm.udiv %t4, %a0 : i32  // mlir-lint: llvm op instead of arith or cf
  %t6 = llvm.srem %t5, %a0 : i32  // mlir-lint: llvm op instead of arith or cf
  %t7 = llvm.urem %t6, %a0 : i32  // mlir-lint: llvm op instead of arith or cf
  %t8 = llvm.and %t7, %a0 : i32  // mlir-lint: llvm op instead of arith or cf
  %t9 = llvm.or %t8, %a0 : i32  // mlir-lint: llvm op instead of arith or cf
  %t10 = llvm.xor %t9, %a0 : i32  // mlir-lint: llvm op instead of arith or cf
  %t11 = llvm.shl %t10, %a0 : i32  // mlir-lint: llvm op instead of arith or cf
  %t12 = llvm.lshr %t11, %a0 : i32  // mlir-lint: llvm op instead of arith or cf
  %t13 = llvm.ashr %t12, %a0 : i32  // mlir-lint: llvm op instead of arith or cf
  %t14 = llvm.icmp "slt" %t13, %a0 : i32  // mlir-lint: llvm op instead of arith or cf
  %t15 = llvm.fcmp "olt" %a1, %a1 : f64  // mlir-lint: llvm op instead of arith or cf
  %t16 = llvm.fadd %a1, %a1 : f64  // mlir-lint: llvm op instead of arith or cf
  %t17 = llvm.fsub %t16, %a1 : f64  // mlir-lint: llvm op instead of arith or cf
  %t18 = llvm.fmul %t17, %a1 : f64  // mlir-lint: llvm op instead of arith or cf
  %t19 = llvm.fdiv %t18, %a1 : f64  // mlir-lint: llvm op instead of arith or cf
  %t20 = llvm.frem %t19, %a1 : f64  // mlir-lint: llvm op instead of arith or cf
  %t21 = llvm.fneg %t20 : f64  // mlir-lint: llvm op instead of arith or cf
  %t22 = llvm.sext %a0 : i32 to i64  // mlir-lint: llvm op instead of arith or cf
  %t23 = llvm.zext %a0 : i32 to i64  // mlir-lint: llvm op instead of arith or cf
  %t24 = llvm.trunc %t23 : i64 to i32  // mlir-lint: llvm op instead of arith or cf
  %t25 = llvm.sitofp %a0 : i32 to f64  // mlir-lint: llvm op instead of arith or cf
  %t26 = llvm.uitofp %a0 : i32 to f64  // mlir-lint: llvm op instead of arith or cf
  %t27 = llvm.fptosi %t21 : f64 to i32  // mlir-lint: llvm op instead of arith or cf
  %t28 = llvm.fptoui %t21 : f64 to i32  // mlir-lint: llvm op instead of arith or cf
  %t29 = llvm.fptrunc %t21 : f64 to f32  // mlir-lint: llvm op instead of arith or cf
  %t30 = llvm.fpext %t29 : f32 to f64  // mlir-lint: llvm op instead of arith or cf
  %t31 = llvm.bitcast %t30 : f64 to i64  // mlir-lint: llvm op instead of arith or cf
  %t32 = llvm.select %t14, %t24, %t27 : i1, i32  // mlir-lint: llvm op instead of arith or cf
  %t33 = llvm.mlir.undef : i32  // mlir-lint: llvm op instead of arith or cf
  %t34 = llvm.mlir.poison : i32  // mlir-lint: llvm op instead of arith or cf
  %t35 = llvm.addrspacecast %retval : !llvm.ptr to !llvm.ptr<1>
  llvm.store %t32, %retval : i32, !llvm.ptr
  %t36 = llvm.load %retval : !llvm.ptr -> i32
  func.return %t36 : i32
}
llvm.func @"h"(%a0: i32, ...) -> i32 {
  %c1_i32 = arith.constant 1 : i32
  %ap.1 = llvm.alloca %c1_i32 x !llvm.array<24 x i8> {alignment = 8 : i64} : (i32) -> !llvm.ptr
  llvm.intr.vastart %ap.1 : !llvm.ptr
  %t1 = llvm.va_arg %ap.1 : (!llvm.ptr) -> i32  // mlir-lint: llvm op instead of arith or cf
  llvm.intr.vaend %ap.1 : !llvm.ptr
  %t2 = arith.cmpi slt, %t1, %a0 : i32
  llvm.cond_br %t2, ^L1, ^L2  // mlir-lint: llvm op instead of arith or cf
^L1:
  llvm.br ^L2  // mlir-lint: llvm op instead of arith or cf
^L2:
  llvm.switch %t1 : i32, ^L3 [  // mlir-lint: llvm op instead of arith or cf
    1: ^L3
  ]
^L3:
  llvm.return %t1 : i32
}
llvm.mlir.global internal @"k"() {alignment = 8 : i64} : i64 {
  %t1 = llvm.mlir.constant(8 : i64) : i64
  llvm.return %t1 : i64
}
}
