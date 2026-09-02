/*
 * ray.h - Ray/geometry intersection operations for the CFW math module
 *
 * Features:
 *   - Full coverage of cglm's compiled glmc_ray_* API: point-at-parameter,
 *     ray/sphere intersection, ray/triangle (Moeller-Trumbore) intersection
 *   - Root-first variants: _1 raw FSize-array underset, _2 framework-struct superset
 *   - A ray is an origin plus a direction: the raw (_1) variant takes them as two
 *     separate 3-contiguous-FSize arrays; the struct (_2) variant takes one Ray
 *     whose .origin/.direction are bridged into cglm vec3 values
 *   - Pure-value semantics: no wrapper mutates a Ray/Vec3 argument in place; the
 *     raw (_1) variant writes caller-supplied destinations, the struct (_2) variant
 *     returns a Vec3 or an intersection result struct (RaySphereHit / RayTriangleHit)
 *
 * Usage Examples:
 *   @code
 *   Ray const  ray   = { .origin = { 0.0, 0.0, 0.0 }, .direction = { 1.0, 0.0, 0.0 } };
 *   Vec3 const point = math_ray_at_2(ray, 5.0);
 *   @endcode
 *
 * Error Handling:
 *   - Pointer (_1) variants validate every pointer with error_check_null, including
 *     the scalar out-params (d, t1, t2).
 *   - Struct (_2) variants take Ray/Vec3/Sphere by value, so there is no pointer to
 *     validate; the intersection tests return RaySphereHit / RayTriangleHit by value.
 *
 * Thread Safety:
 *   - All functions are pure and thread-safe; no shared state is touched.
 *
 * Memory Management:
 *   - No allocation is performed; every function operates on values only.
 *
 * Performance Characteristics:
 *   - Each wrapper converts FSize<->float at the boundary and calls a compiled
 *     glmc_* routine. cglm's ray routines are scalar C behind a non-inlinable call -
 *     only its vec4/mat4 paths are SIMD-accelerated.
 *   - Precision: every value crosses F64 -> float -> F64; see the Precision section
 *     in types.h for what that loses and where it flips a result.
 *
 * Dependencies:
 *   - <math/types.h> for the Ray/Vec3/Sphere types, the raw<->cglm bridges,
 *     cglm, and the error/tracing macros.
 *
 * See ray.c for implementation details.
 */

#ifndef MATH_RAY_H
#define MATH_RAY_H

#include <math/types.h>

/*==============================================================================
 * MARK: - Ray API
 *
 * Raw (_1) variants read an origin and a direction as two separate 3-contiguous
 * FSize arrays; the struct (_2) variants read one Ray value. The raw intersection
 * predicates return a bool and write their scalar result(s) through out-params; the
 * struct forms return RaySphereHit / RayTriangleHit by value. Every t / d is a ray
 * PARAMETER: it equals a distance only when the direction has unit length - cglm
 * assumes that and neither wrapper normalizes or checks it.
 *============================================================================*/

/**
 * @brief Point along a raw ray at parameter t: dest = origin + t * direction.
 * @param origin Raw ray origin (3 contiguous FSize).
 * @param direction Raw ray direction (3 contiguous FSize).
 * @param t Ray parameter.
 * @param dest Destination point of 3 contiguous FSize.
 */
void math_ray_at_1(FSize const *const origin, FSize const *const direction, FSize const t, FSize *const dest);

/**
 * @brief Return the point along a ray at parameter t: origin + t * direction.
 * @param ray Ray providing the origin and direction; t is a ray parameter, a distance only
 *        for a unit-length direction.
 * @param t Ray parameter.
 * @return Point Vec3.
 */
Vec3 math_ray_at_2(Ray const ray, FSize const t);

/**
 * @brief Intersect a raw ray with a sphere, reporting both hit distances.
 * @param origin Raw ray origin (3 contiguous FSize).
 * @param direction Raw ray direction (3 contiguous FSize); unit length, or t1/t2 are ray parameters rather than distances.
 * @param sphere Raw sphere as center + radius (4 contiguous FSize: x, y, z, r).
 * @param t1 Out: nearest intersection distance; 0 on every miss (the wrapper's value: cglm
 *        leaves it untouched with no real root, and writes negative roots for a sphere behind
 *        the origin - both are overridden).
 * @param t2 Out: farthest intersection distance; 0 on every miss, likewise.
 * @return true when the ray intersects the sphere.
 */
bool math_ray_sphere_1(FSize const *const origin, FSize const *const direction, FSize const *const sphere, FSize *const t1, FSize *const t2);

/**
 * @brief Intersect a ray with a sphere, reporting both hit distances.
 * @param ray Ray providing the origin and direction; t1/t2 are ray parameters, distances only
 *        for a unit-length direction.
 * @param sphere Sphere as center (x, y, z) and radius (r).
 * @return RaySphereHit: hit, and t1 <= t2 when it hit (both 0 on every miss).
 */
RaySphereHit math_ray_sphere_2(Ray const ray, Sphere const sphere);

/**
 * @brief Intersect a raw ray with a triangle, reporting the hit distance.
 * @param origin Raw ray origin (3 contiguous FSize).
 * @param direction Raw ray direction (3 contiguous FSize); unit length, or d is a ray parameter rather than a distance.
 * @param v0 Raw first triangle vertex (3 contiguous FSize).
 * @param v1 Raw second triangle vertex (3 contiguous FSize).
 * @param v2 Raw third triangle vertex (3 contiguous FSize).
 * @param d Out: distance from origin to the intersection point; 0 on every miss (the wrapper's
 *        value - cglm leaves it untouched or writes a sub-epsilon distance there).
 * @return true when the ray intersects the triangle.
 */
bool math_ray_triangle_1(FSize const *const origin, FSize const *const direction, FSize const *const v0, FSize const *const v1, FSize const *const v2, FSize *const d);

/**
 * @brief Intersect a ray with a triangle, reporting the hit distance.
 * @param ray Ray providing the origin and direction; d is a ray parameter, a distance only for
 *        a unit-length direction.
 * @param v0 First triangle vertex.
 * @param v1 Second triangle vertex.
 * @param v2 Third triangle vertex.
 * @return RayTriangleHit: hit, and the distance d when it hit (0 on every miss).
 */
RayTriangleHit math_ray_triangle_2(Ray const ray, Vec3 const v0, Vec3 const v1, Vec3 const v2);

#endif // MATH_RAY_H