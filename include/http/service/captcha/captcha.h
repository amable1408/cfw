/*
 * captcha.h - HTTP CAPTCHA verification service for the C Libraries Framework
 * @version 0.3.1
 *
 * Verifies client CAPTCHA tokens against provider siteverify APIs while keeping
 * route policy, request parsing, and account storage outside the service.
 *
 * Features:
 *   - Provider-agnostic verification API.
 *   - Cloudflare Turnstile, hCaptcha, and Google reCAPTCHA support.
 *   - Configurable endpoint, secret, timeouts, TLS checks, and enabled state.
 *   - The provider's whole answer is parsed, not just `success`: error-codes,
 *     hostname, action, and score all land on the result.
 *   - Caller-settable EXPECTATIONS - expected hostname, expected action, and a
 *     minimum score - that a `success:true` answer must also satisfy.
 *   - A backend-independent HTTP_Service_Captcha_Status, so a consumer can tell
 *     a provider outage (answer 503) from a rejected token (answer 403), with
 *     http_service_captcha_status_is_outage answering that question once for
 *     every consumer instead of each one hand-rolling the set.
 *   - A STRICT provider-name parser, so a misspelled configuration value fails
 *     startup instead of silently disabling verification.
 *   - One HTTP client handle cached in the service and reused across
 *     verifications.
 *   - Heap and arena allocation support.
 *
 * Usage Examples:
 *   @code
 *   HTTP_Service_Captcha captcha = DEFAULT_INITIALIZATION;
 *   HTTP_Service_Captcha_Provider provider = HTTP_SERVICE_CAPTCHA_PROVIDER_NONE;
 *
 *   // A typo answers false here rather than quietly disabling verification.
 *   if (!http_service_captcha_provider_parse(provider_name, &provider)) {
 *       return false;
 *   }
 *
 *   if (!http_service_captcha_init_2(&captcha, provider, secret)) {
 *       // Memory Management: false means this instance does not exist - fail startup.
 *       return false;
 *   }
 *
 *   // Optional: a token minted for another site or another action no longer passes.
 *   http_service_captcha_set_expected_hostname(&captcha, "example.com");
 *   http_service_captcha_set_expected_action(&captcha, "register");
 *   http_service_captcha_set_min_score(&captcha, 0.5);  // reCAPTCHA v3 only
 *
 *   HTTP_Service_Captcha_Result result = http_service_captcha_verify(&captcha, token, ip);
 *
 *   if (!result.success) {
 *       // outage -> 503 with a retry hint; anything else -> 403.
 *       bool const outage = http_service_captcha_status_is_outage(result.status);
 *   }
 *
 *   http_service_captcha_result_uninit(&result);
 *   http_service_captcha_uninit(&captcha);
 *   @endcode
 *
 * Token lifetime:
 *   - A provider token is SINGLE USE: the provider consumes it on the first
 *     siteverify that reaches it. HTTP_SERVICE_CAPTCHA_STATUS_TIMEOUT can fire
 *     AFTER the request was already delivered (the total-timeout budget also
 *     covers waiting on the answer), so the token may already be spent. Never
 *     retry a verification on TIMEOUT with the same token - ask the client for a
 *     fresh one, or fail the request closed.
 *
 * Client address:
 *   - `remote_ip` must already be the resolved CLIENT address. Behind a reverse
 *     proxy the raw socket peer is the proxy (127.0.0.1 for a local Caddy), and
 *     the provider's own IP correlation is then worthless - it sees one address
 *     for every visitor. Resolve the real client through the server layer's
 *     trusted-hop X-Forwarded-For walk first, or pass "" and send no address at
 *     all rather than a misleading one.
 *
 * Error Handling:
 *   - Public functions validate non-null pointers through error_check_null,
 *     which LOGS AND ABORTS: those are caller contracts, not data.
 *   - Everything DERIVED FROM A REQUEST is a VALUE, refused rather than aborted.
 *     An empty token, a token longer than the configured cap, a malformed or
 *     truncated provider answer, and an unreachable provider all answer a result
 *     with success=false and a status naming the cause. Network-derived input
 *     must never be able to abort the server.
 *   - Configuration is not request data, so a genuinely nonsensical setting can
 *     abort: http_service_captcha_set_timeouts refuses a zero timeout that way.
 *   - Verification FAILS CLOSED. Enabled-but-invalid configuration, a transport
 *     failure, a non-2xx answer, an unparseable body, and a failed expectation
 *     all answer success=false; only a complete, satisfied provider answer is
 *     success=true. A DISABLED service answers success=true with status
 *     HTTP_SERVICE_CAPTCHA_STATUS_SKIPPED and provider_success LEFT FALSE: no
 *     provider spoke, so no provider value is fabricated.
 *
 * Thread Safety:
 *   - Not thread-safe. Caller must synchronize shared service objects.
 *     http_service_captcha_verify writes through `self` (it creates and reuses
 *     the cached client handle), so it is not safe to call concurrently on one
 *     instance even though it does not mutate the configuration.
 *
 * Memory Management:
 *   - Service owns copied strings and, after the first verification, one
 *     HTTP_Client handle. Call http_service_captcha_uninit() when finished.
 *   - Returned results own their strings; call http_service_captcha_result_uninit().
 *   - A false return from any init/alloc_init leaves *self ZEROED, not
 *     initialized: treat it as "this instance does not exist" and fail startup.
 *     Do not call any other function on it, uninit included.
 *   - The secret is freed but NOT wiped on uninit; a memory-wipe primitive is
 *     still an open framework item. It is also COPIED, percent-escaped, into the
 *     verification payload on every call: that copy lives on the heap (or in the
 *     arena) for the duration of the request and is released, unwiped, when the
 *     call returns. An arena-backed service therefore retains one payload copy
 *     of the secret per verification until the arena itself is reset.
 *   - result.response keeps the provider's RAW body. It can name configuration
 *     state ("invalid-input-secret" says the deployment's secret is wrong), so a
 *     consumer that logs it verbatim leaks that to its log reader. Log
 *     result.error_codes or result.status instead.
 *
 * Performance Characteristics:
 *   - One blocking HTTPS POST per verification, bounded by connect_timeout_ms
 *     and timeout_ms.
 *   - The client handle is created on the first verification and reused after
 *     that. Measured over 20 sequential loopback verifications, 7 interleaved
 *     rounds, median: 0.59 ms/call with a fresh handle per call against
 *     0.46 ms/call cached (-21%); the cached handle won every round, by 19% to
 *     76%. The loopback fixture answers Connection: close, so that delta is the
 *     handle-construction cost ALONE - against a real provider the reused
 *     connection also skips a TLS handshake, which this bench cannot measure.
 *   - The response is capped at HTTP_SERVICE_CAPTCHA_MAX_RESPONSE_SIZE, so a
 *     misrouted URL answering a web page fails fast instead of buffering it.
 *
 * Dependencies:
 *   - http_client, http_query, json, string.
 *
 * See captcha.c for implementation details.
 */
