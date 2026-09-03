#include <container/arrayList/al_al_char.h>

#define _AL_AL_CHAR_GROWTH_FACTOR 2

/*==============================================================================
 * MARK: - Internal
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
static AL_AL_Char _al_al_char_init(Arena *allocator)
#else
static AL_AL_Char _al_al_char_init(void)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    AL_AL_Char al_al_char = DEFAULT_INITIALIZATION;

#ifdef ARENA_IMPLEMENTATION
    if (!memory_empty((void*) allocator)) {
        al_al_char.allocator = allocator;
    }
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return al_al_char;
}

/*==============================================================================
 * MARK: - Arena Constructors
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
AL_AL_Char al_al_char_alloc_init_1(Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    AL_AL_Char al_al_char = _al_al_char_init(allocator);

    trace_log_pop();

    return al_al_char;
}

AL_AL_Char al_al_char_alloc_init_2(USize const capacity, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    AL_AL_Char al_al_char = al_al_char_alloc_init_1(allocator);

    al_al_char.capacity = capacity;

    /* The byte size is a bare multiply: a large capacity wraps it, so the
     * borrow below would return a tiny block while the stored capacity stays
     * enormous. reserve refuses this already; the constructors set capacity
     * directly and need the same guard. */
    if (al_al_char.capacity > USIZE_MAX / sizeof(AL_Char)) {
        al_al_char.capacity = 0;

        trace_log_pop();

        return al_al_char;
    }

    al_al_char.data = (AL_Char*) allocator_borrow(sizeof(AL_Char) * al_al_char.capacity, allocator);

    /* A REFUSED arena hands back null here. Leaving capacity at n with a
     * null data defeats add()'s refused-reserve re-read - it compares size
     * against capacity, sees 0 != n, and writes through the null. Zeroing
     * capacity here is what makes that re-read, shrink, at() and the init_3
     * copy guard all degrade correctly. */
    if (memory_empty((void*) al_al_char.data)) {
        al_al_char.capacity = 0;
    }

    trace_log_pop();

    return al_al_char;
}

AL_AL_Char al_al_char_alloc_init_3(AL_Char *const data, USize const data_size, Arena *allocator) {
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
    AL_AL_Char al_al_char = al_al_char_alloc_init_2(data_size, allocator);

    /* Bounded by BOTH: the write is bounded by capacity, the read of data[i] by the
     * caller's data_size. They are independent quantities, and the source bound is
     * dead only because init_2 currently sets capacity to exactly data_size or 0 -
     * any future change that rounds capacity up (as bit-packed al_bool's word
     * granularity invites) would turn this into a read past the caller's array. */
    for (USize i = 0; i < al_al_char.capacity && i < data_size; i += 1) {
        al_al_char.data[i] = data[i];

        al_al_char.size += 1;
    }

    trace_log_pop();

    return al_al_char;
}

AL_AL_Char* al_al_char_alloc_new_1(Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    AL_AL_Char *const al_al_char = (AL_AL_Char*) allocator_borrow(sizeof(AL_AL_Char), allocator);

    /* The STRUCT borrow, not the element buffer: a refused arena returns null
     * here, and writing through it would put a whole struct through null. This
     * frame needs the same refused-arena guard the other constructors carry,
     * or the module header's blanket "a refused arena leaves the list
     * unchanged" promise is false for the whole alloc_new_* trio. */
    if (memory_empty((void*) al_al_char)) {
        trace_log_pop();

        return nullptr;
    }

    *al_al_char = al_al_char_alloc_init_1(allocator);

    trace_log_pop();

    return al_al_char;
}

AL_AL_Char* al_al_char_alloc_new_2(USize const capacity, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    AL_AL_Char *const al_al_char = al_al_char_alloc_new_1(allocator);

    /* alloc_new_1 now answers null on a refused arena. */
    if (memory_empty((void*) al_al_char)) {
        trace_log_pop();

        return nullptr;
    }

    *al_al_char = al_al_char_alloc_init_2(capacity, allocator);

    trace_log_pop();

    return al_al_char;
}

