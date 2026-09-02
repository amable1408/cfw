/*
 * mat4x2.h - 4x2 matrix operations for the CFW math module
 *
 * Features:
 *   - Full coverage of cglm's compiled glmc_mat4x2_* API: construction, copy,
 *     zero producer, scale, matrix-vector multiply, matrix-matrix multiply, and
 *     transpose
 *   - Root-first variants: _1 raw FSize-array underset, _2 framework-struct superset
 *   - Pure-value semantics: no wrapper mutates its argument in place; the raw (_1)
 *     variant writes a caller-supplied destination, the struct (_2) variant returns
 *     a value
 *   - Cross-type results follow cglm exactly: transpose(mat4x2) -> Mat2x4,
 *     mul(mat4x2, mat2x4) -> Mat2, mulv(mat4x2, vec4) -> Vec2
 *
 * Usage Examples:
 *   @code
 *   Mat4x2 const a = { { { 1.0, 2.0 }, { 3.0, 4.0 }, { 5.0, 6.0 }, { 7.0, 8.0 } } };
 *   Mat2x4 const t = math_mat4x2_transpose_2(a);
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
 *     glmc_* routine. cglm's mat4x2 routines are scalar C behind a non-inlinable call -
 *     only its vec4/mat4 paths are SIMD-accelerated.
 *   - Precision: every value crosses F64 -> float -> F64; see the Precision section
 *     in types.h for what that loses and where it flips a result.
 *
 * Dependencies:
 *   - <math/types.h> for the Mat4x2/Mat2x4/Mat2/Vec4/Vec2 types, the raw<->cglm
 *     bridges, cglm, and the error/tracing macros.
 *
 * See mat4x2.c for implementation details.
 */

#ifndef MATH_MAT4X2_H
#define MATH_MAT4X2_H

#include <math/types.h>

/*==============================================================================
 * MARK: - Mat4x2 API
 *
 * Raw (_1) variants read and write 8 contiguous FSize in column-major order
 * (index = col * 2 + row, 4 columns of 2 rows); the struct (_2) variants read and
 * return a Mat4x2 value. Cross-type ops read/write the raw underset of their cglm
 * operand: mul reads an 8-FSize mat2x4 and writes a 4-FSize Mat2, mulv reads a
 * 4-FSize Vec4 and writes a 2-FSize Vec2, transpose writes an 8-FSize Mat2x4.
 *============================================================================*/

/**
 * @brief Copy a raw 4x2 matrix.
 * @param mat Raw source matrix (8 contiguous FSize, column-major).
 * @param dest Destination of 8 contiguous FSize.
 */
void math_mat4x2_copy_1(FSize const *const mat, FSize *const dest);

/**
 * @brief Return a copy of a 4x2 matrix.
 * @param mat Source matrix.
 * @return Copied Mat4x2.
 */
Mat4x2 math_mat4x2_copy_2(Mat4x2 const mat);

/**
 * @brief Construct a raw 4x2 matrix from a raw FSize source array.
 * @param src Raw source array (8 contiguous FSize, column-major).
 * @param dest Destination of 8 contiguous FSize.
 */
void math_mat4x2_make_1(FSize const *const src, FSize *const dest);

/**
 * @brief Construct a 4x2 matrix from a raw FSize source array.
 * @param src Raw source array (8 contiguous FSize, column-major).
 * @return Constructed Mat4x2.
 */
Mat4x2 math_mat4x2_make_2(FSize const *const src);

/**
 * @brief Multiply a raw 4x2 matrix by a raw 2x4 matrix (a * b -> 2x2).
 * @param a Raw left matrix (8 contiguous FSize, column-major mat4x2).
 * @param b Raw right matrix (8 contiguous FSize, column-major mat2x4).
 * @param dest Destination of 4 contiguous FSize (column-major mat2).
 */
void math_mat4x2_mul_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the product of a 4x2 matrix and a 2x4 matrix (a * b -> Mat2).
 * @param a Left matrix.
 * @param b Right matrix.
 * @return Product Mat2.
 */
Mat2 math_mat4x2_mul_2(Mat4x2 const a, Mat2x4 const b);

/**
 * @brief Multiply a raw 4x2 matrix by a raw 4D vector (m * v -> 2D vector).
 * @param m Raw matrix (8 contiguous FSize, column-major).
 * @param v Raw vector (4 contiguous FSize).
 * @param dest Destination vector of 2 contiguous FSize.
 */
void math_mat4x2_mulv_1(FSize const *const m, FSize const *const v, FSize *const dest);

/**
 * @brief Return the product of a 4x2 matrix and a 4D vector (m * v -> Vec2).
 * @param m Matrix.
 * @param v Vector.
 * @return Result Vec2.
 */
Vec2 math_mat4x2_mulv_2(Mat4x2 const m, Vec4 const v);

/**
 * @brief Scale every element of a raw 4x2 matrix by a scalar.
 * @param mat Raw source matrix (8 contiguous FSize, column-major).
 * @param s Scalar factor.
 * @param dest Destination of 8 contiguous FSize.
 */
void math_mat4x2_scale_1(FSize const *const mat, FSize const s, FSize *const dest);

/**
 * @brief Return a 4x2 matrix with every element scaled by a scalar.
 * @param mat Source matrix.
 * @param s Scalar factor.
 * @return Scaled Mat4x2.
 */
Mat4x2 math_mat4x2_scale_2(Mat4x2 const mat, FSize const s);

/**
 * @brief Transpose of a raw 4x2 matrix (4x2 -> 2x4).
 * @param mat Raw source matrix (8 contiguous FSize, column-major mat4x2).
 * @param dest Destination of 8 contiguous FSize (column-major mat2x4).
 */
void math_mat4x2_transpose_1(FSize const *const mat, FSize *const dest);

/**
 * @brief Return the transpose of a 4x2 matrix (4x2 -> Mat2x4).
 * @param mat Source matrix.
 * @return Transposed Mat2x4.
 */
Mat2x4 math_mat4x2_transpose_2(Mat4x2 const mat);

/**
 * @brief Write the 4x2 zero matrix into a raw destination.
 * @param dest Destination of 8 contiguous FSize.
 */
void math_mat4x2_zero_1(FSize *const dest);

/**
 * @brief Return the 4x2 zero matrix.
 * @return Zero Mat4x2.
 */
Mat4x2 math_mat4x2_zero_2(void);

#endif // MATH_MAT4X2_H