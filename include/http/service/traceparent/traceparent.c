#include <http/service/traceparent/traceparent.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

/** @brief Offset of the two flag digits inside a version-00 value. */
#define _HTTP_SERVICE_TRACEPARENT_FLAGS_OFFSET (_HTTP_SERVICE_TRACEPARENT_PARENT_ID_OFFSET + HTTP_SERVICE_TRACEPARENT_PARENT_ID_SIZE + 1)
/** @brief Offset of the parent id inside a version-00 value. */
#define _HTTP_SERVICE_TRACEPARENT_PARENT_ID_OFFSET (_HTTP_SERVICE_TRACEPARENT_TRACE_ID_OFFSET + HTTP_SERVICE_TRACEPARENT_TRACE_ID_SIZE + 1)
/** @brief Offset of the trace id inside a version-00 value. */
#define _HTTP_SERVICE_TRACEPARENT_TRACE_ID_OFFSET (HTTP_SERVICE_TRACEPARENT_VERSION_SIZE + 1)

/*==============================================================================
 * MARK: - Helpers
 *============================================================================*/

/* Lowercase hex only: the spec fixes the case, so "00-ABCD..." is invalid, not tolerated. */
static bool _http_service_traceparent_hex_valid(char const *const data, USize const data_size, bool const reject_zero) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    bool valid      = data_size > 0;
    bool non_zero   = false;

    for (USize i = 0; valid && i < data_size; i += 1) {
        char const value = data[i];

        valid = (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');

        if (value != '0') {
            non_zero = true;
        }
    }

    if (reject_zero && !non_zero) {
        valid = false;
    }

    trace_log_pop();

    return valid;
}

/* One id maker over crypto_random_hex, where there used to be a bytes-plus-hand-rolled-hex
 * pair per id. W3C forbids an all-zero id, so an (astronomically unlikely) all-zero draw is
 * nudged on the hex side - the same result as nudging the byte before encoding it. */
static bool _http_service_traceparent_id_create(char *const out, USize const byte_count) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "out", (void*) out);
    error_check_non_value_uint(LOG_METADATA, "byte_count", byte_count);

    out[0] = '\0';

    if (result_is_error(crypto_random_hex(out, byte_count))) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_traceparent: the CSPRNG refused - no trace id could be minted");

        trace_log_pop();

        return false;
    }

    USize   const   size        = ENCODING_HEX_SIZE(byte_count);
    bool            non_zero    = false;

    for (USize i = 0; i < size; i += 1) {
        if (out[i] != '0') {
            non_zero = true;

            break;
        }
    }

    if (!non_zero) {
        out[size - 1] = '1';
    }

    trace_log_pop();

    return true;
}

static void _http_service_traceparent_string_add(String *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    USize const data_size = char_length(data);

    if (data_size > 0) {
        string_add_last_2(self, (char*) data, data_size);
    }

    trace_log_pop();
}

/* Carried verbatim, but never unbounded: a hop that forwards whatever arrives is an
 * amplifier, and a control byte in a forwarded header is a header break. */
static bool _http_service_traceparent_tracestate_valid(char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    if (data_size == 0 || data_size > HTTP_SERVICE_TRACEPARENT_TRACESTATE_MAX_SIZE) {
        trace_log_pop();

        return false;
    }

    USize   members = 1;
    bool    valid   = true;

    for (USize i = 0; valid && i < data_size; i += 1) {
        unsigned char const value = (unsigned char) data[i];

        /* Control bytes (0x00-0x1F, 0x7F) are refused; bytes at or above 0x80 pass as
         * obs-text - RFC 9110's own field-content grammar leaves that range legal, and
         * tracestate is carried verbatim, not re-encoded. */
        valid = value >= 0x20 && value != 0x7F;

        if (value == ',') {
            members += 1;
        }
    }

    if (members > HTTP_SERVICE_TRACEPARENT_TRACESTATE_MAX_MEMBERS) {
        valid = false;
    }

    trace_log_pop();

    return valid;
}

