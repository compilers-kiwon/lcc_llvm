/*
 * Driver for vdef.c.  Exits with 0 if every check passes.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "port.h"
long isum(int, ...); double dsum(int, ...); double mixed(const char *, ...);
long late(int, int, int, int, int, int, ...);
double dlate(double, double, double, double, double, double, double, double, int, ...);
long args(int, ...), twice(int, ...), passon(int, ...); int format(char *, const char *, ...);
long lccisum(void); double lccmixed(void); long lcclate(void);
static int bad, n;
#define CHK(e, w, fmt) do { n++; if ((e) != (w)) { bad++; printf("%s: " fmt " != " fmt "\n", #e, e, w); } } while (0)
static long risum(int n, const int *a) { long s = 0; int i; for (i = 0; i < n; i++) s = s * 3 + a[i]; return s; }
static double rdsum(int n, const double *a) { double s = 0; int i; for (i = 0; i < n; i++) s = s * 1.5 + a[i]; return s; }
static double rmixed(const char *fmt, ...) {	/* mixed compiled by clang */
	va_list ap; double s = 0;
	va_start(ap, fmt);
	for (; *fmt; fmt++) {
		s *= 2;
		switch (*fmt) {
		case 'i': s += va_arg(ap, int); break;
		case 'u': s += va_arg(ap, unsigned); break;
		case 'l': s += va_arg(ap, long); break;
		case 'p': { char *p = va_arg(ap, char *); s += p[0] + p[1]; } break;
		case 'd': s += va_arg(ap, double); break;
		case 'c': case 's': case 'b': s += va_arg(ap, int); break;
		case 'f': s += va_arg(ap, double); break;
		}
	}
	va_end(ap);
	return s;
}
static long rvisum(int n, const long *a) { long s = 0; int i; for (i = 0; i < n; i++) s = s * 5 + a[i]; return s; }
int main(void) {
	static const int ia[] = { 1, -2, 3, -4, 5, -6, 7, -8, 9, -10, 11, -12, 13, -14 };
	static const double da[] = { 0.5, -1.25, 2, 3.5, -4.75, 5, 6.125, -7, 8, 9.5, -10, 11, 12.25, 13 };
	char buf[200], ref[200];
	CHK(isum(0), 0L, "%ld");
	CHK(isum(1, 1), risum(1, ia), "%ld");
	CHK(isum(5, 1, -2, 3, -4, 5), risum(5, ia), "%ld");
	CHK(isum(6, 1, -2, 3, -4, 5, -6), risum(6, ia), "%ld");
	CHK(isum(14, 1, -2, 3, -4, 5, -6, 7, -8, 9, -10, 11, -12, 13, -14), risum(14, ia), "%ld");
	CHK(dsum(0), 0.0, "%g");
	CHK(dsum(3, 0.5, -1.25, 2.0), rdsum(3, da), "%g");
	CHK(dsum(8, 0.5, -1.25, 2.0, 3.5, -4.75, 5.0, 6.125, -7.0), rdsum(8, da), "%g");
	CHK(dsum(14, 0.5, -1.25, 2.0, 3.5, -4.75, 5.0, 6.125, -7.0, 8.0, 9.5, -10.0, 11.0, 12.25, 13.0), rdsum(14, da), "%g");
	CHK(mixed(""), 0.0, "%g");
	CHK(mixed("i", -7), -7.0, "%g");
#define MIXED(args) CHK(mixed args, rmixed args, "%g")
	MIXED(("idpclfsub", 3, 0.5, "xy", 'A', -5L, 2.25f, (short)-300, 7u, (unsigned char)200));
	MIXED(("ccss", (char)-1, (char)127, (short)-32768, (short)32767));
	MIXED(("bub", (unsigned char)255, 4000000000u, (unsigned char)0));
	MIXED(("dididididididididid", 1.0, 2, 3.0, 4, 5.0, 6, 7.0, 8, 9.0, 10, 11.0, 12, 13.0, 14, 15.0, 16, 17.0, 18, 19.0));
	MIXED(("llllllll", 1L, 2L, 3L, 4L, 5L, 6L, 7L, LV(0x100000000L, 0x10000L)));
	MIXED(("pdpdpdpdpdpdpdpdpdpd", "ab", 1.5, "cd", 2.5, "ef", 3.5, "gh", 4.5, "ij", 5.5, "kl", 6.5, "mn", 7.5, "op", 8.5, "qr", 9.5, "st", 10.5));
	CHK(mixed("i", -7), -7.0, "%g");
	CHK(mixed("bub", (unsigned char)255, 4000000000u, (unsigned char)0), (255.0 * 2 + 4000000000.0) * 2, "%g");
	CHK(late(1, 2, 3, 4, 5, 6, 7L, 8, 9.75), ((21L * 10 + 7) * 10 + 8) * 10 + 9, "%ld");
	CHK(late(0, 0, 0, 0, 0, 0, LV(-0x100000000L, -0x10000L), -1, -2.5), (LV(-0x100000000L, -0x10000L) * 10 - 1) * 10 - 2, "%ld");
	CHK(dlate(1, 2, 3, 4, 5, 6, 7, 8, 0), 36.0, "%g");
	CHK(dlate(1, 2, 3, 4, 5, 6, 7, 8, 3, 0.5, 1, 0.25, 2, -1.0, 3), ((36.0 * 2 + 0.5 + 1) * 2 + 0.25 + 2) * 2 - 1 + 3, "%g");
	CHK(args(4, 12, 34L, 5, 6), 12 * 1000 + 34 + 5 * 7 - 6, "%ld");
	CHK(twice(4, 1, 2, 3, 4), ((((10L * 2 + 1) * 2 + 2) * 2 + 3) * 2 + 4), "%ld");
	CHK(twice(9, 1, 2, 3, 4, 5, 6, 7, 8, 9), 45L * 512 + 1 * 256 + 2 * 128 + 3 * 64 + 4 * 32 + 5 * 16 + 6 * 8 + 7 * 4 + 8 * 2 + 9, "%ld");
	{ static const long la[] = { 1, 2, 3, -4, 5, LV(-0x100000000L, -0x10000L), 7, 8 };
	  CHK(passon(3, 1L, 2L, 3L), rvisum(3, la), "%ld");
	  CHK(passon(8, 1L, 2L, 3L, -4L, 5L, LV(-0x100000000L, -0x10000L), 7L, 8L), rvisum(8, la), "%ld"); }
	CHK(format(buf, "%d %s %.3f %ld %c %5.1e|", 42, "str", 3.14159, LV(-1234567890123L, -1234567890L), 'z', 6.02e23), sprintf(ref, "%d %s %.3f %ld %c %5.1e|", 42, "str", 3.14159, LV(-1234567890123L, -1234567890L), 'z', 6.02e23), "%d");
	CHK(strcmp(buf, ref), 0, "%d");
	CHK(format(buf, "%g %g %g %g %g %g %g %g %g %g %d %d %d %d %d %d", 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 1, 2, 3, 4, 5, 6),
		sprintf(ref, "%g %g %g %g %g %g %g %g %g %g %d %d %d %d %d %d", 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 1, 2, 3, 4, 5, 6), "%d");
	CHK(strcmp(buf, ref), 0, "%d");
	CHK(lccisum(), risum(9, ia), "%ld");
	CHK(lccmixed(), mixed("idpclfsub", 3, 0.5, "xy", 'A', -5L, 2.25f, (short)-300, 7u, (unsigned char)200), "%g");
	CHK(lcclate(), late(1, 2, 3, 4, 5, 6, 7L, 8, 9.75), "%ld");
	printf("%d checks, %d bad\n", n, bad);
	return bad != 0;
}
