/*
 * Driver for shift.c.  Left shifts of signed values are computed in the
 * unsigned type, as signed overflow is undefined from C99 on; right
 * shifts of negative values are arithmetic in both compilers.  Exits
 * with 0 if every check passes.
 */
#include <stdio.h>
#include <limits.h>
int shl_i(int, int), shr_i(int, int), c_i(int), mix_i(int, int);
unsigned shl_u(unsigned, int), shr_u(unsigned, int), c_u(unsigned);
long shl_l(long, int), shr_l(long, int), c_l(long), mix_l(long, int);
unsigned long shl_ul(unsigned long, int), shr_ul(unsigned long, int), c_ul(unsigned long);
static int bad, n;
#define CHK(e, w, fmt) do { n++; if ((e) != (w)) { bad++; printf("%s: " fmt " != " fmt "\n", #e, e, w); } } while (0)
#define RUN(T, UT, s, fmt, V, W) { T v[] = V; int i, k; \
	for (i = 0; i < (int)(sizeof v / sizeof v[0]); i++) { T a = v[i]; \
		for (k = 0; k < W; k++) { \
			CHK(shl##s(a, k), (T)((UT)a << k), fmt); \
			CHK(shr##s(a, k), (T)(a >> k), fmt); } } }
#define VI { 0, 1, -1, 2, -2, 7, -7, 12345, -12345, 0x7fff, INT_MAX, INT_MIN, 0x55555555, (int)0xaaaaaaaau }
#define VU { 0, 1, 2, 7, 12345, 0x7fffffffu, 0x80000000u, 0xffffffffu, 0xaaaaaaaau }
#define VL { 0, 1, -1, 7, -7, 12345, -12345, 0x123456789L, -0x123456789L, LONG_MAX, LONG_MIN, 0x5555555555555555L }
#define VUL { 0, 1, 7, 0xffffffffUL, 0x123456789abcdefUL, 0x8000000000000000UL, ULONG_MAX, 0xaaaaaaaaaaaaaaaaUL }
int main(void) {
	int i, k;
	RUN(int, unsigned, _i, "%d", VI, 32)
	RUN(unsigned, unsigned, _u, "%u", VU, 32)
	RUN(long, unsigned long, _l, "%ld", VL, 64)
	RUN(unsigned long, unsigned long, _ul, "%lu", VUL, 64)
	for (i = -1000; i <= 1000; i += 37) {
		long a = i * 98765431L;
		CHK(c_i(i), (int)((unsigned)i << 3) ^ (i >> 5), "%d");
		CHK(c_u((unsigned)a), ((unsigned)a << 31) | ((unsigned)a >> 1), "%u");
		CHK(c_l(a), (long)((unsigned long)a << 40) + (a >> 33), "%ld");
		CHK(c_ul((unsigned long)a), ((unsigned long)a << 17) ^ ((unsigned long)a >> 47), "%lu");
		for (k = 0; k < 40; k += 3) {
			CHK(mix_i(i, k), (int)((unsigned)i << (k & 15)) >> (k % 7), "%d");
			CHK(mix_l(a, k), (long)((unsigned long)a << (k & 31)) - (a >> (k & 31)), "%ld");
		}
	}
	printf("%d checks, %d bad\n", n, bad);
	return bad != 0;
}
