#include <http/service/trace/trace.h>

/*==============================================================================
 * MARK: - Helpers
 *============================================================================*/

/*
 * Fixed-capacity line buffer. One log entry is rendered into this buffer, then
 * written with a single fprintf - see trace.h Thread Safety on why that keeps
 * an entry line-atomic under concurrent writers on the same stream.
 *
 * tail_reserved bytes at the end of the capacity are never spent by
 * _http_service_trace_line_append: they are set aside up front for _http_service_trace_line_finish's
 * closing quote/brace and newline, so that terminator always fits whole -
 * never truncated, never split - no matter how much body content a hostile,
 * over-length field tried to add first.
 */
typedef struct {
    char    data[HTTP_SERVICE_TRACE_LINE_CAPACITY_MAX];
    USize   size;
    USize   tail_reserved;
} _Trace_Line;

static void _http_service_trace_line_init(_Trace_Line *const line, USize const tail_reserved) {
    line->data[0]       = '\0';
    line->size          = 0;
    line->tail_reserved = tail_reserved;
}

/* Appends a NUL-terminated string, ALL OR NOTHING: text that does not fit
 * whole in the space left before the reserved tail is dropped in full, never
 * partially copied. In practice this all-or-nothing drop is an UNREACHABLE
 * backstop, not the mechanism that runs: _http_service_trace_line_append_escaped caps each
 * field's escaped bytes well before calling here, so the fixed line skeleton
 * never has to compete with an over-length field for space - see trace.h
 * Error Handling for the real, per-field truncation contract. Kept anyway so
 * a 2-byte ("\\\\") or 6-byte ("\\u00XX") escape can never land half on one
 * side of a cut if that assumption is ever violated. */
static void _http_service_trace_line_append(_Trace_Line *const line, char const *const text) {
    USize const text_size = char_length(text);
    USize const capacity  = HTTP_SERVICE_TRACE_LINE_CAPACITY_MAX - 1 - line->tail_reserved;
    USize const space     = capacity - line->size;

    if (text_size <= space) {
        memory_copy_1(line->data + line->size, text, text_size);

        line->size += text_size;
        line->data[line->size] = '\0';
    }
}

/* Writes the format's closing sequence (e.g. "\n", "\"\n", "}\n") and ends the
 * line. tail was reserved for exactly this at _http_service_trace_line_init, so body
 * content never reaches the reserved boundary in practice (see the per-field
 * caps that make this so, above); the clamp below is an UNREACHABLE defensive
 * backstop, not something a normal call exercises, kept only to still
 * guarantee the terminator if that assumption is ever violated. Every write
 * path calls this exactly once, so a call always ends with tail and never
 * emits more than tail's one trailing '\n'. */
static void _http_service_trace_line_finish(_Trace_Line *const line, char const *const tail) {
    USize const tail_size = char_length(tail);
    USize const max_size  = HTTP_SERVICE_TRACE_LINE_CAPACITY_MAX - 1 - tail_size;

    if (line->size > max_size) {
        line->size = max_size;
    }

    memory_copy_1(line->data + line->size, tail, tail_size);

    line->size += tail_size;
    line->data[line->size] = '\0';
}

/*
 * Appends `value` with quotes and backslashes escaped and control bytes
 * rendered as \xHH (json = false, CLF/Combined) or \uXXXX (json = true).
 * This is the only path a wire-derived string (ip, method, path, protocol,
 * referer, user-agent) takes into a log line: written raw, a path or
 * User-Agent containing a quote or newline forges extra log fields (CWE-117).
 *
 * JSON only (json = true) also escapes bytes >= 0x80 as \u00XX: JSON strings
 * are UTF-8 text, and a raw high byte from non-UTF-8 input would emit invalid
 * JSON. CLF/Combined (json = false) is byte-oriented like Apache's own access
 * log and passes those bytes through unescaped.
 *
 * Stops once max_field_size ESCAPED bytes of this field have been appended and
 * adds a "..." marker in their place, rather than continuing to grow: without
 * a per-field cap, one over-length wire field (a hostile or merely huge path)
 * could consume so much of the line buffer that the FIXED pieces after it -
 * the closing quote, status, byte count, or a JSON object's closing brace -
 * no longer fit and _http_service_trace_line_append's all-or-nothing rule drops them
 * whole, leaving a truncated line that is not well-formed (see trace.h's
 * Error Handling and HTTP_SERVICE_TRACE_LINE_CAPACITY_MAX). Capping every
 * field up front is what makes the line skeleton always fit instead. */
