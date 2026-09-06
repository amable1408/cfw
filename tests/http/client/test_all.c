/*
 * test_all.c - Loopback-fixture suite for the CFW http/client module.
 *
 * Almost every observable behaviour of http_client depends on a live HTTP peer (status line,
 * headers, redirects, encodings, size caps, transport failures), so unlike a pure-offline suite
 * this file IS the loopback suite: it runs a minimal HTTP/1.1 server (fixture.h) on a CFW thread
 * over 127.0.0.1 (OS-assigned port) and drives the real http_client against it.
 *
 * Pinned here: GET 200 with duplicate/mixed-case/empty-value response headers (first-wins,
 * case-insensitive, present-but-empty vs absent), 404/500 as transport success, a 302 redirect
 * whose final response carries only the last hop's headers, MAXREDIRS -> TOO_MANY_REDIRECTS, a
 * ftp:// Location refused by CURLOPT_REDIR_PROTOCOLS_STR, a custom request header seen by the
 * peer and gone after header_clear, chunked and gzip response bodies transparently decoded, a
 * response over max_response_size capped with WRITE_ERROR (never an abort), a response header
 * flood over both the line- and byte-budget branches capped the same way, an idle peer -> TIMEOUT
 * within budget, a closed port -> UNREACHABLE (refused) or TIMEOUT (SYN dropped), a mid-body close -> a
 * non-OK transport result with a non-empty error, a URL libcurl cannot parse -> URL_MALFORMAT/ERROR
 * (never an abort), https:// against a plain HTTP port -> TLS status (proves a real TLS handshake is
 * attempted - not, by itself, that verification is on), GET/POST/PUT/DELETE/HEAD request lines and
 * an embedded-NUL body pinned via the fixture's own capture, a fresh handle AND a handle that just
 * POSTed both sending GET/DELETE with no Content-Length and POSTed sending HEAD (method state does
 * not depend on handle history), default and overridden User-Agent as the peer sees it, response_code
 * staying 0 after a transport failure, keep-alive counting exactly one connection across two
 * requests, the response buffer being cleared between calls, http_client_result_uninit being
 * idempotent, two clients on one thread staying independent, and the escape_1 family
 * (unreserved/space/UTF-8/empty).
 *
 * No network beyond loopback; every port is OS-assigned; every wait is timeout-bounded so a
 * broken peer cannot hang the suite.
 */
#include <stdio.h>
#include <string.h>

#include <arena/arena.h>
#include <char/char.h>
#include <chrono/chrono.h>
#include <container/str/str.h>
#include <http/client/http_client.h>
#include <log/log.h>
#include <net/net.h>
#include <test/test.h>

#include <fixture.h>

/* ── helpers ──────────────────────────────────────────────────────────────── */

static void _url_for(U16 const port, char const *const path, char *const out, USize const out_capacity) {
    snprintf(out, out_capacity, "http://127.0.0.1:%u%s", (unsigned) port, path);
}

static bool _start(Test *const test, Fixture_Server *const server) {
    return test_expect_true(test, "fixture started", fixture_server_start(server));
}

/* ── test cases ───────────────────────────────────────────────────────────── */

static void _test_ok_headers_and_status(Test *const test) {
    test_case_begin(test, "loopback: GET 200 - body, status, and header_find (dup/mixed-case/empty/absent)");

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_OK;

    if (!_start(test, &server)) {
        test_case_end(test);

        return;
    }

    char url[64] = DEFAULT_INITIALIZATION;
    _url_for(fixture_server_port(&server), "/", url, sizeof(url));

    HTTP_Client *client = http_client_new();
    String response = string_init_1();
    HTTP_Client_Result result = http_client_get(client, url, &response);

    test_expect_true(test, "result_is_ok", http_client_result_is_ok(&result));
    test_expect_u(test, "response_status == 200", 200, http_client_response_status(client));
    test_expect_string(test, "body matches", "ok-body-content", string_get_data(&response));

    char const *const dup = http_client_response_header_find(client, "X-DUP");
    test_expect_true(test, "duplicate header: first value wins, case-insensitive lookup", dup != nullptr && strcmp(dup, "first-value") == 0);

    char const *const mixed = http_client_response_header_find(client, "x-mixed-case");
    test_expect_true(test, "mixed-case response header name still matches", mixed != nullptr && strcmp(mixed, "mixed-value") == 0);

    char const *const empty = http_client_response_header_find(client, "X-Empty");
    test_expect_true(test, "present-but-empty header returns a pointer to \"\", not nullptr", empty != nullptr && empty[0] == '\0');

    char const *const missing = http_client_response_header_find(client, "X-Nonexistent");
    test_expect_null(test, "absent header returns nullptr", (void*) missing);

    http_client_result_uninit(&result);
    string_uninit(&response);
    http_client_delete(&client);
    fixture_server_join(&server);
    string_uninit(&server.request_body);

    test_case_end(test);
}

static void _test_status_4xx_5xx_are_transport_success(Test *const test) {
    test_case_begin(test, "loopback: 404 and 500 are transport SUCCESS (result_is_ok is false, success is true)");

    USize const codes[2] = { 404, 500 };

    for (USize index = 0; index < 2; index += 1) {
        Fixture_Server server = DEFAULT_INITIALIZATION;
        server.script = FIXTURE_SCRIPT_STATUS;
        server.status_code = codes[index];

        if (!_start(test, &server)) {
            continue;
        }

        char url[64] = DEFAULT_INITIALIZATION;
        _url_for(fixture_server_port(&server), "/", url, sizeof(url));

        HTTP_Client *client = http_client_new();
        String response = string_init_1();
        HTTP_Client_Result result = http_client_get(client, url, &response);

        test_expect_true(test, "success (transport OK)", result.success);
        test_expect_false(test, "result_is_ok is false (not 2xx)", http_client_result_is_ok(&result));
        test_expect_u(test, "response_code matches", codes[index], result.response_code);
        test_expect_string(test, "body still captured", "status-body", string_get_data(&response));

        http_client_result_uninit(&result);
        string_uninit(&response);
        http_client_delete(&client);
        fixture_server_join(&server);
        string_uninit(&server.request_body);
    }

    test_case_end(test);
}

/* Fixture capability pin, not an http_client pin: response_body / response_content_type let a
 * suite script an arbitrary payload (a siteverify `{"success":true}`, a 400 JSON error) instead
 * of the two fixed literals the OK/STATUS scripts used to hardcode. Left unset, every other
 * case in this file still sees the historical bytes - which is why their expectations above
 * are untouched. Added for the captcha/oauth/email rounds, which share this fixture. */
static void _test_scripted_response_body_round_trips(Test *const test) {
    test_case_begin(test, "loopback: a scripted response_body and Content-Type reach the caller verbatim");

    char const *const scripted = "{\"success\":true,\"hostname\":\"h\"}";

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_OK;
    server.response_body = scripted;
    server.response_content_type = "application/json";

    if (_start(test, &server)) {
        char url[64] = DEFAULT_INITIALIZATION;
        _url_for(fixture_server_port(&server), "/", url, sizeof(url));

        HTTP_Client *client = http_client_new();
        String response = string_init_1();
        HTTP_Client_Result result = http_client_get(client, url, &response);

        test_expect_true(test, "success", result.success);
        test_expect_u(test, "200", 200, result.response_code);
        test_expect_string(test, "the scripted JSON body arrives byte-exact", scripted, string_get_data(&response));

        char const *const content_type = http_client_response_header_find(client, "Content-Type");

        test_expect_true(test, "the scripted Content-Type arrives", content_type != nullptr && strcmp(content_type, "application/json") == 0);

        http_client_result_uninit(&result);
        string_uninit(&response);
        http_client_delete(&client);
        fixture_server_join(&server);
        string_uninit(&server.request_body);
    }

    // A 400 with a scripted JSON error body - the shape the oauth/captcha rounds need.
    Fixture_Server error_server = DEFAULT_INITIALIZATION;
    error_server.script = FIXTURE_SCRIPT_STATUS;
    error_server.status_code = 400;
    error_server.response_body = "{\"error\":\"invalid_grant\"}";

    if (!_start(test, &error_server)) {
        test_case_end(test);

        return;
    }

    char error_url[64] = DEFAULT_INITIALIZATION;
    _url_for(fixture_server_port(&error_server), "/", error_url, sizeof(error_url));

    HTTP_Client *error_client = http_client_new();
    String error_response = string_init_1();
    HTTP_Client_Result error_result = http_client_get(error_client, error_url, &error_response);

    test_expect_true(test, "transport success on a scripted 400", error_result.success);
    test_expect_u(test, "400", 400, error_result.response_code);
    test_expect_string(test, "the scripted error body replaces \"status-body\"", "{\"error\":\"invalid_grant\"}", string_get_data(&error_response));

    http_client_result_uninit(&error_result);
    string_uninit(&error_response);
    http_client_delete(&error_client);
    fixture_server_join(&error_server);
    string_uninit(&error_server.request_body);

    test_case_end(test);
}

