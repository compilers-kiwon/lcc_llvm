/*
 * Floating comparisons, with NaNs, signed zeros, infinities and
 * subnormals, in every kind of context: as values, in if, while, do and
 * for, under && || ! and ?:.  The front end inverts a comparison to
 * jump on false, and an inverted < <= > >= must still be false for a
 * NaN.  Compiled by lcc; fpcmp-drv.c checks the results.
 */
#define VAL(T, s) \
	int eq##s(T a, T b) { return a == b; } \
	int ne##s(T a, T b) { return a != b; } \
	int lt##s(T a, T b) { return a < b; } \
	int le##s(T a, T b) { return a <= b; } \
	int gt##s(T a, T b) { return a > b; } \
	int ge##s(T a, T b) { return a >= b; }
VAL(double, _d)
VAL(float, _f)

int if_lt(double a, double b) { if (a < b) return 1; return 0; }
int if_ge(double a, double b) { if (a >= b) return 1; else return 0; }
int if_not_le(double a, double b) { if (!(a <= b)) return 1; return 0; }
int and_gt(double a, double b, double c) { return a > b && b > c; }
int or_lt(float a, float b, float c) { return a < b || b < c; }
int cond_le(double a, double b) { return a <= b ? 10 : 20; }
int not_d(double a) { return !a; }
int truth(double a) { if (a) return 1; return 0; }
int while_lt(double a, double b) { int n; n = 0; while (a < b && n < 10) { a = a + 1.0; n++; } return n; }
int do_gt(double a, double b) { int n; n = 0; do n++; while (a > b && n < 5); return n; }
int for_le(double a, double b) { int n; for (n = 0; a <= b && n < 7; n++) b = b - 1.0; return n; }
