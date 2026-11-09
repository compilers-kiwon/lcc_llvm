/* Cortex-M3 (thumbv7m, soft float, no OS) with the MLIR back end: rcc
   emits .mlir; mlir-opt, mlir-translate and clang of LLVM 21 make an
   object.  It is a cross compiler: the C library is
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
#ifndef LLVMBIN
#define LLVMBIN "/usr/lib/llvm-21/bin"
#endif

char *suffixes[] = { ".c", ".i", ".mlir", ".o", ".out", 0 };
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
char *com[] = { LCCDIR "rcc", "-target=mlir/cortexm3-eabi",
	"-wchar_t=unsigned_int", "$1", "$2", "$3", 0 };
/* as: a script for sh -c, given the directory of the tools, the object,
   the .mlir and the -Wa options.  It lowers the .mlir to LLVM IR with
   mlir-opt and mlir-translate in a temporary directory, which it removes
   also when interrupted, and compiles that with clang.  Bytecode between
   mlir-opt and mlir-translate keeps fp128 constants exact.  The script
   has no newline (callsys splits commands there) and its first '$' is not
   followed by a digit (compose substitutes only the first $n of an
   element). */
char *as[] = { "/bin/sh", "-c",
	"t=`mktemp -d` || exit 1; trap 'rm -rf \"$t\"' 0; trap 'exit 1' 1 2 15; "
	"b=$1 o=$2 i=$3; shift 3; "
	"\"$b/mlir-opt\" --convert-arith-to-llvm --convert-cf-to-llvm "
	"--convert-func-to-llvm --reconcile-unrealized-casts --emit-bytecode "
	"\"$i\" -o \"$t/a.mlirbc\" && "
	"\"$b/mlir-translate\" --mlir-to-llvmir \"$t/a.mlirbc\" -o \"$t/a.ll\" && "
	"\"$b/clang\" --target=thumbv7m-none-eabi -mcpu=cortex-m3 -mfloat-abi=soft \"$@\" -c -o \"$o\" \"$t/a.ll\"",
	/* 3 */ "lcc-as", /* 4 */ LLVMBIN, "$3", "$2", "$1", 0 };
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
	} else if (strncmp(arg, "-llvmbin=", 9) == 0)
		as[4] = &arg[9];
	else if (strcmp(arg, "-g") == 0)
		;
	else if (strncmp(arg, "-ld=", 4) == 0)
		ld[0] = &arg[4];
	else
		return 0;
	return 1;
}
