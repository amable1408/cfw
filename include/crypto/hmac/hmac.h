/* ============================================================================
 *  Crypto HMAC
 *  --------------------------------------------------------------------------
 *  @file    hmac.h
 *  @brief   Keyed-hash message authentication (HMAC-SHA256/SHA512) over OpenSSL.
 *  @author  CFW
 *  @date    2026-07-17
 *  @license MIT (see LICENSE file)
 *
 *  The keyed-digest leaf of the crypto area: it authenticates a message under
 *  a secret key and offers a constant-time verify path for comparing a
 *  computed MAC against an expected hex string without leaking timing
 *  information. Plain (unkeyed) digests live in crypto/hash.
 *
 *  This is a general-purpose CFW crypto primitive: any caller needing to sign
 *  or verify a payload (webhook signatures, request signing, token integrity)
 *  should use it rather than reaching for OpenSSL directly.
 *
 *  Features:
 *    - HMAC-SHA256 and HMAC-SHA512 over an arbitrary byte message.
 *    - Raw-digest, lowercase-hex, and uppercase-hex output forms.
 *    - Case-insensitive verification against an expected hex MAC: a fast,
 *      non-secret length check first, then a constant-time compare of the
 *      recomputed digest hex.
 *    - char / Str / String message overloads (_1 / _2 / _3) on the lowercase-hex
 *      and verify paths. The raw-byte and uppercase-hex forms carry no Str /
 *      String adapters: the _1 suffix reserves the slots, and adapters are
 *      added when a caller needs one, not speculatively.
 *
 *  Family rule — algorithm naming and output sizing (shared with crypto/hash):
 *    an algorithm chosen at COMPILE time is spelled in the function name, and
 *    its output buffer is then name-sized, so it needs no size parameter and
 *    has no bad-size case to report. An algorithm held as RUNTIME state (like
 *    crypto/hash's streaming Crypto_Hash context) instead takes an enum, and
 *    every buffer it fills needs an explicit size parameter validated against
 *    that state. This module is all one-shots, so it is entirely the first
 *    kind; crypto/hash is the first kind for its one-shots and the second for
 *    its context. Keep new crypto leaves on the same rule.
 *
 *  Usage Examples:
 *    @code
 *    char mac[CRYPTO_HMAC_SHA256_HEX_SIZE + CHAR_END_CHARACTER] = DEFAULT_INITIALIZATION;
 *
 *    if (result_is_error(crypto_hmac_sha256_hex_1(key, key_size, message, message_size, mac))) {
 *        return false; // fail closed
 *    }
 *
 *    bool const ok = crypto_hmac_sha256_verify_1(key, key_size, message, message_size, expected_hex);
 *    @endcode
 *
 *  Error Handling:
 *    - The computing functions return RESULT_SUCCESS, or
 *      RESULT_CATEGORY_LIBRARY on OpenSSL failure (logged with the OpenSSL
 *      error text, then the thread's OpenSSL error queue is drained so stale
 *      residue is never misattributed to a later failure; output contents are
 *      then unspecified and must not be used — fail closed). They produce no
 *      RESULT_CATEGORY_ARGUMENT: every size is either caller-measured or
 *      fixed by the function's own name, and EVP_Q_mac takes size_t natively,
 *      so there is no boundary left to reject.
 *    - The verify functions return plain bool BY DESIGN: a caller must not be
 *      able to distinguish "wrong MAC" from "malformed input" from "library
 *      failure", because that distinction is an oracle. Verify compares the
 *      recomputed lowercase hex against expected_hex, case-insensitively,
 *      folded over the full length with no early exit; the length check runs
 *      first and is not secret (expected_hex is the caller's own string), so
 *      only the digest-hex fold itself needs to be constant-time. expected_hex
 *      must be NUL-terminated; message need not be — message_size governs its
 *      length.
 *    - Empty keys and empty messages are legal values, but the pointers must
 *      still be non-null: a null pointer is a contract violation and aborts
 *      like every other CFW argument (the caller guards an absent key or
 *      message before calling). An empty expected_hex, one of the wrong
 *      length, or any other non-matching text is a VALUE, not a violation,
 *      and answers false with no way to tell which case applied. The _2/_3
 *      Str/String message overloads map an empty Str/String (null data) to
 *      the empty message rather than aborting.
 *    - Required pointer arguments are validated first; a null argument aborts
 *      via error_check_null when ERROR_CHECK_ENABLED is defined. With that
 *      macro undefined the null checks compile out and a null pointer is
 *      undefined behaviour (EVP_Q_mac reads key/message directly; hex writes
 *      through output); the wrong-length verify refusal and the empty-value
 *      paths above are ordinary value branches that hold in every build.
 *
 *  Thread Safety:
 *    - Stateless; every function is safe to call concurrently.
 *
 *  Memory Management:
 *    - No heap allocation. The caller owns every output buffer: hex forms need
 *      CRYPTO_HMAC_<ALG>_HEX_SIZE + 1 chars (digest hex plus the NUL), raw
 *      forms need CRYPTO_HMAC_<ALG>_SIZE bytes. Each function's name fixes the
 *      size it writes, so the requirement is visible at the call site.
 *    - output must not overlap key or message. The raw MAC is staged in a
 *      stack buffer before hex-encoding, and verify's internal hex copy is
 *      likewise stack-resident; neither is zeroised on return — CFW has no
 *      memory_wipe primitive yet, so a caller holding secrets in its own
 *      buffers is responsible for scrubbing them.
 *
 *  Performance Characteristics:
 *    - O(n) in the message length: one OpenSSL MAC pass plus a fixed-size hex
 *      encode. Verification adds one full-length constant-time compare.
 *
 *  Dependencies (Deps):
 *    - OpenSSL >= 3.0 (<openssl/evp.h>, <openssl/err.h>), encoding/hex (hex.h),
 *      String (string.h — chains char → allocator → arena → memory → error,
 *      which provide types.h).
 *    - result.h, included directly, so the header compiles standalone.
 *    - That 3.0 floor is this module's own: EVP_Q_mac is the first 3.0-only
 *      symbol in the framework, and every other OpenSSL call in the tree still
 *      builds against 1.1.1. The floor is enforced below at compile time; the
 *      runtime half (a host whose libcrypto is older than its headers, since
 *      -lcrypto links dynamically) must be verified on the deployment host
 *      (`openssl version` reports the loaded libcrypto).
 * ============================================================================
 */
