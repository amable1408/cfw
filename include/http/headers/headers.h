/*
 * headers.h - HTTP header helpers for the C Libraries Framework
 *
 * Builds common response headers: Cache-Control, Content-*, and browser
 * security policy headers.
 *
 * Features:
 *   - Cache-Control header builders (no-store, no-cache, public/private/immutable).
 *   - Content-Type, Content-Length, and Content-Disposition header builders.
 *   - Security header policy builders (CSP, frame, referrer, permissions, HSTS,
 *     Cross-Origin-Opener/Embedder/Resource-Policy).
 *   - RFC 9110 token validation, so a header name, a method or a list element that would
 *     break an emitted block is refused before it is stored rather than in each service.
 *   - Arena and heap return-value support.
 *
 * Usage Example:
 *   @code
 *   String headers = http_headers_security_create_1();
 *
 *   string_uninit(&headers);
 *   @endcode
 *
 * Error Handling:
 *   - Public functions validate non-null pointers via error_check_null, which ABORTS under
 *     ERROR_CHECK_ENABLED and compiles to nothing otherwise - with checks off, a null argument
 *     is undefined behavior, not a guaranteed abort.
 *   - Content values that reach the wire (filenames, MIME types) are DATA, not programmer
 *     error: a control character REFUSES as the EMPTY String in every build (never abort), and
 *     a filename's '"'/'\\' are backslash-escaped per RFC 6266 rather than refused.
 *   - A security policy value carrying a control byte (e.g. an embedded CRLF) always logs
 *     LOG_LEVEL_WARN. The by-value http_headers_security_init_2 cannot refuse, so it omits only
 *     that field's header; http_headers_security_alloc_init_2 CAN refuse and does - the whole
 *     policy is refused (false, *self zeroed), the same shape as their arena-refusal path below.
 *   - http_headers_security_alloc_init_1/_2 also refuse the WHOLE policy (false, *self zeroed)
 *     when a starved arena would otherwise silently drop a security header for the process
 *     lifetime; the heap tiers stay by-value since memory_alloc aborts rather than degrading to
 *     EMPTY.
 *   - http_headers_token_valid_1/_2 answer a bool about DATA and never abort on the value: an
 *     empty value is a legal value, answered false. Only a null pointer is a programming error.
 *
 * Thread Safety:
 *   - Stateless builders are thread-safe.
 *   - Shared configuration structs require caller synchronization.
 *
 * Memory:
 *   - Returned String values must be uninitialized by caller.
 *   - Arena variants return String storage owned by the provided arena.
 *
 * Performance:
 *   - Builders append into mutable String values.
 *
 * Dependencies:
 *   - string.
 */

#ifndef HTTP_HEADERS_H
#define HTTP_HEADERS_H

#include <container/string/string.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

/* frame-ancestors 'none' here duplicates the default X-Frame-Options: DENY below - harmless
 * (CSP wins in a CSP-aware browser); kept as defense-in-depth for browsers that honor only one. */
#define HTTP_HEADERS_SECURITY_DEFAULT_CONTENT_SECURITY_POLICY "default-src 'self'; frame-ancestors 'none'; base-uri 'self'; object-src 'none'"
#define HTTP_HEADERS_SECURITY_DEFAULT_CONTENT_TYPE_OPTIONS true
#define HTTP_HEADERS_SECURITY_DEFAULT_CROSS_ORIGIN_EMBEDDER_POLICY false
#define HTTP_HEADERS_SECURITY_DEFAULT_CROSS_ORIGIN_OPENER_POLICY true
#define HTTP_HEADERS_SECURITY_DEFAULT_CROSS_ORIGIN_RESOURCE_POLICY true
#define HTTP_HEADERS_SECURITY_DEFAULT_FRAME_OPTIONS "DENY"
#define HTTP_HEADERS_SECURITY_DEFAULT_PERMISSIONS_POLICY "geolocation=(), camera=(), microphone=()"
#define HTTP_HEADERS_SECURITY_DEFAULT_REFERRER_POLICY "no-referrer"
#define HTTP_HEADERS_SECURITY_DEFAULT_STRICT_TRANSPORT_SECURITY false
#define HTTP_HEADERS_SECURITY_DEFAULT_STRICT_TRANSPORT_SECURITY_INCLUDE_SUBDOMAINS true
#define HTTP_HEADERS_SECURITY_DEFAULT_STRICT_TRANSPORT_SECURITY_MAX_AGE 31536000
#define HTTP_HEADERS_SECURITY_DEFAULT_STRICT_TRANSPORT_SECURITY_PRELOAD false

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief Security header policy configuration.
 *
 * @note allocator is unconditional: the Arena type is always declared and this member is
 *       never gated. Layout is uniform across the build only because ARENA_IMPLEMENTATION is a
 *       whole-build flag: the embedded String type keeps ITS allocator member behind that flag,
 *       so one TU compiled without it would not agree with the rest.
 */
