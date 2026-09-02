/*
 * mat4x3.h - 4-column 3-row matrix operations for the CFW math module
 *
 * Features:
 *   - Full coverage of cglm's compiled glmc_mat4x3_* API: copy, zero producer,
 *     construction, cross-type multiply, matrix-vector multiply, cross-type
 *     transpose, and scale
 *   - Root-first variants: _1 raw FSize-array underset, _2 framework-struct superset
 *   - Pure-value semantics: no wrapper mutates its argument in place; the raw (_1)
 *     variant writes a caller-supplied destination, the struct (_2) variant returns
 *     a Mat4x3 (or a cross-type Mat3/Mat3x4/Vec3 for multiply/transpose results)
 *   - Cross-type results follow the cglm header exactly: mul(mat4x3, mat3x4) yields
 *     a 3x3 matrix (Mat3), mulv(mat4x3, vec4) yields a Vec3, and transpose(mat4x3)
 *     yields a Mat3x4
 *
 * Usage Examples:
 *   @code
 *   Mat4x3 const a  = { { { 1.0, 2.0, 3.0 }, { 4.0, 5.0, 6.0 },
 *                         { 7.0, 8.0, 9.0 }, { 10.0, 11.0, 12.0 } } };
 *   Mat3x4 const t  = math_mat4x3_transpose_2(a);
 *   Mat4x3 const sc = math_mat4x3_scale_2(a, 2.0);
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
 *     glmc_* routine. cglm's mat4x3 routines are scalar C behind a non-inlinable call -
 *     only its vec4/mat4 paths are SIMD-accelerated.
 *   - Precision: every value crosses F64 -> float -> F64; see the Precision section
 *     in types.h for what that loses and where it flips a result.
 *
 * Dependencies:
 *   - <math/types.h> for the Mat4x3/Mat3x4/Mat3/Vec3/Vec4 types, the raw<->cglm
 *     bridges, cglm, and the error/tracing macros.
 *
 * See mat4x3.c for implementation details.
 */

#ifndef MATH_MAT4X3_H
#define MATH_MAT4X3_H

#include <math/types.h>

/*==============================================================================
 * MARK: - Mat4x3 API
 *
 * Raw (_1) variants read and write 12 contiguous FSize in column-major order
 * (index = col * 3 + row, 4 columns of 3 rows); the struct (_2) variants read and
 * return a Mat4x3 value. Cross-type ops read/write their operands as contiguous
 * FSize too: a Mat3x4 as 12 (index = col * 4 + row), a Mat3 as 9 (index = col * 3
 * + row), a Vec4 as 4, and a Vec3 as 3.
 *============================================================================*/

/**
 * @brief Copy a raw 4x3 matrix.
 * @param mat Raw source matrix (12 contiguous FSize, column-major).
 * @param dest Destination of 12 contiguous FSize.
 */
void math_mat4x3_copy_1(FSize const *const mat, FSize *const dest);

/**
 * @brief Return a copy of a 4x3 matrix.
 * @param mat Source matrix.
 * @return Copied Mat4x3.
 */
Mat4x3 math_mat4x3_copy_2(Mat4x3 const mat);

/**
 * @brief Construct a raw 4x3 matrix from a raw FSize source array.
 * @param src Raw source array (12 contiguous FSize, column-major).
 * @param dest Destination of 12 contiguous FSize.
 */
void math_mat4x3_make_1(FSize const *const src, FSize *const dest);

/**
 * @brief Construct a 4x3 matrix from a raw FSize source array.
 * @param src Raw source array (12 contiguous FSize, column-major).
 * @return Constructed Mat4x3.
 */
Mat4x3 math_mat4x3_make_2(FSize const *const src);

/**
 * @brief Multiply a raw 4x3 matrix by a raw 3x4 matrix (a * b) into a 3x3 result.
 * @param a Raw left 4x3 matrix (12 contiguous FSize, column-major).
 * @param b Raw right 3x4 matrix (12 contiguous FSize, column-major).
 * @param dest Destination 3x3 matrix of 9 contiguous FSize.
 */
void math_mat4x3_mul_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the product of a 4x3 matrix and a 3x4 matrix (a * b) as a Mat3.
 * @param a Left 4x3 matrix.
 * @param b Right 3x4 matrix.
 * @return Product Mat3 (3x3).
 */
Mat3 math_mat4x3_mul_2(Mat4x3 const a, Mat3x4 const b);

/**
 * @brief Multiply a raw 4x3 matrix by a raw 4D vector (m * v) into a 3D result.
 * @param m Raw 4x3 matrix (12 contiguous FSize, column-major).
 * @param v Raw vector (4 contiguous FSize).
 * @param dest Destination vector of 3 contiguous FSize.
 */
void math_mat4x3_mulv_1(FSize const *const m, FSize const *const v, FSize *const dest);

/**
 * @brief Return the product of a 4x3 matrix and a 4D vector (m * v) as a Vec3.
 * @param m 4x3 matrix.
 * @param v 4D vector.
 * @return Result Vec3.
 */
Vec3 math_mat4x3_mulv_2(Mat4x3 const m, Vec4 const v);

/**
 * @brief Scale every element of a raw 4x3 matrix by a scalar.
 * @param mat Raw source matrix (12 contiguous FSize, column-major).
 * @param s Scalar factor.
 * @param dest Destination of 12 contiguous FSize.
 */
void math_mat4x3_scale_1(FSize const *const mat, FSize const s, FSize *const dest);

/**
 * @brief Return a 4x3 matrix with every element scaled by a scalar.
 * @param mat Source matrix.
 * @param s Scalar factor.
 * @return Scaled Mat4x3.
 */
Mat4x3 math_mat4x3_scale_2(Mat4x3 const mat, FSize const s);

/**
 * @brief Transpose of a raw 4x3 matrix into a 3x4 result.
 * @param mat Raw source 4x3 matrix (12 contiguous FSize, column-major).
 * @param dest Destination 3x4 matrix of 12 contiguous FSize.
 */
void math_mat4x3_transpose_1(FSize const *const mat, FSize *const dest);

/**
 * @brief Return the transpose of a 4x3 matrix as a Mat3x4.
 * @param mat Source 4x3 matrix.
 * @return Transposed Mat3x4.
 */
Mat3x4 math_mat4x3_transpose_2(Mat4x3 const mat);

/**
 * @brief Write the 4x3 zero matrix into a raw destination.
 * @param dest Destination of 12 contiguous FSize.
 */
void math_mat4x3_zero_1(FSize *const dest);

/**
 * @brief Return the 4x3 zero matrix.
 * @return Zero Mat4x3.
 */
Mat4x3 math_mat4x3_zero_2(void);

#endif // MATH_MAT4X3_H