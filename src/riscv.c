#include "c.h"
#include "abi.h"

static char rcsid[] = "$Id$";

/*
 * The RISC-V calling convention for LP64D (rv64gc Linux, riscv64abi) and
 * ILP32D (rv32gc Linux, riscv32abi), as clang passes structures in LLVM
 * IR, with XLEN 8 or 4 and FLEN 8.
 *
 * A structure whose scalars, nested structures and arrays flattened, are
 * one float or double, two of them, or one of them and one integer of
 * at most XLEN bytes (each bit field counts, as its unit) goes in that many
 * FP and integer registers, one LLVM argument each, if they are left
 * and it is not a variadic argument; its result is that scalar or a
 * literal structure of the two.  Others go by the integer convention: at
 * most XLEN bytes in i64 (i32 on RV32), at most 2*XLEN in [2 x i64] (i128
 * if aligned to 16; [2 x i32] and i64 on RV32), else by the address of a
 * copy made by the caller, and returned at an address the caller passes
 * (sret); so is a scalar of more than 2*XLEN bytes, a long double on RV32.  Unions and pointers never
 * go in FP registers.  LLVM assigns the registers; they are counted
 * here only to decide between the conventions.  On RV64, arguments of
 * type i32 are sign-extended to XLEN, unsigned ones too (signext).  Variadic
 * arguments go in integer registers, so a call without a prototype
 * passes its (promoted) arguments as fixed ones, as clang does.
 */

#define FLEN 8

static int xlen = 8;	/* XLEN: 8, or 4 while riscv32abi works */
#define XLEN xlen

struct leaf {
	int off, size, fp;
};

/* flatten - add the scalars of ty at off to l[0..*n-1]; return 0 if ty
   cannot go in FP registers */
static int flatten(Type ty, int off, struct leaf l[], int *n) {
	ty = unqual(ty);
	if (ty->op == UNION)
		return 0;
	if (isstruct(ty)) {
		Field f;
		for (f = ty->u.sym->u.s.flist; f; f = f->link)
			if (!flatten(f->type, off + f->offset, l, n))
				return 0;	/* each bit field counts, as its unit */
		return 1;
	}
	if (isarray(ty)) {
		int i;
		if (ty->type->size == 0)
			return 0;
		for (i = 0; i < ty->size/ty->type->size; i++)
			if (!flatten(ty->type, off + i*ty->type->size, l, n))
				return 0;
		return 1;
	}
	if (*n == 2)
		return 0;
	if (isfloat(ty) && ty->size <= FLEN)
		l[*n].fp = 1;
	else if ((isint(ty) || isenum(ty)) && ty->size <= XLEN)
		l[*n].fp = 0;
	else
		return 0;
	l[*n].off = off;
	l[*n].size = ty->size;
	(*n)++;
	return 1;
}

/* fpleaves - set l[] to the scalars of ty if it goes in FP registers
   (when they are left), and return how many, else 0 */
static int fpleaves(Type ty, struct leaf l[]) {
	int n = 0;

	if (ty->size == 0 || !flatten(ty, 0, l, &n) || n == 0)
		return 0;
	if (!l[0].fp && (n == 1 || !l[1].fp))
		return 0;	/* no float: integers only */
	return n;
}

static char *leaftype(struct leaf *l) {
	if (l->fp)
		return l->size == 4 ? "float" : "double";
	return stringf("i%d", 8*l->size);
}

static void initregs(struct regs *r) {
	r->gp = r->fp = 8;
	r->va = r->vfunc = 0;
}

static void takegp(struct regs *r, int n) {
	r->gp = r->gp > n ? r->gp - n : 0;
}

static void scalar(char *t, struct regs *r) {
	int size = t[0] == 'i' ? atoi(t + 1)/8 : strcmp(t, "float") == 0 ? 4
		: strcmp(t, "double") == 0 ? 8 : strcmp(t, "fp128") == 0 ? 16 : XLEN;

	if ((strcmp(t, "float") == 0 || strcmp(t, "double") == 0)
	&& !r->va && r->fp > 0)
		r->fp--;
	else if (size > 2*XLEN)
		takegp(r, 1);	/* by address */
	else
		takegp(r, (size + XLEN - 1)/XLEN);
}

/* intconv - the part of ty by the integer convention, or 0 if it is
   passed by address */
static int intconv(Type ty, struct part *part) {
	if (ty->size > 2*XLEN)
		return 0;
	if (ty->align > XLEN)
		abi_elems(part, ty, 0, 1, 2*XLEN, XLEN == 8 ? "i128" : "i64");
	else if (ty->size <= XLEN)
		abi_elems(part, ty, 0, 1, XLEN, XLEN == 8 ? "i64" : "i32");
	else
		abi_elems(part, ty, 0, 2, XLEN, XLEN == 8 ? "i64" : "i32");
	return 1;
}

static int arg(Type ty, struct regs *r, struct part **part) {
	struct leaf l[2];
	int i, n, nfp = 0;

	ty = unqual(ty);
	if ((n = fpleaves(ty, l)) > 0) {
		for (i = 0; i < n; i++)
			nfp += l[i].fp;
		if (!r->va && nfp <= r->fp && n - nfp <= r->gp) {
			r->fp -= nfp;
			r->gp -= n - nfp;
			*part = abi_parts(n);
			for (i = 0; i < n; i++)
				abi_elems(&(*part)[i], ty, l[i].off, 1, l[i].size, leaftype(&l[i]));
			return n;
		}
	}
	*part = abi_parts(1);
	if (!intconv(ty, *part)) {
		takegp(r, 1);
		return ABYREF;
	}
	takegp(r, (ty->size + XLEN - 1)/XLEN);
	return 1;
}

static int ret(Type ty, int vfunc, struct part *part) {
	struct leaf l[2];
	int i, n;

	ty = unqual(ty);
	part->alignstack = 0;
	if ((n = fpleaves(ty, l)) > 0) {
		part->n = n;
		part->e = newarray(n, sizeof *part->e, FUNC);
		for (i = 0; i < n; i++) {
			part->e[i].off = l[i].off;
			part->e[i].size = l[i].size;
			part->e[i].type = leaftype(&l[i]);
		}
		part->type = n == 1 ? part->e[0].type
			: stringf("{ %s, %s }", part->e[0].type, part->e[1].type);
		return 1;
	}
	return intconv(ty, part);
}

/* needsslot - a float va_arg arrives as a double; the rest is in place */
static int needsslot(Type ty) {
	ty = unqual(ty);
	return isfloat(ty) && ty->size < doubletype->size;
}

/* extattr - i32 is sign-extended to XLEN, also if unsigned */
static char *extattr(char *t) {
	return strcmp(t, "i32") == 0 ? "signext" : NULL;
}

struct abi riscv64abi = { initregs, scalar, arg, ret, needsslot, extattr, 1, 0, 1, 8 };

/* riscv32abi: the same with XLEN 4, and no extension of i32 */

static void scalar32(char *t, struct regs *r) {
	xlen = 4;
	scalar(t, r);
	xlen = 8;
}

static int arg32(Type ty, struct regs *r, struct part **part) {
	int n;

	xlen = 4;
	n = arg(ty, r, part);
	xlen = 8;
	return n;
}

static int ret32(Type ty, int vfunc, struct part *part) {
	int n;

	xlen = 4;
	n = ret(ty, vfunc, part);
	xlen = 8;
	return n;
}

struct abi riscv32abi = { initregs, scalar32, arg32, ret32, needsslot, NULL, 1, 0, 1, 4 };
