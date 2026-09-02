#include <stdio.h>
#include <string.h>
#ifdef OS_WINDOWS
#include <io.h>
#else
#include <unistd.h>
#endif

#include <test/test.h>

/* This suite verifies the test harness module (test.h/test.c) itself, so it cannot
 * grade itself with its own API. Outer verification is a bare hand-rolled counter;
 * the Test instances built below are the SUBJECT under test, inspected via direct
 * struct-field reads and return-value checks. */
static USize _outer_total    = 0;
static USize _outer_failures = 0;

static void _outer_check(char const *const label, bool const condition) {
    _outer_total += 1;

    if (!condition) {
        _outer_failures += 1;
        fprintf(stderr, "  OUTER FAIL: %s\n", label);
    }
}

/* Shared stdout-fd capture helpers (dup/dup2, not freopen("CON",...) -- see
 * _check_output_format for why). Used by every check that needs to inspect the
 * harness's actual printed bytes. */
typedef struct {
    char const  *path;
    FILE        *capture_stream;
    I32         saved_stdout_fd;
    bool        ok;
} _CaptureState;

static _CaptureState _capture_start(char const *const path) {
    _CaptureState state = { .path = path, .capture_stream = nullptr, .saved_stdout_fd = -1, .ok = false };

    fflush(stdout);

#ifdef OS_WINDOWS
    state.saved_stdout_fd = _dup(_fileno(stdout));
#else
    state.saved_stdout_fd = dup(fileno(stdout));
#endif
    state.capture_stream  = fopen(path, "w");
    state.ok              = state.saved_stdout_fd != -1 && state.capture_stream != nullptr;

    if (state.ok) {
#ifdef OS_WINDOWS
        _dup2(_fileno(state.capture_stream), _fileno(stdout));
#else
        dup2(fileno(state.capture_stream), fileno(stdout));
#endif
    }

    return state;
}

static void _capture_end(_CaptureState *const self, char *const buffer, USize const buffer_size) {
    if (!self->ok) {
        return;
    }

    fflush(stdout);
#ifdef OS_WINDOWS
    _dup2(self->saved_stdout_fd, _fileno(stdout));
    _close(self->saved_stdout_fd);
#else
    dup2(self->saved_stdout_fd, fileno(stdout));
    close(self->saved_stdout_fd);
#endif
    fclose(self->capture_stream);

    FILE *const captured = fopen(self->path, "r");

    if (captured != nullptr) {
        fread(buffer, 1, buffer_size - 1, captured);
        fclose(captured);
    }

    remove(self->path);
}

/*== every test_expect_* variant, passing polarity, once each ==*/
static void _check_passing_expectations(void) {
    Test test = test_init("subject-pass");

    test_verbose_set(&test, false);
    test_suite_begin(&test, "suite");
    test_case_begin(&test, "case");

    bool const r_bool  = test_expect_bool(&test, "bool", true, true);
    bool const r_true  = test_expect_true(&test, "true", true);
    bool const r_false = test_expect_false(&test, "false", false);
    bool const r_i     = test_expect_i(&test, "i", -5, -5);
    bool const r_u     = test_expect_u(&test, "u", 5, 5);
    bool const r_f     = test_expect_f(&test, "f", 1.0, 1.0005, 0.01);
    bool const r_str   = test_expect_string(&test, "string", "abc", "abc");
    bool const r_nn    = test_expect_not_null(&test, "not_null", (void*) &test);
    bool const r_null  = test_expect_null(&test, "null", nullptr);
    bool const r_data  = test_expect_string_contains(&test, "data_contains", "hello world", "world");

    _outer_check("bool passing returns true", r_bool);
    _outer_check("true passing returns true", r_true);
    _outer_check("false passing returns true", r_false);
    _outer_check("i passing returns true", r_i);
    _outer_check("u passing returns true", r_u);
    _outer_check("f passing returns true", r_f);
    _outer_check("string passing returns true", r_str);
    _outer_check("not_null passing returns true", r_nn);
    _outer_check("null passing returns true", r_null);
    _outer_check("data_contains passing returns true", r_data);

    _outer_check("10 passing assertions recorded", test.assertion_count == 10);
    _outer_check("all 10 recorded as pass", test.assertion_pass_count == 10);
    _outer_check("zero failures recorded", test.assertion_failure_count == 0);
    _outer_check("current_case_failure_count stays 0", test.current_case_failure_count == 0);

    test_case_end(&test);
    _outer_check("case_pass_count incremented on all-pass case", test.case_pass_count == 1);
    _outer_check("case_failure_count stays 0", test.case_failure_count == 0);

    test_suite_end(&test);
    _outer_check("suite_pass_count incremented", test.suite_pass_count == 1);

    I32 const result = test_uninit(&test);

    _outer_check("test_uninit returns 0 (PASS) when everything passed", result == 0);
}