#ifndef HTTP_SERVICE_CAPTCHA_H
#define HTTP_SERVICE_CAPTCHA_H

#include <container/string/string.h>
#include <http/client/http_client.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

#define HTTP_SERVICE_CAPTCHA_DEFAULT_CONNECT_TIMEOUT_MS 3000
#define HTTP_SERVICE_CAPTCHA_DEFAULT_TIMEOUT_MS 5000
#define HTTP_SERVICE_CAPTCHA_HCAPTCHA_URL "https://api.hcaptcha.com/siteverify"

/**
 * @brief Largest provider answer this service will buffer, in bytes.
 * @note Unlike the token cap there is no per-instance setter: the response is
 *       the PROVIDER's, not a client's, so one deployment-wide ceiling is the
 *       whole knob. Override it with -D at build time when a provider grows a
 *       genuinely larger siteverify body.
 */
#ifndef HTTP_SERVICE_CAPTCHA_MAX_RESPONSE_SIZE
#define HTTP_SERVICE_CAPTCHA_MAX_RESPONSE_SIZE 65536
#endif // HTTP_SERVICE_CAPTCHA_MAX_RESPONSE_SIZE

#define HTTP_SERVICE_CAPTCHA_RECAPTCHA_URL "https://www.google.com/recaptcha/api/siteverify"

