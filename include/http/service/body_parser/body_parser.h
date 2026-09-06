/*
 * body_parser.h - HTTP request body parser service for the C Libraries Framework
 *
 * Parses HTTP request bodies by Content-Type. Supports
 * application/x-www-form-urlencoded and application/json payloads.
 *
 * Features:
 *   - Parses URL-encoded form data into a Map_Char_Char key-value store.
 *   - Detects application/json payloads and exposes raw data for Json parsing.
 *   - Arena allocator support.
 *
 * Usage Example:
 *   @code
 *   HTTP_Service_Body_Parser parser = http_service_body_parser_init();
 *
 *   char const *content_type = "application/x-www-form-urlencoded";
 *   Byte const *payload = (Byte const *) "username=john&role=admin";
 *   USize payload_size = 24;
 *
 *   if (http_service_body_parser_parse(&parser, content_type, payload, payload_size)) {
 *       char *username = http_service_body_parser_form_get(&parser, "username");
 *   }
 *
 *   http_service_body_parser_uninit(&parser);
 *   @endcode
 *
 * Error Handling:
 *   - Required pointer arguments are validated first; a null argument aborts via
 *     error_check_null when ERROR_CHECK_ENABLED is defined. With ERROR_CHECK_ENABLED
 *     compiled out the null checks disappear and a null pointer is undefined behaviour,
 *     while an empty payload (payload_size == 0) is a value refusal (returns false) that
 *     holds in every build.
 *   - parse() returns false for: an empty payload, an unrecognized Content-Type, and past
 *     max_pairs (see Limits below). It returns true for a recognized, in-budget body even
 *     when individual malformed pairs inside it were skipped.
 *
 * Thread Safety:
 *   - Not thread-safe. Synchronization is required for shared instances.
 *
 * Memory:
 *   - Form keys and values are copied and owned by the service (heap constructor: malloc'd
 *     blocks freed by uninit; alloc_init constructor: arena-owned, released per the arena's
 *     own tier - a linear/pool arena's per-string release is then a no-op until the arena
 *     itself resets, which is expected, not a leak).
 *   - JSON payload pointer references the original payload buffer, never copied.
 *     The payload buffer must outlive the parser.
 *
 * Limits:
 *   - max_pairs bounds the number of URL-encoded pairs kept; init() sets it to
 *     HTTP_SERVICE_BODY_PARSER_MAX_PAIRS_DEFAULT (1024) rather than leaving an unbounded
 *     service as the default. Setting it to 0 EXPLICITLY switches it to unbounded - a
 *     caller opts into that, it is not what a freshly-init'd service does on its own.
 *     Parsing refuses (returns false) once accepting another pair would exceed the bound;
 *     pairs already parsed remain in form_data. The caller is expected to bound total
 *     payload size upstream (e.g. an HTTP server's payload_max_size) - this bounds shape,
 *     not size.
 *
 * Performance:
 *   - URL-encoded parsing is a single linear pass.
 *   - Form field lookup is a linear scan over the map.
 *
 * Dependencies:
 *   - char, map_char_char, memory.
 *
 * See body_parser.c for implementation details.
 */

#ifndef HTTP_SERVICE_BODY_PARSER_H
#define HTTP_SERVICE_BODY_PARSER_H

#include <container/map/map_char_char.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

/**
 * Content-Type value for URL-encoded form data. Matched as an exact, case-insensitive
 * media-type token: the header value up to its first ';' (parameters, e.g. "; charset=...",
 * are ignored), so "APPLICATION/X-WWW-FORM-URLENCODED; charset=utf-8" matches and
 * "application/x-www-form-urlencoded-extra" does not.
 */
#define HTTP_SERVICE_BODY_PARSER_CONTENT_TYPE_FORM "application/x-www-form-urlencoded"

/** Content-Type value for JSON data. Matched the same way as CONTENT_TYPE_FORM above. */
#define HTTP_SERVICE_BODY_PARSER_CONTENT_TYPE_JSON "application/json"

/**
 * Content-Type value for multipart form data. body_parser does NOT handle this content
 * type - parse() returns false for it. Use http/service/multipart
 * (http_service_multipart_parse) instead.
 */
#define HTTP_SERVICE_BODY_PARSER_CONTENT_TYPE_MULTIPART "multipart/form-data"

/** Default value of max_pairs set by init(); see the Limits section above the struct. */
#define HTTP_SERVICE_BODY_PARSER_MAX_PAIRS_DEFAULT 1024

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief HTTP request body parser state.
 * @note A hand-zeroed instance is unbounded, not safely default; construct via init() or alloc_init().
 */
