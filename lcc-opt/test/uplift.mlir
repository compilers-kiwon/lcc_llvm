// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// RUN: lcc-opt --lcc-uplift-while %s | FileCheck %s
// RUN: lcc-opt --lcc-uplift-while --canonicalize %s \
// RUN:   | FileCheck %s --check-prefix=FOLD
//
// lcc-uplift-while makes a counting while loop an scf.for when it can show
// on constants that the induction variable does not wrap, and gives the
// final induction variable, the result of the scf.while, its exact value
// (FOLD shows the constant ones).  Counting down, the scf.for counts the
// trips.  The loops in the second half may wrap or never end and stay.
// Unlike the upstream pattern, slt_step3 gives 12, not 9, and slt_none 5,
// not -1.

func.func private @sink(i32)

// iv < ub, step 1, bounds not constant; the final iv is ub, or lb if lb >= ub
// CHECK-LABEL: func.func @slt_var(
//  CHECK-SAME:     %[[LB:[^:]*]]: i32, %[[UB:[^:]*]]: i32)
//   CHECK-NOT:   scf.while
//       CHECK:   scf.for %{{.*}} = %[[LB]] to %[[UB]] step %c1_i32 : i32 {
//       CHECK:   arith.select
//       CHECK:   return
func.func @slt_var(%lb: i32, %ub: i32) -> i32 {
  %c_step = arith.constant 1 : i32
  %r = scf.while (%i = %lb) : (i32) -> i32 {
    %c = arith.cmpi slt, %i, %ub : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    func.call @sink(%a) : (i32) -> ()
    %n = arith.addi %a, %c_step : i32
    scf.yield %n : i32
  }
  return %r : i32
}

// iv < 10 from 0, step 3: 0, 3, 6, 9, final 12
// CHECK-LABEL: func.func @slt_step3(
//  CHECK-SAME:     %[[LB:[^:]*]]: i32, %[[UB:[^:]*]]: i32)
//   CHECK-NOT:   scf.while
//       CHECK:   scf.for %{{.*}} = %c0_i32 to %c10_i32 step %c3_i32 : i32 {
// FOLD-LABEL: func.func @slt_step3(
//       FOLD:   return %c12_i32 : i32
func.func @slt_step3(%lb: i32, %ub: i32) -> i32 {
  %c_step = arith.constant 3 : i32
  %c_init = arith.constant 0 : i32
  %c_bound = arith.constant 10 : i32
  %r = scf.while (%i = %c_init) : (i32) -> i32 {
    %c = arith.cmpi slt, %i, %c_bound : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    func.call @sink(%a) : (i32) -> ()
    %n = arith.addi %a, %c_step : i32
    scf.yield %n : i32
  }
  return %r : i32
}

// no iteration: 5 < 0 is false, final 5
// CHECK-LABEL: func.func @slt_none(
//  CHECK-SAME:     %[[LB:[^:]*]]: i32, %[[UB:[^:]*]]: i32)
//   CHECK-NOT:   scf.while
//       CHECK:   scf.for %{{.*}} = %c5_i32 to %c0_i32 step %c1_i32 : i32 {
// FOLD-LABEL: func.func @slt_none(
//       FOLD:   return %c5_i32 : i32
func.func @slt_none(%lb: i32, %ub: i32) -> i32 {
  %c_step = arith.constant 1 : i32
  %c_init = arith.constant 5 : i32
  %c_bound = arith.constant 0 : i32
  %r = scf.while (%i = %c_init) : (i32) -> i32 {
    %c = arith.cmpi slt, %i, %c_bound : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    func.call @sink(%a) : (i32) -> ()
    %n = arith.addi %a, %c_step : i32
    scf.yield %n : i32
  }
  return %r : i32
}

