/*
 * Driver for data.c: includes data.c again with renamed globals,
 * compiled by clang, and compares the bytes of each global, padding
 * too; long doubles are compared in their 80 bits, pointers as offsets
 * from their objects.  Then checks the alignment of each global and
 * that variables of each kind are writable.  Exits with 0 if every
 * check passes.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define vc r_vc
#define vsc r_vsc
#define vuc r_vuc
#define vs r_vs
#define vus r_vus
#define vi r_vi
#define vu r_vu
#define vl r_vl
#define vul r_vul
#define lim r_lim
#define vf r_vf
#define negzf r_negzf
#define vd r_vd
#define negz r_negz
#define vld r_vld
#define ldmax r_ldmax
#define ldmin r_ldmin
#define farr r_farr
#define darr r_darr
#define ldarr r_ldarr
#define str r_str
#define big r_big
#define exact r_exact
#define chars r_chars
#define w r_w
#define wbig r_wbig
#define wexact r_wexact
#define wwide r_wwide
#define pw r_pw
#define arr r_arr
#define sarr r_sarr
#define st r_st
#define bits r_bits
#define u1 r_u1
#define u2 r_u2
#define u3 r_u3
#define u4 r_u4
#define u5 r_u5
#define un r_un
#define uarr r_uarr
#define pa r_pa
#define pend r_pend
#define self r_self
#define nest r_nest
#define pnest r_pnest
#define pstr r_pstr
#define pext r_pext
#define pextarr r_pextarr
#define hidden r_hidden
#define phidden r_phidden
#define szero r_szero
#define pszero r_pszero
#define sci r_sci
#define psci r_psci
#define ci r_ci
#define nul r_nul
#define one r_one
#define zero r_zero
#define strs r_strs
#define tab r_tab
#define ipt r_ipt
#define zi r_zi
#define zp r_zp
#define zld r_zld
#define zst r_zst
#define zu r_zu
#define tab2 r_tab2
#define ptab r_ptab
#include "data.c"
#undef vc
#undef vsc
#undef vuc
#undef vs
#undef vus
#undef vi
#undef vu
#undef vl
#undef vul
#undef lim
#undef vf
#undef negzf
#undef vd
#undef negz
#undef vld
#undef ldmax
#undef ldmin
#undef farr
#undef darr
#undef ldarr
#undef str
#undef big
#undef exact
#undef chars
#undef w
#undef wbig
#undef wexact
#undef wwide
#undef pw
#undef arr
#undef sarr
#undef st
#undef bits
#undef u1
#undef u2
#undef u3
#undef u4
#undef u5
#undef un
#undef uarr
#undef pa
#undef pend
#undef self
#undef nest
#undef pnest
#undef pstr
#undef pext
#undef pextarr
#undef hidden
#undef phidden
#undef szero
#undef pszero
#undef sci
#undef psci
#undef ci
#undef nul
#undef one
#undef zero
#undef strs
#undef tab
#undef ipt
#undef zi
#undef zp
#undef zld
#undef zst
#undef zu
#undef tab2
#undef ptab
extern char vc; extern signed char vsc; extern unsigned char vuc;
extern short vs; extern unsigned short vus; extern int vi; extern unsigned vu;
extern long vl; extern unsigned long vul; extern struct limits lim;
extern float vf, negzf; extern double vd, negz; extern long double vld, ldmax, ldmin;
extern float farr[4]; extern double darr[4]; extern long double ldarr[3];
extern char str[9], big[100], exact[3], chars[5];
extern wchar_t w[5], wbig[8], wexact[2], wwide[4], *pw;
extern int arr[5]; extern short sarr[4]; extern struct S st; extern struct bf bits;
extern union uc u1; extern union us u2; extern union uld u3; extern union ubf u4;
extern union ustr u5; extern struct hasu un; extern union uc uarr[3];
extern int *pa; extern char *pend; extern struct node self; extern struct nst nest[2];
extern int *pnest; extern char *pstr; extern int *pext; extern char *pextarr;
extern int *phidden, *pszero; extern const short *psci; extern const int ci;
extern char *nul, *one, *ipt[3]; extern int zero[1000]; extern char *strs[4];
extern int zi; extern char *zp; extern long double zld; extern struct S zst;
extern union us zu;
extern struct ent tab[10000], tab2[512]; extern char *ptab[600];

/* the variables that data.c declares extern */
int ext = 6;
char extarr[8] = "extarr";

