/*
 * test_all.c - the http/service/csrf suite.
 *
 * Offline cases pin the token shape, the exact cookie header VALUE both builders emit (no
 * HttpOnly - the page's own script has to read it), the method table's EXACT-case comparison,
 * both verifier tiers, and every constructor refusal. Live cases drive a REAL server -
 * http_server_run on port 0, the ephemeral port read back with http_server_get_port, and a raw
 * `net` socket for the client - through the full double-submit round trip: GET issues the
 * cookie, POST echoes it in the configured header, and every way of getting that wrong answers
 * 403. Modelled on tests/http/server/test_all.c.
 *
 * The constant-time property of the comparison is NOT testable here; it is a property of
 * char_compare_equal_comptime_2 and is stated in csrf.h rather than pinned.
 *
 * test_unchecked.c holds the half this build cannot observe: every refusal above is an ordinary
 * runtime branch, not an artifact of ERROR_CHECK_ENABLED.
 */
#include <stdarg.h>
#include <stdio.h>

#include <arena/arena.h>
#include <http/server/http_server.h>
#include <http/service/csrf/csrf.h>
#include <log/log.h>
#include <math/scalar.h>
#include <net/net.h>
#include <test/test.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/
#define _CLIENT_BODY_MAX        16384
#define _CLIENT_HEADERS_MAX     8192
#define _CLIENT_IO_TIMEOUT_MS   4000
#define _CLIENT_RAW_MAX         32768

/*==============================================================================
 * MARK: - Raw client
 *============================================================================*/
typedef struct {
    U16     status;
    char    headers[_CLIENT_HEADERS_MAX];
    USize   headers_size;
    char    body[_CLIENT_BODY_MAX];
    USize   body_size;
} _Reply;

static char _raw[_CLIENT_RAW_MAX];

static bool _client_open(U16 const port, Net_Socket *const out) {
    Net_Socket_Address address = DEFAULT_INITIALIZATION;

    if (result_is_error(net_socket_address_init_2(NET_FAMILY_IPV4, port, "127.0.0.1", &address))) {
        return false;
    }

    if (result_is_error(net_socket_init(out, NET_FAMILY_IPV4, NET_TYPE_TCP))) {
        return false;
    }

    if (result_is_error(net_socket_connect(*out, &address))) {
        net_socket_close(*out);

        return false;
    }

    net_socket_set_timeout(*out, _CLIENT_IO_TIMEOUT_MS);
    net_socket_set_nodelay(*out, true);

    return true;
}

static bool _client_write(Net_Socket const socket, void const *const data, USize const size) {
    USize sent_total = 0;

    while (sent_total < size) {
        USize   sent    = 0;
        Result  result  = net_socket_send_1(socket, (Byte const*) data + sent_total, size - sent_total, &sent);

        if (result_is_error(result) || sent == 0) {
            return false;
        }

        sent_total += sent;
    }

    return true;
}

/** @brief Find a header value, case-insensitively, in a reply's header block. */
static bool _reply_header(_Reply const *const reply, char const *const name, char *const out, USize const capacity) {
    static char lowered[_CLIENT_HEADERS_MAX];

    out[0] = '\0';

    USize const size = reply->headers_size < sizeof(lowered) ? reply->headers_size : sizeof(lowered) - 1;

    memory_copy_1((Byte*) lowered, (Byte*) reply->headers, size);

    lowered[size] = '\0';

    char_lower_2(lowered, size);

    char const *const found = char_find_slice_5(lowered, size, 0, name, char_length(name));

    if (found == nullptr) {
        return false;
    }

    USize cursor = (USize) (found - lowered) + char_length(name);

    while (cursor < size && (reply->headers[cursor] == ' ' || reply->headers[cursor] == '\t')) {
        cursor += 1;
    }

    USize stop = cursor;

    while (stop < size && reply->headers[stop] != '\r' && reply->headers[stop] != '\n') {
        stop += 1;
    }

    if (stop - cursor >= capacity) {
        return false;
    }

    if (stop > cursor) {
        char_copy_3(out, capacity, reply->headers + cursor, stop - cursor);
    }

    out[stop - cursor] = '\0';

    return true;
}

/** @brief Read one HTTP/1.1 reply: status line, headers, then Content-Length bytes of body. */
static bool _client_read(Net_Socket const socket, _Reply *const reply) {
    USize raw_size      = 0;
    USize header_end    = 0;

    *reply = (_Reply) DEFAULT_INITIALIZATION;

    while (raw_size + 1 < sizeof(_raw)) {
        USize   received    = 0;
        Result  result      = net_socket_recv_1(socket, _raw + raw_size, sizeof(_raw) - 1 - raw_size, &received);

        if (result_is_error(result) || received == 0) {
            break;
        }

        raw_size = raw_size + received;
        _raw[raw_size] = '\0';

        char const *const terminator = char_find_slice_5(_raw, raw_size, 0, "\r\n\r\n", 4);

        if (terminator != nullptr) {
            header_end = (USize) (terminator - _raw) + 4;

            break;
        }
    }

    if (header_end == 0) {
        return false;
    }

    reply->headers_size = header_end < sizeof(reply->headers) ? header_end : sizeof(reply->headers) - 1;

    memory_copy_1((Byte*) reply->headers, (Byte*) _raw, reply->headers_size);

    reply->headers[reply->headers_size] = '\0';

    if (raw_size >= 12) {
        reply->status = (U16) (((_raw[9] - '0') * 100) + ((_raw[10] - '0') * 10) + (_raw[11] - '0'));
    }

    char    length_text[32] = DEFAULT_INITIALIZATION;
    USize   expected        = 0;

    if (_reply_header(reply, "content-length:", length_text, sizeof(length_text))) {
        for (USize index = 0; length_text[index] >= '0' && length_text[index] <= '9'; index += 1) {
            expected = (expected * 10) + (USize) (length_text[index] - '0');
        }
    }

    USize body_have = raw_size - header_end;

    while (body_have < expected && raw_size + 1 < sizeof(_raw)) {
        USize   received    = 0;
        Result  result      = net_socket_recv_1(socket, _raw + raw_size, sizeof(_raw) - 1 - raw_size, &received);

        if (result_is_error(result) || received == 0) {
            break;
        }

        raw_size    = raw_size + received;
        body_have   = raw_size - header_end;
    }

    reply->body_size = body_have < sizeof(reply->body) ? body_have : sizeof(reply->body) - 1;

    if (reply->body_size > 0) {
        memory_copy_1((Byte*) reply->body, (Byte*) _raw + header_end, reply->body_size);
    }

    reply->body[reply->body_size] = '\0';

    return true;
}

