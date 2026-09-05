/* ============================================================================
 *  Crypto Hash Implementation
 *  --------------------------------------------------------------------------
 *  @file    hash.c
 *  @brief   SHA-256/SHA-512 one-shot and streaming digests over OpenSSL EVP.
 * ============================================================================
 */
#include <crypto/hash/hash.h>

/*==============================================================================
 * MARK: - Internal Constants
 *============================================================================*/

/** @brief Buffer size for the formatted OpenSSL error text in failure logs. */
#define _CRYPTO_HASH_ERROR_TEXT_SIZE 256

/*==============================================================================
 * MARK: - Internal Implementations
 *============================================================================*/

/** @brief The fail-closed bad-argument Result: an unknown algorithm or a
 *  streaming call against a never-initialized/finalized context. Rejected
 *  through the Result channel instead of the error_check abort idiom: a stale
 *  handle or a config-derived algorithm value must fail closed, not crash. */
static Result _crypto_hash_argument(void) {
    return result_make(RESULT_CATEGORY_ARGUMENT, 0, 0);
}

/** @brief The fail-closed OpenSSL-failure Result. Logs the OpenSSL error text
 *  and drains the thread-local error queue, per the crypto error-path
 *  precedent (see crypto/random): operators must be able to diagnose a
 *  breaking digest path. The Result code stays 0 — OpenSSL ERR codes are
 *  packed multi-field values the 16-bit code field must not truncate (see
 *  result.h). */
static Result _crypto_hash_failure(char const *const operation) {
    char text[_CRYPTO_HASH_ERROR_TEXT_SIZE] = DEFAULT_INITIALIZATION;

    ERR_error_string_n(ERR_get_error(), text, sizeof(text));

    // Drain the rest of the thread-local queue so stale residue cannot be
    // misattributed to a later OpenSSL failure on this thread.
    while (ERR_get_error() != 0) {}

    log_message_2(LOG_LEVEL_ERROR, LOG_METADATA, "Hash failure: %s reported \"%s\"", operation, text);

    return result_make(RESULT_CATEGORY_LIBRARY, 0, 0);
}

/** @brief The fail-closed internal-invariant Result: OpenSSL reported success
 *  but wrote an unexpected digest size. Logged plainly, without OpenSSL error
 *  formatting — the ERR queue holds no entry for a non-OpenSSL failure, and
 *  formatting it would fabricate an "error:00000000" misattribution in the
 *  one channel that is the diagnosis. Practically unreachable. */
static Result _crypto_hash_invariant(char const *const operation, USize const written, USize const expected) {
    // Cast to the exact type %llu names: U64 is unsigned long on LP64 Linux,
    // so a (U64) cast would strictly mismatch the specifier there (unchecked
    // varargs — no compiler catches it).
    log_message_2(LOG_LEVEL_ERROR, LOG_METADATA, "Hash failure: %s wrote %llu digest bytes, expected %llu", operation, (unsigned long long) written, (unsigned long long) expected);

    return result_make(RESULT_CATEGORY_LIBRARY, 0, 0);
}

/** @brief Map an algorithm to its OpenSSL digest; null on an unknown value. */
static EVP_MD* _crypto_hash_md(Crypto_Hash_Algorithm const algorithm) {
    switch (algorithm) {
        case CRYPTO_HASH_ALGORITHM_SHA256: return (EVP_MD*) EVP_sha256();
        case CRYPTO_HASH_ALGORITHM_SHA512: return (EVP_MD*) EVP_sha512();
    }

    return nullptr;
}

/** @brief Shared one-shot body: digest message into output for the given
 *  algorithm. EVP_Digest takes size_t natively, so no int-boundary guard is
 *  needed (unlike the kdf). The write is bounded by the digest size the
 *  public wrapper's name fixed at the call site. */
static Result _crypto_hash_one_shot(U8 const *const message, USize const message_size, U8 *const output, Crypto_Hash_Algorithm const algorithm) {
    trace_log_push(LOG_METADATA);

    U32 written = 0;

    if (EVP_Digest(message, message_size, output, &written, _crypto_hash_md(algorithm), nullptr) != 1) {
        trace_log_pop();

        return _crypto_hash_failure("EVP_Digest");
    }

    if ((USize) written != crypto_hash_size(algorithm)) {
        trace_log_pop();

        return _crypto_hash_invariant("EVP_Digest", (USize) written, crypto_hash_size(algorithm));
    }

    trace_log_pop();

    return RESULT_SUCCESS;
}

