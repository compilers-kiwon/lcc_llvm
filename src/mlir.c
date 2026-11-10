#include "c.h"
#include "abi.h"
#include "target.h"

/*
 * MLIR back end: emits MLIR (.mlir) for the targets in target.h, as
 * mlir/x86_64-linux, which mlir-opt and mlir-translate lower to LLVM
 * IR.  Types and the dags from the front end are those of the llvm
 * back end for the same target.
 *
 * The operations of the module are written to a temporary file, which
 * progend copies into the module op.  What must come before the module
 * op, as type aliases, is known only at the end.
 */

#define I(f) mlir_##f
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
	int suffix, size;	/* Kconst: type suffix, size ... */
	Value v;		/* ... and value */
	char *s;		/* Kstring */
	int n;			/* Kstring, Kspace */
	Symbol sym;		/* Kaddr: a variable ... */
	long off;		/* ... plus an offset */
	Item link;
};
static Symbol pending;		/* global being initialized */
static Symbol swtable;		/* table of the switch just emitted as cf.switch */
static int skipping;		/* the global being initialized is that table */
static Item items, *itail;	/* its initial value so far */

/*
 * An initial value that is not a number or bytes alone is built of
 * parts: runs of bytes of constants and strings, runs of zero bytes,
 * and addresses.  More than 256 parts go in groups of 256, recursively,
 * which are parts too: mlir-translate takes time quadratic in the
 * number of elements of an aggregate.  Each operation on an aggregate
 * repeats its type, so structure types are named by aliases.
 */
enum { Pbytes, Pzero, Paddr, Pgroup };
struct ipart {
	int kind;
	char *type;		/* MLIR type */
	Item it;		/* first item, unless a group */
	int n;			/* bytes of a run, parts of a group */
	struct ipart *parts;	/* those of a group */
};

/* addresses of globals plus constants, made by address() */
struct gaddr {
	Symbol q;		/* q stands for ... */
	Symbol p;		/* ... the address of p ... */
	long n;			/* ... plus n */
	struct gaddr *link;
};
static struct gaddr *gaddrs[256];

/* type aliases, as !s1, in the order of their definition; progend
   defines them before the module op, since an alias must be defined
   before it is used */
struct alias {
	char *type;		/* interned */
	char *name;
	struct alias *link;	/* in a hash chain */
	struct alias *next;	/* in the order of definition */
};
static struct alias *aliashash[256], *aliases, **aliastail = &aliases;
static int naliases;

static FILE *module;		/* operations of the module, until progend */
static int llvmfunc;		/* -llvmfunc: every function is an llvm.func */
static int nval;		/* values made by the initializer being emitted */

/* ginst - emit an operation of the region that makes an initial value */
static void ginst(const char *fmt, ...) {
	va_list ap;

	fprint(module, "  ");
	va_start(ap, fmt);
	vfprint(module, NULL, fmt, ap);
	va_end(ap);
	fprint(module, "\n");
}

static char *newval(void) {
	return stringf("%%v%d", ++nval);
}

/* flttypestr - MLIR type of a floating type of size bytes; a long
   double wider than double has the target's type */
static char *flttypestr(int size) {
	return size == 4 ? "f32" : size == 8 ? "f64" : tg->mlirld;
}

/* typestr - MLIR type for objects of type ty */
static char *typestr(Type ty) {
	ty = unqual(ty);
	switch (ty->op) {
	case INT: case UNSIGNED: case ENUM:
		return stringf("i%d", 8*ty->size);
	case FLOAT:
		return flttypestr(ty->size);
	case POINTER:
		return "!llvm.ptr";
	default:
		return stringf("!llvm.array<%d x i8>", ty->size);
	}
}

/* sext - integer constant u of size bytes, sign-extended */
static long sext(unsigned long u, int size) {
	int bits = 8*size;

	if (bits < 8*sizeof u) {
		u &= ones(bits);
		if (u>>(bits - 1))
			u |= ~ones(bits);
	}
	return (long)u;
}

/* hexdigits - put n hex digits of u into buf[i..i+n-1] */
static void hexdigits(char *buf, int i, int n, unsigned long u) {
	while (n-- > 0) {
		buf[i + n] = "0123456789ABCDEF"[u&0xF];
		u >>= 4;
	}
}

/* fltbytes - set b[0..size-1] to the bytes of float or double constant
   d of size 4 or 8, the least significant first, as the host represents
   it; an unsigned long may have only 32 bits */
static void fltbytes(long double d, int size, unsigned char b[]) {
	static union { int x; char endian; } little = { 1 };
	unsigned char t[sizeof (double)];
	float f = (float)d;
	double x = (double)d;
	int i;

	assert(sizeof f == 4 && sizeof x == 8);
	if (size == 4)
		memcpy(t, &f, sizeof f);
	else
		memcpy(t, &x, sizeof x);
	for (i = 0; i < size; i++)
		b[i] = t[little.endian ? i : size - 1 - i];
}

/* fltstr - floating constant d of size bytes as a hex literal of its
   bits, which MLIR takes exactly: 8 or 16 hex digits, for the x87
   80 bits 4 of the sign and the exponent and 16 of the significand, and
   for IEEE quad 32 */
static char *fltstr(Value v, int size) {
	long double d = v.d;
	char buf[35];
	unsigned se;
	unsigned long hi, lo;

	buf[0] = '0', buf[1] = 'x';
	if (size <= 8) {
		unsigned char b[8];
		int i;
		fltbytes(d, size, b);
		for (i = 0; i < size; i++)
			hexdigits(buf, 2 + 2*i, 2, b[size - 1 - i]);
		buf[2 + 2*size] = 0;
	} else if (IR->quadld) {
		unsigned long w[4];
		int i;
		abi_fp128bits(v.q, w);
		for (i = 0; i < 4; i++)
			hexdigits(buf, 2 + 8*i, 8, w[i]);
		buf[34] = 0;
	} else {
		sysv_fp80bits(d, &se, &hi, &lo);
		hexdigits(buf, 2, 4, se);
		hexdigits(buf, 6, 8, hi);
		hexdigits(buf, 14, 8, lo);
		buf[22] = 0;
	}
	return string(buf);
}

/* gname - name of global symbol p, defining it if necessary */
static char *gname(Symbol p) {
	if (p->x.name == NULL)
		(*IR->defsymbol)(p);
	assert(p->x.name);
	return p->x.name;
}

/* gaddr - the address made by address() that q stands for, or NULL */
static struct gaddr *gaddr(Symbol q) {
	struct gaddr *a;

	for (a = gaddrs[((unsigned long)q>>3)&(NELEMS(gaddrs)-1)]; a; a = a->link)
		if (a->q == q)
			return a;
	return NULL;
}

/* alias - the alias of type ty, an interned string, defining it if new */
static char *alias(char *ty) {
	struct alias *a;
	unsigned h = ((unsigned long)ty>>3)&(NELEMS(aliashash)-1);

	for (a = aliashash[h]; a; a = a->link)
		if (a->type == ty)
			return a->name;
	NEW(a, PERM);
	a->type = ty;
	a->name = stringf("!s%d", ++naliases);
	a->link = aliashash[h];
	aliashash[h] = a;
	a->next = NULL;
	*aliastail = a;
	aliastail = &a->next;
	return a->name;
}

/* itemsize - the number of bytes of item it */
static int itemsize(Item it) {
	switch (it->kind) {
	case Kconst:  return it->size;
	case Kstring: case Kspace: return it->n;
	case Kaddr:   return voidptype->size;
	}
	assert(0);
	return 0;
}

/* itembytes - put the bytes of constant item it into b, return their
   number */
static int itembytes(Item it, unsigned char b[]) {
	unsigned long u, hi, lo;
	unsigned se;
	int i;

	switch (it->suffix) {
	case I: u = it->v.i; break;
	case U: u = it->v.u; break;
	case P: u = (unsigned long)it->v.p; break;
	case F:
		if (it->size <= 8) {
			fltbytes(it->v.d, it->size, b);
			return it->size;
		}
		if (IR->quadld) {
			unsigned long w[4];
			abi_fp128bits(it->v.q, w);
			for (i = 0; i < 16; i++)
				b[i] = w[3 - i/4]>>8*(i%4);
			return it->size;
		}
		sysv_fp80bits(it->v.d, &se, &hi, &lo);
		for (i = 0; i < 4; i++) {
			b[i] = lo>>8*i;
			b[i + 4] = hi>>8*i;
		}
		b[8] = se;
		b[9] = se>>8;
		for (i = 10; i < it->size; i++)
			b[i] = 0;
		return it->size;
	default:
		assert(0);
		return 0;
	}
	for (i = 0; i < it->size; i++) {	/* u may have fewer bytes */
		b[i] = i < (int)sizeof u ? u&0xFF
			: it->suffix == I && it->v.i < 0 ? 0xFF : 0;
		u >>= 4;
		u >>= 4;
	}
	return it->size;
}

/* emitbytes - emit the n bytes s in a string literal, as \XX unless
   printable and not a quote or a backslash */
static void emitbytes(unsigned char *s, int n) {
	for ( ; n-- > 0; s++)
		if (*s >= ' ' && *s < 0177 && *s != '"' && *s != '\\')
			putc(*s, module);
		else {
			putc('\\', module);
			putc("0123456789ABCDEF"[*s>>4], module);
			putc("0123456789ABCDEF"[*s&0xF], module);
		}
}

/* emitrun - emit the n bytes of the constants and strings from item it
   on as a string literal */
static void emitrun(Item it, int n) {
	unsigned char b[16];

	putc('"', module);
	for ( ; n > 0; it = it->link) {
		assert(it->kind == Kconst || it->kind == Kstring);
		if (it->kind == Kstring)
			emitbytes((unsigned char *)it->s, it->n);
		else
			emitbytes(b, itembytes(it, b));
		n -= itemsize(it);
	}
	putc('"', module);
}

/* initparts - the parts of initial value items, *np of them */
static struct ipart *initparts(Item items, int *np) {
	struct ipart *p;
	Item it;
	int k, n = 0;

	for (it = items; it; it = it->link)
		n++;
	p = newarray(n, sizeof *p, STMT);	/* at most one part per item */
	n = 0;
	for (it = items; it; it = it->link) {
		k = it->kind == Kaddr ? Paddr : it->kind == Kspace ? Pzero : Pbytes;
		if (n == 0 || k == Paddr || k != p[n-1].kind) {
			p[n].kind = k;
			p[n].it = it;
			p[n].n = 0;
			p[n].parts = NULL;
			n++;
		}
		p[n-1].n += itemsize(it);
	}
	for (k = 0; k < n; k++)
		p[k].type = p[k].kind == Paddr ? "!llvm.ptr"
			: stringf("!llvm.array<%d x i8>", p[k].n);
	*np = n;
	return p;
}

