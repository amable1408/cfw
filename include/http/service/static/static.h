/*
 * static.h - HTTP static file service for the C Libraries Framework
 * @version 0.5.1
 *
 * Maps HTTP request paths to static assets on disk, resolving their MIME types,
 * validating path security, and serving the file data over HTTP.
 *
 * Features:
 *   - Serves directory assets (HTML, CSS, JS, images, fonts).
 *   - Open once, verify the handle, serve from the handle: every request is ONE nofollow open
 *     (file_open_nofollow_try_1), size and mtime are read from that handle, and the 200 and the
 *     206 alike are streamed from it by http_server_response_send_file_handle. Nothing re-opens
 *     the file by name after it was verified, so the bytes on the wire are the bytes of the file
 *     that passed the checks.
 *   - Symlink/junction refusal ON by default (refuse_symlinks; see Symlinks below).
 *   - Non-ASCII asset names are served on Windows: the opener is wide, so the UTF-8 name the URI
 *     decodes to ("caf%C3%A9.txt") opens the file it names.
 *   - Optional cache-control headers, honoured on the full-file, Range (206) and 304 paths.
 *   - Conditional GET: Last-Modified + a strong ETag (size+mtime), 304 on an If-None-Match that
 *     matches by the WEAK comparison RFC 9110 S13.1.2 requires there (a W/ prefix on either side
 *     is ignored, a comma-separated list matches on any member, `*` matches any served file) or
 *     (serve_2 only - see below) an unmet If-Modified-Since, Range served only when If-Range (if
 *     present) still matches - by the STRONG comparison RFC 9110 S13.1.5 requires there, so a
 *     W/-prefixed If-Range never matches and answers a full 200.
 *   - Path traversal prevention. On Windows, also refused unconditionally: device names and
 *     alias spellings (alternate data streams, trailing dot/space, 8.3 short names), through
 *     file/'s file_name_reserved_windows and file_path_aliased_windows - see file.h for the
 *     exact rules.
 *   - Opt-in dotfile refusal (refuse_dotfiles), `.well-known` exempt.
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
 *   - A root_dir with no resolvable final path at construction logs LOG_LEVEL_WARN once (see
 *     Symlinks). A request refused because it reaches a link logs LOG_LEVEL_WARN naming the
 *     file - distinct from a plain miss, which logs nothing. A request refused because the link
 *     check cannot run at all - that unresolved root, or a platform that cannot name an open
 *     handle's path - logs NO per-request line: the first such refusal logs one LOG_LEVEL_WARN
 *     per process naming the real cause, and every later one is refused silently.
 *
 * Symlinks:
 *   - REFUSED BY DEFAULT since 0.5.0 (BEHAVIOUR CHANGE - through 0.4.x links were followed unless
 *     refuse_symlinks was set). A file reached through a symlink or a junction ANYWHERE below
 *     root_dir - the final component or any directory on the way - answers "not served" (a 404
 *     from the caller), and that includes a link whose target is INSIDE root_dir (a
 *     `shared/assets -> ../shared` layout), not only one that escapes. Before upgrading a
 *     deployment, check its served tree ONCE for links it relies on: on Linux
 *     `find -H <root> -type l` (-H, because plain find does not descend into a root that is
 *     itself a link - the legal `current -> releases/N` layout - and reports only the root); on
 *     Windows `dir /AL /S <root>`, counting only the entries marked <SYMLINK>, <SYMLINKD> or
 *     <JUNCTION> (/AL also lists cloud placeholders - Dropbox, OneDrive - which are not links and
 *     keep serving). Every link found stops being served. A tree that needs them sets
 *     self.refuse_symlinks = false after construction (direct field assignment, the established
 *     pattern for this service family's knobs) and accepts what following means.
 *   - How it is enforced, and why it is closed: the file is opened nofollow, so a link in the
 *     FINAL component is refused by the open itself (O_NOFOLLOW; on Windows a reparse point that
 *     is a name surrogate - symlink or junction), and the open never reaches a link's target - a
 *     link to a FIFO or a device would make the OPEN the hazard. The open still follows a linked
 *     ANCESTOR, like any open, so the handle's FINAL PATH (file_get_final_path - where its
 *     bytes physically live) is then compared with root_dir's final path (taken once at
 *     construction) + the request-relative name; any difference refuses. The bytes served come
 *     from that same handle, so a link swapped in at ANY moment serves nothing: before the open,
 *     the open or the compare sees it; after the open, the handle already points at the original
 *     file and the swap changes nothing on the wire. There is no check-then-open window.
 *   - Windows compares case-insensitively, ASCII only: a request spelled in a different case
 *     than the file on disk is served as before when the letters are ASCII, but a NON-ASCII case
 *     variant ("DONNÉES" for "données") is refused - the fail-closed direction, and a browser
 *     requests the href's exact casing. Empty and "." path segments are skipped on both sides.
 *   - A root_dir that is ITSELF a link stays legal: its final path is the target, and requests
 *     compare below the target. That target is resolved ONCE, at construction: re-pointing the
 *     root link afterwards (`ln -sfn releases/N+1 current`) or rewriting root_dir serves nothing -
 *     every request answers 404 with the per-request "link refused" WARN (one line per request,
 *     so a flipped root shows up as log volume), or a silent 404 if the new root_dir does not
 *     exist - until the service is reconstructed (restart, or rebuild the instance, on deploy).
 *     A root_dir that does not exist (or cannot be resolved) when the
 *     service is constructed refuses every request while refuse_symlinks is on; construction
 *     logs one WARN, and the first such refusal one more naming the root (once per process,
 *     never per request) - create the root first, then construct.
 *   - With refuse_symlinks off, a final-component link is followed by resolving its target and
 *     opening THAT nofollow (the target must itself be a regular file, and not on a network
 *     share: a UNC target is refused silently, with no log line); that second resolution is the
 *     check-then-open an operator accepts by turning the refusal off. On Windows the by-path
 *     resolve opens the link's TARGET before the nofollow open could refuse it, so a link to a
 *     pipe or device connects first (operator-placed links only).
 *   - A HARD link is not a symlink: one inside root_dir to a file outside it has a final path
 *     inside the root and is served; refuse_symlinks does not cover it.
 *   - Where Linux has no /proc mounted (a minimal container, a chroot) the handle's final path
 *     cannot be read, and refuse_symlinks refuses every request there - fail closed, with one
 *     WARN per process naming /proc, not a link; see file_get_final_path.
 *
 * Pipelining (LIMIT, since 0.5.0 - a behaviour change like the Symlinks one):
 *   - Every 200 and 206 inherits http_server_response_send_file_handle's LIMIT: a request
 *     pipelined behind it on the same HTTP/1.1 connection (or a stray CRLF) closes the connection
 *     and cuts the body short, where through 0.4.x the 200 went through lws_serve_http_file,
 *     which buffers such a request until the body is done; static has no knob to keep that path.
 *     Browsers never pipeline, and neither do reverse proxies. The stream also has a minimum read
 *     rate - a reader that takes no HTTP_SERVER_FILE_CHUNK_SIZE chunk within http_server's
 *     per-chunk content timeout is dropped - see that function's Doxygen for both.
 *
 * Dotfiles:
 *   - Served by default, like any other asset. Set self.refuse_dotfiles = true after
 *     construction (direct field assignment, the same pattern as refuse_symlinks) to refuse any
 *     path whose request-relative components include one starting with '.' - `.git/`, `.env`,
 *     `.htaccess`, `.vscode/` - at any depth. Exactly `.well-known` is exempt (ACME challenges,
 *     assetlinks.json, security.txt). Pure path arithmetic, no file-system cost. Prefer this on
 *     any root that is a checkout subtree or that a secret could ever be copied into.
 *   - What it refuses is the SPELLING a request uses, not the file it reaches. On Windows the 8.3
 *     short alias of a dotfile ("ENV~1" for ".env") does not start with '.', and is refused only
 *     because every 8.3-shaped component is (see Features). It cannot see a dotfile reached under
 *     another name - a hard link, or a symlink/junction when refuse_symlinks is off.
 *
 * Range requests:
 *   - A 206 is streamed from the handle in HTTP_SERVER_FILE_CHUNK_SIZE chunks with no
 *     module-side buffer, exactly like the 200; the window is whatever the client asked for, as
 *     nginx and Apache serve it (the 1 MiB cap of 0.4.x is gone - `Range: bytes=0-` on a 100 MB
 *     file is one response, not ~100). The Range/416 parsing is this module's own by design.
 *   - A file replaced or deleted while it is being served keeps streaming the OLD bytes to the
 *     reader that opened it (Windows: FILE_SHARE_DELETE; POSIX: unlink semantics), and a
 *     deploy's rename-over never fails against a reader - the same promise file_replace_* makes
 *     on the writer's side.
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
 *   - serve_1 is DEPRECATED as of 0.3.1 and goes in 0.6.0 at the earliest - it was scheduled for
 *     0.5.0, but 0.5.0 was taken by the streaming seam (the handle-based open/verify/serve), and
 *     its deletion precondition below still holds. It takes no request (only a raw path), so it
 *     cannot read If-Modified-Since at all: a client sending only If-Modified-Since - `wget -N`,
 *     an IMS-only proxy - re-downloads the whole body through serve_1 where serve_2 answers 304.
 *     It still supports If-None-Match, Range, and If-Range, reading them directly off the
 *     response's underlying transport handle, which is the last raw lws_hdr_copy left in the
 *     service tier. Every production caller in this tree (28 sites across 16 files) migrated to
 *     serve_2 in the 0.3.1 pass, main.c's root route included; no production caller remains.
 *     serve_1 is kept for two suite uses that need a request-less entry: the unchecked build's
 *     T1/T2 twins (HTTP_Server_Request is opaque, so serve_2 cannot reach the resolve- and
 *     open-level refusals) and the checked suite's late-root log-capture pin (the log buffer is
 *     thread-local, so it must run on the test thread, which a live-server build does not
 *     remove). It is deleted (retiring the last raw lws_hdr_copy) only when an exported
 *     test-build seam over resolve+open+verify, under HTTP_SERVICE_STATIC_TEST_HOOKS beside the
 *     two setters, replaces both. Both entry points resolve, open and verify BEFORE reading any
 *     header, so every refusal returns having touched nothing on the response.
 *   - Prefer serve_2 for any new integration; it is also what the Usage Example above shows.
 *
 * Thread Safety:
 *   - Effectively read-only after construction: serve_1/serve_2 only read `self`. Safe for
 *     concurrent calls on one instance from multiple threads, the same as any other immutable
 *     configuration object; construct and destroy from a single thread.
 *
 * Memory Management:
 *   - Every field is a fixed-size inline buffer (root_dir/root_final/route_prefix/default_file/
 *     fallback_file) or a scalar - there is nothing heap- or arena-owned once construction
 *     returns, arena-backed alloc_init_1/_2 included, since the arena there backs only the
 *     by-value struct itself, not a separate allocation inside it. http_service_static_uninit is
 *     therefore a no-op besides its null check; it exists so callers have one symmetrical
 *     teardown call regardless of which constructor was used, and so a future field that DOES
 *     own storage has somewhere to release it without an API change.
 *   - Per request, the one open file handle is the only resource. A 200 or 206 hands it to the
 *     response (http_server_response_send_file_handle), which closes it when the last chunk is
 *     written, when the client disconnects first, or when the next request on a kept-alive
 *     connection begins; every other answer (304, 416, a refusal, a send the server declined)
 *     closes it before serve_1/serve_2 return. No module-side body buffer exists on any path.
 *
 * Performance Characteristics:
 *   - _http_service_static_resolve is O(path) lexical arithmetic with no file-system access.
 *   - A served request costs ONE nofollow open (plus one attribute query on Windows), one fstat
 *     for size and mtime, and - under refuse_symlinks, the default - one final-path query on the
 *     open handle; a request that misses and falls back to the SPA shell adds a second open. The
 *     0.4.x path cost five by-path stat-family calls plus, with refuse_symlinks, one directory
 *     listing per path component.
 *   - root_dir's own final path is resolved once, at construction, never per request.
 *   - MIME resolution is a linear scan of the ~46-row extension table
 *     (_http_service_static_mimetype), then a single lws_get_mimetype fallback; case-insensitive
 *     comparison happens per candidate row, not once up front.
 *   - The 200 and the 206 stream from the handle one HTTP_SERVER_FILE_CHUNK_SIZE chunk per
 *     write-readiness callback, holding at most one chunk per connection in the transport (see
 *     http_server_response_send_file_handle) - no module-side buffer, whatever the window.
 *
 * HTTP methods:
 *   - serve_1/serve_2 do not inspect the request method; a consumer gating on GET/HEAD does so
 *     itself. A HEAD needs no gate: http_server's send paths emit the headers a GET would
 *     (Content-Length included) and no body, on the full-file, 206 and 304 paths alike - the
 *     streaming seam closes the handle and completes a HEAD synchronously.
 *
 * Dependencies:
 *   - arena, char, datetime, file, http/headers, http/service/compression, http_server, log.
 *
 * See static.c for implementation details.
 */

