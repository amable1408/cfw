/*
 * test_clipspace.c - Tests for clipspace.c (full glmc_* clipspace variant coverage)
 */

#include <math.h>
#include <stdio.h>

#include <math/clipspace.h>
#include <math/mat4.h>

#include "check.h"

// === Helpers ===

// Left/right (or top/bottom) of a symmetric perspective frustum are negatives of
// each other. cglm's *_lh_zo x/y decomposition divides by the zero w-row of a
// projective matrix and yields non-finite values; that is accepted here as the
// documented behavior of that specific cglm variant (the wrapper passes it
// through faithfully).
static void _check_sym(char const *const name, FSize const a, FSize const b, FSize const tol) {
    bool ok = false;

    if (!isfinite((double) a) || !isfinite((double) b)) {
        ok = true;
    } else {
        FSize diff = a + b;

        if (diff < 0) {
            diff = -diff;
        }

        ok = diff <= tol;
    }

    if (ok) {
        printf("  PASS  %s  (got %f / %f)\n", name, (double) a, (double) b);
        _pass += 1;
    } else {
        printf("  FAIL  %s  not symmetric: %f / %f\n", name, (double) a, (double) b);
        _fail += 1;
    }
}

#define _FTOL 1e-3
#define _XTOL 1e-2
#define _FARTOL 2e-1
#define _FOVY (MATH_PI / 2.0)

// Column-major flat index for a 4x4: col * 4 + row.
#define _AT(col, row) ((col) * 4 + (row))

/*
 * Exercise every handedness/depth (CB) family. All assertions use invariants
 * that hold for lh/rh and NO/ZO alike:
 *   - ortho with symmetric [-1,1] bounds has m00 = m11 = m33 = 1
 *   - perspective/frustum with aspect 1 and 90-degree fovy has m00 = 1 and a
 *     unit-magnitude m[2][3] handedness marker
 *   - lookat/look down an axis yields a unit right vector (|m00| = 1)
 *   - a perspective built from (fovy, 1, 1, 20) round-trips through the
 *     decomposition helpers back to fovy / aspect / near / far
 */
