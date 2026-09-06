/*
 * cookie.h - HTTP cookie helpers for the C Libraries Framework
 *
 * Builds Set-Cookie response headers and reads values from Cookie request
 * headers. The module has no application-specific state.
 *
 * Features:
 *   - Cookie request header value lookup.
 *   - Set-Cookie response header generation.
 *   - Clear-cookie response header generation, including a self-mirroring
 *     form for a cookie that was not set with the default Secure/HttpOnly/Lax
 *     flags (http_cookie_clear_header_create_3).
 *   - Path, Domain, Max-Age, Secure, HttpOnly, SameSite, and Partitioned flags.
 *   - Injection refusal: http_cookie_set_header_create and
 *     http_cookie_clear_header_create_3 refuse (EMPTY String + a WARN log)
 *     rather than emit a malformed or split header - see Error Handling.
 *   - Arena and heap allocation support.
 *
 * Usage Example:
 *   @code
 *   HTTP_Cookie cookie = http_cookie_init_1("sid", "token");
 *
 *   http_cookie_http_only_set(&cookie, true);
 *   http_cookie_same_site_set(&cookie, HTTP_COOKIE_SAME_SITE_LAX);
 *
 *   String header = http_cookie_set_header_create(&cookie);
 *
 *   string_uninit(&header);
 *   http_cookie_uninit(&cookie);
 *   @endcode
 *
 * Error Handling:
 *   - Public functions validate non-null pointers; with ERROR_CHECK_ENABLED
 *     off those checks compile out and a null argument is undefined behavior.
 *   - Missing cookies (http_cookie_get_1) return an empty String; an empty
 *     cookie VALUE ("sid=") is indistinguishable from an absent one - both
 *     read back as the empty String. An empty name is refused as a miss
 *     before scanning, even when the header contains a `; =value` pair.
 *   - http_cookie_set_header_create and http_cookie_clear_header_create_3
 *     refuse unconditionally (never `error_check`) on a value that would
 *     split or forge the header: a name that is not an RFC 6265 token, a
 *     value with bytes outside cookie-octet (quoted or bare), CTL, `;`, or `,` in
 *     Path/Domain, or SameSite=None / Partitioned without Secure. Refusal
 *     returns the EMPTY String and logs LOG_LEVEL_WARN - callers already
 *     test string_empty (e.g. http/service/security).
 *   - cookie-octet excludes every byte >= 0x80, so a non-ASCII value is always
 *     refused - percent-encode or base64-encode it before calling
 *     http_cookie_value_set.
 *   - The alloc_* tier refuses whole on a refused arena: http_cookie_alloc_init_1/_2
 *     write through `self` and return false rather than leave some fields
 *     copied and others EMPTY (never `Set-Cookie: =value`).
 *
 * Thread Safety:
 *   - Not thread-safe. Caller must synchronize shared cookie objects.
 *
 * Memory:
 *   - Cookie fields are copied into owned storage.
 *   - Returned String values must be uninitialized by caller.
 *
 * Performance:
 *   - Cookie lookup scans the header linearly.
 *
 * Dependencies:
 *   - string.
 */

#ifndef HTTP_COOKIE_H
#define HTTP_COOKIE_H

#include <container/string/string.h>

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief SameSite attribute value.
 *
 * HTTP_COOKIE_SAME_SITE_NONE requires Secure - http_cookie_set_header_create
 * and http_cookie_clear_header_create_3 refuse otherwise.
 */
typedef enum {
    HTTP_COOKIE_SAME_SITE_LAX      = 1,
    HTTP_COOKIE_SAME_SITE_NONE     = 2,
    HTTP_COOKIE_SAME_SITE_STRICT   = 3,
    HTTP_COOKIE_SAME_SITE_UNSET    = 0
} HTTP_Cookie_Same_Site;

/**
 * @brief Set-Cookie configuration.
 */
typedef struct {
    /** @brief Optional arena used by owned values and returned strings. */
    Arena *allocator;
    /** @brief Optional Domain attribute. */
    String domain;
    /** @brief Whether Max-Age should be emitted. */
    bool has_max_age;
    /** @brief Whether HttpOnly should be emitted. */
    bool http_only;
    /** @brief Max-Age value in seconds. */
    USize max_age;
    /** @brief Cookie name. */
    String name;
    /** @brief Whether Partitioned should be emitted; requires Secure. */
    bool partitioned;
    /** @brief Optional Path attribute. */
    String path;
    /** @brief SameSite attribute. */
    HTTP_Cookie_Same_Site same_site;
    /** @brief Whether Secure should be emitted. */
    bool secure;
    /** @brief Cookie value. */
    String value;
} HTTP_Cookie;

