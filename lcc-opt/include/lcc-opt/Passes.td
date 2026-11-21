//===- Passes.td - Passes of lcc-opt -----------------------*- tablegen -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LCC_OPT_PASSES_TD
#define LCC_OPT_PASSES_TD

include "mlir/Pass/PassBase.td"

def LccCheckText : Pass<"lcc-check-text"> {
  let summary = "Fail when the text form would change a value";
  let description = [{
    The text form of MLIR 23 does not read back to the same IR in two cases
    that lcc's code meets:

    - a float constant (FloatAttr, or an element of a dense float attribute)
      of more precision than a double, an f80 or f128 long double, that is
      not a double: the printer writes a decimal that is exact in the
      float's own semantics, but the parser reads a decimal literal as a
      double;
    - a negative case value of an i64 (or wider) cf.switch: the printer
      writes the cases unsigned and the parser reads an int64_t.  The
      generic form (--mlir-print-op-generic) writes them with signs.

    This pass reports each such operation, as an error, and fails, or as a
    warning when allow-lossy is set.  lcc-opt runs it last whenever it
    writes text (no --emit-bytecode), with allow-lossy from
    --lcc-allow-lossy-text; bytecode keeps every value.
  }];
  let options = [
    Option<"allowLossy", "allow-lossy", "bool", "false",
      "Warn instead of failing">,
  ];
}

def LccByvalCopy : Pass<"lcc-byval-copy"> {
  let summary = "Copy the llvm.byval arguments of func.func in the callee";
  let description = [{
    The inliner leaves the handling of an argument to the dialect of the
    callee.  The LLVM dialect copies an llvm.byval argument of an llvm.func
    into an alloca of the caller, so that the callee changes its own copy;
    the func dialect does not, and an inlined func.func would change the
    object of the caller.

    This pass, run before the inliner, gives every func.func with a body the
    copy itself: for each llvm.byval argument it adds to the entry block an
    llvm.alloca of the llvm.byval type, aligned to the larger of the ABI
    alignment of that type and the llvm.align of the argument, and an
    llvm.intr.memcpy from the argument, and lets the body use the copy.  The
    argument is already the callee's own copy, so a function that is not
    inlined only copies once more.  An argument whose only use is such a
    memcpy is left alone.
  }];
  let dependentDialects = ["mlir::LLVM::LLVMDialect"];
}

def LccHoistAllocas : Pass<"lcc-hoist-allocas"> {
  let summary = "Move constant size llvm.alloca into the entry block";
  let description = [{
    The inliner leaves the handling of the inlined blocks to the dialect of
    the call.  For llvm.call the LLVM dialect moves the static allocas of
    the callee into the entry block of the caller; for func.call the func
    dialect does not, and an alloca of a function inlined into a loop body
    takes more stack in every iteration.  mem2reg, too, promotes only the
    allocas of the entry block.

    This pass, run after the inliner, moves every llvm.alloca of a constant
    count in a block of a function body other than the entry block, in
    func.func or llvm.func, to the start of the entry block, with a new
    llvm.mlir.constant for the count.  lcc puts all its allocas in the entry
    block, so after the inliner those are the allocas of inlined functions.
    An alloca in a nested region is left alone, as it may belong to another
    allocation scope.  Unlike the LLVM dialect it adds no lifetime markers:
    the moved allocas do not share stack slots.
  }];
  let dependentDialects = ["mlir::LLVM::LLVMDialect"];
}

