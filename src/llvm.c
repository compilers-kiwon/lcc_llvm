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

/* addresses of locals computed by address(), emitted after the allocas */
struct laddr {
	Symbol q, p;
	long n;
	struct laddr *link;
};
static struct laddr *laddrs, **ladtail;

static int nreg, nblock, nlocal;
static int terminated;		/* current basic block has its terminator */
static char *rettype;		/* LLVM return type of the current function */

static char *gname(Symbol);

static void unsupported(Node p) {
	error("llvm back end: unsupported operator `%s'\n", opname(p->op));
}

/* inst - emit one instruction, opening a new basic block if needed */
static void inst(const char *fmt, ...) {
	va_list ap;

	if (terminated) {
		print("b%d:\n", ++nblock);
		terminated = 0;
	}
	print("  ");
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

/* argtypestr - LLVM type of an argument of type ty */
static char *argtypestr(Type ty) {
	if (isstruct(ty))	/* wants_argb=0: passed by address */
		return "ptr";
	if (isint(ty) || isenum(ty))
		return typestr(promote(ty));
	return typestr(ty);
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
		if (optype(p->op) == B) {
			unsupported(p);
			return "undef";
		}
		{
			char *a = nodeval(p->kids[0]);
			v = newreg();
			inst("%s = load %s, ptr %s", v, optypestr(p->op), a);
		}
		break;
	case ADD: case SUB: case MUL: case DIV: case MOD:
	case BAND: case BOR: case BXOR:
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
	case NEG: case BCOM:
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
	default:
		unsupported(p);
		return "undef";
	}
	setval(p, v);
	return v;
}

/* emitnode - emit code for the root node p */
static void emitnode(Node p) {
	switch (generic(p->op)) {
	case LABEL:
		if (!terminated)
			print("  br label %%%s\n", gname(p->syms[0]));
		print("%s:\n", gname(p->syms[0]));
		terminated = 0;
		break;
	case JUMP:
		if (specific(p->kids[0]->op) != ADDRG+P) {
			unsupported(p);
			break;
		}
		inst("br label %%%s", gname(p->kids[0]->syms[0]));
		terminated = 1;
		break;
	case ASGN:
		if (optype(p->op) == B) {
			unsupported(p);
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
	if (p->scope == LABELS) {
		error("llvm back end: label addresses are not supported\n");
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
	for ( ; p; p = p->link)
		emitnode(p);
}

static void I(export)(Symbol p) {}

/* function - generate code for a function */
static void I(function)(Symbol f, Symbol caller[], Symbol callee[], int ncalls) {
	int i;
	struct laddr *a;
	Gdata g;

	flushglobal();
	infunc = 1;
	memset(nodevals, 0, sizeof nodevals);
	laddrs = NULL;
	ladtail = &laddrs;
	nreg = nblock = 0;
	terminated = 0;
	rettype = rettypestr(f->type);

	print("\ndefine %s%s %s(", f->sclass == STATIC ? "internal " : "",
		rettype, gname(f));
	for (i = 0; caller[i]; i++)
		print("%s%s %%a%d", i ? ", " : "", typestr(caller[i]->type), i);
	if (variadic(f->type))
		print("%s...", i ? ", " : "");
	print(") {\nentry:\n");
	if (strcmp(rettype, "void") != 0)
		inst("%%retval = alloca %s", rettype);
	for (i = 0; caller[i]; i++) {
		Symbol p = caller[i], q = callee[i];
		p->x.name = localname(p);
		emitalloca(p);
		inst("store %s %%a%d, ptr %s", typestr(p->type), i, p->x.name);
		if (q->sclass == p->sclass && q->type == p->type)
			q->x.name = p->x.name;
		else {	/* the front end assigns caller to callee */
			q->x.name = localname(q);
			emitalloca(q);
		}
	}
	gencode(caller, callee);
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
	if (isfunc(p->type)) {
		Type *proto = p->type->u.f.proto;
		int i, n = 0;
		print("declare %s %s(", rettypestr(p->type), p->x.name);
		if (proto && !p->type->u.f.oldstyle) {
			for (i = 0; proto[i] && proto[i] != voidtype; i++)
				print("%s%s", n++ ? ", " : "", argtypestr(proto[i]));
			if (variadic(p->type))
				print(", ...");
		} else
			print("...");
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
	0,		/* wants_argb */
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
