/*
 * The calling conventions of the llvm and mlir back ends, and data
 * formats they share; see sysv.c, aapcs64.c, riscv.c and arm.c.  Types are named by
 * their LLVM names: "float", "double", "fp128", "x86_fp80", "iN", "ptr".
 */

/* free argument registers while arguments are assigned to them */
struct regs {
	int gp, fp;
	int va;		/* the next arguments are variadic ones */
	int vfunc;	/* the function is variadic */
};

/*
 * A structure passed or returned in registers is cut into parts, each
 * one LLVM argument or the LLVM result.  A part has one or more
 * elements; with more, its type is an array or a literal structure of
 * them.  An element holds size bytes of the structure at off; if its
 * type is an integer wider than that, only size bytes are loaded and
 * stored, so that no part reaches beyond the structure.
 */
struct elem {
	int off, size;
	char *type;
};
struct part {
	char *type;	/* the LLVM type of the argument or result */
	int alignstack;	/* its alignment on the stack, if not 0 */
	int n;		/* elements */
	struct elem *e;
};

/* what abi.arg returns for a structure that is not in registers */
enum { ABYVAL = 0,	/* on the stack: a pointer with byval */
	ABYREF = -1 };	/* the address of a copy made by the caller */

struct abi {
	/* init - set r to the argument registers */
	void (*init)(struct regs *r);
	/* scalar - take a register for a scalar argument of LLVM type t */
	void (*scalar)(char *t, struct regs *r);
	/* arg - set *part to the parts of the structure argument ty, taking
	   registers from r, and return how many, or ABYVAL or ABYREF */
	int (*arg)(Type ty, struct regs *r, struct part **part);
	/* ret - set *part to the structure result ty of a function, variadic
	   if vfunc, and return 1, or return 0 if it is returned at an
	   address the caller passes (sret) */
	int (*ret)(Type ty, int vfunc, struct part *part);
	/* needsslot - whether a va_arg of type ty needs a place for its value */
	int (*needsslot)(Type ty);
	/* extattr - the extension attribute of a scalar argument or result
	   of LLVM type t, or NULL; NULL if there are none */
	char *(*extattr)(char *t);
	/* krfixed - a call without a prototype passes its arguments as
	   fixed ones, not as variadic ones */
	int krfixed;
	/* arrayalign - the least alignment of an array variable of at least
	   so many bytes, or 0 */
	int arrayalign;
	/* narrowext - a char or short result is extended to int by the callee */
	int narrowext;
	/* byvalalign - the least alignment of a structure argument on the stack */
	int byvalalign;
};

extern struct abi sysvabi, aapcs64abi, riscv64abi, armabi, armsoftabi;

/* abi_memtype - the LLVM type element e is loaded and stored as */
extern char *abi_memtype(struct elem *e);
/* abi_parts - n parts, without their types and elements */
extern struct part *abi_parts(int n);
/* abi_elems - give part p n elements of type t, size bytes each from off
   (the last only what is left of the structure ty), and the type t if n
   is 1, else [n x t] */
extern void abi_elems(struct part *p, Type ty, int off, int n, int size, char *t);

/* abi_isagg - whether the type of part p is an array or a structure */
#define abi_isagg(p) ((p)->type[0] == '[' || (p)->type[0] == '{')

extern int abi_returnstwice(char *);
extern int abi_libvariadic(char *);
extern int abi_varalign(struct abi *, Symbol);
extern int abi_hfa(Type, Type *);

extern void abi_fp128bits(Quad, unsigned long [4]);

/* SysV x86-64 parts for va_arg, and the x87 80-bit format */
extern int sysv_structparts(Type, struct regs *, char *[]);
extern int sysv_ssepart(char *);
extern void sysv_fp80bits(long double, unsigned *, unsigned long *, unsigned long *);
