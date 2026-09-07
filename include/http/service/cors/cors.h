/*
 * cors.h - HTTP CORS service for the C Libraries Framework
 * @version 0.3.2
 *
 * Builds and validates Cross-Origin Resource Sharing policy data for browser
 * API requests. Application code owns reading request headers and writing the
 * returned response header block.
 *
 * Features:
 *   - Allowed origin, method, header, and exposed-header lists.
 *   - Credential-aware origin validation: a wildcard origin is REFUSED at add time
 *     while credentials are allowed, instead of storing fine and then rejecting every
 *     request with no signal.
 *   - Preflight validation and response generation, including the reflected
 *     Access-Control-Allow-Methods / -Headers a credentialed preflight needs. Every line
 *     is SIZE-ACCOUNTED - Vary, Allow-Origin, Allow-Credentials and Expose-Headers as much
 *     as the policy lines - so an allocator that drops one is caught rather than passing a
 *     trailing-CRLF test the previous line already satisfies.
 *   - Vary emitted on refusals as well as allows, so a shared cache cannot serve one
 *     origin's answer to another.
 *   - Optional Private Network Access answer (Access-Control-Allow-Private-Network).
 *   - Arena and heap allocation support.
 *
 * Usage Example:
 *   @code
 *   HTTP_Service_CORS cors = DEFAULT_INITIALIZATION;
 *
 *   if (!http_service_cors_init_1(&cors)) {
 *       // Memory Management: false means this instance does not exist - fail startup.
 *   }
 *
 *   http_service_cors_origin_add(&cors, "https://traymon.com");
 *
 *   // ... inside a route callback, with `request` and `response` in hand:
 *   // 320, not 256: a legal Origin reaches 267 bytes (scheme 8 + host 253 + port 6), and a
 *   // header_copy that truncates one yields a value no list entry matches.
 *   char origin[320] = DEFAULT_INITIALIZATION;
 *
 *   http_server_request_header_copy(request, HTTP_SERVER_HEADER_ORIGIN, origin, sizeof(origin));
 *
 *   if (char_compare_iequal_1(http_server_request_get_method_1(request), "OPTIONS")) {
 *       char method[32]     = DEFAULT_INITIALIZATION;
 *       char headers[512]   = DEFAULT_INITIALIZATION;
 *
 *       // libwebsockets tokenizes Origin, but NOT the two Access-Control-Request-*
 *       // names, so those two are read through the custom-header path.
 *       http_server_request_custom_header_copy(request, "Access-Control-Request-Method", method, sizeof(method));
 *       http_server_request_custom_header_copy(request, "Access-Control-Request-Headers", headers, sizeof(headers));
 *
 *       if (http_service_cors_preflight_allowed_1(&cors, origin, method, headers)) {
 *           String block = http_service_cors_preflight_create(&cors, origin, method, headers, false);
 *
 *           if (!string_empty(&block)) {
 *               http_server_response_header_add_raw(response, string_get_data(&block), string_get_size(&block));
 *           }
 *
 *           string_uninit(&block);
 *       }
 *
 *       http_server_response_send_empty(response, HTTP_SERVER_STATUS_CODE_NO_CONTENT);
 *   }
 *   else {
 *       String block = http_service_cors_headers_create(&cors, origin);
 *
 *       if (!string_empty(&block)) {
 *           http_server_response_header_add_raw(response, string_get_data(&block), string_get_size(&block));
 *       }
 *
 *       string_uninit(&block);
 *   }
 *
 *   http_service_cors_uninit(&cors);
 *   @endcode
 *
 * Origin semantics:
 *   - Origins are stored and compared LOWERCASED: scheme and host are case-insensitive
 *     and a browser always sends them lowercase, so http_service_cors_origin_add
 *     ("https://Example.com") would otherwise store an entry nothing can ever match.
 *   - The match is otherwise exact: scheme, host and port all have to agree. There are no
 *     pattern origins - no wildcard subdomain, no port-agnostic localhost - so every
 *     development port needs its own entry. The value echoed back is the STORED entry, not
 *     the request's own bytes, which is what makes header injection through Origin
 *     impossible whatever a client sends.
 *   - "null" (sandboxed iframes, file:// documents, some redirects) matches only when it
 *     was added literally. That is a deliberate footgun: "null" is not one origin, it is
 *     every opaque origin at once, so adding it opens the API to any page that can put
 *     itself in a sandbox. Add it only for a development host.
 *   - An origin carrying a space, a comma, a control byte (CR and LF included), DEL, or any
 *     byte above 0x7E is refused at add time, so no list entry can ever carry a header
 *     break. That one sentence is the whole rule; the WARN and the docs say it the same way.
 *   - http_service_cors_exposed_header_add("*") stores and emits
 *     Access-Control-Expose-Headers: * unchanged, credentials or not. Under credentials a
 *     browser reads that as a header literally NAMED "*" and exposes nothing - the same
 *     trap the method and header lists dodge by reflecting - so name the headers you mean
 *     rather than relying on the wildcard there.
 *
 * Error Handling:
 *   - Public functions validate non-null pointers.
 *   - Every add answers bool: true when the entry is in the list after the call (including
 *     when it already was), false when it was refused - an empty or malformed value, a
 *     wildcard origin under credentials, or an allocator that declined the copy. A false
 *     leaves the list unchanged; it never aborts. Network-derived and config-derived text
 *     alike must be able to be rejected without ending the process.
 *   - An empty origin, method, or header is a legal VALUE, answered false, never an abort.
 *   - Client-caused refusals inside preflight_create are SILENT and cost only the offending
 *     line; only an allocator that dropped a written line discards the block and WARNs.
 *   - A max_age of 0 is legal ("do not cache this preflight"), not a refusal.
 *
 * Thread Safety:
 *   - Configuration (init, uninit, every *_add, private_network_set) is NOT thread-safe and
 *     belongs to startup, before the service is shared.
 *   - Once configured, concurrent reads through a const self - every *_allowed_* query and
 *     both *_create functions - are safe on the heap configuration: they mutate nothing.
 *   - An ARENA-backed instance is the exception: both *_create functions borrow from the
 *     arena, and Arena is not thread-safe (see arena.h). The window is the RETURNED
 *     String's whole life - from the *_create that borrows to the string_uninit that gives
 *     the bytes back, since allocator_release calls the arena's own deallocate. Give each
 *     thread its own arena-backed service, or serialize create-through-uninit around it.
 *
 * Memory Management:
 *   - List values are copied into service-owned storage; the caller's buffer may go away.
 *   - The emitted Allow-Methods, Allow-Headers, Max-Age and Expose-Headers lines are
 *     rendered ONCE per configuration change into service-owned Strings, not per request.
 *   - Returned String values must be uninitialized by the caller.
 *   - A false return from any init/alloc_init leaves *self zeroed - treat it as "this
 *     instance does not exist" and fail startup rather than calling anything else on it.
 *
 * Performance Characteristics:
 *   - Lookups are linear over configured lists; each list caches whether it holds the
 *     wildcard, so a preflight does not rescan for "*" four times.
 *   - A preflight response is three cached line appends plus the origin line. The only
 *     per-request rendering left is the credentialed reflected method/header list.
 *
 * Comparison:
 *   - tower-http::cors offers AllowOrigin::mirror_request, which echoes ANY origin. There
 *     is deliberately no equivalent here: an unbounded mirror is the classic CORS
 *     misconfiguration, and every reflected value in this module came from the list.
 *
 * Dependencies:
 *   - allocator, arena, arrayList, char, http/headers, str, string.
 *
 * See cors.c for implementation details.
 */

