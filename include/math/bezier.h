/*
 * bezier.h - Scalar Bezier/Hermite curve wrappers for the CFW math module
 *
 * Features:
 *   - Scalar cubic Bezier and Hermite curve evaluation returning FSize
 *   - De Casteljau point evaluation of a cubic Bezier segment
 *
 * Usage Examples:
 *   @code
 *   FSize const y = math_bezier_bezier(0.5, 0.0, 1.0, 1.0, 0.0);
 *   FSize const h = math_bezier_hermite(0.5, 0.0, 1.0, 1.0, 1.0);
 *   @endcode
 *
 * Error Handling:
 *   - Scalar wrappers assume valid floating-point input; no validation is done.
 *
 * Thread Safety:
 *   - All functions are pure and thread-safe; no shared state is touched.
 *
 * Memory Management:
 *   - No allocation is performed; every function operates on values only.
 *
 * Performance Characteristics:
 *   - Each wrapper is a single compiled cglm call with FSize<->float casts.
 *
 * Dependencies:
 *   - <math/types.h> for framework types and the compiled cglm API.
 *
 * See bezier.c for implementation details.
 */

#ifndef MATH_BEZIER_H
#define MATH_BEZIER_H

#include <math/types.h>

/*==============================================================================
 * MARK: - Bezier API
 *============================================================================*/

/**
 * @brief Evaluate a cubic Bezier curve at parameter s.
 * @param s Curve parameter in [0, 1].
 * @param p0 Start point.
 * @param c0 First control point.
 * @param c1 Second control point.
 * @param p1 End point.
 * @return Curve value at s as FSize (p0 at s=0, p1 at s=1).
 */
FSize math_bezier_bezier(FSize const s, FSize const p0, FSize const c0, FSize const c1, FSize const p1);

/**
 * @brief Evaluate a cubic Bezier segment by the De Casteljau algorithm.
 * @param s Curve parameter in [0, 1].
 * @param p0 Start point.
 * @param c0 First control point.
 * @param c1 Second control point.
 * @param p1 End point.
 * @return Curve value at s as FSize (p0 at s=0, p1 at s=1).
 */
FSize math_bezier_decasteljau(FSize const s, FSize const p0, FSize const c0, FSize const c1, FSize const p1);

/**
 * @brief Evaluate a cubic Hermite curve at parameter s.
 * @param s Curve parameter in [0, 1].
 * @param p0 Start point.
 * @param t0 Start tangent.
 * @param t1 End tangent.
 * @param p1 End point.
 * @return Curve value at s as FSize (p0 at s=0, p1 at s=1).
 */
FSize math_bezier_hermite(FSize const s, FSize const p0, FSize const t0, FSize const t1, FSize const p1);

#endif // MATH_BEZIER_H