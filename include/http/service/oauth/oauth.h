/*
 * oauth.h - HTTP OAuth 2.0 client service for the C Libraries Framework
 * @version 0.3.0
 *
 * Provides provider configuration, signed state tokens bound to a provider, a
 * TTL and the user agent that started the login, PKCE (RFC 7636)
 * verifier/challenge generation, authorization URL generation, token exchange,
 * token refresh, token revocation, and profile fetching. Account creation,
 * account linking, and session creation must stay in application code.
 *
 * THE SERVICE HAS NO STORE. It holds provider configuration and one HMAC key;
 * it never persists a token, a code, or a nonce. `state` is self-contained
 * (nonce.expiry.mac) precisely so no server-side table is needed, and the PKCE
 * verifier is the caller's to keep between /start and /callback. The login-CSRF
 * BINDING is stateless too: the caller hands the same bytes to issue and verify
 * (a session id, or a random value /start set as an HttpOnly SameSite=Lax
 * cookie), and the MAC - not a table - ties the token to them.
 *
 * Features:
 *   - Generic provider configuration, registered or updated all-or-nothing;
 *     a field carrying a CR or LF, or an authorize_url carrying a "#", is
 *     refused at registration.
 *   - Endpoint constants for Google and GitHub (the registry stays generic).
 *   - Stateless HMAC state tokens: issue with a TTL and a caller binding,
 *     verify constant-time, expiry-checked, bound to the provider they were
 *     issued for and to the user agent that started the login (the _2 tiers;
 *     the _1 tiers bind no user agent and are NOT a login-CSRF defence alone).
 *     The MAC covers the provider name and the binding, each LENGTH-PREFIXED,
 *     plus the nonce and the expiry - so no (provider, binding) pair can spell
 *     another pair's bytes, whatever separators either value carries.
 *   - PKCE S256: verifier generation and challenge derivation.
 *   - Authorization URL generation with encoded query parameters, extra
 *     provider parameters, and an authorize endpoint that already has a query.
 *   - Authorization-code token exchange, with or without a PKCE verifier.
 *   - Refresh-token exchange.
 *   - Token revocation request, with an optional RFC 7009 token_type_hint.
 *   - Bearer-token profile fetch.
 *   - Every network answer carries its transport status, HTTP status code, and
 *     the provider's own `error` / `error_description` - "no access token" is
 *     never the only thing a caller gets to branch on.
 *
 * Usage Examples:
 *   @code
 *   HTTP_Service_OAuth oauth = DEFAULT_INITIALIZATION;
 *
 *   if (!http_service_oauth_init_1(&oauth)) { return; }
 *
 *   HTTP_Service_OAuth_Provider const google = {
 *       .authorize_url = HTTP_SERVICE_OAUTH_GOOGLE_AUTHORIZE_URL,
 *       .client_id     = "...",
 *       .client_secret = "...",
 *       .name          = "google",
 *       .profile_url   = HTTP_SERVICE_OAUTH_GOOGLE_PROFILE_URL,
 *       .redirect_uri  = "https://example.test/auth/oauth/callback",
 *       .revoke_url    = HTTP_SERVICE_OAUTH_GOOGLE_REVOKE_URL,
 *       .scope         = "openid email profile",
 *       .token_url     = HTTP_SERVICE_OAUTH_GOOGLE_TOKEN_URL
 *   };
 *
 *   if (!http_service_oauth_provider_add_1(&oauth, &google)) { return; }
 *
 *   char state[HTTP_SERVICE_OAUTH_STATE_MAX_SIZE + 1]        = DEFAULT_INITIALIZATION;
 *   char verifier[HTTP_SERVICE_OAUTH_PKCE_VERIFIER_SIZE + 1] = DEFAULT_INITIALIZATION;
 *   char challenge[HTTP_SERVICE_OAUTH_PKCE_CHALLENGE_SIZE + 1] = DEFAULT_INITIALIZATION;
 *
 *   // `login` is a random value /start sets as an HttpOnly SameSite=Lax cookie (or the
 *   // user's session id); the callback reads the same cookie back and verifies with it.
 *   char login[33] = DEFAULT_INITIALIZATION;
 *
 *   // Every one of these fails CLOSED: on false there is no state, no verifier, no challenge.
 *   if (result_is_error(crypto_random_hex(login, 16))                                              ||
 *       !http_service_oauth_state_issue_2(&oauth, "google", login, sizeof(login) - 1, 600, state) ||
 *       !http_service_oauth_pkce_verifier_create(verifier)                                         ||
 *       !http_service_oauth_pkce_challenge_create(verifier, challenge)) {
 *       http_service_oauth_uninit(&oauth);
 *
 *       return;
 *   }
 *
 *   // Send the browser here; keep `verifier` in the user's own cookie/session.
 *   String url = http_service_oauth_get_authorize_url_2(&oauth, "google", state, challenge,
 *                                                       "access_type=offline&prompt=consent");
 *
 *   string_uninit(&url);
 *
 *   // ... on the callback, with `state` and `code` off the query string and `login` off the cookie:
 *   if (http_service_oauth_state_verify_2(&oauth, "google", login, sizeof(login) - 1, state)) {
 *       HTTP_Service_OAuth_Token token = http_service_oauth_exchange_code_2(&oauth, "google", "the-code", verifier);
 *
 *       if (http_service_oauth_token_is_ok(&token)) {
 *           HTTP_Service_OAuth_Profile profile = http_service_oauth_get_profile_1(&oauth, "google", &token);
 *
 *           http_service_oauth_profile_uninit(&profile);
 *       }
 *
 *       http_service_oauth_token_uninit(&token);
 *   }
 *
 *   http_service_oauth_uninit(&oauth);
 *   @endcode
 *
 * Error Handling:
 *   - Contract violations - a null self, provider name, output pointer, or token
 *     STRUCT - go through error_check_null, which LOGS AND ABORTS.
 *   - Everything that arrives as DATA is refused by value, never aborted, and
 *     NULL is one of its spellings: a GET /callback with no ?code= or ?state=
 *     hands these functions the data pointer of an empty String, which is null
 *     in this tree, so a null authorization code, state, refresh token or token
 *     to revoke is refused exactly like an empty one - never aborted, because an
 *     unauthenticated request must not be able to kill the server. The same
 *     holds for configuration fields (an empty client_id, an absent revoke
 *     endpoint, a CR or LF in any of the nine, a "#" in authorize_url), an
 *     unparsable or expired state token, a state issued for a different
 *     provider, and a state issued under a different binding. Registration
 *     answers false; the network calls answer a token or profile whose
 *     `status`, `response_code` and `error` say what happened.
 *   - The state BINDING is data too: it is read off the callback's cookie or
 *     session, so a null one claiming a size, or one over
 *     HTTP_SERVICE_OAUTH_STATE_BINDING_MAX_SIZE, is refused by value at both
 *     issue and verify. So is an EMPTY one (size 0) on the _2 tiers: the
 *     empty binding is what the _1 tiers mean, and only they may pass it. The
 *     provider name on the state paths is bounded the same way, so request
 *     data never sizes the MAC message.
 *   - "misconfigured" covers two operator responses: a provider whose
 *     registration never completed is permanent, a row copy the allocator
 *     refused is transient and worth retrying once the arena drains. The field
 *     cannot tell them apart; the WARN logged on the second one can.
 *   - A request body or authorize URL that could not be built WHOLE is never
 *     sent or returned: one field over http/query's encode ceiling, or an
 *     allocator refusal, fails the whole call with error "invalid_request" (or
 *     an empty URL) rather than reaching the provider missing its code, its
 *     client_id or its state.
 *   - A CR or LF is refused before it can end a header line, never sanitized
 *     out of one: in an access token before "Authorization: Bearer ..." is
 *     built, and in get_authorize_url_2's verbatim `extra_params` before a URL
 *     a caller puts in a "Location:" header is returned.
 *   - A PKCE code_verifier outside RFC 7636's 43..128 chars is refused locally
 *     with error "invalid_request", rather than spending a round trip to be
 *     told invalid_grant - the same bounds pkce_challenge_create enforces. The
 *     other half is bounded too: a code_challenge that is not exactly
 *     HTTP_SERVICE_OAUTH_PKCE_CHALLENGE_SIZE chars yields an empty authorize URL.
 *   - A provider that is not registered yields an empty URL and a token/profile
 *     carrying error "unknown_provider" - never a request to a wrong endpoint.
 *     A provider that IS registered whose row copy the allocator refused is
 *     "misconfigured" instead, so an exhausted arena does not read as a
 *     registration that never happened.
 *   - Constructors refuse AS A WHOLE: a failed mutex or key initialization
 *     answers false with nothing allocated, rather than half-building a service
 *     whose registry can be mutated without mutual exclusion.
 *   - `id_token` is passed through UNVERIFIED. Nothing here checks its JWT
 *     signature, issuer, audience, or expiry. Treat it as an opaque string and
 *     never trust a claim read out of it; use the profile endpoint instead.
 *
 * Thread Safety:
 *   - Public operations take this instance's own mutex, and hold it ONLY while
 *     copying the provider row out of the registry. Every network round trip
 *     runs unlocked against that private snapshot, so one stalled provider can
 *     no longer serialize every other login in the process.
 *   - Instances are independent: each owns its lock, so destroying one does not
 *     affect another. The lock lives in the struct, which is why the constructors
 *     initialize in place instead of returning the service by value.
 *   - A provider updated while a request is in flight does not disturb that
 *     request: it finishes against the snapshot it took.
 *
 * Memory Management:
 *   - Provider strings are copied into service-owned storage.
 *   - Returned String, token, and profile values must be uninitialized by caller
 *     (http_service_oauth_token_uninit / _profile_uninit / string_uninit).
 *   - token.raw and token.access_token CARRY THE CREDENTIAL - never log them.
 *     Their storage is released, not wiped, on uninit: a memory-wiping primitive
 *     is an open CFW gap, so the bytes can outlive the free. The same holds for
 *     the client_secret in the per-call provider snapshot, which is deep-copied
 *     and released unwiped once the round trip ends - and for the state key,
 *     which http_service_oauth_uninit zeroes with a plain memory_set that is a
 *     dead store to the optimizer, so LTO may elide it.
 *   - An arena-backed service allocates every String this module builds from
 *     that arena - returned values and per-call scratch alike - so size it for
 *     the response cap below, not just for the provider rows.
 *   - State tokens, PKCE verifiers and challenges are written into CALLER
 *     buffers; nothing is allocated for them.
 *   - Call http_service_oauth_uninit() when finished.
 *
 * Performance Characteristics:
 *   - Provider lookup is a linear scan over the registered rows; fine below the
 *     handful of providers a product actually offers.
 *   - One provider row is deep-copied per network call. That is a handful of
 *     small allocations against a round trip measured in milliseconds, and it is
 *     what buys the unlocked I/O above.
 *   - One HTTP client handle is created and destroyed per call. Negligible
 *     beside the round trip; a pooled handle would need per-thread ownership.
 *   - Responses are capped at HTTP_SERVICE_OAUTH_MAX_RESPONSE_SIZE, well above
 *     any real token or userinfo document and far below the client default.
 *
 * Known gaps:
 *   - GitHub keeps a user's verified addresses at /user/emails, not on /user, so
 *     get_profile can leave `email` empty for a GitHub account with a private
 *     address. Fetch that endpoint with the same bearer token if you need it.
 *   - get_profile_2 always sends a Bearer token; get_profile_1 refuses a token
 *     whose token_type is set and is not "bearer" (case-insensitively) rather
 *     than presenting it under the wrong scheme. A provider answering another
 *     scheme still needs its own request - nothing here can build one.
 *   - The _1 state tiers bind no user agent. Without a binding, any state this
 *     service ever minted verifies on any browser's callback until it expires,
 *     which is exactly RFC 6749 10.12's login-CSRF attack. Use the _2 tiers, or
 *     PKCE with the verifier held in the user's own cookie.
 *   - Token-endpoint authentication is client_secret_post only: client_id and
 *     client_secret go in the body. RFC 6749 2.3.1 lets a server require
 *     client_secret_basic instead, which some OIDC deployments do; Google and
 *     GitHub both accept post.
 *
 * Dependencies:
 *   - allocator, arena, crypto/hash, crypto/hmac, crypto/random, datetime,
 *     encoding/base64, http/client, http/query, json, log, string, thread.
 *
 * See oauth.c for implementation details.
 */