typedef struct {
    /** @brief Optional arena used by owned values and returned strings. */
    Arena *allocator;
    /** @brief Content-Security-Policy value. */
    String content_security_policy;
    /** @brief Whether to emit X-Content-Type-Options: nosniff. */
    bool content_type_options;
    /** @brief Whether to emit Cross-Origin-Embedder-Policy. */
    bool cross_origin_embedder_policy;
    /** @brief Whether to emit Cross-Origin-Opener-Policy. */
    bool cross_origin_opener_policy;
    /** @brief Whether to emit Cross-Origin-Resource-Policy. */
    bool cross_origin_resource_policy;
    /** @brief X-Frame-Options value. */
    String frame_options;
    /** @brief Permissions-Policy value. */
    String permissions_policy;
    /** @brief Referrer-Policy value. */
    String referrer_policy;
    /** @brief Whether to emit Strict-Transport-Security. */
    bool strict_transport_security;
    /** @brief Whether HSTS includes subdomains. */
    bool strict_transport_security_include_subdomains;
    /** @brief HSTS max-age in seconds. */
    USize strict_transport_security_max_age;
    /** @brief Whether HSTS includes preload. */
    bool strict_transport_security_preload;
} HTTP_Headers_Security;

/*==============================================================================
 * MARK: - Cache API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Build an arena-backed no-cache response header.
 * @param allocator Arena allocator.
 * @return Header block.
 */
String http_headers_cache_alloc_no_cache(Arena *const allocator);

/**
 * @brief Build an arena-backed no-store response header.
 * @param allocator Arena allocator.
 * @return Header block.
 */
String http_headers_cache_alloc_no_store(Arena *const allocator);

/**
 * @brief Build an arena-backed private cache response header.
 * @param max_age Cache duration in seconds.
 * @param allocator Arena allocator.
 * @return Header block.
 */
String http_headers_cache_alloc_private(USize const max_age, Arena *const allocator);

/**
 * @brief Build an arena-backed public cache response header.
 * @param max_age Cache duration in seconds.
 * @param allocator Arena allocator.
 * @return Header block.
 */
String http_headers_cache_alloc_public(USize const max_age, Arena *const allocator);

/**
 * @brief Build an arena-backed public immutable static-asset cache response header.
 * @param max_age Cache duration in seconds.
 * @param allocator Arena allocator.
 * @return Header block.
 */
