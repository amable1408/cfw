#include <container/arrayList/al_u8.h>

#define _AL_U8_GROWTH_FACTOR 2

/*==============================================================================
 * MARK: - Internal
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
static AL_U8 _al_u8_init(Arena *allocator)
#else
static AL_U8 _al_u8_init(void)
#endif // ARENA_IMPLEMENTATION
{
    AL_U8 al_u8 = DEFAULT_INITIALIZATION;

#ifdef ARENA_IMPLEMENTATION
    if (!memory_empty((void*) allocator)) {
        al_u8.allocator = allocator;
    }
#endif // ARENA_IMPLEMENTATION

    return al_u8;
}

/*==============================================================================
 * MARK: - Arena Constructors
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
AL_U8 al_u8_alloc_init_1(Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    AL_U8 al_u8 = _al_u8_init(allocator);

    trace_log_pop();

    return al_u8;
}

AL_U8 al_u8_alloc_init_2(USize const capacity, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    AL_U8 al_u8 = al_u8_alloc_init_1(allocator);

    al_u8.capacity = capacity;

    /* The byte size is a bare multiply: a large capacity wraps it, so the
     * borrow below would return a tiny block while the stored capacity stays
     * enormous. reserve refuses this already; the constructors set capacity
     * directly and need the same guard. */
    if (al_u8.capacity > USIZE_MAX / sizeof(U8)) {
        al_u8.capacity = 0;

        trace_log_pop();

        return al_u8;
    }

    al_u8.data = (U8*) allocator_borrow(sizeof(U8) * al_u8.capacity, allocator);

    /* A REFUSED arena hands back null here. Leaving capacity at n with a
     * null data defeats add()'s refused-reserve re-read - it compares size
     * against capacity, sees 0 != n, and writes through the null. Zeroing
     * capacity here is what makes that re-read, shrink, at() and the init_3
     * copy guard all degrade correctly. */
    if (memory_empty((void*) al_u8.data)) {
        al_u8.capacity = 0;
    }

    trace_log_pop();

    return al_u8;
}

AL_U8 al_u8_alloc_init_3(U8 const *const data, USize const data_size, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_non_value_uint(LOG_METADATA, "data_size", data_size);

    AL_U8 al_u8 = al_u8_alloc_init_2(data_size, allocator);

    /* Bounded by BOTH: the write is bounded by capacity, the read of data[i] by the
     * caller's data_size. They are independent quantities, and the source bound is
     * dead only because init_2 currently sets capacity to exactly data_size or 0 -
     * any future change that rounds capacity up (as bit-packed al_bool's word
     * granularity invites) would turn this into a read past the caller's array. */
    for (USize i = 0; i < al_u8.capacity && i < data_size; i += 1) {
        al_u8.data[i] = data[i];

        al_u8.size += 1;
    }

    trace_log_pop();

    return al_u8;
}

AL_U8* al_u8_alloc_new_1(Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    AL_U8 *const al_u8 = (AL_U8*) allocator_borrow(sizeof(AL_U8), allocator);

    /* The STRUCT borrow, not the element buffer: a refused arena returns null
     * here, and writing through it would put a whole struct through null. This
     * frame needs the same refused-arena guard the other constructors carry,
     * or the module header's blanket "a refused arena leaves the list
     * unchanged" promise is false for the whole alloc_new_* trio. */
    if (memory_empty((void*) al_u8)) {
        trace_log_pop();

        return nullptr;
    }

    *al_u8 = al_u8_alloc_init_1(allocator);

    trace_log_pop();

    return al_u8;
}

AL_U8* al_u8_alloc_new_2(USize const capacity, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    AL_U8 *const al_u8 = al_u8_alloc_new_1(allocator);

    /* alloc_new_1 now answers null on a refused arena. */
    if (memory_empty((void*) al_u8)) {
        trace_log_pop();

        return nullptr;
    }

    *al_u8 = al_u8_alloc_init_2(capacity, allocator);

    trace_log_pop();

    return al_u8;
}

AL_U8* al_u8_alloc_new_3(U8 const *const data, USize const data_size, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_non_value_uint(LOG_METADATA, "data_size", data_size);

    AL_U8 *const al_u8 = al_u8_alloc_new_1(allocator);

    /* alloc_new_1 now answers null on a refused arena. */
    if (memory_empty((void*) al_u8)) {
        trace_log_pop();

        return nullptr;
    }

    *al_u8 = al_u8_alloc_init_3(data, data_size, allocator);

    trace_log_pop();

    return al_u8;
}
#endif // ARENA_IMPLEMENTATION

/*==============================================================================
 * MARK: - Element Access
 *============================================================================*/

