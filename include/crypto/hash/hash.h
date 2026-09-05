/* ============================================================================
 *  Crypto Hash
 *  --------------------------------------------------------------------------
 *  @file    hash.h
 *  @brief   Cryptographic digests over OpenSSL EVP — SHA-256 and SHA-512.
 *  @author  CFW
 *  @date    2026-08-09
 *  @license MIT (see LICENSE file)
 *
 *  The plain-digest leaf of the crypto area: one-shot hashing for in-memory
 *  messages and a streaming context for data that arrives in chunks (files,
 *  sockets). It carries NO keying, encoding, or policy opinions — keyed
 *  digests live in crypto/hmac, password records in crypto/password, and hex
 *  presentation composes through encoding/hex at the caller.
 *
 *  Features:
 *    - crypto_hash_sha256 / crypto_hash_sha512: one-shot digest of a byte
 *      message into a caller-owned buffer.
 *    - Crypto_Hash streaming context: init → update... → final, for hashing
 *      data too large or too fragmented for one shot; crypto_hash_uninit is
 *      the abandon path for a live context that will never reach final.
 *    - crypto_hash_size: digest width for an algorithm, fail-closed 0 on an
 *      unknown value; CRYPTO_HASH_SHA256_SIZE / CRYPTO_HASH_SHA512_SIZE cover
 *      the same two widths as macros.
 *    - No heap allocation beyond the one OpenSSL EVP_MD_CTX a streaming
 *      instance owns; the caller owns every buffer.
 *
 *  Usage Examples:
 *    @code
 *    #include <crypto/hash/hash.h>
 *
 *    U8 digest[CRYPTO_HASH_SHA256_SIZE] = DEFAULT_INITIALIZATION;
 *
 *    if (result_is_error(crypto_hash_sha256((U8 const*) data, size, digest))) {
 *        return false; // fail closed
 *    }
 *
 *    Crypto_Hash hash = DEFAULT_INITIALIZATION;
 *
 *    if (result_is_error(crypto_hash_init(&hash, CRYPTO_HASH_ALGORITHM_SHA512))) {
 *        return false;
 *    }
 *
 *    while (read_chunk(&chunk, &chunk_size)) {
 *        if (result_is_error(crypto_hash_update(&hash, chunk, chunk_size))) {
 *            crypto_hash_uninit(&hash);
 *
 *            return false;
 *        }
 *    }
 *
 *    U8 file_digest[CRYPTO_HASH_SHA512_SIZE] = DEFAULT_INITIALIZATION;
 *
 *    if (result_is_error(crypto_hash_final(&hash, file_digest, sizeof(file_digest)))) {
 *        crypto_hash_uninit(&hash); // idempotent on every outcome; covers the ARGUMENT rejections final leaves live
 *
 *        return false;
 *    }
 *    @endcode
 *
 *  Error Handling:
 *    - Returns RESULT_SUCCESS; RESULT_CATEGORY_ARGUMENT on an unknown
 *      algorithm, a streaming call against a context that was never
 *      initialized or was already finalized/released — a stale handle must
 *      fail closed, not crash — or a final output_size below the algorithm's
 *      digest size; these are VALUE refusals that hold in every build, never
 *      an abort. RESULT_CATEGORY_LIBRARY on OpenSSL failure: logged at ERROR
 *      with the OpenSSL error text when OpenSSL produced one (the
 *      size-invariant final path carries none), the thread's OpenSSL error
 *      queue is drained afterwards so residue is not misattributed to a later
 *      failure, and the Result code field is 0 (packed ERR codes do not fit
 *      16 bits); output contents are then unspecified and must not be used —
 *      fail closed.
 *    - LIBRARY failures are reported through the log module at ERROR level;
 *      call log_init before the first crypto call, otherwise the report ends
 *      the process instead of returning (log's documented fail-fast).
 *    - message_size 0 is legal (the digest of the empty message is defined).
 *    - Required pointer arguments are validated first; a null argument aborts
 *      via error_check_null when ERROR_CHECK_ENABLED is defined. With it
 *      undefined the null checks compile out and a null argument is
 *      undefined behaviour (a read or write through it); the ARGUMENT
 *      refusals above still run as ordinary runtime branches in every build.
 *
 *  Thread Safety:
 *    - One-shots are stateless; safe to call concurrently. A Crypto_Hash
 *      instance is NOT synchronized — confine each instance to one thread;
 *      distinct instances are independent.
 *
 *  Memory Management:
 *    - The streaming context owns one OpenSSL EVP_MD_CTX from a successful
 *      init until crypto_hash_final or crypto_hash_uninit releases it. Final's
 *      ARGUMENT rejections never consume the stream; its success or LIBRARY
 *      failure always releases. Uninit is idempotent. The caller owns every
 *      buffer, and final validates output_size then stages the digest
 *      internally, so nothing is ever written past the caller's capacity.
 *    - Digests are not wiped: neither the caller's output buffer nor the
 *      internal staging buffer is cleared after use. CFW has no memory_wipe
 *      primitive yet.
 *
 *  Performance Characteristics:
 *    - Linear in message size; SHA-512 is faster than SHA-256 on 64-bit CPUs
 *      for large inputs, and SHA-256 has hardware support on x86 with SHA-NI
 *      and ARMv8 with the crypto extensions — SHA-512 is usually faster in
 *      pure software on 64-bit CPUs. Pick by protocol requirement, not
 *      micro-benchmarks.
 *
 *  Dependencies (Deps):
 *    - OpenSSL (<openssl/evp.h>, <openssl/err.h>), memory (memory.h — chains
 *      error → tracelog → log → thread, which provide limits.h and types.h).
 *    - result.h, included directly so the header compiles standalone.
 * ============================================================================
 */
