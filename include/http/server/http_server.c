#include <http/server/http_server.h>

/*==============================================================================
 * MARK: - Private Constants
 *============================================================================*/
#define _HTTP_SERVER_CONTENT_TYPE_MAX_SIZE    256
#define _HTTP_SERVER_FORWARDED_MAX_HOPS       32
#define _HTTP_SERVER_FORWARDED_MAX_SIZE       1024
#define _HTTP_SERVER_PAYLOAD_PRESIZE_MAX_SIZE 1048576
#define _HTTP_SERVER_PEER_ADDRESS_MAX_SIZE    64
#define _HTTP_SERVER_REQUEST_METHOD_SIZE      16
#define _HTTP_SERVER_RESPONSE_HEADER_SIZE     2048
#define _HTTP_SERVER_ROUTE_REGEX_MAX_SIZE     512
#define _HTTP_SERVER_SERVICE_TIMEOUT_MS       50
#define _HTTP_SERVER_VHOST_NAME               "cfw"

/*==============================================================================
 * MARK: - Private Types
 *============================================================================*/
/** @brief What a request's Content-Length header is, once read. */
typedef enum {
    _HTTP_SERVER_CONTENT_LENGTH_ABSENT,
    _HTTP_SERVER_CONTENT_LENGTH_INVALID,
    _HTTP_SERVER_CONTENT_LENGTH_PRESENT
} _HTTP_Server_Content_Length;

typedef enum {
    _HTTP_SERVER_DISPATCH_STATUS_ERROR       = -1,
    _HTTP_SERVER_DISPATCH_STATUS_SUCCESS     = 0,
    _HTTP_SERVER_DISPATCH_STATUS_FILE_SERVED = 1,
    /* libwebsockets finished the transaction inside lws_serve_http_file and asked for the
     * connection to be closed - see http_server_response_send_file. */
    _HTTP_SERVER_DISPATCH_STATUS_COMPLETED   = 2
} _HTTP_Server_Dispatch_Status;

typedef struct HTTP_Server_Request {
    struct lws  *wsi;
    char        method[_HTTP_SERVER_REQUEST_METHOD_SIZE];
    U16         method_size;
    char        path[HTTP_SERVER_PATH_MAX_LENGTH];
    U16         path_size;
    String      payload;
    bool        payload_borrowed;
} HTTP_Server_Request;

struct HTTP_Server_Response {
    struct lws  *wsi;
    U16         status_code;
    bool        headers_sent;
    bool        file_served;
    /* Set only when libwebsockets both completed the transaction itself and asked for the
     * connection to be closed (lws_serve_http_file > 0). */
    bool        transaction_completed;
    bool        is_head;
    bool        write_success;
    String      extra_headers;
};

struct HTTP_Server_Route {
    char                        path[HTTP_SERVER_PATH_MAX_LENGTH];
    U16                         path_size;
    Regex                       regex;
    HTTP_Server_Route_Callback  callback;
    HTTP_Server_Holder          holder;
    char                        query_value[HTTP_SERVER_QUERY_MAX_LENGTH];
    U16                         query_value_size;
    struct HTTP_Server_Route    *next;
};

struct HTTP_Server_Router {
    HTTP_Server_Route   *routes;
    /* Written by http_server_stop / http_server_delete from any thread and read by the
     * service loop on its own thread; atomic so neither side races the other's word. */
    _Atomic bool        running;
    bool                is_threaded;
    U16                 port;
    Thread              thread;
};

typedef struct {
    char    *payload;
    USize   payload_size;
    USize   payload_capacity;
    /* What the request's Content-Length declared, carried from the header callback to the
     * first body callback. The one allocation it sizes is made THERE, not on the header:
     * pre-sizing before a byte of body arrived let a header-only request commit up to the
     * presize ceiling per connection and then stall, which is slowloris with amplification. */
    USize   payload_declared_size;
} HTTP_Session;

/*==============================================================================
 * MARK: - Private Helpers
 *============================================================================*/
/** @brief Route a libwebsockets log line into the CFW log. Matches lws_log_emit_t. */
static void _http_server_log_emit(I32 level, char const *line) {
    if (line == nullptr) {
        return;
    }

    /* Every non-ERR level used to land on WARN, so asking lws for LLL_INFO (or the
     * per-request LLL_PARSER/LLL_HEADER firehose) filled the log with warnings that
     * described nothing wrong. The level lws chose is carried across instead. */
    LogLevel log_level = LOG_LEVEL_DEBUG;

    if (level == LLL_ERR) {
        log_level = LOG_LEVEL_ERROR;
    }
    else if (level == LLL_WARN) {
        log_level = LOG_LEVEL_WARN;
    }
    else if (level == LLL_NOTICE || level == LLL_INFO) {
        log_level = LOG_LEVEL_INFO;
    }

    log_message_1(log_level, "libwebsockets: %s", line);
}

/** @brief Map a public header identifier to its libwebsockets token, or -1. */
static I32 _http_server_header_token(HTTP_Server_Header const header) {
    switch (header) {
        case HTTP_SERVER_HEADER_ACCEPT:             { return WSI_TOKEN_HTTP_ACCEPT; }
        case HTTP_SERVER_HEADER_ACCEPT_ENCODING:    { return WSI_TOKEN_HTTP_ACCEPT_ENCODING; }
        case HTTP_SERVER_HEADER_AUTHORIZATION:      { return WSI_TOKEN_HTTP_AUTHORIZATION; }
        case HTTP_SERVER_HEADER_CONTENT_LENGTH:     { return WSI_TOKEN_HTTP_CONTENT_LENGTH; }
        case HTTP_SERVER_HEADER_CONTENT_TYPE:       { return WSI_TOKEN_HTTP_CONTENT_TYPE; }
        case HTTP_SERVER_HEADER_COOKIE:             { return WSI_TOKEN_HTTP_COOKIE; }
        case HTTP_SERVER_HEADER_HOST:               { return WSI_TOKEN_HOST; }
        case HTTP_SERVER_HEADER_IF_MODIFIED_SINCE:  { return WSI_TOKEN_HTTP_IF_MODIFIED_SINCE; }
        case HTTP_SERVER_HEADER_IF_NONE_MATCH:      { return WSI_TOKEN_HTTP_IF_NONE_MATCH; }
        case HTTP_SERVER_HEADER_IF_RANGE:           { return WSI_TOKEN_HTTP_IF_RANGE; }
        case HTTP_SERVER_HEADER_ORIGIN:             { return WSI_TOKEN_ORIGIN; }
        case HTTP_SERVER_HEADER_RANGE:              { return WSI_TOKEN_HTTP_RANGE; }
        case HTTP_SERVER_HEADER_REFERER:            { return WSI_TOKEN_HTTP_REFERER; }
        case HTTP_SERVER_HEADER_USER_AGENT:         { return WSI_TOKEN_HTTP_USER_AGENT; }
        case HTTP_SERVER_HEADER_X_FORWARDED_FOR:    { return WSI_TOKEN_X_FORWARDED_FOR; }
        default:                                    { return -1; }
    }
}

/**
 * @brief Reject a header name or value that could split the response.
 *
 * Refuses every C0 control byte and DEL, which covers CR and LF - the two that let a
 * caller's data close the header block early and inject a whole response of its own.
 * HTAB is refused with them: it is legal inside a folded value that nothing here emits,
 * and allowing it buys a caller nothing while widening what reaches the wire.
 */
static bool _http_server_header_field_valid(char const *const value) {
    for (USize index = 0; value[index] != '\0'; index += 1) {
        Byte const character = (Byte) value[index];

        if (character < 0x20 || character == 0x7F) {
            return false;
        }
    }

    return true;
}

/** @brief Initialize a stack response for one transaction. */
static void _http_server_response_init(HTTP_Server_Response *const self, struct lws *const wsi, bool const is_head) {
    self->wsi                   = wsi;
    self->status_code           = HTTP_SERVER_STATUS_CODE_OK;
    self->headers_sent          = false;
    self->file_served           = false;
    self->transaction_completed = false;
    self->is_head               = is_head;
    self->write_success         = true;
    self->extra_headers         = string_init_1();
}

/** @brief Read the peer transport address, without the reverse DNS lookup lws_get_peer_addresses does. */
static USize _http_server_peer_address(struct lws *const wsi, char *const buffer, USize const capacity) {
    buffer[0] = '\0';

    if (wsi == nullptr) {
        return 0;
    }

    /* lws_get_peer_simple, not lws_get_peer_addresses: the latter resolves the peer's
     * hostname through getnameinfo ON THE SERVICE THREAD, so one slow resolver stalls
     * every connection the server is holding. Nothing here ever wanted the name. */
    lws_get_peer_simple(wsi, buffer, capacity);

    buffer[capacity - 1] = '\0';

    return char_length(buffer);
}

/**
 * @brief Normalize a custom header name into the spelling libwebsockets stores.
 *
 * lws stores an unrecognized header LOWERCASED and with its trailing ':' - "x-device-id:"
 * - and lws_hdr_custom_length compares against exactly that, byte for byte. So both
 * "X-Device-Id" (no colon) and "X-Device-Id:" (wrong case) read as absent, which is
 * indistinguishable from a header the client never sent. Every spelling is folded to the
 * one lws holds, because the failure mode of the old contract was silence.
 *
 * @param name Caller's header name.
 * @param buffer Destination for the normalized name.
 * @param capacity Destination capacity, terminator included.
 * @return Normalized length, or 0 when the name is empty or does not fit.
 */
static USize _http_server_custom_header_name(char const *const name, char *const buffer, USize const capacity) {
    buffer[0] = '\0';

    USize const size = char_length(name);

    if (size == 0) {
        return 0;
    }

    /* The fit is measured on the NORMALIZED length, not on size + 2 unconditionally: a name
     * that already ends in ':' grows by nothing, so a 127-byte "x-trace-id:" fits a
     * 128-byte buffer exactly and used to be refused for a colon it did not need. */
    USize const normalized_size = name[size - 1] == ':' ? size : size + 1;

    if (normalized_size + CHAR_END_CHARACTER > capacity) {
        return 0;
    }

    char_copy_3(buffer, capacity, (char*) name, size);

    buffer[normalized_size - 1]     = ':';
    buffer[normalized_size]         = '\0';

    char_lower_2(buffer, normalized_size);

    return normalized_size;
}

/** @brief Copy an address into the caller's buffer; false when it is empty or does not fit. */
static bool _http_server_address_write(char *const buffer, USize const capacity, char const *const address, USize const size) {
    if (size == 0 || size >= capacity) {
        return false;
    }

    char_copy_3(buffer, capacity, (char*) address, size);

    buffer[size] = '\0';

    return true;
}

/** @brief Answer whether a sized slice is a syntactic dotted-quad IPv4 literal. */
static bool _http_server_address_ipv4_valid(char const *const value, USize const size) {
    USize groups    = 0;
    USize index     = 0;

    while (index < size) {
        USize   digits  = 0;
        U32     octet   = 0;

        while (index < size && value[index] >= '0' && value[index] <= '9') {
            /* "01" and "0177" are refused: a leading zero reads as octal to some parsers
             * and as decimal to others, so one spelling would name two addresses. */
            if (digits == 1 && octet == 0) {
                return false;
            }

            octet   = (octet * 10) + (U32) (value[index] - '0');
            digits  += 1;
            index   += 1;
        }

        if (digits == 0 || digits > 3 || octet > 255) {
            return false;
        }

        groups += 1;

        if (index == size) {
            break;
        }

        if (value[index] != '.' || groups == 4) {
            return false;
        }

        index += 1;
    }

    return groups == 4;
}

