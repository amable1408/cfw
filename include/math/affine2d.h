/*
 * affine2d.h - 2D affine (3x3) transform operations for the CFW math module
 *
 * Features:
 *   - Full coverage of cglm's compiled glmc_*2d affine API: translation
 *     (translate2d, translate2d_x, translate2d_y, translate2d_make), scaling
 *     (scale2d, scale2d_uni, scale2d_make), and rotation (rotate2d,
 *     rotate2d_make) about the Z axis, all operating on 3x3 matrices
 *   - Root-first variants: _1 raw FSize-array underset, _2 framework-struct superset
 *   - Pure-value semantics: cglm's affine2d ops mutate their matrix in place, so
 *     every wrapper is exposed here as a pure producer/transformer. The raw (_1)
 *     transform variant reads a source matrix and writes a caller-supplied
 *     destination; the struct (_2) variant returns a fresh Mat3. The *_make ops
 *     take no source matrix -- they produce a transform from a vector or angle.
 *   - Collapsed pairs: cglm's in-place op and its _to sibling (translate2d /
 *     translate2d_to, scale2d / scale2d_to, rotate2d / rotate2d_to) are exposed
 *     as a single pure op over an explicit source and destination.
 *
 * Usage Examples:
 *   @code
 *   Vec2 const v  = { 5.0, 7.0 };
 *   Mat3 const t  = math_affine2d_translate2d_make_2(v);
 *   Mat3 const r  = math_affine2d_rotate2d_2(t, 1.5707963);
 *   Mat3 const s  = math_affine2d_scale2d_uni_2(r, 2.0);
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
 *     glmc_* routine. cglm's affine2d routines are scalar C behind a non-inlinable call -
 *     only its vec4/mat4 paths are SIMD-accelerated.
 *   - Precision: every value crosses F64 -> float -> F64; see the Precision section
 *     in types.h for what that loses and where it flips a result.
 *
 * Dependencies:
 *   - <math/types.h> for the Mat3 and Vec2 types, the raw<->cglm bridges, cglm,
 *     and the error/tracing macros.
 *
 * See affine2d.c for implementation details.
 */

#ifndef MATH_AFFINE2D_H
#define MATH_AFFINE2D_H

#include <math/types.h>

/*==============================================================================
 * MARK: - Affine2D API
 *
 * Raw (_1) matrix operands read and write 9 contiguous FSize in column-major
 * order; a raw vec2 operand is 2 contiguous FSize. The struct (_2) variants read
 * and return a Mat3 value, taking a Vec2 or an FSize angle/factor. Every op is
 * pure: transform ops read a source matrix and emit the transformed matrix, and
 * *_make ops emit a new matrix built from a vector or angle alone.
 *============================================================================*/

/**
 * @brief Rotate a raw 2D transform matrix about the Z axis by an angle.
 * @param mat Source of 9 contiguous FSize (column-major).
 * @param angle Rotation angle in radians.
 * @param dest Destination of 9 contiguous FSize (column-major).
 */
void math_affine2d_rotate2d_1(FSize const *const mat, FSize const angle, FSize *const dest);

/**
 * @brief Return a 2D transform matrix rotated about the Z axis by an angle.
 * @param mat Source matrix.
 * @param angle Rotation angle in radians.
 * @return Rotated Mat3.
 */
Mat3 math_affine2d_rotate2d_2(Mat3 const mat, FSize const angle);

/**
 * @brief Build a raw 2D rotation matrix for an angle about the Z axis.
 * @param angle Rotation angle in radians.
 * @param dest Destination of 9 contiguous FSize (column-major).
 */
void math_affine2d_rotate2d_make_1(FSize const angle, FSize *const dest);

/**
 * @brief Return a 2D rotation matrix for an angle about the Z axis.
 * @param angle Rotation angle in radians.
 * @return Rotation Mat3.
 */
Mat3 math_affine2d_rotate2d_make_2(FSize const angle);

/**
 * @brief Scale a raw 2D transform matrix by a raw vector.
 * @param mat Source of 9 contiguous FSize (column-major).
 * @param v Raw scale vector (2 contiguous FSize).
 * @param dest Destination of 9 contiguous FSize (column-major).
 */