/*== every test_expect_* variant, failing polarity, once each -- must RECORD, not abort ==*/
static void _check_failing_expectations(void) {
    Test test = test_init("subject-fail");

    test_verbose_set(&test, false);
    test_suite_begin(&test, "suite");
    test_case_begin(&test, "case");

    bool const r_bool  = test_expect_bool(&test, "bool", true, false);
    bool const r_true  = test_expect_true(&test, "true", false);
    bool const r_false = test_expect_false(&test, "false", true);
    bool const r_i     = test_expect_i(&test, "i", 5, 6);
    bool const r_u     = test_expect_u(&test, "u", 5, 6);
    bool const r_f     = test_expect_f(&test, "f", 1.0, 2.0, 0.01);
    bool const r_str   = test_expect_string(&test, "string", "abc", "xyz");
    bool const r_nn    = test_expect_not_null(&test, "not_null", nullptr);
    bool const r_null  = test_expect_null(&test, "null", (void*) &test);
    bool const r_data  = test_expect_string_contains(&test, "data_contains", "hello world", "planet");

    /* reaching this line at all proves none of the 10 failures above aborted the process */
    _outer_check("process survived all 10 failing assertions (record, not abort)", true);

    _outer_check("bool failing returns false", r_bool == false);
    _outer_check("true failing returns false", r_true == false);
    _outer_check("false failing returns false", r_false == false);
    _outer_check("i failing returns false", r_i == false);
    _outer_check("u failing returns false", r_u == false);
    _outer_check("f failing returns false", r_f == false);
    _outer_check("string failing returns false", r_str == false);
    _outer_check("not_null failing returns false", r_nn == false);
    _outer_check("null failing returns false", r_null == false);
    _outer_check("data_contains failing returns false", r_data == false);

    _outer_check("10 assertions recorded", test.assertion_count == 10);
    _outer_check("all 10 recorded as failures", test.assertion_failure_count == 10);
    _outer_check("zero passes recorded", test.assertion_pass_count == 0);
    _outer_check("current_case_failure_count is 10", test.current_case_failure_count == 10);

    test_case_end(&test);
    _outer_check("case_failure_count incremented on all-fail case", test.case_failure_count == 1);
    _outer_check("case_pass_count stays 0", test.case_pass_count == 0);

    test_suite_end(&test);
    _outer_check("suite_failure_count incremented", test.suite_failure_count == 1);

    I32 const result = test_uninit(&test);

    _outer_check("test_uninit returns 1 (FAIL) when assertions failed", result == 1);
}

/*== vacuity guard: a run with ZERO assertions and ZERO api coverage now FAILS and prints
 * the VACUOUS marker (flipped from the old "silently reports PASS" contract). ==*/
static void _check_vacuity(void) {
    char buffer[512] = DEFAULT_INITIALIZATION;

    _CaptureState capture = _capture_start("test_vacuity_capture.tmp");

    Test test = test_init("subject-vacuity");

    I32 const result = test_uninit(&test);

    _capture_end(&capture, buffer, sizeof(buffer));

    _outer_check("zero-assertion, zero-api run now FAILS (vacuity guard)", result == 1);

    if (capture.ok) {
        _outer_check("vacuous run prints the VACUOUS marker", strstr(buffer, "VACUOUS") != nullptr);
    }
}

/*== api_begin/api_pass/api_fail/api_end counters, and effective-failure accounting ==*/
static void _check_api_full_coverage_with_a_failure(void) {
    Test test = test_init("subject-api-full");

    test_api_begin(&test, "widget", 3);

    bool const p1 = test_api_pass(&test, "fn_a");
    bool const p2 = test_api_pass(&test, "fn_b");
    bool const f1 = test_api_fail(&test, "fn_c");

    test_api_end(&test);

    _outer_check("api_pass returns true", p1 && p2);
    _outer_check("api_fail returns false", f1 == false);
    _outer_check("api_count tallies 3 calls", test.api_count == 3);
    _outer_check("api_pass_count is 2", test.api_pass_count == 2);
    _outer_check("api_failure_count is 1", test.api_failure_count == 1);

    I32 const result = test_uninit(&test);

    _outer_check("fully-covered api with 1 explicit failure still FAILs", result == 1);
}

