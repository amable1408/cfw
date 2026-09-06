#include <http/cookie/cookie.h>

#define _HTTP_COOKIE_PREFIX_HOST "__Host-"
#define _HTTP_COOKIE_PREFIX_SECURE "__Secure-"

/*==============================================================================
 * MARK: - Helpers
 *============================================================================*/

static void _http_cookie_string_add(String *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    USize const data_size = char_length(data);

    if (data_size > 0) {
        string_add_last_2(self, data, data_size);
    }

    trace_log_pop();
}

static void _http_cookie_string_add_2(String *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    if (data_size > 0) {
        string_add_last_2(self, data, data_size);
    }

    trace_log_pop();
}

static String _http_cookie_string_init(Arena *const allocator) {
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    if (!memory_empty(allocator)) {
        String const string = string_alloc_init_1(allocator);

        trace_log_pop();

        return string;
    }
#endif // ARENA_IMPLEMENTATION

    String const string = string_init_1();

    trace_log_pop();

    return string;
}

static String _http_cookie_char_to_string(char const *const data, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    USize const data_size = char_length(data);

    if (data_size == 0) {
#ifdef ARENA_IMPLEMENTATION
        if (!memory_empty(allocator)) {
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
    if (!memory_empty(allocator)) {
        String const string = string_alloc_init_static(data, data_size, allocator);

        trace_log_pop();

        return string;
    }
#endif // ARENA_IMPLEMENTATION

    String const string = string_init_static(data, data_size);

    trace_log_pop();

    return string;
}

static void _http_cookie_string_set(String *const self, char const *const data, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    string_uninit(self);

    *self = _http_cookie_char_to_string(data, allocator);

    trace_log_pop();
}

static void _http_cookie_number_add(String *const self, USize const number) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* Stack buffer, not char_new_from_numbers_uint_1 + char_delete: a decimal USize is at most
     * 20 digits, so a per-call heap round trip buys nothing here (matches headers.c). */
    char buffer[21] = DEFAULT_INITIALIZATION;

    char_from_numbers_uint_1(buffer, sizeof(buffer), number);

    _http_cookie_string_add(self, buffer);

    trace_log_pop();
}

static void _http_cookie_attribute_add(String *const header, char const *const name, String const *const value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "header", (void*) header);
    error_check_null(LOG_METADATA, "name", (void*) name);
    error_check_null(LOG_METADATA, "value", (void*) value);

    if (string_empty(value)) {
        trace_log_pop();

        return;
    }

    _http_cookie_string_add(header, "; ");
    _http_cookie_string_add(header, name);
    _http_cookie_string_add(header, "=");
    string_add_last_4(header, value);

    trace_log_pop();
}

static void _http_cookie_attribute_add_text(String *const header, char const *const name, char const *const text) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "header", (void*) header);
    error_check_null(LOG_METADATA, "name", (void*) name);
    error_check_null(LOG_METADATA, "text", (void*) text);

    if (char_length(text) == 0) {
        trace_log_pop();

        return;
    }

    _http_cookie_string_add(header, "; ");
    _http_cookie_string_add(header, name);
    _http_cookie_string_add(header, "=");
    _http_cookie_string_add(header, text);

    trace_log_pop();
}

static char* _http_cookie_same_site_text(HTTP_Cookie_Same_Site const same_site) {
    switch (same_site) {
        case HTTP_COOKIE_SAME_SITE_LAX:    return "Lax";
        case HTTP_COOKIE_SAME_SITE_NONE:   return "None";
        case HTTP_COOKIE_SAME_SITE_STRICT: return "Strict";
        default:                           return "";
    }
}

/* RFC 6265/2616 cookie-name token: 1*<any CHAR except CTL or separator>. */
static bool _http_cookie_name_valid(char const *const data, USize const data_size) {
    if (data_size == 0) {
        return false;
    }

    for (USize i = 0; i < data_size; i += 1) {
        U8 const byte = (U8) data[i];

        if (byte <= 0x20 || byte == 0x7F) {
            return false;
        }

        switch (byte) {
            case '(': case ')': case '<': case '>': case '@':
            case ',': case ';': case ':': case '\\': case '"':
            case '/': case '[': case ']': case '?':  case '=':
            case '{': case '}':
                return false;
            default:
                break;
        }
    }

    return true;
}

