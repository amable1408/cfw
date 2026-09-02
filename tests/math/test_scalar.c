/*
 * test_scalar.c - Unit test runner for the CFW math scalar range helpers
 *
 * Covers math_clamp_f / math_lerp_f / math_remap_f (the axis/normalize range
 * math the chart engine builds on) with exact-value asserts, including the
 * remap-is-the-inverse-of-lerp identity.
 */

#include <log/log.h>
#include <math/scalar.h>
#include <test/test.h>

/*==============================================================================
 * MARK: - Clamp suite
 *============================================================================*/

static void _test_clamp(Test *const self) {
    test_suite_begin(self, "clamp");

    test_case_begin(self, "clamp bounds a value to [min, max]");

    test_expect_f(self, "below floors to min", 0.0, math_clamp_f(-5.0, 0.0, 10.0), 0.0);
    test_expect_f(self, "above ceils to max", 10.0, math_clamp_f(42.0, 0.0, 10.0), 0.0);
    test_expect_f(self, "inside passes through", 7.5, math_clamp_f(7.5, 0.0, 10.0), 0.0);
    test_expect_f(self, "on the lower bound is kept", 0.0, math_clamp_f(0.0, 0.0, 10.0), 0.0);
    test_expect_f(self, "a negative range clamps too", -3.0, math_clamp_f(-9.0, -3.0, 3.0), 0.0);

    test_case_end(self);

    test_suite_end(self);
}

/*==============================================================================
 * MARK: - Lerp suite
 *============================================================================*/

static void _test_lerp(Test *const self) {
    test_suite_begin(self, "lerp");

    test_case_begin(self, "lerp interpolates and extrapolates");

    test_expect_f(self, "t=0 is a", 10.0, math_lerp_f(10.0, 20.0, 0.0), 0.0);
    test_expect_f(self, "t=1 is b", 20.0, math_lerp_f(10.0, 20.0, 1.0), 0.0);
    test_expect_f(self, "t=0.5 is the midpoint", 15.0, math_lerp_f(10.0, 20.0, 0.5), 0.0);
    test_expect_f(self, "t=2 extrapolates past b", 30.0, math_lerp_f(10.0, 20.0, 2.0), 0.0);

    test_case_end(self);

    test_suite_end(self);
}

/*==============================================================================
 * MARK: - Remap suite
 *============================================================================*/

static void _test_remap(Test *const self) {
    test_suite_begin(self, "remap");

    test_case_begin(self, "remap moves a value between ranges");

    test_expect_f(self, "in_min maps to out_min", 0.0, math_remap_f(0.0, 0.0, 10.0, 0.0, 100.0), 0.0001);
    test_expect_f(self, "in_max maps to out_max", 100.0, math_remap_f(10.0, 0.0, 10.0, 0.0, 100.0), 0.0001);
    test_expect_f(self, "midpoint maps to midpoint", 50.0, math_remap_f(5.0, 0.0, 10.0, 0.0, 100.0), 0.0001);
    test_expect_f(self, "inverse-lerp onto [0, 1]", 0.25, math_remap_f(2.5, 0.0, 10.0, 0.0, 1.0), 0.0001);
    test_expect_f(self, "remap is the inverse of lerp", 0.3, math_remap_f(math_lerp_f(4.0, 9.0, 0.3), 4.0, 9.0, 0.0, 1.0), 0.0001);

    test_case_end(self);

    test_suite_end(self);
}

/*==============================================================================
 * MARK: - Runner
 *============================================================================*/

int main(void) {
    LogConfig const log_config = {
        .level             = LOG_LEVEL_ERROR,
        .stream            = stdout,
        .timestamp_enabled = false,
        .autoflush         = false
    };

    log_init(log_config);

    Test test = test_init("tests/math/test_scalar.c");

    test_suite_begin(&test, "scalar");

    _test_clamp(&test);
    _test_lerp(&test);
    _test_remap(&test);

    test_suite_end(&test);

    return test_uninit(&test);
}