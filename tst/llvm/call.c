/*
 * Calls to functions with prototypes: recursion, mutual recursion, more
 * arguments than argument registers, mixed argument types, calls through
 * function pointers, calls as arguments and in conditions, char and
 * short parameters and results, and calls to functions compiled by clang
 * (in call-drv.c).  Compiled by lcc; call-drv.c checks the results.
 */
long ext_add(long, long);		/* in call-drv.c */
signed char ext_narrow(int);		/* in call-drv.c */
int ext_cb(int (*)(int), int);		/* in call-drv.c */

int fib(int n) { if (n < 2) return n; return fib(n - 1) + fib(n - 2); }
int odd(int);
int even(int n) { if (n == 0) return 1; return odd(n - 1); }
int odd(int n) { if (n == 0) return 0; return even(n - 1); }
long ack(long m, long n) {
	if (m == 0) return n + 1;
	if (n == 0) return ack(m - 1, 1);
	return ack(m - 1, ack(m, n - 1));
}

long many(int a, long b, char *c, int d, long e, int f, int g, long h, int i, long j) {
	return a + b * 2 + c[1] + d * 3 + e * 5 + f * 7 + g * 11 + h * 13 + i * 17 + j * 19;
}
long callmany(char *s) { return many(1, 2, s, 4, 5, 6, 7, 8, 9, 10) - many(-1, -2, s + 1, -4, -5, -6, -7, -8, -9, -10); }

/* floating values are only passed and returned: no conversions yet */
double dpass(double x) { return x; }
double dpick(int i, double a, float b, double c) { if (i == 0) return a; return c; }
float fpass(float x) { return x; }
float fpick(int i, float a, double b, float c) { if (i == 0) return a; return c; }
double dcall(double x) { return dpick(2, dpass(1.5), fpass(2.5f), dpass(x)); }

int add1(int x) { return x + 1; }
int twice(int x) { return x * 2; }
int sq(int x) { return x * x; }
int (*ops[3])(int) = { add1, twice, sq };
int apply(int k, int x) { return ops[k](x); }
int compose(int (*f)(int), int (*g)(int), int x) { return f(g(x)); }
int nested(int x) { return add1(twice(sq(x))) + sq(add1(x)) * twice(x); }

int counter;
void bump(int by) { counter = counter + by; }
void bumpn(int n) { int i; for (i = 0; i < n; i++) bump(i); }
int firstpos(int *a, int n) { int i; for (i = 0; i < n; i++) if (sq(a[i]) > 50 && add1(a[i]) > 0) return i; return -1; }

char lowc(char c) { if (c >= 'A' && c <= 'Z') return c + 32; return c; }
unsigned short ushort_of(long x) { return x; }
int narrowsum(char c, short s, unsigned char uc) { return lowc(c) + s + uc + ushort_of(-1); }

long useext(long a, long b) { return ext_add(a, b) + ext_add(ext_add(a, 1), b); }
int usenarrow(int x) { return ext_narrow(x) + 1; }
int usecb(int x) { return ext_cb(sq, x) + ext_cb(add1, x); }