/* cookie-octet: %x21 / %x23-2B / %x2D-3A / %x3C-5B / %x5D-7E. */
static bool _http_cookie_octet_valid(U8 const byte) {
    if (byte == 0x21) {
        return true;
    }

    if (byte >= 0x23 && byte <= 0x2B) {
        return true;
    }

    if (byte >= 0x2D && byte <= 0x3A) {
        return true;
    }

    if (byte >= 0x3C && byte <= 0x5B) {
        return true;
    }

    return byte >= 0x5D && byte <= 0x7E;
}

/* cookie-value = *cookie-octet / ( DQUOTE *cookie-octet DQUOTE ). */
static bool _http_cookie_value_valid(char const *const data, USize const data_size) {
    if (data_size == 0) {
        return true;
    }

    USize start = 0;
    USize end   = data_size;

    if (data_size >= 2 && data[0] == '"' && data[data_size - 1] == '"') {
        start = 1;
        end   = data_size - 1;
    }

    for (USize i = start; i < end; i += 1) {
        if (!_http_cookie_octet_valid((U8) data[i])) {
            return false;
        }
    }

    return true;
}

/* Path/Domain: no CTL, no ';', no ',' (a bare attribute value is otherwise free text). */
static bool _http_cookie_attribute_valid(char const *const data, USize const data_size) {
    for (USize i = 0; i < data_size; i += 1) {
        U8 const byte = (U8) data[i];

        if (byte <= 0x1F || byte == 0x7F || byte == ';' || byte == ',') {
            return false;
        }
    }

    return true;
}

/* A cookie-name prefix test that reads the String's bytes directly: the name is not required to
 * be NUL-terminated here, so char_starts_with_1 is not usable. */
static bool _http_cookie_name_has_prefix(String const *const name, char const *const prefix, USize const prefix_size) {
    USize const name_size = string_get_size(name);

    if (name_size < prefix_size) {
        return false;
    }

    return char_compare_equal_2(string_get_data(name), prefix_size, prefix, prefix_size);
}

/*
 * Every rule whose breach makes a browser DROP the cookie without a word, checked in one place.
 *
 * The `__Host-` / `__Secure-` prefix rules live here rather than in each caller because they are
 * the same class as SameSite=None-without-Secure directly below them: a header this module would
 * happily render and no browser would keep. http/service/session and http/service/csrf each
 * carried their own copy of the two tests before this; a direct HTTP_Cookie user had neither.
 *
 * `value_data` may be nullptr with a `value_size` of 0 - the clear renderer emits no value at all.
 */
static bool _http_cookie_header_fields_valid(
    char const *const caller, String const *const name, char const *const value_data, USize const value_size, String const *const path, String const *const domain,
    HTTP_Cookie_Same_Site const same_site, bool const secure, bool const partitioned) {
    if (!_http_cookie_name_valid(string_get_data(name), string_get_size(name))) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "%s: refusing - the cookie name is not a valid token", caller);

        return false;
    }

    if (!_http_cookie_value_valid(value_data, value_size)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "%s: refusing - the cookie value has bytes outside cookie-octet", caller);

        return false;
    }

    if (_http_cookie_name_has_prefix(name, _HTTP_COOKIE_PREFIX_HOST, CHAR_STATIC_SIZE(_HTTP_COOKIE_PREFIX_HOST)) &&
        (!secure || string_get_size(path) != 1 || string_get_data(path)[0] != '/')) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "%s: refusing - the `__Host-` name prefix requires Secure and Path=\"/\" (browsers drop the cookie otherwise)", caller);

        return false;
    }

    if (_http_cookie_name_has_prefix(name, _HTTP_COOKIE_PREFIX_SECURE, CHAR_STATIC_SIZE(_HTTP_COOKIE_PREFIX_SECURE)) && !secure) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "%s: refusing - the `__Secure-` name prefix requires Secure (browsers drop the cookie otherwise)", caller);

        return false;
    }

    if (!_http_cookie_attribute_valid(string_get_data(path), string_get_size(path))) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "%s: refusing - Path contains a control byte, ';', or ','", caller);

        return false;
    }

    if (!_http_cookie_attribute_valid(string_get_data(domain), string_get_size(domain))) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "%s: refusing - Domain contains a control byte, ';', or ','", caller);

        return false;
    }

    if (same_site == HTTP_COOKIE_SAME_SITE_NONE && !secure) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "%s: refusing - SameSite=None requires Secure", caller);

        return false;
    }

    if (partitioned && !secure) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "%s: refusing - Partitioned requires Secure", caller);

        return false;
    }

    return true;
}

