/*
 * websocket_client.h - WebSocket client wrapper (libcurl curl_ws-based) for the C Libraries Framework
 *
 * Features:
 *   - Synchronous, single-connection WebSocket client over libcurl (CURLOPT_CONNECT_ONLY)
 *   - Supports ws:// and wss:// (TLS provided by libcurl; peer + host verification ON by default)
 *   - Blocking send of a text or binary message, looping through CURLE_AGAIN until the whole
 *     frame is on the wire or an internal send deadline passes
 *   - Blocking receive at message granularity (reassembles fragmented frames), transparent to
 *     interleaved PING/PONG control frames; a peer-initiated CLOSE is answered with an echoing
 *     CLOSE frame (best-effort) and marks the handle dead
 *   - Blocking receive at frame granularity for streaming callers
 *   - Cooperative close handshake (websocket_client_close) in addition to a hard delete
 *
 * Usage Example:
 *   @code
 *   WS_Client *client = websocket_client_new();
 *   WS_Client_Result connect_result = websocket_client_connect_1(client, "ws://127.0.0.1:9222/devtools/page/ID");
 *
 *   if (connect_result.status == WS_CLIENT_STATUS_OK) {
 *       String message = string_init_1();
 *       WS_Client_Result result = websocket_client_recv_2(client, &message, 100);
 *
 *       if (result.status == WS_CLIENT_STATUS_OK) {
 *           // Use message ...
 *       }
 *
 *       websocket_client_result_uninit(&result);
 *       string_uninit(&message);
 *       websocket_client_close(client, 1000, "done"); // result ignored: no owned resources leak,
 *                                                      // but a failed close is then silent
 *   }
 *
 *   websocket_client_result_uninit(&connect_result);
 *   websocket_client_delete(&client);
 *   @endcode
 *
 * Error Handling:
 *   Required pointer arguments are checked first and abort via error_check_null when
 *   ERROR_CHECK_ENABLED is defined (the CFW default); a null argument is a contract violation,
 *   not a value. With ERROR_CHECK_ENABLED undefined the checks compile out and passing null is
 *   undefined behavior. websocket_client_new never returns null (curl_easy_init failure aborts).
 *   Transport failures never abort: they are reported through the WS_Client_Result returned by
 *   value - branch on `status`, treat `code` (the backend CURLcode) as diagnostics only.
 *
 * Compatibility:
 *   Requires libcurl >= 8.13.0 at compile time (enforced below with #error): curl started reading
 *   the WebSocket FIN bit at 8.13 (curl PR #16687), and this module's frame reassembly depends on
 *   FIN being reported correctly for fragmented server messages. A linked libcurl built WITHOUT
 *   --enable-websockets compiles fine but fails every connect at runtime with a clear error -
 *   checked with curl_version_info() before the handshake, alongside a runtime version check for
 *   the same 8.13 floor (a process can link an older libcurl.dll than it was compiled against), so
 *   the failure is immediate and diagnostic, never a hang.
 *
 * curl_global_init:
 *   This module never calls curl_global_init/curl_global_cleanup - they mutate process-global
 *   curl state and are documented by curl as unsafe to call from more than one thread, or from
 *   any thread concurrently with other curl activity. The OWNING PROGRAM calls
 *   curl_global_init(CURL_GLOBAL_DEFAULT) exactly once, before the first WS_Client (or any other
 *   curl handle) is created, from a single thread with no other curl calls in flight; it calls
 *   curl_global_cleanup once at shutdown, after every curl handle in the process is gone.
 *
 * Timeouts (0 = poll once, USIZE_MAX = forever):
 *   Every receive/send budget in this module shares one convention: 0 means "check once, do not
 *   wait" and USIZE_MAX means "no budget - wait forever". Values in between are a millisecond
 *   budget. websocket_client_recv_1 and websocket_client_recv_frame_1 are exactly their _2
 *   sibling called with USIZE_MAX. recv_frame_2 starts a fresh budget per frame; recv_2 shares
 *   ONE budget across every continuation frame of a message (see the buffer-reset idiom below).
 *   recv_2(0) checks once and consumes at most one frame's worth of work - a buffered PING
 *   followed by real data still reports TIMEOUT rather than draining both. The connect/handshake
 *   setters (set_connect_timeout_ms, set_timeout_ms) are the exception: they forward 0 to libcurl
 *   as-is (libcurl's own "use the built-in default") and translate USIZE_MAX to 0 as well, since
 *   libcurl has no separate "wait forever" sentinel for those options.
 *
 * Buffer-reset idiom (recv_2 / recv_frame_2):
 *   `out` is a reassembly buffer, not an all-or-nothing output: it is APPENDED to, never cleared,
 *   by every receive call. Clear it yourself between independent messages (string_clear or
 *   uninit+init) once a call reports WS_CLIENT_STATUS_OK. On WS_CLIENT_STATUS_TIMEOUT, do NOT
 *   clear it: retry with the SAME String so the tail appends onto the already-received prefix -
 *   a fresh buffer silently drops the prefix and desynchronizes the message stream.
 *
 * Message size cap:
 *   Reassembly is bounded (websocket_client_set_max_message_size, default 16 MiB) against both a
 *   single oversized frame and an over-long continuation chain. Exceeding it fails the call with
 *   WS_CLIENT_STATUS_CLOSED and marks the handle dead: every later call on that same handle
 *   (send, recv, recv_frame) answers WS_CLIENT_STATUS_CLOSED immediately without touching the
 *   network, since the framing state at that point cannot be trusted. One connection per handle:
 *   delete this handle and create a new one to reconnect (see websocket_client_connect_1).
 *
 * Thread Safety:
 *   Not thread-safe. A connection is owned by the calling thread; the blocking send/recv model
 *   expects one connection per thread. Caller must synchronize if a handle is shared across
 *   threads. curl_global_init's own thread-safety rule (above) is a separate, one-time concern.
 *
 * Performance:
 *   Each receive call stages one frame in a 64 KiB stack buffer (plus a 125-byte close-payload
 *   buffer) before appending to `out`, sized for libcurl's 64 KiB frame chunk; the buffer is
 *   zeroed on every call though only the bytes actually returned are ever read - a ~64 KiB stack
 *   frame per recv/recv_frame call, negligible next to libcurl's own per-message allocations. A
 *   waiting receive or send blocks on the handle's active socket via poll()/WSAPoll
 *   (CURLINFO_ACTIVESOCKET) rather than sleep-polling, so an idle connection costs one syscall
 *   per wait, not one wakeup per millisecond.
 *
 * Comparative pass:
 *   Not run against libwebsockets' client API or websocketpp for this revision.
 *
 * Platform:
 *   Windows builds link against libcurl's DLL (and, transitively, ws2_32 for WSAPoll); TLS is
 *   whatever backend that linked libcurl build embeds - this tree's dep/curl DLL is a
 *   curl-for-win build using schannel, not OpenSSL, despite the OpenSSL link line some in-tree
 *   consumers carry for unrelated reasons.
 *
 * See websocket_client.c for implementation details.
 */
