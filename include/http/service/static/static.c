/*
 * static.c - HTTP static file service implementation
 */

#include <http/service/static/static.h>

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/* One row of the extension-to-MIME lookup table. */
typedef struct {
    char const  *extension;
    USize       extension_size;
    char const  *mimetype;
    /* Whether to append "; charset=utf-8" - true for every text-family entry (report Mid 9). Kept as an
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

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

/* Buffer size for the rendered "<mimetype>; charset=utf-8" Content-Type value. The longest
 * mimetype in the table below is well under half of this. */
#define _HTTP_SERVICE_STATIC_CONTENT_TYPE_MAX 128
/* Largest window returned for one Range response; caps memory for open-ended ranges. See
 * static.h's Range requests note for the LWS_WITH_RANGES trade-off this cap avoids. */
#define _HTTP_SERVICE_STATIC_RANGE_CHUNK_MAX (4 * 1024 * 1024)
/* Maximum digits accepted per Range number; the I64/ISize digit count keeps
 * accumulation below 2^64 (no wrap) and stays cross-checked by types.h. */
#define _HTTP_SERVICE_STATIC_RANGE_DIGITS_MAX ISIZE_DIGITS_MAX
/* Buffer size for the Range request header and the Content-Range response header. Also used for
 * If-None-Match/If-Range: a weak ETag here is short (size+mtime in hex), but a client may quote
 * a longer value than we generated - this stays generous rather than truncating a real one into
 * a false mismatch. NOTE (Misc 32): lws_hdr_copy returns -1 (not clamped) for a header longer
 * than the buffer, and every call site below treats that exactly like "header absent" - a client
 * sending an oversize conditional/Range header silently gets the unconditional response rather
 * than a 400. Documented, not changed: the safe direction for a value we cannot fully read. */
#define _HTTP_SERVICE_STATIC_RANGE_HEADER_MAX 256

/*
 * Extension-to-MIME map, matched by file-name suffix, CASE-INSENSITIVELY (report Mid 9 - a
 * camera's .JPG/.PNG uploads used to fall through to octet-stream, and the security service's
 * nosniff header then blocks a .JS asset served that way). Anything not listed falls back to
 * lws_get_mimetype and then application/octet-stream. Ordered by category for readability.
 * Video/audio types matter for streaming: browsers (notably Safari/iOS) reject media served as
 * application/octet-stream.
 *
 * .ts is video/mp2t (MPEG transport stream) here, NOT TypeScript source (report Misc 30) - this
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

/*==============================================================================
 * MARK: - Helpers
 *============================================================================*/

/* Case-insensitive byte-span equality for extension matching (report Mid 9). */
static bool _http_service_static_extension_equal(char const *const a, char const *const b, USize const size) {
    trace_log_push(LOG_METADATA);

    for (USize i = 0; i < size; i += 1) {
        if (char_to_lower(a[i]) != char_to_lower(b[i])) {
            trace_log_pop();

            return false;
        }
    }

    trace_log_pop();

    return true;
}

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
            _http_service_static_extension_equal(file_name + file_name_size - extension_size, _HTTP_SERVICE_STATIC_MIME_TABLE[i].extension, extension_size)) {
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
 * per report Mid 8) or a start at/past EOF - in which case the caller must answer 416 rather
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
    } else if (_http_service_static_range_digits(&cursor, &start) > 0 && *cursor == '-') {
        cursor += 1;

        bool parsed = false;

        if (*cursor == '\0') {
            end     = file_size - 1;
            parsed  = true;
        } else if (_http_service_static_range_digits(&cursor, &end) > 0 && *cursor == '\0') {
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
 * Render a weak ETag from file size and mtime - "W/<size-hex>-<mtime-hex>". Cheap (no file
 * content is read) and changes whenever either input changes, which is what a static-file
 * server needs: it does not survive a byte-identical rewrite with a preserved mtime, but no
 * caller here relies on that finer guarantee.
 */
static USize _http_service_static_etag(USize const file_size, I64 const mtime, char *const buffer, USize const capacity) {
    trace_log_push(LOG_METADATA);

    USize const size = (USize) snprintf(buffer, capacity, "W/\"%llx-%llx\"", (unsigned long long) file_size, (unsigned long long) mtime);

    trace_log_pop();

    return size;
}

/* RFC 7231 "HTTP-date" (IMF-fixdate) rendering, e.g. "Wed, 21 Oct 2015 07:28:00 GMT". Datetime's
 * generic datetime_format has no weekday token, so this reads day_week/month/date/etc directly
 * off the struct rather than going through it - self-contained, no locale dependency. This is
 * the RENDERING half; the parsing half an If-Modified-Since needs is datetime_from_http_try,
 * which did not exist when this comment claimed parsing was "not implemented" and now backs the
 * 304 in _http_service_static_serve_conditional (serve_2 only - serve_1 never reads the header
 * at all, see static.h). */
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
 * Whether ANY path component from self->root_dir down through the resolved file_name is a
 * symlink/junction, for the opt-in self->refuse_symlinks check (report Mid 12, walk widened per
 * memsec MED). Previously this checked only the FINAL component, so a symlinked ANCESTOR
 * directory (e.g. root_dir/linked_dir/real_leaf.txt) served straight through the check. dir/dir.h
 * exposes is_link only per directory entry (dir_list_entries), not per single path, so this lists
 * each directory ONCE PER COMPONENT below the root and scans for the matching name - O(depth)
 * directory listings per request, which is exactly why the caller-facing flag defaults to off.
 * Returns false (not a symlink / cannot tell) on any lookup failure, which is the safe direction
 * for this being a REFUSAL check: a false negative here means the file is served as it always
 * was; a false positive would 404 a legitimate file.
 *
 * Accepted risk (see static.h Symlinks note): this is a check-then-open, not an atomic
 * operation - a component swapped for a symlink between this call returning false and the
 * subsequent open (TOCTOU) is not caught.
 */
static bool _http_service_static_is_symlink(HTTP_Service_Static const *const self, char const *const file_name) {
    trace_log_push(LOG_METADATA);

    USize const file_name_size = char_length(file_name);

    char parent[HTTP_SERVER_PATH_MAX_LENGTH * 4] = DEFAULT_INITIALIZATION;

    char_copy_3(parent, sizeof(parent), file_name, self->root_dir_size);
    parent[self->root_dir_size] = '\0';

    USize cursor = self->root_dir_size;

    while (cursor < file_name_size && (file_name[cursor] == '/' || file_name[cursor] == '\\')) {
        cursor += 1;
    }

    while (cursor < file_name_size) {
        USize const segment_start = cursor;

        while (cursor < file_name_size && file_name[cursor] != '/' && file_name[cursor] != '\\') {
            cursor += 1;
        }

        USize const segment_size = cursor - segment_start;

        char segment[HTTP_SERVER_PATH_MAX_LENGTH] = DEFAULT_INITIALIZATION;

        char_copy_3(segment, sizeof(segment), file_name + segment_start, segment_size);
        segment[segment_size] = '\0';

        char const *const           parent_for_lookup = parent[0] == '\0' ? "." : parent;
        DirEntry                    *entries           = nullptr;
        USize                       count              = 0;
        bool                        is_link            = false;

        if (dir_list_entries_1(parent_for_lookup, &entries, &count)) {
            for (USize i = 0; i < count; i += 1) {
                if (char_compare_equal_1(entries[i].name, segment)) {
                    is_link = entries[i].is_link;

                    break;
                }
            }

            dir_list_entries_uninit(entries, count);
        }

        if (is_link) {
            trace_log_pop();

            return true;
        }

        USize parent_size = char_length(parent);

        if (parent_size > 0 && parent[parent_size - 1] != '/' && parent[parent_size - 1] != '\\') {
            parent[parent_size]        = '/';
            parent_size                += 1;
            parent[parent_size]        = '\0';
        }

        char_copy_3(parent + parent_size, sizeof(parent) - parent_size, segment, segment_size);
        parent[parent_size + segment_size] = '\0';

        while (cursor < file_name_size && (file_name[cursor] == '/' || file_name[cursor] == '\\')) {
            cursor += 1;
        }
    }

    trace_log_pop();

    return false;
}

#ifdef _WIN32
/*
 * Whether `name` (a single path COMPONENT, not a full path - the caller passes the resolved
 * file's base name) is a Windows reserved device name: CON, PRN, AUX, NUL, COM1-9, LPT1-9,
 * matched case-insensitively and with or without a trailing extension ("nul.txt" counts, same as
 * bare "nul"). Windows resolves these specially at the filesystem-API level regardless of
 * whatever a stat-family call like file_exists_1 reports - "nul" opens the null device (a stream
 * that reads EOF and discards writes) and "con"/"aux"/"lpt1" open a console/serial/parallel
 * device, none of which is the on-disk file the request appeared to name. Checked only on
 * Windows, since the restriction is Windows-specific; static roots deployed on Linux have no
 * such device namespace to guard against.
 */
static bool _http_service_static_is_reserved_windows_name(char const *const name, USize const name_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "name", (void*) name);

    static char const *const reserved[] = {
        "AUX", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "CON", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9", "NUL"
    };

    /* Win32 strips trailing spaces and dots from a final path component before it looks the
     * name up, so "nul " and "con." reach the device namespace exactly like "nul" and "con"
     * do. Trim them first, then cut the stem at the first dot. */
    USize trimmed_size = name_size;

    while (trimmed_size > 0 && (name[trimmed_size - 1] == ' ' || name[trimmed_size - 1] == '.')) {
        trimmed_size -= 1;
    }

    USize stem_size = trimmed_size;

    for (USize i = 0; i < trimmed_size; i += 1) {
        if (name[i] == '.') {
            stem_size = i;

            break;
        }
    }

    for (USize i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i += 1) {
        USize const reserved_size = char_length(reserved[i]);

        if (stem_size == reserved_size && _http_service_static_extension_equal(name, reserved[i], reserved_size)) {
            trace_log_pop();

            return true;
        }
    }

    trace_log_pop();

    return false;
}
#endif // _WIN32

/*
 * Append a snprintf-formatted header fragment into `buffer` at `*size`, refusing (leaving `*size`
 * unchanged) rather than accumulating a value that would overrun the remaining capacity. Mirrors
 * http_headers_cache_max_age_into's own refuse-rather-than-truncate contract - a cut-off header
 * line is indistinguishable from a real one, and without this clamp an snprintf return value that
 * EXCEEDS the space actually available (the C standard's contract: the number of bytes that WOULD
 * have been written, not the number actually written) could push `*size` past `capacity`; the
 * NEXT call's "capacity - *size" would then underflow (both USize, unsigned) into a huge number
 * and write past the end of `buffer`.
 */
static void _http_service_static_headers_append(char *const buffer, USize const capacity, USize *const size, char const *const format, ...) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "buffer", (void*) buffer);
    error_check_null(LOG_METADATA, "size", (void*) size);
    error_check_null(LOG_METADATA, "format", (void*) format);

    if (*size >= capacity) {
        trace_log_pop();

        return;
    }

    va_list args;

    va_start(args, format);

    I32 const written = vsnprintf(buffer + *size, capacity - *size, format, args);

    va_end(args);

    if (written > 0 && (USize) written < capacity - *size) {
        *size += (USize) written;
    }

    trace_log_pop();
}

