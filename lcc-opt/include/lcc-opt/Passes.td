//===- Passes.td - Passes of lcc-opt -----------------------*- tablegen -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LCC_OPT_PASSES_TD
#define LCC_OPT_PASSES_TD

include "mlir/Pass/PassBase.td"

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
    loop-carried value whose initial value is ub.poison, which is never
    observed because the body runs at least once.
  }];
  let dependentDialects = [
    "mlir::arith::ArithDialect",
    "mlir::scf::SCFDialect",
    "mlir::ub::UBDialect",
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
