/* ============================================================================
 *  HTTP Server Module
 *  --------------------------------------------------------------------------
 *  @file    http_server.h
 *  @brief   HTTP/1.1 server over libwebsockets: routing, request accessors and
 *           response helpers.
 *
 *  Features:
 *    - Exact and regex route registration on one server handle.
 *    - A regex route exposes its named group "file", or group 1, as route query data.
 *    - Request accessors for method, path, URL query, headers, client address and body.
 *    - Response helpers: sized/NUL-terminated bodies, empty bodies, native file streaming.
 *    - Bodies are accumulated for any method that sends one, keyed on Content-Length
 *      rather than on the method, so PUT/PATCH/DELETE bodies reach the handler.
 *    - Extra libwebsockets subprotocols may be registered before the server starts.
 *    - A per-server request handler, or the weak http_server_request_default_callback.
 *
 *  Usage Examples:
 *    @code
 *    static void _on_health(HTTP_Server_Route *const route) {
 *        HTTP_Server_Holder *const holder = http_server_route_get_holder(route);
 *
 *        http_server_response_send_1(holder->response, "ok",
 *                                    HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN,
 *                                    HTTP_SERVER_STATUS_CODE_OK);
 *    }
 *
 *    // The router is reached through the handler: http_server_set_handler is what makes
 *    // a registered route reachable. Without it the module falls back to the weak
 *    // http_server_request_default_callback, which answers 404 and never dispatches.
 *    static void _handle(void *context, HTTP_Server_Request *request, HTTP_Server_Response *response) {
 *        HTTP_Server         *const  server  = (HTTP_Server*) context;
 *        HTTP_Server_Holder          holder  = { .request = request, .response = response, .arena = nullptr };
 *
 *        if (!http_server_router_dispatch_2(server->router,
 *                                           http_server_request_get_path_1(request),
 *                                           http_server_request_get_path_size(request),
 *                                           &holder)) {
 *            http_server_response_send_empty(response, HTTP_SERVER_STATUS_CODE_NOT_FOUND);
 *        }
 *    }
 *
 *    HTTP_Server server = DEFAULT_INITIALIZATION;
 *
 *    http_server_init(&server);
 *    http_server_set_handler(&server, _handle, &server);
 *    http_server_route_add(&server, "/health", _on_health);
 *
 *    if (result_is_error(http_server_run(&server, 0, true))) {
 *        return 1;
 *    }
 *
 *    U16 const port = http_server_get_port(&server);   // 0 asked for an ephemeral port
 *
 *    log_message_1(LOG_LEVEL_INFO, "listening on %u\n", (unsigned) port);
 *
 *    http_server_stop(&server);
 *    http_server_uninit(&server);
 *    @endcode
 *
 *  Error Handling:
 *    - http_server_init, route_add, route_match_add, register_protocol,
 *      response_header_add and response_send_file answer false on refusal and
 *      log a WARN; they never abort on caller data.
 *    - http_server_run / http_server_run_tls answer a Result; a bind or TLS
 *      failure is reported, not aborted.
 *    - Peer-derived data never aborts: an over-long request URI is answered 414,
 *      an over-long body 413, and a header carrying CR, LF or a control byte is
 *      refused rather than written to the wire.
 *    - A CONTENT-LENGTH is required for a request body. A chunked body is answered
 *      411 Length Required: libwebsockets' HTTP/1 role does not hand a chunked
 *      request body to this callback in ANY build - its h1 parser enters the body
 *      state only from a parsed Content-Length and never dechunks - and waiting for
 *      one held the connection open with no reply at all until lws' own timeout
 *      fired. HTTP/2 is not offered: http_server_run_tls pins ALPN to "http/1.1",
 *      so a length-less h2 body - which the h2 role WOULD deliver, and which the
 *      411 cannot see because h2 carries no Transfer-Encoding header - can never
 *      reach this module. Measured: the vendored lws does not negotiate h2 even
 *      unpinned; the pin makes that true of every build, not just this one.
 *    - A Content-Length that is not a canonical decimal number ("+5", "5 ", "12abc")
 *      is answered 400 Bad Request rather than read as 0: libwebsockets' own atoll
 *      would accept a prefix of it and wait for a body this module was not expecting.
 *    - A REPEATED Content-Length is answered 400 as well: libwebsockets keeps the
 *      lines as fragments and concatenates them without a separator, so "5" and "6"
 *      would read as "56" here while lws itself reads 5 - the request-smuggling
 *      disagreement RFC 9110 8.6 says to refuse.
 *    - The 413, 400, 411 and 503 refusals all close the connection. Only the 413 with
 *      a configured body says so in a Connection header; the bare replies are composed
 *      whole by libwebsockets, which offers no hook to add one.
 *    - http_server_request_get_client_ip answers FALSE, with an empty buffer, in three
 *      cases: an X-Forwarded-For longer than the module reads (1023 bytes), a trusted
 *      hop count past the 32 entries it keeps, and a selected entry that is not an
 *      IPv4 or IPv6 literal - it never falls back to the peer in any of them. With one
 *      or more trusted hops the peer IS the proxy, so
 *      falling back would key the client's blocks and strikes to the proxy, which a
 *      client could trigger deliberately by padding the header. With zero trusted
 *      hops the header is never read and the peer is always the answer.
 *    - A null self/argument is a programmer error and is caught by error_check_*.
 *
 *  Thread Safety:
 *    - The request handler and every route callback run on the libwebsockets
 *      service thread. Request and response objects are stack objects owned by
 *      that callback and are invalid once it returns.
 *    - The matched route handed to a route callback is a per-request COPY, so a
 *      callback never observes another request's holder or query capture.
 *    - One HTTP_Server drives one service thread. Routes registered after
 *      http_server_run are not visible to the running router.
 *    - http_server_stop is safe to call from another thread in EITHER mode: it only
 *      clears the running flag and cancels the service loop.
 *    - In a THREADED run (http_server_run with threaded true) stop and delete also
 *      join the service thread, so delete is safe from another thread too.
 *    - In a NON-THREADED run there is no thread to join: http_server_run services the
 *      loop on the caller's own thread and returns when stop is seen. uninit and
 *      delete destroy the lws context, so they must be called AFTER run has returned
 *      - never from another thread while run is still servicing.
 *    - http_server_set_log_level is a PROCESS-WIDE libwebsockets side effect.
 *
 *  Memory Management:
 *    - http_server_init initializes in place and http_server_uninit releases
 *      everything the handle owns, leaving the struct itself to the caller.
 *      http_server_new / http_server_delete are that pair plus the allocation.
 *    - Under ARENA_IMPLEMENTATION http_server_alloc_new / http_server_alloc_delete
 *      are the arena spelling. alloc_delete runs http_server_uninit and then returns
 *      only the HANDLE to the arena: the route list is the module's own heap
 *      allocation and must not be handed to an arena's deallocate.
 *    - Request, response and route objects are stack objects owned by the callback
 *      that received them; a pointer kept past its return dangles.
 *    - Tier _1 (char*) and tier _4 (String) getters answer an OWNED copy - release
 *      with memory_delete and string_uninit. Tier _3 answers an owned Str
 *      (str_uninit) EXCEPT http_server_route_get_query_static_3, which borrows.
 *      get_method_1/_3, get_path_1/_3 and get_payload_1/_3 borrow the request's own
 *      storage and must not be released.
 *    - The alloc_* getters write into the caller's arena and are released with it;
 *      they answer an empty value (null at tier _1) when the arena is exhausted.
 *    - The body and content type given to http_server_set_payload_limit_response are
 *      BORROWED and must outlive the server.
 *
 *  Performance Characteristics:
 *    - Route lookup is O(routes): the list is scanned newest-first and every regex
 *      route costs one pcre2 match. Measured at 80 routes (40 exact + 40 regex) with
 *      a path that matches nothing - the worst case - at ~2.6-3.8 us per dispatch
 *      (run to run). The per-request route COPY is built only in the MATCHED branch;
 *      zero-filling one per route VISITED cost ~48 KiB of memset on every miss and
 *      measured 5.4 us against 2.6 us back to back on the same machine.
 *    - One response is one lws_write of the header block plus, for a non-empty body,
 *      one allocation, one memcpy and one lws_write. Measured at a 16 KiB body with a
 *      fresh connection per request at ~2131 responses/s.
 *    - A declared request body pre-sizes its accumulation buffer from Content-Length
 *      (clamped to the payload cap), so most of it costs one allocation rather than one
 *      per doubling; an undeclared-size body still doubles from
 *      HTTP_SERVER_PAYLOAD_INITIAL_CAPACITY. The pre-size is gated on
 *      HTTP_SERVER_PAYLOAD_INITIAL_CAPACITY bytes of the body having actually arrived,
 *      not on the first byte or the header: a declaration alone reserves nothing, and
 *      neither does a peer that trickles one byte per callback around a huge declared
 *      size, so idle or slow-trickling connections cannot be made to commit memory they
 *      never fill.
 *    - http_server_response_send_file streams through libwebsockets' own write-
 *      readiness loop, so a multi-MB file costs no module-side buffer at all.
 *
 *  Dependencies:
 *    - libwebsockets >= 4.3.5 (system header <libwebsockets.h>). Every libwebsockets
 *      symbol this module uses - lws_get_peer_simple, lws_get_vhost_by_name,
 *      lws_get_vhost_port, lws_cancel_service, lws_set_log_level's emit callback,
 *      lws_hdr_custom_length / lws_hdr_custom_copy, lws_http_get_uri_and_method -
 *      exists at 4.3.5, checked against that release's headers.
 *    - CFW: char, file, math, memory, regex, result, thread, container/str,
 *      container/string
 *
 *  Naming tiers:
 *    Numeric suffixes name the string representation a family varies on:
 *    `_1` = char*, `_2` = sized char* + USize, `_3` = Str, `_4` = String.
 *    The router family varies on the PATH it accepts; the getter families vary
 *    on the representation they RETURN. A tier absent from a family simply has
 *    no meaningful form there.
 * ============================================================================ */
