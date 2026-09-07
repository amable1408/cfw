/*
 * csrf.h - HTTP CSRF service for the C Libraries Framework
 * @version 0.6.0
 *
 * Creates and verifies CSRF tokens for cookie-backed browser sessions, in both of
 * the two patterns that actually ship:
 *
 *   - DOUBLE SUBMIT (http_service_csrf_request_allowed_2 /
 *     http_service_csrf_token_verify_2). The token lives in a NON-HttpOnly cookie
 *     the browser sends automatically and in a request header the page's own
 *     JavaScript sets from it. The server compares the two and keeps NO state -
 *     an attacker's cross-site form can make the browser send the cookie but
 *     cannot read it to set the header. This is why
 *     HTTP_SERVICE_CSRF_DEFAULT_HTTP_ONLY is false, unlike the session cookie's
 *     true: JS must be able to read this one. The `__Host-` cookie prefix
 *     (default) is what stops a sibling subdomain tossing a cookie of its own
 *     into the pair.
 *   - SYNCHRONIZER TOKEN (http_service_csrf_request_allowed_1 /
 *     http_service_csrf_token_verify_1). The token is held in the SERVER's session
 *     record, with an expiry, and compared against the request header. Use this
 *     when you already have server-side session storage.
 *
 * The pair is a hierarchy, not a duplicate: request_allowed_1/_2 is the route
 * gate - it lets a safe method through untouched and verifies everything else;
 * token_verify_1/_2 is the bare comparison underneath it. Every tier carries its
 * number, so a reader never has to guess which one a bare name meant.
 *
 * Token policy: ONE token per session. Issue it at login and re-issue it when
 * http_service_csrf_token_expired says so. Do NOT mint one per request - it
 * breaks multiple tabs and the back button, and buys nothing against a 256-bit
 * random token.
 *
 * Features:
 *   - Random CSRF token generation (crypto/random's CSPRNG, lowercase hex).
 *   - Token expiration checks.
 *   - Constant-time token comparison (char_compare_equal_comptime_2). The timing
 *     property is a property of that primitive; nothing in a unit test can prove
 *     it, so it is stated here rather than pinned. Every tier bottoms out in one
 *     SIZED comparison: a char* side is measured with char_length, a String side
 *     by its own size, so a sized String over a longer terminated buffer never
 *     compares more bytes than it claims.
 *   - Protected-method detection, compared EXACTLY: RFC 9110 section 9.1 method names
 *     are case-sensitive, so "get" is an unknown method, not a safe one.
 *   - Cookie creation, clearing, and parsing off ONE HTTP_Cookie template
 *     validated and built at init; the Cookie header is read from a char*, a Str,
 *     or a String, and a null or empty header of any of the three reads as an
 *     absent cookie.
 *   - The whole double-submit gate in ONE call over either tier:
 *     http_service_csrf_request_allowed_2 takes char* sides,
 *     http_service_csrf_request_allowed_4 takes the Strings
 *     http_server_request_header_get_4 / _custom_header_get_4 hand back. Both
 *     answer the same verdict for the same bytes.
 *   - Whole-object refusal at init (see Error Handling).
 *
 * Usage Example:
 *   @code
 *   #include <http/server/http_server.h>
 *   #include <http/service/csrf/csrf.h>
 *
 *   static HTTP_Service_CSRF _csrf;
 *
 *   // Startup, in a function returning bool.
 *   if (!http_service_csrf_init_1(&_csrf)) {
 *       return false; // Misconfigured: fail startup, do not serve.
 *   }
 *
 *   // Request 1 - GET the page, in a route handler returning void: mint a token
 *   // and hand it to the browser in the cookie its own JavaScript will read back.
 *   // One all-or-nothing call, the twin of http_service_session_mint.
 *   HTTP_Service_CSRF_Token token  = DEFAULT_INITIALIZATION;
 *   String                  cookie = DEFAULT_INITIALIZATION;
 *
 *   if (http_service_csrf_mint(&_csrf, &token, &cookie)) {
 *       http_server_response_header_add(response, "Set-Cookie", string_get_data(&cookie));
 *   }
 *
 *   string_uninit(&cookie);
 *   http_service_csrf_token_uninit(&token);
 *
 *   // Request 2 - the POST, in a route handler returning void: the cookie comes
 *   // back on its own, the header only if
 *   // the page's script set it. Cookie is an lws-RECOGNIZED header, so it is read
 *   // with header_copy; the CSRF header is custom and is NOT, so it needs
 *   // custom_header_copy - and its name is the one this service was configured
 *   // with, http_service_csrf_header_name_get.
 *   char cookie_header[1024]    = DEFAULT_INITIALIZATION;
 *   char header_token[256]      = DEFAULT_INITIALIZATION;
 *
 *   http_server_request_header_copy(request, HTTP_SERVER_HEADER_COOKIE, cookie_header, sizeof(cookie_header));
 *   http_server_request_custom_header_copy(request, http_service_csrf_header_name_get(&_csrf), header_token, sizeof(header_token));
 *
 *   if (!http_service_csrf_request_allowed_2(&_csrf, http_server_request_get_method_1(request), header_token, cookie_header)) {
 *       http_server_response_send_1(response, "forbidden", HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_FORBIDDEN);
 *
 *       return;
 *   }
 *
 *   // ... handle the request ...
 *
 *   // Shutdown.
 *   http_service_csrf_uninit(&_csrf);
 *   @endcode
 *
 * Error Handling:
 *   - Public functions validate non-null pointers on the SERVICE and its
 *     configuration; with ERROR_CHECK_ENABLED off those checks compile out and a
 *     null argument there is undefined behavior. REQUEST DATA is never
 *     error_checked: a null or empty request token, a null or empty cookie
 *     header, and a token of the wrong length all answer false. A missing
 *     X-CSRF-Token header is the ordinary shape of an attack, not a contract
 *     violation, and must never abort the server.
 *   - log_init must have run before any call (hash.h ruling 2026-09-05).
 *   - Every constructor is in-place and returns bool. It REFUSES WHOLE - leaving
 *     *self DEFAULT_INITIALIZATION - on: a refused arena; token_byte_count below
 *     HTTP_SERVICE_CSRF_TOKEN_BYTE_COUNT_MIN; ttl 0; an empty header_name; a
 *     cookie_name that is not an RFC 6265 token (the empty name included); a
 *     `__Host-` name without Secure or with a path other than "/"; a `__Secure-`
 *     name without Secure; SameSite=None without Secure. The prefix cases matter
 *     most in DEVELOPMENT, where a "turn Secure off for localhost" toggle
 *     silently deletes the whole protection: every browser drops a `__Host-`
 *     cookie sent without Secure, so the CSRF check would then compare a header
 *     against a cookie that never arrives. Fail server startup on false.
 *   - A same_site that is neither "Lax"/"None"/"Strict" (case-insensitive) nor
 *     empty logs a WARN and builds cookies with SameSite omitted.
 *   - http_service_csrf_token_create returns false on CSPRNG failure and leaves
 *     *out DEFAULT_INITIALIZATION; http_service_csrf_cookie_create refuses an
 *     EMPTY token rather than emit a cookie with no value in it, and answers the
 *     EMPTY String on any refused render - a token with bytes outside
 *     cookie-octet, or the arena tier refusing the render itself, since the
 *     config was validated at init.
 *   - http_service_csrf_cookie_read (and its _3/_4 tiers) returns an empty (but
 *     NUL-terminated, never nullptr-backed) String when the cookie is absent -
 *     and a NULL Cookie header is exactly the absent case, never an abort, since
 *     a request carrying no Cookie header at all is the ordinary shape of a
 *     cross-site attempt. It returns a
 *     DQUOTE-wrapped value WITH its quotes, as RFC 6265 section 4.1.1 allows - a quoted
 *     cookie can therefore never match an unquoted header token. Since this
 *     module's own tokens are hex, a quoted value is always someone else's cookie
 *     or an attack, and failing closed on it is correct.
 *
 * Thread Safety:
 *   - An initialized HEAP service is READ-ONLY: every function but init/uninit takes
 *     `self` by const pointer and mutates nothing, so one shared instance is safe
 *     across libwebsockets service threads. Never mutate its fields after
 *     publishing it. init/alloc_init/uninit are NOT safe concurrently.
 *   - An ARENA-backed service (alloc_init_*) is the exception: EVERY function that
 *     hands back a String or a token borrows from its arena - cookie_clear,
 *     cookie_create, cookie_read (and its _3/_4 tiers), mint, request_allowed_2
 *     and _4 through that read, and token_create - and Arena is not thread-safe
 *     (see arena.h). Give each thread its own arena-backed service, or serialize
 *     every call on it and the string_uninit of its results. That list is
 *     exhaustive: http_service_csrf_method_protected, _request_allowed_1,
 *     _token_expired and _token_verify_1/_2/_4 hand nothing back and compare in
 *     place, so they allocate in no tier and a verify leaves an arena's used size
 *     unchanged.
 *
 * Memory Management:
 *   - Config strings are COPIED at init (string_init_static and
 *     string_alloc_init_static are owned copies despite the historical name), so
 *     the caller's cookie_name/cookie_path/header_name/same_site buffers may be
 *     released - or be stack buffers that go out of scope - immediately after the
 *     call.
 *   - The arena tier ties the service to `allocator`: the service, every String
 *     it returns, and the token values all live in that arena, so the arena must
 *     outlive them all. It is meant for a SCOPED arena - one per request, or one
 *     per issued token - not a process-lifetime one: a linear arena reclaims
 *     nothing, so every token (one allocation), every cookie render (one), and
 *     every protected request (one) accumulate for as long as the arena lives.
 *     http_service_csrf_uninit clears the stored allocator, so re-initializing
 *     through the arena tier means calling http_service_csrf_alloc_init_* again,
 *     never init_*.
 *   - AN ARENA THAT CANNOT SERVE A REQUEST ENDS THE PROCESS. The protected
 *     request's allocation is sized by the CLIENT's cookie value: cookie_read - and
 *     request_allowed_2/_4 through it - ask the cookie module for a copy of
 *     whatever the browser sent, and that reaches Arena.allocate, which ABORTS on
 *     exhaustion instead of degrading into a refusal (arena.h). So a scoped arena
 *     must be sized for the server's maximum Cookie header plus 64 bytes for the
 *     token, and on ANY request path reachable from the internet the heap tier
 *     (http_service_csrf_init_*) is the choice - an undersized arena there is a
 *     remotely triggerable abort.
 *   - That is a known FRAMEWORK gap, not a decision of this module: arena.h's own
 *     rule is to reach for try_allocate whenever the requested size comes from
 *     outside the program, and String has no try tier to route the copy through
 *     yet. When one exists, these paths degrade into a refusal instead.
 *   - Returned String and token values must be uninitialized by the caller
 *     (http_service_csrf_token_uninit for tokens, string_uninit for the rest),
 *     including after a `false` answer.
 *
 * Performance Characteristics:
 *   - Per token: one CSPRNG call writing straight into one small String, and a
 *     cookie rendered off the stored template - no per-response HTTP_Cookie is
 *     built at all.
 *   - Per request: one allocation, one linear Cookie-header scan, and one
 *     constant-time compare over the token (64 bytes at the default size), plus one
 *     char_length ON THE char* TIER ONLY - request_allowed_4 measures neither side,
 *     since both arrive as Strings that carry their own size. A protected request
 *     with no token header at all, or with no Cookie header at all, is refused
 *     before any of that. Nothing here is worth measuring.
 *
 * Dependencies:
 *   - crypto/random, datetime, http/cookie, string.
 *
 * See csrf.c for implementation details.
 */

