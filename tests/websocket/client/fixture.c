/*
 * fixture.c - implementation of the loopback RFC 6455 server fixture.
 * See fixture.h for the contract. Test-only scaffolding: plain libc +
 * CFW net/thread/char/memory, no websocket_client.h dependency (this is the
 * PEER the module under test talks to).
 */
#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <char/char.h>
#include <chrono/chrono.h>
#include <encoding/base64/base64.h>
#include <memory/memory.h>

#include <fixture.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

#define _FIXTURE_OPCODE_CONT   0x0
#define _FIXTURE_OPCODE_TEXT   0x1
#define _FIXTURE_OPCODE_BINARY 0x2
#define _FIXTURE_OPCODE_CLOSE  0x8
#define _FIXTURE_OPCODE_PING   0x9
#define _FIXTURE_OPCODE_PONG   0xA

#define _FIXTURE_WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define _FIXTURE_HANDSHAKE_BUFFER 8192
#define _FIXTURE_ACCEPT_TIMEOUT_MS 5000
#define _FIXTURE_DRAIN_LARGE_TIMEOUT_MS 15000
#define _FIXTURE_IDLE_SILENT_TIMEOUT_MS 3000
#define _FIXTURE_IO_TIMEOUT_MS 5000
#define _FIXTURE_PING_FLOOD_DRAIN_WINDOW_MS 1000
#define _FIXTURE_PING_FLOOD_POLL_TIMEOUT_MS 50

/*==============================================================================
 * MARK: - Byte-level helpers
 *============================================================================*/

static bool _fixture_recv_exact(Net_Socket const conn, void *const buf, USize const size) {
    USize received_total = 0;

    while (received_total < size) {
        USize received = 0;
        Result const r = net_socket_recv_1(conn, (U8*) buf + received_total, size - received_total, &received);

        if (result_is_error(r) || received == 0) {
            return false;
        }

        received_total += received;
    }

    return true;
}

static bool _fixture_send_all(Net_Socket const conn, void const *const buf, USize const size) {
    USize sent_total = 0;

    while (sent_total < size) {
        USize sent = 0;
        Result const r = net_socket_send_1(conn, (U8 const*) buf + sent_total, size - sent_total, &sent);

        if (result_is_error(r) || sent == 0) {
            return false;
        }

        sent_total += sent;
    }

    return true;
}

/* Skips (discards) a masked frame payload already announced by a header read. */
static bool _fixture_skip_payload(Net_Socket const conn, U64 const size) {
    char scratch[256] = DEFAULT_INITIALIZATION;
    U64 remaining = size;

    while (remaining > 0) {
        USize const chunk = remaining < sizeof(scratch) ? (USize) remaining : sizeof(scratch);

        if (!_fixture_recv_exact(conn, scratch, chunk)) {
            return false;
        }

        remaining -= chunk;
    }

    return true;
}

/*==============================================================================
 * MARK: - Frame I/O
 *============================================================================*/

/* Reads one client->server frame header (always masked per RFC 6455). */
static bool _fixture_read_frame_header(Net_Socket const conn, U8 *const opcode, bool *const fin, U64 *const payload_size, U8 mask_key[static 4]) {
    U8 head[2] = DEFAULT_INITIALIZATION;

    if (!_fixture_recv_exact(conn, head, sizeof(head))) {
        return false;
    }

    *fin = (head[0] & 0x80) != 0;
    *opcode = (U8) (head[0] & 0x0F);

    bool const masked = (head[1] & 0x80) != 0;
    U8 const length_field = head[1] & 0x7F;

    if (length_field == 126) {
        U8 extended[2] = DEFAULT_INITIALIZATION;

        if (!_fixture_recv_exact(conn, extended, sizeof(extended))) {
            return false;
        }

        *payload_size = ((U64) extended[0] << 8) | (U64) extended[1];
    } else if (length_field == 127) {
        U8 extended[8] = DEFAULT_INITIALIZATION;

        if (!_fixture_recv_exact(conn, extended, sizeof(extended))) {
            return false;
        }

        *payload_size = 0;

        for (USize index = 0; index < 8; index += 1) {
            *payload_size = (*payload_size << 8) | (U64) extended[index];
        }
    } else {
        *payload_size = length_field;
    }

    if (!masked) {
        return false; // a compliant client always masks; a bare TCP peer sending this is not one
    }

    return _fixture_recv_exact(conn, mask_key, 4);
}

