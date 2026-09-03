#include <container/map/map_char_al_char.h>

/*==============================================================================
 * MARK: - Internal
 *============================================================================*/

/**
 * @brief Append a pair to both lists, or take nothing at all.
 * @return true when both halves landed; false when either append declined.
 */
static bool _map_char_al_char_append(Map_Char_AL_Char *const self, char *const key, AL_Char *const value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "value", (void*) value);

    /* No null-key check here: the copying constructor carries a stored nullptr key through
     * unchanged, and a nullptr is a legal element. The public add refuses one. */
    USize const key_before = al_char_get_size(&self->key);
    USize const value_before = al_al_char_get_size(&self->value);

    al_char_add_last(&self->key, key);
    al_al_char_add_last(&self->value, value);

    /* Both add_last calls DECLINE rather than grow when the allocator refuses, and the two
     * can decline independently. Leaving a lone orphan behind mis-pairs the NEXT successful
     * add (key at N+1 against value at N), so a lookup returns another entry's value. Roll
     * the odd one out so the decline means exactly "nothing was taken". */
    if (al_char_get_size(&self->key) != key_before + 1 || al_al_char_get_size(&self->value) != value_before + 1) {
        /* NEITHER removal may release its element. add_last stored the caller's key pointer
         * verbatim and a SHALLOW STRUCT COPY of the caller's list, so an owning remove here
         * would free memory the caller still holds - and this path's whole contract is that
         * nothing was taken.
         *
         * The value side needs this as much as the key side and for a sharper reason:
         * al_al_char_remove calls al_char_uninit on the stored element, which would release
         * the caller's buffer AND every string in it. Overwriting the slot with an empty
         * list first makes that uninit a no-op - al_char_uninit on a zeroed AL_Char releases
         * a null data pointer, which allocator_release ignores. This is the one place the
         * scalar maps' simpler rollback does NOT port. */
        if (al_char_get_size(&self->key) == key_before + 1) {
            al_char_get_data(&self->key)[key_before] = nullptr;
            al_char_remove(&self->key, key_before);
        }

        if (al_al_char_get_size(&self->value) == value_before + 1) {
            al_al_char_get_data(&self->value)[value_before] = al_char_init_1();
            al_al_char_remove(&self->value, value_before);
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
 * @note KEY ONLY, and there is deliberately no value counterpart. A stored AL_Char is
 *       released through its OWN allocator field by al_al_char_remove, not through the value
 *       list's - so a helper that copied a value "through the value list" would borrow from
 *       one allocator and return through another. Deep-copying a value is init_3's job, and
 *       it builds each copy with its own allocator explicitly.
 */
static char* _map_char_al_char_copy(AL_Char const *const list, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "list", (void*) list);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* try_borrow, not char_new_2: the size comes from the caller's string, which in a server
     * is request data, and char_new_2 borrows through the ABORTING path - so a long enough
     * key would end the process instead of declining the add. */
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
 */
static void _map_char_al_char_release(AL_Char const *const list, char *const data) {
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
 * @brief Deep-copy one value list, element strings included, through the map's own allocator.
 * @return The copy. A borrow declined MID-LOOP comes back SHORT rather than empty - the
 *         copy breaks out after the appends that did land - which is why the caller
 *         compares SIZES rather than testing for emptiness. There is no separate failure
 *         channel because an AL_Char is returned by value.
 * @note This channel reports ONE of the two failures. A declined CAPACITY borrow does not
 *       reach it at all: the sizing step goes through al_char_init_2 / al_char_alloc_init_2,
 *       which abort rather than return an empty list.
 * @note The copy's own allocator is the map's, which is what makes it correct for
 *       al_al_char_remove to later uninit it: borrow and release then agree.
 */
static AL_Char _map_char_al_char_copy_value(Map_Char_AL_Char const *const self, AL_Char const *const source) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "source", (void*) source);

    USize const size = al_char_get_size(source);

#ifdef ARENA_IMPLEMENTATION
    /* FAMILY IDIOM: pick the copy's constructor by what the KEY LIST's allocator actually
     * holds, not by which family reads better. A heap-built map carries a NULL allocator, and
     * the alloc_ constructors take that argument as their arena - al_char_alloc_init_* even
     * error_checks it non-null - so routing every copy through them would abort on exactly
     * the maps that have no arena. map_char_string.c makes the same choice for the same
     * reason. */
    Arena *const allocator = self->key.allocator;

    AL_Char copy = DEFAULT_INITIALIZATION;

    if (memory_empty((void*) allocator)) {
        copy = size == 0 ? al_char_init_1() : al_char_init_2(size);
    }
    else {
        copy = size == 0 ? al_char_alloc_init_1(allocator) : al_char_alloc_init_2(size, allocator);
    }
#else
    AL_Char copy = size == 0 ? al_char_init_1() : al_char_init_2(size);
#endif // ARENA_IMPLEMENTATION

    for (USize i = 0; i < size; i += 1) {
        char const *const element = al_char_at(source, i);

        /* Measured immediately before each append rather than compared against the loop
         * counter. The counter form was correct only while EVERY iteration appended exactly
         * one element - and the nullptr branch below appended without checking, so a single
         * declined null add would desync `size` from `i` and make the NEXT iteration's
         * successful append read as a decline. That path then releases a pointer the list
         * already owns, and the caller's al_char_uninit releases it again: a double free
         * behind an invariant that held by accident. */
        USize const before = al_char_get_size(&copy);

        /* A stored nullptr element is legal in an AL_Char and copies as itself. */
        if (memory_empty((void*) element)) {
            al_char_add_last(&copy, nullptr);

            /* Nothing to give back on a decline - a null element owns nothing - but the
             * append still has to be checked, or the size stops tracking the loop. */
            if (al_char_get_size(&copy) != before + 1) {
                break;
            }

            continue;
        }

        char *const element_copy = _map_char_al_char_copy(&copy, element, char_length(element));

        if (memory_empty((void*) element_copy)) {
            break;
        }

        al_char_add_last(&copy, element_copy);

        /* add_last can decline independently of the copy succeeding, and then the copy is
         * this frame's to release - nothing else has a handle on it. */
        if (al_char_get_size(&copy) != before + 1) {
            _map_char_al_char_release(&copy, element_copy);

            break;
        }
    }

    trace_log_pop();

    return copy;
}

/**
 * @brief Deep-copy the first `size` pairs of two lists into an empty map.
 * @return true when every pair landed; false when a copy or an append was declined, having
 *         released only the pair in flight - the CALLER empties the map.
 */
static bool _map_char_al_char_fill(Map_Char_AL_Char *const self, AL_Char const *const keys, AL_AL_Char const *const values, USize const size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "keys", (void*) keys);
    error_check_null(LOG_METADATA, "values", (void*) values);

    for (USize i = 0; i < size; i += 1) {
        char const *const key = al_char_at(keys, i);
        AL_Char *const value = al_al_char_at_2(values, i);

        char *key_copy = nullptr;

        if (!memory_empty((void*) key)) {
            key_copy = _map_char_al_char_copy(&self->key, key, char_length(key));

            if (memory_empty((void*) key_copy)) {
                trace_log_pop();

                return false;
            }
        }

        AL_Char value_copy = _map_char_al_char_copy_value(self, value);

        /* A short copy means a borrow was declined partway. Give back what this frame took
         * - both the value copy, which nothing else can reach, and the key copy. */
        if (al_char_get_size(&value_copy) != al_char_get_size(value)) {
            al_char_uninit(&value_copy);
            _map_char_al_char_release(&self->key, key_copy);

            trace_log_pop();

            return false;
        }

        if (!_map_char_al_char_append(self, key_copy, &value_copy)) {
            al_char_uninit(&value_copy);
            _map_char_al_char_release(&self->key, key_copy);

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
static bool _map_char_al_char_index(Map_Char_AL_Char const *const self, char const *const key, USize const key_size, USize *const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "key", (void*) key);
    error_check_null(LOG_METADATA, "index", (void*) index);

    USize const size = map_char_al_char_get_size(self);

    for (USize i = 0; i < size; i += 1) {
        char const *const stored = al_char_at(&self->key, i);

        /* A stored nullptr key is a legal element, so it is skipped rather than handed to
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
Map_Char_AL_Char map_char_al_char_alloc_init_1(Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    Map_Char_AL_Char map_char_al_char = DEFAULT_INITIALIZATION;

    map_char_al_char.key = al_char_alloc_init_1(allocator);
    map_char_al_char.value = al_al_char_alloc_init_1(allocator);

    trace_log_pop();

    return map_char_al_char;
}

Map_Char_AL_Char map_char_al_char_alloc_init_2(USize const capacity, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    Map_Char_AL_Char map_char_al_char = DEFAULT_INITIALIZATION;

    map_char_al_char.key = al_char_alloc_init_2(capacity, allocator);
    map_char_al_char.value = al_al_char_alloc_init_2(capacity, allocator);

    trace_log_pop();

    return map_char_al_char;
}

Map_Char_AL_Char map_char_al_char_alloc_init_3(AL_Char const *const keys, AL_AL_Char const *const values, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "keys", (void*) keys);
    error_check_null(LOG_METADATA, "values", (void*) values);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    USize const key_size = al_char_get_size(keys);
    USize const value_size = al_al_char_get_size(values);
    /* The SHORTER of the two: a mismatched pair would otherwise claim entries the value
     * list does not have. */
    USize const size = key_size < value_size ? key_size : value_size;

    if (size == 0) {
        Map_Char_AL_Char const map_char_al_char = map_char_al_char_alloc_init_1(allocator);

        trace_log_pop();

        return map_char_al_char;
    }

    Map_Char_AL_Char map_char_al_char = map_char_al_char_alloc_init_2(size, allocator);

    if (!_map_char_al_char_fill(&map_char_al_char, keys, values, size)) {
        map_char_al_char_uninit(&map_char_al_char);

        map_char_al_char = map_char_al_char_alloc_init_1(allocator);
    }

    trace_log_pop();

    return map_char_al_char;
}

Map_Char_AL_Char* map_char_al_char_alloc_new_1(Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    /* try_borrow: allocator_borrow reaches arena_linear_alloc, which ABORTS on exhaustion,
     * so a live-but-full arena would kill the process here while a refused one declined. */
    Map_Char_AL_Char *const map_char_al_char = (Map_Char_AL_Char*) allocator_try_borrow(sizeof(Map_Char_AL_Char), allocator);

    if (memory_empty((void*) map_char_al_char)) {
        trace_log_pop();

        return nullptr;
    }

    *map_char_al_char = map_char_al_char_alloc_init_1(allocator);

    trace_log_pop();

    return map_char_al_char;
}

Map_Char_AL_Char* map_char_al_char_alloc_new_2(USize const capacity, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    Map_Char_AL_Char *const map_char_al_char = (Map_Char_AL_Char*) allocator_try_borrow(sizeof(Map_Char_AL_Char), allocator);

    if (memory_empty((void*) map_char_al_char)) {
        trace_log_pop();

        return nullptr;
    }

    *map_char_al_char = map_char_al_char_alloc_init_2(capacity, allocator);

    trace_log_pop();

    return map_char_al_char;
}

Map_Char_AL_Char* map_char_al_char_alloc_new_3(AL_Char const *const keys, AL_AL_Char const *const values, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "keys", (void*) keys);
    error_check_null(LOG_METADATA, "values", (void*) values);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    Map_Char_AL_Char *const map_char_al_char = (Map_Char_AL_Char*) allocator_try_borrow(sizeof(Map_Char_AL_Char), allocator);

    if (memory_empty((void*) map_char_al_char)) {
        trace_log_pop();

        return nullptr;
    }

    *map_char_al_char = map_char_al_char_alloc_init_3(keys, values, allocator);

    trace_log_pop();

    return map_char_al_char;
}
#endif // ARENA_IMPLEMENTATION

/*==============================================================================
 * MARK: - Element Access
 *============================================================================*/

AL_Char* map_char_al_char_at_1(Map_Char_AL_Char const *const self, char const *const key) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "key", (void*) key);

    AL_Char *const value = map_char_al_char_at_2(self, key, char_length(key));

    trace_log_pop();

    return value;
}

