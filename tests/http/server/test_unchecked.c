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
#include <net/net.h>
#include <test/test.h>

#define _CLIENT_IO_TIMEOUT_MS   4000
#define _CLIENT_RAW_MAX         16384
#define _DECIMAL_RADIX          10
/* A fixture Windows still reports open (the server thread's close, an indexer's or a sync
 * client's brief hold on a fresh file) is retried this often before it counts as stuck. */
#define _REMOVE_ATTEMPTS        40
#define _REMOVE_SLEEP_MS        25
/* "HTTP/1.1 200": the three status digits start after the version and its space. */
#define _STATUS_CODE_DIGITS     3
#define _STATUS_CODE_OFFSET     9
#define _STREAM_FIXTURE         "http_server_unchecked_stream_fixture.bin"
#define _STREAM_FIXTURE_SIZE    4096
/* The past-EOF refusal asks for a window that starts this close to the end and runs past it. */
#define _STREAM_PAST_EOF_BACK   5
#define _STREAM_WINDOW_SIZE     10

static void _route_noop(HTTP_Server_Route *const route) {
    (void) route;
}

/*==============================================================================
 * MARK: - send_file_handle refusals
 * The seam's three VALUE refusals: headers already sent, a window past EOF, a handle that
 * is not a regular file. Each must answer false with nothing written in THIS build, where
 * every error_check_* is gone - a refusal that leaned on one would stream here.
 *============================================================================*/
typedef enum {
    _STREAM_MODE_AFTER_SEND,
    _STREAM_MODE_PAST_EOF,
    _STREAM_MODE_IRREGULAR
} _Stream_Mode;

static _Stream_Mode _stream_mode    = _STREAM_MODE_AFTER_SEND;
static bool         _stream_ok      = true;

static void _route_stream(HTTP_Server_Route *const route) {
    HTTP_Server_Holder *const holder = http_server_route_get_holder(route);

    if (_stream_mode == _STREAM_MODE_AFTER_SEND) {
        http_server_response_send_1(holder->response, "first", HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_OK);
    }

    /* Linux: a directory opens and file_regular refuses it. Windows: fopen refuses a
     * directory and file_regular is tautologically true, so the NUL device - a handle
     * with no file size - stands in, refused by the window check instead. */
#ifdef _WIN32
    File *file = _stream_mode == _STREAM_MODE_IRREGULAR ? file_open_try_1("NUL", "rb") : file_open_try_1(_STREAM_FIXTURE, "rb");
#else
    File *file = _stream_mode == _STREAM_MODE_IRREGULAR ? file_open_try_1(".", "rb") : file_open_try_1(_STREAM_FIXTURE, "rb");
#endif // _WIN32

    if (memory_empty(file)) {
        http_server_response_send_1(holder->response, "no handle", HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_INTERNAL_SERVER_ERROR);

        return;
    }

    USize const start   = _stream_mode == _STREAM_MODE_PAST_EOF ? _STREAM_FIXTURE_SIZE - _STREAM_PAST_EOF_BACK : 0;
    USize const size    = _stream_mode == _STREAM_MODE_AFTER_SEND ? _STREAM_FIXTURE_SIZE : _STREAM_WINDOW_SIZE;

    _stream_ok = http_server_response_send_file_handle(holder->response, &file, start, size, HTTP_SERVER_CONTENT_TYPE_APPLICATION_OCTET_STREAM, HTTP_SERVER_STATUS_CODE_OK);

    if (!memory_empty(file)) {
        file_close(&file);
    }

    if (_stream_mode != _STREAM_MODE_AFTER_SEND) {
        http_server_response_send_1(holder->response, "fallback", HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_NOT_FOUND);
    }
}

static void _handler(void *context, HTTP_Server_Request *request, HTTP_Server_Response *response) {
    HTTP_Server         *const  server  = (HTTP_Server*) context;
    HTTP_Server_Holder          holder  = { .request = request, .response = response, .arena = nullptr };

    if (!http_server_router_dispatch_2(server->router, http_server_request_get_path_1(request), http_server_request_get_path_size(request), &holder)) {
        http_server_response_send_1(response, "no route", HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_NOT_FOUND);
    }
}

/** @brief One request on one closing connection, everything the peer writes until EOF. */
static bool _client_fetch(U16 const port, char const *const request, char *const out, USize const capacity, USize *const out_size) {
    Net_Socket_Address  address = DEFAULT_INITIALIZATION;
    Net_Socket          socket  = DEFAULT_INITIALIZATION;

    if (result_is_error(net_socket_address_init_2(NET_FAMILY_IPV4, port, "127.0.0.1", &address)) || result_is_error(net_socket_init(&socket, NET_FAMILY_IPV4, NET_TYPE_TCP))) {
        return false;
    }

    if (result_is_error(net_socket_connect(socket, &address))) {
        net_socket_close(socket);

        return false;
    }

    net_socket_set_timeout(socket, _CLIENT_IO_TIMEOUT_MS);

    USize sent = 0;

    if (result_is_error(net_socket_send_1(socket, (Byte const*) request, char_length(request), &sent)) || sent != char_length(request)) {
        net_socket_close(socket);

        return false;
    }

    USize size = 0;

    while (size + CHAR_END_CHARACTER < capacity) {
        USize got = 0;

        if (result_is_error(net_socket_recv_1(socket, out + size, capacity - CHAR_END_CHARACTER - size, &got)) || got == 0) {
            break;
        }

        size += got;
    }

    out[size]   = '\0';
    *out_size   = size;

    net_socket_close(socket);

    return size > 0;
}

