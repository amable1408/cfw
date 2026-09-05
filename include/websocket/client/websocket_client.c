#include <websocket/client/websocket_client.h>

#define _WEBSOCKET_CLIENT_CLOSE_PAYLOAD_MAX 125
#define _WEBSOCKET_CLIENT_CONNECT_TIMEOUT_DEFAULT_MS 30000
#define _WEBSOCKET_CLIENT_MESSAGE_MAX (16 * 1024 * 1024)
#define _WEBSOCKET_CLIENT_RECV_BUFFER 65536
#define _WEBSOCKET_CLIENT_SEND_TIMEOUT_MS 30000
#define _WEBSOCKET_CLIENT_SPIN_POLL_MS 1

/* Per-handle state that a bare CURL* has nowhere to hold, threaded through
 * libcurl's own CURLOPT_PRIVATE slot so it travels with the handle without a
 * second lookup table. */
typedef struct {
    USize max_message_size;
    bool  connected; /* true once a connect_1/_2's curl_easy_perform returned CURLE_OK (a
                         completed handshake, including a subprotocol mismatch); never cleared
                         except by delete - a fresh handle is required to reconnect (one
                         connection per handle). */
    bool  dead;
} _WS_Client_State;

/* Scratch target for CURLOPT_HEADERFUNCTION during a subprotocol connect, used
 * to verify the server actually accepted the requested subprotocol; lives on
 * _websocket_client_connect's stack for the duration of curl_easy_perform and
 * never escapes it. */
typedef struct {
    char protocol[128];
    bool found;
} _WS_Client_Header_Capture;

/*==============================================================================
 * MARK: - Helpers
 *============================================================================*/

static bool _websocket_client_state_get(WS_Client *const self, _WS_Client_State **const out_state) {
    char *raw = nullptr;

    curl_easy_getinfo((CURL*) self, CURLINFO_PRIVATE, &raw);

    *out_state = (_WS_Client_State*) raw;

    return *out_state != nullptr;
}

static WS_Client_Result _websocket_client_dead_result(void) {
    WS_Client_Result result = websocket_client_result_init();

    result.status = WS_CLIENT_STATUS_CLOSED;

    return result;
}

/* Blocks the caller until the handle's active socket is ready for `events`,
 * or `timeout_ms` elapses. USIZE_MAX blocks with no bound (poll's own -1); a
 * finite timeout above I32_MAX clamps to one I32_MAX-long poll rather than
 * being treated as forever - the caller's own loop re-checks its deadline
 * and calls back in for the remainder. A handle exposing no active socket
 * answers false immediately and, when out_no_socket is non-null, sets
 * *out_no_socket true so a caller still seeing CURLE_AGAIN from curl can
 * tell "nothing to wait on" apart from "waited and nothing arrived" - the
 * former means the transport is already gone, not merely idle. */
static bool _websocket_client_socket_wait(WS_Client *const self, short const events, USize const timeout_ms, bool *const out_no_socket) {
    curl_socket_t socket_fd = CURL_SOCKET_BAD;

    curl_easy_getinfo((CURL*) self, CURLINFO_ACTIVESOCKET, &socket_fd);

    if (out_no_socket != nullptr) {
        *out_no_socket = socket_fd == CURL_SOCKET_BAD;
    }

    if (socket_fd == CURL_SOCKET_BAD) {
        return false;
    }

    I32 const wait_ms = (timeout_ms == USIZE_MAX) ? -1 : (timeout_ms > (USize) I32_MAX ? I32_MAX : (I32) timeout_ms);

    // A negative return (EINTR, or an interrupted WSAPoll) is treated the same as "not ready
    // yet": the caller's own loop re-checks its absolute deadline on the next pass rather than
    // this function retrying internally, so a signal cannot turn a bounded wait into a longer one.
#ifdef OS_WINDOWS
    WSAPOLLFD poll_fd = { .fd = socket_fd, .events = events, .revents = 0 };
    I32 const ready = WSAPoll(&poll_fd, 1, wait_ms);
#else
    struct pollfd poll_fd = { .fd = socket_fd, .events = events, .revents = 0 };
    I32 const ready = poll(&poll_fd, 1, wait_ms);
#endif // OS_WINDOWS

    return ready > 0;
}

/* Whether a subprotocol name is a legal RFC 6455 token: no controls, no
 * separators. Rejecting CR/LF here is what stops header injection into the
 * handshake request, since the name is concatenated into a header line. */
static bool _websocket_client_subprotocol_valid(char const *const subprotocol) {
    USize const size = char_length(subprotocol);

    if (size == 0) {
        return false;
    }

    char const *const separators = "()<>@,;:\\\"/[]?={}";
    USize const separator_size = char_length(separators);

    for (USize index = 0; index < size; index += 1) {
        char const value = subprotocol[index];

        if (value <= 32 || value >= 127) {
            return false;
        }

        for (USize separator = 0; separator < separator_size; separator += 1) {
            if (value == separators[separator]) {
                return false;
            }
        }
    }

    return true;
}

