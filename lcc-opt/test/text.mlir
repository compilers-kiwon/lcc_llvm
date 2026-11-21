// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// RUN: not lcc-opt %s 2>&1 | FileCheck %s --check-prefix=ERR
// RUN: lcc-opt --lcc-allow-lossy-text %s -o %t.mlir 2>&1 \
// RUN:   | FileCheck %s --check-prefix=WARN
// RUN: not lcc-opt --mlir-print-op-generic %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=GENERIC
// RUN: lcc-opt --emit-bytecode %s -o %t.mlirbc
// RUN: lcc-opt --canonicalize --mlir-print-op-generic --lcc-allow-lossy-text \
// RUN:   %t.mlirbc 2>/dev/null | FileCheck %s --check-prefix=EXACT
// RUN: lcc-opt --lcc-allow-lossy-text --mlir-print-op-generic %s 2>/dev/null \
// RUN:   | lcc-opt --canonicalize --mlir-print-op-generic \
// RUN:     --lcc-allow-lossy-text 2>/dev/null \
// RUN:   | FileCheck %s --check-prefix=LOSS
// RUN: not lcc-opt %t.mlir 2>&1 | FileCheck %s --check-prefix=REPARSE
//
// When lcc-opt writes text, lcc-check-text fails on what the text form of
// MLIR 23 would change: an f80 or f128 constant that is not a double (0.1L
// here; 1.5L is a double, and f32 and f64 read back exactly) and a negative
// case of an i64 cf.switch (the i32 one reads back).  The bitcasts, folded
// by canonicalize into integers, which print exactly, show the bits:
// bytecode keeps them, the text form changes those of 0.1L.

// ERR: :[[#@LINE+24]]:8: error: reading the text form back would change a float constant of more precision than a double
// ERR: :[[#@LINE+24]]:8: error: reading the text form back would change a float constant of more precision than a double
// ERR: :[[#@LINE+36]]:3: error: reading the text form back would change a negative case value of a 64-bit cf.switch
// ERR: error: 3 operation(s) would change in the text output; use --emit-bytecode, or --lcc-allow-lossy-text to write the text anyway
// ERR-NOT: error:

// WARN-COUNT-3: warning: reading the text form back would change
// WARN: warning: 3 operation(s) would change in the text output; use --emit-bytecode{{$}}

// The generic form writes the cases with signs.
// GENERIC-NOT: cf.switch
// GENERIC: error: 2 operation(s) would change

// EXACT: value = 302153978578547713559757 : i80
// EXACT: value = 85047745624057062624278117674093484442 : i128
// EXACT: value = 302226843217638866288640 : i80

// LOSS: value = 302153978578547713560576 : i80
// LOSS: value = 85047745624057062624739286275936223232 : i128
// LOSS: value = 302226843217638866288640 : i80

// REPARSE: error: custom op 'cf.switch' integer value too large

func.func @floats() -> (i80, i128, i80, f32, f64) {
  %a = arith.constant 0x3FFBCCCCCCCCCCCCCCCD : f80
  %b = arith.constant 0x3FFB999999999999999999999999999A : f128
  %c = arith.constant 0x3FFFC000000000000000 : f80
  %d = arith.constant 0x3DCCCCCD : f32
  %e = arith.constant 0x3FB999999999999A : f64
  %ai = arith.bitcast %a : f80 to i80
  %bi = arith.bitcast %b : f128 to i128
  %ci = arith.bitcast %c : f80 to i80
  return %ai, %bi, %ci, %d, %e : i80, i128, i80, f32, f64
}

func.func @switches(%x: i64, %y: i32) -> i32 {
  %c1 = arith.constant 1 : i32
  %c2 = arith.constant 2 : i32
  cf.switch %x : i64, [
    default: ^bb1,
    -9223372036854775808: ^bb2
  ]
^bb1:
  cf.switch %y : i32, [
    default: ^bb3,
    -1: ^bb2
  ]
^bb2:
  return %c1 : i32
^bb3:
  return %c2 : i32
}
