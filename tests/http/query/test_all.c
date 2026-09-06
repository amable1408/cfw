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
 * and without "+" -> space, sized and unsized), plus the writing side added in 0.2.0 -
 * http_query_encode_1/_2 (RFC 3986, space -> "%20"), http_query_add_1/_2 (0.3.0: the
 * query-string pair builder, space -> "%20") and http_query_form_add_1/_2 (the
 * x-www-form-urlencoded body builder, space -> "+").
 *
 * The writing side takes its destination String FIRST as of 0.3.0 (encode's `out` moved from
 * last to first, matching form_add and string_add_last_*); every call below is in that order.
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

static void _test_encode_reserved_set_byte_exact(Test *const test) {
    test_case_begin(test, "encode_1: RFC 3986 unreserved pass through, everything else is uppercase %XX, space is %20");

    // "a b&c=d/e-acute": one byte from each class - unreserved, space, sub-delim, '=', '/',
    // and a two-byte UTF-8 sequence written as escapes so the pin does not depend on this
    // file's own source encoding. 0xC3 0xA9 is U+00E9.
    char const *const value = "a b&c=d/\xC3\xA9";
    String out = string_init_1();

    test_expect_true(test, "encode_1 accepts the value", http_query_encode_1(&out, value));
    test_expect_string(test, "space is %20 and never '+', hex digits uppercase", "a%20b%26c%3Dd%2F%C3%A9", string_get_data(&out));

    String unreserved = string_init_1();

    test_expect_true(test, "encode_1 accepts the unreserved set", http_query_encode_1(&unreserved, "AZaz09-._~"));
    test_expect_string(test, "the whole unreserved set passes through untouched", "AZaz09-._~", string_get_data(&unreserved));

    string_uninit(&unreserved);
    string_uninit(&out);

    test_case_end(test);
}

static void _test_encode_appends_and_sized_twin(Test *const test) {
    test_case_begin(test, "encode_1/_2: encoding APPENDS to out, and _2 encodes past an embedded NUL");

    String out = string_init_1();

    test_expect_true(test, "first encode succeeds", http_query_encode_1(&out, "a b"));
    test_expect_true(test, "second encode succeeds", http_query_encode_1(&out, "c/d"));
    test_expect_string(test, "the second encoding is appended, not substituted", "a%20b" "c%2Fd", string_get_data(&out));

    String sized = string_init_1();

    test_expect_true(test, "encode_2 accepts a sized value", http_query_encode_2(&sized, "a\0b", 3));
    test_expect_string(test, "an embedded NUL encodes as %00 instead of cutting the value short", "a%00b", string_get_data(&sized));

    string_uninit(&sized);
    string_uninit(&out);

    test_case_end(test);
}

static void _test_encode_empty_value_and_refusals(Test *const test) {
    test_case_begin(test, "encode_1/_2: an empty value is a legal VALUE; null and oversize are refusals, never aborts");

    String out = string_init_1();

    test_expect_true(test, "an empty value answers true", http_query_encode_1(&out, ""));
    test_expect_true(test, "an empty value appends nothing", string_empty(&out));
    test_expect_true(test, "a zero-size, null value answers true", http_query_encode_2(&out, nullptr, 0));
    test_expect_true(test, "a zero-size, null value appends nothing", string_empty(&out));

    test_expect_true(test, "seed the buffer for the untouched-on-refusal pins", http_query_encode_1(&out, "seed"));

    test_expect_false(test, "a null value is refused", http_query_encode_1(&out, nullptr));
    test_expect_false(test, "a null out is refused", http_query_encode_1(nullptr, "x"));
    test_expect_false(test, "a null value over a non-zero size is refused", http_query_encode_2(&out, nullptr, 4));

    // The cap is checked BEFORE a single byte is read, so passing an over-cap size against a
    // one-byte buffer is safe here - and is exactly what proves the order.
    test_expect_false(test, "a value over HTTP_QUERY_ENCODE_MAX_SIZE is refused before it is read", http_query_encode_2(&out, "x", (USize) HTTP_QUERY_ENCODE_MAX_SIZE + 1));

    // A source inside the destination is refused whole: growth would free the bytes the
    // encoder is still reading, and the alias window is the whole capacity, not the size.
    test_expect_false(test, "a value aliasing out's own buffer is refused", http_query_encode_1(&out, string_get_data(&out)));
    test_expect_false(test, "a sized value aliasing out's own buffer is refused", http_query_encode_2(&out, string_get_data(&out) + 1, 2));
    test_expect_false(test, "a form name aliasing the body is refused", http_query_form_add_1(&out, string_get_data(&out), "1"));
    test_expect_false(test, "a form value aliasing the body is refused", http_query_form_add_1(&out, "a", string_get_data(&out)));
    test_expect_string(test, "every refusal left out untouched", "seed", string_get_data(&out));

    string_uninit(&out);

    test_case_end(test);
}

