#include <http/service/session/session.h>

// char.h is singled out because it declares no TYPES at all: no header's API can ever name one,
// so every chain that happens to reach it is accidental and a file using char_* owes it directly.
#include <char/char.h>

#define _HTTP_SERVICE_SESSION_HASH_BYTE_COUNT CRYPTO_HASH_SHA256_SIZE

/*==============================================================================
 * MARK: - Helpers
 *============================================================================*/

/*
 * string_add_2 already writes the terminator after every append (string.c), so this has work
 * to do only on a String nothing was appended to: it materializes a real "" so that an ABSENT
 * cookie reads back through string_get_data as "" rather than nullptr. Consumers pass that
 * pointer straight into the next call; a nullptr there aborts the server pre-auth.
 */
static void _http_service_session_string_terminate(String *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (!string_empty(self)) {
        trace_log_pop();

        return;
    }

    char const terminator = '\0';

    string_add_last_2(self, &terminator, 1);
    string_set_size(self, 0);

    trace_log_pop();
}

static String _http_service_session_string_init(HTTP_Service_Session const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

#ifdef ARENA_IMPLEMENTATION
    if (self->cookie.allocator != nullptr) {
        String const string = string_alloc_init_1(self->cookie.allocator);

        trace_log_pop();

        return string;
    }
#endif // ARENA_IMPLEMENTATION

    String const string = string_init_1();

    trace_log_pop();

    return string;
}

/*
 * An empty cookie_path / same_site String is a legal config VALUE (CFW empty-string policy),
 * but its data pointer is null and the cookie module's error_check_null aborts on that -
 * substitute "" at the read site. The cookie NAME can never be empty here: a constructor
 * refuses an empty or non-token name before the service exists.
 */
static char* _http_service_session_text(String const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (string_empty(self)) {
        trace_log_pop();

        return "";
    }

    char* const data = string_get_data(self);

    trace_log_pop();

    return data;
}

/* datetime_now() + ttl on a huge configured ttl wraps to a timestamp in the PAST, which would
 * make every fresh token read as already expired. Clamp instead - a saturated expiry is a
 * token that never expires on its own, which is what the caller asked for. */
static USize _http_service_session_expires_at(USize const ttl) {
    trace_log_push(LOG_METADATA);

    USize const now = (USize) datetime_now();

    if (ttl > USIZE_MAX - now) {
        trace_log_pop();

        return USIZE_MAX;
    }

    trace_log_pop();

    return now + ttl;
}

/*
 * The digest itself, rendered into the CALLER's buffer, shared by token_hash and
 * token_verify_1. Keeping it out of a String is what lets verify allocate in NO tier: it
 * compares this stack hex against the stored hash directly, so a verify on an arena-backed
 * service leaves the arena's used size unchanged and session.h's arena bullet stays true by
 * naming only the functions that hand a String back. token_hash still allocates - it must
 * return one.
 */
static bool _http_service_session_digest_hex(char const *const token, char hex[ENCODING_HEX_SIZE(_HTTP_SERVICE_SESSION_HASH_BYTE_COUNT) + CHAR_END_CHARACTER]) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "hex", (void*) hex);
    error_check_null(LOG_METADATA, "token", (void*) token);

    U8 digest[_HTTP_SERVICE_SESSION_HASH_BYTE_COUNT] = DEFAULT_INITIALIZATION;

    if (result_is_error(crypto_hash_sha256((U8 const*) token, char_length(token), digest))) {
        trace_log_pop();

        return false;
    }

    encoding_hex_encode_1(digest, _HTTP_SERVICE_SESSION_HASH_BYTE_COUNT, hex);

    trace_log_pop();

    return true;
}

/*
 * The one comparison both verify tiers bottom out in, SIZED on both sides. The digest side is
 * always this module's own stack hex, of a compile-time size; the STORED side is whatever the
 * caller holds - token_verify_1 measures a C string with char_length, token_verify_4 passes the
 * String's own size, so a sized String over a longer terminated buffer compares the bytes it
 * claims and no more. csrf.c's _http_service_csrf_token_equal is the twin: two sibling modules
 * must not answer the same question two ways.
 */
