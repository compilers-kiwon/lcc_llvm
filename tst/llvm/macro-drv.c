/*
 * Driver for macro.c: includes macro.c again with the functions renamed
 * c_NAME, compiled by clang, and compares each string and value with
 * lcc's.  Exits with 0 if every check passes.
 */
#include <stdio.h>
#include <string.h>

#define F(n) c_##n
#include "macro.c"

extern char *sstr(void), *sstrsp(void), *swrap(void), *swrapsp(void);
extern char *swrapcm(void), *swrapnl(void), *sid(void), *sidid(void);
extern char *spair(void), *scat(void), *scat3(void), *szero(void);
extern char *scall(void), *sparen(void);
extern int vone(void), vplus(void), vplus0(void), varr(void), vif(void);

static int checks, bad;

static void str(char *name, char *lcc, char *c) {
	checks++;
	if (strcmp(lcc, c) != 0) {
		bad++;
		printf("%s: lcc \"%s\", clang \"%s\"\n", name, lcc, c);
	}
}

static void val(char *name, int lcc, int c) {
	checks++;
	if (lcc != c) {
		bad++;
		printf("%s: lcc %d, clang %d\n", name, lcc, c);
	}
}

#define S(n) str(#n, n(), c_##n())
#define V(n) val(#n, n(), c_##n())

int main(void) {
	S(sstr); S(sstrsp); S(swrap); S(swrapsp); S(swrapcm); S(swrapnl);
	S(sid); S(sidid); S(spair); S(scat); S(scat3); S(szero); S(scall);
	S(sparen);
	V(vone); V(vplus); V(vplus0); V(varr); V(vif);
	/* the expected values, not only as clang computes them */
	str("swrap", swrap(), "<>");
	val("vplus0", vplus0(), 5);
	val("varr", varr(), 3);
	printf("%d checks, %d bad\n", checks, bad);
	return bad != 0;
}
