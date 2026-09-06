#include <string.h>

#include <char/char.h>
#include <container/str/str.h>
#include <container/string/string.h>
#include <http/query/query.h>
#include <log/log.h>
#include <test/test.h>

/*
 * Coverage for http/query: http_query_alloc_get_1..5 (find a key's raw value in a
 * "key=value&..." query string, across the char*, sized, Str, String, and sized-query tiers)
 * and http_query_alloc_decode(_2) / http_query_alloc_form_decode(_2) (percent-decoding, with
 * and without "+" -> space, sized and unsized).
 *
 * Null Str or String handles to _3/_4 and a null out_size to decode_2/form_decode_2 go through
 * error_check_null - deliberately absent here (would abort the process); test_unchecked.c pins
 * that every OTHER refusal in this file is an ordinary runtime branch, not an artifact of
 * ERROR_CHECK_ENABLED.
 */

static void _test_get_1_first_middle_last(Test *const test) {
    test_case_begin(test, "get_1: first, middle, and last key in a query, with and without leading '?'");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    test_expect_string(test, "leading key", "1", http_query_alloc_get_1("a=1&b=2&c=3", "a", &arena));
    test_expect_string(test, "middle key", "2", http_query_alloc_get_1("a=1&b=2&c=3", "b", &arena));
    test_expect_string(test, "trailing key", "3", http_query_alloc_get_1("a=1&b=2&c=3", "c", &arena));
    test_expect_string(test, "leading '?' tolerated", "1", http_query_alloc_get_1("?a=1&b=2", "a", &arena));

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_get_1_duplicates_and_prefix_keys(Test *const test) {
    test_case_begin(test, "get_1: duplicate keys (first wins), and a key that prefixes another");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    test_expect_string(test, "duplicate key answers the first occurrence", "1", http_query_alloc_get_1("code=1&code=2", "code", &arena));
    test_expect_string(test, "\"code\" does not false-match inside \"code2\"'s pair", "2", http_query_alloc_get_1("code2=1&code=2", "code", &arena));
    test_expect_null(test, "\"code2\" is not found when only \"code\" is present", http_query_alloc_get_1("code=1", "code2", &arena));

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_get_1_empty_value_and_missing_key(Test *const test) {
    test_case_begin(test, "get_1: an empty value and a missing key both answer nullptr");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    test_expect_null(test, "\"key=\" (empty value) is absent", http_query_alloc_get_1("key=&other=1", "key", &arena));
    test_expect_null(test, "a key not present in the query is absent", http_query_alloc_get_1("a=1", "zzz", &arena));
    test_expect_string(test, "a sibling key is unaffected by a neighbouring empty value", "1", http_query_alloc_get_1("key=&other=1", "other", &arena));

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_get_null_and_empty_arguments(Test *const test) {
    test_case_begin(test, "get_1/_2: null/empty query or key refuse without aborting");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    test_expect_null(test, "null query refuses", http_query_alloc_get_1(nullptr, "a", &arena));
    test_expect_null(test, "empty query refuses", http_query_alloc_get_1("", "a", &arena));
    test_expect_null(test, "null key refuses", http_query_alloc_get_1("a=1", nullptr, &arena));
    test_expect_null(test, "empty (non-null) key refuses via key_size 0", http_query_alloc_get_2("a=1", "", 0, &arena));

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_get_2_oversized_key_size_bounded(Test *const test) {
    test_case_begin(test, "get_2: a key_size larger than the key's real extent is bounded, never read past, and fails closed");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    // "cod" is 3 real bytes; key_size 32 claims far more. The comparison stops at "cod"'s own
    // NUL (Mid 11's fix) rather than reading past it - which means the claimed key_size can
    // never be satisfied, so a caller-lied key_size fails closed instead of risking a wrong
    // match on whatever garbage would otherwise follow "cod" in its buffer.
    char const key[4] = "cod";

    test_expect_null(test, "an oversized key_size fails even an otherwise-mismatching query", http_query_alloc_get_2("code=1", key, 32, &arena));
    test_expect_null(test, "an oversized key_size fails even an otherwise-exact query (fail closed, never a false match)", http_query_alloc_get_2("cod=1", key, 32, &arena));
    test_expect_string(test, "the correctly-sized key_size (3) still matches normally", "1", http_query_alloc_get_2("cod=1", key, 3, &arena));

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_get_3_str_and_get_4_string(Test *const test) {
    test_case_begin(test, "get_3 (Str) and get_4 (String) key tiers");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    Str const key_str = str_init_2((char*) "b");

    test_expect_string(test, "get_3 finds by Str key", "2", http_query_alloc_get_3("a=1&b=2", &key_str, &arena));

    String key_string = string_init_1();

    string_add_last_1(&key_string, (char*) "c");

    test_expect_string(test, "get_4 finds by String key", "3", http_query_alloc_get_4("a=1&b=2&c=3", &key_string, &arena));

    string_uninit(&key_string);
    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_get_5_sized_query_ignores_trailing_garbage(Test *const test) {
    test_case_begin(test, "get_5: a sized, non-NUL-terminated query buffer is bounded by query_size alone");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    // Mirrors a fixed-capacity buffer such as http_server_request_query_copy's: filled past
    // its real content with non-NUL bytes, so any bound on '\0' rather than query_size would
    // walk into the garbage.
    char buffer[16];

    memset(buffer, 'X', sizeof(buffer));
    memcpy(buffer, "a=1&b=2", 7);

    test_expect_string(test, "finds a key within the real content", "2", http_query_alloc_get_5(buffer, 7, "b", 1, &arena));
    test_expect_null(test, "a key that only exists in the trailing garbage is not found", http_query_alloc_get_5(buffer, 7, "X", 1, &arena));

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_decode_basic_and_case(Test *const test) {
    test_case_begin(test, "decode: %XX in both hex cases, no '+' handling");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    test_expect_string(test, "%20 decodes to a space", "abc def", http_query_alloc_decode("abc%20def", &arena));
    test_expect_string(test, "lowercase and uppercase hex both decode '/'", "a/b/c", http_query_alloc_decode("a%2fb%2Fc", &arena));
    test_expect_string(test, "'+' is left untouched (no form convention here)", "a+b", http_query_alloc_decode("a+b", &arena));
    test_expect_string(test, "decoding \"%2B\" yields a literal '+'", "a+b", http_query_alloc_decode("a%2Bb", &arena));

    char const *const empty = http_query_alloc_decode("", &arena);

    test_expect_not_null(test, "decoding an empty (non-null) string succeeds", empty);
    test_expect_u(test, "and is itself empty", 0, char_length(empty));
    test_expect_null(test, "decoding a null pointer refuses", http_query_alloc_decode(nullptr, &arena));

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_decode_malformed_percent_passes_through(Test *const test) {
    test_case_begin(test, "decode: a malformed '%' escape passes through as literal bytes");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    test_expect_string(test, "non-hex digits after '%' pass through raw", "%ggrest", http_query_alloc_decode("%ggrest", &arena));
    test_expect_string(test, "a trailing '%' with too few bytes left passes through raw", "abc%4", http_query_alloc_decode("abc%4", &arena));
    test_expect_string(test, "a valid escape at the exact end still decodes", "abA", http_query_alloc_decode("ab%41", &arena));

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_decode_2_reports_size_past_embedded_nul(Test *const test) {
    test_case_begin(test, "decode_2: an embedded \"%00\" is not silently truncated - out_size proves it");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    USize out_size = 0;
    char const *const decoded = http_query_alloc_decode_2("a%00b", 5, &out_size, &arena);

    test_expect_not_null(test, "decode_2 succeeds", decoded);
    test_expect_u(test, "3 real bytes were decoded ('a', NUL, 'b')", 3, out_size);
    test_expect_u(test, "char_length only sees up to the embedded NUL (the truncation decode_1 cannot expose)", 1, char_length(decoded));
    test_expect_true(test, "the byte after the embedded NUL is still there", decoded[2] == 'b');

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_form_decode_plus_to_space(Test *const test) {
    test_case_begin(test, "form_decode: '+' -> space, alongside ordinary %XX decoding");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    test_expect_string(test, "'+' becomes a space", "a b", http_query_alloc_form_decode("a+b", &arena));
    test_expect_string(test, "%2B still yields a literal '+' (not re-spaced)", "a+b", http_query_alloc_form_decode("a%2Bb", &arena));
    test_expect_string(test, "'+' and %20 together", "a b c", http_query_alloc_form_decode("a+b%20c", &arena));

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_form_decode_2_sized(Test *const test) {
    test_case_begin(test, "form_decode_2: sized form-decoding with an embedded NUL");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    USize out_size = 0;
    char const *const decoded = http_query_alloc_form_decode_2("a+%00+b", 7, &out_size, &arena);

    test_expect_not_null(test, "form_decode_2 succeeds", decoded);
    test_expect_u(test, "5 decoded bytes ('a', ' ', NUL, ' ', 'b')", 5, out_size);
    test_expect_true(test, "'+' decoded to space either side of the embedded NUL", decoded[1] == ' ' && decoded[3] == ' ');
    test_expect_true(test, "the trailing 'b' survives past the embedded NUL", decoded[4] == 'b');

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Entry Point
 *============================================================================*/

int main(void) {
    log_init((LogConfig){ .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true });

    Test test = test_init("tests/http/query/test_all.c");

    test_suite_begin(&test, "http_query");
    _test_get_1_first_middle_last(&test);
    _test_get_1_duplicates_and_prefix_keys(&test);
    _test_get_1_empty_value_and_missing_key(&test);
    _test_get_null_and_empty_arguments(&test);
    _test_get_2_oversized_key_size_bounded(&test);
    _test_get_3_str_and_get_4_string(&test);
    _test_get_5_sized_query_ignores_trailing_garbage(&test);
    _test_decode_basic_and_case(&test);
    _test_decode_malformed_percent_passes_through(&test);
    _test_decode_2_reports_size_past_embedded_nul(&test);
    _test_form_decode_plus_to_space(&test);
    _test_form_decode_2_sized(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}