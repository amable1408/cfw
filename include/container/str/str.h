/*
 * str.h - Canonical string buffer (Str) utilities for the C Libraries Framework
 *
 * Features:
 *   - Flexible, error-checked string buffer abstraction (Str)
 *   - Arena and standard allocation support
 *   - Rich API for add, remove, slice, repeat, compare, copy, and more
 *   - Canonical memory ownership and free function
 *
 * Usage Examples:
 *   @code
 *   // By value: an OWNED copy, released with str_uninit.
 *   Str s = str_init_static("hello", 5);
 *   str_add_last_1(&s, " world");
 *   // ... use s ...
 *   str_uninit(&s);
 *
 *   // On the heap: str_new_static copies too, so str_delete has a buffer to release.
 *   Str *h = str_new_static("hello", 5);
 *   str_delete(&h);
 *   @endcode
 *
 * Error Handling:
 *   Two classes, told apart by WHO got it wrong. A PROGRAMMING error - a nullptr where one is
 *   forbidden, a cross-allocator move, a slice position strictly past the end - trips
 *   error_check, which ABORTS the process in every build that defines ERROR_CHECK_ENABLED
 *   (every shipping build does). Nothing "logs and returns early". A DATA-shaped value - an
 *   index past the end (str_add_*, str_remove, str_replace_*), an out-of-range span
 *   (str_erase), a size that overflows (str_add_*, str_repeat, str_join_*), a refused arena
 *   or a declined copy - is REFUSED as real control flow in every build: the mutator is a
 *   no-op with self unchanged, the producer answers the EMPTY Str. Each function's @return /
 *   @note names its refusal. On HEAP OOM the producers split in two: the try_alloc group
 *   (str_copy_*, str_init_4, str_slice, str_split_*, str_new_4 / str_new_static, str_format,
 *   the float twins) WARNs and answers EMPTY; the borrowing group (str_add_*, str_join_*,
 *   str_slice_range, str_repeat, the int / uint twins) aborts inside the borrow - the closed
 *   abort class, not up for change.
 *
 * Thread Safety:
 *   Not thread-safe. Caller must synchronize if used from multiple threads. A VIEW shares its
 *   buffer with whatever it views, so the two are one object for synchronization purposes.
 *
 * Memory Management:
 *   - Functions returning `Str*` allocate the struct on the heap (str_new_*) or borrow it from
 *     the arena (str_alloc_new_*); str_delete routes it back to where it came from.
 *   - Functions returning `Str` by value do not allocate the struct; the caller owns the
 *     value, and str_uninit() releases its buffer when the Str owns one.
 *   - Functions that modify Str in place expect the caller to manage memory.
 *   - The canonical free function for heap-allocated Str is str_delete().
 *
 * Performance Characteristics:
 *   - str_add_* reallocates an exact-fit buffer on every growth (str_erase and str_remove
 *     shift in place and leave slack), so appending in a loop is O(n^2); builders that
 *     accumulate should use String instead. str_join_* borrows once, pre-sized.
 *   - Comparison, search, split, and replace operations are linear in the sizes involved.
 *
 * Dependencies:
 *   - char/char.h
 *
 * See str.c for implementation details.
 */
#ifndef CONTAINER_STR_H
#define CONTAINER_STR_H

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/
#include <char/char.h>

/*==============================================================================
 * MARK: - Typedefs
 *============================================================================*/
/**
 * @brief String buffer abstraction for dynamic and arena-backed strings.
 *
 * A Str is either an OWNER or a VIEW, and `owned` is what tells them apart.
 *   - VIEW: str_init_2/3, str_new_2/3 and their str_alloc_* twins wrap memory the caller
 *     supplies - possibly a string literal or a stack buffer - and never release it.
 *   - OWNER: everything that copies or produces - str_init_static, str_init_4 (a deep
 *     copy, despite sitting beside the views), str_new_static, str_new_4, str_format,
 *     str_from_numbers_* (all twelve), str_from_replace_*, str_from_trim, str_join_*,
 *     str_split_1..3 (the first token, owned), str_slice, str_slice_range, str_repeat,
 *     str_move_2 (adopts the buffer), and every str_alloc_* form of these. str_uninit
 *     releases only owners.
 *   - PROMOTES: str_add_*, str_copy_* and str_repeat replace the buffer and leave an OWNER,
 *     whatever self was. str_trim / str_alloc_trim adopt the trimmed copy the same way.
 *   - INHERITS: str_move_3 TRANSFERS the source's flag as-is, so moving a view yields a view.
 *
 * EMPTY vs empty. The EMPTY value is `data == nullptr, size 0, owned false` (the arena
 * field kept): what str_init_1 builds, what str_uninit leaves, and what every producer of
 * NOTHING answers - a trim of pure whitespace, a replace that consumes everything, a join of
 * no parts, a repeat of zero, an empty first token, a refused arena, a declined copy. There
 * is no owned zero-length Str: an exact-fit type gains nothing from a one-byte block
 * (String keeps its owned zero-length trim because its capacity is worth keeping). A caller
 * that must tell a refusal from a produced nothing compares `.size` to what it passed in
 * (str_init_static's note); the EMPTY Str is a legal value everywhere, so no function aborts
 * on it - the mutators and searches treat it as "".
 *
 * Adopting a buffer: str_init_1 followed by str_move_2 IS the adopt constructor - move_2
 * sets `owned` and takes the pointer, so no field is ever poked by hand.
 *
 * Ownership is tracked explicitly rather than inferred from `size` or from
 * `data != nullptr`: neither can distinguish the two cases, so before this
 * field existed str_uninit on a view handed a literal to free().
 *
 * Maintenance note: the comparison/find forwarders (equal/iequal/comptime,
 * find/find_reverse and their overloads) mirror char's kernels by hand - a fix
 * to a char comparison or search function must be mirrored here (and in String)
 * manually, since nothing shares the bodies.
 *
 * The default is false, so a CONSTRUCTOR that forgets to set it yields a view,
 * which leaks rather than double-frees. That guarantee does NOT extend to the
 * mutating functions: str_add_*, str_copy_*, str_repeat, str_trim and str_move_* replace
 * (or release) the buffer, so they must consult `owned` before releasing or they free the
 * caller's memory. Any new function that releases self->data has to check it.
 *
 * A view must be over WRITABLE memory if it is passed to a mutator - str_lower,
 * str_upper, str_reverse, str_fill, str_erase, str_remove and str_replace_* all write
 * through `data` in place, so a view over a string literal is a segfault there.
 * str_init_2/3 and str_alloc_init_2/3 take `char *` rather than `char const *` for that
 * reason.
 *
 * Str is a VALUE type carrying an ownership flag, so copying an owner duplicates
 * the claim: `Str b = a;` leaves two Strs both believing they must release the
 * same buffer, and uninit-ing both is a double free. Pass owners by pointer, or
 * hand ownership over explicitly with str_move_3, which clears the source.
 *
 * `allocator` is part of the contract too, not just `owned`: a Str must be
 * released through the SAME allocator its buffer was borrowed from. A function
 * that produces a Str from an arena-backed one has to carry the arena across
 * (str_alloc_init_3, not str_init_3), or str_uninit sends arena memory to free().
 */
