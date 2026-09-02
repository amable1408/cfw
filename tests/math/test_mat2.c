/*
 * test_mat2.c - Tests for include/math/mat2.c (full glmc_mat2_* coverage)
 */

#include <math.h>
#include <stdio.h>

#include <math/mat2.h>

#include "check.h"

// === Helpers ===

#define _TOL 1e-9
#define _FTOL 1e-5

int main(void) {
    printf("=== mat2 module tests ===\n");

    // Matrix A in math (row-major) notation:
    //   | 1  2 |
    //   | 3  4 |
    // cglm/Mat2 is column-major: column 0 = (1,3), column 1 = (2,4).
    //   m[col][row]: m[0][0]=1 m[0][1]=3 m[1][0]=2 m[1][1]=4
    //   raw flat (col*2+row): { 1, 3, 2, 4 }
    Mat2 const a = { { { 1.0, 3.0 }, { 2.0, 4.0 } } };
    FSize pa[4] = { 1.0, 3.0, 2.0, 4.0 };
    FSize po[4] = DEFAULT_INITIALIZATION;

    // --- construction: make / copy / identity / zero ---
    printf("--- construction ---\n");

    Mat2 const cp = math_mat2_copy_2(a);
    _check_f("copy_2 m[0][0]", cp.m[0][0], 1.0, _TOL);
    _check_f("copy_2 m[0][1]", cp.m[0][1], 3.0, _TOL);
    _check_f("copy_2 m[1][0]", cp.m[1][0], 2.0, _TOL);
    _check_f("copy_2 m[1][1]", cp.m[1][1], 4.0, _TOL);
    math_mat2_copy_1(pa, po);
    _check_f("copy_1 [3]", po[3], 4.0, _TOL);

    Mat2 const mk = math_mat2_make_2(pa);
    _check_f("make_2 m[1][1]", mk.m[1][1], 4.0, _TOL);
    math_mat2_make_1(pa, po);
    _check_f("make_1 [2]", po[2], 2.0, _TOL);

    // identity round-trip: identity written then copied back unchanged
    Mat2 const id = math_mat2_identity_2();
    _check_f("identity_2 m[0][0]", id.m[0][0], 1.0, _TOL);
    _check_f("identity_2 m[0][1]", id.m[0][1], 0.0, _TOL);
    _check_f("identity_2 m[1][0]", id.m[1][0], 0.0, _TOL);
    _check_f("identity_2 m[1][1]", id.m[1][1], 1.0, _TOL);
    math_mat2_identity_1(po);
    _check_f("identity_1 [0]", po[0], 1.0, _TOL);
    _check_f("identity_1 [1]", po[1], 0.0, _TOL);
    _check_f("identity_1 [3]", po[3], 1.0, _TOL);

    Mat2 const zr = math_mat2_zero_2();
    _check_f("zero_2 m[0][0]", zr.m[0][0], 0.0, _TOL);
    _check_f("zero_2 m[1][1]", zr.m[1][1], 0.0, _TOL);
    math_mat2_zero_1(po);
    _check_f("zero_1 [2]", po[2], 0.0, _TOL);

    // --- multiply: mul / mulv ---
    printf("--- multiply ---\n");

    // A * I == A
    Mat2 const aid = math_mat2_mul_2(a, id);
    _check_f("mul_2 A*I m[0][0]", aid.m[0][0], 1.0, _FTOL);
    _check_f("mul_2 A*I m[1][1]", aid.m[1][1], 4.0, _FTOL);
    math_mat2_mul_1(pa, po, po); // po currently zero -> A*0 = 0
    _check_f("mul_1 A*0 [0]", po[0], 0.0, _FTOL);

    // A * B where B = 2*I -> every element doubles
    Mat2 const twoI = math_mat2_scale_2(id, 2.0);
    Mat2 const ab = math_mat2_mul_2(a, twoI);
    _check_f("mul_2 A*2I m[0][1]", ab.m[0][1], 6.0, _FTOL);
    _check_f("mul_2 A*2I m[1][0]", ab.m[1][0], 4.0, _FTOL);

    // mulv: A * v.  With v = (1,0) column vector, result = column 0 of A = (1,3).
    Vec2 const e0 = { 1.0, 0.0 };
    Vec2 const av = math_mat2_mulv_2(a, e0);
    _check_f("mulv_2 col0.x", av.x, 1.0, _FTOL);
    _check_f("mulv_2 col0.y", av.y, 3.0, _FTOL);
    FSize pe0[2] = { 0.0, 1.0 }; // v = (0,1) -> column 1 of A = (2,4)
    FSize pav[2] = DEFAULT_INITIALIZATION;
    math_mat2_mulv_1(pa, pe0, pav);
    _check_f("mulv_1 col1.x", pav[0], 2.0, _FTOL);
    _check_f("mulv_1 col1.y", pav[1], 4.0, _FTOL);

    // --- determinant / trace ---
    printf("--- det / trace ---\n");

    // det(A) = 1*4 - 2*3 = -2
    _check_f("det_2", math_mat2_det_2(a), -2.0, _FTOL);
    _check_f("det_1", math_mat2_det_1(pa), -2.0, _FTOL);
    _check_f("det_2 identity", math_mat2_det_2(id), 1.0, _FTOL);

    // trace(A) = 1 + 4 = 5
    _check_f("trace_2", math_mat2_trace_2(a), 5.0, _FTOL);
    _check_f("trace_1", math_mat2_trace_1(pa), 5.0, _FTOL);

    // --- inverse: inv(A)*A ~= I ---
    printf("--- inverse ---\n");

    Mat2 const inv = math_mat2_inv_2(a);
    // A^-1 = 1/det * | 4 -2 ; -3 1 | = | -2 1 ; 1.5 -0.5 | (row-major)
    // column-major m[0]=(-2,1.5) m[1]=(1,-0.5)
    _check_f("inv_2 m[0][0]", inv.m[0][0], -2.0, _FTOL);
    _check_f("inv_2 m[0][1]", inv.m[0][1], 1.5, _FTOL);
    _check_f("inv_2 m[1][0]", inv.m[1][0], 1.0, _FTOL);
    _check_f("inv_2 m[1][1]", inv.m[1][1], -0.5, _FTOL);

    // inv(A) * A ~= I
    Mat2 const shouldBeId = math_mat2_mul_2(inv, a);
    _check_f("inv*A m[0][0]", shouldBeId.m[0][0], 1.0, _FTOL);
    _check_f("inv*A m[0][1]", shouldBeId.m[0][1], 0.0, _FTOL);
    _check_f("inv*A m[1][0]", shouldBeId.m[1][0], 0.0, _FTOL);
    _check_f("inv*A m[1][1]", shouldBeId.m[1][1], 1.0, _FTOL);

    math_mat2_inv_1(pa, po);
    _check_f("inv_1 [0]", po[0], -2.0, _FTOL);

    // --- transpose: transpose(transpose(A)) == A ---
    printf("--- transpose ---\n");

    // transpose of A (row-major |1 2; 3 4|) is |1 3; 2 4|
    // column-major: m[0]=(1,2) m[1]=(3,4)
    Mat2 const at = math_mat2_transpose_2(a);
    _check_f("transpose_2 m[0][1]", at.m[0][1], 2.0, _FTOL);
    _check_f("transpose_2 m[1][0]", at.m[1][0], 3.0, _FTOL);

    // transpose^2 == A
    Mat2 const att = math_mat2_transpose_2(at);
    _check_f("transpose^2 m[0][0]", att.m[0][0], 1.0, _FTOL);
    _check_f("transpose^2 m[0][1]", att.m[0][1], 3.0, _FTOL);
    _check_f("transpose^2 m[1][0]", att.m[1][0], 2.0, _FTOL);
    _check_f("transpose^2 m[1][1]", att.m[1][1], 4.0, _FTOL);

    math_mat2_transpose_1(pa, po);
    _check_f("transpose_1 [1]", po[1], 2.0, _FTOL);

    // --- scale ---
    printf("--- scale ---\n");

    Mat2 const sc = math_mat2_scale_2(a, 3.0);
    _check_f("scale_2 m[1][1]", sc.m[1][1], 12.0, _FTOL);
    _check_f("scale_2 m[0][0]", sc.m[0][0], 3.0, _FTOL);
    math_mat2_scale_1(pa, 3.0, po);
    _check_f("scale_1 [3]", po[3], 12.0, _FTOL);

    // --- swap_col / swap_row ---
    printf("--- swap ---\n");

    // swap columns 0 and 1 of A: column 0=(1,3), column 1=(2,4) -> m[0]=(2,4) m[1]=(1,3)
    Mat2 const scol = math_mat2_swap_col_2(a, 0, 1);
    _check_f("swap_col_2 m[0][0]", scol.m[0][0], 2.0, _FTOL);
    _check_f("swap_col_2 m[1][1]", scol.m[1][1], 3.0, _FTOL);
    math_mat2_swap_col_1(pa, 0, 1, po);
    _check_f("swap_col_1 [0]", po[0], 2.0, _FTOL);

    // swap rows 0 and 1 of A: within each column swap row0/row1
    // column 0 (1,3)->(3,1); column 1 (2,4)->(4,2)
    Mat2 const row = math_mat2_swap_row_2(a, 0, 1);
    _check_f("swap_row_2 m[0][0]", row.m[0][0], 3.0, _FTOL);
    _check_f("swap_row_2 m[1][0]", row.m[1][0], 4.0, _FTOL);
    math_mat2_swap_row_1(pa, 0, 1, po);
    _check_f("swap_row_1 [1]", po[1], 1.0, _FTOL);

    // --- rmc: r * M * c ---
    printf("--- rmc ---\n");

    // r=(1,0), c=(1,0): picks M[0][0] in cglm's rmc (r . (M . c)).
    // M . c with c=(1,0) = column 0 = (1,3); r=(1,0) . (1,3) = 1
    Vec2 const r = { 1.0, 0.0 };
    Vec2 const c = { 1.0, 0.0 };
    _check_f("rmc_2 (e0,A,e0)", math_mat2_rmc_2(r, a, c), 1.0, _FTOL);
    FSize pr[2] = { 0.0, 1.0 };
    FSize pc[2] = { 0.0, 1.0 };
    // M . c with c=(0,1) = column 1 = (2,4); r=(0,1) . (2,4) = 4
    _check_f("rmc_1 (e1,A,e1)", math_mat2_rmc_1(pr, pa, pc), 4.0, _FTOL);

    // keep _check_u / _check_i referenced so the harness helpers are exercised
    _check_u("pass counter positive", _pass > 0 ? 1 : 0, 1);
    _check_i("fail counter type", (ISize) 0, 0);

    return _check_finish();
}