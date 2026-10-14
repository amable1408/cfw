#include <test/test.h>

#include <http/service/body_parser/body_parser.h>

/*
 * Happy-path and contract suite for http_service_body_parser: form parse, JSON
 * passthrough, the "a&b=c" pair-boundary regression, the empty-body refusal, empty
 * values, duplicate keys, Content-Type token matching (case + parameters), max_pairs,
 * and reset-on-reparse. The allocation-failure sweep lives in test_oom.c, built from
 * the same makefile.
 */

static void _test_form_basic(Test *const test) {
    test_case_begin(test, "form: multiple pairs, + and %XX decode");

    char const *const body = "name=John+Doe&city=New%20York&empty_ok=1";

    HTTP_Service_Body_Parser parser = http_service_body_parser_init();

    bool const parsed = http_service_body_parser_parse(&parser, HTTP_SERVICE_BODY_PARSER_CONTENT_TYPE_FORM, (Byte const*) body, char_length(body));

    test_expect_true(test, "parse succeeds", parsed);
    test_expect_true(test, "is_form set", parser.is_form);
    test_expect_false(test, "is_json not set", parser.is_json);
    test_expect_string(test, "name: + decodes to space", "John Doe", http_service_body_parser_form_get(&parser, "name"));
    test_expect_string(test, "city: %20 decodes to space", "New York", http_service_body_parser_form_get(&parser, "city"));
    test_expect_string(test, "empty_ok value", "1", http_service_body_parser_form_get(&parser, "empty_ok"));
    test_expect_null(test, "missing key is absent", http_service_body_parser_form_get(&parser, "missing"));

    http_service_body_parser_uninit(&parser);

    test_case_end(test);
}

static void _test_form_single_pair_no_trailing_amp(Test *const test) {
    test_case_begin(test, "form: single pair, no trailing '&'");

    char const *const body = "only=value";

    HTTP_Service_Body_Parser parser = http_service_body_parser_init();

    test_expect_true(test, "parse succeeds", http_service_body_parser_parse(&parser, HTTP_SERVICE_BODY_PARSER_CONTENT_TYPE_FORM, (Byte const*) body, char_length(body)));
    test_expect_string(test, "the pair is present", "value", http_service_body_parser_form_get(&parser, "only"));

    http_service_body_parser_uninit(&parser);

    test_case_end(test);
}

static void _test_json_passthrough(Test *const test) {
    test_case_begin(test, "json: raw bytes and size are exposed unparsed");

    char const *const body = "{\"a\":1,\"b\":[2,3]}";
    USize const body_size = char_length(body);

    HTTP_Service_Body_Parser parser = http_service_body_parser_init();

    bool const parsed = http_service_body_parser_parse(&parser, HTTP_SERVICE_BODY_PARSER_CONTENT_TYPE_JSON, (Byte const*) body, body_size);

    test_expect_true(test, "parse succeeds", parsed);
    test_expect_true(test, "is_json set", parser.is_json);
    test_expect_false(test, "is_form not set", parser.is_form);
    test_expect_u(test, "json size matches the payload", body_size, http_service_body_parser_json_get_size(&parser));
    test_expect_not_null(test, "json pointer is set", http_service_body_parser_json_get(&parser));

    if (http_service_body_parser_json_get(&parser) != nullptr) {
        Byte const *const json = http_service_body_parser_json_get(&parser);
        bool matches = true;

        for (USize i = 0; i < body_size; i += 1) {
            matches = matches && json[i] == (Byte) body[i];
        }

        test_expect_true(test, "json bytes are unmodified", matches);
    }

    test_expect_null(test, "form_get on a json body is null (not a form)", http_service_body_parser_form_get(&parser, "a"));

    http_service_body_parser_uninit(&parser);

    test_case_end(test);
}

static void _test_unknown_content_type(Test *const test) {
    test_case_begin(test, "unknown Content-Type: parse fails, nothing set");

    char const *const body = "irrelevant";

    HTTP_Service_Body_Parser parser = http_service_body_parser_init();

    bool const parsed = http_service_body_parser_parse(&parser, "text/plain", (Byte const*) body, char_length(body));

    test_expect_false(test, "parse reports failure", parsed);
    test_expect_false(test, "is_form not set", parser.is_form);
    test_expect_false(test, "is_json not set", parser.is_json);

    http_service_body_parser_uninit(&parser);

    test_case_end(test);
}

