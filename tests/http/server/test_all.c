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
#ifndef _WIN32
#include <dir/dir.h>
#endif // _WIN32
#include <math/scalar.h>
#include <http/server/http_server.h>
#include <log/log.h>
#include <net/net.h>
#include <test/test.h>
#include <thread/thread.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/
/* A raw header block every raw path must accept: an HTAB inside a value and an empty value. */
#define _BLOCK_ACCEPTED             "X-Raw-Good: a\tb\r\nX-Raw-Empty: \r\n"
/* The CR/LF chokepoint case: its served file, one formatted assertion name, and one byte per
 * send_file call it makes (seven blocks, five types, the accepted call, the terminator). */
#define _CHOKEPOINT_FIXTURE_SIZE    64
#define _CHOKEPOINT_NAME_MAX        96
#define _CHOKEPOINT_RECORD_MAX      16
#define _CLIENT_BODY_MAX            65536
#define _CLIENT_HEADERS_MAX         8192
#define _CLIENT_IO_TIMEOUT_MS       4000
#define _CLIENT_RAW_MAX             131072
/* A content type every sender must accept: HTAB is RFC 9110 whitespace inside a field value. */
#define _CONTENT_TYPE_ACCEPTED      "text/plain;\tcharset=utf-8"
#define _DECIMAL_RADIX              10
/* A fixture the server thread may still hold is retried this often before it counts as stuck. */
#define _FIXTURE_REMOVE_ATTEMPTS    40
#define _FIXTURE_REMOVE_SLEEP_MS    25
#define _FORWARDED_DEEP_ENTRIES     33
#define _FORWARDED_TEST_ENTRIES     40
/* Two handle-count readings this far apart that agree make a baseline. */
#define _HANDLE_BASELINE_SLEEP_MS   100
/* Handle-count pins poll this many rounds, this far apart (4 s), for the server to tear its wsi down. */
#define _HANDLE_SETTLE_ROUNDS       160
#define _HANDLE_SETTLE_SLEEP_MS     25
#define _HEADER_ENUM_COUNT          15
#define _OWNING_ROUNDS              64
/* The stream fixtures' integer-hash mixer: two odd multipliers and three xor-shifts. */
#define _PATTERN_MULTIPLIER_1       2654435761u
#define _PATTERN_MULTIPLIER_2       2246822519u
#define _PATTERN_SHIFT_1            15
#define _PATTERN_SHIFT_2            13
#define _PATTERN_SHIFT_3            8
#define _PAYLOAD_TEST_MAX           4096
/* A short header value ("yes", "0") and a numeric one (a Content-Length). */
#define _REPLY_FLAG_MAX             16
#define _REPLY_VALUE_MAX            32
#define _ROUTE_BENCHMARK_ROUNDS     20000
#define _ROUTE_BENCHMARK_ROUTES     80
/* 2 MiB: large enough that the client's stall leaves the stream in flight (loopback buffers
 * hold far less), so the disconnect and truncation pins exercise the mid-stream paths. */
#define _STREAM_BIG_FIXTURE_SIZE    (2 * 1024 * 1024)
/* A connection the server closed must do so well inside the client's 4 s IO timeout. */
#define _STREAM_CLOSE_MAX_MS        3000
/* 1 MiB + 1: crosses 16 chunk boundaries and ends mid-chunk. */
#define _STREAM_FIXTURE_SIZE        (1024 * 1024 + 1)
/* The past-EOF refusal asks for a window that starts this close to the end and runs past it. */
#define _STREAM_PAST_EOF_BACK       5
#define _STREAM_READ_MAX            65536
/* The plain one-shot stream request: the server closes once the body is sent. */
#define _STREAM_REQUEST_CLOSE       "GET /stream HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n"
/* The slow reader's receive buffer, pinned before connect so the RECEIVE side holds 4 KiB.
 * It does not bound the server's send buffer, which a kernel autotunes on its own (Windows
 * loopback was measured buffering ~5 MB for one slow reader) - _STREAM_SLOW_FIXTURE_SIZE and
 * the held-handle check are what keep the stream genuinely in flight while it stalls. */
#define _STREAM_SLOW_BUFFER         4096
/* 32 MiB: far past what either kernel buffers for one connection, so the server still has
 * chunks left to write when the stall ends. Only the slow-reader case writes it. */
#define _STREAM_SLOW_FIXTURE_SIZE   (32 * 1024 * 1024)
/* Over the baseline while the stream is in flight: the client's own socket, the server's
 * accepted socket, and the stream's FILE*. A server that finished (or dropped) the stream
 * holds neither of its two, so it reads 1. */
#define _STREAM_SLOW_HELD_HANDLES   3
/* Past the 20 s a chunk used to get (with 3 s of margin), well inside the 129 s a full chunk
 * gets at the 1 KiB/s minimum read rate over two windows of drain. */
#define _STREAM_SLOW_STALL_MS       23000
#define _STREAM_TRUNCATE_TO         1024
/* Straddles the first chunk boundary (65531 + 10 > 65536). */
#define _STREAM_WINDOW_SIZE         10
#define _STREAM_WINDOW_START        65531

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

/**
 * @brief _client_open with SO_RCVBUF pinned to RECEIVE_BUFFER_SIZE BEFORE the connect, so the
 *        window the server sees stays that small instead of autotuning past the body. Raw
 *        setsockopt, because net has no receive-buffer setter.
 */
