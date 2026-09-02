/*
 * test_all.c - Behavioral tests for include/error/error.c.
 *
 * A failing error_check_* aborts the process (see error.h), so the abort-on-failure half of
 * the contract cannot be observed in-process without killing the runner after the first case.
 * Instead the suite spawns ITSELF as a child, re-entered through the --child-* arguments below
 * (the same pattern tests/process/test_all.c uses to test process_run), and asserts on the
 * child's exit status and captured output rather than catching a signal in-process.
 *
 * This suite deliberately does NOT link include/test/test.c: that shared harness module calls
 * error_check_null (test.c:73) without including <error/error.h>, which is a hard compile error
 * (implicit-function-declaration) under this project's -std=c23 - confirmed to also break a
 * clean rebuild of tests/tuple, so it is a pre-existing repo-wide defect, not something specific
 * to this suite. Fixing it is out of scope here (it is not under tests/error/), so this suite
 * uses the same self-contained pass/fail counter tests/graphics/test_color.c already uses.
 */
#include <stdio.h>
#include <string.h>

#include <error/error.h>
#include <log/log.h>
#include <process/process.h>

#ifdef OS_WINDOWS
#include <io.h>
#endif // OS_WINDOWS

/*==============================================================================
 * MARK: - File Scope
 *============================================================================*/

/** Path this binary was invoked with, reused as the program every spawn runs. */
static char const *_program = nullptr;

/** Bumped by _increment_and_return so a passing check's argument evaluation can be pinned. */
static ISize _side_effect_counter = 0;

static ISize _pass = 0;
static ISize _fail = 0;

/*==============================================================================
 * MARK: - Helpers
 *============================================================================*/

static void _check(char const *const name, bool const ok) {
    if (ok) {
        printf("  PASS  %s\n", name);
        _pass += 1;
    } else {
        printf("  FAIL  %s\n", name);
        _fail += 1;
    }
}

/**
 * @brief Point this process's stderr at its stdout, so process_run's stdout-only capture can
 * see text a child writes to stderr (the log module's uninitialized fallback in error.c).
 */
static void _redirect_stderr_to_stdout(void) {
    fflush(stdout);

#ifdef OS_WINDOWS
    _dup2(_fileno(stdout), _fileno(stderr));
#else
    dup2(fileno(stdout), fileno(stderr));
#endif // OS_WINDOWS
}

/*==============================================================================
 * MARK: - Child Modes
 *============================================================================*/

/**
 * @brief Config shared by every child mode: route the abort's log line to stdout so the
 * parent's process_run capture can see it.
 * @return LogConfig ready for log_init.
 */
static LogConfig _child_log_config(void) {
    return (LogConfig) {
        .level = LOG_LEVEL_ERROR,
        .stream = LOG_STREAM_STDOUT,
        .timestamp_enabled = true,
        .autoflush = true,
    };
}

/**
 * @brief error_check_null on a null pointer must abort. A clean return is the bug.
 * @return Exit code observed only if the check failed to abort.
 */
static I32 _child_null(void) {
    log_init(_child_log_config());

    void const *const value = nullptr;

    error_check_null(LOG_METADATA, "value", value);

    return 0;
}

/**
 * @brief error_check_out_of_bound_int with a true condition must abort.
 *
 * NOTE for the vacuity proof only: flipping the trailing `true` below to `false` turns this
 * into a passing check, which must make _test_abort_probe("--child-out-of-bound") FAIL (the
 * child then exits 0 instead of aborting). That edit-build-run-revert was performed once by
 * hand to prove the probe is not vacuous; the committed file keeps the failing `true`.
 *
 * @return Exit code observed only if the check failed to abort.
 */
static I32 _child_out_of_bound(void) {
    log_init(_child_log_config());

    error_check_out_of_bound_int(LOG_METADATA, "i", (ISize) 5, "max", (ISize) 3, "i >= max", true);

    return 0;
}

/**
 * @brief error_fail_bad_format aborts unconditionally.
 * @return Exit code observed only if the call failed to abort.
 */
static I32 _child_bad_format(void) {
    log_init(_child_log_config());

    error_fail_bad_format(LOG_METADATA, "field", "garbage-value");

    return 0;
}

/**
 * @brief The uninitialized-log twin of _child_null: deliberately never calls log_init, so
 * _error_fail must take its stderr fallback path and still abort (the regression this pins).
 * @return Exit code observed only if the check failed to abort.
 */
static I32 _child_null_uninitialized_log(void) {
    _redirect_stderr_to_stdout();

    void const *const value = nullptr;

    error_check_null(LOG_METADATA, "value", value);

    return 0;
}

/**
 * @brief Return value, having first bumped _side_effect_counter.
 *
 * Passed as a check's condition argument so a case can prove the expression was evaluated
 * even though the check itself is a no-op (the passing branch never reaches the failure path).
 *
 * @param value Value to return unchanged.
 * @return value, unchanged.
 */
static bool _increment_and_return(bool const value) {
    _side_effect_counter += 1;

    return value;
}

/**
 * @brief Spawn this binary with flag, and assert it aborted while logging expected_substring.
 *
 * @param case_name              Case label.
 * @param flag                   --child-* flag identifying which probe to run.
 * @param expected_substring     Text the failure's log line must contain.
 * @param require_exact_abort_status When true, the exit assertion matches abort()'s exact OS
 *        death (see process_outcome_aborted) instead of merely "nonzero" - a plain exit(1) is also
 *        nonzero, so only the exact match can tell abort() apart from the old exit(1) bug.
 */
