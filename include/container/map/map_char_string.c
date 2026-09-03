#include <container/map/map_char_string.h>

/*==============================================================================
 * MARK: - Internal
 *============================================================================*/

/**
 * @brief Empty a moved-from String so only the map claims its buffer.
 * @note Mirrors string_move_4's source clearing, and for its reason: leaving the source with
 *       a live data pointer and `owned` still set makes any retained alias a second owner of
 *       the same buffer, so the next string_uninit is a double free. Clearing the caller's
 *       POINTER alone would not be enough - the object it pointed at is what has to go quiet.
 * @note `allocator` is deliberately left alone. It records where the STRUCT was borrowed, not
 *       where the buffer was, and string_delete still needs it if the source came from
 *       string_new_*. This is the same carve-out string_move_4 documents.
 * @note string_move_4 itself cannot serve here: it moves INTO an existing String, and would
 *       need a destination to release first. The append below puts a fresh struct copy into
 *       the list instead, which is also why this module inherits none of move_4's
 *       cross-allocator refusal - see the header.
 */
static void _map_char_string_vacate(String *const source) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "source", (void*) source);

    source->capacity = 0;
    source->data = nullptr;
    source->size = 0;
    source->owned = false;

    trace_log_pop();
}

/**
 * @brief Append a pair to both lists, or take nothing at all.
 * @return true when both halves landed; false when either append declined.
 * @note Does NOT vacate the source - that is the caller's last step, so that a decline can
 *       leave the caller's String whole.
 */
