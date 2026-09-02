/*
 * allocator.c - Implementation of canonical allocator interface for the C Libraries Framework
 *
 * See allocator.h for API documentation, usage, and error handling notes.
 */

#include <allocator/allocator.h>

/*==============================================================================
 * MARK: - API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
void* allocator_borrow(USize const byte_count, Arena *const allocator)
#else
void* allocator_borrow(USize const byte_count)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    if (allocator == nullptr) {
        void *const buffer = (void*) memory_alloc(byte_count);

        trace_log_pop();

        return buffer;
    } else {
        /* A null handler is a REFUSED arena (arena_init_2 rejects bad geometry
         * by leaving handler null with live hooks); calling through would abort
         * inside the arena's own self check, at a site that points away from
         * the failed init. Same graceful nullptr as a null hook. */
        if (allocator->allocate == nullptr || allocator->handler == nullptr) {
            trace_log_pop();

            return nullptr;
        }

        void *const buffer = (void*) allocator->allocate(allocator->handler, byte_count);

        trace_log_pop();

        return buffer;
    }
#else
    void *const buffer = (void*) memory_alloc(byte_count);

    trace_log_pop();

    return buffer;
#endif // ARENA_IMPLEMENTATION
}

#ifdef ARENA_IMPLEMENTATION
void allocator_release(void *const buffer, Arena *const allocator)
#else
void allocator_release(void *const buffer)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    /* A null buffer is IGNORED, as with free(NULL). memory_free itself still aborts on null -
     * that abort is a deliberate bug-catcher for code that frees a pointer it should be holding
     * - but this is the GENERIC seam, and a legitimately empty owner arrives here routinely: a
     * container that was created and never grown has data == nullptr, and all ~25 al_*_uninit
     * pass it straight through. Without this guard, tearing down a service that never saw a
     * single entry killed the process on shutdown. */
    if (memory_empty(buffer)) {
        trace_log_pop();

        return;
    }

#ifdef ARENA_IMPLEMENTATION
    if (allocator == nullptr) {
        memory_free(buffer);
    } else {
        /* Null handler = refused/uninitialized arena; a no-op, as with a null hook. */
        if (allocator->deallocate != nullptr && allocator->handler != nullptr) {
            allocator->deallocate(allocator->handler, buffer);
        }
    }
#else
    memory_free(buffer);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();
}

#ifdef ARENA_IMPLEMENTATION
void* allocator_try_borrow(USize const byte_count, Arena *const allocator)
#else
void* allocator_try_borrow(USize const byte_count)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    if (allocator == nullptr) {
        void *const buffer = (void*) memory_try_alloc(byte_count);

        trace_log_pop();

        return buffer;
    }

    /* Null handler = refused/uninitialized arena; nullptr, as with a null hook. */
    if (allocator->try_allocate == nullptr || allocator->handler == nullptr) {
        trace_log_pop();

        return nullptr;
    }

    void *const buffer = (void*) allocator->try_allocate(allocator->handler, byte_count);

    trace_log_pop();

    return buffer;
#else
    void *const buffer = (void*) memory_try_alloc(byte_count);

    trace_log_pop();

    return buffer;
#endif // ARENA_IMPLEMENTATION
}
