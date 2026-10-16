/*
 * static.c - HTTP static file service implementation
 */

#include <http/service/static/static.h>

// libwebsockets.h is owed directly: lws_get_mimetype, lws_hdr_copy, WSI_TOKEN_* and struct lws are
// named here, and a third-party header a CFW module re-exports is included like any other.
#include <libwebsockets.h>
// stdatomic.h is owed directly too: the "cannot tell" once-flags in
// _http_service_static_handle_below_root are atomic_bool, and log.h reaches it only under
// LOG_THREAD_IMPLEMENTATION.
#include <stdatomic.h>

// The OS error state a refused nofollow open leaves behind (_http_service_static_link_refused):
// GetLastError / ERROR_STOPPED_ON_SYMLINK on Windows, errno / ELOOP elsewhere.
#ifdef _WIN32
#include <platform/windows/windows.h>
#endif // _WIN32

// char.h is singled out because it declares no TYPES at all: no header's API can ever name one,
// so every chain that happens to reach it is accidental and a file using char_* owes it directly.
#include <char/char.h>
#include <datetime/datetime.h>
#include <file/file.h>
#include <http/headers/headers.h>
// Compression stays OUT of static.h: there it dragged an unpublished module into published
// static's dependency closure and broke the public export.
#include <http/service/compression/compression.h>
#include <log/log.h>
#include <memory/memory.h>

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/* One row of the extension-to-MIME lookup table. */
typedef struct {
    char const  *extension;
    USize       extension_size;
    char const  *mimetype;
    /* Whether to append "; charset=utf-8" - true for every text-family entry. Kept as an
     * explicit flag rather than a runtime "starts with text/" check: cheaper, and it leaves each
     * row's answer visible in the table. The JSON-family rows (application/json,
     * application/manifest+json) are textual but say FALSE deliberately - RFC 8259 fixes their
     * encoding at UTF-8 and defines no charset parameter, so sending one is at best ignored. */
    bool        charset;
} _HTTP_Service_Static_Mime;

/*
 * Conditional GET / Range header VALUES, already extracted by the caller into NUL-terminated
 * stack buffers - serve_2 via the clean http_server_request_header_copy API, serve_1 (no
 * HTTP_Server_Request available) via the legacy wsi path. A zero *_size means the header was
 * absent. Shared so the actual conditional-GET/Range/If-Range decision logic
 * (_http_service_static_serve_conditional) lives in exactly one place regardless of which entry
 * point read the headers.
 */
typedef struct {
    char const  *if_none_match;
    USize       if_none_match_size;
    char const  *if_modified_since;
    USize       if_modified_since_size;
    char const  *range;
    USize       range_size;
    char const  *if_range;
    USize       if_range_size;
} _HTTP_Service_Static_Conditional_Headers;

/*
 * Where a served handle lives, as _http_service_static_handle_below_root answers it under
 * refuse_symlinks. Only BELOW_ROOT is served. ELSEWHERE is a fact about THIS request - a link on
 * the way, or a second spelling - and is logged per request; UNKNOWN means the check could not
 * run at all (no root_final, or a handle whose path cannot be read), a fact about the instance or
 * the platform, so its cause is logged once per process and the request is refused all the same.
 */
typedef enum {
    _HTTP_SERVICE_STATIC_PLACEMENT_BELOW_ROOT,
    _HTTP_SERVICE_STATIC_PLACEMENT_ELSEWHERE,
    _HTTP_SERVICE_STATIC_PLACEMENT_UNKNOWN
} _HTTP_Service_Static_Placement;

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

/* Switch for the compression seam _http_service_static_send passes buffer sends through - only
 * the bodiless 304 now; every 200 and 206 is streamed from the handle and never reaches it (see
 * that function's comment). Off until static grows per-instance compression configuration. */
static bool const _HTTP_SERVICE_STATIC_COMPRESSION_ENABLED = false;
/* Buffer size for the rendered "<mimetype>; charset=utf-8" Content-Type value. The longest
 * mimetype in the table below is well under half of this. */
#define _HTTP_SERVICE_STATIC_CONTENT_TYPE_MAX 128

/*
 * Extension-to-MIME map, matched by file-name suffix, CASE-INSENSITIVELY - a camera's
 * .JPG/.PNG uploads otherwise fall through to octet-stream, and the security service's
 * nosniff header then blocks a .JS asset served that way. Anything not listed falls back to
 * lws_get_mimetype and then application/octet-stream. Ordered by category for readability.
 * Video/audio types matter for streaming: browsers (notably Safari/iOS) reject media served as
 * application/octet-stream.
 *
 * .ts is video/mp2t (MPEG transport stream) here, NOT TypeScript source - this
 * table serves compiled web output, and a project that also serves raw .ts sources over this
 * same route needs its own mapping ahead of this table.
 */
static _HTTP_Service_Static_Mime const _HTTP_SERVICE_STATIC_MIME_TABLE[] = {
    { ".css",           CHAR_STATIC_SIZE(".css"),           "text/css",                     true },
    { ".csv",           CHAR_STATIC_SIZE(".csv"),           "text/csv",                     true },
    { ".htm",           CHAR_STATIC_SIZE(".htm"),           "text/html",                    true },
    { ".html",          CHAR_STATIC_SIZE(".html"),          "text/html",                    true },
    { ".js",            CHAR_STATIC_SIZE(".js"),            "text/javascript",              true },
    { ".json",          CHAR_STATIC_SIZE(".json"),          "application/json",             false },
    { ".md",            CHAR_STATIC_SIZE(".md"),            "text/markdown",                true },
    { ".mjs",           CHAR_STATIC_SIZE(".mjs"),           "text/javascript",              true },
    { ".pdf",           CHAR_STATIC_SIZE(".pdf"),           "application/pdf",              false },
    { ".txt",           CHAR_STATIC_SIZE(".txt"),           "text/plain",                   true },
    { ".vtt",           CHAR_STATIC_SIZE(".vtt"),           "text/vtt",                     true },
    { ".wasm",          CHAR_STATIC_SIZE(".wasm"),          "application/wasm",             false },
    { ".webmanifest",   CHAR_STATIC_SIZE(".webmanifest"),   "application/manifest+json",    false },
    { ".xml",           CHAR_STATIC_SIZE(".xml"),           "application/xml",              false },
    { ".apng",          CHAR_STATIC_SIZE(".apng"),          "image/apng",                   false },
    { ".avif",          CHAR_STATIC_SIZE(".avif"),          "image/avif",                   false },
    { ".bmp",           CHAR_STATIC_SIZE(".bmp"),           "image/bmp",                    false },
    { ".gif",           CHAR_STATIC_SIZE(".gif"),           "image/gif",                    false },
    { ".ico",           CHAR_STATIC_SIZE(".ico"),           "image/x-icon",                 false },
    { ".jpeg",          CHAR_STATIC_SIZE(".jpeg"),          "image/jpeg",                   false },
    { ".jpg",           CHAR_STATIC_SIZE(".jpg"),           "image/jpeg",                   false },
    { ".png",           CHAR_STATIC_SIZE(".png"),           "image/png",                    false },
    { ".svg",           CHAR_STATIC_SIZE(".svg"),           "image/svg+xml",                false },
    { ".webp",          CHAR_STATIC_SIZE(".webp"),          "image/webp",                   false },
    { ".otf",           CHAR_STATIC_SIZE(".otf"),           "font/otf",                     false },
    { ".ttf",           CHAR_STATIC_SIZE(".ttf"),           "font/ttf",                     false },
    { ".woff",          CHAR_STATIC_SIZE(".woff"),          "font/woff",                    false },
    { ".woff2",         CHAR_STATIC_SIZE(".woff2"),         "font/woff2",                   false },
    { ".m3u8",          CHAR_STATIC_SIZE(".m3u8"),          "application/vnd.apple.mpegurl", false },
    { ".m4v",           CHAR_STATIC_SIZE(".m4v"),           "video/x-m4v",                  false },
    { ".mkv",           CHAR_STATIC_SIZE(".mkv"),           "video/x-matroska",             false },
    { ".mov",           CHAR_STATIC_SIZE(".mov"),           "video/quicktime",              false },
    { ".mp4",           CHAR_STATIC_SIZE(".mp4"),           "video/mp4",                    false },
    { ".mpd",           CHAR_STATIC_SIZE(".mpd"),           "application/dash+xml",         false },
    { ".ogv",           CHAR_STATIC_SIZE(".ogv"),           "video/ogg",                    false },
    { ".ts",            CHAR_STATIC_SIZE(".ts"),            "video/mp2t",                   false },
    { ".webm",          CHAR_STATIC_SIZE(".webm"),          "video/webm",                   false },
    { ".aac",           CHAR_STATIC_SIZE(".aac"),           "audio/aac",                    false },
    { ".flac",          CHAR_STATIC_SIZE(".flac"),          "audio/flac",                   false },
    { ".m4a",           CHAR_STATIC_SIZE(".m4a"),           "audio/mp4",                    false },
    { ".mp3",           CHAR_STATIC_SIZE(".mp3"),           "audio/mpeg",                   false },
    { ".oga",           CHAR_STATIC_SIZE(".oga"),           "audio/ogg",                    false },
    { ".ogg",           CHAR_STATIC_SIZE(".ogg"),           "audio/ogg",                    false },
    { ".opus",          CHAR_STATIC_SIZE(".opus"),          "audio/opus",                   false },
    { ".wav",           CHAR_STATIC_SIZE(".wav"),           "audio/wav",                    false },
    { ".zip",           CHAR_STATIC_SIZE(".zip"),           "application/zip",              false },
    { ".gz",            CHAR_STATIC_SIZE(".gz"),            "application/gzip",             false }
};