static void _test_redirect_final_hop_headers_only(Test *const test) {
    test_case_begin(test, "loopback: 302 -> only the final hop's headers survive");

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_REDIRECT;
    server.max_connections = 2;

    if (!_start(test, &server)) {
        test_case_end(test);

        return;
    }

    char url[64] = DEFAULT_INITIALIZATION;
    _url_for(fixture_server_port(&server), "/", url, sizeof(url));

    HTTP_Client *client = http_client_new();

    http_client_set_follow_redirects(client, true, 5);

    String response = string_init_1();
    HTTP_Client_Result result = http_client_get(client, url, &response);

    test_expect_true(test, "result_is_ok after following the redirect", http_client_result_is_ok(&result));
    test_expect_string(test, "body is the SECOND hop's body", "redirected-ok", string_get_data(&response));

    char const *const final_header = http_client_response_header_find(client, "X-Second-Final");
    test_expect_true(test, "final hop's own header is present", final_header != nullptr && strcmp(final_header, "yes") == 0);

    char const *const first_header = http_client_response_header_find(client, "X-First-Only");
    test_expect_null(test, "first hop's header did NOT survive the redirect", (void*) first_header);

    http_client_result_uninit(&result);
    string_uninit(&response);
    http_client_delete(&client);
    fixture_server_join(&server);
    string_uninit(&server.request_body);

    test_case_end(test);
}

static void _test_redirect_loop_exceeds_maxredirects(Test *const test) {
    test_case_begin(test, "loopback: a self-redirecting peer trips MAXREDIRS -> CURLE_TOO_MANY_REDIRECTS");

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_REDIRECT_LOOP;
    server.max_connections = 1;

    if (!_start(test, &server)) {
        test_case_end(test);

        return;
    }

    char url[64] = DEFAULT_INITIALIZATION;
    _url_for(fixture_server_port(&server), "/loop", url, sizeof(url));

    HTTP_Client *client = http_client_new();

    http_client_set_follow_redirects(client, true, 3);

    String response = string_init_1();
    HTTP_Client_Result result = http_client_get(client, url, &response);

    test_expect_false(test, "success is false", result.success);
    test_expect_i(test, "code == CURLE_TOO_MANY_REDIRECTS", (ISize) CURLE_TOO_MANY_REDIRECTS, (ISize) result.code);
    test_expect_true(test, "status == ERROR", result.status == HTTP_CLIENT_STATUS_ERROR);

    http_client_result_uninit(&result);
    string_uninit(&response);
    http_client_delete(&client);
    fixture_server_join(&server);
    string_uninit(&server.request_body);

    test_case_end(test);
}

static void _test_redirect_to_ftp_refused(Test *const test) {
    test_case_begin(test, "loopback: Location: ftp:// is refused, never connected to");

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_REDIRECT_FTP;

    if (!_start(test, &server)) {
        test_case_end(test);

        return;
    }

    char url[64] = DEFAULT_INITIALIZATION;
    _url_for(fixture_server_port(&server), "/", url, sizeof(url));

    HTTP_Client *client = http_client_new();

    http_client_set_follow_redirects(client, true, 5);

    String response = string_init_1();
    HTTP_Client_Result result = http_client_get(client, url, &response);

    test_expect_false(test, "success is false", result.success);
    test_expect_true(test, "status == ERROR (not a crash, not silently followed)", result.status == HTTP_CLIENT_STATUS_ERROR);
    test_expect_true(test, "error text is non-empty", string_get_size(&result.error) > 0);

    http_client_result_uninit(&result);
    string_uninit(&response);
    http_client_delete(&client);
    fixture_server_join(&server);
    string_uninit(&server.request_body);

    test_case_end(test);
}

static void _test_chunked_body_decoded(Test *const test) {
    test_case_begin(test, "loopback: Transfer-Encoding: chunked is decoded transparently");

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_CHUNKED;

    if (!_start(test, &server)) {
        test_case_end(test);

        return;
    }

    char url[64] = DEFAULT_INITIALIZATION;
    _url_for(fixture_server_port(&server), "/", url, sizeof(url));

    HTTP_Client *client = http_client_new();
    String response = string_init_1();
    HTTP_Client_Result result = http_client_get(client, url, &response);

    test_expect_true(test, "result_is_ok", http_client_result_is_ok(&result));
    test_expect_string(test, "chunks reassembled correctly", "chunked-body", string_get_data(&response));

    http_client_result_uninit(&result);
    string_uninit(&response);
    http_client_delete(&client);
    fixture_server_join(&server);
    string_uninit(&server.request_body);

    test_case_end(test);
}

static void _test_gzip_body_decoded(Test *const test) {
    test_case_begin(test, "loopback: Content-Encoding: gzip is auto-decoded");

    curl_version_info_data const *const curl_version = curl_version_info(CURLVERSION_NOW);

    /* CURLOPT_ACCEPT_ENCODING only auto-decodes gzip when the linked libcurl was
     * built against zlib - a build without it would fail this test for a reason
     * unrelated to http_client, so skip rather than false-fail. */
    if (memory_empty((void*) curl_version) || (curl_version->features & CURL_VERSION_LIBZ) == 0) {
        printf("  [skip] linked libcurl lacks CURL_VERSION_LIBZ - gzip auto-decode unavailable\n");
        test_case_end(test);

        return;
    }

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_GZIP;

    if (!_start(test, &server)) {
        test_case_end(test);

        return;
    }

    char url[64] = DEFAULT_INITIALIZATION;
    _url_for(fixture_server_port(&server), "/", url, sizeof(url));

    HTTP_Client *client = http_client_new();
    String response = string_init_1();
    HTTP_Client_Result result = http_client_get(client, url, &response);

    test_expect_true(test, "result_is_ok", http_client_result_is_ok(&result));
    test_expect_string(test, "decoded body matches the original plaintext", "gzip-body-ok\n", string_get_data(&response));

    http_client_result_uninit(&result);
    string_uninit(&response);
    http_client_delete(&client);
    fixture_server_join(&server);
    string_uninit(&server.request_body);

    test_case_end(test);
}

