/*
 * test_all.c - the http/service/session suite.
 *
 * Offline cases pin the codec: token shape and uniqueness, the SHA-256 hash against a known
 * vector, the exact cookie header VALUE both builders emit, the Cookie-header scanner, and
 * every constructor refusal (a config a browser would silently drop is a service that must not
 * exist). Live cases drive a REAL server - http_server_run on port 0, the ephemeral port read
 * back with http_server_get_port, and a raw `net` socket for the client, so the bytes on the
 * wire are under the test's own control. Modelled on tests/http/server/test_all.c.
 *
 * test_unchecked.c holds the half this build cannot observe: every refusal above is an ordinary
 * runtime branch, not an artifact of ERROR_CHECK_ENABLED.
 */
#include <stdarg.h>
#include <stdio.h>

#include <allocator/allocator.h>
#include <arena/arena.h>
#include <http/server/http_server.h>
#include <http/service/session/session.h>
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
#define _UNIQUE_MINT_ROUNDS     1000

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
    test_case_begin(test, "token_create: 2 * token_byte_count lowercase hex, a fresh value every time");

    HTTP_Service_Session session = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_1 accepts the defaults", http_service_session_init_1(&session));

    HTTP_Service_Session_Token token = DEFAULT_INITIALIZATION;

    test_expect_true(test, "token_create succeeds", http_service_session_token_create(&session, &token));
    test_expect_u(test, "the token is 2 * 32 hex characters", 64, string_get_size(&token.value));

    bool lowercase_hex = true;

    for (USize index = 0; index < string_get_size(&token.value); index += 1) {
        char const value = string_get_data(&token.value)[index];

        if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'))) {
            lowercase_hex = false;
        }
    }

    test_expect_true(test, "every character is lowercase hex", lowercase_hex);
    test_expect_false(test, "a fresh token is not already expired", http_service_session_token_expired(&token));

    /* 1000 mints, each compared against the previous one: a generator stuck on one value (or on
     * a zeroed buffer) shows up immediately, and the pairwise walk needs no O(n^2) table. */
    String    previous  = string_init_static(string_get_data(&token.value), string_get_size(&token.value));
    USize     duplicates = 0;

    for (USize round = 0; round < _UNIQUE_MINT_ROUNDS; round += 1) {
        HTTP_Service_Session_Token next = DEFAULT_INITIALIZATION;

        if (!http_service_session_token_create(&session, &next)) {
            duplicates += 1;

            continue;
        }

        if (char_compare_equal_2(string_get_data(&next.value), string_get_size(&next.value), string_get_data(&previous), string_get_size(&previous))) {
            duplicates += 1;
        }

        string_uninit(&previous);

        previous = string_init_static(string_get_data(&next.value), string_get_size(&next.value));

        http_service_session_token_uninit(&next);
    }

    test_expect_u(test, "1000 consecutive mints never repeat", 0, duplicates);

    string_uninit(&previous);
    http_service_session_token_uninit(&token);

    test_expect_u(test, "token_uninit clears the expiry", 0, token.expires_at);

    http_service_session_uninit(&session);

    test_case_end(test);
}

