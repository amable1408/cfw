/*
 * test_unchecked.c - the http/service/traceparent suite, built WITHOUT ERROR_CHECK_ENABLED.
 *
 * Everything here is a VALUE-dependent refusal over bytes that arrive from the network or
 * from configuration: a null or malformed traceparent, a null or oversize tracestate, a
 * header name that is not a token. Each has to hold when every error_check_* call compiles
 * to nothing, because none of them is a caller bug.
 *
 * Contract violations (a null self, a null out) are NOT exercised here: with the checks off
 * they are undefined behaviour by design, not a refusal.
 */
#include <stdio.h>

#include <http/service/traceparent/traceparent.h>
#include <log/log.h>
#include <test/test.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/
#define _GOLDEN "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01"

/*==============================================================================
 * MARK: - Cases
 *============================================================================*/
static void _test_null_and_malformed_values(Test *const test) {
    test_case_begin(test, "unchecked: a null or malformed value is refused, never an abort");

    HTTP_Service_Traceparent         trace      = DEFAULT_INITIALIZATION;
    HTTP_Service_Traceparent_Context context    = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_1 succeeds", http_service_traceparent_init_1(&trace));

    test_expect_false(test, "a null value is refused", http_service_traceparent_valid_1(nullptr));
    test_expect_false(test, "an empty value is refused", http_service_traceparent_valid_1(""));
    test_expect_false(test, "a null value does not parse", http_service_traceparent_context_parse_1(&trace, nullptr, &context));
    test_expect_string(test, "and leaves the context zeroed", "", context.trace_id);

    test_expect_true(test, "a null value still mints a child", http_service_traceparent_child_create_1(&trace, nullptr, &context));
    test_expect_true(test, "which is a valid root context", http_service_traceparent_context_valid(&context));

    http_service_traceparent_uninit(&trace);

    test_case_end(test);
}

static void _test_tracestate_caps(Test *const test) {
    test_case_begin(test, "unchecked: a null or oversize tracestate is dropped, not forwarded");

    HTTP_Service_Traceparent         trace      = DEFAULT_INITIALIZATION;
    HTTP_Service_Traceparent_Context context    = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_1 succeeds", http_service_traceparent_init_1(&trace));

    test_expect_true(test, "a null tracestate parses", http_service_traceparent_context_parse_2(&trace, _GOLDEN, nullptr, &context));
    test_expect_string(test, "carrying nothing", "", context.tracestate);

    char oversize[HTTP_SERVICE_TRACEPARENT_TRACESTATE_CAPACITY + 64] = DEFAULT_INITIALIZATION;

    for (USize index = 0; index < HTTP_SERVICE_TRACEPARENT_TRACESTATE_MAX_SIZE + 1; index += 1) {
        oversize[index] = 'a';
    }

    test_expect_true(test, "an oversize tracestate still parses the traceparent", http_service_traceparent_context_parse_2(&trace, _GOLDEN, oversize, &context));
    test_expect_string(test, "and is dropped", "", context.tracestate);

    http_service_traceparent_uninit(&trace);

    test_case_end(test);
}

static void _test_header_name_refused(Test *const test) {
    test_case_begin(test, "unchecked: a header name that is not a token is refused");

    HTTP_Service_Traceparent trace = DEFAULT_INITIALIZATION;

    test_expect_false(test, "an empty name is refused", http_service_traceparent_init_2(&trace, "", 1));
    test_expect_false(test, "a CRLF name is refused", http_service_traceparent_init_2(&trace, "trace\r\nSet-Cookie: x", 1));
    test_expect_string(test, "and the service is left zeroed", "", trace.header_name);
    test_expect_true(test, "a token name is accepted", http_service_traceparent_init_2(&trace, "x-trace", 1));

    http_service_traceparent_uninit(&trace);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Entry point
 *============================================================================*/
I32 main(void) {
    LogConfig const log_config = { .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true };

    log_init(log_config);

    Test test = test_init("http_service_traceparent_unchecked");

    test_suite_begin(&test, "traceparent unchecked");

    _test_null_and_malformed_values(&test);
    _test_tracestate_caps(&test);
    _test_header_name_refused(&test);

    test_suite_end(&test);

    return test_uninit(&test);
}