/* Map a backend code onto the transport-independent status callers branch on.
 * TIMEOUT means "nothing arrived, the connection is fine"; CLOSED means the
 * peer or the transport ended it and the handle must not be reused. */
static WS_Client_Status _websocket_client_status_from_code(CURLcode const code) {
    switch (code) {
        case CURLE_OK:                  { return WS_CLIENT_STATUS_OK; }
        case CURLE_AGAIN:               { return WS_CLIENT_STATUS_TIMEOUT; }
        case CURLE_OPERATION_TIMEDOUT:  { return WS_CLIENT_STATUS_TIMEOUT; }
        case CURLE_GOT_NOTHING:         { return WS_CLIENT_STATUS_CLOSED; }
        case CURLE_PARTIAL_FILE:        { return WS_CLIENT_STATUS_CLOSED; }
        case CURLE_RECV_ERROR:          { return WS_CLIENT_STATUS_CLOSED; }
        case CURLE_SEND_ERROR:          { return WS_CLIENT_STATUS_CLOSED; }
        default:                        { return WS_CLIENT_STATUS_ERROR; }
    }
}

static void _websocket_client_result_error_set(WS_Client_Result *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    // Guard on LENGTH, not nullness: string_init_static aborts on a size-0
    // input, so an empty (but non-null) message must fall back too.
    char const *const error = memory_empty(data) || data[0] == '\0' ? "websocket client request failed" : data;

    string_uninit(&self->error);

#ifdef ARENA_IMPLEMENTATION
    if (!memory_empty(self->allocator)) {
        self->error = string_alloc_init_4((char*) error, char_length(error), self->allocator);

        trace_log_pop();

        return;
    }
#endif // ARENA_IMPLEMENTATION

    self->error = string_init_static((char*) error, char_length(error));

    trace_log_pop();
}

/* curl_version_info's protocol list is a fixed, in-memory array (no I/O, no
 * blocking) - cheap enough to re-check on every connect rather than caching,
 * and correct even if a process somehow links more than one libcurl build. */
static bool _websocket_client_runtime_supports_ws(void) {
    curl_version_info_data const *const info = curl_version_info(CURLVERSION_NOW);

    if (info == nullptr || info->protocols == nullptr) {
        return false;
    }

    for (USize index = 0; info->protocols[index] != nullptr; index += 1) {
        if (char_compare_iequal_1(info->protocols[index], "ws") || char_compare_iequal_1(info->protocols[index], "wss")) {
            return true;
        }
    }

    return false;
}

/* A process can link an OLDER libcurl.dll at runtime than this module was
 * compiled against (the #error above only catches the compile-time header),
 * so the FIN-bit floor needs its own runtime check: 8.13.0 == 0x080D00. */
static bool _websocket_client_runtime_version_at_least_8_13(void) {
    curl_version_info_data const *const info = curl_version_info(CURLVERSION_NOW);

    return info != nullptr && info->version_num >= (unsigned int) CURL_VERSION_BITS(8, 13, 0);
}

/* CURLOPT_HEADERFUNCTION target for a subprotocol connect: captures the
 * server's "Sec-WebSocket-Protocol" reply header so it can be compared
 * against what was requested once the handshake completes. */
static USize _websocket_client_header_callback(char *const buffer, USize const size, USize const nitems, void *const userdata) {
    USize const total = size * nitems;
    _WS_Client_Header_Capture *const capture = (_WS_Client_Header_Capture*) userdata;
    char const *const prefix = "sec-websocket-protocol:";
    USize const prefix_size = char_length(prefix);

    if (capture == nullptr || total <= prefix_size) {
        return total;
    }

    char lower[192] = DEFAULT_INITIALIZATION;
    USize const lower_size = (total < sizeof(lower) - 1) ? total : sizeof(lower) - 1;

    for (USize index = 0; index < lower_size; index += 1) {
        char const value = buffer[index];

        lower[index] = (value >= 'A' && value <= 'Z') ? (char) (value - 'A' + 'a') : value;
    }

    if (!char_starts_with_1(lower, prefix)) {
        return total;
    }

    USize value_start = prefix_size;

    while (value_start < lower_size && (buffer[value_start] == ' ' || buffer[value_start] == '\t')) {
        value_start += 1;
    }

    USize value_end = lower_size;

    while (value_end > value_start && (buffer[value_end - 1] == '\r' || buffer[value_end - 1] == '\n' || buffer[value_end - 1] == ' ')) {
        value_end -= 1;
    }

    USize const value_size = value_end - value_start;
    USize const copy_size = value_size < sizeof(capture->protocol) - 1 ? value_size : sizeof(capture->protocol) - 1;

    memory_copy_1((void*) capture->protocol, (void const*) (buffer + value_start), copy_size);

    capture->protocol[copy_size] = '\0';
    capture->found = true;

    return total;
}

/*==============================================================================
 * MARK: - API
 *============================================================================*/

