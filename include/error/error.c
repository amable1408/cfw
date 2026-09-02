/*
 * error.c - Implementation of centralized error checking utilities for the C Libraries Framework
 *
 * Each check is a no-op unless ERROR_CHECK_ENABLED is defined. On failure it prints the
 * current trace stack, logs the cause at LOG_LEVEL_ERROR through the log module, and aborts
 * the process with abort(). The shared _error_fail helper is the single place that decides
 * how a failure is reported and terminated.
 *
 * See error.h for API documentation and usage examples.
 */

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/
#include <error/error.h>

/*==============================================================================
 * MARK: - Macros and Constants
 *============================================================================*/
#define _ERROR_MESSAGE_BUFFER_SIZE 512

/*==============================================================================
 * MARK: - Static/Internal Functions
 *============================================================================*/
#ifdef ERROR_CHECK_ENABLED
CFW_ATTR_PRINTF(4, 5)
static void _error_fail(char const *const file, I32 const line, char const *const function, char const *const fmt, ...) {
    char buffer[_ERROR_MESSAGE_BUFFER_SIZE] = DEFAULT_INITIALIZATION;

    va_list args = DEFAULT_INITIALIZATION;

    va_start(args, fmt);
    vsnprintf(buffer, sizeof buffer, fmt, args);
    va_end(args);

    trace_log_print();

    // log_message_2 exit(1)s when log_init was never called - which reported
    // the defect as a plain exit code and skipped the abort (no core dump, no
    // debugger break). Guarantee the message and the abort either way.
    if (log_is_initialized()) {
        log_message_2(LOG_LEVEL_ERROR, file, line, function, "%s", buffer);

        // abort() does not flush stdio, and the fatal line is the one write
        // that must survive - flush regardless of the autoflush setting.
        // (The stderr fallback below is unbuffered and needs no flush.)
        FILE *const stream = log_get_stream();

        if (stream != nullptr) {
            fflush(stream);
        }
    }
    else {
        fprintf(stderr, "%s:%d < %s < %s", file != nullptr ? file : "(null)", line, function != nullptr ? function : "(null)", buffer);
    }

    abort();
}
#endif // ERROR_CHECK_ENABLED

/*==============================================================================
 * MARK: - Public API
 *============================================================================*/
void error_check_message(char const *const file, I32 const line, char const *const function, bool const cond, char const *const value) {
#ifdef ERROR_CHECK_ENABLED
    if (cond) {
        _error_fail(file, line, function, "%s\n", value != nullptr ? value : "(null)");
    }
#endif // ERROR_CHECK_ENABLED
}

void error_check_non_value_float(char const *const file, I32 const line, char const *const function, char const *const variable, FSize const value) {
#ifdef ERROR_CHECK_ENABLED
    if (value == 0.0) {
        _error_fail(file, line, function, "[NON_VALUE] > %s > %.1f\n", variable != nullptr ? variable : "(null)", value);
    }
#endif // ERROR_CHECK_ENABLED
}

void error_check_non_value_int(char const *const file, I32 const line, char const *const function, char const *const variable, ISize const value) {
#ifdef ERROR_CHECK_ENABLED
    if (value == 0) {
        _error_fail(file, line, function, "[NON_VALUE] > %s > %lld\n", variable != nullptr ? variable : "(null)", (long long) value);
    }
#endif // ERROR_CHECK_ENABLED
}

void error_check_non_value_uint(char const *const file, I32 const line, char const *const function, char const *const variable, USize const value) {
#ifdef ERROR_CHECK_ENABLED
    if (value == 0) {
        _error_fail(file, line, function, "[NON_VALUE] > %s > %llu\n", variable != nullptr ? variable : "(null)", (unsigned long long) value);
    }
#endif // ERROR_CHECK_ENABLED
}

void error_check_null(char const *const file, I32 const line, char const *const function, char const *const variable, void const *const value) {
#ifdef ERROR_CHECK_ENABLED
    if (value == nullptr) {
        _error_fail(file, line, function, "[NULL_POINTER] > %s > %p\n", variable != nullptr ? variable : "(null)", (void*) value);
    }
#endif // ERROR_CHECK_ENABLED
}

void error_check_out_of_bound_int(
    char const *const file, I32 const line, char const *const function,
    char const *const var_name_0, ISize const var_value_0, char const *const var_name_1, ISize const var_value_1,
    char const *const cond, bool const value) {
#ifdef ERROR_CHECK_ENABLED
    if (value) {
        _error_fail(file, line, function, "[OUT_OF_BOUND_INT] > [%s == %lld] && [%s == %lld] > [%s == true]\n",
            var_name_0 != nullptr ? var_name_0 : "(null)", (long long) var_value_0,
            var_name_1 != nullptr ? var_name_1 : "(null)", (long long) var_value_1,
            cond != nullptr ? cond : "(null)");
    }
#endif // ERROR_CHECK_ENABLED
}

void error_check_out_of_bound_uint(
    char const *const file, I32 const line, char const *const function,
    char const *const var_name_0, USize const var_value_0, char const *const var_name_1, USize const var_value_1,
    char const *const cond, bool const value) {
#ifdef ERROR_CHECK_ENABLED
    if (value) {
        _error_fail(file, line, function, "[OUT_OF_BOUND_UINT] > [%s == %llu] && [%s == %llu] > [%s == true]\n",
            var_name_0 != nullptr ? var_name_0 : "(null)", (unsigned long long) var_value_0,
            var_name_1 != nullptr ? var_name_1 : "(null)", (unsigned long long) var_value_1,
            cond != nullptr ? cond : "(null)");
    }
#endif // ERROR_CHECK_ENABLED
}

void error_check_wrong_value(char const *const file, I32 const line, char const *const function, char const *const variable, bool const value) {
#ifdef ERROR_CHECK_ENABLED
    if (value) {
        _error_fail(file, line, function, "[WRONG_VALUE] > %s\n", variable != nullptr ? variable : "(null)");
    }
#endif // ERROR_CHECK_ENABLED
}

void error_fail_bad_format(char const *const file, I32 const line, char const *const function, char const *const variable, char const *const value) {
#ifdef ERROR_CHECK_ENABLED
    _error_fail(file, line, function, "BAD_FORMAT: %s:%s\n", variable != nullptr ? variable : "(null)", value != nullptr ? value : "(null)");
#else
    // error_fail_* is a deliberate fatal, not a check: unlike the check
    // family, it must abort in EVERY build - only the pretty reporting is
    // gated. Falling through into the code the abort was guarding is what the
    // old all-gated body silently did.
    fprintf(stderr, "%s:%d < %s < BAD_FORMAT: %s:%s\n",
        file != nullptr ? file : "(null)", line, function != nullptr ? function : "(null)",
        variable != nullptr ? variable : "(null)", value != nullptr ? value : "(null)");
    abort();
#endif // ERROR_CHECK_ENABLED
}