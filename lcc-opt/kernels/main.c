/* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception */

/* main.c - run one kernel of this directory on inputs from an LCG, for
   four seeds, and print a checksum of each array it writes.  Compile it
   with one of -DCONVFIX, -DCONVW, -DCONVFF, -DMM and -DCONV3 and link it
   with that kernel.  check.sh compares what it prints when the kernel is
   compiled by gcc with what it prints when the kernel goes through lcc and
   lcc-opt.  With -v it also prints every element.

   The seeds also choose the range of the inputs: for convfix and convw
   most sums are clamped with the first and few with the last; for convff
   the inputs are large, small, very small and integers.  Each array the
   kernel writes is filled with 0x5a first, and the sums of mm and conv3
   stay far from overflowing int. */

#include <stdio.h>
#include <string.h>

static unsigned long seed;
static int verbose;

/* the next number of a 32-bit LCG, in [lo, hi] */
static long rnd(long lo, long hi)
{
	seed = (seed * 1103515245UL + 12345UL) & 0xffffffffUL;
	return lo + (long)((seed >> 8) % (unsigned long)(hi - lo + 1));
}

/* print the FNV-1a hash of the n bytes at p, and with -v the n / size
   elements as type: 'c' signed char, 'i' int, 'f' float */
static void sum(int s, const char *name, const void *p, size_t n, size_t size,
	int type)
{
	const unsigned char *b = p;
	unsigned long h = 2166136261UL;
	size_t k, j;

	for (k = 0; k < n; k++)
		h = ((h ^ b[k]) * 16777619UL) & 0xffffffffUL;
	printf("seed %d %s %08lx\n", s, name, h);
	if (!verbose)
		return;
	for (k = 0; k < n / size; k++) {
		printf("%s[%lu]", name, (unsigned long)k);
		if (type == 'c')
			printf(" %d", ((const signed char *)p)[k]);
		else if (type == 'i')
			printf(" %d", ((const int *)p)[k]);
		else
			printf(" %.9g", ((const float *)p)[k]);
		printf(" bytes");
		for (j = 0; j < size; j++)
			printf(" %02x", b[k * size + j]);
		printf("\n");
	}
}

#if defined(CONVFIX)

#define HW 64
#define IC 16
#define OC 32
extern signed char in[HW][IC], w[OC][IC], out[HW][OC];
extern int bias[OC];
extern void conv(void);

static void run(int s)
{
	static long amp[] = { 128, 9, 3, 1 }, bamp[] = { 300000, 400, 120, 2 };
	long a = amp[s - 1], ab = bamp[s - 1];
	int p, o, i;

	for (p = 0; p < HW; p++)
		for (i = 0; i < IC; i++)
			in[p][i] = (signed char)rnd(-a, a - 1);
	for (o = 0; o < OC; o++)
		for (i = 0; i < IC; i++)
			w[o][i] = (signed char)rnd(-a, a - 1);
	for (o = 0; o < OC; o++)
		bias[o] = (int)rnd(-ab, ab);
	memset(out, 0x5a, sizeof out);
	conv();
	sum(s, "out", out, sizeof out, sizeof out[0][0], 'c');
}

#elif defined(CONVW)

#define HW 8
#define IC 8
#define OC 4
extern signed char in[HW][IC], out[HW][OC];
extern void conv(void);

static void run(int s)
{
	static long amp[] = { 128, 9, 3, 1 };
	long a = amp[s - 1];
	int p, i;

	for (p = 0; p < HW; p++)
		for (i = 0; i < IC; i++)
			in[p][i] = (signed char)rnd(-a, a - 1);
	memset(out, 0x5a, sizeof out);
	conv();
	sum(s, "out", out, sizeof out, sizeof out[0][0], 'c');
}

#elif defined(CONVFF)

#define HW 8
#define IC 8
#define OC 4
extern float in[HW][IC], out[HW][OC];
extern void conv(void);

static void run(int s)
{
	static float scale[] = { 64.0f, 4096.0f, 1e6f, 1.0f };
	float d = scale[s - 1];
	int p, i;

	for (p = 0; p < HW; p++)
		for (i = 0; i < IC; i++)
			in[p][i] = (float)rnd(-1000, 1000) / d;
	memset(out, 0x5a, sizeof out);
	conv();
	sum(s, "out", out, sizeof out, sizeof out[0][0], 'f');
}

#elif defined(MM)

#define M 16
#define N 8
#define K 12
extern int a[M][K], b[K][N], c[M][N], d[M][N], e[M][N];
extern void mm(void), mm2(void), mm3(void);

static void run(int s)
{
	static long amp[] = { 4096, 100, 3, 1 };
	long x = amp[s - 1];
	int i, j, k;

	for (i = 0; i < M; i++)
		for (k = 0; k < K; k++)
			a[i][k] = (int)rnd(-x, x - 1);
	for (k = 0; k < K; k++)
		for (j = 0; j < N; j++)
			b[k][j] = (int)rnd(-x, x - 1);
	for (i = 0; i < M; i++)
		for (j = 0; j < N; j++)
			c[i][j] = (int)rnd(-268435456L, 268435456L);
	memset(d, 0x5a, sizeof d);
	memset(e, 0x5a, sizeof e);
	mm();
	mm2();
	mm3();
	sum(s, "c", c, sizeof c, sizeof c[0][0], 'i');
	sum(s, "d", d, sizeof d, sizeof d[0][0], 'i');
	sum(s, "e", e, sizeof e, sizeof e[0][0], 'i');
}

#elif defined(CONV3)

#define H 34
#define W 34
extern int img[H][W], k3[3][3], res[H - 2][W - 2], res2[H - 2][W - 2];
extern void conv3(void), conv3b(void);

static void run(int s)
{
	static long amp[] = { 4096, 100, 3, 1 };
	long x = amp[s - 1];
	int p, q;

	for (p = 0; p < H; p++)
		for (q = 0; q < W; q++)
			img[p][q] = (int)rnd(-x, x - 1);
	for (p = 0; p < 3; p++)
		for (q = 0; q < 3; q++)
			k3[p][q] = (int)rnd(-x, x - 1);
	for (p = 0; p < H - 2; p++)
		for (q = 0; q < W - 2; q++)
			res[p][q] = (int)rnd(-1048576L, 1048576L);
	memset(res2, 0x5a, sizeof res2);
	conv3();
	conv3b();
	sum(s, "res", res, sizeof res, sizeof res[0][0], 'i');
	sum(s, "res2", res2, sizeof res2, sizeof res2[0][0], 'i');
}

#else
#error "compile with -DCONVFIX, -DCONVW, -DCONVFF, -DMM or -DCONV3"
#endif

int main(int argc, char **argv)
{
	int s;

	verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
	for (s = 1; s <= 4; s++) {
		seed = (unsigned long)s;
		run(s);
	}
	return 0;
}
