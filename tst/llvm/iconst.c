/*
 * Types of integer constants (C89 3.1.3.2).  An unsuffixed octal or
 * hexadecimal constant has the first of int, unsigned int, long and
 * unsigned long that holds its value, a decimal one the first of int,
 * long and unsigned long; a suffix u or l leaves out the types without
 * it.  Compiled by lcc; iconst-drv.c includes this file again with I(n)
 * renaming the objects, compiled by clang, and compares.
 */
#ifndef I
#define I(n) n
#endif
#include "port.h"

/* K(c): twice the size of the type of c, plus 1 if the type is unsigned
   (not with < 0, which lcc does not fold for unsigned constants) */
#define K(c) ((int)sizeof(c) * 2 + ((c) * 0 - 1 > 0))

int I(kinds)[] = {
	K(0), K(00), K(0x0), K(1),
	K(0x7fffffff), K(0x80000000), K(0xffffffff),
	K(017777777777), K(020000000000), K(037777777777),
	K(2147483647), K(LV(2147483648, 2147483647)),
	K(LV(4294967295, 2147483647)), K(LV(9223372036854775807, 2147483647)),
	K(LV(0x100000000, 0x7fffffff)), K(LV(0x7fffffffffffffff, 0x7fffffff)),
	K(LV(0x8000000000000000, 0x80000000)),
	K(LV(0xffffffffffffffff, 0xffffffff)),
	K(LV(040000000000, 017777777777)),
	K(LV(01000000000000000000000, 020000000000)),
	K(0xffffffffu), K(LV(0x100000000U, 0xffffffffU)),
	K(0x7fffffffL), K(0xffffffffl), K(LV(0x8000000000000000L, 0x80000000L)),
	K(0xffffffffUL), K(0x7fffffffLu), K(0uL), K(0Lu)
};

/* the values below depend on the types: with long for the constants
   that are unsigned int, they are 0x100000000, 0x80000001, 1,
   0x100000000 and -0xfffffffe where long has 64 bits */
long I(folded)[] = {
	0xffffffff + 1, -0x80000000 + 1, -1 < 0xffffffff,
	037777777777 + 1, 0x80000000 << 1, 0xfffffffe / -1
};

/* the same at run time; x is 1 for those values */
long I(hexadd)(int x) { return 0xffffffff + x; }
long I(hexneg)(int x) { return -0x80000000 + x; }
long I(hexlt)(int x) { return -x < 0xffffffff; }
long I(octadd)(int x) { return 037777777777 + x; }
long I(hexshl)(int x) { return 0x80000000 << (x & 1); }
long I(hexdiv)(int x) { return 0xfffffffe / -(x | 1); }
