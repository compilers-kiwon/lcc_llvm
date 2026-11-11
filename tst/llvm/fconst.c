/*
 * Floating constants out of the range of their types.  As gcc and clang,
 * lcc warns once when a constant is too large and becomes infinite, once
 * when a nonzero constant is too small and becomes 0, and not when it
 * is subnormal; fconst.2bk holds lcc's warnings.  Compiled by lcc;
 * fconst-drv.c includes this file again with F(n) renaming the objects,
 * compiled by clang, and compares the values.
 */
#ifndef F
#define F(n) n
#endif

float F(fbig) = 1e39f;
float F(fsmall) = 1e-50f;
float F(fsub) = 1e-45f;
float F(fzero) = 0.000e-99f;
float F(ftop) = 3.40282346e38f;
double F(dbig) = 1e10000;
double F(dsmall) = 1e-400;
double F(dsub) = .5e-323;
double F(dzero) = 00.0e-999;
double F(dtop) = 1.7976931348623157e308;
long double F(lbig) = 1e5000L;
long double F(lsmall) = 1e-5000L;
long double F(lzero) = 0e-9999L;

double F(dneg)(void) { return -1e400 + 1; }
double F(dsum)(void) { return 2e-400 + 1e-310; }
float F(fprod)(void) { return 1e-46F * 2; }

/* lcc's table of constants took 0.0 for a constant before it that float
   makes 0, as 1e-310, and -0.0 must still differ from 0.0 */
double F(zafter)(void) { double t = 1e-310, z = 0.0; return t > 0 ? z : 1; }
double F(nzero)(void) { double z = 0.0, n = -0.0; return z == 0 ? n : 1; }
