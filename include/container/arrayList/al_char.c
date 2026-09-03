#include <container/arrayList/al_char.h>

#define _AL_CHAR_GROWTH_FACTOR 2

/*==============================================================================
 * MARK: - Internal
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
static AL_Char _al_char_init(Arena *allocator)
#else
static AL_Char _al_char_init(void)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    AL_Char al_char = DEFAULT_INITIALIZATION;

#ifdef ARENA_IMPLEMENTATION
    if (!memory_empty((void*) allocator)) {
        al_char.allocator = allocator;
    }
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return al_char;
}

/*==============================================================================
 * MARK: - Arena Constructors
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
AL_Char al_char_alloc_init_1(Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    AL_Char al_char = _al_char_init(allocator);

    trace_log_pop();

    return al_char;
}

AL_Char al_char_alloc_init_2(USize const capacity, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    AL_Char al_char = al_char_alloc_init_1(allocator);

    al_char.capacity = capacity;

    /* The byte size is a bare multiply: a large capacity wraps it, so the
     * borrow below would return a tiny block while the stored capacity stays
     * enormous. reserve refuses this already; the constructors set capacity
     * directly and need the same guard. */
    if (al_char.capacity > USIZE_MAX / sizeof(char*)) {
        al_char.capacity = 0;

        trace_log_pop();

        return al_char;
    }

    al_char.data = (char**) allocator_borrow(sizeof(char*) * al_char.capacity, allocator);

    /* A REFUSED arena hands back null here. Leaving capacity at n with a
     * null data defeats add()'s refused-reserve re-read - it compares size
     * against capacity, sees 0 != n, and writes through the null. Zeroing
     * capacity here is what makes that re-read, shrink, at() and the init_3
     * copy guard all degrade correctly. */
    if (memory_empty((void*) al_char.data)) {
        al_char.capacity = 0;
    }

    trace_log_pop();

    return al_char;
}

AL_Char al_char_alloc_init_3(char **const data, USize const data_size, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_non_value_uint(LOG_METADATA, "data_size", data_size);

    /* COPY the backing array rather than adopting the caller's: adoption meant
     * uninit released memory the list never borrowed - a free of a stack array
     * or of a block from a different allocator, decided by whoever called this.
     *
     * The ELEMENTS are still taken by value, exactly as add() takes them, so
     * ownership of what they point to transfers here; only the array itself is
     * now the list's own. */
    AL_Char al_char = al_char_alloc_init_2(data_size, allocator);

    /* Bounded by BOTH: the write is bounded by capacity, the read of data[i] by the
     * caller's data_size. They are independent quantities, and the source bound is
     * dead only because init_2 currently sets capacity to exactly data_size or 0 -
     * any future change that rounds capacity up (as bit-packed al_bool's word
     * granularity invites) would turn this into a read past the caller's array. */
    for (USize i = 0; i < al_char.capacity && i < data_size; i += 1) {
        al_char.data[i] = data[i];

        al_char.size += 1;
    }

    trace_log_pop();

    return al_char;
}

AL_Char* al_char_alloc_new_1(Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    AL_Char *const al_char = (AL_Char*) allocator_borrow(sizeof(AL_Char), allocator);

    /* The STRUCT borrow, not the element buffer: a refused arena returns null
     * here, and writing through it would put a whole struct through null. This
     * frame needs the same refused-arena guard the other constructors carry,
     * or the module header's blanket "a refused arena leaves the list
     * unchanged" promise is false for the whole alloc_new_* trio. */
    if (memory_empty((void*) al_char)) {
        trace_log_pop();

        return nullptr;
    }

    *al_char = al_char_alloc_init_1(allocator);

    trace_log_pop();

    return al_char;
}

AL_Char* al_char_alloc_new_2(USize const capacity, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    AL_Char *const al_char = al_char_alloc_new_1(allocator);

    /* alloc_new_1 now answers null on a refused arena. */
    if (memory_empty((void*) al_char)) {
        trace_log_pop();

        return nullptr;
    }

    *al_char = al_char_alloc_init_2(capacity, allocator);

    trace_log_pop();

    return al_char;
}

AL_Char* al_char_alloc_new_3(char **const data, USize const data_size, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_non_value_uint(LOG_METADATA, "data_size", data_size);

    AL_Char *const al_char = al_char_alloc_new_1(allocator);

    /* alloc_new_1 now answers null on a refused arena. */
    if (memory_empty((void*) al_char)) {
        trace_log_pop();

        return nullptr;
    }

    *al_char = al_char_alloc_init_3(data, data_size, allocator);

    trace_log_pop();

    return al_char;
}
#endif // ARENA_IMPLEMENTATION

/*==============================================================================
 * MARK: - Element Access
 *============================================================================*/

