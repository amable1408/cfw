/*
 * test_all.c - the http/service/email suite.
 *
 * There is no mail server here and none is needed. The module's whole
 * observable surface below the socket is http_service_email_payload_create_2,
 * which takes the clock, the Message-ID and the MIME boundary as a CALLER-
 * SUPPLIED render context; with that context fixed the payload is byte-exact,
 * so the goldens below pin the real wire bytes - CRLF everywhere, the Date and
 * Message-ID that used to be missing entirely, the quoted-printable bodies, and
 * the RFC 2047 B-encoded Spanish subject. The capture transport pins the other
 * half: the SMTP envelope, and the invariant that a Bcc recipient appears as a
 * RCPT TO and NOWHERE in the rendered headers.
 *
 * A live SMTP dialogue (curl's dot-stuffing, the CRLF.CRLF terminator, the
 * `long` option widths) needs an FIXTURE_SCRIPT_SMTP in tests/http/client's
 * fixture, which another pass owns; the render seam covers the rest.
 *
 * test_unchecked.c holds the half this build cannot observe: every refusal here
 * is an ordinary runtime branch, not an artifact of ERROR_CHECK_ENABLED.
 */
#include <stdio.h>

#include <arena/arena.h>
#include <encoding/quoted_printable/quoted_printable.h>
#include <env/env.h>
#include <http/service/email/email.h>
#include <log/log.h>
#include <test/test.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/
#define _FIXED_BOUNDARY     "=_cfw_testboundary"
#define _FIXED_MESSAGE_ID   "<0123456789abcdef0123456789abcdef@app.test>"
#define _FROM               "noreply@app.test"

/* 90 octets, no space anywhere in it: 29 of scheme and host, 60 of opaque token,
 * and the closing '>'. The ordinary shape of a List-Unsubscribe value, and the
 * exact input a 78-column hard split corrupts. */
#define _UNSUBSCRIBE_URL    "<https://mail.example.test/u/abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghij>"

#define _URL                "smtp://relay.example.com:587"

/*==============================================================================
 * MARK: - Helpers
 *============================================================================*/

/* Substring search over a String's bytes. The suite needs both "contains" and
 * "does NOT contain", and test_expect_string_contains only answers the first. */
static bool _contains(String const *const haystack, char const *const needle) {
    USize const haystack_size   = string_get_size(haystack);
    USize const needle_size     = char_length(needle);

    if (needle_size == 0 || needle_size > haystack_size) {
        return false;
    }

    char const *const data = string_get_data(haystack);

    for (USize i = 0; i + needle_size <= haystack_size; i += 1) {
        bool same = true;

        for (USize j = 0; same && j < needle_size; j += 1) {
            same = data[i + j] == needle[j];
        }

        if (same) {
            return true;
        }
    }

    return false;
}

static HTTP_Service_Email_Render _render_fixed(void) {
    HTTP_Service_Email_Render render = DEFAULT_INITIALIZATION;

    /* Datetime.month is documented [0, 11], so 8 is September - the Date header
     * below reads "06 Sep 2026". */
    render.date = datetime_init_5(2026, 8, 6, 12, 0, 0);

    memory_copy_2(render.boundary, sizeof(render.boundary), _FIXED_BOUNDARY, CHAR_STATIC_SIZE(_FIXED_BOUNDARY));
    memory_copy_2(render.message_id, sizeof(render.message_id), _FIXED_MESSAGE_ID, CHAR_STATIC_SIZE(_FIXED_MESSAGE_ID));

    return render;
}

/*==============================================================================
 * MARK: - Cases
 *============================================================================*/

static void _test_golden_plain(Test *const test) {
    test_case_begin(test, "a plain text message renders byte-exact RFC 5322");

    HTTP_Service_Email          email   = DEFAULT_INITIALIZATION;
    HTTP_Service_Email_Message  message = DEFAULT_INITIALIZATION;

    test_expect_true(test, "the service initializes", http_service_email_init_2(&email, _URL, "", "", _FROM));
    test_expect_true(test, "the message initializes", http_service_email_message_init_1(&message));
    test_expect_true(test, "the recipient is stored", http_service_email_message_to_add_1(&message, "person@example.com"));
    test_expect_true(test, "the subject is stored", http_service_email_message_subject_set(&message, "Verify"));

    /* Bare LF on the way in - the shape Gmail bounces 5.7.1 and curl never
     * dot-stuffs - must come back out as CRLF. */
    test_expect_true(test, "the body is stored", http_service_email_message_text_set(&message, "Hello there.\nBye."));

    HTTP_Service_Email_Render   const   render  = _render_fixed();
    String                              payload = DEFAULT_INITIALIZATION;

    test_expect_true(test, "the payload is built", http_service_email_payload_create_2(&email, &message, &render, &payload));
    test_expect_string(test, "the payload is byte-exact",
        "From: noreply@app.test\r\n"
        "To: person@example.com\r\n"
        "Subject: Verify\r\n"
        "Date: 06 Sep 2026 12:00:00 +0000\r\n"
        "Message-ID: " _FIXED_MESSAGE_ID "\r\n"
        "MIME-Version: 1.0\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Content-Transfer-Encoding: quoted-printable\r\n"
        "\r\n"
        "Hello there.\r\n"
        "Bye.\r\n",
        string_get_data(&payload));

    string_uninit(&payload);
    http_service_email_message_uninit(&message);
    http_service_email_uninit(&email);

    test_case_end(test);
}

static void _test_golden_spanish_subject(Test *const test) {
    test_case_begin(test, "a Spanish subject is RFC 2047 B-encoded and the body quoted-printable");

    HTTP_Service_Email          email   = DEFAULT_INITIALIZATION;
    HTTP_Service_Email_Message  message = DEFAULT_INITIALIZATION;

    test_expect_true(test, "the service initializes", http_service_email_init_2(&email, _URL, "", "", _FROM));
    test_expect_true(test, "the message initializes", http_service_email_message_init_1(&message));
    test_expect_true(test, "the recipient is stored", http_service_email_message_to_add_1(&message, "person@example.com"));

    /* "Sábado" in UTF-8: 53 C3 A1 62 61 64 6F. */
    test_expect_true(test, "the subject is stored", http_service_email_message_subject_set(&message, "S\xc3\xa1""bado"));
    test_expect_true(test, "the body is stored", http_service_email_message_text_set(&message, "S\xc3\xa1""bado"));

    HTTP_Service_Email_Render   const   render  = _render_fixed();
    String                              payload = DEFAULT_INITIALIZATION;

    test_expect_true(test, "the payload is built", http_service_email_payload_create_2(&email, &message, &render, &payload));
    test_expect_string(test, "the subject is an encoded word and the body is quoted-printable",
        "From: noreply@app.test\r\n"
        "To: person@example.com\r\n"
        "Subject: =?UTF-8?B?U8OhYmFkbw==?=\r\n"
        "Date: 06 Sep 2026 12:00:00 +0000\r\n"
        "Message-ID: " _FIXED_MESSAGE_ID "\r\n"
        "MIME-Version: 1.0\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Content-Transfer-Encoding: quoted-printable\r\n"
        "\r\n"
        "S=C3=A1bado\r\n",
        string_get_data(&payload));

    string_uninit(&payload);
    http_service_email_message_uninit(&message);
    http_service_email_uninit(&email);

    test_case_end(test);
}