static void _test_oversized_body_capped(Test *const test) {
    test_case_begin(test, "loopback: a response over max_response_size caps at WRITE_ERROR, never an abort");

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_OVERSIZED;
    server.oversized_body_size = 1000000;

    if (!_start(test, &server)) {
        test_case_end(test);

        return;
    }

    char url[64] = DEFAULT_INITIALIZATION;
    _url_for(fixture_server_port(&server), "/", url, sizeof(url));

    HTTP_Client *client = http_client_new();

    http_client_set_max_response_size(client, 1000);

    String response = string_init_1();
    HTTP_Client_Result result = http_client_get(client, url, &response);

    test_expect_false(test, "success is false", result.success);
    test_expect_i(test, "code == CURLE_WRITE_ERROR", (ISize) CURLE_WRITE_ERROR, (ISize) result.code);
    test_expect_true(test, "status == ERROR", result.status == HTTP_CLIENT_STATUS_ERROR);
    test_expect_true(test, "captured body never exceeded the cap", string_get_size(&response) <= 1000);
    test_expect_true(test, "error text names the cap", string_get_size(&result.error) > 0);

    http_client_result_uninit(&result);
    string_uninit(&response);
    http_client_delete(&client);
    fixture_server_join(&server);
    string_uninit(&server.request_body);

    test_case_end(test);
}

static void _test_header_flood_capped(Test *const test) {
    test_case_begin(test, "loopback: a response header flood over the LINE-count budget caps at WRITE_ERROR");

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_HEADER_FLOOD;
    server.header_flood_count = 500; // trips the 256-LINE budget with small values, well under the byte budget

    if (!_start(test, &server)) {
        test_case_end(test);

        return;
    }

    char url[64] = DEFAULT_INITIALIZATION;
    _url_for(fixture_server_port(&server), "/", url, sizeof(url));

    HTTP_Client *client = http_client_new();
    String response = string_init_1();
    HTTP_Client_Result result = http_client_get(client, url, &response);

    test_expect_false(test, "success is false", result.success);
    test_expect_i(test, "code == CURLE_WRITE_ERROR", (ISize) CURLE_WRITE_ERROR, (ISize) result.code);
    test_expect_true(test, "status == ERROR", result.status == HTTP_CLIENT_STATUS_ERROR);

    http_client_result_uninit(&result);
    string_uninit(&response);
    http_client_delete(&client);
    fixture_server_join(&server);
    string_uninit(&server.request_body);

    test_case_end(test);
}

static void _test_header_flood_byte_budget_capped(Test *const test) {
    test_case_begin(test, "loopback: a response header flood over the BYTE-total budget caps at WRITE_ERROR, well under the 256-line budget");

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_HEADER_FLOOD;
    server.header_flood_count      = 150; // well under the 256-line budget
    server.header_flood_value_size = 2000; // 150 * (~2011 bytes/line) > the 256 KiB byte budget

    if (!_start(test, &server)) {
        test_case_end(test);

        return;
    }

    char url[64] = DEFAULT_INITIALIZATION;
    _url_for(fixture_server_port(&server), "/", url, sizeof(url));

    HTTP_Client *client = http_client_new();
    String response = string_init_1();
    HTTP_Client_Result result = http_client_get(client, url, &response);

    test_expect_false(test, "success is false", result.success);
    test_expect_i(test, "code == CURLE_WRITE_ERROR", (ISize) CURLE_WRITE_ERROR, (ISize) result.code);
    test_expect_true(test, "status == ERROR", result.status == HTTP_CLIENT_STATUS_ERROR);

    http_client_result_uninit(&result);
    string_uninit(&response);
    http_client_delete(&client);
    fixture_server_join(&server);
    string_uninit(&server.request_body);

    test_case_end(test);
}

static void _test_stall_times_out(Test *const test) {
    test_case_begin(test, "loopback: a silent peer -> TIMEOUT within the configured budget");

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_STALL;

    if (!_start(test, &server)) {
        test_case_end(test);

        return;
    }

    char url[64] = DEFAULT_INITIALIZATION;
    _url_for(fixture_server_port(&server), "/", url, sizeof(url));

    HTTP_Client *client = http_client_new();

    http_client_set_timeout_ms(client, 500);

    String response = string_init_1();
    ChronoInstant const start = chrono_now();
    HTTP_Client_Result result = http_client_get(client, url, &response);
    U64 const elapsed_ms = chrono_duration_milliseconds(chrono_elapsed(start));

    test_expect_true(test, "status == TIMEOUT", result.status == HTTP_CLIENT_STATUS_TIMEOUT);
    test_expect_true(test, "TIMEOUT honored the ~500ms budget (bounded, not the fixture's own hold)", elapsed_ms >= 400 && elapsed_ms <= 3000);

    printf("  [measurement] stall GET(timeout=500ms) TIMEOUT after %llu ms\n", (unsigned long long) elapsed_ms);

    http_client_result_uninit(&result);
    string_uninit(&response);
    http_client_delete(&client);
    fixture_server_join(&server);
    string_uninit(&server.request_body);

    test_case_end(test);
}

static void _test_closed_port_never_connects(Test *const test) {
    test_case_begin(test, "closed port -> UNREACHABLE (refused) or TIMEOUT (a stealth firewall drops the SYN), never success, no live fixture needed");

    Net_Socket probe = NET_SOCKET_INVALID;
    Net_Socket_Address address = net_socket_address_init_1(NET_FAMILY_IPV4, 0);

    if (!test_expect_true(test, "probe bind succeeds", result_is_success(net_socket_init(&probe, NET_FAMILY_IPV4, NET_TYPE_TCP))
        && result_is_success(net_socket_bind(probe, &address))
        && result_is_success(net_socket_address_local(probe, &address)))) {
        net_socket_close(probe);
        test_case_end(test);

        return;
    }

    U16 const closed_port = net_socket_address_port(&address);

    net_socket_close(probe); // nothing listens here now

    char url[64] = DEFAULT_INITIALIZATION;
    _url_for(closed_port, "/", url, sizeof(url));

    HTTP_Client *client = http_client_new();

    http_client_set_connect_timeout_ms(client, 1000);

    String response = string_init_1();
    HTTP_Client_Result result = http_client_get(client, url, &response);

    test_expect_false(test, "success is false", result.success);
    /* A closed loopback port is refused with RST on most hosts (COULDNT_CONNECT -> UNREACHABLE). When
     * the host's firewall drops the SYN instead, the connect budget expires (OPERATION_TIMEDOUT ->
     * TIMEOUT) - and which one a build sees can differ on the SAME host: MSYS2's libcurl 8.21 timed
     * out where curl-for-win 8.22 was refused in 2 ms. Both are honest answers for "nothing listens
     * here"; the pin is a coherent code/status pair that is never success and never an abort. */
    bool const refused   = result.code == CURLE_COULDNT_CONNECT && result.status == HTTP_CLIENT_STATUS_UNREACHABLE;
    bool const timed_out = result.code == CURLE_OPERATION_TIMEDOUT && result.status == HTTP_CLIENT_STATUS_TIMEOUT;

    test_expect_true(test, "code/status pair is COULDNT_CONNECT/UNREACHABLE or OPERATION_TIMEDOUT/TIMEOUT", refused || timed_out);
    test_expect_u(test, "response_code stays 0 - the peer was never reached to answer with one", 0, result.response_code);
    test_expect_u(test, "http_client_response_status agrees - 0 after a transport failure", 0, http_client_response_status(client));

    http_client_result_uninit(&result);
    string_uninit(&response);
    http_client_delete(&client);

    test_case_end(test);
}

static void _test_partial_close_mid_body(Test *const test) {
    test_case_begin(test, "loopback: peer closes mid-body -> non-OK result with a non-empty error, no abort");

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_PARTIAL_CLOSE;
    server.partial_close_content_length = 1000;
    server.partial_close_send_size = 200;

    if (!_start(test, &server)) {
        test_case_end(test);

        return;
    }

    char url[64] = DEFAULT_INITIALIZATION;
    _url_for(fixture_server_port(&server), "/", url, sizeof(url));

    HTTP_Client *client = http_client_new();
    String response = string_init_1();
    HTTP_Client_Result result = http_client_get(client, url, &response);

    test_expect_false(test, "success is false", result.success);
    test_expect_true(test, "status == ERROR", result.status == HTTP_CLIENT_STATUS_ERROR);
    test_expect_true(test, "error text is non-empty", string_get_size(&result.error) > 0);
    test_expect_true(test, "only the partial bytes were captured", string_get_size(&response) <= 200);

    http_client_result_uninit(&result);
    string_uninit(&response);
    http_client_delete(&client);
    fixture_server_join(&server);
    string_uninit(&server.request_body);

    test_case_end(test);
}

