#include "c.h"
#include "sysv.h"

static char rcsid[] = "$Id$";

/*
 * The SysV x86-64 ABI and data formats that the llvm back end uses and
 * other back ends for the same target can share: the classification of
 * structures for arguments and results, the argument registers taken,
 * the places va_arg needs, functions that return twice, and the bits of
 * an x87 long double.  Register parts are named by their LLVM types:
 * "float", "double", "iN" and "x86_fp80".
 */

/*
 * SysV x86-64 passes a structure or union of at most 16 bytes in up to
 * two registers, one for each eightbyte: an SSE register if it holds
 * only floats and doubles, else an integer register.  Larger ones, ones
 * with a long double (classes X87 and X87UP, also MEMORY when merged
 * with another class), and ones for which too few of the 6 integer and
 * 8 SSE argument registers are left, are copied to the stack (byval).
 */
enum { INTEGER = 1, SSE, X87, X87UP, MEMORY };

/* merge - the class of an eightbyte with scalars of classes a and b */
static int merge(int a, int b) {
	if (a == b || b == 0)
		return a;
	if (a == 0)
		return b;
	if (a == MEMORY || b == MEMORY
	|| a == X87 || a == X87UP || b == X87 || b == X87UP)
		return MEMORY;
	if (a == INTEGER || b == INTEGER)
		return INTEGER;
	return SSE;
}

/* classify - merge the classes of the scalars in ty at offset off */
static void classify(Type ty, int off, int cls[]) {
	ty = unqual(ty);
	if (isstruct(ty)) {
		Field f;
		for (f = ty->u.sym->u.s.flist; f; f = f->link)
			classify(f->type, off + f->offset, cls);
	} else if (isarray(ty)) {
		int i;
		for (i = 0; ty->type->size > 0 && i < ty->size/ty->type->size; i++)
			classify(ty->type, off + i*ty->type->size, cls);
	} else if (isfloat(ty) && ty->size > 8) {	/* long double */
		cls[off/8] = merge(cls[off/8], X87);
		cls[off/8 + 1] = merge(cls[off/8 + 1], X87UP);
	} else if (isfloat(ty))
		cls[off/8] = merge(cls[off/8], SSE);
	else
		cls[off/8] = merge(cls[off/8], INTEGER);
}

/* classes - set cls[0..1] to the classes of the structure ty of at
   most 16 bytes; return how many eightbytes it has */
static int classes(Type ty, int cls[]) {
	cls[0] = cls[1] = 0;
	classify(ty, 0, cls);
	return (ty->size + 7)/8;
}

/*
 * sysv_structparts - set part[] to the LLVM types of the registers that pass
 * the structure ty and take them from r; return how many, or 0 if ty
 * goes on the stack.  The last part covers only the bytes left, so
 * that it is not loaded or stored beyond the structure.
 */
int sysv_structparts(Type ty, struct regs *r, char *part[]) {
	int cls[2], i, n, ngp = 0, nfp = 0;

	ty = unqual(ty);
	if (ty->size == 0 || ty->size > 16)
		return 0;
	n = classes(ty, cls);
	for (i = 0; i < n; i++)
		if (cls[i] == X87 || cls[i] == X87UP || cls[i] == MEMORY)
			return 0;
		else if (cls[i] == SSE)
			nfp++;
		else
			ngp++;
	if (ngp > r->gp || nfp > r->fp)
		return 0;
	r->gp -= ngp;
	r->fp -= nfp;
	for (i = 0; i < n; i++) {
		int bytes = ty->size - 8*i < 8 ? ty->size - 8*i : 8;
		if (cls[i] == SSE)
			part[i] = bytes > 4 ? "double" : "float";
		else
			part[i] = stringf("i%d", 8*bytes);
	}
	return n;
}

/* sysv_ssepart - whether the LLVM type t goes in an SSE register */
int sysv_ssepart(char *t) {
	return strcmp(t, "float") == 0 || strcmp(t, "double") == 0;
}

