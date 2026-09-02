/*
 * mat4.h - 4x4 matrix operations for the CFW math module
 *
 * Features:
 *   - Full coverage of cglm's compiled glmc_mat4_* API: construction, copy,
 *     identity/zero, sub-matrix pick/insert, multiply, matrix-vector products,
 *     trace, quaternion extraction, transpose, scale, determinant, inverse
 *     (fast/precise), column/row swap, row-matrix-column, raw make, and the
 *     texture transform
 *   - Root-first variants: _1 raw FSize-array underset, _2 framework-struct superset
 *   - Pure-value semantics: no wrapper mutates its argument in place; the raw (_1)
 *     variant writes a caller-supplied destination, the struct (_2) variant returns
 *     a Mat4 (or a Vec3/Vec4/Mat3/Quat/FSize for cross-type or scalar results)
 *
 * Usage Examples:
 *   @code
 *   Mat4 const id  = math_mat4_identity_2();
 *   Mat4 const inv = math_mat4_inv_2(id);
 *   @endcode
 *
 * Error Handling:
 *   - Pointer (_1) variants validate every pointer with error_check_null.
 *   - Struct (_2) variants take values, so there is no pointer to validate.
 *   - swap_col / swap_row: an index outside [0, 4) ABORTS (error_check_out_of_bound_int)
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
 *     glmc_* routine; cglm's mat4 paths are SIMD-accelerated (SSE2/AVX) where the
 *     build enables them.
 *   - Precision: every value crosses F64 -> float -> F64; see the Precision section
 *     in types.h for what that loses and where it flips a result.
 *
 * Dependencies:
 *   - <math/types.h> for the Mat4/Mat3/Vec3/Vec4/Quat types, the raw<->cglm
 *     bridges, cglm, and the error/tracing macros.
 *
 * See mat4.c for implementation details.
 */

#ifndef MATH_MAT4_H
#define MATH_MAT4_H

#include <math/cglm_compat.h>
#include <math/types.h>

/*==============================================================================
 * MARK: - Mat4 API
 *
 * Raw (_1) variants read and write 16 contiguous FSize in column-major order; the
 * struct (_2) variants read and return a Mat4 value. Cross-type ops use the
 * matching raw length (9 for a mat3, 4 for a vec4, 3 for a vec3) in the _1 form
 * and the matching struct (Mat3/Vec4/Vec3/Quat) in the _2 form.
 *============================================================================*/

/**
 * @brief Copy a raw 4x4 matrix.
 * @param mat Raw source matrix (16 contiguous FSize, column-major).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_mat4_copy_1(FSize const *const mat, FSize *const dest);

/**
 * @brief Return a copy of a 4x4 matrix.
 * @param mat Source matrix.
 * @return Copied Mat4.
 */
Mat4 math_mat4_copy_2(Mat4 const mat);

/**
 * @brief Determinant of a raw 4x4 matrix.
 * @param mat Raw source matrix (16 contiguous FSize, column-major).
 * @return Determinant as FSize.
 */
FSize math_mat4_det_1(FSize const *const mat);

/**
 * @brief Determinant of a 4x4 matrix.
 * @param mat Source matrix.
 * @return Determinant as FSize.
 */
FSize math_mat4_det_2(Mat4 const mat);

/**
 * @brief Write the 4x4 identity matrix into a raw destination.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_mat4_identity_1(FSize *const dest);

/**
 * @brief Return the 4x4 identity matrix.
 * @return Identity Mat4.
 */
Mat4 math_mat4_identity_2(void);

/**
 * @brief Insert a raw 3x3 matrix into the upper-left of a raw 4x4 matrix.
 * @param mat Raw source 3x3 matrix (9 contiguous FSize, column-major).
 * @param dest In/out 4x4 matrix (16 contiguous FSize, column-major): READ as well as
 *        written, so initialize it before the call; its upper-left 3x3 block is
 *        overwritten, the rest is carried through.
 */