typedef struct {
#ifdef ARENA_IMPLEMENTATION
    Arena *allocator; /**< Arena allocator if used */
#endif // ARENA_IMPLEMENTATION
    char *data;       /**< Pointer to string data */
    USize size;       /**< Size of the string (bytes) */
    bool owned;       /**< True when this Str owns data and str_uninit must release it */
} Str;

/*==============================================================================
 * MARK: - Arena-backed API
 *============================================================================*/
#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Create Str from printf-style formatting using an arena allocator.
 *        The arena twin of str_format: the buffer is borrowed from the arena and the
 *        result carries that allocator, so uninit releases it back to the arena.
 * @param allocator Arena allocator to use. Must not be nullptr (varargs force it first).
 * @param format printf-style format string. Must not be nullptr.
 * @param ... Format arguments.
 * @return Str by value; the empty Str when formatting produces nothing or the
 *         arena was refused.
 */
Str str_alloc_format(Arena *const allocator, char const *const format, ...);

/**
 * @brief Create Str from a floating-point number using arena allocator.
 * @param number Floating-point value.
 * @param allocator Arena allocator to use.
 * @return Str by value: an OWNED copy carrying the arena (released in bulk by arena_uninit, or
 *         by str_uninit); the EMPTY Str on a refused arena, or for NaN, an infinity, or a
 *         magnitude at or above 2^64 (char_from_numbers_float_2 refuses those).
 */
Str str_alloc_from_numbers_float_1(FSize const number, Arena *const allocator);

/**
 * @brief Create Str from a floating-point number with precision using arena allocator.
 * @param number Floating-point value.
 * @param precision Number of digits after the decimal point: at most USIZE_DIGITS_MAX - 1
 *        (19 on 64-bit; larger aborts as caller error), 0 emits no point (char's rule).
 * @param allocator Arena allocator to use.
 * @return Str by value: an OWNED copy carrying the arena (released in bulk by arena_uninit, or
 *         by str_uninit); the EMPTY Str on a refused arena, or for NaN, an infinity, or a
 *         magnitude at or above 2^64 (char_from_numbers_float_2 refuses those).
 */
Str str_alloc_from_numbers_float_2(FSize const number, U8 const precision, Arena *const allocator);

/**
 * @brief Create Str from integer using arena allocator.
 * @param number Integer value.
 * @param allocator Arena allocator to use.
 * @return Str by value: an OWNED copy carrying the arena (released in bulk by arena_uninit, or
 *         by str_uninit), or the EMPTY Str on a refused arena.
 */
Str str_alloc_from_numbers_int_1(ISize const number, Arena *const allocator);

/**
 * @brief Create Str from integer with padding using arena allocator.
 * @param number Integer value.
 * @param padding Number of extra '0' characters prepended to the digits (additive,
 *                not a minimum width: 7 at padding 3 formats as "0007").
 * @param allocator Arena allocator to use.
 * @return Str by value: an OWNED copy carrying the arena (released in bulk by arena_uninit, or
 *         by str_uninit), or the EMPTY Str on a refused arena.
 */
Str str_alloc_from_numbers_int_2(ISize const number, U8 const padding, Arena *const allocator);

/**
 * @brief Create Str from unsigned integer using arena allocator.
 * @param number Unsigned integer value.
 * @param allocator Arena allocator to use.
 * @return Str by value: an OWNED copy carrying the arena (released in bulk by arena_uninit, or
 *         by str_uninit), or the EMPTY Str on a refused arena.
 */
Str str_alloc_from_numbers_uint_1(USize const number, Arena *const allocator);

/**
 * @brief Create Str from unsigned integer with padding using arena allocator.
 * @param number Unsigned integer value.
 * @param padding Number of extra '0' characters prepended to the digits (additive,
 *                not a minimum width: 7 at padding 3 formats as "0007").
 * @param allocator Arena allocator to use.
 * @return Str by value: an OWNED copy carrying the arena (released in bulk by arena_uninit, or
 *         by str_uninit), or the EMPTY Str on a refused arena.
 */
Str str_alloc_from_numbers_uint_2(USize const number, U8 const padding, Arena *const allocator);

/**
 * @brief Create an arena-backed copy of Str with find replaced by replace.
 * @param self Source Str.
 * @param find C-string to replace.
 * @param replace Replacement C-string.
 * @param allocator Arena allocator to use.
 * @return Str by value: an OWNED copy carrying the arena; the EMPTY Str when nothing is left
 *         ("abc" with "abc" replaced by "") or the arena was refused - the same answers as
 *         str_from_replace_1, whatever the allocator.
 */
Str str_alloc_from_replace_1(Str const *const self, char const *const find, char const *const replace, Arena *const allocator);

/**
 * @brief Create an arena-backed copy of Str with find (of given size) replaced by replace.
 * @param self Source Str.
 * @param find Data to replace.
 * @param find_size Size of find.
 * @param replace Replacement data.
 * @param replace_size Size of replace.
 * @param allocator Arena allocator to use.
 * @return Str by value: an OWNED copy carrying the arena; the EMPTY Str when nothing is left
 *         or the arena was refused (str_alloc_from_replace_1).
 * @note A find_size of 0 matches nothing, so the result is a verbatim copy of self.
 */
Str str_alloc_from_replace_2(Str const *const self, char const *const find, USize const find_size, char const *const replace, USize const replace_size, Arena *const allocator);

/**
 * @brief Create an arena-backed trimmed copy of a Str.
 * @param self Source Str.
 * @param allocator Arena allocator to use.
 * @return Str by value: an OWNED trimmed copy carrying the arena; the EMPTY Str for pure
 *         whitespace (the block is released, the heap twin's answer) or a refused arena.
 */
Str str_alloc_from_trim(Str const *const self, Arena *const allocator);

/**
 * @brief Initialize empty Str using arena allocator.
 * @param allocator Arena allocator to use.
 * @return Str by value.
 */
Str str_alloc_init_1(Arena *const allocator);

/**
 * @brief Initialize Str from data using arena allocator.
 * @param data Pointer to data. WRITABLE, like str_init_2's: the result is a VIEW that the
 *        in-place mutators write through.
 * @param allocator Arena allocator to use.
 * @return Str by value: a VIEW over data carrying the arena, never released by str_uninit.
 */
Str str_alloc_init_2(char *const data, Arena *const allocator);