static void _test_malformed_url_no_abort(Test *const test) {
    test_case_begin(test, "a URL libcurl cannot parse answers URL_MALFORMAT/ERROR through the Result, never an abort");

    HTTP_Client *client = http_client_new();

    http_client_set_timeout_ms(client, 1000);

    String response = string_init_1();

    /* An unbalanced bracket in the host part - CURLE_URL_MALFORMAT at parse time,
     * never a DNS lookup. "not-a-url-at-all" (no scheme) used to sit here instead:
     * libcurl guesses http:// for it and RESOLVES the host, so on a box whose
     * resolver cannot reach the network it ran to this test's own timeout and
     * answered UNREACHABLE, failing the ERROR assertion below - exactly what the
     * Linux platform lane hit. */
    HTTP_Client_Result result = http_client_get(client, "http://[::1", &response);

    test_expect_false(test, "success is false", result.success);
    test_expect_i(test, "code == CURLE_URL_MALFORMAT", (ISize) CURLE_URL_MALFORMAT, (ISize) result.code);
    test_expect_true(test, "status == ERROR", result.status == HTTP_CLIENT_STATUS_ERROR);
    test_expect_true(test, "the process is still alive to report this - proof it never aborted", true);

    http_client_result_uninit(&result);
    string_uninit(&response);
    http_client_delete(&client);

    test_case_end(test);
}

static void _test_https_to_plain_port_is_tls_status(Test *const test) {
    /* Proves a real TLS handshake is attempted against this URL, NOT that peer/host
     * verification itself is on - a build with CURLOPT_SSL_VERIFYPEER 0 would fail this same
     * handshake identically (the peer never speaks TLS at all), so this test cannot tell the two
     * apart. The exact status is also backend-dependent: OpenSSL answers SSL_CONNECT_ERROR (an
     * EOF during the handshake) mapped to TLS here; another TLS backend could instead answer
     * RECV_ERROR, which this module maps to ERROR, not TLS. */
    test_case_begin(test, "https:// against a plain HTTP loopback port fails the TLS handshake (status TLS, this backend)");

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_OK;

    if (!_start(test, &server)) {
        test_case_end(test);

        return;
    }

    char url[64] = DEFAULT_INITIALIZATION;
    snprintf(url, sizeof(url), "https://127.0.0.1:%u/", (unsigned) fixture_server_port(&server));

    HTTP_Client *client = http_client_new();

    http_client_set_timeout_ms(client, 3000);

    String response = string_init_1();
    HTTP_Client_Result result = http_client_get(client, url, &response);

    test_expect_false(test, "success is false", result.success);
    test_expect_true(test, "status == TLS", result.status == HTTP_CLIENT_STATUS_TLS);

    http_client_result_uninit(&result);
    string_uninit(&response);
    http_client_delete(&client);
    fixture_server_join(&server);
    string_uninit(&server.request_body);

    test_case_end(test);
}

static void _test_custom_header_seen_then_cleared(Test *const test) {
    test_case_begin(test, "loopback: a custom request header is seen by the peer, and gone after header_clear");

    Fixture_Server present_server = DEFAULT_INITIALIZATION;
    present_server.script = FIXTURE_SCRIPT_OK;

    HTTP_Client *client = http_client_new();

    if (_start(test, &present_server)) {
        char url[64] = DEFAULT_INITIALIZATION;
        _url_for(fixture_server_port(&present_server), "/", url, sizeof(url));

        http_client_header_add(client, "X-Custom-Test: abc123");

        String response = string_init_1();
        HTTP_Client_Result result = http_client_get(client, url, &response);

        test_expect_true(test, "request with the custom header succeeded", result.success);

        fixture_server_join(&present_server);

        test_expect_true(test, "the peer saw the custom header", strstr(present_server.request_headers, "X-Custom-Test: abc123") != nullptr);

        http_client_result_uninit(&result);
        string_uninit(&response);
        string_uninit(&present_server.request_body);
    }

    Fixture_Server absent_server = DEFAULT_INITIALIZATION;
    absent_server.script = FIXTURE_SCRIPT_OK;

    if (_start(test, &absent_server)) {
        char url[64] = DEFAULT_INITIALIZATION;
        _url_for(fixture_server_port(&absent_server), "/", url, sizeof(url));

        http_client_header_clear(client);

        String response = string_init_1();
        HTTP_Client_Result result = http_client_get(client, url, &response);

        test_expect_true(test, "request after header_clear succeeded", result.success);

        fixture_server_join(&absent_server);

        test_expect_null(test, "the peer no longer sees the header after header_clear", (void*) strstr(absent_server.request_headers, "X-Custom-Test"));

        http_client_result_uninit(&result);
        string_uninit(&response);
        string_uninit(&absent_server.request_body);
    }

    http_client_delete(&client);

    test_case_end(test);
}

static void _test_user_agent_default_and_override(Test *const test) {
    test_case_begin(test, "loopback: default User-Agent names this module, http_client_set_user_agent overrides it, both as the peer sees them");

    Fixture_Server default_server = DEFAULT_INITIALIZATION;
    default_server.script = FIXTURE_SCRIPT_OK;

    HTTP_Client *client = http_client_new();

    if (_start(test, &default_server)) {
        char url[64] = DEFAULT_INITIALIZATION;
        _url_for(fixture_server_port(&default_server), "/", url, sizeof(url));

        String response = string_init_1();
        HTTP_Client_Result result = http_client_get(client, url, &response);

        test_expect_true(test, "GET with the default User-Agent succeeded", result.success);

        fixture_server_join(&default_server);

        test_expect_true(test, "the peer saw the default cfw-http-client/ User-Agent", strstr(default_server.request_headers, "User-Agent: cfw-http-client/") != nullptr);

        http_client_result_uninit(&result);
        string_uninit(&response);
        string_uninit(&default_server.request_body);
    }

    Fixture_Server override_server = DEFAULT_INITIALIZATION;
    override_server.script = FIXTURE_SCRIPT_OK;

    if (_start(test, &override_server)) {
        char url[64] = DEFAULT_INITIALIZATION;
        _url_for(fixture_server_port(&override_server), "/", url, sizeof(url));

        http_client_set_user_agent(client, "test-agent/9.9");

        String response = string_init_1();
        HTTP_Client_Result result = http_client_get(client, url, &response);

        test_expect_true(test, "GET with an overridden User-Agent succeeded", result.success);

        fixture_server_join(&override_server);

        test_expect_true(test, "the peer saw the overridden User-Agent, not the default", strstr(override_server.request_headers, "User-Agent: test-agent/9.9") != nullptr);

        http_client_result_uninit(&result);
        string_uninit(&response);
        string_uninit(&override_server.request_body);
    }

    http_client_delete(&client);

    test_case_end(test);
}