/** @brief One request, one reply, one connection. */
static bool _client_round_trip(U16 const port, char const *const request, _Reply *const reply) {
    Net_Socket socket = DEFAULT_INITIALIZATION;

    if (!_client_open(port, &socket)) {
        return false;
    }

    bool ok = _client_write(socket, request, char_length(request));

    if (ok) {
        ok = _client_read(socket, reply);
    }

    net_socket_close(socket);

    return ok;
}

/*==============================================================================
 * MARK: - Offline cases
 *============================================================================*/
static void _test_token_shape(Test *const test) {
    test_case_begin(test, "token_create: 2 * token_byte_count lowercase hex, two tokens differ, expiry clamps");

    HTTP_Service_CSRF csrf = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_1 accepts the defaults", http_service_csrf_init_1(&csrf));

    HTTP_Service_CSRF_Token first  = DEFAULT_INITIALIZATION;
    HTTP_Service_CSRF_Token second = DEFAULT_INITIALIZATION;

    test_expect_true(test, "token_create succeeds", http_service_csrf_token_create(&csrf, &first));
    test_expect_true(test, "a second token_create succeeds", http_service_csrf_token_create(&csrf, &second));
    test_expect_u(test, "the token is 2 * 32 hex characters", 64, string_get_size(&first.value));

    bool lowercase_hex = true;

    for (USize index = 0; index < string_get_size(&first.value); index += 1) {
        char const value = string_get_data(&first.value)[index];

        if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'))) {
            lowercase_hex = false;
        }
    }

    test_expect_true(test, "every character is lowercase hex", lowercase_hex);
    test_expect_false(
        test, "two tokens are never the same value",
        char_compare_equal_2(string_get_data(&first.value), string_get_size(&first.value), string_get_data(&second.value), string_get_size(&second.value)));
    test_expect_false(test, "a fresh token is not already expired", http_service_csrf_token_expired(&first));

    http_service_csrf_token_uninit(&second);
    http_service_csrf_token_uninit(&first);

    test_expect_u(test, "token_uninit clears the expiry", 0, first.expires_at);

    http_service_csrf_uninit(&csrf);

    HTTP_Service_CSRF saturating = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 accepts a USIZE_MAX ttl", http_service_csrf_init_2(&saturating, 32, USIZE_MAX, "csrf", "/", "X-CSRF-Token", "Lax", false, false));

    HTTP_Service_CSRF_Token saturated = DEFAULT_INITIALIZATION;

    test_expect_true(test, "token_create succeeds at a saturating ttl", http_service_csrf_token_create(&saturating, &saturated));
    test_expect_u(test, "expires_at saturates at USIZE_MAX rather than wrapping", USIZE_MAX, saturated.expires_at);
    test_expect_false(test, "a saturated token is not expired (an unclamped wrap would report expired)", http_service_csrf_token_expired(&saturated));

    http_service_csrf_token_uninit(&saturated);
    http_service_csrf_uninit(&saturating);

    test_case_end(test);
}

static void _test_method_table(Test *const test) {
    test_case_begin(test, "method_protected: the four safe methods, compared EXACTLY (RFC 9110 section 9.1 is case-sensitive)");

    test_expect_false(test, "GET is safe", http_service_csrf_method_protected("GET"));
    test_expect_false(test, "HEAD is safe", http_service_csrf_method_protected("HEAD"));
    test_expect_false(test, "OPTIONS is safe", http_service_csrf_method_protected("OPTIONS"));
    test_expect_false(test, "TRACE is safe", http_service_csrf_method_protected("TRACE"));

    test_expect_true(test, "POST is protected", http_service_csrf_method_protected("POST"));
    test_expect_true(test, "PUT is protected", http_service_csrf_method_protected("PUT"));
    test_expect_true(test, "PATCH is protected", http_service_csrf_method_protected("PATCH"));
    test_expect_true(test, "DELETE is protected", http_service_csrf_method_protected("DELETE"));

    /* The old hand-rolled loop upper-cased both sides, so "get" was waved through as safe.
     * A lowercase method is not GET; it is an UNKNOWN method and must be protected. */
    test_expect_true(test, "\"get\" is an unknown method, not a safe one", http_service_csrf_method_protected("get"));
    test_expect_true(test, "\"Head\" is an unknown method, not a safe one", http_service_csrf_method_protected("Head"));
    test_expect_true(test, "an empty method is protected", http_service_csrf_method_protected(""));
    test_expect_true(test, "a null method is protected, and does not abort", http_service_csrf_method_protected(nullptr));

    test_case_end(test);
}

static void _test_cookie_rendering(Test *const test) {
    test_case_begin(test, "cookie_create / cookie_clear emit the exact header VALUE, and the default carries NO HttpOnly");

    HTTP_Service_CSRF csrf = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 with an explicit config", http_service_csrf_init_2(&csrf, 32, 3600, "csrf", "/", "X-CSRF-Token", "Strict", true, false));

    String created = http_service_csrf_cookie_create(&csrf, "abcdef");
    String cleared = http_service_csrf_cookie_clear(&csrf);

    /* No HttpOnly, unlike the session cookie: double submit needs the page's own JavaScript to
     * read this value back out and echo it in the header. */
    test_expect_string(test, "cookie_create renders name=value with Secure but no HttpOnly", "csrf=abcdef; Path=/; Max-Age=3600; Secure; SameSite=Strict", string_get_data(&created));
    test_expect_string(
        test, "cookie_clear mirrors the same flags with Max-Age=0 and an epoch Expires", "csrf=; Path=/; Max-Age=0; Expires=Thu, 01 Jan 1970 00:00:00 GMT; Secure; SameSite=Strict",
        string_get_data(&cleared));

    String empty_token = http_service_csrf_cookie_create(&csrf, "");

    test_expect_u(test, "an empty token is refused, not rendered as a valueless cookie", 0, string_get_size(&empty_token));

    String injected = http_service_csrf_cookie_create(&csrf, "x\r\nSet-Cookie: admin=1");

    test_expect_u(test, "CRLF in the token is refused, never split into a second header", 0, string_get_size(&injected));

    string_uninit(&injected);
    string_uninit(&empty_token);
    string_uninit(&cleared);
    string_uninit(&created);
    http_service_csrf_uninit(&csrf);

    test_case_end(test);
}