#ifndef HTTP_SERVICE_CSRF_H
#define HTTP_SERVICE_CSRF_H

#include <container/string/string.h>
#include <crypto/random/random.h>
#include <datetime/datetime.h>
#include <http/cookie/cookie.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

#define HTTP_SERVICE_CSRF_DEFAULT_COOKIE_NAME "__Host-csrf"
#define HTTP_SERVICE_CSRF_DEFAULT_COOKIE_PATH "/"
#define HTTP_SERVICE_CSRF_DEFAULT_HEADER_NAME "X-CSRF-Token"
#define HTTP_SERVICE_CSRF_DEFAULT_HTTP_ONLY false
#define HTTP_SERVICE_CSRF_DEFAULT_SAME_SITE "Strict"
#define HTTP_SERVICE_CSRF_DEFAULT_SECURE true
#define HTTP_SERVICE_CSRF_DEFAULT_TOKEN_BYTE_COUNT 32
#define HTTP_SERVICE_CSRF_DEFAULT_TTL 3600
#define HTTP_SERVICE_CSRF_TOKEN_BYTE_COUNT_MIN 16

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief CSRF token and cookie configuration.
 *
 * Built and validated once by a constructor; read-only afterwards. Treat every
 * field as private - http_service_csrf_cookie_name_get and
 * http_service_csrf_header_name_get are the only supported reads.
 */