// iv < INT_MAX - 2, step 3: the final iv, at most INT_MAX, fits
// CHECK-LABEL: func.func @slt_step3_max(
//  CHECK-SAME:     %[[LB:[^:]*]]: i32, %[[UB:[^:]*]]: i32)
//   CHECK-NOT:   scf.while
//       CHECK:   scf.for %{{.*}} = %[[LB]] to %c2147483645_i32 step %c3_i32 : i32 {
func.func @slt_step3_max(%lb: i32, %ub: i32) -> i32 {
  %c_step = arith.constant 3 : i32
  %c_bound = arith.constant 2147483645 : i32
  %r = scf.while (%i = %lb) : (i32) -> i32 {
    %c = arith.cmpi slt, %i, %c_bound : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    func.call @sink(%a) : (i32) -> ()
    %n = arith.addi %a, %c_step : i32
    scf.yield %n : i32
  }
  return %r : i32
}

// iv <= 9 as iv < 10, final 10
// CHECK-LABEL: func.func @sle_const(
//  CHECK-SAME:     %[[LB:[^:]*]]: i32, %[[UB:[^:]*]]: i32)
//   CHECK-NOT:   scf.while
//       CHECK:   scf.for %{{.*}} = %c0_i32 to %c10_i32 step %c1_i32 : i32 {
// FOLD-LABEL: func.func @sle_const(
//       FOLD:   return %c10_i32 : i32
func.func @sle_const(%lb: i32, %ub: i32) -> i32 {
  %c_step = arith.constant 1 : i32
  %c_init = arith.constant 0 : i32
  %c_bound = arith.constant 9 : i32
  %r = scf.while (%i = %c_init) : (i32) -> i32 {
    %c = arith.cmpi sle, %i, %c_bound : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    func.call @sink(%a) : (i32) -> ()
    %n = arith.addi %a, %c_step : i32
    scf.yield %n : i32
  }
  return %r : i32
}

// iv <= INT_MAX - 1 as iv < INT_MAX
// CHECK-LABEL: func.func @sle_max1(
//  CHECK-SAME:     %[[LB:[^:]*]]: i32, %[[UB:[^:]*]]: i32)
//   CHECK-NOT:   scf.while
//       CHECK:   scf.for %{{.*}} = %[[LB]] to %c2147483647_i32 step %c1_i32 : i32 {
func.func @sle_max1(%lb: i32, %ub: i32) -> i32 {
  %c_step = arith.constant 1 : i32
  %c_bound = arith.constant 2147483646 : i32
  %r = scf.while (%i = %lb) : (i32) -> i32 {
    %c = arith.cmpi sle, %i, %c_bound : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    func.call @sink(%a) : (i32) -> ()
    %n = arith.addi %a, %c_step : i32
    scf.yield %n : i32
  }
  return %r : i32
}

// unsigned iv < ub
// CHECK-LABEL: func.func @ult_var(
//  CHECK-SAME:     %[[LB:[^:]*]]: i32, %[[UB:[^:]*]]: i32)
//   CHECK-NOT:   scf.while
//       CHECK:   scf.for unsigned %{{.*}} = %[[LB]] to %[[UB]] step %c1_i32 : i32 {
func.func @ult_var(%lb: i32, %ub: i32) -> i32 {
  %c_step = arith.constant 1 : i32
  %r = scf.while (%i = %lb) : (i32) -> i32 {
    %c = arith.cmpi ult, %i, %ub : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    func.call @sink(%a) : (i32) -> ()
    %n = arith.addi %a, %c_step : i32
    scf.yield %n : i32
  }
  return %r : i32
}

// unsigned iv <= 9, step 2: 0, 2, 4, 6, 8, final 10
// CHECK-LABEL: func.func @ule_step2(
//  CHECK-SAME:     %[[LB:[^:]*]]: i32, %[[UB:[^:]*]]: i32)
//   CHECK-NOT:   scf.while
//       CHECK:   scf.for unsigned %{{.*}} = %c0_i32 to %c10_i32 step %c2_i32 : i32 {
// FOLD-LABEL: func.func @ule_step2(
//       FOLD:   return %c10_i32 : i32
func.func @ule_step2(%lb: i32, %ub: i32) -> i32 {
  %c_step = arith.constant 2 : i32
  %c_init = arith.constant 0 : i32
  %c_bound = arith.constant 9 : i32
  %r = scf.while (%i = %c_init) : (i32) -> i32 {
    %c = arith.cmpi ule, %i, %c_bound : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    func.call @sink(%a) : (i32) -> ()
    %n = arith.addi %a, %c_step : i32
    scf.yield %n : i32
  }
  return %r : i32
}