/*
 * Queue "Cache-Control: max-age=<max_age>" on `response` as a header_add VALUE.
 *
 * http_headers_cache_max_age_into renders a whole "Cache-Control: ...\r\n" LINE, which is what
 * the raw extra-headers block on the full-file path wants; header_add wants the value alone, so
 * the prefix and the trailing CRLF are trimmed here. Shared by the 206 and 304 paths, which both
 * used to differ from the 200 by silently carrying no freshness at all (report Mid 5: a 304 that
 * omits Cache-Control does not renew the stored entry's freshness, so a client revalidates on
 * every load once the original max-age has lapsed).
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
 * Report High 1: the charset used to be appended as a second "Content-Type:" line in the raw
 * extra-headers block, on top of the one libwebsockets emits from send_file's content_type
 * argument, so every text-family 200 and HEAD went out with TWO Content-Type headers. It is
 * a singleton field (RFC 9110 S5.5) - which of the two a client honors is implementation-defined
 * and an intermediary may reject the reply outright. Rendering the full value here and handing
 * it to the sender as THE content type leaves exactly one on the wire.
 */
static void _http_service_static_content_type(char const *const mimetype, bool const charset, char *const buffer, USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mimetype", (void*) mimetype);
    error_check_null(LOG_METADATA, "buffer", (void*) buffer);

    snprintf(buffer, capacity, charset ? "%s; charset=utf-8" : "%s", mimetype);

    trace_log_pop();
}