static void _check_api_uncovered_counts_as_failure(void) {
    Test test = test_init("subject-api-uncovered");

    test_api_begin(&test, "widget", 5);
    test_api_pass(&test, "fn_a");
    test_api_end(&test);

    I32 const result = test_uninit(&test);

    _outer_check("uncovered api functions count as failures even with 0 assertions", result == 1);
}

static void _check_api_exact_coverage_passes(void) {
    Test test = test_init("subject-api-exact");

    test_api_begin(&test, "widget", 2);
    test_api_pass(&test, "fn_a");
    test_api_pass(&test, "fn_b");
    test_api_end(&test);

    I32 const result = test_uninit(&test);

    _outer_check("fully covered + all-passed api reports PASS", result == 0);
}

/*== api_total_count now ACCUMULATES across test_api_begin blocks instead of resetting;
 * a failure recorded in an earlier block must survive a later, fully-covered block
 * (regression pin: the old reset erased it from the exit code -- a false green). ==*/
static void _check_api_accumulates_across_blocks(void) {
    Test test = test_init("subject-api-accumulate");

    test_api_begin(&test, "block-one", 3);
    test_api_pass(&test, "fn_a");
    test_api_pass(&test, "fn_b");
    test_api_fail(&test, "fn_c");
    test_api_end(&test);

    test_api_begin(&test, "block-two", 2);
    test_api_pass(&test, "fn_d");
    test_api_pass(&test, "fn_e");
    test_api_end(&test);

    _outer_check("api_total_count accumulates across blocks (3 + 2 = 5)", test.api_total_count == 5);
    _outer_check("api_failure_count from block one survives block two", test.api_failure_count == 1);

    I32 const result = test_uninit(&test);

    _outer_check("a failure in an earlier api block still FAILs after a fully-covered later block", result == 1);
}

/*== section_begin/section_end counters, and that a section failure propagates to the case ==*/
static void _check_sections(void) {
    Test test = test_init("subject-section");

    test_suite_begin(&test, "suite");
    test_case_begin(&test, "case");

    test_section_begin(&test, "section-pass");
    test_expect_true(&test, "ok", true);
    test_section_end(&test);

    _outer_check("section_pass_count incremented", test.section_pass_count == 1);
    _outer_check("section_failure_count stays 0 after a passing section", test.section_failure_count == 0);

    test_section_begin(&test, "section-fail");
    test_expect_true(&test, "boom", false);
    test_section_end(&test);

    _outer_check("section_failure_count incremented", test.section_failure_count == 1);
    _outer_check("current_case_failure_count reflects the section failure", test.current_case_failure_count == 1);

    test_case_end(&test);
    test_suite_end(&test);

    I32 const result = test_uninit(&test);

    _outer_check("uninit reflects the section failure", result == 1);
}

/*== verbose_set toggles the field both ways ==*/
static void _check_verbose_toggle(void) {
    Test test = test_init("subject-verbose");

    _outer_check("verbose defaults to false", test.verbose == false);

    test_verbose_set(&test, true);
    _outer_check("verbose_set(true) takes effect", test.verbose == true);

    test_verbose_set(&test, false);
    _outer_check("verbose_set(false) takes effect", test.verbose == false);

    test_uninit(&test);
}

/*== an *_end with no matching *_begin is a counted no-op -- it must not inflate any
 * pass/failure counter (case_open/section_open/suite_open guard). ==*/
static void _check_bare_end_is_noop(void) {
    Test test = test_init("subject-bare-end");

    test_case_end(&test);
    _outer_check("bare case_end leaves case_pass_count at 0", test.case_pass_count == 0);
    _outer_check("bare case_end leaves case_failure_count at 0", test.case_failure_count == 0);

    test_section_end(&test);
    _outer_check("bare section_end leaves section_pass_count at 0", test.section_pass_count == 0);
    _outer_check("bare section_end leaves section_failure_count at 0", test.section_failure_count == 0);

    test_suite_end(&test);
    _outer_check("bare suite_end leaves suite_pass_count at 0", test.suite_pass_count == 0);
    _outer_check("bare suite_end leaves suite_failure_count at 0", test.suite_failure_count == 0);

    test_uninit(&test);
}

/*== an open scope (case_begin with no matching case_end, etc.) at uninit is a
 * structurally defective run -- must FAIL with the UNBALANCED marker even though a
 * real, passing assertion was recorded (so this is not just the vacuity guard). ==*/