/**
 * @brief Default cap on a client token, in bytes.
 * @note Per-instance overridable through http_service_captcha_set_token_max_size;
 *       this define only moves the default a constructor installs.
 */
#ifndef HTTP_SERVICE_CAPTCHA_TOKEN_MAX_SIZE
#define HTTP_SERVICE_CAPTCHA_TOKEN_MAX_SIZE 4096
#endif // HTTP_SERVICE_CAPTCHA_TOKEN_MAX_SIZE

#define HTTP_SERVICE_CAPTCHA_TURNSTILE_URL "https://challenges.cloudflare.com/turnstile/v0/siteverify"

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief CAPTCHA provider backend.
 */
typedef enum {
    /** @brief CAPTCHA verification disabled. */
    HTTP_SERVICE_CAPTCHA_PROVIDER_NONE,
    /** @brief Cloudflare Turnstile siteverify API. */
    HTTP_SERVICE_CAPTCHA_PROVIDER_TURNSTILE,
    /** @brief hCaptcha siteverify API. */
    HTTP_SERVICE_CAPTCHA_PROVIDER_HCAPTCHA,
    /** @brief Google reCAPTCHA siteverify API. */
    HTTP_SERVICE_CAPTCHA_PROVIDER_RECAPTCHA
} HTTP_Service_Captcha_Provider;

/**
 * @brief Backend-independent outcome of one verification.
 *
 * Callers branch on this instead of on `code`. The distinction that matters at
 * the route: a rejected token is the client's problem (403), while UNREACHABLE,
 * TIMEOUT, TLS, PROVIDER_ERROR, MALFORMED_RESPONSE, MISCONFIGURED and ERROR are
 * the provider's or the deployment's (503 with a retry hint). All of them still
 * fail closed. Ask http_service_captcha_status_is_outage rather than spelling
 * that set out at the call site.
 */
typedef enum {
    /** @brief Any other failure, including a payload that could not be encoded.
     *         Also the zero value, so a zeroed result is never mistaken for OK. */
    HTTP_SERVICE_CAPTCHA_STATUS_ERROR,
    /** @brief The provider answered 2xx with a body this module could not parse
     *         as a siteverify object. Fails closed. */
    HTTP_SERVICE_CAPTCHA_STATUS_MALFORMED_RESPONSE,
    /** @brief Verification is enabled but the configuration cannot verify
     *         anything (no secret, no provider, an http:// endpoint with TLS
     *         verification on). No request was made. */
    HTTP_SERVICE_CAPTCHA_STATUS_MISCONFIGURED,
    /** @brief The provider accepted the token and every configured expectation
     *         held. The only status with success=true besides SKIPPED. */
    HTTP_SERVICE_CAPTCHA_STATUS_OK,
    /** @brief The transport succeeded but the provider answered a non-2xx
     *         status: an outage or a rate limit, not a verdict on the token. */
    HTTP_SERVICE_CAPTCHA_STATUS_PROVIDER_ERROR,
    /** @brief Verification is disabled; no provider was contacted and the token
     *         was not inspected. success is true, provider_success is false. */
    HTTP_SERVICE_CAPTCHA_STATUS_SKIPPED,
    /** @brief The connect or total timeout elapsed. May have fired AFTER the
     *         provider consumed the token - see the token-lifetime note above. */
    HTTP_SERVICE_CAPTCHA_STATUS_TIMEOUT,
    /** @brief TLS handshake or certificate verification failure. */
    HTTP_SERVICE_CAPTCHA_STATUS_TLS,
    /** @brief The token was empty. No request was made. */
    HTTP_SERVICE_CAPTCHA_STATUS_TOKEN_INVALID,
    /** @brief The provider answered success=false, or a configured expectation
     *         (hostname, action, minimum score) did not hold. `error` names
     *         which, and `error_codes` carries the provider's own codes. */
    HTTP_SERVICE_CAPTCHA_STATUS_TOKEN_REJECTED,
    /** @brief The token exceeded the configured cap. No request was made, so a
     *         client cannot make this service POST an arbitrary payload. */
    HTTP_SERVICE_CAPTCHA_STATUS_TOKEN_TOO_LONG,
    /** @brief The provider was never reached: connection refused, or DNS
     *         resolution failure. */
    HTTP_SERVICE_CAPTCHA_STATUS_UNREACHABLE
} HTTP_Service_Captcha_Status;

