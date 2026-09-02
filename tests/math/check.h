/*
 * check.h - The one hand-rolled reporter shared by the math suites
 *
 * Thirty-five of the thirty-seven math suites carried an identical 50-line block of
 * _check_f / _check_u / _check_i / _check_b helpers plus their _pass / _fail counters. This is
 * that block, once. It is deliberately NOT a port to <test/test.h>: the two harnesses disagree
 * on argument order (_check_f(name, got, expected, tol) versus test_expect_f(t, name, expected,
 * got, tol)), and a mechanical conversion that gets the order wrong still passes every
 * symmetric assertion - so each suite converts on its own, with a prove-it-fails run, when it
 * is next touched. Until then `make test` needs only the exit code, which both shapes give.
 *
 * Usage Examples:
 *   @code
 *   #include "check.h"
 *
 *   int main(void) {
 *       _check_f("add_2.x", math_vec2_add_2(a, b).x, 4.0, 1e-9);
 *       _check_b("equal_2", math_rect_equal_2(r, r), true);
 *
 *       return _check_finish();
 *   }
 *   @endcode
 *
 * Error Handling:
 *   - A failed check prints FAIL with the expected and actual values and counts; nothing
 *     aborts. _check_finish prints the totals and returns the exit code the suite must return:
 *     0 only when at least one check ran and none failed. A suite that ran ZERO checks exits 1,
 *     so an empty or skipped suite cannot read as green.
 *
 * Thread Safety:
 *   - Not thread-safe; the counters are plain statics. The suites are single-threaded.
 *
 * Memory Management:
 *   - No allocation.
 *
 * Dependencies:
 *   - <stdio.h> for printf; <types.h> for FSize / ISize / USize.
 */

#ifndef TESTS_MATH_CHECK_H
#define TESTS_MATH_CHECK_H

#include <stdio.h>

#include <types.h>

/*==============================================================================
 * MARK: - Accumulators
 *============================================================================*/

static ISize _pass = 0;
static ISize _fail = 0;

/*==============================================================================
 * MARK: - Checks
 *============================================================================*/

static inline void _check_f(char const *const name, FSize const got, FSize const expected, FSize const tol) {
    /* Equality first: Inf - Inf is NaN, so an Inf expectation could never pass the tolerance path.
     * A NaN expectation still never passes (NaN == NaN is false, NaN - x is NaN) - by design: the
     * module refuses to ZERO, never to NaN, so no suite has one to assert. */
    FSize diff = (got == expected) ? 0 : got - expected;

    if (diff < 0) {
        diff = -diff;
    }

    if (diff <= tol) {
        printf("  PASS  %s  (got %.17g)\n", name, (double) got);
        _pass += 1;
    }
    else {
        printf("  FAIL  %s  expected %.17g  got %.17g\n", name, (double) expected, (double) got);
        _fail += 1;
    }
}

static inline void _check_u(char const *const name, USize const got, USize const expected) {
    if (got == expected) {
        printf("  PASS  %s  (got %zu)\n", name, got);
        _pass += 1;
    }
    else {
        printf("  FAIL  %s  expected %zu  got %zu\n", name, expected, got);
        _fail += 1;
    }
}

static inline void _check_i(char const *const name, ISize const got, ISize const expected) {
    if (got == expected) {
        printf("  PASS  %s  (got %zd)\n", name, got);
        _pass += 1;
    }
    else {
        printf("  FAIL  %s  expected %zd  got %zd\n", name, expected, got);
        _fail += 1;
    }
}

static inline void _check_b(char const *const name, bool const got, bool const expected) {
    if (got == expected) {
        printf("  PASS  %s  (got %s)\n", name, got ? "true" : "false");
        _pass += 1;
    }
    else {
        printf("  FAIL  %s  expected %s  got %s\n", name, expected ? "true" : "false", got ? "true" : "false");
        _fail += 1;
    }
}

/*==============================================================================
 * MARK: - Finish
 *============================================================================*/

/* Prints the totals in the format the runners parse (`=== Results: N passed, M failed ===`) and
 * returns the suite's exit code. Zero checks is a failure: a suite that never asserted anything
 * must not read as green. */
static inline int _check_finish(void) {
    printf("\n=== Results: %zd passed, %zd failed ===\n", _pass, _fail);

    return (_fail == 0 && _pass > 0) ? 0 : 1;
}

#endif // TESTS_MATH_CHECK_H