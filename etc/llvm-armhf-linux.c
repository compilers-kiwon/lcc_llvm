/* ARM hard-float (armv7, gnueabihf) Linux with the LLVM back end: rcc
   emits .ll, clang does the rest.  It is a cross compiler: the headers
   and libraries are those of libc6-dev-armhf-cross, and the programs run
   with qemu-arm. */

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
	"-U__GNUC__", "-D_POSIX_SOURCE", "-D__STDC__=1", "-D__STRICT_ANSI__",
	"-Dunix", "-Dlinux", "-D__unix__", "-D__linux__",
	"-D__arm__", "-D__ARMEL__", "-D__ARM_EABI__", "-D__ARM_ARCH=7",
	"-D__ARM_ARCH_7A__", "-D__ARM_PCS_VFP", "-D__VFP_FP__",
	"-D__SIZEOF_POINTER__=4", "-D__CHAR_UNSIGNED__",
	"-D__ELF__", "-D__signed__=signed",
	/* off_t has 64 bits, time_t 32: Ubuntu's gcc defines also
	   _TIME_BITS=64, but glibc renames time() and the like for it with
	   __asm__ labels, and for other compilers to names it does not
	   declare.  Objects from clang -D_FILE_OFFSET_BITS=64 and gcc
	   -U_TIME_BITS -D_FILE_OFFSET_BITS=64 match lcc's (see README) */
	"-D_FILE_OFFSET_BITS=64",
	"$1", "$2", "$3", 0 };
#ifndef SYSROOT
#define SYSROOT "/usr/arm-linux-gnueabihf"
#endif
char *include[] = { "-I" LCCDIR "include", "-I" SYSROOT "/include", 0 };
char *com[] = { LCCDIR "rcc", "-target=llvm/armhf-linux",
	"-wchar_t=unsigned_int", "$1", "$2", "$3", 0 };
char *as[] = { CLANG, "--target=arm-linux-gnueabihf", "-c", "-o", "$3", "$1", "$2", 0 };
char *ld[] = {
	/*  0 */ CLANG, "", "-o", "$3", "$1", "$2",
	/*  6 */ "-L" LCCDIR, "-llcc", "-lm",
	/*  9 */ "--target=arm-linux-gnueabihf", "-fuse-ld=lld",
	0 };

extern char *concat(char *, char *);

int option(char *arg) {
	if (strncmp(arg, "-lccdir=", 8) == 0) {
		cpp[0] = concat(&arg[8], "/cpp");
		include[0] = concat("-I", concat(&arg[8], "/include"));
		com[0] = concat(&arg[8], "/rcc");
		ld[6] = concat("-L", &arg[8]);
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