#ifndef HTTP_SERVICE_CORS_H
#define HTTP_SERVICE_CORS_H

#include <arena/arena.h>
#include <container/arrayList/al_str.h>
#include <container/string/string.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

/** @brief Credentials are opt-in: the wildcard origin is useless while they are on. */
#define HTTP_SERVICE_CORS_DEFAULT_ALLOW_CREDENTIALS false
/** @brief Preflight cache seconds. Chrome caps a preflight at 7200, so 600 is never clamped. */
#define HTTP_SERVICE_CORS_DEFAULT_MAX_AGE 600

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief CORS policy configuration.
 */
typedef struct {
#ifdef ARENA_IMPLEMENTATION
    /** @brief Optional arena used by owned values and returned strings. */
    Arena *allocator;
#endif // ARENA_IMPLEMENTATION
    /** @brief Whether credentialed browser requests are allowed. */
    bool allow_credentials;
    /** @brief Whether a Private Network Access preflight is answered affirmatively. */
    bool allow_private_network;
    /** @brief Rendered Access-Control-Expose-Headers line, empty when the list is empty. */
    String expose_line;
    /** @brief Response header names exposed to browser code. */
    AL_Str exposed_headers;
    /** @brief Header names accepted by preflight checks. */
    AL_Str headers;
    /** @brief Rendered Access-Control-Allow-Headers line. */
    String headers_line;
    /** @brief Whether `headers` holds the wildcard, cached at add time. */
    bool headers_wildcard;
    /** @brief Preflight cache duration in seconds. Zero is legal and means "do not cache". */
    USize max_age;
    /** @brief Rendered Access-Control-Max-Age line. */
    String max_age_line;
    /** @brief HTTP methods accepted by preflight checks. */
    AL_Str methods;
    /** @brief Rendered Access-Control-Allow-Methods line. */
    String methods_line;
    /** @brief Whether `methods` holds the wildcard, cached at add time. */
    bool methods_wildcard;
    /** @brief Origins accepted by the policy, stored lowercased. */
    AL_Str origins;
    /** @brief Whether `origins` holds the wildcard, cached at add time. */
    bool origins_wildcard;
} HTTP_Service_CORS;

