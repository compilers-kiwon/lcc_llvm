/*
 * Driver for iconst.c: includes iconst.c again with the objects renamed
 * c_NAME, compiled by clang, and compares.  Exits with 0 if every check
 * passes.
 */
#include <stdio.h>

#define I(n) c_##n
#include "iconst.c"

extern int kinds[];
extern long folded[];
long hexadd(int), hexneg(int), hexlt(int), octadd(int), hexshl(int), hexdiv(int);

static int n, bad;

static void chk(char *what, int i, long lcc, long c) {
	n++;
	if (lcc != c) {
		bad++;
		printf("%s %d: %ld != %ld\n", what, i, lcc, c);
	}
}

int main(void) {
	long (*lf[])(int) = { hexadd, hexneg, hexlt, octadd, hexshl, hexdiv };
	long (*cf[])(int) = { c_hexadd, c_hexneg, c_hexlt, c_octadd, c_hexshl, c_hexdiv };
	int i, x;

	for (i = 0; i < (int)(sizeof c_kinds / sizeof c_kinds[0]); i++)
		chk("kinds", i, kinds[i], c_kinds[i]);
	for (i = 0; i < (int)(sizeof c_folded / sizeof c_folded[0]); i++)
		chk("folded", i, folded[i], c_folded[i]);
	for (i = 0; i < (int)(sizeof lf / sizeof lf[0]); i++)
		for (x = -3; x <= 3; x++)
			chk("function", i, lf[i](x), cf[i](x));
	/* the expected values, not only as clang computes them */
	n++;
	if (!(kinds[5] == 2 * (int)sizeof(unsigned) + 1 && kinds[6] == kinds[5]
	    && kinds[9] == kinds[5] && folded[0] == 0 && hexadd(1) == 0
	    && hexneg(1) == 0x80000001L && hexlt(1) == 0 && hexshl(1) == 0
	    && hexdiv(1) == 0)) {
		bad++;
		printf("unexpected values\n");
	}
	printf("%d checks, %d bad\n", n, bad);
	return bad != 0;
}
