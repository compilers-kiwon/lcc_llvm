/* lcc in a Yocto (OpenEmbedded) cross toolchain: the driver of meta-lcc's
   lcc-cross.  The target and the back end come from the host file
   OEHOST, e.g. -DOEHOST='"llvm-aarch64-linux.c"' or "mlir-aarch64-linux.c":
   cpp's predefined macros and rcc's target.  The rest is the toolchain's.
   llc, of llvm-native, makes the object with the options LLCFLAGS of
   OEHOST, after mlir-native's mlir-opt and mlir-translate for MLIR, and
   OECC, e.g. -DOECC='"aarch64-oe-linux-gcc"', links; all are found
   through PATH.  As with Yocto's gcc, the headers and libraries are those
   of the sysroot that --sysroot=dir gives; liblcc is in it, from
   lcc-runtime.  LCCDIR holds cpp, rcc and lcc's own headers. */

#include <string.h>

/* OEHOST's include, as, ld and option are replaced by those below */
#define include oehost_include
#define as oehost_as
#define ld oehost_ld
#define option oehost_option
#include OEHOST
#undef include
#undef as
#undef ld
#undef option

/* include[1] is the sysroot's, if --sysroot is given */
char *include[] = { "-I" LCCDIR "include", 0, 0 };
#ifdef LLVMBIN
/* an mlir host file: lower the .mlir to LLVM IR as its driver does, through
   bytecode, in which f128 constants keep all their bits, then llc */
char *as[] = { "/bin/sh", "-c",
	"t=`mktemp -d` || exit 1; trap 'rm -rf \"$t\"' 0; trap 'exit 1' 1 2 15; "
	"o=$1 i=$2; shift 2; "
	"mlir-opt --convert-arith-to-llvm --convert-cf-to-llvm "
	"--convert-func-to-llvm --reconcile-unrealized-casts --emit-bytecode "
	"\"$i\" -o \"$t/a.mlirbc\" && "
	"mlir-translate --mlir-to-llvmir \"$t/a.mlirbc\" -o \"$t/a.ll\" && "
	"llc \"$@\" -o \"$o\" \"$t/a.ll\"",
	"lcc-as", "$3", "$2", LLCFLAGS, "-relocation-model=pic", "-filetype=obj",
	"$1", 0 };
#else
char *as[] = { "llc", LLCFLAGS, "-relocation-model=pic", "-filetype=obj",
	"-o", "$3", "$1", "$2", 0 };
#endif
/* OEHOST may give OELDFLAGS, options that gcc needs to link for the target */
#ifndef OELDFLAGS
#define OELDFLAGS ""
#endif
char *ld[] = {
	/* 0 */ OECC, "", "", OELDFLAGS, "-o", "$3", "$1", "$2", "-llcc", "-lm",
	0 };

int option(char *arg) {
	if (strncmp(arg, "--sysroot=", 10) == 0) {
		include[1] = concat("-I", concat(&arg[10], "/usr/include"));
		ld[2] = arg;
	} else if (strncmp(arg, "-lccdir=", 8) == 0) {
		include[0] = concat("-I", concat(&arg[8], "/include"));
		oehost_option(arg);
	} else if (strncmp(arg, "-ld=", 4) == 0)
		ld[0] = &arg[4];
	else if (strcmp(arg, "-static") == 0)
		ld[1] = "-static";
	else
		return oehost_option(arg);
	return 1;
}