#ifndef WEBSOCKET_CLIENT_H
#define WEBSOCKET_CLIENT_H

#include <curl/curl.h>
#include <curl/websockets.h>

#include <chrono/chrono.h>
#include <container/string/string.h>

#ifdef OS_WINDOWS
#include <winsock2.h>
#else
#include <poll.h>
#endif // OS_WINDOWS

#if !CURL_AT_LEAST_VERSION(8, 13, 0)
#error "websocket/client requires libcurl >= 8.13 (FIN bit read correctly for fragmented WebSocket messages, curl PR #16687)"
#endif // !CURL_AT_LEAST_VERSION(8, 13, 0)

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief WebSocket client handle (libcurl easy handle in CONNECT_ONLY mode).
 *
 * This module keeps its own per-handle state in CURLOPT_PRIVATE. Do not call
 * curl_easy_setopt on a WS_Client for CURLOPT_PRIVATE, CURLOPT_HTTPHEADER,
 * CURLOPT_HEADERFUNCTION/CURLOPT_HEADERDATA, CURLOPT_CONNECT_ONLY, or CURLOPT_URL - these are
 * reserved by this module and overwriting them loses the dead/cap state or the connection itself.
 */
typedef CURL WS_Client;

/**
 * @brief WebSocket message payload kind.
 */
