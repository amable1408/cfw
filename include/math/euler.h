/*
 * euler.h - Euler-angle rotation builders for the CFW math module
 *
 * Features:
 *   - Full coverage of cglm's compiled glmc_euler* API: build a rotation matrix
 *     from euler angles in the default (XYZ) order, in each of the six explicit
 *     orders (xyz/zyx/zxy/xzy/yzx/yxz), by a runtime order argument, or as a
 *     quaternion; plus extraction of euler angles back out of a matrix
 *   - Root-first variants: _1 raw FSize-array underset, _2 framework-struct superset
 *   - Pure-value semantics: no wrapper mutates its argument in place; the raw (_1)
 *     variant writes a caller-supplied destination, the struct (_2) variant returns
 *     a Mat4/Vec3/Quat value
 *
 * Usage Examples:
 *   @code
 *   Vec3 const angles = { 0.0, 0.0, MATH_PI / 2.0 };
 *   Mat4 const rot    = math_euler_xyz_2(angles);
 *   Vec3 const back   = math_euler_angles_2(rot);
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
 *     glmc_* routine. cglm's euler routines are scalar C behind a non-inlinable call -
 *     only its vec4/mat4 paths are SIMD-accelerated.
 *   - Precision: every value crosses F64 -> float -> F64; see the Precision section
 *     in types.h for what that loses and where it flips a result.
 *
 * Dependencies:
 *   - <math/types.h> for the Vec3/Mat4/Quat types, the raw<->cglm bridges,
 *     cglm, and the error/tracing macros.
 *
 * See euler.c for implementation details.
 */

#ifndef MATH_EULER_H
#define MATH_EULER_H

#include <math/types.h>

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief Rotation order for math_euler_by_order_*.
 *
 * The values are cglm's own glm_euler_seq codes, so the conversion at the cglm boundary is the
 * identity; the typed name is what keeps the public API free of cglm's macro names.
 */
typedef enum MathEulerOrder {
    MATH_EULER_ORDER_XYZ = GLM_EULER_XYZ,
    MATH_EULER_ORDER_XZY = GLM_EULER_XZY,
    MATH_EULER_ORDER_YXZ = GLM_EULER_YXZ,
    MATH_EULER_ORDER_YZX = GLM_EULER_YZX,
    MATH_EULER_ORDER_ZXY = GLM_EULER_ZXY,
    MATH_EULER_ORDER_ZYX = GLM_EULER_ZYX
} MathEulerOrder;

/*==============================================================================
 * MARK: - Euler API
 *
 * Angle inputs are 3 contiguous FSize (raw _1) or a Vec3 (struct _2) ordered
 * [Xangle, Yangle, Zangle]. Matrix results are 16 contiguous column-major FSize
 * (raw) or a Mat4 (struct); quaternion results are 4 contiguous FSize (raw) or a
 * Quat (struct). math_euler_angles inverts the build, reading a matrix and
 * producing the [x, y, z] angles.
 *============================================================================*/

/**
 * @brief Build a raw rotation matrix from raw euler angles (default XYZ order).
 * @param angles Raw euler angles [x, y, z] (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major 4x4).
 */
void math_euler_1(FSize const *const angles, FSize *const dest);

/**
 * @brief Return a rotation matrix built from euler angles (default XYZ order).
 * @param angles Euler angles [x, y, z].
 * @return Rotation Mat4.
 */
Mat4 math_euler_2(Vec3 const angles);

/**
 * @brief Extract raw euler angles [x, y, z] from a raw rotation matrix.
 * @param m Raw 4x4 matrix (16 contiguous FSize, column-major).
 * @param dest Destination of 3 contiguous FSize [x, y, z].
 */
void math_euler_angles_1(FSize const *const m, FSize *const dest);

/**
 * @brief Extract the euler angles [x, y, z] from a rotation matrix.
 * @param m 4x4 rotation matrix.
 * @return Euler-angle Vec3 [x, y, z].
 */
Vec3 math_euler_angles_2(Mat4 const m);

/**
 * @brief Build a raw rotation matrix from raw euler angles in a runtime order.
 * @param angles Raw euler angles [x, y, z] (3 contiguous FSize).
 * @param order Rotation order; a value outside MathEulerOrder yields a zero rotation block
 *        with w = 1, i.e. diag(0, 0, 0, 1) (cglm's dispatch has no default case). Range-check an
 *        order that came from data against the named enumerators BEFORE casting it - the cast
 *        itself is unspecified for a value outside the enum, so the fallback is not a contract.
 * @param dest Destination of 16 contiguous FSize (column-major 4x4).
 */
void math_euler_by_order_1(FSize const *const angles, MathEulerOrder const order, FSize *const dest);

/**
 * @brief Return a rotation matrix built from euler angles in a runtime order.
 * @param angles Euler angles [x, y, z].
 * @param order Rotation order; a value outside MathEulerOrder yields diag(0, 0, 0, 1).
 * @return Rotation Mat4.
 */
Mat4 math_euler_by_order_2(Vec3 const angles, MathEulerOrder const order);

/**
 * @brief Build a raw rotation matrix from raw euler angles in XYZ order.
 * @param angles Raw euler angles [x, y, z] (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major 4x4).
 */
void math_euler_xyz_1(FSize const *const angles, FSize *const dest);

/**
 * @brief Return a rotation matrix built from euler angles in XYZ order.
 * @param angles Euler angles [x, y, z].
 * @return Rotation Mat4.
 */
