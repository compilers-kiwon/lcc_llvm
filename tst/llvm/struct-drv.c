/*
 * Driver for struct.c.  Exits with 0 if every check passes.
 */
#include <stdio.h>
#include <string.h>
struct c1 { char a; };
struct c3 { char a, b, c; };
struct s6 { short a, b, c; };
struct i12 { int a; char b; int c; };
struct mix { char c; double d; long l; short s; };
struct big { int n; char name[37]; long v[30]; };
struct outer { struct c3 h; struct i12 in; struct c3 t; };
struct bits { unsigned a:3, b:13; int c:7; };
union u { long l; char c[13]; double d; };
void cpc1(struct c1 *, struct c1 *), cpc3(struct c3 *, struct c3 *), cps6(struct s6 *, struct s6 *);
void cpi12(struct i12 *, struct i12 *), cpmix(struct mix *, struct mix *), cpbig(struct big *, struct big *);
void cpbits(struct bits *, struct bits *), cpu(union u *, union u *);
void cpinner(struct outer *, struct outer *), rotate(struct i12 *, int);
void chain(struct mix *, struct mix *, struct mix *), self(struct big *);
long byval(struct big *, struct big *); void retbig(struct big *, int), retpass(struct big *, int);
long retarg(int); int retc3(int, int, int); void retmix(struct mix *, struct mix *), retu(union u *, double);
void callinit(struct outer *, struct outer *);
static int bad, n;
#define CHK(e, w, fmt) do { n++; if ((e) != (w)) { bad++; printf("%s: " fmt " != " fmt "\n", #e, e, w); } } while (0)
#define SAME(a, b) do { n++; if (memcmp(a, b, sizeof *(a)) != 0) { bad++; printf("line %d: %s != %s\n", __LINE__, #a, #b); } } while (0)
/* fill with a pattern, so that padding is compared too */
static void fill(void *p, size_t len, int seed) { unsigned char *q = p; size_t i; for (i = 0; i < len; i++) q[i] = (unsigned char)(seed * 31 + i * 7); }
static struct big rmakebig(int n) {
	struct big b; int i;
	memset(&b, 0, sizeof b);
	b.n = n;
	for (i = 0; i < 30; i++) b.v[i] = (long)n * i;
	for (i = 0; i < 36; i++) b.name[i] = 'a' + (n + i) % 26;
	return b;
}
static long rsumbig(struct big *b) { long s = b->n; int i; for (i = 0; i < 30; i++) s += b->v[i]; return s + b->name[0]; }
int main(void) {
	struct c1 c1a, c1b; struct c3 c3a, c3b; struct s6 s6a, s6b; struct i12 i12a, i12b, arr[5], arr0[5];
	struct mix ma, mb, mc, md; struct big ba, bb, bc; struct outer oa, ob, oc; struct bits ta, tb; union u ua, ub;
	int i;
	fill(&c1a, sizeof c1a, 1); fill(&c1b, sizeof c1b, 2); cpc1(&c1a, &c1b); SAME(&c1a, &c1b);
	fill(&c3a, sizeof c3a, 3); fill(&c3b, sizeof c3b, 4); cpc3(&c3a, &c3b); SAME(&c3a, &c3b);
	fill(&s6a, sizeof s6a, 5); fill(&s6b, sizeof s6b, 6); cps6(&s6a, &s6b); SAME(&s6a, &s6b);
	fill(&i12a, sizeof i12a, 7); fill(&i12b, sizeof i12b, 8); cpi12(&i12a, &i12b); SAME(&i12a, &i12b);
	fill(&ma, sizeof ma, 9); fill(&mb, sizeof mb, 10); cpmix(&ma, &mb); SAME(&ma, &mb);
	fill(&ba, sizeof ba, 11); fill(&bb, sizeof bb, 12); cpbig(&ba, &bb); SAME(&ba, &bb);
	fill(&ta, sizeof ta, 13); fill(&tb, sizeof tb, 14); cpbits(&ta, &tb); SAME(&ta, &tb);
	fill(&ua, sizeof ua, 15); fill(&ub, sizeof ub, 16); cpu(&ua, &ub); SAME(&ua, &ub);
	CHK((int)sizeof(struct c3), 3, "%d"); CHK((int)sizeof(struct big), 288, "%d");

	fill(&oa, sizeof oa, 17); fill(&ob, sizeof ob, 18); oc = oa;
	cpinner(&oa, &ob);
	SAME(&oa.in, &ob.in); SAME(&oa.h, &ob.t); SAME(&oa.t, &ob.h);
	CHK(memcmp((char *)&oa + 3, (char *)&oc + 3, 1), 0, "%d");	/* padding before in is kept */

	for (i = 0; i < 5; i++) fill(&arr[i], sizeof arr[i], 20 + i);
	memcpy(arr0, arr, sizeof arr);
	rotate(arr, 5);
	for (i = 0; i < 5; i++) SAME(&arr[i], &arr0[(i + 1) % 5]);

	fill(&ma, sizeof ma, 30); fill(&mb, sizeof mb, 31); fill(&mc, sizeof mc, 32); md = mc;
	chain(&ma, &mb, &mc); SAME(&ma, &md); SAME(&mb, &md); SAME(&mc, &md);
	fill(&ba, sizeof ba, 33); bb = ba; self(&ba); SAME(&ba, &bb);

	for (i = -3; i < 4; i++) {
		bb = rmakebig(i * 5); ba = bb;
		CHK(byval(&ba, &bc), rsumbig(&bb), "%ld"); SAME(&ba, &bb); SAME(&bc, &bb);
		retbig(&ba, i); bb = rmakebig(i);
		CHK(ba.n, bb.n, "%d"); CHK(memcmp(ba.name, bb.name, 36), 0, "%d"); CHK(memcmp(ba.v, bb.v, sizeof ba.v), 0, "%d");
		retpass(&ba, i); bb.v[29] += 2;
		CHK(ba.n, bb.n, "%d"); CHK(memcmp(ba.name, bb.name, 36), 0, "%d"); CHK(memcmp(ba.v, bb.v, sizeof ba.v), 0, "%d");
		bb = rmakebig(i); bc = rmakebig(i + 1);
		CHK(retarg(i), rsumbig(&bb) + bc.v[3] + bb.name[2], "%ld");
		CHK(retc3(i, i * 3, i - 7), i * 3 * 100 + (i - 7), "%d");
	}
	memset(&ma, 0, sizeof ma); ma.c = 12; ma.d = 3.0; ma.l = -7; ma.s = 300;
	retmix(&mb, &ma);
	CHK(mb.c, (char)300, "%d"); CHK(mb.s, 12, "%d"); CHK(mb.d, -7.0, "%g"); CHK(mb.l, 3L, "%ld");
	retu(&ua, 2.5); CHK(ua.d, 2.5, "%g");
	fill(&oa, sizeof oa, 40); memset(&ob, 0, sizeof ob);
	oa.h.a = oa.h.b = oa.h.c = 0; oa.in.a = 7; oa.in.b = 8; oa.in.c = 9; oa.t.a = 1; oa.t.b = 2; oa.t.c = 3;
	callinit(&ob, &oa);
	CHK(ob.h.a, 3, "%d"); CHK(ob.h.b, 1, "%d"); CHK(ob.h.c, 2, "%d");
	CHK(ob.in.a, 7, "%d"); CHK(ob.in.b, 8, "%d"); CHK(ob.in.c, 9, "%d"); SAME(&ob.t, &oa.t);
	printf("%d checks, %d bad\n", n, bad);
	return bad != 0;
}
