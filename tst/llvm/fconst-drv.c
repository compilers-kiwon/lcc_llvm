/*
 * Driver for fconst.c: includes fconst.c again with the objects renamed
 * c_NAME, compiled by clang, and compares each value with lcc's, byte by
 * byte.  Exits with 0 if every check passes.
 */
#include <stdio.h>
#include <string.h>
#include "port.h"

#define F(n) c_##n
#include "fconst.c"

extern float fbig, fsmall, fsub, fzero, ftop;
extern double dbig, dsmall, dsub, dzero, dtop;
extern long double lbig, lsmall, lzero;
extern double dneg(void), dsum(void), zafter(void), nzero(void);
extern float fprod(void);

static int checks, bad;

static void same(char *name, void *lcc, void *c, int n) {
	checks++;
	if (memcmp(lcc, c, n) != 0) {
		bad++;
		printf("%s differs\n", name);
	}
}

#define V(n) same(#n, &n, &c_##n, sizeof n)
#define L(n) same(#n, &n, &c_##n, LDBYTES)

int main(void) {
	double d1, d2;
	float f1, f2;

	V(fbig); V(fsmall); V(fsub); V(fzero); V(ftop);
	V(dbig); V(dsmall); V(dsub); V(dzero); V(dtop);
	L(lbig); L(lsmall); L(lzero);
	d1 = dneg(); d2 = c_dneg(); same("dneg", &d1, &d2, sizeof d1);
	d1 = dsum(); d2 = c_dsum(); same("dsum", &d1, &d2, sizeof d1);
	f1 = fprod(); f2 = c_fprod(); same("fprod", &f1, &f2, sizeof f1);
	d1 = zafter(); d2 = c_zafter(); same("zafter", &d1, &d2, sizeof d1);
	d1 = nzero(); d2 = c_nzero(); same("nzero", &d1, &d2, sizeof d1);
	/* the expected values, not only as clang computes them */
	checks++;
	if (!(dbig > dtop && -dneg() > dtop && fbig > ftop
	    && dsmall == 0 && fsmall == 0 && lsmall == 0 && fprod() == 0
	    && dsub > 0 && fsub > 0 && dsum() == 1e-310 && zafter() == 0
	    && 1 / nzero() < 0)) {
		bad++;
		printf("unexpected values\n");
	}
	printf("%d checks, %d bad\n", checks, bad);
	return bad != 0;
}
