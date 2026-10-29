/*
 * va_list has the System V x86-64 layout so that it can be passed to
 * glibc (vprintf etc.).  va_start, va_arg and va_end expand to
 * __builtin_va_* forms that the front end does not implement yet.
 */

#ifndef __GNUC_VA_LIST
#define __GNUC_VA_LIST
typedef struct __va_list_tag {
	unsigned int gp_offset;
	unsigned int fp_offset;
	void *overflow_arg_area;
	void *reg_save_area;
} __gnuc_va_list[1];
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

#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)

#endif /* __STDARG */
#endif /* __need___va_list */