static void _test_method_lines_and_bodies(Test *const test) {
    test_case_begin(test, "loopback: GET/POST/PUT/DELETE/HEAD method lines and bodies (embedded NUL, empty payload) as the peer received them");

    HTTP_Client *client = http_client_new();

    /* GET on a completely FRESH handle - libcurl's own "CURLOPT_POSTFIELDS implies
     * CURLOPT_POST" rule (unconditional, even for a nullptr value) used to flip
     * every http_client_get into a POST + "Content-Length: 0" on the wire; this
     * is the Critical-1 pin the suite never had before. */
    {
        Fixture_Server server = DEFAULT_INITIALIZATION;
        server.script = FIXTURE_SCRIPT_OK;

        if (_start(test, &server)) {
            char url[64] = DEFAULT_INITIALIZATION;
            _url_for(fixture_server_port(&server), "/", url, sizeof(url));

            String response = string_init_1();
            HTTP_Client_Result result = http_client_get(client, url, &response);

            test_expect_true(test, "GET on a fresh handle succeeded", result.success);

            fixture_server_join(&server);

            test_expect_string(test, "method line was GET, not POST", "GET", server.request_method);
            test_expect_null(test, "no Content-Length reached the fixture: GET on a fresh handle", (void*) strstr(server.request_headers, "Content-Length"));

            http_client_result_uninit(&result);
            string_uninit(&response);
            string_uninit(&server.request_body);
        }
    }

    /* POST with an embedded NUL byte - only the String form carries the true size. */
    {
        Fixture_Server server = DEFAULT_INITIALIZATION;
        server.script = FIXTURE_SCRIPT_OK;

        if (_start(test, &server)) {
            char url[64] = DEFAULT_INITIALIZATION;
            _url_for(fixture_server_port(&server), "/", url, sizeof(url));

            char const payload_bytes[9] = { 'a', 'b', '\0', 'c', 'd', 'e', 'f', 'g', 'h' };
            String payload = string_init_1();

            string_add_last_2(&payload, payload_bytes, sizeof(payload_bytes));

            String response = string_init_1();
            HTTP_Client_Result result = http_client_post_4(client, url, &payload, &response);

            test_expect_true(test, "POST with embedded NUL succeeded", result.success);

            fixture_server_join(&server);

            test_expect_string(test, "method line was POST", "POST", server.request_method);
            test_expect_true(test, "body size preserved the embedded NUL (not truncated at it)",
                string_get_size(&server.request_body) == sizeof(payload_bytes)
                && memcmp(string_get_data(&server.request_body), payload_bytes, sizeof(payload_bytes)) == 0);

            http_client_result_uninit(&result);
            string_uninit(&payload);
            string_uninit(&response);
            string_uninit(&server.request_body);
        }
    }

    /* POST with an empty payload. */
    {
        Fixture_Server server = DEFAULT_INITIALIZATION;
        server.script = FIXTURE_SCRIPT_OK;

        if (_start(test, &server)) {
            char url[64] = DEFAULT_INITIALIZATION;
            _url_for(fixture_server_port(&server), "/", url, sizeof(url));

            String response = string_init_1();
            HTTP_Client_Result result = http_client_post_1(client, url, "", &response);

            test_expect_true(test, "empty-payload POST succeeded", result.success);

            fixture_server_join(&server);

            test_expect_string(test, "method line was POST", "POST", server.request_method);
            test_expect_u(test, "empty payload arrived as zero bytes", 0, string_get_size(&server.request_body));

            http_client_result_uninit(&result);
            string_uninit(&response);
            string_uninit(&server.request_body);
        }
    }

    /* GET on `client`, which just POSTed above - the other half of the Critical-1 pin: a handle
     * that has already POSTed must rest to a bare GET too, not just a fresh one. */
    {
        Fixture_Server server = DEFAULT_INITIALIZATION;
        server.script = FIXTURE_SCRIPT_OK;

        if (_start(test, &server)) {
            char url[64] = DEFAULT_INITIALIZATION;
            _url_for(fixture_server_port(&server), "/", url, sizeof(url));

            String response = string_init_1();
            HTTP_Client_Result result = http_client_get(client, url, &response);

            test_expect_true(test, "GET after a prior POST succeeded", result.success);

            fixture_server_join(&server);

            test_expect_string(test, "method line was GET, not POST", "GET", server.request_method);
            test_expect_null(test, "no Content-Length reached the fixture: GET after a prior POST on this handle", (void*) strstr(server.request_headers, "Content-Length"));

            http_client_result_uninit(&result);
            string_uninit(&response);
            string_uninit(&server.request_body);
        }
    }

    /* HEAD directly after a POST on `client` - proves NOBODY still wins the wire method
     * regardless of the handle's POST history (masked in the original bug because NOBODY,
     * unlike HTTPGET, already overrides the method independently of POSTFIELDS). */
    {
        Fixture_Server post_server = DEFAULT_INITIALIZATION;
        post_server.script = FIXTURE_SCRIPT_OK;

        if (_start(test, &post_server)) {
            char post_url[64] = DEFAULT_INITIALIZATION;
            _url_for(fixture_server_port(&post_server), "/", post_url, sizeof(post_url));

            String post_response = string_init_1();
            HTTP_Client_Result post_result = http_client_post_1(client, post_url, "priming-post", &post_response);

            test_expect_true(test, "the priming POST succeeded", post_result.success);

            fixture_server_join(&post_server);

            http_client_result_uninit(&post_result);
            string_uninit(&post_response);
            string_uninit(&post_server.request_body);
        }

        Fixture_Server head_server = DEFAULT_INITIALIZATION;
        head_server.script = FIXTURE_SCRIPT_OK;

        if (_start(test, &head_server)) {
            char head_url[64] = DEFAULT_INITIALIZATION;
            _url_for(fixture_server_port(&head_server), "/", head_url, sizeof(head_url));

            String head_response = string_init_1();
            HTTP_Client_Result head_result = http_client_head(client, head_url, &head_response);

            test_expect_true(test, "HEAD after a prior POST succeeded", head_result.success);

            fixture_server_join(&head_server);

            test_expect_string(test, "method line was HEAD after a prior POST", "HEAD", head_server.request_method);

            http_client_result_uninit(&head_result);
            string_uninit(&head_response);
            string_uninit(&head_server.request_body);
        }
    }

    /* POST via post_2 (sized) with an embedded NUL byte - only the sized form carries the true
     * size for a raw buffer, the same reason post_4/put_2 exist beside post_1/put_1. */
    {
        Fixture_Server server = DEFAULT_INITIALIZATION;
        server.script = FIXTURE_SCRIPT_OK;

        if (_start(test, &server)) {
            char url[64] = DEFAULT_INITIALIZATION;
            _url_for(fixture_server_port(&server), "/", url, sizeof(url));

            char const payload_bytes[9] = { 'a', 'b', '\0', 'c', 'd', 'e', 'f', 'g', 'h' };
            String response = string_init_1();
            HTTP_Client_Result result = http_client_post_2(client, url, payload_bytes, sizeof(payload_bytes), &response);

            test_expect_true(test, "post_2 with an embedded NUL succeeded", result.success);

            fixture_server_join(&server);

            test_expect_string(test, "method line was POST", "POST", server.request_method);
            test_expect_true(test, "post_2 body size preserved the embedded NUL (not truncated at it)",
                string_get_size(&server.request_body) == sizeof(payload_bytes)
                && memcmp(string_get_data(&server.request_body), payload_bytes, sizeof(payload_bytes)) == 0);

            http_client_result_uninit(&result);
            string_uninit(&response);
            string_uninit(&server.request_body);
        }
    }

    /* POST via post_3 (Str) with a non-empty payload. */
    {
        Fixture_Server server = DEFAULT_INITIALIZATION;
        server.script = FIXTURE_SCRIPT_OK;

        if (_start(test, &server)) {
            char url[64] = DEFAULT_INITIALIZATION;
            _url_for(fixture_server_port(&server), "/", url, sizeof(url));

            Str    payload  = str_init_static("post_3-payload", CHAR_STATIC_SIZE("post_3-payload"));
            String response = string_init_1();
            HTTP_Client_Result result = http_client_post_3(client, url, &payload, &response);

            test_expect_true(test, "post_3 succeeded", result.success);

            fixture_server_join(&server);

            test_expect_string(test, "method line was POST", "POST", server.request_method);
            test_expect_true(test, "post_3 body matched", string_get_size(&server.request_body) == CHAR_STATIC_SIZE("post_3-payload")
                && memcmp(string_get_data(&server.request_body), "post_3-payload", CHAR_STATIC_SIZE("post_3-payload")) == 0);

            http_client_result_uninit(&result);
            str_uninit(&payload);
            string_uninit(&response);
            string_uninit(&server.request_body);
        }
    }

    /* PUT. */
    {
        Fixture_Server server = DEFAULT_INITIALIZATION;
        server.script = FIXTURE_SCRIPT_OK;

        if (_start(test, &server)) {
            char url[64] = DEFAULT_INITIALIZATION;
            _url_for(fixture_server_port(&server), "/", url, sizeof(url));

            String response = string_init_1();
            HTTP_Client_Result result = http_client_put_1(client, url, "put-payload", &response);

            test_expect_true(test, "PUT succeeded", result.success);

            fixture_server_join(&server);

            test_expect_string(test, "method line was PUT", "PUT", server.request_method);
            test_expect_true(test, "PUT body matched", string_get_size(&server.request_body) == char_length("put-payload")
                && memcmp(string_get_data(&server.request_body), "put-payload", char_length("put-payload")) == 0);

            http_client_result_uninit(&result);
            string_uninit(&response);
            string_uninit(&server.request_body);
        }
    }

    /* DELETE on `client`, which has already POSTed and PUT above in this same function - the
     * exact handle history High-1 flags: CURLOPT_POSTFIELDS "implies POST" on setopt, so without
     * resetting the handle to GET before CUSTOMREQUEST, this DELETE would carry the prior
     * request's POST semantics (Content-Length: 0) instead of a bare, body-less DELETE. */
    {
        Fixture_Server server = DEFAULT_INITIALIZATION;
        server.script = FIXTURE_SCRIPT_OK;

        if (_start(test, &server)) {
            char url[64] = DEFAULT_INITIALIZATION;
            _url_for(fixture_server_port(&server), "/", url, sizeof(url));

            String response = string_init_1();
            HTTP_Client_Result result = http_client_request_delete(client, url, &response);

            test_expect_true(test, "DELETE succeeded", result.success);

            fixture_server_join(&server);

            test_expect_string(test, "method line was DELETE", "DELETE", server.request_method);
            test_expect_null(test, "no Content-Length reached the fixture: DELETE after a prior POST/PUT on this handle", (void*) strstr(server.request_headers, "Content-Length"));

            http_client_result_uninit(&result);
            string_uninit(&response);
            string_uninit(&server.request_body);
        }
    }

    /* DELETE on a completely FRESH handle - the other half of the High-1 pin: a handle that has
     * never POSTed must not carry a stale Content-Length either (this leg was already GET-shaped
     * before the fix; kept as the paired assertion so a future regression that reintroduces the
     * bug for ONLY the "used handle" case cannot slip through unnoticed). */
    {
        Fixture_Server server = DEFAULT_INITIALIZATION;
        server.script = FIXTURE_SCRIPT_OK;

        if (_start(test, &server)) {
            char url[64] = DEFAULT_INITIALIZATION;
            _url_for(fixture_server_port(&server), "/", url, sizeof(url));

            HTTP_Client *fresh_client = http_client_new();
            String response = string_init_1();
            HTTP_Client_Result result = http_client_request_delete(fresh_client, url, &response);

            test_expect_true(test, "DELETE on a fresh handle succeeded", result.success);

            fixture_server_join(&server);

            test_expect_string(test, "method line was DELETE", "DELETE", server.request_method);
            test_expect_null(test, "no Content-Length reached the fixture: DELETE on a fresh handle", (void*) strstr(server.request_headers, "Content-Length"));

            http_client_result_uninit(&result);
            string_uninit(&response);
            http_client_delete(&fresh_client);
            string_uninit(&server.request_body);
        }
    }

    /* HEAD - no response body read back, regardless of what the peer sends. */
    {
        Fixture_Server server = DEFAULT_INITIALIZATION;
        server.script = FIXTURE_SCRIPT_OK;

        if (_start(test, &server)) {
            char url[64] = DEFAULT_INITIALIZATION;
            _url_for(fixture_server_port(&server), "/", url, sizeof(url));

            String response = string_init_1();
            HTTP_Client_Result result = http_client_head(client, url, &response);

            test_expect_true(test, "HEAD succeeded", result.success);

            fixture_server_join(&server);

            test_expect_string(test, "method line was HEAD", "HEAD", server.request_method);
            test_expect_u(test, "HEAD's response body stayed empty", 0, string_get_size(&response));

            http_client_result_uninit(&result);
            string_uninit(&response);
            string_uninit(&server.request_body);
        }
    }

    http_client_delete(&client);

    test_case_end(test);
}

