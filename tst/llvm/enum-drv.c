/*
 * Driver for enum.c: includes enum.c again with the functions renamed
 * c_NAME, compiled by clang, and compares their results with those of
 * enum.c compiled by lcc, called here through declarations with the
 * enums' integer types.  Exits with 0 if every check passes.
 */
#include <stdio.h>
#include <limits.h>
#define F(n) c_##n
#include "enum.c"

unsigned ucompat(unsigned);
int scompat(int);
extern unsigned uvar;
long oldu(unsigned);
int krdecl(unsigned);
long usub(unsigned, int);
int ugt(unsigned);
long udiv(unsigned);
long umod(unsigned);
long ushr(unsigned);
double udbl(unsigned);
long uwide(unsigned);
int ucmp(unsigned, int);
int ucmpc(unsigned);
long sdiv(int);
long sshr(int);
double sdbl(int);
int scmp(int, int);
int mlt(int);
long ucast(int);
long scast(int);
long cnst(void);
int usw(unsigned);
unsigned *uptr(unsigned *);
unsigned *uptr2(unsigned *);
int *sptr(int *);
int scond(int *, int *, int);
int ucond(unsigned *, unsigned *, int);
long mcond(int *, int *, int);
unsigned uderef(unsigned *);

static int bad, n;
#define CHK(e, w) do { long a_ = (long)(e), b_ = (long)(w); n++; if (a_ != b_) { \
	bad++; printf("%s: %ld != %ld\n", #e, a_, b_); } } while (0)
#define CHKD(e, w) do { double a_ = (e), b_ = (w); n++; if (a_ != b_) { \
	bad++; printf("%s: %g != %g\n", #e, a_, b_); } } while (0)

int main(void) {
	static int xs[] = { 0, 1, 2, 3, 5, 8, 11, 12, 100, -1, -2, -3, -8,
		INT_MAX, INT_MIN, INT_MIN + 1 };
	int i, j, x, c;
	unsigned ua, ub;
	int ia, ib;

	CHK(sizeof(enum u), sizeof(int));
	CHK(sizeof(enum s), sizeof(int));
	CHK(sizeof(enum m), sizeof(int));
	CHK(uvar, 5);
	CHK(cnst(), c_cnst());
	CHK(cnst(), -1);
	CHK(udiv(0) > 0, 1);	/* unsigned, not only as clang computes it */
	CHK(sdiv(0), -1);
	for (i = 0; i < (int)(sizeof xs/sizeof xs[0]); i++) {
		x = xs[i];
		CHK(ucompat(x), c_ucompat(x));
		CHK(scompat(x), c_scompat(x));
		CHK(oldu(x), c_oldu(x));
		CHK(krdecl(x), c_krdecl(x));
		for (j = 0; j < (int)(sizeof xs/sizeof xs[0]); j++) {
			CHK(usub(x, xs[j]), c_usub(x, xs[j]));
			CHK(ucmp(x, xs[j]), c_ucmp(x, xs[j]));
			CHK(scmp(x, xs[j]), c_scmp(x, xs[j]));
		}
		CHK(ugt(x), c_ugt(x));
		CHK(udiv(x), c_udiv(x));
		CHK(umod(x), c_umod(x));
		CHK(ushr(x), c_ushr(x));
		CHKD(udbl(x), c_udbl(x));
		CHK(uwide(x), c_uwide(x));
		CHK(ucmpc(x), c_ucmpc(x));
		CHK(sdiv(x), c_sdiv(x));
		CHK(sshr(x), c_sshr(x));
		CHKD(sdbl(x), c_sdbl(x));
		CHK(mlt(x), c_mlt(x));
		CHK(ucast(x), c_ucast(x));
		CHK(scast(x), c_scast(x));
		CHK(usw(x), c_usw(x));
		for (c = 0; c < 2; c++) {
			ia = x; ib = -x;
			ua = x; ub = -x;
			CHK(scond(&ia, &ib, c), c_scond((enum s *)&ia, &ib, c));
			CHK(ucond(&ua, &ub, c), c_ucond((enum u *)&ua, &ub, c));
			CHK(mcond(&ia, &ib, c), c_mcond((enum m *)&ia, &ib, c));
		}
		ua = x;
		CHK(uptr(&ua), &ua);
		CHK(uptr2(&ua), &ua);
		CHK(sptr(&ia), &ia);
		CHK(uderef(&ua), (unsigned)x + 1);
		CHK(ua, (unsigned)x + 1);
	}
	printf("%d checks, %d bad\n", n, bad);
	return bad != 0;
}