static void _http_service_trace_line_append_escaped(_Trace_Line *const line, char const *const value, bool const json, USize const max_field_size) {
    USize written = 0;

    for (USize i = 0; value[i] != '\0'; i += 1) {
        unsigned char const byte = (unsigned char) value[i];
        char piece[8] = DEFAULT_INITIALIZATION;
        USize piece_size = 0;

        if (byte == '"') {
            piece[0]   = '\\';
            piece[1]   = '"';
            piece_size = 2;
        }
        else if (byte == '\\') {
            piece[0]   = '\\';
            piece[1]   = '\\';
            piece_size = 2;
        }
        else if (byte < 0x20 || byte == 0x7f || (json && byte >= 0x80)) {
            char_format(piece, sizeof(piece), json ? "\\u%04x" : "\\x%02x", (unsigned) byte);

            piece_size = char_length(piece);
        }
        else {
            piece[0]   = (char) byte;
            piece_size = 1;
        }

        if (written + piece_size > max_field_size) {
            _http_service_trace_line_append(line, "...");

            return;
        }

        _http_service_trace_line_append(line, piece);

        written += piece_size;
    }
}

/*
 * Writes a rendered line to self->stream, guarding both ends of the fprintf:
 * self->stream can be null after uninit (a use-after-uninit rather than a
 * programmer-checked precondition, since callers hold self past uninit in
 * some request-pooling shapes), and fprintf can itself under-write or fail
 * on a broken/closed stream. Neither is fatal - a trace line is diagnostic,
 * not load-bearing - so both are reported once via a static "already warned"
 * flag rather than on every call, which would otherwise flood the very log
 * this function is trying to write to. The flag is a process-wide atomic_bool,
 * not a per-service field, so it is shared across every HTTP_Service_Trace
 * instance in the process (one service's failing stream can suppress the
 * warning for another's) and safe under concurrent writers without its own
 * lock - atomic_exchange_explicit both reads and sets it in one step, so two
 * threads racing to report the same first failure never both win.
 */
static void _http_service_trace_line_write(_Trace_Line const *const line, FILE *const stream) {
    static atomic_bool _warned = false;

    if (stream == nullptr) {
        return;
    }

    I32 const written = fprintf(stream, "%s", line->data);

    if ((written < 0 || (USize) written != line->size) && !atomic_exchange_explicit(&_warned, true, memory_order_relaxed)) {
        log_message_1(LOG_LEVEL_WARN, "http_service_trace: a log line was short or failed to write; further occurrences are not reported\n");
    }
}

/* Common request-line prefix shared by CLF and Combined: ip - - [time] "METHOD path PROTOCOL" status bytes */
static void _http_service_trace_line_append_request_prefix(
    _Trace_Line *const line, char const *const ip, char const *const method, char const *const path, char const *const protocol, I32 const status_code, USize const bytes_sent,
    char const *const timestamp) {
    char number[24] = DEFAULT_INITIALIZATION;

    _http_service_trace_line_append_escaped(line, ip, false, HTTP_SERVICE_TRACE_FIELD_CAPACITY_SHORT);
    _http_service_trace_line_append(line, " - - ");
    _http_service_trace_line_append(line, timestamp);
    _http_service_trace_line_append(line, " \"");
    _http_service_trace_line_append_escaped(line, method, false, HTTP_SERVICE_TRACE_FIELD_CAPACITY_SHORT);
    _http_service_trace_line_append(line, " ");
    _http_service_trace_line_append_escaped(line, path, false, HTTP_SERVICE_TRACE_FIELD_CAPACITY_PATH);
    _http_service_trace_line_append(line, " ");
    _http_service_trace_line_append_escaped(line, protocol, false, HTTP_SERVICE_TRACE_FIELD_CAPACITY_SHORT);
    _http_service_trace_line_append(line, "\" ");

    char_format(number, sizeof(number), "%d", status_code);
    _http_service_trace_line_append(line, number);
    _http_service_trace_line_append(line, " ");

    if (bytes_sent == 0) {
        _http_service_trace_line_append(line, "-");
    }
    else {
        char_format(number, sizeof(number), "%zu", bytes_sent);
        _http_service_trace_line_append(line, number);
    }
}

/*
 * Fills buffer with the current time formatted as a CLF timestamp.
 * Format: [DD/Mon/YYYY:HH:MM:SS +0000]
 * buffer must be at least 32 bytes.
 *
 * Built on datetime, not gmtime: a Datetime is a plain value with no shared
 * static state, so this is reentrant with no locking - unlike gmtime, which
 * returns a pointer into a buffer shared across every call in the process.
 */
