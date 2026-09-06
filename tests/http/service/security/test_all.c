#include <http/service/security/security.h>
#include <net/net.h>
#include <test/test.h>

/* security had no suite of its own, despite 11+ main_ / backend consumers. This pins the R1
 * behavior change (report High 5): the header block is now rendered ONCE at construction time
 * into self.header_block, so every constructor must leave a non-empty, byte-stable cache and
 * uninit must not double-free it or the caller's moved-in policy Strings.
 *
 * apply() links the REAL http_server.c and drives it exactly like
 * tests/http/service/static/test_all.c: http_server_run on port 0, the ephemeral port read back,
 * and a raw `net` socket client, so the security header block's bytes on the wire are under the
 * test's own control - no stub_response.c stand-in (see git history for the earlier stub-based
 * suite that could not reach apply() at all). */

/*==============================================================================
 * MARK: - Raw client (modelled on tests/http/service/static/test_all.c)
 *============================================================================*/
#define _CLIENT_HEADERS_MAX     8192
#define _CLIENT_IO_TIMEOUT_MS   4000
#define _CLIENT_RAW_MAX         16384

typedef struct {
    U16     status;
    char    headers[_CLIENT_HEADERS_MAX];
    USize   headers_size;
} _Reply;

static char _raw[_CLIENT_RAW_MAX];

static bool _client_open(U16 const port, Net_Socket *const out) {
    Net_Socket_Address address = DEFAULT_INITIALIZATION;

    if (result_is_error(net_socket_address_init_2(NET_FAMILY_IPV4, port, "127.0.0.1", &address))) {
        return false;
    }

    if (result_is_error(net_socket_init(out, NET_FAMILY_IPV4, NET_TYPE_TCP))) {
        return false;
    }

    if (result_is_error(net_socket_connect(*out, &address))) {
        net_socket_close(*out);

        return false;
    }

    net_socket_set_timeout(*out, _CLIENT_IO_TIMEOUT_MS);
    net_socket_set_nodelay(*out, true);

    return true;
}

static bool _client_write(Net_Socket const socket, void const *const data, USize const size) {
    USize sent_total = 0;

    while (sent_total < size) {
        USize   sent    = 0;
        Result  result  = net_socket_send_1(socket, (Byte const*) data + sent_total, size - sent_total, &sent);

        if (result_is_error(result) || sent == 0) {
            return false;
        }

        sent_total += sent;
    }

    return true;
}

/** @brief Read one HTTP/1.1 reply's status line and header block (body is unused here). */
static bool _client_read(Net_Socket const socket, _Reply *const reply) {
    USize raw_size = 0;

    *reply = (_Reply) DEFAULT_INITIALIZATION;

    while (raw_size + 1 < sizeof(_raw)) {
        USize   received    = 0;
        Result  result      = net_socket_recv_1(socket, _raw + raw_size, sizeof(_raw) - 1 - raw_size, &received);

        if (result_is_error(result) || received == 0) {
            break;
        }

        raw_size = raw_size + received;
        _raw[raw_size] = '\0';

        char const *const terminator = char_find_slice_5(_raw, raw_size, 0, "\r\n\r\n", 4);

        if (terminator != nullptr) {
            USize const header_end = (USize) (terminator - _raw) + 4;

            reply->headers_size = header_end < sizeof(reply->headers) ? header_end : sizeof(reply->headers) - 1;

            memory_copy_1((Byte*) reply->headers, (Byte*) _raw, reply->headers_size);

            reply->headers[reply->headers_size] = '\0';

            if (raw_size >= 12) {
                reply->status = (U16) (((_raw[9] - '0') * 100) + ((_raw[10] - '0') * 10) + (_raw[11] - '0'));
            }

            return true;
        }
    }

    return false;
}

/** @brief One request, one reply, one connection. */
static bool _client_round_trip(U16 const port, char const *const request, _Reply *const reply) {
    Net_Socket socket = DEFAULT_INITIALIZATION;

    if (!_client_open(port, &socket)) {
        return false;
    }

    bool ok = _client_write(socket, request, char_length(request));

    if (ok) {
        ok = _client_read(socket, reply);
    }

    net_socket_close(socket);

    return ok;
}

/*==============================================================================
 * MARK: - Server wiring
 *============================================================================*/
static HTTP_Service_Security _svc;

static void _handler(void *context, HTTP_Server_Request *request, HTTP_Server_Response *response) {
    (void) context;
    (void) request;

    http_service_security_apply(&_svc, response);
    http_server_response_send_1(response, "ok", HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_OK);
}

/* The module calls this only when no per-server handler is set. _start always sets one, so this
 * is never reached; it exists only so the extern resolves at link time. */
void http_server_request_default_callback(void *context, HTTP_Server_Request *request, HTTP_Server_Response *response) {
    (void) context;
    (void) request;

    http_server_response_send_1(response, "default", HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_OK);
}

static U16 _start(Test *const test, HTTP_Server *const server) {
    http_server_set_handler(server, _handler, server);

    Result const result = http_server_run(server, 0, true);

    test_expect_true(test, "http_server_run succeeds on port 0", result_is_success(result));

    U16 const port = http_server_get_port(server);

    test_expect_true(test, "an ephemeral port was assigned", port != 0);

    return port;
}

/*==============================================================================
 * MARK: - Cases
 *============================================================================*/
static void _test_security_init_1_default_block(Test *const test) {
    test_case_begin(test, "init_1 renders a non-empty default header block at construction");

    HTTP_Service_Security security = http_service_security_init_1();

    test_expect_true(test, "the cached block is non-empty", !string_empty(&security.header_block));

    http_service_security_uninit(&security);

    test_case_end(test);
}

