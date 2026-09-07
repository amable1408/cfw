#include <http/service/cors/cors.h>

/* Implementation dependencies: cors.h's API names none of them. allocator arrives only through
 * cors.h -> al_str.h -> str.h -> char.h -> allocator.h, an ACCIDENTAL chain in which no link
 * names an Allocator. See style_guidelines.md, "Avoid Redundant Chained Includes". */
#include <allocator/allocator.h>
#include <char/char.h>
#include <http/headers/headers.h>

/*==============================================================================
 * MARK: - Helpers
 *============================================================================*/

/* An origin is not a token ("https://host:8080"), so it gets its own rule, and it is the
 * one cors.h and the WARN both spell out: a space, a comma, a control byte (CR and LF
 * included), DEL, or any byte above 0x7E is refused. A stored entry can then never carry a
 * header break, whatever a configuration file holds. */
static bool _http_service_cors_origin_valid(char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    bool valid = data_size > 0;

    for (USize i = 0; valid && i < data_size; i += 1) {
        unsigned char const value = (unsigned char) data[i];

        valid = value > 0x20 && value < 0x7F && value != ',';
    }

    trace_log_pop();

    return valid;
}

static Str* _http_service_cors_list_find(AL_Str const *const list, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "list", (void*) list);
    error_check_null(LOG_METADATA, "data", (void*) data);

    if (data_size == 0) {
        trace_log_pop();

        return nullptr;
    }

    Str *found = nullptr;

    for (USize i = 0; i < al_str_get_size(list); i += 1) {
        Str *const item = al_str_at(list, i);

        if (char_compare_iequal_2(str_get_data(item), str_get_size(item), (char*) data, data_size)) {
            found = item;

            break;
        }
    }

    trace_log_pop();

    return found;
}

static Str _http_service_cors_char_to_str(HTTP_Service_CORS const *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

#ifdef ARENA_IMPLEMENTATION
    if (self->allocator != nullptr) {
        /* str_alloc_init_static's own copy goes through allocator_borrow, which ENDS THE
         * PROCESS on an arena that cannot meet the request - so a configuration list one
         * entry too long for its arena would abort instead of refusing. The same tail is
         * built here over the try_ form: borrow, copy, take ownership. */
        char *const buffer = (char*) allocator_try_borrow(data_size + CHAR_END_CHARACTER, self->allocator);

        if (buffer == nullptr) {
            Str const empty = str_alloc_init_1(self->allocator);

            trace_log_pop();

            return empty;
        }

        char_copy_3(buffer, data_size + CHAR_END_CHARACTER, data, data_size);

        Str str = str_alloc_init_3(buffer, data_size, self->allocator);

        str.owned = true;

        trace_log_pop();

        return str;
    }
#endif // ARENA_IMPLEMENTATION

    Str const str = str_init_static(data, data_size);

    trace_log_pop();

    return str;
}

/* An EMPTY value is a legal value, answered false. It used to reach
 * error_check_non_value_uint, which ends the process - a configuration string read from a
 * file, or a request value handed straight through, could then abort the server. */
static bool _http_service_cors_list_add(HTTP_Service_CORS *const self, AL_Str *const list, char const *const data, USize const data_size, bool const lowercase) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "list", (void*) list);
    error_check_null(LOG_METADATA, "data", (void*) data);

    if (data_size == 0) {
        trace_log_pop();

        return false;
    }

    /* Already present: the postcondition already holds, so this is success. */
    if (_http_service_cors_list_find(list, data, data_size) != nullptr) {
        trace_log_pop();

        return true;
    }

    Str temp = _http_service_cors_char_to_str(self, data, data_size);

    /* The arena path degrades to the EMPTY Str when it refuses the copy, and an empty
     * entry in a CORS list is not inert: the lookup compares sizes first, so it would sit
     * there matching nothing while looking like policy. */
    if (str_get_size(&temp) != data_size) {
        str_uninit(&temp);

        trace_log_pop();

        return false;
    }

    if (lowercase) {
        char_lower_2(str_get_data(&temp), str_get_size(&temp));
    }

    USize const stored_before = al_str_get_size(list);

    al_str_add_last(list, &temp);

    /* add_last DECLINES rather than growing when the allocator refuses, and reports it
     * by leaving the size alone. `temp` owns a fresh COPY of `data`, so a dropped node
     * puts that copy beyond the list's uninit. */
    if (al_str_get_size(list) == stored_before) {
        str_uninit(&temp);

        trace_log_pop();

        return false;
    }

    trace_log_pop();

    return true;
}

