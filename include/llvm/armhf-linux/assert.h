/* assert.h for glibc.  glibc's own gives the name of the function only to
   gcc, clang and C99 compilers (__PRETTY_FUNCTION__, __func__) and a null
   pointer to others; this one gives lcc's __func__, so that the message
   names the function as with gcc and clang.  Like any assert.h, it has
   no include guard: each inclusion defines assert anew by NDEBUG. */
#include <features.h>

#undef assert
#ifdef __USE_GNU
#undef assert_perror
#endif

#ifdef NDEBUG
#define assert(expr) ((void)0)
#ifdef __USE_GNU
#define assert_perror(errnum) ((void)0)
#endif
#else
extern void __assert_fail(const char *__assertion, const char *__file,
	unsigned int __line, const char *__function);
#define assert(expr) \
	((expr) ? (void)0 : __assert_fail(#expr, __FILE__, __LINE__, __func__))
#ifdef __USE_GNU
extern void __assert_perror_fail(int __errnum, const char *__file,
	unsigned int __line, const char *__function);
#define assert_perror(errnum) \
	(!(errnum) ? (void)0 : __assert_perror_fail((errnum), __FILE__, __LINE__, __func__))
#endif
#endif
