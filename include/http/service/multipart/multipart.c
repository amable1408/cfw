#include <http/service/multipart/multipart.h>

/*==============================================================================
 * MARK: - Internal Helpers
 *============================================================================*/

/* Alphabetical: none of these six depends on one defined later in this order
 * (_find_delimiter's only dependency, _find, already precedes it), so the
 * public-API ordering convention (see style_guidelines.md) applies as-is -
 * dependency order and alphabetical order agree here. */

static char* _http_service_multipart_extract_string(HTTP_Service_Multipart const *const self, char const *const source, USize const size) {
    trace_log_push(LOG_METADATA);

    if (source == nullptr || size == 0) {
        trace_log_pop();

        return nullptr;
    }

    /* try_borrow, not borrow: the size comes from an uploaded document, and the
     * aborting borrow would end the process on an arena drained by a crafted
     * upload before any null check here could run. */
#ifdef ARENA_IMPLEMENTATION
    char *const buffer = (char*) allocator_try_borrow(size + 1, self->allocator);
#else
    /* self carries only the allocator, so the non-arena build has no use for it. */
    (void) self;

    char *const buffer = (char*) allocator_try_borrow(size + 1);
#endif // ARENA_IMPLEMENTATION

    /* Report the field as absent - the callers already treat these as optional. */
    if (memory_empty(buffer)) {
        trace_log_pop();

        return nullptr;
    }

    char_copy_2(buffer, source, size);

    /* char_copy_2 writes exactly `size` bytes and no terminator, yet every consumer
     * treats this as a C string. Kept explicit rather than leaning on the allocator's
     * zeroing, so the invariant survives this buffer later moving to an arena. */
    buffer[size] = '\0';

    trace_log_pop();

    return buffer;
}

/* memchr-first search for `needle` (needle_len bytes) in payload starting at
 * `start`. Narrows to candidate positions of the needle's leading byte before
 * comparing the rest, and is called once per delimiter: the caller reuses a
 * found position as the next search's start rather than re-finding it. */
static USize _http_service_multipart_find(Byte const *const payload, USize const payload_size, USize const start, char const *const needle, USize const needle_len) {
    if (needle_len == 0 || start > payload_size || needle_len > payload_size - start) {
        return USIZE_MAX;
    }

    Byte const first = (Byte) needle[0];
    USize const last_start = payload_size - needle_len;
    USize pos = start;

    while (pos <= last_start) {
        void const *const found = memchr(payload + pos, first, last_start - pos + 1);

        if (found == nullptr) {
            return USIZE_MAX;
        }

        USize const candidate = (USize) ((Byte const*) found - payload);

        if (memcmp(payload + candidate, needle, needle_len) == 0) {
            return candidate;
        }

        pos = candidate + 1;
    }

    return USIZE_MAX;
}

/* Case-insensitive search for the "Content-Type:" header field name, with an
 * optional single space before the value. Returns a pointer to the value, or
 * nullptr.
 *
 * Skips a candidate found inside an ODD-parity run of '"' bytes seen so far -
 * i.e. inside a still-open quoted value of an EARLIER parameter - so literal
 * text "content-type:" typed inside a quoted filename (e.g.
 * filename="content-type: fake") is never mistaken for a real header field.
 * This module does no quoted-string unescaping (see the header's Limitations),
 * so "still open" is exactly "an odd number of '"' bytes precede it", not a
 * true parse of the quoted grammar - good enough to refuse the smuggle without
 * pretending to a fidelity this parser does not have. */
static char const* _http_service_multipart_find_content_type(char const *const headers) {
    static char const KEY[] = "content-type:";
    USize const key_len = CHAR_STATIC_SIZE("content-type:");
    USize const headers_len = char_length(headers);

    if (headers_len < key_len) {
        return nullptr;
    }

    USize quote_count = 0;

    for (USize i = 0; i + key_len <= headers_len; i += 1) {
        if ((quote_count % 2) == 0) {
            bool match = true;

            for (USize j = 0; j < key_len; j += 1) {
                if (char_to_lower(headers[i + j]) != KEY[j]) {
                    match = false;

                    break;
                }
            }

            if (match) {
                USize value_start = i + key_len;

                if (headers[value_start] == ' ') {
                    value_start += 1;
                }

                return headers + value_start;
            }
        }

        if (headers[i] == '"') {
            quote_count += 1;
        }
    }

    return nullptr;
}

