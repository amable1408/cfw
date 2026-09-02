/*
 * mat2x4.h - 2x4 matrix operations for the CFW math module
 *
 * Features:
 *   - Full coverage of cglm's compiled glmc_mat2x4_* API: construction, copy,
 *     zero producer, scale, matrix-vector multiply, cross-type multiply, and
 *     cross-type transpose
 *   - Root-first variants: _1 raw FSize-array underset, _2 framework-struct superset
 *   - Pure-value semantics: no wrapper mutates its argument in place; the raw (_1)
 *     variant writes a caller-supplied destination, the struct (_2) variant returns
 *     a Mat2x4 (or a Vec4/Mat4/Mat4x2 for cross-type results)
 *   - Cross-type results follow cglm exactly: transpose yields a Mat4x2, and
 *     mul(mat2x4, mat4x2) yields a Mat4
 *
 * Usage Examples:
 *   @code
 *   Mat2x4 const a = { { { 1.0, 2.0, 3.0, 4.0 }, { 5.0, 6.0, 7.0, 8.0 } } };
 *   Mat2x4 const s = math_mat2x4_scale_2(a, 2.0);
 *   Mat4x2 const t = math_mat2x4_transpose_2(a);
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
 *     glmc_* routine. cglm's mat2x4 routines are scalar C behind a non-inlinable call -
 *     only its vec4/mat4 paths are SIMD-accelerated.
 *   - Precision: every value crosses F64 -> float -> F64; see the Precision section
 *     in types.h for what that loses and where it flips a result.
 *
 * Dependencies:
 *   - <math/types.h> for the Mat2x4/Mat4x2/Mat4/Vec2/Vec4 types, the raw<->cglm
 *     bridges, cglm, and the error/tracing macros.
 *
 * See mat2x4.c for implementation details.
 */

#ifndef MATH_MAT2X4_H
#define MATH_MAT2X4_H

#include <math/types.h>

/*==============================================================================
 * MARK: - Mat2x4 API
 *
 * Raw (_1) variants read and write 8 contiguous FSize in column-major order
 * (index = col * 4 + row); the struct (_2) variants read and return a Mat2x4
 * value. Cross-type ops read/write the matching contiguous FSize layout: mulv
 * takes a Vec2 (2 FSize) and yields a Vec4 (4 FSize); mul takes a Mat4x2 (8
 * FSize) and yields a Mat4 (16 FSize); transpose yields a Mat4x2 (8 FSize).
 *============================================================================*/

/**
 * @brief Copy a raw 2x4 matrix.
 * @param mat Raw source matrix (8 contiguous FSize, column-major).
 * @param dest Destination of 8 contiguous FSize.
 */
void math_mat2x4_copy_1(FSize const *const mat, FSize *const dest);

/**
 * @brief Return a copy of a 2x4 matrix.
 * @param mat Source matrix.
 * @return Copied Mat2x4.
 */
Mat2x4 math_mat2x4_copy_2(Mat2x4 const mat);

/**
 * @brief Construct a raw 2x4 matrix from a raw FSize source array.
 * @param src Raw source array (8 contiguous FSize, column-major).
 * @param dest Destination of 8 contiguous FSize.
 */
void math_mat2x4_make_1(FSize const *const src, FSize *const dest);

/**
 * @brief Construct a 2x4 matrix from a raw FSize source array.
 * @param src Raw source array (8 contiguous FSize, column-major).
 * @return Constructed Mat2x4.
 */
Mat2x4 math_mat2x4_make_2(FSize const *const src);

/**
 * @brief Multiply a raw 2x4 matrix by a raw 4x2 matrix (a * b -> 4x4).
 * @param a Raw left matrix (8 contiguous FSize, column-major).
 * @param b Raw right matrix (8 contiguous FSize, column-major).
 * @param dest Destination of 16 contiguous FSize (column-major 4x4).
 */
void math_mat2x4_mul_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the product of a 2x4 matrix and a 4x2 matrix (a * b).
 * @param a Left matrix.
 * @param b Right matrix.
 * @return Product Mat4.
 */
Mat4 math_mat2x4_mul_2(Mat2x4 const a, Mat4x2 const b);

/**
 * @brief Multiply a raw 2x4 matrix by a raw 2D vector (m * v -> 4D vector).
 * @param m Raw matrix (8 contiguous FSize, column-major).
 * @param v Raw vector (2 contiguous FSize).
 * @param dest Destination vector of 4 contiguous FSize.
 */
void math_mat2x4_mulv_1(FSize const *const m, FSize const *const v, FSize *const dest);

/**
 * @brief Return the product of a 2x4 matrix and a 2D vector (m * v).
 * @param m Matrix.
 * @param v Vector.
 * @return Result Vec4.
 */
Vec4 math_mat2x4_mulv_2(Mat2x4 const m, Vec2 const v);

/**
 * @brief Scale every element of a raw 2x4 matrix by a scalar.
 * @param mat Raw source matrix (8 contiguous FSize, column-major).
 * @param s Scalar factor.
 * @param dest Destination of 8 contiguous FSize.
 */
void math_mat2x4_scale_1(FSize const *const mat, FSize const s, FSize *const dest);

/**
 * @brief Return a 2x4 matrix with every element scaled by a scalar.
 * @param mat Source matrix.
 * @param s Scalar factor.
 * @return Scaled Mat2x4.
 */
Mat2x4 math_mat2x4_scale_2(Mat2x4 const mat, FSize const s);

/**
 * @brief Transpose of a raw 2x4 matrix (yields a 4x2 matrix).
 * @param mat Raw source matrix (8 contiguous FSize, column-major).
 * @param dest Destination of 8 contiguous FSize (column-major 4x2).
 */
void math_mat2x4_transpose_1(FSize const *const mat, FSize *const dest);

/**
 * @brief Return the transpose of a 2x4 matrix.
 * @param mat Source matrix.
 * @return Transposed Mat4x2.
 */
Mat4x2 math_mat2x4_transpose_2(Mat2x4 const mat);

/**
 * @brief Write the 2x4 zero matrix into a raw destination.
 * @param dest Destination of 8 contiguous FSize.
 */
void math_mat2x4_zero_1(FSize *const dest);

/**
 * @brief Return the 2x4 zero matrix.
 * @return Zero Mat2x4.
 */
Mat2x4 math_mat2x4_zero_2(void);

#endif // MATH_MAT2X4_H