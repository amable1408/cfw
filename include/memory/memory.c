/*
 * memory.c - Implementation of canonical memory management utilities for the C Libraries Framework
 *
 * See memory.h for API documentation, usage, and error handling notes.
 */

#include <memory/memory.h>

/*==============================================================================
 * MARK: - API
 *============================================================================*/

void* memory_alloc(USize const byte_count) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "byte_count", byte_count);

    /* calloc, not malloc plus memory_set: the zeroing is unconditional now, and calloc lets
     * the allocator hand back kernel pages that are already zero rather than faulting in and
     * touching every page up front. For a large block that is the difference between paying
     * for the whole allocation immediately and paying only for what is used. */
    void const *const buffer = (void const*) calloc(byte_count, 1);

    error_check_null(LOG_METADATA, "buffer", (void*) buffer);

    if (buffer == nullptr) {
        trace_log_pop();

        return nullptr;
    }

    trace_log_pop();

    return (void*) buffer;
}

void memory_copy_1(void *const dst, void const *const src, USize const src_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dst", (void*) dst);
    error_check_null(LOG_METADATA, "src", (void*) src);

    /* A zero-length copy is a legal no-op (the framework's empty-value policy:
     * only capacity/allocation sizes are illegal at 0). Aborting here forced
     * callers forwarding input-derived lengths to carry their own guards. */
    if (src_size == 0) {
        trace_log_pop();

        return;
    }

    memcpy(dst, src, src_size);

    trace_log_pop();
}

void memory_copy_2(void *const dst, USize const dst_size, void const *const src, USize const src_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dst", (void*) dst);
    error_check_null(LOG_METADATA, "src", (void*) src);
    error_check_out_of_bound_uint(LOG_METADATA, "dst_size", dst_size, "src_size", src_size, "dst_size < src_size", dst_size < src_size);

    /* Zero-length: legal no-op (see memory_copy_1). Overflowing copy: REFUSED
     * unconditionally - the error check above is the loud diagnostic in
     * checked builds, but the bound must hold in release builds too, or the
     * "safe copy" is only safe by build flag. */
    if (src_size == 0 || dst_size < src_size) {
        trace_log_pop();

        return;
    }

    memcpy(dst, src, src_size);

    trace_log_pop();
}

void memory_delete(void **const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_null(LOG_METADATA, "*data", (void*) *data);

    memory_free(*data);

#ifdef MEMORY_NON_DANGLING_POINTER
    *data = nullptr;
#endif // MEMORY_NON_DANGLING_POINTER

    trace_log_pop();
}

bool memory_empty(void const *const self) {
    return self == nullptr;
}

USize memory_fit_size(USize const byte_size, USize const byte_count) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "byte_size", byte_size);
    error_check_non_value_uint(LOG_METADATA, "byte_count", byte_count);

    if (byte_size == 0 || byte_count == 0 || byte_count > USIZE_MAX / byte_size) {
        trace_log_pop();

        return 0;
    }

    USize const byte_total = byte_size * byte_count;
    USize byte_fit = 1;

    while (byte_fit < byte_total) {
        if (byte_fit > USIZE_MAX / 2) {
            trace_log_pop();

            return byte_total;
        }

        byte_fit *= 2;
    }

    trace_log_pop();

    return byte_fit;
}

void memory_free(void const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    free((void*) data);

    trace_log_pop();
}

void* memory_realloc(void const *const data, USize const old_byte_count, USize const byte_count) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "byte_count", byte_count);

    /* realloc(ptr, 0) is outright UB in C23 (7.24.3.7; implementation-defined
     * before). The checked build aborts above; this keeps the unchecked build
     * refusing instead of reaching the UB, and makes both branches agree. */
    if (byte_count == 0) {
        trace_log_pop();

        return nullptr;
    }

    if (data == nullptr) {
        /* The recovering allocator, deliberately: this branch is the grow-from-
         * empty first iteration of the exact loop memory_realloc exists for, and
         * the documented contract is nullptr-on-failure on EVERY path - an abort
         * here made the same call recover or die based on the pointer's history. */
        void const *const buffer = (void const*) memory_try_alloc(byte_count);

        trace_log_pop();

        return (void*) buffer;
    }

    void const *const buffer = (void const*) realloc((void*) data, byte_count);

    if (buffer == nullptr) {
        trace_log_pop();

        return nullptr;
    }

    /* realloc keeps the old bytes and leaves everything past them undefined, which is the one
     * hole in the framework's always-zeroed guarantee. Closing it here means the guarantee
     * survives a resize, so a grown buffer reads the same as a fresh one. Only the delta is
     * touched - rewriting the preserved bytes would defeat the point of reallocating. */
    if (byte_count > old_byte_count) {
        memory_set((void*) ((char*) buffer + old_byte_count), byte_count - old_byte_count, 0);
    }

    trace_log_pop();

    return (void*) buffer;
}

void memory_set(void *const dst, USize const dst_size, U8 const value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dst", (void*) dst);
    error_check_non_value_uint(LOG_METADATA, "dst_size", dst_size);

    memset(dst, (I32) value, dst_size);

    trace_log_pop();
}

void* memory_try_alloc(USize const byte_count) {
    trace_log_push(LOG_METADATA);

    if (byte_count == 0) {
        trace_log_pop();

        return nullptr;
    }

    /* calloc for the same reason memory_alloc uses it: unconditional zeroing, paid lazily by
     * the allocator rather than by touching every page here. */
    void const *const buffer = (void const*) calloc(byte_count, 1);

    if (buffer == nullptr) {
        trace_log_pop();

        return nullptr;
    }

    trace_log_pop();

    return (void*) buffer;
}