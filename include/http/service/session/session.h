/*
 * session.h - HTTP session service for the C Libraries Framework
 * @version 0.4.2
 *
 * Creates secure session tokens, hashes tokens for server-side storage, verifies
 * stored token hashes, and builds cookie header values.
 *
 * THE SERVICE HAS NO STORE. It is a codec: it turns a random token into a cookie
 * value and into the hash you keep, and turns a Cookie header back into that same
 * hash. Persistence, lookup, revocation, idle timeout, and the "is this account
 * allowed here" decision are all the application's - typically one row keyed by
 * the hash (token_hash, expires_at, revoked_at, last_seen_at), which is the shape
 * every consumer in this tree converged on independently.
 *
 * Session model (absolute, not sliding):
 *   - A token expires `ttl` seconds after it was minted and the cookie carries the
 *     same Max-Age, so an ACTIVE user is cut off at ttl. Nothing here re-issues a
 *     cookie on its own.
 *   - To slide the window, re-mint on a request that is close to expiry
 *     (http_service_session_mint again, replace the stored row, send the new
 *     Set-Cookie) or re-send the same token's cookie with
 *     http_service_session_cookie_create to refresh Max-Age only. Re-sending the
 *     cookie does NOT move the stored expires_at - move both or neither.
 *
 * Features:
 *   - Random session token generation (crypto/random's CSPRNG, lowercase hex).
 *   - SHA-256 token hashing for storage.
 *   - Constant-time hash comparison. Both verify tiers bottom out in one SIZED
 *     comparison: a char* side is measured with char_length, a String side by its
 *     own size, so a sized String over a longer terminated buffer never compares
 *     more bytes than it claims. csrf.h states the same property for its own tiers.
 *   - Secure cookie creation and clearing, off ONE HTTP_Cookie template validated
 *     and built at init - SameSite is parsed once, not per response.
 *   - Cookie header parsing, from a char* header, a Str, or a String - a null or
 *     empty header of any of the three reads as an absent cookie.
 *   - The whole read half of a request in ONE call, over a char* header
 *     (http_service_session_cookie_hash) or over the String
 *     http_server_request_header_get_4 hands back (_cookie_hash_4). Both answer
 *     the same hash for the same bytes; neither makes a String-holding route drop
 *     back to cookie_read + token_hash by hand.
 *   - Whole-object refusal at init: a misconfigured service never exists, so a
 *     silently unauthenticatable deployment is not reachable (see Error Handling).
 *
 * Usage Example:
 *   @code
 *   #include <http/server/http_server.h>
 *   #include <http/service/session/session.h>
 *
 *   static HTTP_Service_Session session;
 *
 *   // Startup, in a function returning bool.
 *   if (!http_service_session_init_1(&session)) {
 *       return false; // Misconfigured: fail startup, do not serve.
 *   }
 *
 *   // Login route, with `response` the HTTP_Server_Response and `cookie_header`
 *   // the Cookie request header (nullptr when the request carried none).
 *   // One call mints the token, its stored hash, and the cookie value.
 *   HTTP_Service_Session_Token token   = DEFAULT_INITIALIZATION;
 *   String                     hash    = DEFAULT_INITIALIZATION;
 *   String                     cookie  = DEFAULT_INITIALIZATION;
 *
 *   if (http_service_session_mint(&session, &token, &hash, &cookie)) {
 *       // Persist string_get_data(&hash) with token.expires_at, then:
 *       http_server_response_header_add(response, "Set-Cookie", string_get_data(&cookie));
 *   }
 *
 *   string_uninit(&cookie);
 *   string_uninit(&hash);
 *   http_service_session_token_uninit(&token);
 *
 *   // Any later request: the Cookie header straight to the hash you stored.
 *   String lookup = DEFAULT_INITIALIZATION;
 *
 *   if (http_service_session_cookie_hash(&session, cookie_header, &lookup)) {
 *       // SELECT ... WHERE token_hash = string_get_data(&lookup)
 *   }
 *
 *   string_uninit(&lookup);
 *
 *   // Logout route: delete the stored row, then expire the browser's cookie.
 *   String cleared = http_service_session_cookie_clear(&session);
 *
 *   if (!string_empty(&cleared)) {
 *       http_server_response_header_add(response, "Set-Cookie", string_get_data(&cleared));
 *   }
 *
 *   string_uninit(&cleared);
 *
 *   // Shutdown.
 *   http_service_session_uninit(&session);
 *   @endcode
 *
 * Error Handling:
 *   - Public functions validate non-null pointers; with ERROR_CHECK_ENABLED off
 *     those checks compile out and a null argument is undefined behavior. Request
 *     DATA (a Cookie header, a candidate token) is never error_checked - it is
 *     refused as a value.
 *   - log_init must have run before any call: a refusal logs at LOG_LEVEL_WARN,
 *     and an uninitialized log is itself a contract violation (hash.h ruling
 *     2026-09-05).
 *   - Every constructor is in-place and returns bool. It REFUSES WHOLE - leaving
 *     *self DEFAULT_INITIALIZATION - on: a refused arena; token_byte_count below
 *     HTTP_SERVICE_SESSION_TOKEN_BYTE_COUNT_MIN; ttl 0; a cookie_name that is not
 *     an RFC 6265 token (the empty name included); a `__Host-` name without Secure
 *     or with a path other than "/"; a `__Secure-` name without Secure;
 *     SameSite=None without Secure. Each of the last four is a cookie every
 *     browser silently DROPS: login would appear to succeed and nothing would
 *     authenticate, with no log anywhere. Fail server startup on false.
 *   - A same_site that is neither "Lax"/"None"/"Strict" (case-insensitive) nor
 *     empty logs a WARN and builds cookies with SameSite omitted; that is a
 *     degraded cookie, not a dropped one, so it is not a refusal.
 *   - http_service_session_token_create returns false on CSPRNG failure and
 *     leaves *out DEFAULT_INITIALIZATION - there is no half-object with a
 *     populated expires_at and an empty value.
 *   - http_service_session_token_hash returns the EMPTY String on hashing
 *     failure; http_service_session_cookie_create / _cookie_clear return the
 *     EMPTY String when the cookie module refuses the render (only reachable
 *     through a token value with bytes outside cookie-octet, or through the arena
 *     tier refusing the render itself, since the config was validated at init);
 *     http_service_session_cookie_read (and its _3/_4 tiers) returns an empty
 *     (but NUL-terminated, never nullptr-backed) String when the cookie is absent -
 *     and a NULL Cookie header is exactly the absent case, never an abort, since
 *     a request carrying no Cookie header at all is an ordinary unauthenticated
 *     request. Gate on string_empty and fail closed. It returns a DQUOTE-wrapped
 *     value WITH its quotes, as RFC 6265 section 4.1.1 allows, so `sid="<hex>"`
 *     hashes WITH the quotes and answers a lookup miss. Since every value this
 *     service mints is bare hex, a quoted one is always someone else's cookie or
 *     an attack, and failing closed on it is correct - the same contract csrf.h
 *     states for its own read.
 *   - http_service_session_token_hash("") answers the SHA-256 of the empty string,
 *     not the EMPTY String: it hashes the bytes it is given. No path inside this
 *     module reaches it that way - mint, cookie_hash and token_verify_1 all refuse
 *     an empty value first - so only a direct caller can store that digest. Gate
 *     your own token on string_empty before hashing it.
 *   - http_service_session_mint, _cookie_hash and _cookie_hash_4 answer false
 *     rather than hand back a partially built set; on false every out parameter is
 *     left EMPTY.
 *
 * Thread Safety:
 *   - An initialized HEAP service is READ-ONLY: every function but init/uninit takes
 *     `self` by const pointer and mutates nothing, so one shared instance is safe
 *     across libwebsockets service threads. Never mutate its fields after
 *     publishing it. init/alloc_init/uninit are NOT safe concurrently - construct
 *     before other threads start and destroy after they stop.
 *   - An ARENA-backed service (alloc_init_*) is the exception: EVERY function that
 *     hands back a String or a token borrows from its arena - cookie_clear,
 *     cookie_create, cookie_hash (and _4), cookie_read (and its _3/_4 tiers), mint,
 *     token_create and token_hash - and Arena is not thread-safe (see arena.h).
 *     Give each thread its own arena-backed service, or serialize every call on it
 *     and the string_uninit of its results. That list is exhaustive:
 *     http_service_session_token_verify_1 and _4 hand nothing back and digest to the
 *     STACK, so they allocate in no tier and a verify leaves an arena's used size
 *     unchanged.
 *
 * Memory Management:
 *   - Config strings are COPIED at init (string_init_static and
 *     string_alloc_init_static are owned copies despite the historical name), so
 *     the caller's cookie_name/cookie_path/same_site buffers may be released - or
 *     be stack buffers that go out of scope - immediately after the call.
 *   - The arena tier ties the service to `allocator`: the service, every String it
 *     returns, and the token values all live in that arena, so the arena must
 *     outlive them all. It is meant for a SCOPED arena - one per request, or one
 *     per login - not a process-lifetime one: a linear arena reclaims nothing, so
 *     a mint (token + hash + cookie value, three allocations) and every
 *     authenticated request (two - the cookie's read value and the digest
 *     cookie_hash hands back) accumulate for as long as the arena lives. A verify
 *     adds nothing to that: it digests to the stack.
 *     http_service_session_uninit clears the stored allocator, so re-initializing
 *     an uninitialized service through the arena tier means calling
 *     http_service_session_alloc_init_* again with the arena, never init_*.
 *   - AN ARENA THAT CANNOT SERVE A REQUEST ENDS THE PROCESS. The read half's
 *     allocation is sized by the CLIENT's cookie value: cookie_read and cookie_hash
 *     ask the cookie module for a copy of whatever the browser sent, and that
 *     reaches Arena.allocate, which ABORTS on exhaustion instead of degrading into
 *     a refusal (arena.h). So a scoped arena must be sized for the server's maximum
 *     Cookie header plus 64 bytes for the digest, and on ANY request path reachable
 *     from the internet the heap tier (http_service_session_init_*) is the choice -
 *     an undersized arena there is a remotely triggerable abort.
 *   - That is a known FRAMEWORK gap, not a decision of this module: arena.h's own
 *     rule is to reach for try_allocate whenever the requested size comes from
 *     outside the program, and String has no try tier to route the copy through
 *     yet. When one exists, these paths degrade into a refusal instead.
 *   - Returned String and token values must be uninitialized by the caller
 *     (http_service_session_token_uninit for tokens, string_uninit for the rest),
 *     including after a `false` answer.
 *
 * Performance Characteristics:
 *   - Per mint: one CSPRNG call writing straight into the token String, one
 *     SHA-256 over ~64 bytes, and three small Strings - the cookie renders off
 *     the stored template, so no per-response HTTP_Cookie is built at all.
 *     Per authenticated request: one linear Cookie-header scan, one SHA-256, and
 *     TWO allocations - the scanner's own String and the digest cookie_hash hands
 *     back. A request carrying no Cookie header at all costs neither.
 *   - Per verify: one SHA-256 and NO allocation on either tier - token_verify_1
 *     digests to a stack buffer and compares it against the stored hash in place.
 *   - Tokens are rendered as lowercase hex (64 chars at the default 32 bytes)
 *     rather than base64url (43 chars): the 21 extra bytes buy a value that is
 *     safe in a URL, a log line, and a DB key without any encoding question, and
 *     the cookie is nowhere near a size limit.
 *   - The stored hash is an UNKEYED SHA-256. That is correct here and only here:
 *     the input is 256 bits of CSPRNG output, so there is nothing to brute-force
 *     and no salt to add. NEVER hash a password this way - use crypto/password.
 *
 * Dependencies:
 *   - crypto/hash, crypto/random, datetime, encoding/hex, http/cookie, string (OpenSSL
 *     only transitively, via the crypto headers).
 *
 * See session.c for implementation details.
 */

