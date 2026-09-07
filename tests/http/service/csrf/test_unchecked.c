#include <arena/arena.h>
#include <http/service/csrf/csrf.h>
#include <log/log.h>
#include <test/test.h>

/*
 * Behavioral tests for include/http/service/csrf/csrf.c built WITHOUT ERROR_CHECK_ENABLED.
 *
 * error_check_null in this module guards only null-pointer contracts on the SERVICE and its
 * configuration - deliberately absent here (undefined behavior with the checks compiled out,
 * and this file never exercises them). Everything below is coded as an ordinary runtime branch,
 * never routed through error_check, per the value-dependent-refusal standard: a data-dependent
 * decision is never an abort primitive. This build proves each one refuses identically whether
 * ERROR_CHECK_ENABLED is defined or not.
 *
 *   - The one this suite exists for: a null request token on a PROTECTED method. csrf.c placed
 *     error_check_null(token) AFTER the method gate, so whether a missing X-CSRF-Token header
 *     aborted the whole server depended on the request's METHOD - request data deciding an
 *     abort. With the checks compiled out it was worse: a null dereference inside char_length
 *     instead. It answers false in both builds now.
 *   - Every constructor refusal (browser-dropped cookie configurations, an empty header_name, a
 *     sub-floor token_byte_count, a ttl of 0). ttl 0 and token_byte_count 0 used to reach
 *     error_check_non_value_uint, which ABORTS - and compiles away here, leaving a service whose
 *     cookie expired on arrival.
 *   - cookie_create's empty-token refusal, and the rule that two EMPTY sides never compare equal
 *     (a request with neither header nor cookie is exactly the cross-site case).
 *   - alloc_init_2's whole-refusal on a genuinely REJECTED arena handle (byte_size 0), a plain
 *     runtime condition (the template's copied name comes back EMPTY), not an error_check path.
 */

