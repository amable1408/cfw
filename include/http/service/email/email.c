#include <http/service/email/email.h>

/* Every module below is an IMPLEMENTATION dependency: none of them names a type or function in
 * email.h's API, whose only borrowed type is AL_Str. They are included here rather than chained
 * through the header so that a consumer of email.h - and, for the public export, email's
 * dependency closure - carries only what the API actually needs.
 *
 * char.h is singled out because it is the one module the chain rule can NEVER cover: it
 * declares no types at all, only functions and macros, so no header's API can name a type from
 * it and every chain reaching it is accidental by the rule's own test. Whatever calls a char_*
 * function includes char.h itself. */
#include <char/char.h>
#include <crypto/random/random.h>
#include <encoding/base64/base64.h>
#include <encoding/quoted_printable/quoted_printable.h>
#include <env/env.h>
#include <http/client/http_client.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

/* RFC 5321 §4.5.3.1.1 and §4.5.3.1.2: 64 octets of local part and 255 of domain
 * are the sizes every implementation must accept. Capping there is what keeps a
 * single address inside a legal header line - a recipient list folds BETWEEN
 * addresses, never inside one, so an uncapped 1000-octet address was still one
 * line of its own - and what lets an SMTP envelope be built in a fixed buffer. */
#define _HTTP_SERVICE_EMAIL_ADDRESS_DOMAIN_MAX_LENGTH 255

#define _HTTP_SERVICE_EMAIL_ADDRESS_LOCAL_MAX_LENGTH 64

/* Random bytes behind a MIME boundary. A body the caller does not control (a
 * CRM reminder note, a forwarded quote) that happened to contain the old FIXED
 * boundary split the MIME structure; 12 bytes of entropy makes that a guess
 * nobody wins, and stops the boundary being a fingerprint of this framework. */
#define _HTTP_SERVICE_EMAIL_BOUNDARY_BYTE_COUNT 12

#define _HTTP_SERVICE_EMAIL_BOUNDARY_PREFIX "=_cfw_"

/* Date is rendered at UTC with an explicit +0000. The day-of-week is optional in
 * RFC 5322 §3.3 and datetime_format has no %a, so it is left off rather than
 * fabricated. */
#define _HTTP_SERVICE_EMAIL_DATE_FORMAT "%d %b %Y %H:%M:%S +0000"

#define _HTTP_SERVICE_EMAIL_DATE_MAX_SIZE 48

/* RFC 2047 §2 caps an encoded word at 75 characters. "=?UTF-8?B?" + "?=" costs
 * 12, so 63 base64 characters fit, and 45 raw bytes encode to exactly 60. */
#define _HTTP_SERVICE_EMAIL_ENCODED_WORD_BYTE_COUNT 45

#define _HTTP_SERVICE_EMAIL_ENCODED_WORD_PREFIX "=?UTF-8?B?"
#define _HTTP_SERVICE_EMAIL_ENCODED_WORD_SUFFIX "?="

/* "<" + the longest address _http_service_email_bytes_address_ok accepts + ">"
 * + NUL, so an envelope path is built on the stack and an arena-backed service
 * stops heap-allocating twice per recipient per send. */
#define _HTTP_SERVICE_EMAIL_ENVELOPE_MAX_SIZE \
    (_HTTP_SERVICE_EMAIL_ADDRESS_LOCAL_MAX_LENGTH + CHAR_STATIC_SIZE("@") + _HTTP_SERVICE_EMAIL_ADDRESS_DOMAIN_MAX_LENGTH + CHAR_STATIC_SIZE("<>") + CHAR_END_CHARACTER)

/* The line length an MTA actually answers 5xx on: RFC 5322 §2.1.1's 998 octets,
 * excluding the CRLF. It is deliberately NOT the fold column. Unfolding removes
 * the CRLF and KEEPS the WSP (§2.2.3), so a token hard-split at 78 arrives with
 * a SPACE inside it - a 90-character List-Unsubscribe URL is delivered broken,
 * and that is the ordinary case, not a pathological one. Splitting only at 998
 * corrupts nothing an MTA would have accepted whole, and the space fold at
 * HTTP_SERVICE_EMAIL_HEADER_LINE_MAX_LENGTH still covers every value that HAS a
 * space to fold at. */
#define _HTTP_SERVICE_EMAIL_HEADER_LINE_HARD_LIMIT 998

#define _HTTP_SERVICE_EMAIL_MESSAGE_ID_BYTE_COUNT 16

/* Header names this module renders itself. A caller-supplied duplicate does not
 * replace the module's line, it ADDS a second one, and a receiver picking the
 * wrong Content-Type or an injected Bcc is exactly the kind of silent rewrite
 * header_add must not permit. */
static char const *const _HTTP_SERVICE_EMAIL_OWNED_HEADERS[] = {
    "bcc",
    "cc",
    "content-transfer-encoding",
    "content-type",
    "date",
    "from",
    "message-id",
    "mime-version",
    "reply-to",
    "subject",
    "to"
};

/* Headroom over the message's own bytes for the rendered headers and the ~3%
 * quoted-printable expansion of mostly-ASCII text, so the payload String does
 * not regrow on the common message. */
#define _HTTP_SERVICE_EMAIL_PAYLOAD_HEADROOM 1024

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

typedef struct {
    USize           offset;
    String  const   *payload;
} HTTP_Service_Email_Upload;

/*==============================================================================
 * MARK: - Helpers
 *============================================================================*/

/* Clamps a USize to LONG_MAX before the (long) cast curl_easy_setopt's timeout
 * options require. `long` is 32-bit on this tree's LLP64 Windows target, so a
 * configured value past it narrowed into an implementation-defined (commonly
 * negative) result, and curl reads a negative CURLOPT_TIMEOUT_MS as an error -
 * leaving the send with curl's own default rather than the bound the caller
 * asked for, on a path that may sit on a request. */
static long _http_service_email_long_clamp(USize const value) {
    return value > (USize) LONG_MAX ? LONG_MAX : (long) value;
}

static String const *_http_service_email_value_or_default(String const *const value, String const *const fallback) {
    return !string_empty(value) ? value : fallback;
}

#ifdef ARENA_IMPLEMENTATION
static String _http_service_email_string_init(Arena *const allocator)
#else
static String _http_service_email_string_init(void)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    if (allocator != nullptr) {
        String const string = string_alloc_init_1(allocator);

        trace_log_pop();

        return string;
    }
#endif // ARENA_IMPLEMENTATION

    String const string = string_init_1();

    trace_log_pop();

    return string;
}

#ifdef ARENA_IMPLEMENTATION
static String _http_service_email_string_init_sized(USize const capacity, Arena *const allocator)
#else
static String _http_service_email_string_init_sized(USize const capacity)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    if (allocator != nullptr) {
        String const string = string_alloc_init_2(capacity, allocator);

        trace_log_pop();

        return string;
    }
#endif // ARENA_IMPLEMENTATION

    String const string = string_init_2(capacity);

    trace_log_pop();

    return string;
}

#ifdef ARENA_IMPLEMENTATION
static String _http_service_email_char_to_string(char const *const data, Arena *const allocator)
#else
static String _http_service_email_char_to_string(char const *const data)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    USize const data_size = char_length(data);

    if (data_size == 0) {
#ifdef ARENA_IMPLEMENTATION
        String const string = _http_service_email_string_init(allocator);
#else
        String const string = _http_service_email_string_init();
#endif // ARENA_IMPLEMENTATION

        trace_log_pop();

        return string;
    }

#ifdef ARENA_IMPLEMENTATION
    if (allocator != nullptr) {
        String const string = string_alloc_init_static((char*) data, data_size, allocator);

        trace_log_pop();

        return string;
    }
#endif // ARENA_IMPLEMENTATION

    String const string = string_init_static((char*) data, data_size);

    trace_log_pop();

    return string;
}

/* The empty guard lives HERE rather than at every call site: str_alloc_init_static
 * routes a zero size into error_check_non_value_uint, which ABORTS, and an empty
 * address is data (a blank users.email cell), never a programming error. */
#ifdef ARENA_IMPLEMENTATION
static Str _http_service_email_char_to_str(char const *const data, USize const data_size, Arena *const allocator)
#else
static Str _http_service_email_char_to_str(char const *const data, USize const data_size)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    if (data_size == 0) {
        Str const empty = str_init_1();

        trace_log_pop();

        return empty;
    }

#ifdef ARENA_IMPLEMENTATION
    if (allocator != nullptr) {
        Str const str = str_alloc_init_static(data, data_size, allocator);

        trace_log_pop();

        return str;
    }
#endif // ARENA_IMPLEMENTATION

    Str const str = str_init_static((char*) data, data_size);

    trace_log_pop();

    return str;
}

/* Replaces `self` with a copy of `data`, reporting whether the copy actually
 * happened. A non-empty source that comes back empty is the allocator's
 * refusal, and the caller keeps its old value rather than silently losing it. */
#ifdef ARENA_IMPLEMENTATION
static bool _http_service_email_string_set(String *const self, char const *const data, Arena *const allocator)
#else
static bool _http_service_email_string_set(String *const self, char const *const data)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

#ifdef ARENA_IMPLEMENTATION
    String replacement = _http_service_email_char_to_string(data, allocator);
#else
    String replacement = _http_service_email_char_to_string(data);
#endif // ARENA_IMPLEMENTATION

    if (char_length(data) > 0 && string_get_size(&replacement) == 0) {
        string_uninit(&replacement);

        trace_log_pop();

        return false;
    }

    string_uninit(self);

    *self = replacement;

    trace_log_pop();

    return true;
}

/* The one append every renderer goes through, reporting whether the bytes
 * actually landed. string_add_last_* DECLINE rather than growing when the
 * allocator refuses, and report it only by leaving the size alone; discarding
 * that outcome let an arena refusal truncate a message mid-header while
 * text_set, payload_create_2 and the capture transport all still answered
 * success - a well-formed prefix of a message the caller believes went out. */
static bool _http_service_email_bytes_add(String *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Appending nothing is a no-op that SUCCEEDED: string_add_2 returns early on
     * a zero size, so a size comparison alone would read it as a refusal. */
    if (data_size == 0) {
        trace_log_pop();

        return true;
    }

    USize const stored_before = string_get_size(self);

    string_add_last_2(self, (char*) data, data_size);

    bool const added = string_get_size(self) == stored_before + data_size;

    trace_log_pop();

    return added;
}

static bool _http_service_email_string_add(String *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    bool const added = _http_service_email_bytes_add(self, data, char_length(data));

    trace_log_pop();

    return added;
}

static bool _http_service_email_string_add_2(String *const self, String const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty String's data pointer is NULL, which _bytes_add's own null check
     * would abort on; an empty value is nothing to append, not a refusal. */
    if (string_empty(data)) {
        trace_log_pop();

        return true;
    }

    bool const added = _http_service_email_bytes_add(self, string_get_data(data), string_get_size(data));

    trace_log_pop();

    return added;
}

static bool _http_service_email_str_add(String *const self, Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    if (str_empty(data)) {
        trace_log_pop();

        return true;
    }

    bool const added = _http_service_email_bytes_add(self, str_get_data(data), str_get_size(data));

    trace_log_pop();

    return added;
}