static void _http_service_trace_timestamp(char *const buffer, USize const buffer_size) {
    Datetime const now = datetime_init_1();

    datetime_format(&now, "[%d/%b/%Y:%H:%M:%S +0000]", buffer, buffer_size);
}

static bool _http_service_trace_path_excluded(HTTP_Service_Trace const *const self, char const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "path", (void*) path);

    USize const path_size = char_length(path);
    bool excluded = false;

    for (USize i = 0; i < al_str_get_size(&self->exclude_paths); ++i) {
        Str const *const prefix = al_str_at(&self->exclude_paths, i);
        USize const prefix_size = str_get_size(prefix);

        if (path_size >= prefix_size && char_compare_equal_2((char*) path, prefix_size, (char*) str_get_data(prefix), prefix_size)) {
            excluded = true;

            break;
        }
    }

    trace_log_pop();

    return excluded;
}

static void _http_service_trace_write_common(HTTP_Service_Trace const *const self,
    char const *const ip, char const *const method, char const *const path, char const *const protocol, I32 const status_code, USize const bytes_sent) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "ip", (void*) ip);
    error_check_null(LOG_METADATA, "method", (void*) method);
    error_check_null(LOG_METADATA, "path", (void*) path);
    error_check_null(LOG_METADATA, "protocol", (void*) protocol);

    char timestamp[32] = DEFAULT_INITIALIZATION;

    _http_service_trace_timestamp(timestamp, sizeof(timestamp));

    _Trace_Line line = DEFAULT_INITIALIZATION;

    _http_service_trace_line_init(&line, CHAR_STATIC_SIZE("\n"));
    _http_service_trace_line_append_request_prefix(&line, ip, method, path, protocol, status_code, bytes_sent, timestamp);
    _http_service_trace_line_finish(&line, "\n");

    _http_service_trace_line_write(&line, self->stream);

    trace_log_pop();
}

static void _http_service_trace_write_combined(HTTP_Service_Trace const *const self,
    char const *const ip, char const *const method, char const *const path, char const *const protocol, I32 const status_code, USize const bytes_sent, char const *const referer,
    char const *const user_agent) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "ip", (void*) ip);
    error_check_null(LOG_METADATA, "method", (void*) method);
    error_check_null(LOG_METADATA, "path", (void*) path);
    error_check_null(LOG_METADATA, "protocol", (void*) protocol);

    char timestamp[32] = DEFAULT_INITIALIZATION;

    _http_service_trace_timestamp(timestamp, sizeof(timestamp));

    _Trace_Line line = DEFAULT_INITIALIZATION;

    _http_service_trace_line_init(&line, CHAR_STATIC_SIZE("\"\n"));
    _http_service_trace_line_append_request_prefix(&line, ip, method, path, protocol, status_code, bytes_sent, timestamp);
    _http_service_trace_line_append(&line, " \"");

    if (referer != nullptr) {
        _http_service_trace_line_append_escaped(&line, referer, false, HTTP_SERVICE_TRACE_FIELD_CAPACITY_LONG);
    }
    else {
        _http_service_trace_line_append(&line, "-");
    }

    _http_service_trace_line_append(&line, "\" \"");

    if (user_agent != nullptr) {
        _http_service_trace_line_append_escaped(&line, user_agent, false, HTTP_SERVICE_TRACE_FIELD_CAPACITY_LONG);
    }
    else {
        _http_service_trace_line_append(&line, "-");
    }

    _http_service_trace_line_finish(&line, "\"\n");

    _http_service_trace_line_write(&line, self->stream);

    trace_log_pop();
}