/* Bytes in every on-disk path buffer this file builds: the resolved file name (root_dir + '/' +
 * the request-relative path, each bounded by HTTP_SERVER_PATH_MAX_LENGTH), the handle's final
 * path, and the expected final path it is compared against. The same size as root_final in
 * static.h, so a root_final that fit always leaves room for the comparison. */
#define _HTTP_SERVICE_STATIC_PATH_BUFFER_MAX HTTP_SERVICE_STATIC_ROOT_FINAL_MAX_LENGTH
/* Maximum digits accepted per Range number; the I64/ISize digit count keeps
 * accumulation below 2^64 (no wrap) and stays cross-checked by types.h. */
#define _HTTP_SERVICE_STATIC_RANGE_DIGITS_MAX ISIZE_DIGITS_MAX
/* Buffer size for the Range request header and the Content-Range response header. Also used for
 * If-None-Match/If-Range: an ETag here is short (size+mtime in hex), but a client may quote
 * a longer value than we generated - this stays generous rather than truncating a real one into
 * a false mismatch. NOTE: lws_hdr_copy returns -1 (not clamped) for a header longer
 * than the buffer, and every call site below treats that exactly like "header absent" - a client
 * sending an oversize conditional/Range header silently gets the unconditional response rather
 * than a 400. Documented, not changed: the safe direction for a value we cannot fully read. */
#define _HTTP_SERVICE_STATIC_RANGE_HEADER_MAX 256
/* The separator file_final_path_* answers in, and so the one the expected final path is joined
 * with. */
#ifdef _WIN32
#define _HTTP_SERVICE_STATIC_SEPARATOR '\\'
#else
#define _HTTP_SERVICE_STATIC_SEPARATOR '/'
#endif // _WIN32

#ifdef HTTP_SERVICE_STATIC_TEST_HOOKS
/*==============================================================================
 * MARK: - Test Hooks
 *============================================================================*/

/* Test build only: called with the resolved file_name right after the lexical resolve, before
 * the file is opened - the window a swapped-in link has to land in to be served. */
static FpHTTP_Service_Static_Test_Hook _http_service_static_hook_after_resolve = nullptr;
/* Test build only: called with the resolved file_name after the file is opened and verified,
 * before the body is handed to the sender - a swap here must change nothing the client gets. */
static FpHTTP_Service_Static_Test_Hook _http_service_static_hook_after_open = nullptr;
#endif // HTTP_SERVICE_STATIC_TEST_HOOKS

/*==============================================================================
 * MARK: - Helpers
 *============================================================================*/

/*
 * Resolve a MIME type from a file name by suffix, case-insensitively. Falls back to
 * lws_get_mimetype, then application/octet-stream. Never returns null. *out_charset is set true
 * when the caller should append "; charset=utf-8".
 */
static char const* _http_service_static_mimetype(char const *const file_name, USize const file_name_size, bool *const out_charset) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "file_name", (void*) file_name);
    error_check_null(LOG_METADATA, "out_charset", (void*) out_charset);

    char    const   *mimetype   = nullptr;
    USize   const   count       = sizeof(_HTTP_SERVICE_STATIC_MIME_TABLE) / sizeof(_HTTP_SERVICE_STATIC_MIME_TABLE[0]);

    *out_charset = false;

    for (USize i = 0; i < count; i += 1) {
        USize const extension_size = _HTTP_SERVICE_STATIC_MIME_TABLE[i].extension_size;

        if (file_name_size >= extension_size &&
            char_compare_iequal_2(file_name + file_name_size - extension_size, extension_size, _HTTP_SERVICE_STATIC_MIME_TABLE[i].extension, extension_size)) {
            mimetype        = _HTTP_SERVICE_STATIC_MIME_TABLE[i].mimetype;
            *out_charset    = _HTTP_SERVICE_STATIC_MIME_TABLE[i].charset;

            break;
        }
    }

    if (mimetype == nullptr) {
        mimetype = lws_get_mimetype(file_name, nullptr);
    }

    if (mimetype == nullptr) {
        mimetype = "application/octet-stream";
    }

    trace_log_pop();

    return mimetype;
}

/*
 * Consume a run of at most _HTTP_SERVICE_STATIC_RANGE_DIGITS_MAX ASCII digits at
 * *cursor into *out (unsigned), advancing *cursor past them. Returns the number
 * of digits consumed; 0 means none. The digit cap bounds the accumulation so it
 * cannot be driven to wrap by an over-long value.
 */
static USize _http_service_static_range_digits(char const **const cursor, USize *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "cursor", (void*) cursor);
    error_check_null(LOG_METADATA, "out", (void*) out);

    USize value = 0;
    USize count = 0;

    while (**cursor >= '0' && **cursor <= '9' && count < _HTTP_SERVICE_STATIC_RANGE_DIGITS_MAX) {
        value = value * 10 + (USize) (**cursor - '0');
        *cursor += 1;
        count += 1;
    }

    *out = value;

    trace_log_pop();

    return count;
}

/*
 * Parse a single-range request value ("bytes=start-end"). Supports "start-end",
 * "start-" (to end of file), and "-suffix" (final suffix bytes). On success sets
 * out_start and out_end (inclusive), clamped to [0, file_size-1], and returns true.
 *
 * Returns false for multi-range or malformed (including trailing garbage) input, in which case
 * the caller should IGNORE Range and fall back to a full 200 (RFC 9110 permits a server to
 * ignore Range at any time). Also returns false, but with *out_unsatisfiable set true, for a
 * syntactically valid range that names no existing byte - "-0" (a zero-byte suffix, folded in
 * here) or a start at/past EOF - in which case the caller must answer 416 rather
 * than silently falling back to a full response.
 */
static bool _http_service_static_range_parse(char const *const range, USize const file_size, USize *const out_start, USize *const out_end, bool *const out_unsatisfiable) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "range", (void*) range);
    error_check_null(LOG_METADATA, "out_start", (void*) out_start);
    error_check_null(LOG_METADATA, "out_end", (void*) out_end);
    error_check_null(LOG_METADATA, "out_unsatisfiable", (void*) out_unsatisfiable);

    *out_unsatisfiable = false;

    if (file_size == 0                                                                                 ||
        !char_compare_equal_2(range, CHAR_STATIC_SIZE("bytes="), "bytes=", CHAR_STATIC_SIZE("bytes=")) ||
        char_find_exists_1(range, ",")) {
        trace_log_pop();

        return false;
    }

    char    const   *cursor     = range + CHAR_STATIC_SIZE("bytes=");
    USize           start       = 0;
    USize           end         = 0;
    bool            result      = false;

    if (*cursor == '-') {
        cursor += 1;

        USize suffix = 0;

        if (_http_service_static_range_digits(&cursor, &suffix) > 0 && *cursor == '\0') {
            if (suffix == 0) {
                *out_unsatisfiable = true;

                trace_log_pop();

                return false;
            }

            start   = suffix >= file_size ? 0 : file_size - suffix;
            end     = file_size - 1;
            result  = true;
        }
    }
    else if (_http_service_static_range_digits(&cursor, &start) > 0 && *cursor == '-') {
        cursor += 1;

        bool parsed = false;

        if (*cursor == '\0') {
            end     = file_size - 1;
            parsed  = true;
        }
        else if (_http_service_static_range_digits(&cursor, &end) > 0 && *cursor == '\0') {
            parsed  = true;
        }

        if (parsed) {
            if (end >= file_size) {
                end = file_size - 1;
            }

            if (start >= file_size || start > end) {
                *out_unsatisfiable = true;

                trace_log_pop();

                return false;
            }

            result = true;
        }
    }

    if (result) {
        *out_start  = start;
        *out_end    = end;
    }

    trace_log_pop();

    return result;
}

