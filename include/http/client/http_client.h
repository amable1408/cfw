/* ============================================================================
 *  CFW — HTTP Client (libcurl)
 *  --------------------------------------------------------------------------
 *  @file    http_client.h
 *  @brief   Blocking HTTP client wrapper over libcurl, behind an opaque, reusable handle.
 *  @version 0.3.0
 *  @license MIT (see LICENSE file)
 *
 *  General-purpose (no project coupling). Wraps one libcurl easy handle per
 *  client: every request is blocking, answers its outcome as data rather than
 *  aborting, and keeps its response state on the handle that made it. URL
 *  escaping is delegated to http/query, so the tree carries one percent-encoder
 *  rather than a second copy behind curl_easy_escape.
 *
 * Features:
 *   - Blocking GET / POST / PUT / DELETE / HEAD over an opaque, reusable handle
 *   - TLS peer + host verification ON by default (CA bundle/path setters for a
 *     non-default trust store); redirects OFF by default with an explicit setter
 *   - Every request answers an HTTP_Client_Result by value - a transport failure
 *     (timeout, DNS, TLS, reset) is DATA, never a process abort
 *   - Response body and header size caps (16 MiB body default, a fixed header
 *     budget) so a hostile or misbehaving server cannot exhaust memory
 *   - Per-handle request headers and per-handle last-response headers/status -
 *     no state shared between two clients on the same thread
 *   - URL escaping (char* or String), percent-encoded by http/query - one
 *     encoder in the tree, not a second copy behind curl_easy_escape
 *
 * Usage Examples:
 *   @code
 *   HTTP_Client *client = http_client_new();
 *   String response = string_init_1();
 *   HTTP_Client_Result result = http_client_get(client, "https://example.com", &response);
 *
 *   if (http_client_result_is_ok(&result)) {
 *       // Use string_get_data(&response) / string_get_size(&response) ...
 *   }
 *
 *   http_client_result_uninit(&result);
 *   string_uninit(&response);
 *   http_client_delete(&client);
 *   @endcode
 *
 * Error Handling:
 *   Required pointer arguments are checked first and abort via error_check_null when
 *   ERROR_CHECK_ENABLED is defined (the CFW default); a null argument is a contract
 *   violation, not a value. With ERROR_CHECK_ENABLED undefined the checks compile out
 *   and passing null is undefined behavior. http_client_new never returns null
 *   (curl_easy_init failure aborts). With ERROR_CHECK_ENABLED undefined, a null
 *   curl_easy_init result instead survives into self->curl; libcurl's own
 *   handle-validity check then rejects every curl_easy_setopt/perform/getinfo
 *   call on it with CURLE_BAD_FUNCTION_ARGUMENT (verified: it does not crash),
 *   which _http_client_status_from_code's default case degrades to
 *   HTTP_CLIENT_STATUS_ERROR with `success` false on every request. Transport
 *   failures never abort: they are
 *   reported through the HTTP_Client_Result returned by value - branch on `status`
 *   (or the convenience http_client_result_is_ok), treat `code` (the backend
 *   CURLcode) as diagnostics only. A malformed URL or an oversized response/header
 *   set is also reported through the Result, never aborted.
 *
 * Thread Safety:
 *   One client per thread. A HTTP_Client owns its own request headers, its own
 *   last-response headers, and its own response-body scratch buffer - nothing is
 *   shared between two clients on the same thread or across threads. Caller must
 *   still synchronize if a single handle is shared across threads.
 *
 * Memory Management:
 *   Create a client with http_client_new and release it with http_client_delete.
 *   Each request answers an owned-error HTTP_Client_Result; release it with
 *   http_client_result_uninit. A request writes its body into the caller-owned
 *   `response` String when one is passed, or into the handle's own body (read
 *   back with http_client_response_body) when `response` is nullptr - that body
 *   is valid until the next request on the same handle or until the handle is
 *   deleted. The request header list (http_client_header_add) and the last
 *   response's header list are owned by the handle and released by
 *   http_client_header_clear / http_client_delete.
 *
 * Defaults:
 *   TLS peer + host verification ON; no CA override (system trust store); redirects
 *   OFF (http_client_set_follow_redirects to opt in; only http/https ever followed);
 *   curl-managed credentials never replay across a redirect (CURLOPT_UNRESTRICTED_AUTH
 *   off); connect timeout 2 s, total timeout 20 s; response body cap 16 MiB; response
 *   header budget bounded (256 lines, 256 KiB total); Accept-Encoding auto
 *   (bodies arrive decoded, Content-Encoding still visible via header_find); HTTP/2
 *   over TLS falls back to 1.1 (HTTP/3 is never attempted - see the compatibility
 *   note below); HTTP auth method ANY; no cookie jar unless the caller manages
 *   cookies itself via http_client_get_handle; User-Agent "cfw-http-client/<linked
 *   libcurl version>" (override with http_client_set_user_agent).
 *
 * Redirects and credentials:
 *   A custom header added through http_client_header_add (including a hand-rolled
 *   `Authorization:` line) is re-sent by libcurl on EVERY redirect hop, cross-host
 *   included - this module cannot filter that. Disable redirects
 *   (http_client_set_follow_redirects(self, false, 0), the default) for any request
 *   carrying a credential added as a custom header.
 *
 * curl_global_init:
 *   http_client_new runs curl_global_init(CURL_GLOBAL_ALL) exactly once per process
 *   via pthread_once; http_client_global_uninit balances THIS module's init only.
 *   libcurl >= 7.84 makes curl_global_init reference-counted and thread-safe when
 *   CURL_VERSION_THREADSAFE is set (true for this tree's linked 8.x builds), so an
 *   owning program's own curl_global_init/curl_global_cleanup around this module's
 *   (or websocket/client's) is compatible - see websocket_client.h for the sibling
 *   module, which never calls curl_global_init itself and expects the OWNING PROGRAM
 *   (or this module's once) to have done so first.
 *
 * Compatibility:
 *   Requires libcurl >= 7.85.0 at compile time (enforced below with #error):
 *   CURLOPT_REDIR_PROTOCOLS_STR, used to fence redirects to http/https, was added
 *   in 7.85. websocket/client requires 8.13; a program linking both needs 8.13.
 *
 * Performance Characteristics:
 *   - One blocking curl_easy_perform per request; no connection pooling beyond
 *     what libcurl's own easy handle reuses internally for the same handle.
 *   - The response-body and response-header write callbacks copy every received
 *     chunk once into the target String / response_headers list - O(n) in the
 *     transferred size, bounded by max_response_size and the fixed header budget.
 *   - curl_easy_setopt's return value is not checked at any of this module's own
 *     call sites: every option it sets is a fixed, compile-time-valid one (never
 *     a value libcurl could reject), so the only failure that setopt call can
 *     report is "unsupported by this build", not a bad argument.
 *   - http_client_get_handle's raw-handle escape hatch costs nothing extra: the
 *     next call through this module's own API (http_client_get/post/put/etc.)
 *     re-installs CURLOPT_WRITEFUNCTION/WRITEDATA, HEADERFUNCTION/HEADERDATA and
 *     ERRORBUFFER unconditionally before curl_easy_perform, so a raw-handle
 *     consumer's override never leaks into a later request on the same handle.
 *
 * Dependencies:
 *   - <curl/curl.h>, <pthread.h>, <container/string/string.h>,
 *     <http/query/query.h>
 *   - <http/query/query.h>: the escape family's percent-encoder. Included here
 *     rather than in the .c per the tree's "includes live in the header"
 *     convention; it pulls only arena/str/string, so no cycle and no new
 *     third-party dependency. Every makefile that compiles http_client.c must
 *     also compile include/http/query/query.c.
 *   - <pthread.h>: pthread_once/pthread_once_t gate the process-wide
 *     curl_global_init in the .c. Needed on both platforms this tree builds
 *     for - MinGW ships a full winpthreads-backed <pthread.h> on Windows,
 *     glibc's is native on Linux - so unlike thread/thread.h (which drops to
 *     Win32 primitives on OS_WINDOWS) this module takes the same header on
 *     every target. Not exposed by this header: no public type here names a
 *     pthread type; it is included here (not the .c) per the tree's "includes
 *     live in the header" convention.
 *
 * See http_client.c for implementation details.
 * ============================================================================
 */