// iv != 7 from 3 as iv < 7, final 7
// CHECK-LABEL: func.func @ne_up(
//  CHECK-SAME:     %[[LB:[^:]*]]: i32, %[[UB:[^:]*]]: i32)
//   CHECK-NOT:   scf.while
//       CHECK:   scf.for %{{.*}} = %c3_i32 to %c7_i32 step %c1_i32 : i32 {
// FOLD-LABEL: func.func @ne_up(
//       FOLD:   return %c7_i32 : i32
func.func @ne_up(%lb: i32, %ub: i32) -> i32 {
  %c_step = arith.constant 1 : i32
  %c_init = arith.constant 3 : i32
  %c_bound = arith.constant 7 : i32
  %r = scf.while (%i = %c_init) : (i32) -> i32 {
    %c = arith.cmpi ne, %i, %c_bound : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    func.call @sink(%a) : (i32) -> ()
    %n = arith.addi %a, %c_step : i32
    scf.yield %n : i32
  }
  return %r : i32
}

// iv != INT_MIN + 2 from INT_MAX - 2: unsigned, through INT_MAX
// CHECK-LABEL: func.func @ne_up_unsigned(
//  CHECK-SAME:     %[[LB:[^:]*]]: i32, %[[UB:[^:]*]]: i32)
//   CHECK-NOT:   scf.while
//       CHECK:   scf.for unsigned %{{.*}} = %c2147483645_i32 to %c-2147483646_i32 step %c1_i32 : i32 {
// FOLD-LABEL: func.func @ne_up_unsigned(
//       FOLD:   return %c-2147483646_i32 : i32
func.func @ne_up_unsigned(%lb: i32, %ub: i32) -> i32 {
  %c_step = arith.constant 1 : i32
  %c_init = arith.constant 2147483645 : i32
  %c_bound = arith.constant -2147483646 : i32
  %r = scf.while (%i = %c_init) : (i32) -> i32 {
    %c = arith.cmpi ne, %i, %c_bound : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    func.call @sink(%a) : (i32) -> ()
    %n = arith.addi %a, %c_step : i32
    scf.yield %n : i32
  }
  return %r : i32
}

// iv > lb counting down by 1: k counts the trips, iv = init - k
// CHECK-LABEL: func.func @sgt_var(
//  CHECK-SAME:     %[[LB:[^:]*]]: i32, %[[UB:[^:]*]]: i32)
//   CHECK-NOT:   scf.while
//       CHECK:   scf.for unsigned %[[K:.*]] = %c0_i32 to %{{.*}} step %c1_i32{{(_[0-9]+)?}} : i32 {
//  CHECK-NEXT:     arith.subi %{{.*}}, %[[K]] : i32
func.func @sgt_var(%lb: i32, %ub: i32) -> i32 {
  %c_step = arith.constant 1 : i32
  %r = scf.while (%i = %lb) : (i32) -> i32 {
    %c = arith.cmpi sgt, %i, %ub : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    func.call @sink(%a) : (i32) -> ()
    %n = arith.subi %a, %c_step : i32
    scf.yield %n : i32
  }
  return %r : i32
}

// iv >= -5 counting down by 3, as iv > -6: iv = init - 3k
// CHECK-LABEL: func.func @sge_step3(
//  CHECK-SAME:     %[[LB:[^:]*]]: i32, %[[UB:[^:]*]]: i32)
//   CHECK-NOT:   scf.while
//       CHECK:   scf.for unsigned %[[K:.*]] = %c0_i32 to %{{.*}} step %c1_i32{{(_[0-9]+)?}} : i32 {
//  CHECK-NEXT:     %[[M:.*]] = arith.muli %[[K]], %{{.*}} : i32
//  CHECK-NEXT:     arith.subi %{{.*}}, %[[M]] : i32
func.func @sge_step3(%lb: i32, %ub: i32) -> i32 {
  %c_step = arith.constant 3 : i32
  %c_bound = arith.constant -5 : i32
  %r = scf.while (%i = %lb) : (i32) -> i32 {
    %c = arith.cmpi sge, %i, %c_bound : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    func.call @sink(%a) : (i32) -> ()
    %n = arith.subi %a, %c_step : i32
    scf.yield %n : i32
  }
  return %r : i32
}

