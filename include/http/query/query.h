/* ============================================================================
 *  CFW — Query String & Percent-Encode/Decode Helpers
 *  --------------------------------------------------------------------------
 *  @file    query.h
 *  @brief   Pure parsers and builders for URL query strings and percent-encoded values.
 *  @version 0.4.1
 *  @license MIT (see LICENSE file)
 *
 *  General-purpose (no project coupling). Extracts a raw value for a key from a
 *  query string, percent-decodes an encoded value, and percent-ENCODES a value
 *  (or a whole application/x-www-form-urlencoded body) back. Nothing here
 *  mutates its inputs. Three tiers: the zero-allocation reading side
 *  (http_query_next, http_query_find, http_query_decode) answers VIEW
 *  slices or decodes into a caller buffer and is always available; the
 *  allocating reading side (alloc_get, alloc_decode) copies into the caller
 *  supplied arena and so exists only under ARENA_IMPLEMENTATION; the writing
 *  side appends to a caller-owned String, needs no arena, and is always
 *  available.
 *
 *  Features:
 *    - http_query_encode_1 / _2: percent-encode a value per RFC 3986 - the
 *      unreserved set (A-Z a-z 0-9 '-' '.' '_' '~') passes through, every other
 *      byte becomes "%XX" with UPPERCASE hex digits, and a space becomes "%20",
 *      never "+". _2 is sized, so a value carrying embedded NULs encodes in
 *      full. Both APPEND to `out`, so encodings compose. Every writing-side
 *      entry point takes its destination String FIRST: the String is the `self`
 *      being appended to, matching string_add_last_* and the rest of the family.
 *    - http_query_add_1 / _2: append one "name=value" pair to a QUERY STRING,
 *      prefixing "&" only where `out` already ends a pair - never after the
 *      '?' that opens the query string, and never after a '&' the caller
 *      wrote, so a seeded ".../search?" takes its first pair directly. Same
 *      shape as
 *      http_query_form_add, one flavour apart: both sides are encoded with the
 *      RFC 3986 convention, so a space is "%20" and not "+" - which is what
 *      belongs after a '?' in a URL. _2 is sized.
 *    - http_query_form_add_1 / _2: append one "name=value" pair to an
 *      application/x-www-form-urlencoded body, under the same separator rule as
 *      http_query_add. Both sides are encoded with the form convention, where a
 *      space becomes "+" - the single difference from http_query_add. _2 is
 *      sized. This pair is the canonical CFW form-body builder, and
 *      http_client_escape_1/_3 delegate their percent-encoding to
 *      http_query_encode_2 behind their published signatures.
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
 *    - http_query_next (0.4.0): the pair enumerator. Walks a sized query one
 *      "name=value" pair at a time through a caller-held cursor, answering raw
 *      (NOT decoded) VIEW slices into the query - no allocation, no copy.
 *      http/service/body_parser's form parser walks its bodies with it.
 *    - http_query_find (0.4.0): the zero-allocation lookup. Same match as
 *      alloc_get, answered as a VIEW, and - unlike alloc_get - it tells a
 *      present-but-empty "key=" (true, empty slice) from an absent key (false).
 *      Serves the lookups that used to allocate a value only to compare or
 *      int-parse it. Both match a key against a pair's WHOLE name (the bytes
 *      before its first '='), never a raw prefix of the pair - see Error
 *      Handling for the one behaviour this changed in 0.4.0.
 *    - http_query_decode (0.4.0): the caller-buffer decoder, both conventions
 *      behind one flag, whole-or-nothing - a dest too small for the decoded
 *      value is refused, never silently truncated. It is THE decoder: the
 *      alloc_decode family allocates and calls it, and body_parser decodes into
 *      its own map-adopted blocks with it, so the percent rules exist once.
 *    These three need no arena and are always available. Scanning lives in
 *    http_query_next and decoding in http_query_decode; alloc_get and
 *    alloc_decode are thin allocate-and-copy layers over them.
 *
 *  Usage Examples:
 *    @code
 *    #include <http/query/query.h>
 *
 *    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);
 *    char const *const raw  = http_query_alloc_get_1("?code=abc%20def&state=xyz", "code", &arena);
 *    char const *const code = http_query_alloc_decode(raw, &arena); // "abc def"
 *
 *    String body = string_init_1();
 *    http_query_form_add_1(&body, "secret", "s");
 *    http_query_form_add_1(&body, "response", "tok");   // "secret=s&response=tok"
 *    string_uninit(&body);
 *
 *    String url = string_init_1();
 *    string_add_last_1(&url, "https://example.com/search?");
 *    http_query_add_1(&url, "q", "a b");                // "...?q=a%20b"
 *    http_query_add_1(&url, "page", "2");               // "...?q=a%20b&page=2"
 *    string_uninit(&url);
 *    @endcode
 *
 *  Error Handling:
 *    - Contract violations - a null Str or String key handle to _3/_4, or a
 *      null out_size to alloc_decode_2/alloc_form_decode_2 - go through
 *      error_check_null, which LOGS AND ABORTS the process.
 *    - The zero-allocation tier (0.4.0) aborts through error_check_null on its
 *      CONTRACT pointers only: a null cursor/out_name/out_value, and a null
 *      query, key, dest or encoded while its paired size says bytes live there.
 *      Every size is a value: a zero-size query, key, dest or encoded may be
 *      null - `(nullptr, 0)` is a legal empty here, unlike the allocating
 *      tier, where http_query_alloc_decode_2 / http_query_alloc_form_decode_2
 *      refuse a null encoded pointer whatever its size says. Its refusals are values too - http_query_next answers false once
 *      no pair is left, http_query_find false for an absent key, and
 *      http_query_decode USIZE_MAX (with dest[0] = '\0' when dest_size > 0)
 *      for a dest that cannot hold the whole decoded value plus terminator.
 *    - The zero-allocation tier has TWO NUL rules, chosen per function.
 *      http_query_find reads a query up to its first NUL and skips one leading
 *      '?' (the C-string rule alloc_get always had), so it can be handed a
 *      fixed-capacity buffer such as http_server_request_query_copy's by its
 *      capacity. http_query_next and http_query_decode treat every one of their
 *      size bytes as data - a raw NUL is a byte like any other and no '?' is
 *      skipped - so for a NUL-terminated or fixed-capacity buffer pass
 *      char_length(buffer), not its capacity: over a zero-padded 256-byte
 *      buffer holding "a=1", next answers value "1" followed by 252 NUL bytes.
 *      http_server_request_query_copy answers a bool, not a length, so nothing
 *      hands the caller that size for free.
 *    - Key match (0.4.0, a behaviour change for alloc_get_1..5): a key matches
 *      a pair's whole name, the bytes before the pair's first '=' (and a pair
 *      with no '=' never matches). Through 0.3.x alloc_get matched the key's
 *      raw bytes at a pair's start followed by '=', so a key that itself
 *      carried '=' or '&' matched across them - "a=b" found "c" in "a=b=c",
 *      "&b" found "1" in "a&&b=1". Both now answer absent. Keys free of '='
 *      and '&' answer exactly as before.
 *    - Every other absence is a VALUE, refused (nullptr) rather than aborted:
 *      a null/empty query, a null/empty key (the char* forms, or an empty
 *      Str/String whose data is null), a key not present, and an empty value
 *      ("key=") all answer nullptr identically - callers never special-case
 *      an empty value against a missing key. A key_size larger than the key's
 *      own NUL-terminated length is bounded by the key's real extent, never
 *      read past it. http_query_alloc_decode(_2)/form_decode(_2) refuse the
 *      same way on a null encoded pointer ONLY (the cross-tier contrast with
 *      the zero-allocation tier's `(nullptr, 0)` is stated above): an empty encoded ("", or a
 *      zero encoded_size) is a legal VALUE and answers an empty,
 *      arena-allocated "" (never nullptr), the sized twins included; a malformed "%" escape (not two
 *      hex digits, or fewer than two bytes left) passes through as literal,
 *      undecoded bytes rather than refusing the whole string.
 *    - The writing side NEVER aborts: http_query_encode_1/_2,
 *      http_query_add_1/_2 and http_query_form_add_1/_2 answer false, leaving
 *      `out`/`body` untouched, on a null String handle, a null value (or a null
 *      name), a zero-length name, a value/name longer than
 *      HTTP_QUERY_ENCODE_MAX_SIZE, a value/name that lives INSIDE the
 *      destination's own allocation (the first append that grows the String
 *      would release the bytes still being read), or a destination that cannot
 *      GROW to hold the whole result. That last one is the arena case: the
 *      worst-case size (3x each side, plus "&", "=" and the terminator) is
 *      reserved before the first byte is appended and the reached capacity
 *      checked, so a refused arena answers false with nothing written instead
 *      of landing a separator, a name and an "=" and then losing the value
 *      while still answering true. The alias test is an INTERVAL
 *      overlap against the destination's whole capacity - not just its used
 *      size, and not just the start pointer - so a slice that begins before the
 *      buffer but runs into it is refused too. A ZERO-SIZE slice is answered
 *      true before that test ever runs: it reads nothing, so it cannot dangle.
 *      An EMPTY value is likewise a legal VALUE, not a refusal: encode appends
 *      nothing and answers true, and add/form_add append "name=" (a
 *      present-but-empty field, which is what the wire format spells).
 *      The alias refusal is deliberately over-strict for a VIEW destination
 *      (string_init_static and friends): such a String's storage is never
 *      freed, so growth copies away from it rather than releasing it, and an
 *      aliasing value would in fact be safe. Refusing it uniformly keeps one
 *      rule for one contract instead of a refusal that depends on how the
 *      caller happened to build the destination.
 *    - Mixing flavours into one String is legal and does exactly what it says:
 *      http_query_encode/http_query_add write "%20" where http_query_form_add
 *      writes "+". A String built from both carries both conventions, which no
 *      single decoder round-trips - pick one flavour per destination.
 *    - With ERROR_CHECK_ENABLED off the null-handle checks above compile out
 *      and a violated contract becomes undefined behaviour instead of an
 *      abort; the VALUE refusals are ordinary runtime branches and hold
 *      either way. The writing side carries no error_check at all, so it is
 *      identical in both builds.
 *
 *  Thread Safety:
 *    - Stateless; safe to call concurrently with distinct arenas and distinct
 *      out/body Strings. A single String is not internally synchronized.
 *
 *  Memory Management:
 *    - Zero-allocation tier: nothing is allocated. http_query_next and
 *      http_query_find answer VIEW Strs (owned == false) into the caller's
 *      query - they live exactly as long as that buffer, str_uninit on them is
 *      a harmless no-op, and they are READ-ONLY: never hand one to Str's
 *      in-place mutators (str_lower, str_replace_*, ...), which write through
 *      a view and here would write into the caller's const query.
 *      http_query_decode writes only inside dest[0..dest_size).
 *    - Allocating reading side: arena tier only (no heap/free-pairing API): every
 *      allocation comes from the caller-supplied `allocator` and lives exactly
 *      as long as that arena; there is nothing to free or uninit here.
 *    - Writing side: nothing is allocated by this module. The caller owns the
 *      `out`/`body` String, which grows through string_add_last_2 and must be
 *      string_uninit'd by the caller as usual.
 *
 *  Performance Characteristics:
 *    - Zero-allocation tier: http_query_next is one pass over the pair it
 *      answers; http_query_find one pass over the query (plus a NUL-bounding
 *      pass); http_query_decode one pass, or two when dest is not strictly
 *      larger than encoded (it measures before writing so a refusal is whole).
 *    - Allocating reading side: the matching zero-allocation pass plus one
 *      arena allocation per call.
 *    - Writing side: one pass over the input, producing 1 or 3 output bytes per
 *      input byte; the encoded result is at most 3x the input, so a body built
 *      from N bytes of names and values costs O(N) with the String's own
 *      amortized growth behind it. Output is staged through a 192-byte stack
 *      chunk and flushed with one string_add_last_2 per full chunk, not one per
 *      input byte: measured on a 64 KiB binary value (11 interleaved warmed
 *      rounds, median) the chunked form runs 5.2-6.5x faster than the per-byte
 *      appends it replaced, and the byte-for-byte output is identical.
 *
 *  Dependencies (Deps):
 *    - arena (arena.h); char (char_alloc_new_1/_3, char_raw_to_hex, char_length -
 *      included directly by query.c, since char.h declares no types this
 *      header's API names);
 *      container/str (str_init_1/str_init_3 for the VIEWs next and find
 *      answer, str_get_data/str_get_size; the _3 key tier);
 *      container/string (the _4 key tier; on the writing side
 *      string_add_last_2, string_reserve, and
 *      string_get_data/string_get_size/string_get_capacity - the alias guard
 *      measures the destination's whole allocation, the separator rule reads
 *      its last byte, and the whole-pair reserve is checked by the capacity it
 *      actually reached);
 *      log/tracelog/memory (trace_log_push/pop,
 *      memory_empty - chained transitively via arena -> memory -> error ->
 *      tracelog -> log; query.c adds only char.h beside query.h).
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

/*==============================================================================
 * MARK: - Macros
 *============================================================================*/