static bool _map_char_string_append(Map_Char_String *const self, char *const key, String *const value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "value", (void*) value);

    /* No null-key check here: the copying constructor carries a stored nullptr key through
     * unchanged, and a nullptr is a legal element. The public add refuses one. */
    USize const key_before = al_char_get_size(&self->key);
    USize const value_before = al_string_get_size(&self->value);

    al_char_add_last(&self->key, key);
    al_string_add_last(&self->value, value);

    /* Both add_last calls DECLINE rather than grow when the allocator refuses, and the two
     * can decline independently. Leaving a lone orphan behind mis-pairs the NEXT successful
     * add (key at N+1 against value at N), so a lookup returns another entry's value. Roll
     * the odd one out so the decline means exactly "nothing was taken". */
    if (al_char_get_size(&self->key) != key_before + 1 || al_string_get_size(&self->value) != value_before + 1) {
        /* NEITHER removal may release its element. add_last stored the caller's key pointer
         * verbatim and a SHALLOW STRUCT COPY of the caller's String - and the source has not
         * been vacated yet, so an owning removal here would free a buffer the caller still
         * claims.
         *
         * The value side matters as much as the key side: al_string_remove calls
         * string_uninit on the stored element, which releases when `owned` is set.
         * Overwriting the slot with an empty String first makes that a no-op -
         * string_init_1 holds no buffer and is not an owner. */
        if (al_char_get_size(&self->key) == key_before + 1) {
            al_char_get_data(&self->key)[key_before] = nullptr;
            al_char_remove(&self->key, key_before);
        }

        if (al_string_get_size(&self->value) == value_before + 1) {
            al_string_get_data(&self->value)[value_before] = string_init_1();
            al_string_remove(&self->value, value_before);
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
 * @note KEY ONLY. A value is moved rather than copied by add, and deep-copied by init_3
 *       through string_init_6, which allocates for itself - so there is no value counterpart
 *       and no allocator question to get wrong.
 */
static char* _map_char_string_copy(AL_Char const *const list, char const *const data, USize const data_size) {
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
static void _map_char_string_release(AL_Char const *const list, char *const data) {
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
 * @brief Deep-copy the first `size` pairs of two lists into an empty map.
 * @return true when every pair landed; false when a copy or an append was declined, having
 *         released only the pair in flight - the CALLER empties the map.
 * @note The values go through string_init_6 / string_alloc_init_6, which build an OWNING
 *       copy. A source VIEW is therefore PROMOTED to an owner here, which the header calls
 *       out: it is the right default for a constructor whose contract is that the caller
 *       keeps everything, but it is the one way init_3 differs from add in more than depth.
 */
static bool _map_char_string_fill(Map_Char_String *const self, AL_Char const *const keys, AL_String const *const values, USize const size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "keys", (void*) keys);
    error_check_null(LOG_METADATA, "values", (void*) values);

    for (USize i = 0; i < size; i += 1) {
        char const *const key = al_char_at(keys, i);
        String const *const value = al_string_at(values, i);

        char *key_copy = nullptr;

        /* A stored nullptr key is a legal element, and copies as itself. */
        if (!memory_empty((void*) key)) {
            key_copy = _map_char_string_copy(&self->key, key, char_length(key));

            if (memory_empty((void*) key_copy)) {
                trace_log_pop();

                return false;
            }
        }

#ifdef ARENA_IMPLEMENTATION
        /* FAMILY IDIOM: pick the copy's constructor by what the KEY LIST's allocator actually
         * holds, not by which family reads better. A heap-built map carries a NULL allocator,
         * and string_alloc_init_6 would take that as its arena. map_char_al_char.c makes the
         * same choice for the same reason. */
        String value_copy = memory_empty((void*) self->key.allocator)
            ? string_init_6(value)
            : string_alloc_init_6(value, self->key.allocator);
#else
        String value_copy = string_init_6(value);
#endif // ARENA_IMPLEMENTATION

        /* A declined borrow inside the copy shows up as a short result. Give back what this
         * frame took - the copy, which nothing else can reach, and the key. */
        if (string_get_size(&value_copy) != string_get_size(value)) {
            string_uninit(&value_copy);
            _map_char_string_release(&self->key, key_copy);

            trace_log_pop();

            return false;
        }

        if (!_map_char_string_append(self, key_copy, &value_copy)) {
            string_uninit(&value_copy);
            _map_char_string_release(&self->key, key_copy);

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
static bool _map_char_string_index(Map_Char_String const *const self, char const *const key, USize const key_size, USize *const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "key", (void*) key);
    error_check_null(LOG_METADATA, "index", (void*) index);

    USize const size = map_char_string_get_size(self);

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
Map_Char_String map_char_string_alloc_init_1(Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    Map_Char_String map_char_string = DEFAULT_INITIALIZATION;

    map_char_string.key = al_char_alloc_init_1(allocator);
    map_char_string.value = al_string_alloc_init_1(allocator);

    trace_log_pop();

    return map_char_string;
}

Map_Char_String map_char_string_alloc_init_2(USize const capacity, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    Map_Char_String map_char_string = DEFAULT_INITIALIZATION;

    map_char_string.key = al_char_alloc_init_2(capacity, allocator);
    map_char_string.value = al_string_alloc_init_2(capacity, allocator);

    trace_log_pop();

    return map_char_string;
}

Map_Char_String map_char_string_alloc_init_3(AL_Char const *const keys, AL_String const *const values, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "keys", (void*) keys);
    error_check_null(LOG_METADATA, "values", (void*) values);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    USize const key_size = al_char_get_size(keys);
    USize const value_size = al_string_get_size(values);
    /* The SHORTER of the two: a mismatched pair would otherwise claim entries the value
     * list does not have. */
    USize const size = key_size < value_size ? key_size : value_size;

    if (size == 0) {
        Map_Char_String const map_char_string = map_char_string_alloc_init_1(allocator);

        trace_log_pop();

        return map_char_string;
    }

    Map_Char_String map_char_string = map_char_string_alloc_init_2(size, allocator);

    if (!_map_char_string_fill(&map_char_string, keys, values, size)) {
        map_char_string_uninit(&map_char_string);

        map_char_string = map_char_string_alloc_init_1(allocator);
    }

    trace_log_pop();

    return map_char_string;
}

Map_Char_String* map_char_string_alloc_new_1(Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    /* try_borrow: allocator_borrow reaches arena_linear_alloc, which ABORTS on exhaustion,
     * so a live-but-full arena would kill the process here while a refused one declined. */
    Map_Char_String *const map_char_string = (Map_Char_String*) allocator_try_borrow(sizeof(Map_Char_String), allocator);

    if (memory_empty((void*) map_char_string)) {
        trace_log_pop();

        return nullptr;
    }

    *map_char_string = map_char_string_alloc_init_1(allocator);

    trace_log_pop();

    return map_char_string;
}

Map_Char_String* map_char_string_alloc_new_2(USize const capacity, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    Map_Char_String *const map_char_string = (Map_Char_String*) allocator_try_borrow(sizeof(Map_Char_String), allocator);

    if (memory_empty((void*) map_char_string)) {
        trace_log_pop();

        return nullptr;
    }

    *map_char_string = map_char_string_alloc_init_2(capacity, allocator);

    trace_log_pop();

    return map_char_string;
}

Map_Char_String* map_char_string_alloc_new_3(AL_Char const *const keys, AL_String const *const values, Arena *allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "keys", (void*) keys);
    error_check_null(LOG_METADATA, "values", (void*) values);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    Map_Char_String *const map_char_string = (Map_Char_String*) allocator_try_borrow(sizeof(Map_Char_String), allocator);

    if (memory_empty((void*) map_char_string)) {
        trace_log_pop();

        return nullptr;
    }

    *map_char_string = map_char_string_alloc_init_3(keys, values, allocator);

    trace_log_pop();

    return map_char_string;
}
#endif // ARENA_IMPLEMENTATION

/*==============================================================================
 * MARK: - Element Access
 *============================================================================*/

String* map_char_string_at_1(Map_Char_String const *const self, char const *const key) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "key", (void*) key);

    String *const value = map_char_string_at_2(self, key, char_length(key));

    trace_log_pop();

    return value;
}

String* map_char_string_at_2(Map_Char_String const *const self, char const *const key, USize const key_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "key", (void*) key);

    USize index = 0;

    /* No guard on key_size. An empty key is a legal key, and the abort that used to stand
     * here made "=x" in a parsed request body a remote process kill. */
    if (!_map_char_string_index(self, key, key_size, &index)) {
        trace_log_pop();

        return nullptr;
    }

    String *const value = al_string_at(&self->value, index);

    trace_log_pop();

    return value;
}

bool map_char_string_contains_1(Map_Char_String const *const self, char const *const key) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "key", (void*) key);

    bool const found = map_char_string_contains_2(self, key, char_length(key));

    trace_log_pop();

    return found;
}

