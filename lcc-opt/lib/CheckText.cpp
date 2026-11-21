//===- CheckText.cpp - Values the text form of MLIR 23 changes ------------===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// lcc-check-text: see Passes.td.  The checks follow the printer and the
// parser of MLIR 23:
//
// - printFloatValue (AsmPrinter.cpp) prints a finite float in decimal when
//   that decimal reads back to the same value in the float's own semantics,
//   or else when APFloat::toString has a '.', and in hexadecimal otherwise.
//   Parser::parseFloatFromLiteral reads a decimal as a double and converts
//   that, which changes an f80 or f128 that is not a double.
// - printSwitchOpCases (ControlFlowOps.cpp) prints a case value with
//   getLimitedValue, unsigned, and parseSwitchOpCases reads an int64_t, which
//   fails for a negative case of an i64 cf.switch.  The generic form prints
//   the case values as a dense attribute, with signs.
//
//===----------------------------------------------------------------------===//

#include "lcc-opt/Passes.h"

#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/OperationSupport.h"
#include "llvm/ADT/SmallString.h"

namespace lcc {
#define GEN_PASS_DEF_LCCCHECKTEXT
#include "lcc-opt/Passes.h.inc"
} // namespace lcc

using namespace mlir;

/// Returns true when the text form of `value` reads back as another value.
static bool floatChanges(const APFloat &value) {
  if (value.isNaN() || value.isInfinity())
    return false; // printed in hexadecimal
  const llvm::fltSemantics &semantics = value.getSemantics();
  SmallString<128> text;
  value.toString(text, /*FormatPrecision=*/6, /*FormatMaxPadding=*/0,
                 /*TruncateZero=*/false);
  if (!APFloat(semantics, text).bitwiseIsEqual(value)) {
    text.clear();
    value.toString(text);
    if (!text.str().contains('.'))
      return false; // printed in hexadecimal
  }
  // What the parser makes of the decimal.
  APFloat read(APFloat::IEEEdouble(), text);
  bool losesInfo;
  read.convert(semantics, APFloat::rmNearestTiesToEven, &losesInfo);
  return !read.bitwiseIsEqual(value);
}

/// Returns true when `attr`, or an attribute in it, holds a float whose
/// text form reads back as another value.
static bool attrChanges(Attribute attr) {
  bool changes = false;
  attr.walk([&](Attribute a) {
    if (auto f = dyn_cast<FloatAttr>(a))
      changes |= floatChanges(f.getValue());
    else if (auto dense = dyn_cast<DenseFPElementsAttr>(a))
      for (APFloat f : dense.getValues<APFloat>())
        if (floatChanges(f)) {
          changes = true;
          break;
        }
  });
  return changes;
}

namespace {

struct CheckTextPass : public lcc::impl::LccCheckTextBase<CheckTextPass> {
  using Base::Base;

  void runOnOperation() override {
    bool generic = OpPrintingFlags().shouldPrintGenericOpForm();
    unsigned count = 0;
    Operation *first = nullptr;
    getOperation()->walk([&](Operation *op) {
      const char *what = nullptr;
      if (attrChanges(op->getAttrDictionary()))
        what = "a float constant of more precision than a double (MLIR 23 "
               "reads a decimal float literal as a double)";
      else if (auto sw = dyn_cast<cf::SwitchOp>(op); sw && !generic)
        if (auto cases = sw.getCaseValues())
          for (const APInt &v : cases->getValues<APInt>())
            if (v.getBitWidth() >= 64 && v.getActiveBits() > 63)
              what = "a negative case value of a 64-bit cf.switch (MLIR 23 "
                     "writes the cases unsigned and reads an int64_t)";
      if (!what)
        return;
      if (++count == 1)
        first = op;
      // Report the first few; the summary below counts them all.
      if (count > 5)
        return;
      InFlightDiagnostic diag = allowLossy ? op->emitWarning() : op->emitError();
      diag << "reading the text form back would change " << what;
    });
    if (!count)
      return;
    InFlightDiagnostic diag =
        allowLossy ? first->emitWarning() : first->emitError();
    diag << count
         << " operation(s) would change in the text output; use "
            "--emit-bytecode"
         << (allowLossy ? "" : ", or --lcc-allow-lossy-text to write the "
                               "text anyway");
    if (!allowLossy)
      signalPassFailure();
  }
};

} // namespace