/**
 * @def HTTP_QUERY_ENCODE_MAX_SIZE
 * @brief Largest single name or value http_query_encode_1/_2,
 *        http_query_add_1/_2 and http_query_form_add_1/_2 will encode, in input
 *        bytes (1 MiB). Anything longer is refused (false) before a byte is
 *        read, so the worst-case 3x expansion stays bounded and an absurd size
 *        can never wrap the append arithmetic. Query strings and form bodies on
 *        the wire are orders of magnitude smaller; a caller that genuinely
 *        needs more is not building a URL-encoded payload - but this is a
 *        POLICY number, not a wrap guard (any cap <= USIZE_MAX/3 prevents the
 *        wrap), so it is overridable per the tree's CDEFINES convention:
 *        -D HTTP_QUERY_ENCODE_MAX_SIZE=<bytes> at build time wins over the
 *        default below. Raise it deliberately; a general encoder refusing a
 *        2 MiB "data:" payload with a bare false is otherwise a surprise.
 */
#ifndef HTTP_QUERY_ENCODE_MAX_SIZE
#define HTTP_QUERY_ENCODE_MAX_SIZE 1048576u
#endif // HTTP_QUERY_ENCODE_MAX_SIZE

/*==============================================================================
 * MARK: - Public Functions
 *============================================================================*/