static void _test_mint(Test *const test) {
    test_case_begin(test, "mint builds the token and the cookie that carries it, all or nothing");

    HTTP_Service_CSRF csrf = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 with an explicit config", http_service_csrf_init_2(&csrf, 32, 3600, "csrf", "/", "X-CSRF-Token", "Strict", true, false));

    HTTP_Service_CSRF_Token token  = DEFAULT_INITIALIZATION;
    String                  cookie = DEFAULT_INITIALIZATION;

    test_expect_true(test, "mint answers true on a healthy service", http_service_csrf_mint(&csrf, &token, &cookie));
    test_expect_u(test, "the token carries 2 * token_byte_count hex characters", 64, string_get_size(&token.value));
    test_expect_true(test, "and an expiry in the future", token.expires_at > 0 && !http_service_csrf_token_expired(&token));

    /* The cookie must carry THIS token, not a second one issued behind the caller's back: the
     * page echoes the token it was handed, and the gate compares it against the cookie. */
    char expected[128] = DEFAULT_INITIALIZATION;

    snprintf(expected, sizeof(expected), "csrf=%s; Path=/; Max-Age=3600; Secure; SameSite=Strict", string_get_data(&token.value));

    test_expect_string(test, "the cookie renders the token it just issued", expected, string_get_data(&cookie));

    char request_cookie[128] = DEFAULT_INITIALIZATION;

    snprintf(request_cookie, sizeof(request_cookie), "other=1; csrf=%s", string_get_data(&token.value));

    test_expect_true(test,
        "so the pair it produced passes the gate it exists to feed", http_service_csrf_request_allowed_2(&csrf, "POST", string_get_data(&token.value), request_cookie));

    string_uninit(&cookie);
    http_service_csrf_token_uninit(&token);
    http_service_csrf_uninit(&csrf);

    test_case_end(test);
}

static void _test_cookie_read(Test *const test) {
    test_case_begin(test, "cookie_read: found among several, no prefix false-match, absent answers a real \"\", quotes kept");

    HTTP_Service_CSRF csrf = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 with cookie name \"csrf\"", http_service_csrf_init_2(&csrf, 32, 3600, "csrf", "/", "X-CSRF-Token", "Strict", true, false));

    String middle   = http_service_csrf_cookie_read(&csrf, "a=1; csrf=token-value; b=2");
    String prefixed = http_service_csrf_cookie_read(&csrf, "xcsrf=wrong; other=1");
    String absent   = http_service_csrf_cookie_read(&csrf, "a=1; b=2");
    String quoted   = http_service_csrf_cookie_read(&csrf, "csrf=\"abc\"");

    test_expect_string(test, "the configured cookie is found among others", "token-value", string_get_data(&middle));
    test_expect_u(test, "\"xcsrf\" does not false-match \"csrf\"", 0, string_get_size(&prefixed));
    test_expect_u(test, "an absent cookie is empty", 0, string_get_size(&absent));
    test_expect_string(test, "an absent cookie still answers a NUL-terminated \"\", never a null pointer", "", string_get_data(&absent));

    /* RFC 6265 SS4.1.1 allows a DQUOTE-wrapped value and the scanner returns it WITH the quotes,
     * so a quoted echo can never match the unquoted header token. This module's own tokens are
     * hex, so a quoted value is always someone else's cookie - failing closed is correct. */
    test_expect_string(test, "a DQUOTE-wrapped value comes back with its quotes", "\"abc\"", string_get_data(&quoted));
    test_expect_false(test, "and therefore never matches the bare header token", http_service_csrf_token_verify_2(&csrf, "abc", string_get_data(&quoted)));

    string_uninit(&quoted);
    string_uninit(&absent);
    string_uninit(&prefixed);
    string_uninit(&middle);
    http_service_csrf_uninit(&csrf);

    test_case_end(test);
}

static void _test_cookie_read_null_header_and_string_tier(Test *const test) {
    test_case_begin(test, "a NULL Cookie header is the ABSENT case, and cookie_read_4 / token_verify_4 answer what their char* twins answer");

    HTTP_Service_CSRF csrf = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 with cookie name \"csrf\"", http_service_csrf_init_2(&csrf, 32, 3600, "csrf", "/", "X-CSRF-Token", "Strict", true, false));

    /* A request with no Cookie header at all is the ORDINARY shape of a cross-site attempt, not
     * a contract violation. cookie_read used to error_check it - an abort reachable from the
     * wire in a checked build - while the header, and both docs pages, promised otherwise. */
    String null_header = http_service_csrf_cookie_read(&csrf, nullptr);

    test_expect_u(test, "a null Cookie header answers the empty String, never an abort", 0, string_get_size(&null_header));
    test_expect_string(test, "and it is the same NUL-terminated \"\" the absent case answers", "", string_get_data(&null_header));

    String empty_header = http_service_csrf_cookie_read(&csrf, "");

    test_expect_string(test, "an empty Cookie header answers the same", "", string_get_data(&empty_header));

    String header_string = string_init_static("a=1; csrf=token-value; b=2", CHAR_STATIC_SIZE("a=1; csrf=token-value; b=2"));
    String from_string   = http_service_csrf_cookie_read_4(&csrf, &header_string);

    test_expect_string(test, "the String tier reads the same value the char* tier reads", "token-value", string_get_data(&from_string));

    String const empty_string = DEFAULT_INITIALIZATION;
    String       from_empty   = http_service_csrf_cookie_read_4(&csrf, &empty_string);
    String       from_null    = http_service_csrf_cookie_read_4(&csrf, nullptr);

    test_expect_string(test, "an EMPTY String header reads as absent", "", string_get_data(&from_empty));
    test_expect_string(test, "a null String header reads as absent", "", string_get_data(&from_null));

    test_expect_true(test, "token_verify_4 matches what token_verify_2 matches", http_service_csrf_token_verify_4(&csrf, "token-value", &from_string));
    test_expect_false(test, "and refuses a mismatch", http_service_csrf_token_verify_4(&csrf, "zzz999", &from_string));
    test_expect_false(test, "an EMPTY cookie String is refused, never matched against an empty header token", http_service_csrf_token_verify_4(&csrf, "", &from_empty));
    test_expect_false(test, "a null cookie String is refused without aborting", http_service_csrf_token_verify_4(&csrf, "token-value", nullptr));
    test_expect_false(test, "a null header token is refused without aborting", http_service_csrf_token_verify_4(&csrf, nullptr, &from_string));

    string_uninit(&from_null);
    string_uninit(&from_empty);
    string_uninit(&from_string);
    string_uninit(&header_string);
    string_uninit(&empty_header);
    string_uninit(&null_header);
    http_service_csrf_uninit(&csrf);

    test_case_end(test);
}