static void _test_token_hash_and_verify(Test *const test) {
    test_case_begin(test, "token_hash: the published SHA-256 vector; verify_1/_4 fail closed on every empty");

    HTTP_Service_Session session = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_1 accepts the defaults", http_service_session_init_1(&session));

    String hash = http_service_session_token_hash(&session, "abc");

    /* FIPS 180-4's own worked example: SHA-256("abc"). A hash function that silently became a
     * different one still produces 64 hex characters, so the LENGTH proves nothing. */
    test_expect_string(test, "SHA-256(\"abc\") matches the published digest", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", string_get_data(&hash));

    test_expect_true(test, "verify_1 accepts the token that produced the hash", http_service_session_token_verify_1(&session, "abc", string_get_data(&hash)));
    test_expect_false(test, "verify_1 rejects a different token", http_service_session_token_verify_1(&session, "abd", string_get_data(&hash)));
    test_expect_false(test, "verify_1 rejects an empty hash (a NULL database cell read back as \"\")", http_service_session_token_verify_1(&session, "abc", ""));
    test_expect_false(test, "verify_1 rejects an empty token (an absent cookie)", http_service_session_token_verify_1(&session, "", string_get_data(&hash)));
    test_expect_false(test, "verify_1 rejects a null hash without aborting", http_service_session_token_verify_1(&session, "abc", nullptr));
    test_expect_false(test, "verify_1 rejects a null token without aborting", http_service_session_token_verify_1(&session, nullptr, string_get_data(&hash)));

    test_expect_true(test, "verify_4 accepts the same pair through the String tier", http_service_session_token_verify_4(&session, "abc", &hash));

    String empty = string_init_1();

    test_expect_false(test, "verify_4 rejects an EMPTY String hash", http_service_session_token_verify_4(&session, "abc", &empty));
    test_expect_false(test, "verify_4 rejects a null String hash without aborting", http_service_session_token_verify_4(&session, "abc", nullptr));

    /* R5 Mid 2: verify_4 used to hand the stored hash on to verify_1 as a bare char*, which
     * re-measured it with char_length - so a String bounded SHORT of its own terminated buffer
     * compared bytes past the size it claims. string_set_size does not terminate: it lowers the
     * claim over a buffer that still reads on to a NUL, which is exactly the shape the defect
     * needed. string_init_static alone cannot build it, because it terminates what it copies. */
    String bounded_hash = string_init_static(
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad0000",
        CHAR_STATIC_SIZE("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad0000"));

    string_set_size(&bounded_hash, ENCODING_HEX_SIZE(CRYPTO_HASH_SHA256_SIZE));

    test_expect_true(test, "verify_4 answers on the String's OWN size, not on char_length of its buffer", http_service_session_token_verify_4(&session, "abc", &bounded_hash));
    test_expect_false(test,
        "and the char* spelling of that same buffer - four bytes longer than the String claims - does not verify",
        http_service_session_token_verify_1(&session, "abc", string_get_data(&bounded_hash)));
    test_expect_false(test, "verify_4 rejects a null candidate token without aborting", http_service_session_token_verify_4(&session, nullptr, &bounded_hash));
    test_expect_false(test, "verify_4 rejects an empty candidate token", http_service_session_token_verify_4(&session, "", &bounded_hash));

    string_uninit(&bounded_hash);
    string_uninit(&empty);
    string_uninit(&hash);
    http_service_session_uninit(&session);

    test_case_end(test);
}

static void _test_expiry(Test *const test) {
    test_case_begin(test, "expires_at is now + ttl, and a huge ttl clamps instead of wrapping into the past");

    HTTP_Service_Session session = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 with a 60-second ttl", http_service_session_init_2(&session, 32, 60, "sid", "/", "Lax", false, true));

    HTTP_Service_Session_Token token = DEFAULT_INITIALIZATION;

    test_expect_true(test, "token_create succeeds", http_service_session_token_create(&session, &token));

    USize const now         = (USize) datetime_now();
    USize const difference  = token.expires_at > now + 60 ? token.expires_at - (now + 60) : (now + 60) - token.expires_at;

    test_expect_true(test, "expires_at is now + ttl within a second", difference <= 1);

    http_service_session_token_uninit(&token);
    http_service_session_uninit(&session);

    HTTP_Service_Session saturating = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 accepts a USIZE_MAX ttl", http_service_session_init_2(&saturating, 32, USIZE_MAX, "sid", "/", "Lax", false, true));

    HTTP_Service_Session_Token saturated = DEFAULT_INITIALIZATION;

    test_expect_true(test, "token_create succeeds at a saturating ttl", http_service_session_token_create(&saturating, &saturated));
    test_expect_u(test, "expires_at saturates at USIZE_MAX rather than wrapping", USIZE_MAX, saturated.expires_at);
    test_expect_false(test, "a saturated token is not expired (an unclamped wrap would report expired)", http_service_session_token_expired(&saturated));

    http_service_session_token_uninit(&saturated);
    http_service_session_uninit(&saturating);

    /* Every other case above asserts NOT-expired, so the true branch was never executed: a
     * token_expired stuck at false would have passed the whole suite. Hand-build the token
     * rather than sleep - the field is public and a ttl of 1 would cost a second per run.
     * (tests/http/service/csrf pins its twin the same way.) */
    HTTP_Service_Session_Token const expired = { .expires_at = 1, .value = DEFAULT_INITIALIZATION };

    test_expect_true(test, "a token whose expires_at is in the past IS expired", http_service_session_token_expired(&expired));

    HTTP_Service_Session_Token const boundary = { .expires_at = (USize) datetime_now(), .value = DEFAULT_INITIALIZATION };

    test_expect_true(test, "expiry is inclusive: expires_at == now already counts as expired", http_service_session_token_expired(&boundary));

    test_case_end(test);
}

static void _test_cookie_rendering(Test *const test) {
    test_case_begin(test, "cookie_create / cookie_clear emit the exact header VALUE, with no \"Set-Cookie: \" and no CRLF");

    HTTP_Service_Session session = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 with an explicit config", http_service_session_init_2(&session, 32, 600, "sid", "/", "Strict", true, true));

    String created = http_service_session_cookie_create(&session, "abcdef");
    String cleared = http_service_session_cookie_clear(&session);

    test_expect_string(
        test, "cookie_create renders name=value plus every configured attribute", "sid=abcdef; Path=/; Max-Age=600; Secure; HttpOnly; SameSite=Strict",
        string_get_data(&created));
    test_expect_string(
        test, "cookie_clear mirrors the same flags with Max-Age=0 and an epoch Expires", "sid=; Path=/; Max-Age=0; Expires=Thu, 01 Jan 1970 00:00:00 GMT; Secure; HttpOnly; SameSite=Strict",
        string_get_data(&cleared));

    string_uninit(&created);
    string_uninit(&cleared);
    http_service_session_uninit(&session);

    /* An unparseable same_site is a WARN, not a refusal: the cookie is degraded (no SameSite),
     * not dropped. A lowercase spelling parses, because browsers match it case-insensitively. */
    HTTP_Service_Session bogus = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 accepts an unparseable same_site with a WARN", http_service_session_init_2(&bogus, 32, 600, "sid", "/", "bogus", false, true));

    String degraded = http_service_session_cookie_create(&bogus, "tok");

    test_expect_string(test, "an unparseable same_site omits the attribute entirely", "sid=tok; Path=/; Max-Age=600; HttpOnly", string_get_data(&degraded));

    string_uninit(&degraded);
    http_service_session_uninit(&bogus);

    HTTP_Service_Session lowercase = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 accepts \"lax\"", http_service_session_init_2(&lowercase, 32, 600, "sid", "/", "lax", false, true));

    String lax = http_service_session_cookie_create(&lowercase, "tok");

    test_expect_string(test, "\"lax\" is parsed case-insensitively and rendered canonically", "sid=tok; Path=/; Max-Age=600; HttpOnly; SameSite=Lax", string_get_data(&lax));

    string_uninit(&lax);
    http_service_session_uninit(&lowercase);

    test_case_end(test);
}