def LccLiftLLVMFunc : Pass<"lcc-lift-llvm-func"> {
  let summary = "Lift the control flow of llvm.func to SCF";
  let description = [{
    lift-cf-to-scf lifts only func.func, whose statically infinite loops it
    ends with a func.return; lcc gives variadic functions, functions first
    called without a prototype and, with -Wf-llvmfunc, all functions as
    llvm.func.  This pass runs the same transformation,
    ControlFlowToSCFTransformation, on the body of every llvm.func whose
    branches are all cf.br, cf.cond_br or cf.switch (lcc's), and ends a
    statically infinite loop with an llvm.return of a poison value (or of
    nothing).  A function with other branches is left alone.
  }];
  let dependentDialects = [
    "mlir::arith::ArithDialect",
    "mlir::cf::ControlFlowDialect",
    "mlir::LLVM::LLVMDialect",
    "mlir::scf::SCFDialect",
    "mlir::ub::UBDialect",
  ];
}

def LccRotateDoWhile : Pass<"lcc-rotate-do-while"> {
  let summary = "Rotate do-while shaped scf.while loops into while shape";
  let description = [{
    lcc compiles `for (e1; e2; e3) S` as `e1; goto test; L: S; e3;
    test: if (e2) goto L`, and leaves out the first jump when it can prove
    the first test true, for example `for (i = 0; i < 16; i++)`.  The loop
    is then a do-while loop, and lift-cf-to-scf makes it an scf.while whose
    `before` region holds the whole body and the test, and whose `after`
    region only passes the values on:

    ```mlir
    %r = scf.while (%iv = %init, ...) : (...) -> (...) {
      ... body ...
      %next = arith.addi %iv, %step : i32
      %c = arith.cmpi slt, %next, %ub : i32
      scf.condition(%c) %next, ... : i32, ...
    } do {
    ^bb0(%a: i32, ...):
      scf.yield %a, ... : i32, ...
    }
    ```

    This pass moves the body into the `after` region and tests the
    induction variable itself in `before`:

    ```mlir
    %r = scf.while (%iv = %init, ...) : (...) -> (...) {
      %c = arith.cmpi slt, %iv, %ub : i32
      scf.condition(%c) %iv, ... : i32, ...
    } do {
    ^bb0(%iv2: i32, ...):
      ... body with %iv2 for %iv ...
      %next = arith.addi %iv2, %step : i32
      scf.yield %next, ... : i32, ...
    }
    ```

    which scf-uplift-while-to-for (lcc-uplift-while) can turn into scf.for.
    A loop is rotated only when all of these hold, and is left alone
    otherwise:

    - the `after` region has nothing but an scf.yield of its block
      arguments;
    - the condition is `arith.cmpi slt (arith.addi %iv, %step), %ub`, where
      %iv is a `before` block argument, %step a positive integer constant
      and %ub a value, both defined outside the loop;
    - that addi is the value passed on to the position of %iv;
    - the first test `%init < %ub` folds to true on constants, so that the
      body of the do-while loop, which always runs once, runs once in the
      while loop too.

    The later tests compare the very value the do-while loop compared, so
    the loops run their bodies equally often.  A condition argument that
    the `after` region does not pass on (a value of the last iteration used
    after the loop) becomes, when it is defined outside the loop or by pure
    operations of operands outside the loop, that value, after moving the
    operations in front of the loop; otherwise it becomes one more
    loop-carried value starting from a zero (arith.constant, or
    llvm.mlir.zero for pointers and other LLVM types), which is never read
    because the body runs at least once.  A loop that would need a zero of
    another type is left alone.
  }];
  let dependentDialects = [
    "mlir::arith::ArithDialect",
    "mlir::LLVM::LLVMDialect",
    "mlir::scf::SCFDialect",
  ];
}

def LccUpliftWhile : Pass<"lcc-uplift-while"> {
  let summary = "Uplift while shaped scf.while loops to scf.for";
  let description = [{
    Applies scf::upliftWhileToForLoop, the transformation of the upstream
    populateUpliftWhileToForPatterns, greedily, with two changes:

    - The step must be a positive integer constant.  The upstream pattern
      takes any step defined outside the loop, but scf.for requires a
      strictly positive step, and a while loop with a zero or negative step
      means something else.
    - The final value of the induction variable is computed correctly.
      The upstream pattern replaces the loop result of the induction
      variable by `lb + ((ub - lb + step - 1) / step - 1) * step`, the value
      of the last iteration, while the scf.while result is the value that
      failed the test, one step further, or lb when the loop does not run.
      When that result is used, this pass replaces it by a constant when
      lb, ub and step are constants, and otherwise by one more loop-carried
      value of the scf.for that starts at lb and takes `iv + step`.
  }];
  let dependentDialects = [
    "mlir::arith::ArithDialect",
    "mlir::scf::SCFDialect",
  ];
}

#endif // LCC_OPT_PASSES_TD
