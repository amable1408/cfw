/* ============================================================================
 *  Crypto Password
 *  --------------------------------------------------------------------------
 *  @file    password.h
 *  @brief   Password hashing, verification, and rehash policy over
 *           PBKDF2-HMAC-SHA256 records.
 *  @author  CFW
 *  @date    2026-08-09
 *  @license MIT (see LICENSE file)
 *
 *  The password-record policy layer of the crypto area: it owns the stored
 *  format, the salt, and the rehash rule, composing crypto/kdf (derivation),
 *  crypto/random (salt), and encoding/hex (text form). Any caller needing to
 *  hash or verify a password uses this module — never crypto/kdf directly and
 *  never a hand-rolled format.
 *
 *  The stored format is a LOCKED compatibility contract:
 *      pbkdf2_sha256$<iterations>$<salt hex>$<hash hex>
 *  Records written by earlier versions of this module keep verifying — the
 *  field order, separator, scheme tag, and hex casing on write never change.
 *  The scheme tag matches Django's naming, but the salt and hash fields are
 *  hex, not Django's base64/alnum, so a Django-produced record does NOT
 *  verify here, and this is not a PHC string either; reading accepts hex in
 *  either case even though this module always writes lowercase.
 *
 *  Features:
 *    - crypto_password_hash_1/_2/_3 (+ alloc variants): salt, derive, and
 *      format a stored record for a char* / Str / String password.
 *    - crypto_password_verify_1/_2/_3: constant-time verification against a
 *      stored record; malformed records are simply false.
 *    - crypto_password_needs_rehash: true when a (valid) stored record's
 *      iteration count differs from the configured one — bump deployments
 *      re-hash on the next successful login, no migration required.
 *
 *  Usage Examples:
 *    @code
 *    String stored = crypto_password_hash_1(password, CRYPTO_PASSWORD_DEFAULT_ITERATIONS);
 *
 *    if (!string_empty(&stored)) { // empty = hashing failed (fail closed)
 *        if (crypto_password_verify_1(password, string_get_data(&stored))
 *            && crypto_password_needs_rehash(string_get_data(&stored), iterations)) {
 *            // re-hash and persist
 *        }
 *    }
 *
 *    string_uninit(&stored);
 *    @endcode
 *
 *  Error Handling:
 *    - Hashing returns an empty String on any failure (CSPRNG, KDF, or an
 *      iteration count of zero / above CRYPTO_PASSWORD_ITERATIONS_MAX) — fail
 *      closed; callers must check string_empty before persisting. CSPRNG and
 *      KDF failures are logged through the log module at ERROR level; call
 *      log_init before the first call, otherwise the report ends the process
 *      instead of returning an empty String.
 *    - Verification returns only true or false by design: a richer failure
 *      taxonomy on a verify would itself be an oracle. "Constant-time" covers
 *      only the derived-key compare (a full-length fold with no early exit);
 *      a malformed or oversized stored record returns false immediately,
 *      since record shape is not secret. A login route that must hide
 *      account existence hashes one dummy record at startup and verifies
 *      unknown accounts against it — that is the caller's pattern, not this
 *      module's behaviour.
 *    - Required pointer arguments are validated first; `stored` must be
 *      non-null and NUL-terminated, and a null argument aborts via
 *      error_check_null when ERROR_CHECK_ENABLED is defined. With
 *      ERROR_CHECK_ENABLED compiled out, the null checks disappear and a null
 *      pointer is undefined behaviour; the empty/malformed/oversized record
 *      refusals and the iteration-count bounds are value branches that hold
 *      in every build.
 *
 *  Thread Safety:
 *    - Stateless; safe to call concurrently.
 *
 *  Memory Management:
 *    - Heap hash results are owned by the caller (string_uninit); arena
 *      (alloc) results follow the arena's lifetime.
 *    - The salt, derived hash, verify candidate, and their hex forms live on
 *      the stack and are not zeroised on return; CFW has no memory_wipe yet.
 *      A caller that must erase the plaintext password scrubs its own buffer.
 *
 *  Performance Characteristics:
 *    - Dominated by the KDF iteration count, deliberately (see crypto/kdf).
 *    - CRYPTO_PASSWORD_DEFAULT_ITERATIONS is 600000, matching OWASP 2023
 *      guidance for PBKDF2-HMAC-SHA256 — roughly 100 ms per hash on a
 *      2024-class x86 core, about 4x an older 210000-round record. Callers
 *      run this off any shared lock.
 *
 *  Dependencies (Deps):
 *    - container/string (string.h — provides char/str chains), crypto/kdf
 *      (kdf.h — chains OpenSSL evp/err and the error chain), crypto/random
 *      (random.h — chains encoding/hex).
 * ============================================================================
 */
#ifndef CRYPTO_PASSWORD_H
#define CRYPTO_PASSWORD_H

#include <container/string/string.h>
#include <crypto/kdf/kdf.h>
#include <crypto/random/random.h>