#ifndef HTTP_SERVICE_OAUTH_H
#define HTTP_SERVICE_OAUTH_H

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/

#include <allocator/allocator.h>
#include <crypto/hash/hash.h>
#include <crypto/hmac/hmac.h>
#include <crypto/random/random.h>
#include <datetime/datetime.h>
#include <encoding/base64/base64.h>
#include <http/client/http_client.h>
#include <http/query/query.h>
#include <json/json.h>
#include <log/log.h>
#include <thread/thread.h>

/*==============================================================================
 * MARK: - Macros
 *============================================================================*/

/** @brief GitHub authorization endpoint. */
#define HTTP_SERVICE_OAUTH_GITHUB_AUTHORIZE_URL "https://github.com/login/oauth/authorize"
/** @brief GitHub profile endpoint. GitHub has no RFC 7009 revoke endpoint. */
#define HTTP_SERVICE_OAUTH_GITHUB_PROFILE_URL "https://api.github.com/user"
/** @brief GitHub token endpoint. Answers JSON only when Accept: application/json is sent. */
#define HTTP_SERVICE_OAUTH_GITHUB_TOKEN_URL "https://github.com/login/oauth/access_token"
/** @brief Google authorization endpoint. */
#define HTTP_SERVICE_OAUTH_GOOGLE_AUTHORIZE_URL "https://accounts.google.com/o/oauth2/v2/auth"
/** @brief Google OpenID userinfo endpoint. */
#define HTTP_SERVICE_OAUTH_GOOGLE_PROFILE_URL "https://www.googleapis.com/oauth2/v3/userinfo"
/** @brief Google token revocation endpoint. */
#define HTTP_SERVICE_OAUTH_GOOGLE_REVOKE_URL "https://oauth2.googleapis.com/revoke"
/** @brief Google token endpoint. */
#define HTTP_SERVICE_OAUTH_GOOGLE_TOKEN_URL "https://oauth2.googleapis.com/token"
/**
 * @brief Per-call response cap in bytes (64 KiB). A token response is a few
 *        hundred bytes and a userinfo document a few kilobytes, so this is
 *        generous - and orders of magnitude below the client's own 16 MiB
 *        default, which a hostile or broken endpoint could otherwise fill.
 */
