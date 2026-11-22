/*
 * ref.c - lcc -A -A -Wf-pragma=knpu counts the objects that #pragma knpu
 * names as used, as #pragma ref does, but not names in constants: only
 * `unused' and `r3' are not referenced (ref.2bk).
 */
static int only_pragma[2];
static int unused[2];
static int ref_only[2];
static int r1[2], r2[2], r3[2], r4[2];

void f(void)
{
#pragma knpu op a=only_pragma
#pragma ref ref_only
#pragma ref r1, r2 "r3" 'r' 0x1 r4
}