void math_mat4_ins3_1(FSize const *const mat, FSize *const dest);

/**
 * @brief Return a 4x4 matrix with a 3x3 matrix inserted into its upper-left block.
 * @param mat Source 3x3 matrix.
 * @param accumulator Base 4x4 matrix whose non upper-left entries are preserved.
 * @return Combined Mat4.
 */
Mat4 math_mat4_ins3_2(Mat3 const mat, Mat4 const accumulator);

/**
 * @brief Invert a raw 4x4 matrix.
 * @param mat Raw source matrix (16 contiguous FSize, column-major).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_mat4_inv_1(FSize const *const mat, FSize *const dest);

/**
 * @brief Return the inverse of a 4x4 matrix.
 * @param mat Source matrix.
 * @return Inverse Mat4.
 */
Mat4 math_mat4_inv_2(Mat4 const mat);

/**
 * @brief Invert a raw 4x4 matrix using the fast (less accurate) path.
 * @param mat Raw source matrix (16 contiguous FSize, column-major).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_mat4_inv_fast_1(FSize const *const mat, FSize *const dest);

/**
 * @brief Return the inverse of a 4x4 matrix using the fast (less accurate) path.
 * @param mat Source matrix.
 * @return Inverse Mat4.
 */
Mat4 math_mat4_inv_fast_2(Mat4 const mat);

/**
 * @brief Invert a raw 4x4 matrix using the precise (more accurate) path.
 * @param mat Raw source matrix (16 contiguous FSize, column-major).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_mat4_inv_precise_1(FSize const *const mat, FSize *const dest);

/**
 * @brief Return the inverse of a 4x4 matrix using the precise (more accurate) path.
 * @param mat Source matrix.
 * @return Inverse Mat4.
 */
Mat4 math_mat4_inv_precise_2(Mat4 const mat);

/**
 * @brief Construct a raw 4x4 matrix from a raw FSize source array.
 * @param src Raw source array (16 contiguous FSize, column-major).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_mat4_make_1(FSize const *const src, FSize *const dest);

/**
 * @brief Construct a 4x4 matrix from a raw FSize source array.
 * @param src Raw source array (16 contiguous FSize, column-major).
 * @return Constructed Mat4.
 */
Mat4 math_mat4_make_2(FSize const *const src);

/**
 * @brief Multiply two raw 4x4 matrices (a * b).
 * @param a Raw left matrix (16 contiguous FSize, column-major).
 * @param b Raw right matrix (16 contiguous FSize, column-major).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_mat4_mul_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the product of two 4x4 matrices (a * b).
 * @param a Left matrix.
 * @param b Right matrix.
 * @return Product Mat4.
 */
Mat4 math_mat4_mul_2(Mat4 const a, Mat4 const b);

/**
 * @brief Multiply a raw 4x4 matrix by a raw 4D vector (m * v).
 * @param m Raw matrix (16 contiguous FSize, column-major).
 * @param v Raw vector (4 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize.
 */
void math_mat4_mulv_1(FSize const *const m, FSize const *const v, FSize *const dest);

/**
 * @brief Return the product of a 4x4 matrix and a 4D vector (m * v).
 * @param m Matrix.
 * @param v Vector.
 * @return Product Vec4.
 */
Vec4 math_mat4_mulv_2(Mat4 const m, Vec4 const v);

/**
 * @brief Multiply a raw 4x4 matrix by a raw 3D vector with an explicit w (m * [v, last]).
 * @param m Raw matrix (16 contiguous FSize, column-major).
 * @param v Raw vector (3 contiguous FSize).
 * @param last Fourth (w) component supplied for the vector.
 * @param dest Destination of 3 contiguous FSize.
 */
void math_mat4_mulv3_1(FSize const *const m, FSize const *const v, FSize const last, FSize *const dest);

/**
 * @brief Return the product of a 4x4 matrix and a 3D vector with an explicit w.
 * @param m Matrix.
 * @param v Vector.
 * @param last Fourth (w) component supplied for the vector.
 * @return Product Vec3.
 */
