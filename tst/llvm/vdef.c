/*
 * Definitions of functions with variable arguments: ints, longs,
 * pointers and doubles past the argument registers into the stack,
 * after many fixed arguments, char, short and float arguments read
 * as their promoted types, va_arg in loops and as arguments, two
 * traversals, a
 * va_list passed to another function and to glibc's vsprintf, and
 * calls from lcc as well as from clang (in vdef-drv.c).  Compiled by
 * lcc; vdef-drv.c checks the results.
 */
#include <stdarg.h>
#include <stdio.h>

long isum(int n, ...) {
	va_list ap; long s = 0; int i;
	va_start(ap, n);
	for (i = 0; i < n; i++) s = s * 3 + va_arg(ap, int);
	va_end(ap);
	return s;
}
double dsum(int n, ...) {
	va_list ap; double s = 0; int i;
	va_start(ap, n);
	for (i = 0; i < n; i++) s = s * 1.5 + va_arg(ap, double);
	va_end(ap);
	return s;
}
/* fmt tells the types: i int, u unsigned, l long, p char *, d double;
   c char, s short, b unsigned char and f float are passed promoted */
double mixed(const char *fmt, ...) {
	va_list ap; double s = 0;
	va_start(ap, fmt);
	for (; *fmt; fmt++) {
		s *= 2;
		switch (*fmt) {
		case 'i': s += va_arg(ap, int); break;
		case 'u': s += va_arg(ap, unsigned); break;
		case 'l': s += va_arg(ap, long); break;
		case 'p': { char *p = va_arg(ap, char *); s += p[0] + p[1]; } break;
		case 'd': s += va_arg(ap, double); break;
		case 'c': case 's': case 'b': s += va_arg(ap, int); break;
		case 'f': s += va_arg(ap, double); break;
		}
	}
	va_end(ap);
	return s;
}
/* the variable arguments start on the stack */
long late(int a, int b, int c, int d, int e, int f, ...) {
	va_list ap; long s = a + b + c + d + e + f;
	va_start(ap, f);
	s = s * 10 + va_arg(ap, long);
	s = s * 10 + va_arg(ap, int);
	s = s * 10 + (long)va_arg(ap, double);
	va_end(ap);
	return s;
}
double dlate(double a, double b, double c, double d, double e, double f, double g, double h, int n, ...) {
	va_list ap; double s = a + b + c + d + e + f + g + h;
	int i;
	va_start(ap, n);
	for (i = 0; i < n; i++) s = s * 2 + va_arg(ap, double) + va_arg(ap, int);
	va_end(ap);
	return s;
}
/* va_arg as an argument and in expressions; the order of two in one
   expression is unspecified */
static long pair(long a, long b) { return a * 1000 + b; }
long args(int n, ...) {
	va_list ap; long s; int i;
	va_start(ap, n);
	i = va_arg(ap, int);
	s = pair(i, va_arg(ap, long));
	s += va_arg(ap, int) * 7;
	s -= va_arg(ap, int);
	va_end(ap);
	return s;
}
/* two traversals of the same arguments */
long twice(int n, ...) {
	va_list ap; long s = 0; int i;
	va_start(ap, n);
	for (i = 0; i < n; i++) s += va_arg(ap, int);
	va_end(ap);
	va_start(ap, n);
	for (i = 0; i < n; i++) s = s * 2 + va_arg(ap, int);
	va_end(ap);
	return s;
}
/* a va_list passed on */
long visum(int n, va_list ap) { long s = 0; while (n-- > 0) s = s * 5 + va_arg(ap, long); return s; }
long passon(int n, ...) {
	va_list ap; long s;
	va_start(ap, n);
	s = visum(n, ap);
	va_end(ap);
	return s;
}
int format(char *buf, const char *fmt, ...) {
	va_list ap; int r;
	va_start(ap, fmt);
	r = vsprintf(buf, fmt, ap);
	va_end(ap);
	return r;
}
/* calls from lcc */
long lccisum(void) { return isum(9, 1, -2, 3, -4, 5, -6, 7, -8, 9); }
double lccmixed(void) { return mixed("idpclfsub", 3, 0.5, "xy", 'A', -5L, 2.25f, (short)-300, 7u, (unsigned char)200); }
long lcclate(void) { return late(1, 2, 3, 4, 5, 6, 7L, 8, 9.75); }
