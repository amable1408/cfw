/*
 * compression.h - HTTP response compression service for the C Libraries Framework
 * @version 0.3.1
 *
 * Negotiates a response content coding from the client's Accept-Encoding header,
 * compresses a whole response body buffer-to-buffer, and - through one send seam -
 * emits the Content-Encoding and Vary headers that go with it.
 *
 * Features:
 *   - Brotli ("br") and real RFC 1952 gzip ("gzip"). The gzip path runs
 *     deflateInit2 with windowBits 15 + 16, so the body carries the 1f 8b gzip
 *     magic a browser expects - NOT zlib's compress(), whose RFC 1950 wrapper the
 *     "gzip" token would be lying about.
 *   - Tokenized Accept-Encoding: comma-separated codings, optional ";q=" weights,
 *     case-insensitive names, q=0 as an exclusion, "*" as the fallback weight,
 *     "identity" understood. Highest weight wins; Brotli breaks a tie.
 *   - Configurable size window (min_size .. max_size) and Content-Type allow-list,
 *     with a "+json"/"+xml" structured-suffix rule on top of the prefix list.
 *   - Configurable effort: brotli_quality and gzip_level.
 *   - One send seam that does negotiate -> compress -> headers -> send, and falls
 *     back to identity on every failure - including a 206 Partial Content reply,
 *     which is never coded (see the send seam's documentation).
 *   - Arena and heap allocation support, SPLIT by lifetime - the arena backs configuration
 *     only, and everything on the codec path is heap-owned; Memory Management below is the
 *     single statement of that policy.
 *
 * Usage Example:
 *   @code
 *   HTTP_Service_Compression compression = DEFAULT_INITIALIZATION;
 *
 *   if (!http_service_compression_init_1(&compression)) {
 *       // Memory Management: false means this instance does not exist - fail startup.
 *   }
 *
 *   http_service_compression_mime_type_add(&compression, "application/x-ndjson");
 *
 *   // ... inside a route callback, with `request` and `response` in hand:
 *   http_service_compression_send(&compression, request, response, (Byte const*) body, body_size,
 *       "text/html; charset=utf-8", HTTP_SERVER_STATUS_CODE_OK);
 *
 *   http_service_compression_uninit(&compression);
 *   @endcode
 *
 * Effort defaults (measured 2026-09-06, 16-core UCRT64, best of five/three passes):
 *   - 100 KiB HTML: gzip-6 11.13x in 0.60 ms; br q4 25.23x in 0.66 ms; br q5 21.74x in
 *     1.38 ms; br q11 23.50x in 114.55 ms.
 *   - 1 MiB JSON: gzip-6 9.51x in 7.96 ms; br q4 11.73x in 5.17 ms; br q5 18.22x in
 *     9.86 ms; br q11 27.67x in 1362.13 ms.
 *   - Hence brotli_quality 5 (a gzip-6-priced call that beats gzip-6 by roughly 2x on
 *     the larger body) and gzip_level 6. Quality 11 stays REACHABLE but is never the
 *     default: on the server's single request thread a 1 MiB reply at q11 stalls every
 *     other connection for over a second, for 1.5x the ratio q5 already gets.
 *   - Clamping lgwin to the payload cost nothing measurable (4342 vs 4341 bytes on the
 *     HTML body, byte-identical on the JSON one) and drops the encoder's window
 *     allocation for every body smaller than 4 MiB.
 *
 * Error Handling:
 *   - Public functions validate non-null pointers.
 *   - REQUEST DATA NEVER ABORTS: an unparsable, hostile, or empty Accept-Encoding or
 *     Content-Type is answered with HTTP_SERVICE_COMPRESSION_TYPE_NONE, never an
 *     error_check. Only the service instance itself is contract-checked.
 *   - Configuration values are refused, not aborted: an out-of-range quality or level,
 *     a max_size below min_size, or an empty MIME type returns false.
 *
 * Thread Safety:
 *   - Both encoders are one-shot and reentrant, and no state is kept between calls, so
 *     compress/negotiate/send are safe to run concurrently on ONE shared const service. This
 *     holds for an ARENA-backed service too: by the allocation policy in Memory Management the
 *     codec path never touches the arena, so two concurrent calls cannot bump one Arena (which
 *     is not thread-safe) between them.
 *   - Configuration (mime_type_add) is not synchronized: register every type during
 *     startup, before the server begins serving.
 *
 * Memory Management:
 *   - MIME type prefixes are copied into service-owned storage; call
 *     http_service_compression_uninit() when finished.
 *   - THE ARENA BACKS THE MIME ALLOW-LIST AND NOTHING ELSE. A codec buffer is per-RESPONSE and
 *     as wide as max_size, while the allocator handed to alloc_init_* is SERVICE-lifetime and,
 *     for a linear Arena, reclaims nothing - so routing the codec through it would make a
 *     long-lived server retain every body it ever compressed. Both codec buffers and the String
 *     compress writes are therefore heap-owned whichever constructor built the service, and
 *     string_uninit really releases them.
 *   - A false return from any init/alloc_init leaves *self ZEROED, not initialized -
 *     treat it as "this instance does not exist" and fail startup. Do not call any
 *     other function on it, uninit included.
 *   - http_service_compression_compress writes into a caller-owned String either way:
 *     on false it is the EMPTY String. Calling string_uninit on it is always correct.
 *   - The whole body is held twice during a compress call (input plus a bound-sized
 *     output buffer), which is why max_size exists - see below.
 *
 * Performance Characteristics:
 *   - Whole-body, buffer-to-buffer. There is no streaming tier: the send seam ends in
 *     one http_server_response_send_2, which is itself one write.
 *   - Peak footprint per call is data_size + the codec's bound (data_size + data_size/1000
 *     + 18 for gzip, a similar over-estimate for Brotli) plus the encoder's own state.
 *     max_size (8 MiB by default) is the cap that keeps a legal multi-GiB body from
 *     turning into a heap abort; above it negotiate answers NONE and compress refuses,
 *     so the body goes out as identity through either tier.
 *   - Output that is not SMALLER than the input is discarded and the body goes out as
 *     identity: min_size guards small bodies, this guards incompressible ones.
 *   - The Brotli window (lgwin) is clamped to ceil(log2(data_size)), bounded by
 *     BROTLI_MIN_WINDOW_BITS and 22, so a small body does not pay for a 4 MiB window.
 *
 * Security:
 *   - COMPRESSION IS A SIZE ORACLE (BREACH, CRIME). When one response body mixes a secret -
 *     a CSRF token, a session id, an account number - with input the attacker controls and
 *     sees reflected, the compressed LENGTH leaks how well the two match, and a few thousand
 *     guesses recover the secret byte by byte. TLS does not hide it: the length is on the
 *     wire whatever the cipher is.
 *   - The bypass is per response, not global: call http_server_response_send_2 directly for
 *     such a route, or give it a service whose MIME allow-list omits the type (text/html is
 *     the usual carrier). Neither needs this service reconfigured for the rest of the site.
 *   - The usual mitigations - a per-request CSRF token, keeping reflected input out of any
 *     body that carries a secret - are the caller's, not this module's: it cannot see which
 *     bytes of a payload are secret.
 *
 * Platform:
 *   - Portable C23 with no platform-specific code. It needs zlib and the Brotli encoder as
 *     system packages: `zlib1g-dev libbrotli-dev` on Debian and derivatives,
 *     `mingw-w64-ucrt-x86_64-zlib` and `mingw-w64-ucrt-x86_64-brotli` under MSYS2/UCRT64.
 *   - The suite is verified on Debian 13 and on MinGW (UCRT64).
 *
 * Dependencies:
 *   - zlib (-lz) and the Brotli encoder (-lbrotlienc -lbrotlicommon) as SYSTEM packages;
 *     arrayList, char, http/server, str, string.
 *   - <zlib.h> and <brotli/encode.h> are included by compression.c, NOT by this header:
 *     nothing in this API exposes a type from either, so no consumer of the service
 *     umbrella should have to resolve their include paths or link their packages.
 *
 * See compression.c for implementation details.
 */

