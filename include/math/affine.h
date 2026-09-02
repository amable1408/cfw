/*
 * affine.h - Affine 4x4 transforms for the CFW math module
 *
 * Features:
 *   - Full coverage of cglm's compiled affine API (the bare glmc_ prefix:
 *     glmc_translate*, glmc_scale*, glmc_rotate*, glmc_spin*, glmc_decompose*,
 *     glmc_uniscaled, glmc_mul, glmc_mul_rot, glmc_inv_tr, and the affine-post
 *     glmc_translated, glmc_rotated, and glmc_spinned variants)
 *   - Root-first variants: _1 raw FSize-array underset, _2 framework-struct superset
 *   - Pure-value semantics: cglm's affine ops mutate their matrix in place; every
 *     wrapper here is pure instead - the raw (_1) variant writes a caller-supplied
 *     destination, the struct (_2) variant returns a fresh Mat4 (or Vec3/bool for
 *     cross-type/scalar results). In-place and matching _to pairs are collapsed
 *     into one pure op named after the base (translate, scale, translated).
 *   - Producers (*_make, rotate_atm) build a fresh transform from parameters only,
 *     with no source matrix argument.
 *
 * Usage Examples:
 *   @code
 *   Vec3 const offset = { 1.0, 0.0, 0.0 };
 *   Mat4 const moved  = math_affine_translate_2(math_mat4_identity_2(), offset);
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
 *     glmc_* routine. cglm's affine routines are scalar C behind a non-inlinable call -
 *     only its vec4/mat4 paths are SIMD-accelerated.
 *   - Precision: every value crosses F64 -> float -> F64; see the Precision section
 *     in types.h for what that loses and where it flips a result.
 *
 * Dependencies:
 *   - <math/types.h> for the Mat4/Vec3/Vec4 types, the raw<->cglm bridges, cglm,
 *     and the error/tracing macros.
 *
 * See affine.c for implementation details.
 */

#ifndef MATH_AFFINE_H
#define MATH_AFFINE_H

#include <math/types.h>

/*==============================================================================
 * MARK: - Affine API
 *
 * Raw (_1) variants read and write 16 contiguous FSize in column-major order; the
 * struct (_2) variants read and return a Mat4 value. Cross-type ops use the
 * matching raw length (3 for a vec3, 4 for a vec4) in the _1 form and the matching
 * struct (Vec3/Vec4) in the _2 form.
 *
 * decompose_rs and decompose emit several heterogeneous outputs at once (a
 * rotation matrix plus a scale vector, and additionally a translation vector), so
 * they are provided in the _1 out-pointer form only: a single struct return cannot
 * carry them and multiple out-pointers is the clearest interface. Every other op
 * offers both variants.
 *============================================================================*/

/**
 * @brief Decompose a raw affine transform into translation, rotation, and scale.
 * @param mat Raw source transform (16 contiguous FSize, column-major).
 * @param translation Destination translation vector (4 contiguous FSize).
 * @param rotation Destination rotation matrix (16 contiguous FSize, column-major).
 * @param scale Destination scale vector (3 contiguous FSize).
 */
void math_affine_decompose_1(FSize const *const mat, FSize *const translation, FSize *const rotation, FSize *const scale);

/**
 * @brief Decompose a raw affine transform into rotation and scale (no translation).
 *
 * Formerly misnamed math_affine_decompose_2: it wraps glmc_decompose_rs, a DIFFERENT
 * operation from decompose_1's glmc_decompose, and it is a raw-pointer (_1) form - there is
 * no struct form, because it has two outputs.
 * @param mat Raw source transform (16 contiguous FSize, column-major).
 * @param rotation Destination rotation matrix (16 contiguous FSize, column-major).
 * @param scale Destination scale vector (3 contiguous FSize).
 */
void math_affine_decompose_rs_1(FSize const *const mat, FSize *const rotation, FSize *const scale);

/**
 * @brief Extract the per-axis scale of a raw affine transform.
 * @param mat Raw source transform (16 contiguous FSize, column-major).
 * @param scale Destination scale vector (3 contiguous FSize).
 */
void math_affine_decompose_scalev_1(FSize const *const mat, FSize *const scale);