static bool _http_service_session_hash_equal(char const *const hex, USize const hex_size, char const *const hash, USize const hash_size) {
    trace_log_push(LOG_METADATA);

    /* The stored side is DATA - an empty database cell arrives here as "" or null. Refuse as a
     * value, never abort, and never let two empties compare equal. */
    if (hex == nullptr || hash == nullptr || hex_size == 0 || hash_size == 0) {
        trace_log_pop();

        return false;
    }

    bool const success = char_compare_equal_comptime_2(hex, hex_size, hash, hash_size);

    trace_log_pop();

    return success;
}

/*
 * The single cookie read, shared by the three cookie_read tiers: ask the cookie module for the
 * value in THIS service's allocation tier and terminate it. One allocation per authenticated
 * request - an earlier version copied the module's answer into a second String.
 */
static String _http_service_session_cookie_value(HTTP_Service_Session const *const self, char const *const cookie_header, USize const cookie_header_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* Request data, never error_checked: a request with no Cookie header at all - and a String
     * or Str that was never filled - is an ordinary unauthenticated request, not a contract
     * violation. It answers the same terminated "" the absent-cookie case answers. */
    if (cookie_header == nullptr || cookie_header_size == 0) {
        String empty = _http_service_session_string_init(self);

        _http_service_session_string_terminate(&empty);

        trace_log_pop();

        return empty;
    }

#ifdef ARENA_IMPLEMENTATION
    if (self->cookie.allocator != nullptr) {
        String arena_value = http_cookie_alloc_get_2(cookie_header, cookie_header_size, _http_service_session_text(&self->cookie.name), self->cookie.allocator);

        _http_service_session_string_terminate(&arena_value);

        trace_log_pop();

        return arena_value;
    }
#endif // ARENA_IMPLEMENTATION

    String value = http_cookie_get_2(cookie_header, cookie_header_size, _http_service_session_text(&self->cookie.name));

    _http_service_session_string_terminate(&value);

    trace_log_pop();

    return value;
}

/*
 * The tail both cookie_hash tiers share. The cookie value has already been read and is OWNED
 * from here: hash it into `out` and release it either way. An absent cookie and a failed digest
 * are the same answer to a route - unauthenticated - so both leave *out EMPTY and answer false.
 */
static bool _http_service_session_hash_value(HTTP_Service_Session const *const self, String *const value, String *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "value", (void*) value);
    error_check_null(LOG_METADATA, "out", (void*) out);

    *out = (String) DEFAULT_INITIALIZATION;

    if (string_empty(value)) {
        string_uninit(value);

        trace_log_pop();

        return false;
    }

    String hash = http_service_session_token_hash(self, string_get_data(value));

    string_uninit(value);

    if (string_empty(&hash)) {
        string_uninit(&hash);

        trace_log_pop();

        return false;
    }

    *out = hash;

    trace_log_pop();

    return true;
}

/*
 * Every reason a configured service must not exist, checked once, before it does.
 *
 * Only the two numeric rules live here. Everything about the cookie itself is delegated: the
 * cookie module owns the syntax (RFC 6265 token, no CTL/';'/',' in Path) AND, since 0.4.0, the
 * dropped-cookie rules this file used to duplicate - `__Host-` needs Secure and Path="/",
 * `__Secure-` needs Secure, SameSite=None needs Secure. Rendering the template once reads all
 * of them back through one refusal, and the cookie module logs which one at WARN. That render
 * is also what catches a REFUSED ARENA - an arena that could not copy the name leaves the
 * template's name EMPTY, which the cookie module refuses as "not a valid token".
 */