AL_Char* map_char_al_char_at_2(Map_Char_AL_Char const *const self, char const *const key, USize const key_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "key", (void*) key);

    USize index = 0;

    /* No guard on key_size. An empty key is a legal key, and an abort here would make "=x"
     * in a parsed request body a remote process kill. */
    if (!_map_char_al_char_index(self, key, key_size, &index)) {
        trace_log_pop();

        return nullptr;
    }

    AL_Char *const value = al_al_char_at_2(&self->value, index);

    trace_log_pop();

    return value;
}

bool map_char_al_char_contains_1(Map_Char_AL_Char const *const self, char const *const key) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "key", (void*) key);

    bool const found = map_char_al_char_contains_2(self, key, char_length(key));

    trace_log_pop();

    return found;
}

bool map_char_al_char_contains_2(Map_Char_AL_Char const *const self, char const *const key, USize const key_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "key", (void*) key);

    USize index = 0;
    bool const found = _map_char_al_char_index(self, key, key_size, &index);

    trace_log_pop();

    return found;
}

bool map_char_al_char_empty(Map_Char_AL_Char const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    bool const empty = map_char_al_char_get_size(self) == 0;

    trace_log_pop();

    return empty;
}

USize map_char_al_char_get_capacity(Map_Char_AL_Char const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    USize const key_capacity = al_char_get_capacity(&self->key);
    USize const value_capacity = al_al_char_get_capacity(&self->value);

    /* The SMALLER: a pair needs a slot in both lists, so the larger capacity is room the
     * map cannot actually use. */
    trace_log_pop();

    return key_capacity < value_capacity ? key_capacity : value_capacity;
}

