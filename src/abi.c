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

/*
 * abi_fp128bits - the IEEE quad form of d in w[0..3], 32 bits each, the
 * most significant first.  d has only the 64 significant bits of the
 * x87 long double, the host's.
 */
void abi_fp128bits(long double d, unsigned long w[4]) {
	unsigned se;
	unsigned long hi, lo;

	sysv_fp80bits(d, &se, &hi, &lo);
	hi &= 0x7FFFFFFFUL;	/* the leading 1 is implicit */
	w[0] = (unsigned long)se<<16 | hi>>15;
	w[1] = (hi&0x7FFF)<<17 | lo>>15;
	w[2] = (lo&0x7FFF)<<17;
	w[3] = 0;
}
