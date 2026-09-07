#include <arena/arena.h>
#include <http/service/session/session.h>
#include <log/log.h>
#include <test/test.h>

/*
 * Behavioral tests for include/http/service/session/session.c built WITHOUT ERROR_CHECK_ENABLED.
 *
 * error_check_null in this module guards only null-pointer contracts on the SERVICE and its
 * configuration - deliberately absent here (undefined behavior with the checks compiled out,
 * and this file never exercises them). Everything below is coded as an ordinary runtime branch,
 * never routed through error_check, per the value-dependent-refusal standard: a data-dependent
 * decision is never an abort primitive. This build proves each one refuses identically whether
 * ERROR_CHECK_ENABLED is defined or not.
 *
 *   - Every constructor refusal (browser-dropped cookie configurations, a sub-floor
 *     token_byte_count, a ttl of 0). The old constructors reached ttl 0 and token_byte_count 0
 *     through error_check_non_value_uint, which ABORTS - and compiles away here, leaving a
 *     service that silently emitted an expires-on-arrival cookie. They are plain conditions now.
 *   - REQUEST DATA refusals: a null or empty candidate token, a null or empty stored hash, a
 *     null Cookie header. A missing cookie is the ordinary shape of an unauthenticated request;
 *     aborting the server on it would be a remote kill switch.
 *   - cookie_create's empty-token refusal.
 *   - alloc_init_2's whole-refusal on a genuinely REJECTED arena handle (byte_size 0), which is
 *     a plain runtime condition (the template's copied name comes back EMPTY), not an
 *     error_check path.
 */

I32 main(void) {
    LogConfig const log_config = { .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true };

    log_init(log_config);

    Test test = test_init("tests/http/service/session/test_unchecked.c");

    test_suite_begin(&test, "http_service_session (unchecked)");
    test_case_begin(&test, "constructor refusals hold identically without ERROR_CHECK_ENABLED");

    HTTP_Service_Session service = DEFAULT_INITIALIZATION;

    test_expect_false(&test, "`__Host-` without Secure is still refused", http_service_session_init_2(&service, 32, 600, "__Host-session", "/", "Strict", false, true));
    test_expect_false(&test, "`__Host-` with a non-root path is still refused", http_service_session_init_2(&service, 32, 600, "__Host-session", "/app", "Strict", true, true));
    test_expect_false(&test, "`__Secure-` without Secure is still refused", http_service_session_init_2(&service, 32, 600, "__Secure-session", "/", "Strict", false, true));
    test_expect_false(&test, "SameSite=None without Secure is still refused", http_service_session_init_2(&service, 32, 600, "sid", "/", "None", false, true));
    test_expect_false(&test, "an empty cookie name is still refused", http_service_session_init_2(&service, 32, 600, "", "/", "Strict", true, true));
    test_expect_false(&test, "a non-token cookie name is still refused", http_service_session_init_2(&service, 32, 600, "bad name", "/", "Strict", true, true));

    /* These two reached error_check_non_value_uint before this round: with the checks compiled
     * out they used to sail through and build a service nobody could log into. */
    test_expect_false(&test, "a token_byte_count of 0 is a refusal, not an abort that vanished", http_service_session_init_2(&service, 0, 600, "sid", "/", "Strict", true, true));
    test_expect_false(&test, "a sub-floor token_byte_count is still refused", http_service_session_init_2(&service, 8, 600, "sid", "/", "Strict", true, true));
    test_expect_false(&test, "a ttl of 0 is a refusal, not an abort that vanished", http_service_session_init_2(&service, 32, 0, "sid", "/", "Strict", true, true));

    test_case_end(&test);

    test_case_begin(&test, "request-data refusals never abort, with or without the checks");

    HTTP_Service_Session session = DEFAULT_INITIALIZATION;

    test_expect_true(&test, "a valid configuration still initializes", http_service_session_init_2(&session, 32, 600, "sid", "/", "Lax", false, true));

    String hash = http_service_session_token_hash(&session, "abc");

    test_expect_false(&test, "a null candidate token answers false", http_service_session_token_verify_1(&session, nullptr, string_get_data(&hash)));
    test_expect_false(&test, "a null stored hash answers false", http_service_session_token_verify_1(&session, "abc", nullptr));
    test_expect_false(&test, "an empty stored hash answers false", http_service_session_token_verify_1(&session, "abc", ""));
    test_expect_false(&test, "an empty candidate token answers false", http_service_session_token_verify_1(&session, "", string_get_data(&hash)));
    test_expect_false(&test, "a null String hash answers false", http_service_session_token_verify_4(&session, "abc", nullptr));
    test_expect_true(&test, "the matching pair still verifies", http_service_session_token_verify_1(&session, "abc", string_get_data(&hash)));

    String lookup = DEFAULT_INITIALIZATION;

    test_expect_false(&test, "a null Cookie header answers false rather than aborting", http_service_session_cookie_hash(&session, nullptr, &lookup));
    test_expect_false(&test, "a Cookie header without this cookie answers false", http_service_session_cookie_hash(&session, "other=1", &lookup));

    /* cookie_read itself error_checked its Cookie header until this round - so in the CHECKED
     * build a request with no Cookie header aborted the server, and in this build it read a null
     * pointer. It is a plain runtime branch now, and both builds must agree. */
    String read_null = http_service_session_cookie_read(&session, nullptr);

    test_expect_string(&test, "cookie_read on a null Cookie header answers a terminated \"\"", "", string_get_data(&read_null));

    String read_null_4 = http_service_session_cookie_read_4(&session, nullptr);

    test_expect_string(&test, "and so does the String tier", "", string_get_data(&read_null_4));

    String empty_token = http_service_session_cookie_create(&session, "");

    test_expect_u(&test, "cookie_create still refuses an empty token", 0, string_get_size(&empty_token));

    String injected = http_service_session_cookie_create(&session, "x\r\nSet-Cookie: admin=1");

    test_expect_u(&test, "cookie_create still refuses a CRLF token", 0, string_get_size(&injected));

    string_uninit(&injected);
    string_uninit(&empty_token);
    string_uninit(&read_null_4);
    string_uninit(&read_null);
    string_uninit(&lookup);
    string_uninit(&hash);
    http_service_session_uninit(&session);

    test_case_end(&test);

    test_case_begin(&test, "a rejected arena still refuses the whole service");

    Arena starved = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    HTTP_Service_Session refused = DEFAULT_INITIALIZATION;

    test_expect_false(&test, "alloc_init_2 refuses whole on a starved arena", http_service_session_alloc_init_2(&refused, 32, 600, "sid", "/", "Strict", true, true, &starved));
    test_expect_u(&test, "and leaves *self zeroed", 0, refused.ttl);

    arena_uninit(&starved, ARENA_TYPE_LINEAR);

    test_case_end(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}