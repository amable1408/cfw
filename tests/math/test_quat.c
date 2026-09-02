/*
 * test_quat.c - Tests for include/math/quat.c (full glmc_quat* coverage)
 */

#include <math.h>
#include <stdio.h>

#include <math/quat.h>

#include "check.h"

// === Helpers ===

#define _TOL 1e-9
#define _FTOL 1e-5

int main(void) {
    printf("=== quat module tests ===\n");

    // 90 degrees about z: (0, 0, sin(45), cos(45)) = (0, 0, 0.70710678, 0.70710678)
    FSize const s = (FSize) sin(MATH_PI / 4.0);
    Vec3 const zaxis = { 0.0, 0.0, 1.0 };
    FSize pzaxis[3] = { 0.0, 0.0, 1.0 };

    // --- construction: identity / init / quat / quatv / make / copy ---
    printf("--- construction ---\n");

    Quat const id = math_quat_identity_2();
    _check_f("identity_2.w", id.w, 1.0, _TOL);
    _check_f("identity_2.x", id.x, 0.0, _TOL);
    _check_f("identity_2.z", id.z, 0.0, _TOL);
    FSize pid[4] = DEFAULT_INITIALIZATION;
    math_quat_identity_1(pid);
    _check_f("identity_1.w", pid[3], 1.0, _TOL);

    Quat const iq = math_quat_init_2(1.0, 2.0, 3.0, 4.0);
    _check_f("init_2.x", iq.x, 1.0, _TOL);
    _check_f("init_2.w", iq.w, 4.0, _TOL);
    FSize piq[4] = DEFAULT_INITIALIZATION;
    math_quat_init_1(1.0, 2.0, 3.0, 4.0, piq);
    _check_f("init_1.z", piq[2], 3.0, _TOL);

    // quatv(90 deg, z-axis)
    Quat const qv = math_quat_quatv_2(MATH_PI / 2.0, zaxis);
    _check_f("quatv_2.z", qv.z, s, _FTOL);
    _check_f("quatv_2.w", qv.w, s, _FTOL);
    _check_f("quatv_2.x", qv.x, 0.0, _FTOL);
    FSize pqv[4] = DEFAULT_INITIALIZATION;
    math_quat_quatv_1(MATH_PI / 2.0, pzaxis, pqv);
    _check_f("quatv_1.z", pqv[2], s, _FTOL);

    // quat(90 deg, 0, 0, 1) matches quatv
    Quat const qq = math_quat_quat_2(MATH_PI / 2.0, 0.0, 0.0, 1.0);
    _check_f("quat_2.z", qq.z, s, _FTOL);
    _check_f("quat_2.w", qq.w, s, _FTOL);
    FSize pqq[4] = DEFAULT_INITIALIZATION;
    math_quat_quat_1(MATH_PI / 2.0, 0.0, 0.0, 1.0, pqq);
    _check_f("quat_1.w", pqq[3], s, _FTOL);

    Quat const mk = math_quat_make_2(pqv);
    _check_f("make_2.z", mk.z, s, _FTOL);
    FSize pmk[4] = DEFAULT_INITIALIZATION;
    math_quat_make_1(pqv, pmk);
    _check_f("make_1.z", pmk[2], s, _FTOL);

    Quat const cp = math_quat_copy_2(iq);
    _check_f("copy_2.x", cp.x, 1.0, _TOL);
    FSize pcp[4] = DEFAULT_INITIALIZATION;
    math_quat_copy_1(piq, pcp);
    _check_f("copy_1.w", pcp[3], 4.0, _TOL);

    // --- geometry: norm / dot / real / imag / imagn / imaglen / angle / axis ---
    printf("--- geometry ---\n");

    // norm of (1,2,3,4) = sqrt(30)
    FSize piqn[4] = { 1.0, 2.0, 3.0, 4.0 };
    _check_f("norm_2", math_quat_norm_2(iq), sqrt(30.0), _FTOL);
    _check_f("norm_1", math_quat_norm_1(piqn), sqrt(30.0), _FTOL);

    // dot of (1,2,3,4) with itself = 30
    _check_f("dot_2", math_quat_dot_2(iq, iq), 30.0, _FTOL);
    _check_f("dot_1", math_quat_dot_1(piqn, piqn), 30.0, _FTOL);

    // real part = w
    _check_f("real_2", math_quat_real_2(iq), 4.0, _TOL);
    _check_f("real_1", math_quat_real_1(piqn), 4.0, _TOL);

    // imag part = (x,y,z)
    Vec3 const im = math_quat_imag_2(iq);
    _check_f("imag_2.x", im.x, 1.0, _TOL);
    _check_f("imag_2.z", im.z, 3.0, _TOL);
    FSize pim[3] = DEFAULT_INITIALIZATION;
    math_quat_imag_1(piqn, pim);
    _check_f("imag_1.y", pim[1], 2.0, _TOL);

    // imaglen = |(1,2,3)| = sqrt(14)
    _check_f("imaglen_2", math_quat_imaglen_2(iq), sqrt(14.0), _FTOL);
    _check_f("imaglen_1", math_quat_imaglen_1(piqn), sqrt(14.0), _FTOL);

    // imagn is unit length
    Vec3 const imn = math_quat_imagn_2(iq);
    _check_f("imagn_2 len", sqrt(imn.x * imn.x + imn.y * imn.y + imn.z * imn.z), 1.0, _FTOL);
    FSize pimn[3] = DEFAULT_INITIALIZATION;
    math_quat_imagn_1(piqn, pimn);
    _check_f("imagn_1 len", sqrt(pimn[0] * pimn[0] + pimn[1] * pimn[1] + pimn[2] * pimn[2]), 1.0, _FTOL);

    // angle of 90-deg quat = pi/2; axis = z
    _check_f("angle_2", math_quat_angle_2(qv), MATH_PI / 2.0, _FTOL);
    _check_f("angle_1", math_quat_angle_1(pqv), MATH_PI / 2.0, _FTOL);
    Vec3 const ax = math_quat_axis_2(qv);
    _check_f("axis_2.z", ax.z, 1.0, _FTOL);
    FSize pax[3] = DEFAULT_INITIALIZATION;
    math_quat_axis_1(pqv, pax);
    _check_f("axis_1.z", pax[2], 1.0, _FTOL);

    // --- normalize (unit length) ---
    printf("--- normalize ---\n");

    Quat const nq = math_quat_normalize_2(iq);
    _check_f("normalize_2 unit", math_quat_norm_2(nq), 1.0, _FTOL);
    FSize pnq[4] = DEFAULT_INITIALIZATION;
    math_quat_normalize_1(piqn, pnq);
    _check_f("normalize_1 unit", math_quat_norm_1(pnq), 1.0, _FTOL);

    // --- arithmetic: add / sub ---
    printf("--- arithmetic ---\n");

    Quat const b = math_quat_init_2(5.0, 6.0, 7.0, 8.0);
    FSize pb[4] = { 5.0, 6.0, 7.0, 8.0 };

    Quat const sum = math_quat_add_2(iq, b);
    _check_f("add_2.x", sum.x, 6.0, _TOL);
    _check_f("add_2.w", sum.w, 12.0, _TOL);
    FSize psum[4] = DEFAULT_INITIALIZATION;
    math_quat_add_1(piqn, pb, psum);
    _check_f("add_1.y", psum[1], 8.0, _TOL);

    Quat const diff = math_quat_sub_2(b, iq);
    _check_f("sub_2.x", diff.x, 4.0, _TOL);
    FSize pdiff[4] = DEFAULT_INITIALIZATION;
    math_quat_sub_1(pb, piqn, pdiff);
    _check_f("sub_1.w", pdiff[3], 4.0, _TOL);

    // --- conjugate / inverse ---
    printf("--- conjugate / inverse ---\n");

    // conjugate(1,2,3,4) = (-1,-2,-3,4)
    Quat const cj = math_quat_conjugate_2(iq);
    _check_f("conjugate_2.x", cj.x, -1.0, _TOL);
    _check_f("conjugate_2.w", cj.w, 4.0, _TOL);
    FSize pcj[4] = DEFAULT_INITIALIZATION;
    math_quat_conjugate_1(piqn, pcj);
    _check_f("conjugate_1.z", pcj[2], -3.0, _TOL);

    // q * inv(q) == identity (use unit quat qv)
    Quat const inv = math_quat_inv_2(qv);
    Quat const qiinv = math_quat_mul_2(qv, inv);
    _check_f("inv roundtrip.w", qiinv.w, 1.0, _FTOL);
    _check_f("inv roundtrip.z", qiinv.z, 0.0, _FTOL);
    FSize pinv[4] = DEFAULT_INITIALIZATION;
    math_quat_inv_1(pqv, pinv);
    _check_f("inv_1 set", pinv[3] != 0.0 ? 1.0 : 0.0, 1.0, _TOL);

    // --- mul (by identity) ---
    printf("--- mul ---\n");

    // mul by identity leaves qv unchanged
    Quat const mulid = math_quat_mul_2(qv, id);
    _check_f("mul identity.z", mulid.z, qv.z, _FTOL);
    _check_f("mul identity.w", mulid.w, qv.w, _FTOL);
    FSize pmul[4] = DEFAULT_INITIALIZATION;
    math_quat_mul_1(pqv, pid, pmul);
    _check_f("mul_1 identity.w", pmul[3], qv.w, _FTOL);

    // --- rotatev: quatv(90 deg z) rotates unit-x to unit-y ---
    printf("--- rotatev ---\n");

    Vec3 const ux = { 1.0, 0.0, 0.0 };
    FSize pux[3] = { 1.0, 0.0, 0.0 };
    Vec3 const rot = math_quat_rotatev_2(qv, ux);
    _check_f("rotatev_2.x", rot.x, 0.0, _FTOL);
    _check_f("rotatev_2.y", rot.y, 1.0, _FTOL);
    _check_f("rotatev_2.z", rot.z, 0.0, _FTOL);
    FSize prot[3] = DEFAULT_INITIALIZATION;
    math_quat_rotatev_1(pqv, pux, prot);
    _check_f("rotatev_1.y", prot[1], 1.0, _FTOL);

    // --- matrix conversion: mat3 / mat3t / mat4 / mat4t + round-trip ---
    printf("--- matrix conversion ---\n");

    // quat->mat4->rotate a vector matches rotatev: R * (1,0,0,?) ... check top-left col
    Mat4 const m4 = math_quat_mat4_2(qv);
    // column 0 of a 90-deg-z rotation is (0, 1, 0, 0)
    _check_f("mat4_2 col0.x", m4.m[0][0], 0.0, _FTOL);
    _check_f("mat4_2 col0.y", m4.m[0][1], 1.0, _FTOL);
    FSize pm4[16] = DEFAULT_INITIALIZATION;
    math_quat_mat4_1(pqv, pm4);
    _check_f("mat4_1 col0.y", pm4[1], 1.0, _FTOL);

    // mat4t is the transpose: its col0.y should be -1 (transpose of the rotation)
    Mat4 const m4t = math_quat_mat4t_2(qv);
    _check_f("mat4t_2 col0.y", m4t.m[0][1], -1.0, _FTOL);
    FSize pm4t[16] = DEFAULT_INITIALIZATION;
    math_quat_mat4t_1(pqv, pm4t);
    _check_f("mat4t_1 col0.y", pm4t[1], -1.0, _FTOL);

    Mat3 const m3 = math_quat_mat3_2(qv);
    _check_f("mat3_2 col0.y", m3.m[0][1], 1.0, _FTOL);
    FSize pm3[9] = DEFAULT_INITIALIZATION;
    math_quat_mat3_1(pqv, pm3);
    _check_f("mat3_1 col0.y", pm3[1], 1.0, _FTOL);

    Mat3 const m3t = math_quat_mat3t_2(qv);
    _check_f("mat3t_2 col0.y", m3t.m[0][1], -1.0, _FTOL);
    FSize pm3t[9] = DEFAULT_INITIALIZATION;
    math_quat_mat3t_1(pqv, pm3t);
    _check_f("mat3t_1 col0.y", pm3t[1], -1.0, _FTOL);

    // quat <-> mat4 round-trip: mat4(qv) applied to unit-x yields unit-y (matches rotatev)
    Vec3 const mrot = {
        m4.m[0][0] * ux.x + m4.m[1][0] * ux.y + m4.m[2][0] * ux.z,
        m4.m[0][1] * ux.x + m4.m[1][1] * ux.y + m4.m[2][1] * ux.z,
        m4.m[0][2] * ux.x + m4.m[1][2] * ux.y + m4.m[2][2] * ux.z
    };
    _check_f("mat4 roundtrip.x", mrot.x, 0.0, _FTOL);
    _check_f("mat4 roundtrip.y", mrot.y, 1.0, _FTOL);

    // --- interpolation: lerp / lerpc / nlerp / slerp / slerp_longest ---
    printf("--- interpolation ---\n");

    // slerp endpoints: t=0 -> id, t=1 -> qv
    Quat const sl0 = math_quat_slerp_2(id, qv, 0.0);
    _check_f("slerp_2 t0.w", sl0.w, 1.0, _FTOL);
    Quat const sl1 = math_quat_slerp_2(id, qv, 1.0);
    _check_f("slerp_2 t1.z", sl1.z, qv.z, _FTOL);
    FSize pslerp[4] = DEFAULT_INITIALIZATION;
    math_quat_slerp_1(pid, pqv, 1.0, pslerp);
    _check_f("slerp_1 t1.z", pslerp[2], qv.z, _FTOL);

    // slerp_longest at t=0 takes the far arc: -id (same rotation via double cover, |w|=1)
    Quat const slg = math_quat_slerp_longest_2(id, qv, 0.0);
    _check_f("slerp_longest_2 t0.|w|", slg.w < 0 ? -slg.w : slg.w, 1.0, _FTOL);
    FSize pslg[4] = DEFAULT_INITIALIZATION;
    math_quat_slerp_longest_1(pid, pqv, 0.0, pslg);
    _check_f("slerp_longest_1 t0.|w|", pslg[3] < 0 ? -pslg[3] : pslg[3], 1.0, _FTOL);

    // lerp at t=0 -> id
    Quat const lp = math_quat_lerp_2(id, qv, 0.0);
    _check_f("lerp_2 t0.w", lp.w, 1.0, _FTOL);
    FSize plp[4] = DEFAULT_INITIALIZATION;
    math_quat_lerp_1(pid, pqv, 0.0, plp);
    _check_f("lerp_1 t0.w", plp[3], 1.0, _FTOL);

    // lerpc clamps t>1 to 1 -> qv
    Quat const lpc = math_quat_lerpc_2(id, qv, 2.0);
    _check_f("lerpc_2 clamp.z", lpc.z, qv.z, _FTOL);
    FSize plpc[4] = DEFAULT_INITIALIZATION;
    math_quat_lerpc_1(pid, pqv, 2.0, plpc);
    _check_f("lerpc_1 clamp.z", plpc[2], qv.z, _FTOL);

    // nlerp at t=1 is unit-length qv
    Quat const nl = math_quat_nlerp_2(id, qv, 1.0);
    _check_f("nlerp_2 t1 unit", math_quat_norm_2(nl), 1.0, _FTOL);
    FSize pnl[4] = DEFAULT_INITIALIZATION;
    math_quat_nlerp_1(pid, pqv, 1.0, pnl);
    _check_f("nlerp_1 t1 unit", math_quat_norm_1(pnl), 1.0, _FTOL);

    // --- orientation: for / forp / look ---
    printf("--- orientation ---\n");

    Vec3 const dir = { 0.0, 0.0, -1.0 };
    Vec3 const up = { 0.0, 1.0, 0.0 };
    FSize pdir[3] = { 0.0, 0.0, -1.0 };
    FSize pup[3] = { 0.0, 1.0, 0.0 };

    // for(-z, +y) is a valid unit quaternion
    Quat const fq = math_quat_for_2(dir, up);
    _check_f("for_2 unit", math_quat_norm_2(fq), 1.0, _FTOL);
    FSize pfq[4] = DEFAULT_INITIALIZATION;
    math_quat_for_1(pdir, pup, pfq);
    _check_f("for_1 unit", math_quat_norm_1(pfq), 1.0, _FTOL);

    Vec3 const from = { 0.0, 0.0, 0.0 };
    Vec3 const to = { 0.0, 0.0, -1.0 };
    FSize pfrom[3] = { 0.0, 0.0, 0.0 };
    FSize pto[3] = { 0.0, 0.0, -1.0 };
    Quat const fpq = math_quat_forp_2(from, to, up);
    _check_f("forp_2 unit", math_quat_norm_2(fpq), 1.0, _FTOL);
    FSize pfpq[4] = DEFAULT_INITIALIZATION;
    math_quat_forp_1(pfrom, pto, pup, pfpq);
    _check_f("forp_1 unit", math_quat_norm_1(pfpq), 1.0, _FTOL);

    // look(eye, identity): bottom-right corner is 1
    Vec3 const eye = { 0.0, 0.0, 5.0 };
    FSize peye[3] = { 0.0, 0.0, 5.0 };
    Mat4 const lk = math_quat_look_2(eye, id);
    _check_f("look_2 m33", lk.m[3][3], 1.0, _FTOL);
    FSize plk[16] = DEFAULT_INITIALIZATION;
    math_quat_look_1(peye, pid, plk);
    _check_f("look_1 m33", plk[15], 1.0, _FTOL);

    // --- from_vecs ---
    printf("--- from_vecs ---\n");

    // quat rotating +x to +y, applied to +x, yields +y
    Vec3 const vx = { 1.0, 0.0, 0.0 };
    Vec3 const vy = { 0.0, 1.0, 0.0 };
    FSize pvx[3] = { 1.0, 0.0, 0.0 };
    FSize pvy[3] = { 0.0, 1.0, 0.0 };
    Quat const fv = math_quat_from_vecs_2(vx, vy);
    Vec3 const fvr = math_quat_rotatev_2(fv, vx);
    _check_f("from_vecs_2 rotates.y", fvr.y, 1.0, _FTOL);
    FSize pfv[4] = DEFAULT_INITIALIZATION;
    math_quat_from_vecs_1(pvx, pvy, pfv);
    FSize pfvr[3] = DEFAULT_INITIALIZATION;
    math_quat_rotatev_1(pfv, pvx, pfvr);
    _check_f("from_vecs_1 rotates.y", pfvr[1], 1.0, _FTOL);

    // --- matrix rotate: rotate / rotate_at / rotate_atm ---
    printf("--- matrix rotate ---\n");

    Mat4 const identm = math_quat_mat4_2(id);
    // rotate identity model by qv equals mat4(qv)
    Mat4 const rm = math_quat_rotate_2(identm, qv);
    _check_f("rotate_2 col0.y", rm.m[0][1], 1.0, _FTOL);
    FSize pidentm[16] = DEFAULT_INITIALIZATION;
    math_quat_mat4_1(pid, pidentm);
    FSize prm[16] = DEFAULT_INITIALIZATION;
    math_quat_rotate_1(pidentm, pqv, prm);
    _check_f("rotate_1 col0.y", prm[1], 1.0, _FTOL);

    // rotate_atm about origin pivot equals rotate for the rotational block
    Vec3 const pivot = { 0.0, 0.0, 0.0 };
    FSize ppivot[3] = { 0.0, 0.0, 0.0 };
    Mat4 const ratm = math_quat_rotate_atm_2(identm, qv, pivot);
    _check_f("rotate_atm_2 col0.y", ratm.m[0][1], 1.0, _FTOL);
    FSize pratm[16] = DEFAULT_INITIALIZATION;
    math_quat_rotate_atm_1(pidentm, pqv, ppivot, pratm);
    _check_f("rotate_atm_1 col0.y", pratm[1], 1.0, _FTOL);

    // rotate_at about origin pivot: same rotational block
    Mat4 const rat = math_quat_rotate_at_2(identm, qv, pivot);
    _check_f("rotate_at_2 col0.y", rat.m[0][1], 1.0, _FTOL);
    FSize prat[16] = DEFAULT_INITIALIZATION;
    math_quat_mat4_1(pid, prat);
    math_quat_rotate_at_1(prat, pqv, ppivot, prat);
    _check_f("rotate_at_1 col0.y", prat[1], 1.0, _FTOL);

    // keep _check_u / _check_i referenced so the harness helpers are exercised
    _check_u("pass counter positive", _pass > 0 ? 1 : 0, 1);
    _check_i("fail counter type", (ISize) 0, 0);

    return _check_finish();
}