/*
 * aabb2d.h - 2D axis-aligned bounding box operations for the CFW math module
 *
 * Features:
 *   - Full coverage of cglm's compiled glmc_aabb2d_* API: construction/reset
 *     (zero/invalidate/copy), affine transform, boolean geometry (merge/crop),
 *     metrics (diag/sizev/radius/center), and containment/intersection tests
 *     (isvalid/aabb/point/contains/circle)
 *   - Root-first variants: _1 raw FSize-array underset, _2 framework-struct superset
 *   - Pure-value semantics: no wrapper mutates its argument in place; the raw (_1)
 *     variant writes a caller-supplied destination, the struct (_2) variant returns
 *     an Aabb2d (or a Vec2/FSize/bool for vector/scalar/boolean results)
 *
 * Usage Examples:
 *   @code
 *   Aabb2d const box = { { 0.0, 0.0 }, { 2.0, 2.0 } };
 *   Vec2 const   c   = math_aabb2d_center_2(box);
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
 *     glmc_* routine. cglm's aabb2d routines are scalar C behind a non-inlinable call -
 *     only its vec4/mat4 paths are SIMD-accelerated.
 *   - Precision: every value crosses F64 -> float -> F64; see the Precision section
 *     in types.h for what that loses and where it flips a result.
 *
 * Dependencies:
 *   - <math/types.h> for the Aabb2d/Vec2/Vec3/Mat3 types, the raw<->cglm bridges,
 *     cglm, and the error/tracing macros.
 *
 * See aabb2d.c for implementation details.
 */

#ifndef MATH_AABB2D_H
#define MATH_AABB2D_H

#include <math/types.h>

/*==============================================================================
 * MARK: - Aabb2d API
 *
 * A raw (_1) 2D AABB is 4 contiguous FSize: min.xy then max.xy. The struct (_2)
 * variants read and return an Aabb2d value. Box-producing ops are pure producers
 * (source box in, new box out); metric ops return a Vec2 or FSize; predicate ops
 * return a bool.
 *============================================================================*/

/**
 * @brief Test whether two raw 2D AABBs intersect.
 * @param aabb Raw first box (4 contiguous FSize: min.xy then max.xy).
 * @param other Raw second box (4 contiguous FSize: min.xy then max.xy).
 * @return true when the two boxes overlap.
 */
bool math_aabb2d_aabb_1(FSize const *const aabb, FSize const *const other);

/**
 * @brief Test whether two 2D AABBs intersect.
 * @param aabb First box.
 * @param other Second box.
 * @return true when the two boxes overlap.
 */
bool math_aabb2d_aabb_2(Aabb2d const aabb, Aabb2d const other);

/**
 * @brief Center point of a raw 2D AABB.
 * @param aabb Raw source box (4 contiguous FSize: min.xy then max.xy).
 * @param dest Destination of 2 contiguous FSize.
 */
void math_aabb2d_center_1(FSize const *const aabb, FSize *const dest);

/**
 * @brief Return the center point of a 2D AABB.
 * @param aabb Source box.
 * @return Center Vec2.
 */
Vec2 math_aabb2d_center_2(Aabb2d const aabb);

/**
 * @brief Test whether a raw 2D AABB intersects a circle.
 * @param aabb Raw source box (4 contiguous FSize: min.xy then max.xy).
 * @param circle Raw circle (3 contiguous FSize: center.xy then radius).
 * @return true when the box and circle overlap.
 */
bool math_aabb2d_circle_1(FSize const *const aabb, FSize const *const circle);

/**
 * @brief Test whether a 2D AABB intersects a circle.
 * @param aabb Source box.
 * @param circle The circle.
 * @return true when the box and circle overlap.
 */
bool math_aabb2d_circle_2(Aabb2d const aabb, Circle const circle);

/**
 * @brief Test whether a raw 2D AABB fully contains another raw AABB.
 * @param aabb Raw outer box (4 contiguous FSize: min.xy then max.xy).
 * @param other Raw inner box (4 contiguous FSize: min.xy then max.xy).
 * @return true when other lies entirely within aabb.
 */
bool math_aabb2d_contains_1(FSize const *const aabb, FSize const *const other);