static void _test_cookie_refusals(Test *const test) {
    test_case_begin(test, "cookie_create refuses an empty token and a token the cookie module would not carry");

    HTTP_Service_Session session = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 with an explicit config", http_service_session_init_2(&session, 32, 600, "sid", "/", "Strict", true, true));

    String empty_token = http_service_session_cookie_create(&session, "");

    /* An unchecked token_create failure must not become "sid=; Max-Age=600" on the wire - a real
     * cookie with no session in it, which locks the user out silently instead of loudly. */
    test_expect_u(test, "an empty token is refused, not rendered as a valueless cookie", 0, string_get_size(&empty_token));

    String injected = http_service_session_cookie_create(&session, "x\r\nSet-Cookie: admin=1");

    test_expect_u(test, "CRLF in the token is refused by the cookie module, never split into a second header", 0, string_get_size(&injected));

    string_uninit(&empty_token);
    string_uninit(&injected);
    http_service_session_uninit(&session);

    test_case_end(test);
}

static void _test_cookie_read(Test *const test) {
    test_case_begin(test, "cookie_read: found among several, no prefix false-match, absent answers a real \"\"");

    HTTP_Service_Session session = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 with cookie name \"sid\"", http_service_session_init_2(&session, 32, 600, "sid", "/", "Strict", true, true));

    String middle   = http_service_session_cookie_read(&session, "a=1; sid=token-value; b=2");
    String prefixed = http_service_session_cookie_read(&session, "xsid=wrong; other=1");
    String absent   = http_service_session_cookie_read(&session, "a=1; b=2");

    test_expect_string(test, "the configured cookie is found among others", "token-value", string_get_data(&middle));

    /* main_traymon.c's char_find_exists_1(cookie, "sid=") substring test matches "xsid=" and
     * hands the wrong value to the hash. The scanner must not. */
    test_expect_u(test, "\"xsid\" does not false-match \"sid\" (the substring-test bug)", 0, string_get_size(&prefixed));
    test_expect_u(test, "an absent cookie is empty", 0, string_get_size(&absent));
    test_expect_string(test, "an absent cookie still answers a NUL-terminated \"\", never a null pointer", "", string_get_data(&absent));

    string_uninit(&middle);
    string_uninit(&prefixed);
    string_uninit(&absent);
    http_service_session_uninit(&session);

    test_case_end(test);
}

static void _test_cookie_read_null_header_and_tiers(Test *const test) {
    test_case_begin(test, "a NULL Cookie header is the ABSENT case in every tier, and the Str/String tiers answer what the char* tier answers");

    HTTP_Service_Session session = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 with cookie name \"sid\"", http_service_session_init_2(&session, 32, 600, "sid", "/", "Strict", true, true));

    /* A request that carries no Cookie header at all is an ordinary unauthenticated request,
     * not a contract violation. cookie_read used to error_check it - which ABORTS the server
     * pre-auth in a checked build - while the header, and both docs pages, promised otherwise. */
    String null_header = http_service_session_cookie_read(&session, nullptr);

    test_expect_u(test, "a null Cookie header answers the empty String, never an abort", 0, string_get_size(&null_header));
    test_expect_string(test, "and it is the same NUL-terminated \"\" the absent case answers", "", string_get_data(&null_header));

    String empty_header = http_service_session_cookie_read(&session, "");

    test_expect_u(test, "an empty Cookie header answers the same", 0, string_get_size(&empty_header));
    test_expect_string(test, "terminated, never nullptr-backed", "", string_get_data(&empty_header));

    /* cookie_hash's own null path leads here; it must stay false rather than reach an abort. */
    String hash = DEFAULT_INITIALIZATION;

    test_expect_false(test, "cookie_hash on a null header is false, not an abort", http_service_session_cookie_hash(&session, nullptr, &hash));

    Str         header_str    = str_init_static("a=1; sid=token-value; b=2", CHAR_STATIC_SIZE("a=1; sid=token-value; b=2"));
    String      from_str      = http_service_session_cookie_read_3(&session, &header_str);
    String      header_string = string_init_static("a=1; sid=token-value; b=2", CHAR_STATIC_SIZE("a=1; sid=token-value; b=2"));
    String      from_string   = http_service_session_cookie_read_4(&session, &header_string);

    test_expect_string(test, "the Str tier reads the same value the char* tier reads", "token-value", string_get_data(&from_str));
    test_expect_string(test, "and so does the String tier", "token-value", string_get_data(&from_string));

    /* The whole reason the sized tiers exist: neither view is required to be terminated, and a
     * bound that stops short of the pair must answer a miss rather than run off the end. */
    Str         short_str  = str_init_static("a=1; sid=token-value; b=2", CHAR_STATIC_SIZE("a=1;"));
    String      short_read = http_service_session_cookie_read_3(&session, &short_str);

    test_expect_u(test, "a Str bounded before the cookie answers a miss, not the bytes past its end", 0, string_get_size(&short_read));

    String  const empty_string = DEFAULT_INITIALIZATION;
    String        from_empty   = http_service_session_cookie_read_4(&session, &empty_string);
    String        from_null_4  = http_service_session_cookie_read_4(&session, nullptr);
    String        from_null_3  = http_service_session_cookie_read_3(&session, nullptr);

    test_expect_string(test, "an EMPTY String header reads as absent", "", string_get_data(&from_empty));
    test_expect_string(test, "a null String header reads as absent", "", string_get_data(&from_null_4));
    test_expect_string(test, "a null Str header reads as absent", "", string_get_data(&from_null_3));

    string_uninit(&from_null_3);
    string_uninit(&from_null_4);
    string_uninit(&from_empty);
    string_uninit(&short_read);
    string_uninit(&from_string);
    string_uninit(&header_string);
    string_uninit(&from_str);
    str_uninit(&short_str);
    str_uninit(&header_str);
    string_uninit(&hash);
    string_uninit(&empty_header);
    string_uninit(&null_header);
    http_service_session_uninit(&session);

    test_case_end(test);
}

