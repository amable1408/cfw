/*
 * fixture.c - implementation of the loopback HTTP/1.1 server fixture.
 * See fixture.h for the contract. Test-only scaffolding: plain libc + CFW
 * net/thread/char/memory/container-string, no http_client.h dependency (this
 * is the PEER the module under test talks to).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <char/char.h>
#include <memory/memory.h>

#include <fixture.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

#define _FIXTURE_ACCEPT_TIMEOUT_MS           3000
#define _FIXTURE_FIRST_READ_TIMEOUT_MS       3000
#define _FIXTURE_IO_TIMEOUT_MS               3000
#define _FIXTURE_KEEPALIVE_IDLE_TIMEOUT_MS   700
#define _FIXTURE_REQUEST_BUFFER_SIZE         16384
/* Bytes the status line, the fixed headers and a Content-Type line can occupy ahead of a
 * scripted body; a body that would push past _FIXTURE_RESPONSE_SIZE is refused, never cut. */
#define _FIXTURE_RESPONSE_HEADER_ROOM        512
#define _FIXTURE_RESPONSE_SIZE               2048
#define _FIXTURE_STALL_HOLD_MS               4000

/* gzip of "gzip-body-ok\n" (mtime=0, level 9) - precomputed so the fixture
 * needs no compression library of its own. */
static U8 const _FIXTURE_GZIP_BYTES[] = {
    0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x4b, 0xaf, 0xca, 0x2c, 0xd0, 0x4d,
    0xca, 0x4f, 0xa9, 0xd4, 0xcd, 0xcf, 0xe6, 0x02, 0x00, 0x74, 0x4f, 0x7f, 0xe3, 0x0d, 0x00, 0x00, 0x00
};

/*==============================================================================
 * MARK: - Byte-level helpers
 *============================================================================*/

static bool _fixture_send_all(Net_Socket const conn, void const *const buf, USize const size) {
    USize sent_total = 0;

    while (sent_total < size) {
        USize sent = 0;
        Result const r = net_socket_send_1(conn, (U8 const*) buf + sent_total, size - sent_total, &sent);

        if (result_is_error(r) || sent == 0) {
            return false;
        }

        sent_total += sent;
    }

    return true;
}

/* snprintf's return is how many bytes WOULD have been written for an unbounded
 * buffer - on truncation that can exceed what actually sits in the fixed stack
 * buffer, so handing a bare (USize) cast of it straight to a send call could
 * read past that buffer. Every call site below writes a short fixed string
 * that never truncates in practice, but clamp anyway rather than trust it. */
static USize _fixture_written_size(I32 const written, USize const buffer_capacity) {
    if (written <= 0) {
        return 0;
    }

    USize const size = (USize) written;

    return size < buffer_capacity ? size : buffer_capacity - 1;
}

/** @brief Locates a "\r\n"-terminated header value (case-insensitive name) inside a NUL-terminated header blob. */
static bool _fixture_header_find(char const *const headers, char const *const name, char *const out, USize const out_capacity) {
    USize const name_size = char_length(name);
    char const *line = headers;

    while (line != nullptr && line[0] != '\0') {
        char const *const eol = strstr(line, "\r\n");
        USize const line_size = eol != nullptr ? (USize) (eol - line) : char_length(line);

        if (line_size >= name_size) {
            bool matches = true;

            for (USize index = 0; index < name_size; index += 1) {
                char const a = line[index];
                char const b = name[index];
                char const a_lower = (a >= 'A' && a <= 'Z') ? (char) (a - 'A' + 'a') : a;
                char const b_lower = (b >= 'A' && b <= 'Z') ? (char) (b - 'A' + 'a') : b;

                if (a_lower != b_lower) {
                    matches = false;

                    break;
                }
            }

            if (matches) {
                USize value_start = name_size;

                while (value_start < line_size && (line[value_start] == ' ' || line[value_start] == '\t')) {
                    value_start += 1;
                }

                USize const value_size = line_size - value_start;
                USize const copy_size = value_size < out_capacity - 1 ? value_size : out_capacity - 1;

                memory_copy_1((void*) out, (void const*) (line + value_start), copy_size);

                out[copy_size] = '\0';

                return true;
            }
        }

        if (eol == nullptr) {
            break;
        }

        line = eol + 2;
    }

    return false;
}

