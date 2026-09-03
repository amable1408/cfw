#include <container/arrayList/al_bool.h>

#define _AL_BOOL_BITS (sizeof(USize) * BITS_IN_BYTE)

#define _AL_BOOL_GROWTH_FACTOR 2

/*==============================================================================
 * MARK: - Internal
 *============================================================================*/

static bool _al_bool_get(AL_Bool const *const self, USize const index) {
    USize const word = self->capacity <= _AL_BOOL_BITS ? self->data.raw : self->data.dyn[index / _AL_BOOL_BITS];

    return bits_at(word, index % _AL_BOOL_BITS);
}

static void _al_bool_set(AL_Bool *const self, USize const index, bool const data) {
    USize *const word = self->capacity <= _AL_BOOL_BITS ? &self->data.raw : &self->data.dyn[index / _AL_BOOL_BITS];

    if (bits_at(*word, index % _AL_BOOL_BITS) != data) {
        bits_flip(word, index % _AL_BOOL_BITS);
    }
}

static USize _al_bool_words(USize const capacity) {
    /* Integer ceiling. The old form divided through FSize, which cannot hold a
     * USize exactly past 2^53: the rounded quotient could come back one word
     * short, and every _al_bool_set beyond that point wrote outside the block.
     * Written as quotient-plus-remainder rather than (capacity + BITS - 1) so
     * the addition cannot wrap near USIZE_MAX. */
    return capacity / _AL_BOOL_BITS + (capacity % _AL_BOOL_BITS != 0 ? 1 : 0);
}
#ifdef ARENA_IMPLEMENTATION
static AL_Bool _al_bool_init(Arena *allocator)
#else
static AL_Bool _al_bool_init(void)
#endif // ARENA_IMPLEMENTATION
{
    AL_Bool al_bool = DEFAULT_INITIALIZATION;

#ifdef ARENA_IMPLEMENTATION
    if (!memory_empty((void*) allocator)) {
        al_bool.allocator = allocator;
    }
#endif // ARENA_IMPLEMENTATION

    return al_bool;
}

/*==============================================================================
 * MARK: - Arena Constructors
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
AL_Bool al_bool_alloc_init_1(Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    AL_Bool al_bool = _al_bool_init(allocator);

    trace_log_pop();

    return al_bool;
}

AL_Bool al_bool_alloc_init_2(USize const capacity, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    AL_Bool al_bool = al_bool_alloc_init_1(allocator);

    al_bool.capacity = capacity;

    if (al_bool.capacity > _AL_BOOL_BITS) {
        al_bool.data.dyn = (USize*) allocator_borrow(sizeof(USize) * _al_bool_words(al_bool.capacity), allocator);

        /* A REFUSED arena hands back null. Dropping the capacity back to the
         * inline word is the honest degradation here - the list stays usable for
         * the elements it can actually hold, and add()'s refused-reserve re-read
         * sees a capacity that matches the storage. (No wrap guard is needed here:
         * sizeof(USize) * ceil(capacity / 64) is about capacity / 8, so it cannot
         * wrap for any capacity a USize can express - reserve carries the same
         * guard anyway, as a defensive tripwire rather than a live path.) */
        if (memory_empty((void*) al_bool.data.dyn)) {
            al_bool.capacity = _AL_BOOL_BITS;
            al_bool.data.raw = 0;
        }
    }

    trace_log_pop();

    return al_bool;
}

AL_Bool al_bool_alloc_init_3(bool const *const data, USize const data_size, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_non_value_uint(LOG_METADATA, "data_size", data_size);

    AL_Bool al_bool = al_bool_alloc_init_2(data_size, allocator);

    /* Bounded by BOTH: the write is bounded by the capacity the constructor
     * actually produced, the read of data[i] by the caller's data_size, so a
     * refused or rounded capacity never reads past the caller's array and a
     * short data_size never reads past its end; size counted up per element -
     * the al_u8 twin's shape. Setting size = data_size unconditionally was the
     * one path that could still leave size > capacity: on a refused arena
     * init_2 degrades capacity to the inline word, so a 1000-element copy
     * claimed 1000 over storage for 64. No corruption followed (the bit
     * accessors mask with % _AL_BOOL_BITS and stay inside the inline word),
     * but it silently aliased bits and made at() abort past 64 - and it
     * falsified the size <= capacity invariant the clamp exists to establish. */
    for (USize i = 0; i < al_bool.capacity && i < data_size; i += 1) {
        _al_bool_set(&al_bool, i, data[i]);

        al_bool.size += 1;
    }

    trace_log_pop();

    return al_bool;
}