void al_char_add(AL_Char *const self, char *const data, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "self->size", self->size, "index > self->size", index > self->size);

    if (self->size == self->capacity) {
        if (self->capacity == 0) {
            al_char_reserve(self, _AL_CHAR_GROWTH_FACTOR);
        }
        else {
            al_char_reserve(self, self->capacity > USIZE_MAX / _AL_CHAR_GROWTH_FACTOR ? USIZE_MAX : self->capacity * _AL_CHAR_GROWTH_FACTOR);
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

    /* Shift [index, size) right by one, then place; when index == size the loop
     * is a no-op and this is a plain append. This replaces a special case that
     * appended at `size` whenever index was size - 1 (so inserting before the
     * last element silently appended) and a reverse loop guarded by `i > 0`,
     * which could never place at index 0 - it shifted everything right, dropped
     * the element, grew size, and left slot 0 holding a stale duplicate. The
     * al-family sweep recorded this fix as family-wide; it reached the numeric
     * instantiations only. */
    for (USize i = self->size; i > index; i -= 1) {
        self->data[i] = self->data[i - 1];
    }

    self->data[index] = memory_empty((void*) data) ? nullptr : data;

    self->size += 1;

    trace_log_pop();
}

void al_char_add_first(AL_Char *const self, char *const data) {
    trace_log_push(LOG_METADATA);

    al_char_add(self, data, 0);

    trace_log_pop();
}

void al_char_add_last(AL_Char *const self, char *const data) {
    trace_log_push(LOG_METADATA);

    /* self->size is read in the ARGUMENT below, so a null self faults here
     * before add's own error_check_null can report it. */
    error_check_null(LOG_METADATA, "self", (void*) self);

    /* Forwards `size`, not `size - 1`: the old index was only ever correct
     * against the broken `add`, whose special case rewrote it into an append.
     * Against a real insert it meant "insert before the last element". */
    al_char_add(self, data, self->size);

    trace_log_pop();
}

char* al_char_at(AL_Char const *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "self->size", self->size, "index >= self->size", index >= self->size);

    trace_log_pop();

    return self->data[index];
}

char* al_char_back(AL_Char const *const self) {
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

    return self->data[self->size - 1];
}

void al_char_clear(AL_Char *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    for (USize i = 0; i < self->size; i += 1) {
        if (!memory_empty((void*) self->data[i])) {
#ifdef ARENA_IMPLEMENTATION
            allocator_release((void*) self->data[i], self->allocator);
#else
            allocator_release((void*) self->data[i]);
#endif // ARENA_IMPLEMENTATION

            /* Hygiene only: size drops to 0 below, so nothing reads this slot
             * again and no release can reach it twice. */
#ifdef MEMORY_NON_DANGLING_POINTER
            self->data[i] = nullptr;
#endif // MEMORY_NON_DANGLING_POINTER
        }
    }

    self->size = 0;

    trace_log_pop();
}

void al_char_delete(AL_Char **const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "*self", (void*) *self);

    al_char_uninit(*self);

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

bool al_char_empty(AL_Char const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    /* size, not capacity: capacity answers "has this list ever allocated",
     * which is a different question and left the family split on what empty()
     * means. */
    return self->size == 0;
}

char* al_char_front(AL_Char const *const self) {
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

    return self->data[0];
}

USize al_char_get_capacity(AL_Char const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->capacity;
}

char** al_char_get_data(AL_Char const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->data;
}

USize al_char_get_size(AL_Char const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->size;
}

/*==============================================================================
 * MARK: - Heap Constructors
 *============================================================================*/

AL_Char al_char_init_1(void) {
#ifdef ARENA_IMPLEMENTATION
    return _al_char_init(nullptr);
#else
    return _al_char_init();
#endif // ARENA_IMPLEMENTATION
}

AL_Char al_char_init_2(USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    AL_Char al_char = al_char_init_1();

    al_char.capacity = capacity;

    /* The byte size is a bare multiply: a large capacity wraps it, so the
     * borrow below would return a tiny block while the stored capacity stays
     * enormous. reserve refuses this already; the constructors set capacity
     * directly and need the same guard. */
    if (al_char.capacity > USIZE_MAX / sizeof(char*)) {
        al_char.capacity = 0;

        trace_log_pop();

        return al_char;
    }

#ifdef ARENA_IMPLEMENTATION
    al_char.data = (char**) allocator_borrow(sizeof(char*) * al_char.capacity, nullptr);
#else
    al_char.data = (char**) allocator_borrow(sizeof(char*) * al_char.capacity);
#endif // ARENA_IMPLEMENTATION

    /* A REFUSED arena hands back null here. Leaving capacity at n with a
     * null data defeats add()'s refused-reserve re-read - it compares size
     * against capacity, sees 0 != n, and writes through the null. Zeroing
     * capacity here is what makes that re-read, shrink, at() and the init_3
     * copy guard all degrade correctly. */
    if (memory_empty((void*) al_char.data)) {
        al_char.capacity = 0;
    }

    trace_log_pop();

    return al_char;
}

