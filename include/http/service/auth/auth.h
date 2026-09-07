/*
 * auth.h - HTTP authentication service for the C Libraries Framework
 *
 * Handles authentication primitives without owning account storage, sessions,
 * database writes, or mail delivery. Applications persist returned hashes and
 * tokens in their own storage layer.
 *
 * Features:
 *   - Password policy validation with a machine-readable reason.
 *   - Password hashing and verification, with a service-owned decoy hash that
 *     keeps an unknown account indistinguishable from a wrong password.
 *   - Random reset and email verification token generation.
 *   - SHA-256 token hashing for storage, and verification against the stored
 *     hash rather than a stored plaintext token.
 *   - Deterministic token expiration checks.
 *
 * Usage Examples:
 *   @code
 *   // Registration: policy, then hash.
 *   HTTP_Service_Auth auth = http_service_auth_init_1();
 *
 *   if (http_service_auth_password_reason(&auth, password) == HTTP_SERVICE_AUTH_PASSWORD_OK) {
 *       String hash = http_service_auth_hash_password(&auth, password);
 *
 *       // persist string_get_data(&hash)
 *       string_uninit(&hash);
 *   }
 *
 *   // Login: verify_password_2 runs the decoy KDF when the account is unknown,
 *   // so a missing row costs the same wall time as a wrong password.
 *   String stored = string_init_1();   // empty when no account row was found
 *
 *   if (http_service_auth_verify_password_2(&auth, password, &stored)) {
 *       if (http_service_auth_password_needs_rehash(&auth, &stored)) {
 *           String fresh = http_service_auth_hash_password(&auth, password);
 *
 *           // UPDATE users SET password_hash = fresh WHERE password_hash = stored
 *           string_uninit(&fresh);
 *       }
 *   }
 *
 *   string_uninit(&stored);
 *
 *   // Reset token: hand the VALUE to the user, persist only the HASH.
 *   HTTP_Service_Auth_Token token = http_service_auth_reset_token_create(&auth);
 *
 *   if (!string_empty(&token.value)) {
 *       String token_hash = http_service_auth_token_hash(&auth, string_get_data(&token.value));
 *
 *       // persist token_hash + token.expires_at; mail string_get_data(&token.value)
 *       string_uninit(&token_hash);
 *   }
 *
 *   http_service_auth_token_uninit(&token);
 *
 *   // Consume: the stored hash and the stored expiry, checked in one call. Both come from
 *   // the row the link's token identifies; the candidate is what the visitor presented.
 *   char const *const   candidate           = "the hex token the mail link carried";
 *   String              stored_token_hash   = string_init_1();   // loaded from storage
 *   USize const         stored_expires_at   = 0;                 // loaded from storage
 *
 *   bool const ok = http_service_auth_token_verify_2(&auth, candidate, &stored_token_hash, stored_expires_at);
 *
 *   string_uninit(&stored_token_hash);
 *
 *   http_service_auth_uninit(&auth);
 *   @endcode
 *
 * Error Handling:
 *   - Public functions validate non-null pointers.
 *   - Invalid cryptographic operations return empty strings or false.
 *   - Value-dependent refusals never abort: an empty password, an empty stored
 *     hash and a malformed stored record all answer false.
 *
 * Thread Safety:
 *   - Safe from several threads only when the instance's allocator is nullptr
 *     (the heap path) or the caller serializes access. An arena-backed instance
 *     shares one Arena, and Arena is not thread-safe.
 *   - No function mutates the service after initialization, so a heap-backed
 *     instance may be read concurrently.
 *
 * Memory Management:
 *   - Returned String and token values must be uninitialized by caller.
 *   - Call http_service_auth_token_uninit() for token values.
 *   - The service OWNS its decoy hash: http_service_auth_uninit() releases it.
 *     An arena-backed instance must not outlive the arena it was built from.
 *   - Every accessor commits to a single tier: values arrive as null-terminated
 *     `char const *`, exactly as session does. Sized request data (a JSON node,
 *     a Str) is terminated by the caller before it reaches this API; no `_2`
 *     sized forms exist, and none will be added without a MAJOR bump.
 *
 * Performance Characteristics:
 *   - Password hashing cost is controlled by password_hash_iterations.
 *   - Initialization computes ONE decoy hash, so http_service_auth_init_1()
 *     costs one full KDF run (~100 ms at the default iteration count). Build
 *     the service once per process, never per request.
 *   - Token hashing is one SHA-256 pass over the token.
 *
 * Dependencies:
 *   - crypto/hash, crypto/password (password.h — chains crypto/kdf with OpenSSL,
 *     crypto/random, encoding/hex, and container/string), datetime.
 *
 * See auth.c for implementation details.
 */

