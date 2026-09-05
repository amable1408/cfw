/* ============================================================================
 *  Crypto HMAC Implementation
 *  --------------------------------------------------------------------------
 *  @file    hmac.c
 *  @brief   HMAC-SHA256/SHA512 (raw, lower/upper hex) and constant-time verify.
 * ============================================================================
 */
#include <crypto/hmac/hmac.h>

/*==============================================================================
 * MARK: - Internal Constants
 *============================================================================*/

/** @brief EVP_Q_mac sub-algorithm name for the SHA-256 variant. */
#define _CRYPTO_HMAC_ALGORITHM_SHA256 "SHA256"
/** @brief EVP_Q_mac sub-algorithm name for the SHA-512 variant. */
#define _CRYPTO_HMAC_ALGORITHM_SHA512 "SHA512"
/** @brief Buffer size for the formatted OpenSSL error text in failure logs. */
#define _CRYPTO_HMAC_ERROR_TEXT_SIZE 256
/** @brief EVP_Q_mac MAC-family name; the sub-algorithm selects the digest. */
#define _CRYPTO_HMAC_NAME "HMAC"

/*==============================================================================
 * MARK: - Internal Implementations
 *============================================================================*/

/** @brief The fail-closed OpenSSL-failure Result. Logs the OpenSSL error text
 *  and drains the thread-local error queue, per the crypto error-path
 *  precedent (see crypto/random): a failing MAC breaks signing and webhook
 *  verification silently, which operators must be able to diagnose. The Result
 *  code stays 0 — OpenSSL ERR codes are packed multi-field values the 16-bit
 *  code field must not truncate (see result.h). */
static Result _crypto_hmac_failure(char const *const operation) {
    char text[_CRYPTO_HMAC_ERROR_TEXT_SIZE] = DEFAULT_INITIALIZATION;

    ERR_error_string_n(ERR_get_error(), text, sizeof(text));

    // Drain the rest of the thread-local queue so stale residue cannot be
    // misattributed to a later OpenSSL failure on this thread.
    while (ERR_get_error() != 0) {}

    log_message_2(LOG_LEVEL_ERROR, LOG_METADATA, "HMAC failure: %s reported \"%s\"", operation, text);

    return result_make(RESULT_CATEGORY_LIBRARY, 0, 0);
}

/** @brief The fail-closed internal-invariant Result: OpenSSL reported success
 *  but wrote an unexpected MAC size. Logged plainly, without OpenSSL error
 *  formatting — the ERR queue holds no entry for a non-OpenSSL failure, and
 *  formatting it would fabricate an "error:00000000" misattribution in the one
 *  channel that is the diagnosis. Practically unreachable. */
static Result _crypto_hmac_invariant(char const *const operation, USize const written, USize const expected) {
    // Cast to the exact type %llu names: USize is unsigned long on LP64 Linux,
    // so a (USize) argument would strictly mismatch the specifier there
    // (unchecked varargs — no compiler catches it).
    log_message_2(LOG_LEVEL_ERROR, LOG_METADATA, "HMAC failure: %s wrote %llu MAC bytes, expected %llu", operation, (unsigned long long) written, (unsigned long long) expected);

    return result_make(RESULT_CATEGORY_LIBRARY, 0, 0);
}

/** @brief Shared raw-MAC body: authenticate message under key with the given
 *  sub-algorithm. EVP_Q_mac takes size_t natively and bounds its own write by
 *  output_size, so no int-boundary guard is needed (unlike the retired HMAC()
 *  path, whose int key length forced one). */
