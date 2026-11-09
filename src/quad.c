#include <stdlib.h>
#include <string.h>
#include <float.h>
#include "quad.h"

static char rcsid[] = "$Id$";

/*
 * IEEE 754 binary128 in software, with nothing of the host's floating
 * point but its long double for q_fromld and q_told, and for building
 * exact values in q_todouble and q_tofloat.  Numbers are worked on as
 * bignums: arrays of 16-bit digits in unsigned longs, the least
 * significant first, with value m*2**e for an integer m.  A quad has a
 * 113-bit significand, the leading 1 implicit unless it is subnormal,
 * and a 15-bit exponent biased by 16383.
 */

#define P 113			/* bits of precision */
#define EMIN (-16382)		/* least exponent of a normal number */
#define EMAX 16383		/* greatest exponent */
#define BIAS 16383

enum { QZERO, QNUM, QINF, QNAN };

/* an unpacked quad: m has 8 digits, of which 113 bits; value m*2**e */
typedef struct {
	int sign, cls;
	long e;
	unsigned long m[8];
} Unpacked;

/* biglen - the number of significant bits of a[0..n-1] */
static long biglen(const unsigned long *a, int n) {
	unsigned long d;
	long b;

	while (n > 0 && a[n-1] == 0)
		n--;
	if (n == 0)
		return 0;
	for (b = 16*(long)(n - 1), d = a[n-1]; d; d >>= 1)
		b++;
	return b;
}

/* bigbit - bit i of a[0..n-1] */
static int bigbit(const unsigned long *a, int n, long i) {
	return i >= 0 && i/16 < n ? (int)(a[i/16]>>(i%16)&1) : 0;
}

/* anybelow - whether a[0..n-1] has a 1 bit below bit i */
static int anybelow(const unsigned long *a, int n, long i) {
	long k;

	for (k = 0; k < n && 16*k + 16 <= i; k++)
		if (a[k])
			return 1;
	if (k < n && i > 16*k && (a[k] & ((1UL<<(i - 16*k)) - 1)))
		return 1;
	return 0;
}

/* bigmul - a[0..n-1] = a*m + c, m and c below 2**16; a has room for n+1
   digits; return the new number of digits */
static int bigmul(unsigned long *a, int n, unsigned long m, unsigned long c) {
	int i;

	for (i = 0; i < n; i++) {
		unsigned long t = a[i]*m + c;
		a[i] = t&0xFFFF;
		c = t>>16;
	}
	if (c)
		a[n++] = c;
	return n;
}

/* bigshl - shift a[0..n-1] left by k bits into r, which has room; return
   the number of digits of r */
static int bigshl(unsigned long *r, const unsigned long *a, int n, long k) {
	int q = k/16, s = k%16, i;

	for (i = n + q; i >= 0; i--) {
		unsigned long hi = i - q >= 0 && i - q < n ? a[i-q] : 0;
		unsigned long lo = s && i - q - 1 >= 0 && i - q - 1 < n ? a[i-q-1] : 0;
		r[i] = (hi<<s | lo>>(16 - s))&0xFFFF;
	}
	return n + q + 1;
}

/* bigcmp - compare a[0..na-1] and b[0..nb-1] */
static int bigcmp(const unsigned long *a, int na, const unsigned long *b, int nb) {
	int i;

	for (i = (na > nb ? na : nb) - 1; i >= 0; i--) {
		unsigned long x = i < na ? a[i] : 0, y = i < nb ? b[i] : 0;
		if (x != y)
			return x < y ? -1 : 1;
	}
	return 0;
}

/* bigsub - a[0..na-1] -= b[0..nb-1], which is not greater */
static void bigsub(unsigned long *a, int na, const unsigned long *b, int nb) {
	unsigned long borrow = 0;
	int i;

	for (i = 0; i < na; i++) {
		unsigned long y = (i < nb ? b[i] : 0) + borrow;
		if (a[i] >= y) {
			a[i] -= y;
			borrow = 0;
		} else {
			a[i] = a[i] + 0x10000 - y;
			borrow = 1;
		}
	}
}

/* bigdiv - q[0..nq-1] = a/b, b not 0; return whether the remainder is not
   0.  q has nq digits, enough for the quotient. */