/**
 * @brief Append one "name=value" pair to a QUERY STRING held in `out`,
 *        prefixing "&" only where `out` already ends a pair. Both sides are
 *        percent-encoded with the RFC 3986 convention, where a space becomes
 *        "%20" - the only difference from http_query_form_add_1, which writes
 *        "+" for a body instead.
 * @param out   Caller-owned String holding the query string being built.
 *              Existing content is kept; a "&" separator is added only when
 *              `out` is non-empty AND its last byte is neither '?' nor '&', so
 *              the first pair carries no leading "&" and a caller-written
 *              separator is never doubled. Seed it with everything up to and
 *              including the "?" ("https://example.com/search?") and the first
 *              pair follows that '?' directly.
 * @param name  Parameter name, NUL-terminated and non-empty.
 * @param value Parameter value, NUL-terminated. May be empty, which appends
 *              "name=".
 * @return true when the pair was appended; false, with `out` untouched, on a
 *         null `out`/`name`/`value`, an empty `name`, a name or value longer
 *         than HTTP_QUERY_ENCODE_MAX_SIZE, a name or value that overlaps
 *         `out`'s own allocation, or an `out` whose allocator refuses to grow
 *         it to hold the whole pair. Never aborts.
 */
bool http_query_add_1(String *const out, char const *const name, char const *const value);

