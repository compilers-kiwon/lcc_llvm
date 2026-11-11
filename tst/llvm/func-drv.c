/*
 * Driver for func.c: includes func.c again with the functions renamed
 * c_NAME, compiled by clang, compares lcc's __func__ with clang's, and
 * checks what assert gave when it failed in lcc's code.  Exits with 0 if
 * every check passes.
 */
#include <stdio.h>
#include <string.h>

#define F(n) c_##n
#include "func.c"

extern const char *fname(void), *fnested(void), *fcallstatic(void);
extern int fsize(void), fchar(void), fsame(void);
extern int runassert(int, int, const char **, const char **, const char **, int *);
extern int clangpretty;

static int checks, bad;

static void name(char *what, const char *lcc, const char *c) {
	checks++;
	if (lcc == 0 || strncmp(c, "c_", 2) != 0 || strcmp(lcc, c + 2) != 0) {
		bad++;
		printf("%s: lcc \"%s\", clang \"%s\"\n", what, lcc ? lcc : "(null)", c);
	}
}

static void val(char *what, int lcc, int c) {
	checks++;
	if (lcc != c) {
		bad++;
		printf("%s: lcc %d, clang %d\n", what, lcc, c);
	}
}

static void str(char *what, const char *got, const char *want) {
	checks++;
	if (got == 0 || strcmp(got, want) != 0) {
		bad++;
		printf("%s: \"%s\", expected \"%s\"\n", what, got ? got : "(null)", want);
	}
}

/* one call of runassert: fails says whether assert should fail; func is
   the function's name and pretty clang's __PRETTY_FUNCTION__ for it */
static void tryassert(int which, int x, int fails, const char *expr,
	const char *func, const char *pretty) {
	const char *file = 0, *e = 0, *f = 0;
	int line = -1;

	val("assert failed", runassert(which, x, &file, &e, &f, &line), fails);
	if (fails) {
		str("assert expression", e, expr);
		str("assert function", f, clangpretty ? pretty : func);
		checks++;
		if (file == 0 || strstr(file, "func.c") == 0) {
			bad++;
			printf("assert file: \"%s\"\n", file ? file : "(null)");
		}
		val("assert line", line, 0);
	}
}

int main(void) {
	name("fname", fname(), c_fname());
	name("fnested", fnested(), c_fnested());
	name("fcallstatic", fcallstatic(), c_fcallstatic());
	val("fsize", fsize(), c_fsize() - 2);
	val("fchar", fchar(), c_fchar() == '_' ? 'c' : -1);
	val("fsame", fsame(), c_fsame());
	/* the expected values, not only as clang computes them */
	str("fname", fname(), "fname");
	str("fcallstatic", fcallstatic(), "fstatic");
	val("fsize", fsize(), 6);
	val("fsame", fsame(), 3);

	tryassert(0, 2, 0, 0, 0, 0);
	tryassert(0, 3, 1, "x == 2", "inassert", "void inassert(int)");
	tryassert(1, 3, 0, 0, 0, 0);
	tryassert(2, 1, 0, 0, 0, 0);
	tryassert(2, 0, 1, "x", "again", "void again(int)");
	printf("%d checks, %d bad\n", checks, bad);
	return bad != 0;
}
