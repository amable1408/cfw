/*
 * test_mat3x2.c - Tests for include/math/mat3x2.c (full glmc_mat3x2_* coverage)
 */

#include <math.h>
#include <stdio.h>

#include <math/mat3x2.h>

#include "check.h"

// === Helpers ===

#define _TOL 1e-9
#define _FTOL 1e-5

int main(void) {
    printf("=== mat3x2 module tests ===\n");

    // Mat3x2 A: 3 columns of 2 rows, column-major m[col][row].
    //   col0=(1,2) col1=(3,4) col2=(5,6)
    //   raw flat (col*2+row): { 1, 2, 3, 4, 5, 6 }
    Mat3x2 const a = { { { 1.0, 2.0 }, { 3.0, 4.0 }, { 5.0, 6.0 } } };
    FSize pa[6] = { 1.0, 2.0, 3.0, 4.0, 5.0, 6.0 };
    FSize po[6] = DEFAULT_INITIALIZATION;

    // --- construction: make / copy / zero ---
    printf("--- construction ---\n");

    Mat3x2 const cp = math_mat3x2_copy_2(a);
    _check_f("copy_2 m[0][0]", cp.m[0][0], 1.0, _TOL);
    _check_f("copy_2 m[2][1]", cp.m[2][1], 6.0, _TOL);
    math_mat3x2_copy_1(pa, po);
    _check_f("copy_1 [5]", po[5], 6.0, _TOL);

    Mat3x2 const mk = math_mat3x2_make_2(pa);
    _check_f("make_2 m[1][1]", mk.m[1][1], 4.0, _TOL);
    math_mat3x2_make_1(pa, po);
    _check_f("make_1 [2]", po[2], 3.0, _TOL);

    Mat3x2 const zr = math_mat3x2_zero_2();
    _check_f("zero_2 m[0][0]", zr.m[0][0], 0.0, _TOL);
    _check_f("zero_2 m[2][1]", zr.m[2][1], 0.0, _TOL);
    math_mat3x2_zero_1(po);
    _check_f("zero_1 [3]", po[3], 0.0, _TOL);

    // --- scale ---
    printf("--- scale ---\n");

    Mat3x2 const sc = math_mat3x2_scale_2(a, 2.0);
    _check_f("scale_2 m[0][0]", sc.m[0][0], 2.0, _FTOL);
    _check_f("scale_2 m[2][1]", sc.m[2][1], 12.0, _FTOL);
    math_mat3x2_scale_1(pa, 3.0, po);
    _check_f("scale_1 [5]", po[5], 18.0, _FTOL);

    // --- transpose: 3x2 -> 2x3 ---
    printf("--- transpose ---\n");

    // T = A^T is Mat2x3 (2 cols, 3 rows). Element [col][row] -> [row][col].
    //   T col0=(1,3,5) col1=(2,4,6)
    Mat2x3 const at = math_mat3x2_transpose_2(a);
    _check_f("transpose_2 m[0][0]", at.m[0][0], 1.0, _FTOL);
    _check_f("transpose_2 m[0][1]", at.m[0][1], 3.0, _FTOL);
    _check_f("transpose_2 m[0][2]", at.m[0][2], 5.0, _FTOL);
    _check_f("transpose_2 m[1][0]", at.m[1][0], 2.0, _FTOL);
    _check_f("transpose_2 m[1][1]", at.m[1][1], 4.0, _FTOL);
    _check_f("transpose_2 m[1][2]", at.m[1][2], 6.0, _FTOL);
    // raw 2x3 flat (col*3+row): { 1, 3, 5, 2, 4, 6 }
    math_mat3x2_transpose_1(pa, po);
    _check_f("transpose_1 [0]", po[0], 1.0, _FTOL);
    _check_f("transpose_1 [1]", po[1], 3.0, _FTOL);
    _check_f("transpose_1 [4]", po[4], 4.0, _FTOL);

    // --- mulv: Mat3x2 * Vec3 -> Vec2 ---
    printf("--- mulv ---\n");

    // result[row] = sum_col A[col][row] * v[col].
    // v=(1,0,0) -> column 0 = (1,2)
    Vec3 const e0 = { 1.0, 0.0, 0.0 };
    Vec2 const av = math_mat3x2_mulv_2(a, e0);
    _check_f("mulv_2 col0.x", av.x, 1.0, _FTOL);
    _check_f("mulv_2 col0.y", av.y, 2.0, _FTOL);
    // v=(0,1,0) -> column 1 = (3,4)
    FSize pe1[3] = { 0.0, 1.0, 0.0 };
    FSize pav[2] = DEFAULT_INITIALIZATION;
    math_mat3x2_mulv_1(pa, pe1, pav);
    _check_f("mulv_1 col1.x", pav[0], 3.0, _FTOL);
    _check_f("mulv_1 col1.y", pav[1], 4.0, _FTOL);

    // --- mul: Mat3x2 * Mat2x3 -> Mat2 ---
    printf("--- mul ---\n");

    // B = A^T (Mat2x3): col0=(1,3,5) col1=(2,4,6).
    // A * B = Mat2: m[0]=(35,44) m[1]=(44,56).
    Mat2 const prod = math_mat3x2_mul_2(a, at);
    _check_f("mul_2 m[0][0]", prod.m[0][0], 35.0, _FTOL);
    _check_f("mul_2 m[0][1]", prod.m[0][1], 44.0, _FTOL);
    _check_f("mul_2 m[1][0]", prod.m[1][0], 44.0, _FTOL);
    _check_f("mul_2 m[1][1]", prod.m[1][1], 56.0, _FTOL);
    // raw: m1={1,2,3,4,5,6}, m2 (A^T flat)={1,3,5,2,4,6}, dest 2x2 flat={35,44,44,56}
    FSize pb[6] = { 1.0, 3.0, 5.0, 2.0, 4.0, 6.0 };
    FSize pprod[4] = DEFAULT_INITIALIZATION;
    math_mat3x2_mul_1(pa, pb, pprod);
    _check_f("mul_1 [0]", pprod[0], 35.0, _FTOL);
    _check_f("mul_1 [1]", pprod[1], 44.0, _FTOL);
    _check_f("mul_1 [3]", pprod[3], 56.0, _FTOL);

    // keep _check_u / _check_i referenced so the harness helpers are exercised
    _check_u("pass counter positive", _pass > 0 ? 1 : 0, 1);
    _check_i("fail counter type", (ISize) 0, 0);

    return _check_finish();
}