/* Rendered ONCE per configuration change, not once per request: the preflight tail used to
 * re-walk three lists and heap-allocate the max-age digits on every OPTIONS. */
static bool _http_service_cors_render_list_line(String *const line, char const *const name, AL_Str const *const list) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "line", (void*) line);
    error_check_null(LOG_METADATA, "name", (void*) name);
    error_check_null(LOG_METADATA, "list", (void*) list);

    string_clear(line);

    if (al_str_get_size(list) == 0) {
        trace_log_pop();

        return true;
    }

    /* Counted BEFORE a byte is appended, because a CRLF test cannot see a hole in the
     * middle: string_add_2 refuses a growth WHOLLY, so a refused NAME followed by item
     * appends that fit the existing slack yields "GET, POST\r\n" - nameless, yet
     * CRLF-terminated, and every trailing-byte guard passes it. */
    USize expected = char_length(name) + CHAR_STATIC_SIZE("\r\n");

    for (USize i = 0; i < al_str_get_size(list); i += 1) {
        expected += str_get_size(al_str_at(list, i)) + (i > 0 ? CHAR_STATIC_SIZE(", ") : 0);
    }

    string_add_last_1(line, name);

    for (USize i = 0; i < al_str_get_size(list); i += 1) {
        if (i > 0) {
            string_add_last_1(line, ", ");
        }

        Str const *const item = al_str_at(list, i);

        string_add_last_2(line, str_get_data(item), str_get_size(item));
    }

    string_add_last_1(line, "\r\n");

    /* The size comparison subsumes the CRLF test this used to also run: an equal size means
     * every append landed, the trailing "\r\n" included. */
    if (string_get_size(line) != expected) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_cors: the allocator refused a policy line - %s is now omitted", name);

        string_clear(line);

        trace_log_pop();

        return false;
    }

    trace_log_pop();

    return true;
}

static bool _http_service_cors_render_max_age_line(HTTP_Service_CORS *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    string_clear(&self->max_age_line);

    char *const digits = char_new_from_numbers_uint_1(self->max_age);

    /* A null here used to be handed straight to an error_check_null that ends the process.
     * The Max-Age line is an OPTIONAL cache hint: skipping it costs a repeated preflight,
     * which is the correct degradation. */
    if (digits == nullptr) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_cors: max-age digits could not be rendered - Access-Control-Max-Age is omitted");

        trace_log_pop();

        return false;
    }

    USize const expected = CHAR_STATIC_SIZE("Access-Control-Max-Age: ") + char_length(digits) + CHAR_STATIC_SIZE("\r\n");

    string_add_last_1(&self->max_age_line, "Access-Control-Max-Age: ");
    string_add_last_1(&self->max_age_line, digits);
    string_add_last_1(&self->max_age_line, "\r\n");

    char_delete(digits);

    /* Size-accounted for the same reason the list lines are: a refused NAME with an
     * accepted tail still ends in CRLF, so the size is the only test worth running. */
    if (string_get_size(&self->max_age_line) != expected) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_cors: the allocator refused the max-age line - Access-Control-Max-Age is omitted");

        string_clear(&self->max_age_line);

        trace_log_pop();

        return false;
    }

    trace_log_pop();

    return true;
}

static bool _http_service_cors_header_allowed_2(HTTP_Service_CORS const *const self, char const *const header, USize const header_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "header", (void*) header);

    bool const allowed = self->headers_wildcard || _http_service_cors_list_find(&self->headers, header, header_size) != nullptr;

    trace_log_pop();

    return allowed;
}

/* One element of a comma-separated header list, with optional whitespace (space AND HTAB,
 * both legal around a list element) trimmed off either end. */