/**
 * @brief Sized twin of http_query_add_1: neither side needs to be
 *        NUL-terminated, so a value carrying embedded NULs is encoded in full.
 * @param out        Caller-owned String holding the query string being built.
 * @param name       Parameter name bytes; name_size must be non-zero.
 * @param name_size  Length of name in bytes. 0 is refused (a nameless pair is
 *                   not a query parameter).
 * @param value      Value bytes. May be null only when value_size is 0.
 * @param value_size Length of value in bytes; 0 appends "name=".
 * @return Same contract as http_query_add_1.
 */
bool http_query_add_2(String *const out, char const *const name, USize const name_size, char const *const value, USize const value_size);

/**
 * @brief Percent-decode ("%XX" -> byte) a sized value into a caller-owned buffer, whole or
 *        not at all. The single decoder of this module: http_query_alloc_decode(_2) and
 *        http_query_alloc_form_decode(_2) allocate and call it.
 * @param dest          Caller buffer receiving the decoded bytes and a terminating NUL. May
 *                      be null only when dest_size is 0. May be `encoded` itself (an
 *                      in-place decode is safe: writing never outruns reading); any OTHER
 *                      overlap with `encoded` is undefined.
 * @param dest_size     Capacity of dest in bytes, terminator included. A dest_size strictly
 *                      larger than encoded_size always fits (decoding never grows a value).
 * @param encoded       Encoded bytes, not necessarily NUL-terminated; a raw NUL among them
 *                      is decoded like any other byte. May be null only when encoded_size
 *                      is 0.
 * @param encoded_size  Length of encoded in bytes. 0 is a legal empty value. Every byte is
 *                      data, so for a NUL-terminated or fixed-capacity buffer pass
 *                      char_length(buffer), not its capacity.
 * @param plus_as_space true for the application/x-www-form-urlencoded convention ('+' ->
 *                      space; "%2B" stays a literal '+'), false for a plain query value.
 *                      A malformed escape - '%' not followed by two hex digits, or with
 *                      fewer than two bytes left - passes through as a literal '%'.
 * @return The decoded byte count (which, with a decoded "%00", is larger than char_length
 *         of the result), dest NUL-terminated at that index. USIZE_MAX when dest cannot hold
 *         the whole decoded value plus its terminator - a dest_size of 0 included - with
 *         dest[0] set to '\0' when dest_size > 0 and nothing else written: never a
 *         truncated decode.
 */
