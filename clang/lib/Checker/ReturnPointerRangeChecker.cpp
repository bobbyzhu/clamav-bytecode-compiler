//== ReturnPointerRangeChecker.cpp ------------------------------*- C++ -*--==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines ReturnPointerRangeChecker, which is a path-sensitive check
// which looks for an out-of-bound pointer being returned to callers.
//
//===----------------------------------------------------------------------===//

#include "GRExprEngineInternalChecks.h"
#include "clang/Checker/PathSensitive/GRExprEngine.h"
#include "clang/Checker/BugReporter/BugReporter.h"
#include "clang/Checker/PathSensitive/CheckerVisitor.h"

using namespace clang;

namespace {
class ReturnPointerRangeChecker : 
    public CheckerVisitor<ReturnPointerRangeChecker> {      
  BuiltinBug *BT;
public:
    ReturnPointerRangeChecker() : BT(0) {}
    static void *getTag();
    void PreVisitReturnStmt(CheckerContext &C, const ReturnStmt *RS);
};
}

void clang::RegisterReturnPointerRangeChecker(GRExprEngine &Eng) {
  Eng.registerCheck(new ReturnPointerRangeChecker());
}

void *ReturnPointerRangeChecker::getTag() {
  static int x = 0; return &x;
}

void ReturnPointerRangeChecker::PreVisitReturnStmt(CheckerContext &C,
                                                   const ReturnStmt *RS) {
  const GRState *state = C.getState();

  const Expr *RetE = RS->getRetValue();
  if (!RetE)
    return;
 
  SVal V = state->getSVal(RetE);
  const MemRegion *R = V.getAsRegion();
  if (!R)
    return;

  R = R->StripCasts();
  if (!R)
    return;

  const ElementRegion *ER = dyn_cast_or_null<ElementRegion>(R);
  if (!ER)
    return;

  DefinedOrUnknownSVal &Idx = cast<DefinedOrUnknownSVal>(ER->getIndex());

  // FIXME: All of this out-of-bounds checking should eventually be refactored
  // into a common place.

  DefinedOrUnknownSVal NumElements
    = C.getStoreManager().getSizeInElements(state, ER->getSuperRegion(),
                                           ER->getValueType(C.getASTContext()));

  const GRState *StInBound = state->AssumeInBound(Idx, NumElements, true);
  const GRState *StOutBound = state->AssumeInBound(Idx, NumElements, false);
  if (StOutBound && !StInBound) {
    ExplodedNode *N = C.GenerateSink(StOutBound);

    if (!N)
      return;
  
    // FIXME: This bug correspond to CWE-466.  Eventually we should have bug
    // types explicitly reference such exploit categories (when applicable).
    if (!BT)
      BT = new BuiltinBug("Return of pointer value outside of expected range",
           "Returned pointer value points outside the original object "
           "(potential buffer overflow)");

    // FIXME: It would be nice to eventually make this diagnostic more clear,
    // e.g., by referencing the original declaration or by saying *why* this
    // reference is outside the range.

    // Generate a report for this bug.
    RangedBugReport *report = 
      new RangedBugReport(*BT, BT->getDescription(), N);

    report->addRange(RetE->getSourceRange());
    C.EmitReport(report);
  }
}