// iv != 5 from 10 counting down, final 5
// CHECK-LABEL: func.func @ne_down(
//  CHECK-SAME:     %[[LB:[^:]*]]: i32, %[[UB:[^:]*]]: i32)
//   CHECK-NOT:   scf.while
//       CHECK:   scf.for unsigned %[[K:.*]] = %c0_i32 to %{{.*}} step %c1_i32{{(_[0-9]+)?}} : i32 {
//  CHECK-NEXT:     arith.subi %{{.*}}, %[[K]] : i32
// FOLD-LABEL: func.func @ne_down(
//       FOLD:   return %c5_i32 : i32
func.func @ne_down(%lb: i32, %ub: i32) -> i32 {
  %c_step = arith.constant 1 : i32
  %c_init = arith.constant 10 : i32
  %c_bound = arith.constant 5 : i32
  %r = scf.while (%i = %c_init) : (i32) -> i32 {
    %c = arith.cmpi ne, %i, %c_bound : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    func.call @sink(%a) : (i32) -> ()
    %n = arith.subi %a, %c_step : i32
    scf.yield %n : i32
  }
  return %r : i32
}

// unsigned iv > lb counting down
// CHECK-LABEL: func.func @ugt_var(
//  CHECK-SAME:     %[[LB:[^:]*]]: i32, %[[UB:[^:]*]]: i32)
//   CHECK-NOT:   scf.while
//       CHECK:   scf.for unsigned %[[K:.*]] = %c0_i32 to %{{.*}} step %c1_i32{{(_[0-9]+)?}} : i32 {
//  CHECK-NEXT:     arith.subi %{{.*}}, %[[K]] : i32
func.func @ugt_var(%lb: i32, %ub: i32) -> i32 {
  %c_step = arith.constant 1 : i32
  %r = scf.while (%i = %lb) : (i32) -> i32 {
    %c = arith.cmpi ugt, %i, %ub : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    func.call @sink(%a) : (i32) -> ()
    %n = arith.subi %a, %c_step : i32
    scf.yield %n : i32
  }
  return %r : i32
}

// unsigned iv >= 2 counting down by 2, as iv > 1: 1 - 2 + 1 does not wrap
// CHECK-LABEL: func.func @uge_step2(
//  CHECK-SAME:     %[[LB:[^:]*]]: i32, %[[UB:[^:]*]]: i32)
//   CHECK-NOT:   scf.while
//       CHECK:   scf.for unsigned %[[K:.*]] = %c0_i32 to %{{.*}} step %c1_i32{{(_[0-9]+)?}} : i32 {
//  CHECK-NEXT:     %[[M:.*]] = arith.muli %[[K]], %{{.*}} : i32
//  CHECK-NEXT:     arith.subi %{{.*}}, %[[M]] : i32
func.func @uge_step2(%lb: i32, %ub: i32) -> i32 {
  %c_step = arith.constant 2 : i32
  %c_bound = arith.constant 2 : i32
  %r = scf.while (%i = %lb) : (i32) -> i32 {
    %c = arith.cmpi uge, %i, %c_bound : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    func.call @sink(%a) : (i32) -> ()
    %n = arith.subi %a, %c_step : i32
    scf.yield %n : i32
  }
  return %r : i32
}

// unsigned iv != 0 counting down from any iv: as iv > 0
// CHECK-LABEL: func.func @ne_down_zero(
//   CHECK-NOT:   scf.while
//       CHECK:   scf.for unsigned %[[K:.*]] = %c0_i32{{(_[0-9]+)?}} to %{{.*}} step %c1_i32{{(_[0-9]+)?}} : i32 {
//  CHECK-NEXT:     arith.subi %{{.*}}, %[[K]] : i32
// FOLD-LABEL: func.func @ne_down_zero(
//       FOLD:   return %c0_i32 : i32
func.func @ne_down_zero(%lb: i32, %ub: i32) -> i32 {
  %c_step = arith.constant 1 : i32
  %c_bound = arith.constant 0 : i32
  %r = scf.while (%i = %lb) : (i32) -> i32 {
    %c = arith.cmpi ne, %i, %c_bound : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    func.call @sink(%a) : (i32) -> ()
    %n = arith.subi %a, %c_step : i32
    scf.yield %n : i32
  }
  return %r : i32
}

