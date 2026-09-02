/*
 * string.h - Canonical String Object for the C Libraries Framework
 *
 * Provides a high-level, mutable string object with dynamic or arena-backed memory management.
 *
 * Features:
 *   - Dynamic and arena-allocated string support
 *   - Interoperability with Str and C strings
 *   - API for construction, mutation, comparison, slicing, and conversion
 *   - Canonical error checking and memory safety
 *
 * Usage Examples:
 *   @code
 *   String str = string_init_1();
 *   string_add_1(&str, "Hello", 0);
 *   string_upper(&str);
 *   string_clear(&str);
 *   @endcode
 *
 * Error Handling:
 *   - Two classes of bad argument, treated differently. PROGRAMMING errors - a null self or
 *     data pointer, a zero capacity, a slice strictly past the end - go through error_check_*,
 *     which ABORTS under ERROR_CHECK_ENABLED and compiles to nothing otherwise; nothing here
 *     "returns early" on them. DATA-shaped values - indices, spans, sizes and rendered lengths
 *     that arrive from input - REFUSE as no-ops in every build (string_add_2, string_erase,
 *     string_remove, string_replace_2, string_repeat, string_set_size, string_format's growth
 *     on a refused arena), and each such function notes what it refuses and to what.
 *
 * Thread Safety:
 *   - Not thread-safe. Caller must synchronize if used from multiple threads.
 *   - A VIEW shares its buffer with the object it viewed: two Strings, one buffer, and no
 *     synchronization between them.
 *
 * Memory Management:
 *   - A String is either an OWNER or a VIEW; `owned` tells them apart (see the String
 *     typedef below for the full ownership contract).
 *   - Functions returning `String*` allocate the struct; free with string_delete().
 *     By-value Strings release their buffer via string_uninit().
 *   - Arena-backed `alloc` variants take `Arena *const allocator` last and release
 *     back to the same arena.
 *
 * Performance Characteristics:
 *   - string_add_* grows geometrically through string_reserve, so appending in a
 *     loop is amortized O(n). Comparison, search, split, and replace operations are
 *     linear in the sizes involved.
 *   - string_clear is O(old size): it scrubs the payload. string_shrink and string_trim
 *     always allocate (shrink only when not already exact-fit); string_format grows exact-fit,
 *     not geometrically; string_join reserves once for the whole result.
 *
 * Dependencies:
 *   - container/str/str.h
 *
 * See string.c for implementation details.
 */
#ifndef CONTAINER_STRING_H
#define CONTAINER_STRING_H

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/
#include <container/str/str.h>

/*==============================================================================
 * MARK: - Typedefs
 *============================================================================*/
/**
 * @brief Mutable string object (dynamic or arena-backed).
 *
 * A String is either an OWNER or a VIEW, and `owned` is what tells them apart.
 *
 * VIEW - takes the caller's pointer and never releases it: string_init_3/_4,
 * string_new_3/_4 and their arena twins. The memory may be an lws payload, a
 * request's interior buffer, or a string literal.
 *
 * OWNER - allocates its own buffer, released by string_uninit: string_init_2,
 * string_init_static, string_init_5 and string_init_6 (DEEP copies of a Str / a String -
 * the module's owning-copy constructors, with their _alloc and _new twins),
 * string_from_replace_*, string_from_trim, string_join_*, string_split_1.._4 (an owned
 * copy of the FIRST token), string_slice, string_slice_range, string_wrap,
 * string_from_numbers_*, string_move_2, and any growth through string_reserve or
 * string_shrink.
 *
 * INHERITS - string_move_3/_4 carry the source's flag over, so moving a view yields
 * a view, and they clear the source so only one String ever claims the buffer.
 *
 * PROMOTES - string_repeat, string_copy and string_format write in place while the result
 * still fits, and become OWNERs the moment they grow through string_reserve; string_trim
 * ALWAYS reallocates, so it promotes on every call. On a VIEW, string_format writes straight
 * through the borrowed pointer up to the last claimed byte before it grows - see the
 * capacity rule below - and note that "while the result still fits" IS the
 * reaching-the-last-byte case, not the safe one. string_shrink goes the other way at
 * size 0: it releases and demotes to the empty state.
 *
 * EMPTY - string_init_1 owns nothing and holds no buffer (`data == nullptr`,
 * `owned == false`); string_uninit on it is a no-op. Two words, two facts: EMPTY is this
 * STATE (no buffer), "empty" elsewhere in this header means size 0 over ANY buffer - an
 * owned zero-length String is empty but not EMPTY. string_empty answers the size; test
 * `data == nullptr` for the state.
 *
 * Producers of NOTHING answer the EMPTY String, not an owned zero-length one: a slice at
 * size, a split whose first token is empty, a wrap or join of nothing, a replace over an
 * empty source, a copying constructor over an empty source (string_init_static/_5/_6 and
 * their arena twins) - and, on an arena, a REFUSED borrow answers the same EMPTY String, so a
 * caller cannot tell "nothing to produce" from "refused" by the result alone. Only
 * string_trim / string_from_trim / string_alloc_from_trim answer an owned zero-length
 * String for an all-whitespace source. An EMPTY result still carries the source's
 * allocator, so bulk release stays honest.
 *
 * The capacity rule for views and the terminator byte - which writers may touch index
 * data_size, and why an exactly-sized view is a violation waiting to happen - is the
 * section right after this typedef.
 *
 * Before this field existed, _string_uninit released whenever `data != nullptr`, so
 * uninit-ing a String built over borrowed memory freed an interior pointer or a
 * string literal. http_server worked around it with a separate `payload_borrowed`
 * bool kept alongside the String; that workaround is what this field replaces.
 *
 * The default is false, so a path that forgets to set it yields a view - that leaks
 * at worst, where the opposite default corrupts the heap. The guarantee covers
 * CONSTRUCTORS only: any function that releases or reallocates `data` (string_reserve
 * and everything that grows through it) must consult `owned` before releasing, or it
 * frees the caller's memory.
 */
typedef struct {
#ifdef ARENA_IMPLEMENTATION
    Arena *allocator; /**< Arena allocator, if used. */
#endif // ARENA_IMPLEMENTATION
    USize capacity;   /**< Allocated buffer size. */
    char *data;       /**< Pointer to string data. */
    USize size;       /**< Current string length. */
    bool owned;       /**< True when this String owns data and string_uninit must release it. */
} String;

/*==============================================================================
 * MARK: - Views and the terminator byte
 *============================================================================*/

