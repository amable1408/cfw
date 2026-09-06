/*
 * test_all.c - the http/server suite.
 *
 * Every live case drives a REAL server: http_server_run on port 0, the ephemeral
 * port read back with http_server_get_port, and a raw `net` socket for the client
 * so the bytes on the wire are under the test's own control (no curl, no
 * http_client - the module under test is the peer). Modelled on
 * tests/http/client/fixture.c's socket use.
 *
 * The offline register_protocol cases moved here from tests/http/http_server,
 * which no longer exists: the suite now mirrors the module path.
 */
#include <stdarg.h>
#include <stdio.h>

#include <chrono/chrono.h>
#include <math/scalar.h>
#include <http/server/http_server.h>
#include <log/log.h>
#include <net/net.h>
#include <test/test.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/
#define _CLIENT_BODY_MAX            65536
#define _CLIENT_HEADERS_MAX         8192
#define _CLIENT_IO_TIMEOUT_MS       4000
#define _CLIENT_RAW_MAX             131072
#define _FORWARDED_DEEP_ENTRIES     33
#define _FORWARDED_TEST_ENTRIES     40
#define _HEADER_ENUM_COUNT          15
#define _OWNING_ROUNDS              64
#define _PAYLOAD_TEST_MAX           4096
#define _ROUTE_BENCHMARK_ROUNDS     20000
#define _ROUTE_BENCHMARK_ROUTES     80

/*==============================================================================
 * MARK: - Request builder
 *============================================================================*/
/* snprintf answers the length it WOULD have written, so a truncated call reports a size
 * past the end of the buffer; `cursor += snprintf(...)` then walked the next append out of
 * bounds. Every append in this suite goes through this clamp, which never answers more
 * than capacity - 1. */
static USize _append(char *const buffer, USize const capacity, USize const cursor, char const *const format, ...) {
    if (buffer == nullptr || cursor + 1 >= capacity) {
        return cursor;
    }

    va_list arguments;

    va_start(arguments, format);

    I32 const written = vsnprintf(buffer + cursor, capacity - cursor, format, arguments);

    va_end(arguments);

    if (written <= 0) {
        return cursor;
    }

    return math_min_u(cursor + (USize) written, capacity - 1);
}

/*==============================================================================
 * MARK: - Raw client
 *============================================================================*/