static USize _http_service_cors_element_next(char const *const data, USize const data_size, USize const cursor, USize *const start, USize *const stop) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_null(LOG_METADATA, "start", (void*) start);
    error_check_null(LOG_METADATA, "stop", (void*) stop);

    USize index = cursor;

    while (index < data_size && (data[index] == ' ' || data[index] == '\t' || data[index] == ',')) {
        index += 1;
    }

    *start = index;

    while (index < data_size && data[index] != ',') {
        index += 1;
    }

    *stop = index;

    while (*stop > *start && (data[*stop - 1] == ' ' || data[*stop - 1] == '\t')) {
        *stop -= 1;
    }

    trace_log_pop();

    return index;
}

/* Under credentials a stored "*" cannot be emitted: browsers read it as a header literally
 * NAMED "*" and fail the preflight. The request's own list is echoed instead - after every
 * element is proven a token, so nothing a client sends can reach the header block. */
static bool _http_service_cors_reflect_headers(String *const block, char const *const headers, USize const headers_size, USize *const expected) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "block", (void*) block);
    error_check_null(LOG_METADATA, "headers", (void*) headers);
    error_check_null(LOG_METADATA, "expected", (void*) expected);

    USize cursor    = 0;
    USize elements  = 0;

    *expected = 0;

    /* Validated WHOLE before a byte is emitted: a one-pass version that refused halfway
     * through would leave a truncated "Access-Control-Allow-Headers: X-A" - no CRLF - in the
     * middle of the block it was building. The same pass totals the bytes the write pass
     * owes, so the caller can tell a WRITTEN line from an allocator-dropped one. */
    while (cursor < headers_size) {
        USize start = 0;
        USize stop  = 0;

        cursor = _http_service_cors_element_next(headers, headers_size, cursor, &start, &stop);

        if (stop == start) {
            continue;
        }

        if (!http_headers_token_valid_2(headers + start, stop - start)) {
            *expected = 0;

            trace_log_pop();

            return false;
        }

        *expected += (stop - start) + (elements > 0 ? CHAR_STATIC_SIZE(", ") : CHAR_STATIC_SIZE("Access-Control-Allow-Headers: "));

        elements += 1;
    }

    if (elements > 0) {
        *expected += CHAR_STATIC_SIZE("\r\n");
    }

    USize written = 0;

    cursor = 0;

    while (cursor < headers_size && written < elements) {
        USize start = 0;
        USize stop  = 0;

        cursor = _http_service_cors_element_next(headers, headers_size, cursor, &start, &stop);

        if (stop == start) {
            continue;
        }

        string_add_last_1(block, written == 0 ? "Access-Control-Allow-Headers: " : ", ");

        string_add_last_2(block, (char*) headers + start, stop - start);

        written += 1;
    }

    if (written > 0) {
        string_add_last_1(block, "\r\n");
    }

    trace_log_pop();

    return true;
}

/* Emits Vary first, allowed or not: a shared cache that stored the CORS-less refusal would
 * otherwise serve it to an origin the policy does allow. The reflected origin is the STORED
 * entry, never the request's own bytes.
 *
 * `expected` answers the byte total this call OWES the block - counted before a byte is
 * appended, and on the REFUSAL path too, since the Vary that path writes is the whole point
 * of writing it. Both callers compare it, so the header's "every line is SIZE-ACCOUNTED"
 * sentence covers these three lines as well: a dropped Vary beside a written Allow-Origin
 * is exactly the cache-unsafe shape emitting Vary on refusals was meant to close, and a
 * dropped Allow-Credentials silently downgrades a credentialed policy. */