static void _test_golden_multipart(Test *const test) {
    test_case_begin(test, "text plus html renders multipart/alternative around the random boundary");

    HTTP_Service_Email          email   = DEFAULT_INITIALIZATION;
    HTTP_Service_Email_Message  message = DEFAULT_INITIALIZATION;

    test_expect_true(test, "the service initializes", http_service_email_init_2(&email, _URL, "", "", _FROM));
    test_expect_true(test, "the message initializes", http_service_email_message_init_1(&message));
    test_expect_true(test, "the recipient is stored", http_service_email_message_to_add_1(&message, "person@example.com"));
    test_expect_true(test, "the copy recipient is stored", http_service_email_message_cc_add_1(&message, "watcher@example.com"));
    test_expect_true(test, "the reply-to is stored", http_service_email_message_reply_to_set(&message, "support@app.test"));
    test_expect_true(test, "the display name is stored", http_service_email_message_from_name_set(&message, "App, Support"));
    test_expect_true(test, "the custom header is stored", http_service_email_message_header_add(&message, "X-Campaign: verify"));
    test_expect_true(test, "the text body is stored", http_service_email_message_text_set(&message, "plain"));
    test_expect_true(test, "the html body is stored", http_service_email_message_html_set(&message, "<p>rich</p>"));

    HTTP_Service_Email_Render   const   render  = _render_fixed();
    String                              payload = DEFAULT_INITIALIZATION;

    test_expect_true(test, "the payload is built", http_service_email_payload_create_2(&email, &message, &render, &payload));
    test_expect_string(test, "every part is present and the boundary is the render context's",
        "From: \"App, Support\" <noreply@app.test>\r\n"
        "To: person@example.com\r\n"
        "Cc: watcher@example.com\r\n"
        "Reply-To: support@app.test\r\n"
        "Date: 06 Sep 2026 12:00:00 +0000\r\n"
        "Message-ID: " _FIXED_MESSAGE_ID "\r\n"
        "MIME-Version: 1.0\r\n"
        "X-Campaign: verify\r\n"
        "Content-Type: multipart/alternative; boundary=\"" _FIXED_BOUNDARY "\"\r\n"
        "\r\n"
        "--" _FIXED_BOUNDARY "\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Content-Transfer-Encoding: quoted-printable\r\n"
        "\r\n"
        "plain\r\n"
        "--" _FIXED_BOUNDARY "\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Transfer-Encoding: quoted-printable\r\n"
        "\r\n"
        "<p>rich</p>\r\n"
        "--" _FIXED_BOUNDARY "--\r\n",
        string_get_data(&payload));

    string_uninit(&payload);
    http_service_email_message_uninit(&message);
    http_service_email_uninit(&email);

    test_case_end(test);
}

static void _test_capture_transport(Test *const test) {
    test_case_begin(test, "the capture transport records the envelope and keeps Bcc out of the headers");

    HTTP_Service_Email          email   = DEFAULT_INITIALIZATION;
    HTTP_Service_Email_Message  message = DEFAULT_INITIALIZATION;
    String                      sink    = string_init_1();

    test_expect_true(test, "the service initializes", http_service_email_init_2(&email, _URL, "", "", _FROM));
    test_expect_true(test, "the capture transport is installed", http_service_email_transport_capture_set(&email, &sink));
    test_expect_true(test, "the message initializes", http_service_email_message_init_1(&message));
    test_expect_true(test, "the recipient is stored", http_service_email_message_to_add_1(&message, "person@example.com"));
    test_expect_true(test, "the blind recipient is stored", http_service_email_message_bcc_add_1(&message, "hidden@example.com"));
    test_expect_true(test, "the body is stored", http_service_email_message_text_set(&message, "hello"));

    HTTP_Service_Email_Result result = http_service_email_send(&email, &message);

    test_expect_true(test, "the send succeeds without dialing anything", result.success);
    test_expect_i(test, "the status is OK", HTTP_SERVICE_EMAIL_STATUS_OK, result.status);
    test_expect_true(test, "the envelope sender is recorded", _contains(&sink, "MAIL FROM:<noreply@app.test>\r\n"));
    test_expect_true(test, "the primary recipient is an envelope RCPT", _contains(&sink, "RCPT TO:<person@example.com>\r\n"));

    /* The whole point of Bcc: an envelope recipient every other recipient's mail
     * client must never learn about. */
    test_expect_true(test, "the blind recipient is an envelope RCPT", _contains(&sink, "RCPT TO:<hidden@example.com>\r\n"));
    test_expect_false(test, "the blind recipient is NOT in the headers", _contains(&sink, "Bcc:"));
    test_expect_false(test, "the blind address appears only once, in the envelope", _contains(&sink, "To: hidden@example.com"));
    test_expect_true(test, "the DATA stage and its terminator are recorded", _contains(&sink, "DATA\r\n"));
    test_expect_true(test, "the payload is inside the DATA stage", _contains(&sink, "MIME-Version: 1.0\r\n"));

    http_service_email_result_uninit(&result);
    http_service_email_message_uninit(&message);
    http_service_email_uninit(&email);
    string_uninit(&sink);

    test_case_end(test);
}

/* email.h promises the capture transcript is "exactly what would go on the
 * wire", and email.h also says the PAYLOAD is not dot-stuffed because libcurl
 * stuffs on upload. Both cannot be true of one buffer, and the half that was
 * wrong was the transcript: a body line beginning with '.' reaches a real relay
 * stuffed, and an unstuffed "." line ENDS the DATA block early. */
static void _test_capture_dot_stuffing(Test *const test) {
    test_case_begin(test, "the capture transcript dot-stuffs the payload, exactly as the SMTP upload does");

    HTTP_Service_Email          email   = DEFAULT_INITIALIZATION;
    HTTP_Service_Email_Message  message = DEFAULT_INITIALIZATION;
    String                      sink    = string_init_1();

    test_expect_true(test, "the service initializes", http_service_email_init_2(&email, _URL, "", "", _FROM));
    test_expect_true(test, "the message initializes", http_service_email_message_init_1(&message));
    test_expect_true(test, "the recipient is stored", http_service_email_message_to_add_1(&message, "person@example.com"));

    /* A body a caller does not control - a CRM reminder note - beginning a line
     * with '.', and one line that IS a bare '.'. */
    test_expect_true(test, "the body is stored", http_service_email_message_text_set(&message, "hello\n.hidden\n.\nbye"));

    HTTP_Service_Email_Render   const   render  = _render_fixed();
    String                              payload = DEFAULT_INITIALIZATION;

    test_expect_true(test, "the raw payload is built", http_service_email_payload_create_2(&email, &message, &render, &payload));

    /* payload_create_2 stays UNSTUFFED: it is the message, not the dialogue, and
     * the header says so for a caller piping it into sendmail itself. */
    test_expect_true(test, "the raw payload keeps the caller's leading dot", _contains(&payload, "\r\n.hidden\r\n"));
    test_expect_true(test, "and the caller's bare dot line", _contains(&payload, "\r\n.\r\nbye"));

    string_uninit(&payload);

    test_expect_true(test, "the capture transport is installed", http_service_email_transport_capture_set(&email, &sink));

    HTTP_Service_Email_Result result = http_service_email_send(&email, &message);

    test_expect_true(test, "the capture send succeeds", result.success);
    test_expect_true(test, "the transcript stuffs the leading dot", _contains(&sink, "\r\n..hidden\r\n"));
    test_expect_true(test, "and the bare dot line, which would otherwise END the DATA block", _contains(&sink, "\r\n..\r\nbye"));
    test_expect_false(test, "so no unstuffed body dot survives into the transcript", _contains(&sink, "\r\n.hidden"));
    test_expect_true(test, "while the real terminator is still a bare dot line", _contains(&sink, "bye\r\n.\r\n"));

    http_service_email_result_uninit(&result);
    http_service_email_message_uninit(&message);
    http_service_email_uninit(&email);
    string_uninit(&sink);

    test_case_end(test);
}