/*
 * Read the [start, end] window (capped to _HTTP_SERVICE_STATIC_RANGE_CHUNK_MAX)
 * from file_name and send it as 206 Partial Content with Content-Range and
 * Accept-Ranges headers, honoring max_age (report Mid 8: previously the 206 path
 * never set Cache-Control at all). An unsatisfiable range answers 416 with
 * Content-Range: bytes * /file_size here rather than falling back to a full body.
 * Returns true when a response was already sent (206 or 416) - the caller must not
 * fall back in that case. Returns false only for a malformed/multi-range value or
 * an unreadable file, so the caller can fall back to a full response.
 *
 * content_type is the full rendered value (charset included, report High 1), and etag /
 * last_modified are the caller's already-computed validators, empty when the file could not be
 * stat'ed. Report Mid 4: a 206 used to carry neither, so Safari - whose opening probe is a
 * one-byte `bytes=0-1` - never learned the validator and sent no If-Range on the continuation
 * ranges that follow. RFC 9110 S15.3.7 asks a 206 to carry the same validators as the 200.
 */
static bool _http_service_static_serve_range(char const *const file_name, char const *const content_type, char const *const range, U32 const max_age,
    char const *const etag, char const *const last_modified, HTTP_Server_Response *const response) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "file_name", (void*) file_name);
    error_check_null(LOG_METADATA, "content_type", (void*) content_type);
    error_check_null(LOG_METADATA, "range", (void*) range);
    error_check_null(LOG_METADATA, "etag", (void*) etag);
    error_check_null(LOG_METADATA, "last_modified", (void*) last_modified);
    error_check_null(LOG_METADATA, "response", (void*) response);

    /* file_open_try_1, not file_open_1: the null check below was dead, because
     * file_open_1 ends the process on a path that is not there. A range request
     * for a file that does not exist reaches here from an unauthenticated
     * client, so the dead check was a remote way to stop the server. */
    File *file = file_open_try_1(file_name, "rb");

    if (memory_empty((void*) file)) {
        trace_log_pop();

        return false;
    }

    USize   const   file_size       = file_get_size(file);
    USize           start           = 0;
    USize           end             = 0;
    bool            unsatisfiable   = false;

    if (!_http_service_static_range_parse(range, file_size, &start, &end, &unsatisfiable)) {
        file_close(&file);

        if (unsatisfiable) {
            char content_range[_HTTP_SERVICE_STATIC_RANGE_HEADER_MAX] = DEFAULT_INITIALIZATION;

            snprintf(content_range, sizeof(content_range), "bytes */%llu", (unsigned long long) file_size);

            /* Accept-Ranges on the 416 too (report Misc 19): harmless, and it tells a client
             * whose range was out of bounds that a corrected one is still worth sending. */
            http_server_response_header_add(response, "Accept-Ranges", "bytes");
            http_server_response_header_add(response, "Content-Range", content_range);
            http_server_response_send_empty(response, HTTP_SERVER_STATUS_CODE_REQ_RANGE_NOT_SATISFIABLE);

            trace_log_pop();

            return true;
        }

        trace_log_pop();

        return false;
    }

    if (end - start + 1 > _HTTP_SERVICE_STATIC_RANGE_CHUNK_MAX) {
        end = start + _HTTP_SERVICE_STATIC_RANGE_CHUNK_MAX - 1;
    }

    USize   const   size    = end - start + 1;
    /* try_alloc: the guard below was dead behind the aborting alloc - the same
     * defect as file_open_1 above, 30 lines apart. size is capped by the chunk
     * limit, so this is contract honesty, not exposure. OOM branch verified by
     * reading only - the range path ends in lws_write on a live wsi. */
    Byte    *const  buffer  = (Byte*) memory_try_alloc(size);

    if (buffer == nullptr) {
        file_close(&file);

        trace_log_pop();

        return false;
    }

    file_at(file, start, FILE_POSITION_BEGIN);

    USize const read_size = file_read_1(file, buffer, 1, size);

    file_close(&file);

    if (read_size != size) {
        memory_delete((void**) &buffer);

        trace_log_pop();

        return false;
    }

    char content_range[_HTTP_SERVICE_STATIC_RANGE_HEADER_MAX] = DEFAULT_INITIALIZATION;

    snprintf(content_range, sizeof(content_range), "bytes %llu-%llu/%llu", (unsigned long long) start, (unsigned long long) end, (unsigned long long) file_size);

    http_server_response_header_add(response, "Accept-Ranges", "bytes");
    http_server_response_header_add(response, "Content-Range", content_range);

    /* The same validators the 200 carries (report Mid 4). Empty when the file could not be
     * stat'ed, in which case the 206 goes out without them exactly as the 200 does. */
    if (etag[0] != '\0') {
        http_server_response_header_add(response, "ETag", etag);
    }

    if (last_modified[0] != '\0') {
        http_server_response_header_add(response, "Last-Modified", last_modified);
    }

    _http_service_static_cache_control_add(response, max_age);

    http_server_response_send_2(response, buffer, size, content_type, HTTP_SERVER_STATUS_CODE_PARTIAL_CONTENT);

    memory_delete((void**) &buffer);

    trace_log_pop();

    return true;
}