/* Appends `data` with every line ending rewritten to CRLF: a bare LF and a lone
 * CR both become CRLF, an existing CRLF passes through untouched. RFC 5322
 * forbids a bare LF outright - Gmail bounces such a message 5.7.1 "not RFC 5322
 * compliant", Exchange Online with BareLinefeedsAreIllegal - and curl's
 * dot-stuffing only recognizes CRLF, so a "\n.\n" line was never stuffed. */
static bool _http_service_email_crlf_add(String *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    bool    added = true;
    USize   index = 0;
    USize   run   = 0;

    while (added && index < data_size) {
        char const c = data[index];

        if (c != '\r' && c != '\n') {
            run += 1;
            index += 1;

            continue;
        }

        if (run > 0) {
            added = _http_service_email_bytes_add(self, data + index - run, run);

            run = 0;
        }

        added = added && _http_service_email_bytes_add(self, "\r\n", CHAR_STATIC_SIZE("\r\n"));

        index += c == '\r' && index + 1 < data_size && data[index + 1] == '\n' ? 2 : 1;
    }

    if (added && run > 0) {
        added = _http_service_email_bytes_add(self, data + index - run, run);
    }

    trace_log_pop();

    return added;
}

#ifdef ARENA_IMPLEMENTATION
static bool _http_service_email_crlf_set(String *const self, char const *const data, Arena *const allocator)
#else
static bool _http_service_email_crlf_set(String *const self, char const *const data)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    USize const data_size = char_length(data);

#ifdef ARENA_IMPLEMENTATION
    String replacement = _http_service_email_string_init_sized(data_size + CHAR_END_CHARACTER, allocator);
#else
    String replacement = _http_service_email_string_init_sized(data_size + CHAR_END_CHARACTER);
#endif // ARENA_IMPLEMENTATION

    bool const added = _http_service_email_crlf_add(&replacement, data, data_size);

    /* The size test alone only catches a body that landed NOTHING; `added` is
     * what catches the arena that refused halfway through, which used to store a
     * truncated body and report it stored. */
    if (!added || (data_size > 0 && string_get_size(&replacement) == 0)) {
        string_uninit(&replacement);

        trace_log_pop();

        return false;
    }

    string_uninit(self);

    *self = replacement;

    trace_log_pop();

    return true;
}

static bool _http_service_email_list_add_1(AL_Str *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty address is data, not a programming error: these lists are filled
     * from DB cells and config, so aborting here turned an empty users.email
     * into a server kill. Skip it and report false for the same reason a
     * refusal is - the address is not in the list either way, which is what the
     * caller needs to act on. */
    if (data_size == 0) {
        trace_log_pop();

        return false;
    }

#ifdef ARENA_IMPLEMENTATION
    Str temp = _http_service_email_char_to_str(data, data_size, self->allocator);
#else
    Str temp = _http_service_email_char_to_str(data, data_size);
#endif // ARENA_IMPLEMENTATION

    /* The arena path degrades to the EMPTY Str when it refuses the copy, and an empty
     * address is exactly what the guard above refuses to store. */
    if (str_get_size(&temp) == 0) {
        str_uninit(&temp);

        trace_log_pop();

        return false;
    }

    USize const stored_before = al_str_get_size(self);

    al_str_add_last(self, &temp);

    /* add_last DECLINES rather than growing when the allocator refuses, and reports it
     * by leaving the size alone. `temp` owns a fresh COPY of the address, so a dropped
     * node puts that copy beyond the list's uninit. */
    if (al_str_get_size(self) == stored_before) {
        str_uninit(&temp);

        trace_log_pop();

        return false;
    }

    trace_log_pop();

    return true;
}

static bool _http_service_email_recipient_exists(HTTP_Service_Email_Message const *const message) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "message", (void*) message);

    bool const exists = al_str_get_size(&message->to) > 0 || al_str_get_size(&message->cc) > 0 || al_str_get_size(&message->bcc) > 0;

    trace_log_pop();

    return exists;
}

/* Exactly one '@', a non-empty local part and a non-empty domain, and no byte
 * that could end the header early or break the address parse: every RFC 5322
 * §3.2.3 special but '.' is refused, because an address is rendered BARE into
 * To:/Cc:/From: and each of them changes what a receiver parses there. A ':'
 * turns "x:y@d" into a GROUP, a '(' opens a comment, a ';' closes a group, a
 * '[' opens a domain-literal and a '\' starts a quoted-pair - so the rendered
 * header named a different recipient than the RCPT TO envelope did. A byte
 * >= 0x80 is refused deliberately: an internationalized address needs SMTPUTF8,
 * which this transport never negotiates, so accepting one would produce a
 * message the relay rejects rather than an address that works.
 *
 * The two length caps are RFC 5321 §4.5.3.1.1/.2, and they are a RENDERING
 * guard as much as a parsing one: _list_payload_add folds BETWEEN recipients and
 * never inside one, so a single uncapped address went out as one line of its own
 * length - past 998 octets, a header the relay answers 5xx on. They also bound
 * the envelope buffer at _HTTP_SERVICE_EMAIL_ENVELOPE_MAX_SIZE. */
static bool _http_service_email_bytes_address_ok(char const *const data, USize const size) {
    trace_log_push(LOG_METADATA);

    USize   const   count       = data == nullptr ? 0 : size;
    USize           at_count    = 0;
    USize           at_index    = 0;
    bool            success     = count > 0;

    for (USize i = 0; success && i < count; i += 1) {
        U8 const c = (U8) data[i];

        if (c <= 0x20 || c >= 0x7f || c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' ||
            c == ':' || c == ';' || c == '\\' || c == ',' || c == '"') {
            success = false;
        }
        else if (c == '@') {
            at_count += 1;
            at_index = i;
        }
    }

    success = success && at_count == 1 && at_index > 0 && at_index + 1 < count
           && at_index <= _HTTP_SERVICE_EMAIL_ADDRESS_LOCAL_MAX_LENGTH
           && count - at_index - 1 <= _HTTP_SERVICE_EMAIL_ADDRESS_DOMAIN_MAX_LENGTH;

    trace_log_pop();

    return success;
}

static bool _http_service_email_bytes_header_safe(char const *const data, USize const size) {
    trace_log_push(LOG_METADATA);

    USize   const   count   = data == nullptr ? 0 : size;
    bool            success = true;

    for (USize i = 0; success && i < count; i += 1) {
        U8 const c = (U8) data[i];

        if (c < 0x20 || c == 0x7f) {
            success = false;
        }
    }

    trace_log_pop();

    return success;
}

/* A caller-supplied header line has to BE a header line: a name of at least one
 * character, no leading whitespace (which would make it a continuation of
 * whatever the module emitted last), a ':' terminating the name, and a name the
 * module does not render itself. */
static bool _http_service_email_header_line_ok(char const *const data, USize const size) {
    trace_log_push(LOG_METADATA);

    if (data == nullptr || size == 0 || data[0] == ' ' || data[0] == '\t') {
        trace_log_pop();

        return false;
    }

    if (!_http_service_email_bytes_header_safe(data, size)) {
        trace_log_pop();

        return false;
    }

    USize name_size = 0;

    while (name_size < size && data[name_size] != ':') {
        if (data[name_size] == ' ' || data[name_size] == '\t') {
            trace_log_pop();

            return false;
        }

        name_size += 1;
    }

    if (name_size == 0 || name_size == size) {
        trace_log_pop();

        return false;
    }

    for (USize i = 0; i < sizeof(_HTTP_SERVICE_EMAIL_OWNED_HEADERS) / sizeof(_HTTP_SERVICE_EMAIL_OWNED_HEADERS[0]); i += 1) {
        char    const   *const  owned       = _HTTP_SERVICE_EMAIL_OWNED_HEADERS[i];
        USize   const           owned_size  = char_length(owned);
        bool                    same        = owned_size == name_size;

        for (USize j = 0; same && j < owned_size; j += 1) {
            same = char_to_lower(data[j]) == owned[j];
        }

        if (same) {
            trace_log_pop();

            return false;
        }
    }

    trace_log_pop();

    return true;
}

static bool _http_service_email_address_list_ok(AL_Str const *const list) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "list", (void*) list);

    bool success = true;

    for (USize i = 0; success && i < al_str_get_size(list); i += 1) {
        Str const *const entry = al_str_at(list, i);

        success = _http_service_email_bytes_address_ok(str_get_data(entry), str_get_size(entry));
    }

    trace_log_pop();

    return success;
}

static bool _http_service_email_header_list_safe(AL_Str const *const list) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "list", (void*) list);

    bool success = true;

    for (USize i = 0; success && i < al_str_get_size(list); i += 1) {
        Str const *const entry = al_str_at(list, i);

        success = _http_service_email_header_line_ok(str_get_data(entry), str_get_size(entry));
    }

    trace_log_pop();

    return success;
}

static bool _http_service_email_string_address_ok(String const *const value) {
    return _http_service_email_bytes_address_ok(string_get_data(value), string_get_size(value));
}

static bool _http_service_email_string_header_safe(String const *const value) {
    return _http_service_email_bytes_header_safe(string_get_data(value), string_get_size(value));
}

static bool _http_service_email_bytes_ascii(char const *const data, USize const size) {
    trace_log_push(LOG_METADATA);

    bool ascii = true;

    for (USize i = 0; ascii && i < size; i += 1) {
        ascii = (U8) data[i] < 0x80;
    }

    trace_log_pop();

    return ascii;
}

/* A URL this module will dial: an smtp:// or smtps:// scheme, and no userinfo in
 * the authority. curl echoes the URL into CURLOPT_ERRORBUFFER, and consumers log
 * result.error - so "smtp://user:pass@host" puts the password in a log file. */
static bool _http_service_email_url_ok(char const *const data, USize const size) {
    trace_log_push(LOG_METADATA);

    USize authority = 0;

    if (size > CHAR_STATIC_SIZE("smtp://") && char_compare_equal_2((char*) data, CHAR_STATIC_SIZE("smtp://"), "smtp://", CHAR_STATIC_SIZE("smtp://"))) {
        authority = CHAR_STATIC_SIZE("smtp://");
    }
    else if (size > CHAR_STATIC_SIZE("smtps://") && char_compare_equal_2((char*) data, CHAR_STATIC_SIZE("smtps://"), "smtps://", CHAR_STATIC_SIZE("smtps://"))) {
        authority = CHAR_STATIC_SIZE("smtps://");
    }
    else {
        trace_log_pop();

        return false;
    }

    for (USize i = authority; i < size; i += 1) {
        if (data[i] == '/') {
            break;
        }

        if (data[i] == '@') {
            trace_log_pop();

            return false;
        }
    }

    bool const success = _http_service_email_bytes_header_safe(data, size);

    trace_log_pop();

    return success;
}

static long _http_service_email_security_to_curl(HTTP_Service_Email_Security const security) {
    trace_log_push(LOG_METADATA);

    long value = CURLUSESSL_ALL;

    switch (security) {
        case HTTP_SERVICE_EMAIL_SECURITY_NONE: {
            value = CURLUSESSL_NONE;

            break;
        }

        case HTTP_SERVICE_EMAIL_SECURITY_OPTIONAL: {
            value = CURLUSESSL_TRY;

            break;
        }

        case HTTP_SERVICE_EMAIL_SECURITY_REQUIRED: {
            value = CURLUSESSL_ALL;

            break;
        }
    }

    trace_log_pop();

    return value;
}

