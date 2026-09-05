/*
 * test_loopback.c - Loopback-fixture tests for the CFW WebSocket client module.
 *
 * See test_all.c's header comment for the gap this closes: offline unit tests pin lifecycle
 * only, nothing on the wire. This suite runs a minimal RFC 6455 server (fixture.h) on a
 * CFW thread over 127.0.0.1 (OS-assigned port) and drives the real websocket_client against it.
 *
 * Pinned here: echo round trip (text + binary), 3-way fragment reassembly, a PING interleaved
 * mid-message, a PING flood against a bounded recv_2 timeout, the message-size-cap CLOSED +
 * dead-handle contract (recv/send/recv_frame, and the CURLE_TOO_LARGE code), a peer CLOSE with
 * code + reason (and its RFC 6455 5.5.1 echo observed server-side), wrong-subprotocol connect
 * rejection (plus a second connect on the same handle refused afterward), an ACCEPTED connect_2
 * subprotocol, websocket_client_close reaching the peer as a real CLOSE frame (status OK on a
 * successful send), 4 MiB send integrity under measured backpressure, recv_2 timeout latency,
 * TIMEOUT-then-retry-with-the-same-String (both a whole late message and a mid-message prefix),
 * recv_2(0) buffered-vs-idle, a second connect_1 on a handle that already holds a connection
 * being refused while the first connection keeps working, a >64 KiB single-message receive, and
 * the frame tier (recv_frame_2 out_final/out_type).
 *
 * No network beyond loopback; every port is OS-assigned (net_socket_address_local after a
 * port-0 bind); every wait is timeout-bounded so a broken peer cannot hang the suite.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <websocket/client/websocket_client.h>
#include <chrono/chrono.h>
#include <log/log.h>
#include <test/test.h>

#include <fixture.h>

/* ── helpers ──────────────────────────────────────────────────────────────── */

static void _url_for_port(U16 const port, char *const out, USize const out_capacity) {
    snprintf(out, out_capacity, "ws://127.0.0.1:%u/", (unsigned) port);
}

/* Starts `server` (script/fields already set by the caller) and connects a fresh websocket_client
 * to it via connect_1, asserting both steps. Returns the client on success, or nullptr if the
 * fixture failed to start (nothing else to clean up in that case - the caller's usual
 * test_case_end/return still applies). Not used by cases that need connect_2, a pre-connect
 * setter, or more than one fixture. */
static WS_Client* _connect_to_fixture(Test *const test, Fixture_Server *const server) {
    if (!test_expect_true(test, "fixture started", fixture_server_start(server))) {
        return nullptr;
    }

    char url[64] = DEFAULT_INITIALIZATION;
    _url_for_port(fixture_server_port(server), url, sizeof(url));

    WS_Client *const client = websocket_client_new();
    WS_Client_Result connect_result = websocket_client_connect_1(client, url);

    test_expect_true(test, "connect OK", connect_result.status == WS_CLIENT_STATUS_OK);
    websocket_client_result_uninit(&connect_result);

    return client;
}

/* ── test cases ───────────────────────────────────────────────────────────── */

static void _test_echo_text(Test *const test) {
    test_case_begin(test, "loopback: echo round trip, text");

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_ECHO;

    WS_Client *client = _connect_to_fixture(test, &server);

    if (client == nullptr) {
        test_case_end(test);

        return;
    }

    char const *const message = "hello-loopback-text";
    WS_Client_Result send_result = websocket_client_send(client, message, strlen(message), WS_CLIENT_TEXT);

    test_expect_true(test, "send OK", send_result.status == WS_CLIENT_STATUS_OK);
    websocket_client_result_uninit(&send_result);

    String out = string_init_1();
    WS_Client_Result recv_result = websocket_client_recv_2(client, &out, 2000);

    test_expect_true(test, "recv OK", recv_result.status == WS_CLIENT_STATUS_OK);
    test_expect_true(test, "echoed text matches", string_get_size(&out) == strlen(message)
        && memcmp(string_get_data(&out), message, strlen(message)) == 0);

    websocket_client_result_uninit(&recv_result);
    string_uninit(&out);
    websocket_client_delete(&client);
    fixture_server_join(&server);

    test_case_end(test);
}

