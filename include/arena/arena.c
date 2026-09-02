/*
 * arena.c - Implementation of canonical arena interface for the C Libraries Framework
 *
 * See arena.h for API documentation, usage, and error handling notes.
 */

#include <arena/arena.h>

/*==============================================================================
 * MARK: - API
 *============================================================================*/

/* The whole API is gated WITH its declarations (arena.h): without
 * ARENA_IMPLEMENTATION these non-void functions would compile as empty bodies
 * and fall off the end - runtime UB where a compile error belongs.
 *
 * The (FpArenaAlloc)/(FpArenaFree) casts below call each arena's typed
 * functions through the generic TpArena-shaped pointer type - formally
 * incompatible function types, accepted deliberately on the same uniform-
 * pointer-representation grounds memory.h documents for memory_delete's
 * (void**) cast. Reopen if a target enables -fsanitize=function, CFI, or wasm. */
#ifdef ARENA_IMPLEMENTATION
Arena arena_init_1(USize const byte_count, ArenaType const arena_type) {
    trace_log_push(LOG_METADATA);

    Arena arena = arena_init_2(sizeof(Byte), byte_count, arena_type);

    trace_log_pop();

    return arena;
}

Arena arena_init_2(USize const byte_size, USize const byte_count, ArenaType const arena_type) {
    trace_log_push(LOG_METADATA);

    error_check_wrong_value(LOG_METADATA,
        "arena_type != ARENA_TYPE_LINEAR && arena_type != ARENA_TYPE_POOL && arena_type != ARENA_TYPE_STACK",
        arena_type != ARENA_TYPE_LINEAR && arena_type != ARENA_TYPE_POOL && arena_type != ARENA_TYPE_STACK);

    Arena arena = DEFAULT_INITIALIZATION;

    /* Defensive: arena_init_2 has no non-test caller in this tree today, so nothing is known to
     * reach the wrap - the guard exists because this is public API and the two-operand shape
     * invites a caller to pass an element size and an input-derived count. A wrapped product
     * fails in one of two ways, neither of them a clean error:
     * a non-zero remainder yields a TINY arena whose allocations then return nullptr into
     * consumers (string_alloc_init_2, json_alloc_from_3) that do not null-check, and a product
     * landing on exactly 0 - which any byte_size of 2^k * byte_count of 2^(64-k) does - reaches
     * arena_linear_new(0) and ABORTS the process outright. Rejecting up front leaves `handler`
     * null, the exact failure shape a failed *_new already produces; the allocator seam treats
     * a null handler like a null hook, so a later allocator_borrow/try_borrow on the refused
     * arena returns nullptr instead of calling through. ARENA_TYPE_POOL is unaffected: it
     * forwards both operands and guards the product itself.
     *
     * A plain zero operand is folded into the SAME predicate, not just the wrap. Without that,
     * arena_init_2(0, n) leaves the product at 0, reaches arena_linear_new(0), and aborts on its
     * error_check_non_value_uint - the precise outcome this guard exists to prevent. Refusing
     * both makes LINEAR and STACK behave like POOL, which already returned nullptr for 0. */
    bool  const size_invalid    = byte_size == 0 || byte_count == 0 || byte_size > USIZE_MAX / byte_count;
    USize const total_size      = size_invalid ? 0 : byte_size * byte_count;

    switch (arena_type) {
        case ARENA_TYPE_LINEAR: {
            arena.allocate = (FpArenaAlloc) arena_linear_alloc;
            arena.deallocate = (FpArenaFree) arena_linear_free;
            arena.try_allocate = (FpArenaAlloc) arena_linear_try_alloc;
            arena.handler = size_invalid ? nullptr : (TpArena*) arena_linear_new(total_size);
            break;
        }
        case ARENA_TYPE_POOL: {
            /* The facade is uniformly BYTE-denominated: hand out the *_bytes
             * adapters, or flipping ArenaType at one init site would silently
             * reinterpret every borrow in scope as a block count. Direct
             * arena_pool_* callers keep the block-denominated API. */
            arena.allocate = (FpArenaAlloc) arena_pool_alloc_bytes;
            arena.deallocate = (FpArenaFree) arena_pool_free;
            arena.try_allocate = (FpArenaAlloc) arena_pool_try_alloc_bytes;
            arena.handler = (TpArena*) arena_pool_new(byte_size, byte_count);
            break;
        }
        case ARENA_TYPE_STACK: {
            arena.allocate = (FpArenaAlloc) arena_stack_alloc;
            /* arena_stack_free now takes the buffer, so this cast only adjusts the
             * handler's pointer type - it no longer changes the function's arity. */
            arena.deallocate = (FpArenaFree) arena_stack_free;
            arena.try_allocate = (FpArenaAlloc) arena_stack_try_alloc;
            arena.handler = size_invalid ? nullptr : (TpArena*) arena_stack_new(total_size);
            break;
        }
    }

    trace_log_pop();

    return arena;
}

void arena_uninit(Arena *const self, ArenaType const arena_type) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* A refused init leaves handler null, and every arena_*_delete error_checks `*self` - so
     * pairing init with uninit, which is what a correct caller does, would abort precisely on
     * the error path. Skip straight to the pointer nulling. This became reachable BY DESIGN for
     * LINEAR and STACK when arena_init_2 started refusing bad sizes; POOL could already get
     * here through a failed arena_pool_new. */
    if (memory_empty(self->handler)) {
        #ifdef MEMORY_NON_DANGLING_POINTER
        self->allocate      = nullptr;
        self->deallocate    = nullptr;
        self->try_allocate  = nullptr;
        #endif // MEMORY_NON_DANGLING_POINTER

        trace_log_pop();

        return;
    }

    switch (arena_type) {
        case ARENA_TYPE_LINEAR: {
            arena_linear_delete((ArenaLinear**) &self->handler);
            break;
        }
        case ARENA_TYPE_POOL: {
            arena_pool_delete((ArenaPool**) &self->handler);
            break;
        }
        case ARENA_TYPE_STACK: {
            arena_stack_delete((ArenaStack**) &self->handler);
            break;
        }
    }

#ifdef MEMORY_NON_DANGLING_POINTER
    self->allocate     = nullptr;
    self->deallocate   = nullptr;
    self->handler      = nullptr;
    self->try_allocate = nullptr;
#endif // MEMORY_NON_DANGLING_POINTER

    trace_log_pop();
}
#endif // ARENA_IMPLEMENTATION