String http_headers_cache_alloc_static(USize const max_age, Arena *const allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Build a bare max-age Cache-Control header directly into a caller-owned buffer, for
 *        callers (e.g. lws_serve_http_file's extra-header buffer) that build a raw header block
 *        rather than a String.
 * @param buffer Destination buffer.
 * @param buffer_capacity Destination buffer capacity in bytes.
 * @param max_age Cache duration in seconds.
 * @return Bytes written, excluding the terminator; 0 when buffer_capacity is 0 (buffer is left
 *         untouched) or when the header does not fit (buffer[0] is then '\0') - refused rather
 *         than truncated, since a cut-off header is indistinguishable from a real one.
 */
USize http_headers_cache_max_age_into(char *const buffer, USize const buffer_capacity, USize const max_age);

/**
 * @brief Build a no-cache response header.
 * @return Header block.
 */
String http_headers_cache_no_cache(void);

/**
 * @brief Build a no-store response header.
 * @return Header block.
 */
String http_headers_cache_no_store(void);

/**
 * @brief Build a private cache response header.
 * @param max_age Cache duration in seconds.
 * @return Header block.
 */
String http_headers_cache_private(USize const max_age);

/**
 * @brief Build a public cache response header.
 * @param max_age Cache duration in seconds.
 * @return Header block.
 */
String http_headers_cache_public(USize const max_age);

/**
 * @brief Build a public immutable static-asset cache response header.
 * @param max_age Cache duration in seconds.
 * @return Header block.
 */
String http_headers_cache_static(USize const max_age);

/*==============================================================================
 * MARK: - Content API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Build an arena-backed attachment Content-Disposition response header.
 * @param filename Download filename; '"' and '\\' are backslash-escaped (RFC 6266).
 * @param allocator Arena allocator.
 * @return Header block, or the EMPTY String if filename contains a control character.
 */
String http_headers_content_alloc_disposition_attachment(char const *const filename, Arena *const allocator);

/**
 * @brief Build an arena-backed text/html Content-Type response header.
 * @param allocator Arena allocator.
 * @return Header block.
 */
String http_headers_content_alloc_html(Arena *const allocator);

/**
 * @brief Build an arena-backed application/json Content-Type response header.
 * @param allocator Arena allocator.
 * @return Header block.
 */
String http_headers_content_alloc_json(Arena *const allocator);

/**
 * @brief Build an arena-backed Content-Length response header.
 * @param size Body size in bytes.
 * @param allocator Arena allocator.
 * @return Header block.
 */
String http_headers_content_alloc_length(USize const size, Arena *const allocator);

/**
 * @brief Build an arena-backed Content-Type response header.
 * @param mime MIME type.
 * @param allocator Arena allocator.
 * @return Header block, or the EMPTY String if mime contains a control character.
 */
String http_headers_content_alloc_type(char const *const mime, Arena *const allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Build an attachment Content-Disposition response header.
 * @param filename Download filename; '"' and '\\' are backslash-escaped (RFC 6266).
 * @return Header block, or the EMPTY String if filename contains a control character.
 */
String http_headers_content_disposition_attachment(char const *const filename);

/**
 * @brief Build a text/html Content-Type response header.
 * @return Header block.
 */
String http_headers_content_html(void);

/**
 * @brief Build an application/json Content-Type response header.
 * @return Header block.
 */
String http_headers_content_json(void);

/**
 * @brief Build a Content-Length response header.
 * @param size Body size in bytes.
 * @return Header block.
 */
String http_headers_content_length(USize const size);

/**
 * @brief Build a Content-Type response header.
 * @param mime MIME type.
 * @return Header block, or the EMPTY String if mime contains a control character.
 */
String http_headers_content_type(char const *const mime);

/*==============================================================================
 * MARK: - Security API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Initialize an arena-backed security policy with default values, in place.
 * @param self Destination policy.
 * @param allocator Arena allocator.
 * @return true if initialized; false if the arena refused a value, in which case *self is
 *         zeroed and no header should be emitted from it.
 */
bool http_headers_security_alloc_init_1(HTTP_Headers_Security *const self, Arena *const allocator);

/**
 * @brief Initialize an arena-backed security policy with explicit values, in place.
 * @param self Destination policy.
 * @param content_security_policy Content-Security-Policy value.
 * @param frame_options X-Frame-Options value.
 * @param referrer_policy Referrer-Policy value.
 * @param permissions_policy Permissions-Policy value.
 * @param content_type_options Whether to emit X-Content-Type-Options.
 * @param cross_origin_embedder_policy Whether to emit Cross-Origin-Embedder-Policy.
 * @param cross_origin_opener_policy Whether to emit Cross-Origin-Opener-Policy.
 * @param cross_origin_resource_policy Whether to emit Cross-Origin-Resource-Policy.
 * @param strict_transport_security Whether to emit Strict-Transport-Security.
 * @param strict_transport_security_max_age HSTS max-age in seconds.
 * @param strict_transport_security_include_subdomains Whether HSTS includes subdomains.
 * @param strict_transport_security_preload Whether HSTS includes preload.
 * @param allocator Arena allocator.
 * @return true if initialized; false (with *self zeroed and LOG_LEVEL_WARN logged) if the arena
 *         refused a value OR any policy value contains a control byte (0x00-0x1F or 0x7F; bytes
 *         >= 0x80 pass through as obs-text) - both refuse the WHOLE policy, unlike the by-value
 *         init_2 twin below which can only omit the one affected field.
 * @note A caller changing only a few fields should prefer http_headers_security_alloc_init_1
 *       followed by direct assignment to the public struct fields, over spelling out all 12
 *       positional arguments here. This form remains for full-control construction.
 */
bool http_headers_security_alloc_init_2(HTTP_Headers_Security *const self,
    char const *const content_security_policy, char const *const frame_options, char const *const referrer_policy, char const *const permissions_policy, bool const content_type_options,
    bool const cross_origin_embedder_policy, bool const cross_origin_opener_policy, bool const cross_origin_resource_policy, bool const strict_transport_security,
    USize const strict_transport_security_max_age, bool const strict_transport_security_include_subdomains, bool const strict_transport_security_preload, Arena *const allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Build default security headers.
 * @return Header block.
 */
String http_headers_security_create_1(void);

/**
 * @brief Build security headers from a policy.
 * @param self Security policy.
 * @return Header block.
 */
String http_headers_security_create_2(HTTP_Headers_Security const *const self);

/**
 * @brief Initialize a security policy with default values.
 * @return Initialized policy.
 */
HTTP_Headers_Security http_headers_security_init_1(void);

/**
 * @brief Initialize a security policy with explicit values.
 * @param content_security_policy Content-Security-Policy value.
 * @param frame_options X-Frame-Options value.
 * @param referrer_policy Referrer-Policy value.
 * @param permissions_policy Permissions-Policy value.
 * @param content_type_options Whether to emit X-Content-Type-Options.
 * @param cross_origin_embedder_policy Whether to emit Cross-Origin-Embedder-Policy.
 * @param cross_origin_opener_policy Whether to emit Cross-Origin-Opener-Policy.
 * @param cross_origin_resource_policy Whether to emit Cross-Origin-Resource-Policy.
 * @param strict_transport_security Whether to emit Strict-Transport-Security.
 * @param strict_transport_security_max_age HSTS max-age in seconds.
 * @param strict_transport_security_include_subdomains Whether HSTS includes subdomains.
 * @param strict_transport_security_preload Whether HSTS includes preload.
 * @return Initialized policy. A policy value containing a control byte (0x00-0x1F or 0x7F;
 *         bytes >= 0x80 pass through as obs-text) logs LOG_LEVEL_WARN and has that field's
 *         header omitted rather than the whole policy refused - this by-value form cannot
 *         refuse; see http_headers_security_alloc_init_2 for the refuse-whole tier.
 * @note A caller changing only a few fields should prefer http_headers_security_init_1
 *       followed by direct assignment to the public struct fields, over spelling out all 12
 *       positional arguments here. This form remains for full-control construction.
 */
HTTP_Headers_Security http_headers_security_init_2(
    char const *const content_security_policy, char const *const frame_options, char const *const referrer_policy, char const *const permissions_policy, bool const content_type_options,
    bool const cross_origin_embedder_policy, bool const cross_origin_opener_policy, bool const cross_origin_resource_policy, bool const strict_transport_security,
    USize const strict_transport_security_max_age, bool const strict_transport_security_include_subdomains, bool const strict_transport_security_preload);

/**
 * @brief Uninitialize a security policy.
 * @param self Security policy.
 */
void http_headers_security_uninit(HTTP_Headers_Security *const self);

/*==============================================================================
 * MARK: - Token API
 *============================================================================*/

/**
 * @brief Answer whether a NUL-terminated value is an RFC 9110 token.
 * @param value Candidate header name, method, or list element.
 * @return true when every byte is a tchar. An EMPTY value is answered false: a token has
 *         at least one character, and an empty header name would emit a bare ": value".
 */
bool http_headers_token_valid_1(char const *const value);

/**
 * @brief Answer whether a sized value is an RFC 9110 token.
 * @param value Candidate header name, method, or list element; not required to be
 *        NUL-terminated.
 * @param size Byte length of value.
 * @return See http_headers_token_valid_1. SP, HTAB, ':', ',', '"', '/', '(', ')', CR, LF,
 *         DEL and every byte at or above 0x80 are refused, which is what makes a stored
 *         value unable to carry a header break.
 */
bool http_headers_token_valid_2(char const *const value, USize const size);

#endif // HTTP_HEADERS_H