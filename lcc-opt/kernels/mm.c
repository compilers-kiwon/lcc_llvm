/* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception */

/* mm.c - int matrix products: accumulating in memory, with a local sum,
   and in memory after setting the element to 0 */

#define M 16
#define N 8
#define K 12
int a[M][K], b[K][N], c[M][N], d[M][N], e[M][N];

void mm(void)
{
	int i, j, k;

	for (i = 0; i < M; i++)
		for (j = 0; j < N; j++)
			for (k = 0; k < K; k++)
				c[i][j] += a[i][k] * b[k][j];
}

void mm2(void)
{
	int i, j, k;

	for (i = 0; i < M; i++)
		for (j = 0; j < N; j++) {
			int s = 0;
			for (k = 0; k < K; k++)
				s += a[i][k] * b[k][j];
			d[i][j] = s;
		}
}

void mm3(void)
{
	int i, j, k;

	for (i = 0; i < M; i++)
		for (j = 0; j < N; j++) {
			e[i][j] = 0;
			for (k = 0; k < K; k++)
				e[i][j] += a[i][k] * b[k][j];
		}
}