static bool _http_service_cors_origin_add_header(HTTP_Service_CORS const *const self, String *const headers,
    char const *const origin, USize const origin_size, bool const preflight, USize *const expected) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "headers", (void*) headers);
    error_check_null(LOG_METADATA, "origin", (void*) origin);
    error_check_null(LOG_METADATA, "expected", (void*) expected);

    char const *const vary = self->origins_wildcard ?
        (preflight ? "Vary: Access-Control-Request-Method, Access-Control-Request-Headers\r\n" : "") :
        (preflight ? "Vary: Origin, Access-Control-Request-Method, Access-Control-Request-Headers\r\n" : "Vary: Origin\r\n");

    *expected = char_length(vary);

    string_add_last_1(headers, vary);

    bool const  request_is_wildcard = char_compare_equal_2((char*) origin, origin_size, "*", CHAR_STATIC_SIZE("*"));
    Str *const  match               = request_is_wildcard ? nullptr : _http_service_cors_list_find(&self->origins, origin, origin_size);

    if (match == nullptr && (!self->origins_wildcard || request_is_wildcard || origin_size == 0)) {
        trace_log_pop();

        return false;
    }

    USize const echoed = match != nullptr ? str_get_size(match) : CHAR_STATIC_SIZE("*");

    *expected += CHAR_STATIC_SIZE("Access-Control-Allow-Origin: ") + echoed + CHAR_STATIC_SIZE("\r\n");

    string_add_last_1(headers, "Access-Control-Allow-Origin: ");

    if (match != nullptr) {
        string_add_last_2(headers, str_get_data(match), str_get_size(match));
    }
    else {
        string_add_last_1(headers, "*");
    }

    string_add_last_1(headers, "\r\n");

    if (self->allow_credentials) {
        *expected += CHAR_STATIC_SIZE("Access-Control-Allow-Credentials: true\r\n");

        string_add_last_1(headers, "Access-Control-Allow-Credentials: true\r\n");
    }

    trace_log_pop();

    return true;
}

static void _http_service_cors_lists_init(HTTP_Service_CORS *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

#ifdef ARENA_IMPLEMENTATION
    if (self->allocator != nullptr) {
        self->expose_line       = string_alloc_init_1(self->allocator);
        self->exposed_headers   = al_str_alloc_init_1(self->allocator);
        self->headers           = al_str_alloc_init_1(self->allocator);
        self->headers_line      = string_alloc_init_1(self->allocator);
        self->max_age_line      = string_alloc_init_1(self->allocator);
        self->methods           = al_str_alloc_init_1(self->allocator);
        self->methods_line      = string_alloc_init_1(self->allocator);
        self->origins           = al_str_alloc_init_1(self->allocator);

        trace_log_pop();

        return;
    }
#endif // ARENA_IMPLEMENTATION

    self->expose_line       = string_init_1();
    self->exposed_headers   = al_str_init_1();
    self->headers           = al_str_init_1();
    self->headers_line      = string_init_1();
    self->max_age_line      = string_init_1();
    self->methods           = al_str_init_1();
    self->methods_line      = string_init_1();
    self->origins           = al_str_init_1();

    trace_log_pop();
}

/* Every default add is now ANSWERED. The by-value constructors this module used to have had
 * nowhere to put the answer, so nine possible refusals were silently dropped; an in-place
 * bool constructor reports them, and a service missing a default method rejects requests it
 * was configured to allow. */
static bool _http_service_cors_list_add_defaults(HTTP_Service_CORS *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    bool stored = http_service_cors_method_add(self, "GET");

    stored = http_service_cors_method_add(self, "POST") && stored;
    stored = http_service_cors_method_add(self, "PUT") && stored;
    stored = http_service_cors_method_add(self, "PATCH") && stored;
    stored = http_service_cors_method_add(self, "DELETE") && stored;
    stored = http_service_cors_method_add(self, "OPTIONS") && stored;

    stored = http_service_cors_header_add(self, "Authorization") && stored;
    stored = http_service_cors_header_add(self, "Content-Type") && stored;
    stored = http_service_cors_header_add(self, "X-CSRF-Token") && stored;

    trace_log_pop();

    return stored;
}

static bool _http_service_cors_configure(HTTP_Service_CORS *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    _http_service_cors_lists_init(self);

    bool const rendered = _http_service_cors_render_max_age_line(self);
    bool const defaults = _http_service_cors_list_add_defaults(self);

    if (!rendered || !defaults) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_cors: the default policy could not be stored - refusing to run a half-configured service");

        http_service_cors_uninit(self);

        trace_log_pop();

        return false;
    }

    trace_log_pop();

    return true;
}