void al_u8_add(AL_U8 *const self, U8 const data, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "self->size", self->size, "index > self->size", index > self->size);

    if (self->size == self->capacity) {
        if (self->capacity == 0) {
            al_u8_reserve(self, _AL_U8_GROWTH_FACTOR);
        }
        else {
            al_u8_reserve(self, self->capacity > USIZE_MAX / _AL_U8_GROWTH_FACTOR ? USIZE_MAX : self->capacity * _AL_U8_GROWTH_FACTOR);
        }
    }

    /* reserve REFUSES a refused arena and a wrapping byte size, and reports it
     * by leaving capacity alone. Without this re-read the place below wrote at
     * self->data[size] with size == capacity - through null for a list that
     * never allocated, one past the end otherwise. */
    if (self->size == self->capacity) {
        trace_log_pop();

        return;
    }

    // Shift [index, size) right by one, then place; when index == size the loop is a
    // no-op and this is a plain append.
    for (USize i = self->size; i > index; i -= 1) {
        self->data[i] = self->data[i - 1];
    }

    self->data[index] = data;

    self->size += 1;

    trace_log_pop();
}

void al_u8_add_first(AL_U8 *const self, U8 const data) {
    trace_log_push(LOG_METADATA);

    al_u8_add(self, data, 0);

    trace_log_pop();
}

void al_u8_add_last(AL_U8 *const self, U8 const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    al_u8_add(self, data, self->size);

    trace_log_pop();
}

U8* al_u8_at(AL_U8 const *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "self->size", self->size, "index >= self->size", index >= self->size);
    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "self->capacity", self->capacity, "index >= self->capacity", index >= self->capacity);

    /* Both bounds, in the two files that expose set_size. set_size now CLAMPS, so
     * size <= capacity is an invariant and this second check is redundant - kept as
     * a cheap diagnostic because it is the one that would fire first if the clamp
     * ever regressed. Note it compiles out without ERROR_CHECK_ENABLED, so the clamp
     * is the real protection and this is only the messenger. */

    trace_log_pop();

    return (U8*) &(self->data[index]);
}

U8* al_u8_back(AL_U8 const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* An empty list has no last element, and `self->size - 1` underflowed to
     * USIZE_MAX and formed a wild address the caller then dereferenced. Refuse
     * by returning null - the emptiness is a data question, not a broken
     * contract, so it must not abort. */
    if (self->size == 0) {
        trace_log_pop();

        return nullptr;
    }

    trace_log_pop();

    return (U8*) &(self->data[self->size - 1]);
}

void al_u8_clear(AL_U8 *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    for (USize i = 0; i < self->size; i += 1) {
        self->data[i] = 0;
    }

    self->size = 0;

    trace_log_pop();
}

void al_u8_delete(AL_U8 **const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "*self", (void*) *self);

    al_u8_uninit(*self);

#ifdef ARENA_IMPLEMENTATION
    allocator_release((void*) (*self), (*self)->allocator);
#else
    allocator_release((void*) (*self));
#endif // ARENA_IMPLEMENTATION

#ifdef MEMORY_NON_DANGLING_POINTER
    *self = nullptr;
#endif // MEMORY_NON_DANGLING_POINTER

    trace_log_pop();
}

bool al_u8_empty(AL_U8 const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->size == 0;
}

U8* al_u8_front(AL_U8 const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* An empty list has no first element, and an unallocated one has no buffer
     * to take an address inside. Refuse
     * by returning null - the emptiness is a data question, not a broken
     * contract, so it must not abort. */
    if (self->size == 0) {
        trace_log_pop();

        return nullptr;
    }

    trace_log_pop();

    return (U8*) &(self->data[0]);
}

USize al_u8_get_capacity(AL_U8 const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->capacity;
}

U8* al_u8_get_data(AL_U8 const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->data;
}

USize al_u8_get_size(AL_U8 const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->size;
}

/*==============================================================================
 * MARK: - Heap Constructors
 *============================================================================*/

AL_U8 al_u8_init_1(void) {
#ifdef ARENA_IMPLEMENTATION
    return _al_u8_init(nullptr);
#else
    return _al_u8_init();
#endif // ARENA_IMPLEMENTATION
}

