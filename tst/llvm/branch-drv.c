/*
 * Driver for branch.c: compares the results with clang's own versions
 * of the same functions.  Exits with 0 if every check passes.
 */
#include <stdio.h>
#include <limits.h>
#define DECL(T, s) int eq##s(T, T), ne##s(T, T), lt##s(T, T), le##s(T, T), gt##s(T, T), ge##s(T, T);
DECL(int, _i) DECL(unsigned, _u) DECL(long, _l) DECL(unsigned long, _ul)
int lower(char), sclass(signed char), before(char *, char *), isnull(char *);
int both(int, int), either(int, int), pick(int, int, int);
int sum(int *, int), len(char *), find(int *, int, int), oddsum(int), triangles(int);
long collatz(long); unsigned umax(unsigned *, int);
static int bad, n;
#define CHK(e, w, fmt) do { n++; if ((e) != (w)) { bad++; printf("%s: " fmt " != " fmt "\n", #e, e, w); } } while (0)
#define RUN(T, s, V) { T v[] = V; int i, j, k = sizeof v / sizeof v[0]; \
	for (i = 0; i < k; i++) for (j = 0; j < k; j++) { T a = v[i], b = v[j]; \
		CHK(eq##s(a, b), a == b, "%d"); CHK(ne##s(a, b), a != b, "%d"); \
		CHK(lt##s(a, b), a < b, "%d"); CHK(le##s(a, b), a <= b, "%d"); \
		CHK(gt##s(a, b), a > b, "%d"); CHK(ge##s(a, b), a >= b, "%d"); } }
#define VI { 0, 1, -1, 2, -2, 100, INT_MAX, INT_MIN, INT_MAX-1, INT_MIN+1 }
#define VU { 0, 1, 2, 100, 0x7fffffffu, 0x80000000u, 0xffffffffu }
#define VL { 0, 1, -1, 0x7fffffffL, 0x80000000L, -0x80000000L, LONG_MAX, LONG_MIN }
#define VUL { 0, 1, 0xffffffffUL, 0x100000000UL, 0x8000000000000000UL, ULONG_MAX }
static long ref_collatz(long n) { long k = 0; do { n = n % 2 == 0 ? n / 2 : 3 * n + 1; k++; } while (n != 1); return k; }
static int ref_oddsum(int n) { int i, s = 0; for (i = 0; i < n; i++) { if (i % 2 == 0) continue; if (s > 1000) break; s += i; } return s; }
static int ref_tri(int n) { int i, j, c = 0; for (i = 1; i <= n; i++) for (j = 1; j <= i; j++) { if (i * j > 500) return c; c += (i ^ j) % 3; } return c; }
int main(void) {
	static char s[] = "The Quick brown fox, 123!";
	int a[50], i, j, k;
	unsigned ua[7] = { 3, 0x80000000u, 7, 0xffffffffu, 0, 12345, 1 };
	RUN(int, _i, VI) RUN(unsigned, _u, VU) RUN(long, _l, VL) RUN(unsigned long, _ul, VUL)
	for (i = 0; s[i]; i++) CHK(lower(s[i]), s[i] >= 'a' && s[i] <= 'z', "%d");
	for (i = -128; i < 128; i += 5) CHK(sclass((signed char)i), i < 0 ? -1 : i > 0, "%d");
	CHK(before(s, s + 1), 1, "%d"); CHK(before(s + 1, s), 0, "%d"); CHK(before(s, s), 0, "%d");
	CHK(isnull(0), 1, "%d"); CHK(isnull(s), 0, "%d");
	for (i = -2; i <= 2; i++) for (j = -2; j <= 2; j++) {
		CHK(both(i, j), i && j, "%d"); CHK(either(i, j), i || j, "%d");
		for (k = -2; k <= 2; k++) CHK(pick(i, j, k), i > j ? (j > k ? j : k) : i, "%d");
	}
	for (i = 0; i < 50; i++) a[i] = (i * 37) % 23 - 11;
	for (i = 0; i <= 50; i += 7) { int t = 0; for (j = 0; j < i; j++) t += a[j]; CHK(sum(a, i), t, "%d"); }
	CHK(len(s), (int)sizeof s - 1, "%d"); CHK(len(""), 0, "%d");
	for (i = 1; i < 300; i += 13) CHK(collatz((long)i * 1001 + 1), ref_collatz((long)i * 1001 + 1), "%ld");
	for (i = -12; i <= 12; i++) { int w = -1; for (j = 0; j < 50; j++) if (a[j] == i) { w = j; break; } CHK(find(a, 50, i), w, "%d"); }
	for (i = 0; i < 200; i += 9) CHK(oddsum(i), ref_oddsum(i), "%d");
	for (i = 0; i < 40; i += 3) CHK(triangles(i), ref_tri(i), "%d");
	CHK(umax(ua, 7), 0xffffffffu, "%u"); CHK(umax(ua, 3), 0x80000000u, "%u"); CHK(umax(ua, 0), 0u, "%u");
	printf("%d checks, %d bad\n", n, bad);
	return bad != 0;
}