static void _test_regression_pair_with_no_equals_before_a_later_one(Test *const test) {
    test_case_begin(test, "regression: \"a&b=c\" - a bare key does not swallow the next pair's value");

    char const *const body = "a&b=c";

    HTTP_Service_Body_Parser parser = http_service_body_parser_init();

    bool const parsed = http_service_body_parser_parse(&parser, HTTP_SERVICE_BODY_PARSER_CONTENT_TYPE_FORM, (Byte const*) body, char_length(body));

    test_expect_true(test, "parse succeeds", parsed);
    test_expect_null(test, "'a' has no '=' in its own pair, so it is skipped", http_service_body_parser_form_get(&parser, "a"));
    test_expect_string(test, "'b' keeps its own value, not 'c' stolen from beyond the boundary", "c", http_service_body_parser_form_get(&parser, "b"));

    http_service_body_parser_uninit(&parser);

    test_case_end(test);
}

static void _test_regression_empty_value(Test *const test) {
    test_case_begin(test, "\"k=\" - an empty value is stored as \"\" (present, not absent), http_query_decode(size=0) no longer aborts");

    char const *const body = "k=&next=1";

    HTTP_Service_Body_Parser parser = http_service_body_parser_init();

    bool const parsed = http_service_body_parser_parse(&parser, HTTP_SERVICE_BODY_PARSER_CONTENT_TYPE_FORM, (Byte const*) body, char_length(body));

    test_expect_true(test, "parse succeeds (does not abort)", parsed);
    test_expect_string(test, "'k' is present with an empty value", "", http_service_body_parser_form_get(&parser, "k"));
    test_expect_string(test, "the pair after it still parses", "1", http_service_body_parser_form_get(&parser, "next"));

    http_service_body_parser_uninit(&parser);

    test_case_end(test);
}

static void _test_empty_body_refuses(Test *const test) {
    test_case_begin(test, "empty payload (Content-Length: 0) refuses instead of aborting");

    HTTP_Service_Body_Parser parser = http_service_body_parser_init();

    bool const parsed = http_service_body_parser_parse(&parser, HTTP_SERVICE_BODY_PARSER_CONTENT_TYPE_FORM, (Byte const*) "", 0);

    test_expect_false(test, "parse reports failure", parsed);
    test_expect_false(test, "is_form not set", parser.is_form);
    test_expect_false(test, "is_json not set", parser.is_json);

    http_service_body_parser_uninit(&parser);

    test_case_end(test);
}

static void _test_duplicate_key_first_wins(Test *const test) {
    test_case_begin(test, "\"a=1&a=2\" - a duplicate key keeps the first-parsed value");

    char const *const body = "a=1&a=2";

    HTTP_Service_Body_Parser parser = http_service_body_parser_init();

    test_expect_true(test, "parse succeeds", http_service_body_parser_parse(&parser, HTTP_SERVICE_BODY_PARSER_CONTENT_TYPE_FORM, (Byte const*) body, char_length(body)));
    test_expect_string(test, "the first value wins", "1", http_service_body_parser_form_get(&parser, "a"));

    http_service_body_parser_uninit(&parser);

    test_case_end(test);
}

static void _test_content_type_parameter_and_case(Test *const test) {
    test_case_begin(test, "Content-Type matches its media-type token only, case-insensitively, ignoring \"; charset=...\"");

    char const *const body = "{\"ok\":true}";

    HTTP_Service_Body_Parser parser = http_service_body_parser_init();

    bool const parsed = http_service_body_parser_parse(&parser, "APPLICATION/JSON; charset=utf-8", (Byte const*) body, char_length(body));

    test_expect_true(test, "parse succeeds despite case and a trailing parameter", parsed);
    test_expect_true(test, "is_json set", parser.is_json);

    http_service_body_parser_uninit(&parser);

    test_case_end(test);
}