typedef struct {
    U16     status;
    char    headers[_CLIENT_HEADERS_MAX];
    USize   headers_size;
    char    body[_CLIENT_BODY_MAX];
    USize   body_size;
    bool    complete;
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

/** @brief Find a header value, case-insensitively, in a reply's header block. */
static bool _reply_header(_Reply const *const reply, char const *const name, char *const out, USize const capacity) {
    static char lowered[_CLIENT_HEADERS_MAX];

    out[0] = '\0';

    USize const size = reply->headers_size < sizeof(lowered) ? reply->headers_size : sizeof(lowered) - 1;

    memory_copy_1((Byte*) lowered, (Byte*) reply->headers, size);

    lowered[size] = '\0';

    char_lower_2(lowered, size);

    char const *const found = char_find_slice_5(lowered, size, 0, (char*) name, char_length(name));

    if (found == nullptr) {
        return false;
    }

    USize cursor = (USize) (found - lowered) + char_length(name);

    while (cursor < size && (reply->headers[cursor] == ' ' || reply->headers[cursor] == '\t')) {
        cursor += 1;
    }

    USize stop = cursor;

    while (stop < size && reply->headers[stop] != '\r' && reply->headers[stop] != '\n') {
        stop += 1;
    }

    if (stop - cursor >= capacity) {
        return false;
    }

    if (stop > cursor) {
        char_copy_3(out, capacity, reply->headers + cursor, stop - cursor);
    }

    out[stop - cursor] = '\0';

    return true;
}

/** @brief Read one HTTP/1.1 reply: status line, headers, then Content-Length bytes of body. */
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

    reply->headers_size = header_end < sizeof(reply->headers) ? header_end : sizeof(reply->headers) - 1;

    memory_copy_1((Byte*) reply->headers, (Byte*) _raw, reply->headers_size);

    reply->headers[reply->headers_size] = '\0';

    if (raw_size >= 12) {
        reply->status = (U16) (((_raw[9] - '0') * 100) + ((_raw[10] - '0') * 10) + (_raw[11] - '0'));
    }

    char    length_text[32] = DEFAULT_INITIALIZATION;
    USize   expected        = 0;

    if (_reply_header(reply, "content-length:", length_text, sizeof(length_text))) {
        for (USize index = 0; length_text[index] >= '0' && length_text[index] <= '9'; index += 1) {
            expected = (expected * 10) + (USize) (length_text[index] - '0');
        }
    }

    USize body_have = raw_size - header_end;

    while (body_have < expected && raw_size + 1 < sizeof(_raw)) {
        USize   received    = 0;
        Result  result      = net_socket_recv_1(socket, _raw + raw_size, sizeof(_raw) - 1 - raw_size, &received);

        if (result_is_error(result) || received == 0) {
            break;
        }

        raw_size    = raw_size + received;
        body_have   = raw_size - header_end;
    }

    reply->body_size = body_have < sizeof(reply->body) ? body_have : sizeof(reply->body) - 1;

    if (reply->body_size > 0) {
        memory_copy_1((Byte*) reply->body, (Byte*) _raw + header_end, reply->body_size);
    }

    reply->body[reply->body_size] = '\0';
    reply->complete = body_have >= expected;

    return true;
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
 * MARK: - Route callbacks
 *============================================================================*/
static char     _capture[HTTP_SERVER_QUERY_MAX_LENGTH]  = DEFAULT_INITIALIZATION;
static char     _scratch[4096]                          = DEFAULT_INITIALIZATION;
static bool     _header_add_long_ok                     = true;
static bool     _header_add_crlf_ok                     = true;
/* The public test loop runs every binary from the repository root on a case-sensitive CI, so
 * the served file is one this suite writes itself, never a checked-in file whose spelling
 * differs between trees ("makefile" here, "Makefile" there). Named after the bound port,
 * not a pid: CFW has no process-id primitive, and the port is already in hand and unique
 * per run on one host. Set once by _fixture_file_name_set, after the ephemeral port the
 * server actually bound is read back. */
#define _FIXTURE_FILE_BODY "served by http_server send_file\n"
static char _fixture_file_name[CHAR_STATIC_SIZE("http_server_suite_fixture_65535.txt") + CHAR_END_CHARACTER] = DEFAULT_INITIALIZATION;

static void _fixture_file_name_set(U16 const port) {
    snprintf(_fixture_file_name, sizeof(_fixture_file_name), "http_server_suite_fixture_%u.txt", (unsigned) port);
}

static bool     _send_file_missing_ok                   = true;
static USize    _body_seen[4]                           = DEFAULT_INITIALIZATION;
static USize    _body_seen_count                        = 0;

static void _send_text(HTTP_Server_Response *const response, char const *const text) {
    http_server_response_send_1(response, text, HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_OK);
}

static void _route_exact(HTTP_Server_Route *const route) {
    _send_text(http_server_route_get_holder(route)->response, "exact");
}

static void _route_regex(HTTP_Server_Route *const route) {
    _send_text(http_server_route_get_holder(route)->response, "regex");
}

static void _route_capture(HTTP_Server_Route *const route) {
    HTTP_Server_Holder  *const  holder  = http_server_route_get_holder(route);
    Str                 const   capture = http_server_route_get_query_static_3(route);

    _capture[0] = '\0';

    if (str_get_size(&capture) > 0) {
        char_copy_3(_capture, sizeof(_capture), str_get_data(&capture), str_get_size(&capture));

        _capture[str_get_size(&capture)] = '\0';
    }

    _send_text(holder->response, _capture);
}

static void _route_echo(HTTP_Server_Route *const route) {
    HTTP_Server_Holder  *const  holder      = http_server_route_get_holder(route);
    USize               const   body_size   = http_server_request_get_body_size(holder->request);
    Byte                *const  body        = http_server_request_get_body(holder->request);

    if (_body_seen_count < 4) {
        _body_seen[_body_seen_count] = body_size;
        _body_seen_count += 1;
    }

    I32 const written = snprintf(_scratch, sizeof(_scratch), "%s|%llu|%.*s",
                                 http_server_request_get_method_1(holder->request),
                                 (unsigned long long) body_size,
                                 (int) (body_size < 256 ? body_size : 256),
                                 body != nullptr ? (char*) body : "");

    http_server_response_send_2(holder->response, (Byte const*) _scratch, written > 0 ? (USize) written : 0,
                                HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_OK);
}

static void _route_headers(HTTP_Server_Route *const route) {
    HTTP_Server_Holder *const holder = http_server_route_get_holder(route);

    char host[128]      = DEFAULT_INITIALIZATION;
    char agent[128]     = DEFAULT_INITIALIZATION;
    char range[128]     = DEFAULT_INITIALIZATION;
    char custom[128]    = DEFAULT_INITIALIZATION;
    char absent[128]    = "poisoned";

    bool const host_ok      = http_server_request_header_copy(holder->request, HTTP_SERVER_HEADER_HOST, host, sizeof(host));
    bool const agent_ok     = http_server_request_header_copy(holder->request, HTTP_SERVER_HEADER_USER_AGENT, agent, sizeof(agent));
    bool const range_ok     = http_server_request_header_copy(holder->request, HTTP_SERVER_HEADER_RANGE, range, sizeof(range));
    bool const custom_ok    = http_server_request_custom_header_copy(holder->request, "x-cfw-test:", custom, sizeof(custom));
    bool const absent_ok    = http_server_request_header_copy(holder->request, HTTP_SERVER_HEADER_AUTHORIZATION, absent, sizeof(absent));

    I32 const written = snprintf(_scratch, sizeof(_scratch), "%d:%s|%d:%s|%d:%s|%d:%s|%d:[%s]",
                                 host_ok, host, agent_ok, agent, range_ok, range,
                                 custom_ok, custom, absent_ok, absent);

    http_server_response_send_2(holder->response, (Byte const*) _scratch, written > 0 ? (USize) written : 0,
                                HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_OK);
}

static void _route_query(HTTP_Server_Route *const route) {
    HTTP_Server_Holder *const holder = http_server_route_get_holder(route);

    char query[256] = "poisoned";

    bool const copied = http_server_request_query_copy(holder->request, query, sizeof(query));

    I32 const written = snprintf(_scratch, sizeof(_scratch), "%d:[%s]", copied, query);

    http_server_response_send_2(holder->response, (Byte const*) _scratch, written > 0 ? (USize) written : 0,
                                HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_OK);
}

static void _route_nul(HTTP_Server_Route *const route) {
    /* Eight bytes with a NUL in the middle: send_2 must write all eight, where send_1
     * would have stopped at the NUL. */
    static Byte const payload[8] = { 'a', 'b', 'c', 0x00, 'd', 'e', 'f', 'g' };

    http_server_response_send_2(http_server_route_get_holder(route)->response, payload, sizeof(payload),
                                HTTP_SERVER_CONTENT_TYPE_APPLICATION_OCTET_STREAM, HTTP_SERVER_STATUS_CODE_OK);
}

static void _route_empty(HTTP_Server_Route *const route) {
    http_server_response_send_empty(http_server_route_get_holder(route)->response, HTTP_SERVER_STATUS_CODE_NO_CONTENT);
}

static void _route_notmodified(HTTP_Server_Route *const route) {
    http_server_response_send_empty(http_server_route_get_holder(route)->response, HTTP_SERVER_STATUS_CODE_NOT_MODIFIED);
}

static void _route_head(HTTP_Server_Route *const route) {
    _send_text(http_server_route_get_holder(route)->response, "0123456789");
}

static void _route_big_headers(HTTP_Server_Route *const route) {
    HTTP_Server_Holder *const holder = http_server_route_get_holder(route);

    char name[32]   = DEFAULT_INITIALIZATION;
    char value[256] = DEFAULT_INITIALIZATION;

    for (USize index = 0; index < sizeof(value) - 1; index += 1) {
        value[index] = 'v';
    }

    /* Twenty 256-byte values blow well past the 2048-byte header block. */
    for (USize index = 0; index < 20; index += 1) {
        snprintf(name, sizeof(name), "X-Filler-%llu", (unsigned long long) index);
        http_server_response_header_add(holder->response, name, value);
    }

    _send_text(holder->response, "never reaches the wire");
}

static void _route_header_limits(HTTP_Server_Route *const route) {
    HTTP_Server_Holder *const holder = http_server_route_get_holder(route);

    char oversize[HTTP_SERVER_RESPONSE_HEADER_MAX_LENGTH + 8] = DEFAULT_INITIALIZATION;

    for (USize index = 0; index < sizeof(oversize) - 1; index += 1) {
        oversize[index] = 'x';
    }

    _header_add_long_ok = http_server_response_header_add(holder->response, "X-Long", oversize);
    _header_add_crlf_ok = http_server_response_header_add(holder->response, "X-Split", "ok\r\nX-Injected: yes");

    http_server_response_header_add(holder->response, "X-Clean", "fine");

    I32 const written = snprintf(_scratch, sizeof(_scratch), "%d%d", _header_add_long_ok, _header_add_crlf_ok);

    http_server_response_send_2(holder->response, (Byte const*) _scratch, written > 0 ? (USize) written : 0,
                                HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_OK);
}

static void _route_send_file_missing(HTTP_Server_Route *const route) {
    HTTP_Server_Holder *const holder = http_server_route_get_holder(route);

    http_server_response_header_add(holder->response, "X-Queued", "yes");

    _send_file_missing_ok = http_server_response_send_file(holder->response, "no_such_file_here.bin",
                                                          HTTP_SERVER_CONTENT_TYPE_APPLICATION_OCTET_STREAM, nullptr, 0);

    /* No set_write_success(true) here any more. A send_file refusal writes nothing and
     * therefore no longer fails the response, so this fallback reply reaches the wire on
     * its own - which is the whole point of the contract. The suite used to have to undo
     * the flag by hand, i.e. the API was making the test lie. */
    http_server_response_send_1(holder->response, _send_file_missing_ok ? "served" : "refused",
                                HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_NOT_FOUND);
}

static void _route_header_all(HTTP_Server_Route *const route) {
    HTTP_Server_Holder *const holder = http_server_route_get_holder(route);

    /* Every value the enum carries, so a new member cannot be added without a token
     * mapping: an unmapped one answers false here and drops the count. */
    static HTTP_Server_Header const known[] = {
        HTTP_SERVER_HEADER_ACCEPT,
        HTTP_SERVER_HEADER_ACCEPT_ENCODING,
        HTTP_SERVER_HEADER_AUTHORIZATION,
        HTTP_SERVER_HEADER_CONTENT_LENGTH,
        HTTP_SERVER_HEADER_CONTENT_TYPE,
        HTTP_SERVER_HEADER_COOKIE,
        HTTP_SERVER_HEADER_HOST,
        HTTP_SERVER_HEADER_IF_MODIFIED_SINCE,
        HTTP_SERVER_HEADER_IF_NONE_MATCH,
        HTTP_SERVER_HEADER_IF_RANGE,
        HTTP_SERVER_HEADER_ORIGIN,
        HTTP_SERVER_HEADER_RANGE,
        HTTP_SERVER_HEADER_REFERER,
        HTTP_SERVER_HEADER_USER_AGENT,
        HTTP_SERVER_HEADER_X_FORWARDED_FOR
    };

    char    value[256]  = DEFAULT_INITIALIZATION;
    char    tight[1]    = DEFAULT_INITIALIZATION;
    USize   found       = 0;

    for (USize index = 0; index < sizeof(known) / sizeof(known[0]); index += 1) {
        if (http_server_request_header_copy(holder->request, known[index], value, sizeof(value))) {
            found += 1;
        }
    }

    /* Capacity 1 holds the terminator and nothing else, so every present header is too
     * long for it: the answer must be false with a CLEARED buffer, never a copy that
     * runs one byte past the end. */
    bool const tight_ok = http_server_request_header_copy(holder->request, HTTP_SERVER_HEADER_HOST, tight, sizeof(tight));

    I32 const written = snprintf(_scratch, sizeof(_scratch), "%llu|%d|[%s]",
                                 (unsigned long long) found, tight_ok, tight);

    http_server_response_send_2(holder->response, (Byte const*) _scratch, written > 0 ? (USize) written : 0,
                                HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_OK);
}

static void _route_content_type(HTTP_Server_Route *const route) {
    HTTP_Server_Holder *const holder = http_server_route_get_holder(route);

    I32 const written = snprintf(_scratch, sizeof(_scratch), "%d%d%d%d",
                                 http_server_request_content_type_is(holder->request, "application/json"),
                                 http_server_request_content_type_is(holder->request, "text/plain"),
                                 http_server_request_content_type_is(holder->request, "application/jsonx"),
                                 http_server_request_content_type_is(holder->request, "application/jso"));

    http_server_response_send_2(holder->response, (Byte const*) _scratch, written > 0 ? (USize) written : 0,
                                HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_OK);
}

static void _route_custom_header(HTTP_Server_Route *const route) {
    HTTP_Server_Holder *const holder = http_server_route_get_holder(route);

    char bare[64]   = "poisoned";
    char colon[64]  = "poisoned";
    char scratch[8] = DEFAULT_INITIALIZATION;
    char over_long[HTTP_SERVER_CUSTOM_HEADER_NAME_MAX_LENGTH + 8] = DEFAULT_INITIALIZATION;

    for (USize index = 0; index < sizeof(over_long) - 1; index += 1) {
        over_long[index] = 'x';
    }

    bool const bare_ok  = http_server_request_custom_header_copy(holder->request, "X-Device-Id", bare, sizeof(bare));
    bool const colon_ok = http_server_request_custom_header_copy(holder->request, "x-device-id:", colon, sizeof(colon));
    bool const over_ok  = http_server_request_custom_header_copy(holder->request, over_long, scratch, sizeof(scratch));
    String     owned    = http_server_request_custom_header_get_4(holder->request, "X-Device-Id");

    I32 const written = snprintf(_scratch, sizeof(_scratch), "%d:%s|%d:%s|%d|%s",
                                 bare_ok, bare, colon_ok, colon, over_ok,
                                 string_get_size(&owned) > 0 ? string_get_data(&owned) : "");

    string_uninit(&owned);

    http_server_response_send_2(holder->response, (Byte const*) _scratch, written > 0 ? (USize) written : 0,
                                HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_OK);
}

static void _route_owning(HTTP_Server_Route *const route) {
    HTTP_Server_Holder *const holder = http_server_route_get_holder(route);

    /* Every OWNING tier, acquired and released _OWNING_ROUNDS times. Each release is the
     * pin: a tier that secretly returned a VIEW would have its releaser free storage the
     * request or the connection still owns, and the round after it would read freed
     * memory. The repetition is what makes a per-call leak show up as growth rather than
     * as a single unnoticed block. */
    char    *ip_1       = nullptr;
    Str     ip_3        = DEFAULT_INITIALIZATION;
    String  ip_4        = DEFAULT_INITIALIZATION;
    String  payload_4   = DEFAULT_INITIALIZATION;
    char    *query_1    = nullptr;
    Str     query_3     = DEFAULT_INITIALIZATION;
    String  query_4     = DEFAULT_INITIALIZATION;

    for (USize round = 0; round < _OWNING_ROUNDS; round += 1) {
        ip_1        = http_server_request_get_ip_1(holder->request);
        ip_3        = http_server_request_get_ip_3(holder->request);
        ip_4        = http_server_request_get_ip_4(holder->request);
        payload_4   = http_server_request_get_payload_4(holder->request);
        query_1     = http_server_route_get_query_1(route);
        query_3     = http_server_route_get_query_3(route);
        query_4     = http_server_route_get_query_4(route);

        if (round + 1 == _OWNING_ROUNDS) {
            break;
        }

        memory_delete((void**) &ip_1);
        str_uninit(&ip_3);
        string_uninit(&ip_4);
        string_uninit(&payload_4);
        memory_delete((void**) &query_1);
        str_uninit(&query_3);
        string_uninit(&query_4);
    }

    I32 const written = snprintf(_scratch, sizeof(_scratch), "%s|%llu|%llu|%s|%s|%llu|%s",
                                 ip_1 != nullptr ? ip_1 : "",
                                 (unsigned long long) str_get_size(&ip_3),
                                 (unsigned long long) string_get_size(&ip_4),
                                 string_get_size(&payload_4) > 0 ? string_get_data(&payload_4) : "",
                                 query_1 != nullptr ? query_1 : "",
                                 (unsigned long long) str_get_size(&query_3),
                                 string_get_size(&query_4) > 0 ? string_get_data(&query_4) : "");

    memory_delete((void**) &ip_1);
    str_uninit(&ip_3);
    string_uninit(&ip_4);
    string_uninit(&payload_4);
    memory_delete((void**) &query_1);
    str_uninit(&query_3);
    string_uninit(&query_4);

    http_server_response_send_2(holder->response, (Byte const*) _scratch, written > 0 ? (USize) written : 0,
                                HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_OK);
}

static void _route_send_file(HTTP_Server_Route *const route) {
    HTTP_Server_Holder *const holder = http_server_route_get_holder(route);

    http_server_response_header_add(holder->response, "X-Queued", "yes");
    http_server_response_send_file(holder->response, _fixture_file_name, HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN,
                                   "X-Preformatted: yes\r\n", CHAR_STATIC_SIZE("X-Preformatted: yes\r\n"));
}

static void _route_client_ip(HTTP_Server_Route *const route) {
    HTTP_Server_Holder *const holder = http_server_route_get_holder(route);

    char hop0[HTTP_SERVER_IP_MAX_LENGTH] = DEFAULT_INITIALIZATION;
    char hop1[HTTP_SERVER_IP_MAX_LENGTH] = DEFAULT_INITIALIZATION;
    char hop2[HTTP_SERVER_IP_MAX_LENGTH] = DEFAULT_INITIALIZATION;
    char hop9[HTTP_SERVER_IP_MAX_LENGTH] = DEFAULT_INITIALIZATION;

    http_server_request_get_client_ip(holder->request, 0, hop0, sizeof(hop0));
    http_server_request_get_client_ip(holder->request, 1, hop1, sizeof(hop1));
    http_server_request_get_client_ip(holder->request, 2, hop2, sizeof(hop2));
    http_server_request_get_client_ip(holder->request, 9, hop9, sizeof(hop9));

    I32 const written = snprintf(_scratch, sizeof(_scratch), "%s|%s|%s|%s", hop0, hop1, hop2, hop9);

    http_server_response_send_2(holder->response, (Byte const*) _scratch, written > 0 ? (USize) written : 0,
                                HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_OK);
}

static void _route_client_ip_deep(HTTP_Server_Route *const route) {
    HTTP_Server_Holder *const holder = http_server_route_get_holder(route);

    char hop31[HTTP_SERVER_IP_MAX_LENGTH] = DEFAULT_INITIALIZATION;
    char hop32[HTTP_SERVER_IP_MAX_LENGTH] = DEFAULT_INITIALIZATION;
    char hop33[HTTP_SERVER_IP_MAX_LENGTH] = DEFAULT_INITIALIZATION;

    http_server_request_get_client_ip(holder->request, 31, hop31, sizeof(hop31));
    http_server_request_get_client_ip(holder->request, 32, hop32, sizeof(hop32));
    http_server_request_get_client_ip(holder->request, 33, hop33, sizeof(hop33));

    I32 const written = snprintf(_scratch, sizeof(_scratch), "%s|%s|%s", hop31, hop32, hop33);

    http_server_response_send_2(holder->response, (Byte const*) _scratch, written > 0 ? (USize) written : 0,
                                HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_OK);
}

static void _route_benchmark(HTTP_Server_Route *const route) {
    (void) route;
}

static Byte _big_body[16384] = DEFAULT_INITIALIZATION;

static void _route_big_body(HTTP_Server_Route *const route) {
    http_server_response_send_2(http_server_route_get_holder(route)->response, _big_body, sizeof(_big_body),
                                HTTP_SERVER_CONTENT_TYPE_APPLICATION_OCTET_STREAM, HTTP_SERVER_STATUS_CODE_OK);
}

/*==============================================================================
 * MARK: - Handler
 *============================================================================*/
static void _handler(void *context, HTTP_Server_Request *request, HTTP_Server_Response *response) {
    HTTP_Server         *const  server  = (HTTP_Server*) context;
    HTTP_Server_Holder          holder  = { .request = request, .response = response, .arena = nullptr };

    if (!http_server_router_dispatch_2(server->router,
                                       http_server_request_get_path_1(request),
                                       http_server_request_get_path_size(request),
                                       &holder)) {
        http_server_response_send_1(response, "no route", HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_NOT_FOUND);
    }
}

/* A STRONG definition, displacing the module's weak 404 default. The module calls it only
 * when no per-server handler is set, which _test_default_callback below arranges on
 * purpose: that case is what pins the override actually reaching the wire, rather than the
 * weak body it replaced. (test_unchecked.c defines nothing, pinning the other half - that
 * the module links with no definition at all.) */
void http_server_request_default_callback(void *context, HTTP_Server_Request *request, HTTP_Server_Response *response) {
    (void) context;
    (void) request;

    http_server_response_send_1(response, "default", HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_NOT_FOUND);
}

/*==============================================================================
 * MARK: - Offline cases (register_protocol moved from tests/http/http_server)
 *============================================================================*/
static I32 _stub_protocol_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
    (void) wsi; (void) reason; (void) user; (void) in; (void) len;

    return 0;
}

