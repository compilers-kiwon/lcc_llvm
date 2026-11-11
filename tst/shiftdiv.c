/*
 * Division and remainder by a shift, and a shift by a shift.  On x86 the
 * count of a variable shift is in %ecx, and lcc's x86/linux back end
 * put the divisor in %ecx too, so x / (1 << n) asked for the shift's
 * result and its count in the same register.
 */
#include <stdio.h>

int quo(int x, int n) { return x / (1 << n); }
int rem(int x, int n) { return x % (1 << n); }
unsigned uquo(unsigned x, int n) { return x / (1u << n); }
unsigned urem(unsigned x, int n) { return x % (1u << n); }
int shsh(int x, int a, int n) { return x << (a << n); }

int main(void) {
	int n;

	for (n = 0; n < 6; n++)
		printf("%d %d %u %u %d\n", quo(1000 + n, n), rem(1000 + n, n),
			uquo(4000000000u - n, n), urem(4000000000u - n, n),
			shsh(3, 1, n % 3));
	return 0;
}
