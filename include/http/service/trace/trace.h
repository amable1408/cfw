/*
 * trace.h - HTTP request/response access trace service for the C Libraries Framework
 *
 * Records one log entry per HTTP transaction: method, path, protocol, status code,
 * response size, latency, and client IP. Outputs in Common Log Format, Combined Log
 * Format, or newline-delimited JSON.
 *
 * Features:
 *   - Common, Combined, and JSON output formats.
 *   - Configurable output stream (stdout, stderr, or any FILE*).
 *   - Path exclusion list to suppress health-check or asset noise.
 *   - Arena and heap allocation support.
 *
 * Usage Example:
 *   @code
 *   HTTP_Service_Trace trace = http_service_trace_init_1();
 *   http_service_trace_exclude_add(&trace, "/health");
 *
 *   http_service_trace_log(&trace, "127.0.0.1", "GET", "/api/rooms", "HTTP/1.1",
 *                          200, 512, 3);
 *
 *   http_service_trace_uninit(&trace);
 *   @endcode
 *
 * Error Handling:
 *   - Public functions validate non-null pointers first (error_check_null
 *     aborts when ERROR_CHECK_ENABLED is defined). With ERROR_CHECK_ENABLED
 *     off these checks compile out and a null argument is undefined
 *     behaviour; the value refusals below (empty exclusion prefix, an
 *     allocator decline) hold in every build regardless.
 *   - Invalid or excluded paths produce no output.
 *   - http_service_trace_exclude_add refuses an EMPTY prefix (returns false,
 *     stores nothing): an empty prefix matches every path by definition
 *     (zero-length compares equal to zero-length), which would silently
 *     switch the whole log off. A non-empty prefix matches by PREFIX, not by
 *     path segment - "/health" also suppresses "/healthz".
 *   - The REAL truncation contract is per-field, not per-line: each
 *     wire-derived field is capped at HTTP_SERVICE_TRACE_FIELD_CAPACITY_SHORT,
 *     _PATH, or _LONG escaped bytes and, once its cap is reached, the rest of
 *     that field is dropped and replaced with a "..." marker - never mid-escape,
 *     since the cap is checked before each 1-, 2-, or 6-byte escape piece is
 *     appended. The three field caps summed across one line stay well under
 *     HTTP_SERVICE_TRACE_LINE_CAPACITY_MAX, so a rendered line never actually
 *     reaches that limit and still ends with its format's closing quote/brace
 *     and exactly one trailing newline. The line-level truncation
 *     HTTP_SERVICE_TRACE_LINE_CAPACITY_MAX names is an unreachable backstop
 *     kept for defense in depth, not the mechanism that runs in practice.
 *   - self->stream may be null (e.g. after uninit); a log call then writes
 *     nothing rather than crashing. A short or failed write is reported once,
 *     via a single WARN log line, not on every occurrence.
 *
 * Thread Safety:
 *   - Not thread-safe to configure: callers must synchronize access to
 *     exclude_add and uninit on a shared instance.
 *   - Logging one transaction renders the whole line into a private buffer
 *     first and writes it with a single fprintf, so concurrent log/log_combined
 *     calls on the same stream cannot interleave their bytes - each entry is
 *     line-atomic under the C11 per-FILE lock fprintf holds for the call.
 *
 * Memory:
 *   - Excluded path strings are copied and owned by the service; call
 *     http_service_trace_uninit() to release them. Under an arena, that
 *     release is a no-op until the arena itself is reset or freed.
 *
 * Performance:
 *   - One fprintf per non-excluded request; exclusion check is a linear scan.
 *   - fprintf's default C stream buffering applies here like any other write;
 *     a caller wanting each entry flushed as it is written should call
 *     setvbuf(stream, nullptr, _IOLBF, 0) on the stream before use, or flush
 *     it explicitly - this service never calls fflush itself.
 *
 * Dependencies:
 *   - arrayList, char, datetime, log, memory, str.
 *
 * See trace.c for implementation details.
 */

#ifndef HTTP_SERVICE_TRACE_H
#define HTTP_SERVICE_TRACE_H