/**
 * @brief Return the per-axis scale of an affine transform.
 * @param mat Source transform.
 * @return Scale Vec3.
 */
Vec3 math_affine_decompose_scalev_2(Mat4 const mat);

/**
 * @brief Invert a raw affine transform (rotation + translation) in the fast path.
 * @param mat Raw source transform (16 contiguous FSize, column-major).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_affine_inv_tr_1(FSize const *const mat, FSize *const dest);

/**
 * @brief Return the inverse of an affine transform (rotation + translation).
 * @param mat Source transform.
 * @return Inverse Mat4.
 */
Mat4 math_affine_inv_tr_2(Mat4 const mat);

/**
 * @brief Multiply two raw affine transforms (a * b), affine-optimized.
 * @param a Raw left transform (16 contiguous FSize, column-major).
 * @param b Raw right transform (16 contiguous FSize, column-major).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_affine_mul_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the product of two affine transforms (a * b), affine-optimized.
 * @param a Left transform.
 * @param b Right transform.
 * @return Product Mat4.
 */
Mat4 math_affine_mul_2(Mat4 const a, Mat4 const b);

/**
 * @brief Multiply two raw rotation-only transforms (a * b), rotation-optimized.
 * @param a Raw left transform (16 contiguous FSize, column-major).
 * @param b Raw right transform (16 contiguous FSize, column-major).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_affine_mul_rot_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the product of two rotation-only transforms (a * b).
 * @param a Left transform.
 * @param b Right transform.
 * @return Product Mat4.
 */
Mat4 math_affine_mul_rot_2(Mat4 const a, Mat4 const b);

/**
 * @brief Apply an axis-angle rotation to a raw affine transform.
 * @param mat Raw source transform (16 contiguous FSize, column-major).
 * @param angle Rotation angle in radians.
 * @param axis Raw rotation axis (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_affine_rotate_1(FSize const *const mat, FSize const angle, FSize const *const axis, FSize *const dest);

/**
 * @brief Return an affine transform with an axis-angle rotation applied.
 * @param mat Source transform.
 * @param angle Rotation angle in radians.
 * @param axis Rotation axis.
 * @return Rotated Mat4.
 */
Mat4 math_affine_rotate_2(Mat4 const mat, FSize const angle, Vec3 const axis);

/**
 * @brief Rotate a raw affine transform about a pivot point (axis-angle).
 * @param mat Raw source transform (16 contiguous FSize, column-major).
 * @param pivot Raw pivot point (3 contiguous FSize).
 * @param angle Rotation angle in radians.
 * @param axis Raw rotation axis (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_affine_rotate_at_1(FSize const *const mat, FSize const *const pivot, FSize const angle, FSize const *const axis, FSize *const dest);

/**
 * @brief Return an affine transform rotated about a pivot point (axis-angle).
 * @param mat Source transform.
 * @param pivot Pivot point.
 * @param angle Rotation angle in radians.
 * @param axis Rotation axis.
 * @return Rotated Mat4.
 */
Mat4 math_affine_rotate_at_2(Mat4 const mat, Vec3 const pivot, FSize const angle, Vec3 const axis);

/**
 * @brief Build a raw affine transform that rotates about a pivot point.
 * @param pivot Raw pivot point (3 contiguous FSize).
 * @param angle Rotation angle in radians.
 * @param axis Raw rotation axis (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_affine_rotate_atm_1(FSize const *const pivot, FSize const angle, FSize const *const axis, FSize *const dest);

/**
 * @brief Return an affine transform that rotates about a pivot point.
 * @param pivot Pivot point.
 * @param angle Rotation angle in radians.
 * @param axis Rotation axis.
 * @return Rotation-about-pivot Mat4.
 */
Mat4 math_affine_rotate_atm_2(Vec3 const pivot, FSize const angle, Vec3 const axis);

/**
 * @brief Build a raw axis-angle rotation transform.
 * @param angle Rotation angle in radians.
 * @param axis Raw rotation axis (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_affine_rotate_make_1(FSize const angle, FSize const *const axis, FSize *const dest);

/**
 * @brief Return an axis-angle rotation transform.
 * @param angle Rotation angle in radians.
 * @param axis Rotation axis.
 * @return Rotation Mat4.
 */
Mat4 math_affine_rotate_make_2(FSize const angle, Vec3 const axis);

