#include <container/map/map_char_u32.h>

/*==============================================================================
 * MARK: - Internal
 *============================================================================*/

/**
 * @brief Append a pair to both lists, or take nothing at all.
 * @return true when both halves landed; false when either append declined.
 */
static bool _map_char_u32_append(Map_Char_U32 *const self, char *const key, U32 const value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* No null-key check here: the copying constructor carries a stored nullptr key through
     * unchanged, and a nullptr is a legal element. The public add refuses one. */
    USize const key_before = al_char_get_size(&self->key);
    USize const value_before = al_u32_get_size(&self->value);

    al_char_add_last(&self->key, key);
    al_u32_add_last(&self->value, value);

    /* Both add_last calls DECLINE rather than grow when the allocator refuses, and the two
     * can decline independently. Leaving a lone orphan behind mis-pairs the NEXT successful
     * add (key at N+1 against value at N), so a lookup returns another entry's value. Roll
     * the odd one out so the decline means exactly "nothing was taken". */
    if (al_char_get_size(&self->key) != key_before + 1 || al_u32_get_size(&self->value) != value_before + 1) {
        /* The KEY removal must NOT release the element: add_last stored the caller's
         * pointer verbatim, so an owning remove here would free memory the caller still
         * holds - and this path's whole contract is that nothing was taken. Empty the tail
         * slot first; remove() then shifts an already-empty slot out.
         *
         * The VALUE side needs no such care - al_u32_remove releases nothing, because a
         * scalar owns nothing. That asymmetry is the one thing that does not survive the
         * port from map_char_char, where both halves needed neutralising. */
        if (al_char_get_size(&self->key) == key_before + 1) {
            al_char_get_data(&self->key)[key_before] = nullptr;
            al_char_remove(&self->key, key_before);
        }

        if (al_u32_get_size(&self->value) == value_before + 1) {
            al_u32_remove(&self->value, value_before);
        }

        trace_log_pop();

        return false;
    }

    trace_log_pop();

    return true;
}

/**
 * @brief Duplicate `data_size` bytes through the key list's allocator, without ever aborting.
 * @return The copy, NUL-terminated, or nullptr when the allocator declined.
 * @note SIZED, because the sized form is the primitive: the terminated caller already had to
 *       measure with char_length before this could allocate, so taking the size lets
 *       map_char_u32_add_static_2 store a key that is a slice of a larger buffer.
 * @note Takes the KEY LIST specifically, not the map. Only keys are copied on this
 *       instantiation - the value is a scalar - so unlike map_char_char there is no second
 *       list this could be called with, and no cross-allocator hazard to guard against.
 */
static char* _map_char_u32_copy(AL_Char const *const list, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "list", (void*) list);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* try_borrow, not char_new_2: the size comes from the caller's string, which in
     * a server is request data, and char_new_2 borrows through the ABORTING path -
     * so a long enough key would end the process instead of declining the add. */
#ifdef ARENA_IMPLEMENTATION
    char *const buffer = (char*) allocator_try_borrow(data_size + CHAR_END_CHARACTER, list->allocator);
#else
    char *const buffer = (char*) allocator_try_borrow(data_size + CHAR_END_CHARACTER);
#endif // ARENA_IMPLEMENTATION

    if (memory_empty((void*) buffer)) {
        trace_log_pop();

        return nullptr;
    }

    char_copy_3(buffer, data_size + CHAR_END_CHARACTER, data, data_size);

    trace_log_pop();

    return buffer;
}

/**
 * @brief Release a key through the allocator of the list it came from.
 * @note Takes the list for the same reason _map_char_u32_copy does; the two must always be
 *       called with the same list for one pointer.
 */
static void _map_char_u32_release(AL_Char const *const list, char *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "list", (void*) list);

    if (memory_empty((void*) data)) {
        trace_log_pop();

        return;
    }

#ifdef ARENA_IMPLEMENTATION
    allocator_release((void*) data, list->allocator);
#else
    allocator_release((void*) data);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();
}

/**
 * @brief Deep-copy the keys and copy the values of the first `size` pairs into an empty map.
 * @return true when every pair landed; false when a copy or an append was declined, having
 *         released only the pair in flight - the CALLER empties the map.
 * @note The all-or-nothing property is real but it lives one level up, in the two constructors
 *       that uninit and rebuild on a false.
 * @note The KEYS are copied, not adopted, so the caller keeps its source lists intact and
 *       releases them normally. The values are scalars and copy themselves.
 */