/*
 * Render a STRONG ETag from file size and mtime - "<size-hex>-<mtime-hex>", quoted, no W/
 * prefix. Cheap (no file content is read) and changes whenever either input changes, which is
 * what a static-file server needs: it does not survive a byte-identical rewrite with a preserved
 * mtime, but no caller here relies on that finer guarantee. Strong, as nginx and Apache emit the
 * same size+mtime pair, because If-Range uses the strong comparison (RFC 9110 S13.1.5) - a weak
 * tag can never match there, and Chromium resumes a partial download only against a strong
 * validator. The one-second mtime granularity is the same accepted risk either way.
 */
static USize _http_service_static_etag(USize const file_size, I64 const mtime, char *const buffer, USize const capacity) {
    trace_log_push(LOG_METADATA);

    USize const size = (USize) snprintf(buffer, capacity, "\"%llx-%llx\"", (unsigned long long) file_size, (unsigned long long) mtime);

    trace_log_pop();

    return size;
}

/*
 * Whether an If-None-Match VALUE matches `etag`, by the WEAK comparison RFC 9110 S13.1.2 requires
 * there (unlike If-Range's strong one): "*" matches any existing representation, and otherwise the
 * value is a comma-separated list whose members match when their opaque-tags are equal once a
 * leading "W/" is stripped from each side. Every member is trimmed of optional whitespace (space,
 * tab). The scan is bounded by `value_size` - the value is request data, so a malformed one is
 * simply "no match", never an error_check. The caller only asks this for a file it has stat'ed.
 */
static bool _http_service_static_if_none_match(char const *const value, USize const value_size, char const *const etag, USize const etag_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "value", (void*) value);
    error_check_null(LOG_METADATA, "etag", (void*) etag);

    char    const   *tag        = etag;
    USize           tag_size    = etag_size;

    if (tag_size >= CHAR_STATIC_SIZE("W/") && tag[0] == 'W' && tag[1] == '/') {
        tag         += CHAR_STATIC_SIZE("W/");
        tag_size    -= CHAR_STATIC_SIZE("W/");
    }

    USize cursor = 0;

    while (cursor < value_size) {
        USize member_start  = cursor;
        USize member_end    = cursor;

        while (member_end < value_size && value[member_end] != ',') {
            member_end += 1;
        }

        cursor = member_end + 1;

        while (member_start < member_end && (value[member_start] == ' ' || value[member_start] == '\t')) {
            member_start += 1;
        }

        while (member_end > member_start && (value[member_end - 1] == ' ' || value[member_end - 1] == '\t')) {
            member_end -= 1;
        }

        if (member_end - member_start >= CHAR_STATIC_SIZE("W/") && value[member_start] == 'W' && value[member_start + 1] == '/') {
            member_start += CHAR_STATIC_SIZE("W/");
        }

        char    const *const    member      = value + member_start;
        USize   const           member_size = member_end - member_start;

        if ((member_size == 1 && member[0] == '*') || (tag_size > 0 && char_compare_equal_2(member, member_size, tag, tag_size))) {
            trace_log_pop();

            return true;
        }
    }

    trace_log_pop();

    return false;
}

/* RFC 7231 "HTTP-date" (IMF-fixdate) rendering, e.g. "Wed, 21 Oct 2015 07:28:00 GMT". Datetime's
 * generic datetime_format has no weekday token, so this reads day_week/month/date/etc directly
 * off the struct rather than going through it - self-contained, no locale dependency. This is
 * the RENDERING half; the parsing half an If-Modified-Since needs is datetime_from_http_try,
 * which backs the 304 in _http_service_static_serve_conditional (serve_2 only - serve_1 never
 * reads the header at all, see static.h). */
static USize _http_service_static_http_date(I64 const epoch_seconds, char *const buffer, USize const capacity) {
    trace_log_push(LOG_METADATA);

    static char const *const weekday[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static char const *const month[]   = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

    Datetime const dt = datetime_init_2((USize) (epoch_seconds > 0 ? epoch_seconds : 0));

    USize const size = (USize) snprintf(buffer, capacity, "%s, %02d %s %04d %02d:%02d:%02d GMT",
        weekday[dt.day_week % 7], dt.date, month[dt.month % 12], dt.year, dt.hours, dt.minutes, dt.seconds);

    trace_log_pop();

    return size;
}

/*
 * Whether any component of the request-relative path `rel_path` starts with '.', for the opt-in
 * self->refuse_dotfiles check - `.git`, `.env`, `.htaccess`, `.vscode/` and the like, at any
 * depth. Exactly `.well-known` is exempt: it is the one dot-directory the web platform mandates
 * (ACME challenges, assetlinks.json, security.txt). Both '/' and '\\' separate components, since
 * Windows opens either. Pure path arithmetic - no file-system access.
 */
static bool _http_service_static_has_dotfile(char const *const rel_path, USize const rel_path_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "rel_path", (void*) rel_path);

    USize cursor = 0;

    while (cursor < rel_path_size) {
        USize const segment_start = cursor;

        while (cursor < rel_path_size && rel_path[cursor] != '/' && rel_path[cursor] != '\\') {
            cursor += 1;
        }

        USize const segment_size = cursor - segment_start;

        if (segment_size > 0                                                                                          &&
            rel_path[segment_start] == '.'                                                                            &&
            !char_compare_equal_2(rel_path + segment_start, segment_size, ".well-known", CHAR_STATIC_SIZE(".well-known"))) {
            trace_log_pop();

            return true;
        }

        cursor += 1;
    }

    trace_log_pop();

    return false;
}

/*
 * Queue "Cache-Control: max-age=<max_age>" on `response` as a header_add VALUE.
 *
 * http_headers_cache_max_age_into renders a whole "Cache-Control: ...\r\n" LINE; header_add wants
 * the value alone, so the prefix and the trailing CRLF are trimmed here. Shared by the 200, 206
 * and 304 paths so all three carry the same freshness: a 304 that omits Cache-Control does not
 * renew the stored entry's freshness, so a client revalidates on every load once the original
 * max-age has lapsed.
 */
static void _http_service_static_cache_control_add(HTTP_Server_Response *const response, U32 const max_age) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "response", (void*) response);

    if (max_age == 0) {
        trace_log_pop();

        return;
    }

    char cache_control[64] = DEFAULT_INITIALIZATION;

    http_headers_cache_max_age_into(cache_control, sizeof(cache_control), (USize) max_age);

    char    const *const prefix         = "Cache-Control: ";
    USize   const        prefix_size    = CHAR_STATIC_SIZE("Cache-Control: ");
    USize                value_size     = char_length(cache_control);

    if (value_size < prefix_size || !char_compare_equal_2(cache_control, prefix_size, prefix, prefix_size)) {
        trace_log_pop();

        return;
    }

    char *const value = cache_control + prefix_size;

    value_size -= prefix_size;

    while (value_size > 0 && (value[value_size - 1] == '\r' || value[value_size - 1] == '\n')) {
        value_size -= 1;
    }

    value[value_size] = '\0';

    http_server_response_header_add(response, "Cache-Control", value);

    trace_log_pop();
}

/*
 * Render the Content-Type VALUE a reply should carry: the mimetype, plus "; charset=utf-8" for
 * the text-family entries the MIME table flags.
 *
 * Content-Type is a singleton field (RFC 9110 S5.5): queueing charset as a second "Content-Type"
 * header alongside the one the sender emits from its content_type argument would send two -
 * which of the two a client honors is implementation-defined and an intermediary may reject the
 * reply outright. Rendering the full value here and handing it to the sender as THE content type
 * leaves exactly one on the wire.
 */
static void _http_service_static_content_type(char const *const mimetype, bool const charset, char *const buffer, USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mimetype", (void*) mimetype);
    error_check_null(LOG_METADATA, "buffer", (void*) buffer);

    snprintf(buffer, capacity, charset ? "%s; charset=utf-8" : "%s", mimetype);

    trace_log_pop();
}

