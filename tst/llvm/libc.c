/*
 * The C89 library through glibc's headers: all 15 headers, character
 * classes, mathematical functions, conversions and errno, strings,
 * qsort and bsearch with comparison functions compiled by lcc, div_t
 * and ldiv_t results, setjmp and longjmp, signal handlers, time,
 * streams on a temporary file, locales, the limits in limits.h and
 * float.h, and multibyte characters.  Compiled by lcc; libc-drv.c
 * includes this file again, compiled by clang, and compares.
 */
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int ctypes(int c) {
	assert(c >= EOF && c <= UCHAR_MAX);
	return isalnum(c) != 0 | (isalpha(c) != 0) << 1 | (iscntrl(c) != 0) << 2
		| (isdigit(c) != 0) << 3 | (isgraph(c) != 0) << 4 | (islower(c) != 0) << 5
		| (isprint(c) != 0) << 6 | (ispunct(c) != 0) << 7 | (isspace(c) != 0) << 8
		| (isupper(c) != 0) << 9 | (isxdigit(c) != 0) << 10
		| (tolower(c) & 0xff) << 11 | (toupper(c) & 0xff) << 19;
}

#define NMATH 23
double mathf(int f, double x, double y) {
	double i;
	int e;
	switch (f) {
	case 0: return sin(x);
	case 1: return cos(x);
	case 2: return tan(x);
	case 3: return asin(x / 4);
	case 4: return acos(x / 4);
	case 5: return atan(x);
	case 6: return atan2(x, y);
	case 7: return sinh(x);
	case 8: return cosh(x);
	case 9: return tanh(x);
	case 10: return exp(x);
	case 11: return log(fabs(x) + 1);
	case 12: return log10(fabs(x) + 1);
	case 13: return pow(fabs(x), y);
	case 14: return sqrt(fabs(x));
	case 15: return ceil(x);
	case 16: return floor(x);
	case 17: return fabs(x);
	case 18: return ldexp(x, (int)y);
	case 19: return frexp(x, &e) + e;
	case 20: return modf(x, &i) + 1000 * i;
	case 21: return fmod(x, y);
	case 22: errno = 0; x = sqrt(-1.0 - fabs(x)); return errno == EDOM ? 1 : 2;
	}
	return 0;
}

static char *numbers[] = { "0", "-17", "  +42xyz", "0x1F", "077", "99999999999999999999",
	"-99999999999999999999", "3.25e2", "-.5", "1e400", "abc", "18446744073709551615", NULL };
#define NCONV 7
long convs(int which, char *s) {
	char *end;
	long r;
	errno = 0;
	switch (which) {
	case 0: r = strtol(s, &end, 10); break;
	case 1: r = strtol(s, &end, 0); break;
	case 2: r = (long)strtoul(s, &end, 0); break;
	case 3: r = (long)(strtod(s, &end) * 1000); break;
	case 4: r = atoi(s); end = s; break;
	case 5: r = atol(s); end = s; break;
	case 6: r = (long)(atof(s) * 4); end = s; break;
	default: return 0;
	}
	return r * 1000 + (end - s) * 10 + (errno == ERANGE);
}

#define NSTR 14
long strings(int which, char *buf) {
	static char text[] = "the quick brown fox, jumps over: the lazy dog";
	char *p;
	long n = 0;
	strcpy(buf, text);
	switch (which) {
	case 0: return strlen(buf) + strcspn(buf, ",:") * 100 + strspn(buf, "eht ") * 10000;
	case 1: return strpbrk(buf, "xyz") - buf;
	case 2: return strstr(buf, "the lazy") - buf;
	case 3: return strchr(buf, 'o') - buf + (strrchr(buf, 'o') - buf) * 100;
	case 4: for (p = strtok(buf, " ,:"); p; p = strtok(NULL, " ,:")) n = n * 7 + strlen(p); return n;
	case 5: memmove(buf + 4, buf, 20); return memcmp(buf, text, 30) * 1000 + strncmp(buf + 4, text, 20);
	case 6: memmove(buf, buf + 4, 20); return strncmp(buf, text + 4, 20) == 0;
	case 7: return (char *)memchr(buf, 'j', sizeof text) - buf;
	case 8: strcat(buf, "!"); strncat(buf, "abcdef", 3); return strlen(buf) + buf[strlen(buf) - 1];
	case 9: strncpy(buf, "ab", 5); return buf[0] + buf[1] * 10 + buf[2] + buf[3] + buf[4] + buf[5] * 100;
	case 10: return strcmp("abc", "abd") < 0 && strcmp("b", "a") > 0 && strcmp("x", "x") == 0;
	case 11: return strcoll("abc", "abd") < 0;
	case 12: n = strxfrm(buf, "hello", 64); return n * 100 + (strcmp(buf, "hello") == 0);
	case 13: memset(buf, 'z', 10); return buf[9] + buf[10];
	}
	return 0;
}