static void _test_content_type_leading_space_is_trimmed(Test *const test) {
    test_case_begin(test, "a leading space/tab before the media type is trimmed, not just a trailing one before a parameter");

    char const *const body = "{\"ok\":true}";

    HTTP_Service_Body_Parser parser = http_service_body_parser_init();

    /* A caller handing over the raw header VALUE (as opposed to the whole header
     * line) sometimes keeps the space that followed "Content-Type:". */
    bool const parsed = http_service_body_parser_parse(&parser, " application/json", (Byte const*) body, char_length(body));

    test_expect_true(test, "parse succeeds despite the leading space", parsed);
    test_expect_true(test, "is_json set", parser.is_json);

    http_service_body_parser_uninit(&parser);

    test_case_end(test);
}

static void _test_content_type_extra_suffix_does_not_match(Test *const test) {
    test_case_begin(test, "a media type that only STARTS WITH the token does not match (exact token, not substring)");

    char const *const body = "irrelevant";

    HTTP_Service_Body_Parser parser = http_service_body_parser_init();

    bool const parsed = http_service_body_parser_parse(&parser, "application/x-www-form-urlencoded-extra", (Byte const*) body, char_length(body));

    test_expect_false(test, "parse reports failure", parsed);
    test_expect_false(test, "is_form not set", parser.is_form);

    http_service_body_parser_uninit(&parser);

    test_case_end(test);
}

static void _test_parse_reuse_without_uninit_resets_state(Test *const test) {
    test_case_begin(test, "calling parse() again on the SAME instance (no uninit in between) resets prior state");

    HTTP_Service_Body_Parser parser = http_service_body_parser_init();

    char const *const first = "a=1";

    test_expect_true(test, "first parse (form) succeeds", http_service_body_parser_parse(&parser, HTTP_SERVICE_BODY_PARSER_CONTENT_TYPE_FORM, (Byte const*) first, char_length(first)));
    test_expect_string(test, "first parse value present", "1", http_service_body_parser_form_get(&parser, "a"));

    char const *const second = "{\"x\":true}";

    bool const second_parsed = http_service_body_parser_parse(&parser, HTTP_SERVICE_BODY_PARSER_CONTENT_TYPE_JSON, (Byte const*) second, char_length(second));

    test_expect_true(test, "second parse (json), no uninit call between, succeeds", second_parsed);
    test_expect_true(test, "is_json now set", parser.is_json);
    test_expect_false(test, "is_form is no longer set", parser.is_form);
    test_expect_null(test, "the old form key is gone even without an uninit call", http_service_body_parser_form_get(&parser, "a"));

    http_service_body_parser_uninit(&parser);

    test_case_end(test);
}

static void _test_max_pairs_bound(Test *const test) {
    test_case_begin(test, "an explicit max_pairs override refuses a form body once exceeded");

    char const *const body = "a=1&b=2&c=3";

    HTTP_Service_Body_Parser parser = http_service_body_parser_init();

    parser.max_pairs = 2;

    bool const parsed = http_service_body_parser_parse(&parser, HTTP_SERVICE_BODY_PARSER_CONTENT_TYPE_FORM, (Byte const*) body, char_length(body));

    test_expect_false(test, "parse refuses past the budget", parsed);
    test_expect_string(test, "pairs accepted before the budget was hit remain", "1", http_service_body_parser_form_get(&parser, "a"));
    test_expect_string(test, "pairs accepted before the budget was hit remain", "2", http_service_body_parser_form_get(&parser, "b"));
    test_expect_null(test, "the pair past the budget was never added", http_service_body_parser_form_get(&parser, "c"));

    http_service_body_parser_uninit(&parser);

    test_case_end(test);
}

/* Builds "k0=0&k1=1&...&k{pair_count-1}={pair_count-1}" into buffer, truncating (rather
 * than overflowing) if capacity runs out - callers size the buffer generously enough
 * that this never triggers for the counts used below. */
static void _build_numbered_pairs(char *const buffer, USize const capacity, USize const pair_count) {
    USize offset = 0;

    buffer[0] = '\0';

    for (USize i = 0; i < pair_count; i += 1) {
        char piece[32] = DEFAULT_INITIALIZATION;

        char_format(piece, sizeof(piece), i == 0 ? "k%zu=%zu" : "&k%zu=%zu", i, i);

        USize const piece_size = char_length(piece);

        if (offset + piece_size + 1 > capacity) {
            break;
        }

        memory_copy_1(buffer + offset, piece, piece_size);
        offset += piece_size;
    }

    buffer[offset] = '\0';
}