/*
 * Resolve an incoming request path to an on-disk file, honoring route_prefix, the directory
 * default_file, SPA fallback, and (opt-in) symlink refusal - everything serve_1/serve_2 need
 * before the header-driven conditional-GET/Range decision, which is the only part that differs
 * between the two entry points. Returns false when nothing should be served, in which case the
 * caller must also report "not served" without touching file_name/out_mimetype/out_charset.
 */
static bool _http_service_static_resolve(HTTP_Service_Static const *const self, char const *const path, USize const path_size,
    char *const file_name, USize const file_name_capacity, char const **const out_mimetype, bool *const out_charset) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "path", (void*) path);
    error_check_null(LOG_METADATA, "file_name", (void*) file_name);
    error_check_null(LOG_METADATA, "out_mimetype", (void*) out_mimetype);
    error_check_null(LOG_METADATA, "out_charset", (void*) out_charset);

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

    /* GET .../sub/ (a trailing slash on a non-root relative path) serves sub/index.html the same
     * way the prefix root does (report Mid 12) - previously only rel_path_size == 0 got this
     * treatment, so a subdirectory index needed an exact file name in the URL. */
    bool const directory_request = rel_path_size == 0 || rel_path[rel_path_size - 1] == '/';

    USize file_name_size = 0;

    char_copy_3(file_name, file_name_capacity, self->root_dir, self->root_dir_size);

    file_name_size += self->root_dir_size;

    if (file_name_size > 0 && file_name[file_name_size - 1] != '/' && file_name[file_name_size - 1] != '\\') {
        file_name[file_name_size]   = '/';
        file_name_size              += 1;
    }

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
    {
        USize base_start = 0;

        for (USize i = file_name_size; i > 0; i -= 1) {
            if (file_name[i - 1] == '/' || file_name[i - 1] == '\\') {
                base_start = i;

                break;
            }
        }

        if (_http_service_static_is_reserved_windows_name(file_name + base_start, file_name_size - base_start)) {
            trace_log_pop();

            return false;
        }
    }