static int bylong(const void *a, const void *b) {
	long x = *(const long *)a, y = *(const long *)b;
	return x < y ? -1 : x > y;
}
static int bystr(const void *a, const void *b) {
	return strcmp(*(char *const *)a, *(char *const *)b);
}
#define NSORT 6
long sorts(int which) {
	long a[50], key, *p, s = 0;
	char *w[6];
	div_t d;
	ldiv_t ld;
	int i;
	srand(which + 1);
	for (i = 0; i < 50; i++)
		a[i] = rand() % 1000 - 500;
	switch (which) {
	case 0: case 1:
		qsort(a, 50, sizeof a[0], bylong);
		for (i = 0; i < 50; i++) s = s * 3 + a[i];
		key = a[17];
		p = bsearch(&key, a, 50, sizeof a[0], bylong);
		return s + (p ? *p : 12345);
	case 2:
		w[0] = "pear"; w[1] = "apple"; w[2] = "fig"; w[3] = "kiwi"; w[4] = "banana"; w[5] = "date";
		qsort(w, 6, sizeof w[0], bystr);
		for (i = 0; i < 6; i++) s = s * 10 + w[i][0] - 'a';
		return s;
	case 3: d = div(-17, 5); return d.quot * 100 + d.rem + abs(-9) * 1000;
	case 4: ld = ldiv(123456789012L, -1000L); return ld.quot * 10000 + ld.rem + labs(-3L);
	case 5: return rand();
	}
	return 0;
}

static jmp_buf env;
static void deep(int n) {
	if (n == 0)
		longjmp(env, 7);
	deep(n - 1);
}
long jumps(int n) {
	volatile long count = 0;
	long kept = 100 + n;
	int r = setjmp(env);
	if (r == 0) {
		count++;
		if (count < 3)
			longjmp(env, 0);	/* returns 1 */
		deep(n);
	}
	if (r == 1 && count < 3) {
		count++;
		longjmp(env, 1);
	}
	return count * 1000 + r * 100 + kept;
}

static volatile sig_atomic_t caught;
static void handler(int sig) {
	caught = sig;
}
int signals(int sig) {
	void (*old)(int);
	caught = 0;
	old = signal(sig, handler);
	if (old == SIG_ERR)
		return -1;
	if (raise(sig) != 0)
		return -2;
	signal(sig, old);
	return caught;
}

#define NTIME 5
long times(int which, char *buf) {
	struct tm t, *g;
	time_t x = 1000000000;
	memset(&t, 0, sizeof t);
	t.tm_year = 101; t.tm_mon = 8; t.tm_mday = 9; t.tm_hour = 1; t.tm_min = 46; t.tm_sec = 40;
	switch (which) {
	case 0: g = gmtime(&x); return ((g->tm_year * 100L + g->tm_mon) * 100 + g->tm_mday) * 100 + g->tm_yday;
	case 1: return strftime(buf, 100, "%Y-%m-%d %H:%M:%S %a %b %j", &t);
	case 2: strcpy(buf, asctime(&t)); return strlen(buf);
	case 3: return (long)difftime(x + 90, x) + (clock() != (clock_t)-1) * 1000;
	case 4: t.tm_isdst = -1; x = mktime(&t); return t.tm_wday * 1000 + t.tm_yday + (x != (time_t)-1) * 10000000;
	}
	return 0;
}

static long vfmt(char *buf, const char *fmt, ...) {
	va_list ap;
	long n;
	va_start(ap, fmt);
	n = vsprintf(buf, fmt, ap);
	va_end(ap);
	return n;
}
#define NSTDIO 4
long stdios(int which, char *buf) {
	FILE *f;
	long n = 0, pos;
	int c, i;
	char line[80];
	double d;
	switch (which) {
	case 0:
		return sprintf(buf, "%d|%5.2f|%-6s|%x|%o|%e|%c|%%|%ld|%lu|%+d|%05d|%.3s|%p",
			-42, 3.14159, "ab", 255U, 8U, 12345.678, 'q', -123456789012L, 4000000000UL, 7, -3, "abcdef", (void *)0);
	case 1:
		n = sscanf("  17 -2.5e1 word 0x1f rest", "%d %lf %s %i", &i, &d, line, &c);
		return n * 1000000 + i * 1000 + (long)d * 10 + strlen(line) + c * 100000L;
	case 2:
		if ((f = tmpfile()) == NULL)
			return -1;
		for (i = 0; i < 5; i++)
			fprintf(f, "line %d of %s\n", i, "five");
		fputs("tail", f);
		fputc('!', f);
		pos = ftell(f);
		rewind(f);
		while (fgets(line, sizeof line, f))
			n = n * 3 + strlen(line);
		c = feof(f) != 0;
		clearerr(f);
		fseek(f, 5, SEEK_SET);
		i = fgetc(f);
		ungetc('Z', f);
		n += (fgetc(f) == 'Z') + i * 10 + c * 1000 + pos * 100000;
		fseek(f, 0, SEEK_SET);
		n += (long)fread(line, 1, 10, f) * 1000000 + ferror(f);
		fclose(f);
		return n;
	case 3:
		return vfmt(buf, "%s-%d-%g-%c", "v", 99, 0.125, 'z');
	}
	return 0;
}

