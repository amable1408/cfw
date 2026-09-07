#include <http/service/csrf/csrf.h>

// char.h is singled out because it declares no TYPES at all: no header's API can ever name one,
// so every chain that happens to reach it is accidental and a file using char_* owes it directly.
#include <char/char.h>

/*==============================================================================
 * MARK: - Helpers
 *============================================================================*/

/*
 * string_add_2 already writes the terminator after every append (string.c), so this has work
 * to do only on a String nothing was appended to: it materializes a real "" so that an ABSENT
 * cookie reads back through string_get_data as "" rather than nullptr.
 */
static void _http_service_csrf_string_terminate(String *const self) {
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

static String _http_service_csrf_string_init(HTTP_Service_CSRF const *const self) {
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
 * substitute "" at the read site. The cookie NAME and the header NAME can never be empty
 * here: a constructor refuses both before the service exists.
 */
static char* _http_service_csrf_text(String const *const self) {
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
 * make every fresh token read as already expired. Clamp instead. */
static USize _http_service_csrf_expires_at(USize const ttl) {
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
 * The one comparison every verify and gate tier bottoms out in, SIZED on both sides. The
 * char* tiers measure with char_length; the String tiers pass the String's own size, so a
 * sized String over a longer terminated buffer compares the bytes it claims and no more.
 * Two empty sides never compare equal - a request with neither header nor cookie is precisely
 * the cross-site case.
 */
static bool _http_service_csrf_token_equal(char const *const token, USize const token_size, char const *const cookie_value, USize const cookie_value_size) {
    trace_log_push(LOG_METADATA);

    /* Both sides are REQUEST DATA: refuse as a value, never abort. */
    if (token == nullptr || cookie_value == nullptr || token_size == 0 || cookie_value_size == 0) {
        trace_log_pop();

        return false;
    }

    bool const success = char_compare_equal_comptime_2(token, token_size, cookie_value, cookie_value_size);

    trace_log_pop();

    return success;
}

/*
 * The single cookie read, shared by both cookie_read tiers: ask the cookie module for the value
 * in THIS service's allocation tier and terminate it. One allocation per protected request - an
 * earlier version copied the module's answer into a second String.
 */
static String _http_service_csrf_cookie_value(HTTP_Service_CSRF const *const self, char const *const cookie_header, USize const cookie_header_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* Request data, never error_checked: a request with no Cookie header at all - and a String
     * that was never filled - is the ordinary shape of a cross-site attempt, not a contract
     * violation. It answers the same terminated "" the absent-cookie case answers. */
    if (cookie_header == nullptr || cookie_header_size == 0) {
        String empty = _http_service_csrf_string_init(self);

        _http_service_csrf_string_terminate(&empty);

        trace_log_pop();

        return empty;
    }

#ifdef ARENA_IMPLEMENTATION
    if (self->cookie.allocator != nullptr) {
        String arena_value = http_cookie_alloc_get_2(cookie_header, cookie_header_size, _http_service_csrf_text(&self->cookie.name), self->cookie.allocator);

        _http_service_csrf_string_terminate(&arena_value);

        trace_log_pop();

        return arena_value;
    }
#endif // ARENA_IMPLEMENTATION

    String value = http_cookie_get_2(cookie_header, cookie_header_size, _http_service_csrf_text(&self->cookie.name));

    _http_service_csrf_string_terminate(&value);

    trace_log_pop();

    return value;
}

/*
 * Every reason a configured service must not exist, checked once, before it does.
 *
 * Only the three numeric and header-name rules live here. Everything about the cookie itself is
 * delegated: the cookie module owns the syntax AND, since 0.4.0, the dropped-cookie rules this
 * file used to duplicate - `__Host-` needs Secure and Path="/", `__Secure-` needs Secure,
 * SameSite=None needs Secure. Those matter most in DEVELOPMENT, where a "Secure off for
 * localhost" toggle silently deletes the whole protection: the header still arrives, the cookie
 * never does. Rendering the template once reads all of them back through one refusal, and the
 * cookie module logs which one at WARN. That render is also what catches a REFUSED ARENA, whose
 * empty name the cookie module rejects as "not a valid token".
 */
static bool _http_service_csrf_config_valid(char const *const caller, HTTP_Cookie const *const cookie, char const *const header_name, USize const token_byte_count, USize const ttl) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "caller", (void*) caller);
    error_check_null(LOG_METADATA, "cookie", (void*) cookie);
    error_check_null(LOG_METADATA, "header_name", (void*) header_name);

    if (token_byte_count < HTTP_SERVICE_CSRF_TOKEN_BYTE_COUNT_MIN) {
        log_message_2(
            LOG_LEVEL_WARN, LOG_METADATA, "%s: refusing - token_byte_count %llu is below the %d-byte floor (a shorter token is guessable)", caller, (unsigned long long) token_byte_count,
            HTTP_SERVICE_CSRF_TOKEN_BYTE_COUNT_MIN);

        trace_log_pop();

        return false;
    }

    if (ttl == 0) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "%s: refusing - a ttl of 0 emits Max-Age=0, a cookie that expires on arrival", caller);

        trace_log_pop();

        return false;
    }

    if (char_length(header_name) == 0) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "%s: refusing - header_name is empty, or the arena refused its copy, so no request could ever carry the token", caller);

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