static int bigdiv(unsigned long *q, int nq, const unsigned long *a, int na,
	const unsigned long *b, int nb) {
	long la = biglen(a, na), lb = biglen(b, nb), i;
	int nr = nb + 1, k;
	unsigned long *r = malloc((nr + 1)*sizeof *r);

	memset(q, 0, nq*sizeof *q);
	memset(r, 0, (nr + 1)*sizeof *r);
	/* the remainder starts with the top lb - 1 bits of a */
	for (i = la - 1; i >= 0 && i >= la - lb + 1; i--)
		if (bigbit(a, na, i))
			r[(i - (la - lb + 1))/16] |= 1UL<<((i - (la - lb + 1))%16);
	for (i = la - lb; i >= 0; i--) {
		unsigned long c = bigbit(a, na, i);
		for (k = 0; k < nr; k++) {	/* r = 2r + bit i */
			unsigned long t = r[k]<<1 | c;
			r[k] = t&0xFFFF;
			c = t>>16;
		}
		if (bigcmp(r, nr, b, nb) >= 0) {
			bigsub(r, nr, b, nb);
			q[i/16] |= 1UL<<(i%16);
		}
	}
	for (k = 0; k < nr && r[k] == 0; k++)
		;
	free(r);
	return k < nr;
}

/*
 * roundbits - round m[0..n-1]*2**e, below which bits that are not all 0
 * were lost if sticky, to p bits for exponents emin..emax of the
 * leading bit, with fewer bits below emin: set r[0..7] and *re to the
 * result r*2**re and return 0, or return 1 if it overflows.
 */
static int roundbits(const unsigned long *m, int n, long e, int sticky,
	int p, long emin, long emax, unsigned long r[8], long *re) {
	long len = biglen(m, n), lead, prec, drop, i;
	int rbit;

	memset(r, 0, 8*sizeof *r);
	*re = 0;
	if (len == 0)
		return 0;
	lead = e + len - 1;
	prec = lead < emin ? p - (emin - lead) : p;
	drop = len - prec;
	if (drop <= 0) {	/* exact */
		for (i = 0; i < len; i++)
			if (bigbit(m, n, i))
				r[(i - drop)/16] |= 1UL<<((i - drop)%16);
		*re = e + drop;
	} else {
		rbit = bigbit(m, n, drop - 1);
		sticky = sticky || anybelow(m, n, drop - 1);
		for (i = drop; i < len; i++)
			if (bigbit(m, n, i))
				r[(i - drop)/16] |= 1UL<<((i - drop)%16);
		*re = e + drop;
		if (rbit && (sticky || (r[0]&1))) {
			unsigned long c = 1;
			for (i = 0; i < 8 && c; i++) {
				r[i] += c;
				c = r[i]>>16;
				r[i] &= 0xFFFF;
			}
			if (biglen(r, 8) > p) {	/* 2**p: one bit fewer */
				for (i = 0; i < 8; i++)
					r[i] = (r[i]>>1 | (i < 7 ? r[i+1]<<15 : 0))&0xFFFF;
				(*re)++;
			}
		}
	}
	len = biglen(r, 8);
	return len > 0 && *re + len - 1 > emax;
}

/* pack - the quad of sign and m*2**e, rounded */
static Quad pack(int sign, const unsigned long *m, int n, long e, int sticky) {
	unsigned long r[8];
	long re, lead;
	Quad q;
	int i;

	memset(&q, 0, sizeof q);
	if (roundbits(m, n, e, sticky, P, EMIN, EMAX, r, &re)) {
		q.h[0] = sign<<15 | 0x7FFF;
		return q;
	}
	if (biglen(r, 8) > 0) {
		lead = re + biglen(r, 8) - 1;
		if (lead >= EMIN)	/* normal: the exponent replaces the leading 1 */
			r[7] = lead + BIAS;
		/* else subnormal; r[7] is 1 if rounding made the least normal */
	}
	for (i = 0; i < 8; i++)
		q.h[i] = r[7-i];
	q.h[0] |= sign<<15;
	return q;
}

static Unpacked unpack(Quad q) {
	Unpacked u;
	int i, x = q.h[0]&0x7FFF;

	u.sign = q.h[0]>>15;
	for (i = 0; i < 7; i++)
		u.m[i] = q.h[7-i];
	u.m[7] = 0;
	if (x == 0x7FFF) {
		u.cls = biglen(u.m, 7) ? QNAN : QINF;
		u.e = 0;
	} else if (x == 0) {
		u.cls = biglen(u.m, 7) ? QNUM : QZERO;
		u.e = EMIN - (P - 1);
	} else {
		u.cls = QNUM;
		u.m[7] = 1;
		u.e = x - BIAS - (P - 1);
	}
	return u;
}

