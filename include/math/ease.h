/*
 * ease.h - Scalar easing-function wrappers for the CFW math module
 *
 * Features:
 *   - Framework FSize wrappers over every compiled cglm glmc_ease_* function
 *   - Standard easing family: linear, sine, quad, cubic, quart, quint, exp,
 *     circ, back, elast, and bounce, each in in/out/inout variants
 *
 * Usage Examples:
 *   @code
 *   FSize const eased = math_ease_cubic_inout(0.25);
 *   FSize const y     = math_ease_bounce_out(t);
 *   @endcode
 *
 * Error Handling:
 *   - Each wrapper is a pure scalar map with no validation; any finite t is
 *     accepted. Easing is defined for t in [0, 1]; the result for t outside that range is
 *     UNSPECIFIED - the bounce, elastic and back curves are piecewise and do not extrapolate
 *     meaningfully, and the others merely continue their polynomial. Clamp before calling.
 *
 * Thread Safety:
 *   - All functions are pure and thread-safe; no shared state is touched.
 *
 * Memory Management:
 *   - No allocation is performed; every function operates on values only.
 *
 * Performance Characteristics:
 *   - Each wrapper is a single glmc_ease_* call with two float<->FSize casts.
 *
 * Dependencies:
 *   - <math/types.h> for framework types and the compiled glmc_ease_* API.
 *
 * See ease.c for implementation details.
 */

#ifndef MATH_EASE_H
#define MATH_EASE_H

#include <math/types.h>

/*==============================================================================
 * MARK: - Ease API
 *============================================================================*/

/**
 * @brief Back-in easing (anticipates by overshooting below 0 near the start).
 * @param t Interpolation parameter, normally in [0, 1].
 * @return Eased value as FSize.
 */
FSize math_ease_back_in(FSize const t);

/**
 * @brief Back-in-out easing (overshoots at both ends).
 * @param t Interpolation parameter, normally in [0, 1].
 * @return Eased value as FSize.
 */
FSize math_ease_back_inout(FSize const t);

/**
 * @brief Back-out easing (overshoots above 1 near the end).
 * @param t Interpolation parameter, normally in [0, 1].
 * @return Eased value as FSize.
 */
FSize math_ease_back_out(FSize const t);

/**
 * @brief Bounce-in easing (bounces up to the start).
 * @param t Interpolation parameter, normally in [0, 1].
 * @return Eased value as FSize.
 */
FSize math_ease_bounce_in(FSize const t);

/**
 * @brief Bounce-in-out easing (bounces at both ends).
 * @param t Interpolation parameter, normally in [0, 1].
 * @return Eased value as FSize.
 */
FSize math_ease_bounce_inout(FSize const t);

/**
 * @brief Bounce-out easing (bounces down to the end).
 * @param t Interpolation parameter, normally in [0, 1].
 * @return Eased value as FSize.
 */
FSize math_ease_bounce_out(FSize const t);

/**
 * @brief Circular-in easing.
 * @param t Interpolation parameter, normally in [0, 1].
 * @return Eased value as FSize.
 */
FSize math_ease_circ_in(FSize const t);

/**
 * @brief Circular-in-out easing.
 * @param t Interpolation parameter, normally in [0, 1].
 * @return Eased value as FSize.
 */
FSize math_ease_circ_inout(FSize const t);

/**
 * @brief Circular-out easing.
 * @param t Interpolation parameter, normally in [0, 1].
 * @return Eased value as FSize.
 */
FSize math_ease_circ_out(FSize const t);

/**
 * @brief Cubic-in easing (t^3).
 * @param t Interpolation parameter, normally in [0, 1].
 * @return Eased value as FSize.
 */
FSize math_ease_cubic_in(FSize const t);

/**
 * @brief Cubic-in-out easing.
 * @param t Interpolation parameter, normally in [0, 1].
 * @return Eased value as FSize.
 */
FSize math_ease_cubic_inout(FSize const t);

/**
 * @brief Cubic-out easing.
 * @param t Interpolation parameter, normally in [0, 1].
 * @return Eased value as FSize.
 */