/**
 * @brief Initialize Str from data and size using arena allocator.
 * @param data Pointer to data. WRITABLE, like str_init_3's.
 * @param data_size Size of data. Zero yields the EMPTY Str.
 * @param allocator Arena allocator to use.
 * @return Str by value: a VIEW over data carrying the arena, never released by str_uninit.
 */
Str str_alloc_init_3(char *const data, USize const data_size, Arena *const allocator);

/**
 * @brief Initialize Str from another Str using arena allocator.
 * @param data Pointer to source Str.
 * @param allocator Arena allocator to use.
 * @return Str by value: an OWNED deep copy carrying the arena (str_alloc_init_static), the
 *         EMPTY Str for an empty source or a refused arena.
 */
Str str_alloc_init_4(Str const *const data, Arena *const allocator);

/**
 * @brief Initialize static Str from data and size using arena allocator.
 * @param data Pointer to data.
 * @param data_size Size of data.
 * @param allocator Arena allocator to use.
 * @return Str by value: an owned copy of the data. Degrades to the empty Str
 *         when the arena refuses the copy; a caller that must tell that from
 *         an empty source compares `.size` to `data_size`.
 */
Str str_alloc_init_static(char const *const data, USize const data_size, Arena *const allocator);

/**
 * @brief Join an array of Str with a C-string separator using arena allocator.
 * @param parts Array of Str pointers.
 * @param count Number of Str in parts.
 * @param separator Separator placed between parts.
 * @param allocator Arena allocator to use.
 * @return Str by value: an OWNED result carrying the arena, borrowed once and pre-sized. The
 *         EMPTY Str for no parts (parts may then be nullptr), for a total of zero bytes (every
 *         part empty AND nothing between them - two empties joined by "," are ","), or when
 *         the total overflows or the arena is refused - the WHOLE join refuses, never a
 *         truncated one.
 */
Str str_alloc_join_1(Str const *const *const parts, USize const count, char const *const separator, Arena *const allocator);

/**
 * @brief Join an array of Str with a separator of given size using arena allocator.
 * @param parts Array of Str pointers.
 * @param count Number of Str in parts.
 * @param separator Separator placed between parts.
 * @param separator_size Size of separator.
 * @param allocator Arena allocator to use.
 * @return Str by value: see str_alloc_join_1.
 */
Str str_alloc_join_2(Str const *const *const parts, USize const count, char const *const separator, USize const separator_size, Arena *const allocator);

/**
 * @brief Allocate new Str on heap using arena allocator.
 * @param allocator Arena allocator to use.
 * @return Pointer to new Str, or nullptr when the arena was refused (a rejected
 *         arena_init_2 leaves a null handler). Caller must free with str_delete().
 *         The whole str_alloc_new_* family propagates this nullptr.
 */
Str* str_alloc_new_1(Arena *const allocator);

/**
 * @brief Allocate new Str from data using arena allocator.
 * @param data Pointer to data. WRITABLE: the result is a VIEW (str_alloc_init_2).
 * @param allocator Arena allocator to use.
 * @return Pointer to new Str, a VIEW over data. Caller must free with str_delete().
 */
Str* str_alloc_new_2(char *const data, Arena *const allocator);

/**
 * @brief Allocate new Str from data and size using arena allocator.
 * @param data Pointer to data. WRITABLE: the result is a VIEW (str_alloc_init_3).
 * @param data_size Size of data.
 * @param allocator Arena allocator to use.
 * @return Pointer to new Str, a VIEW over data. Caller must free with str_delete().
 */
Str* str_alloc_new_3(char *const data, USize const data_size, Arena *const allocator);

/**
 * @brief Allocate new Str from another Str using arena allocator.
 * @param data Pointer to source Str.
 * @param allocator Arena allocator to use.
 * @return Pointer to new Str holding an OWNED deep copy - or a pointer to the EMPTY Str when
 *         the buffer borrow was refused after the struct borrow succeeded (compare .size to the
 *         source, as str_alloc_init_static's note says). Caller must free with str_delete().
 */
Str* str_alloc_new_4(Str const *const data, Arena *const allocator);

/**
 * @brief Allocate new static Str from data and size using arena allocator.
 * @param data Pointer to data.
 * @param data_size Size of data.
 * @param allocator Arena allocator to use.
 * @return Pointer to new Str holding an OWNED copy - or a pointer to the EMPTY Str when the
 *         buffer borrow was refused after the struct borrow succeeded (compare .size to
 *         data_size). Caller must free with str_delete().
 */
Str* str_alloc_new_static(char const *const data, USize const data_size, Arena *const allocator);

/**
 * @brief Trim whitespace in place using an arena allocator.
 * @param self Pointer to Str. Modified in place: PROMOTED to an owner of the exact-size
 *        trimmed copy, or left the EMPTY Str (allocator kept) when only whitespace remained.
 * @param allocator Arena allocator to use. Must be self's own: the object's home never changes
 *        (str_delete routes the struct by it), so another arena is REFUSED as a no-op.
 * @note A REFUSED arena keeps self exactly as it was (it used to be wiped to EMPTY).
 */
void str_alloc_trim(Str *const self, Arena *const allocator);
#endif // ARENA_IMPLEMENTATION

/*==============================================================================
 * MARK: - Standard API
 *============================================================================*/
/**
 * @brief Add data at index. Modifies self in place.
 * @param self Pointer to Str. Modified in place.
 * @param data Pointer to data to add.
 * @param index Index at which to add data. index == size appends.
 * @note The whole str_add_* family reallocates an exact-size buffer and copies on
 *       EVERY call - appending in a loop is O(n^2). Builders that accumulate should
 *       use String, whose capacity grows geometrically (amortized O(n)).
 * @note REFUSES as a no-op, self unchanged, in every build: an index past the end, a
 *       data_size that overflows the size, and a refused arena. Self is PROMOTED to an
 *       owner on success, whatever it was. data may point INSIDE self: the new buffer is
 *       built before the old one is released, so an aliased insert is safe.
 */
void str_add_1(Str *const self, char const *const data, USize const index);

/**
 * @brief Add data of given size at index. Modifies self in place.
 * @param self Pointer to Str. Modified in place.
 * @param data Pointer to data to add.
 * @param data_size Size of data to add.
 * @param index Index at which to add data. index == size appends.
 * @note See str_add_1: the same refusals, promotion, and alias safety.
 */
void str_add_2(Str *const self, char const *const data, USize const data_size, USize const index);

/**
 * @brief Add another Str at index. Modifies self in place.
 * @param self Pointer to Str. Modified in place.
 * @param data Pointer to source Str.
 * @param index Index at which to add data. index == size appends.
 * @note See str_add_1: the same refusals, promotion, and alias safety. An EMPTY data is a no-op.
 */
void str_add_3(Str *const self, Str const *const data, USize const index);