static void _http_service_traceparent_tracestate_set(HTTP_Service_Traceparent_Context *const out, char const *const tracestate) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "out", (void*) out);

    out->tracestate[0] = '\0';

    /* A null tracestate is the ordinary "the peer sent no such header" case, answered by
     * carrying nothing - never an abort: this pointer comes off the network. */
    if (tracestate == nullptr) {
        trace_log_pop();

        return;
    }

    USize const tracestate_size = char_length(tracestate);

    if (tracestate_size == 0) {
        trace_log_pop();

        return;
    }

    if (!_http_service_traceparent_tracestate_valid(tracestate, tracestate_size)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA,
            "http_service_traceparent: inbound tracestate dropped - over %d bytes, over %d members, or carrying a control byte",
            HTTP_SERVICE_TRACEPARENT_TRACESTATE_MAX_SIZE, HTTP_SERVICE_TRACEPARENT_TRACESTATE_MAX_MEMBERS);

        trace_log_pop();

        return;
    }

    char_copy_3(out->tracestate, HTTP_SERVICE_TRACEPARENT_TRACESTATE_CAPACITY, tracestate, tracestate_size);

    trace_log_pop();
}

/*==============================================================================
 * MARK: - API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
bool http_service_traceparent_alloc_init_1(HTTP_Service_Traceparent *const self, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    bool const success = http_service_traceparent_alloc_init_2(self, HTTP_SERVICE_TRACEPARENT_DEFAULT_HEADER_NAME, HTTP_SERVICE_TRACEPARENT_DEFAULT_TRACE_FLAGS, allocator);

    trace_log_pop();

    return success;
}

bool http_service_traceparent_alloc_init_2(HTTP_Service_Traceparent *const self, char const *const header_name, U8 const trace_flags, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "header_name", (void*) header_name);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    bool const success = http_service_traceparent_init_2(self, header_name, trace_flags);

    if (success) {
        self->allocator = allocator;
    }

    trace_log_pop();

    return success;
}
#endif // ARENA_IMPLEMENTATION

bool http_service_traceparent_child_create_1(HTTP_Service_Traceparent const *const self, char const *const value, HTTP_Service_Traceparent_Context *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "out", (void*) out);

    bool const success = http_service_traceparent_child_create_2(self, value, nullptr, out);

    trace_log_pop();

    return success;
}

bool http_service_traceparent_child_create_2(HTTP_Service_Traceparent const *const self, char const *const value, char const *const tracestate, HTTP_Service_Traceparent_Context *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "out", (void*) out);

    HTTP_Service_Traceparent_Context parent = DEFAULT_INITIALIZATION;

    /* An unusable inbound value RESTARTS the trace, which also drops the tracestate: those
     * vendor entries referred to a trace id nothing downstream will ever see again. */
    if (!http_service_traceparent_context_parse_2(self, value, tracestate, &parent)) {
        /* DEBUG, never WARN: restarting an unusable inbound trace is the spec's answer, not
         * a fault - but an operator staring at two disconnected traces needs to see WHERE
         * the chain broke, and this is the only place that knows. */
        log_message_2(LOG_LEVEL_DEBUG, LOG_METADATA, "http_service_traceparent: the inbound traceparent was unusable - a new root trace was started");

        bool const created = http_service_traceparent_context_create(self, out);

        trace_log_pop();

        return created;
    }

    *out = parent;

    if (!_http_service_traceparent_id_create(out->parent_id, HTTP_SERVICE_TRACEPARENT_PARENT_ID_BYTE_COUNT)) {
        *out = (HTTP_Service_Traceparent_Context) DEFAULT_INITIALIZATION;

        trace_log_pop();

        return false;
    }

    trace_log_pop();

    return true;
}

bool http_service_traceparent_context_create(HTTP_Service_Traceparent const *const self, HTTP_Service_Traceparent_Context *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "out", (void*) out);

    *out = (HTTP_Service_Traceparent_Context) DEFAULT_INITIALIZATION;

    /* The entropy failure used to be invisible: the append was skipped, the context came
     * back empty, and header_create then answered an empty header with no WARN anywhere. */
    if (!_http_service_traceparent_id_create(out->trace_id, HTTP_SERVICE_TRACEPARENT_TRACE_ID_BYTE_COUNT) ||
        !_http_service_traceparent_id_create(out->parent_id, HTTP_SERVICE_TRACEPARENT_PARENT_ID_BYTE_COUNT)) {
        *out = (HTTP_Service_Traceparent_Context) DEFAULT_INITIALIZATION;

        trace_log_pop();

        return false;
    }

    out->trace_flags = self->trace_flags;

    trace_log_pop();

    return true;
}

