/*
 * Driver for fpcmp.c: the expected values are clang's results for the
 * same expressions.  Exits with 0 if every check passes.
 */
#include <stdio.h>
#include <float.h>
#define DECL(T, s) int eq##s(T, T), ne##s(T, T), lt##s(T, T), le##s(T, T), gt##s(T, T), ge##s(T, T);
DECL(double, _d) DECL(float, _f)
int if_lt(double, double), if_ge(double, double), if_not_le(double, double), and_gt(double, double, double);
int or_lt(float, float, float), cond_le(double, double), not_d(double), truth(double);
int while_lt(double, double), do_gt(double, double), for_le(double, double);
static int bad, n;
#define CHK(e, w) do { n++; if ((e) != (w)) { bad++; printf("%s [a=%g b=%g]: %d != %d\n", #e, (double)a, (double)b, e, w); } } while (0)
int main(void) {
	double zero = 0.0, inf = 1.0 / zero, nan = zero / zero;
	double v[] = { 0.0, -0.0, 1.0, -1.0, 2.5, DBL_MIN / 4, -DBL_MIN / 4, 1e300, 0, 0, 0 };
	int k = sizeof v / sizeof v[0], i, j, m;
	v[k - 3] = inf; v[k - 2] = -inf; v[k - 1] = nan;
	for (i = 0; i < k; i++) for (j = 0; j < k; j++) {
		double a = v[i], b = v[j]; float fa = (float)a, fb = (float)b;
		CHK(eq_d(a, b), a == b); CHK(ne_d(a, b), a != b); CHK(lt_d(a, b), a < b);
		CHK(le_d(a, b), a <= b); CHK(gt_d(a, b), a > b); CHK(ge_d(a, b), a >= b);
		CHK(eq_f(fa, fb), fa == fb); CHK(ne_f(fa, fb), fa != fb); CHK(lt_f(fa, fb), fa < fb);
		CHK(le_f(fa, fb), fa <= fb); CHK(gt_f(fa, fb), fa > fb); CHK(ge_f(fa, fb), fa >= fb);
		CHK(if_lt(a, b), a < b); CHK(if_ge(a, b), a >= b); CHK(if_not_le(a, b), !(a <= b));
		CHK(cond_le(a, b), a <= b ? 10 : 20);
		for (m = 0; m < k; m++) {
			double c = v[m];
			CHK(and_gt(a, b, c), a > b && b > c);
			CHK(or_lt(fa, fb, (float)c), fa < fb || fb < (float)c);
		}
		{ double x = a, y = b; int w = 0; while (x < y && w < 10) { x = x + 1.0; w++; } CHK(while_lt(a, b), w); }
		{ int w = 0; do w++; while (a > b && w < 5); CHK(do_gt(a, b), w); }
		{ double y = b; int w; for (w = 0; a <= y && w < 7; w++) y = y - 1.0; CHK(for_le(a, b), w); }
	}
	for (i = 0; i < k; i++) { double a = v[i], b = 0; CHK(not_d(a), !a); CHK(truth(a), a ? 1 : 0); }
	printf("%d checks, %d bad\n", n, bad);
	return bad != 0;
}