/* aggtype - the type of an aggregate of the n parts p: an array of
   pointers, or else a packed structure, by its alias.  The types of
   its elements are written without the !llvm. of the llvm dialect */
static char *aggtype(struct ipart *p, int n) {
	char *buf, *s, *t;
	int i, len = sizeof "!llvm.struct<packed ()>";

	for (i = 0; i < n && p[i].kind == Paddr; i++)
		;
	if (i == n)
		return stringf("!llvm.array<%d x ptr>", n);
	for (i = 0; i < n; i++)
		len += strlen(p[i].type) + 2;
	buf = allocate(len, STMT);
	s = buf + sprintf(buf, "!llvm.struct<packed (");
	for (i = 0; i < n; i++) {
		t = p[i].type;
		if (strncmp(t, "!llvm.", 6) == 0)
			t += 6;
		s += sprintf(s, "%s%s", i > 0 ? ", " : "", t);
	}
	strcpy(s, ")>");
	return alias(string(buf));
}

/* group - the n parts p, n > 1, as one part: an aggregate of them, or
   of groups of 256 of them, recursively, if there are more */
static struct ipart *group(struct ipart *p, int n) {
	struct ipart *q;
	int i, m;

	do {
		m = (n + 255)/256;
		q = newarray(m, sizeof *q, STMT);
		for (i = 0; i < m; i++) {
			q[i].kind = Pgroup;
			q[i].it = NULL;
			q[i].n = i < m - 1 ? 256 : n - 256*i;
			q[i].parts = p + 256*i;
			q[i].type = aggtype(q[i].parts, q[i].n);
		}
		p = q;
		n = m;
	} while (n > 1);
	return p;
}

static char *functype(Symbol);

/* addrval - emit the address of variable or function p plus n, return
   its value */
static char *addrval(Symbol p, long n) {
	char *v, *c, *w, *t;

	if (isfunc(p->type) && (t = functype(p)) != NULL) {
		c = newval();
		ginst("%s = func.constant @\"%s\" : %s", c, gname(p), t);
		v = newval();
		ginst("%s = builtin.unrealized_conversion_cast %s : %s to !llvm.ptr",
			v, c, t);
	} else {
		v = newval();
		ginst("%s = llvm.mlir.addressof @\"%s\" : !llvm.ptr", v, gname(p));
	}
	if (n == 0)
		return v;
	/* static indices are i32, and their minimum means a dynamic one */
	if (n < -2147483647L || n > 2147483647L) {
		c = newval();
		ginst("%s = llvm.mlir.constant(%D : i64) : i64", c, n);
		w = newval();
		ginst("%s = llvm.getelementptr %s[%s] : (!llvm.ptr, i64) -> !llvm.ptr, i8",
			w, v, c);
	} else {
		w = newval();
		ginst("%s = llvm.getelementptr %s[%D] : (!llvm.ptr) -> !llvm.ptr, i8",
			w, v, n);
	}
	return w;
}

/* ptrval - emit pointer constant p, return its value */
static char *ptrval(void *p) {
	char *v = newval(), *w;

	if (p == NULL) {
		ginst("%s = llvm.mlir.zero : !llvm.ptr", v);
		return v;
	}
	ginst("%s = llvm.mlir.constant(%D : i64) : i64", v, (long)p);
	w = newval();
	ginst("%s = llvm.inttoptr %s : i64 to !llvm.ptr", w, v);
	return w;
}

/* partval - emit the value of part p, return it */
static char *partval(struct ipart *p) {
	char *v = NULL, *e, *w;
	int i;

	switch (p->kind) {
	case Pbytes:
		v = newval();
		fprint(module, "  %s = llvm.mlir.constant(", v);
		emitrun(p->it, p->n);
		fprint(module, ") : %s\n", p->type);
		break;
	case Pzero:
		v = newval();
		ginst("%s = llvm.mlir.zero : %s", v, p->type);
		break;
	case Paddr:
		v = addrval(p->it->sym, p->it->off);
		break;
	case Pgroup:
		v = newval();
		ginst("%s = llvm.mlir.undef : %s", v, p->type);
		for (i = 0; i < p->n; i++) {
			e = partval(&p->parts[i]);
			w = newval();
			ginst("%s = llvm.insertvalue %s, %s[%d] : %s", w, e, v, i,
				p->type);
			v = w;
		}
		break;
	default:
		assert(0);
	}
	return v;
}

/*
 * emitglobal - define global p with initial value items.  A number or
 * a run of bytes is an attribute.  Other values are made in a region:
 * a pointer constant, a part (zeros, of p's type if they are all of p,
 * or an address), or an aggregate of parts.  No items, which only
 * errors leave, make no bytes.
 */
static void emitglobal(Symbol p, Item items) {
	Item it = items;
	struct ipart *parts = NULL;
	char *ty, *v;
	int n, align = abi_varalign(tg->abi, p);

	fprint(module, "llvm.mlir.global %s%s @\"%s\"(",
		p->sclass != STATIC ? "external" : p->generated ? "private" : "internal",
		p->u.seg == LIT ? " constant" : "", gname(p));
	if (it && it->link == NULL && it->kind == Kconst && it->suffix != P) {
		ty = it->suffix == F ? flttypestr(it->size) : stringf("i%d", 8*it->size);
		fprint(module, "%s : %s) {alignment = %d : i64} : %s\n",
			it->suffix == F ? fltstr(it->v, it->size)
			: stringf("%D", sext(it->suffix == I ? it->v.i : it->v.u, it->size)),
			ty, align, ty);
		return;
	}
	if (it && it->link == NULL && it->kind == Kconst)	/* a pointer */
		ty = "!llvm.ptr";
	else if (it == NULL)
		ty = "!llvm.array<0 x i8>";
	else {
		parts = initparts(items, &n);
		if (n == 1 && parts->kind == Pbytes) {
			emitrun(parts->it, parts->n);
			fprint(module, ") {alignment = %d : i64} : %s\n", align,
				parts->type);
			return;
		}
		if (n > 1)
			parts = group(parts, n);
		else if (parts->kind == Pzero && parts->n == p->type->size)
			parts->type = typestr(p->type);
		ty = parts->type;
	}
	fprint(module, ") {alignment = %d : i64} : %s {\n", align, ty);
	nval = 0;
	if (parts)
		v = partval(parts);
	else if (it)	/* the pointer constant */
		v = ptrval(it->v.p);
	else {		/* no items */
		v = newval();
		ginst("%s = llvm.mlir.zero : %s", v, ty);
	}
	ginst("llvm.return %s : %s", v, ty);
	fprint(module, "}\n");
}

/* flushglobal - emit the pending global, if any */
static void flushglobal(void) {
	if (pending) {
		emitglobal(pending, items);
		pending = NULL;
	}
}

static Item newitem(int kind) {
	Item it;

	assert(pending);
	NEW0(it, PERM);
	it->kind = kind;
	*itail = it;
	itail = &it->link;
	return it;
}


/*
 * A function is written to three files: the pool, the constants and
 * addresses of globals that the entry block makes once for the whole
 * function; the prologue, its allocas and the stores of its
 * parameters; and the body, block by block.  At its end the entry
 * block and the blocks that it reaches are copied to the module: MLIR's
 * conversions leave the operations of unreachable blocks behind.  The
 * entry block has no name, so nothing jumps to it, as MLIR requires.
 */
static FILE *pool, *prologue, *body;

struct succ {
	struct block *b;
	struct succ *link;
};
struct block {
	char *name;		/* interned */
	long start;		/* offset of its text in body */
	int defined, reached;
	struct succ *succs;
	struct block *hlink;	/* in a hash chain */
	struct block *dlink;	/* in the order of definition */
	struct block *wlink;	/* in the work list of reach */
};
static struct block *bhash[256], *bfirst, **bdtail, *cur;
static int terminated;		/* the current block has ended */

/* values in the pool, by their interned keys */
struct pooled {
	char *key, *name;
	struct pooled *link;
};
static struct pooled *poolhash[256];

/* values of nodes */
struct nodeval {
	Node node;
	char *val;
	struct nodeval *link;
};
static struct nodeval *nodevals[512];

/* addresses of locals plus constants, made by address() */
struct laddr {
	Symbol q, p;
	long n;
	struct laddr *link;
};
static struct laddr *laddrs, **ladtail;

/* how each function is represented, decided where it is first emitted */
enum { UNDECIDED, FUNCFUNC, LLVMFUNC };
struct fninfo {
	Symbol p;
	int kind;
	char *params;		/* FUNCFUNC: types of the parameters ... */
	char *typed;		/* ... with their attributes ... */
	char *result;		/* ... and of the result, NULL if none */
	char *type;		/* ... as a function type */
	struct fninfo *link;
};
static struct fninfo *fninfos[256];

/* arguments listed since the last call; they all belong to the next CALL */
struct arg {
	char *type, *val;
	Type sty;	/* a structure: val is its address, type is NULL */
	char *attr;	/* attributes, as byval */
	int fixed;	/* matches a parameter, not ... */
	struct arg *link;
};
static struct arg *args, **argtail = &args;

static int nreg, nblock, nlocal, ncst;
static char *rettype;		/* type of %retval, NULL if none */
static char *restype;		/* result type, NULL if none */
static int fkind;		/* FUNCFUNC or LLVMFUNC: the current function */
static Node emitting;		/* the forest being emitted */

static char *nodeval(Node);

/* cat - the concatenation of the strings up to a NULL, of any length */
static char *cat(const char *s, ...) {
	va_list ap;
	const char *t;
	char *r, *q;
	int n = 0;

	va_start(ap, s);
	for (t = s; t; t = va_arg(ap, const char *))
		n += strlen(t);
	va_end(ap);
	q = r = allocate(n + 1, PERM);
	va_start(ap, s);
	for (t = s; t; t = va_arg(ap, const char *)) {
		strcpy(q, t);
		q += strlen(t);
	}
	va_end(ap);
	return r;
}

static char *unsupported(Node p) {
	error("mlir back end: unsupported operator `%s'\n", opname(p->op));
	return "%unsupported";
}

static struct block *blockof(char *name) {
	struct block *b;
	unsigned h;

	name = string(name);
	h = ((unsigned long)name>>3)&(NELEMS(bhash)-1);
	for (b = bhash[h]; b; b = b->hlink)
		if (b->name == name)
			return b;
	NEW0(b, FUNC);
	b->name = name;
	b->hlink = bhash[h];
	bhash[h] = b;
	return b;
}

