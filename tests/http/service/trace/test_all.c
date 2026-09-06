#include <http/service/trace/trace.h>
#include <datetime/datetime.h>
#include <json/json.h>
#include <test/test.h>

/* Behaviour suite for http_service_trace: the three output formats byte-for-byte, the
 * escaping that keeps a hostile path/User-Agent from forging log fields (CWE-117), the
 * exclusion list (hit, miss, and the refused-empty-prefix case), and the smaller contract
 * points (null-stream default, log_combined's per-format fallback, uninit reset). The
 * allocation-refusal half of exclude_add lives in test_unchecked.c, built without
 * ERROR_CHECK_ENABLED - see that file for why. */

#define _JSON_ARENA_BYTES (16 * 1024)

/*==============================================================================
 * MARK: - Helpers
 *============================================================================*/

// Fills buffer with the CLF timestamp trace.c would stamp a log call made "now" with.
static void _now_clf_timestamp(char *const buffer, USize const buffer_size) {
    Datetime const now = datetime_init_1();

    datetime_format(&now, "[%d/%b/%Y:%H:%M:%S +0000]", buffer, buffer_size);
}

// Reads the whole stream from its start into a NUL-terminated buffer.
static USize _read_stream(FILE *const stream, char *const buffer, USize const capacity) {
    rewind(stream);

    USize const read_size = fread(buffer, 1, capacity - 1, stream);

    buffer[read_size] = '\0';

    return read_size;
}

/* Overwrites the "[...]" CLF timestamp span with a fixed placeholder. A test
 * below renders its own "now" timestamp for `expected` BEFORE calling the
 * function under test, so a one-second tick landing between the two would
 * otherwise fail a byte-for-byte comparison on a field no test here is
 * actually trying to pin - masking both sides removes that race outright. */
static void _mask_timestamp(char *const line) {
    char *open = nullptr;

    for (char *p = line; *p != '\0'; p += 1) {
        if (*p == '[') {
            open = p;

            break;
        }
    }

    if (open == nullptr) {
        return;
    }

    for (char *p = open + 1; *p != '\0' && *p != ']'; p += 1) {
        *p = 'X';
    }
}

/*==============================================================================
 * MARK: - Cases
 *============================================================================*/

static void _test_common_format_exact_line(Test *const test) {
    test_case_begin(test, "COMMON renders the exact NCSA line for a known entry");

    FILE *const stream = tmpfile();

    if (!test_expect_not_null(test, "the fixture stream opened", (void*) stream)) {
        test_case_end(test);

        return;
    }

    HTTP_Service_Trace trace = http_service_trace_init_2(stream, HTTP_SERVICE_TRACE_FORMAT_COMMON);

    char timestamp[32] = DEFAULT_INITIALIZATION;

    _now_clf_timestamp(timestamp, sizeof(timestamp));
    http_service_trace_log(&trace, "203.0.113.9", "GET", "/api/rooms", "HTTP/1.1", 200, 512, 3);

    fflush(stream);

    char expected[256] = DEFAULT_INITIALIZATION;
    char actual[256]   = DEFAULT_INITIALIZATION;

    char_format(expected, sizeof(expected), "%s - - %s \"%s %s %s\" %d %zu\n",
                "203.0.113.9", timestamp, "GET", "/api/rooms", "HTTP/1.1", 200, (USize) 512);
    _read_stream(stream, actual, sizeof(actual));

    _mask_timestamp(expected);
    _mask_timestamp(actual);

    test_expect_string(test, "the rendered line matches byte-for-byte", expected, actual);

    http_service_trace_uninit(&trace);
    fclose(stream);

    test_case_end(test);
}

static void _test_common_zero_bytes_renders_dash(Test *const test) {
    test_case_begin(test, "COMMON renders a zero-byte response as \"-\", not \"0\"");

    FILE *const stream = tmpfile();

    if (!test_expect_not_null(test, "the fixture stream opened", (void*) stream)) {
        test_case_end(test);

        return;
    }

    HTTP_Service_Trace trace = http_service_trace_init_2(stream, HTTP_SERVICE_TRACE_FORMAT_COMMON);

    char timestamp[32] = DEFAULT_INITIALIZATION;

    _now_clf_timestamp(timestamp, sizeof(timestamp));
    http_service_trace_log(&trace, "203.0.113.9", "GET", "/health", "HTTP/1.1", 204, 0, 1);

    fflush(stream);

    char expected[256] = DEFAULT_INITIALIZATION;
    char actual[256]   = DEFAULT_INITIALIZATION;

    char_format(expected, sizeof(expected), "%s - - %s \"%s %s %s\" %d -\n",
                "203.0.113.9", timestamp, "GET", "/health", "HTTP/1.1", 204);
    _read_stream(stream, actual, sizeof(actual));

    _mask_timestamp(expected);
    _mask_timestamp(actual);

    test_expect_string(test, "bytes renders as a dash", expected, actual);

    http_service_trace_uninit(&trace);
    fclose(stream);

    test_case_end(test);
}

