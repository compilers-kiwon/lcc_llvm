#include "c.h"
#include "abi.h"

static char rcsid[] = "$Id$";

/* What the calling conventions in sysv.c and aapcs64.c share. */

char *abi_memtype(struct elem *e) {
	if (e->type[0] == 'i' && atoi(e->type + 1) > 8*e->size)
		return stringf("i%d", 8*e->size);
	return e->type;
}

struct part *abi_parts(int n) {
	struct part *p = newarray(n, sizeof *p, FUNC);

	memset(p, 0, n*sizeof *p);
	return p;
}

void abi_elems(struct part *p, Type ty, int off, int n, int size, char *t) {
	int i;

	p->n = n;
	p->e = newarray(n, sizeof *p->e, FUNC);
	for (i = 0; i < n; i++) {
		p->e[i].off = off + i*size;
		p->e[i].size = ty->size - p->e[i].off < size ? ty->size - p->e[i].off : size;
		p->e[i].type = t;
	}
	p->type = n == 1 ? t : stringf("[%d x %s]", n, t);
}

/*
 * abi_varalign - the alignment of the variable p: that of its type, or
 * more for a large array (SysV x86-64 aligns those of at least 16
 * bytes to 16, which code compiled by others relies on).  Generated
 * ones, as string literals, have just that of their type.
 */
int abi_varalign(struct abi *a, Symbol p) {
	int align = p->type->align;

	if (a->arrayalign > align && isarray(p->type)
	&& p->type->size >= a->arrayalign && !p->generated)
		align = a->arrayalign;
	return align;
}

/*
 * abi_libvariadic - the number of fixed parameters of the C library's
 * variadic function named name, or -1.  A call to one without a
 * prototype passes the rest as variadic arguments, as clang does,
 * though C89 leaves it undefined.
 */
int abi_libvariadic(char *name) {
	static struct { char *name; int nfixed; } fns[] = {
		{ "printf", 1 }, { "fprintf", 2 }, { "sprintf", 2 },
		{ "scanf", 1 }, { "fscanf", 2 }, { "sscanf", 2 }, { NULL, 0 } };
	int i;

	for (i = 0; fns[i].name; i++)
		if (strcmp(name, fns[i].name) == 0)
			return fns[i].nfixed;
	return -1;
}

/*
 * abi_returnstwice - whether the function named name can return twice,
 * as setjmp does; LLVM must know, since locals may change between the
 * returns.  The names are those that clang knows.
 */
int abi_returnstwice(char *name) {
	static char *names[] = { "setjmp", "_setjmp", "sigsetjmp",
		"__sigsetjmp", "savectx", "vfork", "getcontext", NULL };
	int i;

	for (i = 0; names[i]; i++)
		if (strcmp(name, names[i]) == 0)
			return 1;
	return 0;
}

/* abi_fp128bits - the bits of q in w[0..3], 32 each, the most
   significant first */
void abi_fp128bits(Quad q, unsigned long w[4]) {
	int i;

	for (i = 0; i < 4; i++)
		w[i] = (unsigned long)q.h[2*i]<<16 | q.h[2*i+1];
}
