/*
 * string.c - Implementation of dynamic string container for the C Libraries Framework
 *
 * Features:
 *   - Dynamic, growable string with arena or heap allocation
 *   - Multiple initialization, copy, and manipulation functions
 *   - Error checking, logging, and TraceLog integration
 *
 * Usage Example:
 *   @code
 *   String s = string_init_1();
 *   string_add_last_1(&s, "hello");
 *   string_print(&s, "example", false);
 *   string_uninit(&s);
 *   @endcode
 *
 * Error Handling:
 *   Programming errors (a null self or data, a zero capacity, a slice strictly past the end) go
 *   through error_check_*, which aborts in checked builds; data-shaped values (indices, spans,
 *   sizes, rendered lengths) REFUSE as no-ops in every build - see the header's Error Handling.
 *
 * Thread Safety:
 *   Not thread-safe. Caller must synchronize if used from multiple threads.
 *
 * See string.h for API documentation and usage details.
 */
/*==============================================================================
 * MARK: - Includes
 *============================================================================*/
#include <container/string/string.h>

/*==============================================================================
 * MARK: - Macros and Constants
 *============================================================================*/
#define _STRING_FROM_NUMBERS_FLOAT_BUFFER_SIZE 128
#define _STRING_GROWTH_FACTOR 2

/*==============================================================================
 * MARK: - Static/Internal Functions
 *============================================================================*/
#ifdef ARENA_IMPLEMENTATION
static String _string_adopt(char *const data, USize const data_size, Arena *const allocator)
#else
static String _string_adopt(char *const data, USize const data_size)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    /* Takes ownership of a buffer this module just allocated. string_init_4 is the
     * BORROWING constructor, so a producing function must not use it for its own
     * result: the flag would say view and the buffer would never be released. */
#ifdef ARENA_IMPLEMENTATION
    String string = !memory_empty(allocator)
        ? string_alloc_init_4(data, data_size, allocator)
        : string_init_4(data, data_size);
#else
    String string = string_init_4(data, data_size);
#endif // ARENA_IMPLEMENTATION

    string.owned = true;

    trace_log_pop();

    return string;
}

#ifdef ARENA_IMPLEMENTATION
static String _string_from_numbers_int(ISize const number, U8 const padding, Arena *const allocator)
#else
static String _string_from_numbers_int(ISize const number, U8 const padding)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    /* Ported from _str_from_numbers_int. The previous walk here searched for the
     * first power of ten above number and then emitted j = 0..i INCLUSIVE, so it produced
     * one digit too many (4096 -> "40966", 5 -> "55"), divided by (exp / 10) == 0 when
     * number was 0, had no negative path, and never wrote a terminator. It was unreachable
     * while the heap path aborted on a nullptr allocator; _string_adopt made it reachable. */
    bool const negative = number < 0;
    USize const magnitude = negative ? (USize) (-(number + 1)) + 1 : (USize) number;
    USize digits = 1;
    USize scale = 1;

    while (magnitude / scale >= 10) {
        scale *= 10;
        digits += 1;
    }

    USize const sign_size = negative ? 1 : 0;
    USize const buffer_size = sign_size + padding + digits;

#ifdef ARENA_IMPLEMENTATION
    char *const buffer = (char*) allocator_borrow(buffer_size + CHAR_END_CHARACTER, allocator);
#else
    char *const buffer = (char*) allocator_borrow(buffer_size + CHAR_END_CHARACTER);
#endif // ARENA_IMPLEMENTATION

    /* Null only from a REFUSED arena; degrade to the plain empty String rather
     * than abort (the error_check that stood here was an abort-style refusal). */
    if (memory_empty(buffer)) {
#ifdef ARENA_IMPLEMENTATION
        String const empty = memory_empty(allocator) ? string_init_1() : string_alloc_init_1(allocator);
#else
        String const empty = string_init_1();
#endif // ARENA_IMPLEMENTATION

        trace_log_pop();

        return empty;
    }

    char_fill(buffer, buffer_size, '0');

    if (negative) {
        buffer[0] = '-';
    }

    USize remaining = magnitude;

    for (USize j = 0; j < digits; j += 1) {
        buffer[buffer_size - 1 - j] = (char) ('0' + remaining % 10);
        remaining /= 10;
    }

    // Unconditional on purpose: the buffer is sized with CHAR_END_CHARACTER above and
    // char_fill writes only the digit positions, so nothing else terminates it.
    buffer[buffer_size] = '\0';

#ifdef ARENA_IMPLEMENTATION
    String const string = _string_adopt(buffer, buffer_size, allocator);
#else
    String const string = _string_adopt(buffer, buffer_size);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return string;
}

#ifdef ARENA_IMPLEMENTATION
static String _string_from_numbers_uint(USize const number, U8 const padding, Arena *const allocator)
#else
static String _string_from_numbers_uint(USize const number, U8 const padding)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    /* Ported from _str_from_numbers_uint. The previous walk here searched for the
     * first power of ten above number and then emitted j = 0..i INCLUSIVE, so it produced
     * one digit too many (4096 -> "40966", 5 -> "55"), divided by (exp / 10) == 0 when
     * number was 0, had no negative path, and never wrote a terminator. It was unreachable
     * while the heap path aborted on a nullptr allocator; _string_adopt made it reachable. */
    USize const magnitude = number;
    USize digits = 1;
    USize scale = 1;

    while (magnitude / scale >= 10) {
        scale *= 10;
        digits += 1;
    }

    USize const sign_size = 0;
    USize const buffer_size = sign_size + padding + digits;

#ifdef ARENA_IMPLEMENTATION
    char *const buffer = (char*) allocator_borrow(buffer_size + CHAR_END_CHARACTER, allocator);
#else
    char *const buffer = (char*) allocator_borrow(buffer_size + CHAR_END_CHARACTER);
#endif // ARENA_IMPLEMENTATION

    /* Null only from a REFUSED arena; degrade to the plain empty String rather
     * than abort (the error_check that stood here was an abort-style refusal). */
    if (memory_empty(buffer)) {
#ifdef ARENA_IMPLEMENTATION
        String const empty = memory_empty(allocator) ? string_init_1() : string_alloc_init_1(allocator);
#else
        String const empty = string_init_1();
#endif // ARENA_IMPLEMENTATION

        trace_log_pop();

        return empty;
    }

    char_fill(buffer, buffer_size, '0');

    USize remaining = magnitude;

    for (USize j = 0; j < digits; j += 1) {
        buffer[buffer_size - 1 - j] = (char) ('0' + remaining % 10);
        remaining /= 10;
    }

    // Unconditional on purpose: the buffer is sized with CHAR_END_CHARACTER above and
    // char_fill writes only the digit positions, so nothing else terminates it.
    buffer[buffer_size] = '\0';

#ifdef ARENA_IMPLEMENTATION
    String const string = _string_adopt(buffer, buffer_size, allocator);
#else
    String const string = _string_adopt(buffer, buffer_size);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return string;
}

#ifdef ARENA_IMPLEMENTATION
static String _string_init(Arena *const allocator)
#else
static String _string_init(void)
#endif // ARENA_IMPLEMENTATION
{
    String string = DEFAULT_INITIALIZATION;

#ifdef ARENA_IMPLEMENTATION
    if (!memory_empty(allocator)) {
        string.allocator = allocator;
    }
#endif // ARENA_IMPLEMENTATION

    return string;
}

#ifdef ARENA_IMPLEMENTATION
static void _string_uninit(String *const self, Arena *const allocator)
#else
static void _string_uninit(String *const self)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* Keyed on the ownership flag: data != nullptr cannot tell an OWNER from a VIEW,
     * so it used to hand borrowed memory - an lws payload, a request's interior
     * buffer, a literal - straight to the allocator. The fields are cleared either
     * way so a released String never keeps a live-looking pointer. */
    if (self->owned && !memory_empty(self->data)) {
#ifdef ARENA_IMPLEMENTATION
        allocator_release(self->data, allocator);
#else
        allocator_release(self->data);
#endif // ARENA_IMPLEMENTATION
    }

    self->capacity  = 0;
    self->data      = nullptr;
    self->size      = 0;
    self->owned     = false;

    trace_log_pop();
}

/*==============================================================================
 * MARK: - Arena-backed API
 *============================================================================*/
#ifdef ARENA_IMPLEMENTATION
String string_alloc_from_numbers_float_1(FSize const number, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    char buffer[_STRING_FROM_NUMBERS_FLOAT_BUFFER_SIZE] = DEFAULT_INITIALIZATION;

    char_from_numbers_float_1(buffer, sizeof(buffer), number);

    String const string = string_alloc_init_static(buffer, char_length(buffer), allocator);

    trace_log_pop();

    return string;
}