#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <curl/curl.h>
#include <pthread.h>

#include <container/string/string.h>
#include <http/query/query.h>

#if !CURL_AT_LEAST_VERSION(7, 85, 0)
#error "http/client requires libcurl >= 7.85 (CURLOPT_REDIR_PROTOCOLS_STR)"
#endif // !CURL_AT_LEAST_VERSION(7, 85, 0)

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief HTTP client handle. Opaque: defined only in http_client.c.
 */
typedef struct HTTP_Client HTTP_Client;

/**
 * @brief Backend-independent outcome of an HTTP request.
 *
 * Callers branch on this instead of on the backend's numeric code: a caller
 * that wants to retry a timeout apart from giving up on a TLS failure needs
 * that distinction without comparing raw CURLcodes.
 */
typedef enum {
    /** @brief Any other failure (malformed URL, response/header cap exceeded,
     *         protocol error). */
    HTTP_CLIENT_STATUS_ERROR,
    /** @brief The transport completed (the server may still have answered with
     *         a 4xx/5xx - check response_code / http_client_result_is_ok). */
    HTTP_CLIENT_STATUS_OK,
    /** @brief The total or connect timeout elapsed. Honest caveat: this can
     *         fire AFTER the request was already delivered to the peer (the
     *         total-timeout budget also covers waiting on the response body) -
     *         response_code may be non-zero. Not safe to blindly retry a
     *         non-idempotent request on this status alone; check response_code
     *         first. */
    HTTP_CLIENT_STATUS_TIMEOUT,
    /** @brief TLS handshake or certificate verification failure. */
    HTTP_CLIENT_STATUS_TLS,
    /** @brief The peer was never reached: connection refused, or DNS resolution
     *         failure (host or proxy). A connect attempt that instead ran out
     *         the connect/total timeout budget is HTTP_CLIENT_STATUS_TIMEOUT,
     *         not this. */
    HTTP_CLIENT_STATUS_UNREACHABLE
} HTTP_Client_Status;

