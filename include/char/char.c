/*==============================================================================
 * MARK: - Includes
 *============================================================================*/
#include <char/char.h>

/*==============================================================================
 * MARK: - Macros and Constants
 *============================================================================*/
#define _CHAR_BYTES_HUMAN_UNIT_DIVISOR 1024
/* The largest power of ten a USize holds is 10^(USIZE_DIGITS_MAX - 1): the fraction scale. */
#define _CHAR_FLOAT_PRECISION_MAX (USIZE_DIGITS_MAX - 1)
#define _CHAR_FROM_NUMBERS_F_PRECISION_DEFAULT 4

/*==============================================================================
 * MARK: - Static/Internal Functions
 *============================================================================*/
#ifdef ARENA_IMPLEMENTATION
static void _char_add(char **const self, USize const self_size, char const *const data, USize const data_size, USize const self_index, Arena *const allocator)
#else
static void _char_add(char **const self, USize const self_size, char const *const data, USize const data_size, USize const self_index)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "*self", *self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* The bounds are real control flow, not diagnostics: the index and the size arrive
     * from DATA (a find result, a request length), and the error_check that used to guard
     * them compiles away without ERROR_CHECK_ENABLED - leaving the tail restore below to
     * copy a wrapped self_size - self_index. Refuse with *self unchanged, the char_copy_3
     * shape. */
    if (self_index > self_size || self_size > USIZE_MAX - CHAR_END_CHARACTER || data_size > USIZE_MAX - self_size - CHAR_END_CHARACTER) {
        trace_log_pop();

        return;
    }

    /* Appending nothing changes nothing - the empty-value policy: only
     * capacity and allocation sizes are illegal at 0. This used to abort, so
     * any handler appending a possibly-empty request field was an abort
     * primitive (the char_compare_equal_1 class that killed a live webhook). */
    if (data_size == 0) {
        trace_log_pop();

        return;
    }

    char *const buffer = *self;
#ifdef ARENA_IMPLEMENTATION
    *self = (char*) allocator_borrow(self_size + data_size + CHAR_END_CHARACTER, allocator);
#else
    *self = (char*) allocator_borrow(self_size + data_size + CHAR_END_CHARACTER);
#endif // ARENA_IMPLEMENTATION

    /* A refused arena answers null: keep the caller's buffer in *self and change nothing. */
    if (memory_empty(*self)) {
        *self = buffer;

        trace_log_pop();

        return;
    }

    /* One splice covers every index, the append included: at self_index == self_size the
     * prefix is the whole string and the tail restore writes nothing but the terminator.
     * The old append special case fired at self_size - 1, so an insert before the LAST
     * character - char_add_first_* on a one-character string - appended instead. */
    if (self_index > 0) {
        char_copy_2(*self, buffer, self_index);
    }

    char_replace_2(*self, self_index, data, data_size);
    char_replace_2(*self, self_index + data_size, buffer + self_index, self_size - self_index);

#ifdef ARENA_IMPLEMENTATION
    allocator_release(buffer, allocator);
#else
    allocator_release(buffer);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();
}

#ifdef ARENA_IMPLEMENTATION
static void _char_add_fixed(char *const self, USize const self_capacity, USize const self_size, char const *const data, USize const data_size, USize const self_index, Arena *const allocator)
#else
static void _char_add_fixed(char *const self, USize const self_capacity, USize const self_size, char const *const data, USize const data_size, USize const self_index)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_non_value_uint(LOG_METADATA, "self_capacity", self_capacity);

    /* As in _char_add, plus the fit against the fixed capacity: whether the result fits
     * depends on the VALUE's length (a right-sized buffer fed an untrusted column, ruling
     * 931), so it refuses in every build rather than aborting in checked ones and writing
     * past the buffer in unchecked ones. The capacity itself stays an error_check above: a
     * zero-capacity buffer is an allocation-size mistake, not data. */
    if (self_index > self_size || self_size > USIZE_MAX - CHAR_END_CHARACTER || data_size > USIZE_MAX - self_size - CHAR_END_CHARACTER
        || self_size + data_size + CHAR_END_CHARACTER > self_capacity) {
        trace_log_pop();

        return;
    }

    /* See _char_add: appending nothing is a no-op, not an abort. */
    if (data_size == 0) {
        trace_log_pop();

        return;
    }

    /* A source inside self's own buffer is legal only when wholly inside the live bytes
     * [0, self_size): the insert path then reads it from the snapshot (the in-place replace
     * would read bytes it has just overwritten) and the append path is disjoint from it. One
     * that starts in the buffer but runs past self_size, or sits in the spare capacity, is
     * REFUSED as a no-op - the string_add_2 contract. */
    bool const data_aliases_self = data >= self && data < self + self_capacity;
    USize const alias_offset     = data_aliases_self ? (USize) (data - self) : 0;

    if (data_aliases_self && (alias_offset >= self_size || data_size > self_size - alias_offset)) {
        trace_log_pop();

        return;
    }

    if (self_size == 0) {
        char_copy_3(self, self_capacity, data, data_size);
    }
    else {
        /* The append needs no snapshot: nothing follows self_index. It fires at self_size, not
         * self_size - 1 - the old test turned an insert before the last character into one. */
        if (self_index == self_size) {
            char_replace_2(self, self_size, data, data_size);
        }
        else {
#ifdef ARENA_IMPLEMENTATION
            char *const buffer = (char*) allocator_borrow(self_capacity, allocator);
#else
            char *const buffer = (char*) allocator_borrow(self_capacity);
#endif // ARENA_IMPLEMENTATION

            /* A refused arena answers null: no snapshot, so no insert - self is unchanged. */
            if (memory_empty(buffer)) {
                trace_log_pop();

                return;
            }

            /* Snapshot the WHOLE string, not just the prefix: the tail restore
             * below reads buffer + self_index, and copying only self_index
             * bytes left that region as the allocator's zero fill - so every
             * insert with a non-empty tail replaced it with zeros. The result
             * stayed in bounds, which is why neither the sanitizers nor the
             * fuzzer could see it. */
            char_copy_2(buffer, self, self_size);

            /* An aliased source is read from the snapshot: the replace below overwrites the live
             * bytes it would otherwise be read from. */
            char const *const source = data_aliases_self ? buffer + alias_offset : data;

            char_replace_2(self, self_index, source, data_size);
            char_replace_2(self, self_index + data_size, buffer + self_index, self_size - self_index);

#ifdef ARENA_IMPLEMENTATION
            allocator_release(buffer, allocator);
#else
            allocator_release(buffer);
#endif // ARENA_IMPLEMENTATION
        }
    }

    trace_log_pop();
}

#ifdef ARENA_IMPLEMENTATION
static char* _char_from_trim(char const *const self, USize const self_size, Arena *const allocator)
#else
static char* _char_from_trim(char const *const self, USize const self_size)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    USize from = 0;
    USize to = self_size;

    while (from < to && char_is_whitespace(self[from])) {
        from += 1;
    }

    while (to > from && char_is_whitespace(self[to - 1])) {
        to -= 1;
    }

    USize const size = to - from;

#ifdef ARENA_IMPLEMENTATION
    char *const buffer = (char*) allocator_borrow(size + CHAR_END_CHARACTER, allocator);
#else
    char *const buffer = (char*) allocator_borrow(size + CHAR_END_CHARACTER);
#endif // ARENA_IMPLEMENTATION

    /* A refused arena answers null (the heap path aborts inside allocator_borrow instead): refuse
     * to nullptr rather than hand char_copy_3 a null destination - every caller degrades on null. */
    if (memory_empty(buffer)) {
        trace_log_pop();

        return nullptr;
    }

    if (size > 0) {
        char_copy_3(buffer, size + CHAR_END_CHARACTER, self + from, size);
    }

    buffer[size] = '\0';

    trace_log_pop();

    return buffer;
}

#ifdef ARENA_IMPLEMENTATION
static char* _char_join(char const *const *const parts, USize const count, char const *const separator, USize const separator_size, Arena *const allocator)
#else
static char* _char_join(char const *const *const parts, USize const count, char const *const separator, USize const separator_size)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "parts", (void*) parts);
    error_check_null(LOG_METADATA, "separator", (void*) separator);

    /* Joining no parts yields "". Settled before the arithmetic below, whose
     * count - 1 would wrap to the maximum USize. */
    if (count == 0) {
#ifdef ARENA_IMPLEMENTATION
        char *const empty = (char*) allocator_borrow(CHAR_END_CHARACTER, allocator);
#else
        char *const empty = (char*) allocator_borrow(CHAR_END_CHARACTER);
#endif // ARENA_IMPLEMENTATION

        /* A refused arena answers null: the producer refuses to nullptr, never aborts on it. */
        if (memory_empty(empty)) {
            trace_log_pop();

            return nullptr;
        }

        empty[0] = '\0';

        trace_log_pop();

        return empty;
    }

    error_check_out_of_bound_uint(LOG_METADATA, "count - 1", count - 1, "USIZE_MAX / separator_size", separator_size > 0 ? USIZE_MAX / separator_size : USIZE_MAX,
        "(count - 1) * separator_size overflows USize", separator_size > 0 && count - 1 > USIZE_MAX / separator_size);

    USize total = (count - 1) * separator_size;

    for (USize i = 0; i < count; i += 1) {
        error_check_null(LOG_METADATA, "parts[i]", (void*) parts[i]);

        USize const part_size = char_length(parts[i]);

        error_check_out_of_bound_uint(LOG_METADATA, "part_size", part_size, "USIZE_MAX - total", USIZE_MAX - total, "total + part_size overflows USize", part_size > USIZE_MAX - total);

        total += part_size;
    }

#ifdef ARENA_IMPLEMENTATION
    char *const buffer = (char*) allocator_borrow(total + CHAR_END_CHARACTER, allocator);
#else
    char *const buffer = (char*) allocator_borrow(total + CHAR_END_CHARACTER);
#endif // ARENA_IMPLEMENTATION

    if (memory_empty(buffer)) {
        trace_log_pop();

        return nullptr;
    }

    USize write = 0;

    for (USize i = 0; i < count; i += 1) {
        USize const part_size = char_length(parts[i]);

        for (USize j = 0; j < part_size; j += 1) {
            buffer[write + j] = parts[i][j];
        }

        write += part_size;

        if (i + 1 < count) {
            for (USize j = 0; j < separator_size; j += 1) {
                buffer[write + j] = separator[j];
            }

            write += separator_size;
        }
    }

    buffer[total] = '\0';

    trace_log_pop();

    return buffer;
}

#ifdef ARENA_IMPLEMENTATION
static void _char_move(char **const self, char **const data, Arena *const allocator)
#else
static void _char_move(char **const self, char **const data)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_null(LOG_METADATA, "*data", *data);

    if (!memory_empty(*self)) {
#ifdef ARENA_IMPLEMENTATION
        allocator_release((void*) *self, allocator);
#else
        allocator_release((void*) *self);
#endif // ARENA_IMPLEMENTATION
    }

    *self = *data;

    *data = nullptr;

    trace_log_pop();
}

#ifdef ARENA_IMPLEMENTATION
static char* _char_new(char const *const data, USize const data_size, Arena *const allocator)
#else
static char* _char_new(char const *const data, USize const data_size)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty source yields a valid one-byte "" buffer rather than aborting: this is
     * the data-carrying constructor, so "" is a value. char_new_1 keeps its guard -
     * there the size is an allocation REQUEST, and asking for nothing is a caller bug. */
    USize const char_new_size = data_size + CHAR_END_CHARACTER;
#ifdef ARENA_IMPLEMENTATION
    char *const char_new = (char*) allocator_borrow(char_new_size, allocator);
#else
    char *const char_new = (char*) allocator_borrow(char_new_size);