#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/
#include <libwebsockets.h>

#include <char/char.h>
#include <file/file.h>
#include <math/math.h>
#include <memory/memory.h>
#include <regex/regex.h>
#include <result.h>
#include <thread/thread.h>

/*==============================================================================
 * MARK: - Content Types
 *============================================================================*/
/** Application JSON response content type. */
#define HTTP_SERVER_CONTENT_TYPE_APPLICATION_JSON "application/json"
/** Application octet-stream response content type. */
#define HTTP_SERVER_CONTENT_TYPE_APPLICATION_OCTET_STREAM "application/octet-stream"
/** GIF image response content type. */
#define HTTP_SERVER_CONTENT_TYPE_IMAGE_GIF "image/gif"
/** JPEG image response content type. */
#define HTTP_SERVER_CONTENT_TYPE_IMAGE_JPEG "image/jpeg"
/** PNG image response content type. */
#define HTTP_SERVER_CONTENT_TYPE_IMAGE_PNG "image/png"
/** SVG image response content type. */
#define HTTP_SERVER_CONTENT_TYPE_IMAGE_SVG_XML "image/svg+xml"
/** WebP image response content type. */
#define HTTP_SERVER_CONTENT_TYPE_IMAGE_WEBP "image/webp"
/** X-Icon image response content type. */
#define HTTP_SERVER_CONTENT_TYPE_IMAGE_X_ICON "image/x-icon"
/** CSS response content type. */
#define HTTP_SERVER_CONTENT_TYPE_TEXT_CSS "text/css"
/** HTML response content type. */
#define HTTP_SERVER_CONTENT_TYPE_TEXT_HTML "text/html"
/** JavaScript response content type. */
#define HTTP_SERVER_CONTENT_TYPE_TEXT_JAVASCRIPT "text/javascript"
/** Plain text response content type. */
#define HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN "text/plain"
/** XML response content type. */
#define HTTP_SERVER_CONTENT_TYPE_TEXT_XML "text/xml"

