/*
 * err-place.c - #pragma knpu where lcc -Wf-pragma=knpu -Wf-pragma=npu2
 * does not take it, with the messages in err-place.2bk: outside function
 * bodies, and in the outermost block of a function only between its
 * declarations and statements, once for each name space.  The lexer
 * reads a #pragma line with the token after it, or when getchr looks
 * past the token before it, as for a label or adjacent strings.
 */
typedef int T;
int g, h;

void before_brace(void)
#pragma knpu before_brace x=g
{
	g = 1;
}
#pragma knpu after_brace x=g

int k;
#pragma knpu between_functions x=g

void nested(void)
{
	int i;

	{
#pragma knpu nested_first x=g
		g = 2;
#pragma knpu nested_last x=g
	}
#pragma knpu after_nested x=g
	for (i = 0; i < 2; i++)
#pragma knpu for_body x=g
		g++;
}

void heads(void)
{
	int i;

	for (i = 0; i < 2; i++)
#pragma knpu for_brace x=g
	{
		g++;
	}
	while (g < 10)
#pragma knpu while_body x=g
		g++;
	do
#pragma knpu do_body x=g
		g--;
	while (g > 5);
	if (g)
#pragma knpu if_body x=g
		g = 1;
	if (g)
		g = 2;
#pragma knpu before_else x=g
	else
		g = 3;
	if (g)
		g = 4;
	else
#pragma knpu else_body x=g
		g = 5;
	switch (g)
#pragma knpu switch_body x=g
	{
	case 1:
		g = 4;
	}
}

void middles(void)
{
	int a = 1 +
#pragma knpu mid_initializer x=g
	    2;

L:
#pragma knpu after_label x=g
	g = 5;
	g = g +
#pragma knpu mid_expression x=g
	    a;
	h = 1,
#pragma knpu mid_comma x=g
	g = 2;
	if (h-- > 0)
		goto L;
	return
#pragma knpu mid_return x=g
	;
}

void peeks(void)
{
	T
#pragma knpu typedef_label x=g
	: h = "abc"
#pragma knpu between_strings x=g
	"def"[1];
}

void twice(void)
{
#pragma knpu first x=g
	g = 1;
#pragma knpu second x=g
#pragma npu2 other x=g
#pragma npu2 again x=g
}

void shadow(void)
{
	{
		int g = 0;
#pragma knpu hidden x=g
		h = g;
	}
#pragma knpu after_hidden x=g
}

void last(void) { g = 6; }
#pragma knpu end_of_file x=g
