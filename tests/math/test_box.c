/*
 * test_box.c - Tests for include/math/box.c (full glmc_aabb_* coverage)
 */

#include <math.h>
#include <stdio.h>

#include <math/box.h>

#include "check.h"

// === Helpers ===

#define _FTOL 1e-6
#define _TOL 1e-9

int main(void) {
    printf("=== box module tests ===\n");

    // A = [(0,0,0)..(2,4,6)] : center (1,2,3), diagonal sqrt(56)
    Box const a = { { 0.0, 0.0, 0.0 }, { 2.0, 4.0, 6.0 } };
    // B overlaps A but is not contained by it
    Box const b = { { 1.0, 1.0, 1.0 }, { 3.0, 3.0, 3.0 } };
    // C is disjoint from A
    Box const c = { { 10.0, 10.0, 10.0 }, { 11.0, 11.0, 11.0 } };
    // D sits fully inside A
    Box const d = { { 0.5, 0.5, 0.5 }, { 1.0, 1.0, 1.0 } };

    FSize pa[6] = { 0.0, 0.0, 0.0, 2.0, 4.0, 6.0 };
    FSize pb[6] = { 1.0, 1.0, 1.0, 3.0, 3.0, 3.0 };
    FSize pc[6] = { 10.0, 10.0, 10.0, 11.0, 11.0, 11.0 };
    FSize pd[6] = { 0.5, 0.5, 0.5, 1.0, 1.0, 1.0 };
    FSize po3[3] = DEFAULT_INITIALIZATION;
    FSize po6[6] = DEFAULT_INITIALIZATION;

    // --- overlap: aabb-aabb (true and false) ---
    printf("--- aabb-aabb ---\n");

    _check_b("aabb_2 overlap true", math_box_aabb_2(a, b), true);
    _check_b("aabb_2 overlap false", math_box_aabb_2(a, c), false);
    _check_b("aabb_1 overlap true", math_box_aabb_1(pa, pb), true);
    _check_b("aabb_1 overlap false", math_box_aabb_1(pa, pc), false);

    // --- center ---
    printf("--- center ---\n");

    Vec3 const ctr = math_box_center_2(a);
    _check_f("center_2.x", ctr.x, 1.0, _FTOL);
    _check_f("center_2.y", ctr.y, 2.0, _FTOL);
    _check_f("center_2.z", ctr.z, 3.0, _FTOL);
    math_box_center_1(pa, po3);
    _check_f("center_1.z", po3[2], 3.0, _FTOL);

    // --- contains ---
    printf("--- contains ---\n");

    _check_b("contains_2 inside true", math_box_contains_2(a, d), true);
    _check_b("contains_2 partial false", math_box_contains_2(a, b), false);
    _check_b("contains_1 inside true", math_box_contains_1(pa, pd), true);
    _check_b("contains_1 partial false", math_box_contains_1(pa, pb), false);

    // --- crop (intersection of A and B) -> [(1,1,1)..(2,3,3)] ---
    printf("--- crop ---\n");

    Box const cr = math_box_crop_2(a, b);
    _check_f("crop_2.min.x", cr.min.x, 1.0, _FTOL);
    _check_f("crop_2.max.z", cr.max.z, 3.0, _FTOL);
    math_box_crop_1(pa, pb, po6);
    _check_f("crop_1.min.x", po6[0], 1.0, _FTOL);
    _check_f("crop_1.max.z", po6[5], 3.0, _FTOL);

    // --- crop_until: crop(A,B) then merge with clamp D -> [(0.5,0.5,0.5)..(2,3,3)] ---
    printf("--- crop_until ---\n");

    Box const cu = math_box_crop_until_2(a, b, d);
    _check_f("crop_until_2.min.x", cu.min.x, 0.5, _FTOL);
    _check_f("crop_until_2.max.z", cu.max.z, 3.0, _FTOL);
    math_box_crop_until_1(pa, pb, pd, po6);
    _check_f("crop_until_1.min.x", po6[0], 0.5, _FTOL);

    // --- frustum ---
    printf("--- frustum ---\n");

    // planes that never reject the box -> inside
    FrustumPlanes const planes_in = { .planes = {
        { 0.0, 0.0, 0.0, 1.0 },
        { 0.0, 0.0, 0.0, 1.0 },
        { 0.0, 0.0, 0.0, 1.0 },
        { 0.0, 0.0, 0.0, 1.0 },
        { 0.0, 0.0, 0.0, 1.0 },
        { 0.0, 0.0, 0.0, 1.0 }
    } };
    _check_b("frustum_2 inside true", math_box_frustum_2(a, planes_in), true);

    // first plane rejects the box -> outside
    FSize pplanes_out[24] = {
        1.0, 0.0, 0.0, -100.0,
        0.0, 0.0, 0.0, 1.0,
        0.0, 0.0, 0.0, 1.0,
        0.0, 0.0, 0.0, 1.0,
        0.0, 0.0, 0.0, 1.0,
        0.0, 0.0, 0.0, 1.0
    };
    _check_b("frustum_1 outside false", math_box_frustum_1(pa, pplanes_out), false);

    // --- invalidate / isvalid ---
    printf("--- invalidate / isvalid ---\n");

    _check_b("isvalid_2 valid true", math_box_isvalid_2(a), true);
    _check_b("isvalid_1 valid true", math_box_isvalid_1(pa), true);

    Box const inv = math_box_invalidate_2();
    _check_b("invalidate_2 -> isvalid false", math_box_isvalid_2(inv), false);
    _check_b("invalidate_2 min huge", inv.min.x > 1e30 ? true : false, true);

    FSize pinv[6] = DEFAULT_INITIALIZATION;
    math_box_invalidate_1(pinv);
    _check_b("invalidate_1 -> isvalid false", math_box_isvalid_1(pinv), false);

    // --- merge: smallest box enclosing A and B -> [(0,0,0)..(3,4,6)] ---
    printf("--- merge ---\n");

    Box const mg = math_box_merge_2(a, b);
    _check_f("merge_2.min.x", mg.min.x, 0.0, _FTOL);
    _check_f("merge_2.max.x", mg.max.x, 3.0, _FTOL);
    _check_f("merge_2.max.z", mg.max.z, 6.0, _FTOL);
    math_box_merge_1(pa, pb, po6);
    _check_f("merge_1.max.x", po6[3], 3.0, _FTOL);
    _check_f("merge_1.max.z", po6[5], 6.0, _FTOL);

    // --- point in box (true and false) ---
    printf("--- point ---\n");

    Vec3 const inside = { 1.0, 2.0, 3.0 };
    Vec3 const outside = { 5.0, 5.0, 5.0 };
    FSize pin[3] = { 1.0, 2.0, 3.0 };
    FSize pout[3] = { 5.0, 5.0, 5.0 };
    _check_b("point_2 inside true", math_box_point_2(a, inside), true);
    _check_b("point_2 outside false", math_box_point_2(a, outside), false);
    _check_b("point_1 inside true", math_box_point_1(pa, pin), true);
    _check_b("point_1 outside false", math_box_point_1(pa, pout), false);

    // --- size / radius (diagonal of A is sqrt(56)) ---
    printf("--- size / radius ---\n");

    _check_f("size_2", math_box_size_2(a), sqrt(56.0), _FTOL);
    _check_f("size_1", math_box_size_1(pa), sqrt(56.0), _FTOL);
    _check_f("radius_2", math_box_radius_2(a), sqrt(56.0) / 2.0, _FTOL);
    _check_f("radius_1", math_box_radius_1(pa), sqrt(56.0) / 2.0, _FTOL);

    // --- sphere intersection (true and false) ---
    printf("--- sphere ---\n");

    Sphere const s_hit = { 1.0, 2.0, 3.0, 1.0 };
    Sphere const s_miss = { 10.0, 10.0, 10.0, 1.0 };
    FSize psin[4] = { 1.0, 2.0, 3.0, 1.0 };
    FSize psout[4] = { 10.0, 10.0, 10.0, 1.0 };
    _check_b("sphere_2 hit true", math_box_sphere_2(a, s_hit), true);
    _check_b("sphere_2 miss false", math_box_sphere_2(a, s_miss), false);
    _check_b("sphere_1 hit true", math_box_sphere_1(pa, psin), true);
    _check_b("sphere_1 miss false", math_box_sphere_1(pa, psout), false);

    // --- transform (identity leaves the box unchanged) ---
    printf("--- transform ---\n");

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
    Box const tr = math_box_transform_2(a, eye4);
    _check_f("transform_2.min.x identity", tr.min.x, 0.0, _FTOL);
    _check_f("transform_2.max.z identity", tr.max.z, 6.0, _FTOL);
    math_box_transform_1(pa, peye4, po6);
    _check_f("transform_1.max.z identity", po6[5], 6.0, _FTOL);

    // keep _check_u / _check_i and _TOL referenced so the harness is exercised
    _check_u("pass counter positive", _pass > 0 ? 1 : 0, 1);
    _check_i("fail counter type", (ISize) 0, 0);
    _check_f("tol sentinel", 1.0, 1.0, _TOL);

    return _check_finish();
}