USize http_query_decode(char *const dest, USize const dest_size, char const *const encoded, USize const encoded_size, bool const plus_as_space);

/**
 * @brief Percent-encode a NUL-terminated value per RFC 3986 and APPEND it to
 *        `out`. The unreserved set (A-Z a-z 0-9 '-' '.' '_' '~') passes
 *        through; every other byte becomes "%XX" with uppercase hex digits, a
 *        space included ("%20", never "+" - use http_query_form_add for the
 *        application/x-www-form-urlencoded convention).
 * @param out   Caller-owned String the encoded bytes are appended to. Existing
 *              content is kept, never cleared.
 * @param value Value to encode, NUL-terminated. May be empty (appends nothing,
 *              answers true); must not be null.
 * @return true when the value was encoded (an empty value included); false,
 *         with `out` untouched, on a null `value`, a null `out`, a value
 *         longer than HTTP_QUERY_ENCODE_MAX_SIZE, a value that overlaps `out`'s
 *         own allocation (the append that grows `out` would release the bytes
 *         still being read), or an `out` whose allocator refuses to grow it to
 *         hold the whole encoding. Never aborts.
 */
bool http_query_encode_1(String *const out, char const *const value);

/**
 * @brief Sized twin of http_query_encode_1: takes an explicit byte count
 *        instead of assuming NUL-termination, so a value carrying embedded
 *        NULs is encoded in full ("%00") rather than cut short.
 * @param out        Caller-owned String the encoded bytes are appended to.
 * @param value      Value bytes. May be null only when value_size is 0.
 * @param value_size Length of value in bytes; 0 appends nothing and answers
 *                   true, without consulting the alias guard - a slice that
 *                   reads no bytes cannot dangle, even one pointing into `out`.
 * @return Same contract as http_query_encode_1.
 */
