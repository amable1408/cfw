/*
 * arena_stack.c - Implementation of stack arena allocator for the C Libraries Framework
 *
 * See arena_stack.h for API documentation, usage, and error handling notes.
 */

#include <arena/arena_stack.h>

/*
 * Memory Layout for Stack Arena Allocator
 *
 * The stack arena manages a contiguous buffer for LIFO (last-in, first-out) allocations.
 * Each allocation pushes the data pointer forward by the requested size, then stores a header
 * (USize) immediately after the allocation, recording the size of the block. The pointer is then
 * advanced past the header. Deallocation (free) pops the most recent allocation by reading the
 * header, moving the pointer back, and zeroing the memory.
 *
 * Layout (after several allocations):
 *
 * +-------------------+-------------------+-------------------+----- ... -----+
 * | Block 1 (data)    | Header 1 (USize)  | Block 2 (data)    | Header 2 ...  |
 * +-------------------+-------------------+-------------------+----- ... -----+
 * ^                   ^                   ^
 * |                   |                   |
 * start of buffer     ...                 current pointer (self->data)
 *
 * Sizes are rounded up before use, so N below is always MEMORY_ALIGN_UP(requested) and the
 * header occupies a full alignment slot (_ARENA_STACK_HEADER_SIZE), not sizeof(USize). Budgeting
 * a block at block + sizeof(USize) under-counts every block - the exact defect that made the
 * public ARENA_STACK_HEADER_SIZE disagree with what the implementation consumed.
 *
 * For each allocation:
 *   - Allocate N bytes: pointer += N
 *   - Write header: pointer += _ARENA_STACK_HEADER_SIZE, store N
 *   - Return pointer to start of block
 *
 * For free:
 *   - pointer -= _ARENA_STACK_HEADER_SIZE, read N
 *   - pointer -= N
 *   - Zero memory for N + _ARENA_STACK_HEADER_SIZE
 */

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/* The per-block header occupies a full alignment slot rather than sizeof(USize). The stride is
 * block + header, so a header of 8 against a 16-byte alignment would make every second block
 * land on an odd boundary no matter how well the block sizes are rounded. */
#define _ARENA_STACK_HEADER_SIZE MEMORY_ALIGN_UP(sizeof(USize))

struct ArenaStack {
    USize   capacity;
    void    *data;
    USize   size;
};

/*==============================================================================
 * MARK: - API
 *============================================================================*/

void* arena_stack_alloc(ArenaStack *const self, USize const byte_count) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_non_value_uint(LOG_METADATA, "byte_count", byte_count);

    void *const buffer = arena_stack_try_alloc(self, byte_count);

    /* allocate treats exhaustion as a programmer error and aborts (the
     * arena.h/allocator.h contract, matching arena_linear_alloc); try_alloc is
     * the recoverable path for input-derived sizes. */
    error_check_message(LOG_METADATA, memory_empty(buffer), "arena_stack_alloc: arena exhausted");

    trace_log_pop();

    return buffer;
}

void arena_stack_delete(ArenaStack **const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "*self", (void*) *self);

    memory_delete((void**) self);

    trace_log_pop();
}

bool arena_stack_empty(ArenaStack const *const self) {
    trace_log_push(LOG_METADATA);
    
    error_check_null(LOG_METADATA, "self", (void*) self);
    
    trace_log_pop();
    
    return self->size == 0;
}