/*
 * Reads one reassembled message (data frame + any CONTINUATIONs), transparently skipping
 * PING/PONG in between, mirroring what websocket_client itself is expected to do. Stops early
 * and reports a CLOSE frame instead, for the client-close pin.
 */
static bool _fixture_read_message(Net_Socket const conn, U8 *const buffer, USize const capacity, USize *const out_size, bool *const out_saw_close, U16 *const out_close_code) {
    USize total = 0;
    bool complete = false;

    *out_saw_close = false;

    while (!complete) {
        U8 opcode = 0;
        bool fin = false;
        U64 payload_size = 0;
        U8 mask_key[4] = DEFAULT_INITIALIZATION;

        if (!_fixture_read_frame_header(conn, &opcode, &fin, &payload_size, mask_key)) {
            return false;
        }

        if (opcode == _FIXTURE_OPCODE_PING || opcode == _FIXTURE_OPCODE_PONG) {
            if (!_fixture_skip_payload(conn, payload_size)) {
                return false;
            }

            continue;
        }

        if (opcode == _FIXTURE_OPCODE_CLOSE) {
            U8 close_payload[125] = DEFAULT_INITIALIZATION;
            USize const close_size = payload_size < sizeof(close_payload) ? (USize) payload_size : sizeof(close_payload);

            if (close_size > 0 && !_fixture_recv_exact(conn, close_payload, close_size)) {
                return false;
            }

            for (USize index = 0; index < close_size; index += 1) {
                close_payload[index] ^= mask_key[index % 4];
            }

            *out_saw_close = true;
            *out_close_code = close_size >= 2 ? (U16) (((U16) close_payload[0] << 8) | close_payload[1]) : 0;
            *out_size = total;

            return true;
        }

        if (payload_size > (U64) capacity || (U64) total > (U64) capacity - payload_size) {
            return false;
        }

        if (payload_size > 0 && !_fixture_recv_exact(conn, buffer + total, (USize) payload_size)) {
            return false;
        }

        for (U64 index = 0; index < payload_size; index += 1) {
            buffer[total + index] ^= mask_key[index % 4];
        }

        total += (USize) payload_size;
        complete = fin;
    }

    *out_size = total;

    return true;
}

static bool _fixture_send_frame(Net_Socket const conn, U8 const opcode, bool const fin, void const *const payload, USize const payload_size) {
    U8 header[10] = DEFAULT_INITIALIZATION;
    USize header_size = 0;

    header[0] = (U8) ((fin ? 0x80 : 0x00) | (opcode & 0x0F));

    if (payload_size <= 125) {
        header[1] = (U8) payload_size;
        header_size = 2;
    } else if (payload_size <= 65535) {
        header[1] = 126;
        header[2] = (U8) ((payload_size >> 8) & 0xFF);
        header[3] = (U8) (payload_size & 0xFF);
        header_size = 4;
    } else {
        header[1] = 127;

        for (USize index = 0; index < 8; index += 1) {
            header[2 + index] = (U8) ((payload_size >> (8 * (7 - index))) & 0xFF);
        }

        header_size = 10;
    }

    if (!_fixture_send_all(conn, header, header_size)) {
        return false;
    }

    return payload_size == 0 || _fixture_send_all(conn, payload, payload_size);
}

/*==============================================================================
 * MARK: - Handshake
 *============================================================================*/