static bool _client_open_receive_buffer(U16 const port, I32 const receive_buffer_size, Net_Socket *const out) {
    Net_Socket_Address address = DEFAULT_INITIALIZATION;

    if (result_is_error(net_socket_address_init_2(NET_FAMILY_IPV4, port, "127.0.0.1", &address))) {
        return false;
    }

    if (result_is_error(net_socket_init(out, NET_FAMILY_IPV4, NET_TYPE_TCP))) {
        return false;
    }

    if (setsockopt(*out, SOL_SOCKET, SO_RCVBUF, (char const*) &receive_buffer_size, sizeof(receive_buffer_size)) != 0 || result_is_error(net_socket_connect(*out, &address))) {
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

/**
 * @brief Read one reply's status line and header block only, never its body.
 *
 * The body loop in _client_read waits for Content-Length bytes, which a HEAD reply
 * declares but never sends - so a HEAD read on a keep-alive socket either blocks for the
 * whole IO timeout or, when the server is buggy enough to write a SECOND reply, silently
 * consumes that reply as the HEAD's body. This helper stops at the header terminator so
 * the caller can prove what does (or does not) arrive next on the same connection.
 */
static bool _client_read_headers(Net_Socket const socket, _Reply *const reply) {
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

    /* Whatever the peer had already written past this reply's headers - zero for a correct
     * HEAD answer, the first bytes of a spurious second reply otherwise. */
    reply->body_size = raw_size - header_end;

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

/** @brief The Content-Length a reply declares, or 0. */
static USize _reply_content_length(_Reply const *const reply) {
    char    text[_REPLY_VALUE_MAX]  = DEFAULT_INITIALIZATION;
    USize   value                   = 0;

    if (_reply_header(reply, "content-length:", text, sizeof(text))) {
        for (USize index = 0; text[index] >= '0' && text[index] <= '9'; index += 1) {
            value = (value * _DECIMAL_RADIX) + (USize) (text[index] - '0');
        }
    }

    return value;
}

/** @brief How many times a header NAME (lowercase, colon included) occurs in a reply. */
static USize _reply_header_count(_Reply const *const reply, char const *const name) {
    static char lowered[_CLIENT_HEADERS_MAX] = DEFAULT_INITIALIZATION;

    USize const size        = reply->headers_size < sizeof(lowered) ? reply->headers_size : sizeof(lowered) - CHAR_END_CHARACTER;
    USize const name_size   = char_length(name);
    USize       offset      = 0;
    USize       count       = 0;

    memory_copy_1((Byte*) lowered, (Byte*) reply->headers, size);

    lowered[size] = '\0';

    char_lower_2(lowered, size);

    /* Guarded on the remainder: char_find_slice_5 aborts when the needle outruns the haystack. */
    while (size - offset >= name_size) {
        char const *const found = char_find_slice_5(lowered + offset, size - offset, 0, (char*) name, name_size);

        if (memory_empty(found)) {
            break;
        }

        count   += 1;
        offset  = (USize) (found - lowered) + name_size;
    }

    return count;
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

/* The send_file_handle routes. The window, status and fixture are set by the test before each
 * request and read by the route on the service thread - sequenced by the request itself, as
 * the suite's other route globals are. */
static char _stream_fixture_name[CHAR_STATIC_SIZE("http_server_suite_stream_65535.bin") + CHAR_END_CHARACTER]         = DEFAULT_INITIALIZATION;
static char _stream_big_fixture_name[CHAR_STATIC_SIZE("http_server_suite_stream_big_65535.bin") + CHAR_END_CHARACTER] = DEFAULT_INITIALIZATION;

static char const   *_stream_served = nullptr;
static USize        _stream_start   = 0;
static USize        _stream_size    = 0;
static U16          _stream_status  = HTTP_SERVER_STATUS_CODE_OK;
static bool         _stream_ok      = false;

static void _stream_fixture_names_set(U16 const port) {
    snprintf(_stream_fixture_name, sizeof(_stream_fixture_name), "http_server_suite_stream_%u.bin", (unsigned) port);
    snprintf(_stream_big_fixture_name, sizeof(_stream_big_fixture_name), "http_server_suite_stream_big_%u.bin", (unsigned) port);
}

static void _route_stream(HTTP_Server_Route *const route) {
    HTTP_Server_Holder *const holder = http_server_route_get_holder(route);

    File *file = file_open_try_1(_stream_served, "rb");

    if (memory_empty(file)) {
        http_server_response_send_1(holder->response, "no fixture", HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_INTERNAL_SERVER_ERROR);

        return;
    }

    http_server_response_header_add(holder->response, "X-Queued", "yes");

    _stream_ok = http_server_response_send_file_handle(holder->response, &file, _stream_start, _stream_size, HTTP_SERVER_CONTENT_TYPE_APPLICATION_OCTET_STREAM, _stream_status);

    /* A refusal leaves the handle with the caller and writes nothing, so the route's own
     * reply goes out - the shape static takes for its 416. */
    if (!_stream_ok) {
        file_close(&file);

        http_server_response_send_1(holder->response, "fallback", HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_REQ_RANGE_NOT_SATISFIABLE);
    }
}

static void _route_stream_after_send(HTTP_Server_Route *const route) {
    HTTP_Server_Holder *const holder = http_server_route_get_holder(route);

    _send_text(holder->response, "first");

    File *file = file_open_try_1(_stream_served, "rb");

    if (memory_empty(file)) {
        return;
    }

    _stream_ok = http_server_response_send_file_handle(holder->response, &file, 0, _stream_size, HTTP_SERVER_CONTENT_TYPE_APPLICATION_OCTET_STREAM, HTTP_SERVER_STATUS_CODE_OK);

    if (!memory_empty(file)) {
        file_close(&file);
    }
}

/* A handle that is not a regular file. Linux: fopen of a directory succeeds and file_regular
 * says so. Windows: fopen refuses a directory and file_regular is tautologically true, so the
 * equivalent is the NUL device - a handle with no file size, which the window check refuses. */
static void _route_stream_irregular(HTTP_Server_Route *const route) {
    HTTP_Server_Holder *const holder = http_server_route_get_holder(route);

#ifdef _WIN32
    File *file = file_open_try_1("NUL", "rb");
#else
    File *file = file_open_try_1(".", "rb");
#endif // _WIN32

    if (memory_empty(file)) {
        http_server_response_send_1(holder->response, "no handle", HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_INTERNAL_SERVER_ERROR);

        return;
    }

    _stream_ok = http_server_response_send_file_handle(holder->response, &file, 0, 1, HTTP_SERVER_CONTENT_TYPE_APPLICATION_OCTET_STREAM, HTTP_SERVER_STATUS_CODE_OK);

    if (!memory_empty(file)) {
        file_close(&file);
    }

    http_server_response_send_1(holder->response, "fallback", HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_NOT_FOUND);
}

/* The CR/LF chokepoint routes. A content type and a raw header block both reach the wire verbatim
 * through libwebsockets, so every refused shape carries an "X-Injected" line that would surface as
 * a header of its own if the refusal let it through. The type is set by the test before each
 * request, sequenced by the request itself as the stream globals are. */
static char const *const _CONTENT_TYPES_REFUSED[] = {
    "text/plain\r\nX-Injected: crlf",
    "text/plain\rX-Injected: cr",
    "text/plain\nX-Injected: lf",
    "text/plain\x01",
    "text/plain\x7F"
};

/* One per refusal in _http_server_header_lines_valid, in its order. */
static char const *const _MALFORMED_BLOCKS[] = {
    "X-Raw: ok\r\nX-Injected: unterminated",
    "X-Raw: ok\nX-Injected: bare-lf\r\n",
    "X-Raw: ok\rX-Injected: bare-cr\r\n",
    "X-Raw: ok\r\n\r\nX-Injected: empty-line\r\n",
    "X Injected: not-a-token\r\n",
    "X-Injected:no-space\r\n",
    "X-Injected: control\x01\r\n"
};

static char const   *_content_type_value                        = HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN;
static char         _file_blocks_record[_CHOKEPOINT_RECORD_MAX] = DEFAULT_INITIALIZATION;
static bool         _type_stream_handle_kept                    = false;

static void _route_type_send_1(HTTP_Server_Route *const route) {
    http_server_response_send_1(http_server_route_get_holder(route)->response, "typed", _content_type_value, HTTP_SERVER_STATUS_CODE_OK);
}

static void _route_type_send_2(HTTP_Server_Route *const route) {
    http_server_response_send_2(http_server_route_get_holder(route)->response, (Byte const*) "typed", CHAR_STATIC_SIZE("typed"), _content_type_value, HTTP_SERVER_STATUS_CODE_OK);
}

/* A refused type is a refusal like the window's: false, nothing written, the response NOT failed,
 * so the route's fallback send_2 must reach the peer - and the handle is still the route's. */
static void _route_type_stream(HTTP_Server_Route *const route) {
    HTTP_Server_Holder *const holder = http_server_route_get_holder(route);

    File *file = file_open_try_1(_stream_served, "rb");

    if (memory_empty(file)) {
        return;
    }

    _stream_ok = http_server_response_send_file_handle(holder->response, &file, 0, _stream_size, _content_type_value, HTTP_SERVER_STATUS_CODE_OK);

    _type_stream_handle_kept = !memory_empty(file);

    if (!_stream_ok) {
        file_close(&file);

        http_server_response_send_2(holder->response, (Byte const*) "fallback", CHAR_STATIC_SIZE("fallback"), HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN,
                                    HTTP_SERVER_STATUS_CODE_INTERNAL_SERVER_ERROR);
    }
}

/* One byte per call - '1' accepted, '0' refused - sent back as the body. */
static void _route_raw_blocks(HTTP_Server_Route *const route) {
    HTTP_Server_Holder *const holder = http_server_route_get_holder(route);

    USize cursor = 0;

    for (USize index = 0; index < sizeof(_MALFORMED_BLOCKS) / sizeof(_MALFORMED_BLOCKS[0]); index += 1) {
        char const *const block = _MALFORMED_BLOCKS[index];

        _scratch[cursor] = http_server_response_header_add_raw(holder->response, block, char_length(block)) ? '1' : '0';
        cursor += 1;
    }

    /* header_add hands its line to the raw path, so a name that passes header_add's own
     * control-byte check but is not a token is refused there. */
    _scratch[cursor] = http_server_response_header_add(holder->response, "X Injected", "header-add") ? '1' : '0';
    cursor += 1;

    _scratch[cursor] = http_server_response_header_add_raw(holder->response, _BLOCK_ACCEPTED, CHAR_STATIC_SIZE(_BLOCK_ACCEPTED)) ? '1' : '0';
    cursor += 1;

    http_server_response_send_2(holder->response, (Byte const*) _scratch, cursor, HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_OK);
}

/* One byte per send_file call into _file_blocks_record. A refusal writes nothing and leaves the
 * response to the next call, so the last, well-formed call is the one that serves the file. */
static void _route_file_blocks(HTTP_Server_Route *const route) {
    HTTP_Server_Holder *const holder = http_server_route_get_holder(route);

    USize cursor = 0;

    for (USize index = 0; index < sizeof(_MALFORMED_BLOCKS) / sizeof(_MALFORMED_BLOCKS[0]); index += 1) {
        char const *const block = _MALFORMED_BLOCKS[index];
        bool const served = http_server_response_send_file(holder->response, _stream_served, HTTP_SERVER_CONTENT_TYPE_APPLICATION_OCTET_STREAM, block, char_length(block));

        _file_blocks_record[cursor] = served ? '1' : '0';
        cursor += 1;
    }

    for (USize index = 0; index < sizeof(_CONTENT_TYPES_REFUSED) / sizeof(_CONTENT_TYPES_REFUSED[0]); index += 1) {
        _file_blocks_record[cursor] = http_server_response_send_file(holder->response, _stream_served, _CONTENT_TYPES_REFUSED[index], nullptr, 0) ? '1' : '0';
        cursor += 1;
    }

    _file_blocks_record[cursor] = http_server_response_send_file(holder->response, _stream_served, _CONTENT_TYPE_ACCEPTED, _BLOCK_ACCEPTED, CHAR_STATIC_SIZE(_BLOCK_ACCEPTED)) ? '1' : '0';
    cursor += 1;

    _file_blocks_record[cursor] = '\0';
}

/*==============================================================================
 * MARK: - Streamed-file client
 *============================================================================*/
/** @brief Byte INDEX of the stream fixtures: a pure function of the offset, so any window can be checked without the file, with no short period a chunk boundary could hide behind. */
static Byte _pattern_byte(USize const index) {
    U32 value = (U32) index * _PATTERN_MULTIPLIER_1;

    value ^= value >> _PATTERN_SHIFT_1;
    value *= _PATTERN_MULTIPLIER_2;
    value ^= value >> _PATTERN_SHIFT_2;

    return (Byte) (value ^ (value >> _PATTERN_SHIFT_3));
}

/** @brief Write SIZE pattern bytes to NAME, 64 KiB at a time. */
static bool _fixture_write(char const *const name, USize const size) {
    static Byte block[_STREAM_READ_MAX] = DEFAULT_INITIALIZATION;

    File *file = file_open_try_1(name, "wb");

    if (memory_empty(file)) {
        return false;
    }

    USize   written = 0;
    bool    ok      = true;

    while (ok && written < size) {
        USize const count = math_min_u(size - written, sizeof(block));

        for (USize index = 0; index < count; index += 1) {
            block[index] = _pattern_byte(written + index);
        }

        ok      = file_write_1(file, block, 1, count) == count;
        written += count;
    }

    file_close(&file);

    return ok;
}

/**
 * @brief Read one reply's headers, then its body up to Content-Length or EOF, checking every
 *        body byte against the pattern from WINDOW_START without buffering the body (the
 *        _Reply body cap is 64 KiB; the fixtures are MiBs).
 * @param out_size  Body bytes received.
 * @param out_exact Every received byte matched the pattern.
 * @param out_eof   The peer closed before Content-Length was reached (false on a timeout too).
 */
static bool _client_read_window(Net_Socket const socket, _Reply *const reply, USize const window_start, USize *const out_size, bool *const out_exact, bool *const out_eof) {
    static Byte buffer[_STREAM_READ_MAX] = DEFAULT_INITIALIZATION;

    if (!_client_read_headers(socket, reply)) {
        return false;
    }

    USize const expected    = _reply_content_length(reply);
    USize       received    = reply->body_size;
    bool        exact       = true;
    bool        eof         = false;

    /* Whatever arrived behind the header terminator in the same read is the body's start. */
    for (USize index = 0; index < reply->body_size; index += 1) {
        exact = exact && (Byte) _raw[reply->headers_size + index] == _pattern_byte(window_start + index);
    }

    while (received < expected) {
        USize           got     = 0;
        Result  const   result  = net_socket_recv_1(socket, buffer, sizeof(buffer), &got);

        if (result_is_error(result)) {
            break;
        }

        if (got == 0) {
            eof = true;

            break;
        }

        for (USize index = 0; index < got; index += 1) {
            exact = exact && buffer[index] == _pattern_byte(window_start + received + index);
        }

        received += got;
    }

    *out_size   = received;
    *out_exact  = exact;
    *out_eof    = eof;

    return true;
}

/**
 * @brief The process's open-handle count: /proc/self/fd entries on Linux (the listing's own
 *        descriptor is counted on every call alike), GetProcessHandleCount on Windows. The
 *        server runs in-process, so a File* the seam failed to close shows up here.
 */
static bool _handle_count(USize *const out) {
#ifdef _WIN32
    DWORD count = 0;

    if (!GetProcessHandleCount(GetCurrentProcess(), &count)) {
        return false;
    }

    *out = (USize) count;

    return true;
#else
    DirEntry    *entries    = nullptr;
    USize       count       = 0;

    if (!dir_list_entries_1("/proc/self/fd", &entries, &count)) {
        return false;
    }

    dir_list_entries_uninit(entries, count);

    *out = count;

    return true;
#endif // _WIN32
}

/** @brief Poll until the handle count is back at BASELINE: the server tears its wsi down on its own thread, after the client already has its bytes. */
static bool _handles_settle(USize const baseline, USize *const out_last) {
    for (USize round = 0; round < _HANDLE_SETTLE_ROUNDS; round += 1) {
        USize count = 0;

        if (_handle_count(&count) && count == baseline) {
            *out_last = count;

            return true;
        }

        *out_last = count;

        thread_sleep(_HANDLE_SETTLE_SLEEP_MS);
    }

    return false;
}

/** @brief A baseline is two readings 100 ms apart that agree - a closing wsi from the request before is not counted into it. */
static bool _handles_baseline(USize *const out) {
    USize previous = 0;

    if (!_handle_count(&previous)) {
        return false;
    }

    for (USize round = 0; round < _HANDLE_SETTLE_ROUNDS; round += 1) {
        USize count = 0;

        thread_sleep(_HANDLE_BASELINE_SLEEP_MS);

        if (!_handle_count(&count)) {
            return false;
        }

        if (count == previous) {
            *out = count;

            return true;
        }

        previous = count;
    }

    return false;
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

    /*
     * The keep-alive half of the same promise, and the regression pin for the defect it
     * caught: lws_serve_http_file answers a HEAD itself and reports the transaction already
     * COMPLETE (>0), so returning "a file is streaming, leave the wsi alone" from the http
     * callback let libwebsockets re-enter the handler for a request that was already
     * answered, and the module wrote a SECOND reply - a 404 - onto the same socket. A
     * keep-alive client read that 404 as the answer to its NEXT request.
     */
    Net_Socket closing = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "HEAD closing connection opens", _client_open(port, &closing))) {
        bool const ok = _client_write(closing, "HEAD /file HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n",
                                      CHAR_STATIC_SIZE("HEAD /file HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n"));

        if (test_expect_true(test, "HEAD on a closing socket answers", ok && _client_read_headers(closing, &reply))) {
            test_expect_u(test, "the HEAD is 200", 200, reply.status);
            test_expect_u(test, "nothing follows the HEAD reply in the same read", 0, reply.body_size);
        }

        _Reply second = DEFAULT_INITIALIZATION;

        test_expect_false(test, "and no SECOND reply is written onto the same socket", _client_read_headers(closing, &second));

        net_socket_close(closing);
    }

    Net_Socket keep_alive = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "HEAD keep-alive connection opens", _client_open(port, &keep_alive))) {
        bool ok = _client_write(keep_alive, "HEAD /file HTTP/1.1\r\nHost: t\r\n\r\n", CHAR_STATIC_SIZE("HEAD /file HTTP/1.1\r\nHost: t\r\n\r\n"));

        if (test_expect_true(test, "HEAD on a kept-alive socket answers", ok && _client_read_headers(keep_alive, &reply))) {
            test_expect_u(test, "the kept-alive HEAD is 200", 200, reply.status);
            test_expect_u(test, "nothing follows the HEAD reply on the wire", 0, reply.body_size);
        }

        ok = _client_write(keep_alive, "GET /nul HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n",
                           CHAR_STATIC_SIZE("GET /nul HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n"));

        if (test_expect_true(test, "the socket still serves the next request", ok && _client_read(keep_alive, &reply))) {
            test_expect_u(test, "the request after a HEAD is answered normally", 200, reply.status);
            test_expect_u(test, "and gets its own body, not the HEAD's leftovers", 8, reply.body_size);
        }

        net_socket_close(keep_alive);
    }

    /*
     * Retried, not attempted once: on Windows a file libwebsockets still holds open cannot be
     * deleted, and the HEAD cases above finish the moment the CLIENT has its reply - the server
     * thread may not have torn its wsi down yet. Without the retry this pin failed intermittently
     * and left a stray fixture behind. The assertion is unchanged: the file must be gone.
     */
    Result removed = file_remove_1(_fixture_file_name);

    for (USize attempt = 0; attempt < 40 && removed != RESULT_SUCCESS; attempt += 1) {
        thread_sleep(25);

        removed = file_remove_1(_fixture_file_name);
    }

    test_expect_true(test, "the fixture file is removed", removed == RESULT_SUCCESS);
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

/** @brief Remove a fixture the server thread may still be tearing down (Windows refuses to delete an open file). */
static bool _fixture_remove(char const *const name) {
    Result removed = file_remove_1(name);

    for (USize attempt = 0; attempt < _FIXTURE_REMOVE_ATTEMPTS && removed != RESULT_SUCCESS; attempt += 1) {
        thread_sleep(_FIXTURE_REMOVE_SLEEP_MS);

        removed = file_remove_1(name);
    }

    return removed == RESULT_SUCCESS;
}

static void _test_send_file_handle(Test *const test) {
    test_case_begin(test, "send_file_handle: 200 whole file, 206 window, empty window, HEAD keep-alive, the three value refusals");

    HTTP_Server *server = http_server_new();

    http_server_route_add(server, "/stream", _route_stream);
    http_server_route_add(server, "/stream-after-send", _route_stream_after_send);
    http_server_route_add(server, "/stream-irregular", _route_stream_irregular);
    http_server_route_add(server, "/exact", _route_exact);

    U16 const port = _start(test, server);

    _stream_fixture_names_set(port);

    test_expect_true(test, "the 1 MiB + 1 fixture is written", _fixture_write(_stream_fixture_name, _STREAM_FIXTURE_SIZE));

    _stream_served  = _stream_fixture_name;
    _stream_start   = 0;
    _stream_size    = _STREAM_FIXTURE_SIZE;
    _stream_status  = HTTP_SERVER_STATUS_CODE_OK;

    _Reply      reply   = DEFAULT_INITIALIZATION;
    Net_Socket  socket  = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "whole-file connection opens", _client_open(port, &socket))) {
        USize   size    = 0;
        bool    exact   = false;
        bool    eof     = false;

        bool const ok = _client_write(socket, _STREAM_REQUEST_CLOSE, CHAR_STATIC_SIZE(_STREAM_REQUEST_CLOSE));

        if (test_expect_true(test, "the whole file is answered", ok && _client_read_window(socket, &reply, 0, &size, &exact, &eof))) {
            char queued[_REPLY_FLAG_MAX] = DEFAULT_INITIALIZATION;

            test_expect_true(test, "the route's seam call answered true", _stream_ok);
            test_expect_u(test, "200", 200, reply.status);
            test_expect_u(test, "exactly one Content-Length", 1, _reply_header_count(&reply, "content-length:"));
            test_expect_u(test, "Content-Length is the file size", _STREAM_FIXTURE_SIZE, _reply_content_length(&reply));
            test_expect_u(test, "every byte arrived", _STREAM_FIXTURE_SIZE, size);
            test_expect_true(test, "the body is byte-exact across 16 chunk boundaries and a partial last chunk", exact);
            test_expect_false(test, "the connection was not cut short", eof);
            test_expect_true(test, "the queued header is emitted ahead of the body", _reply_header(&reply, "x-queued:", queued, sizeof(queued)));
        }

        net_socket_close(socket);
    }

    _stream_start   = _STREAM_WINDOW_START;
    _stream_size    = _STREAM_WINDOW_SIZE;
    _stream_status  = HTTP_SERVER_STATUS_CODE_PARTIAL_CONTENT;

    if (test_expect_true(test, "206 window round trip", _client_round_trip(port, _STREAM_REQUEST_CLOSE, &reply))) {
        bool exact = true;

        for (USize index = 0; index < reply.body_size && index < _STREAM_WINDOW_SIZE; index += 1) {
            exact = exact && (Byte) reply.body[index] == _pattern_byte(_STREAM_WINDOW_START + index);
        }

        test_expect_u(test, "206", 206, reply.status);
        test_expect_u(test, "Content-Length is the window", _STREAM_WINDOW_SIZE, _reply_content_length(&reply));
        test_expect_u(test, "the window's bytes arrived", _STREAM_WINDOW_SIZE, reply.body_size);
        test_expect_true(test, "the window straddling the chunk boundary is exact", exact);
    }

    _stream_start   = 0;
    _stream_size    = 0;
    _stream_status  = HTTP_SERVER_STATUS_CODE_OK;

    if (test_expect_true(test, "empty window round trip", _client_round_trip(port, _STREAM_REQUEST_CLOSE, &reply))) {
        char length[_REPLY_FLAG_MAX] = DEFAULT_INITIALIZATION;

        test_expect_true(test, "an empty window is accepted", _stream_ok);
        test_expect_u(test, "an empty window is 200", 200, reply.status);
        test_expect_true(test, "it declares a Content-Length", _reply_header(&reply, "content-length:", length, sizeof(length)));
        test_expect_string(test, "of zero", "0", length);
        test_expect_u(test, "and carries no body", 0, reply.body_size);
    }

    /* HEAD: the headers a GET would produce, no body, and the keep-alive socket still serves
     * the NEXT request - the regression class is "HEAD answered twice". */
    _stream_size = _STREAM_FIXTURE_SIZE;

    Net_Socket keep_alive = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "HEAD keep-alive connection opens", _client_open(port, &keep_alive))) {
        bool ok = _client_write(keep_alive, "HEAD /stream HTTP/1.1\r\nHost: t\r\n\r\n", CHAR_STATIC_SIZE("HEAD /stream HTTP/1.1\r\nHost: t\r\n\r\n"));

        if (test_expect_true(test, "HEAD on the seam answers", ok && _client_read_headers(keep_alive, &reply))) {
            test_expect_true(test, "the HEAD seam call answered true", _stream_ok);
            test_expect_u(test, "HEAD is 200", 200, reply.status);
            test_expect_u(test, "HEAD declares the window's Content-Length", _STREAM_FIXTURE_SIZE, _reply_content_length(&reply));
            test_expect_u(test, "nothing follows the HEAD reply on the wire", 0, reply.body_size);
        }

        ok = _client_write(keep_alive, "GET /exact HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", CHAR_STATIC_SIZE("GET /exact HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n"));

        if (test_expect_true(test, "the SAME socket still serves the next request", ok && _client_read(keep_alive, &reply))) {
            test_expect_string(test, "and that request gets its own reply", "exact", reply.body);
        }

        net_socket_close(keep_alive);
    }

    /* Refusal 1: the headers are already on the wire. Nothing more follows the first reply. */
    _stream_ok = true;

    Net_Socket after_send = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "headers-sent connection opens", _client_open(port, &after_send))) {
        bool const ok = _client_write(after_send, "GET /stream-after-send HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n",
                                      CHAR_STATIC_SIZE("GET /stream-after-send HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n"));

        if (test_expect_true(test, "the first send is answered", ok && _client_read(after_send, &reply))) {
            test_expect_string(test, "with its own body", "first", reply.body);
        }

        _Reply second = DEFAULT_INITIALIZATION;

        /* The first reply is on the wire BEFORE the route reaches the seam, so the refusal
         * is read only once the connection has closed - which it does when the route has
         * returned and the transaction completed. */
        test_expect_false(test, "and nothing more is written onto the socket", _client_read_headers(after_send, &second));
        test_expect_false(test, "the seam refused because the headers were sent", _stream_ok);

        net_socket_close(after_send);
    }

    /* Refusal 2: a window past EOF - refused BY VALUE, and the route's 416 goes out. */
    _stream_ok      = true;
    _stream_start   = _STREAM_FIXTURE_SIZE - _STREAM_PAST_EOF_BACK;
    _stream_size    = _STREAM_WINDOW_SIZE;
    _stream_status  = HTTP_SERVER_STATUS_CODE_PARTIAL_CONTENT;

    if (test_expect_true(test, "past-EOF round trip", _client_round_trip(port, _STREAM_REQUEST_CLOSE, &reply))) {
        test_expect_false(test, "a window past EOF is refused", _stream_ok);
        test_expect_u(test, "the route's own 416 goes out", 416, reply.status);
        test_expect_string(test, "with the route's own body", "fallback", reply.body);
    }

    /* Refusal 3: not a regular file. */
    _stream_ok = true;

    if (test_expect_true(test, "irregular-handle round trip", _client_round_trip(port, "GET /stream-irregular HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_false(test, "a handle that is not a regular file is refused", _stream_ok);
        test_expect_u(test, "the route's own 404 goes out", 404, reply.status);
        test_expect_string(test, "with the route's own body", "fallback", reply.body);
    }

    http_server_delete(&server);

    test_expect_true(test, "the stream fixture is removed", _fixture_remove(_stream_fixture_name));

    test_case_end(test);
}

/*
 * The CR/LF chokepoint: every path a caller's bytes take into the header block - a sender's
 * content type, a header_add_raw block, send_file's extra_headers - is refused BY VALUE when it
 * could split the response, and the accepted control on each API proves the route and server
 * were live, so a refusal pin cannot pass because nothing ran at all.
 */
static void _test_header_chokepoint(Test *const test) {
    test_case_begin(test, "CR/LF chokepoint: content types and raw header blocks that could split a response are refused by value");

    HTTP_Server *server = http_server_new();

    http_server_route_add(server, "/type-send-1", _route_type_send_1);
    http_server_route_add(server, "/type-send-2", _route_type_send_2);
    http_server_route_add(server, "/type-stream", _route_type_stream);
    http_server_route_add(server, "/raw-blocks", _route_raw_blocks);
    http_server_route_add(server, "/file-blocks", _route_file_blocks);

    U16 const port = _start(test, server);

    _stream_fixture_names_set(port);

    test_expect_true(test, "the chokepoint fixture is written", _fixture_write(_stream_fixture_name, _CHOKEPOINT_FIXTURE_SIZE));

    _stream_served  = _stream_fixture_name;
    _stream_start   = 0;
    _stream_size    = _CHOKEPOINT_FIXTURE_SIZE;

    _Reply  reply                       = DEFAULT_INITIALIZATION;
    char    name[_CHOKEPOINT_NAME_MAX]  = DEFAULT_INITIALIZATION;
    char    value[_REPLY_VALUE_MAX]     = DEFAULT_INITIALIZATION;

    /* send_2: a refused type fails the response, so the connection closes with NO reply - not a
     * reply short its Content-Type, and never one carrying the injected header. */
    for (USize index = 0; index < sizeof(_CONTENT_TYPES_REFUSED) / sizeof(_CONTENT_TYPES_REFUSED[0]); index += 1) {
        _content_type_value = _CONTENT_TYPES_REFUSED[index];

        snprintf(name, sizeof(name), "send_2 refuses content type %llu (CR, LF or a control byte): no reply at all", (unsigned long long) index);

        test_expect_false(test, name, _client_round_trip(port, "GET /type-send-2 HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply));
    }

    _content_type_value = _CONTENT_TYPE_ACCEPTED;

    if (test_expect_true(test, "send_2 accepts a content type with an HTAB in it", _client_round_trip(port, "GET /type-send-2 HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "the accepted send_2 is a 200", 200, reply.status);
        test_expect_true(test, "it carries the Content-Type", _reply_header(&reply, "content-type:", value, sizeof(value)));
        test_expect_string(test, "byte for byte, the HTAB included", _CONTENT_TYPE_ACCEPTED, value);
        test_expect_string(test, "and its body", "typed", reply.body);
    }

    /* send_1 forwards to send_2: one refused type and the accepted one pin the forwarding. */
    _content_type_value = _CONTENT_TYPES_REFUSED[0];

    test_expect_false(test, "send_1 refuses a CRLF content type: no reply at all", _client_round_trip(port, "GET /type-send-1 HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply));

    _content_type_value = _CONTENT_TYPE_ACCEPTED;

    if (test_expect_true(test, "send_1 accepts the HTAB content type", _client_round_trip(port, "GET /type-send-1 HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "the accepted send_1 is a 200", 200, reply.status);
        test_expect_string(test, "with its body", "typed", reply.body);
    }

    /* send_file_handle refuses a type in its own refusal block, like its window: false, nothing
     * written, the handle still the route's - and the response NOT failed, so the route's fallback
     * send_2 (a 500) is what reaches the peer, never a silently closed connection. */
    for (USize index = 0; index < sizeof(_CONTENT_TYPES_REFUSED) / sizeof(_CONTENT_TYPES_REFUSED[0]); index += 1) {
        _content_type_value         = _CONTENT_TYPES_REFUSED[index];
        _stream_ok                  = true;
        _type_stream_handle_kept    = false;

        snprintf(name, sizeof(name), "send_file_handle refuses content type %llu, and the fallback send_2 reaches the peer", (unsigned long long) index);

        if (test_expect_true(test, name, _client_round_trip(port, "GET /type-stream HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
            test_expect_u(test, "the fallback's 500 status line is what arrived", 500, reply.status);
            test_expect_string(test, "with the fallback's body", "fallback", reply.body);
            test_expect_u(test, "and no injected header", 0, _reply_header_count(&reply, "x-injected"));
        }

        test_expect_false(test, "send_file_handle answered false", _stream_ok);
        test_expect_true(test, "and left the handle with the route", _type_stream_handle_kept);
    }

    /* The status line alone cannot tell a failed response from a recovered one: a FAILED response
     * still lets the fallback's bytes reach the kernel before the connection is torn down. What a
     * failed response cannot do is complete its transaction, so only a SECOND request on the same
     * keep-alive socket shows that the refusal left the response alone. */
    Net_Socket socket = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "the refused-type keep-alive connection opens", _client_open(port, &socket))) {
        _Reply first    = DEFAULT_INITIALIZATION;
        _Reply second   = DEFAULT_INITIALIZATION;

        _content_type_value = _CONTENT_TYPES_REFUSED[0];

        bool ok = _client_write(socket, "GET /type-stream HTTP/1.1\r\nHost: t\r\n\r\n", CHAR_STATIC_SIZE("GET /type-stream HTTP/1.1\r\nHost: t\r\n\r\n"));

        ok = ok && _client_read(socket, &first);

        if (test_expect_true(test, "the refused type's fallback is answered on the keep-alive socket", ok)) {
            test_expect_u(test, "with the fallback's 500", 500, first.status);
        }

        _content_type_value = _CONTENT_TYPE_ACCEPTED;

        ok = ok && _client_write(socket, "GET /type-send-2 HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n",
                                 CHAR_STATIC_SIZE("GET /type-send-2 HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n"));

        ok = ok && _client_read(socket, &second);

        if (test_expect_true(test, "the connection survived the refused type: the response was not failed", ok)) {
            test_expect_string(test, "the next request is answered on the SAME socket", "typed", second.body);
        }

        net_socket_close(socket);
    }

    _content_type_value = _CONTENT_TYPE_ACCEPTED;

    if (test_expect_true(test, "send_file_handle accepts the HTAB content type", _client_round_trip(port, "GET /type-stream HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_true(test, "the accepted stream answered true", _stream_ok);
        test_expect_u(test, "the accepted stream is a 200", 200, reply.status);
        test_expect_u(test, "and the whole fixture arrived", _CHOKEPOINT_FIXTURE_SIZE, reply.body_size);
    }

    /* header_add_raw: one byte per call, seven malformed blocks, header_add's non-token name, then
     * the well-formed block. A refused block is refused WHOLE: not even its first, well-formed
     * "X-Raw: ok" line reaches the wire. */
    if (test_expect_true(test, "the raw-block round trip is answered", _client_round_trip(port, "GET /raw-blocks HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_string(test, "header_add_raw refuses every malformed shape, header_add a non-token name, and the well-formed block is accepted", "000000001", reply.body);
        test_expect_u(test, "no injected header reaches the wire", 0, _reply_header_count(&reply, "x-injected"));
        test_expect_u(test, "no line of a refused block reaches it either", 0, _reply_header_count(&reply, "x-raw:"));
        test_expect_true(test, "the accepted block's first line is on the wire", _reply_header(&reply, "x-raw-good:", value, sizeof(value)));
        test_expect_string(test, "with its HTAB", "a\tb", value);
        test_expect_true(test, "and its empty-valued line too", _reply_header(&reply, "x-raw-empty:", value, sizeof(value)));
    }

    /* send_file: seven malformed blocks, five refused types, then the well-formed call serves. */
    _file_blocks_record[0] = '\0';

    if (test_expect_true(test, "the send_file round trip is answered", _client_round_trip(port, "GET /file-blocks HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_string(test, "send_file refuses every malformed block and every refused type, then serves the well-formed call", "0000000000001", _file_blocks_record);
        test_expect_u(test, "the served file is a 200", 200, reply.status);
        test_expect_u(test, "no injected header reaches the wire", 0, _reply_header_count(&reply, "x-injected"));
        test_expect_u(test, "no line of a refused block reaches it either", 0, _reply_header_count(&reply, "x-raw:"));
        test_expect_true(test, "the accepted block is emitted", _reply_header(&reply, "x-raw-good:", value, sizeof(value)));
        test_expect_true(test, "the accepted type is emitted", _reply_header(&reply, "content-type:", value, sizeof(value)));
        test_expect_string(test, "byte for byte, the HTAB included", _CONTENT_TYPE_ACCEPTED, value);
        test_expect_u(test, "and the whole fixture arrived", _CHOKEPOINT_FIXTURE_SIZE, reply.body_size);
    }

    http_server_delete(&server);

    test_expect_true(test, "the chokepoint fixture is removed", _fixture_remove(_stream_fixture_name));

    test_case_end(test);
}

static void _test_send_file_handle_ownership(Test *const test) {
    test_case_begin(test, "send_file_handle ownership: the handle is released on completion, on a mid-stream disconnect, on HEAD, and on truncation");

    HTTP_Server *server = http_server_new();

    http_server_route_add(server, "/stream", _route_stream);
    http_server_route_add(server, "/exact", _route_exact);

    U16 const port = _start(test, server);

    _stream_fixture_names_set(port);

    test_expect_true(test, "the 1 MiB + 1 fixture is written", _fixture_write(_stream_fixture_name, _STREAM_FIXTURE_SIZE));
    test_expect_true(test, "the 2 MiB fixture is written", _fixture_write(_stream_big_fixture_name, _STREAM_BIG_FIXTURE_SIZE));

    _stream_served  = _stream_fixture_name;
    _stream_start   = 0;
    _stream_size    = _STREAM_FIXTURE_SIZE;
    _stream_status  = HTTP_SERVER_STATUS_CODE_OK;

    _Reply      reply       = DEFAULT_INITIALIZATION;
    USize       size        = 0;
    bool        exact       = false;
    bool        eof         = false;
    Net_Socket  socket      = DEFAULT_INITIALIZATION;

    /* Warm-up: whatever the first streamed request allocates lazily is allocated before the
     * baseline is taken, so the counts below compare the seam's handle and nothing else. */
    if (test_expect_true(test, "warm-up connection opens", _client_open(port, &socket))) {
        bool const ok = _client_write(socket, _STREAM_REQUEST_CLOSE, CHAR_STATIC_SIZE(_STREAM_REQUEST_CLOSE));

        test_expect_true(test, "the warm-up is answered", ok && _client_read_window(socket, &reply, 0, &size, &exact, &eof));

        net_socket_close(socket);
    }

    USize baseline  = 0;
    USize last      = 0;

    if (!test_expect_true(test, "a stable handle-count baseline is read", _handles_baseline(&baseline))) {
        http_server_delete(&server);

        test_expect_true(test, "the stream fixture is removed", _fixture_remove(_stream_fixture_name));
        test_expect_true(test, "the big stream fixture is removed", _fixture_remove(_stream_big_fixture_name));

        test_case_end(test);

        return;
    }

    /* (a) completion: the last chunk's own release. */
    if (test_expect_true(test, "completion connection opens", _client_open(port, &socket))) {
        bool const ok = _client_write(socket, _STREAM_REQUEST_CLOSE, CHAR_STATIC_SIZE(_STREAM_REQUEST_CLOSE));

        if (test_expect_true(test, "the whole file is answered", ok && _client_read_window(socket, &reply, 0, &size, &exact, &eof))) {
            test_expect_u(test, "every byte arrived", _STREAM_FIXTURE_SIZE, size);
            test_expect_true(test, "byte-exact", exact);
        }

        net_socket_close(socket);

        test_expect_true(test, "(a) the handle count is back at the baseline after completion", _handles_settle(baseline, &last));
        test_expect_u(test, "(a) count", baseline, last);
    }

    /* (b) disconnect: the client reads the headers and one chunk of a 2 MiB body, then goes
     * away. The stream is mid-flight; only the session reset on CLOSED_HTTP can release it. */
    _stream_served  = _stream_big_fixture_name;
    _stream_size    = _STREAM_BIG_FIXTURE_SIZE;

    if (test_expect_true(test, "disconnect connection opens", _client_open(port, &socket))) {
        static Byte buffer[_STREAM_READ_MAX] = DEFAULT_INITIALIZATION;

        bool const ok = _client_write(socket, "GET /stream HTTP/1.1\r\nHost: t\r\n\r\n", CHAR_STATIC_SIZE("GET /stream HTTP/1.1\r\nHost: t\r\n\r\n"));

        if (test_expect_true(test, "the 2 MiB stream starts", ok && _client_read_headers(socket, &reply))) {
            USize received = reply.body_size;

            test_expect_u(test, "it is a 200", 200, reply.status);

            while (received < HTTP_SERVER_FILE_CHUNK_SIZE) {
                USize got = 0;

                if (result_is_error(net_socket_recv_1(socket, buffer, sizeof(buffer), &got)) || got == 0) {
                    break;
                }

                received += got;
            }

            test_expect_true(test, "the client read at least one chunk before leaving", received >= HTTP_SERVER_FILE_CHUNK_SIZE);
        }

        net_socket_close(socket);

        test_expect_true(test, "(b) the handle count is back at the baseline after a mid-stream disconnect", _handles_settle(baseline, &last));
        test_expect_u(test, "(b) count", baseline, last);
    }

    if (test_expect_true(test, "a NEW connection is served after the disconnect", _client_round_trip(port, "GET /exact HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_string(test, "and answered", "exact", reply.body);
    }

    /* (c) HEAD: closed synchronously inside the call. */
    _stream_served  = _stream_fixture_name;
    _stream_size    = _STREAM_FIXTURE_SIZE;

    if (test_expect_true(test, "HEAD connection opens", _client_open(port, &socket))) {
        bool const ok = _client_write(socket, "HEAD /stream HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n",
                                      CHAR_STATIC_SIZE("HEAD /stream HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n"));

        if (test_expect_true(test, "HEAD is answered", ok && _client_read_headers(socket, &reply))) {
            test_expect_u(test, "HEAD is 200", 200, reply.status);
            test_expect_u(test, "HEAD carries no body", 0, reply.body_size);
        }

        net_socket_close(socket);

        test_expect_true(test, "(c) the handle count is back at the baseline after HEAD", _handles_settle(baseline, &last));
        test_expect_u(test, "(c) count", baseline, last);
    }

    /* Truncation: the 2 MiB fixture is cut to 1 KiB once the headers are on the wire. The
     * server cannot honour its Content-Length, closes, and survives. */
    _stream_served  = _stream_big_fixture_name;
    _stream_size    = _STREAM_BIG_FIXTURE_SIZE;

    if (test_expect_true(test, "truncation connection opens", _client_open(port, &socket))) {
        bool const ok = _client_write(socket, _STREAM_REQUEST_CLOSE, CHAR_STATIC_SIZE(_STREAM_REQUEST_CLOSE));

        if (test_expect_true(test, "the stream's headers arrive", ok && _client_read_headers(socket, &reply))) {
            static Byte buffer[_STREAM_READ_MAX] = DEFAULT_INITIALIZATION;

            test_expect_u(test, "Content-Length promises 2 MiB", _STREAM_BIG_FIXTURE_SIZE, _reply_content_length(&reply));

            File *cut = file_open_try_1(_stream_big_fixture_name, "wb");

            if (test_expect_true(test, "the fixture can be truncated under the server's handle", !memory_empty(cut))) {
                static Byte const stub[_STREAM_TRUNCATE_TO] = DEFAULT_INITIALIZATION;

                file_write_1(cut, stub, 1, sizeof(stub));
                file_close(&cut);
            }

            ChronoInstant   const   start       = chrono_now();
            USize                   received    = reply.body_size;
            bool                    closed      = false;

            while (received < _STREAM_BIG_FIXTURE_SIZE) {
                USize           got     = 0;
                Result  const   result  = net_socket_recv_1(socket, buffer, sizeof(buffer), &got);

                /* An orderly EOF or a reset both mean the server closed; a 4 s timeout does not. */
                if (result_is_error(result) || got == 0) {
                    closed = chrono_duration_milliseconds(chrono_elapsed(start)) < _STREAM_CLOSE_MAX_MS;

                    break;
                }

                received += got;
            }

            test_expect_true(test, "the client sees the connection close before Content-Length", received < _STREAM_BIG_FIXTURE_SIZE);
            test_expect_true(test, "closed by the server, not by the client's timeout", closed);
        }

        net_socket_close(socket);

        test_expect_true(test, "the truncation released the handle", _handles_settle(baseline, &last));
        test_expect_u(test, "truncation count", baseline, last);
    }

    if (test_expect_true(test, "the server survives the truncated stream", _client_round_trip(port, "GET /exact HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_string(test, "and still answers", "exact", reply.body);
    }

    http_server_delete(&server);

    test_expect_true(test, "the stream fixture is removed", _fixture_remove(_stream_fixture_name));
    test_expect_true(test, "the big stream fixture is removed", _fixture_remove(_stream_big_fixture_name));

    test_case_end(test);
}

/**
 * @brief Read a streamed reply until the peer closes, timing the close. Answers the body bytes
 *        received (pattern-checked from offset 0) and whether the close came from the server
 *        - inside _STREAM_CLOSE_MAX_MS - rather than from the client's own IO timeout.
 */
static bool _client_read_until_close(Net_Socket const socket, _Reply *const reply, USize *const out_size, bool *const out_exact, bool *const out_closed) {
    ChronoInstant const start = chrono_now();

    bool eof = false;

    if (!_client_read_window(socket, reply, 0, out_size, out_exact, &eof)) {
        return false;
    }

    *out_closed = chrono_duration_milliseconds(chrono_elapsed(start)) < _STREAM_CLOSE_MAX_MS;

    return true;
}

/*
 * The documented LIMIT of send_file_handle (see its header note): bytes a client sends while
 * a body streams - a pipelined request, a stray CRLF - close the connection, because
 * libwebsockets' HTTP/1 reader has no case for them in the transaction state the seam
 * leaves the wsi in. These pins hold the contract to exactly that: the response is cut
 * short and no second reply follows, the handle is released as on any disconnect, and the
 * server goes on serving. A libwebsockets that one day parks those bytes fails the
 * "cut short" pins, which is the signal to lift the note.
 */
static void _test_send_file_handle_client_bytes(Test *const test) {
    test_case_begin(test, "send_file_handle: client bytes mid-stream (pipelined GET, stray CRLF) close the connection, release the handle, leave the server up");

    HTTP_Server *server = http_server_new();

    http_server_route_add(server, "/stream", _route_stream);
    http_server_route_add(server, "/exact", _route_exact);

    U16 const port = _start(test, server);

    _stream_fixture_names_set(port);

    test_expect_true(test, "the 2 MiB fixture is written", _fixture_write(_stream_big_fixture_name, _STREAM_BIG_FIXTURE_SIZE));

    _stream_served  = _stream_big_fixture_name;
    _stream_start   = 0;
    _stream_size    = _STREAM_BIG_FIXTURE_SIZE;
    _stream_status  = HTTP_SERVER_STATUS_CODE_OK;

    _Reply      reply       = DEFAULT_INITIALIZATION;
    USize       size        = 0;
    bool        exact       = false;
    bool        closed      = false;
    Net_Socket  socket      = DEFAULT_INITIALIZATION;

    /* Warm-up, as in the ownership case: lazy allocations happen before the baseline. */
    if (test_expect_true(test, "warm-up connection opens", _client_open(port, &socket))) {
        bool const ok = _client_write(socket, _STREAM_REQUEST_CLOSE, CHAR_STATIC_SIZE(_STREAM_REQUEST_CLOSE));

        test_expect_true(test, "the warm-up is answered", ok && _client_read_until_close(socket, &reply, &size, &exact, &closed));

        net_socket_close(socket);
    }

    USize baseline  = 0;
    USize last      = 0;

    if (!test_expect_true(test, "a stable handle-count baseline is read", _handles_baseline(&baseline))) {
        http_server_delete(&server);

        test_expect_true(test, "the big stream fixture is removed", _fixture_remove(_stream_big_fixture_name));

        test_case_end(test);

        return;
    }

    /* Pipelined: the second request rides in the same write as the first. */
    if (test_expect_true(test, "pipelined connection opens", _client_open(port, &socket))) {
        bool const ok = _client_write(socket, "GET /stream HTTP/1.1\r\nHost: t\r\n\r\nGET /exact HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n",
                                      CHAR_STATIC_SIZE("GET /stream HTTP/1.1\r\nHost: t\r\n\r\nGET /exact HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n"));

        if (test_expect_true(test, "the first response starts", ok && _client_read_until_close(socket, &reply, &size, &exact, &closed))) {
            test_expect_u(test, "the first response is a 200", 200, reply.status);
            test_expect_u(test, "with the whole file promised", _STREAM_BIG_FIXTURE_SIZE, _reply_content_length(&reply));
            test_expect_true(test, "the pipelined request cuts the first response short (the documented limit)", size < _STREAM_BIG_FIXTURE_SIZE);
            test_expect_true(test, "what did arrive is byte-exact", exact);
            test_expect_true(test, "the server closed the connection, the client did not time out", closed);
        }

        net_socket_close(socket);

        test_expect_true(test, "the handle count is back at the baseline after the pipelined close", _handles_settle(baseline, &last));
        test_expect_u(test, "pipelined count", baseline, last);
    }

    if (test_expect_true(test, "a NEW connection is served after the pipelined close", _client_round_trip(port, "GET /exact HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_string(test, "and answered", "exact", reply.body);
    }

    /* A stray CRLF once the headers have arrived - mid-stream, not in the request. */
    if (test_expect_true(test, "stray-CRLF connection opens", _client_open(port, &socket))) {
        bool ok = _client_write(socket, _STREAM_REQUEST_CLOSE, CHAR_STATIC_SIZE(_STREAM_REQUEST_CLOSE));

        ok = ok && _client_read_headers(socket, &reply);

        if (test_expect_true(test, "the stream's headers arrive", ok)) {
            test_expect_u(test, "it is a 200", 200, reply.status);
        }

        ok = ok && _client_write(socket, "\r\n", CHAR_STATIC_SIZE("\r\n"));

        if (test_expect_true(test, "the CRLF is sent mid-stream", ok)) {
            ChronoInstant const start = chrono_now();

            static Byte buffer[_STREAM_READ_MAX] = DEFAULT_INITIALIZATION;

            USize received = reply.body_size;

            while (received < _STREAM_BIG_FIXTURE_SIZE) {
                USize got = 0;

                if (result_is_error(net_socket_recv_1(socket, buffer, sizeof(buffer), &got)) || got == 0) {
                    break;
                }

                received += got;
            }

            test_expect_true(test, "the stray CRLF cuts the response short (the documented limit)", received < _STREAM_BIG_FIXTURE_SIZE);
            test_expect_true(test, "the server closed the connection, the client did not time out", chrono_duration_milliseconds(chrono_elapsed(start)) < _STREAM_CLOSE_MAX_MS);
        }

        net_socket_close(socket);

        test_expect_true(test, "the handle count is back at the baseline after the CRLF close", _handles_settle(baseline, &last));
        test_expect_u(test, "CRLF count", baseline, last);
    }

    if (test_expect_true(test, "a NEW connection is served after the CRLF close", _client_round_trip(port, "GET /exact HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_string(test, "and answered", "exact", reply.body);
    }

    http_server_delete(&server);

    test_expect_true(test, "the big stream fixture is removed", _fixture_remove(_stream_big_fixture_name));

    test_case_end(test);
}

/*
 * The two transaction shapes the payload/stream split opened. A POST route is dispatched from
 * BODY_COMPLETION, whose reset must release the payload ONLY: the full session reset it used
 * to run closed a send_file_handle handle before its first chunk, and the connection hung on a
 * Content-Length it could never deliver. And a streamed body on a keep-alive connection ends
 * with the transaction completed, so the SAME socket answers its next request - sent only once
 * the whole body is read, because a request sent earlier is the documented LIMIT.
 */
static void _test_send_file_handle_transactions(Test *const test) {
    test_case_begin(test, "send_file_handle: a POST route's hand-over streams in full; a keep-alive connection answers its next request after a streamed body");

    HTTP_Server *server = http_server_new();

    http_server_route_add(server, "/stream", _route_stream);
    http_server_route_add(server, "/exact", _route_exact);

    U16 const port = _start(test, server);

    _stream_fixture_names_set(port);

    test_expect_true(test, "the 1 MiB + 1 fixture is written", _fixture_write(_stream_fixture_name, _STREAM_FIXTURE_SIZE));

    _stream_served  = _stream_fixture_name;
    _stream_start   = 0;
    _stream_size    = _STREAM_FIXTURE_SIZE;
    _stream_status  = HTTP_SERVER_STATUS_CODE_OK;

    _Reply      reply   = DEFAULT_INITIALIZATION;
    USize       size    = 0;
    bool        exact   = false;
    bool        eof     = false;
    Net_Socket  socket  = DEFAULT_INITIALIZATION;

    /* POST: a declared body, so the route runs from BODY_COMPLETION, not from the header callback. */
    _stream_ok = false;

    if (test_expect_true(test, "POST connection opens", _client_open(port, &socket))) {
        bool const success = _client_write(socket, "POST /stream HTTP/1.1\r\nHost: t\r\nContent-Length: 4\r\nConnection: close\r\n\r\nbody",
                                           CHAR_STATIC_SIZE("POST /stream HTTP/1.1\r\nHost: t\r\nContent-Length: 4\r\nConnection: close\r\n\r\nbody"));

        if (test_expect_true(test, "the POST is answered", success && _client_read_window(socket, &reply, 0, &size, &exact, &eof))) {
            test_expect_true(test, "the POST route's seam call answered true", _stream_ok);
            test_expect_u(test, "the POST's reply is a 200", 200, reply.status);
            test_expect_u(test, "Content-Length is the file size", _STREAM_FIXTURE_SIZE, _reply_content_length(&reply));
            test_expect_u(test, "every byte of the POST's streamed body arrived", _STREAM_FIXTURE_SIZE, size);
            test_expect_true(test, "the POST's body is byte-exact", exact);
            test_expect_false(test, "the POST's connection was not cut short", eof);
        }

        net_socket_close(socket);
    }

    /* Keep-alive: no Connection: close on the streamed GET. */
    if (test_expect_true(test, "keep-alive connection opens", _client_open(port, &socket))) {
        bool success = _client_write(socket, "GET /stream HTTP/1.1\r\nHost: t\r\n\r\n", CHAR_STATIC_SIZE("GET /stream HTTP/1.1\r\nHost: t\r\n\r\n"));

        success = success && _client_read_window(socket, &reply, 0, &size, &exact, &eof);

        if (test_expect_true(test, "the keep-alive GET is answered", success)) {
            test_expect_u(test, "the streamed body arrived whole", _STREAM_FIXTURE_SIZE, size);
            test_expect_true(test, "the streamed body is byte-exact", exact);
        }

        success = success && _client_write(socket, "GET /exact HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n",
                                           CHAR_STATIC_SIZE("GET /exact HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n"));

        success = success && _client_read(socket, &reply);

        if (test_expect_true(test, "the SAME socket answers a second request after the streamed body", success)) {
            test_expect_u(test, "the second reply is a 200", 200, reply.status);
            test_expect_string(test, "with its own body", "exact", reply.body);
        }

        net_socket_close(socket);
    }

    http_server_delete(&server);

    test_expect_true(test, "the stream fixture is removed", _fixture_remove(_stream_fixture_name));

    test_case_end(test);
}

/*
 * The minimum read rate. libwebsockets re-arms no timeout while it drains a chunk's unsent
 * tail and does not ask for the next chunk until that tail is gone, so the content timeout
 * armed when a chunk is written must cover draining ALL of it: a fixed 20 s dropped every
 * reader slower than ~3.2 KiB/s. This reader holds a 4 KiB receive window, reads NOTHING for
 * 23 s, past the old window, then drains the rest: the stalled chunk reaches it at ~2.8 KiB/s,
 * under the old floor and over the 1 KiB/s one. A full stall rather than a trickle on purpose:
 * under a trickle a partly-buffered chunk can drain and re-arm the timer once before the drop,
 * which puts the old code's drop anywhere from 20 to 40 s; a stall leaves the last write - and
 * so the armed timer - at the start, so the old code is dropped at 20 s every time.
 *
 * The pin is only meaningful while the stream is IN FLIGHT: a kernel that took the whole body
 * into its send buffer would let the server finish in under a second, no content timer would
 * run during the stall, and the old code would pass as well. So the body is 32 MiB, and after
 * the stall - before a byte is read - the server must still hold the accepted socket and the
 * stream's FILE*. That check fails on the old code too (it dropped both at 20 s), and on an
 * absorbing kernel it fails loudly instead of the pin passing for the wrong reason.
 */
static void _test_send_file_handle_slow_reader(Test *const test) {
    test_case_begin(test, "send_file_handle: a reader under the old 3.2 KiB/s floor but over the minimum rate receives the whole body");

    HTTP_Server *server = http_server_new();

    http_server_route_add(server, "/stream", _route_stream);

    U16 const port = _start(test, server);

    _stream_fixture_names_set(port);

    test_expect_true(test, "the 32 MiB fixture is written", _fixture_write(_stream_big_fixture_name, _STREAM_SLOW_FIXTURE_SIZE));

    _stream_served  = _stream_big_fixture_name;
    _stream_start   = 0;
    _stream_size    = _STREAM_WINDOW_SIZE;
    _stream_status  = HTTP_SERVER_STATUS_CODE_OK;

    _Reply      reply   = DEFAULT_INITIALIZATION;
    USize       size    = 0;
    bool        exact   = false;
    bool        eof     = false;
    Net_Socket  socket  = DEFAULT_INITIALIZATION;

    /* Warm-up: whatever the first streamed request allocates lazily is allocated before the
     * baseline is taken, so the held count below is the slow stream's and nothing else. */
    test_expect_true(test, "the warm-up is answered", _client_round_trip(port, _STREAM_REQUEST_CLOSE, &reply));

    USize baseline = 0;

    if (!test_expect_true(test, "a stable handle-count baseline is read", _handles_baseline(&baseline))) {
        http_server_delete(&server);

        test_expect_true(test, "the big stream fixture is removed", _fixture_remove(_stream_big_fixture_name));

        test_case_end(test);

        return;
    }

    _stream_size = _STREAM_SLOW_FIXTURE_SIZE;

    if (test_expect_true(test, "the small-window connection opens", _client_open_receive_buffer(port, _STREAM_SLOW_BUFFER, &socket))) {
        bool const sent = _client_write(socket, _STREAM_REQUEST_CLOSE, CHAR_STATIC_SIZE(_STREAM_REQUEST_CLOSE));

        test_expect_true(test, "the request is sent", sent);

        thread_sleep(_STREAM_SLOW_STALL_MS);

        USize       held    = 0;
        bool const  counted = _handle_count(&held);

        log_message_1(LOG_LEVEL_ERROR,
                      "MEASUREMENT slow reader: handle count %llu at the baseline, %llu after the %llu ms stall (in flight = baseline + %llu)\n",
                      (unsigned long long) baseline, (unsigned long long) held, (unsigned long long) _STREAM_SLOW_STALL_MS,
                      (unsigned long long) _STREAM_SLOW_HELD_HANDLES);

        test_expect_true(test, "after the stall the server still holds the accepted socket and the FILE* (the body is in flight)", counted && held >= baseline + _STREAM_SLOW_HELD_HANDLES);

        if (test_expect_true(test, "the reply is read after the stall", sent && _client_read_window(socket, &reply, 0, &size, &exact, &eof))) {
            test_expect_u(test, "it is a 200", 200, reply.status);
            test_expect_u(test, "every byte arrived, past the old 20 s window", _STREAM_SLOW_FIXTURE_SIZE, size);
            test_expect_true(test, "byte-exact", exact);
            test_expect_false(test, "the server did not drop the slow reader", eof);
        }

        net_socket_close(socket);
    }

    http_server_delete(&server);

    test_expect_true(test, "the big stream fixture is removed", _fixture_remove(_stream_big_fixture_name));

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
    _test_send_file_handle(&test);
    _test_header_chokepoint(&test);
    _test_send_file_handle_ownership(&test);
    _test_send_file_handle_client_bytes(&test);
    _test_send_file_handle_transactions(&test);
    _test_send_file_handle_slow_reader(&test);
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