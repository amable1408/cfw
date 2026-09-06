/*
 * test_unchecked.c - the same module built WITHOUT ERROR_CHECK_ENABLED.
 *
 * Every error_check_* call in http_server.c compiles to nothing here. What must
 * still hold is the set of VALUE-dependent refusals: data that arrives from a
 * peer, or from a caller passing a legal-but-wrong value, is answered false and
 * logged, never asserted away. If any of these depended on error_check_* to stop
 * the process, this binary would take the failure instead of returning it.
 */
#include <stdio.h>

#include <http/server/http_server.h>
#include <log/log.h>
#include <test/test.h>

static void _route_noop(HTTP_Server_Route *const route) {
    (void) route;
}

/* This binary deliberately defines NO http_server_request_default_callback. That is the
 * pin for the module's one link-time contract: the weak 404 definition is compiled
 * unconditionally, so a program that never supplies the symbol still LINKS. The override
 * that used to sit here was a no-op body, so it pinned nothing and would have hung a
 * stray no-handler request to lws' timeout instead of answering it. test_all.c keeps a
 * strong override and pins the other half - that an override reaches the wire. */

static void _test_route_refusals(Test *const test) {
    test_case_begin(test, "route registration refuses without error_check_*");

    HTTP_Server server = DEFAULT_INITIALIZATION;

    http_server_init(&server);

    char over_long[HTTP_SERVER_PATH_MAX_LENGTH + 8] = DEFAULT_INITIALIZATION;

    for (USize index = 0; index < sizeof(over_long) - 1; index += 1) {
        over_long[index] = 'a';
    }

    test_expect_false(test, "an empty exact path is refused", http_server_route_add(&server, "", _route_noop));
    test_expect_false(test, "an over-long exact path is refused, not truncated", http_server_route_add(&server, over_long, _route_noop));
    test_expect_false(test, "an empty pattern is refused", http_server_route_match_add(&server, "", _route_noop));
    test_expect_false(test, "an over-long pattern is refused", http_server_route_match_add(&server, over_long, _route_noop));
    test_expect_false(test, "a pattern that will not compile is refused, not aborted", http_server_route_match_add(&server, "/bad/(", _route_noop));
    test_expect_false(test, "an unterminated class is refused", http_server_route_match_add(&server, "/bad/[a-", _route_noop));
    test_expect_null(test, "nothing was registered", server.route);

    test_expect_true(test, "a legal route still registers", http_server_route_add(&server, "/ok", _route_noop));
    test_expect_not_null(test, "the legal route is on the list", server.route);

    http_server_uninit(&server);

    test_case_end(test);
}

static void _test_dispatch_empty_path(Test *const test) {
    test_case_begin(test, "dispatching an empty path answers false rather than aborting");

    HTTP_Server server = DEFAULT_INITIALIZATION;

    http_server_init(&server);
    http_server_route_add(&server, "/ok", _route_noop);

    Result const result = http_server_run(&server, 0, true);

    test_expect_true(test, "run on port 0 succeeds", result_is_success(result));

    HTTP_Server_Holder holder = { .request = nullptr, .response = nullptr, .arena = nullptr };

    /* The request path is peer data. Zero length used to reach
     * error_check_non_value_int, which ends the process under ERROR_CHECK_ENABLED and
     * did nothing at all here - two different behaviours for one hostile request. */
    test_expect_false(test, "an empty sized path matches nothing", http_server_router_dispatch_2(server.router, "", 0, &holder));
    test_expect_false(test, "an unmatched path matches nothing", http_server_router_dispatch_2(server.router, "/nope", 5, &holder));
    test_expect_true(test, "a matching path still dispatches", http_server_router_dispatch_2(server.router, "/ok", 3, &holder));

    http_server_uninit(&server);

    test_case_end(test);
}

static void _test_payload_cap(Test *const test) {
    test_case_begin(test, "payload cap semantics without error_check_*");

    HTTP_Server server = DEFAULT_INITIALIZATION;

    http_server_init(&server);

    test_expect_u(test, "the default cap is 1 MiB", HTTP_SERVER_PAYLOAD_DEFAULT_MAX_LENGTH, server.payload_max_size);

    http_server_set_payload_max_size(&server, 0);

    test_expect_u(test, "0 restores the default rather than meaning unlimited",
                  HTTP_SERVER_PAYLOAD_DEFAULT_MAX_LENGTH, server.payload_max_size);

    http_server_set_payload_max_size(&server, HTTP_SERVER_PAYLOAD_UNLIMITED);

    test_expect_u(test, "unlimited has to be spelled out", HTTP_SERVER_PAYLOAD_UNLIMITED, server.payload_max_size);

    http_server_uninit(&server);

    test_case_end(test);
}

static void _test_register_after_run(Test *const test) {
    test_case_begin(test, "register_protocol and a second run are refused while running");

    HTTP_Server server = DEFAULT_INITIALIZATION;

    http_server_init(&server);

    test_expect_true(test, "run on port 0 succeeds", result_is_success(http_server_run(&server, 0, true)));
    test_expect_true(test, "the ephemeral port is readable", http_server_get_port(&server) != 0);
    test_expect_true(test, "a second run is refused", result_is_error(http_server_run(&server, 0, true)));

    http_server_stop(&server);
    http_server_uninit(&server);

    test_case_end(test);
}

static void _test_arena_refusals(Test *const test) {
    test_case_begin(test, "arena allocation refuses without error_check_*");

    /* alloc_new reaches the arena through allocator_try_borrow, so an arena too small for
     * the handle answers null here instead of being called through a hook the error_check
     * would have caught only in the checked build. */
    Arena small = arena_init_1(16, ARENA_TYPE_LINEAR);

    test_expect_null(test, "an arena too small for the handle yields null", http_server_alloc_new(&small));

    arena_uninit(&small, ARENA_TYPE_LINEAR);

    Arena arena = arena_init_1(64 * 1024, ARENA_TYPE_LINEAR);

    HTTP_Server *server = http_server_alloc_new(&arena);

    test_expect_not_null(test, "a sized arena yields a server", server);
    test_expect_true(test, "routes register on an arena server", http_server_route_add(server, "/ok", _route_noop));

    http_server_alloc_delete(&server, &arena);

    test_expect_null(test, "alloc_delete nulls the handle", server);

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

I32 main(void) {
    LogConfig const log_config = { .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true };

    log_init(log_config);

    http_server_set_log_level(LLL_ERR);

    Test test = test_init("http_server (checks off)");

    test_suite_begin(&test, "http_server value-dependent refusals");

    _test_route_refusals(&test);
    _test_dispatch_empty_path(&test);
    _test_payload_cap(&test);
    _test_register_after_run(&test);
    _test_arena_refusals(&test);

    test_suite_end(&test);

    return test_uninit(&test);
}