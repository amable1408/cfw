// str.c - Implementation of the Str container for the C Libraries Framework
// See str.h for API documentation.

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/
#include <container/str/str.h>

/*==============================================================================
 * MARK: - Macros and Constants
 *============================================================================*/
/* Sign, 20 integer digits, the point, 19 fraction digits and the terminator: 42, rounded up. */
#define _STR_FROM_NUMBERS_FLOAT_BUFFER_SIZE 48

/*==============================================================================
 * MARK: - Static/Internal Functions
 *============================================================================*/
#ifdef ARENA_IMPLEMENTATION
static Str _str_from_numbers_int(ISize const number, U8 const padding, Arena *const allocator)
#else
static Str _str_from_numbers_int(ISize const number, U8 const padding)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    /* char renders the digits (sign, additive padding, magnitude) and str adopts the buffer -
     * the str_from_trim shape - where two hand-rolled copies of the renderer used to live. A
     * nullptr allocator is the heap path, which aborts inside the borrow on OOM (the closed
     * abort class); a refused arena answers null and degrades to the EMPTY Str carrying it. */
#ifdef ARENA_IMPLEMENTATION
    char *const buffer = memory_empty(allocator) ? char_new_from_numbers_int_2(number, padding) : char_alloc_from_numbers_int_2(number, padding, allocator);
    Str str = memory_empty(allocator) ? str_init_1() : str_alloc_init_1(allocator);
#else
    char *const buffer = char_new_from_numbers_int_2(number, padding);
    Str str = str_init_1();
#endif // ARENA_IMPLEMENTATION

    if (!memory_empty(buffer)) {
        str.data = buffer;
        str.size = char_length(buffer);
        str.owned = true;
    }

    trace_log_pop();

    return str;
}

#ifdef ARENA_IMPLEMENTATION
static Str _str_from_numbers_uint(USize const number, U8 const padding, Arena *const allocator)
#else
static Str _str_from_numbers_uint(USize const number, U8 const padding)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    /* See _str_from_numbers_int. */
#ifdef ARENA_IMPLEMENTATION
    char *const buffer = memory_empty(allocator) ? char_new_from_numbers_uint_2(number, padding) : char_alloc_from_numbers_uint_2(number, padding, allocator);
    Str str = memory_empty(allocator) ? str_init_1() : str_alloc_init_1(allocator);
#else
    char *const buffer = char_new_from_numbers_uint_2(number, padding);
    Str str = str_init_1();
#endif // ARENA_IMPLEMENTATION

    if (!memory_empty(buffer)) {
        str.data = buffer;
        str.size = char_length(buffer);
        str.owned = true;
    }

    trace_log_pop();

    return str;
}

#ifdef ARENA_IMPLEMENTATION
static Str _str_init(Arena *const allocator)
#else
static Str _str_init(void)
#endif // ARENA_IMPLEMENTATION
{
    Str str = DEFAULT_INITIALIZATION;

#ifdef ARENA_IMPLEMENTATION
    if (!memory_empty(allocator)) {
        str.allocator = allocator;
    }
#endif // ARENA_IMPLEMENTATION

    return str;
}

/* The EMPTY value carrying self's allocator: what every producer of nothing answers, so a
 * later append on it stays in-arena. */
static Str _str_init_like(Str const *const self) {
#ifdef ARENA_IMPLEMENTATION
    Str const str = _str_init(self->allocator);
#else
    Str const str = _str_init();
#endif // ARENA_IMPLEMENTATION

    return str;
}

/* An OWNED copy of data carrying self's allocator (str_slice_range's shape); the EMPTY value
 * for a zero size or a refused arena. */
static Str _str_init_static_like(Str const *const self, char const *const data, USize const data_size) {
#ifdef ARENA_IMPLEMENTATION
    Str const str = memory_empty(self->allocator) ? str_init_static(data, data_size) : str_alloc_init_static(data, data_size, self->allocator);
#else
    Str const str = str_init_static(data, data_size);
#endif // ARENA_IMPLEMENTATION

    return str;
}

#ifdef ARENA_IMPLEMENTATION
static Str _str_join(Str const *const *const parts, USize const count, char const *const separator, USize const separator_size, Arena *const allocator)
#else
static Str _str_join(Str const *const *const parts, USize const count, char const *const separator, USize const separator_size)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    /* parts may be nullptr when there is nothing to join (count == 0). */
    error_check_null(LOG_METADATA, "parts", count > 0 ? (void const*) parts : (void const*) 1);
    error_check_null(LOG_METADATA, "separator", (void*) separator);

    /* Joining 0 parts yields the EMPTY result. */
#ifdef ARENA_IMPLEMENTATION
    Str str = _str_init(allocator);
#else
    Str str = _str_init();
#endif // ARENA_IMPLEMENTATION

    /* One borrow sized from the parts instead of an exact-fit reallocation per append: k parts
     * used to cost k borrows and O(k * total) copies, and left k dead blocks on a linear arena.
     * An overflowing total, or a refused arena, refuses the WHOLE join to the EMPTY Str - never
     * a truncated one that reads as success (String's join, R2). */
    bool overflow = count > 1 && separator_size > 0 && count - 1 > (USIZE_MAX - CHAR_END_CHARACTER) / separator_size;
    USize total = count > 1 && !overflow ? (count - 1) * separator_size : 0;

    for (USize i = 0; i < count && !overflow; i += 1) {
        error_check_null(LOG_METADATA, "parts[i]", (void*) parts[i]);

        overflow = parts[i]->size > USIZE_MAX - CHAR_END_CHARACTER - total;
        total += overflow ? 0 : parts[i]->size;
    }

    if (overflow || total == 0) {
        trace_log_pop();

        return str;
    }

#ifdef ARENA_IMPLEMENTATION
    char *const buffer = (char*) allocator_borrow(total + CHAR_END_CHARACTER, allocator);
#else
    char *const buffer = (char*) allocator_borrow(total + CHAR_END_CHARACTER);
#endif // ARENA_IMPLEMENTATION

    /* Null only from a REFUSED arena. */
    if (memory_empty(buffer)) {
        trace_log_pop();

        return str;
    }

    USize index = 0;

    for (USize i = 0; i < count; i += 1) {
        if (parts[i]->size > 0) {
            char_copy_2(buffer + index, parts[i]->data, parts[i]->size);

            index += parts[i]->size;
        }

        if (separator_size > 0 && i + 1 < count) {
            char_copy_2(buffer + index, separator, separator_size);

            index += separator_size;
        }
    }

    buffer[total] = '\0';

    str.data = buffer;
    str.size = total;
    str.owned = true;

    trace_log_pop();

    return str;
}

#ifdef ARENA_IMPLEMENTATION
static Str* _str_new(Arena *const allocator)
#else
static Str* _str_new(void)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    Str *const str = (Str*) allocator_borrow(sizeof(Str), allocator);
#else
    Str *const str = (Str*) allocator_borrow(sizeof(Str));
#endif // ARENA_IMPLEMENTATION

    /* Null only from a REFUSED arena (the heap path aborts instead of returning
     * null) - the sixth borrow site of the class this round closed, and the one
     * that borrowed a STRUCT rather than a value buffer. Propagate the nullptr. */
    if (memory_empty(str)) {
        trace_log_pop();

        return nullptr;
    }

#ifdef ARENA_IMPLEMENTATION
    *str = _str_init(allocator);
#else
    *str = _str_init();
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return str;
}