Vec3 math_mat4_mulv3_2(Mat4 const m, Vec3 const v, FSize const last);

/**
 * @brief Extract the upper-left 3x3 block of a raw 4x4 matrix.
 * @param mat Raw source matrix (16 contiguous FSize, column-major).
 * @param dest Destination 3x3 matrix (9 contiguous FSize, column-major).
 */
void math_mat4_pick3_1(FSize const *const mat, FSize *const dest);

/**
 * @brief Return the upper-left 3x3 block of a 4x4 matrix.
 * @param mat Source matrix.
 * @return Extracted Mat3.
 */
Mat3 math_mat4_pick3_2(Mat4 const mat);

/**
 * @brief Extract the transpose of the upper-left 3x3 block of a raw 4x4 matrix.
 * @param mat Raw source matrix (16 contiguous FSize, column-major).
 * @param dest Destination 3x3 matrix (9 contiguous FSize, column-major).
 */
void math_mat4_pick3t_1(FSize const *const mat, FSize *const dest);

/**
 * @brief Return the transpose of the upper-left 3x3 block of a 4x4 matrix.
 * @param mat Source matrix.
 * @return Extracted (transposed) Mat3.
 */
Mat3 math_mat4_pick3t_2(Mat4 const mat);

/**
 * @brief Extract a quaternion from the rotation part of a raw 4x4 matrix.
 * @param m Raw source matrix (16 contiguous FSize, column-major).
 * @param dest Destination quaternion (4 contiguous FSize, x, y, z, w).
 */
void math_mat4_quat_1(FSize const *const m, FSize *const dest);

/**
 * @brief Return the quaternion of the rotation part of a 4x4 matrix.
 * @param m Source matrix.
 * @return Extracted Quat.
 */
Quat math_mat4_quat_2(Mat4 const m);

/**
 * @brief Row-vector times matrix times column-vector (r * M * c) for raw inputs.
 * @param r Raw row vector (4 contiguous FSize).
 * @param m Raw matrix (16 contiguous FSize, column-major).
 * @param c Raw column vector (4 contiguous FSize).
 * @return Scalar result as FSize.
 */
FSize math_mat4_rmc_1(FSize const *const r, FSize const *const m, FSize const *const c);

/**
 * @brief Row-vector times matrix times column-vector (r * M * c).
 * @param r Row vector.
 * @param m Matrix.
 * @param c Column vector.
 * @return Scalar result as FSize.
 */
FSize math_mat4_rmc_2(Vec4 const r, Mat4 const m, Vec4 const c);

/**
 * @brief Scale every entry of a raw 4x4 matrix by a scalar.
 * @param mat Raw source matrix (16 contiguous FSize, column-major).
 * @param s Scalar factor.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_mat4_scale_1(FSize const *const mat, FSize const s, FSize *const dest);

/**
 * @brief Return a 4x4 matrix with every entry scaled by a scalar.
 * @param mat Source matrix.
 * @param s Scalar factor.
 * @return Scaled Mat4.
 */
Mat4 math_mat4_scale_2(Mat4 const mat, FSize const s);

/**
 * @brief Scale every entry of a raw 4x4 matrix by a scalar (plain, non-SIMD path).
 * @param m Raw source matrix (16 contiguous FSize, column-major).
 * @param s Scalar factor.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_mat4_scale_p_1(FSize const *const m, FSize const s, FSize *const dest);

/**
 * @brief Return a 4x4 matrix with every entry scaled by a scalar (plain path).
 * @param m Source matrix.
 * @param s Scalar factor.
 * @return Scaled Mat4.
 */
Mat4 math_mat4_scale_p_2(Mat4 const m, FSize const s);

