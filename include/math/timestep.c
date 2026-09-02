/*
 * timestep.c - Fixed-timestep accumulator for the CFW math module.
 *
 * See timestep.h for API documentation and usage examples.
 */

#include <math/timestep.h>

/*==============================================================================
 * MARK: - Timestep API
 *============================================================================*/

U32 timestep_advance(Timestep *const self, F64 const frame_seconds) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (self->fixed_dt <= 0.0) {
        /* A hand-built (zero) Timestep must not spin the drain loop forever. */
        trace_log_pop();

        return 0;
    }

    if (self->paused) {
        U32 const steps = self->step_queued ? 1 : 0;
        self->step_queued = false;

        trace_log_pop();

        return steps;
    }

    F64 clamped_frame_seconds = frame_seconds;

    if (clamped_frame_seconds != clamped_frame_seconds || clamped_frame_seconds < 0.0) {
        clamped_frame_seconds = 0.0;
    }
    else if (clamped_frame_seconds > self->max_frame_seconds) {
        clamped_frame_seconds = self->max_frame_seconds;
    }

    self->accumulator += clamped_frame_seconds;

    U32 steps = 0;

    while (self->accumulator >= self->fixed_dt) {
        self->accumulator -= self->fixed_dt;
        steps += 1;
    }

    trace_log_pop();

    return steps;
}

FSize timestep_alpha(Timestep const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (self->fixed_dt <= 0.0) {
        /* A hand-built (zero) Timestep must not produce a NaN alpha. */
        trace_log_pop();

        return 0.0;
    }

    FSize const alpha = self->accumulator / self->fixed_dt;

    trace_log_pop();

    return alpha;
}

Timestep timestep_init(F64 const fixed_dt, F64 const max_frame_seconds) {
    trace_log_push(LOG_METADATA);

    /* A bad configuration is DATA (a config file, a save, a CLI flag), not a programming
     * error, so it refuses in every build rather than aborting in checked ones. The zeroed
     * Timestep is already a first-class state here: timestep_advance and timestep_alpha both
     * guard a zero fixed_dt, so the refusal costs nothing the module did not already pay.
     * The two caps keep the drain loop finite: a ratio near 1e16 makes accumulator - fixed_dt
     * == accumulator in F64 and the loop never ends, 1e10 is a multi-minute stall, and past the
     * magnitude cap one feed can overflow the accumulator to Inf, which never drains. */
    bool const refused = !isfinite(fixed_dt) || !isfinite(max_frame_seconds) ||
        !(fixed_dt > 0) || !(max_frame_seconds >= fixed_dt)                  ||
        max_frame_seconds > MATH_TIMESTEP_SECONDS_MAX                        ||
        max_frame_seconds / fixed_dt > MATH_TIMESTEP_STEPS_MAX;

    if (refused) {
        Timestep const zeroed = DEFAULT_INITIALIZATION;

        trace_log_pop();

        return zeroed;
    }

    Timestep const self = {
        .accumulator       = 0.0,
        .fixed_dt          = fixed_dt,
        .max_frame_seconds = max_frame_seconds,
        .paused            = false,
        .step_queued       = false
    };

    trace_log_pop();

    return self;
}

void timestep_pause_set(Timestep *const self, bool const paused) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    self->paused = paused;

    trace_log_pop();
}

void timestep_reset(Timestep *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    self->accumulator = 0.0;
    self->step_queued = false;

    trace_log_pop();
}

void timestep_step_queue(Timestep *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (self->paused) {
        self->step_queued = true;
    }

    trace_log_pop();
}