/* allocator is always self->allocator today; the parameter mirrors _str_init's. */
#ifdef ARENA_IMPLEMENTATION
static void _str_uninit(Str *const self, Arena *const allocator)
#else
static void _str_uninit(Str *const self)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* Keyed on the explicit ownership flag, which is the only thing that can tell an
     * OWNER from a VIEW. Keying on size or on data != nullptr cannot: a view over a
     * string literal satisfies both, and allocator_release reaches raw free(), so
     * either would hand .rodata to the allocator. An owned buffer of size 0 is a
     * normal state here and is released correctly. */
    if (self->owned && !memory_empty(self->data)) {
#ifdef ARENA_IMPLEMENTATION
        allocator_release(self->data, allocator);
#else
        allocator_release(self->data);
#endif // ARENA_IMPLEMENTATION
    }

    /* Cleared on BOTH paths, matching _string_uninit: leaving a released view holding a
     * live pointer means str_get_data still hands back borrowed memory that may already
     * be gone - a use-after-free read through an object the caller believes is dead. */
    self->data = nullptr;
    self->size = 0;
    self->owned = false;

    trace_log_pop();
}

/*==============================================================================
 * MARK: - Arena-backed API
 *============================================================================*/
#ifdef ARENA_IMPLEMENTATION
Str str_alloc_format(Arena *const allocator, char const *const format, ...) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);
    error_check_null(LOG_METADATA, "format", (void*) format);

    va_list args;
    va_start(args, format);

    va_list args_copy;
    va_copy(args_copy, args);

    int const needed = vsnprintf(nullptr, 0, format, args_copy);

    va_end(args_copy);

    Str str = str_alloc_init_1(allocator);

    /* An encoding error is a bug in the format, not an allocator event, so it WARNs like the
     * heap twin's; a refused arena stays silent, as every arena refusal in this module does. */
    if (needed < 0) {
        log_message_try_1(LOG_LEVEL_WARN, "str: format \"%s\" could not be rendered (encoding error)\n", format);
    }
    else if (needed > 0) {
        USize const size = (USize) needed;
        char *const buffer = (char*) allocator_borrow(size + CHAR_END_CHARACTER, allocator);

        /* Null only from a REFUSED arena; the empty Str is the degraded result. */
        if (!memory_empty(buffer)) {
            vsnprintf(buffer, size + CHAR_END_CHARACTER, format, args);

            str.data = buffer;
            str.size = size;
            str.owned = true;
        }
    }

    va_end(args);

    trace_log_pop();

    return str;
}

Str str_alloc_from_numbers_float_1(FSize const number, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    char buffer[_STR_FROM_NUMBERS_FLOAT_BUFFER_SIZE] = DEFAULT_INITIALIZATION;

    char_from_numbers_float_1(buffer, sizeof(buffer), number);

    Str const str = str_alloc_init_static(buffer, char_length(buffer), allocator);

    trace_log_pop();

    return str;
}

Str str_alloc_from_numbers_float_2(FSize const number, U8 const precision, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    char buffer[_STR_FROM_NUMBERS_FLOAT_BUFFER_SIZE] = DEFAULT_INITIALIZATION;

    char_from_numbers_float_2(buffer, sizeof(buffer), number, precision);

    Str const str = str_alloc_init_static(buffer, char_length(buffer), allocator);

    trace_log_pop();

    return str;
}

Str str_alloc_from_numbers_int_1(ISize const number, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    Str const str = str_alloc_from_numbers_int_2(number, 0, allocator);

    trace_log_pop();

    return str;
}

Str str_alloc_from_numbers_int_2(ISize const number, U8 const padding, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    Str const str = _str_from_numbers_int(number, padding, allocator);

    trace_log_pop();

    return str;
}

Str str_alloc_from_numbers_uint_1(USize const number, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    Str const str = str_alloc_from_numbers_uint_2(number, 0, allocator);

    trace_log_pop();

    return str;
}

Str str_alloc_from_numbers_uint_2(USize const number, U8 const padding, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    Str const str = _str_from_numbers_uint(number, padding, allocator);

    trace_log_pop();

    return str;
}

Str str_alloc_from_replace_1(Str const *const self, char const *const find, char const *const replace, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "find", (void*) find);
    error_check_null(LOG_METADATA, "replace", (void*) replace);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    Str const str = str_alloc_from_replace_2(self, find, char_length(find), replace, char_length(replace), allocator);

    trace_log_pop();

    return str;
}

Str str_alloc_from_replace_2(Str const *const self, char const *const find, USize const find_size, char const *const replace, USize const replace_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "find", (void*) find);
    error_check_null(LOG_METADATA, "replace", (void*) replace);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    /* Nothing in an empty Str can be replaced, and char_alloc_new_replace_2
     * rejects the nullptr one carries. An empty find pattern is not settled here:
     * it reaches the replace helper, which answers it with a verbatim copy. */
    if (self->size == 0) {
        Str const empty = str_alloc_init_1(allocator);

        trace_log_pop();

        return empty;
    }

    char *const data = char_alloc_new_replace_2(self->data, self->size, find, find_size, replace, replace_size, allocator);
    Str buffer = str_alloc_init_1(allocator);

    /* A refused arena answers null: the EMPTY Str. An all-consumed replacement ("abc" -> "")
     * answers the EMPTY Str too, its one-byte block released - the heap twin's answer (see
     * str_from_replace_2), so the twins agree on nothing whatever the allocator. */
    if (!memory_empty(data)) {
        USize const data_size = char_length(data);

        if (data_size == 0) {
            allocator_release(data, allocator);
        }
        else {
            buffer.data = data;
            buffer.size = data_size;
            buffer.owned = true;
        }
    }

    trace_log_pop();

    return buffer;
}

Str str_alloc_from_trim(Str const *const self, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    /* Trimming an empty Str is empty; char_alloc_from_trim_2 rejects the nullptr it carries.
     * Mirrors str_from_trim, and so does the all-whitespace answer below: the twins agree on
     * nothing for the empty source AND for a source that trims away entirely. */
    if (self->size == 0) {
        Str const empty = str_alloc_init_1(allocator);

        trace_log_pop();

        return empty;
    }

    char *const data = char_alloc_from_trim_2(self->data, self->size, allocator);
    Str buffer = str_alloc_init_1(allocator);

    /* Pure whitespace trims to nothing: the EMPTY Str, the block released (see str_from_trim). */
    if (!memory_empty(data)) {
        USize const data_size = char_length(data);

        if (data_size == 0) {
            allocator_release(data, allocator);
        }
        else {
            buffer.data = data;
            buffer.size = data_size;
            buffer.owned = true;
        }
    }

    trace_log_pop();

    return buffer;
}

Str str_alloc_init_1(Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    Str const str = _str_init(allocator);

    trace_log_pop();

    return str;
}

Str str_alloc_init_2(char *const data, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    Str const str = str_alloc_init_3(data, char_length(data), allocator);

    trace_log_pop();

    return str;
}

Str str_alloc_init_3(char *const data, USize const data_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    /* A zero size yields a valid EMPTY Str rather than aborting - mirrors str_init_3. */
    Str str = str_alloc_init_1(allocator);

    /* A VIEW over caller memory - see the ownership note on Str in str.h. */
    str.data = data;
    str.size = data_size;
    str.owned = false;

    trace_log_pop();

    return str;
}

Str str_alloc_init_4(Str const *const data, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    /* An empty data carries data->data == nullptr, which str_alloc_init_static
     * still (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    Str const str = str_alloc_init_static(data_buffer, data->size, allocator);

    trace_log_pop();

    return str;
}

Str str_alloc_init_static(char const *const data, USize const data_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    /* Mirrors str_init_static: an empty source yields the plain empty Str. */
    if (data_size == 0) {
        Str const empty = str_alloc_init_1(allocator);

        trace_log_pop();

        return empty;
    }

    char *const char_new = (char*) allocator_borrow(data_size + CHAR_END_CHARACTER, allocator);

    /* Null only from a REFUSED arena; degrade to the empty Str. */
    if (memory_empty(char_new)) {
        Str const empty = str_alloc_init_1(allocator);

        trace_log_pop();

        return empty;
    }

    char_copy_3(char_new, data_size + CHAR_END_CHARACTER, data, data_size);

    char_new[data_size] = '\0';

    Str str = str_alloc_init_3(char_new, data_size, allocator);

    /* A COPY of the source, so this Str owns char_new. */
    str.owned = true;

    trace_log_pop();

    return str;
}