static void _test_encode_zero_size_never_consults_the_alias_guard(Test *const test) {
    test_case_begin(test, "encode_2: a ZERO-SIZE slice answers true even when it points into out");

    String out = string_init_1();

    test_expect_true(test, "seed the buffer so it has an allocation to alias", http_query_encode_1(&out, "seed"));

    // A slice that reads no bytes cannot dangle when the destination grows, so the alias
    // test never runs on it - the header promises value_size == 0 answers true, full stop.
    test_expect_true(test, "a zero-size slice pointing at out's own buffer answers true", http_query_encode_2(&out, string_get_data(&out), 0));
    test_expect_true(test, "a zero-size slice one byte into out's own buffer answers true", http_query_encode_2(&out, string_get_data(&out) + 1, 0));
    test_expect_true(test, "a zero-size name side is still refused - a nameless pair is not a field", !http_query_form_add_2(&out, string_get_data(&out), 0, "1", 1));
    test_expect_true(test, "a zero-size value side aliasing the body answers true", http_query_form_add_2(&out, "a", 1, string_get_data(&out), 0));
    test_expect_string(test, "nothing was appended by any zero-size encode; only the pair was", "seed&a=", string_get_data(&out));

    string_uninit(&out);

    test_case_end(test);
}

static void _test_encode_alias_guard_is_an_interval(Test *const test) {
    test_case_begin(test, "encode_2: the alias guard is an INTERVAL overlap, not a start-pointer test");

    /* One array object, so every comparison below is defined rather than a comparison of
     * unrelated pointers: `out` is a VIEW over the middle of `buffer` (string_init_4 borrows,
     * it does not copy), and the slice starts BEFORE that view and runs into it. A guard that
     * only asks "does value start inside out" reads value < data and lets this through. */
    char buffer[128] = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < sizeof(buffer); i += 1) {
        buffer[i] = 'a';
    }

    String out = string_init_4(&buffer[64], 8);

    test_expect_false(test, "a slice starting before out but running into it is refused", http_query_encode_2(&out, &buffer[60], 8));
    test_expect_false(test, "a slice starting inside out is refused", http_query_encode_2(&out, &buffer[70], 2));
    test_expect_false(test, "a slice spanning out entirely is refused", http_query_encode_2(&out, &buffer[0], 128));
    test_expect_true(test, "a slice entirely before out, sharing no byte with it, is accepted", http_query_encode_2(&out, &buffer[0], 4));

    string_uninit(&out);

    test_case_end(test);
}

