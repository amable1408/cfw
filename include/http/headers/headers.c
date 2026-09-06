#include <http/headers/headers.h>

/*==============================================================================
 * MARK: - Helpers
 *============================================================================*/

static void _http_headers_string_add(String *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    USize const data_size = char_length(data);

    if (data_size > 0) {
        string_add_last_2(self, data, data_size);
    }

    trace_log_pop();
}

static String _http_headers_string_init(Arena *const allocator) {
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    if (allocator != nullptr) {
        String const string = string_alloc_init_1(allocator);

        trace_log_pop();

        return string;
    }
#endif // ARENA_IMPLEMENTATION

    String const string = string_init_1();

    trace_log_pop();

    return string;
}

static String _http_headers_char_to_string(char const *const data, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    USize const data_size = char_length(data);

    if (data_size == 0) {
#ifdef ARENA_IMPLEMENTATION
        if (allocator != nullptr) {
            String const string = string_alloc_init_1(allocator);

            trace_log_pop();

            return string;
        }
#endif // ARENA_IMPLEMENTATION

        String const string = string_init_1();

        trace_log_pop();

        return string;
    }

#ifdef ARENA_IMPLEMENTATION
    if (allocator != nullptr) {
        String const string = string_alloc_init_static(data, data_size, allocator);

        trace_log_pop();

        return string;
    }
#endif // ARENA_IMPLEMENTATION

    String const string = string_init_static(data, data_size);

    trace_log_pop();

    return string;
}

static void _http_headers_number_add(String *const self, USize const number) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* Stack buffer, not char_new_from_numbers_uint_1 + char_delete: a decimal USize is at
     * most 20 digits, so a per-call heap round trip buys nothing here. */
    char buffer[21] = DEFAULT_INITIALIZATION;

    char_from_numbers_uint_1(buffer, sizeof(buffer), number);

    _http_headers_string_add(self, buffer);

    trace_log_pop();
}

/*==============================================================================
 * MARK: - Cache Helpers
 *============================================================================*/

static String _http_headers_cache_max_age_create(char const *const visibility, USize const max_age, bool const immutable, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "visibility", (void*) visibility);

    String headers = _http_headers_string_init(allocator);

    _http_headers_string_add(&headers, "Cache-Control: ");
    _http_headers_string_add(&headers, visibility);
    _http_headers_string_add(&headers, ", max-age=");
    _http_headers_number_add(&headers, max_age);

    if (immutable) {
        _http_headers_string_add(&headers, ", immutable");
    }

    _http_headers_string_add(&headers, "\r\n");

    trace_log_pop();

    return headers;
}

/*==============================================================================
 * MARK: - Content Helpers
 *============================================================================*/

static bool _http_headers_content_has_control(char const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    for (USize i = 0; data[i] != '\0'; i += 1) {
        unsigned char const byte = (unsigned char) data[i];

        if (byte < 0x20 || byte == 0x7F) {
            trace_log_pop();

            return true;
        }
    }

    trace_log_pop();

    return false;
}

/* RFC 6266 quoted-string: '"' and '\\' are backslash-escaped; everything else in filename
 * passes through unchanged (CTL was already refused by the caller before this runs). */
static void _http_headers_content_disposition_filename_add(String *const self, char const *const filename) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "filename", (void*) filename);

    USize run_start = 0;
    USize i         = 0;

    for (; filename[i] != '\0'; i += 1) {
        char const byte = filename[i];

        if (byte == '"' || byte == '\\') {
            if (i > run_start) {
                string_add_last_2(self, filename + run_start, i - run_start);
            }

            char const escape[2] = { '\\', byte };

            string_add_last_2(self, escape, 2);

            run_start = i + 1;
        }
    }

    if (i > run_start) {
        string_add_last_2(self, filename + run_start, i - run_start);
    }

    trace_log_pop();
}

/*==============================================================================
 * MARK: - Security Helpers
 *============================================================================*/

static void _http_headers_security_header_add(String *const headers, char const *const name, String const *const value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "headers", (void*) headers);
    error_check_null(LOG_METADATA, "name", (void*) name);
    error_check_null(LOG_METADATA, "value", (void*) value);

    if (string_empty(value)) {
        trace_log_pop();

        return;
    }

    _http_headers_string_add(headers, name);
    string_add_last_4(headers, value);
    _http_headers_string_add(headers, "\r\n");

    trace_log_pop();
}