bool map_char_string_contains_2(Map_Char_String const *const self, char const *const key, USize const key_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "key", (void*) key);

    USize index = 0;
    bool const found = _map_char_string_index(self, key, key_size, &index);

    trace_log_pop();

    return found;
}

bool map_char_string_empty(Map_Char_String const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    bool const empty = map_char_string_get_size(self) == 0;

    trace_log_pop();

    return empty;
}

USize map_char_string_get_capacity(Map_Char_String const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    USize const key_capacity = al_char_get_capacity(&self->key);
    USize const value_capacity = al_string_get_capacity(&self->value);

    /* The SMALLER: a pair needs a slot in both lists, so the larger capacity is room the
     * map cannot actually use. */
    trace_log_pop();

    return key_capacity < value_capacity ? key_capacity : value_capacity;
}

char* map_char_string_get_key(Map_Char_String const *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    USize const size = map_char_string_get_size(self);

    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "size", size, "index >= size", index >= size);

    char *const key = al_char_at(&self->key, index);

    trace_log_pop();

    return key;
}

AL_Char* map_char_string_get_keys(Map_Char_String *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return &self->key;
}

USize map_char_string_get_size(Map_Char_String const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    USize const key_size = al_char_get_size(&self->key);
    USize const value_size = al_string_get_size(&self->value);

    /* DERIVED, not stored. A third counter beside the lists' own two could disagree with
     * them - and did, through get_keys/get_values, which hand out mutable handles the map
     * cannot observe. */
    trace_log_pop();

    return key_size < value_size ? key_size : value_size;
}