#endif // ARENA_IMPLEMENTATION

    if (memory_empty(char_new)) {
        trace_log_pop();

        return nullptr;
    }

    char_copy_3(char_new, char_new_size, data, data_size);

    // Unconditional on purpose (see the style guide's terminator rule): this
    // buffer can be arena-backed. Every CFW arena does zero on recycle, so this is
    // belt-and-braces - but Arena is a Tp vtable a third party can plug into, and the
    // always-zeroed guarantee is only owed by memory_alloc.
    char_new[data_size] = '\0';

    trace_log_pop();

    return char_new;
}

#ifdef ARENA_IMPLEMENTATION
static char* _char_new_from_numbers_int(ISize const number, U8 const padding, Arena *const allocator)
#else
static char* _char_new_from_numbers_int(ISize const number, U8 const padding)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    bool const negative = number < 0;
    USize const magnitude = negative ? (USize) -(number + 1) + 1 : (USize) number;
    USize const sign = negative ? 1 : 0;

    USize digits = 1;

    for (USize n = magnitude; n >= 10; n /= 10) {
        digits += 1;
    }

#ifdef ARENA_IMPLEMENTATION
    char *const buffer = (char*) allocator_borrow(sign + padding + digits + CHAR_END_CHARACTER, allocator);
#else
    char *const buffer = (char*) allocator_borrow(sign + padding + digits + CHAR_END_CHARACTER);
#endif // ARENA_IMPLEMENTATION

    if (memory_empty(buffer)) {
        trace_log_pop();

        return nullptr;
    }

    char_fill(buffer, sign + padding + digits, '0');

    if (negative) {
        buffer[0] = '-';
    }

    USize n = magnitude;

    for (USize k = 0; k < digits; k += 1) {
        buffer[sign + padding + digits - 1 - k] = (char) ((n % 10) + '0');
        n /= 10;
    }

    buffer[sign + padding + digits] = '\0';

    trace_log_pop();

    return buffer;
}

#ifdef ARENA_IMPLEMENTATION
static char* _char_new_from_numbers_uint(USize const number, U8 const padding, Arena *const allocator)
#else
static char* _char_new_from_numbers_uint(USize const number, U8 const padding)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    USize digits = 1;

    for (USize n = number; n >= 10; n /= 10) {
        digits += 1;
    }

#ifdef ARENA_IMPLEMENTATION
    char *const buffer = (char*) allocator_borrow(padding + digits + CHAR_END_CHARACTER, allocator);
#else
    char *const buffer = (char*) allocator_borrow(padding + digits + CHAR_END_CHARACTER);
#endif // ARENA_IMPLEMENTATION

    if (memory_empty(buffer)) {
        trace_log_pop();

        return nullptr;
    }

    char_fill(buffer, padding + digits, '0');

    USize n = number;

    for (USize k = 0; k < digits; k += 1) {
        buffer[padding + digits - 1 - k] = (char) ((n % 10) + '0');
        n /= 10;
    }

    buffer[padding + digits] = '\0';

    trace_log_pop();

    return buffer;
}

#ifdef ARENA_IMPLEMENTATION
static char* _char_new_replace(char const *const self,
    USize const self_size, char const *const find, USize const find_size, char const *const replace, USize const replace_size, Arena *const allocator)
#else
static char* _char_new_replace(char const *const self, USize const self_size, char const *const find, USize const find_size, char const *const replace, USize const replace_size)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "find", (void*) find);
    error_check_null(LOG_METADATA, "replace", (void*) replace);

    /* An empty find pattern marks no occurrence, so count comes back 0 and the
     * result is a verbatim copy. The scan below must still exclude it explicitly:
     * an empty pattern matches at every position without ever advancing read. */
    USize const count = char_find_count_3(self, self_size, find, find_size);

    error_check_out_of_bound_uint(LOG_METADATA, "count", count, "USIZE_MAX / replace_size", replace_size > 0 ? USIZE_MAX / replace_size : USIZE_MAX,
        "count * replace_size overflows USize", replace_size > 0 && count > USIZE_MAX / replace_size);
    error_check_out_of_bound_uint(LOG_METADATA, "self_size", self_size, "count * find_size", count * find_size, "count * find_size > self_size", count * find_size > self_size);
    error_check_out_of_bound_uint(LOG_METADATA, "USIZE_MAX - (self_size - count * find_size)", USIZE_MAX - (self_size - count * find_size),
        "count * replace_size", count * replace_size, "result_size overflows USize", count * replace_size > USIZE_MAX - (self_size - count * find_size));

    /* The subtraction cannot wrap: char_find_count_3 counts the same greedy
     * non-overlapping matches this loop writes, so count * find_size <= self_size is
     * a theorem rather than an assertion - the check above only records it, and holds
     * even in a build that compiles error_check_* away. The second check covers the
     * addition, which no such argument protects. */
    USize const result_size = (self_size - count * find_size) + count * replace_size;

#ifdef ARENA_IMPLEMENTATION
    char *const buffer = (char*) allocator_borrow(result_size + CHAR_END_CHARACTER, allocator);
#else
    char *const buffer = (char*) allocator_borrow(result_size + CHAR_END_CHARACTER);
#endif // ARENA_IMPLEMENTATION

    if (memory_empty(buffer)) {
        trace_log_pop();

        return nullptr;
    }

    USize read = 0;
    USize write = 0;

    while (read < self_size) {
        bool matched = find_size > 0 && find_size <= self_size - read;

        if (matched) {
            for (USize j = 0; j < find_size; j += 1) {
                if (self[read + j] != find[j]) {
                    matched = false;

                    break;
                }
            }
        }

        if (matched) {
            for (USize j = 0; j < replace_size; j += 1) {
                buffer[write + j] = replace[j];
            }

            read += find_size;
            write += replace_size;
        }
        else {
            buffer[write] = self[read];

            read += 1;
            write += 1;
        }
    }

    buffer[result_size] = '\0';

    trace_log_pop();

    return buffer;
}

#ifdef ARENA_IMPLEMENTATION
static char* _char_new_slice(char const *const self, USize const self_size, USize const self_index, Arena *const allocator)
#else
static char* _char_new_slice(char const *const self, USize const self_size, USize const self_index)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    /* An empty source is a VALUE: slicing "" at 0 is the ordinary "remainder
     * after the last delimiter" case, and aborting on it was the same class as
     * the empty-append and over-long-needle aborts removed this round. */
    error_check_out_of_bound_uint(LOG_METADATA, "self_index", self_index, "self_size", self_size, "self_index > self_size", self_index > self_size);

#ifdef ARENA_IMPLEMENTATION
    char *const char_new = _char_new(self + self_index, self_size - self_index, allocator);
#else
    char *const char_new = _char_new(self + self_index, self_size - self_index);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return char_new;
}

#ifdef ARENA_IMPLEMENTATION
static char* _char_new_slice_range(char const *const self, USize const self_size, USize const self_from, USize const self_to, Arena *const allocator)
#else
static char* _char_new_slice_range(char const *const self, USize const self_size, USize const self_from, USize const self_to)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_non_value_uint(LOG_METADATA, "self_size", self_size);
    error_check_out_of_bound_uint(LOG_METADATA, "self_from", self_from, "self_size", self_size, "self_from > self_size", self_from > self_size);
    /* The copy loop is INCLUSIVE of self_to, so self_to == self_size would read
     * one byte past a sized (non-terminated) buffer. */
    error_check_out_of_bound_uint(LOG_METADATA, "self_to", self_to, "self_size", self_size, "self_to >= self_size", self_to >= self_size);
    error_check_out_of_bound_uint(LOG_METADATA, "self_from", self_from, "self_to", self_to, "self_from > self_to", self_from > self_to);

#ifdef ARENA_IMPLEMENTATION
    char *const buffer = (char*) allocator_borrow(self_to - self_from + 1 + CHAR_END_CHARACTER, allocator);
#else
    char *const buffer = (char*) allocator_borrow(self_to - self_from + 1 + CHAR_END_CHARACTER);
#endif // ARENA_IMPLEMENTATION

    if (memory_empty(buffer)) {
        trace_log_pop();

        return nullptr;
    }

    for (USize i = 0; i <= self_to - self_from; i += 1) {
        buffer[i] = self[self_from + i];
    }

    buffer[self_to - self_from + 1] = '\0';

    trace_log_pop();

    return buffer;
}

#ifdef ARENA_IMPLEMENTATION
static void _char_remove(char **const self, USize const self_size, USize const index, Arena *const allocator)
#else
static void _char_remove(char **const self, USize const self_size, USize const index)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "*self", *self);
    error_check_non_value_uint(LOG_METADATA, "self_size", self_size);
    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "self_size", self_size, "index >= self_size", index >= self_size);

    char *const buffer = *self;

#ifdef ARENA_IMPLEMENTATION
    *self = (char*) allocator_borrow(self_size - 1 + CHAR_END_CHARACTER, allocator);
#else
    *self = (char*) allocator_borrow(self_size - 1 + CHAR_END_CHARACTER);
#endif // ARENA_IMPLEMENTATION

    /* A refused arena answers null: keep the caller's buffer in *self and change nothing. */
    if (memory_empty(*self)) {
        *self = buffer;

        trace_log_pop();

        return;
    }

    char_copy_2(*self, buffer, index);

    for (USize i = index; i < self_size - 1; i += 1) {
        (*self)[i] = buffer[i + 1];
    }

    (*self)[self_size - 1] = '\0';

#ifdef ARENA_IMPLEMENTATION
    allocator_release(buffer, allocator);
#else
    allocator_release(buffer);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();
}

#ifdef ARENA_IMPLEMENTATION
static char* _char_repeat(char const *const data, USize const data_size, USize const count, Arena *const allocator)
#else
static char* _char_repeat(char const *const data, USize const data_size, USize const count)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Repeating nothing, or repeating anything zero times, yields "". Settled before
     * the overflow check below, which divides by data_size and would fault on 0. */
    if (data_size == 0 || count == 0) {
#ifdef ARENA_IMPLEMENTATION
        char *const empty = (char*) allocator_borrow(CHAR_END_CHARACTER, allocator);
#else
        char *const empty = (char*) allocator_borrow(CHAR_END_CHARACTER);
#endif // ARENA_IMPLEMENTATION

        /* A refused arena answers null: the producer refuses to nullptr, never aborts on it. */
        if (memory_empty(empty)) {
            trace_log_pop();

            return nullptr;
        }

        empty[0] = '\0';

        trace_log_pop();

        return empty;
    }

    error_check_out_of_bound_uint(LOG_METADATA, "count", count, "USIZE_MAX / data_size", USIZE_MAX / data_size, "data_size * count overflows USize", count > USIZE_MAX / data_size);

    USize const buffer_size = data_size * count;
#ifdef ARENA_IMPLEMENTATION
    char *const buffer = (char*) allocator_borrow(buffer_size + CHAR_END_CHARACTER, allocator);
#else
    char *const buffer = (char*) allocator_borrow(buffer_size + CHAR_END_CHARACTER);
#endif // ARENA_IMPLEMENTATION

    if (memory_empty(buffer)) {
        trace_log_pop();

        return nullptr;
    }

    /* char_replace_2 terminates after every chunk, the last one included, so the loop
     * writes no terminator of its own and does not lean on the allocator's zero fill. */
    for (USize i = 0; i < count; i += 1) {
        char_replace_2(buffer, data_size * i, data, data_size);
    }

    trace_log_pop();

    return buffer;
}

#ifdef ARENA_IMPLEMENTATION
static char* _char_split(char const *const self, USize const self_size, char const *const delimiter, USize const delimiter_size, Arena *const allocator)
#else
static char* _char_split(char const *const self, USize const self_size, char const *const delimiter, USize const delimiter_size)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "delimiter", (void*) delimiter);

    /* An empty delimiter marks no split point, so the first token is the whole string.
     * char_find_3 would answer 0 for it (the empty needle matches at the origin) and
     * yield an empty token instead, so it is settled before the search. A leading
     * delimiter legitimately produces an empty first token - that used to abort. */
    USize const delimiter_index = delimiter_size == 0 ? CHAR_NPOS : char_find_3(self, self_size, 0, delimiter, delimiter_size);
    USize const char_split_size = delimiter_index == CHAR_NPOS ? self_size : delimiter_index;
