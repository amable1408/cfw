/* ============================================================================
 *  CFW — Query String & Percent-Decode Helpers
 *  --------------------------------------------------------------------------
 *  @file    query.h
 *  @brief   Pure parsers for URL query strings and percent-encoded values.
 *  @license MIT (see LICENSE file)
 *
 *  General-purpose (no project coupling). Extracts a raw value for a key from a
 *  query string and percent-decodes an encoded value. Both allocate their
 *  result in the caller-supplied arena and never mutate their inputs. Arena
 *  mandatory: no heap tier is offered, so the whole public surface exists only
 *  under ARENA_IMPLEMENTATION.
 *
 *  Features:
 *    - http_query_alloc_get_1..4: find a key's raw (not decoded) value in a
 *      "key=value&..." query string, by char* key (_1, size via char_length),
 *      sized char* key (_2), Str key (_3), or String key (_4).
 *      http_query_alloc_get_5 additionally takes a sized (not necessarily
 *      NUL-terminated) query, serving a fixed-capacity buffer such as
 *      http_server_request_query_copy's straight from its known capacity.
 *    - http_query_alloc_decode / _2: percent-decode ("%XX" -> byte) a value.
 *      _2 is sized and reports the exact decoded byte count, so an embedded
 *      "%00" is never silently swallowed by a later char_length on the result.
 *    - http_query_alloc_form_decode / _2: the same decoder with the
 *      application/x-www-form-urlencoded convention layered on ("+" -> space).
 *      Backlog (.claude/state/design-backlog/http-leaf.md): move
 *      http/service/body_parser's own hand-rolled +/percent decode onto this
 *      pair instead of keeping a second copy.
 *
 *  Usage Examples:
 *    @code
 *    #include <http/query/query.h>
 *
 *    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);
 *    char const *const raw  = http_query_alloc_get_1("?code=abc%20def&state=xyz", "code", &arena);
 *    char const *const code = http_query_alloc_decode(raw, &arena); // "abc def"
 *    @endcode
 *
 *  Error Handling:
 *    - Contract violations - a null Str or String key handle to _3/_4, or a
 *      null out_size to decode_2/form_decode_2 - go through error_check_null,
 *      which LOGS AND ABORTS the process.
 *    - Every other absence is a VALUE, refused (nullptr) rather than aborted:
 *      a null/empty query, a null/empty key (the char* forms, or an empty
 *      Str/String whose data is null), a key not present, and an empty value
 *      ("key=") all answer nullptr identically - callers never special-case
 *      an empty value against a missing key. A key_size larger than the key's
 *      own NUL-terminated length is bounded by the key's real extent, never
 *      read past it. http_query_alloc_decode(_2)/form_decode(_2) refuse the
 *      same way on a null encoded pointer; a malformed "%" escape (not two
 *      hex digits, or fewer than two bytes left) passes through as literal,
 *      undecoded bytes rather than refusing the whole string.
 *    - With ERROR_CHECK_ENABLED off the null-handle checks above compile out
 *      and a violated contract becomes undefined behaviour instead of an
 *      abort; the VALUE refusals are ordinary runtime branches and hold
 *      either way.
 *
 *  Thread Safety:
 *    - Stateless; safe to call concurrently with distinct arenas.
 *
 *  Memory Management:
 *    - Arena tier only (no heap/free-pairing API): every allocation comes
 *      from the caller-supplied `allocator` and lives exactly as long as that
 *      arena; there is nothing to free or uninit here.
 *
 *  Dependencies (Deps):
 *    - arena (arena.h); char (char_alloc_new_1/_3, char_raw_to_hex, char_length -
 *      chained transitively via container/str, not included directly here);
 *      container/str, container/string (key tiers _3/_4); log/tracelog/memory
 *      (trace_log_push/pop, memory_empty - chained transitively via arena ->
 *      memory -> error -> tracelog -> log, so query.c includes only query.h).
 * ============================================================================
 */
#ifndef HTTP_QUERY_H
#define HTTP_QUERY_H

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/
#include <arena/arena.h>
#include <container/str/str.h>
#include <container/string/string.h>
#include <types.h>

#ifdef ARENA_IMPLEMENTATION

/*==============================================================================
 * MARK: - Public Functions
 *============================================================================*/

/**
 * @brief Percent-decode a URL query value ("%XX" -> byte). No "+" handling -
 *        use http_query_alloc_form_decode for form-encoded values.
 * @param encoded Encoded value, NUL-terminated. May be null/empty.
 * @param allocator Arena the returned string is allocated in.
 * @return Arena-allocated, NUL-terminated decoded string, or nullptr on a
 *         null/empty encoded or a refused allocator. The decoded byte count
 *         can be shorter than char_length() reports when "encoded" contains
 *         "%00" - use http_query_alloc_decode_2 when that must be detected.
 */
char* http_query_alloc_decode(char const *const encoded, Arena *const allocator);