static void _test_send_not_configured(Test *const test) {
    test_case_begin(test, "a send through an unconfigured service says so before dialing");

    HTTP_Service_Email          email   = DEFAULT_INITIALIZATION;
    HTTP_Service_Email_Message  message = DEFAULT_INITIALIZATION;

    test_expect_true(test, "the default service initializes", http_service_email_init_1(&email));
    test_expect_false(test, "but it is not valid to send with", http_service_email_valid(&email));
    test_expect_true(test, "the message initializes", http_service_email_message_init_1(&message));
    test_expect_true(test, "the recipient is stored", http_service_email_message_to_add_1(&message, "person@example.com"));
    test_expect_true(test, "the body is stored", http_service_email_message_text_set(&message, "hello"));

    HTTP_Service_Email_Result result = http_service_email_send(&email, &message);

    test_expect_false(test, "the send fails", result.success);
    test_expect_i(test, "and names configuration, not the relay", HTTP_SERVICE_EMAIL_STATUS_NOT_CONFIGURED, result.status);
    test_expect_i(test, "with no CURLcode, because curl was never reached", 0, result.curl_code);

    http_service_email_result_uninit(&result);
    http_service_email_message_uninit(&message);
    http_service_email_uninit(&email);

    test_case_end(test);
}

static void _test_address_valid_table(Test *const test) {
    test_case_begin(test, "the address table");

    test_expect_true(test, "a minimal address is valid", http_service_email_address_valid("a@b"));
    test_expect_true(test, "an ordinary address is valid", http_service_email_address_valid("person.name+tag@example.co.uk"));
    test_expect_false(test, "an empty address is refused", http_service_email_address_valid(""));
    test_expect_false(test, "no '@' is refused", http_service_email_address_valid("person.example.com"));
    test_expect_false(test, "two '@' are refused", http_service_email_address_valid("a@@b"));
    test_expect_false(test, "an empty local part is refused", http_service_email_address_valid("@example.com"));
    test_expect_false(test, "an empty domain is refused", http_service_email_address_valid("person@"));
    test_expect_false(test, "an embedded space is refused", http_service_email_address_valid("per son@example.com"));
    test_expect_false(test, "a comma is refused", http_service_email_address_valid("a@b,c@d"));
    test_expect_false(test, "angle brackets are refused", http_service_email_address_valid("<a@b>"));
    test_expect_false(test, "a CR is refused", http_service_email_address_valid("a@b\rBcc: c@d"));

    /* SMTPUTF8 is never negotiated by this transport, so a non-ASCII address
     * would produce a message the relay rejects rather than one that works. */
    test_expect_false(test, "a non-ASCII address is refused", http_service_email_address_valid("\xc3\xa1@example.com"));

    test_case_end(test);
}

static void _test_refusal_table(Test *const test) {
    test_case_begin(test, "the refusal table");

    HTTP_Service_Email          email   = DEFAULT_INITIALIZATION;
    HTTP_Service_Email_Message  message = DEFAULT_INITIALIZATION;

    test_expect_false(test, "a non-SMTP scheme is refused whole", http_service_email_init_2(&email, "http://relay.test", "", "", _FROM));
    test_expect_false(test, "an empty URL is refused whole", http_service_email_init_2(&email, "", "", "", _FROM));

    /* curl echoes the URL into its error buffer and consumers log result.error,
     * so credentials in the URL end up in a log file. */
    test_expect_false(test, "userinfo in the URL is refused whole", http_service_email_init_2(&email, "smtp://user:pass@relay.test:587", "", "", _FROM));
    test_expect_false(test, "a sender that is not an address is refused whole", http_service_email_init_2(&email, _URL, "", "", "noreply"));
    test_expect_true(test, "a good configuration initializes", http_service_email_init_2(&email, _URL, "user", "secret", _FROM));
    test_expect_true(test, "and is valid", http_service_email_valid(&email));

    test_expect_false(test, "a zero connect timeout is refused as a value", http_service_email_timeouts_set(&email, 0, 10000));
    test_expect_false(test, "a zero total timeout is refused as a value", http_service_email_timeouts_set(&email, 3000, 0));
    test_expect_true(test, "the previous timeouts stand", http_service_email_valid(&email));
    test_expect_true(test, "non-zero timeouts are stored", http_service_email_timeouts_set(&email, 1000, 2000));
    test_expect_false(test, "a non-SMTP URL is refused by the setter", http_service_email_url_set(&email, "ftp://relay.test"));
    test_expect_false(test, "a sender that is not an address is refused by the setter", http_service_email_from_set(&email, "nope"));
    test_expect_false(test, "a CR in the display name is refused", http_service_email_from_name_set(&email, "App\r\nBcc: x@y"));

    test_expect_true(test, "the message initializes", http_service_email_message_init_1(&message));
    test_expect_false(test, "an empty recipient is refused", http_service_email_message_to_add_1(&message, ""));
    test_expect_false(test, "a malformed recipient is refused", http_service_email_message_to_add_1(&message, "person"));
    test_expect_false(test, "a CR in the subject is refused", http_service_email_message_subject_set(&message, "Hi\r\nBcc: x@y"));

    test_expect_true(test, "an ordinary custom header is stored", http_service_email_message_header_add(&message, "X-Campaign: verify"));
    test_expect_true(test, "an empty-value custom header is stored", http_service_email_message_header_add(&message, "X-Empty:"));
    test_expect_false(test, "a colon-less line is refused", http_service_email_message_header_add(&message, "X-Campaign verify"));
    test_expect_false(test, "a leading-whitespace line is refused", http_service_email_message_header_add(&message, " X-Campaign: verify"));
    test_expect_false(test, "an empty name is refused", http_service_email_message_header_add(&message, ": verify"));
    test_expect_false(test, "a space inside the name is refused", http_service_email_message_header_add(&message, "X Campaign: verify"));

    /* A caller-supplied duplicate does not REPLACE the module's line, it adds a
     * second one - and a second Bcc or Content-Type rewrites the message. */
    test_expect_false(test, "a module-owned Bcc is refused", http_service_email_message_header_add(&message, "Bcc: hidden@example.com"));
    test_expect_false(test, "a module-owned Content-Type is refused, case-insensitively", http_service_email_message_header_add(&message, "content-type: text/plain"));
    test_expect_false(test, "a module-owned Date is refused", http_service_email_message_header_add(&message, "Date: yesterday"));

    /* payload_create_2 renders Reply-To from reply_to_set, so a caller line here
     * would ADD a second one and a receiver picking either address routes the
     * reply somewhere the sender never chose. */
    test_expect_false(test, "a module-owned Reply-To is refused", http_service_email_message_header_add(&message, "Reply-To: attacker@example.com"));
    test_expect_false(test, "and case-insensitively", http_service_email_message_header_add(&message, "reply-to: attacker@example.com"));

    test_expect_false(test, "an embedded CRLF is refused", http_service_email_message_header_add(&message, "X-A: 1\r\nBcc: x@y"));

    test_expect_false(test, "a message with no body is invalid", http_service_email_message_valid(&email, &message));
    test_expect_true(test, "the body is stored", http_service_email_message_text_set(&message, "hello"));
    test_expect_false(test, "a message with no recipient is still invalid", http_service_email_message_valid(&email, &message));
    test_expect_true(test, "the recipient is stored", http_service_email_message_to_add_1(&message, "person@example.com"));
    test_expect_true(test, "and now it is valid", http_service_email_message_valid(&email, &message));

    http_service_email_message_uninit(&message);
    http_service_email_uninit(&email);

    test_case_end(test);
}

