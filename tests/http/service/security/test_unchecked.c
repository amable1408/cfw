#include <arena/arena.h>
#include <http/service/security/security.h>
#include <log/log.h>
#include <test/test.h>

/*
 * Behavioral tests for include/http/service/security/security.c built WITHOUT
 * ERROR_CHECK_ENABLED.
 *
 * error_check_null in this module guards only null-pointer contracts on the public entry
 * points - deliberately absent here (undefined behavior with the checks compiled out, and this
 * file never exercises them). The cache-rendering behavior added in R1 (report High 5) - render
 * once at construction, apply/uninit read the cache as an ordinary String rather than through an
 * error_check gate - holds identically with the checks compiled out, per the value-dependent-
 * refusal standard.
 */

int main(void) {
    LogConfig const log_config = {
        .level             = LOG_LEVEL_ERROR,
        .stream            = stdout,
        .timestamp_enabled = true,
        .autoflush         = true
    };

    log_init(log_config);

    Test test = test_init("tests/http/service/security/test_unchecked.c");

    test_suite_begin(&test, "http_service_security (unchecked)");
    test_case_begin(&test, "the header block is rendered once and reads empty after uninit");

    HTTP_Service_Security security = http_service_security_init_1();

    test_expect_false(&test, "init_1 renders a non-empty cache", string_empty(&security.header_block));

    http_service_security_uninit(&security);

    test_expect_true(&test, "uninit leaves the cache reading empty, not dangling", string_empty(&security.header_block));

    test_case_end(&test);

#ifdef ARENA_IMPLEMENTATION
    test_case_begin(&test, "alloc_init_1 refuses the whole service and zeroes self on a refused arena");

    Arena refused = arena_init_2(0, 8, ARENA_TYPE_LINEAR);
    HTTP_Service_Security security2 = DEFAULT_INITIALIZATION;

    bool const initialized = http_service_security_alloc_init_1(&security2, &refused);

    test_expect_false(&test, "alloc_init_1 answers false on a refused arena", initialized);
    test_expect_null(&test, "allocator is cleared - a caller cannot mistake this for an initialized service", (void*) security2.allocator);
    test_expect_true(&test, "header_block reads empty, not a partial render", string_empty(&security2.header_block));

    arena_uninit(&refused, ARENA_TYPE_LINEAR);

    test_case_end(&test);

    test_case_begin(&test, "alloc_init_1 refuses when the arena admits the policy but not the rendered block");

    /* 168 bytes: fits the four default policy value strings (the policy alone succeeds starting
     * at 160 bytes) but starves the header_block render that follows (which needs 512+ bytes to
     * complete) - probed empirically against http_headers_security_create_2, landing mid-way
     * through the [160, 175] window where the block comes back silently empty rather than
     * tripping the separate malformed-CRLF refusal that starts at 192. */
    Arena starved = arena_init_1(168, ARENA_TYPE_LINEAR);
    HTTP_Service_Security security3 = DEFAULT_INITIALIZATION;

    bool const initialized3 = http_service_security_alloc_init_1(&security3, &starved);

    test_expect_false(&test, "alloc_init_1 answers false when the block render starves", initialized3);
    test_expect_null(&test, "allocator is cleared - a caller cannot mistake this for an initialized service", (void*) security3.allocator);
    test_expect_true(&test, "header_block reads empty, not a partial render", string_empty(&security3.header_block));
    test_expect_true(&test, "policy is uninitialized, not left dangling from the refused block", string_empty(&security3.policy.content_security_policy));

    arena_uninit(&starved, ARENA_TYPE_LINEAR);

    test_case_end(&test);
#endif // ARENA_IMPLEMENTATION

    test_suite_end(&test);

    return test_uninit(&test);
}