/*
 * test_vec4.c - Tests for include/math/vec4.c (full glmc_vec4_* coverage)
 */

#include <math.h>
#include <stdio.h>

#include <math/vec4.h>

#include "check.h"

// === Helpers ===

#define _TOL 1e-9
#define _FTOL 1e-6

int main(void) {
    printf("=== vec4 module tests ===\n");

    Vec4 const a = { 1.0, 2.0, 2.0, 4.0 };
    Vec4 const b = { 4.0, 3.0, 2.0, 1.0 };
    FSize pa[4] = { 1.0, 2.0, 2.0, 4.0 };
    FSize pb[4] = { 4.0, 3.0, 2.0, 1.0 };
    FSize po[4] = DEFAULT_INITIALIZATION;

    // --- construction: vec4 / make / copy / copy3 / fill / broadcast / zero / one ---
    printf("--- construction ---\n");

    Vec4 const cp = math_vec4_copy_2(a);
    _check_f("copy_2.x", cp.x, 1.0, _TOL);
    _check_f("copy_2.w", cp.w, 4.0, _TOL);
    math_vec4_copy_1(pa, po);
    _check_f("copy_1.w", po[3], 4.0, _TOL);

    Vec4 const ucp = math_vec4_ucopy_2(a);
    _check_f("ucopy_2.x", ucp.x, 1.0, _TOL);
    math_vec4_ucopy_1(pa, po);
    _check_f("ucopy_1.w", po[3], 4.0, _TOL);

    // copy3: first three components as Vec3
    Vec3 const cp3 = math_vec4_copy3_2(a);
    _check_f("copy3_2.x", cp3.x, 1.0, _TOL);
    _check_f("copy3_2.z", cp3.z, 2.0, _TOL);
    FSize po3[3] = DEFAULT_INITIALIZATION;
    math_vec4_copy3_1(pa, po3);
    _check_f("copy3_1.y", po3[1], 2.0, _TOL);

    Vec4 const mk = math_vec4_make_2(pa);
    _check_f("make_2.x", mk.x, 1.0, _TOL);
    math_vec4_make_1(pb, po);
    _check_f("make_1.w", po[3], 1.0, _TOL);

    // vec4(vec3, last): build (v.xyz, last)
    Vec3 const v3 = { 1.0, 2.0, 3.0 };
    FSize pv3[3] = { 1.0, 2.0, 3.0 };
    Vec4 const vv = math_vec4_vec4_2(v3, 9.0);
    _check_f("vec4_2.z", vv.z, 3.0, _TOL);
    _check_f("vec4_2.w", vv.w, 9.0, _TOL);
    math_vec4_vec4_1(pv3, 9.0, po);
    _check_f("vec4_1.w", po[3], 9.0, _TOL);
    _check_f("vec4_1.x", po[0], 1.0, _TOL);

    Vec4 const fl = math_vec4_fill_2(7.0);
    _check_f("fill_2.x", fl.x, 7.0, _TOL);
    _check_f("fill_2.w", fl.w, 7.0, _TOL);
    math_vec4_fill_1(5.0, po);
    _check_f("fill_1.z", po[2], 5.0, _TOL);

    Vec4 const bc = math_vec4_broadcast_2(3.0);
    _check_f("broadcast_2.y", bc.y, 3.0, _TOL);
    math_vec4_broadcast_1(6.0, po);
    _check_f("broadcast_1.w", po[3], 6.0, _TOL);

    Vec4 const zr = math_vec4_zero_2();
    _check_f("zero_2.x", zr.x, 0.0, _TOL);
    math_vec4_zero_1(po);
    _check_f("zero_1.w", po[3], 0.0, _TOL);

    Vec4 const on = math_vec4_one_2();
    _check_f("one_2.w", on.w, 1.0, _TOL);
    math_vec4_one_1(po);
    _check_f("one_1.x", po[0], 1.0, _TOL);

    // --- comparison: eq / eq_eps / eq_all / eqv / eqv_eps ---
    printf("--- comparison ---\n");

    Vec4 const sevens = { 7.0, 7.0, 7.0, 7.0 };
    FSize psevens[4] = { 7.0, 7.0, 7.0, 7.0 };
    _check_b("eq_2 true", math_vec4_eq_2(sevens, 7.0), true);
    _check_b("eq_2 false", math_vec4_eq_2(a, 7.0), false);
    _check_b("eq_1 true", math_vec4_eq_1(psevens, 7.0), true);

    _check_b("eq_eps_2 true", math_vec4_eq_eps_2(sevens, 7.0), true);
    _check_b("eq_eps_1 true", math_vec4_eq_eps_1(psevens, 7.0), true);

    _check_b("eq_all_2 true", math_vec4_eq_all_2(sevens), true);
    _check_b("eq_all_2 false", math_vec4_eq_all_2(a), false);
    _check_b("eq_all_1 true", math_vec4_eq_all_1(psevens), true);

    _check_b("eqv_2 true", math_vec4_eqv_2(a, a), true);
    _check_b("eqv_2 false", math_vec4_eqv_2(a, b), false);
    _check_b("eqv_1 true", math_vec4_eqv_1(pa, pa), true);
    _check_b("eqv_1 false", math_vec4_eqv_1(pa, pb), false);

    _check_b("eqv_eps_2 true", math_vec4_eqv_eps_2(a, a), true);
    _check_b("eqv_eps_1 false", math_vec4_eqv_eps_1(pa, pb), false);

    // --- validity: isnan / isinf / isvalid ---
    printf("--- validity ---\n");

    _check_b("isvalid_2 true", math_vec4_isvalid_2(a), true);
    _check_b("isvalid_1 true", math_vec4_isvalid_1(pa), true);
    _check_b("isnan_2 false", math_vec4_isnan_2(a), false);
    _check_b("isinf_2 false", math_vec4_isinf_2(a), false);

    Vec4 const bad = { (FSize) NAN, 0.0, 0.0, 0.0 };
    FSize pbad[4] = { (FSize) INFINITY, 0.0, 0.0, 0.0 };
    _check_b("isnan_2 true", math_vec4_isnan_2(bad), true);
    _check_b("isvalid_2 false", math_vec4_isvalid_2(bad), false);
    _check_b("isinf_1 true", math_vec4_isinf_1(pbad), true);
    _check_b("isvalid_1 false", math_vec4_isvalid_1(pbad), false);

    // --- arithmetic: add/adds/sub/subs/mul/mulv/div/divs/scale/scale_as ---
    printf("--- arithmetic ---\n");

    // a + b = (5, 5, 4, 5)
    Vec4 const sum = math_vec4_add_2(a, b);
    _check_f("add_2.x", sum.x, 5.0, _TOL);
    _check_f("add_2.z", sum.z, 4.0, _TOL);
    math_vec4_add_1(pa, pb, po);
    _check_f("add_1.w", po[3], 5.0, _TOL);

    Vec4 const adds = math_vec4_adds_2(a, 10.0);
    _check_f("adds_2.x", adds.x, 11.0, _TOL);
    math_vec4_adds_1(pa, 10.0, po);
    _check_f("adds_1.w", po[3], 14.0, _TOL);

    // a - b = (-3, -1, 0, 3)
    Vec4 const diff = math_vec4_sub_2(a, b);
    _check_f("sub_2.x", diff.x, -3.0, _TOL);
    _check_f("sub_2.w", diff.w, 3.0, _TOL);
    math_vec4_sub_1(pa, pb, po);
    _check_f("sub_1.z", po[2], 0.0, _TOL);

    Vec4 const subs = math_vec4_subs_2(a, 1.0);
    _check_f("subs_2.x", subs.x, 0.0, _TOL);
    math_vec4_subs_1(pa, 1.0, po);
    _check_f("subs_1.w", po[3], 3.0, _TOL);

    // a * b = (4, 6, 4, 4)
    Vec4 const prod = math_vec4_mul_2(a, b);
    _check_f("mul_2.x", prod.x, 4.0, _TOL);
    _check_f("mul_2.y", prod.y, 6.0, _TOL);
    math_vec4_mul_1(pa, pb, po);
    _check_f("mul_1.z", po[2], 4.0, _TOL);

    Vec4 const prodv = math_vec4_mulv_2(a, b);
    _check_f("mulv_2.y", prodv.y, 6.0, _TOL);
    math_vec4_mulv_1(pa, pb, po);
    _check_f("mulv_1.x", po[0], 4.0, _TOL);

    // a / b = (0.25, 0.6667, 1, 4)
    Vec4 const quot = math_vec4_div_2(a, b);
    _check_f("div_2.x", quot.x, 0.25, _FTOL);
    _check_f("div_2.w", quot.w, 4.0, _FTOL);
    math_vec4_div_1(pa, pb, po);
    _check_f("div_1.z", po[2], 1.0, _FTOL);

    Vec4 const divs = math_vec4_divs_2(a, 2.0);
    _check_f("divs_2.w", divs.w, 2.0, _TOL);
    math_vec4_divs_1(pa, 2.0, po);
    _check_f("divs_1.x", po[0], 0.5, _TOL);

    Vec4 const scaled = math_vec4_scale_2(a, 2.0);
    _check_f("scale_2.x", scaled.x, 2.0, _TOL);
    math_vec4_scale_1(pa, 2.0, po);
    _check_f("scale_1.w", po[3], 8.0, _TOL);

    // scale_as sets the length to s; a has length 5, so scale_as(10) doubles it
    Vec4 const scas = math_vec4_scale_as_2(a, 10.0);
    _check_f("scale_as_2 len", math_vec4_norm_2(scas), 10.0, _FTOL);
    math_vec4_scale_as_1(pa, 10.0, po);
    _check_f("scale_as_1.x", po[0], 2.0, _FTOL);

    // --- fused accumulate: addadd/subadd/addsub/subsub/muladd/muladds/mulsub/mulsubs/maxadd/minadd/maxsub/minsub ---
    printf("--- fused accumulate ---\n");

    Vec4 const accumulator = { 10.0, 20.0, 30.0, 40.0 };
    FSize pacc[4] = { 10.0, 20.0, 30.0, 40.0 };

    // dest += a + b  ->  (10+5, 20+5, 30+4, 40+5)
    Vec4 const addadd = math_vec4_addadd_2(a, b, accumulator);
    _check_f("addadd_2.x", addadd.x, 15.0, _TOL);
    _check_f("addadd_2.z", addadd.z, 34.0, _TOL);
    pacc[0] = 10.0; pacc[1] = 20.0; pacc[2] = 30.0; pacc[3] = 40.0;
    math_vec4_addadd_1(pa, pb, pacc);
    _check_f("addadd_1.w", pacc[3], 45.0, _TOL);

    // subadd: dest += a - b  ->  (10-3, 20-1, 30+0, 40+3)
    Vec4 const subadd = math_vec4_subadd_2(a, b, accumulator);
    _check_f("subadd_2.x", subadd.x, 7.0, _TOL);
    _check_f("subadd_2.w", subadd.w, 43.0, _TOL);
    pacc[0] = 10.0; pacc[1] = 20.0; pacc[2] = 30.0; pacc[3] = 40.0;
    math_vec4_subadd_1(pa, pb, pacc);
    _check_f("subadd_1.y", pacc[1], 19.0, _TOL);

    // addsub: dest -= a + b  ->  (10-5, 20-5, 30-4, 40-5)
    Vec4 const addsub = math_vec4_addsub_2(a, b, accumulator);
    _check_f("addsub_2.x", addsub.x, 5.0, _TOL);
    pacc[0] = 10.0; pacc[1] = 20.0; pacc[2] = 30.0; pacc[3] = 40.0;
    math_vec4_addsub_1(pa, pb, pacc);
    _check_f("addsub_1.z", pacc[2], 26.0, _TOL);

    // subsub: dest -= a - b  ->  (10+3, 20+1, 30-0, 40-3)
    Vec4 const subsub = math_vec4_subsub_2(a, b, accumulator);
    _check_f("subsub_2.x", subsub.x, 13.0, _TOL);
    pacc[0] = 10.0; pacc[1] = 20.0; pacc[2] = 30.0; pacc[3] = 40.0;
    math_vec4_subsub_1(pa, pb, pacc);
    _check_f("subsub_1.w", pacc[3], 37.0, _TOL);

    // muladd: dest += a * b  ->  (10+4, 20+6, 30+4, 40+4)
    Vec4 const muladd = math_vec4_muladd_2(a, b, accumulator);
    _check_f("muladd_2.x", muladd.x, 14.0, _TOL);
    _check_f("muladd_2.y", muladd.y, 26.0, _TOL);
    pacc[0] = 10.0; pacc[1] = 20.0; pacc[2] = 30.0; pacc[3] = 40.0;
    math_vec4_muladd_1(pa, pb, pacc);
    _check_f("muladd_1.w", pacc[3], 44.0, _TOL);

    // muladds: dest += a * s  ->  (10+1*2, 20+2*2, 30+2*2, 40+4*2)
    Vec4 const muladds = math_vec4_muladds_2(a, 2.0, accumulator);
    _check_f("muladds_2.x", muladds.x, 12.0, _TOL);
    pacc[0] = 10.0; pacc[1] = 20.0; pacc[2] = 30.0; pacc[3] = 40.0;
    math_vec4_muladds_1(pa, 2.0, pacc);
    _check_f("muladds_1.w", pacc[3], 48.0, _TOL);

    // mulsub: dest -= a * b  ->  (10-4, 20-6, 30-4, 40-4)
    Vec4 const mulsub = math_vec4_mulsub_2(a, b, accumulator);
    _check_f("mulsub_2.y", mulsub.y, 14.0, _TOL);
    pacc[0] = 10.0; pacc[1] = 20.0; pacc[2] = 30.0; pacc[3] = 40.0;
    math_vec4_mulsub_1(pa, pb, pacc);
    _check_f("mulsub_1.x", pacc[0], 6.0, _TOL);

    // mulsubs: dest -= a * s  ->  (10-2, 20-4, 30-4, 40-8)
    Vec4 const mulsubs = math_vec4_mulsubs_2(a, 2.0, accumulator);
    _check_f("mulsubs_2.x", mulsubs.x, 8.0, _TOL);
    pacc[0] = 10.0; pacc[1] = 20.0; pacc[2] = 30.0; pacc[3] = 40.0;
    math_vec4_mulsubs_1(pa, 2.0, pacc);
    _check_f("mulsubs_1.w", pacc[3], 32.0, _TOL);

    // maxadd: dest += max(a, b)  ->  (10+4, 20+3, 30+2, 40+4)
    Vec4 const maxadd = math_vec4_maxadd_2(a, b, accumulator);
    _check_f("maxadd_2.x", maxadd.x, 14.0, _TOL);
    _check_f("maxadd_2.w", maxadd.w, 44.0, _TOL);
    pacc[0] = 10.0; pacc[1] = 20.0; pacc[2] = 30.0; pacc[3] = 40.0;
    math_vec4_maxadd_1(pa, pb, pacc);
    _check_f("maxadd_1.y", pacc[1], 23.0, _TOL);

    // minadd: dest += min(a, b)  ->  (10+1, 20+2, 30+2, 40+1)
    Vec4 const minadd = math_vec4_minadd_2(a, b, accumulator);
    _check_f("minadd_2.x", minadd.x, 11.0, _TOL);
    pacc[0] = 10.0; pacc[1] = 20.0; pacc[2] = 30.0; pacc[3] = 40.0;
    math_vec4_minadd_1(pa, pb, pacc);
    _check_f("minadd_1.w", pacc[3], 41.0, _TOL);

    // maxsub: dest -= max(a, b)  ->  (10-4, 20-3, 30-2, 40-4)
    Vec4 const maxsub = math_vec4_maxsub_2(a, b, accumulator);
    _check_f("maxsub_2.w", maxsub.w, 36.0, _TOL);
    pacc[0] = 10.0; pacc[1] = 20.0; pacc[2] = 30.0; pacc[3] = 40.0;
    math_vec4_maxsub_1(pa, pb, pacc);
    _check_f("maxsub_1.x", pacc[0], 6.0, _TOL);

    // minsub: dest -= min(a, b)  ->  (10-1, 20-2, 30-2, 40-1)
    Vec4 const minsub = math_vec4_minsub_2(a, b, accumulator);
    _check_f("minsub_2.x", minsub.x, 9.0, _TOL);
    pacc[0] = 10.0; pacc[1] = 20.0; pacc[2] = 30.0; pacc[3] = 40.0;
    math_vec4_minsub_1(pa, pb, pacc);
    _check_f("minsub_1.w", pacc[3], 39.0, _TOL);

    // --- geometry: dot/norm/norm2/norm_one/norm_inf/hadd/distance/distance2 ---
    printf("--- geometry ---\n");

    // dot(a,b) = 4+6+4+4 = 18
    _check_f("dot_2", math_vec4_dot_2(a, b), 18.0, _TOL);
    _check_f("dot_1", math_vec4_dot_1(pa, pb), 18.0, _TOL);

    // norm a = sqrt(1+4+4+16) = 5
    _check_f("norm_2", math_vec4_norm_2(a), 5.0, _FTOL);
    _check_f("norm_1", math_vec4_norm_1(pa), 5.0, _FTOL);
    _check_f("norm2_2", math_vec4_norm2_2(a), 25.0, _FTOL);
    _check_f("norm2_1", math_vec4_norm2_1(pa), 25.0, _FTOL);

    // norm_one = |1|+|2|+|2|+|4| = 9 ; norm_inf = 4
    _check_f("norm_one_2", math_vec4_norm_one_2(a), 9.0, _FTOL);
    _check_f("norm_one_1", math_vec4_norm_one_1(pa), 9.0, _FTOL);
    _check_f("norm_inf_2", math_vec4_norm_inf_2(a), 4.0, _FTOL);
    _check_f("norm_inf_1", math_vec4_norm_inf_1(pa), 4.0, _FTOL);

    // hadd = 1+2+2+4 = 9
    _check_f("hadd_2", math_vec4_hadd_2(a), 9.0, _FTOL);
    _check_f("hadd_1", math_vec4_hadd_1(pa), 9.0, _FTOL);

    // distance (a-b) = (-3,-1,0,3) -> sqrt(19)
    _check_f("distance_2", math_vec4_distance_2(a, b), sqrt(19.0), _FTOL);
    _check_f("distance_1", math_vec4_distance_1(pa, pb), sqrt(19.0), _FTOL);
    _check_f("distance2_2", math_vec4_distance2_2(a, b), 19.0, _FTOL);
    _check_f("distance2_1", math_vec4_distance2_1(pa, pb), 19.0, _FTOL);

    // --- normalize (including zero-length boundary) ---
    printf("--- normalize ---\n");

    Vec4 const nrm = math_vec4_normalize_2(a);
    _check_f("normalize_2 unit", math_vec4_norm_2(nrm), 1.0, _FTOL);
    _check_f("normalize_2.x", nrm.x, 0.2, _FTOL);
    math_vec4_normalize_1(pa, po);
    _check_f("normalize_1.x", po[0], 0.2, _FTOL);

    // boundary: normalizing the zero vector yields zero (no division by zero)
    FSize pzero[4] = DEFAULT_INITIALIZATION;
    FSize pzn[4] = DEFAULT_INITIALIZATION;
    math_vec4_normalize_1(pzero, pzn);
    _check_f("normalize_1 zero.x", pzn[0], 0.0, _TOL);
    _check_f("normalize_1 zero.w", pzn[3], 0.0, _TOL);

    // --- negate / min/max component / minv/maxv / clamp ---
    printf("--- negate / min-max / clamp ---\n");

    Vec4 const neg = math_vec4_negate_2(a);
    _check_f("negate_2.x", neg.x, -1.0, _TOL);
    _check_f("negate_2.w", neg.w, -4.0, _TOL);
    math_vec4_negate_1(pa, po);
    _check_f("negate_1.w", po[3], -4.0, _TOL);

    // max component of a = 4, min = 1
    _check_f("max_2", math_vec4_max_2(a), 4.0, _TOL);
    _check_f("max_1", math_vec4_max_1(pa), 4.0, _TOL);
    _check_f("min_2", math_vec4_min_2(a), 1.0, _TOL);
    _check_f("min_1", math_vec4_min_1(pa), 1.0, _TOL);

    // maxv(a,b) = (4,3,2,4), minv(a,b) = (1,2,2,1)
    Vec4 const mxv = math_vec4_maxv_2(a, b);
    _check_f("maxv_2.x", mxv.x, 4.0, _TOL);
    _check_f("maxv_2.w", mxv.w, 4.0, _TOL);
    Vec4 const mnv = math_vec4_minv_2(a, b);
    _check_f("minv_2.x", mnv.x, 1.0, _TOL);
    math_vec4_maxv_1(pa, pb, po);
    _check_f("maxv_1.y", po[1], 3.0, _TOL);
    math_vec4_minv_1(pa, pb, po);
    _check_f("minv_1.w", po[3], 1.0, _TOL);

    // clamp a into [1.5, 3.5] -> (1.5, 2, 2, 3.5)
    Vec4 const clp = math_vec4_clamp_2(a, 1.5, 3.5);
    _check_f("clamp_2.x", clp.x, 1.5, _FTOL);
    _check_f("clamp_2.w", clp.w, 3.5, _FTOL);
    math_vec4_clamp_1(pa, 1.5, 3.5, po);
    _check_f("clamp_1.y", po[1], 2.0, _FTOL);

    // --- lerp / lerpc / smoothinterp / smoothinterpc / cubic ---
    printf("--- interpolation ---\n");

    // lerp(a, b, 0.5) = midpoint (2.5, 2.5, 2, 2.5)
    Vec4 const mid = math_vec4_lerp_2(a, b, 0.5);
    _check_f("lerp_2.x", mid.x, 2.5, _FTOL);
    _check_f("lerp_2.w", mid.w, 2.5, _FTOL);
    math_vec4_lerp_1(pa, pb, 0.5, po);
    _check_f("lerp_1.z", po[2], 2.0, _FTOL);

    // lerpc clamps t; t=2 acts as t=1 -> equals b
    Vec4 const lc = math_vec4_lerpc_2(a, b, 2.0);
    _check_f("lerpc_2.x", lc.x, 4.0, _FTOL);
    math_vec4_lerpc_1(pa, pb, 2.0, po);
    _check_f("lerpc_1.w", po[3], 1.0, _FTOL);

    // smoothinterp(a, b, 0) = a ; smoothinterp(a, b, 1) = b
    Vec4 const si0 = math_vec4_smoothinterp_2(a, b, 0.0);
    _check_f("smoothinterp_2 t0.x", si0.x, 1.0, _FTOL);
    Vec4 const si1 = math_vec4_smoothinterp_2(a, b, 1.0);
    _check_f("smoothinterp_2 t1.x", si1.x, 4.0, _FTOL);
    math_vec4_smoothinterp_1(pa, pb, 1.0, po);
    _check_f("smoothinterp_1 t1.w", po[3], 1.0, _FTOL);

    // smoothinterpc clamps t; t=-1 acts as t=0 -> equals a
    Vec4 const sic = math_vec4_smoothinterpc_2(a, b, -1.0);
    _check_f("smoothinterpc_2.x", sic.x, 1.0, _FTOL);
    math_vec4_smoothinterpc_1(pa, pb, -1.0, po);
    _check_f("smoothinterpc_1.w", po[3], 4.0, _FTOL);

    // cubic(2) = (8, 4, 2, 1)
    Vec4 const cub = math_vec4_cubic_2(2.0);
    _check_f("cubic_2.x", cub.x, 8.0, _FTOL);
    _check_f("cubic_2.y", cub.y, 4.0, _FTOL);
    _check_f("cubic_2.z", cub.z, 2.0, _FTOL);
    _check_f("cubic_2.w", cub.w, 1.0, _FTOL);
    math_vec4_cubic_1(2.0, po);
    _check_f("cubic_1.x", po[0], 8.0, _FTOL);

    // --- rounding: abs / floor / fract / mods / sign / sqrt ---
    printf("--- rounding ---\n");

    Vec4 const negv = { -1.5, 2.5, -3.0, 0.0 };
    FSize pnegv[4] = { -1.5, 2.5, -3.0, 0.0 };

    Vec4 const av = math_vec4_abs_2(negv);
    _check_f("abs_2.x", av.x, 1.5, _FTOL);
    _check_f("abs_2.z", av.z, 3.0, _FTOL);
    math_vec4_abs_1(pnegv, po);
    _check_f("abs_1.x", po[0], 1.5, _FTOL);

    // floor(-1.5, 2.5, -3, 0) = (-2, 2, -3, 0)
    Vec4 const fv = math_vec4_floor_2(negv);
    _check_f("floor_2.x", fv.x, -2.0, _FTOL);
    _check_f("floor_2.y", fv.y, 2.0, _FTOL);
    math_vec4_floor_1(pnegv, po);
    _check_f("floor_1.y", po[1], 2.0, _FTOL);

    // fract(2.5) = 0.5
    Vec4 const frv = math_vec4_fract_2(negv);
    _check_f("fract_2.y", frv.y, 0.5, _FTOL);
    math_vec4_fract_1(pnegv, po);
    _check_f("fract_1.y", po[1], 0.5, _FTOL);

    // mods((1,2,2,4), 3) = (1, 2, 2, 1)
    Vec4 const mdv = math_vec4_mods_2(a, 3.0);
    _check_f("mods_2.x", mdv.x, 1.0, _FTOL);
    _check_f("mods_2.w", mdv.w, 1.0, _FTOL);
    math_vec4_mods_1(pa, 3.0, po);
    _check_f("mods_1.y", po[1], 2.0, _FTOL);

    // sign(-1.5, 2.5, -3, 0) = (-1, 1, -1, 0)
    Vec4 const sgn = math_vec4_sign_2(negv);
    _check_f("sign_2.x", sgn.x, -1.0, _FTOL);
    _check_f("sign_2.y", sgn.y, 1.0, _FTOL);
    _check_f("sign_2.w", sgn.w, 0.0, _FTOL);
    math_vec4_sign_1(pnegv, po);
    _check_f("sign_1.z", po[2], -1.0, _FTOL);

    // sqrt((1,4,9,16)) = (1,2,3,4)
    Vec4 const sqv = { 1.0, 4.0, 9.0, 16.0 };
    FSize psqv[4] = { 1.0, 4.0, 9.0, 16.0 };
    Vec4 const sq = math_vec4_sqrt_2(sqv);
    _check_f("sqrt_2.y", sq.y, 2.0, _FTOL);
    _check_f("sqrt_2.w", sq.w, 4.0, _FTOL);
    math_vec4_sqrt_1(psqv, po);
    _check_f("sqrt_1.z", po[2], 3.0, _FTOL);

    // --- stepping: step / steps / stepr / smoothstep / smoothstep_uni ---
    printf("--- stepping ---\n");

    // step(edge=(2,2,2,2), x=(1,2,2,4)) -> (x>=edge ? 1 : 0) = (0, 1, 1, 1)
    Vec4 const edge = { 2.0, 2.0, 2.0, 2.0 };
    FSize pedge[4] = { 2.0, 2.0, 2.0, 2.0 };
    Vec4 const stp = math_vec4_step_2(edge, a);
    _check_f("step_2.x", stp.x, 0.0, _TOL);
    _check_f("step_2.w", stp.w, 1.0, _TOL);
    math_vec4_step_1(pedge, pa, po);
    _check_f("step_1.y", po[1], 1.0, _TOL);

    // steps(edge=3.0, x=(1,2,2,4)) -> (0, 0, 0, 1)
    Vec4 const stps = math_vec4_steps_2(3.0, a);
    _check_f("steps_2.x", stps.x, 0.0, _TOL);
    _check_f("steps_2.w", stps.w, 1.0, _TOL);
    math_vec4_steps_1(3.0, pa, po);
    _check_f("steps_1.w", po[3], 1.0, _TOL);

    // stepr(edge=(2,2,2,2), x=3.0) -> (3>=edge ? 1 : 0) = (1,1,1,1)
    Vec4 const stpr = math_vec4_stepr_2(edge, 3.0);
    _check_f("stepr_2.x", stpr.x, 1.0, _TOL);
    math_vec4_stepr_1(pedge, 3.0, po);
    _check_f("stepr_1.w", po[3], 1.0, _TOL);

    // smoothstep(edge0=0, edge1=4, x=a); x<=edge0 ->0, x>=edge1 ->1
    Vec4 const e0 = { 0.0, 0.0, 0.0, 0.0 };
    Vec4 const e1 = { 4.0, 4.0, 4.0, 4.0 };
    FSize pe0[4] = { 0.0, 0.0, 0.0, 0.0 };
    FSize pe1[4] = { 4.0, 4.0, 4.0, 4.0 };
    Vec4 const ss = math_vec4_smoothstep_2(e0, e1, a);
    _check_f("smoothstep_2.w", ss.w, 1.0, _FTOL);
    math_vec4_smoothstep_1(pe0, pe1, pa, po);
    _check_f("smoothstep_1.w", po[3], 1.0, _FTOL);

    // smoothstep_uni(edge0=0, edge1=4, x=a) matches smoothstep with uniform edges
    Vec4 const ssu = math_vec4_smoothstep_uni_2(0.0, 4.0, a);
    _check_f("smoothstep_uni_2.w", ssu.w, 1.0, _FTOL);
    _check_f("smoothstep_uni_2.x", ssu.x, ss.x, _FTOL);
    math_vec4_smoothstep_uni_1(0.0, 4.0, pa, po);
    _check_f("smoothstep_uni_1.w", po[3], 1.0, _FTOL);

    // --- reflect / refract ---
    printf("--- reflect / refract ---\n");

    // reflect (1,-1,0,0) about normal (0,1,0,0) -> (1,1,0,0)
    Vec4 const inc = { 1.0, -1.0, 0.0, 0.0 };
    Vec4 const nrmup = { 0.0, 1.0, 0.0, 0.0 };
    FSize pinc[4] = { 1.0, -1.0, 0.0, 0.0 };
    FSize pnrmup[4] = { 0.0, 1.0, 0.0, 0.0 };
    Vec4 const rfl = math_vec4_reflect_2(inc, nrmup);
    _check_f("reflect_2.x", rfl.x, 1.0, _FTOL);
    _check_f("reflect_2.y", rfl.y, 1.0, _FTOL);
    math_vec4_reflect_1(pinc, pnrmup, po);
    _check_f("reflect_1.y", po[1], 1.0, _FTOL);

    // refract with eta=1 acts as pass-through and returns true
    Vec4 const incn = math_vec4_normalize_2(inc);
    Vec4Refraction const refracted = math_vec4_refract_2(incn, nrmup, 1.0);
    _check_b("refract_2 occurs", refracted.refracted, true);
    _check_f("refract_2 eta 1 passes through x", refracted.v.x, incn.x, _TOL);

    FSize pincn[4] = DEFAULT_INITIALIZATION;
    math_vec4_normalize_1(pinc, pincn);
    FSize prfr[4] = DEFAULT_INITIALIZATION;
    bool const refracted1 = math_vec4_refract_1(pincn, pnrmup, 1.0, prfr);
    _check_b("refract_1 occurs", refracted1, true);

    // total internal reflection: large eta at grazing incidence returns false + zero dest
    Vec4 const grazing = { 0.99, -0.14106, 0.0, 0.0 };
    Vec4 const gn = math_vec4_normalize_2(grazing);
    Vec4Refraction const tir = math_vec4_refract_2(gn, nrmup, 5.0);
    _check_b("refract_2 TIR false", tir.refracted, false);
    _check_f("refract_2 TIR zero.x", tir.v.x, 0.0, _TOL);
    Vec4Refraction const bad_eta = math_vec4_refract_2(incn, nrmup, 0.0);
    _check_b("refract_2 zero eta refused", bad_eta.refracted, false);
    _check_f("refract_2 zero eta zero v", bad_eta.v.w, 0.0, 0.0);
    _check_b("refract_2 eta past float range refused", math_vec4_refract_2(incn, nrmup, 1e300).refracted, false);

    // --- swizzle ---
    printf("--- swizzle ---\n");

    // GLM_SHUFFLE4(0,1,2,3) reverses: dest[i] = v[3-i] -> (1,2,2,4) -> (4,2,2,1)
    Vec4 const swz = math_vec4_swizzle_2(a, GLM_SHUFFLE4(0, 1, 2, 3));
    _check_f("swizzle_2 rev.x", swz.x, 4.0, _TOL);
    _check_f("swizzle_2 rev.w", swz.w, 1.0, _TOL);
    // GLM_SHUFFLE4(3,2,1,0) is identity: dest[i] = v[i]
    math_vec4_swizzle_1(pa, GLM_SHUFFLE4(3, 2, 1, 0), po);
    _check_f("swizzle_1 identity.x", po[0], 1.0, _TOL);
    _check_f("swizzle_1 identity.w", po[3], 4.0, _TOL);

    // keep _check_u / _check_i referenced so the harness helpers are exercised
    _check_u("pass counter positive", _pass > 0 ? 1 : 0, 1);
    _check_i("fail counter type", (ISize) 0, 0);

    return _check_finish();
}