static bool _http_service_csrf_init(HTTP_Service_CSRF *const self,
    char const *const caller, USize const token_byte_count, USize const ttl, char const *const cookie_name, char const *const cookie_path,
    char const *const header_name, char const *const same_site, bool const secure, bool const http_only, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "caller", (void*) caller);
    error_check_null(LOG_METADATA, "cookie_name", (void*) cookie_name);
    error_check_null(LOG_METADATA, "cookie_path", (void*) cookie_path);
    error_check_null(LOG_METADATA, "header_name", (void*) header_name);
    error_check_null(LOG_METADATA, "same_site", (void*) same_site);

    *self = (HTTP_Service_CSRF) DEFAULT_INITIALIZATION;

    if (char_length(same_site) > 0 && http_cookie_same_site_parse(same_site) == HTTP_COOKIE_SAME_SITE_UNSET) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "%s: same_site \"%s\" is not Lax/None/Strict (case-insensitive) - cookies will be built with SameSite omitted", caller, same_site);
    }

    HTTP_Cookie cookie = DEFAULT_INITIALIZATION;
    String      header = DEFAULT_INITIALIZATION;

#ifdef ARENA_IMPLEMENTATION
    if (allocator != nullptr) {
        /* A refused arena leaves the template's name EMPTY; the validation render below reads
         * that as an invalid name and refuses whole, so a false here needs no separate branch. */
        (void) http_cookie_alloc_init_2(&cookie, cookie_name, "", cookie_path, http_cookie_same_site_parse(same_site), secure, http_only, allocator);

        header = string_alloc_init_static(header_name, char_length(header_name), allocator);
    }
    else {
        cookie = http_cookie_init_2(cookie_name, "", cookie_path, http_cookie_same_site_parse(same_site), secure, http_only);
        header = string_init_static(header_name, char_length(header_name));
    }
#else
    cookie = http_cookie_init_2(cookie_name, "", cookie_path, http_cookie_same_site_parse(same_site), secure, http_only);
    header = string_init_static(header_name, char_length(header_name));
#endif // ARENA_IMPLEMENTATION

    /* string_empty(&header) also covers a refused arena copy of a non-empty header_name. */
    if (!_http_service_csrf_config_valid(caller, &cookie, _http_service_csrf_text(&header), token_byte_count, ttl)) {
        string_uninit(&header);
        http_cookie_uninit(&cookie);

        trace_log_pop();

        return false;
    }

    self->cookie            = cookie;
    self->header_name       = header;
    self->token_byte_count  = token_byte_count;
    self->ttl               = ttl;

    trace_log_pop();

    return true;
}

/*==============================================================================
 * MARK: - API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
bool http_service_csrf_alloc_init_1(HTTP_Service_CSRF *const self, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    bool const success = http_service_csrf_alloc_init_2(
        self, HTTP_SERVICE_CSRF_DEFAULT_TOKEN_BYTE_COUNT, HTTP_SERVICE_CSRF_DEFAULT_TTL, HTTP_SERVICE_CSRF_DEFAULT_COOKIE_NAME, HTTP_SERVICE_CSRF_DEFAULT_COOKIE_PATH,
        HTTP_SERVICE_CSRF_DEFAULT_HEADER_NAME, HTTP_SERVICE_CSRF_DEFAULT_SAME_SITE, HTTP_SERVICE_CSRF_DEFAULT_SECURE, HTTP_SERVICE_CSRF_DEFAULT_HTTP_ONLY, allocator);

    trace_log_pop();

    return success;
}

bool http_service_csrf_alloc_init_2(HTTP_Service_CSRF *const self,
    USize const token_byte_count, USize const ttl, char const *const cookie_name, char const *const cookie_path, char const *const header_name,
    char const *const same_site, bool const secure, bool const http_only, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "cookie_name", (void*) cookie_name);
    error_check_null(LOG_METADATA, "cookie_path", (void*) cookie_path);
    error_check_null(LOG_METADATA, "header_name", (void*) header_name);
    error_check_null(LOG_METADATA, "same_site", (void*) same_site);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    bool const success = _http_service_csrf_init(
        self, "http_service_csrf_alloc_init_2", token_byte_count, ttl, cookie_name, cookie_path, header_name, same_site, secure, http_only, allocator);

    trace_log_pop();

    return success;
}
#endif // ARENA_IMPLEMENTATION

String http_service_csrf_cookie_clear(HTTP_Service_CSRF const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* The template's own value is empty and the clear render ignores a value entirely, so the
     * stored template IS the clear cookie - nothing is built per call. */
    String value = http_cookie_clear_header_value_create_3(&self->cookie);

    _http_service_csrf_string_terminate(&value);

    trace_log_pop();

    return value;
}

