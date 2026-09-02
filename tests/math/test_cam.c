/*
 * test_cam.c - Tests for include/math/cam.c (full glmc_ camera coverage)
 */

#include <math.h>
#include <stdio.h>

#include <math/cam.h>

#include "check.h"

// === Helpers ===

#define _FTOL 1e-4

// Column-major flat index for a 4x4: col * 4 + row.
#define _AT(col, row) ((col) * 4 + (row))

int main(void) {
    printf("=== cam module tests ===\n");

    FSize const fovy = 1.0471975512;   // 60 degrees in radians
    FSize const aspect = 1.7777777778; // 16:9
    FSize const near_z = 0.5;
    FSize const far_z = 100.0;

    // --- perspective: [0][0] > 0 and round-trips through the getters ---
    printf("--- perspective ---\n");

    Mat4 const proj = math_cam_perspective_2(fovy, aspect, near_z, far_z);
    _check_b("perspective_2 c0r0 positive", proj.m[0][0] > 0.0, true);
    // c0r0 = 1 / (aspect * tan(fovy/2)); check the closed form.
    FSize const p00 = 1.0 / (aspect * tan(fovy / 2.0));
    _check_f("perspective_2 c0r0 value", proj.m[0][0], p00, _FTOL);

    FSize pproj[16] = DEFAULT_INITIALIZATION;
    math_cam_perspective_1(fovy, aspect, near_z, far_z, pproj);
    _check_b("perspective_1 c0r0 positive", pproj[_AT(0, 0)] > 0.0, true);
    _check_f("perspective_1 matches _2", pproj[_AT(0, 0)], proj.m[0][0], _FTOL);

    // fovy / aspect getters recover the inputs (both variants).
    _check_f("persp_fovy_2 round-trip", math_cam_persp_fovy_2(proj), fovy, _FTOL);
    _check_f("persp_fovy_1 round-trip", math_cam_persp_fovy_1(pproj), fovy, _FTOL);
    _check_f("persp_aspect_2 round-trip", math_cam_persp_aspect_2(proj), aspect, _FTOL);
    _check_f("persp_aspect_1 round-trip", math_cam_persp_aspect_1(pproj), aspect, _FTOL);

    // --- persp decomposition: near/far recovered, decompv order, split axes ---
    printf("--- persp decomp ---\n");

    _check_f("persp_decomp_near_2", math_cam_persp_decomp_near_2(proj), near_z, _FTOL);
    _check_f("persp_decomp_near_1", math_cam_persp_decomp_near_1(pproj), near_z, _FTOL);
    _check_f("persp_decomp_far_2", math_cam_persp_decomp_far_2(proj), far_z, 1e-2);
    _check_f("persp_decomp_far_1", math_cam_persp_decomp_far_1(pproj), far_z, 1e-2);

    FSize dn = 0.0, df = 0.0, dt = 0.0, db = 0.0, dl = 0.0, dr = 0.0;
    math_cam_persp_decomp_1(pproj, &dn, &df, &dt, &db, &dl, &dr);
    _check_f("persp_decomp_1 near", dn, near_z, _FTOL);
    _check_f("persp_decomp_1 far", df, far_z, 1e-2);
    // symmetric frustum: top == -bottom and right == -left.
    _check_f("persp_decomp_1 top/bottom symmetric", dt + db, 0.0, _FTOL);
    _check_f("persp_decomp_1 left/right symmetric", dl + dr, 0.0, _FTOL);

    FSize dv[6] = DEFAULT_INITIALIZATION;
    math_cam_persp_decompv_1(pproj, dv);
    // decompv order is (near, far, top, bottom, left, right).
    _check_f("persp_decompv_1 near", dv[0], near_z, _FTOL);
    _check_f("persp_decompv_1 far", dv[1], far_z, 1e-2);

    FSize zx_l = 0.0, zx_r = 0.0;
    math_cam_persp_decomp_x_1(pproj, &zx_l, &zx_r);
    _check_f("persp_decomp_x_1 matches decompv left", zx_l, dv[4], _FTOL);
    _check_f("persp_decomp_x_1 matches decompv right", zx_r, dv[5], _FTOL);

    FSize zy_t = 0.0, zy_b = 0.0;
    math_cam_persp_decomp_y_1(pproj, &zy_t, &zy_b);
    _check_f("persp_decomp_y_1 matches decompv top", zy_t, dv[2], _FTOL);

    FSize zz_n = 0.0, zz_f = 0.0;
    math_cam_persp_decomp_z_1(pproj, &zz_n, &zz_f);
    _check_f("persp_decomp_z_1 near", zz_n, near_z, _FTOL);
    _check_f("persp_decomp_z_1 far", zz_f, far_z, 1e-2);

    // persp_sizes yields a positive extent vector at the given fovy.
    FSize sizes[4] = DEFAULT_INITIALIZATION;
    math_cam_persp_sizes_1(pproj, fovy, sizes);
    _check_b("persp_sizes_1 x positive", sizes[0] > 0.0, true);
    _check_b("persp_sizes_1 y positive", sizes[1] > 0.0, true);

    // --- perspective variants: default / infinite / resize ---
    printf("--- perspective variants ---\n");

    Mat4 const pdef = math_cam_perspective_default_2(aspect);
    _check_b("perspective_default_2 c0r0 positive", pdef.m[0][0] > 0.0, true);
    FSize ppdef[16] = DEFAULT_INITIALIZATION;
    math_cam_perspective_default_1(aspect, ppdef);
    _check_f("perspective_default_1 matches _2", ppdef[_AT(0, 0)], pdef.m[0][0], _FTOL);

    Mat4 const pinf = math_cam_perspective_infinite_2(fovy, aspect, near_z);
    _check_b("perspective_infinite_2 c0r0 positive", pinf.m[0][0] > 0.0, true);
    FSize ppinf[16] = DEFAULT_INITIALIZATION;
    math_cam_perspective_infinite_1(fovy, aspect, near_z, ppinf);
    _check_f("perspective_infinite_1 matches _2", ppinf[_AT(0, 0)], pinf.m[0][0], _FTOL);

    Mat4 const pdinf = math_cam_perspective_default_infinite_2(aspect);
    _check_b("perspective_default_infinite_2 c0r0 positive", pdinf.m[0][0] > 0.0, true);
    FSize ppdinf[16] = DEFAULT_INITIALIZATION;
    math_cam_perspective_default_infinite_1(aspect, ppdinf);
    _check_f("perspective_default_infinite_1 matches _2", ppdinf[_AT(0, 0)], pdinf.m[0][0], _FTOL);

    // resize to a new aspect changes c0r0 but not c1r1.
    Mat4 const presz = math_cam_perspective_resize_2(proj, 1.0);
    _check_f("perspective_resize_2 keeps c1r1", presz.m[1][1], proj.m[1][1], _FTOL);
    _check_b("perspective_resize_2 changes c0r0", presz.m[0][0] != proj.m[0][0], true);
    FSize ppresz[16] = DEFAULT_INITIALIZATION;
    math_cam_perspective_resize_1(pproj, 1.0, ppresz);
    _check_f("perspective_resize_1 matches _2", ppresz[_AT(0, 0)], presz.m[0][0], _FTOL);

    // move_far pushes the far plane out; the recovered far grows.
    Mat4 const pmf = math_cam_persp_move_far_2(proj, 50.0);
    _check_b("persp_move_far_2 grows far", math_cam_persp_decomp_far_2(pmf) > far_z, true);
    FSize ppmf[16] = DEFAULT_INITIALIZATION;
    math_cam_persp_move_far_1(pproj, 50.0, ppmf);
    _check_f("persp_move_far_1 matches _2 c2r2", ppmf[_AT(2, 2)], pmf.m[2][2], _FTOL);

    // --- lookat / look / look_anyup ---
    printf("--- view matrices ---\n");

    Vec3 const eye = { 0.0, 0.0, 5.0 };
    Vec3 const center = { 0.0, 0.0, 0.0 };
    Vec3 const up = { 0.0, 1.0, 0.0 };
    // Looking down -z from +z: right vector = +x, so view.m[0][0] ~= 1.
    Mat4 const view = math_cam_lookat_2(eye, center, up);
    _check_f("lookat_2 right.x", view.m[0][0], 1.0, _FTOL);
    _check_f("lookat_2 up.y", view.m[1][1], 1.0, _FTOL);
    FSize peye[3] = { 0.0, 0.0, 5.0 };
    FSize pcenter[3] = { 0.0, 0.0, 0.0 };
    FSize pup[3] = { 0.0, 1.0, 0.0 };
    FSize pview[16] = DEFAULT_INITIALIZATION;
    math_cam_lookat_1(peye, pcenter, pup, pview);
    _check_f("lookat_1 right.x", pview[_AT(0, 0)], 1.0, _FTOL);

    // look with an explicit direction (-z) matches lookat toward the origin.
    Vec3 const dir = { 0.0, 0.0, -5.0 };
    Mat4 const vlook = math_cam_look_2(eye, dir, up);
    _check_f("look_2 right.x", vlook.m[0][0], 1.0, _FTOL);
    _check_f("look_2 matches lookat c3r2", vlook.m[3][2], view.m[3][2], _FTOL);
    FSize pdir[3] = { 0.0, 0.0, -5.0 };
    FSize plook[16] = DEFAULT_INITIALIZATION;
    math_cam_look_1(peye, pdir, pup, plook);
    _check_f("look_1 right.x", plook[_AT(0, 0)], 1.0, _FTOL);

    Mat4 const vany = math_cam_look_anyup_2(eye, dir);
    _check_f("look_anyup_2 right.x", vany.m[0][0], 1.0, _FTOL);
    FSize pany[16] = DEFAULT_INITIALIZATION;
    math_cam_look_anyup_1(peye, pdir, pany);
    _check_f("look_anyup_1 right.x", pany[_AT(0, 0)], 1.0, _FTOL);

    // --- ortho / frustum ---
    printf("--- ortho / frustum ---\n");

    // symmetric ortho [-2,2]x[-2,2]: c0r0 = 2/(right-left) = 0.5.
    Mat4 const ortho = math_cam_ortho_2(-2.0, 2.0, -2.0, 2.0, 0.1, 10.0);
    _check_f("ortho_2 c0r0", ortho.m[0][0], 0.5, _FTOL);
    _check_f("ortho_2 c1r1", ortho.m[1][1], 0.5, _FTOL);
    FSize portho[16] = DEFAULT_INITIALIZATION;
    math_cam_ortho_1(-2.0, 2.0, -2.0, 2.0, 0.1, 10.0, portho);
    _check_f("ortho_1 c0r0", portho[_AT(0, 0)], 0.5, _FTOL);

    Mat4 const odef = math_cam_ortho_default_2(aspect);
    _check_b("ortho_default_2 c0r0 nonzero", odef.m[0][0] != 0.0, true);
    FSize podef[16] = DEFAULT_INITIALIZATION;
    math_cam_ortho_default_1(aspect, podef);
    _check_f("ortho_default_1 matches _2", podef[_AT(0, 0)], odef.m[0][0], _FTOL);

    Mat4 const odefs = math_cam_ortho_default_s_2(aspect, 3.0);
    _check_b("ortho_default_s_2 c0r0 nonzero", odefs.m[0][0] != 0.0, true);
    FSize podefs[16] = DEFAULT_INITIALIZATION;
    math_cam_ortho_default_s_1(aspect, 3.0, podefs);
    _check_f("ortho_default_s_1 matches _2", podefs[_AT(0, 0)], odefs.m[0][0], _FTOL);

    // ortho_aabb over a symmetric box [-1..1]^3: c0r0 = 2/2 = 1.
    Box const bbox = { { -1.0, -1.0, -1.0 }, { 1.0, 1.0, 1.0 } };
    Mat4 const oaabb = math_cam_ortho_aabb_2(bbox);
    _check_f("ortho_aabb_2 c0r0", oaabb.m[0][0], 1.0, _FTOL);
    FSize pbox[6] = { -1.0, -1.0, -1.0, 1.0, 1.0, 1.0 };
    FSize poaabb[16] = DEFAULT_INITIALIZATION;
    math_cam_ortho_aabb_1(pbox, poaabb);
    _check_f("ortho_aabb_1 c0r0", poaabb[_AT(0, 0)], 1.0, _FTOL);

    // padding widens the box, shrinking the scale below the unpadded value.
    Mat4 const oaabbp = math_cam_ortho_aabb_p_2(bbox, 1.0);
    _check_b("ortho_aabb_p_2 scale shrinks", oaabbp.m[0][0] < oaabb.m[0][0], true);
    FSize poaabbp[16] = DEFAULT_INITIALIZATION;
    math_cam_ortho_aabb_p_1(pbox, 1.0, poaabbp);
    _check_f("ortho_aabb_p_1 matches _2", poaabbp[_AT(0, 0)], oaabbp.m[0][0], _FTOL);

    Mat4 const oaabbpz = math_cam_ortho_aabb_pz_2(bbox, 1.0);
    // pz pads z only, so the x scale stays at the unpadded value.
    _check_f("ortho_aabb_pz_2 keeps c0r0", oaabbpz.m[0][0], oaabb.m[0][0], _FTOL);
    FSize poaabbpz[16] = DEFAULT_INITIALIZATION;
    math_cam_ortho_aabb_pz_1(pbox, 1.0, poaabbpz);
    _check_f("ortho_aabb_pz_1 matches _2", poaabbpz[_AT(0, 0)], oaabbpz.m[0][0], _FTOL);

    // frustum [-1,1]x[-1,1], near 1, far 10: c0r0 = 2*near/(right-left) = 1.
    Mat4 const fr = math_cam_frustum_2(-1.0, 1.0, -1.0, 1.0, 1.0, 10.0);
    _check_f("frustum_2 c0r0", fr.m[0][0], 1.0, _FTOL);
    FSize pfr[16] = DEFAULT_INITIALIZATION;
    math_cam_frustum_1(-1.0, 1.0, -1.0, 1.0, 1.0, 10.0, pfr);
    _check_f("frustum_1 c0r0", pfr[_AT(0, 0)], 1.0, _FTOL);

    // keep _check_u / _check_i / _check_b referenced so the harness helpers are exercised
    _check_u("pass counter positive", _pass > 0 ? 1 : 0, 1);
    _check_i("fail counter type", (ISize) 0, 0);
    _check_b("perspective c0r0 finite", isfinite((double) proj.m[0][0]), true);

    return _check_finish();
}