static void _test_quoted_printable_encoder(Test *const test) {
    test_case_begin(test, "the quoted-printable encoder");

    String output = string_init_1();

    test_expect_true(test, "printable ASCII passes through", encoding_quoted_printable_encode_2("Hello", CHAR_STATIC_SIZE("Hello"), &output));
    test_expect_string(test, "unchanged", "Hello", string_get_data(&output));

    string_clear(&output);

    test_expect_true(test, "'=' is escaped", encoding_quoted_printable_encode_2("a=b", CHAR_STATIC_SIZE("a=b"), &output));
    test_expect_string(test, "as =3D", "a=3Db", string_get_data(&output));

    string_clear(&output);

    test_expect_true(test, "UTF-8 is escaped byte by byte", encoding_quoted_printable_encode_2("S\xc3\xa1", 3, &output));
    test_expect_string(test, "in uppercase hex", "S=C3=A1", string_get_data(&output));

    string_clear(&output);

    /* A transport is allowed to strip trailing whitespace, so an unescaped one
     * is silent data loss. */
    test_expect_true(test, "a trailing space is escaped", encoding_quoted_printable_encode_2("abc ", CHAR_STATIC_SIZE("abc "), &output));
    test_expect_string(test, "as =20", "abc=20", string_get_data(&output));

    string_clear(&output);

    test_expect_true(test, "a space before a line break is escaped", encoding_quoted_printable_encode_2("a \r\nb", 5, &output));
    test_expect_string(test, "and the CRLF is preserved", "a=20\r\nb", string_get_data(&output));

    string_clear(&output);

    test_expect_true(test, "an interior space passes through", encoding_quoted_printable_encode_2("a b", CHAR_STATIC_SIZE("a b"), &output));
    test_expect_string(test, "unchanged", "a b", string_get_data(&output));

    string_clear(&output);

    /* A CRLF pair is the only line break this encoding recognizes; a lone LF is
     * a control byte, and encoding it is what keeps the output line-safe. */
    test_expect_true(test, "a lone LF is escaped", encoding_quoted_printable_encode_2("a\nb", 3, &output));
    test_expect_string(test, "as =0A", "a=0Ab", string_get_data(&output));

    string_clear(&output);

    char long_line[201] = DEFAULT_INITIALIZATION;

    memory_set(long_line, sizeof(long_line) - 1, 'a');

    test_expect_true(test, "a 200-character run wraps", encoding_quoted_printable_encode_2(long_line, 200, &output));

    /* 75 characters plus the soft break's '=' is exactly the RFC's 76-column
     * line; 200 characters is 75 + 75 + 50 with two soft breaks. */
    test_expect_u(test, "at 76 columns per line", 75 + 3 + 75 + 3 + 50, string_get_size(&output));
    test_expect_true(test, "the first line ends with the soft-break '='", string_get_data(&output)[75] == '=');
    test_expect_true(test, "followed by CRLF", string_get_data(&output)[76] == '\r' && string_get_data(&output)[77] == '\n');

    string_clear(&output);

    test_expect_u(test, "encode_size agrees with the encoder", 6, encoding_quoted_printable_encode_size("abc ", CHAR_STATIC_SIZE("abc ")));
    test_expect_u(test, "an empty input encodes to nothing", 0, encoding_quoted_printable_encode_size("", 0));
    test_expect_true(test, "and appends nothing", encoding_quoted_printable_encode_2("", 0, &output));
    test_expect_u(test, "leaving the sink empty", 0, string_get_size(&output));

    char    small[4]        = DEFAULT_INITIALIZATION;
    USize   encoded_size    = 0;

    test_expect_true(test, "a buffer too small refuses instead of overrunning",
        result_is_error(encoding_quoted_printable_encode_1("abcdefgh", CHAR_STATIC_SIZE("abcdefgh"), small, sizeof(small), &encoded_size)));
    test_expect_u(test, "and leaves encoded_size untouched", 0, encoded_size);

    char exact[8] = DEFAULT_INITIALIZATION;

    test_expect_true(test, "an exactly-sized buffer succeeds",
        result_is_success(encoding_quoted_printable_encode_1("abcdefgh", CHAR_STATIC_SIZE("abcdefgh"), exact, sizeof(exact), &encoded_size)));
    test_expect_u(test, "writing every character", 8, encoded_size);

    string_uninit(&output);

    test_case_end(test);
}

static void _test_long_header_folding(Test *const test) {
    test_case_begin(test, "long header values fold instead of passing the 998-octet limit");

    HTTP_Service_Email          email   = DEFAULT_INITIALIZATION;
    HTTP_Service_Email_Message  message = DEFAULT_INITIALIZATION;

    test_expect_true(test, "the service initializes", http_service_email_init_2(&email, _URL, "", "", _FROM));
    test_expect_true(test, "the message initializes", http_service_email_message_init_1(&message));
    test_expect_true(test, "the body is stored", http_service_email_message_text_set(&message, "hello"));
    test_expect_true(test, "the subject is stored",
        http_service_email_message_subject_set(&message,
            "one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen"));

    for (USize i = 0; i < 6; i += 1) {
        char address[64] = DEFAULT_INITIALIZATION;

        snprintf(address, sizeof(address), "recipient.number.%llu@example-domain.test", (unsigned long long) i);

        test_expect_true(test, "each recipient is stored", http_service_email_message_to_add_1(&message, address));
    }

    HTTP_Service_Email_Render   const   render  = _render_fixed();
    String                              payload = DEFAULT_INITIALIZATION;

    test_expect_true(test, "the payload is built", http_service_email_payload_create_2(&email, &message, &render, &payload));

    char    const   *const  data        = string_get_data(&payload);
    USize   const           size        = string_get_size(&payload);
    USize                   line_length = 0;
    USize                   longest     = 0;

    for (USize i = 0; i < size; i += 1) {
        if (data[i] == '\n') {
            line_length = 0;
        }
        else if (data[i] != '\r') {
            line_length += 1;

            longest = line_length > longest ? line_length : longest;
        }
    }

    test_expect_true(test, "no rendered line passes 78 columns", longest <= HTTP_SERVICE_EMAIL_HEADER_LINE_MAX_LENGTH);
    test_expect_true(test, "the To: list folded onto a continuation line", _contains(&payload, ",\r\n recipient.number."));
    test_expect_true(test, "the subject folded onto a continuation line", _contains(&payload, "\r\n "));

    string_uninit(&payload);
    http_service_email_message_uninit(&message);
    http_service_email_uninit(&email);

    test_case_end(test);
}

