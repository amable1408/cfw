/*
 * static.h - HTTP static file service for the C Libraries Framework
 * @version 0.3.2
 *
 * Maps HTTP request paths to static assets on disk, resolving their MIME types,
 * validating path security, and serving the file data over HTTP.
 *
 * Features:
 *   - Serves directory assets (HTML, CSS, JS, images, fonts).
 *   - Optional cache-control headers, honoured on both the full-file and Range (206) paths.
 *   - Conditional GET: Last-Modified + a weak ETag (size+mtime), 304 on a matching
 *     If-None-Match or (serve_2 only - see below) an unmet If-Modified-Since, Range served only
 *     when If-Range (if present) still matches.
 *   - Path traversal prevention.
 *   - SPA fallback support, restricted to extension-less paths.
 *   - Arena and heap allocation.
 *
 * Usage Example:
 *   @code
 *   HTTP_Service_Static static_svc = http_service_static_init_1("public", "/static");
 *
 *   if (http_service_static_serve_2(&static_svc, request, response)) {
 *       // Static file served successfully
 *   }
 *
 *   http_service_static_uninit(&static_svc);
 *   @endcode
 *
 * Error Handling:
 *   - Public functions validate non-null pointers.
 *   - Missing files or directory traversal attempts fail gracefully.
 *   - An over-long root_dir/route_prefix/default_file is truncated to fit (never a buffer
 *     overrun) and logs LOG_LEVEL_WARN at construction time - see http_service_static_init_1/_2.
 *
 * Symlinks:
 *   - Followed by default, like a plain fopen would. Set self.refuse_symlinks = true after
 *     construction (there is no separate setter; direct field assignment is the established
 *     pattern for this service family's optional knobs) to refuse serving a path any of whose
 *     components - not just the final one - is a symlink/junction below root_dir, at the cost of
 *     one directory listing per path component per request. Prefer this on any root the
 *     deployment does not fully control.
 *   - Accepted risk: this is a check-then-open, not an atomic operation. A symlink swapped into
 *     the path between this check returning "clear" and the subsequent file open (TOCTOU) is not
 *     caught. Closing that gap needs an O_NOFOLLOW-equivalent open, which file/file.h does not
 *     expose today; not implemented here.
 *
 * Range requests:
 *   - Capped at 4 MiB per response (_HTTP_SERVICE_STATIC_RANGE_CHUNK_MAX in static.c), so a
 *     100 MB file needs ~25 round trips. Whether libwebsockets - a system dependency, not
 *     vendored - was built with LWS_WITH_RANGES cannot be assumed; a build
 *     that has it would let this delete the hand-rolled Range/416 path here and stream straight
 *     from disk instead. Not done in this pass; the hand-rolled path runs unconditionally.
 *   - Multi-range requests (a comma in the Range value) are not split into a multipart/byteranges
 *     response; they fall back to a full 200, which is RFC 9110-compliant (a server may always
 *     ignore Range) but wastes bandwidth for that one shape of request.
 *
 * Consumers wanting more than one root currently hand-roll `serve_2(a) || serve_2(b)` (a dozen
 * call sites in this tree do exactly that). A `serve_chain`/multi-root init is a reasonable future
 * addition; not implemented here.
 *
 * serve_1 vs serve_2:
 *   - serve_2 takes an HTTP_Server_Request and reads every conditional/Range header through the
 *     clean http_server_request_header_copy API, including If-Modified-Since (an RFC 7231
 *     IMF-fixdate parsed via datetime_from_http_try - RFC 850/asctime forms are refused, not just
 *     ignored).
 *   - serve_1 is DEPRECATED as of 0.3.1 and will be deleted in 0.4.0. It takes no request (only
 *     a raw path), so it cannot read If-Modified-Since at all: a client sending only
 *     If-Modified-Since - `wget -N`, an IMS-only proxy - re-downloads the whole body through
 *     serve_1 where serve_2 answers 304. It still supports If-None-Match, Range, and If-Range,
 *     reading them directly off the response's underlying transport handle, which is the last
 *     raw lws_hdr_copy left in the service tier. Every production caller in this tree (28 sites
 *     across 16 files) migrated to serve_2 in the 0.3.1 pass; what remains on serve_1 is one
 *     call that serves a FIXED path rather than the request's (main.c's root route), plus the
 *     suite's unchecked build, which has no live request to hand serve_2.
 *   - Prefer serve_2 for any new integration; it is also what the Usage Example above shows.
 *
 * Thread Safety:
 *   - Effectively read-only after construction: serve_1/serve_2 only read `self`. Safe for
 *     concurrent calls on one instance from multiple threads, the same as any other immutable
 *     configuration object; construct and destroy from a single thread.
 *
 * Memory Management:
 *   - Every field is a fixed-size inline buffer (root_dir/route_prefix/default_file/
 *     fallback_file) or a scalar - there is nothing heap- or arena-owned once construction
 *     returns, arena-backed alloc_init_1/_2 included, since the arena there backs only the
 *     by-value struct itself, not a separate allocation inside it. http_service_static_uninit is
 *     therefore a no-op besides its null check; it exists so callers have one symmetrical
 *     teardown call regardless of which constructor was used, and so a future field that DOES
 *     own storage has somewhere to release it without an API change.
 *
 * Performance Characteristics:
 *   - _http_service_static_resolve is O(1) path arithmetic plus one or two file_exists_1 stats
 *     (the primary path, and the SPA fallback path when the first misses).
 *   - MIME resolution is a linear scan of the ~46-row extension table
 *     (_http_service_static_mimetype), then a single lws_get_mimetype fallback; case-insensitive
 *     comparison happens per candidate row, not once up front.
 *   - refuse_symlinks (see the Symlinks note) adds one directory listing per path component
 *     below root_dir, which is why it stays opt-in rather than always-on.
 *   - Range responses read at most _HTTP_SERVICE_STATIC_RANGE_CHUNK_MAX (4 MiB) into one heap
 *     buffer per request; the full-file path streams through http_server_response_send_file with
 *     no module-side buffering.
 *   - A full serve costs five stat-family calls: file_exists_1 in path resolution, file_size_1
 *     and file_modified_1 for the validators, then send_file's own file_exists_1 re-checking
 *     what resolution already confirmed, and libwebsockets' own fstat inside
 *     send_file. The re-check is the price of send_file's own 404 contract - it cannot tell
 *     "missing" from "streamed" without it (see http_server_response_send_file). They fold into
 *     one the day file/ grows a single stat primitive; measured as not worth a private syscall
 *     path before that exists.
 *
 * HTTP methods:
 *   - serve_1/serve_2 do not inspect the request method; a consumer gating on GET/HEAD does so
 *     itself. A HEAD needs no gate: http_server's send path emits the headers a GET would and no
 *     body, on the full-file, 206 and 304 paths alike.
 *
 * Dependencies:
 *   - arena, char, datetime, dir, file, http/headers, http/service/compression, http_server.
 *
 * See static.c for implementation details.
 */