static void _test_empty_same_site_and_path(Test *const test) {
    test_case_begin(test, "an EMPTY same_site is the documented no-WARN path, and an empty cookie_path omits Path entirely");

    /* "" is not a typo, so it must not WARN the way "bogus" does - but it must render the same
     * degraded cookie. Only "bogus" was pinned before; the documented "" path was not. */
    HTTP_Service_Session unset = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 accepts an empty same_site", http_service_session_init_2(&unset, 32, 600, "sid", "/", "", false, true));

    String no_same_site = http_service_session_cookie_create(&unset, "tok");

    test_expect_string(test, "an empty same_site omits the attribute, exactly as an unparseable one does", "sid=tok; Path=/; Max-Age=600; HttpOnly", string_get_data(&no_same_site));

    HTTP_Service_Session no_path = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 accepts an empty cookie_path", http_service_session_init_2(&no_path, 32, 600, "sid", "", "Lax", false, true));

    String scoped = http_service_session_cookie_create(&no_path, "tok");

    /* An empty path is a legal config VALUE whose String data pointer is null; the service
     * substitutes "" at the read site rather than handing the cookie module a nullptr. */
    test_expect_string(test, "an empty cookie_path omits Path rather than emitting a bare \"Path=\"", "sid=tok; Max-Age=600; HttpOnly; SameSite=Lax", string_get_data(&scoped));

    String cleared = http_service_session_cookie_clear(&no_path);

    test_expect_string(
        test, "and the clear cookie omits it too, so the browser matches the pair", "sid=; Max-Age=0; Expires=Thu, 01 Jan 1970 00:00:00 GMT; HttpOnly; SameSite=Lax",
        string_get_data(&cleared));

    string_uninit(&cleared);
    string_uninit(&scoped);
    string_uninit(&no_same_site);
    http_service_session_uninit(&no_path);
    http_service_session_uninit(&unset);

    test_case_end(test);
}

static void _test_mint_and_cookie_hash(Test *const test) {
    test_case_begin(test, "mint builds token+hash+cookie as one, and cookie_hash round-trips the wire value back to it");

    HTTP_Service_Session session = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 with cookie name \"sid\"", http_service_session_init_2(&session, 32, 600, "sid", "/", "Strict", true, true));

    HTTP_Service_Session_Token token   = DEFAULT_INITIALIZATION;
    String                     hash    = DEFAULT_INITIALIZATION;
    String                     cookie  = DEFAULT_INITIALIZATION;

    test_expect_true(test, "mint succeeds", http_service_session_mint(&session, &token, &hash, &cookie));
    test_expect_u(test, "the minted token is 64 hex characters", 64, string_get_size(&token.value));
    test_expect_u(test, "the stored hash is a 64-character SHA-256 hex digest", 64, string_get_size(&hash));
    test_expect_true(test, "the cookie carries the minted token", char_find_exists_1(string_get_data(&cookie), string_get_data(&token.value)));

    /* The whole point of the pair: what a later request derives from the Cookie header must be
     * byte-identical to what login stored, or no session ever looks up. */
    char request_cookie[256] = DEFAULT_INITIALIZATION;

    snprintf(request_cookie, sizeof(request_cookie), "other=1; sid=%s; last=2", string_get_data(&token.value));

    String looked_up = DEFAULT_INITIALIZATION;

    test_expect_true(test, "cookie_hash finds and hashes the cookie", http_service_session_cookie_hash(&session, request_cookie, &looked_up));
    test_expect_string(test, "the request-derived hash equals the hash login stored", string_get_data(&hash), string_get_data(&looked_up));

    string_uninit(&looked_up);

    String missing = DEFAULT_INITIALIZATION;

    test_expect_false(test, "cookie_hash answers false when the cookie is absent", http_service_session_cookie_hash(&session, "other=1", &missing));
    test_expect_u(test, "and leaves the output EMPTY", 0, string_get_size(&missing));

    string_uninit(&missing);

    String null_header = DEFAULT_INITIALIZATION;

    test_expect_false(test, "cookie_hash answers false on a null Cookie header without aborting", http_service_session_cookie_hash(&session, nullptr, &null_header));

    /* R5 Low 3: the char* tier's fast path tested null but not empty, so an EMPTY header ran the
     * whole scan and allocated the "" it would then free - work the String tier already skipped.
     * The verdict never changed; the cost did. */
    String empty_header = DEFAULT_INITIALIZATION;

    test_expect_false(test, "cookie_hash answers false on an EMPTY Cookie header, the same as on a null one", http_service_session_cookie_hash(&session, "", &empty_header));
    test_expect_u(test, "and leaves the output EMPTY", 0, string_get_size(&empty_header));

    string_uninit(&empty_header);
    string_uninit(&null_header);
    string_uninit(&cookie);
    string_uninit(&hash);
    http_service_session_token_uninit(&token);
    http_service_session_uninit(&session);

    test_case_end(test);
}