char* map_char_al_char_get_key(Map_Char_AL_Char const *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    USize const size = map_char_al_char_get_size(self);

    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "size", size, "index >= size", index >= size);

    char *const key = al_char_at(&self->key, index);

    trace_log_pop();

    return key;
}

AL_Char* map_char_al_char_get_keys(Map_Char_AL_Char *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return &self->key;
}

USize map_char_al_char_get_size(Map_Char_AL_Char const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    USize const key_size = al_char_get_size(&self->key);
    USize const value_size = al_al_char_get_size(&self->value);

    /* DERIVED, not stored. A third counter beside the lists' own two could disagree with
     * them - and did, through get_keys/get_values, which hand out mutable handles the map
     * cannot observe. */
    trace_log_pop();

    return key_size < value_size ? key_size : value_size;
}

AL_Char* map_char_al_char_get_value(Map_Char_AL_Char const *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    USize const size = map_char_al_char_get_size(self);

    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "size", size, "index >= size", index >= size);

    AL_Char *const value = al_al_char_at_2(&self->value, index);

    trace_log_pop();

    return value;
}

AL_AL_Char* map_char_al_char_get_values(Map_Char_AL_Char *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return &self->value;
}

/*==============================================================================
 * MARK: - Heap Constructors
 *============================================================================*/

