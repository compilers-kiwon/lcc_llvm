/*
 * Structures and unions passed by value as the SysV x86-64 ABI says:
 * from clang to lcc (lt_), from lcc to clang (lc_, calling dt_ in
 * abi-drv.c) and from lcc to lcc (ll_), in registers or on the stack,
 * also when the registers run out part way, to functions with variable
 * arguments, and through a declaration without a prototype; and
 * returned in registers or memory, from lcc (lr_) and to lcc (lcr_,
 * calling dr_, and llr_).  Compiled by lcc; abi-drv.c checks the
 * results.
 */
#include <stdarg.h>
#include "abi.h"

#define DECL(n) double dt_##n(struct n);
#define UDECL(n) double dt_##n(union n);
SHAPES(DECL)
USHAPES(UDECL)
#define TAKE(n) double lt_##n(struct n s) { return sum_##n(&s); }
#define UTAKE(n) double lt_##n(union n s) { return sum_##n(&s); }
SHAPES(TAKE)
USHAPES(UTAKE)
#define CALL(n) double lc_##n(int k) { struct n s; fill_##n(&s, k); return dt_##n(s); }
#define UCALL(n) double lc_##n(int k) { union n s; fill_##n(&s, k); return dt_##n(s); }
SHAPES(CALL)
USHAPES(UCALL)
#define SELF(n) double ll_##n(int k) { struct n s; fill_##n(&s, k); return lt_##n(s); }
#define USELF(n) double ll_##n(int k) { union n s; fill_##n(&s, k); return lt_##n(s); }
SHAPES(SELF)
USHAPES(USELF)

#define RDECL(n) struct n dr_##n(int);
#define URDECL(n) union n dr_##n(int);
SHAPES(RDECL)
USHAPES(URDECL)
#define RET(n) struct n lr_##n(int k) { struct n s; fill_##n(&s, k); return s; }
#define URET(n) union n lr_##n(int k) { union n s; fill_##n(&s, k); return s; }
SHAPES(RET)
USHAPES(URET)
#define CALLR(n) double lcr_##n(int k) { struct n s; s = dr_##n(k); return sum_##n(&s); }
#define UCALLR(n) double lcr_##n(int k) { union n s; s = dr_##n(k); return sum_##n(&s); }
SHAPES(CALLR)
USHAPES(UCALLR)
#define SELFR(n) double llr_##n(int k) { struct n s; s = lr_##n(k); return sum_##n(&s); }
#define USELFR(n) double llr_##n(int k) { union n s; s = lr_##n(k); return sum_##n(&s); }
SHAPES(SELFR)
USHAPES(USELFR)

/* the address of a result in memory takes an integer register; one in
   registers does not */
struct l3 lr_big(long a, long b, long c, long d, long e, struct i2 s) {
	struct l3 r;
	r.a = a + b + c; r.b = d + e; r.c = (long)sum_i2(&s);
	return r;
}
struct l2 lr_small(long a, long b, long c, long d, long e, struct i2 s) {
	struct l2 r;
	r.a = a + b + c; r.b = d + e + (long)sum_i2(&s);
	return r;
}
/* with variable arguments */
struct d2 lr_var(int n, ...) {
	va_list ap; struct d2 r;
	va_start(ap, n);
	r.a = va_arg(ap, long); r.b = 0;
	while (--n > 0) r.b = r.b * 2 + va_arg(ap, double);
	va_end(ap);
	return r;
}
struct l3 dr_big(long, long, long, long, long, struct i2);
struct l2 dr_small(long, long, long, long, long, struct i2);
struct d2 dr_var(int, ...);
double lcr_misc(int k, int which) {
	struct i2 i2; struct l3 l3; struct l2 l2; struct d2 d2;
	fill_i2(&i2, k);
	switch (which) {
	case 1: l3 = dr_big(k, k + 1, k + 2, k + 3, k + 4, i2); return sum_l3(&l3);
	case 2: l2 = dr_small(k, k + 1, k + 2, k + 3, k + 4, i2); return sum_l2(&l2);
	case 3: d2 = dr_var(4, (long)k, 0.5, k + 0.25, -1.0); return sum_d2(&d2);
	case 11: l3 = lr_big(k, k + 1, k + 2, k + 3, k + 4, i2); return sum_l3(&l3);
	case 12: l2 = lr_small(k, k + 1, k + 2, k + 3, k + 4, i2); return sum_l2(&l2);
	case 13: d2 = lr_var(4, (long)k, 0.5, k + 0.25, -1.0); return sum_d2(&d2);
	case 4: return dt_i3(dr_i3(k)) + dt_l3(dr_l3(k)) + 2 * dt_f3(dr_f3(k)) + 3 * dt_di(lr_di(k));
	}
	return 0;
}

/* registers run out: the structure goes on the stack, later scalars
   still take the registers left */