/**
 * @brief HTTP request execution result.
 */
typedef struct {
#ifdef ARENA_IMPLEMENTATION
    /** @brief Optional arena used by the owned error string; taken from the
     *         `response` String passed to the request, when it has one. */
    Arena *allocator;
#endif // ARENA_IMPLEMENTATION
    /** @brief Transport status code from the HTTP backend. For diagnostics and
     *         logs only - branch on `status` or http_client_result_is_ok instead. */
    CURLcode code;
    /** @brief Transport error text; empty when `success` is true. */
    String error;
    /** @brief HTTP status code returned by the server, or 0 when none was received. */
    USize response_code;
    /** @brief Backend-independent outcome; the field callers should branch on. */
    HTTP_Client_Status status;
    /** @brief Whether the request reached the server and got a response without
     *         transport failure. Transport-only: a 404 or 500 is `success` true.
     *         Use http_client_result_is_ok to also require a 2xx response_code. */
    bool success;
} HTTP_Client_Result;

/*==============================================================================
 * MARK: - API
 *============================================================================*/

/**
 * @brief Free and cleanup an HTTP client handle. Touches nothing shared: only
 *        this handle's own request headers, response headers, and body are
 *        released.
 * @param self Pointer to HTTP_Client pointer.
 */
void http_client_delete(HTTP_Client **const self);

/**
 * @brief URL-encode raw string data per RFC 3986 (unreserved set through,
 *        every other byte "%XX" with uppercase hex, a space "%20"). Encoding is
 *        done by http_query_encode_2, not curl_easy_escape - byte-for-byte the
 *        same output, one fewer allocation and copy. `self` is accepted for a
 *        stable API across the escape family but not consulted (curl_easy_escape
 *        never consulted its handle parameter either); it is still null-CHECKED,
 *        so passing nullptr aborts rather than being quietly ignored, and it
 *        retires at this family's next MAJOR.
 * @param self HTTP client handle: must be non-null (checked), though unused.
 * @param data Raw string data.
 * @return Encoded String. Empty input and input longer than
 *         HTTP_QUERY_ENCODE_MAX_SIZE (1 MiB) both answer an empty String rather
 *         than aborting; the oversized case is logged at WARN, since the two
 *         are otherwise indistinguishable at the call site.
 */
