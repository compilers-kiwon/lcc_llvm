/*
 * Pointer arithmetic: pointer plus or minus an integer of either
 * signedness in either operand order, constant offsets, pointer
 * difference, array and struct member indexing, stores through
 * pointers.  Compiled by lcc; pointer-drv.c checks the results.
 */
struct S { int x; int y[4]; };

int *add_pi(int *p, int i) { return p + i; }
int *add_ip(int *p, int i) { return i + p; }
int *sub_pl(int *p, long i) { return p - i; }
char *add_pu(char *p, unsigned u) { return p + u; }
int *minus3(int *p) { return p - 3; }
struct S *next(struct S *p) { return p + 1; }
long diff(int *p, int *q) { return p - q; }
long diffs(struct S *p, struct S *q) { return p - q; }

int get(int *a, int i) { return a[i]; }
int getneg(int *a) { return a[-1]; }
long getl(long *a, unsigned i) { return a[i]; }
int field(struct S *p, int i) { return p->y[i] + p->x; }
int get2(int **pp, int i, int j) { return pp[i][j]; }
void put(int *a, int i, int v) { a[i] = v; }
void inc(long *a, long i) { a[i] = a[i] + 1; }
char getc_(char *s, int i) { return s[i]; }

int m[5][7];
int getm(int i, int j) { return m[i][j]; }
void setm(int i, int j, int v) { m[i][j] = v; }
int *rowp(int i) { return m[i]; }