/** @brief Answer whether a sized slice is a syntactic IPv6 literal, embedded IPv4 tail included. */
static bool _http_server_address_ipv6_valid(char const *const value, USize const size) {
    USize   groups      = 0;
    USize   index       = 0;
    bool    compressed  = false;

    /* A leading ':' is legal only as the first half of "::". */
    if (value[0] == ':') {
        if (size < 2 || value[1] != ':') {
            return false;
        }

        compressed  = true;
        index       = 2;
    }

    while (index < size) {
        USize   group_stop  = index;
        bool    dotted      = false;

        while (group_stop < size && value[group_stop] != ':') {
            if (value[group_stop] == '.') {
                dotted = true;
            }

            group_stop += 1;
        }

        /* The last group may be written as a dotted IPv4, standing for two groups. */
        if (dotted) {
            if (group_stop != size || !_http_server_address_ipv4_valid(&value[index], group_stop - index)) {
                return false;
            }

            groups += 2;

            break;
        }

        USize digits = 0;

        while (index < size &&
               ((value[index] >= '0' && value[index] <= '9') ||
                (value[index] >= 'a' && value[index] <= 'f') ||
                (value[index] >= 'A' && value[index] <= 'F'))) {
            digits  += 1;
            index   += 1;
        }

        if (digits == 0 || digits > 4) {
            return false;
        }

        groups += 1;

        if (index == size) {
            break;
        }

        if (value[index] != ':') {
            return false;
        }

        index += 1;

        if (index < size && value[index] == ':') {
            if (compressed) {
                return false;
            }

            compressed  = true;
            index       += 1;
        }
        else if (index == size) {
            /* A trailing single ':' is not a literal. */
            return false;
        }
    }

    if (groups > 8 || (compressed && groups > 7) || (!compressed && groups != 8)) {
        return false;
    }

    return groups > 0 || compressed;
}

/**
 * @brief Answer whether a sized slice is a syntactic IPv4 or IPv6 address literal.
 *
 * Written here rather than delegated to net_socket_address_init_2 so http/server keeps
 * its dependency list - and every consumer makefile - unchanged: net would put a Winsock
 * startup on a per-request path to answer a purely lexical question. Syntax only: no
 * reachability, no scope, no zone index, no port suffix (X-Forwarded-For carries bare
 * addresses, so a "host:port" entry is refused rather than half-parsed).
 */
static bool _http_server_address_literal_valid(char const *const value, USize const size) {
    if (size == 0) {
        return false;
    }

    for (USize index = 0; index < size; index += 1) {
        if (value[index] == ':') {
            return _http_server_address_ipv6_valid(value, size);
        }
    }

    return _http_server_address_ipv4_valid(value, size);
}

/**
 * @brief Read the declared request body size.
 *
 * ABSENT and INVALID are separate answers on purpose. Both used to be reported as "0",
 * which is also what a legitimate `Content-Length: 0` says - so "12abc" was read here as
 * "no body declared" and dispatched at once, while libwebsockets' own atoll read it as 12,
 * entered its body state and fired a SECOND dispatch at body completion. A value that is
 * not a canonical decimal number is refused instead of guessed at.
 *
 * @param wsi libwebsockets connection.
 * @param size Declared size, written on ABSENT (0) and PRESENT; untouched on INVALID.
 * @return Which of the three the header is.
 */
static _HTTP_Server_Content_Length _http_server_content_length(struct lws *const wsi, USize *const size) {
    *size = 0;

    I32 const declared_size = lws_hdr_total_length(wsi, WSI_TOKEN_HTTP_CONTENT_LENGTH);

    if (declared_size <= 0) {
        return _HTTP_SERVER_CONTENT_LENGTH_ABSENT;
    }

    /* A REPEATED Content-Length is kept by libwebsockets as separate fragments of the same
     * token. lws_hdr_copy concatenates every fragment with no separator, so "5" and "6"
     * read here as "56" - all digits, so the canonical-number check below waves it through -
     * while lws' own atoll reads the FIRST fragment and enters its body state expecting 5.
     * That disagreement is the request-smuggling shape RFC 9110 8.6 says to answer 400.
     * Fragment index 1 exists only when a second line was sent. */
    if (lws_hdr_fragment_length(wsi, WSI_TOKEN_HTTP_CONTENT_LENGTH, 1) > 0) {
        return _HTTP_SERVER_CONTENT_LENGTH_INVALID;
    }

    char buffer[32] = DEFAULT_INITIALIZATION;

    /* Longer than any 64-bit decimal: not a syntax error, just a number no body can
     * reach. Reported as the saturated value so the payload cap refuses it with 413,
     * which is a truer answer than 400. */
    if ((USize) declared_size >= sizeof(buffer)) {
        *size = USIZE_MAX;

        return _HTTP_SERVER_CONTENT_LENGTH_PRESENT;
    }

    I32 const copy_size = lws_hdr_copy(wsi, buffer, (I32) sizeof(buffer), WSI_TOKEN_HTTP_CONTENT_LENGTH);

    if (copy_size <= 0) {
        return _HTTP_SERVER_CONTENT_LENGTH_INVALID;
    }

    buffer[copy_size] = '\0';

    USize value = 0;

    /* Digits only, end to end: no sign, no leading or trailing space, no trailing text.
     * RFC 9110 defines Content-Length as 1*DIGIT and nothing else. */
    for (I32 index = 0; index < copy_size; index += 1) {
        if (buffer[index] < '0' || buffer[index] > '9') {
            return _HTTP_SERVER_CONTENT_LENGTH_INVALID;
        }

        if (value > (USIZE_MAX - (USize) (buffer[index] - '0')) / 10) {
            *size = USIZE_MAX;

            return _HTTP_SERVER_CONTENT_LENGTH_PRESENT;
        }

        value = (value * 10) + (USize) (buffer[index] - '0');
    }

    *size = value;

    return _HTTP_SERVER_CONTENT_LENGTH_PRESENT;
}

/** @brief Answer the effective body cap for a server handle. */
static USize _http_server_payload_max_size(HTTP_Server const *const self) {
    if (self == nullptr || self->payload_max_size == 0) {
        return HTTP_SERVER_PAYLOAD_DEFAULT_MAX_LENGTH;
    }

    return self->payload_max_size;
}

/**
 * @brief Answer 413 with the server's configured body, or the bare lws status line.
 *
 * Every caller returns -1 straight after, so BOTH replies close the connection. Only the
 * configured-body branch says so in a header: lws_return_http_status composes the whole
 * reply itself and offers no hook to add one. The difference is in the wire header, never
 * in the behaviour - and the same holds for the bare 400, 411 and 503 refusals.
 */
static void _http_server_reject_payload(HTTP_Server const *const self, struct lws *const wsi) {
    if (self != nullptr &&
        !memory_empty(self->payload_limit_body) &&
        !memory_empty(self->payload_limit_type)) {
        HTTP_Server_Response response = DEFAULT_INITIALIZATION;

        _http_server_response_init(&response, wsi, false);
        http_server_response_header_add(&response, "Connection", "close");

        http_server_response_send_1(&response,
                                    self->payload_limit_body,
                                    self->payload_limit_type,
                                    HTTP_SERVER_STATUS_CODE_REQ_ENTITY_TOO_LARGE);
        string_uninit(&response.extra_headers);

        return;
    }

    lws_return_http_status(wsi, HTTP_SERVER_STATUS_CODE_REQ_ENTITY_TOO_LARGE, nullptr);
}

/** @brief Release a keep-alive session's accumulated body AND the counters that describe it. */
static void _http_server_session_reset(HTTP_Session *const self) {
    if (self == nullptr) {
        return;
    }

    if (self->payload != nullptr) {
        memory_delete((void**) &self->payload);
    }

#ifndef MEMORY_NON_DANGLING_POINTER
    self->payload = nullptr;
#endif

    /* The counters go with the buffer. libwebsockets reuses one per-connection user_space
     * across every transaction on a keep-alive connection and clears only its OWN http
     * fields, so a second POST on the same connection would otherwise arrive with the
     * first one's size and capacity, skip the reallocation, and memcpy through a freed
     * pointer at a stale offset. */
    self->payload_size          = 0;
    self->payload_capacity      = 0;
    self->payload_declared_size = 0;
}

/*
 * Compiled UNCONDITIONALLY, and weak. It used to sit behind
 * HTTP_SERVER_REQUEST_DEFAULT_CALLBACK_IMPLEMENTATION, which no build defines any more -
 * so the extern the dispatcher references had no definition at all, and a program that
 * used http_server_set_handler (and therefore never calls this) still failed to LINK
 * unless it defined the symbol itself. The gate made the symbol's presence a build
 * setting; weakness already expresses the same thing at link time, and better: a program
 * that defines its own strong http_server_request_default_callback still overrides this,
 * which is what every existing consumer does.
 */
CFW_ATTR_WEAK void http_server_request_default_callback(void *context, HTTP_Server_Request *request, HTTP_Server_Response *response) {
    trace_log_push(LOG_METADATA);

    (void) context;
    (void) request;

    http_server_response_send_1(response,
                                "<html><head><title>Not found</title></head><body>404</body></html>",
                                HTTP_SERVER_CONTENT_TYPE_TEXT_HTML,
                                HTTP_SERVER_STATUS_CODE_NOT_FOUND);
    trace_log_pop();
}

static char const* _http_server_lws_method_to_str(I32 method_index) {
    switch (method_index) {
        case LWSHUMETH_GET: {
            return "GET";
        }
        case LWSHUMETH_POST: {
            return "POST";
        }
        case LWSHUMETH_OPTIONS: {
            return "OPTIONS";
        }
        case LWSHUMETH_PUT: {
            return "PUT";
        }
        case LWSHUMETH_PATCH: {
            return "PATCH";
        }
        case LWSHUMETH_DELETE: {
            return "DELETE";
        }
        case LWSHUMETH_CONNECT: {
            return "CONNECT";
        }
        case LWSHUMETH_HEAD: {
            return "HEAD";
        }
        default: {
            return "GET";
        }
    }
}

static _HTTP_Server_Dispatch_Status _http_server_dispatch_request(HTTP_Server *const self, struct lws *const wsi, char const *const method, char const *const uri,
    USize const uri_size, char const *const payload, USize const payload_size) {
    HTTP_Server_Request request = DEFAULT_INITIALIZATION;

    request.wsi = wsi;

    USize const method_length   = char_length(method);
    USize const method_size     = method_length < (USize) sizeof(request.method) - 1 ? method_length : (USize) sizeof(request.method) - 1;

    char_copy_3(request.method, sizeof(request.method), (void*) method, method_size);

    request.method_size = (U16) method_size;

    bool const is_head = char_compare_equal_2(request.method, method_size, "HEAD", CHAR_STATIC_SIZE("HEAD"));

    HTTP_Server_Response response = DEFAULT_INITIALIZATION;

    _http_server_response_init(&response, wsi, is_head);

    /* A path longer than the request buffer used to be TRUNCATED at 255 bytes and then
     * matched: "/assets/<255 bytes>/../../secret" would hit a prefix route as a resource
     * the peer never asked for. Answer 414 instead of guessing which resource was meant. */
    if (uri_size >= (USize) sizeof(request.path)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA,
                      "http_server: refusing a %llu-byte request URI, the limit is %llu",
                      (unsigned long long) uri_size,
                      (unsigned long long) sizeof(request.path) - 1);

        http_server_response_send_1(&response,
                                    "Request-URI Too Long",
                                    HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN,
                                    HTTP_SERVER_STATUS_CODE_REQ_URI_TOO_LONG);
        string_uninit(&response.extra_headers);

        return _HTTP_SERVER_DISPATCH_STATUS_SUCCESS;
    }

    char_copy_3(request.path, sizeof(request.path), (void*) uri, uri_size);

    request.path_size = (U16) uri_size;

    if (payload != nullptr) {
        request.payload             = string_init_4((char*) payload, payload_size);
        request.payload_borrowed    = true;
    }
    else {
        request.payload             = string_init_1();
        request.payload_borrowed    = false;
    }

    if (self->handler != nullptr) {
        self->handler(self->handler_context, &request, &response);
    }
    else {
        http_server_request_default_callback(self->router, &request, &response);
    }

    if (!request.payload_borrowed) {
        string_uninit(&request.payload);
    }

    _HTTP_Server_Dispatch_Status status = _HTTP_SERVER_DISPATCH_STATUS_SUCCESS;

    if (response.transaction_completed) {
        status = _HTTP_SERVER_DISPATCH_STATUS_COMPLETED;
    }
    else if (response.file_served) {
        status = _HTTP_SERVER_DISPATCH_STATUS_FILE_SERVED;
    }

    string_uninit(&response.extra_headers);

    if (!response.write_success) {
        return _HTTP_SERVER_DISPATCH_STATUS_ERROR;
    }

    return status;
}