bool http_service_traceparent_context_parse_1(HTTP_Service_Traceparent const *const self, char const *const value, HTTP_Service_Traceparent_Context *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "out", (void*) out);

    bool const success = http_service_traceparent_context_parse_2(self, value, nullptr, out);

    trace_log_pop();

    return success;
}

bool http_service_traceparent_context_parse_2(HTTP_Service_Traceparent const *const self, char const *const value, char const *const tracestate, HTTP_Service_Traceparent_Context *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "out", (void*) out);

    *out = (HTTP_Service_Traceparent_Context) DEFAULT_INITIALIZATION;

    if (!http_service_traceparent_valid_1(value)) {
        trace_log_pop();

        return false;
    }

    char_copy_3(out->trace_id, sizeof(out->trace_id), value + _HTTP_SERVICE_TRACEPARENT_TRACE_ID_OFFSET, HTTP_SERVICE_TRACEPARENT_TRACE_ID_SIZE);
    char_copy_3(out->parent_id, sizeof(out->parent_id), value + _HTTP_SERVICE_TRACEPARENT_PARENT_ID_OFFSET, HTTP_SERVICE_TRACEPARENT_PARENT_ID_SIZE);

    U8 flags = 0;

    if (result_is_error(encoding_hex_decode_1(value + _HTTP_SERVICE_TRACEPARENT_FLAGS_OFFSET, HTTP_SERVICE_TRACEPARENT_TRACE_FLAGS_SIZE, &flags, 1))) {
        flags = 0;
    }

    out->trace_flags = flags;

    _http_service_traceparent_tracestate_set(out, tracestate);

    trace_log_pop();

    return true;
}

bool http_service_traceparent_context_sampled(HTTP_Service_Traceparent_Context const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    bool const sampled = (self->trace_flags & HTTP_SERVICE_TRACEPARENT_FLAG_SAMPLED) != 0;

    trace_log_pop();

    return sampled;
}

bool http_service_traceparent_context_valid(HTTP_Service_Traceparent_Context const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    bool const trace_id_valid   = char_length(self->trace_id) == HTTP_SERVICE_TRACEPARENT_TRACE_ID_SIZE &&
        _http_service_traceparent_hex_valid(self->trace_id, HTTP_SERVICE_TRACEPARENT_TRACE_ID_SIZE, true);
    bool const parent_id_valid  = char_length(self->parent_id) == HTTP_SERVICE_TRACEPARENT_PARENT_ID_SIZE &&
        _http_service_traceparent_hex_valid(self->parent_id, HTTP_SERVICE_TRACEPARENT_PARENT_ID_SIZE, true);
    bool const success          = trace_id_valid && parent_id_valid;

    trace_log_pop();

    return success;
}

String http_service_traceparent_header_create(HTTP_Service_Traceparent const *const self, HTTP_Service_Traceparent_Context const *const context) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "context", (void*) context);

    /* The "arena if this service has one, heap if it does not" branch is
     * string_init_optional's job now; the ifdef remains only because the allocator FIELD is
     * compiled out with ARENA_IMPLEMENTATION, so there is nothing to pass without it. */
#ifdef ARENA_IMPLEMENTATION
    String header = string_init_optional(self->allocator);
#else
    String header = string_init_1();
#endif // ARENA_IMPLEMENTATION

    char value[HTTP_SERVICE_TRACEPARENT_VALUE_CAPACITY] = DEFAULT_INITIALIZATION;

    if (!http_service_traceparent_value_write_1(context, value)) {
        trace_log_pop();

        return header;
    }

    /* The whole block's size is KNOWN before a byte is written, and that is the only test
     * that works: string_add_2 refuses a growth WHOLLY, so a dropped 55-byte value followed
     * by a "\r\n" that fits the existing slack ships "traceparent: \r\n" - well-formed,
     * empty, and every peer downstream restarts the trace. An empty String says "untraced",
     * which the caller can see; a half-written header does not. */
    USize expected = char_length(self->header_name) + CHAR_STATIC_SIZE(": ") +
        HTTP_SERVICE_TRACEPARENT_VALUE_SIZE + CHAR_STATIC_SIZE("\r\n");

    _http_service_traceparent_string_add(&header, self->header_name);
    _http_service_traceparent_string_add(&header, ": ");
    _http_service_traceparent_string_add(&header, value);
    _http_service_traceparent_string_add(&header, "\r\n");

    /* The tracestate line always carries the SPEC name: the header_name knob renames the
     * traceparent only, and a peer looking for vendor state looks for "tracestate". */
    if (context->tracestate[0] != '\0') {
        expected += CHAR_STATIC_SIZE(HTTP_SERVICE_TRACEPARENT_TRACESTATE_HEADER_NAME) + CHAR_STATIC_SIZE(": ") +
            char_length(context->tracestate) + CHAR_STATIC_SIZE("\r\n");

        _http_service_traceparent_string_add(&header, HTTP_SERVICE_TRACEPARENT_TRACESTATE_HEADER_NAME);
        _http_service_traceparent_string_add(&header, ": ");
        _http_service_traceparent_string_add(&header, context->tracestate);
        _http_service_traceparent_string_add(&header, "\r\n");
    }

    if (string_get_size(&header) != expected) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_traceparent: the allocator refused part of the header block - an empty block is emitted instead");

        string_clear(&header);
    }

    trace_log_pop();

    return header;
}

