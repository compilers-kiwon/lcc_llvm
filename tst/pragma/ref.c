/*
 * ref.c - lcc -A -A -Wf-pragma=knpu counts the objects that #pragma knpu
 * names as used, as #pragma ref does: only `unused' is not referenced
 * (ref.2bk).
 */
static int only_pragma[2];
static int unused[2];
static int ref_only[2];

void f(void)
{
#pragma knpu op a=only_pragma
#pragma ref ref_only
}
