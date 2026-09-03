#include <container/arrayList/al_str.h>

#define _AL_STR_GROWTH_FACTOR 2

/*==============================================================================
 * MARK: - Internal
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
static AL_Str _al_str_init(Arena *allocator)
#else
static AL_Str _al_str_init(void)
#endif // ARENA_IMPLEMENTATION
{
    AL_Str al_str = DEFAULT_INITIALIZATION;

#ifdef ARENA_IMPLEMENTATION
    if (!memory_empty((void*) allocator)) {
        al_str.allocator = allocator;
    }
#endif // ARENA_IMPLEMENTATION

    return al_str;
}

/*==============================================================================
 * MARK: - Arena Constructors
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
AL_Str al_str_alloc_init_1(Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    AL_Str al_str = _al_str_init(allocator);

    trace_log_pop();

    return al_str;
}

AL_Str al_str_alloc_init_2(USize const capacity, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    AL_Str al_str = al_str_alloc_init_1(allocator);

    al_str.capacity = capacity;

    /* The byte size is a bare multiply: a large capacity wraps it, so the
     * borrow below would return a tiny block while the stored capacity stays
     * enormous. reserve refuses this already; the constructors set capacity
     * directly and need the same guard. */
    if (al_str.capacity > USIZE_MAX / sizeof(Str)) {
        al_str.capacity = 0;

        trace_log_pop();

        return al_str;
    }

    al_str.data = (Str*) allocator_borrow(sizeof(Str) * al_str.capacity, allocator);

    /* A REFUSED arena hands back null here. Leaving capacity at n with a
     * null data defeats add()'s refused-reserve re-read - it compares size
     * against capacity, sees 0 != n, and writes through the null. Zeroing
     * capacity here is what makes that re-read, shrink, at() and the init_3
     * copy guard all degrade correctly. */
    if (memory_empty((void*) al_str.data)) {
        al_str.capacity = 0;
    }

    trace_log_pop();

    return al_str;
}

AL_Str al_str_alloc_init_3(Str *const data, USize const data_size, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_non_value_uint(LOG_METADATA, "data_size", data_size);

    AL_Str al_str = al_str_alloc_init_2(data_size, allocator);

    /* Bounded by BOTH: the write is bounded by capacity, the read of data[i] by the
     * caller's data_size. They are independent quantities, and the source bound is
     * dead only because init_2 currently sets capacity to exactly data_size or 0 -
     * any future change that rounds capacity up (as bit-packed al_bool's word
     * granularity invites) would turn this into a read past the caller's array. */
    for (USize i = 0; i < al_str.capacity && i < data_size; i += 1) {
        al_str.data[i] = data[i];

        al_str.size += 1;
    }

    trace_log_pop();

    return al_str;
}

AL_Str* al_str_alloc_new_1(Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    AL_Str *const al_str = (AL_Str*) allocator_borrow(sizeof(AL_Str), allocator);

    /* The STRUCT borrow, not the element buffer: a refused arena returns null
     * here, and writing through it would put a whole struct through null. This
     * frame needs the same refused-arena guard the other constructors carry,
     * or the module header's blanket "a refused arena leaves the list
     * unchanged" promise is false for the whole alloc_new_* trio. */
    if (memory_empty((void*) al_str)) {
        trace_log_pop();

        return nullptr;
    }

    *al_str = al_str_alloc_init_1(allocator);

    trace_log_pop();

    return al_str;
}

AL_Str* al_str_alloc_new_2(USize const capacity, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    AL_Str *const al_str = al_str_alloc_new_1(allocator);

    /* alloc_new_1 now answers null on a refused arena. */
    if (memory_empty((void*) al_str)) {
        trace_log_pop();

        return nullptr;
    }

    /* init_2, not _1: the capacity was accepted, error-checked, and then
     * silently discarded by calling the capacity-less constructor - a bug
     * fixed here after being caught first in al_multipart_alloc_new_2. */
    *al_str = al_str_alloc_init_2(capacity, allocator);

    trace_log_pop();

    return al_str;
}

AL_Str* al_str_alloc_new_3(Str *const data, USize const data_size, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_non_value_uint(LOG_METADATA, "data_size", data_size);

    AL_Str *const al_str = al_str_alloc_new_1(allocator);

    /* alloc_new_1 now answers null on a refused arena. */
    if (memory_empty((void*) al_str)) {
        trace_log_pop();

        return nullptr;
    }

    *al_str = al_str_alloc_init_3(data, data_size, allocator);

    trace_log_pop();

    return al_str;
}
#endif // ARENA_IMPLEMENTATION

/*==============================================================================
 * MARK: - Element Access
 *============================================================================*/