String http_service_csrf_cookie_create(HTTP_Service_CSRF const *const self, char const *const token) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "token", (void*) token);

    /* An empty token is a token_create failure the caller did not check. Emitting
     * "__Host-csrf=; Max-Age=3600" would set a real cookie with no token in it - a silent
     * lockout rather than a breach, but still a cookie nobody asked for. */
    if (char_length(token) == 0) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_csrf_cookie_create: refusing - the token is empty (an unchecked http_service_csrf_token_create failure)");

        String value = _http_service_csrf_string_init(self);

        _http_service_csrf_string_terminate(&value);

        trace_log_pop();

        return value;
    }

    /* One render off the stored template - no per-response HTTP_Cookie is built, copied, or
     * uninitialized. The template is not mutated, so a shared service renders concurrently. */
    String value = DEFAULT_INITIALIZATION;

    (void) http_cookie_set_header_value_create_2(&self->cookie, token, self->ttl, &value);

    _http_service_csrf_string_terminate(&value);

    trace_log_pop();

    return value;
}

char* http_service_csrf_cookie_name_get(HTTP_Service_CSRF const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    char* const name = _http_service_csrf_text(&self->cookie.name);

    trace_log_pop();

    return name;
}

String http_service_csrf_cookie_read(HTTP_Service_CSRF const *const self, char const *const cookie_header) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    String const value = _http_service_csrf_cookie_value(self, cookie_header, cookie_header == nullptr ? 0 : char_length(cookie_header));

    trace_log_pop();

    return value;
}

String http_service_csrf_cookie_read_3(HTTP_Service_CSRF const *const self, Str const *const cookie_header) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    String const value = _http_service_csrf_cookie_value(self, cookie_header == nullptr ? nullptr : str_get_data(cookie_header), cookie_header == nullptr ? 0 : str_get_size(cookie_header));

    trace_log_pop();

    return value;
}

String http_service_csrf_cookie_read_4(HTTP_Service_CSRF const *const self, String const *const cookie_header) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    String const value = _http_service_csrf_cookie_value(
        self, cookie_header == nullptr ? nullptr : string_get_data(cookie_header), cookie_header == nullptr ? 0 : string_get_size(cookie_header));

    trace_log_pop();

    return value;
}

char* http_service_csrf_header_name_get(HTTP_Service_CSRF const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    char* const name = _http_service_csrf_text(&self->header_name);

    trace_log_pop();

    return name;
}

bool http_service_csrf_init_1(HTTP_Service_CSRF *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    bool const success = http_service_csrf_init_2(
        self, HTTP_SERVICE_CSRF_DEFAULT_TOKEN_BYTE_COUNT, HTTP_SERVICE_CSRF_DEFAULT_TTL, HTTP_SERVICE_CSRF_DEFAULT_COOKIE_NAME, HTTP_SERVICE_CSRF_DEFAULT_COOKIE_PATH,
        HTTP_SERVICE_CSRF_DEFAULT_HEADER_NAME, HTTP_SERVICE_CSRF_DEFAULT_SAME_SITE, HTTP_SERVICE_CSRF_DEFAULT_SECURE, HTTP_SERVICE_CSRF_DEFAULT_HTTP_ONLY);

    trace_log_pop();

    return success;
}