/*
 * Every BUFFER send this file makes goes through the compression seam, and the seam is switched
 * OFF (_HTTP_SERVICE_STATIC_COMPRESSION_ENABLED). With the flag false this is exactly
 * http_server_response_send_2 - same bytes, same headers, same suite counts. Only the 304 passes
 * through here now, and it has no body: every 200 and 206 is STREAMED from the open handle by
 * http_server_response_send_file_handle, which never calls this function. Compressing a served
 * file therefore means compressing a streamed body, never a buffer: the adjacent for that day is
 * libwebsockets' lws_http_compression_apply, which wraps exactly the LWS_WRITE_HTTP ...
 * LWS_WRITE_HTTP_FINAL sequence the streaming seam writes (a 206 stays uncompressed either way -
 * its Content-Range promises a byte window of the identity body), or precompressed sidecars
 * served alongside the original. Neither is done here. The seam stays off until static grows real
 * per-instance compression configuration: a service built per call would re-add its MIME defaults
 * on every response, and static.h cannot gain a compression field without a public API change.
 *
 * `request` may be null (serve_1 has none), in which case there is no Accept-Encoding to read
 * and the body goes out as identity whatever the flag says.
 */
static void _http_service_static_send(HTTP_Server_Request *const request, HTTP_Server_Response *const response, Byte const *const data,
    USize const data_size, char const *const content_type, U16 const status_code) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "response", (void*) response);
    error_check_null(LOG_METADATA, "content_type", (void*) content_type);

    if (_HTTP_SERVICE_STATIC_COMPRESSION_ENABLED && request != nullptr) {
        HTTP_Service_Compression compression = DEFAULT_INITIALIZATION;

        if (http_service_compression_init_1(&compression)) {
            http_service_compression_send(&compression, request, response, data, data_size, content_type, status_code);
            http_service_compression_uninit(&compression);

            trace_log_pop();

            return;
        }
    }

    http_server_response_send_2(response, data, data_size, content_type, status_code);

    trace_log_pop();
}

/*
 * Resolve an incoming request path to an on-disk file name, LEXICALLY - honoring route_prefix,
 * the directory default_file, and (opt-in) dotfile refusal, and refusing on Windows the second
 * spellings and device names whose OPEN would itself be the hazard. Nothing here touches the file
 * system: whether the name exists, is a link, or lives below the root is answered by the open
 * (_http_service_static_open), on the handle that is then served. Returns false when nothing
 * should be served; on true, file_name holds root_dir + '/' + the request-relative path,
 * *out_relative_start is where that relative part begins in it, and *out_fallback_allowed says
 * whether a failed open may fall back to the SPA shell (fallback on, an extension-less final
 * segment, not a directory request).
 */
static bool _http_service_static_resolve(HTTP_Service_Static const *const self,
    char const *const path, USize const path_size, char *const file_name, USize const file_name_capacity, USize *const out_relative_start, bool *const out_fallback_allowed) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "path", (void*) path);
    error_check_null(LOG_METADATA, "file_name", (void*) file_name);
    error_check_null(LOG_METADATA, "out_relative_start", (void*) out_relative_start);
    error_check_null(LOG_METADATA, "out_fallback_allowed", (void*) out_fallback_allowed);

    /* No error_check_non_value_uint on path_size: it is request-derived, and
     * aborting on zero would kill the process four lines above the branch that
     * already handles it gracefully — the same dead-guard shape as the
     * file_open_1 abort removed from the Range path. An empty relative path is a
     * "not served", not a programming error. */
    if (path_size == 0 || path_size >= HTTP_SERVER_PATH_MAX_LENGTH) {
        trace_log_pop();

        return false;
    }

    USize const prefix_len = self->route_prefix_size;

    if (path_size < prefix_len || !char_compare_equal_2(path, prefix_len, self->route_prefix, prefix_len)) {
        trace_log_pop();

        return false;
    }

    char    const   *rel_path       = path + prefix_len;
    USize           rel_path_size   = path_size - prefix_len;

    while (rel_path_size > 0 && rel_path[0] == '/') {
        rel_path        += 1;
        rel_path_size   -= 1;
    }

    /* A NUL byte inside the sized path (%00): every open below takes a C string and would stop
     * there, so the file opened is not the file the rest of the path names, and the lexical
     * refusals that read the sized buffer judge a different name than the open does. A data
     * value, so a refusal and never an error_check. */
    if (memchr(rel_path, '\0', rel_path_size) != nullptr) {
        trace_log_pop();

        return false;
    }

#ifdef _WIN32
    /* An NTFS alternate data stream (':'), an 8.3 short alias or a trailing-dot/space component
     * anywhere in the path - the second spellings every lexical refusal below would otherwise
     * compare against the wrong name. Policy here, fact in file/: the predicate is OS-independent,
     * the refusal is Windows-only because only Win32 rewrites these spellings. */
    if (file_path_aliased_windows(rel_path, rel_path_size)) {
        trace_log_pop();

        return false;
    }
#endif // _WIN32

    /* Opt-in: off by default, so zero cost unless a caller explicitly asks. */
    if (self->refuse_dotfiles && _http_service_static_has_dotfile(rel_path, rel_path_size)) {
        trace_log_pop();

        return false;
    }

    /* GET .../sub/ (a trailing slash on a non-root relative path) serves sub/index.html the same
     * way the prefix root does, so a subdirectory index needs no exact file name in the URL. */
    bool const directory_request = rel_path_size == 0 || rel_path[rel_path_size - 1] == '/';

    USize file_name_size = 0;

    char_copy_3(file_name, file_name_capacity, self->root_dir, self->root_dir_size);

    file_name_size += self->root_dir_size;

    if (file_name_size > 0 && file_name[file_name_size - 1] != '/' && file_name[file_name_size - 1] != '\\') {
        file_name[file_name_size]   = '/';
        file_name_size              += 1;
    }

    USize const relative_start = file_name_size;

    if (directory_request) {
        if (self->default_file_size == 0) {
            trace_log_pop();

            return false;
        }

        if (rel_path_size > 0) {
            /* Guarded on size: char_find_slice_5 aborts when the needle is longer
             * than the remaining haystack, so a one-byte relative path (GET /a) would
             * otherwise take the process down. A path that short cannot contain "..". */
            if (rel_path_size >= CHAR_STATIC_SIZE("..")
                && char_find_slice_5(rel_path, rel_path_size, 0, "..", CHAR_STATIC_SIZE("..")) != nullptr) {
                trace_log_pop();

                return false;
            }

            char_copy_3(file_name + file_name_size, file_name_capacity - file_name_size, rel_path, rel_path_size);

            file_name_size += rel_path_size;
        }

        char_copy_3(file_name + file_name_size, file_name_capacity - file_name_size, self->default_file, self->default_file_size);

        file_name_size += self->default_file_size;
        file_name[file_name_size] = '\0';
    }
    else {
        /* Guarded on size: char_find_slice_5 aborts when the needle is longer
         * than the remaining haystack, so a one-byte relative path (GET /a) would
         * otherwise take the process down. A path that short cannot contain "..". */
        if (rel_path_size >= CHAR_STATIC_SIZE("..")
            && char_find_slice_5(rel_path, rel_path_size, 0, "..", CHAR_STATIC_SIZE("..")) != nullptr) {
            trace_log_pop();

            return false;
        }

        char_copy_3(file_name + file_name_size, file_name_capacity - file_name_size, rel_path, rel_path_size);

        file_name_size += rel_path_size;
        file_name[file_name_size] = '\0';
    }

#ifdef _WIN32
    /* A Win32 device as the final component - request-chosen, or the config-sourced default_file
     * a directory request appends. Opening one is the hazard ("CONIN$ .txt" blocks this thread),
     * so the refusal precedes the open. */
    {
        char const *const   base_name   = file_basename_1(file_name);
        USize const         base_start  = (USize) (base_name - file_name);

        if (file_name_reserved_windows(file_name + base_start, file_name_size - base_start)) {
            trace_log_pop();

            return false;
        }
    }
#endif // _WIN32

    /* SPA fallback restricted to extension-less paths: falling back to index.html 200 with a
     * text/html body for a missing .js/.css asset makes browsers refuse to run/apply it and
     * report a MIME-type console error. A request with a dot in its
     * final segment is asking for a specific asset, not a client-side route - a dot in an
     * EARLIER segment ("/app/v1.2/users") is not an extension and must not disqualify the
     * fallback, so the search starts after the last '/', mirroring the basename scan above. */
    USize segment_start = 0;

    for (USize i = rel_path_size; i > 0; i -= 1) {
        if (rel_path[i - 1] == '/') {
            segment_start = i;

            break;
        }
    }

    bool const has_extension = !directory_request && !memory_empty(char_find_slice_5(rel_path, rel_path_size, segment_start, ".", CHAR_STATIC_SIZE(".")));

    *out_relative_start     = relative_start;
    *out_fallback_allowed   = self->spa_fallback && self->fallback_file_size > 0 && !has_extension;

