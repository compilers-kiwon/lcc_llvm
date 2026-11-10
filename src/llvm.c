#include "c.h"
#include "abi.h"
#include "target.h"

/*
 * LLVM back end: emits textual LLVM IR (.ll) for the targets in
 * target.h, as llvm/x86_64-linux.
 *
 * Locals and parameters live in allocas in the entry block; mem2reg
 * turns them into SSA values.  The LLVM value of each dag node is kept
 * in a table local to this file, so Xnode is not touched beyond what
 * the front end itself uses.
 */

#define I(f) llvm_##f
static char rcsid[] = "$Id$";

#define xx(id, triple, layout, ldtype, mlirld, lsize, ldsize, psize, uchar, quad, ubf, abi) \
extern Interface I(id##IR);
TARGETS
#undef xx
static struct target targets[] = {
#define xx(id, triple, layout, ldtype, mlirld, lsize, ldsize, psize, uchar, quad, ubf, abi) \
	{ &I(id##IR), triple, layout, ldtype, mlirld, abi },
TARGETS
#undef xx
};
static struct target *tg;	/* the target */

/* initialized global data, collected until the global is complete */
enum { Kconst, Kstring, Kspace, Kaddr };
typedef struct item *Item;
struct item {
	int kind;
	char *type, *val;	/* Kconst; val also for Kaddr */
	char *s;		/* Kstring */
	int n;			/* Kstring, Kspace */
	Item link;
};
typedef struct gdata *Gdata;
struct gdata {
	char *name, *linkage, *type;
	int constant, size, align;
	Symbol sym;
	Item items, *tail;
	Gdata link;
};
static Gdata pending;			/* global being initialized */
static Gdata deferred, *dtail = &deferred; /* completed while in a function */
static int infunc;

/* LLVM value of each node in the current function */
#define NVHASH 512
struct nodeval {
	Node node;
	char *val;
	struct nodeval *link;
};
static struct nodeval *nodevals[NVHASH];

/* va_arg calls of the current function and the places for their values,
   and the places of the copies of structure arguments passed by address */
struct vaslot {
	Node call;
	char *name;
	struct vaslot *link;
};
static struct vaslot *vaslots;

/* addresses of locals computed by address(), emitted after the allocas */
struct laddr {
	Symbol q, p;
	long n;
	struct laddr *link;
};
static struct laddr *laddrs, **ladtail;

/* arguments listed since the last call; they all belong to the next CALL */
struct arg {
	char *type, *val;
	Type sty;	/* a structure: val is its address, type is NULL */
	char *attr;	/* attributes, as byval */
	int fixed;	/* matches a parameter, not ... */
	Node node;	/* the ARG */
	struct arg *link;
};
static struct arg *args, **argtail = &args;

static int nreg, nblock, nlocal;
static int terminated;		/* current basic block has its terminator */
static char *rettype;		/* LLVM return type of the current function */
static int usedmemcpy;		/* llvm.memcpy needs a declaration */
static int usedva;		/* llvm.va_start and va_end need declarations */
static char *fname;		/* LLVM name of the current function */
static Node emitting;		/* the forest being emitted */

static char *gname(Symbol);

static void unsupported(Node p) {
	error("llvm back end: unsupported operator `%s'\n", opname(p->op));
}

/* startline - start an instruction, opening a new basic block if needed */
static void startline(void) {
	if (terminated) {
		print("b%d:\n", ++nblock);
		terminated = 0;
	}
	print("  ");
}

/* inst - emit one instruction */
static void inst(const char *fmt, ...) {
	va_list ap;

	startline();
	va_start(ap, fmt);
	vfprint(stdout, NULL, fmt, ap);
	va_end(ap);
	print("\n");
}

static char *newreg(void) {
	return stringf("%%t%d", ++nreg);
}

static char *lookupval(Node p) {
	struct nodeval *e;

	for (e = nodevals[((unsigned long)p>>4)&(NVHASH-1)]; e; e = e->link)
		if (e->node == p)
			return e->val;
	return NULL;
}

static void setval(Node p, char *val) {
	struct nodeval *e;
	unsigned h = ((unsigned long)p>>4)&(NVHASH-1);

	NEW(e, FUNC);
	e->node = p;
	e->val = val;
	e->link = nodevals[h];
	nodevals[h] = e;
}

/* flttypestr - LLVM type of a floating type of size bytes; a long
   double wider than double has the target's type */
static char *flttypestr(int size) {
	return size == 4 ? "float" : size == 8 ? "double" : tg->ldtype;
}

/* optypestr - LLVM type of operator op's type suffix */
static char *optypestr(int op) {
	switch (optype(op)) {
	case I: case U: return stringf("i%d", 8*opsize(op));
	case F: return flttypestr(opsize(op));
	case P: return "ptr";
	case V: return "void";
	}
	assert(0);
	return NULL;
}

/* intop - LLVM instruction for the integer binary operator op */
static char *intop(int op) {
	switch (generic(op)) {
	case ADD:  return "add";
	case SUB:  return "sub";
	case MUL:  return "mul";
	case DIV:  return optype(op) == U ? "udiv" : "sdiv";
	case MOD:  return optype(op) == U ? "urem" : "srem";
	case BAND: return "and";
	case BOR:  return "or";
	case BXOR: return "xor";
	}
	assert(0);
	return NULL;
}

/* typestr - LLVM type for objects of type ty */
static char *typestr(Type ty) {
	ty = unqual(ty);
	switch (ty->op) {
	case INT: case UNSIGNED: case ENUM:
		return stringf("i%d", 8*ty->size);
	case FLOAT:
		return flttypestr(ty->size);
	case POINTER: case FUNCTION:
		return "ptr";
	case VOID:
		return "void";
	default:
		return stringf("[%d x i8]", ty->size);
	}
}

/* argtypestr - LLVM type of a scalar argument of type ty */
static char *argtypestr(Type ty) {
	assert(!isstruct(ty));
	if (isint(ty) || isenum(ty))
		return typestr(promote(ty));
	return typestr(ty);
}

/* byvalstr - attributes of a pointer to the structure ty on the stack */
static char *byvalstr(Type ty) {
	return stringf("byval([%d x i8]) align %d", ty->size,
		ty->align < tg->abi->byvalalign ? tg->abi->byvalalign : ty->align);
}

/* sretstr - attributes of the address of the structure result ty */
static char *sretstr(Type ty) {
	return stringf("sret([%d x i8]) align %d", ty->size, ty->align);
}

/* elemalign - the alignment of offset off in an object aligned to align */
static int elemalign(int off, int align) {
	return off > 0 && (off & -off) < align ? off & -off : align;
}

/* loadpart - load part p of the structure at addr, aligned to align */
static char *loadpart(struct part *p, char *addr, int align) {
	char *v = "poison";
	int i;

	for (i = 0; i < p->n; i++) {
		struct elem *e = &p->e[i];
		char *x = newreg(), *a = addr, *m = abi_memtype(e);
		if (e->off > 0) {
			a = newreg();
			inst("%s = getelementptr i8, ptr %s, i64 %d", a, addr, e->off);
		}
		inst("%s = load %s, ptr %s, align %d", x, m, a, elemalign(e->off, align));
		if (m != e->type) {
			char *y = newreg();
			inst("%s = zext %s %s to %s", y, m, x, e->type);
			x = y;
		}
		if (!abi_isagg(p))
			return x;
		{
			char *w = newreg();
			inst("%s = insertvalue %s %s, %s %s, %d", w, p->type, v, e->type, x, i);
			v = w;
		}
	}
	return v;
}

/* storepart - store v, the value of part p, in the structure at addr */
static void storepart(struct part *p, char *v, char *addr, int align) {
	int i;

	for (i = 0; i < p->n; i++) {
		struct elem *e = &p->e[i];
		char *x = v, *a = addr, *m = abi_memtype(e);
		if (abi_isagg(p)) {
			x = newreg();
			inst("%s = extractvalue %s %s, %d", x, p->type, v, i);
		}
		if (e->off > 0) {
			a = newreg();
			inst("%s = getelementptr i8, ptr %s, i64 %d", a, addr, e->off);
		}
		if (m != e->type) {
			char *y = newreg();
			inst("%s = trunc %s %s to %s", y, e->type, x, m);
			x = y;
		}
		inst("store %s %s, ptr %s, align %d", m, x, a, elemalign(e->off, align));
	}
}

/* partattr - the attributes of part p, or NULL */
static char *partattr(struct part *p) {
	return p->alignstack ? stringf("alignstack(%d)", p->alignstack) : NULL;
}

/* partstr - the type and attributes of part p */
static char *partstr(struct part *p) {
	return p->alignstack ? stringf("%s %s", p->type, partattr(p)) : p->type;
}

/* extattr - the extension attribute of a scalar of LLVM type t, with a
   space before it, or "" */
static char *extattr(char *t) {
	char *a = tg->abi->extattr ? (*tg->abi->extattr)(t) : NULL;

	return a ? stringf(" %s", a) : "";
}

/* rettypestr - LLVM return type of function type fty */
static char *rettypestr(Type fty) {
	Type rty = freturn(fty);

	if (isstruct(rty)) {
		struct part part;
		return (*tg->abi->ret)(rty, variadic(fty), &part) ? part.type : "void";
	}
	if (isint(rty) || isenum(rty))
		rty = promote(rty);
	return typestr(rty);
}

/* intstr - integer constant u of size bytes, as a signed decimal */
static char *intstr(unsigned long u, int size) {
	int bits = 8*size;

	if (bits < 8*sizeof u) {
		u &= ones(bits);
		if (u>>(bits - 1))
			u |= ~ones(bits);
	}
	return stringf("%D", (long)u);
}

/* hexdigits - put n hex digits of u into buf[i..i+n-1] */
static void hexdigits(char *buf, int i, int n, unsigned long u) {
	while (n-- > 0) {
		buf[i + n] = "0123456789ABCDEF"[u&0xF];
		u >>= 4;
	}
}

/* fp80str - d as an x87 80-bit constant: 0xK, then the sign and the
   15-bit biased exponent, then the 64-bit significand with its leading 1 */
static char *fp80str(long double d) {
	char buf[24];
	unsigned se;
	unsigned long hi, lo;

	sysv_fp80bits(d, &se, &hi, &lo);
	hexdigits(buf, 3, 4, se);
	hexdigits(buf, 7, 8, hi);
	hexdigits(buf, 15, 8, lo);
	buf[0] = '0', buf[1] = 'x', buf[2] = 'K', buf[23] = 0;
	return string(buf);
}

/* fp128str - q as an IEEE quad constant: 0xL, then the low 64 bits and
   the high 64 bits */
static char *fp128str(Quad q) {
	char buf[36];
	unsigned long w[4];

	abi_fp128bits(q, w);
	hexdigits(buf, 3, 8, w[2]);
	hexdigits(buf, 11, 8, w[3]);
	hexdigits(buf, 19, 8, w[0]);
	hexdigits(buf, 27, 8, w[1]);
	buf[0] = '0', buf[1] = 'x', buf[2] = 'L', buf[35] = 0;
	return string(buf);
}

/* floatstr - floating constant v of size bytes, in LLVM's hex form; a
   quad long double is v.q */
static char *floatstr(Value v, int size) {
	static union { int x; char endian; } little = { 1 };
	long double d = v.d;
	double x = size == 4 ? (double)(float)d : (double)d;
	unsigned char b[sizeof x];
	char buf[2*sizeof x + 3], *s = buf;
	int i;

	if (size > 8)
		return IR->quadld ? fp128str(v.q) : fp80str(d);
	memcpy(b, &x, sizeof x);
	*s++ = '0';
	*s++ = 'x';
	for (i = 0; i < (int)sizeof x; i++) {
		int c = b[little.endian ? sizeof x - 1 - i : i];
		*s++ = "0123456789ABCDEF"[c>>4];
		*s++ = "0123456789ABCDEF"[c&0xF];
	}
	*s = 0;
	return string(buf);
}

static char *ptrstr(void *p) {
	if (p == NULL)
		return "null";
	return stringf("inttoptr (i64 %D to ptr)", (long)p);
}

static char *cnststr(int op, Value v) {
	switch (optype(op)) {
	case I: return intstr(v.i, opsize(op));
	case U: return intstr(v.u, opsize(op));
	case P: return ptrstr(v.p);
	case F: return floatstr(v, opsize(op));
	}
	assert(0);
	return NULL;
}

/* gname - LLVM name of symbol p, defining it if necessary */
static char *gname(Symbol p) {
	if (p->x.name == NULL)
		(*IR->defsymbol)(p);
	assert(p->x.name);
	return p->x.name;
}

static char *localname(Symbol p) {
	if (*p->name >= '0' && *p->name <= '9')
		return stringf("%%T%s.%d", p->name, ++nlocal);
	return stringf("%%%s.%d", p->name, ++nlocal);
}

static void emitalloca(Symbol p) {
	inst("%s = alloca %s, align %d", p->x.name, typestr(p->type),
		p->type->align);
}

static char *nodeval(Node), *emitcall(Node);

/*
 * builtin - which stdarg function the call p is to, or 0.  stdarg.h
 * declares __builtin_va_start and __builtin_va_end, and the front end
 * makes __builtin_va_arg(ap, T) a call to a __builtin_va_arg returning
 * T *.  They take the va_list as their only argument.
 */
enum { VASTART = 1, VAARG, VAEND };
static int builtin(Node p) {
	char *name;

	if (specific(p->kids[0]->op) != ADDRG+P)
		return 0;
	name = p->kids[0]->syms[0]->name;
	if (strncmp(name, "__builtin_va_", 13) != 0)
		return 0;
	if (strcmp(name + 13, "start") == 0)
		return VASTART;
	if (strcmp(name + 13, "arg") == 0)
		return VAARG;
	if (strcmp(name + 13, "end") == 0)
		return VAEND;
	return 0;
}

static char *vaslot(Node p) {
	struct vaslot *s;

	for (s = vaslots; s; s = s->link)
		if (s->call == p)
			return s->name;
	return NULL;
}

/* newslot - make an entry block place of type ty for node p */
static void newslot(Node p, char *name, Type ty) {
	struct vaslot *s;

	NEW(s, FUNC);
	s->call = p;
	s->name = stringf("%%%s%d", name, ++nlocal);
	s->link = vaslots;
	vaslots = s;
	inst("%s = alloca %s, align %d", s->name, typestr(ty), ty->align);
}

/*
 * allocvaslots - make an entry block place for each va_arg call in dag
 * p that needs one, and for each structure argument passed by the
 * address of a copy.  The va_arg call returns the address of its value.
 */
static void allocvaslots(Node p) {
	Type ty;
	struct regs r;
	struct part *part;

	if (p == NULL)
		return;
	if (generic(p->op) == CALL && builtin(p) == VAARG && vaslot(p) == NULL
	&& (*tg->abi->needsslot)(ty = unqual(freturn(p->syms[0]->type)->type)))
		newslot(p, "va", ty);
	if (generic(p->op) == ARG && optype(p->op) == B && vaslot(p) == NULL) {
		/* by address with the registers all left, or none (on RV32,
		   {double, double} goes in FP registers or by address) */
		int n;
		(*tg->abi->init)(&r);
		n = (*tg->abi->arg)(p->syms[0]->type, &r, &part);
		(*tg->abi->init)(&r);
		r.gp = r.fp = 0;
		r.va = 1;
		if (n == ABYREF || (*tg->abi->arg)(p->syms[0]->type, &r, &part) == ABYREF)
			newslot(p, "cp", p->syms[0]->type);
	}
	allocvaslots(p->kids[0]);
	allocvaslots(p->kids[1]);
}

/*
 * vaaddr - emit the SysV va_arg for an argument of type ty, and return
 * its address.  The va_list ap has gp_offset at 0, fp_offset at 4,
 * overflow_arg_area at 8 and reg_save_area at 16; the register save
 * area has the 6 integer registers, 8 bytes each, and then the 8 SSE
 * registers, 16 bytes each.  An argument is in registers if all it
 * needs are left, else on the stack; a long double (class X87) always
 * is, and arguments aligned to 16 are so there.  The value is used where it is,
 * except that a structure in an SSE register and another register is
 * put together in slot.  LLVM's va_arg instruction is not used: LLVM 18
 * at -O0 miscompiles it outside the entry block.
 */
static char *vaaddr(char *ap, Type ty, char *slot) {
	char *part[2], *gp = NULL, *fp = NULL, *fpp = NULL, *c = NULL;
	char *rsap, *rsa, *ra, *ovp, *ov, *ov2, *a;
	int i, n, ngp = 0, nfp = 0, size = roundup(ty->size, 8);
	int reg = 0, mem, done;
	struct regs r;

	if (isstruct(ty)) {
		r.gp = 6;
		r.fp = 8;
		n = sysv_structparts(ty, &r, part);
	} else if (isfloat(ty) && ty->size > 8)
		n = 0;	/* long double, class X87: always on the stack */
	else {	/* promoted */
		n = 1;
		part[0] = isfloat(ty) ? "double" : "i64";
	}
	for (i = 0; i < n; i++)
		if (sysv_ssepart(part[i]))
			nfp++;
		else
			ngp++;
	if (ngp > 0) {
		gp = newreg();
		inst("%s = load i32, ptr %s", gp, ap);
		c = newreg();
		inst("%s = icmp ule i32 %s, %d", c, gp, 6*8 - 8*ngp);
	}
	if (nfp > 0) {
		char *c2 = newreg();
		fpp = newreg();
		inst("%s = getelementptr i8, ptr %s, i64 4", fpp, ap);
		fp = newreg();
		inst("%s = load i32, ptr %s", fp, fpp);
		inst("%s = icmp ule i32 %s, %d", c2, fp, 6*8 + 8*16 - 16*nfp);
		if (c) {
			char *both = newreg();
			inst("%s = and i1 %s, %s", both, c, c2);
			c2 = both;
		}
		c = c2;
	}
	mem = ++nblock;
	done = ++nblock;
	if (c) {	/* n > 0: in registers? */
		reg = ++nblock;
		inst("br i1 %s, label %%b%d, label %%b%d", c, reg, mem);
		print("b%d:\n", reg);
		rsap = newreg();
		inst("%s = getelementptr i8, ptr %s, i64 16", rsap, ap);
		rsa = newreg();
		inst("%s = load ptr, ptr %s", rsa, rsap);
		ra = newreg();
		inst("%s = getelementptr i8, ptr %s, i32 %s", ra, rsa, gp ? gp : fp);
		assert(slot || nfp == 0 || n == 1);
		if (slot) {	/* gather the parts */
			int ig = 0, is = 0;
			for (i = 0; i < n; i++) {
				char *src = newreg(), *dst = slot, *v = newreg();
				if (sysv_ssepart(part[i]))
					inst("%s = getelementptr i8, ptr %s, i32 %s", src, rsa, fp);
				else
					inst("%s = getelementptr i8, ptr %s, i32 %s", src, rsa, gp);
				if (sysv_ssepart(part[i]) ? is++ : ig++) {
					char *s2 = newreg();
					inst("%s = getelementptr i8, ptr %s, i64 %d", s2, src,
						sysv_ssepart(part[i]) ? 16 : 8);
					src = s2;
				}
				if (i > 0) {
					dst = newreg();
					inst("%s = getelementptr i8, ptr %s, i64 %d", dst, slot, 8*i);
				}
				inst("%s = load %s, ptr %s, align 8", v, part[i], src);
				inst("store %s %s, ptr %s, align %d", part[i], v, dst, ty->align);
			}
			ra = slot;
		}
		if (ngp > 0) {
			char *next = newreg();
			inst("%s = add i32 %s, %d", next, gp, 8*ngp);
			inst("store i32 %s, ptr %s", next, ap);
		}
		if (nfp > 0) {
			char *next = newreg();
			inst("%s = add i32 %s, %d", next, fp, 16*nfp);
			inst("store i32 %s, ptr %s", next, fpp);
		}
		inst("br label %%b%d", done);
	} else
		inst("br label %%b%d", mem);
	print("b%d:\n", mem);
	ovp = newreg();
	inst("%s = getelementptr i8, ptr %s, i64 8", ovp, ap);
	ov = newreg();
	inst("%s = load ptr, ptr %s", ov, ovp);
	if (ty->align > 8) {	/* such arguments are aligned to 16 */
		char *x = newreg(), *y = newreg(), *z = newreg(), *w = newreg();
		inst("%s = ptrtoint ptr %s to i64", x, ov);
		inst("%s = add i64 %s, 15", y, x);
		inst("%s = and i64 %s, -16", z, y);
		inst("%s = inttoptr i64 %s to ptr", w, z);
		ov = w;
	}
	ov2 = newreg();
	inst("%s = getelementptr i8, ptr %s, i64 %d", ov2, ov, size);
	inst("store ptr %s, ptr %s", ov2, ovp);
	inst("br label %%b%d", done);
	print("b%d:\n", done);
	a = newreg();
	if (reg)
		inst("%s = phi ptr [ %s, %%b%d ], [ %s, %%b%d ]", a, ra, reg, ov, mem);
	else
		inst("%s = phi ptr [ %s, %%b%d ]", a, ov, mem);
	return a;
}

/*
 * vaaddr64 - emit the AAPCS64 va_arg for an argument of type ty, and
 * return its address.  The va_list ap has __stack at 0, __gr_top at 8,
 * __vr_top at 16, __gr_offs at 24 and __vr_offs at 28; an offset is
 * negative while registers are left, and counts up to 0 from the top of
 * its save area, 8 bytes for each general register and 16 for each
 * SIMD register.  An argument is in registers if all it needs are
 * left, else on the stack.  The elements of an HFA of floats or doubles
 * in registers are put together in slot; a structure of more than 16
 * bytes is passed by the address of a copy.
 */
static char *vaaddr64(char *ap, Type ty, char *slot) {
	Type base = NULL;
	int n, gp = 1, nreg = 1, byref = 0, size, align = ty->align, i;
	int chk, reg, mem, done;
	char *offp, *offs, *c, *next, *topp, *top, *off64, *ra, *stk, *stk2, *a;

	if (isstruct(ty) && (n = abi_hfa(ty, &base)) > 0)
		gp = 0, nreg = n;
	else if (isstruct(ty) && ty->size > 16)
		byref = 1, align = 8;
	else if (isstruct(ty))
		nreg = (ty->size + 7)/8;
	else if (isfloat(ty))
		gp = 0;
	size = byref ? 8 : roundup(ty->size, 8);
	chk = ++nblock;
	reg = ++nblock;
	mem = ++nblock;
	done = ++nblock;
	offp = newreg();
	inst("%s = getelementptr i8, ptr %s, i64 %d", offp, ap, gp ? 24 : 28);
	offs = newreg();
	inst("%s = load i32, ptr %s", offs, offp);
	c = newreg();
	inst("%s = icmp sge i32 %s, 0", c, offs);
	inst("br i1 %s, label %%b%d, label %%b%d", c, mem, chk);
	print("b%d:\n", chk);
	if (gp && align > 8) {	/* an even pair of registers */
		char *x = newreg(), *y = newreg();
		inst("%s = add i32 %s, 15", x, offs);
		inst("%s = and i32 %s, -16", y, x);
		offs = y;
	}
	next = newreg();
	inst("%s = add i32 %s, %d", next, offs, gp ? 8*nreg : 16*nreg);
	inst("store i32 %s, ptr %s", next, offp);
	c = newreg();
	inst("%s = icmp sgt i32 %s, 0", c, next);
	inst("br i1 %s, label %%b%d, label %%b%d", c, mem, reg);
	print("b%d:\n", reg);
	topp = newreg();
	inst("%s = getelementptr i8, ptr %s, i64 %d", topp, ap, gp ? 8 : 16);
	top = newreg();
	inst("%s = load ptr, ptr %s", top, topp);
	off64 = newreg();
	inst("%s = sext i32 %s to i64", off64, offs);
	ra = newreg();
	inst("%s = getelementptr i8, ptr %s, i64 %s", ra, top, off64);
	if (slot && isstruct(ty)) {	/* gather the elements */
		char *t = base->size == 4 ? "float" : "double";
		for (i = 0; i < nreg; i++) {
			char *src = newreg(), *dst = newreg(), *v = newreg();
			inst("%s = getelementptr i8, ptr %s, i64 %d", src, ra, 16*i);
			inst("%s = getelementptr i8, ptr %s, i64 %d", dst, slot, i*base->size);
			inst("%s = load %s, ptr %s", v, t, src);
			inst("store %s %s, ptr %s", t, v, dst);
		}
		ra = slot;
	}
	inst("br label %%b%d", done);
	print("b%d:\n", mem);
	stk = newreg();
	inst("%s = load ptr, ptr %s", stk, ap);
	if (align > 8) {
		char *x = newreg(), *y = newreg(), *z = newreg(), *w = newreg();
		inst("%s = ptrtoint ptr %s to i64", x, stk);
		inst("%s = add i64 %s, 15", y, x);
		inst("%s = and i64 %s, -16", z, y);
		inst("%s = inttoptr i64 %s to ptr", w, z);
		stk = w;
	}
	stk2 = newreg();
	inst("%s = getelementptr i8, ptr %s, i64 %d", stk2, stk, size);
	inst("store ptr %s, ptr %s", stk2, ap);
	inst("br label %%b%d", done);
	print("b%d:\n", done);
	a = newreg();
	inst("%s = phi ptr [ %s, %%b%d ], [ %s, %%b%d ]", a, ra, reg, stk, mem);
	if (byref) {
		char *p = newreg();
		inst("%s = load ptr, ptr %s", p, a);
		a = p;
	}
	return a;
}

/*
 * vaaddrptr - emit the va_arg of RISC-V or ARM for an argument of type
 * ty, and return its address.  The va_list ap is a pointer to the next
 * argument in memory, where the callee has stored the argument
 * registers next to the arguments on the stack; each takes a multiple
 * of unit bytes, and starts at a multiple of its alignment if that is
 * more.  An argument of more than byref bytes, if byref is not 0, is
 * passed by the address of a copy (RV64: unit 8, byref 16; RV32: unit 4,
 * byref 8; ARM: unit 4, byref 0).
 */
static char *vaaddrptr(char *ap, Type ty, int unit, int byref) {
	int size, align = ty->align;
	char *p = newreg(), *next = newreg();

	byref = byref && ty->size > byref;
	size = byref ? unit : roundup(ty->size, unit);
	inst("%s = load ptr, ptr %s", p, ap);
	if (!byref && align > unit) {
		char *x = newreg(), *y = newreg(), *z = newreg(), *w = newreg();
		inst("%s = ptrtoint ptr %s to i64", x, p);
		inst("%s = add i64 %s, %d", y, x, align - 1);
		inst("%s = and i64 %s, %d", z, y, -align);
		inst("%s = inttoptr i64 %s to ptr", w, z);
		p = w;
	}
	inst("%s = getelementptr i8, ptr %s, i64 %d", next, p, size);
	inst("store ptr %s, ptr %s", next, ap);
	if (byref) {
		char *q = newreg();
		inst("%s = load ptr, ptr %s", q, p);
		p = q;
	}
	return p;
}

/*
 * emitbuiltin - emit the call p to the stdarg function b.  va_arg
 * returns the address of a value of the type promoted by a call (int
 * for char and short, whose bytes come first, and double for float).
 */
static char *emitbuiltin(Node p, int b) {
	char *ap, *v, *slot = vaslot(p);
	Type ty;

	if (args == NULL || args->link != NULL) {	/* declared otherwise */
		error("`%s' takes the va_list as its only argument\n",
			p->kids[0]->syms[0]->name);
		args = NULL;
		argtail = &args;
		return b == VAARG ? "null" : "";
	}
	ap = args->val;
	args = NULL;
	argtail = &args;
	usedva = 1;
	switch (b) {
	case VASTART:
		if (!variadic(cfunc->type))
			error("`va_start' used in a function with fixed arguments\n");
		inst("call void @llvm.va_start(ptr %s)", ap);
		return "";
	case VAEND:
		inst("call void @llvm.va_end(ptr %s)", ap);
		return "";
	}
	assert(b == VAARG);
	ty = unqual(freturn(p->syms[0]->type)->type);
	if (tg->abi == &aapcs64abi)
		v = vaaddr64(ap, ty, isstruct(ty) ? slot : NULL);
	else if (tg->abi == &riscv64abi)
		v = vaaddrptr(ap, ty, 8, 16);
	else if (tg->abi == &riscv32abi)
		v = vaaddrptr(ap, ty, 4, 8);
	else if (tg->abi == &armabi || tg->abi == &armsoftabi)
		v = vaaddrptr(ap, ty, 4, 0);
	else
		v = vaaddr(ap, ty, isstruct(ty) ? slot : NULL);
	if (slot && !isstruct(ty)) {	/* float */
		char *d = newreg(), *f = newreg();
		inst("%s = load double, ptr %s", d, v);
		inst("%s = fptrunc double %s to float", f, d);
		inst("store float %s, ptr %s", f, slot);
		v = slot;
	}
	return v;
}

/* ptroffset - the integer node k as an i64 byte offset, negated if neg */
static char *ptroffset(Node k, int neg) {
	char *v;

	if (generic(k->op) == CNST) {
		Value c = k->syms[0]->u.c.v;
		long n;

		if (optype(k->op) == U)		/* zero-extended */
			n = opsize(k->op) < 8 ? (long)(c.u & ones(8*opsize(k->op))) : (long)c.u;
		else				/* sign-extended */
			n = atol(intstr(c.u, opsize(k->op)));
		return stringf("%D", neg ? -n : n);
	}
	v = nodeval(k);
	if (opsize(k->op) < 8) {
		char *w = newreg();
		inst("%s = %s %s %s to i64", w, optype(k->op) == U ? "zext" : "sext",
			optypestr(k->op), v);
		v = w;
	}
	if (neg) {
		char *w = newreg();
		inst("%s = sub i64 0, %s", w, v);
		v = w;
	}
	return v;
}

/* nodeval - return the LLVM value of node p, emitting code for it */
static char *nodeval(Node p) {
	char *v = lookupval(p);

	if (v)
		return v;
	switch (generic(p->op)) {
	case CNST:
		v = cnststr(p->op, p->syms[0]->u.c.v);
		break;
	case ADDRG: case ADDRF: case ADDRL:
		v = gname(p->syms[0]);
		break;
	case INDIR:
		if (optype(p->op) == B) {	/* a struct is its address */
			v = nodeval(p->kids[0]);
			break;
		}
		{
			char *a = nodeval(p->kids[0]);
			v = newreg();
			inst("%s = load %s%s, ptr %s", v, p->volatil ? "volatile " : "",
				optypestr(p->op), a);
		}
		break;
	case ADD: case SUB:
		if (optype(p->op) == P) {	/* the pointer may be either kid */
			Node ptr = p->kids[0], k = p->kids[1];
			char *a, *off;

			if (optype(ptr->op) != P) {
				assert(generic(p->op) == ADD);
				ptr = p->kids[1];
				k = p->kids[0];
			}
			assert(optype(ptr->op) == P && optype(k->op) != P);
			a = nodeval(ptr);
			off = ptroffset(k, generic(p->op) == SUB);
			v = newreg();
			inst("%s = getelementptr i8, ptr %s, i64 %s", v, a, off);
			break;
		}
		/* fall through */
	case MUL: case DIV: case MOD:
	case BAND: case BOR: case BXOR:
		if (optype(p->op) == F) {
			char *a = nodeval(p->kids[0]), *b = nodeval(p->kids[1]);
			v = newreg();
			inst("%s = f%s %s %s, %s", v, generic(p->op) == ADD ? "add"
				: generic(p->op) == SUB ? "sub"
				: generic(p->op) == MUL ? "mul" : "div",
				optypestr(p->op), a, b);
			break;
		}
		if (optype(p->op) != I && optype(p->op) != U) {
			unsupported(p);
			return "undef";
		}
		{
			char *a = nodeval(p->kids[0]), *b = nodeval(p->kids[1]);
			v = newreg();
			inst("%s = %s %s %s, %s", v, intop(p->op), optypestr(p->op), a, b);
		}
		break;
	case LSH: case RSH:
		if (optype(p->op) != I && optype(p->op) != U) {
			unsupported(p);
			return "undef";
		}
		{
			char *a = nodeval(p->kids[0]), *b = nodeval(p->kids[1]);
			Node k = p->kids[1];

			/* the count is an int; LLVM wants it as wide as a */
			if (opsize(k->op) != opsize(p->op) && generic(k->op) != CNST) {
				char *c = newreg();
				inst("%s = %s %s %s to %s", c,
					opsize(k->op) < opsize(p->op) ? "zext" : "trunc",
					optypestr(k->op), b, optypestr(p->op));
				b = c;
			}
			v = newreg();
			inst("%s = %s %s %s, %s", v, generic(p->op) == LSH ? "shl"
				: optype(p->op) == U ? "lshr" : "ashr",
				optypestr(p->op), a, b);
		}
		break;
	case CVF:	/* to float or to int; unsigned is built by cast() */
		{
			Node k = p->kids[0];
			char *a = nodeval(k);
			int from = opsize(k->op), size = opsize(p->op);

			assert(from == p->syms[0]->u.c.v.i);	/* source size */
			v = newreg();
			inst("%s = %s %s %s to %s", v, optype(p->op) == I ? "fptosi"
				: from < size ? "fpext" : "fptrunc",
				optypestr(k->op), a, optypestr(p->op));
		}
		break;
	case CVI: case CVU: case CVP:
		if (optype(p->op) == F) {	/* only CVI: cast() splits unsigned */
			Node k = p->kids[0];
			char *a = nodeval(k);

			assert(generic(p->op) == CVI);
			v = newreg();
			inst("%s = sitofp %s %s to %s", v, optypestr(k->op), a,
				optypestr(p->op));
			break;
		}
		{
			Node k = p->kids[0];
			char *a = nodeval(k), *to = optypestr(p->op);
			int from = opsize(k->op), size = opsize(p->op);

			assert(from == p->syms[0]->u.c.v.i);	/* source size */
			if (generic(p->op) == CVP || optype(p->op) == P) {
				v = newreg();
				inst("%s = %s %s %s to %s", v, optype(p->op) == P
					? "inttoptr" : "ptrtoint", optypestr(k->op), a, to);
			} else if (from == size)	/* signedness only */
				v = a;
			else {
				v = newreg();
				inst("%s = %s %s %s to %s", v, from > size ? "trunc"
					: generic(p->op) == CVI ? "sext" : "zext",
					optypestr(k->op), a, to);
			}
		}
		break;
	case NEG: case BCOM:
		if (optype(p->op) == F) {
			char *a = nodeval(p->kids[0]);
			v = newreg();
			inst("%s = fneg %s %s", v, optypestr(p->op), a);
			break;
		}
		if (optype(p->op) != I && optype(p->op) != U) {
			unsupported(p);
			return "undef";
		}
		{
			char *a = nodeval(p->kids[0]);
			v = newreg();
			if (generic(p->op) == NEG)
				inst("%s = sub %s 0, %s", v, optypestr(p->op), a);
			else
				inst("%s = xor %s %s, -1", v, optypestr(p->op), a);
		}
		break;
	case CALL:
		if (optype(p->op) == B) {	/* wants_callb=0: not expected */
			unsupported(p);
			return "undef";
		}
		v = emitcall(p);
		break;
	default:
		unsupported(p);
		return "undef";
	}
	setval(p, v);
	return v;
}

/*
 * emitcall - emit the call p, taking the arguments listed since the
 * previous call.  Calls inside arguments are hoisted by the front end,
 * so they are complete before the first ARG of this call.
 *
 * A function without a prototype is called as (...), and a variadic one
 * as (fixed, ...).  The arguments have their default promotions, and
 * SysV passes them in the same registers either way; a variadic call
 * also sets %al, which a callee with variable arguments needs.  Where
 * variadic arguments go elsewhere (abi->krfixed), a call without a
 * prototype is one with fixed arguments of the types given, except to
 * the variadic functions of the C library (abi_libvariadic).
 */
static char *emitcall(Node p) {
	Type fty = p->syms[0]->type, rty = freturn(fty);
	char *f = nodeval(p->kids[0]), *v = NULL, *rt = optypestr(p->op);
	struct arg *a, *list = NULL, **tail = &list, *res = NULL;
	struct regs r;
	struct part rpart;
	int i, narrow = 0, nfixed = -1;	/* -1: all arguments are fixed */
	int nres = 0;	/* 1 if a structure result is in registers */

	if ((i = builtin(p)) != 0)
		return emitbuiltin(p, i);
	if (fty->u.f.proto == NULL || fty->u.f.oldstyle) {
		nfixed = tg->abi->krfixed ? -1 : 0;
		if (nfixed < 0 && specific(p->kids[0]->op) == ADDRG+P)
			nfixed = abi_libvariadic(p->kids[0]->syms[0]->name);
	}
	else if (variadic(fty))
		for (nfixed = 0; fty->u.f.proto[nfixed + 1]; nfixed++)
			;	/* proto ends with voidtype for ... */
	if (isstruct(rty)) {	/* args has its address first */
		assert(args && args->type && strcmp(args->type, "ptr") == 0);
		res = args;
		if ((nres = (*tg->abi->ret)(rty, nfixed >= 0, &rpart)) > 0) {
			args = args->link;
			rt = rpart.type;
		} else
			res->attr = sretstr(rty);
	}
	if (nfixed >= 0 && nres == 0 && isstruct(rty))
		nfixed++;	/* the result address is fixed: sret is not allowed in ... */
	if (optype(p->op) != V && (isint(rty) || isenum(rty))
	&& rty->size < opsize(p->op)) {
		/* the upper bits of a char or short result are undefined */
		rt = typestr(rty);
		narrow = 1;
	}
	/* pass structures in registers, on the stack or by address */
	(*tg->abi->init)(&r);
	r.vfunc = nfixed >= 0;
	for (i = 0, a = args; a; i++, a = a->link) {
		struct arg *b;
		struct part *part;
		int j, n = 0;
		a->fixed = nfixed < 0 || i < nfixed;
		r.va = !a->fixed;
		if (a->sty == NULL) {
			(*tg->abi->scalar)(a->type, &r);
			if (*extattr(a->type))
				a->attr = extattr(a->type) + 1;
		} else if ((n = (*tg->abi->arg)(a->sty, &r, &part)) == ABYVAL) {
			a->type = "ptr";
			a->attr = byvalstr(a->sty);
		} else if (n == ABYREF) {
			char *copy = vaslot(a->node);
			assert(copy);
			inst("call void @llvm.memcpy.p0.p0.i64(ptr align %d %s, "
				"ptr align %d %s, i64 %d, i1 false)", a->sty->align, copy,
				a->sty->align, a->val, a->sty->size);
			usedmemcpy = 1;
			a->type = "ptr";
			a->val = copy;
		}
		if (n <= 0) {
			*tail = a;
			tail = &a->link;
			continue;
		}
		for (j = 0; j < n; j++) {
			NEW0(b, FUNC);
			b->type = part[j].type;
			b->attr = partattr(&part[j]);
			b->fixed = a->fixed;
			b->val = loadpart(&part[j], a->val, a->sty->align);
			*tail = b;
			tail = &b->link;
		}
	}
	*tail = NULL;
	if (optype(p->op) != V || nres > 0)
		v = newreg();
	startline();
	if (v)
		print("%s = ", v);
	print("call%s %s (", narrow ? "" : extattr(rt), rt);
	for (i = 0, a = list; a && a->fixed; i++, a = a->link)
		print("%s%s", i ? ", " : "", a->type);
	if (nfixed >= 0)
		print("%s...", i ? ", " : "");
	print(") %s(", f);
	for (a = list; a; a = a->link)
		print("%s%s%s%s %s", a == list ? "" : ", ", a->type,
			a->attr ? " " : "", a->attr ? a->attr : "", a->val);
	print(")");
	if (specific(p->kids[0]->op) == ADDRG+P
	&& abi_returnstwice(p->kids[0]->syms[0]->name))
		print(" returns_twice");
	print("\n");
	args = NULL;
	argtail = &args;
	if (nres > 0) {	/* store the registers at the result address */
		storepart(&rpart, v, res->val, rty->align);
		return "";
	}
	if (narrow) {
		char *w = newreg();
		inst("%s = %s %s %s to %s", w, isunsigned(rty) ? "zext" : "sext",
			rt, v, optypestr(p->op));
		v = w;
	}
	return v ? v : "";
}

/* cmppred - icmp predicate for the comparison op */
static char *cmppred(int op) {
	int u = optype(op) == U;

	switch (generic(op)) {
	case EQ: return "eq";
	case NE: return "ne";
	case LT: return u ? "ult" : "slt";
	case LE: return u ? "ule" : "sle";
	case GT: return u ? "ugt" : "sgt";
	case GE: return u ? "uge" : "sge";
	}
	assert(0);
	return NULL;
}

/*
 * fcmppred - fcmp predicate for the floating comparison p.  != is true
 * for unordered operands (a NaN), the others are false, except when
 * the front end inverted the comparison to jump on false; it marks
 * those with syms[1], and then they must be true when unordered.
 */
static char *fcmppred(Node p) {
	int u = p->syms[1] != NULL;

	switch (generic(p->op)) {
	case EQ: return "oeq";
	case NE: return "une";
	case LT: return u ? "ult" : "olt";
	case LE: return u ? "ule" : "ole";
	case GT: return u ? "ugt" : "ogt";
	case GE: return u ? "uge" : "oge";
	}
	assert(0);
	return NULL;
}

/* equated - the label that p was made equal to, as in dag.c */
static Symbol equated(Symbol p) {
	while (p->u.l.equatedto)
		p = p->u.l.equatedto;
	return p;
}

/*
 * emitindirect - emit the indirect jump p through a switch table.  The
 * front end makes one only for a switch: the code list entry right
 * after the jump's has the table, whose labels are the destinations.
 */
static void emitindirect(Node p) {
	char *a = nodeval(p->kids[0]);
	Code cp;
	int i;

	for (cp = codehead.next; cp; cp = cp->next)
		if (cp->kind == Gen && cp->u.forest == emitting)
			break;
	if (cp == NULL || (cp = cp->next) == NULL || cp->kind != Switch) {
		unsupported(p);
		return;
	}
	startline();
	print("indirectbr ptr %s, [label %%%s", a,
		gname(equated(cp->u.swtch.deflab)));
	for (i = 0; i < cp->u.swtch.size; i++)
		print(", label %%%s", gname(equated(cp->u.swtch.labels[i])));
	print("]\n");
}

/* emitnode - emit code for the root node p */
static void emitnode(Node p) {
	switch (generic(p->op)) {
	case EQ: case NE: case LT: case LE: case GT: case GE:
		/* jump to syms[0] if true; else fall into a new block */
		{
			char *a = nodeval(p->kids[0]), *b = nodeval(p->kids[1]);
			char *c = newreg(), *fall = stringf("b%d", ++nblock);

			/* pointers are compared as integers: the front end adds CVPU */
			assert(optype(p->kids[0]->op) != P && optype(p->kids[1]->op) != P);
			if (optype(p->op) == F)
				inst("%s = fcmp %s %s %s, %s", c, fcmppred(p),
					optypestr(p->op), a, b);
			else
				inst("%s = icmp %s %s %s, %s", c, cmppred(p->op),
					optypestr(p->op), a, b);
			inst("br i1 %s, label %%%s, label %%%s", c,
				gname(p->syms[0]), fall);
			print("%s:\n", fall);
		}
		break;
	case LABEL:
		if (!terminated)
			print("  br label %%%s\n", gname(p->syms[0]));
		print("%s:\n", gname(p->syms[0]));
		terminated = 0;
		break;
	case JUMP:
		if (specific(p->kids[0]->op) == ADDRG+P)
			inst("br label %%%s", gname(p->kids[0]->syms[0]));
		else
			emitindirect(p);
		terminated = 1;
		break;
	case ASGN:
		if (optype(p->op) == B) {	/* kids[1] is INDIRB */
			char *a = nodeval(p->kids[0]), *b = nodeval(p->kids[1]);
			int align = p->syms[1]->u.c.v.i;
			assert(generic(p->kids[1]->op) == INDIR);
			inst("call void @llvm.memcpy.p0.p0.i64(ptr align %d %s, "
				"ptr align %d %s, i64 %D, i1 %s)", align, a, align, b,
				p->syms[0]->u.c.v.i,
				p->volatil || p->kids[1]->volatil ? "true" : "false");
			usedmemcpy = 1;
			break;
		}
		{
			char *a = nodeval(p->kids[0]), *v = nodeval(p->kids[1]);
			inst("store %s%s %s, ptr %s", p->volatil ? "volatile " : "",
				optypestr(p->op), v, a);
		}
		break;
	case RET:
		if (optype(p->op) != V)
			inst("store %s %s, ptr %%retval", optypestr(p->op),
				nodeval(p->kids[0]));
		break;
	case ARG:
		{
			struct arg *a;
			NEW0(a, FUNC);
			if (optype(p->op) == B) {	/* dag.c types its size */
				a->sty = p->syms[0]->type;
				assert(isstruct(a->sty));
			} else
				a->type = optypestr(p->op);
			a->val = nodeval(p->kids[0]);
			a->node = p;
			*argtail = a;
			argtail = &a->link;
		}
		break;
	case CALL:
	case INDIR:
		(void)nodeval(p);
		break;
	default:
		unsupported(p);
	}
}

static void emitescaped(int len, const char *s) {
	for ( ; len-- > 0; s++)
		if (*s >= ' ' && *s < 0177 && *s != '"' && *s != '\\')
			print("%c", *s);
		else
			print("\\%c%c", "0123456789ABCDEF"[(*s>>4)&0xF],
				"0123456789ABCDEF"[*s&0xF]);
}

static void emititemtype(Item it) {
	switch (it->kind) {
	case Kconst:  print("%s", it->type); break;
	case Kaddr:   print("ptr"); break;
	case Kstring:
	case Kspace:  print("[%d x i8]", it->n); break;
	default: assert(0);
	}
}

static void emititemval(Item it) {
	switch (it->kind) {
	case Kconst: case Kaddr:
		print("%s", it->val);
		break;
	case Kstring:
		print("c\"");
		emitescaped(it->n, it->s);
		print("\"");
		break;
	case Kspace:
		print("zeroinitializer");
		break;
	default: assert(0);
	}
}

static void emitglobal(Gdata g) {
	Item it;

	print("%s = %s%s ", g->name, g->linkage,
		g->constant ? "constant" : "global");
	if (g->items == NULL)
		print("[0 x i8] zeroinitializer");
	else if (g->items->link == NULL && g->items->kind == Kspace
	&& g->items->n == g->size)	/* all zero: keep the C type */
		print("%s zeroinitializer", g->type);
	else if (g->items->link == NULL) {
		emititemtype(g->items);
		print(" ");
		emititemval(g->items);
	} else {
		print("<{ ");
		for (it = g->items; it; it = it->link) {
			emititemtype(it);
			print(it->link ? ", " : " }> <{ ");
		}
		for (it = g->items; it; it = it->link) {
			emititemtype(it);
			print(" ");
			emititemval(it);
			print(it->link ? ", " : " }>");
		}
	}
	print(", align %d\n", g->align);
}

/* flushglobal - finish the pending global, if any */
static void flushglobal(void) {
	if (pending == NULL)
		return;
	/* an array's size may be known only from its initializer */
	pending->align = abi_varalign(tg->abi, pending->sym);
	if (infunc) {
		*dtail = pending;
		dtail = &pending->link;
	} else
		emitglobal(pending);
	pending = NULL;
}

static Item newitem(int kind) {
	Item it;

	assert(pending);
	NEW0(it, PERM);
	it->kind = kind;
	*pending->tail = it;
	pending->tail = &it->link;
	return it;
}

/* address - initialize q for addressing expression p+n */
static void I(address)(Symbol q, Symbol p, long n) {
	if (p->scope == GLOBAL || p->sclass == STATIC || p->sclass == EXTERN)
		q->x.name = stringf("getelementptr (i8, ptr %s, i64 %D)",
			gname(p), n);
	else {
		struct laddr *a;
		NEW(a, FUNC);
		a->q = q;
		a->p = p;
		a->n = n;
		a->link = NULL;
		*ladtail = a;
		ladtail = &a->link;
		q->x.name = stringf("%%c%d", ++nlocal);
	}
}

static void I(blockbeg)(Env *e) {}
static void I(blockend)(Env *e) {}

/* defaddress - initialize an address */
static void I(defaddress)(Symbol p) {
	if (p->scope == LABELS) {	/* in a switch table */
		assert(infunc);
		newitem(Kaddr)->val = stringf("blockaddress(%s, %%%s)",
			fname, gname(p));
		return;
	}
	newitem(Kaddr)->val = gname(p);
}

/* defconst - define a constant */
static void I(defconst)(int suffix, int size, Value v) {
	Item it = newitem(Kconst);

	switch (suffix) {
	case I: it->type = stringf("i%d", 8*size); it->val = intstr(v.i, size); break;
	case U: it->type = stringf("i%d", 8*size); it->val = intstr(v.u, size); break;
	case P: it->type = "ptr"; it->val = ptrstr(v.p); break;
	case F: it->type = flttypestr(size);
		it->val = floatstr(v, size); break;
	default: assert(0);
	}
}

/* defstring - emit a string constant */
static void I(defstring)(int len, char *s) {
	Item it = newitem(Kstring);

	it->s = memcpy(allocate(len, PERM), s, len);
	it->n = len;
}

/* defsymbol - define a symbol: initialize p->x.name */
static void I(defsymbol)(Symbol p) {
	if (p->x.name)
		return;
	if (p->scope == LABELS)
		p->x.name = stringf("L%s", p->name);
	else if (p->scope == CONSTANTS)
		p->x.name = p->name;
	else if (p->sclass == STATIC && p->generated)
		p->x.name = stringf("@.L%s", p->name);
	else if (p->sclass == STATIC && p->scope >= LOCAL)
		p->x.name = stringf("@%s.%d", p->name, genlabel(1));
	else if (p->scope == GLOBAL || p->sclass == EXTERN)
		p->x.name = stringf("@%s", p->name);
	/* parameters and locals are named by function and local */
}

/* emit - emit the dags on list p */
static void I(emit)(Node p) {
	for (emitting = p; p; p = p->link)
		emitnode(p);
}

static void I(export)(Symbol p) {}

/* function - generate code for a function */
static void I(function)(Symbol f, Symbol caller[], Symbol callee[], int ncalls) {
	int i;
	struct laddr *a;
	Gdata g;
	Code cp;
	Node e;
	struct regs r;
	struct part rpart;
	int nres, np;

	flushglobal();
	infunc = 1;
	vaslots = NULL;
	memset(nodevals, 0, sizeof nodevals);
	args = NULL;
	argtail = &args;
	laddrs = NULL;
	ladtail = &laddrs;
	nreg = nblock = 0;
	terminated = 0;
	rettype = rettypestr(f->type);
	fname = gname(f);

	print("\ndefine %s%s%s %s(", f->sclass == STATIC ? "internal " : "",
		*extattr(rettype) ? stringf("%s ", extattr(rettype) + 1) : "",
		rettype, fname);
	(*tg->abi->init)(&r);
	r.vfunc = variadic(f->type);
	nres = isstruct(freturn(f->type))
		? (*tg->abi->ret)(freturn(f->type), r.vfunc, &rpart) : 0;
	for (i = nres > 0, np = 0; caller[i]; i++) {
		Type ty = caller[i]->type;
		struct part *part;
		int j, n;
		print("%s", np++ ? ", " : "");
		if (i == 0 && isstruct(freturn(f->type))) {	/* the result address */
			print("ptr %s %%a0", sretstr(freturn(f->type)));
			(*tg->abi->scalar)("ptr", &r);
		} else if (!isstruct(ty)) {
			print("%s%s %%a%d", typestr(ty), extattr(typestr(ty)), i);
			(*tg->abi->scalar)(typestr(ty), &r);
		} else if ((n = (*tg->abi->arg)(ty, &r, &part)) == ABYVAL)
			print("ptr %s %%a%d", byvalstr(ty), i);
		else if (n == ABYREF)
			print("ptr %%a%d", i);
		else
			for (j = 0; j < n; j++)
				print("%s%s %%a%d.%d", j ? ", " : "", partstr(&part[j]), i, j);
	}
	if (variadic(f->type))
		print("%s...", np ? ", " : "");
	print(") {\nentry:\n");
	if (strcmp(rettype, "void") != 0)
		inst("%%retval = alloca %s", rettype);
	(*tg->abi->init)(&r);
	r.vfunc = variadic(f->type);
	for (i = 0; caller[i]; i++) {
		Symbol p = caller[i], q = callee[i];
		struct part *part;
		int j, n;
		if (i == 0 && nres > 0) {	/* the result goes in %retval */
			p->x.name = localname(p);
			emitalloca(p);
			inst("store ptr %%retval, ptr %s", p->x.name);
		} else if (!isstruct(p->type)) {
			p->x.name = localname(p);
			emitalloca(p);
			inst("store %s %%a%d, ptr %s", typestr(p->type), i, p->x.name);
			(*tg->abi->scalar)(typestr(p->type), &r);
		} else if ((n = (*tg->abi->arg)(p->type, &r, &part)) <= 0)
			p->x.name = stringf("%%a%d", i);	/* byval or a copy */
		else {	/* in registers: store them */
			p->x.name = localname(p);
			emitalloca(p);
			for (j = 0; j < n; j++)
				storepart(&part[j], stringf("%%a%d.%d", i, j), p->x.name,
					p->type->align);
		}
		if (q->sclass == p->sclass && q->type == p->type)
			q->x.name = p->x.name;
		else {	/* the front end assigns caller to callee */
			q->x.name = localname(q);
			emitalloca(q);
		}
	}
	gencode(caller, callee);
	for (cp = codehead.next; cp; cp = cp->next)
		if (cp->kind == Gen || cp->kind == Jump || cp->kind == Label)
			for (e = cp->u.forest; e; e = e->link)
				allocvaslots(e);
	for (a = laddrs; a; a = a->link)
		inst("%s = getelementptr i8, ptr %s, i64 %D",
			a->q->x.name, gname(a->p), a->n);
	emitcode();
	if (strcmp(rettype, "void") == 0)
		inst("ret void");
	else {
		char *r = newreg();
		inst("%s = load %s, ptr %%retval", r, rettype);
		inst("ret %s %s", rettype, r);
	}
	print("}\n\n");
	infunc = 0;
	for (g = deferred; g; g = g->link)
		emitglobal(g);
	deferred = NULL;
	dtail = &deferred;
}

/* gen - check the dags on list p; emit does the work */
static Node I(gen)(Node p) {
	Node q;

	for (q = p; q; q = q->link) {
		switch (generic(q->op)) {	/* check for valid forest */
		case CALL:
			break;
		case ARG:
		case ASGN: case JUMP: case LABEL: case RET:
		case EQ: case GE: case GT: case LE: case LT: case NE:
			assert(q->count == 0);
			break;
		case INDIR:
			assert(q->count > 0);
			break;
		default:
			assert(0);
		}
		check(q);
	}
	return p;
}

/* global - announce a global */
static void I(global)(Symbol p) {
	flushglobal();
	NEW0(pending, PERM);
	pending->name = gname(p);
	if (p->sclass == STATIC)
		pending->linkage = p->generated ? "private " : "internal ";
	else
		pending->linkage = "";
	pending->constant = p->u.seg == LIT;
	pending->type = typestr(p->type);
	pending->size = p->type->size;
	pending->align = p->type->align;
	pending->sym = p;
	pending->tail = &pending->items;
}

/* import - declare an external symbol */
static void I(import)(Symbol p) {
	gname(p);
	if (strncmp(p->name, "__builtin_va_", 13) == 0)
		return;	/* see builtin */
	if (isfunc(p->type)) {
		Type *proto = p->type->u.f.proto;
		struct regs r;
		struct part rpart, *part;
		int i, j, k, n = 0;
		print("declare %s%s%s %s(", *extattr(rettypestr(p->type))
			? extattr(rettypestr(p->type)) + 1 : "",
			*extattr(rettypestr(p->type)) ? " " : "", rettypestr(p->type), p->x.name);
		(*tg->abi->init)(&r);
		r.vfunc = variadic(p->type);
		if (isstruct(freturn(p->type))
		&& !(*tg->abi->ret)(freturn(p->type), r.vfunc, &rpart)) {
			print("ptr %s", sretstr(freturn(p->type)));	/* the result address */
			n++;
			(*tg->abi->scalar)("ptr", &r);
		}
		if (proto && !p->type->u.f.oldstyle) {
			for (i = 0; proto[i] && proto[i] != voidtype; i++)
				if (!isstruct(proto[i])) {
					print("%s%s%s", n++ ? ", " : "", argtypestr(proto[i]),
						extattr(argtypestr(proto[i])));
					(*tg->abi->scalar)(argtypestr(proto[i]), &r);
				} else if ((k = (*tg->abi->arg)(proto[i], &r, &part)) == ABYVAL)
					print("%sptr %s", n++ ? ", " : "", byvalstr(proto[i]));
				else if (k == ABYREF)
					print("%sptr", n++ ? ", " : "");
				else
					for (j = 0; j < k; j++)
						print("%s%s", n++ ? ", " : "", partstr(&part[j]));
			if (variadic(p->type))
				print(", ...");
		} else
			print("%s...", n ? ", " : "");
		print(")%s\n", abi_returnstwice(p->name) ? " returns_twice" : "");
	} else
		print("%s = external global %s\n", p->x.name, typestr(p->type));
}

/* local - local variable */
static void I(local)(Symbol p) {
	p->x.name = localname(p);
	emitalloca(p);
}

/* progbeg - beginning of program */
static void I(progbeg)(int argc, char *argv[]) {
	for (tg = targets; tg->ir != IR; tg++)
		assert(tg < &targets[NELEMS(targets)-1]);
	if (firstfile && *firstfile) {
		print("source_filename = \"");
		emitescaped(strlen(firstfile), firstfile);
		print("\"\n");
	}
	print("target datalayout = \"%s\"\n", tg->layout);
	print("target triple = \"%s\"\n\n", tg->triple);
}

/* progend - end of program */
static void I(progend)(void) {
	flushglobal();
	if (usedmemcpy)
		print("\ndeclare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)\n");
	if (usedva)
		print("\ndeclare void @llvm.va_start(ptr)\ndeclare void @llvm.va_end(ptr)\n");
}

static void I(segment)(int s) {}

/* space - initialize n bytes of space */
static void I(space)(int n) {
	newitem(Kspace)->n = n;
}

static void I(stabblock)(int brace, int lev, Symbol *p) {}
static void I(stabend)(Coordinate *cp, Symbol p, Coordinate **cpp, Symbol *sp, Symbol *stab) {}
static void I(stabfend)(Symbol p, int lineno) {}
static void I(stabinit)(char *file, int argc, char *argv[]) {}
static void I(stabline)(Coordinate *cp) {}
static void I(stabsym)(Symbol p) {}
static void I(stabtype)(Symbol p) {}

#define xx(id, triple, layout, ldtype, mlirld, lsize, ldsize, psize, uchar, quad, ubf, abi) \
TARGETIR(id, lsize, ldsize, psize, uchar, quad, ubf);
TARGETS
#undef xx