static void _test_echo_binary(Test *const test) {
    test_case_begin(test, "loopback: echo round trip, binary");

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_ECHO;

    WS_Client *client = _connect_to_fixture(test, &server);

    if (client == nullptr) {
        test_case_end(test);

        return;
    }

    U8 const payload[6] = { 0x00, 0x01, 0x02, 0x7F, 0xFE, 0xFF };
    WS_Client_Result send_result = websocket_client_send(client, payload, sizeof(payload), WS_CLIENT_BINARY);

    test_expect_true(test, "send OK", send_result.status == WS_CLIENT_STATUS_OK);
    websocket_client_result_uninit(&send_result);

    String out = string_init_1();
    WS_Client_Result recv_result = websocket_client_recv_2(client, &out, 2000);

    test_expect_true(test, "recv OK", recv_result.status == WS_CLIENT_STATUS_OK);
    test_expect_true(test, "echoed bytes match", string_get_size(&out) == sizeof(payload)
        && memcmp(string_get_data(&out), payload, sizeof(payload)) == 0);

    websocket_client_result_uninit(&recv_result);
    string_uninit(&out);
    websocket_client_delete(&client);
    fixture_server_join(&server);

    test_case_end(test);
}

static void _test_fragment_reassembly(Test *const test) {
    test_case_begin(test, "loopback: 3-fragment message reassembles intact");

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_FRAGMENT_3;

    WS_Client *client = _connect_to_fixture(test, &server);

    if (client == nullptr) {
        test_case_end(test);

        return;
    }

    char const *const expected = "Hello, fragmented world across three wire frames!";
    String out = string_init_1();
    WS_Client_Result recv_result = websocket_client_recv_2(client, &out, 2000);

    test_expect_true(test, "recv OK", recv_result.status == WS_CLIENT_STATUS_OK);
    test_expect_true(test, "reassembled message equals original", string_get_size(&out) == strlen(expected)
        && memcmp(string_get_data(&out), expected, strlen(expected)) == 0);

    websocket_client_result_uninit(&recv_result);
    string_uninit(&out);
    websocket_client_delete(&client);
    fixture_server_join(&server);

    test_case_end(test);
}

static void _test_ping_mid_message(Test *const test) {
    test_case_begin(test, "loopback: PING between fragments 1 and 2 does not corrupt the message");

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_PING_MID;

    WS_Client *client = _connect_to_fixture(test, &server);

    if (client == nullptr) {
        test_case_end(test);

        return;
    }

    char const *const expected = "first-half-of-the-message-second-half-arrives-after-a-ping";
    String out = string_init_1();
    WS_Client_Result recv_result = websocket_client_recv_2(client, &out, 2000);

    test_expect_true(test, "recv OK, message is final and whole", recv_result.status == WS_CLIENT_STATUS_OK);
    test_expect_true(test, "message equals both halves, no ping bytes", string_get_size(&out) == strlen(expected)
        && memcmp(string_get_data(&out), expected, strlen(expected)) == 0);

    websocket_client_result_uninit(&recv_result);
    string_uninit(&out);
    websocket_client_delete(&client);
    fixture_server_join(&server);

    test_case_end(test);
}

static void _test_ping_flood_timeout(Test *const test) {
    test_case_begin(test, "loopback: PING flood + recv_2(100) -> TIMEOUT within budget");

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_PING_FLOOD;
    server.ping_count = 20;

    WS_Client *client = _connect_to_fixture(test, &server);

    if (client == nullptr) {
        test_case_end(test);

        return;
    }

    String out = string_init_1();
    ChronoInstant const start = chrono_now();
    WS_Client_Result recv_result = websocket_client_recv_2(client, &out, 100);
    U64 const elapsed_ms = chrono_duration_milliseconds(chrono_elapsed(start));

    test_expect_true(test, "status TIMEOUT (20 pings never count as a message)", recv_result.status == WS_CLIENT_STATUS_TIMEOUT);
    test_expect_true(test, "TIMEOUT honored the ~100ms budget (measured, not a fixed poll interval)", elapsed_ms >= 90 && elapsed_ms <= 1000);

    printf("  [measurement] ping-flood recv_2(100) TIMEOUT after %llu ms\n", (unsigned long long) elapsed_ms);

    websocket_client_result_uninit(&recv_result);
    string_uninit(&out);
    websocket_client_delete(&client);
    fixture_server_join(&server);

    test_case_end(test);
}

