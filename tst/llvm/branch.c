/*
 * Comparisons and control flow: the six comparisons for signed and
 * unsigned int and long, char and pointer comparisons, && || ! and ?:,
 * while, for and do loops with break, continue, goto and an early
 * return.  Compiled by lcc; branch-drv.c checks the results.
 */
#define CMP(T, s) \
	int eq##s(T a, T b) { return a == b; } \
	int ne##s(T a, T b) { return a != b; } \
	int lt##s(T a, T b) { return a < b; } \
	int le##s(T a, T b) { return a <= b; } \
	int gt##s(T a, T b) { return a > b; } \
	int ge##s(T a, T b) { return a >= b; }
CMP(int, _i)
CMP(unsigned, _u)
CMP(long, _l)
CMP(unsigned long, _ul)

int lower(char c) { return c >= 'a' && c <= 'z'; }
int sclass(signed char c) { return c < 0 ? -1 : c > 0; }
int before(char *p, char *q) { return p < q; }
int isnull(char *p) { return !p; }
int both(int a, int b) { return a && b; }
int either(int a, int b) { return a || b; }
int pick(int a, int b, int c) { return a > b ? (b > c ? b : c) : a; }

int sum(int *a, int n) {
	int i, s;
	s = 0;
	for (i = 0; i < n; i++)
		s = s + a[i];
	return s;
}

int len(char *s) {
	int n;
	n = 0;
	while (*s++)
		n++;
	return n;
}

long collatz(long n) {	/* steps to reach 1 */
	long k;
	k = 0;
	do {
		if (n % 2 == 0)
			n = n / 2;
		else
			n = 3 * n + 1;
		k++;
	} while (n != 1);
	return k;
}

int find(int *a, int n, int x) {
	int i;
	for (i = 0; i < n; i++)
		if (a[i] == x)
			return i;
	return -1;
}

int oddsum(int n) {	/* odd numbers below n, stopping at 1000 */
	int i, s;
	s = 0;
	for (i = 0; ; i++) {
		if (i >= n)
			break;
		if (i % 2 == 0)
			continue;
		if (s > 1000)
			break;
		s = s + i;
	}
	return s;
}

int triangles(int n) {	/* nested loops and goto */
	int i, j, c;
	c = 0;
	for (i = 1; i <= n; i++)
		for (j = 1; j <= i; j++) {
			if (i * j > 500)
				goto out;
			c = c + (i ^ j) % 3;
		}
out:
	return c;
}

unsigned umax(unsigned *a, int n) {
	unsigned m;
	int i;
	m = 0;
	for (i = 0; i < n; i++)
		if (a[i] > m)
			m = a[i];
	return m;
}
