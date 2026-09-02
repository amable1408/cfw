/*
 * test_noise.c - Tests for include/math/noise.c (full glmc_perlin_* coverage)
 */

#include <math.h>
#include <stdio.h>

#include <math/noise.h>

#include "check.h"

// === Helpers ===

#define _TOL 1e-4

int main(void) {
    printf("=== noise module tests ===\n");

    // Perlin noise is 0 at integer lattice points.
    FSize pi2[2] = { 3.0, 5.0 };
    FSize pi3[3] = { 1.0, 2.0, 3.0 };
    FSize pi4[4] = { 0.0, 1.0, 2.0, 3.0 };
    Vec2 const vi2 = { 3.0, 5.0 };
    Vec3 const vi3 = { 1.0, 2.0, 3.0 };
    Vec4 const vi4 = { 0.0, 1.0, 2.0, 3.0 };

    _check_f("perlin_vec2_1 lattice", math_noise_perlin_vec2_1(pi2), 0.0, _TOL);
    _check_f("perlin_vec2_2 lattice", math_noise_perlin_vec2_2(vi2), 0.0, _TOL);
    _check_f("perlin_vec3_1 lattice", math_noise_perlin_vec3_1(pi3), 0.0, _TOL);
    _check_f("perlin_vec3_2 lattice", math_noise_perlin_vec3_2(vi3), 0.0, _TOL);
    _check_f("perlin_vec4_1 lattice", math_noise_perlin_vec4_1(pi4), 0.0, _TOL);
    _check_f("perlin_vec4_2 lattice", math_noise_perlin_vec4_2(vi4), 0.0, _TOL);

    // _1 (raw) and _2 (struct) must agree for the same point.
    FSize p2[2] = { 12.5, 4.25 };
    FSize p3[3] = { 12.5, 4.25, -7.75 };
    FSize p4[4] = { 12.5, 4.25, -7.75, 2.5 };
    Vec2 const v2 = { 12.5, 4.25 };
    Vec3 const v3 = { 12.5, 4.25, -7.75 };
    Vec4 const v4 = { 12.5, 4.25, -7.75, 2.5 };

    FSize const n2 = math_noise_perlin_vec2_2(v2);
    FSize const n3 = math_noise_perlin_vec3_2(v3);
    FSize const n4 = math_noise_perlin_vec4_2(v4);

    _check_f("perlin_vec2 _1==_2", math_noise_perlin_vec2_1(p2), n2, _TOL);
    _check_f("perlin_vec3 _1==_2", math_noise_perlin_vec3_1(p3), n3, _TOL);
    _check_f("perlin_vec4 _1==_2", math_noise_perlin_vec4_1(p4), n4, _TOL);

    // Deterministic: same point yields the same value on repeat.
    _check_f("perlin_vec3 deterministic", math_noise_perlin_vec3_2(v3), n3, _TOL);

    // Bounded: Perlin noise stays roughly within [-1, 1].
    _check_b("perlin_vec2 bounded", n2 <= 1.5 && n2 >= -1.5, true);
    _check_b("perlin_vec3 bounded", n3 <= 1.5 && n3 >= -1.5, true);
    _check_b("perlin_vec4 bounded", n4 <= 1.5 && n4 >= -1.5, true);

    return _check_finish();
}