static void _test_empty_same_site_and_path(Test *const test) {
    test_case_begin(test, "an EMPTY same_site is the documented no-WARN path, and an empty cookie_path omits Path entirely");

    /* "" is not a typo, so it must not WARN the way "bogus" does - but it must render the same
     * degraded cookie. Only "bogus" was pinned before; the documented "" path was not. */
    HTTP_Service_CSRF unset = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 accepts an empty same_site", http_service_csrf_init_2(&unset, 32, 3600, "csrf", "/", "X-CSRF-Token", "", false, false));

    String no_same_site = http_service_csrf_cookie_create(&unset, "tok");

    test_expect_string(test, "an empty same_site omits the attribute, exactly as an unparseable one does", "csrf=tok; Path=/; Max-Age=3600", string_get_data(&no_same_site));

    HTTP_Service_CSRF no_path = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 accepts an empty cookie_path", http_service_csrf_init_2(&no_path, 32, 3600, "csrf", "", "X-CSRF-Token", "Lax", false, false));

    String scoped = http_service_csrf_cookie_create(&no_path, "tok");

    /* An empty path is a legal config VALUE whose String data pointer is null; the service
     * substitutes "" at the read site rather than handing the cookie module a nullptr. */
    test_expect_string(test, "an empty cookie_path omits Path rather than emitting a bare \"Path=\"", "csrf=tok; Max-Age=3600; SameSite=Lax", string_get_data(&scoped));

    String cleared = http_service_csrf_cookie_clear(&no_path);

    test_expect_string(
        test, "and the clear cookie omits it too, so the browser matches the pair", "csrf=; Max-Age=0; Expires=Thu, 01 Jan 1970 00:00:00 GMT; SameSite=Lax", string_get_data(&cleared));

    string_uninit(&cleared);
    string_uninit(&scoped);
    string_uninit(&no_same_site);
    http_service_csrf_uninit(&no_path);
    http_service_csrf_uninit(&unset);

    test_case_end(test);
}

static void _test_double_submit(Test *const test) {
    test_case_begin(test, "double submit: token_verify_2 and request_allowed_2 over a header token and a Cookie header");

    HTTP_Service_CSRF csrf = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 with cookie name \"csrf\"", http_service_csrf_init_2(&csrf, 32, 3600, "csrf", "/", "X-CSRF-Token", "Lax", false, false));

    test_expect_true(test, "identical sides match", http_service_csrf_token_verify_2(&csrf, "abc123", "abc123"));
    test_expect_false(test, "one byte different at the same length does not match", http_service_csrf_token_verify_2(&csrf, "abc123", "abc124"));
    test_expect_false(test, "a different length does not match", http_service_csrf_token_verify_2(&csrf, "abc123", "abc1234"));

    /* Two empties must never compare equal: that would let a request with no header AND no
     * cookie through, which is precisely the cross-site case. */
    test_expect_false(test, "two empty sides do NOT compare equal", http_service_csrf_token_verify_2(&csrf, "", ""));
    test_expect_false(test, "an empty header token is refused", http_service_csrf_token_verify_2(&csrf, "", "abc123"));
    test_expect_false(test, "an empty cookie value is refused", http_service_csrf_token_verify_2(&csrf, "abc123", ""));
    test_expect_false(test, "a null header token is refused without aborting", http_service_csrf_token_verify_2(&csrf, nullptr, "abc123"));
    test_expect_false(test, "a null cookie value is refused without aborting", http_service_csrf_token_verify_2(&csrf, "abc123", nullptr));

    test_expect_true(test, "request_allowed_2 lets a safe method through with no token at all", http_service_csrf_request_allowed_2(&csrf, "GET", nullptr, nullptr));
    test_expect_true(test, "request_allowed_2 accepts a matching header/cookie pair", http_service_csrf_request_allowed_2(&csrf, "POST", "abc123", "other=1; csrf=abc123"));
    test_expect_false(test, "request_allowed_2 rejects a mismatched pair", http_service_csrf_request_allowed_2(&csrf, "POST", "abc123", "csrf=zzz999"));
    test_expect_false(test, "request_allowed_2 rejects a missing header", http_service_csrf_request_allowed_2(&csrf, "POST", "", "csrf=abc123"));
    test_expect_false(test, "request_allowed_2 rejects an absent cookie", http_service_csrf_request_allowed_2(&csrf, "POST", "abc123", "other=1"));
    test_expect_false(test, "request_allowed_2 rejects a null header token without aborting", http_service_csrf_request_allowed_2(&csrf, "POST", nullptr, "csrf=abc123"));
    test_expect_false(test, "request_allowed_2 rejects a null Cookie header without aborting", http_service_csrf_request_allowed_2(&csrf, "POST", "abc123", nullptr));

    /* The header is now tested BEFORE the Cookie header is scanned, so the ordinary cross-site
     * shape (the browser sends the cookie unasked, the attacker page cannot set the header)
     * costs no scan and no allocation. The answer must not change with the order. */
    test_expect_false(test, "a cookie with no header at all is refused before the scan", http_service_csrf_request_allowed_2(&csrf, "POST", nullptr, "csrf=abc123"));
    test_expect_false(test, "an empty header with a present cookie is refused the same way", http_service_csrf_request_allowed_2(&csrf, "POST", "", "csrf=abc123"));
    test_expect_true(test, "and a safe method is still let through before either test", http_service_csrf_request_allowed_2(&csrf, "GET", nullptr, "csrf=abc123"));

    /* R5 Low 3: the char* tier's fast path tested a null Cookie header but not an EMPTY one, so
     * an empty header ran the whole scan and allocated the "" it would then free - work the
     * String tier already skipped. The verdict never changed; the cost did. */
    test_expect_false(test, "an EMPTY Cookie header is refused just as a null one is", http_service_csrf_request_allowed_2(&csrf, "POST", "abc123", ""));
    test_expect_true(test, "and a safe method is still let through with an EMPTY Cookie header", http_service_csrf_request_allowed_2(&csrf, "GET", "abc123", ""));

    http_service_csrf_uninit(&csrf);

    test_case_end(test);
}

