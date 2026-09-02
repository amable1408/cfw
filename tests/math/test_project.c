/*
 * test_project.c - Tests for include/math/project.c (full glmc_*project* coverage)
 */

#include <math.h>
#include <stdio.h>

#include <math/project.h>

#include "check.h"

// === Helpers ===

#define _FTOL 1e-6
#define _RTOL 1e-4

int main(void) {
    printf("=== project module tests ===\n");

    // Build a real perspective MVP and its inverse with the compiled cglm archive.
    // The viewport is (x, y, width, height); the world point sits inside the
    // frustum (in front of the camera along -z) so the round trip is well posed.
    mat4 cmvp = DEFAULT_INITIALIZATION;
    mat4 cinv = DEFAULT_INITIALIZATION;

    glmc_perspective((float) (45.0 * MATH_DEG_TO_RAD), 800.0f / 600.0f, 0.1f, 100.0f, cmvp);
    glmc_mat4_inv(cmvp, cinv);

    Mat4 mvp = DEFAULT_INITIALIZATION;
    Mat4 inv = DEFAULT_INITIALIZATION;
    FSize pmvp[16] = DEFAULT_INITIALIZATION;
    FSize pinv[16] = DEFAULT_INITIALIZATION;

    for (USize col = 0; col < 4; col++) {
        for (USize row = 0; row < 4; row++) {
            mvp.m[col][row] = (FSize) cmvp[col][row];
            inv.m[col][row] = (FSize) cinv[col][row];
            pmvp[col * 4 + row] = (FSize) cmvp[col][row];
            pinv[col * 4 + row] = (FSize) cinv[col][row];
        }
    }

    Vec3 const world = { 0.35, -0.2, -6.0 };
    FSize pworld[3] = { 0.35, -0.2, -6.0 };
    Vec4 const vp = { 0.0, 0.0, 800.0, 600.0 };
    FSize pvp[4] = { 0.0, 0.0, 800.0, 600.0 };
    FSize po[3] = DEFAULT_INITIALIZATION;

    // --- project: object space -> window space ---
    printf("--- project ---\n");

    Vec3 const win = math_project_2(world, mvp, vp);
    // Window x, y must land inside the viewport rectangle.
    printf("  INFO  win = (%f, %f, %f)\n", (double) win.x, (double) win.y, (double) win.z);
    _check_f("project_2.x in viewport (>=0)", win.x >= 0.0 ? 1.0 : 0.0, 1.0, _FTOL);
    _check_f("project_2.x in viewport (<=800)", win.x <= 800.0 ? 1.0 : 0.0, 1.0, _FTOL);
    _check_f("project_2.y in viewport (<=600)", win.y <= 600.0 ? 1.0 : 0.0, 1.0, _FTOL);

    // The raw (_1) variant must agree with the struct (_2) variant.
    math_project_1(pworld, pmvp, pvp, po);
    _check_f("project_1.x matches _2", po[0], win.x, _FTOL);
    _check_f("project_1.y matches _2", po[1], win.y, _FTOL);
    _check_f("project_1.z matches _2", po[2], win.z, _FTOL);

    // --- project_z: only the window depth ---
    printf("--- project_z ---\n");

    // project_z must equal the z component produced by the full projection.
    _check_f("project_z_2 matches project.z", math_project_z_2(world, mvp), win.z, _FTOL);
    _check_f("project_z_1 matches project.z", math_project_z_1(pworld, pmvp), win.z, _FTOL);

    // --- pickmatrix: build a picking-region matrix ---
    printf("--- pickmatrix ---\n");

    // center=(400,300), size=(5,5), vp=(0,0,800,600): the translation cancels
    // (center is the viewport center), leaving a pure scale of (vp.w/size, vp.h/size, 1)
    // = (160, 120, 1) on the diagonal, with m[3][3] == 1.
    Vec2 const center = { 400.0, 300.0 };
    Vec2 const size = { 5.0, 5.0 };
    FSize pcenter[2] = { 400.0, 300.0 };
    FSize psize[2] = { 5.0, 5.0 };

    Mat4 const pick = math_project_pickmatrix_2(center, size, vp);
    _check_f("pickmatrix_2 m[0][0]", pick.m[0][0], 160.0, _FTOL);
    _check_f("pickmatrix_2 m[1][1]", pick.m[1][1], 120.0, _FTOL);
    _check_f("pickmatrix_2 m[2][2]", pick.m[2][2], 1.0, _FTOL);
    _check_f("pickmatrix_2 m[3][3]", pick.m[3][3], 1.0, _FTOL);
    _check_f("pickmatrix_2 m[3][0] (no translate)", pick.m[3][0], 0.0, _FTOL);

    FSize ppick[16] = DEFAULT_INITIALIZATION;
    math_project_pickmatrix_1(pcenter, psize, pvp, ppick);
    _check_f("pickmatrix_1 m[0][0]", ppick[0 * 4 + 0], 160.0, _FTOL);
    _check_f("pickmatrix_1 m[1][1]", ppick[1 * 4 + 1], 120.0, _FTOL);
    _check_f("pickmatrix_1 m[3][3]", ppick[3 * 4 + 3], 1.0, _FTOL);

    // --- unproject: window space -> object space (matrix inverted internally) ---
    printf("--- unproject ---\n");

    // Boundary: project then unproject must round-trip back to the original point.
    Vec3 const back = math_project_unproject_2(win, mvp, vp);
    _check_f("unproject_2 round-trip.x", back.x, world.x, _RTOL);
    _check_f("unproject_2 round-trip.y", back.y, world.y, _RTOL);
    _check_f("unproject_2 round-trip.z", back.z, world.z, _RTOL);

    FSize pwin[3] = { win.x, win.y, win.z };
    math_project_unproject_1(pwin, pmvp, pvp, po);
    _check_f("unproject_1 round-trip.x", po[0], world.x, _RTOL);
    _check_f("unproject_1 round-trip.y", po[1], world.y, _RTOL);
    _check_f("unproject_1 round-trip.z", po[2], world.z, _RTOL);

    // --- unprojecti: window space -> object space (pre-inverted matrix) ---
    printf("--- unprojecti ---\n");

    // Same round trip, but the caller supplies the already-inverted matrix.
    Vec3 const backi = math_project_unprojecti_2(win, inv, vp);
    _check_f("unprojecti_2 round-trip.x", backi.x, world.x, _RTOL);
    _check_f("unprojecti_2 round-trip.y", backi.y, world.y, _RTOL);
    _check_f("unprojecti_2 round-trip.z", backi.z, world.z, _RTOL);

    math_project_unprojecti_1(pwin, pinv, pvp, po);
    _check_f("unprojecti_1 round-trip.x", po[0], world.x, _RTOL);
    _check_f("unprojecti_1 round-trip.y", po[1], world.y, _RTOL);
    _check_f("unprojecti_1 round-trip.z", po[2], world.z, _RTOL);

    // keep _check_u / _check_i referenced so the harness helpers are exercised
    _check_u("pass counter positive", _pass > 0 ? 1 : 0, 1);
    _check_i("fail counter type", (ISize) 0, 0);

    return _check_finish();
}