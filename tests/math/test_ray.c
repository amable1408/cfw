/*
 * test_ray.c - Tests for include/math/ray.c (full glmc_ray_* coverage)
 */

#include <math.h>
#include <stdio.h>

#include <math/ray.h>

#include "check.h"

// === Helpers ===

#define _TOL 1e-9
#define _FTOL 1e-6

int main(void) {
    printf("=== ray module tests ===\n");

    FSize po[3] = DEFAULT_INITIALIZATION;

    // --- at: point = origin + t * direction ---
    printf("--- at ---\n");

    // ray from (1,2,3) along +y at t=2 -> (1,4,3)
    Ray const ray_y = { .origin = { 1.0, 2.0, 3.0 }, .direction = { 0.0, 1.0, 0.0 } };
    Vec3 const at2 = math_ray_at_2(ray_y, 2.0);
    _check_f("at_2.x", at2.x, 1.0, _FTOL);
    _check_f("at_2.y", at2.y, 4.0, _FTOL);
    _check_f("at_2.z", at2.z, 3.0, _FTOL);

    // raw ray from origin along +x at t=5 -> (5,0,0)
    FSize porigin[3] = { 0.0, 0.0, 0.0 };
    FSize pdirx[3] = { 1.0, 0.0, 0.0 };
    math_ray_at_1(porigin, pdirx, 5.0, po);
    _check_f("at_1.x", po[0], 5.0, _FTOL);
    _check_f("at_1.y", po[1], 0.0, _FTOL);

    // --- sphere: hit reports both distances, miss returns false ---
    printf("--- sphere ---\n");

    // ray from origin along +z; sphere centered (0,0,5) radius 1 -> hit at t1=4, t2=6
    Ray const ray_z = { .origin = { 0.0, 0.0, 0.0 }, .direction = { 0.0, 0.0, 1.0 } };
    Sphere const sph = { 0.0, 0.0, 5.0, 1.0 };
    RaySphereHit const hit_s2 = math_ray_sphere_2(ray_z, sph);
    _check_b("sphere_2 hit", hit_s2.hit, true);
    _check_f("sphere_2 t1", hit_s2.t1, 4.0, _FTOL);
    _check_f("sphere_2 t2", hit_s2.t2, 6.0, _FTOL);

    // raw hit: same geometry
    FSize pdirz[3] = { 0.0, 0.0, 1.0 };
    FSize psphere[4] = { 0.0, 0.0, 5.0, 1.0 };
    FSize rt1 = DEFAULT_INITIALIZATION;
    FSize rt2 = DEFAULT_INITIALIZATION;
    bool const hit_s1 = math_ray_sphere_1(porigin, pdirz, psphere, &rt1, &rt2);
    _check_b("sphere_1 hit", hit_s1, true);
    _check_f("sphere_1 t1", rt1, 4.0, _FTOL);
    _check_f("sphere_1 t2", rt2, 6.0, _FTOL);

    // miss: ray along +y never reaches the sphere ahead of it
    Ray const ray_miss = { .origin = { 0.0, 0.0, 0.0 }, .direction = { 0.0, 1.0, 0.0 } };
    RaySphereHit const miss_s2 = math_ray_sphere_2(ray_miss, sph);
    _check_b("sphere_2 miss", miss_s2.hit, false);
    // The miss distances are the wrapper's documented 0, not cglm's (it leaves them untouched).
    _check_f("sphere_2 miss t1 is 0", miss_s2.t1, 0.0, 0.0);
    _check_f("sphere_2 miss t2 is 0", miss_s2.t2, 0.0, 0.0);
    // cglm's other miss branch: both roots real but behind the origin - it writes them negative
    // before returning false, and the wrapper still reports the contract's 0.
    Sphere const behind = { 0.0, 0.0, -5.0, 1.0 };
    RaySphereHit const miss_behind = math_ray_sphere_2(ray_z, behind);
    _check_b("sphere_2 behind miss", miss_behind.hit, false);
    _check_f("sphere_2 behind t1 is 0", miss_behind.t1, 0.0, 0.0);
    FSize pbehind[4] = { 0.0, 0.0, -5.0, 1.0 };
    FSize bt1 = 9.0;
    FSize bt2 = 9.0;
    _check_b("sphere_1 behind miss", math_ray_sphere_1(porigin, pdirz, pbehind, &bt1, &bt2), false);
    _check_f("sphere_1 behind t2 is 0", bt2, 0.0, 0.0);
    FSize pdiry[3] = { 0.0, 1.0, 0.0 };
    bool const miss_s1 = math_ray_sphere_1(porigin, pdiry, psphere, &rt1, &rt2);
    _check_b("sphere_1 miss", miss_s1, false);
    _check_f("sphere_1 miss t1 is 0", rt1, 0.0, 0.0);

    // --- triangle: hit reports distance, miss returns false ---
    printf("--- triangle ---\n");

    // triangle in the z=2 plane enclosing the +z axis; ray from origin along +z hits at d=2
    Vec3 const v0 = { -1.0, -1.0, 2.0 };
    Vec3 const v1 = { 1.0, -1.0, 2.0 };
    Vec3 const v2 = { 0.0, 2.0, 2.0 };
    RayTriangleHit const hit_t2 = math_ray_triangle_2(ray_z, v0, v1, v2);
    _check_b("triangle_2 hit", hit_t2.hit, true);
    _check_f("triangle_2 d", hit_t2.d, 2.0, _FTOL);

    // raw hit: same geometry
    FSize pv0[3] = { -1.0, -1.0, 2.0 };
    FSize pv1[3] = { 1.0, -1.0, 2.0 };
    FSize pv2[3] = { 0.0, 2.0, 2.0 };
    FSize rd = DEFAULT_INITIALIZATION;
    bool const hit_t1 = math_ray_triangle_1(porigin, pdirz, pv0, pv1, pv2, &rd);
    _check_b("triangle_1 hit", hit_t1, true);
    _check_f("triangle_1 d", rd, 2.0, _FTOL);

    // miss: ray pointing away from the triangle (-z) never intersects ahead
    Ray const ray_back = { .origin = { 0.0, 0.0, 0.0 }, .direction = { 0.0, 0.0, -1.0 } };
    RayTriangleHit const miss_t2 = math_ray_triangle_2(ray_back, v0, v1, v2);
    _check_b("triangle_2 miss", miss_t2.hit, false);
    // d is the wrapper's 0 on every miss, like the sphere - pinned on both forms.
    _check_f("triangle_2 miss d is 0", miss_t2.d, 0.0, 0.0);
    FSize pdirneg[3] = { 0.0, 0.0, -1.0 };
    bool const miss_t1 = math_ray_triangle_1(porigin, pdirneg, pv0, pv1, pv2, &rd);
    _check_b("triangle_1 miss", miss_t1, false);
    _check_f("triangle_1 miss d is 0", rd, 0.0, 0.0);

    // keep _check_u / _check_i referenced so the harness helpers are exercised
    _check_u("pass counter positive", _pass > 0 ? 1 : 0, 1);
    _check_i("fail counter type", (ISize) 0, 0);

    return _check_finish();
}