#ifdef ARENA_IMPLEMENTATION
static HTTP_Service_Email_Result _http_service_email_result_init(Arena *const allocator)
#else
static HTTP_Service_Email_Result _http_service_email_result_init(void)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    HTTP_Service_Email_Result const result = {
#ifdef ARENA_IMPLEMENTATION
        .allocator      = allocator,
#endif // ARENA_IMPLEMENTATION
        .curl_code      = 0,
#ifdef ARENA_IMPLEMENTATION
        .error          = _http_service_email_string_init(allocator),
        .response       = _http_service_email_string_init(allocator),
#else
        .error          = _http_service_email_string_init(),
        .response       = _http_service_email_string_init(),
#endif // ARENA_IMPLEMENTATION
        .response_code  = 0,
        .status         = HTTP_SERVICE_EMAIL_STATUS_OK,
        .success        = false
    };

    trace_log_pop();

    return result;
}

static USize _http_service_email_read_callback(char *const buffer, USize const size, USize const nmemb, void *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "buffer", (void*) buffer);
    error_check_null(LOG_METADATA, "data", (void*) data);

    HTTP_Service_Email_Upload   *const  upload          = (HTTP_Service_Email_Upload*) data;
    USize                       const   capacity        = size * nmemb;
    USize                       const   payload_size    = string_get_size(upload->payload);
    USize                       const   remaining       = payload_size > upload->offset ? payload_size - upload->offset : 0;
    USize                       const   byte_count      = remaining < capacity ? remaining : capacity;

    if (byte_count > 0) {
        memory_copy_2(buffer, capacity, string_get_data(upload->payload) + upload->offset, byte_count);

        upload->offset += byte_count;
    }

    trace_log_pop();

    return byte_count;
}

static USize _http_service_email_write_callback(char *const data, USize const size, USize const nmemb, void *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_null(LOG_METADATA, "self", (void*) self);

    String  *const  response    = (String*) self;
    USize   const   byte_count  = size * nmemb;

    if (byte_count > 0) {
        string_add_last_2(response, data, byte_count);
    }

    trace_log_pop();

    return byte_count;
}

/* RFC 2047 §4.1 B-encoding. UTF-8 is chunked on CHARACTER boundaries, never in
 * the middle of a multi-byte sequence, because a receiver decodes each word
 * independently and a split sequence renders as a replacement character. */
static bool _http_service_email_encoded_word_add(String *const payload, char const *const data, USize const size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "payload", (void*) payload);
    error_check_null(LOG_METADATA, "data", (void*) data);

    bool    added = true;
    USize   index = 0;

    while (added && index < size) {
        USize chunk = size - index < _HTTP_SERVICE_EMAIL_ENCODED_WORD_BYTE_COUNT
                    ? size - index
                    : _HTTP_SERVICE_EMAIL_ENCODED_WORD_BYTE_COUNT;

        while (chunk > 1 && index + chunk < size && ((U8) data[index + chunk] & 0xc0) == 0x80) {
            chunk -= 1;
        }

        char encoded[ENCODING_BASE64_ENCODE_SIZE(_HTTP_SERVICE_EMAIL_ENCODED_WORD_BYTE_COUNT) + CHAR_END_CHARACTER] = DEFAULT_INITIALIZATION;

        encoding_base64_encode_1((U8 const*) data + index, chunk, encoded);

        if (index > 0) {
            /* Every word after the first continues the header on a folded line:
             * two encoded words on one line would pass 78 columns, and RFC 2047
             * §5 requires whitespace between adjacent words anyway. */
            added = _http_service_email_string_add(payload, "\r\n ");
        }

        added = added
             && _http_service_email_string_add(payload, _HTTP_SERVICE_EMAIL_ENCODED_WORD_PREFIX)
             && _http_service_email_string_add(payload, encoded)
             && _http_service_email_string_add(payload, _HTTP_SERVICE_EMAIL_ENCODED_WORD_SUFFIX);

        index += chunk;
    }

    trace_log_pop();

    return added;
}

/* Answers whether a hard split of `data` at `split` would land right after a
 * backslash that escapes the byte at `split`: the run of consecutive backslashes
 * ending at `split` is odd, so the last one is the START of a quoted-pair, not
 * the escaped half of a `\\`. Unfolding keeps the fold's WSP, so a split there
 * would hand the receiver `\ ` - the backslash escaping the SPACE - and a `\"`
 * would then close the quoted-string early, with everything after it OUTSIDE the
 * quotes, where a `<evil@x>` in the name is parsed as the address. A `\\` split
 * the same way leaves the quote unterminated instead. */
static bool _http_service_email_escape_split_at(char const *const data, USize const split) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    USize run = 0;

    while (run < split && data[split - 1 - run] == '\\') {
        run += 1;
    }

    trace_log_pop();

    return run % 2 == 1;
}

/* Appends an ASCII header value, folding at spaces so no line passes 78 columns,
 * and HARD-SPLITTING a word too long to fit on a line of its own. RFC 5322 makes
 * a line over 998 octets a hard violation and many MTAs answer 5xx; folding at
 * spaces alone left a single unbroken token - a signed URL, a base64 tracking
 * id, a 1200-byte X- header - on one line well past that limit, because there
 * was no space to fold at.
 *
 * The two thresholds are different on purpose. A fold at a SPACE is lossless -
 * unfolding removes the CRLF and keeps the WSP, which is the space that was
 * there anyway - so it happens at the 78-column recommendation. A hard split has
 * nowhere to put that WSP: unfolding keeps it, so the receiver reads a space
 * INSIDE the token. It therefore happens only at
 * _HTTP_SERVICE_EMAIL_HEADER_LINE_HARD_LIMIT, the point past which the value
 * would be rejected outright - a 90-character List-Unsubscribe URL now goes out
 * intact on one 90-column line instead of arriving as a broken link. A token
 * past 998 is still split lossily, because there is no legal alternative. */
static bool _http_service_email_folded_add(String *const payload, char const *const data, USize const size, USize const start_column) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "payload", (void*) payload);
    error_check_null(LOG_METADATA, "data", (void*) data);

    bool    added   = true;
    USize   column  = start_column;
    USize   index   = 0;

    while (added && index < size) {
        USize word_end = index;

        while (word_end < size && data[word_end] != ' ') {
            word_end += 1;
        }

        USize offset    = index;
        USize remaining = word_end - index;

        if (index > 0) {
            if (column + 1 + remaining > HTTP_SERVICE_EMAIL_HEADER_LINE_MAX_LENGTH && column > 1) {
                added = _http_service_email_bytes_add(payload, "\r\n ", CHAR_STATIC_SIZE("\r\n "));

                column = 1;
            }
            else {
                added = _http_service_email_bytes_add(payload, " ", CHAR_STATIC_SIZE(" "));

                column += 1;
            }
        }

        while (added && remaining > 0) {
            /* Fold FIRST when the line is already full, so `room` below is always
             * at least one byte and the loop cannot spin without consuming. */
            if (column >= _HTTP_SERVICE_EMAIL_HEADER_LINE_HARD_LIMIT) {
                added = _http_service_email_bytes_add(payload, "\r\n ", CHAR_STATIC_SIZE("\r\n "));

                column = 1;
            }

            USize const room    = _HTTP_SERVICE_EMAIL_HEADER_LINE_HARD_LIMIT - column;
            USize       chunk   = remaining < room ? remaining : room;

            /* A hard split never separates a backslash from the byte it escapes:
             * step the break back one byte so the quoted-pair stays together. */
            if (chunk < remaining && _http_service_email_escape_split_at(data, offset + chunk)) {
                chunk -= 1;
            }

            if (chunk == 0) {
                /* The one byte that fit was the escaping backslash itself: fold
                 * now, and the next pass has 997 columns for the pair. */
                added = added && _http_service_email_bytes_add(payload, "\r\n ", CHAR_STATIC_SIZE("\r\n "));

                column = 1;
            }
            else {
                added = added && _http_service_email_bytes_add(payload, data + offset, chunk);

                column      += chunk;
                offset      += chunk;
                remaining   -= chunk;
            }
        }

        index = word_end + 1;
    }

    trace_log_pop();

    return added;
}

static bool _http_service_email_header_value_add(String *const payload, char const *const name, String const *const value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "payload", (void*) payload);
    error_check_null(LOG_METADATA, "name", (void*) name);
    error_check_null(LOG_METADATA, "value", (void*) value);

    USize   const   name_size   = char_length(name);
    char    const   *const data = string_get_data(value);
    USize   const   size        = string_get_size(value);

    bool added = _http_service_email_string_add(payload, name) && _http_service_email_string_add(payload, ": ");

    if (added && _http_service_email_bytes_ascii(data, size)) {
        added = _http_service_email_folded_add(payload, data, size, name_size + CHAR_STATIC_SIZE(": "));
    }
    else if (added) {
        added = _http_service_email_encoded_word_add(payload, data, size);
    }

    added = added && _http_service_email_string_add(payload, "\r\n");

    trace_log_pop();

    return added;
}

static bool _http_service_email_display_name_quoting_needed(char const *const data, USize const size) {
    trace_log_push(LOG_METADATA);

    bool needed = false;

    for (USize i = 0; !needed && i < size; i += 1) {
        char const c = data[i];

        needed = c == '(' || c == ')' || c == '<' || c == '>' || c == '@' || c == ',' || c == ';'
              || c == ':' || c == '\\' || c == '"' || c == '.' || c == '[' || c == ']';
    }

    trace_log_pop();

    return needed;
}

/* "Name" <addr>, folded like every other header value. An unquoted display name
 * carrying a ',' or a '.' makes a receiver parse two addresses (or a malformed
 * one) out of a single sender; a non-ASCII one is illegal in a header without
 * SMTPUTF8 and renders as mojibake on a strict receiver, so it goes out as an
 * RFC 2047 encoded word instead.
 *
 * The ASCII paths render the whole "name <addr>" into a scratch String and hand
 * it to _folded_add. from_name_set caps nothing, so a 1000-byte display name was
 * the one value that reached the wire as a line past the 998-octet hard limit
 * the rest of the renderer guarantees. A fold at a space INSIDE a quoted-string
 * is legal FWS (RFC 5322 §3.2.4), and the address token itself is never split:
 * _bytes_address_ok caps it well under the limit. The encoded-word path needs no
 * scratch: each word is already on a folded line of its own, and the address
 * that follows it cannot reach 998 either. */
static bool _http_service_email_address_payload_add(String *const payload, String const *const name, String const *const email, USize const start_column) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "payload", (void*) payload);
    error_check_null(LOG_METADATA, "name", (void*) name);
    error_check_null(LOG_METADATA, "email", (void*) email);

    if (string_empty(name)) {
        bool const bare = _http_service_email_string_add_2(payload, email);

        trace_log_pop();

        return bare;
    }

    char    const   *const  data    = string_get_data(name);
    USize   const           size    = string_get_size(name);

    if (!_http_service_email_bytes_ascii(data, size)) {
        bool const encoded = _http_service_email_encoded_word_add(payload, data, size) &&
                             _http_service_email_string_add(payload, " <")           &&
                             _http_service_email_string_add_2(payload, email)        &&
                             _http_service_email_string_add(payload, ">");

        trace_log_pop();

        return encoded;
    }

    /* Every name byte may need an escape, plus the quotes, the " <", the ">" and the address. */
    USize const reserve = 2 * size + CHAR_STATIC_SIZE("\"\" <>") + string_get_size(email) + CHAR_END_CHARACTER;

#ifdef ARENA_IMPLEMENTATION
    String value = _http_service_email_string_init_sized(reserve, payload->allocator);
