/* x86-64 Linux with the LLVM back end: rcc emits .ll, clang does the rest */

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
	"-D__x86_64__", "-D__x86_64", "-D__amd64__", "-D__amd64",
	"-D__LP64__", "-D_LP64", "-D__ELF__", "-D__signed__=signed",
	"$1", "$2", "$3", 0 };
char *include[] = { "-I" LCCDIR "include",
	"-I/usr/include/x86_64-linux-gnu", "-I/usr/include", 0 };
char *com[] = { LCCDIR "rcc", "-target=llvm/x86_64-linux",
	"-wchar_t=unsigned_int", "$1", "$2", "$3", 0 };
char *as[] = { CLANG, "-c", "-o", "$3", "$1", "$2", 0 };
char *ld[] = {
	/*  0 */ CLANG, "", "-o", "$3", "$1", "$2",
	/*  6 */ "-L" LCCDIR, "-llcc", "-lm",
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