static void _http_headers_security_hsts_add(HTTP_Headers_Security const *const self, String *const headers) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "headers", (void*) headers);

    if (!self->strict_transport_security) {
        trace_log_pop();

        return;
    }

    _http_headers_string_add(headers, "Strict-Transport-Security: max-age=");
    _http_headers_number_add(headers, self->strict_transport_security_max_age);

    if (self->strict_transport_security_include_subdomains) {
        _http_headers_string_add(headers, "; includeSubDomains");
    }

    if (self->strict_transport_security_preload) {
        _http_headers_string_add(headers, "; preload");
    }

    _http_headers_string_add(headers, "\r\n");

    trace_log_pop();
}

/* A control byte in a policy value (e.g. an embedded CRLF) could inject arbitrary header
 * lines, so it is never trusted verbatim. Always logs - the by-value init_2 tier below uses
 * this to omit just the offending field's header (returns "" here), since it cannot refuse
 * outright; the alloc_init_2 tier instead uses the separate _http_headers_security_policy_has_control
 * below to DECIDE whether to refuse the whole policy, since it can refuse and the fold-3
 * severity (a dropped header with no log line) is exactly what the WHOLE-policy refusal
 * exists to avoid. */
static char const *_http_headers_security_field_safe(char const *const caller, char const *const field_name, char const *const value) {
    if (!_http_headers_content_has_control(value)) {
        return value;
    }

    log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "%s: %s contains a control byte; omitting its header", caller, field_name);

    return "";
}

/* Used only by the refuse-whole alloc_init_2 tier: reports (with one WARN per offending field)
 * whether ANY of the four policy values carries a control byte, without computing a substitute
 * "" for any of them - the caller refuses the whole policy rather than using a substitute. */
static bool _http_headers_security_policy_has_control(
    char const *const caller, char const *const content_security_policy, char const *const frame_options, char const *const permissions_policy, char const *const referrer_policy) {
    bool found = false;

    if (_http_headers_content_has_control(content_security_policy)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "%s: content_security_policy contains a control byte", caller);

        found = true;
    }

    if (_http_headers_content_has_control(frame_options)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "%s: frame_options contains a control byte", caller);

        found = true;
    }

    if (_http_headers_content_has_control(permissions_policy)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "%s: permissions_policy contains a control byte", caller);

        found = true;
    }

    if (_http_headers_content_has_control(referrer_policy)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "%s: referrer_policy contains a control byte", caller);

        found = true;
    }

    return found;
}

#ifdef ARENA_IMPLEMENTATION
/* An arena-backed String comes back EMPTY (data == nullptr) both when the source was genuinely
 * empty and when a starved arena refused the copy (string.h's EMPTY-vs-refused note); the two
 * are told apart here by the source's own length, since only a non-empty source can have been
 * refused. */
static bool _http_headers_security_string_refused(char const *const source, String const *const result) {
    return char_length(source) > 0 && string_get_data(result) == nullptr;
}
#endif // ARENA_IMPLEMENTATION

/*==============================================================================
 * MARK: - Cache API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
String http_headers_cache_alloc_no_cache(Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    String headers = _http_headers_string_init(allocator);

    _http_headers_string_add(&headers, "Cache-Control: no-cache, must-revalidate\r\n");
    _http_headers_string_add(&headers, "Pragma: no-cache\r\n");

    trace_log_pop();

    return headers;
}

String http_headers_cache_alloc_no_store(Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    String headers = _http_headers_string_init(allocator);

    _http_headers_string_add(&headers, "Cache-Control: no-store, max-age=0\r\n");
    _http_headers_string_add(&headers, "Pragma: no-cache\r\n");

    trace_log_pop();

    return headers;
}

String http_headers_cache_alloc_private(USize const max_age, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    String const headers = _http_headers_cache_max_age_create("private", max_age, false, allocator);

    trace_log_pop();

    return headers;
}

String http_headers_cache_alloc_public(USize const max_age, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    String const headers = _http_headers_cache_max_age_create("public", max_age, false, allocator);

    trace_log_pop();

    return headers;
}

String http_headers_cache_alloc_static(USize const max_age, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    String const headers = _http_headers_cache_max_age_create("public", max_age, true, allocator);

    trace_log_pop();

    return headers;
}
#endif // ARENA_IMPLEMENTATION

USize http_headers_cache_max_age_into(char *const buffer, USize const buffer_capacity, USize const max_age) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "buffer", (void*) buffer);

    /* Capacity 0 is a legal value (an empty destination), not an error: answer 0 without
     * touching buffer[0], since a zero-capacity buffer has no writable byte. */
    if (buffer_capacity == 0) {
        trace_log_pop();

        return 0;
    }

    I32 const written = snprintf(buffer, buffer_capacity, "Cache-Control: max-age=%zu\r\n", max_age);

    /* Refuse rather than truncate: a cut-off header is indistinguishable from a real one. */
    if (written < 0 || (USize) written >= buffer_capacity) {
        buffer[0] = '\0';

        trace_log_pop();

        return 0;
    }

    trace_log_pop();

    return (USize) written;
}