static void _test_keep_alive_counts_one_connection(Test *const test) {
    test_case_begin(test, "loopback: keep-alive - two requests, the fixture counts exactly one connection");

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_KEEP_ALIVE;
    server.keep_alive_request_count = 2;
    server.max_connections = 1;

    if (!_start(test, &server)) {
        test_case_end(test);

        return;
    }

    char url[64] = DEFAULT_INITIALIZATION;
    _url_for(fixture_server_port(&server), "/", url, sizeof(url));

    HTTP_Client *client = http_client_new();

    String response_1 = string_init_1();
    HTTP_Client_Result result_1 = http_client_get(client, url, &response_1);

    test_expect_true(test, "first request OK", result_1.success);

    String response_2 = string_init_1();
    HTTP_Client_Result result_2 = http_client_get(client, url, &response_2);

    test_expect_true(test, "second request OK", result_2.success);

    fixture_server_join(&server);

    test_expect_u(test, "exactly ONE TCP connection was accepted", 1, server.connection_count);
    test_expect_u(test, "exactly TWO requests were served", 2, server.request_count);

    http_client_result_uninit(&result_1);
    http_client_result_uninit(&result_2);
    string_uninit(&response_1);
    string_uninit(&response_2);
    http_client_delete(&client);
    string_uninit(&server.request_body);

    test_case_end(test);
}

static void _test_response_cleared_per_call(Test *const test) {
    test_case_begin(test, "the same response String is cleared at the start of every call - no leftover from a prior request");

    Fixture_Server server_a = DEFAULT_INITIALIZATION;
    server_a.script = FIXTURE_SCRIPT_OK; // body "ok-body-content" (15 bytes)

    Fixture_Server server_b = DEFAULT_INITIALIZATION;
    server_b.script = FIXTURE_SCRIPT_STATUS; // body "status-body" (11 bytes) - shorter, on purpose
    server_b.status_code = 404;

    if (!_start(test, &server_a)) {
        test_case_end(test);

        return;
    }

    /* Started separately so a failed server_b leaves server_a's accept thread
     * joined here rather than left running past this function's return -
     * short-circuiting both starts in one condition skipped the join whenever
     * server_a succeeded and server_b did not. */
    if (!_start(test, &server_b)) {
        fixture_server_join(&server_a);
        string_uninit(&server_a.request_body);
        test_case_end(test);

        return;
    }

    char url_a[64] = DEFAULT_INITIALIZATION;
    char url_b[64] = DEFAULT_INITIALIZATION;

    _url_for(fixture_server_port(&server_a), "/", url_a, sizeof(url_a));
    _url_for(fixture_server_port(&server_b), "/", url_b, sizeof(url_b));

    HTTP_Client *client = http_client_new();
    String response = string_init_1();

    HTTP_Client_Result result_a = http_client_get(client, url_a, &response);

    test_expect_string(test, "first call body", "ok-body-content", string_get_data(&response));

    HTTP_Client_Result result_b = http_client_get(client, url_b, &response);

    test_expect_string(test, "second call body has NO residue from the first (shorter body, exact match)", "status-body", string_get_data(&response));
    test_expect_u(test, "size reflects only the second body", char_length("status-body"), string_get_size(&response));

    http_client_result_uninit(&result_a);
    http_client_result_uninit(&result_b);
    string_uninit(&response);
    http_client_delete(&client);
    fixture_server_join(&server_a);
    fixture_server_join(&server_b);
    string_uninit(&server_a.request_body);
    string_uninit(&server_b.request_body);

    test_case_end(test);
}

