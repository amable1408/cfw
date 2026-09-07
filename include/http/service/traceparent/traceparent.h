/*
 * traceparent.h - HTTP traceparent service for the C Libraries Framework
 * @version 0.3.0
 *
 * Creates, validates, and propagates W3C Trace Context traceparent (and tracestate)
 * values. The service stays transport-focused and does not depend on logging,
 * databases, routes, users, or application-specific tracing backends.
 *
 * Features:
 *   - W3C traceparent value generation, with the CSPRNG failure REPORTED rather than
 *     quietly yielding an empty context.
 *   - Incoming traceparent validation and parsing, including future versions: any version
 *     but "ff" is parsed with the version-00 layout, as the spec's Versioning section
 *     requires, so a peer one version ahead no longer restarts every trace.
 *   - Child context creation that preserves trace id and rotates parent id.
 *   - tracestate pass-through: an inbound tracestate rides along with the context and is
 *     emitted beside the traceparent, so vendor correlation survives this hop. It is
 *     dropped when the inbound traceparent is invalid - a restarted trace has no state.
 *   - Response/header block generation, SIZE-ACCOUNTED so an allocator that drops part of
 *     a block yields an empty one rather than a well-formed "traceparent: " naming no
 *     trace, and a zero-allocation value writer.
 *
 * Usage Example:
 *   @code
 *   HTTP_Service_Traceparent trace = DEFAULT_INITIALIZATION;
 *
 *   if (!http_service_traceparent_init_1(&trace)) {
 *       // Memory Management: false means this instance does not exist - fail startup.
 *   }
 *
 *   // ... per request: continue the caller's trace, or start one.
 *   HTTP_Service_Traceparent_Context context = DEFAULT_INITIALIZATION;
 *
 *   if (!http_service_traceparent_child_create_2(&trace, inbound_traceparent, inbound_tracestate, &context)) {
 *       // The CSPRNG refused: no id could be minted, so this request is untraced.
 *   }
 *
 *   String header = http_service_traceparent_header_create(&trace, &context);
 *
 *   string_uninit(&header);
 *
 *   // ... or, propagating downstream with no allocation at all. http_client_header_add
 *   // takes ONE "Name: value" line, not a name and a value:
 *   char value[HTTP_SERVICE_TRACEPARENT_VALUE_CAPACITY] = DEFAULT_INITIALIZATION;
 *
 *   if (http_service_traceparent_value_write_1(&context, value)) {
 *       char line[16 + HTTP_SERVICE_TRACEPARENT_VALUE_CAPACITY] = DEFAULT_INITIALIZATION;
 *
 *       snprintf(line, sizeof(line), "traceparent: %s", value);
 *       http_client_header_add(client, line);
 *   }
 *
 *   http_service_traceparent_uninit(&trace);
 *   @endcode
 *
 * Value layout (55 bytes, version 00):
 *   - "00-" + 32 hex trace-id + "-" + 16 hex parent-id + "-" + 2 hex flags.
 *   - Separators sit at offsets 2, 35 and 52; the fields start at 3, 36 and 53.
 *   - An all-zero trace-id or parent-id is invalid, and so is version "ff".
 *
 * The header_name knob:
 *   - W3C fixes the name "traceparent", so changing it stops this service interoperating
 *     with any other tracing peer. It stays configurable only so a test (or a private
 *     mesh mirroring the format under another name) can drive it, and it is validated as
 *     an HTTP token at init - a name carrying a space, a colon or CR/LF is refused there
 *     rather than corrupting every emitted header block. The tracestate line always uses
 *     the spec name.
 *
 * Error Handling:
 *   - Public functions validate non-null pointers.
 *   - Every constructor answers bool, and an entropy failure is one of the false cases:
 *     an untraced request is a fact the caller has to be able to see.
 *   - Invalid incoming values are answered false with a ZEROED context, never an abort -
 *     these bytes come off the network.
 *
 * Thread Safety:
 *   - The service is read-only after init, so concurrent use of one const instance is
 *     safe on the heap configuration. Contexts are plain values owned by whoever declared
 *     them; nothing is shared.
 *   - An ARENA-backed instance is the exception: header_create and value_create borrow from
 *     the arena, and Arena is not thread-safe (see arena.h). The window is the RETURNED
 *     String's whole life - from the *_create that borrows to the string_uninit that gives
 *     the bytes back, since allocator_release calls the arena's own deallocate. Give each
 *     thread its own arena-backed service, or serialize create-through-uninit around it.
 *
 * Memory Management:
 *   - A context is a POD value: fixed char arrays, no allocator, no uninit. It can live on
 *     the stack, be copied by assignment, and be forgotten without leaking.
 *   - Only header_create and value_create allocate; both return a String the caller
 *     releases with string_uninit, and both return the EMPTY String rather than a partial
 *     one when the allocator refuses. value_write_1/_2 allocate nothing at all.
 *
 * Performance Characteristics:
 *   - Generation and validation are FIXED-SIZE work over a 55-byte value - not
 *     constant-time in the cryptographic sense; nothing here is timing-hardened, and
 *     nothing here is secret.
 *   - The ids come straight from crypto_random_hex into the context's own storage: one
 *     CSPRNG draw per id and no heap traffic per request.
 *
 * Comparison:
 *   - The OpenTelemetry W3C propagator is the reference this follows for the forward
 *     compatible version rule and for dropping tracestate with an unparsable traceparent.
 *     It differs in one place on purpose: tracestate is carried VERBATIM (size and member
 *     count capped), never re-ordered or mutated, because this service is a hop, not a
 *     vendor with an entry to add.
 *
 * Dependencies:
 *   - char, crypto/random, encoding/hex, http/headers, string.
 *
 * See traceparent.c for implementation details.
 */