bool http_service_traceparent_init_1(HTTP_Service_Traceparent *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    bool const success = http_service_traceparent_init_2(self, HTTP_SERVICE_TRACEPARENT_DEFAULT_HEADER_NAME, HTTP_SERVICE_TRACEPARENT_DEFAULT_TRACE_FLAGS);

    trace_log_pop();

    return success;
}

bool http_service_traceparent_init_2(HTTP_Service_Traceparent *const self, char const *const header_name, U8 const trace_flags) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "header_name", (void*) header_name);

    *self = (HTTP_Service_Traceparent) DEFAULT_INITIALIZATION;

    USize const header_name_size = char_length(header_name);

    /* Validated HERE, once, rather than trusted on every emit: a name carrying a colon,
     * a space or CR/LF would put a break into every header block this service writes. */
    if (!http_headers_token_valid_2(header_name, header_name_size) ||
        header_name_size + CHAR_END_CHARACTER > HTTP_SERVICE_TRACEPARENT_HEADER_NAME_CAPACITY) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA,
            "http_service_traceparent: header name refused - empty, over %d bytes, or not an HTTP token", HTTP_SERVICE_TRACEPARENT_HEADER_NAME_CAPACITY - CHAR_END_CHARACTER);

        trace_log_pop();

        return false;
    }

    char_copy_3(self->header_name, HTTP_SERVICE_TRACEPARENT_HEADER_NAME_CAPACITY, header_name, header_name_size);

    self->trace_flags = trace_flags;

    trace_log_pop();

    return true;
}

void http_service_traceparent_uninit(HTTP_Service_Traceparent *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* Nothing to release since the header name became fixed storage; the teardown half of
     * init stays so the service's lifecycle reads the same as every other one's. */
    *self = (HTTP_Service_Traceparent) DEFAULT_INITIALIZATION;

    trace_log_pop();
}

bool http_service_traceparent_valid_1(char const *const value) {
    trace_log_push(LOG_METADATA);

    /* A null value is the ordinary "no inbound header" case - answered false, never an
     * abort. These bytes are network input, and refusing them is the whole job. */
    if (value == nullptr) {
        trace_log_pop();

        return false;
    }

    bool const success = http_service_traceparent_valid_2(value, char_length(value));

    trace_log_pop();

    return success;
}

bool http_service_traceparent_valid_2(char const *const value, USize const value_size) {
    trace_log_push(LOG_METADATA);

    if (value == nullptr || value_size < HTTP_SERVICE_TRACEPARENT_VALUE_SIZE) {
        trace_log_pop();

        return false;
    }

    /* Forward compatibility, per the spec's Versioning section: an unknown version is read
     * with the version-00 layout, and a longer value is only accepted when byte 55 is the
     * '-' that starts the fields this version does not know. "ff" is reserved and invalid.
     * Every future-version peer used to have its trace restarted here. */
    bool const length_valid     = value_size == HTTP_SERVICE_TRACEPARENT_VALUE_SIZE || value[HTTP_SERVICE_TRACEPARENT_VALUE_SIZE] == '-';
    bool const separators_valid = length_valid &&
        value[HTTP_SERVICE_TRACEPARENT_VERSION_SIZE] == '-' &&
        value[_HTTP_SERVICE_TRACEPARENT_PARENT_ID_OFFSET - 1] == '-' &&
        value[_HTTP_SERVICE_TRACEPARENT_FLAGS_OFFSET - 1] == '-';
    bool const version_valid    = separators_valid &&
        _http_service_traceparent_hex_valid(value, HTTP_SERVICE_TRACEPARENT_VERSION_SIZE, false) &&
        !(value[0] == 'f' && value[1] == 'f');
    bool const trace_id_valid   = version_valid && _http_service_traceparent_hex_valid(value + _HTTP_SERVICE_TRACEPARENT_TRACE_ID_OFFSET, HTTP_SERVICE_TRACEPARENT_TRACE_ID_SIZE, true);
    bool const parent_id_valid  = trace_id_valid && _http_service_traceparent_hex_valid(value + _HTTP_SERVICE_TRACEPARENT_PARENT_ID_OFFSET, HTTP_SERVICE_TRACEPARENT_PARENT_ID_SIZE, true);
    bool const success          = parent_id_valid && _http_service_traceparent_hex_valid(value + _HTTP_SERVICE_TRACEPARENT_FLAGS_OFFSET, HTTP_SERVICE_TRACEPARENT_TRACE_FLAGS_SIZE, false);

    trace_log_pop();

    return success;
}