/**
 * @brief Rotate a raw affine transform about the X axis.
 * @param mat Raw source transform (16 contiguous FSize, column-major).
 * @param angle Rotation angle in radians.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_affine_rotate_x_1(FSize const *const mat, FSize const angle, FSize *const dest);

/**
 * @brief Return an affine transform rotated about the X axis.
 * @param mat Source transform.
 * @param angle Rotation angle in radians.
 * @return Rotated Mat4.
 */
Mat4 math_affine_rotate_x_2(Mat4 const mat, FSize const angle);

/**
 * @brief Rotate a raw affine transform about the Y axis.
 * @param mat Raw source transform (16 contiguous FSize, column-major).
 * @param angle Rotation angle in radians.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_affine_rotate_y_1(FSize const *const mat, FSize const angle, FSize *const dest);

/**
 * @brief Return an affine transform rotated about the Y axis.
 * @param mat Source transform.
 * @param angle Rotation angle in radians.
 * @return Rotated Mat4.
 */
Mat4 math_affine_rotate_y_2(Mat4 const mat, FSize const angle);

/**
 * @brief Rotate a raw affine transform about the Z axis.
 * @param mat Raw source transform (16 contiguous FSize, column-major).
 * @param angle Rotation angle in radians.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_affine_rotate_z_1(FSize const *const mat, FSize const angle, FSize *const dest);

/**
 * @brief Return an affine transform rotated about the Z axis.
 * @param mat Source transform.
 * @param angle Rotation angle in radians.
 * @return Rotated Mat4.
 */
Mat4 math_affine_rotate_z_2(Mat4 const mat, FSize const angle);

/**
 * @brief Post-apply an axis-angle rotation to a raw affine transform.
 * @param mat Raw source transform (16 contiguous FSize, column-major).
 * @param angle Rotation angle in radians.
 * @param axis Raw rotation axis (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_affine_rotated_1(FSize const *const mat, FSize const angle, FSize const *const axis, FSize *const dest);

/**
 * @brief Return an affine transform with an axis-angle rotation post-applied.
 * @param mat Source transform.
 * @param angle Rotation angle in radians.
 * @param axis Rotation axis.
 * @return Rotated Mat4.
 */
Mat4 math_affine_rotated_2(Mat4 const mat, FSize const angle, Vec3 const axis);

/**
 * @brief Post-rotate a raw affine transform about a pivot point (axis-angle).
 * @param mat Raw source transform (16 contiguous FSize, column-major).
 * @param pivot Raw pivot point (3 contiguous FSize).
 * @param angle Rotation angle in radians.
 * @param axis Raw rotation axis (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_affine_rotated_at_1(FSize const *const mat, FSize const *const pivot, FSize const angle, FSize const *const axis, FSize *const dest);

/**
 * @brief Return an affine transform post-rotated about a pivot point (axis-angle).
 * @param mat Source transform.
 * @param pivot Pivot point.
 * @param angle Rotation angle in radians.
 * @param axis Rotation axis.
 * @return Rotated Mat4.
 */
Mat4 math_affine_rotated_at_2(Mat4 const mat, Vec3 const pivot, FSize const angle, Vec3 const axis);

/**
 * @brief Post-rotate a raw affine transform about the X axis.
 * @param mat Raw source transform (16 contiguous FSize, column-major).
 * @param angle Rotation angle in radians.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_affine_rotated_x_1(FSize const *const mat, FSize const angle, FSize *const dest);

/**
 * @brief Return an affine transform post-rotated about the X axis.
 * @param mat Source transform.
 * @param angle Rotation angle in radians.
 * @return Rotated Mat4.
 */
Mat4 math_affine_rotated_x_2(Mat4 const mat, FSize const angle);

/**
 * @brief Post-rotate a raw affine transform about the Y axis.
 * @param mat Raw source transform (16 contiguous FSize, column-major).
 * @param angle Rotation angle in radians.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_affine_rotated_y_1(FSize const *const mat, FSize const angle, FSize *const dest);

/**
 * @brief Return an affine transform post-rotated about the Y axis.
 * @param mat Source transform.
 * @param angle Rotation angle in radians.
 * @return Rotated Mat4.
 */
