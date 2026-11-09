/*
 * va_list is a pointer to the next argument; the ARM EABI's is a
 * structure of one pointer, which newlib (vprintf etc.) takes in the
 * same register.  The back end implements calls to __builtin_va_start
 * and __builtin_va_end, and __builtin_va_arg, a keyword of the front end.
 */

#ifndef __GNUC_VA_LIST
#define __GNUC_VA_LIST
typedef void *__gnuc_va_list;
#endif

#ifdef __need___va_list
#undef __need___va_list
#else

#ifndef __STDARG
#define __STDARG

#ifndef _VA_LIST_DEFINED
#define _VA_LIST_DEFINED
typedef __gnuc_va_list va_list;
#endif

extern void __builtin_va_start(__gnuc_va_list *), __builtin_va_end(__gnuc_va_list *);
#define va_start(ap, last) __builtin_va_start(&(ap))
#define va_arg(ap, type) __builtin_va_arg(&(ap), type)
#define va_end(ap) __builtin_va_end(&(ap))

#endif /* __STDARG */
#endif /* __need___va_list */
