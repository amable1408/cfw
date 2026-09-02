/*
 * test_ivec3.c - Tests for include/math/ivec3.c (full glmc_ivec3_* coverage)
 */

#include <math.h>
#include <stdio.h>

#include <math/ivec3.h>

#include "check.h"

// === Helpers ===

#define _FTOL 1e-6

int main(void) {
    printf("=== ivec3 module tests ===\n");

    IVec3 const a = { 3, 4, 12 };
    IVec3 const b = { 1, 2, 3 };
    ISize pa[3] = { 3, 4, 12 };
    ISize pb[3] = { 1, 2, 3 };
    ISize po[3] = DEFAULT_INITIALIZATION;

    // --- construction: ivec3 (from ivec4) / copy / fill / zero / one ---
    printf("--- construction ---\n");

    IVec3 const cp = math_ivec3_copy_2(a);
    _check_i("copy_2.x", cp.x, 3);
    _check_i("copy_2.z", cp.z, 12);
    math_ivec3_copy_1(pa, po);
    _check_i("copy_1.y", po[1], 4);

    // ivec3 from ivec4 drops w: (5,6,7,8) -> (5,6,7)
    IVec4 const src4 = { 5, 6, 7, 8 };
    IVec3 const from4 = math_ivec3_ivec3_2(src4);
    _check_i("ivec3_2.x", from4.x, 5);
    _check_i("ivec3_2.z", from4.z, 7);
    ISize p4[4] = { 5, 6, 7, 8 };
    math_ivec3_ivec3_1(p4, po);
    _check_i("ivec3_1.z", po[2], 7);

    IVec3 const fl = math_ivec3_fill_2(9);
    _check_i("fill_2.x", fl.x, 9);
    _check_i("fill_2.z", fl.z, 9);
    math_ivec3_fill_1(5, po);
    _check_i("fill_1.y", po[1], 5);

    IVec3 const zr = math_ivec3_zero_2();
    _check_i("zero_2.x", zr.x, 0);
    math_ivec3_zero_1(po);
    _check_i("zero_1.z", po[2], 0);

    IVec3 const on = math_ivec3_one_2();
    _check_i("one_2.y", on.y, 1);
    math_ivec3_one_1(po);
    _check_i("one_1.x", po[0], 1);

    // --- comparison: eq / eqv ---
    printf("--- comparison ---\n");

    IVec3 const nines = { 9, 9, 9 };
    ISize pnines[3] = { 9, 9, 9 };
    _check_b("eq_2 true", math_ivec3_eq_2(nines, 9), true);
    _check_b("eq_2 false", math_ivec3_eq_2(a, 9), false);
    _check_b("eq_1 true", math_ivec3_eq_1(pnines, 9), true);

    _check_b("eqv_2 true", math_ivec3_eqv_2(a, a), true);
    _check_b("eqv_2 false", math_ivec3_eqv_2(a, b), false);
    _check_b("eqv_1 true", math_ivec3_eqv_1(pa, pa), true);
    _check_b("eqv_1 false", math_ivec3_eqv_1(pa, pb), false);

    // --- arithmetic: add/adds/sub/subs/mul/scale/div/divs/mod ---
    printf("--- arithmetic ---\n");

    IVec3 const sum = math_ivec3_add_2(a, b);
    _check_i("add_2.x", sum.x, 4);
    _check_i("add_2.z", sum.z, 15);
    math_ivec3_add_1(pa, pb, po);
    _check_i("add_1.x", po[0], 4);

    IVec3 const adds = math_ivec3_adds_2(a, 10);
    _check_i("adds_2.x", adds.x, 13);
    math_ivec3_adds_1(pa, 10, po);
    _check_i("adds_1.z", po[2], 22);

    IVec3 const diff = math_ivec3_sub_2(a, b);
    _check_i("sub_2.x", diff.x, 2);
    _check_i("sub_2.z", diff.z, 9);
    math_ivec3_sub_1(pa, pb, po);
    _check_i("sub_1.y", po[1], 2);

    IVec3 const subs = math_ivec3_subs_2(a, 1);
    _check_i("subs_2.x", subs.x, 2);
    math_ivec3_subs_1(pa, 1, po);
    _check_i("subs_1.z", po[2], 11);

    IVec3 const prod = math_ivec3_mul_2(a, b);
    _check_i("mul_2.x", prod.x, 3);
    _check_i("mul_2.z", prod.z, 36);
    math_ivec3_mul_1(pa, pb, po);
    _check_i("mul_1.y", po[1], 8);

    IVec3 const scaled = math_ivec3_scale_2(a, 2);
    _check_i("scale_2.x", scaled.x, 6);
    _check_i("scale_2.z", scaled.z, 24);
    math_ivec3_scale_1(pa, 2, po);
    _check_i("scale_1.y", po[1], 8);

    // integer division: (3,4,12)/(1,2,3) = (3, 2, 4)
    IVec3 const quot = math_ivec3_div_2(a, b);
    _check_i("div_2.x", quot.x, 3);
    _check_i("div_2.z", quot.z, 4);
    math_ivec3_div_1(pa, pb, po);
    _check_i("div_1.y", po[1], 2);

    // divs: (3,4,12)/2 = (1, 2, 6)
    IVec3 const divs = math_ivec3_divs_2(a, 2);
    _check_i("divs_2.x", divs.x, 1);
    _check_i("divs_2.z", divs.z, 6);
    math_ivec3_divs_1(pa, 2, po);
    _check_i("divs_1.z", po[2], 6);

    // mod: (3,4,12) % (1,2,3) = wait: 3%1=0, 4%2=0, 12%3=0; use (5,7,10)%(3,4,6)
    IVec3 const ma = { 5, 7, 10 };
    IVec3 const mb = { 3, 4, 6 };
    ISize pma[3] = { 5, 7, 10 };
    ISize pmb[3] = { 3, 4, 6 };
    IVec3 const modv = math_ivec3_mod_2(ma, mb);
    _check_i("mod_2.x", modv.x, 2);
    _check_i("mod_2.y", modv.y, 3);
    _check_i("mod_2.z", modv.z, 4);
    math_ivec3_mod_1(pma, pmb, po);
    _check_i("mod_1.z", po[2], 4);

    // --- fused accumulate: addadd(s)/subadd(s)/muladd(s)/maxadd/minadd ---
    printf("--- fused accumulate (add) ---\n");

    IVec3 const accumulator = { 100, 200, 300 };
    ISize pacc[3] = { 100, 200, 300 };

    // dest += a + b -> (100+4, 200+6, 300+15) = (104, 206, 315)
    IVec3 const addadd = math_ivec3_addadd_2(a, b, accumulator);
    _check_i("addadd_2.x", addadd.x, 104);
    _check_i("addadd_2.z", addadd.z, 315);
    pacc[0] = 100; pacc[1] = 200; pacc[2] = 300;
    math_ivec3_addadd_1(pa, pb, pacc);
    _check_i("addadd_1.x", pacc[0], 104);

    // dest += a + s -> (100+3+10, ...) = (113, 214, 322)
    IVec3 const addadds = math_ivec3_addadds_2(a, 10, accumulator);
    _check_i("addadds_2.x", addadds.x, 113);
    _check_i("addadds_2.z", addadds.z, 322);
    pacc[0] = 100; pacc[1] = 200; pacc[2] = 300;
    math_ivec3_addadds_1(pa, 10, pacc);
    _check_i("addadds_1.z", pacc[2], 322);

    // subadd: dest += a - b -> (100+2, 200+2, 300+9) = (102, 202, 309)
    IVec3 const subadd = math_ivec3_subadd_2(a, b, accumulator);
    _check_i("subadd_2.x", subadd.x, 102);
    _check_i("subadd_2.z", subadd.z, 309);
    pacc[0] = 100; pacc[1] = 200; pacc[2] = 300;
    math_ivec3_subadd_1(pa, pb, pacc);
    _check_i("subadd_1.y", pacc[1], 202);

    // subadds: dest += a - s -> (100+3-1, ...) = (102, 203, 311)
    IVec3 const subadds = math_ivec3_subadds_2(a, 1, accumulator);
    _check_i("subadds_2.x", subadds.x, 102);
    _check_i("subadds_2.z", subadds.z, 311);
    pacc[0] = 100; pacc[1] = 200; pacc[2] = 300;
    math_ivec3_subadds_1(pa, 1, pacc);
    _check_i("subadds_1.z", pacc[2], 311);

    // muladd: dest += a * b -> (100+3, 200+8, 300+36) = (103, 208, 336)
    IVec3 const muladd = math_ivec3_muladd_2(a, b, accumulator);
    _check_i("muladd_2.x", muladd.x, 103);
    _check_i("muladd_2.z", muladd.z, 336);
    pacc[0] = 100; pacc[1] = 200; pacc[2] = 300;
    math_ivec3_muladd_1(pa, pb, pacc);
    _check_i("muladd_1.y", pacc[1], 208);

    // muladds: dest += a * s -> (100+3*2, 200+4*2, 300+12*2) = (106, 208, 324)
    IVec3 const muladds = math_ivec3_muladds_2(a, 2, accumulator);
    _check_i("muladds_2.x", muladds.x, 106);
    _check_i("muladds_2.z", muladds.z, 324);
    pacc[0] = 100; pacc[1] = 200; pacc[2] = 300;
    math_ivec3_muladds_1(pa, 2, pacc);
    _check_i("muladds_1.z", pacc[2], 324);

    // maxadd: dest += max(a, b) -> (100+3, 200+4, 300+12) = (103, 204, 312)
    IVec3 const maxadd = math_ivec3_maxadd_2(a, b, accumulator);
    _check_i("maxadd_2.x", maxadd.x, 103);
    _check_i("maxadd_2.z", maxadd.z, 312);
    pacc[0] = 100; pacc[1] = 200; pacc[2] = 300;
    math_ivec3_maxadd_1(pa, pb, pacc);
    _check_i("maxadd_1.z", pacc[2], 312);

    // minadd: dest += min(a, b) -> (100+1, 200+2, 300+3) = (101, 202, 303)
    IVec3 const minadd = math_ivec3_minadd_2(a, b, accumulator);
    _check_i("minadd_2.x", minadd.x, 101);
    _check_i("minadd_2.z", minadd.z, 303);
    pacc[0] = 100; pacc[1] = 200; pacc[2] = 300;
    math_ivec3_minadd_1(pa, pb, pacc);
    _check_i("minadd_1.y", pacc[1], 202);

    // --- fused subtract: addsub(s)/subsub(s)/mulsub(s)/maxsub/minsub ---
    printf("--- fused accumulate (sub) ---\n");

    // addsub: dest -= a + b -> (100-4, 200-6, 300-15) = (96, 194, 285)
    IVec3 const addsub = math_ivec3_addsub_2(a, b, accumulator);
    _check_i("addsub_2.x", addsub.x, 96);
    _check_i("addsub_2.z", addsub.z, 285);
    pacc[0] = 100; pacc[1] = 200; pacc[2] = 300;
    math_ivec3_addsub_1(pa, pb, pacc);
    _check_i("addsub_1.y", pacc[1], 194);

    // addsubs: dest -= a + s -> (100-3-10, ...) = (87, 186, 278)
    IVec3 const addsubs = math_ivec3_addsubs_2(a, 10, accumulator);
    _check_i("addsubs_2.x", addsubs.x, 87);
    _check_i("addsubs_2.z", addsubs.z, 278);
    pacc[0] = 100; pacc[1] = 200; pacc[2] = 300;
    math_ivec3_addsubs_1(pa, 10, pacc);
    _check_i("addsubs_1.z", pacc[2], 278);

    // subsub: dest -= a - b -> (100-2, 200-2, 300-9) = (98, 198, 291)
    IVec3 const subsub = math_ivec3_subsub_2(a, b, accumulator);
    _check_i("subsub_2.x", subsub.x, 98);
    _check_i("subsub_2.z", subsub.z, 291);
    pacc[0] = 100; pacc[1] = 200; pacc[2] = 300;
    math_ivec3_subsub_1(pa, pb, pacc);
    _check_i("subsub_1.y", pacc[1], 198);

    // subsubs: dest -= a - s -> (100-(3-1), ...) = (98, 197, 289)
    IVec3 const subsubs = math_ivec3_subsubs_2(a, 1, accumulator);
    _check_i("subsubs_2.x", subsubs.x, 98);
    _check_i("subsubs_2.z", subsubs.z, 289);
    pacc[0] = 100; pacc[1] = 200; pacc[2] = 300;
    math_ivec3_subsubs_1(pa, 1, pacc);
    _check_i("subsubs_1.z", pacc[2], 289);

    // mulsub: dest -= a * b -> (100-3, 200-8, 300-36) = (97, 192, 264)
    IVec3 const mulsub = math_ivec3_mulsub_2(a, b, accumulator);
    _check_i("mulsub_2.x", mulsub.x, 97);
    _check_i("mulsub_2.z", mulsub.z, 264);
    pacc[0] = 100; pacc[1] = 200; pacc[2] = 300;
    math_ivec3_mulsub_1(pa, pb, pacc);
    _check_i("mulsub_1.y", pacc[1], 192);

    // mulsubs: dest -= a * s -> (100-6, 200-8, 300-24) = (94, 192, 276)
    IVec3 const mulsubs = math_ivec3_mulsubs_2(a, 2, accumulator);
    _check_i("mulsubs_2.x", mulsubs.x, 94);
    _check_i("mulsubs_2.z", mulsubs.z, 276);
    pacc[0] = 100; pacc[1] = 200; pacc[2] = 300;
    math_ivec3_mulsubs_1(pa, 2, pacc);
    _check_i("mulsubs_1.z", pacc[2], 276);

    // maxsub: dest -= max(a, b) -> (100-3, 200-4, 300-12) = (97, 196, 288)
    IVec3 const maxsub = math_ivec3_maxsub_2(a, b, accumulator);
    _check_i("maxsub_2.x", maxsub.x, 97);
    _check_i("maxsub_2.z", maxsub.z, 288);
    pacc[0] = 100; pacc[1] = 200; pacc[2] = 300;
    math_ivec3_maxsub_1(pa, pb, pacc);
    _check_i("maxsub_1.z", pacc[2], 288);

    // minsub: dest -= min(a, b) -> (100-1, 200-2, 300-3) = (99, 198, 297)
    IVec3 const minsub = math_ivec3_minsub_2(a, b, accumulator);
    _check_i("minsub_2.x", minsub.x, 99);
    _check_i("minsub_2.z", minsub.z, 297);
    pacc[0] = 100; pacc[1] = 200; pacc[2] = 300;
    math_ivec3_minsub_1(pa, pb, pacc);
    _check_i("minsub_1.y", pacc[1], 198);

    // --- geometry: dot / norm2 / norm / distance2 / distance ---
    printf("--- geometry ---\n");

    // dot((3,4,12),(1,2,3)) = 3+8+36 = 47
    _check_i("dot_2", math_ivec3_dot_2(a, b), 47);
    _check_i("dot_1", math_ivec3_dot_1(pa, pb), 47);

    // norm2 of (3,4,12) = 9+16+144 = 169
    _check_i("norm2_2", math_ivec3_norm2_2(a), 169);
    _check_i("norm2_1", math_ivec3_norm2_1(pa), 169);

    // norm of (3,4,12) = sqrt(169) = 13 (exact integer)
    _check_i("norm_2", math_ivec3_norm_2(a), 13);
    _check_i("norm_1", math_ivec3_norm_1(pa), 13);

    // distance2((3,4,12),(1,2,3)) = 4+4+81 = 89
    _check_i("distance2_2", math_ivec3_distance2_2(a, b), 89);
    _check_i("distance2_1", math_ivec3_distance2_1(pa, pb), 89);

    // distance is a float result -> sqrt(89)
    _check_f("distance_2", math_ivec3_distance_2(a, b), sqrt(89.0), _FTOL);
    _check_f("distance_1", math_ivec3_distance_1(pa, pb), sqrt(89.0), _FTOL);

    // --- min/max, clamp, abs (including negatives) ---
    printf("--- min/max / clamp / abs ---\n");

    IVec3 const mxv = math_ivec3_maxv_2(a, b);
    _check_i("maxv_2.x", mxv.x, 3);
    _check_i("maxv_2.z", mxv.z, 12);
    IVec3 const mnv = math_ivec3_minv_2(a, b);
    _check_i("minv_2.x", mnv.x, 1);
    _check_i("minv_2.z", mnv.z, 3);
    math_ivec3_maxv_1(pa, pb, po);
    _check_i("maxv_1.z", po[2], 12);
    math_ivec3_minv_1(pa, pb, po);
    _check_i("minv_1.x", po[0], 1);

    // clamp (3,4,12) into [0,5] -> (3,4,5)
    IVec3 const clp = math_ivec3_clamp_2(a, 0, 5);
    _check_i("clamp_2.x", clp.x, 3);
    _check_i("clamp_2.z", clp.z, 5);
    math_ivec3_clamp_1(pa, 0, 5, po);
    _check_i("clamp_1.z", po[2], 5);

    // clamp boundary: values below min clamp up; (-9,4,12) into [-2,10] -> (-2,4,10)
    IVec3 const negs = { -9, 4, 12 };
    ISize pnegs[3] = { -9, 4, 12 };
    IVec3 const clpn = math_ivec3_clamp_2(negs, -2, 10);
    _check_i("clamp_2 low", clpn.x, -2);
    _check_i("clamp_2 high", clpn.z, 10);
    _check_i("clamp_2 mid", clpn.y, 4);

    // abs (-9,4,12) -> (9,4,12)
    IVec3 const av = math_ivec3_abs_2(negs);
    _check_i("abs_2.x", av.x, 9);
    _check_i("abs_2.z", av.z, 12);
    math_ivec3_abs_1(pnegs, po);
    _check_i("abs_1.x", po[0], 9);

    // clamp BOUNDS saturate at the ISize -> int boundary instead of truncating. Before the
    // fix ISIZE_MAX truncated to -1 and this read back -1: the clamp silently inverted.
    IVec3 const three   = { .x = 3, .y = 3, .z = 3 };
    IVec3 const sat     = math_ivec3_clamp_2(three, 0, ISIZE_MAX);
    IVec3 const sat_low = math_ivec3_clamp_2(three, ISIZE_MIN, 2);
    _check_i("clamp_2 with an ISIZE_MAX bound keeps the value", sat.x, 3);
    _check_i("clamp_2 with an ISIZE_MIN bound still clamps high", sat_low.x, 2);

    // Integer division by zero used to TRAP the process; both forms now refuse to the zeroed
    // vector. divs is checked on the CONVERTED int, so a 2^32 scalar (low 32 bits zero) refuses too.
    IVec3 const eight   = { .x = 8, .y = 8, .z = 8 };
    IVec3 const zero_in = { .x = 2, .y = 0, .z = 2 };
    IVec3 const dz      = math_ivec3_divs_2(eight, 0);
    _check_i("divs_2 by zero refuses to zero", dz.x, 0);
    IVec3 const dt      = math_ivec3_divs_2(eight, (ISize) 1 << 32);
    _check_i("divs_2 by 2^32 (truncates to 0) refuses to zero", dt.x, 0);
    IVec3 const dv      = math_ivec3_div_2(eight, zero_in);
    _check_i("div_2 with a zero component refuses the whole vector", dv.x, 0);
    IVec3 const ok      = math_ivec3_divs_2(eight, 2);
    _check_i("divs_2 by 2 still divides", ok.x, 4);

    // The other half of the trap set: INT_MIN / -1 does not fit an int and traps exactly like
    // a zero divisor; mod shares both. All refuse to the zeroed vector.
    IVec3 const int_min = { .x = INT_MIN, .y = 4, .z = 4 };
    _check_i("divs_2 INT_MIN by -1 refuses", math_ivec3_divs_2(int_min, -1).x, 0);
    _check_i("div_2 INT_MIN by -1 refuses", math_ivec3_div_2(int_min, (IVec3) { .x = -1, .y = 1, .z = 1 }).x, 0);
    _check_i("mod_2 by zero refuses", math_ivec3_mod_2(eight, zero_in).x, 0);
    _check_i("mod_2 INT_MIN by -1 refuses", math_ivec3_mod_2(int_min, (IVec3) { .x = -1, .y = 1, .z = 1 }).x, 0);
    _check_i("mod_2 by 3 still works", math_ivec3_mod_2(eight, (IVec3) { .x = 3, .y = 3, .z = 3 }).x, 2);

    // The raw _1 form, with the ZERO in the z component: the ivec3-only arm of the guard.
    ISize const raw_a[3] = { 8, 8, 8 };
    ISize const raw_b[3] = { 2, 2, 0 };
    ISize       raw_d[3] = { 7, 7, 7 };
    math_ivec3_div_1(raw_a, raw_b, raw_d);
    _check_i("div_1 with a zero z component refuses the whole vector", raw_d[0] + raw_d[1] + raw_d[2], 0);

    return _check_finish();
}