#ifndef HTTP_SERVICE_STATIC_H
#define HTTP_SERVICE_STATIC_H

#include <arena/arena.h>
#include <http/server/http_server.h>
#include <types.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

/** Bytes in HTTP_Service_Static.root_final: room for an on-disk final path several times the request-path cap, since the OS may spell the root longer than the configured root_dir. */
#define HTTP_SERVICE_STATIC_ROOT_FINAL_MAX_LENGTH (HTTP_SERVER_PATH_MAX_LENGTH * 4)

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
    /**
     * @brief Root directory on disk.
     *
     * Fixed at construction together with its final path: a root that is itself a link is
     * resolved ONCE, so re-pointing it afterwards (`ln -sfn releases/N+1 current`) or rewriting
     * this field serves nothing - every request answers 404 with the per-request "link refused"
     * WARN, or a silent 404 if the new root_dir does not exist - until the service is
     * reconstructed.
     */
    char root_dir[HTTP_SERVER_PATH_MAX_LENGTH];
    /** @brief Size of the root directory string. */
    U16 root_dir_size;
    /**
     * @brief file_final_path_1(root_dir), taken ONCE at construction: the prefix every served
     *        handle's final path is compared against under refuse_symlinks.
     *
     * Empty when the root could not be resolved then (missing, unreadable, or a final path longer
     * than this buffer) - construction logged one WARN, and refuse_symlinks refuses EVERY request
     * (fail closed) until the service is rebuilt over an existing root. A fixed inline buffer, so
     * the Memory Management "nothing owned" note still holds.
     */
    char root_final[HTTP_SERVICE_STATIC_ROOT_FINAL_MAX_LENGTH];
    /** @brief Size of root_final; 0 when the root could not be resolved at construction. */
    USize root_final_size;
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
     * @brief Refuse to serve a file reached through a symlink or junction anywhere below root_dir.
     *
     * ON by default since 0.5.0 (it was opt-in, and off, through 0.4.x). Set this field to false
     * directly after construction to follow links - no dedicated constructor parameter, the same
     * pattern as refuse_dotfiles. Costs one final-path query on the open handle per request. See
     * the Symlinks note in this header: the check is on the handle that is served, so there is
     * no check-then-open window.
     */
    bool refuse_symlinks;
    /**
     * @brief Opt-in: refuse to serve a path any of whose request-relative components starts with '.'.
     *
     * Off by default (zero behaviour change: dotfiles are served like any asset). No dedicated
     * constructor parameter - set this field directly after construction, the same pattern as
     * refuse_symlinks. Exactly `.well-known` is exempt, the one dot-directory the web platform
     * mandates. Costs a single scan of the request path, no file-system access. See the Dotfiles
     * note in this header for what a spelling check cannot see.
     */
    bool refuse_dotfiles;
} HTTP_Service_Static;