/**
 * @brief Add data to beginning. Modifies self in place.
 * @param self Pointer to Str. Modified in place.
 * @param data Pointer to data to add.
 * @note See str_add_1: the same refusals, promotion, and alias safety.
 */
void str_add_first_1(Str *const self, char const *const data);

/**
 * @brief Add data of given size to beginning. Modifies self in place.
 * @param self Pointer to Str. Modified in place.
 * @param data Pointer to data to add.
 * @param data_size Size of data to add.
 * @note See str_add_1: the same refusals, promotion, and alias safety.
 */
void str_add_first_2(Str *const self, char const *const data, USize const data_size);

/**
 * @brief Add another Str to beginning. Modifies self in place.
 * @param self Pointer to Str. Modified in place.
 * @param data Pointer to source Str.
 * @note See str_add_1: the same refusals, promotion, and alias safety.
 */
void str_add_first_3(Str *const self, Str const *const data);

/**
 * @brief Add data to end. Modifies self in place.
 * @param self Pointer to Str. Modified in place.
 * @param data Pointer to data to add.
 * @note See str_add_1: the same refusals, promotion, and alias safety.
 */
void str_add_last_1(Str *const self, char const *const data);

/**
 * @brief Add data of given size to end. Modifies self in place.
 * @param self Pointer to Str. Modified in place.
 * @param data Pointer to data to add.
 * @param data_size Size of data to add.
 * @note See str_add_1: the same refusals, promotion, and alias safety.
 */
void str_add_last_2(Str *const self, char const *const data, USize const data_size);

/**
 * @brief Add another Str to end. Modifies self in place.
 * @param self Pointer to Str. Modified in place.
 * @param data Pointer to source Str.
 * @note See str_add_1: the same refusals, promotion, and alias safety.
 */
void str_add_last_3(Str *const self, Str const *const data);

/**
 * @brief Get character at index.
 * @param self Pointer to Str.
 * @param index Index of character to retrieve. An index at or past the end is legal
 *              and answers '\0' (char_at parity) - a scan running off a short Str
 *              asks it routinely.
 * @return Character at index, or '\0' when index is at or past the end (the empty
 *         Str included).
 */
char str_at(Str const *const self, USize const index);

/**
 * @brief Compare Str to C-string for equality.
 * @param self Pointer to Str.
 * @param data Pointer to C-string.
 * @return true if equal, false otherwise.
 */
bool str_compare_equal_1(Str const *const self, char const *const data);

/**
 * @brief Compare Str to C-string (with size) for equality.
 * @param self Pointer to Str.
 * @param data Pointer to C-string.
 * @param data_size Size of C-string.
 * @return true if equal, false otherwise.
 */
bool str_compare_equal_2(Str const *const self, char const *const data, USize const data_size);

/**
 * @brief Compare two Str for equality.
 * @param self Pointer to Str.
 * @param data Pointer to other Str.
 * @return true if equal, false otherwise.
 */
bool str_compare_equal_3(Str const *const self, Str const *const data);

/**
 * @brief Timing-safe (constant-time) equality of Str and a C-string. Use it to
 *        compare secrets; folds every byte with no early exit.
 * @param self Pointer to Str.
 * @param data Pointer to C-string.
 * @return true if equal, false otherwise.
 */
bool str_compare_equal_comptime_1(Str const *const self, char const *const data);

/**
 * @brief Timing-safe (constant-time) equality of Str and a sized C-string.
 * @param self Pointer to Str.
 * @param data Pointer to C-string.
 * @param data_size Size of C-string.
 * @return true if equal, false otherwise.
 */
bool str_compare_equal_comptime_2(Str const *const self, char const *const data, USize const data_size);

/**
 * @brief Timing-safe (constant-time) equality of two Str.
 * @param self Pointer to Str.
 * @param data Pointer to other Str.
 * @return true if equal, false otherwise.
 */
bool str_compare_equal_comptime_3(Str const *const self, Str const *const data);

/**
 * @brief Compare Str to C-string for case-insensitive equality.
 * @param self Pointer to Str.
 * @param data Pointer to C-string.
 * @return true if equal ignoring ASCII case, false otherwise.
 */
bool str_compare_iequal_1(Str const *const self, char const *const data);

/**
 * @brief Compare Str to C-string (with size) for case-insensitive equality.
 * @param self Pointer to Str.
 * @param data Pointer to C-string.
 * @param data_size Size of C-string.
 * @return true if equal ignoring ASCII case, false otherwise.
 */
bool str_compare_iequal_2(Str const *const self, char const *const data, USize const data_size);

/**
 * @brief Compare two Str for case-insensitive equality.
 * @param self Pointer to Str.
 * @param data Pointer to other Str.
 * @return true if equal ignoring ASCII case, false otherwise.
 */
bool str_compare_iequal_3(Str const *const self, Str const *const data);

/**
 * @brief Timing-safe case-insensitive equality of Str and a C-string. Use it to
 *        compare secrets; folds every byte with no early exit.
 * @param self Pointer to Str.
 * @param data Pointer to C-string.
 * @return true if equal ignoring ASCII case, false otherwise.
 */
bool str_compare_iequal_comptime_1(Str const *const self, char const *const data);

/**
 * @brief Timing-safe case-insensitive equality of Str and a sized C-string.
 * @param self Pointer to Str.
 * @param data Pointer to C-string.
 * @param data_size Size of C-string.
 * @return true if equal ignoring ASCII case, false otherwise.
 */
bool str_compare_iequal_comptime_2(Str const *const self, char const *const data, USize const data_size);

/**
 * @brief Timing-safe case-insensitive equality of two Str.
 * @param self Pointer to Str.
 * @param data Pointer to other Str.
 * @return true if equal ignoring ASCII case, false otherwise.
 */
bool str_compare_iequal_comptime_3(Str const *const self, Str const *const data);

/**
 * @brief Check whether a C-string occurs anywhere in Str.
 * @param self Pointer to Str.
 * @param data Pointer to C-string to search for.
 * @return true if data occurs in self, false otherwise.
 */
bool str_contains_1(Str const *const self, char const *const data);

/**
 * @brief Check whether data of given size occurs anywhere in Str.
 * @param self Pointer to Str.
 * @param data Pointer to data to search for.
 * @param data_size Size of data.
 * @return true if data occurs in self, false otherwise.
 * @note A data_size of 0 occurs inside every Str, the empty Str included: returns true.
 */
bool str_contains_2(Str const *const self, char const *const data, USize const data_size);

/**
 * @brief Check whether another Str occurs anywhere in Str.
 * @param self Pointer to Str.
 * @param data Pointer to Str to search for.
 * @return true if data occurs in self, false otherwise.
 */
bool str_contains_3(Str const *const self, Str const *const data);

