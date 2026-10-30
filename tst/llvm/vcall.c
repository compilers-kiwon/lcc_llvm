/*
 * Calls to variadic functions (sprintf, and functions in vcall-drv.c
 * that read their arguments with va_arg) and to functions without a
 * prototype: old-style definitions, implicitly declared functions and
 * functions declared without parameters.  Compiled by lcc; vcall-drv.c
 * checks the results.
 */
#include <stdio.h>

long vsum(int n, ...);		/* in vcall-drv.c */
double dsum(int n, ...);	/* in vcall-drv.c: needs %al to be set */
long kr_ext();			/* in vcall-drv.c: long kr_ext(int, long, double) */

char buf[200];
char *fmt1(int i, long l, double d, char *s) {
	sprintf(buf, "%d|%ld|%.3f|%s|%c|%5.1e", i, l, d, s, 'x', 2.5);
	return buf;
}
long vs(int a, int b) { return vsum(4, a, (long)b, a, 1000000000000L) + vsum(0); }
double ds(double x) { return dsum(3, x, 0.25, 1e10); }
double dsi(double x) { return dsum(9, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, x); }

int old(a, b) int a; long b; { return a * 100 + b; }
int useold(int x) { return old(x, 7L) + later(x); }	/* later is implicit */
int later(int x) { return x - 1; }
long usekr(int i) { return kr_ext(i, 1000L, 2.5); }