static Quad special(int sign, int cls) {
	Quad q;

	memset(&q, 0, sizeof q);
	if (cls == QINF)
		q.h[0] = 0x7FFF;
	else if (cls == QNAN) {
		q.h[0] = 0x7FFF;
		q.h[1] = 0x8000;
	}
	q.h[0] |= sign<<15;
	return q;
}

int q_isnan(Quad q) {
	return unpack(q).cls == QNAN;
}

int q_isinf(Quad q) {
	return unpack(q).cls == QINF;
}

int q_iszero(Quad q) {
	return unpack(q).cls == QZERO;
}

Quad q_neg(Quad q) {
	q.h[0] ^= 0x8000;
	return q;
}

Quad q_max(void) {
	Quad q;
	int i;

	q.h[0] = 0x7FFE;
	for (i = 1; i < 8; i++)
		q.h[i] = 0xFFFF;
	return q;
}

int q_cmp(Quad a, Quad b) {
	Unpacked x = unpack(a), y = unpack(b);
	int i, c = 0;

	if (x.cls == QNAN || y.cls == QNAN)
		return 2;
	if (x.cls == QZERO && y.cls == QZERO)
		return 0;
	if (x.cls == QZERO)
		return y.sign ? 1 : -1;
	if (y.cls == QZERO)
		return x.sign ? -1 : 1;
	if (x.sign != y.sign)
		return x.sign ? -1 : 1;
	for (i = 0; i < 8 && c == 0; i++)	/* the magnitudes order as their bits */
		if ((a.h[i]&(i ? 0xFFFF : 0x7FFF)) != (b.h[i]&(i ? 0xFFFF : 0x7FFF)))
			c = (a.h[i]&(i ? 0xFFFF : 0x7FFF)) < (b.h[i]&(i ? 0xFFFF : 0x7FFF)) ? -1 : 1;
	return x.sign ? -c : c;
}

/* addmag - a + b or a - b for unpacked numbers, sub saying which */
static Quad addsub(Unpacked a, Unpacked b, int sub) {
	unsigned long x[32], y[32];
	int nx, ny, sign;
	long e;

	if (sub)
		b.sign ^= 1;
	if (a.cls == QNAN || b.cls == QNAN)
		return special(0, QNAN);
	if (a.cls == QINF && b.cls == QINF)
		return a.sign == b.sign ? special(a.sign, QINF) : special(0, QNAN);
	if (a.cls == QINF || b.cls == QINF)
		return special(a.cls == QINF ? a.sign : b.sign, QINF);
	if (a.cls == QZERO && b.cls == QZERO)
		return special(a.sign && b.sign, QZERO);
	if (a.cls == QZERO)
		return pack(b.sign, b.m, 8, b.e, 0);
	if (b.cls == QZERO)
		return pack(a.sign, a.m, 8, a.e, 0);
	/* one far below the other only decides which way to round: it
	   can be replaced by a 1 bit far below the other's last */
	if (a.e - b.e > 240) {
		memset(b.m, 0, sizeof b.m);
		b.m[0] = 1;
		b.e = a.e - 240;
	} else if (b.e - a.e > 240) {
		memset(a.m, 0, sizeof a.m);
		a.m[0] = 1;
		a.e = b.e - 240;
	}
	e = a.e < b.e ? a.e : b.e;
	nx = bigshl(x, a.m, 8, a.e - e);
	ny = bigshl(y, b.m, 8, b.e - e);
	if (nx < ny)
		memset(x + nx, 0, (ny - nx)*sizeof *x), nx = ny;
	if (ny < nx)
		memset(y + ny, 0, (nx - ny)*sizeof *y), ny = nx;
	if (a.sign == b.sign) {
		unsigned long c = 0;
		int i;
		for (i = 0; i < nx; i++) {
			c += x[i] + y[i];
			x[i] = c&0xFFFF;
			c >>= 16;
		}
		x[nx++] = c;
		sign = a.sign;
	} else {
		int c = bigcmp(x, nx, y, ny);
		if (c == 0)
			return special(0, QZERO);	/* x - x is +0 */
		if (c > 0) {
			bigsub(x, nx, y, ny);
			sign = a.sign;
		} else {
			bigsub(y, ny, x, nx);
			memcpy(x, y, ny*sizeof *x);
			sign = b.sign;
		}
	}
	return pack(sign, x, nx, e, 0);
}

Quad q_add(Quad a, Quad b) {
	return addsub(unpack(a), unpack(b), 0);
}

Quad q_sub(Quad a, Quad b) {
	return addsub(unpack(a), unpack(b), 1);
}

