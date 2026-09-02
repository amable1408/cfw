/*
 * scalar.h - Scalar libm wrappers for the CFW math module
 *
 * Features:
 *   - Scalar libm wrappers returning framework types (FSize/ISize/USize)
 *   - Saturating integer/float min/max, negate, and safe unsigned conversions
 *
 * Usage Examples:
 *   @code
 *   FSize const root = math_sqrt_f(2.0);
 *   ISize const a    = math_abs_i(-5);
 *   @endcode
 *
 * Error Handling:
 *   - Scalar wrappers assume valid floating-point input; see per-function notes.
 *   - Conversions to USize saturate to 0 on negative, NaN, or overflow input.
 *
 * Thread Safety:
 *   - All functions are pure and thread-safe; no shared state is touched.
 *
 * Memory Management:
 *   - No allocation is performed; every function operates on values only.
 *
 * Performance Characteristics:
 *   - Each wrapper is a single libm call or a constant-time comparison.
 *
 * Dependencies:
 *   - <math/types.h> for framework types, constants, and error/tracing macros.
 *
 * See scalar.c for implementation details.
 */

#ifndef MATH_SCALAR_H
#define MATH_SCALAR_H

#include <math/types.h>

/*==============================================================================
 * MARK: - Scalar API
 *============================================================================*/

/**
 * @brief Absolute value (floating-point)
 * @param value Input value
 * @return Absolute value as FSize
 */
FSize math_abs_f(FSize const value);

/**
 * @brief Absolute value (integer)
 * @param value Input value
 * @return Absolute value as ISize
 */
ISize math_abs_i(ISize const value);

/**
 * @brief Arccosine (returns FSize)
 * @param value Input value
 * @return acos(value) as FSize
 */
FSize math_acos_f(FSize const value);

/**
 * @brief Arcsine (returns FSize)
 * @param value Input value
 * @return asin(value) as FSize
 */
FSize math_asin_f(FSize const value);

/**
 * @brief Arctangent of y/x (returns FSize)
 * @param y Y value
 * @param x X value
 * @return atan2(y, x) as FSize
 */
FSize math_atan2_f(FSize const y, FSize const x);

/**
 * @brief Arctangent (returns FSize)
 * @param value Input value
 * @return atan(value) as FSize
 */
FSize math_atan_f(FSize const value);

/**
 * @brief Ceiling of a floating-point value (returns FSize)
 * @param value Input value
 * @return Ceil value as FSize
 */
FSize math_ceil_f(FSize const value);

/**
 * @brief Clamp a floating-point value to the inclusive range [min, max].
 * @param value Input value.
 * @param min Lower bound.
 * @param max Upper bound.
 * @return value bounded to [min, max]; a NaN value is returned unchanged (guard
 *         NaN at the call site if that matters).
 */
FSize math_clamp_f(FSize const value, FSize const min, FSize const max);

/**
 * @brief Cosine (returns FSize)
 * @param value Input value (radians)
 * @return cos(value) as FSize
 */
FSize math_cos_f(FSize const value);

/**
 * @brief Exponential (returns FSize)
 * @param value Input value
 * @return exp(value) as FSize
 */
FSize math_exp_f(FSize const value);

/**
 * @brief Floor a floating-point value (returns FSize)
 * @param value Input value
 * @return Floored value as FSize
 */
FSize math_floor_f(FSize const value);

/**
 * @brief Floor a floating-point value and cast to USize
 * @param value Input value
 * @return Floored value as USize
 */
USize math_floor_u(FSize const value);

/**
 * @brief Floating-point modulus (returns FSize)
 * @param x Dividend
 * @param y Divisor
 * @return fmod(x, y) as FSize
 */
FSize math_fmod_f(FSize const x, FSize const y);

/**
 * @brief Linear interpolation between a and b by parameter t.
 * @param a Value at t = 0.
 * @param b Value at t = 1.
 * @param t Interpolation parameter; t outside [0, 1] extrapolates (unclamped).
 * @return a + (b - a) * t as FSize.
 */
