#include <container/arrayList/al_void.h>

#define _AL_VOID_GROWTH_FACTOR 2

/*==============================================================================
 * MARK: - Internal
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
static AL_Void _al_void_init(Arena *allocator)
#else
static AL_Void _al_void_init(void)
#endif // ARENA_IMPLEMENTATION
{
    AL_Void al_void = DEFAULT_INITIALIZATION;

#ifdef ARENA_IMPLEMENTATION
    if (!memory_empty((void*) allocator)) {
        al_void.allocator = allocator;
    }
#endif // ARENA_IMPLEMENTATION

    return al_void;
}

/*==============================================================================
 * MARK: - Arena Constructors
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
AL_Void al_void_alloc_init_1(Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    AL_Void al_void = _al_void_init(allocator);

    trace_log_pop();

    return al_void;
}

AL_Void al_void_alloc_init_2(USize const capacity, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    AL_Void al_void = al_void_alloc_init_1(allocator);

    al_void.capacity = capacity;

    /* The byte size is a bare multiply: a large capacity wraps it, so the
     * borrow below would return a tiny block while the stored capacity stays
     * enormous. reserve refuses this already; the constructors set capacity
     * directly and need the same guard. */
    if (al_void.capacity > USIZE_MAX / sizeof(void*)) {
        al_void.capacity = 0;

        trace_log_pop();

        return al_void;
    }

    al_void.data = (void**) allocator_borrow(sizeof(void*) * al_void.capacity, allocator);

    /* A REFUSED arena hands back null here. Leaving capacity at n with a
     * null data defeats add()'s refused-reserve re-read - it compares size
     * against capacity, sees 0 != n, and writes through the null. Zeroing
     * capacity here is what makes that re-read, shrink, at() and the init_3
     * copy guard all degrade correctly. */
    if (memory_empty((void*) al_void.data)) {
        al_void.capacity = 0;
    }

    trace_log_pop();

    return al_void;
}

AL_Void al_void_alloc_init_3(void *const *const data, USize const data_size, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_non_value_uint(LOG_METADATA, "data_size", data_size);

    AL_Void al_void = al_void_alloc_init_2(data_size, allocator);

    /* Bounded by BOTH: the write is bounded by capacity, the read of data[i] by the
     * caller's data_size. They are independent quantities, and the source bound is
     * dead only because init_2 currently sets capacity to exactly data_size or 0 -
     * any future change that rounds capacity up (as bit-packed al_bool's word
     * granularity invites) would turn this into a read past the caller's array. */
    for (USize i = 0; i < al_void.capacity && i < data_size; i += 1) {
        al_void.data[i] = data[i];

        al_void.size += 1;
    }

    trace_log_pop();

    return al_void;
}

AL_Void* al_void_alloc_new_1(Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    AL_Void *const al_void = (AL_Void*) allocator_borrow(sizeof(AL_Void), allocator);

    /* The STRUCT borrow, not the element buffer: a refused arena returns null
     * here, and writing through it would put a whole struct through null. This
     * frame needs the same refused-arena guard the other constructors carry,
     * or the module header's blanket "a refused arena leaves the list
     * unchanged" promise is false for the whole alloc_new_* trio. */
    if (memory_empty((void*) al_void)) {
        trace_log_pop();

        return nullptr;
    }

    *al_void = al_void_alloc_init_1(allocator);

    trace_log_pop();

    return al_void;
}

AL_Void* al_void_alloc_new_2(USize const capacity, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    AL_Void *const al_void = al_void_alloc_new_1(allocator);

    /* alloc_new_1 now answers null on a refused arena. */
    if (memory_empty((void*) al_void)) {
        trace_log_pop();

        return nullptr;
    }

    *al_void = al_void_alloc_init_2(capacity, allocator);

    trace_log_pop();

    return al_void;
}

AL_Void* al_void_alloc_new_3(void *const *const data, USize const data_size, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_non_value_uint(LOG_METADATA, "data_size", data_size);

    AL_Void *const al_void = al_void_alloc_new_1(allocator);

    /* alloc_new_1 now answers null on a refused arena. */
    if (memory_empty((void*) al_void)) {
        trace_log_pop();

        return nullptr;
    }

    *al_void = al_void_alloc_init_3(data, data_size, allocator);

    trace_log_pop();

    return al_void;
}
#endif // ARENA_IMPLEMENTATION

/*==============================================================================
 * MARK: - Element Access
 *============================================================================*/