static String _http_cookie_clear_header_create(char const *const name, char const *const path, char const *const domain, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "name", (void*) name);
    error_check_null(LOG_METADATA, "path", (void*) path);
    error_check_null(LOG_METADATA, "domain", (void*) domain);

    if (!_http_cookie_name_valid(name, char_length(name)) ||
        !_http_cookie_attribute_valid(path, char_length(path)) ||
        !_http_cookie_attribute_valid(domain, char_length(domain))) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_cookie_clear_header_create: refusing - invalid name, path, or domain");

        String const empty = _http_cookie_string_init(allocator);

        trace_log_pop();

        return empty;
    }

    String header = _http_cookie_string_init(allocator);

    _http_cookie_string_add(&header, "Set-Cookie: ");
    _http_cookie_string_add(&header, name);
    _http_cookie_string_add(&header, "=");

    /* An empty Path is omitted, not emitted as a bare "Path=" - consistent with
     * clear_header_create_3's _http_cookie_attribute_add, which skips an empty value. */
    if (char_length(path) > 0) {
        _http_cookie_string_add(&header, "; Path=");
        _http_cookie_string_add(&header, path);
    }

    if (char_length(domain) > 0) {
        _http_cookie_string_add(&header, "; Domain=");
        _http_cookie_string_add(&header, domain);
    }

    _http_cookie_string_add(&header, "; Max-Age=0");
    _http_cookie_string_add(&header, "; Expires=Thu, 01 Jan 1970 00:00:00 GMT");
    _http_cookie_string_add(&header, "; Secure; HttpOnly; SameSite=Lax\r\n");

    trace_log_pop();

    return header;
}

/*
 * The two Set-Cookie renderers, written once each and reachable in two spellings.
 *
 * `header_line` true emits the whole response header line - "Set-Cookie: " ... CRLF.
 * `header_line` false emits only the bare VALUE ("sid=abc; Path=/; ..."), which is what a
 * caller hands to http_server_response_header_add("Set-Cookie", value). http/service/session
 * and http/service/csrf each carried a private copy that rendered the full line and then
 * sliced CHAR_STATIC_SIZE("Set-Cookie: ") and the CRLF back off - arithmetic coupled to this
 * module's exact spelling, in two places, with a refused (EMPTY) render collapsing silently
 * to "". Both now call the value forms directly.
 *
 * A rendered value needs no explicit terminator: string_add_2 writes the NUL after every
 * append. A refusal answers the EMPTY String, whose data IS nullptr - callers gate on
 * string_empty, exactly as they already do for the header-line forms.
 */
static String _http_cookie_clear_render_3(HTTP_Cookie const *const self, bool const header_line) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (!_http_cookie_header_fields_valid(
            header_line ? "http_cookie_clear_header_create_3" : "http_cookie_clear_header_value_create_3", &self->name, nullptr, 0, &self->path, &self->domain, self->same_site,
            self->secure, self->partitioned)) {
        String const empty = _http_cookie_string_init(self->allocator);

        trace_log_pop();

        return empty;
    }

    String header = _http_cookie_string_init(self->allocator);

    if (header_line) {
        _http_cookie_string_add(&header, "Set-Cookie: ");
    }

    string_add_last_4(&header, &self->name);
    _http_cookie_string_add(&header, "=");
    _http_cookie_attribute_add(&header, "Path", &self->path);
    _http_cookie_attribute_add(&header, "Domain", &self->domain);
    _http_cookie_string_add(&header, "; Max-Age=0");
    _http_cookie_string_add(&header, "; Expires=Thu, 01 Jan 1970 00:00:00 GMT");

    if (self->secure) {
        _http_cookie_string_add(&header, "; Secure");
    }

    if (self->http_only) {
        _http_cookie_string_add(&header, "; HttpOnly");
    }

    _http_cookie_attribute_add_text(&header, "SameSite", _http_cookie_same_site_text(self->same_site));

    if (self->partitioned) {
        _http_cookie_string_add(&header, "; Partitioned");
    }

    if (header_line) {
        _http_cookie_string_add(&header, "\r\n");
    }

    trace_log_pop();

    return header;
}