/** @brief Locates a "\r\n"-terminated header value (case-insensitive name) inside a NUL-terminated header blob. */
static bool _fixture_header_find(char const *const headers, char const *const name, char *const out, USize const out_capacity) {
    USize const name_size = char_length(name);
    char const *line = headers;

    while (line != nullptr && line[0] != '\0') {
        char const *const eol = strstr(line, "\r\n");
        USize const line_size = eol != nullptr ? (USize) (eol - line) : char_length(line);

        if (line_size >= name_size) {
            bool matches = true;

            for (USize index = 0; index < name_size; index += 1) {
                char const a = line[index];
                char const b = name[index];
                char const a_lower = (a >= 'A' && a <= 'Z') ? (char) (a - 'A' + 'a') : a;
                char const b_lower = (b >= 'A' && b <= 'Z') ? (char) (b - 'A' + 'a') : b;

                if (a_lower != b_lower) {
                    matches = false;

                    break;
                }
            }

            if (matches) {
                USize value_start = name_size;

                while (value_start < line_size && (line[value_start] == ' ' || line[value_start] == '\t')) {
                    value_start += 1;
                }

                USize const value_size = line_size - value_start;
                USize const copy_size = value_size < out_capacity - 1 ? value_size : out_capacity - 1;

                memory_copy_1((void*) out, (void const*) (line + value_start), copy_size);

                out[copy_size] = '\0';

                return true;
            }
        }

        if (eol == nullptr) {
            break;
        }

        line = eol + 2;
    }

    return false;
}

static void _fixture_accept_key(char const *const client_key, char out[static 32]) {
    char combined[256] = DEFAULT_INITIALIZATION;
    I32 const written = snprintf(combined, sizeof(combined), "%s%s", client_key, _FIXTURE_WS_GUID);

    if (written < 0 || (USize) written >= sizeof(combined)) {
        out[0] = '\0';

        return;
    }

    U8 digest[EVP_MAX_MD_SIZE] = DEFAULT_INITIALIZATION;
    U32 digest_size = 0;

    EVP_Digest(combined, (USize) written, digest, &digest_size, EVP_sha1(), nullptr);

    if (digest_size != 20) {
        out[0] = '\0';

        return;
    }

    encoding_base64_encode_1(digest, digest_size, out);
}

/* Reads the client's HTTP upgrade request and replies 101. `mismatch_subprotocol` forces the
 * response to echo a value the client never asked for (the wrong-subprotocol pin). */
static bool _fixture_handshake(Net_Socket const conn, bool const mismatch_subprotocol) {
    char request[_FIXTURE_HANDSHAKE_BUFFER] = DEFAULT_INITIALIZATION;
    USize request_size = 0;

    while (request_size < sizeof(request) - 1) {
        USize received = 0;
        Result const r = net_socket_recv_1(conn, request + request_size, sizeof(request) - 1 - request_size, &received);

        if (result_is_error(r) || received == 0) {
            return false;
        }

        request_size += received;
        request[request_size] = '\0';

        if (strstr(request, "\r\n\r\n") != nullptr) {
            break;
        }
    }

    char client_key[128] = DEFAULT_INITIALIZATION;

    if (!_fixture_header_find(request, "sec-websocket-key:", client_key, sizeof(client_key))) {
        return false;
    }

    char accept_key[32] = DEFAULT_INITIALIZATION;

    _fixture_accept_key(client_key, accept_key);

    char requested_protocol[128] = DEFAULT_INITIALIZATION;
    bool const has_protocol = _fixture_header_find(request, "sec-websocket-protocol:", requested_protocol, sizeof(requested_protocol));

    char response[512] = DEFAULT_INITIALIZATION;
    I32 written = 0;

    if (has_protocol) {
        char const *const echoed = mismatch_subprotocol ? "unrequested-protocol" : requested_protocol;

        written = snprintf(response, sizeof(response),
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: %s\r\n"
            "Sec-WebSocket-Protocol: %s\r\n"
            "\r\n", accept_key, echoed);
    } else {
        written = snprintf(response, sizeof(response),
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: %s\r\n"
            "\r\n", accept_key);
    }

    return written > 0 && _fixture_send_all(conn, response, (USize) written);
}

