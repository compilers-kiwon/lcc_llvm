/* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception */

/* convw.c - convfix.c with constant tables of weights, some negative, and
   biases, smaller; the tables are static, so only the input is the
   caller's */

#define HW 8
#define IC 8
#define OC 4
signed char in[HW][IC], out[HW][OC];
static const signed char w[OC][IC] = {
	{ 1, -2, 3, -4, 5, -6, 7, -8 },
	{ -128, 127, 0, -1, 2, -3, 4, -5 },
	{ 9, -10, 11, -12, 13, -14, 15, -16 },
	{ 17, 18, -19, 20, -21, 22, -23, 24 }
};
static const int bias[OC] = { -300, 0, 70000, -1 };

void conv(void)
{
	int p, o, i;

	for (p = 0; p < HW; p++)
		for (o = 0; o < OC; o++) {
			int acc = bias[o];
			for (i = 0; i < IC; i++)
				acc += w[o][i] * in[p][i];
			if (acc < -128) acc = -128;
			if (acc > 127) acc = 127;
			out[p][o] = (signed char)acc;
		}
}