Str str_alloc_join_1(Str const *const *const parts, USize const count, char const *const separator, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    Str const str = str_alloc_join_2(parts, count, separator, char_length(separator), allocator);

    trace_log_pop();

    return str;
}

Str str_alloc_join_2(Str const *const *const parts, USize const count, char const *const separator, USize const separator_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    Str const str = _str_join(parts, count, separator, separator_size, allocator);

    trace_log_pop();

    return str;
}

Str* str_alloc_new_1(Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    Str const *const str = _str_new(allocator);

    trace_log_pop();

    return (Str*) str;
}

Str* str_alloc_new_2(char *const data, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    Str const *const str = str_alloc_new_3(data, char_length(data), allocator);

    trace_log_pop();

    return (Str*) str;
}

Str* str_alloc_new_3(char *const data, USize const data_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    Str *const str = str_alloc_new_1(allocator);

    /* A refused arena propagates as nullptr from the struct borrow. */
    if (memory_empty(str)) {
        trace_log_pop();

        return nullptr;
    }

    *str = str_alloc_init_3(data, data_size, allocator);

    trace_log_pop();

    return str;
}

Str* str_alloc_new_4(Str const *const data, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    Str *const str = str_alloc_new_1(allocator);

    /* A refused arena propagates as nullptr from the struct borrow. */
    if (memory_empty(str)) {
        trace_log_pop();

        return nullptr;
    }

    *str = str_alloc_init_4(data, allocator);

    trace_log_pop();

    return str;
}

Str* str_alloc_new_static(char const *const data, USize const data_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    Str *const str = str_alloc_new_1(allocator);

    /* A refused arena propagates as nullptr from the struct borrow. */
    if (memory_empty(str)) {
        trace_log_pop();

        return nullptr;
    }

    *str = str_alloc_init_static(data, data_size, allocator);

    trace_log_pop();

    return str;
}

void str_alloc_trim(Str *const self, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    /* The object's home never changes - str_delete routes the STRUCT by this field, the
     * str_move_3 rule - so a trim into another arena is REFUSED as a no-op. (Through str_trim
     * the argument is always self's own.) */
    if (allocator != self->allocator) {
        trace_log_pop();

        return;
    }

    /* The leaf is called directly so its nullptr is unambiguous: a REFUSED arena keeps self
     * exactly as it was (the old route through str_alloc_from_trim wiped it to EMPTY). A
     * source that trims away entirely becomes the EMPTY Str, its allocator kept. */
    if (self->size == 0) {
        trace_log_pop();

        return;
    }

    char *const data = char_alloc_from_trim_2(self->data, self->size, allocator);

    if (memory_empty(data)) {
        trace_log_pop();

        return;
    }

    USize const data_size = char_length(data);

    str_uninit(self);

    if (data_size == 0) {
        allocator_release(data, allocator);

        *self = str_alloc_init_1(allocator);
    }
    else {
        self->data = data;
        self->size = data_size;
        self->owned = true;
    }

    trace_log_pop();
}
#endif // ARENA_IMPLEMENTATION

/*==============================================================================
 * MARK: - Standard API
 *============================================================================*/
void str_add_1(Str *const self, char const *const data, USize const index) {
    trace_log_push(LOG_METADATA);

    str_add_2(self, data, char_length(data), index);

    trace_log_pop();
}

void str_add_2(Str *const self, char const *const data, USize const data_size, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* index routinely arrives from data (an unchecked find result - CHAR_NPOS - or
     * a parsed offset), so past-the-end refuses in every build; the error_check
     * that stood here aborted, and in an unchecked build the splice loops below
     * wrote unbounded. index == size (append at the end) stays legal. */
    if (index > self->size) {
        trace_log_pop();

        return;
    }

    /* Appending nothing is a no-op, not an error. */
    if (data_size == 0) {
        trace_log_pop();

        return;
    }

    /* Parity with string_add_2's overflow guard: a data_size near USIZE_MAX wraps
     * the borrow request small and the copy loops long. The size is a VALUE, so
     * this refuses in every build. */
    if (data_size > USIZE_MAX - self->size - CHAR_END_CHARACTER) {
        trace_log_pop();

        return;
    }

    char const *const buffer = self->data;
    USize buffer_index = 0;
    /* Captured BEFORE the flag is overwritten: the release at the end of this function
     * must know whether the OLD buffer was ours. Without it, appending to a view frees
     * the caller's memory - a string literal in the worst case. */
    bool const buffer_owned = self->owned;

#ifdef ARENA_IMPLEMENTATION
    char *const grown = (char*) allocator_borrow(self->size + data_size + CHAR_END_CHARACTER, self->allocator);
#else
    char *const grown = (char*) allocator_borrow(self->size + data_size + CHAR_END_CHARACTER);
#endif // ARENA_IMPLEMENTATION

    /* Null only from a REFUSED arena. self still holds its old buffer untouched at
     * this point, so refusing here leaves the object fully intact - assigning first
     * (as this code once did) would wipe the object and then copy through null. */
    if (memory_empty(grown)) {
        trace_log_pop();

        return;
    }

    self->data = grown;
    self->owned = true;

    for (; buffer_index < index; buffer_index += 1) {
        self->data[buffer_index] = buffer[buffer_index];
    }

    for (USize i = 0; i < data_size; i += 1, buffer_index += 1) {
        self->data[buffer_index] = data[i];
    }

    for (USize i = index; i < self->size; i += 1, buffer_index += 1) {
        self->data[buffer_index] = buffer[i];
    }

    self->size += data_size;
    self->data[self->size] = '\0';

    if (buffer_owned && !memory_empty(buffer)) {
#ifdef ARENA_IMPLEMENTATION
        allocator_release((char*) buffer, self->allocator);
#else
        allocator_release((char*) buffer);
#endif // ARENA_IMPLEMENTATION
    }

    trace_log_pop();
}

void str_add_3(Str *const self, Str const *const data, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which str_add_2 still (correctly)
     * rejects; substitute a valid empty source so an empty add stays a no-op. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    str_add_2(self, data_buffer, data->size, index);

    trace_log_pop();
}

void str_add_first_1(Str *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    str_add_1(self, data, 0);

    trace_log_pop();
}

void str_add_first_2(Str *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    str_add_2(self, data, data_size, 0);

    trace_log_pop();
}

void str_add_first_3(Str *const self, Str const *const data) {
    trace_log_push(LOG_METADATA);

    str_add_3(self, data, 0);

    trace_log_pop();
}

void str_add_last_1(Str *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    str_add_1(self, data, self->size);

    trace_log_pop();
}

void str_add_last_2(Str *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    str_add_2(self, data, data_size, self->size);

    trace_log_pop();
}

void str_add_last_3(Str *const self, Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    str_add_3(self, data, self->size);

    trace_log_pop();
}

char str_at(Str const *const self, USize const index) {
    error_check_null(LOG_METADATA, "self", (void*) self);

    /* char_at parity: an index at or past the end is a legal QUERY answered '\0'
     * (a scan running off a short Str asks it routinely); the abort that stood
     * here made the family behave differently at each layer. Also covers the
     * empty Str, whose data is nullptr. */
    if (index >= self->size) {
        return '\0';
    }

    return (char) self->data[index];
}