#ifdef ARENA_IMPLEMENTATION
    char *const char_token = _char_new(self, char_split_size, allocator);
#else
    char *const char_token = _char_new(self, char_split_size);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return char_token;
}

/*==============================================================================
 * MARK: - Arena-backed API
 *============================================================================*/
#ifdef ARENA_IMPLEMENTATION
void char_alloc_add_1(char **const self, char const *const data, USize const self_index, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char_alloc_add_4(self, char_length(*self), data, char_length(data), self_index, allocator);

    trace_log_pop();
}

void char_alloc_add_2(char **const self, char const *const data, USize const data_size, USize const self_index, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char_alloc_add_4(self, char_length(*self), data, data_size, self_index, allocator);

    trace_log_pop();
}

void char_alloc_add_3(char **const self, USize const self_size, char const *const data, USize const self_index, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char_alloc_add_4(self, self_size, data, char_length(data), self_index, allocator);

    trace_log_pop();
}

void char_alloc_add_4(char **const self, USize const self_size, char const *const data, USize const data_size, USize const self_index, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    _char_add(self, self_size, data, data_size, self_index, allocator);

    trace_log_pop();
}

void char_alloc_add_first_1(char **const self, char const *const data, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char_alloc_add_first_2(self, data, char_length(data), allocator);

    trace_log_pop();
}

void char_alloc_add_first_2(char **const self, char const *const data, USize const data_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char_alloc_add_first_4(self, char_length(*self), data, data_size, allocator);

    trace_log_pop();
}

void char_alloc_add_first_3(char **const self, USize const self_size, char const *const data, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char_alloc_add_4(self, self_size, data, char_length(data), 0, allocator);

    trace_log_pop();
}

void char_alloc_add_first_4(char **const self, USize const self_size, char const *const data, USize const data_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char_alloc_add_4(self, self_size, data, data_size, 0, allocator);

    trace_log_pop();
}

void char_alloc_add_first_fixed_1(char *const self, USize const self_capacity, char const *const data, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char_alloc_add_first_fixed_2(self, self_capacity, data, char_length(data), allocator);

    trace_log_pop();
}

void char_alloc_add_first_fixed_2(char *const self, USize const self_capacity, char const *const data, USize const data_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char_alloc_add_first_fixed_4(self, self_capacity, char_length(self), data, data_size, allocator);

    trace_log_pop();
}

void char_alloc_add_first_fixed_3(char *const self, USize const self_capacity, USize const self_size, char const *const data, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    /* Forward self_size to _4 rather than routing through _2, which drops it
     * and recomputes char_length(self) - the same discarded-size shape that
     * made char_find_2 read past a non-terminated buffer. Harmless here (a
     * recompute, not a misuse), but the non-arena sibling
     * char_add_first_fixed_3 already forwards, and the pair must agree. */
    char_alloc_add_first_fixed_4(self, self_capacity, self_size, data, char_length(data), allocator);

    trace_log_pop();
}

void char_alloc_add_first_fixed_4(char *const self, USize const self_capacity, USize const self_size, char const *const data, USize const data_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char_alloc_add_fixed_4(self, self_capacity, self_size, data, data_size, 0, allocator);

    trace_log_pop();
}

void char_alloc_add_fixed_1(char *const self, USize const self_capacity, char const *const data, USize const self_index, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char_alloc_add_fixed_4(self, self_capacity, char_length(self), data, char_length(data), self_index, allocator);

    trace_log_pop();
}

void char_alloc_add_fixed_2(char *const self, USize const self_capacity, char const *const data, USize const data_size, USize const self_index, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char_alloc_add_fixed_4(self, self_capacity, char_length(self), data, data_size, self_index, allocator);

    trace_log_pop();
}

void char_alloc_add_fixed_3(char *const self, USize const self_capacity, USize const self_size, char const *const data, USize const self_index, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char_alloc_add_fixed_4(self, self_capacity, self_size, data, char_length(data), self_index, allocator);

    trace_log_pop();
}

void char_alloc_add_fixed_4(char *const self, USize const self_capacity, USize const self_size, char const *const data, USize const data_size, USize const self_index, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    _char_add_fixed(self, self_capacity, self_size, data, data_size, self_index, allocator);

    trace_log_pop();
}

void char_alloc_add_last_1(char **const self, char const *const data, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char_alloc_add_last_2(self, data, char_length(data), allocator);

    trace_log_pop();
}

void char_alloc_add_last_2(char **const self, char const *const data, USize const data_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char_alloc_add_last_4(self, char_length(*self), data, data_size, allocator);

    trace_log_pop();
}

void char_alloc_add_last_3(char **const self, USize const self_size, char const *const data, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char_alloc_add_last_4(self, self_size, data, char_length(data), allocator);

    trace_log_pop();
}

void char_alloc_add_last_4(char **const self, USize const self_size, char const *const data, USize const data_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char_alloc_add_4(self, self_size, data, data_size, self_size, allocator);

    trace_log_pop();
}

void char_alloc_add_last_fixed_1(char *const self, USize const self_capacity, char const *const data, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char_alloc_add_last_fixed_2(self, self_capacity, data, char_length(data), allocator);

    trace_log_pop();
}

void char_alloc_add_last_fixed_2(char *const self, USize const self_capacity, char const *const data, USize const data_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char_alloc_add_last_fixed_4(self, self_capacity, char_length(self), data, data_size, allocator);

    trace_log_pop();
}

void char_alloc_add_last_fixed_3(char *const self, USize const self_capacity, USize const self_size, char const *const data, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char_alloc_add_last_fixed_4(self, self_capacity, self_size, data, char_length(data), allocator);

    trace_log_pop();
}

void char_alloc_add_last_fixed_4(char *const self, USize const self_capacity, USize const self_size, char const *const data, USize const data_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char_alloc_add_fixed_4(self, self_capacity, self_size, data, data_size, self_size, allocator);

    trace_log_pop();
}

void char_alloc_delete(char *const self, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    allocator_release(self, allocator);

    trace_log_pop();
}

char* char_alloc_from_numbers_int_1(ISize const number, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char *const buffer = char_alloc_from_numbers_int_2(number, 0, allocator);

    trace_log_pop();

    return buffer;
}

char* char_alloc_from_numbers_int_2(ISize const number, U8 const padding, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char *const buffer = _char_new_from_numbers_int(number, padding, allocator);

    trace_log_pop();

    return buffer;
}

char* char_alloc_from_numbers_uint_1(USize const number, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char *const buffer = char_alloc_from_numbers_uint_2(number, 0, allocator);

    trace_log_pop();

    return buffer;
}

char* char_alloc_from_numbers_uint_2(USize const number, U8 const padding, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char *const buffer = _char_new_from_numbers_uint(number, padding, allocator);

    trace_log_pop();

    return buffer;
}

char* char_alloc_from_trim_1(char const *const self, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char *const buffer = char_alloc_from_trim_2(self, char_length(self), allocator);

    trace_log_pop();

    return buffer;
}

char* char_alloc_from_trim_2(char const *const self, USize const self_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char *const buffer = _char_from_trim(self, self_size, allocator);

    trace_log_pop();

    return buffer;
}

char* char_alloc_join_1(char const *const *const parts, USize const count, char const *const separator, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char *const buffer = char_alloc_join_2(parts, count, separator, char_length(separator), allocator);

    trace_log_pop();

    return buffer;
}

char* char_alloc_join_2(char const *const *const parts, USize const count, char const *const separator, USize const separator_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char *const buffer = _char_join(parts, count, separator, separator_size, allocator);

    trace_log_pop();

    return buffer;
}

void char_alloc_move(char **const self, char **const data, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    _char_move(self, data, allocator);

    trace_log_pop();
}

char* char_alloc_new_1(USize const size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "size", size);

    char *const buffer = (char*) allocator_borrow(size + CHAR_END_CHARACTER, allocator);

    /* Only memory_alloc owes zeroing. An Arena is a Tp vtable a third party can plug in, so
     * the terminator the header promises is written here rather than assumed. */
    if (!memory_empty(buffer)) {
        buffer[0] = '\0';
    }

    trace_log_pop();

    return buffer;
}

char* char_alloc_new_2(char const *const data, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char *const buffer = char_alloc_new_3(data, char_length(data), allocator);

    trace_log_pop();

    return buffer;
}

char* char_alloc_new_3(char const *const data, USize const data_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char *const buffer = _char_new(data, data_size, allocator);

    trace_log_pop();

    return buffer;
}

char* char_alloc_new_replace_1(char const *const self, char const *const find, char const *const replace, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char *const buffer = char_alloc_new_replace_2(self, char_length(self), find, char_length(find), replace, char_length(replace), allocator);

    trace_log_pop();

    return buffer;
}

char* char_alloc_new_replace_2(char const *const self,
    USize const self_size, char const *const find, USize const find_size, char const *const replace, USize const replace_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char *const buffer = _char_new_replace(self, self_size, find, find_size, replace, replace_size, allocator);

    trace_log_pop();

    return buffer;
}

char* char_alloc_new_slice_1(char const *const self, USize const self_index, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char *const buffer = char_alloc_new_slice_2(self, char_length(self), self_index, allocator);

    trace_log_pop();

    return buffer;
}

char* char_alloc_new_slice_2(char const *const self, USize const self_size, USize const self_index, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char *const buffer = _char_new_slice(self, self_size, self_index, allocator);

    trace_log_pop();

    return buffer;
}

char* char_alloc_new_slice_range_1(char const *const self, USize const self_from, USize const self_to, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char *const buffer = _char_new_slice_range(self, char_length(self), self_from, self_to, allocator);

    trace_log_pop();

    return buffer;
}

char* char_alloc_new_slice_range_2(char const *const self, USize const self_size, USize const self_from, USize const self_to, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char *const buffer = _char_new_slice_range(self, self_size, self_from, self_to, allocator);

    trace_log_pop();

    return buffer;
}

void char_alloc_remove_1(char **const self, USize const index, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char_alloc_remove_2(self, char_length(*self), index, allocator);

    trace_log_pop();
}

void char_alloc_remove_2(char **const self, USize const self_size, USize const index, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    _char_remove(self, self_size, index, allocator);

    trace_log_pop();
}

char* char_alloc_repeat_1(char const *const data, USize const count, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char *const buffer = char_alloc_repeat_2(data, char_length(data), count, allocator);

    trace_log_pop();

    return buffer;
}

char* char_alloc_repeat_2(char const *const data, USize const data_size, USize const count, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char *const buffer = _char_repeat(data, data_size, count, allocator);

    trace_log_pop();

    return buffer;
}

char* char_alloc_split_1(char const *const self, char const *const delimiter, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char *const buffer = char_alloc_split_2(self, char_length(self), delimiter, allocator);

    trace_log_pop();

    return buffer;
}

char* char_alloc_split_2(char const *const self, USize const self_size, char const *const delimiter, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char *const buffer = char_alloc_split_3(self, self_size, delimiter, char_length(delimiter), allocator);

    trace_log_pop();

    return buffer;
}

char* char_alloc_split_3(char const *const self, USize const self_size, char const *const delimiter, USize const delimiter_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    char *const buffer = _char_split(self, self_size, delimiter, delimiter_size, allocator);

    trace_log_pop();

    return buffer;
}

void char_alloc_trim_1(char **const self, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "*self", (void*) *self);

    char_alloc_trim_2(self, char_length(*self), allocator);

    trace_log_pop();
}

void char_alloc_trim_2(char **const self, USize const self_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "*self", (void*) *self);

    char *const trimmed = char_alloc_from_trim_2(*self, self_size, allocator);

    /* A refused arena answers null: keep the caller's buffer in *self and change nothing. */
    if (memory_empty(trimmed)) {
        trace_log_pop();

        return;
    }

    char *const buffer = *self;

    *self = trimmed;

    allocator_release(buffer, allocator);

    trace_log_pop();
}

