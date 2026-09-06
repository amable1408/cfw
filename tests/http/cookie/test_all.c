#include <http/cookie/cookie.h>
#include <log/log.h>
#include <test/test.h>

/*
 * Coverage for http/cookie: http_cookie_get_1 (the Cookie request-header scanner),
 * http_cookie_set_header_create / clear_header_create_1..3 (the Set-Cookie builder and its
 * injection-refusal choke point), http_cookie_same_site_parse, and the alloc_* arena tier's
 * happy path.
 *
 * Null pointers to the public entry points go through error_check_null - deliberately absent
 * here (would abort the process); test_unchecked.c pins that every injection refusal below
 * (bad name, CRLF/space/';' in a value, ';'/CTL in Path or Domain, SameSite=None or Partitioned
 * without Secure) is an ordinary runtime branch, not an artifact of ERROR_CHECK_ENABLED - and
 * (Mid-4, R2) that alloc_init_2's whole-refusal-on-a-refused-arena branch is the same kind of
 * plain runtime condition, so it lives there too rather than being pinned twice.
 */

static void _test_get_1_scanner_rules(Test *const test) {
    test_case_begin(test, "get_1: first/middle/last, duplicates, prefix keys, pair without '='");

    String a = http_cookie_get_1("a=1; b=2; c=3", "a");
    String b = http_cookie_get_1("a=1; b=2; c=3", "b");
    String c = http_cookie_get_1("a=1; b=2; c=3", "c");
    String dup = http_cookie_get_1("a=1; a=2", "a");
    String prefix = http_cookie_get_1("a2=1; a=2", "a");
    String no_equals = http_cookie_get_1("bad; a=1", "a");
    String no_name   = http_cookie_get_1("a=1; =1", "");

    test_expect_string(test, "leading key", "1", string_get_data(&a));
    test_expect_string(test, "middle key", "2", string_get_data(&b));
    test_expect_string(test, "trailing key", "3", string_get_data(&c));
    test_expect_string(test, "duplicate name answers the first occurrence", "1", string_get_data(&dup));
    test_expect_string(test, "\"a\" does not false-match inside \"a2\"'s pair", "2", string_get_data(&prefix));
    test_expect_string(test, "a pair without '=' is skipped, not read as an empty-value name", "1", string_get_data(&no_equals));
    test_expect_u(test, "an empty name is a miss even when the header has a '; =1' pair", 0, string_get_size(&no_name));

    string_uninit(&a);
    string_uninit(&b);
    string_uninit(&c);
    string_uninit(&dup);
    string_uninit(&prefix);
    string_uninit(&no_equals);
    string_uninit(&no_name);

    test_case_end(test);
}

static void _test_get_1_whitespace_and_quoting(Test *const test) {
    test_case_begin(test, "get_1: SP/HTAB around ';' skipped, trailing SP kept, DQUOTE preserved, byte-exact names");

    String no_space   = http_cookie_get_1("a=1;b=2", "b");
    String htab       = http_cookie_get_1("a=1;\tb=2", "b");
    String trailing   = http_cookie_get_1("a=1 ; b=2", "a");
    String quoted     = http_cookie_get_1("sid=\"abc\"", "sid");
    String case_miss  = http_cookie_get_1("sid=1", "SID");

    test_expect_string(test, "a semicolon without a following space still separates pairs", "2", string_get_data(&no_space));
    test_expect_string(test, "HTAB after ';' is skipped like SP", "2", string_get_data(&htab));
    test_expect_string(test, "a trailing SP inside a value is kept as-is", "1 ", string_get_data(&trailing));
    test_expect_string(test, "a DQUOTE-wrapped value is returned WITH its quotes", "\"abc\"", string_get_data(&quoted));
    test_expect_u(test, "name comparison is byte-exact (case-sensitive): 'SID' does not match 'sid'", 0, string_get_size(&case_miss));

    string_uninit(&no_space);
    string_uninit(&htab);
    string_uninit(&trailing);
    string_uninit(&quoted);
    string_uninit(&case_miss);

    test_case_end(test);
}