typedef enum {
    WS_CLIENT_BINARY,
    WS_CLIENT_TEXT,
} WS_Client_Type;

/**
 * @brief Backend-independent outcome of a WebSocket operation.
 *
 * Callers branch on this instead of on the backend's numeric code: a polling
 * receive loop must tell "nothing arrived yet, try again" (TIMEOUT) apart from
 * "this connection is gone" (CLOSED), and comparing raw CURLcodes to do that
 * leaks the transport into every caller.
 */
typedef enum {
    /** @brief The operation completed. */
    WS_CLIENT_STATUS_OK,
    /** @brief No data within the caller's receive budget (websocket_client_recv_2 /
     *         recv_frame_2 only); the connection is still healthy — retry. Never produced by
     *         connect: a handshake that times out yields ERROR, since there is no connection to retry on. */
    WS_CLIENT_STATUS_TIMEOUT,
    /** @brief The peer closed the connection (cleanly or otherwise) or the transport dropped it;
     *         stop using this handle - the handle is marked dead and every later send/recv/recv_frame
     *         answers CLOSED without touching the network. A clean peer CLOSE populates close_code
     *         and, on the receive side, is answered with a best-effort echoing CLOSE frame before
     *         this status is returned; other causes leave close_code 0. Also produced by send/close
     *         when a send deadline passes with a partial frame already on the wire (the handle can
     *         no longer be trusted) - unlike TIMEOUT, this is never a "retry" outcome. */
    WS_CLIENT_STATUS_CLOSED,
    /** @brief Any other failure (bad URL, TLS failure, protocol error). */
    WS_CLIENT_STATUS_ERROR
} WS_Client_Status;

/**
 * @brief WebSocket operation result.
 */
typedef struct {
#ifdef ARENA_IMPLEMENTATION
    /** @brief Optional arena used by the owned error string. */
    Arena *allocator;
#endif // ARENA_IMPLEMENTATION
    /** @brief RFC 6455 close code: from a received CLOSE frame for recv/recv_frame results, or
     *         the code SENT for a websocket_client_close result (that call never receives one of
     *         its own). 0 when no close code is known (any other CLOSED cause, or the operation
     *         has not produced one). */
    U16 close_code;
    /** @brief Transport status code from the WebSocket backend (CURLcode); for diagnostics and logs only — branch on status instead. */
    I32 code;
    /** @brief Transport error text. */
    String error;
    /** @brief Backend-independent outcome; the field callers should branch on. */
    WS_Client_Status status;
} WS_Client_Result;

/*==============================================================================
 * MARK: - API
 *============================================================================*/

/**
 * @brief Send a close frame (CURLWS_CLOSE) and mark the intent to disconnect cleanly.
 *
 * This does not free the handle: websocket_client_delete is still required afterwards.
 * Unlike delete (which just drops the TCP connection), close gives the peer an explicit
 * RFC 6455 close handshake instead of logging the disconnect as abnormal (code 1006).
 *
 * On a successful send this call itself returns WS_CLIENT_STATUS_OK (branch on status, as
 * always) - but the handle is also marked dead behind that OK, same as the message-size-cap
 * overflow: the close frame has already told the peer this side is done, so every later
 * send/recv/recv_frame on this handle answers WS_CLIENT_STATUS_CLOSED immediately without
 * touching the network. Create a new handle to reconnect - websocket_client_connect_1/_2 refuse a
 * handle that already holds a connection. Do not call this after a receive has already reported
 * WS_CLIENT_STATUS_CLOSED for a peer-initiated CLOSE - the module already answered it and the
 * handle is already dead. Loops through
 * CURLE_AGAIN the same way websocket_client_send does; a send deadline exceeded with a partial
 * frame on the wire answers WS_CLIENT_STATUS_CLOSED instead (never TIMEOUT - there is nothing
 * healthy left to retry) and marks the handle dead. Calling this on an already-dead handle is a
 * no-op that answers WS_CLIENT_STATUS_CLOSED without touching the network - so CLOSED from this
 * function means "no frame went out" (already dead, or the deadline), never "it went out".
 *
 * `code` is not validated against RFC 6455 §7.4 (the legal range 1000-4999, excluding the
 * reserved 1004/1005/1006/1015): whatever is passed goes on the wire as-is.
 *
 * @param self WebSocket client handle.
 * @param code RFC 6455 close code (1000 = normal closure).
 * @param reason Optional UTF-8 close reason (may be NULL for none); truncated to fit the
 *               125-byte control-frame payload alongside the 2-byte code.
 * @return Operation result; WS_CLIENT_STATUS_OK on a successful send, with close_code echoing
 *         the code SENT (this call never receives a close code of its own); CLOSED for the
 *         already-dead no-op or a send deadline exceeded with a partial frame on the wire.
 */