// iv != INT_MAX counting up from any iv: as iv < INT_MAX
// CHECK-LABEL: func.func @ne_up_max(
//   CHECK-NOT:   scf.while
//       CHECK:   scf.for %{{.*}} = %{{.*}} to %c2147483647_i32 step %c1_i32 : i32 {
func.func @ne_up_max(%lb: i32, %ub: i32) -> i32 {
  %c_step = arith.constant 1 : i32
  %c_bound = arith.constant 2147483647 : i32
  %r = scf.while (%i = %lb) : (i32) -> i32 {
    %c = arith.cmpi ne, %i, %c_bound : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    func.call @sink(%a) : (i32) -> ()
    %n = arith.addi %a, %c_step : i32
    scf.yield %n : i32
  }
  return %r : i32
}

// Stays: iv <= ub, ub not constant: ub + 1 may wrap
// CHECK-LABEL: func.func @sle_var(
//   CHECK-NOT:   scf.for
//       CHECK:   scf.while
func.func @sle_var(%lb: i32, %ub: i32) -> i32 {
  %c_step = arith.constant 1 : i32
  %r = scf.while (%i = %lb) : (i32) -> i32 {
    %c = arith.cmpi sle, %i, %ub : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    func.call @sink(%a) : (i32) -> ()
    %n = arith.addi %a, %c_step : i32
    scf.yield %n : i32
  }
  return %r : i32
}

// Stays: iv <= INT_MAX never ends
// CHECK-LABEL: func.func @sle_max(
//   CHECK-NOT:   scf.for
//       CHECK:   scf.while
func.func @sle_max(%lb: i32, %ub: i32) -> i32 {
  %c_step = arith.constant 1 : i32
  %c_bound = arith.constant 2147483647 : i32
  %r = scf.while (%i = %lb) : (i32) -> i32 {
    %c = arith.cmpi sle, %i, %c_bound : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    func.call @sink(%a) : (i32) -> ()
    %n = arith.addi %a, %c_step : i32
    scf.yield %n : i32
  }
  return %r : i32
}

// Stays: unsigned iv <= UINT_MAX never ends
// CHECK-LABEL: func.func @ule_max(
//   CHECK-NOT:   scf.for
//       CHECK:   scf.while
func.func @ule_max(%lb: i32, %ub: i32) -> i32 {
  %c_step = arith.constant 1 : i32
  %c_bound = arith.constant -1 : i32
  %r = scf.while (%i = %lb) : (i32) -> i32 {
    %c = arith.cmpi ule, %i, %c_bound : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    func.call @sink(%a) : (i32) -> ()
    %n = arith.addi %a, %c_step : i32
    scf.yield %n : i32
  }
  return %r : i32
}

// Stays: iv < ub, step 4, ub not constant: iv may wrap past INT_MAX
// CHECK-LABEL: func.func @slt_step4_var(
//   CHECK-NOT:   scf.for
//       CHECK:   scf.while
func.func @slt_step4_var(%lb: i32, %ub: i32) -> i32 {
  %c_step = arith.constant 4 : i32
  %r = scf.while (%i = %lb) : (i32) -> i32 {
    %c = arith.cmpi slt, %i, %ub : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    func.call @sink(%a) : (i32) -> ()
    %n = arith.addi %a, %c_step : i32
    scf.yield %n : i32
  }
  return %r : i32
}

// Stays: iv < INT_MAX - 1, step 3: the final iv may be INT_MAX + 1
// CHECK-LABEL: func.func @slt_step3_max1(
//   CHECK-NOT:   scf.for
//       CHECK:   scf.while
func.func @slt_step3_max1(%lb: i32, %ub: i32) -> i32 {
  %c_step = arith.constant 3 : i32
  %c_bound = arith.constant 2147483646 : i32
  %r = scf.while (%i = %lb) : (i32) -> i32 {
    %c = arith.cmpi slt, %i, %c_bound : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    func.call @sink(%a) : (i32) -> ()
    %n = arith.addi %a, %c_step : i32
    scf.yield %n : i32
  }
  return %r : i32
}