static I32 _http_server_callback_http(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *incoming, size_t incoming_size) {
    HTTP_Session    *session    = (HTTP_Session*) user;
    HTTP_Server     *self       = (HTTP_Server*) lws_context_user(lws_get_context(wsi));

    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED: {
            // The HTTP catch-all never serves WebSocket. A WS upgrade that binds here (no or an
            // unmatched Sec-WebSocket-Protocol) is unauthenticated; close it so only a registered
            // WS subprotocol can hold a connection.
            return -1;
        }
        case LWS_CALLBACK_HTTP: {
            if (self == nullptr) {
                return 0;
            }

            char        *uri_buffer     = nullptr;
            I32         uri_buffer_size = 0;
            I32 const   method_index    = lws_http_get_uri_and_method(wsi, &uri_buffer, &uri_buffer_size);

            if (method_index < 0 || uri_buffer == nullptr || uri_buffer_size < 0) {
                return -1;
            }

            USize declared_size = 0;

            _HTTP_Server_Content_Length const   length_kind = _http_server_content_length(wsi, &declared_size);
            USize                       const   payload_max = _http_server_payload_max_size(self);

            /* "+5", "5 ", "12abc": libwebsockets' own atoll accepts a prefix of these and
             * enters its body state, so reading them here as "no body" dispatched the
             * request at once and let body completion dispatch it a SECOND time. The
             * disagreement is answered instead of papered over. */
            if (length_kind == _HTTP_SERVER_CONTENT_LENGTH_INVALID) {
                log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_server: refusing a request whose Content-Length is not a decimal number");
                lws_return_http_status(wsi, HTTP_SERVER_STATUS_CODE_BAD_REQUEST, nullptr);

                return -1;
            }

            /* The declared size is refused BEFORE a byte of it is read, so an oversize
             * upload costs one header parse instead of payload_max bytes of buffering. */
            if (payload_max != HTTP_SERVER_PAYLOAD_UNLIMITED && declared_size > payload_max) {
                log_message_2(LOG_LEVEL_WARN, LOG_METADATA,
                              "http_server: refusing a declared %llu-byte body, the limit is %llu",
                              (unsigned long long) declared_size,
                              (unsigned long long) payload_max);
                _http_server_reject_payload(self, wsi);

                return -1;
            }

            /* A chunked body carries no Content-Length, and libwebsockets' HTTP/1 role
             * never delivers one through LWS_CALLBACK_HTTP_BODY in ANY build: its h1
             * parser enters the body state only from a parsed Content-Length and does no
             * request dechunking at all. Waiting for it held the connection open until
             * lws' own timeout fired, with no reply. Answer 411 so the client is told
             * what is missing instead of hanging. HTTP/2, whose role DOES accept a
             * length-less body, is never negotiated: run_tls pins ALPN to http/1.1. */
            if (length_kind == _HTTP_SERVER_CONTENT_LENGTH_ABSENT && lws_hdr_total_length(wsi, WSI_TOKEN_HTTP_TRANSFER_ENCODING) > 0) {
                log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_server: refusing a chunked request body - Content-Length is required");
                lws_return_http_status(wsi, HTTP_SERVER_STATUS_CODE_LENGTH_REQUIRED, nullptr);

                return -1;
            }

            /* Gated on whether a body PHASE will follow, not on the method alone: PUT,
             * PATCH and DELETE carry bodies too, and dispatching them without waiting left
             * the unread body in the connection to be parsed as the next request.
             *
             * `Content-Length: 0` on a POST is the awkward case. lws fires this callback,
             * then - because the length is explicitly zero and the method is POST - fires
             * HTTP_BODY(NULL, 0) and HTTP_BODY_COMPLETION anyway. Answering here meant the
             * transaction was already complete when body completion arrived, so
             * lws_http_get_uri_and_method answered -1 and a correctly-answered keep-alive
             * connection was closed. The wait is handed to body completion instead. A
             * zero length on any OTHER method gets no body phase, so it is dispatched
             * here; waiting for one would hang. */
            if (declared_size > 0 ||
                (length_kind == _HTTP_SERVER_CONTENT_LENGTH_PRESENT && method_index == LWSHUMETH_POST)) {
                /* Only RECORDED here. The single allocation it sizes is made on the first
                 * body callback: doing it here committed up to the presize ceiling for a
                 * request that had sent nothing but headers, so a few hundred idle
                 * connections could reserve hundreds of MiB without ever sending a byte. */
                if (session != nullptr) {
                    session->payload_declared_size = declared_size;
                }

                return 0;
            }

            char const *const method = _http_server_lws_method_to_str(method_index);

            _HTTP_Server_Dispatch_Status const result = _http_server_dispatch_request(self, wsi, method, uri_buffer, (USize) uri_buffer_size, nullptr, 0);

            if (result == _HTTP_SERVER_DISPATCH_STATUS_ERROR) {
                return -1;
            }

            /* lws completed the transaction inside lws_serve_http_file and asked for the
             * close. Returning 0 here instead left it re-parsing a finished transaction and
             * dispatching the same request a second time (see send_file). */
            if (result == _HTTP_SERVER_DISPATCH_STATUS_COMPLETED) {
                return -1;
            }

            if (result == _HTTP_SERVER_DISPATCH_STATUS_FILE_SERVED) {
                return 0;
            }

            return lws_http_transaction_completed(wsi);
        }

        case LWS_CALLBACK_HTTP_FILE_COMPLETION: {
            break;
        }

        case LWS_CALLBACK_HTTP_WRITEABLE: {
            break;
        }

        case LWS_CALLBACK_HTTP_BODY: {
            if (session == nullptr) {
                return -1;
            }

            if (incoming_size > 0) {
                USize const incoming_size_value = (USize) incoming_size;
                USize const payload_max_size    = _http_server_payload_max_size(self);

                if (payload_max_size != HTTP_SERVER_PAYLOAD_UNLIMITED &&
                    (session->payload_size > payload_max_size || incoming_size_value > payload_max_size - session->payload_size)) {
                    _http_server_session_reset(session);

                    log_message_2(LOG_LEVEL_WARN, LOG_METADATA,
                                  "http_server: refusing a body past the %llu-byte limit",
                                  (unsigned long long) payload_max_size);
                    _http_server_reject_payload(self, wsi);

                    return -1;
                }

                /* One allocation for the rest of the body instead of one per doubling: at
                 * the 4096-byte rx buffer a 1 MiB upload arrives in ~256 callbacks and used
                 * to cost ~8 reallocations plus their memcpys. Sized from the peer's own
                 * declaration, clamped to a ceiling as well as to the cap. Gated on
                 * payload_size, not the first callback: reserving the whole ceiling on the
                 * FIRST body byte let a peer declare a huge Content-Length and then trickle
                 * one byte per callback, holding the presize ceiling per connection on the
                 * strength of a header alone. Requiring payload_size to already have grown
                 * to HTTP_SERVER_PAYLOAD_INITIAL_CAPACITY means the ordinary doubling growth
                 * below has moved that much real data first - proving the transfer, not just
                 * the declaration. The clamp is applied BEFORE the terminator is added: a
                 * saturated USIZE_MAX declaration wrapped the sum to 0, and only
                 * memory_try_alloc(0) answering nullptr kept that benign. */
                if (session->payload_size >= HTTP_SERVER_PAYLOAD_INITIAL_CAPACITY &&
                    session->payload_declared_size > session->payload_capacity) {
                    USize const presize = math_min_u(session->payload_declared_size,
                                                     _HTTP_SERVER_PAYLOAD_PRESIZE_MAX_SIZE - CHAR_END_CHARACTER) + CHAR_END_CHARACTER;

                    if (presize > session->payload_capacity) {
                        /* Not const: memory_delete nulls it through the pointer it is handed. */
                        char *grown = (char*) memory_try_alloc(presize);

                        /* A refusal is not an error here: the body path keeps doubling. */
                        if (!memory_empty(grown)) {
                            memory_copy_2(grown, presize, session->payload, session->payload_size);
                            memory_delete((void**) &session->payload);

                            session->payload          = grown;
                            session->payload_capacity = presize;
                        }
                    }
                }

                if (session->payload_size + incoming_size_value >= session->payload_capacity) {
                    USize payload_capacity_new = session->payload_capacity == 0 ? HTTP_SERVER_PAYLOAD_INITIAL_CAPACITY : session->payload_capacity * 2;

                    while (session->payload_size + incoming_size_value >= payload_capacity_new) {
                        payload_capacity_new *= 2;
                    }

                    /* Not const: memory_delete nulls it through the pointer it is handed. */
                    char *buffer = session->payload;

                    /* try_alloc: the capacity is sized by the body the peer is
                     * sending (bounded by payload_max_size, unless the server
                     * opted into "no limit"). Refuse the request with 503 rather
                     * than end the process, the same exit shape as the 413 above.
                     * OOM branch verified by reading only - it sits inside the lws
                     * body callback, out of reach of the --wrap=calloc harnesses. */
                    session->payload = (char*) memory_try_alloc(payload_capacity_new);

                    if (memory_empty(session->payload)) {
                        if (!memory_empty(buffer)) {
                            memory_delete((void**) &buffer);
                        }

                        session->payload_size       = 0;
                        session->payload_capacity   = 0;

                        lws_return_http_status(wsi, HTTP_SERVER_STATUS_CODE_SERVICE_UNAVAILABLE, nullptr);

                        return -1;
                    }

                    if (buffer != nullptr) {
                        memory_copy_2(session->payload, payload_capacity_new, buffer, session->payload_size);
                        memory_delete((void**) &buffer);
                    }

                    session->payload_capacity = payload_capacity_new;
                }

                memory_copy_1(session->payload + session->payload_size, incoming, incoming_size_value);

                session->payload_size += incoming_size_value;
            }
            break;
        }

        case LWS_CALLBACK_HTTP_BODY_COMPLETION: {
            if (self == nullptr || session == nullptr) {
                return 0;
            }

            char        *uri_buffer     = nullptr;
            I32         uri_buffer_size = 0;
            I32 const   method_index    = lws_http_get_uri_and_method(wsi, &uri_buffer, &uri_buffer_size);

            if (method_index < 0 || uri_buffer == nullptr || uri_buffer_size < 0) {
                _http_server_session_reset(session);

                return -1;
            }

            char const *const method = _http_server_lws_method_to_str(method_index);

            _HTTP_Server_Dispatch_Status const result = _http_server_dispatch_request(self, wsi, method, uri_buffer, (USize) uri_buffer_size, session->payload, session->payload_size);

            _http_server_session_reset(session);

            if (result == _HTTP_SERVER_DISPATCH_STATUS_ERROR) {
                return -1;
            }

            /* lws completed the transaction inside lws_serve_http_file and asked for the
             * close. Returning 0 here instead left it re-parsing a finished transaction and
             * dispatching the same request a second time (see send_file). */
            if (result == _HTTP_SERVER_DISPATCH_STATUS_COMPLETED) {
                return -1;
            }

            if (result == _HTTP_SERVER_DISPATCH_STATUS_FILE_SERVED) {
                return 0;
            }

            return lws_http_transaction_completed(wsi);
        }

        case LWS_CALLBACK_CLOSED_HTTP: {
            _http_server_session_reset(session);
            break;
        }

        default: {
            break;
        }
    }

    return lws_callback_http_dummy(wsi, reason, user, incoming, incoming_size);
}

static struct lws_protocols _http_server_protocols[] = {
    {
        .name                   = "",
        .callback               = _http_server_callback_http,
        .per_session_data_size  = sizeof(HTTP_Session),
        .rx_buffer_size         = 4096,
        .id                     = 0,
        .user                   = nullptr,
        .tx_packet_size         = 0
    },
    { nullptr, nullptr, 0, 0, 0, nullptr, 0 }
};

static void _http_server_protocols_build(HTTP_Server *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (self->protocols != nullptr) {
        memory_free(self->protocols);

        self->protocols = nullptr;
    }

    USize const count = self->registered_count;

    self->protocols = (struct lws_protocols*) memory_alloc(
        memory_fit_size(sizeof(struct lws_protocols), count + 2));

    error_check_null(LOG_METADATA, "self->protocols", (void*) self->protocols);

    self->protocols[0] = _http_server_protocols[0];

    for (USize i = 0; i < count; i += 1) {
        self->protocols[i + 1] = self->registered[i];
    }

    /* The all-zero sentinel libwebsockets scans for, written explicitly rather than left to
     * the allocator. Its members are pointers, and all-bits-zero is not guaranteed by the
     * standard to be a null pointer - true on every ABI here, but this entry is read by a
     * third-party library, so it is stated rather than inferred. */
    self->protocols[count + 1] = (struct lws_protocols) DEFAULT_INITIALIZATION;

    trace_log_pop();
}

