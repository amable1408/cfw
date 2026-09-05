/*
 * test_all.c - Offline unit tests for the CFW WebSocket client module.
 *
 * What is tested (all executable without a live server):
 *   1. result_init / result_uninit lifecycle – no leak, no double-free.
 *   2. Multiple init/uninit cycles – stable repeat behaviour.
 *   3. websocket_client_new / websocket_client_delete round-trip.
 *   4. Setter calls (timeout, TLS) on a fresh handle – must not crash.
 *   5. websocket_client_connect against an unreachable endpoint –
 *      status != OK, non-empty error string, code != 0.
 *   6. Second connect on same handle after a failed first (handle reuse).
 *   7. recv_2 timeout=0 on a never-connected handle – returns at once, no hang.
 *   8. recv_2 with USIZE_MAX-1 (I32_MAX/LONG_MAX clamp path) – no hang.
 *   9. websocket_client_close on a never-connected handle – no crash.
 *
 * What is NOT tested here (needs a live peer – deferred to the loopback
 * fixture pass):
 *   - Null-argument guards: error_check_null in this framework calls abort()
 *     on null, so null-path testing cannot be done in-process without a
 *     subprocess harness. That is a deliberate design choice (fail-fast).
 *   - Full send/recv round-trip, PING/PONG handling, CLOSE-frame drain.
 *   - websocket_client_recv / websocket_client_recv_frame with real data.
 *   - close() marking the handle dead after a SUCCESSFUL send (curl_ws_send
 *     never returns CURLE_OK without an established connection).
 *   - The PING-flood-under-CURLE_OK deadline fix and the CURL_SOCKET_BAD
 *     mid-CURLE_AGAIN → CLOSED mapping, both of which need a real socket.
 */
#include <stdio.h>

#include <websocket/client/websocket_client.h>
#include <log/log.h>
#include <test/test.h>

/* ── helpers ──────────────────────────────────────────────────────────────── */

/*
 * websocket_client_new() does NOT call curl_global_init by design (unsafe
 * from worker threads – see websocket_client.c comment). The test binary owns
 * the one-time process-global call.
 */
static void _curl_global_setup(void) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

static void _curl_global_teardown(void) {
    curl_global_cleanup();
}

/* ── test cases ───────────────────────────────────────────────────────────── */

static void _test_result_lifecycle(Test *const test) {
    test_case_begin(test, "result_init / result_uninit lifecycle");

    WS_Client_Result r = websocket_client_result_init();

    test_expect_false(test, "fresh result: status != OK", (r.status == WS_CLIENT_STATUS_OK));
    test_expect_i(test, "fresh result: code=0", 0, (ISize) r.code);
    test_expect_true(test, "fresh result: status=ERROR (never reads as success)", r.status == WS_CLIENT_STATUS_ERROR);

    /* uninit must not crash and must leave struct in a safe state */
    websocket_client_result_uninit(&r);

    test_expect_false(test, "after uninit: status != OK", (r.status == WS_CLIENT_STATUS_OK));
    test_expect_i(test, "after uninit: code=0", 0, (ISize) r.code);

    /* second uninit on the already-uninitialised result – string_uninit on an
       empty/null String must be idempotent */
    websocket_client_result_uninit(&r);

    test_expect_false(test, "after double uninit: status != OK", (r.status == WS_CLIENT_STATUS_OK));
    test_expect_i(test, "after double uninit: code=0", 0, (ISize) r.code);

    test_case_end(test);
}

static void _test_result_repeat_cycles(Test *const test) {
    test_case_begin(test, "result init/uninit repeated cycles");

    for (USize i = 0; i < 16; i += 1) {
        WS_Client_Result r = websocket_client_result_init();

        test_expect_false(test, "cycle: status != OK", (r.status == WS_CLIENT_STATUS_OK));
        test_expect_i(test, "cycle: code=0", 0, (ISize) r.code);

        websocket_client_result_uninit(&r);
    }

    test_case_end(test);
}

static void _test_new_delete(Test *const test) {
    test_case_begin(test, "websocket_client_new / delete round-trip");

    WS_Client *ws = websocket_client_new();

    test_expect_not_null(test, "new returns non-null handle", (void*) ws);

    websocket_client_delete(&ws);

    test_expect_null(test, "delete nulls the pointer (MEMORY_NON_DANGLING_POINTER)", (void*) ws);

    test_case_end(test);
}