static USize _fixture_content_length(char const *const headers) {
    char value[32] = DEFAULT_INITIALIZATION;

    if (!_fixture_header_find(headers, "content-length:", value, sizeof(value))) {
        return 0;
    }

    long const parsed = strtol(value, nullptr, 10);

    return parsed > 0 ? (USize) parsed : 0;
}

/*==============================================================================
 * MARK: - Request parsing
 *============================================================================*/

/* Reads and parses ONE request off `conn` into self's request_method/path/headers/body
 * output fields. `first` selects the idle-wait budget: generous for a freshly accepted
 * connection's first request, short for a subsequent one on an already-served connection
 * (if the peer isn't sending another request promptly, it isn't going to). Returns false
 * for an idle/closed/reset/malformed read - the normal "no more requests here" outcome. */
static bool _fixture_parse_request(Net_Socket const conn, bool const first, Fixture_Server *const self) {
    net_socket_set_timeout(conn, (U32) (first ? _FIXTURE_FIRST_READ_TIMEOUT_MS : _FIXTURE_KEEPALIVE_IDLE_TIMEOUT_MS));

    char buffer[_FIXTURE_REQUEST_BUFFER_SIZE] = DEFAULT_INITIALIZATION;
    USize buffer_size = 0;
    char const *terminator = nullptr;

    while (terminator == nullptr) {
        if (buffer_size >= sizeof(buffer) - 1) {
            return false; // request too large for this fixture - not exercised by this suite
        }

        USize received = 0;
        Result const r = net_socket_recv_1(conn, buffer + buffer_size, sizeof(buffer) - 1 - buffer_size, &received);

        if (result_is_error(r) || received == 0) {
            return false;
        }

        /* Not an HTTP request line at all (e.g. a TLS ClientHello's leading 0x16 record byte,
         * from a caller that dialed https:// at this plain-text fixture) - close now rather than
         * idling out to the read timeout, so the peer sees a fast connection reset during its
         * handshake instead of a generic connect timeout. */
        if (buffer_size == 0 && !(buffer[0] >= 'A' && buffer[0] <= 'Z')) {
            return false;
        }

        buffer_size += received;
        buffer[buffer_size] = '\0';
        terminator = strstr(buffer, "\r\n\r\n");

        net_socket_set_timeout(conn, (U32) _FIXTURE_IO_TIMEOUT_MS);
    }

    char const *const line_end = strstr(buffer, "\r\n");

    if (line_end == nullptr) {
        return false;
    }

    char method[16] = DEFAULT_INITIALIZATION;
    char path[256] = DEFAULT_INITIALIZATION;

    if (sscanf(buffer, "%15s %255s", method, path) != 2) {
        return false;
    }

    memory_copy_1((void*) self->request_method, (void const*) method, sizeof(method));
    memory_copy_1((void*) self->request_path, (void const*) path, sizeof(path));

    char const *const headers_start = line_end + 2;
    USize const headers_size = terminator > headers_start ? (USize) (terminator - headers_start) : 0;
    USize const headers_copy_size = headers_size < sizeof(self->request_headers) - 1 ? headers_size : sizeof(self->request_headers) - 1;

    memory_copy_1((void*) self->request_headers, (void const*) headers_start, headers_copy_size);
    self->request_headers[headers_copy_size] = '\0';

    USize const headers_end = (USize) (terminator - buffer) + 4;
    USize const content_length = _fixture_content_length(self->request_headers);

    string_clear(&self->request_body);

    if (content_length > 0) {
        USize const already = buffer_size > headers_end ? buffer_size - headers_end : 0;
        USize const already_used = already < content_length ? already : content_length;

        if (already_used > 0) {
            string_add_last_2(&self->request_body, buffer + headers_end, already_used);
        }

        USize remaining = content_length - already_used;

        while (remaining > 0) {
            char body_chunk[4096] = DEFAULT_INITIALIZATION;
            USize const want = remaining < sizeof(body_chunk) ? remaining : sizeof(body_chunk);
            USize received = 0;
            Result const r = net_socket_recv_1(conn, body_chunk, want, &received);

            if (result_is_error(r) || received == 0) {
                return false; // peer vanished mid-body
            }

            string_add_last_2(&self->request_body, body_chunk, received);
            remaining -= received;
        }
    }

    return true;
}