static void _test_oversized_marks_dead(Test *const test) {
    test_case_begin(test, "loopback: message over cap -> CLOSED and handle stays dead");

    U8 const payload[17] = DEFAULT_INITIALIZATION; // cap (16) + 1

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_OVERSIZED;
    server.payload = payload;
    server.payload_size = sizeof(payload);

    if (!test_expect_true(test, "fixture started", fixture_server_start(&server))) {
        test_case_end(test);

        return;
    }

    char url[64] = DEFAULT_INITIALIZATION;
    _url_for_port(fixture_server_port(&server), url, sizeof(url));

    WS_Client *client = websocket_client_new();

    websocket_client_set_max_message_size(client, 16);

    WS_Client_Result connect_result = websocket_client_connect_1(client, url);

    test_expect_true(test, "connect OK", connect_result.status == WS_CLIENT_STATUS_OK);
    websocket_client_result_uninit(&connect_result);

    String out = string_init_1();
    WS_Client_Result recv_result = websocket_client_recv_2(client, &out, 2000);

    test_expect_true(test, "cap+1 bytes -> CLOSED", recv_result.status == WS_CLIENT_STATUS_CLOSED);
    test_expect_i(test, "cap overflow reports code == CURLE_TOO_LARGE", (ISize) CURLE_TOO_LARGE, (ISize) recv_result.code);
    websocket_client_result_uninit(&recv_result);

    ChronoInstant const start = chrono_now();
    WS_Client_Result second_result = websocket_client_recv_2(client, &out, 2000);
    U64 const elapsed_ms = chrono_duration_milliseconds(chrono_elapsed(start));

    test_expect_true(test, "handle answers CLOSED again, synthetically (no 2s network wait)",
        second_result.status == WS_CLIENT_STATUS_CLOSED && elapsed_ms < 500);
    websocket_client_result_uninit(&second_result);

    U8 const dead_payload[1] = { 0 };
    WS_Client_Result send_result = websocket_client_send(client, dead_payload, sizeof(dead_payload), WS_CLIENT_BINARY);

    test_expect_true(test, "send on the dead handle answers CLOSED, synthetically", send_result.status == WS_CLIENT_STATUS_CLOSED);
    websocket_client_result_uninit(&send_result);

    bool frame_final = false;
    WS_Client_Result frame_result = websocket_client_recv_frame_2(client, &out, &frame_final, nullptr, 2000);

    test_expect_true(test, "recv_frame on the dead handle answers CLOSED, synthetically", frame_result.status == WS_CLIENT_STATUS_CLOSED);
    websocket_client_result_uninit(&frame_result);

    string_uninit(&out);
    websocket_client_delete(&client);
    fixture_server_join(&server);

    test_case_end(test);
}

static void _test_close_with_reason(Test *const test) {
    test_case_begin(test, "loopback: peer CLOSE(1000, reason) surfaces code + text");

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_CLOSE;
    server.close_code = 1000;
    server.close_reason = "server done";

    WS_Client *client = _connect_to_fixture(test, &server);

    if (client == nullptr) {
        test_case_end(test);

        return;
    }

    String out = string_init_1();
    WS_Client_Result recv_result = websocket_client_recv_2(client, &out, 2000);

    test_expect_true(test, "status CLOSED", recv_result.status == WS_CLIENT_STATUS_CLOSED);
    test_expect_i(test, "close_code == 1000", 1000, (ISize) recv_result.close_code);
    test_expect_string_contains(test, "error text carries the reason", string_get_data(&recv_result.error) != nullptr ? string_get_data(&recv_result.error) : "", "server done");

    websocket_client_result_uninit(&recv_result);
    fixture_server_join(&server);

    /* RFC 6455 5.5.1 requires the client to echo a CLOSE back; verify the server actually saw
       one on the wire, not just that recv reported CLOSED locally. */
    test_expect_true(test, "server observed the client's echoed CLOSE frame", server.saw_client_close);
    test_expect_i(test, "echoed close code == 1000 (peer sent none of its own to override)", 1000, (ISize) server.client_close_code);

    string_uninit(&out);
    websocket_client_delete(&client);

    test_case_end(test);
}

static void _test_wrong_subprotocol_rejects_connect(Test *const test) {
    test_case_begin(test, "loopback: server echoing the wrong subprotocol fails connect");

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_WRONG_SUBPROTOCOL;

    if (!test_expect_true(test, "fixture started", fixture_server_start(&server))) {
        test_case_end(test);

        return;
    }

    char url[64] = DEFAULT_INITIALIZATION;
    _url_for_port(fixture_server_port(&server), url, sizeof(url));

    WS_Client *client = websocket_client_new();
    WS_Client_Result connect_result = websocket_client_connect_2(client, url, "chat");

    test_expect_true(test, "connect_2 fails (status ERROR)", connect_result.status == WS_CLIENT_STATUS_ERROR);
    test_expect_true(test, "error text is non-empty", string_get_size(&connect_result.error) > 0);

    websocket_client_result_uninit(&connect_result);

    /* The mismatch was a COMPLETED handshake, not a curl-level failure, so `connected` latches
       the same as a successful connect: a second attempt on this handle must be refused rather
       than parking a second socket. */
    WS_Client_Result second_connect_result = websocket_client_connect_2(client, url, "chat");

    test_expect_true(test, "second connect_2 after a subprotocol mismatch is refused (status ERROR)", second_connect_result.status == WS_CLIENT_STATUS_ERROR);
    test_expect_i(test, "refusal reports CURLE_BAD_FUNCTION_ARGUMENT", (ISize) CURLE_BAD_FUNCTION_ARGUMENT, (ISize) second_connect_result.code);

    websocket_client_result_uninit(&second_connect_result);
    websocket_client_delete(&client);
    fixture_server_join(&server);

    test_case_end(test);
}

