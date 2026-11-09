#include "c.h"
#include "abi.h"

static char rcsid[] = "$Id$";

/*
 * The AArch64 procedure call standard (AAPCS64) for Linux, as clang
 * passes structures in LLVM IR; LLVM assigns the registers.
 *
 * A homogeneous floating-point aggregate (HFA), a structure or union of
 * one to four floats, doubles or long doubles of the same type, goes in
 * as many consecutive SIMD registers, or on the stack if too few are
 * left: an array of them, aligned on the stack to 8 (16 for long
 * double).  Another structure of at most 16 bytes goes in one or two
 * consecutive general registers (an even pair if it is aligned to 16),
 * or on the stack: i64, [2 x i64] or i128.  A larger one is copied by
 * the caller, which passes the address of the copy.  Variadic arguments
 * are passed the same way.
 *
 * An HFA result comes in SIMD registers, another one of at most 16 bytes
 * in x0 and x1; a larger one is returned at the address in x8 (sret).
 */

/* fltname - the LLVM type of the floating type ty */
static char *fltname(Type ty) {
	return ty->size == 4 ? "float" : ty->size == 8 ? "double" : "fp128";
}

static void initregs(struct regs *r) {
	r->gp = r->fp = 8;
	r->va = r->vfunc = 0;
}

/* scalar - LLVM assigns registers to scalars itself */
static void scalar(char *t, struct regs *r) {
}

static int arg(Type ty, struct regs *r, struct part **part) {
	Type base;
	int n;

	ty = unqual(ty);
	*part = abi_parts(1);
	if ((n = abi_hfa(ty, &base)) > 0) {
		abi_elems(*part, ty, 0, n, base->size, fltname(base));
		(*part)->type = stringf("[%d x %s]", n, fltname(base));
		(*part)->alignstack = base->size > 8 ? 16 : 8;
	} else if (ty->size > 16)
		return ABYREF;
	else if (ty->align > 8)
		abi_elems(*part, ty, 0, 1, 16, "i128");
	else if (ty->size <= 8)
		abi_elems(*part, ty, 0, 1, 8, "i64");
	else
		abi_elems(*part, ty, 0, 2, 8, "i64");
	return 1;
}

static int ret(Type ty, int vfunc, struct part *part) {
	Type base;
	int i, n;

	ty = unqual(ty);
	part->alignstack = 0;
	if ((n = abi_hfa(ty, &base)) > 0) {
		abi_elems(part, ty, 0, n, base->size, fltname(base));
		part->type = "{ ";
		for (i = 0; i < n; i++)
			part->type = stringf("%s%s%s", part->type, i ? ", " : "", fltname(base));
		part->type = stringf("%s }", part->type);
	} else if (ty->size > 16)
		return 0;
	else if (ty->align > 8)
		abi_elems(part, ty, 0, 1, 16, "i128");
	else if (ty->size <= 8)
		abi_elems(part, ty, 0, 1, 8, stringf("i%d", 8*ty->size));
	else
		abi_elems(part, ty, 0, 2, 8, "i64");
	return 1;
}

/*
 * needsslot - whether a va_arg of type ty needs a place for its value:
 * a float arrives as a double, and the elements of an HFA of floats or
 * doubles in registers are 16 bytes apart in the save area
 */
static int needsslot(Type ty) {
	Type base;

	ty = unqual(ty);
	if (isstruct(ty))
		return abi_hfa(ty, &base) > 1 && base->size < 16;
	return isfloat(ty) && ty->size < doubletype->size;
}

struct abi aapcs64abi = { initregs, scalar, arg, ret, needsslot, NULL, 0, 0, 0, 8 };
