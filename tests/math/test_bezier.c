/*
 * test_bezier.c - Tests for include/math/bezier.c (full glmc_bezier/hermite/decasteljau coverage)
 */

#include <math.h>
#include <stdio.h>

#include <math/bezier.h>

#include "check.h"

// === Helpers ===

#define _FTOL 1e-6

int main(void) {
    printf("=== bezier module tests ===\n");

    // Cubic control values: p0=1, c0=2, c1=3, p1=4.
    // Cubic Bezier midpoint at s=0.5 = 0.125*1 + 0.75*2 + 0.375*3 + 0.125*4 = 2.5.
    printf("--- bezier ---\n");

    _check_f("bezier s=0 -> p0", math_bezier_bezier(0.0, 1.0, 2.0, 3.0, 4.0), 1.0, _FTOL);
    _check_f("bezier s=1 -> p1", math_bezier_bezier(1.0, 1.0, 2.0, 3.0, 4.0), 4.0, _FTOL);
    _check_f("bezier s=0.5 mid", math_bezier_bezier(0.5, 1.0, 2.0, 3.0, 4.0), 2.5, _FTOL);

    // De Casteljau is the inverse: given a value prm on the curve it returns the
    // parameter t in [0, 1]. Controls 1,2,3,4 are collinear, so value = 1 + 3t;
    // prm=p0 -> t=0, prm=p1 -> t=1, prm=2.5 -> t=0.5.
    printf("--- decasteljau ---\n");

    _check_f("decasteljau prm=p0 -> 0", math_bezier_decasteljau(1.0, 1.0, 2.0, 3.0, 4.0), 0.0, _FTOL);
    _check_f("decasteljau prm=p1 -> 1", math_bezier_decasteljau(4.0, 1.0, 2.0, 3.0, 4.0), 1.0, _FTOL);
    _check_f("decasteljau prm=2.5 -> 0.5", math_bezier_decasteljau(2.5, 1.0, 2.0, 3.0, 4.0), 0.5, 1e-3);

    // Hermite with zero tangents reduces to h01(s) = -2s^3 + 3s^2 between p0 and p1.
    // p0=0, t0=0, t1=0, p1=1 -> s=0 gives 0, s=1 gives 1, s=0.5 gives 0.5.
    printf("--- hermite ---\n");

    _check_f("hermite s=0 -> p0", math_bezier_hermite(0.0, 0.0, 0.0, 0.0, 1.0), 0.0, _FTOL);
    _check_f("hermite s=1 -> p1", math_bezier_hermite(1.0, 0.0, 0.0, 0.0, 1.0), 1.0, _FTOL);
    _check_f("hermite s=0.5 mid", math_bezier_hermite(0.5, 0.0, 0.0, 0.0, 1.0), 0.5, _FTOL);

    return _check_finish();
}