/*==============================================================================
 * MARK: - Status Codes
 *
 * Alphabetical with '_' read as a WORD BREAK, not as a character: NO_CONTENT
 * precedes NOT_ACCEPTABLE ("no" < "not") and every REQ_* precedes REQUEST_TIMEOUT
 * ("req" < "request"). Sorting the underscore as a byte would give the opposite
 * answer in both places, so the rule is stated once here rather than re-derived.
 *============================================================================*/
#define HTTP_SERVER_STATUS_CODE_BAD_GATEWAY HTTP_STATUS_BAD_GATEWAY
#define HTTP_SERVER_STATUS_CODE_BAD_REQUEST HTTP_STATUS_BAD_REQUEST
#define HTTP_SERVER_STATUS_CODE_CONFLICT HTTP_STATUS_CONFLICT
#define HTTP_SERVER_STATUS_CODE_CONTINUE HTTP_STATUS_CONTINUE
/* libwebsockets' HTTP_STATUS_* enum has no name for 201, so the number is
 * written out here rather than aliased to a token that does not exist. */
#define HTTP_SERVER_STATUS_CODE_CREATED 201
#define HTTP_SERVER_STATUS_CODE_EXPECTATION_FAILED HTTP_STATUS_EXPECTATION_FAILED
#define HTTP_SERVER_STATUS_CODE_FORBIDDEN HTTP_STATUS_FORBIDDEN
#define HTTP_SERVER_STATUS_CODE_FOUND HTTP_STATUS_FOUND
#define HTTP_SERVER_STATUS_CODE_GATEWAY_TIMEOUT HTTP_STATUS_GATEWAY_TIMEOUT
#define HTTP_SERVER_STATUS_CODE_GONE HTTP_STATUS_GONE
#define HTTP_SERVER_STATUS_CODE_HTTP_VERSION_NOT_SUPPORTED HTTP_STATUS_HTTP_VERSION_NOT_SUPPORTED
#define HTTP_SERVER_STATUS_CODE_INTERNAL_SERVER_ERROR HTTP_STATUS_INTERNAL_SERVER_ERROR
#define HTTP_SERVER_STATUS_CODE_LENGTH_REQUIRED HTTP_STATUS_LENGTH_REQUIRED
#define HTTP_SERVER_STATUS_CODE_METHOD_NOT_ALLOWED HTTP_STATUS_METHOD_NOT_ALLOWED
#define HTTP_SERVER_STATUS_CODE_MOVED_PERMANENTLY HTTP_STATUS_MOVED_PERMANENTLY
#define HTTP_SERVER_STATUS_CODE_NO_CONTENT HTTP_STATUS_NO_CONTENT
#define HTTP_SERVER_STATUS_CODE_NOT_ACCEPTABLE HTTP_STATUS_NOT_ACCEPTABLE
#define HTTP_SERVER_STATUS_CODE_NOT_FOUND HTTP_STATUS_NOT_FOUND
#define HTTP_SERVER_STATUS_CODE_NOT_IMPLEMENTED HTTP_STATUS_NOT_IMPLEMENTED
#define HTTP_SERVER_STATUS_CODE_NOT_MODIFIED HTTP_STATUS_NOT_MODIFIED
#define HTTP_SERVER_STATUS_CODE_OK HTTP_STATUS_OK
#define HTTP_SERVER_STATUS_CODE_PARTIAL_CONTENT HTTP_STATUS_PARTIAL_CONTENT
#define HTTP_SERVER_STATUS_CODE_PAYMENT_REQUIRED HTTP_STATUS_PAYMENT_REQUIRED
/* 308, not 301: the permanent redirect that preserves the request method. */
#define HTTP_SERVER_STATUS_CODE_PERMANENT_REDIRECT 308
#define HTTP_SERVER_STATUS_CODE_PRECONDITION_FAILED HTTP_STATUS_PRECONDITION_FAILED
#define HTTP_SERVER_STATUS_CODE_PROXY_AUTH_REQUIRED HTTP_STATUS_PROXY_AUTH_REQUIRED
#define HTTP_SERVER_STATUS_CODE_REQ_ENTITY_TOO_LARGE HTTP_STATUS_REQ_ENTITY_TOO_LARGE
#define HTTP_SERVER_STATUS_CODE_REQ_RANGE_NOT_SATISFIABLE HTTP_STATUS_REQ_RANGE_NOT_SATISFIABLE
#define HTTP_SERVER_STATUS_CODE_REQ_URI_TOO_LONG HTTP_STATUS_REQ_URI_TOO_LONG
#define HTTP_SERVER_STATUS_CODE_REQUEST_TIMEOUT HTTP_STATUS_REQUEST_TIMEOUT
#define HTTP_SERVER_STATUS_CODE_SEE_OTHER HTTP_STATUS_SEE_OTHER
#define HTTP_SERVER_STATUS_CODE_SERVICE_UNAVAILABLE HTTP_STATUS_SERVICE_UNAVAILABLE
/* 307, not 302: the temporary redirect that preserves the request method. */
#define HTTP_SERVER_STATUS_CODE_TEMPORARY_REDIRECT 307
#define HTTP_SERVER_STATUS_CODE_UNAUTHORIZED HTTP_STATUS_UNAUTHORIZED
#define HTTP_SERVER_STATUS_CODE_UNSUPPORTED_MEDIA_TYPE HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE

/*==============================================================================
 * MARK: - Limits
 *============================================================================*/
/** Maximum custom request header name size, the trailing ':' and terminator included. */
#define HTTP_SERVER_CUSTOM_HEADER_NAME_MAX_LENGTH 128
/** Maximum copied client IP buffer size. */
#define HTTP_SERVER_IP_MAX_LENGTH 48
/** Maximum copied request or route path size. */
#define HTTP_SERVER_PATH_MAX_LENGTH 256
/** Default maximum accepted request body size. */
#define HTTP_SERVER_PAYLOAD_DEFAULT_MAX_LENGTH 1048576
/** Initial request body buffer capacity; it doubles as the body arrives. */
#define HTTP_SERVER_PAYLOAD_INITIAL_CAPACITY 4096
/** Passed to http_server_set_payload_max_size to accept a body of any size. */
#define HTTP_SERVER_PAYLOAD_UNLIMITED USIZE_MAX
/** Maximum registered libwebsockets subprotocols (beyond the built-in HTTP handler). */
#define HTTP_SERVER_PROTOCOLS_MAX 8
/** Maximum captured route query size. */
#define HTTP_SERVER_QUERY_MAX_LENGTH 256
/** Maximum byte length of one response header line, "Name: value\r\n" included. */
#define HTTP_SERVER_RESPONSE_HEADER_MAX_LENGTH 512