static void _test_register_single(Test *const test) {
    test_case_begin(test, "register a single protocol");

    HTTP_Server *server = http_server_new();

    test_expect_not_null(test, "http_server_new", server);

    struct lws_protocols proto = {
        .name                   = "my-proto",
        .callback               = _stub_protocol_callback,
        .per_session_data_size  = 64,
        .rx_buffer_size         = 0,
        .id                     = 0,
        .user                   = nullptr,
        .tx_packet_size         = 0
    };

    test_expect_true(test, "register returns true", http_server_register_protocol(server, &proto));
    test_expect_u(test, "registered_count == 1", 1, server->registered_count);
    test_expect_not_null(test, "registered ptr allocated", server->registered);
    test_expect_string(test, "registered[0].name", "my-proto", server->registered[0].name);
    test_expect_true(test, "registered[0].callback", server->registered[0].callback == _stub_protocol_callback);
    test_expect_u(test, "registered[0].per_session_data_size", 64, (USize) server->registered[0].per_session_data_size);

    http_server_delete(&server);

    test_expect_null(test, "server nulled after delete", server);

    test_case_end(test);
}

static void _test_register_fill_to_max(Test *const test) {
    test_case_begin(test, "fill the registry to HTTP_SERVER_PROTOCOLS_MAX");

    HTTP_Server *server = http_server_new();

    test_expect_not_null(test, "http_server_new", server);

    char const *names[HTTP_SERVER_PROTOCOLS_MAX] = {
        "proto-0", "proto-1", "proto-2", "proto-3",
        "proto-4", "proto-5", "proto-6", "proto-7"
    };

    for (USize index = 0; index < HTTP_SERVER_PROTOCOLS_MAX; index += 1) {
        struct lws_protocols proto = {
            .name                   = names[index],
            .callback               = _stub_protocol_callback,
            .per_session_data_size  = (size_t) (index * 8),
            .rx_buffer_size         = 0,
            .id                     = 0,
            .user                   = nullptr,
            .tx_packet_size         = 0
        };

        test_expect_true(test, "each register returns true", http_server_register_protocol(server, &proto));
    }

    test_expect_u(test, "registered_count == MAX", HTTP_SERVER_PROTOCOLS_MAX, server->registered_count);

    struct lws_protocols extra = {
        .name                   = "overflow",
        .callback               = _stub_protocol_callback,
        .per_session_data_size  = 0,
        .rx_buffer_size         = 0,
        .id                     = 0,
        .user                   = nullptr,
        .tx_packet_size         = 0
    };

    test_expect_false(test, "over-MAX register returns false", http_server_register_protocol(server, &extra));
    test_expect_u(test, "count unchanged after rejection", HTTP_SERVER_PROTOCOLS_MAX, server->registered_count);

    http_server_delete(&server);

    test_expect_null(test, "server nulled after delete", server);

    test_case_end(test);
}