#ifndef HTTP_SERVICE_SESSION_H
#define HTTP_SERVICE_SESSION_H

#include <container/string/string.h>
#include <crypto/hash/hash.h>
#include <crypto/random/random.h>
#include <datetime/datetime.h>
#include <http/cookie/cookie.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

#define HTTP_SERVICE_SESSION_DEFAULT_COOKIE_NAME "__Host-session"
#define HTTP_SERVICE_SESSION_DEFAULT_COOKIE_PATH "/"
#define HTTP_SERVICE_SESSION_DEFAULT_HTTP_ONLY true
#define HTTP_SERVICE_SESSION_DEFAULT_SAME_SITE "Strict"
#define HTTP_SERVICE_SESSION_DEFAULT_SECURE true
#define HTTP_SERVICE_SESSION_DEFAULT_TOKEN_BYTE_COUNT 32
#define HTTP_SERVICE_SESSION_DEFAULT_TTL 86400
#define HTTP_SERVICE_SESSION_TOKEN_BYTE_COUNT_MIN 16

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief Session token and cookie configuration.
 *
 * Built and validated once by a constructor; read-only afterwards. Treat every
 * field as private - http_service_session_cookie_name_get is the only supported
 * read.
 */
typedef struct {
    /** @brief Cookie template: the configured name, path, SameSite, Secure and HttpOnly,
     *         validated at init. Its own value is always empty - cookie_create builds a
     *         cookie carrying the token, and cookie_clear ignores a value entirely. Its
     *         `allocator` is the arena tier's arena, or nullptr on the heap tier. */
    HTTP_Cookie cookie;
    /** @brief Random bytes generated per token before encoding. At least
     *         HTTP_SERVICE_SESSION_TOKEN_BYTE_COUNT_MIN. */
    USize token_byte_count;
    /** @brief Session token time-to-live in seconds; never 0 (a constructor refuses it,
     *         because a cookie with Max-Age=0 expires on arrival). */
    USize ttl;
} HTTP_Service_Session;

