/*
 * switch statements: dense cases that make jump tables, with gaps that
 * go to default, sparse ones that make comparisons, both in one switch
 * (several tables), no default, fall through, negative and unsigned
 * values, values near the ends of long, char, short and unsigned
 * selectors, a selector read from a global, nested switches,
 * switches in loops with break and continue, a dense switch that no
 * path reaches and one whose cases go back to it by goto.  Compiled by lcc;
 * switch-drv.c includes this file again, compiled by clang, and
 * compares the results.
 */
#include "port.h"
int swglobal;

int dense(int x) {
	switch (x) {
	case 0: return 10; case 1: return 11; case 2: return 12; case 3: return 13;
	case 4: return 14; case 5: return 15; case 6: return 16; case 8: return 18;
	case 9: return 19; case 11: return 21;
	default: return -1;
	}
}
int nodefault(int x) {
	int r = 7;
	switch (x) {
	case -5: r = 1; break; case -4: r = 2; break; case -3: r = 3; break;
	case -2: r = 4; break; case -1: r = 5; break; case 0: r = 6; break;
	}
	return r;
}
int fallthrough(int x) {
	int r = 0;
	switch (x) {
	case 1: r += 1;
	case 2: r += 2;
	case 3: r += 4;
	case 4: r += 8; break;
	case 5: r += 16;
	default: r += 32;
	case 6: r += 64;
	}
	return r;
}
/* clusters far apart: several tables joined by comparisons */
int clusters(int x) {
	switch (x) {
	case 1: case 2: return 1; case 3: return 2; case 4: case 5: return 3;
	case 100: return 4; case 101: return 5; case 102: return 6; case 103: return 7; case 104: return 8;
	case 1000: return 9;
	case 5000: return 10; case 5001: return 11; case 5002: return 12; case 5003: return 13;
	case -1000000: return 14;
	default: return 0;
	}
}
int sparse(long x) {
	switch (x) {
	case 7: return 1; case 1000: return 2; case 123456789L: return 3;
	case -987654321L: return 4; case LONG_MAX: return 5;
	case LONG_MIN: return 6;
	}
	return 0;
}
int longends(long x) {
	switch (x) {
	case LONG_MAX - 4: return 1; case LONG_MAX - 3: return 2;
	case LONG_MAX - 2: return 3; case LONG_MAX - 1: return 4;
	case LONG_MAX: return 5;
	case LONG_MIN: return 6; case LONG_MIN + 1: return 7;
	case LONG_MIN + 2: return 8; case LONG_MIN + 3: return 9;
	}
	return 0;
}
int ulongsw(unsigned long x) {
	switch (x) {
	case 0: return 1; case 1: return 2; case 2: return 3; case 3: return 4; case 4: return 5;
	case ULONG_MAX - 1: return 6; case ULONG_MAX: return 7;
	case ULONG_MAX - 2: return 8; case ULONG_MAX - 3: return 9;
	case ULONG_MAX / 2 + 1: return 10;
	}
	return 0;
}
int unsignedsw(unsigned x) {
	switch (x) {
	case 0xfffffffaU: return 1; case 0xfffffffbU: return 2; case 0xfffffffcU: return 3;
	case 0xfffffffdU: return 4; case 0xfffffffeU: return 5; case 0xffffffffU: return 6;
	case 0x80000000U: return 7; case 0x7fffffffU: return 8;
	}
	return 0;
}
int charsw(char c) {
	switch (c) {
	case 'a': case 'e': case 'i': case 'o': case 'u': return 1;
	case 'b': case 'c': case 'd': case 'f': case 'g': return 2;
	case '0': case '1': case '2': case '3': case '4': case '5': return 3;
	case CHAR_MIN: case CHAR_MIN + 1: case CHAR_MIN + 2: case CHAR_MIN + 3: return 4;
	case CHAR_MAX: case CHAR_MAX - 1: return 5;
	}
	return 0;
}
int ucharsw(unsigned char c) {
	switch (c) {
	case 0: case 1: case 2: case 3: return 1;
	case 252: case 253: case 254: case 255: return 2;
	}
	return 0;
}
int shortsw(short s) {
	switch (s) {
	case -32768: return 1; case -32767: return 2; case -32766: return 3; case -32765: return 4;
	case 32767: return 5; case 32766: return 6; case 32765: return 7; case 32764: return 8;
	case 0: return 9;
	}
	return 0;
}
enum color { red, green = 5, blue, cyan, magenta, yellow };
int enumsw(enum color c) {
	switch (c) {
	case red: return 1; case green: return 2; case blue: return 3;
	case cyan: return 4; case magenta: return 5; case yellow: return 6;
	}
	return 0;
}
/* the selector and the index computation read a global */
int globalsw(void) {
	switch (swglobal) {
	case 1: return 10; case 2: return 20; case 3: return 30; case 4: return 40; case 5: return 50;
	}
	return 0;
}
int exprsw(int a, int b) {
	switch (a * 3 + b) {
	case 0: return a; case 1: return b; case 2: return a + b; case 3: return a - b;
	case 4: return a * b; case 5: return -a;
	default: return 99;
	}
}
int nested(int a, int b) {
	switch (a) {
	case 0: case 1: case 2:
		switch (b) {
		case 0: return 100 + a; case 1: return 110 + a; case 2: return 120 + a;
		case 3: return 130 + a; case 4: return 140 + a;
		}
		return 150 + a;
	case 3: case 4: case 5: case 6:
		switch (b + a) {
		case 3: case 4: case 5: break;
		case 6: case 7: case 8: return 200 + b;
		default: return 300 + b;
		}
		return 400 + a;
	}
	return -1;
}
/* a small interpreter: a switch in a loop with break and continue */
long run(const char *prog, long x) {
	long acc = x, n = 0;
	for (; *prog; prog++) {
		switch (*prog) {
		case '+': acc += 1; break;
		case '-': acc -= 1; break;
		case '*': acc *= 2; break;
		case '/': acc /= 2; break;
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			acc = acc * 10 + (*prog - '0'); continue;
		case 'n': n++; continue;
		case '.': return acc + 1000 * n;
		default: acc ^= *prog;
		}
		n += acc & 1;
	}
	return acc - 1000 * n;
}
/* a dense switch after return: its table and cases are never reached */
int deadsw(int x) {
	return x + 1;
	switch (x) {
	case 1: x = 10; break; case 2: x = 20; break; case 3: x = 30; break;
	case 4: x = 40; break; case 5: x = 50; break; case 6: x = 60; break;
	}
	return x;
}
/* cases that go back to the switch */
int gotosw(int x) {
	int n = 0;
again:
	n++;
	switch (x) {
	case 0: x = 3; goto again; case 1: x = 0; goto again; case 2: x = 5; goto again;
	case 3: x = 7; break; case 4: x = 1; goto again; case 5: return -n;
	}
	return x + 100*n;
}