static void _test_cookie_hash_string_tier(Test *const test) {
    test_case_begin(test, "cookie_hash_4 answers byte-for-byte what cookie_hash answers, including on a null and an empty String");

    HTTP_Service_Session session = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 with cookie name \"sid\"", http_service_session_init_2(&session, 32, 600, "sid", "/", "Strict", true, true));

    /* The promotion is only worth its two signatures if the String tier is the SAME answer: a
     * route that switched to header_get_4 must not start looking sessions up by a different
     * digest. Both sides are computed here and compared, never assumed. */
    char const  *const header      = "other=1; sid=abc123def456; last=2";
    String             from_char   = DEFAULT_INITIALIZATION;
    String             header_view = string_init_static(header, CHAR_STATIC_SIZE("other=1; sid=abc123def456; last=2"));
    String             from_string = DEFAULT_INITIALIZATION;

    test_expect_true(test, "cookie_hash finds and hashes the cookie", http_service_session_cookie_hash(&session, header, &from_char));
    test_expect_true(test, "cookie_hash_4 finds and hashes the same cookie", http_service_session_cookie_hash_4(&session, &header_view, &from_string));
    test_expect_u(test, "both tiers answer a 64-character SHA-256 hex digest", 64, string_get_size(&from_string));
    test_expect_u(test, "of the same length", string_get_size(&from_char), string_get_size(&from_string));
    test_expect_string(test, "and the two digests are byte-identical", string_get_data(&from_char), string_get_data(&from_string));

    /* Pinned against token_hash directly, so the pair cannot agree on a WRONG digest. */
    String direct = http_service_session_token_hash(&session, "abc123def456");

    test_expect_string(test, "and both equal token_hash of the cookie value", string_get_data(&direct), string_get_data(&from_string));

    String  const empty_view  = DEFAULT_INITIALIZATION;
    String        from_empty  = DEFAULT_INITIALIZATION;
    String        from_null   = DEFAULT_INITIALIZATION;
    String        from_absent = DEFAULT_INITIALIZATION;
    String        absent_view = string_init_static("other=1", CHAR_STATIC_SIZE("other=1"));

    test_expect_false(test, "an EMPTY String header is the absent case, not an abort", http_service_session_cookie_hash_4(&session, &empty_view, &from_empty));
    test_expect_u(test, "and leaves the output EMPTY", 0, string_get_size(&from_empty));
    test_expect_false(test, "a null String header is the absent case too", http_service_session_cookie_hash_4(&session, nullptr, &from_null));
    test_expect_u(test, "and also leaves the output EMPTY", 0, string_get_size(&from_null));
    test_expect_false(test, "a header without this service's cookie answers false", http_service_session_cookie_hash_4(&session, &absent_view, &from_absent));
    test_expect_u(test, "with an EMPTY output, exactly as the char* tier does", 0, string_get_size(&from_absent));

    string_uninit(&absent_view);
    string_uninit(&from_absent);
    string_uninit(&from_null);
    string_uninit(&from_empty);
    string_uninit(&direct);
    string_uninit(&from_string);
    string_uninit(&header_view);
    string_uninit(&from_char);
    http_service_session_uninit(&session);

    test_case_end(test);
}

static void _test_config_refusals(Test *const test) {
    test_case_begin(test, "init refuses whole on every config a browser would silently drop");

    HTTP_Service_Session service = DEFAULT_INITIALIZATION;

    /* The `__Host-` prefix is the DEFAULT cookie name, and a development "Secure off for
     * localhost" toggle is the exact shape that meets it: the browser drops the cookie, the
     * login answers 200, and nothing ever authenticates. Refuse at startup instead. */
    test_expect_false(test, "`__Host-` without Secure is refused", http_service_session_init_2(&service, 32, 600, "__Host-session", "/", "Strict", false, true));
    test_expect_u(test, "and *self is left zeroed", 0, service.ttl);

    test_expect_false(test, "`__Host-` with a path other than \"/\" is refused", http_service_session_init_2(&service, 32, 600, "__Host-session", "/app", "Strict", true, true));
    test_expect_false(test, "`__Secure-` without Secure is refused", http_service_session_init_2(&service, 32, 600, "__Secure-session", "/", "Strict", false, true));
    test_expect_false(test, "SameSite=None without Secure is refused", http_service_session_init_2(&service, 32, 600, "sid", "/", "None", false, true));
    test_expect_false(test, "an empty cookie name is refused", http_service_session_init_2(&service, 32, 600, "", "/", "Strict", true, true));
    test_expect_false(test, "a cookie name that is not an RFC 6265 token is refused", http_service_session_init_2(&service, 32, 600, "bad name", "/", "Strict", true, true));
    test_expect_false(test, "a CTL byte in the cookie name is refused", http_service_session_init_2(&service, 32, 600, "si\rd", "/", "Strict", true, true));
    test_expect_false(test, "a ';' in the cookie path is refused", http_service_session_init_2(&service, 32, 600, "sid", "/a;b", "Strict", true, true));

    /* 4 random bytes is a 32-bit session id - guessable in minutes. Only 0 used to be refused. */
    test_expect_false(test, "a token_byte_count below the 16-byte floor is refused", http_service_session_init_2(&service, 4, 600, "sid", "/", "Strict", true, true));
    test_expect_true(test, "exactly the floor is accepted", http_service_session_init_2(&service, 16, 600, "sid", "/", "Strict", true, true));

    http_service_session_uninit(&service);

    /* The documented ttl-0 behaviour used to contradict the code (the doc explained Max-Age=0,
     * the constructor aborted). It refuses, and says so. */
    test_expect_false(test, "a ttl of 0 is refused (a cookie with Max-Age=0 expires on arrival)", http_service_session_init_2(&service, 32, 0, "sid", "/", "Strict", true, true));

    test_case_end(test);
}

