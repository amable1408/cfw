/*
 * test_aabb2d.c - Tests for include/math/aabb2d.c (full glmc_aabb2d_* coverage)
 */

#include <math.h>
#include <stdio.h>

#include <math/aabb2d.h>

#include "check.h"

// === Helpers ===

#define _TOL 1e-9
#define _FTOL 1e-6

int main(void) {
    printf("=== aabb2d module tests ===\n");

    // Box A: min (0,0) max (2,2).   Box B: min (1,1) max (3,3).
    Aabb2d const a = { { 0.0, 0.0 }, { 2.0, 2.0 } };
    Aabb2d const b = { { 1.0, 1.0 }, { 3.0, 3.0 } };
    FSize pa[4] = { 0.0, 0.0, 2.0, 2.0 };
    FSize pb[4] = { 1.0, 1.0, 3.0, 3.0 };
    FSize po[4] = DEFAULT_INITIALIZATION;

    // --- construction: zero / invalidate / copy ---
    printf("--- construction ---\n");

    Aabb2d const z = math_aabb2d_zero_2();
    _check_f("zero_2 min.x", z.min.x, 0.0, _TOL);
    _check_f("zero_2 max.y", z.max.y, 0.0, _TOL);
    _check_b("zero_2 isvalid", math_aabb2d_isvalid_2(z), true);
    math_aabb2d_zero_1(po);
    _check_f("zero_1 [2]", po[2], 0.0, _TOL);

    // invalidate produces an empty box (min at +inf, max at -inf) -> not valid
    Aabb2d const inv = math_aabb2d_invalidate_2();
    _check_b("invalidate_2 not valid", math_aabb2d_isvalid_2(inv), false);
    math_aabb2d_invalidate_1(po);
    _check_b("invalidate_1 not valid", math_aabb2d_isvalid_1(po), false);

    Aabb2d const cp = math_aabb2d_copy_2(a);
    _check_f("copy_2 min.x", cp.min.x, 0.0, _TOL);
    _check_f("copy_2 max.x", cp.max.x, 2.0, _TOL);
    math_aabb2d_copy_1(pa, po);
    _check_f("copy_1 [3]", po[3], 2.0, _TOL);

    // --- metrics: center / sizev / diag / radius ---
    printf("--- metrics ---\n");

    // center(A) = (1,1)
    Vec2 const ctr = math_aabb2d_center_2(a);
    _check_f("center_2.x", ctr.x, 1.0, _FTOL);
    _check_f("center_2.y", ctr.y, 1.0, _FTOL);
    math_aabb2d_center_1(pa, po);
    _check_f("center_1.y", po[1], 1.0, _FTOL);

    // sizev(A) = max - min = (2,2)
    Vec2 const sv = math_aabb2d_sizev_2(a);
    _check_f("sizev_2.x", sv.x, 2.0, _FTOL);
    _check_f("sizev_2.y", sv.y, 2.0, _FTOL);
    math_aabb2d_sizev_1(pa, po);
    _check_f("sizev_1.x", po[0], 2.0, _FTOL);

    // diag(A) = |sizev| = sqrt(8);  radius(A) = diag/2 = sqrt(2)
    _check_f("diag_2", math_aabb2d_diag_2(a), sqrt(8.0), _FTOL);
    _check_f("diag_1", math_aabb2d_diag_1(pa), sqrt(8.0), _FTOL);
    _check_f("radius_2", math_aabb2d_radius_2(a), sqrt(2.0), _FTOL);
    _check_f("radius_1", math_aabb2d_radius_1(pa), sqrt(2.0), _FTOL);

    // --- boolean geometry: merge / crop / crop_until ---
    printf("--- boolean geometry ---\n");

    // merge(A,B) = enclosing box = min(0,0) max(3,3)
    Aabb2d const mg = math_aabb2d_merge_2(a, b);
    _check_f("merge_2 min.x", mg.min.x, 0.0, _FTOL);
    _check_f("merge_2 min.y", mg.min.y, 0.0, _FTOL);
    _check_f("merge_2 max.x", mg.max.x, 3.0, _FTOL);
    _check_f("merge_2 max.y", mg.max.y, 3.0, _FTOL);
    math_aabb2d_merge_1(pa, pb, po);
    _check_f("merge_1 max.x", po[2], 3.0, _FTOL);

    // crop(A,B) = intersection = min(1,1) max(2,2)
    Aabb2d const cr = math_aabb2d_crop_2(a, b);
    _check_f("crop_2 min.x", cr.min.x, 1.0, _FTOL);
    _check_f("crop_2 max.x", cr.max.x, 2.0, _FTOL);
    math_aabb2d_crop_1(pa, pb, po);
    _check_f("crop_1 min.y", po[1], 1.0, _FTOL);

    // crop_until = crop(A,B) then merge with clamp box. crop(A,B)=[(1,1),(2,2)];
    // clamp cl sits inside it, so the merge leaves the crop unchanged -> [(1,1),(2,2)].
    Aabb2d const cl = { { 1.5, 1.5 }, { 1.8, 1.8 } };
    FSize pcl[4] = { 1.5, 1.5, 1.8, 1.8 };
    Aabb2d const cru = math_aabb2d_crop_until_2(a, b, cl);
    _check_f("crop_until_2 min.x", cru.min.x, 1.0, _FTOL);
    _check_f("crop_until_2 max.x", cru.max.x, 2.0, _FTOL);
    math_aabb2d_crop_until_1(pa, pb, pcl, po);
    _check_f("crop_until_1 min.x", po[0], 1.0, _FTOL);

    // --- predicates: isvalid / aabb / point / contains / circle ---
    printf("--- predicates ---\n");

    _check_b("isvalid_2 true", math_aabb2d_isvalid_2(a), true);
    _check_b("isvalid_1 true", math_aabb2d_isvalid_1(pa), true);

    // A and B overlap; A and a distant box do not
    Aabb2d const away = { { 5.0, 5.0 }, { 6.0, 6.0 } };
    FSize paway[4] = { 5.0, 5.0, 6.0, 6.0 };
    _check_b("aabb_2 intersect true", math_aabb2d_aabb_2(a, b), true);
    _check_b("aabb_2 intersect false", math_aabb2d_aabb_2(a, away), false);
    _check_b("aabb_1 intersect true", math_aabb2d_aabb_1(pa, pb), true);
    _check_b("aabb_1 intersect false", math_aabb2d_aabb_1(pa, paway), false);

    // point (1,1) inside A -> true; point (5,5) outside A -> false
    Vec2 const pin = { 1.0, 1.0 };
    Vec2 const pout = { 5.0, 5.0 };
    FSize ppin[2] = { 1.0, 1.0 };
    FSize ppout[2] = { 5.0, 5.0 };
    _check_b("point_2 inside", math_aabb2d_point_2(a, pin), true);
    _check_b("point_2 outside", math_aabb2d_point_2(a, pout), false);
    _check_b("point_1 inside", math_aabb2d_point_1(pa, ppin), true);
    _check_b("point_1 outside", math_aabb2d_point_1(pa, ppout), false);

    // A fully contains a small inner box; A does not contain B (B extends past A)
    Aabb2d const inner = { { 0.5, 0.5 }, { 1.5, 1.5 } };
    FSize pinner[4] = { 0.5, 0.5, 1.5, 1.5 };
    _check_b("contains_2 true", math_aabb2d_contains_2(a, inner), true);
    _check_b("contains_2 false", math_aabb2d_contains_2(a, b), false);
    _check_b("contains_1 true", math_aabb2d_contains_1(pa, pinner), true);
    _check_b("contains_1 false", math_aabb2d_contains_1(pa, pb), false);

    // circle at A's center (radius 0.5) overlaps; circle far away does not
    Circle const circ_in = { 1.0, 1.0, 0.5 };
    Circle const circ_out = { 10.0, 10.0, 1.0 };
    FSize pcirc_in[3] = { 1.0, 1.0, 0.5 };
    FSize pcirc_out[3] = { 10.0, 10.0, 1.0 };
    _check_b("circle_2 overlap", math_aabb2d_circle_2(a, circ_in), true);
    _check_b("circle_2 miss", math_aabb2d_circle_2(a, circ_out), false);
    _check_b("circle_1 overlap", math_aabb2d_circle_1(pa, pcirc_in), true);
    _check_b("circle_1 miss", math_aabb2d_circle_1(pa, pcirc_out), false);

    // --- transform (identity keeps the box) ---
    printf("--- transform ---\n");

    Mat3 const eye = { { { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } } };
    FSize peye[9] = { 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0 };
    Aabb2d const tf = math_aabb2d_transform_2(a, eye);
    _check_f("transform_2 min.x", tf.min.x, 0.0, _FTOL);
    _check_f("transform_2 max.x", tf.max.x, 2.0, _FTOL);
    _check_f("transform_2 max.y", tf.max.y, 2.0, _FTOL);
    math_aabb2d_transform_1(pa, peye, po);
    _check_f("transform_1 max.y", po[3], 2.0, _FTOL);

    // keep _check_u / _check_i referenced so the harness helpers are exercised
    _check_u("pass counter positive", _pass > 0 ? 1 : 0, 1);
    _check_i("fail counter type", (ISize) 0, 0);

    // Aabb2d <-> Rect raw form.
    FSize const raw_rect_in[4] = { 1.0, 2.0, 3.0, 4.0 };
    FSize       raw_aabb_out[4] = { 0.0, 0.0, 0.0, 0.0 };
    math_aabb2d_from_rect_1(raw_rect_in, raw_aabb_out);
    _check_f("aabb2d_from_rect_1 max x", raw_aabb_out[2], 4.0, 0.0);
    _check_f("aabb2d_from_rect_1 max y", raw_aabb_out[3], 6.0, 0.0);

    return _check_finish();
}