static void _test_request_allowed_string_tier(Test *const test) {
    test_case_begin(test, "request_allowed_4 answers what request_allowed_2 answers on every shape, and cookie_read_3 twins session's Str tier");

    HTTP_Service_CSRF csrf = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 with cookie name \"csrf\"", http_service_csrf_init_2(&csrf, 32, 3600, "csrf", "/", "X-CSRF-Token", "Lax", false, false));

    /* The promotion is only worth its signature if the String tier is the SAME verdict: a route
     * that switched to custom_header_get_4 must not start allowing or refusing differently.
     * Every shape below is asserted against the char* answer, not against a hard-coded one. */
    String        token_view     = string_init_static("abc123", CHAR_STATIC_SIZE("abc123"));
    String        other_view     = string_init_static("zzz999", CHAR_STATIC_SIZE("zzz999"));
    String  const empty_view     = DEFAULT_INITIALIZATION;
    String        pair_view      = string_init_static("other=1; csrf=abc123", CHAR_STATIC_SIZE("other=1; csrf=abc123"));
    String        no_cookie_view = string_init_static("other=1", CHAR_STATIC_SIZE("other=1"));

    test_expect_true(test, "a safe method is let through with no token at all", http_service_csrf_request_allowed_4(&csrf, "GET", nullptr, nullptr));
    test_expect_true(test, "a matching header/cookie pair is accepted", http_service_csrf_request_allowed_4(&csrf, "POST", &token_view, &pair_view));
    test_expect_u(test,
        "which is the verdict the char* tier gives the same bytes", (USize) http_service_csrf_request_allowed_2(&csrf, "POST", "abc123", "other=1; csrf=abc123"),
        (USize) http_service_csrf_request_allowed_4(&csrf, "POST", &token_view, &pair_view));
    test_expect_false(test, "a mismatched pair is rejected", http_service_csrf_request_allowed_4(&csrf, "POST", &other_view, &pair_view));
    test_expect_false(test, "an EMPTY header token is rejected before the scan", http_service_csrf_request_allowed_4(&csrf, "POST", &empty_view, &pair_view));
    test_expect_false(test, "a null header token is rejected without aborting", http_service_csrf_request_allowed_4(&csrf, "POST", nullptr, &pair_view));
    test_expect_false(test, "an absent cookie is rejected", http_service_csrf_request_allowed_4(&csrf, "POST", &token_view, &no_cookie_view));
    test_expect_false(test, "an EMPTY Cookie header is rejected", http_service_csrf_request_allowed_4(&csrf, "POST", &token_view, &empty_view));
    test_expect_false(test, "a null Cookie header is rejected without aborting", http_service_csrf_request_allowed_4(&csrf, "POST", &token_view, nullptr));
    test_expect_true(test, "and a safe method is still let through before either test", http_service_csrf_request_allowed_4(&csrf, "GET", nullptr, &pair_view));

    /* R4 Low 6: the header token used to reach verify_2 as a bare char* and be re-measured with
     * char_length, so a String bounded SHORT of its own terminated buffer compared bytes past
     * the size it claims - and "abc123" bounded to 3 matched a "csrf=abc123" cookie. Both sides
     * now go to the sized core, so a short view is a short token and does not match. */
    String bounded_view = string_init_static("abc123", CHAR_STATIC_SIZE("abc123"));
    String bounded_pair = string_init_static("other=1; csrf=abc", CHAR_STATIC_SIZE("other=1; csrf=abc"));

    /* string_set_size does not terminate - it lowers the claim over a buffer that still reads
     * "abc123" to a NUL. That is the shape the defect needed: char_length says 6, the String
     * says 3. string_init_static alone cannot build it, because it terminates what it copies. */
    string_set_size(&bounded_view, 3);

    test_expect_false(test, "a header token bounded short of its buffer no longer matches the longer cookie", http_service_csrf_request_allowed_4(&csrf, "POST", &bounded_view, &pair_view));
    test_expect_true(test, "and it matches the cookie that carries exactly the bytes it claims", http_service_csrf_request_allowed_4(&csrf, "POST", &bounded_view, &bounded_pair));
    test_expect_u(test,
        "which is the verdict the char* tier gives those same three bytes", (USize) http_service_csrf_request_allowed_2(&csrf, "POST", "abc", "other=1; csrf=abc"),
        (USize) http_service_csrf_request_allowed_4(&csrf, "POST", &bounded_view, &bounded_pair));

    /* The same rule under token_verify_4: the cookie side is read through the String's size. */
    String bounded_cookie = string_init_static("abc123", CHAR_STATIC_SIZE("abc123"));

    string_set_size(&bounded_cookie, 3);

    test_expect_false(test, "token_verify_4 does not match a full token against a short cookie view", http_service_csrf_token_verify_4(&csrf, "abc123", &bounded_cookie));
    test_expect_true(test, "token_verify_4 matches the bytes the cookie view actually claims", http_service_csrf_token_verify_4(&csrf, "abc", &bounded_cookie));
    test_expect_false(test, "token_verify_4 refuses a null candidate token without aborting", http_service_csrf_token_verify_4(&csrf, nullptr, &bounded_cookie));

    string_uninit(&bounded_cookie);
    string_uninit(&bounded_pair);
    string_uninit(&bounded_view);

    /* cookie_read_3: the Str tier session has carried since 0.3.0, now this service's too, so a
     * caller holding an unterminated view reads the same value out of either module. */
    Str     header_str = str_init_static("other=1; csrf=abc123; last=2", CHAR_STATIC_SIZE("other=1; csrf=abc123; last=2"));
    String  from_str   = http_service_csrf_cookie_read_3(&csrf, &header_str);

    test_expect_string(test, "the Str tier reads the same value the char* tier reads", "abc123", string_get_data(&from_str));

    Str     short_str  = str_init_static("other=1; csrf=abc123; last=2", CHAR_STATIC_SIZE("other=1;"));
    String  short_read = http_service_csrf_cookie_read_3(&csrf, &short_str);
    String  from_null  = http_service_csrf_cookie_read_3(&csrf, nullptr);

    test_expect_u(test, "a Str bounded before the cookie answers a miss, not the bytes past its end", 0, string_get_size(&short_read));
    test_expect_string(test, "a null Str header reads as absent", "", string_get_data(&from_null));

    string_uninit(&from_null);
    string_uninit(&short_read);
    str_uninit(&short_str);
    string_uninit(&from_str);
    str_uninit(&header_str);
    string_uninit(&no_cookie_view);
    string_uninit(&pair_view);
    string_uninit(&other_view);
    string_uninit(&token_view);
    http_service_csrf_uninit(&csrf);

    test_case_end(test);
}