/* startblock - start block name, with arguments params or NULL */
static void startblock(char *name, char *params) {
	cur = blockof(name);
	assert(!cur->defined);
	cur->defined = 1;
	cur->start = ftell(body);
	*bdtail = cur;
	bdtail = &cur->dlink;
	if (params)
		fprint(body, "^%s(%s):\n", cur->name, params);
	else
		fprint(body, "^%s:\n", cur->name);
	terminated = 0;
}

/* inst - emit an operation of the body, in a new block after a
   terminator */
static void inst(const char *fmt, ...) {
	va_list ap;

	if (terminated)
		startblock(stringf("b%d", ++nblock), NULL);
	fprint(body, "  ");
	va_start(ap, fmt);
	vfprint(body, NULL, fmt, ap);
	va_end(ap);
	fprint(body, "\n");
}

/* pinst - emit an operation of the prologue */
static void pinst(const char *fmt, ...) {
	va_list ap;

	fprint(prologue, "  ");
	va_start(ap, fmt);
	vfprint(prologue, NULL, fmt, ap);
	va_end(ap);
	fprint(prologue, "\n");
}

/* edge - record that the current block may jump to block name */
static void edge(char *name) {
	struct succ *s;

	NEW(s, FUNC);
	s->b = blockof(name);
	s->link = cur->succs;
	cur->succs = s;
}

/* br - end the current block with a jump to block name, passing argv
   (as values : types) or NULL */
static void br(char *name, char *argv) {
	if (argv)
		inst("cf.br ^%s(%s)", name, argv);
	else
		inst("cf.br ^%s", name);
	edge(name);
	terminated = 1;
}

/* condbr - end the current block with a jump to block t if c is true
   and to block f if not */
static void condbr(char *c, char *t, char *f) {
	inst("cf.cond_br %s, ^%s, ^%s", c, t, f);
	edge(t);
	edge(f);
	terminated = 1;
}

/* reach - mark the blocks that the entry block reaches */
static void reach(void) {
	struct block *b, *work = bfirst;
	struct succ *s;

	bfirst->reached = 1;
	bfirst->wlink = NULL;
	while ((b = work) != NULL) {
		work = b->wlink;
		for (s = b->succs; s; s = s->link)
			if (!s->b->reached) {
				s->b->reached = 1;
				s->b->wlink = work;
				work = s->b;
			}
	}
}

/* copytext - copy bytes from up to to of f to the module */
static void copytext(FILE *f, long from, long to) {
	char buf[BUFSIZE];
	size_t n;

	fseek(f, from, SEEK_SET);
	for ( ; from < to; from += n) {
		n = to - from < (long)sizeof buf ? to - from : sizeof buf;
		n = fread(buf, 1, n, f);
		assert(n > 0);
		fwrite(buf, 1, n, module);
	}
}

/* flushfunc - copy the pool, the prologue and the reached blocks of
   the body to the module */
static void flushfunc(void) {
	struct block *b;
	long end = ftell(body);

	copytext(pool, 0, ftell(pool));
	copytext(prologue, 0, ftell(prologue));
	reach();
	for (b = bfirst; b; b = b->dlink)
		if (b->reached)
			copytext(body, b->start, b->dlink ? b->dlink->start : end);
}

static char *newreg(void) {
	return stringf("%%t%d", ++nreg);
}

static char *lookupval(Node p) {
	struct nodeval *e;

	for (e = nodevals[((unsigned long)p>>4)&(NELEMS(nodevals)-1)]; e; e = e->link)
		if (e->node == p)
			return e->val;
	return NULL;
}

static void setval(Node p, char *val) {
	struct nodeval *e;
	unsigned h = ((unsigned long)p>>4)&(NELEMS(nodevals)-1);

	NEW(e, FUNC);
	e->node = p;
	e->val = val;
	e->link = nodevals[h];
	nodevals[h] = e;
}

/* pooled - the name of the pool value with key, or NULL; the key is
   interned */
static char *pooled(char *key) {
	struct pooled *e;

	for (e = poolhash[((unsigned long)key>>3)&(NELEMS(poolhash)-1)]; e; e = e->link)
		if (e->key == key)
			return e->name;
	return NULL;
}

/* topool - name the pool value with key, and emit op, which makes it */
static char *topool(char *key, char *name, char *op) {
	struct pooled *e;
	unsigned h = ((unsigned long)key>>3)&(NELEMS(poolhash)-1);

	NEW(e, FUNC);
	e->key = key;
	e->name = name;
	e->link = poolhash[h];
	poolhash[h] = e;
	fprint(pool, "  %s = %s\n", name, op);
	return name;
}

/* iconst - integer constant n of MLIR type ty, as %c<n>_<ty> */
static char *iconst(long n, char *ty) {
	char *v = string(stringf("%%c%D_%s", n, ty));

	if (pooled(v))
		return v;
	return topool(v, v, stringf("arith.constant %D : %s", n, ty));
}

/* boolconst - i1 constant b, as %true or %false */
static char *boolconst(int b) {
	char *v = string(b ? "%true" : "%false");

	return pooled(v) ? v : topool(v, v, b ? "arith.constant true" : "arith.constant false");
}

/* fltconst - floating constant c of size bytes */
static char *fltconst(Value c, int size) {
	char *ty = flttypestr(size), *lit = fltstr(c, size);
	char *k = string(stringf("%s : %s", lit, ty)), *v = pooled(k);

	if (v)
		return v;
	return topool(k, stringf("%%cst%d", ++ncst),
		stringf("arith.constant %s : %s", lit, ty));
}

/* ptrconst - pointer constant p */
static char *ptrconst(void *p) {
	char *c, *v;

	if (p == NULL) {
		v = string("%null");
		return pooled(v) ? v
			: topool(v, v, "llvm.mlir.zero : !llvm.ptr");
	}
	c = iconst((long)p, "i64");
	v = string(stringf("%%ptr%D", (long)p));
	return pooled(v) ? v
		: topool(v, v, stringf("llvm.inttoptr %s : i64 to !llvm.ptr", c));
}

/* gaddrval - the address of global variable or function p plus n, as
   %<p>.addr or %<p>.addr.<n>; the address of a func.func is made from
   %<p>.fn, a value of its function type */
static char *gaddrval(Symbol p, long n) {
	char *base = string(stringf("%%%s.addr", gname(p))), *v, *c, *t;

	if (pooled(base))
		;
	else if (isfunc(p->type) && (t = functype(p)) != NULL) {
		c = string(stringf("%%%s.fn", gname(p)));
		topool(c, c, stringf("func.constant @\"%s\" : %s", gname(p), t));
		topool(base, base, stringf("builtin.unrealized_conversion_cast %s : %s to !llvm.ptr",
			c, t));
	} else
		topool(base, base, stringf("llvm.mlir.addressof @\"%s\" : !llvm.ptr",
			gname(p)));
	if (n == 0)
		return base;
	v = string(stringf("%s.%D", base, n));
	if (pooled(v))
		return v;
	/* static indices are i32, and their minimum means a dynamic one */
	if (n < -2147483647L || n > 2147483647L) {
		c = iconst(n, "i64");
		return topool(v, v, stringf("llvm.getelementptr %s[%s] : (!llvm.ptr, i64) -> !llvm.ptr, i8",
			base, c));
	}
	return topool(v, v, stringf("llvm.getelementptr %s[%D] : (!llvm.ptr) -> !llvm.ptr, i8",
		base, n));
}

/* optypestr - MLIR type of operator op */
static char *optypestr(int op) {
	switch (optype(op)) {
	case I: case U: return stringf("i%d", 8*opsize(op));
	case F: return flttypestr(opsize(op));
	case P: return "!llvm.ptr";
	}
	assert(0);
	return NULL;
}

/* cnstval - constant v of operator op's type and size */
static char *cnstval(int op, Value v) {
	switch (optype(op)) {
	case I: return iconst(sext(v.i, opsize(op)), optypestr(op));
	case U: return iconst(sext(v.u, opsize(op)), optypestr(op));
	case P: return ptrconst(v.p);
	case F: return fltconst(v, opsize(op));
	}
	assert(0);
	return NULL;
}

/* opnd - the value of node k as an operand of op: an integer constant
   of another size is made again in op's type */
static char *opnd(Node k, int op) {
	if (generic(k->op) == CNST && (optype(k->op) == I || optype(k->op) == U)
	&& (optype(op) == I || optype(op) == U) && opsize(k->op) != opsize(op))
		return iconst(sext(k->syms[0]->u.c.v.u, opsize(k->op)),
			optypestr(op));
	return nodeval(k);
}

static char *localname(Symbol p) {
	if (*p->name >= '0' && *p->name <= '9')
		return stringf("%%T%s.%d", p->name, ++nlocal);
	return stringf("%%%s.%d", p->name, ++nlocal);
}

static void emitalloca(Symbol p) {
	pinst("%s = llvm.alloca %s x %s {alignment = %d : i64} : (i32) -> !llvm.ptr",
		p->x.name, iconst(1, "i32"), typestr(p->type), p->type->align);
}

static struct fninfo *fninfo(Symbol p) {
	struct fninfo *f;
	unsigned h = ((unsigned long)p>>3)&(NELEMS(fninfos)-1);

	for (f = fninfos[h]; f; f = f->link)
		if (f->p == p)
			return f;
	NEW0(f, PERM);
	f->p = p;
	f->link = fninfos[h];
	fninfos[h] = f;
	return f;
}

/* mlirpart - the MLIR type of a part that sysv.c names in LLVM's way */
static char *mlirpart(char *t) {
	return strcmp(t, "double") == 0 ? "f64" : strcmp(t, "float") == 0 ? "f32"
		: strcmp(t, tg->ldtype) == 0 ? tg->mlirld : t;
}

/* scalarreg - take a register for a scalar argument of MLIR type t */
static void scalarreg(char *t, struct regs *r) {
	(*tg->abi->scalar)(strcmp(t, "f64") == 0 ? "double" : strcmp(t, "f32") == 0
		? "float" : strcmp(t, tg->mlirld) == 0 ? tg->ldtype : t, r);
}

/* structparts - sysv_structparts, with MLIR types, for the SysV va_arg */
static int structparts(Type ty, struct regs *r, char *part[]) {
	int i, n = sysv_structparts(ty, r, part);

	for (i = 0; i < n; i++)
		part[i] = mlirpart(part[i]);
	return n;
}

/* extattr - the extension attribute of a scalar argument of MLIR type
   t, or NULL */
static char *extattr(char *t) {
	char *a = tg->abi->extattr ? (*tg->abi->extattr)(t) : NULL;

	return a ? stringf("{llvm.%s}", a) : NULL;
}