#ifdef HTTP_SERVICE_STATIC_TEST_HOOKS
/**
 * @brief Test build only: a callback handed the resolved on-disk file name at a fixed point of a
 *        serve, so a suite can swap the file system under the request deterministically.
 */
typedef void (*FpHTTP_Service_Static_Test_Hook)(char const *const file_name);
#endif // HTTP_SERVICE_STATIC_TEST_HOOKS

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
 * @note refuse_symlinks is ON (since 0.5.0) and root_dir's final path is resolved here, once:
 *       root_dir must EXIST when the service is constructed, or every request is refused (fail
 *       closed) and this logs LOG_LEVEL_WARN. See static.h's Symlinks note.
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
 * @note See http_service_static_init_1 for the truncation-logs-WARN note (applies to
 *       default_file here too) and for the root_dir-must-exist note.
 * @note On Windows, a default_file that file_name_reserved_windows or file_path_aliased_windows
 *       refuses ("CON") logs one WARN here, echoing the value (it is config), for default_file and
 *       for the fallback_file copied from it; the service is still constructed and never serves that
 *       name.
 */
HTTP_Service_Static http_service_static_init_2(char const *const root_dir, char const *const route_prefix, char const *const default_file, bool const spa_fallback, U32 const max_age);

/**
 * @brief Attempt to serve a static file from a raw path string.
 * @param self Service instance.
 * @param response HTTP server response context.
 * @param path Requested URL path.
 * @param path_size Size of the URL path string.
 * @return true if the static file was found and served.
 * @deprecated Since 0.3.1; deletion in 0.6.0 at the earliest, once the test-build seam exists (0.5.0 was taken by the streaming
 *             seam). Use http_service_static_serve_2, which takes the request every caller already
 *             holds. No production caller remains; kept only for the suite's two request-less uses
 *             (the unchecked T1/T2 twins and the late-root log-capture pin) until an exported
 *             test-build seam over resolve+open+verify replaces them.
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

#ifdef HTTP_SERVICE_STATIC_TEST_HOOKS
/**
 * @brief Test build only: install (or clear, with nullptr) the hook called after the file is
 *        opened and verified, before the body is handed to the sender.
 * @param hook Callback, or nullptr for none (the default).
 * @note Process-wide, not per instance, and unsynchronized: set it before the server runs or
 *       while no request is in flight. Compiled only under HTTP_SERVICE_STATIC_TEST_HOOKS.
 */
void http_service_static_test_hook_after_open_set(FpHTTP_Service_Static_Test_Hook const hook);

/**
 * @brief Test build only: install (or clear, with nullptr) the hook called after the lexical
 *        resolve, before the file is opened.
 * @param hook Callback, or nullptr for none (the default).
 * @note As http_service_static_test_hook_after_open_set.
 */
void http_service_static_test_hook_after_resolve_set(FpHTTP_Service_Static_Test_Hook const hook);
#endif // HTTP_SERVICE_STATIC_TEST_HOOKS

/**
 * @brief Release all service storage.
 * @param self Service instance.
 */
void http_service_static_uninit(HTTP_Service_Static *const self);

#endif // HTTP_SERVICE_STATIC_H