/*
 * arena_linear.c - Linear arena allocator implementation
 *
 * See arena_linear.h for public API documentation.
 */

#include <arena/arena_linear.h>

/*==============================================================================
 * MARK: - Memory Layout
 *============================================================================*/

/*
 * ArenaLinear uses one contiguous allocation:
 *
 *   +----------------------+----------------------------------------------+
 *   | ArenaLinear metadata | user allocation buffer                       |
 *   +----------------------+----------------------------------------------+
 *                          ^
 *                          self->data
 *
 * The metadata stores:
 *   - capacity: total usable bytes in the buffer.
 *   - data:     pointer to the first usable byte after metadata.
 *   - size:     current bump offset, also the number of bytes already used.
 *
 * Each allocation returns:
 *
 *   (U8*) self->data + old_size
 *
 * Then it advances:
 *
 *   self->size = old_size + byte_count
 *
 * That means allocation is O(1), ordered, and has no per-block metadata. Individual
 * frees are impossible because the arena does not remember block boundaries. Clear
 * zeroes the full buffer and resets size to 0, making the entire arena reusable.
 */

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

struct ArenaLinear {
    /* Total usable byte count in the data buffer. */
    USize capacity;

    /* First byte available for user allocations. */
    void *data;

    /* Current bump offset from data, also used byte count. */
    USize size;
};

/*==============================================================================
 * MARK: - API
 *============================================================================*/

void* arena_linear_alloc(ArenaLinear *const self, USize const byte_count) {
    trace_log_push(LOG_METADATA);

    // The bound check below uses the ALIGNED size that try_alloc will actually consume, so this
    // guard and the allocation agree; otherwise a request could pass here and still return null.
    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_non_value_uint(LOG_METADATA, "byte_count", byte_count);
    error_check_out_of_bound_uint(LOG_METADATA,
        "MEMORY_ALIGN_UP(byte_count)", MEMORY_ALIGN_UP(byte_count),
        "self->capacity - self->size", self->capacity - self->size,
        "MEMORY_ALIGN_UP(byte_count) > self->capacity - self->size", MEMORY_ALIGN_UP(byte_count) > self->capacity - self->size);

    void *const buffer = arena_linear_try_alloc(self, byte_count);

    /* The out-of-bound check above is the rich diagnostic for ordinary
     * exhaustion, but MEMORY_ALIGN_UP wraps for byte_count near USIZE_MAX and
     * slips past it; this trailing check keeps the abort contract airtight,
     * uniform with arena_stack_alloc and arena_pool_alloc. */
    error_check_message(LOG_METADATA, memory_empty(buffer), "arena_linear_alloc: arena exhausted");

    trace_log_pop();

    return buffer;
}

void arena_linear_clear(ArenaLinear *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    memory_set(self->data, self->capacity, 0);

    self->size = 0;

    trace_log_pop();
}

void arena_linear_delete(ArenaLinear **const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "*self", (void*) *self);

    /* Deliberately no clear before the free. It only ever guarded against the NEXT allocation
     * seeing these bytes, and memory_alloc now always zeroes, so wiping a whole arena on the
     * way out buys nothing and costs a full-capacity memset. Scrubbing secrets before handing
     * pages back to the OS is a separate concern and wants an explicit call, not a side effect
     * of delete. */
    memory_delete((void**) &(*self));

    trace_log_pop();
}

bool arena_linear_empty(ArenaLinear const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    bool const empty = self->size == 0;

    trace_log_pop();

    return empty;
}

void arena_linear_free(ArenaLinear *const self, void *const buffer) {
    (void) self;

    (void) buffer;
}

ArenaLinear* arena_linear_new(USize const capacity) {
    trace_log_push(LOG_METADATA);

    /* Refusals return nullptr, as the header promises - including capacity 0
     * and allocation failure (memory_try_alloc, not the aborting memory_alloc,
     * so a computed capacity degrades into a rejected arena, not a dead
     * process). arena_init_2 pre-filters 0; direct callers get the same shape. */
    if (capacity == 0) {
        trace_log_pop();

        return nullptr;
    }

    /* The metadata header is rounded up so `data` itself lands on a MEMORY_ALIGNMENT boundary.
     * memory_alloc returns a max-aligned BLOCK, but data is an offset INTO it - and
     * sizeof(ArenaLinear) is not a multiple of the alignment, so without this every slice would
     * sit at a fixed odd offset no matter how carefully the bump is rounded. The allocation
     * grows by the same amount, or the last bytes of capacity would fall outside the block. */
    USize const header = MEMORY_ALIGN_UP(sizeof(ArenaLinear));

    if (capacity > USIZE_MAX - header) {
        trace_log_pop();

        return nullptr;
    }

    ArenaLinear *const arena = (ArenaLinear*) memory_try_alloc(header + capacity);

    if (memory_empty(arena)) {
        trace_log_pop();

        return nullptr;
    }

    arena->capacity    = capacity;
    arena->data        = (void*) ((U8*) arena + header);
    arena->size        = 0;

    trace_log_pop();

    return arena;
}

void* arena_linear_try_alloc(ArenaLinear *const self, USize const byte_count) {
    trace_log_push(LOG_METADATA);

    /* Deliberately check-free: exhaustion is a runtime condition here, not a
     * programmer error, so every rejection is a null rather than an abort. This
     * is what lets a request parser survive a body sized to drain the arena. */
    /* Rounded UP before the bump, not just for the bound check. arena_linear_new aligns the base
     * `data` pointer, and rounding every slice keeps every LATER pointer on the same boundary -
     * both halves are needed, since aligning only the sizes preserves whatever offset the base
     * started at. Without it a single odd-length slice (any short string) misaligns every
     * subsequent aligned type: UB that x86-64 tolerates silently and ARM faults on. */
    USize const aligned = MEMORY_ALIGN_UP(byte_count);

    if (memory_empty(self) || byte_count == 0 || aligned < byte_count || aligned > self->capacity - self->size) {
        trace_log_pop();

        return nullptr;
    }

    USize const old_size    = self->size;
    self->size             += aligned;

    trace_log_pop();

    return (U8*) self->data + old_size;
}