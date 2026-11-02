#include "c.h"

/*
 * LLVM back end: emits textual LLVM IR (.ll) for llvm/x86_64-linux.
 *
 * Locals and parameters live in allocas in the entry block; mem2reg
 * turns them into SSA values.  The LLVM value of each dag node is kept
 * in a table local to this file, so Xnode is not touched beyond what
 * the front end itself uses.
 */

#define I(f) llvm_##f
static char rcsid[] = "$Id$";

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

/* va_arg calls of the current function and the places for their values */
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

/* optypestr - LLVM type of operator op's type suffix */
static char *optypestr(int op) {
	switch (optype(op)) {
	case I: case U: return stringf("i%d", 8*opsize(op));
	case F: return opsize(op) == 4 ? "float" : "double";
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
		return ty->size == 4 ? "float" : "double";
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

/*
 * SysV x86-64 passes a structure or union of at most 16 bytes in up to
 * two registers, one for each eightbyte: an SSE register if it holds
 * only floats and doubles, else an integer register.  Larger ones, and
 * ones for which too few of the 6 integer and 8 SSE argument registers
 * are left, are copied to the stack (byval).
 */
enum { INTEGER = 1, SSE };

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
	} else if (isfloat(ty)) {
		if (cls[off/8] != INTEGER)
			cls[off/8] = SSE;
	} else
		cls[off/8] = INTEGER;
}

/* free argument registers while arguments are assigned to them */
struct regs {
	int gp, fp;
};

/*
 * structparts - set part[] to the LLVM types of the registers that pass
 * the structure ty and take them from r; return how many, or 0 if ty
 * goes on the stack.  The last part covers only the bytes left, so
 * that it is not loaded or stored beyond the structure.
 */
