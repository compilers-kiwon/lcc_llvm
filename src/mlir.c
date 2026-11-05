#include "c.h"

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

static FILE *module;		/* operations of the module, until progend */
static int llvmfunc;		/* -llvmfunc: every function is an llvm.func */

/* address - initialize q for addressing expression p+n */
static void I(address)(Symbol q, Symbol p, long n) {
	error("mlir back end: unsupported address of `%s' plus a constant\n",
		p->name);
}

static void I(blockbeg)(Env *e) {}
static void I(blockend)(Env *e) {}

/* defaddress - initialize an address */
static void I(defaddress)(Symbol p) {
	error("mlir back end: unsupported address `%s' in global data\n",
		p->name);
}

/* defconst - define a constant */
static void I(defconst)(int suffix, int size, Value v) {
	error("mlir back end: unsupported constant in global data\n");
}

/* defstring - emit a string constant */
static void I(defstring)(int len, char *s) {
	error("mlir back end: unsupported string in global data\n");
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
	error("mlir back end: unsupported global `%s'\n", p->name);
}

/* import - declare an external symbol: not needed while nothing can
   refer to one */
static void I(import)(Symbol p) {}

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

/* progend - end of program: emit the module */
static void I(progend)(void) {
	char buf[BUFSIZE];
	size_t n;

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
	error("mlir back end: unsupported zero bytes in global data\n");
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