WS_Client_Result websocket_client_close(WS_Client *const self, U16 const code, char const *const reason);

/**
 * @brief Connect to a WebSocket endpoint (ws:// or wss://).
 *
 * A connect that failed before the handshake completed (a curl error) leaves the handle
 * reusable: the URL and CONNECT_ONLY option persist on the curl handle, and any request headers
 * set for a previous attempt are cleared before this one. A subprotocol mismatch is a COMPLETED
 * connect, not a failure - delete the handle rather than retrying on it.
 *
 * One connection per handle: calling this again on a handle that already completed a connect
 * (subprotocol mismatch included) is REFUSED with WS_CLIENT_STATUS_ERROR (code
 * CURLE_BAD_FUNCTION_ARGUMENT), before any option is set or the handshake attempted. libcurl has
 * no way to close a single CONNECT_ONLY connection short of
 * curl_easy_cleanup, so silently reconnecting would park the old socket in libcurl's per-handle
 * connection cache until its default MAXCONNECTS (5) evicts it or the handle is deleted - a leak
 * of one socket and one peer session per reconnect. To reconnect: websocket_client_delete this
 * handle and websocket_client_new a fresh one.
 *
 * @param self WebSocket client handle.
 * @param url WebSocket URL.
 * @return Operation result; WS_CLIENT_STATUS_ERROR with code CURLE_BAD_FUNCTION_ARGUMENT if this
 *         handle already holds a connection.
 */
WS_Client_Result websocket_client_connect_1(WS_Client *const self, char const *const url);

/**
 * @brief Connect to a WebSocket endpoint requesting a subprotocol.
 *
 * Sends "Sec-WebSocket-Protocol: <subprotocol>" during the handshake and verifies the reply:
 * if the server's response omits the header or echoes a different value, the connect fails with
 * WS_CLIENT_STATUS_ERROR instead of silently succeeding against a peer that ignored the request.
 * Refuses like websocket_client_connect_1 if this handle already holds a connection.
 *
 * @param self WebSocket client handle.
 * @param url WebSocket URL.
 * @param subprotocol Subprotocol name to request (must not be NULL).
 * @return Operation result.
 */
WS_Client_Result websocket_client_connect_2(WS_Client *const self, char const *const url, char const *const subprotocol);

/**
 * @brief Free and cleanup a WebSocket client handle.
 * @param self Pointer to WS_Client pointer.
 */
void websocket_client_delete(WS_Client **const self);

/**
 * @brief Create and initialize a new WebSocket client handle. Never returns null (aborts if
 *        curl_easy_init fails). Seeds a default ~30 second connect timeout, overridable via
 *        websocket_client_set_connect_timeout_ms.
 * @return Pointer to new WS_Client.
 */
WS_Client* websocket_client_new(void);

