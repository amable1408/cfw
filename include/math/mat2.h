/*
 * mat2.h - 2x2 matrix operations for the CFW math module
 *
 * Features:
 *   - Full coverage of cglm's compiled glmc_mat2_* API: construction, copy,
 *     identity/zero producers, multiply, matrix-vector multiply, transpose,
 *     inverse, scale, column/row swap, trace, determinant, and row-matrix-column
 *   - Root-first variants: _1 raw FSize-array underset, _2 framework-struct superset
 *   - Pure-value semantics: no wrapper mutates its argument in place; the raw (_1)
 *     variant writes a caller-supplied destination, the struct (_2) variant returns
 *     a Mat2 (or a Vec2/FSize for vector/scalar results)
 *
 * Usage Examples:
 *   @code
 *   Mat2 const a   = { { { 1.0, 0.0 }, { 0.0, 1.0 } } };
 *   Mat2 const b   = { { { 2.0, 0.0 }, { 0.0, 2.0 } } };
 *   Mat2 const prod = math_mat2_mul_2(a, b);
 *   @endcode
 *
 * Error Handling:
 *   - Pointer (_1) variants validate every pointer with error_check_null.
 *   - Struct (_2) variants take values, so there is no pointer to validate.
 *   - swap_col / swap_row: an index outside [0, 2) ABORTS (error_check_out_of_bound_int)
 *     in checked builds; it is a programming error, never a data value. Without the
 *     check cglm would read and write past the matrix.
 *
 * Thread Safety:
 *   - All functions are pure and thread-safe; no shared state is touched.
 *
 * Memory Management:
 *   - No allocation is performed; every function operates on values only.
 *
 * Performance Characteristics:
 *   - Each wrapper converts FSize<->float at the boundary and calls a compiled
 *     glmc_* routine. cglm's mat2 routines are scalar C behind a non-inlinable call -
 *     only its vec4/mat4 paths are SIMD-accelerated.
 *   - Precision: every value crosses F64 -> float -> F64; see the Precision section
 *     in types.h for what that loses and where it flips a result.
 *
 * Dependencies:
 *   - <math/types.h> for the Mat2/Vec2 types, the raw<->cglm bridges, cglm, and
 *     the error/tracing macros.
 *
 * See mat2.c for implementation details.
 */

#ifndef MATH_MAT2_H
#define MATH_MAT2_H

#include <math/types.h>

/*==============================================================================
 * MARK: - Mat2 API
 *
 * Raw (_1) variants read and write 4 contiguous FSize in column-major order
 * (index = col * 2 + row); the struct (_2) variants read and return a Mat2 value.
 * Cross-type ops (mulv, rmc) additionally read/write 2 contiguous FSize as a Vec2.
 *============================================================================*/

/**
 * @brief Copy a raw 2x2 matrix.
 * @param mat Raw source matrix (4 contiguous FSize, column-major).
 * @param dest Destination of 4 contiguous FSize.
 */
void math_mat2_copy_1(FSize const *const mat, FSize *const dest);

/**
 * @brief Return a copy of a 2x2 matrix.
 * @param mat Source matrix.
 * @return Copied Mat2.
 */
Mat2 math_mat2_copy_2(Mat2 const mat);

/**
 * @brief Determinant of a raw 2x2 matrix.
 * @param mat Raw source matrix (4 contiguous FSize, column-major).
 * @return Determinant as FSize.
 */
FSize math_mat2_det_1(FSize const *const mat);

/**
 * @brief Determinant of a 2x2 matrix.
 * @param mat Source matrix.
 * @return Determinant as FSize.
 */
FSize math_mat2_det_2(Mat2 const mat);

/**
 * @brief Write the 2x2 identity matrix into a raw destination.
 * @param dest Destination of 4 contiguous FSize.
 */
void math_mat2_identity_1(FSize *const dest);

/**
 * @brief Return the 2x2 identity matrix.
 * @return Identity Mat2.
 */
Mat2 math_mat2_identity_2(void);

/**
 * @brief Inverse of a raw 2x2 matrix.
 * @param mat Raw source matrix (4 contiguous FSize, column-major).
 * @param dest Destination of 4 contiguous FSize.
 */
void math_mat2_inv_1(FSize const *const mat, FSize *const dest);

/**
 * @brief Return the inverse of a 2x2 matrix.
 * @param mat Source matrix.
 * @return Inverse Mat2.
 */
Mat2 math_mat2_inv_2(Mat2 const mat);

/**
 * @brief Construct a raw 2x2 matrix from a raw FSize source array.
 * @param src Raw source array (4 contiguous FSize, column-major).
 * @param dest Destination of 4 contiguous FSize.
 */
void math_mat2_make_1(FSize const *const src, FSize *const dest);

/**
 * @brief Construct a 2x2 matrix from a raw FSize source array.
 * @param src Raw source array (4 contiguous FSize, column-major).
 * @return Constructed Mat2.
 */
Mat2 math_mat2_make_2(FSize const *const src);

/**
 * @brief Multiply two raw 2x2 matrices (a * b).
 * @param a Raw left matrix (4 contiguous FSize, column-major).
 * @param b Raw right matrix (4 contiguous FSize, column-major).
 * @param dest Destination of 4 contiguous FSize.
 */