static void _test_multiple_new_delete(Test *const test) {
    test_case_begin(test, "multiple new/delete pairs");

    for (USize i = 0; i < 4; i += 1) {
        WS_Client *ws = websocket_client_new();

        test_expect_not_null(test, "handle non-null", (void*) ws);

        websocket_client_delete(&ws);

        test_expect_null(test, "handle nulled after delete", (void*) ws);
    }

    test_case_end(test);
}

static void _test_setters(Test *const test) {
    test_case_begin(test, "setter calls on fresh handle – no crash");

    // websocket_client_new never returns null (it aborts on allocation failure, header
    // contract), so there is nothing to guard here - proceed straight to the setter chain.
    WS_Client *ws = websocket_client_new();

    websocket_client_set_connect_timeout_ms(ws, 500);
    websocket_client_set_timeout_ms(ws, 2000);
    websocket_client_set_verify_tls(ws, false);
    websocket_client_set_verify_tls(ws, true);
    websocket_client_set_connect_timeout_ms(ws, 100);
    websocket_client_set_ca_bundle(ws, "./no-such-ca-bundle.pem");
    websocket_client_set_max_message_size(ws, 4096);

    test_expect_not_null(test, "handle still valid after setter chain (no crash)", (void*) ws);

    websocket_client_delete(&ws);

    test_case_end(test);
}

static void _test_connect_unreachable(Test *const test) {
    test_case_begin(test, "connect to unreachable URL – fails gracefully");

    // websocket_client_new never returns null (header contract) - no guard needed.
    WS_Client *ws = websocket_client_new();

    /* Very short timeout so the test finishes quickly.
       192.0.2.x is TEST-NET (RFC 5737) – guaranteed unreachable. */
    websocket_client_set_connect_timeout_ms(ws, 200);
    websocket_client_set_timeout_ms(ws, 200);
    websocket_client_set_verify_tls(ws, false);

    WS_Client_Result r = websocket_client_connect_1(ws, "ws://192.0.2.1:9999/no-such-host");

    test_expect_false(test, "status != OK on unreachable", (r.status == WS_CLIENT_STATUS_OK));
    test_expect_true(test, "code != 0 on failure", r.code != 0);
    test_expect_true(test, "status is not OK on unreachable", r.status != WS_CLIENT_STATUS_OK);
    test_expect_true(test, "unreachable maps to TIMEOUT or ERROR, never CLOSED", r.status == WS_CLIENT_STATUS_TIMEOUT || r.status == WS_CLIENT_STATUS_ERROR);
    test_expect_true(test, "error string non-empty", r.error.size > 0);

    websocket_client_result_uninit(&r);
    websocket_client_delete(&ws);

    test_case_end(test);
}

static void _test_connect_bad_url(Test *const test) {
    test_case_begin(test, "connect to syntactically bad URL – fails gracefully");

    // websocket_client_new never returns null (header contract) - no guard needed.
    WS_Client *ws = websocket_client_new();

    websocket_client_set_connect_timeout_ms(ws, 200);
    websocket_client_set_timeout_ms(ws, 200);

    WS_Client_Result r = websocket_client_connect_1(ws, "ws://[invalid-host]:not_a_port/");

    test_expect_false(test, "status != OK on bad URL", (r.status == WS_CLIENT_STATUS_OK));
    test_expect_true(test, "code != 0 on bad URL", r.code != 0);
    test_expect_true(test, "status=ERROR on bad URL", r.status == WS_CLIENT_STATUS_ERROR);
    test_expect_true(test, "error string non-empty on bad URL", r.error.size > 0);

    websocket_client_result_uninit(&r);
    websocket_client_delete(&ws);

    test_case_end(test);
}

