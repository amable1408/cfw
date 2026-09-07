/*
 * test_all.c - the http/service/traceparent suite.
 *
 * Pure cases drive the parser, the id minting and the header block directly; live cases
 * drive a REAL server - http_server_run on port 0, the ephemeral port read back with
 * http_server_get_port, and a raw `net` socket for the client so the bytes on the wire are
 * under the test's own control (the shape tests/http/server/test_all.c established).
 */
#include <stdio.h>

#include <http/server/http_server.h>
#include <http/service/traceparent/traceparent.h>
#include <log/log.h>
#include <net/net.h>
#include <test/test.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/
#define _CLIENT_HEADERS_MAX     8192
#define _CLIENT_IO_TIMEOUT_MS   4000
#define _CLIENT_RAW_MAX         32768
#define _GOLDEN                 "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01"
#define _GOLDEN_PARENT_ID       "00f067aa0ba902b7"
#define _GOLDEN_TRACE_ID        "4bf92f3577b34da6a3ce929d0e0e4736"
#define _VALUE_MAX              256

/*==============================================================================
 * MARK: - Block reader
 *============================================================================*/
static char _block_text[4096] = DEFAULT_INITIALIZATION;

/** @brief A String's bytes as NUL-terminated text, so the contains assertions can read it. */
static char* _block(String const *const block) {
    USize const size = string_get_size(block);

    _block_text[0] = '\0';

    if (size > 0 && size < sizeof(_block_text)) {
        char_copy_3(_block_text, sizeof(_block_text), string_get_data(block), size);
    }

    return _block_text;
}

/*==============================================================================
 * MARK: - Raw client
 *============================================================================*/
typedef struct {
    U16     status;
    char    headers[_CLIENT_HEADERS_MAX];
} _Reply;

static char _raw[_CLIENT_RAW_MAX] = DEFAULT_INITIALIZATION;

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

static bool _client_write(Net_Socket const socket, char const *const data, USize const size) {
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

/** @brief Read one reply's status line and header block; the body is never needed here. */
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

    char_copy_3(reply->headers, sizeof(reply->headers), _raw, header_end < sizeof(reply->headers) ? header_end : sizeof(reply->headers) - 1);

    if (raw_size >= 12) {
        reply->status = (U16) (((_raw[9] - '0') * 100) + ((_raw[10] - '0') * 10) + (_raw[11] - '0'));
    }

    return true;
}