static void* _http_server_run_callback(void *data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    HTTP_Server const *const self = (HTTP_Server*) data;

    while (self->router && self->router->running) {
        lws_service((struct lws_context*) self->server, _HTTP_SERVER_SERVICE_TIMEOUT_MS);
    }

    trace_log_pop();

    return nullptr;
}

/**
 * @brief The shared tail of http_server_run and http_server_run_tls: build the protocol
 *        array, create the context, learn the bound port, then either spawn the service
 *        thread or run the loop here.
 */
static Result _http_server_start(HTTP_Server *const self, struct lws_context_creation_info *const info, bool const threaded) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "info", (void*) info);

    if (self->server != nullptr) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_server_run: refusing - the server is already running");

        trace_log_pop();

        return result_make(RESULT_CATEGORY_STATE, 1, 0);
    }

    self->router = memory_alloc(sizeof(struct HTTP_Server_Router));

    error_check_null(LOG_METADATA, "self->router", (void*) self->router);

    self->router->routes        = self->route;
    self->router->running       = true;
    self->router->is_threaded   = threaded;
    self->router->port          = info->port;

    _http_server_protocols_build(self);

    info->protocols     = self->protocols;
    info->user          = self;
    info->gid           = -1;
    info->uid           = -1;
    info->vhost_name    = _HTTP_SERVER_VHOST_NAME;

    self->server = lws_create_context(info);

    /* Not an error_check_null: a port already in use or an unreadable certificate is an
     * environment condition a caller can recover from, not a programmer error to abort on. */
    if (self->server == nullptr) {
        log_message_2(LOG_LEVEL_ERROR, LOG_METADATA, "http_server_run: libwebsockets refused to create a context on port %u", (unsigned) info->port);

        memory_delete((void**) &self->router);

#ifndef MEMORY_NON_DANGLING_POINTER
        self->router = nullptr;
#endif

        trace_log_pop();

        return result_make(RESULT_CATEGORY_NETWORK, 1, 0);
    }

    /* Port 0 asked the OS for an ephemeral port; read back what it actually bound so the
     * caller (and the suite) can connect to it. */
    struct lws_vhost *const vhost   = lws_get_vhost_by_name(self->server, _HTTP_SERVER_VHOST_NAME);
    I32 const               bound   = vhost != nullptr ? lws_get_vhost_port(vhost) : -1;

    if (bound > 0) {
        self->router->port = (U16) bound;
    }

    if (threaded) {
        Result const result = thread_create_1(&self->router->thread, _http_server_run_callback, self);

        if (result_is_error(result)) {
            log_message_2(LOG_LEVEL_ERROR, LOG_METADATA, "http_server_run: the service thread could not be started");

            self->router->running = false;

            lws_context_destroy(self->server);

            self->server = nullptr;

            memory_delete((void**) &self->router);

#ifndef MEMORY_NON_DANGLING_POINTER
            self->router = nullptr;
#endif

            trace_log_pop();

            return result;
        }
    }
    else {
        while (self->router->running) {
            lws_service(self->server, _HTTP_SERVER_SERVICE_TIMEOUT_MS);
        }
    }

    trace_log_pop();

    return RESULT_SUCCESS;
}

static bool _http_server_response_add_extra_headers(HTTP_Server_Response *const self, unsigned char **p, unsigned char *end) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "p", (void*) p);
    error_check_null(LOG_METADATA, "end", (void*) end);

    char    *const  start   = string_get_data(&self->extra_headers);
    USize   const   size    = string_get_size(&self->extra_headers);
    USize           offset  = 0;
    bool            success = true;

    if (size == 0 || start == nullptr) {
        trace_log_pop();

        return success;
    }

    // Guarded on the remainder, not just `offset < size`: char_find_slice_5 aborts when the
    // needle is longer than the haystack, so a trailing odd byte would leave a 1-byte
    // remainder and take the process down. A remainder that short cannot hold a CRLF.
    while (size - offset >= 2) {
        char const *const crlf = char_find_slice_5(start + offset, size - offset, 0, "\r\n", 2);

        if (crlf == nullptr) {
            break;
        }

        USize const line_len = crlf - (start + offset);

        if (line_len > 0) {
            char const *const colon = char_find_slice_5(start + offset, line_len, 0, ":", 1);

            if (colon != nullptr) {
                USize   const   name_size   = (colon - (start + offset)) + 1;
                char    const   *value      = colon + 1;
                USize           value_size  = line_len - name_size;

                while (value_size > 0 && *value == ' ') {
                    value       += 1;
                    value_size  -= 1;
                }

                char *const name_term = (char*) (colon + 1);
                char const original_char = *name_term;

                *name_term = '\0';

                I32 const add_res = lws_add_http_header_by_name(self->wsi, (unsigned char const*) (start + offset), (unsigned char const*) value, (I32) value_size, p, end);

                *name_term = original_char;

                if (add_res != 0) {
                    success = false;

                    break;
                }
            }
        }

        offset += line_len + 2;
    }

    trace_log_pop();

    return success;
}

/*==============================================================================
 * MARK: - Arena API
 *============================================================================*/
#ifdef ARENA_IMPLEMENTATION
void http_server_alloc_delete(HTTP_Server **const self, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "*self", (void*) *self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);
    error_check_null(LOG_METADATA, "allocator->deallocate", (void*) allocator->deallocate);

    /* The arena owns exactly ONE of the blocks a server holds: the handle itself, which
     * http_server_alloc_new took from it. Everything else - the router, the protocol
     * arrays, and every route node from http_server_route_add / route_match_add - is a
     * memory_alloc heap block. This used to walk the route list and hand each node to
     * the arena's deallocate, which for a pool arena means an alien pointer on its free
     * list and for a linear arena means a silent leak. http_server_uninit releases them
     * all through the allocator that made them; the arena is asked only for the handle. */
    http_server_uninit(*self);

    allocator->deallocate(allocator->handler, *self);

#ifdef MEMORY_NON_DANGLING_POINTER
    *self = nullptr;
#endif

    trace_log_pop();
}

HTTP_Server* http_server_alloc_new(Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    /* allocator_try_borrow, not allocator->allocate: a REFUSED arena (a null handler or a
     * null try_allocate hook) answers nullptr here instead of being called through, and an
     * exhausted one answers nullptr rather than tripping the aborting path. The
     * allocator->allocate error_check that used to stand in for this compiled away in
     * exactly the build where a null hook would be dereferenced. */
    HTTP_Server *http_server = (HTTP_Server*) allocator_try_borrow(sizeof(HTTP_Server), allocator);

    // Real control flow, not an error_check_*: an exhausted arena returns nullptr and the
    // initialization below would write through it, in exactly the build where the check
    // compiles away.
    if (http_server == nullptr) {
        trace_log_pop();

        return nullptr;
    }

    http_server_init(http_server);

    trace_log_pop();

    return http_server;
}

String http_server_alloc_request_custom_header_get_4(HTTP_Server_Request *const self, char const *const name, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    char        normalized[HTTP_SERVER_CUSTOM_HEADER_NAME_MAX_LENGTH]   = DEFAULT_INITIALIZATION;
    USize const normalized_size                                         = _http_server_custom_header_name(name, normalized, sizeof(normalized));
    I32 const   header_size                                             = normalized_size == 0 ? 0 : lws_hdr_custom_length(self->wsi, normalized, (I32) normalized_size);

    if (header_size <= 0) {
        String const string = string_alloc_init_1(allocator);

        trace_log_pop();

        return string;
    }

    String string = string_alloc_init_2((USize) header_size + CHAR_END_CHARACTER, allocator);

    if (string_get_data(&string) == nullptr) {
        trace_log_pop();

        return string;
    }

    bool const success = http_server_request_custom_header_copy(self, name, string_get_data(&string), string_get_capacity(&string));

    if (!success) {
        string_uninit(&string);

        String const empty = string_alloc_init_1(allocator);

        trace_log_pop();

        return empty;
    }

    string_set_size(&string, char_length(string_get_data(&string)));

    trace_log_pop();

    return string;
}

char *http_server_alloc_request_get_ip_1(HTTP_Server_Request *const self, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    char *const buffer                                  = char_alloc_new_1(HTTP_SERVER_IP_MAX_LENGTH - 1, allocator);
    char        peer[_HTTP_SERVER_PEER_ADDRESS_MAX_SIZE] = DEFAULT_INITIALIZATION;

    // Real control flow, not an error_check_*: that family compiles away without
    // ERROR_CHECK_ENABLED, and with it enabled the arena aborts before returning - so the
    // check could never fire in either build while the write below still deref'd null.
    if (buffer == nullptr) {
        trace_log_pop();

        return nullptr;
    }

    // Unconditional on purpose: this is a raw arena bump, and the always-zeroed guarantee
    // is owed by memory_alloc alone. An unresolvable peer leaves peer empty, the copy below
    // is skipped, and nothing else would terminate it.
    buffer[0] = '\0';

    USize const peer_size   = _http_server_peer_address(self->wsi, peer, sizeof(peer));
    USize const ip_size     = math_min_u(peer_size, HTTP_SERVER_IP_MAX_LENGTH - 1);

    if (ip_size > 0) {
        /* The capacity is the buffer's FULL size, not the requested length:
         * char_alloc_new_1(n) borrows n + CHAR_END_CHARACTER, and char_copy_3 refuses a
         * copy whose size reaches its capacity - so an address exactly
         * HTTP_SERVER_IP_MAX_LENGTH - 1 bytes long (a full-length IPv6 literal) was
         * refused here and answered as an empty string. */
        char_copy_3(buffer, HTTP_SERVER_IP_MAX_LENGTH, peer, ip_size);

        // char_copy_3 already writes a terminator at ip_size (char.c terminates every
        // copy); the explicit terminator below is redundant but harmless, and keeps this
        // path correct even if that internal changed without this comment being reread.
        buffer[ip_size] = '\0';
    }

    trace_log_pop();

    return buffer;
}

Str http_server_alloc_request_get_ip_3(HTTP_Server_Request *const self, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    char peer[_HTTP_SERVER_PEER_ADDRESS_MAX_SIZE] = DEFAULT_INITIALIZATION;

    /* One arena copy, not two: str_alloc_init_static takes its own copy, so reading the
     * peer into a stack buffer first avoids the intermediate arena buffer the _1 form
     * would have allocated only to copy out of. */
    USize   const   peer_size   = math_min_u(_http_server_peer_address(self->wsi, peer, sizeof(peer)), HTTP_SERVER_IP_MAX_LENGTH - 1);
    Str     const   str         = peer_size > 0 ? str_alloc_init_static(peer, peer_size, allocator) : str_alloc_init_1(allocator);

    trace_log_pop();

    return str;
}

String http_server_alloc_request_get_ip_4(HTTP_Server_Request *const self, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    char peer[_HTTP_SERVER_PEER_ADDRESS_MAX_SIZE] = DEFAULT_INITIALIZATION;

    USize   const   peer_size   = math_min_u(_http_server_peer_address(self->wsi, peer, sizeof(peer)), HTTP_SERVER_IP_MAX_LENGTH - 1);
    String  const   string      = peer_size > 0 ? string_alloc_init_static(peer, peer_size, allocator) : string_alloc_init_1(allocator);

    trace_log_pop();

    return string;
}

String http_server_alloc_request_header_get_4(HTTP_Server_Request *const self, HTTP_Server_Header const header, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    I32 const token = _http_server_header_token(header);

    if (token < 0) {
        String const string = string_alloc_init_1(allocator);

        trace_log_pop();

        return string;
    }

    I32 const header_size = lws_hdr_total_length(self->wsi, (enum lws_token_indexes) token);

    if (header_size <= 0) {
        String const string = string_alloc_init_1(allocator);

        trace_log_pop();

        return string;
    }

    String string = string_alloc_init_2((USize) header_size + CHAR_END_CHARACTER, allocator);

    /* An exhausted arena hands back the EMPTY String, whose data pointer is null; copying
     * a header into it would write through that null in the build where error_check_* is
     * compiled out. Answer empty instead. */
    if (string_get_data(&string) == nullptr) {
        trace_log_pop();

        return string;
    }

    bool const success = http_server_request_header_copy(self, header, string_get_data(&string), string_get_capacity(&string));

    if (!success) {
        string_uninit(&string);

        String const empty = string_alloc_init_1(allocator);

        trace_log_pop();

        return empty;
    }

    string_set_size(&string, char_length(string_get_data(&string)));

    trace_log_pop();

    return string;
}