bool http_query_encode_2(String *const out, char const *const value, USize const value_size);

/**
 * @brief Find a key's raw (not decoded) value in a sized query string without allocating,
 *        answering it as a VIEW slice. Same match as http_query_alloc_get_5, but a
 *        present-but-empty "key=" is told apart from an absent key. No char*-key twin
 *        is planned: a literal key is spelled with CHAR_STATIC_SIZE("kind").
 * @param query      Query bytes, not necessarily NUL-terminated. Read up to query_size or
 *                   its first NUL, whichever comes first (the alloc_get rule), and one
 *                   leading '?' is skipped - unlike http_query_next and http_query_decode,
 *                   which read every byte as data. So a fixed-capacity, zero-padded buffer
 *                   (http_server_request_query_copy's) may be passed by its capacity here.
 *                   May be null only when query_size is 0.
 * @param query_size Length of query in bytes; 0 finds nothing.
 * @param key        Key to find, matched RAW (a percent-encoded name only matches its own
 *                   encoded spelling) against a pair's WHOLE name - the bytes before the
 *                   pair's first '=' - never a prefix of it or of the pair: "a" does not
 *                   find "ab=1", and a key that itself contains '=' or '&' finds nothing
 *                   ("a=b" is absent from "a=b=c", whose name is "a"). The comparison stops
 *                   at key's own NUL, so a key_size longer than key's real extent fails
 *                   closed and never reads past it. May be null only when key_size is 0. An
 *                   empty key finds a pair with an empty name ("=v").
 * @param key_size   Length of key in bytes.
 * @param out_value  Set to a read-only VIEW of the first match's value - possibly empty, for
 *                   "key=" - or to the EMPTY Str when the key is absent. Must be non-null.
 * @return true when the key is present (first match wins), its value empty or not; false
 *         when it is absent. A pair with no '=' ("key") is not a match.
 */
bool http_query_find(char const *const query, USize const query_size, char const *const key, USize const key_size, Str *const out_value);

/**
 * @brief Append one "name=value" pair to an application/x-www-form-urlencoded
 *        body, prefixing "&" only where `body` already ends a pair. Both sides
 *        are percent-encoded with the form convention, where a space becomes
 *        "+" (the only difference from http_query_add_1).
 * @param body  Caller-owned String holding the body being built. Existing
 *              content is kept; a "&" separator is added only when `body` is
 *              non-empty AND its last byte is neither '?' nor '&', so the first
 *              pair carries no leading "&" and a caller-written separator is
 *              never doubled.
 * @param name  Field name, NUL-terminated and non-empty.
 * @param value Field value, NUL-terminated. May be empty, which appends
 *              "name=" - a present-but-empty field, which is what the wire
 *              format spells.
 * @return true when the pair was appended; false, with `body` untouched, on a
 *         null `body`/`name`/`value`, an empty `name`, a name or value longer
 *         than HTTP_QUERY_ENCODE_MAX_SIZE, a name or value that overlaps
 *         `body`'s own allocation, or a `body` whose allocator refuses to grow
 *         it to hold the whole pair. Never aborts.
 */
bool http_query_form_add_1(String *const body, char const *const name, char const *const value);

/**
 * @brief Sized twin of http_query_form_add_1: neither side needs to be
 *        NUL-terminated, so a value carrying embedded NULs is encoded in full.
 * @param body       Caller-owned String holding the body being built.
 * @param name       Field name bytes; name_size must be non-zero.
 * @param name_size  Length of name in bytes. 0 is refused (a nameless pair is
 *                   not a form field).
 * @param value      Value bytes. May be null only when value_size is 0.
 * @param value_size Length of value in bytes; 0 appends "name=".
 * @return Same contract as http_query_form_add_1.
 */