/* Finds the next genuine delimiter: a full_boundary match that RFC 2046 5.1.1
 * requires to be followed by "--" (close) or a line ending, not an arbitrary
 * occurrence of the boundary bytes inside a part's own data. A candidate that
 * fails this is skipped and the search resumes just past it, so a boundary
 * value that happens to appear as a data prefix (e.g. boundary "abc" and data
 * starting "--abcd") is never mistaken for a delimiter. */
static USize _http_service_multipart_find_delimiter(Byte const *const payload, USize const payload_size, USize const start, char const *const full_boundary, USize const full_boundary_len) {
    USize pos = start;

    for (;;) {
        USize const candidate = _http_service_multipart_find(payload, payload_size, pos, full_boundary, full_boundary_len);

        if (candidate == USIZE_MAX) {
            return USIZE_MAX;
        }

        USize const after = candidate + full_boundary_len;

        bool const is_close = after + 2 <= payload_size && payload[after] == '-' && payload[after + 1] == '-';
        bool const is_crlf  = after + 2 <= payload_size && payload[after] == '\r' && payload[after + 1] == '\n';
        bool const is_lf    = after + 1 <= payload_size && payload[after] == '\n';

        if (is_close || is_crlf || is_lf) {
            return candidate;
        }

        pos = candidate + 1;
    }
}

/* Finds `key` (e.g. "name=\"") in a NUL-terminated header block, accepting a
 * match only when preceded by ';' or whitespace (or the block's start) AND not
 * sitting inside a still-open quoted value of an earlier parameter (see the
 * quote-parity note on _http_service_multipart_find_content_type - the same
 * approximation applies here). The before-check alone is what stops "name=\""
 * from matching inside "filename=\""; the quote-parity check is what stops it
 * from matching text that only LOOKS like a parameter because it sits inside
 * an earlier parameter's still-open quoted value (e.g.
 * filename="a; name="y"" must not read as name=y).
 *
 * headers_len and key_len are computed ONCE, and the running quote_count only
 * ever counts the newly-passed span since the last candidate - not a rescan
 * from the start - so this stays a single forward pass over the header block
 * as a whole. */
static char const* _http_service_multipart_find_param(char const *const headers, char const *const key) {
    USize const headers_len = char_length(headers);
    USize const key_len = char_length(key);
    USize index = 0;
    USize quote_count = 0;

    for (;;) {
        char const *const found = char_find_slice_5(headers, headers_len, index, key, key_len);

        if (found == nullptr) {
            return nullptr;
        }

        USize const found_index = (USize) (found - headers);

        for (USize i = index; i < found_index; i += 1) {
            if (headers[i] == '"') {
                quote_count += 1;
            }
        }

        char const before = found_index == 0 ? ';' : headers[found_index - 1];
        bool const inside_quotes = (quote_count % 2) != 0;

        if (!inside_quotes && (before == ';' || char_is_whitespace(before))) {
            return found;
        }

        index = found_index + 1;
    }
}

#ifdef ARENA_IMPLEMENTATION
static HTTP_Service_Multipart _http_service_multipart_init(Arena *const allocator)
#else
static HTTP_Service_Multipart _http_service_multipart_init(void)
#endif // ARENA_IMPLEMENTATION
{
    HTTP_Service_Multipart multipart = DEFAULT_INITIALIZATION;

#ifdef ARENA_IMPLEMENTATION
    /* The list's arena constructor aborts on a null allocator, so a null
     * allocator (the heap constructor's case) is routed to the heap path
     * instead of passed through. */
    multipart.allocator = allocator;
    multipart.parts = allocator != nullptr ? al_multipart_alloc_init_1(allocator) : al_multipart_init_1();
#else
    multipart.parts = al_multipart_init_1();
#endif // ARENA_IMPLEMENTATION

    multipart.max_parts        = HTTP_SERVICE_MULTIPART_DEFAULT_MAX_PARTS;
    multipart.max_header_size  = HTTP_SERVICE_MULTIPART_DEFAULT_MAX_HEADER_SIZE;

    return multipart;
}

