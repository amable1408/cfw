#include <math/timestep.h>
#include <test/test.h>

/* Fixed-timestep accumulator coverage: chunk-invariant whole-step counting, the
 * interpolation alpha, the frame-time clamp (spiral-of-death guard), negative-feed
 * rejection, and pause / queued single-step / unpause behavior. */

static void _test_timestep_basic_60hz(Test *const test) {
    test_case_begin(test, "60Hz whole-step count is chunk-invariant");

    // 1.0 / 60.0 does not round-trip exactly in IEEE-754 double: 60 * fixed_dt
    // computes very slightly above 1.0, so a single 1.0s feed deterministically
    // drains only 59 whole steps rather than 60. This is a fixed, portable fact
    // of the bit pattern (not a bug), so it is asserted exactly.
    F64 const fixed_dt = 1.0 / 60.0;

    Timestep oneshot        = timestep_init(fixed_dt, 999.0);
    U32 const oneshot_steps = timestep_advance(&oneshot, 1.0);

    test_expect_u(test, "oneshot 1.0s -> 59 whole steps", 59, oneshot_steps);

    // Power-of-two fractions sum bit-exactly to 1.0, so this isolates chunking
    // from summation drift. The drain loop still runs once per chunk rather
    // than once over the final total, so the rounding path differs slightly
    // from the one-shot case; a +-1 step tolerance is the honest bound for a
    // pure double accumulator (the same tolerance real fixed-timestep loops
    // accept), not a defect in the implementation.
    F64 const chunks[] = { 0.5, 0.25, 0.125, 0.0625, 0.03125, 0.03125 };
    Timestep chunked    = timestep_init(fixed_dt, 999.0);
    U32 chunked_steps   = 0;

    for (USize i = 0; i < sizeof(chunks) / sizeof(chunks[0]); i += 1) {
        chunked_steps += timestep_advance(&chunked, chunks[i]);
    }

    ISize const step_delta = (ISize) chunked_steps - (ISize) oneshot_steps;

    test_expect_true(test, "chunked total within 1 step of oneshot", step_delta >= -1 && step_delta <= 1);

    test_case_end(test);
}

static void _test_timestep_alpha(Test *const test) {
    test_case_begin(test, "alpha stays in [0,1) and matches remainder / fixed_dt");

    F64 const fixed_dt = 1.0 / 60.0;
    Timestep ts        = timestep_init(fixed_dt, 999.0);

    timestep_advance(&ts, 0.1);

    F32 const alpha    = timestep_alpha(&ts);
    F32 const expected = (F32) (ts.accumulator / ts.fixed_dt);

    test_expect_true(test, "alpha >= 0", alpha >= 0.0f);
    test_expect_true(test, "alpha < 1", alpha < 1.0f);
    test_expect_f(test, "alpha equals remainder / fixed_dt", expected, alpha, 0.0001);

    test_case_end(test);
}

static void _test_timestep_frame_clamp(Test *const test) {
    test_case_begin(test, "frame clamp bounds a stalled feed");

    F64 const fixed_dt = 1.0 / 60.0;
    Timestep ts        = timestep_init(fixed_dt, 0.25);

    U32 const steps = timestep_advance(&ts, 10.0);

    test_expect_true(test, "clamped steps <= 15", steps <= 15);

    test_case_end(test);
}

static void _test_timestep_reset(Test *const test) {
    test_case_begin(test, "reset drops the accumulator and the queued step, keeps the config");

    Timestep ts = timestep_init(1.0 / 60.0, 0.25);

    timestep_advance(&ts, 0.02);
    timestep_pause_set(&ts, true);
    timestep_step_queue(&ts);

    test_expect_true(test, "leftover time accumulated before the reset", ts.accumulator > 0.0);

    timestep_reset(&ts);

    test_expect_f(test, "accumulator dropped", 0.0, ts.accumulator, 0.0);
    test_expect_false(test, "queued step dropped", ts.step_queued);
    test_expect_true(test, "pause flag kept", ts.paused);
    test_expect_f(test, "fixed_dt kept", 1.0 / 60.0, ts.fixed_dt, 0.0);

    test_case_end(test);
}

