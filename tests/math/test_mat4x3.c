/*
 * test_mat4x3.c - Tests for include/math/mat4x3.c (full glmc_mat4x3_* coverage)
 */

#include <math.h>
#include <stdio.h>

#include <math/mat4x3.h>

#include "check.h"

// === Helpers ===

#define _TOL 1e-9
#define _FTOL 1e-5

int main(void) {
    printf("=== mat4x3 module tests ===\n");

    // Mat4x3 A: 4 columns of 3 rows, column-major. A.m[col][row] = col*3 + row + 1.
    //   raw flat (col*3+row): { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 }
    Mat4x3 const a = { { { 1.0, 2.0, 3.0 }, { 4.0, 5.0, 6.0 },
                         { 7.0, 8.0, 9.0 }, { 10.0, 11.0, 12.0 } } };
    FSize pa[12] = { 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 11.0, 12.0 };
    FSize po[12] = DEFAULT_INITIALIZATION;

    // --- construction: make / copy / zero ---
    printf("--- construction ---\n");

    Mat4x3 const cp = math_mat4x3_copy_2(a);
    _check_f("copy_2 m[0][0]", cp.m[0][0], 1.0, _TOL);
    _check_f("copy_2 m[3][2]", cp.m[3][2], 12.0, _TOL);
    math_mat4x3_copy_1(pa, po);
    _check_f("copy_1 [11]", po[11], 12.0, _TOL);
    _check_f("copy_1 [4]", po[4], 5.0, _TOL);

    Mat4x3 const mk = math_mat4x3_make_2(pa);
    _check_f("make_2 m[2][0]", mk.m[2][0], 7.0, _TOL);
    math_mat4x3_make_1(pa, po);
    _check_f("make_1 [7]", po[7], 8.0, _TOL);

    Mat4x3 const zr = math_mat4x3_zero_2();
    _check_f("zero_2 m[0][0]", zr.m[0][0], 0.0, _TOL);
    _check_f("zero_2 m[3][2]", zr.m[3][2], 0.0, _TOL);
    math_mat4x3_zero_1(po);
    _check_f("zero_1 [5]", po[5], 0.0, _TOL);

    // --- scale ---
    printf("--- scale ---\n");

    Mat4x3 const sc = math_mat4x3_scale_2(a, 3.0);
    _check_f("scale_2 m[0][0]", sc.m[0][0], 3.0, _FTOL);
    _check_f("scale_2 m[3][2]", sc.m[3][2], 36.0, _FTOL);
    math_mat4x3_scale_1(pa, 2.0, po);
    _check_f("scale_1 [11]", po[11], 24.0, _FTOL);
    _check_f("scale_1 [0]", po[0], 2.0, _FTOL);

    // --- transpose: mat4x3 -> mat3x4 ---
    printf("--- transpose ---\n");

    // t = transpose(A) is a Mat3x4 (3 cols, 4 rows) with t.m[j][i] = A.m[i][j].
    // t.m[0][3] = A.m[3][0] = 10 ; t.m[2][0] = A.m[0][2] = 3 ; t.m[1][2] = A.m[2][1] = 8
    Mat3x4 const t = math_mat4x3_transpose_2(a);
    _check_f("transpose_2 m[0][3] (A[3][0])", t.m[0][3], 10.0, _FTOL);
    _check_f("transpose_2 m[2][0] (A[0][2])", t.m[2][0], 3.0, _FTOL);
    _check_f("transpose_2 m[1][2] (A[2][1])", t.m[1][2], 8.0, _FTOL);
    _check_f("transpose_2 m[0][0] (A[0][0])", t.m[0][0], 1.0, _FTOL);

    // raw transpose: Mat3x4 raw index = col*4 + row.
    // dest[0*4+3] = A[3][0] = 10 ; dest[2*4+0] = A[0][2] = 3
    math_mat4x3_transpose_1(pa, po);
    _check_f("transpose_1 [3] (A[3][0])", po[3], 10.0, _FTOL);
    _check_f("transpose_1 [8] (A[0][2])", po[8], 3.0, _FTOL);

    // --- mulv: mat4x3 * vec4 -> vec3 ---
    printf("--- mulv ---\n");

    // With v = (1,0,0,0), A*v = column 0 of A = (1,2,3).
    Vec4 const e0 = { 1.0, 0.0, 0.0, 0.0 };
    Vec3 const av = math_mat4x3_mulv_2(a, e0);
    _check_f("mulv_2 col0.x", av.x, 1.0, _FTOL);
    _check_f("mulv_2 col0.y", av.y, 2.0, _FTOL);
    _check_f("mulv_2 col0.z", av.z, 3.0, _FTOL);

    // raw: v = (0,0,0,1) -> column 3 of A = (10,11,12)
    FSize pv[4] = { 0.0, 0.0, 0.0, 1.0 };
    FSize pmv[3] = DEFAULT_INITIALIZATION;
    math_mat4x3_mulv_1(pa, pv, pmv);
    _check_f("mulv_1 col3.x", pmv[0], 10.0, _FTOL);
    _check_f("mulv_1 col3.z", pmv[2], 12.0, _FTOL);

    // --- mul: mat4x3 * mat3x4 -> mat3 ---
    printf("--- mul ---\n");

    // cglm: dest[j][i] = sum_{k=0..3} A[k][i] * B[j][k], i,j in 0..2.
    // Choose B (Mat3x4) as the selector B.m[j][k] = (k==j ? 1 : 0), which picks
    // the top-left 3x3 block of A: dest[j][i] = A[j][i].
    Mat3x4 const b = { { { 1.0, 0.0, 0.0, 0.0 },
                         { 0.0, 1.0, 0.0, 0.0 },
                         { 0.0, 0.0, 1.0, 0.0 } } };
    Mat3 const prod = math_mat4x3_mul_2(a, b);
    _check_f("mul_2 m[0][0] (A[0][0])", prod.m[0][0], 1.0, _FTOL);
    _check_f("mul_2 m[0][1] (A[0][1])", prod.m[0][1], 2.0, _FTOL);
    _check_f("mul_2 m[2][2] (A[2][2])", prod.m[2][2], 9.0, _FTOL);
    _check_f("mul_2 m[2][0] (A[2][0])", prod.m[2][0], 7.0, _FTOL);

    // raw mul: same selector B; Mat3x4 raw index = col*4+row, Mat3 raw = col*3+row.
    // B raw: B[0][0]=1 -> [0]; B[1][1]=1 -> [5]; B[2][2]=1 -> [10]
    FSize pb[12] = { 1.0, 0.0, 0.0, 0.0,
                     0.0, 1.0, 0.0, 0.0,
                     0.0, 0.0, 1.0, 0.0 };
    FSize pmul[9] = DEFAULT_INITIALIZATION;
    math_mat4x3_mul_1(pa, pb, pmul);
    // dest[j][i] = A[j][i]; raw dest index = j*3+i.  dest[0][0]=A[0][0]=1 -> [0]
    _check_f("mul_1 [0] (A[0][0])", pmul[0], 1.0, _FTOL);
    _check_f("mul_1 [8] (A[2][2])", pmul[8], 9.0, _FTOL);
    _check_f("mul_1 [6] (A[2][0])", pmul[6], 7.0, _FTOL);

    // keep _check_u / _check_i referenced so the harness helpers are exercised
    _check_u("pass counter positive", _pass > 0 ? 1 : 0, 1);
    _check_i("fail counter type", (ISize) 0, 0);

    return _check_finish();
}