/*==============================================================================
 * MARK: - API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
HTTP_Service_Multipart http_service_multipart_alloc_init(Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    HTTP_Service_Multipart const multipart = _http_service_multipart_init(allocator);

    trace_log_pop();

    return (HTTP_Service_Multipart) multipart;
}
#endif // ARENA_IMPLEMENTATION

HTTP_Service_Multipart_Node* http_service_multipart_at(HTTP_Service_Multipart const *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    HTTP_Service_Multipart_Node *const node = al_multipart_at(&self->parts, index);

    trace_log_pop();

    return node;
}

bool http_service_multipart_boundary(char const *const content_type, char *const buffer, USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "content_type", (void*) content_type);
    error_check_null(LOG_METADATA, "buffer", (void*) buffer);

    /* A zero capacity is a legal VALUE here (an undersized caller buffer), not a
     * broken contract - refuse before writing the empty-string terminator below,
     * which would otherwise write buffer[0] one byte past a zero-byte buffer. */
    if (capacity == 0) {
        trace_log_pop();

        return false;
    }

    buffer[0] = '\0';

    char const *found = char_find_slice_3(content_type, 0, "boundary=");

    if (found == nullptr) {
        trace_log_pop();

        return false;
    }

    found += CHAR_STATIC_SIZE("boundary=");

    bool const quoted = found[0] == '"';

    if (quoted) {
        found += 1;
    }

    USize out = 0;

    while (found[out] != '\0' && found[out] != ';' && !(quoted && found[out] == '"') && out + 1 < capacity) {
        buffer[out] = found[out];
        out += 1;
    }

    buffer[out] = '\0';

    trace_log_pop();

    return out > 0;
}

USize http_service_multipart_count(HTTP_Service_Multipart const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    USize const count = al_multipart_get_size(&self->parts);

    trace_log_pop();

    return count;
}

HTTP_Service_Multipart http_service_multipart_init(void) {
#ifdef ARENA_IMPLEMENTATION
    return _http_service_multipart_init(nullptr);
#else
    return _http_service_multipart_init();
#endif // ARENA_IMPLEMENTATION
}

HTTP_Service_Multipart_Node* http_service_multipart_node_get(HTTP_Service_Multipart const *const self, char const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    for (USize i = 0; i < al_multipart_get_size(&self->parts); i += 1) {
        HTTP_Service_Multipart_Node const *const part = al_multipart_at(&self->parts, i);

        if (part->name != nullptr && char_compare_equal_1(part->name, name)) {
            trace_log_pop();

            return (HTTP_Service_Multipart_Node*) part;
        }
    }

    trace_log_pop();

    return nullptr;
}

bool http_service_multipart_parse(HTTP_Service_Multipart *const self, char const *const boundary, Byte const *const payload, USize const payload_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "boundary", (void*) boundary);

    /* A zero-size payload is a legal VALUE (Content-Length: 0, an empty form
     * submit) - refused here, before the null check below, so an empty body
     * whose payload pointer is also null is a refusal rather than an abort.
     * A non-null payload with a zero size never reaches this parser from a
     * real HTTP layer, so it is not distinguished from the null case. */
    if (payload_size == 0) {
        trace_log_pop();

        return false;
    }

    error_check_null(LOG_METADATA, "payload", (void*) payload);

    USize const boundary_size = char_length(boundary);

    if (boundary_size == 0 || boundary_size > HTTP_SERVICE_MULTIPART_BOUNDARY_MAX_LENGTH) {
        trace_log_pop();

        return false;
    }

    /* try_borrow: boundary_size comes from the request's Content-Type header. */
#ifdef ARENA_IMPLEMENTATION
    char *const full_boundary = (char*) allocator_try_borrow(boundary_size + 3, self->allocator);
#else
    char *const full_boundary = (char*) allocator_try_borrow(boundary_size + 3);
