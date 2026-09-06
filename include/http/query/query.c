#include <http/query/query.h>

/* Staging buffer the encoder fills before handing bytes to the String. Sized so the flush
 * cost amortizes away (one string_add_last_2 per ~64-192 input bytes instead of one per
 * byte) while still living on the stack; measured on a 64 KiB binary value, 11 interleaved
 * warmed rounds, the chunked form is 5.2-6.5x faster than the per-byte appends. Must be at
 * least 3, the widest single encoding ("%XX"). */
#define _HTTP_QUERY_ENCODE_CHUNK_SIZE 192

/*==============================================================================
 * MARK: - Private Functions
 *============================================================================*/

// RFC 3986 "unreserved" set - the only bytes that survive percent-encoding untouched, in
// both the plain and the application/x-www-form-urlencoded flavour.
static bool _http_query_char_unreserved(char const value) {
    if (value >= 'A' && value <= 'Z') {
        return true;
    }

    if (value >= 'a' && value <= 'z') {
        return true;
    }

    if (value >= '0' && value <= '9') {
        return true;
    }

    return value == '-' || value == '.' || value == '_' || value == '~';
}

// Argument gate shared by every writing-side entry point. A null String handle, a null
// value over a non-zero size, and an over-cap size are all VALUE refusals (false), never
// an error_check abort - the standard for data-dependent inputs. The cap also keeps the
// worst-case 3x expansion bounded. A value that lives inside out's own allocation is
// refused too: the first append that grows the String releases the buffer the loop is
// still reading, so the alias window is the whole capacity, not just the used size, and
// the test is an INTERVAL overlap - a slice starting before the buffer but running into it
// aliases exactly as dangerously as one starting inside it. A zero-size slice reads no
// bytes at all, so it is answered legal before the alias test ever runs.
static bool _http_query_encode_valid(String const *const out, char const *const value, USize const value_size) {
    if (out == nullptr) {
        return false;
    }

    if (value_size > HTTP_QUERY_ENCODE_MAX_SIZE) {
        return false;
    }

    if (value_size == 0) {
        return true;
    }

    if (value == nullptr) {
        return false;
    }

    char const *const data = string_get_data(out);

    if (data == nullptr) {
        return true;
    }

    // Compared as integers, not pointers: an ordered comparison of pointers into two
    // unrelated objects is undefined behaviour in C, and this test exists precisely to find
    // out whether they ARE the same object.
    uintptr_t const value_start = (uintptr_t) value;
    uintptr_t const data_start  = (uintptr_t) data;

    return value_start >= data_start + string_get_capacity(out) || value_start + value_size <= data_start;
}

// Shared encoder for http_query_encode_2 and both halves of a pair. plus_for_space selects
// the application/x-www-form-urlencoded convention (' ' -> '+'); with it false a space
// encodes as "%20", per RFC 3986. Appends only - callers validate first, so this never
// refuses. Output is staged in a stack chunk and flushed a chunk at a time rather than a
// byte at a time; the emitted bytes are identical either way.
static void _http_query_encode(String *const out, char const *const value, USize const value_size, bool const plus_for_space) {
    trace_log_push(LOG_METADATA);

    char const *const hex_digits                           = "0123456789ABCDEF";
    char              chunk[_HTTP_QUERY_ENCODE_CHUNK_SIZE] = DEFAULT_INITIALIZATION;
    USize             used                                 = 0;

    for (USize i = 0; i < value_size; i += 1) {
        // Flushed BEFORE the byte is examined, never after it is written: the widest single
        // encoding is three bytes, so the chunk must be known to hold three before the
        // branch below picks one.
        if (used + 3 > sizeof(chunk)) {
            string_add_last_2(out, chunk, used);

            used = 0;
        }

        if (_http_query_char_unreserved(value[i])) {
            chunk[used] = value[i];
            used += 1;

            continue;
        }

        if (plus_for_space && value[i] == ' ') {
            chunk[used] = '+';
            used += 1;

            continue;
        }

        U8 const byte = (U8) value[i];

        chunk[used]     = '%';
        chunk[used + 1] = hex_digits[(byte >> 4) & 0x0F];
        chunk[used + 2] = hex_digits[byte & 0x0F];
        used += 3;
    }

    if (used > 0) {
        string_add_last_2(out, chunk, used);
    }

    trace_log_pop();
}