Mat4 math_euler_xyz_2(Vec3 const angles);

/**
 * @brief Build a raw quaternion from raw euler angles in XYZ order.
 * @param angles Raw euler angles [x, y, z] (3 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize (x, y, z, w).
 */
void math_euler_xyz_quat_1(FSize const *const angles, FSize *const dest);

/**
 * @brief Return a quaternion built from euler angles in XYZ order.
 * @param angles Euler angles [x, y, z].
 * @return Rotation Quat.
 */
Quat math_euler_xyz_quat_2(Vec3 const angles);

/**
 * @brief Build a raw rotation matrix from raw euler angles in XZY order.
 * @param angles Raw euler angles [x, y, z] (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major 4x4).
 */
void math_euler_xzy_1(FSize const *const angles, FSize *const dest);

/**
 * @brief Return a rotation matrix built from euler angles in XZY order.
 * @param angles Euler angles [x, y, z].
 * @return Rotation Mat4.
 */
Mat4 math_euler_xzy_2(Vec3 const angles);

/**
 * @brief Build a raw quaternion from raw euler angles in XZY order.
 * @param angles Raw euler angles [x, y, z] (3 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize (x, y, z, w).
 */
void math_euler_xzy_quat_1(FSize const *const angles, FSize *const dest);

/**
 * @brief Return a quaternion built from euler angles in XZY order.
 * @param angles Euler angles [x, y, z].
 * @return Rotation Quat.
 */
Quat math_euler_xzy_quat_2(Vec3 const angles);

/**
 * @brief Build a raw rotation matrix from raw euler angles in YXZ order.
 * @param angles Raw euler angles [x, y, z] (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major 4x4).
 */
void math_euler_yxz_1(FSize const *const angles, FSize *const dest);

/**
 * @brief Return a rotation matrix built from euler angles in YXZ order.
 * @param angles Euler angles [x, y, z].
 * @return Rotation Mat4.
 */
Mat4 math_euler_yxz_2(Vec3 const angles);

/**
 * @brief Build a raw quaternion from raw euler angles in YXZ order.
 * @param angles Raw euler angles [x, y, z] (3 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize (x, y, z, w).
 */
void math_euler_yxz_quat_1(FSize const *const angles, FSize *const dest);

/**
 * @brief Return a quaternion built from euler angles in YXZ order.
 * @param angles Euler angles [x, y, z].
 * @return Rotation Quat.
 */
Quat math_euler_yxz_quat_2(Vec3 const angles);

/**
 * @brief Build a raw rotation matrix from raw euler angles in YZX order.
 * @param angles Raw euler angles [x, y, z] (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major 4x4).
 */
void math_euler_yzx_1(FSize const *const angles, FSize *const dest);

/**
 * @brief Return a rotation matrix built from euler angles in YZX order.
 * @param angles Euler angles [x, y, z].
 * @return Rotation Mat4.
 */
Mat4 math_euler_yzx_2(Vec3 const angles);

/**
 * @brief Build a raw quaternion from raw euler angles in YZX order.
 * @param angles Raw euler angles [x, y, z] (3 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize (x, y, z, w).
 */
void math_euler_yzx_quat_1(FSize const *const angles, FSize *const dest);

/**
 * @brief Return a quaternion built from euler angles in YZX order.
 * @param angles Euler angles [x, y, z].
 * @return Rotation Quat.
 */
Quat math_euler_yzx_quat_2(Vec3 const angles);

/**
 * @brief Build a raw rotation matrix from raw euler angles in ZXY order.
 * @param angles Raw euler angles [x, y, z] (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major 4x4).
 */
void math_euler_zxy_1(FSize const *const angles, FSize *const dest);

/**
 * @brief Return a rotation matrix built from euler angles in ZXY order.
 * @param angles Euler angles [x, y, z].
 * @return Rotation Mat4.
 */
Mat4 math_euler_zxy_2(Vec3 const angles);

/**
 * @brief Build a raw quaternion from raw euler angles in ZXY order.
 * @param angles Raw euler angles [x, y, z] (3 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize (x, y, z, w).
 */
void math_euler_zxy_quat_1(FSize const *const angles, FSize *const dest);

/**
 * @brief Return a quaternion built from euler angles in ZXY order.
 * @param angles Euler angles [x, y, z].
 * @return Rotation Quat.
 */
Quat math_euler_zxy_quat_2(Vec3 const angles);

/**
 * @brief Build a raw rotation matrix from raw euler angles in ZYX order.
 * @param angles Raw euler angles [x, y, z] (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major 4x4).
 */
void math_euler_zyx_1(FSize const *const angles, FSize *const dest);

/**
 * @brief Return a rotation matrix built from euler angles in ZYX order.
 * @param angles Euler angles [x, y, z].
 * @return Rotation Mat4.
 */
Mat4 math_euler_zyx_2(Vec3 const angles);

/**
 * @brief Build a raw quaternion from raw euler angles in ZYX order.
 * @param angles Raw euler angles [x, y, z] (3 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize (x, y, z, w).
 */
void math_euler_zyx_quat_1(FSize const *const angles, FSize *const dest);

/**
 * @brief Return a quaternion built from euler angles in ZYX order.
 * @param angles Euler angles [x, y, z].
 * @return Rotation Quat.
 */
Quat math_euler_zyx_quat_2(Vec3 const angles);

#endif // MATH_EULER_H