#define HTTP_SERVICE_OAUTH_MAX_RESPONSE_SIZE (64 * 1024)
/** @brief Chars in a PKCE S256 code challenge (base64url of a 32-byte digest), excluding NUL. */
#define HTTP_SERVICE_OAUTH_PKCE_CHALLENGE_SIZE 43
/** @brief Chars in a generated PKCE code verifier (base64url of 32 random bytes), excluding NUL. */
#define HTTP_SERVICE_OAUTH_PKCE_VERIFIER_SIZE 43
/**
 * @brief Largest caller binding a state token can be issued or verified under,
 *        in bytes. A session id or a random cookie value is tens of bytes; the
 *        cap exists so request data cannot size the MAC message.
 */
#define HTTP_SERVICE_OAUTH_STATE_BINDING_MAX_SIZE 256
/** @brief Bytes in the per-service HMAC key that signs state tokens. */
#define HTTP_SERVICE_OAUTH_STATE_KEY_SIZE 32
/**
 * @brief Largest state token this service issues or accepts, excluding NUL:
 *        32 nonce hex chars, a dot, up to 20 expiry digits, a dot, and 64 MAC
 *        hex chars - 118, rounded up.
 */
#define HTTP_SERVICE_OAUTH_STATE_MAX_SIZE 128

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief Tri-state answer to "did the provider say this address is verified".
 *
 * ABSENT is not false: Google sends email_verified, GitHub does not, and a
 * consumer that gates account creation on a verified address must be able to
 * tell "the provider says no" from "the provider did not say".
 */
typedef enum {
    /** @brief The provider sent no email_verified field. */
    HTTP_SERVICE_OAUTH_EMAIL_VERIFIED_UNKNOWN,
    /** @brief The provider sent email_verified: false. */
    HTTP_SERVICE_OAUTH_EMAIL_VERIFIED_NO,
    /** @brief The provider sent email_verified: true. */
    HTTP_SERVICE_OAUTH_EMAIL_VERIFIED_YES
} HTTP_Service_OAuth_Email_Verified;

/**
 * @brief One registered provider's configuration row. OPAQUE: defined only in
 *        oauth.c, so the nine fields can change shape without breaking a consumer.
 */
typedef struct HTTP_Service_OAuth_Row HTTP_Service_OAuth_Row;

/**
 * @brief In-memory OAuth provider registry.
 */
typedef struct {
#ifdef ARENA_IMPLEMENTATION
    /** @brief Optional arena used by owned provider strings and returned values. */
    Arena *allocator;
#endif // ARENA_IMPLEMENTATION
    /**
     * @brief Guards every access to the provider rows.
     *
     * Per instance, not file-static. One shared lock meant uninit of ANY instance destroyed the
     * lock every other instance was still using, and each init re-initialized a possibly-locked
     * mutex - both undefined behaviour on CRITICAL_SECTION and pthreads alike.
     */
    ThreadMutex mutex;
    /** @brief Rows the array can hold before it has to grow. */
    USize row_capacity;
    /** @brief Rows actually registered. */
    USize row_count;
    /** @brief Registered providers, one struct each - NOT parallel arrays. */
    HTTP_Service_OAuth_Row *rows;
    /** @brief Key that signs and verifies this service's state tokens. */
    U8 state_key[HTTP_SERVICE_OAUTH_STATE_KEY_SIZE];
    /** @brief Bytes of state_key actually in use. */
    USize state_key_size;
} HTTP_Service_OAuth;