Quad q_mul(Quad a, Quad b) {
	Unpacked x = unpack(a), y = unpack(b);
	unsigned long r[17];
	int i, j, sign = x.sign ^ y.sign;

	if (x.cls == QNAN || y.cls == QNAN)
		return special(0, QNAN);
	if (x.cls == QINF || y.cls == QINF)
		return x.cls == QZERO || y.cls == QZERO ? special(0, QNAN) : special(sign, QINF);
	if (x.cls == QZERO || y.cls == QZERO)
		return special(sign, QZERO);
	memset(r, 0, sizeof r);
	for (i = 0; i < 8; i++) {
		unsigned long c = 0;
		for (j = 0; j < 8; j++) {
			c += r[i+j] + x.m[i]*y.m[j];
			r[i+j] = c&0xFFFF;
			c >>= 16;
		}
		r[i+8] = c;
	}
	return pack(sign, r, 17, x.e + y.e, 0);
}

Quad q_div(Quad a, Quad b) {
	Unpacked x = unpack(a), y = unpack(b);
	unsigned long n[32], q[16];
	int sign = x.sign ^ y.sign, nn, sticky;
	long s;

	if (x.cls == QNAN || y.cls == QNAN)
		return special(0, QNAN);
	if (x.cls == QINF)
		return y.cls == QINF ? special(0, QNAN) : special(sign, QINF);
	if (y.cls == QINF)
		return special(sign, QZERO);
	if (y.cls == QZERO)
		return x.cls == QZERO ? special(0, QNAN) : special(sign, QINF);
	if (x.cls == QZERO)
		return special(sign, QZERO);
	/* a quotient of at least 116 bits, and a sticky bit */
	s = P + 3 + biglen(y.m, 8) - biglen(x.m, 8);
	nn = bigshl(n, x.m, 8, s);
	sticky = bigdiv(q, 16, n, nn, y.m, 8);
	return pack(sign, q, 16, x.e - y.e - s, sticky);
}

/*
 * q_fromstr - the quad nearest the decimal floating constant at s:
 * digits with an optional point, and an optional exponent; what
 * follows is ignored.  It is D*10**E for an integer D; for E < 0 it is
 * computed as D*2**s/10**-E, with a quotient of at least 116 bits.
 */
Quad q_fromstr(const char *s) {
	const char *t;
	long ndig = 0, sig = 0, frac = 0, x = 0, E, i, k;
	int xsign = 0, n, nb, na;
	unsigned long *d, *b, *a, *q;
	Quad r;

	for (t = s; *t >= '0' && *t <= '9' || *t == '.'; t++)
		if (*t != '.') {
			ndig++;
			if (sig || *t != '0')
				sig++;	/* from the first that is not 0 */
		}
	d = malloc((ndig/4 + 2)*sizeof *d);
	n = 0;
	for (t = s; *t >= '0' && *t <= '9' || *t == '.'; t++)
		if (*t == '.')
			frac = 1;
		else {
			n = bigmul(d, n, 10, *t - '0');
			if (frac)
				frac++;
		}
	frac = frac ? frac - 1 : 0;
	if (*t == 'e' || *t == 'E') {
		t++;
		if (*t == '+' || *t == '-')
			xsign = *t++ == '-';
		for (; *t >= '0' && *t <= '9'; t++)
			if (x < 100000)
				x = 10*x + *t - '0';
	}
	E = (xsign ? -x : x) - frac;
	if (biglen(d, n) == 0)
		r = special(0, QZERO);
	else if (E + sig - 1 >= 4933)	/* at least 10**4933: overflows */
		r = special(0, QINF);
	else if (E + sig <= -4967)	/* less than 10**-4967: 0 */
		r = special(0, QZERO);
	else if (E >= 0) {
		a = malloc((n + E/4 + 3)*sizeof *a);
		memcpy(a, d, n*sizeof *a);
		na = n;
		for (k = E; k >= 4; k -= 4)
			na = bigmul(a, na, 10000, 0);
		for (; k > 0; k--)
			na = bigmul(a, na, 10, 0);
		r = pack(0, a, na, 0, 0);
		free(a);
	} else {
		long sh;
		b = malloc((-E/4 + 3)*sizeof *b);
		b[0] = 1;
		nb = 1;
		for (k = -E; k >= 4; k -= 4)
			nb = bigmul(b, nb, 10000, 0);
		for (; k > 0; k--)
			nb = bigmul(b, nb, 10, 0);
		sh = P + 3 + biglen(b, nb) - biglen(d, n);
		if (sh > 0) {
			a = malloc((n + sh/16 + 2)*sizeof *a);
			na = bigshl(a, d, n, sh);
		} else {	/* D is the larger: shift 10**-E instead */
			unsigned long *b2 = malloc((nb + -sh/16 + 2)*sizeof *b2);
			nb = bigshl(b2, b, nb, -sh);
			free(b);
			b = b2;
			a = malloc(n*sizeof *a);
			memcpy(a, d, n*sizeof *a);
			na = n;
		}
		q = malloc(16*sizeof *q);
		i = bigdiv(q, 16, a, na, b, nb);
		r = pack(0, q, 16, -sh, (int)i);
		free(q);
		free(a);
		free(b);
	}
	free(d);
	return r;
}

