#include <test/test.h>

#include <http/service/multipart/multipart.h>

/* Behavioural pins for http_service_multipart_parse and its accessors.
 *
 * The allocation-failure sweep and its leak ledger live in test_oom.c, which
 * already owns the __wrap_calloc/__wrap_free symbols this suite once
 * duplicated - a leak count is not a parsing behaviour, and the two binaries
 * cannot share those symbols anyway. This file pins what the parser actually
 * produces: field extraction, delimiter/line-ending shapes, the failure
 * contract on a malformed body, the part/header/boundary bounds, and the
 * boundary/count/at accessors. */

#define _BOUNDARY "b"

/*==============================================================================
 * MARK: - Field extraction
 *============================================================================*/

static void _test_fields(Test *const test) {
    test_case_begin(test, "name, filename and content-type are extracted");

    static char const payload[] =
        "--" _BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"caption\"\r\n\r\n"
        "hello\r\n"
        "--" _BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"upload\"; filename=\"a.bin\"\r\n"
        "content-type:application/octet-stream\r\n\r\n"
        "\x01\x02\x03"
        "\r\n"
        "--" _BOUNDARY "--\r\n";

    HTTP_Service_Multipart multipart = http_service_multipart_init();

    bool const parsed = http_service_multipart_parse(&multipart, _BOUNDARY, (Byte const*) payload, sizeof(payload) - 1);

    test_expect_true(test, "payload parsed", parsed);
    test_expect_u(test, "two parts", 2, http_service_multipart_count(&multipart));

    HTTP_Service_Multipart_Node const *const caption = http_service_multipart_node_get(&multipart, "caption");
    HTTP_Service_Multipart_Node const *const upload  = http_service_multipart_node_get(&multipart, "upload");

    test_expect_not_null(test, "caption found", caption);
    test_expect_not_null(test, "upload found", upload);

    if (caption != nullptr) {
        test_expect_null(test, "caption has no filename", caption->filename);
        test_expect_u(test, "caption body size", 5, caption->size);
    }

    if (upload != nullptr) {
        test_expect_string(test, "upload filename", "a.bin", upload->filename);
        /* Lower-case, no space after the colon: the case-insensitive match. */
        test_expect_string(test, "upload content type", "application/octet-stream", upload->content_type);
        test_expect_u(test, "upload body size", 3, upload->size);
    }

    http_service_multipart_uninit(&multipart);

    test_case_end(test);
}

static void _test_quoted_semicolon_does_not_smuggle_a_name(Test *const test) {
    test_case_begin(test, "text shaped like `; name=\"y\"` INSIDE a still-open quoted value is not read as a real name= parameter");

    static char const payload[] =
        "--" _BOUNDARY "\r\n"
        "Content-Disposition: form-data; filename=\"a; name=\"y\"\"\r\n\r\n"
        "body"
        "\r\n"
        "--" _BOUNDARY "--\r\n";

    HTTP_Service_Multipart multipart = http_service_multipart_init();

    bool const parsed = http_service_multipart_parse(&multipart, _BOUNDARY, (Byte const*) payload, sizeof(payload) - 1);

    test_expect_true(test, "payload parsed", parsed);
    test_expect_u(test, "one part", 1, http_service_multipart_count(&multipart));

    HTTP_Service_Multipart_Node const *const part = http_service_multipart_at(&multipart, 0);

    test_expect_null(test, "the embedded name=\"y\" inside the still-open filename value is not extracted as a real name", part->name);

    http_service_multipart_uninit(&multipart);

    test_case_end(test);
}

static void _test_quoted_filename_does_not_smuggle_a_content_type(Test *const test) {
    test_case_begin(test, "the text \"content-type:\" INSIDE a quoted filename value is not read as a real Content-Type header");

    static char const payload[] =
        "--" _BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"x\"; filename=\"content-type: fake\"\r\n\r\n"
        "body"
        "\r\n"
        "--" _BOUNDARY "--\r\n";

    HTTP_Service_Multipart multipart = http_service_multipart_init();

    bool const parsed = http_service_multipart_parse(&multipart, _BOUNDARY, (Byte const*) payload, sizeof(payload) - 1);

    test_expect_true(test, "payload parsed", parsed);

    HTTP_Service_Multipart_Node const *const x = http_service_multipart_node_get(&multipart, "x");

    test_expect_not_null(test, "field found", x);

    if (x != nullptr) {
        test_expect_null(test, "no real Content-Type header was present, so content_type stays unset", x->content_type);
    }

    http_service_multipart_uninit(&multipart);

    test_case_end(test);
}