// Stays: iv != ub, ub not constant
// CHECK-LABEL: func.func @ne_var(
//   CHECK-NOT:   scf.for
//       CHECK:   scf.while
func.func @ne_var(%lb: i32, %ub: i32) -> i32 {
  %c_step = arith.constant 1 : i32
  %r = scf.while (%i = %lb) : (i32) -> i32 {
    %c = arith.cmpi ne, %i, %ub : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    func.call @sink(%a) : (i32) -> ()
    %n = arith.addi %a, %c_step : i32
    scf.yield %n : i32
  }
  return %r : i32
}

// Stays: iv != 5 from 10 counting up wraps around
// CHECK-LABEL: func.func @ne_wraps(
//   CHECK-NOT:   scf.for
//       CHECK:   scf.while
func.func @ne_wraps(%lb: i32, %ub: i32) -> i32 {
  %c_step = arith.constant 1 : i32
  %c_init = arith.constant 10 : i32
  %c_bound = arith.constant 5 : i32
  %r = scf.while (%i = %c_init) : (i32) -> i32 {
    %c = arith.cmpi ne, %i, %c_bound : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    func.call @sink(%a) : (i32) -> ()
    %n = arith.addi %a, %c_step : i32
    scf.yield %n : i32
  }
  return %r : i32
}

// Stays: iv != 9 from 0, step 2, never ends
// CHECK-LABEL: func.func @ne_step2(
//   CHECK-NOT:   scf.for
//       CHECK:   scf.while
func.func @ne_step2(%lb: i32, %ub: i32) -> i32 {
  %c_step = arith.constant 2 : i32
  %c_init = arith.constant 0 : i32
  %c_bound = arith.constant 9 : i32
  %r = scf.while (%i = %c_init) : (i32) -> i32 {
    %c = arith.cmpi ne, %i, %c_bound : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    func.call @sink(%a) : (i32) -> ()
    %n = arith.addi %a, %c_step : i32
    scf.yield %n : i32
  }
  return %r : i32
}

// Stays: iv < ub counting down
// CHECK-LABEL: func.func @slt_down(
//   CHECK-NOT:   scf.for
//       CHECK:   scf.while
func.func @slt_down(%lb: i32, %ub: i32) -> i32 {
  %c_step = arith.constant 1 : i32
  %r = scf.while (%i = %lb) : (i32) -> i32 {
    %c = arith.cmpi slt, %i, %ub : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    func.call @sink(%a) : (i32) -> ()
    %n = arith.subi %a, %c_step : i32
    scf.yield %n : i32
  }
  return %r : i32
}

// Stays: iv >= INT_MIN never ends
// CHECK-LABEL: func.func @sge_min(
//   CHECK-NOT:   scf.for
//       CHECK:   scf.while
func.func @sge_min(%lb: i32, %ub: i32) -> i32 {
  %c_step = arith.constant 1 : i32
  %c_bound = arith.constant -2147483648 : i32
  %r = scf.while (%i = %lb) : (i32) -> i32 {
    %c = arith.cmpi sge, %i, %c_bound : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    func.call @sink(%a) : (i32) -> ()
    %n = arith.subi %a, %c_step : i32
    scf.yield %n : i32
  }
  return %r : i32
}

// Stays: iv > INT_MIN + 1 counting down by 3 may wrap
// CHECK-LABEL: func.func @sgt_step3_min(
//   CHECK-NOT:   scf.for
//       CHECK:   scf.while
func.func @sgt_step3_min(%lb: i32, %ub: i32) -> i32 {
  %c_step = arith.constant 3 : i32
  %c_bound = arith.constant -2147483647 : i32
  %r = scf.while (%i = %lb) : (i32) -> i32 {
    %c = arith.cmpi sgt, %i, %c_bound : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    func.call @sink(%a) : (i32) -> ()
    %n = arith.subi %a, %c_step : i32
    scf.yield %n : i32
  }
  return %r : i32
}

