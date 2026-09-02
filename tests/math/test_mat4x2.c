/*
 * test_mat4x2.c - Tests for include/math/mat4x2.c (full glmc_mat4x2_* coverage)
 */

#include <math.h>
#include <stdio.h>

#include <math/mat4x2.h>

#include "check.h"

// === Helpers ===

#define _TOL 1e-9
#define _FTOL 1e-5

int main(void) {
    printf("=== mat4x2 module tests ===\n");

    // Mat4x2 has 4 columns of 2 rows. Matrix A in math (row-major) notation:
    //   | 1  3  5  7 |   (row 0)
    //   | 2  4  6  8 |   (row 1)
    // Column-major m[col][row]: col0=(1,2) col1=(3,4) col2=(5,6) col3=(7,8).
    //   raw flat (col*2+row): { 1, 2, 3, 4, 5, 6, 7, 8 }
    Mat4x2 const a = { { { 1.0, 2.0 }, { 3.0, 4.0 }, { 5.0, 6.0 }, { 7.0, 8.0 } } };
    FSize pa[8] = { 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0 };
    FSize po[8] = DEFAULT_INITIALIZATION;

    // --- construction: make / copy / zero ---
    printf("--- construction ---\n");

    Mat4x2 const cp = math_mat4x2_copy_2(a);
    _check_f("copy_2 m[0][0]", cp.m[0][0], 1.0, _TOL);
    _check_f("copy_2 m[0][1]", cp.m[0][1], 2.0, _TOL);
    _check_f("copy_2 m[3][1]", cp.m[3][1], 8.0, _TOL);
    math_mat4x2_copy_1(pa, po);
    _check_f("copy_1 [7]", po[7], 8.0, _TOL);

    Mat4x2 const mk = math_mat4x2_make_2(pa);
    _check_f("make_2 m[2][0]", mk.m[2][0], 5.0, _TOL);
    _check_f("make_2 m[3][1]", mk.m[3][1], 8.0, _TOL);
    math_mat4x2_make_1(pa, po);
    _check_f("make_1 [4]", po[4], 5.0, _TOL);

    Mat4x2 const zr = math_mat4x2_zero_2();
    _check_f("zero_2 m[0][0]", zr.m[0][0], 0.0, _TOL);
    _check_f("zero_2 m[3][1]", zr.m[3][1], 0.0, _TOL);
    math_mat4x2_zero_1(po);
    _check_f("zero_1 [5]", po[5], 0.0, _TOL);

    // --- scale ---
    printf("--- scale ---\n");

    Mat4x2 const sc = math_mat4x2_scale_2(a, 3.0);
    _check_f("scale_2 m[0][0]", sc.m[0][0], 3.0, _FTOL);
    _check_f("scale_2 m[3][1]", sc.m[3][1], 24.0, _FTOL);
    math_mat4x2_scale_1(pa, 3.0, po);
    _check_f("scale_1 [7]", po[7], 24.0, _FTOL);

    // --- transpose: 4x2 -> Mat2x4 ---
    printf("--- transpose ---\n");

    // Aᵀ (math 4 rows × 2 cols):
    //   | 1  2 |
    //   | 3  4 |
    //   | 5  6 |
    //   | 7  8 |
    // Mat2x4 column-major m[col][row], col=0..1 row=0..3:
    //   col0 = (1,3,5,7), col1 = (2,4,6,8)
    Mat2x4 const t = math_mat4x2_transpose_2(a);
    _check_f("transpose_2 m[0][0]", t.m[0][0], 1.0, _FTOL);
    _check_f("transpose_2 m[0][1]", t.m[0][1], 3.0, _FTOL);
    _check_f("transpose_2 m[0][3]", t.m[0][3], 7.0, _FTOL);
    _check_f("transpose_2 m[1][0]", t.m[1][0], 2.0, _FTOL);
    _check_f("transpose_2 m[1][3]", t.m[1][3], 8.0, _FTOL);
    // raw: mat2x4 flat (col*4+row) -> [0]=1 [1]=3 [4]=2 [7]=8
    math_mat4x2_transpose_1(pa, po);
    _check_f("transpose_1 [1]", po[1], 3.0, _FTOL);
    _check_f("transpose_1 [4]", po[4], 2.0, _FTOL);
    _check_f("transpose_1 [7]", po[7], 8.0, _FTOL);

    // --- mulv: Mat4x2 * Vec4 -> Vec2 ---
    printf("--- mulv ---\n");

    // A * v with v = (1,0,0,0) picks column 0 = (1,2).
    Vec4 const e0 = { 1.0, 0.0, 0.0, 0.0 };
    Vec2 const av = math_mat4x2_mulv_2(a, e0);
    _check_f("mulv_2 e0 .x", av.x, 1.0, _FTOL);
    _check_f("mulv_2 e0 .y", av.y, 2.0, _FTOL);
    // v = (0,0,1,0) picks column 2 = (5,6).
    FSize pv[4] = { 0.0, 0.0, 1.0, 0.0 };
    FSize pav[2] = DEFAULT_INITIALIZATION;
    math_mat4x2_mulv_1(pa, pv, pav);
    _check_f("mulv_1 e2 [0]", pav[0], 5.0, _FTOL);
    _check_f("mulv_1 e2 [1]", pav[1], 6.0, _FTOL);

    // --- mul: Mat4x2 (4x2) * Mat2x4 (2x4) -> Mat2 (2x2) ---
    printf("--- mul ---\n");

    // Let m2 = Aᵀ as a Mat2x4 (math 4 rows × 2 cols), so A * Aᵀ = A·Aᵀ (2x2):
    //   | 84  100 |   (row-major)
    //   | 100 120 |
    // Mat2x4 column-major m[col][row]: col0=(1,3,5,7) col1=(2,4,6,8)
    Mat2x4 const b = { { { 1.0, 3.0, 5.0, 7.0 }, { 2.0, 4.0, 6.0, 8.0 } } };
    Mat2 const prod = math_mat4x2_mul_2(a, b);
    // Mat2 column-major m[col][row]: m[0][0]=84 m[0][1]=100 m[1][0]=100 m[1][1]=120
    _check_f("mul_2 m[0][0]", prod.m[0][0], 84.0, _FTOL);
    _check_f("mul_2 m[0][1]", prod.m[0][1], 100.0, _FTOL);
    _check_f("mul_2 m[1][0]", prod.m[1][0], 100.0, _FTOL);
    _check_f("mul_2 m[1][1]", prod.m[1][1], 120.0, _FTOL);
    // raw m2 flat (mat2x4 col*4+row): { 1,3,5,7, 2,4,6,8 }
    FSize pb[8] = { 1.0, 3.0, 5.0, 7.0, 2.0, 4.0, 6.0, 8.0 };
    FSize pprod[4] = DEFAULT_INITIALIZATION;
    math_mat4x2_mul_1(pa, pb, pprod);
    // mat2 flat (col*2+row): [0]=84 [1]=100 [2]=100 [3]=120
    _check_f("mul_1 [0]", pprod[0], 84.0, _FTOL);
    _check_f("mul_1 [3]", pprod[3], 120.0, _FTOL);

    // keep _check_u / _check_i referenced so the harness helpers are exercised
    _check_u("pass counter positive", _pass > 0 ? 1 : 0, 1);
    _check_i("fail counter type", (ISize) 0, 0);

    return _check_finish();
}