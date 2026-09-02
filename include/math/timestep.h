/*
 * timestep.h - Fixed-timestep accumulator for the CFW math module
 *
 * Features:
 *   - Converts variable wall-clock frame time into a whole number of fixed
 *     simulation steps plus a leftover render-interpolation alpha
 *   - Frame-time clamp (spiral-of-death guard) so a stall never queues more than
 *     MATH_TIMESTEP_STEPS_MAX + 1 steps of backlog (the one is a carried-over partial step)
 *   - Pause support with an optional single queued step (frame-advance)
 *
 * Usage Examples:
 *   @code
 *   Timestep ts     = timestep_init(1.0 / 60.0, 0.25);
 *   U32 const steps = timestep_advance(&ts, delta_seconds);
 *
 *   for (U32 i = 0; i < steps; i += 1) {
 *       simulate(ts.fixed_dt);
 *   }
 *
 *   FSize const alpha = timestep_alpha(&ts);
 *   render(alpha);
 *   @endcode
 *
 * Error Handling:
 *   - timestep_init REFUSES a bad configuration by returning the zeroed Timestep, in
 *     every build - it is data, not a programming error: fixed_dt <= 0, max_frame_seconds
 *     < fixed_dt, either value non-finite, max_frame_seconds above
 *     MATH_TIMESTEP_SECONDS_MAX, or max_frame_seconds / fixed_dt above
 *     MATH_TIMESTEP_STEPS_MAX. The two caps are what bound timestep_advance: past the ratio
 *     the drain loop would spin for minutes, and near 1e16 F64 stops decreasing at all and
 *     the loop never ends; past the magnitude a feed could overflow the accumulator to Inf.
 *     A zeroed Timestep never advances and reports alpha 0.
 *   - Those checks run at init only. The fields are public for reading; a Timestep mutated
 *     by hand after init (a NaN clamp, a pre-loaded accumulator) is outside the contract.
 *   - Every other function validates self with error_check_null.
 *
 * Thread Safety:
 *   - Not thread-safe; callers serialize access to a single Timestep instance
 *     (one per simulation loop is the intended usage).
 *
 * Memory Management:
 *   - No allocation is performed; Timestep is a plain stack-owned value with
 *     no cleanup function.
 *
 * Performance Characteristics:
 *   - Every function is O(1) pure arithmetic; timestep_advance loops only over
 *     the whole steps it returns.
 *
 * Dependencies:
 *   - <math/types.h> for framework types and the error/tracing macros it chains in.
 *
 * See timestep.c for implementation details.
 */

#ifndef MATH_TIMESTEP_H
#define MATH_TIMESTEP_H

#include <math/types.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

/** Largest max_frame_seconds timestep_init accepts (one day): bounds the accumulator's
 * magnitude so a feed can never overflow it to Inf, which the drain loop could not consume. */
#define MATH_TIMESTEP_SECONDS_MAX 86400.0

/** Bound on the steps one timestep_advance may return - at most this many plus one carried-over
 * partial step; timestep_init refuses a configuration whose max_frame_seconds / fixed_dt exceeds it. */
#define MATH_TIMESTEP_STEPS_MAX 1000000

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief Fixed-timestep accumulator state.
 *
 * Feed wall-clock seconds via timestep_advance; it returns how many whole
 * fixed_dt-sized simulation steps to run and keeps the leftover remainder in
 * accumulator for timestep_alpha to report as the render interpolation factor.
 */
typedef struct Timestep {
    F64  accumulator;        /**< Leftover simulation time not yet consumed, in seconds. */
    F64  fixed_dt;            /**< Seconds per simulation step, e.g. 1.0/60.0. */
    F64  max_frame_seconds;   /**< Clamp for a single feed, e.g. 0.25 - spiral-of-death guard. */
    bool paused;              /**< When true, timestep_advance consumes no wall-clock time. */
    bool step_queued;         /**< One queued single-step while paused, consumed by the next advance. */
} Timestep;

/*==============================================================================
 * MARK: - Timestep API
 *============================================================================*/

/**
 * @brief Feed wall-clock seconds and consume as many whole fixed steps as fit.
 *
 * Negative or NaN frame_seconds contributes nothing (a broken clock produces NaN);
 * positive values, +Inf included, are capped at max_frame_seconds before being added
 * to accumulator. While self->paused, no
 * wall-clock time is consumed: the return is 1 exactly once if a step was
 * queued via timestep_step_queue (which this call then clears), else 0.
 *
 * @param self Accumulator to advance.
 * @param frame_seconds Wall-clock seconds elapsed since the previous call.
 * @return Number of whole fixed_dt steps the caller should simulate.
 */
U32 timestep_advance(Timestep *const self, F64 const frame_seconds);

/**
 * @brief Render-interpolation alpha for the leftover accumulator.
 * @param self Accumulator to read.
 * @return accumulator / fixed_dt, in [0, 1).
 */
FSize timestep_alpha(Timestep const *const self);

/**
 * @brief Build a Timestep accumulator.
 * @param fixed_dt Seconds per simulation step; refused unless finite and > 0.
 * @param max_frame_seconds Clamp applied to a single frame_seconds feed; refused unless finite,
 *        >= fixed_dt, <= MATH_TIMESTEP_SECONDS_MAX and <= MATH_TIMESTEP_STEPS_MAX * fixed_dt.
 * @return Zeroed accumulator, not paused, with no step queued - or the fully zeroed Timestep when
 *         the configuration is refused (fixed_dt == 0 identifies it).
 */
Timestep timestep_init(F64 const fixed_dt, F64 const max_frame_seconds);

/**
 * @brief Pause or unpause the accumulator.
 * @param self Accumulator to update.
 * @param paused True to pause (timestep_advance stops consuming wall-clock time).
 */
void timestep_pause_set(Timestep *const self, bool const paused);

/**
 * @brief Drop the leftover accumulator and any queued single step, keeping the configuration.
 *
 * For a scene change or a teleport: the fixed step and frame clamp stay, but no simulation time
 * carried over from before the reset is consumed after it. The pause flag is left as it was.
 *
 * @param self Timestep to reset.
 */
void timestep_reset(Timestep *const self);

/**
 * @brief Queue exactly one simulation step to run on the next advance while paused.
 *
 * No-op when self is not currently paused.
 *
 * @param self Accumulator to update.
 */
void timestep_step_queue(Timestep *const self);

#endif // MATH_TIMESTEP_H