/*==============================================================================
 * MARK: - Scripted responses
 *============================================================================*/

/* Renders the optional Content-Type header line for the two scriptable responses. A null
 * content_type yields an EMPTY line, so an unscripted FIXTURE_SCRIPT_OK / _STATUS emits the
 * exact bytes it always did - the http/client suite's own totals must not move. Answers
 * false when the value does not fit `line`, so an over-long Content-Type is refused with
 * the body rather than silently cut in half by snprintf. */
static bool _fixture_content_type_line(char const *const content_type, char *const line, USize const line_size) {
    line[0] = '\0';

    if (content_type == nullptr) {
        return true;
    }

    I32 const written = snprintf(line, line_size, "Content-Type: %s\r\n", content_type);

    if (written < 0 || (USize) written >= line_size) {
        line[0] = '\0';

        return false;
    }

    return true;
}

/* The two scriptable responders answer true to CLOSE the connection and false to keep it
 * open for another request. A refusal takes the true path - the fixture has nothing to send,
 * so holding the connection open would only strand the client on a read that never
 * completes - and records itself in self->script_refused for the suite to assert on. */
static bool _fixture_respond_ok(Net_Socket const conn, Fixture_Server *const self) {
    char const *const body = self->response_body != nullptr ? self->response_body : "ok-body-content";
    char content_type_line[256] = DEFAULT_INITIALIZATION;

    // A body the buffer cannot hold is refused outright rather than truncated under a
    // Content-Length that still claims the full size - that would fail at the client with a
    // misleading short-read instead of here, where the script is wrong.
    if (char_length(body) + _FIXTURE_RESPONSE_HEADER_ROOM > _FIXTURE_RESPONSE_SIZE
        || !_fixture_content_type_line(self->response_content_type, content_type_line, sizeof(content_type_line))) {
        self->script_refused += 1;

        return true;
    }

    char response[_FIXTURE_RESPONSE_SIZE] = DEFAULT_INITIALIZATION;
    I32 const written = snprintf(response, sizeof(response),
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: %zu\r\n"
        "%s"
        "X-Dup: first-value\r\n"
        "X-Dup: second-value\r\n"
        "x-MiXeD-case: mixed-value\r\n"
        "X-Empty:\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s", char_length(body), content_type_line, body);

    if (written > 0) {
        _fixture_send_all(conn, response, _fixture_written_size(written, sizeof(response)));
    }

    return true;
}

static bool _fixture_respond_status(Net_Socket const conn, Fixture_Server *const self) {
    USize const       status_code = self->status_code;
    char const *const body        = self->response_body != nullptr ? self->response_body : "status-body";
    char const *const reason      = status_code == 404 ? "Not Found" : status_code == 500 ? "Internal Server Error" : "Status";
    char content_type_line[256] = DEFAULT_INITIALIZATION;

    if (char_length(body) + _FIXTURE_RESPONSE_HEADER_ROOM > _FIXTURE_RESPONSE_SIZE
        || !_fixture_content_type_line(self->response_content_type, content_type_line, sizeof(content_type_line))) {
        self->script_refused += 1;

        return true;
    }

    char response[_FIXTURE_RESPONSE_SIZE] = DEFAULT_INITIALIZATION;
    I32 const written = snprintf(response, sizeof(response),
        "HTTP/1.1 %zu %s\r\nContent-Length: %zu\r\n%sConnection: close\r\n\r\n%s",
        status_code, reason, char_length(body), content_type_line, body);

    if (written > 0) {
        _fixture_send_all(conn, response, _fixture_written_size(written, sizeof(response)));
    }

    return true;
}

/* "/second" -> 200 with only that hop's headers; anything else -> 302 to /second, with a
 * header that must NOT survive into the caller's final response (the first-hop-headers-dropped pin). */
