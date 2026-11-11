/*
 * Kinds of functions and calls: functions called before they are
 * defined, with and without prototypes or by implicit declaration;
 * old-style definitions, called with and without prototypes; char and
 * short results; function addresses in initializers before the
 * definitions, plain and plus a constant; tables of function pointers;
 * mutual recursion of static functions.  A static function and a
 * static variable have the names of globals that kr-drv.c defines, so
 * giving them external linkage makes the link fail.
 *
 * (char *)f converts a function pointer to an object pointer, which
 * gcc and clang accept but -pedantic rejects.  The constants are even:
 * on Thumb the low bit of a function address marks Thumb code, and the
 * linker sets it after adding the constant to an address in data, where
 * code adds the constant to the marked address.
 */
int noproto();
int later1(int);
int later2();
static int even(int);

static int helper(int x) { return x + 1000; }
static int counter = 5;

int (*early[])() = { later1, later2, noproto };
char *fplus[] = { (char *)later1 + 2, (char *)later2 + 4, (char *)helper - 2 };

int callnoproto(int x) { return noproto(x, 3); }
int noproto(a, b) int a, b; { return a*10 + b; }

int callimplicit(int x) { return implicit(x) + 1; }
int implicit(int x) { return x*3; }

int oldstyle(a, s) int a; char *s; { return a + s[0]; }
int calloldstyle(int x) { return oldstyle(x, "A"); }

int protoold(int a, long b);
int protoold(a, b) int a; long b; { return a - (int)b; }
int callprotoold(int x) { return protoold(x, 3L); }

int krchar(c, s) char c; short s; { return c + s; }
int callkrchar(int x) { return krchar(x, x); }

char retc(int x) { return x; }
unsigned char retuc(int x) { return x; }
short rets(int x) { return x; }
unsigned short retus(int x) { return x; }
char oldretc(x) int x; { return x; }

int callrets(int x) {
	char (*pc)(int) = retc;
	unsigned char (*puc)(int) = retuc;
	short (*ps)(int) = rets;
	unsigned short (*pus)(int) = retus;
	char (*pold)() = oldretc;

	return retc(x) + retuc(x) + rets(x) + retus(x) + oldretc(x)
		+ 2*(pc(x) + puc(x) + ps(x) + pus(x) + pold(x));
}

int later1(int x) { return x - 1; }
int later2(x) int x; { return x - 2; }

int callearly(int i, int x) { return i == 2 ? early[i](x, 7) : early[i](x); }

int add2(int a, int b) { return a + b; }
int sub2(int a, int b) { return a - b; }
static int mul2(int a, int b) { return a*b; }
int (*ops[])(int, int) = { add2, sub2, mul2 };

int callops(int i, int a, int b) {
	int (*f)(int, int) = ops[i];

	return f(a, b) + (*ops[i])(b, a);
}

static int odd(int n) { return n == 0 ? 0 : even(n - 1); }
static int even(int n) { return n == 0 ? 1 : odd(n - 1); }
int iseven(int n) { return even(n); }

char *fplus1(void) { return (char *)later1 + 2; }
char *fplus2(void) { return (char *)later2 + 4; }
char *fplus3(void) { return (char *)helper - 2; }
char *fhelper(void) { return (char *)helper; }

int callhelper(int x) { counter++; return helper(x) + counter; }

/* a function implicitly declared at file scope, in sizeof, is the one
   defined later: lcc kept two symbols and declared it twice */
int implsize = sizeof fileimpl(1);
int fileimpl(int x) { return x + 7; }