Map_Char_AL_Char map_char_al_char_init_1(void) {
    trace_log_push(LOG_METADATA);

    Map_Char_AL_Char map_char_al_char = DEFAULT_INITIALIZATION;

    /* Built through the lists' own constructors rather than left at the all-zero state:
     * that guarantee is each list's to make, and made here it holds in one place. */
    map_char_al_char.key = al_char_init_1();
    map_char_al_char.value = al_al_char_init_1();

    trace_log_pop();

    return map_char_al_char;
}

Map_Char_AL_Char map_char_al_char_init_2(USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    Map_Char_AL_Char map_char_al_char = DEFAULT_INITIALIZATION;

    map_char_al_char.key = al_char_init_2(capacity);
    map_char_al_char.value = al_al_char_init_2(capacity);

    trace_log_pop();

    return map_char_al_char;
}

Map_Char_AL_Char map_char_al_char_init_3(AL_Char const *const keys, AL_AL_Char const *const values) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "keys", (void*) keys);
    error_check_null(LOG_METADATA, "values", (void*) values);

    USize const key_size = al_char_get_size(keys);
    USize const value_size = al_al_char_get_size(values);
    USize const size = key_size < value_size ? key_size : value_size;

    if (size == 0) {
        Map_Char_AL_Char const map_char_al_char = map_char_al_char_init_1();

        trace_log_pop();

        return map_char_al_char;
    }

    Map_Char_AL_Char map_char_al_char = map_char_al_char_init_2(size);

    if (!_map_char_al_char_fill(&map_char_al_char, keys, values, size)) {
        map_char_al_char_uninit(&map_char_al_char);

        map_char_al_char = map_char_al_char_init_1();
    }

    trace_log_pop();

    return map_char_al_char;
}