static bool _round_trip(U16 const port, char const *const request, _Reply *const reply) {
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

/** @brief Copy the traceparent value out of a reply's header block. */
static bool _reply_value(_Reply const *const reply, char *const out, USize const capacity) {
    out[0] = '\0';

    char const *const found = char_find_slice_5(reply->headers, char_length(reply->headers), 0, "traceparent: ", CHAR_STATIC_SIZE("traceparent: "));

    if (found == nullptr || capacity <= HTTP_SERVICE_TRACEPARENT_VALUE_SIZE) {
        return false;
    }

    char const *const value = found + CHAR_STATIC_SIZE("traceparent: ");

    /* char_copy_3 below copies a FIXED HTTP_SERVICE_TRACEPARENT_VALUE_SIZE bytes regardless
     * of how much text actually follows the match - a header block cut short right after the
     * name would otherwise read past value's own NUL terminator. */
    if (char_length(value) < HTTP_SERVICE_TRACEPARENT_VALUE_SIZE) {
        return false;
    }

    char_copy_3(out, capacity, value, HTTP_SERVICE_TRACEPARENT_VALUE_SIZE);

    return true;
}

/*==============================================================================
 * MARK: - Live route
 *============================================================================*/
static HTTP_Service_Traceparent _live_trace = DEFAULT_INITIALIZATION;

static void _route_trace(HTTP_Server_Route *const route) {
    HTTP_Server_Holder *const holder = http_server_route_get_holder(route);

    char inbound[_VALUE_MAX]    = DEFAULT_INITIALIZATION;
    char tracestate[512]        = DEFAULT_INITIALIZATION;

    http_server_request_custom_header_copy(holder->request, "traceparent", inbound, sizeof(inbound));
    http_server_request_custom_header_copy(holder->request, "tracestate", tracestate, sizeof(tracestate));

    HTTP_Service_Traceparent_Context context = DEFAULT_INITIALIZATION;

    if (http_service_traceparent_child_create_2(&_live_trace, inbound, tracestate, &context)) {
        String block = http_service_traceparent_header_create(&_live_trace, &context);

        if (!string_empty(&block)) {
            http_server_response_header_add_raw(holder->response, string_get_data(&block), string_get_size(&block));
        }

        string_uninit(&block);
    }

    http_server_response_send_1(holder->response, "ok", HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_OK);
}

/*==============================================================================
 * MARK: - Pure cases
 *============================================================================*/
static void _test_root_context(Test *const test) {
    test_case_begin(test, "root context: in-place bool init, both ids minted, entropy visible");

    HTTP_Service_Traceparent trace = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_1 succeeds", http_service_traceparent_init_1(&trace));
    test_expect_string(test, "the default header name is stored", HTTP_SERVICE_TRACEPARENT_DEFAULT_HEADER_NAME, trace.header_name);

    HTTP_Service_Traceparent_Context context = DEFAULT_INITIALIZATION;

    test_expect_true(test, "context_create reports success", http_service_traceparent_context_create(&trace, &context));
    test_expect_true(test, "the context is valid", http_service_traceparent_context_valid(&context));
    test_expect_u(test, "the trace id is 32 hex characters", HTTP_SERVICE_TRACEPARENT_TRACE_ID_SIZE, char_length(context.trace_id));
    test_expect_u(test, "the parent id is 16", HTTP_SERVICE_TRACEPARENT_PARENT_ID_SIZE, char_length(context.parent_id));
    test_expect_u(test, "the flags come from the service", HTTP_SERVICE_TRACEPARENT_DEFAULT_TRACE_FLAGS, context.trace_flags);
    test_expect_true(test, "the default flags are sampled", http_service_traceparent_context_sampled(&context));
    test_expect_string(test, "no tracestate rides on a root context", "", context.tracestate);

    HTTP_Service_Traceparent_Context second = DEFAULT_INITIALIZATION;

    test_expect_true(test, "a second context is minted", http_service_traceparent_context_create(&trace, &second));
    test_expect_false(test, "and carries a different trace id", char_compare_equal_1(context.trace_id, second.trace_id));

    http_service_traceparent_uninit(&trace);

    test_expect_string(test, "uninit zeroes the header name", "", trace.header_name);

    test_case_end(test);
}

static void _test_value(Test *const test) {
    test_case_begin(test, "value: written into a caller buffer, created as a String, re-validated");

    HTTP_Service_Traceparent         trace      = DEFAULT_INITIALIZATION;
    HTTP_Service_Traceparent_Context context    = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_1 succeeds", http_service_traceparent_init_1(&trace));
    test_expect_true(test, "context_create succeeds", http_service_traceparent_context_create(&trace, &context));

    char value[HTTP_SERVICE_TRACEPARENT_VALUE_CAPACITY] = DEFAULT_INITIALIZATION;

    test_expect_true(test, "value_write_1 succeeds", http_service_traceparent_value_write_1(&context, value));
    test_expect_u(test, "and writes exactly 55 bytes", HTTP_SERVICE_TRACEPARENT_VALUE_SIZE, char_length(value));
    test_expect_true(test, "which validate", http_service_traceparent_valid_1(value));

    String created = http_service_traceparent_value_create(&trace, &context);

    test_expect_u(test, "value_create agrees on the size", HTTP_SERVICE_TRACEPARENT_VALUE_SIZE, string_get_size(&created));
    test_expect_string(test, "and on the bytes", value, _block(&created));

    string_uninit(&created);

    String header = http_service_traceparent_header_create(&trace, &context);

    test_expect_string_contains(test, "the header block names the header", _block(&header), "traceparent: ");
    test_expect_string_contains(test, "and carries the value", _block(&header), value);
    test_expect_false(test, "with no tracestate line", char_contains_1(_block(&header), "tracestate"));

    string_uninit(&header);

    HTTP_Service_Traceparent_Context empty = DEFAULT_INITIALIZATION;

    test_expect_false(test, "an empty context is invalid", http_service_traceparent_context_valid(&empty));
    test_expect_false(test, "value_write_1 refuses it", http_service_traceparent_value_write_1(&empty, value));
    test_expect_string(test, "and leaves the buffer empty", "", value);

    String empty_value  = http_service_traceparent_value_create(&trace, &empty);
    String empty_header = http_service_traceparent_header_create(&trace, &empty);

    test_expect_u(test, "value_create answers the empty String", 0, string_get_size(&empty_value));
    test_expect_u(test, "header_create answers the empty String", 0, string_get_size(&empty_header));

    string_uninit(&empty_value);
    string_uninit(&empty_header);
    http_service_traceparent_uninit(&trace);

    test_case_end(test);
}

static void _test_parse(Test *const test) {
    test_case_begin(test, "parse: the golden value's fields, and a zeroed context on refusal");

    HTTP_Service_Traceparent         trace      = DEFAULT_INITIALIZATION;
    HTTP_Service_Traceparent_Context context    = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_1 succeeds", http_service_traceparent_init_1(&trace));
    test_expect_true(test, "the golden value parses", http_service_traceparent_context_parse_1(&trace, _GOLDEN, &context));
    test_expect_string(test, "the trace id", _GOLDEN_TRACE_ID, context.trace_id);
    test_expect_string(test, "the parent id", _GOLDEN_PARENT_ID, context.parent_id);
    test_expect_u(test, "the flags byte", 1, context.trace_flags);
    test_expect_true(test, "and it is sampled", http_service_traceparent_context_sampled(&context));

    char value[HTTP_SERVICE_TRACEPARENT_VALUE_CAPACITY] = DEFAULT_INITIALIZATION;

    test_expect_true(test, "value_write_1 round trips it", http_service_traceparent_value_write_1(&context, value));
    test_expect_string(test, "byte for byte", _GOLDEN, value);

    test_expect_false(test, "garbage does not parse", http_service_traceparent_context_parse_1(&trace, "not-a-traceparent", &context));
    test_expect_string(test, "and leaves the context zeroed", "", context.trace_id);
    test_expect_u(test, "flags included", 0, context.trace_flags);

    http_service_traceparent_uninit(&trace);

    test_case_end(test);
}

static void _test_invalid_values(Test *const test) {
    test_case_begin(test, "validation: every malformed spelling is refused, null included");

    char short_value[_VALUE_MAX]    = DEFAULT_INITIALIZATION;
    char long_value[_VALUE_MAX]     = DEFAULT_INITIALIZATION;
    char upper_value[_VALUE_MAX]    = DEFAULT_INITIALIZATION;

    char_copy_3(short_value, sizeof(short_value), _GOLDEN, CHAR_STATIC_SIZE(_GOLDEN) - 1);
    snprintf(long_value, sizeof(long_value), "%s0", _GOLDEN);
    snprintf(upper_value, sizeof(upper_value), "00-4BF92F3577B34DA6A3CE929D0E0E4736-00f067aa0ba902b7-01");

    test_expect_false(test, "a null value is refused, not aborted", http_service_traceparent_valid_1(nullptr));
    test_expect_false(test, "an empty value", http_service_traceparent_valid_1(""));
    test_expect_false(test, "54 bytes", http_service_traceparent_valid_1(short_value));
    test_expect_false(test, "56 bytes with no separator at 55", http_service_traceparent_valid_1(long_value));
    test_expect_false(test, "uppercase hex", http_service_traceparent_valid_1(upper_value));
    test_expect_false(test, "the reserved ff version", http_service_traceparent_valid_1("ff-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01"));
    test_expect_false(test, "an all-zero trace id", http_service_traceparent_valid_1("00-00000000000000000000000000000000-00f067aa0ba902b7-01"));
    test_expect_false(test, "an all-zero parent id", http_service_traceparent_valid_1("00-4bf92f3577b34da6a3ce929d0e0e4736-0000000000000000-01"));
    test_expect_false(test, "non-hex flags", http_service_traceparent_valid_1("00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-zz"));
    test_expect_false(test, "a separator in the wrong place", http_service_traceparent_valid_1("000-bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01"));
    test_expect_false(test, "a non-hex version", http_service_traceparent_valid_1("zz-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01"));

    test_expect_true(test, "the golden value still validates", http_service_traceparent_valid_1(_GOLDEN));
    test_expect_true(test, "the sized tier agrees", http_service_traceparent_valid_2(_GOLDEN, CHAR_STATIC_SIZE(_GOLDEN)));
    test_expect_false(test, "and refuses a size that truncates it", http_service_traceparent_valid_2(_GOLDEN, CHAR_STATIC_SIZE(_GOLDEN) - 1));

    test_case_end(test);
}

static void _test_forward_compatibility(Test *const test) {
    test_case_begin(test, "an unknown version is parsed with the 00 layout, ff is not");

    HTTP_Service_Traceparent         trace      = DEFAULT_INITIALIZATION;
    HTTP_Service_Traceparent_Context context    = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_1 succeeds", http_service_traceparent_init_1(&trace));

    test_expect_true(test, "version 01 at 55 bytes is valid", http_service_traceparent_valid_1("01-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01"));
    test_expect_true(test, "version 01 with extra fields behind a separator is valid",
        http_service_traceparent_valid_1("01-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01-what-comes-next"));
    test_expect_false(test, "but not when byte 55 is anything else", http_service_traceparent_valid_1("01-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01x"));
    test_expect_false(test, "and never version ff, whatever the length", http_service_traceparent_valid_1("ff-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01-more"));

    test_expect_true(test, "a future-version value parses",
        http_service_traceparent_context_parse_1(&trace, "01-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01-what-comes-next", &context));
    test_expect_string(test, "keeping the caller's trace id", _GOLDEN_TRACE_ID, context.trace_id);

    char value[HTTP_SERVICE_TRACEPARENT_VALUE_CAPACITY] = DEFAULT_INITIALIZATION;

    /* Understood, but re-emitted as version 00: this hop only knows the 00 layout. */
    test_expect_true(test, "value_write_1 succeeds", http_service_traceparent_value_write_1(&context, value));
    test_expect_string(test, "and re-emits it as version 00", _GOLDEN, value);

    http_service_traceparent_uninit(&trace);

    test_case_end(test);
}

static void _test_child(Test *const test) {
    test_case_begin(test, "child: trace id kept, parent id rotated, flags inherited, garbage restarts");

    HTTP_Service_Traceparent         trace  = DEFAULT_INITIALIZATION;
    HTTP_Service_Traceparent_Context child  = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 with unsampled defaults succeeds", http_service_traceparent_init_2(&trace, HTTP_SERVICE_TRACEPARENT_DEFAULT_HEADER_NAME, 0));

    test_expect_true(test, "a child of the golden value is minted", http_service_traceparent_child_create_1(&trace, _GOLDEN, &child));
    test_expect_string(test, "the trace id is kept", _GOLDEN_TRACE_ID, child.trace_id);
    test_expect_false(test, "the parent id is rotated", char_compare_equal_1(_GOLDEN_PARENT_ID, child.parent_id));
    test_expect_u(test, "the flags are INHERITED, not defaulted", 1, child.trace_flags);
    test_expect_true(test, "and the child is valid", http_service_traceparent_context_valid(&child));

    HTTP_Service_Traceparent_Context restarted = DEFAULT_INITIALIZATION;

    test_expect_true(test, "garbage still mints a context", http_service_traceparent_child_create_1(&trace, "garbage", &restarted));
    test_expect_true(test, "which is valid", http_service_traceparent_context_valid(&restarted));
    test_expect_false(test, "but starts a NEW trace", char_compare_equal_1(_GOLDEN_TRACE_ID, restarted.trace_id));
    test_expect_u(test, "with the service's own flags", 0, restarted.trace_flags);

    HTTP_Service_Traceparent_Context absent = DEFAULT_INITIALIZATION;

    test_expect_true(test, "an absent header mints a root context too", http_service_traceparent_child_create_1(&trace, "", &absent));
    test_expect_true(test, "which is valid", http_service_traceparent_context_valid(&absent));

    http_service_traceparent_uninit(&trace);

    test_case_end(test);
}

static void _test_tracestate(Test *const test) {
    test_case_begin(test, "tracestate: carried verbatim, capped, and dropped with a restarted trace");

    HTTP_Service_Traceparent         trace      = DEFAULT_INITIALIZATION;
    HTTP_Service_Traceparent_Context context    = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_1 succeeds", http_service_traceparent_init_1(&trace));

    test_expect_true(test, "the pair parses", http_service_traceparent_context_parse_2(&trace, _GOLDEN, "vendor=abc,other=def", &context));
    test_expect_string(test, "and the tracestate is carried verbatim", "vendor=abc,other=def", context.tracestate);

    String header = http_service_traceparent_header_create(&trace, &context);

    test_expect_string_contains(test, "the header block carries the traceparent", _block(&header), "traceparent: " _GOLDEN);
    test_expect_string_contains(test, "and the tracestate beside it", _block(&header), "tracestate: vendor=abc,other=def");

    string_uninit(&header);

    test_expect_true(test, "a null tracestate is the absent case, not an abort", http_service_traceparent_context_parse_2(&trace, _GOLDEN, nullptr, &context));
    test_expect_string(test, "carrying nothing", "", context.tracestate);

    char oversize[HTTP_SERVICE_TRACEPARENT_TRACESTATE_CAPACITY + 64] = DEFAULT_INITIALIZATION;

    for (USize index = 0; index < HTTP_SERVICE_TRACEPARENT_TRACESTATE_MAX_SIZE + 1; index += 1) {
        oversize[index] = 'a';
    }

    test_expect_true(test, "an oversize tracestate still parses the traceparent", http_service_traceparent_context_parse_2(&trace, _GOLDEN, oversize, &context));
    test_expect_string(test, "but is DROPPED rather than forwarded", "", context.tracestate);

    char crowded[HTTP_SERVICE_TRACEPARENT_TRACESTATE_CAPACITY] = DEFAULT_INITIALIZATION;

    for (USize index = 0; index < HTTP_SERVICE_TRACEPARENT_TRACESTATE_MAX_MEMBERS + 1; index += 1) {
        crowded[index] = ',';
    }

    test_expect_true(test, "an over-crowded tracestate still parses the traceparent", http_service_traceparent_context_parse_2(&trace, _GOLDEN, crowded, &context));
    test_expect_string(test, "and is dropped as well", "", context.tracestate);

    test_expect_true(test, "a control byte still parses the traceparent", http_service_traceparent_context_parse_2(&trace, _GOLDEN, "vendor=a\r\nSet-Cookie: x", &context));
    test_expect_string(test, "and never reaches a header block", "", context.tracestate);

    HTTP_Service_Traceparent_Context child = DEFAULT_INITIALIZATION;

    test_expect_true(test, "a child of a valid parent carries the state", http_service_traceparent_child_create_2(&trace, _GOLDEN, "vendor=abc", &child));
    test_expect_string(test, "verbatim", "vendor=abc", child.tracestate);

    test_expect_true(test, "a child of garbage still mints", http_service_traceparent_child_create_2(&trace, "garbage", "vendor=abc", &child));
    test_expect_string(test, "and DROPS the state, because the trace restarted", "", child.tracestate);

    http_service_traceparent_uninit(&trace);

    test_case_end(test);
}

static void _test_flags_round_trip(Test *const test) {
    test_case_begin(test, "flags: 00, 01 and ff survive parse and re-emit");

    HTTP_Service_Traceparent trace = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_1 succeeds", http_service_traceparent_init_1(&trace));

    char const *const inbound[3] = {
        "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-00",
        "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01",
        "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-ff"
    };
    U8 const expected[3] = { 0, 1, 255 };

    for (USize index = 0; index < 3; index += 1) {
        HTTP_Service_Traceparent_Context context                        = DEFAULT_INITIALIZATION;
        char                             value[HTTP_SERVICE_TRACEPARENT_VALUE_CAPACITY] = DEFAULT_INITIALIZATION;

        test_expect_true(test, "the value parses", http_service_traceparent_context_parse_1(&trace, inbound[index], &context));
        test_expect_u(test, "the flags byte decodes", expected[index], context.trace_flags);
        test_expect_true(test, "value_write_1 succeeds", http_service_traceparent_value_write_1(&context, value));
        test_expect_string(test, "and re-emits the same bytes", inbound[index], value);
        test_expect_bool(test, "sampled tracks the low bit", (expected[index] & 1) != 0, http_service_traceparent_context_sampled(&context));
    }

    http_service_traceparent_uninit(&trace);

    test_case_end(test);
}

static void _test_header_name(Test *const test) {
    test_case_begin(test, "the header name knob is validated as a token at init");

    HTTP_Service_Traceparent trace = DEFAULT_INITIALIZATION;

    test_expect_false(test, "an empty name is refused", http_service_traceparent_init_2(&trace, "", 1));
    test_expect_false(test, "a spaced name is refused", http_service_traceparent_init_2(&trace, "trace parent", 1));
    test_expect_false(test, "a colon in the name is refused", http_service_traceparent_init_2(&trace, "traceparent:", 1));
    test_expect_false(test, "a CRLF name is refused", http_service_traceparent_init_2(&trace, "trace\r\nSet-Cookie: x", 1));
    test_expect_string(test, "and a refused init leaves the service zeroed", "", trace.header_name);

    char over_long[HTTP_SERVICE_TRACEPARENT_HEADER_NAME_CAPACITY + 8] = DEFAULT_INITIALIZATION;

    for (USize index = 0; index < sizeof(over_long) - 1; index += 1) {
        over_long[index] = 'a';
    }

    test_expect_false(test, "an over-long name is refused", http_service_traceparent_init_2(&trace, over_long, 1));

    test_expect_true(test, "a private token name is accepted", http_service_traceparent_init_2(&trace, "x-trace", 1));

    HTTP_Service_Traceparent_Context context = DEFAULT_INITIALIZATION;

    test_expect_true(test, "the pair parses", http_service_traceparent_context_parse_2(&trace, _GOLDEN, "vendor=abc", &context));

    String header = http_service_traceparent_header_create(&trace, &context);

    test_expect_string_contains(test, "the block uses the configured name", _block(&header), "x-trace: " _GOLDEN);
    test_expect_string_contains(test, "and the SPEC name for the state line", _block(&header), "tracestate: vendor=abc");

    string_uninit(&header);
    http_service_traceparent_uninit(&trace);

    test_case_end(test);
}

static void _test_arena(Test *const test) {
    test_case_begin(test, "arena: the returned blocks come from the arena");

    Arena                       arena = arena_init_1(4096, ARENA_TYPE_LINEAR);
    HTTP_Service_Traceparent    trace = DEFAULT_INITIALIZATION;

    test_expect_true(test, "alloc_init_1 succeeds", http_service_traceparent_alloc_init_1(&trace, &arena));

    HTTP_Service_Traceparent_Context context = DEFAULT_INITIALIZATION;

    test_expect_true(test, "context_create succeeds", http_service_traceparent_context_create(&trace, &context));

    String header = http_service_traceparent_header_create(&trace, &context);

    test_expect_string_contains(test, "the arena-backed block still names the header", _block(&header), "traceparent: ");

    string_uninit(&header);
    http_service_traceparent_uninit(&trace);
    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

/* The shape a CRLF test cannot see: string_add_2 refuses a growth WHOLLY, so a dropped
 * 55-byte value followed by a "\r\n" that fits the slack ships "traceparent: \r\n" -
 * well-formed, traceless, and accepted by every peer downstream.
 *
 * Driven through a REFUSED arena (arena_init_2 leaves `handler` null on zero geometry, with
 * live hooks, so allocator_borrow answers nullptr instead of calling through). IN A CHECKED
 * BUILD - this suite - an EXHAUSTED arena would NOT reach the guard: string_reserve borrows
 * through allocator_borrow, whose `allocate` hook treats exhaustion as a programmer error and
 * ENDS THE PROCESS, so a starved header_create aborts inside string_add_last_2 before any
 * total is compared. Without ERROR_CHECK_ENABLED that abort is not compiled and an exhausted
 * arena degrades exactly like this refused one (see tests/http/service/cors/test_unchecked.c).
 *
 * Not vacuous: delete the size comparison in header_create and the first assertion below
 * flips, because the refused appends then leave an EMPTY-but-unchecked block that the old
 * _line_complete-shaped test would equally have passed. The exact-size assertions that
 * follow are the other half - they fail if `expected` is miscounted, which would discard a
 * block that was written perfectly. */
static void _test_header_size_accounting(Test *const test) {
    test_case_begin(test, "header_create: EXACTLY the sum of its lines, and the EMPTY block when the allocator refuses");

    HTTP_Service_Traceparent            trace   = DEFAULT_INITIALIZATION;
    HTTP_Service_Traceparent_Context    context = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_1 succeeds", http_service_traceparent_init_1(&trace));
    test_expect_true(test, "and mints a context", http_service_traceparent_context_create(&trace, &context));

    String      header      = http_service_traceparent_header_create(&trace, &context);
    USize const expected    = CHAR_STATIC_SIZE("traceparent: ") + HTTP_SERVICE_TRACEPARENT_VALUE_SIZE + CHAR_STATIC_SIZE("\r\n");

    test_expect_u(test, "no tracestate: name + \": \" + 55 + CRLF", expected, string_get_size(&header));

    string_uninit(&header);

    /* The tracestate branch pays its own name, separator, payload and CRLF on top. */
    HTTP_Service_Traceparent_Context carried = DEFAULT_INITIALIZATION;

    test_expect_true(test, "a child carrying tracestate is minted",
        http_service_traceparent_child_create_2(&trace, _GOLDEN, "vendor=abc", &carried));

    String      both            = http_service_traceparent_header_create(&trace, &carried);
    USize const both_expected   = expected + CHAR_STATIC_SIZE("tracestate: vendor=abc\r\n");

    test_expect_u(test, "with tracestate: both lines, counted exactly", both_expected, string_get_size(&both));

    string_uninit(&both);

    Arena                       refused = arena_init_1(0, ARENA_TYPE_LINEAR);
    HTTP_Service_Traceparent    starved = DEFAULT_INITIALIZATION;

    test_expect_true(test, "a REFUSED arena still yields a usable service (it allocates nothing at init)",
        http_service_traceparent_alloc_init_1(&starved, &refused));

    String refused_header = http_service_traceparent_header_create(&starved, &context);

    test_expect_u(test, "and header_create answers the EMPTY block, never \"traceparent: \\r\\n\"", 0, string_get_size(&refused_header));

    string_uninit(&refused_header);

    String refused_value = http_service_traceparent_value_create(&starved, &context);

    test_expect_u(test, "value_create answers the EMPTY String, never a short value", 0, string_get_size(&refused_value));

    string_uninit(&refused_value);
    http_service_traceparent_uninit(&starved);
    arena_uninit(&refused, ARENA_TYPE_LINEAR);
    http_service_traceparent_uninit(&trace);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Live harness
 *============================================================================*/
/* The module's own default handler answers 404 without consulting the router, so a suite
 * that registers routes has to dispatch them itself (tests/http/server does the same). */
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

/*==============================================================================
 * MARK: - Live cases
 *============================================================================*/
static void _test_live(Test *const test) {
    test_case_begin(test, "live: an inbound trace is continued, garbage and absence restart it");

    test_expect_true(test, "the live service initializes", http_service_traceparent_init_1(&_live_trace));

    HTTP_Server *server = http_server_new();

    test_expect_true(test, "route_add /trace", http_server_route_add(server, "/trace", _route_trace));

    U16     const   port    = _start(test, server);
    _Reply          reply   = DEFAULT_INITIALIZATION;
    char            value[HTTP_SERVICE_TRACEPARENT_VALUE_CAPACITY] = DEFAULT_INITIALIZATION;

    bool const continued_ok = _round_trip(port,
        "GET /trace HTTP/1.1\r\nHost: 127.0.0.1\r\ntraceparent: " _GOLDEN "\r\n"
        "tracestate: vendor=abc\r\nConnection: close\r\n\r\n", &reply);

    test_expect_true(test, "the round trip completed", continued_ok);
    test_expect_u(test, "the route answered", 200, reply.status);
    test_expect_true(test, "the reply carries a traceparent", _reply_value(&reply, value, sizeof(value)));
    test_expect_true(test, "which is a valid value", http_service_traceparent_valid_1(value));
    test_expect_string_contains(test, "keeping the inbound trace id", value, _GOLDEN_TRACE_ID);
    test_expect_false(test, "with a rotated parent id", char_contains_1(value, _GOLDEN_PARENT_ID));
    test_expect_string_contains(test, "and the inbound tracestate rides along", reply.headers, "tracestate: vendor=abc");

    bool const garbage_ok = _round_trip(port,
        "GET /trace HTTP/1.1\r\nHost: 127.0.0.1\r\ntraceparent: not-a-value\r\n"
        "tracestate: vendor=abc\r\nConnection: close\r\n\r\n", &reply);

    test_expect_true(test, "the garbage round trip completed", garbage_ok);
    test_expect_true(test, "the reply still carries a traceparent", _reply_value(&reply, value, sizeof(value)));
    test_expect_true(test, "which is valid", http_service_traceparent_valid_1(value));
    test_expect_false(test, "but starts a new trace", char_contains_1(value, _GOLDEN_TRACE_ID));
    test_expect_false(test, "and drops the state of the trace it could not read", char_contains_1(reply.headers, "tracestate:"));

    bool const absent_ok = _round_trip(port,
        "GET /trace HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n", &reply);

    test_expect_true(test, "the no-header round trip completed", absent_ok);
    test_expect_true(test, "the reply carries a fresh traceparent", _reply_value(&reply, value, sizeof(value)));
    test_expect_true(test, "which is valid", http_service_traceparent_valid_1(value));
    test_expect_false(test, "and is not the inbound trace", char_contains_1(value, _GOLDEN_TRACE_ID));

    http_server_delete(&server);
    http_service_traceparent_uninit(&_live_trace);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Entry point
 *============================================================================*/
I32 main(void) {
    LogConfig const log_config = { .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true };

    log_init(log_config);
    http_server_set_log_level(LLL_ERR);

    Test test = test_init("http_service_traceparent");

    test_suite_begin(&test, "traceparent values");

    _test_root_context(&test);
    _test_value(&test);
    _test_parse(&test);
    _test_invalid_values(&test);
    _test_forward_compatibility(&test);
    _test_child(&test);
    _test_tracestate(&test);
    _test_flags_round_trip(&test);
    _test_header_name(&test);
    _test_arena(&test);
    _test_header_size_accounting(&test);

    test_suite_end(&test);

    test_suite_begin(&test, "traceparent live");

    _test_live(&test);

    test_suite_end(&test);

    return test_uninit(&test);
}