String string_alloc_from_numbers_float_2(FSize const number, U8 const precision, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    char buffer[_STRING_FROM_NUMBERS_FLOAT_BUFFER_SIZE] = DEFAULT_INITIALIZATION;

    char_from_numbers_float_2(buffer, sizeof(buffer), number, precision);

    String const string = string_alloc_init_static(buffer, char_length(buffer), allocator);

    trace_log_pop();

    return string;
}

String string_alloc_from_numbers_int_1(ISize const number, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    String const string = string_alloc_from_numbers_int_2(number, 0, allocator);

    trace_log_pop();

    return string;
}

String string_alloc_from_numbers_int_2(ISize const number, U8 const padding, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    String const string = _string_from_numbers_int(number, padding, allocator);

    trace_log_pop();

    return string;
}

String string_alloc_from_numbers_uint_1(USize const number, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    String const string = string_alloc_from_numbers_uint_2(number, 0, allocator);

    trace_log_pop();

    return string;
}

String string_alloc_from_numbers_uint_2(USize const number, U8 const padding, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    String const string = _string_from_numbers_uint(number, padding, allocator);

    trace_log_pop();

    return string;
}

String string_alloc_from_replace_1(String const *const self, char const *const find, char const *const replace, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "find", (void*) find);
    error_check_null(LOG_METADATA, "replace", (void*) replace);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    String const string = string_alloc_from_replace_2(self, find, char_length(find), replace, char_length(replace), allocator);

    trace_log_pop();

    return string;
}

String string_alloc_from_replace_2(String const *const self, char const *const find, USize const find_size, char const *const replace, USize const replace_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "find", (void*) find);
    error_check_null(LOG_METADATA, "replace", (void*) replace);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    /* Nothing in an empty String can be replaced, and char_alloc_new_replace_2
     * rejects the nullptr one carries. An empty find pattern is not settled here:
     * it reaches the replace helper, which answers it with a verbatim copy. */
    if (self->size == 0) {
        String const empty = string_alloc_init_1(allocator);

        trace_log_pop();

        return empty;
    }

    char *const data = char_alloc_new_replace_2(self->data, self->size, find, find_size, replace, replace_size, allocator);
    String buffer = string_alloc_init_1(allocator);

    if (!memory_empty(data)) {
        buffer = _string_adopt(data, char_length(data), allocator);
    }

    trace_log_pop();

    return buffer;
}

String string_alloc_from_trim(String const *const self, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    /* Trimming an empty String is empty; char_alloc_from_trim_2 rejects the nullptr
     * it carries. Mirrors str_from_trim - the twins must not disagree on empty. */
    if (self->size == 0) {
        String const empty = string_alloc_init_1(allocator);

        trace_log_pop();

        return empty;
    }

    char *const data = char_alloc_from_trim_2(self->data, self->size, allocator);
    String buffer = string_alloc_init_1(allocator);

    if (!memory_empty(data)) {
        USize const length = char_length(data);

        buffer.capacity = length + CHAR_END_CHARACTER;
        buffer.data = data;
        buffer.owned = true;
        buffer.size = length;
    }

    trace_log_pop();

    return buffer;
}

String string_alloc_init_1(Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    String const string = _string_init(allocator);

    trace_log_pop();

    return string;
}

String string_alloc_init_2(USize const capacity, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    String string = _string_init(allocator);

    char *const buffer = (char*) allocator_borrow(capacity, allocator);

    /* Null only from a REFUSED arena (the heap and live-arena paths abort instead).
     * Assigning it anyway built a String claiming owned=true and capacity=N with
     * data == nullptr - an object whose own invariants lie; string_format then
     * null-derefed it. The plain empty String is the honest degraded result. */
    if (memory_empty(buffer)) {
        trace_log_pop();

        return string;
    }

    string.capacity = capacity;
    string.data = buffer;
    string.owned = true;

    trace_log_pop();

    return string;
}

String string_alloc_init_3(char *const data, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    String const string = string_alloc_init_4(data, char_length(data), allocator);

    trace_log_pop();

    return string;
}

String string_alloc_init_4(char *const data, USize const data_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    /* A zero size yields a valid EMPTY String rather than aborting - mirrors
     * string_init_4. */
    String string = _string_init(allocator);

    /* Refused to the EMPTY String, as string_init_4: the capacity would wrap. */
    if (data_size == USIZE_MAX) {
        trace_log_pop();

        return string;
    }

    /* A VIEW over caller memory - see the ownership note on String in string.h.
     * Producing functions inside this module use _string_adopt instead. */
    string.capacity    = data_size + CHAR_END_CHARACTER;
    string.data        = data;
    string.size        = data_size;
    string.owned       = false;

    trace_log_pop();

    return string;
}

String string_alloc_init_5(Str const *const data, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Routed through the COPYING constructor, matching string_alloc_init_6. Using the
     * adopting string_alloc_init_4 here made the String store the caller's pointer
     * without owning it: for an empty Str that pointer is a read-only "" literal that
     * string_uninit would then release, and for a non-empty one the Str and the String
     * both believe they own the same block. */
    String const string = string_alloc_init_static(data->size == 0 ? "" : data->data, data->size, allocator);

    trace_log_pop();

    return string;
}

String string_alloc_init_6(String const *const data, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    /* An empty data carries data->data == nullptr, which string_alloc_init_static
     * still (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    String const string = string_alloc_init_static(data_buffer, data->size, allocator);

    trace_log_pop();

    return string;
}

String string_alloc_init_static(char const *const data, USize const data_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    /* An empty source yields the plain empty String; string_alloc_init_2 rejects a
     * zero capacity, and String owns its buffer so there is nothing to copy. */
    if (data_size == 0) {
        String const empty = string_alloc_init_1(allocator);

        trace_log_pop();

        return empty;
    }

    String string = string_alloc_init_2(data_size + CHAR_END_CHARACTER, allocator);

    /* A refused arena degrades alloc_init_2 to the plain empty; checked BEFORE the
     * size assignment, or this would manufacture size > 0 with data == nullptr and
     * char_copy_3 would abort one frame from the actual failure. */
    if (memory_empty(string.data)) {
        trace_log_pop();

        return string;
    }

    string.size = data_size;

    char_copy_3(string.data, string.capacity, data, data_size);

    // Unconditional on purpose: char_copy_3 writes exactly data_size bytes and never
    // terminates, and this twin takes an allocator. CFW's own arenas zero on recycle, so
    // this is belt-and-braces - but the always-zeroed guarantee is owed by memory_alloc
    // alone, and Arena is a Tp vtable a third-party implementation can plug into.
    string.data[data_size] = '\0';

    trace_log_pop();

    return string;
}

String string_alloc_join_1(String const *const *const parts, USize const count, char const *const separator, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "separator", (void*) separator);

    String const string = string_alloc_join_2(parts, count, separator, char_length(separator), allocator);

    trace_log_pop();

    return string;
}

String string_alloc_join_2(String const *const *const parts, USize const count, char const *const separator, USize const separator_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "parts", count > 0 ? (void const*) parts : (void const*) 1);
    error_check_null(LOG_METADATA, "separator", (void*) separator);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    /* Joining 0 parts yields the empty result; the loop below simply doesn't run. */
    String string = string_alloc_init_1(allocator);

    /* One pre-sized reserve instead of a growth per append - and on a refused arena the whole join
     * is refused (the EMPTY String), never a truncated one that reads as success. */
    bool overflow = count > 1 && separator_size > 0 && count - 1 > (USIZE_MAX - CHAR_END_CHARACTER) / separator_size;
    USize total = count > 1 && !overflow ? (count - 1) * separator_size : 0;

    for (USize i = 0; i < count && !overflow; i += 1) {
        error_check_null(LOG_METADATA, "parts[i]", (void*) parts[i]);

        overflow = parts[i]->size > USIZE_MAX - CHAR_END_CHARACTER - total;
        total += overflow ? 0 : parts[i]->size;
    }

    if (overflow) {
        trace_log_pop();

        return string;
    }

    if (total > 0) {
        string_reserve(&string, total + CHAR_END_CHARACTER);

        if (string.capacity < total + CHAR_END_CHARACTER) {
            trace_log_pop();

            return string;
        }
    }

    for (USize i = 0; i < count; i += 1) {
        if (parts[i]->size > 0) {
            string_add_last_4(&string, parts[i]);
        }

        if (separator_size > 0 && i + 1 < count) {
            string_add_last_2(&string, separator, separator_size);
        }
    }

    trace_log_pop();

    return string;
}

String* string_alloc_new_1(Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    String *const string = (String*) allocator_borrow(sizeof(String), allocator);

    /* Null only from a REFUSED arena (the mirror of _str_new's guard - the same
     * struct-borrow site memsec caught on the str side). Propagate the nullptr. */
    if (memory_empty(string)) {
        trace_log_pop();

        return nullptr;
    }

    *string = _string_init(allocator);

    trace_log_pop();

    return string;
}

