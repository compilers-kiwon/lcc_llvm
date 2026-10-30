/*
 * Conversions between the integer types and pointers: narrow parameters
 * and results (passed as int), narrow globals, widening and narrowing
 * between int and long, pointer to integer and back.  Compiled by lcc;
 * conv-drv.c checks the results.
 */
int c2i(char c) { return c; }
int sc2i(signed char c) { return c; }
int uc2i(unsigned char c) { return c; }
int s2i(short s) { return s; }
int us2i(unsigned short s) { return s; }
char i2c(int x) { return x; }
unsigned char i2uc(int x) { return x; }
short l2s(long x) { return x; }
unsigned short l2us(long x) { return x; }

long i2l(int x) { return x; }
int l2i(long x) { return x; }
unsigned long u2ul(unsigned x) { return x; }
unsigned long i2ul(int x) { return x; }
long u2l(unsigned x) { return x; }
unsigned ul2u(unsigned long x) { return x; }
long c2l(char c) { return c; }
unsigned long uc2ul(unsigned char c) { return c; }

char gc; signed char gsc; unsigned char guc;
short gs; unsigned short gus; int gi; unsigned gu;
void setall(long x) { gc = x; gsc = x; guc = x; gs = x; gus = x; gi = x; gu = x; }
long get_c(void) { return gc; }
long get_sc(void) { return gsc; }
long get_uc(void) { return guc; }
long get_s(void) { return gs; }
long get_us(void) { return gus; }
long get_i(void) { return gi; }
long get_u(void) { return gu; }

long p2l(char *p) { return (long)p; }
char *l2p(long x) { return (char *)x; }
unsigned p2u(char *p) { return (unsigned)p; }
char *u2p(unsigned x) { return (char *)x; }

int shlx(int a, long n) { return a << n; }
unsigned long shrux(unsigned long a, unsigned n) { return a >> n; }

long mix(char c, short s, unsigned char uc, unsigned short us, int i) {
	long t;
	t = c + s;
	return t * uc - us + i + (c ^ uc);
}
