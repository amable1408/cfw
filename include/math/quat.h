/*
 * quat.h - Quaternion operations for the CFW math module
 *
 * Features:
 *   - Full coverage of cglm's compiled glmc_quat* API: construction, geometry
 *     (norm/dot/real/imag/angle/axis), arithmetic, conjugate/inverse, matrix
 *     conversion, interpolation (lerp/nlerp/slerp), look/for orientation, and
 *     vector/matrix rotation
 *   - Root-first variants: _1 raw FSize-array underset, _2 framework-struct superset
 *   - Pure-value semantics: no wrapper mutates its argument in place; the raw (_1)
 *     variant writes a caller-supplied destination, the struct (_2) variant returns
 *     a Quat (or a Vec3/Mat3/Mat4/FSize for cross-type/scalar results)
 *
 * Usage Examples:
 *   @code
 *   Vec3 const axis = { 0.0, 0.0, 1.0 };
 *   Quat const q    = math_quat_quatv_2(MATH_PI / 2.0, axis);
 *   Vec3 const rot  = math_quat_rotatev_2(q, (Vec3) { 1.0, 0.0, 0.0 });
 *   @endcode
 *
 * Error Handling:
 *   - Pointer (_1) variants validate every pointer with error_check_null.
 *   - Struct (_2) variants take values, so there is no pointer to validate.
 *
 * Thread Safety:
 *   - All functions are pure and thread-safe; no shared state is touched.
 *
 * Memory Management:
 *   - No allocation is performed; every function operates on values only.
 *
 * Performance Characteristics:
 *   - Each wrapper converts FSize<->float at the boundary and calls a compiled
 *     glmc_* routine. cglm's quat routines are scalar C behind a non-inlinable call -
 *     only its vec4/mat4 paths are SIMD-accelerated.
 *   - Precision: every value crosses F64 -> float -> F64; see the Precision section
 *     in types.h for what that loses and where it flips a result.
 *
 * Dependencies:
 *   - <math/types.h> for the Quat/Vec3/Mat3/Mat4 types, the raw<->cglm bridges,
 *     cglm, and the error/tracing macros.
 *
 * See quat.c for implementation details.
 */

#ifndef MATH_QUAT_H
#define MATH_QUAT_H

#include <math/types.h>

/*==============================================================================
 * MARK: - Quat API
 *
 * Raw (_1) variants read and write 4 contiguous FSize as (x, y, z, w); the
 * struct (_2) variants read and return a Quat value. Cross-type ops take or
 * return Vec3/Mat3/Mat4 per the operation; scalar ops return FSize. The two
 * axis-angle constructors keep cglm's bare symbol names: quat (angle plus x/y/z
 * components) and quatv (angle plus an axis vector).
 *============================================================================*/

/**
 * @brief Add two raw quaternions component-wise (a + b).
 * @param a Raw left quaternion (4 contiguous FSize).
 * @param b Raw right quaternion (4 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize (x, y, z, w).
 */
void math_quat_add_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the component-wise sum of two quaternions (a + b).
 * @param a Left quaternion.
 * @param b Right quaternion.
 * @return Sum Quat.
 */
Quat math_quat_add_2(Quat const a, Quat const b);

/**
 * @brief Rotation angle encoded by a raw quaternion.
 * @param q Raw quaternion (4 contiguous FSize).
 * @return Angle in radians as FSize.
 */
FSize math_quat_angle_1(FSize const *const q);

/**
 * @brief Rotation angle encoded by a quaternion.
 * @param q Source quaternion.
 * @return Angle in radians as FSize.
 */
FSize math_quat_angle_2(Quat const q);

/**
 * @brief Rotation axis of a raw quaternion.
 * @param q Raw quaternion (4 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_quat_axis_1(FSize const *const q, FSize *const dest);

/**
 * @brief Return the rotation axis of a quaternion.
 * @param q Source quaternion.
 * @return Axis Vec3.
 */
Vec3 math_quat_axis_2(Quat const q);

/**
 * @brief Conjugate a raw quaternion (negate the imaginary part).
 * @param q Raw quaternion (4 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize (x, y, z, w).
 */
void math_quat_conjugate_1(FSize const *const q, FSize *const dest);

/**
 * @brief Return the conjugate of a quaternion (negate the imaginary part).
 * @param q Source quaternion.
 * @return Conjugate Quat.
 */
Quat math_quat_conjugate_2(Quat const q);

/**
 * @brief Copy a raw quaternion.
 * @param q Raw source quaternion (4 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize (x, y, z, w).
 */