static void _test_delete_without_register(Test *const test) {
    test_case_begin(test, "delete a server that never registered a protocol");

    HTTP_Server *server = http_server_new();

    test_expect_not_null(test, "http_server_new", server);
    test_expect_null(test, "registered is null initially", server->registered);
    test_expect_u(test, "registered_count is 0 initially", 0, server->registered_count);
    test_expect_null(test, "protocols is null initially", server->protocols);
    test_expect_u(test, "payload cap defaults to 1 MiB", HTTP_SERVER_PAYLOAD_DEFAULT_MAX_LENGTH, server->payload_max_size);

    http_server_delete(&server);

    test_expect_null(test, "server nulled after delete", server);

    test_case_end(test);
}

static void _test_init_in_place(Test *const test) {
    test_case_begin(test, "init/uninit on a stack server");

    HTTP_Server server = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init returns true", http_server_init(&server));
    test_expect_null(test, "route list empty", server.route);
    test_expect_null(test, "router null before run", server.router);
    test_expect_u(test, "port 0 before run", 0, http_server_get_port(&server));
    test_expect_true(test, "route_add on a stack server", http_server_route_add(&server, "/x", _route_exact));
    test_expect_not_null(test, "route list populated", server.route);

    http_server_uninit(&server);

    test_expect_null(test, "uninit frees the route list", server.route);

    test_case_end(test);
}

static void _test_payload_cap_semantics(Test *const test) {
    test_case_begin(test, "payload cap: 0 restores the default, UNLIMITED spells out the old meaning");

    HTTP_Server *server = http_server_new();

    http_server_set_payload_max_size(server, 512);

    test_expect_u(test, "an explicit cap is stored", 512, server->payload_max_size);

    http_server_set_payload_max_size(server, 0);

    test_expect_u(test, "0 restores the default cap", HTTP_SERVER_PAYLOAD_DEFAULT_MAX_LENGTH, server->payload_max_size);

    http_server_set_payload_max_size(server, HTTP_SERVER_PAYLOAD_UNLIMITED);

    test_expect_u(test, "UNLIMITED is stored as-is", HTTP_SERVER_PAYLOAD_UNLIMITED, server->payload_max_size);

    http_server_delete(&server);

    test_case_end(test);
}