char *http_server_alloc_route_get_query_1(HTTP_Server_Route *const self, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    char *const buffer  = char_alloc_new_1(HTTP_SERVER_QUERY_MAX_LENGTH - 1, allocator);
    USize const size    = math_min_u(self->query_value_size, HTTP_SERVER_QUERY_MAX_LENGTH - 1);

    // Real control flow, not an error_check_*: an exhausted arena returns nullptr and the
    // write below would deref it, in exactly the build where the check compiles away.
    if (buffer == nullptr) {
        trace_log_pop();

        return nullptr;
    }

    // Unconditional on purpose: this buffer is arena-backed, and the always-zeroed
    // guarantee is owed by memory_alloc alone - CFW's arenas happen to zero on recycle, but
    // Arena is a Tp vtable a third party can plug into. A zero-length query skips the copy
    // below, so nothing else would terminate it.
    buffer[0] = '\0';

    if (size > 0) {
        /* Full buffer size, as in alloc_request_get_ip_1: char_alloc_new_1(n) borrows
         * n + CHAR_END_CHARACTER, and a capacity of n would refuse an n-byte capture. */
        char_copy_3(buffer, HTTP_SERVER_QUERY_MAX_LENGTH, &self->query_value[0], size);

        // char_copy_3 already writes a terminator at size (char.c terminates every copy);
        // the explicit terminator below is redundant but harmless, and keeps this path
        // correct even if that internal changed without this comment being reread.
        buffer[size] = '\0';
    }

    trace_log_pop();

    return buffer;
}

Str http_server_alloc_route_get_query_3(HTTP_Server_Route *const self, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    /* str_alloc_init_static, not str_alloc_init_3: the _3 form builds a VIEW, so wrapping a
     * fresh arena buffer in it produced a Str documented as owned that str_uninit would
     * never release. */
    USize   const   size    = math_min_u(self->query_value_size, HTTP_SERVER_QUERY_MAX_LENGTH - 1);
    Str     const   str     = size > 0 ? str_alloc_init_static(&self->query_value[0], size, allocator) : str_alloc_init_1(allocator);

    trace_log_pop();

    return str;
}

String http_server_alloc_route_get_query_4(HTTP_Server_Route *const self, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    USize   const   size    = math_min_u(self->query_value_size, HTTP_SERVER_QUERY_MAX_LENGTH - 1);
    String  const   string  = size > 0 ? string_alloc_init_static(&self->query_value[0], size, allocator) : string_alloc_init_1(allocator);

    trace_log_pop();

    return string;
}
#endif // ARENA_IMPLEMENTATION

/*==============================================================================
 * MARK: - Server API
 *============================================================================*/
void http_server_delete(HTTP_Server **const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "*self", (void*) *self);

    http_server_uninit(*self);

    memory_delete((void**) self);

    trace_log_pop();
}

U16 http_server_get_port(HTTP_Server *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    U16 port = 0;

    if (self->router != nullptr) {
        port = self->router->port;
    }

    trace_log_pop();

    return port;
}

bool http_server_init(HTTP_Server *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    self->route             = nullptr;
    self->router            = nullptr;
    self->server            = nullptr;
    self->payload_max_size  = HTTP_SERVER_PAYLOAD_DEFAULT_MAX_LENGTH;
    self->payload_limit_body = nullptr;
    self->payload_limit_type = nullptr;
    self->protocols         = nullptr;
    self->registered        = nullptr;
    self->registered_count  = 0;
    self->handler           = nullptr;
    self->handler_context   = nullptr;

    trace_log_pop();

    return true;
}

HTTP_Server* http_server_new(void) {
    trace_log_push(LOG_METADATA);

    HTTP_Server *http_server = memory_alloc(sizeof(HTTP_Server));

    http_server_init(http_server);

    trace_log_pop();

    return http_server;
}

bool http_server_register_protocol(HTTP_Server *const self, struct lws_protocols const *const protocol) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "protocol", (void*) protocol);

    if (self->server != nullptr || self->registered_count == HTTP_SERVER_PROTOCOLS_MAX) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_server_register_protocol: refusing - the server is running or the registry is full");

        trace_log_pop();

        return false;
    }

    if (self->registered == nullptr) {
        self->registered = (struct lws_protocols*) memory_alloc(
            memory_fit_size(sizeof(struct lws_protocols), HTTP_SERVER_PROTOCOLS_MAX));

        error_check_null(LOG_METADATA, "self->registered", (void*) self->registered);
    }

    self->registered[self->registered_count] = *protocol;
    self->registered_count += 1;

    trace_log_pop();

    return true;
}

Result http_server_run(HTTP_Server *const self, U16 const port, bool const threaded) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    struct lws_context_creation_info info = DEFAULT_INITIALIZATION;

    info.port = port;

    Result const result = _http_server_start(self, &info, threaded);

    trace_log_pop();

    return result;
}

Result http_server_run_tls(HTTP_Server *const self, char const *const key, char const *const cert, U16 const port, bool const threaded) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "key", (void*) key);
    error_check_null(LOG_METADATA, "cert", (void*) cert);

    struct lws_context_creation_info info = DEFAULT_INITIALIZATION;

    info.port                       = port;
    info.options                    = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.ssl_cert_filepath          = cert;
    info.ssl_private_key_filepath   = key;
    /* HTTP/1.1 only, stated rather than inherited. libwebsockets' HTTP/2 role enters its
     * body state with NO Content-Length when END_STREAM is unset and fires
     * LWS_CALLBACK_HTTP before the DATA frames, so a length-less POST would be dispatched
     * with an empty body and then dispatched a second time at body completion - and the
     * 411 that covers HTTP/1 cannot see it, because h2 carries no Transfer-Encoding
     * header to refuse on.
     *
     * MEASURED on the vendored lws 4.5.99 with this line removed: `curl --http2 -k`
     * offers "h2,http/1.1" and the server still accepts http/1.1 (openssl s_client
     * agrees: "ALPN protocol: http/1.1"), so a chunked h2 POST is downgraded and
     * correctly answered 411. h2 is therefore not reachable on this build even unpinned -
     * but that is a property of how this vhost happens to be assembled, not of the 4.3.5
     * floor the module promises. The pin makes the documented behaviour the same on every
     * build instead of incidentally true on one, and costs nothing where h2 was already
     * not being negotiated. */
    info.alpn                       = "http/1.1";

    Result const result = _http_server_start(self, &info, threaded);

    trace_log_pop();

    return result;
}

void http_server_set_handler(HTTP_Server *const self, HTTP_Server_Request_Callback const handler, void *const context) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    self->handler           = handler;
    self->handler_context   = context;

    trace_log_pop();
}

void http_server_set_log_level(I32 const level) {
    trace_log_push(LOG_METADATA);

    /* Process-wide, and deliberately so: libwebsockets keeps one log sink for the whole
     * process. Routing it through _http_server_log_emit means lws diagnostics land in the
     * framework's log with everything else rather than on a bare stderr. */
    lws_set_log_level(level, _http_server_log_emit);

    trace_log_pop();
}

void http_server_set_payload_limit_response(HTTP_Server *const self, char const *const body, char const *const content_type) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "body", (void*) body);
    error_check_null(LOG_METADATA, "content_type", (void*) content_type);

    self->payload_limit_body = body;
    self->payload_limit_type = content_type;

    trace_log_pop();
}

void http_server_set_payload_max_size(HTTP_Server *const self, USize const size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* 0 used to mean "no limit", which is the wrong way for an accidental zero to fail.
     * It now restores the default cap; HTTP_SERVER_PAYLOAD_UNLIMITED spells the old
     * meaning out. */
    self->payload_max_size = size == 0 ? HTTP_SERVER_PAYLOAD_DEFAULT_MAX_LENGTH : size;

    trace_log_pop();
}

void http_server_stop(HTTP_Server *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (self->router == nullptr) {
        trace_log_pop();

        return;
    }

    self->router->running = false;

    if (self->server != nullptr) {
        /* Wakes the service loop out of its poll immediately, so a stop does not wait for
         * the current _HTTP_SERVER_SERVICE_TIMEOUT_MS window to expire. */
        lws_cancel_service(self->server);
    }

    trace_log_pop();
}

void http_server_uninit(HTTP_Server *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    http_server_stop(self);

    if (self->router != nullptr && self->router->is_threaded) {
        thread_join_1(&self->router->thread);
    }

    if (self->server != nullptr) {
        lws_context_destroy(self->server);

        self->server = nullptr;
    }

    if (self->protocols != nullptr) {
        memory_free(self->protocols);

        self->protocols = nullptr;
    }

    if (self->registered != nullptr) {
        memory_free(self->registered);

        self->registered = nullptr;
    }

    if (self->router != nullptr) {
        memory_delete((void**) &self->router);

#ifndef MEMORY_NON_DANGLING_POINTER
        /* memory_delete nulls through the pointer it is handed only in the non-dangling
         * build. Everywhere else the handle would keep pointing at freed router state,
         * and http_server_stop reads self->router first thing - the same reason
         * _http_server_session_reset nulls its payload explicitly. */
        self->router = nullptr;
#endif
    }

    HTTP_Server_Route *current = self->route;

    while (current != nullptr) {
        HTTP_Server_Route const *const next = current->next;

        regex_uninit(&current->regex);
        memory_delete((void**) &current);

        current = (HTTP_Server_Route*) next;
    }

    self->route             = nullptr;
    self->registered_count  = 0;

    trace_log_pop();
}

/*==============================================================================
 * MARK: - Response API Accessors
 *============================================================================*/
void* http_server_response_get_client(HTTP_Server_Response *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return (void*) self->wsi;
}

String const* http_server_response_get_extra_headers(HTTP_Server_Response const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return &self->extra_headers;
}

bool http_server_response_header_add(HTTP_Server_Response *const self, char const *const key, char const *const value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "key", (void*) key);
    error_check_null(LOG_METADATA, "value", (void*) value);

    /* Response splitting: a CR or LF anywhere in the name or the value ends the header
     * block early, and everything after it is read by the client as a response of its
     * own. Refused as a value, loudly, rather than written to the wire. */
    if (!_http_server_header_field_valid(key) || !_http_server_header_field_valid(value)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_server_response_header_add: refusing '%s' - the name or value carries CR, LF or a control byte", key);

        trace_log_pop();

        return false;
    }

    char buffer[HTTP_SERVER_RESPONSE_HEADER_MAX_LENGTH] = DEFAULT_INITIALIZATION;

    I32 const written = snprintf(buffer, sizeof(buffer), "%s: %s\r\n", key, value);

    /* snprintf answers what it WOULD have written, so anything at or past the buffer size
     * means the line was truncated. A silently dropped Set-Cookie or CSP is a security
     * header the caller believes it sent. */
    if (written < 0 || (USize) written >= sizeof(buffer)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_server_response_header_add: refusing '%s' - the line exceeds %llu bytes", key, (unsigned long long) sizeof(buffer) - 1);

        trace_log_pop();

        return false;
    }

    http_server_response_header_add_raw(self, buffer, (USize) written);

    trace_log_pop();

    return true;
}

void http_server_response_header_add_raw(HTTP_Server_Response *const self, char const *const headers, USize const headers_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "headers", (void*) headers);

    if (headers_size > 0) {
        string_add_last_2(&self->extra_headers, (char*) headers, headers_size);
    }

    trace_log_pop();
}

void http_server_response_set_file_served(HTTP_Server_Response *const self, bool const served) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    self->file_served = served;

    trace_log_pop();
}

void http_server_response_set_write_success(HTTP_Server_Response *const self, bool const success) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    self->write_success = success;

    trace_log_pop();
}

/*==============================================================================
 * MARK: - Route API
 *============================================================================*/
