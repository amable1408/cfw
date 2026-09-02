/*
 * mat3x2.h - 3x2 matrix operations for the CFW math module
 *
 * Features:
 *   - Full coverage of cglm's compiled glmc_mat3x2_* API: copy, zero/make
 *     producers, matrix multiply, matrix-vector multiply, transpose, and scale
 *   - Root-first variants: _1 raw FSize-array underset, _2 framework-struct superset
 *   - Pure-value semantics: no wrapper mutates its argument in place; the raw (_1)
 *     variant writes a caller-supplied destination, the struct (_2) variant returns
 *     a Mat3x2 (or a cross-type Mat2/Mat2x3/Vec2 for the mul/transpose/mulv results)
 *
 * Usage Examples:
 *   @code
 *   Mat3x2 const a = { { { 1.0, 2.0 }, { 3.0, 4.0 }, { 5.0, 6.0 } } };
 *   Mat3x2 const s = math_mat3x2_scale_2(a, 2.0);
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
 *     glmc_* routine. cglm's mat3x2 routines are scalar C behind a non-inlinable call -
 *     only its vec4/mat4 paths are SIMD-accelerated.
 *   - Precision: every value crosses F64 -> float -> F64; see the Precision section
 *     in types.h for what that loses and where it flips a result.
 *
 * Dependencies:
 *   - <math/types.h> for the Mat3x2/Mat2x3/Mat2/Vec3/Vec2 types, the raw<->cglm
 *     bridges, cglm, and the error/tracing macros.
 *
 * See mat3x2.c for implementation details.
 */

#ifndef MATH_MAT3X2_H
#define MATH_MAT3X2_H

#include <math/types.h>

/*==============================================================================
 * MARK: - Mat3x2 API
 *
 * A Mat3x2 is 3 columns of 2 rows. Raw (_1) variants read and write 6 contiguous
 * FSize in column-major order (index = col * 2 + row); the struct (_2) variants
 * read and return a Mat3x2 value. Cross-type ops read/write their partner layouts:
 * mul takes a Mat2x3 and yields a Mat2, transpose yields a Mat2x3, mulv takes a
 * Vec3 and yields a Vec2.
 *============================================================================*/

/**
 * @brief Copy a raw 3x2 matrix.
 * @param mat Raw source matrix (6 contiguous FSize, column-major).
 * @param dest Destination of 6 contiguous FSize.
 */
void math_mat3x2_copy_1(FSize const *const mat, FSize *const dest);

/**
 * @brief Return a copy of a 3x2 matrix.
 * @param mat Source matrix.
 * @return Copied Mat3x2.
 */
Mat3x2 math_mat3x2_copy_2(Mat3x2 const mat);

/**
 * @brief Construct a raw 3x2 matrix from a raw FSize source array.
 * @param src Raw source array (6 contiguous FSize, column-major).
 * @param dest Destination of 6 contiguous FSize.
 */
void math_mat3x2_make_1(FSize const *const src, FSize *const dest);

/**
 * @brief Construct a 3x2 matrix from a raw FSize source array.
 * @param src Raw source array (6 contiguous FSize, column-major).
 * @return Constructed Mat3x2.
 */
Mat3x2 math_mat3x2_make_2(FSize const *const src);

/**
 * @brief Multiply a raw 3x2 matrix by a raw 2x3 matrix (a * b -> 2x2).
 * @param a Raw left 3x2 matrix (6 contiguous FSize, column-major).
 * @param b Raw right 2x3 matrix (6 contiguous FSize, column-major).
 * @param dest Destination 2x2 matrix of 4 contiguous FSize.
 */
void math_mat3x2_mul_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the product of a 3x2 matrix and a 2x3 matrix (a * b -> Mat2).
 * @param a Left 3x2 matrix.
 * @param b Right 2x3 matrix.
 * @return Product Mat2.
 */
Mat2 math_mat3x2_mul_2(Mat3x2 const a, Mat2x3 const b);

/**
 * @brief Multiply a raw 3x2 matrix by a raw 3D vector (m * v -> 2D vector).
 * @param m Raw 3x2 matrix (6 contiguous FSize, column-major).
 * @param v Raw vector (3 contiguous FSize).
 * @param dest Destination vector of 2 contiguous FSize.
 */
void math_mat3x2_mulv_1(FSize const *const m, FSize const *const v, FSize *const dest);

/**
 * @brief Return the product of a 3x2 matrix and a 3D vector (m * v -> Vec2).
 * @param m Matrix.
 * @param v Vector.
 * @return Result Vec2.
 */
Vec2 math_mat3x2_mulv_2(Mat3x2 const m, Vec3 const v);

/**
 * @brief Scale every element of a raw 3x2 matrix by a scalar.
 * @param mat Raw source matrix (6 contiguous FSize, column-major).
 * @param s Scalar factor.
 * @param dest Destination of 6 contiguous FSize.
 */
void math_mat3x2_scale_1(FSize const *const mat, FSize const s, FSize *const dest);

/**
 * @brief Return a 3x2 matrix with every element scaled by a scalar.
 * @param mat Source matrix.
 * @param s Scalar factor.
 * @return Scaled Mat3x2.
 */
Mat3x2 math_mat3x2_scale_2(Mat3x2 const mat, FSize const s);

/**
 * @brief Transpose a raw 3x2 matrix into a raw 2x3 matrix.
 * @param mat Raw source 3x2 matrix (6 contiguous FSize, column-major).
 * @param dest Destination 2x3 matrix of 6 contiguous FSize.
 */
void math_mat3x2_transpose_1(FSize const *const mat, FSize *const dest);

/**
 * @brief Return the transpose of a 3x2 matrix (-> Mat2x3).
 * @param mat Source matrix.
 * @return Transposed Mat2x3.
 */
Mat2x3 math_mat3x2_transpose_2(Mat3x2 const mat);

/**
 * @brief Write the 3x2 zero matrix into a raw destination.
 * @param dest Destination of 6 contiguous FSize.
 */
void math_mat3x2_zero_1(FSize *const dest);

/**
 * @brief Return the 3x2 zero matrix.
 * @return Zero Mat3x2.
 */
Mat3x2 math_mat3x2_zero_2(void);

#endif // MATH_MAT3X2_H