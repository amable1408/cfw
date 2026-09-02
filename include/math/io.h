/*
 * io.h - Log-based printing of CFW math types
 *
 * Features:
 *   - Formats Vec2/Vec3/Vec4, Mat2/Mat3/Mat4, and Quat and emits them through the
 *     framework log (log_message_1 at LOG_LEVEL_INFO)
 *   - No FILE* is taken or written; output routes to the active log stream/buffer,
 *     so printing obeys the process-wide log level and destination
 *   - Root-first variants: _1 raw FSize-array underset, _2 framework-struct superset
 *   - Matrices are printed row-major (human-readable), one matrix row per text line
 *
 * Usage Examples:
 *   @code
 *   Vec3 const v = { 1.0, 2.0, 3.0 };
 *   math_io_vec3_print_2(v);   // logs: vec3(1, 2, 3) - %.17g, round-trip exact (0.1 -> 0.10000000000000001)
 *   @endcode
 *
 * Error Handling:
 *   - Pointer (_1) variants validate the source pointer with error_check_null.
 *   - Struct (_2) variants take values, so there is no pointer to validate.
 *
 * Thread Safety:
 *   - Thread-safe to the same degree as the framework log (see log.h).
 *
 * Memory Management:
 *   - No allocation is performed; formatting is delegated to the log layer.
 *
 * Performance Characteristics:
 *   - One log_message_1 call per print; cost is dominated by the log layer.
 *
 * Dependencies:
 *   - <math/types.h> for the CFW types; it chains in <log/log.h> for log_message_1.
 *
 * See io.c for implementation details.
 */

#ifndef MATH_IO_H
#define MATH_IO_H

#include <math/types.h>

/*==============================================================================
 * MARK: - IO API
 *============================================================================*/

/**
 * @brief Log a raw 4-element (2x2) matrix, row-major.
 * @param self Raw matrix (4 contiguous FSize, column-major storage).
 */
void math_io_mat2_print_1(FSize const *const self);

/**
 * @brief Log a 2x2 matrix, row-major.
 * @param self Source matrix.
 */
void math_io_mat2_print_2(Mat2 const self);

/**
 * @brief Log a raw 9-element (3x3) matrix, row-major.
 * @param self Raw matrix (9 contiguous FSize, column-major storage).
 */
void math_io_mat3_print_1(FSize const *const self);

/**
 * @brief Log a 3x3 matrix, row-major.
 * @param self Source matrix.
 */
void math_io_mat3_print_2(Mat3 const self);

/**
 * @brief Log a raw 16-element (4x4) matrix, row-major.
 * @param self Raw matrix (16 contiguous FSize, column-major storage).
 */
void math_io_mat4_print_1(FSize const *const self);

/**
 * @brief Log a 4x4 matrix, row-major.
 * @param self Source matrix.
 */
void math_io_mat4_print_2(Mat4 const self);

/**
 * @brief Log a raw quaternion as (x, y, z, w).
 * @param self Raw quaternion (4 contiguous FSize).
 */
void math_io_quat_print_1(FSize const *const self);

/**
 * @brief Log a quaternion as (x, y, z, w).
 * @param self Source quaternion.
 */
void math_io_quat_print_2(Quat const self);

/**
 * @brief Log a raw 2D vector.
 * @param self Raw vector (2 contiguous FSize).
 */
void math_io_vec2_print_1(FSize const *const self);

/**
 * @brief Log a 2D vector.
 * @param self Source vector.
 */
void math_io_vec2_print_2(Vec2 const self);

/**
 * @brief Log a raw 3D vector.
 * @param self Raw vector (3 contiguous FSize).
 */
void math_io_vec3_print_1(FSize const *const self);

/**
 * @brief Log a 3D vector.
 * @param self Source vector.
 */
void math_io_vec3_print_2(Vec3 const self);

/**
 * @brief Log a raw 4D vector.
 * @param self Raw vector (4 contiguous FSize).
 */
void math_io_vec4_print_1(FSize const *const self);

/**
 * @brief Log a 4D vector.
 * @param self Source vector.
 */
void math_io_vec4_print_2(Vec4 const self);

#endif // MATH_IO_H