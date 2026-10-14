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

#include <char/char.h>
#include <dir/dir.h>
#include <file/file.h>
#include <http/service/static/static.h>
#include <log/log.h>
#include <memory/memory.h>
#include <net/net.h>
#include <test/test.h>
#include <thread/thread.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/
/* 3 MiB + 1: well past the 1 MiB window 0.4.x capped a 206 at, past any socket buffer (so the
 * streaming seam has to wait on a choked connection), and not a whole number of
 * HTTP_SERVER_FILE_CHUNK_SIZE chunks, so the last chunk is a partial one. */
#define _BIG_FILE_SIZE          (3 * 1024 * 1024 + 1)
#define _CLIENT_BODY_MAX        (_BIG_FILE_SIZE + 16384)
#define _CLIENT_HEADERS_MAX     8192
#define _CLIENT_IO_TIMEOUT_MS   4000
#define _CLIENT_RAW_MAX         (_BIG_FILE_SIZE + 32768)
#define _CLIENT_STALL_MS        2000
#define _COMMAND_MAX            1024
#define _FIXTURE_LATE_ROOT      "static_test_fixture_late"
#define _FIXTURE_OUTSIDE        "static_test_fixture_outside"
#define _FIXTURE_ROOT           "static_test_fixture"
#define _FIXTURE_SPA_ROOT       "static_test_fixture_spa"
#define _SECRET_CONTENT         "TOP-SECRET-OUTSIDE-THE-ROOT"
#define _SWAP_RETRY_COUNT       40
#define _SWAP_RETRY_DELAY_MS    50

/* Link targets as a link INSIDE _FIXTURE_ROOT spells them (relative to the link's own directory),
 * in the separator the platform's link resolution expects. */
#ifdef _WIN32
#define _OUTSIDE_FROM_ROOT      "..\\" _FIXTURE_OUTSIDE
#define _SECRET_FROM_ROOT       "..\\" _FIXTURE_OUTSIDE "\\secret.txt"
#else
#define _OUTSIDE_FROM_ROOT      "../" _FIXTURE_OUTSIDE
#define _SECRET_FROM_ROOT       "../" _FIXTURE_OUTSIDE "/secret.txt"
#endif // _WIN32

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

/**
 * @brief Read one HTTP/1.1 reply: status line, headers, then Content-Length bytes of body -
 *        sleeping stall_ms between the header block and the rest of the body (0: no stall), the
 *        slow reader whose unread bytes back up into the server's socket while it waits.
 */