static WS_Client_Result _websocket_client_connect(WS_Client *const self, char const *const url, char const *const subprotocol) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "url", (void*) url);

    WS_Client_Result result = websocket_client_result_init();

    _WS_Client_State *state = nullptr;
    bool const has_state = _websocket_client_state_get(self, &state);

    // One connection per handle: libcurl has no way to close a single CONNECT_ONLY connection
    // short of curl_easy_cleanup, so silently reconnecting would park the old socket in the
    // per-handle connection cache until MAXCONNECTS (default 5) evicts it or the handle is
    // deleted - a leak of one socket and one peer session per reconnect. Refuse before any option
    // is set or the handshake attempted rather than document the leak.
    if (has_state && state->connected) {
        _websocket_client_result_error_set(&result, "handle already holds a connection; delete it and create a new one to reconnect");

        result.code = (I32) CURLE_BAD_FUNCTION_ARGUMENT;
        result.status = WS_CLIENT_STATUS_ERROR;

        trace_log_pop();

        return result;
    }

    if (!_websocket_client_runtime_supports_ws()) {
        _websocket_client_result_error_set(&result, "linked libcurl was built without WebSocket support (curl_version_info lists no ws/wss protocol)");

        result.code = (I32) CURLE_UNSUPPORTED_PROTOCOL;
        result.status = WS_CLIENT_STATUS_ERROR;

        trace_log_pop();

        return result;
    }

    if (!_websocket_client_runtime_version_at_least_8_13()) {
        curl_version_info_data const *const info = curl_version_info(CURLVERSION_NOW);
        char message[128] = DEFAULT_INITIALIZATION;

        snprintf(message, sizeof(message), "linked libcurl %s is older than the required 8.13 (WebSocket FIN bit read incorrectly below that version)",
            (info != nullptr && info->version != nullptr) ? info->version : "(unknown)");

        _websocket_client_result_error_set(&result, message);

        result.code = (I32) CURLE_UNSUPPORTED_PROTOCOL;
        result.status = WS_CLIENT_STATUS_ERROR;

        trace_log_pop();

        return result;
    }

    struct curl_slist *headers = nullptr;
    _WS_Client_Header_Capture capture = { .protocol = { 0 }, .found = false };
    bool const wants_subprotocol = !memory_empty((void*) subprotocol);

    if (wants_subprotocol) {
        char header[96 + CHAR_END_CHARACTER] = DEFAULT_INITIALIZATION;

        // A truncated or injected name would silently request the WRONG protocol,
        // so both are hard failures rather than a best-effort header.
        I32 const written = snprintf(header, sizeof(header), "Sec-WebSocket-Protocol: %s", subprotocol);

        if (!_websocket_client_subprotocol_valid(subprotocol) || written < 0 || (USize) written >= sizeof(header)) {
            _websocket_client_result_error_set(&result, "invalid or oversized websocket subprotocol");

            result.code = (I32) CURLE_BAD_FUNCTION_ARGUMENT;
            result.status = WS_CLIENT_STATUS_ERROR;

            trace_log_pop();

            return result;
        }

        headers = curl_slist_append(nullptr, header);

        // Proceeding without the header would silently negotiate the WRONG
        // protocol and still report success.
        if (memory_empty((void*) headers)) {
            _websocket_client_result_error_set(&result, "websocket subprotocol header allocation failed");

            result.code = (I32) CURLE_OUT_OF_MEMORY;
            result.status = WS_CLIENT_STATUS_ERROR;

            trace_log_pop();

            return result;
        }

        curl_easy_setopt((CURL*) self, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt((CURL*) self, CURLOPT_HEADERFUNCTION, _websocket_client_header_callback);
        curl_easy_setopt((CURL*) self, CURLOPT_HEADERDATA, &capture);
    }

    curl_easy_setopt((CURL*) self, CURLOPT_URL, url);
    curl_easy_setopt((CURL*) self, CURLOPT_CONNECT_ONLY, 2L);

    CURLcode const code = curl_easy_perform((CURL*) self);

    if (wants_subprotocol) {
        // The handshake completed inside curl_easy_perform; curl no longer reads these.
        curl_easy_setopt((CURL*) self, CURLOPT_HTTPHEADER, NULL);
        curl_easy_setopt((CURL*) self, CURLOPT_HEADERFUNCTION, NULL);
        curl_easy_setopt((CURL*) self, CURLOPT_HEADERDATA, NULL);
        curl_slist_free_all(headers);
    }

    if (code != CURLE_OK) {
        _websocket_client_result_error_set(&result, curl_easy_strerror(code));
        result.code = (I32) code;

        // A failed connect is never "retry on this connection" - there is no
        // connection - so it never maps to TIMEOUT/CLOSED, only ERROR.
        result.status = WS_CLIENT_STATUS_ERROR;

        trace_log_pop();

        return result;
    }

    if (has_state) {
        // curl_easy_perform returned CURLE_OK: the handshake completed and the socket is live,
        // whether or not the subprotocol check below accepts it. Latch `connected` here - not
        // only on the success path further down - so a subprotocol mismatch also refuses a
        // later connect_1/_2 on this handle above rather than leaking a socket (the same shape
        // as any other completed connect).
        state->connected = true;
    }

    if (wants_subprotocol && (!capture.found || !char_compare_equal_1(capture.protocol, subprotocol))) {
        _websocket_client_result_error_set(&result, "websocket server did not accept the requested subprotocol");

        // The handshake completed against a peer speaking the wrong protocol: the socket is live,
        // so leaving it undead would let a caller that ignores this ERROR read from that peer.
        // Close code 1002 = protocol error; best-effort, its own result is not checked.
        char close_payload[2] = { (char) ((1002 >> 8) & 0xFF), (char) (1002 & 0xFF) };
        size_t close_sent = 0;

        curl_ws_send((CURL*) self, close_payload, sizeof(close_payload), &close_sent, 0, CURLWS_CLOSE);

        if (has_state) {
            state->dead = true;
        }

        result.code = (I32) CURLE_WEIRD_SERVER_REPLY;
        result.status = WS_CLIENT_STATUS_ERROR;

        trace_log_pop();

        return result;
    }

    result.code = (I32) code;
    result.status = WS_CLIENT_STATUS_OK;

    trace_log_pop();

    return result;
}