/**
 * @brief Copy C-string into Str. Modifies self in place.
 * @param self Pointer to Str. Modified in place: PROMOTED to an owner of the copy (the old
 *        buffer is released if owned), or left the EMPTY Str for an empty source.
 * @param data Pointer to C-string. May point inside self (a self-prefix copy truncates).
 * @note REFUSES with self unchanged when the copy cannot be taken - a refused arena, or the
 *       heap declining (str_init_static). It used to store the empty and release the content.
 */
void str_copy_1(Str *const self, char const *const data);

/**
 * @brief Copy C-string of given size into Str. Modifies self in place.
 * @param self Pointer to Str. Modified in place.
 * @param data Pointer to C-string. May point inside self.
 * @param data_size Size of C-string. Zero leaves the EMPTY Str.
 * @note See str_copy_1: promotes, refuses with self unchanged.
 */
void str_copy_2(Str *const self, char const *const data, USize const data_size);

/**
 * @brief Copy another Str into Str. Modifies self in place.
 * @param self Pointer to Str. Modified in place.
 * @param data Pointer to source Str. May be self (a correct, if wasteful, no-op).
 * @note See str_copy_1: promotes, refuses with self unchanged.
 */
void str_copy_3(Str *const self, Str const *const data);

/**
 * @brief Delete Str and free memory. Canonical free function for heap-allocated Str.
 * @param self Pointer to Str pointer. Must not be nullptr. After this call, *self is nullptr.
 * @note Only use on Str allocated with str_new_* or str_alloc_new_*. A nullptr *self is an
 *       idempotent no-op (free(NULL)'s idiom): a second call after the first nulled the handle,
 *       or a str_alloc_new_* that answered nullptr, is safe.
 */
void str_delete(Str **const self);

/**
 * @brief Check if Str is empty.
 * @param self Pointer to Str.
 * @return true if empty, false otherwise.
 */
bool str_empty(Str const *const self);

/**
 * @brief Check whether Str ends with a C-string suffix.
 * @param self Pointer to Str.
 * @param data Pointer to suffix C-string.
 * @return true if self ends with data, false otherwise.
 */
bool str_ends_with_1(Str const *const self, char const *const data);

/**
 * @brief Check whether Str ends with a suffix of given size.
 * @param self Pointer to Str.
 * @param data Pointer to suffix.
 * @param data_size Size of suffix.
 * @return true if self ends with data, false otherwise.
 * @note A data_size of 0 matches every Str, the empty Str included: returns true.
 */
bool str_ends_with_2(Str const *const self, char const *const data, USize const data_size);

/**
 * @brief Check whether Str ends with another Str suffix.
 * @param self Pointer to Str.
 * @param data Pointer to suffix Str.
 * @return true if self ends with data, false otherwise.
 */
bool str_ends_with_3(Str const *const self, Str const *const data);

/**
 * @brief Erase range from Str. Modifies self in place.
 *        Removes the half-open range [from, to): `to` is EXCLUSIVE, the one-past-end
 *        convention regex match ends and size offsets already use. (Note the slice_range
 *        functions are INCLUSIVE - the two families differ deliberately and each says so.)
 * @param self Pointer to Str. Modified in place.
 * @param from Start index (inclusive).
 * @param to End index (exclusive); to == size erases through the end.
 * @note REFUSES rather than aborting on an out-of-range span (from > to, or
 *       to > size): the indices are regex-match-shaped values. from == to is the
 *       legal empty erase.
 */
void str_erase(Str *const self, USize const from, USize const to);

/**
 * @brief Fill Str with a character. Modifies self in place.
 * @param self Pointer to Str. Modified in place.
 * @param c Character to fill with.
 */
void str_fill(Str *const self, char const c);

/**
 * @brief Find data in Str starting at index.
 * @param self Pointer to Str.
 * @param self_index Index to start searching from.
 * @param data Pointer to data to search for.
 * @return Index of first occurrence, or (USize)-1 if not found.
 */
USize str_find_1(Str const *const self, USize const self_index, char const *const data);

/**
 * @brief Find data of given size in Str starting at index.
 * @param self Pointer to Str.
 * @param self_index Index to start searching from.
 * @param data Pointer to data to search for.
 * @param data_size Size of data to search for.
 * @return Index of first occurrence, or (USize)-1 if not found.
 * @note A data_size of 0 is found at self_index (the search origin); a non-empty
 *       data searched in an empty self returns (USize)-1.
 */
USize str_find_2(Str const *const self, USize const self_index, char const *const data, USize const data_size);

/**
 * @brief Find another Str in Str starting at index.
 * @param self Pointer to Str.
 * @param self_index Index to start searching from.
 * @param data Pointer to Str to search for.
 * @return Index of first occurrence, or (USize)-1 if not found.
 */
USize str_find_3(Str const *const self, USize const self_index, Str const *const data);

/**
 * @brief Find the first index in Str matching any character of a set.
 * @param self Pointer to Str.
 * @param set Pointer to set of characters to match.
 * @return Index of first matching character, or (USize)-1 if none match.
 */
USize str_find_any_1(Str const *const self, char const *const set);

/**
 * @brief Find the first index in Str matching any character of a set of given size.
 * @param self Pointer to Str.
 * @param set Pointer to set of characters to match.
 * @param set_size Number of characters in set.
 * @return Index of first matching character, or (USize)-1 if none match.
 * @note A set_size of 0 holds no candidate character, so nothing matches: returns (USize)-1.
 */
USize str_find_any_2(Str const *const self, char const *const set, USize const set_size);

/**
 * @brief Count occurrences of data in Str.
 * @param self Pointer to Str.
 * @param data Pointer to data to count.
 * @return Number of occurrences.
 */
USize str_find_count_1(Str const *const self, char const *const data);

/**
 * @brief Count occurrences of data of given size in Str.
 * @param self Pointer to Str.
 * @param data Pointer to data to count.
 * @param data_size Size of data to count.
 * @return Number of occurrences.
 * @note A data_size of 0 counts as 0 occurrences, deliberately not one match per gap as in
 *       Python: the replace family sizes its output buffer from this count, so a positive
 *       answer would splice a replacement into every gap.
 */
USize str_find_count_2(Str const *const self, char const *const data, USize const data_size);

/**
 * @brief Check whether a C-string exists in Str.
 * @param self Pointer to Str.
 * @param data Pointer to data to check for.
 * @return true if data exists in self, false otherwise.
 */
bool str_find_exists_1(Str const *const self, char const *const data);

/**
 * @brief Check whether data of given size exists in Str.
 * @param self Pointer to Str.
 * @param data Pointer to data to check for.
 * @param data_size Size of data.
 * @return true if data exists in self, false otherwise.
 * @note A data_size of 0 exists inside every Str, the empty Str included: returns true.
 */
bool str_find_exists_2(Str const *const self, char const *const data, USize const data_size);

/**
 * @brief Check whether another Str exists in Str.
 * @param self Pointer to Str.
 * @param data Pointer to Str to check for.
 * @return true if data exists in self, false otherwise.
 */
