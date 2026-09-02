/*
 * mat3.h - 3x3 matrix operations for the CFW math module
 *
 * Features:
 *   - Full coverage of cglm's compiled glmc_mat3_* API: construction (identity,
 *     zero, make, textrans), copy, arithmetic (mul, scale, transpose, inverse),
 *     matrix-vector product (mulv), row-matrix-column product (rmc), scalars
 *     (det, trace), quaternion extraction (quat), and column/row swaps
 *   - Root-first variants: _1 raw FSize-array underset, _2 framework-struct superset
 *   - Pure-value semantics: no wrapper mutates its argument in place; the raw (_1)
 *     variant writes a caller-supplied destination, the struct (_2) variant returns
 *     a Mat3 (or a Vec3/Quat/FSize for cross-type or scalar results); cglm's in-place
 *     ops are exposed as pure producers, as in mat2.h and mat4.h
 *
 * Usage Examples:
 *   @code
 *   Mat3 const id  = math_mat3_identity_2();
 *   Mat3 const inv = math_mat3_inv_2(id);
 *   Vec3 const v   = { 1.0, 2.0, 3.0 };
 *   Vec3 const mv  = math_mat3_mulv_2(id, v);
 *   @endcode
 *
 * Error Handling:
 *   - Pointer (_1) variants validate every pointer with error_check_null.
 *   - Struct (_2) variants take values, so there is no pointer to validate.
 *   - swap_col / swap_row: an index outside [0, 3) ABORTS (error_check_out_of_bound_int)
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
 *     glmc_* routine. cglm's mat3 routines are scalar C behind a non-inlinable call -
 *     only its vec4/mat4 paths are SIMD-accelerated.
 *   - Precision: every value crosses F64 -> float -> F64; see the Precision section
 *     in types.h for what that loses and where it flips a result.
 *
 * Dependencies:
 *   - <math/types.h> for the Mat3, Vec3, and Quat types, the raw<->cglm bridges,
 *     cglm, and the error/tracing macros.
 *
 * See mat3.c for implementation details.
 */

#ifndef MATH_MAT3_H
#define MATH_MAT3_H

#include <math/cglm_compat.h>
#include <math/types.h>

/*==============================================================================
 * MARK: - Mat3 API
 *
 * Raw (_1) variants read and write 9 contiguous FSize in column-major order; the
 * struct (_2) variants read and return a Mat3 value. Cross-type ops (mulv, rmc,
 * quat) read/return Vec3 or Quat values, and scalar ops (det, trace) return an
 * FSize.
 *============================================================================*/

/**
 * @brief Copy a raw 3x3 matrix.
 * @param mat Source of 9 contiguous FSize (column-major).
 * @param dest Destination of 9 contiguous FSize (column-major).
 */
void math_mat3_copy_1(FSize const *const mat, FSize *const dest);

/**
 * @brief Return a copy of a 3x3 matrix.
 * @param mat Source matrix.
 * @return Copied Mat3.
 */
Mat3 math_mat3_copy_2(Mat3 const mat);

/**
 * @brief Determinant of a raw 3x3 matrix.
 * @param mat Source of 9 contiguous FSize (column-major).
 * @return Determinant as FSize.
 */
FSize math_mat3_det_1(FSize const *const mat);

/**
 * @brief Determinant of a 3x3 matrix.
 * @param mat Source matrix.
 * @return Determinant as FSize.
 */
FSize math_mat3_det_2(Mat3 const mat);

/**
 * @brief Write the 3x3 identity matrix into a raw destination.
 * @param dest Destination of 9 contiguous FSize (column-major).
 */
void math_mat3_identity_1(FSize *const dest);

/**
 * @brief Return the 3x3 identity matrix.
 * @return Identity Mat3.
 */
Mat3 math_mat3_identity_2(void);