String http_client_escape_1(HTTP_Client const *const self, char const *const data);

/**
 * @brief URL-encode String data. See http_client_escape_1 for the `self` note
 *        and the encoder behind both.
 * @param self HTTP client handle: must be non-null (checked), though unused.
 * @param data Raw String data.
 * @return Encoded String. Empty input and input longer than
 *         HTTP_QUERY_ENCODE_MAX_SIZE (1 MiB) both answer an empty String rather
 *         than aborting; the oversized case is logged at WARN, since the two
 *         are otherwise indistinguishable at the call site.
 */
String http_client_escape_3(HTTP_Client const *const self, String const *const data);

/**
 * @brief Perform an HTTP GET request.
 * @param self HTTP client handle.
 * @param url Request URL.
 * @param response Response body destination, or nullptr to use the handle's own
 *        body (read back with http_client_response_body).
 * @return Request result.
 */
HTTP_Client_Result http_client_get(HTTP_Client *const self, char const *const url, String *const response);

/**
 * @brief Escape hatch to the underlying libcurl easy handle, for a caller that
 *        must drive curl_easy_perform itself (e.g. a custom write callback that
 *        would conflict with this module's own). Reserved options this module
 *        manages - CURLOPT_WRITEFUNCTION/WRITEDATA, CURLOPT_HEADERFUNCTION/
 *        HEADERDATA, CURLOPT_ERRORBUFFER - are restored by the next call made
 *        through this module's own API, so a caller that overrides them should
 *        restore them (or stop calling this module's request functions on this
 *        handle) when done.
 * @param self HTTP client handle.
 * @return The backing CURL easy handle. Never null.
 */
CURL* http_client_get_handle(HTTP_Client *const self);

/**
 * @brief Escape hatch to the handle's currently installed request header list,
 *        for a caller driving curl_easy_perform itself via http_client_get_handle
 *        (e.g. CURLOPT_HTTPHEADER on that raw handle).
 * @param self HTTP client handle.
 * @return The request header list installed by http_client_header_add, or
 *         nullptr when empty. Owned by `self`; do not free.
 */
struct curl_slist* http_client_get_headers(HTTP_Client const *const self);

/**
 * @brief Release process-global HTTP client state (curl global + cached user-agent).
 *
 * Call once at process shutdown, after every client has been deleted. Creating new
 * clients afterward is unsupported: the one-time global init does not run again.
 */
void http_client_global_uninit(void);

/**
 * @brief Perform an HTTP HEAD request (no response body; CURLOPT_NOBODY).
 * @param self HTTP client handle.
 * @param url Request URL.
 * @param response Response body destination (typically stays empty), or
 *        nullptr to use the handle's own body.
 * @return Request result.
 */
HTTP_Client_Result http_client_head(HTTP_Client *const self, char const *const url, String *const response);

/**
 * @brief Add a request header to this handle's header list.
 * @param self HTTP client handle.
 * @param header Full header line ("Name: value").
 */
void http_client_header_add(HTTP_Client *const self, char const *const header);

/**
 * @brief Clear this handle's request header list.
 * @param self HTTP client handle.
 */
void http_client_header_clear(HTTP_Client *const self);

/**
 * @brief Create and initialize a new HTTP client handle. Touches nothing shared
 *        beyond the process-global curl_global_init (once per process).
 * @return Pointer to new HTTP_Client. Never null (curl_easy_init failure aborts).
 */
HTTP_Client* http_client_new(void);

/**
 * @brief Perform POST with null-terminated payload.
 * @param self HTTP client handle.
 * @param url Request URL.
 * @param payload Request body.
 * @param response Response body destination, or nullptr to use the handle's own body.
 * @return Request result.
 */
HTTP_Client_Result http_client_post_1(HTTP_Client *const self, char const *const url, char const *const payload, String *const response);

