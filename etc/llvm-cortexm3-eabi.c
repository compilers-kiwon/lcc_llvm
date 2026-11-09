/* Cortex-M3 (thumbv7m, soft float, no OS) with the LLVM back end: rcc
   emits .ll, clang does the rest.  It is a cross compiler: the C library is
   newlib's (libnewlib-arm-none-eabi); arm-none-eabi-gcc links with
   semihosting (rdimon), etc/cortexm3/cortexm3.ld and the vector table
   etc/cortexm3/vectors.c, which are LCCDIR/cortexm3.ld and LCCDIR/vectors.o
   (compiled by arm-none-eabi-gcc); the programs run on qemu-system-arm
   -M mps2-an385. */

#include <string.h>

static char rcsid[] = "$Id$";

#ifndef LCCDIR
#define LCCDIR "/usr/local/lib/lcc/"
#endif
#ifndef CLANG
#define CLANG "/usr/bin/clang"
#endif

char *suffixes[] = { ".c", ".i", ".ll", ".o", ".out", 0 };
char inputs[256] = "";
char *cpp[] = { LCCDIR "cpp",
	"-U__GNUC__", "-D__STDC__=1", "-D__STRICT_ANSI__",
	"-D__arm__", "-D__thumb__", "-D__thumb2__", "-D__ARMEL__",
	"-D__ARM_EABI__", "-D__ARM_ARCH=7", "-D__ARM_ARCH_7M__", "-D__SOFTFP__",
	"-D__VFP_FP__", "-D__CHAR_UNSIGNED__", "-D__ELF__", "-D__signed__=signed",
	"-D__SIZEOF_POINTER__=4",
	/* what newlib's headers take from gcc */
	"-D__extension__=",
	"-D__ORDER_LITTLE_ENDIAN__=1234", "-D__ORDER_BIG_ENDIAN__=4321",
	"-D__BYTE_ORDER__=__ORDER_LITTLE_ENDIAN__",
	"-D__INT_MAX__=0x7fffffff", "-D__LONG_MAX__=0x7fffffffL",
	"-D__WCHAR_MAX__=0xffffffffU", "-D__WCHAR_MIN__=0U",
	"-D__SIZE_TYPE__=unsigned int", "-D__PTRDIFF_TYPE__=int",
	"-D__INT8_TYPE__=signed char", "-D__UINT8_TYPE__=unsigned char",
	"-D__INT16_TYPE__=short int", "-D__UINT16_TYPE__=short unsigned int",
	"-D__INT32_TYPE__=long int", "-D__UINT32_TYPE__=long unsigned int",
	"-D__INT64_TYPE__=long long int", "-D__UINT64_TYPE__=long long unsigned int",
	"-D__INT_LEAST8_TYPE__=signed char", "-D__UINT_LEAST8_TYPE__=unsigned char",
	"-D__INT_LEAST16_TYPE__=short int", "-D__UINT_LEAST16_TYPE__=short unsigned int",
	"-D__INT_LEAST32_TYPE__=long int", "-D__UINT_LEAST32_TYPE__=long unsigned int",
	"-D__INT_LEAST64_TYPE__=long long int", "-D__UINT_LEAST64_TYPE__=long long unsigned int",
	"-D__INT_FAST8_TYPE__=int", "-D__UINT_FAST8_TYPE__=unsigned int",
	"-D__INT_FAST16_TYPE__=int", "-D__UINT_FAST16_TYPE__=unsigned int",
	"-D__INT_FAST32_TYPE__=int", "-D__UINT_FAST32_TYPE__=unsigned int",
	"-D__INT_FAST64_TYPE__=long long int", "-D__UINT_FAST64_TYPE__=long long unsigned int",
	"-D__INTMAX_TYPE__=long long int", "-D__UINTMAX_TYPE__=long long unsigned int",
	"-D__INTPTR_TYPE__=int", "-D__UINTPTR_TYPE__=unsigned int",
	"$1", "$2", "$3", 0 };
#ifndef SYSROOT
#define SYSROOT "/usr/lib/arm-none-eabi"
#endif
/* newlib's headers are system headers (-J): they define variadic macros */
char *include[] = { "-I" LCCDIR "include", "-J" SYSROOT "/include", 0 };
char *com[] = { LCCDIR "rcc", "-target=llvm/cortexm3-eabi",
	"-wchar_t=unsigned_int", "$1", "$2", "$3", 0 };
/* clang 18 normalizes the triple to thumbv7m-none-unknown-eabi, 21 (and
   rcc's IR, for both) to thumbv7m-unknown-none-eabi: the same target */
char *as[] = { CLANG, "--target=thumbv7m-none-eabi", "-mcpu=cortex-m3",
	"-mfloat-abi=soft", "-Wno-override-module", "-c", "-o", "$3", "$1", "$2", 0 };
#ifndef ARMGCC
#define ARMGCC "/usr/bin/arm-none-eabi-gcc"
#endif
char *ld[] = {
	/*  0 */ ARMGCC, "-mcpu=cortex-m3", "-mthumb", "--specs=rdimon.specs",
	/*  4 */ "-T", LCCDIR "cortexm3.ld",
	/*  6 */ "-Wl,--no-enum-size-warning", "-Wl,--no-warn-execstack",
	/*  8 */ "-o", "$3", "$1", "$2", LCCDIR "vectors.o",
	/* 13 */ "-L" LCCDIR, "-llcc", "-lm",
	0 };

extern char *concat(char *, char *);

int option(char *arg) {
	if (strncmp(arg, "-lccdir=", 8) == 0) {
		cpp[0] = concat(&arg[8], "/cpp");
		include[0] = concat("-I", concat(&arg[8], "/include"));
		com[0] = concat(&arg[8], "/rcc");
		ld[5] = concat(&arg[8], "/cortexm3.ld");
		ld[12] = concat(&arg[8], "/vectors.o");
		ld[13] = concat("-L", &arg[8]);
	} else if (strcmp(arg, "-g") == 0)
		;
	else if (strncmp(arg, "-ld=", 4) == 0)
		ld[0] = &arg[4];
	else
		return 0;
	return 1;
}