void math_quat_copy_1(FSize const *const q, FSize *const dest);

/**
 * @brief Return a copy of a quaternion.
 * @param q Source quaternion.
 * @return Copied Quat.
 */
Quat math_quat_copy_2(Quat const q);

/**
 * @brief Dot product of two raw quaternions.
 * @param p Raw first quaternion (4 contiguous FSize).
 * @param q Raw second quaternion (4 contiguous FSize).
 * @return Dot product as FSize.
 */
FSize math_quat_dot_1(FSize const *const p, FSize const *const q);

/**
 * @brief Dot product of two quaternions.
 * @param p First quaternion.
 * @param q Second quaternion.
 * @return Dot product as FSize.
 */
FSize math_quat_dot_2(Quat const p, Quat const q);

/**
 * @brief Build a raw look-rotation quaternion from a raw direction and up vector.
 * @param dir Raw forward direction (3 contiguous FSize).
 * @param up Raw up vector (3 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize (x, y, z, w).
 */
void math_quat_for_1(FSize const *const dir, FSize const *const up, FSize *const dest);

/**
 * @brief Return a look-rotation quaternion from a direction and up vector.
 * @param dir Forward direction.
 * @param up Up vector.
 * @return Orientation Quat.
 */
Quat math_quat_for_2(Vec3 const dir, Vec3 const up);

/**
 * @brief Build a raw look-rotation quaternion from raw from/to positions and up.
 * @param from Raw source position (3 contiguous FSize).
 * @param to Raw target position (3 contiguous FSize).
 * @param up Raw up vector (3 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize (x, y, z, w).
 */
void math_quat_forp_1(FSize const *const from, FSize const *const to, FSize const *const up, FSize *const dest);

/**
 * @brief Return a look-rotation quaternion from from/to positions and up.
 * @param from Source position.
 * @param to Target position.
 * @param up Up vector.
 * @return Orientation Quat.
 */
Quat math_quat_forp_2(Vec3 const from, Vec3 const to, Vec3 const up);

/**
 * @brief Build a raw quaternion rotating raw vector a onto raw vector b.
 * @param a Raw source vector (3 contiguous FSize).
 * @param b Raw target vector (3 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize (x, y, z, w).
 */
void math_quat_from_vecs_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return a quaternion rotating vector a onto vector b.
 * @param a Source vector.
 * @param b Target vector.
 * @return Rotation Quat.
 */
Quat math_quat_from_vecs_2(Vec3 const a, Vec3 const b);

/**
 * @brief Write the identity quaternion into a raw destination.
 * @param dest Destination of 4 contiguous FSize (x, y, z, w).
 */
void math_quat_identity_1(FSize *const dest);

/**
 * @brief Return the identity quaternion.
 * @return Identity Quat.
 */
Quat math_quat_identity_2(void);

/**
 * @brief Imaginary (vector) part of a raw quaternion.
 * @param q Raw quaternion (4 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_quat_imag_1(FSize const *const q, FSize *const dest);

/**
 * @brief Return the imaginary (vector) part of a quaternion.
 * @param q Source quaternion.
 * @return Imaginary Vec3.
 */
Vec3 math_quat_imag_2(Quat const q);

/**
 * @brief Length of the imaginary (vector) part of a raw quaternion.
 * @param q Raw quaternion (4 contiguous FSize).
 * @return Imaginary length as FSize.
 */
FSize math_quat_imaglen_1(FSize const *const q);

/**
 * @brief Length of the imaginary (vector) part of a quaternion.
 * @param q Source quaternion.
 * @return Imaginary length as FSize.
 */
FSize math_quat_imaglen_2(Quat const q);

/**
 * @brief Normalized imaginary (vector) part of a raw quaternion.
 * @param q Raw quaternion (4 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_quat_imagn_1(FSize const *const q, FSize *const dest);

/**
 * @brief Return the normalized imaginary (vector) part of a quaternion.
 * @param q Source quaternion.
 * @return Unit imaginary Vec3.
 */
Vec3 math_quat_imagn_2(Quat const q);

/**
 * @brief Build a raw quaternion from explicit components.
 * @param x Imaginary x component.
 * @param y Imaginary y component.
 * @param z Imaginary z component.
 * @param w Real (scalar) component.
 * @param dest Destination of 4 contiguous FSize (x, y, z, w).
 */
void math_quat_init_1(FSize const x, FSize const y, FSize const z, FSize const w, FSize *const dest);