AL_U8 al_u8_init_2(USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    AL_U8 al_u8 = al_u8_init_1();

    al_u8.capacity = capacity;

    /* The byte size is a bare multiply: a large capacity wraps it, so the
     * borrow below would return a tiny block while the stored capacity stays
     * enormous. reserve refuses this already; the constructors set capacity
     * directly and need the same guard. */
    if (al_u8.capacity > USIZE_MAX / sizeof(U8)) {
        al_u8.capacity = 0;

        trace_log_pop();

        return al_u8;
    }

#ifdef ARENA_IMPLEMENTATION
    al_u8.data = (U8*) allocator_borrow(sizeof(U8) * al_u8.capacity, nullptr);
#else
    al_u8.data = (U8*) allocator_borrow(sizeof(U8) * al_u8.capacity);
#endif // ARENA_IMPLEMENTATION

    /* A REFUSED arena hands back null here. Leaving capacity at n with a
     * null data defeats add()'s refused-reserve re-read - it compares size
     * against capacity, sees 0 != n, and writes through the null. Zeroing
     * capacity here is what makes that re-read, shrink, at() and the init_3
     * copy guard all degrade correctly. */
    if (memory_empty((void*) al_u8.data)) {
        al_u8.capacity = 0;
    }

    trace_log_pop();

    return al_u8;
}

AL_U8 al_u8_init_3(U8 const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_non_value_uint(LOG_METADATA, "data_size", data_size);

    AL_U8 al_u8 = al_u8_init_2(data_size);

    /* Bounded by BOTH: the write is bounded by capacity, the read of data[i] by the
     * caller's data_size. They are independent quantities, and the source bound is
     * dead only because init_2 currently sets capacity to exactly data_size or 0 -
     * any future change that rounds capacity up (as bit-packed al_bool's word
     * granularity invites) would turn this into a read past the caller's array. */
    for (USize i = 0; i < al_u8.capacity && i < data_size; i += 1) {
        al_u8.data[i] = data[i];

        al_u8.size += 1;
    }

    trace_log_pop();

    return al_u8;
}

AL_U8* al_u8_new_1(void) {
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    AL_U8 *const al_u8 = (AL_U8*) allocator_borrow(sizeof(AL_U8), nullptr);
#else
    AL_U8 *const al_u8 = (AL_U8*) allocator_borrow(sizeof(AL_U8));
#endif // ARENA_IMPLEMENTATION

    *al_u8 = al_u8_init_1();

    trace_log_pop();

    return al_u8;
}

AL_U8* al_u8_new_2(USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    AL_U8 *const al_u8 = al_u8_new_1();

    *al_u8 = al_u8_init_2(capacity);

    trace_log_pop();

    return al_u8;
}

AL_U8* al_u8_new_3(U8 const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_non_value_uint(LOG_METADATA, "data_size", data_size);

    AL_U8 *const al_u8 = al_u8_new_1();

    *al_u8 = al_u8_init_3(data, data_size);

    trace_log_pop();

    return al_u8;
}

/*==============================================================================
 * MARK: - Removal and Capacity
 *============================================================================*/

void al_u8_remove(AL_U8 *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "self->size", self->size, "index >= self->size", index >= self->size);

    if (index < self->size - 1) {
        for (USize i = index; i < self->size - 1; i += 1) {
            self->data[i] = self->data[i + 1];
        }
    }

    self->data[self->size - 1] = 0;

    self->size -= 1;

    trace_log_pop();
}

void al_u8_remove_first(AL_U8 *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* An empty list has nothing to remove; remove() would abort on the bound. */
    if (self->size == 0) {
        trace_log_pop();

        return;
    }

    al_u8_remove(self, 0);

    trace_log_pop();
}

void al_u8_remove_last(AL_U8 *const self) {
    trace_log_push(LOG_METADATA);

    /* self->size is read in the ARGUMENT below, so it is dereferenced before
     * remove's own error_check_null can run - a null self segfaulted instead of
     * producing the diagnostic. On an empty list the subtraction also wrapped to
     * USIZE_MAX and aborted with a bounds message about a nonsense index. */
    error_check_null(LOG_METADATA, "self", (void*) self);

    if (self->size == 0) {
        trace_log_pop();

        return;
    }

    al_u8_remove(self, self->size - 1);

    trace_log_pop();
}

void al_u8_reserve(AL_U8 *const self, USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    if (capacity > self->capacity) {
        USize const capacity_before = self->capacity;
        U8 *const buffer = self->data;

        self->capacity = capacity;

        /* The byte size is a bare multiply: a large capacity wraps it, so the
         * borrow returns a tiny block while the stored capacity stays enormous
         * and every later write lands past it. Refuse instead. */
        if (self->capacity > USIZE_MAX / sizeof(U8)) {
            self->capacity = capacity_before;

            trace_log_pop();

            return;
        }

#ifdef ARENA_IMPLEMENTATION
        self->data = (U8*) allocator_borrow(sizeof(U8) * self->capacity, self->allocator);
#else
        self->data = (U8*) allocator_borrow(sizeof(U8) * self->capacity);
#endif // ARENA_IMPLEMENTATION

        /* Null only from a REFUSED arena (allocator_borrow's documented graceful
         * path). Put the old buffer back and refuse the growth rather than
         * writing through null on the very next line. */
        if (memory_empty((void*) self->data)) {
            self->data = buffer;
            self->capacity = capacity_before;

            trace_log_pop();

            return;
        }

        for (USize i = 0; i < self->size; i += 1) {
            self->data[i] = buffer[i];
        }

        if (!memory_empty((void*) buffer)) {
#ifdef ARENA_IMPLEMENTATION
            allocator_release((void*) buffer, self->allocator);
#else
            allocator_release((void*) buffer);
#endif // ARENA_IMPLEMENTATION
        }
    }

    trace_log_pop();
}