static bool _http_service_session_config_valid(char const *const caller, HTTP_Cookie const *const cookie, USize const token_byte_count, USize const ttl) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "caller", (void*) caller);
    error_check_null(LOG_METADATA, "cookie", (void*) cookie);

    if (token_byte_count < HTTP_SERVICE_SESSION_TOKEN_BYTE_COUNT_MIN) {
        log_message_2(
            LOG_LEVEL_WARN, LOG_METADATA, "%s: refusing - token_byte_count %llu is below the %d-byte floor (a shorter token is guessable)", caller, (unsigned long long) token_byte_count,
            HTTP_SERVICE_SESSION_TOKEN_BYTE_COUNT_MIN);

        trace_log_pop();

        return false;
    }

    if (ttl == 0) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "%s: refusing - a ttl of 0 emits Max-Age=0, a cookie that expires on arrival", caller);

        trace_log_pop();

        return false;
    }

    String  probe = http_cookie_set_header_value_create(cookie);
    bool    valid = !string_empty(&probe);

    string_uninit(&probe);

    if (!valid) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "%s: refusing - the cookie module rejected the configured cookie; its own WARN above names the rule", caller);
    }

    trace_log_pop();

    return valid;
}

static bool _http_service_session_init(HTTP_Service_Session *const self,
    char const *const caller, USize const token_byte_count, USize const ttl, char const *const cookie_name, char const *const cookie_path,
    char const *const same_site, bool const secure, bool const http_only, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "caller", (void*) caller);
    error_check_null(LOG_METADATA, "cookie_name", (void*) cookie_name);
    error_check_null(LOG_METADATA, "cookie_path", (void*) cookie_path);
    error_check_null(LOG_METADATA, "same_site", (void*) same_site);

    *self = (HTTP_Service_Session) DEFAULT_INITIALIZATION;

    if (char_length(same_site) > 0 && http_cookie_same_site_parse(same_site) == HTTP_COOKIE_SAME_SITE_UNSET) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "%s: same_site \"%s\" is not Lax/None/Strict (case-insensitive) - cookies will be built with SameSite omitted", caller, same_site);
    }

    HTTP_Cookie cookie = DEFAULT_INITIALIZATION;

#ifdef ARENA_IMPLEMENTATION
    if (allocator != nullptr) {
        /* A refused arena leaves the template's name EMPTY; the validation render below reads
         * that as an invalid name and refuses whole, so a false here needs no separate branch. */
        (void) http_cookie_alloc_init_2(&cookie, cookie_name, "", cookie_path, http_cookie_same_site_parse(same_site), secure, http_only, allocator);
    }
    else {
        cookie = http_cookie_init_2(cookie_name, "", cookie_path, http_cookie_same_site_parse(same_site), secure, http_only);
    }
#else
    cookie = http_cookie_init_2(cookie_name, "", cookie_path, http_cookie_same_site_parse(same_site), secure, http_only);
#endif // ARENA_IMPLEMENTATION

    if (!_http_service_session_config_valid(caller, &cookie, token_byte_count, ttl)) {
        http_cookie_uninit(&cookie);

        trace_log_pop();

        return false;
    }

    self->cookie            = cookie;
    self->token_byte_count  = token_byte_count;
    self->ttl               = ttl;

    trace_log_pop();

    return true;
}

/*==============================================================================
 * MARK: - API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
bool http_service_session_alloc_init_1(HTTP_Service_Session *const self, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    bool const success = http_service_session_alloc_init_2(
        self, HTTP_SERVICE_SESSION_DEFAULT_TOKEN_BYTE_COUNT, HTTP_SERVICE_SESSION_DEFAULT_TTL, HTTP_SERVICE_SESSION_DEFAULT_COOKIE_NAME, HTTP_SERVICE_SESSION_DEFAULT_COOKIE_PATH,
        HTTP_SERVICE_SESSION_DEFAULT_SAME_SITE, HTTP_SERVICE_SESSION_DEFAULT_SECURE, HTTP_SERVICE_SESSION_DEFAULT_HTTP_ONLY, allocator);

    trace_log_pop();

    return success;
}

bool http_service_session_alloc_init_2(HTTP_Service_Session *const self,
    USize const token_byte_count, USize const ttl, char const *const cookie_name, char const *const cookie_path, char const *const same_site,
    bool const secure, bool const http_only, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "cookie_name", (void*) cookie_name);
    error_check_null(LOG_METADATA, "cookie_path", (void*) cookie_path);
    error_check_null(LOG_METADATA, "same_site", (void*) same_site);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    bool const success = _http_service_session_init(
        self, "http_service_session_alloc_init_2", token_byte_count, ttl, cookie_name, cookie_path, same_site, secure, http_only, allocator);

    trace_log_pop();

    return success;
}
#endif // ARENA_IMPLEMENTATION

String http_service_session_cookie_clear(HTTP_Service_Session const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* The template's own value is empty and the clear render ignores a value entirely, so the
     * stored template IS the clear cookie - nothing is built per call. */
    String value = http_cookie_clear_header_value_create_3(&self->cookie);

    _http_service_session_string_terminate(&value);

    trace_log_pop();

    return value;
}

