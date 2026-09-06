/*
 * test_unchecked.c - Behavioral tests for include/http/client/http_client.c built WITHOUT
 * ERROR_CHECK_ENABLED.
 *
 * error_check_null in this module guards NULL-POINTER contracts (self/url/response/etc.) - true
 * undefined behaviour with the checks compiled out, and this file never exercises those. What it
 * DOES exercise is a VALUE-dependent path that never needed this build to prove: http_client_set_timeout_ms
 * and http_client_set_connect_timeout_ms take a plain USize with no error_check_* guard at all, so
 * 0 - "forward to curl's own default", per the header's own documented contract - already forwards
 * and completes normally in the CHECKED build too (test_all.c's own
 * _test_timeout_zero_no_abort_checked_build pins that). This file re-proves the same behaviour
 * with the checks compiled out, for symmetry with the rest of the suite. Also covered: max_response_size(0) means unlimited, and a
 * zero-size payload is a legal no-op, not a crash - both ordinary runtime branches on the module's
 * own fields, never error_check_* guarded, so they hold in either build.
 */
#include <stdio.h>
#include <string.h>

#include <char/char.h>
#include <http/client/http_client.h>
#include <log/log.h>
#include <test/test.h>

#include <fixture.h>

static void _url_for(U16 const port, char *const out, USize const out_capacity) {
    snprintf(out, out_capacity, "http://127.0.0.1:%u/", (unsigned) port);
}

static void _test_timeout_zero_forwards_to_curl_default(Test *const test) {
    test_case_begin(test, "timeout_ms(0) and connect_timeout_ms(0) do not abort without ERROR_CHECK_ENABLED, and forward to curl's own default");

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_OK;

    if (!test_expect_true(test, "fixture started", fixture_server_start(&server))) {
        test_case_end(test);

        return;
    }

    char url[64] = DEFAULT_INITIALIZATION;
    _url_for(fixture_server_port(&server), url, sizeof(url));

    HTTP_Client *client = http_client_new();

    http_client_set_timeout_ms(client, 0);
    http_client_set_connect_timeout_ms(client, 0);

    String response = string_init_1();
    HTTP_Client_Result result = http_client_get(client, url, &response);

    test_expect_true(test, "the request still completes normally (no abort, no hang)", http_client_result_is_ok(&result));
    test_expect_string(test, "body arrived intact", "ok-body-content", string_get_data(&response));

    http_client_result_uninit(&result);
    string_uninit(&response);
    http_client_delete(&client);
    fixture_server_join(&server);
    string_uninit(&server.request_body);

    test_case_end(test);
}

static void _test_max_response_size_zero_is_unlimited(Test *const test) {
    test_case_begin(test, "max_response_size(0) means unlimited - a VALUE opt-out, not a contract violation");

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_OVERSIZED;
    server.oversized_body_size = 50000; // larger than the module's small default fixtures use elsewhere

    if (!test_expect_true(test, "fixture started", fixture_server_start(&server))) {
        test_case_end(test);

        return;
    }

    char url[64] = DEFAULT_INITIALIZATION;
    _url_for(fixture_server_port(&server), url, sizeof(url));

    HTTP_Client *client = http_client_new();

    http_client_set_max_response_size(client, 0);

    String response = string_init_1();
    HTTP_Client_Result result = http_client_get(client, url, &response);

    test_expect_true(test, "the whole 50000-byte body arrived, uncapped", http_client_result_is_ok(&result));
    test_expect_u(test, "size matches exactly", 50000, string_get_size(&response));

    http_client_result_uninit(&result);
    string_uninit(&response);
    http_client_delete(&client);
    fixture_server_join(&server);
    string_uninit(&server.request_body);

    test_case_end(test);
}

static void _test_zero_size_payload_is_legal_no_op(Test *const test) {
    test_case_begin(test, "a zero-size PUT payload is a legal no-op VALUE, not a crash, without ERROR_CHECK_ENABLED");

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_OK;

    if (!test_expect_true(test, "fixture started", fixture_server_start(&server))) {
        test_case_end(test);

        return;
    }

    char url[64] = DEFAULT_INITIALIZATION;
    _url_for(fixture_server_port(&server), url, sizeof(url));

    HTTP_Client *client = http_client_new();
    String response = string_init_1();
    HTTP_Client_Result result = http_client_put_2(client, url, "", 0, &response);

    test_expect_true(test, "zero-size PUT still succeeds", http_client_result_is_ok(&result));

    fixture_server_join(&server);

    test_expect_string(test, "method line was still PUT", "PUT", server.request_method);
    test_expect_u(test, "zero-size payload arrived as zero bytes", 0, string_get_size(&server.request_body));

    http_client_result_uninit(&result);
    string_uninit(&response);
    http_client_delete(&client);
    string_uninit(&server.request_body);

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

    Test test = test_init("tests/http/client/test_unchecked.c");

    test_suite_begin(&test, "http/client (ERROR_CHECK_ENABLED off)");

    _test_timeout_zero_forwards_to_curl_default(&test);
    _test_max_response_size_zero_is_unlimited(&test);
    _test_zero_size_payload_is_legal_no_op(&test);

    test_suite_end(&test);

    I32 const failures = test_uninit(&test);

    http_client_global_uninit();

    return failures;
}