static void _test_add_chained_query_string(Test *const test) {
    test_case_begin(test, "add_1/_2: chained pairs build a byte-exact query string, '&'-joined, %20 flavour");

    String url = string_init_1();

    test_expect_true(test, "first pair added", http_query_add_1(&url, "q", "a b"));
    test_expect_string(test, "the first pair carries no leading '&' and a space is %20", "q=a%20b", string_get_data(&url));
    test_expect_true(test, "second pair added", http_query_add_1(&url, "next", "x&y"));
    test_expect_string(test, "pairs are '&'-joined and the value's own '&' is escaped", "q=a%20b&next=x%26y", string_get_data(&url));

    // Seeded with a URL that already carries a pair: the seed ends in a value byte, so the
    // next pair is joined onto it with a '&' - which is what a URL needs after its first pair.
    String seeded = string_init_1();

    string_add_last_1(&seeded, "https://example.com/search?lang=en");

    test_expect_true(test, "a pair appended after a seeded URL", http_query_add_1(&seeded, "q", "a b"));
    test_expect_string(test, "the seed is kept and the pair joined onto it", "https://example.com/search?lang=en&q=a%20b", string_get_data(&seeded));

    /* The separator joins one pair to the NEXT one, so a destination that ends where a pair
     * has not yet started takes none: a seed stopping at the '?' (the header's own Usage
     * Example) used to spell "?&q=a%20b", and a caller-written '&' used to be doubled. */
    String opened = string_init_1();

    string_add_last_1(&opened, "https://example.com/search?");

    test_expect_true(test, "a pair appended straight after the '?'", http_query_add_1(&opened, "q", "a b"));
    test_expect_string(test, "the first pair follows the '?' with no '&' wedged in", "https://example.com/search?q=a%20b", string_get_data(&opened));
    test_expect_true(test, "and the second pair still joins with '&'", http_query_add_1(&opened, "page", "2"));
    test_expect_string(test, "so the published Usage Example is what the code produces", "https://example.com/search?q=a%20b&page=2", string_get_data(&opened));

    String separated = string_init_1();

    string_add_last_1(&separated, "a=1&");

    test_expect_true(test, "a pair appended after a caller-written '&'", http_query_add_1(&separated, "b", "2"));
    test_expect_string(test, "the caller's separator is used, not doubled", "a=1&b=2", string_get_data(&separated));

    String form_opened = string_init_1();

    string_add_last_1(&form_opened, "seed&");

    test_expect_true(test, "form_add follows the same separator rule", http_query_form_add_1(&form_opened, "a", "b c"));
    test_expect_string(test, "no doubled '&' in a body either, and the flavour is still '+'", "seed&a=b+c", string_get_data(&form_opened));

    String sized = string_init_1();

    test_expect_true(test, "add_2 accepts sized, unterminated sides", http_query_add_2(&sized, "keyXX", 3, "a b", 3));
    test_expect_string(test, "both sides are bounded by their own size and use %20", "key=a%20b", string_get_data(&sized));

    String empty_value = string_init_1();

    test_expect_true(test, "an empty value is a legal VALUE, not a refusal", http_query_add_1(&empty_value, "name", ""));
    test_expect_string(test, "an empty value spells a present-but-empty parameter", "name=", string_get_data(&empty_value));

    string_uninit(&empty_value);
    string_uninit(&sized);
    string_uninit(&form_opened);
    string_uninit(&separated);
    string_uninit(&opened);
    string_uninit(&seeded);
    string_uninit(&url);

    test_case_end(test);
}