void al_u8_set_size(AL_U8 *const self, USize const size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* CLAMPED, never trusted: an unchecked size is the one input that broke this. That form
     * let a caller state a size the storage cannot back, and at() is only one of SEVEN
     * readers keyed on size - add's shift and place, back, clear's zeroing loop, remove,
     * remove_last and uninit all trust it too. `init_2(4); set_size(1 << 26); add_last(x);`
     * wrote 64 MiB past a 4-byte block through the public API alone.
     *
     * Clamping keeps the function and its purpose - pairing with reserve to fill a
     * buffer directly - while refusing to let the count outrun the storage. A caller
     * that wants a larger size reserves first; if the reserve declines, the clamp is
     * what makes that visible instead of fatal. */
    /* Named because it is a real trade: the clamp turns an out-of-bounds write
     * into SILENT truncation. A caller doing reserve(n); set_size(n); then filling
     * n slots now under-fills and hands downstream code a short list that looks
     * complete. Nothing is emitted here - the container has no logger in scope -
     * so a caller that must know compares get_size against what it asked for. */
    self->size = size > self->capacity ? self->capacity : size;

    trace_log_pop();
}

void al_u8_shrink(AL_U8 *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* An empty list releases and resets instead of borrowing zero bytes:
     * memory_alloc aborts on a zero byte count, so `init_2(n); shrink();`
     * - a pure public sequence - killed the process. */
    if (self->size == 0) {
#ifdef ARENA_IMPLEMENTATION
        allocator_release((void*) self->data, self->allocator);
#else
        allocator_release((void*) self->data);
#endif // ARENA_IMPLEMENTATION

        /* Load-bearing, like uninit's: without it a second shrink on the same
         * empty list hands the already-released buffer back to the allocator.
         * Not hygiene, so it stays unconditional (style_guidelines.md, Dangling
         * Pointer Guards, load-bearing-nulls exception). */
        self->data = nullptr;
        self->capacity = 0;

        trace_log_pop();

        return;
    }

    if (self->size < self->capacity) {
        USize const capacity_before = self->capacity;
        U8 *const buffer = self->data;

        self->capacity = self->size;

        /* The byte size is a bare multiply: a large capacity wraps it, so the
         * borrow returns a tiny block while the stored capacity stays enormous
         * and every later write lands past it. Refuse instead. */
        if (self->capacity > USIZE_MAX / sizeof(U8)) {
            self->capacity = capacity_before;

            trace_log_pop();

            return;
        }

#ifdef ARENA_IMPLEMENTATION
        self->data = (U8*) allocator_borrow(sizeof(U8) * self->capacity, self->allocator);
#else
        self->data = (U8*) allocator_borrow(sizeof(U8) * self->capacity);
#endif // ARENA_IMPLEMENTATION

        /* Null only from a REFUSED arena (allocator_borrow's documented graceful
         * path). Put the old buffer back and refuse the growth rather than
         * writing through null on the very next line. */
        if (memory_empty((void*) self->data)) {
            self->data = buffer;
            self->capacity = capacity_before;

            trace_log_pop();

            return;
        }

        for (USize i = 0; i < self->capacity; i += 1) {
            self->data[i] = buffer[i];
        }

#ifdef ARENA_IMPLEMENTATION
        allocator_release((void*) buffer, self->allocator);
#else
        allocator_release((void*) buffer);
#endif // ARENA_IMPLEMENTATION
    }

    trace_log_pop();
}

void al_u8_uninit(AL_U8 *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    al_u8_clear(self);

#ifdef ARENA_IMPLEMENTATION
    allocator_release((void*) self->data, self->allocator);
#else
    allocator_release((void*) self->data);
#endif // ARENA_IMPLEMENTATION

    /* Unconditional, not gated on MEMORY_NON_DANGLING_POINTER: leaving the freed
     * pointer in place made a second uninit hand it back to the allocator, so
     * whether uninit was idempotent depended on a build flag. */
    self->data = nullptr;

    self->capacity = 0;

    trace_log_pop();
}