/* resstr - the result type t of function type fty, NULL if none, with
   its extension attribute: a char or short result is narrow (restypestr),
   and extended as its type is where the callee extends it (narrowext) */
static char *resstr(char *t, Type fty) {
	Type rty = unqual(freturn(fty));
	char *a;

	if (t == NULL)
		return t;
	if ((isint(rty) || isenum(rty)) && rty->size < inttype->size)
		a = !tg->abi->narrowext ? NULL : isunsigned(rty) ? "zeroext" : "signext";
	else
		a = tg->abi->extattr ? (*tg->abi->extattr)(t) : NULL;
	return a ? stringf("(%s {llvm.%s})", t, a) : t;
}

/* parttype - the MLIR type of part p */
static char *parttype(struct part *p) {
	char *t;
	int i;

	if (!abi_isagg(p))
		return mlirpart(p->e[0].type);
	if (p->type[0] == '[')
		return stringf("!llvm.array<%d x %s>", p->n, mlirpart(p->e[0].type));
	t = "!llvm.struct<(";
	for (i = 0; i < p->n; i++)
		t = cat(t, i ? ", " : "", mlirpart(p->e[i].type), NULL);
	return cat(t, ")>", NULL);
}

/* partattr - the attributes of part p, or NULL */
static char *partattr(struct part *p) {
	return p->alignstack
		? stringf("{llvm.alignstack = %d : i64}", p->alignstack) : NULL;
}

/* elemalign - the alignment of offset off in an object aligned to align */
static int elemalign(int off, int align) {
	return off > 0 && (off & -off) < align ? off & -off : align;
}

/* byvalstr - attributes of a structure argument ty passed in memory */
static char *byvalstr(Type ty) {
	return stringf("{llvm.byval = !llvm.array<%d x i8>, llvm.align = %d : i64}",
		ty->size, ty->align < tg->abi->byvalalign ? tg->abi->byvalalign : ty->align);
}

/* sretstr - attributes of the address of the structure result ty */
static char *sretstr(Type ty) {
	return stringf("{llvm.sret = !llvm.array<%d x i8>, llvm.align = %d : i64}",
		ty->size, ty->align);
}

/* restypestr - result type of function type fty, NULL if none: a char
   or short result has its own type, which callers extend; a structure
   in registers is their type, and one in memory has none */
static char *restypestr(Type fty) {
	Type rty = unqual(freturn(fty));
	struct part part;

	if (isstruct(rty))
		return (*tg->abi->ret)(rty, variadic(fty), &part) ? parttype(&part) : NULL;
	return rty->op == VOID ? NULL : typestr(rty);
}

/* rettypestr - type of %retval: char and short results are promoted */
static char *rettypestr(Type fty) {
	Type rty = unqual(freturn(fty));

	if (rty->op == VOID || isstruct(rty))
		return restypestr(fty);
	if (isint(rty) || isenum(rty))
		rty = promote(rty);
	return typestr(rty);
}

/* intop - the arith operation of integer operator op */
static char *intop(int op) {
	switch (generic(op)) {
	case ADD:  return "addi";
	case SUB:  return "subi";
	case MUL:  return "muli";
	case DIV:  return optype(op) == U ? "divui" : "divsi";
	case MOD:  return optype(op) == U ? "remui" : "remsi";
	case BAND: return "andi";
	case BOR:  return "ori";
	case BXOR: return "xori";
	case LSH:  return "shli";
	case RSH:  return optype(op) == U ? "shrui" : "shrsi";
	}
	assert(0);
	return NULL;
}

/* shiftcount - the count of shift p, which is always an int, in the
   type of the value shifted */
static char *shiftcount(Node p) {
	Node k = p->kids[1];
	char *c, *v, *ty = optypestr(p->op);

	if (generic(k->op) == CNST)
		return iconst(sext(k->syms[0]->u.c.v.u, opsize(k->op)), ty);
	c = nodeval(k);
	if (opsize(k->op) == opsize(p->op))
		return c;
	v = newreg();
	inst("%s = arith.%s %s : %s to %s", v,
		opsize(k->op) < opsize(p->op) ? "extui" : "trunci", c,
		optypestr(k->op), ty);
	return v;
}

/* ptroffset - integer node k as an i64 byte offset, negated if neg; a
   constant that fits a static index, which is an i32 whose minimum means
   a dynamic one, goes in *c and NULL is returned */
static char *ptroffset(Node k, int neg, long *c) {
	char *v, *w;
	long n;

	if (generic(k->op) == CNST) {
		if (optype(k->op) == U && opsize(k->op) < 8)
			n = k->syms[0]->u.c.v.u&ones(8*opsize(k->op));
		else
			n = sext(k->syms[0]->u.c.v.u, opsize(k->op));
		if (neg)
			n = -n;
		if (n >= -2147483647L && n <= 2147483647L) {
			*c = n;
			return NULL;
		}
		return iconst(n, "i64");
	}
	v = nodeval(k);
	if (opsize(k->op) < 8) {
		w = newreg();
		inst("%s = arith.%s %s : %s to i64", w,
			optype(k->op) == U ? "extui" : "extsi", v, optypestr(k->op));
		v = w;
	}
	if (neg) {
		w = newreg();
		inst("%s = arith.subi %s, %s : i64", w, iconst(0, "i64"), v);
		v = w;
	}
	return v;
}

/* ptrarith - emit pointer addition or subtraction p, whose pointer may
   be either kid, as a getelementptr of bytes; return its value */
static char *ptrarith(Node p) {
	Node ptr = p->kids[0], k = p->kids[1];
	char *a, *off, *v = newreg();
	long c;

	if (optype(ptr->op) != P) {
		assert(generic(p->op) == ADD);
		ptr = p->kids[1];
		k = p->kids[0];
	}
	assert(optype(ptr->op) == P && optype(k->op) != P);
	a = nodeval(ptr);
	off = ptroffset(k, generic(p->op) == SUB, &c);
	if (off == NULL)
		inst("%s = llvm.getelementptr %s[%D] : (!llvm.ptr) -> !llvm.ptr, i8",
			v, a, c);
	else
		inst("%s = llvm.getelementptr %s[%s] : (!llvm.ptr, i64) -> !llvm.ptr, i8",
			v, a, off);
	return v;
}

/* argtypestr - type of a parameter of type ty in a prototype: integers
   are promoted, as in definitions */
static char *argtypestr(Type ty) {
	return typestr(isint(ty) || isenum(ty) ? promote(ty) : ty);
}

/* fntypestr - the function type with parameters params and result res */
static char *fntypestr(char *params, char *res) {
	return cat("(", params, ") -> ", res ? res : "()", NULL);
}

/* addparam - add a parameter of type ty with attributes attr (or NULL)
   and name to the lists of sigparams */
static void addparam(char **plain, char **typed, char **named, char *ty,
	char *attr, char *name) {
	char *sep = **plain ? ", " : "";

	*plain = cat(*plain, sep, ty, NULL);
	*typed = cat(*typed, sep, ty, attr ? " " : "", attr ? attr : "", NULL);
	*named = cat(*named, sep, name, ": ", ty, attr ? " " : "", attr ? attr : "",
		NULL);
}

/*
 * sigparams - the parameters of a function with result type rty and
 * parameter types ty[0..n-1] as the ABI passes them: their types in
 * *plain, with attributes in *typed, and with names too in *named.  The
 * address of a structure result in memory comes first as %a0 (sret);
 * the others are %a<base+i>, or %a<base+i>.<j> for the parts of a
 * structure in registers, or else they are passed in memory (byval).
 */
static void sigparams(Type fty, Type ty[], int n, int base, char **plain,
	char **typed, char **named) {
	Type rty = freturn(fty);
	struct regs r;
	struct part rpart, *part;
	char *t;
	int i, j, k;

	*plain = *typed = *named = "";
	(*tg->abi->init)(&r);
	r.vfunc = variadic(fty);
	if (isstruct(rty) && !(*tg->abi->ret)(rty, r.vfunc, &rpart)) {
		addparam(plain, typed, named, "!llvm.ptr", sretstr(rty), "%a0");
		(*tg->abi->scalar)("ptr", &r);
	}
	for (i = 0; i < n; i++)
		if (!isstruct(ty[i])) {
			t = argtypestr(ty[i]);
			addparam(plain, typed, named, t, extattr(t), stringf("%%a%d", base + i));
			scalarreg(t, &r);
		} else if ((k = (*tg->abi->arg)(ty[i], &r, &part)) == ABYVAL)
			addparam(plain, typed, named, "!llvm.ptr", byvalstr(ty[i]),
				stringf("%%a%d", base + i));
		else if (k == ABYREF)
			addparam(plain, typed, named, "!llvm.ptr", NULL,
				stringf("%%a%d", base + i));
		else
			for (j = 0; j < k; j++)
				addparam(plain, typed, named, parttype(&part[j]),
					partattr(&part[j]), stringf("%%a%d.%d", base + i, j));
}

/*
 * fnref - the kind of function p, decided here if this is its first
 * use: a func.func of type T if its type has a prototype and is not
 * variadic, else an llvm.func.  T has the promoted types of the
 * integer parameters, as the definition has.
 */
static int fnref(Symbol p) {
	struct fninfo *f = fninfo(p);
	Type fty = p->type, *proto = fty->u.f.proto;
	char *named;
	int n;

	if (f->kind != UNDECIDED)
		return f->kind;
	if (llvmfunc || proto == NULL || fty->u.f.oldstyle || variadic(fty)) {
		f->kind = LLVMFUNC;
		return f->kind;
	}
	f->kind = FUNCFUNC;
	for (n = 0; proto[n] && proto[n] != voidtype; n++)
		;
	sigparams(fty, proto, n, 0, &f->params, &f->typed, &named);
	f->result = restypestr(fty);
	f->type = fntypestr(f->params, f->result);
	return f->kind;
}

/* functype - the type of function p if it is a func.func, else NULL */
static char *functype(Symbol p) {
	return fnref(p) == FUNCFUNC ? fninfo(p)->type : NULL;
}

/* gepconst - emit the address p plus n bytes, return it */
static char *gepconst(char *p, long n) {
	char *v = newreg();

	inst("%s = llvm.getelementptr %s[%D] : (!llvm.ptr) -> !llvm.ptr, i8", v, p, n);
	return v;
}