static bool _map_char_u32_fill(Map_Char_U32 *const self, AL_Char const *const keys, AL_U32 const *const values, USize const size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "keys", (void*) keys);
    error_check_null(LOG_METADATA, "values", (void*) values);

    for (USize i = 0; i < size; i += 1) {
        char const *const key = al_char_at(keys, i);

        char *key_copy = nullptr;

        /* A stored nullptr key is a legal element, and copies as itself. */
        if (!memory_empty((void*) key)) {
            key_copy = _map_char_u32_copy(&self->key, key, char_length(key));

            if (memory_empty((void*) key_copy)) {
                trace_log_pop();

                return false;
            }
        }

        if (!_map_char_u32_append(self, key_copy, *al_u32_at(values, i))) {
            _map_char_u32_release(&self->key, key_copy);

            trace_log_pop();

            return false;
        }
    }

    trace_log_pop();

    return true;
}

/**
 * @brief Find the index of the first pair whose key matches.
 * @return true when found, writing the index through `index`.
 */
static bool _map_char_u32_index(Map_Char_U32 const *const self, char const *const key, USize const key_size, USize *const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "key", (void*) key);
    error_check_null(LOG_METADATA, "index", (void*) index);

    USize const size = map_char_u32_get_size(self);

    for (USize i = 0; i < size; i += 1) {
        char const *const stored = al_char_at(&self->key, i);

        /* A stored nullptr key is a legal element - al_char accepts one and the copying
         * constructors can carry one in - so it is skipped rather than handed to
         * char_length, which would dereference it. */
        if (memory_empty((void*) stored)) {
            continue;
        }

        if (char_compare_equal_2(stored, char_length(stored), key, key_size)) {
            *index = i;

            trace_log_pop();

            return true;
        }
    }

    trace_log_pop();

    return false;
}

/*==============================================================================
 * MARK: - Arena Constructors
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
Map_Char_U32 map_char_u32_alloc_init_1(Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    Map_Char_U32 map_char_u32 = DEFAULT_INITIALIZATION;

    map_char_u32.key = al_char_alloc_init_1(allocator);
    map_char_u32.value = al_u32_alloc_init_1(allocator);

    trace_log_pop();

    return map_char_u32;
}

Map_Char_U32 map_char_u32_alloc_init_2(USize const capacity, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    Map_Char_U32 map_char_u32 = DEFAULT_INITIALIZATION;

    map_char_u32.key = al_char_alloc_init_2(capacity, allocator);
    map_char_u32.value = al_u32_alloc_init_2(capacity, allocator);

    trace_log_pop();

    return map_char_u32;
}

Map_Char_U32 map_char_u32_alloc_init_3(AL_Char const *const keys, AL_U32 const *const values, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "keys", (void*) keys);
    error_check_null(LOG_METADATA, "values", (void*) values);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    USize const key_size = al_char_get_size(keys);
    USize const value_size = al_u32_get_size(values);
    /* The SHORTER of the two: a mismatched pair (5 keys, 3 values) would otherwise claim
     * entries the value list does not have. */
    USize const size = key_size < value_size ? key_size : value_size;

    if (size == 0) {
        Map_Char_U32 const map_char_u32 = map_char_u32_alloc_init_1(allocator);

        trace_log_pop();

        return map_char_u32;
    }

    Map_Char_U32 map_char_u32 = map_char_u32_alloc_init_2(size, allocator);

    /* All or nothing: a partially built map would hold key copies the caller has no handle
     * on while reporting a size that does not account for them. */
    if (!_map_char_u32_fill(&map_char_u32, keys, values, size)) {
        map_char_u32_uninit(&map_char_u32);

        map_char_u32 = map_char_u32_alloc_init_1(allocator);
    }

    trace_log_pop();

    return map_char_u32;
}

Map_Char_U32* map_char_u32_alloc_new_1(Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    /* try_borrow for the same reason new_* uses it: allocator_borrow reaches
     * arena_linear_alloc, which ABORTS on exhaustion, so a live-but-full arena would kill
     * the process here while a refused one declined.
     *
     * This closes the STRUCT borrow only. Growing the lists still goes through
     * al_char_reserve / al_u32_reserve, so an exhausted arena or a failing heap can still
     * abort inside an add; that is the array lists' to change, and the header says so
     * rather than promising a refusal this module cannot deliver. */
    Map_Char_U32 *const map_char_u32 = (Map_Char_U32*) allocator_try_borrow(sizeof(Map_Char_U32), allocator);

    if (memory_empty((void*) map_char_u32)) {
        trace_log_pop();

        return nullptr;
    }

    *map_char_u32 = map_char_u32_alloc_init_1(allocator);

    trace_log_pop();

    return map_char_u32;
}

