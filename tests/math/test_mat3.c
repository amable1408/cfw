/*
 * test_mat3.c - Tests for include/math/mat3.c (full glmc_mat3_* coverage)
 */

#include <math.h>
#include <stdio.h>

#include <math/mat3.h>

#include "check.h"

// === Helpers ===

#define _TOL 1e-9
#define _FTOL 1e-5

int main(void) {
    printf("=== mat3 module tests ===\n");

    // Column-major A: index = col * 3 + row.
    //   col0 = (2,0,1), col1 = (0,3,0), col2 = (1,0,2)
    // As rows: row0 = (2,0,1), row1 = (0,3,0), row2 = (1,0,2).
    // det = 9, trace = 7.
    FSize pa[9] = { 2.0, 0.0, 1.0,  0.0, 3.0, 0.0,  1.0, 0.0, 2.0 };
    FSize pb[9] = { 1.0, 0.0, 0.0,  0.0, 1.0, 0.0,  0.0, 0.0, 1.0 }; // identity
    FSize po[9] = DEFAULT_INITIALIZATION;

    Mat3 const a = math_mat3_make_2(pa);
    Mat3 const b = math_mat3_make_2(pb);

    // --- construction: make / identity / zero / copy ---
    printf("--- construction ---\n");

    _check_f("make_2 col0row0", a.m[0][0], 2.0, _TOL);
    _check_f("make_2 col1row1", a.m[1][1], 3.0, _TOL);
    _check_f("make_2 col2row0", a.m[2][0], 1.0, _TOL);
    math_mat3_make_1(pa, po);
    _check_f("make_1 [4]", po[4], 3.0, _TOL);

    // identity boundary: diagonal ones, off-diagonal zeros
    Mat3 const id = math_mat3_identity_2();
    _check_f("identity_2 diag00", id.m[0][0], 1.0, _TOL);
    _check_f("identity_2 diag11", id.m[1][1], 1.0, _TOL);
    _check_f("identity_2 diag22", id.m[2][2], 1.0, _TOL);
    _check_f("identity_2 off01", id.m[0][1], 0.0, _TOL);
    math_mat3_identity_1(po);
    _check_f("identity_1 [0]", po[0], 1.0, _TOL);
    _check_f("identity_1 [1]", po[1], 0.0, _TOL);

    // zero boundary: every element zero
    Mat3 const zr = math_mat3_zero_2();
    _check_f("zero_2 [0][0]", zr.m[0][0], 0.0, _TOL);
    _check_f("zero_2 [2][2]", zr.m[2][2], 0.0, _TOL);
    math_mat3_zero_1(po);
    _check_f("zero_1 [8]", po[8], 0.0, _TOL);

    Mat3 const cp = math_mat3_copy_2(a);
    _check_f("copy_2 col1row1", cp.m[1][1], 3.0, _TOL);
    math_mat3_copy_1(pa, po);
    _check_f("copy_1 [0]", po[0], 2.0, _TOL);

    // --- scalars: det / trace ---
    printf("--- scalars ---\n");

    _check_f("det_2", math_mat3_det_2(a), 9.0, _FTOL);
    _check_f("det_1", math_mat3_det_1(pa), 9.0, _FTOL);
    _check_f("det_2 identity", math_mat3_det_2(id), 1.0, _FTOL);
    _check_f("trace_2", math_mat3_trace_2(a), 7.0, _FTOL);
    _check_f("trace_1", math_mat3_trace_1(pa), 7.0, _FTOL);

    // --- multiply ---
    printf("--- multiply ---\n");

    // A * I == A
    Mat3 const ai = math_mat3_mul_2(a, b);
    _check_f("mul_2 A*I col0row0", ai.m[0][0], 2.0, _FTOL);
    _check_f("mul_2 A*I col1row1", ai.m[1][1], 3.0, _FTOL);
    _check_f("mul_2 A*I col2row2", ai.m[2][2], 2.0, _FTOL);
    math_mat3_mul_1(pa, pb, po);
    _check_f("mul_1 A*I [4]", po[4], 3.0, _FTOL);

    // --- inverse boundary: inv(A) * A ~= I ---
    printf("--- inverse ---\n");

    Mat3 const inv = math_mat3_inv_2(a);
    Mat3 const prod = math_mat3_mul_2(inv, a);
    _check_f("inv.A~=I col0row0", prod.m[0][0], 1.0, _FTOL);
    _check_f("inv.A~=I col1row1", prod.m[1][1], 1.0, _FTOL);
    _check_f("inv.A~=I col2row2", prod.m[2][2], 1.0, _FTOL);
    _check_f("inv.A~=I off10", prod.m[1][0], 0.0, _FTOL);
    _check_f("inv.A~=I off02", prod.m[0][2], 0.0, _FTOL);

    // _1 variant: inverse then multiply, round-trips to identity
    FSize pinv[9] = DEFAULT_INITIALIZATION;
    FSize pprod[9] = DEFAULT_INITIALIZATION;
    math_mat3_inv_1(pa, pinv);
    math_mat3_mul_1(pinv, pa, pprod);
    _check_f("inv_1.A~=I [0]", pprod[0], 1.0, _FTOL);
    _check_f("inv_1.A~=I [4]", pprod[4], 1.0, _FTOL);
    _check_f("inv_1.A~=I [1]", pprod[1], 0.0, _FTOL);

    // --- transpose boundary: transpose(transpose(A)) == A ---
    printf("--- transpose ---\n");

    Mat3 const at = math_mat3_transpose_2(a);
    // A row0=(2,0,1) becomes column0 of A^T: A^T[0][*] = (2,0,1)
    _check_f("transpose_2 [0][2]", at.m[0][2], 1.0, _FTOL);
    _check_f("transpose_2 [2][0]", at.m[2][0], 1.0, _FTOL);
    Mat3 const att = math_mat3_transpose_2(at);
    _check_f("transpose^2 col0row0", att.m[0][0], 2.0, _FTOL);
    _check_f("transpose^2 col2row0", att.m[2][0], 1.0, _FTOL);
    _check_f("transpose^2 col1row1", att.m[1][1], 3.0, _FTOL);
    math_mat3_transpose_1(pa, po);
    // A^T flat is column-major (col*3+row); A^T[c][r] = A[r][c].
    // flat[2] = A^T col0 row2 = A[2][0] = pa[2] = 1.0
    _check_f("transpose_1 [2]", po[2], 1.0, _FTOL);
    // flat[6] = A^T col2 row0 = A[0][2] = pa[6] = 1.0
    _check_f("transpose_1 [6]", po[6], 1.0, _FTOL);

    // --- scale (pure over source) ---
    printf("--- scale ---\n");

    Mat3 const sc = math_mat3_scale_2(a, 2.0);
    _check_f("scale_2 col0row0", sc.m[0][0], 4.0, _FTOL);
    _check_f("scale_2 col1row1", sc.m[1][1], 6.0, _FTOL);
    math_mat3_scale_1(pa, 2.0, po);
    _check_f("scale_1 [8]", po[8], 4.0, _FTOL);

    // --- matrix-vector product (cross-type) ---
    printf("--- mulv ---\n");

    Vec3 const v = { 1.0, 1.0, 1.0 };
    FSize pv[3] = { 1.0, 1.0, 1.0 };
    FSize pmv[3] = DEFAULT_INITIALIZATION;

    // A * (1,1,1): result[row] = sum_col A[col][row]. -> (3,3,3)
    Vec3 const mv = math_mat3_mulv_2(a, v);
    _check_f("mulv_2.x", mv.x, 3.0, _FTOL);
    _check_f("mulv_2.y", mv.y, 3.0, _FTOL);
    _check_f("mulv_2.z", mv.z, 3.0, _FTOL);
    math_mat3_mulv_1(pa, pv, pmv);
    _check_f("mulv_1.x", pmv[0], 3.0, _FTOL);

    // identity * v == v
    Vec3 const iv = math_mat3_mulv_2(id, v);
    _check_f("mulv_2 I*v.x", iv.x, 1.0, _FTOL);

    // --- rmc (cross-type scalar): r * M * c ---
    printf("--- rmc ---\n");

    // r=(1,1,1), M=A, c=(1,1,1): r . (A*c) = 3+3+3 = 9
    Vec3 const r = { 1.0, 1.0, 1.0 };
    Vec3 const c = { 1.0, 1.0, 1.0 };
    FSize pr[3] = { 1.0, 1.0, 1.0 };
    FSize pc[3] = { 1.0, 1.0, 1.0 };
    _check_f("rmc_2", math_mat3_rmc_2(r, a, c), 9.0, _FTOL);
    _check_f("rmc_1", math_mat3_rmc_1(pr, pa, pc), 9.0, _FTOL);

    // --- swap_col / swap_row (pure over source) ---
    printf("--- swap ---\n");

    // swap columns 0 and 2 of A: new col0 = old col2 = (1,0,2)
    Mat3 const swc = math_mat3_swap_col_2(a, 0, 2);
    _check_f("swap_col_2 new col0row0", swc.m[0][0], 1.0, _FTOL);
    _check_f("swap_col_2 new col2row0", swc.m[2][0], 2.0, _FTOL);
    math_mat3_swap_col_1(pa, 0, 2, po);
    _check_f("swap_col_1 [0]", po[0], 1.0, _FTOL);

    // swap rows 0 and 1 of A: element [col][0] and [col][1] exchange.
    // col1 was (0,3,0) -> after row swap (3,0,0)
    Mat3 const swr = math_mat3_swap_row_2(a, 0, 1);
    _check_f("swap_row_2 col1row0", swr.m[1][0], 3.0, _FTOL);
    _check_f("swap_row_2 col1row1", swr.m[1][1], 0.0, _FTOL);
    math_mat3_swap_row_1(pa, 0, 1, po);
    _check_f("swap_row_1 [3]", po[3], 3.0, _FTOL); // col1 row0

    // --- quat (cross-type): identity matrix -> identity quaternion ---
    printf("--- quat ---\n");

    Quat const q = math_mat3_quat_2(id);
    // identity rotation quaternion is (0,0,0,1)
    _check_f("quat_2 identity.w", q.w, 1.0, _FTOL);
    _check_f("quat_2 identity.x", q.x, 0.0, _FTOL);
    FSize pq[4] = DEFAULT_INITIALIZATION;
    FSize pid[9] = DEFAULT_INITIALIZATION;
    math_mat3_identity_1(pid);
    math_mat3_quat_1(pid, pq);
    _check_f("quat_1 identity.w", pq[3], 1.0, _FTOL);

    // --- textrans: scale+rotate+translate producer ---
    printf("--- textrans ---\n");

    // textrans(sx=2, sy=3, rot=0, tx=5, ty=7):
    // no rotation -> diagonal scale + translation column.
    // cglm mat3 textrans: [0][0]=sx, [1][1]=sy, translation in column 2 rows 0,1.
    Mat3 const tt = math_mat3_textrans_2(2.0, 3.0, 0.0, 5.0, 7.0);
    _check_f("textrans_2 sx", tt.m[0][0], 2.0, _FTOL);
    _check_f("textrans_2 sy", tt.m[1][1], 3.0, _FTOL);
    _check_f("textrans_2 tx", tt.m[2][0], 5.0, _FTOL);
    _check_f("textrans_2 ty", tt.m[2][1], 7.0, _FTOL);
    math_mat3_textrans_1(2.0, 3.0, 0.0, 5.0, 7.0, po);
    _check_f("textrans_1 sx [0]", po[0], 2.0, _FTOL);
    _check_f("textrans_1 tx [6]", po[6], 5.0, _FTOL);

    // keep _check_u / _check_i referenced so the harness helpers are exercised
    _check_u("pass counter positive", _pass > 0 ? 1 : 0, 1);
    _check_i("fail counter type", (ISize) 0, 0);

    return _check_finish();
}