/*
 * A VIEW's buffer must be WRITABLE and carry at least data_size + CHAR_END_CHARACTER
 * bytes, because a view claims that capacity and the in-place writers use it. A view
 * over a string literal is a segfault the first time anything writes through it.
 *
 * THAT EXTRA BYTE IS A PRECONDITION ON YOUR BUFFER, not a courtesy the writers extend. The
 * rule is: every writer that can place a byte at an index THE CURRENT BUFFER BOUNDS ONLY BY
 * CAPACITY - a terminator at the size the write leaves behind, or payload filled up to
 * capacity - may write index data_size. Do not carry a census of which ones; that list has
 * been written twice and been wrong twice. Apply the rule.
 *
 * A capacity comparison alone is NOT the test. string_remove writes its terminator at the
 * LOWERED size, and string_shrink writes into the buffer it has just
 * borrowed rather than the borrowed one - neither can reach data_size. The known ones that
 * can, as examples of the rule:
 *   - string_replace_2, whose own comment says the bound deliberately omits the terminator
 *     byte so string_add_2 can fill capacity exactly before growing;
 *   - string_copy, on its in-place branch (capacity > data->size), terminating at data->size;
 *   - string_format, which hands self->capacity to vsnprintf - and re-renders into a grown
 *     buffer only AFTER that first render has already written through the borrowed pointer,
 *     so the promotion does not undo it. The rendered length is DATA;
 *   - string_repeat at count == 1, where the reserve is a no-op and the terminator
 *     lands at index data_size;
 *   - and an APPEND, once ANY call has lowered `size` while leaving capacity alone (clear,
 *     erase, remove, repeat at count 0, a lowering string_set_size). On a view AS CONSTRUCTED
 *     an append always outgrows capacity and PROMOTES to an owner; that stops being true after
 *     the first call that frees bytes.
 *
 * The writers that bound on SIZE - string_clear, string_fill, string_lower, string_upper,
 * string_reverse, string_erase - stay inside the payload and merely destroy it, SO LONG AS
 * `size` STILL DESCRIBES THE PAYLOAD. string_set_size keeps that invariant by refusing a size
 * the buffer cannot hold (past capacity - 1); it cannot make `size` lie about the allocation,
 * only about the payload's contents, which is the caller's business.
 *
 * The practical consequence: a view over a buffer sized EXACTLY data_size - an lws payload, a
 * slice of a request buffer - VIOLATES THIS CONTRACT before anything writes. Storing such a
 * view is safe, because nothing ever frees what a view did not claim; writing to it is not. */

/*==============================================================================
 * MARK: - Arena-backed API
 *============================================================================*/
#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Create a String from a floating-point number using an arena allocator.
 * @param number Floating-point value.
 * @param allocator Arena allocator to use.
 * @return String by value; OWNED, release with string_uninit.
 */
String string_alloc_from_numbers_float_1(FSize const number, Arena *const allocator);

/**
 * @brief Create a String from a floating-point number with precision using an arena allocator.
 * @param number Floating-point value.
 * @param precision Number of digits after the decimal point.
 * @param allocator Arena allocator to use.
 * @return String by value; OWNED, release with string_uninit.
 */
String string_alloc_from_numbers_float_2(FSize const number, U8 const precision, Arena *const allocator);

/**
 * @brief Create a String from an integer using an arena allocator.
 * @param number Integer value.
 * @param allocator Arena allocator to use.
 * @return String by value; OWNED, release with string_uninit.
 */
String string_alloc_from_numbers_int_1(ISize const number, Arena *const allocator);

/**
 * @brief Create a String from an integer with padding using an arena allocator.
 * @param number Integer value.
 * @param padding Number of padding characters.
 * @param allocator Arena allocator to use.
 * @return String by value; OWNED, release with string_uninit.
 */
String string_alloc_from_numbers_int_2(ISize const number, U8 const padding, Arena *const allocator);

/**
 * @brief Create a String from an unsigned integer using an arena allocator.
 * @param number Unsigned integer value.
 * @param allocator Arena allocator to use.
 * @return String by value; OWNED, release with string_uninit.
 */
String string_alloc_from_numbers_uint_1(USize const number, Arena *const allocator);

/**
 * @brief Create a String from an unsigned integer with padding using an arena allocator.
 * @param number Unsigned integer value.
 * @param padding Number of padding characters.
 * @param allocator Arena allocator to use.
 * @return String by value; OWNED, release with string_uninit.
 */
String string_alloc_from_numbers_uint_2(USize const number, U8 const padding, Arena *const allocator);

/**
 * @brief Create an arena-backed copy of a String with find replaced by replace.
 * @param self Source String.
 * @param find C string to replace.
 * @param replace Replacement C string.
 * @param allocator Arena allocator to use.
 * @return String by value; OWNED, release with string_uninit. The EMPTY String on a refused arena.
 */
String string_alloc_from_replace_1(String const *const self, char const *const find, char const *const replace, Arena *const allocator);

/**
 * @brief Create an arena-backed copy of a String with find (of given size) replaced by replace.
 * @param self Source String.
 * @param find Data to replace.
 * @param find_size Size of find.
 * @param replace Replacement data.
 * @param replace_size Size of replace.
 * @param allocator Arena allocator to use.
 * @return String by value; OWNED, release with string_uninit. The EMPTY String on a refused arena.
 * @note A find_size of 0 matches nothing, so the result is a verbatim copy of self.
 */
String string_alloc_from_replace_2(String const *const self, char const *const find, USize const find_size, char const *const replace, USize const replace_size, Arena *const allocator);

/**
 * @brief Create an arena-backed trimmed copy of a String.
 * @param self Source String.
 * @param allocator Arena allocator to use.
 * @return Trimmed String copy; OWNED, release with string_uninit (an all-whitespace source
 *         yields an owned zero-length String). The EMPTY String on a refused arena.
 */
String string_alloc_from_trim(String const *const self, Arena *const allocator);

/**
 * @brief Initialize an empty String using an arena allocator.
 * @param allocator Arena allocator to use.
 * @return String by value.
 */
String string_alloc_init_1(Arena *const allocator);

/**
 * @brief Initialize a String with a given capacity using an arena allocator.
 * @param capacity Buffer capacity. Must be non-zero; a zero capacity is a programming error
 *        and aborts.
 * @param allocator Arena allocator to use.
 * @return String by value; on a REFUSED arena the coherent EMPTY String (capacity 0, data
 *         nullptr, owned false, `allocator` still set to the caller's arena) - the whole
 *         string_alloc_* family degrades this way.
 */
String string_alloc_init_2(USize const capacity, Arena *const allocator);

/**
 * @brief Initialize a String from data using an arena allocator.
 * @param data Pointer to data.
 * @param allocator Arena allocator to use.
 * @return String by value.
 */
String string_alloc_init_3(char *const data, Arena *const allocator);

/**
 * @brief Initialize a String from data and size using an arena allocator.
 * @param data Pointer to data.
 * @param data_size Size of data. USIZE_MAX (the claimed capacity would wrap) is REFUSED to
 *        the EMPTY String.
 * @param allocator Arena allocator to use.
 * @return String by value.
 */
String string_alloc_init_4(char *const data, USize const data_size, Arena *const allocator);

/**
 * @brief Initialize an OWNED copy of a Str using an arena allocator.
 * @param data Pointer to Str.
 * @param allocator Arena allocator to use.
 * @return String by value; release with string_uninit. The EMPTY String on a refused arena.
 */
String string_alloc_init_5(Str const *const data, Arena *const allocator);

/**
 * @brief Initialize an OWNED copy of another String using an arena allocator.
 * @param data Pointer to String.
 * @param allocator Arena allocator to use.
 * @return String by value; release with string_uninit. The EMPTY String on a refused arena.
 */
String string_alloc_init_6(String const *const data, Arena *const allocator);

/**
 * @brief Initialize an OWNED copy of const bytes using an arena allocator (the name is
 *        historical: nothing about the result is static).
 * @param data Pointer to data.
 * @param data_size Size of data.
 * @param allocator Arena allocator to use.
 * @return String by value; release with string_uninit. The EMPTY String on a refused arena.
 */
String string_alloc_init_static(char const *const data, USize const data_size, Arena *const allocator);

/**
 * @brief Join an array of String with a C string separator using an arena allocator.
 * @param parts Array of String pointers.
 * @param count Number of String in parts.
 * @param separator Separator placed between parts.
 * @param allocator Arena allocator to use.
 * @return String by value; OWNED, release with string_uninit. The EMPTY String on a refused
 *         arena (the whole result is reserved once up front, so a refusal is whole, never a
 *         truncated join) or when the summed size would overflow USize.
 */