#ifdef HTTP_SERVICE_STATIC_TEST_HOOKS
    if (_http_service_static_hook_after_resolve != nullptr) {
        _http_service_static_hook_after_resolve(file_name);
    }
#endif // HTTP_SERVICE_STATIC_TEST_HOOKS

    trace_log_pop();

    return true;
}

/* Whether `character` separates path components where the open reads them: '/' everywhere, and
 * '\\' too on Windows. */
static bool _http_service_static_is_separator(char const character) {
#ifdef _WIN32
    return character == '/' || character == '\\';
#else
    return character == '/';
#endif // _WIN32
}

/*
 * Whether the open that just answered nullptr was refused because its final component is a link -
 * read straight off the OS error state, which file_open_nofollow_try_1 leaves as the refusal's own
 * (ELOOP from O_NOFOLLOW, a symlink to a directory included; ERROR_STOPPED_ON_SYMLINK from the
 * reparse-tag check, which only a FILE link reaches - a final-component junction is a directory,
 * refused by the open itself with ERROR_ACCESS_DENIED, so it never reads as a link here). It must
 * run before anything that could overwrite that state, so it is not traced.
 */
static bool _http_service_static_link_refused(void) {
#ifdef _WIN32
    return GetLastError() == ERROR_STOPPED_ON_SYMLINK;
#else
    return errno == ELOOP;
#endif // _WIN32
}

/*
 * Whether `file` - the handle that will be served - physically lives at root_final + separator +
 * `relative`, the request-relative name AS RESOLVED (default_file appended on a directory request,
 * or the SPA fallback file). The handle's final path is where its bytes live, so a link anywhere
 * on the way - the final component or ANY ancestor, symlink or junction, swapped in before the
 * open or present all along - makes the two differ, and so does an 8.3, trailing-dot or link-case
 * spelling the lexical rules let through. Empty and "." components of `relative` are skipped, as
 * the open itself skips them, so a doubled slash is not a false refusal. Windows compares
 * case-insensitively, ASCII only (NTFS names are; a NON-ASCII case variant mismatches and is
 * refused - the fail-closed direction); POSIX compares exactly. Fails CLOSED: only BELOW_ROOT is
 * served. An expected path that does not fit answers ELSEWHERE. No root_final (it could not be
 * resolved at construction) or a handle whose final path cannot be read (Linux without /proc)
 * answers UNKNOWN, and logs its real cause ONCE per process - one flag per cause, process-wide
 * because self is const, and swapped with atomic_exchange so of two racing first requests exactly
 * one logs. Logging those per request would blame a link that does not exist, at a volume that
 * grows with the traffic. Every other instance's unresolved root was named by its own
 * construction WARN.
 */
static _HTTP_Service_Static_Placement _http_service_static_handle_below_root(HTTP_Service_Static const *const self, File *const file, char const *const relative, USize const relative_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "file", (void*) file);
    error_check_null(LOG_METADATA, "relative", (void*) relative);

    static atomic_bool _root_unresolved_warned  = false;
    static atomic_bool _handle_unnamed_warned   = false;

    if (self->root_final_size == 0) {
        if (!atomic_exchange_explicit(&_root_unresolved_warned, true, memory_order_relaxed)) {
            log_message_2(LOG_LEVEL_WARN, LOG_METADATA,
                "http_service_static: root_dir '%s' had no resolvable final path at construction, so refuse_symlinks cannot tell where a served"
                " file lives and refuses every request below it until the service is rebuilt over an existing root (logged once per process)",
                self->root_dir);
        }

        trace_log_pop();

        return _HTTP_SERVICE_STATIC_PLACEMENT_UNKNOWN;
    }

    char final_path[_HTTP_SERVICE_STATIC_PATH_BUFFER_MAX] = DEFAULT_INITIALIZATION;

    if (!file_get_final_path(file, final_path, sizeof(final_path))) {
        if (!atomic_exchange_explicit(&_handle_unnamed_warned, true, memory_order_relaxed)) {
            log_message_2(LOG_LEVEL_WARN, LOG_METADATA,
                "http_service_static: a file served below root_dir '%s' had a final path that could not be resolved (too long for the buffer, or the platform"
                " cannot report it - see file_get_final_path), so refuse_symlinks cannot tell where it lives and refuses it (logged once per process)",
                self->root_dir);
        }

        trace_log_pop();

        return _HTTP_SERVICE_STATIC_PLACEMENT_UNKNOWN;
    }

    char    expected[_HTTP_SERVICE_STATIC_PATH_BUFFER_MAX]  = DEFAULT_INITIALIZATION;
    USize   expected_size                                   = self->root_final_size;
    USize   cursor                                          = 0;

    char_copy_3(expected, sizeof(expected), self->root_final, self->root_final_size);

    while (cursor < relative_size) {
        USize const segment_start = cursor;

        while (cursor < relative_size && !_http_service_static_is_separator(relative[cursor])) {
            cursor += 1;
        }

        USize const segment_size = cursor - segment_start;

        cursor += 1;

        if (segment_size == 0 || (segment_size == 1 && relative[segment_start] == '.')) {
            continue;
        }

        /* Separator, segment and terminator must all fit: a path that does not is refused, never
         * truncated - a truncated expected path could equal a prefix it is not. */
        if (expected_size + 1 + segment_size + CHAR_END_CHARACTER > sizeof(expected)) {
            trace_log_pop();

            return _HTTP_SERVICE_STATIC_PLACEMENT_ELSEWHERE;
        }

        if (expected[expected_size - 1] != _HTTP_SERVICE_STATIC_SEPARATOR) {
            expected[expected_size] = _HTTP_SERVICE_STATIC_SEPARATOR;
            expected_size += 1;
        }

        char_copy_3(expected + expected_size, sizeof(expected) - expected_size, relative + segment_start, segment_size);

        expected_size += segment_size;
    }

    USize const final_size = char_length(final_path);

#ifdef _WIN32
    bool const below = char_compare_iequal_2(final_path, final_size, expected, expected_size);
#else
    bool const below = char_compare_equal_2(final_path, final_size, expected, expected_size);
#endif // _WIN32

    trace_log_pop();

    return below ? _HTTP_SERVICE_STATIC_PLACEMENT_BELOW_ROOT : _HTTP_SERVICE_STATIC_PLACEMENT_ELSEWHERE;
}

/*
 * Open one resolved name for serving. Always NOFOLLOW first: the open itself must never reach a
 * link's target - a link to a FIFO or a device makes the OPEN the hazard - and a directory, FIFO
 * or device is refused by the open, so a name that is not a regular file is "not served" exactly
 * where file_exists_1 used to answer false. A refused LINK then depends on refuse_symlinks. On
 * (the default) it is refused: logged as a link refusal, distinct from a plain miss, with
 * *out_link_refused set so the caller does not mask it with the SPA shell. Off, the link is
 * followed the documented way: its target is resolved (file_final_path_1) and THAT is opened
 * nofollow, so the target must itself be a regular file and the open still never lands on a link.
 * Following is a second path resolution - the check-then-open an operator accepts by turning
 * refuse_symlinks off. Returns the handle, or nullptr.
 */
static File* _http_service_static_open_one(HTTP_Service_Static const *const self, char const *const file_name, bool *const out_link_refused) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "file_name", (void*) file_name);
    error_check_null(LOG_METADATA, "out_link_refused", (void*) out_link_refused);

    *out_link_refused = false;

    File *file = file_open_nofollow_try_1(file_name);

    if (!memory_empty(file) || !_http_service_static_link_refused()) {
        trace_log_pop();

        return file;
    }

    if (self->refuse_symlinks) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_static: link refused: %s is a symlink or junction (refuse_symlinks)", file_name);

        *out_link_refused = true;

        trace_log_pop();

        return nullptr;
    }

    char target[_HTTP_SERVICE_STATIC_PATH_BUFFER_MAX] = DEFAULT_INITIALIZATION;

    if (file_final_path_1(file_name, target, sizeof(target))) {
        file = file_open_nofollow_try_1(target);
    }

    trace_log_pop();

    return file;
}

/*
 * Open what _http_service_static_resolve named - or the SPA fallback, when that open fails for
 * any reason but a refused link and the fallback is allowed - and, under refuse_symlinks, verify
 * the handle lives where the request said it does (_http_service_static_handle_below_root),
 * closing it and answering nullptr otherwise. `file_name` is rewritten in place to the fallback's
 * name when the fallback is taken, so the caller's MIME lookup sees the file actually served. The
 * handle returned is the one served: nothing after this re-resolves the name.
 */
