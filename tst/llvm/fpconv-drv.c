/*
 * Driver for fpconv.c: compares the bits of each result with clang's
 * conversion of the same value.  Only values that fit the target type
 * are converted to integers.  Exits with 0 if every check passes.
 */
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <float.h>
double i2d(int), l2d(long), c2d(char), u2d(unsigned), ul2d(unsigned long);
float i2f(int), l2f(long), s2f(short), u2f(unsigned), ul2f(unsigned long);
int d2i(double), f2i(float); long d2l(double), f2l(float);
unsigned d2u(double), f2u(float); unsigned long d2ul(double), f2ul(float); char d2c(double);
double f2d(float); float d2f(double); double viald(double), viald2(double); float fneg(float);
long double d2ld(double), ul2ld(unsigned long); double ld2d(long double); unsigned long ld2ul(long double);
double mix(float, double, int, long); float fmix(float, int); long round_down(double);
static int bad, n;
#define CHK(e, w) do { __typeof__(w) x_ = (e), w_ = (w); n++; \
	if (memcmp(&x_, &w_, sizeof x_) != 0) { bad++; printf("%s: %.17g != %.17g\n", #e, (double)x_, (double)w_); } } while (0)
/* the 10 bytes of an x87 long double; the other 6 are padding */
#define CHKLD(e, w) do { long double x_ = (e), w_ = (w); n++; \
	if (memcmp(&x_, &w_, 10) != 0) { bad++; printf("%s: %.21Lg != %.21Lg\n", #e, x_, w_); } } while (0)
int main(void) {
	static long lv[] = { 0, 1, -1, 7, -7, 255, 256, 32767, -32768, 16777216, 16777217, -16777217,
		INT_MAX, INT_MIN, 0x7ffffffffffffc00L, 9007199254740993L, -9007199254740993L, LONG_MAX, LONG_MIN };
	static unsigned long uv[] = { 0, 1, 255, 0x7fffffffUL, 0x80000000UL, 0x80000001UL, 0xffffffffUL,
		0x100000000UL, 9007199254740993UL, 0x7fffffffffffffffUL, 0x8000000000000000UL,
		0x8000000000000401UL, 0x8000000000000400UL, 0x8000000000000c00UL, 0xfffffffffffff800UL,
		0xfffffffffffffbffUL, 0x8000008000000001UL, 0x8000008000000000UL, 0x8000018000000000UL,
		0x7fffff8000000001UL, 0x1234567890abcdefUL, 0xfedcba9876543211UL, ULONG_MAX };
	static double dv[] = { 0.0, -0.0, 0.4, 0.5, 0.6, -0.5, -1.5, 2.5, 99.99, -99.99, 1e9, -2e9,
		2147483647.0, -2147483648.0, 4294967295.0, 3e18, -9.2e18, 9.2e18, 1.8e19, 1e-310, 1e300 };
	int i, k;
	for (i = 0; i < (int)(sizeof lv / sizeof lv[0]); i++) {
		long x = lv[i];
		CHK(i2d((int)x), (double)(int)x); CHK(i2f((int)x), (float)(int)x);
		CHK(l2d(x), (double)x); CHK(l2f(x), (float)x);
		CHK(c2d((char)x), (double)(char)x); CHK(s2f((short)x), (float)(short)x);
	}
	for (i = 0; i < (int)(sizeof uv / sizeof uv[0]); i++) {
		unsigned long x = uv[i];
		CHK(u2d((unsigned)x), (double)(unsigned)x); CHK(u2f((unsigned)x), (float)(unsigned)x);
		CHK(ul2d(x), (double)x); CHK(ul2f(x), (float)x);
		CHKLD(ul2ld(x), (long double)x); CHK(ld2ul((long double)x), x);
		CHK(ld2d((long double)x), (double)x); CHK(ld2d((long double)x / 3), (double)((long double)x / 3));
	}
	for (i = 0; i < (int)(sizeof dv / sizeof dv[0]); i++) {
		double x = dv[i]; float f = (float)x;
		if (x > INT_MIN - 1.0 && x < INT_MAX + 1.0) { CHK(d2i(x), (int)x); }
		if (f > (float)INT_MIN - 1.0f && f < (float)INT_MAX) { CHK(f2i(f), (int)f); }
		if (x > -9.22e18 && x < 9.22e18) { CHK(d2l(x), (long)x); CHK(f2l(f), (long)f); CHK(round_down(x), x < 0 ? (long)(x - 0.5) : (long)(x + 0.5)); }
		if (x > -1.0 && x < UINT_MAX + 1.0) { CHK(d2u(x), (unsigned)x); }
		if (f > -1.0f && f < 4294967296.0f) { CHK(f2u(f), (unsigned)f); }
		if (x > -1.0 && x < 1.8446744073709552e19) { CHK(d2ul(x), (unsigned long)x); CHK(f2ul(f), (unsigned long)f); }
		if (x > -128.0 && x < 128.0) { CHK(d2c(x), (char)x); }
		CHK(f2d(f), (double)f); CHK(d2f(x), (float)x); CHK(fneg(f), -f);
		CHK(viald(x), x); CHK(viald2(x), x * 2);
		CHKLD(d2ld(x), (long double)x); CHK(ld2d((long double)x * 0.1L), (double)((long double)x * 0.1L));
		for (k = -3; k <= 3; k++) {
			CHK(mix(f, x, k * 7, (long)k * 3), f * x + k * 7 / 2 - (long)k * 3 * f);
			CHK(fmix(f, k), f * k + 0.5f);
		}
	}
	printf("%d checks, %d bad\n", n, bad);
	return bad != 0;
}