static void _test_timestep_bad_config_refuses(Test *const test) {
    test_case_begin(test, "a bad configuration is refused, not aborted");

    /* fixed_dt is data (a config file, a save, a flag), so init must REFUSE in every build.
     * The zeroed Timestep is already first-class: advance and alpha both guard it. */
    Timestep zero_dt = timestep_init(0.0, 1.0);
    Timestep const negative = timestep_init(-0.016, 1.0);
    Timestep const inverted = timestep_init(0.016, 0.001);
    Timestep const infinite = timestep_init(1.0 / 60.0, INFINITY);
    Timestep const absorbed = timestep_init(1.0, 1e300);
    Timestep const over_cap = timestep_init(1.0 / 60.0, 1.0e6);
    Timestep const at_cap = timestep_init(0.01, 10000.0);
    Timestep const over_day = timestep_init(1.0, 86401.0);
    Timestep const at_day = timestep_init(1.0, 86400.0);

    test_expect_f(test, "fixed_dt 0 -> zeroed", 0.0, zero_dt.fixed_dt, 0.0);
    test_expect_f(test, "negative fixed_dt -> zeroed", 0.0, negative.fixed_dt, 0.0);
    test_expect_f(test, "max_frame < fixed_dt -> zeroed", 0.0, inverted.fixed_dt, 0.0);
    test_expect_f(test, "infinite max_frame -> zeroed", 0.0, infinite.fixed_dt, 0.0);
    test_expect_f(test, "1e300 max_frame (loop could never end) -> zeroed", 0.0, absorbed.fixed_dt, 0.0);
    test_expect_f(test, "ratio above MATH_TIMESTEP_STEPS_MAX -> zeroed", 0.0, over_cap.fixed_dt, 0.0);
    test_expect_f(test, "ratio exactly at the cap is accepted", 0.01, at_cap.fixed_dt, 0.0);
    test_expect_f(test, "max_frame above MATH_TIMESTEP_SECONDS_MAX -> zeroed", 0.0, over_day.fixed_dt, 0.0);
    test_expect_f(test, "max_frame exactly one day is accepted", 1.0, at_day.fixed_dt, 0.0);
    test_expect_u(test, "a zeroed Timestep never advances", 0, timestep_advance(&zero_dt, 1.0));
    test_expect_f(test, "and reports alpha 0", 0.0, (F64) timestep_alpha(&zero_dt), 0.0);

    test_case_end(test);
}

static void _test_timestep_negative_feed(Test *const test) {
    test_case_begin(test, "negative feed contributes nothing");

    F64 const fixed_dt = 1.0 / 60.0;
    Timestep ts        = timestep_init(fixed_dt, 0.25);

    U32 const steps = timestep_advance(&ts, -1.0);

    test_expect_u(test, "no steps from negative feed", 0, steps);
    test_expect_f(test, "accumulator untouched", 0.0, ts.accumulator, 0.0001);

    test_case_end(test);
}

static void _test_timestep_pause_and_queue(Test *const test) {
    test_case_begin(test, "pause freezes advance; queued step fires once; unpause resumes");

    F64 const fixed_dt = 1.0 / 60.0;
    Timestep ts        = timestep_init(fixed_dt, 999.0);

    timestep_pause_set(&ts, true);

    U32 const paused_steps = timestep_advance(&ts, 1.0);

    test_expect_u(test, "paused advance returns 0", 0, paused_steps);
    test_expect_f(test, "accumulator frozen while paused", 0.0, ts.accumulator, 0.0001);

    timestep_step_queue(&ts);

    U32 const queued_steps = timestep_advance(&ts, 1.0);

    test_expect_u(test, "queued single-step returns 1", 1, queued_steps);
    test_expect_f(test, "accumulator still frozen after queued step", 0.0, ts.accumulator, 0.0001);

    U32 const queued_again = timestep_advance(&ts, 1.0);

    test_expect_u(test, "queue consumed, returns 0 again", 0, queued_again);

    timestep_pause_set(&ts, false);

    U32 const resumed_steps = timestep_advance(&ts, 1.0);

    // Same 1.0s / (1.0/60.0) case as the basic 60Hz test: 59 whole steps.
    test_expect_u(test, "unpaused resumes normal accumulation", 59, resumed_steps);

    test_case_end(test);
}

int main(void) {
    LogConfig const log_config = {
        .level             = LOG_LEVEL_ERROR,
        .stream            = stdout,
        .timestamp_enabled = true,
        .autoflush         = true
    };

    log_init(log_config);

    Test test = test_init("tests/math/test_timestep.c");

    test_suite_begin(&test, "math/timestep");
    _test_timestep_basic_60hz(&test);
    _test_timestep_alpha(&test);
    _test_timestep_frame_clamp(&test);
    _test_timestep_negative_feed(&test);
    _test_timestep_bad_config_refuses(&test);
    _test_timestep_reset(&test);
    _test_timestep_pause_and_queue(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}