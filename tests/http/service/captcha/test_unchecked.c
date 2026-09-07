/*
 * test_unchecked.c - Behavioral tests for include/http/service/captcha/captcha.c built WITHOUT
 * ERROR_CHECK_ENABLED.
 *
 * error_check_null in this module guards NULL-POINTER contracts (self/token/url/secret) - true
 * undefined behaviour with the checks compiled out, and this file never exercises those. What it
 * DOES exercise is the half of the contract that must hold in EITHER build, because it is a
 * value, not a contract: everything derived from a request is refused as data.
 *
 * That distinction matters here more than in most modules. The token is attacker-controlled: if
 * the empty-token or over-cap refusal were an error_check_* rather than an ordinary branch, a
 * client could abort the server by sending a 5 KiB token to a build with the checks off. These
 * cases prove those refusals are real branches by watching them still fire, and still open no
 * connection, with every check compiled to nothing.
 */
#include <stdio.h>

#include <http/service/captcha/captcha.h>
#include <log/log.h>
#include <test/test.h>

#include <fixture.h>

#define _URL_SIZE 128

static void _url_for(U16 const port, char *const out, USize const out_capacity) {
    snprintf(out, out_capacity, "http://127.0.0.1:%u/siteverify", (unsigned) port);
}

/* Runs one verification against a live fixture and reports whether the provider was contacted. */
static void _verify_against_fixture(Test *const test, HTTP_Service_Captcha *const captcha, char const *const token,
                                    char const *const body, HTTP_Service_Captcha_Status const expect_status, USize const expect_connections) {
    Fixture_Server server = DEFAULT_INITIALIZATION;

    server.script         = FIXTURE_SCRIPT_OK;
    server.response_body  = body;

    if (!test_expect_true(test, "fixture started", fixture_server_start(&server))) {
        return;
    }

    char url[_URL_SIZE] = {0};

    _url_for(fixture_server_port(&server), url, sizeof(url));

    http_service_captcha_set_verify_tls(captcha, false);
    http_service_captcha_set_url(captcha, url);

    HTTP_Service_Captcha_Result result = http_service_captcha_verify(captcha, token, "");

    fixture_server_join(&server);

    test_expect_false(test, "verification failed closed", result.success);
    test_expect_u(test, "status as expected", expect_status, result.status);
    test_expect_u(test, "connections opened", expect_connections, server.connection_count);

    http_service_captcha_result_uninit(&result);
    string_uninit(&server.request_body);
}

static void _test_empty_token_refused_without_checks(Test *const test) {
    test_case_begin(test, "an empty token is still refused, and still opens no connection, with error_check compiled out");

    HTTP_Service_Captcha captcha = DEFAULT_INITIALIZATION;

    test_expect_true(test, "service constructed", http_service_captcha_init_2(&captcha, HTTP_SERVICE_CAPTCHA_PROVIDER_TURNSTILE, "s"));
    _verify_against_fixture(test, &captcha, "", "{\"success\":true}", HTTP_SERVICE_CAPTCHA_STATUS_TOKEN_INVALID, 0);
    http_service_captcha_uninit(&captcha);

    test_case_end(test);
}

static void _test_over_cap_token_refused_without_checks(Test *const test) {
    test_case_begin(test, "an over-cap token is still refused before the request with error_check compiled out");

    HTTP_Service_Captcha captcha = DEFAULT_INITIALIZATION;

    test_expect_true(test, "service constructed", http_service_captcha_init_2(&captcha, HTTP_SERVICE_CAPTCHA_PROVIDER_TURNSTILE, "s"));

    char *const long_token = (char*) memory_alloc(HTTP_SERVICE_CAPTCHA_TOKEN_MAX_SIZE + 2);

    memory_set(long_token, HTTP_SERVICE_CAPTCHA_TOKEN_MAX_SIZE + 2, (U8) 'a');
    long_token[HTTP_SERVICE_CAPTCHA_TOKEN_MAX_SIZE + 1] = '\0';

    _verify_against_fixture(test, &captcha, long_token, "{\"success\":true}", HTTP_SERVICE_CAPTCHA_STATUS_TOKEN_TOO_LONG, 0);

    /* The cap is configurable, and raising it lets the same token through to a real request. */
    http_service_captcha_set_token_max_size(&captcha, 0);
    _verify_against_fixture(test, &captcha, long_token, "{\"success\":false}", HTTP_SERVICE_CAPTCHA_STATUS_TOKEN_REJECTED, 1);

    memory_free(long_token);
    http_service_captcha_uninit(&captcha);

    test_case_end(test);
}

static void _test_malformed_answer_refused_without_checks(Test *const test) {
    test_case_begin(test, "a malformed provider answer still fails closed rather than aborting with error_check compiled out");

    HTTP_Service_Captcha captcha = DEFAULT_INITIALIZATION;

    test_expect_true(test, "service constructed", http_service_captcha_init_2(&captcha, HTTP_SERVICE_CAPTCHA_PROVIDER_TURNSTILE, "s"));
    _verify_against_fixture(test, &captcha, "tok", "[]", HTTP_SERVICE_CAPTCHA_STATUS_MALFORMED_RESPONSE, 1);
    _verify_against_fixture(test, &captcha, "tok", "{", HTTP_SERVICE_CAPTCHA_STATUS_MALFORMED_RESPONSE, 1);
    http_service_captcha_uninit(&captcha);

    test_case_end(test);
}

static void _test_configuration_refusals_without_checks(Test *const test) {
    test_case_begin(test, "the https-only endpoint rule and the out-of-range provider refusal are branches, not checks");

    HTTP_Service_Captcha captcha = DEFAULT_INITIALIZATION;

    test_expect_true(test, "service constructed", http_service_captcha_init_2(&captcha, HTTP_SERVICE_CAPTCHA_PROVIDER_TURNSTILE, "s"));
    test_expect_false(test, "an http:// endpoint is still refused", http_service_captcha_set_url(&captcha, "http://127.0.0.1:9/siteverify"));
    test_expect_false(test, "an empty endpoint is still refused", http_service_captcha_set_url(&captcha, ""));
    test_expect_string(test, "the endpoint is untouched", HTTP_SERVICE_CAPTCHA_TURNSTILE_URL, string_get_data(&captcha.verify_url));
    http_service_captcha_uninit(&captcha);

    HTTP_Service_Captcha refused = DEFAULT_INITIALIZATION;

    test_expect_false(test, "an out-of-range provider is still refused", http_service_captcha_init_2(&refused, (HTTP_Service_Captcha_Provider) 42, "s"));
    test_expect_false(test, "the refused instance is not enabled", refused.enabled);

    test_case_end(test);
}

int main(void) {
    LogConfig const log_config = {
        .level             = LOG_LEVEL_ERROR,
        .stream            = LOG_STREAM_STDOUT,
        .timestamp_enabled = true,
        .autoflush         = true
    };

    log_init(log_config);

    Test test = test_init("tests/http/service/captcha/test_unchecked.c");

    test_verbose_set(&test, false);

    test_suite_begin(&test, "http/service/captcha (ERROR_CHECK_ENABLED off)");

    _test_empty_token_refused_without_checks(&test);
    _test_over_cap_token_refused_without_checks(&test);
    _test_malformed_answer_refused_without_checks(&test);
    _test_configuration_refusals_without_checks(&test);

    test_suite_end(&test);

    I32 const failures = test_uninit(&test);

    http_client_global_uninit();

    return failures;
}