static void _test_combined_format_exact_line(Test *const test) {
    test_case_begin(test, "COMBINED renders Common plus quoted referer and user-agent");

    FILE *const stream = tmpfile();

    if (!test_expect_not_null(test, "the fixture stream opened", (void*) stream)) {
        test_case_end(test);

        return;
    }

    HTTP_Service_Trace trace = http_service_trace_init_2(stream, HTTP_SERVICE_TRACE_FORMAT_COMBINED);

    char timestamp[32] = DEFAULT_INITIALIZATION;

    _now_clf_timestamp(timestamp, sizeof(timestamp));
    http_service_trace_log_combined(&trace, "203.0.113.9", "GET", "/api/rooms", "HTTP/1.1", 200, 512, 3,
                                     "https://example.com", "curl/8.0");

    fflush(stream);

    char expected[384] = DEFAULT_INITIALIZATION;
    char actual[384]   = DEFAULT_INITIALIZATION;

    char_format(expected, sizeof(expected), "%s - - %s \"%s %s %s\" %d %zu \"%s\" \"%s\"\n",
                "203.0.113.9", timestamp, "GET", "/api/rooms", "HTTP/1.1", 200, (USize) 512,
                "https://example.com", "curl/8.0");
    _read_stream(stream, actual, sizeof(actual));

    _mask_timestamp(expected);
    _mask_timestamp(actual);

    test_expect_string(test, "the rendered line matches byte-for-byte", expected, actual);

    http_service_trace_uninit(&trace);
    fclose(stream);

    test_case_end(test);
}

static void _test_combined_missing_fields_render_dash(Test *const test) {
    test_case_begin(test, "COMBINED renders a null referer/user-agent as \"-\"");

    FILE *const stream = tmpfile();

    if (!test_expect_not_null(test, "the fixture stream opened", (void*) stream)) {
        test_case_end(test);

        return;
    }

    HTTP_Service_Trace trace = http_service_trace_init_2(stream, HTTP_SERVICE_TRACE_FORMAT_COMBINED);

    char timestamp[32] = DEFAULT_INITIALIZATION;

    _now_clf_timestamp(timestamp, sizeof(timestamp));
    http_service_trace_log_combined(&trace, "203.0.113.9", "GET", "/api/rooms", "HTTP/1.1", 200, 512, 3,
                                     nullptr, nullptr);

    fflush(stream);

    char expected[384] = DEFAULT_INITIALIZATION;
    char actual[384]   = DEFAULT_INITIALIZATION;

    char_format(expected, sizeof(expected), "%s - - %s \"%s %s %s\" %d %zu \"-\" \"-\"\n",
                "203.0.113.9", timestamp, "GET", "/api/rooms", "HTTP/1.1", 200, (USize) 512);
    _read_stream(stream, actual, sizeof(actual));

    _mask_timestamp(expected);
    _mask_timestamp(actual);

    test_expect_string(test, "both fields render as a dash", expected, actual);

    http_service_trace_uninit(&trace);
    fclose(stream);

    test_case_end(test);
}