static Result _crypto_hmac_mac(
    char const *const algorithm, char const *const key, USize const key_size, char const *const message, USize const message_size, U8 *const output, USize const output_size) {
    trace_log_push(LOG_METADATA);

    USize written = 0;

    if (EVP_Q_mac(nullptr, _CRYPTO_HMAC_NAME, nullptr, algorithm, nullptr, key, key_size, (U8 const*) message, message_size, output, output_size, &written) == nullptr) {
        trace_log_pop();

        return _crypto_hmac_failure("EVP_Q_mac");
    }

    if (written != output_size) {
        trace_log_pop();

        return _crypto_hmac_invariant("EVP_Q_mac", written, output_size);
    }

    trace_log_pop();

    return RESULT_SUCCESS;
}

/** @brief Shared hex body: compute the raw MAC into a max-size staging buffer,
 *  then encode digest_size bytes as hex. Staging keeps the raw digest off the
 *  caller's char buffer, which is sized for hex, not bytes. */
static Result _crypto_hmac_mac_hex(
    char const *const algorithm, char const *const key, USize const key_size, char const *const message, USize const message_size, char *const output,
    USize const digest_size, bool const upper) {
    trace_log_push(LOG_METADATA);

    U8              staged[EVP_MAX_MD_SIZE] = DEFAULT_INITIALIZATION;
    Result  const   result                  = _crypto_hmac_mac(algorithm, key, key_size, message, message_size, staged, digest_size);

    if (result_is_error(result)) {
        trace_log_pop();

        return result;
    }

    encoding_hex_encode_2(staged, digest_size, output, upper);

    trace_log_pop();

    return RESULT_SUCCESS;
}

/** @brief Shared verify body: recompute the MAC and compare it against the
 *  expected hex in constant time. Returns plain bool by design — distinguishing
 *  a wrong MAC from a malformed input or a library failure would be an oracle. */
static bool _crypto_hmac_verify(
    char const *const algorithm, char const *const key, USize const key_size, char const *const message, USize const message_size, char const *const expected_hex,
    USize const hex_size, USize const digest_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "key", (void*) key);
    error_check_null(LOG_METADATA, "message", (void*) message);
    error_check_null(LOG_METADATA, "expected_hex", (void*) expected_hex);

    if (char_length(expected_hex) != hex_size) {
        trace_log_pop();

        return false;
    }

    // Sized for the widest supported MAC so one buffer serves both algorithms.
    char computed[CRYPTO_HMAC_SHA512_HEX_SIZE + CHAR_END_CHARACTER] = DEFAULT_INITIALIZATION;

    if (result_is_error(_crypto_hmac_mac_hex(algorithm, key, key_size, message, message_size, computed, digest_size, false))) {
        trace_log_pop();

        return false;
    }

    bool const result = char_compare_iequal_comptime_1(computed, expected_hex);

    trace_log_pop();

    return result;
}

/*==============================================================================
 * MARK: - Public Implementations
 *============================================================================*/

Result crypto_hmac_sha256_bytes_1(U8 const *const key, USize const key_size, U8 const *const message, USize const message_size, U8 *const output) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "key", (void*) key);
    error_check_null(LOG_METADATA, "message", (void*) message);
    error_check_null(LOG_METADATA, "output", (void*) output);

    // The shared worker is char-typed for the hex paths; these are the same
    // bytes either way (EVP_Q_mac takes void*/U8* natively).
    Result const result = _crypto_hmac_mac(_CRYPTO_HMAC_ALGORITHM_SHA256, (char const*) key, key_size, (char const*) message, message_size, output, CRYPTO_HMAC_SHA256_SIZE);

    trace_log_pop();

    return result;
}

Result crypto_hmac_sha256_hex_1(char const *const key, USize const key_size, char const *const message, USize const message_size, char *const output) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "key", (void*) key);
    error_check_null(LOG_METADATA, "message", (void*) message);
    error_check_null(LOG_METADATA, "output", (void*) output);

    Result const result = _crypto_hmac_mac_hex(_CRYPTO_HMAC_ALGORITHM_SHA256, key, key_size, message, message_size, output, CRYPTO_HMAC_SHA256_SIZE, false);

    trace_log_pop();

    return result;
}

