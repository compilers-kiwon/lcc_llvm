/*
 * Structures that mix bit fields with other members, for bflayout.c and
 * bflayout-drv.c, which must lay them out as gcc and clang do: a bit
 * field shares its int with the members before it unless it would cross
 * an int boundary, a zero-width one moves to the next, and unnamed ones
 * align the structure only on ARM (AAPCS).  fill_NAME sets the named
 * members from k, sum_NAME combines them, INIT_NAME initializes one.
 */
struct s1 { char c; int b : 4; };
struct s2 { short h; unsigned u : 8; char d; };
struct s4 { char c; int : 0; char d; };
struct s5 { int a : 4; char c; int b : 4; };
struct s7 { char a, b, c; int x : 9; };
struct s10 { int a : 31; int b : 2; char c; };
struct s12 { double d; int a : 3; };
struct s13 { char c; unsigned u : 3; unsigned : 2; unsigned v : 5; short s; };
struct s14 { char c; int : 4; char d; };
union u1 { char c; int b : 4; };
#define SHAPES(X) X(s1) X(s2) X(s4) X(s5) X(s7) X(s10) X(s12) X(s13) X(s14) X(u1)

#define INIT_s1 { 'a', -3 }
#define INIT_s2 { -300, 200, 'd' }
#define INIT_s4 { 'x', 'y' }
#define INIT_s5 { 5, 'c', -6 }
#define INIT_s7 { 1, 2, 3, -200 }
#define INIT_s10 { -1000000, 1, 'z' }
#define INIT_s12 { 2.5, -2 }
#define INIT_s13 { 'q', 5, 30, -12345 }
#define INIT_s14 { 'm', 'n' }
#define INIT_u1 { 'u' }

static void fill_s1(struct s1 *p, int k) { p->c = 'A' + k; p->b = k % 8 - 4; }
static long sum_s1(struct s1 *p) { return p->c * 100L + p->b; }
static void fill_s2(struct s2 *p, int k) { p->h = -1000 * k; p->u = 200 + k; p->d = k; }
static long sum_s2(struct s2 *p) { return p->h * 1000L + p->u * 10 + p->d; }
static void fill_s4(struct s4 *p, int k) { p->c = k; p->d = -k; }
static long sum_s4(struct s4 *p) { return p->c * 1000L + p->d; }
static void fill_s5(struct s5 *p, int k) { p->a = k % 8; p->c = k * 3; p->b = -(k % 8); }
static long sum_s5(struct s5 *p) { return p->a * 10000L + p->c * 10 + p->b; }
static void fill_s7(struct s7 *p, int k) { p->a = k; p->b = k + 1; p->c = k + 2; p->x = k * 37 - 255; }
static long sum_s7(struct s7 *p) { return ((p->a * 100L + p->b) * 100 + p->c) * 1000 + p->x; }
static void fill_s10(struct s10 *p, int k) { p->a = k * 12345 - 1000000; p->b = k % 2 - 1; p->c = k; }
static long sum_s10(struct s10 *p) { return p->a * 10L + p->b * 3 + p->c; }
static void fill_s12(struct s12 *p, int k) { p->d = k * 0.5; p->a = k % 4 - 4; }
static long sum_s12(struct s12 *p) { return (long)(p->d * 10) + p->a; }
static void fill_s13(struct s13 *p, int k) { p->c = k; p->u = k % 8; p->v = k + 7; p->s = k * 1000; }
static long sum_s13(struct s13 *p) { return ((p->c * 10L + p->u) * 100 + p->v) * 100000 + p->s; }
static void fill_s14(struct s14 *p, int k) { p->c = k + 1; p->d = k + 2; }
static long sum_s14(struct s14 *p) { return p->c * 1000L + p->d; }
static void fill_u1(union u1 *p, int k) { p->b = k % 8 - 4; }
static long sum_u1(union u1 *p) { return p->b; }