#endif // ARENA_IMPLEMENTATION

    /* Refuse the parse instead of writing to null. */
    if (memory_empty(full_boundary)) {
        trace_log_pop();

        return false;
    }

    full_boundary[0] = '-';
    full_boundary[1] = '-';

    char_copy_2(full_boundary + 2, boundary, boundary_size);

    full_boundary[boundary_size + 2] = '\0';

    /* Reserved ONCE, here, rather than left to al_multipart_add_last's own
     * incremental growth: the loop below never adds past max_parts, so a
     * capacity of max_parts up front guarantees add_last never needs to grow
     * mid-parse. That confines an arena too small to hold the list to a single,
     * predictable abort point at parse entry instead of an abort landing on an
     * unpredictable part deep into the payload - see the header's Arena Sizing
     * section for the budgeting rule and why this is an abort, not a decline,
     * when the arena is merely too small (a REFUSED arena still declines here,
     * same as everywhere else in this module). Skipped when max_parts is
     * EXPLICITLY 0 (unbounded): there is no fixed count to reserve for, so
     * add_last's own incremental growth is what unbounded mode has always used. */
    if (self->max_parts != 0) {
        al_multipart_reserve(&self->parts, self->max_parts);
    }

    USize const full_boundary_len = boundary_size + 2;
    USize       boundary_pos      = _http_service_multipart_find_delimiter(payload, payload_size, 0, full_boundary, full_boundary_len);
    bool        closed            = false;

    while (boundary_pos != USIZE_MAX) {
        if (boundary_pos + full_boundary_len + 2 <= payload_size &&
            payload[boundary_pos + full_boundary_len] == '-'     &&
            payload[boundary_pos + full_boundary_len + 1] == '-') {
            closed = true;

            break;
        }

        /* Refuse past the part cap before spending a header-block borrow on
         * a part that will not be kept; the parts collected so far stay
         * valid for the caller. */
        if (self->max_parts != 0 && al_multipart_get_size(&self->parts) >= self->max_parts) {
            break;
        }

        USize headers_start = boundary_pos + full_boundary_len;

        if (headers_start + 2 <= payload_size && payload[headers_start] == '\r' && payload[headers_start + 1] == '\n') {
            headers_start += 2;
        }
        else if (headers_start + 1 <= payload_size && payload[headers_start] == '\n') {
            headers_start += 1;
        }

        /* The delimiter that will end THIS part's data is also the hard upper bound
         * for the header-terminator search below: without it, a "\r\n\r\n" or "\n\n"
         * sitting in a LATER part's own data (or this part's own data, past where the
         * real terminator is) could be mistaken for THIS part's terminator, silently
         * swallowing an intervening boundary into what gets treated as "headers"
         * ("header smuggling"). Searching from headers_start finds the same position
         * a search from data_start would - nothing between them can be a genuine
         * delimiter, since find_delimiter's own RFC 2046 5.1.1 check would reject a
         * boundary-shaped run inside header text unless it were followed by "--" or
         * a line ending, which header text never is - so this one lookup also serves
         * as `next_boundary_pos` used for data_end further down; no second scan. */
        USize const next_boundary_pos = _http_service_multipart_find_delimiter(payload, payload_size, headers_start, full_boundary, full_boundary_len);

        if (next_boundary_pos == USIZE_MAX) {
            break;
        }

        /* Searched 4 bytes past the bound, not exactly at it: the terminator is
         * "\r\n\r\n"/"\n\n" (up to 4 bytes), so a header block whose true length is
         * within max_header_size but whose terminator STRADDLES headers_start +
         * max_header_size (e.g. length max_header_size - 2) would otherwise never
         * be found and the parse would refuse a block that is in fact within
         * bounds. The `> max_header_size` check just below is what actually
         * enforces the bound; this only makes sure it gets a chance to run
         * instead of being pre-empted by a search window that ends too early. */
        USize search_limit = next_boundary_pos;

        if (self->max_header_size != 0 && headers_start + self->max_header_size + 4 < search_limit) {
            search_limit = headers_start + self->max_header_size + 4;
        }

        USize const crlf_headers_end = _http_service_multipart_find(payload, search_limit, headers_start, "\r\n\r\n", 4);
        USize const lf_headers_end   = _http_service_multipart_find(payload, search_limit, headers_start, "\n\n", 2);

        USize headers_end = 0;
        USize headers_separator_len = 0;

        /* Whichever terminator occurs EARLIEST wins, not whichever spelling is
         * tried first: a part genuinely terminated by the nearer "\n\n" must never
         * be extended out to a "\r\n\r\n" that only happens to occur later (inside
         * this part's own data, which is exactly the smuggling shape above). */
        if (crlf_headers_end == USIZE_MAX && lf_headers_end == USIZE_MAX) {
            break;
        }
        else if (lf_headers_end == USIZE_MAX || (crlf_headers_end != USIZE_MAX && crlf_headers_end <= lf_headers_end)) {
            headers_end           = crlf_headers_end;
            headers_separator_len = 4;
        }
        else {
            headers_end           = lf_headers_end;
            headers_separator_len = 2;
        }

        if (self->max_header_size != 0 && (headers_end - headers_start) > self->max_header_size) {
            break;
        }

        USize const data_start = headers_end + headers_separator_len;

        USize data_end = next_boundary_pos;

        /* Floored at data_start, not at 0: a part with an EMPTY body puts the next
         * boundary immediately after the header separator, so stripping the CRLF
         * without this floor drives data_end below data_start and the subtraction
         * below wraps to ~2^64 - a size every consumer would then read through. */
        if (data_end >= data_start + 2 && payload[data_end - 2] == '\r' && payload[data_end - 1] == '\n') {
            data_end -= 2;
        }
        else if (data_end >= data_start + 1 && payload[data_end - 1] == '\n') {
            data_end -= 1;
        }

        if (data_end < data_start) {
            data_end = data_start;
        }

        HTTP_Service_Multipart_Node part = DEFAULT_INITIALIZATION;

        part.data = &payload[data_start];
        part.size = data_end - data_start;

        /* try_borrow: the header block's length is whatever the request sent. */
#ifdef ARENA_IMPLEMENTATION
        char *const headers_str = (char*) allocator_try_borrow((headers_end - headers_start) + 1, self->allocator);
#else
        char *const headers_str = (char*) allocator_try_borrow((headers_end - headers_start) + 1);
#endif // ARENA_IMPLEMENTATION

        /* Stop the parse rather than append a part whose headers were never read;
         * the parts collected so far stay valid for the caller. full_boundary is
         * released first: every other exit from this loop is a break that reaches
         * the release below, and with no arena it is a plain malloc. */
        if (memory_empty(headers_str)) {
#ifdef ARENA_IMPLEMENTATION
            allocator_release((void*) full_boundary, self->allocator);
#else
            allocator_release((void*) full_boundary);
#endif // ARENA_IMPLEMENTATION

            trace_log_pop();

            return false;
        }

        for (USize i = 0; i < headers_end - headers_start; i += 1) {
            headers_str[i] = (char) payload[headers_start + i];
        }

        /* The copy loop writes no terminator, and everything below reads this as
         * a C string. */
        headers_str[headers_end - headers_start] = '\0';

        char const *name_ptr = _http_service_multipart_find_param(headers_str, "name=\"");

        if (name_ptr != nullptr) {
            name_ptr += CHAR_STATIC_SIZE("name=\"");

            char const *name_end = char_find_slice_1(name_ptr, 0, '"');

            if (name_end != nullptr) {
                part.name = _http_service_multipart_extract_string(self, name_ptr, (USize) (name_end - name_ptr));
            }
        }

        char const *filename_ptr = _http_service_multipart_find_param(headers_str, "filename=\"");

        if (filename_ptr != nullptr) {
            filename_ptr += CHAR_STATIC_SIZE("filename=\"");

            char const *filename_end = char_find_slice_1(filename_ptr, 0, '"');

            if (filename_end != nullptr) {
                part.filename = _http_service_multipart_extract_string(self, filename_ptr, (USize) (filename_end - filename_ptr));
            }
        }

        char const *ct_ptr = _http_service_multipart_find_content_type(headers_str);

        if (ct_ptr != nullptr) {
            USize const ct_end_index = char_find_first_1(ct_ptr, "\r\n");
            USize const ct_len       = ct_end_index == CHAR_NPOS ? char_length(ct_ptr) : ct_end_index;

            part.content_type = _http_service_multipart_extract_string(self, ct_ptr, ct_len);
        }

        USize const parts_before = al_multipart_get_size(&self->parts);

        al_multipart_add_last(&self->parts, &part);

        /* This branch is dead in the common (max_parts != 0) case: the reservation
         * at parse entry already sized the list for max_parts nodes, and this loop
         * never adds past max_parts (see the guard above), so add_last here never
         * needs to grow and so cannot decline. It stays reachable only for the
         * max_parts == 0 (explicitly unbounded) case, where add_last still grows
         * incrementally - a REFUSED arena declines that growth silently, leaving
         * the size unchanged, exactly like everywhere else in this module; an
         * arena that is merely out of remaining room instead ABORTS under
         * ERROR_CHECK_ENABLED (see the header's Arena Sizing section), so it never
         * reaches this comparison at all. The three strings were already borrowed
         * above, so a genuine decline puts them beyond uninit's reach; release
         * them here and stop parsing instead. */
        if (al_multipart_get_size(&self->parts) == parts_before) {
#ifdef ARENA_IMPLEMENTATION
            allocator_release((void*) part.name, self->allocator);
            allocator_release((void*) part.filename, self->allocator);
            allocator_release((void*) part.content_type, self->allocator);
            allocator_release((void*) headers_str, self->allocator);
            allocator_release((void*) full_boundary, self->allocator);
#else
            allocator_release((void*) part.name);
            allocator_release((void*) part.filename);
            allocator_release((void*) part.content_type);
            allocator_release((void*) headers_str);
            allocator_release((void*) full_boundary);
#endif // ARENA_IMPLEMENTATION

            trace_log_pop();

            /* The parts collected so far stay valid and owned by the list, so the
             * caller still uninits normally; this reports a truncated parse. */
            return false;
        }

#ifdef ARENA_IMPLEMENTATION
        allocator_release((void*) headers_str, self->allocator);
#else
        allocator_release((void*) headers_str);
#endif // ARENA_IMPLEMENTATION

        /* Reuse the delimiter just found as the next iteration's boundary_pos
         * instead of searching for it again: each delimiter is located once. */
        boundary_pos = next_boundary_pos;
    }

