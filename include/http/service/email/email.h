/* ============================================================================
 *  HTTP Service Email
 *  --------------------------------------------------------------------------
 *  @file    email.h
 *  @brief   SMTP email service — RFC 5322/2045/2047 message rendering plus a
 *           libcurl SMTP transport and an in-memory capture transport.
 *  @author  CFW
 *  @date    2026-09-06
 *  @version 0.4.0
 *  @license MIT (see LICENSE file)
 *
 *  Sends account, security, receipt, and notification emails while keeping
 *  token generation, account state, templates, and persistence outside the
 *  service. The module owns exactly two things: how a message becomes RFC 5322
 *  bytes, and how those bytes reach a relay.
 *
 *  Transports:
 *    - SMTP over libcurl (smtp:// or smtps://). This is the real one.
 *    - Capture, via http_service_email_transport_capture_set: the envelope and
 *      payload are rendered into a caller String and NOTHING is dialed. It is
 *      how the suite observes a send without a mail server, and how a caller
 *      previews exactly what would go on the wire - the payload is DOT-STUFFED
 *      into the sink, exactly as libcurl stuffs it on the SMTP upload.
 *    There is no HTTP-API (Resend/SES/SendGrid) transport; the seam is shaped
 *    for one, but nothing in this tree needs it yet.
 *
 *  Features:
 *    - Plain text, HTML, or multipart/alternative bodies, quoted-printable
 *      encoded (RFC 2045) so no body line exceeds 76 characters.
 *    - Bodies are CRLF-normalized as they are stored: bare LF and lone CR both
 *      become CRLF. Gmail and Exchange Online REJECT a bare-LF message at SMTP
 *      time, and curl's dot-stuffing only recognizes CRLF.
 *    - Date and Message-ID emitted on every message (RFC 5322 §3.6 makes Date
 *      mandatory; a missing Message-ID is a standard spam-score hit).
 *    - Non-ASCII Subject and display names RFC 2047 B-encoded; display names
 *      carrying RFC 5322 specials quoted; long header lines folded at their
 *      spaces - including caller-supplied custom headers and the From display
 *      name - and HARD-SPLIT only at the 998-octet hard limit when a single
 *      word has no space to fold at, so no rendered line can pass RFC 5322's
 *      limit whatever the input.
 *    - To, Cc, Bcc, Reply-To, custom headers, and default sender support.
 *    - Env-driven construction (http_service_email_init_from_env, and
 *      http_service_email_init_from_env_2 when the caller wants to tell "no
 *      relay was configured" from "the configured relay was refused"),
 *      including the STARTTLS mode and the default display name, so the
 *      Mailpit example below is reachable from <prefix>_STARTTLS=none.
 *    - Payload generation for tests and previews, with a caller-supplied render
 *      context so the output is byte-for-byte reproducible.
 *    - Arena and heap allocation support.
 *
 *  Usage Example:
 *    @code
 *    HTTP_Service_Email email = DEFAULT_INITIALIZATION;
 *
 *    // A local dev relay (Mailpit / MailHog) speaks plaintext on 1025.
 *    if (!http_service_email_init_2(&email, "smtp://127.0.0.1:1025", "", "", "noreply@app.com")) {
 *        return; // refused whole: nothing was allocated, nothing to release
 *    }
 *
 *    http_service_email_security_set(&email, HTTP_SERVICE_EMAIL_SECURITY_NONE);
 *
 *    HTTP_Service_Email_Message message = DEFAULT_INITIALIZATION;
 *
 *    http_service_email_message_init_1(&message);
 *    http_service_email_message_to_add_1(&message, "person@example.com");
 *    http_service_email_message_subject_set(&message, "Verify your account");
 *    http_service_email_message_text_set(&message, "Open the verification link.");
 *
 *    HTTP_Service_Email_Result result = http_service_email_send(&email, &message);
 *
 *    if (!result.success) {
 *        // result.status separates "never configured" from "the relay refused"
 *    }
 *
 *    http_service_email_result_uninit(&result);
 *    http_service_email_message_uninit(&message);
 *    http_service_email_uninit(&email);
 *    @endcode
 *
 *  Error Handling:
 *    - Public functions validate non-null pointers through error_check_null.
 *    - Every constructor is an in-place bool: it refuses WHOLE (leaving `self`
 *      untouched and unusable, with nothing to release) rather than degrading a
 *      field to empty, which used to make a refused URL indistinguishable from
 *      a service that was never configured.
 *    - Every setter and adder answers bool. False means the value was NOT
 *      stored - it was refused as data (a CR/LF in a header, an address without
 *      an '@', a timeout of 0, a header the module owns) or the allocator
 *      declined. A caller that ignores it sends a message it believes carries
 *      that recipient or header.
 *    - An allocator refusal is never rendered as a shorter message. Every
 *      append reports whether its bytes landed, so a String that declines
 *      partway - an arena that ran out mid-header - makes
 *      http_service_email_payload_create_1/_2 answer false with `out` left
 *      EMPTY, and makes a capture send answer success=false with
 *      HTTP_SERVICE_EMAIL_STATUS_RENDER_FAILED, rather than producing a
 *      well-formed PREFIX of the message the caller wrote.
 *    - Failed sends return success=false with `status` naming the stage,
 *      `curl_code` carrying the CURLcode for the SMTP transport, and `error`
 *      the text. HTTP_SERVICE_EMAIL_STATUS_NOT_CONFIGURED is reported BEFORE
 *      any dialing, so an empty URL no longer surfaces as curl's
 *      "URL using bad/illegal format".
 *    - Three setters cannot fail and answer true unconditionally, so a caller
 *      scanning the family for a missing check can skip them:
 *      http_service_email_security_set, http_service_email_verify_tls_set and
 *      http_service_email_transport_capture_set. Each stores a value with no
 *      invalid spelling, and none of them allocates.
 *
 *  Thread Safety:
 *    - Construction and every setter mutate the service and must be serialized
 *      with all other access to it.
 *    - http_service_email_send is SAFE to call concurrently on a fully
 *      configured service that is no longer being mutated: it takes the service
 *      read-only, each call owns its own curl easy handle, and no state is
 *      shared between calls. A caller does NOT need a mutex around sends.
 *      The one exception is the capture transport - concurrent sends would
 *      interleave into one sink - so a captured service is single-threaded.
 *    - PREREQUISITE for that guarantee: curl's process-wide bring-up must have
 *      happened BEFORE the first concurrent send. curl_easy_init performs a
 *      lazy, NOT thread-safe global init when nothing has brought curl up, so
 *      two first-sends from two threads race inside libcurl itself. This module
 *      deliberately runs no once of its own (see Deps), which means a program
 *      using email WITHOUT http/client must call http_client_global_init - or
 *      curl_global_init - once from a single thread at startup. A program that
 *      already uses http/client has this for free.
 *    - A message is a plain value and is not shared between threads.
 *
 *  Memory Management:
 *    - Service and message values own COPIES of every string handed to them;
 *      the caller's buffers may go away immediately after the call.
 *    - Returned String and Result values must be uninitialized by the caller.
 *    - The password is freed but not zeroed on uninit (the tree has no
 *      memory_wipe yet); it can survive in freed heap until reused.
 *    - The capture sink is BORROWED: the caller owns that String and must keep
 *      it alive for as long as the service points at it.
 *
 *  Performance Characteristics:
 *    - Payload generation is linear over headers, recipients, and body size,
 *      into a String pre-sized from the bodies PLUS the recipient and custom
 *      header bytes, so a thirty-address To: or a handful of custom headers no
 *      longer forces the common case to regrow.
 *    - An SMTP envelope path is built in a fixed stack buffer, so a send
 *      allocates nothing per recipient.
 *    - Each send pays one connection and one TLS handshake; there is no
 *      connection reuse across sends.
 *
 *  Dependencies (Deps):
 *    - libcurl (SMTP transport), arrayList, str, string.
 *    - crypto/random (Message-ID and MIME boundary entropy), datetime (the Date
 *      header), env (init_from_env), encoding/base64 (RFC 2047 B-encoding),
 *      encoding/quoted_printable (body transfer encoding).
 *    - curl_global_init is NOT called from here. This module follows
 *      websocket/client's contract: the OWNING PROGRAM, or http/client's own
 *      pthread_once (http_client.h, "curl_global_init"), brings curl up, and
 *      the single http_client_global_uninit balances it. A second once of this
 *      module's own took curl's global refcount to two against one cleanup,
 *      which is why it is gone.
 *
 *  See email.c for implementation details.
 * ============================================================================
 */