static void _test_get_1_absent_and_degenerate(Test *const test) {
    test_case_begin(test, "get_1: a missing key, an empty value, and a header of only spaces all answer the empty String");

    String missing    = http_cookie_get_1("a=1", "zzz");
    String empty_val  = http_cookie_get_1("sid=; other=1", "sid");
    String only_space = http_cookie_get_1("   ", "a");

    test_expect_u(test, "a key not present in the header is absent", 0, string_get_size(&missing));
    test_expect_u(test, "an empty value (\"sid=\") reads back the same as absent", 0, string_get_size(&empty_val));
    test_expect_u(test, "a header of only spaces yields no match, not a crash", 0, string_get_size(&only_space));

    string_uninit(&missing);
    string_uninit(&empty_val);
    string_uninit(&only_space);

    test_case_end(test);
}

static void _test_same_site_parse(Test *const test) {
    test_case_begin(test, "same_site_parse: Lax/None/Strict match case-insensitively (Mid-3); everything else is UNSET");

    test_expect_u(test, "\"Lax\" parses", HTTP_COOKIE_SAME_SITE_LAX, http_cookie_same_site_parse("Lax"));
    test_expect_u(test, "\"None\" parses", HTTP_COOKIE_SAME_SITE_NONE, http_cookie_same_site_parse("None"));
    test_expect_u(test, "\"Strict\" parses", HTTP_COOKIE_SAME_SITE_STRICT, http_cookie_same_site_parse("Strict"));
    test_expect_u(test, "lowercase \"lax\" now parses too (was UNSET before Mid-3)", HTTP_COOKIE_SAME_SITE_LAX, http_cookie_same_site_parse("lax"));
    test_expect_u(test, "uppercase \"STRICT\" parses", HTTP_COOKIE_SAME_SITE_STRICT, http_cookie_same_site_parse("STRICT"));
    test_expect_u(test, "mixed-case \"nOnE\" parses", HTTP_COOKIE_SAME_SITE_NONE, http_cookie_same_site_parse("nOnE"));
    test_expect_u(test, "empty text is UNSET", HTTP_COOKIE_SAME_SITE_UNSET, http_cookie_same_site_parse(""));
    test_expect_u(test, "an unrecognized value is UNSET, not a guess", HTTP_COOKIE_SAME_SITE_UNSET, http_cookie_same_site_parse("laxx"));

    test_case_end(test);
}

static void _test_init_1_defaults_exact_bytes(Test *const test) {
    test_case_begin(test, "init_1 defaults (Path=/, SameSite=Lax, Secure, HttpOnly) render exact bytes");

    HTTP_Cookie cookie = http_cookie_init_1("sid", "tok");
    String      header = http_cookie_set_header_create(&cookie);

    test_expect_string(test, "exact header line", "Set-Cookie: sid=tok; Path=/; Secure; HttpOnly; SameSite=Lax\r\n", string_get_data(&header));

    string_uninit(&header);
    http_cookie_uninit(&cookie);

    test_case_end(test);
}