bool str_find_exists_3(Str const *const self, Str const *const data);

/**
 * @brief Find data in Str in reverse starting at index.
 * @param self Pointer to Str.
 * @param self_index Index to start searching from.
 * @param data Pointer to data to search for.
 * @return Index of last occurrence, or (USize)-1 if not found.
 */
USize str_find_reverse_1(Str const *const self, USize const self_index, char const *const data);

/**
 * @brief Find data of given size in Str in reverse starting at index.
 * @param self Pointer to Str.
 * @param self_index Index to start searching from.
 * @param data Pointer to data to search for.
 * @param data_size Size of data to search for.
 * @return Index of last occurrence, or (USize)-1 if not found.
 * @note Mirrors str_find_2: a data_size of 0 is found at self->size (the far end),
 *       not at self_index.
 */
USize str_find_reverse_2(Str const *const self, USize const self_index, char const *const data, USize const data_size);

/**
 * @brief Create a Str from a printf-style format string.
 * @param format Format string.
 * @param ... Format arguments.
 * @return Str by value: an OWNED heap result, released by str_uninit. The EMPTY Str when the
 *         render is empty, when vsnprintf reports an encoding error, or when memory declines
 *         (str_init_static's shape) - the last two are WARNed through log_message_try_1.
 */
Str str_format(char const *const format, ...);

/**
 * @brief Create Str from a floating-point number.
 * @param number Floating-point value.
 * @return Str by value: an OWNED heap copy, released by str_uninit; the EMPTY Str for NaN,
 *         an infinity, a magnitude at or above 2^64, or when memory declines (WARNed).
 */
Str str_from_numbers_float_1(FSize const number);

/**
 * @brief Create Str from a floating-point number with a given precision.
 * @param number Floating-point value.
 * @param precision Number of digits after the decimal point: at most USIZE_DIGITS_MAX - 1
 *        (19 on 64-bit; larger aborts as caller error), 0 emits no point (char's rule).
 * @return Str by value: an OWNED heap copy, released by str_uninit; the EMPTY Str for NaN,
 *         an infinity, a magnitude at or above 2^64, or when memory declines (WARNed).
 */
Str str_from_numbers_float_2(FSize const number, U8 const precision);

/**
 * @brief Create Str from integer.
 * @param number Integer value.
 * @return Str by value: an OWNED heap copy, released by str_uninit.
 * @note Heap OOM aborts inside the borrow (the closed abort class) - unlike the float twins,
 *       which WARN and answer EMPTY.
 */
Str str_from_numbers_int_1(ISize const number);

/**
 * @brief Create Str from integer with padding.
 * @param number Integer value.
 * @param padding Number of extra '0' characters prepended to the digits (additive,
 *                not a minimum width: 7 at padding 3 formats as "0007").
 * @return Str by value: an OWNED heap copy, released by str_uninit.
 * @note Heap OOM aborts inside the borrow (the closed abort class) - unlike the float twins,
 *       which WARN and answer EMPTY.
 */
Str str_from_numbers_int_2(ISize const number, U8 const padding);

/**
 * @brief Create Str from unsigned integer.
 * @param number Unsigned integer value.
 * @return Str by value: an OWNED heap copy, released by str_uninit.
 * @note Heap OOM aborts inside the borrow (the closed abort class) - unlike the float twins,
 *       which WARN and answer EMPTY.
 */
Str str_from_numbers_uint_1(USize const number);

/**
 * @brief Create Str from unsigned integer with padding.
 * @param number Unsigned integer value.
 * @param padding Number of extra '0' characters prepended to the digits (additive,
 *                not a minimum width: 7 at padding 3 formats as "0007").
 * @return Str by value: an OWNED heap copy, released by str_uninit.
 * @note Heap OOM aborts inside the borrow (the closed abort class) - unlike the float twins,
 *       which WARN and answer EMPTY.
 */
Str str_from_numbers_uint_2(USize const number, U8 const padding);

/**
 * @brief Create a copy of Str with every occurrence of find replaced by replace.
 * @param self Source Str.
 * @param find C-string to replace.
 * @param replace Replacement C-string.
 * @return Str by value: an OWNED copy released by str_uninit, or the EMPTY Str when nothing is
 *         left ("abc" with "abc" replaced by "") - there is no owned zero-length Str.
 */
Str str_from_replace_1(Str const *const self, char const *const find, char const *const replace);

/**
 * @brief Create a copy of Str with every occurrence of find (of given size) replaced by replace.
 * @param self Source Str.
 * @param find Data to replace.
 * @param find_size Size of find.
 * @param replace Replacement data.
 * @param replace_size Size of replace.
 * @return Str by value: an OWNED copy, or the EMPTY Str when nothing is left (str_from_replace_1).
 * @note A find_size of 0 matches nothing, so the result is a verbatim copy of self.
 */
Str str_from_replace_2(Str const *const self, char const *const find, USize const find_size, char const *const replace, USize const replace_size);

/**
 * @brief Create a trimmed copy of a Str.
 * @param self Source Str. Not modified.
 * @return Str by value: an OWNED trimmed copy released by str_uninit, or the EMPTY Str for
 *         pure whitespace (unlike string_from_trim, which keeps an owned zero-length String:
 *         Str is exact-fit and has no capacity worth keeping).
 */
Str str_from_trim(Str const *const self);

/**
 * @brief Get pointer to Str data.
 * @param self Pointer to Str.
 * @return Pointer to data: nullptr for the EMPTY Str; terminated for an OWNER (every producer
 *         writes the terminator); for a VIEW, terminated only if the caller's buffer was.
 */
char* str_get_data(Str const *const self);

/**
 * @brief Get the size of Str.
 * @param self Pointer to Str.
 * @return The size, by value.
 */
USize str_get_size(Str const *const self);

/**
 * @brief Initialize empty Str.
 * @return Str by value: the EMPTY Str.
 */
Str str_init_1(void);

/**
 * @brief Initialize Str from C-string.
 * @param data Pointer to C-string. WRITABLE: the in-place mutators write through a view.
 * @return Str by value: a VIEW over data, never released by str_uninit.
 */
Str str_init_2(char *const data);

/**
 * @brief Initialize Str from C-string and size.
 * @param data Pointer to C-string. WRITABLE (see str_init_2).
 * @param data_size Size of C-string. Zero yields the EMPTY Str.
 * @return Str by value: a VIEW over data, never released by str_uninit.
 */
Str str_init_3(char *const data, USize const data_size);

/**
 * @brief Initialize Str from another Str.
 * @param data Pointer to source Str.
 * @return Str by value: an OWNED deep copy (str_init_static), the EMPTY Str for an empty
 *         source or a declined copy.
 */
Str str_init_4(Str const *const data);

