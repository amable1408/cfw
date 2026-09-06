#include <arena/arena.h>
#include <http/cookie/cookie.h>
#include <log/log.h>
#include <test/test.h>

/*
 * Behavioral tests for include/http/cookie/cookie.c built WITHOUT ERROR_CHECK_ENABLED.
 *
 * error_check_null in this module guards only null-pointer contracts on the public entry
 * points - deliberately absent here (undefined behavior with the checks compiled out, and
 * this file never exercises them). Every injection refusal (a name that is not a token, CRLF
 * or ';' in a value, ';'/CTL in Path or Domain, SameSite=None or Partitioned without Secure)
 * is coded as an ordinary runtime branch, never routed through error_check - this build proves
 * each one refuses identically whether ERROR_CHECK_ENABLED is defined or not, per the
 * value-dependent-refusal standard (a data-dependent decision is never an abort primitive).
 *
 * Mid-4 (R2): http_cookie_alloc_init_2's arena-refusal branch (cookie.c:506-516) belongs here
 * too, for the same reason as arena_linear_alloc's abort-on-capacity-overrun contract elsewhere
 * in this tree - a genuinely REJECTED arena handle (byte_size 0) is a plain runtime condition
 * (string_get_data(...) == nullptr on a non-empty source), not an error_check abort path, so it
 * holds identically with the checks compiled out. This is the template test_all.c:9's header
 * comment promised and did not yet deliver.
 */

int main(void) {
    log_init((LogConfig){ .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true });

    Test test = test_init("tests/http/cookie/test_unchecked.c");

    test_suite_begin(&test, "http_cookie (unchecked)");
    test_case_begin(&test, "injection refusals hold identically without ERROR_CHECK_ENABLED");

    HTTP_Cookie bad_name = http_cookie_init_1("bad name", "tok");
    String      r1       = http_cookie_set_header_create(&bad_name);

    test_expect_u(&test, "a name with a space is still refused", 0, string_get_size(&r1));
    string_uninit(&r1);
    http_cookie_uninit(&bad_name);

    HTTP_Cookie crlf_value = http_cookie_init_1("sid", "x\r\nSet-Cookie: admin=1");
    String      r2         = http_cookie_set_header_create(&crlf_value);

    test_expect_u(&test, "CRLF in the value is still refused, not split into a second header", 0, string_get_size(&r2));
    string_uninit(&r2);
    http_cookie_uninit(&crlf_value);

    HTTP_Cookie bad_path = http_cookie_init_2("sid", "tok", "/a;b", HTTP_COOKIE_SAME_SITE_LAX, true, true);
    String      r3       = http_cookie_set_header_create(&bad_path);

    test_expect_u(&test, "';' in Path is still refused", 0, string_get_size(&r3));
    string_uninit(&r3);
    http_cookie_uninit(&bad_path);

    HTTP_Cookie none_insecure = http_cookie_init_2("sid", "tok", "/", HTTP_COOKIE_SAME_SITE_NONE, false, true);
    String      r4            = http_cookie_set_header_create(&none_insecure);

    test_expect_u(&test, "SameSite=None without Secure is still refused", 0, string_get_size(&r4));
    string_uninit(&r4);
    http_cookie_uninit(&none_insecure);

    HTTP_Cookie partitioned_insecure = http_cookie_init_2("sid", "tok", "/", HTTP_COOKIE_SAME_SITE_LAX, false, true);

    http_cookie_partitioned_set(&partitioned_insecure, true);

    String r5 = http_cookie_set_header_create(&partitioned_insecure);

    test_expect_u(&test, "Partitioned without Secure is still refused", 0, string_get_size(&r5));
    string_uninit(&r5);
    http_cookie_uninit(&partitioned_insecure);

    HTTP_Cookie well_formed = http_cookie_init_1("sid", "tok");
    String      r6          = http_cookie_set_header_create(&well_formed);

    test_expect_string(&test, "a well-formed cookie still renders normally", "Set-Cookie: sid=tok; Path=/; Secure; HttpOnly; SameSite=Lax\r\n", string_get_data(&r6));
    string_uninit(&r6);
    http_cookie_uninit(&well_formed);

    String found = http_cookie_get_1("a=1; b=2", "b");

    test_expect_string(&test, "get_1 still scans normally", "2", string_get_data(&found));
    string_uninit(&found);

    test_case_end(&test);

    test_case_begin(&test, "alloc_init_2 refuses whole (Mid-4) on a genuinely refused arena");

    Arena       refused = arena_init_2(0, 8, ARENA_TYPE_LINEAR);
    HTTP_Cookie cookie  = (HTTP_Cookie) DEFAULT_INITIALIZATION;
    bool const  success = http_cookie_alloc_init_1(&cookie, "sid", "tok", &refused);

    test_expect_false(&test, "alloc_init_1 answers false on a refused arena", success);
    test_expect_null(&test, "name carries no data after refusal", (void*) string_get_data(&cookie.name));
    test_expect_null(&test, "value carries no data either - the whole cookie is gone", (void*) string_get_data(&cookie.value));
    test_expect_null(&test, "allocator is cleared - a caller cannot mistake this for an initialized cookie", (void*) cookie.allocator);
    test_expect_false(&test, "secure is zeroed, not left at init_1's default true", cookie.secure);

    arena_uninit(&refused, ARENA_TYPE_LINEAR);

    test_case_end(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}