/**
 * @brief Receive one complete message, reassembling fragmented frames. BLOCKS until a message
 *        arrives or the connection fails — an idle peer blocks forever. Equivalent to
 *        websocket_client_recv_2 called with timeout_ms = USIZE_MAX.
 *
 * Use websocket_client_recv_2 in any loop that must stay responsive (shutdown flags, a shared
 * handle, a UI frame budget): CONNECT_ONLY handles run no transfer loop, so
 * websocket_client_set_timeout_ms does NOT bound this call.
 *
 * PING/PONG control frames received mid-message are consumed transparently (libcurl auto-replies
 * PONG to a PING) and never appear in `out`. A CLOSE frame ends the call with
 * WS_CLIENT_STATUS_CLOSED and a populated close_code instead of being appended as message bytes;
 * the module replies with a best-effort echoing CLOSE frame and marks the handle dead before
 * returning - do not call websocket_client_close afterwards. This call never reports the
 * message's TEXT/BINARY kind - use websocket_client_recv_frame_1/_2 for that.
 *
 * @param self WebSocket client handle.
 * @param out Destination buffer; the message payload is appended (see the header banner's
 *            buffer-reset idiom).
 * @return Operation result.
 */
WS_Client_Result websocket_client_recv_1(WS_Client *const self, String *const out);

/**
 * @brief Receive one complete message, giving up after a time budget.
 *
 * Returns status WS_CLIENT_STATUS_TIMEOUT when nothing arrived within timeout_ms — the connection
 * is still healthy and the call may be retried. One budget covers the whole message: continuation
 * frames share it, so a peer that drips fragments cannot extend the wait indefinitely; a stream of
 * tiny continuation frames that never completes a message still returns TIMEOUT once the shared
 * budget is spent. A peer-initiated CLOSE is answered and the handle marked dead, same as recv_1.
 * This call never reports the message's TEXT/BINARY kind - use recv_frame_1/_2 for that.
 *
 * IMPORTANT on retry: `out` is a reassembly buffer, not an all-or-nothing output (see the header
 * banner's buffer-reset idiom) — retry a TIMEOUT with the SAME String.
 *
 * Reassembly is bounded (see the header banner's message size cap): both a single oversized frame
 * and an over-long chain of continuation frames fail with status WS_CLIENT_STATUS_CLOSED and mark
 * the handle dead, rather than growing `out` without bound. The same bound applies to
 * websocket_client_recv_frame_1/_2.
 *
 * @param self WebSocket client handle.
 * @param out Destination buffer; the message payload is appended.
 * @param timeout_ms Budget in milliseconds; 0 = check once and return immediately if nothing is
 *                    available, USIZE_MAX = block indefinitely (same as _1). Enforced by waiting
 *                    on the handle's socket, so the real wait tracks the budget closely rather
 *                    than rounding up to a fixed poll interval.
 * @return Operation result.
 */
WS_Client_Result websocket_client_recv_2(WS_Client *const self, String *const out, USize const timeout_ms);

/**
 * @brief Receive one complete frame. BLOCKS until a frame arrives or the connection fails.
 *        Equivalent to websocket_client_recv_frame_2 called with timeout_ms = USIZE_MAX.
 *
 * A single logical message larger than the internal 64 KiB read buffer arrives as multiple
 * WS_CLIENT_STATUS_OK calls of the SAME frame (libcurl's CURLWS_OFFSET chunking) before
 * out_final is finally set — "frame granularity" here means "one WHOLE frame after this
 * function's own internal chunk loop", not one syscall. A CLOSE frame is answered (best-effort)
 * and marks the handle dead the same as recv_1/_2, reported as WS_CLIENT_STATUS_CLOSED.
 *
 * @param self WebSocket client handle.
 * @param out Destination buffer; the frame payload is appended.
 * @param out_final Optional; set true when this frame ends the message (no continuation). May be null.
 * @param out_type Optional; set to the frame's TEXT/BINARY kind when that flag is present on the
 *                  chunk delivered (RFC 6455 only carries it on the first fragment of a message —
 *                  a later continuation chunk is re-reported with the same value). May be null.
 * @return Operation result.
 */
WS_Client_Result websocket_client_recv_frame_1(WS_Client *const self, String *const out, bool *const out_final, WS_Client_Type *const out_type);

/**
 * @brief Receive one complete frame, giving up after a time budget.
 * @param self WebSocket client handle.
 * @param out Destination buffer; the frame payload is appended.
 * @param out_final Optional; set true when this frame ends the message (no continuation). May be null.
 * @param out_type Optional; see websocket_client_recv_frame_1. May be null.
 * @param timeout_ms Budget in milliseconds; 0 = check once, USIZE_MAX = block indefinitely (same as _1).
 * @return Operation result; WS_CLIENT_STATUS_TIMEOUT when the budget expired with no frame.
 */
