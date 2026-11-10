/*
 * Bit fields among other members, laid out as gcc and clang do (see
 * bflayout.h): initialized globals, members set and read here, and
 * structures passed to and returned from functions compiled by clang.
 * Compiled by lcc; bflayout-drv.c checks the results.
 */
#include "bflayout.h"

struct s1 g_s1 = INIT_s1;
int lsize_s1(void) { return sizeof(struct s1); }
void lf_s1(struct s1 *p, int k) { fill_s1(p, k); }
long lp_s1(struct s1 *p) { return sum_s1(p); }
long lt_s1(struct s1 x) { return sum_s1(&x); }
struct s1 lr_s1(int k) { struct s1 x; fill_s1(&x, k); return x; }
long dt_s1(struct s1);
long lc_s1(int k) { struct s1 x; fill_s1(&x, k); return dt_s1(x); }
struct s2 g_s2 = INIT_s2;
int lsize_s2(void) { return sizeof(struct s2); }
void lf_s2(struct s2 *p, int k) { fill_s2(p, k); }
long lp_s2(struct s2 *p) { return sum_s2(p); }
long lt_s2(struct s2 x) { return sum_s2(&x); }
struct s2 lr_s2(int k) { struct s2 x; fill_s2(&x, k); return x; }
long dt_s2(struct s2);
long lc_s2(int k) { struct s2 x; fill_s2(&x, k); return dt_s2(x); }
struct s4 g_s4 = INIT_s4;
int lsize_s4(void) { return sizeof(struct s4); }
void lf_s4(struct s4 *p, int k) { fill_s4(p, k); }
long lp_s4(struct s4 *p) { return sum_s4(p); }
long lt_s4(struct s4 x) { return sum_s4(&x); }
struct s4 lr_s4(int k) { struct s4 x; fill_s4(&x, k); return x; }
long dt_s4(struct s4);
long lc_s4(int k) { struct s4 x; fill_s4(&x, k); return dt_s4(x); }
struct s5 g_s5 = INIT_s5;
int lsize_s5(void) { return sizeof(struct s5); }
void lf_s5(struct s5 *p, int k) { fill_s5(p, k); }
long lp_s5(struct s5 *p) { return sum_s5(p); }
long lt_s5(struct s5 x) { return sum_s5(&x); }
struct s5 lr_s5(int k) { struct s5 x; fill_s5(&x, k); return x; }
long dt_s5(struct s5);
long lc_s5(int k) { struct s5 x; fill_s5(&x, k); return dt_s5(x); }
struct s7 g_s7 = INIT_s7;
int lsize_s7(void) { return sizeof(struct s7); }
void lf_s7(struct s7 *p, int k) { fill_s7(p, k); }
long lp_s7(struct s7 *p) { return sum_s7(p); }
long lt_s7(struct s7 x) { return sum_s7(&x); }
struct s7 lr_s7(int k) { struct s7 x; fill_s7(&x, k); return x; }
long dt_s7(struct s7);
long lc_s7(int k) { struct s7 x; fill_s7(&x, k); return dt_s7(x); }
struct s10 g_s10 = INIT_s10;
int lsize_s10(void) { return sizeof(struct s10); }
void lf_s10(struct s10 *p, int k) { fill_s10(p, k); }
long lp_s10(struct s10 *p) { return sum_s10(p); }
long lt_s10(struct s10 x) { return sum_s10(&x); }
struct s10 lr_s10(int k) { struct s10 x; fill_s10(&x, k); return x; }
long dt_s10(struct s10);
long lc_s10(int k) { struct s10 x; fill_s10(&x, k); return dt_s10(x); }
struct s12 g_s12 = INIT_s12;
int lsize_s12(void) { return sizeof(struct s12); }
void lf_s12(struct s12 *p, int k) { fill_s12(p, k); }
long lp_s12(struct s12 *p) { return sum_s12(p); }
long lt_s12(struct s12 x) { return sum_s12(&x); }
struct s12 lr_s12(int k) { struct s12 x; fill_s12(&x, k); return x; }
long dt_s12(struct s12);
long lc_s12(int k) { struct s12 x; fill_s12(&x, k); return dt_s12(x); }
struct s13 g_s13 = INIT_s13;
int lsize_s13(void) { return sizeof(struct s13); }
void lf_s13(struct s13 *p, int k) { fill_s13(p, k); }
long lp_s13(struct s13 *p) { return sum_s13(p); }
long lt_s13(struct s13 x) { return sum_s13(&x); }
struct s13 lr_s13(int k) { struct s13 x; fill_s13(&x, k); return x; }
long dt_s13(struct s13);
long lc_s13(int k) { struct s13 x; fill_s13(&x, k); return dt_s13(x); }
struct s14 g_s14 = INIT_s14;
int lsize_s14(void) { return sizeof(struct s14); }
void lf_s14(struct s14 *p, int k) { fill_s14(p, k); }
long lp_s14(struct s14 *p) { return sum_s14(p); }
long lt_s14(struct s14 x) { return sum_s14(&x); }
struct s14 lr_s14(int k) { struct s14 x; fill_s14(&x, k); return x; }
long dt_s14(struct s14);
long lc_s14(int k) { struct s14 x; fill_s14(&x, k); return dt_s14(x); }
union u1 g_u1 = INIT_u1;
int lsize_u1(void) { return sizeof(union u1); }
void lf_u1(union u1 *p, int k) { fill_u1(p, k); }
long lp_u1(union u1 *p) { return sum_u1(p); }
long lt_u1(union u1 x) { return sum_u1(&x); }
union u1 lr_u1(int k) { union u1 x; fill_u1(&x, k); return x; }
long dt_u1(union u1);
long lc_u1(int k) { union u1 x; fill_u1(&x, k); return dt_u1(x); }
