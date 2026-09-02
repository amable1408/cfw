/*
 * test_ivec2.c - Tests for include/math/ivec2.c (full glmc_ivec2_* coverage)
 */

#include <math.h>
#include <stdio.h>

#include <math/ivec2.h>

#include "check.h"

// === Helpers ===

#define _FTOL 1e-6

int main(void) {
    printf("=== ivec2 module tests ===\n");

    IVec2 const a = { 3, 4 };
    IVec2 const b = { 1, 2 };
    ISize pa[2] = { 3, 4 };
    ISize pb[2] = { 1, 2 };
    ISize po[2] = DEFAULT_INITIALIZATION;

    // --- construction: ivec2 / copy / fill / zero / one ---
    printf("--- construction ---\n");

    IVec2 const cp = math_ivec2_copy_2(a);
    _check_i("copy_2.x", cp.x, 3);
    _check_i("copy_2.y", cp.y, 4);
    math_ivec2_copy_1(pa, po);
    _check_i("copy_1.y", po[1], 4);

    IVec3 const from3 = { .x = 3, .y = 4, .z = 9 };
    IVec2 const iv    = math_ivec2_ivec2_2(from3);
    _check_i("ivec2_2.x", iv.x, 3);
    math_ivec2_ivec2_1(pb, po);
    _check_i("ivec2_1.x", po[0], 1);

    IVec2 const fl = math_ivec2_fill_2(7);
    _check_i("fill_2.x", fl.x, 7);
    _check_i("fill_2.y", fl.y, 7);
    math_ivec2_fill_1(5, po);
    _check_i("fill_1.y", po[1], 5);

    IVec2 const zr = math_ivec2_zero_2();
    _check_i("zero_2.x", zr.x, 0);
    math_ivec2_zero_1(po);
    _check_i("zero_1.y", po[1], 0);

    IVec2 const on = math_ivec2_one_2();
    _check_i("one_2.y", on.y, 1);
    math_ivec2_one_1(po);
    _check_i("one_1.x", po[0], 1);

    // --- comparison: eq / eqv ---
    printf("--- comparison ---\n");

    IVec2 const sevens = { 7, 7 };
    ISize psevens[2] = { 7, 7 };
    _check_b("eq_2 true", math_ivec2_eq_2(sevens, 7), true);
    _check_b("eq_2 false", math_ivec2_eq_2(a, 7), false);
    _check_b("eq_1 true", math_ivec2_eq_1(psevens, 7), true);

    _check_b("eqv_2 true", math_ivec2_eqv_2(a, a), true);
    _check_b("eqv_2 false", math_ivec2_eqv_2(a, b), false);
    _check_b("eqv_1 true", math_ivec2_eqv_1(pa, pa), true);
    _check_b("eqv_1 false", math_ivec2_eqv_1(pa, pb), false);

    // --- arithmetic: add/adds/sub/subs/mul/div/divs/scale/mod ---
    printf("--- arithmetic ---\n");

    IVec2 const sum = math_ivec2_add_2(a, b);
    _check_i("add_2.x", sum.x, 4);
    _check_i("add_2.y", sum.y, 6);
    math_ivec2_add_1(pa, pb, po);
    _check_i("add_1.x", po[0], 4);

    IVec2 const adds = math_ivec2_adds_2(a, 10);
    _check_i("adds_2.x", adds.x, 13);
    math_ivec2_adds_1(pa, 10, po);
    _check_i("adds_1.y", po[1], 14);

    IVec2 const diff = math_ivec2_sub_2(a, b);
    _check_i("sub_2.x", diff.x, 2);
    math_ivec2_sub_1(pa, pb, po);
    _check_i("sub_1.y", po[1], 2);

    IVec2 const subs = math_ivec2_subs_2(a, 1);
    _check_i("subs_2.x", subs.x, 2);
    math_ivec2_subs_1(pa, 1, po);
    _check_i("subs_1.y", po[1], 3);

    IVec2 const prod = math_ivec2_mul_2(a, b);
    _check_i("mul_2.x", prod.x, 3);
    _check_i("mul_2.y", prod.y, 8);
    math_ivec2_mul_1(pa, pb, po);
    _check_i("mul_1.y", po[1], 8);

    IVec2 const quot = math_ivec2_div_2(a, b);
    _check_i("div_2.x", quot.x, 3);
    _check_i("div_2.y", quot.y, 2);
    math_ivec2_div_1(pa, pb, po);
    _check_i("div_1.x", po[0], 3);

    IVec2 const divs = math_ivec2_divs_2(a, 2);
    _check_i("divs_2.x", divs.x, 1);
    math_ivec2_divs_1(pa, 2, po);
    _check_i("divs_1.y", po[1], 2);

    IVec2 const scaled = math_ivec2_scale_2(a, 2);
    _check_i("scale_2.x", scaled.x, 6);
    math_ivec2_scale_1(pa, 2, po);
    _check_i("scale_1.y", po[1], 8);

    // mod((3,4),(2,3)) = (1, 1)
    IVec2 const bmod = { 2, 3 };
    ISize pbmod[2] = { 2, 3 };
    IVec2 const mdv = math_ivec2_mod_2(a, bmod);
    _check_i("mod_2.x", mdv.x, 1);
    _check_i("mod_2.y", mdv.y, 1);
    math_ivec2_mod_1(pa, pbmod, po);
    _check_i("mod_1.x", po[0], 1);

    // --- fused accumulate (vector): addadd/subadd/addsub/subsub ---
    printf("--- fused accumulate (vector) ---\n");

    IVec2 const accumulator = { 10, 20 };
    ISize pacc[2] = { 10, 20 };

    // dest += a + b  ->  (10+4, 20+6) = (14, 26)
    IVec2 const addadd = math_ivec2_addadd_2(a, b, accumulator);
    _check_i("addadd_2.x", addadd.x, 14);
    _check_i("addadd_2.y", addadd.y, 26);
    pacc[0] = 10; pacc[1] = 20;
    math_ivec2_addadd_1(pa, pb, pacc);
    _check_i("addadd_1.x", pacc[0], 14);

    // subadd: dest += a - b  ->  (10+2, 20+2) = (12, 22)
    IVec2 const subadd = math_ivec2_subadd_2(a, b, accumulator);
    _check_i("subadd_2.x", subadd.x, 12);
    _check_i("subadd_2.y", subadd.y, 22);
    pacc[0] = 10; pacc[1] = 20;
    math_ivec2_subadd_1(pa, pb, pacc);
    _check_i("subadd_1.y", pacc[1], 22);

    // addsub: dest -= a + b  ->  (10-4, 20-6) = (6, 14)
    IVec2 const addsub = math_ivec2_addsub_2(a, b, accumulator);
    _check_i("addsub_2.x", addsub.x, 6);
    pacc[0] = 10; pacc[1] = 20;
    math_ivec2_addsub_1(pa, pb, pacc);
    _check_i("addsub_1.y", pacc[1], 14);

    // subsub: dest -= a - b  ->  (10-2, 20-2) = (8, 18)
    IVec2 const subsub = math_ivec2_subsub_2(a, b, accumulator);
    _check_i("subsub_2.x", subsub.x, 8);
    pacc[0] = 10; pacc[1] = 20;
    math_ivec2_subsub_1(pa, pb, pacc);
    _check_i("subsub_1.y", pacc[1], 18);

    // --- fused accumulate (scalar): addadds/subadds/addsubs/subsubs/muladds/mulsubs ---
    printf("--- fused accumulate (scalar) ---\n");

    // addadds: dest += a + s  ->  (10+3+2, 20+4+2) = (15, 26)
    IVec2 const addadds = math_ivec2_addadds_2(a, 2, accumulator);
    _check_i("addadds_2.x", addadds.x, 15);
    _check_i("addadds_2.y", addadds.y, 26);
    pacc[0] = 10; pacc[1] = 20;
    math_ivec2_addadds_1(pa, 2, pacc);
    _check_i("addadds_1.x", pacc[0], 15);

    // subadds: dest += a - s  ->  (10+3-2, 20+4-2) = (11, 22)
    IVec2 const subadds = math_ivec2_subadds_2(a, 2, accumulator);
    _check_i("subadds_2.x", subadds.x, 11);
    pacc[0] = 10; pacc[1] = 20;
    math_ivec2_subadds_1(pa, 2, pacc);
    _check_i("subadds_1.y", pacc[1], 22);

    // addsubs: dest -= a + s  ->  (10-(3+2), 20-(4+2)) = (5, 14)
    IVec2 const addsubs = math_ivec2_addsubs_2(a, 2, accumulator);
    _check_i("addsubs_2.x", addsubs.x, 5);
    pacc[0] = 10; pacc[1] = 20;
    math_ivec2_addsubs_1(pa, 2, pacc);
    _check_i("addsubs_1.y", pacc[1], 14);

    // subsubs: dest -= a - s  ->  (10-(3-2), 20-(4-2)) = (9, 18)
    IVec2 const subsubs = math_ivec2_subsubs_2(a, 2, accumulator);
    _check_i("subsubs_2.x", subsubs.x, 9);
    pacc[0] = 10; pacc[1] = 20;
    math_ivec2_subsubs_1(pa, 2, pacc);
    _check_i("subsubs_1.y", pacc[1], 18);

    // muladds: dest += a * s  ->  (10+3*2, 20+4*2) = (16, 28)
    IVec2 const muladds = math_ivec2_muladds_2(a, 2, accumulator);
    _check_i("muladds_2.x", muladds.x, 16);
    pacc[0] = 10; pacc[1] = 20;
    math_ivec2_muladds_1(pa, 2, pacc);
    _check_i("muladds_1.y", pacc[1], 28);

    // mulsubs: dest -= a * s  ->  (10-6, 20-8) = (4, 12)
    IVec2 const mulsubs = math_ivec2_mulsubs_2(a, 2, accumulator);
    _check_i("mulsubs_2.x", mulsubs.x, 4);
    pacc[0] = 10; pacc[1] = 20;
    math_ivec2_mulsubs_1(pa, 2, pacc);
    _check_i("mulsubs_1.y", pacc[1], 12);

    // --- fused accumulate (mul / min / max) ---
    printf("--- fused accumulate (mul/min/max) ---\n");

    // muladd: dest += a * b  ->  (10+3, 20+8) = (13, 28)
    IVec2 const muladd = math_ivec2_muladd_2(a, b, accumulator);
    _check_i("muladd_2.x", muladd.x, 13);
    _check_i("muladd_2.y", muladd.y, 28);
    pacc[0] = 10; pacc[1] = 20;
    math_ivec2_muladd_1(pa, pb, pacc);
    _check_i("muladd_1.y", pacc[1], 28);

    // mulsub: dest -= a * b  ->  (10-3, 20-8) = (7, 12)
    IVec2 const mulsub = math_ivec2_mulsub_2(a, b, accumulator);
    _check_i("mulsub_2.y", mulsub.y, 12);
    pacc[0] = 10; pacc[1] = 20;
    math_ivec2_mulsub_1(pa, pb, pacc);
    _check_i("mulsub_1.x", pacc[0], 7);

    // maxadd: dest += max(a, b)  ->  (10+3, 20+4) = (13, 24)
    IVec2 const maxadd = math_ivec2_maxadd_2(a, b, accumulator);
    _check_i("maxadd_2.x", maxadd.x, 13);
    _check_i("maxadd_2.y", maxadd.y, 24);
    pacc[0] = 10; pacc[1] = 20;
    math_ivec2_maxadd_1(pa, pb, pacc);
    _check_i("maxadd_1.y", pacc[1], 24);

    // minadd: dest += min(a, b)  ->  (10+1, 20+2) = (11, 22)
    IVec2 const minadd = math_ivec2_minadd_2(a, b, accumulator);
    _check_i("minadd_2.x", minadd.x, 11);
    pacc[0] = 10; pacc[1] = 20;
    math_ivec2_minadd_1(pa, pb, pacc);
    _check_i("minadd_1.y", pacc[1], 22);

    // maxsub: dest -= max(a, b)  ->  (10-3, 20-4) = (7, 16)
    IVec2 const maxsub = math_ivec2_maxsub_2(a, b, accumulator);
    _check_i("maxsub_2.y", maxsub.y, 16);
    pacc[0] = 10; pacc[1] = 20;
    math_ivec2_maxsub_1(pa, pb, pacc);
    _check_i("maxsub_1.x", pacc[0], 7);

    // minsub: dest -= min(a, b)  ->  (10-1, 20-2) = (9, 18)
    IVec2 const minsub = math_ivec2_minsub_2(a, b, accumulator);
    _check_i("minsub_2.x", minsub.x, 9);
    pacc[0] = 10; pacc[1] = 20;
    math_ivec2_minsub_1(pa, pb, pacc);
    _check_i("minsub_1.y", pacc[1], 18);

    // --- geometry: dot/cross/distance/distance2 ---
    printf("--- geometry ---\n");

    _check_i("dot_2", math_ivec2_dot_2(a, b), 11);
    _check_i("dot_1", math_ivec2_dot_1(pa, pb), 11);

    // cross: 3*2 - 4*1 = 2
    _check_i("cross_2", math_ivec2_cross_2(a, b), 2);
    _check_i("cross_1", math_ivec2_cross_1(pa, pb), 2);

    // distance (3,4)-(1,2) = sqrt(8); distance2 = 8
    _check_f("distance_2", math_ivec2_distance_2(a, b), sqrt(8.0), _FTOL);
    _check_f("distance_1", math_ivec2_distance_1(pa, pb), sqrt(8.0), _FTOL);
    _check_i("distance2_2", math_ivec2_distance2_2(a, b), 8);
    _check_i("distance2_1", math_ivec2_distance2_1(pa, pb), 8);

    // --- min/max / clamp ---
    printf("--- min/max / clamp ---\n");

    IVec2 const mxv = math_ivec2_maxv_2(a, b);
    _check_i("maxv_2.x", mxv.x, 3);
    _check_i("maxv_2.y", mxv.y, 4);
    IVec2 const mnv = math_ivec2_minv_2(a, b);
    _check_i("minv_2.x", mnv.x, 1);
    math_ivec2_maxv_1(pa, pb, po);
    _check_i("maxv_1.y", po[1], 4);
    math_ivec2_minv_1(pa, pb, po);
    _check_i("minv_1.x", po[0], 1);

    // clamp (3,4) into [0,3] -> (3, 3)
    IVec2 const clp = math_ivec2_clamp_2(a, 0, 3);
    _check_i("clamp_2.x", clp.x, 3);
    _check_i("clamp_2.y", clp.y, 3);
    math_ivec2_clamp_1(pa, 0, 3, po);
    _check_i("clamp_1.y", po[1], 3);

    // --- absolute value (including negative boundary) ---
    printf("--- abs ---\n");

    // abs(-1, -2) = (1, 2); include negatives as a boundary
    IVec2 const negv = { -1, -2 };
    ISize pnegv[2] = { -1, -2 };
    IVec2 const av = math_ivec2_abs_2(negv);
    _check_i("abs_2.x", av.x, 1);
    _check_i("abs_2.y", av.y, 2);
    math_ivec2_abs_1(pnegv, po);
    _check_i("abs_1.x", po[0], 1);

    // boundary: abs of zero stays zero
    ISize pzero[2] = DEFAULT_INITIALIZATION;
    math_ivec2_abs_1(pzero, po);
    _check_i("abs_1 zero.x", po[0], 0);
    _check_i("abs_1 zero.y", po[1], 0);

    // clamp BOUNDS saturate at the ISize -> int boundary instead of truncating. Before the
    // fix ISIZE_MAX truncated to -1 and this read back -1: the clamp silently inverted.
    IVec2 const three   = { .x = 3, .y = 3 };
    IVec2 const sat     = math_ivec2_clamp_2(three, 0, ISIZE_MAX);
    IVec2 const sat_low = math_ivec2_clamp_2(three, ISIZE_MIN, 2);
    _check_i("clamp_2 with an ISIZE_MAX bound keeps the value", sat.x, 3);
    _check_i("clamp_2 with an ISIZE_MIN bound still clamps high", sat_low.x, 2);

    // Integer division by zero used to TRAP the process; both forms now refuse to the zeroed
    // vector. divs is checked on the CONVERTED int, so a 2^32 scalar (low 32 bits zero) refuses too.
    IVec2 const eight   = { .x = 8, .y = 8 };
    IVec2 const zero_in = { .x = 2, .y = 0 };
    IVec2 const dz      = math_ivec2_divs_2(eight, 0);
    _check_i("divs_2 by zero refuses to zero", dz.x, 0);
    IVec2 const dt      = math_ivec2_divs_2(eight, (ISize) 1 << 32);
    _check_i("divs_2 by 2^32 (truncates to 0) refuses to zero", dt.x, 0);
    IVec2 const dv      = math_ivec2_div_2(eight, zero_in);
    _check_i("div_2 with a zero component refuses the whole vector", dv.x, 0);
    IVec2 const ok      = math_ivec2_divs_2(eight, 2);
    _check_i("divs_2 by 2 still divides", ok.x, 4);

    // The other half of the trap set: INT_MIN / -1 does not fit an int and traps exactly like
    // a zero divisor; mod shares both. All refuse to the zeroed vector.
    IVec2 const int_min = { .x = INT_MIN, .y = 4 };
    _check_i("divs_2 INT_MIN by -1 refuses", math_ivec2_divs_2(int_min, -1).x, 0);
    _check_i("div_2 INT_MIN by -1 refuses", math_ivec2_div_2(int_min, (IVec2) { .x = -1, .y = 1 }).x, 0);
    _check_i("mod_2 by zero refuses", math_ivec2_mod_2(eight, zero_in).x, 0);
    _check_i("mod_2 INT_MIN by -1 refuses", math_ivec2_mod_2(int_min, (IVec2) { .x = -1, .y = 1 }).x, 0);
    _check_i("mod_2 by 3 still works", math_ivec2_mod_2(eight, (IVec2) { .x = 3, .y = 3 }).x, 2);

    // The raw _1 forms carry the same guard; exercise them on the refusal path too.
    ISize const raw_v[2] = { 8, 8 };
    ISize       raw_d[2] = { 7, 7 };
    math_ivec2_divs_1(raw_v, 0, raw_d);
    _check_i("divs_1 by zero refuses to zero (writes dest)", raw_d[0] + raw_d[1], 0);

    return _check_finish();
}