#endif // _WIN32

    if (!file_exists_1(file_name)) {
        /* SPA fallback restricted to extension-less paths (report Low 20) - a missing .js/.css
         * used to fall back to index.html 200 with a text/html body, which browsers then refuse
         * to run/apply and report as a MIME-type console error. A request with a dot in its
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

        bool const has_extension = !directory_request && char_find_slice_5(rel_path, rel_path_size, segment_start, ".", CHAR_STATIC_SIZE(".")) != nullptr;

        if (self->spa_fallback && self->fallback_file_size > 0 && !has_extension) {
            file_name_size = self->root_dir_size;

            char_copy_3(file_name, file_name_capacity, self->root_dir, self->root_dir_size);

            if (file_name_size > 0 && file_name[file_name_size - 1] != '/' && file_name[file_name_size - 1] != '\\') {
                file_name[file_name_size]   = '/';
                file_name_size              += 1;
            }

            char_copy_3(file_name + file_name_size, file_name_capacity - file_name_size, self->fallback_file, self->fallback_file_size);
            file_name_size += self->fallback_file_size;

            /* Stale note this replaces: char_copy_3 now DOES terminate at data_size (char_copy_2
             * is the one that doesn't), so the call above already wrote this terminator - kept
             * explicit here anyway, right before the file_exists_1 check below, so the intent
             * ("file_name_size marks the real end of the fallback path, shorter than the
             * requested path that just failed") stays visible without following the call chain. */
            file_name[file_name_size] = '\0';

            if (!file_exists_1(file_name)) {
                trace_log_pop();

                return false;
            }
        }
        else {
            trace_log_pop();

            return false;
        }
    }

    /* Opt-in (report Mid 12): off by default, so zero cost unless a caller explicitly asks. */
    if (self->refuse_symlinks && _http_service_static_is_symlink(self, file_name)) {
        trace_log_pop();

        return false;
    }

    *out_mimetype = _http_service_static_mimetype(file_name, char_length(file_name), out_charset);

    trace_log_pop();

    return true;
}