/* Folding at spaces only helps when there IS a space. A single unbroken token -
 * a signed URL, a base64 tracking id - and a custom header line went out whole,
 * one line well past RFC 5322's 998-octet hard limit, which an MTA answers 5xx.
 *
 * The split point is the HARD limit, not the 78-column recommendation, and that
 * distinction is the point of this case. Unfolding removes the CRLF and KEEPS
 * the WSP (RFC 5322 §2.2.3), so a token split at 78 arrives at the receiver with
 * a space inside it - the List-Unsubscribe pin below is exactly that defect. */
static void _test_unbreakable_word_folding(Test *const test) {
    test_case_begin(test, "a word with nowhere to fold is hard-split, and custom headers fold too");

    HTTP_Service_Email          email   = DEFAULT_INITIALIZATION;
    HTTP_Service_Email_Message  message = DEFAULT_INITIALIZATION;

    test_expect_true(test, "the service initializes", http_service_email_init_2(&email, _URL, "", "", _FROM));
    test_expect_true(test, "the message initializes", http_service_email_message_init_1(&message));
    test_expect_true(test, "the recipient is stored", http_service_email_message_to_add_1(&message, "person@example.com"));
    test_expect_true(test, "the body is stored", http_service_email_message_text_set(&message, "hello"));

    char subject[1001]  = DEFAULT_INITIALIZATION;
    char header[1213]   = DEFAULT_INITIALIZATION;

    /* 1000 bytes with not one space in them. */
    for (USize i = 0; i < sizeof(subject) - 1; i += 1) {
        subject[i] = 'S';
    }

    memory_copy_2(header, sizeof(header), "X-Trace:", CHAR_STATIC_SIZE("X-Trace:"));

    /* 1200 header-safe value bytes past a legal "Name:" - every byte of it is
     * accepted by header_add, so only the RENDERER can keep the line legal. */
    for (USize i = CHAR_STATIC_SIZE("X-Trace:"); i < sizeof(header) - 1; i += 1) {
        header[i] = 'h';
    }

    test_expect_true(test, "the 1000-byte unbroken subject is stored", http_service_email_message_subject_set(&message, subject));
    test_expect_true(test, "the 1200-byte custom header is stored", http_service_email_message_header_add(&message, header));

    /* An ORDINARY 90-character List-Unsubscribe URL: 18 columns of header name
     * and a 90-byte token with not one space in it. */
    test_expect_true(test, "the 90-character List-Unsubscribe is stored",
        http_service_email_message_header_add(&message, "List-Unsubscribe: " _UNSUBSCRIBE_URL));

    HTTP_Service_Email_Render   const   render          = _render_fixed();
    String                              payload         = DEFAULT_INITIALIZATION;

    test_expect_true(test, "the payload is built", http_service_email_payload_create_2(&email, &message, &render, &payload));

    char    const   *const  data            = string_get_data(&payload);
    USize   const           size            = string_get_size(&payload);
    USize                   line_length     = 0;
    USize                   longest         = 0;
    USize                   subject_run     = 0;
    USize                   subject_longest = 0;

    for (USize i = 0; i < size; i += 1) {
        if (data[i] == '\n') {
            line_length = 0;
        }
        else if (data[i] != '\r') {
            line_length += 1;

            longest = line_length > longest ? line_length : longest;
        }

        subject_run     = data[i] == 'S' ? subject_run + 1 : 0;
        subject_longest = subject_run > subject_longest ? subject_run : subject_longest;
    }

    test_expect_true(test, "no rendered line passes the 998-octet hard limit", longest <= 998);

    /* A line IS allowed past the 78-column recommendation now - that is what
     * splitting only at the hard limit means. */
    test_expect_true(test, "but a value with nowhere to fold does pass 78 columns", longest > HTTP_SERVICE_EMAIL_HEADER_LINE_MAX_LENGTH);
    test_expect_true(test, "the 1000-byte subject was still split, not emitted whole", subject_longest < sizeof(subject) - 1);
    test_expect_u(test, "at the hard limit exactly, not one column earlier", 998 - CHAR_STATIC_SIZE("Subject: "), subject_longest);
    test_expect_true(test, "the custom header folded onto a continuation line", _contains(&payload, "h\r\n h"));
    test_expect_true(test, "and every subject byte survived the split", _contains(&payload, "Subject: SS"));

    /* THE High 1 pin. Split at 78 this URL comes back to the receiver with a
     * space in the middle of it, because unfolding keeps the WSP - a broken
     * link, from a value the module accepted whole. It is 90 octets, far under
     * the limit an MTA actually refuses, so it must survive INTACT. */
    test_expect_true(test, "the 90-character URL is emitted whole, with no fold inside it", _contains(&payload, _UNSUBSCRIBE_URL));
    test_expect_false(test, "so the bytes a 78-column split would have separated stay adjacent", _contains(&payload, "abcdefgh\r\n ijabcdefghij>"));

    string_uninit(&payload);
    http_service_email_message_uninit(&message);
    http_service_email_uninit(&email);

    test_case_end(test);
}

/* A render context is CALLER-supplied, so its two char arrays are data: an
 * unterminated array runs char_length off the end of the struct, and a CRLF in
 * the Message-ID injects a header into a message every other surface here
 * refuses to let a caller inject into. */