static void _test_hostile_path_and_user_agent_are_escaped(Test *const test) {
    test_case_begin(test, "a quote, a backslash and a control byte in the path/UA are escaped, not injected");

    FILE *const stream = tmpfile();

    if (!test_expect_not_null(test, "the fixture stream opened", (void*) stream)) {
        test_case_end(test);

        return;
    }

    HTTP_Service_Trace trace = http_service_trace_init_2(stream, HTTP_SERVICE_TRACE_FORMAT_COMBINED);

    /* Bytes: / a " b \ c 0x01 e n d - a quote, a backslash, and a control byte in one path. */
    char const *const hostile_path = "/a\"b\\c" "\x01" "end";
    /* Bytes: U A " / 1 . 0 \ b o t 0x02 e n d - same three shapes in the User-Agent. */
    char const *const hostile_ua   = "UA\"/1.0\\bot" "\x02" "end";

    char timestamp[32] = DEFAULT_INITIALIZATION;

    _now_clf_timestamp(timestamp, sizeof(timestamp));
    http_service_trace_log_combined(&trace, "203.0.113.9", "GET", hostile_path, "HTTP/1.1", 200, 512, 3,
                                     nullptr, hostile_ua);

    fflush(stream);

    char expected[384] = DEFAULT_INITIALIZATION;
    char actual[384]   = DEFAULT_INITIALIZATION;

    char_format(expected, sizeof(expected), "%s - - %s \"%s %s %s\" %d %zu \"-\" \"%s\"\n",
                "203.0.113.9", timestamp, "GET", "/a\\\"b\\\\c\\x01end", "HTTP/1.1", 200, (USize) 512,
                "UA\\\"/1.0\\\\bot\\x02end");
    _read_stream(stream, actual, sizeof(actual));

    _mask_timestamp(expected);
    _mask_timestamp(actual);

    test_expect_string(test, "the line escapes every hostile byte and stays one well-formed entry", expected, actual);

    http_service_trace_uninit(&trace);
    fclose(stream);

    test_case_end(test);
}

static void _test_json_format_exact_line_and_round_trips(Test *const test) {
    test_case_begin(test, "JSON escapes the same hostile bytes and parses back to the original strings");

    FILE *const stream = tmpfile();

    if (!test_expect_not_null(test, "the fixture stream opened", (void*) stream)) {
        test_case_end(test);

        return;
    }

    HTTP_Service_Trace trace = http_service_trace_init_2(stream, HTTP_SERVICE_TRACE_FORMAT_JSON);

    char const *const hostile_path = "/a\"b\\c" "\x01" "end";
    char const *const hostile_ua   = "UA\"/1.0\\bot" "\x02" "end";

    char timestamp[32] = DEFAULT_INITIALIZATION;

    _now_clf_timestamp(timestamp, sizeof(timestamp));
    http_service_trace_log_combined(&trace, "203.0.113.9", "GET", hostile_path, "HTTP/1.1", 200, 512, 7,
                                     nullptr, hostile_ua);

    fflush(stream);

    char expected[512] = DEFAULT_INITIALIZATION;
    char actual[512]   = DEFAULT_INITIALIZATION;

    char_format(expected, sizeof(expected),
                "{\"ip\":\"%s\",\"time\":\"%s\",\"method\":\"%s\",\"path\":\"%s\",\"protocol\":\"%s\","
                "\"status\":%d,\"bytes\":%zu,\"duration_ms\":%llu,\"referer\":null,\"user_agent\":\"%s\"}\n",
                "203.0.113.9", timestamp, "GET", "/a\\\"b\\\\c\\u0001end", "HTTP/1.1", 200, (USize) 512,
                (unsigned long long) 7, "UA\\\"/1.0\\\\bot\\u0002end");
    _read_stream(stream, actual, sizeof(actual));

    _mask_timestamp(expected);
    _mask_timestamp(actual);

    test_expect_string(test, "the rendered JSON line matches byte-for-byte", expected, actual);

    /* Round-trip: the json module must parse the line and hand back the ORIGINAL, unescaped
     * bytes - proof the escaping is reversible and not merely well-formed-looking. */
    Arena arena = arena_init_1(_JSON_ARENA_BYTES, ARENA_TYPE_LINEAR);
    Json *const parsed = json_alloc_from_1(actual, &arena);

    if (test_expect_not_null(test, "the json module parses the emitted line", (void*) parsed)) {
        char *const path_back = json_read_string(parsed, "path");
        char *const ua_back = json_read_string(parsed, "user_agent");
        ISize status_back = 0;

        test_expect_string(test, "path round-trips to the original bytes", hostile_path, path_back);
        test_expect_string(test, "user_agent round-trips to the original bytes", hostile_ua, ua_back);
        test_expect_true(test, "status parses back", json_read_int(parsed, "status", &status_back));
        test_expect_i(test, "status value round-trips", 200, status_back);
    }

    arena_uninit(&arena, ARENA_TYPE_LINEAR);
    http_service_trace_uninit(&trace);
    fclose(stream);

    test_case_end(test);
}