#else
    String value = _http_service_email_string_init_sized(reserve);
#endif // ARENA_IMPLEMENTATION

    bool added = true;

    if (_http_service_email_display_name_quoting_needed(data, size)) {
        added = _http_service_email_string_add(&value, "\"");

        for (USize i = 0; added && i < size; i += 1) {
            if (data[i] == '"' || data[i] == '\\') {
                added = _http_service_email_bytes_add(&value, "\\", CHAR_STATIC_SIZE("\\"));
            }

            added = added && _http_service_email_bytes_add(&value, data + i, 1);
        }

        added = added && _http_service_email_string_add(&value, "\"");
    }
    else {
        added = _http_service_email_string_add_2(&value, name);
    }

    /* `value` is non-empty whenever `added` still holds, so _folded_add never
     * sees the NULL data pointer an empty String carries. */
    added = added                                                                                                 &&
            _http_service_email_string_add(&value, " <")                                                          &&
            _http_service_email_string_add_2(&value, email)                                                       &&
            _http_service_email_string_add(&value, ">")                                                           &&
            _http_service_email_folded_add(payload, string_get_data(&value), string_get_size(&value), start_column);

    string_uninit(&value);

    trace_log_pop();

    return added;
}

/* Writes "<address>" into a caller buffer and answers false when it could not,
 * which is the caller's failure signal. `data_size` is message-derived - a
 * recipient address out of a DB cell, the configured sender - so a zero is DATA,
 * not a programming error: it used to reach error_check_non_value_uint and abort
 * the process on a blank users.email cell, the same shape _list_add_1 already
 * refuses as a value.
 *
 * A buffer rather than a String because this was the module's only unconditional
 * heap allocation: an arena-backed service still malloc'd twice per recipient
 * per send for a value that never outlives the call and, thanks to the address
 * caps, has a known upper bound. */
static bool _http_service_email_envelope_write(char *const out, USize const out_size, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "out", (void*) out);
    error_check_null(LOG_METADATA, "data", (void*) data);

    if (data_size == 0 || data_size + CHAR_STATIC_SIZE("<>") + CHAR_END_CHARACTER > out_size) {
        trace_log_pop();

        return false;
    }

    out[0] = '<';

    memory_copy_2(out + 1, out_size - 1, (void*) data, data_size);

    out[1 + data_size] = '>';
    out[2 + data_size] = '\0';

    trace_log_pop();

    return true;
}

/* Bytes a list contributes to the RENDERED payload: every entry plus the ", " or
 * CRLF-fold between them. The payload reserve used to count only the two bodies,
 * so a thirty-address To: (~900 bytes) or a few custom headers regrew the String
 * at least once, against a Performance section that says the common case never
 * does. Bcc is deliberately not counted here - it is envelope-only. */
static USize _http_service_email_list_byte_count(AL_Str const *const list) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "list", (void*) list);

    USize total = 0;

    for (USize i = 0; i < al_str_get_size(list); i += 1) {
        total += str_get_size(al_str_at(list, i)) + CHAR_STATIC_SIZE(", ");
    }

    trace_log_pop();

    return total;
}

static bool _http_service_email_list_payload_add(String *const payload, char const *const name, AL_Str const *const list) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "payload", (void*) payload);
    error_check_null(LOG_METADATA, "name", (void*) name);
    error_check_null(LOG_METADATA, "list", (void*) list);

    if (al_str_get_size(list) == 0) {
        trace_log_pop();

        return true;
    }

    USize column = char_length(name);

    bool added = _http_service_email_string_add(payload, name);

    for (USize i = 0; added && i < al_str_get_size(list); i += 1) {
        Str     const   *const  entry       = al_str_at(list, i);
        USize   const           entry_size  = str_get_size(entry);

        if (i > 0) {
            /* Fold BETWEEN recipients: a thirty-address To: is otherwise one
             * line well past the 998-octet limit. */
            if (column + CHAR_STATIC_SIZE(", ") + entry_size > HTTP_SERVICE_EMAIL_HEADER_LINE_MAX_LENGTH) {
                added = _http_service_email_string_add(payload, ",\r\n ");

                column = 1;
            }
            else {
                added = _http_service_email_string_add(payload, ", ");

                column += CHAR_STATIC_SIZE(", ");
            }
        }

        added = added && _http_service_email_str_add(payload, entry);

        column += entry_size;
    }

    added = added && _http_service_email_string_add(payload, "\r\n");

    trace_log_pop();

    return added;
}

/* Caller-supplied header lines are folded like the module's own values: a single
 * 1200-byte X- header is otherwise one line past RFC 5322's 998-octet hard limit
 * even though header_add accepted every byte in it as header-safe. */
static bool _http_service_email_custom_headers_add(String *const payload, AL_Str const *const list) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "payload", (void*) payload);
    error_check_null(LOG_METADATA, "list", (void*) list);

    bool added = true;

    for (USize i = 0; added && i < al_str_get_size(list); i += 1) {
        Str const *const entry = al_str_at(list, i);

        added = _http_service_email_folded_add(payload, str_get_data(entry), str_get_size(entry), 0)
             && _http_service_email_string_add(payload, "\r\n");
    }

    trace_log_pop();

    return added;
}

static bool _http_service_email_part_add(String *const payload, char const *const content_type, String const *const body) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "payload", (void*) payload);
    error_check_null(LOG_METADATA, "content_type", (void*) content_type);
    error_check_null(LOG_METADATA, "body", (void*) body);

    bool const added = _http_service_email_string_add(payload, "Content-Type: ")
                    && _http_service_email_string_add(payload, content_type)
                    && _http_service_email_string_add(payload, "; charset=utf-8\r\n")
                    && _http_service_email_string_add(payload, "Content-Transfer-Encoding: quoted-printable\r\n\r\n")
                    && encoding_quoted_printable_encode_2(string_get_data(body), string_get_size(body), payload)
                    && _http_service_email_string_add(payload, "\r\n");

    trace_log_pop();

    return added;
}

static bool _http_service_email_boundary_add(String *const payload, char const *const boundary, char const *const suffix) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "payload", (void*) payload);
    error_check_null(LOG_METADATA, "boundary", (void*) boundary);
    error_check_null(LOG_METADATA, "suffix", (void*) suffix);

    bool const added = _http_service_email_string_add(payload, "--")
                    && _http_service_email_string_add(payload, boundary)
                    && _http_service_email_string_add(payload, suffix);

    trace_log_pop();

    return added;
}

static bool _http_service_email_body_add(String *const payload, HTTP_Service_Email_Message const *const message, char const *const boundary) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "payload", (void*) payload);
    error_check_null(LOG_METADATA, "message", (void*) message);
    error_check_null(LOG_METADATA, "boundary", (void*) boundary);

    bool const has_text = !string_empty(&message->text);
    bool const has_html = !string_empty(&message->html);

    if (has_text && has_html) {
        bool const added = _http_service_email_string_add(payload, "Content-Type: multipart/alternative; boundary=\"")
                        && _http_service_email_string_add(payload, boundary)
                        && _http_service_email_string_add(payload, "\"\r\n\r\n")
                        && _http_service_email_boundary_add(payload, boundary, "\r\n")
                        && _http_service_email_part_add(payload, "text/plain", &message->text)
                        && _http_service_email_boundary_add(payload, boundary, "\r\n")
                        && _http_service_email_part_add(payload, "text/html", &message->html)
                        && _http_service_email_boundary_add(payload, boundary, "--\r\n");

        trace_log_pop();

        return added;
    }

    bool const added = _http_service_email_part_add(payload, has_html ? "text/html" : "text/plain", has_html ? &message->html : &message->text);

    trace_log_pop();

    return added;
}

/* curl_slist_append answers NULL on its own allocation failure and does NOT free
 * the list it was handed, so assigning its answer straight back into the
 * accumulator both LEAKED every node already built and left `recipients` null -
 * and the send then went out to whichever recipients curl had accepted so far,
 * or to none at all, while still reporting success. Appending into a temp and
 * failing the whole send is the only shape that cannot deliver a message to a
 * different set of people than the caller addressed. */
static bool _http_service_email_curl_recipients_add(struct curl_slist **const recipients, AL_Str const *const list) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "recipients", (void*) recipients);
    error_check_null(LOG_METADATA, "list", (void*) list);

    bool added = true;

    for (USize i = 0; added && i < al_str_get_size(list); i += 1) {
        Str     const   *const  recipient                                       = al_str_at(list, i);
        char                    envelope[_HTTP_SERVICE_EMAIL_ENVELOPE_MAX_SIZE] = DEFAULT_INITIALIZATION;

        if (!_http_service_email_envelope_write(envelope, sizeof(envelope), str_get_data(recipient), str_get_size(recipient))) {
            added = false;
        }
        else {
            struct curl_slist *const grown = curl_slist_append(*recipients, envelope);

            added = grown != nullptr;

            if (added) {
                *recipients = grown;
            }
        }
    }

    trace_log_pop();

    return added;
}

static bool _http_service_email_curl_recipients_create(HTTP_Service_Email_Message const *const message, struct curl_slist **const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "message", (void*) message);
    error_check_null(LOG_METADATA, "out", (void*) out);

    struct curl_slist *recipients = nullptr;

    bool const added = _http_service_email_curl_recipients_add(&recipients, &message->to)
                    && _http_service_email_curl_recipients_add(&recipients, &message->cc)
                    && _http_service_email_curl_recipients_add(&recipients, &message->bcc);

    if (!added) {
        curl_slist_free_all(recipients);

        trace_log_pop();

        return false;
    }

    *out = recipients;

    trace_log_pop();

    return true;
}

static bool _http_service_email_capture_list_add(String *const sink, AL_Str const *const list) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "sink", (void*) sink);
    error_check_null(LOG_METADATA, "list", (void*) list);

    bool added = true;

    for (USize i = 0; added && i < al_str_get_size(list); i += 1) {
        added = _http_service_email_string_add(sink, "RCPT TO:<")
             && _http_service_email_str_add(sink, al_str_at(list, i))
             && _http_service_email_string_add(sink, ">\r\n");
    }

    trace_log_pop();

    return added;
}

/* SMTP transparency, RFC 5321 §4.5.2: a DATA line whose first byte is '.' is
 * sent with a second '.' in front, because an unstuffed "." line ENDS the DATA
 * block early at the relay and everything after it is read as SMTP commands.
 * libcurl does this itself on the upload path, so the SMTP transport was always
 * correct - but the capture transport appended the payload verbatim, and
 * email.h promises the capture shows exactly what would go on the wire. A golden
 * built from an unstuffed capture pinned bytes SMTP never sends, and a preview
 * of a caller-authored body (a CRM reminder note beginning with an ellipsis) hid
 * a truncation that is real. The capture writer stuffs, so the promise holds. */
static bool _http_service_email_dot_stuffed_add(String *const sink, String const *const payload) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "sink", (void*) sink);
    error_check_null(LOG_METADATA, "payload", (void*) payload);

    /* An empty payload has a NULL data pointer, which the byte appender reads as
     * a contract violation rather than as nothing to copy. Appending nothing
     * SUCCEEDED. */
    if (string_get_size(payload) == 0) {
        trace_log_pop();

        return true;
    }

    char    const   *const  data            = string_get_data(payload);
    USize   const           size            = string_get_size(payload);
    bool                    added           = true;
    bool                    at_line_start   = true;
    USize                   start           = 0;

    for (USize i = 0; added && i < size; i += 1) {
        if (at_line_start && data[i] == '.') {
            /* Everything up to the offending byte, then the extra '.'. `start`
             * stays put so the byte itself goes out with the next run. */
            added = _http_service_email_bytes_add(sink, data + start, i - start)
                 && _http_service_email_bytes_add(sink, ".", CHAR_STATIC_SIZE("."));

            start = i;
        }

        at_line_start = data[i] == '\n' && i > 0 && data[i - 1] == '\r';
    }

    added = added && _http_service_email_bytes_add(sink, data + start, size - start);

    trace_log_pop();

    return added;
}

