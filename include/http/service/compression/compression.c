/*
 * compression.c - HTTP response compression service implementation
 */

#include <brotli/encode.h>
#include <zlib.h>

/* char.h is included DIRECTLY, not left to a chain: compression.h reaches it only through
 * http_server.h, and http_server.h's API names no type from char.h - an ACCIDENTAL chain that
 * the next tidy of that header could remove. This file uses char_length, char_compare_iequal_2,
 * CHAR_END_CHARACTER and CHAR_STATIC_SIZE.
 *
 * char.h is singled out because it is the one module the chain rule can NEVER cover: it
 * declares no types at all, only functions and macros, so no header's API can name a type from
 * it and every chain reaching it is accidental by the rule's own test. Whatever calls a char_*
 * function includes char.h itself. */
#include <char/char.h>
#include <http/service/compression/compression.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

/* Longest Accept-Encoding value the send seam reads. Real clients send well under 100 bytes
 * ("gzip, deflate, br, zstd" is 23). A longer value is REFUSED whole by header_copy - it
 * returns false rather than handing back a truncated prefix - so the seam never negotiates
 * from half a header: it simply sends the response as identity. */
#define _HTTP_SERVICE_COMPRESSION_ACCEPT_ENCODING_MAX 256

/* zlib's default memLevel: 8 is the library's own compress() setting. */
#define _HTTP_SERVICE_COMPRESSION_MEMORY_LEVEL 8

/* Fixed-point q, so "0.85" compares as 850 without touching a float. RFC 9110 pins the
 * weight at three decimal places, which is exactly this scale. */
#define _HTTP_SERVICE_COMPRESSION_QUALITY_DEFAULT 1000
#define _HTTP_SERVICE_COMPRESSION_QUALITY_DIGITS  3
#define _HTTP_SERVICE_COMPRESSION_QUALITY_SCALE   1000
#define _HTTP_SERVICE_COMPRESSION_QUALITY_UNSET   (-1)

/* MAX_WBITS (15) + 16: the +16 is zlib's request for an RFC 1952 GZIP wrapper (1f 8b ...)
 * instead of the RFC 1950 zlib wrapper (78 9c ...) that deflateInit/compress produce. The
 * negotiated "gzip" token promises the former; anything else is a body the browser refuses
 * with ERR_CONTENT_DECODING_FAILED. */
#define _HTTP_SERVICE_COMPRESSION_WINDOW_BITS_GZIP (MAX_WBITS + 16)

/*==============================================================================
 * MARK: - Helpers
 *============================================================================*/

/* Zero *self whole. The documented failure contract of every constructor: a refused init
 * leaves an instance that does not exist, rather than a half-built one. */
static void _http_service_compression_reset(HTTP_Service_Compression *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    *self = (HTTP_Service_Compression) DEFAULT_INITIALIZATION;

    trace_log_pop();
}

/* Configuration validation. Value-dependent, so it REFUSES (false) rather than aborting:
 * a level outside the codec's range is a caller mistake worth reporting, not worth killing
 * the process over, and error_check_* would compile away under an unchecked build anyway. */
static bool _http_service_compression_config_valid(USize const min_size, USize const max_size, int const brotli_quality, int const gzip_level) {
    trace_log_push(LOG_METADATA);

    bool const valid = brotli_quality >= HTTP_SERVICE_COMPRESSION_MIN_BROTLI_QUALITY
        && brotli_quality <= HTTP_SERVICE_COMPRESSION_MAX_BROTLI_QUALITY
        && gzip_level >= HTTP_SERVICE_COMPRESSION_MIN_GZIP_LEVEL
        && gzip_level <= HTTP_SERVICE_COMPRESSION_MAX_GZIP_LEVEL
        && (max_size == 0 || max_size >= min_size);

    trace_log_pop();

    return valid;
}

/* The ONE allocation self->allocator still serves: an owned copy of a MIME prefix, written at
 * startup and released only by uninit. It is service-lifetime data, which is exactly what an
 * arena is for - unlike the codec buffers, which are per-response and stay on the heap. */
