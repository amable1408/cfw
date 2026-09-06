/*
 * multipart.h - HTTP Service Multipart parser for the C Libraries Framework
 *
 * Parses multipart/form-data request bodies and provides lookups by field
 * name. Zero-copy for the body: each part's `data` points into the caller's
 * payload buffer rather than a copy.
 *
 * Features:
 *   - Parses multipart/form-data payloads.
 *   - Extracts form field names, filenames, content types, and data.
 *   - Bounds part count, per-part header size, and boundary length.
 *   - Arena allocator support.
 *
 * Usage Example:
 *   @code
 *   Byte const payload[] = "--b\r\nContent-Disposition: form-data; name=\"file\"\r\n\r\nhi\r\n--b--\r\n";
 *   HTTP_Service_Multipart multipart = http_service_multipart_init();
 *
 *   if (http_service_multipart_parse(&multipart, "b", payload, sizeof(payload) - 1)) {
 *       HTTP_Service_Multipart_Node const *const file_part = http_service_multipart_node_get(&multipart, "file");
 *       if (file_part != nullptr) {
 *           // Use file_part->data, file_part->size, file_part->filename, etc.
 *       }
 *   }
 *
 *   http_service_multipart_uninit(&multipart);
 *   @endcode
 *
 * Error Handling:
 *   - Required pointer arguments are validated first; a null argument aborts
 *     via error_check_null when ERROR_CHECK_ENABLED is defined. With
 *     ERROR_CHECK_ENABLED compiled out those null checks disappear and a null
 *     pointer is undefined behaviour; the zero-size, oversize-boundary and
 *     bound refusals below are value branches that hold in every build.
 *   - A zero-size payload is a legal VALUE (an empty request body), refused
 *     before the null check: parse answers false without touching payload.
 *   - Returns false on parse failures, including a body whose closing
 *     delimiter or a part's header block was never found; parts read
 *     successfully before the failure point stay valid and owned by the list.
 *
 * Thread Safety:
 *   - Not thread-safe. Synchronization is required for shared instances.
 *
 * Memory:
 *   - `data` and `size` point into the caller's payload buffer, never copied;
 *     the payload buffer must outlive the parsed multipart parts.
 *   - `data` is never null after a successful parse, even for a part with
 *     `size` 0 (it then points at the byte right after the header block's
 *     separator, possibly one-past-end of the payload). Test `size`, not
 *     `data`, for whether a part's body is empty.
 *   - `name`, `filename`, and `content_type` are COPIES allocated with the
 *     service's allocator (Arena or Heap) and released by
 *     http_service_multipart_uninit. Calling uninit is what frees them; a
 *     parse whose service is never uninit'd leaks one to three blocks per
 *     part.
 *   - Backed by a LINEAR arena, releasing a per-part string at uninit is a
 *     no-op: a linear arena reclaims nothing until it is reset or uninit'd as
 *     a whole. Backed by a pool arena or the heap, each string is reclaimed
 *     immediately.
 *   - Repeated parse calls on one service APPEND, they do not reset. Uninit and
 *     re-init between payloads rather than reusing a parsed service.
 *   - Do not remove from or clear self->parts directly. Uninit releases the
 *     strings by walking the current list, so a part dropped from it beforehand
 *     is unreachable and leaks.
 *
 * Arena Sizing:
 *   - parse() reserves capacity for max_parts nodes up front (whenever max_parts != 0,
 *     which is init's default) so the parts LIST allocates once, at entry, rather than
 *     growing unpredictably mid-parse: budget
 *     max_parts * sizeof(HTTP_Service_Multipart_Node) for it.
 *   - Per part, a LINEAR arena permanently retains up to 2x that part's header-block
 *     byte count: one copy for the block itself (borrowed into a scratch buffer that
 *     a linear arena's "release" cannot actually reclaim until the whole arena resets)
 *     plus up to one more for the combined name/filename/content_type copies, which are
 *     disjoint substrings of that same block and so cannot together exceed its length.
 *     This is an APPROXIMATE bound, not exact: it omits the NUL terminator each of the
 *     up to 4 borrows per part (the header block plus name/filename/content_type) adds,
 *     and the MEMORY_ALIGN_UP padding a linear arena rounds every borrow up to. A more
 *     conservative per-payload budget is
 *     max_parts * (2 * max_header_size + 4 * (MEMORY_ALIGNMENT + 1) + sizeof(HTTP_Service_Multipart_Node))
 *     + boundary_size + 3 + 2 * MEMORY_ALIGNMENT. The trailing "+ 2 * MEMORY_ALIGNMENT" covers the
 *     full_boundary borrow ("--" + boundary + NUL) and the max_parts node-array reservation: both
 *     are themselves linear-arena borrows subject to the same MEMORY_ALIGN_UP rounding as the
 *     per-part borrows above, one alignment's worth each. A part with a small header block is
 *     where this bound is loosest relative to its real retention, since the fixed
 *     terminator/padding overhead dominates there.
 *   - The max_parts reservation above, at parse entry, is the ONE point that can ABORT
 *     on an exhausted arena under ERROR_CHECK_ENABLED - allocator_borrow's contract for
 *     a size the framework itself chose (see allocator.h). Every later borrow (the full
 *     boundary, a part's header block, or an extracted field) goes through
 *     allocator_try_borrow and DECLINES instead: the parse returns false (keeping parts
 *     read so far), or the one field is reported absent - never an abort. A REFUSED
 *     arena (one whose own construction already failed, the "poisoned" shape documented
 *     in arena.h) declines gracefully everywhere in this module, including the
 *     max_parts reservation itself.
 *
 * Performance:
 *   - Parses the payload in a single linear pass; each delimiter is located
 *     once, not re-scanned by the next part.
 *   - The delimiter scan is memchr-first: it narrows to candidate positions of
 *     the boundary's leading byte, then compares the full boundary only at
 *     those positions. A candidate not followed by "--", CRLF, or LF (RFC 2046
 *     5.1.1) is a boundary-shaped run inside a part's own data, not a real
 *     delimiter, and the scan resumes past it rather than ending the part early.
 *     Measured 3.7x faster than the naive from-every-offset scan it replaced:
 *     20.29 ms -> 5.42 ms parsing a 20 MB single-part body with a 60-byte
 *     boundary (2026-09-06).
 *   - Node lookup is a linear scan, matching `name` case-SENSITIVELY.
 *
 * Limitations:
 *   - `name` and `filename` are read as the raw bytes between the quotes: no
 *     quoted-string unescaping (a literal `\"` ends the value early) and no
 *     RFC 6266 `filename*=` extended parameter. Treat both as untrusted text,
 *     never as a path.
 *   - `name=""` (a zero-length value between the quotes) reads as an ABSENT
 *     name, not an empty-named field: the zero-size extraction is reported as
 *     "field not present" the same way a missing `name=` attribute is, so
 *     http_service_multipart_node_get can never match it by name.
 *
 * Dependencies:
 *   - al_multipart, char, memory.
 *
 * See multipart.c for implementation details.
 */