#ifndef HTTP_SERVICE_TRACEPARENT_H
#define HTTP_SERVICE_TRACEPARENT_H

#include <container/string/string.h>
#include <crypto/random/random.h>
#include <http/headers/headers.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

#define HTTP_SERVICE_TRACEPARENT_DEFAULT_HEADER_NAME "traceparent"
#define HTTP_SERVICE_TRACEPARENT_DEFAULT_TRACE_FLAGS HTTP_SERVICE_TRACEPARENT_FLAG_SAMPLED
/** @brief The sampled bit of the flags byte. */
#define HTTP_SERVICE_TRACEPARENT_FLAG_SAMPLED 1
/** @brief Buffer size the service's header name occupies, NUL included. */
#define HTTP_SERVICE_TRACEPARENT_HEADER_NAME_CAPACITY 64
#define HTTP_SERVICE_TRACEPARENT_PARENT_ID_BYTE_COUNT 8
#define HTTP_SERVICE_TRACEPARENT_PARENT_ID_SIZE 16
#define HTTP_SERVICE_TRACEPARENT_TRACE_FLAGS_SIZE 2
#define HTTP_SERVICE_TRACEPARENT_TRACE_ID_BYTE_COUNT 16
#define HTTP_SERVICE_TRACEPARENT_TRACE_ID_SIZE 32
/** @brief Buffer size a carried tracestate occupies, NUL included. */
#define HTTP_SERVICE_TRACEPARENT_TRACESTATE_CAPACITY 513
#define HTTP_SERVICE_TRACEPARENT_TRACESTATE_HEADER_NAME "tracestate"
/** @brief Members past which an inbound tracestate is dropped rather than carried. */
#define HTTP_SERVICE_TRACEPARENT_TRACESTATE_MAX_MEMBERS 32
/** @brief Bytes past which an inbound tracestate is dropped rather than carried. */
#define HTTP_SERVICE_TRACEPARENT_TRACESTATE_MAX_SIZE 512
/** @brief Buffer size http_service_traceparent_value_write_1 needs, NUL included. */
#define HTTP_SERVICE_TRACEPARENT_VALUE_CAPACITY (HTTP_SERVICE_TRACEPARENT_VALUE_SIZE + CHAR_END_CHARACTER)
#define HTTP_SERVICE_TRACEPARENT_VALUE_SIZE 55
#define HTTP_SERVICE_TRACEPARENT_VERSION "00"
#define HTTP_SERVICE_TRACEPARENT_VERSION_SIZE 2

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief Parsed or generated traceparent context - a plain value, not an owner.
 *
 * Fixed-size hex in fixed-size storage: two heap Strings for 48 known bytes cost five
 * allocations per request and forced uninit discipline on data whose every byte count is
 * known at compile time. It is not small - the carried tracestate takes it to 564 bytes -
 * but it is PLAIN: copy it, pass it by value, let it go out of scope.
 */
typedef struct {
    /** @brief Parent id as 16 lowercase hex characters, NUL-terminated. */
    char parent_id[HTTP_SERVICE_TRACEPARENT_PARENT_ID_SIZE + CHAR_END_CHARACTER];
    /** @brief Trace flags byte. */
    U8 trace_flags;
    /** @brief Trace id as 32 lowercase hex characters, NUL-terminated. */
    char trace_id[HTTP_SERVICE_TRACEPARENT_TRACE_ID_SIZE + CHAR_END_CHARACTER];
    /** @brief Inbound tracestate, carried verbatim. Empty when there is none. */
    char tracestate[HTTP_SERVICE_TRACEPARENT_TRACESTATE_CAPACITY];
} HTTP_Service_Traceparent_Context;