/**
 * @brief Swap two columns of a raw 4x4 matrix.
 * @param mat Raw source matrix (16 contiguous FSize, column-major).
 * @param col1 Index of the first column to swap, in [0, 4); out of range aborts.
 * @param col2 Index of the second column to swap, in [0, 4); out of range aborts.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_mat4_swap_col_1(FSize const *const mat, ISize const col1, ISize const col2, FSize *const dest);

/**
 * @brief Return a 4x4 matrix with two columns swapped.
 * @param mat Source matrix.
 * @param col1 Index of the first column to swap, in [0, 4); out of range aborts.
 * @param col2 Index of the second column to swap, in [0, 4); out of range aborts.
 * @return Column-swapped Mat4.
 */
Mat4 math_mat4_swap_col_2(Mat4 const mat, ISize const col1, ISize const col2);

/**
 * @brief Swap two rows of a raw 4x4 matrix.
 * @param mat Raw source matrix (16 contiguous FSize, column-major).
 * @param row1 Index of the first row to swap, in [0, 4); out of range aborts.
 * @param row2 Index of the second row to swap, in [0, 4); out of range aborts.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_mat4_swap_row_1(FSize const *const mat, ISize const row1, ISize const row2, FSize *const dest);

/**
 * @brief Return a 4x4 matrix with two rows swapped.
 * @param mat Source matrix.
 * @param row1 Index of the first row to swap, in [0, 4); out of range aborts.
 * @param row2 Index of the second row to swap, in [0, 4); out of range aborts.
 * @return Row-swapped Mat4.
 */
Mat4 math_mat4_swap_row_2(Mat4 const mat, ISize const row1, ISize const row2);

/**
 * @brief Build a raw 4x4 texture transform matrix.
 * @param sx Scale on the s axis.
 * @param sy Scale on the t axis.
 * @param rot Rotation angle in radians.
 * @param tx Translation on the s axis.
 * @param ty Translation on the t axis.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_mat4_textrans_1(FSize const sx, FSize const sy, FSize const rot, FSize const tx, FSize const ty, FSize *const dest);

/**
 * @brief Return a 4x4 texture transform matrix.
 * @param sx Scale on the s axis.
 * @param sy Scale on the t axis.
 * @param rot Rotation angle in radians.
 * @param tx Translation on the s axis.
 * @param ty Translation on the t axis.
 * @return Texture transform Mat4.
 */
Mat4 math_mat4_textrans_2(FSize const sx, FSize const sy, FSize const rot, FSize const tx, FSize const ty);

/**
 * @brief Trace (sum of the diagonal) of a raw 4x4 matrix.
 * @param mat Raw source matrix (16 contiguous FSize, column-major).
 * @return Trace as FSize.
 */
FSize math_mat4_trace_1(FSize const *const mat);

/**
 * @brief Trace (sum of the diagonal) of a 4x4 matrix.
 * @param mat Source matrix.
 * @return Trace as FSize.
 */
FSize math_mat4_trace_2(Mat4 const mat);

/**
 * @brief Trace of the upper-left 3x3 block of a raw 4x4 matrix.
 * @param m Raw source matrix (16 contiguous FSize, column-major).
 * @return 3x3 trace as FSize.
 */
FSize math_mat4_trace3_1(FSize const *const m);

/**
 * @brief Trace of the upper-left 3x3 block of a 4x4 matrix.
 * @param m Source matrix.
 * @return 3x3 trace as FSize.
 */
FSize math_mat4_trace3_2(Mat4 const m);

/**
 * @brief Transpose a raw 4x4 matrix.
 * @param mat Raw source matrix (16 contiguous FSize, column-major).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_mat4_transpose_1(FSize const *const mat, FSize *const dest);

/**
 * @brief Return the transpose of a 4x4 matrix.
 * @param mat Source matrix.
 * @return Transposed Mat4.
 */
Mat4 math_mat4_transpose_2(Mat4 const mat);

/**
 * @brief Write the 4x4 zero matrix into a raw destination.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_mat4_zero_1(FSize *const dest);

/**
 * @brief Return the 4x4 zero matrix.
 * @return Zero Mat4.
 */
Mat4 math_mat4_zero_2(void);

#endif // MATH_MAT4_H