Map_Char_U32* map_char_u32_alloc_new_2(USize const capacity, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    Map_Char_U32 *const map_char_u32 = (Map_Char_U32*) allocator_try_borrow(sizeof(Map_Char_U32), allocator);

    if (memory_empty((void*) map_char_u32)) {
        trace_log_pop();

        return nullptr;
    }

    *map_char_u32 = map_char_u32_alloc_init_2(capacity, allocator);

    trace_log_pop();

    return map_char_u32;
}

Map_Char_U32* map_char_u32_alloc_new_3(AL_Char const *const keys, AL_U32 const *const values, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "keys", (void*) keys);
    error_check_null(LOG_METADATA, "values", (void*) values);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    Map_Char_U32 *const map_char_u32 = (Map_Char_U32*) allocator_try_borrow(sizeof(Map_Char_U32), allocator);

    if (memory_empty((void*) map_char_u32)) {
        trace_log_pop();

        return nullptr;
    }

    *map_char_u32 = map_char_u32_alloc_init_3(keys, values, allocator);

    trace_log_pop();

    return map_char_u32;
}
#endif // ARENA_IMPLEMENTATION

/*==============================================================================
 * MARK: - Element Access
 *============================================================================*/

U32* map_char_u32_at_1(Map_Char_U32 const *const self, char const *const key) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "key", (void*) key);

    U32 *const value = map_char_u32_at_2(self, key, char_length(key));

    trace_log_pop();

    return value;
}

U32* map_char_u32_at_2(Map_Char_U32 const *const self, char const *const key, USize const key_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "key", (void*) key);

    USize index = 0;

    /* No guard on key_size. An empty key is a legal key, and an abort here would make "=x"
     * in a parsed request body a remote process kill - the value-dependent-refusal
     * standard: a condition that depends on the DATA is refused, never checked. Here
     * "refused" is the nullptr an absent key answers. */
    if (!_map_char_u32_index(self, key, key_size, &index)) {
        trace_log_pop();

        return nullptr;
    }

    U32 *const value = al_u32_at(&self->value, index);

    trace_log_pop();

    return value;
}

bool map_char_u32_contains_1(Map_Char_U32 const *const self, char const *const key) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "key", (void*) key);

    bool const found = map_char_u32_contains_2(self, key, char_length(key));

    trace_log_pop();

    return found;
}

bool map_char_u32_contains_2(Map_Char_U32 const *const self, char const *const key, USize const key_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "key", (void*) key);

    USize index = 0;
    bool const found = _map_char_u32_index(self, key, key_size, &index);

    trace_log_pop();

    return found;
}

bool map_char_u32_empty(Map_Char_U32 const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    bool const empty = map_char_u32_get_size(self) == 0;

    trace_log_pop();

    return empty;
}

USize map_char_u32_get_capacity(Map_Char_U32 const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    USize const key_capacity = al_char_get_capacity(&self->key);
    USize const value_capacity = al_u32_get_capacity(&self->value);

    /* The SMALLER: a pair needs a slot in both lists, so the larger capacity is room the
     * map cannot actually use. Reporting the key list's alone would promise space that a
     * value-side append still has to grow into. */
    trace_log_pop();

    return key_capacity < value_capacity ? key_capacity : value_capacity;
}

char* map_char_u32_get_key(Map_Char_U32 const *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    USize const size = map_char_u32_get_size(self);

    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "size", size, "index >= size", index >= size);

    char *const key = al_char_at(&self->key, index);

    trace_log_pop();

    return key;
}

AL_Char* map_char_u32_get_keys(Map_Char_U32 *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return &self->key;
}

USize map_char_u32_get_size(Map_Char_U32 const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    USize const key_size = al_char_get_size(&self->key);
    USize const value_size = al_u32_get_size(&self->value);

    /* DERIVED, not stored. A third counter beside the lists' own two could disagree with
     * them - and did, through get_keys/get_values, which hand out mutable handles the map
     * cannot observe. The minimum is the only honest answer: a pair needs both halves, so
     * a list that is short of the other bounds the map. */
    trace_log_pop();

    return key_size < value_size ? key_size : value_size;
}