Map_Char_AL_Char* map_char_al_char_new_1(void) {
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    Map_Char_AL_Char *const map_char_al_char = (Map_Char_AL_Char*) allocator_try_borrow(sizeof(Map_Char_AL_Char), nullptr);
#else
    Map_Char_AL_Char *const map_char_al_char = (Map_Char_AL_Char*) allocator_try_borrow(sizeof(Map_Char_AL_Char));
#endif // ARENA_IMPLEMENTATION

    if (memory_empty((void*) map_char_al_char)) {
        trace_log_pop();

        return nullptr;
    }

    *map_char_al_char = map_char_al_char_init_1();

    trace_log_pop();

    return map_char_al_char;
}

Map_Char_AL_Char* map_char_al_char_new_2(USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

#ifdef ARENA_IMPLEMENTATION
    Map_Char_AL_Char *const map_char_al_char = (Map_Char_AL_Char*) allocator_try_borrow(sizeof(Map_Char_AL_Char), nullptr);
#else
    Map_Char_AL_Char *const map_char_al_char = (Map_Char_AL_Char*) allocator_try_borrow(sizeof(Map_Char_AL_Char));
#endif // ARENA_IMPLEMENTATION

    if (memory_empty((void*) map_char_al_char)) {
        trace_log_pop();

        return nullptr;
    }

    *map_char_al_char = map_char_al_char_init_2(capacity);

    trace_log_pop();

    return map_char_al_char;
}

Map_Char_AL_Char* map_char_al_char_new_3(AL_Char const *const keys, AL_AL_Char const *const values) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "keys", (void*) keys);
    error_check_null(LOG_METADATA, "values", (void*) values);

#ifdef ARENA_IMPLEMENTATION
    Map_Char_AL_Char *const map_char_al_char = (Map_Char_AL_Char*) allocator_try_borrow(sizeof(Map_Char_AL_Char), nullptr);
#else
    Map_Char_AL_Char *const map_char_al_char = (Map_Char_AL_Char*) allocator_try_borrow(sizeof(Map_Char_AL_Char));
#endif // ARENA_IMPLEMENTATION

    if (memory_empty((void*) map_char_al_char)) {
        trace_log_pop();

        return nullptr;
    }

    *map_char_al_char = map_char_al_char_init_3(keys, values);

    trace_log_pop();

    return map_char_al_char;
}

/*==============================================================================
 * MARK: - Insertion
 *============================================================================*/

/**
 * @brief Empty a source AL_Char whose buffer the map has just taken, so exactly one claimant
 *        remains.
 * @param source List to vacate. Must not be nullptr.
 * @note `allocator` is deliberately LEFT INTACT: it records where the struct's storage was
 *       borrowed, and the caller may still route a later operation through it - the same
 *       carve-out _map_char_string_vacate documents for String's struct release.
 */
static void _map_char_al_char_vacate(AL_Char *const source) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "source", (void*) source);

    source->capacity = 0;
    source->data = nullptr;
    source->size = 0;

    trace_log_pop();
}

