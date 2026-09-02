/*
 * box.h - 3D axis-aligned bounding box (AABB) operations for the CFW math module
 *
 * Features:
 *   - Full coverage of cglm's compiled glmc_aabb_* API: transform, merge, crop,
 *     crop_until, frustum test, invalidate, validity, size/radius, center, and the
 *     intersection/containment predicates (aabb-aabb, point, contains, sphere)
 *   - Root-first variants: _1 raw FSize-array underset, _2 framework-struct superset
 *   - Pure-value semantics: no wrapper mutates its argument in place; the raw (_1)
 *     variant writes a caller-supplied destination, the struct (_2) variant returns
 *     a Box (or a Vec3/FSize/bool for center/scalar/boolean results)
 *
 * Usage Examples:
 *   @code
 *   Box const a      = { { 0.0, 0.0, 0.0 }, { 2.0, 4.0, 6.0 } };
 *   Box const b      = { { 1.0, 1.0, 1.0 }, { 3.0, 3.0, 3.0 } };
 *   Box const merged = math_box_merge_2(a, b);
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
 *     glmc_* routine. cglm's aabb routines are scalar C behind a non-inlinable call -
 *     only its vec4/mat4 paths are SIMD-accelerated.
 *   - Precision: every value crosses F64 -> float -> F64; see the Precision section
 *     in types.h for what that loses and where it flips a result.
 *
 * Dependencies:
 *   - <math/types.h> for the Box/Vec3/Mat4/Plane/Sphere types, the raw<->cglm
 *     bridges, cglm, and the error/tracing macros.
 *
 * See box.c for implementation details.
 */

#ifndef MATH_BOX_H
#define MATH_BOX_H

#include <math/types.h>

/*==============================================================================
 * MARK: - Box API
 *
 * A box is a 3D axis-aligned bounding box. Raw (_1) variants read and write 6
 * contiguous FSize (min.xyz then max.xyz); the struct (_2) variants read and
 * return a Box value. Predicates return bool, size/radius return FSize, and
 * center returns a Vec3.
 *============================================================================*/

/**
 * @brief Test whether two raw AABBs intersect.
 * @param box Raw first box (6 contiguous FSize: min.xyz then max.xyz).
 * @param other Raw second box (6 contiguous FSize: min.xyz then max.xyz).
 * @return true when the two boxes overlap.
 */
bool math_box_aabb_1(FSize const *const box, FSize const *const other);

/**
 * @brief Test whether two AABBs intersect.
 * @param box First box.
 * @param other Second box.
 * @return true when the two boxes overlap.
 */
bool math_box_aabb_2(Box const box, Box const other);

/**
 * @brief Center point of a raw AABB.
 * @param box Raw box (6 contiguous FSize: min.xyz then max.xyz).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_box_center_1(FSize const *const box, FSize *const dest);

/**
 * @brief Return the center point of an AABB.
 * @param box Source box.
 * @return Center Vec3.
 */
Vec3 math_box_center_2(Box const box);

/**
 * @brief Test whether a raw AABB fully contains another raw AABB.
 * @param box Raw enclosing box (6 contiguous FSize: min.xyz then max.xyz).
 * @param other Raw candidate box (6 contiguous FSize: min.xyz then max.xyz).
 * @return true when box fully contains other.
 */
bool math_box_contains_1(FSize const *const box, FSize const *const other);

/**
 * @brief Test whether an AABB fully contains another AABB.
 * @param box Enclosing box.
 * @param other Candidate box.
 * @return true when box fully contains other.
 */
bool math_box_contains_2(Box const box, Box const other);

/**
 * @brief Crop a raw AABB against a raw crop box (their intersection).
 * @param box Raw box to crop (6 contiguous FSize: min.xyz then max.xyz).
 * @param crop_box Raw crop box (6 contiguous FSize: min.xyz then max.xyz).
 * @param dest Destination box (6 contiguous FSize: min.xyz then max.xyz).
 */
void math_box_crop_1(FSize const *const box, FSize const *const crop_box, FSize *const dest);

/**
 * @brief Return an AABB cropped against a crop box (their intersection).
 * @param box Box to crop.
 * @param crop_box Crop box.
 * @return Cropped Box.
 */
Box math_box_crop_2(Box const box, Box const crop_box);

/**
 * @brief Crop a raw AABB against a raw crop box, then merge with a clamp box.
 * @param box Raw box to crop (6 contiguous FSize: min.xyz then max.xyz).
 * @param crop_box Raw crop box (6 contiguous FSize: min.xyz then max.xyz).
 * @param clamp_box Raw clamp box merged into the crop result (6 contiguous FSize).
 * @param dest Destination box (6 contiguous FSize: min.xyz then max.xyz).
 */
void math_box_crop_until_1(FSize const *const box, FSize const *const crop_box, FSize const *const clamp_box, FSize *const dest);

/**
 * @brief Return an AABB cropped against a crop box, then merged with a clamp box.
 * @param box Box to crop.
 * @param crop_box Crop box.
 * @param clamp_box Clamp box merged into the crop result.
 * @return Cropped and clamped Box.
 */