static File* _http_service_static_open(HTTP_Service_Static const *const self, char *const file_name, USize const file_name_capacity, USize const relative_start, bool const fallback_allowed) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "file_name", (void*) file_name);

    bool link_refused   = false;
    File *file          = _http_service_static_open_one(self, file_name, &link_refused);

    if (memory_empty(file) && !link_refused && fallback_allowed) {
        char_copy_3(file_name + relative_start, file_name_capacity - relative_start, self->fallback_file, self->fallback_file_size);

        file = _http_service_static_open_one(self, file_name, &link_refused);
    }

    if (memory_empty(file)) {
        trace_log_pop();

        return nullptr;
    }

    if (self->refuse_symlinks) {
        USize                           const   file_name_size  = char_length(file_name);
        _HTTP_Service_Static_Placement  const   placement       = _http_service_static_handle_below_root(self, file, file_name + relative_start, file_name_size - relative_start);

        if (placement != _HTTP_SERVICE_STATIC_PLACEMENT_BELOW_ROOT) {
            /* Only ELSEWHERE is this request's own fact; UNKNOWN already named its cause, once. */
            if (placement == _HTTP_SERVICE_STATIC_PLACEMENT_ELSEWHERE) {
                log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_static: link refused: %s does not live below root_dir (refuse_symlinks)", file_name);
            }

            file_close(&file);

            trace_log_pop();

            return nullptr;
        }
    }

#ifdef HTTP_SERVICE_STATIC_TEST_HOOKS
    if (_http_service_static_hook_after_open != nullptr) {
        _http_service_static_hook_after_open(file_name);
    }
#endif // HTTP_SERVICE_STATIC_TEST_HOOKS

    trace_log_pop();

    return file;
}

/*
 * Queue the headers every served body carries - 200 and 206 alike - ahead of the send:
 * Accept-Ranges (so a client knows a byte range is worth asking for), the validators when the
 * handle could be stat'ed (empty strings otherwise, and then omitted - a 206 answers with the
 * same validators the 200 for the same file would, which is what lets Safari's opening one-byte
 * probe learn the ETag and send If-Range on the continuation ranges, RFC 9110 S15.3.7), and the
 * Cache-Control max_age asks for.
 */
static void _http_service_static_body_headers_add(HTTP_Server_Response *const response, char const *const etag, char const *const last_modified, U32 const max_age) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "response", (void*) response);
    error_check_null(LOG_METADATA, "etag", (void*) etag);
    error_check_null(LOG_METADATA, "last_modified", (void*) last_modified);

    http_server_response_header_add(response, "Accept-Ranges", "bytes");

    if (etag[0] != '\0') {
        http_server_response_header_add(response, "ETag", etag);
    }

    if (last_modified[0] != '\0') {
        http_server_response_header_add(response, "Last-Modified", last_modified);
    }

    _http_service_static_cache_control_add(response, max_age);

    trace_log_pop();
}

/*
 * Shared conditional-GET/Range/full-file serving decision, once the file is open and verified and
 * the headers are extracted. Both serve_1 (legacy wsi-read headers) and serve_2 (clean
 * http_server_request_header_copy-read headers) funnel into this, so the RFC 9110 decision logic -
 * If-None-Match takes precedence over If-Modified-Since (S13.1.1), If-Range gates whether a Range
 * is honored, 416 on an unsatisfiable range - exists exactly once. Size and mtime come from the
 * HANDLE, so the validators describe exactly the bytes about to be served.
 *
 * TAKES *file: on a 200 or 206 it is handed to http_server_response_send_file_handle, which owns
 * it from then on; on every other path (304, 416, a refused send) it is closed here. The caller
 * never closes it.
 */
static bool _http_service_static_serve_conditional(HTTP_Service_Static const *const self,
    HTTP_Server_Request *const request, HTTP_Server_Response *const response, File **const file, char const *const mimetype, bool const charset,
    _HTTP_Service_Static_Conditional_Headers const *const headers) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "response", (void*) response);
    error_check_null(LOG_METADATA, "file", (void*) file);
    error_check_null(LOG_METADATA, "*file", !memory_empty(file) ? (void*) *file : nullptr);
    error_check_null(LOG_METADATA, "mimetype", (void*) mimetype);
    error_check_null(LOG_METADATA, "headers", (void*) headers);

    /* Conditional GET: Last-Modified + a strong ETag from size+mtime, a 304 on a
     * weakly-matching If-None-Match, and a 304 on an If-Modified-Since that the file has not
     * changed since - via datetime_from_http_try, an RFC 7231 IMF-fixdate parser. */
    USize   const   file_size   = file_get_size(*file);
    I64             mtime       = 0;
    bool    const   have_stat   = file_get_modified(*file, &mtime);

    char    etag[64]            = DEFAULT_INITIALIZATION;
    USize   etag_size           = 0;
    char    last_modified[40]   = DEFAULT_INITIALIZATION;

    /* Rendered ONCE, and handed to whichever sender below answers, so exactly one Content-Type
     * reaches the wire whatever the status code is. */
    char content_type[_HTTP_SERVICE_STATIC_CONTENT_TYPE_MAX] = DEFAULT_INITIALIZATION;

    _http_service_static_content_type(mimetype, charset, content_type, sizeof(content_type));

    if (have_stat) {
        etag_size = _http_service_static_etag(file_size, mtime, etag, sizeof(etag));

        _http_service_static_http_date(mtime, last_modified, sizeof(last_modified));

        bool not_modified = false;

        if (headers->if_none_match_size > 0) {
            /* WEAK comparison, list-aware, "*" honoured (RFC 9110 S13.1.2) - a cache that stored
             * the tag as W/"..." or batches several tags still gets its 304. */
            not_modified = etag_size > 0 && _http_service_static_if_none_match(headers->if_none_match, headers->if_none_match_size, etag, etag_size);
        }
        else if (headers->if_modified_since_size > 0) {
            /* Only consulted when If-None-Match is absent - it takes precedence per RFC 9110
             * S13.1.1 when a client sends both. */
            Datetime if_modified_since_date = DEFAULT_INITIALIZATION;

            if (datetime_from_http_try(headers->if_modified_since, headers->if_modified_since_size, &if_modified_since_date)) {
                Datetime const mtime_date = datetime_init_2((USize) (mtime > 0 ? mtime : 0));

                /* 304 unless the file changed AFTER the date the client already has. */
                not_modified = !datetime_compare_greater(&mtime_date, &if_modified_since_date);
            }
        }

        if (not_modified) {
            file_close(file);

            http_server_response_header_add(response, "ETag", etag);
            http_server_response_header_add(response, "Last-Modified", last_modified);

            /* The 304 refreshes the stored entry's headers (RFC 9110 S15.4.5,
             * RFC 9111 S4.3.4), so it has to repeat the freshness the 200 carried - without it
             * the entry stays stale and the client revalidates on every single load. */
            _http_service_static_cache_control_add(response, self->max_age);

            /* send_2, not send_empty: send_empty hardcodes text/plain, which is the wrong type
             * for a 304 whose representation is the client's cached copy of THIS file. send_2
             * writes no Content-Length and no body for a 304 (RFC 9110 S8.6), so this is the
             * same empty reply with the honest Content-Type. */
            _http_service_static_send(request, response, nullptr, 0, content_type, HTTP_SERVER_STATUS_CODE_NOT_MODIFIED);

            trace_log_pop();

            return true;
        }
    }

    /*
     * Serve a single byte range as 206 Partial Content when the client requests one AND
     * If-Range (if present) still matches the current ETag, so a range is never served against a
     * stale cached copy. If-Range uses the STRONG comparison (RFC 9110 S13.1.5): the byte-equal
     * test below is exactly that against our strong tag, and a W/-prefixed value - which a client
     * must not send there - never matches, so it falls back to a full 200. The window is whatever
     * the client asked for, streamed from the handle like the 200 - no cap, no module-side
     * buffer. A malformed or multi-range value is ignored (a full 200, which RFC 9110 permits); a
     * syntactically valid range naming no existing byte answers 416.
     */
    if (headers->range_size > 0) {
        bool range_valid = true;

        if (headers->if_range_size > 0) {
            range_valid = have_stat && etag_size > 0 && char_compare_equal_2(headers->if_range, headers->if_range_size, etag, etag_size);
        }

        USize   start           = 0;
        USize   end             = 0;
        bool    unsatisfiable   = false;

        if (range_valid && _http_service_static_range_parse(headers->range, file_size, &start, &end, &unsatisfiable)) {
            char content_range[_HTTP_SERVICE_STATIC_RANGE_HEADER_MAX] = DEFAULT_INITIALIZATION;

            snprintf(content_range, sizeof(content_range), "bytes %llu-%llu/%llu", (unsigned long long) start, (unsigned long long) end, (unsigned long long) file_size);

            http_server_response_header_add(response, "Content-Range", content_range);

            _http_service_static_body_headers_add(response, etag, last_modified, self->max_age);

            bool const served = http_server_response_send_file_handle(response, file, start, end - start + 1, content_type, HTTP_SERVER_STATUS_CODE_PARTIAL_CONTENT);

            if (!served && !memory_empty(*file)) {
                file_close(file);
            }

            trace_log_pop();

            return served;
        }

        if (range_valid && unsatisfiable) {
            file_close(file);

            char content_range[_HTTP_SERVICE_STATIC_RANGE_HEADER_MAX] = DEFAULT_INITIALIZATION;

            snprintf(content_range, sizeof(content_range), "bytes */%llu", (unsigned long long) file_size);

            /* Accept-Ranges on the 416 too: harmless, and it tells a client
             * whose range was out of bounds that a corrected one is still worth sending. */
            http_server_response_header_add(response, "Accept-Ranges", "bytes");
            http_server_response_header_add(response, "Content-Range", content_range);
            http_server_response_send_empty(response, HTTP_SERVER_STATUS_CODE_REQ_RANGE_NOT_SATISFIABLE);

            trace_log_pop();

            return true;
        }
    }

    _http_service_static_body_headers_add(response, etag, last_modified, self->max_age);

    /* The seam answers false with nothing written and the handle still ours - closed here, and
     * the caller's own fallback reply still goes out. On true it owns the handle; on a HEAD it
     * has already closed it and completed the transaction. */
    bool const served = http_server_response_send_file_handle(response, file, 0, file_size, content_type, HTTP_SERVER_STATUS_CODE_OK);

    if (!served && !memory_empty(*file)) {
        file_close(file);
    }

    trace_log_pop();

    return served;
}