bool http_query_form_add_2(String *const body, char const *const name, USize const name_size, char const *const value, USize const value_size);

/**
 * @brief Answer the next "name=value" pair of a sized query string or form body as raw
 *        (NOT decoded) VIEW slices, advancing a caller-held cursor past it. Allocates and
 *        copies nothing; decode a slice with http_query_decode when the decoded form is
 *        wanted.
 *
 * The walk is byte-transparent, unlike find/alloc_get: every one of the query_size bytes
 * is data - a raw NUL does not end the query, and a leading '?' is NOT skipped (it stays part
 * of the first name; pass query + 1 AND query_size - 1 to skip it - over a sized body the
 * unadjusted size is a one-byte over-read; the buffer http_server_request_query_copy fills
 * carries no '?' at all). Pairs are split on '&'; empty segments ("&&",
 * a leading or trailing '&') are skipped, never answered. Within a pair the name runs to the
 * FIRST '=' and the value is everything after it, further '=' included ("a==b" is name "a",
 * value "=b").
 *
 * Telling "k" from "k=" (contract): both answer a ZERO-SIZE value, and out_value's data is
 * nullptr exactly when the pair had no '='. "k" answers the EMPTY Str (data nullptr); "k="
 * answers a non-null, zero-size VIEW (pointing just past the '='). So
 * `memory_empty(str_get_data(&value))` is THE "no '='" test (the size cannot tell the two
 * apart), and next never normalises an empty view to nullptr. "=" alone answers an empty, non-null name and an empty,
 * non-null value: a present pair with an empty name and an empty value.
 *
 * @code
 * USize cursor = 0;
 * Str   name   = str_init_1();
 * Str   value  = str_init_1();
 *
 * while (http_query_next(body, body_size, &cursor, &name, &value)) {
 *     // name/value are VIEWs into body: str_get_data + str_get_size, never char_length
 * }
 * @endcode
 *
 * @param query      Query or body bytes, every one of them data: a raw NUL does not end the
 *                   walk. May be null only when query_size is 0.
 * @param query_size Length of query in bytes. For a NUL-terminated or fixed-capacity buffer
 *                   (http_server_request_query_copy's) pass char_length(buffer), not its
 *                   capacity - the zero padding would otherwise be read as data.
 * @param cursor     Position to resume from: 0 to start, then whatever the previous call
 *                   left. Advanced past the answered pair and the '&' that ended it. When no
 *                   pair is left it is set to query_size - or left untouched if it was already
 *                   past query_size. Must be non-null.
 * @param out_name   Set to a read-only VIEW of the pair's name (possibly empty, for "=v"), or
 *                   to the EMPTY Str when no pair is left. Must be non-null.
 * @param out_value  Set to a read-only VIEW of the pair's value (possibly empty, data still
 *                   non-null, for "k="), or to the EMPTY Str (data nullptr) for a pair
 *                   without '=' and when no pair is left. Must be non-null.
 * @return true when a pair was answered; false when none is left (an empty query, an
 *         all-'&' tail, or a cursor at or past query_size).
 */
bool http_query_next(char const *const query, USize const query_size, USize *const cursor, Str *const out_name, Str *const out_value);

#ifdef ARENA_IMPLEMENTATION

/*==============================================================================
 * MARK: - Public Functions (Arena Tier)
 *============================================================================*/

/**
 * @brief Percent-decode a URL query value ("%XX" -> byte). No "+" handling -
 *        use http_query_alloc_form_decode for form-encoded values.
 * @param encoded Encoded value, NUL-terminated. May be null/empty.
 * @param allocator Arena the returned string is allocated in.
 * @return Arena-allocated, NUL-terminated decoded string, or nullptr on a
 *         null encoded or a refused allocator. An empty encoded answers an
 *         empty, arena-allocated "" - never nullptr. The decoded byte count
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
 *         null encoded or a refused allocator. An empty encoded answers an
 *         empty, arena-allocated "" - never nullptr. Same "%00" truncation
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