static Str _http_service_compression_char_to_str(HTTP_Service_Compression const *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_non_value_uint(LOG_METADATA, "data_size", data_size);

#ifdef ARENA_IMPLEMENTATION
    if (self->allocator != nullptr) {
        Str const str = str_alloc_init_static(data, data_size, self->allocator);

        trace_log_pop();

        return str;
    }
#endif // ARENA_IMPLEMENTATION

    Str const str = str_init_static(data, data_size);

    trace_log_pop();

    return str;
}

/* The span of a header value with leading and trailing optional whitespace removed. Both
 * ends move, so the caller gets back a start offset AND a size. */
static USize _http_service_compression_trim(char const *const text, USize const text_size, USize *const out_start) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "out_start", (void*) out_start);

    USize start = 0;
    USize end   = text_size;

    while (start < end && (text[start] == ' ' || text[start] == '\t')) {
        start += 1;
    }

    while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t')) {
        end -= 1;
    }

    *out_start = start;

    trace_log_pop();

    return end - start;
}

/* The essence of a Content-Type: everything before the first parameter, trimmed. Never
 * reports an error - a malformed header simply yields a span nothing matches. */
static USize _http_service_compression_type_essence(char const *const content_type, USize const content_type_size, USize *const out_start) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "out_start", (void*) out_start);

    USize essence_size = content_type_size;

    for (USize i = 0; i < content_type_size; i += 1) {
        if (content_type[i] == ';') {
            essence_size = i;

            break;
        }
    }

    USize const size = _http_service_compression_trim(content_type, essence_size, out_start);

    trace_log_pop();

    return size;
}

/* RFC 6839 structured suffixes: "application/vnd.api+json" and "image/svg+xml" are textual
 * whatever their vendor tree says, so they compress like the media types they extend. This
 * is why the allow-list does not have to enumerate every vendor JSON type in existence. */
static bool _http_service_compression_structured_suffix(char const *const essence, USize const essence_size) {
    trace_log_push(LOG_METADATA);

    bool suffixed = false;

    if (essence_size >= CHAR_STATIC_SIZE("+json")
        && char_compare_iequal_2(essence + essence_size - CHAR_STATIC_SIZE("+json"), CHAR_STATIC_SIZE("+json"), "+json", CHAR_STATIC_SIZE("+json"))) {
        suffixed = true;
    }
    else if (essence_size >= CHAR_STATIC_SIZE("+xml")
        && char_compare_iequal_2(essence + essence_size - CHAR_STATIC_SIZE("+xml"), CHAR_STATIC_SIZE("+xml"), "+xml", CHAR_STATIC_SIZE("+xml"))) {
        suffixed = true;
    }

    trace_log_pop();

    return suffixed;
}

static bool _http_service_compression_mime_allowed(HTTP_Service_Compression const *const self, char const *const content_type, USize const content_type_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (content_type == nullptr || content_type_size == 0) {
        trace_log_pop();

        return false;
    }

    USize   essence_start   = 0;
    USize   const essence_size = _http_service_compression_type_essence(content_type, content_type_size, &essence_start);
    char    const *const essence = content_type + essence_start;

    if (essence_size == 0) {
        trace_log_pop();

        return false;
    }

    bool allowed = _http_service_compression_structured_suffix(essence, essence_size);

    for (USize i = 0; !allowed && i < al_str_get_size(&self->mime_types); i += 1) {
        Str     const   *const  prefix      = al_str_at(&self->mime_types, i);
        USize   const           prefix_size = str_get_size(prefix);

        if (prefix_size > 0 && essence_size >= prefix_size && char_compare_iequal_2(essence, prefix_size, str_get_data(prefix), prefix_size)) {
            allowed = true;
        }
    }

    trace_log_pop();

    return allowed;
}

/* One Accept-Encoding weight, as fixed-point thousandths. Returns UNSET for anything the
 * grammar does not allow, and the caller treats that as "do not use this coding" - a
 * malformed weight is never read as an enthusiastic one. */
