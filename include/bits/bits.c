#include <bits/bits.h>

/*==============================================================================
 * MARK: - Private Constants
 *============================================================================*/
/* The terminator slot a rendering needs past its characters; a private twin of
 * char's CHAR_END_CHARACTER, because a leaf module does not include char for one
 * constant. */
#define _BITS_TERMINATOR_SIZE 1

/*==============================================================================
 * MARK: - Private Functions
 *============================================================================*/
/* The group a rendering actually uses: a '\0' separator and a group_size of 0
 * are two spellings of "no separator", and only this one reaches the loop, so
 * a terminator can never be embedded mid-rendering. */
static U8 _bits_format_group(char const separator, U8 const group_size) {
    return separator == '\0' ? 0 : group_size;
}

/* Characters a rendering occupies before its terminator: one per bit, plus one
 * separator between every full group counted from the right. A self_size of 0
 * answers 0 rather than evaluating self_size - 1: error_check arguments are
 * evaluated in every build, so this runs before the non-value check can abort. */
static USize _bits_format_length(U8 const self_size, U8 const group_size) {
    USize const separators = self_size == 0 || group_size == 0 ? 0 : (USize) (self_size - 1) / group_size;

    return (USize) self_size + separators;
}

/*==============================================================================
 * MARK: - Public Functions
 *============================================================================*/
bool bits_array_any(U64 const *const self, USize const self_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    bool result = false;

    for (USize i = 0; i < self_size && !result; i += 1) {
        result = self[i] != 0;
    }

    trace_log_pop();

    return result;
}

void bits_array_clear(U64 *const self, USize const self_size, USize const bit) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_out_of_bound_uint(LOG_METADATA, "bit", bit, "self_size (words)", self_size, "bit / 64 >= self_size", bit / BITS_WORD_BITS >= self_size);

    if (bit / BITS_WORD_BITS < self_size) {
        self[bit / BITS_WORD_BITS] &= ~((U64) 1 << (bit % BITS_WORD_BITS));
    }

    trace_log_pop();
}

void bits_array_clear_all(U64 *const self, USize const self_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (self_size > 0) {
        memory_set(self, self_size * sizeof(U64), 0);
    }

    trace_log_pop();
}

USize bits_array_count(U64 const *const self, USize const self_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    USize count = 0;

    for (USize i = 0; i < self_size; i += 1) {
        count += (USize) __builtin_popcountll(self[i]);
    }

    trace_log_pop();

    return count;
}

void bits_array_set(U64 *const self, USize const self_size, USize const bit) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_out_of_bound_uint(LOG_METADATA, "bit", bit, "self_size (words)", self_size, "bit / 64 >= self_size", bit / BITS_WORD_BITS >= self_size);

    if (bit / BITS_WORD_BITS < self_size) {
        self[bit / BITS_WORD_BITS] |= (U64) 1 << (bit % BITS_WORD_BITS);
    }

    trace_log_pop();
}

bool bits_array_test(U64 const *const self, USize const self_size, USize const bit) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_out_of_bound_uint(LOG_METADATA, "bit", bit, "self_size (words)", self_size, "bit / 64 >= self_size", bit / BITS_WORD_BITS >= self_size);

    bool result = false;

    if (bit / BITS_WORD_BITS < self_size) {
        result = ((self[bit / BITS_WORD_BITS] >> (bit % BITS_WORD_BITS)) & (U64) 1) != 0;
    }

    trace_log_pop();

    return result;
}

bool bits_at(USize const self, U8 const index) {
    trace_log_push(LOG_METADATA);

    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "63", 63, "index > 63", index > 63);

    trace_log_pop();

    return index <= 63 && ((self >> index) & (USize) 1) != 0;
}

U8 bits_count(USize const self) {
    trace_log_push(LOG_METADATA);

    U8 const result = (U8) __builtin_popcountll((U64) self);

    trace_log_pop();

    return result;
}

U8 bits_first_set(USize const self) {
    trace_log_push(LOG_METADATA);

    /* A zero word is data, not an error: the builtin is undefined on 0, so the
     * sentinel answers first. */
    U8 const result = self == 0 ? BITS_INDEX_NONE : (U8) __builtin_ctzll((U64) self);

    trace_log_pop();

    return result;
}

void bits_flip(USize *const self, U8 const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "63", 63, "index > 63", index > 63);

    if (index <= 63) {
        *self ^= (USize) 1 << index;
    }

    trace_log_pop();
}

USize bits_format(USize const self, U8 const self_size, char const separator, U8 const group_size, char *const buffer, USize const buffer_capacity) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "buffer", (void*) buffer);
    error_check_non_value_uint(LOG_METADATA, "self_size", self_size);
    error_check_out_of_bound_uint(LOG_METADATA, "self_size", self_size, "64", 64, "self_size > 64", self_size > 64);
    error_check_wrong_value(LOG_METADATA, "buffer_capacity", buffer_capacity < _bits_format_length(self_size, _bits_format_group(separator, group_size)) + _BITS_TERMINATOR_SIZE);

    /* The unchecked-build fallback for a null buffer: there is nowhere to write
     * even an empty rendering, so the answer is a length of nothing. */
    if (memory_empty(buffer)) {
        trace_log_pop();

        return 0;
    }

    /* The unchecked-build fallbacks of the three value checks above: an empty
     * rendering is the one answer that can never overrun the caller's buffer. */
    if (self_size == 0 || self_size > 64 || buffer_capacity < _bits_format_length(self_size, _bits_format_group(separator, group_size)) + _BITS_TERMINATOR_SIZE) {
        buffer[0] = '\0';

        trace_log_pop();

        return 0;
    }

    U8    const group   = _bits_format_group(separator, group_size);
    USize       written = 0;

    /* Most significant bit first; a separator after every digit whose bit index
     * is a non-zero multiple of the group, which groups from the right. */
    for (U8 bit = self_size; bit > 0; bit -= 1) {
        U8 const index = bit - 1;

        buffer[written] = ((self >> index) & (USize) 1) != 0 ? '1' : '0';

        written += 1;

        if (group != 0 && index != 0 && index % group == 0) {
            buffer[written] = separator;

            written += 1;
        }
    }

    buffer[written] = '\0';

    trace_log_pop();

    return written;
}

U8 bits_last_set(USize const self) {
    trace_log_push(LOG_METADATA);

    U8 const result = self == 0 ? BITS_INDEX_NONE : (U8) (63 - __builtin_clzll((U64) self));

    trace_log_pop();

    return result;
}

void bits_print_1(USize const self, U8 const self_size) {
    trace_log_push(LOG_METADATA);

    bits_print_2(self, self_size, ' ');

    trace_log_pop();
}

void bits_print_2(USize const self, U8 const self_size, char const separator) {
    trace_log_push(LOG_METADATA);

    char buffer[BITS_FORMAT_CAPACITY] = DEFAULT_INITIALIZATION;

    /* bits_format carries the checks, the unchecked-build fallback, and the rule
     * that a '\0' separator means none; an empty rendering prints nothing at
     * all, not an empty line. */
    USize const written = bits_format(self, self_size, separator, BITS_IN_BYTE, buffer, sizeof(buffer));

    if (written > 0) {
        fputs(buffer, stdout);
        fputc('\n', stdout);
    }

    trace_log_pop();
}

void bits_write(USize *const self, U8 const index, bool const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "63", 63, "index > 63", index > 63);

    if (index <= 63) {
        if (data) {
            *self |= (USize) 1 << index;
        }
        else {
            *self &= ~((USize) 1 << index);
        }
    }

    trace_log_pop();
}