/*==============================================================================
 * MARK: - API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Build an arena-backed cookie clear header.
 * @param name Cookie name.
 * @param allocator Arena allocator.
 * @return Header block, or EMPTY if name is not a valid cookie token.
 */
String http_cookie_alloc_clear_header_create_1(char const *const name, Arena *const allocator);

/**
 * @brief Build an arena-backed cookie clear header with path and domain.
 * @param name Cookie name.
 * @param path Cookie path.
 * @param domain Cookie domain.
 * @param allocator Arena allocator.
 * @return Header block, or EMPTY on a refused name/path/domain.
 */
String http_cookie_alloc_clear_header_create_2(char const *const name, char const *const path, char const *const domain, Arena *const allocator);

/**
 * @brief Read an arena-backed cookie value from a Cookie request header.
 * @param header Cookie request header value.
 * @param name Cookie name.
 * @param allocator Arena allocator.
 * @return Cookie value or empty String.
 */
String http_cookie_alloc_get_1(char const *const header, char const *const name, Arena *const allocator);

/**
 * @brief Initialize an arena-backed cookie with default flags, in place.
 *
 * Refuses whole (returns false, `*self` left DEFAULT_INITIALIZATION) if the
 * arena refused any copy, rather than leave a half-built cookie.
 * @param self Output cookie.
 * @param name Cookie name.
 * @param value Cookie value.
 * @param allocator Arena allocator.
 * @return true on success, false on a refused arena.
 */
bool http_cookie_alloc_init_1(HTTP_Cookie *const self, char const *const name, char const *const value, Arena *const allocator);

/**
 * @brief Initialize an arena-backed cookie with common secure flags, in place.
 *
 * Refuses whole (returns false, `*self` left DEFAULT_INITIALIZATION) if the
 * arena refused any copy.
 * @param self Output cookie.
 * @param name Cookie name.
 * @param value Cookie value.
 * @param path Cookie path.
 * @param same_site SameSite value.
 * @param secure Whether Secure should be emitted.
 * @param http_only Whether HttpOnly should be emitted.
 * @param allocator Arena allocator.
 * @return true on success, false on a refused arena.
 */
