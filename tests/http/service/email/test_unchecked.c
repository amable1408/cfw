#include <arena/arena.h>
#include <encoding/quoted_printable/quoted_printable.h>
#include <http/service/email/email.h>
#include <log/log.h>
#include <test/test.h>

/*
 * Behavioral tests for include/http/service/email/email.c and
 * include/encoding/quoted_printable/quoted_printable.c built WITHOUT
 * ERROR_CHECK_ENABLED.
 *
 * error_check_null in both modules guards only null-pointer CONTRACTS -
 * deliberately absent here (undefined behavior with the checks compiled out,
 * and this file never exercises them). Everything below is coded as an ordinary
 * runtime branch, never routed through error_check, per the value-dependent-
 * refusal standard: a data-dependent decision is never an abort primitive. This
 * build proves each one refuses identically whether ERROR_CHECK_ENABLED is
 * defined or not.
 *
 *   - The ones this suite exists for. set_timeouts used to route a 0 into
 *     error_check_non_value_uint, which ABORTS in the checked build and
 *     compiles away here - leaving a service whose curl timeout meant "wait
 *     forever" on a request path. A recipient built from a DB cell used to
 *     reach error_check_non_value_uint through str_alloc_init_static, so an
 *     empty users.email killed the server; the guard now lives in one place and
 *     answers false in both builds.
 *   - Every constructor refusal: a non-SMTP scheme, an empty URL, userinfo in
 *     the URL (credentials that curl would echo into result.error, which
 *     consumers log), and a sender that is not an address. Each refuses WHOLE,
 *     so `self` is untouched and a refusal can never read back as "configured
 *     but empty".
 *   - header_add's line rules, including the module-owned names: a caller
 *     "Bcc:" or a second "Content-Type:" does not replace the module's line, it
 *     adds a second one, and that rewrites the message a receiver sees.
 *   - The quoted-printable encoder's capacity refusal, which is the only thing
 *     standing between a short caller buffer and an overrun once the null
 *     checks are gone.
 */