static void _test_render_context_refusal(Test *const test) {
    test_case_begin(test, "a malformed render context is refused, not rendered");

    HTTP_Service_Email          email   = DEFAULT_INITIALIZATION;
    HTTP_Service_Email_Message  message = DEFAULT_INITIALIZATION;

    test_expect_true(test, "the service initializes", http_service_email_init_2(&email, _URL, "", "", _FROM));
    test_expect_true(test, "the message initializes", http_service_email_message_init_1(&message));
    test_expect_true(test, "the recipient is stored", http_service_email_message_to_add_1(&message, "person@example.com"));
    test_expect_true(test, "the body is stored", http_service_email_message_text_set(&message, "hello"));

    String payload = DEFAULT_INITIALIZATION;

    HTTP_Service_Email_Render injected = _render_fixed();

    memory_copy_2(injected.message_id, sizeof(injected.message_id),
        "<a@app.test>\r\nBcc: hidden@example.com", CHAR_STATIC_SIZE("<a@app.test>\r\nBcc: hidden@example.com"));

    test_expect_false(test, "a CRLF-bearing Message-ID is refused", http_service_email_payload_create_2(&email, &message, &injected, &payload));
    test_expect_u(test, "and the payload is left EMPTY", 0, string_get_size(&payload));

    HTTP_Service_Email_Render unterminated = _render_fixed();

    for (USize i = 0; i < sizeof(unterminated.boundary); i += 1) {
        unterminated.boundary[i] = 'b';
    }

    test_expect_false(test, "a boundary with no terminator inside its capacity is refused",
        http_service_email_payload_create_2(&email, &message, &unterminated, &payload));
    test_expect_u(test, "and the payload is still EMPTY", 0, string_get_size(&payload));

    HTTP_Service_Email_Render quoted = _render_fixed();

    memory_copy_2(quoted.boundary, sizeof(quoted.boundary), "=_cfw_a\"b", CHAR_STATIC_SIZE("=_cfw_a\"b"));

    test_expect_false(test, "a boundary carrying a quote is refused", http_service_email_payload_create_2(&email, &message, &quoted, &payload));

    HTTP_Service_Email_Render spaced = _render_fixed();

    memory_copy_2(spaced.boundary, sizeof(spaced.boundary), "=_cfw_a b", CHAR_STATIC_SIZE("=_cfw_a b"));

    test_expect_false(test, "a boundary carrying a space is refused", http_service_email_payload_create_2(&email, &message, &spaced, &payload));

    HTTP_Service_Email_Render empty = _render_fixed();

    empty.message_id[0] = '\0';

    test_expect_false(test, "an empty Message-ID is refused", http_service_email_payload_create_2(&email, &message, &empty, &payload));

    /* email.h documents the field as the value INCLUDING its angle brackets and
     * the renderer emits it verbatim after "Message-ID: ", so an unbracketed or
     * double-bracketed one is a malformed header nothing else would catch. */
    HTTP_Service_Email_Render bare = _render_fixed();

    memory_copy_2(bare.message_id, sizeof(bare.message_id), "abc@app.test", CHAR_STATIC_SIZE("abc@app.test"));

    bare.message_id[CHAR_STATIC_SIZE("abc@app.test")] = '\0';

    test_expect_false(test, "an unbracketed Message-ID is refused", http_service_email_payload_create_2(&email, &message, &bare, &payload));

    HTTP_Service_Email_Render unclosed = _render_fixed();

    memory_copy_2(unclosed.message_id, sizeof(unclosed.message_id), "<abc@app.test", CHAR_STATIC_SIZE("<abc@app.test"));

    unclosed.message_id[CHAR_STATIC_SIZE("<abc@app.test")] = '\0';

    test_expect_false(test, "one that opens but never closes is refused", http_service_email_payload_create_2(&email, &message, &unclosed, &payload));

    HTTP_Service_Email_Render doubled = _render_fixed();

    memory_copy_2(doubled.message_id, sizeof(doubled.message_id), "<a@app.test><b@app.test>", CHAR_STATIC_SIZE("<a@app.test><b@app.test>"));

    doubled.message_id[CHAR_STATIC_SIZE("<a@app.test><b@app.test>")] = '\0';

    test_expect_false(test, "two bracketed ids in one field are refused", http_service_email_payload_create_2(&email, &message, &doubled, &payload));

    HTTP_Service_Email_Render spaced_id = _render_fixed();

    memory_copy_2(spaced_id.message_id, sizeof(spaced_id.message_id), "<a b@app.test>", CHAR_STATIC_SIZE("<a b@app.test>"));

    spaced_id.message_id[CHAR_STATIC_SIZE("<a b@app.test>")] = '\0';

    test_expect_false(test, "an interior space is refused", http_service_email_payload_create_2(&email, &message, &spaced_id, &payload));
    test_expect_u(test, "and none of them rendered anything", 0, string_get_size(&payload));

    HTTP_Service_Email_Render const good = _render_fixed();

    test_expect_true(test, "the fixed context still renders", http_service_email_payload_create_2(&email, &message, &good, &payload));

    string_uninit(&payload);
    http_service_email_message_uninit(&message);
    http_service_email_uninit(&email);

    test_case_end(test);
}

/* An arena that REFUSES (never one that is exhausted - that aborts inside the
 * allocator) is the only way to watch an append decline. string_add_last_* say
 * so only by leaving the size alone, and discarding that answer used to render a
 * truncated message and report it sent. */