void math_mat2_mul_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the product of two 2x2 matrices (a * b).
 * @param a Left matrix.
 * @param b Right matrix.
 * @return Product Mat2.
 */
Mat2 math_mat2_mul_2(Mat2 const a, Mat2 const b);

/**
 * @brief Multiply a raw 2x2 matrix by a raw 2D vector (m * v).
 * @param m Raw matrix (4 contiguous FSize, column-major).
 * @param v Raw vector (2 contiguous FSize).
 * @param dest Destination vector of 2 contiguous FSize.
 */
void math_mat2_mulv_1(FSize const *const m, FSize const *const v, FSize *const dest);

/**
 * @brief Return the product of a 2x2 matrix and a 2D vector (m * v).
 * @param m Matrix.
 * @param v Vector.
 * @return Result Vec2.
 */
Vec2 math_mat2_mulv_2(Mat2 const m, Vec2 const v);

/**
 * @brief Row-matrix-column product r * M * c for raw operands.
 * @param r Raw row vector (2 contiguous FSize).
 * @param m Raw matrix (4 contiguous FSize, column-major).
 * @param c Raw column vector (2 contiguous FSize).
 * @return Scalar result as FSize.
 */
FSize math_mat2_rmc_1(FSize const *const r, FSize const *const m, FSize const *const c);

/**
 * @brief Row-matrix-column product r * M * c.
 * @param r Row vector.
 * @param m Matrix.
 * @param c Column vector.
 * @return Scalar result as FSize.
 */
FSize math_mat2_rmc_2(Vec2 const r, Mat2 const m, Vec2 const c);

/**
 * @brief Scale every element of a raw 2x2 matrix by a scalar.
 * @param mat Raw source matrix (4 contiguous FSize, column-major).
 * @param s Scalar factor.
 * @param dest Destination of 4 contiguous FSize.
 */
void math_mat2_scale_1(FSize const *const mat, FSize const s, FSize *const dest);

/**
 * @brief Return a 2x2 matrix with every element scaled by a scalar.
 * @param mat Source matrix.
 * @param s Scalar factor.
 * @return Scaled Mat2.
 */
Mat2 math_mat2_scale_2(Mat2 const mat, FSize const s);

/**
 * @brief Swap two columns of a raw 2x2 matrix.
 * @param mat Raw source matrix (4 contiguous FSize, column-major).
 * @param col1 First column index (0 or 1); out of range aborts.
 * @param col2 Second column index (0 or 1); out of range aborts.
 * @param dest Destination of 4 contiguous FSize.
 */
void math_mat2_swap_col_1(FSize const *const mat, ISize const col1, ISize const col2, FSize *const dest);

/**
 * @brief Return a 2x2 matrix with two columns swapped.
 * @param mat Source matrix.
 * @param col1 First column index (0 or 1); out of range aborts.
 * @param col2 Second column index (0 or 1); out of range aborts.
 * @return Column-swapped Mat2.
 */
Mat2 math_mat2_swap_col_2(Mat2 const mat, ISize const col1, ISize const col2);

/**
 * @brief Swap two rows of a raw 2x2 matrix.
 * @param mat Raw source matrix (4 contiguous FSize, column-major).
 * @param row1 First row index (0 or 1); out of range aborts.
 * @param row2 Second row index (0 or 1); out of range aborts.
 * @param dest Destination of 4 contiguous FSize.
 */
void math_mat2_swap_row_1(FSize const *const mat, ISize const row1, ISize const row2, FSize *const dest);

/**
 * @brief Return a 2x2 matrix with two rows swapped.
 * @param mat Source matrix.
 * @param row1 First row index (0 or 1); out of range aborts.
 * @param row2 Second row index (0 or 1); out of range aborts.
 * @return Row-swapped Mat2.
 */
Mat2 math_mat2_swap_row_2(Mat2 const mat, ISize const row1, ISize const row2);

/**
 * @brief Trace (sum of the diagonal) of a raw 2x2 matrix.
 * @param mat Raw source matrix (4 contiguous FSize, column-major).
 * @return Trace as FSize.
 */
FSize math_mat2_trace_1(FSize const *const mat);

/**
 * @brief Trace (sum of the diagonal) of a 2x2 matrix.
 * @param mat Source matrix.
 * @return Trace as FSize.
 */
FSize math_mat2_trace_2(Mat2 const mat);

/**
 * @brief Transpose of a raw 2x2 matrix.
 * @param mat Raw source matrix (4 contiguous FSize, column-major).
 * @param dest Destination of 4 contiguous FSize.
 */
void math_mat2_transpose_1(FSize const *const mat, FSize *const dest);

/**
 * @brief Return the transpose of a 2x2 matrix.
 * @param mat Source matrix.
 * @return Transposed Mat2.
 */
Mat2 math_mat2_transpose_2(Mat2 const mat);

/**
 * @brief Write the 2x2 zero matrix into a raw destination.
 * @param dest Destination of 4 contiguous FSize.
 */
void math_mat2_zero_1(FSize *const dest);

/**
 * @brief Return the 2x2 zero matrix.
 * @return Zero Mat2.
 */
Mat2 math_mat2_zero_2(void);

#endif // MATH_MAT2_H