#ifndef HTTP_SERVICE_COMPRESSION_H
#define HTTP_SERVICE_COMPRESSION_H

#include <container/arrayList/al_str.h>
#include <container/string/string.h>
#include <http/server/http_server.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

#define HTTP_SERVICE_COMPRESSION_DEFAULT_BROTLI_QUALITY 5
#define HTTP_SERVICE_COMPRESSION_DEFAULT_GZIP_LEVEL     6
#define HTTP_SERVICE_COMPRESSION_DEFAULT_MAX_SIZE       (8 * 1024 * 1024)
#define HTTP_SERVICE_COMPRESSION_DEFAULT_MIN_SIZE       1024
#define HTTP_SERVICE_COMPRESSION_MAX_BROTLI_QUALITY     11
#define HTTP_SERVICE_COMPRESSION_MAX_GZIP_LEVEL         9
#define HTTP_SERVICE_COMPRESSION_MIN_BROTLI_QUALITY     0
#define HTTP_SERVICE_COMPRESSION_MIN_GZIP_LEVEL         0

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief Compression schemes available for negotiation.
 */
typedef enum {
    HTTP_SERVICE_COMPRESSION_TYPE_NONE = 0,
    HTTP_SERVICE_COMPRESSION_TYPE_GZIP,
    HTTP_SERVICE_COMPRESSION_TYPE_BROTLI
} HTTP_Service_Compression_Type;