WS_Client_Result websocket_client_recv_frame_2(WS_Client *const self, String *const out, bool *const out_final, WS_Client_Type *const out_type, USize const timeout_ms);

/**
 * @brief Initialize a WebSocket operation result.
 * @return Initialized result.
 */
WS_Client_Result websocket_client_result_init(void);

/**
 * @brief Release result storage.
 * @param self Result instance.
 */
void websocket_client_result_uninit(WS_Client_Result *const self);

/**
 * @brief Send a message, looping through CURLE_AGAIN (from `sent` onward) until every byte is
 *        on the wire or an internal send deadline passes. A frame is never left partially
 *        written on success: if the deadline passes first, the call answers
 *        WS_CLIENT_STATUS_CLOSED (never TIMEOUT - there is nothing healthy to retry) and marks
 *        the handle dead, since a peer that has received a frame header promising more bytes
 *        than arrived will misparse every later frame. A size of 0 sends no frame at all and
 *        answers WS_CLIENT_STATUS_OK. Text frames are never UTF-8 validated by this module
 *        either way (RFC 6455 §5.6 / close code 1007).
 * @param self WebSocket client handle.
 * @param data Payload bytes.
 * @param size Payload size in bytes.
 * @param type Payload kind (text or binary).
 * @return Operation result.
 */
WS_Client_Result websocket_client_send(WS_Client *const self, void const *const data, USize const size, WS_Client_Type const type);

/**
 * @brief Set the CA bundle libcurl uses to verify the peer's TLS certificate (CURLOPT_CAINFO),
 *        for wss:// connections. Only relevant when websocket_client_set_verify_tls(self, true) —
 *        the default — is in effect; without it, this is a no-op.
 * @param self WebSocket client handle.
 * @param path Filesystem path to a CA bundle file (must not be NULL).
 */
void websocket_client_set_ca_bundle(WS_Client *const self, char const *const path);

/**
 * @brief Set the connect (handshake) timeout in milliseconds (CURLOPT_CONNECTTIMEOUT_MS).
 *        0 forwards to libcurl's own default (currently 300 s); USIZE_MAX also forwards 0 —
 *        libcurl has no explicit "wait forever" connect timeout, so 0 (its own default/no extra
 *        cap) is the closest available meaning. websocket_client_new seeds a ~30 s default;
 *        call this to override it before connecting.
 * @param self WebSocket client handle.
 * @param timeout_ms Timeout in milliseconds.
 */
void websocket_client_set_connect_timeout_ms(WS_Client *const self, USize const timeout_ms);

/**
 * @brief Set the per-handle reassembly cap (default 16 MiB) enforced by recv_1/_2/recv_frame_1/_2.
 *        See the header banner's message size cap section for what happens when it is exceeded.
 * @param self WebSocket client handle.
 * @param max_message_size Cap in bytes; must not be 0.
 */
void websocket_client_set_max_message_size(WS_Client *const self, USize const max_message_size);

/**
 * @brief Set the WebSocket handshake timeout in milliseconds (CURLOPT_TIMEOUT_MS). This bounds
 *        ONLY the CONNECT_ONLY handshake performed by connect_1/_2 (DNS + TCP + TLS + upgrade) —
 *        a CONNECT_ONLY handle runs no further transfer loop, so it does NOT bound recv_1/_2 or
 *        send; use those functions' own timeout_ms / internal deadline for that.
 *        0 forwards to libcurl's own "no timeout" default; USIZE_MAX also forwards 0.
 * @param self WebSocket client handle.
 * @param timeout_ms Timeout in milliseconds.
 */
void websocket_client_set_timeout_ms(WS_Client *const self, USize const timeout_ms);

/**
 * @brief Set TLS peer and host verification (applies to wss://). Both are ON by default —
 *        libcurl's own default — before this is ever called.
 * @param self WebSocket client handle.
 * @param verify_tls true to verify peer and host.
 */
void websocket_client_set_verify_tls(WS_Client *const self, bool const verify_tls);

#endif // WEBSOCKET_CLIENT_H