#ifndef HTTP_SERVICE_AUTH_H
#define HTTP_SERVICE_AUTH_H

#include <crypto/hash/hash.h>
#include <crypto/password/password.h>
#include <datetime/datetime.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

#define HTTP_SERVICE_AUTH_DEFAULT_EMAIL_TOKEN_TTL 86400
#define HTTP_SERVICE_AUTH_DEFAULT_HASH_ITERATIONS CRYPTO_PASSWORD_DEFAULT_ITERATIONS
#define HTTP_SERVICE_AUTH_DEFAULT_PASSWORD_MAX_LENGTH 256
#define HTTP_SERVICE_AUTH_DEFAULT_PASSWORD_MIN_LENGTH 12
#define HTTP_SERVICE_AUTH_DEFAULT_RESET_TOKEN_TTL 900
#define HTTP_SERVICE_AUTH_DEFAULT_TOKEN_BYTE_COUNT 32
#define HTTP_SERVICE_AUTH_PASSWORD_MAX_LENGTH_CEILING 1024
#define HTTP_SERVICE_AUTH_PASSWORD_MAX_LENGTH_FLOOR 128

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief Why a password failed the service policy.
 */
typedef enum {
    /** @brief The password satisfies every policy rule. */
    HTTP_SERVICE_AUTH_PASSWORD_OK = 0,
    /** @brief The password is longer than password_max_length bytes. */
    HTTP_SERVICE_AUTH_PASSWORD_TOO_LONG,
    /** @brief The password is shorter than password_min_length bytes. */
    HTTP_SERVICE_AUTH_PASSWORD_TOO_SHORT
} HTTP_Service_Auth_Password_Reason;

/**
 * @brief Authentication primitive configuration.
 */
typedef struct {
#ifdef ARENA_IMPLEMENTATION
    /** @brief Optional arena used by returned hash and token strings. */
    Arena *allocator;
#endif // ARENA_IMPLEMENTATION
    /** @brief Service-owned hash a verification of an unknown account runs against. */
    String decoy_hash;
    /** @brief Email verification token time-to-live in seconds. */
    USize email_token_ttl;
    /** @brief Password hash iteration count. */
    USize password_hash_iterations;
    /** @brief Maximum accepted password length in bytes. */
    USize password_max_length;
    /** @brief Minimum accepted password length in bytes. */
    USize password_min_length;
    /** @brief Password reset token time-to-live in seconds. */
    USize reset_token_ttl;
    /** @brief Random bytes generated per token before encoding. */
    USize token_byte_count;
} HTTP_Service_Auth;

/**
 * @brief Random token value with expiration timestamp.
 */
typedef struct {
    /** @brief Expiration timestamp in seconds. */
    USize expires_at;
    /** @brief Encoded random token value. */
    String value;
} HTTP_Service_Auth_Token;

/*==============================================================================
 * MARK: - API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Initialize an arena-backed auth service with default values.
 * @param allocator Arena allocator.
 * @return Initialized service. Costs one KDF run for the decoy hash.
 */
HTTP_Service_Auth http_service_auth_alloc_init_1(Arena *const allocator);

/**
 * @brief Initialize an arena-backed auth service with explicit values.
 * @param password_min_length Minimum accepted password length.
 * @param password_hash_iterations Password hash iteration count.
 * @param token_byte_count Random bytes per token.
 * @param reset_token_ttl Reset token time-to-live in seconds.
 * @param email_token_ttl Email token time-to-live in seconds.
 * @param allocator Arena allocator.
 * @return Initialized service with the default password_max_length.
 * @note That default ceiling bounds password_min_length: a minimum above
 *       HTTP_SERVICE_AUTH_DEFAULT_PASSWORD_MAX_LENGTH aborts on a maximum this
 *       form does not take. Use http_service_auth_alloc_init_3() to raise both.
 */
HTTP_Service_Auth http_service_auth_alloc_init_2(
    USize const password_min_length, USize const password_hash_iterations, USize const token_byte_count, USize const reset_token_ttl, USize const email_token_ttl, Arena *const allocator);

