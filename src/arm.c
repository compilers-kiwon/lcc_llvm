#include "c.h"
#include "abi.h"

static char rcsid[] = "$Id$";

/*
 * The ARM procedure call standard with VFP arguments (AAPCS-VFP, armhf
 * Linux), as clang passes structures in LLVM IR; LLVM assigns the
 * registers.
 *
 * A homogeneous floating-point aggregate (HFA, abi_hfa) goes in VFP
 * registers as a literal structure of its elements, and is returned so.
 * Another structure goes in core registers and on the stack as
 * [N x i32], or [N x i64] if it is aligned to 8, unless it has more than
 * 64 bytes: then it is copied to the stack (byval).  One of at most 4
 * bytes is returned in r0 as i8, i16 or i32, a larger one at an address
 * the caller passes (sret).  A variadic function takes and returns all
 * its arguments, also the fixed ones, as the base standard does, without
 * VFP registers: no HFA.  Variadic arguments go in core registers, so a
 * call without a prototype passes its arguments as fixed ones, as clang
 * does.  A char or short result is extended to 32 bits by the callee.
 */

static void initregs(struct regs *r) {
	r->gp = 4;
	r->fp = 16;
	r->va = r->vfunc = 0;
}

static void scalar(char *t, struct regs *r) {
}

/* fltname - the LLVM type of the floating type ty */
static char *fltname(Type ty) {
	return ty->size == 4 ? "float" : "double";
}

/* hfapart - set part to the n elements of type base of the HFA ty */
static void hfapart(struct part *part, Type ty, Type base, int n) {
	int i;

	abi_elems(part, ty, 0, n, base->size, fltname(base));
	part->type = "{ ";
	for (i = 0; i < n; i++)
		part->type = stringf("%s%s%s", part->type, i ? ", " : "", fltname(base));
	part->type = stringf("%s }", part->type);
}

static int arg(Type ty, struct regs *r, struct part **part) {
	Type base;
	int n, unit;

	ty = unqual(ty);
	*part = abi_parts(1);
	if (!r->vfunc && (n = abi_hfa(ty, &base)) > 0) {
		hfapart(*part, ty, base, n);
		return 1;
	}
	if (ty->size > 64)
		return ABYVAL;
	unit = ty->align > 4 ? 8 : 4;
	n = (ty->size + unit - 1)/unit;
	abi_elems(*part, ty, 0, n, unit, unit == 8 ? "i64" : "i32");
	(*part)->type = stringf("[%d x %s]", n, unit == 8 ? "i64" : "i32");
	return 1;
}

static int ret(Type ty, int vfunc, struct part *part) {
	Type base;
	int n;

	ty = unqual(ty);
	part->alignstack = 0;
	if (!vfunc && (n = abi_hfa(ty, &base)) > 0) {
		hfapart(part, ty, base, n);
		return 1;
	}
	if (ty->size > 4)
		return 0;
	abi_elems(part, ty, 0, 1, 4,
		ty->size == 1 ? "i8" : ty->size == 2 ? "i16" : "i32");
	return 1;
}

/* needsslot - a float va_arg arrives as a double; the rest is in place */
static int needsslot(Type ty) {
	ty = unqual(ty);
	return isfloat(ty) && ty->size < doubletype->size;
}

struct abi armabi = { initregs, scalar, arg, ret, needsslot, NULL, 1, 0, 1, 4 };
