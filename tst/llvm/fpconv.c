/*
 * Conversions with floating types: signed integers to and from float
 * and double, unsigned ones (which the front end builds from signed
 * conversions, rounding once), float to double and back, long double
 * to and from double and unsigned long, unary
 * minus on float (done in double), mixed expressions, and float
 * constants, which are float values when folded too.  Compiled by lcc;
 * fpconv-drv.c compares the bits.
 */
double i2d(int x) { return x; }
float i2f(int x) { return x; }
double l2d(long x) { return x; }
float l2f(long x) { return x; }
double c2d(char x) { return x; }
float s2f(short x) { return x; }
double u2d(unsigned x) { return x; }
float u2f(unsigned x) { return x; }
double ul2d(unsigned long x) { return x; }
float ul2f(unsigned long x) { return x; }

int d2i(double x) { return x; }
long d2l(double x) { return x; }
int f2i(float x) { return x; }
long f2l(float x) { return x; }
unsigned d2u(double x) { return x; }
unsigned long d2ul(double x) { return x; }
unsigned f2u(float x) { return x; }
unsigned long f2ul(float x) { return x; }
char d2c(double x) { return x; }

double f2d(float x) { return x; }
float d2f(double x) { return x; }
double viald(double x) { long double t; t = x; return t; }
double viald2(double x) { long double t; t = x; return t * 2; }
long double d2ld(double x) { return x; }
double ld2d(long double x) { return x; }
long double ul2ld(unsigned long x) { return x; }
unsigned long ld2ul(long double x) { return x; }
int fconst(void) {	/* folded by the compiler */
	return ((float)0.1 == 0.1f) | (0.1f == 0.1) << 1 | ((double)0.1f == 0.1) << 2
		| (16777217.0f == 16777216.0f) << 3 | (3.4028235e38f == (float)3.4028235e38) << 4
		| (1.00000006f == 1.0f) << 5 | (0.3f == 0.1f + 0.2f) << 6;
}
float fconst2(int i) {
	static float t[] = { 0.1f, 16777217.0f, 1.00000006f, 1e-45f, 3.4028235e38f, 0.1f + 0.2f };
	return t[i];
}
float fneg(float x) { return -x; }

double mix(float f, double d, int i, long l) { return f * d + i / 2 - l * f; }
float fmix(float f, int i) { return f * i + 0.5f; }
long round_down(double x) { return x < 0 ? (long)(x - 0.5) : (long)(x + 0.5); }
