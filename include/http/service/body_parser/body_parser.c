#include <http/service/body_parser/body_parser.h>

#include <char/char.h>
#include <container/str/str.h>
#include <http/query/query.h>

/*==============================================================================
 * MARK: - Internal Helpers
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
static HTTP_Service_Body_Parser _http_service_body_parser_init(Arena *const allocator)
#else
static HTTP_Service_Body_Parser _http_service_body_parser_init(void)
#endif // ARENA_IMPLEMENTATION
{
    HTTP_Service_Body_Parser parser = DEFAULT_INITIALIZATION;

#ifdef ARENA_IMPLEMENTATION
    /* A null allocator means "no arena" (the heap constructor's contract); the map's
     * own arena constructor aborts on null, so route a null allocator to its heap
     * constructor instead. */
    parser.allocator  = allocator;
    parser.form_data  = allocator != nullptr ? map_char_char_alloc_init_1(allocator) : map_char_char_init_1();
#else
    parser.form_data  = map_char_char_init_1();
#endif // ARENA_IMPLEMENTATION

    parser.max_pairs = HTTP_SERVICE_BODY_PARSER_MAX_PAIRS_DEFAULT;

    return parser;
}

/*
 * Reports whether content_type's media-type token - the header value up to (not
 * including) its first ';', trailing spaces/tabs trimmed - equals expected, ASCII
 * case-insensitively. Parameters (e.g. "; charset=utf-8") never participate.
 */
static bool _http_service_body_parser_content_type_is(char const *const content_type, char const *const expected) {
    char    const   *const semicolon   = char_find_slice_3(content_type, 0, ";");
    USize           token_start        = 0;
    USize           token_size         = semicolon != nullptr ? (USize) (semicolon - content_type) : char_length(content_type);

    /* A caller handing over the raw header value may still carry the space after
     * a preceding ": " - trim both ends of the token, not just the trailing
     * space/tab before a parameter, so " application/json" still matches. */
    while (token_start < token_size && (content_type[token_start] == ' ' || content_type[token_start] == '\t')) {
        token_start += 1;
    }

    while (token_size > token_start && (content_type[token_size - 1] == ' ' || content_type[token_size - 1] == '\t')) {
        token_size -= 1;
    }

    return char_compare_iequal_2(content_type + token_start, token_size - token_start, expected, char_length(expected));
}

/* Every parse() call starts from a clean slate: a service reused across requests (or
 * re-parsing a body of a different content type) never carries a PREVIOUS call's
 * form_data, is_form/is_json flags, or JSON pointer forward. */
static void _http_service_body_parser_reset(HTTP_Service_Body_Parser *const self) {
    map_char_char_clear(&self->form_data);

    self->is_form   = false;
    self->is_json   = false;
    self->json_data = nullptr;
    self->json_size = 0;
}

/*
 * Pair boundaries come from http_query_next and decoding from http_query_decode - the
 * percent and '+' rules live once, in http/query. next is byte-transparent over the sized
 * payload (a raw NUL is data, a leading '?' is not stripped), which is exactly the walk this
 * parser always made: the payload is a sized buffer that may be unterminated.
 */
