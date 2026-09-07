/*
 * test_all.c - the http/service/cors suite.
 *
 * Pure cases drive the policy object directly; live cases drive a REAL server -
 * http_server_run on port 0, the ephemeral port read back with http_server_get_port, and
 * a raw `net` socket for the client so the bytes on the wire are under the test's own
 * control (the shape tests/http/server/test_all.c established).
 */
#include <stdio.h>

#include <http/server/http_server.h>
#include <http/service/cors/cors.h>
#include <log/log.h>
#include <net/net.h>
#include <test/test.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/
#define _ARENA_REFUSAL_ROUNDS   2000
#define _ARENA_SMALL_CAPACITY   2048
#define _BLOCK_MAX              8192
#define _CLIENT_HEADERS_MAX     8192
#define _CLIENT_IO_TIMEOUT_MS   4000
#define _CLIENT_RAW_MAX         32768

/*==============================================================================
 * MARK: - Block reader
 *============================================================================*/
static char _block_text[_BLOCK_MAX] = DEFAULT_INITIALIZATION;

/** @brief A String's bytes as NUL-terminated text, so the contains assertions can read it. */
static char* _block(String const *const block) {
    USize const size = string_get_size(block);

    _block_text[0] = '\0';

    if (size > 0 && size < sizeof(_block_text)) {
        char_copy_3(_block_text, sizeof(_block_text), string_get_data(block), size);
    }

    return _block_text;
}

/*==============================================================================
 * MARK: - Raw client
 *============================================================================*/
typedef struct {
    U16     status;
    char    headers[_CLIENT_HEADERS_MAX];
} _Reply;

static char _raw[_CLIENT_RAW_MAX] = DEFAULT_INITIALIZATION;

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

static bool _client_write(Net_Socket const socket, char const *const data, USize const size) {
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

/** @brief Read one reply's status line and header block; the body is never needed here. */
static bool _client_read(Net_Socket const socket, _Reply *const reply) {
    USize raw_size      = 0;
    USize header_end    = 0;

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
            header_end = (USize) (terminator - _raw) + 4;

            break;
        }
    }

    if (header_end == 0) {
        return false;
    }

    char_copy_3(reply->headers, sizeof(reply->headers), _raw, header_end < sizeof(reply->headers) ? header_end : sizeof(reply->headers) - 1);

    if (raw_size >= 12) {
        reply->status = (U16) (((_raw[9] - '0') * 100) + ((_raw[10] - '0') * 10) + (_raw[11] - '0'));
    }

    return true;
}