bool http_cookie_alloc_init_2(HTTP_Cookie *const self,
    char const *const name, char const *const value, char const *const path, HTTP_Cookie_Same_Site const same_site, bool const secure, bool const http_only, Arena *const allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Build a cookie clear header (assumes a Secure/HttpOnly/Lax cookie).
 * @param name Cookie name.
 * @return Header block, or EMPTY if name is not a valid cookie token.
 */
String http_cookie_clear_header_create_1(char const *const name);

/**
 * @brief Build a cookie clear header with path and domain (assumes a
 *        Secure/HttpOnly/Lax cookie - use _3 to mirror a non-default cookie).
 * @param name Cookie name.
 * @param path Cookie path.
 * @param domain Cookie domain.
 * @return Header block, or EMPTY on a refused name/path/domain.
 */
String http_cookie_clear_header_create_2(char const *const name, char const *const path, char const *const domain);

/**
 * @brief Build a cookie clear header mirroring self's Path, Domain, Secure,
 *        HttpOnly, SameSite, and Partitioned flags - a cookie set without
 *        Secure is never cleared by _1/_2 (the browser rejects a Secure
 *        clear for a non-Secure cookie).
 * @param self Cookie whose flags are mirrored; its value is ignored.
 * @return Header block, or EMPTY on a refused name/path/domain/SameSite.
 */
String http_cookie_clear_header_create_3(HTTP_Cookie const *const self);

/**
 * @brief Set the cookie Domain attribute.
 * @param self Cookie object.
 * @param domain Cookie domain.
 */
void http_cookie_domain_set(HTTP_Cookie *const self, char const *const domain);

/**
 * @brief Read a cookie value from a Cookie request header.
 *
 * Scanner rules: an empty name is refused as a miss before scanning (an empty
 * name is a value's own text, not a key - "; =1" is a pair with an empty
 * name, not a name-less value); names are compared byte-exact (case-sensitive);
 * SP and HTAB around `;` are skipped; a pair without `=` is skipped
 * entirely (not treated as a name with an empty value); the first matching
 * name wins (browser order already puts the most specific Path first); a
 * DQUOTE-wrapped value is returned WITH its quotes (RFC 6265 SS4.1.1 allows
 * both forms); a trailing SP inside a value is kept as-is.
 * @param header Cookie request header value.
 * @param name Cookie name.
 * @return Cookie value or empty String.
 */
String http_cookie_get_1(char const *const header, char const *const name);

/**
 * @brief Set whether Max-Age should be emitted.
 * @param self Cookie object.
 * @param enabled Whether Max-Age should be emitted.
 */
void http_cookie_has_max_age_set(HTTP_Cookie *const self, bool const enabled);

/**
 * @brief Set whether HttpOnly should be emitted.
 * @param self Cookie object.
 * @param enabled Whether HttpOnly should be emitted.
 */
void http_cookie_http_only_set(HTTP_Cookie *const self, bool const enabled);

/**
 * @brief Initialize a cookie with default flags (Path=/, SameSite=Lax,
 *        Secure=true, HttpOnly=true).
 * @param name Cookie name.
 * @param value Cookie value.
 * @return Initialized cookie.
 */
HTTP_Cookie http_cookie_init_1(char const *const name, char const *const value);

/**
 * @brief Initialize a cookie with common secure flags.
 * @param name Cookie name.
 * @param value Cookie value.
 * @param path Cookie path.
 * @param same_site SameSite value.
 * @param secure Whether Secure should be emitted.
 * @param http_only Whether HttpOnly should be emitted.
 * @return Initialized cookie.
 */
HTTP_Cookie http_cookie_init_2(char const *const name, char const *const value, char const *const path, HTTP_Cookie_Same_Site const same_site, bool const secure, bool const http_only);

/**
 * @brief Set Max-Age value and enable emission.
 *
 * Expires is deliberately not offered as a separate attribute - RFC 6265
 * precedence means Max-Age alone is sufficient; to expire immediately use
 * http_cookie_max_age_set(self, 0).
 * @param self Cookie object.
 * @param max_age Max-Age value in seconds.
 */
void http_cookie_max_age_set(HTTP_Cookie *const self, USize const max_age);

/**
 * @brief Set whether Partitioned should be emitted; requires Secure.
 * @param self Cookie object.
 * @param enabled Whether Partitioned should be emitted.
 */
void http_cookie_partitioned_set(HTTP_Cookie *const self, bool const enabled);

/**
 * @brief Set the cookie Path attribute.
 * @param self Cookie object.
 * @param path Cookie path.
 */
void http_cookie_path_set(HTTP_Cookie *const self, char const *const path);

/**
 * @brief Parse a "Lax"/"None"/"Strict" cookie attribute string.
 * @param text SameSite text; matched case-insensitively ("lax", "LAX", and
 *        "Lax" all parse the same, mirroring how browsers match the
 *        attribute on the wire).
 * @return The matching enum value, or HTTP_COOKIE_SAME_SITE_UNSET when text
 *         does not match any of the three (empty text is UNSET, not a
 *         refusal - the caller decides whether that is acceptable; a
 *         non-empty text that fails to parse is a likely config typo worth
 *         a WARN at the caller's init site, e.g. http_service_session_init_2).
 */
HTTP_Cookie_Same_Site http_cookie_same_site_parse(char const *const text);

/**
 * @brief Set the cookie SameSite attribute.
 * @param self Cookie object.
 * @param same_site SameSite value.
 */
void http_cookie_same_site_set(HTTP_Cookie *const self, HTTP_Cookie_Same_Site const same_site);

/**
 * @brief Set whether Secure should be emitted.
 * @param self Cookie object.
 * @param enabled Whether Secure should be emitted.
 */
void http_cookie_secure_set(HTTP_Cookie *const self, bool const enabled);

/**
 * @brief Build a Set-Cookie response header.
 *
 * Refuses (EMPTY String + LOG_LEVEL_WARN) rather than emit a malformed or
 * split header - see Error Handling.
 * @param self Cookie object.
 * @return Header block, or EMPTY on refusal.
 */
String http_cookie_set_header_create(HTTP_Cookie const *const self);

/**
 * @brief Release cookie storage.
 * @param self Cookie object.
 */
void http_cookie_uninit(HTTP_Cookie *const self);

/**
 * @brief Set the cookie value.
 * @param self Cookie object.
 * @param value Cookie value.
 */
void http_cookie_value_set(HTTP_Cookie *const self, char const *const value);

#endif // HTTP_COOKIE_H