bool http_server_route_add(HTTP_Server *const self, char const *const path, HTTP_Server_Route_Callback callback) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "path", (void*) path);
    error_check_null(LOG_METADATA, "callback", (void*) callback);

    USize const path_size = char_length(path);

    /* Truncation used to be silent, so a long registered path quietly became a DIFFERENT
     * route than the one written in the source. */
    if (path_size == 0 || path_size >= HTTP_SERVER_PATH_MAX_LENGTH) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_server_route_add: refusing a %llu-byte path, the limit is %d", (unsigned long long) path_size, HTTP_SERVER_PATH_MAX_LENGTH - 1);

        trace_log_pop();

        return false;
    }

    HTTP_Server_Route *route_new = memory_alloc(sizeof(HTTP_Server_Route));

    char_copy_3(route_new->path, HTTP_SERVER_PATH_MAX_LENGTH, path, path_size);

    route_new->path_size    = (U16) path_size;
    route_new->callback     = callback;
    route_new->regex        = regex_init();
    route_new->next         = self->route;
    self->route             = route_new;

    trace_log_pop();

    return true;
}

HTTP_Server_Holder* http_server_route_get_holder(HTTP_Server_Route *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return &self->holder;
}

char* http_server_route_get_path(HTTP_Server_Route *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->path;
}

char* http_server_route_get_query_1(HTTP_Server_Route *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    // Sized to the query actually held (clamped to the module max), not a fixed 256
    // bytes every call regardless of content - the _3/_4 tiers call this and inherit
    // the same right-sized allocation.
    USize const buffer_size = math_min_u(self->query_value_size, HTTP_SERVER_QUERY_MAX_LENGTH - 1);
    char *const buffer      = buffer_size > 0 ? char_new_1(buffer_size) : char_new_2("");

    if (buffer == nullptr) {
        trace_log_pop();

        return nullptr;
    }

    // No terminator is written anywhere here: char_new_1 with no arena routes to
    // memory_alloc, which always zeroes, so the byte past the copy is already NUL.
    // Full buffer size, not the requested length: char_new_1(n) borrows n +
    // CHAR_END_CHARACTER, and char_copy_3 refuses a copy whose size reaches its capacity.
    if (buffer_size > 0) {
        char_copy_3(buffer, buffer_size + CHAR_END_CHARACTER, &self->query_value[0], buffer_size);
    }

    trace_log_pop();

    return buffer;
}

Str http_server_route_get_query_3(HTTP_Server_Route *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* str_move_2 adopts the buffer; str_init_3 would have built a VIEW over it and the
     * allocation would leak past str_uninit. */
    char    *buffer     = http_server_route_get_query_1(self);
    USize   const size  = math_min_u(self->query_value_size, HTTP_SERVER_QUERY_MAX_LENGTH - 1);
    Str     str         = str_init_1();

    if (buffer != nullptr) {
        str_move_2(&str, &buffer, size);
    }

    trace_log_pop();

    return str;
}

String http_server_route_get_query_4(HTTP_Server_Route *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* The getter hands back a fresh buffer, so ownership has to be taken explicitly:
     * string_init_3 builds a VIEW and would strand the allocation. */
    char    *buffer = http_server_route_get_query_1(self);
    String  string  = string_init_1();

    if (buffer != nullptr) {
        string_move_2(&string, &buffer, char_length(buffer));
    }

    trace_log_pop();

    return string;
}

Str http_server_route_get_query_static_3(HTTP_Server_Route *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    USize   const   size    = math_min_u(self->query_value_size, HTTP_SERVER_QUERY_MAX_LENGTH - 1);
    Str     const   str     = size > 0 ? str_init_3(self->query_value, size) : str_init_1();

    trace_log_pop();

    return str;
}

bool http_server_route_match_add(HTTP_Server *const self, char const *const path, HTTP_Server_Route_Callback callback) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "path", (void*) path);
    error_check_null(LOG_METADATA, "callback", (void*) callback);

    USize const path_size = char_length(path);

    if (path_size == 0 || path_size >= HTTP_SERVER_PATH_MAX_LENGTH) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_server_route_match_add: refusing a %llu-byte pattern, the limit is %d",
                      (unsigned long long) path_size, HTTP_SERVER_PATH_MAX_LENGTH - 1);

        trace_log_pop();

        return false;
    }

    /* No second size guard here: the pattern is already shorter than
     * HTTP_SERVER_PATH_MAX_LENGTH (256), and the anchored form costs +2 for the ^ and $
     * spliced on plus +1 for the terminator - 258 at worst, against the 512-byte buffer
     * below. A guard on that could never fire; the static assert states the relationship
     * instead, so shrinking the buffer or growing the path limit fails the BUILD. */
    static_assert(HTTP_SERVER_PATH_MAX_LENGTH + 2 <= _HTTP_SERVER_ROUTE_REGEX_MAX_SIZE,
                  "the anchored form of the longest legal route pattern must fit the regex buffer");

    char    route_regex[_HTTP_SERVER_ROUTE_REGEX_MAX_SIZE]  = DEFAULT_INITIALIZATION;
    U16     route_regex_size                                = 0;

    char_add_last_fixed_4(route_regex, _HTTP_SERVER_ROUTE_REGEX_MAX_SIZE, route_regex_size, "^", CHAR_STATIC_SIZE("^"));

    route_regex_size += 1;

    char_add_last_fixed_4(route_regex, _HTTP_SERVER_ROUTE_REGEX_MAX_SIZE, route_regex_size, path, path_size);

    route_regex_size += (U16) path_size;

    char_add_last_fixed_4(route_regex, _HTTP_SERVER_ROUTE_REGEX_MAX_SIZE, route_regex_size, "$", CHAR_STATIC_SIZE("$"));

    HTTP_Server_Route *route_new = memory_alloc(sizeof(HTTP_Server_Route));

    route_new->regex = regex_init();

    /* regex_compile_try, not regex_compile_1: a public route_add answers false on a
     * pattern it cannot compile instead of ending the process. */
    if (!regex_compile_try(&route_new->regex, route_regex)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_server_route_match_add: refusing '%s' - the pattern does not compile", path);

        regex_uninit(&route_new->regex);
        memory_delete((void**) &route_new);

        trace_log_pop();

        return false;
    }

    char_copy_3(route_new->path, HTTP_SERVER_PATH_MAX_LENGTH, path, path_size);

    route_new->path_size    = (U16) path_size;
    route_new->callback     = callback;
    route_new->next         = self->route;
    self->route             = route_new;

    trace_log_pop();

    return true;
}

/*==============================================================================
 * MARK: - Request API
 *============================================================================*/
bool http_server_request_content_type_is(HTTP_Server_Request *const self, char const *const mime) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "mime", (void*) mime);

    char buffer[_HTTP_SERVER_CONTENT_TYPE_MAX_SIZE] = DEFAULT_INITIALIZATION;

    if (!http_server_request_header_copy(self, HTTP_SERVER_HEADER_CONTENT_TYPE, buffer, _HTTP_SERVER_CONTENT_TYPE_MAX_SIZE)) {
        trace_log_pop();

        return false;
    }

    USize   const   buffer_size = char_length(buffer);
    USize   const   mime_size   = char_length(mime);
    USize           index       = 0;

    char_lower_2(buffer, buffer_size);

    while (index < buffer_size && char_is_whitespace(buffer[index])) {
        index += 1;
    }

    if (index + mime_size > buffer_size) {
        trace_log_pop();

        return false;
    }

    bool const match    = char_compare_equal_2(buffer + index, mime_size, mime, mime_size);
    char const next     = buffer[index + mime_size];
    bool const success  = match && (next == '\0' || next == ';' || char_is_whitespace(next));

    trace_log_pop();

    return success;
}

bool http_server_request_custom_header_copy(HTTP_Server_Request *const self, char const *const name, char *const buffer, USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);
    error_check_null(LOG_METADATA, "buffer", (void*) buffer);
    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    buffer[0] = '\0';

    char        normalized[HTTP_SERVER_CUSTOM_HEADER_NAME_MAX_LENGTH]   = DEFAULT_INITIALIZATION;
    USize const normalized_size                                         = _http_server_custom_header_name(name, normalized, sizeof(normalized));

    if (normalized_size == 0) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_server_request_custom_header_copy: refusing a header name longer than %d bytes", HTTP_SERVER_CUSTOM_HEADER_NAME_MAX_LENGTH - 1);

        trace_log_pop();

        return false;
    }

    I32 const name_size     = (I32) normalized_size;
    I32 const header_size   = lws_hdr_custom_length(self->wsi, normalized, name_size);

    if (header_size <= 0 || (USize) header_size >= capacity) {
        trace_log_pop();

        return false;
    }

    I32 const copy_size = lws_hdr_custom_copy(self->wsi, buffer, (I32) math_min_u(capacity, (USize) I32_MAX), normalized, name_size);

    if (copy_size <= 0 || (USize) copy_size >= capacity) {
        buffer[0] = '\0';

        trace_log_pop();

        return false;
    }

    buffer[copy_size] = '\0';

    trace_log_pop();

    return true;
}

String http_server_request_custom_header_get_4(HTTP_Server_Request *const self, char const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    char        normalized[HTTP_SERVER_CUSTOM_HEADER_NAME_MAX_LENGTH]   = DEFAULT_INITIALIZATION;
    USize const normalized_size                                         = _http_server_custom_header_name(name, normalized, sizeof(normalized));
    I32 const   header_size                                             = normalized_size == 0 ? 0 : lws_hdr_custom_length(self->wsi, normalized, (I32) normalized_size);

    if (header_size <= 0) {
        String const string = string_init_1();

        trace_log_pop();

        return string;
    }

    String          string  = string_init_2((USize) header_size + CHAR_END_CHARACTER);
    bool    const   success = http_server_request_custom_header_copy(self, name, string_get_data(&string), string_get_capacity(&string));

    if (!success) {
        string_uninit(&string);

        String const empty = string_init_1();

        trace_log_pop();

        return empty;
    }

    string_set_size(&string, char_length(string_get_data(&string)));

    trace_log_pop();

    return string;
}

Byte* http_server_request_get_body(HTTP_Server_Request *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    Byte *const data = (Byte*) string_get_data(&self->payload);

    trace_log_pop();

    return data;
}

USize http_server_request_get_body_size(HTTP_Server_Request *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    USize const size = string_get_size(&self->payload);

    trace_log_pop();

    return size;
}

void* http_server_request_get_client(HTTP_Server_Request *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return (void*) self->wsi;
}

