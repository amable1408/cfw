/*
 * mat2x3.h - 2x3 (2-column, 3-row) matrix operations for the CFW math module
 *
 * Features:
 *   - Full coverage of cglm's compiled glmc_mat2x3_* API: copy, zero producer,
 *     construction, matrix multiply, matrix-vector multiply, transpose, and scale
 *   - Root-first variants: _1 raw FSize-array underset, _2 framework-struct superset
 *   - Pure-value semantics: no wrapper mutates its argument in place; the raw (_1)
 *     variant writes a caller-supplied destination, the struct (_2) variant returns
 *     a Mat2x3 (or the matching cross-type Mat3/Mat3x2/Vec3 for mul/transpose/mulv)
 *
 * Usage Examples:
 *   @code
 *   Mat2x3 const a    = { { { 1.0, 2.0, 3.0 }, { 4.0, 5.0, 6.0 } } };
 *   Mat2x3 const dbl  = math_mat2x3_scale_2(a, 2.0);
 *   Mat3x2 const at   = math_mat2x3_transpose_2(a);
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
 *     glmc_* routine. cglm's mat2x3 routines are scalar C behind a non-inlinable call -
 *     only its vec4/mat4 paths are SIMD-accelerated.
 *   - Precision: every value crosses F64 -> float -> F64; see the Precision section
 *     in types.h for what that loses and where it flips a result.
 *
 * Dependencies:
 *   - <math/types.h> for the Mat2x3/Mat3x2/Mat3/Vec2/Vec3 types, the raw<->cglm
 *     bridges, cglm, and the error/tracing macros.
 *
 * See mat2x3.c for implementation details.
 */

#ifndef MATH_MAT2X3_H
#define MATH_MAT2X3_H

#include <math/types.h>

/*==============================================================================
 * MARK: - Mat2x3 API
 *
 * Raw (_1) variants read and write 6 contiguous FSize in column-major order
 * (index = col * 3 + row); the struct (_2) variants read and return a Mat2x3
 * value. Cross-type ops read/write the raw form of the type named in each
 * signature: mul takes a mat3x2 (6 FSize) and yields a mat3 (9 FSize), transpose
 * yields a mat3x2 (6 FSize), and mulv takes a vec2 (2 FSize) and yields a vec3
 * (3 FSize).
 *============================================================================*/

/**
 * @brief Copy a raw 2x3 matrix.
 * @param mat Raw source matrix (6 contiguous FSize, column-major).
 * @param dest Destination of 6 contiguous FSize.
 */
void math_mat2x3_copy_1(FSize const *const mat, FSize *const dest);

/**
 * @brief Return a copy of a 2x3 matrix.
 * @param mat Source matrix.
 * @return Copied Mat2x3.
 */
Mat2x3 math_mat2x3_copy_2(Mat2x3 const mat);

/**
 * @brief Construct a raw 2x3 matrix from a raw FSize source array.
 * @param src Raw source array (6 contiguous FSize, column-major).
 * @param dest Destination of 6 contiguous FSize.
 */
void math_mat2x3_make_1(FSize const *const src, FSize *const dest);

/**
 * @brief Construct a 2x3 matrix from a raw FSize source array.
 * @param src Raw source array (6 contiguous FSize, column-major).
 * @return Constructed Mat2x3.
 */
Mat2x3 math_mat2x3_make_2(FSize const *const src);

/**
 * @brief Multiply a raw 2x3 matrix by a raw 3x2 matrix (a * b), yielding a 3x3.
 * @param a Raw left 2x3 matrix (6 contiguous FSize, column-major).
 * @param b Raw right 3x2 matrix (6 contiguous FSize, column-major).
 * @param dest Destination 3x3 matrix of 9 contiguous FSize.
 */
void math_mat2x3_mul_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the product of a 2x3 and a 3x2 matrix (a * b) as a 3x3 matrix.
 * @param a Left 2x3 matrix.
 * @param b Right 3x2 matrix.
 * @return Product Mat3.
 */
Mat3 math_mat2x3_mul_2(Mat2x3 const a, Mat3x2 const b);

/**
 * @brief Multiply a raw 2x3 matrix by a raw 2D vector (m * v), yielding a 3D vector.
 * @param m Raw matrix (6 contiguous FSize, column-major).
 * @param v Raw vector (2 contiguous FSize).
 * @param dest Destination vector of 3 contiguous FSize.
 */
void math_mat2x3_mulv_1(FSize const *const m, FSize const *const v, FSize *const dest);

/**
 * @brief Return the product of a 2x3 matrix and a 2D vector (m * v) as a 3D vector.
 * @param m Matrix.
 * @param v Vector.
 * @return Result Vec3.
 */
Vec3 math_mat2x3_mulv_2(Mat2x3 const m, Vec2 const v);

/**
 * @brief Scale every element of a raw 2x3 matrix by a scalar.
 * @param mat Raw source matrix (6 contiguous FSize, column-major).
 * @param s Scalar factor.
 * @param dest Destination of 6 contiguous FSize.
 */
void math_mat2x3_scale_1(FSize const *const mat, FSize const s, FSize *const dest);

/**
 * @brief Return a 2x3 matrix with every element scaled by a scalar.
 * @param mat Source matrix.
 * @param s Scalar factor.
 * @return Scaled Mat2x3.
 */
Mat2x3 math_mat2x3_scale_2(Mat2x3 const mat, FSize const s);

/**
 * @brief Transpose of a raw 2x3 matrix, yielding a 3x2 matrix.
 * @param mat Raw source 2x3 matrix (6 contiguous FSize, column-major).
 * @param dest Destination 3x2 matrix of 6 contiguous FSize.
 */
void math_mat2x3_transpose_1(FSize const *const mat, FSize *const dest);

/**
 * @brief Return the transpose of a 2x3 matrix as a 3x2 matrix.
 * @param mat Source matrix.
 * @return Transposed Mat3x2.
 */
Mat3x2 math_mat2x3_transpose_2(Mat2x3 const mat);

/**
 * @brief Write the 2x3 zero matrix into a raw destination.
 * @param dest Destination of 6 contiguous FSize.
 */
void math_mat2x3_zero_1(FSize *const dest);

/**
 * @brief Return the 2x3 zero matrix.
 * @return Zero Mat2x3.
 */
Mat2x3 math_mat2x3_zero_2(void);

#endif // MATH_MAT2X3_H