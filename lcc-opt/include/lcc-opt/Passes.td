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

def LccMemcpyIntrinsic : Pass<"lcc-memcpy-intrinsic"> {
  let summary = "Turn lcc's memcpy calls into llvm.intr.memcpy";
  let description = [{
    lcc copies structures with llvm.call_intrinsic "llvm.memcpy.p0.p0.i64"
    (or .i32), as the llvm.intr.memcpy of MLIR 21 took no alignments.
    mem2reg and sroa take the pointers of such a call for escaping, but
    know llvm.intr.memcpy, which in MLIR 23 keeps the alignments in its
    arg_attrs.  This pass replaces each such call whose volatile operand is
    a constant by an llvm.intr.memcpy with the same operands, volatility
    and argument attributes.
  }];
  let dependentDialects = ["mlir::LLVM::LLVMDialect"];
}

def LccSplitAggregates : Pass<"lcc-split-aggregates"> {
  let summary = "Split lcc's byte array allocas into scalar allocas";
  let description = [{
    lcc gives a local structure or array an
    `llvm.alloca 1 x !llvm.array<N x i8>` and reaches its members through
    `llvm.getelementptr ... i8` with scalar loads and stores, which cross
    the byte elements that sroa splits along.  This pass splits such an
    alloca into one scalar alloca per member that mem2reg can promote, when
    every use, through getelementptrs of i8 with a constant index, is a
    non-volatile load or store of an integer, float or pointer, or a
    non-volatile memcpy of a constant length to or from another pointer;
    when the loads and stores overlap only at the same offset with the same
    type; when no memcpy covers part of a member; and when there is at
    least one load or store.  A memcpy becomes a load and a store per slot
    of its range, and the bytes of the range no member covers become
    integer filler slots of at most 8 bytes, so that every byte is still
    copied.  At most 64 slots.
  }];
  let dependentDialects = ["mlir::LLVM::LLVMDialect"];
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

    which lcc-uplift-while can turn into scf.for.  A loop is rotated only
    when all of these hold, and is left alone otherwise:

    - the `after` region has nothing but an scf.yield of its block
      arguments;
    - the condition is an arith.cmpi of any predicate between a value
      passed on to a `before` block argument %iv and a bound %ub defined
      outside the loop, either way round;
    - the first test, the predicate on the initial value of %iv and %ub,
      folds to true on constants, so that the body of the do-while loop,
      which always runs once, runs once in the while loop too.

    The later tests compare the very values the do-while loop compared, so
    the loops run their bodies equally often, whatever the predicate and
    the update of %iv are.  A condition argument that the `after` region
    does not pass on (a value of the last iteration used after the loop)
    becomes, when it is defined outside the loop or by pure operations of
    operands outside the loop, that value, after moving the operations in
    front of the loop; otherwise it becomes one more loop-carried value
    starting from a zero (arith.constant, or llvm.mlir.zero for pointers
    and other LLVM types), which is never read because the body runs at
    least once.  A loop that would need a zero of another type is left
    alone.
  }];
  let dependentDialects = [
    "mlir::arith::ArithDialect",
    "mlir::LLVM::LLVMDialect",
    "mlir::scf::SCFDialect",
  ];
}

