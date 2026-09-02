/*
 * test_rect.c - Tests for include/math/rect.c (full math_rect_* coverage)
 */

#include <math.h>
#include <stdio.h>

#include <math/aabb2d.h>
#include <math/rect.h>

#include "check.h"

// === Helpers ===

#define _FTOL 1e-6

int main(void) {
    printf("=== rect module tests ===\n");

    // Rect A: x0 y0 w4 h2.   Rect B: x2 y1 w4 h4.
    Rect const a = { 0.0, 0.0, 4.0, 2.0 };
    Rect const b = { 2.0, 1.0, 4.0, 4.0 };
    FSize pa[4] = { 0.0, 0.0, 4.0, 2.0 };
    FSize pb[4] = { 2.0, 1.0, 4.0, 4.0 };
    FSize po[4] = DEFAULT_INITIALIZATION;
    FSize pv[2] = DEFAULT_INITIALIZATION;

    // --- construction ---
    printf("--- construction ---\n");

    Rect const init = math_rect_init_2(0.0, 0.0, 4.0, 2.0);
    _check_f("init_2 w", init.w, 4.0, _FTOL);
    math_rect_init_1(0.0, 0.0, 4.0, 2.0, po);
    _check_f("init_1 h", po[3], 2.0, _FTOL);

    Rect const fa = math_rect_make_2(pa);
    _check_f("from_array_2 x", fa.x, 0.0, _FTOL);
    math_rect_make_1(pa, po);
    _check_f("from_array_1 w", po[2], 4.0, _FTOL);
    math_rect_to_array(a, po);
    _check_f("to_array h", po[3], 2.0, _FTOL);

    Vec2 const mn = { 0.0, 0.0 };
    Vec2 const mx = { 4.0, 2.0 };
    Rect const fmm = math_rect_from_min_max_2(mn, mx);
    _check_f("from_min_max_2 w", fmm.w, 4.0, _FTOL);
    _check_f("from_min_max_2 h", fmm.h, 2.0, _FTOL);
    FSize pmn[2] = { 0.0, 0.0 };
    FSize pmx[2] = { 4.0, 2.0 };
    math_rect_from_min_max_1(pmn, pmx, po);
    _check_f("from_min_max_1 w", po[2], 4.0, _FTOL);

    Vec2 const ctr = { 2.0, 1.0 };
    Vec2 const sz = { 4.0, 2.0 };
    Rect const fc = math_rect_from_center_2(ctr, sz);
    _check_f("from_center_2 x", fc.x, 0.0, _FTOL);
    _check_f("from_center_2 y", fc.y, 0.0, _FTOL);
    FSize pctr[2] = { 2.0, 1.0 };
    FSize psz[2] = { 4.0, 2.0 };
    math_rect_from_center_1(pctr, psz, po);
    _check_f("from_center_1 x", po[0], 0.0, _FTOL);

    Vec2 const p0 = { 4.0, 2.0 };
    Vec2 const p1 = { 0.0, 0.0 };
    Rect const fp = math_rect_from_points_2(p0, p1);
    _check_f("from_points_2 x", fp.x, 0.0, _FTOL);
    _check_f("from_points_2 w", fp.w, 4.0, _FTOL);
    FSize pp0[2] = { 4.0, 2.0 };
    FSize pp1[2] = { 0.0, 0.0 };
    math_rect_from_points_1(pp0, pp1, po);
    _check_f("from_points_1 h", po[3], 2.0, _FTOL);

    // --- accessors ---
    printf("--- accessors ---\n");

    _check_f("area_2", math_rect_area_2(a), 8.0, _FTOL);
    _check_f("area_1", math_rect_area_1(pa), 8.0, _FTOL);
    _check_f("perimeter_2", math_rect_perimeter_2(a), 12.0, _FTOL);
    _check_f("perimeter_1", math_rect_perimeter_1(pa), 12.0, _FTOL);
    _check_f("left_2", math_rect_left_2(a), 0.0, _FTOL);
    _check_f("right_2", math_rect_right_2(a), 4.0, _FTOL);
    _check_f("top_2", math_rect_top_2(a), 0.0, _FTOL);
    _check_f("bottom_2", math_rect_bottom_2(a), 2.0, _FTOL);
    _check_f("left_1", math_rect_left_1(pa), 0.0, _FTOL);
    _check_f("right_1", math_rect_right_1(pa), 4.0, _FTOL);
    _check_f("top_1", math_rect_top_1(pa), 0.0, _FTOL);
    _check_f("bottom_1", math_rect_bottom_1(pa), 2.0, _FTOL);

    Vec2 const cn = math_rect_center_2(a);
    _check_f("center_2.x", cn.x, 2.0, _FTOL);
    _check_f("center_2.y", cn.y, 1.0, _FTOL);
    math_rect_center_1(pa, pv);
    _check_f("center_1.x", pv[0], 2.0, _FTOL);

    Vec2 const mnv = math_rect_min_2(a);
    _check_f("min_2.x", mnv.x, 0.0, _FTOL);
    Vec2 const mxv = math_rect_max_2(a);
    _check_f("max_2.x", mxv.x, 4.0, _FTOL);
    _check_f("max_2.y", mxv.y, 2.0, _FTOL);
    Vec2 const szv = math_rect_size_2(a);
    _check_f("size_2.x", szv.x, 4.0, _FTOL);
    math_rect_min_1(pa, pv);
    _check_f("min_1.y", pv[1], 0.0, _FTOL);
    math_rect_max_1(pa, pv);
    _check_f("max_1.x", pv[0], 4.0, _FTOL);
    math_rect_size_1(pa, pv);
    _check_f("size_1.y", pv[1], 2.0, _FTOL);

    Vec2 const tl = math_rect_top_left_2(a);
    Vec2 const tr = math_rect_top_right_2(a);
    Vec2 const bl = math_rect_bottom_left_2(a);
    Vec2 const br = math_rect_bottom_right_2(a);
    _check_f("top_left_2.x", tl.x, 0.0, _FTOL);
    _check_f("top_right_2.x", tr.x, 4.0, _FTOL);
    _check_f("bottom_left_2.y", bl.y, 2.0, _FTOL);
    _check_f("bottom_right_2.x", br.x, 4.0, _FTOL);
    _check_f("bottom_right_2.y", br.y, 2.0, _FTOL);
    math_rect_top_left_1(pa, pv);
    _check_f("top_left_1.y", pv[1], 0.0, _FTOL);
    math_rect_top_right_1(pa, pv);
    _check_f("top_right_1.x", pv[0], 4.0, _FTOL);
    math_rect_bottom_left_1(pa, pv);
    _check_f("bottom_left_1.y", pv[1], 2.0, _FTOL);
    math_rect_bottom_right_1(pa, pv);
    _check_f("bottom_right_1.y", pv[1], 2.0, _FTOL);

    // --- predicates ---
    printf("--- predicates ---\n");

    Vec2 const pin = { 1.0, 1.0 };
    Vec2 const pout = { 5.0, 5.0 };
    FSize ppin[2] = { 1.0, 1.0 };
    FSize ppout[2] = { 5.0, 5.0 };
    _check_b("contains_point_2 in", math_rect_contains_point_2(a, pin), true);
    _check_b("contains_point_2 out", math_rect_contains_point_2(a, pout), false);
    _check_b("contains_point_1 in", math_rect_contains_point_1(pa, ppin), true);
    _check_b("contains_point_1 out", math_rect_contains_point_1(pa, ppout), false);

    Rect const inner = { 1.0, 0.5, 1.0, 1.0 };
    FSize pinner[4] = { 1.0, 0.5, 1.0, 1.0 };
    _check_b("contains_rect_2 true", math_rect_contains_rect_2(a, inner), true);
    _check_b("contains_rect_2 false", math_rect_contains_rect_2(a, b), false);
    _check_b("contains_rect_1 true", math_rect_contains_rect_1(pa, pinner), true);
    _check_b("contains_rect_1 false", math_rect_contains_rect_1(pa, pb), false);

    _check_b("intersects_2 true", math_rect_intersects_2(a, b), true);
    _check_b("intersects_1 true", math_rect_intersects_1(pa, pb), true);
    Rect const away = { 10.0, 10.0, 1.0, 1.0 };
    FSize paway[4] = { 10.0, 10.0, 1.0, 1.0 };
    _check_b("intersects_2 false", math_rect_intersects_2(a, away), false);
    _check_b("intersects_1 false", math_rect_intersects_1(pa, paway), false);

    _check_b("equal_2 true", math_rect_equal_2(a, a), true);
    _check_b("equal_2 false", math_rect_equal_2(a, b), false);
    _check_b("equal_1 true", math_rect_equal_1(pa, pa), true);
    _check_b("equal_1 false", math_rect_equal_1(pa, pb), false);

    Rect const empty = { 0.0, 0.0, 0.0, 2.0 };
    FSize pempty[4] = { 0.0, 0.0, 0.0, 2.0 };
    _check_b("is_empty_2 true", math_rect_is_empty_2(empty), true);
    _check_b("is_empty_2 false", math_rect_is_empty_2(a), false);
    _check_b("is_empty_1 true", math_rect_is_empty_1(pempty), true);
    _check_b("is_empty_1 false", math_rect_is_empty_1(pa), false);

    // --- set ops ---
    printf("--- set ops ---\n");

    // intersection(A,B) = {2,1,2,1}
    Rect const isect = math_rect_intersection_2(a, b);
    _check_f("intersection_2 x", isect.x, 2.0, _FTOL);
    _check_f("intersection_2 y", isect.y, 1.0, _FTOL);
    _check_f("intersection_2 w", isect.w, 2.0, _FTOL);
    _check_f("intersection_2 h", isect.h, 1.0, _FTOL);
    math_rect_intersection_1(pa, pb, po);
    _check_f("intersection_1 w", po[2], 2.0, _FTOL);
    // disjoint intersection -> zero
    math_rect_intersection_1(pa, paway, po);
    _check_f("intersection_1 disjoint w", po[2], 0.0, _FTOL);

    // union(A,B) = {0,0,6,5}
    Rect const uni = math_rect_union_2(a, b);
    _check_f("union_2 w", uni.w, 6.0, _FTOL);
    _check_f("union_2 h", uni.h, 5.0, _FTOL);
    math_rect_union_1(pa, pb, po);
    _check_f("union_1 h", po[3], 5.0, _FTOL);

    // union_point(A,(5,5)) = {0,0,5,5}
    Vec2 const up = { 5.0, 5.0 };
    Rect const unip = math_rect_union_point_2(a, up);
    _check_f("union_point_2 w", unip.w, 5.0, _FTOL);
    _check_f("union_point_2 h", unip.h, 5.0, _FTOL);
    FSize pup[2] = { 5.0, 5.0 };
    math_rect_union_point_1(pa, pup, po);
    _check_f("union_point_1 w", po[2], 5.0, _FTOL);

    // --- transforms ---
    printf("--- transforms ---\n");

    Vec2 const off = { 1.0, 2.0 };
    Rect const tr2 = math_rect_translate_2(a, off);
    _check_f("translate_2 x", tr2.x, 1.0, _FTOL);
    _check_f("translate_2 y", tr2.y, 2.0, _FTOL);
    FSize poff[2] = { 1.0, 2.0 };
    math_rect_translate_1(pa, poff, po);
    _check_f("translate_1 y", po[1], 2.0, _FTOL);

    Rect const sc = math_rect_scale_2(a, 2.0);
    _check_f("scale_2 w", sc.w, 8.0, _FTOL);
    _check_f("scale_2 h", sc.h, 4.0, _FTOL);
    math_rect_scale_1(pa, 2.0, po);
    _check_f("scale_1 w", po[2], 8.0, _FTOL);

    Rect const inf = math_rect_inflate_2(a, 1.0, 1.0);
    _check_f("inflate_2 x", inf.x, -1.0, _FTOL);
    _check_f("inflate_2 w", inf.w, 6.0, _FTOL);
    math_rect_inflate_1(pa, 1.0, 1.0, po);
    _check_f("inflate_1 h", po[3], 4.0, _FTOL);

    Rect const ins = math_rect_inset_2(a, 1.0, 0.0, 1.0, 0.0);
    _check_f("inset_2 x", ins.x, 1.0, _FTOL);
    _check_f("inset_2 w", ins.w, 2.0, _FTOL);
    math_rect_inset_1(pa, 1.0, 0.0, 1.0, 0.0, po);
    _check_f("inset_1 w", po[2], 2.0, _FTOL);

    Rect const flip = { 4.0, 2.0, -4.0, -2.0 };
    FSize pflip[4] = { 4.0, 2.0, -4.0, -2.0 };
    Rect const nrm = math_rect_normalize_2(flip);
    _check_f("normalize_2 x", nrm.x, 0.0, _FTOL);
    _check_f("normalize_2 w", nrm.w, 4.0, _FTOL);
    math_rect_normalize_1(pflip, po);
    _check_f("normalize_1 y", po[1], 0.0, _FTOL);

    Rect const rr = { 0.4, 0.6, 3.5, 2.4 };
    FSize prr[4] = { 0.4, 0.6, 3.5, 2.4 };
    Rect const rnd = math_rect_round_2(rr);
    _check_f("round_2 x", rnd.x, 0.0, _FTOL);
    _check_f("round_2 y", rnd.y, 1.0, _FTOL);
    _check_f("round_2 w", rnd.w, 4.0, _FTOL);
    math_rect_round_1(prr, po);
    _check_f("round_1 h", po[3], 2.0, _FTOL);

    // lerp(A,B,0.5) = {1,0.5,4,3}
    Rect const lp = math_rect_lerp_2(a, b, 0.5);
    _check_f("lerp_2 x", lp.x, 1.0, _FTOL);
    _check_f("lerp_2 h", lp.h, 3.0, _FTOL);
    math_rect_lerp_1(pa, pb, 0.5, po);
    _check_f("lerp_1 x", po[0], 1.0, _FTOL);

    // --- layout ---
    printf("--- layout ---\n");

    Vec2 const cp = math_rect_clamp_point_2(a, pout);
    _check_f("clamp_point_2.x", cp.x, 4.0, _FTOL);
    _check_f("clamp_point_2.y", cp.y, 2.0, _FTOL);
    math_rect_clamp_point_1(pa, ppout, pv);
    _check_f("clamp_point_1.x", pv[0], 4.0, _FTOL);

    Rect const in2 = { 0.0, 0.0, 2.0, 2.0 };
    Rect const out2 = { 0.0, 0.0, 4.0, 4.0 };
    FSize pin2[4] = { 0.0, 0.0, 2.0, 2.0 };
    FSize pout2[4] = { 0.0, 0.0, 4.0, 4.0 };
    Rect const cin = math_rect_center_in_2(in2, out2);
    _check_f("center_in_2 x", cin.x, 1.0, _FTOL);
    _check_f("center_in_2 y", cin.y, 1.0, _FTOL);
    math_rect_center_in_1(pin2, pout2, po);
    _check_f("center_in_1 x", po[0], 1.0, _FTOL);

    Rect const far_rect = { 5.0, 5.0, 2.0, 2.0 };
    FSize pfar[4] = { 5.0, 5.0, 2.0, 2.0 };
    Rect const con = math_rect_constrain_2(far_rect, out2);
    _check_f("constrain_2 x", con.x, 2.0, _FTOL);
    _check_f("constrain_2 y", con.y, 2.0, _FTOL);
    math_rect_constrain_1(pfar, pout2, po);
    _check_f("constrain_1 y", po[1], 2.0, _FTOL);

    // fit_aspect inner 2x1 into 4x4 -> 4x2 centered at y1
    Rect const fit_in = { 0.0, 0.0, 2.0, 1.0 };
    FSize pfit_in[4] = { 0.0, 0.0, 2.0, 1.0 };
    Rect const fit = math_rect_fit_aspect_2(fit_in, out2);
    _check_f("fit_aspect_2 w", fit.w, 4.0, _FTOL);
    _check_f("fit_aspect_2 h", fit.h, 2.0, _FTOL);
    _check_f("fit_aspect_2 y", fit.y, 1.0, _FTOL);
    math_rect_fit_aspect_1(pfit_in, pout2, po);
    _check_f("fit_aspect_1 w", po[2], 4.0, _FTOL);

    // grid_cell(A, 2 cols, 1 row, col1 row0) = {2,0,2,2}
    Rect const cell = math_rect_grid_cell_2(a, (IVec2) { .x = 2, .y = 1 }, (IVec2) { .x = 1, .y = 0 });
    _check_f("grid_cell_2 x", cell.x, 2.0, _FTOL);
    _check_f("grid_cell_2 w", cell.w, 2.0, _FTOL);
    _check_f("grid_cell_2 h", cell.h, 2.0, _FTOL);
    math_rect_grid_cell_1(pa, (IVec2) { .x = 2, .y = 1 }, (IVec2) { .x = 1, .y = 0 }, po);
    _check_f("grid_cell_1 x", po[0], 2.0, _FTOL);
    // zero cols treated as one -> full width cell
    Rect const cell0 = math_rect_grid_cell_2(a, (IVec2) { .x = 0, .y = 0 }, (IVec2) { .x = 0, .y = 0 });
    _check_f("grid_cell_2 zero-cols w", cell0.w, 4.0, _FTOL);

    // A zero-sized inner rect used to divide to Inf and scale to NaN; it now refuses to empty.
    Rect const degenerate = { 0.0, 0.0, 0.0, 4.0 };
    Rect const refused    = math_rect_fit_aspect_2(degenerate, out2);
    _check_f("fit_aspect_2 zero inner width -> empty w", refused.w, 0.0, 0.0);
    _check_f("fit_aspect_2 zero inner width -> empty h", refused.h, 0.0, 0.0);
    FSize const dg[4] = { 0.0, 0.0, 4.0, 0.0 };
    FSize const ob[4] = { 0.0, 0.0, 4.0, 4.0 };
    FSize       rf[4] = { 1.0, 1.0, 1.0, 1.0 };
    math_rect_fit_aspect_1(dg, ob, rf);
    _check_f("fit_aspect_1 zero inner height -> empty", rf[2] + rf[3], 0.0, 0.0);

    // Rect <-> Aabb2d: the same area in the other 2D form, round-tripped.
    Aabb2d const from_rect = math_aabb2d_from_rect_2((Rect) { 1.0, 2.0, 3.0, 4.0 });
    _check_f("aabb2d_from_rect_2 max x", from_rect.max.x, 4.0, 0.0);
    _check_f("aabb2d_from_rect_2 max y", from_rect.max.y, 6.0, 0.0);
    Rect const back = math_rect_from_aabb2d_2(from_rect);
    _check_f("rect_from_aabb2d_2 round trip w", back.w, 3.0, 0.0);
    _check_f("rect_from_aabb2d_2 round trip h", back.h, 4.0, 0.0);
    FSize const raw_aabb[4] = { 1.0, 2.0, 4.0, 6.0 };
    FSize       raw_rect[4] = { 0.0, 0.0, 0.0, 0.0 };
    math_rect_from_aabb2d_1(raw_aabb, raw_rect);
    _check_f("rect_from_aabb2d_1 w", raw_rect[2], 3.0, 0.0);

    return _check_finish();
}