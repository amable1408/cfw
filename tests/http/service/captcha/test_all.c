/*
 * test_all.c - Behavioral tests for include/http/service/captcha/captcha.c.
 *
 * captcha shipped public with no suite of its own. Every case here runs against the SAME
 * loopback fixture tests/http/client uses (tests/http/client/fixture.c, linked in), scripted with
 * a siteverify body, so a real provider is never contacted and the wire bytes this service sends
 * are under the test's own control.
 *
 * What the fixture makes pinnable that nothing else could:
 *   - the exact request body, byte for byte, including field ORDER and the absence of a field;
 *   - that a disabled / misconfigured / refused verification opens NO connection at all
 *     (connection_count == 0 after a join), which is the only honest way to prove a refusal
 *     happened BEFORE the request rather than after it.
 *
 * Encoding note: the payload is built with http_query_form_add_1, so a space encodes as "+" -
 * the application/x-www-form-urlencoded convention. The design report's own example spelled the
 * same token with "%20" because it was written against the older hand-rolled curl encoder this
 * round deleted; both decode to a space and every provider accepts either.
 */
#include <stdio.h>
#include <string.h>

#include <chrono/chrono.h>
#include <http/service/captcha/captcha.h>
#include <log/log.h>
#include <test/test.h>
#include <tracelog/tracelog.h>

#include <fixture.h>

#define _URL_SIZE 128

static void _url_for(U16 const port, char *const out, USize const out_capacity) {
    snprintf(out, out_capacity, "http://127.0.0.1:%u/siteverify", (unsigned) port);
}

/* A service pointed at the fixture. verify_tls goes off FIRST, because set_url refuses a
 * non-https endpoint while TLS verification is on - which is the point of report item High 5. */
static bool _service_for(HTTP_Service_Captcha *const self, U16 const port) {
    if (!http_service_captcha_init_2(self, HTTP_SERVICE_CAPTCHA_PROVIDER_TURNSTILE, "s")) {
        return false;
    }

    char url[_URL_SIZE] = {0};

    _url_for(port, url, sizeof(url));

    http_service_captcha_set_verify_tls(self, false);
    http_service_captcha_set_timeouts(self, 2000, 4000);

    return http_service_captcha_set_url(self, url);
}

/*==============================================================================
 * MARK: - Request shape
 *============================================================================*/

static void _test_request_body_is_byte_exact(Test *const test) {
    test_case_begin(test, "the siteverify request is a POST of secret=s&response=tok&remoteip=1.2.3.4 with the form content type");

    Fixture_Server server = DEFAULT_INITIALIZATION;

    server.script                 = FIXTURE_SCRIPT_OK;
    server.response_body          = "{\"success\":true,\"hostname\":\"h\"}";
    server.response_content_type  = "application/json";

    if (!test_expect_true(test, "fixture started", fixture_server_start(&server))) {
        test_case_end(test);

        return;
    }

    HTTP_Service_Captcha captcha = DEFAULT_INITIALIZATION;

    test_expect_true(test, "service configured", _service_for(&captcha, fixture_server_port(&server)));

    HTTP_Service_Captcha_Result result = http_service_captcha_verify(&captcha, "tok", "1.2.3.4");

    fixture_server_join(&server);

    test_expect_true(test, "verification succeeded", result.success);
    test_expect_u(test, "status is OK", HTTP_SERVICE_CAPTCHA_STATUS_OK, result.status);
    test_expect_true(test, "provider_success is the raw provider value", result.provider_success);
    test_expect_string(test, "hostname parsed", "h", string_get_data(&result.hostname));
    test_expect_u(test, "one connection", 1, server.connection_count);
    test_expect_string(test, "method is POST", "POST", server.request_method);
    test_expect_string(test, "path is the configured endpoint", "/siteverify", server.request_path);
    test_expect_string_contains(test, "form content type sent", server.request_headers, "Content-Type: application/x-www-form-urlencoded");
    test_expect_string(test, "body is byte exact, in field order", "secret=s&response=tok&remoteip=1.2.3.4", string_get_data(&server.request_body));

    http_service_captcha_result_uninit(&result);
    http_service_captcha_uninit(&captcha);
    string_uninit(&server.request_body);

    test_case_end(test);
}

static void _test_request_body_percent_encodes(Test *const test) {
    test_case_begin(test, "a token with reserved bytes and UTF-8 is percent-encoded, not sent raw");

    Fixture_Server server = DEFAULT_INITIALIZATION;

    server.script         = FIXTURE_SCRIPT_OK;
    server.response_body  = "{\"success\":true}";

    if (!test_expect_true(test, "fixture started", fixture_server_start(&server))) {
        test_case_end(test);

        return;
    }

    HTTP_Service_Captcha captcha = DEFAULT_INITIALIZATION;

    test_expect_true(test, "service configured", _service_for(&captcha, fixture_server_port(&server)));

    HTTP_Service_Captcha_Result result = http_service_captcha_verify(&captcha, "a b&c=d/\xC3\xA9", "");

    fixture_server_join(&server);

    /* "&", "=" and "/" MUST be escaped or the token would forge extra form fields; the space is
     * "+" per the form convention, and the two UTF-8 bytes of "e-acute" become %C3%A9. */
    test_expect_string(test, "token escaped and no remoteip field at all", "secret=s&response=a+b%26c%3Dd%2F%C3%A9", string_get_data(&server.request_body));

    http_service_captcha_result_uninit(&result);
    http_service_captcha_uninit(&captcha);
    string_uninit(&server.request_body);

    test_case_end(test);
}

static void _test_null_remote_ip_sends_no_field(Test *const test) {
    test_case_begin(test, "a null remote_ip sends no remoteip field at all, exactly as an empty one does");

    Fixture_Server server = DEFAULT_INITIALIZATION;

    server.script         = FIXTURE_SCRIPT_OK;
    server.response_body  = "{\"success\":true}";

    if (!test_expect_true(test, "fixture started", fixture_server_start(&server))) {
        test_case_end(test);

        return;
    }

    HTTP_Service_Captcha captcha = DEFAULT_INITIALIZATION;

    test_expect_true(test, "service configured", _service_for(&captcha, fixture_server_port(&server)));

    /* Documented in the header, pinned nowhere until now: the address is optional request data,
     * so a null must be a missing field rather than an error_check abort. */
    HTTP_Service_Captcha_Result result = http_service_captcha_verify(&captcha, "tok", nullptr);

    fixture_server_join(&server);

    test_expect_true(test, "verification succeeded", result.success);
    test_expect_string(test, "no remoteip field on the wire", "secret=s&response=tok", string_get_data(&server.request_body));

    http_service_captcha_result_uninit(&result);
    http_service_captcha_uninit(&captcha);
    string_uninit(&server.request_body);

    test_case_end(test);
}

