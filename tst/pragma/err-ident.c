/*
 * err-ident.c - names in #pragma knpu that are not of file-level
 * objects, with the messages of lcc -Wf-pragma=knpu in err-ident.2bk.
 * A name is looked up where the #pragma is, and macros are not expanded.
 */
#define IZP (-3)

int g[2];
int fn(void);
typedef int TT;
enum { EC = 3 };
struct tag { int m; };

void param(int prm)
{
#pragma knpu op a=prm
}

void local(void)
{
	int loc = 0;
#pragma knpu op a=loc
	g[0] = loc;
}

void hides(void)
{
	int g = 0;
#pragma knpu op a=g
	g++;
}

void staticlocal(void)
{
	static int sl;
#pragma knpu op a=sl
	sl++;
}

void others(void)
{
#pragma knpu op a=fn
#pragma knpu op b=TT
#pragma knpu op c=EC
#pragma knpu op d=tag
#pragma knpu op e=nosuch
#pragma knpu op f=IZP
#pragma knpu op h=L
}

void label(void)
{
	goto lab;
lab:	;
}

void labelname(void)
{
#pragma knpu op a=lab
}

void externfn(void)
{
	extern int fn2(void);
#pragma knpu op a=fn2
}
