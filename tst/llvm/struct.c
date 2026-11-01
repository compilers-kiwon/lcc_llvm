/*
 * Structure and union copies: assignment of small, odd-sized, aligned
 * and large structures, nested members, arrays of structures, chained
 * and self assignment, parameters and results passed by value between
 * functions compiled by lcc, results used as arguments and selected
 * from, and initialization of locals from other structures.  Compiled
 * by lcc; struct-drv.c checks the results through pointers, since
 * structures are not passed by value as the SysV ABI says yet.
 */
struct c1 { char a; };
struct c3 { char a, b, c; };
struct s6 { short a, b, c; };
struct i12 { int a; char b; int c; };
struct mix { char c; double d; long l; short s; };
struct big { int n; char name[37]; long v[30]; };
struct outer { struct c3 h; struct i12 in; struct c3 t; };
struct bits { unsigned a:3, b:13; int c:7; };
union u { long l; char c[13]; double d; };

void cpc1(struct c1 *d, struct c1 *s) { *d = *s; }
void cpc3(struct c3 *d, struct c3 *s) { *d = *s; }
void cps6(struct s6 *d, struct s6 *s) { *d = *s; }
void cpi12(struct i12 *d, struct i12 *s) { *d = *s; }
void cpmix(struct mix *d, struct mix *s) { *d = *s; }
void cpbig(struct big *d, struct big *s) { *d = *s; }
void cpbits(struct bits *d, struct bits *s) { *d = *s; }
void cpu(union u *d, union u *s) { *d = *s; }

/* members: the inner structures move with the outer one */
void cpinner(struct outer *d, struct outer *s) { d->in = s->in; d->h = s->t; d->t = s->h; }
/* a[i] = a[j] for every pair, through a local copy */
void rotate(struct i12 *a, int n) {
	struct i12 t; int i;
	t = a[0];
	for (i = 0; i < n - 1; i++) a[i] = a[i + 1];
	a[n - 1] = t;
}
void chain(struct mix *a, struct mix *b, struct mix *c) { *a = *b = *c; }
void self(struct big *p) { struct big *q = p; *p = *q; }

/* by value: the callee works on its own copy */
static long sumbig(struct big b) {
	long s = b.n; int i;
	for (i = 0; i < 30; i++) { s += b.v[i]; b.v[i] = -1; }
	b.n = 0;
	return s + b.name[0];
}
static struct big makebig(int n) {
	struct big b; int i;
	b.n = n;
	for (i = 0; i < 30; i++) b.v[i] = (long)n * i;
	for (i = 0; i < 36; i++) b.name[i] = 'a' + (n + i) % 26;
	b.name[36] = 0;
	return b;
}
static struct big passbig(struct big b) { b.v[29] += 1; return b; }
static struct c3 makec3(int a, int b, int c) { struct c3 r; r.a = a; r.b = b; r.c = c; return r; }
static struct mix swapmix(struct mix m) { struct mix r; r = m; r.c = m.s; r.s = m.c; r.d = m.l; r.l = m.d; return r; }
static union u makeu(double d) { union u r; r.d = d; return r; }

/* returns sumbig of b and its unchanged copy in *after */
long byval(struct big *b, struct big *after) { long s = sumbig(*b); *after = *b; return s; }
void retbig(struct big *d, int n) { *d = makebig(n); }
void retpass(struct big *d, int n) { *d = passbig(passbig(makebig(n))); }
long retarg(int n) { return sumbig(makebig(n)) + makebig(n + 1).v[3] + makebig(n).name[2]; }
int retc3(int a, int b, int c) { return makec3(a, b, c).b * 100 + makec3(c, b, a).a; }
void retmix(struct mix *d, struct mix *s) { *d = swapmix(swapmix(swapmix(*s))); }
void retu(union u *d, double x) { *d = makeu(x); }
/* a local initialized from a parameter and from a call */
void initlocal(struct outer *d, struct outer o) { struct outer a = o; struct c3 c = makec3(o.t.c, 1, 2); a.h = c; *d = a; }
void callinit(struct outer *d, struct outer *s) { initlocal(d, *s); }