#endif // ARENA_IMPLEMENTATION

/*==============================================================================
 * MARK: - Standard API
 *============================================================================*/
void char_add_1(char **const self, char const *const data, USize const self_index) {
    trace_log_push(LOG_METADATA);

    char_add_2(self, data, char_length(data), self_index);

    trace_log_pop();
}

void char_add_2(char **const self, char const *const data, USize const data_size, USize const self_index) {
    trace_log_push(LOG_METADATA);

    char_add_4(self, char_length(*self), data, data_size, self_index);

    trace_log_pop();
}

void char_add_3(char **const self, USize const self_size, char const *const data, USize const self_index) {
    trace_log_push(LOG_METADATA);

    char_add_4(self, self_size, data, char_length(data), self_index);

    trace_log_pop();
}

void char_add_4(char **const self, USize const self_size, char const *const data, USize const data_size, USize const self_index) {
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    _char_add(self, self_size, data, data_size, self_index, nullptr);
#else
    _char_add(self, self_size, data, data_size, self_index);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();
}

void char_add_first_1(char **const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    char_add_first_2(self, data, char_length(data));

    trace_log_pop();
}

void char_add_first_2(char **const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    char_add_first_4(self, char_length(*self), data, data_size);

    trace_log_pop();
}

void char_add_first_3(char **const self, USize const self_size, char const *const data) {
    trace_log_push(LOG_METADATA);

    char_add_first_4(self, self_size, data, char_length(data));

    trace_log_pop();
}

void char_add_first_4(char **const self, USize const self_size, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    char_add_4(self, self_size, data, data_size, 0);

    trace_log_pop();
}

void char_add_first_fixed_1(char *const self, USize const self_capacity, char const *const data) {
    trace_log_push(LOG_METADATA);

    char_add_first_fixed_2(self, self_capacity, data, char_length(data));

    trace_log_pop();
}

void char_add_first_fixed_2(char *const self, USize const self_capacity, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    char_add_first_fixed_4(self, self_capacity, char_length(self), data, data_size);

    trace_log_pop();
}

void char_add_first_fixed_3(char *const self, USize const self_capacity, USize const self_size, char const *const data) {
    trace_log_push(LOG_METADATA);

    char_add_first_fixed_4(self, self_capacity, self_size, data, char_length(data));

    trace_log_pop();
}

void char_add_first_fixed_4(char *const self, USize const self_capacity, USize const self_size, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    char_add_fixed_4(self, self_capacity, self_size, data, data_size, 0);

    trace_log_pop();
}

void char_add_fixed_1(char *const self, USize const self_capacity, char const *const data, USize const self_index) {
    trace_log_push(LOG_METADATA);

    char_add_fixed_2(self, self_capacity, data, char_length(data), self_index);

    trace_log_pop();
}

void char_add_fixed_2(char *const self, USize const self_capacity, char const *const data, USize const data_size, USize const self_index) {
    trace_log_push(LOG_METADATA);

    char_add_fixed_4(self, self_capacity, char_length(self), data, data_size, self_index);

    trace_log_pop();
}

void char_add_fixed_3(char *const self, USize const self_capacity, USize const self_size, char const *const data, USize const self_index) {
    trace_log_push(LOG_METADATA);

    char_add_fixed_4(self, self_capacity, self_size, data, char_length(data), self_index);

    trace_log_pop();
}

void char_add_fixed_4(char *const self, USize const self_capacity, USize const self_size, char const *const data, USize const data_size, USize const self_index) {
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    _char_add_fixed(self, self_capacity, self_size, data, data_size, self_index, nullptr);
#else
    _char_add_fixed(self, self_capacity, self_size, data, data_size, self_index);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();
}

void char_add_last_1(char **const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    char_add_last_2(self, data, char_length(data));

    trace_log_pop();
}

void char_add_last_2(char **const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    char_add_last_4(self, char_length(*self), data, data_size);

    trace_log_pop();
}

void char_add_last_3(char **const self, USize const self_size, char const *const data) {
    trace_log_push(LOG_METADATA);

    char_add_last_4(self, self_size, data, char_length(data));

    trace_log_pop();
}

void char_add_last_4(char **const self, USize const self_size, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    char_add_4(self, self_size, data, data_size, self_size);

    trace_log_pop();
}

void char_add_last_fixed_1(char *const self, USize const self_capacity, char const *const data) {
    trace_log_push(LOG_METADATA);

    char_add_last_fixed_2(self, self_capacity, data, char_length(data));

    trace_log_pop();
}

void char_add_last_fixed_2(char *const self, USize const self_capacity, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    char_add_last_fixed_4(self, self_capacity, char_length(self), data, data_size);

    trace_log_pop();
}

void char_add_last_fixed_3(char *const self, USize const self_capacity, USize const self_size, char const *const data) {
    trace_log_push(LOG_METADATA);

    char_add_last_fixed_4(self, self_capacity, self_size, data, char_length(data));

    trace_log_pop();
}

void char_add_last_fixed_4(char *const self, USize const self_capacity, USize const self_size, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    char_add_fixed_4(self, self_capacity, self_size, data, data_size, self_size);

    trace_log_pop();
}

char char_at(char const *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    USize const self_size = char_length(self);

    /* The header promises '\0' out of bounds, so an index past the end is a
     * legal QUERY, not caller error - a scan running off a short string asks it
     * routinely. The error_check that stood here made that promise unreachable
     * in every shipping build (and let the read happen in unchecked ones);
     * same class as char_find_slice_5's removed abort. */
    if (memory_empty(self) || index >= self_size) {
        trace_log_pop();

        return '\0';
    }

    char const value = (char) self[index];

    trace_log_pop();

    return value;
}

bool char_check_number(char const c) {
    return c >= '0' && c <= '9';
}

void char_clear_1(char *const self) {
    trace_log_push(LOG_METADATA);

    char_clear_2(self, char_length(self));

    trace_log_pop();
}

void char_clear_2(char *const self, USize const self_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* Clearing an empty buffer is a no-op. The early return is required, not cosmetic:
     * memory_set rejects a zero dst_size of its own, so merely dropping the guard here
     * would relocate the abort one frame down rather than remove it. */
    if (self_size == 0) {
        trace_log_pop();

        return;
    }

    memory_set(self, self_size, '\0');

    trace_log_pop();
}

bool char_compare_equal_1(char const *const char_1, char const *const char_2) {
    trace_log_push(LOG_METADATA);

    bool const match = char_compare_equal_2(char_1, char_length(char_1), char_2, char_length(char_2));

    trace_log_pop();

    return match;
}

bool char_compare_equal_2(char const *const char_1, USize const char_1_size, char const *const char_2, USize const char_2_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "char_1", (void*) char_1);
    error_check_null(LOG_METADATA, "char_2", (void*) char_2);

    /* A zero size is DATA, not a programming error: "" arrives routinely from an
     * empty DB cell, a blank JSON field, a missing header or an unset env var.
     * The loop below already gives the right answer for it (two empties compare
     * equal, empty vs non-empty differ in size and so are unequal), so the size
     * guards that used to abort here only ever turned valid input into a crash.
     * A null pointer remains a caller bug and still aborts. */
    bool equal = true;

    if (char_1_size == char_2_size) {
        for (USize i = 0; i < char_1_size; i += 1) {
            if (char_1[i] != char_2[i]) {
                equal = false;

                break;
            }
        }
    }
    else {
        equal = false;
    }

    trace_log_pop();

    return equal;
}

bool char_compare_equal_comptime_1(char const *const char_1, char const *const char_2) {
    trace_log_push(LOG_METADATA);

    bool const equal = char_compare_equal_comptime_2(char_1, char_length(char_1), char_2, char_length(char_2));

    trace_log_pop();

    return equal;
}

bool char_compare_equal_comptime_2(char const *const char_1, USize const char_1_size, char const *const char_2, USize const char_2_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "char_1", (void*) char_1);
    error_check_null(LOG_METADATA, "char_2", (void*) char_2);

    if (char_1_size != char_2_size) {
        trace_log_pop();

        return false;
    }

    U8 diff = 0;

    for (USize i = 0; i < char_1_size; i += 1) {
        diff |= (U8) (char_1[i] ^ char_2[i]);
    }

    bool const equal = diff == 0;

    trace_log_pop();

    return equal;
}

bool char_compare_iequal_1(char const *const char_1, char const *const char_2) {
    trace_log_push(LOG_METADATA);

    bool const equal = char_compare_iequal_2(char_1, char_length(char_1), char_2, char_length(char_2));

    trace_log_pop();

    return equal;
}

bool char_compare_iequal_2(char const *const char_1, USize const char_1_size, char const *const char_2, USize const char_2_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "char_1", (void*) char_1);
    error_check_null(LOG_METADATA, "char_2", (void*) char_2);

    /* Empty is data, not a bug - see char_compare_equal_2 for the reasoning. */
    bool equal = true;

    if (char_1_size == char_2_size) {
        for (USize i = 0; i < char_1_size; i += 1) {
            if (char_to_lower(char_1[i]) != char_to_lower(char_2[i])) {
                equal = false;

                break;
            }
        }
    }
    else {
        equal = false;
    }

    trace_log_pop();

    return equal;
}

bool char_compare_iequal_comptime_1(char const *const char_1, char const *const char_2) {
    trace_log_push(LOG_METADATA);

    bool const equal = char_compare_iequal_comptime_2(char_1, char_length(char_1), char_2, char_length(char_2));

    trace_log_pop();

    return equal;
}

bool char_compare_iequal_comptime_2(char const *const char_1, USize const char_1_size, char const *const char_2, USize const char_2_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "char_1", (void*) char_1);
    error_check_null(LOG_METADATA, "char_2", (void*) char_2);

    if (char_1_size != char_2_size) {
        trace_log_pop();

        return false;
    }

    U8 diff = 0;

    for (USize i = 0; i < char_1_size; i += 1) {
        diff |= (U8) (char_to_lower(char_1[i]) ^ char_to_lower(char_2[i]));
    }

    bool const equal = diff == 0;

    trace_log_pop();

    return equal;
}

bool char_contains_1(char const *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    bool const found = char_find_exists_1(self, data);

    trace_log_pop();

    return found;
}

bool char_contains_2(char const *const self, USize const self_size, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    bool const found = char_find_exists_3(self, self_size, data, data_size);

    trace_log_pop();

    return found;
}

void char_copy_1(char *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    char_copy_2(self, data, char_length(data));

    trace_log_pop();
}

void char_copy_2(char *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Copying nothing is a no-op. Unlike char_copy_3 there is no capacity here, so
     * nothing can be terminated either - this writes exactly data_size bytes. */
    if (data_size == 0) {
        trace_log_pop();

        return;
    }

    memory_copy_1((void*) self, (void*) data, data_size);

    trace_log_pop();
}

void char_copy_3(char *const self, USize const self_capacity, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_non_value_uint(LOG_METADATA, "self_capacity", self_capacity);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Copying nothing is a no-op. Note the asymmetry with self_capacity above,
     * which is still fatal: a zero-capacity DESTINATION is a caller bug (there
     * is nowhere to write, not even a terminator), whereas a zero-length SOURCE
     * is ordinary data. memory_copy_2 aborts on a size-0 source, so return
     * before it rather than relaxing that lower-level primitive. */
    if (data_size == 0) {
        if (self_capacity > 0) {
            self[0] = '\0';
        }

        trace_log_pop();

        return;
    }

    /* Real control flow with no error_check beside it: a kept one aborted on this very
     * condition in every checked build, leaving the refusal dead (the char_at half-fix,
     * rulings 939/972), and the fit depends on the VALUE's length. Without it memory_copy_2
     * would refuse the over-long copy on its own and the terminator below would still land
     * at self[data_size] - one zero byte past the destination. Refuse the whole operation,
     * leaving a terminated (empty) destination. */
    if (data_size >= self_capacity) {
        if (self_capacity > 0) {
            self[0] = '\0';
        }

        trace_log_pop();

        return;
    }

    memory_copy_2((void*) self, self_capacity, (void*) data, data_size);

    /* Terminate. Omitting this left the destination's previous tail live past
     * the copy ("LONGSTRING" then "short" read back as "shortSTRING"), which
     * broke a live server, and left _char_add_fixed handing out unterminated
     * buffers whose char_length walked off the array. The refusal above makes
     * data_size < self_capacity true in every build, so this byte is in
     * bounds without relying on a compiled-away check. */
    self[data_size] = '\0';

    trace_log_pop();
}