/**
 * @brief Session token value with expiration timestamp.
 */
typedef struct {
    /** @brief Expiration timestamp in naive-UTC epoch seconds, from datetime_now() + ttl,
     *         clamped at USIZE_MAX rather than wrapped. This is the SERVER's clock; a
     *         consumer that stores it beside a database `now()` is comparing two clocks. */
    USize expires_at;
    /** @brief Encoded random token value: lowercase hex, 2 * token_byte_count characters. */
    String value;
} HTTP_Service_Session_Token;

/*==============================================================================
 * MARK: - API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Initialize an arena-backed session service with default values, in place.
 * @param self Destination service; left DEFAULT_INITIALIZATION on false.
 * @param allocator Arena allocator; must outlive the service and everything it returns.
 * @return true on success; false on a refused arena (see Error Handling). Fail startup.
 */
bool http_service_session_alloc_init_1(HTTP_Service_Session *const self, Arena *const allocator);

/**
 * @brief Initialize an arena-backed session service with explicit values, in place.
 * @param self Destination service; left DEFAULT_INITIALIZATION on false.
 * @param token_byte_count Random bytes per token; at least HTTP_SERVICE_SESSION_TOKEN_BYTE_COUNT_MIN.
 * @param ttl Session token time-to-live in seconds; must be non-zero.
 * @param cookie_name Cookie name; must be an RFC 6265 token, and a `__Host-`/`__Secure-` prefix binds the flags below.
 * @param cookie_path Cookie path scope; must be "/" when cookie_name carries the `__Host-` prefix.
 * @param same_site SameSite policy value: "Lax", "None", or "Strict" (case-insensitive); anything else logs a WARN and omits SameSite from built cookies. "None" requires secure.
 * @param secure Secure cookie flag.
 * @param http_only HttpOnly cookie flag.
 * @param allocator Arena allocator; must outlive the service and everything it returns.
 * @return true on success; false on any refusal listed in Error Handling. Fail startup.
 */
