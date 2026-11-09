/*
 * Driver for ldouble.c: includes ldouble.c again with renamed
 * functions, compiled by clang, and compares the bytes that hold the
 * value of each long double.  Exits with 0 if every check passes.
 */
#define ldv r_ldv
#define lay r_lay
#define arith r_arith
#define compare r_compare
#define ldconst r_ldconst
#define ldfv r_ldfv
#define ldfold r_ldfold
#define ldfoldc r_ldfoldc
#define tolong r_tolong
#define toulong r_toulong
#define toint r_toint
#define tounsigned r_tounsigned
#define toshort r_toshort
#define tochar r_tochar
#define todouble r_todouble
#define tofloat r_tofloat
#define fromlong r_fromlong
#define fromulong r_fromulong
#define fromint r_fromint
#define fromunsigned r_fromunsigned
#define fromdouble r_fromdouble
#define fromfloat r_fromfloat
#define passld r_passld
#define fmt r_fmt
#define global r_global
#define vsum r_vsum
#define layout r_layout
#include "ldouble.c"
#undef ldv
#undef lay
#undef arith
#undef compare
#undef ldconst
#undef ldfv
#undef ldfold
#undef ldfoldc
#undef tolong
#undef toulong
#undef toint
#undef tounsigned
#undef toshort
#undef tochar
#undef todouble
#undef tofloat
#undef fromlong
#undef fromulong
#undef fromint
#undef fromunsigned
#undef fromdouble
#undef fromfloat
#undef passld
#undef fmt
#undef global
#undef vsum
#undef layout
long double arith(int, long double, long double), passld(long double, double, long double, int, long double);
int compare(long double, long double), fmt(char *, long double), layout(void);
long double ldconst(int), ldfold(int);
extern long double ldfv[];
int ldfoldc(void);
long tolong(long double); unsigned long toulong(long double); int toint(long double); unsigned tounsigned(long double);
short toshort(long double); signed char tochar(long double); double todouble(long double); float tofloat(long double);
long double fromlong(long), fromulong(unsigned long), fromint(int), fromunsigned(unsigned), fromdouble(double), fromfloat(float);
long double global(int), vsum(const char *, ...);
extern long double ldv[];
#undef NLD
#define NLD (sizeof r_ldv / sizeof r_ldv[0])
#undef NFOLD
#define NFOLD (sizeof r_ldfv / sizeof r_ldfv[0])

long double ldext(long double a, double b, long double c, int n, long double d) { return a - b * c + n / d; }

static int bad, n;
#define CHK(e, w, f) do { n++; if ((e) != (w)) { bad++; printf("%s: " f " != " f "\n", #e, e, w); } } while (0)
static void same(const char *what, long double a, long double b) {
	n++;
	if (memcmp(&a, &b, LDBYTES) != 0) { bad++; printf("%s: %.21Lg != %.21Lg\n", what, a, b); }
}
int main(void) {
	static long ls[] = { 0, 1, -1, LV(123456789012345678L, 1234567890L), LONG_MIN, LONG_MAX };
	static unsigned long us[] = { 0, 1, LV(0x8000000000000401UL, 0x80000081UL), ULONG_MAX, ULONG_MAX / 2 + 1 };
	static double ds[] = { 0.1, -2.5, 1e308, 4.9e-324, 0 };
	static float fs[] = { 0.1f, -3.5f, 3e38f, 1e-45f };
	char b1[300], b2[300];
	unsigned i, j;
	int op;
	long double nan = 0.0L / 0.0L, inf = 1.0L / 0.0L;
	CHK(layout(), r_layout(), "%d");
	for (i = 0; i < NCONST; i++)
		same("ldconst", ldconst(i), r_ldconst(i));
	for (i = 0; i < NFOLD; i++) {
		same("ldfv", ldfv[i], r_ldfv[i]);
		same("ldfold", ldfold(i), r_ldfold(i));
	}
	CHK(ldfoldc(), r_ldfoldc(), "%#x");
	for (i = 0; i < NLD; i++) {
		same("global", global(i), r_global(i));
		CHK(tolong(ldv[i]), r_tolong(ldv[i]), "%ld"); CHK(toulong(ldv[i]), r_toulong(ldv[i]), "%lu");
		CHK(toint(ldv[i]), r_toint(ldv[i]), "%d"); CHK(tounsigned(ldv[i]), r_tounsigned(ldv[i]), "%u");
		CHK(toshort(ldv[i]), r_toshort(ldv[i]), "%d"); CHK(tochar(ldv[i]), r_tochar(ldv[i]), "%d");
		{ double x = todouble(ldv[i]), y = r_todouble(ldv[i]); n++; if (memcmp(&x, &y, sizeof x)) { bad++; printf("todouble %u\n", i); } }
		{ float x = tofloat(ldv[i]), y = r_tofloat(ldv[i]); n++; if (memcmp(&x, &y, sizeof x)) { bad++; printf("tofloat %u\n", i); } }
		CHK(fmt(b1, ldv[i]), r_fmt(b2, ldv[i]), "%d"); n++; if (strcmp(b1, b2)) { bad++; printf("fmt: %s != %s\n", b1, b2); }
		for (j = 0; j < NLD; j++) {
			for (op = 0; op < 6; op++)
				same("arith", arith(op, ldv[i], ldv[j]), r_arith(op, ldv[i], ldv[j]));
			CHK(compare(ldv[i], ldv[j]), r_compare(ldv[i], ldv[j]), "%d");
			same("passld", passld(ldv[i], 0.5, ldv[j], (int)i, ldv[(i + j) % NLD]), r_passld(ldv[i], 0.5, ldv[j], (int)i, ldv[(i + j) % NLD]));
		}
		CHK(compare(ldv[i], nan), r_compare(ldv[i], nan), "%d"); CHK(compare(nan, ldv[i]), r_compare(nan, ldv[i]), "%d");
		CHK(compare(ldv[i], inf), r_compare(ldv[i], inf), "%d");
		same("arith nan", arith(0, ldv[i], nan), r_arith(0, ldv[i], nan));
	}
	for (i = 0; i < sizeof ls / sizeof ls[0]; i++) { same("fromlong", fromlong(ls[i]), r_fromlong(ls[i])); same("fromint", fromint((int)ls[i]), r_fromint((int)ls[i])); same("fromunsigned", fromunsigned((unsigned)ls[i]), r_fromunsigned((unsigned)ls[i])); }
	for (i = 0; i < sizeof us / sizeof us[0]; i++) same("fromulong", fromulong(us[i]), r_fromulong(us[i]));
	for (i = 0; i < sizeof ds / sizeof ds[0]; i++) same("fromdouble", fromdouble(ds[i]), r_fromdouble(ds[i]));
	for (i = 0; i < sizeof fs / sizeof fs[0]; i++) same("fromfloat", fromfloat(fs[i]), r_fromfloat(fs[i]));
#define VS(args) same("vsum", vsum args, r_vsum args)
	VS(("l", 0.1L));
	VS(("ildli", 1, 0.1L, 2.5, LDV(1e4000L, 1.5e308L), 7));
	VS(("ddddddddddlldl", 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 1.0L/3, -0.1L, 0.5, LDV(1e-4000L, 1e-300L)));
	VS(("iiiiiiilil", 1, 2, 3, 4, 5, 6, 7, 2.5L, 8, -2.5L));
	VS(("ilililil", 1, 0.1L, 2, 0.2L, 3, 0.3L, 4, 0.4L));
	printf("%d checks, %d bad\n", n, bad);
	return bad != 0;
}
