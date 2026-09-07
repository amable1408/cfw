/*
 * test_unchecked.c - the http/service/cors suite, built WITHOUT ERROR_CHECK_ENABLED.
 *
 * Most of it is a VALUE-dependent refusal: an empty origin, a malformed header name, a
 * max_age of 0, a wildcard origin under credentials. Each one has to hold when every
 * error_check_* call compiles to nothing, because each one is reachable from a
 * configuration file or from the network - never from a caller bug alone.
 *
 * The last case is a different class, and it can only live here: abort-versus-degrade on an
 * exhausted arena is an ERROR_CHECK_ENABLED property (arena_linear_alloc aborts,
 * arena_linear_try_alloc answers nullptr), so the size-accounted discard inside
 * preflight_create and headers_create is unreachable in the checked suite and reachable in
 * this one.
 *
 * Contract violations (a null self) are NOT exercised here: with the checks off they are
 * undefined behaviour by design, not a refusal.
 */
#include <stdio.h>

#include <http/service/cors/cors.h>
#include <log/log.h>
#include <test/test.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/
#define _ARENA_SEARCH_CEILING   8192
#define _ARENA_SEARCH_STRIDE    16

/*==============================================================================
 * MARK: - Cases
 *============================================================================*/
static void _test_empty_values_refuse(Test *const test) {
    test_case_begin(test, "unchecked: an empty or malformed value is refused, never an abort");

    HTTP_Service_CORS cors = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_1 succeeds", http_service_cors_init_1(&cors));

    test_expect_false(test, "an empty origin is refused", http_service_cors_origin_add(&cors, ""));
    test_expect_false(test, "an empty header is refused", http_service_cors_header_add(&cors, ""));
    test_expect_false(test, "an empty exposed header is refused", http_service_cors_exposed_header_add(&cors, ""));
    test_expect_false(test, "an empty method is refused", http_service_cors_method_add(&cors, ""));
    test_expect_false(test, "a CRLF origin is refused", http_service_cors_origin_add(&cors, "https://a\r\nX: y"));
    test_expect_false(test, "a spaced header name is refused", http_service_cors_header_add(&cors, "X Bad"));

    test_expect_u(test, "no origin reached the list", 0, al_str_get_size(&cors.origins));
    test_expect_u(test, "the header list still holds its three defaults", 3, al_str_get_size(&cors.headers));

    test_expect_false(test, "an empty origin is not allowed either", http_service_cors_origin_allowed_1(&cors, ""));
    test_expect_false(test, "an empty method is not allowed", http_service_cors_method_allowed_1(&cors, ""));
    test_expect_true(test, "an empty header list is allowed", http_service_cors_headers_allowed_1(&cors, ""));

    http_service_cors_uninit(&cors);

    test_case_end(test);
}

static void _test_zero_max_age(Test *const test) {
    test_case_begin(test, "unchecked: a max_age of 0 initializes and is emitted");

    HTTP_Service_CORS cors = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 accepts 0", http_service_cors_init_2(&cors, false, 0));
    test_expect_u(test, "and keeps it", 0, cors.max_age);
    test_expect_true(test, "origin_add stores", http_service_cors_origin_add(&cors, "https://a.test"));

    String block = http_service_cors_preflight_create(&cors, "https://a.test", "POST", "content-type", false);

    char text[2048] = DEFAULT_INITIALIZATION;

    if (string_get_size(&block) > 0 && string_get_size(&block) < sizeof(text)) {
        char_copy_3(text, sizeof(text), string_get_data(&block), string_get_size(&block));
    }

    test_expect_string_contains(test, "the zero max age is emitted", text, "Access-Control-Max-Age: 0");

    string_uninit(&block);
    http_service_cors_uninit(&cors);

    test_case_end(test);
}

static void _test_wildcard_under_credentials(Test *const test) {
    test_case_begin(test, "unchecked: the wildcard origin stays refused under credentials");

    HTTP_Service_CORS cors = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 with credentials succeeds", http_service_cors_init_2(&cors, true, 600));
    test_expect_false(test, "origin_add(*) is refused", http_service_cors_origin_add(&cors, "*"));
    test_expect_u(test, "and nothing was stored", 0, al_str_get_size(&cors.origins));

    http_service_cors_uninit(&cors);

    test_case_end(test);
}

/* The discard tests/http/service/cors/test_all.c cannot drive. In a CHECKED build an
 * exhausted linear arena ends the process inside arena_linear_alloc's
 * error_check_out_of_bound_uint before string_reserve can refuse anything, so the
 * size-accounted guards in *_create are unreachable there. Here every error_check_*
 * compiles to nothing: arena_linear_try_alloc answers nullptr, string_reserve leaves the
 * String untouched, an appended line never lands, the totals disagree - and that IS the
 * discard, an EMPTY block rather than a half-written one.
 *
 * The capacity is SEARCHED, never hardcoded. It has to be large enough for alloc_init_1
 * (four lists, nine defaults, four rendered lines) and too small for the ~200 bytes a
 * response block needs, and that window moves with any one of those sizes. Stepping down
 * from a ceiling that comfortably passes init finds it wherever it currently sits.
 *
 * Not vacuous: delete the size comparison in either *_create and the block comes back
 * non-empty (the lines that DID fit), so `found` never turns true. */
