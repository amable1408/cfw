/*
 * test_frustum.c - Tests for include/math/frustum.c (full glmc_frustum_* coverage)
 */

#include <math.h>
#include <stdio.h>

#include <math/frustum.h>
#include <math/mat4.h>

#include "check.h"

// === Helpers ===

#define _FTOL 1e-4

int main(void) {
    printf("=== frustum module tests ===\n");

    // The 4x4 identity is a well-behaved matrix for extraction: its transpose is
    // itself and it is its own inverse, so the extracted planes/corners are exact
    // clip-space quantities.
    FSize pid[16] = DEFAULT_INITIALIZATION;
    math_mat4_identity_1(pid);

    // --- planes: finite, and known left/right from identity ---
    printf("--- planes ---\n");

    FSize planes[24] = DEFAULT_INITIALIZATION;
    math_frustum_planes_1(pid, planes);

    bool planes_finite = true;
    for (USize i = 0; i < 24; i++) {
        if (!isfinite((double) planes[i])) {
            planes_finite = false;
        }
    }
    _check_b("planes all finite", planes_finite, true);

    // transpose(I) rows: t0=(1,0,0,0), t3=(0,0,0,1).
    // left  = normalize(t3 + t0) = normalize(1,0,0,1) -> (1,0,0,1) (|normal| = 1).
    // right = normalize(t3 - t0) = normalize(-1,0,0,1) -> (-1,0,0,1).
    _check_f("planes left nx", planes[0 * 4 + 0], 1.0, _FTOL);
    _check_f("planes left d", planes[0 * 4 + 3], 1.0, _FTOL);
    _check_f("planes right nx", planes[1 * 4 + 0], -1.0, _FTOL);

    // The struct producer returns the same six planes the raw form wrote.
    FrustumPlanes const planes2 = math_frustum_planes_2(math_mat4_identity_2());
    _check_f("planes_2 left nx", planes2.planes[0].x, 1.0, _FTOL);
    _check_f("planes_2 left d", planes2.planes[0].w, 1.0, _FTOL);
    _check_f("planes_2 right nx", planes2.planes[1].x, -1.0, _FTOL);
    _check_f("planes_2 far nz", planes2.planes[5].z, planes[5 * 4 + 2], _FTOL);

    // --- corners: identity invMat yields the clip-space cube corners ---
    printf("--- corners ---\n");

    FSize corners[32] = DEFAULT_INITIALIZATION;
    math_frustum_corners_1(pid, corners);

    // corner 0 = LBN = (-1,-1,-1,1); w-divide by 1 leaves it unchanged.
    _check_f("corners LBN x", corners[0 * 4 + 0], -1.0, _FTOL);
    _check_f("corners LBN y", corners[0 * 4 + 1], -1.0, _FTOL);
    _check_f("corners LBN z", corners[0 * 4 + 2], -1.0, _FTOL);
    // corner 6 = RTF = (1,1,1,1).
    _check_f("corners RTF x", corners[6 * 4 + 0], 1.0, _FTOL);
    _check_f("corners RTF z", corners[6 * 4 + 2], 1.0, _FTOL);

    FrustumCorners const corners2 = math_frustum_corners_2(math_mat4_identity_2());
    _check_f("corners_2 LBN x", corners2.corners[0].x, -1.0, _FTOL);
    _check_f("corners_2 RTF z", corners2.corners[6].z, 1.0, _FTOL);
    _check_f("corners_2 RTF w", corners2.corners[6].w, corners[6 * 4 + 3], _FTOL);

    // --- center: center of 8 symmetric corners is their average ---
    printf("--- center ---\n");

    // A cube of 8 corners centered at (2,3,4), half-extent 1 on each axis.
    FSize sym[32] = DEFAULT_INITIALIZATION;
    FSize const sign[2] = { -1.0, 1.0 };
    USize k = 0;
    for (USize ax = 0; ax < 2; ax++) {
        for (USize ay = 0; ay < 2; ay++) {
            for (USize az = 0; az < 2; az++) {
                sym[k * 4 + 0] = 2.0 + sign[ax];
                sym[k * 4 + 1] = 3.0 + sign[ay];
                sym[k * 4 + 2] = 4.0 + sign[az];
                sym[k * 4 + 3] = 1.0;
                k += 1;
            }
        }
    }

    FrustumCorners sym_corners = DEFAULT_INITIALIZATION;

    for (USize corner = 0; corner < 8; corner += 1) {
        sym_corners.corners[corner] = (Vec4) { sym[corner * 4 + 0], sym[corner * 4 + 1], sym[corner * 4 + 2], sym[corner * 4 + 3] };
    }

    Vec3 const ctr = math_frustum_center_2(sym_corners);
    _check_f("center_2 x", ctr.x, 2.0, _FTOL);
    _check_f("center_2 y", ctr.y, 3.0, _FTOL);
    _check_f("center_2 z", ctr.z, 4.0, _FTOL);
    FSize pctr[3] = DEFAULT_INITIALIZATION;
    math_frustum_center_1(sym, pctr);
    _check_f("center_1 z", pctr[2], 4.0, _FTOL);

    // --- box: identity transform leaves min/max as the corner extremes ---
    printf("--- box ---\n");

    Box const box = math_frustum_box_2(sym_corners, math_mat4_identity_2());
    _check_f("box_2 min x", box.min.x, 1.0, _FTOL);
    _check_f("box_2 min z", box.min.z, 3.0, _FTOL);
    _check_f("box_2 max x", box.max.x, 3.0, _FTOL);
    _check_f("box_2 max z", box.max.z, 5.0, _FTOL);
    FSize pbox[6] = DEFAULT_INITIALIZATION;
    math_frustum_box_1(sym, pid, pbox);
    _check_f("box_1 min y", pbox[1], 2.0, _FTOL);
    _check_f("box_1 max y", pbox[4], 4.0, _FTOL);

    // --- corners_at: split plane between near and far of the clip cube ---
    printf("--- corners_at ---\n");

    // Using the clip-cube corners: RTF=(1,1,1), RTN=(1,1,-1) -> dist = 2.
    // split=1, far=2 -> sc = 2 * (1/2) = 1. For left-bottom:
    //   LBF - LBN = (0,0,2); scale_as to length 1 = (0,0,1); + LBN(-1,-1,-1) = (-1,-1,0).
    FSize pc[16] = DEFAULT_INITIALIZATION;
    math_frustum_corners_at_1(corners, 1.0, 2.0, pc);
    _check_f("corners_at LB x", pc[0 * 4 + 0], -1.0, _FTOL);
    _check_f("corners_at LB y", pc[0 * 4 + 1], -1.0, _FTOL);
    _check_f("corners_at LB z", pc[0 * 4 + 2], 0.0, _FTOL);

    FrustumSplitCorners const split = math_frustum_corners_at_2(corners2, 1.0, 2.0);
    _check_f("corners_at_2 LB x", split.corners[0].x, -1.0, _FTOL);
    _check_f("corners_at_2 LB z", split.corners[0].z, 0.0, _FTOL);
    _check_f("corners_at_2 RT x", split.corners[2].x, pc[2 * 4 + 0], _FTOL);

    // A far distance that is not > 0 is refused to the zeroed corners, not divided by.
    FrustumSplitCorners const refused = math_frustum_corners_at_2(corners2, 1.0, 0.0);
    _check_f("corners_at_2 far 0 refused", refused.corners[0].x, 0.0, 0.0);
    _check_f("corners_at_2 far 0 refused w", refused.corners[3].w, 0.0, 0.0);
    FSize pc_refused[16] = { 7.0, 7.0, 7.0, 7.0, 7.0, 7.0, 7.0, 7.0, 7.0, 7.0, 7.0, 7.0, 7.0, 7.0, 7.0, 7.0 };
    math_frustum_corners_at_1(corners, 1.0, -2.0, pc_refused);
    _check_f("corners_at_1 far < 0 refused", pc_refused[5], 0.0, 0.0);
    // A positive F64 that narrows to 0.0f would reach the division in cglm; the guard tests the float.
    FrustumSplitCorners const tiny = math_frustum_corners_at_2(corners2, 1.0, 1e-300);
    _check_f("corners_at_2 far below float range refused", tiny.corners[0].x, 0.0, 0.0);
    // A subnormal float would overflow split / far to Inf: refused as well.
    FrustumSplitCorners const subnormal = math_frustum_corners_at_2(corners2, 1.0, 1e-40);
    _check_f("corners_at_2 subnormal far refused", subnormal.corners[0].z, 0.0, 0.0);
    // An F64 past float range is refused by the bound BEFORE the cast, never cast to Inf.
    FrustumSplitCorners const huge = math_frustum_corners_at_2(corners2, 1.0, 1e300);
    _check_f("corners_at_2 far past float range refused", huge.corners[1].x, 0.0, 0.0);
    // The split distance is refused on the F64 too: NaN or past float range must not coerce to 0.
    FrustumSplitCorners const nan_split = math_frustum_corners_at_2(corners2, NAN, 2.0);
    _check_f("corners_at_2 NaN split refused", nan_split.corners[2].x, 0.0, 0.0);
    FrustumSplitCorners const huge_split = math_frustum_corners_at_2(corners2, 1e300, 2.0);
    _check_f("corners_at_2 split past float range refused", huge_split.corners[2].x, 0.0, 0.0);

    bool at_finite = true;
    for (USize i = 0; i < 16; i++) {
        if (!isfinite((double) pc[i])) {
            at_finite = false;
        }
    }
    _check_b("corners_at all finite", at_finite, true);

    // keep _check_u / _check_i referenced so the harness helpers are exercised
    _check_u("pass counter positive", _pass > 0 ? 1 : 0, 1);
    _check_i("fail counter type", (ISize) 0, 0);

    return _check_finish();
}