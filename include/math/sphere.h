/*
 * sphere.h - Bounding-sphere operations for the CFW math module
 *
 * Features:
 *   - Full coverage of cglm's compiled glmc_sphere_* API: radius extraction,
 *     matrix transform, two-sphere merge, sphere-sphere intersection, and
 *     sphere-point containment
 *   - Root-first variants: _1 raw FSize-array underset, _2 framework-struct superset
 *   - Pure-value semantics: no wrapper mutates its argument in place; the raw (_1)
 *     variant writes a caller-supplied destination, the struct (_2) variant returns
 *     a Sphere (or an FSize/bool for scalar/boolean results)
 *
 * Usage Examples:
 *   @code
 *   Sphere const a      = { 0.0, 0.0, 0.0, 1.0 };
 *   Sphere const b      = { 3.0, 0.0, 0.0, 1.0 };
 *   Sphere const merged = math_sphere_merge_2(a, b);
 *   bool   const hit    = math_sphere_sphere_2(a, b);
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
 *     glmc_* routine. cglm's sphere routines are scalar C behind a non-inlinable call -
 *     only its vec4/mat4 paths are SIMD-accelerated.
 *   - Precision: every value crosses F64 -> float -> F64; see the Precision section
 *     in types.h for what that loses and where it flips a result.
 *
 * Dependencies:
 *   - <math/types.h> for the Sphere/Vec3/Mat4 types, the raw<->cglm bridges,
 *     cglm, and the error/tracing macros.
 *
 * A cglm sphere is a vec4 packing the center in xyz and the radius in w; the
 * framework Sphere mirrors that as { x, y, z, r }.
 *
 * See sphere.c for implementation details.
 */

#ifndef MATH_SPHERE_H
#define MATH_SPHERE_H

#include <math/types.h>

/*==============================================================================
 * MARK: - Sphere API
 *
 * Raw (_1) variants read and write 4 contiguous FSize for a sphere (x, y, z, r);
 * the struct (_2) variants read and return a Sphere value. Predicates return
 * bool and radii returns an FSize regardless of variant.
 *============================================================================*/

/**
 * @brief Merge two raw spheres into the smallest sphere enclosing both.
 * @param a Raw first sphere (4 contiguous FSize: x, y, z, r).
 * @param b Raw second sphere (4 contiguous FSize: x, y, z, r).
 * @param dest Destination sphere (4 contiguous FSize: x, y, z, r).
 */
void math_sphere_merge_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the smallest sphere enclosing two spheres.
 * @param a First sphere.
 * @param b Second sphere.
 * @return Merged Sphere.
 */
Sphere math_sphere_merge_2(Sphere const a, Sphere const b);

/**
 * @brief Test whether a raw point lies inside or on a raw sphere.
 * @param sphere Raw sphere (4 contiguous FSize: x, y, z, r).
 * @param point Raw point (3 contiguous FSize).
 * @return true when the point is inside or on the sphere.
 */
bool math_sphere_point_1(FSize const *const sphere, FSize const *const point);

/**
 * @brief Test whether a point lies inside or on a sphere.
 * @param sphere Sphere.
 * @param point Point to test.
 * @return true when the point is inside or on the sphere.
 */
bool math_sphere_point_2(Sphere const sphere, Vec3 const point);

/**
 * @brief Radius of a raw sphere.
 * @param sphere Raw sphere (4 contiguous FSize: x, y, z, r).
 * @return Radius as FSize.
 */
FSize math_sphere_radii_1(FSize const *const sphere);

/**
 * @brief Radius of a sphere.
 * @param sphere Sphere.
 * @return Radius as FSize.
 */
FSize math_sphere_radii_2(Sphere const sphere);

/**
 * @brief Test whether two raw spheres intersect or touch.
 * @param a Raw first sphere (4 contiguous FSize: x, y, z, r).
 * @param b Raw second sphere (4 contiguous FSize: x, y, z, r).
 * @return true when the spheres intersect or touch.
 */
bool math_sphere_sphere_1(FSize const *const a, FSize const *const b);

/**
 * @brief Test whether two spheres intersect or touch.
 * @param a First sphere.
 * @param b Second sphere.
 * @return true when the spheres intersect or touch.
 */
bool math_sphere_sphere_2(Sphere const a, Sphere const b);

/**
 * @brief Transform a raw sphere by a raw column-major 4x4 matrix.
 * @param sphere Raw source sphere (4 contiguous FSize: x, y, z, r).
 * @param m Raw 4x4 matrix (16 contiguous FSize, column-major).
 * @param dest Destination sphere (4 contiguous FSize: x, y, z, r).
 */
void math_sphere_transform_1(FSize const *const sphere, FSize const *const m, FSize *const dest);

/**
 * @brief Return a sphere transformed by a column-major 4x4 matrix.
 * @param sphere Source sphere.
 * @param m 4x4 transform matrix.
 * @return Transformed Sphere.
 */
Sphere math_sphere_transform_2(Sphere const sphere, Mat4 const m);

#endif // MATH_SPHERE_H