static I32 _http_service_compression_quality_parse(char const *const text, USize const text_size) {
    trace_log_push(LOG_METADATA);

    USize       start   = 0;
    USize const size    = _http_service_compression_trim(text, text_size, &start);

    if (size == 0) {
        trace_log_pop();

        return _HTTP_SERVICE_COMPRESSION_QUALITY_UNSET;
    }

    char const *const value = text + start;

    if (value[0] != '0' && value[0] != '1') {
        trace_log_pop();

        return _HTTP_SERVICE_COMPRESSION_QUALITY_UNSET;
    }

    I32 quality = (value[0] - '0') * _HTTP_SERVICE_COMPRESSION_QUALITY_SCALE;

    if (size == 1) {
        trace_log_pop();

        return quality;
    }

    if (value[1] != '.' || size > 2 + _HTTP_SERVICE_COMPRESSION_QUALITY_DIGITS) {
        trace_log_pop();

        return _HTTP_SERVICE_COMPRESSION_QUALITY_UNSET;
    }

    I32 place = _HTTP_SERVICE_COMPRESSION_QUALITY_SCALE / 10;

    for (USize i = 2; i < size; i += 1) {
        if (value[i] < '0' || value[i] > '9') {
            trace_log_pop();

            return _HTTP_SERVICE_COMPRESSION_QUALITY_UNSET;
        }

        quality += (value[i] - '0') * place;
        place /= 10;
    }

    if (quality > _HTTP_SERVICE_COMPRESSION_QUALITY_SCALE) {
        trace_log_pop();

        return _HTTP_SERVICE_COMPRESSION_QUALITY_UNSET;
    }

    trace_log_pop();

    return quality;
}

/* The weight one token carries: the part after the first ";q=" parameter, or the default
 * when the token has none. An unparsable weight excludes the coding. */
static I32 _http_service_compression_token_quality(char const *const token, USize const token_size, USize *const out_name_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "out_name_size", (void*) out_name_size);

    USize name_size = token_size;

    for (USize i = 0; i < token_size; i += 1) {
        if (token[i] == ';') {
            name_size = i;

            break;
        }
    }

    *out_name_size = name_size;

    I32 quality = _HTTP_SERVICE_COMPRESSION_QUALITY_DEFAULT;
    USize cursor = name_size;

    while (cursor < token_size) {
        USize const parameter_start = cursor + 1;
        USize       parameter_end   = token_size;

        for (USize i = parameter_start; i < token_size; i += 1) {
            if (token[i] == ';') {
                parameter_end = i;

                break;
            }
        }

        USize       trimmed_start   = 0;
        USize const trimmed_size    = _http_service_compression_trim(token + parameter_start, parameter_end - parameter_start, &trimmed_start);
        char const  *const parameter = token + parameter_start + trimmed_start;

        if (trimmed_size >= CHAR_STATIC_SIZE("q=") && char_compare_iequal_2(parameter, CHAR_STATIC_SIZE("q="), "q=", CHAR_STATIC_SIZE("q="))) {
            quality = _http_service_compression_quality_parse(parameter + CHAR_STATIC_SIZE("q="), trimmed_size - CHAR_STATIC_SIZE("q="));

            break;
        }

        cursor = parameter_end;
    }

    trace_log_pop();

    return quality;
}

/*
 * Tokenized Accept-Encoding. Splits on ',', reads each coding name case-insensitively and
 * its optional ";q=" weight, and answers the best coding this service can actually produce.
 *
 * Deliberately total: every refusal is NONE and no error_check touches the text, because
 * this is untrusted request data and a header must never be able to abort the server.
 */