static void _http_service_trace_write_json(HTTP_Service_Trace const *const self,
    char const *const ip, char const *const method, char const *const path, char const *const protocol, I32 const status_code, USize const bytes_sent, U64 const duration_ms,
    char const *const referer, char const *const user_agent) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "ip", (void*) ip);
    error_check_null(LOG_METADATA, "method", (void*) method);
    error_check_null(LOG_METADATA, "path", (void*) path);
    error_check_null(LOG_METADATA, "protocol", (void*) protocol);

    char timestamp[32] = DEFAULT_INITIALIZATION;

    _http_service_trace_timestamp(timestamp, sizeof(timestamp));

    _Trace_Line line = DEFAULT_INITIALIZATION;
    char number[24] = DEFAULT_INITIALIZATION;

    _http_service_trace_line_init(&line, CHAR_STATIC_SIZE("}\n"));
    _http_service_trace_line_append(&line, "{\"ip\":\"");
    _http_service_trace_line_append_escaped(&line, ip, true, HTTP_SERVICE_TRACE_FIELD_CAPACITY_SHORT);
    _http_service_trace_line_append(&line, "\",\"time\":\"");
    _http_service_trace_line_append(&line, timestamp);
    _http_service_trace_line_append(&line, "\",\"method\":\"");
    _http_service_trace_line_append_escaped(&line, method, true, HTTP_SERVICE_TRACE_FIELD_CAPACITY_SHORT);
    _http_service_trace_line_append(&line, "\",\"path\":\"");
    _http_service_trace_line_append_escaped(&line, path, true, HTTP_SERVICE_TRACE_FIELD_CAPACITY_PATH);
    _http_service_trace_line_append(&line, "\",\"protocol\":\"");
    _http_service_trace_line_append_escaped(&line, protocol, true, HTTP_SERVICE_TRACE_FIELD_CAPACITY_SHORT);
    _http_service_trace_line_append(&line, "\",\"status\":");

    char_format(number, sizeof(number), "%d", status_code);
    _http_service_trace_line_append(&line, number);

    _http_service_trace_line_append(&line, ",\"bytes\":");

    char_format(number, sizeof(number), "%zu", bytes_sent);
    _http_service_trace_line_append(&line, number);

    _http_service_trace_line_append(&line, ",\"duration_ms\":");

    char_format(number, sizeof(number), "%llu", (unsigned long long) duration_ms);
    _http_service_trace_line_append(&line, number);

    _http_service_trace_line_append(&line, ",\"referer\":");

    if (referer != nullptr) {
        _http_service_trace_line_append(&line, "\"");
        _http_service_trace_line_append_escaped(&line, referer, true, HTTP_SERVICE_TRACE_FIELD_CAPACITY_LONG);
        _http_service_trace_line_append(&line, "\"");
    }
    else {
        _http_service_trace_line_append(&line, "null");
    }

    _http_service_trace_line_append(&line, ",\"user_agent\":");

    if (user_agent != nullptr) {
        _http_service_trace_line_append(&line, "\"");
        _http_service_trace_line_append_escaped(&line, user_agent, true, HTTP_SERVICE_TRACE_FIELD_CAPACITY_LONG);
        _http_service_trace_line_append(&line, "\"");
    }
    else {
        _http_service_trace_line_append(&line, "null");
    }

    _http_service_trace_line_finish(&line, "}\n");

    _http_service_trace_line_write(&line, self->stream);

    trace_log_pop();
}

#ifdef ARENA_IMPLEMENTATION
static HTTP_Service_Trace _http_service_trace_init(FILE *const stream, HTTP_Service_Trace_Format const format, Arena *const allocator)
#else
static HTTP_Service_Trace _http_service_trace_init(FILE *const stream, HTTP_Service_Trace_Format const format)
#endif // ARENA_IMPLEMENTATION
{
    HTTP_Service_Trace trace = DEFAULT_INITIALIZATION;

    trace.stream = (stream != nullptr) ? stream : HTTP_SERVICE_TRACE_DEFAULT_STREAM;
    trace.format = format;

#ifdef ARENA_IMPLEMENTATION
    trace.allocator     = allocator;
    trace.exclude_paths = (allocator != nullptr) ? al_str_alloc_init_1(allocator) : al_str_init_1();
#else
    trace.exclude_paths = al_str_init_1();
#endif // ARENA_IMPLEMENTATION

    return trace;
}

/*==============================================================================
 * MARK: - API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
HTTP_Service_Trace http_service_trace_alloc_init_1(Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    HTTP_Service_Trace const trace = http_service_trace_alloc_init_2(HTTP_SERVICE_TRACE_DEFAULT_STREAM, HTTP_SERVICE_TRACE_FORMAT_COMMON, allocator);

    trace_log_pop();

    return trace;
}

HTTP_Service_Trace http_service_trace_alloc_init_2(FILE *const stream, HTTP_Service_Trace_Format const format, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    HTTP_Service_Trace const trace = _http_service_trace_init(stream, format, allocator);

    trace_log_pop();

    return trace;
}
#endif // ARENA_IMPLEMENTATION

bool http_service_trace_exclude_add(HTTP_Service_Trace *const self, char const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "path", (void*) path);

    USize const path_size = char_length(path);

    /* An empty prefix would match every path: http_service_trace_log compares
     * prefix_size bytes, and an empty prefix has prefix_size 0, which every
     * path satisfies trivially. Refuse it outright rather than let it silently
     * switch the whole log off. */
    if (path_size == 0) {
        trace_log_pop();

        return false;
    }

