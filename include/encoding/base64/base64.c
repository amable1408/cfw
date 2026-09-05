/* ============================================================================
 *  Encoding Base64 Implementation
 *  --------------------------------------------------------------------------
 *  @file    base64.c
 *  @brief   Standard and URL-safe base64 codec, table-based, no heap.
 * ============================================================================
 */
#include <encoding/base64/base64.h>

/*==============================================================================
 * MARK: - Internal Constants
 *============================================================================*/

/** @brief Reverse-table sentinel marking a char outside the alphabet. */
#define _ENCODING_BASE64_INVALID ((U8) 255)

/** @brief Entry count of a reverse-lookup table (one entry per possible byte). */
#define _ENCODING_BASE64_REVERSE_TABLE_SIZE 256

/** @brief Standard RFC 4648 §4 alphabet (`+` `/`, `=` padding on encode). */
static char const _ENCODING_BASE64_STD_ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/** @brief URL-safe RFC 4648 §5 alphabet (`-` `_`, never padded). */
static char const _ENCODING_BASE64_URL_ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

/** @brief Reverse lookup for the standard alphabet: table[ch] is the 6-bit
 *  value of ch, or _ENCODING_BASE64_INVALID when ch is not a member of the
 *  alphabet. */
static U8 const _encoding_base64_std_reverse[_ENCODING_BASE64_REVERSE_TABLE_SIZE] = {
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,  62, 255, 255, 255,  63,
     52,  53,  54,  55,  56,  57,  58,  59,  60,  61, 255, 255, 255, 255, 255, 255,
    255,   0,   1,   2,   3,   4,   5,   6,   7,   8,   9,  10,  11,  12,  13,  14,
     15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25, 255, 255, 255, 255, 255,
    255,  26,  27,  28,  29,  30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,
     41,  42,  43,  44,  45,  46,  47,  48,  49,  50,  51, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
};

/** @brief Reverse lookup for the URL-safe alphabet: table[ch] is the 6-bit
 *  value of ch, or _ENCODING_BASE64_INVALID when ch is not a member of the
 *  alphabet. */
static U8 const _encoding_base64_url_reverse[_ENCODING_BASE64_REVERSE_TABLE_SIZE] = {
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,  62, 255, 255,
     52,  53,  54,  55,  56,  57,  58,  59,  60,  61, 255, 255, 255, 255, 255, 255,
    255,   0,   1,   2,   3,   4,   5,   6,   7,   8,   9,  10,  11,  12,  13,  14,
     15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25, 255, 255, 255, 255,  63,
    255,  26,  27,  28,  29,  30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,
     41,  42,  43,  44,  45,  46,  47,  48,  49,  50,  51, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
};

/*==============================================================================
 * MARK: - Internal Implementations
 *============================================================================*/

/** @brief The bad-input Result shared by every decode failure path: a bad
 *  length, a stray or missing padding character, a character outside the
 *  alphabet, nonzero leftover bits in a partial final group, or a decoded
 *  size that exceeds output_capacity. Decode failures are plain bad input,
 *  not a policy deviation. */
static Result _encoding_base64_argument(void) {
    return result_make(RESULT_CATEGORY_ARGUMENT, 0, 0);
}

/** @brief Shared strict decode core for both alphabets once trailing padding
 *  has been stripped/validated by the caller: data holds data_length chars to
 *  decode via reverse, which may end in a partial final group of 2 or 3
 *  characters (never 1). Any '=' remaining in the span is rejected here via
 *  the reverse table (255) — callers rely on that instead of pre-scanning.
 *  Also rejects an out-of-alphabet character, a decoded size over
 *  output_capacity (checked before any byte is written), or nonzero
 *  leftover bits in a partial final group. On success writes *decoded_size;
 *  it is left untouched on every failure path. */