String string_alloc_join_1(String const *const *const parts, USize const count, char const *const separator, Arena *const allocator);

/**
 * @brief Join an array of String with a separator of a specified size using an arena allocator.
 * @param parts Array of String pointers.
 * @param count Number of String in parts.
 * @param separator Separator placed between parts.
 * @param separator_size Size of the separator.
 * @param allocator Arena allocator to use.
 * @return String by value; OWNED, release with string_uninit. The EMPTY String on a refused
 *         arena (the whole result is reserved once up front, so a refusal is whole, never a
 *         truncated join) or when the summed size would overflow USize.
 */
String string_alloc_join_2(String const *const *const parts, USize const count, char const *const separator, USize const separator_size, Arena *const allocator);

/**
 * @brief Allocate a new String on the heap using an arena allocator.
 * @param allocator Arena allocator to use.
 * @return Pointer to new String, or nullptr when the arena was refused (a rejected
 *         arena_init_2 leaves a null handler). Caller must free with
 *         string_delete(). The whole string_alloc_new_* family propagates this.
 */
String* string_alloc_new_1(Arena *const allocator);

/**
 * @brief Allocate a new String on the heap with a given capacity using an arena allocator.
 *        A zero capacity is a programming error and aborts, as string_alloc_init_2.
 * @param capacity Buffer capacity.
 * @param allocator Arena allocator to use.
 * @return Pointer to new String. Caller must free with string_delete().
 */
String* string_alloc_new_2(USize const capacity, Arena *const allocator);

/**
 * @brief Allocate a new String on the heap from data using an arena allocator.
 * @param data Pointer to data.
 * @param allocator Arena allocator to use.
 * @return Pointer to new String. Caller must free with string_delete().
 */
String* string_alloc_new_3(char *const data, Arena *const allocator);

/**
 * @brief Allocate a new String on the heap from data and size using an arena allocator.
 * @param data Pointer to data.
 * @param data_size Size of data.
 * @param allocator Arena allocator to use.
 * @return Pointer to new String. Caller must free with string_delete().
 */
String* string_alloc_new_4(char *const data, USize const data_size, Arena *const allocator);

/**
 * @brief Allocate a new String on the heap from a Str using an arena allocator.
 * @param data Pointer to Str.
 * @param allocator Arena allocator to use.
 * @return Pointer to new String. Caller must free with string_delete().
 */
String* string_alloc_new_5(Str const *const data, Arena *const allocator);

/**
 * @brief Allocate a new String on the heap from another String using an arena allocator.
 * @param data Pointer to String.
 * @param allocator Arena allocator to use.
 * @return Pointer to new String. Caller must free with string_delete().
 */
String* string_alloc_new_6(String const *const data, Arena *const allocator);

/**
 * @brief Allocate a new static String from data and size using an arena allocator.
 * @param data Pointer to data.
 * @param data_size Size of data.
 * @param allocator Arena allocator to use.
 * @return Pointer to new String. Caller must free with string_delete().
 */
String* string_alloc_new_static(char const *const data, USize const data_size, Arena *const allocator);

/**
 * @brief Trim leading and trailing whitespace using an arena allocator, REPLACING self's
 *        buffer (see string_trim: not in place, promotes a VIEW, all-whitespace yields an
 *        owned zero-length String). A REFUSED arena is a no-op leaving self intact. On
 *        success self ADOPTS `allocator`: the new buffer is that arena's, and every later
 *        growth and the final release go through it, whatever self carried before.
 * @param self Pointer to the String object.
 * @param allocator Arena allocator to use.
 */
void string_alloc_trim(String *const self, Arena *const allocator);
#endif // ARENA_IMPLEMENTATION

/*==============================================================================
 * MARK: - Standard API
 *============================================================================*/
/**
 * @brief Add data to a String at a specified index.
 * @param self Pointer to the String object.
 * @param data Pointer to the data to add.
 * @param index Index at which to add the data.
 */
void string_add_1(String *const self, char const *const data, USize const index);

/**
 * @brief Add data to a String with a specified size at a specified index.
 * @param self Pointer to the String object.
 * @param data Pointer to the data to add.
 * @param data_size Size of the data to add.
 * @param index Index at which to add the data.
 * @note REFUSES as a no-op, in every build: an index past size (index == size appends), a
 *       size that would overflow, and a growth the arena refused. A data pointer inside
 *       self's own buffer is detected and re-based across the growth and the shift, so a
 *       self-insert at any index is exact - provided the source lies wholly inside [0, size):
 *       one that starts in the buffer but runs past size, or sits in the spare capacity, is
 *       REFUSED as a no-op.
 */
void string_add_2(String *const self, char const *const data, USize const data_size, USize const index);

/**
 * @brief Add a Str to a String at a specified index.
 * @param self Pointer to the String object.
 * @param data Pointer to the Str to add.
 * @param index Index at which to add the Str.
 */
void string_add_3(String *const self, Str const *const data, USize const index);

/**
 * @brief Add another String to a String at a specified index.
 * @param self Pointer to the String object.
 * @param data Pointer to the String to add.
 * @param index Index at which to add the String.
 */
void string_add_4(String *const self, String const *const data, USize const index);

/**
 * @brief Add data to the beginning of a String.
 * @param self Pointer to the String object.
 * @param data Pointer to the data to add.
 */
void string_add_first_1(String *const self, char const *const data);

/**
 * @brief Add data to the beginning of a String with a specified size.
 * @param self Pointer to the String object.
 * @param data Pointer to the data to add.
 * @param data_size Size of the data to add.
 */
void string_add_first_2(String *const self, char const *const data, USize const data_size);

/**
 * @brief Add a Str to the beginning of a String.
 * @param self Pointer to the String object.
 * @param data Pointer to the Str to add.
 */
void string_add_first_3(String *const self, Str const *const data);

/**
 * @brief Add another String to the beginning of a String.
 * @param self Pointer to the String object.
 * @param data Pointer to the String to add.
 */
void string_add_first_4(String *const self, String const *const data);

/**
 * @brief Add data to the end of a String.
 * @param self Pointer to the String object.
 * @param data Pointer to the data to add.
 */
void string_add_last_1(String *const self, char const *const data);

/**
 * @brief Add data to the end of a String with a specified size.
 * @param self Pointer to the String object.
 * @param data Pointer to the data to add.
 * @param data_size Size of the data to add.
 */
void string_add_last_2(String *const self, char const *const data, USize const data_size);

/**
 * @brief Add a Str to the end of a String.
 * @param self Pointer to the String object.
 * @param data Pointer to the Str to add.
 */
void string_add_last_3(String *const self, Str const *const data);

/**
 * @brief Add another String to the end of a String.
 * @param self Pointer to the String object.
 * @param data Pointer to the String to add.
 */
void string_add_last_4(String *const self, String const *const data);

/**
 * @brief Get a character from a String at a specified index.
 * @param self Pointer to the String object.
 * @param index Index of the character to get. An index at or past the end is legal
 *              and answers '\0' (char_at/str_at parity).
 * @return Character at the specified index, or '\0' when index is at or past the
 *         end (the empty String included) - answered without reading that byte, so a
 *         view over an unterminated buffer is safe.
 */
char string_at(String const *const self, USize const index);

/**
 * @brief Clear the contents of a String: size becomes 0 and the whole old payload is SCRUBBED
 *        (zero-filled, O(old size)) - deliberate, so a cleared secret leaves no bytes behind.
 * @param self Pointer to the String object.
 */