static void _test_set_header_attribute_toggles(Test *const test) {
    test_case_begin(test, "set_header_create: Max-Age, Domain, and each bool flag on/off");

    HTTP_Cookie cookie = http_cookie_init_2("sid", "tok", "/", HTTP_COOKIE_SAME_SITE_UNSET, false, false);
    String      bare   = http_cookie_set_header_create(&cookie);

    test_expect_string(test, "no Domain/Max-Age/Secure/HttpOnly/SameSite when all are off", "Set-Cookie: sid=tok; Path=/\r\n", string_get_data(&bare));

    string_uninit(&bare);

    http_cookie_max_age_set(&cookie, 0);

    String zero_age = http_cookie_set_header_create(&cookie);

    test_expect_string(test, "Max-Age=0 with no Expires (that shortcut belongs to the clear_* forms only)", "Set-Cookie: sid=tok; Path=/; Max-Age=0\r\n", string_get_data(&zero_age));

    string_uninit(&zero_age);

    http_cookie_max_age_set(&cookie, 3600);
    http_cookie_domain_set(&cookie, "example.com");

    String full = http_cookie_set_header_create(&cookie);

    test_expect_string(test, "Domain before Max-Age, in attribute order", "Set-Cookie: sid=tok; Path=/; Domain=example.com; Max-Age=3600\r\n", string_get_data(&full));

    string_uninit(&full);
    http_cookie_uninit(&cookie);

    HTTP_Cookie strict = http_cookie_init_2("sid", "tok", "/", HTTP_COOKIE_SAME_SITE_STRICT, true, true);

    http_cookie_partitioned_set(&strict, true);

    String partitioned = http_cookie_set_header_create(&strict);

    test_expect_string(test, "Partitioned (with Secure) after SameSite", "Set-Cookie: sid=tok; Path=/; Secure; HttpOnly; SameSite=Strict; Partitioned\r\n", string_get_data(&partitioned));

    string_uninit(&partitioned);
    http_cookie_uninit(&strict);

    test_case_end(test);
}

static void _test_has_max_age_toggle_off(Test *const test) {
    test_case_begin(test, "has_max_age_set(false) after max_age_set omits Max-Age again");

    HTTP_Cookie cookie = http_cookie_init_1("sid", "tok");

    http_cookie_max_age_set(&cookie, 10);
    http_cookie_has_max_age_set(&cookie, false);

    String header = http_cookie_set_header_create(&cookie);

    test_expect_string(test, "Max-Age is gone once has_max_age is turned back off", "Set-Cookie: sid=tok; Path=/; Secure; HttpOnly; SameSite=Lax\r\n", string_get_data(&header));

    string_uninit(&header);
    http_cookie_uninit(&cookie);

    test_case_end(test);
}

