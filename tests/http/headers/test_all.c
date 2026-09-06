#include <stdio.h>

#include <arena/arena.h>
#include <http/headers/headers.h>
#include <test/test.h>

/*
 * Suite for http/headers/headers.c (cache + content + security folded into one directory,
 * 2026-09-06 fold). Pins exact byte output for every builder (heap and arena tiers), the
 * Critical-3 content refusals (CTL in filename/mime, '"'/'\\' escaping in a disposition
 * filename), and the security policy's field-by-field behavior (each bool's line, the 4 HSTS
 * combinations, an empty policy value omitting its header, uninit resetting every field, and
 * create_1 == init_1 + create_2). The High-4 arena-refuses-the-whole-policy path needs
 * ERROR_CHECK_ENABLED off to observe past error_check_null's abort shape on a null allocator,
 * so it lives in test_unchecked.c instead - the arena itself is refused (not null), which is a
 * plain runtime condition and holds under both builds, but this suite keeps that scenario in
 * one place rather than pinning it twice. Mid-1 (R2): a control byte in a policy value now
 * WARN-logs either way, and additionally refuses the WHOLE policy in the alloc_init_2 tier
 * (only the by-value init_2 tier still omits just that one field) - both are ordinary runtime
 * conditions on a healthy arena, so both are pinned here rather than in test_unchecked.c.
 */

/*==============================================================================
 * MARK: - Cache
 *============================================================================*/

static void _test_cache(Test *const test) {
    test_case_begin(test, "cache: heap tier exact bytes");

    String no_cache = http_headers_cache_no_cache();
    test_expect_string(test, "no_cache", "Cache-Control: no-cache, must-revalidate\r\nPragma: no-cache\r\n", string_get_data(&no_cache));
    string_uninit(&no_cache);

    String no_store = http_headers_cache_no_store();
    test_expect_string(test, "no_store", "Cache-Control: no-store, max-age=0\r\nPragma: no-cache\r\n", string_get_data(&no_store));
    string_uninit(&no_store);

    String private_zero = http_headers_cache_private(0);
    test_expect_string(test, "private, max-age=0", "Cache-Control: private, max-age=0\r\n", string_get_data(&private_zero));
    string_uninit(&private_zero);

    char expected_max[64] = DEFAULT_INITIALIZATION;
    snprintf(expected_max, sizeof(expected_max), "Cache-Control: private, max-age=%zu\r\n", USIZE_MAX);

    String private_max = http_headers_cache_private(USIZE_MAX);
    test_expect_string(test, "private, max-age=USIZE_MAX", expected_max, string_get_data(&private_max));
    string_uninit(&private_max);

    String public_header = http_headers_cache_public(3600);
    test_expect_string(test, "public, max-age=3600", "Cache-Control: public, max-age=3600\r\n", string_get_data(&public_header));
    string_uninit(&public_header);

    String static_header = http_headers_cache_static(3600);
    test_expect_string(test, "static (public, immutable)", "Cache-Control: public, max-age=3600, immutable\r\n", string_get_data(&static_header));
    string_uninit(&static_header);

    test_case_end(test);
}

#ifdef ARENA_IMPLEMENTATION
static void _test_cache_arena(Test *const test) {
    test_case_begin(test, "cache: arena tier exact bytes");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    String no_cache = http_headers_cache_alloc_no_cache(&arena);
    test_expect_string(test, "alloc_no_cache", "Cache-Control: no-cache, must-revalidate\r\nPragma: no-cache\r\n", string_get_data(&no_cache));

    String no_store = http_headers_cache_alloc_no_store(&arena);
    test_expect_string(test, "alloc_no_store", "Cache-Control: no-store, max-age=0\r\nPragma: no-cache\r\n", string_get_data(&no_store));

    String private_header = http_headers_cache_alloc_private(60, &arena);
    test_expect_string(test, "alloc_private", "Cache-Control: private, max-age=60\r\n", string_get_data(&private_header));

    String public_header = http_headers_cache_alloc_public(60, &arena);
    test_expect_string(test, "alloc_public", "Cache-Control: public, max-age=60\r\n", string_get_data(&public_header));

    String static_header = http_headers_cache_alloc_static(60, &arena);
    test_expect_string(test, "alloc_static", "Cache-Control: public, max-age=60, immutable\r\n", string_get_data(&static_header));

    arena_uninit(&arena, ARENA_TYPE_LINEAR);
    test_case_end(test);
}
#endif // ARENA_IMPLEMENTATION