/*==============================================================================
 * MARK: - Type Definitions
 *============================================================================*/
/** Opaque request object. */
struct HTTP_Server_Request;
typedef struct HTTP_Server_Request HTTP_Server_Request;

/** Opaque response object. */
struct HTTP_Server_Response;
typedef struct HTTP_Server_Response HTTP_Server_Response;

/** Opaque route object. */
struct HTTP_Server_Route;
typedef struct HTTP_Server_Route HTTP_Server_Route;

/** Opaque router object. */
struct HTTP_Server_Router;
typedef struct HTTP_Server_Router HTTP_Server_Router;

/** Opaque libwebsockets protocol descriptor (defined by libwebsockets.h). */
struct lws_protocols;

/**
 * @brief Request callback signature.
 * @param context Handler context: the value given to http_server_set_handler, or the
 *                server's own router when the weak default callback is in use.
 * @param request Request object.
 * @param response Response object.
 */
typedef void (*HTTP_Server_Request_Callback)(void *context, HTTP_Server_Request *request, HTTP_Server_Response *response);

/**
 * @brief Route callback signature.
 * @param route Matched route object. A per-request copy, valid only for this call.
 */
typedef void (*HTTP_Server_Route_Callback)(HTTP_Server_Route *route);

/**
 * @brief Route callback request/response holder.
 * @note 'arena' is never set by this module. It exists so a consumer can carry a
 *       request-scoped scratch arena from its request handler into its route
 *       callbacks; a consumer that does not set it reads back whatever it stored.
 */
typedef struct {
    HTTP_Server_Request     *request;   /**< Matched request. */
    HTTP_Server_Response    *response;  /**< Matched response. */
    Arena                   *arena;     /**< Request-scoped scratch arena (consumer-provided). */
} HTTP_Server_Holder;

/**
 * @brief HTTP server handle.
 * @note Every member is READ-ONLY to consumers. 'server' and 'protocols' are exposed
 *       so a consumer registering a libwebsockets subprotocol can reach the context
 *       it needs; writing through them corrupts the server.
 */
typedef struct {
    HTTP_Server_Route               *route;                 /**< Route list head. */
    HTTP_Server_Router              *router;                 /**< Runtime router state. */
    struct lws_context              *server;                 /**< libwebsockets context. */
    USize                           payload_max_size;        /**< Maximum accepted request body size. */
    char                    const   *payload_limit_body;     /**< Optional payload overflow body. */
    char                    const   *payload_limit_type;     /**< Optional payload overflow content type. */
    struct lws_protocols            *protocols;              /**< Assembled lws protocol array (owned; freed on delete). */
    struct lws_protocols            *registered;             /**< Registered extra subprotocols (owned). */
    USize                           registered_count;        /**< Number of registered subprotocols. */
    HTTP_Server_Request_Callback    handler;                 /**< Per-server request handler, or null. */
    void                            *handler_context;        /**< Context passed to 'handler'. */
} HTTP_Server;

/**
 * @brief Known request header identifiers.
 * @note These are headers libwebsockets RECOGNIZES and parses into its own token
 *       slots. They are readable ONLY through http_server_request_header_copy —
 *       http_server_request_custom_header_copy can never see them, because the
 *       custom store holds only headers lws did not recognize. A recognized
 *       header read through the custom path silently reports "absent", which
 *       reads exactly like a legitimately missing header. Extend this enum
 *       rather than reaching for the custom path.
 */
typedef enum {
    HTTP_SERVER_HEADER_ACCEPT,
    HTTP_SERVER_HEADER_ACCEPT_ENCODING,
    HTTP_SERVER_HEADER_AUTHORIZATION,
    HTTP_SERVER_HEADER_CONTENT_LENGTH,
    HTTP_SERVER_HEADER_CONTENT_TYPE,
    HTTP_SERVER_HEADER_COOKIE,
    HTTP_SERVER_HEADER_HOST,
    HTTP_SERVER_HEADER_IF_MODIFIED_SINCE,
    HTTP_SERVER_HEADER_IF_NONE_MATCH,
    HTTP_SERVER_HEADER_IF_RANGE,
    HTTP_SERVER_HEADER_ORIGIN,
    HTTP_SERVER_HEADER_RANGE,
    HTTP_SERVER_HEADER_REFERER,
    HTTP_SERVER_HEADER_USER_AGENT,
    HTTP_SERVER_HEADER_X_FORWARDED_FOR
} HTTP_Server_Header;

/*==============================================================================
 * MARK: - Arena API
 *============================================================================*/
#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Delete an arena-allocated server.
 * @param self Server pointer reference.
 * @param allocator Arena allocator.
 */
void http_server_alloc_delete(HTTP_Server **const self, Arena *allocator);

/**
 * @brief Allocate and initialize a server with an arena.
 * @param allocator Arena allocator.
 * @return Server pointer, or null when the arena is exhausted.
 */
HTTP_Server *http_server_alloc_new(Arena *allocator);

/**
 * @brief Copy a custom request header into an arena String.
 * @param self Request object.
 * @param name Custom header name, any case, with or without a trailing ':' (see
 *             http_server_request_custom_header_copy).
 * @param allocator Arena allocator.
 * @return Arena-backed String. Empty if missing or invalid.
 */
String http_server_alloc_request_custom_header_get_4(HTTP_Server_Request *const self, char const *const name, Arena *const allocator);

/**
 * @brief Copy the client address into an arena char buffer.
 * @param self Request object.
 * @param allocator Arena allocator.
 * @return Arena char buffer, or null when the arena is exhausted.
 */
char *http_server_alloc_request_get_ip_1(HTTP_Server_Request *const self, Arena *allocator);

/**
 * @brief Copy the client address into an arena Str.
 * @param self Request object.
 * @param allocator Arena allocator.
 * @return Arena-backed Str. Empty when the arena is exhausted.
 */