String http_headers_cache_no_cache(void) {
    trace_log_push(LOG_METADATA);

    String headers = _http_headers_string_init(nullptr);

    _http_headers_string_add(&headers, "Cache-Control: no-cache, must-revalidate\r\n");
    _http_headers_string_add(&headers, "Pragma: no-cache\r\n");

    trace_log_pop();

    return headers;
}

String http_headers_cache_no_store(void) {
    trace_log_push(LOG_METADATA);

    String headers = _http_headers_string_init(nullptr);

    _http_headers_string_add(&headers, "Cache-Control: no-store, max-age=0\r\n");
    _http_headers_string_add(&headers, "Pragma: no-cache\r\n");

    trace_log_pop();

    return headers;
}

String http_headers_cache_private(USize const max_age) {
    trace_log_push(LOG_METADATA);

    String const headers = _http_headers_cache_max_age_create("private", max_age, false, nullptr);

    trace_log_pop();

    return headers;
}

String http_headers_cache_public(USize const max_age) {
    trace_log_push(LOG_METADATA);

    String const headers = _http_headers_cache_max_age_create("public", max_age, false, nullptr);

    trace_log_pop();

    return headers;
}

String http_headers_cache_static(USize const max_age) {
    trace_log_push(LOG_METADATA);

    String const headers = _http_headers_cache_max_age_create("public", max_age, true, nullptr);

    trace_log_pop();

    return headers;
}