/**
 * @brief Initialize an arena-backed auth service with an explicit password maximum.
 * @param password_min_length Minimum accepted password length.
 * @param password_max_length Maximum accepted password length. Must lie in
 *        [HTTP_SERVICE_AUTH_PASSWORD_MAX_LENGTH_FLOOR,
 *        HTTP_SERVICE_AUTH_PASSWORD_MAX_LENGTH_CEILING] and be at least
 *        password_min_length.
 * @param password_hash_iterations Password hash iteration count.
 * @param token_byte_count Random bytes per token.
 * @param reset_token_ttl Reset token time-to-live in seconds.
 * @param email_token_ttl Email token time-to-live in seconds.
 * @param allocator Arena allocator.
 * @return Initialized service.
 */
HTTP_Service_Auth http_service_auth_alloc_init_3(
    USize const password_min_length, USize const password_max_length, USize const password_hash_iterations, USize const token_byte_count, USize const reset_token_ttl,
    USize const email_token_ttl, Arena *const allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Create an email verification token using service defaults.
 * @param self Service instance.
 * @return Token value and expiration timestamp. On CSPRNG failure the value is
 *         an empty String (fail closed) — check string_empty before use.
 */
HTTP_Service_Auth_Token http_service_auth_email_token_create(HTTP_Service_Auth const *const self);

/**
 * @brief Hash a password using the service password hash settings.
 * @param self Service instance.
 * @param password Plain password.
 * @return Encoded password hash. Caller must uninitialize it. Empty on a KDF or
 *         CSPRNG failure AND for a password past password_max_length, which is
 *         refused before any KDF runs. The two cases are indistinguishable from
 *         the return alone — run http_service_auth_password_reason() first when
 *         the caller must tell them apart.
 */
String http_service_auth_hash_password(HTTP_Service_Auth const *const self, char const *const password);

/**
 * @brief Initialize auth service with default values.
 * @return Initialized service. Costs one KDF run for the decoy hash.
 */
HTTP_Service_Auth http_service_auth_init_1(void);

/**
 * @brief Initialize auth service with explicit values.
 * @param password_min_length Minimum accepted password length.
 * @param password_hash_iterations Password hash iteration count.
 * @param token_byte_count Random bytes per token.
 * @param reset_token_ttl Reset token time-to-live in seconds.
 * @param email_token_ttl Email token time-to-live in seconds.
 * @return Initialized service with the default password_max_length.
 * @note That default ceiling bounds password_min_length: a minimum above
 *       HTTP_SERVICE_AUTH_DEFAULT_PASSWORD_MAX_LENGTH aborts on a maximum this
 *       form does not take. Use http_service_auth_init_3() to raise both.
 */
HTTP_Service_Auth http_service_auth_init_2(
    USize const password_min_length, USize const password_hash_iterations, USize const token_byte_count, USize const reset_token_ttl, USize const email_token_ttl);

/**
 * @brief Initialize auth service with an explicit password maximum.
 * @param password_min_length Minimum accepted password length.
 * @param password_max_length Maximum accepted password length. Must lie in
 *        [HTTP_SERVICE_AUTH_PASSWORD_MAX_LENGTH_FLOOR,
 *        HTTP_SERVICE_AUTH_PASSWORD_MAX_LENGTH_CEILING] and be at least
 *        password_min_length. It is the DoS bound: a body past it is refused
 *        before any KDF runs.
 * @param password_hash_iterations Password hash iteration count.
 * @param token_byte_count Random bytes per token.
 * @param reset_token_ttl Reset token time-to-live in seconds.
 * @param email_token_ttl Email token time-to-live in seconds.
 * @return Initialized service.
 */
HTTP_Service_Auth http_service_auth_init_3(
    USize const password_min_length, USize const password_max_length, USize const password_hash_iterations, USize const token_byte_count, USize const reset_token_ttl,
    USize const email_token_ttl);

/**
 * @brief Check whether a stored password hash needs re-hashing under the
 *        service's configured iteration count.
 * @param self Service instance.
 * @param hash Stored password hash record.
 * @return true when the record is valid and its iteration count differs from
 *         the configured one (a warning is logged); false for a matching
 *         count, an empty hash, or a malformed record.
 * @note Persist the new hash as a COMPARE-AND-SWAP against the record that was
 *       just verified, never as a bare write:
 *       `UPDATE users SET password_hash = :fresh WHERE id = :id AND password_hash = :verified`.
 *       A naive persist races an administrator reset or a concurrent password
 *       change and silently reinstates the old credential.
 * @note Re-hashing also closes a TIMING gap, not just a strength one: a record
 *       still at the old, lower count verifies faster than the decoy
 *       http_service_auth_verify_password_2() runs for an unknown account, so
 *       until this rehash lands that account is distinguishable by latency —
 *       see that function's note. Every dormant account stays legacy, and so
 *       stays distinguishable, until its owner logs in.
 * @note A hit is logged at INFO, not WARN: during an iteration-count migration
 *       every legacy login takes this branch, so it is the expected path.
 */
bool http_service_auth_password_needs_rehash(HTTP_Service_Auth const *const self, String const *const hash);

/**
 * @brief Report why a password fails the service policy.
 * @param self Service instance.
 * @param password Plain password.
 * @return HTTP_SERVICE_AUTH_PASSWORD_OK, or the first rule the password breaks.
 *         Lengths are counted in BYTES, not characters: twelve bytes of UTF-8
 *         can be as few as three characters.
 */
HTTP_Service_Auth_Password_Reason http_service_auth_password_reason(HTTP_Service_Auth const *const self, char const *const password);

/**
 * @brief Validate password against the service policy.
 * @param self Service instance.
 * @param password Plain password.
 * @return true when password passes policy. Use
 *         http_service_auth_password_reason() when the route must tell the user
 *         which rule failed.
 */
bool http_service_auth_password_valid(HTTP_Service_Auth const *const self, char const *const password);

/**
 * @brief Create a password reset token using service defaults.
 * @param self Service instance.
 * @return Token value and expiration timestamp. On CSPRNG failure the value is
 *         an empty String (fail closed) — check string_empty before use.
 */
HTTP_Service_Auth_Token http_service_auth_reset_token_create(HTTP_Service_Auth const *const self);

/**
 * @brief Create a random token with caller-defined time-to-live.
 * @param self Service instance.
 * @param ttl Token time-to-live in seconds. Must be non-zero: 0 is a contract
 *        violation and ABORTS, it is not a way to mint an already-expired
 *        token. Build one of those by storing an expires_at already in the
 *        past, or by reading USIZE_MAX, the permanently-expired sentinel.
 * @return Token value and expiration timestamp. On CSPRNG failure the value
 *         is an empty String (fail closed) — check string_empty before use.
 * @note The value is lowercase hex, matching http/service/session, so a token
 *       needs no URL escaping in a mail link. base64url would be shorter and is
 *       deliberately not used: the two token services would then disagree.
 * @note The expiry SATURATES at USIZE_MAX rather than wrapping — when the ttl
 *       would overflow, and when the system clock reads before the epoch. That
 *       value is the sentinel http_service_auth_token_expired_at() reads as
 *       ALREADY EXPIRED under every clock reading, so a broken clock refuses
 *       tokens instead of minting eternal ones.
 */
HTTP_Service_Auth_Token http_service_auth_token_create(HTTP_Service_Auth const *const self, USize const ttl);

/**
 * @brief Check whether a token's expiration timestamp has passed.
 * @param self Token instance.
 * @return true when the token has expired. The boundary is inclusive — a token
 *         whose expires_at equals the current second is EXPIRED, matching
 *         http_service_session_token_expired(). A clock reading before the epoch
 *         is clamped to USIZE_MAX, which is at or past every expiry: a broken
 *         clock expires everything rather than admitting everything.
 */
bool http_service_auth_token_expired(HTTP_Service_Auth_Token const *const self);

/**
 * @brief Check a token's expiration against a caller-supplied clock reading.
 * @param self Token instance.
 * @param now Current time in seconds.
 * @return true when now is at or past the token's expiration, and ALWAYS true
 *         for the saturation sentinel expires_at == USIZE_MAX — a token minted
 *         while the clock read before the epoch stays expired after the clock is
 *         repaired, rather than becoming eternal.
 */
bool http_service_auth_token_expired_at(HTTP_Service_Auth_Token const *const self, USize const now);

/**
 * @brief Hash a token value for storage.
 * @param self Service instance.
 * @param token Plain token value. Must be exactly
 *        ENCODING_HEX_SIZE(token_byte_count) bytes — the length this service
 *        mints. Any other length, the empty string included, is refused.
 * @return Lowercase SHA-256 hex of the token. Caller must uninitialize it.
 *         Empty on a hash failure and for a token of the wrong length (fail
 *         closed) — check string_empty before persisting.
 * @note The length gate is why a CSPRNG failure cannot become a live
 *       credential: http_service_auth_token_create() answers an empty value on
 *       that failure, and a caller who persists it unchecked would otherwise
 *       store the SHA-256 of the empty string — a row any empty candidate then
 *       verifies against. Hashing refuses it instead of writing it.
 * @note Store this, never the token value. A token is a bearer credential: a
 *       row read from a leaked backup or an over-broad SELECT is a live account
 *       takeover until the TTL expires.
 */
String http_service_auth_token_hash(HTTP_Service_Auth const *const self, char const *const token);

/**
 * @brief Release token value storage.
 * @param self Token instance.
 */
void http_service_auth_token_uninit(HTTP_Service_Auth_Token *const self);

/**
 * @brief Verify a candidate against a token still held in memory.
 * @param self Stored token to verify against. Its value is compared in constant
 *        time; its expires_at is checked first.
 * @param token Candidate token value.
 * @return true when token matches and has not expired.
 * @note MEMORY-ONLY. This compares plaintext against plaintext, so it is usable
 *       only while both sides live in this process — a one-shot confirmation
 *       inside a single request, or a test. Anything that reaches storage must
 *       use http_service_auth_token_hash() plus
 *       http_service_auth_token_verify_2(). Unlike every other function here,
 *       the first argument is the STORED TOKEN, not the service.
 */
bool http_service_auth_token_verify_1(HTTP_Service_Auth_Token const *const self, char const *const token);

/**
 * @brief Verify a candidate token against a stored hash and stored expiry.
 * @param self Service instance.
 * @param token Candidate token value.
 * @param hash Stored token hash, as produced by http_service_auth_token_hash().
 * @param expires_at Stored expiration timestamp in seconds.
 * @return true when the candidate hashes to the stored hash and the stored
 *         expiry has not passed. false for an empty hash, a hash failure, a
 *         candidate whose length is not ENCODING_HEX_SIZE(token_byte_count),
 *         or an expired record — including a stored expires_at of USIZE_MAX,
 *         which is both the saturation sentinel and what a NEGATIVE stored
 *         column becomes when a call site casts it to USize.
 * @note The comparison is constant time. Expiry is checked in the same call so
 *       a caller cannot verify the hash and forget the clock.
 * @note The length gate runs BEFORE any hashing, so an empty or otherwise
 *       wrong-length candidate is refused as a LENGTH, not by failing a
 *       comparison — that is what makes the refusal true of every stored hash,
 *       the SHA-256 of the empty string included. It also bounds the hash pass
 *       to a token-sized input rather than to the request body cap. The gate is
 *       EXACT: a token minted under a different token_byte_count no longer
 *       verifies, which one service instance per token kind already implies.
 */
bool http_service_auth_token_verify_2(HTTP_Service_Auth const *const self, char const *const token, String const *const hash, USize const expires_at);

/**
 * @brief Verify a candidate token against a stored hash held as a plain string.
 * @param self Service instance.
 * @param token Candidate token value.
 * @param hash Stored token hash, as produced by http_service_auth_token_hash().
 * @param expires_at Stored expiration timestamp in seconds.
 * @return Exactly what http_service_auth_token_verify_2() answers; an empty
 *         hash is the absent-record case and refuses, and so does a candidate
 *         whose length is not ENCODING_HEX_SIZE(token_byte_count) — refused
 *         before any hashing, the empty candidate included.
 * @note The char const* twin of http_service_auth_token_verify_2(), mirroring
 *       http_service_session_token_verify_1() versus _4(): a database cell
 *       arrives as a plain string and should not have to be wrapped in a String
 *       just to be compared.
 */
bool http_service_auth_token_verify_3(HTTP_Service_Auth const *const self, char const *const token, char const *const hash, USize const expires_at);

/**
 * @brief Verify a candidate against an in-memory token at a caller-supplied clock reading.
 * @param self Stored token to verify against.
 * @param token Candidate token value.
 * @param now Current time in seconds.
 * @return true when token matches and now is before the token's expiration.
 *         The deterministic form of http_service_auth_token_verify_1().
 */
bool http_service_auth_token_verify_at(HTTP_Service_Auth_Token const *const self, char const *const token, USize const now);

/**
 * @brief Release auth service storage.
 * @param self Service instance.
 * @note Releases the service-owned decoy hash and zeroes the configuration. An
 *       instance built from an arena releases nothing to the heap; the arena
 *       still owns the decoy's bytes.
 */
void http_service_auth_uninit(HTTP_Service_Auth *const self);

/**
 * @brief Verify a plain password against an encoded hash.
 * @param self Service instance.
 * @param password Plain password.
 * @param hash Encoded password hash.
 * @return true when password matches hash. false for an empty hash, a malformed
 *         record, and for a password past password_max_length, which is refused
 *         before crypto_password_verify_1 runs — run
 *         http_service_auth_password_reason() first when the caller must tell an
 *         over-ceiling password from a wrong one.
 * @note The cost comes from the STORED RECORD's iteration count, not from the
 *       service's configured one — self is validated but does not gate
 *       verification. That is what lets an old record still verify after
 *       password_hash_iterations is raised.
 * @note This returns false in microseconds for an empty hash, which is an
 *       account-enumeration oracle. Use http_service_auth_verify_password_2()
 *       on any path where the hash comes from a lookup that can miss.
 */
bool http_service_auth_verify_password_1(HTTP_Service_Auth const *const self, char const *const password, String const *const hash);

/**
 * @brief Verify a plain password against a stored hash that may be absent.
 * @param self Service instance.
 * @param password Plain password.
 * @param hash Encoded password hash, or an empty String when no account row was
 *        found. A non-empty hash must be NUL-terminated at its size: it is
 *        handed to crypto_password_verify_1() as a plain string, and String
 *        does not guarantee a terminator in general — only some writers add
 *        one. http_service_auth_hash_password() does; use
 *        http_service_auth_verify_password_3() when the record arrives as a
 *        database cell, which carries its own terminator.
 * @return true when password matches hash; false otherwise, including for an
 *         empty hash.
 * @note An empty hash runs the service's DECOY hash through the KDF, so a login
 *       attempt against an unknown account costs the same wall time as one
 *       against a known account with the wrong password. This is the function
 *       every login route should call.
 * @note That equality holds only for records AT THE CONFIGURED iteration count.
 *       The decoy runs at password_hash_iterations; a stored record runs at its
 *       own, so a record hashed under a lower count answers faster than the
 *       decoy — for a wrong password too — until http_service_auth_password_needs_rehash()
 *       drives its re-hash on the next successful login. Raise the count
 *       knowing every dormant account is distinguishable by latency until then.
 *       Padding the legacy path up to the decoy's cost would double every
 *       legacy login and still differ, so the bound is documented, not coded.
 * @note If the decoy hash itself failed to build (a CSPRNG or KDF failure at
 *       init), the service logs a WARN and this decoy_hash stays empty - an
 *       unknown-account lookup then returns in the microseconds an empty-hash
 *       refusal costs, not a KDF pass, so the timing guarantee above does NOT
 *       hold until the service is reinitialized.
 * @note The guarantee covers an ABSENT record, not a corrupt one. A NON-empty
 *       but malformed stored hash fails crypto_password_verify_1's parse in
 *       microseconds, so its latency still says "this row exists and is
 *       corrupt". That is a database-integrity problem, not an enumeration
 *       vector: the row's existence was already established by finding it.
 */
bool http_service_auth_verify_password_2(HTTP_Service_Auth const *const self, char const *const password, String const *const hash);

/**
 * @brief Verify a plain password against a stored hash held as a plain string.
 * @param self Service instance.
 * @param password Plain password.
 * @param hash Encoded password hash, or an EMPTY string ("") when no account
 *        row was found. Emptiness is the first byte, not the pointer: a column
 *        read hands back a live buffer either way.
 * @return Exactly what http_service_auth_verify_password_2() answers, decoy KDF
 *         pass and all — this is the same body over a char const* record.
 * @note The char const* twin of http_service_auth_verify_password_2(), mirroring
 *       http_service_auth_token_verify_3() versus _2(): a database cell arrives
 *       as a plain string and should not have to be wrapped in a String just to
 *       be verified. It also carries its own terminator, which the String form
 *       requires but cannot enforce.
 * @note Every note on http_service_auth_verify_password_2() applies unchanged,
 *       the legacy-record timing bound included.
 */
bool http_service_auth_verify_password_3(HTTP_Service_Auth const *const self, char const *const password, char const *const hash);

#endif // HTTP_SERVICE_AUTH_H