void arena_stack_free(ArenaStack *const self, void *const buffer) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* A null buffer is IGNORED, matching free(NULL). It used to fall through the LIFO guard
     * below and pop - and zero - whatever block was on top. That is reachable without anyone
     * writing nullptr on purpose: allocator_release forwards straight here, and al_u64_uninit
     * and its ~20 al_*_uninit siblings hand over self->data unguarded, which is nullptr for a
     * container that was created but never grown. The result was a live block released out from
     * under its owner, silently, because the pop zeroes it on the way out. */
    if (memory_empty(buffer)) {
        trace_log_pop();

        return;
    }

    /* No error_check on size: a release against an empty arena - a double free, or a free before
     * any allocation - is exactly what the guard below absorbs, and aborting one line earlier
     * would turn that defensive path into a process kill. */
    if (self->size < _ARENA_STACK_HEADER_SIZE) {
        trace_log_pop();

        return;
    }

    USize const data_size = *(USize const*) ((U8*) self->data - _ARENA_STACK_HEADER_SIZE);

    if (data_size > self->size - _ARENA_STACK_HEADER_SIZE) {
        trace_log_pop();

        return;
    }

    /* This arena is LIFO: only the most recent block can be released. The buffer used
     * to be absent from the signature entirely - arena.c cast this one-parameter
     * function to the two-parameter FpArenaFree and called it as deallocate(handler,
     * buffer), which is undefined behaviour AND silently discarded the pointer, so
     * releasing any non-top allocation popped an unrelated live block instead.
     * Refusing the out-of-order release leaks that block rather than corrupting one. */
    /* The block starts a FULL block-plus-header below the current pointer. Comparing against
     * `data - data_size` was off by the header, so it never matched what try_alloc returned and
     * every non-null release was silently refused - the arena only ever grew. */
    if (buffer != (void*) ((U8*) self->data - data_size - _ARENA_STACK_HEADER_SIZE)) {
        trace_log_pop();

        return;
    }

    USize const block_total = data_size + _ARENA_STACK_HEADER_SIZE;

    self->size -= block_total;
    self->data = (void*) ((U8*) self->data - block_total);

    memory_set(self->data, block_total, 0);

    trace_log_pop();
}

ArenaStack* arena_stack_new(USize const capacity) {
    trace_log_push(LOG_METADATA);

    /* Refusals return nullptr, as the header promises - including capacity 0
     * and allocation failure (memory_try_alloc, not the aborting memory_alloc). */
    if (capacity == 0) {
        trace_log_pop();

        return nullptr;
    }

    /* Same reason as arena_linear_new: memory_alloc returns a max-aligned BLOCK, but `data` is an
     * offset into it, and sizeof(ArenaStack) is not a multiple of the alignment. */
    USize const metadata = MEMORY_ALIGN_UP(sizeof(ArenaStack));

    if (capacity > USIZE_MAX - metadata) {
        trace_log_pop();

        return nullptr;
    }

    ArenaStack *const stack = (ArenaStack*) memory_try_alloc(metadata + capacity);

    if (memory_empty(stack)) {
        trace_log_pop();

        return nullptr;
    }

    stack->capacity = capacity;
    stack->data = (void*) ((U8*) stack + metadata);
    stack->size = 0;

    trace_log_pop();

    return stack;
}

void* arena_stack_try_alloc(ArenaStack *const self, USize const byte_count) {
    trace_log_push(LOG_METADATA);

    /* Deliberately check-free; see arena_linear_try_alloc for why. */
    /* Aligned for the same reason as arena_linear_try_alloc, and it matters twice here: the
     * USize header written after each block must land on an aligned address too, or the header
     * write itself is a misaligned store. The header records the ALIGNED size, so
     * arena_stack_free unwinds exactly what was consumed. */
    USize const aligned = MEMORY_ALIGN_UP(byte_count);

    if (memory_empty(self)
        || byte_count == 0
        || aligned < byte_count
        || self->size + _ARENA_STACK_HEADER_SIZE > self->capacity
        || aligned > self->capacity - self->size - _ARENA_STACK_HEADER_SIZE) {
        trace_log_pop();

        return nullptr;
    }

    self->size += aligned + _ARENA_STACK_HEADER_SIZE;

    self->data = (void*) ((U8*) self->data + aligned);
    USize *const header_size = (USize*) self->data;
    *header_size = aligned;
    self->data = (void*) ((U8*) self->data + _ARENA_STACK_HEADER_SIZE);

    trace_log_pop();

    return (void*) ((U8*) self->data - aligned - _ARENA_STACK_HEADER_SIZE);
}