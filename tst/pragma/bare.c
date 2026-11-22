/*
 * bare.c - #pragma lines without a word.  lcc 4.2 read the first token of
 * the next line as their first word and lost that line.  lcc compiles
 * this the same with and without -Wf-pragma: the output is bare.1bk
 * (gcc -Wno-unknown-pragmas).  bare.i has the same lines for rcc alone.
 */
#include <stdio.h>

#pragma
int x = 1;

int f(void)
{
#pragma
	return 3;
}

int g(void)
{
	int a = 4;
#pragma /* only a comment, which cpp removes */
	a += 5;
	return a;
}

int h(void)
{
#pragma (not a word)
	return 6;
}

int k(void)
{
#pragma ref
	return 7;
}

int main(void)
{
	printf("%d %d %d %d %d\n", x, f(), g(), h(), k());
	return 0;
}