static void _test_security_init_2_custom_policy(Test *const test) {
    test_case_begin(test, "init_2 renders the custom policy's fields into the cache");

    HTTP_Headers_Security policy = http_headers_security_init_2(
        "default-src 'self'", "DENY", "no-referrer", "",
        true, false, true, false,
        true, 31536000, true, true);

    HTTP_Service_Security security = http_service_security_init_2(&policy);

    test_expect_true(test, "the cache is non-empty", !string_empty(&security.header_block));
    test_expect_true(test, "X-Frame-Options: DENY is in the rendered cache", char_find_exists_1(string_get_data(&security.header_block), "X-Frame-Options: DENY"));

    http_service_security_uninit(&security);

    test_case_end(test);
}

static void _test_security_init_2_moves_source_policy(Test *const test) {
    test_case_begin(test, "init_2 zeroes the caller's policy - a stray http_headers_security_uninit on it afterward is a no-op, not a double-free");

    HTTP_Headers_Security policy = http_headers_security_init_2(
        "default-src 'self'", "DENY", "no-referrer", "",
        true, false, true, false,
        true, 31536000, true, true);

    HTTP_Service_Security security = http_service_security_init_2(&policy);

    test_expect_true(test, "the source policy's String fields read empty after the move", string_empty(&policy.content_security_policy) && string_empty(&policy.frame_options));

    /* The type-level guarantee this pins: a caller who forgets init_2 moved the policy and calls
     * uninit on it anyway hits string_uninit on an already-empty String, not a second free. */
    http_headers_security_uninit(&policy);

    http_service_security_uninit(&security);

    test_case_end(test);
}

static void _test_security_uninit_is_safe_to_repeat_fields(Test *const test) {
    test_case_begin(test, "uninit clears the cache so a stray re-read sees empty, not freed memory");

    HTTP_Service_Security security = http_service_security_init_1();

    http_service_security_uninit(&security);

    test_expect_true(test, "the header block reads empty after uninit", string_empty(&security.header_block));

    test_case_end(test);
}

#ifdef ARENA_IMPLEMENTATION
static void _test_security_alloc_init_1_renders_cache(Test *const test) {
    test_case_begin(test, "alloc_init_1 renders a non-empty default header block");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    HTTP_Service_Security security = DEFAULT_INITIALIZATION;

    test_expect_true(test, "a properly sized arena initializes", http_service_security_alloc_init_1(&security, &arena));
    test_expect_true(test, "the cache is non-empty", !string_empty(&security.header_block));

    http_service_security_uninit(&security);
    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_security_alloc_init_2_renders_cache(Test *const test) {
    test_case_begin(test, "alloc_init_2 renders the cache from an arena-backed policy");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    HTTP_Headers_Security policy = DEFAULT_INITIALIZATION;

    test_expect_true(test, "the policy itself initializes", http_headers_security_alloc_init_1(&policy, &arena));

    HTTP_Service_Security security = http_service_security_alloc_init_2(&policy, &arena);

    test_expect_true(test, "the cache is non-empty", !string_empty(&security.header_block));

    http_service_security_uninit(&security);
    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}
#endif // ARENA_IMPLEMENTATION

static void _test_security_apply_wire(Test *const test, U16 const port) {
    test_case_begin(test, "apply() writes the exact cached header block, byte for byte, on every response");

    _Reply reply1 = DEFAULT_INITIALIZATION;
    _Reply reply2 = DEFAULT_INITIALIZATION;

    char const *const expected      = string_get_data(&_svc.header_block);
    USize const       expected_size = string_get_size(&_svc.header_block);

    if (test_expect_true(test, "first round trip",
                         _client_round_trip(port, "GET / HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply1))) {
        test_expect_u(test, "first response status 200", 200, reply1.status);

        char const *const found1 = char_find_slice_5(reply1.headers, reply1.headers_size, 0, (char*) expected, expected_size);

        test_expect_true(test, "the cached block appears byte-exact on the wire (1st response)", found1 != nullptr);
    }

    if (test_expect_true(test, "second round trip (same instance, separate connection)",
                         _client_round_trip(port, "GET / HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply2))) {
        test_expect_u(test, "second response status 200", 200, reply2.status);

        char const *const found2 = char_find_slice_5(reply2.headers, reply2.headers_size, 0, (char*) expected, expected_size);

        test_expect_true(test, "the cached block appears byte-exact on the wire (2nd response)", found2 != nullptr);
    }

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Entry point
 *============================================================================*/
int main(void) {
    LogConfig const log_config = {
        .level             = LOG_LEVEL_ERROR,
        .stream            = stdout,
        .timestamp_enabled = true,
        .autoflush         = true
    };

    log_init(log_config);

    http_server_set_log_level(LLL_ERR);

    Test test = test_init("./test_all.c");

    test_verbose_set(&test, false);

    test_suite_begin(&test, "http_service_security");
    _test_security_init_1_default_block(&test);
    _test_security_init_2_custom_policy(&test);
    _test_security_init_2_moves_source_policy(&test);
    _test_security_uninit_is_safe_to_repeat_fields(&test);
#ifdef ARENA_IMPLEMENTATION
    _test_security_alloc_init_1_renders_cache(&test);
    _test_security_alloc_init_2_renders_cache(&test);
#endif // ARENA_IMPLEMENTATION

    _svc = http_service_security_init_1();

    HTTP_Server *server = http_server_new();
    U16 const    port   = _start(&test, server);

    _test_security_apply_wire(&test, port);

    http_server_delete(&server);
    http_service_security_uninit(&_svc);

    test_suite_end(&test);

    return test_uninit(&test);
}