#include <stdatomic.h>

#include <container/arrayList/al_str.h>
#include <datetime/datetime.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

/** Default output stream used when none is supplied. */
#define HTTP_SERVICE_TRACE_DEFAULT_STREAM stdout

/** Max ESCAPED bytes kept from referer/user-agent before a "..." marker replaces the rest. */
#define HTTP_SERVICE_TRACE_FIELD_CAPACITY_LONG 1024

/** Max ESCAPED bytes kept from the path before a "..." marker replaces the rest. The
 *  largest of the three field caps: a path is the field most likely to be long
 *  legitimately (query strings, deep routes). */
#define HTTP_SERVICE_TRACE_FIELD_CAPACITY_PATH 4096

/** Max ESCAPED bytes kept from ip/method/protocol before a "..." marker replaces the
 *  rest. These fields are short in every legitimate request, so a generous cap still
 *  catches a hostile one while leaving room for the others - see LINE_CAPACITY_MAX. */
#define HTTP_SERVICE_TRACE_FIELD_CAPACITY_SHORT 256

/** Max rendered line size in bytes, including the terminator. A line that would
 *  exceed this is truncated rather than growing without bound. The three field caps
 *  above (ip/method/protocol x3 SHORT, path PATH, referer+user_agent x2 LONG) sum to
 *  well under this, plus a "..." marker per truncated field and the fixed skeleton
 *  text (quotes, spaces, status, byte count) - a rendered line can never need to drop
 *  a whole field, only never overflow to begin with. */
#define HTTP_SERVICE_TRACE_LINE_CAPACITY_MAX 8192

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief Supported log entry output formats.
 */
typedef enum {
    /** @brief NCSA Common Log Format: ip - - [timestamp] "METHOD path PROTOCOL" status bytes
     *  Latency is NOT part of this line - it is JSON-only. `bytes` prints "-" for a
     *  zero-byte response, matching Apache. */
    HTTP_SERVICE_TRACE_FORMAT_COMMON = 0,
    /** @brief NCSA Combined Log Format: Common fields plus "referer" "user-agent".
     *  Latency is NOT part of this line either, for the same reason. */
    HTTP_SERVICE_TRACE_FORMAT_COMBINED,
    /** @brief Newline-delimited JSON object per request. The only format that carries
     *  duration_ms; referer/user_agent are JSON null when the caller passed nullptr. */
    HTTP_SERVICE_TRACE_FORMAT_JSON
} HTTP_Service_Trace_Format;

/**
 * @brief Access trace service configuration.
 */
typedef struct {
#ifdef ARENA_IMPLEMENTATION
    /** @brief Optional arena used by owned values. */
    Arena *allocator;
#endif // ARENA_IMPLEMENTATION
    /** @brief Output stream for log entries. */
    FILE *stream;
    /** @brief Log entry format. */
    HTTP_Service_Trace_Format format;
    /** @brief Paths excluded from logging (e.g. "/health", "/favicon.ico"). */
    AL_Str exclude_paths;
} HTTP_Service_Trace;

/*==============================================================================
 * MARK: - API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Initialize an arena-backed trace service with default values.
 * @param allocator Arena allocator.
 * @return Initialized service.
 */
HTTP_Service_Trace http_service_trace_alloc_init_1(Arena *const allocator);

/**
 * @brief Initialize an arena-backed trace service with explicit values.
 * @param stream Output FILE stream.
 * @param format Log entry output format.
 * @param allocator Arena allocator.
 * @return Initialized service.
 */