/* loadpart - load part p of the structure at addr, aligned to align */
static char *loadpart(struct part *p, char *addr, int align) {
	char *v = NULL, *t = parttype(p);
	int i;

	for (i = 0; i < p->n; i++) {
		struct elem *e = &p->e[i];
		char *x = newreg(), *m = abi_memtype(e), *a;
		a = e->off > 0 ? gepconst(addr, e->off) : addr;
		inst("%s = llvm.load %s {alignment = %d : i64} : !llvm.ptr -> %s",
			x, a, elemalign(e->off, align), mlirpart(m));
		if (m != e->type) {
			char *y = newreg();
			inst("%s = arith.extui %s : %s to %s", y, x, m, e->type);
			x = y;
		}
		if (!abi_isagg(p))
			return x;
		if (v == NULL) {
			v = newreg();
			inst("%s = llvm.mlir.poison : %s", v, t);
		}
		{
			char *w = newreg();
			inst("%s = llvm.insertvalue %s, %s[%d] : %s", w, x, v, i, t);
			v = w;
		}
	}
	return v;
}

/*
 * storepart - store v, the value of part p, in the structure at addr;
 * with pro, in the prologue
 */
static void storepart(struct part *p, char *v, char *addr, int align, int pro) {
	void (*emit)(const char *, ...) = pro ? pinst : inst;
	char *t = parttype(p);
	int i;

	for (i = 0; i < p->n; i++) {
		struct elem *e = &p->e[i];
		char *x = v, *a = addr, *m = abi_memtype(e);
		if (abi_isagg(p)) {
			x = newreg();
			(*emit)("%s = llvm.extractvalue %s[%d] : %s", x, v, i, t);
		}
		if (e->off > 0 && pro) {
			a = newreg();
			pinst("%s = llvm.getelementptr %s[%d] : (!llvm.ptr) -> !llvm.ptr, i8",
				a, addr, e->off);
		} else if (e->off > 0)
			a = gepconst(addr, e->off);
		if (m != e->type) {
			char *y = newreg();
			(*emit)("%s = arith.trunci %s : %s to %s", y, x, e->type, m);
			x = y;
		}
		(*emit)("llvm.store %s, %s {alignment = %d : i64} : %s, !llvm.ptr",
			x, a, elemalign(e->off, align), mlirpart(m));
	}
}

/* ssepart - is part t passed in an SSE register? */
static int ssepart(char *t) {
	return strcmp(t, "f32") == 0 || strcmp(t, "f64") == 0;
}

/*
 * vaaddr - emit the address of the next argument of type ty in va_list
 * ap, and step ap past it, as the SysV ABI does: in the register save
 * area if all of its registers are left, else in the overflow area.
 * The two paths join in a block whose argument is the address.  A
 * structure split between an SSE register and another one is copied
 * from the save area to slot, and its address is slot.
 */
static char *vaaddr(char *ap, Type ty, char *slot) {
	char *part[2], *gp = NULL, *fp = NULL, *fpp = NULL, *c = NULL, *c2, *t;
	char *rsa, *ra, *ovp, *ov, *a, *reg, *mem, *done, *src, *dst, *v;
	int i, n, ngp = 0, nfp = 0, ig = 0, is = 0;
	struct regs r;

	if (isstruct(ty)) {
		r.gp = 6;
		r.fp = 8;
		n = structparts(ty, &r, part);
	} else if (isfloat(ty) && ty->size > doubletype->size)
		n = 0;	/* class X87: always in memory */
	else {
		n = 1;
		part[0] = isfloat(ty) ? "f64" : "i64";
	}
	for (i = 0; i < n; i++)
		if (ssepart(part[i]))
			nfp++;
		else
			ngp++;
	/* gp_offset at 0, fp_offset at 4, overflow_arg_area at 8,
	   reg_save_area at 16 */
	if (ngp > 0) {
		gp = newreg();
		inst("%s = llvm.load %s : !llvm.ptr -> i32", gp, ap);
		c = newreg();
		inst("%s = arith.cmpi ule, %s, %s : i32", c, gp, iconst(6*8 - 8*ngp, "i32"));
	}
	if (nfp > 0) {
		fpp = gepconst(ap, 4);
		fp = newreg();
		inst("%s = llvm.load %s : !llvm.ptr -> i32", fp, fpp);
		c2 = newreg();
		inst("%s = arith.cmpi ule, %s, %s : i32", c2, fp,
			iconst(6*8 + 8*16 - 16*nfp, "i32"));
		if (c) {
			t = newreg();
			inst("%s = arith.andi %s, %s : i1", t, c, c2);
			c2 = t;
		}
		c = c2;
	}
	mem = stringf("b%d", ++nblock);
	done = stringf("b%d", ++nblock);
	a = newreg();
	if (c) {
		reg = stringf("b%d", ++nblock);
		condbr(c, reg, mem);
		startblock(reg, NULL);
		rsa = newreg();
		inst("%s = llvm.load %s : !llvm.ptr -> !llvm.ptr", rsa, gepconst(ap, 16));
		ra = newreg();
		inst("%s = llvm.getelementptr %s[%s] : (!llvm.ptr, i32) -> !llvm.ptr, i8",
			ra, rsa, gp ? gp : fp);
		assert(slot || nfp == 0 || n == 1);
		if (slot) {
			for (i = 0; i < n; i++) {
				src = newreg();
				inst("%s = llvm.getelementptr %s[%s] : (!llvm.ptr, i32) -> !llvm.ptr, i8",
					src, rsa, ssepart(part[i]) ? fp : gp);
				if (ssepart(part[i]) ? is++ : ig++)
					src = gepconst(src, ssepart(part[i]) ? 16 : 8);
				dst = i > 0 ? gepconst(slot, 8*i) : slot;
				v = newreg();
				inst("%s = llvm.load %s {alignment = 8 : i64} : !llvm.ptr -> %s",
					v, src, part[i]);
				inst("llvm.store %s, %s {alignment = %d : i64} : %s, !llvm.ptr",
					v, dst, ty->align, part[i]);
			}
			ra = slot;
		}
		if (ngp > 0) {
			t = newreg();
			inst("%s = arith.addi %s, %s : i32", t, gp, iconst(8*ngp, "i32"));
			inst("llvm.store %s, %s : i32, !llvm.ptr", t, ap);
		}
		if (nfp > 0) {
			t = newreg();
			inst("%s = arith.addi %s, %s : i32", t, fp, iconst(16*nfp, "i32"));
			inst("llvm.store %s, %s : i32, !llvm.ptr", t, fpp);
		}
		br(done, stringf("%s : !llvm.ptr", ra));
	} else
		br(mem, NULL);
	startblock(mem, NULL);
	ovp = gepconst(ap, 8);
	ov = newreg();
	inst("%s = llvm.load %s : !llvm.ptr -> !llvm.ptr", ov, ovp);
	if (ty->align > 8) {	/* round up to 16 */
		char *x = newreg(), *y = newreg(), *z = newreg(), *w = newreg();
		inst("%s = llvm.ptrtoint %s : !llvm.ptr to i64", x, ov);
		inst("%s = arith.addi %s, %s : i64", y, x, iconst(15, "i64"));
		inst("%s = arith.andi %s, %s : i64", z, y, iconst(-16, "i64"));
		inst("%s = llvm.inttoptr %s : i64 to !llvm.ptr", w, z);
		ov = w;
	}
	inst("llvm.store %s, %s : !llvm.ptr, !llvm.ptr", gepconst(ov, roundup(ty->size, 8)),
		ovp);
	br(done, stringf("%s : !llvm.ptr", ov));
	startblock(done, stringf("%s: !llvm.ptr", a));
	return a;
}

/*
 * vaaddrptr - emit the address of the next argument of type ty in
 * va_list ap, and step ap past it, as RISC-V and ARM do: ap points to
 * the next argument in memory, where the callee has stored the argument
 * registers next to the arguments on the stack; each takes a multiple
 * of unit bytes, and starts at a multiple of its alignment if that is
 * more.  An argument of more than byref bytes, if byref is not 0, is
 * passed by the address of a copy (RV64: unit 8, byref 16; RV32: unit 4,
 * byref 8; ARM: unit 4, byref 0).
 */
static char *vaaddrptr(char *ap, Type ty, int unit, int byref) {
	int align = ty->align;
	char *p = newreg();

	byref = byref && ty->size > byref;
	inst("%s = llvm.load %s : !llvm.ptr -> !llvm.ptr", p, ap);
	if (!byref && align > unit) {	/* round up to the alignment */
		char *x = newreg(), *y = newreg(), *z = newreg(), *w = newreg();
		inst("%s = llvm.ptrtoint %s : !llvm.ptr to i64", x, p);
		inst("%s = arith.addi %s, %s : i64", y, x, iconst(align - 1, "i64"));
		inst("%s = arith.andi %s, %s : i64", z, y, iconst(-align, "i64"));
		inst("%s = llvm.inttoptr %s : i64 to !llvm.ptr", w, z);
		p = w;
	}
	inst("llvm.store %s, %s : !llvm.ptr, !llvm.ptr",
		gepconst(p, byref ? unit : roundup(ty->size, unit)), ap);
	if (byref) {
		char *q = newreg();
		inst("%s = llvm.load %s : !llvm.ptr -> !llvm.ptr", q, p);
		p = q;
	}
	return p;
}

/*
 * vaaddr64 - emit the address of the next argument of type ty in va_list
 * ap, and step ap past it, as AAPCS64 does.  ap has __stack at 0,
 * __gr_top at 8, __vr_top at 16, __gr_offs at 24 and __vr_offs at 28;
 * an offset is negative while registers are left, and counts up to 0
 * from the top of its save area, 8 bytes for each general register and
 * 16 for each SIMD register.  An argument is in registers if all it
 * needs are left, else on the stack.  The elements of an HFA of floats
 * or doubles in registers are copied to slot, which is then its
 * address; a structure of more than 16 bytes is passed by the address
 * of a copy.
 */