static HTTP_Service_Compression_Type _http_service_compression_accepted(char const *const accept_encoding, USize const accept_encoding_size) {
    trace_log_push(LOG_METADATA);

    I32 brotli_quality  = _HTTP_SERVICE_COMPRESSION_QUALITY_UNSET;
    I32 gzip_quality    = _HTTP_SERVICE_COMPRESSION_QUALITY_UNSET;
    I32 wildcard_quality = _HTTP_SERVICE_COMPRESSION_QUALITY_UNSET;
    USize cursor        = 0;

    while (cursor < accept_encoding_size) {
        USize token_end = accept_encoding_size;

        for (USize i = cursor; i < accept_encoding_size; i += 1) {
            if (accept_encoding[i] == ',') {
                token_end = i;

                break;
            }
        }

        USize       token_start = 0;
        USize const token_size  = _http_service_compression_trim(accept_encoding + cursor, token_end - cursor, &token_start);
        char const  *const token = accept_encoding + cursor + token_start;

        if (token_size > 0) {
            USize       name_size   = 0;
            I32 const   quality     = _http_service_compression_token_quality(token, token_size, &name_size);
            USize       name_start  = 0;

            name_size = _http_service_compression_trim(token, name_size, &name_start);

            char const *const name = token + name_start;

            if (name_size > 0 && quality != _HTTP_SERVICE_COMPRESSION_QUALITY_UNSET) {
                if (char_compare_iequal_2(name, name_size, "br", CHAR_STATIC_SIZE("br"))) {
                    brotli_quality = quality;
                }
                else if (char_compare_iequal_2(name, name_size, "gzip", CHAR_STATIC_SIZE("gzip"))
                    || char_compare_iequal_2(name, name_size, "x-gzip", CHAR_STATIC_SIZE("x-gzip"))) {
                    gzip_quality = quality;
                }
                else if (char_compare_iequal_2(name, name_size, "*", CHAR_STATIC_SIZE("*"))) {
                    wildcard_quality = quality;
                }
            }
        }

        cursor = token_end + 1;
    }

    /* A coding the client never named inherits the wildcard's weight, or 0 when there is no
     * wildcard: "identity" alone, "gzip;q=0" and an empty header all land here as NONE. An
     * EXPLICIT q=0 is never resurrected by a wildcard - the client said no to that one. */
    if (brotli_quality == _HTTP_SERVICE_COMPRESSION_QUALITY_UNSET) {
        brotli_quality = wildcard_quality == _HTTP_SERVICE_COMPRESSION_QUALITY_UNSET ? 0 : wildcard_quality;
    }

    if (gzip_quality == _HTTP_SERVICE_COMPRESSION_QUALITY_UNSET) {
        gzip_quality = wildcard_quality == _HTTP_SERVICE_COMPRESSION_QUALITY_UNSET ? 0 : wildcard_quality;
    }

    HTTP_Service_Compression_Type type = HTTP_SERVICE_COMPRESSION_TYPE_NONE;

    if (brotli_quality > 0 && brotli_quality >= gzip_quality) {
        type = HTTP_SERVICE_COMPRESSION_TYPE_BROTLI;
    }
    else if (gzip_quality > 0) {
        type = HTTP_SERVICE_COMPRESSION_TYPE_GZIP;
    }

    trace_log_pop();

    return type;
}

/* ceil(log2(data_size)), bounded by Brotli's own window range. A 4 KiB reply then asks the
 * encoder for a 4 KiB window instead of the 4 MiB one BROTLI_DEFAULT_WINDOW allocates, at
 * no measurable cost to the ratio (see the header's measurement block). */
static int _http_service_compression_window_bits(USize const data_size) {
    trace_log_push(LOG_METADATA);

    int bits = BROTLI_MIN_WINDOW_BITS;

    while (bits < BROTLI_DEFAULT_WINDOW && ((USize) 1 << bits) < data_size) {
        bits += 1;
    }

    trace_log_pop();

    return bits;
}

/* THE CODEC BUFFERS ARE ALWAYS HEAP-OWNED - string_init_2, never string_alloc_init_2 - whatever
 * tier the service itself was built on, and so is the String each codec hands back. The buffer
 * is per-RESPONSE and as wide as max_size (8 MiB by default), while self->allocator is
 * SERVICE-lifetime: a linear Arena reclaims nothing on string_uninit, so routing the codec
 * through it would make a long-lived server retain every body it ever compressed, and two
 * concurrent compress calls would bump one Arena that is not thread-safe. The allocator backs
 * the MIME allow-list - configuration, written once at startup - and nothing else. */
