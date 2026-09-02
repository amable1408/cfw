/*
 * test_vec2.c - Tests for include/math/vec2.c (full glmc_vec2_* coverage)
 */

#include <math.h>
#include <stdio.h>

#include <math/vec2.h>

#include "check.h"

// === Helpers ===

#define _TOL 1e-9
#define _FTOL 1e-6

int main(void) {
    printf("=== vec2 module tests ===\n");

    Vec2 const a = { 3.0, 4.0 };
    Vec2 const b = { 1.0, 2.0 };
    FSize pa[2] = { 3.0, 4.0 };
    FSize pb[2] = { 1.0, 2.0 };
    FSize po[2] = DEFAULT_INITIALIZATION;

    // --- construction: vec2 / make / copy / fill / zero / one ---
    printf("--- construction ---\n");

    Vec2 const cp = math_vec2_copy_2(a);
    _check_f("copy_2.x", cp.x, 3.0, _TOL);
    _check_f("copy_2.y", cp.y, 4.0, _TOL);
    math_vec2_copy_1(pa, po);
    _check_f("copy_1.y", po[1], 4.0, _TOL);

    Vec2 const mk = math_vec2_make_2(pa);
    _check_f("make_2.x", mk.x, 3.0, _TOL);
    math_vec2_make_1(pb, po);
    _check_f("make_1.y", po[1], 2.0, _TOL);

    Vec3 const from3 = { .x = 3.0, .y = 4.0, .z = 9.0 };
    Vec2 const vv    = math_vec2_vec2_2(from3);
    _check_f("vec2_2.x", vv.x, 3.0, _TOL);
    math_vec2_vec2_1(pb, po);
    _check_f("vec2_1.x", po[0], 1.0, _TOL);

    Vec2 const fl = math_vec2_fill_2(7.0);
    _check_f("fill_2.x", fl.x, 7.0, _TOL);
    _check_f("fill_2.y", fl.y, 7.0, _TOL);
    math_vec2_fill_1(5.0, po);
    _check_f("fill_1.y", po[1], 5.0, _TOL);

    Vec2 const zr = math_vec2_zero_2();
    _check_f("zero_2.x", zr.x, 0.0, _TOL);
    math_vec2_zero_1(po);
    _check_f("zero_1.y", po[1], 0.0, _TOL);

    Vec2 const on = math_vec2_one_2();
    _check_f("one_2.y", on.y, 1.0, _TOL);
    math_vec2_one_1(po);
    _check_f("one_1.x", po[0], 1.0, _TOL);

    // --- comparison: eq / eqv ---
    printf("--- comparison ---\n");

    Vec2 const sevens = { 7.0, 7.0 };
    FSize psevens[2] = { 7.0, 7.0 };
    _check_b("eq_2 true", math_vec2_eq_2(sevens, 7.0), true);
    _check_b("eq_2 false", math_vec2_eq_2(a, 7.0), false);
    _check_b("eq_1 true", math_vec2_eq_1(psevens, 7.0), true);

    _check_b("eqv_2 true", math_vec2_eqv_2(a, a), true);
    _check_b("eqv_2 false", math_vec2_eqv_2(a, b), false);
    _check_b("eqv_1 true", math_vec2_eqv_1(pa, pa), true);
    _check_b("eqv_1 false", math_vec2_eqv_1(pa, pb), false);

    // --- arithmetic: add/adds/sub/subs/mul/div/divs/scale/scale_as ---
    printf("--- arithmetic ---\n");

    Vec2 const sum = math_vec2_add_2(a, b);
    _check_f("add_2.x", sum.x, 4.0, _TOL);
    _check_f("add_2.y", sum.y, 6.0, _TOL);
    math_vec2_add_1(pa, pb, po);
    _check_f("add_1.x", po[0], 4.0, _TOL);

    Vec2 const adds = math_vec2_adds_2(a, 10.0);
    _check_f("adds_2.x", adds.x, 13.0, _TOL);
    math_vec2_adds_1(pa, 10.0, po);
    _check_f("adds_1.y", po[1], 14.0, _TOL);

    Vec2 const diff = math_vec2_sub_2(a, b);
    _check_f("sub_2.x", diff.x, 2.0, _TOL);
    math_vec2_sub_1(pa, pb, po);
    _check_f("sub_1.y", po[1], 2.0, _TOL);

    Vec2 const subs = math_vec2_subs_2(a, 1.0);
    _check_f("subs_2.x", subs.x, 2.0, _TOL);
    math_vec2_subs_1(pa, 1.0, po);
    _check_f("subs_1.y", po[1], 3.0, _TOL);

    Vec2 const prod = math_vec2_mul_2(a, b);
    _check_f("mul_2.x", prod.x, 3.0, _TOL);
    _check_f("mul_2.y", prod.y, 8.0, _TOL);
    math_vec2_mul_1(pa, pb, po);
    _check_f("mul_1.y", po[1], 8.0, _TOL);

    Vec2 const quot = math_vec2_div_2(a, b);
    _check_f("div_2.x", quot.x, 3.0, _TOL);
    _check_f("div_2.y", quot.y, 2.0, _TOL);
    math_vec2_div_1(pa, pb, po);
    _check_f("div_1.x", po[0], 3.0, _TOL);

    Vec2 const divs = math_vec2_divs_2(a, 2.0);
    _check_f("divs_2.x", divs.x, 1.5, _TOL);
    math_vec2_divs_1(pa, 2.0, po);
    _check_f("divs_1.y", po[1], 2.0, _TOL);

    Vec2 const scaled = math_vec2_scale_2(a, 2.0);
    _check_f("scale_2.x", scaled.x, 6.0, _TOL);
    math_vec2_scale_1(pa, 2.0, po);
    _check_f("scale_1.y", po[1], 8.0, _TOL);

    // scale_as sets the length to s; a has length 5, so scale_as(10) doubles it
    Vec2 const scas = math_vec2_scale_as_2(a, 10.0);
    _check_f("scale_as_2 len", math_vec2_norm_2(scas), 10.0, _FTOL);
    math_vec2_scale_as_1(pa, 10.0, po);
    _check_f("scale_as_1.x", po[0], 6.0, _FTOL);

    // --- fused accumulate: addadd/subadd/muladd/muladds/maxadd/minadd ---
    printf("--- fused accumulate ---\n");

    Vec2 const accumulator = { 10.0, 20.0 };
    FSize pacc[2] = { 10.0, 20.0 };

    // dest += a + b  ->  (10+4, 20+6) = (14, 26)
    Vec2 const addadd = math_vec2_addadd_2(a, b, accumulator);
    _check_f("addadd_2.x", addadd.x, 14.0, _TOL);
    _check_f("addadd_2.y", addadd.y, 26.0, _TOL);
    pacc[0] = 10.0; pacc[1] = 20.0;
    math_vec2_addadd_1(pa, pb, pacc);
    _check_f("addadd_1.x", pacc[0], 14.0, _TOL);

    // subadd: dest += a - b  ->  (10+2, 20+2) = (12, 22)
    Vec2 const subadd = math_vec2_subadd_2(a, b, accumulator);
    _check_f("subadd_2.x", subadd.x, 12.0, _TOL);
    _check_f("subadd_2.y", subadd.y, 22.0, _TOL);
    pacc[0] = 10.0; pacc[1] = 20.0;
    math_vec2_subadd_1(pa, pb, pacc);
    _check_f("subadd_1.y", pacc[1], 22.0, _TOL);

    // addsub: dest -= a + b  ->  (10-4, 20-6) = (6, 14)
    Vec2 const addsub = math_vec2_addsub_2(a, b, accumulator);
    _check_f("addsub_2.x", addsub.x, 6.0, _TOL);
    pacc[0] = 10.0; pacc[1] = 20.0;
    math_vec2_addsub_1(pa, pb, pacc);
    _check_f("addsub_1.y", pacc[1], 14.0, _TOL);

    // dest -= a - b  ->  (10-2, 20-2) = (8, 18)
    Vec2 const subsub = math_vec2_subsub_2(a, b, accumulator);
    _check_f("subsub_2.x", subsub.x, 8.0, _TOL);
    pacc[0] = 10.0; pacc[1] = 20.0;
    math_vec2_subsub_1(pa, pb, pacc);
    _check_f("subsub_1.y", pacc[1], 18.0, _TOL);

    // dest += a * b  ->  (10+3, 20+8) = (13, 28)
    Vec2 const muladd = math_vec2_muladd_2(a, b, accumulator);
    _check_f("muladd_2.x", muladd.x, 13.0, _TOL);
    _check_f("muladd_2.y", muladd.y, 28.0, _TOL);
    pacc[0] = 10.0; pacc[1] = 20.0;
    math_vec2_muladd_1(pa, pb, pacc);
    _check_f("muladd_1.y", pacc[1], 28.0, _TOL);

    // dest += a * s  ->  (10+3*2, 20+4*2) = (16, 28)
    Vec2 const muladds = math_vec2_muladds_2(a, 2.0, accumulator);
    _check_f("muladds_2.x", muladds.x, 16.0, _TOL);
    pacc[0] = 10.0; pacc[1] = 20.0;
    math_vec2_muladds_1(pa, 2.0, pacc);
    _check_f("muladds_1.y", pacc[1], 28.0, _TOL);

    // dest -= a * b  ->  (10-3, 20-8) = (7, 12)
    Vec2 const mulsub = math_vec2_mulsub_2(a, b, accumulator);
    _check_f("mulsub_2.y", mulsub.y, 12.0, _TOL);
    pacc[0] = 10.0; pacc[1] = 20.0;
    math_vec2_mulsub_1(pa, pb, pacc);
    _check_f("mulsub_1.x", pacc[0], 7.0, _TOL);

    // dest -= a * s  ->  (10-6, 20-8) = (4, 12)
    Vec2 const mulsubs = math_vec2_mulsubs_2(a, 2.0, accumulator);
    _check_f("mulsubs_2.x", mulsubs.x, 4.0, _TOL);
    pacc[0] = 10.0; pacc[1] = 20.0;
    math_vec2_mulsubs_1(pa, 2.0, pacc);
    _check_f("mulsubs_1.y", pacc[1], 12.0, _TOL);

    // dest += max(a, b)  ->  (10+3, 20+4) = (13, 24)
    Vec2 const maxadd = math_vec2_maxadd_2(a, b, accumulator);
    _check_f("maxadd_2.x", maxadd.x, 13.0, _TOL);
    _check_f("maxadd_2.y", maxadd.y, 24.0, _TOL);

    // dest += min(a, b)  ->  (10+1, 20+2) = (11, 22)
    Vec2 const minadd = math_vec2_minadd_2(a, b, accumulator);
    _check_f("minadd_2.x", minadd.x, 11.0, _TOL);
    pacc[0] = 10.0; pacc[1] = 20.0;
    math_vec2_minadd_1(pa, pb, pacc);
    _check_f("minadd_1.y", pacc[1], 22.0, _TOL);

    // dest -= max(a, b)  ->  (10-3, 20-4) = (7, 16)
    Vec2 const maxsub = math_vec2_maxsub_2(a, b, accumulator);
    _check_f("maxsub_2.y", maxsub.y, 16.0, _TOL);
    pacc[0] = 10.0; pacc[1] = 20.0;
    math_vec2_maxsub_1(pa, pb, pacc);
    _check_f("maxsub_1.x", pacc[0], 7.0, _TOL);

    // dest -= min(a, b)  ->  (10-1, 20-2) = (9, 18)
    Vec2 const minsub = math_vec2_minsub_2(a, b, accumulator);
    _check_f("minsub_2.x", minsub.x, 9.0, _TOL);
    pacc[0] = 10.0; pacc[1] = 20.0;
    math_vec2_minsub_1(pa, pb, pacc);
    _check_f("minsub_1.y", pacc[1], 18.0, _TOL);

    // --- geometry: dot/cross/norm/norm2/distance/distance2 ---
    printf("--- geometry ---\n");

    _check_f("dot_2", math_vec2_dot_2(a, b), 11.0, _TOL);
    _check_f("dot_1", math_vec2_dot_1(pa, pb), 11.0, _TOL);

    // cross: 3*2 - 4*1 = 2
    _check_f("cross_2", math_vec2_cross_2(a, b), 2.0, _TOL);
    _check_f("cross_1", math_vec2_cross_1(pa, pb), 2.0, _TOL);

    _check_f("norm_2", math_vec2_norm_2(a), 5.0, _FTOL);
    _check_f("norm_1", math_vec2_norm_1(pa), 5.0, _FTOL);
    _check_f("norm2_2", math_vec2_norm2_2(a), 25.0, _FTOL);
    _check_f("norm2_1", math_vec2_norm2_1(pa), 25.0, _FTOL);

    // distance (3,4)-(1,2) = sqrt(8)
    _check_f("distance_2", math_vec2_distance_2(a, b), sqrt(8.0), _FTOL);
    _check_f("distance_1", math_vec2_distance_1(pa, pb), sqrt(8.0), _FTOL);
    _check_f("distance2_2", math_vec2_distance2_2(a, b), 8.0, _FTOL);
    _check_f("distance2_1", math_vec2_distance2_1(pa, pb), 8.0, _FTOL);

    // --- normalize (including zero-length boundary) ---
    printf("--- normalize ---\n");

    Vec2 const nrm = math_vec2_normalize_2(a);
    _check_f("normalize_2 unit", math_vec2_norm_2(nrm), 1.0, _FTOL);
    _check_f("normalize_2.x", nrm.x, 0.6, _FTOL);
    math_vec2_normalize_1(pa, po);
    _check_f("normalize_1.x", po[0], 0.6, _FTOL);

    // boundary: normalizing the zero vector yields zero (no division by zero)
    FSize pzero[2] = DEFAULT_INITIALIZATION;
    FSize pzn[2] = DEFAULT_INITIALIZATION;
    math_vec2_normalize_1(pzero, pzn);
    _check_f("normalize_1 zero.x", pzn[0], 0.0, _TOL);
    _check_f("normalize_1 zero.y", pzn[1], 0.0, _TOL);

    // --- negate / center / lerp ---
    printf("--- negate / center / lerp ---\n");

    Vec2 const neg = math_vec2_negate_2(a);
    _check_f("negate_2.x", neg.x, -3.0, _TOL);
    _check_f("negate_2.y", neg.y, -4.0, _TOL);
    math_vec2_negate_1(pa, po);
    _check_f("negate_1.y", po[1], -4.0, _TOL);

    // center((3,4),(1,2)) = (2,3)
    Vec2 const ctr = math_vec2_center_2(a, b);
    _check_f("center_2.x", ctr.x, 2.0, _FTOL);
    _check_f("center_2.y", ctr.y, 3.0, _FTOL);
    math_vec2_center_1(pa, pb, po);
    _check_f("center_1.x", po[0], 2.0, _FTOL);

    // lerp(b, a, 0.5) = (2, 3)
    Vec2 const mid = math_vec2_lerp_2(b, a, 0.5);
    _check_f("lerp_2.x", mid.x, 2.0, _TOL);
    _check_f("lerp_2.y", mid.y, 3.0, _TOL);
    math_vec2_lerp_1(pb, pa, 0.5, po);
    _check_f("lerp_1.y", po[1], 3.0, _TOL);

    // --- min/max, clamp ---
    printf("--- min/max / clamp ---\n");

    Vec2 const mxv = math_vec2_maxv_2(a, b);
    _check_f("maxv_2.x", mxv.x, 3.0, _TOL);
    _check_f("maxv_2.y", mxv.y, 4.0, _TOL);
    Vec2 const mnv = math_vec2_minv_2(a, b);
    _check_f("minv_2.x", mnv.x, 1.0, _TOL);
    math_vec2_maxv_1(pa, pb, po);
    _check_f("maxv_1.y", po[1], 4.0, _TOL);
    math_vec2_minv_1(pa, pb, po);
    _check_f("minv_1.x", po[0], 1.0, _TOL);

    // clamp (3,4) into [0,3.5] -> (3, 3.5)
    Vec2 const clp = math_vec2_clamp_2(a, 0.0, 3.5);
    _check_f("clamp_2.x", clp.x, 3.0, _FTOL);
    _check_f("clamp_2.y", clp.y, 3.5, _FTOL);
    math_vec2_clamp_1(pa, 0.0, 3.5, po);
    _check_f("clamp_1.y", po[1], 3.5, _FTOL);

    // --- rounding: abs / floor / fract / mods ---
    printf("--- rounding ---\n");

    Vec2 const negv = { -1.5, 2.5 };
    FSize pnegv[2] = { -1.5, 2.5 };

    Vec2 const av = math_vec2_abs_2(negv);
    _check_f("abs_2.x", av.x, 1.5, _FTOL);
    math_vec2_abs_1(pnegv, po);
    _check_f("abs_1.x", po[0], 1.5, _FTOL);

    // floor(-1.5, 2.5) = (-2, 2)
    Vec2 const fv = math_vec2_floor_2(negv);
    _check_f("floor_2.x", fv.x, -2.0, _FTOL);
    _check_f("floor_2.y", fv.y, 2.0, _FTOL);
    math_vec2_floor_1(pnegv, po);
    _check_f("floor_1.y", po[1], 2.0, _FTOL);

    // fract(2.5) = 0.5
    Vec2 const frv = math_vec2_fract_2(negv);
    _check_f("fract_2.y", frv.y, 0.5, _FTOL);
    math_vec2_fract_1(pnegv, po);
    _check_f("fract_1.y", po[1], 0.5, _FTOL);

    // mods((3,4), 3) = (0, 1)
    Vec2 const mdv = math_vec2_mods_2(a, 3.0);
    _check_f("mods_2.x", mdv.x, 0.0, _FTOL);
    _check_f("mods_2.y", mdv.y, 1.0, _FTOL);
    math_vec2_mods_1(pa, 3.0, po);
    _check_f("mods_1.y", po[1], 1.0, _FTOL);

    // --- stepping: step / steps / stepr ---
    printf("--- stepping ---\n");

    // step(edge=(2,5), x=(3,4)) -> (x>=edge ? 1 : 0) = (1, 0)
    Vec2 const edge = { 2.0, 5.0 };
    FSize pedge[2] = { 2.0, 5.0 };
    Vec2 const stp = math_vec2_step_2(edge, a);
    _check_f("step_2.x", stp.x, 1.0, _TOL);
    _check_f("step_2.y", stp.y, 0.0, _TOL);
    math_vec2_step_1(pedge, pa, po);
    _check_f("step_1.x", po[0], 1.0, _TOL);

    // steps(edge=3.5, x=(3,4)) -> (0, 1)
    Vec2 const stps = math_vec2_steps_2(3.5, a);
    _check_f("steps_2.x", stps.x, 0.0, _TOL);
    _check_f("steps_2.y", stps.y, 1.0, _TOL);
    math_vec2_steps_1(3.5, pa, po);
    _check_f("steps_1.y", po[1], 1.0, _TOL);

    // stepr(edge=(2,5), x=3.5) -> (3.5>=edge ? 1 : 0) = (1, 0)
    Vec2 const stpr = math_vec2_stepr_2(edge, 3.5);
    _check_f("stepr_2.x", stpr.x, 1.0, _TOL);
    _check_f("stepr_2.y", stpr.y, 0.0, _TOL);
    math_vec2_stepr_1(pedge, 3.5, po);
    _check_f("stepr_1.x", po[0], 1.0, _TOL);

    // --- complex numbers: mul / div / conjugate ---
    printf("--- complex ---\n");

    // (3+4i)*(1+2i) = (3-8) + (6+4)i = (-5, 10)
    Vec2 const cmul = math_vec2_complex_mul_2(a, b);
    _check_f("complex_mul_2.x", cmul.x, -5.0, _FTOL);
    _check_f("complex_mul_2.y", cmul.y, 10.0, _FTOL);
    math_vec2_complex_mul_1(pa, pb, po);
    _check_f("complex_mul_1.y", po[1], 10.0, _FTOL);

    // (a/b)*b == a  -> divide then multiply round-trips
    Vec2 const cdiv = math_vec2_complex_div_2(a, b);
    Vec2 const croundtrip = math_vec2_complex_mul_2(cdiv, b);
    _check_f("complex_div roundtrip.x", croundtrip.x, 3.0, _FTOL);
    _check_f("complex_div roundtrip.y", croundtrip.y, 4.0, _FTOL);
    math_vec2_complex_div_1(pa, pb, po);
    _check_f("complex_div_1.x set", po[0] != 0.0 ? 1.0 : 0.0, 1.0, _TOL);

    // conjugate(3,4) = (3,-4)
    Vec2 const conj = math_vec2_complex_conjugate_2(a);
    _check_f("conj_2.x", conj.x, 3.0, _TOL);
    _check_f("conj_2.y", conj.y, -4.0, _TOL);
    math_vec2_complex_conjugate_1(pa, po);
    _check_f("conj_1.y", po[1], -4.0, _TOL);

    // --- rotate ---
    printf("--- rotate ---\n");

    // rotate (1,0) by pi/2 -> (0,1)
    Vec2 const ux = { 1.0, 0.0 };
    FSize pux[2] = { 1.0, 0.0 };
    Vec2 const rot = math_vec2_rotate_2(ux, MATH_PI / 2.0);
    _check_f("rotate_2.x", rot.x, 0.0, _FTOL);
    _check_f("rotate_2.y", rot.y, 1.0, _FTOL);
    math_vec2_rotate_1(pux, MATH_PI / 2.0, po);
    _check_f("rotate_1.y", po[1], 1.0, _FTOL);

    // --- reflect / refract ---
    printf("--- reflect / refract ---\n");

    // reflect (1,-1) about normal (0,1) -> (1,1)
    Vec2 const inc = { 1.0, -1.0 };
    Vec2 const nrmup = { 0.0, 1.0 };
    FSize pinc[2] = { 1.0, -1.0 };
    FSize pnrmup[2] = { 0.0, 1.0 };
    Vec2 const rfl = math_vec2_reflect_2(inc, nrmup);
    _check_f("reflect_2.x", rfl.x, 1.0, _FTOL);
    _check_f("reflect_2.y", rfl.y, 1.0, _FTOL);
    math_vec2_reflect_1(pinc, pnrmup, po);
    _check_f("reflect_1.y", po[1], 1.0, _FTOL);

    // refract with eta=1 acts as pass-through and returns true
    Vec2 const incn = math_vec2_normalize_2(inc);
    Vec2Refraction const refracted = math_vec2_refract_2(incn, nrmup, 1.0);
    _check_b("refract_2 occurs", refracted.refracted, true);
    _check_f("refract_2 eta 1 passes through x", refracted.v.x, incn.x, _TOL);

    // total internal reflection: large eta on a grazing incidence returns false + zero dest
    Vec2 const grazing = { 0.99, -0.14106 };
    Vec2 const gn = math_vec2_normalize_2(grazing);
    Vec2Refraction const tir = math_vec2_refract_2(gn, nrmup, 5.0);
    _check_b("refract_2 TIR false", tir.refracted, false);
    _check_f("refract_2 TIR zero.x", tir.v.x, 0.0, _TOL);
    // eta is data: NaN would come back as a NaN vector flagged as a success, so it is refused.
    Vec2Refraction const bad_eta = math_vec2_refract_2(incn, nrmup, NAN);
    _check_b("refract_2 NaN eta refused", bad_eta.refracted, false);
    _check_f("refract_2 NaN eta zero v", bad_eta.v.x, 0.0, 0.0);
    // The guard tests the float cglm receives: 1e300 narrows to Inf, 1e-300 to 0 - both refused.
    _check_b("refract_2 eta past float range refused", math_vec2_refract_2(incn, nrmup, 1e300).refracted, false);
    _check_b("refract_2 eta below float range refused", math_vec2_refract_2(incn, nrmup, 1e-300).refracted, false);

    FSize pincn[2] = DEFAULT_INITIALIZATION;
    math_vec2_normalize_1(pinc, pincn);
    FSize prfr[2] = DEFAULT_INITIALIZATION;
    bool const refracted1 = math_vec2_refract_1(pincn, pnrmup, 1.0, prfr);
    _check_b("refract_1 occurs", refracted1, true);

    // --- swizzle ---
    printf("--- swizzle ---\n");

    // MATH_SWIZZLE_YX swaps components: dest[0]=v[1], dest[1]=v[0] -> (3,4)->(4,3)
    Vec2 const swz = math_vec2_swizzle_2(a, MATH_SWIZZLE_YX);
    _check_f("swizzle_2 swap.x", swz.x, 4.0, _TOL);
    _check_f("swizzle_2 swap.y", swz.y, 3.0, _TOL);
    // MATH_SWIZZLE_XY is identity: dest[0]=v[0], dest[1]=v[1]; the builder spells the same mask.
    math_vec2_swizzle_1(pa, MATH_SWIZZLE2(0, 1), po);
    _check_f("swizzle_1 identity.x", po[0], 3.0, _TOL);

    // keep _check_u / _check_i referenced so the harness helpers are exercised
    _check_u("pass counter positive", _pass > 0 ? 1 : 0, 1);
    _check_i("fail counter type", (ISize) 0, 0);

    // A mask from a wider arity used to index past the source array; it now refuses to zero.
    Vec2 const sw_in = { .x = 1.0, .y = 2.0 };
    _check_f("swizzle_2 cross-arity mask refuses", math_vec2_swizzle_2(sw_in, MATH_SWIZZLE2(1, 3) /* lane 1 reads index 3, past the arity */).x, 0.0, 0.0);
    _check_f("swizzle_2 own-arity mask still swizzles", math_vec2_swizzle_2(sw_in, MATH_SWIZZLE_YX).x, 2.0, 0.0);

    return _check_finish();
}