static void _test_exclusion_hit_and_miss(Test *const test) {
    test_case_begin(test, "an excluded prefix suppresses its path but not an unrelated one");

    FILE *const stream = tmpfile();

    if (!test_expect_not_null(test, "the fixture stream opened", (void*) stream)) {
        test_case_end(test);

        return;
    }

    HTTP_Service_Trace trace = http_service_trace_init_2(stream, HTTP_SERVICE_TRACE_FORMAT_COMMON);

    test_expect_true(test, "the prefix is stored", http_service_trace_exclude_add(&trace, "/health"));

    http_service_trace_log(&trace, "203.0.113.9", "GET", "/health/live", "HTTP/1.1", 200, 4, 1);

    long const after_excluded = ftell(stream);

    test_expect_i(test, "the excluded path wrote nothing", 0, after_excluded);

    http_service_trace_log(&trace, "203.0.113.9", "GET", "/api/rooms", "HTTP/1.1", 200, 4, 1);

    long const after_unrelated = ftell(stream);

    test_expect_true(test, "an unrelated path still writes", after_unrelated > after_excluded);

    http_service_trace_uninit(&trace);
    fclose(stream);

    test_case_end(test);
}

static void _test_exclude_add_refuses_empty(Test *const test) {
    test_case_begin(test, "exclude_add(\"\") is refused and does not silence the log");

    FILE *const stream = tmpfile();

    if (!test_expect_not_null(test, "the fixture stream opened", (void*) stream)) {
        test_case_end(test);

        return;
    }

    HTTP_Service_Trace trace = http_service_trace_init_2(stream, HTTP_SERVICE_TRACE_FORMAT_COMMON);

    test_expect_false(test, "an empty prefix is refused", http_service_trace_exclude_add(&trace, ""));
    test_expect_u(test, "nothing was stored", 0, al_str_get_size(&trace.exclude_paths));

    http_service_trace_log(&trace, "203.0.113.9", "GET", "/anything", "HTTP/1.1", 200, 4, 1);

    long const after = ftell(stream);

    test_expect_true(test, "logging still works after the refused call", after > 0);

    http_service_trace_uninit(&trace);
    fclose(stream);

    test_case_end(test);
}