static void _test_synchronizer(Test *const test) {
    test_case_begin(test, "synchronizer: token_verify_1 and request_allowed_1 against a server-held token, expiry included");

    HTTP_Service_CSRF csrf = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 with a one-hour ttl", http_service_csrf_init_2(&csrf, 32, 3600, "csrf", "/", "X-CSRF-Token", "Lax", false, false));

    HTTP_Service_CSRF_Token stored = DEFAULT_INITIALIZATION;

    test_expect_true(test, "token_create succeeds", http_service_csrf_token_create(&csrf, &stored));

    char token[128] = DEFAULT_INITIALIZATION;

    char_copy_3(token, sizeof(token), string_get_data(&stored.value), string_get_size(&stored.value));

    token[string_get_size(&stored.value)] = '\0';

    test_expect_true(test, "the minted token verifies against itself", http_service_csrf_token_verify_1(&csrf, token, &stored));
    test_expect_true(test, "request_allowed lets a safe method through with no token", http_service_csrf_request_allowed_1(&csrf, "GET", nullptr, nullptr));
    test_expect_true(test, "request_allowed accepts the matching token on POST", http_service_csrf_request_allowed_1(&csrf, "POST", token, &stored));

    /* csrf.c used to error_check_null the request token AFTER the method gate, so whether a
     * missing X-CSRF-Token header aborted the whole server depended on the request's METHOD. */
    test_expect_false(test, "a null request token on a protected method answers false, never aborts", http_service_csrf_request_allowed_1(&csrf, "POST", nullptr, &stored));
    test_expect_false(test, "a null stored token on a protected method answers false, never aborts", http_service_csrf_request_allowed_1(&csrf, "POST", token, nullptr));
    test_expect_false(test, "an empty request token answers false", http_service_csrf_request_allowed_1(&csrf, "POST", "", &stored));

    HTTP_Service_CSRF_Token empty_stored = DEFAULT_INITIALIZATION;

    test_expect_false(test, "an EMPTY stored token answers false (a CSPRNG failure fails closed)", http_service_csrf_token_verify_1(&csrf, token, &empty_stored));

    HTTP_Service_CSRF_Token expired = DEFAULT_INITIALIZATION;

    expired.value       = string_init_static(token, char_length(token));
    expired.expires_at  = 1;

    test_expect_true(test, "a token whose expiry is in the past reads as expired", http_service_csrf_token_expired(&expired));
    test_expect_false(test, "and an expired stored token never verifies", http_service_csrf_token_verify_1(&csrf, token, &expired));

    http_service_csrf_token_uninit(&expired);
    http_service_csrf_token_uninit(&stored);
    http_service_csrf_uninit(&csrf);

    test_case_end(test);
}

static void _test_config_refusals(Test *const test) {
    test_case_begin(test, "init refuses whole on every config a browser would silently drop, and on an unusable header name");

    HTTP_Service_CSRF service = DEFAULT_INITIALIZATION;

    /* `__Host-csrf` is the DEFAULT name, and a development "Secure off for localhost" toggle is
     * exactly what meets it: the browser drops the cookie, the header still arrives, and the
     * double-submit check compares against a cookie that never comes. */
    test_expect_false(test, "`__Host-` without Secure is refused", http_service_csrf_init_2(&service, 32, 3600, "__Host-csrf", "/", "X-CSRF-Token", "Strict", false, false));
    test_expect_u(test, "and *self is left zeroed", 0, service.ttl);

    test_expect_false(test, "`__Host-` with a path other than \"/\" is refused", http_service_csrf_init_2(&service, 32, 3600, "__Host-csrf", "/app", "X-CSRF-Token", "Strict", true, false));
    test_expect_false(test, "`__Secure-` without Secure is refused", http_service_csrf_init_2(&service, 32, 3600, "__Secure-csrf", "/", "X-CSRF-Token", "Strict", false, false));
    test_expect_false(test, "SameSite=None without Secure is refused", http_service_csrf_init_2(&service, 32, 3600, "csrf", "/", "X-CSRF-Token", "None", false, false));
    test_expect_false(test, "an empty cookie name is refused", http_service_csrf_init_2(&service, 32, 3600, "", "/", "X-CSRF-Token", "Strict", true, false));
    test_expect_false(test, "a cookie name that is not an RFC 6265 token is refused", http_service_csrf_init_2(&service, 32, 3600, "bad name", "/", "X-CSRF-Token", "Strict", true, false));
    test_expect_false(test, "a ';' in the cookie path is refused", http_service_csrf_init_2(&service, 32, 3600, "csrf", "/a;b", "X-CSRF-Token", "Strict", true, false));

    /* header_name used to be stored and never read by anything - a config the module ignored.
     * It now has a getter and a job, so an empty one is a service no request could satisfy. */
    test_expect_false(test, "an empty header_name is refused", http_service_csrf_init_2(&service, 32, 3600, "csrf", "/", "", "Strict", true, false));

    test_expect_false(test, "a token_byte_count below the 16-byte floor is refused", http_service_csrf_init_2(&service, 4, 3600, "csrf", "/", "X-CSRF-Token", "Strict", true, false));
    test_expect_false(
        test, "a ttl of 0 is refused (a cookie with Max-Age=0 expires on arrival)",
        http_service_csrf_init_2(&service, 32, 0, "csrf", "/", "X-CSRF-Token", "Strict", true, false));

    test_expect_true(test, "exactly the floor is accepted", http_service_csrf_init_2(&service, 16, 3600, "csrf", "/", "X-CSRF-Token", "Strict", true, false));
    test_expect_string(test, "cookie_name_get reads back the configured name", "csrf", http_service_csrf_cookie_name_get(&service));
    test_expect_string(test, "header_name_get reads back the configured header", "X-CSRF-Token", http_service_csrf_header_name_get(&service));

    http_service_csrf_uninit(&service);

    /* An unparseable same_site is a WARN, not a refusal: the cookie is degraded, not dropped. */
    HTTP_Service_CSRF bogus = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 accepts an unparseable same_site with a WARN", http_service_csrf_init_2(&bogus, 32, 3600, "csrf", "/", "X-CSRF-Token", "bogus", false, false));

    String degraded = http_service_csrf_cookie_create(&bogus, "tok");

    test_expect_string(test, "an unparseable same_site omits the attribute entirely", "csrf=tok; Path=/; Max-Age=3600", string_get_data(&degraded));

    string_uninit(&degraded);
    http_service_csrf_uninit(&bogus);

    test_case_end(test);
}

