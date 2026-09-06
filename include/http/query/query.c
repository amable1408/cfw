#include <http/query/query.h>

#ifdef ARENA_IMPLEMENTATION

/*==============================================================================
 * MARK: - Private Functions
 *============================================================================*/

// Shared decode core for http_query_alloc_decode(_2) and
// http_query_alloc_form_decode(_2). plus_as_space selects the form-encoding
// convention ("+" -> space); out_size, when non-null, receives the exact
// decoded byte count (item 12: a decoded "%00" must not be silently
// truncated away by a later char_length on the result).
static char* _http_query_alloc_decode(char const *const encoded, USize const encoded_size, bool const plus_as_space, USize *const out_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    if (memory_empty(encoded)) {
        trace_log_pop();

        return nullptr;
    }

    // Hardening: encoded_size this close to USIZE_MAX cannot be a real query string, but
    // refuse it explicitly rather than let "+ CHAR_END_CHARACTER" below wrap to a tiny
    // allocation that the decode loop then overruns.
    if (encoded_size > USIZE_MAX - 2) {
        trace_log_pop();

        return nullptr;
    }

    // "+ CHAR_END_CHARACTER" both sidesteps char_alloc_new_1's own zero-size abort when
    // encoded_size is 0 and gives one byte of slack: decoding only ever shrinks or holds
    // the input's length, never grows it.
    char *const decoded = char_alloc_new_1(encoded_size + CHAR_END_CHARACTER, allocator);

    if (decoded == nullptr) {
        trace_log_pop();

        return nullptr;
    }

    USize in = 0;
    USize out = 0;

    while (in < encoded_size) {
        if (encoded[in] == '%' && in + 2 < encoded_size) {
            U8 const hi_nibble = char_raw_to_hex(encoded[in + 1]);
            U8 const lo_nibble = char_raw_to_hex(encoded[in + 2]);

            if (hi_nibble != 0xFF && lo_nibble != 0xFF) {
                decoded[out] = (char) ((hi_nibble << 4) | lo_nibble);
                in += 3;
                out += 1;

                continue;
            }
        }

        // A malformed "%" escape (invalid hex, or too few bytes left) is not decoded -
        // it passes through as literal bytes rather than refusing the whole string.
        decoded[out] = (plus_as_space && encoded[in] == '+') ? ' ' : encoded[in];
        in += 1;
        out += 1;
    }

    decoded[out] = '\0';

    if (out_size != nullptr) {
        *out_size = out;
    }

    trace_log_pop();

    return decoded;
}

// Scans query[0..query_size) for key[0..key_size), stopping the comparison at whichever
// of key_size or key's own NUL comes first - a caller-supplied key_size longer than key's
// real extent used to read past it (the old :72 over-read; Mid 11). On a match, *out_value
// / *out_value_size describe the raw (undecoded) value slice, which may be empty ("key=").
static bool _http_query_find(char const *const query, USize const query_size, char const *const key, USize const key_size, char const **const out_value, USize *const out_value_size) {
    trace_log_push(LOG_METADATA);

    char const *const query_end = query + query_size;
    char const *pos = (query < query_end && *query == '?') ? query + 1 : query;

    while (pos < query_end && *pos != '\0') {
        USize i = 0;

        while (i < key_size && key[i] != '\0' && pos + i < query_end && pos[i] != '\0' && pos[i] == key[i]) {
            i += 1;
        }

        if (i == key_size && pos + key_size < query_end && pos[key_size] == '=') {
            char const *const value_start = pos + key_size + 1;
            char const *value_end = value_start;

            while (value_end < query_end && *value_end != '\0' && *value_end != '&') {
                value_end += 1;
            }

            *out_value = value_start;
            *out_value_size = (USize) (value_end - value_start);

            trace_log_pop();

            return true;
        }

        while (pos < query_end && *pos != '\0' && *pos != '&') {
            pos += 1;
        }

        if (pos < query_end && *pos == '&') {
            pos += 1;
        }
    }

    trace_log_pop();

    return false;
}

// Shared lookup+allocate core for http_query_alloc_get_1..5. An empty value ("key=")
// answers nullptr, same as a missing key (Mid 13, pinned in the header): callers never
// special-case one against the other, and char_alloc_new_3 aborts on a zero size anyway.
static char* _http_query_alloc_get(char const *const query, USize const query_size, char const *const key, USize const key_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    if (memory_empty(query) || memory_empty(key) || key_size == 0) {
        trace_log_pop();

        return nullptr;
    }

    char const *value = nullptr;
    USize value_size = 0;

    bool const found = _http_query_find(query, query_size, key, key_size, &value, &value_size);

    trace_log_pop();

    if (!found || value_size == 0) {
        return nullptr;
    }

    return char_alloc_new_3(value, value_size, allocator);
}

/*==============================================================================
 * MARK: - Public API Implementations
 *============================================================================*/

char* http_query_alloc_decode(char const *const encoded, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char *const decoded = memory_empty(encoded) ? nullptr : _http_query_alloc_decode(encoded, char_length(encoded), false, nullptr, allocator);

    trace_log_pop();

    return decoded;
}

char* http_query_alloc_decode_2(char const *const encoded, USize const encoded_size, USize *const out_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "out_size", (void*) out_size);

    char *const decoded = _http_query_alloc_decode(encoded, encoded_size, false, out_size, allocator);

    trace_log_pop();

    return decoded;
}

char* http_query_alloc_form_decode(char const *const encoded, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char *const decoded = memory_empty(encoded) ? nullptr : _http_query_alloc_decode(encoded, char_length(encoded), true, nullptr, allocator);

    trace_log_pop();

    return decoded;
}

char* http_query_alloc_form_decode_2(char const *const encoded, USize const encoded_size, USize *const out_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "out_size", (void*) out_size);

    char *const decoded = _http_query_alloc_decode(encoded, encoded_size, true, out_size, allocator);

    trace_log_pop();

    return decoded;
}

char* http_query_alloc_get_1(char const *const query, char const *const key, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char *const value = memory_empty(key) ? nullptr : http_query_alloc_get_2(query, key, char_length(key), allocator);

    trace_log_pop();

    return value;
}

char* http_query_alloc_get_2(char const *const query, char const *const key, USize const key_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char *const value = memory_empty(query) ? nullptr : _http_query_alloc_get(query, char_length(query), key, key_size, allocator);

    trace_log_pop();

    return value;
}

char* http_query_alloc_get_3(char const *const query, Str const *const key, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "key", (void*) key);

    char *const value = http_query_alloc_get_2(query, str_get_data(key), str_get_size(key), allocator);

    trace_log_pop();

    return value;
}

char* http_query_alloc_get_4(char const *const query, String const *const key, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "key", (void*) key);

    char *const value = http_query_alloc_get_2(query, string_get_data(key), string_get_size(key), allocator);

    trace_log_pop();

    return value;
}

char* http_query_alloc_get_5(char const *const query, USize const query_size, char const *const key, USize const key_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char *const value = _http_query_alloc_get(query, query_size, key, key_size, allocator);

    trace_log_pop();

    return value;
}

#endif // ARENA_IMPLEMENTATION