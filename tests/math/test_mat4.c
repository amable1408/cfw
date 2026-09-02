/*
 * test_mat4.c - Tests for include/math/mat4.c (full glmc_mat4_* coverage)
 */

#include <math.h>
#include <stdio.h>

#include <math/mat4.h>

#include "check.h"

// === Helpers ===

#define _FTOL 1e-4
#define _FTOL_FAST 1e-3
#define _TOL 1e-9

// Column-major flat index for a 4x4: col * 4 + row.
#define _AT(col, row) ((col) * 4 + (row))

int main(void) {
    printf("=== mat4 module tests ===\n");

    // An invertible, non-symmetric matrix A (column-major flat storage).
    // Columns: c0=(2,0,0,0) c1=(1,3,0,0) c2=(0,1,4,0) c3=(5,6,7,1)
    // Upper-triangular-ish: det = 2*3*4*1 = 24, trace = 2+3+4+1 = 10.
    FSize pa[16] = {
        2.0, 0.0, 0.0, 0.0,
        1.0, 3.0, 0.0, 0.0,
        0.0, 1.0, 4.0, 0.0,
        5.0, 6.0, 7.0, 1.0,
    };

    Mat4 const a = math_mat4_make_2(pa);

    // --- construction: make / copy / identity / zero ---
    printf("--- construction ---\n");

    _check_f("make_2 c1r1", a.m[1][1], 3.0, _FTOL);
    _check_f("make_2 c3r0", a.m[3][0], 5.0, _FTOL);
    FSize pmake[16] = DEFAULT_INITIALIZATION;
    math_mat4_make_1(pa, pmake);
    _check_f("make_1 c2r2", pmake[_AT(2, 2)], 4.0, _FTOL);

    Mat4 const cp = math_mat4_copy_2(a);
    _check_f("copy_2 c3r2", cp.m[3][2], 7.0, _FTOL);
    FSize pcopy[16] = DEFAULT_INITIALIZATION;
    math_mat4_copy_1(pa, pcopy);
    _check_f("copy_1 c0r0", pcopy[_AT(0, 0)], 2.0, _FTOL);

    // identity round-trip: I is diagonal 1s, 0 elsewhere.
    Mat4 const id = math_mat4_identity_2();
    _check_f("identity_2 c0r0", id.m[0][0], 1.0, _TOL);
    _check_f("identity_2 c1r1", id.m[1][1], 1.0, _TOL);
    _check_f("identity_2 c1r0 off", id.m[1][0], 0.0, _TOL);
    FSize pid[16] = DEFAULT_INITIALIZATION;
    math_mat4_identity_1(pid);
    _check_f("identity_1 c2r2", pid[_AT(2, 2)], 1.0, _TOL);
    _check_f("identity_1 c3r3", pid[_AT(3, 3)], 1.0, _TOL);

    Mat4 const zr = math_mat4_zero_2();
    _check_f("zero_2 c0r0", zr.m[0][0], 0.0, _TOL);
    _check_f("zero_2 c3r3", zr.m[3][3], 0.0, _TOL);
    FSize pzero[16] = DEFAULT_INITIALIZATION;
    pzero[0] = 9.0;
    math_mat4_zero_1(pzero);
    _check_f("zero_1 c0r0", pzero[_AT(0, 0)], 0.0, _TOL);

    // --- identity round-trip: A * I == A ---
    printf("--- mul / identity round-trip ---\n");

    Mat4 const a_times_id = math_mat4_mul_2(a, id);
    _check_f("A*I c1r1", a_times_id.m[1][1], 3.0, _FTOL);
    _check_f("A*I c3r0", a_times_id.m[3][0], 5.0, _FTOL);
    _check_f("A*I c2r2", a_times_id.m[2][2], 4.0, _FTOL);
    FSize pmul[16] = DEFAULT_INITIALIZATION;
    math_mat4_mul_1(pa, pid, pmul);
    _check_f("A*I_1 c3r2", pmul[_AT(3, 2)], 7.0, _FTOL);

    // --- inverse: inv(A) * A ~= I (also inv_fast / inv_precise) ---
    printf("--- inverse ---\n");

    Mat4 const inv = math_mat4_inv_2(a);
    Mat4 const inv_a = math_mat4_mul_2(inv, a);
    _check_f("inv*A c0r0", inv_a.m[0][0], 1.0, _FTOL);
    _check_f("inv*A c1r1", inv_a.m[1][1], 1.0, _FTOL);
    _check_f("inv*A c3r3", inv_a.m[3][3], 1.0, _FTOL);
    _check_f("inv*A c3r0 off", inv_a.m[3][0], 0.0, _FTOL);
    _check_f("inv*A c0r1 off", inv_a.m[0][1], 0.0, _FTOL);

    FSize pinv[16] = DEFAULT_INITIALIZATION;
    math_mat4_inv_1(pa, pinv);
    FSize pinva[16] = DEFAULT_INITIALIZATION;
    math_mat4_mul_1(pinv, pa, pinva);
    _check_f("inv_1*A c2r2", pinva[_AT(2, 2)], 1.0, _FTOL);

    Mat4 const invp = math_mat4_inv_precise_2(a);
    Mat4 const invp_a = math_mat4_mul_2(invp, a);
    _check_f("inv_precise*A c0r0", invp_a.m[0][0], 1.0, _FTOL);
    FSize pinvp[16] = DEFAULT_INITIALIZATION;
    math_mat4_inv_precise_1(pa, pinvp);
    _check_f("inv_precise_1 set", pinvp[_AT(0, 0)] != 0.0 ? 1.0 : 0.0, 1.0, _TOL);

    // inv_fast takes the SSE path: cglm divides through _mm_rcp_ps, a 12-bit
    // hardware approximation whose exact result differs between CPU vendors.
    // 1 - 2^-12 came back on one CI host where another host gave exactly 1, so
    // the pin allows what the instruction promises (about 2.4e-4), not _FTOL.
    Mat4 const invf = math_mat4_inv_fast_2(a);
    Mat4 const invf_a = math_mat4_mul_2(invf, a);
    _check_f("inv_fast*A c0r0", invf_a.m[0][0], 1.0, _FTOL_FAST);
    _check_f("inv_fast*A c1r1", invf_a.m[1][1], 1.0, _FTOL_FAST);
    FSize pinvf[16] = DEFAULT_INITIALIZATION;
    math_mat4_inv_fast_1(pa, pinvf);
    _check_f("inv_fast_1 set", pinvf[_AT(2, 2)] != 0.0 ? 1.0 : 0.0, 1.0, _TOL);

    // --- determinant / trace / trace3 ---
    printf("--- det / trace ---\n");

    _check_f("det_2", math_mat4_det_2(a), 24.0, _FTOL);
    _check_f("det_1", math_mat4_det_1(pa), 24.0, _FTOL);
    _check_f("det_2 identity", math_mat4_det_2(id), 1.0, _FTOL);

    _check_f("trace_2", math_mat4_trace_2(a), 10.0, _FTOL);
    _check_f("trace_1", math_mat4_trace_1(pa), 10.0, _FTOL);
    // trace3 = sum of upper-left 3x3 diagonal = 2+3+4 = 9.
    _check_f("trace3_2", math_mat4_trace3_2(a), 9.0, _FTOL);
    _check_f("trace3_1", math_mat4_trace3_1(pa), 9.0, _FTOL);

    // --- transpose: (A^T)^T == A, and A^T c0r3 == A c3r0 ---
    printf("--- transpose ---\n");

    Mat4 const at = math_mat4_transpose_2(a);
    // transpose swaps [col][row] with [row][col]: at.m[0][3] == a.m[3][0] == 5.
    _check_f("A^T c0r3", at.m[0][3], 5.0, _FTOL);
    // at.m[i][j] == a.m[j][i]: at.m[0][1] == a.m[1][0] == 1.
    _check_f("A^T c0r1", at.m[0][1], 1.0, _FTOL);
    Mat4 const att = math_mat4_transpose_2(at);
    _check_f("(A^T)^T c1r1", att.m[1][1], 3.0, _FTOL);
    _check_f("(A^T)^T c3r0", att.m[3][0], 5.0, _FTOL);
    FSize pat[16] = DEFAULT_INITIALIZATION;
    math_mat4_transpose_1(pa, pat);
    _check_f("A^T_1 c0r3", pat[_AT(0, 3)], 5.0, _FTOL);

    // --- mulv / mulv3 ---
    printf("--- mulv / mulv3 ---\n");

    // A * I applied to a vector is the vector itself; use identity for a clean check.
    Vec4 const v = { 1.0, 2.0, 3.0, 4.0 };
    Vec4 const idv = math_mat4_mulv_2(id, v);
    _check_f("I*v x", idv.x, 1.0, _FTOL);
    _check_f("I*v w", idv.w, 4.0, _FTOL);
    // A * e0 (=(1,0,0,0)) yields A's first column = (2,0,0,0).
    Vec4 const e0 = { 1.0, 0.0, 0.0, 0.0 };
    Vec4 const col0 = math_mat4_mulv_2(a, e0);
    _check_f("A*e0 x", col0.x, 2.0, _FTOL);
    _check_f("A*e0 y", col0.y, 0.0, _FTOL);
    FSize pv[4] = { 1.0, 0.0, 0.0, 0.0 };
    FSize pmv[4] = DEFAULT_INITIALIZATION;
    math_mat4_mulv_1(pa, pv, pmv);
    _check_f("A*e0_1 x", pmv[0], 2.0, _FTOL);

    // mulv3 with identity and last=1: result == input vec3.
    Vec3 const v3 = { 5.0, 6.0, 7.0 };
    Vec3 const idv3 = math_mat4_mulv3_2(id, v3, 1.0);
    _check_f("I*v3 x", idv3.x, 5.0, _FTOL);
    _check_f("I*v3 z", idv3.z, 7.0, _FTOL);
    FSize pv3[3] = { 5.0, 6.0, 7.0 };
    FSize pmv3[3] = DEFAULT_INITIALIZATION;
    math_mat4_mulv3_1(pid, pv3, 1.0, pmv3);
    _check_f("I*v3_1 y", pmv3[1], 6.0, _FTOL);

    // --- pick3 / pick3t / ins3 ---
    printf("--- pick3 / ins3 ---\n");

    Mat3 const p3 = math_mat4_pick3_2(a);
    // upper-left 3x3 of A: pick3 c2r2 == a.m[2][2] == 4.
    _check_f("pick3_2 c2r2", p3.m[2][2], 4.0, _FTOL);
    _check_f("pick3_2 c1r1", p3.m[1][1], 3.0, _FTOL);
    FSize pp3[9] = DEFAULT_INITIALIZATION;
    math_mat4_pick3_1(pa, pp3);
    // mat3 column-major flat index = col*3 + row; c0r0 = 2.
    _check_f("pick3_1 c0r0", pp3[0 * 3 + 0], 2.0, _FTOL);

    // pick3t transposes the 3x3: pick3t c0r1 == pick3 c1r0.
    Mat3 const p3t = math_mat4_pick3t_2(a);
    _check_f("pick3t_2 c0r1", p3t.m[0][1], p3.m[1][0], _FTOL);
    FSize pp3t[9] = DEFAULT_INITIALIZATION;
    math_mat4_pick3t_1(pa, pp3t);
    _check_f("pick3t_1 c1r0", pp3t[1 * 3 + 0], p3.m[0][1], _FTOL);

    // ins3: insert p3 into identity's upper-left; c2r2 becomes 4, bottom stays identity.
    Mat4 const ins = math_mat4_ins3_2(p3, id);
    _check_f("ins3_2 c2r2", ins.m[2][2], 4.0, _FTOL);
    _check_f("ins3_2 c3r3 kept", ins.m[3][3], 1.0, _FTOL);
    _check_f("ins3_2 c0r0", ins.m[0][0], 2.0, _FTOL);
    FSize pins[16] = DEFAULT_INITIALIZATION;
    math_mat4_identity_1(pins);
    math_mat4_ins3_1(pp3, pins);
    _check_f("ins3_1 c1r1", pins[_AT(1, 1)], 3.0, _FTOL);
    _check_f("ins3_1 c3r3 kept", pins[_AT(3, 3)], 1.0, _FTOL);

    // --- quat: identity matrix -> identity quaternion (0,0,0,1) ---
    printf("--- quat ---\n");

    Quat const q = math_mat4_quat_2(id);
    _check_f("quat_2 w", q.w, 1.0, _FTOL);
    _check_f("quat_2 x", q.x, 0.0, _FTOL);
    FSize pq[4] = DEFAULT_INITIALIZATION;
    math_mat4_quat_1(pid, pq);
    _check_f("quat_1 w", pq[3], 1.0, _FTOL);

    // --- scale / scale_p ---
    printf("--- scale ---\n");

    Mat4 const sc = math_mat4_scale_2(a, 2.0);
    _check_f("scale_2 c0r0", sc.m[0][0], 4.0, _FTOL);
    _check_f("scale_2 c3r2", sc.m[3][2], 14.0, _FTOL);
    FSize psc[16] = DEFAULT_INITIALIZATION;
    math_mat4_scale_1(pa, 2.0, psc);
    _check_f("scale_1 c1r1", psc[_AT(1, 1)], 6.0, _FTOL);

    Mat4 const scp = math_mat4_scale_p_2(a, 3.0);
    _check_f("scale_p_2 c1r1", scp.m[1][1], 9.0, _FTOL);
    FSize pscp[16] = DEFAULT_INITIALIZATION;
    math_mat4_scale_p_1(pa, 3.0, pscp);
    _check_f("scale_p_1 c0r0", pscp[_AT(0, 0)], 6.0, _FTOL);

    // --- swap_col / swap_row ---
    printf("--- swap ---\n");

    // swap columns 0 and 3: new c0 == old c3 (5,6,7,1), new c3 == old c0 (2,0,0,0).
    Mat4 const swc = math_mat4_swap_col_2(a, 0, 3);
    _check_f("swap_col_2 c0r0", swc.m[0][0], 5.0, _FTOL);
    _check_f("swap_col_2 c3r0", swc.m[3][0], 2.0, _FTOL);
    FSize pswc[16] = DEFAULT_INITIALIZATION;
    math_mat4_swap_col_1(pa, 0, 3, pswc);
    _check_f("swap_col_1 c0r2", pswc[_AT(0, 2)], 7.0, _FTOL);

    // swap rows 0 and 3: entry [col][0] <-> [col][3] for every column.
    Mat4 const swr = math_mat4_swap_row_2(a, 0, 3);
    _check_f("swap_row_2 c3r0", swr.m[3][0], 1.0, _FTOL);
    _check_f("swap_row_2 c0r3", swr.m[0][3], 2.0, _FTOL);
    FSize pswr[16] = DEFAULT_INITIALIZATION;
    math_mat4_swap_row_1(pa, 0, 3, pswr);
    _check_f("swap_row_1 c3r0", pswr[_AT(3, 0)], 1.0, _FTOL);

    // --- rmc: r * M * c ---
    printf("--- rmc ---\n");

    // r * I * c == dot(r, c). r=(1,2,3,4), c=(1,1,1,1) -> 1+2+3+4 = 10.
    Vec4 const r = { 1.0, 2.0, 3.0, 4.0 };
    Vec4 const c = { 1.0, 1.0, 1.0, 1.0 };
    _check_f("rmc_2 r*I*c", math_mat4_rmc_2(r, id, c), 10.0, _FTOL);
    FSize pr[4] = { 1.0, 2.0, 3.0, 4.0 };
    FSize pc[4] = { 1.0, 1.0, 1.0, 1.0 };
    _check_f("rmc_1 r*I*c", math_mat4_rmc_1(pr, pid, pc), 10.0, _FTOL);

    // --- textrans ---
    printf("--- textrans ---\n");

    // scale-only texture transform (sx=2, sy=3, no rot/trans): diagonal 2 and 3.
    Mat4 const tex = math_mat4_textrans_2(2.0, 3.0, 0.0, 0.0, 0.0);
    _check_f("textrans_2 c0r0", tex.m[0][0], 2.0, _FTOL);
    _check_f("textrans_2 c1r1", tex.m[1][1], 3.0, _FTOL);
    FSize ptex[16] = DEFAULT_INITIALIZATION;
    math_mat4_textrans_1(2.0, 3.0, 0.0, 4.0, 5.0, ptex);
    // translation lands in the last column, rows 0 and 1.
    _check_f("textrans_1 tx", ptex[_AT(3, 0)], 4.0, _FTOL);
    _check_f("textrans_1 ty", ptex[_AT(3, 1)], 5.0, _FTOL);

    // keep _check_u / _check_i / _check_b referenced so the harness helpers are exercised
    _check_u("pass counter positive", _pass > 0 ? 1 : 0, 1);
    _check_i("fail counter type", (ISize) 0, 0);
    _check_b("det nonzero", math_mat4_det_2(a) != 0.0, true);

    // textrans with rot == 0 is pure scale + translate: c = 1, s = 0 exactly.
    Mat4 const st = math_mat4_textrans_2(3.0, 5.0, 0.0, 7.0, 11.0);
    _check_f("textrans rot 0: [0][0] = sx", st.m[0][0], 3.0, 0.0);
    _check_f("textrans rot 0: [1][1] = sy", st.m[1][1], 5.0, 0.0);
    _check_f("textrans rot 0: [0][1] = 0", st.m[0][1], 0.0, 0.0);
    _check_f("textrans rot 0: [1][0] = 0", st.m[1][0], 0.0, 0.0);
    _check_f("textrans rot 0: [3][0] = tx", st.m[3][0], 7.0, 0.0);
    _check_f("textrans rot 0: [3][1] = ty", st.m[3][1], 11.0, 0.0);

    return _check_finish();
}