Mat4 math_affine_rotated_y_2(Mat4 const mat, FSize const angle);

/**
 * @brief Post-rotate a raw affine transform about the Z axis.
 * @param mat Raw source transform (16 contiguous FSize, column-major).
 * @param angle Rotation angle in radians.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_affine_rotated_z_1(FSize const *const mat, FSize const angle, FSize *const dest);

/**
 * @brief Return an affine transform post-rotated about the Z axis.
 * @param mat Source transform.
 * @param angle Rotation angle in radians.
 * @return Rotated Mat4.
 */
Mat4 math_affine_rotated_z_2(Mat4 const mat, FSize const angle);

/**
 * @brief Apply a non-uniform scale to a raw affine transform.
 * @param mat Raw source transform (16 contiguous FSize, column-major).
 * @param factors Raw per-axis scale factors (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_affine_scale_1(FSize const *const mat, FSize const *const factors, FSize *const dest);

/**
 * @brief Return an affine transform with a non-uniform scale applied.
 * @param mat Source transform.
 * @param factors Per-axis scale factors.
 * @return Scaled Mat4.
 */
Mat4 math_affine_scale_2(Mat4 const mat, Vec3 const factors);

/**
 * @brief Build a raw non-uniform scale transform.
 * @param factors Raw per-axis scale factors (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_affine_scale_make_1(FSize const *const factors, FSize *const dest);

/**
 * @brief Return a non-uniform scale transform.
 * @param factors Per-axis scale factors.
 * @return Scale Mat4.
 */
Mat4 math_affine_scale_make_2(Vec3 const factors);

/**
 * @brief Apply a uniform scale to a raw affine transform.
 * @param mat Raw source transform (16 contiguous FSize, column-major).
 * @param factor Uniform scale factor applied to every axis.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_affine_scale_uni_1(FSize const *const mat, FSize const factor, FSize *const dest);

/**
 * @brief Return an affine transform with a uniform scale applied.
 * @param mat Source transform.
 * @param factor Uniform scale factor applied to every axis.
 * @return Scaled Mat4.
 */
Mat4 math_affine_scale_uni_2(Mat4 const mat, FSize const factor);

/**
 * @brief Spin a raw affine transform about an axis without moving its origin.
 * @param mat Raw source transform (16 contiguous FSize, column-major).
 * @param angle Rotation angle in radians.
 * @param axis Raw rotation axis (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_affine_spin_1(FSize const *const mat, FSize const angle, FSize const *const axis, FSize *const dest);

/**
 * @brief Return an affine transform spun about an axis without moving its origin.
 * @param mat Source transform.
 * @param angle Rotation angle in radians.
 * @param axis Rotation axis.
 * @return Spun Mat4.
 */
Mat4 math_affine_spin_2(Mat4 const mat, FSize const angle, Vec3 const axis);

/**
 * @brief Post-spin a raw affine transform about an axis without moving its origin.
 * @param mat Raw source transform (16 contiguous FSize, column-major).
 * @param angle Rotation angle in radians.
 * @param axis Raw rotation axis (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_affine_spinned_1(FSize const *const mat, FSize const angle, FSize const *const axis, FSize *const dest);

/**
 * @brief Return an affine transform post-spun about an axis about its origin.
 * @param mat Source transform.
 * @param angle Rotation angle in radians.
 * @param axis Rotation axis.
 * @return Spun Mat4.
 */
Mat4 math_affine_spinned_2(Mat4 const mat, FSize const angle, Vec3 const axis);

/**
 * @brief Apply a translation to a raw affine transform.
 * @param mat Raw source transform (16 contiguous FSize, column-major).
 * @param offset Raw translation offset (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_affine_translate_1(FSize const *const mat, FSize const *const offset, FSize *const dest);

/**
 * @brief Return an affine transform with a translation applied.
 * @param mat Source transform.
 * @param offset Translation offset.
 * @return Translated Mat4.
 */
Mat4 math_affine_translate_2(Mat4 const mat, Vec3 const offset);

/**
 * @brief Build a raw translation transform.
 * @param offset Raw translation offset (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_affine_translate_make_1(FSize const *const offset, FSize *const dest);

/**
 * @brief Return a translation transform.
 * @param offset Translation offset.
 * @return Translation Mat4.
 */