def LccUpliftWhile : Pass<"lcc-uplift-while"> {
  let summary = "Uplift counting while shaped scf.while loops to scf.for";
  let description = [{
    A while shaped scf.while, whose `before` region is one arith.cmpi
    between a `before` block argument %iv and a bound defined outside the
    loop and an scf.condition passing on every `before` block argument,
    and whose `after` region yields `%iv + s` or `%iv - s` for %iv, with s
    a constant, counts %iv.  This pass makes such a loop an scf.for when it
    can show, on constants, that %iv does not wrap: scf.for leaves a loop
    undefined whose final induction variable does not fit, while the
    scf.while just wraps.  It does not rely on C's signed overflow being
    undefined.

    - Counting up, the loops `%iv < %ub` (slt, ult) with s = 1 or, with
      s > 1, a constant %ub with %ub + s - 1 that fits; `%iv <= %ub` (sle,
      ule) with a constant %ub, as `%iv < %ub + 1`, under the same
      condition; and `%iv != %ub` (ne) with s = 1 and constant %init <= %ub,
      signed or unsigned, or %ub the largest value of its type.  They become `scf.for [unsigned] %iv = %init to
      %end step s`.
    - Counting down, the loops `%iv > %lb` (sgt, ugt) with s = 1 or, with
      s > 1, a constant %lb with %lb - s + 1 that does not wrap; `%iv >= %lb`
      (sge, uge) with a constant %lb, as `%iv > %lb - 1`, likewise; and
      `%iv != %lb` with s = 1 and constant %init >= %lb, or %lb the
      smallest value (`%u != 0`).  They become
      `scf.for unsigned %k = 0 to %n step 1` with %iv = %init - %k * s and
      the trip count %n = (%init - %end - 1) / s + 1, or 0.

    The final %iv, a result of the scf.while, becomes %init + %n * s
    (counting up, with %n = (%end - %init - 1) / s + 1 or 0) or %init - %n *
    s, computed in unsigned arithmetic of the width of %iv, exact because
    the result fits.  The upstream populateUpliftWhileToForPatterns does
    only `slt`/`sgt` counting up, takes any step, also one that may be zero
    or negative at run time, and gives the final %iv one step short.
  }];
  let dependentDialects = [
    "mlir::arith::ArithDialect",
    "mlir::scf::SCFDialect",
  ];
}

def LccIndexLoops : Pass<"lcc-index-loops"> {
  let summary = "Turn integer scf.for loops into index scf.for loops";
  let description = [{
    lcc's loops come out of lcc-to-scf as scf.for of the type of the C loop
    variable, mostly i32; affine.for, and the upstream loop transformations
    in general, take index loops.  This pass makes an index loop of every
    scf.for of an integer type iN whose values index can hold, with the
    bounds and the step cast to index, and gives the body the induction
    variable cast back to iN, which is exact since every value of the
    induction variable fits in iN.  It does so for a `scf.for unsigned` too,
    which becomes a signed index loop with the bounds zero-extended, when
    the bounds are non-negative as signed numbers of the width of index:
    always when N is less than that width, and otherwise when the ranges of
    the bounds show it, together with `ub + step - 1` not wrapping
    (LinearAnalysis); other loops are left alone.

    Where the body extends the induction variable, `arith.extsi` (signed
    loops) or `arith.extui` (unsigned loops) to iM, or casts it to index
    with `arith.index_cast` or `arith.index_castui`, it uses the index
    induction variable, cast to iM, instead, which is the same value.
  }];
  let dependentDialects = [
    "mlir::arith::ArithDialect",
    "mlir::scf::SCFDialect",
  ];
}