void string_clear(String *const self);

/**
 * @brief Compare a String to a C string for equality.
 * @param self Pointer to the String object.
 * @param data Pointer to the C string to compare.
 * @return true if equal, false otherwise.
 */
bool string_compare_equal_1(String const *const self, char const *const data);

/**
 * @brief Compare a String to a C string with a specified size for equality.
 * @param self Pointer to the String object.
 * @param data Pointer to the C string to compare.
 * @param data_size Size of the data to compare.
 * @return true if equal, false otherwise.
 */
bool string_compare_equal_2(String const *const self, char const *const data, USize const data_size);

/**
 * @brief Compare a String to a Str for equality.
 * @param self Pointer to the String object.
 * @param data Pointer to the Str to compare.
 * @return true if equal, false otherwise.
 */
bool string_compare_equal_3(String const *const self, Str const *const data);

/**
 * @brief Compare a String to another String for equality.
 * @param self Pointer to the String object.
 * @param data Pointer to the String to compare.
 * @return true if equal, false otherwise.
 */
bool string_compare_equal_4(String const *const self, String const *const data);

/**
 * @brief Timing-safe (constant-time) equality of a String and a C string. Use it
 *        to compare secrets; folds every byte with no early exit.
 * @param self Pointer to the String object.
 * @param data Pointer to the C string to compare.
 * @return true if equal, false otherwise.
 */
bool string_compare_equal_comptime_1(String const *const self, char const *const data);

/**
 * @brief Timing-safe (constant-time) equality of a String and a sized C string.
 * @param self Pointer to the String object.
 * @param data Pointer to the C string to compare.
 * @param data_size Size of the data to compare.
 * @return true if equal, false otherwise.
 */
bool string_compare_equal_comptime_2(String const *const self, char const *const data, USize const data_size);

/**
 * @brief Timing-safe (constant-time) equality of a String and a Str.
 * @param self Pointer to the String object.
 * @param data Pointer to the Str to compare.
 * @return true if equal, false otherwise.
 */
bool string_compare_equal_comptime_3(String const *const self, Str const *const data);

/**
 * @brief Timing-safe (constant-time) equality of two String.
 * @param self Pointer to the String object.
 * @param data Pointer to the String to compare.
 * @return true if equal, false otherwise.
 */
bool string_compare_equal_comptime_4(String const *const self, String const *const data);

/**
 * @brief Compare a String to a C string for case-insensitive equality.
 * @param self Pointer to the String object.
 * @param data Pointer to the C string to compare.
 * @return true if equal ignoring ASCII case, false otherwise.
 */
bool string_compare_iequal_1(String const *const self, char const *const data);

/**
 * @brief Compare a String to a C string (with size) for case-insensitive equality.
 * @param self Pointer to the String object.
 * @param data Pointer to the C string to compare.
 * @param data_size Size of the data to compare.
 * @return true if equal ignoring ASCII case, false otherwise.
 */
bool string_compare_iequal_2(String const *const self, char const *const data, USize const data_size);

/**
 * @brief Compare a String to a Str for case-insensitive equality.
 * @param self Pointer to the String object.
 * @param data Pointer to the Str to compare.
 * @return true if equal ignoring ASCII case, false otherwise.
 */
bool string_compare_iequal_3(String const *const self, Str const *const data);

/**
 * @brief Compare a String to another String for case-insensitive equality.
 * @param self Pointer to the String object.
 * @param data Pointer to the String to compare.
 * @return true if equal ignoring ASCII case, false otherwise.
 */
bool string_compare_iequal_4(String const *const self, String const *const data);

/**
 * @brief Timing-safe case-insensitive equality of a String and a C string. Use it
 *        to compare secrets; folds every byte with no early exit.
 * @param self Pointer to the String object.
 * @param data Pointer to the C string to compare.
 * @return true if equal ignoring ASCII case, false otherwise.
 */
bool string_compare_iequal_comptime_1(String const *const self, char const *const data);

/**
 * @brief Timing-safe case-insensitive equality of a String and a sized C string.
 * @param self Pointer to the String object.
 * @param data Pointer to the C string to compare.
 * @param data_size Size of the data to compare.
 * @return true if equal ignoring ASCII case, false otherwise.
 */
bool string_compare_iequal_comptime_2(String const *const self, char const *const data, USize const data_size);

/**
 * @brief Timing-safe case-insensitive equality of a String and a Str.
 * @param self Pointer to the String object.
 * @param data Pointer to the Str to compare.
 * @return true if equal ignoring ASCII case, false otherwise.
 */
bool string_compare_iequal_comptime_3(String const *const self, Str const *const data);

/**
 * @brief Timing-safe case-insensitive equality of two String.
 * @param self Pointer to the String object.
 * @param data Pointer to the String to compare.
 * @return true if equal ignoring ASCII case, false otherwise.
 */
bool string_compare_iequal_comptime_4(String const *const self, String const *const data);

/**
 * @brief Check whether a C string occurs anywhere in a String (string_find_exists_1 answers the
 *        same question - the pair is kept for the two char twins it mirrors).
 * @param self Pointer to the String object.
 * @param data Pointer to the C string to search for.
 * @return true if data occurs in self, false otherwise.
 */
bool string_contains_1(String const *const self, char const *const data);

/**
 * @brief Check whether data of a specified size occurs anywhere in a String.
 * @param self Pointer to the String object.
 * @param data Pointer to the data to search for.
 * @param data_size Size of the data.
 * @return true if data occurs in self, false otherwise.
 * @note A data_size of 0 occurs inside every String, the empty String included: returns true.
 */
bool string_contains_2(String const *const self, char const *const data, USize const data_size);

/**
 * @brief Check whether a Str occurs anywhere in a String.
 * @param self Pointer to the String object.
 * @param data Pointer to the Str to search for.
 * @return true if data occurs in self, false otherwise.
 */
bool string_contains_3(String const *const self, Str const *const data);

/**
 * @brief Check whether another String occurs anywhere in a String.
 * @param self Pointer to the String object.
 * @param data Pointer to the String to search for.
 * @return true if data occurs in self, false otherwise.
 */
bool string_contains_4(String const *const self, String const *const data);

/**
 * @brief Copy the contents of one String to another.
 * @param self Pointer to the destination String object.
 * @param data Pointer to the source String to copy from.
 */
void string_copy(String *const self, String const *const data);

/**
 * @brief Delete a String, freeing its resources.
 * @param self Pointer to the String object to delete.
 */
void string_delete(String **const self);

/**
 * @brief Check if a String is empty (size 0) - true for the EMPTY state and for an owned
 *        zero-length buffer alike; test `data == nullptr` to tell the state apart.
 * @param self Pointer to the String object.
 * @return true if empty, false otherwise.
 */
bool string_empty(String const *const self);

/**
 * @brief Check whether a String ends with a C string suffix.
 * @param self Pointer to the String object.
 * @param data Pointer to the suffix C string.
 * @return true if self ends with data, false otherwise.
 */
bool string_ends_with_1(String const *const self, char const *const data);

/**
 * @brief Check whether a String ends with a suffix of a specified size.
 * @param self Pointer to the String object.
 * @param data Pointer to the suffix.
 * @param data_size Size of the suffix.
 * @return true if self ends with data, false otherwise.
 * @note A data_size of 0 matches every String, the empty String included: returns true.
 */
bool string_ends_with_2(String const *const self, char const *const data, USize const data_size);

/**
 * @brief Check whether a String ends with a Str suffix.
 * @param self Pointer to the String object.
 * @param data Pointer to the suffix Str.
 * @return true if self ends with data, false otherwise.
 */
