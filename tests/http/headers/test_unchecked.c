#include <arena/arena.h>
#include <http/headers/headers.h>
#include <test/test.h>

/*
 * Behavioral test for http/headers/headers.c built WITHOUT ERROR_CHECK_ENABLED.
 *
 * High 4: http_headers_security_alloc_init_1/_2 refuse the WHOLE policy when the arena itself
 * is refused (arena_init_2(0, 8, ...) - every borrow answers EMPTY), rather than silently
 * building a policy that is missing whichever header happened to lose the race. This is a
 * plain runtime `if` over string_get_data(...) == nullptr, not an error_check_* abort path, so
 * it holds identically with the checks compiled out - this build proves that.
 */

int main(void) {
    log_init((LogConfig){ .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true });

    Test test = test_init("tests/http/headers/test_unchecked.c");

    test_suite_begin(&test, "http/headers (unchecked)");
    test_case_begin(&test, "security: a starved arena refuses the WHOLE policy (High 4)");

    Arena refused = arena_init_2(0, 8, ARENA_TYPE_LINEAR);
    HTTP_Headers_Security policy = DEFAULT_INITIALIZATION;

    bool const initialized = http_headers_security_alloc_init_1(&policy, &refused);

    test_expect_false(&test, "alloc_init_1 answers false on a refused arena", initialized);
    test_expect_null(&test, "content_security_policy carries no data after refusal", (void*) string_get_data(&policy.content_security_policy));
    test_expect_false(&test, "content_type_options is zeroed, not left at the default", policy.content_type_options);
    test_expect_false(&test, "cross_origin_opener_policy is zeroed, not left at the default", policy.cross_origin_opener_policy);
    test_expect_u(&test, "strict_transport_security_max_age is zeroed", 0, policy.strict_transport_security_max_age);
    test_expect_null(&test, "allocator is cleared - a caller cannot mistake this for an initialized policy", (void*) policy.allocator);

    arena_uninit(&refused, ARENA_TYPE_LINEAR);

    test_case_end(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}