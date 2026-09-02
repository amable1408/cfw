/*
 * test_vec3.c - Tests for include/math/vec3.c (full glmc_vec3_* coverage)
 */

#include <math.h>
#include <stdio.h>

#include <math/vec3.h>

#include "check.h"

// === Helpers ===

#define _TOL 1e-9
#define _FTOL 1e-6

int main(void) {
    printf("=== vec3 module tests ===\n");

    // a = (2, 3, 6): norm 7, handy for scale_as/normalize checks
    Vec3 const a = { 2.0, 3.0, 6.0 };
    Vec3 const b = { 1.0, 2.0, 3.0 };
    FSize pa[3] = { 2.0, 3.0, 6.0 };
    FSize pb[3] = { 1.0, 2.0, 3.0 };
    FSize po[3] = DEFAULT_INITIALIZATION;

    // --- construction: copy / make / broadcast / fill / zero / one / vec3 ---
    printf("--- construction ---\n");

    Vec3 const cp = math_vec3_copy_2(a);
    _check_f("copy_2.x", cp.x, 2.0, _TOL);
    _check_f("copy_2.z", cp.z, 6.0, _TOL);
    math_vec3_copy_1(pa, po);
    _check_f("copy_1.z", po[2], 6.0, _TOL);

    Vec3 const mk = math_vec3_make_2(pa);
    _check_f("make_2.y", mk.y, 3.0, _TOL);
    math_vec3_make_1(pb, po);
    _check_f("make_1.z", po[2], 3.0, _TOL);

    Vec3 const bc = math_vec3_broadcast_2(7.0);
    _check_f("broadcast_2.x", bc.x, 7.0, _TOL);
    _check_f("broadcast_2.z", bc.z, 7.0, _TOL);
    math_vec3_broadcast_1(5.0, po);
    _check_f("broadcast_1.y", po[1], 5.0, _TOL);

    Vec3 const fl = math_vec3_fill_2(4.0);
    _check_f("fill_2.z", fl.z, 4.0, _TOL);
    math_vec3_fill_1(9.0, po);
    _check_f("fill_1.x", po[0], 9.0, _TOL);

    Vec3 const zr = math_vec3_zero_2();
    _check_f("zero_2.y", zr.y, 0.0, _TOL);
    math_vec3_zero_1(po);
    _check_f("zero_1.z", po[2], 0.0, _TOL);

    Vec3 const on = math_vec3_one_2();
    _check_f("one_2.z", on.z, 1.0, _TOL);
    math_vec3_one_1(po);
    _check_f("one_1.x", po[0], 1.0, _TOL);

    // vec3(vec4) drops the w component: (1,2,3,4) -> (1,2,3)
    Vec4 const v4 = { 1.0, 2.0, 3.0, 4.0 };
    FSize pv4[4] = { 1.0, 2.0, 3.0, 4.0 };
    Vec3 const v3 = math_vec3_vec3_2(v4);
    _check_f("vec3_2.x", v3.x, 1.0, _TOL);
    _check_f("vec3_2.z", v3.z, 3.0, _TOL);
    math_vec3_vec3_1(pv4, po);
    _check_f("vec3_1.z", po[2], 3.0, _TOL);

    // --- comparison: eq / eq_all / eq_eps / eqv / eqv_eps ---
    printf("--- comparison ---\n");

    Vec3 const sevens = { 7.0, 7.0, 7.0 };
    FSize psevens[3] = { 7.0, 7.0, 7.0 };
    _check_b("eq_2 true", math_vec3_eq_2(sevens, 7.0), true);
    _check_b("eq_2 false", math_vec3_eq_2(a, 7.0), false);
    _check_b("eq_1 true", math_vec3_eq_1(psevens, 7.0), true);

    _check_b("eq_all_2 true", math_vec3_eq_all_2(sevens), true);
    _check_b("eq_all_2 false", math_vec3_eq_all_2(a), false);
    _check_b("eq_all_1 true", math_vec3_eq_all_1(psevens), true);

    _check_b("eq_eps_2 true", math_vec3_eq_eps_2(sevens, 7.0), true);
    _check_b("eq_eps_1 true", math_vec3_eq_eps_1(psevens, 7.0), true);

    _check_b("eqv_2 true", math_vec3_eqv_2(a, a), true);
    _check_b("eqv_2 false", math_vec3_eqv_2(a, b), false);
    _check_b("eqv_1 true", math_vec3_eqv_1(pa, pa), true);
    _check_b("eqv_1 false", math_vec3_eqv_1(pa, pb), false);

    _check_b("eqv_eps_2 true", math_vec3_eqv_eps_2(a, a), true);
    _check_b("eqv_eps_1 true", math_vec3_eqv_eps_1(pa, pa), true);

    // --- validity: isnan / isinf / isvalid ---
    printf("--- validity ---\n");

    _check_b("isvalid_2 true", math_vec3_isvalid_2(a), true);
    _check_b("isvalid_1 true", math_vec3_isvalid_1(pa), true);
    _check_b("isnan_2 false", math_vec3_isnan_2(a), false);
    _check_b("isinf_2 false", math_vec3_isinf_2(a), false);

    Vec3 const bad = { NAN, 1.0, INFINITY };
    FSize pbad[3] = { NAN, 1.0, INFINITY };
    _check_b("isnan_2 true", math_vec3_isnan_2(bad), true);
    _check_b("isinf_2 true", math_vec3_isinf_2(bad), true);
    _check_b("isvalid_2 false", math_vec3_isvalid_2(bad), false);
    _check_b("isnan_1 true", math_vec3_isnan_1(pbad), true);
    _check_b("isinf_1 true", math_vec3_isinf_1(pbad), true);

    // --- arithmetic: add/adds/sub/subs/mul/mulv/div/divs/scale/scale_as ---
    printf("--- arithmetic ---\n");

    Vec3 const sum = math_vec3_add_2(a, b);
    _check_f("add_2.x", sum.x, 3.0, _TOL);
    _check_f("add_2.z", sum.z, 9.0, _TOL);
    math_vec3_add_1(pa, pb, po);
    _check_f("add_1.z", po[2], 9.0, _TOL);

    Vec3 const adds = math_vec3_adds_2(a, 10.0);
    _check_f("adds_2.x", adds.x, 12.0, _TOL);
    math_vec3_adds_1(pa, 10.0, po);
    _check_f("adds_1.z", po[2], 16.0, _TOL);

    Vec3 const diff = math_vec3_sub_2(a, b);
    _check_f("sub_2.z", diff.z, 3.0, _TOL);
    math_vec3_sub_1(pa, pb, po);
    _check_f("sub_1.x", po[0], 1.0, _TOL);

    Vec3 const subs = math_vec3_subs_2(a, 1.0);
    _check_f("subs_2.z", subs.z, 5.0, _TOL);
    math_vec3_subs_1(pa, 1.0, po);
    _check_f("subs_1.y", po[1], 2.0, _TOL);

    Vec3 const prod = math_vec3_mul_2(a, b);
    _check_f("mul_2.z", prod.z, 18.0, _TOL);
    math_vec3_mul_1(pa, pb, po);
    _check_f("mul_1.y", po[1], 6.0, _TOL);

    // mulv is a mul alias
    Vec3 const prodv = math_vec3_mulv_2(a, b);
    _check_f("mulv_2.z", prodv.z, 18.0, _TOL);
    math_vec3_mulv_1(pa, pb, po);
    _check_f("mulv_1.x", po[0], 2.0, _TOL);

    Vec3 const quot = math_vec3_div_2(a, b);
    _check_f("div_2.x", quot.x, 2.0, _TOL);
    _check_f("div_2.z", quot.z, 2.0, _TOL);
    math_vec3_div_1(pa, pb, po);
    _check_f("div_1.y", po[1], 1.5, _TOL);

    Vec3 const divs = math_vec3_divs_2(a, 2.0);
    _check_f("divs_2.z", divs.z, 3.0, _TOL);
    math_vec3_divs_1(pa, 2.0, po);
    _check_f("divs_1.x", po[0], 1.0, _TOL);

    Vec3 const scaled = math_vec3_scale_2(a, 2.0);
    _check_f("scale_2.z", scaled.z, 12.0, _TOL);
    math_vec3_scale_1(pa, 2.0, po);
    _check_f("scale_1.y", po[1], 6.0, _TOL);

    // scale_as sets length to s; a has length 7, so scale_as(14) doubles it
    Vec3 const scas = math_vec3_scale_as_2(a, 14.0);
    _check_f("scale_as_2 len", math_vec3_norm_2(scas), 14.0, _FTOL);
    math_vec3_scale_as_1(pa, 14.0, po);
    _check_f("scale_as_1.x", po[0], 4.0, _FTOL);

    // --- fused accumulate ---
    printf("--- fused accumulate ---\n");

    Vec3 const accumulator = { 10.0, 20.0, 30.0 };
    FSize pacc[3] = { 10.0, 20.0, 30.0 };

    // dest += a + b  ->  (10+3, 20+5, 30+9) = (13, 25, 39)
    Vec3 const addadd = math_vec3_addadd_2(a, b, accumulator);
    _check_f("addadd_2.x", addadd.x, 13.0, _TOL);
    _check_f("addadd_2.z", addadd.z, 39.0, _TOL);
    pacc[0] = 10.0; pacc[1] = 20.0; pacc[2] = 30.0;
    math_vec3_addadd_1(pa, pb, pacc);
    _check_f("addadd_1.z", pacc[2], 39.0, _TOL);

    // dest -= a + b  ->  (10-3, 20-5, 30-9) = (7, 15, 21)
    Vec3 const addsub = math_vec3_addsub_2(a, b, accumulator);
    _check_f("addsub_2.z", addsub.z, 21.0, _TOL);
    pacc[0] = 10.0; pacc[1] = 20.0; pacc[2] = 30.0;
    math_vec3_addsub_1(pa, pb, pacc);
    _check_f("addsub_1.x", pacc[0], 7.0, _TOL);

    // dest += a - b  ->  (10+1, 20+1, 30+3) = (11, 21, 33)
    Vec3 const subadd = math_vec3_subadd_2(a, b, accumulator);
    _check_f("subadd_2.z", subadd.z, 33.0, _TOL);
    pacc[0] = 10.0; pacc[1] = 20.0; pacc[2] = 30.0;
    math_vec3_subadd_1(pa, pb, pacc);
    _check_f("subadd_1.x", pacc[0], 11.0, _TOL);

    // dest -= a - b  ->  (10-1, 20-1, 30-3) = (9, 19, 27)
    Vec3 const subsub = math_vec3_subsub_2(a, b, accumulator);
    _check_f("subsub_2.z", subsub.z, 27.0, _TOL);
    pacc[0] = 10.0; pacc[1] = 20.0; pacc[2] = 30.0;
    math_vec3_subsub_1(pa, pb, pacc);
    _check_f("subsub_1.y", pacc[1], 19.0, _TOL);

    // dest += a * b  ->  (10+2, 20+6, 30+18) = (12, 26, 48)
    Vec3 const muladd = math_vec3_muladd_2(a, b, accumulator);
    _check_f("muladd_2.z", muladd.z, 48.0, _TOL);
    pacc[0] = 10.0; pacc[1] = 20.0; pacc[2] = 30.0;
    math_vec3_muladd_1(pa, pb, pacc);
    _check_f("muladd_1.y", pacc[1], 26.0, _TOL);

    // dest += a * s  ->  (10+2*2, 20+3*2, 30+6*2) = (14, 26, 42)
    Vec3 const muladds = math_vec3_muladds_2(a, 2.0, accumulator);
    _check_f("muladds_2.z", muladds.z, 42.0, _TOL);
    pacc[0] = 10.0; pacc[1] = 20.0; pacc[2] = 30.0;
    math_vec3_muladds_1(pa, 2.0, pacc);
    _check_f("muladds_1.x", pacc[0], 14.0, _TOL);

    // dest -= a * b  ->  (10-2, 20-6, 30-18) = (8, 14, 12)
    Vec3 const mulsub = math_vec3_mulsub_2(a, b, accumulator);
    _check_f("mulsub_2.z", mulsub.z, 12.0, _TOL);
    pacc[0] = 10.0; pacc[1] = 20.0; pacc[2] = 30.0;
    math_vec3_mulsub_1(pa, pb, pacc);
    _check_f("mulsub_1.y", pacc[1], 14.0, _TOL);

    // dest -= a * s  ->  (10-4, 20-6, 30-12) = (6, 14, 18)
    Vec3 const mulsubs = math_vec3_mulsubs_2(a, 2.0, accumulator);
    _check_f("mulsubs_2.z", mulsubs.z, 18.0, _TOL);
    pacc[0] = 10.0; pacc[1] = 20.0; pacc[2] = 30.0;
    math_vec3_mulsubs_1(pa, 2.0, pacc);
    _check_f("mulsubs_1.x", pacc[0], 6.0, _TOL);

    // dest += max(a,b)  ->  (10+2, 20+3, 30+6) = (12, 23, 36)
    Vec3 const maxadd = math_vec3_maxadd_2(a, b, accumulator);
    _check_f("maxadd_2.z", maxadd.z, 36.0, _TOL);
    pacc[0] = 10.0; pacc[1] = 20.0; pacc[2] = 30.0;
    math_vec3_maxadd_1(pa, pb, pacc);
    _check_f("maxadd_1.x", pacc[0], 12.0, _TOL);

    // dest -= max(a,b)  ->  (10-2, 20-3, 30-6) = (8, 17, 24)
    Vec3 const maxsub = math_vec3_maxsub_2(a, b, accumulator);
    _check_f("maxsub_2.z", maxsub.z, 24.0, _TOL);
    pacc[0] = 10.0; pacc[1] = 20.0; pacc[2] = 30.0;
    math_vec3_maxsub_1(pa, pb, pacc);
    _check_f("maxsub_1.y", pacc[1], 17.0, _TOL);

    // dest += min(a,b)  ->  (10+1, 20+2, 30+3) = (11, 22, 33)
    Vec3 const minadd = math_vec3_minadd_2(a, b, accumulator);
    _check_f("minadd_2.z", minadd.z, 33.0, _TOL);
    pacc[0] = 10.0; pacc[1] = 20.0; pacc[2] = 30.0;
    math_vec3_minadd_1(pa, pb, pacc);
    _check_f("minadd_1.x", pacc[0], 11.0, _TOL);

    // dest -= min(a,b)  ->  (10-1, 20-2, 30-3) = (9, 18, 27)
    Vec3 const minsub = math_vec3_minsub_2(a, b, accumulator);
    _check_f("minsub_2.z", minsub.z, 27.0, _TOL);
    pacc[0] = 10.0; pacc[1] = 20.0; pacc[2] = 30.0;
    math_vec3_minsub_1(pa, pb, pacc);
    _check_f("minsub_1.y", pacc[1], 18.0, _TOL);

    // --- geometry: dot/cross/crossn/norm*/distance/angle/proj/hadd ---
    printf("--- geometry ---\n");

    // dot(a,b) = 2+6+18 = 26
    _check_f("dot_2", math_vec3_dot_2(a, b), 26.0, _TOL);
    _check_f("dot_1", math_vec3_dot_1(pa, pb), 26.0, _TOL);

    // cross(x,y) = z : (1,0,0) x (0,1,0) = (0,0,1)
    Vec3 const ux = { 1.0, 0.0, 0.0 };
    Vec3 const uy = { 0.0, 1.0, 0.0 };
    FSize pux[3] = { 1.0, 0.0, 0.0 };
    FSize puy[3] = { 0.0, 1.0, 0.0 };
    Vec3 const cr = math_vec3_cross_2(ux, uy);
    _check_f("cross_2.z", cr.z, 1.0, _TOL);
    _check_f("cross_2.x", cr.x, 0.0, _TOL);
    math_vec3_cross_1(pux, puy, po);
    _check_f("cross_1.z", po[2], 1.0, _TOL);

    // crossn of orthonormal x,y is unit z
    Vec3 const crn = math_vec3_crossn_2(ux, uy);
    _check_f("crossn_2 len", math_vec3_norm_2(crn), 1.0, _FTOL);
    math_vec3_crossn_1(pux, puy, po);
    _check_f("crossn_1.z", po[2], 1.0, _FTOL);

    _check_f("norm_2", math_vec3_norm_2(a), 7.0, _FTOL);
    _check_f("norm_1", math_vec3_norm_1(pa), 7.0, _FTOL);
    _check_f("norm2_2", math_vec3_norm2_2(a), 49.0, _FTOL);
    _check_f("norm2_1", math_vec3_norm2_1(pa), 49.0, _FTOL);

    // norm_one(a) = |2|+|3|+|6| = 11 ; norm_inf(a) = 6
    _check_f("norm_one_2", math_vec3_norm_one_2(a), 11.0, _FTOL);
    _check_f("norm_one_1", math_vec3_norm_one_1(pa), 11.0, _FTOL);
    _check_f("norm_inf_2", math_vec3_norm_inf_2(a), 6.0, _FTOL);
    _check_f("norm_inf_1", math_vec3_norm_inf_1(pa), 6.0, _FTOL);

    // distance (2,3,6)-(1,2,3) = sqrt(1+1+9) = sqrt(11)
    _check_f("distance_2", math_vec3_distance_2(a, b), sqrt(11.0), _FTOL);
    _check_f("distance_1", math_vec3_distance_1(pa, pb), sqrt(11.0), _FTOL);
    _check_f("distance2_2", math_vec3_distance2_2(a, b), 11.0, _FTOL);
    _check_f("distance2_1", math_vec3_distance2_1(pa, pb), 11.0, _FTOL);

    // angle between x and y axes is pi/2
    _check_f("angle_2", math_vec3_angle_2(ux, uy), MATH_PI / 2.0, _FTOL);
    _check_f("angle_1", math_vec3_angle_1(pux, puy), MATH_PI / 2.0, _FTOL);

    // proj of (2,0,0) onto x axis is (2,0,0)
    Vec3 const p2x = { 2.0, 0.0, 0.0 };
    Vec3 const proj = math_vec3_proj_2(p2x, ux);
    _check_f("proj_2.x", proj.x, 2.0, _FTOL);
    FSize pp2x[3] = { 2.0, 0.0, 0.0 };
    math_vec3_proj_1(pp2x, pux, po);
    _check_f("proj_1.x", po[0], 2.0, _FTOL);

    // hadd(a) = 2+3+6 = 11
    _check_f("hadd_2", math_vec3_hadd_2(a), 11.0, _FTOL);
    _check_f("hadd_1", math_vec3_hadd_1(pa), 11.0, _FTOL);

    // --- reduction: max / min ---
    printf("--- reduction ---\n");

    _check_f("max_2", math_vec3_max_2(a), 6.0, _TOL);
    _check_f("max_1", math_vec3_max_1(pa), 6.0, _TOL);
    _check_f("min_2", math_vec3_min_2(a), 2.0, _TOL);
    _check_f("min_1", math_vec3_min_1(pa), 2.0, _TOL);

    // --- normalize (including zero-length boundary) ---
    printf("--- normalize ---\n");

    Vec3 const nrm = math_vec3_normalize_2(a);
    _check_f("normalize_2 unit", math_vec3_norm_2(nrm), 1.0, _FTOL);
    _check_f("normalize_2.x", nrm.x, 2.0 / 7.0, _FTOL);
    math_vec3_normalize_1(pa, po);
    _check_f("normalize_1.z", po[2], 6.0 / 7.0, _FTOL);

    // boundary: normalizing the zero vector yields zero
    FSize pzero[3] = DEFAULT_INITIALIZATION;
    FSize pzn[3] = DEFAULT_INITIALIZATION;
    math_vec3_normalize_1(pzero, pzn);
    _check_f("normalize_1 zero.x", pzn[0], 0.0, _TOL);
    _check_f("normalize_1 zero.z", pzn[2], 0.0, _TOL);

    // --- negate / center / lerp / lerpc / smoothinterp ---
    printf("--- negate / center / interp ---\n");

    Vec3 const neg = math_vec3_negate_2(a);
    _check_f("negate_2.z", neg.z, -6.0, _TOL);
    math_vec3_negate_1(pa, po);
    _check_f("negate_1.x", po[0], -2.0, _TOL);

    // center((2,3,6),(1,2,3)) = (1.5, 2.5, 4.5)
    Vec3 const ctr = math_vec3_center_2(a, b);
    _check_f("center_2.z", ctr.z, 4.5, _FTOL);
    math_vec3_center_1(pa, pb, po);
    _check_f("center_1.x", po[0], 1.5, _FTOL);

    // lerp(b, a, 0.5) = center
    Vec3 const mid = math_vec3_lerp_2(b, a, 0.5);
    _check_f("lerp_2.z", mid.z, 4.5, _TOL);
    math_vec3_lerp_1(pb, pa, 0.5, po);
    _check_f("lerp_1.x", po[0], 1.5, _TOL);

    // lerpc clamps t: t=2.0 clamps to 1.0 -> yields a
    Vec3 const lc = math_vec3_lerpc_2(b, a, 2.0);
    _check_f("lerpc_2.z clamp", lc.z, 6.0, _FTOL);
    math_vec3_lerpc_1(pb, pa, 2.0, po);
    _check_f("lerpc_1.x clamp", po[0], 2.0, _FTOL);

    // smoothinterp at t=0 is from, at t=1 is to
    Vec3 const si = math_vec3_smoothinterp_2(b, a, 0.0);
    _check_f("smoothinterp_2 t0.x", si.x, 1.0, _FTOL);
    math_vec3_smoothinterp_1(pb, pa, 1.0, po);
    _check_f("smoothinterp_1 t1.z", po[2], 6.0, _FTOL);

    // smoothinterpc clamps t: t=-1 clamps to 0 -> from
    Vec3 const sic = math_vec3_smoothinterpc_2(b, a, -1.0);
    _check_f("smoothinterpc_2 clamp.x", sic.x, 1.0, _FTOL);
    math_vec3_smoothinterpc_1(pb, pa, -1.0, po);
    _check_f("smoothinterpc_1 clamp.z", po[2], 3.0, _FTOL);

    // --- min/max vectors, clamp, ortho ---
    printf("--- minv/maxv / clamp / ortho ---\n");

    Vec3 const mxv = math_vec3_maxv_2(a, b);
    _check_f("maxv_2.z", mxv.z, 6.0, _TOL);
    Vec3 const mnv = math_vec3_minv_2(a, b);
    _check_f("minv_2.x", mnv.x, 1.0, _TOL);
    math_vec3_maxv_1(pa, pb, po);
    _check_f("maxv_1.z", po[2], 6.0, _TOL);
    math_vec3_minv_1(pa, pb, po);
    _check_f("minv_1.x", po[0], 1.0, _TOL);

    // clamp (2,3,6) into [0,4] -> (2,3,4)
    Vec3 const clp = math_vec3_clamp_2(a, 0.0, 4.0);
    _check_f("clamp_2.z", clp.z, 4.0, _FTOL);
    _check_f("clamp_2.x", clp.x, 2.0, _FTOL);
    math_vec3_clamp_1(pa, 0.0, 4.0, po);
    _check_f("clamp_1.z", po[2], 4.0, _FTOL);

    // ortho: result must be perpendicular to source (dot == 0)
    Vec3 const orth = math_vec3_ortho_2(a);
    _check_f("ortho_2 perp", math_vec3_dot_2(orth, a), 0.0, _FTOL);
    math_vec3_ortho_1(pa, po);
    Vec3 const orth1 = { po[0], po[1], po[2] };
    _check_f("ortho_1 perp", math_vec3_dot_2(orth1, a), 0.0, _FTOL);

    // --- rounding: abs / floor / fract / mods / sign / sqrt ---
    printf("--- rounding ---\n");

    Vec3 const negv = { -1.5, 2.5, -3.0 };
    FSize pnegv[3] = { -1.5, 2.5, -3.0 };

    Vec3 const av = math_vec3_abs_2(negv);
    _check_f("abs_2.x", av.x, 1.5, _FTOL);
    _check_f("abs_2.z", av.z, 3.0, _FTOL);
    math_vec3_abs_1(pnegv, po);
    _check_f("abs_1.z", po[2], 3.0, _FTOL);

    // floor(-1.5, 2.5, -3.0) = (-2, 2, -3)
    Vec3 const fv = math_vec3_floor_2(negv);
    _check_f("floor_2.x", fv.x, -2.0, _FTOL);
    math_vec3_floor_1(pnegv, po);
    _check_f("floor_1.y", po[1], 2.0, _FTOL);

    // fract(2.5) = 0.5
    Vec3 const frv = math_vec3_fract_2(negv);
    _check_f("fract_2.y", frv.y, 0.5, _FTOL);
    math_vec3_fract_1(pnegv, po);
    _check_f("fract_1.y", po[1], 0.5, _FTOL);

    // mods((2,3,6), 4) = (2, 3, 2)
    Vec3 const mdv = math_vec3_mods_2(a, 4.0);
    _check_f("mods_2.z", mdv.z, 2.0, _FTOL);
    math_vec3_mods_1(pa, 4.0, po);
    _check_f("mods_1.x", po[0], 2.0, _FTOL);

    // sign(-1.5, 2.5, -3.0) = (-1, 1, -1)
    Vec3 const sgn = math_vec3_sign_2(negv);
    _check_f("sign_2.x", sgn.x, -1.0, _TOL);
    _check_f("sign_2.y", sgn.y, 1.0, _TOL);
    math_vec3_sign_1(pnegv, po);
    _check_f("sign_1.z", po[2], -1.0, _TOL);

    // sqrt((4,9,16)) = (2,3,4)
    Vec3 const sqv = { 4.0, 9.0, 16.0 };
    FSize psqv[3] = { 4.0, 9.0, 16.0 };
    Vec3 const sq = math_vec3_sqrt_2(sqv);
    _check_f("sqrt_2.z", sq.z, 4.0, _FTOL);
    math_vec3_sqrt_1(psqv, po);
    _check_f("sqrt_1.y", po[1], 3.0, _FTOL);

    // --- stepping: step / steps / stepr / smoothstep / smoothstep_uni ---
    printf("--- stepping ---\n");

    // step(edge=(2,5,6), x=a=(2,3,6)) -> (x>=edge?1:0) = (1,0,1)
    Vec3 const edge = { 2.0, 5.0, 6.0 };
    FSize pedge[3] = { 2.0, 5.0, 6.0 };
    Vec3 const stp = math_vec3_step_2(edge, a);
    _check_f("step_2.x", stp.x, 1.0, _TOL);
    _check_f("step_2.y", stp.y, 0.0, _TOL);
    _check_f("step_2.z", stp.z, 1.0, _TOL);
    math_vec3_step_1(pedge, pa, po);
    _check_f("step_1.y", po[1], 0.0, _TOL);

    // steps(edge=3.5, x=a) -> (0,0,1)
    Vec3 const stps = math_vec3_steps_2(3.5, a);
    _check_f("steps_2.z", stps.z, 1.0, _TOL);
    _check_f("steps_2.x", stps.x, 0.0, _TOL);
    math_vec3_steps_1(3.5, pa, po);
    _check_f("steps_1.z", po[2], 1.0, _TOL);

    // stepr(edge=(2,5,6), x=3.5) -> (3.5>=edge?1:0) = (1,0,0)
    Vec3 const stpr = math_vec3_stepr_2(edge, 3.5);
    _check_f("stepr_2.x", stpr.x, 1.0, _TOL);
    _check_f("stepr_2.y", stpr.y, 0.0, _TOL);
    math_vec3_stepr_1(pedge, 3.5, po);
    _check_f("stepr_1.x", po[0], 1.0, _TOL);

    // smoothstep: x below edge0 -> 0, above edge1 -> 1
    Vec3 const e0 = { 0.0, 0.0, 0.0 };
    Vec3 const e1 = { 4.0, 4.0, 4.0 };
    FSize pe0[3] = { 0.0, 0.0, 0.0 };
    FSize pe1[3] = { 4.0, 4.0, 4.0 };
    Vec3 const ss = math_vec3_smoothstep_2(e0, e1, a);
    _check_f("smoothstep_2.z clamp", ss.z, 1.0, _FTOL);
    math_vec3_smoothstep_1(pe0, pe1, pa, po);
    _check_f("smoothstep_1.z clamp", po[2], 1.0, _FTOL);

    // smoothstep_uni(0, 4, a): x=6 -> above edge1 -> 1
    Vec3 const ssu = math_vec3_smoothstep_uni_2(0.0, 4.0, a);
    _check_f("smoothstep_uni_2.z", ssu.z, 1.0, _FTOL);
    math_vec3_smoothstep_uni_1(0.0, 4.0, pa, po);
    _check_f("smoothstep_uni_1.z", po[2], 1.0, _FTOL);

    // --- rotate: axis / m3 / m4 ---
    printf("--- rotate ---\n");

    // rotate x axis by pi/2 about z axis -> y axis (0,1,0)
    Vec3 const uz = { 0.0, 0.0, 1.0 };
    FSize puz[3] = { 0.0, 0.0, 1.0 };
    Vec3 const rot = math_vec3_rotate_2(ux, MATH_PI / 2.0, uz);
    _check_f("rotate_2.y", rot.y, 1.0, _FTOL);
    _check_f("rotate_2.x", rot.x, 0.0, _FTOL);
    math_vec3_rotate_1(pux, MATH_PI / 2.0, puz, po);
    _check_f("rotate_1.y", po[1], 1.0, _FTOL);

    // identity mat3 leaves the vector unchanged
    Mat3 const eye3 = { .m = { { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } } };
    FSize peye3[9] = { 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0 };
    Vec3 const rm3 = math_vec3_rotate_m3_2(eye3, a);
    _check_f("rotate_m3_2.z identity", rm3.z, 6.0, _FTOL);
    math_vec3_rotate_m3_1(peye3, pa, po);
    _check_f("rotate_m3_1.x identity", po[0], 2.0, _FTOL);

    // 90-deg rotation about z (column-major mat3): x -> y
    // columns: c0=(0,1,0), c1=(-1,0,0), c2=(0,0,1)
    Mat3 const rz3 = { .m = { { 0.0, 1.0, 0.0 }, { -1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 } } };
    Vec3 const rzx = math_vec3_rotate_m3_2(rz3, ux);
    _check_f("rotate_m3_2 rotz.y", rzx.y, 1.0, _FTOL);

    // identity mat4 leaves the vector unchanged
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
    Vec3 const rm4 = math_vec3_rotate_m4_2(eye4, a);
    _check_f("rotate_m4_2.z identity", rm4.z, 6.0, _FTOL);
    math_vec3_rotate_m4_1(peye4, pa, po);
    _check_f("rotate_m4_1.y identity", po[1], 3.0, _FTOL);

    // --- reflect / refract / faceforward ---
    printf("--- reflect / refract / faceforward ---\n");

    // reflect (1,-1,0) about normal (0,1,0) -> (1,1,0)
    Vec3 const inc = { 1.0, -1.0, 0.0 };
    Vec3 const nup = { 0.0, 1.0, 0.0 };
    FSize pinc[3] = { 1.0, -1.0, 0.0 };
    FSize pnup[3] = { 0.0, 1.0, 0.0 };
    Vec3 const rfl = math_vec3_reflect_2(inc, nup);
    _check_f("reflect_2.y", rfl.y, 1.0, _FTOL);
    _check_f("reflect_2.x", rfl.x, 1.0, _FTOL);
    math_vec3_reflect_1(pinc, pnup, po);
    _check_f("reflect_1.y", po[1], 1.0, _FTOL);

    // refract with eta=1 passes through and returns true
    Vec3 const incn = math_vec3_normalize_2(inc);
    Vec3Refraction const refracted = math_vec3_refract_2(incn, nup, 1.0);
    _check_b("refract_2 occurs", refracted.refracted, true);
    _check_f("refract_2 eta 1 passes through x", refracted.v.x, incn.x, _TOL);
    FSize pincn[3] = DEFAULT_INITIALIZATION;
    math_vec3_normalize_1(pinc, pincn);
    FSize prfr[3] = DEFAULT_INITIALIZATION;
    bool const refracted1 = math_vec3_refract_1(pincn, pnup, 1.0, prfr);
    _check_b("refract_1 occurs", refracted1, true);
    Vec3Refraction const bad_eta = math_vec3_refract_2(incn, nup, INFINITY);
    _check_b("refract_2 Inf eta refused", bad_eta.refracted, false);
    _check_f("refract_2 Inf eta zero v", bad_eta.v.z, 0.0, 0.0);
    FSize pbad_eta[3] = { 7.0, 7.0, 7.0 };
    _check_b("refract_1 negative eta refused", math_vec3_refract_1(pincn, pnup, -1.0, pbad_eta), false);
    _check_f("refract_1 negative eta zero dest", pbad_eta[0], 0.0, 0.0);
    _check_b("refract_1 eta below float range refused", math_vec3_refract_1(pincn, pnup, 1e-300, pbad_eta), false);

    // faceforward: with n and nref opposing v it returns n (or -n) so that it
    // faces against v; here dot(nref, v) sign flips n toward the viewer
    Vec3 const ff = math_vec3_faceforward_2(nup, inc, nup);
    _check_f("faceforward_2 mag", math_vec3_norm_2(ff), 1.0, _FTOL);
    math_vec3_faceforward_1(pnup, pinc, pnup, po);
    Vec3 const ff1 = { po[0], po[1], po[2] };
    _check_f("faceforward_1 mag", math_vec3_norm_2(ff1), 1.0, _FTOL);

    // --- swizzle ---
    printf("--- swizzle ---\n");

    // GLM_SHUFFLE3(0,1,2) reverses: dest = (v[2], v[1], v[0]) = (6,3,2)
    Vec3 const swz = math_vec3_swizzle_2(a, GLM_SHUFFLE3(0, 1, 2));
    _check_f("swizzle_2 rev.x", swz.x, 6.0, _TOL);
    _check_f("swizzle_2 rev.z", swz.z, 2.0, _TOL);
    // GLM_SHUFFLE3(2,1,0) is identity
    math_vec3_swizzle_1(pa, GLM_SHUFFLE3(2, 1, 0), po);
    _check_f("swizzle_1 identity.x", po[0], 2.0, _TOL);

    // keep _check_u / _check_i referenced so the harness helpers are exercised
    _check_u("pass counter positive", _pass > 0 ? 1 : 0, 1);
    _check_i("fail counter type", (ISize) 0, 0);

    // A mask from a wider arity used to index past the source array; it now refuses to zero.
    Vec3 const sw_in = { .x = 1.0, .y = 2.0, .z = 3.0 };
    _check_f("swizzle_2 cross-arity mask refuses", math_vec3_swizzle_2(sw_in, (ISize) 0x31 /* lane 0 = index 1, lane 2 = index 3 (past the arity) */).x, 0.0, 0.0);
    _check_f("swizzle_2 own-arity mask still swizzles", math_vec3_swizzle_2(sw_in, MATH_SWIZZLE_ZYX).x, 3.0, 0.0);

    return _check_finish();
}