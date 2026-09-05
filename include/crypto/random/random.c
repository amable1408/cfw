/* ============================================================================
 *  Crypto Random Implementation
 *  --------------------------------------------------------------------------
 *  @file    random.c
 *  @brief   CSPRNG bytes, hex tokens, and unbiased bounded draws over OpenSSL.
 * ============================================================================
 */
#include <crypto/random/random.h>

/*==============================================================================
 * MARK: - Internal Constants
 *============================================================================*/

/** @brief Buffer size for the formatted OpenSSL error text in failure logs. */
#define _CRYPTO_RANDOM_ERROR_TEXT_SIZE 256

/** @brief Stack chunk for hex generation: bytes drawn and encoded per round. */
#define _CRYPTO_RANDOM_HEX_CHUNK_SIZE 64

/*==============================================================================
 * MARK: - Internal Implementations
 *============================================================================*/

/** @brief The fail-closed argument-rejection Result. Zero sizes/bounds are
 *  rejected through the Result channel instead of the house error_check abort
 *  idiom: sizes here may someday derive from request input, and a secret-
 *  issuing path must fail closed, not crash — while a zero-byte "success"
 *  would be an empty secret. */
static Result _crypto_random_argument(void) {
    return result_make(RESULT_CATEGORY_ARGUMENT, 0, 0);
}

/** @brief The fail-closed CSPRNG-failure Result shared by every function.
 *  Logs the OpenSSL error text: RAND_bytes failing means the server is
 *  silently issuing no secrets, which operators must be able to diagnose.
 *  The Result code stays 0 — OpenSSL ERR codes are packed multi-field values,
 *  which the 16-bit code field must not truncate (see result.h). */
static Result _crypto_random_failure(void) {
    char text[_CRYPTO_RANDOM_ERROR_TEXT_SIZE] = DEFAULT_INITIALIZATION;

    ERR_error_string_n(ERR_get_error(), text, sizeof(text));

    // Drain the rest of the thread-local queue so stale residue cannot be
    // misattributed to a later OpenSSL failure on this thread.
    while (ERR_get_error() != 0) {}

    log_message_2(LOG_LEVEL_ERROR, LOG_METADATA, "CSPRNG failure: RAND_bytes reported \"%s\"", text);

    return result_make(RESULT_CATEGORY_LIBRARY, 0, 0);
}

/*==============================================================================
 * MARK: - Public Implementations
 *============================================================================*/

Result crypto_random_bytes(U8 *const output, USize const size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "output", (void*) output);

    if (size == 0) {
        trace_log_pop();

        return _crypto_random_argument();
    }

    U8 *at              = output;
    USize remaining     = size;

    // RAND_bytes takes an int count, so a USize request larger than INT_MAX
    // must be filled in chunks — never narrowed in one unguarded cast.
    while (remaining > 0) {
        USize const chunk = remaining > (USize) INT_MAX ? (USize) INT_MAX : remaining;

        if (RAND_bytes(at, (I32) chunk) != 1) {
            trace_log_pop();

            return _crypto_random_failure();
        }

        at        += chunk;
        remaining -= chunk;
    }

    trace_log_pop();

    return RESULT_SUCCESS;
}

Result crypto_random_hex(char *const output, USize const byte_count) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "output", (void*) output);

    // Beyond this bound the CRYPTO_RANDOM_HEX_SIZE(n) plus the NUL slot (the
    // - 1) wraps, so the caller's buffer cannot be correctly sized.
    if (byte_count == 0 || byte_count > (USIZE_MAX - 1) / ENCODING_HEX_DIGITS_PER_BYTE) {
        trace_log_pop();

        return _crypto_random_argument();
    }

    char *at        = output;
    USize remaining = byte_count;

    // Draw and encode through a fixed stack chunk so arbitrary token sizes
    // never require a heap allocation.
    while (remaining > 0) {
        U8 buffer[_CRYPTO_RANDOM_HEX_CHUNK_SIZE] = DEFAULT_INITIALIZATION;
        USize const chunk = remaining > _CRYPTO_RANDOM_HEX_CHUNK_SIZE ? _CRYPTO_RANDOM_HEX_CHUNK_SIZE : remaining;

        Result const result = crypto_random_bytes(buffer, chunk);

        if (result_is_error(result)) {
            trace_log_pop();

            return result;
        }

        encoding_hex_encode_1(buffer, chunk, at);

        at        += CRYPTO_RANDOM_HEX_SIZE(chunk);
        remaining -= chunk;
    }

    trace_log_pop();

    return RESULT_SUCCESS;
}

Result crypto_random_uniform(U64 const bound, U64 *const value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "value", (void*) value);

    if (bound == 0) {
        trace_log_pop();

        return _crypto_random_argument();
    }

    if (bound == 1) {
        *value = 0;

        trace_log_pop();

        return RESULT_SUCCESS;
    }

    // Rejection sampling: draws below threshold (2^64 mod bound) fall in the
    // partial final cycle and would bias low values, so redraw instead of
    // folding them in. Rejection probability is < 1/2 per round.
    U64 const threshold = (0 - bound) % bound;
    U64 draw            = 0;

    do {
        Result const result = crypto_random_bytes((U8*) &draw, sizeof(draw));

        if (result_is_error(result)) {
            trace_log_pop();

            return result;
        }
    } while (draw < threshold);

    *value = draw % bound;

    trace_log_pop();

    return RESULT_SUCCESS;
}