FSize math_ease_cubic_out(FSize const t);

/**
 * @brief Elastic-in easing (oscillates while easing in).
 * @param t Interpolation parameter, normally in [0, 1].
 * @return Eased value as FSize.
 */
FSize math_ease_elast_in(FSize const t);

/**
 * @brief Elastic-in-out easing.
 * @param t Interpolation parameter, normally in [0, 1].
 * @return Eased value as FSize.
 */
FSize math_ease_elast_inout(FSize const t);

/**
 * @brief Elastic-out easing.
 * @param t Interpolation parameter, normally in [0, 1].
 * @return Eased value as FSize.
 */
FSize math_ease_elast_out(FSize const t);

/**
 * @brief Exponential-in easing (2^(10*(t-1))).
 * @param t Interpolation parameter, normally in [0, 1].
 * @return Eased value as FSize.
 */
FSize math_ease_exp_in(FSize const t);

/**
 * @brief Exponential-in-out easing.
 * @param t Interpolation parameter, normally in [0, 1].
 * @return Eased value as FSize.
 */
FSize math_ease_exp_inout(FSize const t);

/**
 * @brief Exponential-out easing.
 * @param t Interpolation parameter, normally in [0, 1].
 * @return Eased value as FSize.
 */
FSize math_ease_exp_out(FSize const t);

/**
 * @brief Linear easing (identity; returns t).
 * @param t Interpolation parameter, normally in [0, 1].
 * @return Eased value as FSize.
 */
FSize math_ease_linear(FSize const t);

/**
 * @brief Quadratic-in easing (t^2).
 * @param t Interpolation parameter, normally in [0, 1].
 * @return Eased value as FSize.
 */
FSize math_ease_quad_in(FSize const t);

/**
 * @brief Quadratic-in-out easing.
 * @param t Interpolation parameter, normally in [0, 1].
 * @return Eased value as FSize.
 */
FSize math_ease_quad_inout(FSize const t);

/**
 * @brief Quadratic-out easing.
 * @param t Interpolation parameter, normally in [0, 1].
 * @return Eased value as FSize.
 */
FSize math_ease_quad_out(FSize const t);

/**
 * @brief Quartic-in easing (t^4).
 * @param t Interpolation parameter, normally in [0, 1].
 * @return Eased value as FSize.
 */
FSize math_ease_quart_in(FSize const t);

/**
 * @brief Quartic-in-out easing.
 * @param t Interpolation parameter, normally in [0, 1].
 * @return Eased value as FSize.
 */
FSize math_ease_quart_inout(FSize const t);

/**
 * @brief Quartic-out easing.
 * @param t Interpolation parameter, normally in [0, 1].
 * @return Eased value as FSize.
 */
FSize math_ease_quart_out(FSize const t);

/**
 * @brief Quintic-in easing (t^5).
 * @param t Interpolation parameter, normally in [0, 1].
 * @return Eased value as FSize.
 */
FSize math_ease_quint_in(FSize const t);

/**
 * @brief Quintic-in-out easing.
 * @param t Interpolation parameter, normally in [0, 1].
 * @return Eased value as FSize.
 */
FSize math_ease_quint_inout(FSize const t);

/**
 * @brief Quintic-out easing.
 * @param t Interpolation parameter, normally in [0, 1].
 * @return Eased value as FSize.
 */
FSize math_ease_quint_out(FSize const t);

/**
 * @brief Sine-in easing.
 * @param t Interpolation parameter, normally in [0, 1].
 * @return Eased value as FSize.
 */
FSize math_ease_sine_in(FSize const t);

/**
 * @brief Sine-in-out easing.
 * @param t Interpolation parameter, normally in [0, 1].
 * @return Eased value as FSize.
 */
FSize math_ease_sine_inout(FSize const t);

/**
 * @brief Sine-out easing.
 * @param t Interpolation parameter, normally in [0, 1].
 * @return Eased value as FSize.
 */
FSize math_ease_sine_out(FSize const t);

#endif // MATH_EASE_H