#ifndef CRYPTO_HASH_H
#define CRYPTO_HASH_H

#include <openssl/err.h>
#include <openssl/evp.h>

#include <result.h>
#include <memory/memory.h>

/*==============================================================================
 * MARK: - Public Macros
 *============================================================================*/

#define CRYPTO_HASH_SHA256_SIZE 32 /**< SHA-256 digest bytes. */
#define CRYPTO_HASH_SHA512_SIZE 64 /**< SHA-512 digest bytes. */

/*==============================================================================
 * MARK: - Public Types
 *============================================================================*/

/** @brief The digest algorithm a Crypto_Hash context runs. */
typedef enum Crypto_Hash_Algorithm {
    CRYPTO_HASH_ALGORITHM_SHA256,
    CRYPTO_HASH_ALGORITHM_SHA512,
} Crypto_Hash_Algorithm;

/** @brief Streaming digest context. Zero-initialize, then crypto_hash_init;
 *  a zeroed (never-initialized) or finalized context fails closed on use. */
typedef struct Crypto_Hash {
    EVP_MD_CTX              *context;   /**< Owned OpenSSL context; null when inert. */
    Crypto_Hash_Algorithm   algorithm;  /**< Algorithm chosen at init. */
} Crypto_Hash;

/*==============================================================================
 * MARK: - Public Functions
 *============================================================================*/

/**
 * @brief Finalize a streaming digest and release the context.
 * @param self        Initialized, not-yet-finalized context. ARGUMENT
 *                    rejections never consume the stream (retry with a
 *                    right-sized buffer is legal); success or LIBRARY failure
 *                    always releases it.
 * @param output      Destination for the digest. Exactly
 *                    crypto_hash_size(algorithm) bytes are written on
 *                    success; nothing is written on failure.
 * @param output_size Capacity of output in bytes; anything at or above the
 *                    algorithm's digest size is legal (an EVP_MAX_MD_SIZE
 *                    buffer serves any algorithm).
 * @return RESULT_SUCCESS; RESULT_CATEGORY_ARGUMENT on a never-initialized or
 *         already-finalized context, or an output_size below the algorithm's
 *         digest size — the size is runtime state set at init, possibly far
 *         from the buffer, so it is validated here instead of trusted;
 *         RESULT_CATEGORY_LIBRARY on OpenSSL failure (output unspecified —
 *         fail closed).
 */