bool string_ends_with_3(String const *const self, Str const *const data);

/**
 * @brief Check whether a String ends with another String suffix.
 * @param self Pointer to the String object.
 * @param data Pointer to the suffix String.
 * @return true if self ends with data, false otherwise.
 */
bool string_ends_with_4(String const *const self, String const *const data);

/**
 * @brief Erase a range of characters in a String.
 *        Removes the half-open range [from, to): `to` is EXCLUSIVE, matching
 *        str_erase and the one-past-end convention regex match ends use. (The
 *        slice_range functions are INCLUSIVE - the two families differ deliberately
 *        and each says so.)
 * @param self Pointer to the String object.
 * @param from Start index of the range to erase (inclusive).
 * @param to End index (exclusive); to == size erases through the end.
 * @note REFUSES rather than aborting on an out-of-range span (from > to, or
 *       to > size): the indices are regex-match-shaped values. from == to is the
 *       legal empty erase.
 */
void string_erase(String *const self, USize const from, USize const to);

/**
 * @brief Fill a String with a character.
 * @param self Pointer to the String object.
 * @param c Character to fill with.
 */
void string_fill(String *const self, char const c);

/**
 * @brief Find the index of the first occurrence of data in a String.
 * @param self Pointer to the String object.
 * @param self_index Starting index for the search.
 * @param data Pointer to the data to find.
 * @return Index of the first occurrence, or CHAR_NPOS (== SIZE_MAX) if not found.
 */
USize string_find_1(String const *const self, USize const self_index, char const *const data);

/**
 * @brief Find the index of the first occurrence of data with a specified size in a String.
 * @param self Pointer to the String object.
 * @param self_index Starting index for the search.
 * @param data Pointer to the data to find.
 * @param data_size Size of the data to find.
 * @return Index of the first occurrence, or CHAR_NPOS (== SIZE_MAX) if not found.
 * @note A data_size of 0 is found at self_index (the search origin); a non-empty
 *       data searched in an empty self returns CHAR_NPOS.
 */
USize string_find_2(String const *const self, USize const self_index, char const *const data, USize const data_size);

/**
 * @brief Find the index of the first occurrence of a Str in a String.
 * @param self Pointer to the String object.
 * @param self_index Starting index for the search.
 * @param data Pointer to the Str to find.
 * @return Index of the first occurrence, or CHAR_NPOS (== SIZE_MAX) if not found.
 */
USize string_find_3(String const *const self, USize const self_index, Str const *const data);

/**
 * @brief Find the index of the first occurrence of another String in a String.
 * @param self Pointer to the String object.
 * @param self_index Starting index for the search.
 * @param data Pointer to the String to find.
 * @return Index of the first occurrence, or CHAR_NPOS (== SIZE_MAX) if not found.
 */
USize string_find_4(String const *const self, USize const self_index, String const *const data);

/**
 * @brief Find the first index in a String matching any character of a set.
 * @param self Pointer to the String object.
 * @param set Pointer to the set of characters to match.
 * @return Index of the first matching character, or CHAR_NPOS (== SIZE_MAX) if none match.
 */
USize string_find_any_1(String const *const self, char const *const set);

/**
 * @brief Find the first index in a String matching any character of a set of a specified size.
 * @param self Pointer to the String object.
 * @param set Pointer to the set of characters to match.
 * @param set_size Number of characters in the set.
 * @return Index of the first matching character, or CHAR_NPOS (== SIZE_MAX) if none match.
 * @note A set_size of 0 holds no candidate character, so nothing matches: returns CHAR_NPOS.
 */
USize string_find_any_2(String const *const self, char const *const set, USize const set_size);

/**
 * @brief Count occurrences of data in a String.
 * @param self Pointer to the String object.
 * @param data Pointer to the data to count.
 * @return Number of occurrences.
 */
USize string_find_count_1(String const *const self, char const *const data);

/**
 * @brief Count occurrences of data with a specified size in a String.
 * @param self Pointer to the String object.
 * @param data Pointer to the data to count.
 * @param data_size Size of the data to count.
 * @return Number of occurrences.
 * @note A data_size of 0 counts as 0 occurrences, deliberately not one match per gap as in
 *       Python: the replace family sizes its output buffer from this count, so a positive
 *       answer would splice a replacement into every gap.
 */
USize string_find_count_2(String const *const self, char const *const data, USize const data_size);

/**
 * @brief Check whether a C string exists in a String (the same answer as string_contains_1).
 * @param self Pointer to the String object.
 * @param data Pointer to the data to check for.
 * @return true if data exists in self, false otherwise.
 */
bool string_find_exists_1(String const *const self, char const *const data);

/**
 * @brief Check whether data of a specified size exists in a String.
 * @param self Pointer to the String object.
 * @param data Pointer to the data to check for.
 * @param data_size Size of the data.
 * @return true if data exists in self, false otherwise.
 * @note A data_size of 0 exists inside every String, the empty String included: returns true.
 */
bool string_find_exists_2(String const *const self, char const *const data, USize const data_size);

/**
 * @brief Check whether a Str exists in a String.
 * @param self Pointer to the String object.
 * @param data Pointer to the Str to check for.
 * @return true if data exists in self, false otherwise.
 */
bool string_find_exists_3(String const *const self, Str const *const data);

/**
 * @brief Check whether another String exists in a String.
 * @param self Pointer to the String object.
 * @param data Pointer to the String to check for.
 * @return true if data exists in self, false otherwise.
 */
bool string_find_exists_4(String const *const self, String const *const data);

/**
 * @brief Find the index of the last occurrence of data in a String.
 * @param self Pointer to the String object.
 * @param self_index Starting index for the search (from the end).
 * @param data Pointer to the data to find.
 * @return Index of the last occurrence, or CHAR_NPOS (== SIZE_MAX) if not found.
 */
USize string_find_reverse_1(String const *const self, USize const self_index, char const *const data);

/**
 * @brief Find the index of the last occurrence of data with a specified size in a String.
 * @param self Pointer to the String object.
 * @param self_index Starting index for the search (from the end).
 * @param data Pointer to the data to find.
 * @param data_size Size of the data to find.
 * @return Index of the last occurrence, or CHAR_NPOS (== SIZE_MAX) if not found.
 * @note Mirrors string_find_2: a data_size of 0 is found at self->size (the far end),
 *       not at self_index.
 */
USize string_find_reverse_2(String const *const self, USize const self_index, char const *const data, USize const data_size);

/**
 * @brief Format data into an existing String.
 * @param self String instance.
 * @param format Format string.
 * @note GROWS on overflow: when the rendered length reaches capacity the String is
 *       reserved up to the full length and re-rendered - the rendered length is
 *       data (it carries a User-Agent header in crawler/fetch), so it must never abort. A VIEW
 *       grown here becomes an OWNER (string_reserve's documented behavior). On a
 *       refused arena the growth is impossible; the truncated render (capacity - 1
 *       bytes, terminated) is kept.
 * @note ON A VIEW THE PROMOTION DOES NOT UNDO THE FIRST RENDER. vsnprintf is handed
 *       self->capacity, so a render reaching the view's size has already terminated at
 *       index data_size - through the BORROWED pointer - before the grow path runs. See
 *       the capacity rule on `owned` in this header: that byte is a precondition on the
 *       buffer you viewed, and the rendered length is data.
 * @note A render of ZERO bytes yields the empty String (size 0, terminated); only a negative
 *       vsnprintf result (an encoding error) leaves self untouched.
 * @note NO FORMAT ARGUMENT MAY POINT INTO self->data: vsnprintf would read a source inside the
 *       buffer it is writing, and the grow-and-retry re-renders from the same, now clobbered,
 *       pointer. Format into a fresh String and string_copy or string_move it; the varargs are
 *       opaque, so this cannot be detected at runtime.
 */