/* Shared send loop for websocket_client_send and websocket_client_close: writes `data`/`size`
 * under `flags`, looping through CURLE_AGAIN until every byte is on the wire or the internal send
 * deadline passes. `sent_chunk` is added to the running total BEFORE the AGAIN/OK branch is
 * decided - curl documents `sent` as bytes actually sent regardless of the returned code, so a
 * retry after AGAIN must resume from that point rather than re-sending bytes the peer already
 * has. A deadline exceeded with a partial frame already on the wire answers CLOSED, never
 * TIMEOUT: the peer has a frame header promising more bytes than will ever arrive, so there is
 * nothing healthy left to retry, and `state` (may be null) is marked dead. */
static WS_Client_Result _websocket_client_send_frame(WS_Client *const self, _WS_Client_State *const state, void const *const data, USize const size, U32 const flags) {
    trace_log_push(LOG_METADATA);

    WS_Client_Result result = websocket_client_result_init();
    USize sent_total = 0;
    ChronoInstant const start = chrono_now();

    while (sent_total < size) {
        // Checked at the TOP of every pass (mirrors _websocket_client_recv_frame's loop-top
        // deadline) so a CURLE_OK reply that writes zero bytes this pass - not just a CURLE_AGAIN -
        // cannot spin past the budget with no progress and no wait.
        U64 const elapsed_ms = chrono_duration_milliseconds(chrono_elapsed(start));

        if (elapsed_ms >= _WEBSOCKET_CLIENT_SEND_TIMEOUT_MS) {
            _websocket_client_result_error_set(&result, "websocket send timed out with a partial frame already on the wire");

            result.code = (I32) CURLE_OPERATION_TIMEDOUT;
            result.status = WS_CLIENT_STATUS_CLOSED;

            if (state != nullptr) {
                state->dead = true;
            }

            trace_log_pop();

            return result;
        }

        size_t sent_chunk = 0;
        CURLcode const code = curl_ws_send((CURL*) self, (char const*) data + sent_total, size - sent_total, &sent_chunk, 0, flags);

        sent_total += (USize) sent_chunk;

        if (code == CURLE_AGAIN) {
            // Wait for the REMAINING budget, not the full window every pass - otherwise a send
            // that hits AGAIN repeatedly near the deadline overshoots it by up to one full wait.
            USize const remaining = (USize) (_WEBSOCKET_CLIENT_SEND_TIMEOUT_MS - elapsed_ms);
            bool no_socket = false;

            _websocket_client_socket_wait(self, POLLOUT, remaining, &no_socket);

            if (no_socket) {
                // Mirrors the recv loop's identical guard: a handle exposing no active socket
                // while curl still answers AGAIN means the transport is already gone, so retrying
                // would spin hot until the deadline rather than report the handle unusable.
                _websocket_client_result_error_set(&result, "websocket send found no active socket");

                result.code = (I32) CURLE_GOT_NOTHING;
                result.status = WS_CLIENT_STATUS_CLOSED;

                if (state != nullptr) {
                    state->dead = true;
                }

                trace_log_pop();

                return result;
            }

            continue;
        }

        if (code != CURLE_OK) {
            _websocket_client_result_error_set(&result, curl_easy_strerror(code));
            result.code = (I32) code;
            result.status = _websocket_client_status_from_code(code);

            trace_log_pop();

            return result;
        }

        if (sent_chunk == 0) {
            // CURLE_OK with nothing written this pass and the frame not yet complete: a short,
            // bounded wait avoids a tight spin, mirroring the recv loop's identical guard.
            bool no_socket = false;

            _websocket_client_socket_wait(self, POLLOUT, _WEBSOCKET_CLIENT_SPIN_POLL_MS, &no_socket);

            if (no_socket) {
                // Mirrors the AGAIN branch above: curl answered CURLE_OK yet exposes
                // no active socket, so the transport is already gone.
                _websocket_client_result_error_set(&result, "websocket send found no active socket");

                result.code = (I32) CURLE_GOT_NOTHING;
                result.status = WS_CLIENT_STATUS_CLOSED;

                if (state != nullptr) {
                    state->dead = true;
                }

                trace_log_pop();

                return result;
            }
        }
    }

    result.code = (I32) CURLE_OK;
    result.status = WS_CLIENT_STATUS_OK;

    trace_log_pop();

    return result;
}