String* string_alloc_new_2(USize const capacity, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    String *const string = string_alloc_new_1(allocator);

    /* A refused arena propagates as nullptr from the struct borrow. */
    if (memory_empty(string)) {
        trace_log_pop();

        return nullptr;
    }

    *string = string_alloc_init_2(capacity, allocator);

    trace_log_pop();

    return string;
}

String* string_alloc_new_3(char *const data, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    String const *const string = string_alloc_new_4(data, char_length(data), allocator);

    trace_log_pop();

    return (String*) string;
}

String* string_alloc_new_4(char *const data, USize const data_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    String *const string = string_alloc_new_1(allocator);

    /* A refused arena propagates as nullptr from the struct borrow. */
    if (memory_empty(string)) {
        trace_log_pop();

        return nullptr;
    }

    /* A VIEW, matching string_alloc_init_4: data comes from the CALLER, so this is
     * not an adopt. _string_adopt is only for buffers this module allocated. */
    *string = string_alloc_init_4(data, data_size, allocator);

    trace_log_pop();

    return string;
}

String* string_alloc_new_5(Str const *const data, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which string_alloc_new_static
     * still (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    String const *const string = string_alloc_new_static(data_buffer, data->size, allocator);

    trace_log_pop();

    return (String*) string;
}

String* string_alloc_new_6(String const *const data, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which string_alloc_new_static
     * still (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    String const *const string = string_alloc_new_static(data_buffer, data->size, allocator);

    trace_log_pop();

    return (String*) string;
}

String* string_alloc_new_static(char const *const data, USize const data_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    String *const string = string_alloc_new_1(allocator);

    /* A refused arena propagates as nullptr from the struct borrow. */
    if (memory_empty(string)) {
        trace_log_pop();

        return nullptr;
    }

    *string = string_alloc_init_static(data, data_size, allocator);

    trace_log_pop();

    return string;
}

void string_alloc_trim(String *const self, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    String const buffer = string_alloc_from_trim(self, allocator);

    /* A refused arena degrades the copy to the EMPTY String (data nullptr) while self still holds
     * bytes: keep self rather than release it for nothing (the string_copy / string_reserve shape).
     * The test is the POINTER, not the size - an all-whitespace source trims to an owned
     * zero-length String, which is a result, not a refusal. */
    if (self->size > 0 && memory_empty(buffer.data)) {
        trace_log_pop();

        return;
    }

    string_uninit(self);

    *self = buffer;

    trace_log_pop();
}

#endif // ARENA_IMPLEMENTATION

/*==============================================================================
 * MARK: - Standard API
 *============================================================================*/
void string_add_1(String *const self, char const *const data, USize const index) {
    trace_log_push(LOG_METADATA);

    string_add_2(self, data, char_length(data), index);

    trace_log_pop();
}

void string_add_2(String *const self, char const *const data, USize const data_size, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* index routinely arrives from data (an unchecked find result - CHAR_NPOS - or
     * a parsed offset) and data_size can wrap the growth arithmetic; both refuse in
     * every build (str_add_2's shape, the framework standard). index == size stays
     * the legal append-at-end. */
    if (index > self->size || data_size > USIZE_MAX - self->size - CHAR_END_CHARACTER) {
        trace_log_pop();

        return;
    }

    /* Appending nothing is a no-op, not an error - strcat(s, "") does not fail. The
     * overflow check above is safe to run first at a zero size, so unlike _char_repeat
     * and _char_join this return sits after the whole group rather than inside it. */
    if (data_size == 0) {
        trace_log_pop();

        return;
    }

    /* data may point INTO self->data (self-append): growth below releases the old
     * buffer, and the splice would then read through the freed block - a UAF that
     * survives on some CRTs only by accident. Capture the offset now and re-base
     * after any growth. (A view's buffer is not released by reserve, but re-basing
     * is correct there too: the bytes were copied into the new buffer.) */
    bool const data_aliases_self = !memory_empty(self->data) && data >= self->data && data < self->data + self->capacity;
    USize const alias_offset = data_aliases_self ? (USize) (data - self->data) : 0;

    /* The window is the whole ALLOCATION, not the live bytes: a source in the spare capacity
     * [size, capacity) would otherwise pass as foreign and dangle when the growth releases the
     * buffer. A self-aliased source must then lie wholly inside [0, size) - one that starts in
     * the live bytes but runs past size (or starts in the spare capacity) has no honest
     * re-base after the shift, so it is REFUSED as a no-op. */
    if (data_aliases_self && (alias_offset >= self->size || data_size > self->size - alias_offset)) {
        trace_log_pop();

        return;
    }

    /* One reserve, sized once: at least the bytes needed INCLUDING the terminator
     * (so the post-splice terminator reserve below never fires a second borrow),
     * and at least geometric growth so appends stay amortized O(n). The four-branch
     * ladder this replaces could call string_reserve twice per append. Integer
     * arithmetic: the old float round-trip was UB above ~2^63. */
    USize const required = self->size + data_size + CHAR_END_CHARACTER;

    if (required > self->capacity) {
        USize const doubled = self->capacity > USIZE_MAX / _STRING_GROWTH_FACTOR ? USIZE_MAX : self->capacity * _STRING_GROWTH_FACTOR;
        USize const grown = doubled == 0 ? _STRING_GROWTH_FACTOR : doubled;

        string_reserve(self, required > grown ? required : grown);

        /* A refused arena leaves capacity unchanged; the splice below would then
         * write past the real buffer, so refuse wholly instead. */
        if (required > self->capacity) {
            trace_log_pop();

            return;
        }
    }

    /* Shift [index, size) up by data_size; the count-down form needs no i == 0 special case. */
    for (USize i = self->size; i > index; i -= 1) {
        self->data[data_size + i - 1] = self->data[i - 1];
    }

    /* An aliased source after the shift: bytes at or above index moved up by data_size, so a
     * source that started there is re-based past the shift and is now disjoint from the hole;
     * a source that started BELOW index is untouched (the shift wrote only at index + data_size
     * and above) but may overlap the hole from below, so it is copied backward - the memmove
     * order - never through the forward string_replace_2. */
    if (data_aliases_self && alias_offset < index) {
        for (USize i = data_size; i > 0; i -= 1) {
            self->data[index + i - 1] = self->data[alias_offset + i - 1];
        }
    }
    else {
        char const *const source = data_aliases_self ? self->data + alias_offset + (alias_offset >= index ? data_size : 0) : data;

        string_replace_2(self, source, data_size, index);
    }

    self->size += data_size;

    /* No second reserve: `required` above included the terminator slot. */
    self->data[self->size] = '\0';

    trace_log_pop();
}

void string_add_3(String *const self, Str const *const data, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which string_add_2 still
     * (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    string_add_2(self, data_buffer, data->size, index);

    trace_log_pop();
}

void string_add_4(String *const self, String const *const data, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which string_add_2 still
     * (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    string_add_2(self, data_buffer, data->size, index);

    trace_log_pop();
}

void string_add_first_1(String *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    string_add_1(self, data, 0);

    trace_log_pop();
}

void string_add_first_2(String *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    string_add_2(self, data, data_size, 0);

    trace_log_pop();
}

void string_add_first_3(String *const self, Str const *const data) {
    trace_log_push(LOG_METADATA);

    string_add_3(self, data, 0);

    trace_log_pop();
}

void string_add_first_4(String *const self, String const *const data) {
    trace_log_push(LOG_METADATA);

    string_add_4(self, data, 0);

    trace_log_pop();
}

void string_add_last_1(String *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    string_add_1(self, data, self->size);

    trace_log_pop();
}

void string_add_last_2(String *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    string_add_2(self, data, data_size, self->size);

    trace_log_pop();
}

void string_add_last_3(String *const self, Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    string_add_3(self, data, self->size);

    trace_log_pop();
}

void string_add_last_4(String *const self, String const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    string_add_4(self, data, self->size);

    trace_log_pop();
}

char string_at(String const *const self, USize const index) {
    error_check_null(LOG_METADATA, "self", (void*) self);

    /* char_at/str_at parity: an index at or past the end is a legal QUERY answered
     * '\0'. Untraced like str_at - accessors are leaves. */
    if (index >= self->size) {
        return '\0';
    }

    return self->data[index];
}

void string_clear(String *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (self->size > 0) {
        char_clear_2(self->data, self->size);

        self->size = 0;
    }

    trace_log_pop();
}

bool string_compare_equal_1(String const *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    bool const match = string_compare_equal_2(self, data, char_length(data));

    trace_log_pop();

    return match;
}

bool string_compare_equal_2(String const *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Empty is data, not a caller bug. Settled here rather than delegated
     * because an empty String carries data == nullptr, and char_compare_equal_2
     * still (correctly) rejects a null pointer. Two empties are equal; an empty
     * and a non-empty are not. */
    if (self->size == 0 || data_size == 0) {
        bool const empty_match = self->size == data_size;

        trace_log_pop();

        return empty_match;
    }

    bool const match = char_compare_equal_2(self->data, self->size, (char*) data, data_size);

    trace_log_pop();

    return match;
}

bool string_compare_equal_3(String const *const self, Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which string_compare_equal_2
     * still (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    bool const match = string_compare_equal_2(self, data_buffer, data->size);

    trace_log_pop();

    return match;
}

bool string_compare_equal_4(String const *const self, String const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which string_compare_equal_2
     * still (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    bool const match = string_compare_equal_2(self, data_buffer, data->size);

    trace_log_pop();

    return match;
}

bool string_compare_equal_comptime_1(String const *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    bool const match = string_compare_equal_comptime_2(self, data, char_length(data));

    trace_log_pop();

    return match;
}

bool string_compare_equal_comptime_2(String const *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Settled here rather than delegated because an empty String carries
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

bool string_compare_equal_comptime_3(String const *const self, Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which
     * string_compare_equal_comptime_2 still (correctly) rejects; substitute a
     * valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    bool const match = string_compare_equal_comptime_2(self, data_buffer, data->size);

    trace_log_pop();

    return match;
}

bool string_compare_equal_comptime_4(String const *const self, String const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which
     * string_compare_equal_comptime_2 still (correctly) rejects; substitute a
     * valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    bool const match = string_compare_equal_comptime_2(self, data_buffer, data->size);

    trace_log_pop();

    return match;
}

bool string_compare_iequal_1(String const *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    bool const match = string_compare_iequal_2(self, data, char_length(data));

    trace_log_pop();

    return match;
}

bool string_compare_iequal_2(String const *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* See string_compare_equal_2: empty is data, and an empty String's data is null. */
    if (self->size == 0 || data_size == 0) {
        bool const empty_match = self->size == data_size;

        trace_log_pop();

        return empty_match;
    }

    bool const match = char_compare_iequal_2(self->data, self->size, data, data_size);

    trace_log_pop();

    return match;
}

bool string_compare_iequal_3(String const *const self, Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which string_compare_iequal_2
     * still (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    bool const match = string_compare_iequal_2(self, data_buffer, data->size);

    trace_log_pop();

    return match;
}

bool string_compare_iequal_4(String const *const self, String const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which string_compare_iequal_2
     * still (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    bool const match = string_compare_iequal_2(self, data_buffer, data->size);

    trace_log_pop();

    return match;
}

bool string_compare_iequal_comptime_1(String const *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    bool const match = string_compare_iequal_comptime_2(self, data, char_length(data));

    trace_log_pop();

    return match;
}

bool string_compare_iequal_comptime_2(String const *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Settled here rather than delegated because an empty String carries
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

bool string_compare_iequal_comptime_3(String const *const self, Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which
     * string_compare_iequal_comptime_2 still (correctly) rejects; substitute a
     * valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    bool const match = string_compare_iequal_comptime_2(self, data_buffer, data->size);

    trace_log_pop();

    return match;
}

bool string_compare_iequal_comptime_4(String const *const self, String const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which
     * string_compare_iequal_comptime_2 still (correctly) rejects; substitute a
     * valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    bool const match = string_compare_iequal_comptime_2(self, data_buffer, data->size);

    trace_log_pop();

    return match;
}

bool string_contains_1(String const *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    bool const value = string_contains_2(self, data, char_length(data));

    trace_log_pop();

    return value;
}

bool string_contains_2(String const *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Settled here rather than delegated because an empty String carries
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

bool string_contains_3(String const *const self, Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which string_contains_2 still
     * (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    bool const value = string_contains_2(self, data_buffer, data->size);

    trace_log_pop();

    return value;
}

bool string_contains_4(String const *const self, String const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which string_contains_2 still
     * (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    bool const value = string_contains_2(self, data_buffer, data->size);

    trace_log_pop();

    return value;
}

void string_copy(String *const self, String const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    if (self->capacity > data->size) {
        /* An empty data carries data->data == nullptr, which char_copy_2 still
         * (correctly) rejects; substitute a valid empty source. */
        char const *const data_buffer = data->size == 0 ? "" : data->data;

        char_copy_2(self->data, data_buffer, data->size);

        self->data[data->size] = '\0';
        self->size = data->size;
    }
    else {
        /* Build the copy BEFORE releasing self: on a refused arena the constructor degrades to the
         * empty String, and the old content must survive that refusal (the string_reserve shape). */
#ifdef ARENA_IMPLEMENTATION
        String const copy = memory_empty(self->allocator) ? string_init_6(data) : string_alloc_init_6(data, self->allocator);
#else
        String const copy = string_init_6(data);
#endif // ARENA_IMPLEMENTATION

        if (data->size > 0 && memory_empty(copy.data)) {
            trace_log_pop();

            return;
        }

        string_uninit(self);

        *self = copy;
    }

    trace_log_pop();
}

void string_delete(String **const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "*self", (void*) *self);

    /* Captured before the uninit so the struct goes back to whatever allocator produced
     * it: string_alloc_new_* borrows the struct itself from the arena, and memory_delete
     * would hand an interior pointer of the arena's block to free(). */
#ifdef ARENA_IMPLEMENTATION
    Arena *const allocator = (*self)->allocator;

    string_uninit(*self);

    if (!memory_empty(allocator)) {
        allocator_release((void*) *self, allocator);

        *self = nullptr;
    }
    else {
        memory_delete((void**) self);
    }
#else
    string_uninit(*self);

    memory_delete((void**) self);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();
}

bool string_empty(String const *const self) {
    error_check_null(LOG_METADATA, "self", (void*) self);

    return self->size == 0;
}

bool string_ends_with_1(String const *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    bool const value = string_ends_with_2(self, data, char_length(data));

    trace_log_pop();

    return value;
}

bool string_ends_with_2(String const *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Settled here rather than delegated because an empty String carries
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

bool string_ends_with_3(String const *const self, Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which string_ends_with_2 still
     * (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    bool const value = string_ends_with_2(self, data_buffer, data->size);

    trace_log_pop();

    return value;
}

bool string_ends_with_4(String const *const self, String const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which string_ends_with_2 still
     * (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    bool const value = string_ends_with_2(self, data_buffer, data->size);

    trace_log_pop();

    return value;
}

void string_erase(String *const self, USize const from, USize const to) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* from/to are regex-match-shaped values (main_pcre2 passes match.begin/end
     * directly), so an out-of-range span refuses in every build. `to` is EXCLUSIVE,
     * matching str_erase - this body used to pass `to` straight into char_erase_2's
     * INCLUSIVE parameter (removing one byte more than the convention says) while
     * decrementing size by only to - from, so bytes and size disagreed after every
     * call. from == to is the legal empty erase, [size, size) included. */
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

void string_fill(String *const self, char const c) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (!string_empty(self)) {
        char_fill(self->data, self->size, c);
    }

    trace_log_pop();
}

USize string_find_1(String const *const self, USize const self_index, char const *const data) {
    trace_log_push(LOG_METADATA);

    USize const value = string_find_2(self, self_index, data, char_length(data));

    trace_log_pop();

    return value;
}

USize string_find_2(String const *const self, USize const self_index, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Settled here rather than delegated because an empty String carries
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

USize string_find_3(String const *const self, USize const self_index, Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which string_find_2 still
     * (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    USize const value = string_find_2(self, self_index, data_buffer, data->size);

    trace_log_pop();

    return value;
}

USize string_find_4(String const *const self, USize const self_index, String const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which string_find_2 still
     * (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    USize const value = string_find_2(self, self_index, data_buffer, data->size);

    trace_log_pop();

    return value;
}

USize string_find_any_1(String const *const self, char const *const set) {
    trace_log_push(LOG_METADATA);

    USize const value = string_find_any_2(self, set, char_length(set));

    trace_log_pop();

    return value;
}

USize string_find_any_2(String const *const self, char const *const set, USize const set_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "set", (void*) set);

    /* Settled here rather than delegated because an empty String carries
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

USize string_find_count_1(String const *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    USize const value = string_find_count_2(self, data, char_length(data));

    trace_log_pop();

    return value;
}

USize string_find_count_2(String const *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Settled here rather than delegated because an empty String carries
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

bool string_find_exists_1(String const *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    bool const value = string_find_exists_2(self, data, char_length(data));

    trace_log_pop();

    return value;
}

bool string_find_exists_2(String const *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Settled here rather than delegated because an empty String carries
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

bool string_find_exists_3(String const *const self, Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which string_find_exists_2
     * still (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    bool const value = string_find_exists_2(self, data_buffer, data->size);

    trace_log_pop();

    return value;
}

bool string_find_exists_4(String const *const self, String const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which string_find_exists_2
     * still (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    bool const value = string_find_exists_2(self, data_buffer, data->size);

    trace_log_pop();

    return value;
}

USize string_find_reverse_1(String const *const self, USize const self_index, char const *const data) {
    trace_log_push(LOG_METADATA);

    USize const value = string_find_reverse_2(self, self_index, data, char_length(data));

    trace_log_pop();

    return value;
}

USize string_find_reverse_2(String const *const self, USize const self_index, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Settled here rather than delegated because an empty String carries
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

void string_format(String *const self, char const *const format, ...) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "format", (void*) format);

    /* No data/capacity precondition: on the default empty String (data == nullptr,
     * capacity 0) the first render below is vsnprintf(nullptr, 0, ...) - the
     * standard MEASURING call - and the grow path then allocates. The growable
     * formatter must not abort on the growable type's own default state. */

    va_list args;

    va_start(args, format);

    va_list args_copy;

    va_copy(args_copy, args);

    ISize const size = (ISize) vsnprintf(self->data, (size_t) self->capacity, format, args);

    /* Only a NEGATIVE return is an encoding error: a zero-length render is the empty result and
     * must still set size, or the bytes would read "" while size kept the old payload. */
    if (size < 0) {
        va_end(args_copy);
        va_end(args);

        trace_log_pop();

        return;
    }

    /* vsnprintf returns the FULL rendered length; the error_check that stood here
     * aborted when it exceeded capacity - and the length is data (in the live
     * consumer it carried a User-Agent header), so a long input was a remote
     * process kill. String is the GROWABLE type: grow and re-render from the
     * va_copy captured above (the first render consumed `args`). A view grown
     * here becomes an owner - string_reserve's documented behavior. */
    if ((USize) size >= self->capacity) {
        string_reserve(self, (USize) size + CHAR_END_CHARACTER);

        /* A refused arena leaves capacity unchanged; keep the truncated render
         * vsnprintf already produced (capacity - 1 bytes, terminated) rather than
         * abort or return garbage. */
        if ((USize) size >= self->capacity) {
            string_set_size(self, self->capacity == 0 ? 0 : self->capacity - CHAR_END_CHARACTER);

            va_end(args_copy);
            va_end(args);

            trace_log_pop();

            return;
        }

        vsnprintf(self->data, (size_t) self->capacity, format, args_copy);
    }

    string_set_size(self, (USize) size);

    va_end(args_copy);
    va_end(args);

    trace_log_pop();
}

String string_from_numbers_float_1(FSize const number) {
    trace_log_push(LOG_METADATA);

    char buffer[_STRING_FROM_NUMBERS_FLOAT_BUFFER_SIZE] = DEFAULT_INITIALIZATION;

    char_from_numbers_float_1(buffer, sizeof(buffer), number);

    String const string = string_init_static(buffer, char_length(buffer));

    trace_log_pop();

    return string;
}

String string_from_numbers_float_2(FSize const number, U8 const precision) {
    trace_log_push(LOG_METADATA);

    char buffer[_STRING_FROM_NUMBERS_FLOAT_BUFFER_SIZE] = DEFAULT_INITIALIZATION;

    char_from_numbers_float_2(buffer, sizeof(buffer), number, precision);

    String const string = string_init_static(buffer, char_length(buffer));

    trace_log_pop();

    return string;
}

String string_from_numbers_int_1(ISize const number) {
    trace_log_push(LOG_METADATA);

    String const string = string_from_numbers_int_2(number, 0);

    trace_log_pop();

    return string;
}

String string_from_numbers_int_2(ISize const number, U8 const padding) {
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    String const string = _string_from_numbers_int(number, padding, nullptr);
#else
    String const string = _string_from_numbers_int(number, padding);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return string;
}

String string_from_numbers_uint_1(USize const number) {
    trace_log_push(LOG_METADATA);

    String const string = string_from_numbers_uint_2(number, 0);

    trace_log_pop();

    return string;
}

String string_from_numbers_uint_2(USize const number, U8 const padding) {
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    String const string = _string_from_numbers_uint(number, padding, nullptr);
#else
    String const string = _string_from_numbers_uint(number, padding);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return string;
}

String string_from_replace_1(String const *const self, char const *const find, char const *const replace) {
    trace_log_push(LOG_METADATA);

    String const string = string_from_replace_2(self, find, char_length(find), replace, char_length(replace));

    trace_log_pop();

    return string;
}

String string_from_replace_2(String const *const self, char const *const find, USize const find_size, char const *const replace, USize const replace_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "find", (void*) find);
    error_check_null(LOG_METADATA, "replace", (void*) replace);

    /* Nothing in an empty String can be replaced, and char_new_replace_2 rejects
     * the nullptr one carries. An empty find pattern is not settled here: it
     * reaches the replace helper, which answers it with a verbatim copy. */
    if (self->size == 0) {
        String const empty = string_init_1();

        trace_log_pop();

        return empty;
    }

    char *const data = char_new_replace_2(self->data, self->size, find, find_size, replace, replace_size);
    String buffer = string_init_1();

    if (!memory_empty(data)) {
#ifdef ARENA_IMPLEMENTATION
        buffer = _string_adopt(data, char_length(data), nullptr);
#else
        buffer = _string_adopt(data, char_length(data));
#endif // ARENA_IMPLEMENTATION
    }

    trace_log_pop();

    return buffer;
}

String string_from_trim(String const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* Trimming an empty String is empty; char_from_trim_2 rejects the nullptr it
     * carries. Mirrors str_from_trim - the twins must not disagree on empty. */
    if (self->size == 0) {
        String const empty = string_init_1();

        trace_log_pop();

        return empty;
    }

    char *const data = char_from_trim_2(self->data, self->size);
    String buffer = string_init_1();

    if (!memory_empty(data)) {
        USize const length = char_length(data);

        buffer.capacity = length + CHAR_END_CHARACTER;
        buffer.data = data;
        buffer.owned = true;
        buffer.size = length;
    }

    trace_log_pop();

    return buffer;
}

USize string_get_capacity(String const *const self) {
    error_check_null(LOG_METADATA, "self", (void*) self);

    return self->capacity;
}

char* string_get_data(String const *const self) {
    error_check_null(LOG_METADATA, "self", (void*) self);

    return self->data;
}

USize string_get_size(String const *const self) {
    error_check_null(LOG_METADATA, "self", (void*) self);

    return self->size;
}

String string_init_1(void) {
#ifdef ARENA_IMPLEMENTATION
    String const string = _string_init(nullptr);
#else
    String const string = _string_init();
#endif // ARENA_IMPLEMENTATION

    return string;
}

String string_init_2(USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    String string = string_init_1();

    string.capacity = capacity;

#ifdef ARENA_IMPLEMENTATION
    string.data = (char*) allocator_borrow(string.capacity, nullptr);
    string.owned = true;
#else
    string.data = (char*) allocator_borrow(string.capacity);
    string.owned = true;
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return string;
}

String string_init_3(char *const data) {
    trace_log_push(LOG_METADATA);

    String const string = string_init_4(data, char_length(data));

    trace_log_pop();

    return string;
}

String string_init_4(char *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    String string = string_init_1();

    /* A data_size of USIZE_MAX would wrap the claimed capacity to 0 under a size of USIZE_MAX:
     * refused to the EMPTY String. */
    if (data_size == USIZE_MAX) {
        trace_log_pop();

        return string;
    }

    /* A VIEW over caller memory - see the ownership note on String in string.h.
     * Producing functions inside this module use _string_adopt instead. */
    string.capacity    = data_size + CHAR_END_CHARACTER;
    string.data        = data;
    string.size        = data_size;
    string.owned       = false;

    trace_log_pop();

    return string;
}

String string_init_5(Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which string_init_static still
     * (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    String const string = string_init_static(data_buffer, data->size);

    trace_log_pop();

    return string;
}

String string_init_6(String const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which string_init_static still
     * (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    String const string = string_init_static(data_buffer, data->size);

    trace_log_pop();

    return string;
}

String string_init_static(char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty source yields the plain empty String; string_init_2 rejects a
     * zero capacity, and String owns its buffer so there is nothing to copy. */
    if (data_size == 0) {
        String const empty = string_init_1();

        trace_log_pop();

        return empty;
    }

    String string   = string_init_2(data_size + CHAR_END_CHARACTER);
    string.size     = data_size;

    char_copy_3(string.data, string.capacity, data, data_size);

    // The trailing slot is fresh - char_copy_3 writes exactly data_size bytes - and this
    // buffer is always heap-backed, because string_init_2 borrows with a nullptr
    // allocator and so never takes the arena path the exclusion is written for. The
    // arena twin below keeps its write unconditional for exactly that reason.

    trace_log_pop();

    return string;
}

String string_join_1(String const *const *const parts, USize const count, char const *const separator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "separator", (void*) separator);

    String const string = string_join_2(parts, count, separator, char_length(separator));

    trace_log_pop();

    return string;
}

String string_join_2(String const *const *const parts, USize const count, char const *const separator, USize const separator_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "parts", count > 0 ? (void const*) parts : (void const*) 1);
    error_check_null(LOG_METADATA, "separator", (void*) separator);

    /* Joining 0 parts yields the empty result; the loop below simply doesn't run. */
    String string = string_init_1();

    /* One pre-sized reserve instead of a growth per append - and on a refused arena the whole join
     * is refused (the EMPTY String), never a truncated one that reads as success. */
    bool overflow = count > 1 && separator_size > 0 && count - 1 > (USIZE_MAX - CHAR_END_CHARACTER) / separator_size;
    USize total = count > 1 && !overflow ? (count - 1) * separator_size : 0;

    for (USize i = 0; i < count && !overflow; i += 1) {
        error_check_null(LOG_METADATA, "parts[i]", (void*) parts[i]);

        overflow = parts[i]->size > USIZE_MAX - CHAR_END_CHARACTER - total;
        total += overflow ? 0 : parts[i]->size;
    }

    if (overflow) {
        trace_log_pop();

        return string;
    }

    if (total > 0) {
        string_reserve(&string, total + CHAR_END_CHARACTER);

        if (string.capacity < total + CHAR_END_CHARACTER) {
            trace_log_pop();

            return string;
        }
    }

    for (USize i = 0; i < count; i += 1) {
        if (parts[i]->size > 0) {
            string_add_last_4(&string, parts[i]);
        }

        if (separator_size > 0 && i + 1 < count) {
            string_add_last_2(&string, separator, separator_size);
        }
    }

    trace_log_pop();

    return string;
}

void string_lower(String *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (!string_empty(self)) {
        char_lower_2(self->data, self->size);
    }

    trace_log_pop();
}

void string_move_1(String *const self, char **const data) {
    trace_log_push(LOG_METADATA);

    /* *data is read by char_length below, so these must run HERE - the _2 body's
     * checks sit after the deref and the diagnostic pointed at char_length. */
    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_null(LOG_METADATA, "*data", (void*) *data);

    string_move_2(self, data, char_length(*data));

    trace_log_pop();
}

void string_move_2(String *const self, char **const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_null(LOG_METADATA, "*data", (void*) *data);

    /* A data_size of USIZE_MAX would wrap the claimed capacity to 0: REFUSED as a no-op, self
     * and the source pointer both untouched. */
    if (data_size == USIZE_MAX) {
        trace_log_pop();

        return;
    }

    string_uninit(self);

    /* An owned buffer of size 0 is safe to store: _string_uninit keys on the ownership
     * flag, so it is released either way - unlike Str, whose empty owners are avoided
     * at the source. Callers wanting a VIEW use string_init_4, not a move. */
    self->capacity  = data_size + CHAR_END_CHARACTER;
    self->data      = *data;
    self->owned     = true;
    self->size      = data_size;

    *data = nullptr;

    trace_log_pop();
}

void string_move_3(String *const self, Str **const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_null(LOG_METADATA, "*data", (void*) *data);

    /* Captured before the move: string_move_2 always claims ownership, so a Str that
     * was only a VIEW would come out the other side marked owned and its borrowed
     * buffer - an lws payload, a literal - would be released by string_uninit. */
    bool const data_owned = (*data)->owned;

#ifdef ARENA_IMPLEMENTATION
    /* `allocator` does double duty: _string_uninit reads it to release the BUFFER, and
     * string_delete reads it to release the STRUCT. A move therefore cannot simply adopt the
     * source's - that would rewrite where the destination's own struct is believed to
     * live, and string_delete would hand an arena interior pointer to free(). Crossing
     * allocators is REFUSED, as the header promises - a real no-op leaving both
     * objects untouched. (An error_check stood here; it aborted.) */
    if ((*data)->allocator != self->allocator) {
        trace_log_pop();

        return;
    }
#endif // ARENA_IMPLEMENTATION

    /* An EMPTY Str (data nullptr, the family's convention for an empty value - string_add_3
     * and string_split_3 substitute around the same nullptr) is a legal VALUE, not a refused
     * move: string_move_2's error_check_null on *data would abort on it. Route it directly -
     * self's old buffer is released and self becomes the EMPTY String too, nothing to adopt. */
    if (memory_empty((*data)->data)) {
        string_uninit(self);

        self->capacity = 0;
        self->data     = nullptr;
        self->owned    = false;
        self->size     = 0;

        (*data)->size  = 0;
        (*data)->owned = false;
        *data = nullptr;

        trace_log_pop();

        return;
    }

    string_move_2(self, &(*data)->data, (*data)->size);

    /* A refused move_2 (size USIZE_MAX) leaves the source pointer set: nothing changed hands,
     * so neither the ownership flag nor the source may be rewritten. */
    if (!memory_empty((*data)->data)) {
        trace_log_pop();

        return;
    }

    self->owned = data_owned;

    (*data)->size  = 0;
    (*data)->owned = false;
    *data = nullptr;

    trace_log_pop();
}

void string_move_4(String *const self, String **const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_null(LOG_METADATA, "*data", (void*) *data);

#ifdef ARENA_IMPLEMENTATION
    /* `allocator` does double duty: _string_uninit reads it to release the BUFFER, and
     * string_delete reads it to release the STRUCT. A move therefore cannot simply adopt the
     * source's - that would rewrite where the destination's own struct is believed to
     * live, and string_delete would hand an arena interior pointer to free(). Crossing
     * allocators is REFUSED, as the header promises - checked BEFORE the uninit
     * below, so a refusal cannot destroy the destination first. (An error_check
     * stood here, after the uninit: it aborted a half-destroyed pair.) */
    if ((*data)->allocator != self->allocator) {
        trace_log_pop();

        return;
    }
#endif // ARENA_IMPLEMENTATION

    string_uninit(self);

    /* Ownership transfers with the buffer: moving a view yields a view. */
    self->capacity  = (*data)->capacity;
    self->data      = (*data)->data;
    self->owned     = (*data)->owned;
    self->size      = (*data)->size;

    /* The SOURCE is cleared, not just the caller's pointer variable: leaving it with a
     * live data pointer and owned still set makes any retained alias a second owner of
     * the same buffer, so the next string_uninit is a double free. Mirrors str_move_3. */
    /* allocator is deliberately NOT cleared: it records where the STRUCT was borrowed
     * from, which string_delete reads to decide how to release it. Clearing data/owned
     * is what stops the source from claiming the buffer. */
    (*data)->capacity = 0;
    (*data)->data     = nullptr;
    (*data)->owned    = false;
    (*data)->size     = 0;

    *data = nullptr;

    trace_log_pop();
}

String* string_new_1(void) {
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    String *const string = (String*) allocator_borrow(sizeof(String), nullptr);
#else
    String *const string = (String*) allocator_borrow(sizeof(String));
#endif // ARENA_IMPLEMENTATION

    *string = string_init_1();

    trace_log_pop();

    return string;
}

String* string_new_2(USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    String *const string = string_new_1();

    *string = string_init_2(capacity);

    trace_log_pop();

    return string;
}

String* string_new_3(char *const data) {
    trace_log_push(LOG_METADATA);

    String const *const string = string_new_4(data, char_length(data));

    trace_log_pop();

    return (String*) string;
}

String* string_new_4(char *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    String *const string = string_new_1();

    /* A VIEW, matching string_init_4: data comes from the CALLER, so this is not an
     * adopt. _string_adopt is only for buffers this module allocated. */
    *string = string_init_4(data, data_size);

    trace_log_pop();

    return string;
}

String* string_new_5(Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which string_new_static still
     * (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    String const *const string = string_new_static(data_buffer, data->size);

    trace_log_pop();

    return (String*) string;
}

String* string_new_6(String const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which string_new_static still
     * (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    String const *const string = string_new_static(data_buffer, data->size);

    trace_log_pop();

    return (String*) string;
}

String* string_new_static(char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    String *const string = string_new_1();

    *string = string_init_static(data, data_size);

    trace_log_pop();

    return string;
}

void string_print(String const *const self, char const *const label, bool const log) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "label", (void*) label);

    int const display_size = self->size <= (USize) I32_MAX ? (int) self->size : I32_MAX;
    /* The empty String carries data == nullptr; %.*s with a null pointer is undefined even at 0. */
    char const *const bytes = self->data == nullptr ? "" : self->data;

    if (log) {
        log_message_2(LOG_LEVEL_INFO, LOG_METADATA, "String %s:\n", label);
        log_message_2(LOG_LEVEL_INFO, LOG_METADATA, "\tcapacity: %zu\n", (size_t) self->capacity);
        log_message_2(LOG_LEVEL_INFO, LOG_METADATA, "\tdata    : %.*s\n", display_size, bytes);
        log_message_2(LOG_LEVEL_INFO, LOG_METADATA, "\tsize    : %zu\n", (size_t) self->size);
    }
    else {
        printf("String %s:\n", label);
        printf("\tcapacity: %zu\n", (size_t) self->capacity);
        printf("\tdata    : %.*s\n", display_size, bytes);
        printf("\tsize    : %zu\n", (size_t) self->size);
    }

    trace_log_pop();
}

void string_remove(String *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* index is data-shaped (a parsed position, an unchecked find result); refuse in
     * every build. Also covers the empty String, whose size decrement below would
     * otherwise wrap to USIZE_MAX in an unchecked build. */
    if (index >= self->size) {
        trace_log_pop();

        return;
    }

    for (USize i = index; i + 1 < self->size; i += 1) {
        self->data[i] = self->data[i + 1];
    }

    self->size -= 1;

    /* Unconditional: size <= capacity - 1 is the invariant, so the lowered size is at most
     * capacity - 2 and the terminator slot is always inside the claimed capacity. */
    self->data[self->size] = '\0';

    trace_log_pop();
}

void string_repeat(String *const self, USize const count) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (string_empty(self)) {
        trace_log_pop();

        return;
    }

    /* Repeating 0 times yields the empty result: the overflow check below passes
     * trivially (0 never overflows), the copy loop below doesn't run, and the size
     * simply collapses to self->size * 0 == 0, leaving self a valid owned empty
     * String rather than aborting. */
    error_check_out_of_bound_uint(LOG_METADATA, "count", count, "(USIZE_MAX - CHAR_END_CHARACTER) / self->size", (USIZE_MAX - CHAR_END_CHARACTER) / self->size,
        "self->size * count + CHAR_END_CHARACTER overflows USize", count > (USIZE_MAX - CHAR_END_CHARACTER) / self->size);

    USize const unit = self->size;

    string_reserve(self, unit * count + CHAR_END_CHARACTER);

    /* A refused arena leaves capacity unchanged; the copy loop below would then
     * write unit * count bytes into the old, smaller buffer - on a VIEW that is
     * the CALLER's adjacent memory. Refuse wholly (the add_2/format shape). */
    if (unit * count + CHAR_END_CHARACTER > self->capacity) {
        trace_log_pop();

        return;
    }

    for (USize i = 1; i < count; i += 1) {
        char_copy_2(self->data + unit * i, self->data, unit);
    }

    self->size = unit * count;
    self->data[self->size] = '\0';

    trace_log_pop();
}

void string_replace_1(String *const self, char const *const data, USize const index) {
    trace_log_push(LOG_METADATA);

    string_replace_2(self, data, char_length(data), index);

    trace_log_pop();
}

void string_replace_2(String *const self, char const *const data, USize const data_size, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Repeated as real control flow because error_check_* compiles away without
     * ERROR_CHECK_ENABLED, and the loop below writes data_size bytes into the
     * destination with nothing else bounding it - a heap overflow whose length and
     * contents both come from the caller. Its sibling str_replace_2 checks this.
     *
     * The bound deliberately does NOT reserve a byte for the terminator: this function
     * does not terminate, and string_add_2 relies on being able to fill capacity
     * exactly before growing by CHAR_END_CHARACTER and writing the terminator itself.
     * A caller invoking this directly on a full buffer therefore has to re-terminate,
     * or leave self->size describing fewer bytes than it wrote. */
    if (index >= self->capacity || data_size > self->capacity - index) {
        trace_log_pop();

        return;
    }

    /* Writing 0 bytes is a no-op; the loop below simply doesn't run. */
    for (USize i = 0; i < data_size; i += 1) {
        self->data[index + i] = data[i];
    }

    trace_log_pop();
}

void string_replace_3(String *const self, Str const *const data, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which string_replace_2 still
     * (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    string_replace_2(self, data_buffer, data->size, index);

    trace_log_pop();
}

void string_replace_4(String *const self, String const *const data, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which string_replace_2 still
     * (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    string_replace_2(self, data_buffer, data->size, index);

    trace_log_pop();
}

void string_reserve(String *const self, USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    if (capacity > self->capacity) {
        char *buffer = self->data;
        /* Captured BEFORE the flag is overwritten. Growing a VIEW copies the borrowed
         * bytes into a buffer this String does own, but the ORIGINAL must not be
         * released - it belongs to the caller. */
        bool const buffer_owned = self->owned;

#ifdef ARENA_IMPLEMENTATION
        char *const grown = (char*) allocator_borrow(capacity, self->allocator);
#else
        char *const grown = (char*) allocator_borrow(capacity);
#endif // ARENA_IMPLEMENTATION

        /* Null only from a REFUSED arena. self still holds its old buffer and
         * capacity untouched, so refusing here leaves the object fully intact -
         * assigning first (as this code once did) grew capacity, nulled data and
         * marked it owned before anything could notice. */
        if (memory_empty(grown)) {
            trace_log_pop();

            return;
        }

        self->capacity = capacity;
        self->data = grown;
        self->owned = true;

        if (self->size > 0) {
            char_copy_2(self->data, buffer, self->size);
        }

        /* Unconditional (the terminator rule): the grown buffer can be arena-backed, and only
         * memory_alloc owes the zero fill. */
        self->data[self->size] = '\0';

        if (buffer_owned && !memory_empty(buffer)) {
#ifdef ARENA_IMPLEMENTATION
            allocator_release(buffer, self->allocator);
#else
            allocator_release(buffer);
#endif // ARENA_IMPLEMENTATION
        }
    }

    trace_log_pop();
}

void string_reverse(String *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (!string_empty(self)) {
        char_reverse_2(self->data, self->size);
    }

    trace_log_pop();
}

void string_set_size(String *const self, USize const size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* A size the buffer cannot hold (past capacity - 1, or non-zero on the empty String) would
     * send every size-bounded reader and writer off the end; the value is data (a count handed
     * back by an external writer), so it is REFUSED as a no-op in every build. */
    if (self->capacity == 0 ? size != 0 : size > self->capacity - CHAR_END_CHARACTER) {
        trace_log_pop();

        return;
    }

    self->size = size;

    trace_log_pop();
}

void string_shrink(String *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* Shrinking to nothing would ask allocator_borrow for 0 bytes, which memory_alloc
     * rejects outright. Release and reset instead - the result is the same empty
     * String, without the abort. */
    if (self->size == 0) {
        string_uninit(self);

        trace_log_pop();

        return;
    }

    /* Exact fit already (capacity == size + terminator): nothing to gain, and a second shrink
     * used to reallocate and copy for nothing - and promote an exact-fit VIEW. */
    if (self->size + CHAR_END_CHARACTER < self->capacity) {
        char *buffer = self->data;
        /* Captured before the flag is overwritten - shrinking a VIEW must copy the
         * borrowed bytes out without releasing the caller's buffer. */
        bool const buffer_owned = self->owned;

        /* The terminator slot is part of the capacity. Shrinking to exactly self->size
         * left no room for it and copied only self->size bytes, so string_get_data came
         * back unterminated and char_length ran into whatever followed the allocation. */
#ifdef ARENA_IMPLEMENTATION
        char *const shrunk = (char*) allocator_borrow(self->size + CHAR_END_CHARACTER, self->allocator);
#else
        char *const shrunk = (char*) allocator_borrow(self->size + CHAR_END_CHARACTER);
#endif // ARENA_IMPLEMENTATION

        /* Null only from a REFUSED arena; keeping the roomier buffer is a correct
         * no-op, corrupting the object is not. */
        if (memory_empty(shrunk)) {
            trace_log_pop();

            return;
        }

        self->capacity = self->size + CHAR_END_CHARACTER;
        self->data = shrunk;
        self->owned = true;

        char_copy_2(self->data, buffer, self->size);

        self->data[self->size] = '\0';

        if (buffer_owned && !memory_empty(buffer)) {
#ifdef ARENA_IMPLEMENTATION
            allocator_release(buffer, self->allocator);
#else
            allocator_release(buffer);
#endif // ARENA_IMPLEMENTATION
        }
    }

    trace_log_pop();
}

String string_slice(String const *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "self->size", self->size, "index > self->size", index > self->size);

    /* index == size is the tail after the last delimiter - the empty String, a
     * VALUE (#941 precedent). Also covers the empty source. Only strictly past the
     * end remains caller error. */
    if (index >= self->size) {
#ifdef ARENA_IMPLEMENTATION
        String const empty = memory_empty(self->allocator) ? string_init_1() : string_alloc_init_1(self->allocator);
#else
        String const empty = string_init_1();
#endif // ARENA_IMPLEMENTATION

        trace_log_pop();

        return empty;
    }

    /* An OWNED copy carrying the source's allocator, mirroring string_slice_range. */
#ifdef ARENA_IMPLEMENTATION
    String const string = memory_empty(self->allocator)
        ? string_init_static(self->data + index, self->size - index)
        : string_alloc_init_static(self->data + index, self->size - index, self->allocator);
#else
    String const string = string_init_static(self->data + index, self->size - index);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return string;
}

String string_slice_range(String const *const self, USize const from, USize const to) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_out_of_bound_uint(LOG_METADATA, "from", from, "to", to, "from > to", from > to);
    error_check_out_of_bound_uint(LOG_METADATA, "from", from, "self->size", self->size, "from >= self->size", from >= self->size);
    error_check_out_of_bound_uint(LOG_METADATA, "to", to, "self->size", self->size, "to >= self->size", to >= self->size);

    USize const buffer_size = to - from + 1;
#ifdef ARENA_IMPLEMENTATION
    char *buffer = (char*) allocator_borrow(buffer_size + CHAR_END_CHARACTER, self->allocator);
#else
    char *buffer = (char*) allocator_borrow(buffer_size + CHAR_END_CHARACTER);
#endif // ARENA_IMPLEMENTATION

    /* Null only from a REFUSED arena; degrade to the empty String carrying the
     * source's allocator. */
    if (memory_empty(buffer)) {
#ifdef ARENA_IMPLEMENTATION
        String const empty = memory_empty(self->allocator) ? string_init_1() : string_alloc_init_1(self->allocator);
#else
        String const empty = string_init_1();
#endif // ARENA_IMPLEMENTATION

        trace_log_pop();

        return empty;
    }

    for (USize i = 0; i < buffer_size; i += 1) {
        buffer[i] = self->data[from + i];
    }

    buffer[buffer_size] = '\0';

#ifdef ARENA_IMPLEMENTATION
    String const string = _string_adopt(buffer, buffer_size, self->allocator);
#else
    String const string = _string_adopt(buffer, buffer_size);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return string;
}

String string_split_1(String const *const self, char const *const delimiter) {
    trace_log_push(LOG_METADATA);

    String const string = string_split_2(self, delimiter, char_length(delimiter));

    trace_log_pop();

    return string;
}

String string_split_2(String const *const self, char const *const delimiter, USize const delimiter_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "delimiter", (void*) delimiter);

    /* An empty delimiter marks no split point, so the whole string is the one
     * token. An empty self has nothing to split either way. Both are settled
     * here because char_find_3 rejects the nullptr an empty String carries. */
    USize const index = self->size == 0 || delimiter_size == 0 ? CHAR_NPOS : char_find_3(self->data, self->size, 0, (char*) delimiter, delimiter_size);
    USize const split_size = index == CHAR_NPOS ? self->size : index;

    /* An OWNED copy carrying the source's allocator, the slice idiom: a token cut from an arena
     * String is released in bulk with it, never individually on the heap. */
#ifdef ARENA_IMPLEMENTATION
    String const string = split_size == 0 ? (memory_empty(self->allocator) ? string_init_1() : string_alloc_init_1(self->allocator))
        : memory_empty(self->allocator) ? string_init_static(self->data, split_size) : string_alloc_init_static(self->data, split_size, self->allocator);
#else
    String const string = split_size == 0 ? string_init_1() : string_init_static(self->data, split_size);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return string;
}

String string_split_3(String const *const self, Str const *const delimiter) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "delimiter", (void*) delimiter);

    /* An empty delimiter carries delimiter->data == nullptr, which string_split_2
     * still (correctly) rejects; substitute a valid empty source. */
    char const *const delimiter_buffer = delimiter->size == 0 ? "" : delimiter->data;

    String const string = string_split_2(self, delimiter_buffer, delimiter->size);

    trace_log_pop();

    return string;
}

String string_split_4(String const *const self, String const *const delimiter) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "delimiter", (void*) delimiter);

    /* An empty delimiter carries delimiter->data == nullptr, which string_split_2
     * still (correctly) rejects; substitute a valid empty source. */
    char const *const delimiter_buffer = delimiter->size == 0 ? "" : delimiter->data;

    String const string = string_split_2(self, delimiter_buffer, delimiter->size);

    trace_log_pop();

    return string;
}

bool string_split_next(String const *const self, char const *const delimiter, USize const delimiter_size, USize *const index, USize *const token_from, USize *const token_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "delimiter", (void*) delimiter);
    error_check_null(LOG_METADATA, "index", (void*) index);
    error_check_null(LOG_METADATA, "token_from", (void*) token_from);
    error_check_null(LOG_METADATA, "token_size", (void*) token_size);

    /* Settled here rather than delegated because an empty String carries
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

bool string_starts_with_1(String const *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    bool const value = string_starts_with_2(self, data, char_length(data));

    trace_log_pop();

    return value;
}

bool string_starts_with_2(String const *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Settled here rather than delegated because an empty String carries
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

bool string_starts_with_3(String const *const self, Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which string_starts_with_2
     * still (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    bool const value = string_starts_with_2(self, data_buffer, data->size);

    trace_log_pop();

    return value;
}

bool string_starts_with_4(String const *const self, String const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty data carries data->data == nullptr, which string_starts_with_2
     * still (correctly) rejects; substitute a valid empty source. */
    char const *const data_buffer = data->size == 0 ? "" : data->data;

    bool const value = string_starts_with_2(self, data_buffer, data->size);

    trace_log_pop();

    return value;
}

FSize string_to_numbers_float(String const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* An empty String carries data == nullptr and a size of 0, both of which
     * char_to_numbers_float_2 rejects. Parsing nothing yields nothing. */
    if (self->size == 0) {
        trace_log_pop();

        return 0.0;
    }

    FSize const value = char_to_numbers_float_2(self->data, self->size);

    trace_log_pop();

    return value;
}

ISize string_to_numbers_int(String const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* An empty String carries data == nullptr and a size of 0, both of which
     * char_to_numbers_int_2 rejects. Parsing nothing yields nothing. */
    if (self->size == 0) {
        trace_log_pop();

        return 0;
    }

    ISize const value = char_to_numbers_int_2(self->data, self->size);

    trace_log_pop();

    return value;
}

USize string_to_numbers_uint(String const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* An empty String carries data == nullptr and a size of 0, both of which
     * char_to_numbers_uint_2 rejects. Parsing nothing yields nothing. */
    if (self->size == 0) {
        trace_log_pop();

        return 0;
    }

    USize const value = char_to_numbers_uint_2(self->data, self->size);

    trace_log_pop();

    return value;
}

void string_trim(String *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

#ifdef ARENA_IMPLEMENTATION
    if (!memory_empty(self->allocator)) {
        string_alloc_trim(self, self->allocator);

        trace_log_pop();

        return;
    }
#endif // ARENA_IMPLEMENTATION

    String const buffer = string_from_trim(self);

    string_uninit(self);

    *self = buffer;

    trace_log_pop();
}

void string_uninit(String *const self) {
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    _string_uninit(self, self->allocator);
#else
    _string_uninit(self);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();
}

void string_upper(String *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (!string_empty(self)) {
        char_upper_2(self->data, self->size);
    }

    trace_log_pop();
}

String string_wrap(String const *const self, USize const width) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_non_value_uint(LOG_METADATA, "width", width);

    /* The EMPTY result carries self's allocator on every branch, the split_2 shape: a caller
     * following the bulk-release contract must not be handed a heap-flavored String. */
#ifdef ARENA_IMPLEMENTATION
    String result = memory_empty(self->allocator) ? string_init_1() : string_alloc_init_1(self->allocator);
#else
    String result = string_init_1();
#endif // ARENA_IMPLEMENTATION

    if (!string_empty(self)) {
        char *wrapped            = char_wrap_2(self->data, self->size, width);
        USize const wrapped_size = char_length(wrapped);

        /* Adopt the buffer char_wrap_2 just heap-allocated instead of copying it
         * into a second allocation and freeing the first (a whole-document copy
         * saved per call). string_move_2 claims ownership; result.allocator stays
         * null, so uninit releases via free() - the matched pair. */
        if (wrapped_size == 0) {
            char_delete(wrapped);
        }
#ifdef ARENA_IMPLEMENTATION
        else if (!memory_empty(self->allocator)) {
            /* An arena String yields an arena result (released in bulk with the source): one copy
             * into the arena, the heap buffer char_wrap_2 built goes straight back. */
            result = string_alloc_init_static(wrapped, wrapped_size, self->allocator);

            char_delete(wrapped);
        }
#endif // ARENA_IMPLEMENTATION
        else {
            string_move_2(&result, &wrapped, wrapped_size);
        }
    }

    trace_log_pop();

    return result;
}