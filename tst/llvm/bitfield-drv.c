/*
 * Driver for bitfield.c: the expected values are computed by clang on
 * the same struct.  Exits with 0 if every check passes.
 */
#include <stdio.h>
struct bf { signed int i : 7; unsigned int u : 7; unsigned int w : 32; int z : 3; unsigned int v : 11; };
extern struct bf b;
int mod_iu(int), mod_uu(void), lt_neg(void), lt_negw(void), neg_u(void), com_u(void), shr_u(void), cfield(void);
unsigned mod_iuc(int); long widen(void);
void set(int, unsigned, unsigned, int, unsigned);
int get_i(void), get_u(void), get_z(void), get_v(void), bump(void), asgval(int); unsigned get_w(void);
static int bad, n;
#define CHK(e, w, fmt) do { n++; if ((e) != (w)) { bad++; printf("%s: " fmt " != " fmt "\n", #e, e, w); } } while (0)
int main(void) {
	static int iv[] = { -64, -13, -1, 0, 1, 13, 63 };
	static unsigned uv[] = { 1, 7, 61, 127 };
	int i, j;
	struct bf r;
	for (i = 0; i < 7; i++) for (j = 0; j < 4; j++) {
		set(iv[i], uv[j], 0xfffffff0u + j, iv[i] % 4, uv[j] * 13);
		r.i = iv[i]; r.u = uv[j]; r.w = 0xfffffff0u + j; r.z = iv[i] % 4; r.v = uv[j] * 13;
		CHK(get_i(), r.i, "%d"); CHK(get_u(), r.u, "%d"); CHK(get_w(), r.w, "%u");
		CHK(get_z(), r.z, "%d"); CHK(get_v(), r.v, "%d");
		CHK(mod_iu(iv[i] * 3), iv[i] * 3 % r.u, "%d");
		CHK(mod_iuc(iv[i] * 3), iv[i] * 3 % (unsigned)r.u, "%u");
		CHK(mod_uu(), r.i % r.u, "%d");
		CHK(lt_neg(), r.u < -1, "%d"); CHK(lt_negw(), r.w < -1, "%d");
		CHK(neg_u(), -r.u, "%d"); CHK(com_u(), ~r.u, "%d"); CHK(shr_u(), -r.u >> 2, "%d");
		CHK(widen(), (long)(r.u + r.i + r.z + r.v), "%ld");
		r.u++; r.z--; r.v = r.v + 1000; r.i += 50;
		CHK(bump(), r.u, "%d");
		CHK(get_i(), r.i, "%d"); CHK(get_z(), r.z, "%d"); CHK(get_v(), r.v, "%d"); CHK(get_w(), r.w, "%u");
		{ int x = iv[i] * 5; int want = (r.u = x) + (r.z = x); CHK(asgval(x), want, "%d"); CHK(get_u(), r.u, "%d"); CHK(get_z(), r.z, "%d"); }
	}
	CHK(cfield(), -3 * 100 + 21, "%d");
	printf("%d checks, %d bad\n", n, bad);
	return bad != 0;
}