WS_Client_Result websocket_client_close(WS_Client *const self, U16 const code, char const *const reason) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    _WS_Client_State *state = nullptr;
    bool const has_state = _websocket_client_state_get(self, &state);

    if (has_state && state->dead) {
        trace_log_pop();

        return _websocket_client_dead_result();
    }

    char payload[_WEBSOCKET_CLIENT_CLOSE_PAYLOAD_MAX] = DEFAULT_INITIALIZATION;

    payload[0] = (char) ((code >> 8) & 0xFF);
    payload[1] = (char) (code & 0xFF);

    USize reason_size = memory_empty((void*) reason) ? 0 : char_length(reason);
    USize const reason_room = sizeof(payload) - 2;

    if (reason_size > reason_room) {
        reason_size = reason_room;
    }

    if (reason_size > 0) {
        memory_copy_1((void*) (payload + 2), (void const*) reason, reason_size);
    }

    WS_Client_Result result = _websocket_client_send_frame(self, state, payload, 2 + reason_size, CURLWS_CLOSE);

    if (result.status != WS_CLIENT_STATUS_OK) {
        // A deadline-exceeded CLOSED already marked the handle dead inside the shared loop; any
        // other failure is reported as-is without forcing the mark.
        trace_log_pop();

        return result;
    }

    // A close frame has now told the peer this side is done: the handle's
    // framing state is no longer meaningful, so mark it dead the same as an
    // overflow/send-timeout - every later send/recv/recv_frame on it answers
    // CLOSED without touching the network. This call's OWN result stays OK,
    // though: it reports whether the CLOSE frame itself was sent, not the
    // (now dead) state of the handle - CLOSED is reserved for the dead no-op
    // and the deadline-exceeded case above, per the enum's own contract.
    if (has_state) {
        state->dead = true;
    }

    result.close_code = code;
    result.status = WS_CLIENT_STATUS_OK;

    trace_log_pop();

    return result;
}

WS_Client_Result websocket_client_connect_1(WS_Client *const self, char const *const url) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "url", (void*) url);

    WS_Client_Result const result = _websocket_client_connect(self, url, nullptr);

    trace_log_pop();

    return result;
}

WS_Client_Result websocket_client_connect_2(WS_Client *const self, char const *const url, char const *const subprotocol) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "url", (void*) url);
    error_check_null(LOG_METADATA, "subprotocol", (void*) subprotocol);

    WS_Client_Result const result = _websocket_client_connect(self, url, subprotocol);

    trace_log_pop();

    return result;
}

void websocket_client_delete(WS_Client **const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "*self", (void*) *self);

    _WS_Client_State *state = nullptr;

    if (_websocket_client_state_get(*self, &state)) {
        memory_free((void*) state);
    }

    curl_easy_cleanup(*self);

#ifdef MEMORY_NON_DANGLING_POINTER
    *self = nullptr;
#endif // MEMORY_NON_DANGLING_POINTER

    trace_log_pop();
}

WS_Client* websocket_client_new(void) {
    trace_log_push(LOG_METADATA);

    // curl_global_init / curl_global_cleanup are intentionally not called here:
    // they touch process-global curl state and libcurl documents them as unsafe
    // from more than one thread. The owning program performs the one-time
    // curl global init (see this header's curl_global_init section).
    CURL *const curl = curl_easy_init();

    error_check_null(LOG_METADATA, "curl", (void*) curl);

    _WS_Client_State *const state = (_WS_Client_State*) memory_alloc(sizeof(_WS_Client_State));

    state->max_message_size = _WEBSOCKET_CLIENT_MESSAGE_MAX;
    state->connected = false;
    state->dead = false;

    curl_easy_setopt(curl, CURLOPT_PRIVATE, (void*) state);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, (long) _WEBSOCKET_CLIENT_CONNECT_TIMEOUT_DEFAULT_MS);

    trace_log_pop();

    return (WS_Client*) curl;
}

/* `start` is a single ABSOLUTE deadline anchor (one chrono_now() taken by the
 * caller, shared across every frame of a multi-frame message) rather than an
 * in/out millisecond accumulator: the accumulator let sub-millisecond waits
 * round down to 0 forever, and - the bug this replaces - was only consulted
 * from the CURLE_AGAIN branch, so a PING flood (CURLE_OK every time) never
 * hit a budget check and never returned even with timeout_ms 0. The deadline
 * is now re-measured against `start` at the TOP of every loop pass, so the
 * PING/PONG skip, the CLOSE drain, and the received-0 wait - all of which
 * loop back here via `continue` - fall under the same check.
 *
 * `allow_first_poll` exempts only the very first curl_ws_recv attempt of the WHOLE message from
 * that check, so timeout_ms == 0 still gets one attempt - it must be true only from
 * recv_frame_1/_2's own entry and from the first call inside _websocket_client_recv's message
 * loop. Every later call in that same loop (one per continuation frame) passes false: this
 * function's own `iteration` counter resets to 0 on each such call, so without this parameter a
 * frame that completes in a single internal pass NEVER re-enters the loop and never re-checks
 * `start` - a peer sending endless tiny CONT frames would never hit the deadline and recv_2/
 * recv_1 would block past their budget forever. */