U32* map_char_u32_get_value(Map_Char_U32 const *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    USize const size = map_char_u32_get_size(self);

    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "size", size, "index >= size", index >= size);

    U32 *const value = al_u32_at(&self->value, index);

    trace_log_pop();

    return value;
}

AL_U32* map_char_u32_get_values(Map_Char_U32 *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return &self->value;
}

/*==============================================================================
 * MARK: - Heap Constructors
 *============================================================================*/

Map_Char_U32 map_char_u32_init_1(void) {
    trace_log_push(LOG_METADATA);

    Map_Char_U32 map_char_u32 = DEFAULT_INITIALIZATION;

    /* Built through the lists' own constructors. The all-zero state is a valid empty list
     * today, but that is each list's guarantee to make, and made here it holds in one place
     * rather than in nine files trusting a struct layout they do not own. */
    map_char_u32.key = al_char_init_1();
    map_char_u32.value = al_u32_init_1();

    trace_log_pop();

    return map_char_u32;
}

Map_Char_U32 map_char_u32_init_2(USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    Map_Char_U32 map_char_u32 = DEFAULT_INITIALIZATION;

    map_char_u32.key = al_char_init_2(capacity);
    map_char_u32.value = al_u32_init_2(capacity);

    trace_log_pop();

    return map_char_u32;
}

Map_Char_U32 map_char_u32_init_3(AL_Char const *const keys, AL_U32 const *const values) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "keys", (void*) keys);
    error_check_null(LOG_METADATA, "values", (void*) values);

    USize const key_size = al_char_get_size(keys);
    USize const value_size = al_u32_get_size(values);
    USize const size = key_size < value_size ? key_size : value_size;

    /* See map_char_u32_alloc_init_3: the shorter of the two, and an empty source builds an
     * empty map rather than tripping al_char_init_2's zero-capacity contract. */
    if (size == 0) {
        Map_Char_U32 const map_char_u32 = map_char_u32_init_1();

        trace_log_pop();

        return map_char_u32;
    }

    Map_Char_U32 map_char_u32 = map_char_u32_init_2(size);

    if (!_map_char_u32_fill(&map_char_u32, keys, values, size)) {
        map_char_u32_uninit(&map_char_u32);

        map_char_u32 = map_char_u32_init_1();
    }

    trace_log_pop();

    return map_char_u32;
}

Map_Char_U32* map_char_u32_new_1(void) {
    trace_log_push(LOG_METADATA);

    /* try_borrow, matching alloc_new_*: allocator_borrow routes a null arena to the
     * ABORTING memory_alloc, and the Error Handling block promises a refusal rather than
     * a process kill for anything that depends on how much memory is left. */
#ifdef ARENA_IMPLEMENTATION
    Map_Char_U32 *const map_char_u32 = (Map_Char_U32*) allocator_try_borrow(sizeof(Map_Char_U32), nullptr);
#else
    Map_Char_U32 *const map_char_u32 = (Map_Char_U32*) allocator_try_borrow(sizeof(Map_Char_U32));
#endif // ARENA_IMPLEMENTATION

    if (memory_empty((void*) map_char_u32)) {
        trace_log_pop();

        return nullptr;
    }

    *map_char_u32 = map_char_u32_init_1();

    trace_log_pop();

    return map_char_u32;
}

Map_Char_U32* map_char_u32_new_2(USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

#ifdef ARENA_IMPLEMENTATION
    Map_Char_U32 *const map_char_u32 = (Map_Char_U32*) allocator_try_borrow(sizeof(Map_Char_U32), nullptr);
#else
    Map_Char_U32 *const map_char_u32 = (Map_Char_U32*) allocator_try_borrow(sizeof(Map_Char_U32));
#endif // ARENA_IMPLEMENTATION

    if (memory_empty((void*) map_char_u32)) {
        trace_log_pop();

        return nullptr;
    }

    *map_char_u32 = map_char_u32_init_2(capacity);

    trace_log_pop();

    return map_char_u32;
}

Map_Char_U32* map_char_u32_new_3(AL_Char const *const keys, AL_U32 const *const values) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "keys", (void*) keys);
    error_check_null(LOG_METADATA, "values", (void*) values);

#ifdef ARENA_IMPLEMENTATION
    Map_Char_U32 *const map_char_u32 = (Map_Char_U32*) allocator_try_borrow(sizeof(Map_Char_U32), nullptr);
#else
    Map_Char_U32 *const map_char_u32 = (Map_Char_U32*) allocator_try_borrow(sizeof(Map_Char_U32));