void string_format(String *const self, char const *const format, ...);

/**
 * @brief Create a String from a floating-point number.
 * @param number Floating-point value.
 * @return String by value; OWNED, release with string_uninit.
 */
String string_from_numbers_float_1(FSize const number);

/**
 * @brief Create a String from a floating-point number with a given precision.
 * @param number Floating-point value.
 * @param precision Number of digits after the decimal point.
 * @return String by value; OWNED, release with string_uninit.
 */
String string_from_numbers_float_2(FSize const number, U8 const precision);

/**
 * @brief Create a String from an integer.
 * @param number Integer value.
 * @return String by value; OWNED, release with string_uninit.
 */
String string_from_numbers_int_1(ISize const number);

/**
 * @brief Create a String from an integer with padding.
 * @param number Integer value.
 * @param padding Number of padding characters.
 * @return String by value; OWNED, release with string_uninit.
 */
String string_from_numbers_int_2(ISize const number, U8 const padding);

/**
 * @brief Create a String from an unsigned integer.
 * @param number Unsigned integer value.
 * @return String by value; OWNED, release with string_uninit.
 */
String string_from_numbers_uint_1(USize const number);

/**
 * @brief Create a String from an unsigned integer with padding.
 * @param number Unsigned integer value.
 * @param padding Number of padding characters.
 * @return String by value; OWNED, release with string_uninit.
 */
String string_from_numbers_uint_2(USize const number, U8 const padding);

/**
 * @brief Create a copy of a String with every occurrence of find replaced by replace.
 * @param self Source String.
 * @param find C string to replace.
 * @param replace Replacement C string.
 * @return String by value; OWNED, release with string_uninit (never string_delete - that frees
 *         the struct, and this one lives on your stack).
 */
String string_from_replace_1(String const *const self, char const *const find, char const *const replace);

/**
 * @brief Create a copy of a String with every occurrence of find (of given size) replaced by replace.
 * @param self Source String.
 * @param find Data to replace.
 * @param find_size Size of find.
 * @param replace Replacement data.
 * @param replace_size Size of replace.
 * @return String by value; OWNED, release with string_uninit (never string_delete - that frees
 *         the struct, and this one lives on your stack).
 * @note A find_size of 0 matches nothing, so the result is a verbatim copy of self.
 */
String string_from_replace_2(String const *const self, char const *const find, USize const find_size, char const *const replace, USize const replace_size);

/**
 * @brief Create a trimmed copy of a String.
 * @param self Source String.
 * @return Trimmed String copy, OWNED; release with string_uninit. An all-whitespace source
 *         yields an owned zero-length String; an EMPTY source yields the EMPTY String.
 */
String string_from_trim(String const *const self);

/**
 * @brief Get the capacity of a String.
 * @param self Pointer to the String object.
 * @return The capacity, by value.
 */
USize string_get_capacity(String const *const self);

/**
 * @brief Get a pointer to the data of a String.
 * @param self Pointer to the String object.
 * @return Pointer to the data - nullptr for the EMPTY String (size 0 with no buffer), so
 *         hand it to %s only after the size check. Bytes written by this module's own writers
 *         are terminated; a buffer filled directly through string_get_data + string_set_size
 *         (or string_replace_2, which does not re-terminate) is terminated only if that writer
 *         did it, and a VIEW's only if the viewed buffer was. The
 *         pointer is writable even from a const String - the one const escape in this API -
 *         and it dangles after any call that reallocates (reserve, a growing add or format,
 *         copy's grow branch, trim, shrink at size 0).
 */
char* string_get_data(String const *const self);

/**
 * @brief Get the size of a String.
 * @param self Pointer to the String object.
 * @return The size, by value.
 */
USize string_get_size(String const *const self);

/**
 * @brief Initialize an empty String.
 * @return String by value.
 */
String string_init_1(void);

/**
 * @brief Initialize a String with a given capacity.
 * @param capacity Buffer capacity. Must be non-zero; a zero capacity is a
 *        programming error and aborts.
 * @return String by value.
 */
String string_init_2(USize const capacity);

/**
 * @brief Initialize a String from data.
 * @param data Pointer to data.
 * @return String by value.
 */
String string_init_3(char *const data);

/**
 * @brief Initialize a String from data and size.
 * @param data Pointer to data.
 * @param data_size Size of data. USIZE_MAX (the claimed capacity would wrap) is REFUSED to
 *        the EMPTY String.
 * @return String by value.
 */
String string_init_4(char *const data, USize const data_size);

/**
 * @brief Initialize an OWNED copy of a Str.
 * @param data Pointer to Str.
 * @return String by value; release with string_uninit.
 */
String string_init_5(Str const *const data);

/**
 * @brief Initialize an OWNED copy of another String.
 * @param data Pointer to String.
 * @return String by value; release with string_uninit.
 */
String string_init_6(String const *const data);

/**
 * @brief Initialize an OWNED copy of const bytes (the name is historical: nothing about the
 *        result is static).
 * @param data Pointer to data.
 * @param data_size Size of data.
 * @return String by value; release with string_uninit.
 */
String string_init_static(char const *const data, USize const data_size);

/**
 * @brief Join an array of String with a C string separator.
 * @param parts Array of String pointers.
 * @param count Number of String in parts.
 * @param separator Separator placed between parts.
 * @return String by value; OWNED, release with string_uninit (never string_delete - that frees
 *         the struct, and this one lives on your stack).
 */
String string_join_1(String const *const *const parts, USize const count, char const *const separator);

/**
 * @brief Join an array of String with a separator of a specified size.
 * @param parts Array of String pointers.
 * @param count Number of String in parts.
 * @param separator Separator placed between parts.
 * @param separator_size Size of the separator.
 * @return String by value; OWNED, release with string_uninit (never string_delete - that frees
 *         the struct, and this one lives on your stack).
 */
String string_join_2(String const *const *const parts, USize const count, char const *const separator, USize const separator_size);

/**
 * @brief Convert a String to lowercase.
 * @param self Pointer to the String object.
 */
void string_lower(String *const self);

/**
 * @brief Move data from a source pointer to a String.
 * @param self Pointer to the String object.
 * @param data Pointer to the source data pointer.
 * @note Forwards to string_move_2: the buffer must come from the SAME allocator this object
 *       was built with (see that function's note), and the source pointer is cleared.
 */
void string_move_1(String *const self, char **const data);

/**
 * @brief Move data from a source pointer to a String with a specified size.
 * @param self Pointer to the String object.
 * @param data Pointer to the source data pointer.
 * @param data_size Size of the data to move. The buffer must hold data_size +
 *        CHAR_END_CHARACTER bytes: the String claims that capacity and writes the terminator
 *        slot. A data_size of USIZE_MAX (the capacity would wrap) is REFUSED as a no-op.
 * @note The buffer must come from the SAME allocator this object was built with - a
 *       bare pointer carries no provenance, so the release at uninit goes through
 *       self->allocator whatever the buffer's real origin was. Moving a heap buffer
 *       into an arena-backed object (or the reverse) is a wrong-allocator release.
 *       The object-taking str_move_3 / string_move_3 / string_move_4 forms do NOT
 *       solve this: `allocator` also records where the STRUCT was borrowed from, so
 *       they cannot adopt the source's without breaking *_delete. They refuse to
 *       cross allocators instead - both objects must share one.
 */