/*
 * Shared conditional-GET/Range/full-file serving decision, once path resolution and header
 * extraction are both done. Both serve_1 (legacy wsi-read headers) and serve_2 (clean
 * http_server_request_header_copy-read headers) funnel into this, so the RFC 9110 decision logic
 * - If-None-Match takes precedence over If-Modified-Since (S13.1.1), If-Range gates whether a
 * Range is honored, the 4 MiB range cap, 416 on an unsatisfiable range - exists exactly once.
 */
static bool _http_service_static_serve_conditional(
    HTTP_Service_Static const *const self,
    HTTP_Server_Response *const response,
    char const *const file_name,
    char const *const mimetype,
    bool const charset,
    _HTTP_Service_Static_Conditional_Headers const *const headers) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "response", (void*) response);
    error_check_null(LOG_METADATA, "file_name", (void*) file_name);
    error_check_null(LOG_METADATA, "mimetype", (void*) mimetype);
    error_check_null(LOG_METADATA, "headers", (void*) headers);

    /* Conditional GET (report High 4): Last-Modified + a weak ETag from size+mtime, a 304 on a
     * matching If-None-Match, and (new) a 304 on an If-Modified-Since that the file has not
     * changed since - now possible thanks to datetime_from_http_try, an RFC 7231 IMF-fixdate
     * parser that did not exist when static.h's older "not implemented" note was written. */
    USize   file_size   = 0;
    I64     mtime       = 0;
    bool    const have_stat = file_size_1(file_name, &file_size) && file_modified_1(file_name, &mtime);

    char    etag[64]            = DEFAULT_INITIALIZATION;
    USize   etag_size           = 0;
    char    last_modified[40]   = DEFAULT_INITIALIZATION;

    /* Rendered ONCE, and handed to whichever of the three senders below answers, so exactly one
     * Content-Type reaches the wire whatever the status code is (report High 1). */
    char content_type[_HTTP_SERVICE_STATIC_CONTENT_TYPE_MAX] = DEFAULT_INITIALIZATION;

    _http_service_static_content_type(mimetype, charset, content_type, sizeof(content_type));

    if (have_stat) {
        etag_size = _http_service_static_etag(file_size, mtime, etag, sizeof(etag));

        _http_service_static_http_date(mtime, last_modified, sizeof(last_modified));

        bool not_modified = false;

        if (headers->if_none_match_size > 0) {
            not_modified = etag_size > 0 && char_compare_equal_2(headers->if_none_match, headers->if_none_match_size, etag, etag_size);
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
            http_server_response_header_add(response, "ETag", etag);
            http_server_response_header_add(response, "Last-Modified", last_modified);

            /* Report Mid 5: the 304 refreshes the stored entry's headers (RFC 9110 S15.4.5,
             * RFC 9111 S4.3.4), so it has to repeat the freshness the 200 carried - without it
             * the entry stays stale and the client revalidates on every single load. */
            _http_service_static_cache_control_add(response, self->max_age);

            /* send_2, not send_empty: send_empty hardcodes text/plain, which is the wrong type
             * for a 304 whose representation is the client's cached copy of THIS file. send_2
             * writes no Content-Length and no body for a 304 (RFC 9110 S8.6), so this is the
             * same empty reply with the honest Content-Type. */
            http_server_response_send_2(response, nullptr, 0, content_type, HTTP_SERVER_STATUS_CODE_NOT_MODIFIED);

            trace_log_pop();

            return true;
        }
    }

    /*
     * Serve a single byte range as 206 Partial Content when the client requests one AND
     * If-Range (if present) still matches the current ETag (report Low 19 - previously any
     * If-Range value was ignored outright, which is compliant but wasted the round trip; now
     * that a validator exists there is no reason not to honor it). libwebsockets is a system
     * dependency since decision 2071, not vendored, and whether it was built with
     * LWS_WITH_RANGES cannot be assumed, so this hand-rolled path runs unconditionally.
     */
    if (headers->range_size > 0) {
        bool range_valid = true;

        if (headers->if_range_size > 0) {
            range_valid = have_stat && etag_size > 0 && char_compare_equal_2(headers->if_range, headers->if_range_size, etag, etag_size);
        }

        if (range_valid && _http_service_static_serve_range(file_name, content_type, headers->range, self->max_age, etag, last_modified, response)) {
            trace_log_pop();

            return true;
        }
    }

    char extra_headers[2048] = DEFAULT_INITIALIZATION;
    USize extra_headers_size = 0;

    /* Advertise range support so clients know they may request byte ranges. */
    _http_service_static_headers_append(extra_headers, sizeof(extra_headers), &extra_headers_size, "Accept-Ranges: bytes\r\n");

    if (have_stat) {
        _http_service_static_headers_append(extra_headers, sizeof(extra_headers), &extra_headers_size, "ETag: %s\r\nLast-Modified: %s\r\n", etag, last_modified);
    }

    if (self->max_age > 0) {
        /* http_headers_cache_max_age_into already clamps itself (returns 0 on overflow), so the
         * remaining-capacity arithmetic here stays safe without going through the helper above. */
        extra_headers_size += http_headers_cache_max_age_into(extra_headers + extra_headers_size, sizeof(extra_headers) - extra_headers_size, (USize) self->max_age);
    }

    /* No "Content-Type:" line in this block: the charset travels in content_type, which
     * libwebsockets emits as the reply's ONE Content-Type header (report High 1). */

    /* http_server_response_send_file merges in anything already queued on `response` via
     * http_server_response_header_add (Cache-Control, Set-Cookie, ...) itself, and it sets
     * write_success/file_served on `response` internally - the manual bookkeeping this used to
     * need around lws_serve_http_file is gone along with that raw call. */
    bool const served = http_server_response_send_file(response, file_name, content_type, extra_headers_size > 0 ? extra_headers : nullptr, extra_headers_size);

    trace_log_pop();

    return served;
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
    self.refuse_symlinks    = false;

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
    }

    self.spa_fallback       = spa_fallback;
    self.max_age            = max_age;
    self.refuse_symlinks    = false;

    trace_log_pop();

    return self;
}