AL_Bool* al_bool_alloc_new_1(Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    AL_Bool *const al_bool = (AL_Bool*) allocator_borrow(sizeof(AL_Bool), allocator);

    /* The STRUCT borrow, not the element buffer: a refused arena returns null
     * here, and writing through it would put a whole struct through null. This
     * frame needs the same refused-arena guard the other constructors carry,
     * or the module header's blanket "a refused arena leaves the list
     * unchanged" promise is false for the whole alloc_new_* trio. */
    if (memory_empty((void*) al_bool)) {
        trace_log_pop();

        return nullptr;
    }

    *al_bool = al_bool_alloc_init_1(allocator);

    trace_log_pop();

    return al_bool;
}

AL_Bool* al_bool_alloc_new_2(USize const capacity, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    AL_Bool *const al_bool = al_bool_alloc_new_1(allocator);

    /* alloc_new_1 now answers null on a refused arena. */
    if (memory_empty((void*) al_bool)) {
        trace_log_pop();

        return nullptr;
    }

    *al_bool = al_bool_alloc_init_2(capacity, allocator);

    trace_log_pop();

    return al_bool;
}

AL_Bool* al_bool_alloc_new_3(bool const *const data, USize const data_size, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_non_value_uint(LOG_METADATA, "data_size", data_size);

    AL_Bool *const al_bool = al_bool_alloc_new_1(allocator);

    /* alloc_new_1 now answers null on a refused arena. */
    if (memory_empty((void*) al_bool)) {
        trace_log_pop();

        return nullptr;
    }

    *al_bool = al_bool_alloc_init_3(data, data_size, allocator);

    trace_log_pop();

    return al_bool;
}
#endif // ARENA_IMPLEMENTATION

/*==============================================================================
 * MARK: - Element Access
 *============================================================================*/

void al_bool_add(AL_Bool *const self, bool const data, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "self->size", self->size, "index > self->size", index > self->size);

    if (self->size == self->capacity) {
        if (self->capacity == 0) {
            al_bool_reserve(self, _AL_BOOL_GROWTH_FACTOR);
        }
        else {
            al_bool_reserve(self, self->capacity > USIZE_MAX / _AL_BOOL_GROWTH_FACTOR ? USIZE_MAX : self->capacity * _AL_BOOL_GROWTH_FACTOR);
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
        _al_bool_set(self, i, _al_bool_get(self, i - 1));
    }

    _al_bool_set(self, index, data);

    self->size += 1;

    trace_log_pop();
}

void al_bool_add_first(AL_Bool *const self, bool const data) {
    trace_log_push(LOG_METADATA);

    al_bool_add(self, data, 0);

    trace_log_pop();
}

void al_bool_add_last(AL_Bool *const self, bool const data) {
    trace_log_push(LOG_METADATA);

    /* self->size is read in the ARGUMENT below, so a null self faults here
     * before add's own error_check_null can report it. */
    error_check_null(LOG_METADATA, "self", (void*) self);

    /* Forwards `size`, not `size - 1`: the old index was only ever correct
     * against the broken `add`, whose special case rewrote it into an append.
     * Against a real insert it meant "insert before the last element". */
    al_bool_add(self, data, self->size);

    trace_log_pop();
}

bool al_bool_at(AL_Bool const *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "self->size", self->size, "index >= self->size", index >= self->size);
    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "self->capacity", self->capacity, "index >= self->capacity", index >= self->capacity);

    /* Both bounds, in the two files that expose set_size. set_size CLAMPS and the
     * get_size/get_capacity handles that used to bypass it now return by value, so
     * size <= capacity really is an invariant here and this second check is
     * redundant - kept as the cheapest tripwire if the clamp ever regresses. It
     * compiles out without ERROR_CHECK_ENABLED, so the clamp is the protection and
     * this is only the messenger. */

    bool const value = _al_bool_get(self, index);

    trace_log_pop();

    return value;
}

bool al_bool_back(AL_Bool const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* An empty list has no last element. This returns a VALUE, so it
     * cannot signal with null; false is the documented answer, and emptiness is
     * a data question that must not abort. Callers that need to tell an empty
     * list from a stored false check al_bool_empty first. */
    if (self->size == 0) {
        trace_log_pop();

        return false;
    }

    bool const value = _al_bool_get(self, self->size - 1);

    trace_log_pop();

    return value;
}

void al_bool_clear(AL_Bool *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (self->capacity <= _AL_BOOL_BITS) {
        self->data.raw = 0;
    }
    else {
        for (USize i = 0; i < _al_bool_words(self->capacity); i += 1) {
            self->data.dyn[i] = 0;
        }
    }

    self->size = 0;

    trace_log_pop();
}

void al_bool_delete(AL_Bool **const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "*self", (void*) *self);

    al_bool_uninit(*self);

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

bool al_bool_empty(AL_Bool const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->size == 0;
}