/**
 * @brief Return a quaternion built from explicit components.
 * @param x Imaginary x component.
 * @param y Imaginary y component.
 * @param z Imaginary z component.
 * @param w Real (scalar) component.
 * @return Constructed Quat.
 */
Quat math_quat_init_2(FSize const x, FSize const y, FSize const z, FSize const w);

/**
 * @brief Invert a raw quaternion.
 * @param q Raw quaternion (4 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize (x, y, z, w).
 */
void math_quat_inv_1(FSize const *const q, FSize *const dest);

/**
 * @brief Return the inverse of a quaternion.
 * @param q Source quaternion.
 * @return Inverse Quat.
 */
Quat math_quat_inv_2(Quat const q);

/**
 * @brief Linearly interpolate between two raw quaternions.
 * @param from Raw start quaternion (4 contiguous FSize).
 * @param to Raw end quaternion (4 contiguous FSize).
 * @param t Interpolation factor in [0, 1].
 * @param dest Destination of 4 contiguous FSize (x, y, z, w).
 */
void math_quat_lerp_1(FSize const *const from, FSize const *const to, FSize const t, FSize *const dest);

/**
 * @brief Return the linear interpolation between two quaternions.
 * @param from Start quaternion.
 * @param to End quaternion.
 * @param t Interpolation factor in [0, 1].
 * @return Interpolated Quat.
 */
Quat math_quat_lerp_2(Quat const from, Quat const to, FSize const t);

/**
 * @brief Clamped linear interpolation between two raw quaternions (t in [0, 1]).
 * @param from Raw start quaternion (4 contiguous FSize).
 * @param to Raw end quaternion (4 contiguous FSize).
 * @param t Interpolation factor, clamped into [0, 1].
 * @param dest Destination of 4 contiguous FSize (x, y, z, w).
 */
void math_quat_lerpc_1(FSize const *const from, FSize const *const to, FSize const t, FSize *const dest);

/**
 * @brief Return the clamped linear interpolation between two quaternions.
 * @param from Start quaternion.
 * @param to End quaternion.
 * @param t Interpolation factor, clamped into [0, 1].
 * @return Interpolated Quat.
 */
Quat math_quat_lerpc_2(Quat const from, Quat const to, FSize const t);

/**
 * @brief Build a raw view matrix looking from an eye position with a raw orientation.
 * @param eye Raw eye position (3 contiguous FSize).
 * @param ori Raw orientation quaternion (4 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major 4x4).
 */
void math_quat_look_1(FSize const *const eye, FSize const *const ori, FSize *const dest);

/**
 * @brief Return a view matrix looking from an eye position with an orientation.
 * @param eye Eye position.
 * @param ori Orientation quaternion.
 * @return View Mat4.
 */
Mat4 math_quat_look_2(Vec3 const eye, Quat const ori);

/**
 * @brief Construct a raw quaternion by copying a raw FSize source array.
 * @param src Raw source array (4 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize (x, y, z, w).
 */
void math_quat_make_1(FSize const *const src, FSize *const dest);

/**
 * @brief Construct a quaternion by copying a raw FSize source array.
 * @param src Raw source array (4 contiguous FSize).
 * @return Constructed Quat.
 */
Quat math_quat_make_2(FSize const *const src);

/**
 * @brief Convert a raw quaternion to a raw 3x3 rotation matrix.
 * @param q Raw quaternion (4 contiguous FSize).
 * @param dest Destination of 9 contiguous FSize (column-major 3x3).
 */
void math_quat_mat3_1(FSize const *const q, FSize *const dest);

/**
 * @brief Return the 3x3 rotation matrix of a quaternion.
 * @param q Source quaternion.
 * @return Rotation Mat3.
 */
Mat3 math_quat_mat3_2(Quat const q);

/**
 * @brief Convert a raw quaternion to a raw transposed 3x3 rotation matrix.
 * @param q Raw quaternion (4 contiguous FSize).
 * @param dest Destination of 9 contiguous FSize (column-major 3x3).
 */
void math_quat_mat3t_1(FSize const *const q, FSize *const dest);

/**
 * @brief Return the transposed 3x3 rotation matrix of a quaternion.
 * @param q Source quaternion.
 * @return Transposed rotation Mat3.
 */
Mat3 math_quat_mat3t_2(Quat const q);

/**
 * @brief Convert a raw quaternion to a raw 4x4 rotation matrix.
 * @param q Raw quaternion (4 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major 4x4).
 */
void math_quat_mat4_1(FSize const *const q, FSize *const dest);

