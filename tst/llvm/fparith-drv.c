/*
 * Driver for fparith.c: compares the bits of each result with clang's
 * result for the same operation, so NaNs, signed zeros, infinities and
 * subnormals must match exactly.  Exits with 0 if every check passes.
 */
#include <stdio.h>
#include <string.h>
#include <float.h>
double dadd(double, double), dsub(double, double), dmul(double, double), ddiv(double, double), dneg(double);
float fadd(float, float), fsub(float, float), fmul(float, float), fdiv(float, float);
double dpoly(double), dsum(double *, int), shared(double, double); float fpoly(float), fdot(float *, float *, int);
extern double g; void acc(double);
static int bad, n;
#define CHK(e, w) do { __typeof__(w) x_ = (e), w_ = (w); n++; \
	if (memcmp(&x_, &w_, sizeof x_) != 0) { bad++; printf("%s: %.17g != %.17g\n", #e, (double)x_, (double)w_); } } while (0)
int main(void) {
	double zero = 0.0, inf = 1.0 / zero, nan = zero / zero;
	double dv[] = { 0.0, -0.0, 1.0, -1.0, 0.1, 3.5, -2.75, 1e300, -1e-300, DBL_MIN / 3, DBL_MAX, 1.0 / 3, 0, 0 };
	float fv[] = { 0.0f, -0.0f, 1.0f, -1.0f, 0.1f, 3.5f, -2.75f, 1e30f, -1e-30f, FLT_MIN / 3, FLT_MAX, 1.0f / 3, 0, 0 };
	int k = sizeof dv / sizeof dv[0], i, j;
	double da[100]; float fa[50], fb[50];
	dv[k - 2] = inf; dv[k - 1] = nan; fv[k - 2] = (float)inf; fv[k - 1] = (float)nan;
	for (i = 0; i < k; i++) {
		double a = dv[i]; float fa1 = fv[i];
		CHK(dneg(a), -a);
		CHK(dpoly(a), ((2.5 * a - 1.25) * a + 0.5) * a - 3.0);
		CHK(fpoly(fa1), ((2.5f * fa1 - 1.25f) * fa1 + 0.5f) * fa1 - 3.0f);
		for (j = 0; j < k; j++) {
			double b = dv[j]; float fb1 = fv[j];
			CHK(dadd(a, b), a + b); CHK(dsub(a, b), a - b); CHK(dmul(a, b), a * b); CHK(ddiv(a, b), a / b);
			CHK(fadd(fa1, fb1), fa1 + fb1); CHK(fsub(fa1, fb1), fa1 - fb1);
			CHK(fmul(fa1, fb1), fa1 * fb1); CHK(fdiv(fa1, fb1), fa1 / fb1);
			{ double t = a + b; CHK(shared(a, b), t * t - (a - b) / (t + 1.0)); }
		}
	}
	for (i = 0; i < 100; i++) da[i] = (i % 7 - 3) * 0.37 + i * 1e-3;
	for (i = 0; i <= 100; i += 9) { double s = 0.0; for (j = 0; j < i; j++) s = s + da[j]; CHK(dsum(da, i), s); }
	for (i = 0; i < 50; i++) { fa[i] = i * 0.25f - 3; fb[i] = 1.0f / (i + 1); }
	for (i = 0; i <= 50; i += 7) { float s = 0.0f; for (j = 0; j < i; j++) s = s + fa[j] * fb[j]; CHK(fdot(fa, fb, i), s); }
	{ double w = 0.0; g = 0.0; for (i = 0; i < 20; i++) { acc(i * 1.5); w = w * 0.5 + i * 1.5; } CHK(g, w); }
	printf("%d checks, %d bad\n", n, bad);
	return bad != 0;
}