typedef struct {
    /** @brief Cookie template: the configured name, path, SameSite, Secure and HttpOnly,
     *         validated at init. Its own value is always empty. Its `allocator` is the
     *         arena tier's arena, or nullptr on the heap tier. */
    HTTP_Cookie cookie;
    /** @brief Request header name the browser echoes the token in. Never empty on an
     *         initialized service; read it with http_service_csrf_header_name_get so the
     *         route lookup and the CORS whitelist cannot drift apart. */
    String header_name;
    /** @brief Random bytes generated per token before encoding. At least
     *         HTTP_SERVICE_CSRF_TOKEN_BYTE_COUNT_MIN. */
    USize token_byte_count;
    /** @brief CSRF token time-to-live in seconds; never 0 (a constructor refuses it,
     *         because a cookie with Max-Age=0 expires on arrival). */
    USize ttl;
} HTTP_Service_CSRF;

/**
 * @brief CSRF token value with expiration timestamp.
 */
typedef struct {
    /** @brief Expiration timestamp in naive-UTC epoch seconds, from datetime_now() + ttl,
     *         clamped at USIZE_MAX rather than wrapped. */
    USize expires_at;
    /** @brief Encoded random token value: lowercase hex, 2 * token_byte_count characters. */
    String value;
} HTTP_Service_CSRF_Token;