#ifdef ARENA_IMPLEMENTATION
    Str str = (self->allocator != nullptr) ?
        str_alloc_init_static((char*) path, path_size, self->allocator) :
        str_init_static((char*) path, path_size);
#else
    Str str = str_init_static((char*) path, path_size);
#endif // ARENA_IMPLEMENTATION

    /* str_alloc_init_static degrades to the EMPTY Str when the arena refuses the
     * copy. Appending that would reproduce the exact failure just refused above,
     * so treat a post-refusal empty Str the same way: refuse and release it. */
    if (str_get_size(&str) == 0) {
        str_uninit(&str);

        trace_log_pop();

        return false;
    }

    USize const excluded_before = al_str_get_size(&self->exclude_paths);

    al_str_add_last(&self->exclude_paths, &str);

    /* add_last DECLINES rather than growing when the allocator refuses, and reports
     * it by leaving the size alone. The Str owns a fresh COPY of `path`, so a
     * dropped node puts that copy beyond exclude_paths' uninit - release it here,
     * where this frame still owns it. */
    if (al_str_get_size(&self->exclude_paths) == excluded_before) {
        str_uninit(&str);

        trace_log_pop();

        return false;
    }

    trace_log_pop();

    return true;
}

HTTP_Service_Trace http_service_trace_init_1(void) {
#ifdef ARENA_IMPLEMENTATION
    return _http_service_trace_init(HTTP_SERVICE_TRACE_DEFAULT_STREAM, HTTP_SERVICE_TRACE_FORMAT_COMMON, nullptr);
#else
    return _http_service_trace_init(HTTP_SERVICE_TRACE_DEFAULT_STREAM, HTTP_SERVICE_TRACE_FORMAT_COMMON);
#endif // ARENA_IMPLEMENTATION
}

HTTP_Service_Trace http_service_trace_init_2(FILE *const stream, HTTP_Service_Trace_Format const format) {
#ifdef ARENA_IMPLEMENTATION
    return _http_service_trace_init(stream, format, nullptr);
#else
    return _http_service_trace_init(stream, format);
#endif // ARENA_IMPLEMENTATION
}

void http_service_trace_log(HTTP_Service_Trace const *const self,
    char const *const ip, char const *const method, char const *const path, char const *const protocol, I32 const status_code, USize const bytes_sent, U64 const duration_ms) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "ip", (void*) ip);
    error_check_null(LOG_METADATA, "method", (void*) method);
    error_check_null(LOG_METADATA, "path", (void*) path);
    error_check_null(LOG_METADATA, "protocol", (void*) protocol);

    if (_http_service_trace_path_excluded(self, path)) {
        trace_log_pop();

        return;
    }

    if (self->format == HTTP_SERVICE_TRACE_FORMAT_JSON) {
        _http_service_trace_write_json(self, ip, method, path, protocol, status_code, bytes_sent, duration_ms, nullptr, nullptr);
    }
    else {
        _http_service_trace_write_common(self, ip, method, path, protocol, status_code, bytes_sent);
    }

    trace_log_pop();
}

void http_service_trace_log_combined(HTTP_Service_Trace const *const self,
    char const *const ip, char const *const method, char const *const path, char const *const protocol, I32 const status_code, USize const bytes_sent, U64 const duration_ms,
    char const *const referer, char const *const user_agent) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "ip", (void*) ip);
    error_check_null(LOG_METADATA, "method", (void*) method);
    error_check_null(LOG_METADATA, "path", (void*) path);
    error_check_null(LOG_METADATA, "protocol", (void*) protocol);

    if (_http_service_trace_path_excluded(self, path)) {
        trace_log_pop();

        return;
    }

    if (self->format == HTTP_SERVICE_TRACE_FORMAT_COMBINED) {
        _http_service_trace_write_combined(self, ip, method, path, protocol, status_code, bytes_sent, referer, user_agent);
    }
    else if (self->format == HTTP_SERVICE_TRACE_FORMAT_JSON) {
        _http_service_trace_write_json(self, ip, method, path, protocol, status_code, bytes_sent, duration_ms, referer, user_agent);
    }
    else {
        _http_service_trace_write_common(self, ip, method, path, protocol, status_code, bytes_sent);
    }

    trace_log_pop();
}

void http_service_trace_uninit(HTTP_Service_Trace *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    al_str_uninit(&self->exclude_paths);

#ifdef ARENA_IMPLEMENTATION
    self->allocator = nullptr;
#endif // ARENA_IMPLEMENTATION
    self->stream    = nullptr;
    self->format    = HTTP_SERVICE_TRACE_FORMAT_COMMON;

    trace_log_pop();
}