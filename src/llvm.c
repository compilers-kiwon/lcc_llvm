#include "c.h"

/*
 * LLVM back end: emits textual LLVM IR (.ll) for llvm/x86_64-linux.
 */

#define I(f) llvm_##f
static char rcsid[] = "$Id$";

static void emitescaped(int len, const char *s) {
	for ( ; len-- > 0; s++)
		if (*s >= ' ' && *s < 0177 && *s != '"' && *s != '\\')
			print("%c", *s);
		else
			print("\\%c%c", "0123456789ABCDEF"[(*s>>4)&0xF],
				"0123456789ABCDEF"[*s&0xF]);
}

static void I(address)(Symbol q, Symbol p, long n) {}
static void I(blockbeg)(Env *e) {}
static void I(blockend)(Env *e) {}
static void I(defaddress)(Symbol p) {}
static void I(defconst)(int suffix, int size, Value v) {}
static void I(defstring)(int len, char *s) {}
static void I(defsymbol)(Symbol p) {}
static void I(emit)(Node p) {}
static void I(export)(Symbol p) {}
static void I(function)(Symbol f, Symbol caller[], Symbol callee[], int ncalls) {}
static Node I(gen)(Node p) { return p; }
static void I(global)(Symbol p) {}
static void I(import)(Symbol p) {}
static void I(local)(Symbol p) {}

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

static void I(progend)(void) {}
static void I(segment)(int s) {}
static void I(space)(int n) {}
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