/*==============================================================================
 * MARK: - API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Initialize an arena-backed CSRF service with default values, in place.
 * @param self Destination service; left DEFAULT_INITIALIZATION on false.
 * @param allocator Arena allocator; must outlive the service and everything it returns.
 * @return true on success; false on a refused arena (see Error Handling). Fail startup.
 */
bool http_service_csrf_alloc_init_1(HTTP_Service_CSRF *const self, Arena *const allocator);

/**
 * @brief Initialize an arena-backed CSRF service with explicit values, in place.
 * @param self Destination service; left DEFAULT_INITIALIZATION on false.
 * @param token_byte_count Random bytes per token; at least HTTP_SERVICE_CSRF_TOKEN_BYTE_COUNT_MIN.
 * @param ttl CSRF token time-to-live in seconds; must be non-zero.
 * @param cookie_name Cookie name; must be an RFC 6265 token, and a `__Host-`/`__Secure-` prefix binds the flags below.
 * @param cookie_path Cookie path scope; must be "/" when cookie_name carries the `__Host-` prefix.
 * @param header_name Request header name; must be non-empty.
 * @param same_site SameSite policy value: "Lax", "None", or "Strict" (case-insensitive); anything else logs a WARN and omits SameSite from built cookies. "None" requires secure.
 * @param secure Secure cookie flag.
 * @param http_only HttpOnly cookie flag; leave it false for double submit, or the page's script cannot echo the token.
 * @param allocator Arena allocator; must outlive the service and everything it returns.
 * @return true on success; false on any refusal listed in Error Handling. Fail startup.
 */