/*==============================================================================
 * MARK: - Scripts
 *============================================================================*/

static void _fixture_run_client_close(Net_Socket const conn, Fixture_Server *const self) {
    U8 scratch[256] = DEFAULT_INITIALIZATION;
    USize size = 0;
    bool saw_close = false;
    U16 close_code = 0;

    net_socket_set_timeout(conn, _FIXTURE_IO_TIMEOUT_MS);
    _fixture_read_message(conn, scratch, sizeof(scratch), &size, &saw_close, &close_code);

    self->saw_client_close = saw_close;
    self->client_close_code = close_code;
}

static void _fixture_run_close(Net_Socket const conn, U16 const code, char const *const reason, Fixture_Server *const self) {
    U8 frame[125] = DEFAULT_INITIALIZATION;

    frame[0] = (U8) ((code >> 8) & 0xFF);
    frame[1] = (U8) (code & 0xFF);

    USize const reason_size = reason != nullptr ? char_length(reason) : 0;
    USize const copy_size = reason_size < sizeof(frame) - 2 ? reason_size : sizeof(frame) - 2;

    if (copy_size > 0) {
        memory_copy_1((void*) (frame + 2), (void const*) reason, copy_size);
    }

    _fixture_send_frame(conn, _FIXTURE_OPCODE_CLOSE, true, frame, 2 + copy_size);

    // RFC 6455 5.5.1: the client MUST answer a CLOSE with one of its own. Read for it (bounded by
    // the connection's own IO timeout) - this is what proves websocket_client's echo-on-receive
    // actually reaches the wire, not just that it compiled the reply bytes.
    U8 echo_buffer[64] = DEFAULT_INITIALIZATION;
    USize echo_size = 0;
    bool saw_echo = false;
    U16 echo_code = 0;

    _fixture_read_message(conn, echo_buffer, sizeof(echo_buffer), &echo_size, &saw_echo, &echo_code);

    self->saw_client_close = saw_echo;
    self->client_close_code = echo_code;
}

static void _fixture_run_drain_large(Net_Socket const conn, Fixture_Server *const self) {
    thread_sleep(self->pre_read_delay_ms);

    U8 *const buffer = (U8*) malloc(self->payload_size);

    if (buffer == nullptr) {
        return;
    }

    net_socket_set_timeout(conn, _FIXTURE_DRAIN_LARGE_TIMEOUT_MS);

    USize size = 0;
    bool saw_close = false;
    U16 close_code = 0;
    bool const read_ok = _fixture_read_message(conn, buffer, self->payload_size, &size, &saw_close, &close_code);

    self->drain_received = size;
    self->drain_ok = read_ok && !saw_close && size == self->payload_size
        && memcmp(buffer, self->payload, self->payload_size) == 0;

    free(buffer);
}

/* ECHO test messages are always a single unfragmented frame, so the opcode and payload can be
 * read directly without the generic reassembler (which exists for the multi-frame scripts). */
static void _fixture_run_echo(Net_Socket const conn) {
    U8 buffer[65536] = DEFAULT_INITIALIZATION;
    U8 opcode = 0;
    bool fin = false;
    U64 payload_size = 0;
    U8 mask_key[4] = DEFAULT_INITIALIZATION;

    if (!_fixture_read_frame_header(conn, &opcode, &fin, &payload_size, mask_key) || payload_size > sizeof(buffer)) {
        return;
    }

    if (payload_size > 0 && !_fixture_recv_exact(conn, buffer, (USize) payload_size)) {
        return;
    }

    for (U64 index = 0; index < payload_size; index += 1) {
        buffer[index] ^= mask_key[index % 4];
    }

    _fixture_send_frame(conn, opcode, true, buffer, (USize) payload_size);
}

