/*
 * Driver for libc.c: includes libc.c again with renamed functions,
 * compiled by clang, and compares.  Exits with 0 if every check passes.
 */
#define ctypes r_ctypes
#define mathf r_mathf
#define convs r_convs
#define strings r_strings
#define sorts r_sorts
#define jumps r_jumps
#define signals r_signals
#define times r_times
#define stdios r_stdios
#define locales r_locales
#define limit r_limit
#define mbs r_mbs
#include "libc.c"
#undef ctypes
#undef mathf
#undef convs
#undef strings
#undef sorts
#undef jumps
#undef signals
#undef times
#undef stdios
#undef locales
#undef limit
#undef mbs
int ctypes(int); double mathf(int, double, double); long convs(int, char *), strings(int, char *);
long sorts(int), jumps(int); int signals(int); long times(int, char *), stdios(int, char *);
long locales(int, char *); double limit(int); long mbs(int);
static int bad, n;
#define CHK(e, w, fmt) do { n++; if ((e) != (w)) { bad++; printf("%s: " fmt " != " fmt "\n", #e, e, w); } } while (0)
#define CHKS(a, b) do { n++; if (strcmp(a, b) != 0) { bad++; printf("line %d: \"%s\" != \"%s\"\n", __LINE__, a, b); } } while (0)
static int same(double a, double b) { return a == b || a != a && b != b; }
int main(void) {
	static double xs[] = { 0, 0.5, -0.5, 1, -1, 2.75, -3.25, 10, -20, 1e-300, 700, 1e10, 0.1 };
	static double ys[] = { 1, -1, 0.5, 3, -2.5 };
	static int sigs[] = { SIGABRT, SIGFPE, SIGILL, SIGINT, SIGSEGV, SIGTERM };
	char b1[256], b2[256];
	int c, f, i, j;
	for (c = EOF; c <= UCHAR_MAX; c++)
		CHK(ctypes(c), r_ctypes(c), "%d");
	for (f = 0; f < NMATH; f++)
		for (i = 0; i < (int)(sizeof xs / sizeof xs[0]); i++)
			for (j = 0; j < (int)(sizeof ys / sizeof ys[0]); j++) {
				double a = mathf(f, xs[i], ys[j]), b = r_mathf(f, xs[i], ys[j]);
				n++;
				if (!same(a, b)) { bad++; printf("mathf(%d, %g, %g): %.17g != %.17g\n", f, xs[i], ys[j], a, b); }
			}
	for (f = 0; f < NCONV; f++)
		for (i = 0; numbers[i]; i++)
			CHK(convs(f, numbers[i]), r_convs(f, numbers[i]), "%ld");
	for (f = 0; f < NSTR; f++) {
		CHK(strings(f, b1), r_strings(f, b2), "%ld");
		CHKS(b1, b2);
	}
	for (f = 0; f < NSORT; f++)
		CHK(sorts(f), r_sorts(f), "%ld");
	for (i = 0; i < 5; i++)
		CHK(jumps(i), r_jumps(i), "%ld");
	for (i = 0; i < (int)(sizeof sigs / sizeof sigs[0]); i++)
		CHK(signals(sigs[i]), r_signals(sigs[i]), "%d");
	for (f = 0; f < NTIME; f++) {
		b1[0] = b2[0] = 0;
		CHK(times(f, b1), r_times(f, b2), "%ld");
		CHKS(b1, b2);
	}
	for (f = 0; f < NSTDIO; f++) {
		b1[0] = b2[0] = 0;
		CHK(stdios(f, b1), r_stdios(f, b2), "%ld");
		CHKS(b1, b2);
	}
	for (f = 0; f < NLOCALE; f++) {
		CHK(locales(f, b1), r_locales(f, b2), "%ld");
		CHKS(b1, b2);
	}
	for (i = 0; i < (int)NLIMITS; i++)
		CHK(limit(i), r_limit(i), "%g");
	for (f = 0; f < NMB; f++)
		CHK(mbs(f), r_mbs(f), "%ld");
	printf("%d checks, %d bad\n", n, bad);
	return bad != 0;
}