static char *vaaddr64(char *ap, Type ty, char *slot) {
	Type base = NULL;
	int n, gp = 1, nreg = 1, byref = 0, size, align = ty->align, i;
	char *offp, *offs, *c, *next, *top, *off64, *ra, *stk, *a;
	char *chk, *reg, *mem, *done;

	if (isstruct(ty) && (n = abi_hfa(ty, &base)) > 0)
		gp = 0, nreg = n;
	else if (isstruct(ty) && ty->size > 16)
		byref = 1, align = 8;
	else if (isstruct(ty))
		nreg = (ty->size + 7)/8;
	else if (isfloat(ty))
		gp = 0;
	size = byref ? 8 : roundup(ty->size, 8);
	chk = stringf("b%d", ++nblock);
	reg = stringf("b%d", ++nblock);
	mem = stringf("b%d", ++nblock);
	done = stringf("b%d", ++nblock);
	offp = gepconst(ap, gp ? 24 : 28);
	offs = newreg();
	inst("%s = llvm.load %s : !llvm.ptr -> i32", offs, offp);
	c = newreg();
	inst("%s = arith.cmpi sge, %s, %s : i32", c, offs, iconst(0, "i32"));
	condbr(c, mem, chk);
	startblock(chk, NULL);
	if (gp && align > 8) {	/* an even pair of registers */
		char *x = newreg(), *y = newreg();
		inst("%s = arith.addi %s, %s : i32", x, offs, iconst(15, "i32"));
		inst("%s = arith.andi %s, %s : i32", y, x, iconst(-16, "i32"));
		offs = y;
	}
	next = newreg();
	inst("%s = arith.addi %s, %s : i32", next, offs,
		iconst(gp ? 8*nreg : 16*nreg, "i32"));
	inst("llvm.store %s, %s : i32, !llvm.ptr", next, offp);
	c = newreg();
	inst("%s = arith.cmpi sgt, %s, %s : i32", c, next, iconst(0, "i32"));
	condbr(c, mem, reg);
	startblock(reg, NULL);
	top = newreg();
	inst("%s = llvm.load %s : !llvm.ptr -> !llvm.ptr", top, gepconst(ap, gp ? 8 : 16));
	off64 = newreg();
	inst("%s = arith.extsi %s : i32 to i64", off64, offs);
	ra = newreg();
	inst("%s = llvm.getelementptr %s[%s] : (!llvm.ptr, i64) -> !llvm.ptr, i8",
		ra, top, off64);
	if (slot && isstruct(ty)) {	/* gather the elements */
		char *t = base->size == 4 ? "f32" : "f64";
		for (i = 0; i < nreg; i++) {
			char *src = gepconst(ra, 16*i), *dst, *v;
			dst = gepconst(slot, i*base->size);
			v = newreg();
			inst("%s = llvm.load %s : !llvm.ptr -> %s", v, src, t);
			inst("llvm.store %s, %s : %s, !llvm.ptr", v, dst, t);
		}
		ra = slot;
	}
	br(done, stringf("%s : !llvm.ptr", ra));
	startblock(mem, NULL);
	stk = newreg();
	inst("%s = llvm.load %s : !llvm.ptr -> !llvm.ptr", stk, ap);
	if (align > 8) {	/* round up to 16 */
		char *x = newreg(), *y = newreg(), *z = newreg(), *w = newreg();
		inst("%s = llvm.ptrtoint %s : !llvm.ptr to i64", x, stk);
		inst("%s = arith.addi %s, %s : i64", y, x, iconst(15, "i64"));
		inst("%s = arith.andi %s, %s : i64", z, y, iconst(-16, "i64"));
		inst("%s = llvm.inttoptr %s : i64 to !llvm.ptr", w, z);
		stk = w;
	}
	inst("llvm.store %s, %s : !llvm.ptr, !llvm.ptr", gepconst(stk, size), ap);
	br(done, stringf("%s : !llvm.ptr", stk));
	a = newreg();
	startblock(done, stringf("%s: !llvm.ptr", a));
	if (byref) {
		char *p = newreg();
		inst("%s = llvm.load %s : !llvm.ptr -> !llvm.ptr", p, a);
		a = p;
	}
	return a;
}

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

/*
 * emitbuiltin - emit call p to stdarg builtin b, return its value.  A
 * va_arg whose value is not in the argument area as it is gets a slot
 * in the prologue: a float arrives as a double, and see vaaddr.
 */
static char *emitbuiltin(Node p, int b) {
	char *ap, *v, *d, *slot = NULL;
	Type ty;

	if (args == NULL || args->link != NULL) {
		error("`%s' takes the va_list as its only argument\n",
			p->kids[0]->syms[0]->name);
		args = NULL;
		argtail = &args;
		return b == VAARG ? ptrconst(NULL) : "";
	}
	ap = args->val;
	args = NULL;
	argtail = &args;
	switch (b) {
	case VASTART:
		if (!variadic(cfunc->type))
			error("`va_start' used in a function with fixed arguments\n");
		inst("llvm.intr.vastart %s : !llvm.ptr", ap);
		return "";
	case VAEND:
		inst("llvm.intr.vaend %s : !llvm.ptr", ap);
		return "";
	}
	assert(b == VAARG);
	ty = unqual(freturn(p->syms[0]->type)->type);
	if ((*tg->abi->needsslot)(ty)) {
		slot = stringf("%%va%d", ++nlocal);
		pinst("%s = llvm.alloca %s x %s {alignment = %d : i64} : (i32) -> !llvm.ptr",
			slot, iconst(1, "i32"), typestr(ty), ty->align);
	}
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
	if (slot && !isstruct(ty)) {	/* a float, which came as a double */
		d = newreg();
		inst("%s = llvm.load %s : !llvm.ptr -> f64", d, v);
		v = newreg();
		inst("%s = arith.truncf %s : f64 to f32", v, d);
		inst("llvm.store %s, %s : f32, !llvm.ptr", v, slot);
		v = slot;
	}
	return v;
}

/*
 * emitcall - emit call p, return its value: a func.call if it is a
 * direct call of a func.func whose type matches the call's, else an
 * llvm.call through a pointer, vararg if the call's type has no
 * prototype or is variadic.  The ABI attributes go on the call too.  A
 * char or short result is extended: its upper bits are undefined.
 * Structures are passed and returned as sigparams says: the front end
 * passes the address of a structure result first.
 */
static char *emitcall(Node p) {
	Type fty = p->syms[0]->type, rty = freturn(fty);
	Node k = p->kids[0];
	Symbol f = specific(k->op) == ADDRG+P ? k->syms[0] : NULL;
	struct arg *a, *b, *list = NULL, **tail = &list, *res = NULL;
	struct regs r;
	char *rt = optype(p->op) == V ? NULL : optypestr(p->op);
	struct part rpart, *part;
	char *plain = "", *typed = "", *attrs = "", *fixed = "", *operands = "";
	char *v = NULL, *w;
	int i, j, n, narrow = 0, hasattr = 0, nres = 0;
	int nfixed = -1;	/* -1: all arguments are fixed */

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
		assert(args && args->type && strcmp(args->type, "!llvm.ptr") == 0);
		res = args;
		if ((nres = (*tg->abi->ret)(rty, nfixed >= 0, &rpart)) > 0) {
			args = args->link;
			rt = parttype(&rpart);
		} else
			res->attr = sretstr(rty);
	}
	if (nfixed >= 0 && nres == 0 && isstruct(rty))
		nfixed++;	/* the result address is fixed: sret is not allowed in ... */
	if (rt && (isint(rty) || isenum(rty)) && rty->size < opsize(p->op)) {
		rt = typestr(rty);
		narrow = 1;
	}
	/* pass structures in registers, in memory or by address */
	(*tg->abi->init)(&r);
	r.vfunc = nfixed >= 0;
	for (i = 0, a = args; a; i++, a = a->link) {
		a->fixed = nfixed < 0 || i < nfixed;
		r.va = !a->fixed;
		n = 0;
		if (a->sty == NULL) {
			scalarreg(a->type, &r);
			if (extattr(a->type))
				a->attr = extattr(a->type);
		}
		else if ((n = (*tg->abi->arg)(a->sty, &r, &part)) == ABYVAL) {
			a->type = "!llvm.ptr";
			a->attr = byvalstr(a->sty);
		} else if (n == ABYREF) {	/* the address of a copy */
			char *copy = stringf("%%cp%d", ++nlocal), *one, *size, *f;
			one = iconst(1, "i32");
			pinst("%s = llvm.alloca %s x %s {alignment = %d : i64} : (i32) -> !llvm.ptr",
				copy, one, typestr(a->sty), a->sty->align);
			size = iconst(a->sty->size, "i64");
			f = boolconst(0);
			inst("llvm.call_intrinsic \"llvm.memcpy.p0.p0.i64\"(%s, %s, %s, %s) : "
				"(!llvm.ptr {llvm.align = %d : i64}, !llvm.ptr {llvm.align = %d : i64}, i64, i1) -> ()",
				copy, a->val, size, f, a->sty->align, a->sty->align);
			a->type = "!llvm.ptr";
			a->val = copy;
		}
		if (n <= 0) {
			*tail = a;
			tail = &a->link;
			continue;
		}
		for (j = 0; j < n; j++) {
			NEW0(b, FUNC);
			b->type = parttype(&part[j]);
			b->attr = partattr(&part[j]);
			b->fixed = a->fixed;
			b->val = loadpart(&part[j], a->val, a->sty->align);
			*tail = b;
			tail = &b->link;
		}
	}
	*tail = NULL;
	args = NULL;
	argtail = &args;
	for (i = 0, a = list; a; i++, a = a->link) {
		plain = cat(plain, i ? ", " : "", a->type, NULL);
		typed = cat(typed, i ? ", " : "", a->type, a->attr ? " " : "",
			a->attr ? a->attr : "", NULL);
		attrs = cat(attrs, i ? ", " : "", a->attr ? a->attr : "{}", NULL);
		operands = cat(operands, i ? ", " : "", a->val, NULL);
		if (a->fixed)
			fixed = cat(fixed, i ? ", " : "", a->type, NULL);
		if (a->attr)
			hasattr = 1;
	}
	if (rt)
		v = newreg();
	if (f && isfunc(f->type) && gaddr(f) == NULL && nfixed < 0
	&& fnref(f) == FUNCFUNC && strcmp(fninfo(f)->type, fntypestr(plain, rt)) == 0)
		inst("%s", cat(v ? v : "", v ? " = " : "", "func.call @\"", gname(f), "\"(",
			operands, ")", hasattr ? cat(" {arg_attrs = [", attrs, "]}", NULL) : "",
			" : (", plain, ") -> ", rt ? rt : "()", NULL));
	else
		inst("%s", cat(v ? v : "", v ? " = " : "", "llvm.call ", nodeval(k),
			"(", operands, ")", nfixed < 0 ? "" : cat(" vararg(!llvm.func<",
			rt ? rt : "void", " (", fixed, *fixed ? ", " : "", "...)>)", NULL),
			" : !llvm.ptr, (", typed, ") -> ", rt ? rt : "()", NULL));
	if (nres > 0) {	/* store the registers at the result address */
		storepart(&rpart, v, res->val, rty->align, 0);
		return "";
	}
	if (v == NULL)
		return "";
	if (narrow) {
		w = newreg();
		inst("%s = arith.%s %s : %s to %s", w, isunsigned(rty) ? "extui" : "extsi",
			v, rt, optypestr(p->op));
		v = w;
	}
	return v;
}