/*==============================================================================
 * MARK: - Content API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
String http_headers_content_alloc_disposition_attachment(char const *const filename, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "filename", (void*) filename);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    if (_http_headers_content_has_control(filename)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_headers_content_alloc_disposition_attachment: refusing a filename with a control character");

        String const empty = _http_headers_string_init(allocator);

        trace_log_pop();

        return empty;
    }

    String headers = _http_headers_string_init(allocator);

    _http_headers_string_add(&headers, "Content-Disposition: attachment; filename=\"");
    _http_headers_content_disposition_filename_add(&headers, filename);
    _http_headers_string_add(&headers, "\"\r\n");

    trace_log_pop();

    return headers;
}

String http_headers_content_alloc_html(Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    String const headers = http_headers_content_alloc_type("text/html; charset=utf-8", allocator);

    trace_log_pop();

    return headers;
}

String http_headers_content_alloc_json(Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    String const headers = http_headers_content_alloc_type("application/json; charset=utf-8", allocator);

    trace_log_pop();

    return headers;
}

String http_headers_content_alloc_length(USize const size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    String headers = _http_headers_string_init(allocator);

    _http_headers_string_add(&headers, "Content-Length: ");
    _http_headers_number_add(&headers, size);
    _http_headers_string_add(&headers, "\r\n");

    trace_log_pop();

    return headers;
}

String http_headers_content_alloc_type(char const *const mime, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mime", (void*) mime);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    if (_http_headers_content_has_control(mime)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_headers_content_alloc_type: refusing a mime type with a control character");

        String const empty = _http_headers_string_init(allocator);

        trace_log_pop();

        return empty;
    }

    String headers = _http_headers_string_init(allocator);

    _http_headers_string_add(&headers, "Content-Type: ");
    _http_headers_string_add(&headers, mime);
    _http_headers_string_add(&headers, "\r\n");

    trace_log_pop();

    return headers;
}
#endif // ARENA_IMPLEMENTATION

String http_headers_content_disposition_attachment(char const *const filename) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "filename", (void*) filename);

    String const empty_carrier = _http_headers_string_init(nullptr);

    if (_http_headers_content_has_control(filename)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_headers_content_disposition_attachment: refusing a filename with a control character");

        trace_log_pop();

        return empty_carrier;
    }

    String headers = empty_carrier;

    _http_headers_string_add(&headers, "Content-Disposition: attachment; filename=\"");
    _http_headers_content_disposition_filename_add(&headers, filename);
    _http_headers_string_add(&headers, "\"\r\n");

    trace_log_pop();

    return headers;
}

String http_headers_content_html(void) {
    trace_log_push(LOG_METADATA);

    String const headers = http_headers_content_type("text/html; charset=utf-8");

    trace_log_pop();

    return headers;
}

String http_headers_content_json(void) {
    trace_log_push(LOG_METADATA);

    String const headers = http_headers_content_type("application/json; charset=utf-8");

    trace_log_pop();

    return headers;
}

String http_headers_content_length(USize const size) {
    trace_log_push(LOG_METADATA);

    String headers = _http_headers_string_init(nullptr);

    _http_headers_string_add(&headers, "Content-Length: ");
    _http_headers_number_add(&headers, size);
    _http_headers_string_add(&headers, "\r\n");

    trace_log_pop();

    return headers;
}

String http_headers_content_type(char const *const mime) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mime", (void*) mime);

    String const empty_carrier = _http_headers_string_init(nullptr);

    if (_http_headers_content_has_control(mime)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_headers_content_type: refusing a mime type with a control character");

        trace_log_pop();

        return empty_carrier;
    }

    String headers = empty_carrier;

    _http_headers_string_add(&headers, "Content-Type: ");
    _http_headers_string_add(&headers, mime);
    _http_headers_string_add(&headers, "\r\n");

    trace_log_pop();

    return headers;
}

/*==============================================================================
 * MARK: - Security API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
bool http_headers_security_alloc_init_1(HTTP_Headers_Security *const self, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    bool const initialized = http_headers_security_alloc_init_2(self,
        HTTP_HEADERS_SECURITY_DEFAULT_CONTENT_SECURITY_POLICY, HTTP_HEADERS_SECURITY_DEFAULT_FRAME_OPTIONS, HTTP_HEADERS_SECURITY_DEFAULT_REFERRER_POLICY,
        HTTP_HEADERS_SECURITY_DEFAULT_PERMISSIONS_POLICY, HTTP_HEADERS_SECURITY_DEFAULT_CONTENT_TYPE_OPTIONS, HTTP_HEADERS_SECURITY_DEFAULT_CROSS_ORIGIN_EMBEDDER_POLICY,
        HTTP_HEADERS_SECURITY_DEFAULT_CROSS_ORIGIN_OPENER_POLICY, HTTP_HEADERS_SECURITY_DEFAULT_CROSS_ORIGIN_RESOURCE_POLICY, HTTP_HEADERS_SECURITY_DEFAULT_STRICT_TRANSPORT_SECURITY,
        HTTP_HEADERS_SECURITY_DEFAULT_STRICT_TRANSPORT_SECURITY_MAX_AGE, HTTP_HEADERS_SECURITY_DEFAULT_STRICT_TRANSPORT_SECURITY_INCLUDE_SUBDOMAINS,
        HTTP_HEADERS_SECURITY_DEFAULT_STRICT_TRANSPORT_SECURITY_PRELOAD, allocator);

    trace_log_pop();

    return initialized;
}

bool http_headers_security_alloc_init_2(HTTP_Headers_Security *const self,
    char const *const content_security_policy, char const *const frame_options, char const *const referrer_policy, char const *const permissions_policy, bool const content_type_options,
    bool const cross_origin_embedder_policy, bool const cross_origin_opener_policy, bool const cross_origin_resource_policy, bool const strict_transport_security,
    USize const strict_transport_security_max_age, bool const strict_transport_security_include_subdomains, bool const strict_transport_security_preload, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "content_security_policy", (void*) content_security_policy);
    error_check_null(LOG_METADATA, "frame_options", (void*) frame_options);
    error_check_null(LOG_METADATA, "referrer_policy", (void*) referrer_policy);
    error_check_null(LOG_METADATA, "permissions_policy", (void*) permissions_policy);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    /* Unlike the by-value init_2 twin, this tier CAN refuse - so a control byte in any policy
     * value refuses the WHOLE policy rather than silently omitting just that field's header
     * (a dropped-with-no-log-line header is exactly the fail-open shape this tier exists to
     * close). _http_headers_security_policy_has_control already logs the WARN for us. */
    if (_http_headers_security_policy_has_control("http_headers_security_alloc_init_2", content_security_policy, frame_options, permissions_policy, referrer_policy)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_headers_security_alloc_init_2: refusing the whole policy - a value contains a control byte");

        *self = (HTTP_Headers_Security) DEFAULT_INITIALIZATION;

        trace_log_pop();

        return false;
    }

    String const content_security_policy_string = _http_headers_char_to_string(content_security_policy, allocator);
    String const frame_options_string            = _http_headers_char_to_string(frame_options, allocator);
    String const permissions_policy_string       = _http_headers_char_to_string(permissions_policy, allocator);
    String const referrer_policy_string          = _http_headers_char_to_string(referrer_policy, allocator);

    if (_http_headers_security_string_refused(content_security_policy, &content_security_policy_string) ||
        _http_headers_security_string_refused(frame_options, &frame_options_string) ||
        _http_headers_security_string_refused(permissions_policy, &permissions_policy_string) ||
        _http_headers_security_string_refused(referrer_policy, &referrer_policy_string)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_headers_security_alloc_init_2: arena refused a policy value; refusing the whole policy");

        *self = (HTTP_Headers_Security) DEFAULT_INITIALIZATION;

        trace_log_pop();

        return false;
    }

    *self = (HTTP_Headers_Security) {
        .allocator                                      = allocator,
        .content_type_options                           = content_type_options,
        .cross_origin_embedder_policy                   = cross_origin_embedder_policy,
        .cross_origin_opener_policy                     = cross_origin_opener_policy,
        .cross_origin_resource_policy                   = cross_origin_resource_policy,
        .content_security_policy                        = content_security_policy_string,
        .frame_options                                  = frame_options_string,
        .permissions_policy                             = permissions_policy_string,
        .referrer_policy                                = referrer_policy_string,
        .strict_transport_security                      = strict_transport_security,
        .strict_transport_security_include_subdomains   = strict_transport_security_include_subdomains,
        .strict_transport_security_max_age              = strict_transport_security_max_age,
        .strict_transport_security_preload              = strict_transport_security_preload
    };

    trace_log_pop();

    return true;
}
#endif // ARENA_IMPLEMENTATION