bool http_server_request_get_client_ip(HTTP_Server_Request *const self, USize const trusted_hops, char *const buffer, USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "buffer", (void*) buffer);
    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    buffer[0] = '\0';

    char        peer[_HTTP_SERVER_PEER_ADDRESS_MAX_SIZE] = DEFAULT_INITIALIZATION;
    USize const peer_size = _http_server_peer_address(self->wsi, peer, sizeof(peer));

    /* No trusted proxy in front of this server means X-Forwarded-For is whatever the peer
     * chose to claim; the transport address is the only address that is not forgeable. */
    if (trusted_hops == 0) {
        bool const written = _http_server_address_write(buffer, capacity, peer, peer_size);

        trace_log_pop();

        return written;
    }

    /* A hop count past the fixed array is a CONFIGURATION error, decidable before a byte of
     * the header is parsed - so it is answered here rather than discovered mid-scan.
     * Answering the peer here handed a hops >= 1 caller the PROXY's address, the same
     * mistake the over-long-header case below was fixed for; false is the honest answer
     * for a misconfiguration this module cannot service. */
    if (trusted_hops > _HTTP_SERVER_FORWARDED_MAX_HOPS) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA,
                      "http_server_request_get_client_ip: %llu trusted hops is past the %d the module reads",
                      (unsigned long long) trusted_hops, _HTTP_SERVER_FORWARDED_MAX_HOPS);

        trace_log_pop();

        return false;
    }

    char forwarded[_HTTP_SERVER_FORWARDED_MAX_SIZE] = DEFAULT_INITIALIZATION;

    /* An OVER-LONG header is not an absent one. Falling back to the peer here handed the
     * caller the PROXY's address - trusted_hops >= 1 says the peer IS the proxy - so a
     * client had only to pad X-Forwarded-For past this buffer to escape an ip_block on its
     * own address and land every strike it earned on the proxy, blocking everyone behind
     * it. lws' default max_http_header_data (4096) makes such a header deliverable.
     * "Unidentifiable" is the honest answer for a value that keys blocks. */
    if (lws_hdr_total_length(self->wsi, WSI_TOKEN_X_FORWARDED_FOR) >= (I32) sizeof(forwarded)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA,
                      "http_server_request_get_client_ip: refusing an X-Forwarded-For of %d bytes, the limit is %d",
                      lws_hdr_total_length(self->wsi, WSI_TOKEN_X_FORWARDED_FOR), _HTTP_SERVER_FORWARDED_MAX_SIZE - 1);

        trace_log_pop();

        return false;
    }

    if (!http_server_request_header_copy(self, HTTP_SERVER_HEADER_X_FORWARDED_FOR, forwarded, sizeof(forwarded))) {
        bool const written = _http_server_address_write(buffer, capacity, peer, peer_size);

        trace_log_pop();

        return written;
    }

    /* Split from the RIGHT, keeping only the entries the hop count actually needs.
     * Collecting the LEFTMOST _HTTP_SERVER_FORWARDED_MAX_HOPS entries and then indexing
     * from the right meant a peer could send that many short fakes of its own and push
     * the entry the nearest trusted proxy appended - the one address in the list that is
     * not forgeable - past the end of the array; the reported client was then a value the
     * peer had chosen. Nothing further left than the hop count is ever looked at now.
     *
     * Exactly trusted_hops entries are collected - no more. Taking trusted_hops + 1 meant
     * hop 31 on a 33-entry list filled the 32-slot array with the list still running to the
     * left, and was refused although the entry asked for was already in hand. */
    USize const wanted = trusted_hops;

    char    *entry_data[_HTTP_SERVER_FORWARDED_MAX_HOPS] = DEFAULT_INITIALIZATION;
    USize   entry_size[_HTTP_SERVER_FORWARDED_MAX_HOPS]  = DEFAULT_INITIALIZATION;
    USize   entry_count                                  = 0;
    USize   cursor                                       = char_length(forwarded);

    while (entry_count < wanted) {
        USize stop = cursor;

        while (cursor > 0 && forwarded[cursor - 1] != ',') {
            cursor -= 1;
        }

        USize start = cursor;

        while (start < stop && char_is_whitespace(forwarded[start])) {
            start += 1;
        }

        while (stop > start && char_is_whitespace(forwarded[stop - 1])) {
            stop -= 1;
        }

        /* An empty element is not a hop; it is skipped without consuming a slot, so
         * "a,,b" counts two hops exactly as "a,b" does. */
        if (stop > start) {
            entry_data[entry_count] = &forwarded[start];
            entry_size[entry_count] = stop - start;
            entry_count += 1;
        }

        if (cursor == 0) {
            break;
        }

        cursor -= 1;
    }

    if (entry_count == 0) {
        bool const written = _http_server_address_write(buffer, capacity, peer, peer_size);

        trace_log_pop();

        return written;
    }

    /* Counting from the right: hop 1 is the last X-Forwarded-For entry (what the nearest
     * trusted proxy appended), hop 2 the one before it. Asking for more hops than the list
     * holds clamps to the leftmost entry rather than walking off it. */
    USize const index = trusted_hops <= entry_count ? trusted_hops - 1 : entry_count - 1;

    /* The selected entry must be an address, not merely text a peer put in the list.
     * "unknown", "_hidden" and RFC 7239 node identifiers are all legal to WRITE there, and
     * this value goes on to key rate limiters and IP blocks - so an entry that is not an
     * address literal is UNIDENTIFIABLE, the same answer the over-long-header case above
     * gives for the same reason: with trusted_hops >= 1 the peer IS the proxy, so falling
     * back to it would still hand out an address the client's malformed entry chose to
     * dodge. */
    if (!_http_server_address_literal_valid(entry_data[index], entry_size[index])) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA,
                      "http_server_request_get_client_ip: X-Forwarded-For hop %llu is not an address literal",
                      (unsigned long long) trusted_hops);

        trace_log_pop();

        return false;
    }

    bool const written = _http_server_address_write(buffer, capacity, entry_data[index], entry_size[index]);

    trace_log_pop();

    return written;
}

char* http_server_request_get_ip_1(HTTP_Server_Request *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    char *const buffer                                  = memory_alloc(HTTP_SERVER_IP_MAX_LENGTH);
    char        peer[_HTTP_SERVER_PEER_ADDRESS_MAX_SIZE] = DEFAULT_INITIALIZATION;

    // No terminator is written anywhere here: memory_alloc always zeroes, so both an
    // unresolved peer (nothing copied) and a short copy leave a NUL already in place.
    USize const peer_size   = _http_server_peer_address(self->wsi, peer, sizeof(peer));
    USize const ip_size     = math_min_u(peer_size, HTTP_SERVER_IP_MAX_LENGTH - 1);

    if (ip_size > 0) {
        char_copy_3(buffer, HTTP_SERVER_IP_MAX_LENGTH, peer, ip_size);

        // char_copy_3 already writes a terminator at ip_size; no explicit buffer[ip_size]
        // = '\0' is needed here, and memory_alloc's zeroed tail would have covered it
        // either way - an unterminated "IP" would otherwise trail garbage into the
        // rate-limit and ip-block keys and hash the same client differently per request.
    }

    trace_log_pop();

    return buffer;
}

Str http_server_request_get_ip_3(HTTP_Server_Request *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* str_move_2 adopts the buffer; str_init_2 built a VIEW over it, so the allocation
     * outlived every str_uninit a caller could reach for. */
    char    *buffer = http_server_request_get_ip_1(self);
    Str     str     = str_init_1();

    if (buffer != nullptr) {
        str_move_2(&str, &buffer, char_length(buffer));
    }

    trace_log_pop();

    return str;
}

String http_server_request_get_ip_4(HTTP_Server_Request *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* The getter hands back a fresh buffer, so ownership has to be taken explicitly:
     * string_init_3 builds a VIEW and would strand the allocation. */
    char    *buffer = http_server_request_get_ip_1(self);
    String  string  = string_init_1();

    if (buffer != nullptr) {
        string_move_2(&string, &buffer, char_length(buffer));
    }

    trace_log_pop();

    return string;
}

char* http_server_request_get_method_1(HTTP_Server_Request *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->method;
}

Str http_server_request_get_method_3(HTTP_Server_Request *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    Str const str = self->method_size > 0 ? str_init_3(self->method, self->method_size) : str_init_1();

    trace_log_pop();

    return str;
}

String http_server_request_get_method_4(HTTP_Server_Request *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* string_init_static, not string_init_4: the _4 form is a VIEW over the request's own
     * stack buffer, and this getter is documented as answering an owned String. */
    String const string = self->method_size > 0 ? string_init_static(self->method, self->method_size) : string_init_1();

    trace_log_pop();

    return string;
}

char* http_server_request_get_path_1(HTTP_Server_Request *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->path;
}

Str http_server_request_get_path_3(HTTP_Server_Request *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    Str const str = self->path_size > 0 ? str_init_3(self->path, self->path_size) : str_init_1();

    trace_log_pop();

    return str;
}

String http_server_request_get_path_4(HTTP_Server_Request *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    String const string = self->path_size > 0 ? string_init_static(self->path, self->path_size) : string_init_1();

    trace_log_pop();

    return string;
}

USize http_server_request_get_path_size(HTTP_Server_Request *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    USize const size = self->path_size;

    trace_log_pop();

    return size;
}

char* http_server_request_get_payload_1(HTTP_Server_Request *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return string_get_data(&self->payload);
}

Str http_server_request_get_payload_3(HTTP_Server_Request *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    Str str = string_get_size(&self->payload) > 0 ? str_init_3(string_get_data(&self->payload), string_get_size(&self->payload)) : str_init_1();

    trace_log_pop();

    return str;
}

String http_server_request_get_payload_4(HTTP_Server_Request *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* An OWNED copy. Returning self->payload handed back a VIEW over the connection's own
     * body buffer, so the caller's string_uninit freed memory the session still owned. */
    USize   const   size    = string_get_size(&self->payload);
    String  const   string  = size > 0 ? string_init_static(string_get_data(&self->payload), size) : string_init_1();

    trace_log_pop();

    return string;
}

bool http_server_request_header_copy(HTTP_Server_Request *const self, HTTP_Server_Header const header, char *const buffer, USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "buffer", (void*) buffer);
    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    buffer[0] = '\0';

    I32 const token = _http_server_header_token(header);

    if (token < 0) {
        trace_log_pop();

        return false;
    }

    I32 const header_size = lws_hdr_total_length(self->wsi, (enum lws_token_indexes) token);

    if (header_size <= 0 || (USize) header_size >= capacity) {
        trace_log_pop();

        return false;
    }

    /* Clamped, not cast: capacity is the caller's USize and lws takes an int, so a buffer
     * larger than I32_MAX would arrive as a negative length. */
    I32 const copy_size = lws_hdr_copy(self->wsi, buffer, (I32) math_min_u(capacity, (USize) I32_MAX), (enum lws_token_indexes) token);

    if (copy_size <= 0 || (USize) copy_size >= capacity) {
        buffer[0] = '\0';

        trace_log_pop();

        return false;
    }

    buffer[copy_size] = '\0';

    trace_log_pop();

    return true;
}

String http_server_request_header_get_4(HTTP_Server_Request *const self, HTTP_Server_Header const header) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    I32 const token = _http_server_header_token(header);

    if (token < 0) {
        String const string = string_init_1();

        trace_log_pop();

        return string;
    }

    I32 const header_size = lws_hdr_total_length(self->wsi, (enum lws_token_indexes) token);

    if (header_size <= 0) {
        String const string = string_init_1();

        trace_log_pop();

        return string;
    }

    String          string  = string_init_2((USize) header_size + CHAR_END_CHARACTER);
    bool    const   success = http_server_request_header_copy(self, header, string_get_data(&string), string_get_capacity(&string));

    if (!success) {
        string_uninit(&string);

        String const empty = string_init_1();

        trace_log_pop();

        return empty;
    }

    string_set_size(&string, char_length(string_get_data(&string)));

    trace_log_pop();

    return string;
}

bool http_server_request_query_copy(HTTP_Server_Request *const self, char *const buffer, USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "buffer", (void*) buffer);
    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    buffer[0] = '\0';

    I32 const args_size = lws_hdr_total_length(self->wsi, WSI_TOKEN_HTTP_URI_ARGS);

    if (args_size <= 0 || (USize) args_size >= capacity) {
        trace_log_pop();

        return false;
    }

    I32 const copy_size = lws_hdr_copy(self->wsi, buffer, (I32) math_min_u(capacity, (USize) I32_MAX), WSI_TOKEN_HTTP_URI_ARGS);

    if (copy_size <= 0 || (USize) copy_size >= capacity) {
        buffer[0] = '\0';

        trace_log_pop();

        return false;
    }

    buffer[copy_size] = '\0';

    trace_log_pop();

    return true;
}

/*==============================================================================
 * MARK: - Response API
 *============================================================================*/
void http_server_response_send_1(HTTP_Server_Response *const self, char const *const data, char const *const content_type, U16 const status_code) {
    trace_log_push(LOG_METADATA);

    http_server_response_send_2(self, (Byte const*) data, data != nullptr ? char_length(data) : 0, content_type, status_code);

    trace_log_pop();
}

