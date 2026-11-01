/*
 * Driver for switch.c: includes switch.c again with renamed functions,
 * compiled by clang, and compares.  Exits with 0 if every check passes.
 */
#include <stdio.h>
#include <limits.h>
#define swglobal r_swglobal
#define dense r_dense
#define nodefault r_nodefault
#define fallthrough r_fallthrough
#define clusters r_clusters
#define sparse r_sparse
#define longends r_longends
#define ulongsw r_ulongsw
#define unsignedsw r_unsignedsw
#define charsw r_charsw
#define ucharsw r_ucharsw
#define shortsw r_shortsw
#define enumsw r_enumsw
#define globalsw r_globalsw
#define exprsw r_exprsw
#define nested r_nested
#define run r_run
#include "switch.c"
#undef swglobal
#undef dense
#undef nodefault
#undef fallthrough
#undef clusters
#undef sparse
#undef longends
#undef ulongsw
#undef unsignedsw
#undef charsw
#undef ucharsw
#undef shortsw
#undef enumsw
#undef globalsw
#undef exprsw
#undef nested
#undef run
extern int swglobal;
int dense(int), nodefault(int), fallthrough(int), clusters(int), sparse(long), longends(long);
int ulongsw(unsigned long), unsignedsw(unsigned), charsw(char), ucharsw(unsigned char), shortsw(short);
int enumsw(enum color), globalsw(void), exprsw(int, int), nested(int, int); long run(const char *, long);
static int bad, n;
#define CHK(e, w, fmt) do { n++; if ((e) != (w)) { bad++; printf("%s: " fmt " != " fmt "\n", #e, e, w); } } while (0)
static long lvals[] = { 0, 1, 7, 1000, 123456789L, -987654321L, LONG_MAX, LONG_MIN, LONG_MAX - 1, LONG_MAX - 4,
	LONG_MAX - 5, LONG_MIN + 1, LONG_MIN + 3, LONG_MIN + 4, -1, 2, 5, 6, 0x100000000L, -0x100000000L };
static const char *progs[] = { "", "+++", "12*3-n.", "9/n/n/x", "abc+-*", "nnn.", "7*7*7*7*7*7*7*7*7*7", "q.9" };
int main(void) {
	long i; int j;
	for (i = -20; i <= 20; i++) {
		CHK(dense(i), r_dense(i), "%d"); CHK(nodefault(i), r_nodefault(i), "%d");
		CHK(fallthrough(i), r_fallthrough(i), "%d"); CHK(exprsw(i, 2), r_exprsw(i, 2), "%d");
		CHK(exprsw(1, i), r_exprsw(1, i), "%d");
		for (j = -2; j < 10; j++) CHK(nested(i, j), r_nested(i, j), "%d");
		swglobal = r_swglobal = i; CHK(globalsw(), r_globalsw(), "%d");
	}
	for (i = -1100000; i <= 6000; i += (i < -999990 || i > -10 && i < 1100 || i > 4990) ? 1 : 997)
		CHK(clusters(i), r_clusters(i), "%d");
	for (j = 0; j < (int)(sizeof lvals / sizeof lvals[0]); j++) {
		long v = lvals[j];
		CHK(sparse(v), r_sparse(v), "%d"); CHK(longends(v), r_longends(v), "%d");
		CHK(ulongsw(v), r_ulongsw(v), "%d"); CHK(ulongsw(-v), r_ulongsw(-v), "%d");
		CHK(unsignedsw(v), r_unsignedsw(v), "%d"); CHK(unsignedsw(-v - 6), r_unsignedsw(-v - 6), "%d");
	}
	for (j = -5; j < 10; j++) {
		CHK(ulongsw(j), r_ulongsw(j), "%d"); CHK(ulongsw(-j), r_ulongsw(-j), "%d");
		CHK(unsignedsw(-j), r_unsignedsw(-j), "%d"); CHK(unsignedsw(0x80000000U + j), r_unsignedsw(0x80000000U + j), "%d");
		CHK(longends(LONG_MAX - j), r_longends(LONG_MAX - j), "%d"); CHK(longends(LONG_MIN + j), r_longends(LONG_MIN + j), "%d");
	}
	for (j = CHAR_MIN; j <= UCHAR_MAX; j++) {
		CHK(charsw(j), r_charsw(j), "%d"); CHK(ucharsw(j), r_ucharsw(j), "%d");
	}
	for (j = -32768; j <= 32767; j += (j < -32700 || j > 32700 || j > -10 && j < 10) ? 1 : 101)
		CHK(shortsw(j), r_shortsw(j), "%d");
	for (j = -1; j < 12; j++) CHK(enumsw((enum color)j), r_enumsw((enum color)j), "%d");
	for (j = 0; j < (int)(sizeof progs / sizeof progs[0]); j++)
		for (i = -3; i <= 3; i++) CHK(run(progs[j], i), r_run(progs[j], i), "%ld");
	printf("%d checks, %d bad\n", n, bad);
	return bad != 0;
}
