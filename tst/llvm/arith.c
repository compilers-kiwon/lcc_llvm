/*
 * Integer arithmetic and bitwise operators for int, unsigned, long and
 * unsigned long.  Compiled by lcc; arith-drv.c, compiled by clang, calls
 * these functions and compares their results with its own.
 */
#define BIN(name, T, op) T name(a, b) T a, b; { return a op b; }
#define FOUR(T, s) BIN(add##s, T, +) BIN(sub##s, T, -) BIN(mul##s, T, *) \
	BIN(div##s, T, /) BIN(mod##s, T, %) BIN(and##s, T, &) \
	BIN(or##s, T, |) BIN(xor##s, T, ^)
FOUR(int, _i)
FOUR(unsigned, _u)
FOUR(long, _l)
FOUR(unsigned long, _ul)
int neg_i(a) int a; { return -a; }
long neg_l(a) long a; { return -a; }
unsigned neg_u(a) unsigned a; { return -a; }
unsigned long neg_ul(a) unsigned long a; { return -a; }
int com_i(a) int a; { return ~a; }
long com_l(a) long a; { return ~a; }
unsigned com_u(a) unsigned a; { return ~a; }
unsigned long com_ul(a) unsigned long a; { return ~a; }
/* shared subexpressions, locals and a global */
int g;
int mix(a, b) int a, b; {
	int t, u;
	t = a + b;
	u = (a + b) * (a + b) - (a ^ b);
	g = g + t;
	return u - t * 3 + g % 7;
}
long mixl(a, b) long a, b; { return (a * b + a) / (b | 1) - ~(a & b); }
