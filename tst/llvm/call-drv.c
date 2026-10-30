/*
 * Driver for call.c, which also calls ext_add, ext_narrow and ext_cb
 * below.  Exits with 0 if every check passes.
 */
#include <stdio.h>
int fib(int), even(int), odd(int); long ack(long, long);
long many(int, long, char *, int, long, int, int, long, int, long), callmany(char *);
double dpass(double), dpick(int, double, float, double), dcall(double); float fpass(float), fpick(int, float, double, float);
int apply(int, int), compose(int (*)(int), int (*)(int), int), nested(int), add1(int), twice(int), sq(int);
extern int counter; void bump(int), bumpn(int); int firstpos(int *, int);
char lowc(char); unsigned short ushort_of(long); int narrowsum(char, short, unsigned char);
long useext(long, long); int usenarrow(int); int usecb(int);
long ext_add(long a, long b) { return a + b; }
signed char ext_narrow(int x) { return (signed char)x; }
int ext_cb(int (*f)(int), int x) { return f(x) * 10; }
static int bad, n;
#define CHK(e, w, fmt) do { n++; if ((e) != (w)) { bad++; printf("%s: " fmt " != " fmt "\n", #e, e, w); } } while (0)
static int rfib(int n) { return n < 2 ? n : rfib(n - 1) + rfib(n - 2); }
static long rack(long m, long n) { return m == 0 ? n + 1 : n == 0 ? rack(m - 1, 1) : rack(m - 1, rack(m, n - 1)); }
static long rmany(int a, long b, char *c, int d, long e, int f, int g, long h, int i, long j) {
	return a + b * 2 + c[1] + d * 3 + e * 5 + f * 7 + g * 11 + h * 13 + i * 17 + j * 19; }
static int rsq(int x) { return x * x; } static int radd1(int x) { return x + 1; } static int rtwice(int x) { return x * 2; }
int main(void) {
	static char s[] = "xyz";
	int a[6] = { 1, -9, 3, 8, -20, 7 }, i, sum = 0;
	for (i = 0; i < 20; i++) CHK(fib(i), rfib(i), "%d");
	for (i = 0; i < 30; i++) { CHK(even(i), i % 2 == 0, "%d"); CHK(odd(i), i % 2 == 1, "%d"); }
	CHK(ack(2, 3), rack(2, 3), "%ld"); CHK(ack(3, 3), rack(3, 3), "%ld");
	CHK(many(1, 2, s, 4, 5, 6, 7, 8, 9, 10), rmany(1, 2, s, 4, 5, 6, 7, 8, 9, 10), "%ld");
	CHK(callmany(s), rmany(1, 2, s, 4, 5, 6, 7, 8, 9, 10) - rmany(-1, -2, s + 1, -4, -5, -6, -7, -8, -9, -10), "%ld");
	CHK(dpass(3.25), 3.25, "%g"); CHK(fpass(0.75f), 0.75f, "%g");
	CHK(dpick(0, 1.0, 2.0f, 3.0), 1.0, "%g"); CHK(dpick(2, 1.0, 2.0f, -3.5), -3.5, "%g");
	CHK(fpick(0, 1.5f, 2.0, 3.5f), 1.5f, "%g"); CHK(fpick(1, 1.5f, 2.0, -3.25f), -3.25f, "%g");
	CHK(dcall(9.125), 9.125, "%g");
	for (i = -5; i <= 5; i++) {
		CHK(apply(0, i), i + 1, "%d"); CHK(apply(1, i), i * 2, "%d"); CHK(apply(2, i), i * i, "%d");
		CHK(compose(add1, sq, i), i * i + 1, "%d"); CHK(compose(twice, add1, i), (i + 1) * 2, "%d");
		CHK(nested(i), radd1(rtwice(rsq(i))) + rsq(radd1(i)) * rtwice(i), "%d");
	}
	bump(5); bumpn(10); for (i = 0; i < 10; i++) sum += i;
	CHK(counter, 5 + sum, "%d");
	CHK(firstpos(a, 6), 3, "%d"); CHK(firstpos(a, 3), -1, "%d");
	CHK(lowc('Q'), 'q', "%d"); CHK(lowc('q'), 'q', "%d"); CHK(lowc('!'), '!', "%d");
	CHK(ushort_of(-1), 65535, "%d"); CHK(ushort_of(0x12345), 0x2345, "%d");
	CHK(narrowsum('A', -300, 200), 'a' - 300 + 200 + 65535, "%d");
	CHK(useext(40, 2), 42 + 43, "%ld");
	for (i = -300; i <= 300; i += 7) CHK(usenarrow(i), (signed char)i + 1, "%d");
	for (i = -4; i <= 4; i++) CHK(usecb(i), i * i * 10 + (i + 1) * 10, "%d");
	printf("%d checks, %d bad\n", n, bad);
	return bad != 0;
}