String http_service_session_cookie_create(HTTP_Service_Session const *const self, char const *const token) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "token", (void*) token);

    /* An empty token is a token_create failure the caller did not check. Emitting
     * "__Host-session=; Max-Age=86400" would set a real cookie with no session in it - a
     * silent lockout rather than a breach, but still a cookie nobody asked for. */
    if (char_length(token) == 0) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_session_cookie_create: refusing - the token is empty (an unchecked http_service_session_token_create failure)");

        String value = _http_service_session_string_init(self);

        _http_service_session_string_terminate(&value);

        trace_log_pop();

        return value;
    }

    /* One render off the stored template - no per-response HTTP_Cookie is built, copied, or
     * uninitialized. The template is not mutated, so a shared service renders concurrently. */
    String value = DEFAULT_INITIALIZATION;

    (void) http_cookie_set_header_value_create_2(&self->cookie, token, self->ttl, &value);

    _http_service_session_string_terminate(&value);

    trace_log_pop();

    return value;
}

bool http_service_session_cookie_hash(HTTP_Service_Session const *const self, char const *const cookie_header, String *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "out", (void*) out);

    *out = (String) DEFAULT_INITIALIZATION;

    /* Request data, never error_checked: a request with no Cookie header at all is an ordinary
     * unauthenticated request, not a contract violation. cookie_read already reads a null or empty
     * header as absent, so this branch is a FAST PATH rather than a guard - it answers the same
     * false without allocating the empty String that scan would hand back. The empty test reads
     * one byte rather than running char_length, and it makes this tier cost what the String tier
     * costs on the same request. */
    if (cookie_header == nullptr || cookie_header[0] == '\0') {
        trace_log_pop();

        return false;
    }

    String      value = http_service_session_cookie_read(self, cookie_header);
    bool const  found = _http_service_session_hash_value(self, &value, out);

    trace_log_pop();

    return found;
}

bool http_service_session_cookie_hash_4(HTTP_Service_Session const *const self, String const *const cookie_header, String *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "out", (void*) out);

    *out = (String) DEFAULT_INITIALIZATION;

    /* Request data, never error_checked: cookie_read_4 reads a null or empty String as the
     * absent cookie, which becomes the same false the char* tier answers on a null header. This
     * is that answer one allocation earlier - the char* tier's fast path, on the shape an
     * ordinary unauthenticated request actually has. */
    if (cookie_header == nullptr || string_empty(cookie_header)) {
        trace_log_pop();

        return false;
    }

    String      value = http_service_session_cookie_read_4(self, cookie_header);
    bool const  found = _http_service_session_hash_value(self, &value, out);

    trace_log_pop();

    return found;
}

char* http_service_session_cookie_name_get(HTTP_Service_Session const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    char* const name = _http_service_session_text(&self->cookie.name);

    trace_log_pop();

    return name;
}

String http_service_session_cookie_read(HTTP_Service_Session const *const self, char const *const cookie_header) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    String const value = _http_service_session_cookie_value(self, cookie_header, cookie_header == nullptr ? 0 : char_length(cookie_header));

    trace_log_pop();

    return value;
}

String http_service_session_cookie_read_3(HTTP_Service_Session const *const self, Str const *const cookie_header) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    String const value = _http_service_session_cookie_value(self, cookie_header == nullptr ? nullptr : str_get_data(cookie_header), cookie_header == nullptr ? 0 : str_get_size(cookie_header));

    trace_log_pop();

    return value;
}

String http_service_session_cookie_read_4(HTTP_Service_Session const *const self, String const *const cookie_header) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    String const value = _http_service_session_cookie_value(
        self, cookie_header == nullptr ? nullptr : string_get_data(cookie_header), cookie_header == nullptr ? 0 : string_get_size(cookie_header));

    trace_log_pop();

    return value;
}