/**
 * @brief Return the 4x4 rotation matrix of a quaternion.
 * @param q Source quaternion.
 * @return Rotation Mat4.
 */
Mat4 math_quat_mat4_2(Quat const q);

/**
 * @brief Convert a raw quaternion to a raw transposed 4x4 rotation matrix.
 * @param q Raw quaternion (4 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major 4x4).
 */
void math_quat_mat4t_1(FSize const *const q, FSize *const dest);

/**
 * @brief Return the transposed 4x4 rotation matrix of a quaternion.
 * @param q Source quaternion.
 * @return Transposed rotation Mat4.
 */
Mat4 math_quat_mat4t_2(Quat const q);

/**
 * @brief Multiply two raw quaternions (a * b).
 * @param a Raw left quaternion (4 contiguous FSize).
 * @param b Raw right quaternion (4 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize (x, y, z, w).
 */
void math_quat_mul_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the product of two quaternions (a * b).
 * @param a Left quaternion.
 * @param b Right quaternion.
 * @return Product Quat.
 */
Quat math_quat_mul_2(Quat const a, Quat const b);

/**
 * @brief Normalized linear interpolation between two raw quaternions.
 * @param q Raw start quaternion (4 contiguous FSize).
 * @param r Raw end quaternion (4 contiguous FSize).
 * @param t Interpolation factor in [0, 1].
 * @param dest Destination of 4 contiguous FSize (x, y, z, w).
 */
void math_quat_nlerp_1(FSize const *const q, FSize const *const r, FSize const t, FSize *const dest);

/**
 * @brief Return the normalized linear interpolation between two quaternions.
 * @param q Start quaternion.
 * @param r End quaternion.
 * @param t Interpolation factor in [0, 1].
 * @return Interpolated unit Quat.
 */
Quat math_quat_nlerp_2(Quat const q, Quat const r, FSize const t);

/**
 * @brief Euclidean norm (length) of a raw quaternion.
 * @param q Raw quaternion (4 contiguous FSize).
 * @return Norm as FSize.
 */
FSize math_quat_norm_1(FSize const *const q);

/**
 * @brief Euclidean norm (length) of a quaternion.
 * @param q Source quaternion.
 * @return Norm as FSize.
 */
FSize math_quat_norm_2(Quat const q);

/**
 * @brief Normalize a raw quaternion.
 * @param q Raw quaternion (4 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize (x, y, z, w).
 */
void math_quat_normalize_1(FSize const *const q, FSize *const dest);

/**
 * @brief Return the normalized quaternion.
 * @param q Source quaternion.
 * @return Unit Quat.
 */
Quat math_quat_normalize_2(Quat const q);

/**
 * @brief Build a raw quaternion from an angle and explicit axis components.
 * @param angle Rotation angle in radians.
 * @param x Axis x component.
 * @param y Axis y component.
 * @param z Axis z component.
 * @param dest Destination of 4 contiguous FSize (x, y, z, w).
 */
void math_quat_quat_1(FSize const angle, FSize const x, FSize const y, FSize const z, FSize *const dest);

/**
 * @brief Return a quaternion built from an angle and explicit axis components.
 * @param angle Rotation angle in radians.
 * @param x Axis x component.
 * @param y Axis y component.
 * @param z Axis z component.
 * @return Rotation Quat.
 */
Quat math_quat_quat_2(FSize const angle, FSize const x, FSize const y, FSize const z);

/**
 * @brief Build a raw quaternion from an angle and a raw axis vector.
 * @param angle Rotation angle in radians.
 * @param axis Raw rotation axis (3 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize (x, y, z, w).
 */
void math_quat_quatv_1(FSize const angle, FSize const *const axis, FSize *const dest);

/**
 * @brief Return a quaternion built from an angle and an axis vector.
 * @param angle Rotation angle in radians.
 * @param axis Rotation axis.
 * @return Rotation Quat.
 */
Quat math_quat_quatv_2(FSize const angle, Vec3 const axis);

/**
 * @brief Real (scalar) part of a raw quaternion.
 * @param q Raw quaternion (4 contiguous FSize).
 * @return Real part as FSize.
 */
FSize math_quat_real_1(FSize const *const q);

/**
 * @brief Real (scalar) part of a quaternion.
 * @param q Source quaternion.
 * @return Real part as FSize.
 */
FSize math_quat_real_2(Quat const q);

/**
 * @brief Apply a raw quaternion rotation to a raw 4x4 model matrix (m * R).
 * @param m Raw model matrix (16 contiguous FSize, column-major 4x4).
 * @param q Raw rotation quaternion (4 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major 4x4).
 */
