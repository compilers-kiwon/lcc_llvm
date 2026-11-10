/*
 * Enumerated types as gcc and clang make them: an enum without negative
 * constants is compatible with unsigned int and its arithmetic is
 * unsigned; one with a negative constant is compatible with int; the
 * constants themselves are ints.  So a function may be declared with
 * the enum and defined with its integer type, pointers to the two mix
 * without casts (gcc.c-torture enum-3.c), and an enum parameter of an
 * old-style definition or of a function also declared without a
 * prototype keeps that type.  Compiled by lcc; enum-drv.c includes this
 * file again with F(n) renaming the functions, compiled by clang, and
 * compares the results.
 */
#ifndef F
#define F(n) n
#endif

enum u { U0, U1, U5 = 5 };
enum s { SM = -3, S0 = 0, S1 };
enum m { MMIN = -2147483647 - 1, MMAX = 2147483647 };

/* compatible declarations: the enum and its integer type */
unsigned F(ucompat)(enum u);
enum u F(ucompat)(unsigned);
int F(scompat)(enum s);
enum s F(scompat)(int);
extern enum u F(uvar);
extern unsigned F(uvar);
enum u F(uvar) = U5;

unsigned F(ucompat)(enum u x) { return x + 2; }
enum s F(scompat)(int x) { return x - 2; }

/* default argument promotions keep an enum's integer type */
long F(oldu)(enum u);
long F(oldu)(x) enum u x; { return x - 1; }
int F(krdecl)();
int F(krdecl)(enum u);
int F(krdecl)(enum u x) { return x - 1 > 1; }

/* arithmetic in the enum's integer type */
long F(usub)(enum u x, int k) { return x - k; }
int F(ugt)(enum u x) { return x - 1 > 10; }
long F(udiv)(enum u x) { return (x - 3) / 2; }
long F(umod)(enum u x) { return (x - 3) % 7; }
long F(ushr)(enum u x) { return (x - 8) >> 1; }
double F(udbl)(enum u x) { return x - 1; }
long F(uwide)(enum u x) { return x + -1L; }
int F(ucmp)(enum u x, int i) { return x < i; }
int F(ucmpc)(enum u x) { return x < (enum u)-1; }
long F(sdiv)(enum s x) { return (x - 3) / 2; }
long F(sshr)(enum s x) { return (x - 8) >> 1; }
double F(sdbl)(enum s x) { return x - 1; }
int F(scmp)(enum s x, int i) { return x < i; }
int F(mlt)(enum m x) { return x < 0; }
long F(ucast)(int i) { return (enum u)i; }
long F(scast)(int i) { return (enum s)i; }
long F(cnst)(void) { return U0 - 1; }	/* constants are int */

/* switch on an enum value; the selector is unsigned */
int F(usw)(enum u x) {
	switch (x - 1) {
	case 0: return 10;
	case 4: return 14;
	case -1: return 9;	/* converted to unsigned: 0 - 1 */
	default: return -1;
	}
}

/* pointers to the enum and to its integer type */
unsigned *F(uptr)(enum u *p) { return p; }
enum u *F(uptr2)(unsigned *p) { return p; }
int *F(sptr)(enum s *p) { return p; }
int F(scond)(enum s *q, int *p, int c) { return *(c ? q : p) > 0; }
int F(ucond)(enum u *q, unsigned *p, int c) { return *(c ? p : q) > 0; }
long F(mcond)(enum m *q, int *p, int c) { return *(c ? q : p) / 2; }
unsigned F(uderef)(enum u *p) { *p = *p + 1; return *p; }