static void _test_injection_refusals(Test *const test) {
    test_case_begin(test, "set_header_create refuses (EMPTY) rather than split or forge a header");

    HTTP_Cookie bad_name = http_cookie_init_1("bad name", "tok");
    String      r1       = http_cookie_set_header_create(&bad_name);

    test_expect_u(test, "a name with a space is not a token", 0, string_get_size(&r1));
    string_uninit(&r1);
    http_cookie_uninit(&bad_name);

    HTTP_Cookie crlf_value = http_cookie_init_1("sid", "x\r\nSet-Cookie: admin=1");
    String      r2         = http_cookie_set_header_create(&crlf_value);

    test_expect_u(test, "CRLF in the value is refused, not split into a second header", 0, string_get_size(&r2));
    string_uninit(&r2);
    http_cookie_uninit(&crlf_value);

    HTTP_Cookie semicolon_value = http_cookie_init_1("sid", "a;b");
    String      r3              = http_cookie_set_header_create(&semicolon_value);

    test_expect_u(test, "';' in a bare value is refused", 0, string_get_size(&r3));
    string_uninit(&r3);
    http_cookie_uninit(&semicolon_value);

    HTTP_Cookie space_value = http_cookie_init_1("sid", "a b");
    String      r4          = http_cookie_set_header_create(&space_value);

    test_expect_u(test, "a bare space in the value is refused (not cookie-octet)", 0, string_get_size(&r4));
    string_uninit(&r4);
    http_cookie_uninit(&space_value);

    HTTP_Cookie quoted_value = http_cookie_init_1("sid", "\"abc\"");
    String      r5           = http_cookie_set_header_create(&quoted_value);

    test_expect_string(test, "a DQUOTE-wrapped value of valid cookie-octets is accepted", "Set-Cookie: sid=\"abc\"; Path=/; Secure; HttpOnly; SameSite=Lax\r\n", string_get_data(&r5));
    string_uninit(&r5);
    http_cookie_uninit(&quoted_value);

    HTTP_Cookie bad_path = http_cookie_init_2("sid", "tok", "/a;b", HTTP_COOKIE_SAME_SITE_LAX, true, true);
    String      r6        = http_cookie_set_header_create(&bad_path);

    test_expect_u(test, "';' in Path is refused", 0, string_get_size(&r6));
    string_uninit(&r6);
    http_cookie_uninit(&bad_path);

    HTTP_Cookie ctl_path = http_cookie_init_2("sid", "tok", "/a\rb", HTTP_COOKIE_SAME_SITE_LAX, true, true);
    String      r7        = http_cookie_set_header_create(&ctl_path);

    test_expect_u(test, "a control byte in Path is refused", 0, string_get_size(&r7));
    string_uninit(&r7);
    http_cookie_uninit(&ctl_path);

    HTTP_Cookie bad_domain = http_cookie_init_1("sid", "tok");

    http_cookie_domain_set(&bad_domain, "ex;ample.com");

    String r8 = http_cookie_set_header_create(&bad_domain);

    test_expect_u(test, "';' in Domain is refused", 0, string_get_size(&r8));
    string_uninit(&r8);
    http_cookie_uninit(&bad_domain);

    HTTP_Cookie none_insecure = http_cookie_init_2("sid", "tok", "/", HTTP_COOKIE_SAME_SITE_NONE, false, true);
    String      r9            = http_cookie_set_header_create(&none_insecure);

    test_expect_u(test, "SameSite=None without Secure is refused", 0, string_get_size(&r9));
    string_uninit(&r9);
    http_cookie_uninit(&none_insecure);

    HTTP_Cookie none_secure = http_cookie_init_2("sid", "tok", "/", HTTP_COOKIE_SAME_SITE_NONE, true, true);
    String      r10         = http_cookie_set_header_create(&none_secure);

    test_expect_string(test, "SameSite=None WITH Secure is accepted", "Set-Cookie: sid=tok; Path=/; Secure; HttpOnly; SameSite=None\r\n", string_get_data(&r10));
    string_uninit(&r10);
    http_cookie_uninit(&none_secure);

    HTTP_Cookie partitioned_insecure = http_cookie_init_2("sid", "tok", "/", HTTP_COOKIE_SAME_SITE_LAX, false, true);

    http_cookie_partitioned_set(&partitioned_insecure, true);

    String r11 = http_cookie_set_header_create(&partitioned_insecure);

    test_expect_u(test, "Partitioned without Secure is refused", 0, string_get_size(&r11));
    string_uninit(&r11);
    http_cookie_uninit(&partitioned_insecure);

    /* Cookie-octet grammar regression pins: DEL, a byte >= 0x80, backslash, and comma are all
     * outside cookie-octet and must stay refused (session.c/csrf.c both depend on
     * http_cookie_get_1 answering EMPTY, never a half-decoded value, for anything this rejects
     * on the way out - see the cookie_read guard note in those services' cookie_read()).
     * An embedded NUL cannot be represented through this NUL-terminated char* API and so is
     * not pinned here. */
    HTTP_Cookie del_value = http_cookie_init_1("sid", "a" "\x7F" "b");
    String      r12       = http_cookie_set_header_create(&del_value);

    test_expect_u(test, "DEL (0x7F) in the value is refused", 0, string_get_size(&r12));
    string_uninit(&r12);
    http_cookie_uninit(&del_value);

    HTTP_Cookie high_bit_value = http_cookie_init_1("sid", "a" "\x80" "b");
    String      r13            = http_cookie_set_header_create(&high_bit_value);

    test_expect_u(test, "a byte >= 0x80 in the value is refused", 0, string_get_size(&r13));
    string_uninit(&r13);
    http_cookie_uninit(&high_bit_value);

    HTTP_Cookie backslash_value = http_cookie_init_1("sid", "a\\b");
    String      r14             = http_cookie_set_header_create(&backslash_value);

    test_expect_u(test, "a backslash in the value is refused", 0, string_get_size(&r14));
    string_uninit(&r14);
    http_cookie_uninit(&backslash_value);

    HTTP_Cookie comma_value = http_cookie_init_1("sid", "a,b");
    String      r15         = http_cookie_set_header_create(&comma_value);

    test_expect_u(test, "a comma in the value is refused", 0, string_get_size(&r15));
    string_uninit(&r15);
    http_cookie_uninit(&comma_value);

    HTTP_Cookie comma_path = http_cookie_init_2("sid", "tok", "/a,b", HTTP_COOKIE_SAME_SITE_LAX, true, true);
    String      r16        = http_cookie_set_header_create(&comma_path);

    test_expect_u(test, "a comma in Path is refused", 0, string_get_size(&r16));
    string_uninit(&r16);
    http_cookie_uninit(&comma_path);

    test_case_end(test);
}

