# 1 "tst/pragma/err-rcc.i"
int g;
void f(void)
{
#pragma knpu op a="x
#pragma knpu op /* cpp removes comments */ a=1
#pragma knpu op a=1 @
#pragma knpu op a="\
}