String* map_char_string_get_value(Map_Char_String const *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    USize const size = map_char_string_get_size(self);

    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "size", size, "index >= size", index >= size);

    String *const value = al_string_at(&self->value, index);

    trace_log_pop();

    return value;
}

AL_String* map_char_string_get_values(Map_Char_String *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return &self->value;
}

/*==============================================================================
 * MARK: - Heap Constructors
 *============================================================================*/

Map_Char_String map_char_string_init_1(void) {
    trace_log_push(LOG_METADATA);

    Map_Char_String map_char_string = DEFAULT_INITIALIZATION;

    /* Built through the lists' own constructors rather than left at the all-zero state:
     * that guarantee is each list's to make, and made here it holds in one place. */
    map_char_string.key = al_char_init_1();
    map_char_string.value = al_string_init_1();

    trace_log_pop();

    return map_char_string;
}

Map_Char_String map_char_string_init_2(USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    Map_Char_String map_char_string = DEFAULT_INITIALIZATION;

    map_char_string.key = al_char_init_2(capacity);
    map_char_string.value = al_string_init_2(capacity);

    trace_log_pop();

    return map_char_string;
}

Map_Char_String map_char_string_init_3(AL_Char const *const keys, AL_String const *const values) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "keys", (void*) keys);
    error_check_null(LOG_METADATA, "values", (void*) values);

    USize const key_size = al_char_get_size(keys);
    USize const value_size = al_string_get_size(values);
    USize const size = key_size < value_size ? key_size : value_size;

    if (size == 0) {
        Map_Char_String const map_char_string = map_char_string_init_1();

        trace_log_pop();

        return map_char_string;
    }

    Map_Char_String map_char_string = map_char_string_init_2(size);

    if (!_map_char_string_fill(&map_char_string, keys, values, size)) {
        map_char_string_uninit(&map_char_string);

        map_char_string = map_char_string_init_1();
    }

    trace_log_pop();

    return map_char_string;
}

Map_Char_String* map_char_string_new_1(void) {
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    Map_Char_String *const map_char_string = (Map_Char_String*) allocator_try_borrow(sizeof(Map_Char_String), nullptr);
#else
    Map_Char_String *const map_char_string = (Map_Char_String*) allocator_try_borrow(sizeof(Map_Char_String));
#endif // ARENA_IMPLEMENTATION

    if (memory_empty((void*) map_char_string)) {
        trace_log_pop();

        return nullptr;
    }

    *map_char_string = map_char_string_init_1();

    trace_log_pop();

    return map_char_string;
}

Map_Char_String* map_char_string_new_2(USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

#ifdef ARENA_IMPLEMENTATION
    Map_Char_String *const map_char_string = (Map_Char_String*) allocator_try_borrow(sizeof(Map_Char_String), nullptr);
#else
    Map_Char_String *const map_char_string = (Map_Char_String*) allocator_try_borrow(sizeof(Map_Char_String));
#endif // ARENA_IMPLEMENTATION

    if (memory_empty((void*) map_char_string)) {
        trace_log_pop();

        return nullptr;
    }

    *map_char_string = map_char_string_init_2(capacity);

    trace_log_pop();

    return map_char_string;
}

Map_Char_String* map_char_string_new_3(AL_Char const *const keys, AL_String const *const values) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "keys", (void*) keys);
    error_check_null(LOG_METADATA, "values", (void*) values);

#ifdef ARENA_IMPLEMENTATION
    Map_Char_String *const map_char_string = (Map_Char_String*) allocator_try_borrow(sizeof(Map_Char_String), nullptr);
#else
    Map_Char_String *const map_char_string = (Map_Char_String*) allocator_try_borrow(sizeof(Map_Char_String));
#endif // ARENA_IMPLEMENTATION

    if (memory_empty((void*) map_char_string)) {
        trace_log_pop();

        return nullptr;
    }

    *map_char_string = map_char_string_init_3(keys, values);

    trace_log_pop();

    return map_char_string;
}

