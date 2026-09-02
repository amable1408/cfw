/*
 * test_mat2x3.c - Tests for include/math/mat2x3.c (full glmc_mat2x3_* coverage)
 */

#include <math.h>
#include <stdio.h>

#include <math/mat2x3.h>

#include "check.h"

// === Helpers ===

#define _TOL 1e-9
#define _FTOL 1e-5

int main(void) {
    printf("=== mat2x3 module tests ===\n");

    // Matrix A is a Mat2x3: 2 columns of 3 rows, column-major (index = col*3 + row).
    //   column 0 = (1,2,3), column 1 = (4,5,6)
    //   m[col][row]: m[0][0]=1 m[0][1]=2 m[0][2]=3  m[1][0]=4 m[1][1]=5 m[1][2]=6
    //   raw flat: { 1, 2, 3, 4, 5, 6 }
    // As a linear map R^2 -> R^3 its columns are the images of the basis vectors.
    Mat2x3 const a = { { { 1.0, 2.0, 3.0 }, { 4.0, 5.0, 6.0 } } };
    FSize pa[6] = { 1.0, 2.0, 3.0, 4.0, 5.0, 6.0 };
    FSize po[6] = DEFAULT_INITIALIZATION;

    // --- construction: make / copy / zero ---
    printf("--- construction ---\n");

    Mat2x3 const cp = math_mat2x3_copy_2(a);
    _check_f("copy_2 m[0][0]", cp.m[0][0], 1.0, _TOL);
    _check_f("copy_2 m[0][2]", cp.m[0][2], 3.0, _TOL);
    _check_f("copy_2 m[1][0]", cp.m[1][0], 4.0, _TOL);
    _check_f("copy_2 m[1][2]", cp.m[1][2], 6.0, _TOL);
    math_mat2x3_copy_1(pa, po);
    _check_f("copy_1 [5]", po[5], 6.0, _TOL);
    _check_f("copy_1 [2]", po[2], 3.0, _TOL);

    Mat2x3 const mk = math_mat2x3_make_2(pa);
    _check_f("make_2 m[1][1]", mk.m[1][1], 5.0, _TOL);
    math_mat2x3_make_1(pa, po);
    _check_f("make_1 [3]", po[3], 4.0, _TOL);

    Mat2x3 const zr = math_mat2x3_zero_2();
    _check_f("zero_2 m[0][0]", zr.m[0][0], 0.0, _TOL);
    _check_f("zero_2 m[1][2]", zr.m[1][2], 0.0, _TOL);
    math_mat2x3_zero_1(po);
    _check_f("zero_1 [4]", po[4], 0.0, _TOL);

    // --- scale ---
    printf("--- scale ---\n");

    Mat2x3 const sc = math_mat2x3_scale_2(a, 3.0);
    _check_f("scale_2 m[0][0]", sc.m[0][0], 3.0, _FTOL);
    _check_f("scale_2 m[1][2]", sc.m[1][2], 18.0, _FTOL);
    math_mat2x3_scale_1(pa, 2.0, po);
    _check_f("scale_1 [0]", po[0], 2.0, _FTOL);
    _check_f("scale_1 [5]", po[5], 12.0, _FTOL);

    // --- mulv: A * v yields a 3D vector (dimension 2 -> 3) ---
    printf("--- mulv ---\n");

    // v = (1,0) -> column 0 of A = (1,2,3)
    Vec2 const e0 = { 1.0, 0.0 };
    Vec3 const av0 = math_mat2x3_mulv_2(a, e0);
    _check_f("mulv_2 e0.x", av0.x, 1.0, _FTOL);
    _check_f("mulv_2 e0.y", av0.y, 2.0, _FTOL);
    _check_f("mulv_2 e0.z", av0.z, 3.0, _FTOL);
    // v = (0,1) -> column 1 of A = (4,5,6)
    FSize pe1[2] = { 0.0, 1.0 };
    FSize pav[3] = DEFAULT_INITIALIZATION;
    math_mat2x3_mulv_1(pa, pe1, pav);
    _check_f("mulv_1 e1[0]", pav[0], 4.0, _FTOL);
    _check_f("mulv_1 e1[1]", pav[1], 5.0, _FTOL);
    _check_f("mulv_1 e1[2]", pav[2], 6.0, _FTOL);
    // general v = (2,1) -> 2*col0 + 1*col1 = (2+4, 4+5, 6+6) = (6,9,12)
    Vec2 const vg = { 2.0, 1.0 };
    Vec3 const avg = math_mat2x3_mulv_2(a, vg);
    _check_f("mulv_2 (2,1).x", avg.x, 6.0, _FTOL);
    _check_f("mulv_2 (2,1).z", avg.z, 12.0, _FTOL);

    // --- transpose: 2x3 -> 3x2 (dimension check + values) ---
    printf("--- transpose ---\n");

    // At (Mat3x2) columns are the rows of A:
    //   col0 = (1,4), col1 = (2,5), col2 = (3,6)
    Mat3x2 const at = math_mat2x3_transpose_2(a);
    _check_f("transpose_2 m[0][0]", at.m[0][0], 1.0, _FTOL);
    _check_f("transpose_2 m[0][1]", at.m[0][1], 4.0, _FTOL);
    _check_f("transpose_2 m[1][0]", at.m[1][0], 2.0, _FTOL);
    _check_f("transpose_2 m[1][1]", at.m[1][1], 5.0, _FTOL);
    _check_f("transpose_2 m[2][0]", at.m[2][0], 3.0, _FTOL);
    _check_f("transpose_2 m[2][1]", at.m[2][1], 6.0, _FTOL);

    // raw transpose writes 6 FSize as a mat3x2 (col*2 + row):
    //   { 1,4, 2,5, 3,6 }
    FSize pat[6] = DEFAULT_INITIALIZATION;
    math_mat2x3_transpose_1(pa, pat);
    _check_f("transpose_1 [0]", pat[0], 1.0, _FTOL);
    _check_f("transpose_1 [1]", pat[1], 4.0, _FTOL);
    _check_f("transpose_1 [2]", pat[2], 2.0, _FTOL);
    _check_f("transpose_1 [3]", pat[3], 5.0, _FTOL);
    _check_f("transpose_1 [4]", pat[4], 3.0, _FTOL);
    _check_f("transpose_1 [5]", pat[5], 6.0, _FTOL);

    // round-trip via mulv: (A^T applied to e_i picks a row of A) is exercised
    // above; here confirm A^T's diagonal-ish entries relate to A's columns.
    _check_f("transpose diag m[0][0]==A col0 row0", at.m[0][0], a.m[0][0], _FTOL);
    _check_f("transpose diag m[2][1]==A col1 row2", at.m[2][1], a.m[1][2], _FTOL);

    // --- mul: A(2x3) * B(3x2) yields a 3x3 matrix ---
    printf("--- mul ---\n");

    // Use B = A^T (Mat3x2). A * A^T is the known symmetric 3x3:
    //   [[17,22,27],[22,29,36],[27,36,45]]  (math row,col)
    // Stored column-major in Mat3: res.m[col][row] = math entry(row,col).
    Mat3 const p = math_mat2x3_mul_2(a, at);
    _check_f("mul_2 m[0][0]", p.m[0][0], 17.0, _FTOL);
    _check_f("mul_2 m[0][1]", p.m[0][1], 22.0, _FTOL);
    _check_f("mul_2 m[0][2]", p.m[0][2], 27.0, _FTOL);
    _check_f("mul_2 m[1][1]", p.m[1][1], 29.0, _FTOL);
    _check_f("mul_2 m[1][2]", p.m[1][2], 36.0, _FTOL);
    _check_f("mul_2 m[2][2]", p.m[2][2], 45.0, _FTOL);

    // raw mul into a 9-FSize mat3 dest (col*3 + row). m[0][1]=22 -> index 1.
    FSize pat2[6] = { 1.0, 4.0, 2.0, 5.0, 3.0, 6.0 }; // A^T raw (mat3x2)
    FSize pmul[9] = DEFAULT_INITIALIZATION;
    math_mat2x3_mul_1(pa, pat2, pmul);
    _check_f("mul_1 [0] (m00)", pmul[0], 17.0, _FTOL);
    _check_f("mul_1 [1] (m01)", pmul[1], 22.0, _FTOL);
    _check_f("mul_1 [8] (m22)", pmul[8], 45.0, _FTOL);

    // --- mul against a second known matrix: A * I3x2-ish selector ---
    // B selects columns: B (3x2) with columns e-vectors of R^3 truncated;
    // use B = identity-like { col0=(1,0,0), col1=(0,1,0) } (mat3x2, m[col][row]).
    Mat3x2 const bsel = { { { 1.0, 0.0 }, { 0.0, 1.0 }, { 0.0, 0.0 } } };
    // A(2x3) * B(3x2): math A is 3x2 rows=(1,4),(2,5),(3,6); B math 2x3
    // rows=(1,0,0),(0,1,0). Product 3x3 = for row i: (A[i][0], A[i][1], 0).
    //   row0=(1,4,0) row1=(2,5,0) row2=(3,6,0)
    Mat3 const psel = math_mat2x3_mul_2(a, bsel);
    _check_f("mul_2 sel m[0][0]", psel.m[0][0], 1.0, _FTOL); // math(0,0)=1
    _check_f("mul_2 sel m[1][0]", psel.m[1][0], 4.0, _FTOL); // math(0,1)=4
    _check_f("mul_2 sel m[2][2]", psel.m[2][2], 0.0, _FTOL); // math(2,2)=0
    _check_f("mul_2 sel m[0][2]", psel.m[0][2], 3.0, _FTOL); // math(2,0)=3

    // keep _check_u / _check_i referenced so the harness helpers are exercised
    _check_u("pass counter positive", _pass > 0 ? 1 : 0, 1);
    _check_i("fail counter type", (ISize) 0, 0);

    return _check_finish();
}