static void _test_allocator_refusal(Test *const test) {
    test_case_begin(test, "an allocator refusal is a refusal, never a shorter message");

    /* arena_init_2 folds a zero operand into its refusal predicate and leaves
     * `handler` null, so every allocator_borrow through it answers nullptr. */
    Arena refused = arena_init_1(0, ARENA_TYPE_LINEAR);

    HTTP_Service_Email_Message starved = DEFAULT_INITIALIZATION;

    test_expect_true(test, "a message on the refused arena initializes", http_service_email_message_alloc_init_1(&starved, &refused));
    test_expect_false(test, "but text_set answers false instead of storing a truncated body",
        http_service_email_message_text_set(&starved, "Hello there.\nBye."));
    test_expect_u(test, "and the body is left empty", 0, string_get_size(&starved.text));
    test_expect_false(test, "html_set answers false too", http_service_email_message_html_set(&starved, "<p>Hello</p>"));

    http_service_email_message_uninit(&starved);

    /* A well-formed heap message and service, rendering THROUGH the refused
     * arena: every payload append declines, so the answer must be false with an
     * EMPTY out - not a From: line and nothing else. */
    HTTP_Service_Email          email   = DEFAULT_INITIALIZATION;
    HTTP_Service_Email_Message  message = DEFAULT_INITIALIZATION;

    test_expect_true(test, "the heap service initializes", http_service_email_init_2(&email, _URL, "", "", _FROM));
    test_expect_true(test, "the heap message initializes", http_service_email_message_init_1(&message));
    test_expect_true(test, "the recipient is stored", http_service_email_message_to_add_1(&message, "person@example.com"));
    test_expect_true(test, "the subject is stored", http_service_email_message_subject_set(&message, "Verify"));
    test_expect_true(test, "the body is stored", http_service_email_message_text_set(&message, "Hello there."));
    test_expect_true(test, "and the message is valid", http_service_email_message_valid(&email, &message));

    email.allocator = &refused;

    HTTP_Service_Email_Render   const   render  = _render_fixed();
    String                              payload = DEFAULT_INITIALIZATION;

    test_expect_false(test, "payload_create_2 refuses whole", http_service_email_payload_create_2(&email, &message, &render, &payload));
    test_expect_u(test, "leaving out EMPTY rather than a well-formed prefix", 0, string_get_size(&payload));

    /* The capture transport reads the same refusal: a truncated transcript is
     * never reported as a send. */
    String sink = DEFAULT_INITIALIZATION;

    test_expect_true(test, "the capture sink is armed", http_service_email_transport_capture_set(&email, &sink));

    HTTP_Service_Email_Result result = http_service_email_send(&email, &message);

    test_expect_false(test, "the capture send fails", result.success);
    test_expect_i(test, "naming the render stage", HTTP_SERVICE_EMAIL_STATUS_RENDER_FAILED, result.status);

    http_service_email_result_uninit(&result);
    string_uninit(&sink);

    email.allocator = nullptr;

    http_service_email_message_uninit(&message);
    http_service_email_uninit(&email);
    arena_uninit(&refused, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_render_init(Test *const test) {
    test_case_begin(test, "render_init draws a fresh boundary and Message-ID per message");

    HTTP_Service_Email          email   = DEFAULT_INITIALIZATION;
    HTTP_Service_Email_Message  message = DEFAULT_INITIALIZATION;

    test_expect_true(test, "the service initializes", http_service_email_init_2(&email, _URL, "", "", _FROM));
    test_expect_true(test, "the message initializes", http_service_email_message_init_1(&message));

    HTTP_Service_Email_Render first  = DEFAULT_INITIALIZATION;
    HTTP_Service_Email_Render second = DEFAULT_INITIALIZATION;

    test_expect_true(test, "the first context fills", http_service_email_render_init(&email, &message, &first));
    test_expect_true(test, "the second context fills", http_service_email_render_init(&email, &message, &second));

    test_expect_false(test, "the boundaries differ", char_compare_equal_2(first.boundary, char_length(first.boundary), second.boundary, char_length(second.boundary)));
    test_expect_false(test, "the Message-IDs differ", char_compare_equal_2(first.message_id, char_length(first.message_id), second.message_id, char_length(second.message_id)));
    test_expect_true(test, "the Message-ID is bracketed", first.message_id[0] == '<');
    test_expect_true(test, "and carries the sender's domain", char_length(first.message_id) > CHAR_STATIC_SIZE("app.test"));

    http_service_email_message_uninit(&message);
    http_service_email_uninit(&email);

    test_case_end(test);
}

static void _test_arena_tier(Test *const test) {
    test_case_begin(test, "the arena tier renders the same bytes as the heap tier");

    Arena arena = arena_init_1(65536, ARENA_TYPE_LINEAR);

    HTTP_Service_Email          email   = DEFAULT_INITIALIZATION;
    HTTP_Service_Email_Message  message = DEFAULT_INITIALIZATION;

    test_expect_true(test, "the arena service initializes", http_service_email_alloc_init_2(&email, _URL, "", "", _FROM, &arena));
    test_expect_true(test, "the arena message initializes", http_service_email_message_alloc_init_1(&message, &arena));
    test_expect_true(test, "the recipient is stored", http_service_email_message_to_add_1(&message, "person@example.com"));
    test_expect_true(test, "the subject is stored", http_service_email_message_subject_set(&message, "Verify"));
    test_expect_true(test, "the body is stored", http_service_email_message_text_set(&message, "Hello there.\nBye."));

    HTTP_Service_Email_Render   const   render  = _render_fixed();
    String                              payload = DEFAULT_INITIALIZATION;

    test_expect_true(test, "the payload is built", http_service_email_payload_create_2(&email, &message, &render, &payload));
    test_expect_string(test, "byte-identical to the heap tier",
        "From: noreply@app.test\r\n"
        "To: person@example.com\r\n"
        "Subject: Verify\r\n"
        "Date: 06 Sep 2026 12:00:00 +0000\r\n"
        "Message-ID: " _FIXED_MESSAGE_ID "\r\n"
        "MIME-Version: 1.0\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Content-Transfer-Encoding: quoted-printable\r\n"
        "\r\n"
        "Hello there.\r\n"
        "Bye.\r\n",
        string_get_data(&payload));

    string_uninit(&payload);
    http_service_email_message_uninit(&message);
    http_service_email_uninit(&email);
    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_init_from_env(Test *const test) {
    test_case_begin(test, "init_from_env reads the names the three consumers hand-rolled");

    HTTP_Service_Email email = DEFAULT_INITIALIZATION;

    test_expect_false(test, "an absent URL leaves an unconfigured service", http_service_email_init_from_env(&email, "CFW_TEST_SMTP"));
    test_expect_false(test, "which reports itself as such", http_service_email_valid(&email));

    http_service_email_uninit(&email);

    test_expect_true(test, "the URL is set", result_is_success(env_set_1("CFW_TEST_SMTP_URL", _URL)));
    test_expect_true(test, "the sender is set", result_is_success(env_set_1("CFW_TEST_SMTP_FROM", _FROM)));
    test_expect_true(test, "the user is set", result_is_success(env_set_1("CFW_TEST_SMTP_USER", "relay-user")));
    test_expect_true(test, "the password is set", result_is_success(env_set_1("CFW_TEST_SMTP_PASSWORD", "relay-secret")));
    test_expect_true(test, "verification is disabled", result_is_success(env_set_1("CFW_TEST_SMTP_VERIFY_TLS", "0")));

    test_expect_true(test, "the environment configures the service", http_service_email_init_from_env(&email, "CFW_TEST_SMTP"));
    test_expect_true(test, "and it is valid", http_service_email_valid(&email));
    test_expect_string(test, "with the URL from the environment", _URL, string_get_data(&email.url));
    test_expect_string(test, "the username from the environment", "relay-user", string_get_data(&email.username));
    test_expect_false(test, "and TLS verification off, because the value was exactly \"0\"", email.verify_tls);

    http_service_email_uninit(&email);

    /* Anything other than "0" fails SAFE: a typo must not silently disable
     * certificate verification. */
    test_expect_true(test, "a non-\"0\" value is set", result_is_success(env_set_1("CFW_TEST_SMTP_VERIFY_TLS", "false")));
    test_expect_true(test, "the environment configures the service", http_service_email_init_from_env(&email, "CFW_TEST_SMTP"));
    test_expect_true(test, "and verification stays ON", email.verify_tls);

    http_service_email_uninit(&email);

    env_unset("CFW_TEST_SMTP_URL");
    env_unset("CFW_TEST_SMTP_FROM");
    env_unset("CFW_TEST_SMTP_USER");
    env_unset("CFW_TEST_SMTP_PASSWORD");
    env_unset("CFW_TEST_SMTP_VERIFY_TLS");

    test_case_end(test);
}

/* init_from_env answered the same false for "no relay was configured" and "the
 * configured relay was refused", so TRAYMON_SMTP_URL=smtp:/host - one missing
 * slash - booted a server that looked healthy and silently never mailed. */
static void _test_init_from_env_diagnosis(Test *const test) {
    test_case_begin(test, "init_from_env_2 tells an unset variable from a refused one");

    HTTP_Service_Email          email   = DEFAULT_INITIALIZATION;
    HTTP_Service_Email_Status   status  = HTTP_SERVICE_EMAIL_STATUS_TRANSPORT_FAILED;

    test_expect_false(test, "an absent URL leaves an unconfigured service", http_service_email_init_from_env_2(&email, "CFW_TEST_DIAG_SMTP", &status));
    test_expect_i(test, "and names it as such", HTTP_SERVICE_EMAIL_STATUS_NOT_CONFIGURED, status);
    test_expect_false(test, "the service reports itself unconfigured", http_service_email_valid(&email));

    http_service_email_uninit(&email);

    /* One missing slash. Every other observable is identical to the unset case:
     * false, an unconfigured service, valid() answering false. */
    test_expect_true(test, "a malformed URL is set", result_is_success(env_set_1("CFW_TEST_DIAG_SMTP_URL", "smtp:/relay.example.com")));
    test_expect_true(test, "the sender is set", result_is_success(env_set_1("CFW_TEST_DIAG_SMTP_FROM", _FROM)));

    status = HTTP_SERVICE_EMAIL_STATUS_TRANSPORT_FAILED;

    test_expect_false(test, "the environment describes a relay the module refuses", http_service_email_init_from_env_2(&email, "CFW_TEST_DIAG_SMTP", &status));
    test_expect_i(test, "which is a MISCONFIGURED deployment, not an absent one", HTTP_SERVICE_EMAIL_STATUS_INVALID_CONFIGURATION, status);
    test_expect_false(test, "the service is still left safe to use and uninit", http_service_email_valid(&email));
    test_expect_false(test, "and the bool tier cannot tell the two apart at all", http_service_email_init_from_env(&email, "CFW_TEST_DIAG_SMTP"));

    http_service_email_uninit(&email);

    /* A sender that is not an address is the same class: named, and refused. */
    test_expect_true(test, "a good URL is set", result_is_success(env_set_1("CFW_TEST_DIAG_SMTP_URL", _URL)));
    test_expect_true(test, "a malformed sender is set", result_is_success(env_set_1("CFW_TEST_DIAG_SMTP_FROM", "noreply")));

    status = HTTP_SERVICE_EMAIL_STATUS_TRANSPORT_FAILED;

    test_expect_false(test, "a sender that is not an address is refused", http_service_email_init_from_env_2(&email, "CFW_TEST_DIAG_SMTP", &status));
    test_expect_i(test, "and named the same way", HTTP_SERVICE_EMAIL_STATUS_INVALID_CONFIGURATION, status);

    http_service_email_uninit(&email);

    test_expect_true(test, "a good sender is set", result_is_success(env_set_1("CFW_TEST_DIAG_SMTP_FROM", _FROM)));

    status = HTTP_SERVICE_EMAIL_STATUS_TRANSPORT_FAILED;

    test_expect_true(test, "and now the environment configures the service", http_service_email_init_from_env_2(&email, "CFW_TEST_DIAG_SMTP", &status));
    test_expect_i(test, "reporting OK", HTTP_SERVICE_EMAIL_STATUS_OK, status);
    test_expect_true(test, "with a valid configuration", http_service_email_valid(&email));

    http_service_email_uninit(&email);

    env_unset("CFW_TEST_DIAG_SMTP_URL");
    env_unset("CFW_TEST_DIAG_SMTP_FROM");

    /* Every consumer logging a failed send hand-wrote these six names. */
    test_expect_string(test, "OK has a name", "ok", http_service_email_status_name(HTTP_SERVICE_EMAIL_STATUS_OK));
    test_expect_string(test, "so does NOT_CONFIGURED", "not_configured", http_service_email_status_name(HTTP_SERVICE_EMAIL_STATUS_NOT_CONFIGURED));
    test_expect_string(test, "INVALID_MESSAGE", "invalid_message", http_service_email_status_name(HTTP_SERVICE_EMAIL_STATUS_INVALID_MESSAGE));
    test_expect_string(test, "RENDER_FAILED", "render_failed", http_service_email_status_name(HTTP_SERVICE_EMAIL_STATUS_RENDER_FAILED));
    test_expect_string(test, "TRANSPORT_INIT_FAILED", "transport_init_failed", http_service_email_status_name(HTTP_SERVICE_EMAIL_STATUS_TRANSPORT_INIT_FAILED));
    test_expect_string(test, "TRANSPORT_FAILED", "transport_failed", http_service_email_status_name(HTTP_SERVICE_EMAIL_STATUS_TRANSPORT_FAILED));
    test_expect_string(test, "INVALID_CONFIGURATION", "invalid_configuration", http_service_email_status_name(HTTP_SERVICE_EMAIL_STATUS_INVALID_CONFIGURATION));
    test_expect_string(test, "and a value outside the enum is named, never null", "unknown", http_service_email_status_name((HTTP_Service_Email_Status) 99));

    test_case_end(test);
}

/* Three small invariants a reader would otherwise have to take on trust: the CA
 * bundle is validated like every sibling setter, an address is length-capped so
 * the recipient list cannot render one over-long line, and uninit restores the
 * SAME verify_tls both constructors default to. */
static void _test_configuration_invariants(Test *const test) {
    test_case_begin(test, "the CA bundle, the address caps, and a symmetric uninit");

    HTTP_Service_Email email = DEFAULT_INITIALIZATION;

    test_expect_true(test, "the service initializes", http_service_email_init_2(&email, _URL, "", "", _FROM));
    test_expect_true(test, "TLS verification is on by default", email.verify_tls);
    test_expect_true(test, "an ordinary CA bundle path is stored", http_service_email_ca_bundle_set(&email, "C:/certs/private-ca.pem"));

    /* The value reaches CURLOPT_CAINFO; every sibling setter refuses a control
     * byte and this one used to validate nothing at all. */
    test_expect_false(test, "a control byte in the path is refused", http_service_email_ca_bundle_set(&email, "C:/certs/ca.pem\r\nX: y"));
    test_expect_string(test, "and the previous bundle stands", "C:/certs/private-ca.pem", string_get_data(&email.ca_bundle));
    test_expect_true(test, "an empty path clears it back to curl's default store", http_service_email_ca_bundle_set(&email, ""));

    test_expect_true(test, "TLS verification can be turned off deliberately", http_service_email_verify_tls_set(&email, false));

    http_service_email_uninit(&email);

    /* `security` was made symmetric in an earlier round for exactly this
     * reason: a reset that leaves verification OFF is the one asymmetry that
     * silently weakens a re-initialized service. */
    test_expect_true(test, "uninit restores the constructors' verify_tls", email.verify_tls);
    test_expect_i(test, "beside the security mode it already restored", HTTP_SERVICE_EMAIL_SECURITY_REQUIRED, email.security);

    /* RFC 5321 §4.5.3.1: 64 octets of local part, 255 of domain. The recipient
     * list folds BETWEEN addresses and never inside one, so an uncapped address
     * was rendered as a single line of its own length. */
    char local[80]   = DEFAULT_INITIALIZATION;
    char domain[320] = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 64; i += 1) {
        local[i] = 'a';
    }

    local[64] = '@';
    local[65] = 'b';

    test_expect_true(test, "a 64-octet local part is accepted", http_service_email_address_valid(local));

    local[64] = 'a';
    local[65] = '@';
    local[66] = 'b';

    test_expect_false(test, "a 65-octet one is refused", http_service_email_address_valid(local));

    domain[0] = 'a';
    domain[1] = '@';

    for (USize i = 0; i < 255; i += 1) {
        domain[2 + i] = 'd';
    }

    test_expect_true(test, "a 255-octet domain is accepted", http_service_email_address_valid(domain));

    domain[2 + 255] = 'd';

    test_expect_false(test, "a 256-octet one is refused", http_service_email_address_valid(domain));

    /* The whole point of the caps: the longest address the module accepts still
     * renders inside one legal header line, because a To: list folds only
     * BETWEEN addresses. */
    test_expect_true(test, "and the longest accepted address is well under 998 columns",
        CHAR_STATIC_SIZE("To: ") + 64 + CHAR_STATIC_SIZE("@") + 255 < 998);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Entry
 *============================================================================*/

I32 main(void) {
    LogConfig const log_config = { .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true };

    log_init(log_config);

    Test test = test_init("http_service_email");

    test_suite_begin(&test, "http_service_email");

    _test_golden_plain(&test);
    _test_golden_spanish_subject(&test);
    _test_golden_multipart(&test);
    _test_capture_transport(&test);
    _test_capture_dot_stuffing(&test);
    _test_send_not_configured(&test);
    _test_address_valid_table(&test);
    _test_refusal_table(&test);
    _test_quoted_printable_encoder(&test);
    _test_long_header_folding(&test);
    _test_unbreakable_word_folding(&test);
    _test_render_context_refusal(&test);
    _test_allocator_refusal(&test);
    _test_render_init(&test);
    _test_arena_tier(&test);
    _test_init_from_env(&test);
    _test_init_from_env_diagnosis(&test);
    _test_configuration_invariants(&test);

    test_suite_end(&test);

    return test_uninit(&test);
}