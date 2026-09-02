/*
 * test_affine.c - Tests for include/math/affine.c (full call/affine.h coverage)
 */

#include <math.h>
#include <stdio.h>

#include <math/affine.h>
#include <math/mat4.h>

#include "check.h"

// === Helpers ===

#define _FTOL 1e-4

// Column-major flat index for a 4x4: col * 4 + row.
#define _AT(col, row) ((col) * 4 + (row))

int main(void) {
    printf("=== affine module tests ===\n");

    Mat4 const id = math_mat4_identity_2();
    FSize pid[16] = DEFAULT_INITIALIZATION;
    math_mat4_identity_1(pid);

    // --- translate: offset lands in column 3, rows 0..2 ---
    printf("--- translate ---\n");

    Vec3 const off = { 4.0, 5.0, 6.0 };
    Mat4 const tr = math_affine_translate_2(id, off);
    _check_f("translate_2 c3r0", tr.m[3][0], 4.0, _FTOL);
    _check_f("translate_2 c3r1", tr.m[3][1], 5.0, _FTOL);
    _check_f("translate_2 c3r2", tr.m[3][2], 6.0, _FTOL);
    _check_f("translate_2 c3r3 kept", tr.m[3][3], 1.0, _FTOL);

    FSize poff[3] = { 4.0, 5.0, 6.0 };
    FSize ptr[16] = DEFAULT_INITIALIZATION;
    math_affine_translate_1(pid, poff, ptr);
    _check_f("translate_1 c3r0", ptr[_AT(3, 0)], 4.0, _FTOL);
    _check_f("translate_1 c3r2", ptr[_AT(3, 2)], 6.0, _FTOL);

    // translate_make builds the same transform straight from the offset.
    Mat4 const trm = math_affine_translate_make_2(off);
    _check_f("translate_make_2 c3r1", trm.m[3][1], 5.0, _FTOL);
    _check_f("translate_make_2 c0r0", trm.m[0][0], 1.0, _FTOL);
    FSize ptrm[16] = DEFAULT_INITIALIZATION;
    math_affine_translate_make_1(poff, ptrm);
    _check_f("translate_make_1 c3r2", ptrm[_AT(3, 2)], 6.0, _FTOL);

    // per-axis translate.
    Mat4 const trx = math_affine_translate_x_2(id, 7.0);
    _check_f("translate_x_2 c3r0", trx.m[3][0], 7.0, _FTOL);
    Mat4 const try_ = math_affine_translate_y_2(id, 8.0);
    _check_f("translate_y_2 c3r1", try_.m[3][1], 8.0, _FTOL);
    Mat4 const trz = math_affine_translate_z_2(id, 9.0);
    _check_f("translate_z_2 c3r2", trz.m[3][2], 9.0, _FTOL);
    FSize ptrx[16] = DEFAULT_INITIALIZATION;
    math_affine_translate_x_1(pid, 7.0, ptrx);
    _check_f("translate_x_1 c3r0", ptrx[_AT(3, 0)], 7.0, _FTOL);
    FSize ptry[16] = DEFAULT_INITIALIZATION;
    math_affine_translate_y_1(pid, 8.0, ptry);
    _check_f("translate_y_1 c3r1", ptry[_AT(3, 1)], 8.0, _FTOL);
    FSize ptrz[16] = DEFAULT_INITIALIZATION;
    math_affine_translate_z_1(pid, 9.0, ptrz);
    _check_f("translate_z_1 c3r2", ptrz[_AT(3, 2)], 9.0, _FTOL);

    // affine-post translated variants match on identity (post == pre when base is I).
    Mat4 const trd = math_affine_translated_2(id, off);
    _check_f("translated_2 c3r0", trd.m[3][0], 4.0, _FTOL);
    FSize ptrd[16] = DEFAULT_INITIALIZATION;
    math_affine_translated_1(pid, poff, ptrd);
    _check_f("translated_1 c3r2", ptrd[_AT(3, 2)], 6.0, _FTOL);
    Mat4 const trdx = math_affine_translated_x_2(id, 7.0);
    _check_f("translated_x_2 c3r0", trdx.m[3][0], 7.0, _FTOL);
    Mat4 const trdy = math_affine_translated_y_2(id, 8.0);
    _check_f("translated_y_2 c3r1", trdy.m[3][1], 8.0, _FTOL);
    Mat4 const trdz = math_affine_translated_z_2(id, 9.0);
    _check_f("translated_z_2 c3r2", trdz.m[3][2], 9.0, _FTOL);
    FSize ptrdx[16] = DEFAULT_INITIALIZATION;
    math_affine_translated_x_1(pid, 7.0, ptrdx);
    _check_f("translated_x_1 c3r0", ptrdx[_AT(3, 0)], 7.0, _FTOL);
    FSize ptrdy[16] = DEFAULT_INITIALIZATION;
    math_affine_translated_y_1(pid, 8.0, ptrdy);
    _check_f("translated_y_1 c3r1", ptrdy[_AT(3, 1)], 8.0, _FTOL);
    FSize ptrdz[16] = DEFAULT_INITIALIZATION;
    math_affine_translated_z_1(pid, 9.0, ptrdz);
    _check_f("translated_z_1 c3r2", ptrdz[_AT(3, 2)], 9.0, _FTOL);

    // --- rotate: 90 deg about Z ---
    printf("--- rotate ---\n");

    // R_z(90): column-major m[0][0]=cos=0, m[0][1]=sin=1, m[1][0]=-sin=-1, m[1][1]=cos=0.
    FSize const half_pi = MATH_PI / 2.0;
    Mat4 const rz = math_affine_rotate_z_2(id, half_pi);
    _check_f("rotate_z_2 c0r0 cos", rz.m[0][0], 0.0, _FTOL);
    _check_f("rotate_z_2 c0r1 sin", rz.m[0][1], 1.0, _FTOL);
    _check_f("rotate_z_2 c1r0 -sin", rz.m[1][0], -1.0, _FTOL);
    _check_f("rotate_z_2 c1r1 cos", rz.m[1][1], 0.0, _FTOL);
    _check_f("rotate_z_2 c2r2 kept", rz.m[2][2], 1.0, _FTOL);
    FSize prz[16] = DEFAULT_INITIALIZATION;
    math_affine_rotate_z_1(pid, half_pi, prz);
    _check_f("rotate_z_1 c0r1 sin", prz[_AT(0, 1)], 1.0, _FTOL);

    // rotate about X: m[1][1]=cos=0, m[1][2]=sin=1.
    Mat4 const rx = math_affine_rotate_x_2(id, half_pi);
    _check_f("rotate_x_2 c1r1 cos", rx.m[1][1], 0.0, _FTOL);
    _check_f("rotate_x_2 c1r2 sin", rx.m[1][2], 1.0, _FTOL);
    // rotate about Y: m[0][0]=cos=0, m[0][2]=-sin=-1.
    Mat4 const ry = math_affine_rotate_y_2(id, half_pi);
    _check_f("rotate_y_2 c0r0 cos", ry.m[0][0], 0.0, _FTOL);
    _check_f("rotate_y_2 c0r2 -sin", ry.m[0][2], -1.0, _FTOL);
    FSize prx[16] = DEFAULT_INITIALIZATION;
    math_affine_rotate_x_1(pid, half_pi, prx);
    _check_f("rotate_x_1 c1r2 sin", prx[_AT(1, 2)], 1.0, _FTOL);
    FSize pry[16] = DEFAULT_INITIALIZATION;
    math_affine_rotate_y_1(pid, half_pi, pry);
    _check_f("rotate_y_1 c0r2 -sin", pry[_AT(0, 2)], -1.0, _FTOL);

    // general axis-angle rotate about Z matches rotate_z.
    Vec3 const zaxis = { 0.0, 0.0, 1.0 };
    Mat4 const rgen = math_affine_rotate_2(id, half_pi, zaxis);
    _check_f("rotate_2 c0r1 sin", rgen.m[0][1], 1.0, _FTOL);
    FSize pzaxis[3] = { 0.0, 0.0, 1.0 };
    FSize prgen[16] = DEFAULT_INITIALIZATION;
    math_affine_rotate_1(pid, half_pi, pzaxis, prgen);
    _check_f("rotate_1 c1r0 -sin", prgen[_AT(1, 0)], -1.0, _FTOL);

    // rotate_make builds the same rotation from scratch.
    Mat4 const rmk = math_affine_rotate_make_2(half_pi, zaxis);
    _check_f("rotate_make_2 c0r1 sin", rmk.m[0][1], 1.0, _FTOL);
    FSize prmk[16] = DEFAULT_INITIALIZATION;
    math_affine_rotate_make_1(half_pi, pzaxis, prmk);
    _check_f("rotate_make_1 c1r1 cos", prmk[_AT(1, 1)], 0.0, _FTOL);

    // affine-post rotated variants match on identity base.
    Mat4 const rdz = math_affine_rotated_z_2(id, half_pi);
    _check_f("rotated_z_2 c0r1 sin", rdz.m[0][1], 1.0, _FTOL);
    Mat4 const rdx = math_affine_rotated_x_2(id, half_pi);
    _check_f("rotated_x_2 c1r2 sin", rdx.m[1][2], 1.0, _FTOL);
    Mat4 const rdy = math_affine_rotated_y_2(id, half_pi);
    _check_f("rotated_y_2 c0r2 -sin", rdy.m[0][2], -1.0, _FTOL);
    Mat4 const rdgen = math_affine_rotated_2(id, half_pi, zaxis);
    _check_f("rotated_2 c0r1 sin", rdgen.m[0][1], 1.0, _FTOL);
    FSize prdz[16] = DEFAULT_INITIALIZATION;
    math_affine_rotated_z_1(pid, half_pi, prdz);
    _check_f("rotated_z_1 c0r1 sin", prdz[_AT(0, 1)], 1.0, _FTOL);
    FSize prdx[16] = DEFAULT_INITIALIZATION;
    math_affine_rotated_x_1(pid, half_pi, prdx);
    _check_f("rotated_x_1 c1r2 sin", prdx[_AT(1, 2)], 1.0, _FTOL);
    FSize prdy[16] = DEFAULT_INITIALIZATION;
    math_affine_rotated_y_1(pid, half_pi, prdy);
    _check_f("rotated_y_1 c0r2 -sin", prdy[_AT(0, 2)], -1.0, _FTOL);
    FSize prdgen[16] = DEFAULT_INITIALIZATION;
    math_affine_rotated_1(pid, half_pi, pzaxis, prdgen);
    _check_f("rotated_1 c1r0 -sin", prdgen[_AT(1, 0)], -1.0, _FTOL);

    // --- rotate_at / rotate_atm: rotating identity about the origin equals plain rotate ---
    printf("--- rotate_at / spin ---\n");

    Vec3 const origin = { 0.0, 0.0, 0.0 };
    Mat4 const rat = math_affine_rotate_at_2(id, origin, half_pi, zaxis);
    _check_f("rotate_at_2 c0r1 sin", rat.m[0][1], 1.0, _FTOL);
    Mat4 const ratm = math_affine_rotate_atm_2(origin, half_pi, zaxis);
    _check_f("rotate_atm_2 c0r1 sin", ratm.m[0][1], 1.0, _FTOL);
    FSize porigin[3] = { 0.0, 0.0, 0.0 };
    FSize prat[16] = DEFAULT_INITIALIZATION;
    math_affine_rotate_at_1(pid, porigin, half_pi, pzaxis, prat);
    _check_f("rotate_at_1 c1r0 -sin", prat[_AT(1, 0)], -1.0, _FTOL);
    FSize pratm[16] = DEFAULT_INITIALIZATION;
    math_affine_rotate_atm_1(porigin, half_pi, pzaxis, pratm);
    _check_f("rotate_atm_1 c0r1 sin", pratm[_AT(0, 1)], 1.0, _FTOL);
    Mat4 const rdat = math_affine_rotated_at_2(id, origin, half_pi, zaxis);
    _check_f("rotated_at_2 c0r1 sin", rdat.m[0][1], 1.0, _FTOL);
    FSize prdat[16] = DEFAULT_INITIALIZATION;
    math_affine_rotated_at_1(pid, porigin, half_pi, pzaxis, prdat);
    _check_f("rotated_at_1 c1r0 -sin", prdat[_AT(1, 0)], -1.0, _FTOL);

    // spin/spinned about Z from identity equals rotate_z.
    Mat4 const sp = math_affine_spin_2(id, half_pi, zaxis);
    _check_f("spin_2 c0r1 sin", sp.m[0][1], 1.0, _FTOL);
    Mat4 const spd = math_affine_spinned_2(id, half_pi, zaxis);
    _check_f("spinned_2 c0r1 sin", spd.m[0][1], 1.0, _FTOL);
    FSize psp[16] = DEFAULT_INITIALIZATION;
    math_affine_spin_1(pid, half_pi, pzaxis, psp);
    _check_f("spin_1 c1r0 -sin", psp[_AT(1, 0)], -1.0, _FTOL);
    FSize pspd[16] = DEFAULT_INITIALIZATION;
    math_affine_spinned_1(pid, half_pi, pzaxis, pspd);
    _check_f("spinned_1 c0r1 sin", pspd[_AT(0, 1)], 1.0, _FTOL);

    // --- scale: factors land on the diagonal ---
    printf("--- scale ---\n");

    Vec3 const facs = { 2.0, 3.0, 4.0 };
    Mat4 const sc = math_affine_scale_2(id, facs);
    _check_f("scale_2 c0r0", sc.m[0][0], 2.0, _FTOL);
    _check_f("scale_2 c1r1", sc.m[1][1], 3.0, _FTOL);
    _check_f("scale_2 c2r2", sc.m[2][2], 4.0, _FTOL);
    _check_f("scale_2 c3r3 kept", sc.m[3][3], 1.0, _FTOL);
    FSize pfacs[3] = { 2.0, 3.0, 4.0 };
    FSize psc[16] = DEFAULT_INITIALIZATION;
    math_affine_scale_1(pid, pfacs, psc);
    _check_f("scale_1 c1r1", psc[_AT(1, 1)], 3.0, _FTOL);

    Mat4 const scm = math_affine_scale_make_2(facs);
    _check_f("scale_make_2 c2r2", scm.m[2][2], 4.0, _FTOL);
    FSize pscm[16] = DEFAULT_INITIALIZATION;
    math_affine_scale_make_1(pfacs, pscm);
    _check_f("scale_make_1 c0r0", pscm[_AT(0, 0)], 2.0, _FTOL);

    Mat4 const scu = math_affine_scale_uni_2(id, 5.0);
    _check_f("scale_uni_2 c0r0", scu.m[0][0], 5.0, _FTOL);
    _check_f("scale_uni_2 c2r2", scu.m[2][2], 5.0, _FTOL);
    FSize pscu[16] = DEFAULT_INITIALIZATION;
    math_affine_scale_uni_1(pid, 5.0, pscu);
    _check_f("scale_uni_1 c1r1", pscu[_AT(1, 1)], 5.0, _FTOL);

    // --- uniscaled ---
    printf("--- uniscaled ---\n");

    _check_b("uniscaled_2 uniform", math_affine_uniscaled_2(scu), true);
    _check_b("uniscaled_2 non-uniform", math_affine_uniscaled_2(sc), false);
    _check_b("uniscaled_1 uniform", math_affine_uniscaled_1(pscu), true);
    _check_b("uniscaled_1 non-uniform", math_affine_uniscaled_1(psc), false);

    // --- mul / mul_rot composition ---
    printf("--- mul ---\n");

    // T * S: translation kept in column 3, scale on the diagonal (T*S is block form).
    Mat4 const comp = math_affine_mul_2(trm, scm);
    _check_f("mul_2 c0r0 scale", comp.m[0][0], 2.0, _FTOL);
    _check_f("mul_2 c1r1 scale", comp.m[1][1], 3.0, _FTOL);
    _check_f("mul_2 c3r0 trans", comp.m[3][0], 4.0, _FTOL);
    _check_f("mul_2 c3r2 trans", comp.m[3][2], 6.0, _FTOL);
    // mul by identity is a no-op.
    Mat4 const mulid = math_affine_mul_2(sc, id);
    _check_f("mul_2 A*I c2r2", mulid.m[2][2], 4.0, _FTOL);
    FSize pcomp[16] = DEFAULT_INITIALIZATION;
    math_affine_mul_1(ptrm, pscm, pcomp);
    _check_f("mul_1 c3r1 trans", pcomp[_AT(3, 1)], 5.0, _FTOL);

    // mul_rot composes two rotations: R_z(90) * R_z(90) == R_z(180) -> c0r0 = -1.
    Mat4 const rr = math_affine_mul_rot_2(rz, rz);
    _check_f("mul_rot_2 c0r0", rr.m[0][0], -1.0, _FTOL);
    _check_f("mul_rot_2 c0r1", rr.m[0][1], 0.0, _FTOL);
    FSize prr[16] = DEFAULT_INITIALIZATION;
    math_affine_mul_rot_1(prz, prz, prr);
    _check_f("mul_rot_1 c0r0", prr[_AT(0, 0)], -1.0, _FTOL);

    // --- inv_tr: inv(M) * M == I for a rotation+translation transform ---
    printf("--- inv_tr ---\n");

    // Build M = T * R (rotation + translation, no scale) via post-multiply translate on a rotation.
    Mat4 const rtm = math_affine_translate_2(rz, off);
    Mat4 const inv = math_affine_inv_tr_2(rtm);
    Mat4 const back = math_affine_mul_2(inv, rtm);
    _check_f("inv_tr*M c0r0", back.m[0][0], 1.0, _FTOL);
    _check_f("inv_tr*M c1r1", back.m[1][1], 1.0, _FTOL);
    _check_f("inv_tr*M c3r3", back.m[3][3], 1.0, _FTOL);
    _check_f("inv_tr*M c3r0 off", back.m[3][0], 0.0, _FTOL);
    _check_f("inv_tr*M c0r1 off", back.m[0][1], 0.0, _FTOL);
    FSize prtm[16] = DEFAULT_INITIALIZATION;
    math_affine_translate_1(prz, poff, prtm);
    FSize pinv[16] = DEFAULT_INITIALIZATION;
    math_affine_inv_tr_1(prtm, pinv);
    FSize pback[16] = DEFAULT_INITIALIZATION;
    math_affine_mul_1(pinv, prtm, pback);
    _check_f("inv_tr_1*M c2r2", pback[_AT(2, 2)], 1.0, _FTOL);

    // --- decompose round-trip: M = T * R * S recovers translation and scale ---
    printf("--- decompose ---\n");

    // Build M = translate(rotate(scale)) so that decompose returns t=(4,5,6), s=(2,3,4).
    FSize pm[16] = DEFAULT_INITIALIZATION;
    math_affine_translate_make_1(poff, pm);       // M = T
    math_affine_rotate_1(pm, half_pi, pzaxis, pm); // M = T * R
    math_affine_scale_1(pm, pfacs, pm);            // M = T * R * S

    FSize dt[4] = DEFAULT_INITIALIZATION;
    FSize dr[16] = DEFAULT_INITIALIZATION;
    FSize ds[3] = DEFAULT_INITIALIZATION;
    math_affine_decompose_1(pm, dt, dr, ds);
    _check_f("decompose t x", dt[0], 4.0, _FTOL);
    _check_f("decompose t y", dt[1], 5.0, _FTOL);
    _check_f("decompose t z", dt[2], 6.0, _FTOL);
    _check_f("decompose s x", ds[0], 2.0, _FTOL);
    _check_f("decompose s y", ds[1], 3.0, _FTOL);
    _check_f("decompose s z", ds[2], 4.0, _FTOL);
    // recovered rotation is the pure R_z(90): c0r1 == sin == 1.
    _check_f("decompose r c0r1 sin", dr[_AT(0, 1)], 1.0, _FTOL);

    // decompose_rs recovers the same rotation and scale (no translation output).
    FSize drr[16] = DEFAULT_INITIALIZATION;
    FSize drs[3] = DEFAULT_INITIALIZATION;
    math_affine_decompose_rs_1(pm, drr, drs);
    _check_f("decompose_rs s y", drs[1], 3.0, _FTOL);
    _check_f("decompose_rs r c1r0 -sin", drr[_AT(1, 0)], -1.0, _FTOL);

    // decompose_scalev recovers the scale vector alone (both variants).
    Mat4 const m2 = math_mat4_make_2(pm);
    Vec3 const sv = math_affine_decompose_scalev_2(m2);
    _check_f("decompose_scalev_2 x", sv.x, 2.0, _FTOL);
    _check_f("decompose_scalev_2 z", sv.z, 4.0, _FTOL);
    FSize psv[3] = DEFAULT_INITIALIZATION;
    math_affine_decompose_scalev_1(pm, psv);
    _check_f("decompose_scalev_1 y", psv[1], 3.0, _FTOL);

    // keep _check_u / _check_i referenced so the harness helpers are exercised.
    _check_u("pass counter positive", _pass > 0 ? 1 : 0, 1);
    _check_i("fail counter type", (ISize) 0, 0);

    return _check_finish();
}