AL_AL_Char* al_al_char_alloc_new_3(AL_Char *const data, USize const data_size, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_non_value_uint(LOG_METADATA, "data_size", data_size);

    AL_AL_Char *const al_al_char = al_al_char_alloc_new_1(allocator);

    /* alloc_new_1 now answers null on a refused arena. */
    if (memory_empty((void*) al_al_char)) {
        trace_log_pop();

        return nullptr;
    }

    *al_al_char = al_al_char_alloc_init_3(data, data_size, allocator);

    trace_log_pop();

    return al_al_char;
}
#endif // ARENA_IMPLEMENTATION

/*==============================================================================
 * MARK: - Element Access
 *============================================================================*/

void al_al_char_add(AL_AL_Char *const self, AL_Char *const data, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "self->size", self->size, "index > self->size", index > self->size);

    /* A source that is one of self's OWN elements is refused as a no-op, in every build: the list
     * adopts what it stores, so the element would be owned twice (released twice at uninit),
     * and the growth below may release the buffer the source lives in before the shift moves
     * it - so it could never be adopted honestly. Tested BEFORE the growth, while self->data is
     * still the buffer the source points into. */
    if (!memory_empty((void*) data) && !memory_empty((void*) self->data) && data >= self->data && data < self->data + self->capacity) {
        trace_log_pop();

        return;
    }

    if (self->size == self->capacity) {
        if (self->capacity == 0) {
            al_al_char_reserve(self, _AL_AL_CHAR_GROWTH_FACTOR);
        }
        else {
            al_al_char_reserve(self, self->capacity > USIZE_MAX / _AL_AL_CHAR_GROWTH_FACTOR ? USIZE_MAX : self->capacity * _AL_AL_CHAR_GROWTH_FACTOR);
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

    self->data[index] = memory_empty((void*) data) ? al_char_init_1() : *data;

    self->size += 1;

    trace_log_pop();
}

void al_al_char_add_first(AL_AL_Char *const self, AL_Char *const data) {
    trace_log_push(LOG_METADATA);

    al_al_char_add(self, data, 0);

    trace_log_pop();
}

void al_al_char_add_last(AL_AL_Char *const self, AL_Char *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    al_al_char_add(self, data, self->size);

    trace_log_pop();
}

char** al_al_char_at_1(AL_AL_Char const *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "self->size", self->size, "index >= self->size", index >= self->size);

    char **const data = al_char_get_data(&self->data[index]);

    trace_log_pop();

    return data;
}

AL_Char* al_al_char_at_2(AL_AL_Char const *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "self->size", self->size, "index >= self->size", index >= self->size);

    trace_log_pop();

    return &self->data[index];
}

char** al_al_char_back_1(AL_AL_Char const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* Emptiness is a data condition, not a broken contract, so this answers
     * nullptr instead of going through error_check, which would abort. */
    if (self->size == 0) {
        trace_log_pop();

        return nullptr;
    }

    char **const data = al_char_get_data(&self->data[self->size - 1]);

    trace_log_pop();

    return data;
}

AL_Char* al_al_char_back_2(AL_AL_Char const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* Emptiness is a data condition, not a broken contract, so this answers
     * nullptr instead of going through error_check, which would abort. */
    if (self->size == 0) {
        trace_log_pop();

        return nullptr;
    }

    trace_log_pop();

    return &self->data[self->size - 1];
}

void al_al_char_clear(AL_AL_Char *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* Unguarded: al_char_clear and al_char_uninit are already no-ops on a zeroed
     * list, because allocator_release ignores a null buffer by contract. The
     * `!al_char_empty(...)` tests that stood here read empty() as "has an
     * allocation", which is what blocked the family from aligning empty() on
     * size - flipping the predicate under them would have skipped an
     * allocated-then-cleared nested list and leaked its backing array.
     *
     * uninit, not clear, on each element: al_char_clear frees the nested
     * elements but keeps the nested BACKING ARRAY, and this then set size to 0,
     * so the later al_al_char_uninit loop (bounded by size) could no longer
     * reach any of them. clear-then-uninit leaked one array per nested list. */
    for (USize i = 0; i < self->size; i += 1) {
        al_char_uninit(&self->data[i]);

        self->data[i] = DEFAULT_INITIALIZATION_TYPE(AL_Char);
    }

    self->size = 0;

    trace_log_pop();
}