bool al_bool_front(AL_Bool const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* An empty list has no first element. This returns a VALUE, so it
     * cannot signal with null; false is the documented answer, and emptiness is
     * a data question that must not abort. Callers that need to tell an empty
     * list from a stored false check al_bool_empty first. */
    if (self->size == 0) {
        trace_log_pop();

        return false;
    }

    bool const value = _al_bool_get(self, 0);

    trace_log_pop();

    return value;
}

USize al_bool_get_capacity(AL_Bool const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->capacity;
}

USize al_bool_get_size(AL_Bool const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->size;
}

/*==============================================================================
 * MARK: - Heap Constructors
 *============================================================================*/

AL_Bool al_bool_init_1(void) {
#ifdef ARENA_IMPLEMENTATION
    return _al_bool_init(nullptr);
#else
    return _al_bool_init();
#endif // ARENA_IMPLEMENTATION
}

AL_Bool al_bool_init_2(USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    AL_Bool al_bool = al_bool_init_1();

    al_bool.capacity = capacity;

    if (al_bool.capacity > _AL_BOOL_BITS) {
#ifdef ARENA_IMPLEMENTATION
        al_bool.data.dyn = (USize*) allocator_borrow(sizeof(USize) * _al_bool_words(al_bool.capacity), nullptr);
#else
        al_bool.data.dyn = (USize*) allocator_borrow(sizeof(USize) * _al_bool_words(al_bool.capacity));
#endif // ARENA_IMPLEMENTATION

        /* A REFUSED arena hands back null. Dropping the capacity back to the
         * inline word is the honest degradation here - the list stays usable for
         * the elements it can actually hold, and add()'s refused-reserve re-read
         * sees a capacity that matches the storage. (No wrap guard is needed here:
         * sizeof(USize) * ceil(capacity / 64) is about capacity / 8, so it cannot
         * wrap for any capacity a USize can express.) */
        if (memory_empty((void*) al_bool.data.dyn)) {
            al_bool.capacity = _AL_BOOL_BITS;
            al_bool.data.raw = 0;
        }
    }

    trace_log_pop();

    return al_bool;
}

AL_Bool al_bool_init_3(bool const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_non_value_uint(LOG_METADATA, "data_size", data_size);

    AL_Bool al_bool = al_bool_init_2(data_size);

    /* Bounded by BOTH: the write is bounded by the capacity the constructor
     * actually produced, the read of data[i] by the caller's data_size, so a
     * refused or rounded capacity never reads past the caller's array and a
     * short data_size never reads past its end; size counted up per element -
     * the al_u8 twin's shape. Setting size = data_size unconditionally was the
     * one path that could still leave size > capacity: on a refused arena
     * init_2 degrades capacity to the inline word, so a 1000-element copy
     * claimed 1000 over storage for 64. No corruption followed (the bit
     * accessors mask with % _AL_BOOL_BITS and stay inside the inline word),
     * but it silently aliased bits and made at() abort past 64 - and it
     * falsified the size <= capacity invariant the clamp exists to establish. */
    for (USize i = 0; i < al_bool.capacity && i < data_size; i += 1) {
        _al_bool_set(&al_bool, i, data[i]);

        al_bool.size += 1;
    }

    trace_log_pop();

    return al_bool;
}

AL_Bool* al_bool_new_1(void) {
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    AL_Bool *const al_bool = (AL_Bool*) allocator_borrow(sizeof(AL_Bool), nullptr);
#else
    AL_Bool *const al_bool = (AL_Bool*) allocator_borrow(sizeof(AL_Bool));
#endif // ARENA_IMPLEMENTATION

    *al_bool = al_bool_init_1();

    trace_log_pop();

    return al_bool;
}

AL_Bool* al_bool_new_2(USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    AL_Bool *const al_bool = al_bool_new_1();

    *al_bool = al_bool_init_2(capacity);

    trace_log_pop();

    return al_bool;
}

AL_Bool* al_bool_new_3(bool const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_non_value_uint(LOG_METADATA, "data_size", data_size);

    AL_Bool *const al_bool = al_bool_new_1();

    *al_bool = al_bool_init_3(data, data_size);

    trace_log_pop();

    return al_bool;
}

/*==============================================================================
 * MARK: - Removal and Capacity
 *============================================================================*/

void al_bool_remove(AL_Bool *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "self->size", self->size, "index >= self->size", index >= self->size);

    for (USize i = index; i < self->size - 1; i += 1) {
        _al_bool_set(self, i, _al_bool_get(self, i + 1));
    }

    _al_bool_set(self, self->size - 1, false);

    self->size -= 1;

    trace_log_pop();
}

void al_bool_remove_first(AL_Bool *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* An empty list has nothing to remove; remove() would abort on the bound. */
    if (self->size == 0) {
        trace_log_pop();

        return;
    }

    al_bool_remove(self, 0);

    trace_log_pop();
}