/**
 * @brief Initialize static Str from C-string and size.
 * @param data Pointer to C-string.
 * @param data_size Size of C-string.
 * @return Str by value: an owned copy of the data. Degrades to the empty Str
 *         when the copy cannot be taken (out of memory); a caller that must
 *         tell that from an empty source compares `.size` to `data_size`.
 */
Str str_init_static(char const *const data, USize const data_size);

/**
 * @brief Join an array of Str with a C-string separator.
 * @param parts Array of Str pointers.
 * @param count Number of Str in parts.
 * @param separator Separator placed between parts.
 * @return Str by value: an OWNED heap result, borrowed once and pre-sized, released by
 *         str_uninit. The EMPTY Str for no parts (parts may then be nullptr), for a total of
 *         zero bytes (every part empty AND nothing between them - two empties joined by ","
 *         are ","), or when the total overflows - the WHOLE join refuses, never a truncated one.
 */
Str str_join_1(Str const *const *const parts, USize const count, char const *const separator);

/**
 * @brief Join an array of Str with a separator of given size.
 * @param parts Array of Str pointers.
 * @param count Number of Str in parts.
 * @param separator Separator placed between parts.
 * @param separator_size Size of separator.
 * @return Str by value: see str_join_1.
 */
Str str_join_2(Str const *const *const parts, USize const count, char const *const separator, USize const separator_size);

/**
 * @brief Convert Str to lowercase in place.
 * @param self Pointer to Str. Modified in place.
 */
void str_lower(Str *const self);

/**
 * @brief Move C-string data into Str. Modifies self in place.
 * @param self Pointer to Str. Modified in place.
 * @param data Pointer to C-string pointer. Set to nullptr after move.
 */
void str_move_1(Str *const self, char **const data);

/**
 * @brief Move C-string data of given size into Str. Modifies self in place.
 * @param self Pointer to Str. Modified in place.
 * @param data Pointer to C-string pointer. Set to nullptr after move.
 * @param data_size Size of C-string.
 * @note Self is released first and becomes an OWNER of *data (the adopt constructor, after
 *       str_init_1). Moving self's OWN buffer into itself is REFUSED as a no-op - it would be
 *       released and then adopted dead.
 * @note The buffer must come from the SAME allocator this object was built with - a
 *       bare pointer carries no provenance, so the release at uninit goes through
 *       self->allocator whatever the buffer's real origin was. Moving a heap buffer
 *       into an arena-backed object (or the reverse) is a wrong-allocator release.
 *       The object-taking str_move_3 / string_move_3 / string_move_4 forms do NOT
 *       solve this: `allocator` also records where the STRUCT was borrowed from, so
 *       they cannot adopt the source's without breaking *_delete. They refuse to
 *       cross allocators instead - both objects must share one.
 */
void str_move_2(Str *const self, char **const data, USize const data_size);

/**
 * @brief Move another Str into Str. Modifies self in place.
 *        Ownership TRANSFERS as-is: moving a view yields a view, moving an owner
 *        yields an owner (unlike str_move_2, which always produces an owner).
 * @param self Pointer to Str. Modified in place.
 * @param data Pointer to Str pointer. Set to nullptr after move.
 * @note Only the source's FIELDS are harvested - the source STRUCT itself is never
 *       released (a stack source would be undefined to free, so the API cannot).
 *       Moving from a heap-allocated source (str_new_*) through your only pointer
 *       strands sizeof(Str): keep a second pointer and str_delete it, or move from
 *       stack/value objects. A cross-allocator move is REFUSED as a no-op leaving
 *       both objects untouched. Moving from the EMPTY Str is legal and leaves self EMPTY.
 */
void str_move_3(Str *const self, Str **const data);

/**
 * @brief Allocate new empty Str on heap. Caller must free with str_delete().
 * @return Pointer to new Str.
 */
Str* str_new_1(void);

/**
 * @brief Allocate new Str from C-string. Caller must free with str_delete().
 * @param data Pointer to C-string. WRITABLE: the result is a VIEW (str_init_2).
 * @return Pointer to new Str, a VIEW over data.
 */
Str* str_new_2(char *const data);

/**
 * @brief Allocate new Str from C-string and size. Caller must free with str_delete().
 * @param data Pointer to C-string. WRITABLE: the result is a VIEW (str_init_3).
 * @param data_size Size of C-string.
 * @return Pointer to new Str, a VIEW over data.
 */
Str* str_new_3(char *const data, USize const data_size);

/**
 * @brief Allocate new Str from another Str. Caller must free with str_delete().
 * @param data Pointer to source Str.
 * @return Pointer to new Str holding an OWNED deep copy.
 */
Str* str_new_4(Str const *const data);

/**
 * @brief Allocate new static Str from C-string and size. Caller must free with str_delete().
 * @param data Pointer to C-string.
 * @param data_size Size of C-string.
 * @return Pointer to new Str holding an OWNED copy.
 */
Str* str_new_static(char const *const data, USize const data_size);

/**
 * @brief Print Str with optional logging.
 * @param self Pointer to Str.
 * @param data Label or message to print.
 * @param log If true, print with log metadata.
 */
void str_print(Str const *const self, char const *const data, bool const log);

/**
 * @brief Remove character at index. Modifies self in place: str_erase(index, index + 1), the
 *        same in-place shift, so a view stays a view and must be over writable memory.
 * @param self Pointer to Str. Modified in place.
 * @param index Index of character to remove. At or past the end (the EMPTY Str included) is
 *              REFUSED as a no-op in every build.
 */
void str_remove(Str *const self, USize const index);

/**
 * @brief Repeat Str contents a number of times. Modifies self in place.
 * @param self Pointer to Str. Modified in place.
 * @param count Number of repetitions. A count of 0 yields the EMPTY Str with its
 *              buffer RELEASED (data == nullptr) - unlike string_repeat, which keeps
 *              its allocation at size 0. Both are empty; they differ in what they hold.
 * @note REFUSES with self unchanged, in every build, when size * count overflows or the
 *       arena is refused. Self is PROMOTED to an owner of the repeated copy on success.
 */
void str_repeat(Str *const self, USize const count);

/**
 * @brief Replace data at index. Modifies self in place.
 * @param self Pointer to Str. Modified in place.
 * @param data Pointer to data to copy from.
 * @param index Index at which to replace.
 * @note See str_replace_2 for the refusal (an out-of-range index or size is a no-op).
 */
void str_replace_1(Str *const self, char const *const data, USize const index);

/**
 * @brief Replace data of given size at index. Modifies self in place.
 *        Overwrites [index, index + data_size) within the CURRENT contents; replacing
 *        through the final byte (index + data_size == size) is legal.
 * @param self Pointer to Str. Modified in place.
 * @param data Pointer to data to copy from.
 * @param data_size Size of data to copy.
 * @param index Index at which to replace.
 * @note REFUSES rather than writing out of bounds: when index is at or past the end,
 *       or data_size exceeds size - index, the call is a no-op in every build. index
 *       and data_size routinely arrive from data (a regex match, a parsed offset), so
 *       an out-of-range pair is a runtime condition, not caller error.
 */