#ifndef HTTP_SERVICE_EMAIL_H
#define HTTP_SERVICE_EMAIL_H

#include <container/arrayList/al_str.h>
#include <container/string/string.h>
#include <datetime/datetime.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

/** @brief Capacity of a render context's MIME boundary, terminator included. */
#define HTTP_SERVICE_EMAIL_BOUNDARY_MAX_SIZE 48

/** @brief Default SMTP connect timeout. Short because a send may sit on a request path. */
#define HTTP_SERVICE_EMAIL_DEFAULT_CONNECT_TIMEOUT_MS 3000

/** @brief Default SMTP total timeout. Short because a send may sit on a request path. */
#define HTTP_SERVICE_EMAIL_DEFAULT_TIMEOUT_MS 10000

/** @brief Column a rendered header line folds at (RFC 5322 §2.1.1 recommends 78). */
#define HTTP_SERVICE_EMAIL_HEADER_LINE_MAX_LENGTH 78

/** @brief Capacity of a render context's Message-ID, angle brackets and terminator included. */
#define HTTP_SERVICE_EMAIL_MESSAGE_ID_MAX_SIZE 96

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief SMTP transport security mode.
 * @note This is the STARTTLS axis only - whether an upgrade on a plaintext
 *       connection is skipped, tried, or required. The other axis is the URL
 *       scheme: smtps:// is implicit TLS from the first byte and is secure
 *       regardless of this setting, while smtp:// is plaintext until a
 *       STARTTLS this enum permits. HTTP_SERVICE_EMAIL_SECURITY_NONE on an
 *       smtp:// URL sends credentials in the clear and exists for a local dev
 *       relay, nothing else.
 */
