/* AArch64 Linux with the LLVM back end: rcc emits .ll, clang does the rest.
   It is a cross compiler: the headers and libraries are those of
   libc6-dev-arm64-cross, and the programs run with qemu-aarch64. */

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
	"-D__aarch64__", "-D__AARCH64EL__", "-D__ARM_64BIT_STATE",
	"-D__ARM_PCS_AAPCS64", "-D__CHAR_UNSIGNED__",
	"-D__LP64__", "-D_LP64", "-D__ELF__", "-D__signed__=signed",
	"$1", "$2", "$3", 0 };
#ifndef SYSROOT
#define SYSROOT "/usr/aarch64-linux-gnu"
#endif
char *include[] = { "-I" LCCDIR "include", "-I" SYSROOT "/include", 0 };
char *com[] = { LCCDIR "rcc", "-target=llvm/aarch64-linux",
	"-wchar_t=unsigned_int", "$1", "$2", "$3", 0 };
/* what clang gives llc by default for this target, for the Yocto
   toolchain of etc/oe-linux.c */
#define LLCFLAGS "-mcpu=generic", "-mattr=+v8a,+fp-armv8,+neon"
char *as[] = { CLANG, "--target=aarch64-linux-gnu", "-c", "-o", "$3", "$1", "$2", 0 };
char *ld[] = {
	/*  0 */ CLANG, "", "-o", "$3", "$1", "$2",
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