static bool _round_trip(U16 const port, char const *const request, _Reply *const reply) {
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
 * MARK: - Live route
 *============================================================================*/
static HTTP_Service_CORS _live_cors = DEFAULT_INITIALIZATION;

/* The header's own OPTIONS branch, with one deliberate difference: the preflight block is
 * added even when the policy refuses, so the suite can prove Vary rides on a refusal too. */
static void _route_cors(HTTP_Server_Route *const route) {
    HTTP_Server_Holder *const holder = http_server_route_get_holder(route);

    char origin[256] = DEFAULT_INITIALIZATION;

    http_server_request_header_copy(holder->request, HTTP_SERVER_HEADER_ORIGIN, origin, sizeof(origin));

    if (char_compare_iequal_1(http_server_request_get_method_1(holder->request), "OPTIONS")) {
        char method[64]     = DEFAULT_INITIALIZATION;
        char headers[512]   = DEFAULT_INITIALIZATION;

        /* libwebsockets tokenizes Origin but NOT the two Access-Control-Request-* names,
         * so those two are read through the custom-header path. */
        http_server_request_custom_header_copy(holder->request, "Access-Control-Request-Method", method, sizeof(method));
        http_server_request_custom_header_copy(holder->request, "Access-Control-Request-Headers", headers, sizeof(headers));

        String block = http_service_cors_preflight_create(&_live_cors, origin, method, headers, false);

        if (!string_empty(&block)) {
            http_server_response_header_add_raw(holder->response, string_get_data(&block), string_get_size(&block));
        }

        string_uninit(&block);
        http_server_response_send_empty(holder->response, HTTP_SERVER_STATUS_CODE_NO_CONTENT);

        return;
    }

    String block = http_service_cors_headers_create(&_live_cors, origin);

    if (!string_empty(&block)) {
        http_server_response_header_add_raw(holder->response, string_get_data(&block), string_get_size(&block));
    }

    string_uninit(&block);
    http_server_response_send_1(holder->response, "ok", HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_OK);
}

/*==============================================================================
 * MARK: - Pure cases
 *============================================================================*/
static void _test_defaults(Test *const test) {
    test_case_begin(test, "init_1: in-place bool, credentials OFF, nine defaults stored");

    HTTP_Service_CORS cors = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_1 succeeds", http_service_cors_init_1(&cors));
    test_expect_false(test, "credentials are opt-in", cors.allow_credentials);
    test_expect_u(test, "default max age", HTTP_SERVICE_CORS_DEFAULT_MAX_AGE, cors.max_age);
    test_expect_u(test, "six default methods", 6, al_str_get_size(&cors.methods));
    test_expect_u(test, "three default headers", 3, al_str_get_size(&cors.headers));

    test_expect_true(test, "GET is allowed", http_service_cors_method_allowed_1(&cors, "GET"));
    test_expect_true(test, "OPTIONS is allowed", http_service_cors_method_allowed_1(&cors, "OPTIONS"));
    test_expect_false(test, "TRACE is not", http_service_cors_method_allowed_1(&cors, "TRACE"));
    test_expect_false(test, "an empty method is not", http_service_cors_method_allowed_1(&cors, ""));

    test_expect_true(test, "the default headers are allowed", http_service_cors_headers_allowed_1(&cors, "Authorization, Content-Type"));
    test_expect_false(test, "an unknown header is not", http_service_cors_headers_allowed_1(&cors, "X-Nope"));

    http_service_cors_uninit(&cors);

    test_expect_u(test, "uninit zeroes the max age", 0, cors.max_age);
    test_expect_u(test, "uninit empties the method list", 0, al_str_get_size(&cors.methods));

    test_case_end(test);
}

static void _test_origin_exact(Test *const test) {
    test_case_begin(test, "origins: exact match, tiers, and the request wildcard");

    HTTP_Service_CORS cors = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_1 succeeds", http_service_cors_init_1(&cors));
    test_expect_true(test, "origin_add stores", http_service_cors_origin_add(&cors, "https://a.test"));
    test_expect_true(test, "adding it again is success, not a duplicate", http_service_cors_origin_add(&cors, "https://a.test"));
    test_expect_u(test, "the list still holds one entry", 1, al_str_get_size(&cors.origins));

    test_expect_true(test, "the stored origin is allowed", http_service_cors_origin_allowed_1(&cors, "https://a.test"));
    test_expect_false(test, "another origin is not", http_service_cors_origin_allowed_1(&cors, "https://b.test"));
    test_expect_false(test, "a prefix of it is not", http_service_cors_origin_allowed_1(&cors, "https://a.tes"));
    test_expect_false(test, "an empty origin is not", http_service_cors_origin_allowed_1(&cors, ""));
    test_expect_false(test, "a literal * as the REQUEST origin is never allowed", http_service_cors_origin_allowed_1(&cors, "*"));

    test_expect_true(test, "the sized tier agrees", http_service_cors_origin_allowed_2(&cors, "https://a.test/ignored", CHAR_STATIC_SIZE("https://a.test")));

    Str origin          = str_init_static("https://a.test", CHAR_STATIC_SIZE("https://a.test"));
    Str origin_empty    = str_init_1();

    test_expect_true(test, "the Str tier agrees", http_service_cors_origin_allowed_3(&cors, &origin));
    test_expect_false(test, "an empty Str answers false, never a null deref", http_service_cors_origin_allowed_3(&cors, &origin_empty));

    str_uninit(&origin);
    str_uninit(&origin_empty);
    http_service_cors_uninit(&cors);

    test_case_end(test);
}

static void _test_origin_case_and_refusals(Test *const test) {
    test_case_begin(test, "origins: stored lowercased, and every malformed spelling refused");

    HTTP_Service_CORS cors = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_1 succeeds", http_service_cors_init_1(&cors));

    /* A browser always sends the host lowercase, so a mixed-case configuration entry used
     * to sit in the list matching nothing at all. */
    test_expect_true(test, "a mixed-case origin stores", http_service_cors_origin_add(&cors, "https://Example.com"));
    test_expect_true(test, "and matches the lowercase spelling a browser sends", http_service_cors_origin_allowed_1(&cors, "https://example.com"));
    test_expect_true(test, "and matches its own spelling", http_service_cors_origin_allowed_1(&cors, "https://EXAMPLE.com"));

    test_expect_false(test, "an empty origin is refused, not aborted", http_service_cors_origin_add(&cors, ""));
    test_expect_false(test, "a CRLF origin is refused", http_service_cors_origin_add(&cors, "https://a\r\nX: y"));
    test_expect_false(test, "a spaced origin is refused", http_service_cors_origin_add(&cors, "https://a b"));
    test_expect_false(test, "a comma origin is refused", http_service_cors_origin_add(&cors, "https://a,b"));
    test_expect_u(test, "none of them reached the list", 1, al_str_get_size(&cors.origins));

    String block = http_service_cors_headers_create(&cors, "https://a\r\nX: y");

    test_expect_false(test, "the refused origin is never reflected", char_contains_1(_block(&block), "X: y"));
    test_expect_false(test, "and gets no Allow-Origin", char_contains_1(_block(&block), "Access-Control-Allow-Origin"));

    string_uninit(&block);

    test_expect_false(test, "a non-token header name is refused", http_service_cors_header_add(&cors, "X Bad"));
    test_expect_false(test, "a CRLF header name is refused", http_service_cors_header_add(&cors, "X-Bad\r\nSet-Cookie: x"));
    test_expect_false(test, "an empty header name is refused", http_service_cors_header_add(&cors, ""));
    test_expect_false(test, "an empty method is refused", http_service_cors_method_add(&cors, ""));

    http_service_cors_uninit(&cors);

    test_case_end(test);
}

static void _test_wildcard_and_credentials(Test *const test) {
    test_case_begin(test, "the wildcard origin is refused under credentials, and works without them");

    HTTP_Service_CORS credentialed = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 with credentials succeeds", http_service_cors_init_2(&credentialed, true, 600));
    test_expect_false(test, "origin_add(*) is REFUSED while credentials are on", http_service_cors_origin_add(&credentialed, "*"));
    test_expect_u(test, "and nothing was stored", 0, al_str_get_size(&credentialed.origins));
    test_expect_false(test, "so no origin is silently allowed", http_service_cors_origin_allowed_1(&credentialed, "https://a.test"));

    http_service_cors_uninit(&credentialed);

    HTTP_Service_CORS open = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 without credentials succeeds", http_service_cors_init_2(&open, false, 600));
    test_expect_true(test, "origin_add(*) stores", http_service_cors_origin_add(&open, "*"));
    test_expect_true(test, "any origin is allowed", http_service_cors_origin_allowed_1(&open, "https://anything.test"));

    String block = http_service_cors_headers_create(&open, "https://anything.test");

    test_expect_string_contains(test, "the wildcard is emitted", _block(&block), "Access-Control-Allow-Origin: *");
    test_expect_false(test, "no Allow-Credentials", char_contains_1(_block(&block), "Access-Control-Allow-Credentials"));
    test_expect_false(test, "no Vary: Origin, because the answer cannot vary", char_contains_1(_block(&block), "Vary: Origin"));

    string_uninit(&block);
    http_service_cors_uninit(&open);

    test_case_end(test);
}

static void _test_headers_create(Test *const test) {
    test_case_begin(test, "actual-request block: echoed origin, credentials, expose, Vary on refusal");

    HTTP_Service_CORS cors = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 with credentials succeeds", http_service_cors_init_2(&cors, true, 600));
    test_expect_true(test, "origin_add stores", http_service_cors_origin_add(&cors, "https://a.test"));

    String allowed = http_service_cors_headers_create(&cors, "https://a.test");

    test_expect_string_contains(test, "the origin is echoed", _block(&allowed), "Access-Control-Allow-Origin: https://a.test");
    test_expect_string_contains(test, "credentials are announced", _block(&allowed), "Access-Control-Allow-Credentials: true");
    test_expect_string_contains(test, "Vary rides along", _block(&allowed), "Vary: Origin");
    test_expect_false(test, "no Expose-Headers while the list is empty", char_contains_1(_block(&allowed), "Expose-Headers"));

    string_uninit(&allowed);

    test_expect_true(test, "exposed_header_add stores", http_service_cors_exposed_header_add(&cors, "X-Total-Count"));

    String exposed = http_service_cors_headers_create(&cors, "https://a.test");

    test_expect_string_contains(test, "the exposed header is emitted", _block(&exposed), "Access-Control-Expose-Headers: X-Total-Count");

    string_uninit(&exposed);

    String refused = http_service_cors_headers_create(&cors, "https://evil.test");

    /* A shared cache that stored the CORS-less refusal would otherwise serve it to an
     * origin the policy does allow. */
    test_expect_string_contains(test, "a refusal still carries Vary", _block(&refused), "Vary: Origin");
    test_expect_false(test, "and no Allow-Origin", char_contains_1(_block(&refused), "Access-Control-Allow-Origin"));

    string_uninit(&refused);
    http_service_cors_uninit(&cors);

    test_case_end(test);
}

static void _test_headers_allowed_parsing(Test *const test) {
    test_case_begin(test, "header-list parsing: padding, HTAB, empty elements, one unknown");

    HTTP_Service_CORS cors = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_1 succeeds", http_service_cors_init_1(&cors));

    test_expect_true(test, "an empty list is allowed", http_service_cors_headers_allowed_1(&cors, ""));
    test_expect_true(test, "comma padding is skipped", http_service_cors_headers_allowed_1(&cors, ", ,content-type, ,"));
    test_expect_true(test, "trailing spaces are trimmed", http_service_cors_headers_allowed_1(&cors, "content-type   ,   authorization  "));
    test_expect_true(test, "HTAB is trimmed too", http_service_cors_headers_allowed_1(&cors, "\tcontent-type\t,\tauthorization\t"));
    test_expect_true(test, "case is ignored", http_service_cors_headers_allowed_1(&cors, "CONTENT-TYPE"));
    test_expect_false(test, "one unknown name fails the whole list", http_service_cors_headers_allowed_1(&cors, "content-type, x-nope"));
    test_expect_true(test, "the sized tier stops at its size", http_service_cors_headers_allowed_2(&cors, "content-type, x-nope", CHAR_STATIC_SIZE("content-type")));

    http_service_cors_uninit(&cors);

    test_case_end(test);
}

static void _test_preflight_block(Test *const test) {
    test_case_begin(test, "preflight block: lists, max age, Vary names both request headers");

    HTTP_Service_CORS cors = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 succeeds", http_service_cors_init_2(&cors, false, 600));
    test_expect_true(test, "origin_add stores", http_service_cors_origin_add(&cors, "https://a.test"));

    test_expect_true(test, "the preflight is allowed", http_service_cors_preflight_allowed_1(&cors, "https://a.test", "POST", "content-type"));
    test_expect_false(test, "an unknown origin is not", http_service_cors_preflight_allowed_1(&cors, "https://evil.test", "POST", "content-type"));
    test_expect_false(test, "an unknown method is not", http_service_cors_preflight_allowed_1(&cors, "https://a.test", "TRACE", "content-type"));
    test_expect_false(test, "an unknown header is not", http_service_cors_preflight_allowed_1(&cors, "https://a.test", "POST", "x-nope"));
    test_expect_true(test, "the sized tier agrees",
        http_service_cors_preflight_allowed_2(&cors, "https://a.test", CHAR_STATIC_SIZE("https://a.test"), "POST", CHAR_STATIC_SIZE("POST"), "content-type", CHAR_STATIC_SIZE("content-type")));

    String block = http_service_cors_preflight_create(&cors, "https://a.test", "POST", "content-type", false);

    test_expect_string_contains(test, "the method list is emitted", _block(&block), "Access-Control-Allow-Methods: GET, POST, PUT, PATCH, DELETE, OPTIONS");
    test_expect_string_contains(test, "the header list is emitted", _block(&block), "Access-Control-Allow-Headers: Authorization, Content-Type, X-CSRF-Token");
    test_expect_string_contains(test, "the max age is emitted", _block(&block), "Access-Control-Max-Age: 600");
    test_expect_string_contains(test, "Vary names Origin", _block(&block), "Vary: Origin");
    test_expect_string_contains(test, "Vary names the request method", _block(&block), "Access-Control-Request-Method");
    test_expect_string_contains(test, "Vary names the request headers", _block(&block), "Access-Control-Request-Headers");
    test_expect_false(test, "no Private-Network answer was asked for", char_contains_1(_block(&block), "Private-Network"));

    string_uninit(&block);

    String refused = http_service_cors_preflight_create(&cors, "https://evil.test", "POST", "content-type", false);

    test_expect_string_contains(test, "a refused preflight still carries Vary", _block(&refused), "Vary: Origin");
    test_expect_false(test, "and no method list", char_contains_1(_block(&refused), "Allow-Methods"));

    string_uninit(&refused);
    http_service_cors_uninit(&cors);

    test_case_end(test);
}

static void _test_preflight_reflection(Test *const test) {
    test_case_begin(test, "credentialed wildcard lists reflect the request instead of emitting *");

    HTTP_Service_CORS cors = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 with credentials succeeds", http_service_cors_init_2(&cors, true, 600));
    test_expect_true(test, "origin_add stores", http_service_cors_origin_add(&cors, "https://a.test"));
    test_expect_true(test, "method_add(*) stores", http_service_cors_method_add(&cors, "*"));
    test_expect_true(test, "header_add(*) stores", http_service_cors_header_add(&cors, "*"));

    String block = http_service_cors_preflight_create(&cors, "https://a.test", "PATCH", "x-custom, authorization", false);

    /* A browser reads a literal "*" under credentials as a header NAMED "*", so the
     * preflight fails; the request's own (validated) names go back instead. */
    test_expect_string_contains(test, "the requested method is reflected", _block(&block), "Access-Control-Allow-Methods: PATCH");
    test_expect_string_contains(test, "the requested headers are reflected", _block(&block), "Access-Control-Allow-Headers: x-custom, authorization");
    test_expect_false(test, "no literal wildcard reaches the wire", char_contains_1(_block(&block), ": *"));

    string_uninit(&block);

    String hostile = http_service_cors_preflight_create(&cors, "https://a.test", "PATCH", "evil\r\nSet-Cookie: x", false);

    test_expect_false(test, "a non-token request header list is not reflected at all", char_contains_1(_block(&hostile), "Allow-Headers"));
    test_expect_false(test, "and nothing it carried reaches the block", char_contains_1(_block(&hostile), "Set-Cookie"));

    /* One refusal shape for one input class: this used to wipe the block, Vary included,
     * while a non-token METHOD kept it - and cors.h promises Vary always names the two
     * Access-Control-Request-* inputs. Only the offending line goes. */
    test_expect_string_contains(test, "Vary survives a client-caused refusal",
        _block(&hostile), "Vary: Origin, Access-Control-Request-Method, Access-Control-Request-Headers");
    test_expect_string_contains(test, "and so does the origin line", _block(&hostile), "Access-Control-Allow-Origin: https://a.test");

    string_uninit(&hostile);

    String hostile_method = http_service_cors_preflight_create(&cors, "https://a.test", "PATCH\r\nX: y", "x-custom", false);

    test_expect_false(test, "a non-token request method is not reflected either", char_contains_1(_block(&hostile_method), "Allow-Methods"));

    string_uninit(&hostile_method);
    http_service_cors_uninit(&cors);

    HTTP_Service_CORS open = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 without credentials succeeds", http_service_cors_init_2(&open, false, 600));
    test_expect_true(test, "origin_add stores", http_service_cors_origin_add(&open, "https://a.test"));
    test_expect_true(test, "header_add(*) stores", http_service_cors_header_add(&open, "*"));

    String uncredentialed = http_service_cors_preflight_create(&open, "https://a.test", "POST", "x-custom", false);

    test_expect_string_contains(test, "uncredentialed, the stored wildcard is emitted as-is",
        _block(&uncredentialed), "Access-Control-Allow-Headers: Authorization, Content-Type, X-CSRF-Token, *");

    string_uninit(&uncredentialed);
    http_service_cors_uninit(&open);

    test_case_end(test);
}

static void _test_max_age_and_private_network(Test *const test) {
    test_case_begin(test, "max_age 0 is legal, and the Private Network answer is opt-in");

    HTTP_Service_CORS cors = DEFAULT_INITIALIZATION;

    /* Zero used to reach error_check_non_value_uint and end the process over a legal
     * configuration number - the standard setting while a policy is being developed. */
    test_expect_true(test, "init_2 accepts a max_age of 0", http_service_cors_init_2(&cors, false, 0));
    test_expect_true(test, "origin_add stores", http_service_cors_origin_add(&cors, "https://a.test"));

    String zero = http_service_cors_preflight_create(&cors, "https://a.test", "POST", "content-type", true);

    test_expect_string_contains(test, "and emits it", _block(&zero), "Access-Control-Max-Age: 0");
    test_expect_false(test, "the Private Network answer stays off by default", char_contains_1(_block(&zero), "Private-Network"));

    string_uninit(&zero);

    http_service_cors_private_network_set(&cors, true);

    String asked = http_service_cors_preflight_create(&cors, "https://a.test", "POST", "content-type", true);

    test_expect_string_contains(test, "an asking preflight gets the answer", _block(&asked), "Access-Control-Allow-Private-Network: true");

    string_uninit(&asked);

    String unasked = http_service_cors_preflight_create(&cors, "https://a.test", "POST", "content-type", false);

    test_expect_false(test, "a preflight that did not ask does not", char_contains_1(_block(&unasked), "Private-Network"));

    string_uninit(&unasked);
    http_service_cors_uninit(&cors);

    test_case_end(test);
}

static void _test_arena(Test *const test) {
    test_case_begin(test, "arena: a refused add answers false and leaves the list unchanged");

    Arena               arena   = arena_init_1(_ARENA_SMALL_CAPACITY, ARENA_TYPE_LINEAR);
    HTTP_Service_CORS   cors    = DEFAULT_INITIALIZATION;

    if (!http_service_cors_alloc_init_1(&cors, &arena)) {
        /* A too-small arena is a startup sizing mistake, reported rather than aborted. */
        test_expect_u(test, "a refused init leaves the service zeroed", 0, cors.max_age);
        test_expect_u(test, "and its lists empty", 0, al_str_get_size(&cors.origins));

        arena_uninit(&arena, ARENA_TYPE_LINEAR);

        test_case_end(test);

        return;
    }

    bool    refused         = false;
    bool    size_unchanged  = true;
    char    origin[64]      = DEFAULT_INITIALIZATION;

    for (USize round = 0; round < _ARENA_REFUSAL_ROUNDS && !refused; round += 1) {
        snprintf(origin, sizeof(origin), "https://host-%llu.test", (unsigned long long) round);

        USize const before = al_str_get_size(&cors.origins);

        if (!http_service_cors_origin_add(&cors, origin)) {
            refused         = true;
            size_unchanged  = al_str_get_size(&cors.origins) == before;
        }
    }

    test_expect_true(test, "the arena eventually refused an add", refused);
    test_expect_true(test, "and the refused add left the list alone", size_unchanged);

    http_service_cors_uninit(&cors);
    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

/* preflight_create's guards are SIZE comparisons now, so their arithmetic is load-bearing:
 * an `expected` one byte off discards a block that was written perfectly. These two cases
 * are the anti-vacuity anchor for that arithmetic - the block has to come back at EXACTLY
 * the sum of the lines it is made of, on both the cached path and the reflected one, and a
 * miscounted separator or name prefix shows up here as an empty block.
 *
 * What is NOT pinned HERE: the discard itself, because IN A CHECKED BUILD - this suite -
 * an EXHAUSTED arena never reaches the guard. string_reserve borrows through
 * allocator_borrow, whose `allocate` hook treats exhaustion as a programmer error and ENDS
 * THE PROCESS (arena_linear_alloc's error_check_out_of_bound_uint), so a starved
 * preflight_create aborts inside string_add_last_2 before any line is checked. A REFUSED
 * arena (null handler) does degrade, but http_service_cors_alloc_init_1 refuses on it
 * first, so *_create is never reached with one. Without ERROR_CHECK_ENABLED that abort is
 * not compiled: arena_linear_try_alloc answers nullptr, string_reserve refuses, and an
 * exhausted arena walks straight into these guards - which is exactly where test_unchecked.c
 * pins the discard, with a linear arena stepped down until *_create answers the EMPTY
 * block. The abort-versus-degrade split is an ERROR_CHECK_ENABLED property, not an arena
 * one, so the two suites together are the whole contract. */
static void _test_preflight_size_accounting(Test *const test) {
    test_case_begin(test, "preflight: the block is EXACTLY the sum of its lines (the size-accounted guards' arithmetic)");

    HTTP_Service_CORS cached = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 without credentials succeeds", http_service_cors_init_2(&cached, false, 600));
    test_expect_true(test, "origin_add stores", http_service_cors_origin_add(&cached, "https://a.test"));

    String block = http_service_cors_preflight_create(&cached, "https://a.test", "GET", "x-a", false);

    USize const cached_expected = CHAR_STATIC_SIZE("Vary: Origin, Access-Control-Request-Method, Access-Control-Request-Headers\r\n") +
        CHAR_STATIC_SIZE("Access-Control-Allow-Origin: https://a.test\r\n") +
        string_get_size(&cached.methods_line) +
        string_get_size(&cached.headers_line) +
        string_get_size(&cached.max_age_line);

    test_expect_u(test, "cached path: no line was miscounted and discarded", cached_expected, string_get_size(&block));

    string_uninit(&block);
    http_service_cors_uninit(&cached);

    HTTP_Service_CORS reflected = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 with credentials succeeds", http_service_cors_init_2(&reflected, true, 600));
    test_expect_true(test, "origin_add stores", http_service_cors_origin_add(&reflected, "https://a.test"));
    test_expect_true(test, "method_add(*) stores", http_service_cors_method_add(&reflected, "*"));
    test_expect_true(test, "header_add(*) stores", http_service_cors_header_add(&reflected, "*"));

    String echoed = http_service_cors_preflight_create(&reflected, "https://a.test", "PATCH", "x-a, x-b", false);

    /* Two elements, so the reflected line pays the name prefix ONCE and ", " once - the
     * exact branch _reflect_headers' new running total has to get right. */
    USize const reflected_expected = CHAR_STATIC_SIZE("Vary: Origin, Access-Control-Request-Method, Access-Control-Request-Headers\r\n") +
        CHAR_STATIC_SIZE("Access-Control-Allow-Origin: https://a.test\r\n") +
        CHAR_STATIC_SIZE("Access-Control-Allow-Credentials: true\r\n") +
        CHAR_STATIC_SIZE("Access-Control-Allow-Methods: PATCH\r\n") +
        CHAR_STATIC_SIZE("Access-Control-Allow-Headers: x-a, x-b\r\n") +
        string_get_size(&reflected.max_age_line);

    test_expect_u(test, "reflected path: the running total matches the bytes written", reflected_expected, string_get_size(&echoed));
    test_expect_string_contains(test, "and the two elements are joined with one separator", _block(&echoed), "Access-Control-Allow-Headers: x-a, x-b\r\n");

    string_uninit(&echoed);
    http_service_cors_uninit(&reflected);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Live harness
 *============================================================================*/
/* The module's own default handler answers 404 without consulting the router, so a suite
 * that registers routes has to dispatch them itself (tests/http/server does the same). */
static void _handler(void *context, HTTP_Server_Request *request, HTTP_Server_Response *response) {
    HTTP_Server         *const  server  = (HTTP_Server*) context;
    HTTP_Server_Holder          holder  = { .request = request, .response = response, .arena = nullptr };

    if (!http_server_router_dispatch_2(server->router, http_server_request_get_path_1(request), http_server_request_get_path_size(request), &holder)) {
        http_server_response_send_1(response, "no route", HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_NOT_FOUND);
    }
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
 * MARK: - Live cases
 *============================================================================*/
static void _test_live(Test *const test) {
    test_case_begin(test, "live: preflight, actual request, unknown origin, no origin");

    test_expect_true(test, "the live policy initializes", http_service_cors_init_2(&_live_cors, true, 600));
    test_expect_true(test, "origin_add stores", http_service_cors_origin_add(&_live_cors, "https://a.test"));
    test_expect_true(test, "exposed_header_add stores", http_service_cors_exposed_header_add(&_live_cors, "X-Total-Count"));

    HTTP_Server *server = http_server_new();

    test_expect_true(test, "route_add /cors", http_server_route_add(server, "/cors", _route_cors));

    U16     const   port    = _start(test, server);
    _Reply          reply   = DEFAULT_INITIALIZATION;

    bool const preflight_ok = _round_trip(port,
        "OPTIONS /cors HTTP/1.1\r\nHost: 127.0.0.1\r\nOrigin: https://a.test\r\n"
        "Access-Control-Request-Method: POST\r\nAccess-Control-Request-Headers: content-type\r\n"
        "Connection: close\r\n\r\n", &reply);

    test_expect_true(test, "the preflight round trip completed", preflight_ok);
    test_expect_u(test, "a preflight answers 204", 204, reply.status);
    test_expect_string_contains(test, "the origin is echoed", reply.headers, "Access-Control-Allow-Origin: https://a.test");
    test_expect_string_contains(test, "credentials are announced", reply.headers, "Access-Control-Allow-Credentials: true");
    test_expect_string_contains(test, "the method list rides along", reply.headers, "Access-Control-Allow-Methods:");
    test_expect_string_contains(test, "the header list rides along", reply.headers, "Access-Control-Allow-Headers:");
    test_expect_string_contains(test, "the max age rides along", reply.headers, "Access-Control-Max-Age: 600");
    test_expect_string_contains(test, "Vary rides along", reply.headers, "Vary: Origin");
    test_expect_false(test, "and never a wildcard under credentials", char_contains_1(reply.headers, "Allow-Origin: *"));

    bool const unknown_ok = _round_trip(port,
        "OPTIONS /cors HTTP/1.1\r\nHost: 127.0.0.1\r\nOrigin: https://evil.test\r\n"
        "Access-Control-Request-Method: POST\r\nConnection: close\r\n\r\n", &reply);

    test_expect_true(test, "the unknown-origin round trip completed", unknown_ok);
    test_expect_false(test, "an unknown origin gets no Allow-Origin", char_contains_1(reply.headers, "Access-Control-Allow-Origin"));
    test_expect_string_contains(test, "but still gets Vary", reply.headers, "Vary: Origin");

    bool const actual_ok = _round_trip(port,
        "GET /cors HTTP/1.1\r\nHost: 127.0.0.1\r\nOrigin: https://a.test\r\nConnection: close\r\n\r\n", &reply);

    test_expect_true(test, "the actual-request round trip completed", actual_ok);
    test_expect_u(test, "an actual request answers 200", 200, reply.status);
    test_expect_string_contains(test, "the origin is echoed", reply.headers, "Access-Control-Allow-Origin: https://a.test");
    test_expect_string_contains(test, "the exposed header is announced", reply.headers, "Access-Control-Expose-Headers: X-Total-Count");

    bool const bare_ok = _round_trip(port,
        "GET /cors HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n", &reply);

    test_expect_true(test, "the no-origin round trip completed", bare_ok);
    test_expect_false(test, "a request with no Origin gets no CORS headers", char_contains_1(reply.headers, "Access-Control-Allow-Origin"));

    http_server_delete(&server);
    http_service_cors_uninit(&_live_cors);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Entry point
 *============================================================================*/
I32 main(void) {
    LogConfig const log_config = { .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true };

    log_init(log_config);
    http_server_set_log_level(LLL_ERR);

    Test test = test_init("http_service_cors");

    test_suite_begin(&test, "cors policy");

    _test_defaults(&test);
    _test_origin_exact(&test);
    _test_origin_case_and_refusals(&test);
    _test_wildcard_and_credentials(&test);
    _test_headers_create(&test);
    _test_headers_allowed_parsing(&test);
    _test_preflight_block(&test);
    _test_preflight_reflection(&test);
    _test_max_age_and_private_network(&test);
    _test_arena(&test);
    _test_preflight_size_accounting(&test);

    test_suite_end(&test);

    test_suite_begin(&test, "cors live");

    _test_live(&test);

    test_suite_end(&test);

    return test_uninit(&test);
}