static void _test_oversize_path_truncates_at_field_boundary(Test *const test) {
    test_case_begin(test, "a path far past the line capacity truncates whole-field, keeps one trailing newline and the closing brace, and starts a clean second line");

    FILE *const stream = tmpfile();

    if (!test_expect_not_null(test, "the fixture stream opened", (void*) stream)) {
        test_case_end(test);

        return;
    }

    HTTP_Service_Trace trace = http_service_trace_init_2(stream, HTTP_SERVICE_TRACE_FORMAT_JSON);

    /* Every byte past the leading '/' is 0x01, which always escapes to the 6-byte JSON
     * unit - a uniform run of atomic pieces, so any correct truncation must
     * land exactly on a piece boundary, never mid-escape. */
    char huge_path[HTTP_SERVICE_TRACE_LINE_CAPACITY_MAX * 2] = DEFAULT_INITIALIZATION;

    huge_path[0] = '/';

    for (USize i = 1; i < sizeof(huge_path) - 1; i += 1) {
        huge_path[i] = '\x01';
    }

    http_service_trace_log(&trace, "203.0.113.9", "GET", huge_path, "HTTP/1.1", 200, 0, 1);
    http_service_trace_log(&trace, "203.0.113.9", "GET", "/second", "HTTP/1.1", 200, 0, 1);

    fflush(stream);

    char actual[HTTP_SERVICE_TRACE_LINE_CAPACITY_MAX * 3] = DEFAULT_INITIALIZATION;
    USize const read_size = _read_stream(stream, actual, sizeof(actual));

    USize newline_count = 0;

    for (USize i = 0; i < read_size; i += 1) {
        if (actual[i] == '\n') {
            newline_count += 1;
        }
    }

    test_expect_u(test, "exactly two newlines total - one per call, never more from the oversize call", 2, newline_count);

    char const *const first_newline = memchr(actual, '\n', read_size);
    USize const first_line_len = first_newline != nullptr ? (USize) (first_newline - actual) : 0;

    /* A regression that grew first_line_len past HTTP_SERVICE_TRACE_LINE_CAPACITY_MAX would
     * overflow the fixed-size first_line buffer below - return early on this failure rather
     * than let the length feed the memory_copy_1 further down unguarded. */
    if (!test_expect_true(test, "the truncated first line stays within the capacity", first_line_len > 0 && first_line_len < HTTP_SERVICE_TRACE_LINE_CAPACITY_MAX)) {
        http_service_trace_uninit(&trace);
        fclose(stream);

        test_case_end(test);

        return;
    }

    test_expect_true(test, "the truncated line still ends with the JSON closing brace before its newline", actual[first_line_len - 1] == '}');

    /* A split 2-byte "\\u0001" escape would leave a lone '\' right before the brace -
     * an odd-length run. Every atomic escape is 2 (\\\\) or 6 bytes wide, so a whole
     * number of them ending in a backslash-led piece is impossible; only a SPLIT one
     * could produce an odd run. */
    USize backslash_run = 0;

    if (first_line_len > 0) {
        for (USize i = first_line_len - 1; i > 0 && actual[i - 1] == '\\'; i -= 1) {
            backslash_run += 1;
        }
    }

    test_expect_true(test, "no odd trailing run of backslashes precedes the closing brace", backslash_run % 2 == 0);
    test_expect_true(test, "the second, unrelated call still lands on its own line after the first's newline", read_size > first_line_len + 1);

    /* The field cap (High 1) is what makes this whole test possible: it is not
     * enough that the line ENDS in '}' with a balanced backslash run - the
     * fixed skeleton after the oversize path (protocol, status, bytes,
     * duration_ms, referer, user_agent) must still be there at all. Parsing
     * the truncated line back with the json module is the actual proof: an
     * unclosed "path" string (the pre-fix defect) fails to parse outright. */
    /* Re-checked here, not just trusted from the early return above: this guard is what
     * actually stands between first_line_len and an overflow of the fixed-size first_line
     * buffer below, should the earlier check ever be edited away. */
    if (first_line_len > 0 && first_line_len < HTTP_SERVICE_TRACE_LINE_CAPACITY_MAX) {
        char first_line[HTTP_SERVICE_TRACE_LINE_CAPACITY_MAX] = DEFAULT_INITIALIZATION;

        memory_copy_1(first_line, actual, first_line_len);
        first_line[first_line_len] = '\0';

        /* Bigger than _JSON_ARENA_BYTES: this line's path field is near the full
         * HTTP_SERVICE_TRACE_FIELD_CAPACITY_PATH cap of \u-escaped bytes, and
         * yyjson's parse buffer scales with input size, not just content size. */
        Arena json_arena = arena_init_1(128 * 1024, ARENA_TYPE_LINEAR);
        Json *const parsed = json_alloc_from_1(first_line, &json_arena);

        if (test_expect_not_null(test, "the truncated JSON line still parses as valid JSON", (void*) parsed)) {
            ISize status_back = 0;

            test_expect_true(test, "status survives past the truncated path field", json_read_int(parsed, "status", &status_back));
            test_expect_i(test, "status value is intact", 200, status_back);

            char *const path_back = json_read_string(parsed, "path");

            test_expect_not_null(test, "path is still present, just cut short", (void*) path_back);

            if (path_back != nullptr) {
                test_expect_true(test, "the cut path ends with the \"...\" marker", char_length(path_back) >= 3 && char_compare_equal_1(path_back + char_length(path_back) - 3, "..."));
            }
        }

        arena_uninit(&json_arena, ARENA_TYPE_LINEAR);
    }

    http_service_trace_uninit(&trace);
    fclose(stream);

    test_case_end(test);
}