static WS_Client_Result _websocket_client_recv_frame(WS_Client *const self,
    String *const out, bool *const out_final, WS_Client_Type *const out_type, USize const timeout_ms, ChronoInstant const start, bool const allow_first_poll) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "out", (void*) out);

    WS_Client_Result result = websocket_client_result_init();

    _WS_Client_State *state = nullptr;

    _websocket_client_state_get(self, &state);

    USize const message_max = state != nullptr ? state->max_message_size : _WEBSOCKET_CLIENT_MESSAGE_MAX;

    char buffer[_WEBSOCKET_CLIENT_RECV_BUFFER] = DEFAULT_INITIALIZATION;
    char close_payload[_WEBSOCKET_CLIENT_CLOSE_PAYLOAD_MAX] = DEFAULT_INITIALIZATION;
    USize close_size = 0;
    bool complete = false;
    USize iteration = 0;

    while (!complete) {
        // Checked at the TOP of every pass, against the ABSOLUTE deadline `start` - so a PING
        // flood, a CLOSE drain, or a CURLE_OK/received==0 spin all honor the same budget instead
        // of looping past it via their own `continue` below. Skipped ONLY on the first pass of a
        // call that is itself the first poll of the whole message (see allow_first_poll above),
        // so timeout_ms == 0 still gets exactly one curl_ws_recv attempt per message, not one per
        // continuation frame.
        if (iteration > 0 || !allow_first_poll) {
            U64 const elapsed_ms = chrono_duration_milliseconds(chrono_elapsed(start));

            if (timeout_ms == 0 || (timeout_ms != USIZE_MAX && elapsed_ms >= (U64) timeout_ms)) {
                result.code = (I32) CURLE_OPERATION_TIMEDOUT;
                result.status = WS_CLIENT_STATUS_TIMEOUT;

                trace_log_pop();

                return result;
            }
        }

        iteration += 1;

        size_t received_raw = 0;
        struct curl_ws_frame const *meta = nullptr;
        CURLcode const code = curl_ws_recv((CURL*) self, buffer, sizeof(buffer), &received_raw, &meta);
        USize const received = (USize) received_raw;

        if (code == CURLE_AGAIN) {
            // CONNECT_ONLY handles run no transfer loop, so curl's own timeout
            // never fires here: the budget must be enforced by this loop or an
            // idle peer blocks the caller forever.
            U64 const elapsed_ms = chrono_duration_milliseconds(chrono_elapsed(start));
            USize const remaining = (timeout_ms == USIZE_MAX) ? USIZE_MAX : ((U64) timeout_ms > elapsed_ms ? (USize) (timeout_ms - (USize) elapsed_ms) : 0);
            bool no_socket = false;

            _websocket_client_socket_wait(self, POLLIN, remaining, &no_socket);

            if (no_socket) {
                // curl still answers CURLE_AGAIN yet exposes no active socket:
                // the transport is already gone, so retrying (instantly, or
                // forever under timeout_ms == USIZE_MAX) would spin the CPU
                // rather than report the handle unusable.
                result.code = (I32) CURLE_GOT_NOTHING;
                result.status = WS_CLIENT_STATUS_CLOSED;

                trace_log_pop();

                return result;
            }

            continue;
        }

        if (code != CURLE_OK) {
            _websocket_client_result_error_set(&result, curl_easy_strerror(code));

            result.code = (I32) code;
            result.status = _websocket_client_status_from_code(code);

            trace_log_pop();

            return result;
        }

        if (received > 0 && meta == nullptr) {
            // Never observed from curl in practice (CURLE_OK with payload bytes always carries
            // frame metadata) - defensive only, since without `meta` this loop has no bytesleft/
            // flags to decide completion on and would otherwise spin appending forever.
            _websocket_client_result_error_set(&result, "websocket receive returned data with no frame metadata");

            result.code = (I32) CURLE_WEIRD_SERVER_REPLY;
            result.status = WS_CLIENT_STATUS_ERROR;

            trace_log_pop();

            return result;
        }

        bool const is_ping_or_pong = meta != nullptr && (meta->flags & (CURLWS_PING | CURLWS_PONG)) != 0;
        bool const is_close = meta != nullptr && (meta->flags & CURLWS_CLOSE) != 0;

        if (is_ping_or_pong) {
            // Control-frame payload never touches `out` or `complete`; libcurl
            // auto-replies PONG to a PING. Appending it would corrupt any
            // message this PING interrupted (RFC 6455 5.4 lets peers do that).
            continue;
        }

        if (is_close) {
            if (received > 0 && close_size < sizeof(close_payload)) {
                USize const room = sizeof(close_payload) - close_size;
                USize const copy = received < room ? received : room;

                memory_copy_1((void*) (close_payload + close_size), (void const*) buffer, copy);

                close_size += copy;
            }

            if (meta->bytesleft != 0) {
                continue;
            }

            U16 close_code_value = 0;
            char reason[_WEBSOCKET_CLIENT_CLOSE_PAYLOAD_MAX - 1] = DEFAULT_INITIALIZATION;

            if (close_size >= 2) {
                close_code_value = (U16) (((U8) close_payload[0] << 8) | (U8) close_payload[1]);

                USize const reason_size = close_size - 2;

                if (reason_size > 0) {
                    memory_copy_1((void*) reason, (void const*) (close_payload + 2), reason_size);
                }

                reason[reason_size] = '\0';
            }

            _websocket_client_result_error_set(&result, reason[0] != '\0' ? reason : "websocket close frame received");

            // RFC 6455 5.5.1: the endpoint receiving a Close frame MUST send one back. Best-effort
            // (its own result is not checked) - echo the code the peer sent, or 1000 when it sent
            // none. The handle is marked dead the same as a self-initiated close: this side has
            // now told the peer it is done too, and the framing state is no longer meaningful.
            U16 const echo_code = close_size >= 2 ? close_code_value : 1000;
            char close_reply[2] = { (char) ((echo_code >> 8) & 0xFF), (char) (echo_code & 0xFF) };
            size_t reply_sent = 0;

            curl_ws_send((CURL*) self, close_reply, sizeof(close_reply), &reply_sent, 0, CURLWS_CLOSE);

            if (state != nullptr) {
                state->dead = true;
            }

            result.close_code = close_code_value;
            result.code = (I32) CURLE_OK;
            result.status = WS_CLIENT_STATUS_CLOSED;

            trace_log_pop();

            return result;
        }

        if (received > 0) {
            string_add_last_2(out, buffer, received);

            // The cap MUST live here, not only between frames: curl returns
            // CURLE_OK repeatedly while bytesleft > 0, so one frame declaring a
            // huge payload would stream unbounded bytes into `out` before the
            // message-level check ever ran (remote memory exhaustion).
            if (string_get_size(out) > message_max) {
                _websocket_client_result_error_set(&result, "websocket message exceeds the reassembly limit");

                result.code = (I32) CURLE_TOO_LARGE;
                result.status = WS_CLIENT_STATUS_CLOSED;

                if (state != nullptr) {
                    state->dead = true;
                }

                trace_log_pop();

                return result;
            }
        }

        if (meta != nullptr && (meta->flags & (CURLWS_TEXT | CURLWS_BINARY)) != 0 && out_type != nullptr) {
            *out_type = (meta->flags & CURLWS_TEXT) != 0 ? WS_CLIENT_TEXT : WS_CLIENT_BINARY;
        }

        if (meta != nullptr && meta->bytesleft == 0) {
            complete = true;

            if (out_final != nullptr) {
                *out_final = (meta->flags & CURLWS_CONT) == 0;
            }
        } else if (received == 0) {
            // CURLE_OK with nothing delivered and the frame not yet complete:
            // a short, bounded wait avoids a tight spin. The loop-top deadline
            // check (this `continue` lands back there) still applies.
            bool no_socket = false;

            _websocket_client_socket_wait(self, POLLIN, _WEBSOCKET_CLIENT_SPIN_POLL_MS, &no_socket);

            if (no_socket) {
                // Mirrors the AGAIN branch above: curl answered CURLE_OK yet exposes
                // no active socket, so the transport is already gone.
                result.code = (I32) CURLE_GOT_NOTHING;
                result.status = WS_CLIENT_STATUS_CLOSED;

                trace_log_pop();

                return result;
            }
        }
    }

    result.code = (I32) CURLE_OK;
    result.status = WS_CLIENT_STATUS_OK;

    trace_log_pop();

    return result;
}

