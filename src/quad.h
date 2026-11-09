/*
 * IEEE 754 binary128 (quad) arithmetic in software, for the constants of
 * targets whose long double is quad, which the host's long double may
 * not hold; see quad.c.  Results are rounded to nearest, ties to even.
 */
typedef struct {
	unsigned short h[8];	/* 16 bits each; h[0] has the sign and the exponent */
} Quad;

extern Quad q_fromstr(const char *s);
extern Quad q_fromld(long double);
extern Quad q_fromlong(long);
extern Quad q_fromulong(unsigned long);
extern long double q_told(Quad);
extern double q_todouble(Quad);
extern float q_tofloat(Quad);
extern long q_tolong(Quad);
extern Quad q_add(Quad, Quad);
extern Quad q_sub(Quad, Quad);
extern Quad q_mul(Quad, Quad);
extern Quad q_div(Quad, Quad);
extern Quad q_neg(Quad);
extern int q_cmp(Quad, Quad);	/* -1, 0, 1, or 2 if unordered */
extern int q_iszero(Quad);
extern int q_isinf(Quad);
extern int q_isnan(Quad);
extern Quad q_max(void);