static char *nodeval(Node p) {
	char *v = lookupval(p), *a, *b;
	int from;
	struct gaddr *g;
	Symbol q;

	if (v)
		return v;
	switch (generic(p->op)) {
	case CNST:
		v = cnstval(p->op, p->syms[0]->u.c.v);
		break;
	case ADDRG:
		q = p->syms[0];
		if ((g = gaddr(q)) != NULL)
			v = gaddrval(g->p, g->n);
		else
			v = gaddrval(q, 0);
		break;
	case ADDRF: case ADDRL:
		v = gname(p->syms[0]);
		break;
	case INDIR:
		if (optype(p->op) == B) {	/* a structure is its address */
			v = nodeval(p->kids[0]);
			break;
		}
		a = nodeval(p->kids[0]);
		v = newreg();
		inst("%s = llvm.load %s%s : !llvm.ptr -> %s", v,
			p->volatil ? "volatile " : "", a, optypestr(p->op));
		break;
	case ADD: case SUB:
		if (optype(p->op) == P) {
			v = ptrarith(p);
			break;
		}
		/* fall through */
	case MUL: case DIV:
		if (optype(p->op) == F) {
			a = nodeval(p->kids[0]);
			b = nodeval(p->kids[1]);
			v = newreg();
			inst("%s = arith.%s %s, %s : %s", v, generic(p->op) == ADD ? "addf"
				: generic(p->op) == SUB ? "subf" : generic(p->op) == MUL ? "mulf"
				: "divf", a, b, optypestr(p->op));
			break;
		}
		/* fall through */
	case MOD: case BAND: case BOR: case BXOR:
		assert(optype(p->op) == I || optype(p->op) == U);
		a = opnd(p->kids[0], p->op);
		b = opnd(p->kids[1], p->op);
		v = newreg();
		inst("%s = arith.%s %s, %s : %s", v, intop(p->op), a, b,
			optypestr(p->op));
		break;
	case LSH: case RSH:
		a = opnd(p->kids[0], p->op);
		b = shiftcount(p);
		v = newreg();
		inst("%s = arith.%s %s, %s : %s", v, intop(p->op), a, b,
			optypestr(p->op));
		break;
	case CVF:
		/* rounding attributes would make constrained intrinsics */
		a = nodeval(p->kids[0]);
		from = opsize(p->kids[0]->op);
		assert(from == p->syms[0]->u.c.v.i);
		if (optype(p->op) == F && from == opsize(p->op))
			v = a;
		else {
			v = newreg();
			inst("%s = arith.%s %s : %s to %s", v, optype(p->op) == I ? "fptosi"
				: from < opsize(p->op) ? "extf" : "truncf", a,
				optypestr(p->kids[0]->op), optypestr(p->op));
		}
		break;
	case CVI: case CVU: case CVP:
		if (optype(p->op) == F) {	/* unsigned goes by way of signed */
			assert(generic(p->op) == CVI);
			a = nodeval(p->kids[0]);
			v = newreg();
			inst("%s = arith.sitofp %s : %s to %s", v, a,
				optypestr(p->kids[0]->op), optypestr(p->op));
			break;
		}
		a = nodeval(p->kids[0]);
		from = opsize(p->kids[0]->op);
		assert(from == p->syms[0]->u.c.v.i);
		if (generic(p->op) == CVP || optype(p->op) == P) {
			v = newreg();
			inst("%s = llvm.%s %s : %s to %s", v,
				optype(p->op) == P ? "inttoptr" : "ptrtoint", a,
				optypestr(p->kids[0]->op), optypestr(p->op));
		} else if (from == opsize(p->op))
			v = a;
		else {
			v = newreg();
			inst("%s = arith.%s %s : %s to %s", v, from > opsize(p->op)
				? "trunci" : generic(p->op) == CVI ? "extsi" : "extui", a,
				optypestr(p->kids[0]->op), optypestr(p->op));
		}
		break;
	case CALL:
		v = emitcall(p);
		break;
	case NEG:
		a = nodeval(p->kids[0]);
		v = newreg();
		if (optype(p->op) == F) {
			inst("%s = arith.negf %s : %s", v, a, optypestr(p->op));
			break;
		}
		inst("%s = arith.subi %s, %s : %s", v, iconst(0, optypestr(p->op)), a,
			optypestr(p->op));
		break;
	case BCOM:
		a = nodeval(p->kids[0]);
		v = newreg();
		inst("%s = arith.xori %s, %s : %s", v, a, iconst(-1, optypestr(p->op)),
			optypestr(p->op));
		break;
	default:
		return unsupported(p);
	}
	setval(p, v);
	return v;
}

/* cmppred - the predicate of integer comparison op */
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

/* fcmppred - the predicate of floating comparison p: dag.c marks those
   that it reversed to jump if false with syms[1], and they must be true
   if unordered */
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
 * emitswitch - emit the indirect jump p through a switch table as a
 * cf.switch.  The front end makes one only for a switch: the code list
 * entry right after the jump's has the variable switched on, which is
 * read again, and the cases.  Values missing from the table go to the
 * default, and those out of its range never reach the jump.  The table
 * itself is not emitted: global() skips it.
 */
static void emitswitch(Node p) {
	Code cp;
	Symbol sym, lab;
	char *v, *ty;
	int i;

	for (cp = codehead.next; cp; cp = cp->next)
		if (cp->kind == Gen && cp->u.forest == emitting)
			break;
	if (cp == NULL || (cp = cp->next) == NULL || cp->kind != Switch) {
		unsupported(p);
		return;
	}
	sym = cp->u.swtch.sym;
	ty = typestr(sym->type);
	v = newreg();
	inst("%s = llvm.load %s : !llvm.ptr -> %s", v, sym->scope == GLOBAL
		|| sym->sclass == STATIC || sym->sclass == EXTERN ? gaddrval(sym, 0)
		: gname(sym), ty);
	lab = equated(cp->u.swtch.deflab);
	inst("cf.switch %s : %s, [", v, ty);
	fprint(body, "    default: ^%s", gname(lab));
	edge(gname(lab));
	for (i = 0; i < cp->u.swtch.size; i++) {
		lab = equated(cp->u.swtch.labels[i]);
		fprint(body, ",\n    %D: ^%s", sext(cp->u.swtch.values[i],
			sym->type->size), gname(lab));
		edge(gname(lab));
	}
	fprint(body, "\n  ]\n");
	terminated = 1;
	swtable = cp->u.swtch.table;
}

static void emitnode(Node p) {
	char *a, *b, *c, *v, *fall;
	struct arg *arg;

	switch (generic(p->op)) {
	case EQ: case NE: case LT: case LE: case GT: case GE:
		/* jump to syms[0] if true; else fall into a new block */
		/* pointers are compared as integers: the front end adds CVPU */
		a = opnd(p->kids[0], p->op);
		b = opnd(p->kids[1], p->op);
		c = newreg();
		fall = stringf("b%d", ++nblock);
		if (optype(p->op) == F)
			inst("%s = arith.cmpf %s, %s, %s : %s", c, fcmppred(p), a, b,
				optypestr(p->op));
		else
			inst("%s = arith.cmpi %s, %s, %s : %s", c, cmppred(p->op), a, b,
				optypestr(p->op));
		condbr(c, gname(p->syms[0]), fall);
		startblock(fall, NULL);
		break;
	case LABEL:
		if (!terminated)
			br(gname(p->syms[0]), NULL);
		startblock(gname(p->syms[0]), NULL);
		break;
	case JUMP:
		if (specific(p->kids[0]->op) == ADDRG+P)
			br(gname(p->kids[0]->syms[0]), NULL);
		else
			emitswitch(p);
		break;
	case ASGN:
		if (optype(p->op) == B) {	/* kids[1] is INDIRB */
			/* llvm.intr.memcpy has no alignments */
			int align = p->syms[1]->u.c.v.i;
			assert(generic(p->kids[1]->op) == INDIR);
			a = nodeval(p->kids[0]);
			b = nodeval(p->kids[1]);
			/* one at a time: the pool must not depend on the order of
			   evaluation of arguments */
			c = iconst(p->syms[0]->u.c.v.i, "i64");
			v = boolconst(p->volatil || p->kids[1]->volatil);
			inst("llvm.call_intrinsic \"llvm.memcpy.p0.p0.i64\"(%s, %s, %s, %s) : "
				"(!llvm.ptr {llvm.align = %d : i64}, !llvm.ptr {llvm.align = %d : i64}, i64, i1) -> ()",
				a, b, c, v, align, align);
			break;
		}
		a = nodeval(p->kids[0]);
		v = opnd(p->kids[1], p->op);
		inst("llvm.store %s%s, %s : %s, !llvm.ptr", p->volatil ? "volatile " : "",
			v, a, optypestr(p->op));
		break;
	case RET:
		if (optype(p->op) != V)
			inst("llvm.store %s, %%retval : %s, !llvm.ptr",
				opnd(p->kids[0], p->op), optypestr(p->op));
		break;
	case ARG:
		NEW0(arg, FUNC);
		if (optype(p->op) == B) {	/* dag.c types its size */
			arg->sty = p->syms[0]->type;
			assert(isstruct(arg->sty));
			arg->val = nodeval(p->kids[0]);
		} else {
			arg->type = optypestr(p->op);
			arg->val = opnd(p->kids[0], p->op);
		}
		*argtail = arg;
		argtail = &arg->link;
		break;
	case CALL: case INDIR:
		(void)nodeval(p);
		break;
	default:
		unsupported(p);
	}
}
/* address - initialize q for addressing expression p+n: for a global p,
   q stands for the variable or function that p is or stands for plus
   an offset; for a local p, q is a value that the prologue computes
   after gencode, when every local has its alloca */
static void I(address)(Symbol q, Symbol p, long n) {
	struct gaddr *a;
	struct laddr *l;
	unsigned h;

	if (p->scope != GLOBAL && p->sclass != STATIC && p->sclass != EXTERN) {
		NEW(l, FUNC);
		l->q = q;
		l->p = p;
		l->n = n;
		l->link = NULL;
		*ladtail = l;
		ladtail = &l->link;
		q->x.name = stringf("%%la%d", ++nlocal);
		return;
	}
	if ((a = gaddr(p)) != NULL) {
		n += a->n;
		p = a->p;
	}
	NEW(a, PERM);
	a->q = q;
	a->p = p;
	a->n = n;
	h = ((unsigned long)q>>3)&(NELEMS(gaddrs)-1);
	a->link = gaddrs[h];
	gaddrs[h] = a;
}

static void I(blockbeg)(Env *e) {}
static void I(blockend)(Env *e) {}