#ifdef ARENA_IMPLEMENTATION
    allocator_release((void*) full_boundary, self->allocator);
#else
    allocator_release((void*) full_boundary);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return closed;
}

void http_service_multipart_uninit(HTTP_Service_Multipart *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* The three header-derived strings are borrowed per part by
     * _http_service_multipart_extract_string; nothing else references them, so
     * releasing them is this function's job. `data` and `size` are deliberately
     * not released: they point INTO the caller's payload buffer (see the
     * header's Memory section), so this list owns the strings and only the
     * strings.
     *
     * The release lives here rather than in al_multipart_clear/_remove because a
     * generic container cannot tell an owned field from a borrowed one; the
     * service is the only place that knows. */
    for (USize i = 0; i < al_multipart_get_size(&self->parts); i += 1) {
        HTTP_Service_Multipart_Node *const part = al_multipart_at(&self->parts, i);

#ifdef ARENA_IMPLEMENTATION
        allocator_release((void*) part->name, self->allocator);
        allocator_release((void*) part->filename, self->allocator);
        allocator_release((void*) part->content_type, self->allocator);
#else
        allocator_release((void*) part->name);
        allocator_release((void*) part->filename);
        allocator_release((void*) part->content_type);
#endif // ARENA_IMPLEMENTATION

        /* Hygiene only: al_multipart_uninit's clear zeroes every slot immediately
         * below, so nothing can reach these pointers again either way. */
#ifdef MEMORY_NON_DANGLING_POINTER
        part->name = nullptr;
        part->filename = nullptr;
        part->content_type = nullptr;
#endif // MEMORY_NON_DANGLING_POINTER
    }

    al_multipart_uninit(&self->parts);

    trace_log_pop();
}