/*==============================================================================
 * MARK: - API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Initialize an arena-backed CORS service with default values in place.
 * @param self Service instance, zeroed on failure.
 * @param allocator Arena allocator.
 * @return true when the service is usable; false when a default entry or a rendered
 *         line was refused (see http_service_cors_init_1).
 */
bool http_service_cors_alloc_init_1(HTTP_Service_CORS *const self, Arena *const allocator);

/**
 * @brief Initialize an arena-backed CORS service with explicit values in place.
 * @param self Service instance, zeroed on failure.
 * @param allow_credentials Whether credentialed browser requests are allowed.
 * @param max_age Preflight cache duration in seconds. Zero is legal.
 * @param allocator Arena allocator.
 * @return See http_service_cors_alloc_init_1.
 */
bool http_service_cors_alloc_init_2(HTTP_Service_CORS *const self, bool const allow_credentials, USize const max_age, Arena *const allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Add an exposed response header name.
 * @param self Service instance.
 * @param header Header name; an HTTP token, matched case-insensitively.
 * @return true when the entry is in the list AND the emitted line was re-rendered; false
 *         when the value is empty or not a token, or the allocator refused the copy
 *         (list unchanged) or the re-render (the entry is stored, but the line is cleared,
 *         so the policy fails CLOSED until a later add renders it whole). Entries are
 *         deduplicated case-insensitively and emitted in the case they were stored in,
 *         which browsers compare case-insensitively anyway.
 */
bool http_service_cors_exposed_header_add(HTTP_Service_CORS *const self, char const *const header);

/**
 * @brief Add an allowed request header name.
 * @param self Service instance.
 * @param header Header name; an HTTP token, or "*".
 * @return See http_service_cors_exposed_header_add.
 */
bool http_service_cors_header_add(HTTP_Service_CORS *const self, char const *const header);

/**
 * @brief Check whether a comma-separated header list is allowed.
 * @param self Service instance.
 * @param headers Comma-separated header names, an empty string for none.
 * @return true when every named header is allowed.
 */
bool http_service_cors_headers_allowed_1(HTTP_Service_CORS const *const self, char const *const headers);

/**
 * @brief Check whether a sized comma-separated header list is allowed.
 * @param self Service instance.
 * @param headers Comma-separated header names.
 * @param headers_size Byte length of headers.
 * @return true when every named header is allowed. Optional whitespace around each name
 *         is trimmed, space and HTAB alike, and empty elements ("a,,b") are skipped.
 */
bool http_service_cors_headers_allowed_2(HTTP_Service_CORS const *const self, char const *const headers, USize const headers_size);

/**
 * @brief Build response headers for an actual (non-preflight) CORS request.
 * @param self Service instance.
 * @param origin Request Origin header value.
 * @return Header block, to be released with string_uninit. A refused origin still yields
 *         the Vary line whenever the origin list is not the wildcard - a cache that stored
 *         the CORS-less refusal would otherwise serve it to an allowed origin. Empty only
 *         when the wildcard list refused the origin, or the allocator declined.
 */
String http_service_cors_headers_create(HTTP_Service_CORS const *const self, char const *const origin);

/**
 * @brief Initialize a CORS service with default values in place.
 * @param self Service instance, zeroed on failure.
 * @return true when the service is usable; false when any of the nine default method and
 *         header entries, or any rendered line, was refused - a service missing a default
 *         rejects requests that should have been allowed, so the refusal is reported here
 *         rather than discovered as a broken client.
 */
bool http_service_cors_init_1(HTTP_Service_CORS *const self);

/**
 * @brief Initialize a CORS service with explicit values in place.
 * @param self Service instance, zeroed on failure.
 * @param allow_credentials Whether credentialed browser requests are allowed.
 * @param max_age Preflight cache duration in seconds. Zero is legal.
 * @return See http_service_cors_init_1.
 */
bool http_service_cors_init_2(HTTP_Service_CORS *const self, bool const allow_credentials, USize const max_age);

/**
 * @brief Add an allowed HTTP method.
 * @param self Service instance.
 * @param method HTTP method; an HTTP token, or "*".
 * @return See http_service_cors_exposed_header_add.
 */
bool http_service_cors_method_add(HTTP_Service_CORS *const self, char const *const method);

/**
 * @brief Check whether a method is allowed.
 * @param self Service instance.
 * @param method HTTP method.
 * @return true when allowed. The comparison is case-INSENSITIVE, which is laxer than
 *         RFC 9110 (methods are case-sensitive); browsers only ever send the normalized
 *         upper-case spelling of the methods they know, so nothing reaches this laxity
 *         from a browser.
 */
bool http_service_cors_method_allowed_1(HTTP_Service_CORS const *const self, char const *const method);

/**
 * @brief Check whether a sized method is allowed.
 * @param self Service instance.
 * @param method HTTP method.
 * @param method_size Byte length of method.
 * @return See http_service_cors_method_allowed_1.
 */
bool http_service_cors_method_allowed_2(HTTP_Service_CORS const *const self, char const *const method, USize const method_size);

/**
 * @brief Add an allowed origin.
 * @param self Service instance.
 * @param origin Origin value, or "*".
 * @return true when the entry is in the list (including when it already was); false when
 *         it was refused, leaving the list unchanged. "*" is refused (with a WARN) while
 *         allow_credentials is set: a wildcard origin and credentials are mutually
 *         exclusive in the browser, so storing it would silently deny every request.
 */
bool http_service_cors_origin_add(HTTP_Service_CORS *const self, char const *const origin);

/**
 * @brief Check whether an origin is allowed.
 * @param self Service instance.
 * @param origin Request Origin header value.
 * @return true when allowed.
 */
bool http_service_cors_origin_allowed_1(HTTP_Service_CORS const *const self, char const *const origin);

/**
 * @brief Check whether a sized origin is allowed.
 * @param self Service instance.
 * @param origin Request Origin header value.
 * @param origin_size Byte length of origin.
 * @return true when allowed. A literal "*" as the REQUEST origin is never allowed by the
 *         wildcard list; no browser sends it, so it can only be a caller's mistake.
 */
bool http_service_cors_origin_allowed_2(HTTP_Service_CORS const *const self, char const *const origin, USize const origin_size);

/**
 * @brief Check whether a Str origin is allowed.
 * @param self Service instance.
 * @param origin Request Origin header value; the server's sized accessors yield one.
 * @return See http_service_cors_origin_allowed_2. An empty Str answers false.
 */
bool http_service_cors_origin_allowed_3(HTTP_Service_CORS const *const self, Str const *const origin);

/**
 * @brief Check whether a preflight request is allowed.
 * @param self Service instance.
 * @param origin Request Origin header value.
 * @param method Access-Control-Request-Method value.
 * @param headers Access-Control-Request-Headers value, empty when the header is absent.
 * @return true when the preflight can proceed.
 */
bool http_service_cors_preflight_allowed_1(HTTP_Service_CORS const *const self, char const *const origin, char const *const method, char const *const headers);

/**
 * @brief Check whether a sized preflight request is allowed.
 * @param self Service instance.
 * @param origin Request Origin header value.
 * @param origin_size Byte length of origin.
 * @param method Access-Control-Request-Method value.
 * @param method_size Byte length of method.
 * @param headers Access-Control-Request-Headers value.
 * @param headers_size Byte length of headers.
 * @return true when the preflight can proceed.
 */
bool http_service_cors_preflight_allowed_2(HTTP_Service_CORS const *const self,
    char const *const origin, USize const origin_size, char const *const method, USize const method_size, char const *const headers, USize const headers_size);

/**
 * @brief Build response headers for a CORS preflight request.
 * @param self Service instance.
 * @param origin Request Origin header value.
 * @param method Access-Control-Request-Method value.
 * @param headers Access-Control-Request-Headers value.
 * @param private_network_requested Whether the request carried
 *        Access-Control-Request-Private-Network: true.
 * @return Header block, to be released with string_uninit. A wildcard method or header
 *         list is NEVER emitted as a literal "*" under credentials - browsers read it as a
 *         header literally named "*" and fail the preflight - so the requested method and
 *         header list is reflected instead. A request method or header list that is not an
 *         HTTP token omits ONLY its own Allow-* line and logs nothing: the bytes came from
 *         the client, the browser then fails the preflight for want of that line, and a
 *         WARN per hostile preflight would be a log flood any client could drive. Vary
 *         always names Access-Control-Request-Method and Access-Control-Request-Headers,
 *         plus Origin whenever the origin list is not the wildcard - the block itself is
 *         discarded only when the ALLOCATOR drops a line this service did write.
 */
String http_service_cors_preflight_create(HTTP_Service_CORS const *const self,
    char const *const origin, char const *const method, char const *const headers, bool const private_network_requested);

/**
 * @brief Set whether a Private Network Access preflight is answered affirmatively.
 * @param self Service instance.
 * @param allow true to emit Access-Control-Allow-Private-Network: true when a preflight
 *        asked for it. Default false: a public page reaching a private-network address is
 *        exactly what the browser check exists to stop.
 */
void http_service_cors_private_network_set(HTTP_Service_CORS *const self, bool const allow);

/**
 * @brief Release all service storage.
 * @param self Service instance.
 */
void http_service_cors_uninit(HTTP_Service_CORS *const self);

#endif // HTTP_SERVICE_CORS_H