Box math_box_crop_until_2(Box const box, Box const crop_box, Box const clamp_box);

/**
 * @brief Test whether a raw AABB intersects a set of 6 raw frustum planes.
 * @param box Raw box (6 contiguous FSize: min.xyz then max.xyz).
 * @param planes Raw plane set (24 contiguous FSize: 6 planes, each normal.xyz + distance.w).
 * @return true when the box is not fully outside any plane.
 */
bool math_box_frustum_1(FSize const *const box, FSize const *const planes);

/**
 * @brief Test whether an AABB intersects a set of 6 frustum planes.
 * @param box Source box.
 * @param planes The six frustum planes (normal x, y, z and signed distance w each).
 * @return true when the box is not fully outside any plane.
 */
bool math_box_frustum_2(Box const box, FrustumPlanes const planes);

/**
 * @brief Write an invalidated (empty) AABB into a raw destination.
 * @param dest Destination box (6 contiguous FSize: min.xyz then max.xyz).
 */
void math_box_invalidate_1(FSize *const dest);

/**
 * @brief Return an invalidated (empty) AABB.
 * @return Box with min set to +FLT_MAX and max set to -FLT_MAX.
 */
Box math_box_invalidate_2(void);

/**
 * @brief Test whether a raw AABB is valid (not invalidated/empty).
 * @param box Raw box (6 contiguous FSize: min.xyz then max.xyz).
 * @return true when the box is valid.
 */
bool math_box_isvalid_1(FSize const *const box);

/**
 * @brief Test whether an AABB is valid (not invalidated/empty).
 * @param box Source box.
 * @return true when the box is valid.
 */
bool math_box_isvalid_2(Box const box);

/**
 * @brief Merge two raw AABBs into the smallest box enclosing both.
 * @param box1 Raw first box (6 contiguous FSize: min.xyz then max.xyz).
 * @param box2 Raw second box (6 contiguous FSize: min.xyz then max.xyz).
 * @param dest Destination box (6 contiguous FSize: min.xyz then max.xyz).
 */
void math_box_merge_1(FSize const *const box1, FSize const *const box2, FSize *const dest);

/**
 * @brief Return the smallest AABB enclosing two AABBs.
 * @param box1 First box.
 * @param box2 Second box.
 * @return Merged Box.
 */
Box math_box_merge_2(Box const box1, Box const box2);

/**
 * @brief Test whether a raw point lies inside a raw AABB.
 * @param box Raw box (6 contiguous FSize: min.xyz then max.xyz).
 * @param point Raw point (3 contiguous FSize).
 * @return true when the point is inside the box.
 */
bool math_box_point_1(FSize const *const box, FSize const *const point);

/**
 * @brief Test whether a point lies inside an AABB.
 * @param box Source box.
 * @param point Point to test.
 * @return true when the point is inside the box.
 */
bool math_box_point_2(Box const box, Vec3 const point);

/**
 * @brief Radius of the sphere surrounding a raw AABB (half the diagonal).
 * @param box Raw box (6 contiguous FSize: min.xyz then max.xyz).
 * @return Radius as FSize.
 */
FSize math_box_radius_1(FSize const *const box);

/**
 * @brief Radius of the sphere surrounding an AABB (half the diagonal).
 * @param box Source box.
 * @return Radius as FSize.
 */
FSize math_box_radius_2(Box const box);

/**
 * @brief Diagonal length (min-to-max distance) of a raw AABB.
 * @param box Raw box (6 contiguous FSize: min.xyz then max.xyz).
 * @return Diagonal length as FSize.
 */
FSize math_box_size_1(FSize const *const box);

/**
 * @brief Diagonal length (min-to-max distance) of an AABB.
 * @param box Source box.
 * @return Diagonal length as FSize.
 */
FSize math_box_size_2(Box const box);

/**
 * @brief Test whether a raw AABB intersects a raw sphere.
 * @param box Raw box (6 contiguous FSize: min.xyz then max.xyz).
 * @param sphere Raw sphere (4 contiguous FSize: center.xyz + radius.w).
 * @return true when the box and sphere overlap.
 */
bool math_box_sphere_1(FSize const *const box, FSize const *const sphere);

/**
 * @brief Test whether an AABB intersects a sphere.
 * @param box Source box.
 * @param sphere Sphere (center x, y, z and radius r).
 * @return true when the box and sphere overlap.
 */
bool math_box_sphere_2(Box const box, Sphere const sphere);

/**
 * @brief Apply a raw 4x4 transform to a raw AABB.
 * @param box Raw box (6 contiguous FSize: min.xyz then max.xyz).
 * @param m Raw 4x4 matrix (16 contiguous FSize, column-major).
 * @param dest Destination box (6 contiguous FSize: min.xyz then max.xyz).
 */
void math_box_transform_1(FSize const *const box, FSize const *const m, FSize *const dest);

/**
 * @brief Return an AABB transformed by a 4x4 matrix.
 * @param box Source box.
 * @param m 4x4 transform matrix.
 * @return Transformed Box.
 */
Box math_box_transform_2(Box const box, Mat4 const m);

#endif // MATH_BOX_H