#ifndef HTTP_SERVICE_MULTIPART_H
#define HTTP_SERVICE_MULTIPART_H

#include <http/service/multipart/al_multipart.h>
#include <char/char.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

/** @brief Longest boundary value parse() accepts, per RFC 2046 section 5.1.1. */
#define HTTP_SERVICE_MULTIPART_BOUNDARY_MAX_LENGTH 70

/** @brief Default cap, in bytes, on one part's header block (0 = unbounded); see max_header_size. */
#define HTTP_SERVICE_MULTIPART_DEFAULT_MAX_HEADER_SIZE 16384

/** @brief Default cap on parsed parts per payload (0 = unbounded); see max_parts. */
#define HTTP_SERVICE_MULTIPART_DEFAULT_MAX_PARTS 1024

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief HTTP multipart service parser configuration and state.
 */
typedef struct {
#ifdef ARENA_IMPLEMENTATION
    /** @brief Optional arena allocator pointer. */
    Arena *allocator;
#endif // ARENA_IMPLEMENTATION
    /** @brief List of parsed multipart nodes/parts. */
    AL_MultiPart parts;
    /** @brief Refuse the parse past this many parts. 0 = unbounded. Defaults to HTTP_SERVICE_MULTIPART_DEFAULT_MAX_PARTS. */
    USize max_parts;
    /** @brief Refuse the parse when one part's header block exceeds this many bytes. 0 = unbounded. Defaults to HTTP_SERVICE_MULTIPART_DEFAULT_MAX_HEADER_SIZE. */
    USize max_header_size;
} HTTP_Service_Multipart;