static bool _http_service_compression_compress_gzip(HTTP_Service_Compression const *const self, Byte const *const data, USize const data_size, String *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_null(LOG_METADATA, "out", (void*) out);

    /* zlib's avail_in/avail_out are uInt (32-bit on every supported target), so a payload
     * past UINT_MAX cannot be handed to the stream at all - and deflateBound's own answer
     * has to fit the same field. Both are refused up front rather than truncated. */
    if (data_size > (USize) UINT_MAX) {
        trace_log_pop();

        return false;
    }

    z_stream stream = DEFAULT_INITIALIZATION;

    if (deflateInit2(&stream, self->gzip_level, Z_DEFLATED, _HTTP_SERVICE_COMPRESSION_WINDOW_BITS_GZIP,
            _HTTP_SERVICE_COMPRESSION_MEMORY_LEVEL, Z_DEFAULT_STRATEGY) != Z_OK) {
        trace_log_pop();

        return false;
    }

    uLong const bound = deflateBound(&stream, (uLong) data_size);

    /* A bound below the input is deflateBound's wrap; USIZE_MAX would wrap the +1 below. */
    if (bound > (uLong) UINT_MAX || (USize) bound < data_size || (USize) bound == USIZE_MAX) {
        deflateEnd(&stream);

        trace_log_pop();

        return false;
    }

    /* + 1, in USize: string_set_size refuses a size that leaves no terminator slot, and a
     * worst-case (incompressible) input fills the bound exactly. */
    String compressed = string_init_2((USize) bound + CHAR_END_CHARACTER);

    /* Live in exactly ONE build mode, and not dead in it: string_init_2 reaches memory_alloc,
     * whose error_check_null ABORTS under ERROR_CHECK_ENABLED but returns null without it. This
     * is the framework-wide memory_alloc shape rather than anything local, so the check stays. */
    if (string_get_data(&compressed) == nullptr) {
        string_uninit(&compressed);
        deflateEnd(&stream);

        trace_log_pop();

        return false;
    }

    stream.next_in      = (Bytef*) data;
    stream.avail_in     = (uInt) data_size;
    stream.next_out     = (Bytef*) string_get_data(&compressed);
    stream.avail_out    = (uInt) bound;

    int     const   result      = deflate(&stream, Z_FINISH);
    USize   const   produced    = (USize) stream.total_out;

    deflateEnd(&stream);

    /* Z_OK here would mean the bound was not enough for a single Z_FINISH pass, which
     * deflateBound guarantees cannot happen - so anything but Z_STREAM_END is a failure. */
    if (result != Z_STREAM_END || produced == 0 || produced >= data_size) {
        string_uninit(&compressed);

        trace_log_pop();

        return false;
    }

    string_set_size(&compressed, produced);

    *out = compressed;

    trace_log_pop();

    return true;
}

static bool _http_service_compression_compress_brotli(HTTP_Service_Compression const *const self, Byte const *const data, USize const data_size, String *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_null(LOG_METADATA, "out", (void*) out);

    size_t const bound = BrotliEncoderMaxCompressedSize(data_size);

    /* Brotli answers a bound of 0 when the input size overflows its arithmetic: refuse it as
     * the out-of-range result rather than allocate a one-byte buffer for a compression that
     * cannot succeed. USIZE_MAX would wrap the +1 below. */
    if (bound == 0 || (USize) bound == USIZE_MAX) {
        trace_log_pop();

        return false;
    }

    String compressed = string_init_2((USize) bound + CHAR_END_CHARACTER);

    /* Reachable only in an UNCHECKED build, for the reason the gzip path spells out above. */
    if (string_get_data(&compressed) == nullptr) {
        string_uninit(&compressed);

        trace_log_pop();

        return false;
    }

    size_t              produced    = bound;
    BROTLI_BOOL const   result      = BrotliEncoderCompress(self->brotli_quality, _http_service_compression_window_bits(data_size),
        BROTLI_DEFAULT_MODE, data_size, (uint8_t const*) data, &produced, (uint8_t*) string_get_data(&compressed));

    if (result != BROTLI_TRUE || produced == 0 || (USize) produced >= data_size) {
        string_uninit(&compressed);

        trace_log_pop();

        return false;
    }

    string_set_size(&compressed, (USize) produced);

    *out = compressed;

    trace_log_pop();

    return true;
}

/* Whether a payload of this type and size is a compression CANDIDATE at all - the test the
 * send seam also uses to decide whether Vary must go out on the identity path. */
static bool _http_service_compression_candidate(HTTP_Service_Compression const *const self, char const *const content_type,
    USize const content_type_size, USize const content_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    bool const candidate = content_size >= self->min_size
        && (self->max_size == 0 || content_size <= self->max_size)
        && _http_service_compression_mime_allowed(self, content_type, content_type_size);

    trace_log_pop();

    return candidate;
}

/* The compressible types every deployment wants. Each add's bool is HONOURED: a refused
 * default means the service would silently compress nothing of that type, so the whole
 * construction is refused instead. */
