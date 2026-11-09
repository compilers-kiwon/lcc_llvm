/*
 * Globals and their initial values, without functions: integers,
 * floating values, strings and wide strings, partly initialized arrays,
 * structures with padding, bit fields, unions, addresses of globals
 * plus constants, also of variables defined elsewhere, statics, arrays
 * without an initializer, and two big tables.  Compiled by lcc;
 * data-drv.c includes this file again, compiled by clang, and compares
 * the globals.
 */
#include <stddef.h>	/* wchar_t */
#include "port.h"

/* integers: a plain char, one limit of each other type, then both
   limits of each */
char vc = 'a';
signed char vsc = -128;
unsigned char vuc = 255;
short vs = -32768;
unsigned short vus = 65535;
int vi = -2147483647 - 1;
unsigned vu = 4294967295U;
long vl = LV(-9223372036854775807L, -2147483647L) - 1;
unsigned long vul = LV(18446744073709551615UL, 4294967295UL);
struct limits {
	signed char sc[2]; unsigned char uc[2]; short s[2]; unsigned short us[2];
	int i[2]; unsigned u[2]; long l[2]; unsigned long ul[2];
} lim = {
	{ -128, 127 }, { 0, 255 }, { -32768, 32767 }, { 0, 65535 },
	{ -2147483647 - 1, 2147483647 }, { 0, 4294967295U },
	{ LV(-9223372036854775807L, -2147483647L) - 1, LV(9223372036854775807L, 2147483647L) },
	{ 0, LV(18446744073709551615UL, 4294967295UL) }
};

/* floating: rounding, signed zeros, limits; ldmin is the smallest
   denormal */
float vf = 0.1f;
float negzf = -0.0f;
double vd = 0.1;
double negz = -0.0;
long double vld = 0.1L;
long double ldmax = LDMAXC;
long double ldmin = LDDENC;
float farr[] = { 0.1f, -0.0f, 3.40282347e+38f, 1.17549435e-38f };
double darr[] = { 0.1, -0.0, 1.7976931348623157e+308, 2.2250738585072014e-308 };
long double ldarr[3] = { 1.5L, -0.0L, 0.1L };

/* strings: NUL and bytes over 127 inside, room left, no room for the
   NUL, characters one by one */
char str[] = "a\0b\n\"\\\377\200";
char big[100] = "x";
char exact[3] = "abc";
char chars[] = { 'a', -1, 0, 127, -128 };

/* wide strings: arrays like those above, a pointer, elements wider
   than a byte */
wchar_t w[] = L"ab\377c";
wchar_t wbig[8] = L"hi";
wchar_t wexact[2] = L"ab";
wchar_t wwide[] = { L"\x1234\x7fffffff\xffffffff" };
wchar_t *pw = L"xyz";

/* arrays and structures: partly initialized, padding */
int arr[5] = { 1, 2 };
short sarr[4] = { -1, 2, -3 };
struct S {
	char c; int i; double d; long double ld;
	char s[7]; short h; float f; char *p;
} st = { 'z', 7, 2.5, 3.25L, "abc", -3, 1.5f, str + 2 };

/* bit fields: signed, unsigned and full width fields, members after a
   full unit and after a partly filled one, a field that starts a new
   unit, unnamed fields and a :0.  No field follows a member that ends
   inside a 32-bit unit: lcc would start a new unit there, gcc does not */
struct bf {
	unsigned a:3; int b:5; unsigned c:24;
	char d; short e;
	int n:4; unsigned m:28;
	int s:32; unsigned x:20; unsigned y:20; int z:1;
	unsigned :5; unsigned f:4; unsigned :0; unsigned g:7;
	char h;
} bits = { 5, -3, 0xABCDEF, 'q', -300, -8, 0xFFFFFFF,
	-2147483647 - 1, 0xFEDCB, 0x12345, -1, 9, 100, 'h' };

/* unions: the first member is initialized, the rest is padding */
union uc { char c; double d; long l; } u1 = { 'x' };
union us { struct { char a; int b; } s; long double ld; } u2 = { { 'p', -9 } };
union uld { long double ld; int i; } u3 = { 1.5L };
union ubf { unsigned b:5; int i; } u4 = { 21 };
union ustr { char s[3]; int i; } u5 = { "ab" };
struct hasu { char c; union uc u; short s; } un = { 'a', { 'b' }, -7 };
union uc uarr[3] = { { 'x' }, { 'y' } };

/* addresses: plus constants, one past the end, to itself, nested,
   into a string, of variables that data-drv.c defines */
int *pa = &arr[3];
char *pend = big + 100;
struct node { struct node *next; int v; } self = { &self, 5 };
struct nst { int v; int sub[2]; } nest[2] = { { 1, { 2, 3 } }, { 4, { 5, 6 } } };
int *pnest = &nest[1].sub[1];
char *pstr = "hello" + 2;
extern int ext;
extern char extarr[];
int *pext = &ext;
char *pextarr = extarr + 3;

/* others: static, static const and const variables, null and integer
   pointers, also inside an array, string pointers, and variables of
   each kind without an initializer, one of them static.  lcc defines
   those at the end of the file, so zero comes after ptab, which points
   into it */
static int hidden = 3;
int *phidden = &hidden;
static int szero[3];
int *pszero = szero;
static const short sci[2] = { 7, -7 };
const short *psci = sci;
const int ci = 42;
char *nul = 0;
char *one = (char *)1;
char *ipt[3] = { (char *)1, 0, (char *)0x100 };
int zero[1000];
int zi;
char *zp;
long double zld;
struct S zst;
union us zu;
char *strs[] = { "one", "two", 0, "four" };

/* big tables, of 10000 and 512 structures and 600 pointers, which a
   back end may emit in pieces: E(n) is entry n - 10000 of tab and tab2,
   an address and a number; P(n) is entries 2*(n - 1000) and 2*(n - 1000) + 1 of ptab, a
   string and an address */
struct ent { char *p; int v; };
#define E(n) { big + (n - 10000) % 100, 7 * (n - 10000) + 1 },
#define E10(n) E(n##0) E(n##1) E(n##2) E(n##3) E(n##4) \
	E(n##5) E(n##6) E(n##7) E(n##8) E(n##9)
#define E100(n) E10(n##0) E10(n##1) E10(n##2) E10(n##3) E10(n##4) \
	E10(n##5) E10(n##6) E10(n##7) E10(n##8) E10(n##9)
#define E1000(n) E100(n##0) E100(n##1) E100(n##2) E100(n##3) E100(n##4) \
	E100(n##5) E100(n##6) E100(n##7) E100(n##8) E100(n##9)
struct ent tab[10000] = {
	E1000(10) E1000(11) E1000(12) E1000(13) E1000(14)
	E1000(15) E1000(16) E1000(17) E1000(18) E1000(19)
};
struct ent tab2[512] = {
	E100(100) E100(101) E100(102) E100(103) E100(104)
	E10(1050) E(10510) E(10511)
};
#define P(n) #n, (char *)&zero[n - 1000],
#define P10(n) P(n##0) P(n##1) P(n##2) P(n##3) P(n##4) \
	P(n##5) P(n##6) P(n##7) P(n##8) P(n##9)
#define P100(n) P10(n##0) P10(n##1) P10(n##2) P10(n##3) P10(n##4) \
	P10(n##5) P10(n##6) P10(n##7) P10(n##8) P10(n##9)
char *ptab[600] = { P100(10) P100(11) P100(12) };