double lt_press1(long a, long b, long c, long d, long e, struct l2 s, long g) {
	return a + 2 * b + 3 * c + 4 * d + 5 * e + 7 * sum_l2(&s) + 11 * g;
}
double lt_press2(double a, double b, double c, double d, double e, double f, double g, struct d2 s, double h) {
	return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 11 * sum_d2(&s) + 13 * h;
}
double lt_press3(struct i2 a, struct id b, struct f2 c, struct l3 d, struct di e, struct i3 f, struct l2 g, struct fi h) {
	return sum_i2(&a) + 2 * sum_id(&b) + 3 * sum_f2(&c) + 5 * sum_l3(&d) + 7 * sum_di(&e)
		+ 11 * sum_i3(&f) + 13 * sum_l2(&g) + 17 * sum_fi(&h);
}
double lt_press4(int n, struct d2 a, struct d2 b, struct d2 c, struct d2 d, struct f1 e, float f) {
	return n + 2 * sum_d2(&a) + 3 * sum_d2(&b) + 5 * sum_d2(&c) + 7 * sum_d2(&d) + 11 * sum_f1(&e) + 13 * f;
}
double dt_press1(long, long, long, long, long, struct l2, long);
double dt_press2(double, double, double, double, double, double, double, struct d2, double);
double dt_press3(struct i2, struct id, struct f2, struct l3, struct di, struct i3, struct l2, struct fi);
double dt_press4(int, struct d2, struct d2, struct d2, struct d2, struct f1, float);
double lc_press(int k, int which) {
	struct l2 l2; struct d2 d2a, d2b, d2c, d2d; struct i2 i2; struct id id; struct f2 f2;
	struct l3 l3; struct di di; struct i3 i3; struct fi fi; struct f1 f1;
	fill_l2(&l2, k); fill_d2(&d2a, k); fill_d2(&d2b, k + 1); fill_d2(&d2c, k + 2); fill_d2(&d2d, k + 3);
	fill_i2(&i2, k); fill_id(&id, k); fill_f2(&f2, k); fill_l3(&l3, k); fill_di(&di, k);
	fill_i3(&i3, k); fill_fi(&fi, k); fill_f1(&f1, k);
	switch (which) {
	case 1: return dt_press1(k, k + 1, k + 2, k + 3, k + 4, l2, k + 6);
	case 2: return dt_press2(k, k + 1, k + 2, k + 3, k + 4, k + 5, k + 6, d2a, k + 8);
	case 3: return dt_press3(i2, id, f2, l3, di, i3, l2, fi);
	case 4: return dt_press4(k, d2a, d2b, d2c, d2d, f1, k + 0.5f);
	case 11: return lt_press1(k, k + 1, k + 2, k + 3, k + 4, l2, k + 6);
	case 12: return lt_press2(k, k + 1, k + 2, k + 3, k + 4, k + 5, k + 6, d2a, k + 8);
	case 13: return lt_press3(i2, id, f2, l3, di, i3, l2, fi);
	case 14: return lt_press4(k, d2a, d2b, d2c, d2d, f1, k + 0.5f);
	}
	return 0;
}

/* to a function with variable arguments, in abi-drv.c */
double dv(int n, ...);
double lc_var(int k) {
	struct i3 a; struct f3 b; struct di c; struct l3 d; struct d2 e; struct l2 f;
	fill_i3(&a, k); fill_f3(&b, k); fill_di(&c, k); fill_l3(&d, k); fill_d2(&e, k); fill_l2(&f, k);
	return dv(6, a, b, c, d, e, f, k + 0.5);
}

/* va_arg of structures: lvs here, dvs in abi-drv.c */
double lvs(const char *fmt, ...) {
	va_list ap; double s;
	va_start(ap, fmt);
	s = vsum(fmt, ap);
	va_end(ap);
	return s;
}
double dvs(const char *, ...);
double lc_vs(int k, int which, int self) {
	double (*f)(const char *, ...) = self ? lvs : dvs;
	struct i3 a; struct f3 b; struct di c; struct l3 d; struct d2 e; struct l2 f2;
	struct id g; struct fi h; struct c3 i; struct a13 j; struct nest kk; struct ffi l;
	struct sd m; struct f1 nn; struct d1 o; struct ic p;
	fill_i3(&a, k); fill_f3(&b, k); fill_di(&c, k); fill_l3(&d, k); fill_d2(&e, k); fill_l2(&f2, k);
	fill_id(&g, k); fill_fi(&h, k); fill_c3(&i, k); fill_a13(&j, k); fill_nest(&kk, k); fill_ffi(&l, k);
	fill_sd(&m, k); fill_f1(&nn, k); fill_d1(&o, k); fill_ic(&p, k);
	switch (which) {
	case 0: return f("abcdefghijklmnop", a, b, c, d, e, f2, g, h, i, j, kk, l, m, nn, o, p);
	case 1: return f("cccccccc", c, c, c, c, c, c, c, c);
	case 2: return f("eeeeee", e, e, e, e, e, e);
	case 3: return f("xxxxxxxxxebx", 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, e, b, 10.0);
	case 4: return f("yyyyycyf", 1L, 2L, 3L, 4L, 5L, c, 6L, f2);
	case 5: return f("yyyyfgy", 1L, 2L, 3L, 4L, f2, g, 7L);
	case 6: return f("kblkblkbl", kk, b, l, kk, b, l, kk, b, l);
	}
	return 0;
}

/* results through declarations without a prototype: the address of a
   result in memory is still a fixed argument */
struct l3 dk_l3();
struct i2 dk_i2();
struct l3 (*dkp)() = dk_l3;
double lcr_noproto(int k) {
	struct l3 a, c; struct i2 b;
	a = dk_l3(k); b = dk_i2(k); c = (*dkp)(k + 1);
	return sum_l3(&a) + 2 * sum_i2(&b) + 3 * sum_l3(&c);
}

/* through a declaration without a prototype */
double dt_i3(), dt_ffi(), dt_l3();
double lc_noproto(int k) {
	struct i3 a; struct ffi b; struct l3 c;
	fill_i3(&a, k); fill_ffi(&b, k); fill_l3(&c, k);
	return dt_i3(a) + 2 * dt_ffi(b) + 3 * dt_l3(c);
}