/* the names of the statics in data.c, with other values: the link
   fails, or a check below does, if lcc makes one of them external */
int hidden = 4;
int szero[3] = { 1, 2, 3 };
const short sci[2] = { 8, -8 };

static int bad, n;

static void check(const char *what, int ok) {
	n++;
	if (!ok) {
		bad++;
		printf("%s is wrong\n", what);
	}
}

static void same(const char *what, const void *a, const void *b, size_t size) {
	check(what, memcmp(a, b, size) == 0);
}

/* part - compare the bytes of objects a and b at offsets from to to - 1 */
static void part(const char *what, const void *a, const void *b, size_t from, size_t to) {
	same(what, (const char *)a + from, (const char *)b + from, to - from);
}

/* aligned - check that the address p is a multiple of a */
static void aligned(const char *what, const void *p, int a) {
	check(what, (unsigned long)p % a == 0);
}

/* upto - check a table of len elements, of which k are right */
static void upto(const char *what, int k, int len) {
	check(what, k == len);
	if (k < len)
		printf("%s[%d] is the first wrong element\n", what, k);
}

#define SAME(x) same(#x, &x, &r_##x, sizeof x)
#define LD(x) same(#x, &x, &r_##x, 10)	/* the 80 bits of a long double */
#define AL(x, a) aligned("the alignment of " #x, &x, a)