void char_copy_truncate(char *const self, USize const self_capacity, char const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_non_value_uint(LOG_METADATA, "self_capacity", self_capacity);

    USize length = 0;

    if (!memory_empty(data)) {
        for (; data[length] != '\0' && length + 1 < self_capacity; length += 1) {
            self[length] = data[length];
        }
    }

    if (self_capacity > 0) {
        self[length] = '\0';
    }

    trace_log_pop();
}

void char_delete(char *const self) {
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    allocator_release(self, nullptr);
#else
    allocator_release(self);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();
}

bool char_empty(char const *const self) {
    /* Untraced like char_is_* and char_check_number: a two-compare leaf predicate. */
    return memory_empty(self) || *self == '\0';
}

bool char_ends_with_1(char const *const self, char const *const suffix) {
    trace_log_push(LOG_METADATA);

    bool const ends = char_ends_with_2(self, char_length(self), suffix, char_length(suffix));

    trace_log_pop();

    return ends;
}

bool char_ends_with_2(char const *const self, USize const self_size, char const *const suffix, USize const suffix_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "suffix", (void*) suffix);

    bool ends = suffix_size <= self_size;

    if (ends) {
        USize const offset = self_size - suffix_size;

        for (USize i = 0; i < suffix_size; i += 1) {
            if (self[offset + i] != suffix[i]) {
                ends = false;

                break;
            }
        }
    }

    trace_log_pop();

    return ends;
}

bool char_equal_1(char const *const char_1, char const *const char_2) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "char_1", (void*) char_1);
    error_check_null(LOG_METADATA, "char_2", (void*) char_2);

    USize const char_1_size = char_length(char_1);
    USize const char_2_size = char_length(char_2);

    bool const equal = (char_1_size == char_2_size) && (char_1_size == 0 || char_compare_equal_2(char_1, char_1_size, char_2, char_2_size));

    trace_log_pop();

    return equal;
}

bool char_equal_2(char const *const char_1, USize const char_1_size, char const *const char_2, USize const char_2_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "char_1", (void*) char_1);
    error_check_null(LOG_METADATA, "char_2", (void*) char_2);

    bool const equal = (char_1_size == char_2_size) && (char_1_size == 0 || char_compare_equal_2(char_1, char_1_size, char_2, char_2_size));

    trace_log_pop();

    return equal;
}

void char_erase_1(char *const self, USize const self_from, USize const self_to) {
    trace_log_push(LOG_METADATA);

    char_erase_2(self, char_length(self), self_from, self_to);

    trace_log_pop();
}

void char_erase_2(char *const self, USize const self_size, USize const self_from, USize const self_to) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_non_value_uint(LOG_METADATA, "self_size", self_size);
    error_check_out_of_bound_uint(LOG_METADATA, "self_from", self_from, "self_size", self_size, "self_from >= self_size", self_from >= self_size);
    error_check_out_of_bound_uint(LOG_METADATA, "self_to", self_to, "self_size", self_size, "self_to >= self_size", self_to >= self_size);
    error_check_out_of_bound_uint(LOG_METADATA, "self_from", self_from, "self_to", self_to, "self_from > self_to", self_from > self_to);

    for (USize i = self_to + 1; i < self_size; i += 1) {
        self[self_from + (i - self_to - 1)] = self[i];
    }

    for (USize i = self_size - (self_to - self_from + 1); i < self_size; i += 1) {
        self[i] = '\0';
    }

    trace_log_pop();
}

void char_fill(char *const self, USize const self_size, char const c) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* Filling an empty buffer is a no-op. Same reason as char_clear_2: memory_set
     * rejects a zero dst_size, so the guard has to live here. */
    if (self_size == 0) {
        trace_log_pop();

        return;
    }

    memory_set(self, self_size, c);

    trace_log_pop();
}

USize char_find_1(char const *const self, USize const self_index, char const *const data) {
    trace_log_push(LOG_METADATA);

    USize const value = char_find_2(self, self_index, data, char_length(data));

    trace_log_pop();

    return value;
}

USize char_find_2(char const *const self, USize const self_index, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    /* Forward data_size. Running char_length over `data` instead discarded the
     * caller's explicit size and read past any needle that is an exact-length,
     * non-terminated buffer - which is precisely what the sized overloads
     * exist to accept. Every sibling _2 wrapper already forwards its size. */
    USize const value = char_find_3(self, char_length(self), self_index, data, data_size);

    trace_log_pop();

    return value;
}

USize char_find_3(char const *const self, USize const self_size, USize const self_index, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty needle matches at the search origin: the empty string is a
     * prefix of every position, including one past the last character. Settled
     * up front because the scan below reads data[0] before consulting data_size. */
    if (data_size == 0) {
        USize const origin = self_index <= self_size ? self_index : CHAR_NPOS;

        trace_log_pop();

        return origin;
    }

    USize index = CHAR_NPOS;
    USize data_index = 0;
    bool match = false;

    for (USize i = 0; self_index + i < self_size; i += 1) {
        if (self[self_index + i] == data[data_index]) {
            for (USize j = self_index + i; data_index < data_size && j < self_size; j += 1) {
                if (self[j] == data[data_index]) {
                    if (!match) {
                        match = true;
                    }

                    data_index += 1;
                }
                else {
                    data_index = 0;

                    match = false;

                    break;
                }
            }

            if (data_index < data_size) {
                data_index = 0;

                match = false;
            }
        }
        else {
            data_index = 0;
        }

        if (match) {
            index = self_index + i;

            break;
        }
    }

    trace_log_pop();

    return index;
}

USize char_find_count_1(char const *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    USize const value = char_find_count_2(self, data, char_length(data));

    trace_log_pop();

    return value;
}

USize char_find_count_2(char const *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    USize const value = char_find_count_3(self, char_length(self), data, data_size);

    trace_log_pop();

    return value;
}

USize char_find_count_3(char const *const self, USize const self_size, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty needle counts as zero occurrences, not as one match between every
     * character. The replace family sizes its output buffer from this count, so a
     * positive answer here would make it splice a replacement into every gap. */
    if (data_size == 0) {
        trace_log_pop();

        return 0;
    }

    /* Greedy and non-overlapping, deliberately identical to the write loop in
     * _char_new_replace: that helper sizes its output buffer from this count, so
     * any disagreement between the two is a heap overflow. Counting overlapping
     * matches here would make count * data_size exceed self_size. */
    USize count = 0;

    if (data_size <= self_size) {
        USize i = 0;

        while (i <= self_size - data_size) {
            bool matched = true;

            for (USize j = 0; j < data_size; j += 1) {
                if (self[i + j] != data[j]) {
                    matched = false;

                    break;
                }
            }

            if (matched) {
                count += 1;
                i += data_size;
            }
            else {
                i += 1;
            }
        }
    }

    trace_log_pop();

    return count;
}

bool char_find_exists_1(char const *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    bool const match = char_find_exists_2(self, data, char_length(data));

    trace_log_pop();

    return match;
}

bool char_find_exists_2(char const *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    bool const match = char_find_exists_3(self, char_length(self), data, data_size);

    trace_log_pop();

    return match;
}

bool char_find_exists_3(char const *const self, USize const self_size, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty needle exists inside every string, the empty string included. */
    if (data_size == 0) {
        trace_log_pop();

        return true;
    }

    /* Bounded by data_size on every read: the previous walk carried its needle
     * cursor across outer iterations and could index past data[data_size - 1] on
     * a buffer that is not NUL-terminated. */
    if (data_size <= self_size) {
        for (USize i = 0; i <= self_size - data_size; i += 1) {
            bool matched = true;

            for (USize j = 0; j < data_size; j += 1) {
                if (self[i + j] != data[j]) {
                    matched = false;

                    break;
                }
            }

            if (matched) {
                trace_log_pop();

                return true;
            }
        }
    }

    trace_log_pop();

    return false;
}

USize char_find_first_1(char const *const self, char const *const set) {
    trace_log_push(LOG_METADATA);

    USize const index = char_find_first_2(self, char_length(self), set, char_length(set));

    trace_log_pop();

    return index;
}

USize char_find_first_2(char const *const self, USize const self_size, char const *const set, USize const set_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "set", (void*) set);

    /* An empty set holds no candidate character, so nothing can match it, and
     * an empty self offers no position to match at. Both loops below already
     * settle those cases by never running. */
    USize index = CHAR_NPOS;

    for (USize i = 0; i < self_size; i += 1) {
        for (USize j = 0; j < set_size; j += 1) {
            if (self[i] == set[j]) {
                index = i;

                break;
            }
        }

        if (index != CHAR_NPOS) {
            break;
        }
    }

    trace_log_pop();

    return index;
}

USize char_find_reverse_1(char const *const self, USize const self_index, char const *const data) {
    trace_log_push(LOG_METADATA);

    USize const value = char_find_reverse_2(self, self_index, data, char_length(data));

    trace_log_pop();

    return value;
}

USize char_find_reverse_2(char const *const self, USize const self_index, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    USize const value = char_find_reverse_3(self, char_length(self), self_index, data, data_size);

    trace_log_pop();

    return value;
}

USize char_find_reverse_3(char const *const self, USize const self_size, USize const self_index, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Mirror of char_find_3: searching backwards, an empty needle matches at the
     * far end rather than at the origin. Both empty cases are settled here because
     * the two _last values below underflow to the maximum USize when a size is 0. */
    if (data_size == 0) {
        USize const origin = self_index <= self_size ? self_size : CHAR_NPOS;

        trace_log_pop();

        return origin;
    }

    if (self_size == 0) {
        trace_log_pop();

        return CHAR_NPOS;
    }

    USize const data_last = data_size - 1;
    USize const self_last = self_size - 1;

    USize index = CHAR_NPOS;
    bool match = false;
    USize i = self_last + 1;

    while (i > self_index) {
        i -= 1;

        if (self[i] == data[data_last]) {
            bool full = true;
            USize k = i;
            USize j = data_last + 1;

            while (j > 0) {
                j -= 1;

                if (self[k] != data[j]) {
                    full = false;

                    break;
                }

                if (j == 0) {
                    break;
                }

                if (k == 0) {
                    full = false;

                    break;
                }

                k -= 1;
            }

            if (full && k >= self_index) {
                match = true;
                index = k;
            }
        }

        if (match) {
            break;
        }
    }

    trace_log_pop();

    return index;
}

char* char_find_slice_1(char const *const self, USize const self_index, char const c) {
    trace_log_push(LOG_METADATA);

    char const *const buffer = char_find_slice_2(self, char_length(self), self_index, c);

    trace_log_pop();

    return (char*) buffer;
}

char* char_find_slice_2(char const *const self, USize const self_size, USize const self_index, char const c) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_out_of_bound_uint(LOG_METADATA, "self_index", self_index, "self_size", self_size, "self_index > self_size", self_index > self_size);

    /* An empty self has no character to find, but its start is still a legal address
     * to scan from, so the search simply comes back empty-handed. Bounded by
     * self_size rather than by a terminator: self_size is documented as the buffer
     * extent, so an exact-length buffer that is not NUL-terminated is a legal input
     * and strchr would read past the end of it. */

    /* Repeats the bound as real control flow: error_check_* compiles away without
     * ERROR_CHECK_ENABLED, and self_size - self_index would then wrap and hand memchr
     * a length of ~2^64. An out-of-range origin finds nothing rather than running off. */
    if (self_index >= self_size) {
        trace_log_pop();

        return nullptr;
    }

    char const *const buffer = (char const*) memchr(&self[self_index], c, self_size - self_index);

    trace_log_pop();

    return (char*) buffer;
}