/* Builds one block on a linear arena of exactly `capacity` bytes and answers its size.
 * `configured` says whether the service was even constructible there - below some capacity
 * alloc_init_1 refuses, and a block that was never attempted proves nothing. */
static USize _starved_block_size(USize const capacity, bool const preflight, bool *const configured, USize *const list_sizes) {
    Arena               arena   = arena_init_1(capacity, ARENA_TYPE_LINEAR);
    HTTP_Service_CORS   cors    = DEFAULT_INITIALIZATION;
    USize               size    = 0;

    *configured = false;
    *list_sizes = 0;

    if (http_service_cors_alloc_init_1(&cors, &arena)) {
        if (http_service_cors_origin_add(&cors, "https://a.test")) {
            *configured = true;

            String block = preflight ?
                http_service_cors_preflight_create(&cors, "https://a.test", "GET", "x-a", false) :
                http_service_cors_headers_create(&cors, "https://a.test");

            size        = string_get_size(&block);
            *list_sizes = al_str_get_size(&cors.origins) + al_str_get_size(&cors.methods) + al_str_get_size(&cors.headers);

            string_uninit(&block);
        }

        http_service_cors_uninit(&cors);
    }

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    return size;
}

/* The whole capacity range is swept rather than one interesting point, because the guards'
 * contract is a UNIVERSAL one: at EVERY capacity the block is either the sum of its lines
 * or nothing at all - never the 43-byte "Vary + Allow-Origin:" fragment a browser would
 * read as a policy. A String grows geometrically and a linear arena reclaims nothing, so a
 * block costs several borrows, and the capacities between "the last borrow fits" and "the
 * first borrow fits" are exactly the partial-write band. That band is what the size
 * accounting turns into an empty block.
 *
 * Not vacuous, and this is the shape a narrower test got wrong: searching for the largest
 * STARVING capacity passes with the guards deleted, because the search simply walks further
 * down to where not even the first borrow fits and the block is empty on its own. Sweeping
 * for a PARTIAL block cannot be satisfied that way - delete either comparison and the
 * fragment appears immediately. */
static void _test_exhausted_arena_discards(Test *const test) {
    test_case_begin(test, "unchecked: an exhausted arena degrades, so *_create discards the block");

    bool    configured          = false;
    USize   lists               = 0;
    USize   full_preflight      = _starved_block_size(_ARENA_SEARCH_CEILING, true, &configured, &lists);
    USize   full_headers        = _starved_block_size(_ARENA_SEARCH_CEILING, false, &configured, &lists);
    USize   partial_preflight   = 0;
    USize   partial_headers     = 0;
    USize   starved_lists       = 0;
    bool    empty_seen          = false;

    test_expect_true(test, "the search ceiling builds a whole preflight block", full_preflight > 0);
    test_expect_true(test, "and a whole actual-request block", full_headers > 0);

    for (USize capacity = _ARENA_SEARCH_CEILING; capacity >= _ARENA_SEARCH_STRIDE; capacity -= _ARENA_SEARCH_STRIDE) {
        USize const size = _starved_block_size(capacity, true, &configured, &lists);

        if (configured && size != 0 && size != full_preflight) {
            partial_preflight = size;
        }

        if (configured && size == 0) {
            empty_seen      = true;
            starved_lists   = lists;
        }

        USize const plain = _starved_block_size(capacity, false, &configured, &lists);

        if (configured && plain != 0 && plain != full_headers) {
            partial_headers = plain;
        }
    }

    test_expect_true(test, "some capacity starved a create into the EMPTY block", empty_seen);
    test_expect_u(test, "no capacity ever shipped a PARTIAL preflight block", 0, partial_preflight);
    test_expect_u(test, "nor a partial actual-request block", 0, partial_headers);
    test_expect_u(test, "and a discard left origin + six methods + three headers in place", 10, starved_lists);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Entry point
 *============================================================================*/
I32 main(void) {
    LogConfig const log_config = { .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true };

    log_init(log_config);

    Test test = test_init("http_service_cors_unchecked");

    test_suite_begin(&test, "cors unchecked");

    _test_empty_values_refuse(&test);
    _test_zero_max_age(&test);
    _test_wildcard_under_credentials(&test);
    _test_exhausted_arena_discards(&test);

    test_suite_end(&test);

    return test_uninit(&test);
}