/*==============================================================================
 * MARK: - Macros
 *============================================================================*/

#define CRYPTO_PASSWORD_DEFAULT_ITERATIONS 600000   /**< Default PBKDF2 rounds; raising it deploys via needs_rehash, never a bulk migration. See Performance below. */
#define CRYPTO_PASSWORD_HASH_BYTE_COUNT 32          /**< Derived-key bytes (SHA-256 width). */
#define CRYPTO_PASSWORD_ITERATIONS_MAX 10000000     /**< Hard cap for both hashing and stored-record parsing. */
#define CRYPTO_PASSWORD_SALT_BYTE_COUNT 16          /**< Random salt bytes drawn per hash. */
#define CRYPTO_PASSWORD_SCHEME "pbkdf2_sha256"      /**< Scheme tag leading every stored record. */

/*==============================================================================
 * MARK: - Public Functions
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Hash a char* password into an arena-backed stored record.
 * @param password   Plain password (NUL-terminated; empty is legal — a
 *                   minimum-length policy belongs to your login policy
 *                   layer).
 * @param iterations PBKDF2 rounds; 0 or above CRYPTO_PASSWORD_ITERATIONS_MAX fails.
 * @param allocator  Arena allocator.
 * @return Stored record, or an empty String on failure (fail closed).
 */
String crypto_password_alloc_hash_1(char const *const password, USize const iterations, Arena *const allocator);

/**
 * @brief Hash a Str password into an arena-backed stored record.
 * @param password   Plain password (empty is a legal value).
 * @param iterations PBKDF2 rounds; 0 or above CRYPTO_PASSWORD_ITERATIONS_MAX fails.
 * @param allocator  Arena allocator.
 * @return Stored record, or an empty String on failure (fail closed).
 */
String crypto_password_alloc_hash_2(Str const *const password, USize const iterations, Arena *const allocator);

/**
 * @brief Hash a String password into an arena-backed stored record.
 * @param password   Plain password (empty is a legal value).
 * @param iterations PBKDF2 rounds; 0 or above CRYPTO_PASSWORD_ITERATIONS_MAX fails.
 * @param allocator  Arena allocator.
 * @return Stored record, or an empty String on failure (fail closed).
 */
String crypto_password_alloc_hash_3(String const *const password, USize const iterations, Arena *const allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Hash a char* password into a heap stored record.
 * @param password   Plain password (NUL-terminated; empty is a legal value).
 * @param iterations PBKDF2 rounds; 0 or above CRYPTO_PASSWORD_ITERATIONS_MAX fails.
 * @return Stored record (caller must string_uninit), or an empty String on
 *         failure (fail closed).
 */
String crypto_password_hash_1(char const *const password, USize const iterations);

/**
 * @brief Hash a Str password into a heap stored record.
 * @param password   Plain password (empty is a legal value).
 * @param iterations PBKDF2 rounds; 0 or above CRYPTO_PASSWORD_ITERATIONS_MAX fails.
 * @return Stored record (caller must string_uninit), or an empty String on
 *         failure (fail closed).
 */
String crypto_password_hash_2(Str const *const password, USize const iterations);

/**
 * @brief Hash a String password into a heap stored record.
 * @param password   Plain password (empty is a legal value).
 * @param iterations PBKDF2 rounds; 0 or above CRYPTO_PASSWORD_ITERATIONS_MAX fails.
 * @return Stored record (caller must string_uninit), or an empty String on
 *         failure (fail closed).
 */
String crypto_password_hash_3(String const *const password, USize const iterations);

/**
 * @brief Check whether a valid stored record's iteration count differs from
 *        the configured one.
 * @param stored     Stored record text.
 * @param iterations Currently configured PBKDF2 rounds.
 * @return true when the record parses and its count differs (rehash on next
 *         login); false for a matching count or a malformed record.
 */
bool crypto_password_needs_rehash(char const *const stored, USize const iterations);

/**
 * @brief Verify a char* password against a stored record (constant-time).
 * @param password Plain password (NUL-terminated; empty is a legal value).
 * @param stored   Stored record text.
 * @return true only when the record is well-formed and the derived key
 *         matches; malformed records and derivation failures are false.
 */
bool crypto_password_verify_1(char const *const password, char const *const stored);

/**
 * @brief Verify a Str password against a stored record (constant-time).
 * @param password Plain password (empty is a legal value).
 * @param stored   Stored record text.
 * @return true only when the record is well-formed and the derived key matches.
 */
bool crypto_password_verify_2(Str const *const password, char const *const stored);

/**
 * @brief Verify a String password against a stored record (constant-time).
 * @param password Plain password (empty is a legal value).
 * @param stored   Stored record text.
 * @return true only when the record is well-formed and the derived key matches.
 */
bool crypto_password_verify_3(String const *const password, char const *const stored);

#endif // CRYPTO_PASSWORD_H