Result crypto_hmac_sha256_hex_2(char const *const key, USize const key_size, Str const *const message, char *const output) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "message", (void*) message);

    // An empty Str carries null data, which the worker's null check would
    // abort on — empty is a legal value, so map it to the empty C string.
    char    const   *const  data    = str_empty((Str*) message) ? "" : str_get_data((Str*) message);
    USize   const           size    = str_empty((Str*) message) ? 0 : str_get_size((Str*) message);
    Result  const           result  = crypto_hmac_sha256_hex_1(key, key_size, data, size, output);

    trace_log_pop();

    return result;
}

Result crypto_hmac_sha256_hex_3(char const *const key, USize const key_size, String const *const message, char *const output) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "message", (void*) message);

    // An empty String carries null data — see crypto_hmac_sha256_hex_2.
    char    const   *const  data    = string_empty((String*) message) ? "" : string_get_data((String*) message);
    USize   const           size    = string_empty((String*) message) ? 0 : string_get_size((String*) message);
    Result  const           result  = crypto_hmac_sha256_hex_1(key, key_size, data, size, output);

    trace_log_pop();

    return result;
}

Result crypto_hmac_sha256_hex_upper_1(char const *const key, USize const key_size, char const *const message, USize const message_size, char *const output) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "key", (void*) key);
    error_check_null(LOG_METADATA, "message", (void*) message);
    error_check_null(LOG_METADATA, "output", (void*) output);

    Result const result = _crypto_hmac_mac_hex(_CRYPTO_HMAC_ALGORITHM_SHA256, key, key_size, message, message_size, output, CRYPTO_HMAC_SHA256_SIZE, true);

    trace_log_pop();

    return result;
}

bool crypto_hmac_sha256_verify_1(char const *const key, USize const key_size, char const *const message, USize const message_size, char const *const expected_hex) {
    trace_log_push(LOG_METADATA);

    bool const result = _crypto_hmac_verify(
        _CRYPTO_HMAC_ALGORITHM_SHA256, key, key_size, message, message_size, expected_hex, CRYPTO_HMAC_SHA256_HEX_SIZE, CRYPTO_HMAC_SHA256_SIZE);

    trace_log_pop();

    return result;
}

bool crypto_hmac_sha256_verify_2(char const *const key, USize const key_size, Str const *const message, char const *const expected_hex) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "message", (void*) message);

    // An empty Str carries null data — see crypto_hmac_sha256_hex_2.
    char    const   *const  data    = str_empty((Str*) message) ? "" : str_get_data((Str*) message);
    USize   const           size    = str_empty((Str*) message) ? 0 : str_get_size((Str*) message);
    bool    const           result  = crypto_hmac_sha256_verify_1(key, key_size, data, size, expected_hex);

    trace_log_pop();

    return result;
}

bool crypto_hmac_sha256_verify_3(char const *const key, USize const key_size, String const *const message, char const *const expected_hex) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "message", (void*) message);

    // An empty String carries null data — see crypto_hmac_sha256_hex_2.
    char    const   *const  data    = string_empty((String*) message) ? "" : string_get_data((String*) message);
    USize   const           size    = string_empty((String*) message) ? 0 : string_get_size((String*) message);
    bool    const           result  = crypto_hmac_sha256_verify_1(key, key_size, data, size, expected_hex);

    trace_log_pop();

    return result;
}

Result crypto_hmac_sha512_bytes_1(U8 const *const key, USize const key_size, U8 const *const message, USize const message_size, U8 *const output) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "key", (void*) key);
    error_check_null(LOG_METADATA, "message", (void*) message);
    error_check_null(LOG_METADATA, "output", (void*) output);

    // See crypto_hmac_sha256_bytes_1 on the cast.
    Result const result = _crypto_hmac_mac(_CRYPTO_HMAC_ALGORITHM_SHA512, (char const*) key, key_size, (char const*) message, message_size, output, CRYPTO_HMAC_SHA512_SIZE);

    trace_log_pop();

    return result;
}

