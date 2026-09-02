/*
 * mat3x4.h - 3-column 4-row matrix operations for the CFW math module
 *
 * Features:
 *   - Full coverage of cglm's compiled glmc_mat3x4_* API: copy, zero producer,
 *     construction from a raw array, matrix multiply (mat3x4 * mat4x3 -> mat4),
 *     matrix-vector multiply (mat3x4 * vec3 -> vec4), transpose (-> mat4x3), and
 *     scale
 *   - Root-first variants: _1 raw FSize-array underset, _2 framework-struct superset
 *   - Pure-value semantics: no wrapper mutates its argument in place; the raw (_1)
 *     variant writes a caller-supplied destination, the struct (_2) variant returns
 *     a Mat3x4 (or a Mat4/Mat4x3/Vec4 for cross-type results)
 *
 * Usage Examples:
 *   @code
 *   Mat3x4 const a  = { { { 1.0, 0.0, 0.0, 0.0 },
 *                         { 0.0, 1.0, 0.0, 0.0 },
 *                         { 0.0, 0.0, 1.0, 0.0 } } };
 *   Mat4x3 const at = math_mat3x4_transpose_2(a);
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
 *     glmc_* routine. cglm's mat3x4 routines are scalar C behind a non-inlinable call -
 *     only its vec4/mat4 paths are SIMD-accelerated.
 *   - Precision: every value crosses F64 -> float -> F64; see the Precision section
 *     in types.h for what that loses and where it flips a result.
 *
 * Dependencies:
 *   - <math/types.h> for the Mat3x4/Mat4x3/Mat4/Vec3/Vec4 types, the raw<->cglm
 *     bridges, cglm, and the error/tracing macros.
 *
 * See mat3x4.c for implementation details.
 */

#ifndef MATH_MAT3X4_H
#define MATH_MAT3X4_H

#include <math/types.h>

/*==============================================================================
 * MARK: - Mat3x4 API
 *
 * Raw (_1) variants read and write 12 contiguous FSize in column-major order
 * (index = col * 4 + row, 3 columns of 4 rows); the struct (_2) variants read
 * and return a Mat3x4 value. Cross-type ops read/write the raw operand of the
 * other type: mul takes a mat4x3 (12 FSize) and yields a mat4 (16 FSize), mulv
 * takes a vec3 (3 FSize) and yields a vec4 (4 FSize), and transpose yields a
 * mat4x3 (12 FSize).
 *============================================================================*/

/**
 * @brief Copy a raw 3x4 matrix.
 * @param mat Raw source matrix (12 contiguous FSize, column-major).
 * @param dest Destination of 12 contiguous FSize.
 */
void math_mat3x4_copy_1(FSize const *const mat, FSize *const dest);

/**
 * @brief Return a copy of a 3x4 matrix.
 * @param mat Source matrix.
 * @return Copied Mat3x4.
 */
Mat3x4 math_mat3x4_copy_2(Mat3x4 const mat);

/**
 * @brief Construct a raw 3x4 matrix from a raw FSize source array.
 * @param src Raw source array (12 contiguous FSize, column-major).
 * @param dest Destination of 12 contiguous FSize.
 */
void math_mat3x4_make_1(FSize const *const src, FSize *const dest);

/**
 * @brief Construct a 3x4 matrix from a raw FSize source array.
 * @param src Raw source array (12 contiguous FSize, column-major).
 * @return Constructed Mat3x4.
 */
Mat3x4 math_mat3x4_make_2(FSize const *const src);

/**
 * @brief Multiply a raw 3x4 matrix by a raw 4x3 matrix (a * b -> 4x4).
 * @param a Raw left matrix (12 contiguous FSize, column-major mat3x4).
 * @param b Raw right matrix (12 contiguous FSize, column-major mat4x3).
 * @param dest Destination of 16 contiguous FSize (column-major mat4).
 */
void math_mat3x4_mul_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the product of a 3x4 and a 4x3 matrix (a * b -> Mat4).
 * @param a Left 3x4 matrix.
 * @param b Right 4x3 matrix.
 * @return Product Mat4.
 */
Mat4 math_mat3x4_mul_2(Mat3x4 const a, Mat4x3 const b);

/**
 * @brief Multiply a raw 3x4 matrix by a raw 3D vector (m * v -> 4D vector).
 * @param m Raw matrix (12 contiguous FSize, column-major).
 * @param v Raw vector (3 contiguous FSize).
 * @param dest Destination vector of 4 contiguous FSize.
 */
void math_mat3x4_mulv_1(FSize const *const m, FSize const *const v, FSize *const dest);

/**
 * @brief Return the product of a 3x4 matrix and a 3D vector (m * v -> Vec4).
 * @param m Matrix.
 * @param v Vector.
 * @return Result Vec4.
 */
Vec4 math_mat3x4_mulv_2(Mat3x4 const m, Vec3 const v);

/**
 * @brief Scale every element of a raw 3x4 matrix by a scalar.
 * @param mat Raw source matrix (12 contiguous FSize, column-major).
 * @param s Scalar factor.
 * @param dest Destination of 12 contiguous FSize.
 */
void math_mat3x4_scale_1(FSize const *const mat, FSize const s, FSize *const dest);

/**
 * @brief Return a 3x4 matrix with every element scaled by a scalar.
 * @param mat Source matrix.
 * @param s Scalar factor.
 * @return Scaled Mat3x4.
 */
Mat3x4 math_mat3x4_scale_2(Mat3x4 const mat, FSize const s);

/**
 * @brief Transpose of a raw 3x4 matrix (-> 4x3).
 * @param mat Raw source matrix (12 contiguous FSize, column-major mat3x4).
 * @param dest Destination of 12 contiguous FSize (column-major mat4x3).
 */
void math_mat3x4_transpose_1(FSize const *const mat, FSize *const dest);

/**
 * @brief Return the transpose of a 3x4 matrix (-> Mat4x3).
 * @param mat Source matrix.
 * @return Transposed Mat4x3.
 */
Mat4x3 math_mat3x4_transpose_2(Mat3x4 const mat);

/**
 * @brief Write the 3x4 zero matrix into a raw destination.
 * @param dest Destination of 12 contiguous FSize.
 */
void math_mat3x4_zero_1(FSize *const dest);

/**
 * @brief Return the 3x4 zero matrix.
 * @return Zero Mat3x4.
 */
Mat3x4 math_mat3x4_zero_2(void);

#endif // MATH_MAT3X4_H