/**
 * @brief Perform POST with sized payload (may contain embedded NUL bytes).
 * @param self HTTP client handle.
 * @param url Request URL.
 * @param payload Request body.
 * @param payload_size Request body size.
 * @param response Response body destination, or nullptr to use the handle's own body.
 * @return Request result.
 */
HTTP_Client_Result http_client_post_2(HTTP_Client *const self, char const *const url, char const *const payload, USize const payload_size, String *const response);

/**
 * @brief Perform POST with Str payload.
 * @param self HTTP client handle.
 * @param url Request URL.
 * @param payload Request body.
 * @param response Response body destination, or nullptr to use the handle's own body.
 * @return Request result.
 */
HTTP_Client_Result http_client_post_3(HTTP_Client *const self, char const *const url, Str const *const payload, String *const response);

/**
 * @brief Perform POST with String payload.
 * @param self HTTP client handle.
 * @param url Request URL.
 * @param payload Request body.
 * @param response Response body destination, or nullptr to use the handle's own body.
 * @return Request result.
 */
HTTP_Client_Result http_client_post_4(HTTP_Client *const self, char const *const url, String const *const payload, String *const response);

/**
 * @brief Perform PUT with null-terminated payload.
 * @param self HTTP client handle.
 * @param url Request URL.
 * @param payload Request body.
 * @param response Response body destination, or nullptr to use the handle's own body.
 * @return Request result.
 */
HTTP_Client_Result http_client_put_1(HTTP_Client *const self, char const *const url, char const *const payload, String *const response);

/**
 * @brief Perform PUT with sized payload (may contain embedded NUL bytes).
 * @param self HTTP client handle.
 * @param url Request URL.
 * @param payload Request body.
 * @param payload_size Request body size.
 * @param response Response body destination, or nullptr to use the handle's own body.
 * @return Request result.
 */
HTTP_Client_Result http_client_put_2(HTTP_Client *const self, char const *const url, char const *const payload, USize const payload_size, String *const response);

/**
 * @brief Perform PUT with Str payload.
 * @param self HTTP client handle.
 * @param url Request URL.
 * @param payload Request body.
 * @param response Response body destination, or nullptr to use the handle's own body.
 * @return Request result.
 */
HTTP_Client_Result http_client_put_3(HTTP_Client *const self, char const *const url, Str const *const payload, String *const response);

/**
 * @brief Perform PUT with String payload.
 * @param self HTTP client handle.
 * @param url Request URL.
 * @param payload Request body.
 * @param response Response body destination, or nullptr to use the handle's own body.
 * @return Request result.
 */
HTTP_Client_Result http_client_put_4(HTTP_Client *const self, char const *const url, String const *const payload, String *const response);

/**
 * @brief Perform an HTTP DELETE request (no body; parameters belong in the URL).
 * @param self HTTP client handle.
 * @param url Request URL.
 * @param response Response body destination, or nullptr to use the handle's own body.
 * @return Request result.
 */
HTTP_Client_Result http_client_request_delete(HTTP_Client *const self, char const *const url, String *const response);

/**
 * @brief Read this handle's own response body.
 *
 * Populated only by a request called with `response` nullptr; otherwise reflects
 * whatever the last such call left behind (empty for a fresh handle). Valid
 * until the next request on this handle or until the handle is deleted.
 * @param self HTTP client handle.
 * @return The handle's own body buffer.
 */
String const* http_client_response_body(HTTP_Client const *const self);

/**
 * @brief Find a header of the most recent response on this handle.
 * @param self HTTP client handle.
 * @param name Header name without the colon, matched ASCII case-insensitively
 *        ("X-Ratelimit-Remaining").
 * @return The value of the FIRST such header with leading blanks skipped, or
 *         nullptr when the last response carried none. The pointer is valid
 *         until the next request on this handle or the handle is deleted.
 */
char const* http_client_response_header_find(HTTP_Client const *const self, char const *const name);

/**
 * @brief HTTP status code of the most recent request on this handle.
 * @param self HTTP client handle.
 * @return The status code, or 0 when no response has been received yet.
 */
USize http_client_response_status(HTTP_Client const *const self);