char* char_find_slice_3(char const *const self, USize const self_index, char const *const data) {
    trace_log_push(LOG_METADATA);

    char const *const buffer = char_find_slice_5(self, char_length(self), self_index, data, char_length(data));

    trace_log_pop();

    return (char*) buffer;
}

char* char_find_slice_4(char const *const self, USize const self_index, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    char const *const buffer = char_find_slice_5(self, char_length(self), self_index, data, data_size);

    trace_log_pop();

    return (char*) buffer;
}

char* char_find_slice_5(char const *const self, USize const self_size, USize const self_index, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_out_of_bound_uint(LOG_METADATA, "self_index", self_index, "self_size", self_size, "self_index > self_size", self_index > self_size);

    /* A needle longer than the remaining haystack is NOT FOUND, not a caller
     * bug: this used to abort, so scanning a 1-byte request path for ".."
     * killed the process - one GET was a whole-server DoS. */
    if (data_size > self_size - self_index) {
        trace_log_pop();

        return nullptr;
    }

    /* self_index may sit one past the last character: that is where an empty needle
     * matches. Routed through char_find_3 rather than strstr because both sizes are
     * explicit here, so neither buffer is required to be NUL-terminated - strstr
     * would read past the end of an exact-length self or data. */
    USize const found = char_find_3(self, self_size, self_index, data, data_size);
    char const *const buffer = found == CHAR_NPOS ? nullptr : &self[found];

    trace_log_pop();

    return (char*) buffer;
}

void char_format(char *const self, USize const self_capacity, char const *const format, ...) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_non_value_uint(LOG_METADATA, "self_capacity", self_capacity);
    error_check_null(LOG_METADATA, "format", (void*) format);

    va_list args = DEFAULT_INITIALIZATION;

    va_start(args, format);

    vsnprintf(self, (size_t) self_capacity, format, args);

    va_end(args);

    trace_log_pop();
}

void char_from_bytes_human_1(char *const self, USize const self_capacity, USize const bytes) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_non_value_uint(LOG_METADATA, "self_capacity", self_capacity);

    if (bytes < _CHAR_BYTES_HUMAN_UNIT_DIVISOR) {
        char_format(self, self_capacity, "%llu B", (unsigned long long) bytes);

        trace_log_pop();

        return;
    }

    char const *const units[] = { "K", "M", "G", "T", "P" };
    USize const unit_count    = sizeof(units) / sizeof(units[0]);
    FSize value               = (FSize) bytes / (FSize) _CHAR_BYTES_HUMAN_UNIT_DIVISOR;
    USize unit                = 0;

    while (value >= (FSize) _CHAR_BYTES_HUMAN_UNIT_DIVISOR && unit + 1 < unit_count) {
        value /= (FSize) _CHAR_BYTES_HUMAN_UNIT_DIVISOR;
        unit += 1;
    }

    char_format(self, self_capacity, "%.1f %s", value, units[unit]);

    trace_log_pop();
}

char char_from_number(U8 const number) {
    if (number <= 9) {
        return '0' + number;
    }

    return '\0';
}

void char_from_numbers_float_1(char *const self, USize const self_capacity, FSize const number) {
    trace_log_push(LOG_METADATA);

    char_from_numbers_float_2(self, self_capacity, number, _CHAR_FROM_NUMBERS_F_PRECISION_DEFAULT);

    trace_log_pop();
}

void char_from_numbers_float_2(char *const self, USize const self_capacity, FSize const number, U8 const precision) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_non_value_uint(LOG_METADATA, "self_capacity", self_capacity);
    error_check_out_of_bound_uint(LOG_METADATA, "precision", precision, "_CHAR_FLOAT_PRECISION_MAX", _CHAR_FLOAT_PRECISION_MAX, "precision > _CHAR_FLOAT_PRECISION_MAX",
        precision > _CHAR_FLOAT_PRECISION_MAX);

    /* NaN has no decimal form, and casting it to USize is UB (fuzzer-found): refuse before
     * any write. NaN fails every comparison, so test it by self-inequality; an infinity and
     * an out-of-range magnitude fall to the single range test after the sign below. */
    if (number != number) {
        if (self_capacity > 0) {
            self[0] = '\0';
        }

        trace_log_pop();

        return;
    }

    bool const negative = number < 0;
    FSize const magnitude = negative ? math_negate_f(number) : number;

    USize self_index = 0;

    if (negative) {
        if (self_capacity > 0) {
            self[0] = '-';
        }

        self_index = 1;
    }

    /* The sign can consume the whole buffer (self_capacity == 1). Delegating then
     * passes a zero self_capacity down to uint_2, whose zero-size precondition
     * ABORTS - the same value-dependent-abort class this batch removes, one
     * layer deeper. Refuse here instead. (Found by the fuzzer.) */
    if (self_index >= self_capacity) {
        if (self_capacity > 0) {
            self[0] = '\0';
        }

        trace_log_pop();

        return;
    }

    /* The magnitude must be representable before the cast: (USize) of an
     * out-of-range FSize is UB, and the integer part is written through the
     * now-bounded uint_1, which refuses (empty result) rather than overflowing. */
    if (magnitude >= (FSize) USIZE_MAX) {
        if (self_capacity > 0) {
            self[0] = '\0';
        }

        trace_log_pop();

        return;
    }

    /* Round the fraction BEFORE the integer part is written: a fraction that rounds up to
     * 1.0 carries into it (0.99999 at precision 4 is "1.0000"), where clamping to all nines
     * printed a value the input never had. */
    USize const scale = math_pow_u(10, precision);

    /* math_pow_u answers 0 past USize - a precision beyond _CHAR_FLOAT_PRECISION_MAX in an
     * unchecked build: refuse rather than render value + 1 over an all-zero fraction. */
    if (scale == 0) {
        self[0] = '\0';

        trace_log_pop();

        return;
    }

    USize integer_part = (USize) magnitude;
    USize fraction_digits = (USize) ((magnitude - (FSize) integer_part) * (FSize) scale + 0.5);

    if (fraction_digits >= scale) {
        integer_part += 1;
        fraction_digits = 0;
    }

    char_from_numbers_uint_1(self + self_index, self_capacity - self_index, integer_part);

    self_index = char_length(self);

    /* uint_1 refused (buffer too small): stop rather than append a '.' and
     * fraction digits onto an empty integer part. */
    if (self_index == 0 || (negative && self_index == 1)) {
        if (self_capacity > 0) {
            self[0] = '\0';
        }

        trace_log_pop();

        return;
    }

    /* No dangling point at precision 0: "3", printf's %.0f shape (the half was rounded
     * up into the integer part above). */
    if (precision == 0) {
        trace_log_pop();

        return;
    }

    /* Real control flow, not a diagnostic: without this the '.' and every
     * precision digit were written unbounded in a release build - and the
     * fit depends on the VALUE's digit count, so it is a runtime condition
     * rather than caller error (see uint_2). Refuses WHOLLY, leaving the empty
     * string: keeping the bare integer part would have made a refusal
     * indistinguishable from a successful format of a whole number, breaking
     * the family's "empty means refused" sentinel. */
    if (self_index + 1 + precision + CHAR_END_CHARACTER > self_capacity) {
        self[0] = '\0';

        trace_log_pop();

        return;
    }

    char_copy_2(self + self_index, ".", CHAR_STATIC_SIZE("."));

    self_index += 1;

    char_fill(self + self_index, precision, '0');

    for (U8 i = 0; i < precision; i += 1) {
        self[self_index + precision - 1 - i] = (char) ((fraction_digits % 10) + '0');

        fraction_digits /= 10;
    }

    self[self_index + precision] = '\0';

    trace_log_pop();
}

void char_from_numbers_int_1(char *const self, USize const self_capacity, ISize const number) {
    trace_log_push(LOG_METADATA);

    char_from_numbers_int_2(self, self_capacity, number, 0);

    trace_log_pop();
}

void char_from_numbers_int_2(char *const self, USize const self_capacity, ISize const number, U8 const padding) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_non_value_uint(LOG_METADATA, "self_capacity", self_capacity);

    bool const negative = number < 0;
    USize const magnitude = negative ? (USize) -(number + 1) + 1 : (USize) number;
    USize const sign = negative ? 1 : 0;

    USize digits = 1;

    for (USize n = magnitude; n >= 10; n /= 10) {
        digits += 1;
    }

    USize const required = padding + sign + digits + CHAR_END_CHARACTER;

    /* Bound against what this call actually writes, not against
     * ISIZE_DIGITS_MAX: that older guard admitted self_capacity == 20 while
     * ISIZE_MIN needs 21 (sign + 19 digits + NUL), overflowing by one byte,
     * and it ignored `padding` entirely. Refuses without aborting, as uint_2. */
    if (required > self_capacity) {
        if (self_capacity > 0) {
            self[0] = '\0';
        }

        trace_log_pop();

        return;
    }

    /* Layout is sign + padding + digits, matching _char_new_from_numbers_int:
     * the sign leads. Padding the sign INTO the number ("0000-123") produced a
     * string no parser accepts. */
    char_fill(self, sign + padding + digits, '0');

    if (negative) {
        self[0] = '-';
    }

    USize n = magnitude;

    for (USize k = 0; k < digits; k += 1) {
        self[sign + padding + digits - 1 - k] = (char) ((n % 10) + '0');
        n /= 10;
    }

    self[sign + padding + digits] = '\0';

    trace_log_pop();
}

void char_from_numbers_uint_1(char *const self, USize const self_capacity, USize const number) {
    trace_log_push(LOG_METADATA);

    char_from_numbers_uint_2(self, self_capacity, number, 0);

    trace_log_pop();
}

void char_from_numbers_uint_2(char *const self, USize const self_capacity, USize const number, U8 const padding) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_non_value_uint(LOG_METADATA, "self_capacity", self_capacity);

    USize digits = 1;

    for (USize n = number; n >= 10; n /= 10) {
        digits += 1;
    }

    USize const required = padding + digits + CHAR_END_CHARACTER;

    /* Refuse rather than truncate, and refuse WITHOUT aborting: self_capacity used
     * to be accepted and never read, so a 4-byte buffer took a 7-digit number
     * and wrote 4 bytes past it (live in csv, whose column count comes from the
     * parsed file). Whether the result fits depends on the VALUE, not on caller
     * correctness, so this is a runtime condition - the same reasoning that
     * removed char_find_slice_5's abort. A truncated number is
     * indistinguishable from a real one, so an empty result is the honest
     * failure; callers that must tell them apart check for it. */
    if (required > self_capacity) {
        if (self_capacity > 0) {
            self[0] = '\0';
        }

        trace_log_pop();

        return;
    }

    /* Pad first: the in-place twins never wrote this region, so callers reading
     * back through char_length walked whatever the buffer held before. */
    char_fill(self, padding + digits, '0');

    USize n = number;

    for (USize k = 0; k < digits; k += 1) {
        self[padding + digits - 1 - k] = (char) ((n % 10) + '0');
        n /= 10;
    }

    self[padding + digits] = '\0';

    trace_log_pop();
}

char* char_from_trim_1(char const *const self) {
    trace_log_push(LOG_METADATA);

    char *const buffer = char_from_trim_2(self, char_length(self));

    trace_log_pop();

    return buffer;
}

char* char_from_trim_2(char const *const self, USize const self_size) {
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    char *const buffer = _char_from_trim(self, self_size, nullptr);
#else
    char *const buffer = _char_from_trim(self, self_size);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return buffer;
}

bool char_glob_match_1(char const *const pattern, char const *const text) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "pattern", (void*) pattern);
    error_check_null(LOG_METADATA, "text", (void*) text);

    bool const matched = char_glob_match_2(pattern, char_length(pattern), text, char_length(text));

    trace_log_pop();

    return matched;
}