typedef enum {
    HTTP_SERVICE_EMAIL_SECURITY_NONE,
    HTTP_SERVICE_EMAIL_SECURITY_OPTIONAL,
    HTTP_SERVICE_EMAIL_SECURITY_REQUIRED
} HTTP_Service_Email_Security;

/**
 * @brief Stage a send reached, so a caller can tell configuration from delivery.
 * @note HTTP_SERVICE_EMAIL_STATUS_INVALID_CONFIGURATION is never produced by a
 *       send - a service that reached one is already refused whole. It exists
 *       for http_service_email_init_from_env_2, which needs to say "the
 *       environment named a relay and it was refused" apart from
 *       HTTP_SERVICE_EMAIL_STATUS_NOT_CONFIGURED's "it named none". It is last
 *       in the list so the existing values keep the numbers they had.
 */
typedef enum {
    HTTP_SERVICE_EMAIL_STATUS_OK,
    HTTP_SERVICE_EMAIL_STATUS_NOT_CONFIGURED,
    HTTP_SERVICE_EMAIL_STATUS_INVALID_MESSAGE,
    HTTP_SERVICE_EMAIL_STATUS_RENDER_FAILED,
    HTTP_SERVICE_EMAIL_STATUS_TRANSPORT_INIT_FAILED,
    HTTP_SERVICE_EMAIL_STATUS_TRANSPORT_FAILED,
    HTTP_SERVICE_EMAIL_STATUS_INVALID_CONFIGURATION
} HTTP_Service_Email_Status;

/**
 * @brief Everything a payload needs that is NOT in the message: the clock and
 *        the entropy.
 *
 * Date, Message-ID, and the MIME boundary would otherwise make every render
 * different, which is exactly what a golden test cannot have. Filling this from
 * the clock and the RNG is http_service_email_render_init's job;
 * http_service_email_payload_create_2 accepts one already filled, so a test
 * pins byte-exact output and a caller can reproduce a payload it logged.
 *
 * Both char arrays are VALIDATED on the way in, because a caller-filled context
 * is data: each must be NUL-terminated inside its own capacity, carry no
 * control byte, and the boundary must carry neither '"' nor a space (it is
 * rendered into a quoted Content-Type parameter). The Message-ID must also BE
 * one: it opens with '<', closes with '>', and carries no interior '<', '>' or
 * space, because the renderer emits it verbatim after "Message-ID: ". A context
 * failing any of those is refused, not rendered.
 */
typedef struct {
    /** @brief MIME multipart boundary, WITHOUT the leading "--". */
    char boundary[HTTP_SERVICE_EMAIL_BOUNDARY_MAX_SIZE];
    /** @brief Timestamp the Date header renders (naive UTC; rendered as +0000). */
    Datetime date;
    /** @brief Message-ID value INCLUDING its angle brackets. */
    char message_id[HTTP_SERVICE_EMAIL_MESSAGE_ID_MAX_SIZE];
} HTTP_Service_Email_Render;

/**
 * @brief SMTP email service configuration.
 */
typedef struct {
#ifdef ARENA_IMPLEMENTATION
    /** @brief Optional arena used by owned values and returned strings. */
    Arena *allocator;
#endif // ARENA_IMPLEMENTATION
    /** @brief Optional SASL mechanism list for CURLOPT_LOGIN_OPTIONS, e.g. "AUTH=PLAIN". */
    String auth_mechanism;
    /** @brief Optional CA bundle path for TLS verification; empty uses curl's default store. */
    String ca_bundle;
    /** @brief SMTP connection timeout in milliseconds. Never 0. */
    USize connect_timeout_ms;
    /** @brief Default envelope/header sender address. */
    String from;
    /** @brief Optional default sender display name. */
    String from_name;
    /** @brief SMTP password or provider token. */
    String password;
    /** @brief Transport security mode. */
    HTTP_Service_Email_Security security;
    /** @brief SMTP total send timeout in milliseconds. Never 0. */
    USize timeout_ms;
    /** @brief Borrowed capture sink; non-null replaces the SMTP transport. */
    String *transport_sink;
    /** @brief SMTP URL, for example smtp://host:587 or smtps://host:465. */
    String url;
    /** @brief SMTP username. */
    String username;
    /** @brief Whether TLS peer and host verification are enabled. */
    bool verify_tls;
} HTTP_Service_Email;

/**
 * @brief Email message data.
 */
