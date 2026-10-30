/*
 * Shift operators for int, unsigned, long and unsigned long, with
 * variable and constant counts.  The count is always an int in lcc's
 * trees, so for long operands it must be widened.  Compiled by lcc;
 * shift-drv.c checks the results.
 */
int shl_i(int a, int n) { return a << n; }
int shr_i(int a, int n) { return a >> n; }
unsigned shl_u(unsigned a, int n) { return a << n; }
unsigned shr_u(unsigned a, int n) { return a >> n; }
long shl_l(long a, int n) { return a << n; }
long shr_l(long a, int n) { return a >> n; }
unsigned long shl_ul(unsigned long a, int n) { return a << n; }
unsigned long shr_ul(unsigned long a, int n) { return a >> n; }

int c_i(int a) { return (a << 3) ^ (a >> 5); }
unsigned c_u(unsigned a) { return (a << 31) | (a >> 1); }
long c_l(long a) { return (a << 40) + (a >> 33); }
unsigned long c_ul(unsigned long a) { return (a << 17) ^ (a >> 47); }

/* counts computed at run time, and a shared count */
int mix_i(int a, int n) { return a << (n & 15) >> (n % 7); }
long mix_l(long a, int n) { int k; k = n & 31; return (a << k) - (a >> k); }