/**
 * @brief Invert a raw 3x3 matrix.
 * @param mat Source of 9 contiguous FSize (column-major).
 * @param dest Destination of 9 contiguous FSize (column-major).
 */
void math_mat3_inv_1(FSize const *const mat, FSize *const dest);

/**
 * @brief Return the inverse of a 3x3 matrix.
 * @param mat Source matrix.
 * @return Inverse Mat3.
 */
Mat3 math_mat3_inv_2(Mat3 const mat);

/**
 * @brief Construct a raw 3x3 matrix from a raw FSize source array.
 * @param src Raw source array (9 contiguous FSize, column-major).
 * @param dest Destination of 9 contiguous FSize (column-major).
 */
void math_mat3_make_1(FSize const *const src, FSize *const dest);

/**
 * @brief Construct a 3x3 matrix from a raw FSize source array.
 * @param src Raw source array (9 contiguous FSize, column-major).
 * @return Constructed Mat3.
 */
Mat3 math_mat3_make_2(FSize const *const src);

/**
 * @brief Multiply two raw 3x3 matrices (a * b).
 * @param a Raw left matrix (9 contiguous FSize, column-major).
 * @param b Raw right matrix (9 contiguous FSize, column-major).
 * @param dest Destination of 9 contiguous FSize (column-major).
 */
void math_mat3_mul_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the product of two 3x3 matrices (a * b).
 * @param a Left matrix.
 * @param b Right matrix.
 * @return Product Mat3.
 */
Mat3 math_mat3_mul_2(Mat3 const a, Mat3 const b);

/**
 * @brief Multiply a raw 3x3 matrix by a raw 3D vector (m * v).
 * @param m Raw matrix (9 contiguous FSize, column-major).
 * @param v Raw vector (3 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_mat3_mulv_1(FSize const *const m, FSize const *const v, FSize *const dest);

/**
 * @brief Return the product of a 3x3 matrix and a 3D vector (m * v).
 * @param m Matrix.
 * @param v Vector.
 * @return Transformed Vec3.
 */
Vec3 math_mat3_mulv_2(Mat3 const m, Vec3 const v);

/**
 * @brief Extract the rotation quaternion of a raw 3x3 matrix.
 * @param m Raw matrix (9 contiguous FSize, column-major).
 * @param dest Destination of 4 contiguous FSize (x, y, z, w).
 */
void math_mat3_quat_1(FSize const *const m, FSize *const dest);

/**
 * @brief Return the rotation quaternion of a 3x3 matrix.
 * @param m Source matrix.
 * @return Rotation Quat.
 */
Quat math_mat3_quat_2(Mat3 const m);

/**
 * @brief Row-matrix-column product of raw operands (r * M * c).
 * @param r Raw row vector (3 contiguous FSize).
 * @param m Raw matrix (9 contiguous FSize, column-major).
 * @param c Raw column vector (3 contiguous FSize).
 * @return Scalar product as FSize.
 */
FSize math_mat3_rmc_1(FSize const *const r, FSize const *const m, FSize const *const c);

/**
 * @brief Row-matrix-column product (r * M * c).
 * @param r Row vector.
 * @param m Matrix.
 * @param c Column vector.
 * @return Scalar product as FSize.
 */
FSize math_mat3_rmc_2(Vec3 const r, Mat3 const m, Vec3 const c);

/**
 * @brief Scale every element of a raw 3x3 matrix by a scalar.
 * @param mat Source of 9 contiguous FSize (column-major).
 * @param s Scalar factor.
 * @param dest Destination of 9 contiguous FSize (column-major).
 */
void math_mat3_scale_1(FSize const *const mat, FSize const s, FSize *const dest);

/**
 * @brief Return a 3x3 matrix with every element scaled by a scalar.
 * @param mat Source matrix.
 * @param s Scalar factor.
 * @return Scaled Mat3.
 */
Mat3 math_mat3_scale_2(Mat3 const mat, FSize const s);