#ifndef CRYPTO_HMAC_H
#define CRYPTO_HMAC_H

#include <openssl/err.h>
#include <openssl/evp.h>

#include <result.h>
#include <container/string/string.h>
#include <encoding/hex/hex.h>

#if OPENSSL_VERSION_NUMBER < 0x30000000L
#error "crypto/hmac requires OpenSSL >= 3.0 (EVP_Q_mac)"
#endif // OPENSSL_VERSION_NUMBER < 0x30000000L

/*==============================================================================
 * MARK: - Macros
 *============================================================================*/

#define CRYPTO_HMAC_SHA256_HEX_SIZE 64  /**< Hex chars in a SHA-256 HMAC (excl. NUL). */
#define CRYPTO_HMAC_SHA256_SIZE     32  /**< Raw bytes in a SHA-256 HMAC. */
#define CRYPTO_HMAC_SHA512_HEX_SIZE 128 /**< Hex chars in a SHA-512 HMAC (excl. NUL). */
#define CRYPTO_HMAC_SHA512_SIZE     64  /**< Raw bytes in a SHA-512 HMAC. */

/*==============================================================================
 * MARK: - Public Functions
 *============================================================================*/

/**
 * @brief Compute a raw-byte HMAC-SHA256 of a byte message.
 * @param key          Secret key bytes (empty legal, non-null required).
 * @param key_size     Length of the key in bytes.
 * @param message      Message bytes to authenticate (empty legal, non-null required).
 * @param message_size Length of the message in bytes.
 * @param output       Destination; must hold CRYPTO_HMAC_SHA256_SIZE bytes.
 * @return RESULT_SUCCESS; RESULT_CATEGORY_LIBRARY on OpenSSL failure (output
 *         unspecified — fail closed).
 * @note U8 in, U8 out: this form exists for byte-domain constructions (key
 *       derivation, AEAD key material), whose buffers are U8 — matching
 *       crypto/hash's one-shots. The hex and verify forms stay char-typed
 *       because their inputs are text.
 */
CFW_ATTR_NODISCARD
Result crypto_hmac_sha256_bytes_1(U8 const *const key, USize const key_size, U8 const *const message, USize const message_size, U8 *const output);