/**
 * @brief Test whether a 2D AABB fully contains another AABB.
 * @param aabb Outer box.
 * @param other Inner box.
 * @return true when other lies entirely within aabb.
 */
bool math_aabb2d_contains_2(Aabb2d const aabb, Aabb2d const other);

/**
 * @brief Copy a raw 2D AABB.
 * @param aabb Raw source box (4 contiguous FSize: min.xy then max.xy).
 * @param dest Destination of 4 contiguous FSize (min.xy then max.xy).
 */
void math_aabb2d_copy_1(FSize const *const aabb, FSize *const dest);

/**
 * @brief Return a copy of a 2D AABB.
 * @param aabb Source box.
 * @return Copied Aabb2d.
 */
Aabb2d math_aabb2d_copy_2(Aabb2d const aabb);

/**
 * @brief Crop a raw 2D AABB against a raw cropping box (intersection).
 * @param aabb Raw source box (4 contiguous FSize: min.xy then max.xy).
 * @param crop_aabb Raw cropping box (4 contiguous FSize: min.xy then max.xy).
 * @param dest Destination of 4 contiguous FSize (min.xy then max.xy).
 */
void math_aabb2d_crop_1(FSize const *const aabb, FSize const *const crop_aabb, FSize *const dest);

/**
 * @brief Return a 2D AABB cropped against a cropping box (intersection).
 * @param aabb Source box.
 * @param crop_aabb Cropping box.
 * @return Cropped Aabb2d.
 */
Aabb2d math_aabb2d_crop_2(Aabb2d const aabb, Aabb2d const crop_aabb);

/**
 * @brief Crop a raw 2D AABB, then expand the result back inside a clamp box.
 * @param aabb Raw source box (4 contiguous FSize: min.xy then max.xy).
 * @param crop_aabb Raw cropping box (4 contiguous FSize: min.xy then max.xy).
 * @param clamp_aabb Raw clamp box (4 contiguous FSize: min.xy then max.xy).
 * @param dest Destination of 4 contiguous FSize (min.xy then max.xy).
 */
void math_aabb2d_crop_until_1(FSize const *const aabb, FSize const *const crop_aabb, FSize const *const clamp_aabb, FSize *const dest);

/**
 * @brief Return a 2D AABB cropped, then expanded back inside a clamp box.
 * @param aabb Source box.
 * @param crop_aabb Cropping box.
 * @param clamp_aabb Clamp box.
 * @return Cropped-and-clamped Aabb2d.
 */
Aabb2d math_aabb2d_crop_until_2(Aabb2d const aabb, Aabb2d const crop_aabb, Aabb2d const clamp_aabb);

/**
 * @brief Diagonal length of a raw 2D AABB.
 * @param aabb Raw source box (4 contiguous FSize: min.xy then max.xy).
 * @return Diagonal length as FSize.
 */
FSize math_aabb2d_diag_1(FSize const *const aabb);

/**
 * @brief Diagonal length of a 2D AABB.
 * @param aabb Source box.
 * @return Diagonal length as FSize.
 */
FSize math_aabb2d_diag_2(Aabb2d const aabb);

/**
 * @brief Build a raw min/max box from a raw rectangle.
 * @param rect Raw source rectangle (4 contiguous FSize: x, y, w, h).
 * @param dest Destination of 4 contiguous FSize (min x, min y, max x, max y); may overlap rect.
 */
void math_aabb2d_from_rect_1(FSize const *const rect, FSize *const dest);

/**
 * @brief Build a min/max box from a rectangle: the same area in the other 2D form.
 * @param rect Source rectangle.
 * @return Aabb2d with min at the rect's origin and max at origin + size.
 */
Aabb2d math_aabb2d_from_rect_2(Rect const rect);

/**
 * @brief Produce an invalid ("empty") 2D AABB into a raw destination.
 * @param dest Destination of 4 contiguous FSize (min.xy then max.xy).
 */
void math_aabb2d_invalidate_1(FSize *const dest);

/**
 * @brief Return an invalid ("empty") 2D AABB (min at +inf, max at -inf).
 * @return Invalid Aabb2d ready to be grown by merges.
 */
