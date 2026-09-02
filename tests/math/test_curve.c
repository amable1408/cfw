/*
 * test_curve.c - Tests for include/math/curve.c (full glmc_smc coverage)
 */

#include <math.h>
#include <stdio.h>

#include <math/curve.h>

#include "check.h"

// === Helpers ===

#define _FTOL 1e-5

int main(void) {
    printf("=== curve module tests ===\n");

    // smc(s, m, c) = cubic(s) . (m . c), where cubic(s) = (s^3, s^2, s, 1).
    // With m = identity: smc = dot(cubic(s), c).

    // Identity basis, both raw (column-major, 16 FSize) and struct forms.
    Mat4 const identity = {
        {
            { 1.0, 0.0, 0.0, 0.0 },
            { 0.0, 1.0, 0.0, 0.0 },
            { 0.0, 0.0, 1.0, 0.0 },
            { 0.0, 0.0, 0.0, 1.0 },
        }
    };
    FSize pidentity[16] = {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0,
    };

    // --- identity basis: smc collapses to dot(cubic(s), c) ---
    printf("--- identity basis ---\n");

    // s = 1 -> cubic = (1,1,1,1); c = (1,2,3,4); dot = 1+2+3+4 = 10
    Vec4 const c1 = { 1.0, 2.0, 3.0, 4.0 };
    FSize pc1[4] = { 1.0, 2.0, 3.0, 4.0 };
    _check_f("smc_2 s=1 identity", math_curve_smc_2(1.0, identity, c1), 10.0, _FTOL);
    _check_f("smc_1 s=1 identity", math_curve_smc_1(1.0, pidentity, pc1), 10.0, _FTOL);

    // s = 2 -> cubic = (8,4,2,1); c = (1,0,0,0); dot = 8
    Vec4 const c2 = { 1.0, 0.0, 0.0, 0.0 };
    FSize pc2[4] = { 1.0, 0.0, 0.0, 0.0 };
    _check_f("smc_2 s=2 identity", math_curve_smc_2(2.0, identity, c2), 8.0, _FTOL);
    _check_f("smc_1 s=2 identity", math_curve_smc_1(2.0, pidentity, pc2), 8.0, _FTOL);

    // s = 0 -> cubic = (0,0,0,1); c = (1,2,3,4); dot = c.w = 4
    _check_f("smc_2 s=0 identity", math_curve_smc_2(0.0, identity, c1), 4.0, _FTOL);
    _check_f("smc_1 s=0 identity", math_curve_smc_1(0.0, pidentity, pc1), 4.0, _FTOL);

    // --- non-identity basis: verify the matrix multiply participates ---
    printf("--- scaled basis ---\n");

    // m = 2 * identity -> smc = 2 * dot(cubic(s), c). s=1, c=(1,2,3,4) -> 20
    Mat4 const twice = {
        {
            { 2.0, 0.0, 0.0, 0.0 },
            { 0.0, 2.0, 0.0, 0.0 },
            { 0.0, 0.0, 2.0, 0.0 },
            { 0.0, 0.0, 0.0, 2.0 },
        }
    };
    FSize ptwice[16] = {
        2.0, 0.0, 0.0, 0.0,
        0.0, 2.0, 0.0, 0.0,
        0.0, 0.0, 2.0, 0.0,
        0.0, 0.0, 0.0, 2.0,
    };
    _check_f("smc_2 scaled basis", math_curve_smc_2(1.0, twice, c1), 20.0, _FTOL);
    _check_f("smc_1 scaled basis", math_curve_smc_1(1.0, ptwice, pc1), 20.0, _FTOL);

    // keep _check_u / _check_i referenced so the harness helpers are exercised
    _check_u("pass counter positive", _pass > 0 ? 1 : 0, 1);
    _check_i("fail counter type", (ISize) 0, 0);

    return _check_finish();
}