bool char_glob_match_2(char const *const pattern, USize const pattern_size, char const *const text, USize const text_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "pattern", (void*) pattern);
    error_check_null(LOG_METADATA, "text", (void*) text);

    USize pattern_index = 0;
    USize text_index    = 0;
    USize star_index    = 0;
    USize match_index   = 0;
    bool  has_star      = false;

    while (text_index < text_size) {
        /* The star branch MUST come first: a '*' in the pattern is always a
         * wildcard. With the equality branch first, a '*' in the TEXT made the
         * pattern's star match it as a literal byte and lose its wildcard role
         * ("*b" failed to match "*ab"). */
        if (pattern_index < pattern_size && pattern[pattern_index] == '*') {
            has_star = true;
            star_index = pattern_index;
            match_index = text_index;
            pattern_index += 1;
        }
        else if (pattern_index < pattern_size && (pattern[pattern_index] == '?' || pattern[pattern_index] == text[text_index])) {
            pattern_index += 1;
            text_index += 1;
        }
        else if (has_star) {
            pattern_index = star_index + 1;
            match_index += 1;
            text_index = match_index;
        }
        else {
            trace_log_pop();

            return false;
        }
    }

    while (pattern_index < pattern_size && pattern[pattern_index] == '*') {
        pattern_index += 1;
    }

    bool const matched = pattern_index == pattern_size;

    trace_log_pop();

    return matched;
}

bool char_is_alpha(char const c) {
    return char_is_lower(c) || char_is_upper(c);
}

bool char_is_lower(char const c) {
    return c >= 'a' && c <= 'z';
}

bool char_is_number(char const c) {
    return c >= '0' && c <= '9';
}

bool char_is_upper(char const c) {
    return c >= 'A' && c <= 'Z';
}

bool char_is_whitespace(char const c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

char* char_join_1(char const *const *const parts, USize const count, char const *const separator) {
    trace_log_push(LOG_METADATA);

    char *const buffer = char_join_2(parts, count, separator, char_length(separator));

    trace_log_pop();

    return buffer;
}

char* char_join_2(char const *const *const parts, USize const count, char const *const separator, USize const separator_size) {
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    char *const buffer = _char_join(parts, count, separator, separator_size, nullptr);
#else
    char *const buffer = _char_join(parts, count, separator, separator_size);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return buffer;
}

USize char_length(char const *const self) {
    error_check_null(LOG_METADATA, "self", (void*) self);

    /* Untraced: the module's hottest leaf, reached one to three times by every _1
     * forwarder - the str_at / string_at precedent, on ruling 826's measurement. */
    return strlen((char*) self);
}

void char_lower_1(char *const self) {
    trace_log_push(LOG_METADATA);

    char_lower_2(self, char_length(self));

    trace_log_pop();
}

void char_lower_2(char *const self, USize const self_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    for (USize i = 0; i < self_size; i += 1) {
        self[i] = char_to_lower(self[i]);
    }

    trace_log_pop();
}

void char_move(char **const self, char **const data) {
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    _char_move(self, data, nullptr);
#else
    _char_move(self, data);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();
}

char* char_new_1(USize const size) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "size", size);

#ifdef ARENA_IMPLEMENTATION
    char *const buffer = (char*) allocator_borrow(size + CHAR_END_CHARACTER, nullptr);
#else
    char *const buffer = (char*) allocator_borrow(size + CHAR_END_CHARACTER);
#endif // ARENA_IMPLEMENTATION

    /* The heap zeroes and never declines, but the terminator is a promise of this function,
     * not of the allocator behind it - see char_alloc_new_1. */
    if (!memory_empty(buffer)) {
        buffer[0] = '\0';
    }

    trace_log_pop();

    return buffer;
}

char* char_new_2(char const *const data) {
    trace_log_push(LOG_METADATA);

    char *const buffer = char_new_3(data, char_length(data));

    trace_log_pop();

    return buffer;
}

char* char_new_3(char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    char *const buffer = _char_new(data, data_size, nullptr);
#else
    char *const buffer = _char_new(data, data_size);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return buffer;
}

char* char_new_from_numbers_int_1(ISize const number) {
    trace_log_push(LOG_METADATA);

    char *const buffer = char_new_from_numbers_int_2(number, 0);

    trace_log_pop();

    return buffer;
}

char* char_new_from_numbers_int_2(ISize const number, U8 const padding) {
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    char *const buffer = _char_new_from_numbers_int(number, padding, nullptr);
#else
    char *const buffer = _char_new_from_numbers_int(number, padding);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return buffer;
}

char* char_new_from_numbers_uint_1(USize const number) {
    trace_log_push(LOG_METADATA);

    char *const buffer = char_new_from_numbers_uint_2(number, 0);

    trace_log_pop();

    return buffer;
}

char* char_new_from_numbers_uint_2(USize const number, U8 const padding) {
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    char *const buffer = _char_new_from_numbers_uint(number, padding, nullptr);
#else
    char *const buffer = _char_new_from_numbers_uint(number, padding);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return buffer;
}

char* char_new_replace_1(char const *const self, char const *const find, char const *const replace) {
    trace_log_push(LOG_METADATA);

    char *const buffer = char_new_replace_2(self, char_length(self), find, char_length(find), replace, char_length(replace));

    trace_log_pop();

    return buffer;
}

char* char_new_replace_2(char const *const self, USize const self_size, char const *const find, USize const find_size, char const *const replace, USize const replace_size) {
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    char *const buffer = _char_new_replace(self, self_size, find, find_size, replace, replace_size, nullptr);
#else
    char *const buffer = _char_new_replace(self, self_size, find, find_size, replace, replace_size);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return buffer;
}

char* char_new_slice_1(char const *const self, USize const self_index) {
    trace_log_push(LOG_METADATA);

    char *const buffer = char_new_slice_2(self, char_length(self), self_index);

    trace_log_pop();

    return buffer;
}

char* char_new_slice_2(char const *const self, USize const self_size, USize const self_index) {
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    char *const buffer = _char_new_slice(self, self_size, self_index, nullptr);
#else
    char *const buffer = _char_new_slice(self, self_size, self_index);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return buffer;
}

char* char_new_slice_range_1(char const *const self, USize const self_from, USize const self_to) {
    trace_log_push(LOG_METADATA);

    char *const buffer = char_new_slice_range_2(self, char_length(self), self_from, self_to);

    trace_log_pop();

    return buffer;
}

char* char_new_slice_range_2(char const *const self, USize const self_size, USize const self_from, USize const self_to) {
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    char *const buffer = _char_new_slice_range(self, self_size, self_from, self_to, nullptr);
#else
    char *const buffer = _char_new_slice_range(self, self_size, self_from, self_to);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return buffer;
}

U8 char_raw_to_hex(char const c) {
    if (c >= '0' && c <= '9') { return (U8) (c - '0'); }
    if (c >= 'a' && c <= 'f') { return (U8) (c - 'a' + 10); }
    if (c >= 'A' && c <= 'F') { return (U8) (c - 'A' + 10); }

    return 0xFF;
}

void char_remove_1(char **const self, USize const index) {
    trace_log_push(LOG_METADATA);

    char_remove_2(self, char_length(*self), index);

    trace_log_pop();
}

void char_remove_2(char **const self, USize const self_size, USize const index) {
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    _char_remove(self, self_size, index, nullptr);
#else
    _char_remove(self, self_size, index);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();
}

char* char_repeat_1(char const *const data, USize const count) {
    trace_log_push(LOG_METADATA);

    char *const buffer = char_repeat_2(data, char_length(data), count);

    trace_log_pop();

    return buffer;
}

char* char_repeat_2(char const *const data, USize const data_size, USize const count) {
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    char *const buffer = _char_repeat(data, data_size, count, nullptr);
#else
    char *const buffer = _char_repeat(data, data_size, count);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return buffer;
}

void char_replace_1(char *const self, USize const self_index, char const *const data) {
    trace_log_push(LOG_METADATA);

    char_replace_2(self, self_index, data, char_length(data));

    trace_log_pop();
}

void char_replace_2(char *const self, USize const self_index, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Writing no bytes still terminates at self_index, which is what the splice
     * callers above rely on when the tail they hand over is empty. */
    for (USize i = 0; i < data_size; i += 1) {
        self[self_index + i] = data[i];
    }

    self[self_index + data_size] = '\0';

    trace_log_pop();
}

void char_reverse_1(char *const self) {
    trace_log_push(LOG_METADATA);

    char_reverse_2(self, char_length(self));

    trace_log_pop();
}

void char_reverse_2(char *const self, USize const self_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    for (USize i = 0; i < self_size / 2; i += 1) {
        char const temp = self[i];

        self[i] = self[self_size - 1 - i];
        self[self_size - 1 - i] = temp;
    }

    trace_log_pop();
}

char* char_slice_1(char const *const self, USize const self_index) {
    trace_log_push(LOG_METADATA);

    char *const buffer = char_slice_2(self, char_length(self), self_index);

    trace_log_pop();

    return buffer;
}

char* char_slice_2(char const *const self, USize const self_size, USize const self_index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    /* An empty source is a VALUE: slicing "" at 0 is the ordinary "remainder
     * after the last delimiter" case, and aborting on it was the same class as
     * the empty-append and over-long-needle aborts removed this round. */
    error_check_out_of_bound_uint(LOG_METADATA, "self_index", self_index, "self_size", self_size, "self_index > self_size", self_index > self_size);

    trace_log_pop();

    return (char*) self + self_index;
}

char* char_split_1(char const *const self, char const *const delimiter) {
    trace_log_push(LOG_METADATA);

    char *const buffer = char_split_2(self, char_length(self), delimiter);

    trace_log_pop();

    return buffer;
}

char* char_split_2(char const *const self, USize const self_size, char const *const delimiter) {
    trace_log_push(LOG_METADATA);

    char *const buffer = char_split_3(self, self_size, delimiter, char_length(delimiter));

    trace_log_pop();

    return buffer;
}

char* char_split_3(char const *const self, USize const self_size, char const *const delimiter, USize const delimiter_size) {
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    char *const buffer = _char_split(self, self_size, delimiter, delimiter_size, nullptr);
#else
    char *const buffer = _char_split(self, self_size, delimiter, delimiter_size);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return buffer;
}

bool char_split_next(char const *const self,
    USize const self_size, char const *const delimiter, USize const delimiter_size, USize *const index, USize *const token_from, USize *const token_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "delimiter", (void*) delimiter);
    error_check_null(LOG_METADATA, "index", (void*) index);
    error_check_null(LOG_METADATA, "token_from", (void*) token_from);
    error_check_null(LOG_METADATA, "token_size", (void*) token_size);

    if (*index == CHAR_NPOS) {
        trace_log_pop();

        return false;
    }

    /* index is caller-owned, so a resumed or hand-built cursor can land past the end,
     * and both self_size - *index sites below would wrap to a token size of ~2^64.
     * Real control flow with no error_check beside it: a kept one aborted here in every
     * checked build, leaving this refusal dead (the char_at half-fix). */
    if (*index > self_size) {
        trace_log_pop();

        return false;
    }

    /* An empty delimiter marks no split point, so the remainder is one final token.
     * Handled here rather than through char_find_3, whose empty-needle answer is the
     * search origin itself: that would leave index unmoved and never terminate. */
    if (delimiter_size == 0) {
        *token_from = *index;
        *token_size = self_size - *index;
        *index = CHAR_NPOS;

        trace_log_pop();

        return true;
    }

    USize const found = char_find_3(self, self_size, *index, delimiter, delimiter_size);

    *token_from = *index;

    if (found == CHAR_NPOS) {
        *token_size = self_size - *index;
        *index = CHAR_NPOS;
    }
    else {
        *token_size = found - *index;
        *index = found + delimiter_size;
    }

    trace_log_pop();

    return true;
}

bool char_starts_with_1(char const *const self, char const *const prefix) {
    trace_log_push(LOG_METADATA);

    bool const starts = char_starts_with_2(self, char_length(self), prefix, char_length(prefix));

    trace_log_pop();

    return starts;
}