static bool _http_service_compression_add_defaults(HTTP_Service_Compression *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    static char const *const defaults[] = {
        "text/",
        "application/javascript",
        "application/json",
        "application/manifest+json",
        "application/wasm",
        "application/xml",
        "image/svg+xml"
    };

    for (USize i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i += 1) {
        if (!http_service_compression_mime_type_add(self, defaults[i])) {
            trace_log_pop();

            return false;
        }
    }

    trace_log_pop();

    return true;
}

/*==============================================================================
 * MARK: - API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
bool http_service_compression_alloc_init_1(HTTP_Service_Compression *const self, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    bool const initialized = http_service_compression_alloc_init_2(self, HTTP_SERVICE_COMPRESSION_DEFAULT_MIN_SIZE,
        HTTP_SERVICE_COMPRESSION_DEFAULT_MAX_SIZE, HTTP_SERVICE_COMPRESSION_DEFAULT_BROTLI_QUALITY,
        HTTP_SERVICE_COMPRESSION_DEFAULT_GZIP_LEVEL, allocator);

    trace_log_pop();

    return initialized;
}

bool http_service_compression_alloc_init_2(HTTP_Service_Compression *const self, USize const min_size, USize const max_size,
    int const brotli_quality, int const gzip_level, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    if (!_http_service_compression_config_valid(min_size, max_size, brotli_quality, gzip_level)) {
        _http_service_compression_reset(self);

        trace_log_pop();

        return false;
    }

    *self = (HTTP_Service_Compression) {
        .allocator      = allocator,
        .brotli_quality = brotli_quality,
        .gzip_level     = gzip_level,
        .max_size       = max_size,
        .min_size       = min_size,
        .mime_types     = al_str_alloc_init_1(allocator)
    };

    if (!_http_service_compression_add_defaults(self)) {
        al_str_uninit(&self->mime_types);
        _http_service_compression_reset(self);

        trace_log_pop();

        return false;
    }

    trace_log_pop();

    return true;
}
#endif // ARENA_IMPLEMENTATION

bool http_service_compression_compress(HTTP_Service_Compression const *const self, HTTP_Service_Compression_Type const type,
    Byte const *const data, USize const data_size, String *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "out", (void*) out);

    /* The EMPTY String owns no buffer, so it costs nothing on either tier and string_uninit on
     * it is a no-op; it is built without self->allocator so that `out` is heap-owned on EVERY
     * path, and a caller never has to ask which constructor built the service. */
    *out = string_init_1();

    if (data == nullptr || data_size == 0) {
        trace_log_pop();

        return false;
    }

    /* The same cap negotiate applies, enforced on the RAW tier too: max_size is documented as
     * what keeps a legal multi-GiB body from being held twice, and a caller reaching past the
     * seam would otherwise allocate the codec's bound for exactly the body it forbids. */
    if (self->max_size != 0 && data_size > self->max_size) {
        trace_log_pop();

        return false;
    }

    /* Zeroed, not the empty String: both codecs write *out only on success and release their
     * own buffer on failure, so there is nothing here to allocate or release first. */
    String  compressed      = DEFAULT_INITIALIZATION;
    bool    compressed_ok   = false;

    if (type == HTTP_SERVICE_COMPRESSION_TYPE_BROTLI) {
        compressed_ok = _http_service_compression_compress_brotli(self, data, data_size, &compressed);
    }
    else if (type == HTTP_SERVICE_COMPRESSION_TYPE_GZIP) {
        compressed_ok = _http_service_compression_compress_gzip(self, data, data_size, &compressed);
    }

    if (!compressed_ok) {
        string_uninit(&compressed);

        trace_log_pop();

        return false;
    }

    /* Assigned, not released first: `out` is WRITE-ONLY by contract, and on every path that
     * reaches here it still holds the EMPTY String written above, so a string_uninit would be a
     * guaranteed no-op that only suggests this function releases what the caller passed in. It
     * does not - see the @param out block in the header. */
    *out = compressed;

    trace_log_pop();

    return true;
}