static bool _http_service_body_parser_parse_form(HTTP_Service_Body_Parser *const self, char const *const payload, USize const payload_size) {
    trace_log_push(LOG_METADATA);

    USize cursor = 0;
    Str name = str_init_1();
    Str value = str_init_1();
    bool over_budget = false;

    while (http_query_next(payload, payload_size, &cursor, &name, &value)) {
        char const *const name_data = str_get_data(&name);
        USize const key_encoded_size = str_get_size(&name);
        USize const val_encoded_size = str_get_size(&value);

        /* A pair with no '=' ("k") is skipped before it is ever counted against max_pairs.
         * next's contract: the value's data is nullptr exactly when the pair had no '='
         * ("k=" answers a non-null, zero-size view). */
        if (memory_empty(str_get_data(&value))) {
            continue;
        }

        if (self->max_pairs != 0 && map_char_char_get_size(&self->form_data) >= self->max_pairs) {
            over_budget = true;

            break;
        }

        /* try_borrow: both sizes come straight from the request body. */
#ifdef ARENA_IMPLEMENTATION
        char *const key = (char*) allocator_try_borrow(key_encoded_size + CHAR_END_CHARACTER, self->allocator);
        char *const val = (char*) allocator_try_borrow(val_encoded_size + CHAR_END_CHARACTER, self->allocator);
#else
        char *const key = (char*) allocator_try_borrow(key_encoded_size + CHAR_END_CHARACTER);
        char *const val = (char*) allocator_try_borrow(val_encoded_size + CHAR_END_CHARACTER);
#endif // ARENA_IMPLEMENTATION

        /* Skip the pair on an empty key (never a meaningful field name) or when
         * the allocator is drained. Both blocks are released here, val-before-key
         * (a LIFO stack arena pops in order); allocator_release ignores a null
         * pointer, but the guard still only releases the half that DID succeed,
         * so a decline on one side is never mistaken for one on the other. An
         * empty VALUE is legal past this point: decode(size 0) stores "". */
        if (memory_empty(key) || memory_empty(val) || key_encoded_size == 0) {
#ifdef ARENA_IMPLEMENTATION
            if (!memory_empty(val)) {
                allocator_release((void*) val, self->allocator);
            }

            if (!memory_empty(key)) {
                allocator_release((void*) key, self->allocator);
            }
#else
            if (!memory_empty(val)) {
                allocator_release((void*) val);
            }

            if (!memory_empty(key)) {
                allocator_release((void*) key);
            }
#endif // ARENA_IMPLEMENTATION

            continue;
        }

        /* Each block is one byte larger than its encoded slice, and decoding never grows a
         * value, so neither call can answer the too-small refusal (USIZE_MAX). */
        http_query_decode(key, key_encoded_size + CHAR_END_CHARACTER, name_data, key_encoded_size, true);
        http_query_decode(val, val_encoded_size + CHAR_END_CHARACTER, str_get_data(&value), val_encoded_size, true);

        /* add() ADOPTS on success and takes nothing on a decline; a declined
         * add must release both blocks itself or they leak. */
        if (!map_char_char_add(&self->form_data, key, val)) {
#ifdef ARENA_IMPLEMENTATION
            allocator_release((void*) val, self->allocator);
            allocator_release((void*) key, self->allocator);
#else
            allocator_release((void*) val);
            allocator_release((void*) key);
#endif // ARENA_IMPLEMENTATION
        }
    }

    trace_log_pop();

    return !over_budget;
}

/*==============================================================================
 * MARK: - API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
HTTP_Service_Body_Parser http_service_body_parser_alloc_init(Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    HTTP_Service_Body_Parser const parser = _http_service_body_parser_init(allocator);

    trace_log_pop();

    return (HTTP_Service_Body_Parser) parser;
}
#endif // ARENA_IMPLEMENTATION

char* http_service_body_parser_form_get(HTTP_Service_Body_Parser const *const self, char const *const key) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "key", (void*) key);

    if (!self->is_form) {
        trace_log_pop();

        return nullptr;
    }

    char *const buffer = map_char_char_at_1(&self->form_data, key);

    trace_log_pop();

    return buffer;
}

HTTP_Service_Body_Parser http_service_body_parser_init(void) {
#ifdef ARENA_IMPLEMENTATION
    return _http_service_body_parser_init(nullptr);
#else
    return _http_service_body_parser_init();
#endif // ARENA_IMPLEMENTATION
}

Byte const* http_service_body_parser_json_get(HTTP_Service_Body_Parser const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->is_json ? self->json_data : nullptr;
}

USize http_service_body_parser_json_get_size(HTTP_Service_Body_Parser const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->is_json ? self->json_size : 0;
}

bool http_service_body_parser_parse(HTTP_Service_Body_Parser *const self, char const *const content_type, Byte const *const payload, USize const payload_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "content_type", (void*) content_type);

    /* An empty payload (Content-Length: 0) is client input, not a programmer error: a
     * null payload with a non-zero size is still a contract abort below. */
    if (payload_size == 0) {
        _http_service_body_parser_reset(self);

        trace_log_pop();

        return false;
    }

    error_check_null(LOG_METADATA, "payload", (void*) payload);

    _http_service_body_parser_reset(self);

    bool success = false;

    if (_http_service_body_parser_content_type_is(content_type, HTTP_SERVICE_BODY_PARSER_CONTENT_TYPE_FORM)) {
        self->is_form = true;
        success       = _http_service_body_parser_parse_form(self, (char const *) payload, payload_size);
    }
    else if (_http_service_body_parser_content_type_is(content_type, HTTP_SERVICE_BODY_PARSER_CONTENT_TYPE_JSON)) {
        self->is_json   = true;
        self->json_data = payload;
        self->json_size = payload_size;
        success         = true;
    }

    trace_log_pop();

    return success;
}

void http_service_body_parser_uninit(HTTP_Service_Body_Parser *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    map_char_char_uninit(&self->form_data);

    self->is_form   = false;
    self->is_json   = false;
    self->json_data = nullptr;
    self->json_size = 0;

    trace_log_pop();
}