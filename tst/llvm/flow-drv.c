/*
 * Driver for flow.c: includes flow.c again with renamed functions,
 * compiled by clang, and compares.  Defines global functions with the
 * names of flow.c's static functions.  Exits with 0 if every check
 * passes.
 */
#include <stdio.h>
#define twice r_twice
#define count r_count
#define bump r_bump
#define afterret r_afterret
#define afterbreak r_afterbreak
#define entrylabel r_entrylabel
#define labels r_labels
#define backward r_backward
#define gotodead r_gotodead
#define nested r_nested
#define forever r_forever
#define whileone r_whileone
#define deadloops r_deadloops
#define sparse r_sparse
#define noreturn r_noreturn
#define setcount r_setcount
#define nothing r_nothing
#define getcount r_getcount
#define whilegoto r_whilegoto
#include "flow.c"
#undef twice
#undef count
#undef bump
#undef afterret
#undef afterbreak
#undef entrylabel
#undef labels
#undef backward
#undef gotodead
#undef nested
#undef forever
#undef whileone
#undef deadloops
#undef sparse
#undef noreturn
#undef setcount
#undef nothing
#undef getcount
#undef whilegoto

int afterret(int), afterbreak(int), entrylabel(int, int), labels(int);
int backward(int), gotodead(int), nested(int), forever(int), whileone(int);
int deadloops(int), sparse(int), noreturn(int *, int), getcount(void);
int whilegoto(int);
void setcount(int), nothing(void);

/* the names of flow.c's static functions and variable */
int twice(int x) { return 3*x; }
int count = 77;
void bump(int n) { count = -n; }

static int bad, n;
#define CHK(e, w) do { int a_ = (e), b_ = (w); n++; if (a_ != b_) { bad++; \
	printf("%s: %d != %d\n", #e, a_, b_); } } while (0)

int main(void) {
	static int ks[] = { -5, 0, 1, 2, 3, 4, 7, 13, 20, 999, 1000, 1001, 100000 };
	int i, j, k, v, w;

	for (i = 0; i < (int)(sizeof ks/sizeof ks[0]); i++) {
		k = ks[i];
		CHK(afterret(k), r_afterret(k));
		CHK(afterbreak(k), r_afterbreak(k));
		CHK(labels(k), r_labels(k));
		CHK(gotodead(k), r_gotodead(k));
		CHK(deadloops(k), r_deadloops(k));
		CHK(sparse(k), r_sparse(k));
		CHK(whileone(k), r_whileone(k));
		if (k >= 0 && k < 1000) {
			CHK(backward(k), r_backward(k));
			CHK(nested(k), r_nested(k));
			CHK(forever(k), r_forever(k));
			CHK(whilegoto(k), r_whilegoto(k));
		}
		for (j = -2; j < 3; j++)
			CHK(entrylabel(k%50, j), r_entrylabel(k%50, j));
		noreturn(&v, k);
		r_noreturn(&w, k);
		CHK(v, w);
		setcount(k);
		r_setcount(k);
		CHK(getcount(), r_getcount());
	}
	nothing();
	CHK(twice(5), 15);
	CHK(count, 77);
	bump(4);
	CHK(count, -4);
	printf("%d checks, %d bad\n", n, bad);
	return bad != 0;
}
