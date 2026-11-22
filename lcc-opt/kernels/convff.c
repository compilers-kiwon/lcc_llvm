/* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception */

/* convff.c - convw.c in float: constant tables of weights and biases with
   -0.0, subnormal numbers, inexact values, terms that cancel, and a weight
   whose products with most inputs overflow */

#define HW 8
#define IC 8
#define OC 4
float in[HW][IC], out[HW][OC];
static const float w[OC][IC] = {
	{ -0.0f, 1e-45f, 1.5f, -2.25f, 0.1f, -0.3f, 0.7f, 3.0f },
	{ 3e38f, -1.75f, 0.5f, 0.125f, -1e-3f, 2.5f, -3.0f, 0.333f },
	{ 1e-38f, -6e-39f, 10.0f, -0.0f, 0.2f, 1e5f, -1e5f, 0.01f },
	{ -0.1f, 0.3f, -0.7f, 1.1f, -1.3f, 1.7f, -1.9f, 2.3f }
};
static const float bias[OC] = { -7.0f, 6.5f, -0.0f, 0.1f };

void conv(void)
{
	int p, o, i;

	for (p = 0; p < HW; p++)
		for (o = 0; o < OC; o++) {
			float acc = bias[o];
			for (i = 0; i < IC; i++)
				acc += w[o][i] * in[p][i];
			if (acc < -128) acc = -128;
			if (acc > 127) acc = 127;
			out[p][o] = acc;
		}
}
