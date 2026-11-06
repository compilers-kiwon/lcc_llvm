#include "c.h"
#include "sysv.h"

/*
 * MLIR back end: emits MLIR (.mlir) for mlir/x86_64-linux, which
 * mlir-opt and mlir-translate lower to LLVM IR.  Types and the dags
 * from the front end are those of llvm/x86_64-linux.
 *
 * The operations of the module are written to a temporary file, which
 * progend copies into the module op.  What must come before the module
 * op, as type aliases, is known only at the end.
 */

#define I(f) mlir_##f
static char rcsid[] = "$Id$";

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
struct part {
	int kind;
	char *type;		/* MLIR type */
	Item it;		/* first item, unless a group */
	int n;			/* bytes of a run, parts of a group */
	struct part *parts;	/* those of a group */
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

/* flttypestr - MLIR type of a floating type of size bytes; long double
   is the x87 80-bit type, which takes 16 bytes */
static char *flttypestr(int size) {
	return size == 4 ? "f32" : size == 8 ? "f64" : "f80";
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

/* fltbits - the bits of float or double constant d of size 4 or 8, as
   the host represents it */
static unsigned long fltbits(long double d, int size) {
	if (size == 4) {
		float f = (float)d;
		unsigned u;
		assert(sizeof u == sizeof f);
		memcpy(&u, &f, sizeof u);
		return u;
	} else {
		double x = (double)d;
		unsigned long u;
		assert(sizeof u == sizeof x);
		memcpy(&u, &x, sizeof u);
		return u;
	}
}

/* fltstr - floating constant d of size bytes as a hex literal of its
   bits, which MLIR takes exactly: 8 or 16 hex digits, or for the x87
   80 bits 4 of the sign and the exponent and 16 of the significand */
static char *fltstr(long double d, int size) {
	char buf[23];
	unsigned se;
	unsigned long hi, lo;

	buf[0] = '0', buf[1] = 'x';
	if (size <= 8) {
		hexdigits(buf, 2, 2*size, fltbits(d, size));
		buf[2 + 2*size] = 0;
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
			u = fltbits(it->v.d, it->size);
			break;
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
	for (i = 0; i < it->size; i++)
		b[i] = u>>8*i;
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
static struct part *initparts(Item items, int *np) {
	struct part *p;
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
static char *aggtype(struct part *p, int n) {
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
static struct part *group(struct part *p, int n) {
	struct part *q;
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

/* addrval - emit the address of variable p plus n, return its value */
static char *addrval(Symbol p, long n) {
	char *v = newval(), *c, *w;

	ginst("%s = llvm.mlir.addressof @\"%s\" : !llvm.ptr", v, gname(p));
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
static char *partval(struct part *p) {
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
	struct part *parts = NULL;
	char *ty, *v;
	int n, align = p->type->align;

	fprint(module, "llvm.mlir.global %s%s @\"%s\"(",
		p->sclass != STATIC ? "external" : p->generated ? "private" : "internal",
		p->u.seg == LIT ? " constant" : "", gname(p));
	if (it && it->link == NULL && it->kind == Kconst && it->suffix != P) {
		ty = it->suffix == F ? flttypestr(it->size) : stringf("i%d", 8*it->size);
		fprint(module, "%s : %s) {alignment = %d : i64} : %s\n",
			it->suffix == F ? fltstr(it->v.d, it->size)
			: intstr(it->suffix == I ? it->v.i : it->v.u, it->size),
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

/* address - initialize q for addressing expression p+n: for a global p,
   q stands for the variable or function that p is or stands for plus
   an offset */
static void I(address)(Symbol q, Symbol p, long n) {
	struct gaddr *a;
	unsigned h;

	if (p->scope != GLOBAL && p->sclass != STATIC && p->sclass != EXTERN) {
		error("mlir back end: unsupported address of local `%s' plus a constant\n",
			p->name);
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

	if (a) {
		n = a->n;
		p = a->p;
	}
	if (p->scope == LABELS || isfunc(p->type)) {
		error("mlir back end: unsupported address of %s `%s' in global data\n",
			p->scope == LABELS ? "label" : "function", p->name);
		return;
	}
	it = newitem(Kaddr);
	it->sym = p;
	it->off = n;
}

/* defconst - define a constant */
static void I(defconst)(int suffix, int size, Value v) {
	Item it = newitem(Kconst);

	assert(suffix == I || suffix == U || suffix == P || suffix == F);
	it->suffix = suffix;
	it->size = size;
	it->v = v;
}

/* defstring - emit a string constant */
static void I(defstring)(int len, char *s) {
	Item it = newitem(Kstring);

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

static void I(emit)(Node p) {}
static void I(export)(Symbol p) {}

/* function - generate code for a function */
static void I(function)(Symbol f, Symbol caller[], Symbol callee[], int ncalls) {
	error("mlir back end: unsupported function definition `%s'\n", f->name);
}

static Node I(gen)(Node p) { return p; }

/* global - announce a global */
static void I(global)(Symbol p) {
	flushglobal();
	pending = p;
	items = NULL;
	itail = &items;
}

/* import - declare an external variable, which may have an incomplete
   type and so no alignment yet; functions need no declarations while
   nothing can call them */
static void I(import)(Symbol p) {
	if (isfunc(p->type))
		return;
	fprint(module, "llvm.mlir.global external @\"%s\"() {alignment = %d : i64} : %s\n",
		gname(p), p->type->align > 0 ? p->type->align : 1,
		typestr(p->type));
}

/* local - local variable */
static void I(local)(Symbol p) {
	error("mlir back end: unsupported local `%s'\n", p->name);
}

/* progbeg - beginning of program */
static void I(progbeg)(int argc, char *argv[]) {
	int i;

	for (i = 1; i < argc; i++)
		if (strcmp(argv[i], "-llvmfunc") == 0)
			llvmfunc = 1;
	if ((module = tmpfile()) == NULL) {
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
	print("module attributes {llvm.data_layout = \"e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128\", "
		"llvm.target_triple = \"x86_64-pc-linux-gnu\"} {\n");
	rewind(module);
	while ((n = fread(buf, 1, sizeof buf, module)) > 0)
		fwrite(buf, 1, n, stdout);
	fclose(module);
	print("}\n");
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

Interface mlirIR = {
	1, 1, 0,	/* char */
	2, 2, 0,	/* short */
	4, 4, 0,	/* int */
	8, 8, 0,	/* long */
	8, 8, 0,	/* long long */
	4, 4, 0,	/* float */
	8, 8, 0,	/* double */
	16, 16, 0,	/* long double: x87 80 bits */
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
