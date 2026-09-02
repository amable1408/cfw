/*
 * test_affine2d.c - Tests for include/math/affine2d.c (full glmc_*2d coverage)
 */

#include <math.h>
#include <stdio.h>

#include <math/affine2d.h>

#include "check.h"

// === Helpers ===

#define _TOL 1e-9
#define _FTOL 1e-5
#define _HALF_PI 1.57079632679489661923

int main(void) {
    printf("=== affine2d module tests ===\n");

    // Identity source in both forms.
    //   struct m[col][row]; raw flat index = col * 3 + row.
    Mat3 const id = { { { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } } };
    FSize pid[9] = { 1.0, 0.0, 0.0,  0.0, 1.0, 0.0,  0.0, 0.0, 1.0 };
    FSize po[9] = DEFAULT_INITIALIZATION;

    // --- translate2d_make: producer, offset lands in column 2 ---
    printf("--- translate2d_make ---\n");

    Vec2 const tv = { 5.0, 7.0 };
    Mat3 const tm = math_affine2d_translate2d_make_2(tv);
    _check_f("translate2d_make_2 tx col2row0", tm.m[2][0], 5.0, _FTOL);
    _check_f("translate2d_make_2 ty col2row1", tm.m[2][1], 7.0, _FTOL);
    _check_f("translate2d_make_2 diag00", tm.m[0][0], 1.0, _FTOL);
    _check_f("translate2d_make_2 diag22", tm.m[2][2], 1.0, _FTOL);
    _check_f("translate2d_make_2 off01", tm.m[0][1], 0.0, _FTOL);

    FSize ptv[2] = { 5.0, 7.0 };
    math_affine2d_translate2d_make_1(ptv, po);
    // col2 row0 = index 6, col2 row1 = index 7
    _check_f("translate2d_make_1 tx [6]", po[6], 5.0, _FTOL);
    _check_f("translate2d_make_1 ty [7]", po[7], 7.0, _FTOL);
    _check_f("translate2d_make_1 diag [0]", po[0], 1.0, _FTOL);

    // --- translate2d: pure transform over a source ---
    printf("--- translate2d ---\n");

    Mat3 const tt = math_affine2d_translate2d_2(id, tv);
    _check_f("translate2d_2 tx col2row0", tt.m[2][0], 5.0, _FTOL);
    _check_f("translate2d_2 ty col2row1", tt.m[2][1], 7.0, _FTOL);
    math_affine2d_translate2d_1(pid, ptv, po);
    _check_f("translate2d_1 tx [6]", po[6], 5.0, _FTOL);
    _check_f("translate2d_1 ty [7]", po[7], 7.0, _FTOL);

    // --- translate2d_x / translate2d_y: single-axis offset ---
    printf("--- translate2d_x / translate2d_y ---\n");

    Mat3 const tx = math_affine2d_translate2d_x_2(id, 3.0);
    _check_f("translate2d_x_2 col2row0", tx.m[2][0], 3.0, _FTOL);
    _check_f("translate2d_x_2 col2row1", tx.m[2][1], 0.0, _FTOL);
    math_affine2d_translate2d_x_1(pid, 3.0, po);
    _check_f("translate2d_x_1 [6]", po[6], 3.0, _FTOL);

    Mat3 const ty = math_affine2d_translate2d_y_2(id, 4.0);
    _check_f("translate2d_y_2 col2row1", ty.m[2][1], 4.0, _FTOL);
    _check_f("translate2d_y_2 col2row0", ty.m[2][0], 0.0, _FTOL);
    math_affine2d_translate2d_y_1(pid, 4.0, po);
    _check_f("translate2d_y_1 [7]", po[7], 4.0, _FTOL);

    // --- rotate2d_make: 90 degrees -> c=0, s=1 ---
    printf("--- rotate2d_make ---\n");

    // rotate2d_make(90): m[0][0]=c=0, m[0][1]=s=1, m[1][0]=-s=-1, m[1][1]=c=0.
    Mat3 const rm = math_affine2d_rotate2d_make_2(_HALF_PI);
    _check_f("rotate2d_make_2 [0][0]", rm.m[0][0], 0.0, _FTOL);
    _check_f("rotate2d_make_2 [0][1]", rm.m[0][1], 1.0, _FTOL);
    _check_f("rotate2d_make_2 [1][0]", rm.m[1][0], -1.0, _FTOL);
    _check_f("rotate2d_make_2 [1][1]", rm.m[1][1], 0.0, _FTOL);
    _check_f("rotate2d_make_2 [2][2]", rm.m[2][2], 1.0, _FTOL);
    math_affine2d_rotate2d_make_1(_HALF_PI, po);
    // col0row0 = [0], col0row1 = [1], col1row0 = [3], col1row1 = [4]
    _check_f("rotate2d_make_1 [0]", po[0], 0.0, _FTOL);
    _check_f("rotate2d_make_1 [1]", po[1], 1.0, _FTOL);
    _check_f("rotate2d_make_1 [3]", po[3], -1.0, _FTOL);

    // --- rotate2d: pure transform, identity source == make ---
    printf("--- rotate2d ---\n");

    Mat3 const rt = math_affine2d_rotate2d_2(id, _HALF_PI);
    _check_f("rotate2d_2 [0][1]", rt.m[0][1], 1.0, _FTOL);
    _check_f("rotate2d_2 [1][0]", rt.m[1][0], -1.0, _FTOL);
    math_affine2d_rotate2d_1(pid, _HALF_PI, po);
    _check_f("rotate2d_1 [1]", po[1], 1.0, _FTOL);
    _check_f("rotate2d_1 [3]", po[3], -1.0, _FTOL);

    // --- scale2d_make: diagonal scale ---
    printf("--- scale2d_make ---\n");

    Vec2 const sv = { 2.0, 3.0 };
    Mat3 const sm = math_affine2d_scale2d_make_2(sv);
    _check_f("scale2d_make_2 sx [0][0]", sm.m[0][0], 2.0, _FTOL);
    _check_f("scale2d_make_2 sy [1][1]", sm.m[1][1], 3.0, _FTOL);
    _check_f("scale2d_make_2 diag22", sm.m[2][2], 1.0, _FTOL);
    _check_f("scale2d_make_2 off01", sm.m[0][1], 0.0, _FTOL);
    FSize psv[2] = { 2.0, 3.0 };
    math_affine2d_scale2d_make_1(psv, po);
    _check_f("scale2d_make_1 sx [0]", po[0], 2.0, _FTOL);
    _check_f("scale2d_make_1 sy [4]", po[4], 3.0, _FTOL);

    // --- scale2d: pure transform, diagonal ---
    printf("--- scale2d ---\n");

    Mat3 const st = math_affine2d_scale2d_2(id, sv);
    _check_f("scale2d_2 sx [0][0]", st.m[0][0], 2.0, _FTOL);
    _check_f("scale2d_2 sy [1][1]", st.m[1][1], 3.0, _FTOL);
    math_affine2d_scale2d_1(pid, psv, po);
    _check_f("scale2d_1 sx [0]", po[0], 2.0, _FTOL);
    _check_f("scale2d_1 sy [4]", po[4], 3.0, _FTOL);

    // --- scale2d_uni: uniform diagonal ---
    printf("--- scale2d_uni ---\n");

    Mat3 const su = math_affine2d_scale2d_uni_2(id, 2.0);
    _check_f("scale2d_uni_2 [0][0]", su.m[0][0], 2.0, _FTOL);
    _check_f("scale2d_uni_2 [1][1]", su.m[1][1], 2.0, _FTOL);
    math_affine2d_scale2d_uni_1(pid, 2.0, po);
    _check_f("scale2d_uni_1 [0]", po[0], 2.0, _FTOL);
    _check_f("scale2d_uni_1 [4]", po[4], 2.0, _FTOL);

    // keep _check_u / _check_i referenced so the harness helpers are exercised
    _check_u("pass counter positive", _pass > 0 ? 1 : 0, 1);
    _check_i("fail counter type", (ISize) 0, 0);

    return _check_finish();
}