void al_void_add(AL_Void *const self, void *const data, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "self->size", self->size, "index > self->size", index > self->size);

    if (self->size == self->capacity) {
        /* Clamped like the rest of the family: the multiply below wraps at
         * capacities above USIZE_MAX / factor, and a wrapped capacity sizes the
         * sizeof(void*) * capacity borrow far too small. */
        USize const grown = self->capacity == 0
            ? _AL_VOID_GROWTH_FACTOR
            : (self->capacity > USIZE_MAX / _AL_VOID_GROWTH_FACTOR ? USIZE_MAX : self->capacity * _AL_VOID_GROWTH_FACTOR);

        al_void_reserve(self, grown);
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

void al_void_add_first(AL_Void *const self, void *const data) {
    trace_log_push(LOG_METADATA);

    al_void_add(self, data, 0);

    trace_log_pop();
}

void al_void_add_last(AL_Void *const self, void *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    al_void_add(self, data, self->size);

    trace_log_pop();
}

void** al_void_at(AL_Void const *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "self->size", self->size, "index >= self->size", index >= self->size);

    trace_log_pop();

    return (void**) &(self->data[index]);
}

void** al_void_back(AL_Void const *const self) {
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

    return (void**) &(self->data[self->size - 1]);
}

void al_void_clear(AL_Void *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* Hygiene only: this list owns no elements, so the loop releases nothing and
     * size drops to 0 below - nothing reads these slots again. */
#ifdef MEMORY_NON_DANGLING_POINTER
    for (USize i = 0; i < self->size; i += 1) {
        self->data[i] = nullptr;
    }
#endif // MEMORY_NON_DANGLING_POINTER

    self->size = 0;

    trace_log_pop();
}

void al_void_delete(AL_Void **const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "*self", (void*) *self);

    al_void_uninit(*self);

#ifdef ARENA_IMPLEMENTATION
    allocator_release((void*) (*self), (*self)->allocator);
#else
    allocator_release((void*) (*self));
#endif // ARENA_IMPLEMENTATION

    // Clear the caller's handle; never write through *self, which was just released.
#ifdef MEMORY_NON_DANGLING_POINTER
    *self = nullptr;
#endif // MEMORY_NON_DANGLING_POINTER

    trace_log_pop();
}

bool al_void_empty(AL_Void const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->size == 0;
}

void** al_void_front(AL_Void const *const self) {
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

    return (void**) &(self->data[0]);
}

USize al_void_get_capacity(AL_Void const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->capacity;
}

void** al_void_get_data(AL_Void const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->data;
}

USize al_void_get_size(AL_Void const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->size;
}

/*==============================================================================
 * MARK: - Heap Constructors
 *============================================================================*/

AL_Void al_void_init_1(void) {
#ifdef ARENA_IMPLEMENTATION
    return _al_void_init(nullptr);
#else
    return _al_void_init();
#endif // ARENA_IMPLEMENTATION
}

AL_Void al_void_init_2(USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    AL_Void al_void = al_void_init_1();

    al_void.capacity = capacity;

    /* The byte size is a bare multiply: a large capacity wraps it, so the
     * borrow below would return a tiny block while the stored capacity stays
     * enormous. reserve refuses this already; the constructors set capacity
     * directly and need the same guard. */
    if (al_void.capacity > USIZE_MAX / sizeof(void*)) {
        al_void.capacity = 0;

        trace_log_pop();

        return al_void;
    }

#ifdef ARENA_IMPLEMENTATION
    al_void.data = (void**) allocator_borrow(sizeof(void*) * al_void.capacity, nullptr);
#else
    al_void.data = (void**) allocator_borrow(sizeof(void*) * al_void.capacity);
#endif // ARENA_IMPLEMENTATION

    /* A REFUSED arena hands back null here. Leaving capacity at n with a
     * null data defeats add()'s refused-reserve re-read - it compares size
     * against capacity, sees 0 != n, and writes through the null. Zeroing
     * capacity here is what makes that re-read, shrink, at() and the init_3
     * copy guard all degrade correctly. */
    if (memory_empty((void*) al_void.data)) {
        al_void.capacity = 0;
    }

    trace_log_pop();

    return al_void;
}

AL_Void al_void_init_3(void *const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_non_value_uint(LOG_METADATA, "data_size", data_size);

    AL_Void al_void = al_void_init_2(data_size);

    /* Bounded by BOTH: the write is bounded by capacity, the read of data[i] by the
     * caller's data_size. They are independent quantities, and the source bound is
     * dead only because init_2 currently sets capacity to exactly data_size or 0 -
     * any future change that rounds capacity up (as bit-packed al_bool's word
     * granularity invites) would turn this into a read past the caller's array. */
    for (USize i = 0; i < al_void.capacity && i < data_size; i += 1) {
        al_void.data[i] = data[i];

        al_void.size += 1;
    }

    trace_log_pop();

    return al_void;
}