#endif // ARENA_IMPLEMENTATION

    if (memory_empty((void*) map_char_u32)) {
        trace_log_pop();

        return nullptr;
    }

    *map_char_u32 = map_char_u32_init_3(keys, values);

    trace_log_pop();

    return map_char_u32;
}

/*==============================================================================
 * MARK: - Insertion
 *============================================================================*/

bool map_char_u32_add(Map_Char_U32 *const self, char *const key, U32 const value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "key", (void*) key);

    bool const added = _map_char_u32_append(self, key, value);

    trace_log_pop();

    return added;
}

bool map_char_u32_add_static(Map_Char_U32 *const self, char const *const key, U32 const value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "key", (void*) key);

    bool const added = map_char_u32_add_static_2(self, key, char_length(key), value);

    trace_log_pop();

    return added;
}

bool map_char_u32_add_static_2(Map_Char_U32 *const self, char const *const key, USize const key_size, U32 const value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "key", (void*) key);

    char *const key_copy = _map_char_u32_copy(&self->key, key, key_size);

    if (memory_empty((void*) key_copy)) {
        trace_log_pop();

        return false;
    }

    if (!_map_char_u32_append(self, key_copy, value)) {
        _map_char_u32_release(&self->key, key_copy);

        trace_log_pop();

        return false;
    }

    trace_log_pop();

    return true;
}

/*==============================================================================
 * MARK: - Removal and Capacity
 *============================================================================*/

void map_char_u32_clear(Map_Char_U32 *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    al_char_clear(&self->key);
    al_u32_clear(&self->value);

    trace_log_pop();
}

void map_char_u32_delete(Map_Char_U32 **const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "*self", (void*) *self);

#ifdef ARENA_IMPLEMENTATION
    /* Read before uninit because the read belongs next to its use, not because reading it
     * afterwards would be unsafe - al_char_uninit nulls the list's data and zeroes its
     * capacity but never touches its allocator field, and *self is still allocated, so the
     * later read would be identical. The struct has to go back to the arena the lists came
     * from, and keeping the two adjacent is what makes that obvious.
     *
     * The KEY side is the one consulted, which is exact for every map this file builds:
     * both new_ and alloc_new_ give the struct and the two lists one allocator. A map
     * assembled by hand from two lists with different allocators cannot be deleted through
     * this, and the header says so. */
    Arena *const allocator = (*self)->key.allocator;
#endif // ARENA_IMPLEMENTATION

    map_char_u32_uninit(*self);

#ifdef ARENA_IMPLEMENTATION
    allocator_release((void*) *self, allocator);
#else
    allocator_release((void*) *self);
#endif // ARENA_IMPLEMENTATION

#ifdef MEMORY_NON_DANGLING_POINTER
    *self = nullptr;
#endif // MEMORY_NON_DANGLING_POINTER

    trace_log_pop();
}

bool map_char_u32_remove_1(Map_Char_U32 *const self, char const *const key) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "key", (void*) key);

    bool const removed = map_char_u32_remove_2(self, key, char_length(key));

    trace_log_pop();

    return removed;
}

bool map_char_u32_remove_2(Map_Char_U32 *const self, char const *const key, USize const key_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "key", (void*) key);

    USize index = 0;

    if (!_map_char_u32_index(self, key, key_size, &index)) {
        trace_log_pop();

        return false;
    }

    /* The KEY removal releases the element, which is correct here and wrong in
     * _map_char_u32_append: a stored key belongs to the map, an unrolled append never did.
     * The index came from get_size, the smaller of the two, so it is in bounds for both
     * lists. */
    al_char_remove(&self->key, index);
    al_u32_remove(&self->value, index);

    trace_log_pop();

    return true;
}

void map_char_u32_remove_at(Map_Char_U32 *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    USize const size = map_char_u32_get_size(self);

    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "size", size, "index >= size", index >= size);

    al_char_remove(&self->key, index);
    al_u32_remove(&self->value, index);

    trace_log_pop();
}

void map_char_u32_reserve(Map_Char_U32 *const self, USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    al_char_reserve(&self->key, capacity);
    al_u32_reserve(&self->value, capacity);

    trace_log_pop();
}

void map_char_u32_shrink(Map_Char_U32 *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    al_char_shrink(&self->key);
    al_u32_shrink(&self->value);

    trace_log_pop();
}

void map_char_u32_uninit(Map_Char_U32 *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    al_char_uninit(&self->key);
    al_u32_uninit(&self->value);

    trace_log_pop();
}