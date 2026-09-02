/*
 * frustum.h - View-frustum extraction for the CFW math module
 *
 * Features:
 *   - Full coverage of cglm's compiled glmc_frustum_* API: plane extraction,
 *     corner extraction, center, transformed bounding box, and split-plane
 *     corners (CSM/PSSM)
 *   - Root-first variants: _1 raw FSize-array underset, _2 framework-struct superset
 *   - Pure-value semantics: no wrapper mutates its argument in place; the raw (_1)
 *     variant writes a caller-supplied destination, the struct (_2) variant returns
 *     a value: Vec3 (center), Box (bounding box), FrustumPlanes (planes),
 *     FrustumCorners (corners) or FrustumSplitCorners (corners_at)
 *
 * The array producers carry both forms: the raw (_1) planes / corners / corners_at write
 *   24 / 32 / 16 contiguous FSize, and the struct (_2) forms return the matching
 *   FrustumPlanes / FrustumCorners / FrustumSplitCorners from types.h, so a producer's
 *   result feeds center_2, box_2 and math_box_frustum_2 without a hand-rolled copy.
 *
 * Note on center: cglm's glmc_frustum_center writes a vec4 (the averaged corner,
 *   with w). The wrapper exposes the spatial point only, as a Vec3 (x, y, z); the
 *   redundant w is dropped.
 *
 * Usage Examples:
 *   @code
 *   FrustumPlanes const planes = math_frustum_planes_2(view_proj);   // [left,right,bottom,top,near,far]
 *   FrustumCorners const corners = math_frustum_corners_2(inv_view_proj);
 *   Vec3 const center = math_frustum_center_2(corners);
 *   @endcode
 *
 * Error Handling:
 *   - Pointer (_1) variants validate every pointer with error_check_null.
 *   - Struct (_2) variants take values, so there is no pointer to validate.
 *   - corners_at REFUSES a far_dist that is not > 0 (cglm divides by it) and returns the
 *     zeroed corners in every build - it is data, not a programming error.
 *
 * Thread Safety:
 *   - All functions are pure and thread-safe; no shared state is touched.
 *
 * Memory Management:
 *   - No allocation is performed; every function operates on caller storage.
 *
 * Performance Characteristics:
 *   - Each wrapper converts FSize<->float at the boundary and calls a compiled
 *     glmc_* routine. cglm's frustum routines are scalar C behind a non-inlinable call -
 *     only its vec4/mat4 paths are SIMD-accelerated.
 *   - Precision: every value crosses F64 -> float -> F64; see the Precision section
 *     in types.h for what that loses and where it flips a result.
 *
 * Dependencies:
 *   - <math/types.h> for the Mat4/Vec3/Vec4/Box types, the raw<->cglm bridges,
 *     cglm, and the error/tracing macros.
 *
 * See frustum.c for implementation details.
 */

#ifndef MATH_FRUSTUM_H
#define MATH_FRUSTUM_H

#include <math/types.h>

/*==============================================================================
 * MARK: - Frustum API
 *
 * Raw (_1) variants read and write contiguous FSize: a matrix is 16 (column-major),
 * the corner set is 32 (8 corners of 4 each), the plane set is 24 (6 planes of 4).
 * Every op carries a _2: the array producers return FrustumPlanes / FrustumCorners /
 * FrustumSplitCorners, the single-valued ops a Vec3 (center) or a Box (box).
 *============================================================================*/

/**
 * @brief Bounding box of frustum corners transformed by a matrix, into raw storage.
 * @param corners Raw frustum corners (32 contiguous FSize, 8 corners of 4).
 * @param m Raw transform matrix applied to the corners (16 contiguous FSize, column-major).
 * @param box_dest Destination of 6 contiguous FSize: min (x, y, z) then max (x, y, z).
 */
void math_frustum_box_1(FSize const *const corners, FSize const *const m, FSize *const box_dest);

/**
 * @brief Return the bounding box of frustum corners transformed by a matrix.
 * @param corners The eight frustum corners.
 * @param m Transform applied to the corners.
 * @return Bounding Box (min, max corners).
 */