/**
 * @brief Swap two columns of a raw 3x3 matrix.
 * @param mat Source of 9 contiguous FSize (column-major).
 * @param col1 First column index (0..2); out of range aborts.
 * @param col2 Second column index (0..2); out of range aborts.
 * @param dest Destination of 9 contiguous FSize (column-major).
 */
void math_mat3_swap_col_1(FSize const *const mat, ISize const col1, ISize const col2, FSize *const dest);

/**
 * @brief Return a 3x3 matrix with two columns swapped.
 * @param mat Source matrix.
 * @param col1 First column index (0..2); out of range aborts.
 * @param col2 Second column index (0..2); out of range aborts.
 * @return Column-swapped Mat3.
 */
Mat3 math_mat3_swap_col_2(Mat3 const mat, ISize const col1, ISize const col2);

/**
 * @brief Swap two rows of a raw 3x3 matrix.
 * @param mat Source of 9 contiguous FSize (column-major).
 * @param row1 First row index (0..2); out of range aborts.
 * @param row2 Second row index (0..2); out of range aborts.
 * @param dest Destination of 9 contiguous FSize (column-major).
 */
void math_mat3_swap_row_1(FSize const *const mat, ISize const row1, ISize const row2, FSize *const dest);

/**
 * @brief Return a 3x3 matrix with two rows swapped.
 * @param mat Source matrix.
 * @param row1 First row index (0..2); out of range aborts.
 * @param row2 Second row index (0..2); out of range aborts.
 * @return Row-swapped Mat3.
 */
Mat3 math_mat3_swap_row_2(Mat3 const mat, ISize const row1, ISize const row2);

/**
 * @brief Build a raw 2D texture-transform matrix (scale, rotate, translate).
 * @param sx Scale along x.
 * @param sy Scale along y.
 * @param rot Rotation in radians.
 * @param tx Translation along x.
 * @param ty Translation along y.
 * @param dest Destination of 9 contiguous FSize (column-major).
 */
void math_mat3_textrans_1(FSize const sx, FSize const sy, FSize const rot, FSize const tx, FSize const ty, FSize *const dest);

/**
 * @brief Return a 2D texture-transform matrix (scale, rotate, translate).
 * @param sx Scale along x.
 * @param sy Scale along y.
 * @param rot Rotation in radians.
 * @param tx Translation along x.
 * @param ty Translation along y.
 * @return Texture-transform Mat3.
 */
Mat3 math_mat3_textrans_2(FSize const sx, FSize const sy, FSize const rot, FSize const tx, FSize const ty);

/**
 * @brief Trace (sum of the diagonal) of a raw 3x3 matrix.
 * @param mat Raw matrix (9 contiguous FSize, column-major).
 * @return Trace as FSize.
 */
FSize math_mat3_trace_1(FSize const *const mat);

/**
 * @brief Trace (sum of the diagonal) of a 3x3 matrix.
 * @param mat Source matrix.
 * @return Trace as FSize.
 */
FSize math_mat3_trace_2(Mat3 const mat);

/**
 * @brief Transpose a raw 3x3 matrix.
 * @param mat Source of 9 contiguous FSize (column-major).
 * @param dest Destination of 9 contiguous FSize (column-major).
 */
void math_mat3_transpose_1(FSize const *const mat, FSize *const dest);

/**
 * @brief Return the transpose of a 3x3 matrix.
 * @param mat Source matrix.
 * @return Transposed Mat3.
 */
Mat3 math_mat3_transpose_2(Mat3 const mat);

/**
 * @brief Write the 3x3 zero matrix into a raw destination.
 * @param dest Destination of 9 contiguous FSize (column-major).
 */
void math_mat3_zero_1(FSize *const dest);

/**
 * @brief Return the 3x3 zero matrix.
 * @return Zero Mat3.
 */
Mat3 math_mat3_zero_2(void);

#endif // MATH_MAT3_H