static void _test_config_is_copied(Test *const test) {
    test_case_begin(test, "config strings are copied at init: stack buffers overwritten afterwards do not change the cookie");

    HTTP_Service_CSRF csrf = DEFAULT_INITIALIZATION;

    char name[16]   = DEFAULT_INITIALIZATION;
    char header[24] = DEFAULT_INITIALIZATION;

    char_copy_3(name, sizeof(name), "csrf", 4);
    char_copy_3(header, sizeof(header), "X-CSRF-Token", 12);

    test_expect_true(test, "init_2 from stack buffers", http_service_csrf_init_2(&csrf, 32, 3600, name, "/", header, "Lax", false, false));

    char_copy_3(name, sizeof(name), "zzzz", 4);
    char_copy_3(header, sizeof(header), "X-Wrong-Name", 12);

    String cookie = http_service_csrf_cookie_create(&csrf, "tok");

    test_expect_string(test, "the cookie still uses the name as it was at init", "csrf=tok; Path=/; Max-Age=3600; SameSite=Lax", string_get_data(&cookie));
    test_expect_string(test, "and header_name_get still answers the copied header name", "X-CSRF-Token", http_service_csrf_header_name_get(&csrf));

    string_uninit(&cookie);
    http_service_csrf_uninit(&csrf);

    test_case_end(test);
}