bool http_service_csrf_init_2(HTTP_Service_CSRF *const self,
    USize const token_byte_count, USize const ttl, char const *const cookie_name, char const *const cookie_path, char const *const header_name,
    char const *const same_site, bool const secure, bool const http_only) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "cookie_name", (void*) cookie_name);
    error_check_null(LOG_METADATA, "cookie_path", (void*) cookie_path);
    error_check_null(LOG_METADATA, "header_name", (void*) header_name);
    error_check_null(LOG_METADATA, "same_site", (void*) same_site);

    bool const success = _http_service_csrf_init(
        self, "http_service_csrf_init_2", token_byte_count, ttl, cookie_name, cookie_path, header_name, same_site, secure, http_only, nullptr);

    trace_log_pop();

    return success;
}

bool http_service_csrf_method_protected(char const *const method) {
    trace_log_push(LOG_METADATA);

    /* A request with no method at all is data, not a contract violation: treat it as
     * protected so the gate fails closed. */
    if (method == nullptr) {
        trace_log_pop();

        return true;
    }

    /* Exact compare, not case-folded: RFC 9110 section 9.1 method names are case-sensitive, so "get"
     * is an UNKNOWN method and must be protected, not waved through as GET. */
    bool const safe = char_compare_equal_1(method, "GET")     ||
                      char_compare_equal_1(method, "HEAD")    ||
                      char_compare_equal_1(method, "OPTIONS") ||
                      char_compare_equal_1(method, "TRACE");

    trace_log_pop();

    return !safe;
}

bool http_service_csrf_mint(HTTP_Service_CSRF const *const self, HTTP_Service_CSRF_Token *const token, String *const cookie) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "token", (void*) token);
    error_check_null(LOG_METADATA, "cookie", (void*) cookie);

    *token  = (HTTP_Service_CSRF_Token) DEFAULT_INITIALIZATION;
    *cookie = (String) DEFAULT_INITIALIZATION;

    if (!http_service_csrf_token_create(self, token)) {
        trace_log_pop();

        return false;
    }

    *cookie = http_service_csrf_cookie_create(self, string_get_data(&token->value));

    /* All-or-nothing, the shape http_service_session_mint has: a route that sent the token to the
     * page but no cookie - or a cookie the render refused - would fail every later POST with a
     * mismatch nothing logs. Release both rather than hand back half a pair. */
    if (string_empty(cookie)) {
        http_service_csrf_token_uninit(token);
        string_uninit(cookie);

        trace_log_pop();

        return false;
    }

    trace_log_pop();

    return true;
}

bool http_service_csrf_request_allowed_1(HTTP_Service_CSRF const *const self, char const *const method, char const *const token, HTTP_Service_CSRF_Token const *const stored) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (!http_service_csrf_method_protected(method)) {
        trace_log_pop();

        return true;
    }

    /* token and stored are REQUEST DATA on this path: the doc invites null when the header was
     * absent, and whether that aborts must never depend on the method. token_verify_1 refuses a
     * null `stored` itself, so there is no second null test here to drift out of step with it. */
    bool const allowed = http_service_csrf_token_verify_1(self, token, stored);

    trace_log_pop();

    return allowed;
}

bool http_service_csrf_request_allowed_2(HTTP_Service_CSRF const *const self, char const *const method, char const *const header_token, char const *const cookie_header) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (!http_service_csrf_method_protected(method)) {
        trace_log_pop();

        return true;
    }

    /* The ordinary cross-site shape is a request that carries the cookie (the browser sends it
     * unasked) and NO header, because the attacker page could not read the cookie to set one.
     * Testing the header first refuses that request before scanning and allocating; verify_2
     * would answer false on the same input anyway, one allocation later. An EMPTY Cookie header
     * is refused for the same reason and by the same one-byte test the String tier applies, so
     * the two tiers cost the same on the same request. */
    if (header_token == nullptr || char_length(header_token) == 0 || cookie_header == nullptr || cookie_header[0] == '\0') {
        trace_log_pop();

        return false;
    }

    String      cookie_value = http_service_csrf_cookie_read(self, cookie_header);
    bool const  allowed      = http_service_csrf_token_verify_2(self, header_token, string_get_data(&cookie_value));

    string_uninit(&cookie_value);

    trace_log_pop();

    return allowed;
}

