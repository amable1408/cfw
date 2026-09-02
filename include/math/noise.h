/*
 * noise.h - Perlin noise for the CFW math module
 *
 * Features:
 *   - Classic Perlin gradient noise sampled at a 2D/3D/4D point (over cglm's
 *     compiled glmc_perlin_* API)
 *   - Root-first variants: _1 raw FSize-array underset, _2 framework-struct superset
 *   - Deterministic and smooth: the same point always yields the same value, and
 *     nearby points yield nearby values (ideal for procedural generation)
 *
 * Usage Examples:
 *   @code
 *   Vec2 const p = { 12.5, 4.25 };
 *   FSize const n = math_noise_perlin_vec2_2(p);   // n in roughly [-1, 1]
 *   @endcode
 *
 * Error Handling:
 *   - Pointer (_1) variants validate the point pointer with error_check_null.
 *   - Struct (_2) variants take values, so there is no pointer to validate.
 *
 * Thread Safety:
 *   - All functions are pure and thread-safe; no shared state is touched.
 *
 * Memory Management:
 *   - No allocation is performed; every function operates on values only.
 *
 * Performance Characteristics:
 *   - Each call converts FSize<->float at the boundary and runs one cglm perlin
 *     evaluation (a fixed amount of lattice gradient work).
 *
 * Dependencies:
 *   - <math/types.h> for the CFW types, cglm, and the raw<->cglm bridges.
 *
 * See noise.c for implementation details.
 */

#ifndef MATH_NOISE_H
#define MATH_NOISE_H

#include <math/types.h>

/*==============================================================================
 * MARK: - Noise API
 *============================================================================*/

/**
 * @brief Perlin noise at a raw 2D point.
 * @param point Raw sample point (2 contiguous FSize).
 * @return Noise value (roughly [-1, 1]) as FSize.
 */
FSize math_noise_perlin_vec2_1(FSize const *const point);

/**
 * @brief Perlin noise at a 2D point.
 * @param point Sample point.
 * @return Noise value (roughly [-1, 1]) as FSize.
 */
FSize math_noise_perlin_vec2_2(Vec2 const point);

/**
 * @brief Perlin noise at a raw 3D point.
 * @param point Raw sample point (3 contiguous FSize).
 * @return Noise value (roughly [-1, 1]) as FSize.
 */
FSize math_noise_perlin_vec3_1(FSize const *const point);

/**
 * @brief Perlin noise at a 3D point.
 * @param point Sample point.
 * @return Noise value (roughly [-1, 1]) as FSize.
 */
FSize math_noise_perlin_vec3_2(Vec3 const point);

/**
 * @brief Perlin noise at a raw 4D point.
 * @param point Raw sample point (4 contiguous FSize).
 * @return Noise value (roughly [-1, 1]) as FSize.
 */
FSize math_noise_perlin_vec4_1(FSize const *const point);

/**
 * @brief Perlin noise at a 4D point.
 * @param point Sample point.
 * @return Noise value (roughly [-1, 1]) as FSize.
 */
FSize math_noise_perlin_vec4_2(Vec4 const point);

#endif // MATH_NOISE_H