static void _test_arena_tier(Test *const test) {
    test_case_begin(test, "the arena tier mirrors the heap tier, and a refused arena refuses the whole service");

    Arena arena = arena_init_1(65536, ARENA_TYPE_LINEAR);

    HTTP_Service_CSRF csrf = DEFAULT_INITIALIZATION;

    test_expect_true(test, "alloc_init_2 succeeds on a healthy arena", http_service_csrf_alloc_init_2(&csrf, 32, 3600, "csrf", "/", "X-CSRF-Token", "Strict", true, false, &arena));

    String cookie = http_service_csrf_cookie_create(&csrf, "abcdef");

    test_expect_string(test, "an arena-backed cookie renders identically to a heap-backed one", "csrf=abcdef; Path=/; Max-Age=3600; Secure; SameSite=Strict", string_get_data(&cookie));

    HTTP_Service_CSRF_Token token = DEFAULT_INITIALIZATION;

    test_expect_true(test, "arena-backed token_create succeeds", http_service_csrf_token_create(&csrf, &token));
    test_expect_u(test, "and produces the same 64 hex characters", 64, string_get_size(&token.value));

    http_service_csrf_token_uninit(&token);
    string_uninit(&cookie);
    http_service_csrf_uninit(&csrf);
    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    /* A genuinely REJECTED arena handle (byte_size 0), the shape tests/http/cookie already uses:
     * every copy comes back EMPTY, so the cookie module reads the empty name as "not a valid
     * token". A merely SMALL arena is not the same experiment - arena_linear_alloc aborts on a
     * capacity overrun rather than answering null. */
    Arena starved = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    HTTP_Service_CSRF refused = DEFAULT_INITIALIZATION;

    test_expect_false(test, "alloc_init_2 refuses whole on a refused arena", http_service_csrf_alloc_init_2(&refused, 32, 3600, "csrf", "/", "X-CSRF-Token", "Strict", true, false, &starved));
    test_expect_u(test, "and leaves *self zeroed", 0, refused.token_byte_count);

    arena_uninit(&starved, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Live server
 *============================================================================*/
static HTTP_Service_CSRF _live_csrf;

static void _route_issue(HTTP_Server_Route *const route) {
    HTTP_Server_Holder *const holder = http_server_route_get_holder(route);

    HTTP_Service_CSRF_Token token = DEFAULT_INITIALIZATION;

    if (!http_service_csrf_token_create(&_live_csrf, &token)) {
        http_server_response_send_1(holder->response, "mint failed", HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_INTERNAL_SERVER_ERROR);

        return;
    }

    String cookie = http_service_csrf_cookie_create(&_live_csrf, string_get_data(&token.value));

    http_server_response_header_add(holder->response, "Set-Cookie", string_get_data(&cookie));
    http_server_response_send_1(holder->response, string_get_data(&token.value), HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_OK);

    string_uninit(&cookie);
    http_service_csrf_token_uninit(&token);
}

static void _route_submit(HTTP_Server_Route *const route) {
    HTTP_Server_Holder *const holder = http_server_route_get_holder(route);

    char cookie_header[1024]    = DEFAULT_INITIALIZATION;
    char header_token[256]      = DEFAULT_INITIALIZATION;

    /* Cookie is an lws-RECOGNIZED header and is readable only through header_copy; the CSRF
     * header is custom and is invisible to that call, so it needs custom_header_copy - and the
     * name comes from the service's own configuration, not a hard-coded literal. */
    http_server_request_header_copy(holder->request, HTTP_SERVER_HEADER_COOKIE, cookie_header, sizeof(cookie_header));
    http_server_request_custom_header_copy(holder->request, http_service_csrf_header_name_get(&_live_csrf), header_token, sizeof(header_token));

    if (!http_service_csrf_request_allowed_2(&_live_csrf, http_server_request_get_method_1(holder->request), header_token, cookie_header)) {
        http_server_response_send_1(holder->response, "forbidden", HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_FORBIDDEN);

        return;
    }

    http_server_response_send_1(holder->response, "accepted", HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_OK);
}

static void _handler(void *context, HTTP_Server_Request *request, HTTP_Server_Response *response) {
    HTTP_Server         *const  server  = (HTTP_Server*) context;
    HTTP_Server_Holder          holder  = { .request = request, .response = response, .arena = nullptr };

    if (!http_server_router_dispatch_2(server->router, http_server_request_get_path_1(request), http_server_request_get_path_size(request), &holder)) {
        http_server_response_send_1(response, "no route", HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_NOT_FOUND);
    }
}

static U16 _start(Test *const test, HTTP_Server *const server) {
    http_server_set_handler(server, _handler, server);

    Result const result = http_server_run(server, 0, true);

    test_expect_true(test, "http_server_run succeeds on port 0", result_is_success(result));

    U16 const port = http_server_get_port(server);

    test_expect_true(test, "an ephemeral port was assigned", port != 0);

    return port;
}

static void _test_live_double_submit(Test *const test) {
    test_case_begin(test, "live: GET issues the cookie, POST echoing it in the header is 200, every other way is 403");

    /* Secure is OFF here on purpose: the client is a plain HTTP socket, and the cookie name is a
     * plain "csrf" precisely because `__Host-` would be refused without Secure. */
    test_expect_true(test, "init_2 for the live server", http_service_csrf_init_2(&_live_csrf, 32, 3600, "csrf", "/", "X-CSRF-Token", "Lax", false, false));

    HTTP_Server *server = http_server_new();

    test_expect_true(test, "route_add /issue", http_server_route_add(server, "/issue", _route_issue));
    test_expect_true(test, "route_add /submit", http_server_route_add(server, "/submit", _route_submit));

    U16 const port = _start(test, server);

    _Reply  reply           = DEFAULT_INITIALIZATION;
    char    set_cookie[512] = DEFAULT_INITIALIZATION;
    char    token[256]      = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "GET /issue round trip", _client_round_trip(port, "GET /issue HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "/issue answers 200", 200, reply.status);
        test_expect_true(test, "/issue sends a Set-Cookie header", _reply_header(&reply, "set-cookie:", set_cookie, sizeof(set_cookie)));
        test_expect_string_contains(test, "the cookie carries no HttpOnly, so the page's script can read it", set_cookie, "; Path=/; Max-Age=3600; SameSite=Lax");
        test_expect_false(test, "and really has no HttpOnly flag", char_find_exists_1(set_cookie, "HttpOnly"));
        test_expect_u(test, "the body is the token itself", 64, reply.body_size);

        // The length above is an assertion, not a guard: a body the buffer cannot hold is
        // clamped here so an oversize reply fails the pins instead of writing past `token`.
        USize const token_size = math_min_u(reply.body_size, sizeof(token) - 1);

        char_copy_3(token, sizeof(token), reply.body, token_size);

        token[token_size] = '\0';
    }

    char request[1024] = DEFAULT_INITIALIZATION;

    snprintf(request, sizeof(request), "POST /submit HTTP/1.1\r\nHost: t\r\nContent-Length: 0\r\nCookie: csrf=%s\r\nX-CSRF-Token: %s\r\nConnection: close\r\n\r\n", token, token);

    if (_client_round_trip(port, request, &reply)) {
        test_expect_u(test, "POST with a matching cookie and header answers 200", 200, reply.status);
        test_expect_string(test, "and is accepted", "accepted", reply.body);
    }

    snprintf(request, sizeof(request), "POST /submit HTTP/1.1\r\nHost: t\r\nContent-Length: 0\r\nCookie: csrf=%s\r\nConnection: close\r\n\r\n", token);

    if (_client_round_trip(port, request, &reply)) {
        test_expect_u(test, "POST with the cookie but no header answers 403 - the cross-site case", 403, reply.status);
    }

    snprintf(request, sizeof(request), "POST /submit HTTP/1.1\r\nHost: t\r\nContent-Length: 0\r\nX-CSRF-Token: %s\r\nConnection: close\r\n\r\n", token);

    if (_client_round_trip(port, request, &reply)) {
        test_expect_u(test, "POST with the header but no cookie answers 403", 403, reply.status);
    }

    if (_client_round_trip(port, "POST /submit HTTP/1.1\r\nHost: t\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", &reply)) {
        test_expect_u(test, "POST with neither answers 403 - two empties never compare equal", 403, reply.status);
    }

    snprintf(request, sizeof(request), "POST /submit HTTP/1.1\r\nHost: t\r\nContent-Length: 0\r\nCookie: csrf=%s\r\nX-CSRF-Token: 0123456789\r\nConnection: close\r\n\r\n", token);

    if (_client_round_trip(port, request, &reply)) {
        test_expect_u(test, "POST with a header of the wrong length answers 403", 403, reply.status);
    }

    char tampered[256] = DEFAULT_INITIALIZATION;

    char_copy_3(tampered, sizeof(tampered), token, char_length(token));

    tampered[char_length(token)] = '\0';
    tampered[0] = tampered[0] == 'a' ? 'b' : 'a';

    snprintf(request, sizeof(request), "POST /submit HTTP/1.1\r\nHost: t\r\nContent-Length: 0\r\nCookie: csrf=%s\r\nX-CSRF-Token: %s\r\nConnection: close\r\n\r\n", token, tampered);

    if (_client_round_trip(port, request, &reply)) {
        test_expect_u(test, "POST with one byte of the header token flipped answers 403", 403, reply.status);
    }

    if (_client_round_trip(port, "GET /submit HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply)) {
        test_expect_u(test, "a GET carrying no token at all is a safe method and answers 200", 200, reply.status);
    }

    http_server_delete(&server);
    http_service_csrf_uninit(&_live_csrf);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Entry point
 *============================================================================*/
I32 main(void) {
    LogConfig const log_config = { .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true };

    log_init(log_config);

    http_server_set_log_level(LLL_ERR);

    Test test = test_init("http_service_csrf");

    test_suite_begin(&test, "http_service_csrf offline");

    _test_token_shape(&test);
    _test_method_table(&test);
    _test_cookie_rendering(&test);
    _test_mint(&test);
    _test_cookie_read(&test);
    _test_cookie_read_null_header_and_string_tier(&test);
    _test_empty_same_site_and_path(&test);
    _test_double_submit(&test);
    _test_request_allowed_string_tier(&test);
    _test_synchronizer(&test);
    _test_config_refusals(&test);
    _test_config_is_copied(&test);
    _test_arena_tier(&test);

    test_suite_end(&test);

    test_suite_begin(&test, "http_service_csrf live");

    _test_live_double_submit(&test);

    test_suite_end(&test);

    return test_uninit(&test);
}