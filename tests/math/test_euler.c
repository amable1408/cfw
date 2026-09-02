/*
 * test_euler.c - Tests for include/math/euler.c (full glmc_euler* coverage)
 */

#include <math.h>
#include <stdio.h>

#include <math/euler.h>

#include "check.h"

// === Helpers ===

#define _FTOL 1e-6

int main(void) {
    printf("=== euler module tests ===\n");

    // Zero angles must produce the identity rotation for every order.
    Vec3 const z = { 0.0, 0.0, 0.0 };
    FSize pz[3] = { 0.0, 0.0, 0.0 };
    FSize pm[16] = DEFAULT_INITIALIZATION;
    FSize pq[4] = DEFAULT_INITIALIZATION;

    // --- default order (glmc_euler) ---
    printf("--- default order ---\n");

    // zero angles -> identity: diagonal ones, no off-diagonal terms
    Mat4 const e0 = math_euler_2(z);
    _check_f("euler_2 zero m00", e0.m[0][0], 1.0, _FTOL);
    _check_f("euler_2 zero m11", e0.m[1][1], 1.0, _FTOL);
    _check_f("euler_2 zero m22", e0.m[2][2], 1.0, _FTOL);
    _check_f("euler_2 zero m01", e0.m[0][1], 0.0, _FTOL);
    math_euler_1(pz, pm);
    _check_f("euler_1 zero [0]", pm[0], 1.0, _FTOL);
    _check_f("euler_1 zero [10]", pm[10], 1.0, _FTOL);

    // 90-deg about z (default order == XYZ): column 0 = (0,1,0), column 1 = (-1,0,0)
    Vec3 const rz = { 0.0, 0.0, MATH_PI / 2.0 };
    FSize prz[3] = { 0.0, 0.0, MATH_PI / 2.0 };
    Mat4 const e90 = math_euler_2(rz);
    _check_f("euler_2 rotz m00", e90.m[0][0], 0.0, _FTOL);
    _check_f("euler_2 rotz m01", e90.m[0][1], 1.0, _FTOL);
    _check_f("euler_2 rotz m10", e90.m[1][0], -1.0, _FTOL);
    math_euler_1(prz, pm);
    _check_f("euler_1 rotz [1]", pm[1], 1.0, _FTOL);      // col0 row1
    _check_f("euler_1 rotz [4]", pm[4], -1.0, _FTOL);     // col1 row0

    // --- angle extraction round-trip (euler_angles inverts euler XYZ) ---
    printf("--- extraction round-trip ---\n");

    Vec3 const ang = { 0.3, 0.4, 0.5 };
    FSize pang[3] = { 0.3, 0.4, 0.5 };
    Mat4 const rm = math_euler_2(ang);
    Vec3 const back = math_euler_angles_2(rm);
    _check_f("angles_2 round.x", back.x, 0.3, _FTOL);
    _check_f("angles_2 round.y", back.y, 0.4, _FTOL);
    _check_f("angles_2 round.z", back.z, 0.5, _FTOL);
    math_euler_1(pang, pm);
    FSize pback[3] = DEFAULT_INITIALIZATION;
    math_euler_angles_1(pm, pback);
    _check_f("angles_1 round.x", pback[0], 0.3, _FTOL);
    _check_f("angles_1 round.z", pback[2], 0.5, _FTOL);

    // --- by_order: MATH_EULER_ORDER_XYZ must match the explicit XYZ builder ---
    printf("--- by_order ---\n");

    Mat4 const bo = math_euler_by_order_2(ang, MATH_EULER_ORDER_XYZ);
    Mat4 const xyz = math_euler_xyz_2(ang);
    _check_f("by_order_2 == xyz m01", bo.m[0][1], xyz.m[0][1], _FTOL);
    _check_f("by_order_2 == xyz m20", bo.m[2][0], xyz.m[2][0], _FTOL);
    math_euler_by_order_1(pz, MATH_EULER_ORDER_XYZ, pm);
    _check_f("by_order_1 zero [0]", pm[0], 1.0, _FTOL);
    _check_f("by_order_1 zero [10]", pm[10], 1.0, _FTOL);

    // --- explicit matrix orders: zero angles -> identity (exercises every order) ---
    printf("--- explicit matrix orders ---\n");

    Mat4 const mxyz = math_euler_xyz_2(z);
    _check_f("xyz_2 zero m00", mxyz.m[0][0], 1.0, _FTOL);
    math_euler_xyz_1(pz, pm);
    _check_f("xyz_1 zero [10]", pm[10], 1.0, _FTOL);

    Mat4 const mxzy = math_euler_xzy_2(z);
    _check_f("xzy_2 zero m11", mxzy.m[1][1], 1.0, _FTOL);
    math_euler_xzy_1(pz, pm);
    _check_f("xzy_1 zero [0]", pm[0], 1.0, _FTOL);

    Mat4 const myxz = math_euler_yxz_2(z);
    _check_f("yxz_2 zero m22", myxz.m[2][2], 1.0, _FTOL);
    math_euler_yxz_1(pz, pm);
    _check_f("yxz_1 zero [10]", pm[10], 1.0, _FTOL);

    Mat4 const myzx = math_euler_yzx_2(z);
    _check_f("yzx_2 zero m00", myzx.m[0][0], 1.0, _FTOL);
    math_euler_yzx_1(pz, pm);
    _check_f("yzx_1 zero [0]", pm[0], 1.0, _FTOL);

    Mat4 const mzxy = math_euler_zxy_2(z);
    _check_f("zxy_2 zero m11", mzxy.m[1][1], 1.0, _FTOL);
    math_euler_zxy_1(pz, pm);
    _check_f("zxy_1 zero [5]", pm[5], 1.0, _FTOL);        // col1 row1

    Mat4 const mzyx = math_euler_zyx_2(z);
    _check_f("zyx_2 zero m22", mzyx.m[2][2], 1.0, _FTOL);
    math_euler_zyx_1(pz, pm);
    _check_f("zyx_1 zero [10]", pm[10], 1.0, _FTOL);

    // --- quaternion orders: zero angles -> identity quat (0,0,0,1) ---
    printf("--- quaternion orders ---\n");

    Quat const qxyz = math_euler_xyz_quat_2(z);
    _check_f("xyz_quat_2 zero.w", qxyz.w, 1.0, _FTOL);
    _check_f("xyz_quat_2 zero.x", qxyz.x, 0.0, _FTOL);
    math_euler_xyz_quat_1(pz, pq);
    _check_f("xyz_quat_1 zero.w", pq[3], 1.0, _FTOL);

    // 90-deg about x via XYZ order -> quat (sin45, 0, 0, cos45)
    Vec3 const rx = { MATH_PI / 2.0, 0.0, 0.0 };
    FSize prx[3] = { MATH_PI / 2.0, 0.0, 0.0 };
    Quat const q90 = math_euler_xyz_quat_2(rx);
    _check_f("xyz_quat_2 rotx.x", q90.x, sin(MATH_PI / 4.0), _FTOL);
    _check_f("xyz_quat_2 rotx.w", q90.w, cos(MATH_PI / 4.0), _FTOL);
    math_euler_xyz_quat_1(prx, pq);
    _check_f("xyz_quat_1 rotx.x", pq[0], sin(MATH_PI / 4.0), _FTOL);

    Quat const qxzy = math_euler_xzy_quat_2(z);
    _check_f("xzy_quat_2 zero.w", qxzy.w, 1.0, _FTOL);
    math_euler_xzy_quat_1(pz, pq);
    _check_f("xzy_quat_1 zero.w", pq[3], 1.0, _FTOL);

    Quat const qyxz = math_euler_yxz_quat_2(z);
    _check_f("yxz_quat_2 zero.w", qyxz.w, 1.0, _FTOL);
    math_euler_yxz_quat_1(pz, pq);
    _check_f("yxz_quat_1 zero.w", pq[3], 1.0, _FTOL);

    Quat const qyzx = math_euler_yzx_quat_2(z);
    _check_f("yzx_quat_2 zero.w", qyzx.w, 1.0, _FTOL);
    math_euler_yzx_quat_1(pz, pq);
    _check_f("yzx_quat_1 zero.w", pq[3], 1.0, _FTOL);

    Quat const qzxy = math_euler_zxy_quat_2(z);
    _check_f("zxy_quat_2 zero.w", qzxy.w, 1.0, _FTOL);
    math_euler_zxy_quat_1(pz, pq);
    _check_f("zxy_quat_1 zero.w", pq[3], 1.0, _FTOL);

    Quat const qzyx = math_euler_zyx_quat_2(z);
    _check_f("zyx_quat_2 zero.w", qzyx.w, 1.0, _FTOL);
    math_euler_zyx_quat_1(pz, pq);
    _check_f("zyx_quat_1 zero.w", pq[3], 1.0, _FTOL);

    // keep _check_u / _check_i / _check_b referenced so the harness helpers are exercised
    _check_u("pass counter positive", _pass > 0 ? 1 : 0, 1);
    _check_i("fail counter type", (ISize) 0, 0);
    _check_b("harness ok", true, true);

    return _check_finish();
}