Aabb2d math_aabb2d_invalidate_2(void);

/**
 * @brief Test whether a raw 2D AABB is valid (min <= max on every axis).
 * @param aabb Raw source box (4 contiguous FSize: min.xy then max.xy).
 * @return true when the box is valid.
 */
bool math_aabb2d_isvalid_1(FSize const *const aabb);

/**
 * @brief Test whether a 2D AABB is valid (min <= max on every axis).
 * @param aabb Source box.
 * @return true when the box is valid.
 */
bool math_aabb2d_isvalid_2(Aabb2d const aabb);

/**
 * @brief Merge two raw 2D AABBs into their enclosing box (union).
 * @param aabb1 Raw first box (4 contiguous FSize: min.xy then max.xy).
 * @param aabb2 Raw second box (4 contiguous FSize: min.xy then max.xy).
 * @param dest Destination of 4 contiguous FSize (min.xy then max.xy).
 */
void math_aabb2d_merge_1(FSize const *const aabb1, FSize const *const aabb2, FSize *const dest);

/**
 * @brief Return the enclosing box (union) of two 2D AABBs.
 * @param aabb1 First box.
 * @param aabb2 Second box.
 * @return Merged Aabb2d.
 */
Aabb2d math_aabb2d_merge_2(Aabb2d const aabb1, Aabb2d const aabb2);

/**
 * @brief Test whether a raw 2D AABB contains a raw point.
 * @param aabb Raw source box (4 contiguous FSize: min.xy then max.xy).
 * @param point Raw point (2 contiguous FSize).
 * @return true when the point lies inside the box.
 */
bool math_aabb2d_point_1(FSize const *const aabb, FSize const *const point);

/**
 * @brief Test whether a 2D AABB contains a point.
 * @param aabb Source box.
 * @param point Point to test.
 * @return true when the point lies inside the box.
 */
bool math_aabb2d_point_2(Aabb2d const aabb, Vec2 const point);

/**
 * @brief Bounding-circle radius of a raw 2D AABB (half its diagonal).
 * @param aabb Raw source box (4 contiguous FSize: min.xy then max.xy).
 * @return Radius as FSize.
 */
FSize math_aabb2d_radius_1(FSize const *const aabb);

/**
 * @brief Bounding-circle radius of a 2D AABB (half its diagonal).
 * @param aabb Source box.
 * @return Radius as FSize.
 */
FSize math_aabb2d_radius_2(Aabb2d const aabb);

/**
 * @brief Size (extents) of a raw 2D AABB as a vector (max - min).
 * @param aabb Raw source box (4 contiguous FSize: min.xy then max.xy).
 * @param dest Destination of 2 contiguous FSize.
 */
void math_aabb2d_sizev_1(FSize const *const aabb, FSize *const dest);

/**
 * @brief Return the size (extents) of a 2D AABB as a vector (max - min).
 * @param aabb Source box.
 * @return Size Vec2.
 */
Vec2 math_aabb2d_sizev_2(Aabb2d const aabb);

/**
 * @brief Transform a raw 2D AABB by a raw 3x3 affine matrix.
 * @param aabb Raw source box (4 contiguous FSize: min.xy then max.xy).
 * @param m Raw column-major 3x3 matrix (9 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize (min.xy then max.xy).
 */
void math_aabb2d_transform_1(FSize const *const aabb, FSize const *const m, FSize *const dest);

/**
 * @brief Return a 2D AABB transformed by a 3x3 affine matrix.
 * @param aabb Source box.
 * @param m Column-major 3x3 transform matrix.
 * @return Transformed (re-fitted) Aabb2d.
 */
Aabb2d math_aabb2d_transform_2(Aabb2d const aabb, Mat3 const m);

/**
 * @brief Produce a zeroed 2D AABB into a raw destination.
 * @param dest Destination of 4 contiguous FSize (min.xy then max.xy).
 */
void math_aabb2d_zero_1(FSize *const dest);

/**
 * @brief Return a zeroed 2D AABB (min and max both at the origin).
 * @return Zeroed Aabb2d.
 */
Aabb2d math_aabb2d_zero_2(void);

#endif // MATH_AABB2D_H