static void _fixture_run_fragment_3(Net_Socket const conn) {
    char const *const message = "Hello, fragmented world across three wire frames!";
    USize const total = char_length(message);
    USize const third = total / 3;

    _fixture_send_frame(conn, _FIXTURE_OPCODE_TEXT, false, message, third);
    _fixture_send_frame(conn, _FIXTURE_OPCODE_CONT, false, message + third, third);
    _fixture_send_frame(conn, _FIXTURE_OPCODE_CONT, true, message + (2 * third), total - (2 * third));
}

/* Sends fragment 1, sleeps `delay_ms`, sends fragment 2 (final) - for pinning a TIMEOUT that
 * lands mid-message (a prefix already appended to `out`) rather than before any bytes arrive. */
static void _fixture_run_fragment_delay(Net_Socket const conn, USize const delay_ms) {
    char const *const first_half = "before-the-pause-";
    char const *const second_half = "after-the-pause-completes-the-message";

    _fixture_send_frame(conn, _FIXTURE_OPCODE_TEXT, false, first_half, char_length(first_half));
    thread_sleep(delay_ms);
    _fixture_send_frame(conn, _FIXTURE_OPCODE_CONT, true, second_half, char_length(second_half));
}

static void _fixture_run_idle_silent(Net_Socket const conn, USize const delay_ms) {
    if (delay_ms > 0) {
        thread_sleep(delay_ms);

        char const *const message = "delayed-hello";

        _fixture_send_frame(conn, _FIXTURE_OPCODE_TEXT, true, message, char_length(message));
    }

    /* Otherwise: say nothing for the recv-timeout-latency measurement. Bound the thread's own
     * lifetime so the suite cannot hang if the client never closes. */
    U8 scratch[16] = DEFAULT_INITIALIZATION;
    USize received = 0;

    net_socket_set_timeout(conn, _FIXTURE_IDLE_SILENT_TIMEOUT_MS);
    net_socket_recv_1(conn, scratch, sizeof(scratch), &received);
}

static void _fixture_run_oversized(Net_Socket const conn, U8 const *const payload, USize const payload_size) {
    _fixture_send_frame(conn, _FIXTURE_OPCODE_BINARY, true, payload, payload_size);
}

static void _fixture_run_ping_flood(Net_Socket const conn, USize const ping_count) {
    for (USize index = 0; index < ping_count; index += 1) {
        char payload[16] = DEFAULT_INITIALIZATION;
        I32 const written = snprintf(payload, sizeof(payload), "p%zu", index);
        USize const payload_size = written < 0
            ? 0
            : ((USize) written < sizeof(payload) - 1 ? (USize) written : sizeof(payload) - 1);

        _fixture_send_frame(conn, _FIXTURE_OPCODE_PING, true, payload, payload_size);
    }

    /* Then nothing further from THIS side - but libcurl auto-replies PONG to every PING, so the
     * connection must stay open and draining long enough for all of those to land, or the
     * client's next send (its own PONG) hits a socket this side already closed. Drain for a
     * bounded window rather than a single recv call. */
    net_socket_set_timeout(conn, _FIXTURE_PING_FLOOD_POLL_TIMEOUT_MS);

    ChronoInstant const drain_start = chrono_now();

    while (chrono_duration_milliseconds(chrono_elapsed(drain_start)) < _FIXTURE_PING_FLOOD_DRAIN_WINDOW_MS) {
        U8 scratch[64] = DEFAULT_INITIALIZATION;
        USize received = 0;
        Result const r = net_socket_recv_1(conn, scratch, sizeof(scratch), &received);

        if (result_is_success(r) && received == 0) {
            break; // peer closed
        }
    }
}

static void _fixture_run_ping_mid(Net_Socket const conn) {
    char const *const first_half = "first-half-of-the-message-";
    char const *const second_half = "second-half-arrives-after-a-ping";
    char const *const ping_payload = "mid-message-ping";

    _fixture_send_frame(conn, _FIXTURE_OPCODE_TEXT, false, first_half, char_length(first_half));
    _fixture_send_frame(conn, _FIXTURE_OPCODE_PING, true, ping_payload, char_length(ping_payload));
    _fixture_send_frame(conn, _FIXTURE_OPCODE_CONT, true, second_half, char_length(second_half));
}

