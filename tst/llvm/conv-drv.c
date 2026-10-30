/*
 * Driver for conv.c: the expected values are the same conversions done
 * by clang.  Exits with 0 if every check passes.
 */
#include <stdio.h>
#include <limits.h>
int c2i(char), sc2i(signed char), uc2i(unsigned char), s2i(short), us2i(unsigned short);
char i2c(int); unsigned char i2uc(int); short l2s(long); unsigned short l2us(long);
long i2l(int); int l2i(long); unsigned long u2ul(unsigned), i2ul(int);
long u2l(unsigned); unsigned ul2u(unsigned long); long c2l(char); unsigned long uc2ul(unsigned char);
void setall(long);
long get_c(void), get_sc(void), get_uc(void), get_s(void), get_us(void), get_i(void), get_u(void);
long p2l(char *); char *l2p(long); unsigned p2u(char *); char *u2p(unsigned);
int shlx(int, long); unsigned long shrux(unsigned long, unsigned);
long mix(char, short, unsigned char, unsigned short, int);
static int bad, n;
#define CHK(e, w, fmt) do { n++; if ((e) != (w)) { bad++; printf("%s: " fmt " != " fmt "\n", #e, e, w); } } while (0)
int main(void) {
	static long v[] = { 0, 1, -1, 2, 127, 128, -128, -129, 255, 256, 300, -300, 32767, 32768,
		-32768, 65535, 65536, 0x12345678, INT_MAX, INT_MIN, 0x80000000L, 0xffffffffL,
		0x123456789L, -0x123456789L, LONG_MAX, LONG_MIN, 0x7fff00ff80L };
	int i, k;
	char buf[2];
	for (i = 0; i < (int)(sizeof v / sizeof v[0]); i++) {
		long x = v[i];
		CHK(c2i((char)x), (int)(char)x, "%d");
		CHK(sc2i((signed char)x), (int)(signed char)x, "%d");
		CHK(uc2i((unsigned char)x), (int)(unsigned char)x, "%d");
		CHK(s2i((short)x), (int)(short)x, "%d");
		CHK(us2i((unsigned short)x), (int)(unsigned short)x, "%d");
		CHK(i2c((int)x), (char)(int)x, "%d");
		CHK(i2uc((int)x), (unsigned char)(int)x, "%d");
		CHK(l2s(x), (short)x, "%d");
		CHK(l2us(x), (unsigned short)x, "%d");
		CHK(i2l((int)x), (long)(int)x, "%ld");
		CHK(l2i(x), (int)x, "%d");
		CHK(u2ul((unsigned)x), (unsigned long)(unsigned)x, "%lu");
		CHK(i2ul((int)x), (unsigned long)(int)x, "%lu");
		CHK(u2l((unsigned)x), (long)(unsigned)x, "%ld");
		CHK(ul2u((unsigned long)x), (unsigned)x, "%u");
		CHK(c2l((char)x), (long)(char)x, "%ld");
		CHK(uc2ul((unsigned char)x), (unsigned long)(unsigned char)x, "%lu");
		setall(x);
		CHK(get_c(), (long)(char)x, "%ld");
		CHK(get_sc(), (long)(signed char)x, "%ld");
		CHK(get_uc(), (long)(unsigned char)x, "%ld");
		CHK(get_s(), (long)(short)x, "%ld");
		CHK(get_us(), (long)(unsigned short)x, "%ld");
		CHK(get_i(), (long)(int)x, "%ld");
		CHK(get_u(), (long)(unsigned)x, "%ld");
		CHK(l2p(x), (char *)x, "%p");
		CHK(u2p((unsigned)x), (char *)(unsigned long)(unsigned)x, "%p");
		CHK(mix((char)x, (short)(x >> 3), (unsigned char)(x >> 5), (unsigned short)(x >> 7), (int)x),
			((long)((char)x + (short)(x >> 3))) * (unsigned char)(x >> 5) - (unsigned short)(x >> 7)
			+ (int)x + ((char)x ^ (unsigned char)(x >> 5)), "%ld");
		for (k = 0; k < 31; k += 5)
			CHK(shlx((int)x, (long)k), (int)((unsigned)(int)x << k), "%d");
		for (k = 0; k < 64; k += 7)
			CHK(shrux((unsigned long)x, (unsigned)k), (unsigned long)x >> k, "%lu");
	}
	CHK(p2l(buf), (long)buf, "%ld");
	CHK(p2u(buf + 1), (unsigned)(long)(buf + 1), "%u");
	printf("%d checks, %d bad\n", n, bad);
	return bad != 0;
}