static void _test_client_close_reaches_server(Test *const test) {
    test_case_begin(test, "loopback: websocket_client_close reaches the peer as a real CLOSE frame");

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_CLIENT_CLOSE;

    WS_Client *client = _connect_to_fixture(test, &server);

    if (client == nullptr) {
        test_case_end(test);

        return;
    }

    WS_Client_Result close_result = websocket_client_close(client, 1001, "bye-bye");

    /* websocket_client_close's own contract: a successful send answers OK - the handle is
       marked dead BEHIND that OK, but CLOSED is reserved for the dead no-op and the
       send-deadline case, not for a close that actually went out. */
    test_expect_true(test, "close() send succeeded (status OK)", close_result.status == WS_CLIENT_STATUS_OK);
    test_expect_i(test, "close() echoes the sent code", 1001, (ISize) close_result.close_code);
    websocket_client_result_uninit(&close_result);

    fixture_server_join(&server);

    test_expect_true(test, "server actually saw a CLOSE frame", server.saw_client_close);
    test_expect_i(test, "server-observed close code == 1001", 1001, (ISize) server.client_close_code);

    String out = string_init_1();
    ChronoInstant const start = chrono_now();
    WS_Client_Result post_close_result = websocket_client_recv_2(client, &out, 2000);
    U64 const elapsed_ms = chrono_duration_milliseconds(chrono_elapsed(start));

    test_expect_true(test, "handle answers CLOSED after close(), synthetically",
        post_close_result.status == WS_CLIENT_STATUS_CLOSED && elapsed_ms < 500);

    websocket_client_result_uninit(&post_close_result);
    string_uninit(&out);
    websocket_client_delete(&client);

    test_case_end(test);
}

static void _test_4mib_send_integrity(Test *const test) {
    test_case_begin(test, "loopback: 4 MiB send integrity under receiver backpressure");

    USize const size = 4 * 1024 * 1024;
    U8 *const buffer = (U8*) malloc(size);

    if (!test_expect_not_null(test, "4 MiB source buffer allocated", buffer)) {
        test_case_end(test);

        return;
    }

    for (USize index = 0; index < size; index += 1) {
        buffer[index] = (U8) (index % 251);
    }

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_DRAIN_LARGE;
    server.payload = buffer;
    server.payload_size = size;
    server.pre_read_delay_ms = 800; // server reads nothing for 800ms - the intended backpressure window

    WS_Client *client = _connect_to_fixture(test, &server);

    if (client == nullptr) {
        free(buffer);
        test_case_end(test);

        return;
    }

    ChronoInstant const start = chrono_now();
    WS_Client_Result send_result = websocket_client_send(client, buffer, size, WS_CLIENT_BINARY);
    U64 const send_elapsed_ms = chrono_duration_milliseconds(chrono_elapsed(start));

    test_expect_true(test, "send of 4 MiB completed OK (loop-from-sent survived any CURLE_AGAIN)", send_result.status == WS_CLIENT_STATUS_OK);
    websocket_client_result_uninit(&send_result);

    fixture_server_join(&server);

    test_expect_u(test, "server received exactly 4 MiB", size, server.drain_received);
    test_expect_true(test, "drained bytes match the sent pattern exactly (no corruption)", server.drain_ok);

    /* Without this, a loopback tcp_wmem large enough to hold the whole payload lets curl_ws_send
       finish in one CURLE_OK pass and the test would pass without ever exercising the
       AGAIN/backpressure loop it is named for. Require the send to have actually waited out
       (most of) the server's read delay - proof the payload did NOT just sit fully buffered. */
    test_expect_true(test, "send actually blocked on receiver backpressure (not fully kernel-buffered)",
        send_elapsed_ms >= (U64) server.pre_read_delay_ms - 100);

    printf("  [measurement] websocket_client_send(4 MiB) wall time = %llu ms (receiver held off %u ms)\n",
        (unsigned long long) send_elapsed_ms, (unsigned) server.pre_read_delay_ms);

    websocket_client_delete(&client);
    free(buffer);

    test_case_end(test);
}