Box math_frustum_box_2(FrustumCorners const corners, Mat4 const m);

/**
 * @brief Center of frustum corners, into raw storage.
 * @param corners Raw frustum corners (32 contiguous FSize, 8 corners of 4).
 * @param dest Destination of 3 contiguous FSize (the center point x, y, z).
 */
void math_frustum_center_1(FSize const *const corners, FSize *const dest);

/**
 * @brief Return the center of frustum corners.
 * @param corners The eight frustum corners.
 * @return Center point as Vec3.
 */
Vec3 math_frustum_center_2(FrustumCorners const corners);

/**
 * @brief Extract the 8 frustum corners from an inverse view-projection matrix.
 * @param inv Raw inverse matrix (16 contiguous FSize, column-major); invViewProj
 *        for world space, invMVP for object space.
 * @param dest Destination of 32 contiguous FSize (8 corners of 4), clip-space order
 *        [LBN, LTN, RTN, RBN, LBF, LTF, RTF, RBF]; a near corner at i has its far at i + 4.
 */
void math_frustum_corners_1(FSize const *const inv, FSize *const dest);

/**
 * @brief Return the 8 frustum corners extracted from an inverse view-projection matrix.
 * @param inv Inverse matrix; invViewProj for world space, invMVP for object space.
 * @return The corners in clip-space order [LBN, LTN, RTN, RBN, LBF, LTF, RTF, RBF].
 */
FrustumCorners math_frustum_corners_2(Mat4 const inv);

/**
 * @brief Corners of a split plane parallel to and between the near and far planes.
 * @param corners Raw frustum corners (32 contiguous FSize, 8 corners of 4).
 * @param split_dist Split distance along the frustum.
 * @param far_dist Far distance (zFar); refused (dest zeroed) unless, bounded to float range and
 *        converted, it is a normal positive float whose quotient with split_dist is finite -
 *        zero, negative, NaN, subnormal, Inf, or an F64 past float range - and a NaN, Inf or
 *        past-float-range split_dist, which is refused on the F64 rather than coerced to 0 -
 *        all fail. That guards the divisor only: corners far from the origin can still overflow
 *        inside cglm.
 * @param dest Destination of 16 contiguous FSize (4 corners of 4), order [LB, LT, RT, RB].
 */
void math_frustum_corners_at_1(FSize const *const corners, FSize const split_dist, FSize const far_dist, FSize *const dest);

/**
 * @brief Return the corners of a split plane parallel to and between the near and far planes.
 * @param corners The eight frustum corners.
 * @param split_dist Split distance along the frustum.
 * @param far_dist Far distance (zFar); refused (zeroed result) unless, bounded to float range and
 *        converted, it is a normal positive float whose quotient with split_dist is finite -
 *        zero, negative, NaN, subnormal, Inf, or an F64 past float range - and a NaN, Inf or
 *        past-float-range split_dist, which is refused on the F64 rather than coerced to 0 -
 *        all fail. That guards the divisor only: corners far from the origin can still overflow
 *        inside cglm.
 * @return The four split-plane corners in order [LB, LT, RT, RB].
 */
FrustumSplitCorners math_frustum_corners_at_2(FrustumCorners const corners, FSize const split_dist, FSize const far_dist);

/**
 * @brief Extract the 6 view-frustum planes from a matrix.
 * @param m Raw matrix (16 contiguous FSize, column-major); proj for view space,
 *        viewProj for world space, MVP for object space.
 * @param dest Destination of 24 contiguous FSize (6 planes of 4, normal xyz + distance w),
 *        order [left, right, bottom, top, near, far].
 */
void math_frustum_planes_1(FSize const *const m, FSize *const dest);

/**
 * @brief Return the 6 view-frustum planes extracted from a matrix.
 * @param m Source matrix; proj for view space, viewProj for world space, MVP for object space.
 * @return The planes in order [left, right, bottom, top, near, far].
 */
FrustumPlanes math_frustum_planes_2(Mat4 const m);

#endif // MATH_FRUSTUM_H