bool http_service_csrf_request_allowed_4(HTTP_Service_CSRF const *const self, char const *const method, String const *const header_token, String const *const cookie_header) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (!http_service_csrf_method_protected(method)) {
        trace_log_pop();

        return true;
    }

    /* Same order as the char* tier, for the same reason: the ordinary cross-site request carries
     * the cookie and NO header, so testing the header first refuses it before scanning. A null
     * or empty Cookie String is the absent cookie the char* tier's null check already caught -
     * refusing it here is that same answer one allocation earlier. */
    if (header_token == nullptr || string_empty(header_token) || cookie_header == nullptr || string_empty(cookie_header)) {
        trace_log_pop();

        return false;
    }

    /* Both sides go to the sized core: the header token's String size, not char_length. */
    String      cookie_value = http_service_csrf_cookie_read_4(self, cookie_header);
    bool const  allowed      = _http_service_csrf_token_equal(
        string_get_data(header_token), string_get_size(header_token), string_get_data(&cookie_value), string_get_size(&cookie_value));

    string_uninit(&cookie_value);

    trace_log_pop();

    return allowed;
}

bool http_service_csrf_token_create(HTTP_Service_CSRF const *const self, HTTP_Service_CSRF_Token *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "out", (void*) out);

    *out = (HTTP_Service_CSRF_Token) DEFAULT_INITIALIZATION;

    USize const value_size = CRYPTO_RANDOM_HEX_SIZE(self->token_byte_count);

    /* ONE allocation: the CSPRNG writes its hex straight into the String own buffer. The earlier
     * shape borrowed a scratch buffer, encoded into it, copied that into a fresh String and
     * released the scratch - two allocations and a second copy of the secret per token. */
#ifdef ARENA_IMPLEMENTATION
    String value = self->cookie.allocator != nullptr ? string_alloc_init_2(value_size + CHAR_END_CHARACTER, self->cookie.allocator) : string_init_2(value_size + CHAR_END_CHARACTER);
#else
    String value = string_init_2(value_size + CHAR_END_CHARACTER);
#endif // ARENA_IMPLEMENTATION

    char *const buffer = string_get_data(&value);

    /* A refused arena answers the EMPTY String, whose data IS nullptr - that is the same
     * half-object (an expiry with no value) this signature exists to prevent. */
    if (buffer == nullptr || result_is_error(crypto_random_hex(buffer, self->token_byte_count))) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_csrf_token_create: refusing - no buffer, or the CSPRNG did not produce a token");

        string_uninit(&value);

        trace_log_pop();

        return false;
    }

    string_set_size(&value, value_size);

    out->expires_at = _http_service_csrf_expires_at(self->ttl);
    out->value      = value;

    trace_log_pop();

    return true;
}

bool http_service_csrf_token_expired(HTTP_Service_CSRF_Token const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    bool const expired = (USize) datetime_now() >= self->expires_at;

    trace_log_pop();

    return expired;
}

void http_service_csrf_token_uninit(HTTP_Service_CSRF_Token *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    string_uninit(&self->value);

    self->expires_at = 0;

    trace_log_pop();
}

bool http_service_csrf_token_verify_1(HTTP_Service_CSRF const *const self, char const *const token, HTTP_Service_CSRF_Token const *const stored) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (stored == nullptr || string_empty(&stored->value) || http_service_csrf_token_expired(stored)) {
        trace_log_pop();

        return false;
    }

    bool const success = http_service_csrf_token_verify_2(self, token, string_get_data(&stored->value));

    trace_log_pop();

    return success;
}

bool http_service_csrf_token_verify_2(HTTP_Service_CSRF const *const self, char const *const token, char const *const cookie_value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* Both sides are REQUEST DATA. An absent header and an absent cookie both arrive as null
     * or "" - refuse as a value, never abort, and never let two empties compare equal. The
     * null test comes first because char_length cannot measure a null pointer. */
    if (token == nullptr || cookie_value == nullptr) {
        trace_log_pop();

        return false;
    }

    bool const success = _http_service_csrf_token_equal(token, char_length(token), cookie_value, char_length(cookie_value));

    trace_log_pop();

    return success;
}

bool http_service_csrf_token_verify_4(HTTP_Service_CSRF const *const self, char const *const token, String const *const cookie_value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (token == nullptr || cookie_value == nullptr || string_empty(cookie_value)) {
        trace_log_pop();

        return false;
    }

    /* The cookie side is read through the String's OWN size, not char_length: this tier's whole
     * point is a String the server handed over, and a sized String over a longer terminated
     * buffer must compare the bytes it claims. */
    bool const success = _http_service_csrf_token_equal(token, char_length(token), string_get_data(cookie_value), string_get_size(cookie_value));

    trace_log_pop();

    return success;
}

void http_service_csrf_uninit(HTTP_Service_CSRF *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    http_cookie_uninit(&self->cookie);
    string_uninit(&self->header_name);

    self->token_byte_count  = 0;
    self->ttl               = 0;

    trace_log_pop();
}