bool http_service_compression_init_1(HTTP_Service_Compression *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    bool const initialized = http_service_compression_init_2(self, HTTP_SERVICE_COMPRESSION_DEFAULT_MIN_SIZE,
        HTTP_SERVICE_COMPRESSION_DEFAULT_MAX_SIZE, HTTP_SERVICE_COMPRESSION_DEFAULT_BROTLI_QUALITY,
        HTTP_SERVICE_COMPRESSION_DEFAULT_GZIP_LEVEL);

    trace_log_pop();

    return initialized;
}

bool http_service_compression_init_2(HTTP_Service_Compression *const self, USize const min_size, USize const max_size,
    int const brotli_quality, int const gzip_level) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (!_http_service_compression_config_valid(min_size, max_size, brotli_quality, gzip_level)) {
        _http_service_compression_reset(self);

        trace_log_pop();

        return false;
    }

    *self = (HTTP_Service_Compression) {
#ifdef ARENA_IMPLEMENTATION
        .allocator      = nullptr,
#endif // ARENA_IMPLEMENTATION
        .brotli_quality = brotli_quality,
        .gzip_level     = gzip_level,
        .max_size       = max_size,
        .min_size       = min_size,
        .mime_types     = al_str_init_1()
    };

    if (!_http_service_compression_add_defaults(self)) {
        al_str_uninit(&self->mime_types);
        _http_service_compression_reset(self);

        trace_log_pop();

        return false;
    }

    trace_log_pop();

    return true;
}

bool http_service_compression_mime_type_add(HTTP_Service_Compression *const self, char const *const mime_type) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "mime_type", (void*) mime_type);

    USize const mime_type_size = char_length(mime_type);

    /* An empty prefix is CONFIGURATION data, not a contract violation: it would sit in the
     * list matching every type while looking like a deliberate entry, so it is refused
     * unconditionally rather than aborted through error_check_non_value_uint. */
    if (mime_type_size == 0) {
        trace_log_pop();

        return false;
    }

    Str str = _http_service_compression_char_to_str(self, mime_type, mime_type_size);

    /* The copy degrades to the EMPTY Str when the allocator refuses it. */
    if (str_get_size(&str) == 0) {
        str_uninit(&str);

        trace_log_pop();

        return false;
    }

    USize const stored_before = al_str_get_size(&self->mime_types);

    al_str_add_last(&self->mime_types, &str);

    /* add_last DECLINES rather than growing when the allocator refuses, and reports it by
     * leaving the size alone. `str` owns a fresh COPY of `mime_type`, so a dropped node puts
     * that copy beyond mime_types' uninit. */
    if (al_str_get_size(&self->mime_types) == stored_before) {
        str_uninit(&str);

        trace_log_pop();

        return false;
    }

    trace_log_pop();

    return true;
}

HTTP_Service_Compression_Type http_service_compression_negotiate_1(HTTP_Service_Compression const *const self,
    char const *const accept_encoding, char const *const content_type, USize const content_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (accept_encoding == nullptr || content_type == nullptr) {
        trace_log_pop();

        return HTTP_SERVICE_COMPRESSION_TYPE_NONE;
    }

    HTTP_Service_Compression_Type const type = http_service_compression_negotiate_2(self, accept_encoding, char_length(accept_encoding),
        content_type, char_length(content_type), content_size);

    trace_log_pop();

    return type;
}

HTTP_Service_Compression_Type http_service_compression_negotiate_2(HTTP_Service_Compression const *const self,
    char const *const accept_encoding, USize const accept_encoding_size, char const *const content_type,
    USize const content_type_size, USize const content_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (accept_encoding == nullptr || accept_encoding_size == 0) {
        trace_log_pop();

        return HTTP_SERVICE_COMPRESSION_TYPE_NONE;
    }

    if (!_http_service_compression_candidate(self, content_type, content_type_size, content_size)) {
        trace_log_pop();

        return HTTP_SERVICE_COMPRESSION_TYPE_NONE;
    }

    HTTP_Service_Compression_Type const type = _http_service_compression_accepted(accept_encoding, accept_encoding_size);

    trace_log_pop();

    return type;
}

