/*
 * long double (the x87 80-bit type on x86-64, IEEE quad or double on
 * other targets): arithmetic, comparisons (NaN too),
 * conversions to and from every arithmetic type, constants with more
 * digits than a double has and out of its range where long double has
 * a wider one (port.h), in data and in code, globals and their layout, arguments and
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