/**
 * @brief Profile response returned by provider userinfo endpoints.
 */
typedef struct {
    /** @brief Provider email value when present. */
    String email;
    /** @brief Whether the provider vouched for the address; see the enum. */
    HTTP_Service_OAuth_Email_Verified email_verified;
    /** @brief Machine-readable failure code: the provider's own, or one of this
     *         service's ("unknown_provider", "invalid_request", "misconfigured",
     *         "transport"). Empty when the fetch succeeded. */
    String error;
    /** @brief Human-readable failure detail when the provider sent one. */
    String error_description;
    /** @brief Provider user ID or subject value; a numeric id is rendered as decimal. */
    String id;
    /** @brief Provider display name when present. */
    String name;
    /** @brief Provider avatar/profile image URL when present. */
    String picture;
    /** @brief Raw provider profile response. */
    String raw;
    /** @brief HTTP status the provider answered with, or 0 when none was received. */
    USize response_code;
    /** @brief Transport outcome. HTTP_CLIENT_STATUS_ERROR with response_code 0
     *         is also what a refusal that never reached the wire reports. */
    HTTP_Client_Status status;
} HTTP_Service_OAuth_Profile;

/**
 * @brief Provider configuration view used when registering a provider.
 */
typedef struct {
    /** @brief Provider authorization endpoint URL. Required. */
    char const *authorize_url;
    /** @brief OAuth client ID. Required. */
    char const *client_id;
    /** @brief OAuth client secret. Required. */
    char const *client_secret;
    /** @brief Provider lookup name. Required. */
    char const *name;
    /** @brief Provider userinfo/profile endpoint URL. Required. */
    char const *profile_url;
    /** @brief OAuth redirect URI. Required. */
    char const *redirect_uri;
    /** @brief Provider token revoke endpoint URL. OPTIONAL - GitHub and Discord
     *         have no RFC 7009 endpoint, and requiring one would force a lie.
     *         Empty (or null) registers the provider and makes revoke answer false. */
    char const *revoke_url;
    /** @brief Provider scope string. OPTIONAL - empty (or null) omits `&scope=`
     *         from the authorization URL entirely rather than sending an empty one. */
    char const *scope;
    /** @brief Provider token endpoint URL. Required. */
    char const *token_url;
} HTTP_Service_OAuth_Provider;

/**
 * @brief Token response returned by provider token endpoints.
 */
typedef struct {
    /** @brief Provider access token. THE CREDENTIAL - never log it. */
    String access_token;
    /** @brief Machine-readable failure code: the provider's own ("invalid_grant",
     *         "invalid_client", ...), or one of this service's ("unknown_provider",
     *         "misconfigured", "invalid_request", "transport"). Empty when the
     *         exchange succeeded. */
    String error;
    /** @brief Human-readable failure detail when the provider sent one. */
    String error_description;
    /** @brief Absolute epoch second the access token expires at, or 0 when the
     *         provider sent no lifetime. Computed at parse time from expires_in
     *         so a caller storing the token does not repeat the arithmetic. */
    USize expires_at;
    /** @brief Access token lifetime in seconds when provided. Read whether the
     *         provider sent it as a number or as a string. */
    USize expires_in;
    /** @brief Provider ID token when provided. UNVERIFIED - see Error Handling. */
    String id_token;
    /** @brief Raw provider token response. CARRIES THE CREDENTIAL - never log it. */
    String raw;
    /** @brief Provider refresh token when provided. THE CREDENTIAL - never log it. */
    String refresh_token;
    /** @brief HTTP status the provider answered with, or 0 when none was received. */
    USize response_code;
    /** @brief Granted scope string when provided. */
    String scope;
    /** @brief Transport outcome; the field to branch on before response_code. */
    HTTP_Client_Status status;
    /** @brief Provider token type, usually Bearer. */
    String token_type;
} HTTP_Service_OAuth_Token;

/*==============================================================================
 * MARK: - API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Initialize an arena-backed OAuth service with a generated state key.
 * @param self Service to initialize IN PLACE.
 * @param allocator Arena allocator.
 * @return true when the service is ready. On false NOTHING was initialized and
 *         self must not be used or uninitialized.
 * @note Writes through self rather than returning the service, because it owns a ThreadMutex
 *       and neither CRITICAL_SECTION nor pthread_mutex_t may be copied once initialized - a
 *       by-value return would hand the caller a copy of an initialized lock.
 */
bool http_service_oauth_alloc_init_1(HTTP_Service_OAuth *const self, Arena *const allocator);

/**
 * @brief Initialize an arena-backed OAuth service with an EXPLICIT state key.
 * @param self Service to initialize IN PLACE.
 * @param allocator Arena allocator.
 * @param key Key bytes that sign state tokens.
 * @param key_size Bytes of key to use; 1..HTTP_SERVICE_OAUTH_STATE_KEY_SIZE.
 * @return true when the service is ready; false (nothing initialized) on a
 *         key_size of 0 or above the maximum.
 * @note Use this across a multi-process or restarted deployment: a state token
 *       issued under a generated key stops verifying the moment the process that
 *       generated it goes away.
 */
bool http_service_oauth_alloc_init_2(HTTP_Service_OAuth *const self, Arena *const allocator, U8 const *const key, USize const key_size);

/**
 * @brief Pre-tier spelling of http_service_oauth_alloc_init_1.
 * @param self Service to initialize IN PLACE.
 * @param allocator Arena allocator.
 * @return true when the service is ready.
 * @deprecated Call http_service_oauth_alloc_init_1; retired before 1.0.
 */
