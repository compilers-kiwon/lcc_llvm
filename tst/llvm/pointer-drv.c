/*
 * Driver for pointer.c: compares the pointers and values with clang's.
 * Exits with 0 if every check passes.
 */
#include <stdio.h>
struct S { int x; int y[4]; };
int *add_pi(int *, int), *add_ip(int *, int), *sub_pl(int *, long), *minus3(int *);
char *add_pu(char *, unsigned); struct S *next(struct S *);
long diff(int *, int *), diffs(struct S *, struct S *);
int get(int *, int), getneg(int *), field(struct S *, int), get2(int **, int, int);
long getl(long *, unsigned); void put(int *, int, int); void inc(long *, long);
char getc_(char *, int);
extern int m[5][7]; int getm(int, int); void setm(int, int, int); int *rowp(int);
static int bad, n;
#define CHK(e, w, fmt) do { n++; if ((e) != (w)) { bad++; printf("%s: " fmt " != " fmt "\n", #e, e, w); } } while (0)
int main(void) {
	static int a[100]; static long la[50]; static struct S s[10];
	static char str[] = "hello, world";
	int *rows[3]; int r0[4] = { 1, 2, 3, 4 }, r1[4] = { 5, 6, 7, 8 }, r2[4] = { 9, 10, 11, 12 };
	int i, j;
	for (i = 0; i < 100; i++) a[i] = i * 7 - 300;
	for (i = 0; i < 50; i++) la[i] = i * 1000000007L;
	for (i = 0; i < 10; i++) { s[i].x = i; for (j = 0; j < 4; j++) s[i].y[j] = 10 * i + j; }
	rows[0] = r0; rows[1] = r1; rows[2] = r2;
	for (i = -40; i < 40; i++) {
		int *p = a + 50;
		CHK(add_pi(p, i), p + i, "%p");
		CHK(add_ip(p, i), i + p, "%p");
		CHK(sub_pl(p, (long)i), p - (long)i, "%p");
		CHK(diff(p + i, p), (long)i, "%ld");
		CHK(get(p, i), p[i], "%d");
		CHK(getneg(p + i), p[i - 1], "%d");
	}
	CHK(add_pu(str, 3u), str + 3, "%p");
	CHK(add_pu(str, 3000000000u), str + 3000000000u, "%p");
	CHK(minus3(a + 10), a + 7, "%p");
	CHK(next(s), s + 1, "%p");
	CHK(diffs(s + 9, s + 2), 7L, "%ld");
	CHK(diffs(s + 2, s + 9), -7L, "%ld");
	for (i = 0; i < 50; i++) CHK(getl(la, (unsigned)i), la[i], "%ld");
	for (i = 0; i < 10; i++) for (j = 0; j < 4; j++) CHK(field(s + i, j), s[i].y[j] + s[i].x, "%d");
	for (i = 0; i < 3; i++) for (j = 0; j < 4; j++) CHK(get2(rows, i, j), rows[i][j], "%d");
	for (i = 0; i < 100; i++) put(a, i, i * i);
	for (i = 0; i < 100; i++) CHK(a[i], i * i, "%d");
	for (i = 0; i < 50; i += 3) inc(la, (long)i);
	for (i = 0; i < 50; i++) CHK(la[i], i * 1000000007L + (i % 3 == 0), "%ld");
	for (i = 0; i < 12; i++) CHK(getc_(str, i), str[i], "%d");
	for (i = 0; i < 5; i++) for (j = 0; j < 7; j++) setm(i, j, i * 100 + j);
	for (i = 0; i < 5; i++) for (j = 0; j < 7; j++) {
		CHK(m[i][j], i * 100 + j, "%d");
		CHK(getm(i, j), i * 100 + j, "%d");
	}
	for (i = 0; i < 5; i++) CHK(rowp(i), m[i], "%p");
	printf("%d checks, %d bad\n", n, bad);
	return bad != 0;
}
