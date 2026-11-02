/*
 * Structures for the SysV x86-64 ABI tests in abi.c and abi-drv.c, with
 * functions that fill them from k and sum their members (not their
 * padding).  SHAPES lists them.
 */
struct c1 { char a; };
struct c3 { char a, b, c; };
struct s2 { short a; char b; };
struct i1 { int a; };
struct i2 { int a, b; };
struct ic { int a; char b; };
struct i3 { int a, b, c; };
struct l2 { long a, b; };
struct l3 { long a, b, c; };
struct f1 { float a; };
struct f2 { float a, b; };
struct f3 { float a, b, c; };
struct d1 { double a; };
struct d2 { double a, b; };
struct fi { float a; int b; };
struct di { double a; int b; };
struct id { int a; double b; };
struct ffi { float a, b; int c; };
struct cd { char a; double b; };
union ul { double d; long l; };
union uf { float f[2]; double d; };
struct a16 { char s[16]; };
struct a13 { char s[13]; };
struct fa3 { float a[3]; };
struct nest { struct f2 p; int k; };
struct bits { unsigned a:3, b:20; int c:5; };
struct da3 { double a[3]; };
struct pl { char *p; long n; };
struct sd { short a; double b; short c; };

#define SHAPES(X) X(c1) X(c3) X(s2) X(i1) X(i2) X(ic) X(i3) X(l2) X(l3) \
	X(f1) X(f2) X(f3) X(d1) X(d2) X(fi) X(di) X(id) X(ffi) X(cd) \
	X(a16) X(a13) X(fa3) X(nest) X(bits) X(da3) X(pl) X(sd)
#define USHAPES(X) X(ul) X(uf)

static char abitext[] = "abcdefghijklmnopqrstuvwxyz";

