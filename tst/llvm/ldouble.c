/*
 * long double (the x87 80-bit type on x86-64, IEEE quad or double on
 * other targets): arithmetic, comparisons (NaN too),
 * conversions to and from every arithmetic type, constants with more
 * digits than a double has and out of its range where long double has
 * a wider one (port.h), in data and in code, constant expressions the
 * compiler folds (where the host's long double is narrower than the
 * target's, lcc folds in software), globals and their layout, arguments and
 * results passed to and from functions compiled by clang, va_arg of long
 * double among other arguments, and printf's %Lg.  Compiled by lcc; ldouble-drv.c includes this file again,
 * compiled by clang, and compares the results bit for bit.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "port.h"

long double ldext(long double, double, long double, int, long double);	/* in ldouble-drv.c */

long double ldv[] = { 0.0L, -0.0L, 1.0L, -1.0L, 0.1L, 1.0L/3, 2.5L, -1e300L, 1e-300L,
	LDV(1e4000L, 1.5e308L), LDV(-1e-4000L, -1e-310L), LDMINC, LDMAXC,
	9223372036854775807.0L, 18446744073709551615.0L, 4294967296.5L, LDV(1e-4945L, 1e-320L) };
#define NLD (sizeof ldv / sizeof ldv[0])
struct lay { char c; long double x; int i; long double y[2]; } lay = { 'a', 1.5L, 7, { -0.25L, 1e100L } };

long double arith(int op, long double a, long double b) {
	switch (op) {
	case 0: return a + b;
	case 1: return a - b;
	case 2: return a * b;
	case 3: return b != 0 ? a / b : a;
	case 4: return -a;
	case 5: return a * a - b / 3 + 0.1L;
	}
	return 0;
}
int compare(long double a, long double b) {
	return (a < b) | (a <= b) << 1 | (a > b) << 2 | (a >= b) << 3 | (a == b) << 4 | (a != b) << 5
		| !(a < b) << 6 | !(a >= b) << 7;
}
/* constants in code, not in data: LDBL_MAX, the least subnormal, LDBL_MIN */
long double ldconst(int i) {
	if (i == 0) return LDMAXC;
	if (i == 1) return LDDENC;
	if (i == 2) return LDMINC;
	if (i == 3) return -0.0L;
	if (i == 4) return 0.1L;
	if (i == 5) return LDV(-1e-4945L, -1e-320L);
	if (i == 6) return LDV(1e4000L, 1.5e308L);
	if (i == 7) return 18446744073709551615.0L;
	return 1.0L/3;
}
#define NCONST 9
/* constant expressions, folded in data and in code; ONEP53 and ONEP24 are
   just above a tie of double and float, which rounding twice misses */
#define ONEP53 (1.0L + 1.0L/9007199254740992.0L + 1.0L/1180591620717411303424.0L)
#define ONEP24 (1.0L + 1.0L/16777216.0L + 1.0L/1180591620717411303424.0L)
#define F0 (0.1L + 0.2L)
#define F1 (0.3L - 0.1L)
#define F2 (1.1L * 1.1L)
#define F3 (2.0L / 3)
#define F4 (-(0.7L))
#define F5 ONEP53
#define F6 ONEP24
#define F7 (LDV(1e4000L, 1e300L) * LDV(1e-3000L, 1e-200L))
#define F8 (LDV(1e-4940L, 1e-310L) / 3)
#define F9 ((long double)0.1)
#define F10 ((long double)0.1f)
#define F11 ((long double)123456789 * 1000)
#define F12 ((long double)(double)ONEP53)
#define F13 ((long double)(float)ONEP24)
#define F14 ((long double)(long)(LV(1e15L, 1e9L) + 0.75L))
#define F15 ((long double)(double)(1.0L/3))
#define F16 ((long double)(unsigned long)(LV(1e15L, 4e9L) + 0.25L))
long double ldfv[] = { F0, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15, F16 };
#define NFOLD (sizeof ldfv / sizeof ldfv[0])
long double ldfold(int i) {
	switch (i) {
	case 0: return F0; case 1: return F1; case 2: return F2; case 3: return F3;
	case 4: return F4; case 5: return F5; case 6: return F6; case 7: return F7;
	case 8: return F8; case 9: return F9; case 10: return F10; case 11: return F11;
	case 12: return F12; case 13: return F13; case 14: return F14; case 15: return F15;
	}
	return F16;
}
int ldfoldc(void) {
	return (0.1L < 0.1000000000000000000000000000000001L) | (0.1L < 0.1L) << 1
		| (0.1L <= 0.1L) << 2 | (0.2L > 0.1L) << 3 | (0.1L > 0.1L) << 4
		| (0.2L >= 0.3L) << 5 | (1.0L/3 == 0.333333333333333333333333333333333333L) << 6
		| (0.1L != 0.1L) << 7 | (ONEP53 != 1.0L + 1.0L/9007199254740992.0L) << 8
		| (0.3L >= 0.3L) << 9 | (0.3L <= 0.2L) << 10 | (0.1L == 0.1L) << 11;
}
/* to other types, and back */
long tolong(long double a) { return a < LONG_MAX && a > LONG_MIN ? (long)a : 0; }
unsigned long toulong(long double a) { return a >= 0 && a < ULONG_MAX ? (unsigned long)a : 0; }
int toint(long double a) { return a < 2e9L && a > -2e9L ? (int)a : 0; }
unsigned tounsigned(long double a) { return a >= 0 && a < 4e9L ? (unsigned)a : 0; }
short toshort(long double a) { return a < 3e4L && a > -3e4L ? (short)a : 0; }
signed char tochar(long double a) { return a < 100 && a > -100 ? (signed char)a : 0; }
double todouble(long double a) { return (double)a; }
float tofloat(long double a) { return a < 1e38L && a > -1e38L ? (float)a : 0; }
long double fromlong(long x) { return x; }
long double fromulong(unsigned long x) { return x; }
long double fromint(int x) { return x; }
long double fromunsigned(unsigned x) { return x; }
long double fromdouble(double x) { return x; }
long double fromfloat(float x) { return x; }
/* arguments and results through clang's code */
long double passld(long double a, double b, long double c, int n, long double d) { return ldext(a, b, c, n, d) * 2 + a; }
int fmt(char *buf, long double a) {	/* %Lf only where it is short */
	if (a < 1e30L && a > -1e30L)
		return sprintf(buf, "%.20Lg|%Le|%10.3Lf|%Lg", a, a, a, -a);
	return sprintf(buf, "%.20Lg|%Le|%Lg", a, a, -a);
}
long double global(int i) { return ldv[i]; }
/* fmt: l long double, d double, i int */
long double vsum(const char *fmt, ...) {
	va_list ap; long double s = 0;
	va_start(ap, fmt);
	for (; *fmt; fmt++)
		switch (*fmt) {
		case 'l': s = s * 3 + va_arg(ap, long double); break;
		case 'd': s = s * 3 + va_arg(ap, double); break;
		case 'i': s = s * 3 + va_arg(ap, int); break;
		}
	va_end(ap);
	return s;
}
int layout(void) {
	return (int)(sizeof(long double) * 1000 + sizeof lay * 10 + ((char *)&lay.x - (char *)&lay) + ((char *)&lay.y - (char *)&lay) * 100000);
}
