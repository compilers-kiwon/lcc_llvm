/*
 * Driver for bflayout.c: clang lays out the same structures, and the
 * bytes lcc's code writes must be those clang's writes.  Exits with 0 if
 * every check passes.
 */
#include <stdio.h>
#include <string.h>
#include "bflayout.h"

static int bad, n;
#define CHK(e, w, fmt) do { n++; if ((e) != (w)) { bad++; printf("%s: " fmt " != " fmt "\n", #e, e, w); } } while (0)
#define SAME(what, a, b, size) do { n++; if (memcmp(a, b, size) != 0) { bad++; printf("%s differs\n", what); } } while (0)

extern struct s1 g_s1;
struct s1 r_g_s1 = INIT_s1;
int lsize_s1(void); void lf_s1(struct s1 *, int); long lp_s1(struct s1 *);
long lt_s1(struct s1); struct s1 lr_s1(int); long lc_s1(int);
long dt_s1(struct s1 x) { return sum_s1(&x); }
static void check_s1(int k) {
	struct s1 a, b, r;
	CHK(lsize_s1(), (int)sizeof(struct s1), "%d");
	SAME("g_s1", &g_s1, &r_g_s1, sizeof(struct s1));
	memset(&a, 0, sizeof a);
	memset(&b, 0, sizeof b);
	lf_s1(&a, k);
	fill_s1(&b, k);
	SAME("lf_s1", &a, &b, sizeof a);
	CHK(lp_s1(&b), sum_s1(&b), "%ld");
	CHK(lt_s1(b), sum_s1(&b), "%ld");
	r = lr_s1(k);
	CHK(sum_s1(&r), sum_s1(&b), "%ld");
	CHK(lc_s1(k), sum_s1(&b), "%ld");
}
extern struct s2 g_s2;
struct s2 r_g_s2 = INIT_s2;
int lsize_s2(void); void lf_s2(struct s2 *, int); long lp_s2(struct s2 *);
long lt_s2(struct s2); struct s2 lr_s2(int); long lc_s2(int);
long dt_s2(struct s2 x) { return sum_s2(&x); }
static void check_s2(int k) {
	struct s2 a, b, r;
	CHK(lsize_s2(), (int)sizeof(struct s2), "%d");
	SAME("g_s2", &g_s2, &r_g_s2, sizeof(struct s2));
	memset(&a, 0, sizeof a);
	memset(&b, 0, sizeof b);
	lf_s2(&a, k);
	fill_s2(&b, k);
	SAME("lf_s2", &a, &b, sizeof a);
	CHK(lp_s2(&b), sum_s2(&b), "%ld");
	CHK(lt_s2(b), sum_s2(&b), "%ld");
	r = lr_s2(k);
	CHK(sum_s2(&r), sum_s2(&b), "%ld");
	CHK(lc_s2(k), sum_s2(&b), "%ld");
}
extern struct s4 g_s4;
struct s4 r_g_s4 = INIT_s4;
int lsize_s4(void); void lf_s4(struct s4 *, int); long lp_s4(struct s4 *);
long lt_s4(struct s4); struct s4 lr_s4(int); long lc_s4(int);
long dt_s4(struct s4 x) { return sum_s4(&x); }
static void check_s4(int k) {
	struct s4 a, b, r;
	CHK(lsize_s4(), (int)sizeof(struct s4), "%d");
	SAME("g_s4", &g_s4, &r_g_s4, sizeof(struct s4));
	memset(&a, 0, sizeof a);
	memset(&b, 0, sizeof b);
	lf_s4(&a, k);
	fill_s4(&b, k);
	SAME("lf_s4", &a, &b, sizeof a);
	CHK(lp_s4(&b), sum_s4(&b), "%ld");
	CHK(lt_s4(b), sum_s4(&b), "%ld");
	r = lr_s4(k);
	CHK(sum_s4(&r), sum_s4(&b), "%ld");
	CHK(lc_s4(k), sum_s4(&b), "%ld");
}
extern struct s5 g_s5;
struct s5 r_g_s5 = INIT_s5;
int lsize_s5(void); void lf_s5(struct s5 *, int); long lp_s5(struct s5 *);
long lt_s5(struct s5); struct s5 lr_s5(int); long lc_s5(int);
long dt_s5(struct s5 x) { return sum_s5(&x); }
static void check_s5(int k) {
	struct s5 a, b, r;
	CHK(lsize_s5(), (int)sizeof(struct s5), "%d");
	SAME("g_s5", &g_s5, &r_g_s5, sizeof(struct s5));
	memset(&a, 0, sizeof a);
	memset(&b, 0, sizeof b);
	lf_s5(&a, k);
	fill_s5(&b, k);
	SAME("lf_s5", &a, &b, sizeof a);
	CHK(lp_s5(&b), sum_s5(&b), "%ld");
	CHK(lt_s5(b), sum_s5(&b), "%ld");
	r = lr_s5(k);
	CHK(sum_s5(&r), sum_s5(&b), "%ld");
	CHK(lc_s5(k), sum_s5(&b), "%ld");
}
extern struct s7 g_s7;
struct s7 r_g_s7 = INIT_s7;
int lsize_s7(void); void lf_s7(struct s7 *, int); long lp_s7(struct s7 *);
long lt_s7(struct s7); struct s7 lr_s7(int); long lc_s7(int);
long dt_s7(struct s7 x) { return sum_s7(&x); }
static void check_s7(int k) {
	struct s7 a, b, r;
	CHK(lsize_s7(), (int)sizeof(struct s7), "%d");
	SAME("g_s7", &g_s7, &r_g_s7, sizeof(struct s7));
	memset(&a, 0, sizeof a);
	memset(&b, 0, sizeof b);
	lf_s7(&a, k);
	fill_s7(&b, k);
	SAME("lf_s7", &a, &b, sizeof a);
	CHK(lp_s7(&b), sum_s7(&b), "%ld");
	CHK(lt_s7(b), sum_s7(&b), "%ld");
	r = lr_s7(k);
	CHK(sum_s7(&r), sum_s7(&b), "%ld");
	CHK(lc_s7(k), sum_s7(&b), "%ld");
}
extern struct s10 g_s10;
struct s10 r_g_s10 = INIT_s10;
int lsize_s10(void); void lf_s10(struct s10 *, int); long lp_s10(struct s10 *);
long lt_s10(struct s10); struct s10 lr_s10(int); long lc_s10(int);
long dt_s10(struct s10 x) { return sum_s10(&x); }
static void check_s10(int k) {
	struct s10 a, b, r;
	CHK(lsize_s10(), (int)sizeof(struct s10), "%d");
	SAME("g_s10", &g_s10, &r_g_s10, sizeof(struct s10));
	memset(&a, 0, sizeof a);
	memset(&b, 0, sizeof b);
	lf_s10(&a, k);
	fill_s10(&b, k);
	SAME("lf_s10", &a, &b, sizeof a);
	CHK(lp_s10(&b), sum_s10(&b), "%ld");
	CHK(lt_s10(b), sum_s10(&b), "%ld");
	r = lr_s10(k);
	CHK(sum_s10(&r), sum_s10(&b), "%ld");
	CHK(lc_s10(k), sum_s10(&b), "%ld");
}
extern struct s12 g_s12;
struct s12 r_g_s12 = INIT_s12;
int lsize_s12(void); void lf_s12(struct s12 *, int); long lp_s12(struct s12 *);
long lt_s12(struct s12); struct s12 lr_s12(int); long lc_s12(int);
long dt_s12(struct s12 x) { return sum_s12(&x); }
static void check_s12(int k) {
	struct s12 a, b, r;
	CHK(lsize_s12(), (int)sizeof(struct s12), "%d");
	SAME("g_s12", &g_s12, &r_g_s12, sizeof(struct s12));
	memset(&a, 0, sizeof a);
	memset(&b, 0, sizeof b);
	lf_s12(&a, k);
	fill_s12(&b, k);
	SAME("lf_s12", &a, &b, sizeof a);
	CHK(lp_s12(&b), sum_s12(&b), "%ld");
	CHK(lt_s12(b), sum_s12(&b), "%ld");
	r = lr_s12(k);
	CHK(sum_s12(&r), sum_s12(&b), "%ld");
	CHK(lc_s12(k), sum_s12(&b), "%ld");
}
extern struct s13 g_s13;
struct s13 r_g_s13 = INIT_s13;
int lsize_s13(void); void lf_s13(struct s13 *, int); long lp_s13(struct s13 *);
long lt_s13(struct s13); struct s13 lr_s13(int); long lc_s13(int);
long dt_s13(struct s13 x) { return sum_s13(&x); }
static void check_s13(int k) {
	struct s13 a, b, r;
	CHK(lsize_s13(), (int)sizeof(struct s13), "%d");
	SAME("g_s13", &g_s13, &r_g_s13, sizeof(struct s13));
	memset(&a, 0, sizeof a);
	memset(&b, 0, sizeof b);
	lf_s13(&a, k);
	fill_s13(&b, k);
	SAME("lf_s13", &a, &b, sizeof a);
	CHK(lp_s13(&b), sum_s13(&b), "%ld");
	CHK(lt_s13(b), sum_s13(&b), "%ld");
	r = lr_s13(k);
	CHK(sum_s13(&r), sum_s13(&b), "%ld");
	CHK(lc_s13(k), sum_s13(&b), "%ld");
}
extern struct s14 g_s14;
struct s14 r_g_s14 = INIT_s14;
int lsize_s14(void); void lf_s14(struct s14 *, int); long lp_s14(struct s14 *);
long lt_s14(struct s14); struct s14 lr_s14(int); long lc_s14(int);
long dt_s14(struct s14 x) { return sum_s14(&x); }
static void check_s14(int k) {
	struct s14 a, b, r;
	CHK(lsize_s14(), (int)sizeof(struct s14), "%d");
	SAME("g_s14", &g_s14, &r_g_s14, sizeof(struct s14));
	memset(&a, 0, sizeof a);
	memset(&b, 0, sizeof b);
	lf_s14(&a, k);
	fill_s14(&b, k);
	SAME("lf_s14", &a, &b, sizeof a);
	CHK(lp_s14(&b), sum_s14(&b), "%ld");
	CHK(lt_s14(b), sum_s14(&b), "%ld");
	r = lr_s14(k);
	CHK(sum_s14(&r), sum_s14(&b), "%ld");
	CHK(lc_s14(k), sum_s14(&b), "%ld");
}
extern union u1 g_u1;
union u1 r_g_u1 = INIT_u1;
int lsize_u1(void); void lf_u1(union u1 *, int); long lp_u1(union u1 *);
long lt_u1(union u1); union u1 lr_u1(int); long lc_u1(int);
long dt_u1(union u1 x) { return sum_u1(&x); }
static void check_u1(int k) {
	union u1 a, b, r;
	CHK(lsize_u1(), (int)sizeof(union u1), "%d");
	SAME("g_u1", &g_u1, &r_g_u1, sizeof(union u1));
	memset(&a, 0, sizeof a);
	memset(&b, 0, sizeof b);
	lf_u1(&a, k);
	fill_u1(&b, k);
	SAME("lf_u1", &a, &b, sizeof a);
	CHK(lp_u1(&b), sum_u1(&b), "%ld");
	CHK(lt_u1(b), sum_u1(&b), "%ld");
	r = lr_u1(k);
	CHK(sum_u1(&r), sum_u1(&b), "%ld");
	CHK(lc_u1(k), sum_u1(&b), "%ld");
}

int main(void) {
	int k;
	for (k = 0; k < 20; k++) {
		check_s1(k);
		check_s2(k);
		check_s4(k);
		check_s5(k);
		check_s7(k);
		check_s10(k);
		check_s12(k);
		check_s13(k);
		check_s14(k);
		check_u1(k);
	}
	printf("%d checks, %d bad\n", n, bad);
	return bad != 0;
}