/*==============================================================================
 * MARK: - Thread entry
 *============================================================================*/

static void* _fixture_thread_main(void *const data) {
    Fixture_Server *const self = (Fixture_Server*) data;

    Net_Wait ready = 0;

    if (result_is_error(net_socket_wait(self->listen_socket, NET_WAIT_READ, _FIXTURE_ACCEPT_TIMEOUT_MS, &ready)) || ready == 0) {
        return nullptr;
    }

    Net_Socket conn = NET_SOCKET_INVALID;

    if (result_is_error(net_socket_accept(self->listen_socket, &conn, nullptr))) {
        return nullptr;
    }

    net_socket_set_timeout(conn, _FIXTURE_IO_TIMEOUT_MS);

    bool const handshake_ok = _fixture_handshake(conn, self->script == FIXTURE_SCRIPT_WRONG_SUBPROTOCOL);

    if (handshake_ok) {
        switch (self->script) {
            case FIXTURE_SCRIPT_ECHO:              { _fixture_run_echo(conn);                             break; }
            case FIXTURE_SCRIPT_FRAGMENT_3:        { _fixture_run_fragment_3(conn);                       break; }
            case FIXTURE_SCRIPT_PING_MID:          { _fixture_run_ping_mid(conn);                         break; }
            case FIXTURE_SCRIPT_PING_FLOOD:        { _fixture_run_ping_flood(conn, self->ping_count);     break; }
            case FIXTURE_SCRIPT_OVERSIZED:         { _fixture_run_oversized(conn, self->payload, self->payload_size); break; }
            case FIXTURE_SCRIPT_CLOSE:             { _fixture_run_close(conn, self->close_code, self->close_reason, self); break; }
            case FIXTURE_SCRIPT_FRAGMENT_DELAY:    { _fixture_run_fragment_delay(conn, self->pre_read_delay_ms);         break; }
            case FIXTURE_SCRIPT_WRONG_SUBPROTOCOL: { /* mismatch already sent during handshake */         break; }
            case FIXTURE_SCRIPT_CLIENT_CLOSE:      { _fixture_run_client_close(conn, self);               break; }
            case FIXTURE_SCRIPT_DRAIN_LARGE:       { _fixture_run_drain_large(conn, self);                break; }
            case FIXTURE_SCRIPT_IDLE_SILENT:       { _fixture_run_idle_silent(conn, self->pre_read_delay_ms); break; }
        }
    }

    net_socket_close(conn);

    return nullptr;
}

/*==============================================================================
 * MARK: - Public API
 *============================================================================*/

bool fixture_server_start(Fixture_Server *const self) {
    if (self == nullptr) {
        return false;
    }

    if (result_is_error(net_socket_init(&self->listen_socket, NET_FAMILY_IPV4, NET_TYPE_TCP))) {
        return false;
    }

    net_socket_set_reuse_address(self->listen_socket, true);

    Net_Socket_Address address = net_socket_address_init_1(NET_FAMILY_IPV4, 0);

    if (result_is_error(net_socket_bind(self->listen_socket, &address))
        || result_is_error(net_socket_listen(self->listen_socket, 1))
        || result_is_error(net_socket_address_local(self->listen_socket, &address))) {
        net_socket_close(self->listen_socket);

        return false;
    }

    self->port = net_socket_address_port(&address);

    if (result_is_error(thread_create_1(&self->thread, _fixture_thread_main, self))) {
        net_socket_close(self->listen_socket);

        return false;
    }

    return true;
}

U16 fixture_server_port(Fixture_Server const *const self) {
    return self->port;
}

void fixture_server_join(Fixture_Server *const self) {
    thread_join_1(&self->thread);
    net_socket_close(self->listen_socket);
}