static bool _fixture_respond_redirect(Net_Socket const conn, char const *const path) {
    if (char_compare_iequal_2(path, char_length(path), "/second", char_length("/second"))) {
        char const *const body = "redirected-ok";
        char response[512] = DEFAULT_INITIALIZATION;
        I32 const written = snprintf(response, sizeof(response),
            "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nX-Second-Final: yes\r\nConnection: close\r\n\r\n%s",
            char_length(body), body);

        if (written > 0) {
            _fixture_send_all(conn, response, _fixture_written_size(written, sizeof(response)));
        }

        return true;
    }

    char const *const body = "go-look-elsewhere";
    char response[512] = DEFAULT_INITIALIZATION;
    I32 const written = snprintf(response, sizeof(response),
        "HTTP/1.1 302 Found\r\nContent-Length: %zu\r\nLocation: /second\r\nX-First-Only: dropped-by-redirect\r\n\r\n%s",
        char_length(body), body);

    if (written > 0) {
        _fixture_send_all(conn, response, _fixture_written_size(written, sizeof(response)));
    }

    return false; // stay open - the next request (here or on a fresh connection) is "/second"
}

/* Redirects every request right back to itself - the caller's own MAXREDIRS is what ends this,
 * never the fixture; the accept loop's max_connections is only a safety bound. */
static bool _fixture_respond_redirect_loop(Net_Socket const conn) {
    char const *const response = "HTTP/1.1 302 Found\r\nContent-Length: 0\r\nLocation: /loop\r\n\r\n";

    _fixture_send_all(conn, response, char_length(response));

    return false;
}

/* A non-http(s) redirect target - CURLOPT_REDIR_PROTOCOLS_STR must refuse this before ever
 * attempting to reach 192.0.2.1 (TEST-NET-1, guaranteed unroutable). */
static bool _fixture_respond_redirect_ftp(Net_Socket const conn) {
    char const *const response = "HTTP/1.1 302 Found\r\nContent-Length: 0\r\nLocation: ftp://192.0.2.1/unreachable\r\n\r\n";

    _fixture_send_all(conn, response, char_length(response));

    return true;
}

static bool _fixture_respond_chunked(Net_Socket const conn) {
    char const *const response =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: close\r\n"
        "\r\n"
        "5\r\nchunk\r\n"
        "6\r\ned-bod\r\n"
        "1\r\ny\r\n"
        "0\r\n\r\n";

    _fixture_send_all(conn, response, char_length(response));

    return true;
}

static bool _fixture_respond_gzip(Net_Socket const conn) {
    char header[256] = DEFAULT_INITIALIZATION;
    I32 const written = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nContent-Encoding: gzip\r\nConnection: close\r\n\r\n",
        sizeof(_FIXTURE_GZIP_BYTES));

    if (written > 0 && _fixture_send_all(conn, header, _fixture_written_size(written, sizeof(header)))) {
        _fixture_send_all(conn, _FIXTURE_GZIP_BYTES, sizeof(_FIXTURE_GZIP_BYTES));
    }

    return true;
}

/* Declares Content-Length: `size` and actually writes that many bytes. A test client with a
 * small max_response_size is expected to abort partway (CURLE_WRITE_ERROR) - a send failure
 * here from the client hanging up early is the EXPECTED outcome, not a fixture defect. */
static bool _fixture_respond_oversized(Net_Socket const conn, USize const size) {
    char header[256] = DEFAULT_INITIALIZATION;
    I32 const written = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", size);

    if (written <= 0 || !_fixture_send_all(conn, header, _fixture_written_size(written, sizeof(header)))) {
        return true;
    }

    char chunk[4096] = DEFAULT_INITIALIZATION;

    memset(chunk, 'A', sizeof(chunk));

    USize remaining = size;

    while (remaining > 0) {
        USize const want = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
        USize sent = 0;
        Result const r = net_socket_send_1(conn, chunk, want, &sent);

        if (result_is_error(r) || sent == 0) {
            break;
        }

        remaining -= sent;
    }

    return true;
}

/* Sends `count` response headers before any body, each with a `value_size`-byte value (1 when 0,
 * the historical "v") - well past the client's header budget (line count, byte total, or both
 * depending on the combination the caller picks). A send failure partway through is the expected
 * shape of the client aborting once its own budget trips, not a fixture defect. */