static void _test_oversize_path_common_format_stays_well_formed(Test *const test) {
    test_case_begin(test, "an oversize path in COMMON format leaves the closing quote, status and bytes intact");

    FILE *const stream = tmpfile();

    if (!test_expect_not_null(test, "the fixture stream opened", (void*) stream)) {
        test_case_end(test);

        return;
    }

    HTTP_Service_Trace trace = http_service_trace_init_2(stream, HTTP_SERVICE_TRACE_FORMAT_COMMON);

    char huge_path[HTTP_SERVICE_TRACE_LINE_CAPACITY_MAX * 2] = DEFAULT_INITIALIZATION;

    huge_path[0] = '/';

    for (USize i = 1; i < sizeof(huge_path) - 1; i += 1) {
        huge_path[i] = 'a';
    }

    http_service_trace_log(&trace, "203.0.113.9", "GET", huge_path, "HTTP/1.1", 200, 512, 1);

    fflush(stream);

    char actual[HTTP_SERVICE_TRACE_LINE_CAPACITY_MAX * 2] = DEFAULT_INITIALIZATION;
    USize const read_size = _read_stream(stream, actual, sizeof(actual));

    /* Field-split the request line's closing quote, rather than assuming any fixed
     * offset: "METHOD path PROTOCOL" status bytes\n. Before the cap (High 1), an
     * oversize path could consume so much of the buffer that this closing quote,
     * the status and the byte count were dropped entirely by the all-or-nothing
     * append rule. */
    char const *const closing_quote = char_find_slice_3(actual, 0, "\" ");

    if (test_expect_not_null(test, "the request line's closing quote and the fields after it are present", (void*) closing_quote)) {
        I32   status_back = 0;
        long  bytes_back  = 0;
        int   matched     = sscanf(closing_quote, "\" %d %ld", &status_back, &bytes_back);

        test_expect_i(test, "both status and bytes parsed out", 2, matched);
        test_expect_i(test, "status is intact", 200, status_back);
        test_expect_i(test, "bytes is intact", 512, (I32) bytes_back);
    }

    /* read_size > 1, not just > 0: at read_size == 1 the third operand would read
     * actual[(USize) -1], an out-of-bounds access disguised by unsigned wraparound. */
    test_expect_true(test, "the line ends in exactly one trailing newline", read_size > 1 && actual[read_size - 1] == '\n' && actual[read_size - 2] != '\n');

    http_service_trace_uninit(&trace);
    fclose(stream);

    test_case_end(test);
}

static void _test_json_escapes_high_bytes_common_passes_them_raw(Test *const test) {
    test_case_begin(test, "JSON escapes a byte >= 0x80 as \\u00XX; COMMON stays byte-oriented and passes it through raw");

    char const *const path_with_high_byte = "/a" "\xFF" "b";

    FILE *const json_stream = tmpfile();

    if (test_expect_not_null(test, "the JSON fixture stream opened", (void*) json_stream)) {
        HTTP_Service_Trace json_trace = http_service_trace_init_2(json_stream, HTTP_SERVICE_TRACE_FORMAT_JSON);

        http_service_trace_log(&json_trace, "203.0.113.9", "GET", path_with_high_byte, "HTTP/1.1", 200, 0, 1);

        fflush(json_stream);

        char actual[256] = DEFAULT_INITIALIZATION;

        _read_stream(json_stream, actual, sizeof(actual));

        test_expect_not_null(test, "0xFF renders as \\u00ff in the JSON path field", (void*) char_find_slice_3(actual, 0, "\"path\":\"/a\\u00ffb\""));

        http_service_trace_uninit(&json_trace);
        fclose(json_stream);
    }

    FILE *const common_stream = tmpfile();

    if (test_expect_not_null(test, "the COMMON fixture stream opened", (void*) common_stream)) {
        HTTP_Service_Trace common_trace = http_service_trace_init_2(common_stream, HTTP_SERVICE_TRACE_FORMAT_COMMON);

        http_service_trace_log(&common_trace, "203.0.113.9", "GET", path_with_high_byte, "HTTP/1.1", 200, 0, 1);

        fflush(common_stream);

        char actual[256] = DEFAULT_INITIALIZATION;
        USize const read_size = _read_stream(common_stream, actual, sizeof(actual));
        bool raw_byte_present = false;

        for (USize i = 0; i < read_size; i += 1) {
            if ((unsigned char) actual[i] == 0xFF) {
                raw_byte_present = true;

                break;
            }
        }

        test_expect_true(test, "0xFF passes through raw in COMMON (byte-oriented CLF), not escaped", raw_byte_present);

        http_service_trace_uninit(&common_trace);
        fclose(common_stream);
    }

    test_case_end(test);
}

/* Worst-case pin: every one of the six escaped fields (ip, method, path, protocol, referer,
 * user_agent) oversize AT ONCE, in both formats that carry all six (COMBINED and JSON - COMMON
 * never sees referer/user_agent). This is the case the per-field caps exist for: without them,
 * six over-length fields together could blow the line past HTTP_SERVICE_TRACE_LINE_CAPACITY_MAX
 * and force the line-level backstop truncation trace.h calls out as otherwise unreachable. */