int main(void) {
	int k;

	SAME(vc); SAME(vsc); SAME(vuc); SAME(vs); SAME(vus); SAME(vi); SAME(vu);
	SAME(vl); SAME(vul); SAME(lim);
	SAME(vf); SAME(negzf); SAME(vd); SAME(negz); LD(vld); LD(ldmax); LD(ldmin);
	SAME(farr); SAME(darr);
	for (k = 0; k < 3; k++)
		same("ldarr", &ldarr[k], &r_ldarr[k], 10);
	SAME(str); SAME(big); SAME(exact); SAME(chars);
	SAME(w); SAME(wbig); SAME(wexact); SAME(wwide);
	same("pw", pw, r_pw, sizeof L"xyz");
	SAME(arr); SAME(sarr);
	/* all of st but its pointer and the 6 bytes after the 80 bits of
	   st.ld */
	part("st", &st, &r_st, 0, offsetof(struct S, ld) + 10);
	part("st", &st, &r_st, offsetof(struct S, s), offsetof(struct S, p));
	part("st", &st, &r_st, offsetof(struct S, p) + sizeof st.p, sizeof st);
	check("st.p", st.p == str + 2);
	SAME(bits);
	SAME(u1); SAME(u2); LD(u3); SAME(u4); SAME(u5); SAME(un); SAME(uarr);
	check("pa", pa == &arr[3]); check("pend", pend == big + 100);
	check("self", self.next == &self);
	part("self", &self, &r_self, offsetof(struct node, v), sizeof self);
	SAME(nest); check("pnest", pnest == &nest[1].sub[1]);
	check("pstr", memcmp(pstr - 2, "hello", 6) == 0);
	check("pext", pext == &ext); check("pextarr", pextarr == extarr + 3);
	check("phidden", *phidden == 3 && hidden == 4);
	same("pszero", pszero, r_szero, sizeof r_szero);
	same("psci", psci, r_sci, sizeof r_sci); SAME(ci);
	check("nul", nul == 0); check("one", one == (char *)1); SAME(ipt);
	SAME(zi); SAME(zp); SAME(zld); SAME(zst); SAME(zu);
	for (k = 0; k < 1000 && zero[k] == 0; k++)
		;
	upto("zero", k, 1000);
	for (k = 0; k < 4; k++)
		check("strs", strs[k] == 0 ? r_strs[k] == 0
			: r_strs[k] != 0 && strcmp(strs[k], r_strs[k]) == 0);
	for (k = 0; k < 10000; k++)
		if (tab[k].p != big + (r_tab[k].p - r_big)
		|| memcmp(&tab[k].v, &r_tab[k].v, sizeof tab[k] - offsetof(struct ent, v)) != 0)
			break;
	upto("tab", k, 10000);
	for (k = 0; k < 512; k++)
		if (tab2[k].p != big + (r_tab2[k].p - r_big)
		|| memcmp(&tab2[k].v, &r_tab2[k].v, sizeof tab2[k] - offsetof(struct ent, v)) != 0)
			break;
	upto("tab2", k, 512);
	for (k = 0; k < 600; k++)
		if (k%2 == 0 ? strcmp(ptab[k], r_ptab[k]) != 0
		: ptab[k] != (char *)&zero[k/2])
			break;
	upto("ptab", k, 600);

	/* each global, and the statics and the wide string that pointers
	   point to, on a multiple of the alignment of its C type, where that
	   is more than 1.  The SysV ABI also aligns global arrays of 16
	   bytes or more to 16; lcc does not yet, so that is not checked */
	AL(vs, 2); AL(vus, 2); AL(vi, 4); AL(vu, 4); AL(vl, 8); AL(vul, 8); AL(lim, 8);
	AL(vf, 4); AL(negzf, 4); AL(vd, 8); AL(negz, 8); AL(vld, 16); AL(ldmax, 16); AL(ldmin, 16);
	AL(farr, 4); AL(darr, 8); AL(ldarr, 16);
	AL(w, 4); AL(wbig, 4); AL(wexact, 4); AL(wwide, 4);
	AL(pw, 8); aligned("the alignment of *pw", pw, 4);
	AL(arr, 4); AL(sarr, 2); AL(st, 16); AL(bits, 4);
	AL(u1, 8); AL(u2, 16); AL(u3, 16); AL(u4, 4); AL(u5, 4); AL(un, 8); AL(uarr, 8);
	AL(pa, 8); AL(pend, 8); AL(self, 8); AL(nest, 4); AL(pnest, 8); AL(pstr, 8);
	AL(pext, 8); AL(pextarr, 8);
	AL(phidden, 8); aligned("the alignment of hidden", phidden, 4);
	AL(pszero, 8); aligned("the alignment of szero", pszero, 4);
	AL(psci, 8); aligned("the alignment of sci", psci, 2);
	AL(ci, 4); AL(nul, 8); AL(one, 8); AL(zero, 4); AL(strs, 8); AL(tab, 8); AL(ptab, 8);
	AL(ipt, 8); AL(zi, 4); AL(zp, 8); AL(zld, 16); AL(zst, 16); AL(zu, 16); AL(tab2, 8);

	/* variables with each kind of initial value are writable: numbers,
	   strings, zeros, a single address or pointer constant, bytes and
	   addresses, arrays of pointers.  The driver crashes here if one is
	   read-only */
	vi = 1; vd = 2; vld = 3; *phidden = 4;
	str[0] = 5; exact[2] = 6;
	zero[999] = 7; pszero[2] = 8;
	pa = &arr[0]; pw = wbig; nul = big; one = 0;
	big[99] = 9; wbig[7] = 10; arr[4] = 11; st.h = 12; bits.g = 13; u2.s.b = 14;
	self.v = 15; tab[9999].v = 16;
	strs[2] = big; ptab[599] = 0;
	check("a written variable", vi == 1 && vd == 2 && vld == 3 && *phidden == 4
		&& str[0] == 5 && exact[2] == 6 && zero[999] == 7 && pszero[2] == 8
		&& pa == &arr[0] && pw == wbig && nul == big && one == 0
		&& big[99] == 9 && wbig[7] == 10 && arr[4] == 11 && st.h == 12
		&& bits.g == 13 && u2.s.b == 14 && self.v == 15 && tab[9999].v == 16
		&& strs[2] == big && ptab[599] == 0);

	printf("%d checks, %d bad\n", n, bad);
	return bad != 0;
}
