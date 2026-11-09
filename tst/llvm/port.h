/*
 * What the unit tests need to know of the target beyond what C89
 * promises, from its standard headers only, so that they run on targets
 * whose long has 32 bits, whose char is unsigned or whose long double is
 * not the x87 80-bit type.
 */
#ifndef PORT_H
#define PORT_H
#include <limits.h>
#include <float.h>

/* LV(v64, v32): the long constant v64 if long has 64 bits, else v32;
   the other one is dropped by the preprocessor, so v64 may be too big
   for a 32-bit long */
#if LONG_MAX > 2147483647L
#define LONG64 1
#define LV(v64, v32) v64
#else
#define LONG64 0
#define LV(v64, v32) v32
#endif
#define LONGBITS ((int)(sizeof(long) * CHAR_BIT))

/* LDBYTES: how many bytes of a long double hold its value; the x87
   80-bit type has 10, and the rest of its storage is padding */
#if LDBL_MANT_DIG == 64
#define LDBYTES 10
#else
#define LDBYTES ((int)sizeof(long double))
#endif

/* LDV(wide, narrow): the long double constant wide if long double has a
   wider exponent range than double (x87, IEEE quad), else narrow */
#if LDBL_MAX_EXP > DBL_MAX_EXP
#define LDV(wide, narrow) wide
#else
#define LDV(wide, narrow) narrow
#endif

/* the largest long double, the least normal one and the least
   denormal one, written with all the digits the type needs */
#if LDBL_MANT_DIG == 64		/* x87 */
#define LDMAXC 1.18973149535723176502e+4932L
#define LDMINC 3.36210314311209350626e-4932L
#define LDDENC 3.64519953188247460253e-4951L
#elif LDBL_MANT_DIG == 113	/* IEEE quad */
#define LDMAXC 1.18973149535723176508575932662800702e+4932L
#define LDMINC 3.36210314311209350626267781732175260e-4932L
#define LDDENC 6.47517511943802511092443895822764655e-4966L
#else				/* long double is double */
#define LDMAXC 1.7976931348623157e+308L
#define LDMINC 2.2250738585072014e-308L
#define LDDENC 4.9406564584124654e-324L
#endif
#endif