/* A render context is CALLER-supplied - a golden test's fixed value, a payload
 * replayed from a log - so its two char arrays are DATA, not module output. An
 * array with no terminator inside its capacity runs char_length off the end of
 * the struct; a CR or LF in the Message-ID injects a header into the message the
 * whole header-safety surface exists to prevent; a '"' or a space in the
 * boundary escapes the quoted Content-Type parameter it is rendered into and
 * re-splits the MIME structure.
 *
 * The Message-ID's SHAPE is checked for the same reason: email.h documents the
 * field as the value INCLUDING its angle brackets and the renderer emits it
 * verbatim after "Message-ID: ", so a context replayed from a log - or a golden
 * hand-written as "abc@host" - would otherwise render a malformed header. An
 * interior '<', '>' or space is refused too: RFC 5322 §3.6.4's msg-id holds
 * exactly one bracketed, space-free id, and a second pair is what a threading
 * forgery looks like. */
static bool _http_service_email_render_valid(HTTP_Service_Email_Render const *const render) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "render", (void*) render);

    USize boundary_size     = 0;
    USize message_id_size   = 0;

    while (boundary_size < sizeof(render->boundary) && render->boundary[boundary_size] != '\0') {
        boundary_size += 1;
    }

    while (message_id_size < sizeof(render->message_id) && render->message_id[message_id_size] != '\0') {
        message_id_size += 1;
    }

    bool success = boundary_size > 0 && boundary_size < sizeof(render->boundary)
                && message_id_size > 0 && message_id_size < sizeof(render->message_id)
                && _http_service_email_bytes_header_safe(render->boundary, boundary_size)
                && _http_service_email_bytes_header_safe(render->message_id, message_id_size);

    for (USize i = 0; success && i < boundary_size; i += 1) {
        char const c = render->boundary[i];

        success = c != '"' && c != ' ';
    }

    success = success
           && render->message_id[0] == '<'
           && render->message_id[message_id_size - 1] == '>';

    for (USize i = 1; success && i + 1 < message_id_size; i += 1) {
        char const c = render->message_id[i];

        success = c != '<' && c != '>' && c != ' ';
    }

    trace_log_pop();

    return success;
}

/*==============================================================================
 * MARK: - API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
bool http_service_email_alloc_init_1(HTTP_Service_Email *const self, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    HTTP_Service_Email const email = {
        .allocator          = allocator,
        .auth_mechanism     = _http_service_email_char_to_string("", allocator),
        .ca_bundle          = _http_service_email_char_to_string("", allocator),
        .connect_timeout_ms = HTTP_SERVICE_EMAIL_DEFAULT_CONNECT_TIMEOUT_MS,
        .from               = _http_service_email_char_to_string("", allocator),
        .from_name          = _http_service_email_char_to_string("", allocator),
        .password           = _http_service_email_char_to_string("", allocator),
        .security           = HTTP_SERVICE_EMAIL_SECURITY_REQUIRED,
        .timeout_ms         = HTTP_SERVICE_EMAIL_DEFAULT_TIMEOUT_MS,
        .transport_sink     = nullptr,
        .url                = _http_service_email_char_to_string("", allocator),
        .username           = _http_service_email_char_to_string("", allocator),
        .verify_tls         = true
    };

    *self = email;

    trace_log_pop();

    return true;
}

bool http_service_email_alloc_init_2(HTTP_Service_Email *const self,
    char const *const url, char const *const username, char const *const password, char const *const from, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "url", (void*) url);
    error_check_null(LOG_METADATA, "username", (void*) username);
    error_check_null(LOG_METADATA, "password", (void*) password);
    error_check_null(LOG_METADATA, "from", (void*) from);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    if (!_http_service_email_url_ok(url, char_length(url)) || !http_service_email_address_valid(from)) {
        trace_log_pop();

        return false;
    }

    if (!http_service_email_alloc_init_1(self, allocator)) {
        trace_log_pop();

        return false;
    }

    /* Whole-or-nothing: a refused copy releases everything the constructor built
     * and leaves the caller with an untouched `self`, so a refusal can never be
     * mistaken for "configured but empty". */
    if (!_http_service_email_string_set(&self->url, url, allocator)
        || !_http_service_email_string_set(&self->from, from, allocator)
        || !_http_service_email_string_set(&self->username, username, allocator)
        || !_http_service_email_string_set(&self->password, password, allocator)) {
        http_service_email_uninit(self);

        trace_log_pop();

        return false;
    }

    trace_log_pop();

    return true;
}

bool http_service_email_message_alloc_init_1(HTTP_Service_Email_Message *const self, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    HTTP_Service_Email_Message const message = {
        .allocator  = allocator,
        .bcc        = al_str_alloc_init_1(allocator),
        .cc         = al_str_alloc_init_1(allocator),
        .from       = _http_service_email_char_to_string("", allocator),
        .from_name  = _http_service_email_char_to_string("", allocator),
        .headers    = al_str_alloc_init_1(allocator),
        .html       = _http_service_email_char_to_string("", allocator),
        .reply_to   = _http_service_email_char_to_string("", allocator),
        .subject    = _http_service_email_char_to_string("", allocator),
        .text       = _http_service_email_char_to_string("", allocator),
        .to         = al_str_alloc_init_1(allocator)
    };

    *self = message;

    trace_log_pop();

    return true;
}
#endif // ARENA_IMPLEMENTATION

bool http_service_email_address_valid(char const *const email) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "email", (void*) email);

    bool const success = _http_service_email_bytes_address_ok(email, char_length(email));

    trace_log_pop();

    return success;
}

bool http_service_email_auth_mechanism_set(HTTP_Service_Email *const self, char const *const mechanism) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "mechanism", (void*) mechanism);

    if (!_http_service_email_bytes_header_safe(mechanism, char_length(mechanism))) {
        trace_log_pop();

        return false;
    }

#ifdef ARENA_IMPLEMENTATION
    bool const stored = _http_service_email_string_set(&self->auth_mechanism, mechanism, self->allocator);
#else
    bool const stored = _http_service_email_string_set(&self->auth_mechanism, mechanism);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return stored;
}

bool http_service_email_auth_set(HTTP_Service_Email *const self, char const *const username, char const *const password) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "username", (void*) username);
    error_check_null(LOG_METADATA, "password", (void*) password);

#ifdef ARENA_IMPLEMENTATION
    bool const stored = _http_service_email_string_set(&self->username, username, self->allocator) && _http_service_email_string_set(&self->password, password, self->allocator);
#else
    bool const stored = _http_service_email_string_set(&self->username, username) && _http_service_email_string_set(&self->password, password);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return stored;
}

bool http_service_email_ca_bundle_set(HTTP_Service_Email *const self, char const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "path", (void*) path);

    /* The value reaches CURLOPT_CAINFO, so it is refused like every sibling
     * setter's: a control byte in a configured path is a mangled environment
     * variable, not a store curl can open. */
    if (!_http_service_email_bytes_header_safe(path, char_length(path))) {
        trace_log_pop();

        return false;
    }

#ifdef ARENA_IMPLEMENTATION
    bool const stored = _http_service_email_string_set(&self->ca_bundle, path, self->allocator);
#else
    bool const stored = _http_service_email_string_set(&self->ca_bundle, path);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return stored;
}

bool http_service_email_from_name_set(HTTP_Service_Email *const self, char const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    if (!_http_service_email_bytes_header_safe(name, char_length(name))) {
        trace_log_pop();

        return false;
    }

#ifdef ARENA_IMPLEMENTATION
    bool const stored = _http_service_email_string_set(&self->from_name, name, self->allocator);
#else
    bool const stored = _http_service_email_string_set(&self->from_name, name);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return stored;
}

bool http_service_email_from_set(HTTP_Service_Email *const self, char const *const email) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "email", (void*) email);

    if (!http_service_email_address_valid(email)) {
        trace_log_pop();

        return false;
    }

#ifdef ARENA_IMPLEMENTATION
    bool const stored = _http_service_email_string_set(&self->from, email, self->allocator);
#else
    bool const stored = _http_service_email_string_set(&self->from, email);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return stored;
}

void http_service_email_header_sanitize(char *const value) {
    trace_log_push(LOG_METADATA);

    if (value != nullptr) {
        for (char *cursor = value; *cursor != '\0'; cursor += 1) {
            U8 const c = (U8) *cursor;

            if (c < 0x20 || c == 0x7f) {
                *cursor = ' ';
            }
        }
    }

    trace_log_pop();
}

bool http_service_email_init_1(HTTP_Service_Email *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    HTTP_Service_Email const email = {
#ifdef ARENA_IMPLEMENTATION
        .allocator          = nullptr,
        .auth_mechanism     = _http_service_email_char_to_string("", nullptr),
        .ca_bundle          = _http_service_email_char_to_string("", nullptr),
#else
        .auth_mechanism     = _http_service_email_char_to_string(""),
        .ca_bundle          = _http_service_email_char_to_string(""),
#endif // ARENA_IMPLEMENTATION
        .connect_timeout_ms = HTTP_SERVICE_EMAIL_DEFAULT_CONNECT_TIMEOUT_MS,
#ifdef ARENA_IMPLEMENTATION
        .from               = _http_service_email_char_to_string("", nullptr),
        .from_name          = _http_service_email_char_to_string("", nullptr),
        .password           = _http_service_email_char_to_string("", nullptr),
#else
        .from               = _http_service_email_char_to_string(""),
        .from_name          = _http_service_email_char_to_string(""),
        .password           = _http_service_email_char_to_string(""),
#endif // ARENA_IMPLEMENTATION
        .security           = HTTP_SERVICE_EMAIL_SECURITY_REQUIRED,
        .timeout_ms         = HTTP_SERVICE_EMAIL_DEFAULT_TIMEOUT_MS,
        .transport_sink     = nullptr,
#ifdef ARENA_IMPLEMENTATION
        .url                = _http_service_email_char_to_string("", nullptr),
        .username           = _http_service_email_char_to_string("", nullptr),
#else
        .url                = _http_service_email_char_to_string(""),
        .username           = _http_service_email_char_to_string(""),
#endif // ARENA_IMPLEMENTATION
        .verify_tls         = true
    };

    *self = email;

    trace_log_pop();

    return true;
}

bool http_service_email_init_2(HTTP_Service_Email *const self, char const *const url, char const *const username, char const *const password, char const *const from) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "url", (void*) url);
    error_check_null(LOG_METADATA, "username", (void*) username);
    error_check_null(LOG_METADATA, "password", (void*) password);
    error_check_null(LOG_METADATA, "from", (void*) from);

    if (!_http_service_email_url_ok(url, char_length(url)) || !http_service_email_address_valid(from)) {
        trace_log_pop();

        return false;
    }

    if (!http_service_email_init_1(self)) {
        trace_log_pop();

        return false;
    }

