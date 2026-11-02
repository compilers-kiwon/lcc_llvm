/*
 * Driver for abi.c, with the dt_ functions that abi.c calls.  Exits
 * with 0 if every check passes.
 */
#include <stdarg.h>
#include <stdio.h>
#include "abi.h"

static int bad, n;
#define CHK(e, w) do { double e_ = (e), w_ = (w); n++; if (e_ != w_) { bad++; printf("%s: %.17g != %.17g\n", #e, e_, w_); } } while (0)

#define DT(n) double dt_##n(struct n s) { return sum_##n(&s); }
#define UDT(n) double dt_##n(union n s) { return sum_##n(&s); }
SHAPES(DT)
USHAPES(UDT)
#define DECL(n) double lt_##n(struct n), lc_##n(int), ll_##n(int);
#define UDECL(n) double lt_##n(union n), lc_##n(int), ll_##n(int);
SHAPES(DECL)
USHAPES(UDECL)

double dt_press1(long a, long b, long c, long d, long e, struct l2 s, long g) {
	return a + 2 * b + 3 * c + 4 * d + 5 * e + 7 * sum_l2(&s) + 11 * g;
}
double dt_press2(double a, double b, double c, double d, double e, double f, double g, struct d2 s, double h) {
	return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 11 * sum_d2(&s) + 13 * h;
}
double dt_press3(struct i2 a, struct id b, struct f2 c, struct l3 d, struct di e, struct i3 f, struct l2 g, struct fi h) {
	return sum_i2(&a) + 2 * sum_id(&b) + 3 * sum_f2(&c) + 5 * sum_l3(&d) + 7 * sum_di(&e)
		+ 11 * sum_i3(&f) + 13 * sum_l2(&g) + 17 * sum_fi(&h);
}
double dt_press4(int n, struct d2 a, struct d2 b, struct d2 c, struct d2 d, struct f1 e, float f) {
	return n + 2 * sum_d2(&a) + 3 * sum_d2(&b) + 5 * sum_d2(&c) + 7 * sum_d2(&d) + 11 * sum_f1(&e) + 13 * f;
}
double lt_press1(long, long, long, long, long, struct l2, long);
double lt_press2(double, double, double, double, double, double, double, struct d2, double);
double lt_press3(struct i2, struct id, struct f2, struct l3, struct di, struct i3, struct l2, struct fi);
double lt_press4(int, struct d2, struct d2, struct d2, struct d2, struct f1, float);
double lc_press(int, int), lc_var(int), lc_noproto(int);

double dv(int n, ...) {
	va_list ap; double s;
	struct i3 a; struct f3 b; struct di c; struct l3 d; struct d2 e; struct l2 f;
	va_start(ap, n);
	a = va_arg(ap, struct i3); b = va_arg(ap, struct f3); c = va_arg(ap, struct di);
	d = va_arg(ap, struct l3); e = va_arg(ap, struct d2); f = va_arg(ap, struct l2);
	s = n + sum_i3(&a) + 2 * sum_f3(&b) + 3 * sum_di(&c) + 5 * sum_l3(&d) + 7 * sum_d2(&e) + 11 * sum_l2(&f)
		+ 13 * va_arg(ap, double);
	va_end(ap);
	return s;
}

int main(void) {
	int k;
	for (k = -3; k <= 5; k++) {
#define CHECK(n) { struct n s; fill_##n(&s, k); CHK(lt_##n(s), sum_##n(&s)); CHK(lc_##n(k), sum_##n(&s)); CHK(ll_##n(k), sum_##n(&s)); }
#define UCHECK(n) { union n s; fill_##n(&s, k); CHK(lt_##n(s), sum_##n(&s)); CHK(lc_##n(k), sum_##n(&s)); CHK(ll_##n(k), sum_##n(&s)); }
		SHAPES(CHECK)
		USHAPES(UCHECK)
		{
			struct l2 l2; struct d2 d2a, d2b, d2c, d2d; struct i2 i2; struct id id; struct f2 f2;
			struct l3 l3; struct di di; struct i3 i3; struct fi fi; struct f1 f1; struct f3 f3; struct ffi ffi;
			double w;
			fill_l2(&l2, k); fill_d2(&d2a, k); fill_d2(&d2b, k + 1); fill_d2(&d2c, k + 2); fill_d2(&d2d, k + 3);
			fill_i2(&i2, k); fill_id(&id, k); fill_f2(&f2, k); fill_l3(&l3, k); fill_di(&di, k);
			fill_i3(&i3, k); fill_fi(&fi, k); fill_f1(&f1, k); fill_f3(&f3, k); fill_ffi(&ffi, k);
			w = dt_press1(k, k + 1, k + 2, k + 3, k + 4, l2, k + 6);
			CHK(lt_press1(k, k + 1, k + 2, k + 3, k + 4, l2, k + 6), w); CHK(lc_press(k, 1), w); CHK(lc_press(k, 11), w);
			w = dt_press2(k, k + 1, k + 2, k + 3, k + 4, k + 5, k + 6, d2a, k + 8);
			CHK(lt_press2(k, k + 1, k + 2, k + 3, k + 4, k + 5, k + 6, d2a, k + 8), w); CHK(lc_press(k, 2), w); CHK(lc_press(k, 12), w);
			w = dt_press3(i2, id, f2, l3, di, i3, l2, fi);
			CHK(lt_press3(i2, id, f2, l3, di, i3, l2, fi), w); CHK(lc_press(k, 3), w); CHK(lc_press(k, 13), w);
			w = dt_press4(k, d2a, d2b, d2c, d2d, f1, k + 0.5f);
			CHK(lt_press4(k, d2a, d2b, d2c, d2d, f1, k + 0.5f), w); CHK(lc_press(k, 4), w); CHK(lc_press(k, 14), w);
			CHK(lc_var(k), dv(6, i3, f3, di, l3, d2a, l2, k + 0.5));
			CHK(lc_noproto(k), dt_i3(i3) + 2 * dt_ffi(ffi) + 3 * dt_l3(l3));
		}
	}
	printf("%d checks, %d bad\n", n, bad);
	return bad != 0;
}