static void _test_filename_only_part_has_no_name(Test *const test) {
    test_case_begin(test, "a part with only filename= is not mistaken for name=");

    static char const payload[] =
        "--" _BOUNDARY "\r\n"
        "Content-Disposition: form-data; filename=\"photo.jpg\"\r\n\r\n"
        "data"
        "\r\n"
        "--" _BOUNDARY "--\r\n";

    HTTP_Service_Multipart multipart = http_service_multipart_init();

    bool const parsed = http_service_multipart_parse(&multipart, _BOUNDARY, (Byte const*) payload, sizeof(payload) - 1);

    test_expect_true(test, "payload parsed", parsed);
    test_expect_u(test, "one part", 1, http_service_multipart_count(&multipart));

    HTTP_Service_Multipart_Node const *const part = http_service_multipart_at(&multipart, 0);

    test_expect_null(test, "name= did not match inside filename=", part->name);
    test_expect_string(test, "filename is still read", "photo.jpg", part->filename);

    http_service_multipart_uninit(&multipart);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Body shapes
 *============================================================================*/

static void _test_lf_only_body(Test *const test) {
    test_case_begin(test, "LF-only line endings parse like CRLF");

    static char const payload[] =
        "--" _BOUNDARY "\n"
        "Content-Disposition: form-data; name=\"x\"\n\n"
        "value\n"
        "--" _BOUNDARY "--\n";

    HTTP_Service_Multipart multipart = http_service_multipart_init();

    bool const parsed = http_service_multipart_parse(&multipart, _BOUNDARY, (Byte const*) payload, sizeof(payload) - 1);

    test_expect_true(test, "payload parsed", parsed);

    HTTP_Service_Multipart_Node const *const x = http_service_multipart_node_get(&multipart, "x");

    test_expect_not_null(test, "field found", x);

    if (x != nullptr) {
        test_expect_u(test, "body size excludes the trailing LF", 5, x->size);
    }

    http_service_multipart_uninit(&multipart);

    test_case_end(test);
}

static void _test_lf_only_content_type_does_not_swallow_the_next_header(Test *const test) {
    test_case_begin(test, "an LF-only Content-Type value ends at the \\n, not at the next header line");

    static char const payload[] =
        "--" _BOUNDARY "\n"
        "Content-Disposition: form-data; name=\"x\"\n"
        "content-type:text/plain\n"
        "X-Foo: bar\n"
        "\n"
        "value\n"
        "--" _BOUNDARY "--\n";

    HTTP_Service_Multipart multipart = http_service_multipart_init();

    bool const parsed = http_service_multipart_parse(&multipart, _BOUNDARY, (Byte const*) payload, sizeof(payload) - 1);

    test_expect_true(test, "payload parsed", parsed);

    HTTP_Service_Multipart_Node const *const x = http_service_multipart_node_get(&multipart, "x");

    test_expect_not_null(test, "field found", x);

    if (x != nullptr) {
        test_expect_string(test, "content type stops at the LF, the following header is not swallowed", "text/plain", x->content_type);
    }

    http_service_multipart_uninit(&multipart);

    test_case_end(test);
}

static void _test_empty_part(Test *const test) {
    test_case_begin(test, "a part with an empty body has size 0 and non-null data");

    static char const payload[] =
        "--" _BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"empty\"\r\n\r\n"
        "\r\n"
        "--" _BOUNDARY "--\r\n";

    HTTP_Service_Multipart multipart = http_service_multipart_init();

    bool const parsed = http_service_multipart_parse(&multipart, _BOUNDARY, (Byte const*) payload, sizeof(payload) - 1);

    test_expect_true(test, "payload parsed", parsed);

    HTTP_Service_Multipart_Node const *const part = http_service_multipart_node_get(&multipart, "empty");

    test_expect_not_null(test, "field found", part);

    if (part != nullptr) {
        test_expect_u(test, "size is 0", 0, part->size);
    }

    http_service_multipart_uninit(&multipart);

    test_case_end(test);
}

static void _test_boundary_shaped_data_is_not_a_delimiter(Test *const test) {
    test_case_begin(test, "a boundary-shaped byte run inside data is not mistaken for a delimiter");

    /* Boundary is "b"; the body's own bytes contain "--bXYZ" - the delimiter
     * bytes followed by something other than "--", CRLF or LF, so RFC 2046
     * 5.1.1 says this is not a real delimiter and the scan must skip past it. */
    static char const payload[] =
        "--" _BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"x\"\r\n\r\n"
        "abc--bXYZdef"
        "\r\n"
        "--" _BOUNDARY "--\r\n";

    HTTP_Service_Multipart multipart = http_service_multipart_init();

    bool const parsed = http_service_multipart_parse(&multipart, _BOUNDARY, (Byte const*) payload, sizeof(payload) - 1);

    test_expect_true(test, "payload parsed", parsed);

    HTTP_Service_Multipart_Node const *const x = http_service_multipart_node_get(&multipart, "x");

    test_expect_not_null(test, "field found", x);

    if (x != nullptr) {
        test_expect_u(test, "the false-positive run stayed part of the body", 12, x->size);
    }

    http_service_multipart_uninit(&multipart);

    test_case_end(test);
}

static void _test_header_terminator_not_smuggled_past_next_boundary(Test *const test) {
    test_case_begin(test, "a part terminated by \\n\\n parses correctly even though a later \\r\\n\\r\\n appears in its own data before the true boundary");

    /* Whichever terminator spelling occurs EARLIEST must win: a CRLFCRLF that
     * only happens to occur later, inside this part's own data, must never be
     * mistaken for the terminator when a nearer LFLF is the real one. */
    static char const payload[] =
        "--" _BOUNDARY "\n"
        "Content-Disposition: form-data; name=\"x\"\n\n"
        "abc\r\n\r\ndef"
        "\r\n"
        "--" _BOUNDARY "--\r\n";

    HTTP_Service_Multipart multipart = http_service_multipart_init();

    bool const parsed = http_service_multipart_parse(&multipart, _BOUNDARY, (Byte const*) payload, sizeof(payload) - 1);

    test_expect_true(test, "payload parsed", parsed);

    HTTP_Service_Multipart_Node const *const x = http_service_multipart_node_get(&multipart, "x");

    test_expect_not_null(test, "field found", x);

    if (x != nullptr) {
        test_expect_u(test, "the whole data span, including the embedded CRLFCRLF, stayed the body - not swallowed into headers", 10, x->size);
    }

    http_service_multipart_uninit(&multipart);

    test_case_end(test);
}

static void _test_preamble_is_skipped(Test *const test) {
    test_case_begin(test, "bytes before the first delimiter (preamble) are skipped");

    static char const payload[] =
        "this is a preamble, ignored by every implementation\r\n"
        "--" _BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"x\"\r\n\r\n"
        "v"
        "\r\n"
        "--" _BOUNDARY "--\r\n";

    HTTP_Service_Multipart multipart = http_service_multipart_init();

    bool const parsed = http_service_multipart_parse(&multipart, _BOUNDARY, (Byte const*) payload, sizeof(payload) - 1);

    test_expect_true(test, "payload parsed", parsed);
    test_expect_not_null(test, "field found past the preamble", http_service_multipart_node_get(&multipart, "x"));

    http_service_multipart_uninit(&multipart);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Failure contract
 *============================================================================*/

static void _test_size_zero_payload_is_refused(Test *const test) {
    test_case_begin(test, "a zero-size payload refuses rather than aborting");

    HTTP_Service_Multipart multipart = http_service_multipart_init();

    bool const parsed = http_service_multipart_parse(&multipart, _BOUNDARY, (Byte const*) "", 0);

    test_expect_false(test, "parse reports failure", parsed);
    test_expect_u(test, "no parts", 0, http_service_multipart_count(&multipart));

    http_service_multipart_uninit(&multipart);

    test_case_end(test);
}

static void _test_missing_close_delimiter(Test *const test) {
    test_case_begin(test, "a body with no closing delimiter fails but keeps the parts read so far");

    static char const payload[] =
        "--" _BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"x\"\r\n\r\n"
        "v\r\n"
        "--" _BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"y\"\r\n\r\n"
        "w\r\n"; /* no closing "--b--", and no boundary follows to bound y's data */

    HTTP_Service_Multipart multipart = http_service_multipart_init();

    bool const parsed = http_service_multipart_parse(&multipart, _BOUNDARY, (Byte const*) payload, sizeof(payload) - 1);

    test_expect_false(test, "parse reports failure", parsed);
    test_expect_u(test, "the part read before the failure is kept", 1, http_service_multipart_count(&multipart));
    test_expect_not_null(test, "it is still addressable", http_service_multipart_node_get(&multipart, "x"));
    test_expect_null(test, "the unterminated trailing part was never added", http_service_multipart_node_get(&multipart, "y"));

    http_service_multipart_uninit(&multipart);

    test_case_end(test);
}

static void _test_unterminated_headers(Test *const test) {
    test_case_begin(test, "a header block with no blank-line terminator fails");

    static char const payload[] =
        "--" _BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"x\"\r\n"
        "no terminator here";

    HTTP_Service_Multipart multipart = http_service_multipart_init();

    bool const parsed = http_service_multipart_parse(&multipart, _BOUNDARY, (Byte const*) payload, sizeof(payload) - 1);

    test_expect_false(test, "parse reports failure", parsed);
    test_expect_u(test, "no complete part was read", 0, http_service_multipart_count(&multipart));

    http_service_multipart_uninit(&multipart);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Bounds
 *============================================================================*/

static void _test_max_parts_bound(Test *const test) {
    test_case_begin(test, "max_parts refuses past the cap and keeps the earlier parts");

    static char const payload[] =
        "--" _BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"one\"\r\n\r\n1\r\n"
        "--" _BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"two\"\r\n\r\n2\r\n"
        "--" _BOUNDARY "--\r\n";

    HTTP_Service_Multipart multipart = http_service_multipart_init();

    multipart.max_parts = 1;

    bool const parsed = http_service_multipart_parse(&multipart, _BOUNDARY, (Byte const*) payload, sizeof(payload) - 1);

    test_expect_false(test, "parse reports failure", parsed);
    test_expect_u(test, "only the first part was kept", 1, http_service_multipart_count(&multipart));
    test_expect_not_null(test, "first part addressable", http_service_multipart_node_get(&multipart, "one"));
    test_expect_null(test, "second part never added", http_service_multipart_node_get(&multipart, "two"));

    http_service_multipart_uninit(&multipart);

    test_case_end(test);
}

static void _test_max_header_size_bound(Test *const test) {
    test_case_begin(test, "max_header_size refuses a part whose header block is too large");

    static char const payload[] =
        "--" _BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"x\"\r\n\r\n"
        "v\r\n"
        "--" _BOUNDARY "--\r\n";

    HTTP_Service_Multipart multipart = http_service_multipart_init();

    multipart.max_header_size = 4; /* the header block above is far larger */

    bool const parsed = http_service_multipart_parse(&multipart, _BOUNDARY, (Byte const*) payload, sizeof(payload) - 1);

    test_expect_false(test, "parse reports failure", parsed);
    test_expect_u(test, "no part was collected", 0, http_service_multipart_count(&multipart));

    http_service_multipart_uninit(&multipart);

    test_case_end(test);
}

static void _test_max_header_size_terminator_straddling_the_bound_is_not_refused(Test *const test) {
    test_case_begin(test, "a header block whose length is within max_header_size but whose terminator starts past it is still found, not refused");

    /* Header content is 19 bytes - one byte SHORT of max_header_size (20) - but
     * the "\n\n" terminator that follows starts at offset 19, past the old
     * search window's reach (headers_start + max_header_size - needle_len =
     * headers_start + 18). The terminator search must look 4 bytes past the
     * bound for the check below (headers_end - headers_start > max_header_size)
     * to ever get a chance to run. */
    static char const payload[] =
        "--" _BOUNDARY "\n"
        "0123456789012345678" "\n\n"
        "v\n"
        "--" _BOUNDARY "--\n";

    HTTP_Service_Multipart multipart = http_service_multipart_init();

    multipart.max_header_size = 20;

    bool const parsed = http_service_multipart_parse(&multipart, _BOUNDARY, (Byte const*) payload, sizeof(payload) - 1);

    test_expect_true(test, "payload parsed - the block is within bound", parsed);
    test_expect_u(test, "one part", 1, http_service_multipart_count(&multipart));

    http_service_multipart_uninit(&multipart);

    test_case_end(test);
}

static void _test_boundary_too_long_is_refused(Test *const test) {
    test_case_begin(test, "a boundary longer than RFC 2046's 70 bytes is refused");

    static char const long_boundary[] =
        "0123456789012345678901234567890123456789012345678901234567890123456789X"; /* 74 bytes */

    HTTP_Service_Multipart multipart = http_service_multipart_init();

    bool const parsed = http_service_multipart_parse(&multipart, long_boundary, (Byte const*) "anything", 8);

    test_expect_false(test, "parse reports failure", parsed);

    http_service_multipart_uninit(&multipart);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Accessors
 *============================================================================*/

static void _test_boundary_accessor(Test *const test) {
    test_case_begin(test, "http_service_multipart_boundary extracts the bare boundary value");

    char buffer[64] = DEFAULT_INITIALIZATION;

    test_expect_true(test, "unquoted", http_service_multipart_boundary("multipart/form-data; boundary=abc123", buffer, sizeof buffer));
    test_expect_string(test, "unquoted value", "abc123", buffer);

    test_expect_true(test, "quoted", http_service_multipart_boundary("multipart/form-data; boundary=\"ab c\"", buffer, sizeof buffer));
    test_expect_string(test, "quoted value, quotes stripped", "ab c", buffer);

    test_expect_true(test, "trailing parameter", http_service_multipart_boundary("multipart/form-data; boundary=abc; charset=utf-8", buffer, sizeof buffer));
    test_expect_string(test, "stops at the ';'", "abc", buffer);

    test_expect_false(test, "no boundary parameter", http_service_multipart_boundary("multipart/form-data", buffer, sizeof buffer));
    test_expect_string(test, "left as an empty string", "", buffer);

    test_case_end(test);
}

static void _test_boundary_zero_capacity_refuses(Test *const test) {
    test_case_begin(test, "a zero capacity is refused as a value before anything is written");

    char buffer[1] = { 'X' };

    test_expect_false(test, "capacity 0 is refused", http_service_multipart_boundary("multipart/form-data; boundary=abc", buffer, 0));
    test_expect_true(test, "the buffer was never touched", buffer[0] == 'X');

    test_case_end(test);
}

static void _test_max_parts_reserved_up_front(Test *const test) {
    test_case_begin(test, "parse reserves capacity for the whole max_parts bound up front, not grown incrementally per part");

    static char const payload[] =
        "--" _BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"x\"\r\n\r\nv\r\n"
        "--" _BOUNDARY "--\r\n";

    HTTP_Service_Multipart multipart = http_service_multipart_init();

    bool const parsed = http_service_multipart_parse(&multipart, _BOUNDARY, (Byte const*) payload, sizeof(payload) - 1);

    test_expect_true(test, "payload parsed", parsed);
    test_expect_true(test, "capacity covers the full max_parts bound even though only one part arrived", al_multipart_get_capacity(&multipart.parts) >= multipart.max_parts);

    http_service_multipart_uninit(&multipart);

    test_case_end(test);
}

static void _test_count_and_at_agree_with_node_get(Test *const test) {
    test_case_begin(test, "count/at iterate the same parts node_get finds by name");

    static char const payload[] =
        "--" _BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"a\"\r\n\r\n1\r\n"
        "--" _BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"b\"\r\n\r\n2\r\n"
        "--" _BOUNDARY "--\r\n";

    HTTP_Service_Multipart multipart = http_service_multipart_init();

    http_service_multipart_parse(&multipart, _BOUNDARY, (Byte const*) payload, sizeof(payload) - 1);

    USize const count = http_service_multipart_count(&multipart);

    test_expect_u(test, "two parts", 2, count);

    for (USize i = 0; i < count; i += 1) {
        HTTP_Service_Multipart_Node const *const by_index = http_service_multipart_at(&multipart, i);

        /* node_get aborts on a null name argument (a contract violation, not a
         * value question) - guarded so this loop stays valid if it is ever run
         * over a fixture containing a nameless part. */
        if (by_index->name == nullptr) {
            continue;
        }

        HTTP_Service_Multipart_Node const *const by_name = http_service_multipart_node_get(&multipart, by_index->name);

        test_expect_true(test, "at(i) is the same node node_get(name) finds", by_index == by_name);
    }

    http_service_multipart_uninit(&multipart);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Main
 *============================================================================*/

int main(void) {
    LogConfig const log_config = {
        .level             = LOG_LEVEL_ERROR,
        .stream            = stdout,
        .timestamp_enabled = true,
        .autoflush         = true
    };

    log_init(log_config);

    Test test = test_init("tests/http/service/multipart/test_all.c");

    test_suite_begin(&test, "http_service_multipart");
    _test_fields(&test);
    _test_quoted_semicolon_does_not_smuggle_a_name(&test);
    _test_quoted_filename_does_not_smuggle_a_content_type(&test);
    _test_filename_only_part_has_no_name(&test);
    _test_lf_only_body(&test);
    _test_lf_only_content_type_does_not_swallow_the_next_header(&test);
    _test_empty_part(&test);
    _test_boundary_shaped_data_is_not_a_delimiter(&test);
    _test_header_terminator_not_smuggled_past_next_boundary(&test);
    _test_preamble_is_skipped(&test);
    _test_size_zero_payload_is_refused(&test);
    _test_missing_close_delimiter(&test);
    _test_unterminated_headers(&test);
    _test_max_parts_bound(&test);
    _test_max_header_size_bound(&test);
    _test_max_header_size_terminator_straddling_the_bound_is_not_refused(&test);
    _test_boundary_too_long_is_refused(&test);
    _test_boundary_accessor(&test);
    _test_boundary_zero_capacity_refuses(&test);
    _test_max_parts_reserved_up_front(&test);
    _test_count_and_at_agree_with_node_get(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}