bool str_compare_equal_1(Str const *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    bool const match = str_compare_equal_2(self, data, char_length(data));

    trace_log_pop();

    return match;
}

bool str_compare_equal_2(Str const *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Empty is data, not a caller bug. Settled here rather than delegated
     * because an empty Str carries data == nullptr, and char_compare_equal_2
     * still (correctly) rejects a null pointer - a null IS a caller bug. Two
     * empties are equal; an empty and a non-empty are not. */
    if (self->size == 0 || data_size == 0) {
        bool const empty_match = self->size == data_size;

        trace_log_pop();

        return empty_match;
    }

    bool const match = char_compare_equal_2(self->data, self->size, (char*) data, data_size);

    trace_log_pop();

    return match;
}

bool str_compare_equal_3(Str const *const self, Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which str_compare_equal_2 still
     * (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    bool const match = str_compare_equal_2(self, data_buffer, data->size);

    trace_log_pop();

    return match;
}

bool str_compare_equal_comptime_1(Str const *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    bool const match = str_compare_equal_comptime_2(self, data, char_length(data));

    trace_log_pop();

    return match;
}

bool str_compare_equal_comptime_2(Str const *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Settled here rather than delegated because an empty Str carries
     * data == nullptr, which char_compare_equal_comptime_2 still (correctly)
     * rejects. Comparing sizes leaks no more than the existing code already does. */
    if (self->size == 0 || data_size == 0) {
        bool const empty_match = self->size == data_size;

        trace_log_pop();

        return empty_match;
    }

    bool const match = char_compare_equal_comptime_2(self->data, self->size, (char*) data, data_size);

    trace_log_pop();

    return match;
}

bool str_compare_equal_comptime_3(Str const *const self, Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which str_compare_equal_comptime_2
     * still (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    bool const match = str_compare_equal_comptime_2(self, data_buffer, data->size);

    trace_log_pop();

    return match;
}

bool str_compare_iequal_1(Str const *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    bool const match = str_compare_iequal_2(self, data, char_length(data));

    trace_log_pop();

    return match;
}

bool str_compare_iequal_2(Str const *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* See str_compare_equal_2: empty is data, and an empty Str's data is null. */
    if (self->size == 0 || data_size == 0) {
        bool const empty_match = self->size == data_size;

        trace_log_pop();

        return empty_match;
    }

    bool const match = char_compare_iequal_2(self->data, self->size, data, data_size);

    trace_log_pop();

    return match;
}

bool str_compare_iequal_3(Str const *const self, Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which str_compare_iequal_2 still
     * (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    bool const match = str_compare_iequal_2(self, data_buffer, data->size);

    trace_log_pop();

    return match;
}

bool str_compare_iequal_comptime_1(Str const *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    bool const match = str_compare_iequal_comptime_2(self, data, char_length(data));

    trace_log_pop();

    return match;
}

bool str_compare_iequal_comptime_2(Str const *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Settled here rather than delegated because an empty Str carries
     * data == nullptr, which char_compare_iequal_comptime_2 still (correctly)
     * rejects. Comparing sizes leaks no more than the existing code already does. */
    if (self->size == 0 || data_size == 0) {
        bool const empty_match = self->size == data_size;

        trace_log_pop();

        return empty_match;
    }

    bool const match = char_compare_iequal_comptime_2(self->data, self->size, (char*) data, data_size);

    trace_log_pop();

    return match;
}

bool str_compare_iequal_comptime_3(Str const *const self, Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which str_compare_iequal_comptime_2
     * still (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    bool const match = str_compare_iequal_comptime_2(self, data_buffer, data->size);

    trace_log_pop();

    return match;
}

bool str_contains_1(Str const *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    bool const value = str_contains_2(self, data, char_length(data));

    trace_log_pop();

    return value;
}

bool str_contains_2(Str const *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Settled here rather than delegated because an empty Str carries
     * data == nullptr, which char_contains_2 still (correctly) rejects.
     * Only an empty needle can be found inside an empty haystack. */
    if (self->size == 0) {
        bool const empty_match = data_size == 0;

        trace_log_pop();

        return empty_match;
    }

    bool const value = char_contains_2(self->data, self->size, data, data_size);

    trace_log_pop();

    return value;
}

bool str_contains_3(Str const *const self, Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which str_contains_2 still
     * (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    bool const value = str_contains_2(self, data_buffer, data->size);

    trace_log_pop();

    return value;
}

void str_copy_1(Str *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    str_copy_2(self, data, char_length(data));

    trace_log_pop();
}

void str_copy_2(Str *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* The new value is built BEFORE the old buffer is released, never after. Copying first
     * and releasing second is what keeps an aliasing source readable - a source inside self
     * included (str_copy_2(&s, s.data, 2) truncates s to its own prefix; str_copy_3(&s, &s) is
     * a wasteful but correct no-op) - so the alias guard that once stood here is not needed. */
    char *const old_data = self->data;
    bool const old_owned = self->owned;

    Str const copy = _str_init_static_like(self, data, data_size);

    /* A refused arena (or, on the heap, str_init_static's try_alloc declining) answers the
     * EMPTY Str for a non-empty input: that is a REFUSAL, and self keeps its value - the old
     * assignment stored the empty and then released the only copy of the content. */
    if (data_size > 0 && memory_empty(copy.data)) {
        trace_log_pop();

        return;
    }

    *self = copy;

    if (old_owned && !memory_empty(old_data)) {
#ifdef ARENA_IMPLEMENTATION
        allocator_release(old_data, self->allocator);
#else
        allocator_release(old_data);
#endif // ARENA_IMPLEMENTATION
    }

    trace_log_pop();
}

void str_copy_3(Str *const self, Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which str_copy_2 still (correctly)
     * rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    str_copy_2(self, data_buffer, data->size);

    trace_log_pop();
}

void str_delete(Str **const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* A null handle is the idempotent no-op the header promises (free(NULL)'s idiom): the
     * second str_delete after the first nulled *self, and a str_alloc_new_* that answered
     * nullptr, both land here. It used to be an error_check abort under a "safe" note. */
    if (memory_empty(*self)) {
        trace_log_pop();

        return;
    }

    /* Captured before the uninit so the struct goes back to whatever allocator produced
     * it: str_alloc_new_* borrows the struct itself from the arena, and memory_delete
     * would hand an interior pointer of the arena's block to free(). */
#ifdef ARENA_IMPLEMENTATION
    Arena *const allocator = (*self)->allocator;

    str_uninit(*self);

    if (!memory_empty(allocator)) {
        allocator_release((void*) *self, allocator);

        *self = nullptr;
    }
    else {
        memory_delete((void**) self);
    }
#else
    str_uninit(*self);

    memory_delete((void**) self);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();
}

bool str_empty(Str const *const self) {
    error_check_null(LOG_METADATA, "self", (void*) self);

    return self->size == 0;
}

bool str_ends_with_1(Str const *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    bool const value = str_ends_with_2(self, data, char_length(data));

    trace_log_pop();

    return value;
}

bool str_ends_with_2(Str const *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Settled here rather than delegated because an empty Str carries
     * data == nullptr, which char_ends_with_2 still (correctly) rejects.
     * The empty suffix ends every string, the empty one included. */
    if (self->size == 0) {
        bool const empty_match = data_size == 0;

        trace_log_pop();

        return empty_match;
    }

    bool const value = char_ends_with_2(self->data, self->size, data, data_size);

    trace_log_pop();

    return value;
}

bool str_ends_with_3(Str const *const self, Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which str_ends_with_2 still
     * (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    bool const value = str_ends_with_2(self, data_buffer, data->size);

    trace_log_pop();

    return value;
}

void str_erase(Str *const self, USize const from, USize const to) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* from/to are regex-match-shaped values (main_pcre2 passes match.begin/end
     * directly), so an out-of-range span refuses in every build rather than
     * aborting. from == to is the legal empty erase, [size, size) included. */
    if (from > to || to > self->size) {
        trace_log_pop();

        return;
    }

    if (from < to) {
        char_erase_2(self->data, self->size, from, to - 1);

        self->size -= to - from;
    }

    trace_log_pop();
}

void str_fill(Str *const self, char const c) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (!str_empty(self)) {
        char_fill(self->data, self->size, c);
    }

    trace_log_pop();
}

USize str_find_1(Str const *const self, USize const self_index, char const *const data) {
    trace_log_push(LOG_METADATA);

    USize const value = str_find_2(self, self_index, data, char_length(data));

    trace_log_pop();

    return value;
}

USize str_find_2(Str const *const self, USize const self_index, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Settled here rather than delegated because an empty Str carries
     * data == nullptr, which char_find_3 still (correctly) rejects. Only an
     * empty needle matches, and it does so at the one legal origin. */
    if (self->size == 0) {
        USize const empty_match = data_size == 0 && self_index == 0 ? 0 : CHAR_NPOS;

        trace_log_pop();

        return empty_match;
    }

    USize const value = char_find_3(self->data, self->size, self_index, data, data_size);

    trace_log_pop();

    return value;
}

USize str_find_3(Str const *const self, USize const self_index, Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which str_find_2 still (correctly)
     * rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    USize const value = str_find_2(self, self_index, data_buffer, data->size);

    trace_log_pop();

    return value;
}

USize str_find_any_1(Str const *const self, char const *const set) {
    trace_log_push(LOG_METADATA);

    USize const value = str_find_any_2(self, set, char_length(set));

    trace_log_pop();

    return value;
}

USize str_find_any_2(Str const *const self, char const *const set, USize const set_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "set", (void*) set);

    /* Settled here rather than delegated because an empty Str carries
     * data == nullptr, which char_find_first_2 still (correctly) rejects.
     * An empty haystack offers no position for any of the set to match at. */
    if (self->size == 0) {
        trace_log_pop();

        return CHAR_NPOS;
    }

    USize const value = char_find_first_2(self->data, self->size, set, set_size);

    trace_log_pop();

    return value;
}

USize str_find_count_1(Str const *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    USize const value = str_find_count_2(self, data, char_length(data));

    trace_log_pop();

    return value;
}

USize str_find_count_2(Str const *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Settled here rather than delegated because an empty Str carries
     * data == nullptr, which char_find_count_3 still (correctly) rejects.
     * Nothing occurs inside an empty haystack, an empty needle included. */
    if (self->size == 0) {
        trace_log_pop();

        return 0;
    }

    USize const value = char_find_count_3(self->data, self->size, data, data_size);

    trace_log_pop();

    return value;
}

bool str_find_exists_1(Str const *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    bool const value = str_find_exists_2(self, data, char_length(data));

    trace_log_pop();

    return value;
}

bool str_find_exists_2(Str const *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Settled here rather than delegated because an empty Str carries
     * data == nullptr, which char_find_exists_3 still (correctly) rejects. */
    if (self->size == 0) {
        bool const empty_match = data_size == 0;

        trace_log_pop();

        return empty_match;
    }

    bool const value = char_find_exists_3(self->data, self->size, data, data_size);

    trace_log_pop();

    return value;
}

bool str_find_exists_3(Str const *const self, Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which str_find_exists_2 still
     * (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    bool const value = str_find_exists_2(self, data_buffer, data->size);

    trace_log_pop();

    return value;
}

USize str_find_reverse_1(Str const *const self, USize const self_index, char const *const data) {
    trace_log_push(LOG_METADATA);

    USize const value = str_find_reverse_2(self, self_index, data, char_length(data));

    trace_log_pop();

    return value;
}

USize str_find_reverse_2(Str const *const self, USize const self_index, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Settled here rather than delegated because an empty Str carries
     * data == nullptr, which char_find_reverse_3 still (correctly) rejects. */
    if (self->size == 0) {
        USize const empty_match = data_size == 0 && self_index == 0 ? 0 : CHAR_NPOS;

        trace_log_pop();

        return empty_match;
    }

    USize const value = char_find_reverse_3(self->data, self->size, self_index, data, data_size);

    trace_log_pop();

    return value;
}

Str str_format(char const *const format, ...) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "format", (void*) format);

    va_list args;
    va_start(args, format);

    va_list args_copy;
    va_copy(args_copy, args);

    int const needed = vsnprintf(nullptr, 0, format, args_copy);

    va_end(args_copy);

    Str str = str_init_1();

    /* Both silent EMPTY answers now say why, as str_init_static's does: a negative needed is
     * an encoding error in the format, a declined try_alloc is memory. */
    if (needed < 0) {
        log_message_try_1(LOG_LEVEL_WARN, "str: format \"%s\" could not be rendered (encoding error)\n", format);
    }
    else if (needed > 0) {
        USize const size = (USize) needed;
        /* try_alloc: the guard below was dead behind the aborting alloc. */
        char *const buffer = (char*) memory_try_alloc(size + CHAR_END_CHARACTER);

        if (memory_empty(buffer)) {
            log_message_try_1(LOG_LEVEL_WARN, "str: not enough memory to format %llu bytes\n", (unsigned long long) size);
        }
        else {
            vsnprintf(buffer, size + CHAR_END_CHARACTER, format, args);

            str.data = buffer;
            str.size = size;
            str.owned = true;
        }
    }

    va_end(args);

    trace_log_pop();

    return str;
}

Str str_from_numbers_float_1(FSize const number) {
    trace_log_push(LOG_METADATA);

    char buffer[_STR_FROM_NUMBERS_FLOAT_BUFFER_SIZE] = DEFAULT_INITIALIZATION;

    char_from_numbers_float_1(buffer, sizeof(buffer), number);

    Str const str = str_init_static(buffer, char_length(buffer));

    trace_log_pop();

    return str;
}

Str str_from_numbers_float_2(FSize const number, U8 const precision) {
    trace_log_push(LOG_METADATA);

    char buffer[_STR_FROM_NUMBERS_FLOAT_BUFFER_SIZE] = DEFAULT_INITIALIZATION;

    char_from_numbers_float_2(buffer, sizeof(buffer), number, precision);

    Str const str = str_init_static(buffer, char_length(buffer));

    trace_log_pop();

    return str;
}

Str str_from_numbers_int_1(ISize const number) {
    trace_log_push(LOG_METADATA);

    Str const str = str_from_numbers_int_2(number, 0);

    trace_log_pop();

    return str;
}

Str str_from_numbers_int_2(ISize const number, U8 const padding) {
    trace_log_push(LOG_METADATA);

    /* Calls the shared helper directly, NOT str_alloc_from_numbers_int_2: that entry
     * point null-checks its allocator, so routing a nullptr through it aborted every
     * call to this function in an ARENA build. */
#ifdef ARENA_IMPLEMENTATION
    Str const str = _str_from_numbers_int(number, padding, nullptr);
#else
    Str const str = _str_from_numbers_int(number, padding);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return str;
}

Str str_from_numbers_uint_1(USize const number) {
    trace_log_push(LOG_METADATA);

    Str const str = str_from_numbers_uint_2(number, 0);

    trace_log_pop();

    return str;
}

Str str_from_numbers_uint_2(USize const number, U8 const padding) {
    trace_log_push(LOG_METADATA);

    /* Calls the shared helper directly, NOT str_alloc_from_numbers_uint_2: that entry
     * point null-checks its allocator, so routing a nullptr through it aborted every
     * call to this function in an ARENA build. */
#ifdef ARENA_IMPLEMENTATION
    Str const str = _str_from_numbers_uint(number, padding, nullptr);
#else
    Str const str = _str_from_numbers_uint(number, padding);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return str;
}

Str str_from_replace_1(Str const *const self, char const *const find, char const *const replace) {
    trace_log_push(LOG_METADATA);

    Str const str = str_from_replace_2(self, find, char_length(find), replace, char_length(replace));

    trace_log_pop();

    return str;
}

Str str_from_replace_2(Str const *const self, char const *const find, USize const find_size, char const *const replace, USize const replace_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "find", (void*) find);
    error_check_null(LOG_METADATA, "replace", (void*) replace);

    /* Nothing in an empty Str can be replaced, and char_new_replace_2 rejects the
     * nullptr one carries. An empty find pattern is not settled here: it reaches
     * the replace helper, which answers it with a verbatim copy. */
    if (self->size == 0) {
        Str const empty = str_init_1();

        trace_log_pop();

        return empty;
    }

    char *const data = char_new_replace_2(self->data, self->size, find, find_size, replace, replace_size);
    Str buffer = str_init_1();

    if (!memory_empty(data)) {
        USize const data_size = char_length(data);

        /* An all-consumed replacement ("abc" -> "") answers the EMPTY Str, not an owned
         * zero-length buffer: producers of nothing answer EMPTY, and an exact-fit Str gains
         * nothing from keeping a one-byte block (String keeps its owned zero-length trim
         * because a capacity is worth keeping; Str has none). _str_uninit could release it -
         * it keys on `owned` - so this is a value choice, not a leak workaround. */
        if (data_size == 0) {
            char_delete(data);
        }
        else {
            buffer.data = data;
            buffer.size = data_size;
            buffer.owned = true;
        }
    }

    trace_log_pop();

    return buffer;
}

Str str_from_trim(Str const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* Trimming an empty Str is empty; char_from_trim_2 rejects the nullptr it carries. */
    if (self->size == 0) {
        Str const empty = str_init_1();

        trace_log_pop();

        return empty;
    }

    char *const data = char_from_trim_2(self->data, self->size);
    Str buffer = str_init_1();

    if (!memory_empty(data)) {
        USize const data_size = char_length(data);

        /* Pure whitespace trims away to nothing: the EMPTY Str, as str_from_replace_2 says. */
        if (data_size == 0) {
            char_delete(data);
        }
        else {
            buffer.data = data;
            buffer.size = data_size;
            buffer.owned = true;
        }
    }

    trace_log_pop();

    return buffer;
}

char* str_get_data(Str const *const self) {
    error_check_null(LOG_METADATA, "self", (void*) self);

    return self->data;
}

USize str_get_size(Str const *const self) {
    error_check_null(LOG_METADATA, "self", (void*) self);

    return self->size;
}

Str str_init_1(void) {
#ifdef ARENA_IMPLEMENTATION
    Str const str = _str_init(nullptr);
#else
    Str const str = _str_init();
#endif // ARENA_IMPLEMENTATION

    return str;
}

Str str_init_2(char *const data) {
    error_check_null(LOG_METADATA, "data", (void*) data);

    Str const str = str_init_3(data, char_length(data));

    return str;
}

Str str_init_3(char *const data, USize const data_size) {
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* A zero size yields a valid EMPTY Str rather than aborting: "" is a legal
     * string value, and refusing it here made it impossible to even build the
     * empty case that the comparison and search APIs must handle. */
    Str str = str_init_1();

    /* A VIEW over caller memory - see the ownership note on Str in str.h. */
    str.data = data;
    str.size = data_size;
    str.owned = false;

    return str;
}

Str str_init_4(Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which str_init_static still
     * (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    Str const str = str_init_static(data_buffer, data->size);

    trace_log_pop();

    return str;
}

Str str_init_static(char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty source yields the EMPTY Str rather than an owned zero-length buffer:
     * producers of nothing answer EMPTY (see str_from_replace_2). */
    if (data_size == 0) {
        Str const empty = str_init_1();

        trace_log_pop();

        return empty;
    }

    /* try_alloc, not alloc: data_size is whatever the caller measured, and the
     * request-facing callers (json labels, regex groups, header values) measure
     * it from the wire. The aborting alloc would end the process on the one copy
     * it could not afford; the empty Str is the same degradation the arena twin
     * already makes on a refused arena, and every caller accepts it as "absent". */
    char *const char_new = (char*) memory_try_alloc(data_size + CHAR_END_CHARACTER);

    if (memory_empty(char_new)) {
        log_message_try_1(LOG_LEVEL_WARN, "str: not enough memory to copy %llu bytes\n", (unsigned long long) data_size);

        Str const empty = str_init_1();

        trace_log_pop();

        return empty;
    }

    char_copy_3(char_new, data_size + CHAR_END_CHARACTER, data, data_size);

    char_new[data_size] = '\0';

    Str str = str_init_3(char_new, data_size);

    /* A COPY of the source, so this Str owns char_new. */
    str.owned = true;

    trace_log_pop();

    return str;
}

Str str_join_1(Str const *const *const parts, USize const count, char const *const separator) {
    trace_log_push(LOG_METADATA);

    Str const str = str_join_2(parts, count, separator, char_length(separator));

    trace_log_pop();

    return str;
}

Str str_join_2(Str const *const *const parts, USize const count, char const *const separator, USize const separator_size) {
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    Str const str = _str_join(parts, count, separator, separator_size, nullptr);
#else
    Str const str = _str_join(parts, count, separator, separator_size);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return str;
}

void str_lower(Str *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (!str_empty(self)) {
        char_lower_2(self->data, self->size);
    }

    trace_log_pop();
}

void str_move_1(Str *const self, char **const data) {
    trace_log_push(LOG_METADATA);

    /* *data is read by char_length below, so these must run HERE - the _2 body's
     * checks sit after the deref and the diagnostic pointed at char_length. */
    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_null(LOG_METADATA, "*data", (void*) *data);

    str_move_2(self, data, char_length(*data));

    trace_log_pop();
}

void str_move_2(Str *const self, char **const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_null(LOG_METADATA, "*data", (void*) *data);

    /* A Str's own buffer moved into itself would be released by the uninit below and then
     * adopted dead: refuse, leaving the object and *data as they are. */
    if (*data == self->data) {
        trace_log_pop();

        return;
    }

    str_uninit(self);

    /* A zero size needs no special case: the move takes ownership either way, and an
     * owned buffer of size 0 is released correctly now that _str_uninit keys on the
     * ownership flag rather than on the size. */
    self->data = *data;
    self->size = data_size;
    self->owned = true;

    *data = nullptr;

    trace_log_pop();
}

void str_move_3(Str *const self, Str **const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_null(LOG_METADATA, "*data", (void*) *data);

#ifdef ARENA_IMPLEMENTATION
    /* `allocator` does double duty: _str_uninit reads it to release the BUFFER, and
     * str_delete reads it to release the STRUCT. A move therefore cannot simply adopt the
     * source's - that would rewrite where the destination's own struct is believed to
     * live, and str_delete would hand an arena interior pointer to free(). Crossing
     * allocators is REFUSED, as the header promises - a real no-op leaving BOTH
     * objects untouched, checked BEFORE the uninit below so a refusal cannot
     * destroy the destination first. (An error_check stood here: it aborted, and
     * only after str_uninit had already run.) */
    if ((*data)->allocator != self->allocator) {
        trace_log_pop();

        return;
    }
#endif // ARENA_IMPLEMENTATION

    str_uninit(self);

    /* Ownership transfers with the buffer: moving a view yields a view. */
    self->data = (*data)->data;
    self->size = (*data)->size;
    self->owned = (*data)->owned;

    (*data)->data = nullptr;
    (*data)->size = 0;
    (*data)->owned = false;

    *data = nullptr;

    trace_log_pop();
}

Str* str_new_1(void) {
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    Str const *const str = _str_new(nullptr);
#else
    Str const *const str = _str_new();
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return (Str*) str;
}

Str* str_new_2(char *const data) {
    trace_log_push(LOG_METADATA);

    Str const *const str = str_new_3(data, char_length(data));

    trace_log_pop();

    return (Str*) str;
}

Str* str_new_3(char *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    Str *const str = str_new_1();

    *str = str_init_3(data, data_size);

    trace_log_pop();

    return str;
}

Str* str_new_4(Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    Str *const str = str_new_1();

    *str = str_init_4(data);

    trace_log_pop();

    return str;
}

Str* str_new_static(char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    Str *const str = str_new_1();

    *str = str_init_static(data, data_size);

    trace_log_pop();

    return str;
}

void str_print(Str const *const self, char const *const data, bool const log) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    int const display_size = self->size <= (USize) I32_MAX ? (int) self->size : I32_MAX;

    if (log) {
        log_message_2(LOG_LEVEL_INFO, LOG_METADATA, "Str %s:\n", data);
        log_message_2(LOG_LEVEL_INFO, LOG_METADATA, "\tdata: %.*s\n", display_size, memory_empty(self->data) ? "" : self->data);
        log_message_2(LOG_LEVEL_INFO, LOG_METADATA, "\tsize : %zu\n", self->size);
    }
    else {
        printf("Str %s:\n", data);
        /* An empty Str carries data == nullptr; %.*s with a null pointer is UB even
         * at width 0, merely tolerated by common libcs. */
        printf("\tdata: %.*s\n", display_size, memory_empty(self->data) ? "" : self->data);
        printf("\tsize: %zu\n", self->size);
    }

    trace_log_pop();
}

void str_remove(Str *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* remove(i) IS erase(i, i + 1): the same in-place shift, so a view over read-only memory
     * faults here exactly as it does in str_erase (the typedef's in-place list). The former
     * copy-on-write plus a second heap borrow through char_remove_2 bought two allocations and
     * a view/owner asymmetry with its own superset. A past-the-end index (the empty Str
     * included) is data-shaped and refuses in every build. */
    if (index < self->size) {
        str_erase(self, index, index + 1);
    }

    trace_log_pop();
}

void str_repeat(Str *const self, USize const count) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* Repeating 0 times yields the empty result. char_repeat_2/char_alloc_repeat_2
     * still (correctly) reject count == 0, so it is settled here before either is
     * reached, whether self starts empty or not. */
    if (count == 0) {
        str_uninit(self);

        trace_log_pop();

        return;
    }

    if (str_empty(self)) {
        trace_log_pop();

        return;
    }

    /* count is a value, so an overflowing product refuses in every build - self unchanged -
     * rather than aborting in checked builds and wrapping the borrow small in unchecked ones. */
    if (count > USIZE_MAX / self->size) {
        trace_log_pop();

        return;
    }

    USize const new_size = self->size * count;

#ifdef ARENA_IMPLEMENTATION
    if (!memory_empty(self->allocator)) {
        char *const arena_buffer = char_alloc_repeat_2(self->data, self->size, count, self->allocator);

        /* char_alloc_repeat_2 refuses a refused arena to nullptr (since 2026-08-29); this
         * guard keeps the str-side contract - a null is never adopted as data == nullptr
         * with size > 0. Refuses BEFORE the uninit, leaving self intact. */
        if (memory_empty(arena_buffer)) {
            trace_log_pop();

            return;
        }

        str_uninit(self);

        self->data = arena_buffer;
        self->size = new_size;
        self->owned = true;

        trace_log_pop();

        return;
    }
#endif // ARENA_IMPLEMENTATION

    char *const buffer = char_repeat_2(self->data, self->size, count);

    /* Same contract as the arena branch above. */
    if (memory_empty(buffer)) {
        trace_log_pop();

        return;
    }

    str_uninit(self);

    self->data = buffer;
    self->size = new_size;
    self->owned = true;

    trace_log_pop();
}

void str_replace_1(Str *const self, char const *const data, USize const index) {
    trace_log_push(LOG_METADATA);

    str_replace_2(self, data, char_length(data), index);

    trace_log_pop();
}

void str_replace_2(Str *const self, char const *const data, USize const data_size, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Real control flow, value-dependent refusal (string_replace_2's shape): index
     * and data_size routinely arrive from data (a regex match, a parsed offset).
     * The error_checks that stood here were both wrong and dangerous - `>=` forbade
     * replacing the LAST byte (a normal op), and in an unchecked build the checks
     * compiled away entirely, leaving a caller-sized unbounded overwrite. The legal
     * condition for an in-place overwrite of [index, index + data_size) is
     * index + data_size <= size. */
    if (index >= self->size || data_size > self->size - index) {
        trace_log_pop();

        return;
    }

    /* Writing 0 bytes is a no-op; the loop below simply doesn't run. */
    for (USize i = 0; i < data_size; i += 1) {
        self->data[index + i] = data[i];
    }

    trace_log_pop();
}

void str_replace_3(Str *const self, Str const *const data, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which str_replace_2 still
     * (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    str_replace_2(self, data_buffer, data->size, index);

    trace_log_pop();
}

void str_reverse(Str *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (!str_empty(self)) {
        char_reverse_2(self->data, self->size);
    }

    trace_log_pop();
}

void str_set_size(Str *const self, USize const size) {
    error_check_null(LOG_METADATA, "self", (void*) self);

    /* The one bound provable without a capacity field: the EMPTY state has no buffer for any
     * size to fit in. Everything else stays the caller's guarantee - erase and move_2 leave
     * allocation > size + 1 legitimately, so size > self->size is not provably wrong. */
    if (memory_empty(self->data) && size != 0) {
        return;
    }

    self->size = size;
}

Str str_slice(Str const *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "self->size", self->size, "index > self->size", index > self->size);

    /* index == size is the tail after the last delimiter - the empty Str, a VALUE
     * (#941 char slice precedent). Also covers the empty source at index 0. Only
     * strictly past the end remains caller error. */
    if (index >= self->size) {
#ifdef ARENA_IMPLEMENTATION
        Str const empty = memory_empty(self->allocator) ? str_init_1() : str_alloc_init_1(self->allocator);
#else
        Str const empty = str_init_1();
#endif // ARENA_IMPLEMENTATION

        trace_log_pop();

        return empty;
    }

    /* An OWNED copy carrying the source's allocator, mirroring str_slice_range -
     * str_init_static always copied to the HEAP, so slicing an arena-backed Str
     * produced a heap owner (correctly paired with free(), but inconsistent). */
#ifdef ARENA_IMPLEMENTATION
    Str const str = memory_empty(self->allocator)
        ? str_init_static(self->data + index, self->size - index)
        : str_alloc_init_static(self->data + index, self->size - index, self->allocator);
#else
    Str const str = str_init_static(self->data + index, self->size - index);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return str;
}

Str str_slice_range(Str const *const self, USize const from, USize const to) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_out_of_bound_uint(LOG_METADATA, "from", from, "to", to, "from > to", from > to);
    error_check_out_of_bound_uint(LOG_METADATA, "from", from, "self->size", self->size, "from >= self->size", from >= self->size);
    error_check_out_of_bound_uint(LOG_METADATA, "to", to, "self->size", self->size, "to >= self->size", to >= self->size);

    USize const buffer_size = to - from + 1;
#ifdef ARENA_IMPLEMENTATION
    char *const buffer = (char*) allocator_borrow(buffer_size + CHAR_END_CHARACTER, self->allocator);
#else
    char *const buffer = (char*) allocator_borrow(buffer_size + CHAR_END_CHARACTER);
#endif // ARENA_IMPLEMENTATION

    /* Null only from a REFUSED arena; degrade to the empty Str, carrying the
     * source's allocator. */
    if (memory_empty(buffer)) {
#ifdef ARENA_IMPLEMENTATION
        Str const empty = memory_empty(self->allocator) ? str_init_1() : str_alloc_init_1(self->allocator);
#else
        Str const empty = str_init_1();
#endif // ARENA_IMPLEMENTATION

        trace_log_pop();

        return empty;
    }

    for (USize i = 0; i < buffer_size; i += 1) {
        buffer[i] = self->data[from + i];
    }

    buffer[buffer_size] = '\0';

    /* buffer was borrowed here, not supplied by the caller, so this Str owns it -
     * str_init_3 builds a VIEW by default, which would strand the block. The result
     * must also carry the allocator it was borrowed FROM: str_init_3 leaves that null,
     * so an arena slice would later be released through free(). */
#ifdef ARENA_IMPLEMENTATION
    Str str = !memory_empty(self->allocator) ? str_alloc_init_3(buffer, buffer_size, self->allocator) : str_init_3(buffer, buffer_size);
#else
    Str str = str_init_3(buffer, buffer_size);
#endif // ARENA_IMPLEMENTATION

    str.owned = true;

    trace_log_pop();

    return str;
}

Str str_split_1(Str const *const self, char const *const delimiter) {
    trace_log_push(LOG_METADATA);

    Str const str = str_split_2(self, delimiter, char_length(delimiter));

    trace_log_pop();

    return str;
}

Str str_split_2(Str const *const self, char const *const delimiter, USize const delimiter_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "delimiter", (void*) delimiter);

    /* An empty delimiter marks no split point, so the whole string is the one
     * token. An empty self has nothing to split either way. Both are settled
     * here because char_find_3 rejects the nullptr an empty Str carries. */
    if (self->size == 0 || delimiter_size == 0) {
        Str const whole = self->size == 0 ? _str_init_like(self) : _str_init_static_like(self, self->data, self->size);

        trace_log_pop();

        return whole;
    }

    /* The token carries the source's allocator (str_slice's shape): a heap owner cut from an
     * arena-backed Str had to be freed individually while everything around it was released
     * in bulk. An empty first token is the EMPTY Str, still carrying the allocator. */
    USize const index = char_find_3(self->data, self->size, 0, (char*) delimiter, delimiter_size);
    USize const split_size = index == CHAR_NPOS ? self->size : index;
    Str const str = _str_init_static_like(self, self->data, split_size);

    trace_log_pop();

    return str;
}

Str str_split_3(Str const *const self, Str const *const delimiter) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "delimiter", (void*) delimiter);

    /* An empty delimiter carries delimiter->data == nullptr, which str_split_2 still
     * (correctly) rejects; substitute a valid empty source. */
    char const *const delimiter_buffer = delimiter->size == 0 ? "" : delimiter->data;

    Str const str = str_split_2(self, delimiter_buffer, delimiter->size);

    trace_log_pop();

    return str;
}