static void _test_clear_header_create_1_and_2(Test *const test) {
    test_case_begin(test, "clear_header_create_1/_2 exact bytes (assume a Secure/HttpOnly/Lax cookie)");

    String c1 = http_cookie_clear_header_create_1("sid");

    test_expect_string(test, "_1 default path, no domain",
        "Set-Cookie: sid=; Path=/; Max-Age=0; Expires=Thu, 01 Jan 1970 00:00:00 GMT; Secure; HttpOnly; SameSite=Lax\r\n", string_get_data(&c1));
    string_uninit(&c1);

    String c2 = http_cookie_clear_header_create_2("sid", "/app", "example.com");

    test_expect_string(test, "_2 explicit path and domain",
        "Set-Cookie: sid=; Path=/app; Domain=example.com; Max-Age=0; Expires=Thu, 01 Jan 1970 00:00:00 GMT; Secure; HttpOnly; SameSite=Lax\r\n", string_get_data(&c2));
    string_uninit(&c2);

    String c3 = http_cookie_clear_header_create_2("bad name", "/", "");

    test_expect_u(test, "_2 refuses an invalid name", 0, string_get_size(&c3));
    string_uninit(&c3);

    String c4 = http_cookie_clear_header_create_2("sid", "", "");

    test_expect_string(test, "an empty Path is OMITTED, not emitted as bare 'Path=' (Low-11: aligns with clear_3)",
        "Set-Cookie: sid=; Max-Age=0; Expires=Thu, 01 Jan 1970 00:00:00 GMT; Secure; HttpOnly; SameSite=Lax\r\n", string_get_data(&c4));
    string_uninit(&c4);

    test_case_end(test);
}

static void _test_clear_header_create_3_mirrors_self(Test *const test) {
    test_case_begin(test, "clear_header_create_3 mirrors self's own flags instead of assuming Secure/HttpOnly/Lax");

    HTTP_Cookie plain = http_cookie_init_2("sid", "tok", "/", HTTP_COOKIE_SAME_SITE_UNSET, false, false);
    String      c1    = http_cookie_clear_header_create_3(&plain);

    test_expect_string(test, "a non-Secure, non-HttpOnly, no-SameSite cookie clears without those flags",
        "Set-Cookie: sid=; Path=/; Max-Age=0; Expires=Thu, 01 Jan 1970 00:00:00 GMT\r\n", string_get_data(&c1));
    string_uninit(&c1);
    http_cookie_uninit(&plain);

    HTTP_Cookie strict = http_cookie_init_2("sid", "tok", "/", HTTP_COOKIE_SAME_SITE_STRICT, true, true);

    http_cookie_partitioned_set(&strict, true);

    String c2 = http_cookie_clear_header_create_3(&strict);

    test_expect_string(test, "Secure/HttpOnly/SameSite/Partitioned are all mirrored back",
        "Set-Cookie: sid=; Path=/; Max-Age=0; Expires=Thu, 01 Jan 1970 00:00:00 GMT; Secure; HttpOnly; SameSite=Strict; Partitioned\r\n", string_get_data(&c2));
    string_uninit(&c2);
    http_cookie_uninit(&strict);

    HTTP_Cookie invalid = http_cookie_init_1("bad name", "tok");
    String      c3      = http_cookie_clear_header_create_3(&invalid);

    test_expect_u(test, "an invalid name refuses just like set_header_create", 0, string_get_size(&c3));
    string_uninit(&c3);
    http_cookie_uninit(&invalid);

    test_case_end(test);
}