/*==============================================================================
 * MARK: - API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
bool http_service_cors_alloc_init_1(HTTP_Service_CORS *const self, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    bool const success = http_service_cors_alloc_init_2(self, HTTP_SERVICE_CORS_DEFAULT_ALLOW_CREDENTIALS, HTTP_SERVICE_CORS_DEFAULT_MAX_AGE, allocator);

    trace_log_pop();

    return success;
}

bool http_service_cors_alloc_init_2(HTTP_Service_CORS *const self, bool const allow_credentials, USize const max_age, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    /* Initialized through self, in the caller's final storage: the by-value form could not
     * report a refused default, and a service is configuration, not a value to copy. */
    *self = (HTTP_Service_CORS) DEFAULT_INITIALIZATION;

    self->allocator         = allocator;
    self->allow_credentials = allow_credentials;
    self->max_age           = max_age;

    bool const success = _http_service_cors_configure(self);

    trace_log_pop();

    return success;
}
#endif // ARENA_IMPLEMENTATION

bool http_service_cors_exposed_header_add(HTTP_Service_CORS *const self, char const *const header) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "header", (void*) header);

    USize const header_size = char_length(header);

    if (!http_headers_token_valid_2(header, header_size)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_cors: exposed header refused - not an HTTP token");

        trace_log_pop();

        return false;
    }

    bool stored = _http_service_cors_list_add(self, &self->exposed_headers, header, header_size, false);

    if (stored) {
        stored = _http_service_cors_render_list_line(&self->expose_line, "Access-Control-Expose-Headers: ", &self->exposed_headers);
    }

    trace_log_pop();

    return stored;
}

bool http_service_cors_header_add(HTTP_Service_CORS *const self, char const *const header) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "header", (void*) header);

    USize const header_size = char_length(header);

    if (!http_headers_token_valid_2(header, header_size)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_cors: request header refused - not an HTTP token");

        trace_log_pop();

        return false;
    }

    bool stored = _http_service_cors_list_add(self, &self->headers, header, header_size, false);

    if (stored) {
        self->headers_wildcard  = self->headers_wildcard || char_compare_equal_2((char*) header, header_size, "*", CHAR_STATIC_SIZE("*"));
        stored                  = _http_service_cors_render_list_line(&self->headers_line, "Access-Control-Allow-Headers: ", &self->headers);
    }

    trace_log_pop();

    return stored;
}

bool http_service_cors_headers_allowed_1(HTTP_Service_CORS const *const self, char const *const headers) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "headers", (void*) headers);

    bool const allowed = http_service_cors_headers_allowed_2(self, headers, char_length(headers));

    trace_log_pop();

    return allowed;
}

bool http_service_cors_headers_allowed_2(HTTP_Service_CORS const *const self, char const *const headers, USize const headers_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "headers", (void*) headers);

    USize   cursor  = 0;
    bool    allowed = true;

    while (cursor < headers_size) {
        USize start = 0;
        USize stop  = 0;

        cursor = _http_service_cors_element_next(headers, headers_size, cursor, &start, &stop);

        if (stop > start && !_http_service_cors_header_allowed_2(self, headers + start, stop - start)) {
            allowed = false;

            break;
        }
    }

    trace_log_pop();

    return allowed;
}

String http_service_cors_headers_create(HTTP_Service_CORS const *const self, char const *const origin) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "origin", (void*) origin);

    /* The "arena if this service has one, heap if it does not" branch is
     * string_init_optional's job now; the ifdef remains only because the allocator FIELD is
     * compiled out with ARENA_IMPLEMENTATION, so there is nothing to pass without it. */
#ifdef ARENA_IMPLEMENTATION
    String headers = string_init_optional(self->allocator);
#else
    String headers = string_init_1();
#endif // ARENA_IMPLEMENTATION

    USize const before      = string_get_size(&headers);
    USize       expected    = 0;
    bool const  allowed     = _http_service_cors_origin_add_header(self, &headers, origin, char_length(origin), false, &expected);

    if (allowed) {
        expected += string_get_size(&self->expose_line);

        string_add_last_4(&headers, &self->expose_line);
    }

    /* ONE comparison covers both paths, the refusal included, so every line this function
     * can emit - Vary, Allow-Origin, Allow-Credentials, Expose-Headers - is size-accounted.
     * A block missing one of them is discarded rather than shipped incomplete: half a CORS
     * answer is worse than none, and the Vary half is cache-unsafe for every other origin. */
    if (string_get_size(&headers) != before + expected) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_cors: the allocator refused a response header line - the header block is discarded");

        string_clear(&headers);
    }

    trace_log_pop();

    return headers;
}

