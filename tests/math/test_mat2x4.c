/*
 * test_mat2x4.c - Tests for include/math/mat2x4.c (full glmc_mat2x4_* coverage)
 */

#include <math.h>
#include <stdio.h>

#include <math/mat2x4.h>

#include "check.h"

// === Helpers ===

#define _TOL 1e-9
#define _FTOL 1e-5

int main(void) {
    printf("=== mat2x4 module tests ===\n");

    // Mat2x4 A: 2 columns of 4 rows, column-major.
    //   column 0 = (1,2,3,4), column 1 = (5,6,7,8)
    //   raw flat (col*4+row): { 1,2,3,4, 5,6,7,8 }
    // As a math (row,col) matrix this is a 4-row 2-col matrix:
    //   | 1 5 |
    //   | 2 6 |
    //   | 3 7 |
    //   | 4 8 |
    Mat2x4 const a = { { { 1.0, 2.0, 3.0, 4.0 }, { 5.0, 6.0, 7.0, 8.0 } } };
    FSize pa[8] = { 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0 };
    FSize po[8] = DEFAULT_INITIALIZATION;

    // --- construction: make / copy / zero ---
    printf("--- construction ---\n");

    Mat2x4 const cp = math_mat2x4_copy_2(a);
    _check_f("copy_2 m[0][0]", cp.m[0][0], 1.0, _TOL);
    _check_f("copy_2 m[0][3]", cp.m[0][3], 4.0, _TOL);
    _check_f("copy_2 m[1][0]", cp.m[1][0], 5.0, _TOL);
    _check_f("copy_2 m[1][3]", cp.m[1][3], 8.0, _TOL);
    math_mat2x4_copy_1(pa, po);
    _check_f("copy_1 [7]", po[7], 8.0, _TOL);

    Mat2x4 const mk = math_mat2x4_make_2(pa);
    _check_f("make_2 m[1][2]", mk.m[1][2], 7.0, _TOL);
    math_mat2x4_make_1(pa, po);
    _check_f("make_1 [4]", po[4], 5.0, _TOL);

    Mat2x4 const zr = math_mat2x4_zero_2();
    _check_f("zero_2 m[0][0]", zr.m[0][0], 0.0, _TOL);
    _check_f("zero_2 m[1][3]", zr.m[1][3], 0.0, _TOL);
    math_mat2x4_zero_1(po);
    _check_f("zero_1 [3]", po[3], 0.0, _TOL);
    _check_f("zero_1 [7]", po[7], 0.0, _TOL);

    // --- scale ---
    printf("--- scale ---\n");

    Mat2x4 const sc = math_mat2x4_scale_2(a, 2.0);
    _check_f("scale_2 m[0][0]", sc.m[0][0], 2.0, _FTOL);
    _check_f("scale_2 m[1][3]", sc.m[1][3], 16.0, _FTOL);
    math_mat2x4_scale_1(pa, 3.0, po);
    _check_f("scale_1 [0]", po[0], 3.0, _FTOL);
    _check_f("scale_1 [7]", po[7], 24.0, _FTOL);

    // --- transpose: mat2x4 -> mat4x2 ---
    printf("--- transpose ---\n");

    // Transpose of the 4x2 math matrix is the 2x4 math matrix:
    //   | 1 2 3 4 |
    //   | 5 6 7 8 |
    // As Mat4x2 (4 cols, 2 rows): col0=(1,5) col1=(2,6) col2=(3,7) col3=(4,8)
    Mat4x2 const t = math_mat2x4_transpose_2(a);
    _check_f("transpose_2 m[0][0]", t.m[0][0], 1.0, _FTOL);
    _check_f("transpose_2 m[0][1]", t.m[0][1], 5.0, _FTOL);
    _check_f("transpose_2 m[3][0]", t.m[3][0], 4.0, _FTOL);
    _check_f("transpose_2 m[3][1]", t.m[3][1], 8.0, _FTOL);
    _check_f("transpose_2 m[2][0]", t.m[2][0], 3.0, _FTOL);
    math_mat2x4_transpose_1(pa, po);
    // Mat4x2 raw flat (col*2+row): {1,5, 2,6, 3,7, 4,8}
    _check_f("transpose_1 [1]", po[1], 5.0, _FTOL);
    _check_f("transpose_1 [6]", po[6], 4.0, _FTOL);

    // --- mulv: mat2x4 * vec2 -> vec4 ---
    printf("--- mulv ---\n");

    // v = (2,3): result = 2*col0 + 3*col1
    //   = 2*(1,2,3,4) + 3*(5,6,7,8) = (17,22,27,32)
    Vec2 const v = { 2.0, 3.0 };
    Vec4 const mv = math_mat2x4_mulv_2(a, v);
    _check_f("mulv_2 .x", mv.x, 17.0, _FTOL);
    _check_f("mulv_2 .y", mv.y, 22.0, _FTOL);
    _check_f("mulv_2 .z", mv.z, 27.0, _FTOL);
    _check_f("mulv_2 .w", mv.w, 32.0, _FTOL);
    FSize pv[2] = { 2.0, 3.0 };
    FSize pmv[4] = DEFAULT_INITIALIZATION;
    math_mat2x4_mulv_1(pa, pv, pmv);
    _check_f("mulv_1 [0]", pmv[0], 17.0, _FTOL);
    _check_f("mulv_1 [3]", pmv[3], 32.0, _FTOL);

    // --- mul: mat2x4 * mat4x2 -> mat4 ---
    printf("--- mul ---\n");

    // B (Mat4x2, 4 cols 2 rows): col0=(1,0) col1=(0,1) col2=(1,0) col3=(0,1)
    // math B (2 rows, 4 cols):  | 1 0 1 0 |
    //                           | 0 1 0 1 |
    // A_math (4x2) * B_math (2x4) = 4x4:
    //   | 1 5 1 5 |
    //   | 2 6 2 6 |
    //   | 3 7 3 7 |
    //   | 4 8 4 8 |
    // cglm mat4 column-major: col0=(1,2,3,4) col1=(5,6,7,8) col2=(1,2,3,4) col3=(5,6,7,8)
    Mat4x2 const b = { { { 1.0, 0.0 }, { 0.0, 1.0 }, { 1.0, 0.0 }, { 0.0, 1.0 } } };
    Mat4 const prod = math_mat2x4_mul_2(a, b);
    _check_f("mul_2 m[0][0]", prod.m[0][0], 1.0, _FTOL);
    _check_f("mul_2 m[0][3]", prod.m[0][3], 4.0, _FTOL);
    _check_f("mul_2 m[1][0]", prod.m[1][0], 5.0, _FTOL);
    _check_f("mul_2 m[1][3]", prod.m[1][3], 8.0, _FTOL);
    _check_f("mul_2 m[2][2]", prod.m[2][2], 3.0, _FTOL);
    _check_f("mul_2 m[3][3]", prod.m[3][3], 8.0, _FTOL);
    // B raw flat (Mat4x2, col*2+row): {1,0, 0,1, 1,0, 0,1}
    FSize pb[8] = { 1.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0, 1.0 };
    FSize pprod[16] = DEFAULT_INITIALIZATION;
    math_mat2x4_mul_1(pa, pb, pprod);
    // Mat4 raw flat (col*4+row): col0=(1,2,3,4) -> [0..3], col3=(5,6,7,8) -> [12..15]
    _check_f("mul_1 [0]", pprod[0], 1.0, _FTOL);
    _check_f("mul_1 [3]", pprod[3], 4.0, _FTOL);
    _check_f("mul_1 [15]", pprod[15], 8.0, _FTOL);

    // keep _check_u / _check_i referenced so the harness helpers are exercised
    _check_u("pass counter positive", _pass > 0 ? 1 : 0, 1);
    _check_i("fail counter type", (ISize) 0, 0);

    return _check_finish();
}