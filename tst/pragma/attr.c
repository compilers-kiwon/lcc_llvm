/*
 * attr.c - #pragma knpu and #pragma npu2 that lcc -Wf-pragma=knpu
 * -Wf-pragma=npu2 takes: the attributes knpu.op, knpu.args, npu2.op and
 * npu2.args of the mlir back end are in attr.abk, the output, the same
 * without the pragmas, in attr.1bk (gcc -Wno-unknown-pragmas).
 */
#include <stdio.h>
#include <stdarg.h>

#define conv2d not_expanded	/* cpp passes #pragma lines as they are */

signed char act0[8][4], act1[8][2];
static const signed char L1_W[2][4] = { { 1, -2, 3, -4 }, { -5, 6, -7, 8 } };
static const int L1_B[2] = { 10, -10 };
static const int L1_M[2] = { 1, 2 }, L1_S[2] = { 1, 2 };
int tent[3];				/* a tentative definition */
static int st[2];
static const signed char only[4] = { 9, 8, 7, 6 };	/* named only in a pragma */
extern signed char ext_only[8];		/* declared, defined nowhere */

/* as the generator writes it: on the line after {, before the declarations */
static void layer1(void) {
#pragma knpu conv2d in=act0 w=L1_W bias=L1_B mult=L1_M shift=L1_S out=act1 in_shape=(1,8,8,16) stride=(1,1) izp=-3 round="double"
	int p, o, i;

	for (p = 0; p < 8; p++)
		for (o = 0; o < 2; o++) {
			int acc = L1_B[o];
			for (i = 0; i < 4; i++)
				acc += L1_W[o][i] * act0[p][i];
			acc = acc * L1_M[o] - L1_S[o];
			if (acc < -128)
				acc = -128;
			if (acc > 127)
				acc = 127;
			act1[p][o] = (signed char)acc;
		}
}

/* every kind of value; blanks, a comment and continued lines */
static int values(void)
{
#pragma knpu values pos=7 neg=-3 plus=+5 sp = - 4 hex=0x7f HEX=0XFF negx=-0x10 \
	zero=0 mzero=-0 max=9223372036854775807 min=-9223372036854775808 \
	minx=-0x8000000000000000 maxx=0x7FFFFFFFFFFFFFFF empty=() one=(5) \
	many=( 1, -2,+3 , 0x10,-0x8000000000000000 ) /* a comment */ \
	str="a\"b\\c" esc="\a\b\f\n\r\t\v\'\?\0\101\x41\377\200" nul="" \
	obj=tent st=st ext=ext_only only=only
	return tent[0] + st[1];
}

/* the { on a line of its own, no arguments */
void place1(void)
{
#pragma knpu noargs
	tent[0] = 1;
}

/* between the declarations, and between the declarations and the
   statements; a second name space; a name space that is not given */
int place2(int n)
{
	int a = n;
#pragma knpu between a=act1
	int b = a + 1;
#pragma npu2 second s="another name space"
	b += 2;
#pragma npu3 ignored=1
	return b;
}

/* after a block in which act0 is a local */
int place3(void)
{
	int r = 0;

	{
		int act0 = 5;
		r = act0;
	}
#pragma knpu after_block in=act0
	return r + act0[1][1];
}

/* before the } of the function, after a statement */
void place4(void)
{
	tent[1] = 2;
	if (tent[0])
		tent[2] = 3;
#pragma knpu last x=tent
}

/* followed by other pragmas */
void place5(void)
{
#pragma knpu with_others x=st
#pragma ref st
#pragma npu3 other
	st[0] = 3;
}

/* names declared extern in the block: the static st, one defined below,
   one defined nowhere */
int blockextern(void)
{
	extern int st[2];
	extern int later[2];
	extern int blk_only[2];
#pragma knpu blockextern a=st b=later c=blk_only
	return st[0] + later[1];
}
int later[2] = { 10, 20 };

/* an llvm.func: variadic */
int sum(int n, ...)
{
#pragma knpu variadic n=(1)
	va_list ap;
	int s = 0;

	va_start(ap, n);
	while (n-- > 0)
		s += va_arg(ap, int);
	va_end(ap);
	return s;
}

/* an llvm.func on x86-64: an old-style definition called before it */
int oldstyle();
int callold(void)
{
	return oldstyle(4);
}
int oldstyle(a)
int a;
{
#pragma knpu oldstyle in=act0
#pragma npu2 oldstyle2
	return a * 3;
}

int main(void)
{
	int p, i;

	for (p = 0; p < 8; p++)
		for (i = 0; i < 4; i++)
			act0[p][i] = (signed char)(3*p - 5*i);
	layer1();
	for (p = 0; p < 8; p++)
		printf("%d %d\n", act1[p][0], act1[p][1]);
	place1();
	place4();
	place5();
	st[1] = 4;
	printf("%d %d %d %d\n", values(), place2(5), place3(), blockextern());
	printf("%d %d\n", sum(3, 1, 2, 3), callold());
	return 0;
}
