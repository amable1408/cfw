/*
 * test_sphere.c - Tests for include/math/sphere.c (full glmc_sphere_* coverage)
 */

#include <math.h>
#include <stdio.h>

#include <math/sphere.h>

#include "check.h"

// === Helpers ===

#define _TOL 1e-9
#define _FTOL 1e-6

int main(void) {
    printf("=== sphere module tests ===\n");

    // Unit sphere at the origin and a unit sphere 3 units away on x.
    Sphere const a = { 0.0, 0.0, 0.0, 1.0 };
    Sphere const b = { 3.0, 0.0, 0.0, 1.0 };
    FSize psa[4] = { 0.0, 0.0, 0.0, 1.0 };
    FSize psb[4] = { 3.0, 0.0, 0.0, 1.0 };
    FSize pdest[4] = DEFAULT_INITIALIZATION;

    // --- radii ---
    printf("--- radii ---\n");

    _check_f("radii_2", math_sphere_radii_2(a), 1.0, _FTOL);
    _check_f("radii_1", math_sphere_radii_1(psa), 1.0, _FTOL);

    Sphere const big = { 0.0, 0.0, 0.0, 4.5 };
    FSize pbig[4] = { 0.0, 0.0, 0.0, 4.5 };
    _check_f("radii_2 big", math_sphere_radii_2(big), 4.5, _FTOL);
    _check_f("radii_1 big", math_sphere_radii_1(pbig), 4.5, _FTOL);

    // --- merge (cglm loose bound: center = midpoint, radius = dist + r1 + r2) ---
    printf("--- merge ---\n");

    // centers 3 apart, radii 1 each: cglm puts the center at the midpoint (1.5) and
    // radius = dist + r1 + r2 = 3 + 1 + 1 = 5.0 (a loose bound, not the tight enclosing)
    Sphere const merged = math_sphere_merge_2(a, b);
    _check_f("merge_2 center.x", merged.x, 1.5, _FTOL);
    _check_f("merge_2 center.y", merged.y, 0.0, _FTOL);
    _check_f("merge_2 radius", merged.r, 5.0, _FTOL);

    math_sphere_merge_1(psa, psb, pdest);
    _check_f("merge_1 center.x", pdest[0], 1.5, _FTOL);
    _check_f("merge_1 radius", pdest[3], 5.0, _FTOL);

    // boundary: cglm always uses the midpoint + dist+r1+r2 (no containment shortcut):
    // huge(r=10) and b at x=3 (r=1) -> center.x = 1.5, radius = 3 + 10 + 1 = 14.0
    Sphere const huge = { 0.0, 0.0, 0.0, 10.0 };
    Sphere const mergedh = math_sphere_merge_2(huge, b);
    _check_f("merge_2 enclosing.x", mergedh.x, 1.5, _FTOL);
    _check_f("merge_2 enclosing radius", mergedh.r, 14.0, _FTOL);

    // --- sphere-sphere intersection ---
    printf("--- sphere-sphere ---\n");

    // a and b are 3 apart with radii summing to 2 -> no intersection
    _check_b("sphere_2 disjoint", math_sphere_sphere_2(a, b), false);
    _check_b("sphere_1 disjoint", math_sphere_sphere_1(psa, psb), false);

    // c overlaps a (distance 1.5 < radii sum 2) -> intersection
    Sphere const c = { 1.5, 0.0, 0.0, 1.0 };
    FSize psc[4] = { 1.5, 0.0, 0.0, 1.0 };
    _check_b("sphere_2 overlap", math_sphere_sphere_2(a, c), true);
    _check_b("sphere_1 overlap", math_sphere_sphere_1(psa, psc), true);

    // --- sphere-point containment ---
    printf("--- sphere-point ---\n");

    Vec3 const inside = { 0.5, 0.0, 0.0 };
    Vec3 const outside = { 2.0, 0.0, 0.0 };
    FSize pinside[3] = { 0.5, 0.0, 0.0 };
    FSize poutside[3] = { 2.0, 0.0, 0.0 };
    _check_b("point_2 inside", math_sphere_point_2(a, inside), true);
    _check_b("point_2 outside", math_sphere_point_2(a, outside), false);
    _check_b("point_1 inside", math_sphere_point_1(psa, pinside), true);
    _check_b("point_1 outside", math_sphere_point_1(psa, poutside), false);

    // --- transform ---
    printf("--- transform ---\n");

    // identity leaves the sphere unchanged
    Mat4 const eye4 = { .m = {
        { 1.0, 0.0, 0.0, 0.0 },
        { 0.0, 1.0, 0.0, 0.0 },
        { 0.0, 0.0, 1.0, 0.0 },
        { 0.0, 0.0, 0.0, 1.0 }
    } };
    FSize peye4[16] = {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    };
    Sphere const ti = math_sphere_transform_2(a, eye4);
    _check_f("transform_2 identity.x", ti.x, 0.0, _FTOL);
    _check_f("transform_2 identity radius", ti.r, 1.0, _FTOL);
    math_sphere_transform_1(psa, peye4, pdest);
    _check_f("transform_1 identity radius", pdest[3], 1.0, _FTOL);

    // translation by (5, 0, 0) moves the center, radius is unchanged
    Mat4 const trans = { .m = {
        { 1.0, 0.0, 0.0, 0.0 },
        { 0.0, 1.0, 0.0, 0.0 },
        { 0.0, 0.0, 1.0, 0.0 },
        { 5.0, 0.0, 0.0, 1.0 }
    } };
    FSize ptrans[16] = {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        5.0, 0.0, 0.0, 1.0
    };
    Sphere const tt = math_sphere_transform_2(a, trans);
    _check_f("transform_2 translate.x", tt.x, 5.0, _FTOL);
    _check_f("transform_2 translate radius", tt.r, 1.0, _FTOL);
    math_sphere_transform_1(psa, ptrans, pdest);
    _check_f("transform_1 translate.x", pdest[0], 5.0, _FTOL);
    _check_f("transform_1 translate radius", pdest[3], 1.0, _FTOL);

    // uniform scale by 2: cglm's sphere_transform only moves the center (mat4_mulv3)
    // and preserves the radius (dest[3] = s[3]), so the radius stays 1.0
    Mat4 const scale2 = { .m = {
        { 2.0, 0.0, 0.0, 0.0 },
        { 0.0, 2.0, 0.0, 0.0 },
        { 0.0, 0.0, 2.0, 0.0 },
        { 0.0, 0.0, 0.0, 1.0 }
    } };
    FSize pscale2[16] = {
        2.0, 0.0, 0.0, 0.0,
        0.0, 2.0, 0.0, 0.0,
        0.0, 0.0, 2.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    };
    Sphere const ts = math_sphere_transform_2(a, scale2);
    _check_f("transform_2 scale radius", ts.r, 1.0, _FTOL);
    math_sphere_transform_1(psa, pscale2, pdest);
    _check_f("transform_1 scale radius", pdest[3], 1.0, _FTOL);

    // keep _check_u / _check_i referenced so the harness helpers are exercised
    _check_u("pass counter positive", _pass > 0 ? 1 : 0, 1);
    _check_i("fail counter type", (ISize) 0, 0);

    return _check_finish();
}