static void _test_config_is_copied(Test *const test) {
    test_case_begin(test, "config strings are copied at init: a stack buffer overwritten afterwards still builds the right cookie");

    HTTP_Service_Session session = DEFAULT_INITIALIZATION;

    char name[16] = DEFAULT_INITIALIZATION;
    char path[16] = DEFAULT_INITIALIZATION;

    char_copy_3(name, sizeof(name), "sid", 3);
    char_copy_3(path, sizeof(path), "/", 1);

    test_expect_true(test, "init_2 from stack buffers", http_service_session_init_2(&session, 32, 600, name, path, "Lax", false, true));

    /* If the service had stored VIEWS instead of copies, the cookie below would carry "zzz". */
    char_copy_3(name, sizeof(name), "zzz", 3);
    char_copy_3(path, sizeof(path), "/x", 2);

    String cookie = http_service_session_cookie_create(&session, "tok");

    test_expect_string(test, "the cookie still uses the name and path as they were at init", "sid=tok; Path=/; Max-Age=600; HttpOnly; SameSite=Lax", string_get_data(&cookie));
    test_expect_string(test, "cookie_name_get reads back the copied name", "sid", http_service_session_cookie_name_get(&session));

    string_uninit(&cookie);
    http_service_session_uninit(&session);

    test_case_end(test);
}