/* sysv_scalarreg - take a register for a scalar argument of LLVM type t */
void sysv_scalarreg(char *t, struct regs *r) {
	if (sysv_ssepart(t)) {
		if (r->fp > 0)
			r->fp--;
	} else if (strcmp(t, "x86_fp80") == 0)
		;	/* class X87: always on the stack */
	else if (r->gp > 0)
		r->gp--;
}

/*
 * A structure result is classified as an argument, and returned in up
 * to two registers (rax and rdx, xmm0 and xmm1), in st0 if it has just a
 * long double, or else in memory at an address passed by the caller
 * (sret).  The
 * front end (wants_callb=0) always passes that address as the first
 * argument; for a result in registers, the back end drops it.
 */

/* sysv_retparts - set part[] to the registers of the structure result ty;
   return how many, or 0 if ty is returned in memory */
int sysv_retparts(Type ty, char *part[]) {
	struct regs r;
	int cls[2];

	ty = unqual(ty);
	if (ty->size == 16 && classes(ty, cls) == 2
	&& cls[0] == X87 && cls[1] == X87UP) {	/* in st0, like a long double */
		part[0] = "x86_fp80";
		return 1;
	}
	r.gp = r.fp = 2;
	return sysv_structparts(ty, &r, part);
}

/*
 * sysv_returnstwice - whether the function named name can return twice, as
 * setjmp does; LLVM must know, since locals may change between the
 * returns.  The names are those that clang knows.
 */
int sysv_returnstwice(char *name) {
	static char *names[] = { "setjmp", "_setjmp", "sigsetjmp",
		"__sigsetjmp", "savectx", "vfork", "getcontext", NULL };
	int i;

	for (i = 0; names[i]; i++)
		if (strcmp(name, names[i]) == 0)
			return 1;
	return 0;
}

/*
 * sysv_needsslot - whether a va_arg of type ty needs a place for its value
 * because it is not in the argument area as it is: a float arrives as a
 * double, and a structure in an SSE register and another register is
 * split in the register save area
 */
int sysv_needsslot(Type ty) {
	struct regs r;
	char *part[2];

	r.gp = 6;
	r.fp = 8;
	if (isstruct(ty))
		return sysv_structparts(ty, &r, part) == 2
			&& (sysv_ssepart(part[0]) || sysv_ssepart(part[1]));
	return isfloat(ty) && ty->size < doubletype->size;
}

/*
 * sysv_fp80bits - the x87 80-bit form of d, the type of a long double:
 * *se gets the sign and the 15-bit biased exponent, *hi and *lo the
 * 64-bit significand with its leading 1.  It is computed with long
 * double arithmetic, not taken from the host's representation; a NaN
 * gives the quiet one.
 */
void sysv_fp80bits(long double d, unsigned *se, unsigned long *hi, unsigned long *lo) {
	int sign = 0, e = 0;

	*hi = *lo = 0;
	if (d != d) {
		*se = 0x7FFF;
		*hi = 0xC0000000UL;
		return;
	}
	if (d < 0 || d == 0 && 1/d < 0) {
		sign = 1;
		d = -d;
	}
	if (d - d != 0)	/* infinity */
		e = 0x7FFF, *hi = 0x80000000UL;
	else if (d != 0) {
		long double m;
		e = 16383;	/* bias; d = m * 2**(e - bias), 1 <= m < 2 */
		while (d >= 2) {
			d /= 2;
			e++;
		}
		while (d < 1 && e > 1) {
			d *= 2;
			e--;
		}
		if (d < 1)	/* subnormal: exponent field 0, scale 2**(1 - bias) */
			e = 0;
		m = d*2147483648.0;	/* 2**31: the top 32 bits */
		*hi = (unsigned long)m;
		*lo = (unsigned long)((m - *hi)*4294967296.0);
	}
	*se = sign<<15 | e;
}