static void _test_recv_timeout_latency(Test *const test) {
    test_case_begin(test, "loopback: recv_2(100) TIMEOUT latency distribution");

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_IDLE_SILENT;
    server.pre_read_delay_ms = 0; // never sends

    WS_Client *client = _connect_to_fixture(test, &server);

    if (client == nullptr) {
        test_case_end(test);

        return;
    }

    USize const samples = 5;
    U64 total_ms = 0;
    U64 min_ms = U64_MAX;
    U64 max_ms = 0;

    for (USize index = 0; index < samples; index += 1) {
        String out = string_init_1();
        ChronoInstant const start = chrono_now();
        WS_Client_Result recv_result = websocket_client_recv_2(client, &out, 100);
        U64 const elapsed_ms = chrono_duration_milliseconds(chrono_elapsed(start));

        test_expect_true(test, "sample returns TIMEOUT", recv_result.status == WS_CLIENT_STATUS_TIMEOUT);

        total_ms += elapsed_ms;
        min_ms = elapsed_ms < min_ms ? elapsed_ms : min_ms;
        max_ms = elapsed_ms > max_ms ? elapsed_ms : max_ms;

        websocket_client_result_uninit(&recv_result);
        string_uninit(&out);
    }

    F64 const average_ms = (F64) total_ms / (F64) samples;

    test_expect_true(test, "average TIMEOUT latency tracks the 100ms budget, not a fixed poll grain", average_ms >= 90.0 && average_ms <= 1000.0);

    printf("  [measurement] recv_2(100) TIMEOUT latency over %zu samples: min=%llu ms max=%llu ms avg=%.1f ms\n",
        samples, (unsigned long long) min_ms, (unsigned long long) max_ms, average_ms);

    websocket_client_delete(&client);
    fixture_server_join(&server);

    test_case_end(test);
}

static void _test_idle_then_send_same_string_retry(Test *const test) {
    test_case_begin(test, "loopback: TIMEOUT then retry with the SAME String (idle then delayed send)");

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_IDLE_SILENT;
    server.pre_read_delay_ms = 500; // margin over the 100ms recv budget below, for a loaded CI runner

    WS_Client *client = _connect_to_fixture(test, &server);

    if (client == nullptr) {
        test_case_end(test);

        return;
    }

    String out = string_init_1();
    WS_Client_Result first_result = websocket_client_recv_2(client, &out, 100);

    test_expect_true(test, "first recv_2(100) times out before the delayed message arrives", first_result.status == WS_CLIENT_STATUS_TIMEOUT);
    websocket_client_result_uninit(&first_result);

    /* Buffer-reset idiom: do NOT clear `out` on TIMEOUT, retry with the same String. */
    WS_Client_Result second_result = websocket_client_recv_2(client, &out, 1000);

    char const *const expected = "delayed-hello";

    test_expect_true(test, "retry with same String receives the delayed message", second_result.status == WS_CLIENT_STATUS_OK);
    test_expect_true(test, "message content is intact (no duplication from the retry)", string_get_size(&out) == strlen(expected)
        && memcmp(string_get_data(&out), expected, strlen(expected)) == 0);

    websocket_client_result_uninit(&second_result);
    string_uninit(&out);
    websocket_client_delete(&client);
    fixture_server_join(&server);

    test_case_end(test);
}