bool http_service_session_init_1(HTTP_Service_Session *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    bool const success = http_service_session_init_2(
        self, HTTP_SERVICE_SESSION_DEFAULT_TOKEN_BYTE_COUNT, HTTP_SERVICE_SESSION_DEFAULT_TTL, HTTP_SERVICE_SESSION_DEFAULT_COOKIE_NAME, HTTP_SERVICE_SESSION_DEFAULT_COOKIE_PATH,
        HTTP_SERVICE_SESSION_DEFAULT_SAME_SITE, HTTP_SERVICE_SESSION_DEFAULT_SECURE, HTTP_SERVICE_SESSION_DEFAULT_HTTP_ONLY);

    trace_log_pop();

    return success;
}

bool http_service_session_init_2(HTTP_Service_Session *const self,
    USize const token_byte_count, USize const ttl, char const *const cookie_name, char const *const cookie_path, char const *const same_site,
    bool const secure, bool const http_only) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "cookie_name", (void*) cookie_name);
    error_check_null(LOG_METADATA, "cookie_path", (void*) cookie_path);
    error_check_null(LOG_METADATA, "same_site", (void*) same_site);

    bool const success = _http_service_session_init(self, "http_service_session_init_2", token_byte_count, ttl, cookie_name, cookie_path, same_site, secure, http_only, nullptr);

    trace_log_pop();

    return success;
}

bool http_service_session_mint(HTTP_Service_Session const *const self, HTTP_Service_Session_Token *const token, String *const hash, String *const cookie) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "token", (void*) token);
    error_check_null(LOG_METADATA, "hash", (void*) hash);
    error_check_null(LOG_METADATA, "cookie", (void*) cookie);

    *token  = (HTTP_Service_Session_Token) DEFAULT_INITIALIZATION;
    *hash   = (String) DEFAULT_INITIALIZATION;
    *cookie = (String) DEFAULT_INITIALIZATION;

    if (!http_service_session_token_create(self, token)) {
        trace_log_pop();

        return false;
    }

    *hash = http_service_session_token_hash(self, string_get_data(&token->value));

    if (string_empty(hash)) {
        http_service_session_token_uninit(token);
        string_uninit(hash);

        trace_log_pop();

        return false;
    }

    *cookie = http_service_session_cookie_create(self, string_get_data(&token->value));

    if (string_empty(cookie)) {
        http_service_session_token_uninit(token);
        string_uninit(hash);
        string_uninit(cookie);

        trace_log_pop();

        return false;
    }

    trace_log_pop();

    return true;
}

bool http_service_session_token_create(HTTP_Service_Session const *const self, HTTP_Service_Session_Token *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "out", (void*) out);

    *out = (HTTP_Service_Session_Token) DEFAULT_INITIALIZATION;

    USize const value_size = CRYPTO_RANDOM_HEX_SIZE(self->token_byte_count);

    /* ONE allocation: the CSPRNG writes its hex straight into the String's own buffer. The
     * earlier shape borrowed a scratch buffer, encoded into it, copied that into a fresh
     * String and released the scratch - two allocations and a copy of the secret per mint. */
#ifdef ARENA_IMPLEMENTATION
    String value = self->cookie.allocator != nullptr ? string_alloc_init_2(value_size + CHAR_END_CHARACTER, self->cookie.allocator) : string_init_2(value_size + CHAR_END_CHARACTER);
#else
    String value = string_init_2(value_size + CHAR_END_CHARACTER);
#endif // ARENA_IMPLEMENTATION

    char *const buffer = string_get_data(&value);

    /* A refused arena answers the EMPTY String, whose data IS nullptr - that is the same
     * half-object (an expiry with no value) this signature exists to prevent. */
    if (buffer == nullptr || result_is_error(crypto_random_hex(buffer, self->token_byte_count))) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_session_token_create: refusing - no buffer, or the CSPRNG did not produce a token");

        string_uninit(&value);

        trace_log_pop();

        return false;
    }

    string_set_size(&value, value_size);

    out->expires_at = _http_service_session_expires_at(self->ttl);
    out->value      = value;

    trace_log_pop();

    return true;
}