static void _test_abort_probe(char const *const case_name, char const *const flag, char const *const expected_substring, bool const require_exact_abort_status) {
    char const *const argv_vector[] = { _program, flag, nullptr };
    ProcessSpec const spec = { .argv = argv_vector, .timeout_milliseconds = 5000 };
    ProcessOutcome outcome = DEFAULT_INITIALIZATION;

    Result const result = process_run(spec, &outcome);

    char label[256] = DEFAULT_INITIALIZATION;

    snprintf(label, sizeof label, "%s: process_run reports no OS error", case_name);
    _check(label, result_is_success(result));

    snprintf(label, sizeof label, "%s: child did not time out", case_name);
    _check(label, !outcome.timed_out);

    if (require_exact_abort_status) {
        snprintf(label, sizeof label, "%s: child hit abort() status, not a plain exit()", case_name);
        _check(label, process_outcome_aborted(&outcome));
    } else {
        snprintf(label, sizeof label, "%s: child aborted (nonzero exit)", case_name);
        _check(label, outcome.exit_code != 0);
    }

    snprintf(label, sizeof label, "%s: abort logged before terminating", case_name);
    _check(label, outcome.output != nullptr && strstr(outcome.output, expected_substring) != nullptr);

    process_outcome_uninit(&outcome);
}

/*==============================================================================
 * MARK: - Cases
 *============================================================================*/

static void _test_passing_checks_are_noops(void) {
    ISize sequence = 40;

    void const *const valid_pointer = &sequence;

    error_check_null(LOG_METADATA, "valid_pointer", valid_pointer);
    sequence += 1;
    _check("error_check_null(valid pointer) returned", sequence == 41);

    error_check_wrong_value(LOG_METADATA, "flag", false);
    sequence += 1;
    _check("error_check_wrong_value(false) returned", sequence == 42);

    error_check_out_of_bound_int(LOG_METADATA, "i", (ISize) 1, "max", (ISize) 10, "i >= max", false);
    sequence += 1;
    _check("error_check_out_of_bound_int(in range) returned", sequence == 43);

    error_check_out_of_bound_uint(LOG_METADATA, "i", (USize) 1, "max", (USize) 10, "i >= max", false);
    sequence += 1;
    _check("error_check_out_of_bound_uint(in range) returned", sequence == 44);

    error_check_message(LOG_METADATA, false, "unreachable message");
    sequence += 1;
    _check("error_check_message(false) returned", sequence == 45);

    error_check_non_value_int(LOG_METADATA, "n", (ISize) 7);
    sequence += 1;
    _check("error_check_non_value_int(nonzero) returned", sequence == 46);

    error_check_non_value_uint(LOG_METADATA, "n", (USize) 7);
    sequence += 1;
    _check("error_check_non_value_uint(nonzero) returned", sequence == 47);

    error_check_non_value_float(LOG_METADATA, "n", (FSize) 7.0);
    sequence += 1;
    _check("error_check_non_value_float(nonzero) returned", sequence == 48);
}

static void _test_arguments_always_evaluated(void) {
    _side_effect_counter = 0;

    error_check_wrong_value(LOG_METADATA, "guarded", _increment_and_return(false));

    _check("passing check's argument expression ran exactly once", _side_effect_counter == 1);
}

/*==============================================================================
 * MARK: - Entry point
 *============================================================================*/

int main(int argc, char **argv) {
    /* Child modes come first: this process was spawned by a running case and must behave as
     * the small probe that case asked for, never touching the counters below. */
    if (argc >= 2) {
        if (strcmp(argv[1], "--child-null") == 0) {
            return _child_null();
        }

        if (strcmp(argv[1], "--child-out-of-bound") == 0) {
            return _child_out_of_bound();
        }

        if (strcmp(argv[1], "--child-bad-format") == 0) {
            return _child_bad_format();
        }

        if (strcmp(argv[1], "--child-null-uninitialized-log") == 0) {
            return _child_null_uninitialized_log();
        }
    }

    LogConfig const log_config = {
        .level = LOG_LEVEL_ERROR,
        .stream = LOG_STREAM_STDOUT,
        .timestamp_enabled = true,
        .autoflush = true,
    };

    log_init(log_config);

    _program = argv[0];

    printf("=== error module tests ===\n");

    printf("--- passing checks are no-ops ---\n");
    _test_passing_checks_are_noops();

    printf("--- argument evaluation contract ---\n");
    _test_arguments_always_evaluated();

    printf("--- abort probes (subprocess) ---\n");
    _test_abort_probe("error_check_null(nullptr)", "--child-null", "NULL_POINTER", false);
    _test_abort_probe("error_check_out_of_bound_int(true)", "--child-out-of-bound", "OUT_OF_BOUND_INT", false);
    _test_abort_probe("error_fail_bad_format", "--child-bad-format", "BAD_FORMAT", false);
    _test_abort_probe("error_check_null(nullptr, uninitialized log)", "--child-null-uninitialized-log", "[NULL_POINTER]", true);

    printf("\n=== Results: %zd passed, %zd failed ===\n", _pass, _fail);

    return _fail == 0 ? 0 : 1;
}