/* q_fromld - the quad of d, which it holds exactly */
Quad q_fromld(long double d) {
	unsigned long m[8];
	long e = 0;
	int sign = 0, i;

	if (d != d)
		return special(0, QNAN);
	if (d < 0 || d == 0 && 1/d < 0) {
		sign = 1;
		d = -d;
	}
	if (d == 0)
		return special(sign, QZERO);
	if (d - d != 0)
		return special(sign, QINF);
	while (d >= 4294967296.0) {
		d /= 4294967296.0;
		e += 32;
	}
	while (d >= 2) {
		d /= 2;
		e++;
	}
	while (d < 1/4294967296.0) {
		d *= 4294967296.0;
		e -= 32;
	}
	while (d < 1) {
		d *= 2;
		e--;
	}
	/* 1 <= d < 2: 1 bit, then 7 digits of 16 */
	memset(m, 0, sizeof m);
	m[7] = 1;
	d -= 1;
	for (i = 6; i >= 0; i--) {
		unsigned long digit;
		d *= 65536;
		digit = (unsigned long)d;
		m[i] = digit;
		d -= digit;
	}
	return pack(sign, m, 8, e - (P - 1), 0);
}

Quad q_fromulong(unsigned long u) {
	unsigned long m[8];
	int n = 0;

	while (u) {
		m[n++] = u&0xFFFF;
		u >>= 8;
		u >>= 8;
	}
	return n ? pack(0, m, n, 0, 0) : special(0, QZERO);
}

Quad q_fromlong(long l) {
	if (l < 0)
		return q_neg(q_fromulong((unsigned long)-(l + 1) + 1));
	return q_fromulong(l);
}

/* q_tolong - q truncated to an integer, which a long must hold */
long q_tolong(Quad q) {
	Unpacked u = unpack(q);
	unsigned long v = 0;
	long i, len;

	if (u.cls != QNUM)
		return 0;
	len = biglen(u.m, 8);
	for (i = len - 1; i >= 0 && i + u.e >= 0; i--)
		if (i + u.e < (long)(8*sizeof v))
			v |= (unsigned long)bigbit(u.m, 8, i)<<(i + u.e);
	if (u.sign)
		return v ? -(long)(v - 1) - 1 : 0;
	return v;
}

/*
 * toh - q as a host long double rounded to p bits for exponents
 * emin..emax; the host's long double must hold it.  The value is built
 * from at most 64 bits with exact steps.
 */
static long double toh(Quad q, int p, long emin, long emax) {
	Unpacked u = unpack(q);
	unsigned long r[8];
	long re;
	long double v = 0, zero = 0;
	int i;

	if (u.cls == QNAN)
		return zero/zero;
	if (u.cls == QINF)
		return u.sign ? -1/zero : 1/zero;
	if (u.cls == QZERO)
		return u.sign ? -zero : zero;
	if (roundbits(u.m, 8, u.e, 0, p, emin, emax, r, &re))
		return u.sign ? -1/zero : 1/zero;
	for (i = 7; i >= 0; i--)
		v = v*65536 + r[i];
	for (; re >= 32; re -= 32)
		v *= 4294967296.0;
	for (; re > 0; re--)
		v *= 2;
	for (; re <= -32; re += 32)
		v /= 4294967296.0;
	for (; re < 0; re++)
		v /= 2;
	return u.sign ? -v : v;
}

long double q_told(Quad q) {
	return toh(q, LDBL_MANT_DIG, LDBL_MIN_EXP - 1, LDBL_MAX_EXP - 1);
}

double q_todouble(Quad q) {
	return (double)toh(q, DBL_MANT_DIG, DBL_MIN_EXP - 1, DBL_MAX_EXP - 1);
}

float q_tofloat(Quad q) {
	return (float)toh(q, FLT_MANT_DIG, FLT_MIN_EXP - 1, FLT_MAX_EXP - 1);
}