typedef struct {
#ifdef ARENA_IMPLEMENTATION
    /** @brief Optional arena allocator pointer. */
    Arena *allocator;
#endif // ARENA_IMPLEMENTATION
    /**
     * @brief Maximum URL-encoded pairs to accept. init() sets this to
     *        HTTP_SERVICE_BODY_PARSER_MAX_PAIRS_DEFAULT; the caller may override it before
     *        parse(), including setting it explicitly to 0 for unbounded. Past this count
     *        parse() refuses (returns false); pairs already accepted stay in form_data.
     */
    USize max_pairs;
    /** @brief True if payload was application/x-www-form-urlencoded. */
    bool is_form;
    /** @brief True if payload was application/json. */
    bool is_json;
    /** @brief Parsed form key-value pairs (populated when is_form is true). */
    Map_Char_Char form_data;
    /** @brief Pointer to raw JSON payload (populated when is_json is true). */
    Byte const *json_data;
    /** @brief Size of raw JSON payload in bytes. */
    USize json_size;
} HTTP_Service_Body_Parser;

/*==============================================================================
 * MARK: - API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Initialize an arena-backed body parser service.
 * @param allocator Arena allocator.
 * @return Initialized service, with max_pairs set to HTTP_SERVICE_BODY_PARSER_MAX_PAIRS_DEFAULT.
 */
HTTP_Service_Body_Parser http_service_body_parser_alloc_init(Arena *const allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Get a parsed form field value by key.
 *
 * Only valid after a successful parse of a URL-encoded payload. An empty submitted value
 * ("k=") answers "" (present, empty) - distinct from a key that was never sent, which
 * answers nullptr. A key repeated in the body answers its first-parsed value.
 *
 * @param self Service instance.
 * @param key Field name to look up.
 * @return Pointer to value string, or nullptr if not found or not a form request.
 */
char* http_service_body_parser_form_get(HTTP_Service_Body_Parser const *const self, char const *const key);

/**
 * @brief Initialize a heap-backed body parser service.
 * @return Initialized service, with max_pairs set to HTTP_SERVICE_BODY_PARSER_MAX_PAIRS_DEFAULT.
 */
HTTP_Service_Body_Parser http_service_body_parser_init(void);

/**
 * @brief Get the raw JSON payload pointer.
 *
 * Only valid after a successful parse of a JSON payload. The pointer borrows the payload
 * buffer passed to parse() - it is never copied, and does not outlive that buffer.
 * Caller is responsible for parsing it with json_from_2() or equivalent.
 *
 * @param self Service instance.
 * @return Pointer to raw JSON bytes, or nullptr if not a JSON request.
 */
Byte const* http_service_body_parser_json_get(HTTP_Service_Body_Parser const *const self);

/**
 * @brief Get the size of the raw JSON payload.
 *
 * Only valid after a successful parse of a JSON payload.
 *
 * @param self Service instance.
 * @return Size of raw JSON payload in bytes, or 0 if not a JSON request.
 */
USize http_service_body_parser_json_get_size(HTTP_Service_Body_Parser const *const self);

/**
 * @brief Parse the request body based on its Content-Type.
 *
 * Every call resets prior state first (is_form, is_json, form_data, and the JSON fields
 * are cleared), so a service can be reused across requests without an intervening uninit -
 * parse() is single-use per call, never additive. content_type is matched as an exact,
 * case-insensitive media-type token up to its first ';' (see CONTENT_TYPE_FORM/JSON above);
 * leading and trailing spaces/tabs around the token are both trimmed before the comparison.
 *
 * Returns false for: an empty payload (payload_size == 0), an unrecognized or unsupported
 * Content-Type (including multipart/form-data - see CONTENT_TYPE_MULTIPART), and a form body
 * that would exceed max_pairs. Payload is byte-oriented (Byte const*): URL-encoded parsing
 * treats it as a sized run of bytes with no requirement that it be NUL-terminated.
 *
 * Form value notes: an empty value ("k=") is stored as ""; a decoded "%00" ends the C string
 * early because form_data stores C strings (the bytes after it are silently unreachable via
 * form_get - documented, not fixed here). A duplicate key ("a=1&a=2") keeps the first value;
 * form_get always answers the first-inserted value for a repeated key.
 *
 * @param self Service instance.
 * @param content_type The HTTP Content-Type header value.
 * @param payload The raw request body payload.
 * @param payload_size The size of the payload in bytes.
 * @return true on success, false on failure, an unsupported content type, an empty payload,
 *         or a form body over max_pairs.
 */
bool http_service_body_parser_parse(HTTP_Service_Body_Parser *const self, char const *const content_type, Byte const *const payload, USize const payload_size);

/**
 * @brief Release all service storage and reset state.
 * @param self Service instance.
 */
void http_service_body_parser_uninit(HTTP_Service_Body_Parser *const self);

#endif // HTTP_SERVICE_BODY_PARSER_H