/*==============================================================================
 * MARK: - Public Implementations
 *============================================================================*/

Result crypto_hash_final(Crypto_Hash *const self, U8 *const output, USize const output_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "output", (void*) output);

    if (self->context == nullptr) {
        trace_log_pop();

        return _crypto_hash_argument();
    }

    USize const size = crypto_hash_size(self->algorithm);

    // The required size is runtime state set at init, possibly far from the
    // caller's buffer — validate it here instead of trusting the caller, and
    // leave the stream intact so a right-sized retry stays possible.
    if (output_size < size) {
        trace_log_pop();

        return _crypto_hash_argument();
    }

    // Stage the digest in a max-size buffer so nothing reaches the caller's
    // buffer until both EVP success and the size invariant hold.
    U8          staged[EVP_MAX_MD_SIZE] = DEFAULT_INITIALIZATION;
    U32         written                 = 0;
    bool const  finalized               = EVP_DigestFinal_ex(self->context, staged, &written) == 1;

    // Success or LIBRARY failure always releases; the ARGUMENT rejections
    // above never consume the stream.
    crypto_hash_uninit(self);

    if (!finalized) {
        trace_log_pop();

        return _crypto_hash_failure("EVP_DigestFinal_ex");
    }

    if ((USize) written != size) {
        trace_log_pop();

        return _crypto_hash_invariant("EVP_DigestFinal_ex", (USize) written, size);
    }

    memory_copy_1(output, staged, size);

    trace_log_pop();

    return RESULT_SUCCESS;
}

Result crypto_hash_init(Crypto_Hash *const self, Crypto_Hash_Algorithm const algorithm) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    // Inert on entry: a failed init must leave the handle safe to uninit even
    // when the caller skipped zero-initialization.
    self->context = nullptr;

    EVP_MD const *const md = _crypto_hash_md(algorithm);

    if (md == nullptr) {
        trace_log_pop();

        return _crypto_hash_argument();
    }

    EVP_MD_CTX *const context = EVP_MD_CTX_new();

    if (context == nullptr) {
        trace_log_pop();

        return _crypto_hash_failure("EVP_MD_CTX_new");
    }

    if (EVP_DigestInit_ex(context, md, nullptr) != 1) {
        EVP_MD_CTX_free(context);

        trace_log_pop();

        return _crypto_hash_failure("EVP_DigestInit_ex");
    }

    self->context   = context;
    self->algorithm = algorithm;

    trace_log_pop();

    return RESULT_SUCCESS;
}

Result crypto_hash_sha256(U8 const *const message, USize const message_size, U8 *const output) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "message", (void*) message);
    error_check_null(LOG_METADATA, "output", (void*) output);

    Result const result = _crypto_hash_one_shot(message, message_size, output, CRYPTO_HASH_ALGORITHM_SHA256);

    trace_log_pop();

    return result;
}

Result crypto_hash_sha512(U8 const *const message, USize const message_size, U8 *const output) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "message", (void*) message);
    error_check_null(LOG_METADATA, "output", (void*) output);

    Result const result = _crypto_hash_one_shot(message, message_size, output, CRYPTO_HASH_ALGORITHM_SHA512);

    trace_log_pop();

    return result;
}

USize crypto_hash_size(Crypto_Hash_Algorithm const algorithm) {
    switch (algorithm) {
        case CRYPTO_HASH_ALGORITHM_SHA256: return CRYPTO_HASH_SHA256_SIZE;
        case CRYPTO_HASH_ALGORITHM_SHA512: return CRYPTO_HASH_SHA512_SIZE;
    }

    return 0;
}

void crypto_hash_uninit(Crypto_Hash *const self) {
    error_check_null(LOG_METADATA, "self", (void*) self);

    if (self->context != nullptr) {
        EVP_MD_CTX_free(self->context);

        self->context = nullptr;
    }
}

Result crypto_hash_update(Crypto_Hash *const self, U8 const *const message, USize const message_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "message", (void*) message);

    if (self->context == nullptr) {
        trace_log_pop();

        return _crypto_hash_argument();
    }

    if (EVP_DigestUpdate(self->context, message, message_size) != 1) {
        // The stream cannot meaningfully continue past a failed absorb —
        // release now so later update/final calls fail closed as ARGUMENT.
        crypto_hash_uninit(self);

        trace_log_pop();

        return _crypto_hash_failure("EVP_DigestUpdate");
    }

    trace_log_pop();

    return RESULT_SUCCESS;
}