static void _test_worst_case_all_fields_oversize_stays_under_line_capacity(Test *const test) {
    test_case_begin(test, "all six fields oversize at once still fits well under the line cap with six truncation markers");

    char huge_short[HTTP_SERVICE_TRACE_FIELD_CAPACITY_SHORT * 2] = DEFAULT_INITIALIZATION;
    char huge_path[HTTP_SERVICE_TRACE_FIELD_CAPACITY_PATH * 2]   = DEFAULT_INITIALIZATION;
    char huge_long[HTTP_SERVICE_TRACE_FIELD_CAPACITY_LONG * 2]   = DEFAULT_INITIALIZATION;

    memory_set(huge_short, sizeof(huge_short) - 1, 'a');
    huge_short[sizeof(huge_short) - 1] = '\0';

    memory_set(huge_path, sizeof(huge_path) - 1, 'a');
    huge_path[0]                     = '/';
    huge_path[sizeof(huge_path) - 1] = '\0';

    memory_set(huge_long, sizeof(huge_long) - 1, 'a');
    huge_long[sizeof(huge_long) - 1] = '\0';

    FILE *const combined_stream = tmpfile();

    if (test_expect_not_null(test, "the COMBINED fixture stream opened", (void*) combined_stream)) {
        HTTP_Service_Trace trace = http_service_trace_init_2(combined_stream, HTTP_SERVICE_TRACE_FORMAT_COMBINED);

        http_service_trace_log_combined(&trace, huge_short, huge_short, huge_path, huge_short, 200, 512, 1, huge_long, huge_long);

        fflush(combined_stream);

        char actual[HTTP_SERVICE_TRACE_LINE_CAPACITY_MAX * 2] = DEFAULT_INITIALIZATION;
        USize const read_size = _read_stream(combined_stream, actual, sizeof(actual));

        test_expect_true(test, "COMBINED: the rendered line stays under the line capacity", read_size < HTTP_SERVICE_TRACE_LINE_CAPACITY_MAX);
        test_expect_u(test, "COMBINED: all six oversize fields were truncated with a marker", 6, char_find_count_1(actual, "..."));

        http_service_trace_uninit(&trace);
        fclose(combined_stream);
    }

    FILE *const json_stream = tmpfile();

    if (test_expect_not_null(test, "the JSON fixture stream opened", (void*) json_stream)) {
        HTTP_Service_Trace trace = http_service_trace_init_2(json_stream, HTTP_SERVICE_TRACE_FORMAT_JSON);

        http_service_trace_log_combined(&trace, huge_short, huge_short, huge_path, huge_short, 200, 512, 1, huge_long, huge_long);

        fflush(json_stream);

        char actual[HTTP_SERVICE_TRACE_LINE_CAPACITY_MAX * 2] = DEFAULT_INITIALIZATION;
        USize const read_size = _read_stream(json_stream, actual, sizeof(actual));

        test_expect_true(test, "JSON: the rendered line stays under the line capacity", read_size < HTTP_SERVICE_TRACE_LINE_CAPACITY_MAX);
        test_expect_u(test, "JSON: all six oversize fields were truncated with a marker", 6, char_find_count_1(actual, "..."));

        /* Round-trip: proof the six truncated fields leave the JSON object itself well-formed,
         * not merely under the byte cap. 128 * 1024, not 64 * 1024: yyjson's parse buffer
         * scales with input size, not just content size, and this line sits near the full
         * HTTP_SERVICE_TRACE_FIELD_CAPACITY_PATH cap of bytes, same as the sibling oversize test. */
        Arena arena = arena_init_1(128 * 1024, ARENA_TYPE_LINEAR);
        Json *const parsed = json_alloc_from_1(actual, &arena);

        if (test_expect_not_null(test, "JSON: the worst-case line still parses as valid JSON", (void*) parsed)) {
            ISize status_back = 0;

            test_expect_true(test, "JSON: status survives past all six truncated fields", json_read_int(parsed, "status", &status_back));
            test_expect_i(test, "JSON: status value is intact", 200, status_back);
        }

        arena_uninit(&arena, ARENA_TYPE_LINEAR);
        http_service_trace_uninit(&trace);
        fclose(json_stream);
    }

    test_case_end(test);
}