I32 main(void) {
    LogConfig const log_config = { .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true };

    log_init(log_config);

    Test test = test_init("tests/http/service/email/test_unchecked.c");

    test_suite_begin(&test, "http_service_email (unchecked)");
    test_case_begin(&test, "value-dependent refusals hold with the checks compiled out");

    HTTP_Service_Email          email   = DEFAULT_INITIALIZATION;
    HTTP_Service_Email_Message  message = DEFAULT_INITIALIZATION;

    test_expect_false(&test, "a non-SMTP scheme is refused whole", http_service_email_init_2(&email, "http://relay.test", "", "", "noreply@app.test"));
    test_expect_false(&test, "an empty URL is refused whole", http_service_email_init_2(&email, "", "", "", "noreply@app.test"));
    test_expect_false(&test, "userinfo in the URL is refused whole", http_service_email_init_2(&email, "smtp://user:pass@relay.test", "", "", "noreply@app.test"));
    test_expect_false(&test, "a sender that is not an address is refused whole", http_service_email_init_2(&email, "smtp://relay.test:587", "", "", "noreply"));

    test_expect_true(&test, "a good configuration initializes", http_service_email_init_2(&email, "smtp://relay.test:587", "", "", "noreply@app.test"));

    /* A zero timeout used to reach error_check_non_value_uint: an abort in the
     * checked build, and NOTHING here - leaving curl waiting forever. */
    test_expect_false(&test, "a zero connect timeout is refused", http_service_email_timeouts_set(&email, 0, 10000));
    test_expect_false(&test, "a zero total timeout is refused", http_service_email_timeouts_set(&email, 3000, 0));
    test_expect_true(&test, "the service is still valid", http_service_email_valid(&email));

    test_expect_false(&test, "a non-SMTP URL is refused by the setter", http_service_email_url_set(&email, "ftp://relay.test"));
    test_expect_false(&test, "a sender that is not an address is refused by the setter", http_service_email_from_set(&email, "nope"));
    test_expect_false(&test, "a CR in the display name is refused", http_service_email_from_name_set(&email, "App\r\nBcc: x@y"));

    /* The CA bundle path reaches CURLOPT_CAINFO and used to be stored without
     * any validation at all, unlike every sibling setter. */
    test_expect_true(&test, "an ordinary CA bundle path is stored", http_service_email_ca_bundle_set(&email, "/etc/ssl/private-ca.pem"));
    test_expect_false(&test, "a control byte in the path is refused", http_service_email_ca_bundle_set(&email, "/etc/ssl/ca.pem\r\nX: y"));

    /* RFC 5321 §4.5.3.1's caps are a value refusal, never an abort: an oversized
     * address out of a DB cell is data. */
    char oversized[80] = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 65; i += 1) {
        oversized[i] = 'a';
    }

    oversized[65] = '@';
    oversized[66] = 'b';

    test_expect_false(&test, "a 65-octet local part is refused, not fatal", http_service_email_address_valid(oversized));

    test_expect_true(&test, "the message initializes", http_service_email_message_init_1(&message));

    /* An empty address is DATA (a blank users.email cell), never a programming
     * error: it is skipped and reported, not aborted on. */
    test_expect_false(&test, "an empty recipient is refused, not fatal", http_service_email_message_to_add_1(&message, ""));
    test_expect_false(&test, "an empty copy recipient is refused, not fatal", http_service_email_message_cc_add_1(&message, ""));
    test_expect_false(&test, "an empty blind recipient is refused, not fatal", http_service_email_message_bcc_add_1(&message, ""));
    test_expect_false(&test, "a malformed recipient is refused", http_service_email_message_to_add_1(&message, "person"));
    test_expect_false(&test, "a CR in the subject is refused", http_service_email_message_subject_set(&message, "Hi\r\nBcc: x@y"));

    test_expect_false(&test, "a colon-less header line is refused", http_service_email_message_header_add(&message, "X-Campaign verify"));
    test_expect_false(&test, "a leading-whitespace header line is refused", http_service_email_message_header_add(&message, " X-Campaign: verify"));
    test_expect_false(&test, "an empty header name is refused", http_service_email_message_header_add(&message, ": verify"));
    test_expect_false(&test, "a module-owned Bcc is refused", http_service_email_message_header_add(&message, "Bcc: hidden@example.com"));
    test_expect_false(&test, "a module-owned Content-Type is refused", http_service_email_message_header_add(&message, "content-type: text/plain"));
    test_expect_false(&test, "an embedded CRLF is refused", http_service_email_message_header_add(&message, "X-A: 1\r\nBcc: x@y"));
    test_expect_false(&test, "a module-owned Reply-To is refused", http_service_email_message_header_add(&message, "Reply-To: attacker@example.com"));
    test_expect_true(&test, "an ordinary header is still stored", http_service_email_message_header_add(&message, "X-Campaign: verify"));

    test_expect_false(&test, "a message with no recipient and no body is invalid", http_service_email_message_valid(&email, &message));

    HTTP_Service_Email_Result result = http_service_email_send(&email, &message);

    test_expect_false(&test, "sending it fails", result.success);
    test_expect_i(&test, "naming the message, not the relay", HTTP_SERVICE_EMAIL_STATUS_INVALID_MESSAGE, result.status);

    http_service_email_result_uninit(&result);

    HTTP_Service_Email unconfigured = DEFAULT_INITIALIZATION;

    test_expect_true(&test, "an unconfigured service initializes", http_service_email_init_1(&unconfigured));
    test_expect_false(&test, "but reports itself unusable", http_service_email_valid(&unconfigured));

    HTTP_Service_Email_Result never = http_service_email_send(&unconfigured, &message);

    test_expect_false(&test, "sending through it fails", never.success);
    test_expect_i(&test, "naming configuration before anything is dialed", HTTP_SERVICE_EMAIL_STATUS_NOT_CONFIGURED, never.status);

    http_service_email_result_uninit(&never);
    http_service_email_uninit(&unconfigured);

    test_case_end(&test);
    test_case_begin(&test, "a caller-supplied render context is validated as data, not trusted");

    HTTP_Service_Email          sender      = DEFAULT_INITIALIZATION;
    HTTP_Service_Email_Message  sendable    = DEFAULT_INITIALIZATION;

    test_expect_true(&test, "the service initializes", http_service_email_init_2(&sender, "smtp://relay.test:587", "", "", "noreply@app.test"));
    test_expect_true(&test, "the message initializes", http_service_email_message_init_1(&sendable));
    test_expect_true(&test, "the recipient is stored", http_service_email_message_to_add_1(&sendable, "person@example.com"));
    test_expect_true(&test, "the body is stored", http_service_email_message_text_set(&sendable, "hello"));

    /* All-'b' with no terminator: char_length would read off the end of the
     * struct. This is an ordinary bounds walk, never an error_check. */
    HTTP_Service_Email_Render   context = DEFAULT_INITIALIZATION;
    String                      payload = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < sizeof(context.boundary); i += 1) {
        context.boundary[i] = 'b';
    }

    memory_copy_2(context.message_id, sizeof(context.message_id), "<a@app.test>", CHAR_STATIC_SIZE("<a@app.test>"));

    test_expect_false(&test, "an unterminated boundary is refused", http_service_email_payload_create_2(&sender, &sendable, &context, &payload));
    test_expect_u(&test, "and the payload stays EMPTY", 0, string_get_size(&payload));

    memory_copy_2(context.boundary, sizeof(context.boundary), "=_cfw_ok", CHAR_STATIC_SIZE("=_cfw_ok"));

    context.boundary[CHAR_STATIC_SIZE("=_cfw_ok")] = '\0';

    memory_copy_2(context.message_id, sizeof(context.message_id), "<a@app.test>\r\nBcc: x@y", CHAR_STATIC_SIZE("<a@app.test>\r\nBcc: x@y"));

    test_expect_false(&test, "a CRLF-bearing Message-ID is refused", http_service_email_payload_create_2(&sender, &sendable, &context, &payload));

    /* email.h documents the field as the value INCLUDING its brackets and the
     * renderer emits it verbatim, so an unbracketed one is a malformed header.
     * This is an ordinary shape check, never an error_check. */
    memory_copy_2(context.message_id, sizeof(context.message_id), "abc@app.test", CHAR_STATIC_SIZE("abc@app.test"));

    context.message_id[CHAR_STATIC_SIZE("abc@app.test")] = '\0';

    test_expect_false(&test, "an unbracketed Message-ID is refused", http_service_email_payload_create_2(&sender, &sendable, &context, &payload));

    memory_copy_2(context.message_id, sizeof(context.message_id), "<a b@app.test>", CHAR_STATIC_SIZE("<a b@app.test>"));

    context.message_id[CHAR_STATIC_SIZE("<a b@app.test>")] = '\0';

    test_expect_false(&test, "one carrying an interior space is refused", http_service_email_payload_create_2(&sender, &sendable, &context, &payload));

    memory_copy_2(context.message_id, sizeof(context.message_id), "<a@app.test>", CHAR_STATIC_SIZE("<a@app.test>"));

    context.message_id[CHAR_STATIC_SIZE("<a@app.test>")] = '\0';

    test_expect_true(&test, "the corrected context renders", http_service_email_payload_create_2(&sender, &sendable, &context, &payload));

    string_uninit(&payload);

    /* A refused arena (never an exhausted one, which aborts inside the
     * allocator) makes every append decline: the refusal must survive with the
     * checks compiled out, because a truncated message reads as a valid one. */
    Arena refused = arena_init_1(0, ARENA_TYPE_LINEAR);

    HTTP_Service_Email_Message starved = DEFAULT_INITIALIZATION;

    test_expect_true(&test, "a message on the refused arena initializes", http_service_email_message_alloc_init_1(&starved, &refused));
    test_expect_false(&test, "text_set answers false instead of truncating", http_service_email_message_text_set(&starved, "Hello there.\nBye."));

    sender.allocator = &refused;

    test_expect_false(&test, "payload_create_2 refuses whole", http_service_email_payload_create_2(&sender, &sendable, &context, &payload));
    test_expect_u(&test, "leaving out EMPTY", 0, string_get_size(&payload));

    sender.allocator = nullptr;

    http_service_email_message_uninit(&starved);
    arena_uninit(&refused, ARENA_TYPE_LINEAR);
    http_service_email_message_uninit(&sendable);
    http_service_email_uninit(&sender);

    test_case_end(&test);
    test_case_begin(&test, "the quoted-printable capacity refusal holds with the checks compiled out");

    char    small[4]        = DEFAULT_INITIALIZATION;
    USize   encoded_size    = 0;

    test_expect_true(&test, "a buffer too small refuses instead of overrunning",
        result_is_error(encoding_quoted_printable_encode_1("abcdefgh", CHAR_STATIC_SIZE("abcdefgh"), small, sizeof(small), &encoded_size)));
    test_expect_u(&test, "and leaves encoded_size untouched", 0, encoded_size);
    test_expect_u(&test, "an empty input is a legal value", 0, encoding_quoted_printable_encode_size("", 0));

    http_service_email_message_uninit(&message);
    http_service_email_uninit(&email);

    test_case_end(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}