bool http_service_session_token_expired(HTTP_Service_Session_Token const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    bool const expired = (USize) datetime_now() >= self->expires_at;

    trace_log_pop();

    return expired;
}

String http_service_session_token_hash(HTTP_Service_Session const *const self, char const *const token) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "token", (void*) token);

    char hex[ENCODING_HEX_SIZE(_HTTP_SERVICE_SESSION_HASH_BYTE_COUNT) + CHAR_END_CHARACTER] = DEFAULT_INITIALIZATION;

    if (!_http_service_session_digest_hex(token, hex)) {
        // Fail closed with an empty String - every mint site gates on string_empty
        // before using the hash.
        trace_log_pop();

        return (String) DEFAULT_INITIALIZATION;
    }

    String string = _http_service_session_string_init(self);

    /* The size is a positive compile-time constant, so the append is unconditional; the
     * terminate call below still has a job on a REFUSED arena, which appends nothing. */
    string_add_last_2(&string, hex, ENCODING_HEX_SIZE(_HTTP_SERVICE_SESSION_HASH_BYTE_COUNT));
    _http_service_session_string_terminate(&string);

    trace_log_pop();

    return string;
}

void http_service_session_token_uninit(HTTP_Service_Session_Token *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    string_uninit(&self->value);

    self->expires_at = 0;

    trace_log_pop();
}

bool http_service_session_token_verify_1(HTTP_Service_Session const *const self, char const *const token, char const *const hash) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* token and hash are DATA - a missing cookie and an empty database cell both arrive here
     * as an empty (or null) string. Refuse as a value, never abort. */
    if (token == nullptr || hash == nullptr || char_length(token) == 0 || char_length(hash) == 0) {
        trace_log_pop();

        return false;
    }

    /* The digest goes to the STACK, never through token_hash: verify hands nothing back, so it
     * must not borrow from the service's arena either - an arena reclaims nothing and every
     * verify would leak into it. */
    char hex[ENCODING_HEX_SIZE(_HTTP_SERVICE_SESSION_HASH_BYTE_COUNT) + CHAR_END_CHARACTER] = DEFAULT_INITIALIZATION;

    if (!_http_service_session_digest_hex(token, hex)) {
        trace_log_pop();

        return false;
    }

    bool const success = _http_service_session_hash_equal(hex, ENCODING_HEX_SIZE(_HTTP_SERVICE_SESSION_HASH_BYTE_COUNT), hash, char_length(hash));

    trace_log_pop();

    return success;
}

bool http_service_session_token_verify_4(HTTP_Service_Session const *const self, char const *const token, String const *const hash) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* token and hash are DATA, exactly as in the char* tier. This tier does NOT delegate to it:
     * routing through string_get_data would hand the stored hash over as a C string and have it
     * re-measured with char_length, so a String bounded short of its own terminated buffer would
     * compare bytes past the size it claims - and one whose data is not terminated at all would
     * be an over-read. */
    if (token == nullptr || hash == nullptr || char_length(token) == 0 || string_empty(hash)) {
        trace_log_pop();

        return false;
    }

    /* The digest goes to the STACK, never through token_hash: verify hands nothing back, so it
     * must not borrow from the service's arena either. */
    char hex[ENCODING_HEX_SIZE(_HTTP_SERVICE_SESSION_HASH_BYTE_COUNT) + CHAR_END_CHARACTER] = DEFAULT_INITIALIZATION;

    if (!_http_service_session_digest_hex(token, hex)) {
        trace_log_pop();

        return false;
    }

    /* The stored side is read through the String's OWN size, not char_length. */
    bool const success = _http_service_session_hash_equal(
        hex, ENCODING_HEX_SIZE(_HTTP_SERVICE_SESSION_HASH_BYTE_COUNT), string_get_data(hash), string_get_size(hash));

    trace_log_pop();

    return success;
}

void http_service_session_uninit(HTTP_Service_Session *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    http_cookie_uninit(&self->cookie);

    self->token_byte_count  = 0;
    self->ttl               = 0;

    trace_log_pop();
}