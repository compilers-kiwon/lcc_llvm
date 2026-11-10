/*
 * Empty macro arguments as gcc and clang take them: S() calls a macro of
 * one parameter with one empty argument (C89 leaves an empty argument
 * undefined; C99 6.10.3 allows it), alone or beside #, ## and other
 * arguments, in text and in #if.  Compiled by lcc; macro-drv.c includes
 * this file again with F(n) renaming the functions, compiled by clang,
 * and compares the strings and values.
 */
#ifndef F
#define F(n) n
#endif

#define STR(x) #x
#define XSTR(x) STR(x)
#define ID(x) x
#define WRAP(x) <x>
#define PAIR(x, y) x|y
#define CAT(a, b) a ## b
#define CAT3(a, b, c) a ## b ## c
#define ZERO() z
#define PLUS0(x) (x + 0)
#define CALL(m) m()

char *F(sstr)(void) { return STR(); }
char *F(sstrsp)(void) { return STR( ); }
char *F(swrap)(void) { return XSTR(WRAP()); }
char *F(swrapsp)(void) { return XSTR(WRAP( )); }
char *F(swrapcm)(void) { return XSTR(WRAP(/**/)); }
char *F(swrapnl)(void) { return XSTR(WRAP(
)); }
char *F(sid)(void) { return XSTR(ID()); }
char *F(sidid)(void) { return XSTR(ID(ID())); }
char *F(spair)(void) { return XSTR(PAIR(,)); }
char *F(scat)(void) { return XSTR(CAT(a, ) CAT(, b) CAT(,)); }
char *F(scat3)(void) { return XSTR(CAT3(x, , y)); }
char *F(szero)(void) { return XSTR(ZERO()); }
char *F(scall)(void) { return XSTR(CALL(WRAP) CALL(ZERO)); }
char *F(sparen)(void) { return XSTR(WRAP(())); }

int F(vone)(void) { return ID() 1; }
int F(vplus)(void) { return 3 ID() + ID(4); }
int F(vplus0)(void) { return PLUS0() + PLUS0(5); }
int F(varr)(void) { int a ID() [3]; return sizeof a / sizeof a[0]; }

#if ID() 1
int F(vif)(void) { return 1; }
#else
int F(vif)(void) { return 0; }
#endif