static void _test_frame_tier_fragment_3(Test *const test) {
    test_case_begin(test, "loopback: recv_frame_2 out_final + out_type across a 3-fragment message");

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_FRAGMENT_3;

    WS_Client *client = _connect_to_fixture(test, &server);

    if (client == nullptr) {
        test_case_end(test);

        return;
    }

    char const *const expected = "Hello, fragmented world across three wire frames!";
    String out = string_init_1();

    bool final_1 = true;
    WS_Client_Type type_1 = WS_CLIENT_BINARY;
    WS_Client_Result frame_1 = websocket_client_recv_frame_2(client, &out, &final_1, &type_1, 2000);

    test_expect_true(test, "frame 1 OK", frame_1.status == WS_CLIENT_STATUS_OK);
    test_expect_true(test, "frame 1 out_type == TEXT", type_1 == WS_CLIENT_TEXT);
    test_expect_true(test, "frame 1 out_final == false", !final_1);
    websocket_client_result_uninit(&frame_1);

    bool final_2 = true;
    WS_Client_Result frame_2 = websocket_client_recv_frame_2(client, &out, &final_2, nullptr, 2000);

    test_expect_true(test, "frame 2 OK", frame_2.status == WS_CLIENT_STATUS_OK);
    test_expect_true(test, "frame 2 out_final == false", !final_2);
    websocket_client_result_uninit(&frame_2);

    bool final_3 = false;
    WS_Client_Result frame_3 = websocket_client_recv_frame_2(client, &out, &final_3, nullptr, 2000);

    test_expect_true(test, "frame 3 OK", frame_3.status == WS_CLIENT_STATUS_OK);
    test_expect_true(test, "frame 3 out_final == true", final_3);
    websocket_client_result_uninit(&frame_3);

    test_expect_true(test, "reassembled across the 3 recv_frame_2 calls equals the original", string_get_size(&out) == strlen(expected)
        && memcmp(string_get_data(&out), expected, strlen(expected)) == 0);

    string_uninit(&out);
    websocket_client_delete(&client);
    fixture_server_join(&server);

    test_case_end(test);
}

static void _test_recv_zero_buffered_and_idle(Test *const test) {
    test_case_begin(test, "loopback: recv_2(0) drains buffered data (OK) vs an idle peer (TIMEOUT < 50ms)");

    Fixture_Server buffered_server = DEFAULT_INITIALIZATION;
    buffered_server.script = FIXTURE_SCRIPT_IDLE_SILENT;
    buffered_server.pre_read_delay_ms = 30;

    if (!test_expect_true(test, "buffered fixture started", fixture_server_start(&buffered_server))) {
        test_case_end(test);

        return;
    }

    char buffered_url[64] = DEFAULT_INITIALIZATION;
    _url_for_port(fixture_server_port(&buffered_server), buffered_url, sizeof(buffered_url));

    WS_Client *buffered_client = websocket_client_new();
    WS_Client_Result buffered_connect = websocket_client_connect_1(buffered_client, buffered_url);

    test_expect_true(test, "buffered: connect OK", buffered_connect.status == WS_CLIENT_STATUS_OK);
    websocket_client_result_uninit(&buffered_connect);

    // Give the message time to actually arrive and sit in this side's socket buffer before
    // recv_2(0) ever asks - "buffered" must mean "already here", not "arrives during the call".
    thread_sleep(300);

    String buffered_out = string_init_1();
    WS_Client_Result buffered_result = websocket_client_recv_2(buffered_client, &buffered_out, 0);

    test_expect_true(test, "buffered: recv_2(0) drains it -> OK", buffered_result.status == WS_CLIENT_STATUS_OK);

    websocket_client_result_uninit(&buffered_result);
    string_uninit(&buffered_out);
    websocket_client_delete(&buffered_client);
    fixture_server_join(&buffered_server);

    Fixture_Server idle_server = DEFAULT_INITIALIZATION;
    idle_server.script = FIXTURE_SCRIPT_IDLE_SILENT;
    idle_server.pre_read_delay_ms = 0; // never sends

    if (!test_expect_true(test, "idle fixture started", fixture_server_start(&idle_server))) {
        test_case_end(test);

        return;
    }

    char idle_url[64] = DEFAULT_INITIALIZATION;
    _url_for_port(fixture_server_port(&idle_server), idle_url, sizeof(idle_url));

    WS_Client *idle_client = websocket_client_new();
    WS_Client_Result idle_connect = websocket_client_connect_1(idle_client, idle_url);

    test_expect_true(test, "idle: connect OK", idle_connect.status == WS_CLIENT_STATUS_OK);
    websocket_client_result_uninit(&idle_connect);

    String idle_out = string_init_1();
    ChronoInstant const idle_start = chrono_now();
    WS_Client_Result idle_result = websocket_client_recv_2(idle_client, &idle_out, 0);
    U64 const idle_elapsed_ms = chrono_duration_milliseconds(chrono_elapsed(idle_start));

    test_expect_true(test, "idle: recv_2(0) -> TIMEOUT", idle_result.status == WS_CLIENT_STATUS_TIMEOUT);
    test_expect_true(test, "idle: recv_2(0) answers in under 50ms (one non-blocking poll, no wait)", idle_elapsed_ms < 50);

    printf("  [measurement] recv_2(0) idle TIMEOUT after %llu ms\n", (unsigned long long) idle_elapsed_ms);

    websocket_client_result_uninit(&idle_result);
    string_uninit(&idle_out);
    websocket_client_delete(&idle_client);
    fixture_server_join(&idle_server);

    test_case_end(test);
}