/**
 * @brief Sized twin of http_query_alloc_decode: takes an explicit byte count
 *        instead of assuming NUL-termination, and reports the exact decoded
 *        byte count so an embedded "%00" is never silently truncated away.
 * @param encoded      Encoded value bytes. May be null.
 * @param encoded_size Length of encoded in bytes.
 * @param out_size     Set to the decoded byte count on success; untouched on
 *                      a refusal. Must be non-null.
 * @param allocator    Arena the returned buffer is allocated in.
 * @return Arena-allocated, NUL-terminated decoded buffer (NUL-terminated at
 *         its true end, but may carry embedded NULs before that - read
 *         *out_size bytes, not char_length()), or nullptr on a null encoded
 *         or a refused allocator.
 */
char* http_query_alloc_decode_2(char const *const encoded, USize const encoded_size, USize *const out_size, Arena *const allocator);

/**
 * @brief Percent-decode a URL-encoded query/form value with "+" -> space, the
 *        application/x-www-form-urlencoded convention.
 * @param encoded Encoded value, NUL-terminated. May be null/empty.
 * @param allocator Arena the returned string is allocated in.
 * @return Arena-allocated, NUL-terminated decoded string, or nullptr on a
 *         null/empty encoded or a refused allocator. Same "%00" truncation
 *         caveat as http_query_alloc_decode - use _2 when it must be detected.
 */
char* http_query_alloc_form_decode(char const *const encoded, Arena *const allocator);

/**
 * @brief Sized twin of http_query_alloc_form_decode.
 * @param encoded      Encoded value bytes. May be null.
 * @param encoded_size Length of encoded in bytes.
 * @param out_size     Set to the decoded byte count on success; untouched on
 *                      a refusal. Must be non-null.
 * @param allocator    Arena the returned buffer is allocated in.
 * @return Arena-allocated, NUL-terminated decoded buffer, or nullptr on a
 *         null encoded or a refused allocator.
 */
char* http_query_alloc_form_decode_2(char const *const encoded, USize const encoded_size, USize *const out_size, Arena *const allocator);

/**
 * @brief Extract a value for 'key' from a URL query string
 *        (e.g. "?code=xxx&state=yyy"), by NUL-terminated char* key.
 * @param query Query string; a leading '?' is tolerated. May be null/empty.
 * @param key   Key to find, NUL-terminated. May be null/empty.
 * @param allocator Arena the returned copy is allocated in.
 * @return Arena-allocated, NUL-terminated copy of the raw (not decoded)
 *         value, or nullptr when the key is absent, its value is empty
 *         ("key="), an argument is null/empty, or the allocator refuses.
 */
char* http_query_alloc_get_1(char const *const query, char const *const key, Arena *const allocator);

/**
 * @brief Sized-key twin of http_query_alloc_get_1.
 * @param query    Query string; a leading '?' is tolerated. May be null/empty.
 * @param key      Key to find. May be null/empty.
 * @param key_size Length of key in bytes. A value larger than key's own
 *                 NUL-terminated length is bounded by key's real extent
 *                 (never read past it) and simply fails to match.
 * @param allocator Arena the returned copy is allocated in.
 * @return Same contract as http_query_alloc_get_1.
 */
char* http_query_alloc_get_2(char const *const query, char const *const key, USize const key_size, Arena *const allocator);

/**
 * @brief Str-key twin of http_query_alloc_get_1.
 * @param query Query string; a leading '?' is tolerated. May be null/empty.
 * @param key   Key to find. Must be non-null (aborts); an empty Str (null
 *              data) is a value refusal, same as an empty char* key.
 * @param allocator Arena the returned copy is allocated in.
 * @return Same contract as http_query_alloc_get_1.
 */
char* http_query_alloc_get_3(char const *const query, Str const *const key, Arena *const allocator);

/**
 * @brief String-key twin of http_query_alloc_get_1.
 * @param query Query string; a leading '?' is tolerated. May be null/empty.
 * @param key   Key to find. Must be non-null (aborts); an empty String (null
 *              data) is a value refusal, same as an empty char* key.
 * @param allocator Arena the returned copy is allocated in.
 * @return Same contract as http_query_alloc_get_1.
 */
char* http_query_alloc_get_4(char const *const query, String const *const key, Arena *const allocator);

/**
 * @brief Sized-query, sized-key twin of http_query_alloc_get_1: neither
 *        argument needs to be NUL-terminated. Serves a fixed-capacity buffer
 *        such as http_server_request_query_copy's directly, from its known
 *        capacity, instead of taking a char_length pass over it first.
 * @param query      Query bytes; a leading '?' is tolerated. May be null.
 * @param query_size Length of query in bytes.
 * @param key        Key to find. May be null.
 * @param key_size   Length of key in bytes. Same bounding as _2's key_size.
 * @param allocator  Arena the returned copy is allocated in.
 * @return Same contract as http_query_alloc_get_1.
 */
char* http_query_alloc_get_5(char const *const query, USize const query_size, char const *const key, USize const key_size, Arena *const allocator);

#endif // ARENA_IMPLEMENTATION

#endif // HTTP_QUERY_H