bool http_service_static_serve_1(HTTP_Service_Static const *const self, HTTP_Server_Response *const response, char const *const path, USize const path_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "response", (void*) response);
    error_check_null(LOG_METADATA, "path", (void*) path);

    char            file_name[HTTP_SERVER_PATH_MAX_LENGTH * 4]  = DEFAULT_INITIALIZATION;
    char    const   *mimetype                                   = nullptr;
    bool            charset                                     = false;

    if (!_http_service_static_resolve(self, path, path_size, file_name, sizeof(file_name), &mimetype, &charset)) {
        trace_log_pop();

        return false;
    }

    /*
     * This entry point receives no HTTP_Server_Request, so it cannot use
     * http_server_request_header_copy (serve_2's clean path, below). It reads lws's recognized
     * header tokens directly off the response's underlying wsi instead - exactly what this
     * function did before this rewrite - so serve_1's one remaining caller (the unchecked test
     * suite; main.c migrated to serve_2) keeps full conditional-GET/Range support with no
     * signature change (see static.h). This is the one place static.c still touches a raw
     * wsi/lws_hdr_copy, and it is
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

    bool const served = _http_service_static_serve_conditional(self, response, file_name, mimetype, charset, &headers);

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

    char            file_name[HTTP_SERVER_PATH_MAX_LENGTH * 4]  = DEFAULT_INITIALIZATION;
    char    const   *mimetype                                   = nullptr;
    bool            charset                                     = false;

    if (!_http_service_static_resolve(self, path_data, path_size, file_name, sizeof(file_name), &mimetype, &charset)) {
        trace_log_pop();

        return false;
    }

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

    bool const served = _http_service_static_serve_conditional(self, response, file_name, mimetype, charset, &headers);

    trace_log_pop();

    return served;
}

void http_service_static_uninit(HTTP_Service_Static *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();
}