static void _check_unbalanced_scope_fails(void) {
    char buffer[512] = DEFAULT_INITIALIZATION;

    _CaptureState capture = _capture_start("test_unbalanced_capture.tmp");

    Test test = test_init("subject-unbalanced");

    test_case_begin(&test, "never closed");
    test_expect_true(&test, "a real passing assertion", true);
    /* deliberately no test_case_end -- case_open stays true */

    I32 const result = test_uninit(&test);

    _capture_end(&capture, buffer, sizeof(buffer));

    _outer_check("an unclosed case FAILs at uninit even with a passing assertion (not vacuous)", result == 1);

    if (capture.ok) {
        _outer_check("unbalanced run prints the UNBALANCED marker", strstr(buffer, "UNBALANCED") != nullptr);
    }
}

/*== a disabled (nullptr) log stream SILENCES harness output while counters and the
 * exit code keep working. ==*/
static void _check_silenced_stream(void) {
    char buffer[512] = DEFAULT_INITIALIZATION;

    log_set_stream(nullptr);

    _CaptureState capture = _capture_start("test_silenced_capture.tmp");

    Test test = test_init("subject-silenced");

    test_suite_begin(&test, "suite");
    test_case_begin(&test, "case");
    test_expect_true(&test, "will fail on purpose", false);

    USize const failure_count_before_uninit = test.assertion_failure_count;

    test_case_end(&test);
    test_suite_end(&test);

    I32 const result = test_uninit(&test);

    _capture_end(&capture, buffer, sizeof(buffer));

    log_set_stream(stdout); /* restore -- log is process-global, later checks need it back */

    _outer_check("counters still work while the stream is silenced", failure_count_before_uninit == 1);
    _outer_check("exit code still reflects the failure while silenced", result == 1);

    if (capture.ok) {
        _outer_check("silenced stream writes ZERO harness bytes to stdout", buffer[0] == '\0');
    }
}

/*== integration check: capture real stdout and assert the documented output format
 * (pass/fail markers, nullptr-name fallback, failure detail, overall banner). ==*/
static void _check_output_format(void) {
    /* Captured via dup/dup2 rather than freopen("CON", ...): freopen to the console
     * device is unreliable when this exe's own stdout is itself already redirected/
     * piped by the invoking shell (no real console to reopen), which would silently
     * swallow every line printed after the restore. dup/dup2 instead saves and
     * restores whatever fd 1 actually was, console or pipe alike. */
    char buffer[4096] = DEFAULT_INITIALIZATION;

    _CaptureState capture = _capture_start("test_output_capture.tmp");

    _outer_check("stdout captured for the output-format check", capture.ok);

    if (capture.ok) {
        Test test = test_init("(subject)");

        test_verbose_set(&test, true);
        test_suite_begin(&test, nullptr);
        test_case_begin(&test, "captured-case");
        test_expect_true(&test, "pass-line", true);
        test_expect_true(&test, "fail-line", false);
        test_case_end(&test);
        test_suite_end(&test);
        test_uninit(&test);
    }

    _capture_end(&capture, buffer, sizeof(buffer));

    _outer_check("output falls back to (unnamed) for a nullptr suite name", strstr(buffer, "(unnamed)") != nullptr);
    _outer_check("output contains a pass marker line", strstr(buffer, "+ pass-line") != nullptr);
    _outer_check("output contains a fail marker line", strstr(buffer, "x fail-line") != nullptr);
    _outer_check("output contains the failure detail", strstr(buffer, "expected=true actual=false") != nullptr);
    _outer_check("output contains the overall FAIL banner", strstr(buffer, "FAIL") != nullptr);
    _outer_check("output contains the Assertions summary line", strstr(buffer, "Assertions:") != nullptr);
}

int main(void) {
    log_init((LogConfig) { .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true });

    printf("== tests/test: exercising the test harness module itself ==\n");

    _check_passing_expectations();
    _check_failing_expectations();
    _check_vacuity();
    _check_api_full_coverage_with_a_failure();
    _check_api_uncovered_counts_as_failure();
    _check_api_exact_coverage_passes();
    _check_api_accumulates_across_blocks();
    _check_sections();
    _check_verbose_toggle();
    _check_bare_end_is_noop();
    _check_unbalanced_scope_fails();
    _check_silenced_stream();
    _check_output_format();

    printf("\nouter checks: %llu run, %llu failed\n",
        (unsigned long long) _outer_total,
        (unsigned long long) _outer_failures);

    return _outer_failures == 0 ? 0 : 1;
}