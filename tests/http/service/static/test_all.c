/*
 * test_all.c - the http/service/static suite.
 *
 * Part 1 left this suite stub-based: stub_response.c faked out http_server_response_get_client
 * and every lws_* call static.c touched, so every case had to return false BEFORE serve_1
 * reached the "found, about to serve" path - path validation/refusal only, none of conditional
 * GET, Range, SPA fallback, or symlink refusal.
 *
 * This rewrite links the REAL http_server.c and drives it exactly like tests/http/server/
 * test_all.c: http_server_run on port 0, the ephemeral port read back, and a raw `net` socket
 * for the client so every byte on the wire - including conditional/Range request headers and the
 * response headers/body they produce - is under the test's own control. static.c's two entry
 * points (serve_1's legacy wsi-read headers, serve_2's clean http_server_request_header_copy
 * path) share one decision function, so pinning behavior through serve_2 here covers both.
 */
#include <stdio.h>

/* Gated on the compiler-defined _WIN32, not CFW's OS_WINDOWS: OS_WINDOWS arrives with
 * <types.h> through the CFW headers below, so testing it here would evaluate to 0 and
 * silently skip both branches. <platform/windows/windows.h> is where CreateSymbolicLinkA
 * comes from; symlink() is the POSIX equivalent, and Debian - the public CI target - has no
 * privilege gate on it, so the refusal pin runs for real there rather than skipping. */
#ifdef _WIN32
#include <platform/windows/windows.h>
#else
#include <unistd.h>
#endif // _WIN32

#include <dir/dir.h>
#include <http/service/static/static.h>
#include <log/log.h>
#include <net/net.h>
#include <test/test.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/
#define _BIG_FILE_SIZE          (1 * 1024 * 1024 + 100000)    /* > the 1 MiB range cap */
#define _CLIENT_BODY_MAX        (_BIG_FILE_SIZE + 16384)
#define _CLIENT_HEADERS_MAX     8192
#define _CLIENT_IO_TIMEOUT_MS   4000
#define _CLIENT_RAW_MAX         (_BIG_FILE_SIZE + 32768)
#define _FIXTURE_ROOT           "static_test_fixture"
#define _FIXTURE_SPA_ROOT       "static_test_fixture_spa"

/*==============================================================================
 * MARK: - Raw client (modelled on tests/http/server/test_all.c)
 *============================================================================*/
/*
 * body is a pointer into the shared _reply_body static buffer below, not an inline array: this
 * struct is declared as an ordinary stack local all over this file, and an inline MiB-sized array
 * (needed for the range-cap case's body) blew the default thread stack the instant one of these
 * locals came into scope, even in test cases that never touch a large body. Tests run
 * sequentially and single-threaded, so one shared static backing buffer is safe.
 */
typedef struct {
    U16     status;
    char    headers[_CLIENT_HEADERS_MAX];
    USize   headers_size;
    char    *body;
    USize   body_size;
    bool    complete;
} _Reply;

static char _raw[_CLIENT_RAW_MAX];
static char _reply_body[_CLIENT_BODY_MAX];

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
    reply->body = _reply_body;

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

    reply->body_size = body_have < sizeof(_reply_body) ? body_have : sizeof(_reply_body) - 1;

    if (reply->body_size > 0) {
        memory_copy_1((Byte*) reply->body, (Byte*) _raw + header_end, reply->body_size);
    }

    reply->body[reply->body_size] = '\0';
    reply->complete = body_have >= expected;

    return true;
}

/**
 * @brief Count how many times a header name appears in a reply's header block.
 *
 * _reply_header answers with the FIRST match, so it cannot see a duplicated singleton
 * field - and a duplicated Content-Type is exactly what the charset line used to add on
 * top of the one libwebsockets emits from send_file's content_type argument.
 */
static USize _reply_header_count(_Reply const *const reply, char const *const name) {
    static char lowered[_CLIENT_HEADERS_MAX];

    USize const size = reply->headers_size < sizeof(lowered) ? reply->headers_size : sizeof(lowered) - 1;

    memory_copy_1((Byte*) lowered, (Byte*) reply->headers, size);

    lowered[size] = '\0';

    char_lower_2(lowered, size);

    USize const name_size   = char_length(name);
    USize       count       = 0;
    USize       cursor      = 0;

    while (cursor + name_size <= size) {
        char const *const found = char_find_slice_5(lowered, size, cursor, (char*) name, name_size);

        if (found == nullptr) {
            break;
        }

        count  += 1;
        cursor  = (USize) (found - lowered) + name_size;
    }

    return count;
}

/**
 * @brief Read one reply's status line and header block only, never its body.
 *
 * A HEAD reply declares a Content-Length it never sends, so _client_read's body loop would
 * either block for the whole IO timeout or swallow whatever the server writes NEXT as this
 * reply's body. Stopping at the header terminator leaves that on the wire where a test can
 * see it: body_size here is "bytes already buffered past these headers", which must be 0.
 */