void string_move_2(String *const self, char **const data, USize const data_size);

/**
 * @brief Move a Str from a source pointer to a String.
 *        The source's ownership flag is preserved across the move (a view stays a
 *        view), and the source object is emptied.
 * @param self Pointer to the String object.
 * @param data Pointer to the source Str pointer. Set to nullptr after the move.
 * @note A cross-allocator move is REFUSED as a no-op leaving both objects
 *       untouched. Only the source's FIELDS are harvested - the source STRUCT is
 *       never released; keep a second pointer to a heap-allocated source
 *       (str_new_*) or move from stack/value objects.
 */
void string_move_3(String *const self, Str **const data);

/**
 * @brief Move another String from a source pointer to a String.
 *        Ownership transfers with the buffer: moving a view yields a view.
 * @param self Pointer to the String object.
 * @param data Pointer to the source String pointer. Set to nullptr after the move;
 *             the source object is cleared so a retained alias cannot double-free.
 * @note A cross-allocator move is REFUSED as a no-op leaving both objects
 *       untouched - checked before the destination is released, so a refusal
 *       cannot destroy it. The source STRUCT is never released; keep a second
 *       pointer to a heap-allocated source (string_new_*) or move from stack
 *       objects.
 */
void string_move_4(String *const self, String **const data);

/**
 * @brief Allocate a new String on the heap.
 * @return Pointer to new String. Caller must free with string_delete().
 */
String* string_new_1(void);

/**
 * @brief Allocate a new String on the heap with a given capacity.
 * @param capacity Buffer capacity. Must be non-zero; a zero capacity is a
 *        programming error and aborts.
 * @return Pointer to new String. Caller must free with string_delete().
 */
String* string_new_2(USize const capacity);

/**
 * @brief Allocate a new String on the heap from data.
 * @param data Pointer to data.
 * @return Pointer to new String. Caller must free with string_delete().
 */
String* string_new_3(char *const data);

/**
 * @brief Allocate a new String on the heap from data and size.
 * @param data Pointer to data.
 * @param data_size Size of data.
 * @return Pointer to new String. Caller must free with string_delete().
 */
String* string_new_4(char *const data, USize const data_size);

/**
 * @brief Allocate a new String on the heap from a Str.
 * @param data Pointer to Str.
 * @return Pointer to new String. Caller must free with string_delete().
 */
String* string_new_5(Str const *const data);

/**
 * @brief Allocate a new String on the heap from another String.
 * @param data Pointer to String.
 * @return Pointer to new String. Caller must free with string_delete().
 */
String* string_new_6(String const *const data);

/**
 * @brief Allocate a new static String from data and size.
 * @param data Pointer to data.
 * @param data_size Size of data.
 * @return Pointer to new String. Caller must free with string_delete().
 */
String* string_new_static(char const *const data, USize const data_size);

/**
 * @brief Print a String's label, capacity, bytes and size to the log or to stdout.
 * @param self Pointer to the String object.
 * @param label Name printed before the fields - a debugging aid, not an output destination.
 * @param log true routes through log_message at LOG_LEVEL_INFO; false prints to stdout.
 * @note The EMPTY String prints no bytes (its null data is never handed to %.*s).
 */
void string_print(String const *const self, char const *const label, bool const log);

/**
 * @brief Remove a character from a String at a specified index (a single-byte string_erase).
 * @param self Pointer to the String object.
 * @param index Index of the character to remove.
 * @note REFUSES an index at or past size as a no-op in every build (the empty String
 *       included) - the index is data-shaped, as string_erase's note explains.
 */
void string_remove(String *const self, USize const index);

/**
 * @brief Repeat a String's contents a number of times. Modifies self in place.
 * @param self Pointer to the String object.
 * @param count Number of repetitions. A count of 0 empties the String but KEEPS its
 *              allocation (an owned buffer at size 0) - unlike str_repeat, which
 *              releases. Both are empty; they differ in what they hold.
 * @note REFUSES wholly, as a no-op, when the arena refuses the growth the repetition needs:
 *       on a VIEW the copy loop would otherwise write the caller's adjacent memory.
 */
void string_repeat(String *const self, USize const count);

/**
 * @brief Replace data in a String at a specified index.
 * @param self Pointer to the String object.
 * @param data Pointer to the data to replace with.
 * @param index Index at which to replace the data.
 */
void string_replace_1(String *const self, char const *const data, USize const index);

/**
 * @brief Replace data in a String with a specified size at a specified index.
 * @param self Pointer to the String object.
 * @param data Pointer to the data to replace with.
 * @param data_size Size of the data to replace with.
 * @param index Index at which to replace the data.
 * @note CAPACITY-scoped, unlike str_replace_2 (which is content-scoped): this form
 *       may write into [size, capacity) - it is string_add_2's shift-then-write
 *       helper - and it does NOT move size or re-terminate. A direct caller writing
 *       past size must re-terminate and set size itself, or it leaves a gap of
 *       stale bytes the String still reports as content.
 * @note REFUSES as a silent no-op when index or index + data_size exceeds capacity: the
 *       bound is capacity, and nothing past it is ever written.
 */
void string_replace_2(String *const self, char const *const data, USize const data_size, USize const index);

/**
 * @brief Replace a Str in a String at a specified index.
 * @param self Pointer to the String object.
 * @param data Pointer to the Str to replace with.
 * @param index Index at which to replace the Str.
 * @note An empty data replaces nothing, leaving self unchanged.
 */
void string_replace_3(String *const self, Str const *const data, USize const index);

/**
 * @brief Replace another String in a String at a specified index.
 * @param self Pointer to the String object.
 * @param data Pointer to the String to replace with.
 * @param index Index at which to replace the String.
 */
void string_replace_4(String *const self, String const *const data, USize const index);

/**
 * @brief Reserve space for a specified capacity in a String.
 * @param self Pointer to the String object.
 * @param capacity Desired buffer capacity. Must be non-zero; a zero capacity is
 *        a programming error and aborts.
 * @note Never shrinks. A refused arena is a no-op leaving self intact. Growing a VIEW copies
 *       its bytes into a fresh buffer and PROMOTES it to an OWNER.
 */
void string_reserve(String *const self, USize const capacity);

/**
 * @brief Reverse a String's contents in place.
 * @param self Pointer to the String object.
 */
void string_reverse(String *const self);

/**
 * @brief Set the size of a String - the low-level setter external writers call after
 *        filling the buffer through string_get_data.
 *        LOWERING it on a VIEW re-arms the in-place append case; see the capacity rule on
 *        `owned` in this header. It does not terminate: the caller wrote the bytes.
 * @param self Pointer to the String object.
 * @param size Desired string size.
 * @note REFUSES as a no-op, in every build, a size the buffer cannot hold: past
 *       capacity - 1, or non-zero on the EMPTY String. The value is data (a count handed
 *       back by vsnprintf, strftime, a bounded read), never an abort.
 */
void string_set_size(String *const self, USize const size);

/**
 * @brief Shrink a String to fit its contents.
 * @param self Pointer to the String object.
 * @note Idempotent: an exact-fit String (capacity == size + 1) is left untouched, so a second
 *       shrink neither reallocates nor promotes an exact-fit VIEW.
 * @note Size 0 releases the buffer and demotes to the EMPTY String. A refused arena is a
 *       no-op leaving self intact.
 */
void string_shrink(String *const self);

