/* AArch64 Linux with the MLIR back end: rcc emits .mlir; mlir-opt,
   mlir-translate and clang of LLVM 21 do the rest.  It is a cross
   compiler: the headers and libraries are those of libc6-dev-arm64-cross,
   and the programs run with qemu-aarch64. */

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
	"-U__GNUC__", "-D_POSIX_SOURCE", "-D__STDC__=1", "-D__STRICT_ANSI__",
	"-Dunix", "-Dlinux", "-D__unix__", "-D__linux__",
	"-D__aarch64__", "-D__AARCH64EL__", "-D__ARM_64BIT_STATE",
	"-D__ARM_PCS_AAPCS64", "-D__CHAR_UNSIGNED__",
	"-D__LP64__", "-D_LP64", "-D__ELF__", "-D__signed__=signed",
	"$1", "$2", "$3", 0 };
#ifndef SYSROOT
#define SYSROOT "/usr/aarch64-linux-gnu"
#endif
char *include[] = { "-I" LCCDIR "include", "-I" SYSROOT "/include", 0 };
char *com[] = { LCCDIR "rcc", "-target=mlir/aarch64-linux",
	"-wchar_t=unsigned_int", "$1", "$2", "$3", 0 };
/* as: a script for sh -c, given the directory of the tools, the object,
   the .mlir and the -Wa options.  It lowers the .mlir to LLVM IR with
   mlir-opt and mlir-translate in a temporary directory, which it removes
   also when interrupted, and compiles that with clang.  Bytecode between
   mlir-opt and mlir-translate keeps fp128 constants exact.  The script
   has no newline (callsys splits commands there) and its first '$' is not
   followed by a digit (compose substitutes only the first $n of an
   element). */
/* what clang gives llc by default for this target, for the Yocto
   toolchain of etc/oe-linux.c */
#define LLCFLAGS "-mcpu=generic", "-mattr=+v8a,+fp-armv8,+neon"
char *as[] = { "/bin/sh", "-c",
	"t=`mktemp -d` || exit 1; trap 'rm -rf \"$t\"' 0; trap 'exit 1' 1 2 15; "
	"b=$1 o=$2 i=$3; shift 3; "
	"\"$b/mlir-opt\" --convert-arith-to-llvm --convert-cf-to-llvm "
	"--convert-func-to-llvm --reconcile-unrealized-casts --emit-bytecode "
	"\"$i\" -o \"$t/a.mlirbc\" && "
	"\"$b/mlir-translate\" --mlir-to-llvmir \"$t/a.mlirbc\" -o \"$t/a.ll\" && "
	"\"$b/clang\" --target=aarch64-linux-gnu \"$@\" -c -o \"$o\" \"$t/a.ll\"",
	/* 3 */ "lcc-as", /* 4 */ LLVMBIN, "$3", "$2", "$1", 0 };
char *ld[] = {
	/*  0 */ LLVMBIN "/clang", "", "-o", "$3", "$1", "$2",
	/*  6 */ "-L" LCCDIR, "-llcc", "-lm",
	/*  9 */ "--target=aarch64-linux-gnu", "-fuse-ld=lld",
	0 };

extern char *concat(char *, char *);

int option(char *arg) {
	if (strncmp(arg, "-lccdir=", 8) == 0) {
		cpp[0] = concat(&arg[8], "/cpp");
		include[0] = concat("-I", concat(&arg[8], "/include"));
		com[0] = concat(&arg[8], "/rcc");
		ld[6] = concat("-L", &arg[8]);
	} else if (strncmp(arg, "-llvmbin=", 9) == 0) {
		as[4] = &arg[9];
		ld[0] = concat(&arg[9], "/clang");
	} else if (strcmp(arg, "-g") == 0)
		;
	else if (strncmp(arg, "-ld=", 4) == 0)
		ld[0] = &arg[4];
	else if (strcmp(arg, "-static") == 0)
		ld[1] = "-static";
	else
		return 0;
	return 1;
}
