/*
 * curve.h - Cubic spline/curve evaluation for the CFW math module
 *
 * Features:
 *   - Full coverage of cglm's compiled glmc_smc API: evaluate a cubic curve at a
 *     scalar parameter s against a 4x4 basis matrix and a 4-component control set
 *   - Root-first variants: _1 raw FSize-array underset, _2 framework-struct superset
 *   - Pure-value semantics: neither wrapper mutates its arguments; both return the
 *     scalar curve value as an FSize
 *
 * Usage Examples:
 *   @code
 *   Mat4 const basis = { { {1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,0,0,1} } };
 *   Vec4 const ctrl  = { 1.0, 2.0, 3.0, 4.0 };
 *   FSize const y     = math_curve_smc_2(1.0, basis, ctrl);
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
 *     glmc_* routine. cglm's curve routines are scalar C behind a non-inlinable call -
 *     only its vec4/mat4 paths are SIMD-accelerated.
 *   - Precision: every value crosses F64 -> float -> F64; see the Precision section
 *     in types.h for what that loses and where it flips a result.
 *
 * Dependencies:
 *   - <math/types.h> for the Mat4/Vec4 types, the raw<->cglm bridges, cglm, and
 *     the error/tracing macros.
 *
 * See curve.c for implementation details.
 */

#ifndef MATH_CURVE_H
#define MATH_CURVE_H

#include <math/types.h>

/*==============================================================================
 * MARK: - Curve API
 *
 * Evaluate a cubic curve at parameter s: form the cubic weights (s^3, s^2, s, 1)
 * and return their product against the basis matrix and control values,
 * cubic(s) . (m . c). The raw (_1) variant reads a column-major 16-FSize matrix
 * and a 4-FSize control vector; the struct (_2) variant reads a Mat4 and a Vec4.
 *============================================================================*/

/**
 * @brief Evaluate a cubic curve at s against a raw basis matrix and control set.
 * @param s Scalar curve parameter.
 * @param m Raw column-major basis matrix (16 contiguous FSize).
 * @param c Raw control values (4 contiguous FSize).
 * @return Curve value at s as FSize.
 */
FSize math_curve_smc_1(FSize const s, FSize const *const m, FSize const *const c);

/**
 * @brief Evaluate a cubic curve at s against a basis matrix and control set.
 * @param s Scalar curve parameter.
 * @param m Basis matrix.
 * @param c Control values.
 * @return Curve value at s as FSize.
 */
FSize math_curve_smc_2(FSize const s, Mat4 const m, Vec4 const c);

#endif // MATH_CURVE_H