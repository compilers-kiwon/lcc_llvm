/* lcc in a Yocto (OpenEmbedded) cross toolchain, with the LLVM back end:
   the driver of meta-lcc's lcc-cross.  The target comes from the host
   file OEHOST, e.g. -DOEHOST='"llvm-aarch64-linux.c"': cpp's predefined
   macros and rcc's target.  The rest is the toolchain's.  llc, of
   llvm-native, makes the object with the options LLCFLAGS of OEHOST, and
   OECC, e.g. -DOECC='"aarch64-oe-linux-gcc"', links; both are found
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
char *as[] = { "llc", LLCFLAGS, "-relocation-model=pic", "-filetype=obj",
	"-o", "$3", "$1", "$2", 0 };
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
