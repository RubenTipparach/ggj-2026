/*
 * Fixed-point trigonometry functions
 * Based on ps1-bare-metal by spicyjpeg
 */

#include "trig.h"

#define A (1 << 12)
#define B 19900
#define	C  3516

int isin(int x) {
	int c = x << (30 - ISIN_SHIFT);
	x    -= 1 << ISIN_SHIFT;

	x <<= 31 - ISIN_SHIFT;
	x >>= 31 - ISIN_SHIFT;
	x  *= x;
	x >>= 2 * ISIN_SHIFT - 14;

	int y = B - (x * C >> 14);
	y     = A - (x * y >> 16);

	return (c >= 0) ? y : (-y);
}

int isin2(int x) {
	int c = x << (30 - ISIN2_SHIFT);
	x    -= 1 << ISIN2_SHIFT;

	x <<= 31 - ISIN2_SHIFT;
	x >>= 31 - ISIN2_SHIFT;
	x  *= x;
	x >>= 2 * ISIN2_SHIFT - 14;

	int y = B - (x * C >> 14);
	y     = A - (x * y >> 16);

	return (c >= 0) ? y : (-y);
}

/*
 * Integer atan2 approximation
 * Returns angle in PS1 units: 0-4095 = 0-360 degrees
 *
 * Based on polynomial approximation of atan.
 * Octant-based algorithm for full 360-degree coverage.
 */
int iatan2(int y, int x) {
	/* Handle zero case */
	if (x == 0 && y == 0) return 0;

	/* Determine octant and make both values positive */
	int negX = (x < 0);
	int negY = (y < 0);
	if (negX) x = -x;
	if (negY) y = -y;

	/* Ensure x >= y for the atan approximation (octant 0) */
	int swapped = (y > x);
	if (swapped) {
		int t = x;
		x = y;
		y = t;
	}

	/* atan(y/x) approximation for |y/x| <= 1
	 * Using simple linear approximation: atan(r) ≈ r * (45 degrees)
	 * More accurate: atan(r) ≈ r * 0.97 / (1 + 0.28 * r^2)
	 *
	 * For PS1 units (1024 = 90 degrees, so 45 deg = 512):
	 * Simple: angle = (y * 512) / x
	 */
	int angle;
	if (x > 0) {
		/* Improved approximation using (y/x) * 651 - (y/x)^3 * 139 */
		/* This is a polynomial fit that's more accurate than linear */
		int ratio = (y << 12) / x;  /* Fixed-point ratio (4.12) */
		int ratio3 = (ratio * ratio >> 12) * ratio >> 12;

		/* atan(r) ≈ r * 651/1024 - r^3 * 139/1024 (in 1024 = 90deg units) */
		angle = (ratio * 651 - ratio3 * 139) >> 12;
	} else {
		angle = 0;
	}

	/* Map from octant 0 to correct octant */
	if (swapped) angle = 1024 - angle;  /* 90 deg - angle */
	if (negX) angle = 2048 - angle;     /* 180 deg - angle */
	if (negY) angle = -angle;           /* Negate for negative Y */

	/* Normalize to 0-4095 range */
	while (angle < 0) angle += 4096;
	while (angle >= 4096) angle -= 4096;

	return angle;
}

/*
 * Integer square root using Newton's method
 */
int isqrt(int x) {
	if (x <= 0) return 0;
	if (x == 1) return 1;

	/* Initial guess */
	int guess = x >> 1;
	int lastGuess;

	/* Newton's method iterations */
	do {
		lastGuess = guess;
		guess = (guess + x / guess) >> 1;
	} while (guess < lastGuess);

	return lastGuess;
}