/*
 * The set renderer proper: `self` supplies the name, Path, Domain and flags, while the VALUE and
 * the Max-Age are passed in. http_cookie_set_header_value_create_2 uses that to render one
 * template with a substitute value, so a per-response cookie (a session mint, a CSRF token) costs
 * no copy of the name/path/value at all - the two services each used to build, render and then
 * uninitialize a throwaway HTTP_Cookie per response.
 */
static String _http_cookie_set_render_fields(HTTP_Cookie const *const self,
    char const *const caller, char const *const value_data, USize const value_size, bool const has_max_age, USize const max_age, bool const header_line) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "caller", (void*) caller);

    if (!_http_cookie_header_fields_valid(caller, &self->name, value_data, value_size, &self->path, &self->domain, self->same_site, self->secure, self->partitioned)) {
        String const empty = _http_cookie_string_init(self->allocator);

        trace_log_pop();

        return empty;
    }

    String header = _http_cookie_string_init(self->allocator);

    if (header_line) {
        _http_cookie_string_add(&header, "Set-Cookie: ");
    }

    string_add_last_4(&header, &self->name);
    _http_cookie_string_add(&header, "=");
    _http_cookie_string_add_2(&header, value_data == nullptr ? "" : value_data, value_size);
    _http_cookie_attribute_add(&header, "Path", &self->path);
    _http_cookie_attribute_add(&header, "Domain", &self->domain);

    if (has_max_age) {
        _http_cookie_string_add(&header, "; Max-Age=");
        _http_cookie_number_add(&header, max_age);
    }

    if (self->secure) {
        _http_cookie_string_add(&header, "; Secure");
    }

    if (self->http_only) {
        _http_cookie_string_add(&header, "; HttpOnly");
    }

    _http_cookie_attribute_add_text(&header, "SameSite", _http_cookie_same_site_text(self->same_site));

    if (self->partitioned) {
        _http_cookie_string_add(&header, "; Partitioned");
    }

    if (header_line) {
        _http_cookie_string_add(&header, "\r\n");
    }

    trace_log_pop();

    return header;
}

static String _http_cookie_set_render(HTTP_Cookie const *const self, bool const header_line) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    String const header = _http_cookie_set_render_fields(
        self, header_line ? "http_cookie_set_header_create" : "http_cookie_set_header_value_create", string_get_data(&self->value), string_get_size(&self->value), self->has_max_age,
        self->max_age, header_line);

    trace_log_pop();

    return header;
}

static String _http_cookie_get(char const *const header, USize const header_size, char const *const name, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "header", (void*) header);
    error_check_null(LOG_METADATA, "name", (void*) name);

    String      value       = _http_cookie_string_init(allocator);
    USize const name_size   = char_length(name);

    /* An empty name names no cookie - it is a value's own text ("; =1" is a pair with an empty
     * name, not a name-less value) - so refuse it as a miss before scanning rather than let it
     * match the first such pair. */
    if (name_size == 0) {
        trace_log_pop();

        return value;
    }

    USize index = 0;

    while (index < header_size) {
        while (index < header_size && (header[index] == ' ' || header[index] == '\t' || header[index] == ';')) {
            index += 1;
        }

        USize const key_start = index;

        while (index < header_size && header[index] != '=' && header[index] != ';') {
            index += 1;
        }

        USize const key_size = index - key_start;

        if (index >= header_size || header[index] != '=') {
            while (index < header_size && header[index] != ';') {
                index += 1;
            }

            continue;
        }

        index += 1;

        USize const value_start = index;

        while (index < header_size && header[index] != ';') {
            index += 1;
        }

        USize   const value_size = index - value_start;
        bool    const success    = char_compare_equal_comptime_2(header + key_start, key_size, name, name_size);

        if (success) {
            _http_cookie_string_add_2(&value, header + value_start, value_size);

            trace_log_pop();

            return value;
        }
    }

    trace_log_pop();

    return value;
}

static HTTP_Cookie _http_cookie_init(
    char const *const name, char const *const value, char const *const path, HTTP_Cookie_Same_Site const same_site, bool const secure, bool const http_only, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "name", (void*) name);
    error_check_null(LOG_METADATA, "value", (void*) value);
    error_check_null(LOG_METADATA, "path", (void*) path);

    HTTP_Cookie const cookie = {
        .allocator      = allocator,
        .domain         = _http_cookie_string_init(allocator),
        .has_max_age    = false,
        .http_only      = http_only,
        .max_age        = 0,
        .name           = _http_cookie_char_to_string(name, allocator),
        .partitioned    = false,
        .path           = _http_cookie_char_to_string(path, allocator),
        .same_site      = same_site,
        .secure         = secure,
        .value          = _http_cookie_char_to_string(value, allocator)
    };

    trace_log_pop();

    return cookie;
}