bool http_service_oauth_alloc_init(HTTP_Service_OAuth *const self, Arena *const allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Exchange an authorization code for a provider token response.
 * @param self Service instance.
 * @param provider Provider name.
 * @param code Authorization code from provider redirect. Empty OR NULL is
 *        REFUSED by value (error "invalid_request"), never sent as "code=" and
 *        never aborted - a callback URL with no ?code= arrives here as null.
 * @return Parsed token response with raw JSON and failure fields attached.
 *         Caller must http_service_oauth_token_uninit it.
 */
HTTP_Service_OAuth_Token http_service_oauth_exchange_code_1(HTTP_Service_OAuth *const self, char const *const provider, char const *const code);

/**
 * @brief Exchange an authorization code, sending a PKCE code verifier with it.
 * @param self Service instance.
 * @param provider Provider name.
 * @param code Authorization code from provider redirect. Empty or null is
 *        REFUSED by value, as in http_service_oauth_exchange_code_1.
 * @param code_verifier PKCE verifier matching the challenge sent to /authorize.
 *        Empty or null omits code_verifier, making this identical to _1. A
 *        present one is BOUNDED to RFC 7636 4.1's 43..128 chars - the same range
 *        http_service_oauth_pkce_challenge_create enforces - and one outside it
 *        is refused by value rather than spent as a round trip.
 * @return Parsed token response, carrying error "invalid_request" when the code
 *         was empty or null, when the code_verifier was out of range, or when a
 *         field would not fit the request body. Caller must uninitialize it.
 */
HTTP_Service_OAuth_Token http_service_oauth_exchange_code_2(HTTP_Service_OAuth *const self, char const *const provider, char const *const code, char const *const code_verifier);

/**
 * @brief Pre-tier spelling of http_service_oauth_exchange_code_1.
 * @param self Service instance.
 * @param provider Provider name.
 * @param code Authorization code.
 * @return Parsed token response. Caller must uninitialize it.
 * @deprecated Call http_service_oauth_exchange_code_1; retired before 1.0.
 */
HTTP_Service_OAuth_Token http_service_oauth_exchange_code(HTTP_Service_OAuth *const self, char const *const provider, char const *const code);

/**
 * @brief Build a provider authorization URL.
 * @param self Service instance.
 * @param provider Provider name.
 * @param state CSRF/session state value - see http_service_oauth_state_issue_2.
 *        Empty or null is REFUSED by value, never aborted, and never sent as
 *        "&state=" - a state the provider echoes back with nothing to verify.
 * @return URL string, empty when the provider is unknown, when `state` was empty
 *         or null, or when any parameter would not fit. Caller must uninitialize it.
 */
String http_service_oauth_get_authorize_url_1(HTTP_Service_OAuth *const self, char const *const provider, char const *const state);

/**
 * @brief Build a provider authorization URL with PKCE and provider extras.
 * @param self Service instance.
 * @param provider Provider name.
 * @param state CSRF/session state value. Empty or null is REFUSED by value.
 * @param code_challenge PKCE S256 challenge; empty or null omits code_challenge
 *        and code_challenge_method entirely.
 * @param extra_params ALREADY-ENCODED "a=b&c=d" fragment appended verbatim -
 *        "access_type=offline&prompt=consent" is what makes Google issue a
 *        refresh token at all. Empty or null appends nothing. Nothing here
 *        re-encodes it: pass values you encoded yourself. NEVER pass request
 *        data here - this is server configuration. A CR or LF inside it is
 *        REFUSED (empty URL), because the result goes into a "Location:" header
 *        a line break would split; and a duplicate `redirect_uri` or `scope` in
 *        this fragment is appended AFTER the module's own, which some providers
 *        resolve by taking the last occurrence.
 * @return URL string, empty when the provider is unknown, when `state` was empty
 *         or null, when `extra_params` carried a CR or LF, or when any parameter
 *         would not fit. Caller must uninitialize it.
 * @note The separator is chosen by inspection: an authorize endpoint that
 *       already carries a query gets "&", not a second "?". Only "?" is looked
 *       for - see the Known gaps on a configured "#" fragment.
 */
String http_service_oauth_get_authorize_url_2(HTTP_Service_OAuth *const self, char const *const provider, char const *const state,
    char const *const code_challenge, char const *const extra_params);

/**
 * @brief Pre-tier spelling of http_service_oauth_get_authorize_url_1.
 * @param self Service instance.
 * @param provider Provider name.
 * @param state CSRF/session state value.
 * @return URL string. Caller must uninitialize it.
 * @deprecated Call http_service_oauth_get_authorize_url_1; retired before 1.0.
 */
String http_service_oauth_get_authorize_url(HTTP_Service_OAuth *const self, char const *const provider, char const *const state);

/**
 * @brief Fetch a provider profile using a token response.
 * @param self Service instance.
 * @param provider Provider name.
 * @param token Token response; its access_token is sent as a Bearer credential.
 *        The struct pointer is CONTRACT and checked; the access_token inside is
 *        DATA - empty, or containing a CR or LF, is REFUSED by value with error
 *        "invalid_request" rather than pasted into a header line it would split.
 *        A token_type that is set and is not "bearer" (case-insensitively) is
 *        refused the same way rather than presented under the wrong scheme; an
 *        absent token_type is taken as Bearer.
 * @return Parsed profile response; see http_service_oauth_get_profile_2, which
 *         this forwards to with the token's access_token.
 */
HTTP_Service_OAuth_Profile http_service_oauth_get_profile_1(HTTP_Service_OAuth *const self, char const *const provider, HTTP_Service_OAuth_Token const *const token);

/**
 * @brief Fetch a provider profile using a bare access token.
 * @param self Service instance.
 * @param provider Provider name.
 * @param access_token Access token sent as "Authorization: Bearer ...". DATA:
 *        empty OR NULL (a token read back from storage absent arrives as the
 *        null data pointer of an empty String), or one containing a CR or LF,
 *        is REFUSED by value with error "invalid_request", never aborted.
 * @return Parsed profile response with raw JSON and failure fields attached;
 *         error "unknown_provider" for an unregistered name, "misconfigured"
 *         when the row exists and its copy was refused. Caller must
 *         http_service_oauth_profile_uninit it.
 * @note This is the form for a token stored earlier: nothing has to fabricate
 *       a Token struct around one char*.
 */
HTTP_Service_OAuth_Profile http_service_oauth_get_profile_2(HTTP_Service_OAuth *const self, char const *const provider, char const *const access_token);

/**
 * @brief Pre-tier spelling of http_service_oauth_get_profile_1.
 * @param self Service instance.
 * @param provider Provider name.
 * @param token Token response.
 * @return Parsed profile response. Caller must uninitialize it.
 * @deprecated Call http_service_oauth_get_profile_1; retired before 1.0.
 */
HTTP_Service_OAuth_Profile http_service_oauth_get_profile(HTTP_Service_OAuth *const self, char const *const provider, HTTP_Service_OAuth_Token const *const token);

/**
 * @brief Initialize an empty OAuth service with a generated state key.
 * @param self Service to initialize IN PLACE.
 * @return true when the service is ready. On false NOTHING was initialized and
 *         self must not be used or uninitialized.
 * @note Writes through self; see http_service_oauth_alloc_init_1 for why it
 *       cannot return the service by value.
 */
bool http_service_oauth_init_1(HTTP_Service_OAuth *const self);

/**
 * @brief Initialize an empty OAuth service with an EXPLICIT state key.
 * @param self Service to initialize IN PLACE.
 * @param key Key bytes that sign state tokens.
 * @param key_size Bytes of key to use; 1..HTTP_SERVICE_OAUTH_STATE_KEY_SIZE.
 * @return true when the service is ready; false (nothing initialized) on a
 *         key_size of 0 or above the maximum.
 */
bool http_service_oauth_init_2(HTTP_Service_OAuth *const self, U8 const *const key, USize const key_size);

/**
 * @brief Pre-tier spelling of http_service_oauth_init_1.
 * @param self Service to initialize IN PLACE.
 * @return true when the service is ready.
 * @deprecated Call http_service_oauth_init_1; retired before 1.0.
 */
bool http_service_oauth_init(HTTP_Service_OAuth *const self);

/**
 * @brief Derive the PKCE S256 challenge for a code verifier (RFC 7636 §4.2).
 * @param verifier Code verifier text, 43..128 chars per RFC 7636 §4.1.
 * @param out Destination; must hold HTTP_SERVICE_OAUTH_PKCE_CHALLENGE_SIZE + 1 chars.
 * @return true when the challenge was written; false (out untouched) on a
 *         verifier outside the legal length range or a digest failure.
 */
bool http_service_oauth_pkce_challenge_create(char const *const verifier, char *const out);

/**
 * @brief Generate a PKCE code verifier: 32 CSPRNG bytes, base64url, unpadded.
 * @param out Destination; must hold HTTP_SERVICE_OAUTH_PKCE_VERIFIER_SIZE + 1 chars.
 * @return true when the verifier was written; false (out untouched) when the
 *         CSPRNG refused - fail closed, never a predictable verifier.
 */
bool http_service_oauth_pkce_verifier_create(char *const out);

/**
 * @brief Whether a profile fetch actually succeeded.
 * @param self Profile response.
 * @return true when the transport completed, the provider answered 2xx, no error
 *         code was parsed, and an id was parsed out - the same four conditions
 *         http_service_oauth_token_is_ok requires. A 2xx carrying an id AND an
 *         `error` is NOT ok.
 */
bool http_service_oauth_profile_is_ok(HTTP_Service_OAuth_Profile const *const self);

/**
 * @brief Release profile response storage.
 * @param self Profile response.
 */
void http_service_oauth_profile_uninit(HTTP_Service_OAuth_Profile *const self);

/**
 * @brief Register or update a provider from a provider struct.
 * @param self Service instance.
 * @param provider Provider configuration. name, client_id, client_secret,
 *        redirect_uri, authorize_url, token_url and profile_url must be
 *        non-empty; revoke_url and scope are optional and may be null or empty.
 * @return true when the provider is registered. On false NOTHING was stored: a
 *         missing required field, or an allocator refusal, is REFUSED BY VALUE
 *         rather than aborting - configuration is data, and an empty
 *         TRAYMON_OAUTH_..._SECRET= in the environment must not kill a server at
 *         boot. Registration is all-or-nothing, and so is an update of an
 *         existing name: the whole replacement row is built before the stored
 *         one is released, so a refused update leaves the provider exactly as it
 *         was rather than pairing a new client_id with the old secret.
 */
bool http_service_oauth_provider_add_1(HTTP_Service_OAuth *const self, HTTP_Service_OAuth_Provider const *const provider);

/**
 * @brief Register or update a provider from explicit fields.
 * @param self Service instance.
 * @param name Provider name.
 * @param client_id OAuth client ID.
 * @param client_secret OAuth client secret.
 * @param redirect_uri OAuth redirect URI.
 * @param authorize_url Provider authorization endpoint.
 * @param token_url Provider token endpoint.
 * @param profile_url Provider userinfo/profile endpoint.
 * @param revoke_url Provider revoke endpoint; optional.
 * @param scope Provider scope list; optional.
 * @return true when the provider is registered; see http_service_oauth_provider_add_1.
 * @deprecated Nine same-typed positional char* in an order that matches neither
 *             the struct nor the wire. Call http_service_oauth_provider_add_1
 *             with a designated initializer; this is retired before 1.0.
 */
bool http_service_oauth_provider_add_2(HTTP_Service_OAuth *const self,
    char const *const name, char const *const client_id, char const *const client_secret, char const *const redirect_uri, char const *const authorize_url, char const *const token_url,
    char const *const profile_url, char const *const revoke_url, char const *const scope);

/**
 * @brief Exchange a refresh token for a new provider token response.
 * @param self Service instance.
 * @param provider Provider name.
 * @param refresh_token Refresh token string. Empty OR NULL is REFUSED by value,
 *        never aborted - a stored token read back absent arrives here as null.
 * @return Parsed token response, carrying error "invalid_request" when the
 *         refresh token was empty or null, or when a field would not fit the
 *         request body. Caller must uninitialize it.
 */
HTTP_Service_OAuth_Token http_service_oauth_refresh(HTTP_Service_OAuth *const self, char const *const provider, char const *const refresh_token);

/**
 * @brief Revoke a provider token.
 * @param self Service instance.
 * @param provider Provider name.
 * @param token Token string. Empty OR NULL is REFUSED by value, never aborted.
 * @return true only when the provider answered 2xx. false when the token was
 *         empty or null, the provider is unknown, it has no revoke endpoint
 *         configured, a field would not fit the request body, the transport
 *         failed, or the endpoint rejected the request.
 */
bool http_service_oauth_revoke_1(HTTP_Service_OAuth *const self, char const *const provider, char const *const token);

/**
 * @brief Revoke a provider token, telling the endpoint which kind it is.
 * @param self Service instance.
 * @param provider Provider name.
 * @param token Token string. Empty or null is REFUSED by value, as in
 *        http_service_oauth_revoke_1.
 * @param token_type_hint RFC 7009 §2.1 hint, "access_token" or "refresh_token".
 *        Empty or null omits the parameter, making this identical to _1.
 * @return true only when the provider answered 2xx; see http_service_oauth_revoke_1.
 */
bool http_service_oauth_revoke_2(HTTP_Service_OAuth *const self, char const *const provider, char const *const token, char const *const token_type_hint);

/**
 * @brief Issue a signed, self-contained state token for a login round trip.
 * @param self Service instance.
 * @param provider Provider name the state is bound to. DATA: one longer than
 *        HTTP_SERVICE_OAUTH_STATE_BINDING_MAX_SIZE is refused by value, so a
 *        route parameter cannot size the MAC message.
 * @param ttl_seconds Seconds the token stays valid; must be non-zero, and small
 *        enough that "now + ttl_seconds" does not wrap USIZE_MAX.
 * @param out Destination; must hold HTTP_SERVICE_OAUTH_STATE_MAX_SIZE + 1 chars.
 * @return true when a token was written; false (out untouched) on an empty or
 *         over-long provider, a zero ttl, a ttl that would wrap the expiry, or
 *         a CSPRNG/HMAC failure - fail closed, never an unsigned state and never
 *         a token born already expired.
 * @note The token is "nonce.expiry.mac" and needs no server-side storage. It
 *       proves the callback belongs to a login this service started, for THIS
 *       provider, within the TTL - and NOTHING about who started it: the MAC
 *       message is the one http_service_oauth_state_issue_2 documents, under
 *       the EMPTY binding (so this tier shares that tier's 0.3.0 format
 *       change), and the empty binding is this tier's alone - the _2 tier
 *       refuses it. On its own this tier is NOT a login-CSRF defence: every
 *       state it ever minted verifies on any browser's callback until it
 *       expires, so an attacker can plant their own state and code in a
 *       victim's callback URL (RFC 6749 10.12). Use
 *       http_service_oauth_state_issue_2 with a binding, or PKCE with the
 *       verifier held in the user's own cookie. It is not a session and not a
 *       nonce replay guard either: a token stays verifiable until it expires.
 */
bool http_service_oauth_state_issue_1(HTTP_Service_OAuth *const self, char const *const provider, USize const ttl_seconds, char *const out);

/**
 * @brief Issue a signed state token bound to the user agent that starts the login.
 * @param self Service instance.
 * @param provider Provider name the state is bound to. DATA: one longer than
 *        HTTP_SERVICE_OAUTH_STATE_BINDING_MAX_SIZE is refused by value.
 * @param binding Bytes identifying the user agent: the user's session id, or a
 *        random value (16 bytes of crypto_random_hex is plenty) that the /start
 *        route sets as an HttpOnly SameSite=Lax cookie and the /callback route
 *        reads back. Arbitrary bytes, NOT NUL-terminated - `binding_size` says
 *        how many. DATA, refused by value: null with a non-zero size answers
 *        false, and so does a size of 0 - see the note.
 * @param binding_size Bytes of `binding`; 1..HTTP_SERVICE_OAUTH_STATE_BINDING_MAX_SIZE,
 *        refused outside that range.
 * @param ttl_seconds Seconds the token stays valid; must be non-zero, and small
 *        enough that "now + ttl_seconds" does not wrap USIZE_MAX.
 * @param out Destination; must hold HTTP_SERVICE_OAUTH_STATE_MAX_SIZE + 1 chars.
 * @return true when a token was written; false (out untouched) on an empty or
 *         over-long provider, a refused binding (empty, over the cap, or null
 *         with a size), a zero ttl, a ttl that would wrap the expiry, or a
 *         CSPRNG/HMAC failure - fail closed, never an unsigned state and never
 *         a token born already expired.
 * @note An EMPTY binding is a REFUSAL, not a degraded issue: with one this tier
 *       would mint exactly what http_service_oauth_state_issue_1 mints, a state
 *       any browser's callback redeems, and a _2 caller only ever reaches that
 *       through a bug (a /start that lost the value it meant to bind). A
 *       caller that genuinely has no binding must call _1 and accept what that
 *       tier's note says it means.
 * @note The binding is folded into the MAC message LENGTH-PREFIXED, so the
 *       token is tied to exactly those bytes, and the token itself still
 *       carries nothing but "nonce.expiry.mac": the state stays stateless. The
 *       provider name is prefixed the same way, since 0.3.0: a name is a route
 *       parameter and may carry the message's own separators, and before 0.3.0
 *       one (provider, binding) pair could spell another's exact bytes. That
 *       makes 0.3.0's MAC message a DIFFERENT message from 0.2.x's even under
 *       the same key: a state minted by a 0.2.x process does not verify under
 *       0.3.0, and the reverse, so a rolling restart across that upgrade sends
 *       the logins in flight back to /start - the same thing a key change does,
 *       and as harmless (nothing is stored; the user starts again). A
 *       callback that presents this token with a different binding - another
 *       browser's cookie, another session - is refused, which is the login-CSRF
 *       defence the _1 tier does not have. The binding is the caller's
 *       SECRET-ENOUGH value: it must be unguessable by the attacker (a random
 *       cookie or a session id is; a username is not), and it must be the same
 *       bytes on both routes.
 */
bool http_service_oauth_state_issue_2(HTTP_Service_OAuth *const self, char const *const provider, char const *const binding, USize const binding_size, USize const ttl_seconds,
    char *const out);

/**
 * @brief Pre-tier spelling of http_service_oauth_state_issue_1.
 * @param self Service instance.
 * @param provider Provider name the state is bound to.
 * @param ttl_seconds Seconds the token stays valid.
 * @param out Destination; must hold HTTP_SERVICE_OAUTH_STATE_MAX_SIZE + 1 chars.
 * @return true when a token was written.
 * @deprecated Call http_service_oauth_state_issue_1 - or, for a login-CSRF
 *             defence, http_service_oauth_state_issue_2; retired before 1.0.
 */
bool http_service_oauth_state_issue(HTTP_Service_OAuth *const self, char const *const provider, USize const ttl_seconds, char *const out);

/**
 * @brief Verify a state token issued by this service for this provider, under
 *        no binding.
 * @param self Service instance.
 * @param provider Provider name the state must be bound to. DATA: one longer
 *        than HTTP_SERVICE_OAUTH_STATE_BINDING_MAX_SIZE is refused by value.
 * @param state State token from the provider's redirect. Empty or NULL is
 *        REFUSED by value, never aborted: a callback with no ?state= is the
 *        first thing an attacker sends, and it arrives here as a null pointer.
 * @return true only when the MAC verifies (constant-time), the token was issued
 *         for `provider` under the EMPTY binding, and the expiry has not passed.
 *         An absent, empty, malformed, truncated, oversized, tampered, expired,
 *         cross-provider or cross-binding token, or an over-long provider,
 *         answers false without distinguishing which - the distinction would be
 *         an oracle.
 * @note Verifies under the empty binding - the one binding
 *       http_service_oauth_state_verify_2 refuses - so it accepts exactly what
 *       http_service_oauth_state_issue_1 minted, from ANY browser, which is why
 *       on its own it is NOT a login-CSRF defence; see
 *       http_service_oauth_state_issue_1.
 */
bool http_service_oauth_state_verify_1(HTTP_Service_OAuth *const self, char const *const provider, char const *const state);

/**
 * @brief Verify a state token issued by this service for this provider and
 *        this binding.
 * @param self Service instance.
 * @param provider Provider name the state must be bound to. DATA: one longer
 *        than HTTP_SERVICE_OAUTH_STATE_BINDING_MAX_SIZE is refused by value.
 * @param binding The same bytes the token was issued under - read back off the
 *        callback's cookie or session. DATA, refused by value exactly as in
 *        http_service_oauth_state_issue_2, the EMPTY binding included: an
 *        absent cookie arrives as a null pointer and a size of 0, and that is
 *        the attack posture (a planted state on a browser that never started
 *        the login), so it answers false by construction rather than falling
 *        through to what http_service_oauth_state_verify_1 would say.
 * @param binding_size Bytes of `binding`; 1..HTTP_SERVICE_OAUTH_STATE_BINDING_MAX_SIZE,
 *        refused outside that range.
 * @param state State token from the provider's redirect. Empty or NULL is
 *        REFUSED by value, never aborted.
 * @return true only when the MAC verifies (constant-time) over `provider`, this
 *         exact non-empty binding, the nonce and the expiry, and the expiry has
 *         not passed. A token issued under any other binding - including the
 *         empty one http_service_oauth_state_issue_1 uses - answers false, and
 *         so does an empty binding presented here, with no distinction from any
 *         other refusal. A caller that genuinely has no binding must call _1
 *         and accept what that means.
 */
bool http_service_oauth_state_verify_2(HTTP_Service_OAuth *const self, char const *const provider, char const *const binding, USize const binding_size, char const *const state);

/**
 * @brief Pre-tier spelling of http_service_oauth_state_verify_1.
 * @param self Service instance.
 * @param provider Provider name the state must be bound to.
 * @param state State token from the provider's redirect.
 * @return true only when the token verifies under the empty binding.
 * @deprecated Call http_service_oauth_state_verify_1 - or, for a login-CSRF
 *             defence, http_service_oauth_state_verify_2; retired before 1.0.
 */
bool http_service_oauth_state_verify(HTTP_Service_OAuth *const self, char const *const provider, char const *const state);

/**
 * @brief Whether a token exchange actually succeeded.
 * @param self Token response.
 * @return true when the transport completed, the provider answered 2xx, no
 *         error code was parsed, and an access token is present.
 */
bool http_service_oauth_token_is_ok(HTTP_Service_OAuth_Token const *const self);

/**
 * @brief Release token response storage.
 * @param self Token response.
 * @note Releases, does not WIPE: the credential bytes can outlive the free until
 *       CFW grows a memory-wiping primitive.
 */
void http_service_oauth_token_uninit(HTTP_Service_OAuth_Token *const self);

/**
 * @brief Release all provider storage.
 * @param self Service instance.
 */
void http_service_oauth_uninit(HTTP_Service_OAuth *const self);

#endif // HTTP_SERVICE_OAUTH_H