// Stays: unsigned iv >= 0 never ends
// CHECK-LABEL: func.func @uge_zero(
//   CHECK-NOT:   scf.for
//       CHECK:   scf.while
func.func @uge_zero(%lb: i32, %ub: i32) -> i32 {
  %c_step = arith.constant 1 : i32
  %c_bound = arith.constant 0 : i32
  %r = scf.while (%i = %lb) : (i32) -> i32 {
    %c = arith.cmpi uge, %i, %c_bound : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    func.call @sink(%a) : (i32) -> ()
    %n = arith.subi %a, %c_step : i32
    scf.yield %n : i32
  }
  return %r : i32
}

// Stays: unsigned iv >= 1 counting down by 2 wraps from 1 to UINT_MAX
// CHECK-LABEL: func.func @uge_step2_wraps(
//   CHECK-NOT:   scf.for
//       CHECK:   scf.while
func.func @uge_step2_wraps(%lb: i32, %ub: i32) -> i32 {
  %c_step = arith.constant 2 : i32
  %c_bound = arith.constant 1 : i32
  %r = scf.while (%i = %lb) : (i32) -> i32 {
    %c = arith.cmpi uge, %i, %c_bound : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    func.call @sink(%a) : (i32) -> ()
    %n = arith.subi %a, %c_step : i32
    scf.yield %n : i32
  }
  return %r : i32
}

// The pass works in llvm.func too.
// CHECK-LABEL: llvm.func @in_llvm_func(
//       CHECK:   scf.for
//  CHECK-NEXT:     llvm.call @lsink
llvm.func @lsink(i32)
llvm.func @in_llvm_func(%ub: i32) -> i32 {
  %c0 = arith.constant 0 : i32
  %c1 = arith.constant 1 : i32
  %r = scf.while (%i = %c0) : (i32) -> i32 {
    %c = arith.cmpi slt, %i, %ub : i32
    scf.condition(%c) %i : i32
  } do {
  ^bb0(%a: i32):
    llvm.call @lsink(%a) : (i32) -> ()
    %n = arith.addi %a, %c1 : i32
    scf.yield %n : i32
  }
  llvm.return %r : i32
}

// Other loop-carried values stay in order, and the iv on the right of the
// comparison (sgt ub, iv) is iv < ub.
// CHECK-LABEL: func.func @carried(
//  CHECK-SAME:     %[[LB:[^:]*]]: i32, %[[UB:[^:]*]]: i32)
//       CHECK:   %[[R:.*]]:2 = scf.for %[[I:.*]] = %[[LB]] to %[[UB]] step %{{.*}} iter_args(%[[S:.*]] = %{{.*}}, %[[P:.*]] = %{{.*}}) -> (i32, i32) : i32 {
//  CHECK-NEXT:     %[[SN:.*]] = arith.addi %[[S]], %[[I]] : i32
//  CHECK-NEXT:     %[[PN:.*]] = arith.muli %[[P]], %[[I]] : i32
//       CHECK:     scf.yield %[[SN]], %[[PN]] : i32, i32
//       CHECK:   return %[[R]]#1, %{{.*}}, %[[R]]#0 : i32, i32, i32
func.func @carried(%lb: i32, %ub: i32) -> (i32, i32, i32) {
  %c0 = arith.constant 0 : i32
  %c1 = arith.constant 1 : i32
  %r:3 = scf.while (%s = %c0, %i = %lb, %p = %c1) : (i32, i32, i32) -> (i32, i32, i32) {
    %c = arith.cmpi sgt, %ub, %i : i32
    scf.condition(%c) %p, %i, %s : i32, i32, i32
  } do {
  ^bb0(%p2: i32, %i2: i32, %s2: i32):
    %sn = arith.addi %s2, %i2 : i32
    %pn = arith.muli %p2, %i2 : i32
    %n = arith.addi %i2, %c1 : i32
    scf.yield %sn, %n, %pn : i32, i32, i32
  }
  return %r#0, %r#1, %r#2 : i32, i32, i32
}