/*==============================================================================
 * MARK: - API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
String http_cookie_alloc_clear_header_create_1(char const *const name, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "name", (void*) name);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    String const header = http_cookie_alloc_clear_header_create_2(name, "/", "", allocator);

    trace_log_pop();

    return header;
}

String http_cookie_alloc_clear_header_create_2(char const *const name, char const *const path, char const *const domain, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "name", (void*) name);
    error_check_null(LOG_METADATA, "path", (void*) path);
    error_check_null(LOG_METADATA, "domain", (void*) domain);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    String const header = _http_cookie_clear_header_create(name, path, domain, allocator);

    trace_log_pop();

    return header;
}

String http_cookie_alloc_get_1(char const *const header, char const *const name, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "header", (void*) header);
    error_check_null(LOG_METADATA, "name", (void*) name);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    String const value = _http_cookie_get(header, char_length(header), name, allocator);

    trace_log_pop();

    return value;
}

String http_cookie_alloc_get_2(char const *const header, USize const header_size, char const *const name, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "header", (void*) header);
    error_check_null(LOG_METADATA, "name", (void*) name);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    String const value = _http_cookie_get(header, header_size, name, allocator);

    trace_log_pop();

    return value;
}

bool http_cookie_alloc_init_1(HTTP_Cookie *const self, char const *const name, char const *const value, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    bool const success = http_cookie_alloc_init_2(self, name, value, "/", HTTP_COOKIE_SAME_SITE_LAX, true, true, allocator);

    trace_log_pop();

    return success;
}

bool http_cookie_alloc_init_2(HTTP_Cookie *const self,
    char const *const name, char const *const value, char const *const path, HTTP_Cookie_Same_Site const same_site, bool const secure, bool const http_only, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    HTTP_Cookie cookie = _http_cookie_init(name, value, path, same_site, secure, http_only, allocator);

    bool const name_ok  = char_length(name)  == 0 || string_get_data(&cookie.name)  != nullptr;
    bool const value_ok = char_length(value) == 0 || string_get_data(&cookie.value) != nullptr;
    bool const path_ok  = char_length(path)  == 0 || string_get_data(&cookie.path)  != nullptr;

    if (!name_ok || !value_ok || !path_ok) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_cookie_alloc_init_2: refusing whole - the arena rejected a copy");

        http_cookie_uninit(&cookie);

        *self = (HTTP_Cookie) DEFAULT_INITIALIZATION;

        trace_log_pop();

        return false;
    }

    *self = cookie;

    trace_log_pop();

    return true;
}
#endif // ARENA_IMPLEMENTATION

String http_cookie_clear_header_create_1(char const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "name", (void*) name);

    String const header = http_cookie_clear_header_create_2(name, "/", "");

    trace_log_pop();

    return header;
}

String http_cookie_clear_header_create_2(char const *const name, char const *const path, char const *const domain) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "name", (void*) name);
    error_check_null(LOG_METADATA, "path", (void*) path);
    error_check_null(LOG_METADATA, "domain", (void*) domain);

    String const header = _http_cookie_clear_header_create(name, path, domain, nullptr);

    trace_log_pop();

    return header;
}

String http_cookie_clear_header_create_3(HTTP_Cookie const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    String const header = _http_cookie_clear_render_3(self, true);

    trace_log_pop();

    return header;
}

String http_cookie_clear_header_value_create_3(HTTP_Cookie const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    String const value = _http_cookie_clear_render_3(self, false);

    trace_log_pop();

    return value;
}

void http_cookie_domain_set(HTTP_Cookie *const self, char const *const domain) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "domain", (void*) domain);

    _http_cookie_string_set(&self->domain, domain, self->allocator);

    trace_log_pop();
}

String http_cookie_get_1(char const *const header, char const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "header", (void*) header);
    error_check_null(LOG_METADATA, "name", (void*) name);

    String const value = _http_cookie_get(header, char_length(header), name, nullptr);

    trace_log_pop();

    return value;
}

String http_cookie_get_2(char const *const header, USize const header_size, char const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "header", (void*) header);
    error_check_null(LOG_METADATA, "name", (void*) name);

    String const value = _http_cookie_get(header, header_size, name, nullptr);

    trace_log_pop();

    return value;
}

void http_cookie_has_max_age_set(HTTP_Cookie *const self, bool const enabled) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    self->has_max_age = enabled;

    trace_log_pop();
}

void http_cookie_http_only_set(HTTP_Cookie *const self, bool const enabled) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    self->http_only = enabled;

    trace_log_pop();
}

HTTP_Cookie http_cookie_init_1(char const *const name, char const *const value) {
    trace_log_push(LOG_METADATA);

    HTTP_Cookie const cookie = http_cookie_init_2(name, value, "/", HTTP_COOKIE_SAME_SITE_LAX, true, true);

    trace_log_pop();

    return cookie;
}

HTTP_Cookie http_cookie_init_2(char const *const name, char const *const value, char const *const path, HTTP_Cookie_Same_Site const same_site, bool const secure, bool const http_only) {
    trace_log_push(LOG_METADATA);

    HTTP_Cookie const cookie = _http_cookie_init(name, value, path, same_site, secure, http_only, nullptr);

    trace_log_pop();

    return cookie;
}

void http_cookie_max_age_set(HTTP_Cookie *const self, USize const max_age) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    self->has_max_age   = true;
    self->max_age       = max_age;

    trace_log_pop();
}

void http_cookie_partitioned_set(HTTP_Cookie *const self, bool const enabled) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    self->partitioned = enabled;

    trace_log_pop();
}

void http_cookie_path_set(HTTP_Cookie *const self, char const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "path", (void*) path);

    _http_cookie_string_set(&self->path, path, self->allocator);

    trace_log_pop();
}

HTTP_Cookie_Same_Site http_cookie_same_site_parse(char const *const text) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "text", (void*) text);

    USize const size = char_length(text);

    /* Case-insensitive: browsers match SameSite values on the wire the same way,
     * and this parse feeds session.c/csrf.c's config text - a lowercase "lax" must not silently
     * fall through to UNSET. */
    if (char_compare_iequal_comptime_2(text, size, "Lax", 3)) {
        trace_log_pop();

        return HTTP_COOKIE_SAME_SITE_LAX;
    }

    if (char_compare_iequal_comptime_2(text, size, "None", 4)) {
        trace_log_pop();

        return HTTP_COOKIE_SAME_SITE_NONE;
    }

    if (char_compare_iequal_comptime_2(text, size, "Strict", 6)) {
        trace_log_pop();

        return HTTP_COOKIE_SAME_SITE_STRICT;
    }

    trace_log_pop();

    return HTTP_COOKIE_SAME_SITE_UNSET;
}

