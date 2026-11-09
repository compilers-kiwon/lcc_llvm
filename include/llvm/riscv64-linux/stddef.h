#ifndef __STDDEF
#define __STDDEF
#endif

/*
 * Every type is guarded on its own and the glibc __need_* requests are
 * accepted without being honored: whichever header asks, all of them
 * are defined once.
 */

#if !defined(_SIZE_T) && !defined(_SIZE_T_) && !defined(_SIZE_T_DEFINED)
#define _SIZE_T
#define _SIZE_T_
#define _SIZE_T_DEFINED
typedef unsigned long size_t;
#endif

#if !defined(_PTRDIFF_T) && !defined(_PTRDIFF_T_)
#define _PTRDIFF_T
#define _PTRDIFF_T_
typedef long ptrdiff_t;
#endif

/* rcc is run with -wchar_t=unsigned_int; glibc uses int, same size */
#if !defined(_WCHAR_T) && !defined(_WCHAR_T_) && !defined(_WCHAR_T_DEFINED)
#define _WCHAR_T
#define _WCHAR_T_
#define _WCHAR_T_DEFINED
typedef unsigned int wchar_t;
#endif

#ifndef NULL
#define NULL ((void*)0)
#endif

#ifndef offsetof
#define offsetof(ty,mem) ((size_t)((char*)&((ty*)0)->mem - (char*)0))
#endif

#undef __need_size_t
#undef __need_ptrdiff_t
#undef __need_wchar_t
#undef __need_NULL
#undef __need_wint_t