void al_al_char_delete(AL_AL_Char **const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "*self", (void*) *self);

    al_al_char_uninit(*self);

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

bool al_al_char_empty(AL_AL_Char const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    /* size, not capacity: capacity answers "has this list ever allocated",
     * which is a different question and left the family split on what empty()
     * means. */
    return self->size == 0;
}

char** al_al_char_front_1(AL_AL_Char const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* Emptiness is a data condition, not a broken contract, so this answers
     * nullptr instead of going through error_check, which would abort. */
    if (self->size == 0) {
        trace_log_pop();

        return nullptr;
    }

    char **const data = al_char_get_data(&self->data[0]);

    trace_log_pop();

    return data;
}

AL_Char* al_al_char_front_2(AL_AL_Char const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* Emptiness is a data condition, not a broken contract, so this answers
     * nullptr instead of going through error_check, which would abort. */
    if (self->size == 0) {
        trace_log_pop();

        return nullptr;
    }

    trace_log_pop();

    return &self->data[0];
}

USize al_al_char_get_capacity(AL_AL_Char const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->capacity;
}

AL_Char* al_al_char_get_data(AL_AL_Char const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return (AL_Char*) self->data;
}

USize al_al_char_get_size(AL_AL_Char const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->size;
}

/*==============================================================================
 * MARK: - Heap Constructors
 *============================================================================*/

AL_AL_Char al_al_char_init_1(void) {
#ifdef ARENA_IMPLEMENTATION
    return _al_al_char_init(nullptr);
#else
    return _al_al_char_init();
#endif // ARENA_IMPLEMENTATION
}

AL_AL_Char al_al_char_init_2(USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    AL_AL_Char al_al_char = al_al_char_init_1();

    al_al_char.capacity = capacity;

    /* The byte size is a bare multiply: a large capacity wraps it, so the
     * borrow below would return a tiny block while the stored capacity stays
     * enormous. reserve refuses this already; the constructors set capacity
     * directly and need the same guard. */
    if (al_al_char.capacity > USIZE_MAX / sizeof(AL_Char)) {
        al_al_char.capacity = 0;

        trace_log_pop();

        return al_al_char;
    }

#ifdef ARENA_IMPLEMENTATION
    al_al_char.data = (AL_Char*) allocator_borrow(sizeof(AL_Char) * al_al_char.capacity, nullptr);
#else
    al_al_char.data = (AL_Char*) allocator_borrow(sizeof(AL_Char) * al_al_char.capacity);
#endif // ARENA_IMPLEMENTATION

    /* A REFUSED arena hands back null here. Leaving capacity at n with a
     * null data defeats add()'s refused-reserve re-read - it compares size
     * against capacity, sees 0 != n, and writes through the null. Zeroing
     * capacity here is what makes that re-read, shrink, at() and the init_3
     * copy guard all degrade correctly. */
    if (memory_empty((void*) al_al_char.data)) {
        al_al_char.capacity = 0;
    }

    trace_log_pop();

    return al_al_char;
}

AL_AL_Char al_al_char_init_3(AL_Char *const data, USize const data_size) {
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
    AL_AL_Char al_al_char = al_al_char_init_2(data_size);

    /* Bounded by BOTH: the write is bounded by capacity, the read of data[i] by the
     * caller's data_size. They are independent quantities, and the source bound is
     * dead only because init_2 currently sets capacity to exactly data_size or 0 -
     * any future change that rounds capacity up (as bit-packed al_bool's word
     * granularity invites) would turn this into a read past the caller's array. */
    for (USize i = 0; i < al_al_char.capacity && i < data_size; i += 1) {
        al_al_char.data[i] = data[i];

        al_al_char.size += 1;
    }

    trace_log_pop();

    return al_al_char;
}