Str http_server_alloc_request_get_ip_3(HTTP_Server_Request *const self, Arena *allocator);

/**
 * @brief Copy the client address into an arena String.
 * @param self Request object.
 * @param allocator Arena allocator.
 * @return Arena-backed String. Empty when the arena is exhausted.
 */
String http_server_alloc_request_get_ip_4(HTTP_Server_Request *const self, Arena *allocator);

/**
 * @brief Copy a known request header into an arena String.
 * @param self Request object.
 * @param header Header identifier.
 * @param allocator Arena allocator.
 * @return Arena-backed String. Empty if missing or invalid.
 */
String http_server_alloc_request_header_get_4(HTTP_Server_Request *const self, HTTP_Server_Header const header, Arena *const allocator);

/**
 * @brief Copy the route query capture into an arena char buffer.
 * @param self Route object.
 * @param allocator Arena allocator.
 * @return Arena char buffer, or null when the arena is exhausted.
 */
char *http_server_alloc_route_get_query_1(HTTP_Server_Route *const self, Arena *allocator);

/**
 * @brief Copy the route query capture into an arena Str.
 * @param self Route object.
 * @param allocator Arena allocator.
 * @return Arena-backed Str. Empty when the arena is exhausted.
 */
Str http_server_alloc_route_get_query_3(HTTP_Server_Route *const self, Arena *allocator);

/**
 * @brief Copy the route query capture into an arena String.
 * @param self Route object.
 * @param allocator Arena allocator.
 * @return Arena-backed String. Empty when the arena is exhausted.
 */
String http_server_alloc_route_get_query_4(HTTP_Server_Route *const self, Arena *allocator);
#endif // ARENA_IMPLEMENTATION

/*==============================================================================
 * MARK: - Server API
 *============================================================================*/
/**
 * @brief Delete server resources and null the handle.
 * @param self Server pointer reference.
 */
void http_server_delete(HTTP_Server **const self);

/**
 * @brief Get the port the server is bound to.
 * @param self Server object.
 * @return Bound port, or 0 when the server is not running. When run was asked for
 *         port 0 this reports the ephemeral port the OS actually assigned.
 */
U16 http_server_get_port(HTTP_Server *const self);

/**
 * @brief Initialize a server in place.
 *
 * In place, not by value: http_server_run stores 'self' in the libwebsockets
 * context and hands it to the service thread, so a copy made after run would
 * dangle.
 * @param self Server object.
 * @return true when initialized.
 */
bool http_server_init(HTTP_Server *const self);

/**
 * @brief Allocate and initialize a server.
 * @return Server pointer.
 */
HTTP_Server* http_server_new(void);

/**
 * @brief Register a libwebsockets subprotocol before the server starts.
 *
 * The caller fills a struct lws_protocols (its own callback and per_session_data_size)
 * and passes it; the descriptor is copied. Must be called before http_server_run /
 * http_server_run_tls; registration is refused once the server is running.
 * @param self Server object.
 * @param protocol Protocol descriptor to copy.
 * @return true if registered; false if the registry is full or the server is already running.
 */
bool http_server_register_protocol(HTTP_Server *const self, struct lws_protocols const *const protocol);

/**
 * @brief Run an HTTP server.
 * @param self Server object.
 * @param port Port number. 0 asks the OS for an ephemeral port; read it back with
 *             http_server_get_port.
 * @param threaded If true, run the service loop in a thread and return immediately.
 * @return RESULT_SUCCESS, or an error when the context could not be created (bind
 *         failure, port in use) or the service thread could not start.
 */
Result http_server_run(HTTP_Server *const self, U16 const port, bool const threaded);

/**
 * @brief Run an HTTPS server.
 * @param self Server object.
 * @param key TLS key path.
 * @param cert TLS certificate path.
 * @param port Port number. 0 asks the OS for an ephemeral port.
 * @param threaded If true, run the service loop in a thread and return immediately.
 * @return RESULT_SUCCESS, or an error when the context could not be created (bind
 *         failure, unreadable key or certificate) or the service thread could not start.
 * @note ALPN is pinned to "http/1.1". libwebsockets would otherwise offer "h2,http/1.1",
 *       and its HTTP/2 role enters the body state with no Content-Length at all, firing
 *       LWS_CALLBACK_HTTP before the DATA frames - a POST would be dispatched with an
 *       empty body and then dispatched a SECOND time at body completion. The 411 that
 *       covers HTTP/1 cannot see that case, so h2 is not offered until the module
 *       handles it.
 */
Result http_server_run_tls(HTTP_Server *const self, char const *const key, char const *const cert, U16 const port, bool const threaded);

/**
 * @brief Set the per-server request handler.
 *
 * When no handler is set the module calls the weak
 * http_server_request_default_callback, passing the server's own router as the
 * context — the historical single-handler-per-process arrangement, kept as a
 * fallback. A handler set here lets two servers live in one binary.
 * @param self Server object.
 * @param handler Request handler, or null to fall back to the weak default.
 * @param context Opaque value passed back as the handler's first argument.
 */
void http_server_set_handler(HTTP_Server *const self, HTTP_Server_Request_Callback const handler, void *const context);

/**
 * @brief Set the libwebsockets log level and route its output into the CFW log.
 *
 * PROCESS-WIDE: libwebsockets keeps one log level for the whole process.
 * @param level Bitmask of libwebsockets LLL_* levels. LLL_ERR alone is the default.
 */
void http_server_set_log_level(I32 const level);

/**
 * @brief Set the response body used when a request body exceeds the maximum.
 * @param self Server object.
 * @param body Response body. Borrowed; must outlive the server.
 * @param content_type Response content type. Borrowed; must outlive the server.
 */
void http_server_set_payload_limit_response(HTTP_Server *const self, char const *const body, char const *const content_type);

/**
 * @brief Set the maximum accepted request body size.
 * @param self Server object.
 * @param size Maximum body bytes. 0 restores the module default
 *             (HTTP_SERVER_PAYLOAD_DEFAULT_MAX_LENGTH); pass
 *             HTTP_SERVER_PAYLOAD_UNLIMITED to accept a body of any size.
 */