String http_service_traceparent_value_create(HTTP_Service_Traceparent const *const self, HTTP_Service_Traceparent_Context const *const context) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "context", (void*) context);

#ifdef ARENA_IMPLEMENTATION
    String value = string_init_optional(self->allocator);
#else
    String value = string_init_1();
#endif // ARENA_IMPLEMENTATION

    char buffer[HTTP_SERVICE_TRACEPARENT_VALUE_CAPACITY] = DEFAULT_INITIALIZATION;

    if (!http_service_traceparent_value_write_1(context, buffer)) {
        trace_log_pop();

        return value;
    }

    string_add_last_2(&value, buffer, HTTP_SERVICE_TRACEPARENT_VALUE_SIZE);

    /* Whole-or-nothing, the header_create rule at one line: a refused append leaves the
     * EMPTY String rather than a value short of its 55 bytes. */
    if (string_get_size(&value) != HTTP_SERVICE_TRACEPARENT_VALUE_SIZE) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_traceparent: the allocator refused the value - an empty String is returned instead");

        string_clear(&value);
    }

    trace_log_pop();

    return value;
}

bool http_service_traceparent_value_write_1(HTTP_Service_Traceparent_Context const *const self, char *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "out", (void*) out);

    out[0] = '\0';

    if (!http_service_traceparent_context_valid(self)) {
        trace_log_pop();

        return false;
    }

    char flags[ENCODING_HEX_SIZE(1) + CHAR_END_CHARACTER] = DEFAULT_INITIALIZATION;

    encoding_hex_encode_1(&self->trace_flags, 1, flags);

    char_copy_2(out, HTTP_SERVICE_TRACEPARENT_VERSION, HTTP_SERVICE_TRACEPARENT_VERSION_SIZE);

    out[HTTP_SERVICE_TRACEPARENT_VERSION_SIZE] = '-';

    char_copy_2(out + _HTTP_SERVICE_TRACEPARENT_TRACE_ID_OFFSET, self->trace_id, HTTP_SERVICE_TRACEPARENT_TRACE_ID_SIZE);

    out[_HTTP_SERVICE_TRACEPARENT_PARENT_ID_OFFSET - 1] = '-';

    char_copy_2(out + _HTTP_SERVICE_TRACEPARENT_PARENT_ID_OFFSET, self->parent_id, HTTP_SERVICE_TRACEPARENT_PARENT_ID_SIZE);

    out[_HTTP_SERVICE_TRACEPARENT_FLAGS_OFFSET - 1] = '-';

    char_copy_2(out + _HTTP_SERVICE_TRACEPARENT_FLAGS_OFFSET, flags, HTTP_SERVICE_TRACEPARENT_TRACE_FLAGS_SIZE);

    out[HTTP_SERVICE_TRACEPARENT_VALUE_SIZE] = '\0';

    trace_log_pop();

    return true;
}

bool http_service_traceparent_value_write_2(HTTP_Service_Traceparent_Context const *const self, char *const out, USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "out", (void*) out);

    /* Refused before a single byte is touched: a caller buffer sized to the wrong constant
     * is exactly the bug this sized tier exists to catch, not to overrun past. */
    if (capacity < HTTP_SERVICE_TRACEPARENT_VALUE_CAPACITY) {
        trace_log_pop();

        return false;
    }

    bool const success = http_service_traceparent_value_write_1(self, out);

    trace_log_pop();

    return success;
}