static Result _encoding_base64_decode_core(U8 const *const reverse, char const *const data, USize const data_length, U8 *const output, USize const output_capacity, USize *const decoded_size) {
    trace_log_push(LOG_METADATA);

    USize const leftover     = data_length % ENCODING_BASE64_GROUP_CHAR_COUNT;
    USize const full_groups  = data_length / ENCODING_BASE64_GROUP_CHAR_COUNT;
    USize const partial_size = leftover == 0 ? 0 : (leftover == 2 ? 1 : 2);
    // Cannot wrap: full_groups <= USIZE_MAX / GROUP_CHAR_COUNT, so the byte
    // total stays below 0.75 * USIZE_MAX for every representable data_length.
    USize const total        = full_groups * ENCODING_BASE64_GROUP_BYTE_COUNT + partial_size;

    if (total > output_capacity) {
        trace_log_pop();

        return _encoding_base64_argument();
    }

    USize out_index = 0;

    for (USize g = 0; g < full_groups; g += 1) {
        USize const base = g * ENCODING_BASE64_GROUP_CHAR_COUNT;
        U8 const v0       = reverse[(U8) data[base]];
        U8 const v1       = reverse[(U8) data[base + 1]];
        U8 const v2       = reverse[(U8) data[base + 2]];
        U8 const v3       = reverse[(U8) data[base + 3]];

        if (v0 == _ENCODING_BASE64_INVALID || v1 == _ENCODING_BASE64_INVALID || v2 == _ENCODING_BASE64_INVALID || v3 == _ENCODING_BASE64_INVALID) {
            trace_log_pop();

            return _encoding_base64_argument();
        }

        output[out_index]     = (U8) ((v0 << 2) | (v1 >> 4));
        output[out_index + 1] = (U8) (((v1 & 0x0F) << 4) | (v2 >> 2));
        output[out_index + 2] = (U8) (((v2 & 0x03) << 6) | v3);
        out_index             += ENCODING_BASE64_GROUP_BYTE_COUNT;
    }

    if (leftover == 2) {
        USize const base = full_groups * ENCODING_BASE64_GROUP_CHAR_COUNT;
        U8 const v0       = reverse[(U8) data[base]];
        U8 const v1       = reverse[(U8) data[base + 1]];

        if (v0 == _ENCODING_BASE64_INVALID || v1 == _ENCODING_BASE64_INVALID || (v1 & 0x0F) != 0) {
            trace_log_pop();

            return _encoding_base64_argument();
        }

        output[out_index] = (U8) ((v0 << 2) | (v1 >> 4));
        out_index         += 1;
    }
    else if (leftover == 3) {
        USize const base = full_groups * ENCODING_BASE64_GROUP_CHAR_COUNT;
        U8 const v0       = reverse[(U8) data[base]];
        U8 const v1       = reverse[(U8) data[base + 1]];
        U8 const v2       = reverse[(U8) data[base + 2]];

        if (v0 == _ENCODING_BASE64_INVALID || v1 == _ENCODING_BASE64_INVALID || v2 == _ENCODING_BASE64_INVALID || (v2 & 0x03) != 0) {
            trace_log_pop();

            return _encoding_base64_argument();
        }

        output[out_index]     = (U8) ((v0 << 2) | (v1 >> 4));
        output[out_index + 1] = (U8) (((v1 & 0x0F) << 4) | (v2 >> 2));
        out_index             += 2;
    }

    *decoded_size = out_index;

    trace_log_pop();

    return RESULT_SUCCESS;
}

/*==============================================================================
 * MARK: - Public Implementations
 *============================================================================*/

Result encoding_base64_decode_1(char const *const input, USize const input_size, U8 *const output, USize const output_capacity, USize *const decoded_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "input", (void*) input);
    error_check_null(LOG_METADATA, "output", (void*) output);
    error_check_null(LOG_METADATA, "decoded_size", (void*) decoded_size);

    if (input_size == 0) {
        *decoded_size = 0;

        trace_log_pop();

        return RESULT_SUCCESS;
    }

    if (input_size % ENCODING_BASE64_GROUP_CHAR_COUNT != 0) {
        trace_log_pop();

        return _encoding_base64_argument();
    }

    USize const last        = input_size - 1;
    USize       pad_count   = 0;

    if (input[last] == '=') {
        pad_count = 1;

        if (input[last - 1] == '=') {
            pad_count = 2;
        }
    }

    // '=' is legal only as the trailing 1 or 2 characters; the decode core
    // rejects any '=' left inside the data span via the reverse table (255),
    // so no separate scan pass is needed.
    Result const result = _encoding_base64_decode_core(_encoding_base64_std_reverse, input, input_size - pad_count, output, output_capacity, decoded_size);

    trace_log_pop();

    return result;
}