void http_server_set_payload_max_size(HTTP_Server *const self, USize const size);

/**
 * @brief Stop a running server.
 *
 * Clears the running flag and cancels the libwebsockets service loop. A threaded
 * run returns its thread to a joinable state; a non-threaded run's
 * http_server_run returns. Safe to call from another thread, and safe to call on
 * a server that is not running.
 * @note TERMINAL for the handle. The libwebsockets context is left standing, so a
 *       later http_server_run on the same handle is refused as "already running".
 *       Restarting means http_server_uninit followed by http_server_init and
 *       re-registering the routes - uninit is what drops the route list and the
 *       context together.
 * @param self Server object.
 */
void http_server_stop(HTTP_Server *const self);

/**
 * @brief Release everything a server owns, leaving the struct itself untouched.
 *
 * The teardown half of http_server_init: stops the service loop, joins a threaded
 * run, destroys the libwebsockets context and frees the route list. Use it for a
 * server that lives on the stack or inside another object;
 * http_server_delete is this plus freeing the handle.
 * @param self Server object.
 */
void http_server_uninit(HTTP_Server *const self);

/*==============================================================================
 * MARK: - Response API Accessors
 *============================================================================*/
/**
 * @brief Get the client WSI pointer from a response.
 * @param self Response object.
 * @return WSI pointer.
 */
void* http_server_response_get_client(HTTP_Server_Response *const self);

/**
 * @brief Get the accumulated extra response headers.
 * @param self Response context.
 * @return Borrowed String pointer containing the headers. Read-only.
 */
String const* http_server_response_get_extra_headers(HTTP_Server_Response const *const self);

/**
 * @brief Add a custom HTTP header to the response.
 * @param self Response object.
 * @param key Header name.
 * @param value Header value.
 * @return true when queued; false (with a WARN) when key or value carries CR, LF or a
 *         control byte, or when "key: value\r\n" would exceed
 *         HTTP_SERVER_RESPONSE_HEADER_MAX_LENGTH.
 */
bool http_server_response_header_add(HTTP_Server_Response *const self, char const *const key, char const *const value);

/**
 * @brief Append a raw headers buffer block to the response context.
 * @param self Response context.
 * @param headers Raw CRLF-terminated HTTP headers block.
 * @param headers_size Byte length of the headers block.
 */
void http_server_response_header_add_raw(HTTP_Server_Response *const self, char const *const headers, USize const headers_size);

/**
 * @brief Set the file_served status of a response.
 * @param self Response object.
 * @param served File served status.
 */
void http_server_response_set_file_served(HTTP_Server_Response *const self, bool const served);

/**
 * @brief Set the write_success status of a response.
 * @param self Response object.
 * @param success Write success status.
 */
void http_server_response_set_write_success(HTTP_Server_Response *const self, bool const success);

/*==============================================================================
 * MARK: - Route API
 *============================================================================*/
/**
 * @brief Add an exact path route.
 * @param self Server object.
 * @param path Exact route path.
 * @param callback Route callback.
 * @return true when registered; false (with a WARN) when 'path' is empty or would be
 *         truncated at HTTP_SERVER_PATH_MAX_LENGTH.
 */
bool http_server_route_add(HTTP_Server *const self, char const *const path, HTTP_Server_Route_Callback callback);

/**
 * @brief Get the request/response holder from a matched route.
 * @param self Route object.
 * @return Holder pointer. Valid only inside the route callback.
 */
HTTP_Server_Holder* http_server_route_get_holder(HTTP_Server_Route *const self);

/**
 * @brief Borrow the matched route path.
 * @param self Route object.
 * @return Borrowed route path. Read-only; valid only inside the route callback.
 */
char* http_server_route_get_path(HTTP_Server_Route *const self);

/**
 * @brief Copy the route query capture into a char buffer.
 * @param self Route object.
 * @return Owned char buffer; release with memory_delete.
 */
char* http_server_route_get_query_1(HTTP_Server_Route *const self);

/**
 * @brief Copy the route query capture into a Str.
 * @param self Route object.
 * @return Owned Str; release with str_uninit.
 */
Str http_server_route_get_query_3(HTTP_Server_Route *const self);

/**
 * @brief Copy the route query capture into a String.
 * @param self Route object.
 * @return Owned String; release with string_uninit.
 */
String http_server_route_get_query_4(HTTP_Server_Route *const self);

/**
 * @brief Borrow the route query capture as a Str.
 * @param self Route object.
 * @return Borrowed Str. Must NOT be released; valid only inside the route callback.
 */
Str http_server_route_get_query_static_3(HTTP_Server_Route *const self);

/**
 * @brief Add a regex route.
 * @param self Server object.
 * @param path Regex route pattern; anchored with ^ and $ by the module.
 * @param callback Route callback.
 * @return true when registered; false (with a WARN) when 'path' is empty, would be
 *         truncated, or is not a compilable pattern.
 */
bool http_server_route_match_add(HTTP_Server *const self, char const *const path, HTTP_Server_Route_Callback callback);

/*==============================================================================
 * MARK: - Request API
 *============================================================================*/
/**
 * @brief Check whether Content-Type matches a MIME value.
 * @param self Request object.
 * @param mime Expected MIME value.
 * @return true when Content-Type starts with MIME and optional parameters follow.
 */
bool http_server_request_content_type_is(HTTP_Server_Request *const self, char const *const mime);

/**
 * @brief Copy a custom request header into caller-provided storage.
 *
 * libwebsockets stores an unrecognized header name LOWERCASED and WITH its trailing ':',
 * and its own lookup compares against that spelling byte for byte - so "X-Device-Id" used
 * to read as "absent", which is indistinguishable from a header the client never sent.
 * The name is folded here instead: any case, with or without the ':', finds the header.
 * @param self Request object.
 * @param name Custom header name, any case, with or without a trailing ':'.
 * @param buffer Destination buffer, set to "" when the header is absent.
 * @param capacity Destination capacity including null terminator.
 * @return true when the header was copied. False (with a WARN) when the name is longer
 *         than HTTP_SERVER_CUSTOM_HEADER_NAME_MAX_LENGTH.
 */