String http_headers_security_create_1(void) {
    trace_log_push(LOG_METADATA);

    HTTP_Headers_Security security = http_headers_security_init_1();
    String headers = http_headers_security_create_2(&security);

    http_headers_security_uninit(&security);

    trace_log_pop();

    return headers;
}

String http_headers_security_create_2(HTTP_Headers_Security const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    String headers = _http_headers_string_init(self->allocator);

    _http_headers_security_header_add(&headers, "Content-Security-Policy: ", &self->content_security_policy);

    if (self->content_type_options) {
        _http_headers_string_add(&headers, "X-Content-Type-Options: nosniff\r\n");
    }

    _http_headers_security_header_add(&headers, "X-Frame-Options: ", &self->frame_options);
    _http_headers_security_header_add(&headers, "Referrer-Policy: ", &self->referrer_policy);
    _http_headers_security_header_add(&headers, "Permissions-Policy: ", &self->permissions_policy);

    if (self->cross_origin_opener_policy) {
        _http_headers_string_add(&headers, "Cross-Origin-Opener-Policy: same-origin\r\n");
    }

    if (self->cross_origin_embedder_policy) {
        _http_headers_string_add(&headers, "Cross-Origin-Embedder-Policy: require-corp\r\n");
    }

    if (self->cross_origin_resource_policy) {
        _http_headers_string_add(&headers, "Cross-Origin-Resource-Policy: same-origin\r\n");
    }

    _http_headers_security_hsts_add(self, &headers);

    /* Every line this function appends ends in "\r\n"; a non-empty block that does not is a
     * sign something upstream degraded mid-build (e.g. a starved arena truncating a copy) -
     * emit nothing rather than a malformed header block. Currently unreachable in practice
     * (every append above is a whole "...\r\n" literal or a value string.h refuses down to
     * EMPTY rather than truncates, per string.h's abort-not-truncate contract), but kept as
     * belt-and-braces: a future field that appends a bare value without its own CRLF would trip
     * this rather than ship a malformed block. */
    if (!string_empty(&headers) && !string_ends_with_1(&headers, "\r\n")) {
        log_message_2(LOG_LEVEL_ERROR, LOG_METADATA, "http_headers_security_create_2: refusing a malformed block that does not end in CRLF");

        string_uninit(&headers);

        String const refused = _http_headers_string_init(self->allocator);

        trace_log_pop();

        return refused;
    }

    trace_log_pop();

    return headers;
}