bool str_split_next(Str const *const self, char const *const delimiter, USize const delimiter_size, USize *const index, USize *const token_from, USize *const token_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "delimiter", (void*) delimiter);
    error_check_null(LOG_METADATA, "index", (void*) index);
    error_check_null(LOG_METADATA, "token_from", (void*) token_from);
    error_check_null(LOG_METADATA, "token_size", (void*) token_size);

    /* Settled here rather than delegated because an empty Str carries
     * data == nullptr. An empty string yields exactly one empty token, so the
     * walk reports it once and then reports exhaustion. */
    if (self->size == 0) {
        bool const first = *index != CHAR_NPOS;

        if (first) {
            *token_from = 0;
            *token_size = 0;
            *index = CHAR_NPOS;
        }

        trace_log_pop();

        return first;
    }

    bool const value = char_split_next(self->data, self->size, delimiter, delimiter_size, index, token_from, token_size);

    trace_log_pop();

    return value;
}

bool str_starts_with_1(Str const *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    bool const value = str_starts_with_2(self, data, char_length(data));

    trace_log_pop();

    return value;
}

bool str_starts_with_2(Str const *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Settled here rather than delegated because an empty Str carries
     * data == nullptr, which char_starts_with_2 still (correctly) rejects.
     * The empty prefix begins every string, the empty one included. */
    if (self->size == 0) {
        bool const empty_match = data_size == 0;

        trace_log_pop();

        return empty_match;
    }

    bool const value = char_starts_with_2(self->data, self->size, data, data_size);

    trace_log_pop();

    return value;
}