static void _test_arena_new_and_delete(Test *const test) {
    test_case_begin(test, "alloc_new/alloc_delete: routes go back to the heap, the handle to the arena");

    Arena arena = arena_init_1(64 * 1024, ARENA_TYPE_LINEAR);

    HTTP_Server *server = http_server_alloc_new(&arena);

    test_expect_not_null(test, "http_server_alloc_new", server);
    test_expect_u(test, "the arena server starts with the default cap", HTTP_SERVER_PAYLOAD_DEFAULT_MAX_LENGTH, server->payload_max_size);
    test_expect_null(test, "no routes yet", server->route);

    /* Route nodes are memory_alloc heap blocks, NOT arena blocks. alloc_delete used to
     * hand each one to the arena's deallocate: a leak on a linear arena and an alien
     * pointer on a pool arena's free list. It runs http_server_uninit now, which releases
     * them through the allocator that made them. */
    test_expect_true(test, "exact route on an arena server", http_server_route_add(server, "/a", _route_exact));
    test_expect_true(test, "regex route on an arena server", http_server_route_match_add(server, "/b/(.+)", _route_capture));
    test_expect_not_null(test, "the route list is populated", server->route);

    http_server_alloc_delete(&server, &arena);

    test_expect_null(test, "alloc_delete nulls the handle", server);

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_arena_new_refuses_an_exhausted_arena(Test *const test) {
    test_case_begin(test, "alloc_new answers null on an arena too small for the handle");

    /* allocator_try_borrow, not allocator->allocate: an arena that cannot meet the request
     * answers null here rather than aborting or being called through a null hook. */
    Arena arena = arena_init_1(16, ARENA_TYPE_LINEAR);

    HTTP_Server *server = http_server_alloc_new(&arena);

    test_expect_null(test, "an exhausted arena yields null, not an abort", server);

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Live-server harness
 *============================================================================*/
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
static void _test_routing(Test *const test) {
    test_case_begin(test, "routing: precedence, last-registered-wins, capture, refusals");

    HTTP_Server *server = http_server_new();

    /* "/both" is registered exact FIRST and regex SECOND. The list is a prepend, so the
     * regex one is scanned first: the LAST registration wins. */
    test_expect_true(test, "route_add /exact", http_server_route_add(server, "/exact", _route_exact));
    test_expect_true(test, "route_add /both", http_server_route_add(server, "/both", _route_exact));
    test_expect_true(test, "route_match_add /both", http_server_route_match_add(server, "/both", _route_regex));
    test_expect_true(test, "route_match_add group-1 capture", http_server_route_match_add(server, "/cap/(.+)", _route_capture));
    test_expect_true(test, "route_match_add named capture", http_server_route_match_add(server, "/named/(?<file>[a-z]+)", _route_capture));

    char over_long[HTTP_SERVER_PATH_MAX_LENGTH + 8] = DEFAULT_INITIALIZATION;

    for (USize index = 0; index < sizeof(over_long) - 1; index += 1) {
        over_long[index] = 'a';
    }

    test_expect_false(test, "route_add refuses an empty path", http_server_route_add(server, "", _route_exact));
    test_expect_false(test, "route_add refuses an over-long path", http_server_route_add(server, over_long, _route_exact));
    test_expect_false(test, "route_match_add refuses a pattern that will not compile", http_server_route_match_add(server, "/bad/(", _route_exact));

    U16 const port = _start(test, server);

    _Reply reply = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "GET /exact round trip", _client_round_trip(port, "GET /exact HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "/exact status 200", 200, reply.status);
        test_expect_string(test, "/exact body", "exact", reply.body);
    }

    if (_client_round_trip(port, "GET /both HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply)) {
        test_expect_string(test, "/both -> the LAST registration wins", "regex", reply.body);
    }

    if (_client_round_trip(port, "GET /cap/hello HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply)) {
        test_expect_string(test, "group 1 capture", "hello", reply.body);
    }

    if (_client_round_trip(port, "GET /named/world HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply)) {
        test_expect_string(test, "named group 'file' capture", "world", reply.body);
    }

    if (_client_round_trip(port, "GET /missing HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply)) {
        test_expect_u(test, "unmatched path -> the handler's own 404", 404, reply.status);
    }

    http_server_delete(&server);

    test_case_end(test);
}

static void _test_uri_too_long(Test *const test) {
    test_case_begin(test, "a 300-character path is answered 414, not truncated into another route");

    HTTP_Server *server = http_server_new();

    http_server_route_match_add(server, "/assets/(.*)", _route_exact);

    U16 const port = _start(test, server);

    char request[512] = DEFAULT_INITIALIZATION;
    USize cursor = _append(request, sizeof(request), 0, "GET /assets/");

    for (USize index = 0; index < 300 && cursor + 1 < sizeof(request); index += 1) {
        request[cursor] = 'a';
        cursor += 1;
    }

    _append(request, sizeof(request), cursor, " HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n");

    _Reply reply = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "over-long URI round trip", _client_round_trip(port, request, &reply))) {
        test_expect_u(test, "status is 414 Request-URI Too Long", 414, reply.status);
        test_expect_string(test, "414 body", "Request-URI Too Long", reply.body);
    }

    http_server_delete(&server);

    test_case_end(test);
}

static void _test_bodies(Test *const test) {
    test_case_begin(test, "bodies: chunked POST, PUT, two POSTs on one connection");

    HTTP_Server *server = http_server_new();

    http_server_route_add(server, "/echo", _route_echo);

    U16 const port = _start(test, server);

    _body_seen_count = 0;

    _Reply reply = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "POST round trip",
                         _client_round_trip(port, "POST /echo HTTP/1.1\r\nHost: t\r\nContent-Length: 5\r\nConnection: close\r\n\r\nhello", &reply))) {
        test_expect_string(test, "POST body echoed", "POST|5|hello", reply.body);
    }

    /* Chunked: this libwebsockets build never delivers a chunked REQUEST body through
     * LWS_CALLBACK_HTTP_BODY, so waiting for one held the connection open with no reply
     * until lws' own timeout fired. The refusal has to be loud and immediate. */
    if (test_expect_true(test, "chunked POST round trip",
                         _client_round_trip(port,
                                            "POST /echo HTTP/1.1\r\nHost: t\r\nTransfer-Encoding: chunked\r\n\r\n"
                                            "5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n",
                                            &reply))) {
        test_expect_u(test, "a chunked body is refused 411, not left to hang", 411, reply.status);
    }

    /* PUT used to be dispatched with payload=nullptr and its body left in the connection
     * to be parsed as the next request. */
    if (test_expect_true(test, "PUT with a body round trip",
                         _client_round_trip(port, "PUT /echo HTTP/1.1\r\nHost: t\r\nContent-Length: 7\r\nConnection: close\r\n\r\nputbody", &reply))) {
        test_expect_string(test, "PUT body reaches the handler", "PUT|7|putbody", reply.body);
    }

    /* Two POSTs on ONE keep-alive connection: the second used to arrive with the first
     * one's payload_size and capacity still set, skip the reallocation, and memcpy
     * through a freed pointer at a stale offset. */
    Net_Socket socket = DEFAULT_INITIALIZATION;

    _body_seen_count = 0;

    if (test_expect_true(test, "keep-alive connection opens", _client_open(port, &socket))) {
        _Reply first    = DEFAULT_INITIALIZATION;
        _Reply second   = DEFAULT_INITIALIZATION;

        bool ok = _client_write(socket, "POST /echo HTTP/1.1\r\nHost: t\r\nContent-Length: 4\r\n\r\nAAAA",
                                CHAR_STATIC_SIZE("POST /echo HTTP/1.1\r\nHost: t\r\nContent-Length: 4\r\n\r\nAAAA"));

        ok = ok && _client_read(socket, &first);

        if (ok) {
            test_expect_string(test, "first POST on the connection", "POST|4|AAAA", first.body);
        }

        ok = ok && _client_write(socket, "POST /echo HTTP/1.1\r\nHost: t\r\nContent-Length: 9\r\nConnection: close\r\n\r\nBBBBBBBBB",
                                 CHAR_STATIC_SIZE("POST /echo HTTP/1.1\r\nHost: t\r\nContent-Length: 9\r\nConnection: close\r\n\r\nBBBBBBBBB"));

        ok = ok && _client_read(socket, &second);

        if (test_expect_true(test, "second POST on the SAME connection is answered", ok)) {
            test_expect_string(test, "second POST body is its own, not the first's", "POST|9|BBBBBBBBB", second.body);
        }

        net_socket_close(socket);
    }

    http_server_delete(&server);

    test_case_end(test);
}

static void _test_payload_limits(Test *const test) {
    test_case_begin(test, "413 at the cap and one byte past it, with a custom body");

    HTTP_Server *server = http_server_new();

    http_server_route_add(server, "/echo", _route_echo);
    http_server_set_payload_max_size(server, _PAYLOAD_TEST_MAX);
    http_server_set_payload_limit_response(server, "{\"error\":\"too_large\"}", HTTP_SERVER_CONTENT_TYPE_APPLICATION_JSON);

    U16 const port = _start(test, server);

    static char request[_PAYLOAD_TEST_MAX + 512];

    USize cursor = _append(request, sizeof(request), 0,
                           "POST /echo HTTP/1.1\r\nHost: t\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
                           _PAYLOAD_TEST_MAX);

    for (USize index = 0; index < _PAYLOAD_TEST_MAX; index += 1) {
        request[cursor + index] = 'z';
    }

    request[cursor + _PAYLOAD_TEST_MAX] = '\0';

    _Reply reply = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "a body exactly at the cap round trip", _client_round_trip(port, request, &reply))) {
        test_expect_u(test, "a body exactly at the cap is accepted", 200, reply.status);
        test_expect_string_contains(test, "the whole body arrived", reply.body, "|4096|");
    }

    cursor = _append(request, sizeof(request), 0,
                     "POST /echo HTTP/1.1\r\nHost: t\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
                     _PAYLOAD_TEST_MAX + 1);

    for (USize index = 0; index < _PAYLOAD_TEST_MAX + 1; index += 1) {
        request[cursor + index] = 'z';
    }

    request[cursor + _PAYLOAD_TEST_MAX + 1] = '\0';

    if (test_expect_true(test, "a body one byte past the cap round trip", _client_round_trip(port, request, &reply))) {
        test_expect_u(test, "one byte past the cap is refused 413", 413, reply.status);
        test_expect_string(test, "the configured 413 body is used", "{\"error\":\"too_large\"}", reply.body);
    }

    http_server_delete(&server);

    test_case_end(test);
}