#define NLOCALE 3
long locales(int which, char *buf) {
	struct lconv *lc;
	char *s;
	switch (which) {
	case 0: s = setlocale(LC_ALL, NULL); strcpy(buf, s ? s : "null"); return strlen(buf);
	case 1: s = setlocale(LC_NUMERIC, "C"); strcpy(buf, s ? s : "null"); return strlen(buf);
	case 2: lc = localeconv(); strcpy(buf, lc->decimal_point); return strlen(lc->thousands_sep) + lc->frac_digits;
	}
	return 0;
}

/* limits.h and float.h; long double is double in lcc */
static double limitsv[] = {
	CHAR_BIT, SCHAR_MIN, SCHAR_MAX, UCHAR_MAX, CHAR_MIN, CHAR_MAX, MB_LEN_MAX,
	SHRT_MIN, SHRT_MAX, USHRT_MAX, INT_MIN, INT_MAX, UINT_MAX,
	LONG_MIN, LONG_MAX, ULONG_MAX,
	FLT_RADIX, FLT_MANT_DIG, FLT_EPSILON, FLT_DIG, FLT_MIN_EXP, FLT_MIN,
	FLT_MIN_10_EXP, FLT_MAX_EXP, FLT_MAX, FLT_MAX_10_EXP,
	DBL_MANT_DIG, DBL_EPSILON, DBL_DIG, DBL_MIN_EXP, DBL_MIN, DBL_MIN_10_EXP,
	DBL_MAX_EXP, DBL_MAX, DBL_MAX_10_EXP,
	LDBL_DIG, LDBL_MANT_DIG, LDBL_MAX_10_EXP, LDBL_MAX_EXP, LDBL_MIN_10_EXP, LDBL_MIN_EXP,
	1-FLT_MIN_10_EXP, 1-FLT_MIN_EXP, 1-DBL_MIN_10_EXP, 1-DBL_MIN_EXP, 1-LDBL_MIN_10_EXP, 1-LDBL_MIN_EXP,
	EDOM, ERANGE, EOF, BUFSIZ, FILENAME_MAX, L_tmpnam, TMP_MAX, FOPEN_MAX,
	SEEK_SET, SEEK_CUR, SEEK_END, _IOFBF, _IOLBF, _IONBF, EXIT_SUCCESS, EXIT_FAILURE, RAND_MAX,
	CLOCKS_PER_SEC, SIGABRT, SIGFPE, SIGILL, SIGINT, SIGSEGV, SIGTERM,
	LC_ALL, LC_COLLATE, LC_CTYPE, LC_MONETARY, LC_NUMERIC, LC_TIME,
	sizeof(size_t), sizeof(ptrdiff_t), sizeof(wchar_t), sizeof(jmp_buf), sizeof(fpos_t),
	sizeof(time_t), sizeof(clock_t), sizeof(div_t), sizeof(ldiv_t), sizeof(sig_atomic_t),
	offsetof(struct tm, tm_year), offsetof(div_t, rem), offsetof(struct lconv, decimal_point)
};
#define NLIMITS (sizeof limitsv / sizeof limitsv[0] + 2)
double limit(int i) {
	if (i == NLIMITS - 2)
		return FLT_ROUNDS;	/* not a constant expression */
	if (i == NLIMITS - 1)
		return HUGE_VAL;	/* 1e10000 for lcc, which glibc warns about */
	return limitsv[i];
}

/* the long double limits, which a double cannot hold */
static long double ldlimitsv[] = { LDBL_EPSILON, LDBL_MAX, LDBL_MIN, 1 + LDBL_EPSILON, LDBL_MAX / 3 };
#define NLDLIMITS (sizeof ldlimitsv / sizeof ldlimitsv[0])
long double ldlimit(int i) {
	return ldlimitsv[i];
}

#define NMB 6
long mbs(int which) {
	wchar_t w[8];
	char s[16];
	long n;
	void *p;
	switch (which) {
	case 0: return mblen("x", 1) * 10 + mblen("", 1) + (mblen(NULL, 0) != 0) * 100;
	case 1: n = mbtowc(w, "A", 1); return n * 1000 + w[0];
	case 2: n = mbstowcs(w, "hello", 8); return n * 1000 + w[4];
	case 3: w[0] = 'o'; w[1] = 'k'; w[2] = 0; n = wcstombs(s, w, 16); return n * 1000 + s[1];
	case 4: n = wctomb(s, 'Q'); return n * 1000 + s[0];
	case 5:
		p = calloc(10, sizeof(long));
		n = ((long *)p)[9];
		p = realloc(p, 100 * sizeof(long));
		((long *)p)[99] = 5;
		n += ((long *)p)[99] + (getenv("LCC_SURELY_UNSET_VARIABLE") == NULL) * 10;
		free(p);
		return n;
	}
	return 0;
}