bool http_service_cors_init_1(HTTP_Service_CORS *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    bool const success = http_service_cors_init_2(self, HTTP_SERVICE_CORS_DEFAULT_ALLOW_CREDENTIALS, HTTP_SERVICE_CORS_DEFAULT_MAX_AGE);

    trace_log_pop();

    return success;
}

bool http_service_cors_init_2(HTTP_Service_CORS *const self, bool const allow_credentials, USize const max_age) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* max_age 0 is a legal VALUE ("do not cache this preflight"), and the standard setting
     * while a policy is being developed. It used to reach error_check_non_value_uint, which
     * ends the process over a configuration number. */
    *self = (HTTP_Service_CORS) DEFAULT_INITIALIZATION;

    self->allow_credentials = allow_credentials;
    self->max_age           = max_age;

    bool const success = _http_service_cors_configure(self);

    trace_log_pop();

    return success;
}

bool http_service_cors_method_add(HTTP_Service_CORS *const self, char const *const method) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "method", (void*) method);

    USize const method_size = char_length(method);

    if (!http_headers_token_valid_2(method, method_size)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_cors: method refused - not an HTTP token");

        trace_log_pop();

        return false;
    }

    bool stored = _http_service_cors_list_add(self, &self->methods, method, method_size, false);

    if (stored) {
        self->methods_wildcard  = self->methods_wildcard || char_compare_equal_2((char*) method, method_size, "*", CHAR_STATIC_SIZE("*"));
        stored                  = _http_service_cors_render_list_line(&self->methods_line, "Access-Control-Allow-Methods: ", &self->methods);
    }

    trace_log_pop();

    return stored;
}

bool http_service_cors_method_allowed_1(HTTP_Service_CORS const *const self, char const *const method) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "method", (void*) method);

    bool const allowed = http_service_cors_method_allowed_2(self, method, char_length(method));

    trace_log_pop();

    return allowed;
}

bool http_service_cors_method_allowed_2(HTTP_Service_CORS const *const self, char const *const method, USize const method_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "method", (void*) method);

    bool const allowed = method_size > 0 &&
        (self->methods_wildcard || _http_service_cors_list_find(&self->methods, method, method_size) != nullptr);

    trace_log_pop();

    return allowed;
}

bool http_service_cors_origin_add(HTTP_Service_CORS *const self, char const *const origin) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "origin", (void*) origin);

    USize   const   origin_size = char_length(origin);
    bool    const   wildcard    = char_compare_equal_2((char*) origin, origin_size, "*", CHAR_STATIC_SIZE("*"));

    if (!_http_service_cors_origin_valid(origin, origin_size)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA,
            "http_service_cors: origin refused - empty, or carrying a space, a comma, a control byte (CR and LF included), DEL, or any byte above 0x7E");

        trace_log_pop();

        return false;
    }

    /* The footgun this module shipped with: "*" stored fine and then denied EVERY request,
     * because a wildcard origin and credentials are mutually exclusive in the browser. It
     * is refused here, loudly, instead of being discovered as a site that cannot call its
     * own API. */
    if (wildcard && self->allow_credentials) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA,
            "http_service_cors: the wildcard origin is refused while credentials are allowed - name each origin, or initialize with allow_credentials false");

        trace_log_pop();

        return false;
    }

    bool const stored = _http_service_cors_list_add(self, &self->origins, origin, origin_size, true);

    if (stored && wildcard) {
        self->origins_wildcard = true;
    }

    trace_log_pop();

    return stored;
}

bool http_service_cors_origin_allowed_1(HTTP_Service_CORS const *const self, char const *const origin) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "origin", (void*) origin);

    bool const allowed = http_service_cors_origin_allowed_2(self, origin, char_length(origin));

    trace_log_pop();

    return allowed;
}