void al_str_add(AL_Str *const self, Str *const data, USize const index) {
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
            al_str_reserve(self, _AL_STR_GROWTH_FACTOR);
        }
        else {
            al_str_reserve(self, self->capacity > USIZE_MAX / _AL_STR_GROWTH_FACTOR ? USIZE_MAX : self->capacity * _AL_STR_GROWTH_FACTOR);
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
     * the element, grew size, and left slot 0 holding a stale duplicate. For an
     * element-owning list that duplicate is an ALIAS, so uninit then released
     * the same buffer twice (observed as heap corruption). */
    for (USize i = self->size; i > index; i -= 1) {
        self->data[i] = self->data[i - 1];
    }

    self->data[index] = memory_empty((void*) data) ? str_init_1() : *data;

    self->size += 1;

    trace_log_pop();
}

void al_str_add_first(AL_Str *const self, Str *const data) {
    trace_log_push(LOG_METADATA);

    al_str_add(self, data, 0);

    trace_log_pop();
}

void al_str_add_last(AL_Str *const self, Str *const data) {
    trace_log_push(LOG_METADATA);

    /* self->size is read in the ARGUMENT below, so a null self faults here
     * before add's own error_check_null can report it. */
    error_check_null(LOG_METADATA, "self", (void*) self);

    /* Forwards `size`, not `size - 1`: the old index was only ever correct
     * against the broken `add`, whose special case rewrote it into an append.
     * Against a real insert it meant "insert before the last element". */
    al_str_add(self, data, self->size);

    trace_log_pop();
}

Str* al_str_at(AL_Str const *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "self->size", self->size, "index >= self->size", index >= self->size);

    trace_log_pop();

    return (Str*) &(self->data[index]);
}

Str* al_str_back(AL_Str const *const self) {
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

    return (Str*) &(self->data[self->size - 1]);
}

void al_str_clear(AL_Str *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    for (USize i = 0; i < self->size; i += 1) {
        str_uninit(&self->data[i]);
#ifdef ARENA_IMPLEMENTATION
        if (!memory_empty((void*) self->data[i].allocator)) {
            self->data[i] = str_alloc_init_1(self->data[i].allocator);
        }
        else {
            self->data[i] = str_init_1();
        }
#else
        self->data[i] = str_init_1();
#endif // ARENA_IMPLEMENTATION
    }

    self->size = 0;

    trace_log_pop();
}

void al_str_delete(AL_Str **const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "*self", (void*) *self);

    al_str_uninit(*self);

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

bool al_str_empty(AL_Str const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    /* Keyed on size, matching al_void/al_u64/al_bool/al_f64. It used to report
     * capacity == 0, i.e. "has never allocated", so a list whose elements had all been
     * removed still claimed to be non-empty. (al_char keeps the capacity reading on
     * purpose - al_al_char calls it as a has-something-to-release guard.) */
    return self->size == 0;
}

Str* al_str_front(AL_Str const *const self) {
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

    return (Str*) &(self->data[0]);
}

USize al_str_get_capacity(AL_Str const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->capacity;
}

Str* al_str_get_data(AL_Str const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->data;
}

USize al_str_get_size(AL_Str const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->size;
}

/*==============================================================================
 * MARK: - Heap Constructors
 *============================================================================*/

AL_Str al_str_init_1(void) {
#ifdef ARENA_IMPLEMENTATION
    return _al_str_init(nullptr);
#else
    return _al_str_init();
#endif // ARENA_IMPLEMENTATION
}

AL_Str al_str_init_2(USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    AL_Str al_str = al_str_init_1();

    al_str.capacity = capacity;

    /* The byte size is a bare multiply: a large capacity wraps it, so the
     * borrow below would return a tiny block while the stored capacity stays
     * enormous. reserve refuses this already; the constructors set capacity
     * directly and need the same guard. */
    if (al_str.capacity > USIZE_MAX / sizeof(Str)) {
        al_str.capacity = 0;

        trace_log_pop();

        return al_str;
    }

#ifdef ARENA_IMPLEMENTATION
    al_str.data = (Str*) allocator_borrow(sizeof(Str) * al_str.capacity, nullptr);
#else
    al_str.data = (Str*) allocator_borrow(sizeof(Str) * al_str.capacity);
#endif // ARENA_IMPLEMENTATION

    /* A REFUSED arena hands back null here. Leaving capacity at n with a
     * null data defeats add()'s refused-reserve re-read - it compares size
     * against capacity, sees 0 != n, and writes through the null. Zeroing
     * capacity here is what makes that re-read, shrink, at() and the init_3
     * copy guard all degrade correctly. */
    if (memory_empty((void*) al_str.data)) {
        al_str.capacity = 0;
    }

    trace_log_pop();

    return al_str;
}