AL_AL_Char* al_al_char_new_1(void) {
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    AL_AL_Char *const al_al_char = (AL_AL_Char*) allocator_borrow(sizeof(AL_AL_Char), nullptr);
#else
    AL_AL_Char *const al_al_char = (AL_AL_Char*) allocator_borrow(sizeof(AL_AL_Char));
#endif // ARENA_IMPLEMENTATION

    *al_al_char = al_al_char_init_1();

    trace_log_pop();

    return al_al_char;
}

AL_AL_Char* al_al_char_new_2(USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    AL_AL_Char *const al_al_char = al_al_char_new_1();

    *al_al_char = al_al_char_init_2(capacity);

    trace_log_pop();

    return al_al_char;
}

AL_AL_Char* al_al_char_new_3(AL_Char *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_non_value_uint(LOG_METADATA, "data_size", data_size);

    AL_AL_Char *const al_al_char = al_al_char_new_1();

    *al_al_char = al_al_char_init_3(data, data_size);

    trace_log_pop();

    return al_al_char;
}

/*==============================================================================
 * MARK: - Removal and Capacity
 *============================================================================*/

void al_al_char_remove(AL_AL_Char *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "self->size", self->size, "index >= self->size", index >= self->size);

    // Release the element at index before it is overwritten by the shift below;
    // uninit'ing the tail slot afterward would free a buffer aliased with the surviving element.
    al_char_uninit(&self->data[index]);

    if (index < self->size - 1) {
        for (USize i = index; i < self->size - 1; i += 1) {
            self->data[i] = self->data[i + 1];
        }
    }

    self->data[self->size - 1] = al_char_init_1();

    self->size -= 1;

    trace_log_pop();
}

void al_al_char_remove_first(AL_AL_Char *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* An empty list has nothing to remove; remove() would abort on the bound. */
    if (self->size == 0) {
        trace_log_pop();

        return;
    }

    al_al_char_remove(self, 0);

    trace_log_pop();
}

void al_al_char_remove_last(AL_AL_Char *const self) {
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

    al_al_char_remove(self, self->size - 1);

    trace_log_pop();
}

void al_al_char_reserve(AL_AL_Char *const self, USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    if (capacity > self->capacity) {
        USize const capacity_before = self->capacity;
        AL_Char *const buffer = self->data;

        self->capacity = capacity;

        /* The byte size is a bare multiply: a large capacity wraps it, so the
         * borrow returns a tiny block while the stored capacity stays enormous
         * and every later write lands past it. Refuse instead. */
        if (self->capacity > USIZE_MAX / sizeof(AL_Char)) {
            self->capacity = capacity_before;

            trace_log_pop();

            return;
        }

#ifdef ARENA_IMPLEMENTATION
        self->data = (AL_Char*) allocator_borrow(sizeof(AL_Char) * self->capacity, self->allocator);
#else
        self->data = (AL_Char*) allocator_borrow(sizeof(AL_Char) * self->capacity);
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

void al_al_char_shrink(AL_AL_Char *const self) {
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
        AL_Char *const buffer = self->data;

        self->capacity = self->size;

        /* The byte size is a bare multiply: a large capacity wraps it, so the
         * borrow returns a tiny block while the stored capacity stays enormous
         * and every later write lands past it. Refuse instead. */
        if (self->capacity > USIZE_MAX / sizeof(AL_Char)) {
            self->capacity = capacity_before;

            trace_log_pop();

            return;
        }

#ifdef ARENA_IMPLEMENTATION
        self->data = (AL_Char*) allocator_borrow(sizeof(AL_Char) * self->capacity, self->allocator);
#else
        self->data = (AL_Char*) allocator_borrow(sizeof(AL_Char) * self->capacity);
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

void al_al_char_uninit(AL_AL_Char *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    for (USize i = 0; i < self->size; i += 1) {
        al_char_uninit(&self->data[i]);
    }

    self->size = 0;

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