void math_quat_rotate_1(FSize const *const m, FSize const *const q, FSize *const dest);

/**
 * @brief Return a 4x4 model matrix with a quaternion rotation applied (m * R).
 * @param m Model matrix.
 * @param q Rotation quaternion.
 * @return Rotated Mat4.
 */
Mat4 math_quat_rotate_2(Mat4 const m, Quat const q);

/**
 * @brief Rotate a raw 4x4 model matrix about a raw pivot by a raw quaternion.
 * @param m Raw model matrix (16 contiguous FSize, column-major 4x4).
 * @param q Raw rotation quaternion (4 contiguous FSize).
 * @param pivot Raw pivot point (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major 4x4).
 */
void math_quat_rotate_at_1(FSize const *const m, FSize const *const q, FSize const *const pivot, FSize *const dest);

/**
 * @brief Return a 4x4 model matrix rotated about a pivot by a quaternion.
 * @param m Model matrix.
 * @param q Rotation quaternion.
 * @param pivot Pivot point.
 * @return Rotated Mat4.
 */
Mat4 math_quat_rotate_at_2(Mat4 const m, Quat const q, Vec3 const pivot);

/**
 * @brief Build a raw pivot-rotation matrix from a raw quaternion and pivot.
 * @param m Raw base matrix (16 contiguous FSize, column-major 4x4).
 * @param q Raw rotation quaternion (4 contiguous FSize).
 * @param pivot Raw pivot point (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major 4x4).
 */
void math_quat_rotate_atm_1(FSize const *const m, FSize const *const q, FSize const *const pivot, FSize *const dest);

/**
 * @brief Return a pivot-rotation matrix built from a quaternion and pivot.
 * @param m Base matrix.
 * @param q Rotation quaternion.
 * @param pivot Pivot point.
 * @return Rotation Mat4.
 */
Mat4 math_quat_rotate_atm_2(Mat4 const m, Quat const q, Vec3 const pivot);

/**
 * @brief Rotate a raw 3D vector by a raw quaternion.
 * @param q Raw rotation quaternion (4 contiguous FSize).
 * @param v Raw vector to rotate (3 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_quat_rotatev_1(FSize const *const q, FSize const *const v, FSize *const dest);

/**
 * @brief Return a 3D vector rotated by a quaternion.
 * @param q Rotation quaternion.
 * @param v Vector to rotate.
 * @return Rotated Vec3.
 */
Vec3 math_quat_rotatev_2(Quat const q, Vec3 const v);

/**
 * @brief Spherically interpolate between two raw quaternions.
 * @param q Raw start quaternion (4 contiguous FSize).
 * @param r Raw end quaternion (4 contiguous FSize).
 * @param t Interpolation factor in [0, 1].
 * @param dest Destination of 4 contiguous FSize (x, y, z, w).
 */
void math_quat_slerp_1(FSize const *const q, FSize const *const r, FSize const t, FSize *const dest);

/**
 * @brief Return the spherical interpolation between two quaternions.
 * @param q Start quaternion.
 * @param r End quaternion.
 * @param t Interpolation factor in [0, 1].
 * @return Interpolated Quat.
 */
Quat math_quat_slerp_2(Quat const q, Quat const r, FSize const t);

/**
 * @brief Spherically interpolate two raw quaternions along the longest arc.
 * @param q Raw start quaternion (4 contiguous FSize).
 * @param r Raw end quaternion (4 contiguous FSize).
 * @param t Interpolation factor in [0, 1].
 * @param dest Destination of 4 contiguous FSize (x, y, z, w).
 */
void math_quat_slerp_longest_1(FSize const *const q, FSize const *const r, FSize const t, FSize *const dest);

/**
 * @brief Return the longest-arc spherical interpolation between two quaternions.
 * @param q Start quaternion.
 * @param r End quaternion.
 * @param t Interpolation factor in [0, 1].
 * @return Interpolated Quat.
 */
Quat math_quat_slerp_longest_2(Quat const q, Quat const r, FSize const t);

/**
 * @brief Subtract two raw quaternions component-wise (a - b).
 * @param a Raw left quaternion (4 contiguous FSize).
 * @param b Raw right quaternion (4 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize (x, y, z, w).
 */
void math_quat_sub_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the component-wise difference of two quaternions (a - b).
 * @param a Left quaternion.
 * @param b Right quaternion.
 * @return Difference Quat.
 */
Quat math_quat_sub_2(Quat const a, Quat const b);

#endif // MATH_QUAT_H