/*
 * Take root_dir's final path ONCE, at construction, into self->root_final - the prefix every
 * served handle's final path is compared against under refuse_symlinks. A root that cannot be
 * resolved then (missing, unreadable, too long for the buffer) leaves root_final EMPTY and logs
 * one WARN: refuse_symlinks then refuses every request - fail closed - until the service is
 * rebuilt over a root that exists. A root that is itself a link is legal: its final path is the
 * target, and requests compare below the target.
 */
static void _http_service_static_root_final_take(HTTP_Service_Static *const self, char const *const caller) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "caller", (void*) caller);

    if (file_final_path_1(self->root_dir, self->root_final, sizeof(self->root_final))) {
        self->root_final_size = char_length(self->root_final);
    }
    else {
        /* file_final_path_1 left root_final empty, and root_final_size is still the zero the
         * constructor's DEFAULT_INITIALIZATION gave it - the fail-closed state. */
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA,
            "%s: root_dir '%s' has no resolvable final path; refuse_symlinks (on by default) refuses every request until the service is rebuilt"
            " over an existing root", caller, self->root_dir);
    }

    trace_log_pop();
}

/*==============================================================================
 * MARK: - API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
HTTP_Service_Static http_service_static_alloc_init_1(char const *const root_dir, char const *const route_prefix, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "root_dir", (void*) root_dir);
    error_check_null(LOG_METADATA, "route_prefix", (void*) route_prefix);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    HTTP_Service_Static self    = http_service_static_init_1(root_dir, route_prefix);
    self.allocator              = allocator;

    trace_log_pop();

    return self;
}

HTTP_Service_Static http_service_static_alloc_init_2(
    char const *const root_dir, char const *const route_prefix, char const *const default_file, bool const spa_fallback, U32 const max_age, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "root_dir", (void*) root_dir);
    error_check_null(LOG_METADATA, "route_prefix", (void*) route_prefix);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    HTTP_Service_Static self    = http_service_static_init_2(root_dir, route_prefix, default_file, spa_fallback, max_age);
    self.allocator              = allocator;

    trace_log_pop();

    return self;
}
#endif // ARENA_IMPLEMENTATION

HTTP_Service_Static http_service_static_init_1(char const *const root_dir, char const *const route_prefix) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "root_dir", (void*) root_dir);
    error_check_null(LOG_METADATA, "route_prefix", (void*) route_prefix);

    HTTP_Service_Static         self        = DEFAULT_INITIALIZATION;
    USize               const   root_len    = char_length(root_dir);
    USize               const   prefix_len  = char_length(route_prefix);

    if (root_len >= sizeof(self.root_dir)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_static_init_1: root_dir of %zu bytes truncated to fit %zu", root_len, sizeof(self.root_dir) - 1);
    }

    if (prefix_len >= sizeof(self.route_prefix)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_static_init_1: route_prefix of %zu bytes truncated to fit %zu", prefix_len, sizeof(self.route_prefix) - 1);
    }

    char_copy_3(self.root_dir, sizeof(self.root_dir), root_dir, root_len < sizeof(self.root_dir) ? root_len : sizeof(self.root_dir) - 1);

    self.root_dir_size = (U16) (root_len < sizeof(self.root_dir) ? root_len : sizeof(self.root_dir) - 1);

    char_copy_3(self.route_prefix, sizeof(self.route_prefix), route_prefix, prefix_len < sizeof(self.route_prefix) ? prefix_len : sizeof(self.route_prefix) - 1);

    self.route_prefix_size = (U16) (prefix_len < sizeof(self.route_prefix) ? prefix_len : sizeof(self.route_prefix) - 1);

    char_copy_3(self.default_file, sizeof(self.default_file), "index.html", CHAR_STATIC_SIZE("index.html"));

    self.default_file_size  = CHAR_STATIC_SIZE("index.html");
    self.spa_fallback       = false;
    self.max_age            = 0;
    self.refuse_dotfiles    = false;
    self.refuse_symlinks    = true;

    _http_service_static_root_final_take(&self, "http_service_static_init_1");

    trace_log_pop();

    return self;
}

HTTP_Service_Static http_service_static_init_2(char const *const root_dir, char const *const route_prefix, char const *const default_file, bool const spa_fallback, U32 const max_age) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "root_dir", (void*) root_dir);
    error_check_null(LOG_METADATA, "route_prefix", (void*) route_prefix);

    HTTP_Service_Static         self        = DEFAULT_INITIALIZATION;
    USize               const   root_len    = char_length(root_dir);
    USize               const   prefix_len  = char_length(route_prefix);

    if (root_len >= sizeof(self.root_dir)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_static_init_2: root_dir of %zu bytes truncated to fit %zu", root_len, sizeof(self.root_dir) - 1);
    }

    if (prefix_len >= sizeof(self.route_prefix)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_static_init_2: route_prefix of %zu bytes truncated to fit %zu", prefix_len, sizeof(self.route_prefix) - 1);
    }

    char_copy_3(self.root_dir, sizeof(self.root_dir), root_dir, root_len < sizeof(self.root_dir) ? root_len : sizeof(self.root_dir) - 1);

    self.root_dir_size = (U16) (root_len < sizeof(self.root_dir) ? root_len : sizeof(self.root_dir) - 1);

    char_copy_3(self.route_prefix, sizeof(self.route_prefix), route_prefix, prefix_len < sizeof(self.route_prefix) ? prefix_len : sizeof(self.route_prefix) - 1);

    self.route_prefix_size = (U16) (prefix_len < sizeof(self.route_prefix) ? prefix_len : sizeof(self.route_prefix) - 1);

    if (default_file != nullptr) {
        USize const def_len = char_length(default_file);

        if (def_len >= sizeof(self.default_file)) {
            log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_static_init_2: default_file of %zu bytes truncated to fit %zu", def_len, sizeof(self.default_file) - 1);
        }

        char_copy_3(self.default_file, sizeof(self.default_file), default_file, def_len < sizeof(self.default_file) ? def_len : sizeof(self.default_file) - 1);

        self.default_file_size = (U8) (def_len < sizeof(self.default_file) ? def_len : sizeof(self.default_file) - 1);

        char_copy_3(self.fallback_file, sizeof(self.fallback_file), default_file, def_len < sizeof(self.fallback_file) ? def_len : sizeof(self.fallback_file) - 1);

        self.fallback_file_size = (U8) (def_len < sizeof(self.fallback_file) ? def_len : sizeof(self.fallback_file) - 1);

#ifdef _WIN32
        /* Config, not request data, so it is echoed. fallback_file was copied from default_file a few
         * lines up - one value, so one check and one WARN. Not a refusal of the service: serving a
         * name Windows would read as a device or an alias is refused per request (a silent
         * not-served, never an open), and the operator should learn why here, once. */
        if (file_name_reserved_windows(self.default_file, self.default_file_size) || file_path_aliased_windows(self.default_file, self.default_file_size)) {
            log_message_2(LOG_LEVEL_WARN, LOG_METADATA,
                "http_service_static_init_2: default_file '%s' (also the fallback_file) is a Windows device or alias name and will never be served", self.default_file);
        }
