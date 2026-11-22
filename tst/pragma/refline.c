/*
 * refline.c - #pragma ref ends at the end of its line.  lcc 4.2 read on
 * in the next line when this one ended in \r, as in a file with CRLF line
 * ends, or in a comment, and lost lines.  run-llvm.sh compiles a copy with
 * CRLF line ends, with and without -Wf-pragma; refline.i has comments
 * for rcc alone.  The output is refline.1bk (gcc -Wno-unknown-pragmas).
 * No #include: lcc's cpp does not take #include <file> with a \r after it.
 */
int printf(const char *, ...);

int x = 1, y = 2;

#pragma ref x
int g(void)
{
	return 5;
}

int f(void)
{
#pragma ref x y
	return 3;
}

int main(void)
{
	printf("%d %d %d\n", f(), g(), x + y);
	return 0;
}