HTTP_Service_Trace http_service_trace_alloc_init_2(FILE *const stream, HTTP_Service_Trace_Format const format, Arena *const allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Add a path to the exclusion list.
 *
 * Requests whose path starts with an excluded prefix produce no log output.
 * Prefix matching, not path-segment matching: "/health" also suppresses
 * "/healthz".
 *
 * Refuses (returns false, stores nothing) for an EMPTY path: an empty prefix
 * would compare equal to every path's own empty-length check and silently
 * exclude the whole log. Also returns false when the allocator refused,
 * leaving the exclusion list exactly as it was. Treat either false as a
 * REFUSAL, not a detail: the caller asked for a path to be kept out of the
 * log and it will now be logged, so a caller that ignores this silently
 * starts recording the traffic it meant to suppress.
 *
 * @param self Service instance.
 * @param path Non-empty path prefix to suppress (e.g. "/health").
 * @return true when the prefix was stored.
 */
bool http_service_trace_exclude_add(HTTP_Service_Trace *const self, char const *const path);

/**
 * @brief Initialize a heap-backed trace service with default values.
 *
 * Uses stdout and HTTP_SERVICE_TRACE_FORMAT_COMMON.
 *
 * @return Initialized service.
 */
HTTP_Service_Trace http_service_trace_init_1(void);

/**
 * @brief Initialize a heap-backed trace service with explicit values.
 * @param stream Output FILE stream.
 * @param format Log entry output format.
 * @return Initialized service.
 */
HTTP_Service_Trace http_service_trace_init_2(FILE *const stream, HTTP_Service_Trace_Format const format);

/**
 * @brief Write one log entry for an HTTP transaction.
 *
 * Writes nothing when the request path matches an excluded prefix. Every
 * wire-derived field (ip, method, path, protocol) is escaped before being
 * written: quotes and backslashes are escaped, control bytes render as
 * \\xHH (COMMON/COMBINED) or \\uXXXX (JSON), and JSON additionally escapes
 * bytes >= 0x80 the same way (e.g. 0xFF becomes \\u00ff) since a JSON string
 * is UTF-8 text; COMMON/COMBINED stay byte-oriented like Apache's own access
 * log and pass those bytes through unescaped. This is what keeps a hostile
 * path or header from forging extra log fields or breaking a downstream
 * parser.
 *
 * @param self Service instance.
 * @param ip Client IP address string.
 * @param method HTTP method string (e.g. "GET").
 * @param path Request path string (e.g. "/api/rooms").
 * @param protocol Request line protocol token (e.g. "HTTP/1.1", "HTTP/2").
 * @param status_code HTTP response status code, written as given (not validated).
 * @param bytes_sent Response body byte count; renders as "-" when zero in
 *        COMMON/COMBINED, matching Apache.
 * @param duration_ms Request handling time in milliseconds; JSON only.
 */
void http_service_trace_log(HTTP_Service_Trace const *const self,
    char const *const ip, char const *const method, char const *const path, char const *const protocol, I32 const status_code, USize const bytes_sent, U64 const duration_ms);

/**
 * @brief Write one Combined log entry with referer and user-agent fields.
 *
 * Only meaningful when the service format is HTTP_SERVICE_TRACE_FORMAT_COMBINED
 * or HTTP_SERVICE_TRACE_FORMAT_JSON. Falls back to http_service_trace_log for
 * COMMON. Same escaping guarantee as http_service_trace_log, extended to
 * referer and user_agent.
 *
 * @param self Service instance.
 * @param ip Client IP address string.
 * @param method HTTP method string.
 * @param path Request path string.
 * @param protocol Request line protocol token (e.g. "HTTP/1.1", "HTTP/2").
 * @param status_code HTTP response status code, written as given (not validated).
 * @param bytes_sent Response body byte count.
 * @param duration_ms Request handling time in milliseconds; JSON only.
 * @param referer Value of the Referer request header, or nullptr. COMMON/COMBINED
 *        render a missing referer as "-"; JSON renders it as null.
 * @param user_agent Value of the User-Agent request header, or nullptr. Same
 *        missing-value rendering as referer.
 */
void http_service_trace_log_combined(HTTP_Service_Trace const *const self,
    char const *const ip, char const *const method, char const *const path, char const *const protocol, I32 const status_code, USize const bytes_sent, U64 const duration_ms,
    char const *const referer, char const *const user_agent);

/**
 * @brief Release all service storage.
 * @param self Service instance.
 */
void http_service_trace_uninit(HTTP_Service_Trace *const self);

#endif // HTTP_SERVICE_TRACE_H