bool http_service_session_alloc_init_2(HTTP_Service_Session *const self,
    USize const token_byte_count, USize const ttl, char const *const cookie_name, char const *const cookie_path, char const *const same_site,
    bool const secure, bool const http_only, Arena *const allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Build a cookie header value that clears the configured session cookie.
 * @param self Service instance.
 * @return Cookie header value for http_server_response_header_add("Set-Cookie", ...),
 *         or EMPTY on a refused render. Caller must uninitialize it.
 */
String http_service_session_cookie_clear(HTTP_Service_Session const *const self);

/**
 * @brief Build a cookie header value carrying a session token.
 * @param self Service instance.
 * @param token Session token value; refused (EMPTY + WARN) when empty, so an ignored
 *        token_create failure cannot set a real cookie with no value in it.
 * @return Cookie header value for http_server_response_header_add("Set-Cookie", ...),
 *         or EMPTY on refusal. Caller must uninitialize it.
 */
String http_service_session_cookie_create(HTTP_Service_Session const *const self, char const *const token);

/**
 * @brief Read the session cookie out of a Cookie header and hash it for lookup - the
 *        whole read half of a request in one call.
 * @param self Service instance.
 * @param cookie_header Cookie request header value.
 * @param out Destination hash; left EMPTY on false. Caller must uninitialize it either way.
 * @return true when the cookie was present and hashed; false when it is absent, empty,
 *         or hashing failed. On false the request is unauthenticated - fail closed.
 */
bool http_service_session_cookie_hash(HTTP_Service_Session const *const self, char const *const cookie_header, String *const out);

/**
 * @brief Read the session cookie out of a Cookie header held as a String and hash it for
 *        lookup - the tier http_server_request_header_get_4 hands back.
 *
 * The String twin of http_service_session_cookie_hash: same scan, same digest, same answer
 * for the same bytes, so a route holding a String never has to fall back to
 * http_service_session_cookie_read_4 followed by http_service_session_token_hash.
 * @param self Service instance.
 * @param cookie_header Cookie request header value; null or empty reads as absent, and the
 *        bytes need not be NUL-terminated.
 * @param out Destination hash; left EMPTY on false. Caller must uninitialize it either way.
 * @return true when the cookie was present and hashed; false when it is absent, empty,
 *         or hashing failed. On false the request is unauthenticated - fail closed.
 */
bool http_service_session_cookie_hash_4(HTTP_Service_Session const *const self, String const *const cookie_header, String *const out);

/**
 * @brief Read the configured cookie name.
 * @param self Service instance.
 * @return The cookie name as a NUL-terminated string owned by the service, read-only
 *         despite the non-const return type; never nullptr and never empty on an
 *         initialized service. Valid only until http_service_session_uninit.
 */
char* http_service_session_cookie_name_get(HTTP_Service_Session const *const self);

/**
 * @brief Read the configured session token from a Cookie header value.
 * @param self Service instance.
 * @param cookie_header Cookie request header value; null or empty reads as absent. This tier
 *        measures with char_length, so unlike the _3/_4 tiers these bytes MUST be
 *        NUL-terminated.
 * @return Session token value when found, otherwise an empty String whose data is a
 *         NUL-terminated "" (never nullptr). Caller must uninitialize it.
 */
String http_service_session_cookie_read(HTTP_Service_Session const *const self, char const *const cookie_header);

/**
 * @brief Read the configured session token from a Cookie header held as a Str.
 *
 * The Str story stops here: there is no _3 tier above it (no cookie_hash_3), because http_server
 * has no Str producer to feed one - header_copy hands back a char* and header_get_4 a String.
 * @param self Service instance.
 * @param cookie_header Cookie request header value; null or empty reads as absent, and the
 *        bytes need not be NUL-terminated.
 * @return Session token value when found, otherwise an empty String whose data is a
 *         NUL-terminated "" (never nullptr). Caller must uninitialize it.
 */
String http_service_session_cookie_read_3(HTTP_Service_Session const *const self, Str const *const cookie_header);

/**
 * @brief Read the configured session token from a Cookie header held as a String - the tier
 *        http_server_request_header_get_4 hands back.
 * @param self Service instance.
 * @param cookie_header Cookie request header value; null or empty reads as absent.
 * @return Session token value when found, otherwise an empty String whose data is a
 *         NUL-terminated "" (never nullptr). Caller must uninitialize it.
 */
String http_service_session_cookie_read_4(HTTP_Service_Session const *const self, String const *const cookie_header);

/**
 * @brief Initialize a session service with default values, in place.
 * @param self Destination service; left DEFAULT_INITIALIZATION on false.
 * @return true on success; false if the defaults could not be honored. Fail startup.
 */
bool http_service_session_init_1(HTTP_Service_Session *const self);

/**
 * @brief Initialize a session service with explicit values, in place.
 * @param self Destination service; left DEFAULT_INITIALIZATION on false.
 * @param token_byte_count Random bytes per token; at least HTTP_SERVICE_SESSION_TOKEN_BYTE_COUNT_MIN.
 * @param ttl Session token time-to-live in seconds; must be non-zero.
 * @param cookie_name Cookie name; must be an RFC 6265 token, and a `__Host-`/`__Secure-` prefix binds the flags below.
 * @param cookie_path Cookie path scope; must be "/" when cookie_name carries the `__Host-` prefix.
 * @param same_site SameSite policy value: "Lax", "None", or "Strict" (case-insensitive); anything else logs a WARN and omits SameSite from built cookies. "None" requires secure.
 * @param secure Secure cookie flag.
 * @param http_only HttpOnly cookie flag.
 * @return true on success; false on any refusal listed in Error Handling. Fail startup.
 */
bool http_service_session_init_2(HTTP_Service_Session *const self,
    USize const token_byte_count, USize const ttl, char const *const cookie_name, char const *const cookie_path, char const *const same_site,
    bool const secure, bool const http_only);

/**
 * @brief Mint a login: a fresh token, the hash to store, and the cookie to send.
 *
 * All-or-nothing. On false nothing usable was produced and no cookie must be sent.
 * @param self Service instance.
 * @param token Destination token; uninitialize with http_service_session_token_uninit.
 * @param hash Destination storage hash; uninitialize with string_uninit.
 * @param cookie Destination cookie header value; uninitialize with string_uninit.
 * @return true when all three were built; false when the CSPRNG, the hash, or the cookie
 *         render failed, in which case all three outputs are EMPTY.
 */
bool http_service_session_mint(HTTP_Service_Session const *const self, HTTP_Service_Session_Token *const token, String *const hash, String *const cookie);

/**
 * @brief Create a random session token using service defaults, in place.
 * @param self Service instance.
 * @param out Destination token; left DEFAULT_INITIALIZATION on false. Uninitialize with
 *        http_service_session_token_uninit.
 * @return true on success; false on CSPRNG failure. Never send a cookie on false.
 */
bool http_service_session_token_create(HTTP_Service_Session const *const self, HTTP_Service_Session_Token *const out);

/**
 * @brief Return true when token expiration timestamp has passed.
 * @param self Token instance.
 * @return true when expired.
 */
bool http_service_session_token_expired(HTTP_Service_Session_Token const *const self);

/**
 * @brief Hash a token for storage.
 * @param self Service instance.
 * @param token Session token value.
 * @return Encoded hash; EMPTY String on hashing failure - gate on
 *         string_empty before use (fail closed). Caller must uninitialize it.
 */
String http_service_session_token_hash(HTTP_Service_Session const *const self, char const *const token);

/**
 * @brief Release token value storage.
 * @param self Token instance.
 */
void http_service_session_token_uninit(HTTP_Service_Session_Token *const self);

/**
 * @brief Verify a candidate token against a stored hash held as a C string - the tier a
 *        database cell arrives in.
 * @param self Service instance.
 * @param token Candidate token value; a C string, measured with char_length, so it must be
 *        NUL-terminated.
 * @param hash Stored token hash; also a C string on this tier and also measured with
 *        char_length.
 * @return true when the token hashes to hash; false on an empty hash, an empty token, or
 *         a hashing failure.
 */
bool http_service_session_token_verify_1(HTTP_Service_Session const *const self, char const *const token, char const *const hash);

/**
 * @brief Verify a candidate token against a stored hash held as a String.
 * @param self Service instance.
 * @param token Candidate token value. This side is a C string and IS measured with char_length,
 *        so it must be terminated.
 * @param hash Stored token hash; null or empty answers false. Read through the String's own
 *        size, so those bytes need not be terminated - this tier does NOT hand the hash on as a
 *        C string, so a String bounded short of a longer buffer compares only what it claims.
 * @return true when the token hashes to hash; false on an empty hash, an empty token, or
 *         a hashing failure.
 */
bool http_service_session_token_verify_4(HTTP_Service_Session const *const self, char const *const token, String const *const hash);

/**
 * @brief Release session service storage.
 * @param self Service instance.
 */
void http_service_session_uninit(HTTP_Service_Session *const self);

#endif // HTTP_SERVICE_SESSION_H