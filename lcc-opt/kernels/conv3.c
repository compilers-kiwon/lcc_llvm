/* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception */

/* conv3.c - 3x3 convolutions of a 34x34 image into 32x32, accumulating in
   memory, and with a local sum */

#define H 34
#define W 34
int img[H][W], k3[3][3], res[H - 2][W - 2], res2[H - 2][W - 2];

void conv3(void)
{
	int p, q, r, s;

	for (p = 0; p < H - 2; p++)
		for (q = 0; q < W - 2; q++)
			for (r = 0; r < 3; r++)
				for (s = 0; s < 3; s++)
					res[p][q] += img[p + r][q + s] * k3[r][s];
}

void conv3b(void)
{
	int p, q, r, s;

	for (p = 0; p < H - 2; p++)
		for (q = 0; q < W - 2; q++) {
			int t = 0;
			for (r = 0; r < 3; r++)
				for (s = 0; s < 3; s++)
					t += img[p + r][q + s] * k3[r][s];
			res2[p][q] = t;
		}
}
