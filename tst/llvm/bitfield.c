/*
 * Bit fields: an unsigned field narrower than int promotes to int
 * (C89 3.2.1.1) in arithmetic, comparisons and unary operators; a full
 * width unsigned field stays unsigned; signed fields are sign-extended,
 * also when const; stores, compound assignments and increments keep the
 * neighbouring fields.  Compiled by lcc; bitfield-drv.c checks the
 * results.
 */
struct bf { signed int i : 7; unsigned int u : 7; unsigned int w : 32; int z : 3; unsigned int v : 11; };
struct bf b;
const struct cbf { const int ci : 5; const unsigned cu : 5; } cb = { -3, 21 };

int mod_iu(int i) { return i % b.u; }
unsigned mod_iuc(int i) { return i % (unsigned)b.u; }
int mod_uu(void) { return b.i % b.u; }
int lt_neg(void) { return b.u < -1; }
int lt_negw(void) { return b.w < -1; }
int neg_u(void) { return -b.u; }
int com_u(void) { return ~b.u; }
int shr_u(void) { return -b.u >> 2; }
long widen(void) { return b.u + b.i + b.z + b.v; }
int cfield(void) { return cb.ci * 100 + cb.cu; }

void set(int i, unsigned u, unsigned w, int z, unsigned v) { b.i = i; b.u = u; b.w = w; b.z = z; b.v = v; }
int get_i(void) { return b.i; }
int get_u(void) { return b.u; }
unsigned get_w(void) { return b.w; }
int get_z(void) { return b.z; }
int get_v(void) { return b.v; }
int bump(void) { b.u++; b.z--; b.v = b.v + 1000; b.i += 50; return b.u; }
int asgval(int x) { int r; r = (b.u = x); return r + (b.z = x); }