void http_cookie_same_site_set(HTTP_Cookie *const self, HTTP_Cookie_Same_Site const same_site) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    self->same_site = same_site;

    trace_log_pop();
}

void http_cookie_secure_set(HTTP_Cookie *const self, bool const enabled) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    self->secure = enabled;

    trace_log_pop();
}

String http_cookie_set_header_create(HTTP_Cookie const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    String const header = _http_cookie_set_render(self, true);

    trace_log_pop();

    return header;
}

String http_cookie_set_header_value_create(HTTP_Cookie const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    String const value = _http_cookie_set_render(self, false);

    trace_log_pop();

    return value;
}

bool http_cookie_set_header_value_create_2(HTTP_Cookie const *const template, char const *const value, USize const max_age, String *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "template", (void*) template);
    error_check_null(LOG_METADATA, "value", (void*) value);
    error_check_null(LOG_METADATA, "out", (void*) out);

    *out = _http_cookie_set_render_fields(template, "http_cookie_set_header_value_create_2", value, char_length(value), true, max_age, false);

    bool const success = !string_empty(out);

    trace_log_pop();

    return success;
}

void http_cookie_uninit(HTTP_Cookie *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    string_uninit(&self->domain);
    string_uninit(&self->name);
    string_uninit(&self->path);
    string_uninit(&self->value);

    self->allocator     = nullptr;
    self->has_max_age   = false;
    self->http_only     = false;
    self->max_age       = 0;
    self->partitioned   = false;
    self->same_site     = HTTP_COOKIE_SAME_SITE_UNSET;
    self->secure        = false;

    trace_log_pop();
}

void http_cookie_value_set(HTTP_Cookie *const self, char const *const value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "value", (void*) value);

    _http_cookie_string_set(&self->value, value, self->allocator);

    trace_log_pop();
}