#ifdef ARENA_IMPLEMENTATION
    bool const stored = _http_service_email_string_set(&self->url, url, nullptr)
                     && _http_service_email_string_set(&self->from, from, nullptr)
                     && _http_service_email_string_set(&self->username, username, nullptr)
                     && _http_service_email_string_set(&self->password, password, nullptr);
#else
    bool const stored = _http_service_email_string_set(&self->url, url)
                     && _http_service_email_string_set(&self->from, from)
                     && _http_service_email_string_set(&self->username, username)
                     && _http_service_email_string_set(&self->password, password);
#endif // ARENA_IMPLEMENTATION

    if (!stored) {
        http_service_email_uninit(self);

        trace_log_pop();

        return false;
    }

    trace_log_pop();

    return true;
}

bool http_service_email_init_from_env(HTTP_Service_Email *const self, char const *const prefix) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "prefix", (void*) prefix);

    HTTP_Service_Email_Status status = HTTP_SERVICE_EMAIL_STATUS_OK;

    bool const configured = http_service_email_init_from_env_2(self, prefix, &status);

    trace_log_pop();

    return configured;
}

bool http_service_email_init_from_env_2(HTTP_Service_Email *const self, char const *const prefix, HTTP_Service_Email_Status *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "prefix", (void*) prefix);
    error_check_null(LOG_METADATA, "out", (void*) out);

    USize   const   prefix_size                 = char_length(prefix);
    char            name[128]                   = DEFAULT_INITIALIZATION;
    char    const   *const  suffixes[]          = { "_URL", "_USER", "_PASSWORD", "_FROM", "_VERIFY_TLS", "_STARTTLS", "_FROM_NAME" };
    char    const   *values[sizeof(suffixes) / sizeof(suffixes[0])] = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i += 1) {
        USize const suffix_size = char_length(suffixes[i]);

        /* A prefix that cannot hold its longest suffix is a configuration
         * mistake, not a crash: fall through to the unconfigured service. */
        if (prefix_size + suffix_size + CHAR_END_CHARACTER > sizeof(name)) {
            values[i] = "";

            continue;
        }

        memory_copy_2(name, sizeof(name), (void*) prefix, prefix_size);
        memory_copy_2(name + prefix_size, sizeof(name) - prefix_size, (void*) suffixes[i], suffix_size);

        name[prefix_size + suffix_size] = '\0';

        values[i] = env_get_2(name, "");
    }

    bool const configured = !memory_empty(values[0]) && values[0][0] != '\0' && !memory_empty(values[3]) && values[3][0] != '\0';

    /* Not configured is not an error: the caller gets a usable, UNCONFIGURED
     * service it can still uninit, and NOT_CONFIGURED says only "the environment
     * did not describe a relay". */
    if (!configured) {
        *out = HTTP_SERVICE_EMAIL_STATUS_NOT_CONFIGURED;

        http_service_email_init_1(self);

        trace_log_pop();

        return false;
    }

    /* The environment DID describe a relay and the module refused it - one
     * missing slash in TRAYMON_SMTP_URL, userinfo in the authority, a sender
     * that is not an address. This used to answer exactly like an unset
     * variable, so a server booted, looked healthy and silently never mailed. */
    if (!http_service_email_init_2(self, values[0], values[1], values[2], values[3])) {
        *out = HTTP_SERVICE_EMAIL_STATUS_INVALID_CONFIGURATION;

        http_service_email_init_1(self);

        trace_log_pop();

        return false;
    }

    /* A display name the module refuses - a control byte in it - is the same
     * class as a sender that is not an address: the environment named it, and
     * the message it would head cannot be sent. The configured service is torn
     * down again so the caller gets the same unconfigured default as above. */
    if (!memory_empty(values[6]) && values[6][0] != '\0' && !http_service_email_from_name_set(self, values[6])) {
        *out = HTTP_SERVICE_EMAIL_STATUS_INVALID_CONFIGURATION;

        http_service_email_uninit(self);
        http_service_email_init_1(self);

        trace_log_pop();

        return false;
    }

    /* Exactly "0" turns verification off - the same parsing the three consumers
     * hand-rolled. Anything else leaves it on, so a typo fails SAFE. */
    if (!memory_empty(values[4]) && char_compare_equal_2((char*) values[4], char_length(values[4]), "0", CHAR_STATIC_SIZE("0"))) {
        http_service_email_verify_tls_set(self, false);
    }

    /* Exactly "none", "optional" or "required". Empty keeps the REQUIRED default
     * and so does any other spelling - a typo must never downgrade a relay to
     * plaintext - but that one is LOGGED, because the deployment asked for a
     * mode and silently got the strictest one instead. */
    if (!memory_empty(values[5]) && values[5][0] != '\0') {
        USize const value_size = char_length(values[5]);

        if (char_compare_equal_2((char*) values[5], value_size, "none", CHAR_STATIC_SIZE("none"))) {
            http_service_email_security_set(self, HTTP_SERVICE_EMAIL_SECURITY_NONE);
        }
        else if (char_compare_equal_2((char*) values[5], value_size, "optional", CHAR_STATIC_SIZE("optional"))) {
            http_service_email_security_set(self, HTTP_SERVICE_EMAIL_SECURITY_OPTIONAL);
        }
        else if (!char_compare_equal_2((char*) values[5], value_size, "required", CHAR_STATIC_SIZE("required"))) {
            log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_email: %s_STARTTLS=\"%s\" is not none, optional or required; STARTTLS stays required", prefix, values[5]);
        }
    }

    *out = HTTP_SERVICE_EMAIL_STATUS_OK;

    trace_log_pop();

    return true;
}

bool http_service_email_message_bcc_add_1(HTTP_Service_Email_Message *const self, char const *const email) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "email", (void*) email);

    bool const stored = http_service_email_address_valid(email) && _http_service_email_list_add_1(&self->bcc, email, char_length(email));

    trace_log_pop();

    return stored;
}

bool http_service_email_message_bcc_add_2(HTTP_Service_Email_Message *const self, String const *const email) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "email", (void*) email);

    bool const stored = _http_service_email_string_address_ok(email) && _http_service_email_list_add_1(&self->bcc, string_get_data(email), string_get_size(email));

    trace_log_pop();

    return stored;
}

bool http_service_email_message_cc_add_1(HTTP_Service_Email_Message *const self, char const *const email) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "email", (void*) email);

    bool const stored = http_service_email_address_valid(email) && _http_service_email_list_add_1(&self->cc, email, char_length(email));

    trace_log_pop();

    return stored;
}

bool http_service_email_message_cc_add_2(HTTP_Service_Email_Message *const self, String const *const email) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "email", (void*) email);

    bool const stored = _http_service_email_string_address_ok(email) && _http_service_email_list_add_1(&self->cc, string_get_data(email), string_get_size(email));

    trace_log_pop();

    return stored;
}

bool http_service_email_message_from_name_set(HTTP_Service_Email_Message *const self, char const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    if (!_http_service_email_bytes_header_safe(name, char_length(name))) {
        trace_log_pop();

        return false;
    }

#ifdef ARENA_IMPLEMENTATION
    bool const stored = _http_service_email_string_set(&self->from_name, name, self->allocator);
#else
    bool const stored = _http_service_email_string_set(&self->from_name, name);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return stored;
}

bool http_service_email_message_from_set(HTTP_Service_Email_Message *const self, char const *const email) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "email", (void*) email);

    if (email[0] != '\0' && !http_service_email_address_valid(email)) {
        trace_log_pop();

        return false;
    }

#ifdef ARENA_IMPLEMENTATION
    bool const stored = _http_service_email_string_set(&self->from, email, self->allocator);
#else
    bool const stored = _http_service_email_string_set(&self->from, email);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return stored;
}

bool http_service_email_message_header_add(HTTP_Service_Email_Message *const self, char const *const header) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "header", (void*) header);

    USize const header_size = char_length(header);

    if (!_http_service_email_header_line_ok(header, header_size)) {
        trace_log_pop();

        return false;
    }

    bool const stored = _http_service_email_list_add_1(&self->headers, header, header_size);

    trace_log_pop();

    return stored;
}

bool http_service_email_message_html_set(HTTP_Service_Email_Message *const self, char const *const html) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "html", (void*) html);

#ifdef ARENA_IMPLEMENTATION
    bool const stored = _http_service_email_crlf_set(&self->html, html, self->allocator);
#else
    bool const stored = _http_service_email_crlf_set(&self->html, html);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return stored;
}

bool http_service_email_message_init_1(HTTP_Service_Email_Message *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    HTTP_Service_Email_Message const message = {
#ifdef ARENA_IMPLEMENTATION
        .allocator  = nullptr,
#endif // ARENA_IMPLEMENTATION
        .bcc        = al_str_init_1(),
        .cc         = al_str_init_1(),
#ifdef ARENA_IMPLEMENTATION
        .from       = _http_service_email_char_to_string("", nullptr),
        .from_name  = _http_service_email_char_to_string("", nullptr),
#else
        .from       = _http_service_email_char_to_string(""),
        .from_name  = _http_service_email_char_to_string(""),
#endif // ARENA_IMPLEMENTATION
        .headers    = al_str_init_1(),
#ifdef ARENA_IMPLEMENTATION
        .html       = _http_service_email_char_to_string("", nullptr),
        .reply_to   = _http_service_email_char_to_string("", nullptr),
        .subject    = _http_service_email_char_to_string("", nullptr),
        .text       = _http_service_email_char_to_string("", nullptr),
#else
        .html       = _http_service_email_char_to_string(""),
        .reply_to   = _http_service_email_char_to_string(""),
        .subject    = _http_service_email_char_to_string(""),
        .text       = _http_service_email_char_to_string(""),
#endif // ARENA_IMPLEMENTATION
        .to         = al_str_init_1()
    };

    *self = message;

    trace_log_pop();

    return true;
}

bool http_service_email_message_reply_to_set(HTTP_Service_Email_Message *const self, char const *const email) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "email", (void*) email);

    if (email[0] != '\0' && !http_service_email_address_valid(email)) {
        trace_log_pop();

        return false;
    }

#ifdef ARENA_IMPLEMENTATION
    bool const stored = _http_service_email_string_set(&self->reply_to, email, self->allocator);
#else
    bool const stored = _http_service_email_string_set(&self->reply_to, email);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return stored;
}

bool http_service_email_message_subject_set(HTTP_Service_Email_Message *const self, char const *const subject) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "subject", (void*) subject);

    if (!_http_service_email_bytes_header_safe(subject, char_length(subject))) {
        trace_log_pop();

        return false;
    }

#ifdef ARENA_IMPLEMENTATION
    bool const stored = _http_service_email_string_set(&self->subject, subject, self->allocator);
#else
    bool const stored = _http_service_email_string_set(&self->subject, subject);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return stored;
}

bool http_service_email_message_text_set(HTTP_Service_Email_Message *const self, char const *const text) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "text", (void*) text);

#ifdef ARENA_IMPLEMENTATION
    bool const stored = _http_service_email_crlf_set(&self->text, text, self->allocator);
#else
    bool const stored = _http_service_email_crlf_set(&self->text, text);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return stored;
}

bool http_service_email_message_to_add_1(HTTP_Service_Email_Message *const self, char const *const email) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "email", (void*) email);

    bool const stored = http_service_email_address_valid(email) && _http_service_email_list_add_1(&self->to, email, char_length(email));

    trace_log_pop();

    return stored;
}