/**
 * @brief Get a slice of a String starting at a specified index.
 *        The result is an OWNED copy carrying the source's allocator.
 * @param self Pointer to the String object.
 * @param index Start index of the slice. index == size is legal - the tail after
 *              the last delimiter - and yields the empty String, as does slicing an
 *              empty source. Only strictly past the end is a programming error, and
 *              ABORTS in checked builds.
 * @return Substring object; an OWNED copy. Release with string_uninit.
 */
String string_slice(String const *const self, USize const index);

/**
 * @brief Get a slice of a String within a specified range.
 *        Copies the INCLUSIVE range [from, to] - both endpoints are kept, so
 *        from == to is the legal single-character slice. (string_erase is
 *        exclusive; the two families differ deliberately and each says so.)
 * @param self Pointer to the String object.
 * @param from Start index of the range (inclusive).
 * @param to End index (INCLUSIVE). Must be < size; strictly past the end ABORTS in
 *           checked builds.
 * @return Substring object; an OWNED copy carrying the source's allocator. Release with
 *         string_uninit.
 */
String string_slice_range(String const *const self, USize const from, USize const to);

/**
 * @brief Split a String by a delimiter.
 * @param self Pointer to the String object.
 * @param delimiter Pointer to the delimiter string.
 * @return A copy of the FIRST token, OWNED when non-empty (the bytes before the first delimiter, or the
 *         whole self when the delimiter is absent), carrying self's allocator - an arena
 *         String's token is released in bulk with it; release a heap one with string_uninit.
 *         To walk every token without allocating, use string_split_next.
 */
String string_split_1(String const *const self, char const *const delimiter);

/**
 * @brief Split a String by a delimiter with a specified size.
 * @param self Pointer to the String object.
 * @param delimiter Pointer to the delimiter string.
 * @param delimiter_size Size of the delimiter.
 * @return A copy of the FIRST token, OWNED when non-empty (an empty token - and, on an arena, a
 *         refused borrow - is the EMPTY String); release with string_uninit. string_split_next
 *         walks every token without allocating.
 * @note An empty delimiter marks no split point, so the whole self is returned as
 *       one token. Splitting an empty self yields one empty token.
 */
String string_split_2(String const *const self, char const *const delimiter, USize const delimiter_size);

/**
 * @brief Split a String by a Str delimiter.
 * @param self Pointer to the String object.
 * @param delimiter Pointer to the Str delimiter.
 * @return A copy of the FIRST token, OWNED when non-empty (empty token or refused arena: the EMPTY
 *         String); release with string_uninit.
 */
String string_split_3(String const *const self, Str const *const delimiter);

/**
 * @brief Split a String by another String delimiter.
 * @param self Pointer to the String object.
 * @param delimiter Pointer to the String delimiter.
 * @return A copy of the FIRST token, OWNED when non-empty (empty token or refused arena: the EMPTY
 *         String); release with string_uninit.
 */
String string_split_4(String const *const self, String const *const delimiter);

/**
 * @brief Iterate the tokens of a String separated by a delimiter, without allocating.
 * @param self Pointer to the String object.
 * @param delimiter Delimiter separating tokens.
 * @param delimiter_size Size of the delimiter.
 * @param index Caller-owned iteration cursor. Set to 0 before the first call.
 * @param token_from Out: start index of the token.
 * @param token_size Out: byte length of the token.
 * @return true if a token was produced, false when iteration is complete.
 * @note An empty delimiter marks no split point, so the whole remaining string is
 *       returned as one final token. Splitting an empty self yields exactly one
 *       empty token, then reports iteration complete.
 */
bool string_split_next(String const *const self, char const *const delimiter, USize const delimiter_size, USize *const index, USize *const token_from, USize *const token_size);

/**
 * @brief Check whether a String starts with a C string prefix.
 * @param self Pointer to the String object.
 * @param data Pointer to the prefix C string.
 * @return true if self starts with data, false otherwise.
 */
bool string_starts_with_1(String const *const self, char const *const data);

/**
 * @brief Check whether a String starts with a prefix of a specified size.
 * @param self Pointer to the String object.
 * @param data Pointer to the prefix.
 * @param data_size Size of the prefix.
 * @return true if self starts with data, false otherwise.
 * @note A data_size of 0 matches every String, the empty String included: returns true.
 */
bool string_starts_with_2(String const *const self, char const *const data, USize const data_size);

/**
 * @brief Check whether a String starts with a Str prefix.
 * @param self Pointer to the String object.
 * @param data Pointer to the prefix Str.
 * @return true if self starts with data, false otherwise.
 */
bool string_starts_with_3(String const *const self, Str const *const data);

/**
 * @brief Check whether a String starts with another String prefix.
 * @param self Pointer to the String object.
 * @param data Pointer to the prefix String.
 * @return true if self starts with data, false otherwise.
 */
bool string_starts_with_4(String const *const self, String const *const data);

/**
 * @brief Convert a String to a floating-point number.
 * @param self Pointer to the String object.
 * @return Parsed floating-point value. Lenient: parsing stops at the first non-numeric byte and
 *         an out-of-range value saturates, so 0 is a value, not an error signal - only a String
 *         with no leading number yields 0. (A detecting string_try_to_numbers_* form, mirroring
 *         char_try_to_number_*, is deferred until a caller needs it.)
 */
FSize string_to_numbers_float(String const *const self);

/**
 * @brief Convert a String to a signed integer.
 * @param self Pointer to the String object.
 * @return Parsed signed integer value. Lenient: stops at the first non-numeric byte and saturates
 *         to ISIZE_MIN / ISIZE_MAX out of range, so 0 is a value, not an error signal.
 */
ISize string_to_numbers_int(String const *const self);

/**
 * @brief Convert a String to an unsigned integer.
 * @param self Pointer to the String object.
 * @return Parsed unsigned integer value. Lenient: stops at the first non-numeric byte and
 *         saturates to USIZE_MAX out of range, so 0 is a value, not an error signal.
 */
USize string_to_numbers_uint(String const *const self);

/**
 * @brief Trim leading and trailing whitespace, REPLACING self's buffer.
 * @param self Pointer to the String object.
 * @note Not in place: a fresh trimmed copy is built and self's buffer released, on every call
 *       and even when nothing was trimmed - so a VIEW becomes an OWNER, a pointer from
 *       string_get_data dangles. An all-whitespace input yields an OWNED zero-length String
 *       (size 0 over a one-byte buffer); a source that is ALREADY empty - the EMPTY String
 *       or an owned zero-length one - collapses straight to the EMPTY String instead.
 */
void string_trim(String *const self);

/**
 * @brief Uninitialize a String, freeing its resources.
 * @param self Pointer to the String object to uninitialize.
 */
void string_uninit(String *const self);

/**
 * @brief Convert a String to uppercase.
 * @param self Pointer to the String object.
 */
void string_upper(String *const self);

/**
 * @brief Word-wrap a String to a column width, returning a new String - OWNED when
 *        non-empty; an empty or whitespace-only source answers the EMPTY String,
 *        carrying self's allocator on an arena.
 *        Greedy reflow (see char_wrap_2): words re-joined with single spaces, a newline
 *        inserted before any word that would overflow `width`; width is a byte count.
 * @param self Source String.
 * @param width Maximum line width in bytes. Must be non-zero.
 * @return A newly-initialized String holding the wrapped text, carrying self's allocator (an
 *         arena String's result is released in bulk with it); release a heap one with
 *         string_uninit.
 */
String string_wrap(String const *const self, USize const width);

#endif // CONTAINER_STRING_H