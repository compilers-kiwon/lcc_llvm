/*
 * Driver for kr.c: calls its functions, directly and through pointers,
 * and compares the results with values computed here.  Defines globals
 * with the names of kr.c's static function and variable.  Exits with 0
 * if every check passes.
 */
#include <stdio.h>

int noproto(), later1(int), later2(), implicit(int), oldstyle();
int callnoproto(int), callimplicit(int), calloldstyle(int), protoold(int, long);
int callprotoold(int), krchar(), callkrchar(int), callrets(int);
char retc(int), oldretc();
unsigned char retuc(int);
short rets(int);
unsigned short retus(int);
int callearly(int, int), add2(int, int), sub2(int, int), callops(int, int, int);
int iseven(int), callhelper(int), fileimpl(int);
extern int implsize;
char *fplus1(void), *fplus2(void), *fplus3(void), *fhelper(void);
extern int (*early[])();
extern char *fplus[];
extern int (*ops[])(int, int);

/* the names of kr.c's static function and variable */
int helper(int x) { return -x; }
int counter = 77;

static int bad, n;
#define CHK(e, w) do { long a_ = (long)(e), b_ = (long)(w); n++; if (a_ != b_) { \
	bad++; printf("%s: %ld != %ld\n", #e, a_, b_); } } while (0)

int main(void) {
	static int xs[] = { 0, 1, -1, 65, 127, 128, 200, 255, 256, 300, -129,
		32767, 32768, 65535, 65536, 70000, -70000 };
	int i, j, x, c;

	for (i = 0; i < (int)(sizeof xs/sizeof xs[0]); i++) {
		x = xs[i];
		CHK(callnoproto(x), x*10 + 3);
		CHK(noproto(x, -x), x*10 - x);
		CHK(callimplicit(x), x*3 + 1);
		CHK(implicit(x), x*3);
		CHK(calloldstyle(x), x + 'A');
		CHK(oldstyle(x, "z"), x + 'z');
		CHK(callprotoold(x), x - 3);
		CHK(protoold(x, 5L), x - 5);
		CHK(callkrchar(x), (char)x + (short)x);
		CHK(krchar(x, x), (char)x + (short)x);
		CHK(retc(x), (char)x);
		CHK(retuc(x), (unsigned char)x);
		CHK(rets(x), (short)x);
		CHK(retus(x), (unsigned short)x);
		CHK(oldretc(x), (char)x);
		c = (char)x + (unsigned char)x + (short)x + (unsigned short)x + (char)x;
		CHK(callrets(x), 3*c);
		CHK(callearly(0, x), x - 1);
		CHK(callearly(1, x), x - 2);
		CHK(callearly(2, x), x*10 + 7);
		CHK(early[0](x), x - 1);
		CHK(early[2](x, 1), x*10 + 1);
		for (j = 0; j < 3; j++)
			CHK(callops(j, x, 3), j == 0 ? 2*(x + 3) : j == 1 ? 0 : 2*(x*3));
		CHK(ops[1](x, 3), x - 3);
		CHK(iseven(i), i%2 == 0);
	}
	CHK(implsize, sizeof(int));
	CHK(fileimpl(5), 12);
	CHK(early[0] == later1, 1);
	CHK(early[1] == later2, 1);
	CHK(early[2] == noproto, 1);
	CHK(ops[0] == add2, 1);
	CHK(ops[1] == sub2, 1);
	CHK(fplus[0], (char *)later1 + 2);
	CHK(fplus[1], (char *)later2 + 4);
	CHK(fplus1(), (char *)later1 + 2);
	CHK(fplus2(), (char *)later2 + 4);
	CHK(fplus3(), fhelper() - 2);
	CHK(fplus[2], fhelper() - 2);
	CHK(fhelper() != (char *)helper, 1);
	CHK(callhelper(4), 1004 + 6);
	CHK(callhelper(4), 1004 + 7);
	CHK(helper(4), -4);
	CHK(counter, 77);
	printf("%d checks, %d bad\n", n, bad);
	return bad != 0;
}
