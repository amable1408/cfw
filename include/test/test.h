/*
 * test.h - Small test runner for the C Libraries Framework
 *
 * Features:
 *   - Jest-like suite, case, section, and summary output
 *   - Boolean, numeric, pointer, and string assertions
 *   - API coverage counters
 *   - Elapsed timing
 *
 * Usage Example:
 *   @code
 *   Test test = test_init("./example.test.c");
 *   test_suite_begin(&test, "parser");
 *   test_case_begin(&test, "small object");
 *   test_expect_true(&test, "json != nullptr", json != nullptr);
 *   test_case_end(&test);
 *   test_suite_end(&test);
 *   return test_uninit(&test);
 *   @endcode
 *
 * Error Handling:
 *   - Null test handles abort via error_check_null under ERROR_CHECK_ENABLED
 *     (without it, a null handle is a plain null dereference).
 *   - Failed assertions are counted and reported; they never abort.
 *   - test_uninit FAILS a run that recorded zero assertions and zero API
 *     coverage (the vacuous-pass guard): green requires something to have run.
 *   - test_uninit also FAILS a run with any suite, case, or section still open
 *     (the unbalanced-scope guard): an end call that never ran means the pass
 *     bookkeeping is incomplete.
 *   - Works without log_init: harness output falls back to stdout. With an
 *     initialized log, the configured stream is honored - including a
 *     deliberately disabled (nullptr) stream, which silences the output while
 *     counters and the exit code keep working.
 *
 * Thread Safety:
 *   - Not thread-safe. One Test instance is intended per runner.
 *
 * Memory Management:
 *   - Test stores borrowed labels only. Caller owns label lifetime.
 *
 * Performance Characteristics:
 *   - O(1) per assertion.
 *
 * Dependencies:
 *   - <error/error.h> (chains tracelog -> log -> the standard headers used
 *     here); the assertion family calls error_check_null and traces via
 *     trace_log_push/pop.
 */
#ifndef TEST_H
#define TEST_H

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/
#include <error/error.h> // chains tracelog.h; test.c's error_check_* need it directly (thread.h no longer smuggles it in)

/*==============================================================================
 * MARK: - Types
 *============================================================================*/
typedef struct {
    char const  *case_name;
    char const  *name;
    char const  *section_name;
    char const  *suite_name;
    USize       api_count;
    USize       api_failure_count;
    USize       api_pass_count;
    USize       api_total_count;
    USize       assertion_count;
    USize       assertion_failure_count;
    USize       assertion_pass_count;
    USize       case_count;
    USize       case_failure_count;
    USize       case_pass_count;
    USize       current_case_failure_count;
    USize       current_section_failure_count;
    USize       current_suite_failure_count;
    USize       section_count;
    USize       section_failure_count;
    USize       section_pass_count;
    USize       suite_count;
    USize       suite_failure_count;
    USize       suite_pass_count;
    U64         case_started_at;
    U64         section_started_at;
    U64         started_at;
    U64         suite_started_at;
    bool        case_open;
    bool        section_open;
    bool        suite_open;
    bool        verbose;
} Test;

/*==============================================================================
 * MARK: - Public API
 *============================================================================*/
void test_api_begin(Test *const self, char const *const name, USize const total_count);

void test_api_end(Test *const self);

bool test_api_fail(Test *const self, char const *const name);

bool test_api_pass(Test *const self, char const *const name);

void test_case_begin(Test *const self, char const *const name);

void test_case_end(Test *const self);

bool test_expect_bool(Test *const self, char const *const name, bool const expected, bool const actual);

bool test_expect_f(Test *const self, char const *const name, FSize const expected, FSize const actual, FSize const tolerance);

bool test_expect_false(Test *const self, char const *const name, bool const actual);

bool test_expect_i(Test *const self, char const *const name, ISize const expected, ISize const actual);

bool test_expect_not_null(Test *const self, char const *const name, void const *const actual);

bool test_expect_null(Test *const self, char const *const name, void const *const actual);

bool test_expect_string(Test *const self, char const *const name, char const *const expected, char const *const actual);

bool test_expect_string_contains(Test *const self, char const *const name, char const *const text, char const *const search);

bool test_expect_true(Test *const self, char const *const name, bool const actual);

bool test_expect_u(Test *const self, char const *const name, USize const expected, USize const actual);

Test test_init(char const *const name);

void test_section_begin(Test *const self, char const *const name);

void test_section_end(Test *const self);

void test_suite_begin(Test *const self, char const *const name);

void test_suite_end(Test *const self);

I32 test_uninit(Test *const self);

void test_verbose_set(Test *const self, bool const value);

#endif // TEST_H