static void _test_cached_client_does_not_duplicate_headers(Test *const test) {
    test_case_begin(test, "two verifications on one service reuse the client handle without stacking a second Content-Type");

    Fixture_Server server = DEFAULT_INITIALIZATION;

    server.script           = FIXTURE_SCRIPT_OK;
    server.response_body    = "{\"success\":true}";
    server.max_connections  = 2;

    if (!test_expect_true(test, "fixture started", fixture_server_start(&server))) {
        test_case_end(test);

        return;
    }

    HTTP_Service_Captcha captcha = DEFAULT_INITIALIZATION;

    test_expect_true(test, "service configured", _service_for(&captcha, fixture_server_port(&server)));

    HTTP_Service_Captcha_Result first = http_service_captcha_verify(&captcha, "tok", "");

    test_expect_true(test, "first verification succeeded", first.success);

    /* http_client_new never answers null, so verify has no null branch after it: the handle is
     * simply created and cached here, and the second call below reuses this very one. */
    test_expect_not_null(test, "the first verification cached a client handle", (void*) captcha.client);

    HTTP_Service_Captcha_Result second = http_service_captcha_verify(&captcha, "tok", "");

    fixture_server_join(&server);

    test_expect_true(test, "second verification succeeded", second.success);
    test_expect_u(test, "two connections served", 2, server.connection_count);

    /* The handle is reused, so its request header list is cleared before each call; without that
     * clear the second request would carry Content-Type twice. */
    char const *const first_type  = strstr(server.request_headers, "Content-Type:");
    char const *const second_type = first_type == nullptr ? nullptr : strstr(first_type + 1, "Content-Type:");

    test_expect_not_null(test, "second request carries a Content-Type", (void*) first_type);
    test_expect_null(test, "second request carries exactly one Content-Type", (void*) second_type);

    http_service_captcha_result_uninit(&second);
    http_service_captcha_result_uninit(&first);
    http_service_captcha_uninit(&captcha);
    string_uninit(&server.request_body);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Provider answers
 *============================================================================*/

static void _test_rejection_populates_error_codes(Test *const test) {
    test_case_begin(test, "success:false with error-codes fails closed as TOKEN_REJECTED and surfaces the provider's codes");

    Fixture_Server server = DEFAULT_INITIALIZATION;

    server.script         = FIXTURE_SCRIPT_OK;
    server.response_body  = "{\"success\":false,\"error-codes\":[\"invalid-input-response\",\"timeout-or-duplicate\"]}";

    if (!test_expect_true(test, "fixture started", fixture_server_start(&server))) {
        test_case_end(test);

        return;
    }

    HTTP_Service_Captcha captcha = DEFAULT_INITIALIZATION;

    test_expect_true(test, "service configured", _service_for(&captcha, fixture_server_port(&server)));

    HTTP_Service_Captcha_Result result = http_service_captcha_verify(&captcha, "tok", "");

    fixture_server_join(&server);

    test_expect_false(test, "verification failed", result.success);
    test_expect_u(test, "status is TOKEN_REJECTED", HTTP_SERVICE_CAPTCHA_STATUS_TOKEN_REJECTED, result.status);
    test_expect_string(test, "error codes joined", "invalid-input-response,timeout-or-duplicate", string_get_data(&result.error_codes));
    test_expect_u(test, "client status is OK - the transport worked", HTTP_CLIENT_STATUS_OK, result.client_status);

    http_service_captcha_result_uninit(&result);
    http_service_captcha_uninit(&captcha);
    string_uninit(&server.request_body);

    test_case_end(test);
}

/* One malformed-body case; the caller supplies the body and a label. */
static void _expect_malformed(Test *const test, char const *const label, char const *const body) {
    Fixture_Server server = DEFAULT_INITIALIZATION;

    server.script         = FIXTURE_SCRIPT_OK;
    server.response_body  = body;

    if (!test_expect_true(test, "fixture started", fixture_server_start(&server))) {
        return;
    }

    HTTP_Service_Captcha captcha = DEFAULT_INITIALIZATION;

    _service_for(&captcha, fixture_server_port(&server));

    HTTP_Service_Captcha_Result result = http_service_captcha_verify(&captcha, "tok", "");

    fixture_server_join(&server);

    test_expect_false(test, label, result.success);
    test_expect_u(test, "status is MALFORMED_RESPONSE", HTTP_SERVICE_CAPTCHA_STATUS_MALFORMED_RESPONSE, result.status);
    test_expect_false(test, "provider_success left false", result.provider_success);

    http_service_captcha_result_uninit(&result);
    http_service_captcha_uninit(&captcha);
    string_uninit(&server.request_body);
}

static void _test_malformed_answers_fail_closed(Test *const test) {
    test_case_begin(test, "a truncated, non-object, or empty 200 body fails closed instead of aborting");

    /* json_from_4 answers nullptr for "{"; json_at_1 on an array or a bare string root answers
     * nullptr for ['success']; an empty body never reaches the parser. None may abort: the body
     * comes off the network. */
    _expect_malformed(test, "truncated object rejected", "{");
    _expect_malformed(test, "array root rejected", "[]");
    _expect_malformed(test, "string root rejected", "\"x\"");
    _expect_malformed(test, "empty body rejected", "");
    _expect_malformed(test, "object without success rejected", "{\"hostname\":\"h\"}");
    _expect_malformed(test, "non-boolean success rejected", "{\"success\":\"true\"}");

    test_case_end(test);
}

static void _test_non_2xx_never_passes(Test *const test) {
    test_case_begin(test, "a 500 carrying success:true is PROVIDER_ERROR, never a pass");

    Fixture_Server server = DEFAULT_INITIALIZATION;

    server.script         = FIXTURE_SCRIPT_STATUS;
    server.status_code    = 500;
    server.response_body  = "{\"success\":true}";

    if (!test_expect_true(test, "fixture started", fixture_server_start(&server))) {
        test_case_end(test);

        return;
    }

    HTTP_Service_Captcha captcha = DEFAULT_INITIALIZATION;

    test_expect_true(test, "service configured", _service_for(&captcha, fixture_server_port(&server)));

    HTTP_Service_Captcha_Result result = http_service_captcha_verify(&captcha, "tok", "");

    fixture_server_join(&server);

    test_expect_false(test, "verification failed", result.success);
    test_expect_u(test, "status is PROVIDER_ERROR", HTTP_SERVICE_CAPTCHA_STATUS_PROVIDER_ERROR, result.status);
    test_expect_u(test, "response code carried", 500, result.response_code);
    test_expect_false(test, "body never parsed on a non-2xx", result.provider_success);

    http_service_captcha_result_uninit(&result);
    http_service_captcha_uninit(&captcha);
    string_uninit(&server.request_body);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Expectations
 *============================================================================*/

static void _test_hostname_expectation(Test *const test) {
    test_case_begin(test, "a success:true token minted for another hostname is rejected once an expected hostname is set");

    Fixture_Server server = DEFAULT_INITIALIZATION;

    server.script         = FIXTURE_SCRIPT_OK;
    server.response_body  = "{\"success\":true,\"hostname\":\"attacker.example\"}";

    if (!test_expect_true(test, "fixture started", fixture_server_start(&server))) {
        test_case_end(test);

        return;
    }

    HTTP_Service_Captcha captcha = DEFAULT_INITIALIZATION;

    test_expect_true(test, "service configured", _service_for(&captcha, fixture_server_port(&server)));
    test_expect_true(test, "the expectation reports that it was stored", http_service_captcha_set_expected_hostname(&captcha, "traymon.example"));

    HTTP_Service_Captcha_Result result = http_service_captcha_verify(&captcha, "tok", "");

    fixture_server_join(&server);

    test_expect_false(test, "verification failed despite success:true", result.success);
    test_expect_u(test, "status is TOKEN_REJECTED", HTTP_SERVICE_CAPTCHA_STATUS_TOKEN_REJECTED, result.status);
    test_expect_true(test, "provider_success still reports the raw value", result.provider_success);
    test_expect_string_contains(test, "the reason is visible", string_get_data(&result.error), "hostname");

    http_service_captcha_result_uninit(&result);
    http_service_captcha_uninit(&captcha);
    string_uninit(&server.request_body);

    test_case_end(test);
}

static void _test_hostname_expectation_is_case_insensitive(Test *const test) {
    test_case_begin(test, "a DNS name differing only in case satisfies the hostname expectation, while the action stays byte-exact");

    Fixture_Server server = DEFAULT_INITIALIZATION;

    server.script         = FIXTURE_SCRIPT_OK;
    server.response_body  = "{\"success\":true,\"hostname\":\"Traymon.Example\",\"action\":\"Register\"}";

    if (!test_expect_true(test, "fixture started", fixture_server_start(&server))) {
        test_case_end(test);

        return;
    }

    HTTP_Service_Captcha captcha = DEFAULT_INITIALIZATION;

    test_expect_true(test, "service configured", _service_for(&captcha, fixture_server_port(&server)));
    test_expect_true(test, "hostname expectation stored", http_service_captcha_set_expected_hostname(&captcha, "traymon.example"));

    HTTP_Service_Captcha_Result result = http_service_captcha_verify(&captcha, "tok", "");

    fixture_server_join(&server);

    /* Rejecting this token would blame the user for the provider's choice of capitalization. */
    test_expect_true(test, "a case-different hostname still matches", result.success);
    test_expect_u(test, "status is OK", HTTP_SERVICE_CAPTCHA_STATUS_OK, result.status);

    http_service_captcha_result_uninit(&result);
    http_service_captcha_uninit(&captcha);
    string_uninit(&server.request_body);

    test_case_end(test);
}

static void _test_action_expectation_is_byte_exact(Test *const test) {
    test_case_begin(test, "an action differing only in case is rejected: an action is an application label, not a DNS name");

    Fixture_Server server = DEFAULT_INITIALIZATION;

    server.script         = FIXTURE_SCRIPT_OK;
    server.response_body  = "{\"success\":true,\"action\":\"Register\"}";

    if (!test_expect_true(test, "fixture started", fixture_server_start(&server))) {
        test_case_end(test);

        return;
    }

    HTTP_Service_Captcha captcha = DEFAULT_INITIALIZATION;

    test_expect_true(test, "service configured", _service_for(&captcha, fixture_server_port(&server)));
    test_expect_true(test, "action expectation stored", http_service_captcha_set_expected_action(&captcha, "register"));

    HTTP_Service_Captcha_Result result = http_service_captcha_verify(&captcha, "tok", "");

    fixture_server_join(&server);

    test_expect_false(test, "a case-different action does not match", result.success);
    test_expect_string_contains(test, "the reason names the action", string_get_data(&result.error), "action");

    http_service_captcha_result_uninit(&result);
    http_service_captcha_uninit(&captcha);
    string_uninit(&server.request_body);

    test_case_end(test);
}

static void _test_action_expectation(Test *const test) {
    test_case_begin(test, "a token minted for another action is rejected once an expected action is set");

    Fixture_Server server = DEFAULT_INITIALIZATION;

    server.script         = FIXTURE_SCRIPT_OK;
    server.response_body  = "{\"success\":true,\"action\":\"login\"}";

    if (!test_expect_true(test, "fixture started", fixture_server_start(&server))) {
        test_case_end(test);

        return;
    }

    HTTP_Service_Captcha captcha = DEFAULT_INITIALIZATION;

    test_expect_true(test, "service configured", _service_for(&captcha, fixture_server_port(&server)));
    http_service_captcha_set_expected_action(&captcha, "register");

    HTTP_Service_Captcha_Result result = http_service_captcha_verify(&captcha, "tok", "");

    fixture_server_join(&server);

    test_expect_false(test, "verification failed", result.success);
    test_expect_string(test, "action parsed", "login", string_get_data(&result.action));
    test_expect_string_contains(test, "the reason is visible", string_get_data(&result.error), "action");

    http_service_captcha_result_uninit(&result);
    http_service_captcha_uninit(&captcha);
    string_uninit(&server.request_body);

    test_case_end(test);
}

static void _test_score_expectation(Test *const test) {
    test_case_begin(test, "a reCAPTCHA v3 bot score below the configured minimum is rejected; an integer score still reads");

    Fixture_Server server = DEFAULT_INITIALIZATION;

    server.script           = FIXTURE_SCRIPT_OK;
    server.response_body    = "{\"success\":true,\"score\":0.1}";
    server.max_connections  = 3;

    if (!test_expect_true(test, "fixture started", fixture_server_start(&server))) {
        test_case_end(test);

        return;
    }

    HTTP_Service_Captcha captcha = DEFAULT_INITIALIZATION;

    test_expect_true(test, "service configured", _service_for(&captcha, fixture_server_port(&server)));
    http_service_captcha_set_min_score(&captcha, 0.5);

    HTTP_Service_Captcha_Result low = http_service_captcha_verify(&captcha, "tok", "");

    test_expect_false(test, "a 0.1 score fails a 0.5 minimum", low.success);
    test_expect_f(test, "score parsed", 0.1, low.score, 0.0001);
    test_expect_string_contains(test, "the reason is visible", string_get_data(&low.error), "score");

    fixture_server_join(&server);
    string_uninit(&server.request_body);

    /* A whole score serializes as an INTEGER node, which the float getter reads as 0.0 - it must
     * not be mistaken for "no score" and fail the same minimum. */
    Fixture_Server whole = DEFAULT_INITIALIZATION;

    whole.script         = FIXTURE_SCRIPT_OK;
    whole.response_body  = "{\"success\":true,\"score\":1}";

    if (test_expect_true(test, "second fixture started", fixture_server_start(&whole))) {
        HTTP_Service_Captcha other = DEFAULT_INITIALIZATION;

        _service_for(&other, fixture_server_port(&whole));
        http_service_captcha_set_min_score(&other, 0.5);

        HTTP_Service_Captcha_Result high = http_service_captcha_verify(&other, "tok", "");

        fixture_server_join(&whole);

        test_expect_true(test, "an integer score of 1 passes a 0.5 minimum", high.success);
        test_expect_f(test, "integer score parsed", 1.0, high.score, 0.0001);

        http_service_captcha_result_uninit(&high);
        http_service_captcha_uninit(&other);
        string_uninit(&whole.request_body);
    }

    http_service_captcha_result_uninit(&low);
    http_service_captcha_uninit(&captcha);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Transport failures
 *============================================================================*/

static void _test_stall_is_timeout(Test *const test) {
    test_case_begin(test, "a provider that never answers is TIMEOUT inside the configured budget, not a hang");

    Fixture_Server server = DEFAULT_INITIALIZATION;

    server.script = FIXTURE_SCRIPT_STALL;

    if (!test_expect_true(test, "fixture started", fixture_server_start(&server))) {
        test_case_end(test);

        return;
    }

    HTTP_Service_Captcha captcha = DEFAULT_INITIALIZATION;

    test_expect_true(test, "service configured", _service_for(&captcha, fixture_server_port(&server)));
    http_service_captcha_set_timeouts(&captcha, 200, 500);

    ChronoInstant const start = chrono_now();

    HTTP_Service_Captcha_Result result = http_service_captcha_verify(&captcha, "tok", "");

    U64 const elapsed_ms = chrono_duration_milliseconds(chrono_elapsed(start));

    fixture_server_join(&server);

    test_expect_false(test, "verification failed", result.success);
    test_expect_u(test, "status is TIMEOUT", HTTP_SERVICE_CAPTCHA_STATUS_TIMEOUT, result.status);
    test_expect_u(test, "client status is TIMEOUT too", HTTP_CLIENT_STATUS_TIMEOUT, result.client_status);
    test_expect_true(test, "gave up well inside 2 s", elapsed_ms < 2000);

    http_service_captcha_result_uninit(&result);
    http_service_captcha_uninit(&captcha);
    string_uninit(&server.request_body);

    test_case_end(test);
}

/* The cached handle's state after a FAILED transfer is what a deployment meets first: an outage,
 * then recovery. Both halves below drive one service - therefore one curl handle - across the
 * failure and out the other side, which nothing pinned while the reuse cases only chained two
 * successes. `verify` re-applies every option per call, so a second fixture on a second port is
 * reached with set_url; the handle is the same one. */
static void _test_recovers_after_a_timed_out_transfer(Test *const test) {
    test_case_begin(test, "a service that timed out verifies successfully on the next call, on the same cached handle");

    Fixture_Server stalling = DEFAULT_INITIALIZATION;

    stalling.script = FIXTURE_SCRIPT_STALL;

    if (!test_expect_true(test, "stalling fixture started", fixture_server_start(&stalling))) {
        test_case_end(test);

        return;
    }

    HTTP_Service_Captcha captcha = DEFAULT_INITIALIZATION;

    test_expect_true(test, "service configured", _service_for(&captcha, fixture_server_port(&stalling)));
    http_service_captcha_set_timeouts(&captcha, 200, 500);

    HTTP_Service_Captcha_Result timed_out = http_service_captcha_verify(&captcha, "tok", "");

    fixture_server_join(&stalling);

    test_expect_false(test, "the first verification failed", timed_out.success);
    test_expect_u(test, "status is TIMEOUT", HTTP_SERVICE_CAPTCHA_STATUS_TIMEOUT, timed_out.status);
    test_expect_not_null(test, "the handle is cached even after a failure", (void*) captcha.client);

    Fixture_Server answering = DEFAULT_INITIALIZATION;

    answering.script         = FIXTURE_SCRIPT_OK;
    answering.response_body  = "{\"success\":true,\"hostname\":\"h\"}";

    if (!test_expect_true(test, "answering fixture started", fixture_server_start(&answering))) {
        http_service_captcha_result_uninit(&timed_out);
        http_service_captcha_uninit(&captcha);
        string_uninit(&stalling.request_body);

        test_case_end(test);

        return;
    }

    char url[_URL_SIZE] = {0};

    _url_for(fixture_server_port(&answering), url, sizeof(url));

    http_service_captcha_set_timeouts(&captcha, 2000, 4000);
    test_expect_true(test, "re-pointed at the answering fixture", http_service_captcha_set_url(&captcha, url));

    HTTP_Service_Captcha_Result recovered = http_service_captcha_verify(&captcha, "tok", "");

    fixture_server_join(&answering);

    /* curl's last error, the capped-body flag and any partial response all live on the reused
     * handle; none of them may leak into the recovery call. */
    test_expect_true(test, "the recovery verification succeeded", recovered.success);
    test_expect_u(test, "status is OK", HTTP_SERVICE_CAPTCHA_STATUS_OK, recovered.status);
    test_expect_string(test, "no stale response text survived the failure", "h", string_get_data(&recovered.hostname));
    test_expect_string(test, "the request body is intact", "secret=s&response=tok", string_get_data(&answering.request_body));

    http_service_captcha_result_uninit(&recovered);
    http_service_captcha_result_uninit(&timed_out);
    http_service_captcha_uninit(&captcha);
    string_uninit(&answering.request_body);
    string_uninit(&stalling.request_body);

    test_case_end(test);
}

static void _test_recovers_after_a_provider_error(Test *const test) {
    test_case_begin(test, "a service that got a 500 verifies successfully on the next call, on the same cached handle");

    Fixture_Server failing = DEFAULT_INITIALIZATION;

    failing.script         = FIXTURE_SCRIPT_STATUS;
    failing.status_code    = 500;
    failing.response_body  = "{\"success\":true}";

    if (!test_expect_true(test, "failing fixture started", fixture_server_start(&failing))) {
        test_case_end(test);

        return;
    }

    HTTP_Service_Captcha captcha = DEFAULT_INITIALIZATION;

    test_expect_true(test, "service configured", _service_for(&captcha, fixture_server_port(&failing)));

    HTTP_Service_Captcha_Result errored = http_service_captcha_verify(&captcha, "tok", "");

    fixture_server_join(&failing);

    test_expect_false(test, "the first verification failed", errored.success);
    test_expect_u(test, "status is PROVIDER_ERROR", HTTP_SERVICE_CAPTCHA_STATUS_PROVIDER_ERROR, errored.status);

    Fixture_Server answering = DEFAULT_INITIALIZATION;

    answering.script         = FIXTURE_SCRIPT_OK;
    answering.response_body  = "{\"success\":true,\"hostname\":\"h\"}";

    if (!test_expect_true(test, "answering fixture started", fixture_server_start(&answering))) {
        http_service_captcha_result_uninit(&errored);
        http_service_captcha_uninit(&captcha);
        string_uninit(&failing.request_body);

        test_case_end(test);

        return;
    }

    char url[_URL_SIZE] = {0};

    _url_for(fixture_server_port(&answering), url, sizeof(url));

    test_expect_true(test, "re-pointed at the answering fixture", http_service_captcha_set_url(&captcha, url));

    HTTP_Service_Captcha_Result recovered = http_service_captcha_verify(&captcha, "tok", "");

    fixture_server_join(&answering);

    test_expect_true(test, "the recovery verification succeeded", recovered.success);
    test_expect_u(test, "the 200 is reported, not the previous 500", 200, recovered.response_code);
    test_expect_string(test, "the recovery answer was parsed", "h", string_get_data(&recovered.hostname));

    /* The header list is cleared per call, so the recovery request must still carry exactly one
     * Content-Type even though the request between them failed at the HTTP layer. */
    char const *const first_type  = strstr(answering.request_headers, "Content-Type:");
    char const *const second_type = first_type == nullptr ? nullptr : strstr(first_type + 1, "Content-Type:");

    test_expect_not_null(test, "the recovery request carries a Content-Type", (void*) first_type);
    test_expect_null(test, "and exactly one", (void*) second_type);

    http_service_captcha_result_uninit(&recovered);
    http_service_captcha_result_uninit(&errored);
    http_service_captcha_uninit(&captcha);
    string_uninit(&answering.request_body);
    string_uninit(&failing.request_body);

    test_case_end(test);
}

static void _test_dead_port_is_not_ok(Test *const test) {
    test_case_begin(test, "a provider endpoint nothing is listening on fails closed");

    Fixture_Server server = DEFAULT_INITIALIZATION;

    server.script = FIXTURE_SCRIPT_OK;

    if (!test_expect_true(test, "fixture started", fixture_server_start(&server))) {
        test_case_end(test);

        return;
    }

    U16 const port = fixture_server_port(&server);

    /* Join FIRST, so the port is closed before the verification runs. */
    fixture_server_join(&server);
    string_uninit(&server.request_body);

    HTTP_Service_Captcha captcha = DEFAULT_INITIALIZATION;

    test_expect_true(test, "service configured", _service_for(&captcha, port));
    http_service_captcha_set_timeouts(&captcha, 500, 1000);

    HTTP_Service_Captcha_Result result = http_service_captcha_verify(&captcha, "tok", "");

    /* MSYS2 curl reads a firewalled closed port as a TIMEOUT rather than a refusal, so only "not
     * OK" is portable here; the point is that a dead provider never passes. */
    bool const expected = result.status == HTTP_SERVICE_CAPTCHA_STATUS_UNREACHABLE ||
        result.status == HTTP_SERVICE_CAPTCHA_STATUS_TIMEOUT;

    test_expect_false(test, "verification failed", result.success);
    test_expect_true(test, "status is UNREACHABLE or TIMEOUT", expected);

    http_service_captcha_result_uninit(&result);
    http_service_captcha_uninit(&captcha);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Refusals that never open a connection
 *============================================================================*/

/* Every refusal below must happen BEFORE the request. The fixture's connection_count is the
 * proof: it is started and joined around the call, and must still be 0. */
static void _expect_no_connection(Test *const test, char const *const label, HTTP_Service_Captcha *const captcha,
                                  char const *const token, bool const expect_success, HTTP_Service_Captcha_Status const expect_status) {
    Fixture_Server server = DEFAULT_INITIALIZATION;

    server.script         = FIXTURE_SCRIPT_OK;
    server.response_body  = "{\"success\":true}";

    if (!test_expect_true(test, "fixture started", fixture_server_start(&server))) {
        return;
    }

    char url[_URL_SIZE] = {0};

    _url_for(fixture_server_port(&server), url, sizeof(url));

    http_service_captcha_set_verify_tls(captcha, false);
    http_service_captcha_set_url(captcha, url);

    HTTP_Service_Captcha_Result result = http_service_captcha_verify(captcha, token, "");

    fixture_server_join(&server);

    test_expect_bool(test, label, expect_success, result.success);
    test_expect_u(test, "status as expected", expect_status, result.status);
    test_expect_u(test, "no connection was ever opened", 0, server.connection_count);

    http_service_captcha_result_uninit(&result);
    string_uninit(&server.request_body);
}

static void _test_refusals_precede_the_request(Test *const test) {
    test_case_begin(test, "disabled, misconfigured, empty-token and over-cap verifications never contact the provider");

    HTTP_Service_Captcha disabled = DEFAULT_INITIALIZATION;

    test_expect_true(test, "disabled service constructed", http_service_captcha_init_1(&disabled));
    test_expect_false(test, "a NONE service is not enabled", http_service_captcha_enabled(&disabled));
    _expect_no_connection(test, "disabled verification passes", &disabled, "tok", true, HTTP_SERVICE_CAPTCHA_STATUS_SKIPPED);
    http_service_captcha_uninit(&disabled);

    HTTP_Service_Captcha secretless = DEFAULT_INITIALIZATION;

    test_expect_true(test, "secretless service constructed", http_service_captcha_init_2(&secretless, HTTP_SERVICE_CAPTCHA_PROVIDER_TURNSTILE, ""));
    test_expect_false(test, "an enabled service with no secret is not valid", http_service_captcha_valid(&secretless));
    _expect_no_connection(test, "misconfigured verification fails closed", &secretless, "tok", false, HTTP_SERVICE_CAPTCHA_STATUS_MISCONFIGURED);
    http_service_captcha_uninit(&secretless);

    HTTP_Service_Captcha empty = DEFAULT_INITIALIZATION;

    test_expect_true(test, "service constructed", http_service_captcha_init_2(&empty, HTTP_SERVICE_CAPTCHA_PROVIDER_TURNSTILE, "s"));
    _expect_no_connection(test, "an empty token fails closed", &empty, "", false, HTTP_SERVICE_CAPTCHA_STATUS_TOKEN_INVALID);
    http_service_captcha_uninit(&empty);

    /* An oversized token must never become an oversized POST to a third party. */
    HTTP_Service_Captcha capped = DEFAULT_INITIALIZATION;

    test_expect_true(test, "service constructed", http_service_captcha_init_2(&capped, HTTP_SERVICE_CAPTCHA_PROVIDER_TURNSTILE, "s"));
    test_expect_u(test, "default cap", HTTP_SERVICE_CAPTCHA_TOKEN_MAX_SIZE, capped.token_max_size);

    char *const long_token = (char*) memory_alloc(HTTP_SERVICE_CAPTCHA_TOKEN_MAX_SIZE + 2);

    memory_set(long_token, HTTP_SERVICE_CAPTCHA_TOKEN_MAX_SIZE + 2, (U8) 'a');
    long_token[HTTP_SERVICE_CAPTCHA_TOKEN_MAX_SIZE + 1] = '\0';

    _expect_no_connection(test, "an over-cap token fails closed", &capped, long_token, false, HTTP_SERVICE_CAPTCHA_STATUS_TOKEN_TOO_LONG);

    memory_free(long_token);
    http_service_captcha_uninit(&capped);

    test_case_end(test);
}

/* Report items Low 12 and Low 14. A skipped verification once fabricated provider_success=true
 * to match `success`, and one shared error text - "invalid captcha configuration or token" -
 * covered both a deployment fault and a client one. Both are pinned here rather than in
 * _expect_no_connection above, which only ever looks at `success` and `status`. */
static void _test_skipped_and_refused_causes_are_distinct(Test *const test) {
    test_case_begin(test, "a skip fabricates no provider value, and MISCONFIGURED and TOKEN_INVALID each name their own cause");

    HTTP_Service_Captcha disabled = DEFAULT_INITIALIZATION;

    test_expect_true(test, "disabled service constructed", http_service_captcha_init_1(&disabled));

    HTTP_Service_Captcha_Result skipped = http_service_captcha_verify(&disabled, "tok", "");

    test_expect_true(test, "a disabled verification passes", skipped.success);
    test_expect_u(test, "status is SKIPPED", HTTP_SERVICE_CAPTCHA_STATUS_SKIPPED, skipped.status);

    /* The field is documented as the RAW provider value. No provider spoke, so it stays false:
     * a consumer logging it must not read a provider verdict that never happened. */
    test_expect_false(test, "provider_success is not fabricated on a skip", skipped.provider_success);
    test_expect_true(test, "no provider body was recorded", string_empty(&skipped.response));
    test_expect_true(test, "a skip carries no error text", string_empty(&skipped.error));

    http_service_captcha_result_uninit(&skipped);
    http_service_captcha_uninit(&disabled);

    HTTP_Service_Captcha secretless = DEFAULT_INITIALIZATION;

    test_expect_true(test, "secretless service constructed", http_service_captcha_init_2(&secretless, HTTP_SERVICE_CAPTCHA_PROVIDER_TURNSTILE, ""));

    HTTP_Service_Captcha_Result misconfigured = http_service_captcha_verify(&secretless, "tok", "");

    test_expect_false(test, "a misconfigured verification fails closed", misconfigured.success);
    test_expect_u(test, "status is MISCONFIGURED", HTTP_SERVICE_CAPTCHA_STATUS_MISCONFIGURED, misconfigured.status);
    test_expect_string(test, "the error names the deployment's configuration", "captcha service is not configured for verification", string_get_data(&misconfigured.error));

    http_service_captcha_result_uninit(&misconfigured);
    http_service_captcha_uninit(&secretless);

    HTTP_Service_Captcha armed = DEFAULT_INITIALIZATION;

    test_expect_true(test, "armed service constructed", http_service_captcha_init_2(&armed, HTTP_SERVICE_CAPTCHA_PROVIDER_TURNSTILE, "s"));
    test_expect_true(test, "an armed service is valid", http_service_captcha_valid(&armed));

    HTTP_Service_Captcha_Result invalid = http_service_captcha_verify(&armed, "", "");

    test_expect_false(test, "an empty token fails closed", invalid.success);
    test_expect_u(test, "status is TOKEN_INVALID", HTTP_SERVICE_CAPTCHA_STATUS_TOKEN_INVALID, invalid.status);
    test_expect_string(test, "the error names the client's token", "captcha token is empty", string_get_data(&invalid.error));

    http_service_captcha_result_uninit(&invalid);
    http_service_captcha_uninit(&armed);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Configuration
 *============================================================================*/

static void _test_https_only_verify_url(Test *const test) {
    test_case_begin(test, "set_url refuses a plaintext endpoint while TLS verification is on, and valid() agrees");

    HTTP_Service_Captcha captcha = DEFAULT_INITIALIZATION;

    test_expect_true(test, "service constructed", http_service_captcha_init_2(&captcha, HTTP_SERVICE_CAPTCHA_PROVIDER_TURNSTILE, "s"));
    test_expect_true(test, "the default Turnstile endpoint is valid", http_service_captcha_valid(&captcha));

    /* The secret travels in the body; an http:// typo would put it on the wire in clear text. */
    test_expect_false(test, "an http:// endpoint is refused", http_service_captcha_set_url(&captcha, "http://127.0.0.1:9/siteverify"));
    test_expect_string(test, "the endpoint is left untouched", HTTP_SERVICE_CAPTCHA_TURNSTILE_URL, string_get_data(&captcha.verify_url));
    test_expect_false(test, "an empty endpoint is refused", http_service_captcha_set_url(&captcha, ""));
    test_expect_true(test, "another https:// endpoint is accepted", http_service_captcha_set_url(&captcha, "https://example.test/siteverify"));
    test_expect_true(test, "scheme comparison is case-insensitive", http_service_captcha_set_url(&captcha, "HTTPS://example.test/siteverify"));

    /* The loopback suite's own escape hatch: turn TLS verification off deliberately, first. */
    http_service_captcha_set_verify_tls(&captcha, false);
    test_expect_true(test, "with verify_tls off, http:// is accepted", http_service_captcha_set_url(&captcha, "http://127.0.0.1:9/siteverify"));
    test_expect_true(test, "and the service is valid", http_service_captcha_valid(&captcha));

    /* Turning verification back on with a plaintext endpoint still stored must not stay valid. */
    http_service_captcha_set_verify_tls(&captcha, true);
    test_expect_false(test, "re-arming TLS invalidates a stored http:// endpoint", http_service_captcha_valid(&captcha));

    http_service_captcha_uninit(&captcha);

    test_case_end(test);
}

static void _test_provider_table(Test *const test) {
    test_case_begin(test, "provider_from_string is case-insensitive, has an explicit none, and never guesses");

    test_expect_u(test, "turnstile", HTTP_SERVICE_CAPTCHA_PROVIDER_TURNSTILE, http_service_captcha_provider_from_string("turnstile"));
    test_expect_u(test, "TurnStile", HTTP_SERVICE_CAPTCHA_PROVIDER_TURNSTILE, http_service_captcha_provider_from_string("TurnStile"));
    test_expect_u(test, "hcaptcha", HTTP_SERVICE_CAPTCHA_PROVIDER_HCAPTCHA, http_service_captcha_provider_from_string("hCaptcha"));
    test_expect_u(test, "recaptcha", HTTP_SERVICE_CAPTCHA_PROVIDER_RECAPTCHA, http_service_captcha_provider_from_string("RECAPTCHA"));
    test_expect_u(test, "none", HTTP_SERVICE_CAPTCHA_PROVIDER_NONE, http_service_captcha_provider_from_string("none"));

    /* An unset environment variable must not silently arm a backend this deployment has no
     * secret for, so an empty or unknown name is NONE, not Turnstile. */
    test_expect_u(test, "empty is NONE, not a guess", HTTP_SERVICE_CAPTCHA_PROVIDER_NONE, http_service_captcha_provider_from_string(""));
    test_expect_u(test, "null is NONE", HTTP_SERVICE_CAPTCHA_PROVIDER_NONE, http_service_captcha_provider_from_string(nullptr));
    test_expect_u(test, "unknown is NONE", HTTP_SERVICE_CAPTCHA_PROVIDER_NONE, http_service_captcha_provider_from_string("mystery"));

    test_expect_string(test, "turnstile url", HTTP_SERVICE_CAPTCHA_TURNSTILE_URL, http_service_captcha_provider_url(HTTP_SERVICE_CAPTCHA_PROVIDER_TURNSTILE));
    test_expect_string(test, "hcaptcha url", HTTP_SERVICE_CAPTCHA_HCAPTCHA_URL, http_service_captcha_provider_url(HTTP_SERVICE_CAPTCHA_PROVIDER_HCAPTCHA));
    test_expect_string(test, "recaptcha url", HTTP_SERVICE_CAPTCHA_RECAPTCHA_URL, http_service_captcha_provider_url(HTTP_SERVICE_CAPTCHA_PROVIDER_RECAPTCHA));
    test_expect_string(test, "none has no url", "", http_service_captcha_provider_url(HTTP_SERVICE_CAPTCHA_PROVIDER_NONE));

    /* provider_url returns from inside its switch, one trace_log_pop per case, so a case that
     * forgot its pop would leave the stack one frame deeper than it found it - for the rest of
     * the process, since nothing ever unwinds it. */
    USize const depth_before = trace_log_depth();

    http_service_captcha_provider_url(HTTP_SERVICE_CAPTCHA_PROVIDER_TURNSTILE);
    http_service_captcha_provider_url(HTTP_SERVICE_CAPTCHA_PROVIDER_HCAPTCHA);
    http_service_captcha_provider_url(HTTP_SERVICE_CAPTCHA_PROVIDER_RECAPTCHA);
    http_service_captcha_provider_url(HTTP_SERVICE_CAPTCHA_PROVIDER_NONE);

    test_expect_u(test, "provider_url leaves the trace stack balanced", depth_before, trace_log_depth());

    test_case_end(test);
}

static void _test_provider_parse_reports_a_typo(Test *const test) {
    test_case_begin(test, "provider_parse accepts the three names and an explicit disable, and REFUSES an unrecognized one");

    HTTP_Service_Captcha_Provider provider = HTTP_SERVICE_CAPTCHA_PROVIDER_NONE;

    test_expect_true(test, "turnstile parses", http_service_captcha_provider_parse("turnstile", &provider));
    test_expect_u(test, "as TURNSTILE", HTTP_SERVICE_CAPTCHA_PROVIDER_TURNSTILE, provider);
    test_expect_true(test, "case is ignored", http_service_captcha_provider_parse("hCaptcha", &provider));
    test_expect_u(test, "as HCAPTCHA", HTTP_SERVICE_CAPTCHA_PROVIDER_HCAPTCHA, provider);
    test_expect_true(test, "RECAPTCHA parses", http_service_captcha_provider_parse("RECAPTCHA", &provider));
    test_expect_u(test, "as RECAPTCHA", HTTP_SERVICE_CAPTCHA_PROVIDER_RECAPTCHA, provider);

    /* An unset variable and a deliberate disable are both legitimate configuration, so they
     * answer true - only a name that was MEANT to be a provider and is not one answers false. */
    test_expect_true(test, "null is an accepted disable", http_service_captcha_provider_parse(nullptr, &provider));
    test_expect_u(test, "as NONE", HTTP_SERVICE_CAPTCHA_PROVIDER_NONE, provider);
    test_expect_true(test, "empty is an accepted disable", http_service_captcha_provider_parse("", &provider));
    test_expect_true(test, "\"none\" is an accepted disable", http_service_captcha_provider_parse("none", &provider));
    test_expect_true(test, "\"OFF\" is an accepted disable", http_service_captcha_provider_parse("OFF", &provider));
    test_expect_u(test, "still NONE", HTTP_SERVICE_CAPTCHA_PROVIDER_NONE, provider);

    /* The whole point: "turnstlie" through provider_from_string builds a DISABLED service whose
     * every verification silently passes. Here it is a false a deployment can fail startup on,
     * and the caller's own pre-set value is left standing rather than overwritten with NONE. */
    provider = HTTP_SERVICE_CAPTCHA_PROVIDER_TURNSTILE;

    test_expect_false(test, "a typo is refused", http_service_captcha_provider_parse("turnstlie", &provider));
    test_expect_u(test, "and provider_out is left untouched", HTTP_SERVICE_CAPTCHA_PROVIDER_TURNSTILE, provider);
    test_expect_false(test, "an unknown name is refused", http_service_captcha_provider_parse("mystery", &provider));
    test_expect_false(test, "a partial name is refused", http_service_captcha_provider_parse("turn", &provider));

    /* The lenient wrapper still folds every one of those into NONE - that is what it is for. */
    test_expect_u(test, "from_string folds a typo to NONE", HTTP_SERVICE_CAPTCHA_PROVIDER_NONE, http_service_captcha_provider_from_string("turnstlie"));
    test_expect_u(test, "from_string still parses a real name", HTTP_SERVICE_CAPTCHA_PROVIDER_HCAPTCHA, http_service_captcha_provider_from_string("hcaptcha"));

    test_case_end(test);
}

static void _test_status_is_outage_covers_every_status(Test *const test) {
    test_case_begin(test, "status_is_outage answers for every enum value: the provider's or the deployment's fault, or the client's");

    /* Pinned over EVERY value, so a status added later cannot join the wrong side unnoticed. */
    test_expect_true(test, "ERROR is ours", http_service_captcha_status_is_outage(HTTP_SERVICE_CAPTCHA_STATUS_ERROR));
    test_expect_true(test, "MALFORMED_RESPONSE is ours", http_service_captcha_status_is_outage(HTTP_SERVICE_CAPTCHA_STATUS_MALFORMED_RESPONSE));
    test_expect_true(test, "MISCONFIGURED is ours", http_service_captcha_status_is_outage(HTTP_SERVICE_CAPTCHA_STATUS_MISCONFIGURED));
    test_expect_true(test, "PROVIDER_ERROR is ours", http_service_captcha_status_is_outage(HTTP_SERVICE_CAPTCHA_STATUS_PROVIDER_ERROR));
    test_expect_true(test, "TIMEOUT is ours", http_service_captcha_status_is_outage(HTTP_SERVICE_CAPTCHA_STATUS_TIMEOUT));
    test_expect_true(test, "TLS is ours", http_service_captcha_status_is_outage(HTTP_SERVICE_CAPTCHA_STATUS_TLS));
    test_expect_true(test, "UNREACHABLE is ours", http_service_captcha_status_is_outage(HTTP_SERVICE_CAPTCHA_STATUS_UNREACHABLE));

    test_expect_false(test, "OK is not an outage", http_service_captcha_status_is_outage(HTTP_SERVICE_CAPTCHA_STATUS_OK));
    test_expect_false(test, "SKIPPED is not an outage", http_service_captcha_status_is_outage(HTTP_SERVICE_CAPTCHA_STATUS_SKIPPED));
    test_expect_false(test, "TOKEN_INVALID is the client's", http_service_captcha_status_is_outage(HTTP_SERVICE_CAPTCHA_STATUS_TOKEN_INVALID));
    test_expect_false(test, "TOKEN_REJECTED is the client's", http_service_captcha_status_is_outage(HTTP_SERVICE_CAPTCHA_STATUS_TOKEN_REJECTED));
    test_expect_false(test, "TOKEN_TOO_LONG is the client's", http_service_captcha_status_is_outage(HTTP_SERVICE_CAPTCHA_STATUS_TOKEN_TOO_LONG));

    /* A cast value blames the deployment rather than accusing the user. */
    test_expect_true(test, "an out-of-range status is ours", http_service_captcha_status_is_outage((HTTP_Service_Captcha_Status) 99));

    test_case_end(test);
}

static void _test_set_provider_refuses_out_of_range(Test *const test) {
    test_case_begin(test, "set_provider refuses a value outside the enum, exactly as the constructors do");

    HTTP_Service_Captcha captcha = DEFAULT_INITIALIZATION;

    test_expect_true(test, "service constructed", http_service_captcha_init_2(&captcha, HTTP_SERVICE_CAPTCHA_PROVIDER_TURNSTILE, "s"));

    /* Storing 42 would leave a valid() service that POSTs to "" - failing closed only by
     * accident, one request later and one layer down in http_client. */
    test_expect_false(test, "an out-of-range provider is refused", http_service_captcha_set_provider(&captcha, (HTTP_Service_Captcha_Provider) 42));
    test_expect_u(test, "the provider is untouched", HTTP_SERVICE_CAPTCHA_PROVIDER_TURNSTILE, captcha.provider);
    test_expect_string(test, "the endpoint is untouched", HTTP_SERVICE_CAPTCHA_TURNSTILE_URL, string_get_data(&captcha.verify_url));
    test_expect_true(test, "and the service is still valid", http_service_captcha_valid(&captcha));

    test_expect_true(test, "an in-range provider is accepted", http_service_captcha_set_provider(&captcha, HTTP_SERVICE_CAPTCHA_PROVIDER_HCAPTCHA));
    test_expect_string(test, "and installs its default endpoint", HTTP_SERVICE_CAPTCHA_HCAPTCHA_URL, string_get_data(&captcha.verify_url));

    /* Documented on set_url: a later set_provider REVERTS a URL override. */
    test_expect_true(test, "an https override is accepted", http_service_captcha_set_url(&captcha, "https://example.test/siteverify"));
    test_expect_true(test, "set_provider succeeds", http_service_captcha_set_provider(&captcha, HTTP_SERVICE_CAPTCHA_PROVIDER_RECAPTCHA));
    test_expect_string(test, "and reverts the override to the provider default", HTTP_SERVICE_CAPTCHA_RECAPTCHA_URL, string_get_data(&captcha.verify_url));

    test_expect_true(test, "NONE is in range", http_service_captcha_set_provider(&captcha, HTTP_SERVICE_CAPTCHA_PROVIDER_NONE));
    test_expect_false(test, "and disables the service", http_service_captcha_enabled(&captcha));

    http_service_captcha_uninit(&captcha);

    test_case_end(test);
}

static void _test_constructor_refuses_whole(Test *const test) {
    test_case_begin(test, "a provider value outside the enum is refused whole, leaving the instance zeroed");

    HTTP_Service_Captcha captcha = DEFAULT_INITIALIZATION;

    /* An environment-derived provider reaches this through a cast; storing it would leave a
     * service whose endpoint switch falls through to "" much later. */
    test_expect_false(test, "an out-of-range provider is refused", http_service_captcha_init_2(&captcha, (HTTP_Service_Captcha_Provider) 42, "s"));
    test_expect_false(test, "the refused instance is not enabled", captcha.enabled);
    test_expect_null(test, "the refused instance holds no client handle", (void*) captcha.client);
    test_expect_u(test, "the refused instance holds no secret", 0, string_get_size(&captcha.secret));

    test_case_end(test);
}

#ifdef ARENA_IMPLEMENTATION
static void _test_arena_twins(Test *const test) {
    test_case_begin(test, "the arena constructors build the same service and verify through the same path");

    Arena arena = arena_init_1(1 << 20, ARENA_TYPE_LINEAR);

    HTTP_Service_Captcha disabled = DEFAULT_INITIALIZATION;

    test_expect_true(test, "alloc_init_1 built a disabled service", http_service_captcha_alloc_init_1(&disabled, &arena));
    test_expect_false(test, "and it is disabled", http_service_captcha_enabled(&disabled));
    http_service_captcha_uninit(&disabled);

    Fixture_Server server = DEFAULT_INITIALIZATION;

    server.script         = FIXTURE_SCRIPT_OK;
    server.response_body  = "{\"success\":true,\"hostname\":\"h\"}";

    if (!test_expect_true(test, "fixture started", fixture_server_start(&server))) {
        arena_uninit(&arena, ARENA_TYPE_LINEAR);

        test_case_end(test);

        return;
    }

    HTTP_Service_Captcha captcha = DEFAULT_INITIALIZATION;

    test_expect_true(test, "alloc_init_2 built a Turnstile service", http_service_captcha_alloc_init_2(&captcha, HTTP_SERVICE_CAPTCHA_PROVIDER_TURNSTILE, "s", &arena));

    char url[_URL_SIZE] = {0};

    _url_for(fixture_server_port(&server), url, sizeof(url));

    http_service_captcha_set_verify_tls(&captcha, false);
    test_expect_true(test, "endpoint accepted", http_service_captcha_set_url(&captcha, url));

    HTTP_Service_Captcha_Result result = http_service_captcha_verify(&captcha, "tok", "1.2.3.4");

    fixture_server_join(&server);

    test_expect_true(test, "arena-backed verification succeeded", result.success);
    test_expect_string(test, "arena-backed body is identical", "secret=s&response=tok&remoteip=1.2.3.4", string_get_data(&server.request_body));
    test_expect_string(test, "arena-backed hostname parsed", "h", string_get_data(&result.hostname));

    http_service_captcha_result_uninit(&result);
    http_service_captcha_uninit(&captcha);
    string_uninit(&server.request_body);
    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}
#endif // ARENA_IMPLEMENTATION

int main(void) {
    LogConfig const log_config = {
        .level             = LOG_LEVEL_ERROR,
        .stream            = LOG_STREAM_STDOUT,
        .timestamp_enabled = true,
        .autoflush         = true
    };

    log_init(log_config);

    Test test = test_init("tests/http/service/captcha/test_all.c");

    test_verbose_set(&test, false);

    test_suite_begin(&test, "http/service/captcha loopback fixture");

    _test_request_body_is_byte_exact(&test);
    _test_request_body_percent_encodes(&test);
    _test_null_remote_ip_sends_no_field(&test);
    _test_cached_client_does_not_duplicate_headers(&test);
    _test_rejection_populates_error_codes(&test);
    _test_malformed_answers_fail_closed(&test);
    _test_non_2xx_never_passes(&test);
    _test_hostname_expectation(&test);
    _test_hostname_expectation_is_case_insensitive(&test);
    _test_action_expectation_is_byte_exact(&test);
    _test_action_expectation(&test);
    _test_score_expectation(&test);
    _test_stall_is_timeout(&test);
    _test_recovers_after_a_timed_out_transfer(&test);
    _test_recovers_after_a_provider_error(&test);
    _test_dead_port_is_not_ok(&test);
    _test_refusals_precede_the_request(&test);
    _test_skipped_and_refused_causes_are_distinct(&test);
    _test_https_only_verify_url(&test);
    _test_provider_table(&test);
    _test_provider_parse_reports_a_typo(&test);
    _test_status_is_outage_covers_every_status(&test);
    _test_set_provider_refuses_out_of_range(&test);
    _test_constructor_refuses_whole(&test);
#ifdef ARENA_IMPLEMENTATION
    _test_arena_twins(&test);
#endif // ARENA_IMPLEMENTATION

    test_suite_end(&test);

    I32 const failures = test_uninit(&test);

    http_client_global_uninit();

    return failures;
}