/** @brief The status of a raw reply: the three digits after "HTTP/1.1 ", or 0 when the line is too short. */
static U16 _reply_status(char const *const raw, USize const size) {
    if (size < _STATUS_CODE_OFFSET + _STATUS_CODE_DIGITS) {
        return 0;
    }

    U16 status = 0;

    for (USize index = 0; index < _STATUS_CODE_DIGITS; index += 1) {
        status = (U16) ((status * _DECIMAL_RADIX) + (U16) (raw[_STATUS_CODE_OFFSET + index] - '0'));
    }

    return status;
}

/** @brief A raw reply's body: what follows the header terminator, or the empty tail when there is none. A READ-ONLY view into raw - never written through, never released. */
static char* _reply_body(char const *const raw, USize const size) {
    char const *const terminator = size >= CHAR_STATIC_SIZE("\r\n\r\n") ? char_find_slice_5(raw, size, 0, "\r\n\r\n", CHAR_STATIC_SIZE("\r\n\r\n")) : nullptr;
    char const *const body       = memory_empty(terminator) ? raw + size : terminator + CHAR_STATIC_SIZE("\r\n\r\n");

    return (char*) body;
}

/** @brief Remove a fixture, retrying while Windows still refuses to delete a file something holds open. */
static bool _fixture_remove(char const *const name) {
    Result removed = file_remove_1(name);

    for (USize attempt = 0; attempt < _REMOVE_ATTEMPTS && removed != RESULT_SUCCESS; attempt += 1) {
        thread_sleep(_REMOVE_SLEEP_MS);

        removed = file_remove_1(name);
    }

    return removed == RESULT_SUCCESS;
}

static void _test_send_file_handle_refusals(Test *const test) {
    test_case_begin(test, "send_file_handle refuses by value without error_check_*");

    static char       raw[_CLIENT_RAW_MAX]         = DEFAULT_INITIALIZATION;
    static Byte const filler[_STREAM_FIXTURE_SIZE] = DEFAULT_INITIALIZATION;

    File *fixture = file_open_try_1(_STREAM_FIXTURE, "wb");

    if (test_expect_true(test, "the fixture is written", !memory_empty(fixture))) {
        file_write_1(fixture, filler, 1, sizeof(filler));
        file_close(&fixture);
    }

    HTTP_Server server = DEFAULT_INITIALIZATION;

    http_server_init(&server);
    http_server_set_handler(&server, _handler, &server);
    http_server_route_add(&server, "/stream", _route_stream);

    test_expect_true(test, "run on port 0 succeeds", result_is_success(http_server_run(&server, 0, true)));

    U16 const port = http_server_get_port(&server);

    USize size = 0;

    _stream_mode    = _STREAM_MODE_AFTER_SEND;
    _stream_ok      = true;

    if (test_expect_true(test, "headers-sent fetch", _client_fetch(port, "GET /stream HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", raw, sizeof(raw), &size))) {
        test_expect_false(test, "the seam refuses once the headers are sent", _stream_ok);
        test_expect_u(test, "the first reply's status stands", 200, _reply_status(raw, size));
        test_expect_string(test, "and nothing follows its body on the wire", "first", _reply_body(raw, size));
    }

    _stream_mode    = _STREAM_MODE_PAST_EOF;
    _stream_ok      = true;

    if (test_expect_true(test, "past-EOF fetch", _client_fetch(port, "GET /stream HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", raw, sizeof(raw), &size))) {
        test_expect_false(test, "a window past EOF is refused by value", _stream_ok);
        test_expect_u(test, "the route's own 404 goes out", 404, _reply_status(raw, size));
        test_expect_string(test, "with the route's own body", "fallback", _reply_body(raw, size));
    }

    _stream_mode    = _STREAM_MODE_IRREGULAR;
    _stream_ok      = true;

    if (test_expect_true(test, "irregular-handle fetch", _client_fetch(port, "GET /stream HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", raw, sizeof(raw), &size))) {
        test_expect_false(test, "a handle that is not a regular file is refused by value", _stream_ok);
        test_expect_u(test, "the route's own 404 goes out", 404, _reply_status(raw, size));
        test_expect_string(test, "with the route's own body", "fallback", _reply_body(raw, size));
    }

    http_server_stop(&server);
    http_server_uninit(&server);

    test_expect_true(test, "the fixture is removed", _fixture_remove(_STREAM_FIXTURE));

    test_case_end(test);
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
    _test_send_file_handle_refusals(&test);

    test_suite_end(&test);

    return test_uninit(&test);
}