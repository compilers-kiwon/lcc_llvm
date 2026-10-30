/*
 * Driver for arith.c: compiled by clang with -fwrapv, so that signed
 * overflow wraps as in the IR lcc emits.  Division by zero and
 * INT_MIN / -1 are left out.  Exits with 0 if every check passes.
 */
#include <stdio.h>
#include <limits.h>
#define DECL(T, s) T add##s(T, T), sub##s(T, T), mul##s(T, T), div##s(T, T), \
	mod##s(T, T), and##s(T, T), or##s(T, T), xor##s(T, T), neg##s(T), com##s(T);
DECL(int, _i) DECL(unsigned, _u) DECL(long, _l) DECL(unsigned long, _ul)
int mix(int, int); long mixl(long, long); extern int g;
static int bad, n;
#define CHK(e, w, fmt) do { n++; if ((e) != (w)) { bad++; printf("%s: " fmt " != " fmt "\n", #e, e, w); } } while (0)
#define RUN(T, s, fmt, V, MIN) { T v[] = V; int i, j, k = sizeof v / sizeof v[0]; \
	for (i = 0; i < k; i++) { T a = v[i]; \
		CHK(neg##s(a), (T)-a, fmt); CHK(com##s(a), (T)~a, fmt); \
		for (j = 0; j < k; j++) { T b = v[j]; \
			CHK(add##s(a, b), (T)(a + b), fmt); CHK(sub##s(a, b), (T)(a - b), fmt); \
			CHK(mul##s(a, b), (T)(a * b), fmt); CHK(and##s(a, b), (T)(a & b), fmt); \
			CHK(or##s(a, b), (T)(a | b), fmt); CHK(xor##s(a, b), (T)(a ^ b), fmt); \
			if (b != 0 && !(a == MIN && b == (T)-1)) { \
				CHK(div##s(a, b), (T)(a / b), fmt); CHK(mod##s(a, b), (T)(a % b), fmt); } } } }
#define VI { 0, 1, -1, 2, -2, 3, 7, -7, 100, -100, 12345, -12345, 0x7fff, 0x8000, INT_MAX, INT_MIN, INT_MAX-1, INT_MIN+1 }
#define VU { 0, 1, 2, 3, 7, 100, 12345, 0x7fffffffu, 0x80000000u, 0xfffffffeu, 0xffffffffu }
#define VL { 0, 1, -1, 2, -2, 7, -7, 12345, -12345, 0x7fffffffL, 0x80000000L, -0x80000000L, 0x123456789L, -0x123456789L, LONG_MAX, LONG_MIN, LONG_MAX-1 }
#define VUL { 0, 1, 2, 7, 12345, 0xffffffffUL, 0x100000000UL, 0x123456789abcdefUL, 0x8000000000000000UL, ULONG_MAX, ULONG_MAX-1 }
int main(void) {
	int a, b, gg = 0;
	RUN(int, _i, "%d", VI, INT_MIN)
	RUN(unsigned, _u, "%u", VU, 0)
	RUN(long, _l, "%ld", VL, LONG_MIN)
	RUN(unsigned long, _ul, "%lu", VUL, 0)
	for (a = -20; a <= 20; a += 3) for (b = -9; b <= 9; b += 2) {
		int t = a + b, w;
		gg += t;
		w = (a + b) * (a + b) - (a ^ b) - t * 3 + gg % 7;
		CHK(mix(a, b), w, "%d");
		CHK(mixl(a * 1000003L, b), (a * 1000003L * b + a * 1000003L) / (b | 1) - ~(a * 1000003L & b), "%ld");
	}
	CHK(g, gg, "%d");
	printf("%d checks, %d bad\n", n, bad);
	return bad != 0;
}
