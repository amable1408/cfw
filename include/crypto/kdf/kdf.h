/* ============================================================================
 *  Crypto KDF
 *  --------------------------------------------------------------------------
 *  @file    kdf.h
 *  @brief   Key derivation functions over OpenSSL — PBKDF2-HMAC-SHA256.
 *  @author  CFW
 *  @date    2026-08-09
 *  @license MIT (see LICENSE file)
 *
 *  The raw key-derivation leaf of the crypto area: it turns a low-entropy
 *  secret (a password) plus a salt into a fixed-size derived key, with a
 *  caller-chosen iteration cost. It carries NO storage-format or policy
 *  opinions — record formats, salts, and rehash policy live in
 *  crypto/password, which composes this module. Callers wanting to hash or
 *  verify passwords should use crypto/password, not this directly.
 *
 *  Features:
 *    - crypto_kdf_pbkdf2_sha256: RFC 2898 PBKDF2 with HMAC-SHA256, arbitrary
 *      salt and output sizes, caller-chosen iterations.
 *
 *  Usage Examples:
 *    @code
 *    #include <char/char.h>
 *    #include <crypto/kdf/kdf.h>
 *
 *    U8 key[32] = DEFAULT_INITIALIZATION;
 *
 *    if (result_is_error(crypto_kdf_pbkdf2_sha256(password, char_length(password), salt, sizeof(salt), iterations, key, sizeof(key)))) {
 *        return false; // fail closed
 *    }
 *    @endcode
 *
 *  Error Handling:
 *    - Returns RESULT_SUCCESS; RESULT_CATEGORY_ARGUMENT when salt_size,
 *      iterations, or output_size is zero, or any of password_size,
 *      salt_size, iterations, or output_size exceeds the OpenSSL int boundary
 *      (INT_MAX) — the narrowing casts are guarded, never silent, and output
 *      is not written at all on this path. This leaf enforces only that int
 *      boundary; the 10,000,000-iteration policy cap
 *      (CRYPTO_PASSWORD_ITERATIONS_MAX) and the 600,000 default live one
 *      layer up, in crypto/password.
 *    - RESULT_CATEGORY_LIBRARY on OpenSSL failure: the Result carries the
 *      category alone — code 0, since OpenSSL's packed ERR codes do not fit
 *      the 16-bit code field — and the reason lives in the log, not the code,
 *      so no caller should switch on it; the OpenSSL error text is logged and
 *      the thread's OpenSSL error queue is drained afterward; output contents
 *      are then unspecified and must not be used — fail closed.
 *    - password_size 0 is legal (an empty password is a value; rejecting empty
 *      passwords is policy and belongs to the caller).
 *    - Required pointer arguments are validated first; a null argument aborts
 *      via error_check_null when ERROR_CHECK_ENABLED is defined. With
 *      ERROR_CHECK_ENABLED compiled out those null checks disappear and a
 *      null password, salt, or output is undefined behaviour — handed
 *      straight to OpenSSL — while the zero-size and INT_MAX refusals above
 *      are value branches that hold in every build; all three pointer
 *      arguments are non-nullable in every build.
 *
 *  Thread Safety:
 *    - Stateless; safe to call concurrently.
 *
 *  Memory Management:
 *    - No heap allocation; the caller owns every buffer. output must not
 *      overlap password or salt — OpenSSL re-reads both per block while
 *      writing output progressively, so an overlapping buffer's behaviour is
 *      undefined.
 *    - Nothing here is wiped: the derived key left in output (and any partial
 *      bytes written before a LIBRARY failure) persists until the caller
 *      overwrites it. The module holds no secret locals of its own. CFW has
 *      no memory_wipe primitive yet — a caller holding key material is
 *      responsible for clearing its own buffers when done.
 *
 *  Performance Characteristics:
 *    - Deliberately expensive: linear in iterations (that is the point of a
 *      password KDF). crypto/password's 600,000-iteration OWASP-2023 default
 *      is the number to start from when budgeting against login latency.
 *
 *  Dependencies (Deps):
 *    - OpenSSL (<openssl/evp.h>, <openssl/err.h>), error (error.h — chains
 *      tracelog → log → thread, which provide limits.h and types.h).
 *    - result.h, included directly so the header compiles standalone.
 * ============================================================================
 */
#ifndef CRYPTO_KDF_H
#define CRYPTO_KDF_H

#include <openssl/err.h>
#include <openssl/evp.h>

#include <result.h>
#include <error/error.h>

/*==============================================================================
 * MARK: - Public Functions
 *============================================================================*/

/**
 * @brief Derive output_size key bytes from a password via PBKDF2-HMAC-SHA256.
 * @param password      Secret input bytes (empty is legal).
 * @param password_size Length of password in bytes.
 * @param salt          Salt bytes.
 * @param salt_size     Length of salt in bytes.
 * @param iterations    PBKDF2 rounds (cost factor).
 * @param output        Destination for the derived key; must not overlap
 *                       password or salt.
 * @param output_size   Number of key bytes to derive.
 * @return RESULT_SUCCESS; RESULT_CATEGORY_ARGUMENT on a zero salt_size /
 *         iterations / output_size or any value above INT_MAX;
 *         RESULT_CATEGORY_LIBRARY on OpenSSL failure (output unspecified —
 *         fail closed).
 */
CFW_ATTR_NODISCARD
Result crypto_kdf_pbkdf2_sha256(
    char const *const password, USize const password_size, U8 const *const salt, USize const salt_size, USize const iterations, U8 *const output, USize const output_size);

#endif // CRYPTO_KDF_H