bool str_starts_with_3(Str const *const self, Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which str_starts_with_2 still
     * (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    bool const value = str_starts_with_2(self, data_buffer, data->size);

    trace_log_pop();

    return value;
}

FSize str_to_numbers_float(Str const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* An empty Str carries data == nullptr and a size of 0, both of which
     * char_to_numbers_float_2 rejects. Parsing nothing yields nothing. */
    if (self->size == 0) {
        trace_log_pop();

        return 0.0;
    }

    FSize const value = char_to_numbers_float_2(self->data, self->size);

    trace_log_pop();

    return value;
}

ISize str_to_numbers_int(Str const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* An empty Str carries data == nullptr and a size of 0, both of which
     * char_to_numbers_int_2 rejects. Parsing nothing yields nothing. */
    if (self->size == 0) {
        trace_log_pop();

        return 0;
    }

    ISize const value = char_to_numbers_int_2(self->data, self->size);

    trace_log_pop();

    return value;
}

USize str_to_numbers_uint(Str const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* An empty Str carries data == nullptr and a size of 0, both of which
     * char_to_numbers_uint_2 rejects. Parsing nothing yields nothing. */
    if (self->size == 0) {
        trace_log_pop();

        return 0;
    }

    USize const value = char_to_numbers_uint_2(self->data, self->size);

    trace_log_pop();

    return value;
}

void str_trim(Str *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

#ifdef ARENA_IMPLEMENTATION
    if (!memory_empty(self->allocator)) {
        str_alloc_trim(self, self->allocator);

        trace_log_pop();

        return;
    }
#endif // ARENA_IMPLEMENTATION

    /* The arena twin's shape (str_alloc_trim); the heap leaf declines only on a refused
     * arena it is never given, so the guard is for symmetry. Nothing to trim is a no-op, pure
     * whitespace becomes the EMPTY Str, anything else adopts the exact-size copy. */
    if (self->size == 0) {
        trace_log_pop();

        return;
    }

    char *const data = char_from_trim_2(self->data, self->size);

    if (memory_empty(data)) {
        trace_log_pop();

        return;
    }

    USize const data_size = char_length(data);

    str_uninit(self);

    if (data_size == 0) {
        char_delete(data);
    }
    else {
        self->data = data;
        self->size = data_size;
        self->owned = true;
    }

    trace_log_pop();
}

void str_uninit(Str *const self) {
    trace_log_push(LOG_METADATA);

    /* Checked HERE: the arena build reads self->allocator before _str_uninit's own check. */
    error_check_null(LOG_METADATA, "self", (void*) self);

#ifdef ARENA_IMPLEMENTATION
    _str_uninit(self, self->allocator);
#else
    _str_uninit(self);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();
}

void str_upper(Str *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (!str_empty(self)) {
        char_upper_2(self->data, self->size);
    }

    trace_log_pop();
}