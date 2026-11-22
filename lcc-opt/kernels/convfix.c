/* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception */

/* convfix.c - a 1x1 int8 convolution on fixed size global arrays, the
   kernel of lcc-opt's completion criteria: lcc-to-affine raises it to
   three affine.for, lcc-to-linalg to a linalg.broadcast of the bias, a
   linalg.matmul and an element-wise linalg.generic for the clamp */

#define HW 64
#define IC 16
#define OC 32
signed char in[HW][IC], w[OC][IC], out[HW][OC];
int bias[OC];

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