/**
 * @brief CAPTCHA verification service configuration.
 */
typedef struct {
#ifdef ARENA_IMPLEMENTATION
    /** @brief Optional arena used by owned service strings. */
    Arena *allocator;
#endif // ARENA_IMPLEMENTATION
    /** @brief Reused client handle; null until the first verification. Never an
     *         arena allocation - http/client owns its own storage. */
    HTTP_Client *client;
    /** @brief Provider connection timeout in milliseconds. */
    USize connect_timeout_ms;
    /** @brief Whether verification is active. Disabled verification passes. */
    bool enabled;
    /** @brief Required `action` in the provider answer; empty disables the check. */
    String expected_action;
    /** @brief Required `hostname` in the provider answer; empty disables the check. */
    String expected_hostname;
    /** @brief Minimum accepted `score`; 0 or less disables the check. */
    FSize min_score;
    /** @brief Active provider backend. */
    HTTP_Service_Captcha_Provider provider;
    /** @brief Provider secret key. */
    String secret;
    /** @brief Provider total request timeout in milliseconds. */
    USize timeout_ms;
    /** @brief Largest token this service will send; 0 disables the cap. */
    USize token_max_size;
    /** @brief Whether TLS peer and host verification are enabled. */
    bool verify_tls;
    /** @brief Provider verification endpoint. */
    String verify_url;
} HTTP_Service_Captcha;

/**
 * @brief CAPTCHA verification result.
 */
typedef struct {
#ifdef ARENA_IMPLEMENTATION
    /** @brief Optional arena used by result strings. */
    Arena *allocator;
#endif // ARENA_IMPLEMENTATION
    /** @brief Provider `action` from the response JSON; empty when absent. */
    String action;
    /** @brief Transport outcome from http/client. Diagnostics for a caller that
     *         wants the client's own vocabulary; branch on `status` instead. */
    HTTP_Client_Status client_status;
    /** @brief Backend transport code from http/client. For logs only. */
    I32 code;
    /** @brief Transport, configuration, or expectation error text. */
    String error;
    /** @brief Provider `error-codes` joined by ",". Empty when the array is
     *         absent or empty. May name configuration state - see the header's
     *         Memory Management note before logging it. */
    String error_codes;
    /** @brief Provider `hostname` from the response JSON; empty when absent. */
    String hostname;
    /** @brief Raw provider `success` value from the response JSON. Left false
     *         when no provider answered, disabled verification included. */
    bool provider_success;
    /** @brief Raw provider response body. May name configuration state - see
     *         the header's Memory Management note before logging it. */
    String response;
    /** @brief HTTP response status code from provider. */
    USize response_code;
    /** @brief Provider `score` from the response JSON; 0 when absent. */
    FSize score;
    /** @brief Backend-independent outcome; the field callers should branch on. */
    HTTP_Service_Captcha_Status status;
    /** @brief Whether transport, HTTP status, provider validation, and every
     *         configured expectation all passed. */
    bool success;
} HTTP_Service_Captcha_Result;

/*==============================================================================
 * MARK: - API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Initialize an arena-backed disabled CAPTCHA service.
 * @param self Service instance to initialize in place.
 * @param allocator Arena allocator.
 * @return true when initialized; false leaves *self zeroed and unusable.
 * @note Writes through self rather than returning by value: the service owns a
 *       client handle, so a copied instance would double-delete it.
 */
bool http_service_captcha_alloc_init_1(HTTP_Service_Captcha *const self, Arena *const allocator);