/**
 * @brief Compute a lowercase-hex HMAC-SHA256 of a raw byte message.
 * @param key          Secret key bytes.
 * @param key_size     Length of the key in bytes.
 * @param message      Message bytes to authenticate.
 * @param message_size Length of the message in bytes.
 * @param output       Destination; must hold CRYPTO_HMAC_SHA256_HEX_SIZE + 1 chars.
 * @return RESULT_SUCCESS; RESULT_CATEGORY_LIBRARY on OpenSSL failure.
 */
CFW_ATTR_NODISCARD
Result crypto_hmac_sha256_hex_1(char const *const key, USize const key_size, char const *const message, USize const message_size, char *const output);

/**
 * @brief Compute a lowercase-hex HMAC-SHA256 of a Str message.
 * @param key      Secret key bytes.
 * @param key_size Length of the key in bytes.
 * @param message  Message to authenticate.
 * @param output   Destination; must hold CRYPTO_HMAC_SHA256_HEX_SIZE + 1 chars.
 * @return RESULT_SUCCESS; RESULT_CATEGORY_LIBRARY on OpenSSL failure.
 */
CFW_ATTR_NODISCARD
Result crypto_hmac_sha256_hex_2(char const *const key, USize const key_size, Str const *const message, char *const output);

/**
 * @brief Compute a lowercase-hex HMAC-SHA256 of a String message.
 * @param key      Secret key bytes.
 * @param key_size Length of the key in bytes.
 * @param message  Message to authenticate.
 * @param output   Destination; must hold CRYPTO_HMAC_SHA256_HEX_SIZE + 1 chars.
 * @return RESULT_SUCCESS; RESULT_CATEGORY_LIBRARY on OpenSSL failure.
 */
CFW_ATTR_NODISCARD
Result crypto_hmac_sha256_hex_3(char const *const key, USize const key_size, String const *const message, char *const output);

/**
 * @brief Compute an uppercase-hex HMAC-SHA256 of a raw byte message.
 * @param key          Secret key bytes.
 * @param key_size     Length of the key in bytes.
 * @param message      Message bytes to authenticate.
 * @param message_size Length of the message in bytes.
 * @param output       Destination; must hold CRYPTO_HMAC_SHA256_HEX_SIZE + 1 chars.
 * @return RESULT_SUCCESS; RESULT_CATEGORY_LIBRARY on OpenSSL failure.
 */
CFW_ATTR_NODISCARD
Result crypto_hmac_sha256_hex_upper_1(char const *const key, USize const key_size, char const *const message, USize const message_size, char *const output);

/**
 * @brief Verify a raw byte message against an expected hex MAC (constant-time).
 * @param key          Secret key bytes.
 * @param key_size     Length of the key in bytes.
 * @param message      Message bytes to authenticate.
 * @param message_size Length of the message in bytes.
 * @param expected_hex Expected lowercase/uppercase hex MAC (compared case-insensitively).
 * @return true if the recomputed MAC equals expected_hex, false otherwise.
 */
bool crypto_hmac_sha256_verify_1(char const *const key, USize const key_size, char const *const message, USize const message_size, char const *const expected_hex);

/**
 * @brief Verify a Str message against an expected hex MAC (constant-time).
 * @param key          Secret key bytes.
 * @param key_size     Length of the key in bytes.
 * @param message      Message to authenticate.
 * @param expected_hex Expected hex MAC (compared case-insensitively).
 * @return true if the recomputed MAC equals expected_hex, false otherwise.
 */
bool crypto_hmac_sha256_verify_2(char const *const key, USize const key_size, Str const *const message, char const *const expected_hex);

/**
 * @brief Verify a String message against an expected hex MAC (constant-time).
 * @param key          Secret key bytes.
 * @param key_size     Length of the key in bytes.
 * @param message      Message to authenticate.
 * @param expected_hex Expected hex MAC (compared case-insensitively).
 * @return true if the recomputed MAC equals expected_hex, false otherwise.
 */
bool crypto_hmac_sha256_verify_3(char const *const key, USize const key_size, String const *const message, char const *const expected_hex);

/**
 * @brief Compute a raw-byte HMAC-SHA512 of a byte message.
 * @param key          Secret key bytes (empty legal, non-null required).
 * @param key_size     Length of the key in bytes.
 * @param message      Message bytes to authenticate (empty legal, non-null required).
 * @param message_size Length of the message in bytes.
 * @param output       Destination; must hold CRYPTO_HMAC_SHA512_SIZE bytes.
 * @return RESULT_SUCCESS; RESULT_CATEGORY_LIBRARY on OpenSSL failure (output
 *         unspecified — fail closed).
 * @note U8 in, U8 out — see crypto_hmac_sha256_bytes_1.
 */