static void fill_c1(struct c1 *p, int k) { p->a = k + 1; }
static double sum_c1(struct c1 *p) { return p->a; }
static void fill_c3(struct c3 *p, int k) { p->a = k + 1; p->b = -k; p->c = 2 * k; }
static double sum_c3(struct c3 *p) { return p->a + 10.0 * p->b + 100.0 * p->c; }
static void fill_s2(struct s2 *p, int k) { p->a = -300 * k; p->b = k + 7; }
static double sum_s2(struct s2 *p) { return p->a + 10.0 * p->b; }
static void fill_i1(struct i1 *p, int k) { p->a = 100000 * k - 3; }
static double sum_i1(struct i1 *p) { return p->a; }
static void fill_i2(struct i2 *p, int k) { p->a = k - 5; p->b = -70000 * k; }
static double sum_i2(struct i2 *p) { return p->a + 10.0 * p->b; }
static void fill_ic(struct ic *p, int k) { p->a = 123456 * k; p->b = 3 - k; }
static double sum_ic(struct ic *p) { return p->a + 10.0 * p->b; }
static void fill_i3(struct i3 *p, int k) { p->a = k; p->b = -2 * k - 1; p->c = 99999 * k; }
static double sum_i3(struct i3 *p) { return p->a + 10.0 * p->b + 100.0 * p->c; }
static void fill_l2(struct l2 *p, int k) { p->a = 0x100000001L * k; p->b = -7L * k - 1; }
static double sum_l2(struct l2 *p) { return p->a + 10.0 * p->b; }
static void fill_l3(struct l3 *p, int k) { p->a = k; p->b = -0x200000000L * k; p->c = 5 - k; }
static double sum_l3(struct l3 *p) { return p->a + 10.0 * p->b + 100.0 * p->c; }
static void fill_f1(struct f1 *p, int k) { p->a = k + 0.5f; }
static double sum_f1(struct f1 *p) { return p->a; }
static void fill_f2(struct f2 *p, int k) { p->a = k * 0.25f; p->b = -k - 0.75f; }
static double sum_f2(struct f2 *p) { return p->a + 10.0 * p->b; }
static void fill_f3(struct f3 *p, int k) { p->a = k; p->b = k * 1.5f; p->c = -k * 0.125f; }
static double sum_f3(struct f3 *p) { return p->a + 10.0 * p->b + 100.0 * p->c; }
static void fill_d1(struct d1 *p, int k) { p->a = k * 1e10 + 0.5; }
static double sum_d1(struct d1 *p) { return p->a; }
static void fill_d2(struct d2 *p, int k) { p->a = k * 3.25; p->b = 1.0 / (k + 4); }
static double sum_d2(struct d2 *p) { return p->a + 10.0 * p->b; }
static void fill_fi(struct fi *p, int k) { p->a = k * 2.5f; p->b = -k * 1000; }
static double sum_fi(struct fi *p) { return p->a + 10.0 * p->b; }
static void fill_di(struct di *p, int k) { p->a = k - 0.0625; p->b = k * 77; }
static double sum_di(struct di *p) { return p->a + 10.0 * p->b; }
static void fill_id(struct id *p, int k) { p->a = 9 - k; p->b = k * 1e-3; }
static double sum_id(struct id *p) { return p->a + 10.0 * p->b; }
static void fill_ffi(struct ffi *p, int k) { p->a = k; p->b = 0.5f - k; p->c = k * k; }
static double sum_ffi(struct ffi *p) { return p->a + 10.0 * p->b + 100.0 * p->c; }
static void fill_cd(struct cd *p, int k) { p->a = 'A' + k; p->b = -k * 0.5; }
static double sum_cd(struct cd *p) { return p->a + 10.0 * p->b; }
static void fill_ul(union ul *p, int k) { p->l = -0x123456789L * k; }
static double sum_ul(union ul *p) { return p->l; }
static void fill_uf(union uf *p, int k) { p->f[0] = k + 0.25f; p->f[1] = -k * 4.0f; }
static double sum_uf(union uf *p) { return p->f[0] + 10.0 * p->f[1]; }
static void fill_a16(struct a16 *p, int k) { int i; for (i = 0; i < 16; i++) p->s[i] = k + i * 3; }
static double sum_a16(struct a16 *p) { double s = 0; int i; for (i = 0; i < 16; i++) s = s * 2 + p->s[i]; return s; }
static void fill_a13(struct a13 *p, int k) { int i; for (i = 0; i < 13; i++) p->s[i] = k * 5 - i; }
static double sum_a13(struct a13 *p) { double s = 0; int i; for (i = 0; i < 13; i++) s = s * 2 + p->s[i]; return s; }
static void fill_fa3(struct fa3 *p, int k) { p->a[0] = k; p->a[1] = k + 0.5f; p->a[2] = -k; }
static double sum_fa3(struct fa3 *p) { return p->a[0] + 10.0 * p->a[1] + 100.0 * p->a[2]; }
static void fill_nest(struct nest *p, int k) { fill_f2(&p->p, k); p->k = k * 11; }
static double sum_nest(struct nest *p) { return sum_f2(&p->p) + 1000.0 * p->k; }
static void fill_bits(struct bits *p, int k) { p->a = k; p->b = 1000 * k; p->c = -k; }
static double sum_bits(struct bits *p) { return p->a + 10.0 * p->b + 100.0 * p->c; }
static void fill_da3(struct da3 *p, int k) { p->a[0] = k; p->a[1] = k * 0.5; p->a[2] = -k; }
static double sum_da3(struct da3 *p) { return p->a[0] + 10.0 * p->a[1] + 100.0 * p->a[2]; }
static void fill_pl(struct pl *p, int k) { p->p = abitext + (k & 15); p->n = k * 1000L; }
static double sum_pl(struct pl *p) { return p->p[0] + 10.0 * p->p[1] + p->n; }
static void fill_sd(struct sd *p, int k) { p->a = k; p->b = k * 0.75; p->c = -k; }
static double sum_sd(struct sd *p) { return p->a + 10.0 * p->b + 100.0 * p->c; }
