/*
 * __func__, which gcc and clang give in C89 too: static const char
 * __func__[] = "name" in each function.  Compiled by lcc; func-drv.c
 * includes this file again with F(n) renaming the functions, compiled by
 * clang, and compares the names (without clang's c_) and sizes.
 *
 * Compiled by lcc only, assert names the function as with gcc (lcc's
 * assert.h on glibc, __ASSERT_FUNC on newlib; clang's glibc assert gives
 * __PRETTY_FUNCTION__, "void f(int)", when this file is compiled by clang
 * to check the test).  This file defines
 * the C library's function that assert calls when it fails, which saves
 * its arguments and returns to the test with longjmp.
 */
#ifndef F
#define F(n) n
#define LCCSIDE
#endif

const char *F(fname)(void) { return __func__; }
int F(fsize)(void) { return sizeof __func__; }
const char *F(fnested)(void) { { { return __func__; } } }
static const char *F(fstatic)(void) { return __func__; }
const char *F(fcallstatic)(void) { return F(fstatic)(); }
int F(fchar)(void) { return __func__[1]; }
int F(fsame)(void) {
	static const char *s = __func__;
	const char *a = __func__, *b;
	{ b = __func__; }
	return (a == b) + (a == s) + (a != F(fname)());
}

#ifdef LCCSIDE
#include <setjmp.h>
#include <assert.h>

static jmp_buf aenv;
static const char *afile, *aexpr, *afunc;
static unsigned aline;

#if defined __GLIBC__
void __assert_fail(const char *expr, const char *file, unsigned int line,
	const char *func) {
#elif defined __NEWLIB__
void __assert_func(const char *file, int line, const char *func,
	const char *expr) {
#else
#error "neither glibc nor newlib"
#endif
	afile = file;
	aline = line;
	afunc = func;
	aexpr = expr;
	longjmp(aenv, 1);
}

int expline;
#ifdef __clang__
int clangpretty = 1;	/* clang's __PRETTY_FUNCTION__ is "void f(int)" */
#else
int clangpretty = 0;
#endif

static void inassert(int x) {
	expline = __LINE__; assert(x == 2);
}

#define NDEBUG
#include <assert.h>
static void offassert(int x) {
	assert(x == 2);
}
#undef NDEBUG
#include <assert.h>

static void again(int x) {
	expline = __LINE__; assert(x);
}

/* calls f(x); 1 if assert failed, and then the arguments it gave */
int runassert(int which, int x, const char **file, const char **expr,
	const char **func, int *line) {
	static void (*f[])(int) = { inassert, offassert, again };

	afile = aexpr = afunc = 0;
	aline = 0;
	if (setjmp(aenv)) {
		*file = afile;
		*expr = aexpr;
		*func = afunc;
		*line = aline - expline;
		return 1;
	}
	f[which](x);
	return 0;
}
#endif