bool http_service_email_message_to_add_2(HTTP_Service_Email_Message *const self, String const *const email) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "email", (void*) email);

    bool const stored = _http_service_email_string_address_ok(email) && _http_service_email_list_add_1(&self->to, string_get_data(email), string_get_size(email));

    trace_log_pop();

    return stored;
}

void http_service_email_message_uninit(HTTP_Service_Email_Message *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    al_str_uninit(&self->bcc);
    al_str_uninit(&self->cc);
    al_str_uninit(&self->headers);
    al_str_uninit(&self->to);
    string_uninit(&self->from);
    string_uninit(&self->from_name);
    string_uninit(&self->html);
    string_uninit(&self->reply_to);
    string_uninit(&self->subject);
    string_uninit(&self->text);

#ifdef ARENA_IMPLEMENTATION
    self->allocator = nullptr;
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();
}

bool http_service_email_message_valid(HTTP_Service_Email const *const self, HTTP_Service_Email_Message const *const message) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "message", (void*) message);

    String  const   *const  from        = _http_service_email_value_or_default(&message->from, &self->from);
    String  const   *const  from_name   = _http_service_email_value_or_default(&message->from_name, &self->from_name);
    bool    const           has_body    = !string_empty(&message->text) || !string_empty(&message->html);

    bool const addresses_ok =
        _http_service_email_string_address_ok(from)
        && _http_service_email_address_list_ok(&message->to)
        && _http_service_email_address_list_ok(&message->cc)
        && _http_service_email_address_list_ok(&message->bcc)
        && (string_empty(&message->reply_to)
            || _http_service_email_string_address_ok(&message->reply_to));

    bool const headers_ok =
        _http_service_email_string_header_safe(from_name)
        && _http_service_email_string_header_safe(&message->subject)
        && _http_service_email_header_list_safe(&message->headers);

    bool const success =
        !string_empty(from) && _http_service_email_recipient_exists(message) && has_body && addresses_ok && headers_ok;

    trace_log_pop();

    return success;
}

bool http_service_email_payload_create_1(HTTP_Service_Email const *const self, HTTP_Service_Email_Message const *const message, String *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "message", (void*) message);
    error_check_null(LOG_METADATA, "out", (void*) out);

    HTTP_Service_Email_Render render = DEFAULT_INITIALIZATION;

    if (!http_service_email_render_init(self, message, &render)) {
        trace_log_pop();

        return false;
    }

    bool const created = http_service_email_payload_create_2(self, message, &render, out);

    trace_log_pop();

    return created;
}

bool http_service_email_payload_create_2(HTTP_Service_Email const *const self,
    HTTP_Service_Email_Message const *const message, HTTP_Service_Email_Render const *const render, String *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "message", (void*) message);
    error_check_null(LOG_METADATA, "render", (void*) render);
    error_check_null(LOG_METADATA, "out", (void*) out);

    if (!http_service_email_valid(self) || !http_service_email_message_valid(self, message) || !_http_service_email_render_valid(render)) {
        trace_log_pop();

        return false;
    }

    String  const   *const  from        = _http_service_email_value_or_default(&message->from, &self->from);
    String  const   *const  from_name   = _http_service_email_value_or_default(&message->from_name, &self->from_name);
    USize   const           reserve     = string_get_size(&message->text)
                                        + string_get_size(&message->html)
                                        + _http_service_email_list_byte_count(&message->to)
                                        + _http_service_email_list_byte_count(&message->cc)
                                        + _http_service_email_list_byte_count(&message->headers)
                                        + _HTTP_SERVICE_EMAIL_PAYLOAD_HEADROOM;

#ifdef ARENA_IMPLEMENTATION
    String payload = _http_service_email_string_init_sized(reserve, self->allocator);
#else
    String payload = _http_service_email_string_init_sized(reserve);
#endif // ARENA_IMPLEMENTATION

    bool added = _http_service_email_string_add(&payload, "From: ")
              && _http_service_email_address_payload_add(&payload, from_name, from, CHAR_STATIC_SIZE("From: "))
              && _http_service_email_string_add(&payload, "\r\n")
              && _http_service_email_list_payload_add(&payload, "To: ", &message->to)
              && _http_service_email_list_payload_add(&payload, "Cc: ", &message->cc);

    if (added && !string_empty(&message->reply_to)) {
        added = _http_service_email_string_add(&payload, "Reply-To: ")
             && _http_service_email_string_add_2(&payload, &message->reply_to)
             && _http_service_email_string_add(&payload, "\r\n");
    }

    if (added && !string_empty(&message->subject)) {
        added = _http_service_email_header_value_add(&payload, "Subject", &message->subject);
    }

    char date[_HTTP_SERVICE_EMAIL_DATE_MAX_SIZE] = DEFAULT_INITIALIZATION;

    datetime_format(&render->date, _HTTP_SERVICE_EMAIL_DATE_FORMAT, date, sizeof(date));

    added = added
         && _http_service_email_string_add(&payload, "Date: ")
         && _http_service_email_string_add(&payload, date)
         && _http_service_email_string_add(&payload, "\r\n")
         && _http_service_email_string_add(&payload, "Message-ID: ")
         && _http_service_email_string_add(&payload, render->message_id)
         && _http_service_email_string_add(&payload, "\r\n")
         && _http_service_email_string_add(&payload, "MIME-Version: 1.0\r\n")
         && _http_service_email_custom_headers_add(&payload, &message->headers)
         && _http_service_email_body_add(&payload, message, render->boundary);

    /* `added` is the half a size test cannot see: an allocator that refuses
     * PART of the way through leaves a well-formed prefix - headers and no body,
     * or a multipart with one part and no closing boundary - which every caller
     * downstream would have sent as if it were the message. */
    if (!added || string_get_size(&payload) == 0) {
        string_uninit(&payload);

        trace_log_pop();

        return false;
    }

    *out = payload;

    trace_log_pop();

    return true;
}

bool http_service_email_render_init(HTTP_Service_Email const *const self, HTTP_Service_Email_Message const *const message, HTTP_Service_Email_Render *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "message", (void*) message);
    error_check_null(LOG_METADATA, "out", (void*) out);

    char id[CRYPTO_RANDOM_HEX_SIZE(_HTTP_SERVICE_EMAIL_MESSAGE_ID_BYTE_COUNT) + CHAR_END_CHARACTER]             = DEFAULT_INITIALIZATION;
    char boundary[CRYPTO_RANDOM_HEX_SIZE(_HTTP_SERVICE_EMAIL_BOUNDARY_BYTE_COUNT) + CHAR_END_CHARACTER]         = DEFAULT_INITIALIZATION;

    /* Entropy failure is visible, never papered over with a predictable value:
     * a guessable boundary is a body-injection seam and a guessable Message-ID
     * is a threading forgery seam. */
    if (result_is_error(crypto_random_hex(id, _HTTP_SERVICE_EMAIL_MESSAGE_ID_BYTE_COUNT)) || result_is_error(crypto_random_hex(boundary, _HTTP_SERVICE_EMAIL_BOUNDARY_BYTE_COUNT))) {
        trace_log_pop();

        return false;
    }

    String  const   *const  from        = _http_service_email_value_or_default(&message->from, &self->from);
    char    const   *const  from_data   = string_get_data(from);
    USize   const           from_size   = string_get_size(from);
    USize                   domain      = 0;

    while (domain < from_size && from_data[domain] != '@') {
        domain += 1;
    }

    domain = domain < from_size ? domain + 1 : from_size;

    USize const domain_size = from_size - domain;
    USize const id_size     = char_length(id);

    out->date = datetime_init_1();

    memory_copy_2(out->boundary, sizeof(out->boundary), _HTTP_SERVICE_EMAIL_BOUNDARY_PREFIX, CHAR_STATIC_SIZE(_HTTP_SERVICE_EMAIL_BOUNDARY_PREFIX));
    memory_copy_2(out->boundary + CHAR_STATIC_SIZE(_HTTP_SERVICE_EMAIL_BOUNDARY_PREFIX),
                  sizeof(out->boundary) - CHAR_STATIC_SIZE(_HTTP_SERVICE_EMAIL_BOUNDARY_PREFIX),
                  boundary, char_length(boundary));

    out->boundary[CHAR_STATIC_SIZE(_HTTP_SERVICE_EMAIL_BOUNDARY_PREFIX) + char_length(boundary)] = '\0';

    /* "<" + hex + "@" + domain + ">" + NUL. A domain longer than the remaining
     * room is truncated rather than overrunning; the Message-ID stays
     * well-formed either way. */
    USize const room = sizeof(out->message_id) - id_size - CHAR_STATIC_SIZE("<@>") - CHAR_END_CHARACTER;
    USize const kept = domain_size < room ? domain_size : room;

    out->message_id[0] = '<';

    memory_copy_2(out->message_id + 1, sizeof(out->message_id) - 1, id, id_size);

    out->message_id[1 + id_size] = '@';

    memory_copy_2(out->message_id + 2 + id_size, sizeof(out->message_id) - 2 - id_size, (void*) (from_data + domain), kept);

    out->message_id[2 + id_size + kept]     = '>';
    out->message_id[3 + id_size + kept]     = '\0';

    trace_log_pop();

    return true;
}

void http_service_email_result_uninit(HTTP_Service_Email_Result *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    string_uninit(&self->error);
    string_uninit(&self->response);

#ifdef ARENA_IMPLEMENTATION
    self->allocator     = nullptr;
#endif // ARENA_IMPLEMENTATION
    self->curl_code     = 0;
    self->response_code = 0;
    self->status        = HTTP_SERVICE_EMAIL_STATUS_OK;
    self->success       = false;

    trace_log_pop();
}

bool http_service_email_security_set(HTTP_Service_Email *const self, HTTP_Service_Email_Security const security) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    self->security = security;

    trace_log_pop();

    return true;
}

HTTP_Service_Email_Result http_service_email_send(HTTP_Service_Email const *const self, HTTP_Service_Email_Message const *const message) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "message", (void*) message);

#ifdef ARENA_IMPLEMENTATION
    HTTP_Service_Email_Result result = _http_service_email_result_init(self->allocator);
#else
    HTTP_Service_Email_Result result = _http_service_email_result_init();
