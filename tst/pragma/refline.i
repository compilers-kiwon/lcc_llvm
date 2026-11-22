# 1 "tst/pragma/refline.i"
/*
 * refline.i - the #pragma ref lines of refline.c as rcc gets them without
 * cpp, with comments: one that ends on its line, and one that does not,
 * which ends with the line, so that rcc reads the next line
 */
int printf(const char *, ...);
int x = 1, y = 2;
#pragma ref x /* a comment */
int g(void)
{
	return 5;
}
int f(void)
{
#pragma ref x /* a comment */ y /* one that does not end on this line
	return 3;
}
int main(void)
{
	printf("%d %d %d\n", f(), g(), x + y);
	return 0;
}