/*==============================================================================
 * MARK: - Insertion
 *============================================================================*/

bool map_char_string_add(Map_Char_String *const self, char *const key, String **const value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "key", (void*) key);
    error_check_null(LOG_METADATA, "value", (void*) value);
    error_check_null(LOG_METADATA, "*value", (void*) *value);

    if (!_map_char_string_append(self, key, *value)) {
        trace_log_pop();

        return false;
    }

    /* Vacated only AFTER both halves landed. The order is the whole contract: a decline has
     * to leave the caller's String holding its buffer, and a success has to leave exactly
     * one claimant. */
    _map_char_string_vacate(*value);

    *value = nullptr;

    trace_log_pop();

    return true;
}

bool map_char_string_add_static(Map_Char_String *const self, char const *const key, String **const value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "key", (void*) key);

    bool const added = map_char_string_add_static_2(self, key, char_length(key), value);

    trace_log_pop();

    return added;
}

bool map_char_string_add_static_2(Map_Char_String *const self, char const *const key, USize const key_size, String **const value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "key", (void*) key);
    error_check_null(LOG_METADATA, "value", (void*) value);
    error_check_null(LOG_METADATA, "*value", (void*) *value);

    char *const key_copy = _map_char_string_copy(&self->key, key, key_size);

    if (memory_empty((void*) key_copy)) {
        trace_log_pop();

        return false;
    }

    if (!_map_char_string_append(self, key_copy, *value)) {
        _map_char_string_release(&self->key, key_copy);

        trace_log_pop();

        return false;
    }

    _map_char_string_vacate(*value);

    *value = nullptr;

    trace_log_pop();

    return true;
}

/*==============================================================================
 * MARK: - Removal and Capacity
 *============================================================================*/

void map_char_string_clear(Map_Char_String *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    al_char_clear(&self->key);
    al_string_clear(&self->value);

    trace_log_pop();
}

void map_char_string_delete(Map_Char_String **const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "*self", (void*) *self);

#ifdef ARENA_IMPLEMENTATION
    /* Read before uninit because the read belongs next to its use. The KEY side is the one
     * consulted, which is exact for every map this file builds. */
    Arena *const allocator = (*self)->key.allocator;
#endif // ARENA_IMPLEMENTATION

    map_char_string_uninit(*self);

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

bool map_char_string_remove_1(Map_Char_String *const self, char const *const key) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "key", (void*) key);

    bool const removed = map_char_string_remove_2(self, key, char_length(key));

    trace_log_pop();

    return removed;
}

bool map_char_string_remove_2(Map_Char_String *const self, char const *const key, USize const key_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "key", (void*) key);

    USize index = 0;

    if (!_map_char_string_index(self, key, key_size, &index)) {
        trace_log_pop();

        return false;
    }

    /* Both removals RELEASE, which is correct here and wrong in _map_char_string_append: a
     * stored pair belongs to the map, an unrolled append never did. The value release is
     * conditional on its own `owned`, so a stored view is dropped rather than freed. The
     * index came from get_size, the smaller of the two, so it is in bounds for both. */
    al_char_remove(&self->key, index);
    al_string_remove(&self->value, index);

    trace_log_pop();

    return true;
}

void map_char_string_remove_at(Map_Char_String *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    USize const size = map_char_string_get_size(self);

    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "size", size, "index >= size", index >= size);

    al_char_remove(&self->key, index);
    al_string_remove(&self->value, index);

    trace_log_pop();
}

void map_char_string_reserve(Map_Char_String *const self, USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    al_char_reserve(&self->key, capacity);
    al_string_reserve(&self->value, capacity);

    trace_log_pop();
}

void map_char_string_shrink(Map_Char_String *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    al_char_shrink(&self->key);
    al_string_shrink(&self->value);

    trace_log_pop();
}

void map_char_string_uninit(Map_Char_String *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    al_char_uninit(&self->key);
    al_string_uninit(&self->value);

    trace_log_pop();
}