void math_affine2d_scale2d_1(FSize const *const mat, FSize const *const v, FSize *const dest);

/**
 * @brief Return a 2D transform matrix scaled by a vector.
 * @param mat Source matrix.
 * @param v Scale vector.
 * @return Scaled Mat3.
 */
Mat3 math_affine2d_scale2d_2(Mat3 const mat, Vec2 const v);

/**
 * @brief Build a raw 2D scale matrix from a raw vector.
 * @param v Raw scale vector (2 contiguous FSize).
 * @param dest Destination of 9 contiguous FSize (column-major).
 */
void math_affine2d_scale2d_make_1(FSize const *const v, FSize *const dest);

/**
 * @brief Return a 2D scale matrix built from a vector.
 * @param v Scale vector.
 * @return Scale Mat3.
 */
Mat3 math_affine2d_scale2d_make_2(Vec2 const v);

/**
 * @brief Uniformly scale a raw 2D transform matrix by a scalar factor.
 * @param mat Source of 9 contiguous FSize (column-major).
 * @param s Uniform scale factor.
 * @param dest Destination of 9 contiguous FSize (column-major).
 */
void math_affine2d_scale2d_uni_1(FSize const *const mat, FSize const s, FSize *const dest);

/**
 * @brief Return a 2D transform matrix uniformly scaled by a scalar factor.
 * @param mat Source matrix.
 * @param s Uniform scale factor.
 * @return Uniformly scaled Mat3.
 */
Mat3 math_affine2d_scale2d_uni_2(Mat3 const mat, FSize const s);

/**
 * @brief Translate a raw 2D transform matrix by a raw vector.
 * @param mat Source of 9 contiguous FSize (column-major).
 * @param v Raw translation vector (2 contiguous FSize).
 * @param dest Destination of 9 contiguous FSize (column-major).
 */
void math_affine2d_translate2d_1(FSize const *const mat, FSize const *const v, FSize *const dest);

/**
 * @brief Return a 2D transform matrix translated by a vector.
 * @param mat Source matrix.
 * @param v Translation vector.
 * @return Translated Mat3.
 */
Mat3 math_affine2d_translate2d_2(Mat3 const mat, Vec2 const v);

/**
 * @brief Build a raw 2D translation matrix from a raw vector.
 * @param v Raw translation vector (2 contiguous FSize).
 * @param dest Destination of 9 contiguous FSize (column-major).
 */
void math_affine2d_translate2d_make_1(FSize const *const v, FSize *const dest);

/**
 * @brief Return a 2D translation matrix built from a vector.
 * @param v Translation vector.
 * @return Translation Mat3.
 */
Mat3 math_affine2d_translate2d_make_2(Vec2 const v);

/**
 * @brief Translate a raw 2D transform matrix along the x axis by a factor.
 * @param mat Source of 9 contiguous FSize (column-major).
 * @param to Translation factor along x.
 * @param dest Destination of 9 contiguous FSize (column-major).
 */
void math_affine2d_translate2d_x_1(FSize const *const mat, FSize const to, FSize *const dest);

/**
 * @brief Return a 2D transform matrix translated along the x axis by a factor.
 * @param mat Source matrix.
 * @param to Translation factor along x.
 * @return Translated Mat3.
 */
Mat3 math_affine2d_translate2d_x_2(Mat3 const mat, FSize const to);

/**
 * @brief Translate a raw 2D transform matrix along the y axis by a factor.
 * @param mat Source of 9 contiguous FSize (column-major).
 * @param to Translation factor along y.
 * @param dest Destination of 9 contiguous FSize (column-major).
 */
void math_affine2d_translate2d_y_1(FSize const *const mat, FSize const to, FSize *const dest);

/**
 * @brief Return a 2D transform matrix translated along the y axis by a factor.
 * @param mat Source matrix.
 * @param to Translation factor along y.
 * @return Translated Mat3.
 */
Mat3 math_affine2d_translate2d_y_2(Mat3 const mat, FSize const to);

#endif // MATH_AFFINE2D_H