static int structparts(Type ty, struct regs *r, char *part[]) {
	int cls[2], i, n, ngp = 0, nfp = 0;

	ty = unqual(ty);
	if (ty->size == 0 || ty->size > 16)
		return 0;
	cls[0] = cls[1] = 0;
	classify(ty, 0, cls);
	n = (ty->size + 7)/8;
	for (i = 0; i < n; i++)
		if (cls[i] == SSE)
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

/* scalarreg - take a register for a scalar argument of LLVM type t */
static void scalarreg(char *t, struct regs *r) {
	if (strcmp(t, "float") == 0 || strcmp(t, "double") == 0) {
		if (r->fp > 0)
			r->fp--;
	} else if (r->gp > 0)
		r->gp--;
}

/* byvalstr - attributes of a pointer to the structure ty on the stack */
static char *byvalstr(Type ty) {
	return stringf("byval([%d x i8]) align %d", ty->size,
		ty->align < 8 ? 8 : ty->align);
}

/* rettypestr - LLVM return type of function type fty */
static char *rettypestr(Type fty) {
	Type rty = freturn(fty);

	if (isstruct(rty))	/* wants_callb=0: returned via hidden pointer */
		return "void";
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

/* floatstr - floating constant d of size bytes, in LLVM's hex form */
static char *floatstr(long double d, int size) {
	static union { int x; char endian; } little = { 1 };
	double x = size == 4 ? (double)(float)d : (double)d;
	unsigned char b[sizeof x];
	char buf[2*sizeof x + 3], *s = buf;
	int i;

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
	case F: return floatstr(v.d, opsize(op));
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

/*
 * allocvaslots - make an entry block place for each va_arg call in dag
 * p whose value is not in the argument area as it is: a float arrives
 * as a double.  The call returns the address of its value.
 */
static void allocvaslots(Node p) {
	Type ty;

	if (p == NULL)
		return;
	if (generic(p->op) == CALL && builtin(p) == VAARG && vaslot(p) == NULL
	&& isfloat(ty = unqual(freturn(p->syms[0]->type)->type))
	&& ty->size < doubletype->size) {
		struct vaslot *s;
		NEW(s, FUNC);
		s->call = p;
		s->name = stringf("%%va%d", ++nlocal);
		s->link = vaslots;
		vaslots = s;
		inst("%s = alloca %s, align %d", s->name, typestr(ty), ty->align);
	}
	allocvaslots(p->kids[0]);
	allocvaslots(p->kids[1]);
}

/*
 * vaaddr - emit the SysV va_arg for an argument of class INTEGER or, if
 * fp, SSE, and return its address.  The va_list ap has gp_offset at 0,
 * fp_offset at 4, overflow_arg_area at 8 and reg_save_area at 16; the
 * register save area has the 6 integer registers and then the 8 SSE
 * registers, 16 bytes each.  The value is used where it is.  LLVM's
 * va_arg instruction is not used: LLVM 18 at -O0 miscompiles it outside
 * the entry block.
 */
static char *vaaddr(char *ap, int fp) {
	char *offp = ap, *off = newreg(), *c = newreg(), *rsap = newreg();
	char *rsa = newreg(), *ra = newreg(), *next = newreg(), *ovp = newreg();
	char *ov = newreg(), *ov2 = newreg(), *a = newreg();
	int reg = ++nblock, mem = ++nblock, done = ++nblock;

	if (fp) {
		offp = newreg();
		inst("%s = getelementptr i8, ptr %s, i64 4", offp, ap);
	}
	inst("%s = load i32, ptr %s", off, offp);
	inst("%s = icmp ule i32 %s, %d", c, off, fp ? 6*8 + 7*16 : 5*8);
	inst("br i1 %s, label %%b%d, label %%b%d", c, reg, mem);
	print("b%d:\n", reg);
	inst("%s = getelementptr i8, ptr %s, i64 16", rsap, ap);
	inst("%s = load ptr, ptr %s", rsa, rsap);
	inst("%s = getelementptr i8, ptr %s, i32 %s", ra, rsa, off);
	inst("%s = add i32 %s, %d", next, off, fp ? 16 : 8);
	inst("store i32 %s, ptr %s", next, offp);
	inst("br label %%b%d", done);
	print("b%d:\n", mem);
	inst("%s = getelementptr i8, ptr %s, i64 8", ovp, ap);
	inst("%s = load ptr, ptr %s", ov, ovp);
	inst("%s = getelementptr i8, ptr %s, i64 8", ov2, ov);
	inst("store ptr %s, ptr %s", ov2, ovp);
	inst("br label %%b%d", done);
	print("b%d:\n", done);
	inst("%s = phi ptr [ %s, %%b%d ], [ %s, %%b%d ]", a, ra, reg, ov, mem);
	return a;
}

/*
 * emitbuiltin - emit the call p to the stdarg function b.  va_arg
 * returns the address of a scalar of the type promoted by a call (int
 * for char and short, whose bytes come first, and double for float).
 */
static char *emitbuiltin(Node p, int b) {
	char *ap, *v, *slot = vaslot(p);
	Type ty;

	assert(args && args->link == NULL);
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
	if (isstruct(ty)) {
		error("llvm back end: va_arg of a structure is not supported\n");
		return "null";
	}
	v = vaaddr(ap, isfloat(ty));
	if (slot) {	/* float */
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
			inst("%s = load %s, ptr %s", v, optypestr(p->op), a);
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
			if (optype(p->op) == F && from == size)	/* long double */
				v = a;
			else {
				v = newreg();
				inst("%s = %s %s %s to %s", v, optype(p->op) == I ? "fptosi"
					: from < size ? "fpext" : "fptrunc",
					optypestr(k->op), a, optypestr(p->op));
			}
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
 * also sets %al, which a callee with variable arguments needs.
 */
static char *emitcall(Node p) {
	Type fty = p->syms[0]->type, rty = freturn(fty);
	char *f = nodeval(p->kids[0]), *v = NULL, *rt = optypestr(p->op);
	struct arg *a, *list = NULL, **tail = &list;
	struct regs r;
	int i, narrow = 0, nfixed = -1;	/* -1: all arguments are fixed */

	if ((i = builtin(p)) != 0)
		return emitbuiltin(p, i);
	if (fty->u.f.proto == NULL || fty->u.f.oldstyle)
		nfixed = 0;
	else if (variadic(fty)) {
		for (nfixed = 0; fty->u.f.proto[nfixed + 1]; nfixed++)
			;	/* proto ends with voidtype for ... */
		if (isstruct(rty))	/* the hidden result address */
			nfixed++;
	}
	if (optype(p->op) != V && (isint(rty) || isenum(rty))
	&& rty->size < opsize(p->op)) {
		/* the upper bits of a char or short result are undefined */
		rt = typestr(rty);
		narrow = 1;
	}
	/* pass structures in registers or on the stack */
	r.gp = 6;
	r.fp = 8;
	for (i = 0, a = args; a; i++, a = a->link) {
		struct arg *b;
		char *part[2];
		int j, n = 0;
		a->fixed = nfixed < 0 || i < nfixed;
		if (a->sty == NULL)
			scalarreg(a->type, &r);
		else if ((n = structparts(a->sty, &r, part)) == 0) {
			a->type = "ptr";
			a->attr = byvalstr(a->sty);
		}
		if (n == 0) {
			*tail = a;
			tail = &a->link;
			continue;
		}
		for (j = 0; j < n; j++) {
			char *addr = a->val;
			NEW0(b, FUNC);
			b->type = part[j];
			b->fixed = a->fixed;
			b->val = newreg();
			if (j > 0) {
				addr = newreg();
				inst("%s = getelementptr i8, ptr %s, i64 %d", addr, a->val, 8*j);
			}
			inst("%s = load %s, ptr %s, align %d", b->val, part[j], addr,
				a->sty->align);
			*tail = b;
			tail = &b->link;
		}
	}
	*tail = NULL;
	if (optype(p->op) != V)
		v = newreg();
	startline();
	if (v)
		print("%s = ", v);
	print("call %s (", rt);
	for (i = 0, a = list; a && a->fixed; i++, a = a->link)
		print("%s%s", i ? ", " : "", a->type);
	if (nfixed >= 0)
		print("%s...", i ? ", " : "");
	print(") %s(", f);
	for (a = list; a; a = a->link)
		print("%s%s%s%s %s", a == list ? "" : ", ", a->type,
			a->attr ? " " : "", a->attr ? a->attr : "", a->val);
	print(")\n");
	args = NULL;
	argtail = &args;
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
				"ptr align %d %s, i64 %D, i1 false)", align, a, align, b,
				p->syms[0]->u.c.v.i);
			usedmemcpy = 1;
			break;
		}
		{
			char *a = nodeval(p->kids[0]), *v = nodeval(p->kids[1]);
			inst("store %s %s, ptr %s", optypestr(p->op), v, a);
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
	case F: it->type = size == 4 ? "float" : "double";
		it->val = floatstr(v.d, size); break;
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

	print("\ndefine %s%s %s(", f->sclass == STATIC ? "internal " : "",
		rettype, fname);
	r.gp = 6;
	r.fp = 8;
	for (i = 0; caller[i]; i++) {
		Type ty = caller[i]->type;
		char *part[2];
		int j, n;
		print("%s", i ? ", " : "");
		if (!isstruct(ty)) {
			print("%s %%a%d", typestr(ty), i);
			scalarreg(typestr(ty), &r);
		} else if ((n = structparts(ty, &r, part)) == 0)
			print("ptr %s %%a%d", byvalstr(ty), i);
		else
			for (j = 0; j < n; j++)
				print("%s%s %%a%d.%d", j ? ", " : "", part[j], i, j);
	}
	if (variadic(f->type))
		print("%s...", i ? ", " : "");
	print(") {\nentry:\n");
	if (strcmp(rettype, "void") != 0)
		inst("%%retval = alloca %s", rettype);
	r.gp = 6;
	r.fp = 8;
	for (i = 0; caller[i]; i++) {
		Symbol p = caller[i], q = callee[i];
		char *part[2];
		int j, n;
		if (!isstruct(p->type)) {
			p->x.name = localname(p);
			emitalloca(p);
			inst("store %s %%a%d, ptr %s", typestr(p->type), i, p->x.name);
			scalarreg(typestr(p->type), &r);
		} else if ((n = structparts(p->type, &r, part)) == 0)
			p->x.name = stringf("%%a%d", i);	/* byval */
		else {	/* in registers: store them */
			p->x.name = localname(p);
			emitalloca(p);
			for (j = 0; j < n; j++) {
				char *addr = p->x.name;
				if (j > 0) {
					addr = newreg();
					inst("%s = getelementptr i8, ptr %s, i64 %d",
						addr, p->x.name, 8*j);
				}
				inst("store %s %%a%d.%d, ptr %s, align %d", part[j], i, j,
					addr, p->type->align);
			}
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
		char *part[2];
		int i, j, k, n = 0;
		print("declare %s %s(", rettypestr(p->type), p->x.name);
		r.gp = 6;
		r.fp = 8;
		if (isstruct(freturn(p->type))) {	/* the result address */
			print("ptr");
			n++;
			r.gp--;
		}
		if (proto && !p->type->u.f.oldstyle) {
			for (i = 0; proto[i] && proto[i] != voidtype; i++)
				if (!isstruct(proto[i])) {
					print("%s%s", n++ ? ", " : "", argtypestr(proto[i]));
					scalarreg(argtypestr(proto[i]), &r);
				} else if ((k = structparts(proto[i], &r, part)) == 0)
					print("%sptr %s", n++ ? ", " : "", byvalstr(proto[i]));
				else
					for (j = 0; j < k; j++)
						print("%s%s", n++ ? ", " : "", part[j]);
			if (variadic(p->type))
				print(", ...");
		} else
			print("%s...", n ? ", " : "");
		print(")\n");
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
	if (firstfile && *firstfile) {
		print("source_filename = \"");
		emitescaped(strlen(firstfile), firstfile);
		print("\"\n");
	}
	print("target datalayout = \"e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128\"\n");
	print("target triple = \"x86_64-pc-linux-gnu\"\n\n");
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

Interface llvmIR = {
	1, 1, 0,	/* char */
	2, 2, 0,	/* short */
	4, 4, 0,	/* int */
	8, 8, 0,	/* long */
	8, 8, 0,	/* long long */
	4, 4, 0,	/* float */
	8, 8, 0,	/* double */
	8, 8, 0,	/* long double (same as double for now) */
	8, 8, 0,	/* T* */
	0, 1, 0,	/* struct */
	1,		/* little_endian */
	0,		/* mulops_calls */
	0,		/* wants_callb */
	1,		/* wants_argb */
	1,		/* left_to_right */
	1,		/* wants_dag */
	0,		/* unsigned_char */
	I(address),
	I(blockbeg),
	I(blockend),
	I(defaddress),
	I(defconst),
	I(defstring),
	I(defsymbol),
	I(emit),
	I(export),
	I(function),
	I(gen),
	I(global),
	I(import),
	I(local),
	I(progbeg),
	I(progend),
	I(segment),
	I(space),
	I(stabblock),
	I(stabend),
	I(stabfend),
	I(stabinit),
	I(stabline),
	I(stabsym),
	I(stabtype)
};