#ifndef HTTP_SERVICE_STATIC_H
#define HTTP_SERVICE_STATIC_H

#include <arena/arena.h>
#include <http/server/http_server.h>
#include <types.h>

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief Static file serving service configuration.
 */
typedef struct {
#ifdef ARENA_IMPLEMENTATION
    /** @brief Optional arena used by owned values. */
    Arena *allocator;
#endif // ARENA_IMPLEMENTATION
    /** @brief Cache-Control max-age in seconds. Zero disables cache header. */
    U32 max_age;
    /** @brief Enable Single Page Application fallback routing. */
    bool spa_fallback;
    /** @brief Root directory on disk. */
    char root_dir[HTTP_SERVER_PATH_MAX_LENGTH];
    /** @brief Size of the root directory string. */
    U16 root_dir_size;
    /** @brief URL route prefix matching static assets. */
    char route_prefix[HTTP_SERVER_PATH_MAX_LENGTH];
    /** @brief Size of the route prefix string. */
    U16 route_prefix_size;
    /** @brief Default file served when path maps to a directory (e.g. index.html). */
    char default_file[64];
    /** @brief Size of the default file string. */
    U8 default_file_size;
    /** @brief Fallback file served under SPA routing when files are missing. */
    char fallback_file[64];
    /** @brief Size of the fallback file string. */
    U8 fallback_file_size;
    /**
     * @brief Opt-in: refuse to serve a path any of whose components is a symlink/junction.
     *
     * Off by default (matches the historical behavior: symlinks are followed like a plain
     * fopen). No dedicated constructor parameter - set this field directly after construction,
     * as documented in static.h. Costs one directory listing per path COMPONENT below root_dir
     * when enabled (dir/dir.h only exposes is_link per directory entry, not per single path), so
     * enable it only on roots the deployment does not fully control. See the Symlinks note in
     * this header for the accepted TOCTOU gap.
     */
    bool refuse_symlinks;
} HTTP_Service_Static;

