/*
 * err-syntax.c - #pragma knpu lines that are not well formed, with the
 * messages of lcc -Wf-pragma=knpu in err-syntax.2bk, one for each line
 * at most.  A value is a name, an integer that fits in 64 bits, decimal
 * or hexadecimal with an optional sign, a list of them in parentheses,
 * or a string.
 */
int g;

void names(void)
{
#pragma knpu
#pragma knpu int
#pragma knpu 3op
#pragma knpu op if=1
#pragma knpu op a
#pragma knpu op a 3
#pragma knpu op a=
#pragma knpu op a=1 b=2 a=3
#pragma knpu op a=1, b=2
#pragma knpu op a=1 ;
#pragma knpu op a=int
}

void integers(void)
{
#pragma knpu op a=1.5
#pragma knpu op a=1e3
#pragma knpu op a=.5
#pragma knpu op a=0x1p3
#pragma knpu op a=010
#pragma knpu op a=08
#pragma knpu op a=00
#pragma knpu op a=10u
#pragma knpu op a=0x10L
#pragma knpu op a=0x
#pragma knpu op a=1f
#pragma knpu op a='x'
#pragma knpu op a=L'x'
#pragma knpu op a=--1
#pragma knpu op a=-g
#pragma knpu op a=9223372036854775808
#pragma knpu op a=-9223372036854775809
#pragma knpu op a=0x8000000000000000
#pragma knpu op a=-0x8000000000000001
#pragma knpu op a=18446744073709551616
#pragma knpu op a=99999999999999999999999999999
}

void lists(void)
{
#pragma knpu op a=(1,(2))
#pragma knpu op a=(1,2,)
#pragma knpu op a=(1,2
#pragma knpu op a=(1 2)
#pragma knpu op a=(g)
#pragma knpu op a=("x")
#pragma knpu op a=(1.5)
#pragma knpu op a=(9223372036854775808)
}

void strings(void)
{
#pragma knpu op a=L"x"
#pragma knpu op a="x" "y"
#pragma knpu op a="x" L"y"
#pragma knpu op a="\q"
#pragma knpu op a="\x"
#pragma knpu op a="\x100"
#pragma knpu op a="\777"
}

/* the line of a continued #pragma, and of the next one */
void continued(void)
{
#pragma knpu op a=1 \
	b=2 \
	c=0777
	g = 1;
#pragma knpu op d=nosuch
}