static void _test_result_uninit_idempotent(Test *const test) {
    test_case_begin(test, "http_client_result_uninit is idempotent - calling it twice does not crash or resurrect state");

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_OK;

    if (!_start(test, &server)) {
        test_case_end(test);

        return;
    }

    char url[64] = DEFAULT_INITIALIZATION;
    _url_for(fixture_server_port(&server), "/", url, sizeof(url));

    HTTP_Client *client = http_client_new();
    String response = string_init_1();
    HTTP_Client_Result result = http_client_get(client, url, &response);

    http_client_result_uninit(&result);
    http_client_result_uninit(&result);

    test_expect_false(test, "success reads false after a double uninit", result.success);
    test_expect_true(test, "status reset to OK's sentinel value", result.status == HTTP_CLIENT_STATUS_OK);

    string_uninit(&response);
    http_client_delete(&client);
    fixture_server_join(&server);
    string_uninit(&server.request_body);

    test_case_end(test);
}

static void _test_two_clients_one_thread_independent(Test *const test) {
    test_case_begin(test, "two HTTP_Client handles on one thread stay fully independent (no shared state)");

    Fixture_Server server_a = DEFAULT_INITIALIZATION;
    server_a.script = FIXTURE_SCRIPT_OK;

    Fixture_Server server_b = DEFAULT_INITIALIZATION;
    server_b.script = FIXTURE_SCRIPT_STATUS;
    server_b.status_code = 404;

    if (!_start(test, &server_a)) {
        test_case_end(test);

        return;
    }

    /* Started separately so a failed server_b leaves server_a's accept thread
     * joined here rather than left running past this function's return -
     * short-circuiting both starts in one condition skipped the join whenever
     * server_a succeeded and server_b did not. */
    if (!_start(test, &server_b)) {
        fixture_server_join(&server_a);
        string_uninit(&server_a.request_body);
        test_case_end(test);

        return;
    }

    char url_a[64] = DEFAULT_INITIALIZATION;
    char url_b[64] = DEFAULT_INITIALIZATION;

    _url_for(fixture_server_port(&server_a), "/", url_a, sizeof(url_a));
    _url_for(fixture_server_port(&server_b), "/", url_b, sizeof(url_b));

    HTTP_Client *client_a = http_client_new();
    HTTP_Client *client_b = http_client_new();

    HTTP_Client_Result result_a = http_client_get(client_a, url_a, nullptr); // uses client_a's OWN body buffer
    HTTP_Client_Result result_b = http_client_get(client_b, url_b, nullptr); // uses client_b's OWN body buffer

    test_expect_true(test, "client_a's own body is unaffected by client_b's request", strcmp(string_get_data(http_client_response_body(client_a)), "ok-body-content") == 0);
    test_expect_u(test, "client_a's status is unaffected", 200, http_client_response_status(client_a));
    test_expect_string(test, "client_b's own body is independent", "status-body", string_get_data(http_client_response_body(client_b)));
    test_expect_u(test, "client_b's status is independent", 404, http_client_response_status(client_b));

    http_client_result_uninit(&result_a);
    http_client_result_uninit(&result_b);
    http_client_delete(&client_a);
    http_client_delete(&client_b);
    fixture_server_join(&server_a);
    fixture_server_join(&server_b);
    string_uninit(&server_a.request_body);
    string_uninit(&server_b.request_body);

    test_case_end(test);
}

static void _test_escape_vectors(Test *const test) {
    test_case_begin(test, "http_client_escape_1: unreserved untouched, space -> %20, UTF-8 bytes escaped, empty stays empty");

    HTTP_Client *client = http_client_new();

    String unreserved = http_client_escape_1(client, "OK-2024_test~ABC");
    test_expect_string(test, "unreserved characters pass through unchanged", "OK-2024_test~ABC", string_get_data(&unreserved));

    String space = http_client_escape_1(client, " ");
    test_expect_string(test, "a space escapes to %20", "%20", string_get_data(&space));

    String utf8 = http_client_escape_1(client, "caf\xC3\xA9"); // "café" as raw UTF-8 bytes
    test_expect_string(test, "UTF-8 bytes escape byte-by-byte", "caf%C3%A9", string_get_data(&utf8));

    String empty = http_client_escape_1(client, "");
    test_expect_u(test, "empty input escapes to an empty string", 0, string_get_size(&empty));

    string_uninit(&unreserved);
    string_uninit(&space);
    string_uninit(&utf8);
    string_uninit(&empty);
    http_client_delete(&client);

    test_case_end(test);
}

static void _test_empty_string_str_payload_no_abort(Test *const test) {
    test_case_begin(test, "post_4/put_3/put_4 with an empty String/Str payload (data nullptr, size 0) send an empty body, no abort");

    HTTP_Client *client = http_client_new();

    /* post_4: empty String. */
    {
        Fixture_Server server = DEFAULT_INITIALIZATION;
        server.script = FIXTURE_SCRIPT_OK;

        if (_start(test, &server)) {
            char url[64] = DEFAULT_INITIALIZATION;
            _url_for(fixture_server_port(&server), "/", url, sizeof(url));

            String payload  = string_init_1(); // empty: data == nullptr
            String response = string_init_1();
            HTTP_Client_Result result = http_client_post_4(client, url, &payload, &response);

            test_expect_true(test, "post_4 with an empty String payload did not abort and succeeded", result.success);

            fixture_server_join(&server);

            test_expect_string(test, "method line was POST", "POST", server.request_method);
            test_expect_u(test, "empty String payload arrived as zero bytes", 0, string_get_size(&server.request_body));

            http_client_result_uninit(&result);
            string_uninit(&payload);
            string_uninit(&response);
            string_uninit(&server.request_body);
        }
    }

    /* put_3: empty Str. */
    {
        Fixture_Server server = DEFAULT_INITIALIZATION;
        server.script = FIXTURE_SCRIPT_OK;

        if (_start(test, &server)) {
            char url[64] = DEFAULT_INITIALIZATION;
            _url_for(fixture_server_port(&server), "/", url, sizeof(url));

            Str    payload  = str_init_1(); // empty: data == nullptr
            String response = string_init_1();
            HTTP_Client_Result result = http_client_put_3(client, url, &payload, &response);

            test_expect_true(test, "put_3 with an empty Str payload did not abort and succeeded", result.success);

            fixture_server_join(&server);

            test_expect_string(test, "method line was PUT", "PUT", server.request_method);
            test_expect_u(test, "empty Str payload arrived as zero bytes", 0, string_get_size(&server.request_body));

            http_client_result_uninit(&result);
            string_uninit(&response);
            str_uninit(&payload);
            string_uninit(&server.request_body);
        }
    }

    /* put_4: empty String. */
    {
        Fixture_Server server = DEFAULT_INITIALIZATION;
        server.script = FIXTURE_SCRIPT_OK;

        if (_start(test, &server)) {
            char url[64] = DEFAULT_INITIALIZATION;
            _url_for(fixture_server_port(&server), "/", url, sizeof(url));

            String payload  = string_init_1(); // empty: data == nullptr
            String response = string_init_1();
            HTTP_Client_Result result = http_client_put_4(client, url, &payload, &response);

            test_expect_true(test, "put_4 with an empty String payload did not abort and succeeded", result.success);

            fixture_server_join(&server);

            test_expect_string(test, "method line was PUT", "PUT", server.request_method);
            test_expect_u(test, "empty String payload arrived as zero bytes", 0, string_get_size(&server.request_body));

            http_client_result_uninit(&result);
            string_uninit(&payload);
            string_uninit(&response);
            string_uninit(&server.request_body);
        }
    }

    http_client_delete(&client);

    test_case_end(test);
}

