/*
 * Floating arithmetic in float and in double, kept apart because
 * conversions come later: + - * /, unary minus on double, a polynomial, sums
 * in loops, a global accumulator and shared subexpressions.  Compiled
 * by lcc; fparith-drv.c compares the bits of the results.
 */
double dadd(double a, double b) { return a + b; }
double dsub(double a, double b) { return a - b; }
double dmul(double a, double b) { return a * b; }
double ddiv(double a, double b) { return a / b; }
double dneg(double a) { return -a; }
float fadd(float a, float b) { return a + b; }
float fsub(float a, float b) { return a - b; }
float fmul(float a, float b) { return a * b; }
float fdiv(float a, float b) { return a / b; }
/* -a for a float goes through double (lcc's promote), so it is in fpconv */

double dpoly(double x) { return ((2.5 * x - 1.25) * x + 0.5) * x - 3.0; }
float fpoly(float x) { return ((2.5f * x - 1.25f) * x + 0.5f) * x - 3.0f; }
double dsum(double *a, int n) { int i; double s; s = 0.0; for (i = 0; i < n; i++) s = s + a[i]; return s; }
float fdot(float *a, float *b, int n) { int i; float s; s = 0.0f; for (i = 0; i < n; i++) s = s + a[i] * b[i]; return s; }

double g;
void acc(double x) { g = g * 0.5 + x; }
double shared(double a, double b) { double t; t = a + b; return t * t - (a - b) / (t + 1.0); }