bool map_char_al_char_add(Map_Char_AL_Char *const self, char *const key, AL_Char **const value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "key", (void*) key);
    error_check_null(LOG_METADATA, "value", (void*) value);
    error_check_null(LOG_METADATA, "*value", (void*) *value);

    if (!_map_char_al_char_append(self, key, *value)) {
        trace_log_pop();

        return false;
    }

    /* Vacated only AFTER both halves landed - map_char_string's order, and the whole
     * contract: a decline leaves the caller's list holding its buffer, a success leaves
     * exactly one claimant. This is what retired the "do not uninit your local afterwards"
     * caveat: the vacated local is release-quiet by construction. */
    _map_char_al_char_vacate(*value);

    *value = nullptr;

    trace_log_pop();

    return true;
}

bool map_char_al_char_add_static(Map_Char_AL_Char *const self, char const *const key, AL_Char **const value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "key", (void*) key);

    bool const added = map_char_al_char_add_static_2(self, key, char_length(key), value);

    trace_log_pop();

    return added;
}

bool map_char_al_char_add_static_2(Map_Char_AL_Char *const self, char const *const key, USize const key_size, AL_Char **const value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "key", (void*) key);
    error_check_null(LOG_METADATA, "value", (void*) value);
    error_check_null(LOG_METADATA, "*value", (void*) *value);

    char *const key_copy = _map_char_al_char_copy(&self->key, key, key_size);

    if (memory_empty((void*) key_copy)) {
        trace_log_pop();

        return false;
    }

    if (!_map_char_al_char_append(self, key_copy, *value)) {
        _map_char_al_char_release(&self->key, key_copy);

        trace_log_pop();

        return false;
    }

    _map_char_al_char_vacate(*value);

    *value = nullptr;

    trace_log_pop();

    return true;
}

/*==============================================================================
 * MARK: - Removal and Capacity
 *============================================================================*/

void map_char_al_char_clear(Map_Char_AL_Char *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    al_char_clear(&self->key);
    al_al_char_clear(&self->value);

    trace_log_pop();
}

void map_char_al_char_delete(Map_Char_AL_Char **const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "*self", (void*) *self);

#ifdef ARENA_IMPLEMENTATION
    /* Read before uninit because the read belongs next to its use. The KEY side is the one
     * consulted, which is exact for every map this file builds. */
    Arena *const allocator = (*self)->key.allocator;
#endif // ARENA_IMPLEMENTATION

    map_char_al_char_uninit(*self);

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

bool map_char_al_char_remove_1(Map_Char_AL_Char *const self, char const *const key) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "key", (void*) key);

    bool const removed = map_char_al_char_remove_2(self, key, char_length(key));

    trace_log_pop();

    return removed;
}

bool map_char_al_char_remove_2(Map_Char_AL_Char *const self, char const *const key, USize const key_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "key", (void*) key);

    USize index = 0;

    if (!_map_char_al_char_index(self, key, key_size, &index)) {
        trace_log_pop();

        return false;
    }

    /* Both removals RELEASE, which is correct here and wrong in _map_char_al_char_append: a
     * stored pair belongs to the map, an unrolled append never did. The index came from
     * get_size, the smaller of the two, so it is in bounds for both lists. */
    al_char_remove(&self->key, index);
    al_al_char_remove(&self->value, index);

    trace_log_pop();

    return true;
}

void map_char_al_char_remove_at(Map_Char_AL_Char *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    USize const size = map_char_al_char_get_size(self);

    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "size", size, "index >= size", index >= size);

    al_char_remove(&self->key, index);
    al_al_char_remove(&self->value, index);

    trace_log_pop();
}

void map_char_al_char_reserve(Map_Char_AL_Char *const self, USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    al_char_reserve(&self->key, capacity);
    al_al_char_reserve(&self->value, capacity);

    trace_log_pop();
}

void map_char_al_char_shrink(Map_Char_AL_Char *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    al_char_shrink(&self->key);
    al_al_char_shrink(&self->value);

    trace_log_pop();
}

void map_char_al_char_uninit(Map_Char_AL_Char *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    al_char_uninit(&self->key);
    al_al_char_uninit(&self->value);

    trace_log_pop();
}