bool http_server_request_custom_header_copy(HTTP_Server_Request *const self, char const *const name, char *const buffer, USize const capacity);

/**
 * @brief Copy a custom request header into a String.
 * @param self Request object.
 * @param name Custom header name, any case, with or without a trailing ':' (see
 *             http_server_request_custom_header_copy).
 * @return Owned String. Empty if missing or invalid.
 */
String http_server_request_custom_header_get_4(HTTP_Server_Request *const self, char const *const name);

/**
 * @brief Get the request body pointer.
 * @param self Request object.
 * @return Borrowed body pointer into the request's own buffer, mutable so a parser
 *         can tokenize in place. Owned by the request; valid only inside the callback.
 */
Byte* http_server_request_get_body(HTTP_Server_Request *const self);

/**
 * @brief Get the request body size.
 * @param self Request object.
 * @return Body size.
 */
USize http_server_request_get_body_size(HTTP_Server_Request *const self);

/**
 * @brief Get the libwebsockets client object.
 * @param self Request object.
 * @return Client pointer.
 */
void* http_server_request_get_client(HTTP_Server_Request *const self);

/**
 * @brief Resolve the originating client address, walking X-Forwarded-For.
 *
 * With 'trusted_hops' 0 the peer address is reported and X-Forwarded-For is
 * ignored — the only correct answer when the server is reached directly. With N
 * trusted hops the N-th entry counted from the RIGHT is reported: the rightmost
 * entry is what the nearest trusted proxy appended, so it is the address that
 * proxy observed. The list is scanned right to left and only the entries the hop
 * count actually needs are kept, so a peer cannot push the proxy-appended entry
 * out of reach by prepending fakes. When the list is shorter than the hop count
 * the leftmost entry is used; when there is no usable list at all the peer
 * address is reported.
 *
 * An entry that is not a syntactic IPv4 or IPv6 literal is discarded rather than
 * reported: the value feeds rate limiters and IP blocks, and letting a peer put
 * arbitrary text there would let it forge an identity those consumers key on.
 * @param self Request object.
 * @param trusted_hops Number of reverse proxies in front of this server.
 * @param buffer Destination buffer, set to "" when nothing could be resolved.
 * @param capacity Destination capacity including null terminator.
 * @return true when a non-empty address was written.
 */
bool http_server_request_get_client_ip(HTTP_Server_Request *const self, USize const trusted_hops, char *const buffer, USize const capacity);

/**
 * @brief Copy the peer address into a char buffer.
 * @param self Request object.
 * @return Owned buffer; release with memory_delete. This is the transport peer,
 *         which behind a reverse proxy is the proxy — use
 *         http_server_request_get_client_ip for the originating client.
 */
char* http_server_request_get_ip_1(HTTP_Server_Request *const self);

/**
 * @brief Copy the peer address into a Str.
 * @param self Request object.
 * @return Owned Str; release with str_uninit.
 */
Str http_server_request_get_ip_3(HTTP_Server_Request *const self);

/**
 * @brief Copy the peer address into a String.
 * @param self Request object.
 * @return Owned String; release with string_uninit.
 */
String http_server_request_get_ip_4(HTTP_Server_Request *const self);

/**
 * @brief Borrow the request method as a char buffer.
 * @param self Request object.
 * @return Borrowed method. Valid only inside the callback.
 */
char* http_server_request_get_method_1(HTTP_Server_Request *const self);

/**
 * @brief Borrow the request method as a Str.
 * @param self Request object.
 * @return Borrowed method Str. Must NOT be released.
 */
Str http_server_request_get_method_3(HTTP_Server_Request *const self);

/**
 * @brief Copy the request method into a String.
 * @param self Request object.
 * @return Owned String; release with string_uninit.
 */
String http_server_request_get_method_4(HTTP_Server_Request *const self);

/**
 * @brief Borrow the request path as a char buffer.
 * @param self Request object.
 * @return Borrowed path. Valid only inside the callback.
 */
char* http_server_request_get_path_1(HTTP_Server_Request *const self);

/**
 * @brief Borrow the request path as a Str.
 * @param self Request object.
 * @return Borrowed path Str. Must NOT be released.
 */
Str http_server_request_get_path_3(HTTP_Server_Request *const self);

/**
 * @brief Copy the request path into a String.
 * @param self Request object.
 * @return Owned path String; release with string_uninit.
 */
String http_server_request_get_path_4(HTTP_Server_Request *const self);

/**
 * @brief Get the request path size.
 * @param self Request object.
 * @return Path size.
 */
USize http_server_request_get_path_size(HTTP_Server_Request *const self);

/**
 * @brief Borrow the request payload as a char buffer.
 * @param self Request object.
 * @return Borrowed payload. Valid only inside the callback.
 */
char* http_server_request_get_payload_1(HTTP_Server_Request *const self);

/**
 * @brief Borrow the request payload as a Str.
 * @param self Request object.
 * @return Borrowed payload Str. Must NOT be released.
 */
Str http_server_request_get_payload_3(HTTP_Server_Request *const self);

/**
 * @brief Copy the request payload into a String.
 * @param self Request object.
 * @return Owned String; release with string_uninit.
 */
String http_server_request_get_payload_4(HTTP_Server_Request *const self);

/**
 * @brief Copy a known request header into caller-provided storage.
 * @param self Request object.
 * @param header Header identifier.
 * @param buffer Destination buffer, set to "" when the header is absent.
 * @param capacity Destination capacity including null terminator.
 * @return true when the header was copied.
 */
bool http_server_request_header_copy(HTTP_Server_Request *const self, HTTP_Server_Header const header, char *const buffer, USize const capacity);

/**
 * @brief Copy a known request header into a String.
 * @param self Request object.
 * @param header Header identifier.
 * @return Owned String. Empty if missing or invalid.
 */
String http_server_request_header_get_4(HTTP_Server_Request *const self, HTTP_Server_Header const header);