/**
 * @brief Compression policy configuration.
 */
typedef struct {
#ifdef ARENA_IMPLEMENTATION
    /** @brief Optional arena backing the MIME allow-list only; codec output is heap-owned. */
    Arena *allocator;
#endif // ARENA_IMPLEMENTATION
    /** @brief Brotli quality, HTTP_SERVICE_COMPRESSION_MIN_BROTLI_QUALITY..MAX. */
    int brotli_quality;
    /** @brief zlib deflate level, HTTP_SERVICE_COMPRESSION_MIN_GZIP_LEVEL..MAX. */
    int gzip_level;
    /** @brief Largest payload that may be compressed; 0 means no upper bound. */
    USize max_size;
    /** @brief Minimum payload byte size required to trigger compression. */
    USize min_size;
    /** @brief Compressible Content-Type prefixes. */
    AL_Str mime_types;
} HTTP_Service_Compression;

/*==============================================================================
 * MARK: - API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Initialize an arena-backed compression service with default values, in place.
 * @param self Service instance to initialize.
 * @param allocator Arena allocator.
 * @return true when the whole service exists; false when any default MIME type was
 *         refused, in which case *self is left ZEROED and must not be used or uninit'ed.
 */
bool http_service_compression_alloc_init_1(HTTP_Service_Compression *const self, Arena *const allocator);

/**
 * @brief Initialize an arena-backed compression service with explicit values, in place.
 * @param self Service instance to initialize.
 * @param min_size Minimum payload size required for compression.
 * @param max_size Largest payload that may be compressed; 0 means no upper bound.
 * @param brotli_quality Brotli quality, 0..11.
 * @param gzip_level zlib deflate level, 0..9.
 * @param allocator Arena allocator.
 * @return See http_service_compression_alloc_init_1; also false when a configuration
 *         value is out of range or max_size is non-zero and below min_size.
 */
