/* Replaces glibc's bits/setjmp.h for ARM, which aligns __jmp_buf to 8,
   as the ARM C library ABI requires, with __attribute__((__aligned__(8))):
   other compilers than gcc and clang do not see the attribute.  The union
   with a double has the same size, 256, and alignment, 8, as gcc's and
   clang's array, so jmp_buf has their size, 392, and alignment.  glibc
   uses __jmp_buf only as the type of a member. */
#ifndef _BITS_SETJMP_H
#define _BITS_SETJMP_H 1

#if !defined _SETJMP_H && !defined _PTHREAD_H
#error "include <setjmp.h>, not <bits/setjmp.h>"
#endif

typedef union {
	int __regs[64];
	double __align;
} __jmp_buf;

#endif