AL_Void* al_void_new_1(void) {
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    AL_Void *const al_void = (AL_Void*) allocator_borrow(sizeof(AL_Void), nullptr);
#else
    AL_Void *const al_void = (AL_Void*) allocator_borrow(sizeof(AL_Void));
#endif // ARENA_IMPLEMENTATION

    *al_void = al_void_init_1();

    trace_log_pop();

    return al_void;
}

AL_Void* al_void_new_2(USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    AL_Void *const al_void = al_void_new_1();

    *al_void = al_void_init_2(capacity);

    trace_log_pop();

    return al_void;
}

AL_Void* al_void_new_3(void *const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_non_value_uint(LOG_METADATA, "data_size", data_size);

    AL_Void *const al_void = al_void_new_1();

    *al_void = al_void_init_3(data, data_size);

    trace_log_pop();

    return al_void;
}

/*==============================================================================
 * MARK: - Removal and Capacity
 *============================================================================*/

void al_void_remove(AL_Void *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "self->size", self->size, "index >= self->size", index >= self->size);

    if (index < self->size - 1) {
        for (USize i = index; i < self->size - 1; i += 1) {
            self->data[i] = self->data[i + 1];
        }
    }

    /* Hygiene only: the decrement below puts this slot past the size, so the
     * alias the shift left here is never read and never released. */
#ifdef MEMORY_NON_DANGLING_POINTER
    self->data[self->size - 1] = nullptr;
#endif // MEMORY_NON_DANGLING_POINTER

    self->size -= 1;

    trace_log_pop();
}

void al_void_remove_first(AL_Void *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* An empty list has nothing to remove; remove() would abort on the bound. */
    if (self->size == 0) {
        trace_log_pop();

        return;
    }

    al_void_remove(self, 0);

    trace_log_pop();
}

void al_void_remove_last(AL_Void *const self) {
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

    al_void_remove(self, self->size - 1);

    trace_log_pop();
}

void al_void_reserve(AL_Void *const self, USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    if (capacity > self->capacity) {
        USize const capacity_before = self->capacity;
        void **const buffer = self->data;

        self->capacity = capacity;

        /* The byte size is a bare multiply: a large capacity wraps it, so the
         * borrow returns a tiny block while the stored capacity stays enormous
         * and every later write lands past it. Refuse instead. */
        if (self->capacity > USIZE_MAX / sizeof(void*)) {
            self->capacity = capacity_before;

            trace_log_pop();

            return;
        }

#ifdef ARENA_IMPLEMENTATION
        self->data = (void**) allocator_borrow(sizeof(void*) * self->capacity, self->allocator);
#else
        self->data = (void**) allocator_borrow(sizeof(void*) * self->capacity);
#endif // ARENA_IMPLEMENTATION

        /* Null only from a REFUSED arena. Put the old buffer back and refuse the
         * growth rather than writing through null on the very next line. */
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

void al_void_shrink(AL_Void *const self) {
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
        void **const buffer = self->data;

        self->capacity = self->size;

        /* The byte size is a bare multiply: a large capacity wraps it, so the
         * borrow returns a tiny block while the stored capacity stays enormous
         * and every later write lands past it. Refuse instead. */
        if (self->capacity > USIZE_MAX / sizeof(void*)) {
            self->capacity = capacity_before;

            trace_log_pop();

            return;
        }

#ifdef ARENA_IMPLEMENTATION
        self->data = (void**) allocator_borrow(sizeof(void*) * self->capacity, self->allocator);
#else
        self->data = (void**) allocator_borrow(sizeof(void*) * self->capacity);
#endif // ARENA_IMPLEMENTATION

        /* Null only from a REFUSED arena. Put the old buffer back and refuse the
         * growth rather than writing through null on the very next line. */
        if (memory_empty((void*) self->data)) {
            self->data = buffer;
            self->capacity = capacity_before;

            trace_log_pop();

            return;
        }

        for (USize i = 0; i < self->capacity; i += 1) {
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

void al_void_uninit(AL_Void *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    al_void_clear(self);

    if (!memory_empty((void*) self->data)) {
#ifdef ARENA_IMPLEMENTATION
        allocator_release((void*) self->data, self->allocator);
#else
        allocator_release((void*) self->data);
#endif // ARENA_IMPLEMENTATION
    }

    /* Unconditional, not gated on MEMORY_NON_DANGLING_POINTER: leaving the freed
     * pointer in place made a second uninit hand it back to the allocator, so
     * whether uninit was idempotent depended on a build flag. */
    self->data = nullptr;

    self->capacity = 0;

    trace_log_pop();
}