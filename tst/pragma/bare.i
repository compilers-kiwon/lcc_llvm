# 1 "tst/pragma/bare.i"
#pragma
int x = 1;
/*
 * bare.i - the #pragma lines of bare.c as rcc gets them without cpp: the
 * first (lcc 4.2 read int before it had set up the types, and crashed),
 * lines that end in blanks (f, h), one with only a comment (g)
 */
int printf(const char *, ...);
int f(void)
{
#pragma   
	return 3;
}
int g(void)
{
	int a = 4;
#pragma /* only a comment */
	a += 5;
	return a;
}
int h(void)
{
#pragma	
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
