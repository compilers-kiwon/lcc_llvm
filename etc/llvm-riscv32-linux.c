/* RISC-V 32 (rv32gc, ILP32D) Linux with the LLVM back end: rcc emits .ll,
   clang does the rest.  It is a cross compiler: LCCDIR/riscv32 is (a link
   to) a riscv-gnu-toolchain built for rv32gc/ilp32d, whose sysroot has
   glibc; clang links with its libgcc and crt files, and the programs run
   with qemu-riscv32 -L LCCDIR/riscv32/sysroot. */

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
	"-D__riscv", "-D__riscv_xlen=32", "-D__riscv_flen=64",
	"-D__riscv_float_abi_double", "-D__SIZEOF_POINTER__=4",
	"-D__CHAR_UNSIGNED__", "-D__ELF__", "-D__signed__=signed",
	"$1", "$2", "$3", 0 };
char *include[] = { "-I" LCCDIR "include",
	"-I" LCCDIR "riscv32/sysroot/usr/include", 0 };
char *com[] = { LCCDIR "rcc", "-target=llvm/riscv32-linux",
	"-wchar_t=unsigned_int", "$1", "$2", "$3", 0 };
#define TARGET "--target=riscv32-unknown-linux-gnu", "-march=rv32gc", "-mabi=ilp32d"
char *as[] = { CLANG, TARGET, "-c", "-o", "$3", "$1", "$2", 0 };
char *ld[] = {
	/*  0 */ CLANG, "", "-o", "$3", "$1", "$2",
	/*  6 */ "-L" LCCDIR, "-llcc", "-lm",
	/*  9 */ "--target=riscv32-unknown-linux-gnu", "-march=rv32gc", "-mabi=ilp32d",
	/* 12 */ "--gcc-toolchain=" LCCDIR "riscv32", "--sysroot=" LCCDIR "riscv32/sysroot",
	/* 14 */ "-fuse-ld=lld",
	0 };

extern char *concat(char *, char *);

int option(char *arg) {
	if (strncmp(arg, "-lccdir=", 8) == 0) {
		cpp[0] = concat(&arg[8], "/cpp");
		include[0] = concat("-I", concat(&arg[8], "/include"));
		com[0] = concat(&arg[8], "/rcc");
		include[1] = concat("-I", concat(&arg[8], "/riscv32/sysroot/usr/include"));
		ld[6] = concat("-L", &arg[8]);
		ld[12] = concat("--gcc-toolchain=", concat(&arg[8], "/riscv32"));
		ld[13] = concat("--sysroot=", concat(&arg[8], "/riscv32/sysroot"));
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