static void _test_null_stream_logs_safely(Test *const test) {
    test_case_begin(test, "a null self->stream (e.g. reused after uninit) makes log a safe no-op, not a crash");

    HTTP_Service_Trace trace = http_service_trace_init_1();

    trace.stream = nullptr;

    http_service_trace_log(&trace, "203.0.113.9", "GET", "/api/rooms", "HTTP/1.1", 200, 4, 1);

    test_expect_true(test, "reaching this line means the null-stream call did not crash", true);

    http_service_trace_uninit(&trace);

    test_case_end(test);
}

static void _test_init_2_null_stream_defaults_to_stdout(Test *const test) {
    test_case_begin(test, "init_2 with a null stream falls back to stdout");

    HTTP_Service_Trace trace = http_service_trace_init_2(nullptr, HTTP_SERVICE_TRACE_FORMAT_COMMON);

    test_expect_true(test, "the stream defaults to stdout", trace.stream == stdout);

    http_service_trace_uninit(&trace);

    test_case_end(test);
}

static void _test_log_combined_falls_back_to_common(Test *const test) {
    test_case_begin(test, "log_combined on a COMMON-format service ignores referer/user-agent");

    FILE *const stream = tmpfile();

    if (!test_expect_not_null(test, "the fixture stream opened", (void*) stream)) {
        test_case_end(test);

        return;
    }

    HTTP_Service_Trace trace = http_service_trace_init_2(stream, HTTP_SERVICE_TRACE_FORMAT_COMMON);

    char timestamp[32] = DEFAULT_INITIALIZATION;

    _now_clf_timestamp(timestamp, sizeof(timestamp));
    http_service_trace_log_combined(&trace, "203.0.113.9", "GET", "/api/rooms", "HTTP/1.1", 200, 512, 3,
                                     "https://example.com", "curl/8.0");

    fflush(stream);

    char expected[256] = DEFAULT_INITIALIZATION;
    char actual[256]   = DEFAULT_INITIALIZATION;

    char_format(expected, sizeof(expected), "%s - - %s \"%s %s %s\" %d %zu\n",
                "203.0.113.9", timestamp, "GET", "/api/rooms", "HTTP/1.1", 200, (USize) 512);
    _read_stream(stream, actual, sizeof(actual));

    _mask_timestamp(expected);
    _mask_timestamp(actual);

    test_expect_string(test, "the referer/user-agent never appear - the line is plain Common", expected, actual);

    http_service_trace_uninit(&trace);
    fclose(stream);

    test_case_end(test);
}

static void _test_uninit_resets_fields(Test *const test) {
    test_case_begin(test, "uninit releases exclude_paths and resets stream/format");

    HTTP_Service_Trace trace = http_service_trace_init_1();

    test_expect_true(test, "a prefix is stored before uninit", http_service_trace_exclude_add(&trace, "/health"));

    http_service_trace_uninit(&trace);

    test_expect_null(test, "the stream is reset", (void*) trace.stream);
    test_expect_true(test, "the format resets to COMMON", trace.format == HTTP_SERVICE_TRACE_FORMAT_COMMON);
    test_expect_u(test, "the exclusion list is empty", 0, al_str_get_size(&trace.exclude_paths));

    test_case_end(test);
}

I32 main(void) {
    Test test = test_init("tests/http/service/trace/test_all.c");

    test_verbose_set(&test, false);

    test_suite_begin(&test, "http_service_trace behaviour");
    _test_common_format_exact_line(&test);
    _test_common_zero_bytes_renders_dash(&test);
    _test_combined_format_exact_line(&test);
    _test_combined_missing_fields_render_dash(&test);
    _test_hostile_path_and_user_agent_are_escaped(&test);
    _test_json_format_exact_line_and_round_trips(&test);
    _test_exclusion_hit_and_miss(&test);
    _test_exclude_add_refuses_empty(&test);
    _test_init_2_null_stream_defaults_to_stdout(&test);
    _test_log_combined_falls_back_to_common(&test);
    _test_uninit_resets_fields(&test);
    _test_oversize_path_truncates_at_field_boundary(&test);
    _test_oversize_path_common_format_stays_well_formed(&test);
    _test_json_escapes_high_bytes_common_passes_them_raw(&test);
    _test_worst_case_all_fields_oversize_stays_under_line_capacity(&test);
    _test_null_stream_logs_safely(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}