typedef struct {
#ifdef ARENA_IMPLEMENTATION
    /** @brief Optional arena used by owned values. */
    Arena *allocator;
#endif // ARENA_IMPLEMENTATION
    /** @brief Blind-copy recipients used by SMTP envelope only. */
    AL_Str bcc;
    /** @brief Copy recipients. */
    AL_Str cc;
    /** @brief Message-specific sender address. */
    String from;
    /** @brief Message-specific sender display name. */
    String from_name;
    /** @brief Additional raw RFC 5322 header lines. */
    AL_Str headers;
    /** @brief HTML body, stored CRLF-normalized. */
    String html;
    /** @brief Reply-To address. */
    String reply_to;
    /** @brief Message subject. */
    String subject;
    /** @brief Plain text body, stored CRLF-normalized. */
    String text;
    /** @brief Primary recipients. */
    AL_Str to;
} HTTP_Service_Email_Message;

/**
 * @brief Send result data.
 */
typedef struct {
#ifdef ARENA_IMPLEMENTATION
    /** @brief Optional arena used by result strings. */
    Arena *allocator;
#endif // ARENA_IMPLEMENTATION
    /** @brief CURLcode from the SMTP transport; 0 for the capture transport. */
    I32 curl_code;
    /** @brief Error text when send failed. */
    String error;
    /** @brief Reserved for a future HTTP-API transport; SMTP uploads receive no body, so it stays empty. */
    String response;
    /** @brief SMTP response code when available. */
    USize response_code;
    /** @brief Stage the send reached. */
    HTTP_Service_Email_Status status;
    /** @brief Whether the send completed successfully. */
    bool success;
} HTTP_Service_Email_Result;

/*==============================================================================
 * MARK: - API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Initialize an arena-backed email service with defaults, in place.
 * @param self Uninitialized service to fill.
 * @param allocator Arena allocator.
 * @return true when initialized. False leaves `self` untouched and unusable.
 */
bool http_service_email_alloc_init_1(HTTP_Service_Email *const self, Arena *const allocator);

/**
 * @brief Initialize an arena-backed email service with explicit SMTP values, in place.
 * @param self Uninitialized service to fill.
 * @param url SMTP URL; must be smtp:// or smtps:// and must not carry userinfo.
 * @param username SMTP username; may be empty.
 * @param password SMTP password or token; may be empty.
 * @param from Default sender address.
 * @param allocator Arena allocator.
 * @return true when initialized. False means a value was refused or the arena
 *         declined: `self` is untouched, so there is nothing to release.
 */
bool http_service_email_alloc_init_2(HTTP_Service_Email *const self,
    char const *const url, char const *const username, char const *const password, char const *const from, Arena *const allocator);

/**
 * @brief Initialize an arena-backed empty email message, in place.
 * @param self Uninitialized message to fill.
 * @param allocator Arena allocator.
 * @return true when initialized. False leaves `self` untouched and unusable.
 */