/*==============================================================================
 * MARK: - API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Initialize an arena-backed static file service with defaults.
 * @param root_dir Root directory on disk.
 * @param route_prefix URL route prefix.
 * @param allocator Arena allocator.
 * @return Initialized service.
 */
HTTP_Service_Static http_service_static_alloc_init_1(char const *const root_dir, char const *const route_prefix, Arena *const allocator);

/**
 * @brief Initialize an arena-backed static file service with custom options.
 * @param root_dir Root directory on disk.
 * @param route_prefix URL route prefix.
 * @param default_file Default directory file (e.g., "index.html").
 * @param spa_fallback Enable SPA fallback routing.
 * @param max_age Cache-Control max-age in seconds.
 * @param allocator Arena allocator.
 * @return Initialized service.
 */
HTTP_Service_Static http_service_static_alloc_init_2(
    char const *const root_dir, char const *const route_prefix, char const *const default_file, bool const spa_fallback, U32 const max_age, Arena *const allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Initialize a heap-backed static file service with defaults.
 * @param root_dir Root directory on disk.
 * @param route_prefix URL route prefix.
 * @return Initialized service.
 * @note An over-long root_dir/route_prefix is truncated to fit its fixed buffer AND logs
 *       LOG_LEVEL_WARN, so a deployment serving from (or routing through) a truncated,
 *       nonexistent path gets a diagnostic rather than silence.
 */
HTTP_Service_Static http_service_static_init_1(char const *const root_dir, char const *const route_prefix);

/**
 * @brief Initialize a heap-backed static file service with custom options.
 * @param root_dir Root directory on disk.
 * @param route_prefix URL route prefix.
 * @param default_file Default directory file (e.g., "index.html").
 * @param spa_fallback Enable SPA fallback routing.
 * @param max_age Cache-Control max-age in seconds.
 * @return Initialized service.
 * @note See http_service_static_init_1 for the truncation-logs-WARN note; applies to
 *       default_file here too.
 */
HTTP_Service_Static http_service_static_init_2(char const *const root_dir, char const *const route_prefix, char const *const default_file, bool const spa_fallback, U32 const max_age);

/**
 * @brief Attempt to serve a static file from a raw path string.
 * @param self Service instance.
 * @param response HTTP server response context.
 * @param path Requested URL path.
 * @param path_size Size of the URL path string.
 * @return true if the static file was found and served.
 * @deprecated Since 0.3.1; scheduled for deletion in 0.4.0. Use http_service_static_serve_2,
 *             which takes the request every caller already holds. Kept only for a caller with no
 *             HTTP_Server_Request at all, or one serving a FIXED path rather than the request's.
 * @note No HTTP_Server_Request is available here, so If-Modified-Since is never evaluated (see
 *       static.h's "serve_1 vs serve_2" note). If-None-Match, Range, and If-Range are still fully
 *       supported. Prefer serve_2 when a request is available.
 */
bool http_service_static_serve_1(HTTP_Service_Static const *const self, HTTP_Server_Response *const response, char const *const path, USize const path_size);

/**
 * @brief Attempt to serve a static file from a server request context.
 * @param self Service instance.
 * @param request HTTP server request context.
 * @param response HTTP server response context.
 * @return true if the static file was found and served.
 * @note The preferred entry point: reads If-None-Match, If-Modified-Since, Range, and If-Range
 *       through the clean http_server_request_header_copy API (see static.h's "serve_1 vs
 *       serve_2" note).
 */
bool http_service_static_serve_2(HTTP_Service_Static const *const self, HTTP_Server_Request *const request, HTTP_Server_Response *const response);

/**
 * @brief Release all service storage.
 * @param self Service instance.
 */
void http_service_static_uninit(HTTP_Service_Static *const self);

#endif // HTTP_SERVICE_STATIC_H