static void _test_max_pairs_default_is_1024(Test *const test) {
    test_case_begin(test, "init() sets max_pairs to the documented default (1024), not 0/unbounded");

    HTTP_Service_Body_Parser parser = http_service_body_parser_init();

    test_expect_u(test, "max_pairs defaults to HTTP_SERVICE_BODY_PARSER_MAX_PAIRS_DEFAULT", HTTP_SERVICE_BODY_PARSER_MAX_PAIRS_DEFAULT, parser.max_pairs);

    http_service_body_parser_uninit(&parser);

    test_case_end(test);
}

static void _test_default_max_pairs_refuses_past_1024(Test *const test) {
    test_case_begin(test, "the default max_pairs bound refuses a form body once it is exceeded, unlike the old unbounded default");

    char body[16 * 1024] = DEFAULT_INITIALIZATION;

    _build_numbered_pairs(body, sizeof(body), 1030);

    HTTP_Service_Body_Parser parser = http_service_body_parser_init();

    bool const parsed = http_service_body_parser_parse(&parser, HTTP_SERVICE_BODY_PARSER_CONTENT_TYPE_FORM, (Byte const*) body, char_length(body));

    test_expect_false(test, "parse refuses once the default bound is exceeded", parsed);
    test_expect_string(test, "the 1024th pair (index 1023) is still present", "1023", http_service_body_parser_form_get(&parser, "k1023"));
    test_expect_null(test, "the 1025th pair (index 1024) was never accepted", http_service_body_parser_form_get(&parser, "k1024"));

    http_service_body_parser_uninit(&parser);

    test_case_end(test);
}

static void _test_explicit_zero_max_pairs_is_unbounded(Test *const test) {
    test_case_begin(test, "explicitly setting max_pairs to 0 restores unbounded parsing past the new default");

    char body[16 * 1024] = DEFAULT_INITIALIZATION;

    _build_numbered_pairs(body, sizeof(body), 1030);

    HTTP_Service_Body_Parser parser = http_service_body_parser_init();

    parser.max_pairs = 0;

    bool const parsed = http_service_body_parser_parse(&parser, HTTP_SERVICE_BODY_PARSER_CONTENT_TYPE_FORM, (Byte const*) body, char_length(body));

    test_expect_true(test, "parse succeeds past the old default when max_pairs is explicitly 0", parsed);
    test_expect_string(test, "the 1030th pair is present", "1029", http_service_body_parser_form_get(&parser, "k1029"));

    http_service_body_parser_uninit(&parser);

    test_case_end(test);
}

static void _test_regression_empty_key(Test *const test) {
    test_case_begin(test, "regression: \"=v\" - an empty key is skipped, not passed to http_query_decode(size=0)");

    char const *const body = "=v&next=1";

    HTTP_Service_Body_Parser parser = http_service_body_parser_init();

    bool const parsed = http_service_body_parser_parse(&parser, HTTP_SERVICE_BODY_PARSER_CONTENT_TYPE_FORM, (Byte const*) body, char_length(body));

    test_expect_true(test, "parse succeeds (does not abort)", parsed);
    test_expect_string(test, "the pair after it still parses", "1", http_service_body_parser_form_get(&parser, "next"));

    http_service_body_parser_uninit(&parser);

    test_case_end(test);
}

static void _test_uninit_without_parse(Test *const test) {
    test_case_begin(test, "uninit on a service that never parsed");

    HTTP_Service_Body_Parser parser = http_service_body_parser_init();

    http_service_body_parser_uninit(&parser);

    test_expect_false(test, "is_form reset", parser.is_form);
    test_expect_false(test, "is_json reset", parser.is_json);
    test_expect_null(test, "json pointer reset", http_service_body_parser_json_get(&parser));

    test_case_end(test);
}