bool http_service_email_message_alloc_init_1(HTTP_Service_Email_Message *const self, Arena *const allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Report whether an address is safe to place in an SMTP header.
 * @param email Address to check.
 * @return True when it holds EXACTLY ONE '@' with a non-empty local part and a
 *         non-empty domain, and every byte is printable US-ASCII other than
 *         whitespace, controls and every RFC 5322 §3.2.3 special but '.':
 *         '(', ')', '<', '>', '[', ']', ':', ';', '\', ',' and '"'. Each of
 *         those would permit header injection or change what a receiver parses
 *         - "x:y@d" in a To: is a GROUP, "a(b@d" opens a comment - so the
 *         rendered header would name a different recipient than the RCPT TO
 *         envelope. A byte >= 0x80 is refused: an internationalized address
 *         needs SMTPUTF8, which this transport does not negotiate.
 * @note The local part is capped at 64 octets and the domain at 255 - RFC 5321
 *       §4.5.3.1.1 and §4.5.3.1.2, the sizes every SMTP implementation must
 *       accept. The cap is not pedantry: a recipient list folds BETWEEN
 *       addresses and never inside one, so an uncapped address was rendered as
 *       a single line of its own length, and past 998 octets that is a header
 *       an MTA answers 5xx on.
 */
bool http_service_email_address_valid(char const *const email);

/**
 * @brief Set the SASL mechanism list offered to the relay.
 * @param self Email service.
 * @param mechanism CURLOPT_LOGIN_OPTIONS value, e.g. "AUTH=PLAIN" or "AUTH=*";
 *        empty clears it and lets curl choose.
 * @return true when stored; false when the value carried a control byte or the
 *         allocator refused, in which case the previous value stands.
 */
bool http_service_email_auth_mechanism_set(HTTP_Service_Email *const self, char const *const mechanism);

/**
 * @brief Set SMTP auth values.
 * @param self Email service.
 * @param username SMTP username.
 * @param password SMTP password or token.
 * @return true when both were stored; false when the allocator refused.
 */
bool http_service_email_auth_set(HTTP_Service_Email *const self, char const *const username, char const *const password);

/**
 * @brief Set the CA bundle used to verify the relay's certificate.
 * @param self Email service.
 * @param path PEM bundle path; empty restores curl's default store.
 * @return true when stored; false when the value carried a control byte or the
 *         allocator refused, in which case the previous bundle stands.
 * @note This is the way to trust a private CA. Turning verify_tls off instead
 *       disables verification entirely and is not an equivalent.
 */
bool http_service_email_ca_bundle_set(HTTP_Service_Email *const self, char const *const path);

/**
 * @brief Set default sender display name.
 * @param self Email service.
 * @param name Sender display name; may be empty.
 * @return true when stored; false when it carried a control byte or the
 *         allocator refused.
 * @note The length is NOT capped: the rendered From: line folds at the name's
 *       spaces and is hard-split at 998 octets like every other header value,
 *       so a long name can never produce a line an MTA rejects.
 */
bool http_service_email_from_name_set(HTTP_Service_Email *const self, char const *const name);

/**
 * @brief Set default sender address.
 * @param self Email service.
 * @param email Sender address.
 * @return true when stored; false when it is not a valid address or the
 *         allocator refused, in which case the previous sender stands.
 */
bool http_service_email_from_set(HTTP_Service_Email *const self, char const *const email);

/**
 * @brief Neutralize control bytes in a header-bound value in place.
 * @param value String to sanitize; a nullptr is ignored.
 * @note Each byte below 0x20 or equal to 0x7f (notably CR and LF) is replaced
 *       with a space so the value cannot inject additional SMTP headers. This
 *       is for a caller cleaning a value BEFORE handing it over; the setters
 *       refuse such a value rather than rewriting it.
 */
void http_service_email_header_sanitize(char *const value);

/**
 * @brief Initialize email service with defaults, in place.
 * @param self Uninitialized service to fill.
 * @return true ALWAYS: the default allocates nothing (every String starts
 *         empty), so there is no path on which it can refuse. The service is
 *         VALID but NOT CONFIGURED: it has no URL, so http_service_email_valid
 *         answers false and a send reports
 *         HTTP_SERVICE_EMAIL_STATUS_NOT_CONFIGURED.
 */
bool http_service_email_init_1(HTTP_Service_Email *const self);

/**
 * @brief Initialize email service with explicit SMTP values, in place.
 * @param self Uninitialized service to fill.
 * @param url SMTP URL; must be smtp:// or smtps:// and must not carry userinfo.
 * @param username SMTP username; may be empty.
 * @param password SMTP password or token; may be empty.
 * @param from Default sender address.
 * @return true when initialized. False means a value was refused or the
 *         allocator declined: `self` is untouched, so there is nothing to
 *         release, and the caller can tell a bad URL from an absent one.
 */
bool http_service_email_init_2(HTTP_Service_Email *const self, char const *const url, char const *const username, char const *const password, char const *const from);

/**
 * @brief Initialize an email service from the environment, in place.
 * @param self Uninitialized service to fill.
 * @param prefix Environment variable prefix, e.g. "TRAYMON_SMTP".
 * @return true when <prefix>_URL and <prefix>_FROM were both present and the
 *         service came up configured. False means `self` was initialized to
 *         the UNCONFIGURED default instead - still safe to use and to uninit -
 *         whether the environment named no relay or named one the module
 *         refused. This bool CANNOT tell those two apart, and neither can
 *         http_service_email_valid: the default has no URL, so valid() is
 *         false on both paths. http_service_email_init_from_env_2 reports
 *         which, and http_service_email_status_name puts it in a log line.
 * @note Reads <prefix>_URL, <prefix>_USER, <prefix>_PASSWORD, <prefix>_FROM,
 *       <prefix>_FROM_NAME, <prefix>_STARTTLS and <prefix>_VERIFY_TLS. The
 *       first five are the names the three existing consumers already read by
 *       hand. VERIFY_TLS is honored only when it is exactly "0", which disables
 *       peer and host verification; anything else leaves verification on.
 *       STARTTLS is exactly "none", "optional" or "required"
 *       (HTTP_Service_Email_Security in that order); empty keeps the REQUIRED
 *       default, and any other spelling ALSO stays REQUIRED - the safe side -
 *       and is logged at WARN, because the deployment asked for a mode and did
 *       not get it. FROM_NAME is passed through
 *       http_service_email_from_name_set, so a control byte in it refuses the
 *       whole configuration.
 * @note This tier CANNOT tell an unset variable from a value the module
 *       refused: `TRAYMON_SMTP_URL=smtp:/host` - one missing slash - boots a
 *       server that silently never mails and answers exactly like a machine
 *       where the variable was never set. Use
 *       http_service_email_init_from_env_2 to tell the two apart.
 */
bool http_service_email_init_from_env(HTTP_Service_Email *const self, char const *const prefix);

/**
 * @brief Initialize an email service from the environment, reporting WHY it failed.
 * @param self Uninitialized service to fill.
 * @param prefix Environment variable prefix, e.g. "TRAYMON_SMTP".
 * @param out Receives the outcome. Never read when the function is not called;
 *        always written when it is.
 * @return true when <prefix>_URL and <prefix>_FROM were both present and the
 *         service came up configured, with `out` set to
 *         HTTP_SERVICE_EMAIL_STATUS_OK.
 * @note False separates the two failures the bool tier merges.
 *       HTTP_SERVICE_EMAIL_STATUS_NOT_CONFIGURED means the environment
 *       described no relay - a deliberate local build, not a fault.
 *       HTTP_SERVICE_EMAIL_STATUS_INVALID_CONFIGURATION means it described one
 *       and the module refused it: a URL that is not smtp:// or smtps://, one
 *       carrying userinfo, a sender that is not an address, a display name
 *       carrying a control byte, or an allocator that declined. That one is a
 *       MISCONFIGURED deployment and deserves a loud startup line, because it
 *       looks identical to a working server until the first mail is not
 *       delivered.
 * @note Both false paths leave `self` initialized to the UNCONFIGURED default,
 *       safe to use and to uninit, without exception: that default is
 *       http_service_email_init_1's, which allocates nothing and cannot refuse.
 * @note Reads the same seven variables as http_service_email_init_from_env,
 *       with the same STARTTLS and VERIFY_TLS parsing.
 */
bool http_service_email_init_from_env_2(HTTP_Service_Email *const self, char const *const prefix, HTTP_Service_Email_Status *const out);

/**
 * @brief Add a Bcc recipient.
 * @param self Message instance.
 * @param email Recipient address; a COPY is stored.
 * @return true when the address was stored. False means it was NOT: it was
 *         empty, not a valid address, or the allocator refused. That recipient
 *         will not be contacted, so a caller that ignores this sends a message
 *         it believes went out.
 * @note Bcc is an ENVELOPE-only field: these addresses become RCPT TO commands
 *       and never appear in the rendered headers.
 */
bool http_service_email_message_bcc_add_1(HTTP_Service_Email_Message *const self, char const *const email);

/**
 * @brief Add a Bcc recipient from a String value.
 * @param self Message instance.
 * @param email Recipient address; the String's data is COPIED.
 * @return true when the address was stored; see _1 for what false costs.
 */
bool http_service_email_message_bcc_add_2(HTTP_Service_Email_Message *const self, String const *const email);

/**
 * @brief Add a Cc recipient.
 * @param self Message instance.
 * @param email Recipient address; a COPY is stored.
 * @return true when the address was stored; see bcc_add_1 for what false costs.
 */
bool http_service_email_message_cc_add_1(HTTP_Service_Email_Message *const self, char const *const email);

/**
 * @brief Add a Cc recipient from a String value.
 * @param self Message instance.
 * @param email Recipient address; the String's data is COPIED.
 * @return true when the address was stored; see bcc_add_1 for what false costs.
 */
bool http_service_email_message_cc_add_2(HTTP_Service_Email_Message *const self, String const *const email);

/**
 * @brief Set message-specific sender display name.
 * @param self Message instance.
 * @param name Sender display name; may be empty.
 * @return true when stored; false when it carried a control byte or the
 *         allocator refused.
 * @note Not length-capped; see http_service_email_from_name_set.
 */
bool http_service_email_message_from_name_set(HTTP_Service_Email_Message *const self, char const *const name);

/**
 * @brief Set message-specific sender.
 * @param self Message instance.
 * @param email Sender address; empty clears it back to the service default.
 * @return true when stored; false when it is not a valid address or the
 *         allocator refused.
 */
bool http_service_email_message_from_set(HTTP_Service_Email_Message *const self, char const *const email);

/**
 * @brief Add a raw header line.
 * @param self Message instance.
 * @param header Header line "Name: value", without a trailing CRLF.
 * @return true when the header line was stored. False means it was NOT: it was
 *         empty, carried a control byte, started with whitespace, had no ':'
 *         after a non-empty name, named a header this module OWNS (From, To,
 *         Cc, Bcc, Reply-To, Subject, Date, Message-ID, MIME-Version,
 *         Content-Type, Content-Transfer-Encoding), or the allocator refused.
 *         The message is still sendable but goes out WITHOUT that header, so a
 *         caller relying on one it set for routing or threading gets different
 *         handling than it asked for.
 * @note Reply-To is on that list because the module renders it itself from
 *       http_service_email_message_reply_to_set - a second one added here would
 *       not replace it, and a receiver picking either address is a reply routed
 *       somewhere the sender did not choose. Set it through the setter.
 * @note A stored line is FOLDED when rendered, at its spaces, so no line passes
 *       78 columns while there is a space left to fold at. A run with NO space
 *       in it is left whole up to 998 octets and hard-split only past that -
 *       unfolding keeps the inserted whitespace (RFC 5322 §2.2.3), so splitting
 *       an unbreakable token any earlier would deliver a 90-character
 *       List-Unsubscribe URL with a space in the middle of it.
 */
bool http_service_email_message_header_add(HTTP_Service_Email_Message *const self, char const *const header);

/**
 * @brief Set HTML body.
 * @param self Message instance.
 * @param html HTML body; stored CRLF-normalized.
 * @return true when stored; false when the allocator refused.
 */
bool http_service_email_message_html_set(HTTP_Service_Email_Message *const self, char const *const html);

/**
 * @brief Initialize an empty email message, in place.
 * @param self Uninitialized message to fill.
 * @return true when initialized. False leaves `self` untouched and unusable.
 */
bool http_service_email_message_init_1(HTTP_Service_Email_Message *const self);

/**
 * @brief Set Reply-To address.
 * @param self Message instance.
 * @param email Reply-To address; empty clears it.
 * @return true when stored; false when it is not a valid address or the
 *         allocator refused.
 */
bool http_service_email_message_reply_to_set(HTTP_Service_Email_Message *const self, char const *const email);

/**
 * @brief Set message subject.
 * @param self Message instance.
 * @param subject Subject text; UTF-8 is fine and is B-encoded when rendered.
 * @return true when stored; false when it carried a control byte (a CR or LF
 *         would inject headers) or the allocator refused.
 * @note An empty subject is legal and renders no Subject header, but scores
 *       badly with spam filters - set one.
 */
bool http_service_email_message_subject_set(HTTP_Service_Email_Message *const self, char const *const subject);

/**
 * @brief Set plain text body.
 * @param self Message instance.
 * @param text Plain text body; stored CRLF-normalized.
 * @return true when stored; false when the allocator refused.
 */
bool http_service_email_message_text_set(HTTP_Service_Email_Message *const self, char const *const text);

/**
 * @brief Add a primary recipient.
 * @param self Message instance.
 * @param email Recipient address; a COPY is stored.
 * @return true when the address was stored; see bcc_add_1 for what false costs.
 */
bool http_service_email_message_to_add_1(HTTP_Service_Email_Message *const self, char const *const email);

/**
 * @brief Add a primary recipient from a String value.
 * @param self Message instance.
 * @param email Recipient address; the String's data is COPIED.
 * @return true when the address was stored; see bcc_add_1 for what false costs.
 */
bool http_service_email_message_to_add_2(HTTP_Service_Email_Message *const self, String const *const email);

/**
 * @brief Release message storage.
 * @param self Message instance.
 */
void http_service_email_message_uninit(HTTP_Service_Email_Message *const self);

/**
 * @brief Validate message against service defaults.
 * @param self Email service.
 * @param message Message instance.
 * @return true when ALL FIVE of these hold, and false the moment one does not:
 *         (1) a non-empty sender - the message's own, or the service's default
 *         when the message set none; (2) at least one recipient across to, cc
 *         and bcc; (3) a non-empty text body OR a non-empty html body; (4) every
 *         address present - sender, every to/cc/bcc entry, and reply_to when it
 *         is set - passes http_service_email_address_valid; (5) the display name
 *         in force, the subject and every custom header line carry no control
 *         byte. The answer is one bool, so a caller that needs to tell a user
 *         WHICH input was wrong has to check its own inputs; the setters and
 *         adders already refuse (4) and (5) at the point the value arrives,
 *         which is where a caller can still name the field.
 */
bool http_service_email_message_valid(HTTP_Service_Email const *const self, HTTP_Service_Email_Message const *const message);

/**
 * @brief Build the RFC 5322/MIME payload for a message, dating it NOW.
 * @param self Email service.
 * @param message Message instance.
 * @param out Uninitialized String receiving the payload.
 * @return true when the payload was built. False leaves `out` untouched: the
 *         service is unconfigured, the message is invalid, or entropy failed.
 * @note The output is NOT dot-stuffed. libcurl stuffs on upload, so the SMTP
 *       transport is correct, and so does the capture transport, so its
 *       transcript is still the wire; a caller piping THIS payload into
 *       sendmail or a raw socket must stuff leading '.' lines itself.
 */
bool http_service_email_payload_create_1(HTTP_Service_Email const *const self, HTTP_Service_Email_Message const *const message, String *const out);

/**
 * @brief Build the RFC 5322/MIME payload for a message from a FIXED render context.
 * @param self Email service.
 * @param message Message instance.
 * @param render Date, Message-ID and MIME boundary to render with.
 * @param out Uninitialized String receiving the payload.
 * @return true when the payload was built. False leaves `out` untouched: the
 *         service is unconfigured, the message is invalid, the render context
 *         failed its validation (see HTTP_Service_Email_Render), or the
 *         allocator refused - including PARTWAY through, which is refused
 *         whole rather than handed back as a truncated message.
 * @note Deterministic: the same service, message and render context always
 *       produce the same bytes. This is the seam the suite's goldens pin, and
 *       what http_service_email_payload_create_1 and the transports call.
 */
bool http_service_email_payload_create_2(HTTP_Service_Email const *const self,
    HTTP_Service_Email_Message const *const message, HTTP_Service_Email_Render const *const render, String *const out);

/**
 * @brief Fill a render context from the clock and the RNG.
 * @param self Email service (its sender's domain names the Message-ID).
 * @param message Message instance (a message-specific sender wins).
 * @param out Render context to fill.
 * @return true when filled. False means the RNG refused, and `out` is
 *         unspecified - a message must not be rendered with it.
 */
bool http_service_email_render_init(HTTP_Service_Email const *const self, HTTP_Service_Email_Message const *const message, HTTP_Service_Email_Render *const out);

/**
 * @brief Release result storage.
 * @param self Result instance.
 */
void http_service_email_result_uninit(HTTP_Service_Email_Result *const self);

/**
 * @brief Set SMTP security mode.
 * @param self Email service.
 * @param security Security mode.
 * @return true always; the parameter is an enum and cannot be refused.
 */
bool http_service_email_security_set(HTTP_Service_Email *const self, HTTP_Service_Email_Security const security);

/**
 * @brief Send a message through the configured transport.
 * @param self Email service.
 * @param message Message instance.
 * @return Send result; inspect `status` before `curl_code`.
 * @note A capture send answers success=false with
 *       HTTP_SERVICE_EMAIL_STATUS_RENDER_FAILED when the sink's allocator
 *       refused, so a truncated transcript is never reported as a send.
 * @note HTTP_SERVICE_EMAIL_STATUS_TRANSPORT_INIT_FAILED also covers a
 *       recipient list curl would not accept: nothing is dialed, and the
 *       message is never delivered to a SUBSET of the addresses it named.
 * @note With the SMTP transport this BLOCKS for up to connect_timeout_ms +
 *       timeout_ms. Do not call it from an lws event-loop handler: one slow
 *       relay freezes every other client for that long. Hand it to a worker.
 * @note Recipients are NOT de-duplicated. An address listed in both `to` and
 *       `cc` - or in `to` and `bcc` - becomes two RCPT TO commands, and some
 *       relays deliver the message twice. A caller assembling a list from more
 *       than one source should de-duplicate it before adding.
 * @note Concurrent sends need curl's global init to have already run; see the
 *       Thread Safety section in this file. A program that uses email without
 *       http/client must call http_client_global_init once at startup, because
 *       curl_easy_init's lazy fallback global init is not thread-safe.
 */
HTTP_Service_Email_Result http_service_email_send(HTTP_Service_Email const *const self, HTTP_Service_Email_Message const *const message);

/**
 * @brief The name of a send or configuration outcome, for a log line.
 * @param status Outcome to name.
 * @return "ok", "not_configured", "invalid_message", "render_failed",
 *         "transport_init_failed", "transport_failed" or
 *         "invalid_configuration"; "unknown" for a value outside the enum.
 *         Read-only, statically allocated, never null; do not free or modify it.
 * @note Every consumer logging a failed send was hand-writing these six names.
 */
char* http_service_email_status_name(HTTP_Service_Email_Status const status);

/**
 * @brief Set SMTP timeout values.
 * @param self Email service.
 * @param connect_timeout_ms Connect timeout in milliseconds; must be > 0.
 * @param timeout_ms Total timeout in milliseconds; must be > 0.
 * @return true when both were stored. False when either is 0 - a zero timeout
 *         means "wait forever" to curl, which on a request path is a hang, so
 *         it is refused as a VALUE rather than aborting; the previous timeouts
 *         stand.
 */
bool http_service_email_timeouts_set(HTTP_Service_Email *const self, USize const connect_timeout_ms, USize const timeout_ms);

/**
 * @brief Redirect sends into a String instead of dialing a relay.
 * @param self Email service.
 * @param sink BORROWED String receiving "MAIL FROM:<...>", one "RCPT TO:<...>"
 *        per envelope recipient, "DATA", the DOT-STUFFED payload and the "."
 *        terminator - each CRLF-terminated. Pass nullptr to restore the SMTP
 *        transport.
 * @return true always.
 * @note The caller owns `sink` and must keep it alive while the service points
 *       at it. Each send APPENDS, so a caller inspecting one message should
 *       clear the sink between sends.
 */
bool http_service_email_transport_capture_set(HTTP_Service_Email *const self, String *const sink);

/**
 * @brief Release email service storage.
 * @param self Email service.
 * @note Does NOT call curl_global_cleanup. This module's curl bring-up runs
 *       once per process and is balanced by http_client_global_uninit or by the
 *       owning program, exactly as http/client documents - the old per-service
 *       cleanup ran while other curl users were still live.
 */
void http_service_email_uninit(HTTP_Service_Email *const self);

/**
 * @brief Set SMTP URL.
 * @param self Email service.
 * @param url SMTP URL.
 * @return true when stored. False when the scheme is not smtp:// or smtps://,
 *         when the authority carries userinfo (credentials in a URL leak into
 *         curl's error text, which callers log), or when the allocator refused;
 *         the previous URL stands.
 */
bool http_service_email_url_set(HTTP_Service_Email *const self, char const *const url);

/**
 * @brief Validate service SMTP configuration.
 * @param self Email service.
 * @return true when the configuration can send: a non-empty smtp:// or smtps://
 *         URL without userinfo, a non-empty default sender, and both timeouts
 *         above 0.
 */
bool http_service_email_valid(HTTP_Service_Email const *const self);

/**
 * @brief Set TLS verification behavior.
 * @param self Email service.
 * @param verify_tls true to verify peer and host.
 * @return true always.
 */
bool http_service_email_verify_tls_set(HTTP_Service_Email *const self, bool const verify_tls);

#endif // HTTP_SERVICE_EMAIL_H