/**
 * @brief Traceparent service configuration.
 */
typedef struct {
#ifdef ARENA_IMPLEMENTATION
    /** @brief Optional arena used by returned strings. */
    Arena *allocator;
#endif // ARENA_IMPLEMENTATION
    /** @brief Header name used when generating header blocks, NUL-terminated. */
    char header_name[HTTP_SERVICE_TRACEPARENT_HEADER_NAME_CAPACITY];
    /** @brief Default trace flags byte for a root context. */
    U8 trace_flags;
} HTTP_Service_Traceparent;

/*==============================================================================
 * MARK: - API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Initialize an arena-backed traceparent service with default values in place.
 * @param self Service instance, zeroed on failure.
 * @param allocator Arena allocator.
 * @return true when the service is usable.
 */
bool http_service_traceparent_alloc_init_1(HTTP_Service_Traceparent *const self, Arena *const allocator);

/**
 * @brief Initialize an arena-backed traceparent service with explicit values in place.
 * @param self Service instance, zeroed on failure.
 * @param header_name Header name used when generating header blocks.
 * @param trace_flags Default trace flags byte.
 * @param allocator Arena allocator.
 * @return See http_service_traceparent_init_2.
 */
bool http_service_traceparent_alloc_init_2(HTTP_Service_Traceparent *const self, char const *const header_name, U8 const trace_flags, Arena *const allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Create a child context from an incoming traceparent value.
 * @param self Service instance.
 * @param value Incoming traceparent value.
 * @param out Child context, zeroed on failure.
 * @return true when a context was minted. An invalid or absent inbound value is NOT a
 *         failure - it yields a new root trace, which is the spec's answer. False means
 *         the CSPRNG refused and no id exists.
 */
bool http_service_traceparent_child_create_1(HTTP_Service_Traceparent const *const self, char const *const value, HTTP_Service_Traceparent_Context *const out);

/**
 * @brief Create a child context from an incoming traceparent and tracestate pair.
 * @param self Service instance.
 * @param value Incoming traceparent value.
 * @param tracestate Incoming tracestate value, or null when the header was absent.
 * @param out Child context, zeroed on failure.
 * @return See http_service_traceparent_child_create_1. The tracestate is carried only
 *         when the traceparent was valid: restarting a trace invalidates every vendor
 *         entry that referred to the old one.
 */
bool http_service_traceparent_child_create_2(HTTP_Service_Traceparent const *const self, char const *const value, char const *const tracestate, HTTP_Service_Traceparent_Context *const out);

/**
 * @brief Create a new root traceparent context.
 * @param self Service instance.
 * @param out Generated context, zeroed on failure.
 * @return true when both ids were minted; false when the CSPRNG refused - the request is
 *         then untraced, and the caller can say so rather than emitting an empty header.
 */
bool http_service_traceparent_context_create(HTTP_Service_Traceparent const *const self, HTTP_Service_Traceparent_Context *const out);

/**
 * @brief Parse an incoming traceparent value.
 * @param self Service instance.
 * @param value Incoming traceparent value.
 * @param out Parsed context, zeroed when the value is invalid.
 * @return true when the value parsed.
 */
bool http_service_traceparent_context_parse_1(HTTP_Service_Traceparent const *const self, char const *const value, HTTP_Service_Traceparent_Context *const out);

/**
 * @brief Parse an incoming traceparent value together with its tracestate.
 * @param self Service instance.
 * @param value Incoming traceparent value.
 * @param tracestate Incoming tracestate value, or null when the header was absent.
 * @param out Parsed context, zeroed when the traceparent is invalid.
 * @return true when the traceparent parsed. The tracestate is carried verbatim when it
 *         holds at most HTTP_SERVICE_TRACEPARENT_TRACESTATE_MAX_SIZE bytes and
 *         HTTP_SERVICE_TRACEPARENT_TRACESTATE_MAX_MEMBERS members and carries no control
 *         byte (0x00-0x1F, 0x7F refused; bytes at or above 0x80 pass as obs-text); past any
 *         of those it is DROPPED (the context still parses), because a hop that forwards an
 *         unbounded header is an amplifier.
 *         The tier number here reads as "the tracestate one", not "the sized one": every
 *         value this module parses is NUL-terminated, so no sized tier is real.
 */
bool http_service_traceparent_context_parse_2(HTTP_Service_Traceparent const *const self, char const *const value, char const *const tracestate, HTTP_Service_Traceparent_Context *const out);

/**
 * @brief Check whether a context is sampled.
 * @param self Traceparent context.
 * @return true when the sampled bit of the flags byte is set.
 */
bool http_service_traceparent_context_sampled(HTTP_Service_Traceparent_Context const *const self);

/**
 * @brief Check whether a context has valid trace and parent ids.
 * @param self Traceparent context.
 * @return true when valid.
 */
bool http_service_traceparent_context_valid(HTTP_Service_Traceparent_Context const *const self);

/**
 * @brief Build a traceparent header block.
 * @param self Service instance.
 * @param context Traceparent context.
 * @return Header block, to be released with string_uninit. Carries the traceparent line
 *         and, when the context holds one, the tracestate line. Empty when the context is
 *         invalid - an invalid context has no value to name.
 */
String http_service_traceparent_header_create(HTTP_Service_Traceparent const *const self, HTTP_Service_Traceparent_Context const *const context);

/**
 * @brief Initialize a traceparent service with default values in place.
 * @param self Service instance, zeroed on failure.
 * @return true when the service is usable.
 */
bool http_service_traceparent_init_1(HTTP_Service_Traceparent *const self);

/**
 * @brief Initialize a traceparent service with explicit values in place.
 * @param self Service instance, zeroed on failure.
 * @param header_name Header name used when generating header blocks.
 * @param trace_flags Default trace flags byte.
 * @return true when the service is usable; false when header_name is empty, is not an
 *         HTTP token, or does not fit HTTP_SERVICE_TRACEPARENT_HEADER_NAME_CAPACITY.
 */
bool http_service_traceparent_init_2(HTTP_Service_Traceparent *const self, char const *const header_name, U8 const trace_flags);

/**
 * @brief Release service storage.
 * @param self Service instance.
 */
void http_service_traceparent_uninit(HTTP_Service_Traceparent *const self);

/**
 * @brief Validate an incoming traceparent value.
 * @param value Incoming traceparent value.
 * @return true when valid.
 */
bool http_service_traceparent_valid_1(char const *const value);

/**
 * @brief Validate a sized incoming traceparent value.
 * @param value Incoming traceparent value.
 * @param value_size Byte length of value.
 * @return true when valid. A value LONGER than 55 bytes is accepted when byte 55 is the
 *         '-' that starts a future version's extra fields; the first 55 bytes are then
 *         read with the version-00 layout. Version "ff" is invalid at any length.
 */
bool http_service_traceparent_valid_2(char const *const value, USize const value_size);

/**
 * @brief Build a traceparent value from a context.
 * @param self Service instance.
 * @param context Traceparent context.
 * @return traceparent value, to be released with string_uninit. Empty when the context is
 *         invalid, and empty rather than short when the allocator refuses the 55 bytes.
 *         Prefer http_service_traceparent_value_write_1 when a buffer will do.
 */
String http_service_traceparent_value_create(HTTP_Service_Traceparent const *const self, HTTP_Service_Traceparent_Context const *const context);

/**
 * @brief Write a traceparent value into a caller buffer, allocating nothing. The
 *        FIXED-SIZE form: out must be exactly HTTP_SERVICE_TRACEPARENT_VALUE_CAPACITY
 *        bytes, unchecked. Prefer http_service_traceparent_value_write_2 for a buffer
 *        whose size is not statically known to be that constant.
 * @param self Traceparent context.
 * @param out Destination of HTTP_SERVICE_TRACEPARENT_VALUE_CAPACITY bytes; NUL-terminated
 *        on success, left as "" otherwise.
 * @return true when the 55-byte value was written; false when the context is invalid.
 */
bool http_service_traceparent_value_write_1(HTTP_Service_Traceparent_Context const *const self, char *const out);

/**
 * @brief Write a traceparent value into a caller buffer of a given capacity.
 * @param self Traceparent context.
 * @param out Destination buffer.
 * @param capacity Byte length of out.
 * @return true when the 55-byte value was written; false when capacity is less than
 *         HTTP_SERVICE_TRACEPARENT_VALUE_CAPACITY (refused before out is touched) or the
 *         context is invalid.
 */
bool http_service_traceparent_value_write_2(HTTP_Service_Traceparent_Context const *const self, char *const out, USize const capacity);

#endif // HTTP_SERVICE_TRACEPARENT_H