static void _test_arena_tier(Test *const test) {
    test_case_begin(test, "the arena tier mirrors the heap tier, and a refused arena refuses the whole service");

    Arena arena = arena_init_1(65536, ARENA_TYPE_LINEAR);

    HTTP_Service_Session session = DEFAULT_INITIALIZATION;

    test_expect_true(test, "alloc_init_2 succeeds on a healthy arena", http_service_session_alloc_init_2(&session, 32, 600, "sid", "/", "Strict", true, true, &arena));

    String cookie = http_service_session_cookie_create(&session, "abcdef");

    test_expect_string(test, "an arena-backed cookie renders identically to a heap-backed one", "sid=abcdef; Path=/; Max-Age=600; Secure; HttpOnly; SameSite=Strict", string_get_data(&cookie));

    HTTP_Service_Session_Token token = DEFAULT_INITIALIZATION;

    test_expect_true(test, "arena-backed token_create succeeds", http_service_session_token_create(&session, &token));
    test_expect_u(test, "and produces the same 64 hex characters", 64, string_get_size(&token.value));

    http_service_session_token_uninit(&token);
    string_uninit(&cookie);
    http_service_session_uninit(&session);
    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    /* A genuinely REJECTED arena handle (byte_size 0), the shape tests/http/cookie already uses:
     * every copy comes back EMPTY, so the cookie module reads the empty name as "not a valid
     * token". A merely SMALL arena is not the same experiment - arena_linear_alloc aborts on a
     * capacity overrun rather than answering null. The service must not exist rather than
     * silently emit no cookie per request. */
    Arena starved = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    HTTP_Service_Session refused = DEFAULT_INITIALIZATION;

    test_expect_false(test, "alloc_init_2 refuses whole on a starved arena", http_service_session_alloc_init_2(&refused, 32, 600, "sid", "/", "Strict", true, true, &starved));
    test_expect_u(test, "and leaves *self zeroed", 0, refused.token_byte_count);

    arena_uninit(&starved, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

/*
 * Arena carries no public "used bytes" accessor, so the pin below measures what is LEFT: it
 * drains an arena in fixed blocks and counts them. Two arenas built the same way, one of which
 * ran verifies and one of which did not, must drain to the same count.
 */
static USize _test_arena_blocks_left(Arena *const arena) {
    USize blocks = 0;

    while (allocator_try_borrow(64, arena) != nullptr) {
        blocks = blocks + 1;
    }

    return blocks;
}

static void _test_verify_allocates_nothing(Test *const test) {
    test_case_begin(test, "verify and the absent-cookie fast path allocate on neither tier: an arena-backed service is left exactly as full");

    /* R4 Mid 1: verify used to digest THROUGH token_hash, which allocates in the service's own
     * tier - so on a linear arena, which reclaims nothing, every verify leaked a 65-byte digest
     * and session.h's arena bullet (it names only the functions that hand a String back) was
     * false. The digest goes to the stack now, and this is what says so. */
    HTTP_Service_Session heap = DEFAULT_INITIALIZATION;

    test_expect_true(test, "a heap service supplies the token and stored digest the pin verifies against", http_service_session_init_2(&heap, 32, 600, "sid", "/", "Strict", true, true));

    HTTP_Service_Session_Token token = DEFAULT_INITIALIZATION;

    test_expect_true(test, "token_create succeeds", http_service_session_token_create(&heap, &token));

    String stored = http_service_session_token_hash(&heap, string_get_data(&token.value));

    test_expect_false(test, "and its digest is not EMPTY", string_empty(&stored));

    Arena quiet = arena_init_1(65536, ARENA_TYPE_LINEAR);
    Arena busy  = arena_init_1(65536, ARENA_TYPE_LINEAR);

    HTTP_Service_Session quiet_session = DEFAULT_INITIALIZATION;
    HTTP_Service_Session busy_session  = DEFAULT_INITIALIZATION;

    test_expect_true(test, "the arena that runs no verify initializes", http_service_session_alloc_init_2(&quiet_session, 32, 600, "sid", "/", "Strict", true, true, &quiet));
    test_expect_true(test, "and so does its twin, which will run 320 calls", http_service_session_alloc_init_2(&busy_session, 32, 600, "sid", "/", "Strict", true, true, &busy));

    /* R4 Low 4 rides the same pin: cookie_hash_4 used to reach cookie_read_4 on a null or EMPTY
     * header, which allocates and terminates a String only to free it - on an arena, to leak it.
     * The unauthenticated request is the ORDINARY one, so it must cost the arena nothing. */
    String  const absent_view = DEFAULT_INITIALIZATION;
    USize         successes   = 0;

    for (USize index = 0; index < 64; index = index + 1) {
        String out = DEFAULT_INITIALIZATION;

        if (http_service_session_token_verify_1(&busy_session, string_get_data(&token.value), string_get_data(&stored))) {
            successes = successes + 1;
        }

        if (http_service_session_token_verify_4(&busy_session, string_get_data(&token.value), &stored)) {
            successes = successes + 1;
        }

        if (!http_service_session_token_verify_1(&busy_session, "not-the-token", string_get_data(&stored))) {
            successes = successes + 1;
        }

        if (!http_service_session_cookie_hash_4(&busy_session, nullptr, &out) && string_empty(&out)) {
            successes = successes + 1;
        }

        if (!http_service_session_cookie_hash_4(&busy_session, &absent_view, &out) && string_empty(&out)) {
            successes = successes + 1;
        }
    }

    test_expect_u(test, "all 320 arena-tier calls answered correctly", 320, successes);

    USize const quiet_left = _test_arena_blocks_left(&quiet);
    USize const busy_left  = _test_arena_blocks_left(&busy);

    test_expect_true(test, "the probe has room to measure, so the comparison is not vacuously 0 == 0", quiet_left > 0);
    test_expect_u(test, "and the busy arena has exactly as much left as the one that verified nothing", quiet_left, busy_left);

    http_service_session_uninit(&busy_session);
    http_service_session_uninit(&quiet_session);

    arena_uninit(&busy, ARENA_TYPE_LINEAR);
    arena_uninit(&quiet, ARENA_TYPE_LINEAR);

    string_uninit(&stored);
    http_service_session_token_uninit(&token);
    http_service_session_uninit(&heap);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Live server
 *============================================================================*/
static HTTP_Service_Session _live_session;
static char                 _live_hash[128] = DEFAULT_INITIALIZATION;

static void _route_login(HTTP_Server_Route *const route) {
    HTTP_Server_Holder *const holder = http_server_route_get_holder(route);

    HTTP_Service_Session_Token token   = DEFAULT_INITIALIZATION;
    String                     hash    = DEFAULT_INITIALIZATION;
    String                     cookie  = DEFAULT_INITIALIZATION;

    if (!http_service_session_mint(&_live_session, &token, &hash, &cookie)) {
        http_server_response_send_1(holder->response, "mint failed", HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_INTERNAL_SERVER_ERROR);

        return;
    }

    /* The "database": one row, keyed by the hash, exactly as every consumer stores it. */
    char_copy_3(_live_hash, sizeof(_live_hash), string_get_data(&hash), string_get_size(&hash));

    _live_hash[string_get_size(&hash)] = '\0';

    http_server_response_header_add(holder->response, "Set-Cookie", string_get_data(&cookie));
    http_server_response_send_1(holder->response, "ok", HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_OK);

    string_uninit(&cookie);
    string_uninit(&hash);
    http_service_session_token_uninit(&token);
}

static void _route_me(HTTP_Server_Route *const route) {
    HTTP_Server_Holder *const holder = http_server_route_get_holder(route);

    char cookie_header[1024] = DEFAULT_INITIALIZATION;

    http_server_request_header_copy(holder->request, HTTP_SERVER_HEADER_COOKIE, cookie_header, sizeof(cookie_header));

    String lookup = DEFAULT_INITIALIZATION;

    if (!http_service_session_cookie_hash(&_live_session, cookie_header, &lookup) || _live_hash[0] == '\0' ||
        !char_compare_equal_1(string_get_data(&lookup), _live_hash)) {
        string_uninit(&lookup);

        http_server_response_send_1(holder->response, "unauthorized", HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_UNAUTHORIZED);

        return;
    }

    string_uninit(&lookup);

    http_server_response_send_1(holder->response, "identified", HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_OK);
}

static void _route_logout(HTTP_Server_Route *const route) {
    HTTP_Server_Holder *const holder = http_server_route_get_holder(route);

    String cookie = http_service_session_cookie_clear(&_live_session);

    _live_hash[0] = '\0';

    http_server_response_header_add(holder->response, "Set-Cookie", string_get_data(&cookie));
    http_server_response_send_1(holder->response, "bye", HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_OK);

    string_uninit(&cookie);
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

static void _test_live_login_flow(Test *const test) {
    test_case_begin(test, "live: POST /login sets the cookie, GET /me with it identifies, a tampered or absent cookie is 401, /logout clears");

    /* Secure is OFF here on purpose: the client is a plain HTTP socket, and the cookie name is
     * a plain "sid" precisely because `__Host-` would be refused without Secure. */
    test_expect_true(test, "init_2 for the live server", http_service_session_init_2(&_live_session, 32, 600, "sid", "/", "Lax", false, true));

    HTTP_Server *server = http_server_new();

    test_expect_true(test, "route_add /login", http_server_route_add(server, "/login", _route_login));
    test_expect_true(test, "route_add /me", http_server_route_add(server, "/me", _route_me));
    test_expect_true(test, "route_add /logout", http_server_route_add(server, "/logout", _route_logout));

    U16 const port = _start(test, server);

    _Reply  reply           = DEFAULT_INITIALIZATION;
    char    set_cookie[512] = DEFAULT_INITIALIZATION;
    char    token[256]      = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "POST /login round trip", _client_round_trip(port, "POST /login HTTP/1.1\r\nHost: t\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", &reply))) {
        test_expect_u(test, "/login answers 200", 200, reply.status);
        test_expect_true(test, "/login sends a Set-Cookie header", _reply_header(&reply, "set-cookie:", set_cookie, sizeof(set_cookie)));
        test_expect_string_contains(test, "the Set-Cookie value is the bare cookie, HttpOnly and all", set_cookie, "; Path=/; Max-Age=600; HttpOnly; SameSite=Lax");
        test_expect_false(test, "and carries no Secure flag, matching the configuration", char_find_exists_1(set_cookie, "; Secure"));
    }

    /* "sid=<64 hex>; Path=/; ..." - lift the value back out to build the next request's Cookie. */
    char const *const value_start = char_find_exists_1(set_cookie, "sid=") ? set_cookie + 4 : set_cookie;
    USize             value_size  = 0;

    while (value_start[value_size] != '\0' && value_start[value_size] != ';') {
        value_size += 1;
    }

    char_copy_3(token, sizeof(token), value_start, math_min_u(value_size, sizeof(token) - 1));

    token[math_min_u(value_size, sizeof(token) - 1)] = '\0';

    test_expect_u(test, "the cookie carries a 64-hex-character token", 64, char_length(token));

    char request[1024] = DEFAULT_INITIALIZATION;

    snprintf(request, sizeof(request), "GET /me HTTP/1.1\r\nHost: t\r\nCookie: sid=%s\r\nConnection: close\r\n\r\n", token);

    if (_client_round_trip(port, request, &reply)) {
        test_expect_u(test, "GET /me with the session cookie answers 200", 200, reply.status);
        test_expect_string(test, "and identifies the session", "identified", reply.body);
    }

    if (_client_round_trip(port, "GET /me HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n", &reply)) {
        test_expect_u(test, "GET /me with no cookie at all answers 401", 401, reply.status);
    }

    snprintf(request, sizeof(request), "GET /me HTTP/1.1\r\nHost: t\r\nCookie: xsid=%s\r\nConnection: close\r\n\r\n", token);

    if (_client_round_trip(port, request, &reply)) {
        test_expect_u(test, "a cookie named \"xsid\" does not authenticate through a substring match", 401, reply.status);
    }

    /* One byte flipped: the hash misses, and a constant-time compare answers false. */
    token[0] = token[0] == 'a' ? 'b' : 'a';

    snprintf(request, sizeof(request), "GET /me HTTP/1.1\r\nHost: t\r\nCookie: sid=%s\r\nConnection: close\r\n\r\n", token);

    if (_client_round_trip(port, request, &reply)) {
        test_expect_u(test, "a token with one byte tampered answers 401", 401, reply.status);
    }

    token[0] = token[0] == 'a' ? 'b' : 'a';

    if (_client_round_trip(port, "POST /logout HTTP/1.1\r\nHost: t\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", &reply)) {
        test_expect_u(test, "/logout answers 200", 200, reply.status);
        test_expect_true(test, "/logout sends a Set-Cookie header", _reply_header(&reply, "set-cookie:", set_cookie, sizeof(set_cookie)));
        test_expect_string_contains(test, "the clear cookie expires immediately", set_cookie, "Max-Age=0");
    }

    snprintf(request, sizeof(request), "GET /me HTTP/1.1\r\nHost: t\r\nCookie: sid=%s\r\nConnection: close\r\n\r\n", token);

    if (_client_round_trip(port, request, &reply)) {
        test_expect_u(test, "replaying the old cookie after logout answers 401", 401, reply.status);
    }

    http_server_delete(&server);
    http_service_session_uninit(&_live_session);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Entry point
 *============================================================================*/
I32 main(void) {
    LogConfig const log_config = { .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true };

    log_init(log_config);

    http_server_set_log_level(LLL_ERR);

    Test test = test_init("http_service_session");

    test_suite_begin(&test, "http_service_session offline");

    _test_token_shape(&test);
    _test_token_hash_and_verify(&test);
    _test_expiry(&test);
    _test_cookie_rendering(&test);
    _test_cookie_refusals(&test);
    _test_cookie_read(&test);
    _test_cookie_read_null_header_and_tiers(&test);
    _test_empty_same_site_and_path(&test);
    _test_mint_and_cookie_hash(&test);
    _test_cookie_hash_string_tier(&test);
    _test_config_refusals(&test);
    _test_config_is_copied(&test);
    _test_arena_tier(&test);
    _test_verify_allocates_nothing(&test);

    test_suite_end(&test);

    test_suite_begin(&test, "http_service_session live");

    _test_live_login_flow(&test);

    test_suite_end(&test);

    return test_uninit(&test);
}