#endif // _WIN32
    }

    self.spa_fallback       = spa_fallback;
    self.max_age            = max_age;
    self.refuse_dotfiles    = false;
    self.refuse_symlinks    = true;

    _http_service_static_root_final_take(&self, "http_service_static_init_2");

    trace_log_pop();

    return self;
}

bool http_service_static_serve_1(HTTP_Service_Static const *const self, HTTP_Server_Response *const response, char const *const path, USize const path_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "response", (void*) response);
    error_check_null(LOG_METADATA, "path", (void*) path);

    char    file_name[_HTTP_SERVICE_STATIC_PATH_BUFFER_MAX]  = DEFAULT_INITIALIZATION;
    USize   relative_start                                  = 0;
    bool    fallback_allowed                                = false;

    if (!_http_service_static_resolve(self, path, path_size, file_name, sizeof(file_name), &relative_start, &fallback_allowed)) {
        trace_log_pop();

        return false;
    }

    /* Opened, and under refuse_symlinks verified, BEFORE any header is read: a refusal returns
     * here having touched nothing on the response. */
    File *file = _http_service_static_open(self, file_name, sizeof(file_name), relative_start, fallback_allowed);

    if (memory_empty(file)) {
        trace_log_pop();

        return false;
    }

    bool                charset     = false;
    char const *const   mimetype    = _http_service_static_mimetype(file_name, char_length(file_name), &charset);

    /*
     * This entry point receives no HTTP_Server_Request, so it cannot use
     * http_server_request_header_copy (serve_2's clean path, below). It reads lws's recognized
     * header tokens directly off the response's underlying wsi instead, so serve_1's one
     * remaining caller (the unchecked test suite; main.c migrated to serve_2) keeps full
     * conditional-GET/Range support with no signature change (see static.h). This is the one
     * place static.c still touches a raw wsi/lws_hdr_copy, and it is
     * kept deliberately, not left over: http_server_response_get_client has no clean replacement
     * for a caller with no request. If-Modified-Since is intentionally left unread here - it is a
     * new capability added alongside serve_2's clean header API, not a preexisting one, so not
     * reading it here is not a regression. New callers should prefer serve_2.
     */
    struct lws *const wsi = (struct lws*) http_server_response_get_client(response);

    _HTTP_Service_Static_Conditional_Headers headers = DEFAULT_INITIALIZATION;

    char if_none_match[_HTTP_SERVICE_STATIC_RANGE_HEADER_MAX] = DEFAULT_INITIALIZATION;
    I32  const if_none_match_size = lws_hdr_copy(wsi, if_none_match, (I32) sizeof(if_none_match), WSI_TOKEN_HTTP_IF_NONE_MATCH);

    if (if_none_match_size > 0) {
        headers.if_none_match       = if_none_match;
        headers.if_none_match_size  = (USize) if_none_match_size;
    }

    char range[_HTTP_SERVICE_STATIC_RANGE_HEADER_MAX] = DEFAULT_INITIALIZATION;
    I32  const range_size = lws_hdr_copy(wsi, range, (I32) sizeof(range), WSI_TOKEN_HTTP_RANGE);

    if (range_size > 0) {
        headers.range       = range;
        headers.range_size  = (USize) range_size;
    }

    char if_range[_HTTP_SERVICE_STATIC_RANGE_HEADER_MAX] = DEFAULT_INITIALIZATION;
    I32  const if_range_size = lws_hdr_copy(wsi, if_range, (I32) sizeof(if_range), WSI_TOKEN_HTTP_IF_RANGE);

    if (if_range_size > 0) {
        headers.if_range       = if_range;
        headers.if_range_size  = (USize) if_range_size;
    }

    /* nullptr: this entry point has no HTTP_Server_Request, so the compression seam has no
     * Accept-Encoding to read and every body it sends stays identity. */
    bool const served = _http_service_static_serve_conditional(self, nullptr, response, &file, mimetype, charset, &headers);

    trace_log_pop();

    return served;
}

bool http_service_static_serve_2(HTTP_Service_Static const *const self, HTTP_Server_Request *const request, HTTP_Server_Response *const response) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "request", (void*) request);
    error_check_null(LOG_METADATA, "response", (void*) response);

    char const *const path_data = http_server_request_get_path_1(request);
    USize const path_size = http_server_request_get_path_size(request);

    char    file_name[_HTTP_SERVICE_STATIC_PATH_BUFFER_MAX]  = DEFAULT_INITIALIZATION;
    USize   relative_start                                  = 0;
    bool    fallback_allowed                                = false;

    if (!_http_service_static_resolve(self, path_data, path_size, file_name, sizeof(file_name), &relative_start, &fallback_allowed)) {
        trace_log_pop();

        return false;
    }

    /* Opened, and under refuse_symlinks verified, BEFORE any header is read: a refusal returns
     * here having touched nothing on the response. */
    File *file = _http_service_static_open(self, file_name, sizeof(file_name), relative_start, fallback_allowed);

    if (memory_empty(file)) {
        trace_log_pop();

        return false;
    }

    bool                charset     = false;
    char const *const   mimetype    = _http_service_static_mimetype(file_name, char_length(file_name), &charset);

    /* The clean path: every header read through http_server_request_header_copy, no raw wsi. */
    _HTTP_Service_Static_Conditional_Headers headers = DEFAULT_INITIALIZATION;

    char if_none_match[_HTTP_SERVICE_STATIC_RANGE_HEADER_MAX] = DEFAULT_INITIALIZATION;

    if (http_server_request_header_copy(request, HTTP_SERVER_HEADER_IF_NONE_MATCH, if_none_match, sizeof(if_none_match))) {
        headers.if_none_match      = if_none_match;
        headers.if_none_match_size = char_length(if_none_match);
    }

    char if_modified_since[_HTTP_SERVICE_STATIC_RANGE_HEADER_MAX] = DEFAULT_INITIALIZATION;

    if (http_server_request_header_copy(request, HTTP_SERVER_HEADER_IF_MODIFIED_SINCE, if_modified_since, sizeof(if_modified_since))) {
        headers.if_modified_since      = if_modified_since;
        headers.if_modified_since_size = char_length(if_modified_since);
    }

    char range[_HTTP_SERVICE_STATIC_RANGE_HEADER_MAX] = DEFAULT_INITIALIZATION;

    if (http_server_request_header_copy(request, HTTP_SERVER_HEADER_RANGE, range, sizeof(range))) {
        headers.range      = range;
        headers.range_size = char_length(range);
    }

    char if_range[_HTTP_SERVICE_STATIC_RANGE_HEADER_MAX] = DEFAULT_INITIALIZATION;

    if (http_server_request_header_copy(request, HTTP_SERVER_HEADER_IF_RANGE, if_range, sizeof(if_range))) {
        headers.if_range      = if_range;
        headers.if_range_size = char_length(if_range);
    }

    bool const served = _http_service_static_serve_conditional(self, request, response, &file, mimetype, charset, &headers);

    trace_log_pop();

    return served;
}

#ifdef HTTP_SERVICE_STATIC_TEST_HOOKS
void http_service_static_test_hook_after_open_set(FpHTTP_Service_Static_Test_Hook const hook) {
    trace_log_push(LOG_METADATA);

    _http_service_static_hook_after_open = hook;

    trace_log_pop();
}

void http_service_static_test_hook_after_resolve_set(FpHTTP_Service_Static_Test_Hook const hook) {
    trace_log_push(LOG_METADATA);

    _http_service_static_hook_after_resolve = hook;

    trace_log_pop();
}

bool http_service_static_test_resolve(HTTP_Service_Static const *const self, char const *const path, USize const path_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "path", (void*) path);

    char    file_name[_HTTP_SERVICE_STATIC_PATH_BUFFER_MAX]  = DEFAULT_INITIALIZATION;
    USize   relative_start                                  = 0;
    bool    fallback_allowed                                = false;

    bool const resolved = _http_service_static_resolve(self, path, path_size, file_name, sizeof(file_name), &relative_start, &fallback_allowed);

    trace_log_pop();

    return resolved;
}
#endif // HTTP_SERVICE_STATIC_TEST_HOOKS

void http_service_static_uninit(HTTP_Service_Static *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();
}