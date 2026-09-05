/* ============================================================================
 *  Crypto KDF Implementation
 *  --------------------------------------------------------------------------
 *  @file    kdf.c
 *  @brief   PBKDF2-HMAC-SHA256 over OpenSSL, guarded at the int boundary.
 * ============================================================================
 */
#include <crypto/kdf/kdf.h>

/*==============================================================================
 * MARK: - Internal Constants
 *============================================================================*/

/** @brief Buffer size for the formatted OpenSSL error text in failure logs. */
#define _CRYPTO_KDF_ERROR_TEXT_SIZE 256

/*==============================================================================
 * MARK: - Internal Implementations
 *============================================================================*/

/** @brief The fail-closed bad-argument Result: zero salt/iterations/output or
 *  a size above the OpenSSL int boundary. Rejected through the Result channel
 *  instead of the error_check abort idiom: iteration counts and sizes here may
 *  derive from stored or configured data, and a secret-deriving path must fail
 *  closed, not crash. */
static Result _crypto_kdf_argument(void) {
    return result_make(RESULT_CATEGORY_ARGUMENT, 0, 0);
}

/** @brief The fail-closed OpenSSL-failure Result. Logs the OpenSSL error text
 *  and drains the thread-local error queue, per the crypto error-path
 *  precedent (see crypto/random): a failing KDF means logins silently break,
 *  which operators must be able to diagnose. The Result code stays 0 —
 *  OpenSSL ERR codes are packed multi-field values the 16-bit code field must
 *  not truncate (see result.h). */
static Result _crypto_kdf_failure(void) {
    char text[_CRYPTO_KDF_ERROR_TEXT_SIZE] = DEFAULT_INITIALIZATION;

    ERR_error_string_n(ERR_get_error(), text, sizeof(text));

    // Drain the rest of the thread-local queue so stale residue cannot be
    // misattributed to a later OpenSSL failure on this thread.
    while (ERR_get_error() != 0) {}

    log_message_2(LOG_LEVEL_ERROR, LOG_METADATA, "KDF failure: PKCS5_PBKDF2_HMAC reported \"%s\"", text);

    return result_make(RESULT_CATEGORY_LIBRARY, 0, 0);
}

/*==============================================================================
 * MARK: - Public Implementations
 *============================================================================*/

Result crypto_kdf_pbkdf2_sha256(
    char const *const password, USize const password_size, U8 const *const salt, USize const salt_size, USize const iterations, U8 *const output, USize const output_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "password", (void*) password);
    error_check_null(LOG_METADATA, "salt", (void*) salt);
    error_check_null(LOG_METADATA, "output", (void*) output);

    if (salt_size == 0 || iterations == 0 || output_size == 0) {
        trace_log_pop();

        return _crypto_kdf_argument();
    }

    // PKCS5_PBKDF2_HMAC takes int for every count, so anything above INT_MAX
    // must be rejected here — a silent narrowing would collapse the iteration
    // cost or truncate an input.
    if (password_size > (USize) INT_MAX || salt_size > (USize) INT_MAX || iterations > (USize) INT_MAX || output_size > (USize) INT_MAX) {
        trace_log_pop();

        return _crypto_kdf_argument();
    }

    if (PKCS5_PBKDF2_HMAC(password, (I32) password_size, salt, (I32) salt_size, (I32) iterations, EVP_sha256(), (I32) output_size, output) != 1) {
        trace_log_pop();

        return _crypto_kdf_failure();
    }

    trace_log_pop();

    return RESULT_SUCCESS;
}