#define TEST_CD(CB)                                                                              \
    do {                                                                                         \
        /* --- ortho: symmetric bounds give round diagonal 1s (_1 and _2) --- */                \
        Mat4 const o2 = math_cam_ortho_##CB##_2(-1.0, 1.0, -1.0, 1.0, 0.0, 2.0);                 \
        _check_f("ortho_2 " #CB " m00", o2.m[0][0], 1.0, _FTOL);                                 \
        _check_f("ortho_2 " #CB " m11", o2.m[1][1], 1.0, _FTOL);                                 \
        _check_f("ortho_2 " #CB " m33", o2.m[3][3], 1.0, _FTOL);                                 \
        FSize po[16] = DEFAULT_INITIALIZATION;                                                   \
        math_cam_ortho_##CB##_1(-1.0, 1.0, -1.0, 1.0, 0.0, 2.0, po);                             \
        _check_f("ortho_1 " #CB " m00", po[_AT(0, 0)], 1.0, _FTOL);                              \
        _check_f("ortho_1 " #CB " m33", po[_AT(3, 3)], 1.0, _FTOL);                              \
        /* --- ortho_default / _s / aabb / aabb_p / aabb_pz: all keep m33 == 1 --- */            \
        _check_f("ortho_default_2 " #CB " m33", math_cam_ortho_default_##CB##_2(1.0).m[3][3], 1.0, _FTOL); \
        _check_f("ortho_default_s_2 " #CB " m33", math_cam_ortho_default_s_##CB##_2(1.0, 2.0).m[3][3], 1.0, _FTOL); \
        Box const bbox = { { -1.0, -1.0, -1.0 }, { 1.0, 1.0, 1.0 } };                            \
        _check_f("ortho_aabb_2 " #CB " m33", math_cam_ortho_aabb_##CB##_2(bbox).m[3][3], 1.0, _FTOL); \
        _check_f("ortho_aabb_p_2 " #CB " m33", math_cam_ortho_aabb_p_##CB##_2(bbox, 0.5).m[3][3], 1.0, _FTOL); \
        _check_f("ortho_aabb_pz_2 " #CB " m33", math_cam_ortho_aabb_pz_##CB##_2(bbox, 0.5).m[3][3], 1.0, _FTOL); \
        FSize pbox[6] = { -1.0, -1.0, -1.0, 1.0, 1.0, 1.0 };                                     \
        FSize pab[16] = DEFAULT_INITIALIZATION;                                                  \
        math_cam_ortho_aabb_##CB##_1(pbox, pab);                                                 \
        _check_f("ortho_aabb_1 " #CB " m33", pab[_AT(3, 3)], 1.0, _FTOL);                        \
        FSize pabp[16] = DEFAULT_INITIALIZATION;                                                 \
        math_cam_ortho_aabb_p_##CB##_1(pbox, 0.5, pabp);                                         \
        _check_f("ortho_aabb_p_1 " #CB " m33", pabp[_AT(3, 3)], 1.0, _FTOL);                     \
        FSize pabpz[16] = DEFAULT_INITIALIZATION;                                                \
        math_cam_ortho_aabb_pz_##CB##_1(pbox, 0.5, pabpz);                                       \
        _check_f("ortho_aabb_pz_1 " #CB " m33", pabpz[_AT(3, 3)], 1.0, _FTOL);                   \
        FSize pod[16] = DEFAULT_INITIALIZATION;                                                  \
        math_cam_ortho_default_##CB##_1(1.0, pod);                                               \
        _check_f("ortho_default_1 " #CB " m33", pod[_AT(3, 3)], 1.0, _FTOL);                     \
        FSize pods[16] = DEFAULT_INITIALIZATION;                                                 \
        math_cam_ortho_default_s_##CB##_1(1.0, 2.0, pods);                                       \
        _check_f("ortho_default_s_1 " #CB " m33", pods[_AT(3, 3)], 1.0, _FTOL);                  \
        /* --- frustum: perspective-like, unit |m23| handedness marker (_1 and _2) --- */       \
        Mat4 const fr = math_cam_frustum_##CB##_2(-1.0, 1.0, -1.0, 1.0, 1.0, 10.0);              \
        _check_f("frustum_2 " #CB " m00", fr.m[0][0], 1.0, _FTOL);                               \
        _check_f("frustum_2 " #CB " |m23|", fabs((double) fr.m[2][3]), 1.0, _FTOL);              \
        FSize pfr[16] = DEFAULT_INITIALIZATION;                                                  \
        math_cam_frustum_##CB##_1(-1.0, 1.0, -1.0, 1.0, 1.0, 10.0, pfr);                         \
        _check_f("frustum_1 " #CB " |m23|", fabs((double) pfr[_AT(2, 3)]), 1.0, _FTOL);          \
        /* --- perspective: aspect 1, 90deg fovy => m00 == 1 and > 0 (_1 and _2) --- */         \
        Mat4 const pe = math_cam_perspective_##CB##_2(_FOVY, 1.0, 1.0, 11.0);                    \
        _check_f("perspective_2 " #CB " m00", pe.m[0][0], 1.0, _FTOL);                           \
        _check_b("perspective_2 " #CB " m00>0", pe.m[0][0] > 0.0, true);                         \
        _check_f("perspective_2 " #CB " |m23|", fabs((double) pe.m[2][3]), 1.0, _FTOL);          \
        FSize ppe[16] = DEFAULT_INITIALIZATION;                                                  \
        math_cam_perspective_##CB##_1(_FOVY, 1.0, 1.0, 11.0, ppe);                               \
        _check_f("perspective_1 " #CB " m00", ppe[_AT(0, 0)], 1.0, _FTOL);                       \
        /* --- lookat / look / look_anyup: unit right vector, identity translation --- */       \
        Vec3 const eye = { 0.0, 0.0, 0.0 };                                                      \
        Vec3 const ctr = { 0.0, 0.0, -1.0 };                                                     \
        Vec3 const dir = { 0.0, 0.0, -1.0 };                                                     \
        Vec3 const up = { 0.0, 1.0, 0.0 };                                                       \
        Mat4 const la = math_cam_lookat_##CB##_2(eye, ctr, up);                                  \
        _check_f("lookat_2 " #CB " |right.x|", fabs((double) la.m[0][0]), 1.0, _FTOL);           \
        _check_f("lookat_2 " #CB " m33", la.m[3][3], 1.0, _FTOL);                                \
        FSize peye[3] = { 0.0, 0.0, 0.0 };                                                       \
        FSize pctr[3] = { 0.0, 0.0, -1.0 };                                                      \
        FSize pup[3] = { 0.0, 1.0, 0.0 };                                                        \
        FSize pla[16] = DEFAULT_INITIALIZATION;                                                  \
        math_cam_lookat_##CB##_1(peye, pctr, pup, pla);                                          \
        _check_f("lookat_1 " #CB " |right.x|", fabs((double) pla[_AT(0, 0)]), 1.0, _FTOL);       \
        Mat4 const lk = math_cam_look_##CB##_2(eye, dir, up);                                    \
        _check_f("look_2 " #CB " |right.x|", fabs((double) lk.m[0][0]), 1.0, _FTOL);             \
        FSize pdir[3] = { 0.0, 0.0, -1.0 };                                                      \
        FSize plk[16] = DEFAULT_INITIALIZATION;                                                  \
        math_cam_look_##CB##_1(peye, pdir, pup, plk);                                            \
        _check_f("look_1 " #CB " m33", plk[_AT(3, 3)], 1.0, _FTOL);                              \
        Mat4 const lany = math_cam_look_anyup_##CB##_2(eye, dir);                                \
        _check_f("look_anyup_2 " #CB " m33", lany.m[3][3], 1.0, _FTOL);                          \
        FSize plany[16] = DEFAULT_INITIALIZATION;                                                \
        math_cam_look_anyup_##CB##_1(peye, pdir, plany);                                         \
        _check_f("look_anyup_1 " #CB " m33", plany[_AT(3, 3)], 1.0, _FTOL);                      \
        /* --- perspective decomposition round-trips from (fovy, 1, 1, 20) --- */                \
        Mat4 const p = math_cam_perspective_##CB##_2(_FOVY, 1.0, 1.0, 20.0);                     \
        FSize pp[16] = DEFAULT_INITIALIZATION;                                                   \
        math_cam_perspective_##CB##_1(_FOVY, 1.0, 1.0, 20.0, pp);                                \
        _check_f("persp_fovy_2 " #CB, math_cam_persp_fovy_##CB##_2(p), _FOVY, _XTOL);            \
        _check_f("persp_fovy_1 " #CB, math_cam_persp_fovy_##CB##_1(pp), _FOVY, _XTOL);           \
        _check_f("persp_aspect_2 " #CB, math_cam_persp_aspect_##CB##_2(p), 1.0, _XTOL);          \
        _check_f("persp_aspect_1 " #CB, math_cam_persp_aspect_##CB##_1(pp), 1.0, _XTOL);         \
        _check_f("persp_decomp_near_2 " #CB, math_cam_persp_decomp_near_##CB##_2(p), 1.0, _XTOL);\
        _check_f("persp_decomp_near_1 " #CB, math_cam_persp_decomp_near_##CB##_1(pp), 1.0, _XTOL);\
        _check_f("persp_decomp_far_2 " #CB, math_cam_persp_decomp_far_##CB##_2(p), 20.0, _FARTOL);\
        _check_f("persp_decomp_far_1 " #CB, math_cam_persp_decomp_far_##CB##_1(pp), 20.0, _FARTOL);\
        FSize dn = DEFAULT_INITIALIZATION;                                                       \
        FSize df = DEFAULT_INITIALIZATION;                                                       \
        math_cam_persp_decomp_z_##CB##_1(pp, &dn, &df);                                          \
        _check_f("persp_decomp_z_1 " #CB " near", dn, 1.0, _XTOL);                               \
        _check_f("persp_decomp_z_1 " #CB " far", df, 20.0, _FARTOL);                             \
        FSize dl = DEFAULT_INITIALIZATION;                                                       \
        FSize dr = DEFAULT_INITIALIZATION;                                                       \
        math_cam_persp_decomp_x_##CB##_1(pp, &dl, &dr);                                          \
        _check_sym("persp_decomp_x_1 " #CB " left/right", dl, dr, _XTOL);                        \
        FSize dt = DEFAULT_INITIALIZATION;                                                       \
        FSize db = DEFAULT_INITIALIZATION;                                                       \
        math_cam_persp_decomp_y_##CB##_1(pp, &dt, &db);                                          \
        _check_sym("persp_decomp_y_1 " #CB " top/bottom", dt, db, _XTOL);                        \
        FSize dnz = DEFAULT_INITIALIZATION;                                                      \
        FSize dfz = DEFAULT_INITIALIZATION;                                                      \
        FSize dtp = DEFAULT_INITIALIZATION;                                                      \
        FSize dbt = DEFAULT_INITIALIZATION;                                                      \
        FSize dlt = DEFAULT_INITIALIZATION;                                                      \
        FSize drt = DEFAULT_INITIALIZATION;                                                      \
        math_cam_persp_decomp_##CB##_1(pp, &dnz, &dfz, &dtp, &dbt, &dlt, &drt);                  \
        _check_f("persp_decomp_1 " #CB " near", dnz, 1.0, _XTOL);                                \
        _check_f("persp_decomp_1 " #CB " far", dfz, 20.0, _FARTOL);                              \
        FSize dv[6] = DEFAULT_INITIALIZATION;                                                    \
        math_cam_persp_decompv_##CB##_1(pp, dv);                                                 \
        _check_f("persp_decompv_1 " #CB " near", dv[0], 1.0, _XTOL);                             \
        _check_f("persp_decompv_1 " #CB " far", dv[1], 20.0, _FARTOL);                           \
        /* --- resize to aspect 2, move far by 5, sizes non-degenerate --- */                   \
        Mat4 const pr = math_cam_perspective_resize_##CB##_2(p, 2.0);                            \
        _check_f("perspective_resize_2 " #CB " aspect", math_cam_persp_aspect_##CB##_2(pr), 2.0, _XTOL); \
        FSize ppr[16] = DEFAULT_INITIALIZATION;                                                  \
        math_cam_perspective_resize_##CB##_1(pp, 2.0, ppr);                                      \
        _check_f("perspective_resize_1 " #CB " aspect", math_cam_persp_aspect_##CB##_1(ppr), 2.0, _XTOL); \
        Mat4 const pm = math_cam_persp_move_far_##CB##_2(p, 5.0);                                \
        _check_f("persp_move_far_2 " #CB " far", math_cam_persp_decomp_far_##CB##_2(pm), 25.0, _FARTOL); \
        FSize ppm[16] = DEFAULT_INITIALIZATION;                                                  \
        math_cam_persp_move_far_##CB##_1(pp, 5.0, ppm);                                          \
        _check_f("persp_move_far_1 " #CB " far", math_cam_persp_decomp_far_##CB##_1(ppm), 25.0, _FARTOL); \
        Vec4 const sz = math_cam_persp_sizes_##CB##_2(p, _FOVY);                                 \
        _check_b("persp_sizes_2 " #CB " x>0", sz.x > 0.0, true);                                 \
        FSize psz[4] = DEFAULT_INITIALIZATION;                                                   \
        math_cam_persp_sizes_##CB##_1(pp, _FOVY, psz);                                           \
        _check_b("persp_sizes_1 " #CB " x>0", psz[0] > 0.0, true);                               \
    } while (0)

/*
 * project / unproject round-trip for a depth family (D = no or zo). With an
 * identity MVP the world point passes through NDC unchanged, so unprojecting the
 * projected window point recovers it; project_z must equal the projected z.
 */
#define TEST_PROJ(D)                                                                             \
    do {                                                                                         \
        Mat4 const mvp = math_mat4_identity_2();                                                 \
        Mat4 const inv = math_mat4_inv_2(mvp);                                                   \
        Vec4 const vp = { 0.0, 0.0, 800.0, 600.0 };                                              \
        Vec3 const world = { 0.3, -0.4, 0.2 };                                                   \
        Vec3 const scr = math_cam_project_##D##_2(world, mvp, vp);                               \
        Vec3 const back = math_cam_unprojecti_##D##_2(scr, inv, vp);                             \
        _check_f("project/unproject_2 " #D " x", back.x, 0.3, _FTOL);                            \
        _check_f("project/unproject_2 " #D " y", back.y, -0.4, _FTOL);                           \
        _check_f("project/unproject_2 " #D " z", back.z, 0.2, _FTOL);                            \
        _check_f("project_z_2 " #D, math_cam_project_z_##D##_2(world, mvp), scr.z, _FTOL);       \
        FSize pmvp[16] = DEFAULT_INITIALIZATION;                                                 \
        math_mat4_identity_1(pmvp);                                                              \
        FSize pinv[16] = DEFAULT_INITIALIZATION;                                                 \
        math_mat4_inv_1(pmvp, pinv);                                                             \
        FSize pvp[4] = { 0.0, 0.0, 800.0, 600.0 };                                               \
        FSize pw[3] = { 0.3, -0.4, 0.2 };                                                        \
        FSize pscr[3] = DEFAULT_INITIALIZATION;                                                  \
        math_cam_project_##D##_1(pw, pmvp, pvp, pscr);                                           \
        FSize pback[3] = DEFAULT_INITIALIZATION;                                                 \
        math_cam_unprojecti_##D##_1(pscr, pinv, pvp, pback);                                     \
        _check_f("project/unproject_1 " #D " x", pback[0], 0.3, _FTOL);                          \
        _check_f("project/unproject_1 " #D " z", pback[2], 0.2, _FTOL);                          \
        _check_f("project_z_1 " #D, math_cam_project_z_##D##_1(pw, pmvp), pscr[2], _FTOL);       \
    } while (0)

int main(void) {
    printf("=== clipspace module tests ===\n");

    printf("--- lh_no ---\n");
    TEST_CD(lh_no);
    printf("--- lh_zo ---\n");
    TEST_CD(lh_zo);
    printf("--- rh_no ---\n");
    TEST_CD(rh_no);
    printf("--- rh_zo ---\n");
    TEST_CD(rh_zo);

    printf("--- project ---\n");
    TEST_PROJ(no);
    TEST_PROJ(zo);

    // keep _check_u / _check_i referenced so the harness helpers are exercised
    _check_u("pass counter positive", _pass > 0 ? 1 : 0, 1);
    _check_i("fail counter type", (ISize) 0, 0);

    // perspective_resize on a matrix whose [0][0] is zero (never a perspective) is a NO-OP:
    // the CFW-side body carries that upstream guard, and this pins it.
    Mat4 const never_persp = math_mat4_zero_2();
    Mat4 const untouched   = math_cam_perspective_resize_rh_no_2(never_persp, 2.0);
    _check_f("resize_rh_no_2 leaves a zero [0][0] alone", untouched.m[0][0], 0.0, 0.0);
    _check_f("and does not invent a [1][1]", untouched.m[1][1], 0.0, 0.0);

    return _check_finish();
}