static void _test_uninit_is_idempotent(Test *const test) {
    test_case_begin(test, "uninit twice is safe");

    HTTP_Cookie cookie = http_cookie_init_1("sid", "tok");

    http_cookie_uninit(&cookie);
    http_cookie_uninit(&cookie);

    test_expect_true(test, "same_site resets to UNSET", cookie.same_site == HTTP_COOKIE_SAME_SITE_UNSET);
    test_expect_false(test, "secure resets to false", cookie.secure);

    test_case_end(test);
}

static void _test_alloc_tier_get_and_clear(Test *const test) {
    test_case_begin(test, "alloc_get_1 and alloc_clear_header_create_1/_2 match the heap tier byte-for-byte");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    String value = http_cookie_alloc_get_1("a=1; b=2", "b", &arena);

    test_expect_string(test, "alloc_get_1 finds the value", "2", string_get_data(&value));

    String header = http_cookie_alloc_clear_header_create_1("sid", &arena);

    test_expect_string(test, "alloc_clear_header_create_1 matches the heap tier's bytes",
        "Set-Cookie: sid=; Path=/; Max-Age=0; Expires=Thu, 01 Jan 1970 00:00:00 GMT; Secure; HttpOnly; SameSite=Lax\r\n", string_get_data(&header));

    // arena-backed Strings release with the arena itself - no per-string uninit.
    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_alloc_init_success(Test *const test) {
    // A genuinely exhausted (too-small) linear arena is NOT exercised here: arena_linear_alloc
    // aborts the process on capacity overrun under ERROR_CHECK_ENABLED (an abort primitive, not
    // a graceful refusal) and skips the bound check entirely when it is off - neither is a safe
    // or meaningful way to drive http_cookie_alloc_init_2's own refuse-whole branch, which reacts
    // to a String coming back EMPTY-with-null-data (a genuinely REJECTED Arena handle), not to
    // mid-allocation exhaustion. Only the success path is pinned here; the refuse-whole branch
    // (a REJECTED handle, arena_init_2(0, ...)) is pinned in test_unchecked.c (Mid-4, R2).
    test_case_begin(test, "alloc_init_1/_2 succeed on a working arena and render the same bytes as the heap tier");

    Arena       roomy   = arena_init_1(4096, ARENA_TYPE_LINEAR);
    HTTP_Cookie cookie  = (HTTP_Cookie) DEFAULT_INITIALIZATION;
    bool const  success = http_cookie_alloc_init_1(&cookie, "sid", "tok", &roomy);

    test_expect_true(test, "a roomy arena succeeds", success);

    String header = http_cookie_set_header_create(&cookie);

    test_expect_string(test, "the arena-backed cookie renders the same bytes as the heap tier", "Set-Cookie: sid=tok; Path=/; Secure; HttpOnly; SameSite=Lax\r\n", string_get_data(&header));

    string_uninit(&header);
    http_cookie_uninit(&cookie);
    arena_uninit(&roomy, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Entry Point
 *============================================================================*/

int main(void) {
    log_init((LogConfig){ .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true });

    Test test = test_init("tests/http/cookie/test_all.c");

    test_suite_begin(&test, "http_cookie");
    _test_get_1_scanner_rules(&test);
    _test_get_1_whitespace_and_quoting(&test);
    _test_get_1_absent_and_degenerate(&test);
    _test_same_site_parse(&test);
    _test_init_1_defaults_exact_bytes(&test);
    _test_set_header_attribute_toggles(&test);
    _test_has_max_age_toggle_off(&test);
    _test_injection_refusals(&test);
    _test_clear_header_create_1_and_2(&test);
    _test_clear_header_create_3_mirrors_self(&test);
    _test_uninit_is_idempotent(&test);
    _test_alloc_tier_get_and_clear(&test);
    _test_alloc_init_success(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}