CFW_ATTR_NODISCARD
Result crypto_hmac_sha512_bytes_1(U8 const *const key, USize const key_size, U8 const *const message, USize const message_size, U8 *const output);

/**
 * @brief Compute a lowercase-hex HMAC-SHA512 of a raw byte message.
 * @param key          Secret key bytes.
 * @param key_size     Length of the key in bytes.
 * @param message      Message bytes to authenticate.
 * @param message_size Length of the message in bytes.
 * @param output       Destination; must hold CRYPTO_HMAC_SHA512_HEX_SIZE + 1 chars.
 * @return RESULT_SUCCESS; RESULT_CATEGORY_LIBRARY on OpenSSL failure.
 */
CFW_ATTR_NODISCARD
Result crypto_hmac_sha512_hex_1(char const *const key, USize const key_size, char const *const message, USize const message_size, char *const output);

/**
 * @brief Compute a lowercase-hex HMAC-SHA512 of a Str message.
 * @param key      Secret key bytes.
 * @param key_size Length of the key in bytes.
 * @param message  Message to authenticate.
 * @param output   Destination; must hold CRYPTO_HMAC_SHA512_HEX_SIZE + 1 chars.
 * @return RESULT_SUCCESS; RESULT_CATEGORY_LIBRARY on OpenSSL failure.
 */
CFW_ATTR_NODISCARD
Result crypto_hmac_sha512_hex_2(char const *const key, USize const key_size, Str const *const message, char *const output);

/**
 * @brief Compute a lowercase-hex HMAC-SHA512 of a String message.
 * @param key      Secret key bytes.
 * @param key_size Length of the key in bytes.
 * @param message  Message to authenticate.
 * @param output   Destination; must hold CRYPTO_HMAC_SHA512_HEX_SIZE + 1 chars.
 * @return RESULT_SUCCESS; RESULT_CATEGORY_LIBRARY on OpenSSL failure.
 */
CFW_ATTR_NODISCARD
Result crypto_hmac_sha512_hex_3(char const *const key, USize const key_size, String const *const message, char *const output);

/**
 * @brief Compute an uppercase-hex HMAC-SHA512 of a raw byte message.
 * @param key          Secret key bytes.
 * @param key_size     Length of the key in bytes.
 * @param message      Message bytes to authenticate.
 * @param message_size Length of the message in bytes.
 * @param output       Destination; must hold CRYPTO_HMAC_SHA512_HEX_SIZE + 1 chars.
 * @return RESULT_SUCCESS; RESULT_CATEGORY_LIBRARY on OpenSSL failure.
 */
CFW_ATTR_NODISCARD
Result crypto_hmac_sha512_hex_upper_1(char const *const key, USize const key_size, char const *const message, USize const message_size, char *const output);

/**
 * @brief Verify a raw byte message against an expected SHA-512 hex MAC (constant-time).
 * @param key          Secret key bytes.
 * @param key_size     Length of the key in bytes.
 * @param message      Message bytes to authenticate.
 * @param message_size Length of the message in bytes.
 * @param expected_hex Expected lowercase/uppercase hex MAC (compared case-insensitively).
 * @return true if the recomputed MAC equals expected_hex, false otherwise.
 */
bool crypto_hmac_sha512_verify_1(char const *const key, USize const key_size, char const *const message, USize const message_size, char const *const expected_hex);

/**
 * @brief Verify a Str message against an expected SHA-512 hex MAC (constant-time).
 * @param key          Secret key bytes.
 * @param key_size     Length of the key in bytes.
 * @param message      Message to authenticate.
 * @param expected_hex Expected hex MAC (compared case-insensitively).
 * @return true if the recomputed MAC equals expected_hex, false otherwise.
 */
bool crypto_hmac_sha512_verify_2(char const *const key, USize const key_size, Str const *const message, char const *const expected_hex);

/**
 * @brief Verify a String message against an expected SHA-512 hex MAC (constant-time).
 * @param key          Secret key bytes.
 * @param key_size     Length of the key in bytes.
 * @param message      Message to authenticate.
 * @param expected_hex Expected hex MAC (compared case-insensitively).
 * @return true if the recomputed MAC equals expected_hex, false otherwise.
 */
bool crypto_hmac_sha512_verify_3(char const *const key, USize const key_size, String const *const message, char const *const expected_hex);

#endif // CRYPTO_HMAC_H