HTTP_Service_Compression_Type http_service_compression_negotiate_3(HTTP_Service_Compression const *const self,
    Str const *const accept_encoding, Str const *const content_type, USize const content_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "accept_encoding", (void*) accept_encoding);
    error_check_null(LOG_METADATA, "content_type", (void*) content_type);

    /* The EMPTY Str carries a null data pointer, which negotiate_2 answers as NONE. */
    HTTP_Service_Compression_Type const type = http_service_compression_negotiate_2(self, str_get_data(accept_encoding),
        str_get_size(accept_encoding), str_get_data(content_type), str_get_size(content_type), content_size);

    trace_log_pop();

    return type;
}

bool http_service_compression_send(HTTP_Service_Compression const *const self, HTTP_Server_Request *const request,
    HTTP_Server_Response *const response, Byte const *const data, USize const data_size, char const *const content_type,
    U16 const status_code) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "request", (void*) request);
    error_check_null(LOG_METADATA, "response", (void*) response);
    error_check_null(LOG_METADATA, "content_type", (void*) content_type);

    USize   const   content_type_size   = char_length(content_type);
    bool    const   candidate           = _http_service_compression_candidate(self, content_type, content_type_size, data_size);

    /* RFC 9110 14.4: a Content-Range offset indexes the SELECTED representation, and
     * Content-Encoding is what makes the coded bytes that representation - so a compressed
     * slice labelled `bytes a-b/n` of the identity file is a body no client can reassemble.
     * The refusal lives HERE rather than in each caller because the seam is public: static's
     * Range path already routes through it, and any future caller would repeat the mistake.
     * Vary is still emitted below - it is about the representation, not this one reply. */
    bool const codeable = status_code != HTTP_SERVER_STATUS_CODE_PARTIAL_CONTENT;

    HTTP_Service_Compression_Type type = HTTP_SERVICE_COMPRESSION_TYPE_NONE;

    if (candidate && codeable && data != nullptr) {
        char accept_encoding[_HTTP_SERVICE_COMPRESSION_ACCEPT_ENCODING_MAX] = DEFAULT_INITIALIZATION;

        if (http_server_request_header_copy(request, HTTP_SERVER_HEADER_ACCEPT_ENCODING, accept_encoding, sizeof(accept_encoding))) {
            /* _accepted, not negotiate_2: negotiate_2's whole job beyond this call is the size and
             * MIME test that `candidate` above ALREADY ran, so going through it would rescan the
             * allow-list on the hot path of every response for an answer we hold. Its two other
             * guards need no replacement here - accept_encoding is this stack buffer, never null,
             * and _accepted answers NONE for an empty one on its own: no token names a coding and
             * none supplies a wildcard, so both weights end at 0. */
            type = _http_service_compression_accepted(accept_encoding, char_length(accept_encoding));
        }
    }

    /* Cache correctness: a compressible representation VARIES by Accept-Encoding whether or
     * not this particular client asked for a coding, so the header goes out on the identity
     * path too. Without it a shared cache can hand one client's stored body to the other. */
    if (candidate) {
        http_server_response_header_add(response, "Vary", "Accept-Encoding");
    }

    bool sent_compressed = false;

    if (type != HTTP_SERVICE_COMPRESSION_TYPE_NONE) {
        String compressed = DEFAULT_INITIALIZATION;

        if (http_service_compression_compress(self, type, data, data_size, &compressed)
            && http_server_response_header_add(response, "Content-Encoding", http_service_compression_type_name(type))) {
            http_server_response_send_2(response, (Byte const*) string_get_data(&compressed), string_get_size(&compressed),
                content_type, status_code);

            sent_compressed = true;
        }

        string_uninit(&compressed);
    }

    if (!sent_compressed) {
        http_server_response_send_2(response, data, data_size, content_type, status_code);
    }

    trace_log_pop();

    return sent_compressed;
}

char* http_service_compression_type_name(HTTP_Service_Compression_Type const type) {
    trace_log_push(LOG_METADATA);

    char *name = (char*) "identity";

    if (type == HTTP_SERVICE_COMPRESSION_TYPE_BROTLI) {
        name = (char*) "br";
    }
    else if (type == HTTP_SERVICE_COMPRESSION_TYPE_GZIP) {
        name = (char*) "gzip";
    }

    trace_log_pop();

    return name;
}

void http_service_compression_uninit(HTTP_Service_Compression *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    al_str_uninit(&self->mime_types);

    _http_service_compression_reset(self);

    trace_log_pop();
}