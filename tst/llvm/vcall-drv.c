/*
 * Driver for vcall.c, with the variadic functions vsum and dsum and the
 * function kr_ext that vcall.c calls without a prototype.  Exits with 0
 * if every check passes.
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
char *fmt1(int, long, double, char *);
long vs(int, int); double ds(double), dsi(double);
int useold(int); long usekr(int);
long vsum(int n, ...) {		/* ints and longs, as told by the count */
	va_list ap; long s = 0; int i;
	va_start(ap, n);
	for (i = 0; i < n; i++) s += i % 2 ? va_arg(ap, long) : va_arg(ap, int);
	va_end(ap);
	return s;
}
double dsum(int n, ...) {
	va_list ap; double s = 0; int i;
	va_start(ap, n);
	for (i = 0; i < n; i++) s += va_arg(ap, double);
	va_end(ap);
	return s;
}
long kr_ext(int i, long l, double d) { return i * l + (long)(d * 4); }
static int bad, n;
#define CHK(e, w, fmt) do { n++; if ((e) != (w)) { bad++; printf("%s: " fmt " != " fmt "\n", #e, e, w); } } while (0)
int main(void) {
	char want[200];
	int i;
	for (i = -3; i <= 3; i++) {
		long l = i * 123456789012L; double d = i * 1.25;
		sprintf(want, "%d|%ld|%.3f|%s|%c|%5.1e", i, l, d, "str", 'x', 2.5);
		n++; if (strcmp(fmt1(i, l, d, "str"), want) != 0) { bad++; printf("fmt1: %s != %s\n", fmt1(i, l, d, "str"), want); }
		CHK(vs(i, i * 7), i + (long)(i * 7) + i + 1000000000000L, "%ld");
		CHK(ds(d), d + 0.25 + 1e10, "%g");
		CHK(dsi(d), 36.0 + d, "%g");
		CHK(useold(i), i * 100 + 7 + i - 1, "%d");
		CHK(usekr(i), i * 1000L + 10, "%ld");
	}
	printf("%d checks, %d bad\n", n, bad);
	return bad != 0;
}
