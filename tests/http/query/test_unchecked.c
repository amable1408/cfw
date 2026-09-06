#include <string.h>

#include <char/char.h>
#include <http/query/query.h>
#include <test/test.h>

/*
 * Behavioral tests for include/http/query/query.c built WITHOUT ERROR_CHECK_ENABLED.
 *
 * error_check_null in this module guards only two NULL-POINTER contracts - a null Str or
 * String key handle to get_3/get_4, and a null out_size to decode_2/form_decode_2 - both
 * undefined behavior with the checks compiled out, and this file never exercises them. Every
 * OTHER refusal (null/empty query or key, a missing key, an empty value, an oversized
 * key_size, a malformed "%" escape, a sized-query bound) is coded as an ordinary runtime
 * branch, never routed through error_check - this build proves each one refuses identically
 * whether ERROR_CHECK_ENABLED is defined or not.
 */

int main(void) {
    log_init((LogConfig){ .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true });

    Test test = test_init("tests/http/query/test_unchecked.c");

    test_suite_begin(&test, "http_query (unchecked)");
    test_case_begin(&test, "value refusals hold identically without ERROR_CHECK_ENABLED");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    test_expect_null(&test, "null query is still refused", http_query_alloc_get_1(nullptr, "a", &arena));
    test_expect_null(&test, "null key is still refused", http_query_alloc_get_1("a=1", nullptr, &arena));
    test_expect_null(&test, "a missing key is still nullptr", http_query_alloc_get_1("a=1", "zzz", &arena));
    test_expect_null(&test, "an empty value is still nullptr", http_query_alloc_get_1("key=", "key", &arena));
    test_expect_string(&test, "a well-formed lookup still works", "1", http_query_alloc_get_1("a=1&b=2", "a", &arena));

    char const key[4] = "cod";

    test_expect_null(&test, "an oversized key_size still fails closed, not a crash", http_query_alloc_get_2("code=1", key, 32, &arena));
    test_expect_null(&test, "an oversized key_size fails closed even on an otherwise-exact query", http_query_alloc_get_2("cod=1", key, 32, &arena));
    test_expect_string(&test, "the correctly-sized key_size (3) still matches normally", "1", http_query_alloc_get_2("cod=1", key, 3, &arena));

    char buffer[16];

    memset(buffer, 'X', sizeof(buffer));
    memcpy(buffer, "a=1&b=2", 7);

    test_expect_string(&test, "get_5's sized-query bound still holds", "2", http_query_alloc_get_5(buffer, 7, "b", 1, &arena));
    test_expect_null(&test, "get_5 still ignores content past query_size", http_query_alloc_get_5(buffer, 7, "X", 1, &arena));

    test_expect_string(&test, "a malformed '%' escape still passes through raw", "%ggrest", http_query_alloc_decode("%ggrest", &arena));
    test_expect_string(&test, "'+' -> space still holds in form_decode", "a b", http_query_alloc_form_decode("a+b", &arena));

    USize out_size = 0;
    char const *const decoded = http_query_alloc_decode_2("a%00b", 5, &out_size, &arena);

    test_expect_not_null(&test, "decode_2 still succeeds", decoded);
    test_expect_u(&test, "the embedded NUL is still counted in out_size", 3, out_size);
    test_expect_u(&test, "char_length still stops at the embedded NUL (the exact truncation out_size guards against)", 1, char_length(decoded));

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}