/*
 * plane.h - Plane operations for the CFW math module
 *
 * Features:
 *   - Full coverage of cglm's compiled glmc_plane_* API: normalization
 *   - Root-first variants: _1 raw FSize-array underset, _2 framework-struct superset
 *   - Pure-value semantics: cglm's in-place normalize is exposed as a producer;
 *     the raw (_1) variant writes a caller-supplied destination, the struct (_2)
 *     variant returns a Plane
 *
 * Usage Examples:
 *   @code
 *   Plane const p    = { 0.0, 3.0, 4.0, 10.0 };
 *   Plane const unit = math_plane_normalize_2(p);
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
 *     glmc_* routine. cglm's plane routines are scalar C behind a non-inlinable call -
 *     only its vec4/mat4 paths are SIMD-accelerated.
 *   - Precision: every value crosses F64 -> float -> F64; see the Precision section
 *     in types.h for what that loses and where it flips a result.
 *
 * Dependencies:
 *   - <math/types.h> for the Plane type (a vec4 of normal xyz + signed distance w),
 *     cglm, and the error/tracing macros.
 *
 * See plane.c for implementation details.
 */

#ifndef MATH_PLANE_H
#define MATH_PLANE_H

#include <math/types.h>

/*==============================================================================
 * MARK: - Plane API
 *
 * A plane is a vec4: the normal in x, y, z and the signed distance in w.
 * Normalization divides all four components by the length of the normal, so the
 * resulting normal has unit length. Raw (_1) variants read and write 4
 * contiguous FSize; the struct (_2) variants read and return a Plane value.
 *============================================================================*/

/**
 * @brief Normalize a raw plane so its normal has unit length.
 * @param plane Raw source plane (4 contiguous FSize: normal x, y, z, signed distance w).
 * @param dest Destination of 4 contiguous FSize; zeroed when the normal is degenerate.
 */
void math_plane_normalize_1(FSize const *const plane, FSize *const dest);

/**
 * @brief Return the normalized plane (normal scaled to unit length, distance scaled to match).
 * @param plane Source plane (normal x, y, z, signed distance w).
 * @return Normalized Plane; the zero plane when the normal is degenerate.
 */
Plane math_plane_normalize_2(Plane const plane);

#endif // MATH_PLANE_H