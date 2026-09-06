/*
 * security.h - HTTP security headers service for the C Libraries Framework
 * @version 0.3.1
 *
 * Automatically appends modern browser security response headers (CSP, HSTS, X-Frame-Options,
 * X-Content-Type-Options, etc.) to all HTTP responses via a configured policy.
 *
 * Features:
 *   - Automatic response header injection.
 *   - Customizable policies using HTTP_Headers_Security.
 *   - Header block rendered ONCE at construction; apply() is a plain header_add_raw of that
 *     cached String, not a re-render per response.
 *   - Arena and heap allocation support.
 *
 * Usage Example:
 *   @code
 *   HTTP_Service_Security security_svc = http_service_security_init_1();
 *   http_service_security_apply(&security_svc, response);
 *   http_service_security_uninit(&security_svc);
 *   @endcode
 *
 * Error Handling:
 *   - Public functions validate non-null pointers.
 *
 * Thread Safety:
 *   - apply() only reads the cached header block, so concurrent apply() calls on one instance,
 *     even from different threads, are safe. init/alloc_init/uninit are NOT: construct and
 *     destroy from a single thread before other threads start calling apply().
 *
 * Ownership (init_2/alloc_init_2, MOVE since 0.3.0 - R1 Mid 16):
 *   - Takes ownership of *policy's String fields via a shallow struct copy, then zeroes *policy's
 *     String fields so the caller cannot see or double-free the moved-out data. *policy itself is
 *     therefore mutated - it must not be declared `HTTP_Headers_Security const`, and
 *     http_headers_security_uninit(policy) afterward is a safe no-op, not a double-free, because
 *     uninit tolerates an already-empty String. A read-only shallow-copy signature was shipped
 *     first and considered permanent (every caller declared its local policy
 *     `HTTP_Headers_Security const`), but a stale caller could otherwise uninit *policy itself
 *     believing it still owned the Strings, freeing memory init_2/alloc_init_2 already adopted -
 *     the move closes that double-free window at the type level instead of by convention.
 *   - Passing an already-moved-from policy (one zeroed by a prior init_2/alloc_init_2 call) into a
 *     second init_2/alloc_init_2 is not refused - it renders an empty header_block, so the
 *     resulting service silently applies no security headers.
 *
 * In-place constructor convention:
 *   - alloc_init_1 is the only constructor here that cannot be by-value (it can refuse); it
 *     follows ip_block's bool-return convention: true on success, false with *self zeroed on a
 *     refused arena. Every other constructor here returns the service by value, because none of
 *     them owns anything that a copy would corrupt (no mutex, unlike ip_block).
 *
 * Dependencies:
 *   - http_server, http/headers.
 *
 * See security.c for implementation details.
 */

#ifndef HTTP_SERVICE_SECURITY_H
#define HTTP_SERVICE_SECURITY_H

#include <http/headers/headers.h>
#include <http/server/http_server.h>

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief Security middleware service configuration.
 */
typedef struct {
#ifdef ARENA_IMPLEMENTATION
    /** @brief Optional arena used by owned values. Present only in an arena build, matching the
     *         static/ip_block siblings - the two constructors that set it are themselves behind
     *         ARENA_IMPLEMENTATION, so a non-arena build carried a field nothing could fill. */
    Arena *allocator;
#endif // ARENA_IMPLEMENTATION
    /** @brief Rendered header block, built once at construction. What apply() actually sends. */
    String header_block;
    /** @brief Underlying security headers policy. */
    HTTP_Headers_Security policy;
} HTTP_Service_Security;

/*==============================================================================
 * MARK: - API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Initialize an arena-backed security service with default values, in place.
 * @param self Destination service.
 * @param allocator Arena allocator.
 * @return true if initialized; false if the arena refused the underlying policy (starved
 *         arena, or a policy value with a control byte) OR refused the rendered header block
 *         (arena starved between policy init and render), in which case *self is zeroed and
 *         http_service_security_apply must not be called on it. Per the in-place-constructor
 *         convention: the caller can fail server startup on false instead of silently shipping
 *         a response with no security headers.
 */
bool http_service_security_alloc_init_1(HTTP_Service_Security *const self, Arena *const allocator);

/**
 * @brief Initialize an arena-backed security service with a custom policy.
 * @param policy Custom security policy. MOVED FROM: *policy's String fields are adopted and then
 *        zeroed in place - policy must not be `HTTP_Headers_Security const`, and the caller must
 *        treat *policy as empty afterward (an http_headers_security_uninit(policy) call remains
 *        safe, but is a no-op).
 * @param allocator Arena allocator.
 * @return Initialized service.
 */
HTTP_Service_Security http_service_security_alloc_init_2(HTTP_Headers_Security *const policy, Arena *const allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Applies the security policy headers to the active response.
 * @param self Security service instance.
 * @param response Active response context.
 */
void http_service_security_apply(HTTP_Service_Security const *const self, HTTP_Server_Response *const response);

/**
 * @brief Initialize a heap-backed security service with default values.
 * @return Initialized service.
 */
HTTP_Service_Security http_service_security_init_1(void);

/**
 * @brief Initialize a heap-backed security service with a custom policy.
 * @param policy Custom security policy. MOVED FROM: *policy's String fields are adopted and then
 *        zeroed in place - policy must not be `HTTP_Headers_Security const`, and the caller must
 *        treat *policy as empty afterward (an http_headers_security_uninit(policy) call remains
 *        safe, but is a no-op).
 * @return Initialized service.
 */
HTTP_Service_Security http_service_security_init_2(HTTP_Headers_Security *const policy);

/**
 * @brief Release all service storage.
 * @param self Service instance.
 */
void http_service_security_uninit(HTTP_Service_Security *const self);

#endif // HTTP_SERVICE_SECURITY_H