void encoding_base64_encode_1(U8 const *const bytes, USize const size, char *const output) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "bytes", (void*) bytes);
    error_check_null(LOG_METADATA, "output", (void*) output);

    char const *const alphabet = _ENCODING_BASE64_STD_ALPHABET;
    USize out_index            = 0;
    USize i                    = 0;

    while (i + ENCODING_BASE64_GROUP_BYTE_COUNT <= size) {
        U32 const chunk = ((U32) bytes[i] << 16) | ((U32) bytes[i + 1] << 8) | (U32) bytes[i + 2];

        output[out_index]     = alphabet[(chunk >> 18) & 0x3F];
        output[out_index + 1] = alphabet[(chunk >> 12) & 0x3F];
        output[out_index + 2] = alphabet[(chunk >> 6) & 0x3F];
        output[out_index + 3] = alphabet[chunk & 0x3F];
        out_index             += ENCODING_BASE64_GROUP_CHAR_COUNT;
        i                     += ENCODING_BASE64_GROUP_BYTE_COUNT;
    }

    USize const remaining = size - i;

    if (remaining == 1) {
        U32 const chunk = (U32) bytes[i] << 16;

        output[out_index]     = alphabet[(chunk >> 18) & 0x3F];
        output[out_index + 1] = alphabet[(chunk >> 12) & 0x3F];
        output[out_index + 2] = '=';
        output[out_index + 3] = '=';
        out_index             += ENCODING_BASE64_GROUP_CHAR_COUNT;
    }
    else if (remaining == 2) {
        U32 const chunk = ((U32) bytes[i] << 16) | ((U32) bytes[i + 1] << 8);

        output[out_index]     = alphabet[(chunk >> 18) & 0x3F];
        output[out_index + 1] = alphabet[(chunk >> 12) & 0x3F];
        output[out_index + 2] = alphabet[(chunk >> 6) & 0x3F];
        output[out_index + 3] = '=';
        out_index             += ENCODING_BASE64_GROUP_CHAR_COUNT;
    }

    output[out_index] = '\0';

    trace_log_pop();
}

Result encoding_base64_url_decode_1(char const *const input, USize const input_size, U8 *const output, USize const output_capacity, USize *const decoded_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "input", (void*) input);
    error_check_null(LOG_METADATA, "output", (void*) output);
    error_check_null(LOG_METADATA, "decoded_size", (void*) decoded_size);

    if (input_size == 0) {
        *decoded_size = 0;

        trace_log_pop();

        return RESULT_SUCCESS;
    }

    if (input_size % ENCODING_BASE64_GROUP_CHAR_COUNT == 1) {
        trace_log_pop();

        return _encoding_base64_argument();
    }

    // The URL-safe alphabet is never padded, so any '=' anywhere is malformed;
    // the decode core rejects it via the reverse table (255).
    Result const result = _encoding_base64_decode_core(_encoding_base64_url_reverse, input, input_size, output, output_capacity, decoded_size);

    trace_log_pop();

    return result;
}

void encoding_base64_url_encode_1(U8 const *const bytes, USize const size, char *const output) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "bytes", (void*) bytes);
    error_check_null(LOG_METADATA, "output", (void*) output);

    char const *const alphabet = _ENCODING_BASE64_URL_ALPHABET;
    USize out_index            = 0;
    USize i                    = 0;

    while (i + ENCODING_BASE64_GROUP_BYTE_COUNT <= size) {
        U32 const chunk = ((U32) bytes[i] << 16) | ((U32) bytes[i + 1] << 8) | (U32) bytes[i + 2];

        output[out_index]     = alphabet[(chunk >> 18) & 0x3F];
        output[out_index + 1] = alphabet[(chunk >> 12) & 0x3F];
        output[out_index + 2] = alphabet[(chunk >> 6) & 0x3F];
        output[out_index + 3] = alphabet[chunk & 0x3F];
        out_index             += ENCODING_BASE64_GROUP_CHAR_COUNT;
        i                     += ENCODING_BASE64_GROUP_BYTE_COUNT;
    }

    USize const remaining = size - i;

    if (remaining == 1) {
        U32 const chunk = (U32) bytes[i] << 16;

        output[out_index]     = alphabet[(chunk >> 18) & 0x3F];
        output[out_index + 1] = alphabet[(chunk >> 12) & 0x3F];
        out_index             += ENCODING_BASE64_GROUP_CHAR_COUNT - 2; // minus the 2 padding chars url never emits
    }
    else if (remaining == 2) {
        U32 const chunk = ((U32) bytes[i] << 16) | ((U32) bytes[i + 1] << 8);

        output[out_index]     = alphabet[(chunk >> 18) & 0x3F];
        output[out_index + 1] = alphabet[(chunk >> 12) & 0x3F];
        output[out_index + 2] = alphabet[(chunk >> 6) & 0x3F];
        out_index             += ENCODING_BASE64_GROUP_CHAR_COUNT - 1; // minus the 1 padding char url never emits
    }

    output[out_index] = '\0';

    trace_log_pop();
}