static void _test_connect2_accepted_subprotocol(Test *const test) {
    test_case_begin(test, "loopback: connect_2 succeeds when the server echoes the requested subprotocol");

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_ECHO;

    if (!test_expect_true(test, "fixture started", fixture_server_start(&server))) {
        test_case_end(test);

        return;
    }

    char url[64] = DEFAULT_INITIALIZATION;
    _url_for_port(fixture_server_port(&server), url, sizeof(url));

    WS_Client *client = websocket_client_new();
    WS_Client_Result connect_result = websocket_client_connect_2(client, url, "chat");

    test_expect_true(test, "connect_2 with an accepted subprotocol -> OK", connect_result.status == WS_CLIENT_STATUS_OK);
    websocket_client_result_uninit(&connect_result);

    char const *const message = "subprotocol-accepted";
    WS_Client_Result send_result = websocket_client_send(client, message, strlen(message), WS_CLIENT_TEXT);

    test_expect_true(test, "send after accepted connect_2 OK", send_result.status == WS_CLIENT_STATUS_OK);
    websocket_client_result_uninit(&send_result);

    String out = string_init_1();
    WS_Client_Result recv_result = websocket_client_recv_2(client, &out, 2000);

    test_expect_true(test, "echo round trip OK after connect_2", recv_result.status == WS_CLIENT_STATUS_OK);
    test_expect_true(test, "echoed text matches", string_get_size(&out) == strlen(message)
        && memcmp(string_get_data(&out), message, strlen(message)) == 0);

    websocket_client_result_uninit(&recv_result);
    string_uninit(&out);
    websocket_client_delete(&client);
    fixture_server_join(&server);

    test_case_end(test);
}

static void _test_reconnect_refused(Test *const test) {
    test_case_begin(test, "loopback: a second connect_1 on a handle that already holds a connection is refused; the first connection keeps working");

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_ECHO;

    WS_Client *client = _connect_to_fixture(test, &server);

    if (client == nullptr) {
        test_case_end(test);

        return;
    }

    char url[64] = DEFAULT_INITIALIZATION;
    _url_for_port(fixture_server_port(&server), url, sizeof(url));

    ChronoInstant const reconnect_start = chrono_now();
    WS_Client_Result reconnect_result = websocket_client_connect_1(client, url);
    U64 const reconnect_elapsed_ms = chrono_duration_milliseconds(chrono_elapsed(reconnect_start));

    test_expect_true(test, "second connect_1 on the same handle is refused (status ERROR)", reconnect_result.status == WS_CLIENT_STATUS_ERROR);
    test_expect_i(test, "refusal reports CURLE_BAD_FUNCTION_ARGUMENT", (ISize) CURLE_BAD_FUNCTION_ARGUMENT, (ISize) reconnect_result.code);
    test_expect_true(test, "refusal is a local state check, not a network attempt (answers well under a handshake round trip)", reconnect_elapsed_ms < 200);
    websocket_client_result_uninit(&reconnect_result);

    char const *const message = "first-connection-still-works-after-the-refused-reconnect";
    WS_Client_Result send_result = websocket_client_send(client, message, strlen(message), WS_CLIENT_TEXT);

    test_expect_true(test, "send on the first connection still succeeds after the refused reconnect attempt", send_result.status == WS_CLIENT_STATUS_OK);
    websocket_client_result_uninit(&send_result);

    String out = string_init_1();
    WS_Client_Result recv_result = websocket_client_recv_2(client, &out, 2000);

    test_expect_true(test, "recv on the first connection still succeeds (echo)", recv_result.status == WS_CLIENT_STATUS_OK);
    test_expect_true(test, "the first connection is unaffected by the refused reconnect attempt", string_get_size(&out) == strlen(message)
        && memcmp(string_get_data(&out), message, strlen(message)) == 0);

    websocket_client_result_uninit(&recv_result);
    string_uninit(&out);

    /* websocket_client_delete only drops the TCP connection - it never sends CURLWS_CLOSE (only
       websocket_client_close does, pinned separately by _test_client_close_reaches_server), so
       there is no close FRAME here for the fixture to observe, only a TCP teardown; nothing is
       asserted on that for this reason. */
    websocket_client_delete(&client);
    fixture_server_join(&server);

    test_case_end(test);
}