static void _test_request_accessors(Test *const test) {
    test_case_begin(test, "header_copy per enum, custom, absent; query_copy");

    HTTP_Server *server = http_server_new();

    http_server_route_add(server, "/hdr", _route_headers);
    http_server_route_add(server, "/query", _route_query);

    U16 const port = _start(test, server);

    _Reply reply = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "header round trip",
                         _client_round_trip(port,
                                            "GET /hdr HTTP/1.1\r\nHost: example.test\r\nUser-Agent: cfw-suite\r\n"
                                            "Range: bytes=0-9\r\nX-Cfw-Test: custom-value\r\nConnection: close\r\n\r\n",
                                            &reply))) {
        test_expect_string_contains(test, "HOST enum reads Host", reply.body, "1:example.test");
        test_expect_string_contains(test, "USER_AGENT enum reads User-Agent", reply.body, "1:cfw-suite");
        test_expect_string_contains(test, "RANGE enum reads Range", reply.body, "1:bytes=0-9");
        test_expect_string_contains(test, "custom_header_copy reads an unrecognized header", reply.body, "1:custom-value");
        test_expect_string_contains(test, "an absent header answers false and CLEARS the buffer", reply.body, "0:[]");
    }

    if (test_expect_true(test, "query round trip",
                         _client_round_trip(port, "GET /query?a=1&b=two HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_string(test, "query_copy returns the raw query without '?'", "1:[a=1&b=two]", reply.body);
    }

    if (test_expect_true(test, "no-query round trip",
                         _client_round_trip(port, "GET /query HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_string(test, "no query answers false and CLEARS the buffer", "0:[]", reply.body);
    }

    http_server_delete(&server);

    test_case_end(test);
}

static void _test_responses(Test *const test) {
    test_case_begin(test, "send_2 with an embedded NUL, send_empty, HEAD, header caps, send_file");

    HTTP_Server *server = http_server_new();

    http_server_route_add(server, "/nul", _route_nul);
    http_server_route_add(server, "/empty", _route_empty);
    http_server_route_add(server, "/notmodified", _route_notmodified);
    http_server_route_add(server, "/head", _route_head);
    http_server_route_add(server, "/bighdr", _route_big_headers);
    http_server_route_add(server, "/hdrlimits", _route_header_limits);
    http_server_route_add(server, "/nofile", _route_send_file_missing);
    http_server_route_add(server, "/file", _route_send_file);

    U16 const port = _start(test, server);

    _fixture_file_name_set(port);

    _Reply reply = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "embedded-NUL round trip", _client_round_trip(port, "GET /nul HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "send_2 writes past the embedded NUL", 8, reply.body_size);
        test_expect_true(test, "the NUL itself is on the wire", reply.body[3] == '\0');
        test_expect_true(test, "bytes after the NUL survive", reply.body[7] == 'g');
    }

    if (test_expect_true(test, "send_empty round trip", _client_round_trip(port, "GET /empty HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        char length[32] = DEFAULT_INITIALIZATION;

        test_expect_u(test, "send_empty keeps the caller's status", 204, reply.status);
        test_expect_false(test, "a 204 carries no Content-Length at all", _reply_header(&reply, "content-length:", length, sizeof(length)));
        test_expect_u(test, "no body bytes", 0, reply.body_size);
    }

    if (test_expect_true(test, "send_empty 304 round trip", _client_round_trip(port, "GET /notmodified HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        char length[32] = DEFAULT_INITIALIZATION;

        test_expect_u(test, "send_empty keeps a 304 status", 304, reply.status);
        test_expect_false(test, "a 304 carries no Content-Length either", _reply_header(&reply, "content-length:", length, sizeof(length)));
        test_expect_u(test, "no body bytes", 0, reply.body_size);
    }

    if (test_expect_true(test, "HEAD round trip", _client_round_trip(port, "HEAD /head HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        char length[32] = DEFAULT_INITIALIZATION;

        test_expect_u(test, "HEAD status 200", 200, reply.status);
        test_expect_true(test, "HEAD still declares Content-Length", _reply_header(&reply, "content-length:", length, sizeof(length)));
        test_expect_string(test, "the length a GET would have had", "10", length);
        test_expect_u(test, "HEAD carries no body", 0, reply.body_size);
    }

    /* Over 2048 bytes of extra headers: the send used to return with write_success still
     * true, so the transaction completed having written nothing and the client read an
     * empty reply that looked like success. It now fails the write loudly. */
    Net_Socket socket = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "oversize-header connection opens", _client_open(port, &socket))) {
        _Reply blank = DEFAULT_INITIALIZATION;

        _client_write(socket, "GET /bighdr HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n",
                      CHAR_STATIC_SIZE("GET /bighdr HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n"));

        test_expect_false(test, "an over-2048-byte header block writes NO reply at all", _client_read(socket, &blank));

        net_socket_close(socket);
    }

    if (test_expect_true(test, "header-limit round trip", _client_round_trip(port, "GET /hdrlimits HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        char injected[64]   = DEFAULT_INITIALIZATION;
        char clean[64]      = DEFAULT_INITIALIZATION;

        test_expect_string(test, "header_add refuses both the over-long line and the CRLF one", "00", reply.body);
        test_expect_false(test, "the injected header never reaches the wire", _reply_header(&reply, "x-injected:", injected, sizeof(injected)));
        test_expect_false(test, "the over-long header never reaches the wire", _reply_header(&reply, "x-long:", injected, sizeof(injected)));
        test_expect_true(test, "a clean header still gets through", _reply_header(&reply, "x-clean:", clean, sizeof(clean)));
        test_expect_string(test, "the clean header's value", "fine", clean);
    }

    if (test_expect_true(test, "missing-file round trip", _client_round_trip(port, "GET /nofile HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_string(test, "send_file answers false for a missing file", "refused", reply.body);
        test_expect_false(test, "send_file refused, so it never claimed success", _send_file_missing_ok);
    }

    File *fixture = file_open_try_1(_fixture_file_name, "wb");

    if (test_expect_true(test, "the suite can write its fixture file", fixture != nullptr)) {
        file_write_1(fixture, _FIXTURE_FILE_BODY, 1, CHAR_STATIC_SIZE(_FIXTURE_FILE_BODY));
        file_close(&fixture);
    }

    if (test_expect_true(test, "send_file round trip", _client_round_trip(port, "GET /file HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        char preformatted[64]   = DEFAULT_INITIALIZATION;
        char queued[64]         = DEFAULT_INITIALIZATION;

        test_expect_u(test, "the file is served 200", 200, reply.status);
        test_expect_true(test, "the caller's pre-formatted header is emitted", _reply_header(&reply, "x-preformatted:", preformatted, sizeof(preformatted)));
        test_expect_true(test, "the queued header is emitted too", _reply_header(&reply, "x-queued:", queued, sizeof(queued)));
        test_expect_u(test, "the file body is exactly what the suite wrote", CHAR_STATIC_SIZE(_FIXTURE_FILE_BODY), reply.body_size);
        test_expect_true(test, "the file bytes match", memcmp(reply.body, _FIXTURE_FILE_BODY, CHAR_STATIC_SIZE(_FIXTURE_FILE_BODY)) == 0);
    }

    /* HEAD is handled inside lws_serve_http_file, not by this module, so the promise that
     * a HEAD reply carries a GET's headers and no body has to be pinned on the FILE path
     * too, not only on the send_1 one above. */
    if (test_expect_true(test, "HEAD send_file round trip",
                         _client_round_trip(port, "HEAD /file HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        char length[64] = DEFAULT_INITIALIZATION;

        test_expect_u(test, "HEAD on a served file is 200", 200, reply.status);
        test_expect_true(test, "HEAD on a served file still declares Content-Length", _reply_header(&reply, "content-length:", length, sizeof(length)));
        test_expect_u(test, "HEAD on a served file carries no body", 0, reply.body_size);
    }

    test_expect_true(test, "the fixture file is removed", file_remove_1(_fixture_file_name) == RESULT_SUCCESS);
    http_server_delete(&server);

    test_case_end(test);
}

static void _test_client_ip(Test *const test) {
    test_case_begin(test, "get_client_ip walks X-Forwarded-For from the right");

    HTTP_Server *server = http_server_new();

    http_server_route_add(server, "/ip", _route_client_ip);

    U16 const port = _start(test, server);

    _Reply reply = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "forwarded round trip",
                         _client_round_trip(port,
                                            "GET /ip HTTP/1.1\r\nHost: t\r\n"
                                            "X-Forwarded-For: 203.0.113.7, 198.51.100.4, 192.0.2.9\r\nConnection: close\r\n\r\n",
                                            &reply))) {
        test_expect_string(test, "0 hops answers the peer, ignoring the header",
                           "127.0.0.1|192.0.2.9|198.51.100.4|203.0.113.7", reply.body);
    }

    if (test_expect_true(test, "no-header round trip", _client_round_trip(port, "GET /ip HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_string(test, "no X-Forwarded-For falls back to the peer at every hop count",
                           "127.0.0.1|127.0.0.1|127.0.0.1|127.0.0.1", reply.body);
    }

    http_server_delete(&server);

    test_case_end(test);
}

static void _test_client_ip_hostile(Test *const test) {
    test_case_begin(test, "get_client_ip: a 40-entry list cannot push the proxy's entry out of reach");

    HTTP_Server *server = http_server_new();

    http_server_route_add(server, "/ip", _route_client_ip);

    U16 const port = _start(test, server);

    /* 40 entries: more than the module's fixed hop array. Splitting from the LEFT kept
     * entries 1..32 and indexed from the right INSIDE that window, so hop 1 answered
     * 10.0.0.32 - an address the peer wrote - instead of 10.0.0.40, the one the nearest
     * trusted proxy appended. Splitting from the right makes the list's length irrelevant. */
    static char request[2048];

    USize cursor = _append(request, sizeof(request), 0, "GET /ip HTTP/1.1\r\nHost: t\r\nX-Forwarded-For: ");

    for (USize index = 0; index < _FORWARDED_TEST_ENTRIES; index += 1) {
        cursor = _append(request, sizeof(request), cursor, "%s10.0.0.%llu",
                         index == 0 ? "" : ", ", (unsigned long long) (index + 1));
    }

    _append(request, sizeof(request), cursor, "\r\nConnection: close\r\n\r\n");

    _Reply reply = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "40-entry forwarded round trip", _client_round_trip(port, request, &reply))) {
        test_expect_string(test, "hop 1 is the RIGHTMOST entry, not the 32nd from the left",
                           "127.0.0.1|10.0.0.40|10.0.0.39|10.0.0.32", reply.body);
    }

    if (test_expect_true(test, "non-literal forwarded round trip",
                         _client_round_trip(port,
                                            "GET /ip HTTP/1.1\r\nHost: t\r\nX-Forwarded-For: not-an-ip\r\nConnection: close\r\n\r\n",
                                            &reply))) {
        test_expect_string(test, "an entry that is not an address literal answers nothing at all, never the peer",
                           "127.0.0.1|||", reply.body);
    }

    if (test_expect_true(test, "mixed forwarded round trip",
                         _client_round_trip(port,
                                            "GET /ip HTTP/1.1\r\nHost: t\r\nX-Forwarded-For: unknown, 203.0.113.7\r\nConnection: close\r\n\r\n",
                                            &reply))) {
        test_expect_string(test, "a literal hop is reported; a non-literal one answers nothing, never the peer",
                           "127.0.0.1|203.0.113.7||", reply.body);
    }

    if (test_expect_true(test, "IPv6 forwarded round trip",
                         _client_round_trip(port,
                                            "GET /ip HTTP/1.1\r\nHost: t\r\nX-Forwarded-For: 2001:db8::1, ::ffff:192.0.2.9\r\nConnection: close\r\n\r\n",
                                            &reply))) {
        test_expect_string(test, "IPv6 literals, compressed and IPv4-mapped, are accepted",
                           "127.0.0.1|::ffff:192.0.2.9|2001:db8::1|2001:db8::1", reply.body);
    }

    /* An X-Forwarded-For past the module's 1024-byte buffer. Falling back to the peer here
     * handed the caller the PROXY's address, so padding the header let a client escape a
     * block on its own address and put its strikes on the proxy. Hop 0 never reads the
     * header at all, so the peer is still the right answer there. */
    cursor = _append(request, sizeof(request), 0, "GET /ip HTTP/1.1\r\nHost: t\r\nX-Forwarded-For: 203.0.113.7");

    while (cursor < 1100 + CHAR_STATIC_SIZE("GET /ip HTTP/1.1\r\nHost: t\r\nX-Forwarded-For: ")) {
        cursor = _append(request, sizeof(request), cursor, ", 203.0.113.7");
    }

    _append(request, sizeof(request), cursor, "\r\nConnection: close\r\n\r\n");

    if (test_expect_true(test, "over-long forwarded round trip", _client_round_trip(port, request, &reply))) {
        test_expect_string(test, "an over-long X-Forwarded-For answers nothing at all, never the proxy",
                           "127.0.0.1|||", reply.body);
    }

    http_server_delete(&server);

    server = http_server_new();

    http_server_route_add(server, "/deep", _route_client_ip_deep);

    U16 const deep_port = _start(test, server);

    /* 33 entries against a 32-slot array. `wanted = trusted_hops + 1` filled the array with
     * the list still running to the left and refused hop 31 - an entry already in hand. */
    cursor = _append(request, sizeof(request), 0, "GET /deep HTTP/1.1\r\nHost: t\r\nX-Forwarded-For: ");

    for (USize index = 0; index < _FORWARDED_DEEP_ENTRIES; index += 1) {
        cursor = _append(request, sizeof(request), cursor, "%s10.0.0.%llu",
                         index == 0 ? "" : ", ", (unsigned long long) (index + 1));
    }

    _append(request, sizeof(request), cursor, "\r\nConnection: close\r\n\r\n");

    if (test_expect_true(test, "33-entry forwarded round trip", _client_round_trip(deep_port, request, &reply))) {
        test_expect_string(test, "hop 31 and hop 32 are reached; hop 33, past the cap, answers nothing at all",
                           "10.0.0.3|10.0.0.2|", reply.body);
    }

    http_server_delete(&server);

    test_case_end(test);
}

static void _test_request_accessors_full(Test *const test) {
    test_case_begin(test, "every header enum value, capacity-1, content_type_is, custom-header ':' normalization");

    HTTP_Server *server = http_server_new();

    http_server_route_add(server, "/hdrall", _route_header_all);
    http_server_route_add(server, "/ctype", _route_content_type);
    http_server_route_add(server, "/custom", _route_custom_header);

    U16 const port = _start(test, server);

    _Reply reply = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "all-headers round trip",
                         _client_round_trip(port,
                                            "POST /hdrall HTTP/1.1\r\nHost: t\r\nAccept: */*\r\nAccept-Encoding: gzip\r\n"
                                            "Authorization: Bearer x\r\nContent-Length: 4\r\nContent-Type: text/plain\r\n"
                                            "Cookie: a=b\r\nIf-Modified-Since: Wed, 21 Oct 2015 07:28:00 GMT\r\n"
                                            "If-None-Match: \"tag\"\r\nIf-Range: \"tag\"\r\nOrigin: http://t\r\n"
                                            "Range: bytes=0-1\r\nReferer: http://t/a\r\nUser-Agent: cfw\r\n"
                                            "X-Forwarded-For: 203.0.113.7\r\nConnection: close\r\n\r\nabcd",
                                            &reply))) {
        char expected[64] = DEFAULT_INITIALIZATION;

        snprintf(expected, sizeof(expected), "%d|0|[]", _HEADER_ENUM_COUNT);

        test_expect_string(test, "every enum value maps to a token and reads its header", expected, reply.body);
    }

    if (test_expect_true(test, "content-type round trip",
                         _client_round_trip(port,
                                            "GET /ctype HTTP/1.1\r\nHost: t\r\nContent-Type: application/json; charset=utf-8\r\nConnection: close\r\n\r\n",
                                            &reply))) {
        test_expect_string(test, "the MIME matches, a parameter follows, and no prefix matches loosely", "1000", reply.body);
    }

    if (test_expect_true(test, "custom-header round trip",
                         _client_round_trip(port,
                                            "GET /custom HTTP/1.1\r\nHost: t\r\nX-Device-Id: pixel-7\r\nConnection: close\r\n\r\n",
                                            &reply))) {
        test_expect_string(test, "both spellings find the header, an over-long name is refused",
                           "1:pixel-7|1:pixel-7|0|pixel-7", reply.body);
    }

    http_server_delete(&server);

    test_case_end(test);
}

static void _test_owning_tiers(Test *const test) {
    test_case_begin(test, "owning tiers acquire and release cleanly, 64 rounds inside one callback");

    HTTP_Server *server = http_server_new();

    http_server_route_match_add(server, "/own/(?<file>[a-z0-9]+)", _route_owning);

    U16 const port = _start(test, server);

    _Reply reply = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "owning-tier round trip",
                         _client_round_trip(port, "POST /own/token9 HTTP/1.1\r\nHost: t\r\nContent-Length: 4\r\nConnection: close\r\n\r\nbody",
                                            &reply))) {
        test_expect_u(test, "the request survived 64 acquire/release rounds", 200, reply.status);
        test_expect_string(test, "every owning tier answered its own copy",
                           "127.0.0.1|9|9|body|token9|6|token9", reply.body);
    }

    http_server_delete(&server);

    test_case_end(test);
}

static void _test_content_length_and_zero_body(Test *const test) {
    test_case_begin(test, "a non-canonical Content-Length is 400; POST CL:0 leaves the keep-alive connection usable");

    HTTP_Server *server = http_server_new();

    http_server_route_add(server, "/echo", _route_echo);
    http_server_route_add(server, "/exact", _route_exact);

    U16 const port = _start(test, server);

    _Reply reply = DEFAULT_INITIALIZATION;

    /* lws' own atoll reads "12abc" as 12 and enters its body state; reading it here as
     * "no body" dispatched at once and let body completion dispatch a SECOND time. */
    if (test_expect_true(test, "malformed Content-Length round trip",
                         _client_round_trip(port, "POST /echo HTTP/1.1\r\nHost: t\r\nContent-Length: 12abc\r\nConnection: close\r\n\r\nhello",
                                            &reply))) {
        test_expect_u(test, "a Content-Length that is not a decimal number is 400", 400, reply.status);
    }

    if (test_expect_true(test, "signed Content-Length round trip",
                         _client_round_trip(port, "POST /echo HTTP/1.1\r\nHost: t\r\nContent-Length: +5\r\nConnection: close\r\n\r\nhello",
                                            &reply))) {
        test_expect_u(test, "a signed Content-Length is 400 too", 400, reply.status);
    }

    /* Two Content-Length lines. lws keeps them as fragments of one token and lws_hdr_copy
     * concatenates them with no separator, so "5" and "6" read here as "56" - all digits,
     * so the canonical-number check waved it through - while lws' own atoll read 5. That
     * disagreement is the smuggling shape RFC 9110 8.6 says to answer 400. */
    if (test_expect_true(test, "duplicate Content-Length round trip",
                         _client_round_trip(port, "POST /echo HTTP/1.1\r\nHost: t\r\nContent-Length: 5\r\nContent-Length: 6\r\nConnection: close\r\n\r\nhello",
                                            &reply))) {
        test_expect_u(test, "a repeated Content-Length is 400, not a concatenated 56", 400, reply.status);
    }

    /* POST with Content-Length: 0. lws fires LWS_CALLBACK_HTTP and then, because the
     * length is explicitly zero and the method is POST, HTTP_BODY + BODY_COMPLETION
     * anyway. Answering in HTTP completed the transaction, so body completion found no
     * URI, returned -1, and dropped a keep-alive connection that had just been answered
     * correctly - which only a SECOND request on the same socket can show. */
    Net_Socket socket = DEFAULT_INITIALIZATION;

    _body_seen_count = 0;

    if (test_expect_true(test, "keep-alive connection opens", _client_open(port, &socket))) {
        _Reply first    = DEFAULT_INITIALIZATION;
        _Reply second   = DEFAULT_INITIALIZATION;

        bool ok = _client_write(socket, "POST /echo HTTP/1.1\r\nHost: t\r\nContent-Length: 0\r\n\r\n",
                                CHAR_STATIC_SIZE("POST /echo HTTP/1.1\r\nHost: t\r\nContent-Length: 0\r\n\r\n"));

        ok = ok && _client_read(socket, &first);

        if (test_expect_true(test, "the zero-length POST is answered", ok)) {
            test_expect_string(test, "answered once, with an empty body", "POST|0|", first.body);
        }

        ok = ok && _client_write(socket, "GET /exact HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n",
                                 CHAR_STATIC_SIZE("GET /exact HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n"));

        ok = ok && _client_read(socket, &second);

        if (test_expect_true(test, "the connection survived the zero-length POST", ok)) {
            test_expect_string(test, "the GET after it is answered on the SAME socket", "exact", second.body);
        }

        test_expect_u(test, "the zero-length POST was dispatched exactly once", 1, _body_seen_count);

        net_socket_close(socket);
    }

    http_server_delete(&server);

    test_case_end(test);
}

static void _test_lifecycle(Test *const test) {
    test_case_begin(test, "lifecycle: run twice refused, register after run refused, stop, delete joins");

    HTTP_Server *server = http_server_new();

    http_server_route_add(server, "/exact", _route_exact);

    U16 const port = _start(test, server);

    struct lws_protocols late = {
        .name                   = "too-late",
        .callback               = _stub_protocol_callback,
        .per_session_data_size  = 0,
        .rx_buffer_size         = 0,
        .id                     = 0,
        .user                   = nullptr,
        .tx_packet_size         = 0
    };

    test_expect_false(test, "register_protocol is refused once the server runs", http_server_register_protocol(server, &late));

    Result const again = http_server_run(server, 0, true);

    test_expect_true(test, "a second run on a running server is refused", result_is_error(again));
    test_expect_u(test, "the refused second run left the port alone", port, http_server_get_port(server));

    _Reply reply = DEFAULT_INITIALIZATION;

    test_expect_true(test, "the server still answers after the refused second run",
                     _client_round_trip(port, "GET /exact HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply));

    /* stop is TERMINAL: it clears the running flag but leaves the libwebsockets context
     * standing, so a run afterwards is refused exactly like a run on a live server.
     * Restarting means uninit + init + re-registering, which is what the header says. */
    http_server_stop(server);

    Result const after_stop = http_server_run(server, 0, true);

    test_expect_true(test, "run after stop is refused - stop is terminal for the handle", result_is_error(after_stop));

    /* delete joins the service thread; if stop had not woken it, this would hang. */
    http_server_delete(&server);

    test_expect_null(test, "delete joined and nulled the handle", server);

    Net_Socket socket = DEFAULT_INITIALIZATION;

    test_expect_false(test, "nothing listens on the port after stop + delete", _client_open(port, &socket));

    test_case_end(test);
}

static void _test_run_refuses_a_taken_port(Test *const test) {
    test_case_begin(test, "run answers a Result instead of aborting on a bind failure");

    HTTP_Server *first = http_server_new();

    http_server_route_add(first, "/exact", _route_exact);

    U16 const port = _start(test, first);

    HTTP_Server *second = http_server_new();

    http_server_set_handler(second, _handler, second);

    Result const result = http_server_run(second, port, true);

    test_expect_true(test, "binding a port already in use is an error, not an abort", result_is_error(result));
    test_expect_u(test, "the refused server reports no port", 0, http_server_get_port(second));

    http_server_delete(&second);
    http_server_delete(&first);

    test_case_end(test);
}

static void _test_default_callback(Test *const test) {
    test_case_begin(test, "with NO handler set, the strong default callback answers");

    HTTP_Server *server = http_server_new();

    /* Deliberately NOT _start: that sets a handler, and the module reaches
     * http_server_request_default_callback only when none is set. This suite defines a
     * STRONG one, so what has to arrive is this file's body - not the module's weak 404. */
    http_server_route_add(server, "/exact", _route_exact);

    Result const result = http_server_run(server, 0, true);

    test_expect_true(test, "http_server_run succeeds with no handler set", result_is_success(result));

    U16 const port = http_server_get_port(server);

    test_expect_true(test, "an ephemeral port was assigned", port != 0);

    _Reply reply = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "no-handler round trip",
                         _client_round_trip(port, "GET /exact HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_string(test, "the strong override reaches the wire, not the weak 404 body", "default", reply.body);
        test_expect_u(test, "the override's status is what went out", 404, reply.status);
    }

    http_server_delete(&server);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Measurements
 *============================================================================*/
static void _measure_route_table(Test *const test) {
    test_case_begin(test, "measurement: worst-case dispatch over 80 routes");

    HTTP_Server *server = http_server_new();

    char path[64] = DEFAULT_INITIALIZATION;

    /* 40 exact + 40 regex, the shape a real CRM carries. The measured path matches
     * NOTHING, so every route including all 40 pcre2 patterns is tried: the worst case. */
    for (USize index = 0; index < _ROUTE_BENCHMARK_ROUTES / 2; index += 1) {
        snprintf(path, sizeof(path), "/exact/resource/%llu", (unsigned long long) index);
        http_server_route_add(server, path, _route_benchmark);

        snprintf(path, sizeof(path), "/regex/resource/%llu/([a-z0-9]+)", (unsigned long long) index);
        http_server_route_match_add(server, path, _route_benchmark);
    }

    U16 const port = _start(test, server);

    (void) port;

    HTTP_Server_Holder  holder  = { .request = nullptr, .response = nullptr, .arena = nullptr };
    char const *const   miss    = "/no/such/route/at/all";
    USize               matched = 0;

    ChronoInstant const start = chrono_now();

    for (USize round = 0; round < _ROUTE_BENCHMARK_ROUNDS; round += 1) {
        if (http_server_router_dispatch_1(server->router, miss, &holder)) {
            matched += 1;
        }
    }

    ChronoDuration  const   elapsed         = chrono_elapsed(start);
    U64             const   microseconds    = chrono_duration_microseconds(elapsed);

    test_expect_u(test, "the worst-case path matches nothing", 0, matched);

    log_message_1(LOG_LEVEL_ERROR,
                  "MEASUREMENT route-table: %d routes (40 exact + 40 regex), %d worst-case dispatches in %llu us = %.3f us/dispatch\n",
                  _ROUTE_BENCHMARK_ROUTES, _ROUTE_BENCHMARK_ROUNDS,
                  (unsigned long long) microseconds,
                  (double) microseconds / (double) _ROUTE_BENCHMARK_ROUNDS);

    http_server_delete(&server);

    test_case_end(test);
}

static void _measure_send_throughput(Test *const test) {
    test_case_begin(test, "measurement: send_2 responses per second at a 16 KiB body");

    HTTP_Server *server = http_server_new();

    for (USize index = 0; index < sizeof(_big_body); index += 1) {
        _big_body[index] = (Byte) ('A' + (index % 26));
    }

    http_server_route_add(server, "/big", _route_big_body);

    U16 const port = _start(test, server);

    USize const             rounds  = 300;
    USize                   served  = 0;
    USize                   bytes   = 0;
    ChronoInstant   const   start   = chrono_now();

    for (USize round = 0; round < rounds; round += 1) {
        _Reply reply = DEFAULT_INITIALIZATION;

        if (_client_round_trip(port, "GET /big HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply) && reply.complete) {
            served += 1;
            bytes += reply.body_size;
        }
    }

    U64 const microseconds = chrono_duration_microseconds(chrono_elapsed(start));

    test_expect_u(test, "every request was answered in full", rounds, served);
    test_expect_u(test, "every body arrived whole", rounds * sizeof(_big_body), bytes);

    log_message_1(LOG_LEVEL_ERROR,
                  "MEASUREMENT send_2: %llu round trips at a %llu-byte body in %llu us = %.0f responses/s (one connection each)\n",
                  (unsigned long long) served, (unsigned long long) sizeof(_big_body), (unsigned long long) microseconds,
                  microseconds > 0 ? (double) served * 1000000.0 / (double) microseconds : 0.0);

    http_server_delete(&server);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Entry point
 *============================================================================*/
I32 main(void) {
    LogConfig const log_config = { .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true };

    log_init(log_config);

    /* Route libwebsockets' own diagnostics into the CFW log rather than bare stderr. */
    http_server_set_log_level(LLL_ERR);

    Test test = test_init("http_server");

    test_suite_begin(&test, "http_server offline");

    _test_delete_without_register(&test);
    _test_register_single(&test);
    _test_register_fill_to_max(&test);
    _test_init_in_place(&test);
    _test_payload_cap_semantics(&test);
    _test_arena_new_and_delete(&test);
    _test_arena_new_refuses_an_exhausted_arena(&test);

    test_suite_end(&test);

    test_suite_begin(&test, "http_server live");

    _test_routing(&test);
    _test_uri_too_long(&test);
    _test_bodies(&test);
    _test_content_length_and_zero_body(&test);
    _test_payload_limits(&test);
    _test_request_accessors(&test);
    _test_request_accessors_full(&test);
    _test_owning_tiers(&test);
    _test_responses(&test);
    _test_client_ip(&test);
    _test_client_ip_hostile(&test);
    _test_default_callback(&test);
    _test_lifecycle(&test);
    _test_run_refuses_a_taken_port(&test);

    test_suite_end(&test);

    test_suite_begin(&test, "http_server measurements");

    _measure_route_table(&test);
    _measure_send_throughput(&test);

    test_suite_end(&test);

    return test_uninit(&test);
}