void str_replace_2(Str *const self, char const *const data, USize const data_size, USize const index);

/**
 * @brief Replace with another Str at index. Modifies self in place.
 * @param self Pointer to Str. Modified in place.
 * @param data Pointer to source Str.
 * @param index Index at which to replace.
 * @note See str_replace_2 for the refusal (an out-of-range index or size is a no-op).
 */
void str_replace_3(Str *const self, Str const *const data, USize const index);

/**
 * @brief Reverse Str contents in place.
 * @param self Pointer to Str. Modified in place.
 */
void str_reverse(Str *const self);

/**
 * @brief Set Str size. Modifies self in place.
 *        UNCHECKED low-level setter: no bound against the allocation is applied, and a
 *        size past the real buffer makes every subsequent read walk off it. The caller
 *        guarantees size fits the allocation (size + 1 bytes including the terminator).
 * @param self Pointer to Str. Modified in place.
 * @param size New size to set. Must fit the underlying allocation. The one bound that IS
 *             checked: a non-zero size on the EMPTY Str (no buffer at all) is REFUSED.
 */
void str_set_size(Str *const self, USize const size);

/**
 * @brief Create slice of Str from index.
 *        The result is an OWNED copy of the tail (unlike char_slice_*, which borrows).
 * @param self Pointer to Str.
 * @param index Index to start slice. index == size is legal - the tail after the last
 *              delimiter - and yields the EMPTY Str, as does slicing an empty source.
 *              Only an index strictly past the end is caller error (aborts).
 * @return Str by value: an OWNED copy carrying the source's allocator; the EMPTY Str
 *         (allocator carried) for the empty tail or a refused arena.
 */
Str str_slice(Str const *const self, USize const index);

/**
 * @brief Create slice of Str from range.
 *        Copies the INCLUSIVE range [from, to] - both endpoints are kept, so
 *        from == to is the legal single-character slice. (str_erase is exclusive;
 *        the two families differ deliberately and each says so.)
 * @param self Pointer to Str.
 * @param from Start index (inclusive).
 * @param to End index (INCLUSIVE). Must be < size - past it aborts (caller error), and so
 *           does from > to.
 * @return Str by value: an OWNED copy carrying the source's allocator; the EMPTY Str
 *         (allocator carried) on a refused arena.
 */
Str str_slice_range(Str const *const self, USize const from, USize const to);

/**
 * @brief Split Str by delimiter: the FIRST token only (str_split_next iterates the rest).
 * @param self Pointer to Str.
 * @param delimiter Pointer to delimiter string.
 * @return Str by value: an OWNED copy of the first token carrying the source's allocator; the
 *         EMPTY Str (allocator carried) for an empty token, an empty source, or a refused
 *         arena.
 */
Str str_split_1(Str const *const self, char const *const delimiter);

/**
 * @brief Split Str by delimiter of given size.
 * @param self Pointer to Str.
 * @param delimiter Pointer to delimiter string.
 * @param delimiter_size Size of delimiter.
 * @return Str by value: see str_split_1.
 * @note An empty delimiter marks no split point, so the whole self is returned as
 *       one token. Splitting an empty self yields one empty token.
 */
Str str_split_2(Str const *const self, char const *const delimiter, USize const delimiter_size);

/**
 * @brief Split Str by another Str delimiter.
 * @param self Pointer to Str.
 * @param delimiter Pointer to delimiter Str.
 * @return Str by value: see str_split_1.
 */
Str str_split_3(Str const *const self, Str const *const delimiter);

/**
 * @brief Iterate the tokens of Str separated by a delimiter, without allocating.
 * @param self Pointer to Str.
 * @param delimiter Delimiter separating tokens.
 * @param delimiter_size Size of delimiter.
 * @param index Caller-owned iteration cursor. Set to 0 before the first call.
 * @param token_from Out: start index of the token.
 * @param token_size Out: byte length of the token.
 * @return true if a token was produced, false when iteration is complete.
 * @note An empty delimiter marks no split point, so the whole remaining string is
 *       returned as one final token. Splitting an empty self yields exactly one
 *       empty token, then reports iteration complete.
 */
bool str_split_next(Str const *const self, char const *const delimiter, USize const delimiter_size, USize *const index, USize *const token_from, USize *const token_size);

/**
 * @brief Check whether Str starts with a C-string prefix.
 * @param self Pointer to Str.
 * @param data Pointer to prefix C-string.
 * @return true if self starts with data, false otherwise.
 */
bool str_starts_with_1(Str const *const self, char const *const data);

/**
 * @brief Check whether Str starts with a prefix of given size.
 * @param self Pointer to Str.
 * @param data Pointer to prefix.
 * @param data_size Size of prefix.
 * @return true if self starts with data, false otherwise.
 * @note A data_size of 0 matches every Str, the empty Str included: returns true.
 */
bool str_starts_with_2(Str const *const self, char const *const data, USize const data_size);

/**
 * @brief Check whether Str starts with another Str prefix.
 * @param self Pointer to Str.
 * @param data Pointer to prefix Str.
 * @return true if self starts with data, false otherwise.
 */
bool str_starts_with_3(Str const *const self, Str const *const data);

/**
 * @brief Convert Str to a floating-point number.
 * @param self Pointer to Str.
 * @return Parsed floating-point value, or 0 on error.
 */
FSize str_to_numbers_float(Str const *const self);

/**
 * @brief Convert Str to a signed integer.
 * @param self Pointer to Str.
 * @return Parsed signed integer value, or 0 on error.
 */
ISize str_to_numbers_int(Str const *const self);

/**
 * @brief Convert Str to an unsigned integer.
 * @param self Pointer to Str.
 * @return Parsed unsigned integer value, or 0 on error.
 */
USize str_to_numbers_uint(Str const *const self);

/**
 * @brief Trim whitespace in place.
 * @param self Pointer to Str. Modified in place: PROMOTED to an owner of the exact-size
 *        trimmed copy, or left the EMPTY Str when only whitespace remained. An arena-backed
 *        Str routes through str_alloc_trim and keeps its arena.
 */
void str_trim(Str *const self);

/**
 * @brief Release an owned buffer and leave the EMPTY Str. Modifies self in place.
 * @param self Pointer to Str. Modified in place. A view releases nothing; the allocator
 *        field is kept, so the object can be reused. Idempotent.
 */
void str_uninit(Str *const self);

/**
 * @brief Convert Str to uppercase in place.
 * @param self Pointer to Str. Modified in place.
 */
void str_upper(Str *const self);

#endif // CONTAINER_STR_H