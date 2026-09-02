/*
 * test_ivec4.c - Tests for include/math/ivec4.c (full glmc_ivec4_* coverage)
 */

#include <math.h>
#include <stdio.h>

#include <math/ivec4.h>

#include "check.h"

// === Helpers ===

#define _FTOL 1e-6

int main(void) {
    printf("=== ivec4 module tests ===\n");

    IVec4 const a = { 3, 4, 5, 6 };
    IVec4 const b = { 1, 2, 3, 4 };
    ISize pa[4] = { 3, 4, 5, 6 };
    ISize pb[4] = { 1, 2, 3, 4 };
    ISize po[4] = DEFAULT_INITIALIZATION;

    // --- construction: ivec4 / copy / zero / one ---
    printf("--- construction ---\n");

    // ivec4(v3, last): build (7,8,9 | 10)
    IVec3 const v3 = { 7, 8, 9 };
    ISize pv3[3] = { 7, 8, 9 };
    IVec4 const ctor = math_ivec4_ivec4_2(v3, 10);
    _check_i("ivec4_2.x", ctor.x, 7);
    _check_i("ivec4_2.w", ctor.w, 10);
    math_ivec4_ivec4_1(pv3, 10, po);
    _check_i("ivec4_1.z", po[2], 9);
    _check_i("ivec4_1.w", po[3], 10);

    IVec4 const cp = math_ivec4_copy_2(a);
    _check_i("copy_2.x", cp.x, 3);
    _check_i("copy_2.w", cp.w, 6);
    math_ivec4_copy_1(pa, po);
    _check_i("copy_1.y", po[1], 4);

    IVec4 const zr = math_ivec4_zero_2();
    _check_i("zero_2.x", zr.x, 0);
    _check_i("zero_2.w", zr.w, 0);
    math_ivec4_zero_1(po);
    _check_i("zero_1.z", po[2], 0);

    IVec4 const on = math_ivec4_one_2();
    _check_i("one_2.y", on.y, 1);
    _check_i("one_2.w", on.w, 1);
    math_ivec4_one_1(po);
    _check_i("one_1.x", po[0], 1);

    // --- arithmetic: add/adds/sub/subs/mul/scale ---
    printf("--- arithmetic ---\n");

    IVec4 const sum = math_ivec4_add_2(a, b);
    _check_i("add_2.x", sum.x, 4);
    _check_i("add_2.w", sum.w, 10);
    math_ivec4_add_1(pa, pb, po);
    _check_i("add_1.z", po[2], 8);

    IVec4 const adds = math_ivec4_adds_2(a, 10);
    _check_i("adds_2.x", adds.x, 13);
    math_ivec4_adds_1(pa, 10, po);
    _check_i("adds_1.w", po[3], 16);

    IVec4 const diff = math_ivec4_sub_2(a, b);
    _check_i("sub_2.x", diff.x, 2);
    _check_i("sub_2.w", diff.w, 2);
    math_ivec4_sub_1(pa, pb, po);
    _check_i("sub_1.y", po[1], 2);

    IVec4 const subs = math_ivec4_subs_2(a, 1);
    _check_i("subs_2.x", subs.x, 2);
    math_ivec4_subs_1(pa, 1, po);
    _check_i("subs_1.w", po[3], 5);

    IVec4 const prod = math_ivec4_mul_2(a, b);
    _check_i("mul_2.x", prod.x, 3);
    _check_i("mul_2.w", prod.w, 24);
    math_ivec4_mul_1(pa, pb, po);
    _check_i("mul_1.z", po[2], 15);

    IVec4 const scaled = math_ivec4_scale_2(a, 2);
    _check_i("scale_2.x", scaled.x, 6);
    _check_i("scale_2.w", scaled.w, 12);
    math_ivec4_scale_1(pa, 3, po);
    _check_i("scale_1.y", po[1], 12);

    // --- fused accumulate ---
    printf("--- fused accumulate ---\n");

    IVec4 const accumulator = { 10, 20, 30, 40 };
    ISize pacc[4] = { 10, 20, 30, 40 };

    // dest += a + b -> (10+4, 20+6, 30+8, 40+10) = (14,26,38,50)
    IVec4 const addadd = math_ivec4_addadd_2(a, b, accumulator);
    _check_i("addadd_2.x", addadd.x, 14);
    _check_i("addadd_2.w", addadd.w, 50);
    pacc[0] = 10; pacc[1] = 20; pacc[2] = 30; pacc[3] = 40;
    math_ivec4_addadd_1(pa, pb, pacc);
    _check_i("addadd_1.z", pacc[2], 38);

    // dest += a + s -> (10+3+2, ...) with s=2 -> (15, 26, 37, 48)
    IVec4 const addadds = math_ivec4_addadds_2(a, 2, accumulator);
    _check_i("addadds_2.x", addadds.x, 15);
    pacc[0] = 10; pacc[1] = 20; pacc[2] = 30; pacc[3] = 40;
    math_ivec4_addadds_1(pa, 2, pacc);
    _check_i("addadds_1.w", pacc[3], 48);

    // dest -= a + b -> (10-4, 20-6, 30-8, 40-10) = (6,14,22,30)
    IVec4 const addsub = math_ivec4_addsub_2(a, b, accumulator);
    _check_i("addsub_2.x", addsub.x, 6);
    pacc[0] = 10; pacc[1] = 20; pacc[2] = 30; pacc[3] = 40;
    math_ivec4_addsub_1(pa, pb, pacc);
    _check_i("addsub_1.w", pacc[3], 30);

    // dest -= a + s (s=2) -> (10-5, ...) = (5, 14, 23, 32)
    IVec4 const addsubs = math_ivec4_addsubs_2(a, 2, accumulator);
    _check_i("addsubs_2.x", addsubs.x, 5);
    pacc[0] = 10; pacc[1] = 20; pacc[2] = 30; pacc[3] = 40;
    math_ivec4_addsubs_1(pa, 2, pacc);
    _check_i("addsubs_1.w", pacc[3], 32);

    // dest += a - b -> (10+2, 20+2, 30+2, 40+2) = (12,22,32,42)
    IVec4 const subadd = math_ivec4_subadd_2(a, b, accumulator);
    _check_i("subadd_2.x", subadd.x, 12);
    pacc[0] = 10; pacc[1] = 20; pacc[2] = 30; pacc[3] = 40;
    math_ivec4_subadd_1(pa, pb, pacc);
    _check_i("subadd_1.y", pacc[1], 22);

    // dest += a - s (s=1) -> (10+2, 20+3, 30+4, 40+5) = (12,23,34,45)
    IVec4 const subadds = math_ivec4_subadds_2(a, 1, accumulator);
    _check_i("subadds_2.x", subadds.x, 12);
    pacc[0] = 10; pacc[1] = 20; pacc[2] = 30; pacc[3] = 40;
    math_ivec4_subadds_1(pa, 1, pacc);
    _check_i("subadds_1.w", pacc[3], 45);

    // dest -= a - b -> (10-2, ...) = (8,18,28,38)
    IVec4 const subsub = math_ivec4_subsub_2(a, b, accumulator);
    _check_i("subsub_2.x", subsub.x, 8);
    pacc[0] = 10; pacc[1] = 20; pacc[2] = 30; pacc[3] = 40;
    math_ivec4_subsub_1(pa, pb, pacc);
    _check_i("subsub_1.w", pacc[3], 38);

    // dest -= a - s (s=1) -> (10-2, 20-3, 30-4, 40-5) = (8,17,26,35)
    IVec4 const subsubs = math_ivec4_subsubs_2(a, 1, accumulator);
    _check_i("subsubs_2.x", subsubs.x, 8);
    pacc[0] = 10; pacc[1] = 20; pacc[2] = 30; pacc[3] = 40;
    math_ivec4_subsubs_1(pa, 1, pacc);
    _check_i("subsubs_1.w", pacc[3], 35);

    // dest += a * b -> (10+3, 20+8, 30+15, 40+24) = (13,28,45,64)
    IVec4 const muladd = math_ivec4_muladd_2(a, b, accumulator);
    _check_i("muladd_2.x", muladd.x, 13);
    _check_i("muladd_2.w", muladd.w, 64);
    pacc[0] = 10; pacc[1] = 20; pacc[2] = 30; pacc[3] = 40;
    math_ivec4_muladd_1(pa, pb, pacc);
    _check_i("muladd_1.z", pacc[2], 45);

    // dest += a * s (s=2) -> (10+6, 20+8, 30+10, 40+12) = (16,28,40,52)
    IVec4 const muladds = math_ivec4_muladds_2(a, 2, accumulator);
    _check_i("muladds_2.x", muladds.x, 16);
    pacc[0] = 10; pacc[1] = 20; pacc[2] = 30; pacc[3] = 40;
    math_ivec4_muladds_1(pa, 2, pacc);
    _check_i("muladds_1.w", pacc[3], 52);

    // dest -= a * b -> (10-3, 20-8, 30-15, 40-24) = (7,12,15,16)
    IVec4 const mulsub = math_ivec4_mulsub_2(a, b, accumulator);
    _check_i("mulsub_2.w", mulsub.w, 16);
    pacc[0] = 10; pacc[1] = 20; pacc[2] = 30; pacc[3] = 40;
    math_ivec4_mulsub_1(pa, pb, pacc);
    _check_i("mulsub_1.x", pacc[0], 7);

    // dest -= a * s (s=2) -> (10-6, 20-8, 30-10, 40-12) = (4,12,20,28)
    IVec4 const mulsubs = math_ivec4_mulsubs_2(a, 2, accumulator);
    _check_i("mulsubs_2.x", mulsubs.x, 4);
    pacc[0] = 10; pacc[1] = 20; pacc[2] = 30; pacc[3] = 40;
    math_ivec4_mulsubs_1(pa, 2, pacc);
    _check_i("mulsubs_1.w", pacc[3], 28);

    // dest += max(a,b) -> a dominates -> (13,24,35,46)
    IVec4 const maxadd = math_ivec4_maxadd_2(a, b, accumulator);
    _check_i("maxadd_2.x", maxadd.x, 13);
    pacc[0] = 10; pacc[1] = 20; pacc[2] = 30; pacc[3] = 40;
    math_ivec4_maxadd_1(pa, pb, pacc);
    _check_i("maxadd_1.w", pacc[3], 46);

    // dest -= max(a,b) -> (10-3, 20-4, 30-5, 40-6) = (7,16,25,34)
    IVec4 const maxsub = math_ivec4_maxsub_2(a, b, accumulator);
    _check_i("maxsub_2.y", maxsub.y, 16);
    pacc[0] = 10; pacc[1] = 20; pacc[2] = 30; pacc[3] = 40;
    math_ivec4_maxsub_1(pa, pb, pacc);
    _check_i("maxsub_1.x", pacc[0], 7);

    // dest += min(a,b) -> b dominates -> (11,22,33,44)
    IVec4 const minadd = math_ivec4_minadd_2(a, b, accumulator);
    _check_i("minadd_2.x", minadd.x, 11);
    pacc[0] = 10; pacc[1] = 20; pacc[2] = 30; pacc[3] = 40;
    math_ivec4_minadd_1(pa, pb, pacc);
    _check_i("minadd_1.w", pacc[3], 44);

    // dest -= min(a,b) -> (10-1, 20-2, 30-3, 40-4) = (9,18,27,36)
    IVec4 const minsub = math_ivec4_minsub_2(a, b, accumulator);
    _check_i("minsub_2.x", minsub.x, 9);
    pacc[0] = 10; pacc[1] = 20; pacc[2] = 30; pacc[3] = 40;
    math_ivec4_minsub_1(pa, pb, pacc);
    _check_i("minsub_1.w", pacc[3], 36);

    // --- geometry: distance / distance2 (float + int returns) ---
    printf("--- geometry ---\n");

    // a-b = (2,2,2,2) -> distance2 = 16, distance = 4
    _check_i("distance2_2", math_ivec4_distance2_2(a, b), 16);
    _check_i("distance2_1", math_ivec4_distance2_1(pa, pb), 16);
    _check_f("distance_2", math_ivec4_distance_2(a, b), 4.0, _FTOL);
    _check_f("distance_1", math_ivec4_distance_1(pa, pb), 4.0, _FTOL);

    // --- min/max, clamp ---
    printf("--- min/max / clamp ---\n");

    IVec4 const mxv = math_ivec4_maxv_2(a, b);
    _check_i("maxv_2.x", mxv.x, 3);
    _check_i("maxv_2.w", mxv.w, 6);
    IVec4 const mnv = math_ivec4_minv_2(a, b);
    _check_i("minv_2.x", mnv.x, 1);
    math_ivec4_maxv_1(pa, pb, po);
    _check_i("maxv_1.z", po[2], 5);
    math_ivec4_minv_1(pa, pb, po);
    _check_i("minv_1.w", po[3], 4);

    // clamp (3,4,5,6) into [4,5] -> (4,4,5,5)
    IVec4 const clp = math_ivec4_clamp_2(a, 4, 5);
    _check_i("clamp_2.x", clp.x, 4);
    _check_i("clamp_2.w", clp.w, 5);
    math_ivec4_clamp_1(pa, 4, 5, po);
    _check_i("clamp_1.y", po[1], 4);

    // --- abs (with negative boundary) ---
    printf("--- abs ---\n");

    IVec4 const negv = { -3, 4, -5, 6 };
    ISize pnegv[4] = { -3, 4, -5, 6 };
    IVec4 const av = math_ivec4_abs_2(negv);
    _check_i("abs_2.x", av.x, 3);
    _check_i("abs_2.z", av.z, 5);
    math_ivec4_abs_1(pnegv, po);
    _check_i("abs_1.x", po[0], 3);

    // --- boundaries: zero-distance, negative scale, INT extremes ---
    printf("--- boundaries ---\n");

    // distance of a vector to itself is 0
    _check_i("distance2 self", math_ivec4_distance2_2(a, a), 0);
    _check_f("distance self", math_ivec4_distance_2(a, a), 0.0, _FTOL);

    // negative scale flips sign
    IVec4 const negscale = math_ivec4_scale_2(a, -1);
    _check_i("scale_2 neg.x", negscale.x, -3);
    _check_i("scale_2 neg.w", negscale.w, -6);

    // clamp with a fully-below vector snaps every component to minVal
    IVec4 const below = { -100, -100, -100, -100 };
    IVec4 const belowc = math_ivec4_clamp_2(below, 0, 10);
    _check_i("clamp_2 below.x", belowc.x, 0);
    _check_i("clamp_2 below.w", belowc.w, 0);

    // clamp BOUNDS saturate at the ISize -> int boundary instead of truncating. Before the
    // fix ISIZE_MAX truncated to -1 and this read back -1: the clamp silently inverted.
    IVec4 const three   = { .x = 3, .y = 3, .z = 3, .w = 3 };
    IVec4 const sat     = math_ivec4_clamp_2(three, 0, ISIZE_MAX);
    IVec4 const sat_low = math_ivec4_clamp_2(three, ISIZE_MIN, 2);
    _check_i("clamp_2 with an ISIZE_MAX bound keeps the value", sat.x, 3);
    _check_i("clamp_2 with an ISIZE_MIN bound still clamps high", sat_low.x, 2);

    return _check_finish();
}