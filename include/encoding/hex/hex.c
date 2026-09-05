/* ============================================================================
 *  Encoding Hex Implementation
 *  --------------------------------------------------------------------------
 *  @file    hex.c
 *  @brief   Bytes-to-hex-text codec, table-based, no heap.
 * ============================================================================
 */
#include <encoding/hex/hex.h>

/*==============================================================================
 * MARK: - Internal Implementations
 *============================================================================*/

/** @brief The bad-input Result shared by every decode failure path: an odd
 *  length pairing, a length mismatch against output_size, an output_size too
 *  large for the strictness product to represent, or a non-hex character.
 *  Decode failures are plain bad input, not a policy deviation. */
static Result _encoding_hex_argument(void) {
    return result_make(RESULT_CATEGORY_ARGUMENT, 0, 0);
}

/** @brief Decode a single hex digit to its 4-bit value, or -1 when ch is not
 *  a hex digit (0-9, a-f, A-F). */
static I32 _encoding_hex_digit_value(char const ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }

    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }

    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }

    return -1;
}

/*==============================================================================
 * MARK: - Public Implementations
 *============================================================================*/

Result encoding_hex_decode_1(char const *const input, USize const input_size, U8 *const output, USize const output_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "input", (void*) input);
    error_check_null(LOG_METADATA, "output", (void*) output);

    // output_size beyond USIZE_MAX / ENCODING_HEX_DIGITS_PER_BYTE wraps the
    // strictness product, which could let a wrong-length input pass the
    // check; reject it outright.
    if (output_size > USIZE_MAX / ENCODING_HEX_DIGITS_PER_BYTE || input_size != ENCODING_HEX_SIZE(output_size)) {
        trace_log_pop();

        return _encoding_hex_argument();
    }

    if (output_size == 0) {
        trace_log_pop();

        return RESULT_SUCCESS;
    }

    for (USize i = 0; i < output_size; i += 1) {
        I32 const high = _encoding_hex_digit_value(input[ENCODING_HEX_SIZE(i)]);
        I32 const low  = _encoding_hex_digit_value(input[ENCODING_HEX_SIZE(i) + 1]);

        if (high < 0 || low < 0) {
            trace_log_pop();

            return _encoding_hex_argument();
        }

        output[i] = (U8) ((high << 4) | low);
    }

    trace_log_pop();

    return RESULT_SUCCESS;
}

void encoding_hex_encode_1(U8 const *const bytes, USize const size, char *const output) {
    trace_log_push(LOG_METADATA);

    encoding_hex_encode_2(bytes, size, output, false);

    trace_log_pop();
}

void encoding_hex_encode_2(U8 const *const bytes, USize const size, char *const output, bool const upper) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "bytes", (void*) bytes);
    error_check_null(LOG_METADATA, "output", (void*) output);

    char const *const digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";

    for (USize i = 0; i < size; i += 1) {
        output[ENCODING_HEX_SIZE(i)]     = digits[(bytes[i] >> 4) & 0x0F];
        output[ENCODING_HEX_SIZE(i) + 1] = digits[bytes[i] & 0x0F];
    }

    output[ENCODING_HEX_SIZE(size)] = '\0';

    trace_log_pop();
}