bool http_service_csrf_alloc_init_2(HTTP_Service_CSRF *const self,
    USize const token_byte_count, USize const ttl, char const *const cookie_name, char const *const cookie_path, char const *const header_name,
    char const *const same_site, bool const secure, bool const http_only, Arena *const allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Build a cookie header value that clears the configured CSRF cookie.
 * @param self Service instance.
 * @return Cookie header value for http_server_response_header_add("Set-Cookie", ...),
 *         or EMPTY on a refused render. Caller must uninitialize it.
 */
String http_service_csrf_cookie_clear(HTTP_Service_CSRF const *const self);

/**
 * @brief Build a cookie header value carrying a CSRF token.
 * @param self Service instance.
 * @param token CSRF token value; refused (EMPTY + WARN) when empty, so an ignored
 *        token_create failure cannot set a real cookie with no token in it.
 * @return Cookie header value for http_server_response_header_add("Set-Cookie", ...),
 *         or EMPTY on refusal. Caller must uninitialize it.
 */
String http_service_csrf_cookie_create(HTTP_Service_CSRF const *const self, char const *const token);

/**
 * @brief Read the configured cookie name.
 * @param self Service instance.
 * @return The cookie name as a NUL-terminated string owned by the service, read-only
 *         despite the non-const return type; never nullptr and never empty on an
 *         initialized service. Valid only until http_service_csrf_uninit.
 */
char* http_service_csrf_cookie_name_get(HTTP_Service_CSRF const *const self);

/**
 * @brief Read the configured CSRF token from a Cookie header value.
 * @param self Service instance.
 * @param cookie_header Cookie request header value; null or empty reads as absent. This tier
 *        measures with char_length, so unlike the _3/_4 tiers these bytes MUST be
 *        NUL-terminated.
 * @return CSRF token value when found, otherwise an empty String whose data is a
 *         NUL-terminated "" (never nullptr). Caller must uninitialize it.
 */
String http_service_csrf_cookie_read(HTTP_Service_CSRF const *const self, char const *const cookie_header);

/**
 * @brief Read the configured CSRF token from a Cookie header held as a Str - the twin of
 *        http_service_session_cookie_read_3, so both services read the same three tiers.
 *
 * The Str story stops here: there is no _3 tier above it (no request_allowed_3), because
 * http_server has no Str producer to feed one - header_copy hands back a char* and header_get_4
 * a String.
 * @param self Service instance.
 * @param cookie_header Cookie request header value; null or empty reads as absent, and the
 *        bytes need not be NUL-terminated.
 * @return CSRF token value when found, otherwise an empty String whose data is a
 *         NUL-terminated "" (never nullptr). Caller must uninitialize it.
 */
String http_service_csrf_cookie_read_3(HTTP_Service_CSRF const *const self, Str const *const cookie_header);

/**
 * @brief Read the configured CSRF token from a Cookie header held as a String - the tier
 *        http_server_request_header_get_4 hands back.
 * @param self Service instance.
 * @param cookie_header Cookie request header value; null or empty reads as absent.
 * @return CSRF token value when found, otherwise an empty String whose data is a
 *         NUL-terminated "" (never nullptr). Caller must uninitialize it.
 */
String http_service_csrf_cookie_read_4(HTTP_Service_CSRF const *const self, String const *const cookie_header);

/**
 * @brief Read the configured request header name.
 *
 * Use it for the http_server_request_custom_header_copy lookup and for the CORS
 * Access-Control-Allow-Headers list, so a configured name cannot drift away from a
 * hard-coded one.
 * @param self Service instance.
 * @return The header name as a NUL-terminated string owned by the service, read-only
 *         despite the non-const return type; never nullptr and never empty on an
 *         initialized service. Valid only until http_service_csrf_uninit.
 */
char* http_service_csrf_header_name_get(HTTP_Service_CSRF const *const self);

/**
 * @brief Initialize a CSRF service with default values, in place.
 * @param self Destination service; left DEFAULT_INITIALIZATION on false.
 * @return true on success; false if the defaults could not be honored. Fail startup.
 */
bool http_service_csrf_init_1(HTTP_Service_CSRF *const self);

/**
 * @brief Initialize a CSRF service with explicit values, in place.
 * @param self Destination service; left DEFAULT_INITIALIZATION on false.
 * @param token_byte_count Random bytes per token; at least HTTP_SERVICE_CSRF_TOKEN_BYTE_COUNT_MIN.
 * @param ttl CSRF token time-to-live in seconds; must be non-zero.
 * @param cookie_name Cookie name; must be an RFC 6265 token, and a `__Host-`/`__Secure-` prefix binds the flags below.
 * @param cookie_path Cookie path scope; must be "/" when cookie_name carries the `__Host-` prefix.
 * @param header_name Request header name; must be non-empty.
 * @param same_site SameSite policy value: "Lax", "None", or "Strict" (case-insensitive); anything else logs a WARN and omits SameSite from built cookies. "None" requires secure.
 * @param secure Secure cookie flag.
 * @param http_only HttpOnly cookie flag; leave it false for double submit, or the page's script cannot echo the token.
 * @return true on success; false on any refusal listed in Error Handling. Fail startup.
 */
bool http_service_csrf_init_2(HTTP_Service_CSRF *const self,
    USize const token_byte_count, USize const ttl, char const *const cookie_name, char const *const cookie_path, char const *const header_name,
    char const *const same_site, bool const secure, bool const http_only);

/**
 * @brief Return true when method needs CSRF validation.
 * @param method HTTP method; compared EXACTLY (RFC 9110 section 9.1 method names are
 *        case-sensitive), so "get" is protected and "GET" is not. A null or empty method
 *        is protected too - an unreadable method is not a safe one.
 * @return true for anything that is not GET, HEAD, OPTIONS, or TRACE.
 */
bool http_service_csrf_method_protected(char const *const method);

/**
 * @brief Mint a token and the cookie that carries it, in one call - the twin of
 *        http_service_session_mint, minus the stored hash a CSRF token does not have.
 *
 * All-or-nothing. On false nothing usable was produced and no cookie must be sent: a page given
 * a token but no cookie (or a cookie the render refused) would fail every later POST on a
 * mismatch that nothing logs.
 * @param self Service instance.
 * @param token Destination token; uninitialize with http_service_csrf_token_uninit.
 * @param cookie Destination cookie header value; uninitialize with string_uninit.
 * @return true when both were built; false when the CSPRNG or the cookie render failed, in which
 *         case both outputs are left DEFAULT_INITIALIZATION - EMPTY by size, but nullptr-backed
 *         rather than the NUL-terminated "" String the cookie_read tiers answer with, so
 *         string_get_data of a refused cookie is nullptr and must never reach a header call.
 */
bool http_service_csrf_mint(HTTP_Service_CSRF const *const self, HTTP_Service_CSRF_Token *const token, String *const cookie);

/**
 * @brief Route gate for the SYNCHRONIZER-token pattern: let a safe method through,
 *        otherwise verify the request token against the one held server-side.
 * @param self Service instance.
 * @param method HTTP method.
 * @param token Request token, read from the configured header. May be null or empty when
 *        the header was absent - that answers false on a protected method, never aborts.
 * @param stored Stored/session token. May be null - that answers false on a protected
 *        method.
 * @return true when the request can proceed.
 */
bool http_service_csrf_request_allowed_1(HTTP_Service_CSRF const *const self, char const *const method, char const *const token, HTTP_Service_CSRF_Token const *const stored);

/**
 * @brief Route gate for the DOUBLE-SUBMIT pattern: let a safe method through, otherwise
 *        compare the request header's token against this service's own cookie in the
 *        request's Cookie header. Keeps no server-side state and consults no expiry -
 *        the cookie's Max-Age is what expires the pair.
 * @param self Service instance.
 * @param method HTTP method.
 * @param header_token Token from the configured request header. Null or empty answers
 *        false on a protected method, and is checked BEFORE the Cookie header is scanned -
 *        the ordinary cross-site request (cookie present, header absent) costs no scan.
 * @param cookie_header Raw Cookie request header value. Null, empty, or missing this
 *        service's cookie answers false on a protected method.
 * @return true when the request can proceed.
 */
bool http_service_csrf_request_allowed_2(HTTP_Service_CSRF const *const self, char const *const method, char const *const header_token, char const *const cookie_header);

/**
 * @brief Route gate for the DOUBLE-SUBMIT pattern with both sides held as Strings - the
 *        tiers http_server_request_custom_header_get_4 and _header_get_4 hand back.
 *
 * The String twin of http_service_csrf_request_allowed_2: same order of checks, same
 * verdict for the same bytes, so a route holding Strings never has to fall back to
 * http_service_csrf_cookie_read_4 followed by http_service_csrf_token_verify_4.
 * @param self Service instance.
 * @param method HTTP method.
 * @param header_token Token from the configured request header; null or empty answers false
 *        on a protected method, and is checked BEFORE the Cookie header is scanned. Read
 *        through its own size, so the bytes need not be NUL-terminated.
 * @param cookie_header Cookie request header value; null, empty, or missing this service's
 *        cookie answers false on a protected method, and a null or empty one answers it
 *        without scanning. The bytes need not be NUL-terminated.
 * @return true when the request can proceed.
 */
bool http_service_csrf_request_allowed_4(HTTP_Service_CSRF const *const self, char const *const method, String const *const header_token, String const *const cookie_header);

/**
 * @brief Create a random CSRF token using service defaults, in place.
 * @param self Service instance.
 * @param out Destination token; left DEFAULT_INITIALIZATION on false. Uninitialize with
 *        http_service_csrf_token_uninit.
 * @return true on success; false on CSPRNG failure. Never set a cookie on false.
 */
bool http_service_csrf_token_create(HTTP_Service_CSRF const *const self, HTTP_Service_CSRF_Token *const out);

/**
 * @brief Return true when token expiration timestamp has passed.
 * @param self Token instance.
 * @return true when expired.
 */
bool http_service_csrf_token_expired(HTTP_Service_CSRF_Token const *const self);

/**
 * @brief Release token value storage.
 * @param self Token instance.
 */
void http_service_csrf_token_uninit(HTTP_Service_CSRF_Token *const self);

/**
 * @brief Compare a request token against a token held server-side - the synchronizer
 *        primitive under http_service_csrf_request_allowed_1.
 * @param self Service instance.
 * @param token Candidate token value; null or empty answers false.
 * @param stored Stored/session token; null, empty, or expired answers false.
 * @return true when token matches and has not expired.
 */
bool http_service_csrf_token_verify_1(HTTP_Service_CSRF const *const self, char const *const token, HTTP_Service_CSRF_Token const *const stored);

/**
 * @brief Compare a request token against the value echoed back in the cookie - the
 *        double-submit primitive under http_service_csrf_request_allowed_2. No expiry is
 *        consulted: a cookie carries none.
 * @param self Service instance.
 * @param token Candidate token from the request header; null or empty answers false.
 * @param cookie_value Token read out of the Cookie header; null or empty answers false.
 * @return true when the two sides are identical.
 */
bool http_service_csrf_token_verify_2(HTTP_Service_CSRF const *const self, char const *const token, char const *const cookie_value);

/**
 * @brief Compare a request token against a cookie value held as a String - the tier
 *        http_service_csrf_cookie_read_4 hands back.
 * @param self Service instance.
 * @param token Candidate token from the request header; null or empty answers false. This
 *        side is a C string and IS measured with char_length, so it must be terminated.
 * @param cookie_value Token read out of the Cookie header; null or empty answers false.
 *        Read through the String's own size, so those bytes need not be terminated.
 * @return true when the two sides are identical.
 */
bool http_service_csrf_token_verify_4(HTTP_Service_CSRF const *const self, char const *const token, String const *const cookie_value);

/**
 * @brief Release CSRF service storage.
 * @param self Service instance.
 */
void http_service_csrf_uninit(HTTP_Service_CSRF *const self);

#endif // HTTP_SERVICE_CSRF_H