bool http_service_cors_origin_allowed_2(HTTP_Service_CORS const *const self, char const *const origin, USize const origin_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "origin", (void*) origin);

    bool const request_is_wildcard  = char_compare_equal_2((char*) origin, origin_size, "*", CHAR_STATIC_SIZE("*"));
    bool const allowed              = origin_size > 0 && !request_is_wildcard &&
        (_http_service_cors_list_find(&self->origins, origin, origin_size) != nullptr || self->origins_wildcard);

    trace_log_pop();

    return allowed;
}

bool http_service_cors_origin_allowed_3(HTTP_Service_CORS const *const self, Str const *const origin) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "origin", (void*) origin);

    /* An empty Str's data pointer is null, so the size is read first and answered here. */
    if (str_get_size(origin) == 0) {
        trace_log_pop();

        return false;
    }

    bool const allowed = http_service_cors_origin_allowed_2(self, str_get_data(origin), str_get_size(origin));

    trace_log_pop();

    return allowed;
}

bool http_service_cors_preflight_allowed_1(HTTP_Service_CORS const *const self, char const *const origin, char const *const method, char const *const headers) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "origin", (void*) origin);
    error_check_null(LOG_METADATA, "method", (void*) method);
    error_check_null(LOG_METADATA, "headers", (void*) headers);

    bool const allowed = http_service_cors_preflight_allowed_2(self, origin, char_length(origin), method, char_length(method), headers, char_length(headers));

    trace_log_pop();

    return allowed;
}

bool http_service_cors_preflight_allowed_2(HTTP_Service_CORS const *const self,
    char const *const origin, USize const origin_size, char const *const method, USize const method_size, char const *const headers, USize const headers_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "origin", (void*) origin);
    error_check_null(LOG_METADATA, "method", (void*) method);
    error_check_null(LOG_METADATA, "headers", (void*) headers);

    bool const allowed = http_service_cors_origin_allowed_2(self, origin, origin_size) &&
        http_service_cors_method_allowed_2(self, method, method_size) &&
        http_service_cors_headers_allowed_2(self, headers, headers_size);

    trace_log_pop();

    return allowed;
}

String http_service_cors_preflight_create(HTTP_Service_CORS const *const self,
    char const *const origin, char const *const method, char const *const headers, bool const private_network_requested) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "origin", (void*) origin);
    error_check_null(LOG_METADATA, "method", (void*) method);
    error_check_null(LOG_METADATA, "headers", (void*) headers);

#ifdef ARENA_IMPLEMENTATION
    String block = string_init_optional(self->allocator);
#else
    String block = string_init_1();