CFW_ATTR_NODISCARD
Result crypto_hash_final(Crypto_Hash *const self, U8 *const output, USize const output_size);

/**
 * @brief Start a streaming digest over the given algorithm.
 * @param self      Context to initialize; any prior owned state is NOT freed —
 *                  uninit a live context first. Set inert (context null) on
 *                  entry, so a failed init leaves it safe to uninit even when
 *                  the caller skipped zero-initialization.
 * @param algorithm Digest algorithm to run.
 * @return RESULT_SUCCESS; RESULT_CATEGORY_ARGUMENT on an unknown algorithm;
 *         RESULT_CATEGORY_LIBRARY on OpenSSL failure (self stays inert).
 */
CFW_ATTR_NODISCARD
Result crypto_hash_init(Crypto_Hash *const self, Crypto_Hash_Algorithm const algorithm);

/**
 * @brief One-shot SHA-256 digest of a byte message.
 * @param message      Message bytes. Empty is legal, but the pointer must
 *                     still be non-null even at size 0 ("" works; null
 *                     aborts).
 * @param message_size Length of message in bytes.
 * @param output       Destination; must hold CRYPTO_HASH_SHA256_SIZE bytes.
 * @return RESULT_SUCCESS; RESULT_CATEGORY_LIBRARY on OpenSSL failure (output
 *         unspecified — fail closed).
 */
CFW_ATTR_NODISCARD
Result crypto_hash_sha256(U8 const *const message, USize const message_size, U8 *const output);

/**
 * @brief One-shot SHA-512 digest of a byte message.
 * @param message      Message bytes. Empty is legal, but the pointer must
 *                     still be non-null even at size 0 ("" works; null
 *                     aborts).
 * @param message_size Length of message in bytes.
 * @param output       Destination; must hold CRYPTO_HASH_SHA512_SIZE bytes.
 * @return RESULT_SUCCESS; RESULT_CATEGORY_LIBRARY on OpenSSL failure (output
 *         unspecified — fail closed).
 */
CFW_ATTR_NODISCARD
Result crypto_hash_sha512(U8 const *const message, USize const message_size, U8 *const output);

/**
 * @brief Digest size in bytes for an algorithm.
 * @param algorithm Digest algorithm.
 * @return CRYPTO_HASH_SHA256_SIZE / CRYPTO_HASH_SHA512_SIZE; 0 on an unknown
 *         algorithm (fail-closed size — never trust it for a buffer).
 */
USize crypto_hash_size(Crypto_Hash_Algorithm const algorithm);

/**
 * @brief Release a streaming context without finalizing (the abandon path).
 * @param self Context to release; safe on a never-initialized or already
 *             finalized/released context (idempotent).
 */
void crypto_hash_uninit(Crypto_Hash *const self);

/**
 * @brief Absorb message bytes into a streaming digest.
 * @param self         Initialized, not-yet-finalized context.
 * @param message      Message bytes. Empty is legal, but the pointer must
 *                     still be non-null even at size 0 ("" works; null
 *                     aborts).
 * @param message_size Length of message in bytes.
 * @return RESULT_SUCCESS; RESULT_CATEGORY_ARGUMENT on a never-initialized or
 *         finalized context; RESULT_CATEGORY_LIBRARY on OpenSSL failure (the
 *         context is released — the stream cannot continue past a failure).
 */
CFW_ATTR_NODISCARD
Result crypto_hash_update(Crypto_Hash *const self, U8 const *const message, USize const message_size);

#endif // CRYPTO_HASH_H