void al_bool_remove_last(AL_Bool *const self) {
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

    al_bool_remove(self, self->size - 1);

    trace_log_pop();
}

void al_bool_reserve(AL_Bool *const self, USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    if (capacity > self->capacity) {
        if (capacity <= _AL_BOOL_BITS) {
            self->capacity = capacity;
        }
        else {
            USize const old_capacity = self->capacity;
            USize *const buffer = old_capacity > _AL_BOOL_BITS ? self->data.dyn : nullptr;

            /* Guards the same multiply alloc_init_2's comment reasons is unreachable:
             * sizeof(USize) * ceil(capacity / 64) is about capacity / 8, which cannot
             * wrap for any capacity a USize can express. Kept anyway as the cheapest
             * tripwire if that reasoning is ever invalidated - a future word width or
             * bit-packing change should not have to rediscover the multiply is safe. */
            if (_al_bool_words(capacity) > USIZE_MAX / sizeof(USize)) {
                trace_log_pop();

                return;
            }

#ifdef ARENA_IMPLEMENTATION
            USize *const words = (USize*) allocator_borrow(sizeof(USize) * _al_bool_words(capacity), self->allocator);
#else
            USize *const words = (USize*) allocator_borrow(sizeof(USize) * _al_bool_words(capacity));
#endif // ARENA_IMPLEMENTATION

            /* Null only from a REFUSED arena (allocator_borrow's documented graceful
             * path). Refuse the growth rather than writing through null on the very
             * next line - the list keeps its old capacity and add() sees that. */
            if (memory_empty((void*) words)) {
                trace_log_pop();

                return;
            }

            if (!memory_empty((void*) buffer)) {
                for (USize i = 0; i < _al_bool_words(old_capacity); i += 1) {
                    words[i] = buffer[i];
                }

#ifdef ARENA_IMPLEMENTATION
                allocator_release((void*) buffer, self->allocator);
#else
                allocator_release((void*) buffer);
#endif // ARENA_IMPLEMENTATION
            }
            else {
                words[0] = self->data.raw;
            }

            self->data.dyn = words;
            self->capacity = capacity;
        }
    }

    trace_log_pop();
}

void al_bool_set_size(AL_Bool *const self, USize const size) {
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

void al_bool_shrink(AL_Bool *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (self->size < self->capacity) {
        if (self->size <= _AL_BOOL_BITS) {
            if (self->capacity > _AL_BOOL_BITS) {
                USize const word = self->data.dyn[0];

#ifdef ARENA_IMPLEMENTATION
                allocator_release((void*) self->data.dyn, self->allocator);
#else
                allocator_release((void*) self->data.dyn);
#endif // ARENA_IMPLEMENTATION
                self->data.raw = word;
            }

            self->capacity = self->size;
        }
        else {
            USize *const buffer = self->data.dyn;

#ifdef ARENA_IMPLEMENTATION
            USize *const words = (USize*) allocator_borrow(sizeof(USize) * _al_bool_words(self->size), self->allocator);
#else
            USize *const words = (USize*) allocator_borrow(sizeof(USize) * _al_bool_words(self->size));
#endif // ARENA_IMPLEMENTATION

            /* Guarded like every other borrow in the family: a refused arena hands
             * back null here, and this refuses the shrink rather than writing
             * through it. Unreachable today - a refused arena can never have
             * produced the capacity > 64 dynamic buffer this branch needs - but the
             * guard costs nothing and keeps the shape consistent with the rest of
             * the file if that ever changes. */
            if (memory_empty((void*) words)) {
                trace_log_pop();

                return;
            }

            for (USize i = 0; i < _al_bool_words(self->size); i += 1) {
                words[i] = buffer[i];
            }

#ifdef ARENA_IMPLEMENTATION
            allocator_release((void*) buffer, self->allocator);
#else
            allocator_release((void*) buffer);
#endif // ARENA_IMPLEMENTATION
            self->data.dyn = words;
            self->capacity = self->size;
        }
    }

    trace_log_pop();
}

void al_bool_uninit(AL_Bool *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (self->capacity > _AL_BOOL_BITS) {
#ifdef ARENA_IMPLEMENTATION
        allocator_release((void*) self->data.dyn, self->allocator);
#else
        allocator_release((void*) self->data.dyn);
#endif // ARENA_IMPLEMENTATION

        /* Unconditional, not gated on MEMORY_NON_DANGLING_POINTER, matching the
         * rest of the family: a freed pointer left in the struct makes uninit's
         * idempotence depend on a build flag. */
        self->data.dyn = nullptr;
    }
    else {
        self->data.raw = 0;
    }

    self->capacity = 0;

    self->size = 0;

    trace_log_pop();
}