static bool _client_read_headers(Net_Socket const socket, _Reply *const reply) {
    USize raw_size      = 0;
    USize header_end    = 0;

    *reply = (_Reply) DEFAULT_INITIALIZATION;
    reply->body = _reply_body;

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

    reply->body_size    = raw_size - header_end;
    reply->body[0]      = '\0';

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
 * MARK: - Fixture
 *============================================================================*/
static Byte _big_file_content[_BIG_FILE_SIZE];

static void _fixture_write(char const *const path, char const *const content) {
    FILE *const file = fopen(path, "wb");

    if (file == nullptr) {
        log_message_2(LOG_LEVEL_ERROR, LOG_METADATA, "_fixture_write: fopen failed for %s", path);

        return;
    }

    fwrite(content, 1, char_length(content), file);
    fclose(file);
}

static void _fixture_write_symlink(void) {
#ifdef _WIN32
    /* CreateSymbolicLinkA needs either an elevated process or Developer Mode's
     * SeCreateSymbolicLinkPrivilege for an unprivileged caller. This machine may have
     * neither - the symlink-refusal pin below skips itself (loudly, not silently) rather
     * than faking a pass when creation fails. */
    CreateSymbolicLinkA(_FIXTURE_ROOT "/symlink_link.txt", "symlink_target.txt", 0);
    /* SYMBOLIC_LINK_FLAG_DIRECTORY - the ANCESTOR case: a link that is not the final path
     * component, which the refusal has to walk every component to see. Without the privilege,
     * fall back to a junction (mklink /J needs none, and resolves the target to an absolute
     * path); dir_list_entries reports a junction as is_link exactly like a symlink. */
    if (!CreateSymbolicLinkA(_FIXTURE_ROOT "/sublink", "sub", 1)) {
        system("mklink /J " _FIXTURE_ROOT "\\sublink " _FIXTURE_ROOT "\\sub >NUL");
    }
#else
    /* No privilege gate on POSIX, so the refusal pins always run there. */
    symlink("symlink_target.txt", _FIXTURE_ROOT "/symlink_link.txt");
    symlink("sub", _FIXTURE_ROOT "/sublink");
#endif // _WIN32
}

static void _fixture_setup(void) {
    dir_create_all_1(_FIXTURE_ROOT);
    dir_create_all_1(_FIXTURE_ROOT "/sub");
    dir_create_all_1(_FIXTURE_SPA_ROOT);

    _fixture_write(_FIXTURE_ROOT "/index.html", "<html>root</html>");
    _fixture_write(_FIXTURE_ROOT "/app.js", "console.log(1);");
    /* An UPPERCASE extension and one the table does not know, for the MIME pins. */
    _fixture_write(_FIXTURE_ROOT "/upper.JS", "console.log(2);");
    _fixture_write(_FIXTURE_ROOT "/blob.zzz", "opaque");
    _fixture_write(_FIXTURE_ROOT "/sub/index.html", "<html>sub</html>");
    _fixture_write(_FIXTURE_ROOT "/range.txt", "ABCDEFGHIJ");
    _fixture_write(_FIXTURE_ROOT "/symlink_target.txt", "target");
    _fixture_write_symlink();

    /* Dotfiles at the root and below a dot-directory, plus the one exempt dot-directory, for the
     * refuse_dotfiles pins. */
    dir_create_all_1(_FIXTURE_ROOT "/sub/.git");
    dir_create_all_1(_FIXTURE_ROOT "/.well-known");
    _fixture_write(_FIXTURE_ROOT "/.env", "SECRET=1");
    _fixture_write(_FIXTURE_ROOT "/sub/.git/config", "[core]");
    _fixture_write(_FIXTURE_ROOT "/.well-known/x", "well-known");

    /* A real long name with '~'+digit that can never be an 8.3 alias (its stem is 11 bytes), so
     * the short-name refusal is pinned not to over-reach onto it. */
    _fixture_write(_FIXTURE_ROOT "/my-report~2.pdf", "report");

#ifdef _WIN32
    /* A real NTFS alternate data stream, so the ':' refusal pin is not satisfied by a missing
     * file: without the refusal, file_exists_1 sees this stream and lws serves it. */
    _fixture_write(_FIXTURE_ROOT "/index.html:Zone.Identifier", "[ZoneTransfer]\r\nZoneId=3\r\n");

    /* A real asset whose stem merely starts like a device name ("CON"), so the device refusal
     * is pinned not to over-reach onto legitimate names. */
    _fixture_write(_FIXTURE_ROOT "/console.txt", "console");

    /* A real long name that IS 8.3-shaped, so the alias refusal pin is not satisfied by a
     * missing file: without the refusal, this file is served. */
    _fixture_write(_FIXTURE_ROOT "/BACKUP~1.TXT", "backup");
#endif // _WIN32

    _fixture_write(_FIXTURE_SPA_ROOT "/index.html", "<html>spa shell</html>");

    for (USize index = 0; index < sizeof(_big_file_content); index += 1) {
        _big_file_content[index] = (Byte) ('a' + (index % 26));
    }

    FILE *const big = fopen(_FIXTURE_ROOT "/big.bin", "wb");

    if (big == nullptr) {
        log_message_2(LOG_LEVEL_ERROR, LOG_METADATA, "_fixture_setup: fopen failed for %s/big.bin", _FIXTURE_ROOT);

        return;
    }

    fwrite(_big_file_content, 1, sizeof(_big_file_content), big);
    fclose(big);
}

static void _fixture_teardown(void) {
    /* dir_remove_all_1 refuses any path containing a "." or ".." segment (dir.h's documented
     * contract); the former "./"-prefixed roots tripped that refusal on every platform, so the
     * fixture trees silently survived every run. Fixed by dropping the leading "./" from the
     * root macros above - the paths are relative either way, since the suite always runs from
     * its own directory. The result is checked here so a future regression logs instead of
     * leaving another silent leftover. */
    if (!dir_remove_all_1(_FIXTURE_ROOT)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "_fixture_teardown: dir_remove_all_1 failed for %s", _FIXTURE_ROOT);
    }

    if (!dir_remove_all_1(_FIXTURE_SPA_ROOT)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "_fixture_teardown: dir_remove_all_1 failed for %s", _FIXTURE_SPA_ROOT);
    }
}

/*==============================================================================
 * MARK: - Server wiring
 *============================================================================*/
static HTTP_Service_Static _svc_default;
static HTTP_Service_Static _svc_spa;
static HTTP_Service_Static _svc_cached;
static HTTP_Service_Static _svc_dotfiles;

/* No router needed - every request in this suite goes through the same two static services, so
 * the per-server handler (set via http_server_set_handler below) does exactly what a real
 * main_*.c's default_callback does for its static routes (see main_traymon.c and friends). */
static void _handler(void *context, HTTP_Server_Request *request, HTTP_Server_Response *response) {
    (void) context;

    if (http_service_static_serve_2(&_svc_default, request, response)) {
        return;
    }

    if (http_service_static_serve_2(&_svc_spa, request, response)) {
        return;
    }

    if (http_service_static_serve_2(&_svc_cached, request, response)) {
        return;
    }

    if (http_service_static_serve_2(&_svc_dotfiles, request, response)) {
        return;
    }

    http_server_response_send_1(response, "not found", HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_NOT_FOUND);
}