static void _test_add_refused_allocator_writes_nothing(Test *const test) {
    test_case_begin(test, "add/form_add: a destination that cannot grow is refused WHOLE, never half-written");

    /* A null-handler arena, not an exhausted one: arena_init_2 rejects the geometry and
     * leaves the hooks live with a null handler, which allocator_borrow answers with a plain
     * nullptr in EVERY build. An exhausted arena would take allocator_borrow's aborting
     * path instead, which a checked build cannot survive long enough to assert on. */
    Arena refused = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    String out = string_init_optional(&refused);

    test_expect_u(test, "the arena-backed destination starts with no capacity at all", 0, string_get_capacity(&out));
    test_expect_false(test, "add refuses rather than appending what fits", http_query_add_1(&out, "name", "value"));
    test_expect_u(test, "and NOTHING was written - not the name, not the '='", 0, string_get_size(&out));

    String body = string_init_optional(&refused);

    test_expect_false(test, "form_add refuses the same way", http_query_form_add_1(&body, "name", "value"));
    test_expect_u(test, "with the body untouched", 0, string_get_size(&body));

    String encoded = string_init_optional(&refused);

    test_expect_false(test, "encode_2 refuses a value it cannot hold whole", http_query_encode_2(&encoded, "a b", 3));
    test_expect_u(test, "leaving the destination empty", 0, string_get_size(&encoded));
    test_expect_true(test, "an EMPTY value is still a legal value, even on a refused allocator", http_query_encode_2(&encoded, "", 0));

    string_uninit(&encoded);
    string_uninit(&body);
    string_uninit(&out);
    arena_uninit(&refused, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_add_refusals_and_flavour_split(Test *const test) {
    test_case_begin(test, "add_1/_2: same refusals as form_add, and the two differ ONLY in the space byte");

    String out = string_init_1();

    test_expect_true(test, "seed the query string", http_query_add_1(&out, "a", "1"));

    test_expect_false(test, "a null out is refused", http_query_add_1(nullptr, "a", "1"));
    test_expect_false(test, "a null name is refused", http_query_add_1(&out, nullptr, "1"));
    test_expect_false(test, "a null value is refused", http_query_add_1(&out, "a", nullptr));
    test_expect_false(test, "an empty name is refused", http_query_add_1(&out, "", "1"));
    test_expect_false(test, "a zero name_size is refused", http_query_add_2(&out, "a", 0, "1", 1));
    test_expect_false(test, "an over-cap name is refused", http_query_add_2(&out, "a", (USize) HTTP_QUERY_ENCODE_MAX_SIZE + 1, "1", 1));
    test_expect_false(test, "an over-cap value is refused", http_query_add_2(&out, "a", 1, "1", (USize) HTTP_QUERY_ENCODE_MAX_SIZE + 1));
    test_expect_false(test, "a name aliasing out is refused", http_query_add_1(&out, string_get_data(&out), "1"));
    test_expect_false(test, "a value aliasing out is refused", http_query_add_1(&out, "a", string_get_data(&out)));
    test_expect_string(test, "no refusal left a half-written pair behind", "a=1", string_get_data(&out));

    // The whole difference between the two builders, pinned side by side: same input, same
    // output but for the space. Everything else - '&', '=', '/', UTF-8 - encodes identically.
    String query = string_init_1();
    String form  = string_init_1();

    test_expect_true(test, "the query flavour accepts the pair", http_query_add_1(&query, "a b", "a b&c=d/\xC3\xA9"));
    test_expect_true(test, "the form flavour accepts the same pair", http_query_form_add_1(&form, "a b", "a b&c=d/\xC3\xA9"));
    test_expect_string(test, "the query flavour writes %20", "a%20b=a%20b%26c%3Dd%2F%C3%A9", string_get_data(&query));
    test_expect_string(test, "the form flavour writes '+'", "a+b=a+b%26c%3Dd%2F%C3%A9", string_get_data(&form));

    string_uninit(&form);
    string_uninit(&query);
    string_uninit(&out);

    test_case_end(test);
}

static void _test_encode_chunk_boundary_byte_exact(Test *const test) {
    test_case_begin(test, "encode_2: a value far longer than the staging chunk encodes byte for byte");

    /* The encoder stages output in a 192-byte stack chunk and flushes it when full. This
     * value is long enough to cross that boundary many times, and every byte encodes to three
     * so a flush lands mid-value rather than tidily at its end. */
    char        source[512]   = DEFAULT_INITIALIZATION;
    char        expected[1537] = DEFAULT_INITIALIZATION;
    String      out            = string_init_1();

    for (USize i = 0; i < sizeof(source); i += 1) {
        source[i] = (char) 0xFF;
        expected[i * 3]     = '%';
        expected[i * 3 + 1] = 'F';
        expected[i * 3 + 2] = 'F';
    }

    test_expect_true(test, "the long value is accepted", http_query_encode_2(&out, source, sizeof(source)));
    test_expect_u(test, "every input byte produced exactly three output bytes", sizeof(source) * 3, string_get_size(&out));
    test_expect_string(test, "the chunked flush produces the same bytes a per-byte append would", expected, string_get_data(&out));

    string_uninit(&out);

    test_case_end(test);
}

static void _test_form_add_chained_body(Test *const test) {
    test_case_begin(test, "form_add_1: chained pairs build a byte-exact x-www-form-urlencoded body");

    String body = string_init_1();

    test_expect_true(test, "first pair added", http_query_form_add_1(&body, "secret", "s"));
    test_expect_string(test, "the first pair carries no leading '&'", "secret=s", string_get_data(&body));
    test_expect_true(test, "second pair added", http_query_form_add_1(&body, "response", "tok"));
    test_expect_true(test, "third pair added", http_query_form_add_1(&body, "remoteip", "1.2.3.4"));
    test_expect_string(test, "siteverify body byte-exact, field order preserved", "secret=s&response=tok&remoteip=1.2.3.4", string_get_data(&body));

    string_uninit(&body);

    test_case_end(test);
}

static void _test_form_add_encodes_both_sides(Test *const test) {
    test_case_begin(test, "form_add_1/_2: both sides encoded with the form convention (' ' -> '+'), empty value spells \"name=\"");

    String body = string_init_1();

    test_expect_true(test, "a pair with reserved bytes on both sides is added", http_query_form_add_1(&body, "a b", "a b&c=d/\xC3\xA9"));
    test_expect_string(test, "space is '+' on both sides; everything else matches the plain flavour", "a+b=a+b%26c%3Dd%2F%C3%A9", string_get_data(&body));

    String empty_value = string_init_1();

    test_expect_true(test, "an empty value is a legal VALUE, not a refusal", http_query_form_add_1(&empty_value, "name", ""));
    test_expect_string(test, "an empty value spells a present-but-empty field", "name=", string_get_data(&empty_value));

    String sized = string_init_1();

    test_expect_true(test, "form_add_2 accepts sized, unterminated sides", http_query_form_add_2(&sized, "keyXX", 3, "a b", 3));
    test_expect_string(test, "both sides are bounded by their own size", "key=a+b", string_get_data(&sized));

    string_uninit(&sized);
    string_uninit(&empty_value);
    string_uninit(&body);

    test_case_end(test);
}

static void _test_form_add_refusals(Test *const test) {
    test_case_begin(test, "form_add_1/_2: null, empty-name and oversize are refusals that leave the body untouched");

    String body = string_init_1();

    test_expect_true(test, "seed the body", http_query_form_add_1(&body, "a", "1"));

    test_expect_false(test, "a null body is refused", http_query_form_add_1(nullptr, "a", "1"));
    test_expect_false(test, "a null name is refused", http_query_form_add_1(&body, nullptr, "1"));
    test_expect_false(test, "a null value is refused", http_query_form_add_1(&body, "a", nullptr));
    test_expect_false(test, "an empty name is refused", http_query_form_add_1(&body, "", "1"));
    test_expect_false(test, "a zero name_size is refused", http_query_form_add_2(&body, "a", 0, "1", 1));
    test_expect_false(test, "an over-cap name is refused", http_query_form_add_2(&body, "a", (USize) HTTP_QUERY_ENCODE_MAX_SIZE + 1, "1", 1));
    test_expect_false(test, "an over-cap value is refused", http_query_form_add_2(&body, "a", 1, "1", (USize) HTTP_QUERY_ENCODE_MAX_SIZE + 1));
    test_expect_string(test, "no refusal left a half-written pair behind", "a=1", string_get_data(&body));

    string_uninit(&body);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Entry Point
 *============================================================================*/

int main(void) {
    LogConfig const log_config = { .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true };

    log_init(log_config);

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
    _test_encode_reserved_set_byte_exact(&test);
    _test_encode_appends_and_sized_twin(&test);
    _test_encode_empty_value_and_refusals(&test);
    _test_encode_zero_size_never_consults_the_alias_guard(&test);
    _test_encode_alias_guard_is_an_interval(&test);
    _test_encode_chunk_boundary_byte_exact(&test);
    _test_add_chained_query_string(&test);
    _test_add_refused_allocator_writes_nothing(&test);
    _test_add_refusals_and_flavour_split(&test);
    _test_form_add_chained_body(&test);
    _test_form_add_encodes_both_sides(&test);
    _test_form_add_refusals(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}