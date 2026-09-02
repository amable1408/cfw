/*
 * test_math.c - Tests for the math umbrella (include/math/math.h) and its scalar core
 */

#include <limits.h>
#include <math.h>
#include <stdio.h>

#include <math/math.h>

#include "check.h"

// === Helpers ===

#define _TOL 1e-9

int main(void) {
    printf("=== math module tests ===\n");

    // math_floor_f
    _check_f("math_floor_f(3.7)",  math_floor_f(3.7),  3.0, _TOL);
    _check_f("math_floor_f(-1.2)", math_floor_f(-1.2), -2.0, _TOL);
    _check_f("math_floor_f(0.0)",  math_floor_f(0.0),  0.0, _TOL);

    // math_floor_u
    _check_u("math_floor_u(3.9)",  math_floor_u(3.9),  3);
    _check_u("math_floor_u(0.0)",  math_floor_u(0.0),  0);
    // boundary: exactly 2^64 (the double USIZE_MAX rounds up to) must saturate to 0
    _check_u("math_floor_u(2^64)", math_floor_u(0x1p64), 0);
    // boundary: largest double strictly below 2^64 must NOT saturate
    _check_b("math_floor_u(2^64-eps) nonzero", math_floor_u(0x1p64 - 4096.0) != 0, true);

    // math_ceil_f
    _check_f("math_ceil_f(2.1)",   math_ceil_f(2.1),   3.0, _TOL);
    _check_f("math_ceil_f(-1.9)",  math_ceil_f(-1.9),  -1.0, _TOL);

    // math_round_f / math_round_u
    _check_f("math_round_f(2.5)",  math_round_f(2.5),  3.0, _TOL);
    _check_f("math_round_f(2.4)",  math_round_f(2.4),  2.0, _TOL);
    _check_u("math_round_u(2.9)",  math_round_u(2.9),  3);
    _check_u("math_round_u(2.4)",  math_round_u(2.4),  2);
    // boundary: exactly 2^64 must saturate to 0
    _check_u("math_round_u(2^64)", math_round_u(0x1p64), 0);
    // boundary: largest double strictly below 2^64 must NOT saturate
    _check_b("math_round_u(2^64-eps) nonzero", math_round_u(0x1p64 - 4096.0) != 0, true);

    // math_trunc_f
    _check_f("math_trunc_f(3.9)",  math_trunc_f(3.9),  3.0, _TOL);
    _check_f("math_trunc_f(-3.9)", math_trunc_f(-3.9), -3.0, _TOL);

    // math_abs_i / math_abs_f
    _check_i("math_abs_i(-5)",          math_abs_i(-5),          5);
    _check_i("math_abs_i(5)",           math_abs_i(5),           5);
    // boundary: LLONG_MIN < -ISIZE_MAX => clamped to ISIZE_MAX
    _check_i("math_abs_i(LLONG_MIN)",   math_abs_i(LLONG_MIN),   (ISize) ISIZE_MAX);
    _check_f("math_abs_f(-2.5)",        math_abs_f(-2.5),        2.5, _TOL);
    _check_f("math_abs_f(2.5)",         math_abs_f(2.5),         2.5, _TOL);

    // math_sqrt_f
    _check_f("math_sqrt_f(4.0)",   math_sqrt_f(4.0),   2.0, _TOL);
    _check_f("math_sqrt_f(9.0)",   math_sqrt_f(9.0),   3.0, _TOL);
    _check_f("math_sqrt_f(0.0)",   math_sqrt_f(0.0),   0.0, _TOL);

    // math_pow_f / math_pow_u — 2^8 = 256, 3^3 = 27
    _check_f("math_pow_f(2.0,8.0)", math_pow_f(2.0, 8.0), 256.0, _TOL);
    _check_f("math_pow_f(3.0,3.0)", math_pow_f(3.0, 3.0),  27.0, _TOL);
    _check_f("math_pow_f(5.0,0.0)", math_pow_f(5.0, 0.0),   1.0, _TOL);
    _check_u("math_pow_u(2,8)",        math_pow_u(2.0, 8.0),   256);
    _check_u("math_pow_u(10,3)",       math_pow_u(10.0, 3.0), 1000);
    // boundary: negative result => 0
    _check_u("math_pow_u(-2,3)",       math_pow_u(-2.0, 3.0),    0);
    // boundary: NaN result (0^0 is 1 by convention; use -1^0.5 for NaN) => 0
    _check_u("math_pow_u(-1,0.5)=NaN", math_pow_u(-1.0, 0.5),    0);
    // boundary: 2^64 == exactly (FSize) USIZE_MAX (2^64-1 isn't double-representable) => saturate to 0
    _check_u("math_pow_u(2,64)=2^64", math_pow_u(2.0, 64.0), 0);
    // boundary: base^1 == base; use the largest double strictly below 2^64 => must NOT saturate
    _check_b("math_pow_u(2^64-eps,1) nonzero", math_pow_u(0x1p64 - 4096.0, 1.0) != 0, true);

    // math_exp_f / math_log_f / math_log10_f
    _check_f("math_exp_f(0.0)",    math_exp_f(0.0),    1.0, _TOL);
    _check_f("math_log_f(1.0)",    math_log_f(1.0),    0.0, _TOL);
    _check_f("math_log10_f(100.0)",math_log10_f(100.0),2.0, _TOL);

    // trig
    _check_f("math_sin_f(0.0)",    math_sin_f(0.0),    0.0, _TOL);
    _check_f("math_cos_f(0.0)",    math_cos_f(0.0),    1.0, _TOL);
    _check_f("math_tan_f(0.0)",    math_tan_f(0.0),    0.0, _TOL);
    _check_f("math_asin_f(0.0)",   math_asin_f(0.0),   0.0, _TOL);
    _check_f("math_acos_f(1.0)",   math_acos_f(1.0),   0.0, _TOL);
    _check_f("math_atan_f(0.0)",   math_atan_f(0.0),   0.0, _TOL);
    _check_f("math_atan2_f(1,1)",  math_atan2_f(1.0,1.0), MATH_PI/4.0, 1e-9);

    // math_fmod_f
    _check_f("math_fmod_f(5.5,2.0)", math_fmod_f(5.5, 2.0), 1.5, _TOL);

    // math_max / math_min
    _check_f("math_max_f(3,5)",    math_max_f(3.0,5.0), 5.0, _TOL);
    _check_f("math_min_f(3,5)",    math_min_f(3.0,5.0), 3.0, _TOL);
    _check_i("math_max_i(-2,4)",   math_max_i(-2,4),    4);
    _check_i("math_min_i(-2,4)",   math_min_i(-2,4),   -2);
    _check_u("math_max_u(10,20)",  math_max_u(10,20),  20);
    _check_u("math_min_u(10,20)",  math_min_u(10,20),  10);

    // math_negate
    _check_f("math_negate_f(3.0)", math_negate_f(3.0), -3.0, _TOL);
    _check_f("math_negate_f(-3.0)",math_negate_f(-3.0), 3.0, _TOL);
    // two's complement negation: -1 as USize wraps to USIZE_MAX, then +1 = 0; use small value
    _check_u("math_negate_u(0)",   math_negate_u(0), 0);

    // === Vec2 (both _1 raw and _2 struct variants) ===
    printf("--- Vec2 ---\n");

    Vec2 const v2a = { 3.0, 4.0 };
    Vec2 const v2b = { 1.0, 2.0 };
    FSize p2a[2] = { 3.0, 4.0 };
    FSize p2b[2] = { 1.0, 2.0 };
    FSize p2o[2] = DEFAULT_INITIALIZATION;

    Vec2 const v2sum = math_vec2_add_2(v2a, v2b);
    _check_f("vec2_add_2.x", v2sum.x, 4.0, _TOL);
    _check_f("vec2_add_2.y", v2sum.y, 6.0, _TOL);

    math_vec2_add_1(p2a, p2b, p2o);
    _check_f("vec2_add_1.x", p2o[0], 4.0, _TOL);

    Vec2 const v2diff = math_vec2_sub_2(v2a, v2b);
    _check_f("vec2_sub_2.x", v2diff.x, 2.0, _TOL);
    math_vec2_sub_1(p2a, p2b, p2o);
    _check_f("vec2_sub_1.y", p2o[1], 2.0, _TOL);

    Vec2 const v2scaled = math_vec2_scale_2(v2a, 2.0);
    _check_f("vec2_scale_2.x", v2scaled.x, 6.0, _TOL);
    math_vec2_scale_1(p2a, 2.0, p2o);
    _check_f("vec2_scale_1.y", p2o[1], 8.0, _TOL);

    _check_f("vec2_dot_2", math_vec2_dot_2(v2a, v2b), 11.0, _TOL);
    _check_f("vec2_dot_1", math_vec2_dot_1(p2a, p2b), 11.0, _TOL);
    _check_f("vec2_length_2", math_vec2_norm_2(v2a), 5.0, _TOL);
    _check_f("vec2_length_1", math_vec2_norm_1(p2a), 5.0, _TOL);
    // cglm computes in float, so an irrational distance needs a float-precision tol
    _check_f("vec2_distance_2", math_vec2_distance_2(v2a, v2b), math_sqrt_f(8.0), 1e-6);
    _check_f("vec2_distance_1", math_vec2_distance_1(p2a, p2b), math_sqrt_f(8.0), 1e-6);

    Vec2 const v2norm = math_vec2_normalize_2(v2a);
    _check_f("vec2_normalize_2 unit", math_vec2_norm_2(v2norm), 1.0, 1e-6);
    math_vec2_normalize_1(p2a, p2o);
    _check_f("vec2_normalize_1.x", p2o[0], 0.6, 1e-6);

    Vec2 const v2neg = math_vec2_negate_2(v2a);
    _check_f("vec2_negate_2.x", v2neg.x, -3.0, _TOL);
    math_vec2_negate_1(p2a, p2o);
    _check_f("vec2_negate_1.y", p2o[1], -4.0, _TOL);

    Vec2 const v2mid = math_vec2_lerp_2(v2b, v2a, 0.5);
    _check_f("vec2_lerp_2.x", v2mid.x, 2.0, _TOL);
    math_vec2_lerp_1(p2b, p2a, 0.5, p2o);
    _check_f("vec2_lerp_1.y", p2o[1], 3.0, _TOL);

    _check_b("vec2_equal_2 true", math_vec2_eqv_2(v2a, v2a), true);
    _check_b("vec2_equal_2 false", math_vec2_eqv_2(v2a, v2b), false);
    _check_b("vec2_equal_1 true", math_vec2_eqv_1(p2b, p2b), true);

    // === Vec3 (both _1 raw and _2 struct variants) ===
    printf("--- Vec3 ---\n");

    Vec3 const v3a = { 1.0, 2.0, 3.0 };
    Vec3 const v3b = { 4.0, 5.0, 6.0 };
    FSize p3a[3] = { 1.0, 2.0, 3.0 };
    FSize p3b[3] = { 4.0, 5.0, 6.0 };
    FSize p3o[3] = DEFAULT_INITIALIZATION;

    Vec3 const v3sum = math_vec3_add_2(v3a, v3b);
    _check_f("vec3_add_2.z", v3sum.z, 9.0, _TOL);
    math_vec3_add_1(p3a, p3b, p3o);
    _check_f("vec3_add_1.x", p3o[0], 5.0, _TOL);

    Vec3 const v3diff = math_vec3_sub_2(v3b, v3a);
    _check_f("vec3_sub_2.x", v3diff.x, 3.0, _TOL);
    math_vec3_sub_1(p3b, p3a, p3o);
    _check_f("vec3_sub_1.z", p3o[2], 3.0, _TOL);

    Vec3 const v3scaled = math_vec3_scale_2(v3a, 2.0);
    _check_f("vec3_scale_2.y", v3scaled.y, 4.0, _TOL);
    math_vec3_scale_1(p3a, 2.0, p3o);
    _check_f("vec3_scale_1.z", p3o[2], 6.0, _TOL);

    _check_f("vec3_dot_2", math_vec3_dot_2(v3a, v3b), 32.0, _TOL);
    _check_f("vec3_dot_1", math_vec3_dot_1(p3a, p3b), 32.0, _TOL);

    // cross of unit x and unit y is unit z
    Vec3 const ux = { 1.0, 0.0, 0.0 };
    Vec3 const uy = { 0.0, 1.0, 0.0 };
    Vec3 const v3cross = math_vec3_cross_2(ux, uy);
    _check_f("vec3_cross_2.z", v3cross.z, 1.0, _TOL);
    FSize pux[3] = { 1.0, 0.0, 0.0 };
    FSize puy[3] = { 0.0, 1.0, 0.0 };
    math_vec3_cross_1(pux, puy, p3o);
    _check_f("vec3_cross_1.z", p3o[2], 1.0, _TOL);

    Vec3 const v345 = { 3.0, 4.0, 0.0 };
    FSize p345[3] = { 3.0, 4.0, 0.0 };
    _check_f("vec3_length_2", math_vec3_norm_2(v345), 5.0, _TOL);
    _check_f("vec3_length_1", math_vec3_norm_1(p345), 5.0, _TOL);
    _check_f("vec3_distance_2", math_vec3_distance_2(v345, ux), math_sqrt_f(20.0), 1e-6);
    _check_f("vec3_distance_1", math_vec3_distance_1(p345, pux), math_sqrt_f(20.0), 1e-6);

    Vec3 const v3norm = math_vec3_normalize_2(v345);
    _check_f("vec3_normalize_2.x", v3norm.x, 0.6, 1e-6);

    // boundary: normalizing the zero vector yields zero (no division by zero)
    FSize pzero3[3] = DEFAULT_INITIALIZATION;
    FSize pzn3[3] = DEFAULT_INITIALIZATION;
    math_vec3_normalize_1(pzero3, pzn3);
    _check_f("vec3_normalize_1 zero", pzn3[0], 0.0, _TOL);

    Vec3 const v3neg = math_vec3_negate_2(v3a);
    _check_f("vec3_negate_2.y", v3neg.y, -2.0, _TOL);
    math_vec3_negate_1(p3a, p3o);
    _check_f("vec3_negate_1.x", p3o[0], -1.0, _TOL);

    Vec3 const v3mid = math_vec3_lerp_2(v3a, v3b, 0.5);
    _check_f("vec3_lerp_2.x", v3mid.x, 2.5, _TOL);
    math_vec3_lerp_1(p3a, p3b, 0.5, p3o);
    _check_f("vec3_lerp_1.z", p3o[2], 4.5, _TOL);

    _check_b("vec3_equal_2 true", math_vec3_eqv_2(v3a, v3a), true);
    _check_b("vec3_equal_2 false", math_vec3_eqv_2(v3a, v3b), false);
    _check_b("vec3_equal_1 true", math_vec3_eqv_1(p3a, p3a), true);

    // === Vec4 (both _1 raw and _2 struct variants) ===
    printf("--- Vec4 ---\n");

    Vec4 const v4a = { 1.0, 2.0, 3.0, 4.0 };
    Vec4 const v4b = { 5.0, 6.0, 7.0, 8.0 };
    FSize p4a[4] = { 1.0, 2.0, 3.0, 4.0 };
    FSize p4b[4] = { 5.0, 6.0, 7.0, 8.0 };
    FSize p4o[4] = DEFAULT_INITIALIZATION;

    Vec4 const v4sum = math_vec4_add_2(v4a, v4b);
    _check_f("vec4_add_2.w", v4sum.w, 12.0, _TOL);
    math_vec4_add_1(p4a, p4b, p4o);
    _check_f("vec4_add_1.x", p4o[0], 6.0, _TOL);

    Vec4 const v4diff = math_vec4_sub_2(v4b, v4a);
    _check_f("vec4_sub_2.w", v4diff.w, 4.0, _TOL);
    math_vec4_sub_1(p4b, p4a, p4o);
    _check_f("vec4_sub_1.x", p4o[0], 4.0, _TOL);

    Vec4 const v4scaled = math_vec4_scale_2(v4a, 3.0);
    _check_f("vec4_scale_2.z", v4scaled.z, 9.0, _TOL);
    math_vec4_scale_1(p4a, 3.0, p4o);
    _check_f("vec4_scale_1.w", p4o[3], 12.0, _TOL);

    _check_f("vec4_dot_2", math_vec4_dot_2(v4a, v4b), 70.0, _TOL);
    _check_f("vec4_dot_1", math_vec4_dot_1(p4a, p4b), 70.0, _TOL);

    Vec4 const v4l = { 2.0, 0.0, 0.0, 0.0 };
    FSize p4l[4] = { 2.0, 0.0, 0.0, 0.0 };
    _check_f("vec4_length_2", math_vec4_norm_2(v4l), 2.0, _TOL);
    _check_f("vec4_length_1", math_vec4_norm_1(p4l), 2.0, _TOL);
    _check_f("vec4_distance_2", math_vec4_distance_2(v4a, v4b), math_sqrt_f(64.0), _TOL);
    _check_f("vec4_distance_1", math_vec4_distance_1(p4a, p4b), math_sqrt_f(64.0), _TOL);

    Vec4 const v4norm = math_vec4_normalize_2(v4l);
    _check_f("vec4_normalize_2.x", v4norm.x, 1.0, 1e-6);
    math_vec4_normalize_1(p4l, p4o);
    _check_f("vec4_normalize_1.x", p4o[0], 1.0, 1e-6);

    Vec4 const v4neg = math_vec4_negate_2(v4a);
    _check_f("vec4_negate_2.w", v4neg.w, -4.0, _TOL);
    math_vec4_negate_1(p4a, p4o);
    _check_f("vec4_negate_1.z", p4o[2], -3.0, _TOL);

    Vec4 const v4mid = math_vec4_lerp_2(v4a, v4b, 0.5);
    _check_f("vec4_lerp_2.x", v4mid.x, 3.0, _TOL);
    math_vec4_lerp_1(p4a, p4b, 0.5, p4o);
    _check_f("vec4_lerp_1.w", p4o[3], 6.0, _TOL);

    _check_b("vec4_equal_2 true", math_vec4_eqv_2(v4a, v4a), true);
    _check_b("vec4_equal_2 false", math_vec4_eqv_2(v4a, v4b), false);
    _check_b("vec4_equal_1 true", math_vec4_eqv_1(p4a, p4a), true);

    // === Mat4 (both _1 raw and _2 struct variants) ===
    printf("--- Mat4 ---\n");

    Mat4 const identity = math_mat4_identity_2();
    _check_f("mat4_identity_2 diag", identity.m[0][0], 1.0, _TOL);
    _check_f("mat4_identity_2 off", identity.m[0][1], 0.0, _TOL);

    FSize pid[16] = DEFAULT_INITIALIZATION;
    math_mat4_identity_1(pid);
    _check_f("mat4_identity_1 [0]", pid[0], 1.0, _TOL);
    _check_f("mat4_identity_1 [5]", pid[5], 1.0, _TOL);
    _check_f("mat4_identity_1 [1]", pid[1], 0.0, _TOL);

    // translate: column-major stores the offset in the 4th column (indices 12..14)
    Vec3 const offset = { 1.0, 2.0, 3.0 };
    Mat4 const trans = math_affine_translate_2(identity, offset);
    _check_f("mat4_translate_2 [3][0]", trans.m[3][0], 1.0, _TOL);
    _check_f("mat4_translate_2 [3][2]", trans.m[3][2], 3.0, _TOL);

    FSize poff[3] = { 1.0, 2.0, 3.0 };
    FSize ptrans[16] = DEFAULT_INITIALIZATION;
    math_affine_translate_1(pid, poff, ptrans);
    _check_f("mat4_translate_1 [12]", ptrans[12], 1.0, _TOL);
    _check_f("mat4_translate_1 [14]", ptrans[14], 3.0, _TOL);

    // inverse(A) * A ~= I
    Mat4 const inv = math_mat4_inv_2(trans);
    Mat4 const prod = math_mat4_mul_2(inv, trans);
    _check_f("mat4 inv*A [0][0]", prod.m[0][0], 1.0, 1e-6);
    _check_f("mat4 inv*A [1][1]", prod.m[1][1], 1.0, 1e-6);
    _check_f("mat4 inv*A [3][0]", prod.m[3][0], 0.0, 1e-6);

    FSize pinv[16] = DEFAULT_INITIALIZATION;
    FSize pprod[16] = DEFAULT_INITIALIZATION;
    math_mat4_inv_1(ptrans, pinv);
    math_mat4_mul_1(pinv, ptrans, pprod);
    _check_f("mat4 inv*A_1 [0]", pprod[0], 1.0, 1e-6);
    _check_f("mat4 inv*A_1 [12]", pprod[12], 0.0, 1e-6);

    // transpose(transpose(A)) == A
    Mat4 const tt = math_mat4_transpose_2(math_mat4_transpose_2(trans));
    _check_f("mat4 transpose^2 [3][0]", tt.m[3][0], 1.0, _TOL);
    FSize ptt[16] = DEFAULT_INITIALIZATION;
    math_mat4_transpose_1(ptrans, ptt);
    // transposing the translation matrix moves col-3 offset into row-3 (index 3)
    _check_f("mat4_transpose_1 [3]", ptt[3], 1.0, _TOL);

    // scale places factors on the diagonal
    Vec3 const factors = { 2.0, 3.0, 4.0 };
    Mat4 const scaled = math_affine_scale_2(identity, factors);
    _check_f("mat4_scale_2 [0][0]", scaled.m[0][0], 2.0, _TOL);
    _check_f("mat4_scale_2 [2][2]", scaled.m[2][2], 4.0, _TOL);
    FSize pfac[3] = { 2.0, 3.0, 4.0 };
    FSize pscaled[16] = DEFAULT_INITIALIZATION;
    math_affine_scale_1(pid, pfac, pscaled);
    _check_f("mat4_scale_1 [5]", pscaled[5], 3.0, _TOL);

    // rotate identity by 90 degrees about z: m[0][0] == cos(90) == 0
    Vec3 const zaxis = { 0.0, 0.0, 1.0 };
    Mat4 const rot = math_affine_rotate_2(identity, MATH_PI / 2.0, zaxis);
    _check_f("mat4_rotate_2 cos", rot.m[0][0], 0.0, 1e-6);
    FSize pz[3] = { 0.0, 0.0, 1.0 };
    FSize prot[16] = DEFAULT_INITIALIZATION;
    math_affine_rotate_1(pid, MATH_PI / 2.0, pz, prot);
    _check_f("mat4_rotate_1 cos", prot[0], 0.0, 1e-6);

    // perspective produces a positive focal term at m[0][0]
    Mat4 const persp = math_cam_perspective_2(MATH_PI / 3.0, 1.5, 0.1, 100.0);
    _check_b("mat4_perspective_2 [0][0]>0", persp.m[0][0] > 0.0, true);
    FSize ppersp[16] = DEFAULT_INITIALIZATION;
    math_cam_perspective_1(MATH_PI / 3.0, 1.5, 0.1, 100.0, ppersp);
    _check_b("mat4_perspective_1 [0]>0", ppersp[0] > 0.0, true);

    // look_at from (0,0,5) toward origin: right-vector x component is 1
    Vec3 const eye = { 0.0, 0.0, 5.0 };
    Vec3 const center = { 0.0, 0.0, 0.0 };
    Vec3 const up = { 0.0, 1.0, 0.0 };
    Mat4 const view = math_cam_lookat_2(eye, center, up);
    _check_f("mat4_look_at_2 [0][0]", view.m[0][0], 1.0, 1e-6);
    FSize peye[3] = { 0.0, 0.0, 5.0 };
    FSize pcenter[3] = { 0.0, 0.0, 0.0 };
    FSize pup[3] = { 0.0, 1.0, 0.0 };
    FSize pview[16] = DEFAULT_INITIALIZATION;
    math_cam_lookat_1(peye, pcenter, pup, pview);
    _check_f("mat4_look_at_1 [0]", pview[0], 1.0, 1e-6);

    // === Quat (both _1 raw and _2 struct variants) ===
    printf("--- Quat ---\n");

    Quat const qid = math_quat_identity_2();
    _check_f("quat_identity_2.w", qid.w, 1.0, _TOL);
    _check_f("quat_identity_2.x", qid.x, 0.0, _TOL);
    FSize pqid[4] = DEFAULT_INITIALIZATION;
    math_quat_identity_1(pqid);
    _check_f("quat_identity_1.w", pqid[3], 1.0, _TOL);

    // rotate unit x by 90 degrees about z yields unit y
    Quat const q90 = math_quat_quatv_2(MATH_PI / 2.0, zaxis);
    Vec3 const rotated = math_quat_rotatev_2(q90, ux);
    _check_f("quat rotate x->y .x", rotated.x, 0.0, 1e-6);
    _check_f("quat rotate x->y .y", rotated.y, 1.0, 1e-6);

    FSize pq90[4] = DEFAULT_INITIALIZATION;
    FSize prv[3] = DEFAULT_INITIALIZATION;
    math_quat_quatv_1(MATH_PI / 2.0, pz, pq90);
    math_quat_rotatev_1(pq90, pux, prv);
    _check_f("quat rotate_1 x->y .y", prv[1], 1.0, 1e-6);

    // normalize keeps unit length
    Quat const qn = math_quat_normalize_2(q90);
    FSize const qlen = math_sqrt_f(qn.x * qn.x + qn.y * qn.y + qn.z * qn.z + qn.w * qn.w);
    _check_f("quat_normalize_2 unit", qlen, 1.0, 1e-6);
    FSize pqn[4] = DEFAULT_INITIALIZATION;
    math_quat_normalize_1(pq90, pqn);
    FSize const pqlen = math_sqrt_f(pqn[0]*pqn[0] + pqn[1]*pqn[1] + pqn[2]*pqn[2] + pqn[3]*pqn[3]);
    _check_f("quat_normalize_1 unit", pqlen, 1.0, 1e-6);

    // identity * q == q
    Quat const qmul = math_quat_mul_2(qid, q90);
    _check_f("quat_mul_2 identity .z", qmul.z, q90.z, 1e-6);
    FSize pqmul[4] = DEFAULT_INITIALIZATION;
    math_quat_mul_1(pqid, pq90, pqmul);
    _check_f("quat_mul_1 identity .z", pqmul[2], pq90[2], 1e-6);

    // slerp endpoints return the endpoints
    Quat const qs0 = math_quat_slerp_2(qid, q90, 0.0);
    _check_f("quat_slerp_2 t=0 .w", qs0.w, 1.0, 1e-6);
    FSize pqs[4] = DEFAULT_INITIALIZATION;
    math_quat_slerp_1(pqid, pq90, 1.0, pqs);
    _check_f("quat_slerp_1 t=1 .z", pqs[2], pq90[2], 1e-6);

    return _check_finish();
}