AL_Str al_str_init_3(Str *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_non_value_uint(LOG_METADATA, "data_size", data_size);

    AL_Str al_str = al_str_init_2(data_size);

    /* Bounded by BOTH: the write is bounded by capacity, the read of data[i] by the
     * caller's data_size. They are independent quantities, and the source bound is
     * dead only because init_2 currently sets capacity to exactly data_size or 0 -
     * any future change that rounds capacity up (as bit-packed al_bool's word
     * granularity invites) would turn this into a read past the caller's array. */
    for (USize i = 0; i < al_str.capacity && i < data_size; i += 1) {
        al_str.data[i] = data[i];

        al_str.size += 1;
    }

    trace_log_pop();

    return al_str;
}

AL_Str* al_str_new_1(void) {
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    AL_Str *const al_str = (AL_Str*) allocator_borrow(sizeof(AL_Str), nullptr);
#else
    AL_Str *const al_str = (AL_Str*) allocator_borrow(sizeof(AL_Str));
#endif // ARENA_IMPLEMENTATION

    *al_str = al_str_init_1();

    trace_log_pop();

    return al_str;
}

AL_Str* al_str_new_2(USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    AL_Str *const al_str = al_str_new_1();

    /* init_2, not _1: the capacity was accepted, error-checked, and then
     * silently discarded by calling the capacity-less constructor - a bug
     * fixed here after being caught first in al_multipart_alloc_new_2. */
    *al_str = al_str_init_2(capacity);

    trace_log_pop();

    return al_str;
}

AL_Str* al_str_new_3(Str *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_non_value_uint(LOG_METADATA, "data_size", data_size);

    AL_Str *const al_str = al_str_new_1();

    *al_str = al_str_init_3(data, data_size);

    trace_log_pop();

    return al_str;
}

/*==============================================================================
 * MARK: - Removal and Capacity
 *============================================================================*/

void al_str_remove(AL_Str *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "self->size", self->size, "index >= self->size", index >= self->size);

    /* Released BEFORE the shift. Shifting first overwrites the removed element (leaking
     * its buffer) and leaves data[size - 1] a duplicate of the element now at
     * size - 2, so uninit-ing the tail freed a buffer a live element still points at -
     * a dangling reference and, once that element is released too, a double free. */
    str_uninit(&self->data[index]);

    if (index < self->size - 1) {
        for (USize i = index; i < self->size - 1; i += 1) {
            self->data[i] = self->data[i + 1];
        }
    }

    self->data[self->size - 1] = str_init_1();

    self->size -= 1;

    trace_log_pop();
}

void al_str_remove_first(AL_Str *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* An empty list has nothing to remove; remove() would abort on the bound. */
    if (self->size == 0) {
        trace_log_pop();

        return;
    }

    al_str_remove(self, 0);

    trace_log_pop();
}

void al_str_remove_last(AL_Str *const self) {
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

    al_str_remove(self, self->size - 1);

    trace_log_pop();
}

void al_str_reserve(AL_Str *const self, USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    if (capacity > self->capacity) {
        USize const capacity_before = self->capacity;
        Str *const buffer = self->data;

        self->capacity = capacity;

        /* The byte size is a bare multiply: a large capacity wraps it, so the
         * borrow returns a tiny block while the stored capacity stays enormous
         * and every later write lands past it. Refuse instead. */
        if (self->capacity > USIZE_MAX / sizeof(Str)) {
            self->capacity = capacity_before;

            trace_log_pop();

            return;
        }

#ifdef ARENA_IMPLEMENTATION
        self->data = (Str*) allocator_borrow(sizeof(Str) * self->capacity, self->allocator);
#else
        self->data = (Str*) allocator_borrow(sizeof(Str) * self->capacity);
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

void al_str_shrink(AL_Str *const self) {
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
        Str *const buffer = self->data;

        self->capacity = self->size;

        /* The byte size is a bare multiply: a large capacity wraps it, so the
         * borrow returns a tiny block while the stored capacity stays enormous
         * and every later write lands past it. Refuse instead. */
        if (self->capacity > USIZE_MAX / sizeof(Str)) {
            self->capacity = capacity_before;

            trace_log_pop();

            return;
        }

#ifdef ARENA_IMPLEMENTATION
        self->data = (Str*) allocator_borrow(sizeof(Str) * self->capacity, self->allocator);
#else
        self->data = (Str*) allocator_borrow(sizeof(Str) * self->capacity);
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

void al_str_uninit(AL_Str *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    for (USize i = 0; i < self->size; i += 1) {
        str_uninit(&self->data[i]);
    }

    if (memory_empty((void*) self->data)) {
        self->capacity = 0;
        self->size = 0;

        trace_log_pop();

        return;
    }

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
    self->size = 0;

    trace_log_pop();
}