AL_Char al_char_init_3(char **const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_non_value_uint(LOG_METADATA, "data_size", data_size);

    /* COPY the backing array rather than adopting the caller's: adoption meant
     * uninit released memory the list never borrowed - a free of a stack array
     * or of a block from a different allocator, decided by whoever called this.
     *
     * The ELEMENTS are still taken by value, exactly as add() takes them, so
     * ownership of what they point to transfers here; only the array itself is
     * now the list's own. */
    AL_Char al_char = al_char_init_2(data_size);

    /* Bounded by BOTH: the write is bounded by capacity, the read of data[i] by the
     * caller's data_size. They are independent quantities, and the source bound is
     * dead only because init_2 currently sets capacity to exactly data_size or 0 -
     * any future change that rounds capacity up (as bit-packed al_bool's word
     * granularity invites) would turn this into a read past the caller's array. */
    for (USize i = 0; i < al_char.capacity && i < data_size; i += 1) {
        al_char.data[i] = data[i];

        al_char.size += 1;
    }

    trace_log_pop();

    return al_char;
}

AL_Char* al_char_new_1(void) {
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    AL_Char *const al_char = (AL_Char*) allocator_borrow(sizeof(AL_Char), nullptr);
#else
    AL_Char *const al_char = (AL_Char*) allocator_borrow(sizeof(AL_Char));
#endif // ARENA_IMPLEMENTATION

    *al_char = al_char_init_1();

    trace_log_pop();

    return al_char;
}

AL_Char* al_char_new_2(USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    AL_Char *const al_char = al_char_new_1();

    *al_char = al_char_init_2(capacity);

    trace_log_pop();

    return al_char;
}

AL_Char* al_char_new_3(char **const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_non_value_uint(LOG_METADATA, "data_size", data_size);

    AL_Char *const al_char = al_char_new_1();

    *al_char = al_char_init_3(data, data_size);

    trace_log_pop();

    return al_char;
}

/*==============================================================================
 * MARK: - Removal and Capacity
 *============================================================================*/

void al_char_remove(AL_Char *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "self->size", self->size, "index >= self->size", index >= self->size);

    /* A stored nullptr is a VALUE here - al_char_add explicitly accepts one and
     * stores it - so aborting on it turned a legal element into a process kill.
     * And the release goes through the list's own allocator, matching
     * al_char_clear: char_delete always frees to the heap, so removing from an
     * arena-backed list handed an arena pointer to free(). */
    if (!memory_empty((void*) self->data[index])) {
#ifdef ARENA_IMPLEMENTATION
        allocator_release((void*) self->data[index], self->allocator);
#else
        allocator_release((void*) self->data[index]);
#endif // ARENA_IMPLEMENTATION
    }

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

void al_char_remove_first(AL_Char *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* An empty list has nothing to remove; remove() would abort on the bound. */
    if (self->size == 0) {
        trace_log_pop();

        return;
    }

    al_char_remove(self, 0);

    trace_log_pop();
}

void al_char_remove_last(AL_Char *const self) {
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

    al_char_remove(self, self->size - 1);

    trace_log_pop();
}

void al_char_reserve(AL_Char *const self, USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    if (capacity > self->capacity) {
        USize const capacity_before = self->capacity;
        char **const buffer = self->data;

        self->capacity = capacity;

        /* The byte size is a bare multiply: a large capacity wraps it, so the
         * borrow returns a tiny block while the stored capacity stays enormous
         * and every later write lands past it. Refuse instead. */
        if (self->capacity > USIZE_MAX / sizeof(char*)) {
            self->capacity = capacity_before;

            trace_log_pop();

            return;
        }

#ifdef ARENA_IMPLEMENTATION
        self->data = (char**) allocator_borrow(sizeof(char*) * self->capacity, self->allocator);
#else
        self->data = (char**) allocator_borrow(sizeof(char*) * self->capacity);
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

void al_char_shrink(AL_Char *const self) {
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
        char **const buffer = self->data;

        self->capacity = self->size;

        /* The byte size is a bare multiply: a large capacity wraps it, so the
         * borrow returns a tiny block while the stored capacity stays enormous
         * and every later write lands past it. Refuse instead. */
        if (self->capacity > USIZE_MAX / sizeof(char*)) {
            self->capacity = capacity_before;

            trace_log_pop();

            return;
        }

#ifdef ARENA_IMPLEMENTATION
        self->data = (char**) allocator_borrow(sizeof(char*) * self->capacity, self->allocator);
#else
        self->data = (char**) allocator_borrow(sizeof(char*) * self->capacity);
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

#ifdef ARENA_IMPLEMENTATION
        allocator_release((void*) buffer, self->allocator);
#else
        allocator_release((void*) buffer);
#endif // ARENA_IMPLEMENTATION
    }

    trace_log_pop();
}

void al_char_uninit(AL_Char *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    al_char_clear(self);

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