#endif // ARENA_IMPLEMENTATION

    USize       before          = string_get_size(&block);
    USize       origin_expected = 0;
    bool const  origin_allowed  = _http_service_cors_origin_add_header(self, &block, origin, char_length(origin), true, &origin_expected);

    /* Compared on the refusal path as well: the Vary a shared cache needs is written BEFORE
     * the allow decision, so a dropped one has to discard the block rather than let it ship
     * beside an Allow-Origin - or, on a refusal, as an answer no cache can key correctly. */
    if (string_get_size(&block) != before + origin_expected) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_cors: the allocator refused an origin line - the preflight response is discarded");

        string_clear(&block);

        trace_log_pop();

        return block;
    }

    if (!origin_allowed) {
        trace_log_pop();

        return block;
    }

    USize const method_size = char_length(method);

    /* Every line below is SIZE-ACCOUNTED, not CRLF-tested. string_add_2 refuses a growth
     * WHOLLY, so a dropped line leaves the block ending in the PREVIOUS line's CRLF and a
     * trailing-byte guard passes it - which is why the guards this replaced could not fire
     * once, and a preflight whose Allow-Methods the allocator dropped shipped an
     * Allow-Origin naming no methods. IN A CHECKED BUILD the only reachable shape is a
     * REFUSED allocator: an EXHAUSTED arena never gets here, because string_reserve borrows
     * through allocator_borrow, whose allocate hook ends the process on exhaustion. Without
     * ERROR_CHECK_ENABLED that abort compiles out, arena_linear_try_alloc answers nullptr,
     * string_reserve refuses, and an exhausted arena reaches these guards exactly like a
     * refused one - which is where tests/http/service/cors/test_unchecked.c pins them. */
    before = string_get_size(&block);

    if (self->methods_wildcard && self->allow_credentials && http_headers_token_valid_2(method, method_size)) {
        USize const expected = CHAR_STATIC_SIZE("Access-Control-Allow-Methods: ") + method_size + CHAR_STATIC_SIZE("\r\n");

        string_add_last_1(&block, "Access-Control-Allow-Methods: ");

        string_add_last_2(&block, (char*) method, method_size);

        string_add_last_1(&block, "\r\n");

        if (string_get_size(&block) != before + expected) {
            log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_cors: the allocator refused the reflected methods line - the preflight response is discarded");

            string_clear(&block);

            trace_log_pop();

            return block;
        }
    }
    else if (!self->methods_wildcard || !self->allow_credentials) {
        string_add_last_4(&block, &self->methods_line);

        if (string_get_size(&block) != before + string_get_size(&self->methods_line)) {
            log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_cors: the allocator refused the cached methods line - the preflight response is discarded");

            string_clear(&block);

            trace_log_pop();

            return block;
        }
    }

    before = string_get_size(&block);

    if (self->headers_wildcard && self->allow_credentials) {
        USize expected = 0;

        /* A non-token request list is CLIENT bytes, so it is answered exactly as a
         * non-token request METHOD is: the offending line is omitted and the rest of the
         * block stands, which fails closed at the browser for want of Allow-Headers. It is
         * also SILENT - the old shape wiped the block and logged a WARN per hostile
         * preflight, which is a log flood any client could drive. Only the second case,
         * the allocator dropping a line this service did write, is worth a line. */
        if (_http_service_cors_reflect_headers(&block, headers, char_length(headers), &expected) && string_get_size(&block) != before + expected) {
            log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_cors: the allocator refused the reflected headers line - the preflight response is discarded");

            string_clear(&block);

            trace_log_pop();

            return block;
        }
    }
    else {
        string_add_last_4(&block, &self->headers_line);

        if (string_get_size(&block) != before + string_get_size(&self->headers_line)) {
            log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_cors: the allocator refused the cached headers line - the preflight response is discarded");

            string_clear(&block);

            trace_log_pop();

            return block;
        }
    }

    /* Max-Age is an OPTIONAL cache hint, but it is guarded like every other line so that the
     * header's "every line is size-accounted" contract holds without a carve-out: a refused
     * append discards the block rather than shipping one that differs from what a checked
     * build would send. */
    before = string_get_size(&block);

    string_add_last_4(&block, &self->max_age_line);

    if (string_get_size(&block) != before + string_get_size(&self->max_age_line)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_cors: the allocator refused the max-age line - the preflight response is discarded");

        string_clear(&block);

        trace_log_pop();

        return block;
    }

    before = string_get_size(&block);

    if (private_network_requested && self->allow_private_network) {
        string_add_last_1(&block, "Access-Control-Allow-Private-Network: true\r\n");

        if (string_get_size(&block) != before + CHAR_STATIC_SIZE("Access-Control-Allow-Private-Network: true\r\n")) {
            log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_cors: the allocator refused the private-network line - the preflight response is discarded");

            string_clear(&block);

            trace_log_pop();

            return block;
        }
    }

    trace_log_pop();

    return block;
}

void http_service_cors_private_network_set(HTTP_Service_CORS *const self, bool const allow) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    self->allow_private_network = allow;

    trace_log_pop();
}

void http_service_cors_uninit(HTTP_Service_CORS *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    al_str_uninit(&self->exposed_headers);
    al_str_uninit(&self->headers);
    al_str_uninit(&self->methods);
    al_str_uninit(&self->origins);

    string_uninit(&self->expose_line);
    string_uninit(&self->headers_line);
    string_uninit(&self->max_age_line);
    string_uninit(&self->methods_line);

    *self = (HTTP_Service_CORS) DEFAULT_INITIALIZATION;

    trace_log_pop();
}