// Shared pair builder behind http_query_add and http_query_form_add: identical but for the
// space convention, which is the whole difference between a query string and a form body.
static bool _http_query_pair_add(String *const out, char const *const name, USize const name_size, char const *const value, USize const value_size, bool const plus_for_space) {
    trace_log_push(LOG_METADATA);

    // A nameless pair is not a field; both sides are gated before ANY byte is appended, so
    // a refusal never leaves a half-written "name=" behind in the caller's String.
    if (name_size == 0 || !_http_query_encode_valid(out, name, name_size) || !_http_query_encode_valid(out, value, value_size)) {
        trace_log_pop();

        return false;
    }

    USize const       size = string_get_size(out);
    char const *const data = string_get_data(out);
    char const        last = size > 0 ? data[size - 1] : '\0';

    /* The "&" joins one pair to the NEXT one, so it is written only where a pair already
     * ends. A destination seeded with everything up to and including the '?'
     * ("https://example.com/search?") is non-empty but has nothing to join onto, and a
     * caller who wrote the '&' themselves must not get a second one - both used to spell
     * "?&q=..." / "&&q=...". Empty, '?'-terminated and '&'-terminated destinations all take
     * no separator; everything else does. */
    bool const separator_needed = size > 0 && last != '?' && last != '&';

    /* One reserve for the WHOLE pair - worst-case 3x expansion of both sides, plus the "&",
     * the "=" and the terminator - then a capacity check. A refused arena grows nothing, and
     * appending piece by piece into one would land the separator, the name and the "=" and
     * then silently drop the value while still answering true, against a header that
     * promises a whole pair or an untouched destination. Reserving up front makes the
     * refusal whole, and on a live allocator it removes every mid-pair realloc. */
    USize const needed = size + 2 + 3 * (name_size + value_size) + CHAR_END_CHARACTER;

    string_reserve(out, needed);

    if (string_get_capacity(out) < needed) {
        trace_log_pop();

        return false;
    }

    if (separator_needed) {
        string_add_last_2(out, "&", CHAR_STATIC_SIZE("&"));
    }

    _http_query_encode(out, name, name_size, plus_for_space);
    string_add_last_2(out, "=", CHAR_STATIC_SIZE("="));
    _http_query_encode(out, value, value_size, plus_for_space);

    trace_log_pop();

    return true;
}

/*==============================================================================
 * MARK: - Public API Implementations
 *============================================================================*/

// The four _1 wrappers push no trace frame of their own: each measures its arguments and
// hands the whole operation to the _2 twin, which pushes one. A second frame naming the same
// call would only double the entries a diagnostic stack has to read.
bool http_query_add_1(String *const out, char const *const name, char const *const value) {
    if (name == nullptr || value == nullptr) {
        return false;
    }

    return http_query_add_2(out, name, char_length(name), value, char_length(value));
}

bool http_query_add_2(String *const out, char const *const name, USize const name_size, char const *const value, USize const value_size) {
    return _http_query_pair_add(out, name, name_size, value, value_size, false);
}

bool http_query_encode_1(String *const out, char const *const value) {
    if (value == nullptr) {
        return false;
    }

    return http_query_encode_2(out, value, char_length(value));
}

bool http_query_encode_2(String *const out, char const *const value, USize const value_size) {
    trace_log_push(LOG_METADATA);

    if (!_http_query_encode_valid(out, value, value_size)) {
        trace_log_pop();

        return false;
    }

    /* The same whole-or-nothing reserve the pair builder makes: the encoder flushes its
     * staging chunk once per 192 output bytes, so on a refused arena a long value would land
     * its first flush, lose every later one and still answer true. Skipped at a zero size,
     * where nothing is appended and an empty value is a legal VALUE that must answer true
     * even when the destination could not grow. */
    if (value_size > 0) {
        USize const needed = string_get_size(out) + 3 * value_size + CHAR_END_CHARACTER;

        string_reserve(out, needed);

        if (string_get_capacity(out) < needed) {
            trace_log_pop();

            return false;
        }
    }

    _http_query_encode(out, value, value_size, false);

    trace_log_pop();

    return true;
}

bool http_query_form_add_1(String *const body, char const *const name, char const *const value) {
    if (name == nullptr || value == nullptr) {
        return false;
    }

    return http_query_form_add_2(body, name, char_length(name), value, char_length(value));
}

bool http_query_form_add_2(String *const body, char const *const name, USize const name_size, char const *const value, USize const value_size) {
    return _http_query_pair_add(body, name, name_size, value, value_size, true);
}

#ifdef ARENA_IMPLEMENTATION

/*==============================================================================
 * MARK: - Private Functions (Arena Tier)
 *============================================================================*/

// Shared decode core for http_query_alloc_decode(_2) and
// http_query_alloc_form_decode(_2). plus_as_space selects the form-encoding
// convention ("+" -> space); out_size, when non-null, receives the exact
// decoded byte count, so a decoded "%00" is not silently truncated away by a
// later char_length on the result.
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
// of key_size or key's own NUL comes first, so a caller-supplied key_size longer than key's
// real extent fails closed instead of reading past it. On a match, *out_value
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
// answers nullptr, same as a missing key: callers never
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
 * MARK: - Public API Implementations (Arena Tier)
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