FSize math_lerp_f(FSize const a, FSize const b, FSize const t);

/**
 * @brief Base-10 logarithm (returns FSize)
 * @param value Input value
 * @return log10(value) as FSize
 */
FSize math_log10_f(FSize const value);

/**
 * @brief Natural logarithm (returns FSize)
 * @param value Input value
 * @return log(value) as FSize
 */
FSize math_log_f(FSize const value);

/**
 * @brief Return the greater floating-point value.
 * @param a First value.
 * @param b Second value.
 * @return Greater value as FSize.
 */
FSize math_max_f(FSize const a, FSize const b);

/**
 * @brief Return the greater signed integer value.
 * @param a First value.
 * @param b Second value.
 * @return Greater value as ISize.
 */
ISize math_max_i(ISize const a, ISize const b);

/**
 * @brief Return the greater unsigned integer value.
 * @param a First value.
 * @param b Second value.
 * @return Greater value as USize.
 */
USize math_max_u(USize const a, USize const b);

/**
 * @brief Return the lesser floating-point value.
 * @param a First value.
 * @param b Second value.
 * @return Lesser value as FSize.
 */
FSize math_min_f(FSize const a, FSize const b);

/**
 * @brief Return the lesser signed integer value.
 * @param a First value.
 * @param b Second value.
 * @return Lesser value as ISize.
 */
ISize math_min_i(ISize const a, ISize const b);

/**
 * @brief Return the lesser unsigned integer value.
 * @param a First value.
 * @param b Second value.
 * @return Lesser value as USize.
 */
USize math_min_u(USize const a, USize const b);

/**
 * @brief Return arithmetic negation for a floating-point value.
 * @param x Input value.
 * @return Negated value as FSize.
 */
FSize math_negate_f(FSize const x);

/**
 * @brief Return two's complement negation for an unsigned integer.
 * @param x Input value.
 * @return Negated value as USize.
 */
USize math_negate_u(USize const x);

/**
 * @brief Raise base to the power of exp, returns FSize
 * @param base Base value
 * @param exp Exponent value
 * @return (FSize)pow(base, exp)
 */
FSize math_pow_f(FSize const base, FSize const exp);

/**
 * @brief Raise base to the power of exp, cast to USize
 * @param base Base value
 * @param exp Exponent value
 * @return (USize)pow(base, exp)
 */
USize math_pow_u(FSize const base, FSize const exp);

/**
 * @brief Remap a value from the source range [in_min, in_max] onto [out_min, out_max].
 * @param value Input value.
 * @param in_min Source range minimum.
 * @param in_max Source range maximum (the caller must ensure it differs from in_min).
 * @param out_min Destination range minimum.
 * @param out_max Destination range maximum.
 * @return The linearly remapped value as FSize (unclamped).
 */
FSize math_remap_f(FSize const value, FSize const in_min, FSize const in_max, FSize const out_min, FSize const out_max);

/**
 * @brief Round a floating-point value (returns FSize)
 * @param value Input value
 * @return Rounded value as FSize
 */
FSize math_round_f(FSize const value);

/**
 * @brief Round a floating-point value and cast to USize
 * @param value Input value
 * @return Rounded value as USize
 */
USize math_round_u(FSize const value);

/**
 * @brief Sine (returns FSize)
 * @param value Input value (radians)
 * @return sin(value) as FSize
 */
FSize math_sin_f(FSize const value);

/**
 * @brief Square root (returns FSize)
 * @param value Input value
 * @return sqrt(value) as FSize
 */
FSize math_sqrt_f(FSize const value);

/**
 * @brief Tangent (returns FSize)
 * @param value Input value (radians)
 * @return tan(value) as FSize
 */
FSize math_tan_f(FSize const value);

/**
 * @brief Truncate a floating-point value (returns FSize)
 * @param value Input value
 * @return Truncated value as FSize
 */
FSize math_trunc_f(FSize const value);

#endif // MATH_SCALAR_H