#endif // ARENA_IMPLEMENTATION

    /* Checked SEPARATELY from the message so a service that was never given a
     * URL says so, instead of surfacing later as curl's "URL using bad/illegal
     * format" and sending the caller looking at the recipient.
     *
     * This DELIBERATELY re-validates: payload_create_1 runs valid() and
     * message_valid() again through payload_create_2, so a send pays three full
     * passes (each of which re-parses the URL). That is kept on purpose - the
     * status a send reports has to name the stage it stopped at, and a cached
     * verdict would have to be invalidated by every setter. Three microseconds
     * against an SMTP round trip is not a cost worth the coupling. */
    if (!http_service_email_valid(self)) {
        result.status = HTTP_SERVICE_EMAIL_STATUS_NOT_CONFIGURED;

#ifdef ARENA_IMPLEMENTATION
        _http_service_email_string_set(&result.error, "email service is not configured", self->allocator);
#else
        _http_service_email_string_set(&result.error, "email service is not configured");
#endif // ARENA_IMPLEMENTATION

        trace_log_pop();

        return result;
    }

    if (!http_service_email_message_valid(self, message)) {
        result.status = HTTP_SERVICE_EMAIL_STATUS_INVALID_MESSAGE;

#ifdef ARENA_IMPLEMENTATION
        _http_service_email_string_set(&result.error, "invalid email message", self->allocator);
#else
        _http_service_email_string_set(&result.error, "invalid email message");
#endif // ARENA_IMPLEMENTATION

        trace_log_pop();

        return result;
    }

    String payload = DEFAULT_INITIALIZATION;

    if (!http_service_email_payload_create_1(self, message, &payload)) {
        result.status = HTTP_SERVICE_EMAIL_STATUS_RENDER_FAILED;

#ifdef ARENA_IMPLEMENTATION
        _http_service_email_string_set(&result.error, "message could not be rendered", self->allocator);
#else
        _http_service_email_string_set(&result.error, "message could not be rendered");
#endif // ARENA_IMPLEMENTATION

        trace_log_pop();

        return result;
    }

    String const *const from = _http_service_email_value_or_default(&message->from, &self->from);

    /* The capture transport: the same envelope the SMTP path would send, into a
     * String, with nothing dialed. Bcc appears here as RCPT TO and NOWHERE in
     * the payload, which is the invariant a test needs to see. The payload goes
     * in DOT-STUFFED, because that is what libcurl puts on the wire and what the
     * transcript claims to be. */
    if (self->transport_sink != nullptr) {
        bool const captured = _http_service_email_string_add(self->transport_sink, "MAIL FROM:<")
                           && _http_service_email_string_add_2(self->transport_sink, from)
                           && _http_service_email_string_add(self->transport_sink, ">\r\n")
                           && _http_service_email_capture_list_add(self->transport_sink, &message->to)
                           && _http_service_email_capture_list_add(self->transport_sink, &message->cc)
                           && _http_service_email_capture_list_add(self->transport_sink, &message->bcc)
                           && _http_service_email_string_add(self->transport_sink, "DATA\r\n")
                           && _http_service_email_dot_stuffed_add(self->transport_sink, &payload)
                           && _http_service_email_string_add(self->transport_sink, ".\r\n");

        /* The sink is the caller's String and may be arena-backed: a refusal
         * partway leaves a TRUNCATED transcript, and reporting success on one
         * makes a preview - or a golden the suite reads back - disagree with the
         * message that would actually have gone out. */
        result.success  = captured;
        result.status   = captured ? HTTP_SERVICE_EMAIL_STATUS_OK : HTTP_SERVICE_EMAIL_STATUS_RENDER_FAILED;

        if (!captured) {
#ifdef ARENA_IMPLEMENTATION
            _http_service_email_string_set(&result.error, "the allocator refused the capture sink mid-write", self->allocator);
#else
            _http_service_email_string_set(&result.error, "the allocator refused the capture sink mid-write");
#endif // ARENA_IMPLEMENTATION
        }

        string_uninit(&payload);

        trace_log_pop();

        return result;
    }

    /* No curl_global_init here. This module used to run a SECOND pthread_once of
     * its own beside http/client's, which took curl's global refcount to two
     * while only one curl_global_cleanup (http_client_global_uninit) ever
     * balanced it - so curl's global state was never actually released. email
     * now follows websocket/client's contract instead: the OWNING PROGRAM, or
     * http/client's own once, brings curl up first. See http_client.h's
     * curl_global_init note. */
    CURL *const curl = curl_easy_init();

    if (curl == nullptr) {
        result.status = HTTP_SERVICE_EMAIL_STATUS_TRANSPORT_INIT_FAILED;

#ifdef ARENA_IMPLEMENTATION
        _http_service_email_string_set(&result.error, "curl_easy_init failed", self->allocator);
#else
        _http_service_email_string_set(&result.error, "curl_easy_init failed");
#endif // ARENA_IMPLEMENTATION

        string_uninit(&payload);

        trace_log_pop();

        return result;
    }

    struct curl_slist           *recipients                                          = nullptr;
    char                        error_buffer[CURL_ERROR_SIZE]                        = DEFAULT_INITIALIZATION;
    char                        from_envelope[_HTTP_SERVICE_EMAIL_ENVELOPE_MAX_SIZE] = DEFAULT_INITIALIZATION;
    HTTP_Service_Email_Upload   upload                                               = {
        .offset = 0,
        .payload = &payload
    };

    bool const enveloped = _http_service_email_envelope_write(from_envelope, sizeof(from_envelope), string_get_data(from), string_get_size(from));

    /* An envelope or a recipient list that could not be built is a transport the
     * message cannot go out on, not a message to send to fewer people. */
    if (!enveloped || !_http_service_email_curl_recipients_create(message, &recipients)) {
        result.status = HTTP_SERVICE_EMAIL_STATUS_TRANSPORT_INIT_FAILED;

#ifdef ARENA_IMPLEMENTATION
        _http_service_email_string_set(&result.error, "the SMTP envelope could not be built", self->allocator);
#else
        _http_service_email_string_set(&result.error, "the SMTP envelope could not be built");
#endif // ARENA_IMPLEMENTATION

        curl_easy_cleanup(curl);
        string_uninit(&payload);

        trace_log_pop();

        return result;
    }

    /* curl_easy_setopt is VARIADIC and reads a `long` for every option in this
     * family. Passing an I32 works by accident of register zero-extension on
     * Windows and is undefined behaviour on the LP64 Linux the CRM VPS runs.
     * The two timeouts go through _long_clamp for the other half of the same
     * problem: a USize past LONG_MAX narrows into an implementation-defined
     * value on the LLP64 build, and curl rejects a negative one. */
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, _http_service_email_long_clamp(self->connect_timeout_ms));
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, from_envelope);
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, _http_service_email_read_callback);
    curl_easy_setopt(curl, CURLOPT_READDATA, &upload);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, self->verify_tls ? 2L : 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, self->verify_tls ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, _http_service_email_long_clamp(self->timeout_ms));
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_URL, string_get_data(&self->url));
    curl_easy_setopt(curl, CURLOPT_USE_SSL, _http_service_email_security_to_curl(self->security));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _http_service_email_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result.response);

    if (!string_empty(&self->auth_mechanism)) {
        curl_easy_setopt(curl, CURLOPT_LOGIN_OPTIONS, string_get_data(&self->auth_mechanism));
    }

    if (!string_empty(&self->ca_bundle)) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, string_get_data(&self->ca_bundle));
    }

    if (!string_empty(&self->username)) {
        curl_easy_setopt(curl, CURLOPT_USERNAME, string_get_data(&self->username));
    }

    if (!string_empty(&self->password)) {
        curl_easy_setopt(curl, CURLOPT_PASSWORD, string_get_data(&self->password));
    }

    CURLcode    const   code            = curl_easy_perform(curl);
    long                response_code   = 0;

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

    result.curl_code        = (I32) code;
    result.response_code    = response_code > 0 ? (USize) response_code : 0;
    result.success          = code == CURLE_OK;
    result.status           = result.success ? HTTP_SERVICE_EMAIL_STATUS_OK : HTTP_SERVICE_EMAIL_STATUS_TRANSPORT_FAILED;

    if (!result.success) {
        char const *const error = error_buffer[0] != '\0' ? error_buffer : curl_easy_strerror(code);

#ifdef ARENA_IMPLEMENTATION
        _http_service_email_string_set(&result.error, error, self->allocator);
#else
        _http_service_email_string_set(&result.error, error);
#endif // ARENA_IMPLEMENTATION
    }

    curl_slist_free_all(recipients);
    curl_easy_cleanup(curl);
    string_uninit(&payload);

    trace_log_pop();

    return result;
}

char* http_service_email_status_name(HTTP_Service_Email_Status const status) {
    trace_log_push(LOG_METADATA);

    char *name = (char*) "unknown";

    if (status == HTTP_SERVICE_EMAIL_STATUS_OK) {
        name = (char*) "ok";
    }
    else if (status == HTTP_SERVICE_EMAIL_STATUS_NOT_CONFIGURED) {
        name = (char*) "not_configured";
    }
    else if (status == HTTP_SERVICE_EMAIL_STATUS_INVALID_MESSAGE) {
        name = (char*) "invalid_message";
    }
    else if (status == HTTP_SERVICE_EMAIL_STATUS_RENDER_FAILED) {
        name = (char*) "render_failed";
    }
    else if (status == HTTP_SERVICE_EMAIL_STATUS_TRANSPORT_INIT_FAILED) {
        name = (char*) "transport_init_failed";
    }
    else if (status == HTTP_SERVICE_EMAIL_STATUS_TRANSPORT_FAILED) {
        name = (char*) "transport_failed";
    }
    else if (status == HTTP_SERVICE_EMAIL_STATUS_INVALID_CONFIGURATION) {
        name = (char*) "invalid_configuration";
    }

    trace_log_pop();

    return name;
}

bool http_service_email_timeouts_set(HTTP_Service_Email *const self, USize const connect_timeout_ms, USize const timeout_ms) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* Timeouts are configuration DATA, read from env and settings tables. A 0
     * means "wait forever" to curl, which on a request path is a hang - refused
     * as a value, never routed through an abort primitive. */
    if (connect_timeout_ms == 0 || timeout_ms == 0) {
        trace_log_pop();

        return false;
    }

    self->connect_timeout_ms    = connect_timeout_ms;
    self->timeout_ms            = timeout_ms;

    trace_log_pop();

    return true;
}

bool http_service_email_transport_capture_set(HTTP_Service_Email *const self, String *const sink) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    self->transport_sink = sink;

    trace_log_pop();

    return true;
}

void http_service_email_uninit(HTTP_Service_Email *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* The password is freed, not zeroed: the tree has no memory_wipe yet, so it
     * can survive in freed heap until the allocator reuses that block. */
    string_uninit(&self->auth_mechanism);
    string_uninit(&self->ca_bundle);
    string_uninit(&self->from);
    string_uninit(&self->from_name);
    string_uninit(&self->password);
    string_uninit(&self->url);
    string_uninit(&self->username);

#ifdef ARENA_IMPLEMENTATION
    self->allocator             = nullptr;
#endif // ARENA_IMPLEMENTATION
    /* `security` and `verify_tls` both go back to what BOTH constructors default
     * them to. Leaving verify_tls false here made a re-initialized-by-hand
     * service silently skip certificate verification, which is the one asymmetry
     * a reset must never introduce. */
    self->connect_timeout_ms    = 0;
    self->security              = HTTP_SERVICE_EMAIL_SECURITY_REQUIRED;
    self->timeout_ms            = 0;
    self->transport_sink        = nullptr;
    self->verify_tls            = true;

    trace_log_pop();
}

bool http_service_email_url_set(HTTP_Service_Email *const self, char const *const url) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "url", (void*) url);

    if (!_http_service_email_url_ok(url, char_length(url))) {
        trace_log_pop();

        return false;
    }

#ifdef ARENA_IMPLEMENTATION
    bool const stored = _http_service_email_string_set(&self->url, url, self->allocator);
#else
    bool const stored = _http_service_email_string_set(&self->url, url);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return stored;
}

bool http_service_email_valid(HTTP_Service_Email const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    bool const success = _http_service_email_url_ok(string_get_data(&self->url), string_get_size(&self->url))
                      && _http_service_email_string_address_ok(&self->from)
                      && self->connect_timeout_ms > 0
                      && self->timeout_ms > 0;

    trace_log_pop();

    return success;
}

bool http_service_email_verify_tls_set(HTTP_Service_Email *const self, bool const verify_tls) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    self->verify_tls = verify_tls;

    trace_log_pop();

    return true;
}