static bool _fixture_respond_header_flood(Net_Socket const conn, USize const count, USize const value_size) {
    char const *const status_line = "HTTP/1.1 200 OK\r\n";

    if (!_fixture_send_all(conn, status_line, char_length(status_line))) {
        return true;
    }

    char        value[4096]        = DEFAULT_INITIALIZATION; // zero-initialized: the tail stays NUL-terminated
    USize const clamped_value_size = value_size == 0 ? 1 : value_size >= sizeof(value) ? sizeof(value) - 1 : value_size;

    memset(value, 'v', clamped_value_size);

    for (USize index = 0; index < count; index += 1) {
        char line[sizeof(value) + 32] = DEFAULT_INITIALIZATION;
        I32 const written = snprintf(line, sizeof(line), "X-Flood-%zu: %s\r\n", index, value);

        if (written <= 0 || !_fixture_send_all(conn, line, _fixture_written_size(written, sizeof(line)))) {
            return true;
        }
    }

    char const *const tail = "Content-Length: 4\r\n\r\nabcd";

    _fixture_send_all(conn, tail, char_length(tail));

    return true;
}

/* Sends nothing at all, holding the connection open until the peer gives up (its own
 * CURLOPT_TIMEOUT_MS/CONNECTTIMEOUT_MS) or this bound elapses first. */
static bool _fixture_respond_stall(Net_Socket const conn) {
    net_socket_set_timeout(conn, _FIXTURE_STALL_HOLD_MS);

    char scratch[16] = DEFAULT_INITIALIZATION;
    USize received = 0;

    net_socket_recv_1(conn, scratch, sizeof(scratch), &received);

    return true;
}

/* Declares `content_length` but writes only `send_size` bytes before the caller closes the
 * socket right after this returns - the peer sees the transfer end short. */
static bool _fixture_respond_partial_close(Net_Socket const conn, USize const content_length, USize const send_size) {
    char header[256] = DEFAULT_INITIALIZATION;
    I32 const written = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", content_length);

    if (written > 0 && _fixture_send_all(conn, header, _fixture_written_size(written, sizeof(header))) && send_size > 0) {
        char *const partial = (char*) malloc(send_size);

        if (partial != nullptr) {
            memset(partial, 'B', send_size);
            _fixture_send_all(conn, partial, send_size);
            free(partial);
        }
    }

    return true;
}

static bool _fixture_respond_keep_alive(Net_Socket const conn, USize const request_number, USize const target_count) {
    char const *const body = "keep-alive-body";
    bool const         done = request_number >= target_count;
    char response[512] = DEFAULT_INITIALIZATION;
    I32 const written = snprintf(response, sizeof(response),
        "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nConnection: %s\r\n\r\n%s",
        char_length(body), done ? "close" : "keep-alive", body);

    if (written > 0) {
        _fixture_send_all(conn, response, _fixture_written_size(written, sizeof(response)));
    }

    return done;
}

/*==============================================================================
 * MARK: - Dispatch / connection / thread
 *============================================================================*/

/* Returns true when the connection should close after this response. */
static bool _fixture_dispatch(Net_Socket const conn, Fixture_Server *const self, char const *const path, USize const request_number) {
    switch (self->script) {
        case FIXTURE_SCRIPT_OK:            return _fixture_respond_ok(conn, self);
        case FIXTURE_SCRIPT_STATUS:        return _fixture_respond_status(conn, self);
        case FIXTURE_SCRIPT_REDIRECT:      return _fixture_respond_redirect(conn, path);
        case FIXTURE_SCRIPT_REDIRECT_LOOP: return _fixture_respond_redirect_loop(conn);
        case FIXTURE_SCRIPT_REDIRECT_FTP:  return _fixture_respond_redirect_ftp(conn);
        case FIXTURE_SCRIPT_CHUNKED:       return _fixture_respond_chunked(conn);
        case FIXTURE_SCRIPT_GZIP:          return _fixture_respond_gzip(conn);
        case FIXTURE_SCRIPT_OVERSIZED:     return _fixture_respond_oversized(conn, self->oversized_body_size);
        case FIXTURE_SCRIPT_HEADER_FLOOD:  return _fixture_respond_header_flood(conn, self->header_flood_count, self->header_flood_value_size);
        case FIXTURE_SCRIPT_STALL:         return _fixture_respond_stall(conn);
        case FIXTURE_SCRIPT_PARTIAL_CLOSE: return _fixture_respond_partial_close(conn, self->partial_close_content_length, self->partial_close_send_size);
        case FIXTURE_SCRIPT_KEEP_ALIVE:    return _fixture_respond_keep_alive(conn, request_number, self->keep_alive_request_count);
    }

    return true;
}