static void _test_timeout_mid_message_prefix(Test *const test) {
    test_case_begin(test, "loopback: TIMEOUT with a mid-message prefix, then resume with the SAME String");

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_FRAGMENT_DELAY;
    server.pre_read_delay_ms = 500; // margin over the 100ms recv budget below, for a loaded CI runner

    WS_Client *client = _connect_to_fixture(test, &server);

    if (client == nullptr) {
        test_case_end(test);

        return;
    }

    char const *const first_half = "before-the-pause-";
    String out = string_init_1();
    WS_Client_Result first_result = websocket_client_recv_2(client, &out, 100);

    test_expect_true(test, "first recv_2(100) times out mid-message (fragment 2 held back)",
        first_result.status == WS_CLIENT_STATUS_TIMEOUT);
    test_expect_true(test, "fragment 1's bytes already landed in `out`", string_get_size(&out) == strlen(first_half)
        && memcmp(string_get_data(&out), first_half, strlen(first_half)) == 0);
    websocket_client_result_uninit(&first_result);

    /* Buffer-reset idiom: do NOT clear `out` on TIMEOUT, retry with the same String. */
    WS_Client_Result second_result = websocket_client_recv_2(client, &out, 1000);

    char const *const expected = "before-the-pause-after-the-pause-completes-the-message";

    test_expect_true(test, "retry with the same String completes the message", second_result.status == WS_CLIENT_STATUS_OK);
    test_expect_true(test, "no duplication - the prefix appears exactly once", string_get_size(&out) == strlen(expected)
        && memcmp(string_get_data(&out), expected, strlen(expected)) == 0);

    websocket_client_result_uninit(&second_result);
    string_uninit(&out);
    websocket_client_delete(&client);
    fixture_server_join(&server);

    test_case_end(test);
}

static void _test_receive_above_64kib(Test *const test) {
    test_case_begin(test, "loopback: a single message over 64 KiB completes via libcurl's internal CURLWS_OFFSET chunk loop");

    USize const size = 200000; // > the 64 KiB internal read buffer, well under the 16 MiB default cap
    U8 *const payload = (U8*) malloc(size);

    if (!test_expect_not_null(test, "payload buffer allocated", payload)) {
        test_case_end(test);

        return;
    }

    for (USize index = 0; index < size; index += 1) {
        payload[index] = (U8) (index % 253);
    }

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_OVERSIZED;
    server.payload = payload;
    server.payload_size = size;

    // _connect_to_fixture's handle keeps the default 16 MiB cap - not lowered here.
    WS_Client *client = _connect_to_fixture(test, &server);

    if (client == nullptr) {
        free(payload);
        test_case_end(test);

        return;
    }

    String out = string_init_1();
    WS_Client_Result recv_result = websocket_client_recv_2(client, &out, 5000);

    test_expect_true(test, "recv OK across the internal chunk loop", recv_result.status == WS_CLIENT_STATUS_OK);
    test_expect_true(test, "all 200000 bytes arrived intact", string_get_size(&out) == size
        && memcmp(string_get_data(&out), payload, size) == 0);

    websocket_client_result_uninit(&recv_result);
    string_uninit(&out);
    websocket_client_delete(&client);
    fixture_server_join(&server);
    free(payload);

    test_case_end(test);
}

/* ── entry point ──────────────────────────────────────────────────────────── */

int main(void) {
    LogConfig const log_config = {
        .level             = LOG_LEVEL_ERROR,
        .stream            = LOG_STREAM_STDOUT,
        .timestamp_enabled = true,
        .autoflush         = true,
    };

    log_init(log_config);
    curl_global_init(CURL_GLOBAL_DEFAULT);

    Test test = test_init("./test_loopback.c");

    test_suite_begin(&test, "websocket/client loopback fixture");

    _test_echo_text(&test);
    _test_echo_binary(&test);
    _test_fragment_reassembly(&test);
    _test_ping_mid_message(&test);
    _test_ping_flood_timeout(&test);
    _test_oversized_marks_dead(&test);
    _test_close_with_reason(&test);
    _test_wrong_subprotocol_rejects_connect(&test);
    _test_client_close_reaches_server(&test);
    _test_4mib_send_integrity(&test);
    _test_recv_timeout_latency(&test);
    _test_idle_then_send_same_string_retry(&test);
    _test_frame_tier_fragment_3(&test);
    _test_recv_zero_buffered_and_idle(&test);
    _test_connect2_accepted_subprotocol(&test);
    _test_reconnect_refused(&test);
    _test_timeout_mid_message_prefix(&test);
    _test_receive_above_64kib(&test);

    test_suite_end(&test);

    I32 const failures = test_uninit(&test);

    curl_global_cleanup();

    return failures;
}