static WS_Client_Result _websocket_client_recv(WS_Client *const self, String *const out, USize const timeout_ms) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "out", (void*) out);

    _WS_Client_State *state = nullptr;

    if (_websocket_client_state_get(self, &state) && state->dead) {
        trace_log_pop();

        return _websocket_client_dead_result();
    }

    WS_Client_Result result = websocket_client_result_init();
    bool final = false;

    // ONE absolute deadline for the whole message, shared across continuation
    // frames: a per-frame budget would let a peer drip fragments and hold the
    // caller (and any lock it holds) forever.
    ChronoInstant const start = chrono_now();
    bool allow_first_poll = true;

    while (!final) {
        bool frame_final = false;

        websocket_client_result_uninit(&result);
        result = _websocket_client_recv_frame(self, out, &frame_final, nullptr, timeout_ms, start, allow_first_poll);
        allow_first_poll = false;

        if (result.status != WS_CLIENT_STATUS_OK) {
            trace_log_pop();

            return result;
        }

        final = frame_final;
    }

    trace_log_pop();

    return result;
}

WS_Client_Result websocket_client_recv_1(WS_Client *const self, String *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "out", (void*) out);

    WS_Client_Result const result = _websocket_client_recv(self, out, USIZE_MAX);

    trace_log_pop();

    return result;
}

WS_Client_Result websocket_client_recv_2(WS_Client *const self, String *const out, USize const timeout_ms) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "out", (void*) out);

    WS_Client_Result const result = _websocket_client_recv(self, out, timeout_ms);

    trace_log_pop();

    return result;
}