static void _test_cache_max_age_into(Test *const test) {
    test_case_begin(test, "cache: http_headers_cache_max_age_into");

    char buffer[64] = DEFAULT_INITIALIZATION;
    USize const written = http_headers_cache_max_age_into(buffer, sizeof(buffer), 5);

    test_expect_string(test, "writes the same shape static.c used to hand-roll", "Cache-Control: max-age=5\r\n", buffer);
    test_expect_u(test, "return value is strlen of the written header", char_length(buffer), written);

    USize const needed = char_length(buffer) + 1;
    char exact[64] = DEFAULT_INITIALIZATION;
    USize const written_exact = http_headers_cache_max_age_into(exact, needed, 5);

    test_expect_string(test, "capacity == needed still succeeds", "Cache-Control: max-age=5\r\n", exact);
    test_expect_u(test, "exact-fit return value", needed - 1, written_exact);

    char too_small[64] = "unchanged";
    USize const written_refused = http_headers_cache_max_age_into(too_small, needed - 1, 5);

    test_expect_u(test, "one byte short REFUSES rather than truncates", 0, written_refused);
    test_expect_i(test, "a refusal clears buffer[0]", (ISize) '\0', (ISize) too_small[0]);

    char zero_capacity[1] = "x";
    USize const written_zero = http_headers_cache_max_age_into(zero_capacity, 0, 5);

    test_expect_u(test, "capacity 0 is a legal value: answers 0 rather than aborting", 0, written_zero);
    test_expect_i(test, "capacity 0 leaves the caller's buffer untouched (no writable byte to clear)", (ISize) 'x', (ISize) zero_capacity[0]);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Content
 *============================================================================*/

static void _test_content(Test *const test) {
    test_case_begin(test, "content: heap tier exact bytes and refusals");

    String html = http_headers_content_html();
    test_expect_string(test, "html", "Content-Type: text/html; charset=utf-8\r\n", string_get_data(&html));
    string_uninit(&html);

    String json = http_headers_content_json();
    test_expect_string(test, "json", "Content-Type: application/json; charset=utf-8\r\n", string_get_data(&json));
    string_uninit(&json);

    String length_zero = http_headers_content_length(0);
    test_expect_string(test, "length 0", "Content-Length: 0\r\n", string_get_data(&length_zero));
    string_uninit(&length_zero);

    char expected_max[64] = DEFAULT_INITIALIZATION;
    snprintf(expected_max, sizeof(expected_max), "Content-Length: %zu\r\n", USIZE_MAX);

    String length_max = http_headers_content_length(USIZE_MAX);
    test_expect_string(test, "length USIZE_MAX", expected_max, string_get_data(&length_max));
    string_uninit(&length_max);

    String type = http_headers_content_type("image/png");
    test_expect_string(test, "type", "Content-Type: image/png\r\n", string_get_data(&type));
    string_uninit(&type);

    String plain = http_headers_content_disposition_attachment("report.pdf");
    test_expect_string(test, "disposition, no special characters", "Content-Disposition: attachment; filename=\"report.pdf\"\r\n", string_get_data(&plain));
    string_uninit(&plain);

    String escaped = http_headers_content_disposition_attachment("a\"b\\c.txt");
    test_expect_string(test, "disposition escapes '\"' and '\\\\' (RFC 6266)", "Content-Disposition: attachment; filename=\"a\\\"b\\\\c.txt\"\r\n", string_get_data(&escaped));
    string_uninit(&escaped);

    String ctl_filename = http_headers_content_disposition_attachment("evil\r\nSet-Cookie: x=1");
    test_expect_true(test, "a CTL byte in filename REFUSES (EMPTY)", string_get_data(&ctl_filename) == nullptr);
    test_expect_true(test, "the refusal is empty by size too", string_empty(&ctl_filename));

    String ctl_mime = http_headers_content_type("text/html\r\nX-Injected: 1");
    test_expect_true(test, "a CTL byte in mime REFUSES (EMPTY)", string_get_data(&ctl_mime) == nullptr);

    test_case_end(test);
}

#ifdef ARENA_IMPLEMENTATION
static void _test_content_arena(Test *const test) {
    test_case_begin(test, "content: arena tier exact bytes and refusals");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    String html = http_headers_content_alloc_html(&arena);
    test_expect_string(test, "alloc_html", "Content-Type: text/html; charset=utf-8\r\n", string_get_data(&html));

    String json = http_headers_content_alloc_json(&arena);
    test_expect_string(test, "alloc_json", "Content-Type: application/json; charset=utf-8\r\n", string_get_data(&json));

    String length = http_headers_content_alloc_length(42, &arena);
    test_expect_string(test, "alloc_length", "Content-Length: 42\r\n", string_get_data(&length));

    String type = http_headers_content_alloc_type("image/png", &arena);
    test_expect_string(test, "alloc_type", "Content-Type: image/png\r\n", string_get_data(&type));

    String escaped = http_headers_content_alloc_disposition_attachment("a\"b\\c.txt", &arena);
    test_expect_string(test, "alloc_disposition_attachment escapes", "Content-Disposition: attachment; filename=\"a\\\"b\\\\c.txt\"\r\n", string_get_data(&escaped));

    String ctl_mime = http_headers_content_alloc_type("text/html\r\nX-Injected: 1", &arena);
    test_expect_true(test, "arena tier also refuses a CTL mime (EMPTY)", string_get_data(&ctl_mime) == nullptr);

    arena_uninit(&arena, ARENA_TYPE_LINEAR);
    test_case_end(test);
}
#endif // ARENA_IMPLEMENTATION

/*==============================================================================
 * MARK: - Security
 *============================================================================*/

static void _test_security_default(Test *const test) {
    test_case_begin(test, "security: default policy exact block");

    String policy = http_headers_security_create_1();

    char const *const expected =
        "Content-Security-Policy: default-src 'self'; frame-ancestors 'none'; base-uri 'self'; object-src 'none'\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "X-Frame-Options: DENY\r\n"
        "Referrer-Policy: no-referrer\r\n"
        "Permissions-Policy: geolocation=(), camera=(), microphone=()\r\n"
        "Cross-Origin-Opener-Policy: same-origin\r\n"
        "Cross-Origin-Resource-Policy: same-origin\r\n";

    test_expect_string(test, "create_1 default block", expected, string_get_data(&policy));
    string_uninit(&policy);

    HTTP_Headers_Security init = http_headers_security_init_1();
    String from_init = http_headers_security_create_2(&init);

    test_expect_string(test, "create_1 == init_1 + create_2", expected, string_get_data(&from_init));

    string_uninit(&from_init);
    http_headers_security_uninit(&init);

    test_case_end(test);
}

static void _test_security_bools(Test *const test) {
    test_case_begin(test, "security: each bool off removes its own line");

    HTTP_Headers_Security all_off = http_headers_security_init_2("csp", "DENY", "no-referrer", "perm", false, false, false, false, false, 0, false, false);
    String block = http_headers_security_create_2(&all_off);

    test_expect_string(test, "no optional line survives with every bool false",
        "Content-Security-Policy: csp\r\nX-Frame-Options: DENY\r\nReferrer-Policy: no-referrer\r\nPermissions-Policy: perm\r\n", string_get_data(&block));

    string_uninit(&block);
    http_headers_security_uninit(&all_off);

    HTTP_Headers_Security embedder_only = http_headers_security_init_2("csp", "DENY", "no-referrer", "perm", false, true, false, false, false, 0, false, false);
    String embedder_block = http_headers_security_create_2(&embedder_only);

    test_expect_true(test, "cross_origin_embedder_policy on adds its own line", string_find_count_1(&embedder_block, "Cross-Origin-Embedder-Policy: require-corp\r\n") == 1);

    string_uninit(&embedder_block);
    http_headers_security_uninit(&embedder_only);

    test_case_end(test);
}

static void _test_security_hsts(Test *const test) {
    test_case_begin(test, "security: HSTS 4 combinations");

    HTTP_Headers_Security off = http_headers_security_init_2("csp", "DENY", "ref", "perm", false, false, false, false, false, 100, false, false);
    String off_block = http_headers_security_create_2(&off);
    test_expect_true(test, "sts off: no Strict-Transport-Security line", string_find_count_1(&off_block, "Strict-Transport-Security") == 0);
    string_uninit(&off_block);
    http_headers_security_uninit(&off);

    HTTP_Headers_Security plain = http_headers_security_init_2("csp", "DENY", "ref", "perm", false, false, false, false, true, 100, false, false);
    String plain_block = http_headers_security_create_2(&plain);
    test_expect_string(test, "sts on, no subdomains/preload", "Content-Security-Policy: csp\r\nX-Frame-Options: DENY\r\nReferrer-Policy: ref\r\n"
        "Permissions-Policy: perm\r\nStrict-Transport-Security: max-age=100\r\n", string_get_data(&plain_block));
    string_uninit(&plain_block);
    http_headers_security_uninit(&plain);

    HTTP_Headers_Security subdomains = http_headers_security_init_2("csp", "DENY", "ref", "perm", false, false, false, false, true, 100, true, false);
    String subdomains_block = http_headers_security_create_2(&subdomains);
    test_expect_true(test, "sts + includeSubDomains", string_find_count_1(&subdomains_block, "Strict-Transport-Security: max-age=100; includeSubDomains\r\n") == 1);
    string_uninit(&subdomains_block);
    http_headers_security_uninit(&subdomains);

    HTTP_Headers_Security preload = http_headers_security_init_2("csp", "DENY", "ref", "perm", false, false, false, false, true, 100, true, true);
    String preload_block = http_headers_security_create_2(&preload);
    test_expect_true(test, "sts + includeSubDomains + preload", string_find_count_1(&preload_block, "Strict-Transport-Security: max-age=100; includeSubDomains; preload\r\n") == 1);
    string_uninit(&preload_block);
    http_headers_security_uninit(&preload);

    test_case_end(test);
}

static void _test_security_empty_value(Test *const test) {
    test_case_begin(test, "security: an empty policy value omits its header");

    HTTP_Headers_Security policy = http_headers_security_init_2("csp", "DENY", "", "perm", false, false, false, false, false, 0, false, false);
    String block = http_headers_security_create_2(&policy);

    test_expect_true(test, "empty referrer_policy means no Referrer-Policy line", string_find_count_1(&block, "Referrer-Policy") == 0);
    test_expect_true(test, "the other lines are unaffected", string_find_count_1(&block, "Content-Security-Policy: csp\r\n") == 1);

    string_uninit(&block);
    http_headers_security_uninit(&policy);

    test_case_end(test);
}

static void _test_security_control_byte_refusal(Test *const test) {
    test_case_begin(test, "security: a control byte in a policy value logs WARN and omits only that header (heap tier cannot refuse whole)");

    char        log_buffer[512]        = DEFAULT_INITIALIZATION;
    LogLevel const previous_level      = log_get_level();

    log_set_level(LOG_LEVEL_WARN);
    log_set_buffer(log_buffer, sizeof(log_buffer));

    HTTP_Headers_Security policy = http_headers_security_init_2("csp\r\nX-Injected: 1", "DENY", "no-referrer", "perm", false, false, false, false, false, 0, false, false);
    String block = http_headers_security_create_2(&policy);

    test_expect_true(test, "the CRLF-carrying Content-Security-Policy value is omitted, not injected", string_find_count_1(&block, "Content-Security-Policy") == 0);
    test_expect_true(test, "no injected header line leaked through", string_find_count_1(&block, "X-Injected") == 0);
    test_expect_true(test, "an unaffected field's line still renders", string_find_count_1(&block, "X-Frame-Options: DENY\r\n") == 1);
    test_expect_true(test, "the substitution is WARN-logged (Mid-1: was silent before)", char_length(log_buffer) > 0);

    log_set_buffer(nullptr, 0);
    log_set_level(previous_level);

    string_uninit(&block);
    http_headers_security_uninit(&policy);

    test_case_end(test);
}

static void _test_security_uninit(Test *const test) {
    test_case_begin(test, "security: uninit resets every field");

    HTTP_Headers_Security policy = http_headers_security_init_1();
    http_headers_security_uninit(&policy);

    test_expect_u(test, "content_security_policy is empty", 0, string_get_size(&policy.content_security_policy));
    test_expect_u(test, "frame_options is empty", 0, string_get_size(&policy.frame_options));
    test_expect_u(test, "permissions_policy is empty", 0, string_get_size(&policy.permissions_policy));
    test_expect_u(test, "referrer_policy is empty", 0, string_get_size(&policy.referrer_policy));
    test_expect_false(test, "content_type_options reset", policy.content_type_options);
    test_expect_false(test, "cross_origin_embedder_policy reset", policy.cross_origin_embedder_policy);
    test_expect_false(test, "cross_origin_opener_policy reset", policy.cross_origin_opener_policy);
    test_expect_false(test, "cross_origin_resource_policy reset", policy.cross_origin_resource_policy);
    test_expect_false(test, "strict_transport_security reset", policy.strict_transport_security);
    test_expect_false(test, "strict_transport_security_include_subdomains reset", policy.strict_transport_security_include_subdomains);
    test_expect_u(test, "strict_transport_security_max_age reset", 0, policy.strict_transport_security_max_age);
    test_expect_false(test, "strict_transport_security_preload reset", policy.strict_transport_security_preload);
    test_expect_null(test, "allocator reset (unconditional field, 2026-09 arena ruling)", (void*) policy.allocator);

    test_case_end(test);
}

#ifdef ARENA_IMPLEMENTATION
static void _test_security_arena(Test *const test) {
    test_case_begin(test, "security: arena tier happy path matches the heap tier");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);
    HTTP_Headers_Security policy = DEFAULT_INITIALIZATION;

    bool const initialized = http_headers_security_alloc_init_1(&policy, &arena);

    test_expect_true(test, "alloc_init_1 succeeds on a healthy arena", initialized);

    String block = http_headers_security_create_2(&policy);
    String heap_block = http_headers_security_create_1();

    test_expect_string(test, "arena-backed default policy matches the heap default byte for byte", string_get_data(&heap_block), string_get_data(&block));

    string_uninit(&heap_block);
    string_uninit(&block);
    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_security_alloc_control_byte_refuses_whole(Test *const test) {
    test_case_begin(test, "security: alloc_init_2 refuses the WHOLE policy (Mid-1) when a value has a control byte, unlike the heap tier");

    Arena                   arena           = arena_init_1(4096, ARENA_TYPE_LINEAR);
    HTTP_Headers_Security   policy          = DEFAULT_INITIALIZATION;
    char                    log_buffer[512] = DEFAULT_INITIALIZATION;
    LogLevel const          previous_level  = log_get_level();

    log_set_level(LOG_LEVEL_WARN);
    log_set_buffer(log_buffer, sizeof(log_buffer));

    bool const initialized = http_headers_security_alloc_init_2(&policy,
        "csp\r\nX-Injected: 1", "DENY", "no-referrer", "perm", false, false, false, false, false, 0, false, false, &arena);

    test_expect_false(test, "a control byte in one field refuses the WHOLE policy, not just that field", initialized);
    test_expect_null(test, "content_security_policy carries no data after refusal", (void*) string_get_data(&policy.content_security_policy));
    test_expect_null(test, "frame_options carries no data either - the whole policy is gone, not just the offending field", (void*) string_get_data(&policy.frame_options));
    test_expect_null(test, "allocator is cleared", (void*) policy.allocator);
    test_expect_true(test, "the refusal is WARN-logged", char_length(log_buffer) > 0);

    log_set_buffer(nullptr, 0);
    log_set_level(previous_level);

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}
#endif // ARENA_IMPLEMENTATION

/*==============================================================================
 * MARK: - Entry Point
 *============================================================================*/

int main(void) {
    LogConfig const log_config = { .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true };

    log_init(log_config);

    Test test = test_init("tests/http/headers/test_all.c");

    test_suite_begin(&test, "http/headers");
    _test_cache(&test);
    _test_cache_max_age_into(&test);
    _test_content(&test);
    _test_security_default(&test);
    _test_security_bools(&test);
    _test_security_hsts(&test);
    _test_security_empty_value(&test);
    _test_security_control_byte_refusal(&test);
    _test_security_uninit(&test);
#ifdef ARENA_IMPLEMENTATION
    _test_cache_arena(&test);
    _test_content_arena(&test);
    _test_security_arena(&test);
    _test_security_alloc_control_byte_refuses_whole(&test);
#endif // ARENA_IMPLEMENTATION
    test_suite_end(&test);

    return test_uninit(&test);
}