Result crypto_hmac_sha512_hex_1(char const *const key, USize const key_size, char const *const message, USize const message_size, char *const output) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "key", (void*) key);
    error_check_null(LOG_METADATA, "message", (void*) message);
    error_check_null(LOG_METADATA, "output", (void*) output);

    Result const result = _crypto_hmac_mac_hex(_CRYPTO_HMAC_ALGORITHM_SHA512, key, key_size, message, message_size, output, CRYPTO_HMAC_SHA512_SIZE, false);

    trace_log_pop();

    return result;
}

Result crypto_hmac_sha512_hex_2(char const *const key, USize const key_size, Str const *const message, char *const output) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "message", (void*) message);

    // An empty Str carries null data — see crypto_hmac_sha256_hex_2.
    char    const   *const  data    = str_empty((Str*) message) ? "" : str_get_data((Str*) message);
    USize   const           size    = str_empty((Str*) message) ? 0 : str_get_size((Str*) message);
    Result  const           result  = crypto_hmac_sha512_hex_1(key, key_size, data, size, output);

    trace_log_pop();

    return result;
}

Result crypto_hmac_sha512_hex_3(char const *const key, USize const key_size, String const *const message, char *const output) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "message", (void*) message);

    // An empty String carries null data — see crypto_hmac_sha256_hex_2.
    char    const   *const  data    = string_empty((String*) message) ? "" : string_get_data((String*) message);
    USize   const           size    = string_empty((String*) message) ? 0 : string_get_size((String*) message);
    Result  const           result  = crypto_hmac_sha512_hex_1(key, key_size, data, size, output);

    trace_log_pop();

    return result;
}

Result crypto_hmac_sha512_hex_upper_1(char const *const key, USize const key_size, char const *const message, USize const message_size, char *const output) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "key", (void*) key);
    error_check_null(LOG_METADATA, "message", (void*) message);
    error_check_null(LOG_METADATA, "output", (void*) output);

    Result const result = _crypto_hmac_mac_hex(_CRYPTO_HMAC_ALGORITHM_SHA512, key, key_size, message, message_size, output, CRYPTO_HMAC_SHA512_SIZE, true);

    trace_log_pop();

    return result;
}

bool crypto_hmac_sha512_verify_1(char const *const key, USize const key_size, char const *const message, USize const message_size, char const *const expected_hex) {
    trace_log_push(LOG_METADATA);

    bool const result = _crypto_hmac_verify(
        _CRYPTO_HMAC_ALGORITHM_SHA512, key, key_size, message, message_size, expected_hex, CRYPTO_HMAC_SHA512_HEX_SIZE, CRYPTO_HMAC_SHA512_SIZE);

    trace_log_pop();

    return result;
}

bool crypto_hmac_sha512_verify_2(char const *const key, USize const key_size, Str const *const message, char const *const expected_hex) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "message", (void*) message);

    // An empty Str carries null data — see crypto_hmac_sha256_hex_2.
    char    const   *const  data    = str_empty((Str*) message) ? "" : str_get_data((Str*) message);
    USize   const           size    = str_empty((Str*) message) ? 0 : str_get_size((Str*) message);
    bool    const           result  = crypto_hmac_sha512_verify_1(key, key_size, data, size, expected_hex);

    trace_log_pop();

    return result;
}

bool crypto_hmac_sha512_verify_3(char const *const key, USize const key_size, String const *const message, char const *const expected_hex) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "message", (void*) message);

    // An empty String carries null data — see crypto_hmac_sha256_hex_2.
    char    const   *const  data    = string_empty((String*) message) ? "" : string_get_data((String*) message);
    USize   const           size    = string_empty((String*) message) ? 0 : string_get_size((String*) message);
    bool    const           result  = crypto_hmac_sha512_verify_1(key, key_size, data, size, expected_hex);

    trace_log_pop();

    return result;
}