static void _fixture_serve_connection(Net_Socket const conn, Fixture_Server *const self) {
    bool  keep_open      = true;
    USize request_number = 0;

    while (keep_open) {
        if (!_fixture_parse_request(conn, request_number == 0, self)) {
            break;
        }

        request_number += 1;
        self->request_count += 1;

        keep_open = !_fixture_dispatch(conn, self, self->request_path, request_number);
    }
}

static void* _fixture_thread_main(void *const data) {
    Fixture_Server *const self            = (Fixture_Server*) data;
    USize const           max_connections = self->max_connections > 0 ? self->max_connections : 1;

    while (self->connection_count < max_connections) {
        Net_Wait ready = 0;

        if (result_is_error(net_socket_wait(self->listen_socket, NET_WAIT_READ, _FIXTURE_ACCEPT_TIMEOUT_MS, &ready)) || ready == 0) {
            break;
        }

        Net_Socket conn = NET_SOCKET_INVALID;

        if (result_is_error(net_socket_accept(self->listen_socket, &conn, nullptr))) {
            break;
        }

        /* fixture_server_join connects once to its own listen port to wake an
         * accept blocked here before it closes the listen socket; that
         * wake-up connection carries no request and must be dropped rather
         * than served. */
        if (atomic_load(&self->stop)) {
            net_socket_close(conn);

            break;
        }

        self->connection_count += 1;

        _fixture_serve_connection(conn, self);

        net_socket_close(conn);
    }

    return nullptr;
}

/*==============================================================================
 * MARK: - Public API
 *============================================================================*/

void fixture_server_join(Fixture_Server *const self) {
    /* Wake the accept loop instead of closing the listen socket out from
     * under it: a thread blocked in net_socket_wait/net_socket_accept on a
     * socket this thread closes is undefined on Winsock, and on the
     * two-server tests the closed SOCKET value can be recycled by the OTHER
     * fixture's accept before the fixture thread's loop ever observes the
     * close - a flake source. Set the stop flag, connect once to our own
     * listen port so a blocked accept returns immediately (the thread
     * recognizes and drops that connection above), join, and only then close
     * the listen socket. */
    atomic_store(&self->stop, true);

    Net_Socket           wake   = NET_SOCKET_INVALID;
    Net_Socket_Address   target = DEFAULT_INITIALIZATION;

    if (!result_is_error(net_socket_init(&wake, NET_FAMILY_IPV4, NET_TYPE_TCP))
        && !result_is_error(net_socket_address_init_2(NET_FAMILY_IPV4, self->port, "127.0.0.1", &target))) {
        net_socket_connect(wake, &target);
    }

    thread_join_1(&self->thread);

    net_socket_close(wake);
    net_socket_close(self->listen_socket);
}

U16 fixture_server_port(Fixture_Server const *const self) {
    return self->port;
}

bool fixture_server_start(Fixture_Server *const self) {
    if (self == nullptr) {
        return false;
    }

    self->request_body = string_init_1();

    if (result_is_error(net_socket_init(&self->listen_socket, NET_FAMILY_IPV4, NET_TYPE_TCP))) {
        string_uninit(&self->request_body);

        return false;
    }

    net_socket_set_reuse_address(self->listen_socket, true);

    Net_Socket_Address address = net_socket_address_init_1(NET_FAMILY_IPV4, 0);

    if (result_is_error(net_socket_bind(self->listen_socket, &address))
        || result_is_error(net_socket_listen(self->listen_socket, 4))
        || result_is_error(net_socket_address_local(self->listen_socket, &address))) {
        net_socket_close(self->listen_socket);
        string_uninit(&self->request_body);

        return false;
    }

    self->port = net_socket_address_port(&address);

    if (result_is_error(thread_create_1(&self->thread, _fixture_thread_main, self))) {
        net_socket_close(self->listen_socket);
        string_uninit(&self->request_body);

        return false;
    }

    return true;
}