static void _test_handle_reuse_after_failure(Test *const test) {
    test_case_begin(test, "handle reuse – second connect after failed first");

    // websocket_client_new never returns null (header contract) - no guard needed.
    WS_Client *ws = websocket_client_new();

    websocket_client_set_connect_timeout_ms(ws, 200);
    websocket_client_set_timeout_ms(ws, 200);
    websocket_client_set_verify_tls(ws, false);

    /* First attempt – will fail */
    WS_Client_Result r1 = websocket_client_connect_1(ws, "ws://192.0.2.1:9999/");

    test_expect_false(test, "first connect failed as expected", (r1.status == WS_CLIENT_STATUS_OK));

    websocket_client_result_uninit(&r1);

    /* Second attempt on the same handle – must also fail gracefully (not crash) */
    WS_Client_Result r2 = websocket_client_connect_1(ws, "ws://192.0.2.2:9999/");

    test_expect_false(test, "second connect also fails gracefully", (r2.status == WS_CLIENT_STATUS_OK));
    test_expect_true(test, "second error string non-empty", r2.error.size > 0);

    websocket_client_result_uninit(&r2);
    websocket_client_delete(&ws);

    test_case_end(test);
}

static void _test_recv_timeout_zero_unconnected(Test *const test) {
    test_case_begin(test, "recv_2 timeout=0 on a never-connected handle returns at once");

    // websocket_client_new never returns null (header contract) - no guard needed.
    WS_Client *ws = websocket_client_new();

    String out = string_init_1();
    ChronoInstant const start = chrono_now();
    WS_Client_Result r = websocket_client_recv_2(ws, &out, 0);
    U64 const elapsed_ms = chrono_duration_milliseconds(chrono_elapsed(start));

    test_expect_true(test, "returned well under a second (no hang)", elapsed_ms < 1000);
    test_expect_true(test, "status is not OK on an unconnected handle", r.status != WS_CLIENT_STATUS_OK);

    websocket_client_result_uninit(&r);
    string_uninit(&out);
    websocket_client_delete(&ws);

    test_case_end(test);
}

static void _test_recv_huge_timeout_unconnected_no_hang(Test *const test) {
    test_case_begin(test, "recv_2 with USIZE_MAX-1 on a never-connected handle does not block");

    // websocket_client_new never returns null (header contract) - no guard needed.
    WS_Client *ws = websocket_client_new();

    String out = string_init_1();
    ChronoInstant const start = chrono_now();
    WS_Client_Result r = websocket_client_recv_2(ws, &out, USIZE_MAX - 1);
    U64 const elapsed_ms = chrono_duration_milliseconds(chrono_elapsed(start));

    test_expect_true(test, "returned well under a second (I32_MAX/LONG_MAX clamp doesn't hang)", elapsed_ms < 1000);
    test_expect_true(test, "status is not OK", r.status != WS_CLIENT_STATUS_OK);

    websocket_client_result_uninit(&r);
    string_uninit(&out);
    websocket_client_delete(&ws);

    test_case_end(test);
}

static void _test_close_never_connected(Test *const test) {
    test_case_begin(test, "close on a never-connected handle – fails gracefully, no crash");

    // websocket_client_new never returns null (header contract) - no guard needed.
    WS_Client *ws = websocket_client_new();

    WS_Client_Result r = websocket_client_close(ws, 1000, "bye");

    test_expect_true(test, "close on a never-connected handle never reports OK (no frame could go out)", r.status != WS_CLIENT_STATUS_OK);

    websocket_client_result_uninit(&r);
    websocket_client_delete(&ws);

    test_case_end(test);
}

/* NOTE: "close marks the handle dead after a SUCCESSFUL send" needs curl_ws_send to
   actually return CURLE_OK, which requires a live peer with an established connection -
   deferred to the loopback fixture pass. */

/* ── entry point ──────────────────────────────────────────────────────────── */

int main(void) {
    LogConfig const log_config = {
        .level             = LOG_LEVEL_ERROR,
        .stream            = LOG_STREAM_STDOUT,
        .timestamp_enabled = true,
        .autoflush         = true,
    };

    log_init(log_config);
    _curl_global_setup();

    Test test = test_init("./test_all.c");

    test_suite_begin(&test, "websocket_client");

    _test_result_lifecycle(&test);
    _test_result_repeat_cycles(&test);
    _test_new_delete(&test);
    _test_multiple_new_delete(&test);
    _test_setters(&test);
    _test_connect_unreachable(&test);
    _test_connect_bad_url(&test);
    _test_handle_reuse_after_failure(&test);
    _test_recv_timeout_zero_unconnected(&test);
    _test_recv_huge_timeout_unconnected_no_hang(&test);
    _test_close_never_connected(&test);

    test_suite_end(&test);

    _curl_global_teardown();

    return test_uninit(&test);
}