/* The module calls this only when no per-server handler is set. Every case here sets one (see
 * _start), so this is never reached; it exists only so the extern resolves at link time. */
void http_server_request_default_callback(void *context, HTTP_Server_Request *request, HTTP_Server_Response *response) {
    (void) context;
    (void) request;

    http_server_response_send_1(response, "default", HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_NOT_FOUND);
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
static void _test_conditional_get(Test *const test, U16 const port) {
    test_case_begin(test, "conditional GET: ETag/Last-Modified, If-None-Match, If-Modified-Since");

    _Reply reply = DEFAULT_INITIALIZATION;
    char   etag[128] = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "plain GET round trip",
                         _client_round_trip(port, "GET /static/range.txt HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        char last_modified[64] = DEFAULT_INITIALIZATION;

        test_expect_u(test, "plain GET status 200", 200, reply.status);
        test_expect_true(test, "an ETag is present", _reply_header(&reply, "etag:", etag, sizeof(etag)));
        test_expect_true(test, "a Last-Modified is present", _reply_header(&reply, "last-modified:", last_modified, sizeof(last_modified)));
    }

    char request[512] = DEFAULT_INITIALIZATION;

    snprintf(request, sizeof(request), "GET /static/range.txt HTTP/1.1\r\nHost: t\r\nIf-None-Match: %s\r\nConnection: close\r\n\r\n", etag);

    if (test_expect_true(test, "If-None-Match round trip", _client_round_trip(port, request, &reply))) {
        test_expect_u(test, "a matching If-None-Match answers 304", 304, reply.status);
        test_expect_u(test, "304 carries no body", 0, reply.body_size);
    }

    /* If-None-Match uses the WEAK comparison (RFC 9110 S13.1.2), unlike If-Range: a W/ prefix is
     * ignored, a list matches on any member, and "*" matches any file that exists. */
    snprintf(request, sizeof(request), "GET /static/range.txt HTTP/1.1\r\nHost: t\r\nIf-None-Match: W/%s\r\nConnection: close\r\n\r\n", etag);

    if (test_expect_true(test, "weak If-None-Match round trip", _client_round_trip(port, request, &reply))) {
        test_expect_u(test, "a W/-prefixed If-None-Match matches weakly -> 304", 304, reply.status);
    }

    snprintf(request, sizeof(request), "GET /static/range.txt HTTP/1.1\r\nHost: t\r\nIf-None-Match: \"other\", %s\r\nConnection: close\r\n\r\n", etag);

    if (test_expect_true(test, "listed If-None-Match round trip", _client_round_trip(port, request, &reply))) {
        test_expect_u(test, "a list whose second member matches -> 304", 304, reply.status);
    }

    if (test_expect_true(test, "If-None-Match * round trip", _client_round_trip(port, "GET /static/range.txt HTTP/1.1\r\nHost: t\r\nIf-None-Match: *\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "If-None-Match: * on an existing file -> 304", 304, reply.status);
    }

    if (test_expect_true(test, "non-matching If-None-Match round trip",
                         _client_round_trip(port, "GET /static/range.txt HTTP/1.1\r\nHost: t\r\nIf-None-Match: \"other\"\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "a non-matching If-None-Match -> 200", 200, reply.status);
    }

    if (test_expect_true(test, "future If-Modified-Since round trip",
                         _client_round_trip(port,
                                            "GET /static/range.txt HTTP/1.1\r\nHost: t\r\nIf-Modified-Since: Fri, 01 Jan 2100 00:00:00 GMT\r\nConnection: close\r\n\r\n",
                                            &reply))) {
        test_expect_u(test, "a future If-Modified-Since answers 304", 304, reply.status);
    }

    if (test_expect_true(test, "past If-Modified-Since round trip",
                         _client_round_trip(port,
                                            "GET /static/range.txt HTTP/1.1\r\nHost: t\r\nIf-Modified-Since: Mon, 01 Jan 2001 00:00:00 GMT\r\nConnection: close\r\n\r\n",
                                            &reply))) {
        test_expect_u(test, "a past If-Modified-Since answers 200", 200, reply.status);
    }

    test_case_end(test);
}

static void _test_range(Test *const test, U16 const port) {
    test_case_begin(test, "Range: prefix/suffix/open, multi-range, garbage, overflow, 416s");

    _Reply reply = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "bytes=0-4 round trip",
                         _client_round_trip(port, "GET /static/range.txt HTTP/1.1\r\nHost: t\r\nRange: bytes=0-4\r\nConnection: close\r\n\r\n", &reply))) {
        char content_range[64] = DEFAULT_INITIALIZATION;

        test_expect_u(test, "bytes=0-4 -> 206", 206, reply.status);
        test_expect_string(test, "bytes=0-4 body", "ABCDE", reply.body);
        test_expect_true(test, "Content-Range present", _reply_header(&reply, "content-range:", content_range, sizeof(content_range)));
        test_expect_string(test, "Content-Range value", "bytes 0-4/10", content_range);
    }

    if (test_expect_true(test, "bytes=5- round trip",
                         _client_round_trip(port, "GET /static/range.txt HTTP/1.1\r\nHost: t\r\nRange: bytes=5-\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "bytes=5- -> 206", 206, reply.status);
        test_expect_string(test, "bytes=5- body", "FGHIJ", reply.body);
    }

    if (test_expect_true(test, "bytes=-3 (suffix) round trip",
                         _client_round_trip(port, "GET /static/range.txt HTTP/1.1\r\nHost: t\r\nRange: bytes=-3\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "bytes=-3 -> 206", 206, reply.status);
        test_expect_string(test, "bytes=-3 body", "HIJ", reply.body);
    }

    if (test_expect_true(test, "bytes=-0 (zero-byte suffix) round trip",
                         _client_round_trip(port, "GET /static/range.txt HTTP/1.1\r\nHost: t\r\nRange: bytes=-0\r\nConnection: close\r\n\r\n", &reply))) {
        char content_range[64] = DEFAULT_INITIALIZATION;

        test_expect_u(test, "bytes=-0 is unsatisfiable -> 416", 416, reply.status);
        test_expect_true(test, "Content-Range present on 416", _reply_header(&reply, "content-range:", content_range, sizeof(content_range)));
        test_expect_string(test, "Content-Range is bytes */size", "bytes */10", content_range);
    }

    if (test_expect_true(test, "multi-range round trip",
                         _client_round_trip(port, "GET /static/range.txt HTTP/1.1\r\nHost: t\r\nRange: bytes=0-1,3-4\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "a comma Range falls back to a full 200", 200, reply.status);
        test_expect_string(test, "full body on multi-range fallback", "ABCDEFGHIJ", reply.body);
    }

    if (test_expect_true(test, "garbage Range round trip",
                         _client_round_trip(port, "GET /static/range.txt HTTP/1.1\r\nHost: t\r\nRange: pineapple\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "a garbage Range falls back to a full 200", 200, reply.status);
    }

    if (test_expect_true(test, "20-digit Range number round trip",
                         _client_round_trip(port, "GET /static/range.txt HTTP/1.1\r\nHost: t\r\nRange: bytes=00000000000000000000-1\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "a 20-digit Range number is refused, not UB -> full 200", 200, reply.status);
        test_expect_string(test, "full body on digit-overflow fallback", "ABCDEFGHIJ", reply.body);
    }

    if (test_expect_true(test, "start>end round trip",
                         _client_round_trip(port, "GET /static/range.txt HTTP/1.1\r\nHost: t\r\nRange: bytes=5-2\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "start>end -> 416", 416, reply.status);
    }

    if (test_expect_true(test, "beyond-EOF round trip",
                         _client_round_trip(port, "GET /static/range.txt HTTP/1.1\r\nHost: t\r\nRange: bytes=1000-2000\r\nConnection: close\r\n\r\n", &reply))) {
        char content_range[64] = DEFAULT_INITIALIZATION;

        test_expect_u(test, "a range beyond EOF -> 416", 416, reply.status);
        test_expect_true(test, "Content-Range present", _reply_header(&reply, "content-range:", content_range, sizeof(content_range)));
        test_expect_string(test, "Content-Range names the real size", "bytes */10", content_range);
    }

    test_case_end(test);
}

static void _test_if_range(Test *const test, U16 const port) {
    test_case_begin(test, "If-Range: matching ETag honors Range, a stale one falls back to full 200");

    _Reply reply = DEFAULT_INITIALIZATION;
    char   etag[128] = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "plain GET to capture the ETag",
                         _client_round_trip(port, "GET /static/range.txt HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_true(test, "an ETag is present", _reply_header(&reply, "etag:", etag, sizeof(etag)));
    }

    char request[512] = DEFAULT_INITIALIZATION;

    snprintf(request, sizeof(request), "GET /static/range.txt HTTP/1.1\r\nHost: t\r\nRange: bytes=0-4\r\nIf-Range: %s\r\nConnection: close\r\n\r\n", etag);

    if (test_expect_true(test, "matching If-Range round trip", _client_round_trip(port, request, &reply))) {
        test_expect_u(test, "a matching If-Range honors the Range -> 206", 206, reply.status);
        test_expect_string(test, "the range body", "ABCDE", reply.body);
    }

    if (test_expect_true(test, "stale If-Range round trip",
                         _client_round_trip(port,
                                            "GET /static/range.txt HTTP/1.1\r\nHost: t\r\nRange: bytes=0-4\r\nIf-Range: W/\"deadbeef-0\"\r\nConnection: close\r\n\r\n",
                                            &reply))) {
        test_expect_u(test, "a stale If-Range falls back to a full 200", 200, reply.status);
        test_expect_string(test, "the full body", "ABCDEFGHIJ", reply.body);
    }

    /* The ETag is STRONG, and If-Range uses the strong comparison (RFC 9110 S13.1.5): the
     * current tag re-spelled with a W/ prefix is a weak tag, which can never match there. */
    test_expect_true(test, "the served ETag is strong (no W/ prefix)", etag[0] == '"');

    snprintf(request, sizeof(request), "GET /static/range.txt HTTP/1.1\r\nHost: t\r\nRange: bytes=0-4\r\nIf-Range: W/%s\r\nConnection: close\r\n\r\n", etag);

    if (test_expect_true(test, "weak If-Range round trip", _client_round_trip(port, request, &reply))) {
        test_expect_u(test, "a W/-prefixed If-Range never matches -> full 200", 200, reply.status);
        test_expect_string(test, "the full body on a weak If-Range", "ABCDEFGHIJ", reply.body);
    }

    test_case_end(test);
}

static void _test_cached_headers(Test *const test, U16 const port) {
    test_case_begin(test, "max_age > 0: 206 carries validators, 304 carries Cache-Control + the real Content-Type, 416 carries Accept-Ranges");

    _Reply reply     = DEFAULT_INITIALIZATION;
    char   etag[128] = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "ranged GET round trip",
                         _client_round_trip(port, "GET /cached/range.txt HTTP/1.1\r\nHost: t\r\nRange: bytes=0-4\r\nConnection: close\r\n\r\n", &reply))) {
        char last_modified[64] = DEFAULT_INITIALIZATION;

        test_expect_u(test, "206 status", 206, reply.status);
        test_expect_true(test, "206 carries an ETag", _reply_header(&reply, "etag:", etag, sizeof(etag)));
        test_expect_true(test, "206 carries a Last-Modified", _reply_header(&reply, "last-modified:", last_modified, sizeof(last_modified)));
    }

    if (test_expect_true(test, "plain GET to capture index.html's ETag",
                         _client_round_trip(port, "GET /cached/index.html HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_true(test, "an ETag is present", _reply_header(&reply, "etag:", etag, sizeof(etag)));
    }

    char request[512] = DEFAULT_INITIALIZATION;

    snprintf(request, sizeof(request), "GET /cached/index.html HTTP/1.1\r\nHost: t\r\nIf-None-Match: %s\r\nConnection: close\r\n\r\n", etag);

    if (test_expect_true(test, "If-None-Match round trip", _client_round_trip(port, request, &reply))) {
        char cache_control[64] = DEFAULT_INITIALIZATION;
        char content_type[64]  = DEFAULT_INITIALIZATION;

        test_expect_u(test, "304 status", 304, reply.status);
        test_expect_true(test, "304 carries Cache-Control", _reply_header(&reply, "cache-control:", cache_control, sizeof(cache_control)));
        test_expect_true(test, "304 carries a Content-Type", _reply_header(&reply, "content-type:", content_type, sizeof(content_type)));
        test_expect_string(test, "304's Content-Type is index.html's real type, not text/plain", "text/html; charset=utf-8", content_type);
    }

    if (test_expect_true(test, "unsatisfiable range round trip",
                         _client_round_trip(port, "GET /cached/range.txt HTTP/1.1\r\nHost: t\r\nRange: bytes=-0\r\nConnection: close\r\n\r\n", &reply))) {
        char accept_ranges[32] = DEFAULT_INITIALIZATION;

        test_expect_u(test, "416 status", 416, reply.status);
        test_expect_true(test, "416 carries Accept-Ranges", _reply_header(&reply, "accept-ranges:", accept_ranges, sizeof(accept_ranges)));
    }

    test_case_end(test);
}

static void _test_range_cap(Test *const test, U16 const port) {
    test_case_begin(test, "the 1 MiB range cap on a file larger than the cap");

    _Reply reply = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "open-ended Range on the big file round trip",
                         _client_round_trip(port, "GET /static/big.bin HTTP/1.1\r\nHost: t\r\nRange: bytes=0-\r\nConnection: close\r\n\r\n", &reply))) {
        char content_range[64] = DEFAULT_INITIALIZATION;

        test_expect_u(test, "a wide-open Range on an oversize file -> 206", 206, reply.status);
        test_expect_u(test, "the body is capped at 1 MiB, not the whole file", 1 * 1024 * 1024, reply.body_size);
        test_expect_true(test, "Content-Range present", _reply_header(&reply, "content-range:", content_range, sizeof(content_range)));

        char expected[64] = DEFAULT_INITIALIZATION;

        snprintf(expected, sizeof(expected), "bytes 0-%d/%d", 1 * 1024 * 1024 - 1, _BIG_FILE_SIZE);

        test_expect_string(test, "Content-Range reports the capped window and the real file size", expected, content_range);
    }

    test_case_end(test);
}

static void _test_head_range(Test *const test, U16 const port) {
    test_case_begin(test, "HEAD + Range answers 206 headers with no body");

    _Reply reply = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "HEAD+Range round trip",
                         _client_round_trip(port, "HEAD /static/range.txt HTTP/1.1\r\nHost: t\r\nRange: bytes=0-4\r\nConnection: close\r\n\r\n", &reply))) {
        char content_range[64] = DEFAULT_INITIALIZATION;

        test_expect_u(test, "HEAD+Range status is still 206", 206, reply.status);
        test_expect_true(test, "Content-Range is still declared", _reply_header(&reply, "content-range:", content_range, sizeof(content_range)));
        test_expect_u(test, "HEAD carries no body even for a Range response", 0, reply.body_size);
    }

    test_case_end(test);
}

/*
 * The regression pin for the server defect this round fixed (report High 2): libwebsockets
 * answers a HEAD entirely inside lws_serve_http_file, calls lws_http_transaction_completed
 * itself and returns ITS answer - >0 meaning "the transaction is over, close". http/server used
 * to report that as an ordinary "a file is streaming", so lws re-parsed the finished
 * transaction and the handler ran a SECOND time for the same request, writing a second reply -
 * this suite's own 404 "not found" - onto the same socket.
 *
 * Two halves, because only one of them showed it: with Connection: close the second reply
 * followed the first on the wire, while on a kept-alive connection lws answered 0 and the
 * transaction was already correct. Both are pinned so neither direction can regress.
 */
static void _test_head_full_file(Test *const test, U16 const port) {
    test_case_begin(test, "HEAD on a full file: exactly one response, and a kept-alive socket stays usable");

    _Reply      reply   = DEFAULT_INITIALIZATION;
    Net_Socket  closing = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "closing connection opens", _client_open(port, &closing))) {
        bool const ok = _client_write(closing, "HEAD /static/index.html HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n",
                                      CHAR_STATIC_SIZE("HEAD /static/index.html HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n"));

        if (test_expect_true(test, "HEAD round trip", ok && _client_read_headers(closing, &reply))) {
            test_expect_u(test, "HEAD on a full file is 200", 200, reply.status);
            test_expect_u(test, "exactly one Content-Type on a HEAD", 1, _reply_header_count(&reply, "content-type:"));
            test_expect_true(test, "the Content-Type carries the charset", _reply_header_count(&reply, "charset=utf-8") == 1);
            test_expect_u(test, "nothing follows the HEAD reply in the same read", 0, reply.body_size);
        }

        _Reply second = DEFAULT_INITIALIZATION;

        test_expect_false(test, "and no SECOND reply is written onto the same socket", _client_read_headers(closing, &second));

        net_socket_close(closing);
    }

    Net_Socket keep_alive = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "keep-alive connection opens", _client_open(port, &keep_alive))) {
        bool ok = _client_write(keep_alive, "HEAD /static/index.html HTTP/1.1\r\nHost: t\r\n\r\n",
                                CHAR_STATIC_SIZE("HEAD /static/index.html HTTP/1.1\r\nHost: t\r\n\r\n"));

        if (test_expect_true(test, "kept-alive HEAD round trip", ok && _client_read_headers(keep_alive, &reply))) {
            test_expect_u(test, "the kept-alive HEAD is 200", 200, reply.status);
            test_expect_u(test, "nothing follows it on the wire either", 0, reply.body_size);
        }

        ok = _client_write(keep_alive, "GET /static/range.txt HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n",
                           CHAR_STATIC_SIZE("GET /static/range.txt HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n"));

        if (test_expect_true(test, "the next request on the same socket round trips", ok && _client_read(keep_alive, &reply))) {
            test_expect_u(test, "the request after a HEAD is answered normally", 200, reply.status);
            test_expect_string(test, "and gets its own body", "ABCDEFGHIJ", reply.body);
        }

        net_socket_close(keep_alive);
    }

    test_case_end(test);
}

static void _test_mimetypes(Test *const test, U16 const port) {
    test_case_begin(test, "MIME: case-insensitive extension, unknown -> octet-stream, charset on the text family");

    _Reply reply            = DEFAULT_INITIALIZATION;
    char   content_type[128] = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "lowercase .js round trip",
                         _client_round_trip(port, "GET /static/app.js HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_true(test, "Content-Type present", _reply_header(&reply, "content-type:", content_type, sizeof(content_type)));
        test_expect_string(test, ".js is text/javascript WITH the charset, once", "text/javascript; charset=utf-8", content_type);
        test_expect_u(test, "exactly one Content-Type header", 1, _reply_header_count(&reply, "content-type:"));
    }

    /* The table is matched case-insensitively (report Mid 9): an uppercase extension used to
     * fall through to octet-stream, which the security service's nosniff header then blocks. */
    if (test_expect_true(test, "uppercase .JS round trip",
                         _client_round_trip(port, "GET /static/upper.JS HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_true(test, "Content-Type present", _reply_header(&reply, "content-type:", content_type, sizeof(content_type)));
        test_expect_string(test, ".JS resolves exactly like .js", "text/javascript; charset=utf-8", content_type);
    }

    if (test_expect_true(test, "unknown extension round trip",
                         _client_round_trip(port, "GET /static/blob.zzz HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_true(test, "Content-Type present", _reply_header(&reply, "content-type:", content_type, sizeof(content_type)));
        test_expect_string(test, "an unknown extension falls back to octet-stream, with no charset", "application/octet-stream", content_type);
        test_expect_u(test, "exactly one Content-Type header", 1, _reply_header_count(&reply, "content-type:"));
    }

    if (test_expect_true(test, "text/html round trip",
                         _client_round_trip(port, "GET /static/index.html HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_true(test, "Content-Type present", _reply_header(&reply, "content-type:", content_type, sizeof(content_type)));
        test_expect_string(test, ".html carries the charset too", "text/html; charset=utf-8", content_type);
    }

    test_case_end(test);
}

static void _test_default_file_subdirectory(Test *const test, U16 const port) {
    test_case_begin(test, "GET /sub/ maps to sub/index.html");

    _Reply reply = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "GET /static/sub/ round trip",
                         _client_round_trip(port, "GET /static/sub/ HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "status 200", 200, reply.status);
        test_expect_string(test, "sub/index.html body", "<html>sub</html>", reply.body);
    }

    test_case_end(test);
}

static void _test_spa_fallback(Test *const test, U16 const port) {
    test_case_begin(test, "SPA fallback: extension-less unmatched serves the shell, .js/.css do not");

    _Reply reply = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "extension-less unmatched round trip",
                         _client_round_trip(port, "GET /spa/some/client/route HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "status 200", 200, reply.status);
        test_expect_string(test, "the SPA shell is served", "<html>spa shell</html>", reply.body);
    }

    if (test_expect_true(test, "missing .js round trip",
                         _client_round_trip(port, "GET /spa/missing.js HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "a missing .js is NOT routed to the SPA shell", 404, reply.status);
    }

    if (test_expect_true(test, "missing .css round trip",
                         _client_round_trip(port, "GET /spa/missing.css HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "a missing .css is NOT routed to the SPA shell", 404, reply.status);
    }

    /* Report Low 5: a dot in an EARLIER segment ("v1.2") is not an extension - only the FINAL
     * segment ("users", extension-less) decides. A whole-path scan used to see the dot in "v1.2"
     * and answer 404 here instead of the shell. */
    if (test_expect_true(test, "dot in an earlier segment, extension-less final segment round trip",
                         _client_round_trip(port, "GET /spa/v1.2/users HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "status 200", 200, reply.status);
        test_expect_string(test, "the SPA shell is served, not a 404", "<html>spa shell</html>", reply.body);
    }

    test_case_end(test);
}

static void _test_symlink_refusal(Test *const test, U16 const port) {
    test_case_begin(test, "refuse_symlinks refuses a symlink in the final path component or an ancestor directory");

    _Reply reply = DEFAULT_INITIALIZATION;

    if (!file_exists_1(_FIXTURE_ROOT "/symlink_link.txt")) {
#ifdef _WIN32
        log_message_1(LOG_LEVEL_WARN, "SKIPPED: symlink_refusal leaf - CreateSymbolicLinkA failed (no SeCreateSymbolicLinkPrivilege on this"
                                      " account/machine); this pin needs a real symlink to refuse.\n");
#else
        log_message_1(LOG_LEVEL_WARN, "SKIPPED: symlink_refusal leaf - symlink() failed on this filesystem/account; this pin needs a real"
                                      " symlink to refuse.\n");
#endif // _WIN32
    }
    else if (test_expect_true(test, "symlink round trip", _client_round_trip(port, "GET /static/symlink_link.txt HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "a symlinked leaf is refused -> 404", 404, reply.status);
    }

    /* The ANCESTOR case: the link is a DIRECTORY in the middle of the path, and the real file
     * at the end is not a link at all. Only a walk of every component sees it - a final-component
     * check answers "not a link" and serves the file straight out of the linked-to tree. Run
     * independently of the leaf half: on Windows the fixture falls back to a junction, which
     * needs no privilege, so this half runs where the file symlink cannot be created. */
    if (!file_exists_1(_FIXTURE_ROOT "/sublink/index.html")) {
        log_message_1(LOG_LEVEL_WARN, "SKIPPED: symlink ANCESTOR - the directory symlink could not be created on this account/machine.\n");

        test_case_end(test);

        return;
    }

    if (test_expect_true(test, "symlinked-ancestor round trip",
                         _client_round_trip(port, "GET /static/sublink/index.html HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "a real file under a symlinked DIRECTORY is refused -> 404", 404, reply.status);
    }

#ifdef _WIN32
    /* Second Win32 spellings of the same linked directory: NTFS opens "SUBLINK" as "sublink" and
     * strips the trailing dot from an intermediate "sublink.", so an exact listing match missed
     * both and served straight through the link. */
    if (test_expect_true(test, "case-variant symlinked-ancestor round trip",
                         _client_round_trip(port, "GET /static/SUBLINK/index.html HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "a case variant of a symlinked DIRECTORY is refused -> 404", 404, reply.status);
    }

    if (test_expect_true(test, "trailing-dot symlinked-ancestor round trip",
                         _client_round_trip(port, "GET /static/sublink./index.html HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "'sublink.' (a trailing-dot spelling of the link) is refused -> 404", 404, reply.status);
    }
#endif // _WIN32

    test_case_end(test);
}

static void _test_dotfiles(Test *const test, U16 const port) {
    test_case_begin(test, "refuse_dotfiles: off serves a dotfile, on refuses any dot component except .well-known");

    _Reply reply = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "dotfile with the knob off round trip", _client_round_trip(port, "GET /static/.env HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "refuse_dotfiles off (the default): /.env is served -> 200", 200, reply.status);
        test_expect_string(test, "the dotfile body", "SECRET=1", reply.body);
    }

    if (test_expect_true(test, "root dotfile with the knob on round trip", _client_round_trip(port, "GET /dot/.env HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "refuse_dotfiles on: /.env is refused -> 404", 404, reply.status);
    }

    if (test_expect_true(test, "nested dot-directory with the knob on round trip",
                         _client_round_trip(port, "GET /dot/sub/.git/config HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "refuse_dotfiles on: /sub/.git/config is refused -> 404", 404, reply.status);
    }

    if (test_expect_true(test, ".well-known with the knob on round trip", _client_round_trip(port, "GET /dot/.well-known/x HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "refuse_dotfiles on: /.well-known/x is exempt -> 200", 200, reply.status);
        test_expect_string(test, "the .well-known body", "well-known", reply.body);
    }

    test_case_end(test);
}

/*
 * The 8.3 short-alias refusal is shape-narrow: a '~'+digit name whose stem is longer than 8 bytes
 * (or whose extension is longer than 3) can never be a short name, so it is served on every OS.
 */
static void _test_tilde_name_served(Test *const test, U16 const port) {
    test_case_begin(test, "a '~'+digit long name that cannot be an 8.3 alias is served");

    _Reply reply = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "my-report~2.pdf round trip", _client_round_trip(port, "GET /static/my-report~2.pdf HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "'my-report~2.pdf' (stem > 8 bytes) is served -> 200", 200, reply.status);
        test_expect_string(test, "the my-report~2.pdf body", "report", reply.body);
    }

    test_case_end(test);
}

#ifdef _WIN32
static void _test_ntfs_spellings_refused(Test *const test, U16 const port) {
    test_case_begin(test, "Windows: an NTFS stream (':') or a trailing dot/space spelling of a real file is refused");

    _Reply reply = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "Zone.Identifier stream round trip",
                         _client_round_trip(port, "GET /static/index.html:Zone.Identifier HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "an alternate data stream is refused -> 404", 404, reply.status);
    }

    if (test_expect_true(test, "::$DATA round trip", _client_round_trip(port, "GET /static/index.html::$DATA HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "the ::$DATA spelling is refused -> 404", 404, reply.status);
    }

    if (test_expect_true(test, "trailing dot round trip", _client_round_trip(port, "GET /static/index.html. HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "'index.html.' is refused -> 404 (Win32 would open index.html)", 404, reply.status);
    }

    if (test_expect_true(test, "trailing space round trip", _client_round_trip(port, "GET /static/index.html%20 HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "'index.html ' is refused -> 404 (Win32 would open index.html)", 404, reply.status);
    }

    /* An INTERMEDIATE trailing dot: Win32 strips it there too, so "sub./index.html" opens
     * sub/index.html - pinned whether or not the symlink fixture could be created. */
    if (test_expect_true(test, "intermediate trailing dot round trip", _client_round_trip(port, "GET /static/sub./index.html HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "'sub./index.html' is refused -> 404 (Win32 would open sub/index.html)", 404, reply.status);
    }

    test_case_end(test);
}

static void _test_reserved_windows_name_refused(Test *const test, U16 const port) {
    test_case_begin(test, "a Windows reserved device name is refused, with or without an extension");

    _Reply bare = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "bare reserved name round trip",
                         _client_round_trip(port, "GET /static/nul HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &bare))) {
        test_expect_u(test, "bare 'nul' is refused -> 404", 404, bare.status);
    }

    _Reply with_extension = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "reserved name with extension round trip",
                         _client_round_trip(port, "GET /static/con.txt HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &with_extension))) {
        test_expect_u(test, "'con.txt' is refused -> 404", 404, with_extension.status);
    }

    _Reply trailing_space = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "reserved name with a trailing space round trip",
                         _client_round_trip(port, "GET /static/nul%20 HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &trailing_space))) {
        test_expect_u(test, "'nul ' is refused -> 404 (Win32 trims trailing spaces before the lookup)", 404, trailing_space.status);
    }

    _Reply trailing_dot = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "reserved name with a trailing dot round trip",
                         _client_round_trip(port, "GET /static/con. HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &trailing_dot))) {
        test_expect_u(test, "'con.' is refused -> 404 (Win32 trims trailing dots before the lookup)", 404, trailing_dot.status);
    }

    _Reply console_input = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "CONIN$ round trip", _client_round_trip(port, "GET /static/CONIN$ HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &console_input))) {
        test_expect_u(test, "'CONIN$' (the console input device) is refused -> 404", 404, console_input.status);
    }

    test_case_end(test);
}

/*
 * Device-name spellings Win32 still resolves to the device. Spaces and dots between the stem and
 * the extension are trimmed before Win32's device check, so "CONIN$ .txt" opens the console input
 * device (an unrefused request hangs the server thread on it). The ANSI APIs read the single
 * cp1252 byte 0xB9/0xB2/0xB3 after COM/LPT as the superscript device digit. Every row must answer
 * 404; a legitimate "console.txt" must still answer 200.
 */
static void _test_reserved_windows_name_spellings_refused(Test *const test, U16 const port) {
    test_case_begin(test, "Windows: device-name spellings Win32 trims or reads as a device are refused");

    static char const *const paths[] = {
        "CONIN$%20.txt", "conin$%20.txt", "AUX%20.html", "CONIN$.%20.txt", "CONIN$%20.%20.txt", "CONOUT$%20.js", "nul%20.txt",
        "CONIN$%20%20.a.b", "COM%B9", "LPT%B2.txt", "com%B3", "COM%C2%B9", "LPT%C2%B2.txt", "CONIN$.txt", "conin$", "ENV~10"
    };

    for (USize i = 0; i < sizeof(paths) / sizeof(paths[0]); i += 1) {
        _Reply  reply           = DEFAULT_INITIALIZATION;
        char    request[256]    = DEFAULT_INITIALIZATION;
        char    label[128]      = DEFAULT_INITIALIZATION;

        snprintf(request, sizeof(request), "GET /static/%s HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", paths[i]);
        snprintf(label, sizeof(label), "'%s' is refused -> 404", paths[i]);

        if (test_expect_true(test, "device-name spelling round trip", _client_round_trip(port, request, &reply))) {
            test_expect_u(test, label, 404, reply.status);
        }
    }

    _Reply legitimate = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "console.txt round trip", _client_round_trip(port, "GET /static/console.txt HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &legitimate))) {
        test_expect_u(test, "'console.txt' (a real asset, not a device) is served -> 200", 200, legitimate.status);
    }

    test_case_end(test);
}

/*
 * The 8.3 short alias of a dotfile. GetShortPathNameA asks the volume for `.env`'s real short
 * name: on a volume with short names enabled (C: by default) it answers something like "ENV~1",
 * which starts with no '.', so refuse_dotfiles alone never saw it and the file was served. The
 * '~'+digit refusal is unconditional, so it is pinned with the knob on (/dot) AND off (/static).
 * A volume with short names disabled answers the long name back; the refusal is lexical, so a
 * hand-spelled ENV~1 is pinned instead and the live-alias half is logged as skipped.
 */
static void _test_short_name_refused(Test *const test, U16 const port) {
    test_case_begin(test, "Windows: an 8.3 short alias (ENV~1 for .env) is refused, refuse_dotfiles on or off");

    char            short_path[512] = DEFAULT_INITIALIZATION;
    DWORD   const   short_size      = GetShortPathNameA(_FIXTURE_ROOT "\\.env", short_path, (DWORD) sizeof(short_path));
    char    const   *short_name     = "ENV~1";
    bool            live_alias      = false;

    if (short_size > 0 && short_size < sizeof(short_path)) {
        USize base_start = 0;

        for (USize index = short_size; index > 0; index -= 1) {
            if (short_path[index - 1] == '\\' || short_path[index - 1] == '/') {
                base_start = index;

                break;
            }
        }

        if (!char_compare_equal_1(short_path + base_start, ".env")) {
            short_name = short_path + base_start;
            live_alias = true;
        }
    }

    if (!live_alias) {
        log_message_1(LOG_LEVEL_WARN, "SKIPPED: live 8.3 alias - this volume answered no short name for .env; pinning a hand-spelled ENV~1.\n");
    }

    _Reply  reply           = DEFAULT_INITIALIZATION;
    char    request[512]    = DEFAULT_INITIALIZATION;

    snprintf(request, sizeof(request), "GET /dot/%s HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", short_name);

    if (test_expect_true(test, "short alias with refuse_dotfiles on round trip", _client_round_trip(port, request, &reply))) {
        test_expect_u(test, "the 8.3 alias of .env is refused with refuse_dotfiles on -> 404", 404, reply.status);
    }

    snprintf(request, sizeof(request), "GET /static/%s HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", short_name);

    if (test_expect_true(test, "short alias with refuse_dotfiles off round trip", _client_round_trip(port, request, &reply))) {
        test_expect_u(test, "the 8.3 alias of .env is refused with refuse_dotfiles off too -> 404", 404, reply.status);
    }

    if (test_expect_true(test, "BACKUP~1.TXT round trip", _client_round_trip(port, "GET /static/BACKUP~1.TXT HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "'BACKUP~1.TXT' (a real file, but 8.3-shaped) is refused -> 404", 404, reply.status);
    }

    test_case_end(test);
}
#endif // _WIN32

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

    _fixture_setup();

    _svc_default = http_service_static_init_1(_FIXTURE_ROOT, "/static");
    _svc_default.refuse_symlinks = true;

    _svc_spa = http_service_static_init_2(_FIXTURE_SPA_ROOT, "/spa", "index.html", true, 0);

    /* max_age > 0 (report Low 6): only this service exercises the Cache-Control path on the
     * 206 and 304 branches - _svc_default and _svc_spa both pass max_age 0. */
    _svc_cached = http_service_static_init_2(_FIXTURE_ROOT, "/cached", nullptr, false, 60);

    /* The same root as _svc_default, with the dotfile knob on - _svc_default keeps it off, so the
     * pair pins both directions against one fixture. */
    _svc_dotfiles = http_service_static_init_1(_FIXTURE_ROOT, "/dot");
    _svc_dotfiles.refuse_dotfiles = true;

    Test test = test_init("tests/http/service/static/test_all.c");

    test_suite_begin(&test, "http_service_static (live server)");

    HTTP_Server *server = http_server_new();
    U16 const    port   = _start(&test, server);

    _test_conditional_get(&test, port);
    _test_range(&test, port);
    _test_if_range(&test, port);
    _test_cached_headers(&test, port);
    _test_range_cap(&test, port);
    _test_head_range(&test, port);
    _test_head_full_file(&test, port);
    _test_mimetypes(&test, port);
    _test_default_file_subdirectory(&test, port);
    _test_spa_fallback(&test, port);
    _test_symlink_refusal(&test, port);
    _test_dotfiles(&test, port);
    _test_tilde_name_served(&test, port);
#ifdef _WIN32
    _test_ntfs_spellings_refused(&test, port);
    _test_reserved_windows_name_refused(&test, port);
    _test_reserved_windows_name_spellings_refused(&test, port);
    _test_short_name_refused(&test, port);
#endif // _WIN32

    http_server_delete(&server);

    test_suite_end(&test);

    I32 const result = test_uninit(&test);

    _fixture_teardown();

    return result;
}