Mat4 math_affine_translate_make_2(Vec3 const offset);

/**
 * @brief Translate a raw affine transform along the X axis.
 * @param mat Raw source transform (16 contiguous FSize, column-major).
 * @param to Translation amount along X.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_affine_translate_x_1(FSize const *const mat, FSize const to, FSize *const dest);

/**
 * @brief Return an affine transform translated along the X axis.
 * @param mat Source transform.
 * @param to Translation amount along X.
 * @return Translated Mat4.
 */
Mat4 math_affine_translate_x_2(Mat4 const mat, FSize const to);

/**
 * @brief Translate a raw affine transform along the Y axis.
 * @param mat Raw source transform (16 contiguous FSize, column-major).
 * @param to Translation amount along Y.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_affine_translate_y_1(FSize const *const mat, FSize const to, FSize *const dest);

/**
 * @brief Return an affine transform translated along the Y axis.
 * @param mat Source transform.
 * @param to Translation amount along Y.
 * @return Translated Mat4.
 */
Mat4 math_affine_translate_y_2(Mat4 const mat, FSize const to);

/**
 * @brief Translate a raw affine transform along the Z axis.
 * @param mat Raw source transform (16 contiguous FSize, column-major).
 * @param to Translation amount along Z.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_affine_translate_z_1(FSize const *const mat, FSize const to, FSize *const dest);

/**
 * @brief Return an affine transform translated along the Z axis.
 * @param mat Source transform.
 * @param to Translation amount along Z.
 * @return Translated Mat4.
 */
Mat4 math_affine_translate_z_2(Mat4 const mat, FSize const to);

/**
 * @brief Post-apply a translation to a raw affine transform.
 * @param mat Raw source transform (16 contiguous FSize, column-major).
 * @param offset Raw translation offset (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_affine_translated_1(FSize const *const mat, FSize const *const offset, FSize *const dest);

/**
 * @brief Return an affine transform with a translation post-applied.
 * @param mat Source transform.
 * @param offset Translation offset.
 * @return Translated Mat4.
 */
Mat4 math_affine_translated_2(Mat4 const mat, Vec3 const offset);

/**
 * @brief Post-translate a raw affine transform along the X axis.
 * @param mat Raw source transform (16 contiguous FSize, column-major).
 * @param to Translation amount along X.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_affine_translated_x_1(FSize const *const mat, FSize const to, FSize *const dest);

/**
 * @brief Return an affine transform post-translated along the X axis.
 * @param mat Source transform.
 * @param to Translation amount along X.
 * @return Translated Mat4.
 */
Mat4 math_affine_translated_x_2(Mat4 const mat, FSize const to);

/**
 * @brief Post-translate a raw affine transform along the Y axis.
 * @param mat Raw source transform (16 contiguous FSize, column-major).
 * @param to Translation amount along Y.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_affine_translated_y_1(FSize const *const mat, FSize const to, FSize *const dest);

/**
 * @brief Return an affine transform post-translated along the Y axis.
 * @param mat Source transform.
 * @param to Translation amount along Y.
 * @return Translated Mat4.
 */
Mat4 math_affine_translated_y_2(Mat4 const mat, FSize const to);

/**
 * @brief Post-translate a raw affine transform along the Z axis.
 * @param mat Raw source transform (16 contiguous FSize, column-major).
 * @param to Translation amount along Z.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_affine_translated_z_1(FSize const *const mat, FSize const to, FSize *const dest);

/**
 * @brief Return an affine transform post-translated along the Z axis.
 * @param mat Source transform.
 * @param to Translation amount along Z.
 * @return Translated Mat4.
 */
Mat4 math_affine_translated_z_2(Mat4 const mat, FSize const to);

/**
 * @brief Report whether a raw affine transform has uniform scale on every axis.
 * @param mat Raw source transform (16 contiguous FSize, column-major).
 * @return true when the scale is uniform, false otherwise.
 */
bool math_affine_uniscaled_1(FSize const *const mat);

/**
 * @brief Report whether an affine transform has uniform scale on every axis.
 * @param mat Source transform.
 * @return true when the scale is uniform, false otherwise.
 */
bool math_affine_uniscaled_2(Mat4 const mat);

#endif // MATH_AFFINE_H