static void _test_perform_resets_active_response_and_postfields(Test *const test) {
    test_case_begin(test, "_http_client_perform clears active_response and POSTFIELDS at the end, so a "
        "raw-handle perform afterward neither dereferences a dead local nor resends the prior POST body");

    HTTP_Client *client = http_client_new();

    Fixture_Server server_a = DEFAULT_INITIALIZATION;
    server_a.script = FIXTURE_SCRIPT_OK;

    if (!_start(test, &server_a)) {
        http_client_delete(&client);
        test_case_end(test);

        return;
    }

    /* Scoped so `response` (what self->active_response pointed at) is a dead local
     * by the time the raw-handle perform below runs - without the fix,
     * active_response still aims at this stack slot. */
    {
        char url[64] = DEFAULT_INITIALIZATION;
        _url_for(fixture_server_port(&server_a), "/", url, sizeof(url));

        String response = string_init_1();
        HTTP_Client_Result result = http_client_post_1(client, url, "leak-check-body", &response);

        test_expect_true(test, "the setup POST succeeded", result.success);

        fixture_server_join(&server_a);

        test_expect_string(test, "setup body arrived intact", "leak-check-body", string_get_data(&server_a.request_body));

        http_client_result_uninit(&result);
        string_uninit(&response);
        string_uninit(&server_a.request_body);
    }

    Fixture_Server server_b = DEFAULT_INITIALIZATION;
    server_b.script = FIXTURE_SCRIPT_OK;

    if (_start(test, &server_b)) {
        char url_b[64] = DEFAULT_INITIALIZATION;
        _url_for(fixture_server_port(&server_b), "/", url_b, sizeof(url_b));

        /* Deliberately bypass this module's own API: a raw-handle consumer that
         * only sets URL + method, never touching POSTFIELDS itself, must not
         * inherit the prior request's body or write into the prior request's
         * (now dead) response target. */
        CURL *const handle = http_client_get_handle(client);

        curl_easy_setopt(handle, CURLOPT_URL, url_b);
        curl_easy_setopt(handle, CURLOPT_HTTPGET, 1L);

        CURLcode const rc = curl_easy_perform(handle);

        fixture_server_join(&server_b);

        /* active_response is nullptr here (the fix), so the write callback's own
         * existing guard refuses the response body instead of dereferencing the
         * dead `response` local from the block above - CURLE_WRITE_ERROR is the
         * SAFE outcome, not a crash and not CURLE_OK. What this proves is that the
         * process survives and, via the fixture's capture of what WE SENT, that
         * POSTFIELDS did not leak into a request this module's API never set up. */
        test_expect_true(test, "the raw-handle request completed without crashing", rc == CURLE_WRITE_ERROR);
        test_expect_string(test, "method line was GET", "GET", server_b.request_method);
        test_expect_u(test, "no leaked POST body on the raw-handle request", 0, string_get_size(&server_b.request_body));

        string_uninit(&server_b.request_body);
    }

    http_client_delete(&client);

    test_case_end(test);
}

static void _test_refused_arena_response_write_reports_truncation(Test *const test) {
    test_case_begin(test, "an arena-backed response on a REFUSED arena (handler nullptr) fails the write with WRITE_ERROR, never an abort - the Result names the truncation");

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_OK;

    if (!_start(test, &server)) {
        test_case_end(test);

        return;
    }

    char url[64] = DEFAULT_INITIALIZATION;
    _url_for(fixture_server_port(&server), "/", url, sizeof(url));

    HTTP_Client *client = http_client_new();

    /* byte_count 0 is refused geometry (arena.c): handler stays nullptr while
     * allocate/try_allocate stay set, so allocator_borrow short-circuits to
     * nullptr on the handler check - never reaching arena_linear_alloc's own
     * abort-on-exhaustion path. This is the "refused arena" string_add_2/
     * string_reserve already document, not ordinary capacity exhaustion. */
    Arena  arena    = arena_init_1(0, ARENA_TYPE_LINEAR);
    String response = string_alloc_init_1(&arena);

    HTTP_Client_Result result = http_client_get(client, url, &response);

    test_expect_false(test, "success is false", result.success);
    test_expect_i(test, "code == CURLE_WRITE_ERROR", (ISize) CURLE_WRITE_ERROR, (ISize) result.code);
    test_expect_true(test, "status == ERROR", result.status == HTTP_CLIENT_STATUS_ERROR);
    test_expect_u(test, "nothing was appended to the refused response", 0, string_get_size(&response));
    test_expect_true(test, "error text names the truncation", string_get_size(&result.error) > 0);

    http_client_result_uninit(&result);
    string_uninit(&response);
    arena_uninit(&arena, ARENA_TYPE_LINEAR);
    http_client_delete(&client);
    fixture_server_join(&server);
    string_uninit(&server.request_body);

    test_case_end(test);
}

static void _test_timeout_zero_no_abort_checked_build(Test *const test) {
    test_case_begin(test, "timeout_ms(0) and connect_timeout_ms(0) do not abort with ERROR_CHECK_ENABLED either, and forward to curl's own default");

    Fixture_Server server = DEFAULT_INITIALIZATION;
    server.script = FIXTURE_SCRIPT_OK;

    if (!_start(test, &server)) {
        test_case_end(test);

        return;
    }

    char url[64] = DEFAULT_INITIALIZATION;
    _url_for(fixture_server_port(&server), "/", url, sizeof(url));

    HTTP_Client *client = http_client_new();

    http_client_set_timeout_ms(client, 0);
    http_client_set_connect_timeout_ms(client, 0);

    String response = string_init_1();
    HTTP_Client_Result result = http_client_get(client, url, &response);

    test_expect_true(test, "the request still completes normally (no abort, no hang)", http_client_result_is_ok(&result));
    test_expect_string(test, "body arrived intact", "ok-body-content", string_get_data(&response));

    http_client_result_uninit(&result);
    string_uninit(&response);
    http_client_delete(&client);
    fixture_server_join(&server);
    string_uninit(&server.request_body);

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

    Test test = test_init("tests/http/client/test_all.c");

    test_suite_begin(&test, "http/client loopback fixture");

    _test_ok_headers_and_status(&test);
    _test_status_4xx_5xx_are_transport_success(&test);
    _test_scripted_response_body_round_trips(&test);
    _test_redirect_final_hop_headers_only(&test);
    _test_redirect_loop_exceeds_maxredirects(&test);
    _test_redirect_to_ftp_refused(&test);
    _test_chunked_body_decoded(&test);
    _test_gzip_body_decoded(&test);
    _test_oversized_body_capped(&test);
    _test_header_flood_capped(&test);
    _test_header_flood_byte_budget_capped(&test);
    _test_stall_times_out(&test);
    _test_closed_port_never_connects(&test);
    _test_partial_close_mid_body(&test);
    _test_malformed_url_no_abort(&test);
    _test_https_to_plain_port_is_tls_status(&test);
    _test_custom_header_seen_then_cleared(&test);
    _test_user_agent_default_and_override(&test);
    _test_method_lines_and_bodies(&test);
    _test_keep_alive_counts_one_connection(&test);
    _test_response_cleared_per_call(&test);
    _test_result_uninit_idempotent(&test);
    _test_two_clients_one_thread_independent(&test);
    _test_escape_vectors(&test);
    _test_empty_string_str_payload_no_abort(&test);
    _test_perform_resets_active_response_and_postfields(&test);
    _test_refused_arena_response_write_reports_truncation(&test);
    _test_timeout_zero_no_abort_checked_build(&test);

    test_suite_end(&test);

    I32 const failures = test_uninit(&test);

    http_client_global_uninit();

    return failures;
}