static void _test_reparse_resets_previous_state(Test *const test) {
    test_case_begin(test, "uninit between two parses on the same service leaves no stale state");

    HTTP_Service_Body_Parser parser = http_service_body_parser_init();

    char const *const first = "a=1";

    test_expect_true(test, "first parse (form) succeeds", http_service_body_parser_parse(&parser, HTTP_SERVICE_BODY_PARSER_CONTENT_TYPE_FORM, (Byte const*) first, char_length(first)));
    test_expect_string(test, "first parse value present", "1", http_service_body_parser_form_get(&parser, "a"));

    http_service_body_parser_uninit(&parser);

    char const *const second = "{\"x\":true}";

    test_expect_true(test, "second parse (json) succeeds", http_service_body_parser_parse(&parser, HTTP_SERVICE_BODY_PARSER_CONTENT_TYPE_JSON, (Byte const*) second, char_length(second)));
    test_expect_true(test, "is_json now set", parser.is_json);
    test_expect_false(test, "is_form is no longer set", parser.is_form);
    test_expect_null(test, "the old form key is gone", http_service_body_parser_form_get(&parser, "a"));

    http_service_body_parser_uninit(&parser);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Characterisation Pins (form decode + pair split)
 *============================================================================*/

/* Recorded on the hand-rolled decoder and '&' splitter BEFORE they moved onto
 * http/query's http_query_decode / http_query_next, and required to hold unchanged
 * after: every expectation below is what the old code answered, not what a spec says. */

static bool _parse_form_sized(HTTP_Service_Body_Parser *const self, char const *const body, USize const body_size) {
    return http_service_body_parser_parse(self, HTTP_SERVICE_BODY_PARSER_CONTENT_TYPE_FORM, (Byte const*) body, body_size);
}

static void _test_pin_decode_escapes(Test *const test) {
    test_case_begin(test, "pin: percent and '+' decoding of a form value");

    HTTP_Service_Body_Parser parser = http_service_body_parser_init();

    test_expect_true(test, "truncated escape: parse succeeds", _parse_form_sized(&parser, "a=x%4", 5));
    test_expect_string(test, "a truncated \"%4\" at the end passes through literally", "x%4", http_service_body_parser_form_get(&parser, "a"));

    test_expect_true(test, "lone '%': parse succeeds", _parse_form_sized(&parser, "a=%", 3));
    test_expect_string(test, "a lone trailing '%' passes through literally", "%", http_service_body_parser_form_get(&parser, "a"));

    test_expect_true(test, "invalid escape: parse succeeds", _parse_form_sized(&parser, "a=%zz", 5));
    test_expect_string(test, "an invalid \"%zz\" passes through literally", "%zz", http_service_body_parser_form_get(&parser, "a"));

    test_expect_true(test, "half-invalid escape: parse succeeds", _parse_form_sized(&parser, "a=%4g", 5));
    test_expect_string(test, "a half-hex \"%4g\" passes through literally", "%4g", http_service_body_parser_form_get(&parser, "a"));

    test_expect_true(test, "'%' then '+': parse succeeds", _parse_form_sized(&parser, "a=%+41", 6));
    test_expect_string(test, "the '%' stays literal and the '+' still becomes a space", "% 41", http_service_body_parser_form_get(&parser, "a"));

    test_expect_true(test, "'+': parse succeeds", _parse_form_sized(&parser, "a=b+c", 5));
    test_expect_string(test, "'+' decodes to a space", "b c", http_service_body_parser_form_get(&parser, "a"));

    test_expect_true(test, "encoded '+': parse succeeds", _parse_form_sized(&parser, "a=%2B", 5));
    test_expect_string(test, "\"%2B\" decodes to a literal '+', not a space", "+", http_service_body_parser_form_get(&parser, "a"));

    test_expect_true(test, "two escapes: parse succeeds", _parse_form_sized(&parser, "a=%41%42", 8));
    test_expect_string(test, "\"%41%42\" decodes to \"AB\"", "AB", http_service_body_parser_form_get(&parser, "a"));

    test_expect_true(test, "lowercase hex: parse succeeds", _parse_form_sized(&parser, "a=%6b", 5));
    test_expect_string(test, "lowercase hex digits decode", "k", http_service_body_parser_form_get(&parser, "a"));

    test_expect_true(test, "\"%00\": parse succeeds", _parse_form_sized(&parser, "a=x%00y", 7));
    test_expect_string(test, "a decoded \"%00\" ends the stored C string", "x", http_service_body_parser_form_get(&parser, "a"));

    test_expect_true(test, "encoded key: parse succeeds", _parse_form_sized(&parser, "%6B=1", 5));
    test_expect_string(test, "the KEY is decoded too: \"%6B\" is stored as \"k\"", "1", http_service_body_parser_form_get(&parser, "k"));
    test_expect_null(test, "the raw encoded key is not what is stored", http_service_body_parser_form_get(&parser, "%6B"));

    test_expect_true(test, "'+' in a key: parse succeeds", _parse_form_sized(&parser, "a+b=1", 5));
    test_expect_string(test, "'+' in a key decodes to a space", "1", http_service_body_parser_form_get(&parser, "a b"));

    http_service_body_parser_uninit(&parser);

    test_case_end(test);
}

static void _test_pin_pair_split(Test *const test) {
    test_case_begin(test, "pin: '&' splitting, empty segments, missing '=', empty key and value");

    HTTP_Service_Body_Parser parser = http_service_body_parser_init();

    test_expect_true(test, "\"&&\": parse succeeds", _parse_form_sized(&parser, "a=1&&b=2", 8));
    test_expect_u(test, "an empty segment adds nothing", 2, map_char_char_get_size(&parser.form_data));
    test_expect_string(test, "the pair before \"&&\" parses", "1", http_service_body_parser_form_get(&parser, "a"));
    test_expect_string(test, "the pair after \"&&\" parses", "2", http_service_body_parser_form_get(&parser, "b"));

    test_expect_true(test, "leading and trailing '&': parse succeeds", _parse_form_sized(&parser, "&a=1&", 5));
    test_expect_u(test, "only the one real pair is kept", 1, map_char_char_get_size(&parser.form_data));
    test_expect_string(test, "the pair between them parses", "1", http_service_body_parser_form_get(&parser, "a"));

    test_expect_true(test, "\"=v\": parse succeeds", _parse_form_sized(&parser, "=v", 2));
    test_expect_u(test, "an empty key is skipped", 0, map_char_char_get_size(&parser.form_data));

    test_expect_true(test, "\"k=\": parse succeeds", _parse_form_sized(&parser, "k=", 2));
    test_expect_u(test, "an empty value is kept", 1, map_char_char_get_size(&parser.form_data));
    test_expect_string(test, "an empty value is stored as \"\"", "", http_service_body_parser_form_get(&parser, "k"));

    test_expect_true(test, "\"k\": parse succeeds", _parse_form_sized(&parser, "k", 1));
    test_expect_u(test, "a pair with no '=' is skipped", 0, map_char_char_get_size(&parser.form_data));
    test_expect_null(test, "a pair with no '=' is absent, not empty", http_service_body_parser_form_get(&parser, "k"));

    test_expect_true(test, "\"a==b\": parse succeeds", _parse_form_sized(&parser, "a==b", 4));
    test_expect_string(test, "the split is at the FIRST '='; the rest is the value", "=b", http_service_body_parser_form_get(&parser, "a"));

    test_expect_true(test, "leading '?': parse succeeds", _parse_form_sized(&parser, "?a=1", 4));
    test_expect_null(test, "a leading '?' is NOT stripped from a form body", http_service_body_parser_form_get(&parser, "a"));
    test_expect_string(test, "it stays part of the first key", "1", http_service_body_parser_form_get(&parser, "?a"));

    test_expect_true(test, "duplicate after decode: parse succeeds", _parse_form_sized(&parser, "k=1&%6B=2", 9));
    test_expect_string(test, "a key equal only AFTER decoding still counts as a duplicate (first wins)", "1", http_service_body_parser_form_get(&parser, "k"));

    http_service_body_parser_uninit(&parser);

    test_case_end(test);
}

static void _test_pin_raw_nul_bytes(Test *const test) {
    test_case_begin(test, "pin: a RAW NUL byte inside the sized payload is data, not an end marker");

    HTTP_Service_Body_Parser parser = http_service_body_parser_init();

    char const key_nul[] = { 'a', '\0', 'b', '=', '1' };

    test_expect_true(test, "raw NUL in a key: parse succeeds", _parse_form_sized(&parser, key_nul, sizeof(key_nul)));
    test_expect_u(test, "the pair is kept (the '=' after the NUL is still found)", 1, map_char_char_get_size(&parser.form_data));
    test_expect_string(test, "the stored C-string key ends at the NUL", "1", http_service_body_parser_form_get(&parser, "a"));

    char const value_nul[] = { 'a', '=', '1', '\0', '&', 'b', '=', '2' };

    test_expect_true(test, "raw NUL in a value: parse succeeds", _parse_form_sized(&parser, value_nul, sizeof(value_nul)));
    test_expect_string(test, "the value ends at the NUL as a C string", "1", http_service_body_parser_form_get(&parser, "a"));
    test_expect_string(test, "the pair AFTER the NUL is still parsed", "2", http_service_body_parser_form_get(&parser, "b"));

    http_service_body_parser_uninit(&parser);

    test_case_end(test);
}

static void _test_pin_max_pairs_counting(Test *const test) {
    test_case_begin(test, "pin: what max_pairs counts - only '='-bearing pairs, checked before the empty-key skip");

    HTTP_Service_Body_Parser parser = http_service_body_parser_init();

    parser.max_pairs = 2;

    test_expect_true(test, "a pair with no '=' never trips the budget", _parse_form_sized(&parser, "a=1&x&b=2", 9));
    test_expect_u(test, "both '='-bearing pairs are kept", 2, map_char_char_get_size(&parser.form_data));

    test_expect_true(test, "a no-'=' pair after the budget is full is ignored, not refused", _parse_form_sized(&parser, "a=1&b=2&k", 9));

    test_expect_false(test, "an EMPTY-KEY pair after the budget is full still refuses (the check precedes the skip)", _parse_form_sized(&parser, "a=1&b=2&=v", 10));
    test_expect_u(test, "the pairs before the refusal remain", 2, map_char_char_get_size(&parser.form_data));

    test_expect_false(test, "a third real pair refuses", _parse_form_sized(&parser, "a=1&b=2&c=3", 11));
    test_expect_null(test, "the pair past the budget was never added", http_service_body_parser_form_get(&parser, "c"));

    test_expect_false(test, "a duplicate key IS stored (it grows the map), so a=1&a=2&b=2 refuses at b", _parse_form_sized(&parser, "a=1&a=2&b=2", 11));
    test_expect_u(test, "both duplicates occupy the budget", 2, map_char_char_get_size(&parser.form_data));
    test_expect_string(test, "lookup still answers the first-parsed duplicate", "1", http_service_body_parser_form_get(&parser, "a"));
    test_expect_null(test, "b was refused", http_service_body_parser_form_get(&parser, "b"));

    http_service_body_parser_uninit(&parser);

    test_case_end(test);
}

int main(void) {
    LogConfig const log_config = {
        .level             = LOG_LEVEL_ERROR,
        .stream            = stdout,
        .timestamp_enabled = true,
        .autoflush         = true
    };

    log_init(log_config);

    Test test = test_init("tests/http/service/body_parser/test_all.c");

    test_suite_begin(&test, "http_service_body_parser");
    _test_form_basic(&test);
    _test_form_single_pair_no_trailing_amp(&test);
    _test_json_passthrough(&test);
    _test_unknown_content_type(&test);
    _test_regression_pair_with_no_equals_before_a_later_one(&test);
    _test_regression_empty_value(&test);
    _test_regression_empty_key(&test);
    _test_empty_body_refuses(&test);
    _test_duplicate_key_first_wins(&test);
    _test_content_type_parameter_and_case(&test);
    _test_content_type_leading_space_is_trimmed(&test);
    _test_content_type_extra_suffix_does_not_match(&test);
    _test_max_pairs_bound(&test);
    _test_max_pairs_default_is_1024(&test);
    _test_default_max_pairs_refuses_past_1024(&test);
    _test_explicit_zero_max_pairs_is_unbounded(&test);
    _test_uninit_without_parse(&test);
    _test_reparse_resets_previous_state(&test);
    _test_parse_reuse_without_uninit_resets_state(&test);
    _test_pin_decode_escapes(&test);
    _test_pin_pair_split(&test);
    _test_pin_raw_nul_bytes(&test);
    _test_pin_max_pairs_counting(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}