/**
 * @brief Initialize an arena-backed CAPTCHA service with provider and secret.
 * @param self Service instance to initialize in place.
 * @param provider Provider backend.
 * @param secret Provider secret key.
 * @param allocator Arena allocator.
 * @return true when initialized; see http_service_captcha_alloc_init_1 for the false case.
 */
bool http_service_captcha_alloc_init_2(HTTP_Service_Captcha *const self, HTTP_Service_Captcha_Provider const provider, char const *const secret, Arena *const allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Check whether CAPTCHA verification is enabled.
 * @param self Service instance.
 * @return true when verification is active.
 */
bool http_service_captcha_enabled(HTTP_Service_Captcha const *const self);

/**
 * @brief Initialize a disabled CAPTCHA service.
 * @param self Service instance to initialize in place.
 * @return true when initialized; see http_service_captcha_alloc_init_1 for the false case.
 * @note Writes through self; see http_service_captcha_alloc_init_1 for why it cannot
 *       return the service by value.
 */
bool http_service_captcha_init_1(HTTP_Service_Captcha *const self);

/**
 * @brief Initialize CAPTCHA service with provider and secret.
 * @param self Service instance to initialize in place.
 * @param provider Provider backend.
 * @param secret Provider secret key.
 * @return true when initialized; see http_service_captcha_alloc_init_1 for the false case.
 */
bool http_service_captcha_init_2(HTTP_Service_Captcha *const self, HTTP_Service_Captcha_Provider const provider, char const *const secret);

/**
 * @brief LENIENT wrapper over http_service_captcha_provider_parse: an
 *        unrecognized name is folded into NONE instead of being reported.
 *        Prefer the parser - a misspelled provider name reaching this function
 *        DISABLES verification, which is indistinguishable from an unset
 *        variable. ASCII case-insensitive.
 * @param data Provider name: "turnstile", "hcaptcha", or "recaptcha". Every
 *        other spelling - a deliberate "none" or "off", a null, an empty
 *        string, an unrecognized name - answers NONE. This function never
 *        guesses a provider, so a missing environment variable cannot silently
 *        arm a backend the deployment has no secret for; consumers that want a
 *        default apply it themselves.
 * @return Provider enum value.
 */
HTTP_Service_Captcha_Provider http_service_captcha_provider_from_string(char const *const data);

/**
 * @brief Parse provider name into provider enum, reporting an unrecognized one.
 *        ASCII case-insensitive.
 * @param data Provider name. "turnstile", "hcaptcha" and "recaptcha" name a
 *        backend; null, "", "none" and "off" all mean DISABLED and are accepted
 *        as such. Anything else is a configuration typo, not a request value.
 * @param provider_out Where the parsed provider is written. Untouched when this
 *        function answers false, so a caller that pre-set a default keeps it.
 * @return true when data named a provider or an explicit disable; false for an
 *         unrecognized name. Fail startup on false: "turnstlie" would otherwise
 *         build a disabled service whose every verification silently passes.
 */
bool http_service_captcha_provider_parse(char const *const data, HTTP_Service_Captcha_Provider *const provider_out);

/**
 * @brief Get default siteverify URL for a provider.
 * @param provider Provider backend.
 * @return Static endpoint URL, or "" for HTTP_SERVICE_CAPTCHA_PROVIDER_NONE (a
 *         service constructed with NONE therefore copies an empty verify_url and
 *         is not valid() until one is set). Treat returned memory as read-only.
 */
char* http_service_captcha_provider_url(HTTP_Service_Captcha_Provider const provider);

/**
 * @brief Release result storage.
 * @param self Result instance.
 */
void http_service_captcha_result_uninit(HTTP_Service_Captcha_Result *const self);

/**
 * @brief Set whether verification is enabled.
 * @param self Service instance.
 * @param enabled true to verify tokens.
 */
void http_service_captcha_set_enabled(HTTP_Service_Captcha *const self, bool const enabled);

/**
 * @brief Require the provider answer's `action` to equal this value. Comparison
 *        is byte-exact: a provider action is an application-chosen label, not a
 *        DNS name.
 * @param self Service instance.
 * @param action Expected action. Empty disables the check.
 * @return true when the expectation was stored. false means an allocation was
 *         refused and the expectation is NOT installed - the service would then
 *         accept a token minted for any action. Check it at startup:
 *         http_service_captcha_valid cannot see a missing expectation, because
 *         an absent one is also the legitimate "no action check" configuration.
 */
bool http_service_captcha_set_expected_action(HTTP_Service_Captcha *const self, char const *const action);

/**
 * @brief Require the provider answer's `hostname` to equal this value.
 *        Without it, a token minted for ANY site key under the same secret
 *        verifies here. Comparison is ASCII case-INSENSITIVE, because a DNS
 *        name is: a provider answering "Example.com" satisfies "example.com".
 * @param self Service instance.
 * @param hostname Expected hostname. Empty disables the check.
 * @return true when the expectation was stored; false leaves it NOT installed -
 *         see http_service_captcha_set_expected_action for what that costs.
 */
bool http_service_captcha_set_expected_hostname(HTTP_Service_Captcha *const self, char const *const hostname);

/**
 * @brief Require the provider answer's `score` to be at least this value.
 *        reCAPTCHA v3 answers success=true with a low score for a bot, so
 *        without a minimum this service accepts it.
 * @param self Service instance.
 * @param min_score Minimum accepted score. 0 or less disables the check. A
 *        provider that reports no score at all fails a check set above 0.
 */
void http_service_captcha_set_min_score(HTTP_Service_Captcha *const self, FSize const min_score);

/**
 * @brief Set provider and matching default verification URL. This OVERWRITES a
 *        verification endpoint installed by http_service_captcha_set_url, so
 *        configure the provider first and the endpoint override second.
 * @param self Service instance.
 * @param provider Provider backend.
 * @return true when the provider was stored. A value outside the enum - which
 *         is what an environment string cast to the type can be - is REFUSED
 *         (false) and the instance is left entirely untouched, matching what
 *         the constructors do rather than storing a value whose endpoint switch
 *         would fall through to "" one request later.
 * @note It also writes `enabled`, exactly as the constructors do:
 *       HTTP_SERVICE_CAPTCHA_PROVIDER_NONE turns verification OFF and every
 *       other provider turns it on. A configuration path that can reach NONE -
 *       a "none" or "off" value, a parser result left at its default - therefore
 *       needs the SAME startup gate an unset variable does, because this setter
 *       disables verification without saying so. Gating on
 *       http_service_captcha_valid alone is not that gate: valid() short-circuits
 *       on `enabled`, so a service this call quietly turned off never reaches the
 *       configuration checks. Ask http_service_captcha_enabled as well, and
 *       decide whether a disabled service is what the deployment meant.
 */
bool http_service_captcha_set_provider(HTTP_Service_Captcha *const self, HTTP_Service_Captcha_Provider const provider);

/**
 * @brief Set provider secret key.
 * @param self Service instance.
 * @param secret Provider secret key.
 */
void http_service_captcha_set_secret(HTTP_Service_Captcha *const self, char const *const secret);

/**
 * @brief Set provider request timeouts.
 * @param self Service instance.
 * @param connect_timeout_ms Connection timeout in milliseconds. Zero ABORTS:
 *        this is configuration, not request data.
 * @param timeout_ms Total request timeout in milliseconds. Zero ABORTS.
 */
void http_service_captcha_set_timeouts(HTTP_Service_Captcha *const self, USize const connect_timeout_ms, USize const timeout_ms);

/**
 * @brief Override the largest token this service will send.
 * @param self Service instance.
 * @param token_max_size Cap in bytes, or 0 to disable the cap. Defaults to
 *        HTTP_SERVICE_CAPTCHA_TOKEN_MAX_SIZE; every documented provider token is
 *        well under it. Disabling it lets a client drive the size of a POST this
 *        service makes to a third party.
 */
void http_service_captcha_set_token_max_size(HTTP_Service_Captcha *const self, USize const token_max_size);

/**
 * @brief Override provider verification endpoint.
 * @param self Service instance.
 * @param url Verification endpoint URL.
 * @return true when the URL was accepted. An empty URL, or a non-"https://" URL
 *         while verify_tls is on, is REFUSED (false) and verify_url is left
 *         unchanged: the secret is the deployment's only credential and an
 *         http:// typo would put it on the wire in clear text. To point this
 *         service at a plaintext endpoint deliberately - a loopback test server -
 *         call http_service_captcha_set_verify_tls(self, false) FIRST.
 * @note A later http_service_captcha_set_provider REVERTS this override to that
 *       provider's default endpoint. Configuring from environment variables in
 *       provider-then-url order keeps the override.
 */
bool http_service_captcha_set_url(HTTP_Service_Captcha *const self, char const *const url);

/**
 * @brief Set TLS verification behavior.
 * @param self Service instance.
 * @param verify_tls true to verify peer and host.
 */
void http_service_captcha_set_verify_tls(HTTP_Service_Captcha *const self, bool const verify_tls);

/**
 * @brief Whether a status names an OUTAGE - the provider's or the deployment's -
 *        rather than a verdict on the client's token. The classification the
 *        route needs: an outage is a 503 with a retry hint, everything else a
 *        403. Ask here instead of hand-rolling the set, so a status added later
 *        does not silently reclassify itself in every consumer.
 * @param status Status from a verification result.
 * @return true for ERROR, MALFORMED_RESPONSE, MISCONFIGURED, PROVIDER_ERROR,
 *         TIMEOUT, TLS, and UNREACHABLE. false for OK, SKIPPED, and the three
 *         TOKEN_* verdicts. Both answers still fail closed - this only says who
 *         to blame, never whether to let the request through.
 */
bool http_service_captcha_status_is_outage(HTTP_Service_Captcha_Status const status);

/**
 * @brief Release service storage, the cached client handle included.
 * @param self Service instance.
 */
void http_service_captcha_uninit(HTTP_Service_Captcha *const self);

/**
 * @brief Validate service configuration for active verification. Call it at
 *        startup: an enabled service that answers false here rejects every
 *        request it ever sees, and says so only when the first user arrives.
 * @param self Service instance.
 * @return true when enabled configuration can verify tokens: a real provider, a
 *         non-empty secret, and an endpoint that is https:// unless TLS
 *         verification has been deliberately turned off.
 * @note A DISABLED service answers false here without any of that being looked
 *       at, so this function alone cannot tell "misconfigured" from
 *       "deliberately off". The startup gate that reads `enabled() && !valid()`
 *       therefore passes a service http_service_captcha_set_provider disabled by
 *       being handed HTTP_SERVICE_CAPTCHA_PROVIDER_NONE - see that setter's note.
 *       Check http_service_captcha_enabled on its own too.
 * @note The timeouts are deliberately NOT re-checked here, and a zero one cannot
 *       reach this function anyway: http_service_captcha_set_timeouts ABORTS on
 *       zero and every constructor installs a non-zero default.
 */
bool http_service_captcha_valid(HTTP_Service_Captcha const *const self);

/**
 * @brief Verify a CAPTCHA token against the configured provider.
 * @param self Service instance. Written through: the cached client handle is
 *        created here on first use, so this is not a read-only operation and two
 *        threads must not share one instance across it.
 * @param token Client token to verify. Null or empty is REFUSED as TOKEN_INVALID and
 *        one longer than the configured cap as TOKEN_TOO_LONG, both before any request
 *        is built - never an abort, since the token is request data.
 * @param remote_ip Optional remote IP sent to provider. Null or empty sends no
 *        address field at all rather than an empty one. It must ALREADY be the
 *        resolved CLIENT address: behind a reverse proxy the raw socket peer is
 *        the proxy, and the provider then correlates every visitor to one
 *        address - see the header's Client address note.
 * @return Verification result. Caller must uninitialize it.
 */
HTTP_Service_Captcha_Result http_service_captcha_verify(HTTP_Service_Captcha *const self, char const *const token, char const *const remote_ip);

#endif // HTTP_SERVICE_CAPTCHA_H