bool http_service_compression_alloc_init_2(HTTP_Service_Compression *const self, USize const min_size, USize const max_size,
    int const brotli_quality, int const gzip_level, Arena *const allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Compress a payload with the given scheme.
 * @param self Service instance.
 * @param type Compression scheme to apply; NONE always fails.
 * @param data Uncompressed payload data.
 * @param data_size Byte count of payload.
 * @param out Receives the compressed payload. WRITE-ONLY: whatever String it held on entry
 *            is OVERWRITTEN, never released, so pass a fresh String or one already uninit'ed -
 *            reusing a String that still owns a buffer LEAKS that buffer. Written on BOTH
 *            outcomes - the EMPTY String on false - so string_uninit(out) is always the
 *            correct cleanup AFTERWARDS.
 * @return true when out holds a compressed payload STRICTLY SMALLER than data_size.
 *         false for a zero size, a size past max_size (when max_size is non-zero), a
 *         size past the codec's range, an allocator refusal, a codec failure, or output
 *         that did not shrink; send the payload as identity.
 *
 * max_size is honoured HERE as well as in negotiate, so the raw tier cannot allocate the
 * bound for a body the service was configured never to hold twice. min_size is NOT: a
 * caller who reaches past the seam has already decided this payload is worth coding.
 *
 * `out` is HEAP-owned even for an arena-backed service - the codec path never allocates from
 * self's allocator - so string_uninit(out) genuinely releases it on either tier.
 */
bool http_service_compression_compress(HTTP_Service_Compression const *const self, HTTP_Service_Compression_Type const type,
    Byte const *const data, USize const data_size, String *const out);

/**
 * @brief Initialize a compression service with default values, in place.
 * @param self Service instance to initialize.
 * @return See http_service_compression_alloc_init_1.
 */
bool http_service_compression_init_1(HTTP_Service_Compression *const self);

/**
 * @brief Initialize a compression service with explicit values, in place.
 * @param self Service instance to initialize.
 * @param min_size Minimum payload size required for compression.
 * @param max_size Largest payload that may be compressed; 0 means no upper bound.
 * @param brotli_quality Brotli quality, 0..11.
 * @param gzip_level zlib deflate level, 0..9.
 * @return See http_service_compression_alloc_init_2.
 */
bool http_service_compression_init_2(HTTP_Service_Compression *const self, USize const min_size, USize const max_size,
    int const brotli_quality, int const gzip_level);

/**
 * @brief Add a compressible MIME type prefix (e.g. "text/").
 * @param self Service instance.
 * @param mime_type MIME type prefix string; matched case-insensitively.
 * @return true when stored; false when the value is empty or the allocator refused,
 *         leaving the list unchanged. Responses of that type then go out UNCOMPRESSED,
 *         which is a bandwidth regression rather than a correctness one.
 */
bool http_service_compression_mime_type_add(HTTP_Service_Compression *const self, char const *const mime_type);

/**
 * @brief Negotiate the best available compression scheme from null-terminated header text.
 * @param self Service instance.
 * @param accept_encoding The Accept-Encoding header value; null or empty yields NONE.
 * @param content_type The Content-Type header value; null yields NONE.
 * @param content_size Byte count of the response payload.
 * @return Negotiated compression scheme, or NONE when the payload is outside the size
 *         window, the type is not compressible, or the client accepts neither coding.
 */
HTTP_Service_Compression_Type http_service_compression_negotiate_1(HTTP_Service_Compression const *const self,
    char const *const accept_encoding, char const *const content_type, USize const content_size);

/**
 * @brief Negotiate from sized header text - the header_copy buffer form.
 * @param self Service instance.
 * @param accept_encoding The Accept-Encoding header value; need not be terminated.
 * @param accept_encoding_size Byte count of accept_encoding.
 * @param content_type The Content-Type header value; need not be terminated.
 * @param content_type_size Byte count of content_type.
 * @param content_size Byte count of the response payload.
 * @return See http_service_compression_negotiate_1.
 */
HTTP_Service_Compression_Type http_service_compression_negotiate_2(HTTP_Service_Compression const *const self,
    char const *const accept_encoding, USize const accept_encoding_size, char const *const content_type,
    USize const content_type_size, USize const content_size);

/**
 * @brief Negotiate from Str header values.
 * @param self Service instance.
 * @param accept_encoding The Accept-Encoding header value; the EMPTY Str yields NONE.
 * @param content_type The Content-Type header value; the EMPTY Str yields NONE.
 * @param content_size Byte count of the response payload.
 * @return See http_service_compression_negotiate_1.
 */
HTTP_Service_Compression_Type http_service_compression_negotiate_3(HTTP_Service_Compression const *const self,
    Str const *const accept_encoding, Str const *const content_type, USize const content_size);

/**
 * @brief Negotiate, compress, add Content-Encoding and Vary, and send the response.
 *
 * The one call a route or the static service makes instead of hand-rolling
 * header_copy -> negotiate -> compress -> header_add -> send_2 -> string_uninit. Every
 * failure inside it - an unreadable Accept-Encoding, an allocator refusal, a codec
 * error, output that did not shrink, a refused header - degrades to sending the
 * ORIGINAL payload as identity, so the response is sent exactly once either way.
 *
 * `Vary: Accept-Encoding` is emitted whenever the content type is COMPRESSIBLE, on the
 * identity path too: a shared cache that saw a plain client first must not go on to
 * serve that stored copy to a client that would have received a compressed one.
 *
 * A 206 Partial Content reply is NEVER coded - it goes out as identity, Vary included.
 * RFC 9110 section 14.4: a Content-Range offset indexes the SELECTED representation, and
 * Content-Encoding is what makes the coded bytes that representation. Compressing the
 * slice while labelling it `bytes a-b/n` of the identity file yields a body no client
 * can reassemble, so the seam refuses on the status code rather than trusting the caller
 * to know. (Compressing the WHOLE representation and then slicing it is a different
 * design, and not one this whole-body seam can express.)
 *
 * THE PAYLOAD MUST BE THE IDENTITY REPRESENTATION. The seam applies a content coding and labels
 * it; it cannot see one that is ALREADY there - `data` is opaque bytes and http/server offers no
 * read-back of the headers a caller has queued - so a body that already carries a
 * Content-Encoding (a precompressed .br/.gz sidecar read off disk, a proxied upstream reply
 * passed through untouched) is coded a SECOND time and labelled with the outer coding alone,
 * which no client can undo. Send such a body through http_server_response_send_2 with its own
 * Content-Encoding header instead.
 *
 * A BODY-LESS reply is never coded either. A 204 No Content or a 304 Not Modified carries no
 * representation to code: data_size 0 is below any min_size above 0, and even at min_size 0
 * compress refuses a zero size, so the reply goes out as identity on both routes. At min_size 0
 * a compressible type still gets its Vary header, which is what a shared cache wants.
 *
 * The request METHOD is deliberately NOT inspected. A HEAD reply must carry the headers the GET
 * it mirrors would carry, so the seam negotiates, compresses and labels it identically and lets
 * the server decide the body is not written - short-circuiting HEAD to identity would advertise
 * a DIFFERENT Content-Encoding than the GET, which is worse than the work it saves. The codec
 * cost is real, though: a caller serving many HEAD requests should answer them through
 * http_server_response_send_2 with the headers it wants, rather than through this seam.
 *
 * @param self Service instance.
 * @param request Request the Accept-Encoding header is read from.
 * @param response Response the headers are queued on and the body is sent through.
 * @param data Response payload; may be null when data_size is 0.
 * @param data_size Byte count of payload.
 * @param content_type Full Content-Type header value, charset parameter included.
 * @param status_code HTTP status code to send; 206 forces the identity path, see above.
 * @return true when a COMPRESSED body was sent, false when the body was sent as
 *         identity. Either way the response has been sent - this is not an error flag.
 */
bool http_service_compression_send(HTTP_Service_Compression const *const self, HTTP_Server_Request *const request,
    HTTP_Server_Response *const response, Byte const *const data, USize const data_size, char const *const content_type,
    U16 const status_code);

/**
 * @brief The Content-Encoding token for a scheme.
 * @param type Compression scheme.
 * @return "br", "gzip", or "identity" for NONE. Read-only, statically allocated, never
 *         null; do not free or modify it. "identity" is the honest name for NONE but is
 *         NOT a Content-Encoding header value the send seam ever emits.
 */
char* http_service_compression_type_name(HTTP_Service_Compression_Type const type);

/**
 * @brief Release all service storage.
 * @param self Service instance.
 */
void http_service_compression_uninit(HTTP_Service_Compression *const self);

#endif // HTTP_SERVICE_COMPRESSION_H