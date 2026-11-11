/* Reads two positive integers and prints their greatest common divisor:
   echo "270 192" | lcc-hello prints "gcd 6". */
#include <stdio.h>

static long gcd(long a, long b) {
	while (b != 0) {
		long t = a % b;
		a = b;
		b = t;
	}
	return a;
}

int main(void) {
	long a, b;

	if (scanf("%ld %ld", &a, &b) != 2 || a <= 0 || b <= 0) {
		fprintf(stderr, "usage: echo a b | lcc-hello\n");
		return 1;
	}
	printf("gcd %ld\n", gcd(a, b));
	return 0;
}