bool char_starts_with_2(char const *const self, USize const self_size, char const *const prefix, USize const prefix_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "prefix", (void*) prefix);

    bool starts = prefix_size <= self_size;

    if (starts) {
        for (USize i = 0; i < prefix_size; i += 1) {
            if (self[i] != prefix[i]) {
                starts = false;

                break;
            }
        }
    }

    trace_log_pop();

    return starts;
}

char char_to_lower(char const c) {
    if (c >= 'A' && c <= 'Z') {
        return (char) (c + ('a' - 'A'));
    }

    return c;
}

U8 char_to_number(char const number) {
    if (number >= '0' && number <= '9') {
        return number - '0';
    }

    return 0xFF;
}

FSize char_to_numbers_float_1(char const *const number) {
    trace_log_push(LOG_METADATA);

    FSize const value = char_to_numbers_float_2(number, char_length(number));

    trace_log_pop();

    return value;
}

FSize char_to_numbers_float_2(char const *const number, USize const number_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "number", (void*) number);

    bool fraction_found = false;
    bool negation_found = false;

    /* The integer part accumulates in FSize, which cannot wrap: an over-long run loses
     * precision and eventually reaches infinity, where the old USize accumulator turned a
     * huge value into a small PLAUSIBLE one - the wrap ruling 942 removed from the int/uint
     * twins. The fraction stays exact in USize and stops taking digits once another would
     * overflow it (nineteen already sit below FSize's 53-bit resolution), which keeps its
     * scale representable and retires the U8 digit counter that wrapped at 256. */
    FSize integer  = 0;
    USize fraction = 0;
    FSize scale    = 1;

    for (USize i = 0; i < number_size; i += 1) {
        if (char_is_number(number[i])) {
            USize const digit = (USize) (number[i] - '0');

            if (!fraction_found) {
                integer = integer * 10 + (FSize) digit;
            }
            else if (fraction <= (USIZE_MAX - digit) / 10) {
                fraction = fraction * 10 + digit;
                scale    = scale * 10;
            }
        }
        else if (number[i] == '.') {
            fraction_found = true;
        }
        else if (number[i] == '-') {
            negation_found = true;
        }
        else {
            /* Stop at the first byte that is not part of a number - the
             * documented lenient behavior, which the break already implements.
             * The error_check here ABORTED instead: digit-ness is a property of
             * the DATA, not of caller correctness, so "?limit=abc" reaching
             * char_to_numbers_uint_1 killed the process. Same class as the
             * empty-value and over-long-needle aborts removed this round; use
             * char_try_to_number_* when a caller must detect the bad byte. */
            break;
        }
    }

    FSize numbers_f = integer + (FSize) fraction / scale;

    if (negation_found) {
        numbers_f = math_negate_f(numbers_f);
    }

    trace_log_pop();

    return numbers_f;
}

ISize char_to_numbers_int_1(char const *const number) {
    trace_log_push(LOG_METADATA);

    ISize const value = char_to_numbers_int_2(number, char_length(number));

    trace_log_pop();

    return value;
}

ISize char_to_numbers_int_2(char const *const number, USize const number_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "number", (void*) number);

    /* Parsing nothing yields 0 - the loop below already produces 0 for an empty input,
     * so the old guard only turned a value into a crash. Note that 0 is indistinguishable
     * from a parsed "0"; use char_try_to_number_i when the caller must tell them apart. */
    bool negation_found = false;
    /* The magnitude accumulates UNSIGNED: parsing ISIZE_MIN's own formatted
     * text needs 2^63, one past ISIZE_MAX, and signed overflow is UB (found by
     * the fuzzer round-tripping the formatter's output). Saturates instead of
     * wrapping; use char_try_to_number_i when the caller must detect the
     * overflow rather than absorb it. */
    USize magnitude = 0;
    bool saturated = false;

    for (USize i = 0; i < number_size; i += 1) {
        if (char_is_number(number[i])) {
            USize const digit = (USize) (number[i] - '0');

            if (magnitude > (USIZE_MAX - digit) / 10) {
                saturated = true;

                continue;
            }

            magnitude = magnitude * 10 + digit;
        }
        else if (number[i] == '-') {
            negation_found = true;
        }
        else {
            /* Stop at the first byte that is not part of a number - the
             * documented lenient behavior, which the break already implements.
             * The error_check here ABORTED instead: digit-ness is a property of
             * the DATA, not of caller correctness, so "?limit=abc" reaching
             * char_to_numbers_uint_1 killed the process. Same class as the
             * empty-value and over-long-needle aborts removed this round; use
             * char_try_to_number_* when a caller must detect the bad byte. */
            break;
        }
    }

    /* Clamp to the signed range before converting: the magnitude is unsigned
     * and ISIZE_MIN's magnitude (2^63) is representable only as the negative. */
    USize const limit = negation_found ? (USize) ISIZE_MAX + 1 : (USize) ISIZE_MAX;

    if (saturated || magnitude > limit) {
        magnitude = limit;
    }

    ISize numbers = negation_found
        ? (magnitude == (USize) ISIZE_MAX + 1 ? ISIZE_MIN : -(ISize) magnitude)
        : (ISize) magnitude;

    trace_log_pop();

    return numbers;
}

USize char_to_numbers_uint_1(char const *const number) {
    trace_log_push(LOG_METADATA);

    USize const value = char_to_numbers_uint_2(number, char_length(number));

    trace_log_pop();

    return value;
}

USize char_to_numbers_uint_2(char const *const number, USize const number_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "number", (void*) number);

    /* Parsing nothing yields 0 - see char_to_numbers_int_2. Use char_try_to_number_u
     * when an empty input must be distinguished from a parsed "0". */
    USize numbers = 0;
    bool saturated = false;

    for (USize i = 0; i < number_size; i += 1) {
        if (!char_is_number(number[i])) {
            /* Stop at the first byte that is not part of a number - the
             * documented lenient behavior, which the break already implements.
             * The error_check here ABORTED instead: digit-ness is a property of
             * the DATA, not of caller correctness, so "?limit=abc" reaching
             * char_to_numbers_uint_1 killed the process. Same class as the
             * empty-value and over-long-needle aborts removed this round; use
             * char_try_to_number_* when a caller must detect the bad byte. */
            break;
        }

        USize const digit = (USize) (number[i] - '0');

        /* Saturate, matching the signed twin: a wrap turns a huge
         * attacker-supplied number into a small PLAUSIBLE one, which sails
         * through a downstream range check on a length, index or amount,
         * whereas USIZE_MAX obviously fails it. char_try_to_number_u is the
         * detecting form. */
        if (saturated || numbers > (USIZE_MAX - digit) / 10) {
            saturated = true;

            continue;
        }

        numbers = numbers * 10 + digit;
    }

    if (saturated) {
        numbers = USIZE_MAX;
    }

    trace_log_pop();

    return numbers;
}

char char_to_upper(char const c) {
    if (c >= 'a' && c <= 'z') {
        return (char) (c - ('a' - 'A'));
    }

    return c;
}

void char_trim_1(char **const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "*self", (void*) *self);

    char_trim_2(self, char_length(*self));

    trace_log_pop();
}

void char_trim_2(char **const self, USize const self_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "*self", (void*) *self);

    char *const buffer = *self;

    *self = char_from_trim_2(*self, self_size);

#ifdef ARENA_IMPLEMENTATION
    allocator_release(buffer, nullptr);
#else
    allocator_release(buffer);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();
}

bool char_try_to_bool(char const *const value, bool *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "value", (void*) value);
    error_check_null(LOG_METADATA, "out", (void*) out);

    bool success = false;

    if (value[0] != '\0') {
        if (char_compare_iequal_1(value, "1") || char_compare_iequal_1(value, "true") || char_compare_iequal_1(value, "yes") || char_compare_iequal_1(value, "on")) {
            *out = true;

            success = true;
        }
        else if (char_compare_iequal_1(value, "0") || char_compare_iequal_1(value, "false") || char_compare_iequal_1(value, "no") || char_compare_iequal_1(value, "off")) {
            *out = false;

            success = true;
        }
    }

    trace_log_pop();

    return success;
}

bool char_try_to_number_f(char const *const value, FSize *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "value", (void*) value);
    error_check_null(LOG_METADATA, "out", (void*) out);

    char *end = nullptr;

    FSize const parsed = (FSize) strtod(value, &end);

    // (parsed == parsed) rejects NaN; (parsed - parsed == 0.0) rejects +/-inf, which is the
    // overflow answer. errno is not consulted: an underflow (ERANGE with a denormal or zero)
    // is a legal tiny value, not a failure.
    bool const success = (end != value) && (*end == '\0') && (parsed == parsed) && (parsed - parsed == 0.0);

    if (success) {
        *out = parsed;
    }

    trace_log_pop();

    return success;
}

bool char_try_to_number_i(char const *const value, ISize *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "value", (void*) value);
    error_check_null(LOG_METADATA, "out", (void*) out);

    char *end = nullptr;

    errno = 0;
    ISize const parsed = (ISize) strtoll(value, &end, 10);
    bool const success = (errno == 0) && (end != value) && (*end == '\0');

    if (success) {
        *out = parsed;
    }

    trace_log_pop();

    return success;
}

bool char_try_to_number_u(char const *const value, USize *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "value", (void*) value);
    error_check_null(LOG_METADATA, "out", (void*) out);

    char *end = nullptr;

    errno = 0;
    USize const parsed = (USize) strtoull(value, &end, 10);
    bool success       = (errno == 0) && (end != value) && (*end == '\0');

    // strtoull skips leading whitespace and silently negates a '-' (wrapping mod 2^64),
    // so reject any '-' in the consumed span rather than only at index 0.
    for (char const *scan = value; success && scan < end; scan += 1) {
        if (*scan == '-') {
            success = false;
        }
    }

    if (success) {
        *out = parsed;
    }

    trace_log_pop();

    return success;
}

void char_upper_1(char *const self) {
    trace_log_push(LOG_METADATA);

    char_upper_2(self, char_length(self));

    trace_log_pop();
}

void char_upper_2(char *const self, USize const self_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    for (USize i = 0; i < self_size; i += 1) {
        self[i] = char_to_upper(self[i]);
    }

    trace_log_pop();
}

char* char_wrap_1(char const *const self, USize const width) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    char *const wrapped = char_wrap_2(self, char_length(self), width);

    trace_log_pop();

    return wrapped;
}

char* char_wrap_2(char const *const self, USize const self_size, USize const width) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_non_value_uint(LOG_METADATA, "width", width);

    // Output never exceeds the input length: each word is preceded by exactly one
    // separator byte (a space or a newline) that replaces the input whitespace run.
    // self_size + CHAR_END_CHARACTER is passed as the CONTENT size, so the buffer is one
    // byte larger than needed - deliberately: it keeps an empty input clear of char_new_1's
    // zero-size abort, and "" wraps to "".
    char *const out = char_new_1(self_size + CHAR_END_CHARACTER);

    USize out_length  = 0;
    USize line_length = 0;
    USize index       = 0;

    while (index < self_size) {
        while (index < self_size && char_is_whitespace(self[index])) {
            index += 1;
        }

        if (index >= self_size) {
            break;
        }

        USize const word_start = index;

        while (index < self_size && !char_is_whitespace(self[index])) {
            index += 1;
        }

        USize const word_length = index - word_start;

        if (line_length > 0 && line_length + 1 + word_length > width) {
            out[out_length] = '\n';

            out_length += 1;
            line_length = 0;
        }
        else if (line_length > 0) {
            out[out_length] = ' ';

            out_length += 1;
            line_length += 1;
        }

        for (USize i = 0; i < word_length; i += 1) {
            out[out_length + i] = self[word_start + i];
        }

        out_length += word_length;
        line_length += word_length;
    }

    out[out_length] = '\0';

    trace_log_pop();

    return out;
}