def LccGlobalsToMemref : Pass<"lcc-globals-to-memref", "mlir::ModuleOp"> {
  let summary = "Turn lcc's global arrays into memref.global";
  let description = [{
    lcc gives every global variable an `llvm.mlir.global` of
    `!llvm.array<N x i8>` (or a packed structure of byte arrays when the
    initializer has zero runs) and reaches its elements through
    `llvm.mlir.addressof`, `llvm.getelementptr` of i8 at byte offsets and
    `llvm.load`/`llvm.store`.  This pass replaces such a global with a 1-D
    `memref.global` of `memref<(N / sizeof(T)) x T>`, where T is the type of
    every load and store of it, and those loads and stores with
    `memref.load` and `memref.store` at element indices.

    A global is changed only when all of these hold, and left alone
    otherwise:

    - it is an array of bytes as above, not a scalar variable (which lcc
      gives its own type) or an array of pointers, and N is not 0;
    - it is defined or declared with external or internal linkage (lcc's
      string literals and the initializers of local arrays are private),
      in address space 0, without a section, comdat, thread_local or other
      visibility;
    - its name is used only by `llvm.mlir.addressof` in function bodies,
      not in the initializer of a global (nor in its own);
    - it has loads or stores, all of the same type T, an integer of 8, 16,
      32 or 64 bits, f32 or f64, and N is a multiple of sizeof(T);
    - the initializer is `llvm.mlir.zero` (dense<0>), or bytes and zeros
      with no address (a string, or lcc's packed structures, nested for
      long initializers), unpacked little-endian into T, for f32 and f64
      the bits as they are (-0.0, NaN payloads and signs, infinities,
      subnormals); or there is none (a declaration).  A big-endian data
      layout leaves the global alone unless it is zero or a declaration.

    External linkage becomes public, internal private (the memref lowering
    gives private linkage); constant and alignment stay.  A load or store
    becomes a memref access when it is not volatile, not atomic and has no
    other attribute than an alignment of at least sizeof(T), and its
    address is the global's plus a sum of byte offsets, which
    LinearAnalysis shows to be `c0 + sum of c_k * x_k` exactly in the
    pointer's index width with c0 and every c_k a multiple of sizeof(T);
    the index is `c0 / sizeof(T) + sum of (c_k / sizeof(T)) * x_k` in index
    arithmetic.  Every other use of the global's address (a volatile access,
    a call argument, a stored address, an offset that may wrap) takes a
    pointer made from the memref: `memref.extract_aligned_pointer_as_index`,
    `arith.index_cast` and `llvm.inttoptr`.

    With `remarks`, it reports each global, changed or not, and why, as a
    remark.
  }];
  let options = [
    Option<"remarks", "remarks", "bool", "false",
      "Report each global as a remark">,
  ];
  let dependentDialects = [
    "mlir::arith::ArithDialect",
    "mlir::LLVM::LLVMDialect",
    "mlir::memref::MemRefDialect",
  ];
}

def LccScfToAffine : Pass<"lcc-scf-to-affine"> {
  let summary = "Raise index scf.for loop nests on memrefs to affine.for";
  let description = [{
    This pass makes affine.for of the scf.for loops of a nest, and
    affine.load and affine.store of its memref accesses, when the whole nest
    is such that affine analysis, which takes accesses to different memrefs
    for disjoint and sees no other memory operation, is right about it:

    - every loop is a signed index scf.for with a positive constant step
      and bounds that are affine in the induction variables of the
      enclosing loops of the nest and in symbols: constants and values
      defined outside the nest that are valid affine symbols, or integers
      of at most the width of index defined at the top level of the
      function, which the pass casts to index after their definition;
    - the nest holds only side effect free arith operations, constants,
      ub.poison, scf.if, inner loops of the nest and memref.load and
      memref.store of a 1-D memref of a memref.get_global, at an index
      affine like the bounds (LinearAnalysis), whose range over the ranges
      of the induction variables, from the bounds, lies in the memref;
    - it is in a func.func (an affine scope), not an llvm.func.

    So no call, llvm.load, llvm.store, llvm.alloca or other operation with
    side effects.  The pass tries every scf.for from the outside in and
    raises the first loop whose nest qualifies, with all its inner loops;
    inner nests of a loop that does not qualify are tried on their own, but
    can only qualify when they do not use the induction variables of the
    outer loops.

    With `remarks`, it reports each loop nest it raises and each loop it
    tries and leaves, with the reason, as a remark, saying which are in
    another loop ("inner").
  }];
  let options = [
    Option<"remarks", "remarks", "bool", "false",
      "Report each loop nest as a remark">,
  ];
  let dependentDialects = [
    "mlir::affine::AffineDialect",
    "mlir::arith::ArithDialect",
    "mlir::memref::MemRefDialect",
  ];
}

#endif // LCC_OPT_PASSES_TD