/*==============================================================================
 * MARK: - API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Initialize an arena-backed multipart service.
 * @param allocator Arena allocator.
 * @return Initialized service, with max_parts/max_header_size set to their defaults.
 */
HTTP_Service_Multipart http_service_multipart_alloc_init(Arena *const allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Get the address of the parsed part at an index.
 * @param self Service instance.
 * @param index Index to access.
 * @return Address of the part. Treat it as READ-ONLY (see http_service_multipart_node_get).
 * @note Bounded by the parsed count, not any reserved capacity; an out-of-range index aborts (see al_multipart_at).
 */
HTTP_Service_Multipart_Node* http_service_multipart_at(HTTP_Service_Multipart const *const self, USize const index);

/**
 * @brief Extract the bare boundary value out of a Content-Type header.
 * @param content_type The full Content-Type header value (e.g. "multipart/form-data; boundary=abc").
 * @param buffer Destination for the bare boundary (no leading "--", quotes stripped).
 * @param capacity Size of buffer, including the terminator.
 * @return true if a non-empty boundary was extracted, false otherwise (buffer is left as an
 *         empty string when capacity allows it; a capacity of 0 is refused as a VALUE before
 *         anything is written, since there is then no room even for that empty string).
 */
bool http_service_multipart_boundary(char const *const content_type, char *const buffer, USize const capacity);

/**
 * @brief Get the number of parsed parts.
 * @param self Service instance.
 * @return The number of parts currently parsed.
 */
USize http_service_multipart_count(HTTP_Service_Multipart const *const self);

/**
 * @brief Initialize a heap-backed multipart service.
 * @return Initialized service, with max_parts/max_header_size set to their defaults.
 */
HTTP_Service_Multipart http_service_multipart_init(void);

/**
 * @brief Get a parsed part/node by field name.
 * @param self Service instance.
 * @param name The name of the field to retrieve.
 * @return Pointer to the part node, or nullptr if not found. Treat the node as
 *         READ-ONLY and do not free it or any of its fields: the list owns the
 *         strings and releases them at uninit, and `data` points into the
 *         caller's payload buffer.
 */
HTTP_Service_Multipart_Node* http_service_multipart_node_get(HTTP_Service_Multipart const *const self, char const *const name);

/**
 * @brief Parse the multipart/form-data payload.
 * @param self Service instance.
 * @param boundary The bare boundary string extracted from Content-Type (see http_service_multipart_boundary).
 * @param payload The request body payload.
 * @param payload_size The size of the payload.
 * @return true when the closing delimiter was found; false on a zero-size payload,
 *         an empty or oversize (> HTTP_SERVICE_MULTIPART_BOUNDARY_MAX_LENGTH) boundary,
 *         an allocator that declined a borrow (a REFUSED allocator, or an arena with
 *         too little room left for the full boundary or a part's header block - see
 *         Arena Sizing above), a max_parts/max_header_size bound being hit, or a body
 *         whose closing delimiter or a part's header block was never found. Parts read
 *         before a false return stay valid and owned by self->parts. Only the max_parts
 *         reservation at entry ABORTS on an exhausted arena instead of returning false -
 *         see Arena Sizing above.
 */
bool http_service_multipart_parse(HTTP_Service_Multipart *const self, char const *const boundary, Byte const *const payload, USize const payload_size);

/**
 * @brief Release service storage and parsed parts.
 * @param self Service instance.
 */
void http_service_multipart_uninit(HTTP_Service_Multipart *const self);

#endif // HTTP_SERVICE_MULTIPART_H