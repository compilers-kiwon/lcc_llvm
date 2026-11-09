/*
 * The targets of the llvm and mlir back ends.  For each,
 *
 *	xx(id, triple, layout, ldtype, mlirld, lsize, ldsize, psize, uchar, quad, abi)
 *
 * gives the suffix of its Interfaces' names, the LLVM target triple and
 * data layout, the LLVM and MLIR types of long double, the sizes, which
 * are also the alignments, of long, long double and pointers, whether
 * plain char is unsigned, whether long double is IEEE quad, which the
 * front end then keeps as Quad, and the calling convention (abi.h).  The other types are the same on all
 * of them: char 1, short 2, int 4, long long 8, float 4 and double 8
 * bytes, aligned to their sizes.
 */
#define TARGETS \
xx(x86_64_linux, "x86_64-pc-linux-gnu", \
	"e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128", \
	"x86_fp80", "f80", 8, 16, 8, 0, 0, &sysvabi) \
xx(aarch64_linux, "aarch64-unknown-linux-gnu", \
	"e-m:e-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128", \
	"fp128", "f128", 8, 16, 8, 1, 1, &aapcs64abi) \
xx(riscv64_linux, "riscv64-unknown-linux-gnu", \
	"e-m:e-p:64:64-i64:64-i128:128-n32:64-S128", \
	"fp128", "f128", 8, 16, 8, 1, 1, &riscv64abi)

/* a target, as a back end looks it up from IR */
struct target {
	Interface *ir;
	char *triple, *layout;
	char *ldtype, *mlirld;
	struct abi *abi;
};

/*
 * TARGETIR - the Interface of target id in the back end whose functions
 * are named I(f).  The llvm and mlir back ends take the dags in the
 * same form.
 */
#define TARGETIR(id, lsize, ldsize, psize, uchar, quad) \
Interface I(id##IR) = { \
	1, 1, 0,		/* char */ \
	2, 2, 0,		/* short */ \
	4, 4, 0,		/* int */ \
	lsize, lsize, 0,	/* long */ \
	8, 8, 0,		/* long long */ \
	4, 4, 0,		/* float */ \
	8, 8, 0,		/* double */ \
	ldsize, ldsize, 0,	/* long double */ \
	psize, psize, 0,	/* T* */ \
	0, 1, 0,		/* struct */ \
	1,			/* little_endian */ \
	0,			/* mulops_calls */ \
	0,			/* wants_callb */ \
	1,			/* wants_argb */ \
	1,			/* left_to_right */ \
	1,			/* wants_dag */ \
	uchar,			/* unsigned_char */ \
	I(address), \
	I(blockbeg), \
	I(blockend), \
	I(defaddress), \
	I(defconst), \
	I(defstring), \
	I(defsymbol), \
	I(emit), \
	I(export), \
	I(function), \
	I(gen), \
	I(global), \
	I(import), \
	I(local), \
	I(progbeg), \
	I(progend), \
	I(segment), \
	I(space), \
	I(stabblock), \
	I(stabend), \
	I(stabfend), \
	I(stabinit), \
	I(stabline), \
	I(stabsym), \
	I(stabtype), \
	{0},			/* x */ \
	quad			/* quadld */ \
}
