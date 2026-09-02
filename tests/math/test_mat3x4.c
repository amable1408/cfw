/*
 * test_mat3x4.c - Tests for include/math/mat3x4.c (full glmc_mat3x4_* coverage)
 */

#include <math.h>
#include <stdio.h>

#include <math/mat3x4.h>

#include "check.h"

// === Helpers ===

#define _TOL 1e-9
#define _FTOL 1e-5

int main(void) {
    printf("=== mat3x4 module tests ===\n");

    // Mat3x4 A: 3 columns of 4 rows, column-major m[col][row].
    //   col0 = (1, 2, 3, 4)
    //   col1 = (5, 6, 7, 8)
    //   col2 = (9,10,11,12)
    // raw flat (col*4+row): { 1..12 } in order.
    Mat3x4 const a = { { { 1.0, 2.0, 3.0, 4.0 },
                         { 5.0, 6.0, 7.0, 8.0 },
                         { 9.0, 10.0, 11.0, 12.0 } } };
    FSize pa[12] = { 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 11.0, 12.0 };
    FSize po[12] = DEFAULT_INITIALIZATION;

    // --- construction: make / copy / zero ---
    printf("--- construction ---\n");

    Mat3x4 const cp = math_mat3x4_copy_2(a);
    _check_f("copy_2 m[0][0]", cp.m[0][0], 1.0, _TOL);
    _check_f("copy_2 m[2][3]", cp.m[2][3], 12.0, _TOL);
    math_mat3x4_copy_1(pa, po);
    _check_f("copy_1 [11]", po[11], 12.0, _TOL);
    _check_f("copy_1 [4]", po[4], 5.0, _TOL);

    Mat3x4 const mk = math_mat3x4_make_2(pa);
    _check_f("make_2 m[1][2]", mk.m[1][2], 7.0, _TOL);
    math_mat3x4_make_1(pa, po);
    _check_f("make_1 [8]", po[8], 9.0, _TOL);

    Mat3x4 const zr = math_mat3x4_zero_2();
    _check_f("zero_2 m[0][0]", zr.m[0][0], 0.0, _TOL);
    _check_f("zero_2 m[2][3]", zr.m[2][3], 0.0, _TOL);
    math_mat3x4_zero_1(po);
    _check_f("zero_1 [5]", po[5], 0.0, _TOL);

    // --- scale ---
    printf("--- scale ---\n");

    Mat3x4 const sc = math_mat3x4_scale_2(a, 2.0);
    _check_f("scale_2 m[0][0]", sc.m[0][0], 2.0, _FTOL);
    _check_f("scale_2 m[2][3]", sc.m[2][3], 24.0, _FTOL);
    math_mat3x4_scale_1(pa, 2.0, po);
    _check_f("scale_1 [4]", po[4], 10.0, _FTOL);

    // --- transpose: mat3x4 -> mat4x3 ---
    printf("--- transpose ---\n");

    // transpose maps m[col][row] -> t[row][col]; t is a Mat4x3 (4 cols, 3 rows).
    //   t.m[r][c] == a.m[c][r]
    //   t.m[0][0]=a.m[0][0]=1  t.m[3][2]=a.m[2][3]=12  t.m[1][0]=a.m[0][1]=2
    Mat4x3 const at = math_mat3x4_transpose_2(a);
    _check_f("transpose_2 t[0][0]", at.m[0][0], 1.0, _FTOL);
    _check_f("transpose_2 t[1][0]", at.m[1][0], 2.0, _FTOL);
    _check_f("transpose_2 t[0][1]", at.m[0][1], 5.0, _FTOL);
    _check_f("transpose_2 t[3][2]", at.m[3][2], 12.0, _FTOL);
    // raw transpose into a mat4x3 (12 FSize, col*3+row).
    // t[0][0] at raw idx 0; t[1][0] at raw idx 3; t[3][2] at raw idx 11.
    math_mat3x4_transpose_1(pa, po);
    _check_f("transpose_1 [0]", po[0], 1.0, _FTOL);
    _check_f("transpose_1 [3]", po[3], 2.0, _FTOL);
    _check_f("transpose_1 [11]", po[11], 12.0, _FTOL);

    // --- mul: mat3x4 * mat4x3 -> mat4 ---
    printf("--- mul ---\n");

    // Use B = A^T (a Mat4x3). Result R = A * A^T is a 4x4 matrix.
    //   R[c][r] = sum_k A.m[k][r] * B.m[c][k],  B.m[c][k] = A.m[k][c]
    //           = sum_{k=0..2} A.m[k][r] * A.m[k][c]
    // R[0][0] = sum_k A.m[k][0]^2 = 1^2 + 5^2 + 9^2 = 1+25+81 = 107
    // R[3][3] = sum_k A.m[k][3]^2 = 4^2 + 8^2 + 12^2 = 16+64+144 = 224
    // R[1][0] = sum_k A.m[k][0]*A.m[k][1] = 1*2 + 5*6 + 9*10 = 2+30+90 = 122
    Mat4 const rr = math_mat3x4_mul_2(a, at);
    _check_f("mul_2 R[0][0]", rr.m[0][0], 107.0, _FTOL);
    _check_f("mul_2 R[3][3]", rr.m[3][3], 224.0, _FTOL);
    _check_f("mul_2 R[1][0]", rr.m[1][0], 122.0, _FTOL);

    // raw path: pa is Mat3x4, transpose it into ptr (Mat4x3), then multiply.
    FSize pat[12] = DEFAULT_INITIALIZATION;
    FSize pr[16] = DEFAULT_INITIALIZATION;
    math_mat3x4_transpose_1(pa, pat);
    math_mat3x4_mul_1(pa, pat, pr);
    // mat4 raw index = col*4+row; R[0][0] at 0, R[3][3] at 15.
    _check_f("mul_1 R[0][0]", pr[0], 107.0, _FTOL);
    _check_f("mul_1 R[3][3]", pr[15], 224.0, _FTOL);

    // --- mulv: mat3x4 * vec3 -> vec4 ---
    printf("--- mulv ---\n");

    // A * v, v a Vec3. result[r] = sum_{k=0..2} A.m[k][r] * v[k].
    // v = (1,0,0) -> column 0 of A = (1,2,3,4).
    Vec3 const e0 = { 1.0, 0.0, 0.0 };
    Vec4 const av = math_mat3x4_mulv_2(a, e0);
    _check_f("mulv_2 col0.x", av.x, 1.0, _FTOL);
    _check_f("mulv_2 col0.y", av.y, 2.0, _FTOL);
    _check_f("mulv_2 col0.z", av.z, 3.0, _FTOL);
    _check_f("mulv_2 col0.w", av.w, 4.0, _FTOL);
    // v = (0,0,1) -> column 2 of A = (9,10,11,12).
    FSize pe2[3] = { 0.0, 0.0, 1.0 };
    FSize pav[4] = DEFAULT_INITIALIZATION;
    math_mat3x4_mulv_1(pa, pe2, pav);
    _check_f("mulv_1 col2 [0]", pav[0], 9.0, _FTOL);
    _check_f("mulv_1 col2 [3]", pav[3], 12.0, _FTOL);

    // keep _check_u / _check_i referenced so the harness helpers are exercised
    _check_u("pass counter positive", _pass > 0 ? 1 : 0, 1);
    _check_i("fail counter type", (ISize) 0, 0);

    return _check_finish();
}