I32 main(void) {
    LogConfig const log_config = { .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true };

    log_init(log_config);

    Test test = test_init("tests/http/service/csrf/test_unchecked.c");

    test_suite_begin(&test, "http_service_csrf (unchecked)");
    test_case_begin(&test, "a null or empty request token on a protected method refuses instead of aborting");

    HTTP_Service_CSRF csrf = DEFAULT_INITIALIZATION;

    test_expect_true(&test, "a valid configuration still initializes", http_service_csrf_init_2(&csrf, 32, 3600, "csrf", "/", "X-CSRF-Token", "Lax", false, false));

    HTTP_Service_CSRF_Token stored = DEFAULT_INITIALIZATION;

    test_expect_true(&test, "token_create succeeds", http_service_csrf_token_create(&csrf, &stored));

    test_expect_false(&test, "request_allowed with a null token on POST answers false", http_service_csrf_request_allowed_1(&csrf, "POST", nullptr, &stored));
    test_expect_false(&test, "request_allowed with a null stored token on POST answers false", http_service_csrf_request_allowed_1(&csrf, "POST", "abc", nullptr));
    test_expect_false(&test, "request_allowed with an empty token on POST answers false", http_service_csrf_request_allowed_1(&csrf, "POST", "", &stored));
    test_expect_true(&test, "request_allowed still lets GET through with no token at all", http_service_csrf_request_allowed_1(&csrf, "GET", nullptr, nullptr));

    test_expect_false(&test, "request_allowed_2 with a null header token answers false", http_service_csrf_request_allowed_2(&csrf, "POST", nullptr, "csrf=abc123"));
    test_expect_false(&test, "request_allowed_2 with a null Cookie header answers false", http_service_csrf_request_allowed_2(&csrf, "POST", "abc123", nullptr));
    test_expect_false(&test, "request_allowed_2 with neither answers false", http_service_csrf_request_allowed_2(&csrf, "POST", nullptr, nullptr));
    test_expect_false(&test, "token_verify_2 never lets two empty sides compare equal", http_service_csrf_token_verify_2(&csrf, "", ""));
    test_expect_false(&test, "token_verify_2 refuses a null header token", http_service_csrf_token_verify_2(&csrf, nullptr, "abc"));
    test_expect_false(&test, "token_verify_2 refuses a null cookie value", http_service_csrf_token_verify_2(&csrf, "abc", nullptr));
    test_expect_true(&test, "and a genuinely matching pair still passes", http_service_csrf_token_verify_2(&csrf, "abc123", "abc123"));

    /* cookie_read itself error_checked its Cookie header until this round - so in the CHECKED
     * build a request with no Cookie header aborted the server, and in this build it read a null
     * pointer. It is a plain runtime branch now, and both builds must agree. */
    String read_null = http_service_csrf_cookie_read(&csrf, nullptr);

    test_expect_string(&test, "cookie_read on a null Cookie header answers a terminated \"\"", "", string_get_data(&read_null));

    String read_null_4 = http_service_csrf_cookie_read_4(&csrf, nullptr);

    test_expect_string(&test, "and so does the String tier", "", string_get_data(&read_null_4));
    test_expect_false(&test, "token_verify_4 refuses a null cookie String", http_service_csrf_token_verify_4(&csrf, "abc", nullptr));

    string_uninit(&read_null_4);
    string_uninit(&read_null);

    test_expect_true(&test, "method_protected still treats a null method as protected", http_service_csrf_method_protected(nullptr));
    test_expect_true(&test, "and still refuses to read \"get\" as GET", http_service_csrf_method_protected("get"));

    String empty_token = http_service_csrf_cookie_create(&csrf, "");

    test_expect_u(&test, "cookie_create still refuses an empty token", 0, string_get_size(&empty_token));

    String injected = http_service_csrf_cookie_create(&csrf, "x\r\nSet-Cookie: admin=1");

    test_expect_u(&test, "cookie_create still refuses a CRLF token", 0, string_get_size(&injected));

    string_uninit(&injected);
    string_uninit(&empty_token);
    http_service_csrf_token_uninit(&stored);
    http_service_csrf_uninit(&csrf);

    test_case_end(&test);

    test_case_begin(&test, "constructor refusals hold identically without ERROR_CHECK_ENABLED");

    HTTP_Service_CSRF service = DEFAULT_INITIALIZATION;

    test_expect_false(&test, "`__Host-` without Secure is still refused", http_service_csrf_init_2(&service, 32, 3600, "__Host-csrf", "/", "X-CSRF-Token", "Strict", false, false));
    test_expect_false(&test, "`__Host-` with a non-root path is still refused", http_service_csrf_init_2(&service, 32, 3600, "__Host-csrf", "/app", "X-CSRF-Token", "Strict", true, false));
    test_expect_false(&test, "`__Secure-` without Secure is still refused", http_service_csrf_init_2(&service, 32, 3600, "__Secure-csrf", "/", "X-CSRF-Token", "Strict", false, false));
    test_expect_false(&test, "SameSite=None without Secure is still refused", http_service_csrf_init_2(&service, 32, 3600, "csrf", "/", "X-CSRF-Token", "None", false, false));
    test_expect_false(&test, "an empty cookie name is still refused", http_service_csrf_init_2(&service, 32, 3600, "", "/", "X-CSRF-Token", "Strict", true, false));
    test_expect_false(&test, "an empty header_name is still refused", http_service_csrf_init_2(&service, 32, 3600, "csrf", "/", "", "Strict", true, false));

    /* These two reached error_check_non_value_uint before this round: with the checks compiled
     * out they used to sail through and build a service whose cookie expired on arrival. */
    test_expect_false(
        &test, "a token_byte_count of 0 is a refusal, not an abort that vanished",
        http_service_csrf_init_2(&service, 0, 3600, "csrf", "/", "X-CSRF-Token", "Strict", true, false));
    test_expect_false(&test, "a sub-floor token_byte_count is still refused", http_service_csrf_init_2(&service, 8, 3600, "csrf", "/", "X-CSRF-Token", "Strict", true, false));
    test_expect_false(&test, "a ttl of 0 is a refusal, not an abort that vanished", http_service_csrf_init_2(&service, 32, 0, "csrf", "/", "X-CSRF-Token", "Strict", true, false));

    test_case_end(&test);

    test_case_begin(&test, "a rejected arena still refuses the whole service");

    Arena starved = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    HTTP_Service_CSRF refused = DEFAULT_INITIALIZATION;

    test_expect_false(
        &test, "alloc_init_2 refuses whole on a refused arena", http_service_csrf_alloc_init_2(&refused, 32, 3600, "csrf", "/", "X-CSRF-Token", "Strict", true, false, &starved));
    test_expect_u(&test, "and leaves *self zeroed", 0, refused.ttl);

    arena_uninit(&starved, ARENA_TYPE_LINEAR);

    test_case_end(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}