/* defaddress - initialize an address */
static void I(defaddress)(Symbol p) {
	struct gaddr *a = gaddr(p);
	Item it;
	long n = 0;

	if (skipping)
		return;
	if (a) {
		n = a->n;
		p = a->p;
	}
	if (p->scope == LABELS) {	/* a switch table is not emitted */
		error("mlir back end: unsupported address of label `%s' in global data\n",
			p->name);
		return;
	}
	it = newitem(Kaddr);
	it->sym = p;
	it->off = n;
}

/* defconst - define a constant */
static void I(defconst)(int suffix, int size, Value v) {
	Item it;

	if (skipping)
		return;
	it = newitem(Kconst);
	assert(suffix == I || suffix == U || suffix == P || suffix == F);
	it->suffix = suffix;
	it->size = size;
	it->v = v;
}

/* defstring - emit a string constant */
static void I(defstring)(int len, char *s) {
	Item it;

	if (skipping)
		return;
	it = newitem(Kstring);
	it->s = memcpy(allocate(len, PERM), s, len);
	it->n = len;
}

/* defsymbol - define a symbol: initialize p->x.name.  Global names are
   always printed quoted, as @".L5": unquoted, they must begin with a
   letter or _ */
static void I(defsymbol)(Symbol p) {
	if (p->x.name)
		return;
	if (p->scope == LABELS)
		p->x.name = stringf("L%s", p->name);
	else if (p->scope == CONSTANTS)
		p->x.name = p->name;
	else if (p->sclass == STATIC && p->generated)
		p->x.name = stringf(".L%s", p->name);
	else if (p->sclass == STATIC && p->scope >= LOCAL)
		p->x.name = stringf("%s.%d", p->name, genlabel(1));
	else if (p->scope == GLOBAL || p->sclass == EXTERN)
		p->x.name = p->name;
	/* parameters and locals are named by function and local */
}

static void I(emit)(Node p) {
	for (emitting = p; p; p = p->link)
		emitnode(p);
}
static void I(export)(Symbol p) {}

/*
 * function - generate code for function f: a func.func, or an llvm.func
 * if it is variadic or -llvmfunc is given.  Its parameters have the
 * types of caller[]: the front end promotes the integer parameters of
 * prototyped definitions.  A structure result comes as an address in
 * caller[0]: one in registers goes in %retval instead.
 */
static void I(function)(Symbol f, Symbol caller[], Symbol callee[], int ncalls) {
	Type rty = freturn(f->type), *ty;
	int i, j, n, nres = 0, base = isstruct(rty);
	struct fninfo *fi;
	struct laddr *a;
	struct regs r;
	struct part rpart, *part;
	char *v, *w, *plain, *typed, *named;

	flushglobal();
	rettype = rettypestr(f->type);
	restype = restypestr(f->type);
	if (isstruct(rty))
		nres = (*tg->abi->ret)(rty, variadic(f->type), &rpart);
	for (n = 0; caller[n]; n++)
		;
	ty = newarray(n + 1, sizeof *ty, FUNC);
	for (i = 0; i < n; i++)
		ty[i] = caller[i]->type;
	sigparams(f->type, ty + base, n - base, base, &plain, &typed, &named);
	fi = fninfo(f);
	if (fi->kind == UNDECIDED) {
		fi->kind = variadic(f->type) || llvmfunc ? LLVMFUNC : FUNCFUNC;
		fi->params = plain;
		fi->typed = typed;
		fi->result = restype;
		fi->type = fntypestr(plain, restype);
	} else if (fi->kind == FUNCFUNC
	&& strcmp(fi->type, fntypestr(plain, restype)) != 0) {
		error("mlir back end: `%s' is defined as %s but was used as %s\n",
			f->name, fntypestr(plain, restype), fi->type);
		return;
	}
	fkind = fi->kind;
	memset(nodevals, 0, sizeof nodevals);
	memset(poolhash, 0, sizeof poolhash);
	memset(bhash, 0, sizeof bhash);
	laddrs = NULL;
	ladtail = &laddrs;
	args = NULL;
	argtail = &args;
	nreg = nblock = ncst = 0;
	rewind(pool);
	rewind(prologue);
	rewind(body);
	NEW0(bfirst, FUNC);
	bfirst->defined = 1;
	bfirst->start = 0;
	bdtail = &bfirst->dlink;
	cur = bfirst;
	terminated = 0;

	fprint(module, "%s ", fkind == FUNCFUNC ? "func.func" : "llvm.func");
	if (fkind == LLVMFUNC && f->sclass == STATIC)
		fprint(module, "internal ");
	fprint(module, "@\"%s\"(%s", gname(f), named);
	if (variadic(f->type))
		fprint(module, "%s...", *named ? ", " : "");
	fprint(module, ")");
	if (restype)
		fprint(module, " -> %s", resstr(restype, f->type));
	if (fkind == FUNCFUNC && f->sclass == STATIC)
		fprint(module, " attributes {llvm.linkage = #llvm.linkage<internal>}");
	fprint(module, " {\n");

	if (rettype)
		pinst("%%retval = llvm.alloca %s x %s : (i32) -> !llvm.ptr",
			iconst(1, "i32"), rettype);
	(*tg->abi->init)(&r);
	r.vfunc = variadic(f->type);
	for (i = 0; caller[i]; i++) {
		Symbol p = caller[i], q = callee[i];
		if (i == 0 && nres > 0) {	/* the result goes in %retval */
			p->x.name = localname(p);
			emitalloca(p);
			pinst("llvm.store %%retval, %s : !llvm.ptr, !llvm.ptr", p->x.name);
		} else if (!isstruct(p->type)) {
			p->x.name = localname(p);
			emitalloca(p);
			pinst("llvm.store %%a%d, %s : %s, !llvm.ptr", i, p->x.name,
				typestr(p->type));
			scalarreg(typestr(p->type), &r);
		} else if ((n = (*tg->abi->arg)(p->type, &r, &part)) <= 0)
			p->x.name = stringf("%%a%d", i);	/* byval or a copy */
		else {	/* in registers: store them */
			p->x.name = localname(p);
			emitalloca(p);
			for (j = 0; j < n; j++)
				storepart(&part[j], stringf("%%a%d.%d", i, j), p->x.name,
					p->type->align, 1);
		}
		if (q->sclass == p->sclass && q->type == p->type)
			q->x.name = p->x.name;
		else {	/* the front end assigns caller to callee */
			q->x.name = localname(q);
			emitalloca(q);
		}
	}
	gencode(caller, callee);
	for (a = laddrs; a; a = a->link)
		pinst("%s = llvm.getelementptr %s[%D] : (!llvm.ptr) -> !llvm.ptr, i8",
			a->q->x.name, gname(a->p), a->n);
	emitcode();
	v = fkind == FUNCFUNC ? "func.return" : "llvm.return";
	if (rettype == NULL)
		inst("%s", v);
	else {
		w = newreg();
		inst("%s = llvm.load %%retval : !llvm.ptr -> %s", w, rettype);
		if (strcmp(rettype, restype) != 0) {
			char *t = newreg();
			inst("%s = arith.trunci %s : %s to %s", t, w, rettype, restype);
			w = t;
		}
		inst("%s %s : %s", v, w, restype);
	}
	flushfunc();
	fprint(module, "}\n");
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
	if ((skipping = p == swtable) != 0) {
		swtable = NULL;
		return;
	}
	pending = p;
	items = NULL;
	itail = &items;
}

/* import - declare an external symbol: a variable, which may have an
   incomplete type and so no alignment yet, or a function, without
   parameters if it has no prototype; the stdarg builtins are not
   functions */
static void I(import)(Symbol p) {
	Type fty = p->type, *proto = fty->u.f.proto;
	char *plain, *typed, *named;
	int n = 0;

	if (isfunc(fty)) {
		if (strncmp(p->name, "__builtin_va_", 13) == 0)
			return;
		if (fnref(p) == FUNCFUNC) {
			struct fninfo *f = fninfo(p);
			fprint(module, "func.func private @\"%s\"(%s)", gname(p), f->typed);
			if (f->result)
				fprint(module, " -> %s", resstr(f->result, fty));
		} else {
			if (proto && !fty->u.f.oldstyle)
				for ( ; proto[n] && proto[n] != voidtype; n++)
					;
			sigparams(fty, proto, n, 0, &plain, &typed, &named);
			fprint(module, "llvm.func @\"%s\"(%s", gname(p), typed);
			if (proto == NULL || fty->u.f.oldstyle || variadic(fty))
				fprint(module, "%s...", *typed ? ", " : "");
			fprint(module, ")");
			if (restypestr(p->type))
				fprint(module, " -> %s", resstr(restypestr(p->type), p->type));
		}
		if (abi_returnstwice(p->name))
			fprint(module, " attributes {passthrough = [\"returns_twice\"]}");
		fprint(module, "\n");
		return;
	}
	fprint(module, "llvm.mlir.global external @\"%s\"() {alignment = %d : i64} : %s\n",
		gname(p), p->type->align > 0 ? p->type->align : 1,
		typestr(p->type));
}

/* local - local variable */
static void I(local)(Symbol p) {
	p->x.name = localname(p);
	emitalloca(p);
}

/* progbeg - beginning of program */
static void I(progbeg)(int argc, char *argv[]) {
	int i;

	for (tg = targets; tg->ir != IR; tg++)
		assert(tg < &targets[NELEMS(targets)-1]);
	for (i = 1; i < argc; i++)
		if (strcmp(argv[i], "-llvmfunc") == 0)
			llvmfunc = 1;
	if ((module = tmpfile()) == NULL || (pool = tmpfile()) == NULL
	|| (prologue = tmpfile()) == NULL || (body = tmpfile()) == NULL) {
		fprint(stderr, "%s: can't create a temporary file\n", argv[0]);
		exit(EXIT_FAILURE);
	}
}

/* progend - end of program: emit the type aliases and the module */
static void I(progend)(void) {
	char buf[BUFSIZE];
	size_t n;
	struct alias *a;

	flushglobal();
	for (a = aliases; a; a = a->next)
		print("%s = %s\n", a->name, a->type);
	print("module attributes {llvm.data_layout = \"%s\", "
		"llvm.target_triple = \"%s\"} {\n", tg->layout, tg->triple);
	rewind(module);
	while ((n = fread(buf, 1, sizeof buf, module)) > 0)
		fwrite(buf, 1, n, stdout);
	fclose(module);
	print("}\n");
}

static void I(segment)(int s) {}

/* space - initialize n bytes of space */
static void I(space)(int n) {
	if (!skipping)
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