/**
 * @brief Copy the request URL query string (the raw "key=value&..." after '?',
 *        without the '?') into 'buffer', NUL-terminated. This is the real URL
 *        query, unlike http_server_route_get_query_* which returns a regex
 *        capture group. Pair with http_query_alloc_get_1 to read parameters, or
 *        http_query_alloc_get_5 to scan the buffer by its known capacity directly.
 * @param self     Request object.
 * @param buffer   Destination buffer (set to "" when there is no query).
 * @param capacity Size of 'buffer'.
 * @return true when a non-empty query was copied, false otherwise.
 */
bool http_server_request_query_copy(HTTP_Server_Request *const self, char *const buffer, USize const capacity);

/*==============================================================================
 * MARK: - Response API
 *============================================================================*/
/**
 * @brief Send a null-terminated response.
 * @param self Response object.
 * @param data Response data, or null for an empty body.
 * @param content_type HTTP content type.
 * @param status_code HTTP status code.
 */
void http_server_response_send_1(HTTP_Server_Response *const self, char const *const data, char const *const content_type, U16 const status_code);

/**
 * @brief Send a sized response. The body is stripped for a HEAD request; the
 *        Content-Length header still reports the size the body would have had. A
 *        1xx, 204 or 304 status sends neither Content-Length nor a body, whatever
 *        data says - RFC 9110 forbids a length there, and a 304 with a body would
 *        tell a cache the representation is empty.
 * @param self Response object.
 * @param data Response data, or null for an empty body.
 * @param data_size Response data size.
 * @param content_type HTTP content type.
 * @param status_code HTTP status code.
 */
void http_server_response_send_2(HTTP_Server_Response *const self, Byte const *const data, USize const data_size, char const *const content_type, U16 const status_code);

/**
 * @brief Send an empty response.
 * @param self Response object.
 * @param status_code HTTP status code.
 */
void http_server_response_send_empty(HTTP_Server_Response *const self, U16 const status_code);

/**
 * @brief Stream a file from disk as a 200 response using libwebsockets' native
 *        file serving, which services write-readiness internally and therefore
 *        never truncates a large (multi-MB) body the way a single `send_2`
 *        lws_write can over a real socket.
 * @param self Response object.
 * @param path On-disk file path.
 * @param content_type HTTP content type (MIME).
 * @param extra_headers Pre-formatted CRLF-terminated header block to emit alongside
 *                      lws' own, or null. Headers queued with
 *                      http_server_response_header_add are appended after it.
 * @param extra_headers_size Byte length of 'extra_headers'.
 * @return true when the file was handed to the transport for streaming; false (with a
 *         WARN) when the file does not exist or the combined header block does not fit.
 * @note Existence is checked here rather than left to libwebsockets, which answers a
 *       missing file with its own 404 and reports the transaction complete - so a
 *       caller could not tell "streamed" from "was not there", and its own 404 body
 *       never reached the client. Nothing is written when this answers false, and the
 *       response is left untouched: a refusal here does NOT fail the response, so the
 *       caller's own fallback reply still goes out on a live connection.
 * @note The existence check is a documented TOCTOU and is ACCEPTED. A file deleted
 *       between the check and lws_serve_http_file makes lws answer its own 404 - the
 *       exact behaviour that held before the check was added, so the window is never
 *       worse than not checking. Closing it would mean holding an open descriptor
 *       across a call that opens the path by name itself, which lws' file-serving API
 *       does not offer; the check buys the common case at no cost to the rare one.
 */
bool http_server_response_send_file(HTTP_Server_Response *const self, char const *const path, char const *const content_type, char const *const extra_headers, USize const extra_headers_size);

/*==============================================================================
 * MARK: - Router API
 *============================================================================*/
/**
 * @brief Dispatch a route by null-terminated path.
 *
 * Routes are scanned newest-first: the LAST registration that matches wins.
 * @param self Router object.
 * @param path Request path.
 * @param handler Request/response holder.
 * @return true if a route matched and its callback ran.
 */
bool http_server_router_dispatch_1(HTTP_Server_Router *const self, char const *const path, HTTP_Server_Holder *const handler);

/**
 * @brief Dispatch a route by sized path.
 * @param self Router object.
 * @param path Request path.
 * @param path_size Request path size. An empty path matches nothing and answers false.
 * @param handler Request/response holder.
 * @return true if a route matched and its callback ran.
 */
bool http_server_router_dispatch_2(HTTP_Server_Router *const self, char const *const path, USize const path_size, HTTP_Server_Holder *const handler);

/**
 * @brief Dispatch a route by Str path.
 * @param self Router object.
 * @param path Request path.
 * @param handler Request/response holder.
 * @return true if a route matched and its callback ran.
 */
bool http_server_router_dispatch_3(HTTP_Server_Router *const self, Str const *const path, HTTP_Server_Holder *const handler);

/**
 * @brief Dispatch a route by String path.
 * @param self Router object.
 * @param path Request path.
 * @param handler Request/response holder.
 * @return true if a route matched and its callback ran.
 */
bool http_server_router_dispatch_4(HTTP_Server_Router *const self, String const *const path, HTTP_Server_Holder *const handler);

/*==============================================================================
 * MARK: - Default Callback
 *============================================================================*/
/**
 * @brief Weak fallback request callback, used when no per-server handler is set.
 *        Prefer http_server_set_handler; this exists so an existing single-server
 *        program can keep defining the symbol.
 * @note The module always compiles a weak 404 definition, so a program that sets a
 *       handler links without supplying one. A program that DEFINES this symbol
 *       overrides that weak default, which is how the single-handler-per-process
 *       arrangement still works. HTTP_SERVER_REQUEST_DEFAULT_CALLBACK_IMPLEMENTATION,
 *       which used to gate the definition, is retired: it made the symbol's existence
 *       a build setting, and weakness says the same thing at link time.
 * @note A strong definition displaces the weak one only when the linker actually sees
 *       it: it must live in an object file named on the link line, or in an archive
 *       member pulled in for some OTHER symbol. A strong definition sitting alone in a
 *       .a is never fetched, because the weak one already satisfies the reference - so
 *       the weak 404 wins silently. This bites the day CFW ships as libcfw.a.
 * @param context The server's own router.
 * @param request Request object.
 * @param response Response object.
 */
extern void http_server_request_default_callback(void *context, HTTP_Server_Request *request, HTTP_Server_Response *response);

#endif // HTTP_SERVER_H