HTTP_Headers_Security http_headers_security_init_1(void) {
    trace_log_push(LOG_METADATA);

    HTTP_Headers_Security const security = http_headers_security_init_2(
        HTTP_HEADERS_SECURITY_DEFAULT_CONTENT_SECURITY_POLICY, HTTP_HEADERS_SECURITY_DEFAULT_FRAME_OPTIONS, HTTP_HEADERS_SECURITY_DEFAULT_REFERRER_POLICY,
        HTTP_HEADERS_SECURITY_DEFAULT_PERMISSIONS_POLICY, HTTP_HEADERS_SECURITY_DEFAULT_CONTENT_TYPE_OPTIONS, HTTP_HEADERS_SECURITY_DEFAULT_CROSS_ORIGIN_EMBEDDER_POLICY,
        HTTP_HEADERS_SECURITY_DEFAULT_CROSS_ORIGIN_OPENER_POLICY, HTTP_HEADERS_SECURITY_DEFAULT_CROSS_ORIGIN_RESOURCE_POLICY, HTTP_HEADERS_SECURITY_DEFAULT_STRICT_TRANSPORT_SECURITY,
        HTTP_HEADERS_SECURITY_DEFAULT_STRICT_TRANSPORT_SECURITY_MAX_AGE, HTTP_HEADERS_SECURITY_DEFAULT_STRICT_TRANSPORT_SECURITY_INCLUDE_SUBDOMAINS,
        HTTP_HEADERS_SECURITY_DEFAULT_STRICT_TRANSPORT_SECURITY_PRELOAD);

    trace_log_pop();

    return security;
}

HTTP_Headers_Security http_headers_security_init_2(
    char const *const content_security_policy, char const *const frame_options, char const *const referrer_policy, char const *const permissions_policy, bool const content_type_options,
    bool const cross_origin_embedder_policy, bool const cross_origin_opener_policy, bool const cross_origin_resource_policy, bool const strict_transport_security,
    USize const strict_transport_security_max_age, bool const strict_transport_security_include_subdomains, bool const strict_transport_security_preload) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "content_security_policy", (void*) content_security_policy);
    error_check_null(LOG_METADATA, "frame_options", (void*) frame_options);
    error_check_null(LOG_METADATA, "referrer_policy", (void*) referrer_policy);
    error_check_null(LOG_METADATA, "permissions_policy", (void*) permissions_policy);

    /* This by-value tier cannot refuse, so a control byte in a policy value logs LOG_LEVEL_WARN
     * and omits only that field's header (see http_headers_security_alloc_init_2 for the
     * refuse-whole tier). */
    char const *const content_security_policy_safe = _http_headers_security_field_safe("http_headers_security_init_2", "content_security_policy", content_security_policy);
    char const *const frame_options_safe            = _http_headers_security_field_safe("http_headers_security_init_2", "frame_options", frame_options);
    char const *const permissions_policy_safe       = _http_headers_security_field_safe("http_headers_security_init_2", "permissions_policy", permissions_policy);
    char const *const referrer_policy_safe          = _http_headers_security_field_safe("http_headers_security_init_2", "referrer_policy", referrer_policy);

    HTTP_Headers_Security const security = {
        .allocator                                      = nullptr,
        .content_type_options                           = content_type_options,
        .cross_origin_embedder_policy                   = cross_origin_embedder_policy,
        .cross_origin_opener_policy                     = cross_origin_opener_policy,
        .cross_origin_resource_policy                   = cross_origin_resource_policy,
        .content_security_policy                        = _http_headers_char_to_string(content_security_policy_safe, nullptr),
        .frame_options                                  = _http_headers_char_to_string(frame_options_safe, nullptr),
        .permissions_policy                             = _http_headers_char_to_string(permissions_policy_safe, nullptr),
        .referrer_policy                                = _http_headers_char_to_string(referrer_policy_safe, nullptr),
        .strict_transport_security                      = strict_transport_security,
        .strict_transport_security_include_subdomains   = strict_transport_security_include_subdomains,
        .strict_transport_security_max_age              = strict_transport_security_max_age,
        .strict_transport_security_preload              = strict_transport_security_preload
    };

    trace_log_pop();

    return security;
}

void http_headers_security_uninit(HTTP_Headers_Security *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    string_uninit(&self->content_security_policy);
    string_uninit(&self->frame_options);
    string_uninit(&self->permissions_policy);
    string_uninit(&self->referrer_policy);

    self->allocator                                     = nullptr;
    self->content_type_options                          = false;
    self->cross_origin_embedder_policy                  = false;
    self->cross_origin_opener_policy                    = false;
    self->cross_origin_resource_policy                  = false;
    self->strict_transport_security                     = false;
    self->strict_transport_security_include_subdomains  = false;
    self->strict_transport_security_max_age             = 0;
    self->strict_transport_security_preload             = false;

    trace_log_pop();
}