void http_server_response_send_2(HTTP_Server_Response *const self, Byte const *const data, USize const data_size, char const *const content_type, U16 const status_code) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "content_type", (void*) content_type);

    if (self->headers_sent) {
        trace_log_pop();

        return;
    }

    self->status_code = status_code;

    /* RFC 9110 SS8.6: a server MUST NOT send Content-Length on 1xx or 204, and MUST NOT
     * on 304 unless it equals the 200's length - which send_2 never knows, so absence is
     * the only correct answer. All three also carry no body, even if the caller passed
     * one: a 304's "representation" is the cached copy, not this reply's data_size. */
    bool const no_body = status_code < 200 ||
                          status_code == HTTP_SERVER_STATUS_CODE_NO_CONTENT ||
                          status_code == HTTP_SERVER_STATUS_CODE_NOT_MODIFIED;

    Byte    header[_HTTP_SERVER_RESPONSE_HEADER_SIZE + LWS_PRE]  = DEFAULT_INITIALIZATION;
    Byte    *p                                                   = &header[LWS_PRE];
    Byte    *end                                                 = &header[sizeof(header) - 1];

    if (lws_add_http_header_status(self->wsi, status_code, &p, end) ||
        lws_add_http_header_by_token(self->wsi, WSI_TOKEN_HTTP_CONTENT_TYPE, (Byte const*) content_type, (I32) char_length(content_type), &p, end) ||
        (!no_body && lws_add_http_header_content_length(self->wsi, data_size, &p, end)) ||
        !_http_server_response_add_extra_headers(self, &p, end) ||
        lws_finalize_http_header(self->wsi, &p, end)) {
        /* The header block did not fit (2048 bytes, extra headers included). This used to
         * return with write_success still true, so the transaction "completed" having
         * written nothing at all and the client read an empty reply. */
        log_message_2(LOG_LEVEL_ERROR, LOG_METADATA, "http_server_response_send_2: the response headers exceed %d bytes; nothing was written", _HTTP_SERVER_RESPONSE_HEADER_SIZE);

        self->write_success = false;

        trace_log_pop();

        return;
    }

    I32 const write_h       = lws_write(self->wsi, &header[LWS_PRE], (USize) (p - &header[LWS_PRE]), LWS_WRITE_HTTP_HEADERS);
    I32 const header_size   = (I32) (p - &header[LWS_PRE]);

    if (write_h < 0 || write_h != header_size) {
        log_message_2(LOG_LEVEL_ERROR, LOG_METADATA, "http_server_response_send_2: header write returned %d, expected %d", write_h, header_size);

        self->write_success = false;

        trace_log_pop();

        return;
    }

    self->headers_sent = true;

    /* HEAD asks for the headers a GET would produce and no body at all. Content-Length
     * above still reports the size the body would have had, which is what the method is
     * for; writing the body itself would make the reply unreadable to the client. */
    if (self->is_head) {
        trace_log_pop();

        return;
    }

    if (!no_body && data != nullptr && data_size > 0) {
        /* try_alloc: data_size is the caller's, and a route body often echoes
         * request-derived content. Report the write as failed, exactly as a
         * failed header write is reported above, instead of ending the process.
         * OOM branch verified by reading only - the writer needs a live wsi. */
        /* Not const: memory_delete nulls it through the pointer it is handed. */
        Byte *body = (Byte*) memory_try_alloc(LWS_PRE + data_size);

        if (memory_empty(body)) {
            log_message_2(LOG_LEVEL_ERROR, LOG_METADATA, "http_server_response_send_2: no memory for a %llu-byte body", (unsigned long long) data_size);

            self->write_success = false;

            trace_log_pop();

            return;
        }

        memory_copy_1(body + LWS_PRE, data, data_size);

        I32 const write_b = lws_write(self->wsi, body + LWS_PRE, data_size, LWS_WRITE_HTTP);

        if (write_b < 0 || (USize) write_b != data_size) {
            log_message_2(LOG_LEVEL_ERROR, LOG_METADATA, "http_server_response_send_2: body write returned %d, expected %llu", write_b, (unsigned long long) data_size);

            self->write_success = false;
        }

        memory_delete((void**) &body);
    }

    trace_log_pop();
}

void http_server_response_send_empty(HTTP_Server_Response *const self, U16 const status_code) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    http_server_response_send_2(self, nullptr, 0, HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, status_code);

    trace_log_pop();
}

bool http_server_response_send_file(HTTP_Server_Response *const self, char const *const path, char const *const content_type, char const *const extra_headers, USize const extra_headers_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "path", (void*) path);
    error_check_null(LOG_METADATA, "content_type", (void*) content_type);

    /* The same guard send_2 carries: the status line and headers go out exactly once, so a
     * second send on one response is a no-op rather than a second header block spliced
     * into a body already on the wire. */
    if (self->headers_sent) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_server_response_send_file: refusing '%s' - the response headers are already sent", path);

        trace_log_pop();

        return false;
    }

    /* None of the three refusals below writes a byte, so none of them touches
     * write_success. Marking the response failed for a refusal the caller is expected to
     * recover from (its own 404 body, say) turned that fallback reply into a closed
     * connection - and forced the suite to un-set the flag by hand to test the answer.
     * Only a failed lws_serve_http_file, which really did start writing, fails the write. */

    /* The caller's pre-formatted block first, then anything queued with
     * http_server_response_header_add (Cache-Control, etc.), so lws_serve_http_file emits
     * both alongside its own. */
    char    extra[_HTTP_SERVER_RESPONSE_HEADER_SIZE] = DEFAULT_INITIALIZATION;
    USize   extra_size                               = 0;

    if (extra_headers != nullptr && extra_headers_size > 0) {
        if (extra_headers_size >= sizeof(extra)) {
            log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_server_response_send_file: refusing - the caller's header block exceeds %llu bytes", (unsigned long long) sizeof(extra) - 1);

            trace_log_pop();

            return false;
        }

        memory_copy_1((Byte*) extra, (Byte const*) extra_headers, extra_headers_size);

        extra_size = extra_headers_size;
    }

    if (!string_empty(&self->extra_headers)) {
        USize const copy_size = string_get_size(&self->extra_headers);

        /* Silently dropping the queued headers used to mean a caller's Cache-Control or
         * Set-Cookie never reached the wire while send_file still answered true. */
        if (extra_size + copy_size >= sizeof(extra)) {
            log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_server_response_send_file: refusing - the combined header block exceeds %llu bytes", (unsigned long long) sizeof(extra) - 1);

            trace_log_pop();

            return false;
        }

        memory_copy_1((Byte*) extra + extra_size, (Byte*) string_get_data(&self->extra_headers), copy_size);

        extra_size += copy_size;
    }

    /* Checked here rather than left to lws: lws_serve_http_file answers a missing file by
     * writing its own 404 and reporting the transaction COMPLETE, so send_file could not
     * tell "streamed" from "did not exist" and the caller's own 404 body was never sent. */
    if (!file_exists_1(path)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_server_response_send_file: '%s' does not exist", path);

        trace_log_pop();

        return false;
    }

    /* lws owns the send loop from here: it services LWS_CALLBACK_HTTP_WRITEABLE
     * and flushes the file in transport-sized chunks, so the body cannot be cut
     * short the way a single one-shot lws_write of the whole body can. */
    I32 const result = lws_serve_http_file(self->wsi, path, content_type, extra_size > 0 ? extra : nullptr, (I32) extra_size);

    /* The status line and the header block are on the wire from here, whichever branch
     * below applies, so a second send on this response is refused like send_2's. */
    self->headers_sent  = true;
    self->file_served   = true;

    /*
     * ZERO is the only answer that means "the transfer started, service it later, leave the
     * wsi alone". Anything else means libwebsockets took the transaction to its end itself:
     * it wrote the status line and headers, called lws_http_transaction_completed, and is
     * telling this callback to close the connection - positive plainly, and NEGATIVE when
     * the completion asked for the close, which is exactly what a HEAD on a
     * `Connection: close` request produces and is indistinguishable here from a real serve
     * failure.
     *
     * Reading that negative as "refused, nothing written" is what made a HEAD answer TWICE:
     * send_file reported false with a 200 already on the wire, the caller ran its own
     * not-found branch, and a second reply - a 404 - went out behind it. A keep-alive client
     * reads that 404 as the answer to its NEXT request. Both non-zero answers are recorded
     * as a completed transaction here, and the dispatcher closes instead of re-entering.
     */
    if (result != 0) {
        /* Negative on a HEAD is the expected close-after-completion answer (see above) and
         * stays silent; negative on anything else is a genuine serve failure - a header-buffer
         * overflow or a short header write - worth LOG_LEVEL_ERROR. The completion semantics
         * above are unchanged either way. */
        if (result < 0 && lws_hdr_total_length(self->wsi, WSI_TOKEN_HEAD_URI) == 0) {
            log_message_2(LOG_LEVEL_ERROR, LOG_METADATA, "http_server_response_send_file: '%s' serve failed (result %d)", path, (int) result);
        }

        self->transaction_completed = true;
    }

    trace_log_pop();

    return true;
}

/*==============================================================================
 * MARK: - Router API
 *============================================================================*/
bool http_server_router_dispatch_1(HTTP_Server_Router *const self, char const *const path, HTTP_Server_Holder *const handler) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    bool const match = http_server_router_dispatch_2(self, path, char_length(path), handler);

    trace_log_pop();

    return match;
}

bool http_server_router_dispatch_2(HTTP_Server_Router *const self, char const *const path, USize const path_size, HTTP_Server_Holder *const handler) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "path", (void*) path);
    error_check_null(LOG_METADATA, "handler", (void*) handler);

    /* Not an error_check_*: the path comes from the peer, and an empty one is a request
     * that matches nothing, not a programmer error worth ending the process for. */
    if (path_size == 0) {
        trace_log_pop();

        return false;
    }

    HTTP_Server_Route *current = self->routes;

    /* The matched route is dispatched as a per-request COPY. Writing the holder and the
     * query capture into the shared list node meant every in-flight request on that route
     * saw the last one's request/response pair.
     *
     * The copy is declared inside the two MATCHED branches, not at the top of the scan: a
     * HTTP_Server_Route is ~600 bytes, so zero-filling one per route visited cost a memset
     * of the whole table on every miss - the 80-route worst case paid ~48 KiB for a
     * dispatch that matched nothing. */
    while (current != nullptr) {
        if (current->regex.re != nullptr) {
            if (regex_match_2(&current->regex, path, path_size, 0)) {
                HTTP_Server_Route match = DEFAULT_INITIALIZATION;

                USize   size    = HTTP_SERVER_QUERY_MAX_LENGTH - 1;
                Result  result  = regex_copy_match_group_name_1(&current->regex, "file", &match.query_value[0], &size);

                if (!result_is_success(result)) {
                    size    = HTTP_SERVER_QUERY_MAX_LENGTH - 1;
                    result  = regex_copy_match_group_number_1(&current->regex, 1, &match.query_value[0], &size);
                }

                match.query_value_size = result_is_success(result) ? (U16) size : 0;

                /* The capture buffer is fixed, and pcre2_substring_copy_* answers NOMEMORY
                 * rather than a short copy when the group does not fit - so it writes
                 * NOTHING and the callback would see an EMPTY capture where the request
                 * carried a long one. Watching for a capture that merely filled the buffer
                 * could never fire: pcre2 needs room for its own terminator, so a
                 * successful copy is at most QUERY_MAX_LENGTH - 2 bytes. The refusal code
                 * is what says "truncated", and it is what is watched for. */
                if (!result_is_success(result) && result_code(result) == (U16) -PCRE2_ERROR_NOMEMORY) {
                    log_message_2(LOG_LEVEL_WARN, LOG_METADATA,
                                  "http_server_router_dispatch: a route capture longer than %d bytes was dropped, not truncated",
                                  HTTP_SERVER_QUERY_MAX_LENGTH - 1);
                }

                char_copy_3(match.path, HTTP_SERVER_PATH_MAX_LENGTH, current->path, current->path_size);

                match.path_size = current->path_size;
                match.callback  = current->callback;
                match.holder    = *handler;

                match.callback(&match);

                trace_log_pop();

                return true;
            }
        }
        else {
            if (char_compare_equal_2(current->path, current->path_size, path, path_size)) {
                HTTP_Server_Route match = DEFAULT_INITIALIZATION;

                char_copy_3(match.path, HTTP_SERVER_PATH_MAX_LENGTH, current->path, current->path_size);

                match.path_size = current->path_size;
                match.callback  = current->callback;
                match.holder    = *handler;

                match.callback(&match);

                trace_log_pop();

                return true;
            }
        }

        current = current->next;
    }

    trace_log_pop();

    return false;
}

bool http_server_router_dispatch_3(HTTP_Server_Router *const self, Str const *const path, HTTP_Server_Holder *const handler) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    bool const match = http_server_router_dispatch_2(self, str_get_data(path), str_get_size(path), handler);

    trace_log_pop();

    return match;
}

bool http_server_router_dispatch_4(HTTP_Server_Router *const self, String const *const path, HTTP_Server_Holder *const handler) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    bool const match = http_server_router_dispatch_2(self, string_get_data(path), string_get_size(path), handler);

    trace_log_pop();

    return match;
}