/**
 * @brief Whether a result is both a transport success and a 2xx HTTP status.
 * @param self Result to inspect.
 * @return true when `success` is true and response_code is in [200, 300).
 */
bool http_client_result_is_ok(HTTP_Client_Result const *const self);

/**
 * @brief Release result storage.
 * @param self Result instance.
 */
void http_client_result_uninit(HTTP_Client_Result *const self);

/**
 * @brief Set a CA bundle file for TLS verification (CURLOPT_CAINFO), for a peer
 *        whose certificate is not covered by the system trust store.
 * @param self HTTP client handle.
 * @param path Path to a PEM CA bundle.
 */
void http_client_set_ca_file(HTTP_Client *const self, char const *const path);

/**
 * @brief Set a CA directory for TLS verification (CURLOPT_CAPATH).
 * @param self HTTP client handle.
 * @param path Path to a directory of hashed CA certificates.
 */
void http_client_set_ca_path(HTTP_Client *const self, char const *const path);

/**
 * @brief Set connection timeout in milliseconds. 0 forwards to curl's own
 *        built-in connect-timeout default; it does not disable the check.
 * @param self HTTP client handle.
 * @param timeout_ms Timeout in milliseconds; clamped to LONG_MAX before the
 *        backend call (long is 32-bit on an LLP64 build).
 */
void http_client_set_connect_timeout_ms(HTTP_Client *const self, USize const timeout_ms);

/**
 * @brief Enable or disable following HTTP redirects (CURLOPT_FOLLOWLOCATION).
 *        Only http/https are ever followed, regardless of what a server names
 *        in Location. See the Redirects and credentials note above before
 *        enabling this for a request carrying a custom-header credential.
 * @param self HTTP client handle.
 * @param follow true to follow redirects.
 * @param max_redirects Maximum hops when `follow` is true (ignored otherwise);
 *        clamped to LONG_MAX before the backend call. curl's own "-1 means
 *        unlimited" is unreachable through this unsigned parameter - pass a
 *        large finite bound instead.
 * @note `follow` true with `max_redirects` 0 refuses every hop (CURLOPT_MAXREDIRS
 *       0 means zero redirects allowed, not unlimited) - equivalent to `follow` false.
 */
void http_client_set_follow_redirects(HTTP_Client *const self, bool const follow, USize const max_redirects);

/**
 * @brief Set the response body size cap enforced in the write callback. A
 *        response exceeding it fails the request with HTTP_CLIENT_STATUS_ERROR
 *        and a result.error naming the cap - never a memory_alloc abort.
 * @param self HTTP client handle.
 * @param max_bytes Cap in bytes. 0 means unlimited (an explicit opt-out, not a
 *        contract violation).
 */
void http_client_set_max_response_size(HTTP_Client *const self, USize const max_bytes);

/**
 * @brief Set an HTTP proxy for this handle (CURLOPT_PROXY).
 * @param self HTTP client handle.
 * @param url Proxy URL, or "" to force a direct connection (overriding any
 *        proxy environment variable).
 */
void http_client_set_proxy(HTTP_Client *const self, char const *const url);

/**
 * @brief Set total request timeout in milliseconds. 0 forwards to curl meaning
 *        no total-time limit; it does not disable the check.
 * @param self HTTP client handle.
 * @param timeout_ms Timeout in milliseconds; clamped to LONG_MAX before the
 *        backend call (long is 32-bit on an LLP64 build).
 */
void http_client_set_timeout_ms(HTTP_Client *const self, USize const timeout_ms);

/**
 * @brief Set the User-Agent header (CURLOPT_USERAGENT).
 * @param self HTTP client handle.
 * @param user_agent User-Agent string.
 */
void http_client_set_user_agent(HTTP_Client *const self, char const *const user_agent);

/**
 * @brief Set TLS peer and host verification. ON by default; only turn off for a
 *        loopback/test target, never for a production endpoint.
 * @param self HTTP client handle.
 * @param verify_tls true to verify peer and host.
 */
void http_client_set_verify_tls(HTTP_Client *const self, bool const verify_tls);

#endif // HTTP_CLIENT_H