WS_Client_Result websocket_client_recv_frame_1(WS_Client *const self, String *const out, bool *const out_final, WS_Client_Type *const out_type) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "out", (void*) out);

    _WS_Client_State *state = nullptr;

    if (_websocket_client_state_get(self, &state) && state->dead) {
        trace_log_pop();

        return _websocket_client_dead_result();
    }

    WS_Client_Result const result = _websocket_client_recv_frame(self, out, out_final, out_type, USIZE_MAX, chrono_now(), true);

    trace_log_pop();

    return result;
}

WS_Client_Result websocket_client_recv_frame_2(WS_Client *const self, String *const out, bool *const out_final, WS_Client_Type *const out_type, USize const timeout_ms) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "out", (void*) out);

    _WS_Client_State *state = nullptr;

    if (_websocket_client_state_get(self, &state) && state->dead) {
        trace_log_pop();

        return _websocket_client_dead_result();
    }

    WS_Client_Result const result = _websocket_client_recv_frame(self, out, out_final, out_type, timeout_ms, chrono_now(), true);

    trace_log_pop();

    return result;
}

WS_Client_Result websocket_client_result_init(void) {
    trace_log_push(LOG_METADATA);

    /* A fresh result is "not yet performed": ERROR, so a caller that forgets to
     * run an operation never reads it as success. */
    WS_Client_Result const result = {
#ifdef ARENA_IMPLEMENTATION
        .allocator  = nullptr,
#endif // ARENA_IMPLEMENTATION
        .close_code = 0,
        .code       = 0,
        .error      = string_init_1(),
        .status     = WS_CLIENT_STATUS_ERROR
    };

    trace_log_pop();

    return result;
}

void websocket_client_result_uninit(WS_Client_Result *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    string_uninit(&self->error);

#ifdef ARENA_IMPLEMENTATION
#ifdef MEMORY_NON_DANGLING_POINTER
    self->allocator = nullptr;
#endif // MEMORY_NON_DANGLING_POINTER
#endif // ARENA_IMPLEMENTATION
    self->close_code = 0;
    self->code       = 0;

    // Back to "not yet performed": a released result must never read as OK.
    self->status     = WS_CLIENT_STATUS_ERROR;

    trace_log_pop();
}

WS_Client_Result websocket_client_send(WS_Client *const self, void const *const data, USize const size, WS_Client_Type const type) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    _WS_Client_State *state = nullptr;

    if (_websocket_client_state_get(self, &state) && state->dead) {
        trace_log_pop();

        return _websocket_client_dead_result();
    }

    U32 const flags = (type == WS_CLIENT_BINARY) ? CURLWS_BINARY : CURLWS_TEXT;
    WS_Client_Result const result = _websocket_client_send_frame(self, state, data, size, flags);

    trace_log_pop();

    return result;
}

void websocket_client_set_ca_bundle(WS_Client *const self, char const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "path", (void*) path);

    curl_easy_setopt((CURL*) self, CURLOPT_CAINFO, path);

    trace_log_pop();
}

void websocket_client_set_connect_timeout_ms(WS_Client *const self, USize const timeout_ms) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    // USIZE_MAX means "forever" (forwarded as libcurl's own 0/no-timeout);
    // anything else is clamped to LONG_MAX before the (long) cast so a huge
    // USize timeout on this LLP64 build (long is 32-bit) can't silently wrap.
    long const clamped = (timeout_ms == USIZE_MAX) ? 0L : (timeout_ms > (USize) LONG_MAX ? LONG_MAX : (long) timeout_ms);

    curl_easy_setopt((CURL*) self, CURLOPT_CONNECTTIMEOUT_MS, clamped);

    trace_log_pop();
}

void websocket_client_set_max_message_size(WS_Client *const self, USize const max_message_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_non_value_uint(LOG_METADATA, "max_message_size", max_message_size);

    _WS_Client_State *state = nullptr;

    if (_websocket_client_state_get(self, &state)) {
        state->max_message_size = max_message_size;
    }

    trace_log_pop();
}

void websocket_client_set_timeout_ms(WS_Client *const self, USize const timeout_ms) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    // USIZE_MAX means "forever" (forwarded as libcurl's own 0/no-timeout);
    // anything else is clamped to LONG_MAX before the (long) cast so a huge
    // USize timeout on this LLP64 build (long is 32-bit) can't silently wrap.
    long const clamped = (timeout_ms == USIZE_MAX) ? 0L : (timeout_ms > (USize) LONG_MAX ? LONG_MAX : (long) timeout_ms);

    curl_easy_setopt((CURL*) self, CURLOPT_TIMEOUT_MS, clamped);

    trace_log_pop();
}

void websocket_client_set_verify_tls(WS_Client *const self, bool const verify_tls) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    curl_easy_setopt((CURL*) self, CURLOPT_SSL_VERIFYHOST, verify_tls ? 2L : 0L);
    curl_easy_setopt((CURL*) self, CURLOPT_SSL_VERIFYPEER, verify_tls ? 1L : 0L);

    trace_log_pop();
}