static bool _client_read_stalled(Net_Socket const socket, _Reply *const reply, U32 const stall_ms) {
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

    if (stall_ms > 0) {
        thread_sleep(stall_ms);
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

/** @brief Read one HTTP/1.1 reply with no stall (see _client_read_stalled). */
static bool _client_read(Net_Socket const socket, _Reply *const reply) {
    return _client_read_stalled(socket, reply, 0);
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
/* Whether this account can create a symbolic link to a FILE (probed once by _fixture_setup). */
static bool _file_links_available = false;
/* Whether T4's file symlink and T5's junction to the outside secret could be created. */
static bool _outside_file_link_created = false;
static bool _outside_junction_created = false;

static void _fixture_write(char const *const path, char const *const content) {
    FILE *const file = fopen(path, "wb");

    if (memory_empty(file)) {
        log_message_2(LOG_LEVEL_ERROR, LOG_METADATA, "_fixture_write: fopen failed for %s", path);

        return;
    }

    fwrite(content, 1, char_length(content), file);
    fclose(file);
}

#ifdef _WIN32
/* Copy `path` into `buffer` with every '/' turned into '\\' - cmd's mklink reads a forward slash
 * as a switch. */
static void _fixture_backslash(char const *const path, char *const buffer, USize const capacity) {
    char_copy_truncate(buffer, capacity, path);

    for (USize index = 0; buffer[index] != '\0'; index += 1) {
        if (buffer[index] == '/') {
            buffer[index] = '\\';
        }
    }
}
#endif // _WIN32

/* rename with a short retry: the fixture lives under a Dropbox-synced tree, whose client can hold
 * a just-written entry open for a moment, and a rename against it then fails with a sharing
 * violation that has nothing to do with the server under test. */
static bool _fixture_rename(char const *const from, char const *const to) {
    for (USize attempt = 0; attempt < _SWAP_RETRY_COUNT; attempt += 1) {
        if (rename(from, to) == 0) {
            return true;
        }

        thread_sleep(_SWAP_RETRY_DELAY_MS);
    }

    log_message_2(LOG_LEVEL_ERROR, LOG_METADATA, "_fixture_rename: %s -> %s failed", from, to);

    return false;
}

/* A symbolic link to a FILE. Windows needs SeCreateSymbolicLinkPrivilege - an elevated process, or
 * Developer Mode honouring the unprivileged flag - and answers false without it. */
static bool _fixture_link_file(char const *const link, char const *const target_from_link) {
#ifdef _WIN32
    return CreateSymbolicLinkA(link, target_from_link, SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != 0;
#else
    return symlink(target_from_link, link) == 0;
#endif // _WIN32
}

#ifdef _WIN32
/* A junction - mklink /J needs no privilege and resolves its cwd-relative target to an absolute
 * path. True when `link` now exists as a reparse point. */
static bool _fixture_junction(char const *const link, char const *const target_from_cwd) {
    char link_native[_COMMAND_MAX]      = DEFAULT_INITIALIZATION;
    char target_native[_COMMAND_MAX]    = DEFAULT_INITIALIZATION;
    char command[_COMMAND_MAX]          = DEFAULT_INITIALIZATION;

    _fixture_backslash(link, link_native, sizeof(link_native));
    _fixture_backslash(target_from_cwd, target_native, sizeof(target_native));
    snprintf(command, sizeof(command), "mklink /J \"%s\" \"%s\" >NUL", link_native, target_native);
    system(command);

    DWORD const attributes = GetFileAttributesA(link);

    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}
#endif // _WIN32

/* A link to a DIRECTORY: a directory symlink where it can be created, else (Windows, no
 * privilege) a junction. */
static bool _fixture_link_directory(char const *const link, char const *const target_from_link, char const *const target_from_cwd) {
#ifdef _WIN32
    if (CreateSymbolicLinkA(link, target_from_link, SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != 0) {
        return true;
    }

    return _fixture_junction(link, target_from_cwd);
#else
    (void) target_from_cwd;

    return symlink(target_from_link, link) == 0;
#endif // _WIN32
}

/* Remove a link (or a plain file) itself, never what it points at. */
static void _fixture_unlink(char const *const link, bool const directory) {
#ifdef _WIN32
    if (directory) {
        RemoveDirectoryA(link);
    }
    else {
        DeleteFileA(link);
    }
#else
    (void) directory;

    unlink(link);
#endif // _WIN32
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

    /* The TOCTOU pins: a secret OUTSIDE the root, and innocent files the swap hooks replace with
     * links to it - a file (T1, T3) and a directory holding a same-named file (T2). */
    dir_create_all_1(_FIXTURE_OUTSIDE);
    dir_create_all_1(_FIXTURE_ROOT "/swapdir");
    _fixture_write(_FIXTURE_OUTSIDE "/secret.txt", _SECRET_CONTENT);
    _fixture_write(_FIXTURE_ROOT "/swap_a.txt", "innocent-a");
    _fixture_write(_FIXTURE_ROOT "/swapdir/secret.txt", "innocent-dir");

    _file_links_available = _fixture_link_file(_FIXTURE_ROOT "/probe_link.txt", "symlink_target.txt");

    if (_file_links_available) {
        _fixture_unlink(_FIXTURE_ROOT "/probe_link.txt", false);
    }

    /* T3's file (replaced after the open), and T4/T5's links to the outside secret, IN PLACE from
     * the start: a file symlink (needs the privilege) and, on Windows, a junction (needs none). */
    _fixture_write(_FIXTURE_ROOT "/swap_open.txt", "original-bytes-T3");
    _outside_file_link_created = _fixture_link_file(_FIXTURE_ROOT "/outside_link.txt", _SECRET_FROM_ROOT);
#ifdef _WIN32
    _outside_junction_created = _fixture_junction(_FIXTURE_ROOT "/outside_junction", _FIXTURE_OUTSIDE);
#endif // _WIN32

    /* T6: the dotfile whose 8.3 alias (HTACCE~1 on a volume that makes short names) is asked for
     * with refuse_symlinks on and off. */
    _fixture_write(_FIXTURE_ROOT "/.htaccess", "Deny from all");

    /* T8: a non-ASCII name, written through the WIDE API on Windows so the bytes on disk are
     * "cafe" with an acute e whatever the process code page is. */
#ifdef _WIN32
    FILE *const non_ascii = _wfopen(L"" _FIXTURE_ROOT L"\\caf\u00e9.txt", L"wb");
#else
    FILE *const non_ascii = fopen(_FIXTURE_ROOT "/caf\xC3\xA9.txt", "wb");
#endif // _WIN32

    if (!memory_empty(non_ascii)) {
        fwrite("non-ascii-name", 1, CHAR_STATIC_SIZE("non-ascii-name"), non_ascii);
        fclose(non_ascii);
    }

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
    /* The links into _FIXTURE_OUTSIDE go first, as links, so no recursive removal below can
     * ever be asked to walk through one. */
    if (_outside_file_link_created) {
        _fixture_unlink(_FIXTURE_ROOT "/outside_link.txt", false);
    }

    if (_outside_junction_created) {
        _fixture_unlink(_FIXTURE_ROOT "/outside_junction", true);
    }

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

    if (!dir_remove_all_1(_FIXTURE_LATE_ROOT)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "_fixture_teardown: dir_remove_all_1 failed for %s", _FIXTURE_LATE_ROOT);
    }

    /* Last: every link into it lived under _FIXTURE_ROOT, already gone. */
    if (!dir_remove_all_1(_FIXTURE_OUTSIDE)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "_fixture_teardown: dir_remove_all_1 failed for %s", _FIXTURE_OUTSIDE);
    }
}

/*==============================================================================
 * MARK: - Swap hooks (TOCTOU pins)
 *============================================================================*/
/*
 * What a static.c test hook does to the file system when it fires: park `original` under
 * `parked`, then put a file link, a directory link (junction on Windows without privilege), or a
 * plain file carrying the secret's bytes where it was. `at_open` picks the hook - after the
 * lexical resolve (before the open) or after the open (before the send). One request consumes
 * one arming; _swap_restore puts the fixture back.
 */
typedef enum {
    _SWAP_KIND_DIRECTORY_LINK,
    _SWAP_KIND_FILE_LINK,
    _SWAP_KIND_REGULAR_FILE
} _Swap_Kind;

typedef struct {
    char const  *original;
    char const  *parked;
    char const  *target_from_link;
    char const  *target_from_cwd;
    _Swap_Kind  kind;
    bool        at_open;
    bool        armed;
    bool        done;
} _Swap;

static _Swap _swap = DEFAULT_INITIALIZATION;

static void _swap_apply(void) {
    _swap.armed = false;

    if (!_fixture_rename(_swap.original, _swap.parked)) {
        return;
    }

    if (_swap.kind == _SWAP_KIND_FILE_LINK) {
        _swap.done = _fixture_link_file(_swap.original, _swap.target_from_link);
    }
    else if (_swap.kind == _SWAP_KIND_DIRECTORY_LINK) {
        _swap.done = _fixture_link_directory(_swap.original, _swap.target_from_link, _swap.target_from_cwd);
    }
    else {
        _fixture_write(_swap.original, _SECRET_CONTENT);
        _swap.done = file_exists_1(_swap.original);
    }

    if (!_swap.done) {
        _fixture_rename(_swap.parked, _swap.original);
    }
}

static void _swap_hook_after_resolve(char const *const file_name) {
    (void) file_name;

    if (_swap.armed && !_swap.at_open) {
        _swap_apply();
    }
}

static void _swap_hook_after_open(char const *const file_name) {
    (void) file_name;

    if (_swap.armed && _swap.at_open) {
        _swap_apply();
    }
}

static void _swap_restore(void) {
    _swap.armed = false;

    if (!_swap.done) {
        return;
    }

    _fixture_unlink(_swap.original, _swap.kind == _SWAP_KIND_DIRECTORY_LINK);
    _fixture_rename(_swap.parked, _swap.original);

    _swap.done = false;
}

/*==============================================================================
 * MARK: - Server wiring
 *============================================================================*/
static HTTP_Service_Static _svc_default;
static HTTP_Service_Static _svc_spa;
static HTTP_Service_Static _svc_cached;
static HTTP_Service_Static _svc_dotfiles;
static HTTP_Service_Static _svc_follow = DEFAULT_INITIALIZATION;
static HTTP_Service_Static _svc_late = DEFAULT_INITIALIZATION;

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

    if (http_service_static_serve_2(&_svc_follow, request, response)) {
        return;
    }

    if (http_service_static_serve_2(&_svc_late, request, response)) {
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
/* A pin that cannot run on this machine says so on stdout - where the suite's own report goes,
 * whatever the log level (the suite runs at LOG_LEVEL_ERROR, which silenced the WARN skips this
 * replaced) - instead of passing vacuously. */
static void _skip_loudly(char const *const message) {
    printf("    SKIPPED: %s\n", message);
}

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

/*
 * T7 - the 0.4.x 1 MiB cap is gone: a wide-open Range on a 3 MiB + 1 file is ONE 206 carrying the
 * whole remainder, streamed from the handle chunk by chunk, and an interior window starting
 * mid-chunk arrives byte-exact too.
 */
static void _test_range_large_whole(Test *const test, U16 const port) {
    test_case_begin(test, "T7: a wide-open Range on a 3 MiB file arrives whole as ONE 206, byte-exact; an interior window too");

    _Reply reply = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "open-ended Range on the big file round trip",
                         _client_round_trip(port, "GET /static/big.bin HTTP/1.1\r\nHost: t\r\nRange: bytes=0-\r\nConnection: close\r\n\r\n", &reply))) {
        char content_range[64]  = DEFAULT_INITIALIZATION;
        char expected[64]       = DEFAULT_INITIALIZATION;

        snprintf(expected, sizeof(expected), "bytes 0-%d/%d", _BIG_FILE_SIZE - 1, _BIG_FILE_SIZE);

        test_expect_u(test, "a wide-open Range on a 3 MiB file -> 206", 206, reply.status);
        test_expect_true(test, "the whole declared body arrived", reply.complete);
        test_expect_u(test, "the body is the WHOLE remainder, no cap", _BIG_FILE_SIZE, reply.body_size);
        test_expect_true(test, "Content-Range present", _reply_header(&reply, "content-range:", content_range, sizeof(content_range)));
        test_expect_string(test, "Content-Range names the whole file", expected, content_range);
        test_expect_true(test, "the body is byte-exact",
                         reply.body_size == _BIG_FILE_SIZE && char_compare_equal_2(reply.body, reply.body_size, (char const*) _big_file_content, _BIG_FILE_SIZE));
    }

    USize const window_start    = 65535;
    USize const window_end      = 2200000;
    USize const window_size     = window_end - window_start + 1;
    char        request[256]    = DEFAULT_INITIALIZATION;

    snprintf(request, sizeof(request), "GET /static/big.bin HTTP/1.1\r\nHost: t\r\nRange: bytes=%llu-%llu\r\nConnection: close\r\n\r\n",
             (unsigned long long) window_start, (unsigned long long) window_end);

    if (test_expect_true(test, "interior window round trip", _client_round_trip(port, request, &reply))) {
        test_expect_u(test, "an interior window -> 206", 206, reply.status);
        test_expect_u(test, "the interior window's size", window_size, reply.body_size);
        test_expect_true(test, "the interior window is byte-exact",
                         reply.body_size == window_size && char_compare_equal_2(reply.body, reply.body_size, (char const*) _big_file_content + window_start, window_size));
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
        _skip_loudly("symlink_refusal leaf - CreateSymbolicLinkA failed (no SeCreateSymbolicLinkPrivilege on this account/machine); this pin needs a real symlink to refuse.");
#else
        _skip_loudly("symlink_refusal leaf - symlink() failed on this filesystem/account; this pin needs a real symlink to refuse.");
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
        _skip_loudly("symlink ANCESTOR - the directory symlink could not be created on this account/machine.");

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

/*
 * T1 - swap BEFORE the open, final component. After the lexical resolve (and, on the old code,
 * after its refuse_symlinks walk said "clear"), the hook parks swap_a.txt and puts a symlink to a
 * secret OUTSIDE the root in its place. The open must refuse the link itself: a nofollow open
 * answers "link refused", the request 404s, and not one byte of the secret is on the wire.
 */
static void _test_swap_before_open_file(Test *const test, U16 const port) {
    test_case_begin(test, "TOCTOU T1: a file swapped for a link to an outside secret between resolve and open is never served");

    if (!_file_links_available) {
        _skip_loudly("TOCTOU T1 (final-component swap) - this account cannot create a FILE symlink (no"
                     " SeCreateSymbolicLinkPrivilege, Developer Mode off); a hard link is no substitute (not a"
                     " reparse point). T2 below pins the swap through a junction, which needs no privilege.");

        test_case_end(test);

        return;
    }

    _Swap const swap = {
        .original         = _FIXTURE_ROOT "/swap_a.txt",
        .parked           = _FIXTURE_ROOT "/swap_a.parked",
        .target_from_link = _SECRET_FROM_ROOT,
        .target_from_cwd  = _FIXTURE_OUTSIDE "/secret.txt",
        .kind             = _SWAP_KIND_FILE_LINK,
        .at_open          = false,
        .armed            = true,
        .done             = false
    };

    _swap = swap;

    _Reply reply = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "T1 round trip", _client_round_trip(port, "GET /static/swap_a.txt HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_true(test, "T1: the hook swapped swap_a.txt for a link to the secret", _swap.done);
        test_expect_u(test, "T1: the link swapped in before the open is refused -> 404", 404, reply.status);
        test_expect_false(test, "T1: not one byte of the secret reaches the client", char_find_exists_1(reply.body, _SECRET_CONTENT));
    }

    _swap_restore();

    test_case_end(test);
}

/*
 * T2 - swap BEFORE the open, an ANCESTOR. The hook parks the directory swapdir and puts a link to
 * the outside directory in its place (a junction on Windows without privilege), so
 * swapdir/secret.txt now names the outside secret. A nofollow open follows an ancestor like any
 * open - only the final-path compare on the opened handle sees where the bytes really live.
 */
static void _test_swap_before_open_ancestor(Test *const test, U16 const port) {
    test_case_begin(test, "TOCTOU T2: a directory swapped for a link to an outside directory between resolve and open is never served");

    _Swap const swap = {
        .original         = _FIXTURE_ROOT "/swapdir",
        .parked           = _FIXTURE_ROOT "/swapdir_parked",
        .target_from_link = _OUTSIDE_FROM_ROOT,
        .target_from_cwd  = _FIXTURE_OUTSIDE,
        .kind             = _SWAP_KIND_DIRECTORY_LINK,
        .at_open          = false,
        .armed            = true,
        .done             = false
    };

    _swap = swap;

    _Reply reply = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "T2 round trip", _client_round_trip(port, "GET /static/swapdir/secret.txt HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_true(test, "T2: the hook swapped swapdir for a link to the outside directory", _swap.done);
        test_expect_u(test, "T2: a linked ancestor swapped in before the open is refused -> 404", 404, reply.status);
        test_expect_false(test, "T2: not one byte of the secret reaches the client", char_find_exists_1(reply.body, _SECRET_CONTENT));
    }

    _swap_restore();

    test_case_end(test);
}

/*
 * T3 - swap AFTER the open. The hook (after the open and the compare, before the send) parks
 * swap_open.txt and puts a link to the outside secret in its place - or, where this account cannot
 * create a file symlink, a plain file carrying the secret's bytes, which pins the same property.
 * The response must carry the ORIGINAL bytes: the body comes from the handle that was verified,
 * and nothing re-resolves the name. A future "re-open per chunk" refactor fails this.
 */
static void _test_swap_after_open(Test *const test, U16 const port) {
    test_case_begin(test, "TOCTOU T3: a file swapped for the secret AFTER the open still serves the original bytes (200)");

    if (!_file_links_available) {
        _skip_loudly("TOCTOU T3 link variant - no FILE symlink privilege here; T3 swaps in a plain file carrying the secret's"
                     " bytes instead, which pins the same property (the body comes from the handle, never the name).");
    }

    _Swap const swap = {
        .original         = _FIXTURE_ROOT "/swap_open.txt",
        .parked           = _FIXTURE_ROOT "/swap_open.parked",
        .target_from_link = _SECRET_FROM_ROOT,
        .target_from_cwd  = _FIXTURE_OUTSIDE "/secret.txt",
        .kind             = _file_links_available ? _SWAP_KIND_FILE_LINK : _SWAP_KIND_REGULAR_FILE,
        .at_open          = true,
        .armed            = true,
        .done             = false
    };

    _swap = swap;

    _Reply reply = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "T3 round trip", _client_round_trip(port, "GET /static/swap_open.txt HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_true(test, "T3: the hook replaced swap_open.txt after the open", _swap.done);
        test_expect_u(test, "T3: a swap after the open changes nothing -> 200", 200, reply.status);
        test_expect_string(test, "T3: the body is the ORIGINAL file's bytes, from the handle", "original-bytes-T3", reply.body);
    }

    _swap_restore();

    test_case_end(test);
}

/*
 * T4 - a file symlink to the outside secret, in place from the start (no hook): refused with
 * refuse_symlinks on (the default), followed with it off - the documented opt-out.
 */
static void _test_link_in_place(Test *const test, U16 const port) {
    test_case_begin(test, "T4: a file symlink to an outside secret, in place: refuse_symlinks on -> 404, off -> 200 (followed)");

    if (!_outside_file_link_created) {
        _skip_loudly("T4 (file symlink in place) - this account cannot create a FILE symlink; T5 pins the in-place refusal through a junction.");

        test_case_end(test);

        return;
    }

    _Reply reply = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "T4 on round trip", _client_round_trip(port, "GET /static/outside_link.txt HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "T4: refuse_symlinks on (the default) refuses the link -> 404", 404, reply.status);
        test_expect_false(test, "T4: not one byte of the secret reaches the client", char_find_exists_1(reply.body, _SECRET_CONTENT));
    }

    if (test_expect_true(test, "T4 off round trip", _client_round_trip(port, "GET /follow/outside_link.txt HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "T4: refuse_symlinks off follows the link -> 200", 200, reply.status);
        test_expect_string(test, "T4: ... and serves its target", _SECRET_CONTENT, reply.body);
    }

    test_case_end(test);
}

/*
 * T5 - the same with a junction (Windows; needs no privilege), as an ANCESTOR: the open follows
 * it like any intermediate component, so only the final-path compare refuses it.
 */
static void _test_junction_in_place(Test *const test, U16 const port) {
    test_case_begin(test, "T5: a junction to an outside directory, in place: refuse_symlinks on -> 404, off -> 200 (followed)");

#ifdef _WIN32
    if (!_outside_junction_created) {
        _skip_loudly("T5 (junction in place) - mklink /J failed on this volume.");

        test_case_end(test);

        return;
    }

    _Reply reply = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "T5 on round trip", _client_round_trip(port, "GET /static/outside_junction/secret.txt HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "T5: refuse_symlinks on refuses a file reached through a junction -> 404", 404, reply.status);
        test_expect_false(test, "T5: not one byte of the secret reaches the client", char_find_exists_1(reply.body, _SECRET_CONTENT));
    }

    if (test_expect_true(test, "T5 off round trip", _client_round_trip(port, "GET /follow/outside_junction/secret.txt HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "T5: refuse_symlinks off follows the junction -> 200", 200, reply.status);
        test_expect_string(test, "T5: ... and serves the file behind it", _SECRET_CONTENT, reply.body);
    }
#else
    (void) port;

    _skip_loudly("T5 (junction) - junctions are Windows-only; the directory-symlink ancestor is pinned by T2 and the symlink-refusal case.");
#endif // _WIN32

    test_case_end(test);
}

/*
 * refuse_symlinks is ON by default (0.5.0) in both constructors, and root_dir's final path is
 * taken once at construction - so a root that did not exist then refuses every request (fail
 * closed) even after it is created, while the same tree served with the knob off is served.
 *
 * That refusal is a "cannot tell" - no root to compare against - not a link, so its WARN names
 * the ROOT, never a "link refused", and it is logged ONCE per process, not once per request (the
 * log volume would otherwise scale with traffic). The log state is per THREAD (LOG_THREAD_
 * IMPLEMENTATION), so the server thread's WARN is out of the mirror's reach: the two captured
 * requests go through serve_1 on THIS thread with a never-dereferenced response, as
 * test_unchecked.c does - a refusal returns before serve_1 touches it, and a late root that was
 * SERVED would crash the run loudly, never pass. The once-flag is process-wide, so these must be
 * the first requests any case sends to /late; the live round trip follows them.
 */
static void _test_symlink_default_and_late_root(Test *const test, U16 const port) {
    test_case_begin(test, "refuse_symlinks defaults ON in init_1 and init_2; a root missing at construction fails closed, WARNed once naming the root");

    test_expect_true(test, "init_1 turns refuse_symlinks on", _svc_default.refuse_symlinks);
    test_expect_true(test, "init_2 turns refuse_symlinks on", _svc_spa.refuse_symlinks);
    test_expect_true(test, "init_1 resolved root_dir's final path", _svc_default.root_final_size > 0);
    test_expect_true(test, "init_2 resolved root_dir's final path", _svc_spa.root_final_size > 0);
    test_expect_u(test, "a root that did not exist at construction has no final path", 0, _svc_late.root_final_size);

    char            log_buffer[2048]    = DEFAULT_INITIALIZATION;
    LogLevel const  previous_level      = log_get_level();

    log_set_level(LOG_LEVEL_WARN);
    log_set_buffer(log_buffer, sizeof(log_buffer));
    log_buffer_clear();

    /* Never dereferenced: a refusal returns before serve_1 touches it (see above). */
    HTTP_Server_Response *const unused_response = (HTTP_Server_Response*) (void*) 1;
    char const *const           late_path       = "/late/late.txt";

    test_expect_false(test, "the late root's (now existing) file is refused on this thread", http_service_static_serve_1(&_svc_late, unused_response, late_path, char_length(late_path)));
    test_expect_true(test, "the first refusal WARNs naming the unresolved root", char_find_exists_1(log_buffer, "root_dir '" _FIXTURE_LATE_ROOT "'"));
    test_expect_true(test, "and does not blame a link that does not exist", !char_find_exists_1(log_buffer, "link refused"));

    log_buffer_clear();

    test_expect_false(test, "a second request is refused too", http_service_static_serve_1(&_svc_late, unused_response, late_path, char_length(late_path)));
    test_expect_u(test, "and logs nothing - the cause was named once, not per request", 0, log_get_buffer_size());

    log_set_buffer(nullptr, 0);
    log_set_level(previous_level);

    _Reply reply = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "late-root round trip", _client_round_trip(port, "GET /late/late.txt HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "the late root's (now existing) file is refused - fail closed -> 404", 404, reply.status);
    }

    test_case_end(test);
}

#ifdef _WIN32
/*
 * T6 - the 8.3 alias of a dotfile, asked for with refuse_symlinks on (/static) AND off (/follow):
 * 404 both ways. The lexical rule refuses it before the open either way; under refuse_symlinks
 * the final-path compare would refuse it too (the handle's final path is the LONG name), so the
 * refusal does not rest on one mechanism. GetShortPathNameA asks the volume for the real alias;
 * a volume that makes no short names (`fsutil 8dot3name query` needs elevation here, so the
 * volume is asked directly) answers the long name back, and the hand-spelled HTACCE~1 is pinned.
 */
static void _test_short_name_under_compare(Test *const test, U16 const port) {
    test_case_begin(test, "T6: an 8.3 alias (HTACCE~1 for .htaccess) is refused with refuse_symlinks on AND off");

    char            short_path[512] = DEFAULT_INITIALIZATION;
    DWORD   const   short_size      = GetShortPathNameA(_FIXTURE_ROOT "\\.htaccess", short_path, (DWORD) sizeof(short_path));
    char    const   *short_name     = "HTACCE~1";
    bool            live_alias      = false;

    if (short_size > 0 && short_size < sizeof(short_path)) {
        USize base_start = 0;

        for (USize index = short_size; index > 0; index -= 1) {
            if (short_path[index - 1] == '\\' || short_path[index - 1] == '/') {
                base_start = index;

                break;
            }
        }

        if (!char_compare_equal_1(short_path + base_start, ".htaccess")) {
            short_name = short_path + base_start;
            live_alias = true;
        }
    }

    if (!live_alias) {
        _skip_loudly("T6 live 8.3 alias - this volume answered no short name for .htaccess; pinning the hand-spelled HTACCE~1 (lexical refusal) instead.");
    }

    _Reply  reply           = DEFAULT_INITIALIZATION;
    char    request[512]    = DEFAULT_INITIALIZATION;

    snprintf(request, sizeof(request), "GET /static/%s HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", short_name);

    if (test_expect_true(test, "T6 on round trip", _client_round_trip(port, request, &reply))) {
        test_expect_u(test, "T6: the 8.3 alias with refuse_symlinks on -> 404", 404, reply.status);
    }

    snprintf(request, sizeof(request), "GET /follow/%s HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", short_name);

    if (test_expect_true(test, "T6 off round trip", _client_round_trip(port, request, &reply))) {
        test_expect_u(test, "T6: the 8.3 alias with refuse_symlinks off -> 404 too", 404, reply.status);
    }

    test_case_end(test);
}
#endif // _WIN32

/*
 * T8 - a non-ASCII name. The URI's %C3%A9 decodes to the UTF-8 bytes of an acute e, and the wide
 * opener opens exactly the file those bytes name - where the 0.4.x ANSI path read them in the
 * process code page and 404'd or mis-targeted. Under refuse_symlinks (on here) the final-path
 * compare matches it byte for byte.
 */
static void _test_non_ascii_name(Test *const test, U16 const port) {
    test_case_begin(test, "T8: a non-ASCII asset name (caf%C3%A9.txt) is served with its bytes");

    _Reply reply = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "T8 round trip", _client_round_trip(port, "GET /static/caf%C3%A9.txt HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "T8: the non-ASCII name is served -> 200", 200, reply.status);
        test_expect_string(test, "T8: ... with its own bytes", "non-ascii-name", reply.body);
    }

    test_case_end(test);
}

/*
 * T9 - a slow reader. The client reads the header block, stalls 2 s while the 3 MiB body backs up
 * into the server's socket (the seam's choked-wait path), then reads the rest: every byte must
 * arrive, in order. lws's per-chunk content timeout is far longer than the stall, so this pins
 * that the choked wait RESUMES, not how much memory it holds.
 */
static void _test_slow_reader(Test *const test, U16 const port) {
    test_case_begin(test, "T9: a reader that stalls 2 s after the headers still gets every byte of a 3 MiB body");

    Net_Socket socket = DEFAULT_INITIALIZATION;

    if (!test_expect_true(test, "T9 connection opens", _client_open(port, &socket))) {
        test_case_end(test);

        return;
    }

    _Reply      reply   = DEFAULT_INITIALIZATION;
    bool const  sent    = _client_write(socket, "GET /static/big.bin HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n",
                                        CHAR_STATIC_SIZE("GET /static/big.bin HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n"));

    if (test_expect_true(test, "T9 stalled round trip", sent && _client_read_stalled(socket, &reply, _CLIENT_STALL_MS))) {
        test_expect_u(test, "T9: status 200", 200, reply.status);
        test_expect_true(test, "T9: the whole declared body arrived after the stall", reply.complete);
        test_expect_u(test, "T9: every byte", _BIG_FILE_SIZE, reply.body_size);
        test_expect_true(test, "T9: byte-exact", reply.body_size == _BIG_FILE_SIZE && char_compare_equal_2(reply.body, reply.body_size, (char const*) _big_file_content, _BIG_FILE_SIZE));
    }

    net_socket_close(socket);

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
        _skip_loudly("live 8.3 alias - this volume answered no short name for .env; pinning a hand-spelled ENV~1.");
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

    /* refuse_symlinks is left at its default - ON since 0.5.0, pinned by
     * _test_symlink_default_and_late_root - rather than set here. */
    _svc_default = http_service_static_init_1(_FIXTURE_ROOT, "/static");

    _svc_spa = http_service_static_init_2(_FIXTURE_SPA_ROOT, "/spa", "index.html", true, 0);

    /* max_age > 0 (report Low 6): only this service exercises the Cache-Control path on the
     * 206 and 304 branches - _svc_default and _svc_spa both pass max_age 0. */
    _svc_cached = http_service_static_init_2(_FIXTURE_ROOT, "/cached", nullptr, false, 60);

    /* The same root as _svc_default, with the dotfile knob on - _svc_default keeps it off, so the
     * pair pins both directions against one fixture. */
    _svc_dotfiles = http_service_static_init_1(_FIXTURE_ROOT, "/dot");
    _svc_dotfiles.refuse_dotfiles = true;

    /* The same root again with the link refusal turned OFF - the documented opt-out - so T4/T5
     * pin both directions against one fixture. */
    _svc_follow = http_service_static_init_1(_FIXTURE_ROOT, "/follow");
    _svc_follow.refuse_symlinks = false;

    /* A root that does NOT exist at construction (its final path cannot be taken - one WARN),
     * created only afterwards: refuse_symlinks must keep refusing it (fail closed). */
    _svc_late = http_service_static_init_1(_FIXTURE_LATE_ROOT, "/late");

    dir_create_all_1(_FIXTURE_LATE_ROOT);
    _fixture_write(_FIXTURE_LATE_ROOT "/late.txt", "late");

    /* The swap hooks act only when a case arms them (_swap.armed), so installing them once for the
     * whole run leaves every other case untouched. */
    http_service_static_test_hook_after_resolve_set(_swap_hook_after_resolve);
    http_service_static_test_hook_after_open_set(_swap_hook_after_open);

    Test test = test_init("tests/http/service/static/test_all.c");

    test_suite_begin(&test, "http_service_static (live server)");

    HTTP_Server *server = http_server_new();
    U16 const    port   = _start(&test, server);

    _test_conditional_get(&test, port);
    _test_range(&test, port);
    _test_if_range(&test, port);
    _test_cached_headers(&test, port);
    _test_range_large_whole(&test, port);
    _test_head_range(&test, port);
    _test_head_full_file(&test, port);
    _test_mimetypes(&test, port);
    _test_default_file_subdirectory(&test, port);
    _test_spa_fallback(&test, port);
    _test_symlink_refusal(&test, port);
    _test_swap_before_open_file(&test, port);
    _test_swap_before_open_ancestor(&test, port);
    _test_swap_after_open(&test, port);
    _test_link_in_place(&test, port);
    _test_junction_in_place(&test, port);
    _test_symlink_default_and_late_root(&test, port);
    _test_non_ascii_name(&test, port);
    _test_slow_reader(&test, port);
    _test_dotfiles(&test, port);
    _test_tilde_name_served(&test, port);
#ifdef _WIN32
    _test_ntfs_spellings_refused(&test, port);
    _test_reserved_windows_name_refused(&test, port);
    _test_reserved_windows_name_spellings_refused(&test, port);
    _test_short_name_refused(&test, port);
    _test_short_name_under_compare(&test, port);
#endif // _WIN32

    http_server_delete(&server);

    test_suite_end(&test);

    I32 const result = test_uninit(&test);

    _fixture_teardown();

    return result;
}