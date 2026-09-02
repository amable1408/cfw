/*
 * char.h - Null-terminated char-buffer string utilities for the CFW framework.
 *
 * Features:
 *   - Creation, copying, slicing, splitting, trimming, searching, comparison, repetition,
 *     and numeric <-> string conversion for null-terminated char buffers.
 *   - Optional arena-backed allocation gated by ARENA_IMPLEMENTATION.
 *
 * Naming:
 *   - The _1.._5 suffix is the arity ladder: each rung adds a size parameter, and a rung
 *     that takes a size treats its buffer as possibly unterminated (it never measures it).
 *     The widest rung is the kernel; the narrower ones measure what they lack and forward.
 *   - _fixed_ forms write into a caller-owned buffer of a given capacity; char_alloc_* forms
 *     take an Arena and exist only under ARENA_IMPLEMENTATION; new_ / from_ forms allocate.
 *
 * Usage Examples:
 *   @code
 *   // Create a new string and append data
 *   char *s = char_new_2("hello");
 *   char_add_last_1(&s, " world");
 *
 *   // Take a slice: a borrowed VIEW into s, not a copy. Never freed, and only
 *   // valid while s is.
 *   char *slice = char_slice_1(s, 2); // slice = "llo world"
 *
 *   // Repeat: this one DOES allocate, so it is freed.
 *   char *r = char_repeat_1(s, 3); // r = "hello worldhello worldhello world"
 *   char_delete(r);
 *
 *   // s is freed last: slice pointed into it, and freeing s invalidates slice.
 *   char_delete(s);
 *   @endcode
 *
 * Error Handling:
 *   - Two classes, told apart by WHO got it wrong. A PROGRAMMING error - a nullptr where one
 *     is forbidden, a zero capacity or allocation size (char_new_1's size, char_format's
 *     capacity) - trips error_check, which ABORTS the process in every build that defines
 *     ERROR_CHECK_ENABLED (every shipping build does). Nothing "logs and returns early".
 *   - A DATA-shaped value - an index past the end, a result that does not fit its buffer, a
 *     needle longer than the haystack, a non-digit byte, a source too long to copy - is
 *     REFUSED as real control flow in every build: the function is a no-op with self
 *     unchanged, or answers its documented refusal value ("" for the formatters, nullptr for
 *     a search, false, 0). Each function's @return / @note names its refusal. The one
 *     deliberate nullptr INPUT is char_copy_truncate's source, which yields "". The one
 *     family that does NOT refuse an index is slice / erase / remove: a past-end index there
 *     is caller error and aborts (each @note says so), as is a start index strictly past the
 *     end in char_find_slice_2/5.
 *   - On the HEAP nullptr is never returned: an allocation failure aborts inside memory_alloc.
 *     A nullptr result means a REFUSED ARENA only (a degenerate arena_init_2 leaves a null
 *     handler and every borrow answers null): the char_alloc_* producers refuse to nullptr
 *     and the char_alloc_add_* / remove_* / trim_* mutators leave *self exactly as it was -
 *     never an abort. char_alloc_add_fixed_* leaves self unchanged only on the insert path
 *     that needs a snapshot buffer; its append and empty-target paths allocate nothing and
 *     still succeed.
 *   - Producers of nothing answer an allocated "" (a join of zero parts, a repeat of zero, a
 *     trim of pure whitespace, an empty token), never nullptr - char's analogue of the
 *     String family's EMPTY value.
 *
 * Thread Safety:
 *   - Not thread-safe. Caller must synchronize if used from multiple threads.
 *
 * Memory Management:
 *   - All functions returning char* via char_new_*, char_repeat_*, char_split_*, etc.
 *     allocate new heap memory. Caller must free with char_delete().
 *   - EXCEPTION: char_slice_* returns a borrowed VIEW into the source (pointer arithmetic,
 *     no allocation). Never free it; use char_new_slice_* for an owned copy.
 *   - The in-place writers - char_copy_1/2, char_replace_*, char_fill, char_clear_*,
 *     char_lower/upper/reverse_* - allocate and free nothing, and write exactly what they are
 *     told with NO bound: the caller guarantees the capacity. char_copy_3 is the bounded form.
 *   - The `char **` mutators - char_add_*, char_remove_*, char_trim_* - REALLOCATE: they borrow
 *     a new buffer and release the old one, so *self must be heap-owned (arena-owned for the
 *     char_alloc_* twins), and every pointer into the old buffer - `data` included, when it
 *     aliased - dangles after the call.
 *   - char_delete() is the canonical free function for anything returned by char_new_*,
 *     char_repeat_*, or char_split_* (NOT char_slice_*, which allocates nothing).
 *
 * Performance Characteristics:
 *   - Linear in string length for searches and copies; allocations are O(result size).
 *   - Every char_add_* reallocates to the exact new size, so an accumulation loop over
 *     char_add_last_* is O(n^2) in the total length: build with container/string, whose
 *     capacity grows geometrically, and take the buffer once at the end.
 *   - char_at walks to the terminator on every call, so indexing a string through it in a
 *     loop is quadratic: index the buffer directly there.
 *
 * Dependencies:
 *   - allocator/allocator.h (allocation), memory/memory.h, math/scalar.h (math_pow_u,
 *     math_negate_f), error/error.h and tracelog/tracelog.h (the checks and traces), types.h.
 *
 * See char.c for implementation details.
 */
#ifndef CHAR_CHAR_H
#define CHAR_CHAR_H

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/
#include <errno.h>

#include <allocator/allocator.h>

/*==============================================================================
 * MARK: - Macros and Constants
 *============================================================================*/
#define CHAR_END_CHARACTER 1
#define CHAR_NPOS ((USize) -1)
/** Content size of a string LITERAL or char array, without the terminator. Applied to a
 *  pointer it silently yields sizeof(pointer) - 1: never use it on one. */
#define CHAR_STATIC_SIZE(str) (sizeof(str) - CHAR_END_CHARACTER)

/*==============================================================================
 * MARK: - Arena-backed API
 *============================================================================*/
// Optional, enabled with ARENA_IMPLEMENTATION.
#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Add data to a string at a given index using an arena allocator.
 * @param self Pointer to the string pointer.
 * @param data Data to add.
 * @param self_index Index at which to add data.
 * @param allocator Arena allocator to use.
 * @note Refusals: see char_add_1; a REFUSED arena leaves *self unchanged (Error Handling).
 */
void char_alloc_add_1(char **const self, char const *const data, USize const self_index, Arena *const allocator);

/**
 * @brief Add data of given size to a string at a given index using an arena allocator.
 * @param self Pointer to the string pointer.
 * @param data Data to add.
 * @param data_size Size of data.
 * @param self_index Index at which to add data.
 * @param allocator Arena allocator to use.
 * @note Refusals: see char_add_1; a REFUSED arena leaves *self unchanged (Error Handling).
 */
void char_alloc_add_2(char **const self, char const *const data, USize const data_size, USize const self_index, Arena *const allocator);

/**
 * @brief Add data to a string of given size at a given index using an arena allocator.
 * @param self Pointer to the string pointer.
 * @param self_size Current size of the string.
 * @param data Data to add.
 * @param self_index Index at which to add data.
 * @param allocator Arena allocator to use.
 * @note Refusals: see char_add_1; a REFUSED arena leaves *self unchanged (Error Handling).
 */
void char_alloc_add_3(char **const self, USize const self_size, char const *const data, USize const self_index, Arena *const allocator);

/**
 * @brief Add data of given size to a string of given size at a given index using an arena allocator.
 * @param self Pointer to the string pointer.
 * @param self_size Current size of the string.
 * @param data Data to add.
 * @param data_size Size of data to add.
 * @param self_index Index at which to add data.
 * @param allocator Arena allocator to use.
 * @note Refusals: see char_add_1; a REFUSED arena leaves *self unchanged (Error Handling).
 */
void char_alloc_add_4(char **const self, USize const self_size, char const *const data, USize const data_size, USize const self_index, Arena *const allocator);

/**
 * @brief Add data to the beginning of a string using an arena allocator.
 * @param self Pointer to the string pointer.
 * @param data Data to add.
 * @param allocator Arena allocator to use.
 * @note Refusals: see char_add_1; a REFUSED arena leaves *self unchanged (Error Handling).
 */
void char_alloc_add_first_1(char **const self, char const *const data, Arena *const allocator);

/**
 * @brief Add data of given size to the beginning of a string using an arena allocator.
 * @param self Pointer to the string pointer.
 * @param data Data to add.
 * @param data_size Size of data.
 * @param allocator Arena allocator to use.
 * @note Refusals: see char_add_1; a REFUSED arena leaves *self unchanged (Error Handling).
 */
void char_alloc_add_first_2(char **const self, char const *const data, USize const data_size, Arena *const allocator);

/**
 * @brief Add data to the beginning of a string of given size using an arena allocator.
 * @param self Pointer to the string pointer.
 * @param self_size Current size of the string.
 * @param data Data to add.
 * @param allocator Arena allocator to use.
 * @note Refusals: see char_add_1; a REFUSED arena leaves *self unchanged (Error Handling).
 */
void char_alloc_add_first_3(char **const self, USize const self_size, char const *const data, Arena *const allocator);

/**
 * @brief Add data of given size to the beginning of a string of given size using an arena allocator.
 * @param self Pointer to the string pointer.
 * @param self_size Current size of the string.
 * @param data Data to add.
 * @param data_size Size of data.
 * @param allocator Arena allocator to use.
 * @note Refusals: see char_add_1; a REFUSED arena leaves *self unchanged (Error Handling).
 */
void char_alloc_add_first_4(char **const self, USize const self_size, char const *const data, USize const data_size, Arena *const allocator);

/**
 * @brief Add data to the beginning of a fixed capacity string using an arena allocator.
 * @param self Pointer to the string.
 * @param self_capacity Capacity of the string buffer.
 * @param data Data to add.
 * @param allocator Arena allocator to use.
 * @note Refusals: char_add_fixed_4 (index, overflow, fit) and char_add_fixed_1 (aliasing); a
 *       REFUSED arena leaves self unchanged on the insert path (Error Handling).
 */
void char_alloc_add_first_fixed_1(char *const self, USize const self_capacity, char const *const data, Arena *const allocator);

/**
 * @brief Add data of given size to the beginning of a fixed capacity string using an arena allocator.
 * @param self Pointer to the string.
 * @param self_capacity Capacity of the string buffer.
 * @param data Data to add.
 * @param data_size Size of data to add.
 * @param allocator Arena allocator to use.
 * @note Refusals: char_add_fixed_4 (index, overflow, fit) and char_add_fixed_1 (aliasing); a
 *       REFUSED arena leaves self unchanged on the insert path (Error Handling).
 */
void char_alloc_add_first_fixed_2(char *const self, USize const self_capacity, char const *const data, USize const data_size, Arena *const allocator);

/**
 * @brief Add data to the beginning of a fixed capacity string of given size using an arena allocator.
 * @param self Pointer to the string.
 * @param self_capacity Capacity of the string buffer.
 * @param self_size Current size of the string.
 * @param data Data to add.
 * @param allocator Arena allocator to use.
 * @note Refusals: char_add_fixed_4 (index, overflow, fit) and char_add_fixed_1 (aliasing); a
 *       REFUSED arena leaves self unchanged on the insert path (Error Handling).
 */
void char_alloc_add_first_fixed_3(char *const self, USize const self_capacity, USize const self_size, char const *const data, Arena *const allocator);

/**
 * @brief Add data of given size to the beginning of a fixed capacity string of given size using an arena allocator.
 * @param self Fixed-capacity buffer. Must not be nullptr. Modified in place.
 * @param self_capacity Capacity of the string buffer.
 * @param self_size Size of the string.
 * @param data Data to add.
 * @param data_size Size of data.
 * @param allocator Arena allocator to use.
 * @note Refusals: char_add_fixed_4 (index, overflow, fit) and char_add_fixed_1 (aliasing); a
 *       REFUSED arena leaves self unchanged on the insert path (Error Handling).
 */
void char_alloc_add_first_fixed_4(char *const self, USize const self_capacity, USize const self_size, char const *const data, USize const data_size, Arena *const allocator);

/**
 * @brief Add data to a fixed capacity string at a given index using an arena allocator.
 * @param self Pointer to the string.
 * @param self_capacity Capacity of the string buffer.
 * @param data Data to add.
 * @param self_index Index at which to add data.
 * @param allocator Arena allocator to use.
 * @note Refusals: char_add_fixed_4 (index, overflow, fit) and char_add_fixed_1 (aliasing); a
 *       REFUSED arena leaves self unchanged on the insert path (Error Handling).
 */
void char_alloc_add_fixed_1(char *const self, USize const self_capacity, char const *const data, USize const self_index, Arena *const allocator);

/**
 * @brief Add data of given size to a fixed capacity string at a given index using an arena allocator.
 * @param self Pointer to the string.
 * @param self_capacity Capacity of the string buffer.
 * @param data Data to add.
 * @param data_size Size of data to add.
 * @param self_index Index at which to add data.
 * @param allocator Arena allocator to use.
 * @note Refusals: char_add_fixed_4 (index, overflow, fit) and char_add_fixed_1 (aliasing); a
 *       REFUSED arena leaves self unchanged on the insert path (Error Handling).
 */
void char_alloc_add_fixed_2(char *const self, USize const self_capacity, char const *const data, USize const data_size, USize const self_index, Arena *const allocator);

/**
 * @brief Add data to a fixed capacity string of given size at a given index using an arena allocator.
 * @param self Pointer to the string.
 * @param self_capacity Capacity of the string buffer.
 * @param self_size Current size of the string.
 * @param data Data to add.
 * @param self_index Index at which to add data.
 * @param allocator Arena allocator to use.
 * @note Refusals: char_add_fixed_4 (index, overflow, fit) and char_add_fixed_1 (aliasing); a
 *       REFUSED arena leaves self unchanged on the insert path (Error Handling).
 */
void char_alloc_add_fixed_3(char *const self, USize const self_capacity, USize const self_size, char const *const data, USize const self_index, Arena *const allocator);

/**
 * @brief Add data of given size to a fixed capacity string of given size at a given index using an arena allocator.
 * @param self Fixed-capacity buffer. Must not be nullptr. Modified in place.
 * @param self_capacity Capacity of the string buffer.
 * @param self_size Size of the string.
 * @param data Data to add.
 * @param data_size Size of data.
 * @param self_index Index at which to add data.
 * @param allocator Arena allocator to use.
 * @note Refusals: char_add_fixed_4 (index, overflow, fit) and char_add_fixed_1 (aliasing); a
 *       REFUSED arena leaves self unchanged on the insert path (Error Handling).
 */
void char_alloc_add_fixed_4(char *const self, USize const self_capacity, USize const self_size, char const *const data, USize const data_size, USize const self_index, Arena *const allocator);

/**
 * @brief Add data to the end of a string using an arena allocator.
 * @param self Pointer to the string pointer.
 * @param data Data to add.
 * @param allocator Arena allocator to use.
 * @note Refusals: see char_add_1; a REFUSED arena leaves *self unchanged (Error Handling).
 */
void char_alloc_add_last_1(char **const self, char const *const data, Arena *const allocator);

/**
 * @brief Add data of given size to the end of a string using an arena allocator.
 * @param self Pointer to the string pointer.
 * @param data Data to add.
 * @param data_size Size of data.
 * @param allocator Arena allocator to use.
 * @note Refusals: see char_add_1; a REFUSED arena leaves *self unchanged (Error Handling).
 */
void char_alloc_add_last_2(char **const self, char const *const data, USize const data_size, Arena *const allocator);

/**
 * @brief Add data to the end of a string of given size using an arena allocator.
 * @param self Pointer to the string pointer.
 * @param self_size Current size of the string.
 * @param data Data to add.
 * @param allocator Arena allocator to use.
 * @note Refusals: see char_add_1; a REFUSED arena leaves *self unchanged (Error Handling).
 */
void char_alloc_add_last_3(char **const self, USize const self_size, char const *const data, Arena *const allocator);

/**
 * @brief Add data of given size to the end of a string of given size using an arena allocator.
 * @param self Pointer to the string pointer.
 * @param self_size Current size of the string.
 * @param data Data to add.
 * @param data_size Size of data.
 * @param allocator Arena allocator to use.
 * @note Refusals: see char_add_1; a REFUSED arena leaves *self unchanged (Error Handling).
 */
void char_alloc_add_last_4(char **const self, USize const self_size, char const *const data, USize const data_size, Arena *const allocator);

/**
 * @brief Add data to the end of a fixed capacity string using an arena allocator.
 * @param self Pointer to the string.
 * @param self_capacity Capacity of the string buffer.
 * @param data Data to add.
 * @param allocator Arena allocator to use.
 * @note Refusals: char_add_fixed_4 (index, overflow, fit) and char_add_fixed_1 (aliasing); a
 *       REFUSED arena leaves self unchanged on the insert path (Error Handling).
 */
void char_alloc_add_last_fixed_1(char *const self, USize const self_capacity, char const *const data, Arena *const allocator);

/**
 * @brief Add data of given size to the end of a fixed capacity string using an arena allocator.
 * @param self Pointer to the string.
 * @param self_capacity Capacity of the string buffer.
 * @param data Data to add.
 * @param data_size Size of data to add.
 * @param allocator Arena allocator to use.
 * @note Refusals: char_add_fixed_4 (index, overflow, fit) and char_add_fixed_1 (aliasing); a
 *       REFUSED arena leaves self unchanged on the insert path (Error Handling).
 */
void char_alloc_add_last_fixed_2(char *const self, USize const self_capacity, char const *const data, USize const data_size, Arena *const allocator);

/**
 * @brief Add data to the end of a fixed capacity string of given size using an arena allocator.
 * @param self Pointer to the string.
 * @param self_capacity Capacity of the string buffer.
 * @param self_size Current size of the string.
 * @param data Data to add.
 * @param allocator Arena allocator to use.
 * @note Refusals: char_add_fixed_4 (index, overflow, fit) and char_add_fixed_1 (aliasing); a
 *       REFUSED arena leaves self unchanged on the insert path (Error Handling).
 */
void char_alloc_add_last_fixed_3(char *const self, USize const self_capacity, USize const self_size, char const *const data, Arena *const allocator);

/**
 * @brief Add data of given size to the end of a fixed capacity string of given size using an arena allocator.
 * @param self Fixed-capacity buffer. Must not be nullptr. Modified in place.
 * @param self_capacity Capacity of the string buffer.
 * @param self_size Size of the string.
 * @param data Data to add.
 * @param data_size Size of data.
 * @param allocator Arena allocator to use.
 * @note Refusals: char_add_fixed_4 (index, overflow, fit) and char_add_fixed_1 (aliasing); a
 *       REFUSED arena leaves self unchanged on the insert path (Error Handling).
 */
void char_alloc_add_last_fixed_4(char *const self, USize const self_capacity, USize const self_size, char const *const data, USize const data_size, Arena *const allocator);

/**
 * @brief Delete a string using an arena allocator.
 * @param self String to delete.
 * @param allocator Arena allocator to use.
 */
void char_alloc_delete(char *const self, Arena *const allocator);

/**
 * @brief Create a string from an integer using an arena allocator.
 * @param number Integer value.
 * @param allocator Arena allocator to use.
 * @return Newly allocated string, or nullptr on a refused arena.
 * @note Cannot refuse for size: the buffer is allocated to fit, unlike the fixed-buffer
 *       char_from_numbers_* whose "" means refused.
 */
char* char_alloc_from_numbers_int_1(ISize const number, Arena *const allocator);

/**
 * @brief Create a string from an integer with padding using an arena allocator.
 * @param number Integer value.
 * @param padding Number of extra '0' characters prepended to the digits (additive,
 *                not a minimum width).
 * @param allocator Arena allocator to use.
 * @return Newly allocated string, or nullptr on a refused arena.
 * @note Cannot refuse for size: the buffer is allocated to fit, unlike the fixed-buffer
 *       char_from_numbers_* whose "" means refused.
 */
char* char_alloc_from_numbers_int_2(ISize const number, U8 const padding, Arena *const allocator);

/**
 * @brief Create a string from an unsigned integer using an arena allocator.
 * @param number Unsigned integer value.
 * @param allocator Arena allocator to use.
 * @return Newly allocated string, or nullptr on a refused arena.
 * @note Cannot refuse for size: the buffer is allocated to fit, unlike the fixed-buffer
 *       char_from_numbers_* whose "" means refused.
 */
char* char_alloc_from_numbers_uint_1(USize const number, Arena *const allocator);

/**
 * @brief Create a string from an unsigned integer with padding using an arena allocator.
 * @param number Unsigned integer value.
 * @param padding Number of extra '0' characters prepended to the digits (additive,
 *                not a minimum width).
 * @param allocator Arena allocator to use.
 * @return Newly allocated string, or nullptr on a refused arena.
 * @note Cannot refuse for size: the buffer is allocated to fit, unlike the fixed-buffer
 *       char_from_numbers_* whose "" means refused.
 */
char* char_alloc_from_numbers_uint_2(USize const number, U8 const padding, Arena *const allocator);

/**
 * @brief Create an arena-backed trimmed copy of a null-terminated string.
 * @param self Source string.
 * @param allocator Arena allocator to use.
 * @return Newly allocated trimmed string, or nullptr on a refused arena.
 */
char* char_alloc_from_trim_1(char const *const self, Arena *const allocator);

/**
 * @brief Create an arena-backed trimmed copy of a string of given size.
 * @param self Source string.
 * @param self_size Size of source string.
 * @param allocator Arena allocator to use.
 * @return Newly allocated trimmed string, or nullptr when the arena was refused (the
 *         string family degrades on it: string_alloc_from_trim yields the EMPTY String).
 */
char* char_alloc_from_trim_2(char const *const self, USize const self_size, Arena *const allocator);

/**
 * @brief Join an array of null-terminated strings with a separator using an arena allocator.
 * @param parts Array of string pointers. Must not be nullptr; no element may be nullptr.
 * @param count Number of strings in parts. Zero yields "".
 * @param separator Separator placed between parts. Must not be nullptr (may be empty).
 * @param allocator Arena allocator to use.
 * @return Pointer to the new joined string, or nullptr on a refused arena.
 */
char* char_alloc_join_1(char const *const *const parts, USize const count, char const *const separator, Arena *const allocator);

/**
 * @brief Join an array of strings with a separator of given size using an arena allocator.
 * @param parts Array of string pointers. Must not be nullptr; no element may be nullptr.
 * @param count Number of strings in parts. Zero yields "".
 * @param separator Separator placed between parts. Must not be nullptr (may be empty).
 * @param separator_size Number of bytes of separator.
 * @param allocator Arena allocator to use.
 * @return Pointer to the new joined string, or nullptr on a refused arena.
 */
char* char_alloc_join_2(char const *const *const parts, USize const count, char const *const separator, USize const separator_size, Arena *const allocator);

/**
 * @brief Move a string pointer using an arena allocator.
 * @param self Pointer to the string pointer.
 * @param data Pointer to the data pointer.
 * @param allocator Arena allocator to use.
 */
void char_alloc_move(char **const self, char **const data, Arena *const allocator);

/**
 * @brief Create a new string of given size using an arena allocator.
 * @param size Size of the string. Must be non-zero; a zero size is a programming
 *        error and aborts.
 * @param allocator Arena allocator to use.
 * @return Newly allocated string, or nullptr on a refused arena.
 */
char* char_alloc_new_1(USize const size, Arena *const allocator);

/**
 * @brief Create a new string from data using an arena allocator.
 * @param data Data to copy.
 * @param allocator Arena allocator to use.
 * @return Newly allocated string, or nullptr on a refused arena.
 */
char* char_alloc_new_2(char const *const data, Arena *const allocator);

/**
 * @brief Create a new string from data of given size using an arena allocator.
 * @param data Data to copy.
 * @param data_size Size of data.
 * @param allocator Arena allocator to use.
 * @return Newly allocated string, or nullptr on a refused arena.
 */
char* char_alloc_new_3(char const *const data, USize const data_size, Arena *const allocator);

/**
 * @brief Create a new string with every occurrence of find replaced by replace, using an arena allocator.
 * @param self Source string. Must not be nullptr.
 * @param find Substring to replace. Must not be nullptr.
 * @param replace Replacement substring. Must not be nullptr (may be empty).
 * @param allocator Arena allocator to use.
 * @return Pointer to the new string, or nullptr on a refused arena.
 * @note An empty find matches nothing, so the result is a verbatim copy of self.
 */
char* char_alloc_new_replace_1(char const *const self, char const *const find, char const *const replace, Arena *const allocator);

/**
 * @brief Create a new string with every occurrence of find (of given size) replaced by replace, using an arena allocator.
 * @param self Source string. Must not be nullptr.
 * @param self_size Size of the source string.
 * @param find Substring to replace. Must not be nullptr.
 * @param find_size Number of bytes of find.
 * @param replace Replacement substring. Must not be nullptr (may be empty).
 * @param replace_size Number of bytes of replace.
 * @param allocator Arena allocator to use.
 * @return Pointer to the new string, or nullptr on a refused arena.
 * @note A find_size of 0 matches nothing, so the result is a verbatim copy of self.
 */
char* char_alloc_new_replace_2(char const *const self,
    USize const self_size, char const *const find, USize const find_size, char const *const replace, USize const replace_size, Arena *const allocator);

/**
 * @brief Create a new string slice from a given index using an arena allocator.
 * @param self Source string.
 * @param self_index Index to start slice.
 * @param allocator Arena allocator to use.
 * @return Newly allocated string slice, or nullptr on a refused arena.
 */
char* char_alloc_new_slice_1(char const *const self, USize const self_index, Arena *const allocator);

/**
 * @brief Create a new string slice from a given index and size using an arena allocator.
 * @param self Source string.
 * @param self_size Size of source string.
 * @param self_index Index to start slice.
 * @param allocator Arena allocator to use.
 * @return Newly allocated string slice, or nullptr on a refused arena.
 */
char* char_alloc_new_slice_2(char const *const self, USize const self_size, USize const self_index, Arena *const allocator);

/**
 * @brief Create a new string slice from a range using an arena allocator.
 * @param self Source string.
 * @param self_from Start index.
 * @param self_to End index.
 * @param allocator Arena allocator to use.
 * @return Newly allocated string slice, or nullptr on a refused arena.
 */
char* char_alloc_new_slice_range_1(char const *const self, USize const self_from, USize const self_to, Arena *const allocator);

/**
 * @brief Create a new string slice from a range and size using an arena allocator.
 * @param self Source string.
 * @param self_size Size of source string.
 * @param self_from Start index.
 * @param self_to End index.
 * @param allocator Arena allocator to use.
 * @return Newly allocated string slice, or nullptr on a refused arena.
 */
char* char_alloc_new_slice_range_2(char const *const self, USize const self_size, USize const self_from, USize const self_to, Arena *const allocator);

/**
 * @brief Remove a character at an index from a string using an arena allocator.
 * @param self Pointer to the string pointer.
 * @param index Index to remove.
 * @param allocator Arena allocator to use.
 */
void char_alloc_remove_1(char **const self, USize const index, Arena *const allocator);

/**
 * @brief Remove a character at an index from a string of given size using an arena allocator.
 * @param self Pointer to the string pointer.
 * @param self_size Size of string.
 * @param index Index to remove.
 * @param allocator Arena allocator to use.
 */
void char_alloc_remove_2(char **const self, USize const self_size, USize const index, Arena *const allocator);

/**
 * @brief Repeat a string a given number of times using an arena allocator.
 * @param data String to repeat.
 * @param count Number of repetitions.
 * @param allocator Arena allocator to use.
 * @return Newly allocated repeated string, or nullptr on a refused arena.
 */
char* char_alloc_repeat_1(char const *const data, USize const count, Arena *const allocator);

/**
 * @brief Repeat a string of given size a given number of times using an arena allocator.
 * @param data String to repeat.
 * @param data_size Size of string.
 * @param count Number of repetitions.
 * @param allocator Arena allocator to use.
 * @return Newly allocated repeated string, or nullptr on a refused arena.
 */
char* char_alloc_repeat_2(char const *const data, USize const data_size, USize const count, Arena *const allocator);

/**
 * @brief Split a string by a delimiter using an arena allocator.
 * @param self Source string.
 * @param delimiter Delimiter string.
 * @param allocator Arena allocator to use.
 * @return Newly allocated copy of the FIRST token only - the bytes before the first
 *         delimiter, or the whole string when the delimiter is absent. This does not
 *         return a list; use char_split_next to walk the remaining tokens, or nullptr on a refused arena.
 */
char* char_alloc_split_1(char const *const self, char const *const delimiter, Arena *const allocator);

/**
 * @brief Split a string of given size by a delimiter using an arena allocator.
 * @param self Source string.
 * @param self_size Size of source string.
 * @param delimiter Delimiter string.
 * @param allocator Arena allocator to use.
 * @return Newly allocated copy of the FIRST token only - the bytes before the first
 *         delimiter, or the whole string when the delimiter is absent. This does not
 *         return a list; use char_split_next to walk the remaining tokens, or nullptr on a refused arena.
 */
char* char_alloc_split_2(char const *const self, USize const self_size, char const *const delimiter, Arena *const allocator);

/**
 * @brief Split a string of given size by a delimiter of given size using an arena allocator.
 * @param self Source string.
 * @param self_size Size of source string.
 * @param delimiter Delimiter string.
 * @param delimiter_size Size of delimiter.
 * @param allocator Arena allocator to use.
 * @return Newly allocated copy of the FIRST token only - the bytes before the first
 *         delimiter, or the whole string when the delimiter is absent. This does not
 *         return a list; use char_split_next to walk the remaining tokens, or nullptr on a refused arena.
 */
char* char_alloc_split_3(char const *const self, USize const self_size, char const *const delimiter, USize const delimiter_size, Arena *const allocator);

/**
 * @brief Trim whitespace by replacing an arena-backed string.
 * @param self Pointer to the string pointer.
 * @param allocator Arena allocator to use.
 */
void char_alloc_trim_1(char **const self, Arena *const allocator);

/**
 * @brief Trim whitespace by replacing an arena-backed string of given size.
 * @param self Pointer to the string pointer.
 * @param self_size Size of source string.
 * @param allocator Arena allocator to use.
 */
void char_alloc_trim_2(char **const self, USize const self_size, Arena *const allocator);

#endif // ARENA_IMPLEMENTATION

/*==============================================================================
 * MARK: - Standard API
 *============================================================================*/
/**
 * @brief Add data to a string at a given index.
 * @param self Pointer to the string pointer. Must not be nullptr. Modified in place.
 * @param data Pointer to the data to add. Must not be nullptr. Not modified.
 * @param self_index Index in the string where data will be inserted. Must be <= string length.
 * @note A nullptr self or data ABORTS (caller error). An index past the end, a size that overflows,
 *       or - in the fixed forms - a result that does not fit self_capacity is REFUSED as a no-op
 *       with self unchanged, in every build.
 * @note data may point inside *self at any index: the new buffer is built before the old one
 *       is released. The old buffer - data included - is gone after the call.
 */
void char_add_1(char **const self, char const *const data, USize const self_index);

/**
 * @brief Add data of given size to a string at a given index.
 * @param self Pointer to the string pointer. Must not be nullptr. Modified in place.
 * @param data Pointer to the data to add. Must not be nullptr. Not modified.
 * @param data_size Number of bytes from data to add. Must be <= strlen(data).
 * @param self_index Index in the string where data will be inserted. Must be <= string length.
 * @note A nullptr self or data ABORTS (caller error). An index past the end, a size that overflows,
 *       or - in the fixed forms - a result that does not fit self_capacity is REFUSED as a no-op
 *       with self unchanged, in every build.
 * @note data may point inside *self at any index: the new buffer is built before the old one
 *       is released. The old buffer - data included - is gone after the call.
 */
void char_add_2(char **const self, char const *const data, USize const data_size, USize const self_index);

/**
 * @brief Add data to a string of given size at a given index.
 * @param self Pointer to the string pointer. Must not be nullptr. Modified in place.
 * @param self_size Size of source string. Must be <= strlen(self).
 * @param data Pointer to the data to add. Must not be nullptr. Not modified.
 * @param self_index Index in the string where data will be inserted. Must be <= string length.
 * @note A nullptr self or data ABORTS (caller error). An index past the end, a size that overflows,
 *       or - in the fixed forms - a result that does not fit self_capacity is REFUSED as a no-op
 *       with self unchanged, in every build.
 * @note data may point inside *self at any index: the new buffer is built before the old one
 *       is released. The old buffer - data included - is gone after the call.
 */
void char_add_3(char **const self, USize const self_size, char const *const data, USize const self_index);

/**
 * @brief Add data of given size to a string of given size at a given index.
 * @param self Pointer to the string pointer. Must not be nullptr. Modified in place.
 * @param self_size Size of source string. Must be <= strlen(self).
 * @param data Pointer to the data to add. Must not be nullptr. Not modified.
 * @param data_size Number of bytes from data to add. Must be <= strlen(data).
 * @param self_index Index in the string where data will be inserted. Must be <= string length.
 * @note A nullptr self or data ABORTS (caller error). An index past the end, a size that overflows,
 *       or - in the fixed forms - a result that does not fit self_capacity is REFUSED as a no-op
 *       with self unchanged, in every build.
 * @note data may point inside *self at any index: the new buffer is built before the old one
 *       is released. The old buffer - data included - is gone after the call.
 */
void char_add_4(char **const self, USize const self_size, char const *const data, USize const data_size, USize const self_index);

/**
 * @brief Add data to the beginning of a string.
 * @param self Pointer to the string pointer. Must not be nullptr. Modified in place.
 * @param data Pointer to the data to add. Must not be nullptr. Not modified.
 * @note Refusals: see char_add_1.
 */
void char_add_first_1(char **const self, char const *const data);

/**
 * @brief Add data of given size to the beginning of a string.
 * @param self Pointer to the string pointer. Must not be nullptr. Modified in place.
 * @param data Pointer to the data to add. Must not be nullptr. Not modified.
 * @param data_size Number of bytes from data to add. Must be <= strlen(data).
 * @note Refusals: see char_add_1.
 */
void char_add_first_2(char **const self, char const *const data, USize const data_size);

/**
 * @brief Add data to the beginning of a string of given size.
 * @param self Pointer to the string pointer. Must not be nullptr. Modified in place.
 * @param self_size Size of source string. Must be <= strlen(self).
 * @param data Pointer to the data to add. Must not be nullptr. Not modified.
 * @note Refusals: see char_add_1.
 */
void char_add_first_3(char **const self, USize const self_size, char const *const data);

/**
 * @brief Add data of given size to the beginning of a string of given size.
 * @param self Pointer to the string pointer. Must not be nullptr. Modified in place.
 * @param self_size Size of source string. Must be <= strlen(self).
 * @param data Pointer to the data to add. Must not be nullptr. Not modified.
 * @param data_size Number of bytes from data to add. Must be <= strlen(data).
 * @note Refusals: see char_add_1.
 */
void char_add_first_4(char **const self, USize const self_size, char const *const data, USize const data_size);

/**
 * @brief Add data to the beginning of a fixed capacity string.
 * @param self Fixed-capacity buffer. Must not be nullptr. Modified in place.
 * @param self_capacity Capacity of the string buffer.
 * @param data Pointer to the data to add. Must not be nullptr.
 * @note Refusals (a no-op, self unchanged, in every build): see char_add_fixed_4 for the
 *       index, overflow and fit rules, char_add_fixed_1 for the aliasing rule.
 */
void char_add_first_fixed_1(char *const self, USize const self_capacity, char const *const data);

/**
 * @brief Add data of given size to the beginning of a fixed capacity string.
 * @param self Fixed-capacity buffer. Must not be nullptr. Modified in place.
 * @param self_capacity Capacity of the string buffer.
 * @param data Pointer to the data to add. Must not be nullptr.
 * @param data_size Size of data to add.
 * @note Refusals (a no-op, self unchanged, in every build): see char_add_fixed_4 for the
 *       index, overflow and fit rules, char_add_fixed_1 for the aliasing rule.
 */
void char_add_first_fixed_2(char *const self, USize const self_capacity, char const *const data, USize const data_size);

/**
 * @brief Add data to the beginning of a fixed capacity string of given size.
 * @param self Pointer to the string pointer. Must not be nullptr. Modified in place.
 * @param self_capacity Capacity of the string buffer.
 * @param self_size Size of the string.
 * @param data Pointer to the data to add. Must not be nullptr. Not modified.
 * @note Refusals (a no-op, self unchanged, in every build): see char_add_fixed_4 for the
 *       index, overflow and fit rules, char_add_fixed_1 for the aliasing rule.
 */
void char_add_first_fixed_3(char *const self, USize const self_capacity, USize const self_size, char const *const data);

/**
 * @brief Add data of given size to the beginning of a fixed capacity string of given size.
 * @param self Pointer to the string pointer. Must not be nullptr. Modified in place.
 * @param self_capacity Capacity of the string buffer.
 * @param self_size Size of the string.
 * @param data Pointer to the data to add. Must not be nullptr. Not modified.
 * @param data_size Number of bytes from data to add. Must be <= strlen(data).
 * @note Refusals (a no-op, self unchanged, in every build): see char_add_fixed_4 for the
 *       index, overflow and fit rules, char_add_fixed_1 for the aliasing rule.
 */
void char_add_first_fixed_4(char *const self, USize const self_capacity, USize const self_size, char const *const data, USize const data_size);

/**
 * @brief Add data to a fixed capacity string at a given index.
 * @param self Fixed-capacity buffer. Must not be nullptr. Modified in place.
 * @param self_capacity Capacity of the string buffer.
 * @param data Pointer to the data to add. Must not be nullptr.
 * @param self_index Index where data will be inserted.
 * @note REFUSES as a no-op: a data_size of 0, and a `data` inside self's own buffer that
 *       is not wholly inside the live bytes [0, self_size) - one that runs past self_size,
 *       or sits in the spare capacity. A `data` wholly inside the live bytes is legal at
 *       any index: the insert path reads it from a snapshot, and the append and
 *       empty-target paths are disjoint from it and need none. These four take no arena,
 *       so the snapshot is a heap borrow that never declines - a failed allocation ABORTS
 *       inside memory_alloc; the arena twins char_alloc_add_fixed_* instead leave self
 *       unchanged when a refused arena answers null to the snapshot borrow.
 * @note Refusals (a no-op, self unchanged, in every build): see char_add_fixed_4 for the
 *       index, overflow and fit rules, char_add_fixed_1 for the aliasing rule.
 */
void char_add_fixed_1(char *const self, USize const self_capacity, char const *const data, USize const self_index);

/**
 * @brief Add data of given size to a fixed capacity string at a given index.
 * @param self Fixed-capacity buffer. Must not be nullptr. Modified in place.
 * @param self_capacity Capacity of the string buffer.
 * @param data Pointer to the data to add. Must not be nullptr.
 * @param data_size Size of data to add.
 * @param self_index Index where data will be inserted.
 * @note REFUSES as a no-op: a data_size of 0, and a `data` inside self's own buffer that
 *       is not wholly inside the live bytes [0, self_size) - one that runs past self_size,
 *       or sits in the spare capacity. A `data` wholly inside the live bytes is legal at
 *       any index: the insert path reads it from a snapshot, and the append and
 *       empty-target paths are disjoint from it and need none. These four take no arena,
 *       so the snapshot is a heap borrow that never declines - a failed allocation ABORTS
 *       inside memory_alloc; the arena twins char_alloc_add_fixed_* instead leave self
 *       unchanged when a refused arena answers null to the snapshot borrow.
 * @note Refusals (a no-op, self unchanged, in every build): see char_add_fixed_4 for the
 *       index, overflow and fit rules, char_add_fixed_1 for the aliasing rule.
 */
void char_add_fixed_2(char *const self, USize const self_capacity, char const *const data, USize const data_size, USize const self_index);

/**
 * @brief Add data to a fixed capacity string of given size at a given index.
 * @param self Fixed-capacity buffer. Must not be nullptr. Modified in place.
 * @param self_capacity Capacity of the string buffer.
 * @param self_size Current size of the string.
 * @param data Pointer to the data to add. Must not be nullptr.
 * @param self_index Index where data will be inserted.
 * @note REFUSES as a no-op: a data_size of 0, and a `data` inside self's own buffer that
 *       is not wholly inside the live bytes [0, self_size) - one that runs past self_size,
 *       or sits in the spare capacity. A `data` wholly inside the live bytes is legal at
 *       any index: the insert path reads it from a snapshot, and the append and
 *       empty-target paths are disjoint from it and need none. These four take no arena,
 *       so the snapshot is a heap borrow that never declines - a failed allocation ABORTS
 *       inside memory_alloc; the arena twins char_alloc_add_fixed_* instead leave self
 *       unchanged when a refused arena answers null to the snapshot borrow.
 * @note Refusals (a no-op, self unchanged, in every build): see char_add_fixed_4 for the
 *       index, overflow and fit rules, char_add_fixed_1 for the aliasing rule.
 */
void char_add_fixed_3(char *const self, USize const self_capacity, USize const self_size, char const *const data, USize const self_index);

/**
 * @brief Add data of given size to a fixed capacity string of given size at a given index.
 * @param self Pointer to the string pointer. Must not be nullptr. Modified in place.
 * @param self_capacity Capacity of the string buffer.
 * @param self_size Size of the string.
 * @param data Pointer to the data to add. Must not be nullptr. Not modified.
 * @param data_size Number of bytes from data to add. Must be <= strlen(data).
 * @param self_index Index in the string where data will be inserted. Must be <= string length.
 * @note A nullptr self or data ABORTS (caller error). An index past the end, a size that overflows,
 *       or - in the fixed forms - a result that does not fit self_capacity is REFUSED as a no-op
 *       with self unchanged, in every build.
 * @note REFUSES as a no-op: a data_size of 0, and a `data` inside self's own buffer that
 *       is not wholly inside the live bytes [0, self_size) - one that runs past self_size,
 *       or sits in the spare capacity. A `data` wholly inside the live bytes is legal at
 *       any index: the insert path reads it from a snapshot, and the append and
 *       empty-target paths are disjoint from it and need none. These four take no arena,
 *       so the snapshot is a heap borrow that never declines - a failed allocation ABORTS
 *       inside memory_alloc; the arena twins char_alloc_add_fixed_* instead leave self
 *       unchanged when a refused arena answers null to the snapshot borrow.
 */
void char_add_fixed_4(char *const self, USize const self_capacity, USize const self_size, char const *const data, USize const data_size, USize const self_index);

/**
 * @brief Add data to the end of a string.
 * @param self Pointer to the string pointer. Must not be nullptr. Modified in place.
 * @param data Pointer to the data to add. Must not be nullptr. Not modified.
 * @note Refusals: see char_add_1.
 */
void char_add_last_1(char **const self, char const *const data);

/**
 * @brief Add data of given size to the end of a string.
 * @param self Pointer to the string pointer. Must not be nullptr. Modified in place.
 * @param data Pointer to the data to add. Must not be nullptr. Not modified.
 * @param data_size Number of bytes from data to add. Must be <= strlen(data).
 * @note Refusals: see char_add_1.
 */
void char_add_last_2(char **const self, char const *const data, USize const data_size);

/**
 * @brief Add data to the end of a string of given size.
 * @param self Pointer to the string pointer. Must not be nullptr. Modified in place.
 * @param self_size Size of source string. Must be <= strlen(self).
 * @param data Pointer to the data to add. Must not be nullptr. Not modified.
 * @note Refusals: see char_add_1.
 */
void char_add_last_3(char **const self, USize const self_size, char const *const data);

/**
 * @brief Add data of given size to the end of a string of given size.
 * @param self Pointer to the string pointer. Must not be nullptr. Modified in place.
 * @param self_size Size of source string. Must be <= strlen(self).
 * @param data Pointer to the data to add. Must not be nullptr. Not modified.
 * @param data_size Number of bytes from data to add. Must be <= strlen(data).
 * @note Refusals: see char_add_1.
 */
void char_add_last_4(char **const self, USize const self_size, char const *const data, USize const data_size);

/**
 * @brief Add data to the end of a fixed capacity string.
 * @param self Fixed-capacity buffer. Must not be nullptr. Modified in place.
 * @param self_capacity Capacity of the string buffer.
 * @param data Pointer to the data to add. Must not be nullptr.
 * @note Refusals (a no-op, self unchanged, in every build): see char_add_fixed_4 for the
 *       index, overflow and fit rules, char_add_fixed_1 for the aliasing rule.
 */
void char_add_last_fixed_1(char *const self, USize const self_capacity, char const *const data);

/**
 * @brief Add data of given size to the end of a fixed capacity string.
 * @param self Fixed-capacity buffer. Must not be nullptr. Modified in place.
 * @param self_capacity Capacity of the string buffer.
 * @param data Pointer to the data to add. Must not be nullptr.
 * @param data_size Size of data to add.
 * @note Refusals (a no-op, self unchanged, in every build): see char_add_fixed_4 for the
 *       index, overflow and fit rules, char_add_fixed_1 for the aliasing rule.
 */
void char_add_last_fixed_2(char *const self, USize const self_capacity, char const *const data, USize const data_size);

/**
 * @brief Add data to the end of a fixed capacity string of given size.
 * @param self Pointer to the string pointer. Must not be nullptr. Modified in place.
 * @param self_capacity Capacity of the string buffer.
 * @param self_size Size of the string.
 * @param data Pointer to the data to add. Must not be nullptr. Not modified.
 * @note Refusals (a no-op, self unchanged, in every build): see char_add_fixed_4 for the
 *       index, overflow and fit rules, char_add_fixed_1 for the aliasing rule.
 */
void char_add_last_fixed_3(char *const self, USize const self_capacity, USize const self_size, char const *const data);

/**
 * @brief Add data of given size to the end of a fixed capacity string of given size.
 * @param self Pointer to the string pointer. Must not be nullptr. Modified in place.
 * @param self_capacity Capacity of the string buffer.
 * @param self_size Size of the string.
 * @param data Pointer to the data to add. Must not be nullptr. Not modified.
 * @param data_size Number of bytes from data to add. Must be <= strlen(data).
 * @note Refusals (a no-op, self unchanged, in every build): see char_add_fixed_4 for the
 *       index, overflow and fit rules, char_add_fixed_1 for the aliasing rule.
 */
void char_add_last_fixed_4(char *const self, USize const self_capacity, USize const self_size, char const *const data, USize const data_size);

/**
 * @brief Get the character at a given index.
 * @param self Pointer to the string. Must not be nullptr.
 * @param index Index of the character to retrieve. An index at or past the end is
 *              legal and answers '\0' - a scan running off a short string asks it
 *              routinely.
 * @return The character at the specified index, or '\0' when index is at or past
 *         the end of the string.
 */
char char_at(char const *const self, USize const index);

/**
 * @brief Check if a character is a numeric digit ('0'-'9').
 * @param c Character to check.
 * @return true if c is a digit, false otherwise.
 */
bool char_check_number(char const c);

/**
 * @brief Clear a string: zero-fills every byte up to the terminator, not just the first.
 * @param self Pointer to the string. Must not be nullptr. Modified in place.
 */
void char_clear_1(char *const self);

/**
 * @brief Clear a string with a given size (set all bytes to '\0').
 * @param self Pointer to the string. Must not be nullptr. Modified in place.
 * @param self_size Number of bytes to clear.
 */
void char_clear_2(char *const self, USize const self_size);

/**
 * @brief Compare two null-terminated strings for equality.
 * @param char_1 First string. Must not be nullptr.
 * @param char_2 Second string. Must not be nullptr.
 * @return true if both strings are equal, false otherwise.
 */
bool char_compare_equal_1(char const *const char_1, char const *const char_2);

/**
 * @brief Compare two strings for equality, up to given sizes.
 * @param char_1 First string. Must not be nullptr.
 * @param char_1_size Number of bytes to compare from char_1.
 * @param char_2 Second string. Must not be nullptr.
 * @param char_2_size Number of bytes to compare from char_2.
 * @return true if both substrings are equal, false otherwise.
 */
bool char_compare_equal_2(char const *const char_1, USize const char_1_size, char const *const char_2, USize const char_2_size);

/**
 * @brief Timing-safe (constant-time) equality of two null-terminated strings.
 *        Folds every byte with no early exit, so the runtime does not reveal how
 *        many bytes matched — use it to compare secrets (tokens, MACs, hashes).
 *        Lengths are compared first and are not treated as secret.
 * @param char_1 First string. Must not be nullptr.
 * @param char_2 Second string. Must not be nullptr.
 * @return true if both strings are equal, false otherwise.
 */
bool char_compare_equal_comptime_1(char const *const char_1, char const *const char_2);

/**
 * @brief Timing-safe (constant-time) equality of two strings up to given sizes.
 *        Folds every byte with no early exit; use it to compare secrets.
 * @param char_1 First string. Must not be nullptr.
 * @param char_1_size Number of bytes to compare from char_1.
 * @param char_2 Second string. Must not be nullptr.
 * @param char_2_size Number of bytes to compare from char_2.
 * @return true if both substrings are equal, false otherwise.
 */
bool char_compare_equal_comptime_2(char const *const char_1, USize const char_1_size, char const *const char_2, USize const char_2_size);

/**
 * @brief Compare two null-terminated strings for ASCII case-insensitive equality.
 * @param char_1 First string. Must not be nullptr.
 * @param char_2 Second string. Must not be nullptr.
 * @return true if equal ignoring ASCII case, false otherwise.
 */
bool char_compare_iequal_1(char const *const char_1, char const *const char_2);

/**
 * @brief Compare two strings of given sizes for ASCII case-insensitive equality.
 * @param char_1 First string. Must not be nullptr.
 * @param char_1_size Number of bytes to compare from char_1.
 * @param char_2 Second string. Must not be nullptr.
 * @param char_2_size Number of bytes to compare from char_2.
 * @return true if equal ignoring ASCII case, false otherwise.
 */
bool char_compare_iequal_2(char const *const char_1, USize const char_1_size, char const *const char_2, USize const char_2_size);

/**
 * @brief Timing-safe ASCII case-insensitive equality of two null-terminated strings.
 *        Folds every byte with no early exit; use it to compare secrets.
 *        Lengths are compared first and are not treated as secret.
 * @param char_1 First string. Must not be nullptr.
 * @param char_2 Second string. Must not be nullptr.
 * @return true if equal ignoring ASCII case, false otherwise.
 */
bool char_compare_iequal_comptime_1(char const *const char_1, char const *const char_2);

/**
 * @brief Timing-safe ASCII case-insensitive equality of two strings of given sizes.
 *        Folds every byte with no early exit; use it to compare secrets.
 * @param char_1 First string. Must not be nullptr.
 * @param char_1_size Number of bytes to compare from char_1.
 * @param char_2 Second string. Must not be nullptr.
 * @param char_2_size Number of bytes to compare from char_2.
 * @return true if equal ignoring ASCII case, false otherwise.
 */
bool char_compare_iequal_comptime_2(char const *const char_1, USize const char_1_size, char const *const char_2, USize const char_2_size);

/**
 * @brief Check whether data occurs anywhere in a string.
 * @param self Pointer to the string. Must not be nullptr.
 * @param data Data to search for. Must not be nullptr.
 * @return true if data occurs in self, false otherwise.
 * @note An empty data occurs inside every string, the empty string included: returns true.
 */
bool char_contains_1(char const *const self, char const *const data);

/**
 * @brief Check whether data of given size occurs in a string of given size.
 * @param self Pointer to the string. Must not be nullptr.
 * @param self_size Size of the string.
 * @param data Data to search for. Must not be nullptr.
 * @param data_size Number of bytes of data.
 * @return true if data occurs in self, false otherwise.
 * @note A data_size of 0 occurs inside every string, the empty string included: returns true.
 */
bool char_contains_2(char const *const self, USize const self_size, char const *const data, USize const data_size);

/**
 * @brief Copy a null-terminated string into another.
 * @param self Destination string. Must not be nullptr. Modified in place.
 * @param data Source string. Must not be nullptr.
 * @note Writes strlen(data) bytes and NO terminator (char_copy_2's rule): the destination keeps
 *       whatever byte followed. Unbounded - the caller guarantees the capacity. char_copy_3 is
 *       the bounded, terminating form.
 */
void char_copy_1(char *const self, char const *const data);

/**
 * @brief Copy data of given size into a string.
 * @param self Destination string. Must not be nullptr. Modified in place.
 * @param data Source data. Must not be nullptr.
 * @param data_size Number of bytes to copy.
 * @note Writes exactly data_size bytes and NO terminator; unbounded. char_copy_3 terminates
 *       and refuses an over-long source.
 */
void char_copy_2(char *const self, char const *const data, USize const data_size);

/**
 * @brief Copy data of given size into a string with a given self capacity.
 * @param self Destination string. Must not be nullptr. Modified in place.
 * @param self_capacity Capacity of the string buffer. Must be non-zero; a zero
 *        capacity is a programming error (there is nowhere to write, not even
 *        a terminator) and aborts.
 * @param data Source data. Must not be nullptr.
 * @param data_size Number of bytes to copy. May be 0 (copies nothing, still writes
 *        the terminator) — this is the asymmetry with self_capacity above: a
 *        zero-length source is ordinary data, not a caller bug.
 * @note REFUSES to "" when data_size plus the terminator exceed self_capacity - in every
 *       build; the fit depends on the VALUE's length, so it is not a caller error.
 */
void char_copy_3(char *const self, USize const self_capacity, char const *const data, USize const data_size);

/**
 * @brief Copy a null-terminated string into a fixed buffer, truncating to fit.
 * @param self Destination buffer. Must not be nullptr. Modified in place.
 * @param self_capacity Capacity of the destination buffer. Must be at least 1; a
 *        zero capacity is a programming error and aborts.
 * @param data Source string. A nullptr source yields an empty destination.
 * @note At most `self_capacity - 1` bytes are copied and the result is always
 *       null-terminated; over-long input is silently truncated rather than an
 *       error, unlike char_copy_3 which rejects overflow.
 */
void char_copy_truncate(char *const self, USize const self_capacity, char const *const data);

/**
 * @brief Delete a string (free memory allocated by char_new_*, char_repeat_*, char_split_*, etc.).
 * @note NOT for char_slice_* results - those are borrowed views into an existing buffer.
 * @param self Pointer to the string to delete. Must not be nullptr. After this call, self is invalid.
 * @note Only use on memory allocated by this module's allocation functions.
 */
void char_delete(char *const self);

/**
 * @brief Check if a string is empty (nullptr or an empty string).
 * @param self String pointer to check. May be nullptr.
 * @return true if self is nullptr or points to an empty string, false otherwise.
 */
bool char_empty(char const *const self);

/**
 * @brief Check whether a string ends with a suffix.
 * @param self Pointer to the string. Must not be nullptr.
 * @param suffix Suffix to test. Must not be nullptr.
 * @return true if self ends with suffix, false otherwise.
 * @note An empty suffix matches every string, the empty string included: returns true.
 */
bool char_ends_with_1(char const *const self, char const *const suffix);

/**
 * @brief Check whether a string of given size ends with a suffix of given size.
 * @param self Pointer to the string. Must not be nullptr.
 * @param self_size Size of the string.
 * @param suffix Suffix to test. Must not be nullptr.
 * @param suffix_size Number of bytes of suffix.
 * @return true if self ends with suffix, false otherwise.
 * @note A suffix_size of 0 matches every string, the empty string included: returns true.
 */
bool char_ends_with_2(char const *const self, USize const self_size, char const *const suffix, USize const suffix_size);

/**
 * @brief Compare two null-terminated strings for equality (empty-safe).
 *        Two empty strings compare equal; an empty and a non-empty do not.
 * @note Behaviorally identical to char_compare_equal_1. The pair existed because
 *       char_compare_equal_1 once aborted on a zero-length input; it no longer
 *       does, so either spelling is safe on "".
 * @param char_1 First string. Must not be nullptr (may be "").
 * @param char_2 Second string. Must not be nullptr (may be "").
 * @return true if both strings are equal, false otherwise.
 */
bool char_equal_1(char const *const char_1, char const *const char_2);

/**
 * @brief Compare two strings up to given sizes for equality (empty-safe).
 *        Sizes are compared first, so a differing size is unequal without a byte scan.
 * @note Behaviorally identical to char_compare_equal_2. The pair existed because
 *       char_compare_equal_2 once aborted on a zero size; it no longer does, so
 *       either spelling is safe on an empty range.
 * @param char_1 First string. Must not be nullptr.
 * @param char_1_size Number of bytes to compare from char_1.
 * @param char_2 Second string. Must not be nullptr.
 * @param char_2_size Number of bytes to compare from char_2.
 * @return true if both byte ranges are equal, false otherwise.
 */
bool char_equal_2(char const *const char_1, USize const char_1_size, char const *const char_2, USize const char_2_size);

/**
 * @brief Erase a range from a string, compacting what follows.
 *        The bytes after self_to shift left over the erased range and the vacated
 *        tail is zero-filled; the range is NOT merely blanked in place.
 * @param self Pointer to the string. Must not be nullptr. Modified in place.
 * @param self_from Start index (inclusive).
 * @param self_to End index (inclusive).
 */
void char_erase_1(char *const self, USize const self_from, USize const self_to);

/**
 * @brief Erase a range from a string with a given size, compacting what follows.
 *        The bytes after self_to shift left over the erased range and the vacated
 *        tail is zero-filled; the range is NOT merely blanked in place.
 * @param self Pointer to the string. Must not be nullptr. Modified in place.
 * @param self_size Number of bytes in the string.
 * @param self_from Start index (inclusive).
 * @param self_to End index (inclusive).
 */
void char_erase_2(char *const self, USize const self_size, USize const self_from, USize const self_to);

/**
 * @brief Fill a string with a character.
 * @param self Pointer to the string. Must not be nullptr. Modified in place.
 * @param self_size Number of bytes to fill.
 * @param c Character to fill with.
 */
void char_fill(char *const self, USize const self_size, char const c);

/**
 * @brief Find the first occurrence of data in a string starting at an index.
 * @param self Pointer to the string. Must not be nullptr.
 * @param self_index Index to start searching from.
 * @param data Data to search for. Must not be nullptr.
 * @return Index of the first occurrence, or CHAR_NPOS if not found.
 * @note An empty data is found at self_index (the search origin); a non-empty data
 *       searched in an empty self returns CHAR_NPOS.
 */
USize char_find_1(char const *const self, USize const self_index, char const *const data);

/**
 * @brief Find the first occurrence of data of given size in a string starting at an index.
 * @param self Pointer to the string. Must not be nullptr.
 * @param self_index Index to start searching from.
 * @param data Data to search for. Must not be nullptr.
 * @param data_size Number of bytes to search for.
 * @return Index of the first occurrence, or CHAR_NPOS if not found.
 * @note A data_size of 0 is found at self_index (the search origin); a non-empty
 *       data searched in an empty self returns CHAR_NPOS.
 */
USize char_find_2(char const *const self, USize const self_index, char const *const data, USize const data_size);

/**
 * @brief Find the first occurrence of data of given size in a string of given size starting at an index.
 * @param self Pointer to the string. Must not be nullptr.
 * @param self_size Size of the string.
 * @param self_index Index to start searching from.
 * @param data Data to search for. Must not be nullptr.
 * @param data_size Number of bytes to search for.
 * @return Index of the first occurrence, or CHAR_NPOS if not found.
 * @note A data_size of 0 is found at self_index (the search origin); a non-empty
 *       data searched in an empty self (self_size 0) returns CHAR_NPOS.
 */
USize char_find_3(char const *const self, USize const self_size, USize const self_index, char const *const data, USize const data_size);

/**
 * @brief Count occurrences of data in a string.
 * @param self Pointer to the string. Must not be nullptr.
 * @param data Data to count occurrences of. Must not be nullptr.
 * @return Number of occurrences of data in self.
 * @note An empty data counts as 0 occurrences, deliberately not one match per gap as in
 *       Python: the replace family sizes its output buffer from this count, so a positive
 *       answer would splice a replacement into every gap.
 */
USize char_find_count_1(char const *const self, char const *const data);

/**
 * @brief Count occurrences of data of given size in a string.
 * @param self Pointer to the string. Must not be nullptr.
 * @param data Data to count occurrences of. Must not be nullptr.
 * @param data_size Number of bytes to count occurrences of.
 * @return Number of occurrences of data in self.
 * @note A data_size of 0 counts as 0 occurrences, deliberately not one match per gap as in
 *       Python: the replace family sizes its output buffer from this count, so a positive
 *       answer would splice a replacement into every gap.
 */
USize char_find_count_2(char const *const self, char const *const data, USize const data_size);

/**
 * @brief Count occurrences of data of given size in a string of given size.
 * @param self Pointer to the string. Must not be nullptr.
 * @param self_size Size of the string.
 * @param data Data to count occurrences of. Must not be nullptr.
 * @param data_size Number of bytes to count occurrences of.
 * @return Number of occurrences of data in self.
 * @note A data_size of 0 counts as 0 occurrences, deliberately not one match per gap as in
 *       Python: the replace family sizes its output buffer from this count, so a positive
 *       answer would splice a replacement into every gap.
 */
USize char_find_count_3(char const *const self, USize const self_size, char const *const data, USize const data_size);

/**
 * @brief Check if data exists in a string.
 * @param self Pointer to the string. Must not be nullptr.
 * @param data Data to check for existence. Must not be nullptr.
 * @return true if data exists in self, false otherwise.
 * @note An empty data exists inside every string, the empty string included: returns true.
 */
bool char_find_exists_1(char const *const self, char const *const data);

/**
 * @brief Check if data of given size exists in a string.
 * @param self Pointer to the string. Must not be nullptr.
 * @param data Data to check for existence. Must not be nullptr.
 * @param data_size Number of bytes to check.
 * @return true if data exists in self, false otherwise.
 * @note A data_size of 0 exists inside every string, the empty string included: returns true.
 */
bool char_find_exists_2(char const *const self, char const *const data, USize const data_size);

/**
 * @brief Check if data of given size exists in a string of given size.
 * @param self Pointer to the string. Must not be nullptr.
 * @param self_size Size of the string.
 * @param data Data to check for existence. Must not be nullptr.
 * @param data_size Number of bytes to check.
 * @return true if data exists in self, false otherwise.
 * @note A data_size of 0 exists inside every string, the empty string included: returns true.
 */
bool char_find_exists_3(char const *const self, USize const self_size, char const *const data, USize const data_size);

/**
 * @brief Find the first index in a string matching any character of a set.
 * @param self Pointer to the string. Must not be nullptr.
 * @param set Set of characters to match. Must not be nullptr.
 * @return Index of the first matching character, or CHAR_NPOS if none match.
 * @note An empty set holds no candidate character, so nothing matches: returns CHAR_NPOS.
 */
USize char_find_first_1(char const *const self, char const *const set);

/**
 * @brief Find the first index in a string of given size matching any character of a set.
 * @param self Pointer to the string. Must not be nullptr.
 * @param self_size Size of the string.
 * @param set Set of characters to match. Must not be nullptr.
 * @param set_size Number of characters in set.
 * @return Index of the first matching character, or CHAR_NPOS if none match.
 * @note A set_size of 0 holds no candidate character, so nothing matches: returns CHAR_NPOS.
 */
USize char_find_first_2(char const *const self, USize const self_size, char const *const set, USize const set_size);

/**
 * @brief Find the last occurrence of data in a string starting at an index.
 * @param self Pointer to the string. Must not be nullptr.
 * @param self_index Lower bound of the backward search: the scan runs from the end down to it.
 * @param data Data to search for. Must not be nullptr.
 * @return Index of the last occurrence, or CHAR_NPOS if not found.
 * @note Mirrors char_find_1: an empty data is found at self_size (the far end),
 *       not at self_index.
 */
USize char_find_reverse_1(char const *const self, USize const self_index, char const *const data);

/**
 * @brief Find the last occurrence of data of given size in a string starting at an index.
 * @param self Pointer to the string. Must not be nullptr.
 * @param self_index Lower bound of the backward search: the scan runs from the end down to it.
 * @param data Data to search for. Must not be nullptr.
 * @param data_size Number of bytes to search for.
 * @return Index of the last occurrence, or CHAR_NPOS if not found.
 * @note Mirrors char_find_2: a data_size of 0 is found at self_size (the far end),
 *       not at self_index.
 */
USize char_find_reverse_2(char const *const self, USize const self_index, char const *const data, USize const data_size);

/**
 * @brief Find the last occurrence of data of given size in a string of given size starting at an index.
 * @param self Pointer to the string. Must not be nullptr.
 * @param self_size Size of the string.
 * @param self_index Lower bound of the backward search: the scan runs from the end down to it.
 * @param data Data to search for. Must not be nullptr.
 * @param data_size Number of bytes to search for.
 * @return Index of the last occurrence, or CHAR_NPOS if not found.
 * @note Mirrors char_find_3: a data_size of 0 is found at self_size (the far end),
 *       not at self_index.
 */
USize char_find_reverse_3(char const *const self, USize const self_size, USize const self_index, char const *const data, USize const data_size);

/**
 * @brief Find the first occurrence of a character in a string starting at an index.
 * @param self Pointer to the null-terminated string. Must not be nullptr.
 * @param self_index Index to start searching from.
 * @param c Character to search for.
 * @return Pointer to the first occurrence within self, or nullptr if not found.
 */
char* char_find_slice_1(char const *const self, USize const self_index, char const c);

/**
 * @brief Find the first occurrence of a character in a string starting at an index.
 * @param self Pointer to the null-terminated string. Must not be nullptr.
 * @param self_size Size of the string.
 * @param self_index Index to start searching from.
 * @param c Character to search for.
 * @return Pointer to the first occurrence within self, or nullptr if not found - a position
 *         into the sized buffer, which may be unterminated, not a string. A
 *         self_index equal to self_size has nothing left to scan and answers nullptr;
 *         one past self_size is a caller error.
 */
char* char_find_slice_2(char const *const self, USize const self_size, USize const self_index, char const c);

/**
 * @brief Find the first occurrence of data in a string starting at an index.
 * @param self Pointer to the null-terminated string. Must not be nullptr.
 * @param self_index Index to start searching from.
 * @param data Substring to search for. Must not be nullptr.
 * @return Pointer to the first occurrence within self, or nullptr if not found.
 */
char* char_find_slice_3(char const *const self, USize const self_index, char const *const data);

/**
 * @brief Find the first occurrence of data of given size in a string starting at an index.
 * @param self Pointer to the null-terminated string. Must not be nullptr.
 * @param self_index Index to start searching from.
 * @param data Substring to search for. Must not be nullptr.
 * @param data_size Number of bytes in data.
 * @return Pointer to the first occurrence within self, or nullptr if not found - a position
 *         into the sized buffer, which may be unterminated, not a string.
 */
char* char_find_slice_4(char const *const self, USize const self_index, char const *const data, USize const data_size);

/**
 * @brief Find the first occurrence of data of given size in a string of given size starting at an index.
 * @param self Pointer to the string. Must not be nullptr.
 * @param self_size Size of the string in bytes.
 * @param self_index Index to start searching from.
 * @param data Substring to search for. Must not be nullptr.
 * @param data_size Number of bytes in data.
 * @return Pointer to the first occurrence within self, or nullptr if not found - a position
 *         into the sized buffer, which may be unterminated, not a string.
 */
char* char_find_slice_5(char const *const self, USize const self_size, USize const self_index, char const *const data, USize const data_size);

/**
 * @brief Format a string with arguments into a buffer of given capacity.
 * @param self Target buffer. Must not be nullptr.
 * @param self_capacity Target buffer capacity. Must be non-zero (aborts on 0).
 * @param format Format string. Must not be nullptr.
 * @note A result longer than self_capacity - 1 is silently TRUNCATED (always terminated);
 *       the needed size is not reported. Use container/string's string_format when the
 *       result must grow to fit.
 */
void char_format(char *const self, USize const self_capacity, char const *const format, ...);

/**
 * @brief Format a byte count as a human-readable size into a buffer.
 *        Values under 1024 render as "<n> B"; larger values use binary units
 *        (K, M, G, T, P) with one decimal place, e.g. "1.5 K".
 * @param self Destination buffer. Must not be nullptr. Modified in place.
 * @param self_capacity Size of the destination buffer (including null terminator).
 * @param bytes Byte count to format.
 * @note Display-only: a buffer too small for the text truncates it (char_format), and "P"
 *       is the last unit, so exabyte values render as thousands of P.
 */
void char_from_bytes_human_1(char *const self, USize const self_capacity, USize const bytes);

/**
 * @brief Convert a digit value to its character: the inverse of char_to_number.
 * @param number Digit value, 0-9.
 * @return '0'-'9' for 0-9, '\0' for anything else.
 */
char char_from_number(U8 const number);

/**
 * @brief Convert a floating-point number to a string with default precision.
 * @param self Destination buffer. Must not be nullptr. Modified in place.
 * @param self_capacity Total size of the destination buffer in bytes, including
 *                     room for the null terminator. This is the BUFFER's size,
 *                     not a digit count - passing a *_DIGITS_MAX constant here
 *                     under-reports the space available and the value is refused.
 * @param number Floating-point value to convert.
 */
void char_from_numbers_float_1(char *const self, USize const self_capacity, FSize const number);

/**
 * @brief Convert a floating-point number to a string with a given precision.
 * @param self Destination buffer. Must not be nullptr. Modified in place.
 * @param self_capacity Total size of the destination buffer in bytes, including
 *                     room for the null terminator. This is the BUFFER's size,
 *                     not a digit count - passing a *_DIGITS_MAX constant here
 *                     under-reports the space available and the value is refused.
 * @param number Floating-point value to convert.
 * @param precision Number of digits to emit after the decimal point.
 * @note Rounds half up at the last digit and CARRIES: 0.99999 at precision 4 is "1.0000",
 *       never "0.9999". REFUSES wholly to "" (never a truncated number) when the result does
 *       not fit, or for NaN, an infinity, or a magnitude beyond USize.
 * @note precision is bounded by USIZE_DIGITS_MAX - 1 (19 on 64-bit): larger aborts as caller
 *       error. precision 0 emits no point ("3"). Digits past the seventeenth carry no
 *       information at FSize's resolution.
 */
void char_from_numbers_float_2(char *const self, USize const self_capacity, FSize const number, U8 const precision);

/**
 * @brief Convert a signed integer to a null-terminated string.
 * @param self Destination buffer. Must not be nullptr. Modified in place.
 * @param self_capacity Total size of the destination buffer in bytes, including
 *                     room for the null terminator. This is the BUFFER's size,
 *                     not a digit count - passing a *_DIGITS_MAX constant here
 *                     under-reports the space available and the value is refused.
 * @param number Integer value to convert.
 * @note REFUSES rather than truncating: when the result (padding, sign, digits and the
 *       terminator) does not fit in self_capacity, the buffer is set to the empty string in every
 *       build - a truncated number is indistinguishable from a real one at the call site.
 */
void char_from_numbers_int_1(char *const self, USize const self_capacity, ISize const number);

/**
 * @brief Convert a signed integer to a string with left-padding.
 * @param self Destination buffer. Must not be nullptr. Modified in place.
 * @param self_capacity Total size of the destination buffer in bytes, including
 *                     room for the null terminator. This is the BUFFER's size,
 *                     not a digit count - passing a *_DIGITS_MAX constant here
 *                     under-reports the space available and the value is refused.
 * @param number Integer value to convert.
 * @param padding Number of extra zero characters to pad the digits with.
 * @note Layout is sign + padding + digits (a minus sign, if any, always leads). If the
 *       result does not fit in self_capacity, the function refuses without aborting: self
 *       is set to the empty string rather than writing a truncated value.
 */
void char_from_numbers_int_2(char *const self, USize const self_capacity, ISize const number, U8 const padding);

/**
 * @brief Convert an unsigned integer to a null-terminated string.
 * @param self Destination buffer. Must not be nullptr. Modified in place.
 * @param self_capacity Total size of the destination buffer in bytes, including
 *                     room for the null terminator. This is the BUFFER's size,
 *                     not a digit count - passing a *_DIGITS_MAX constant here
 *                     under-reports the space available and the value is refused.
 * @param number Unsigned integer value to convert.
 */
void char_from_numbers_uint_1(char *const self, USize const self_capacity, USize const number);

/**
 * @brief Convert an unsigned integer to a string with left-padding.
 * @param self Destination buffer. Must not be nullptr. Modified in place.
 * @param self_capacity Total size of the destination buffer in bytes, including
 *                     room for the null terminator. This is the BUFFER's size,
 *                     not a digit count - passing a *_DIGITS_MAX constant here
 *                     under-reports the space available and the value is refused.
 * @param number Unsigned integer value to convert.
 * @param padding Number of extra zero characters to pad the digits with.
 * @note Layout is padding + digits. If the result does not fit in self_capacity, the
 *       function refuses without aborting: self is set to the empty string rather
 *       than writing a truncated value.
 */
void char_from_numbers_uint_2(char *const self, USize const self_capacity, USize const number, U8 const padding);

/**
 * @brief Create a trimmed copy of a null-terminated string.
 * @param self Source string. Not modified.
 * @return Newly allocated trimmed string ("" for pure whitespace). Caller must free. This
 *         ALLOCATES despite the from_ name it shares with the in-place formatters.
 */
char* char_from_trim_1(char const *const self);

/**
 * @brief Create a trimmed copy of a string of given size.
 * @param self Source string. Not modified.
 * @param self_size Size of source string.
 * @return Newly allocated trimmed string ("" for pure whitespace). Caller must free. This
 *         ALLOCATES despite the from_ name it shares with the in-place formatters.
 */
char* char_from_trim_2(char const *const self, USize const self_size);

/**
 * @brief Match a string against a glob pattern ('*' = any run, '?' = any one char).
 *        Literal bytes match themselves; there are no character classes or escapes, and
 *        '*' crosses EVERY byte, '/' included - unlike a path matcher (vestigo's), where a
 *        segment star stops at the separator.
 * @param pattern Glob pattern. Must not be nullptr (may be "").
 * @param text String to test. Must not be nullptr (may be "").
 * @return true if the whole text matches the whole pattern.
 */
bool char_glob_match_1(char const *const pattern, char const *const text);

/**
 * @brief Match sized spans against a glob pattern ('*' / '?'); empty-safe.
 * @param pattern Glob pattern. Must not be nullptr.
 * @param pattern_size Number of bytes in pattern.
 * @param text String to test. Must not be nullptr.
 * @param text_size Number of bytes in text.
 * @return true if the whole text matches the whole pattern.
 */
bool char_glob_match_2(char const *const pattern, USize const pattern_size, char const *const text, USize const text_size);

/**
 * @brief Check if a character is an ASCII alphabetic letter.
 * @param c Character to check.
 * @return true if c is A-Z or a-z, false otherwise.
 */
bool char_is_alpha(char const c);

/**
 * @brief Check if a character is a lowercase ASCII letter.
 * @param c Character to check.
 * @return true if c is a-z, false otherwise.
 */
bool char_is_lower(char const c);

/**
 * @brief Check if a character is a numeric digit ('0'-'9').
 * @param c Character to check.
 * @return true if c is a digit, false otherwise.
 */
bool char_is_number(char const c);

/**
 * @brief Check if a character is an uppercase ASCII letter.
 * @param c Character to check.
 * @return true if c is A-Z, false otherwise.
 */
bool char_is_upper(char const c);

/**
 * @brief Check if a character is an ASCII whitespace character.
 * @param c Character to check.
 * @return true if c is space, tab, newline, or carriage return.
 */
bool char_is_whitespace(char const c);

/**
 * @brief Join an array of null-terminated strings with a separator into a new string.
 * @param parts Array of string pointers. Must not be nullptr; no element may be nullptr.
 * @param count Number of strings in parts. Zero yields "".
 * @param separator Separator placed between parts. Must not be nullptr (may be empty).
 * @return Pointer to the new joined string. Caller must free.
 */
char* char_join_1(char const *const *const parts, USize const count, char const *const separator);

/**
 * @brief Join an array of strings with a separator of given size into a new string.
 * @param parts Array of string pointers. Must not be nullptr; no element may be nullptr.
 * @param count Number of strings in parts. Zero yields "".
 * @param separator Separator placed between parts. Must not be nullptr (may be empty).
 * @param separator_size Number of bytes of separator.
 * @return Pointer to the new joined string. Caller must free.
 */
char* char_join_2(char const *const *const parts, USize const count, char const *const separator, USize const separator_size);

/**
 * @brief Get the length of a null-terminated string.
 * @param self Pointer to the string. Must not be nullptr.
 * @return Number of characters before the null terminator.
 */
USize char_length(char const *const self);

/**
 * @brief Convert a string to lowercase in place.
 * @param self Pointer to the string. Must not be nullptr. Modified in place.
 */
void char_lower_1(char *const self);

/**
 * @brief Convert a string of given size to lowercase in place.
 * @param self Pointer to the string. Must not be nullptr. Modified in place.
 * @param self_size Number of bytes to convert.
 */
void char_lower_2(char *const self, USize const self_size);

/**
 * @brief Move a string pointer to another pointer (transfers ownership).
 * @param self Pointer to the destination pointer. Modified to point to data.
 * @param data Pointer to the source pointer. Set to nullptr after move.
 * @note After this call, *data is nullptr and *self owns the memory.
 */
void char_move(char **const self, char **const data);

/**
 * @brief Create a new string holding size content bytes, zero-initialized.
 * @param size Number of content bytes to allocate, NOT counting the null terminator
 *        (a terminator byte is allocated in addition to size). Must be non-zero; a
 *        zero size is a programming error and aborts.
 * @return Pointer to the new string. Caller must free with char_delete().
 * @note The buffer is zero-initialized (memory_alloc always zeroes new memory), so
 *       every byte, including the terminator, already reads as zero. For an empty VALUE
 *       use char_new_2("").
 */
char* char_new_1(USize const size);

/**
 * @brief Create a new string from a null-terminated data buffer.
 * @param data Source string. Must not be nullptr.
 * @return Pointer to the new string. Caller must free.
 */
char* char_new_2(char const *const data);

/**
 * @brief Create a new string from a data buffer of given size.
 * @param data Source data. Must not be nullptr.
 * @param data_size Number of bytes to copy.
 * @return Pointer to the new string. Caller must free.
 */
char* char_new_3(char const *const data, USize const data_size);

/**
 * @brief Create a new string from a signed integer value.
 * @param number Integer value to convert.
 * @return Pointer to the new string. Caller must free.
 * @note Cannot refuse for size: the buffer is allocated to fit, unlike the fixed-buffer
 *       char_from_numbers_* whose "" means refused.
 */
char* char_new_from_numbers_int_1(ISize const number);

/**
 * @brief Create a new string from a signed integer with left-padding.
 * @param number Integer value to convert.
 * @param padding Number of extra '0' characters prepended to the digits. This is
 *                additive, NOT a minimum width: a padding of 3 on the value 7
 *                yields "0007" (3 + 1 digit), not "007".
 * @return Pointer to the new string. Caller must free.
 * @note Cannot refuse for size: the buffer is allocated to fit, unlike the fixed-buffer
 *       char_from_numbers_* whose "" means refused.
 */
char* char_new_from_numbers_int_2(ISize const number, U8 const padding);

/**
 * @brief Create a new string from an unsigned integer value.
 * @param number Unsigned integer value to convert.
 * @return Pointer to the new string. Caller must free.
 * @note Cannot refuse for size: the buffer is allocated to fit, unlike the fixed-buffer
 *       char_from_numbers_* whose "" means refused.
 */
char* char_new_from_numbers_uint_1(USize const number);

/**
 * @brief Create a new string from an unsigned integer with left-padding.
 * @param number Unsigned integer value to convert.
 * @param padding Number of extra '0' characters prepended to the digits. This is
 *                additive, NOT a minimum width: a padding of 3 on the value 7
 *                yields "0007" (3 + 1 digit), not "007".
 * @return Pointer to the new string. Caller must free.
 * @note Cannot refuse for size: the buffer is allocated to fit, unlike the fixed-buffer
 *       char_from_numbers_* whose "" means refused.
 */
char* char_new_from_numbers_uint_2(USize const number, U8 const padding);

/**
 * @brief Create a new string with every occurrence of find replaced by replace.
 * @param self Source string. Must not be nullptr.
 * @param find Substring to replace. Must not be nullptr.
 * @param replace Replacement substring. Must not be nullptr (may be empty).
 * @return Pointer to the new string. Caller must free.
 * @note An empty find matches nothing, so the result is a verbatim copy of self.
 */
char* char_new_replace_1(char const *const self, char const *const find, char const *const replace);

/**
 * @brief Create a new string with every occurrence of find (of given size) replaced by replace.
 * @param self Source string. Must not be nullptr.
 * @param self_size Size of the source string.
 * @param find Substring to replace. Must not be nullptr.
 * @param find_size Number of bytes of find.
 * @param replace Replacement substring. Must not be nullptr (may be empty).
 * @param replace_size Number of bytes of replace.
 * @return Pointer to the new string. Caller must free.
 * @note A find_size of 0 matches nothing, so the result is a verbatim copy of self.
 */
char* char_new_replace_2(char const *const self, USize const self_size, char const *const find, USize const find_size, char const *const replace, USize const replace_size);

/**
 * @brief Create a new string slice from a given index.
 * @param self Source string. Must not be nullptr.
 * @param self_index Index to start the slice (inclusive).
 * @return Pointer to the new string slice. Caller must free.
 */
char* char_new_slice_1(char const *const self, USize const self_index);

/**
 * @brief Create a new string slice from a given index and size.
 * @param self Source string. Must not be nullptr.
 * @param self_size Number of bytes in the source string.
 * @param self_index Index to start the slice (inclusive).
 * @return Pointer to the new string slice. Caller must free.
 */
char* char_new_slice_2(char const *const self, USize const self_size, USize const self_index);

/**
 * @brief Create a new string slice from a range.
 * @param self Source string. Must not be nullptr.
 * @param self_from Start index (inclusive).
 * @param self_to End index (inclusive).
 * @return Pointer to the new string slice. Caller must free.
 */
char* char_new_slice_range_1(char const *const self, USize const self_from, USize const self_to);

/**
 * @brief Create a new string slice from a range and size.
 * @param self Source string. Must not be nullptr.
 * @param self_size Number of bytes in the source string.
 * @param self_from Start index (inclusive).
 * @param self_to End index (inclusive).
 * @return Pointer to the new string slice. Caller must free.
 */
char* char_new_slice_range_2(char const *const self, USize const self_size, USize const self_from, USize const self_to);

/**
 * @brief Convert a hex character ('0'-'9', 'a'-'f', 'A'-'F') to its numeric value.
 * @param c Character to convert.
 * @return Numeric value (0-15), or 0xFF if not a valid hex digit.
 */
U8 char_raw_to_hex(char const c);

/**
 * @brief Remove a character at an index from a string.
 * @param self Pointer to the string pointer. Must not be nullptr. Modified in place.
 * @param index Index of the character to remove.
 */
void char_remove_1(char **const self, USize const index);

/**
 * @brief Remove a character at an index from a string of given size.
 * @param self Pointer to the string pointer. Must not be nullptr. Modified in place.
 * @param self_size Number of bytes in the string.
 * @param index Index of the character to remove.
 */
void char_remove_2(char **const self, USize const self_size, USize const index);

/**
 * @brief Repeat a string a given number of times.
 * @param data Source string to repeat. Must not be nullptr.
 * @param count Number of repetitions.
 * @return Pointer to the new repeated string. Caller must free.
 */
char* char_repeat_1(char const *const data, USize const count);

/**
 * @brief Repeat a string of given size a given number of times.
 * @param data Source string to repeat. Must not be nullptr.
 * @param data_size Number of bytes in the source string.
 * @param count Number of repetitions.
 * @return Pointer to the new repeated string. Caller must free.
 */
char* char_repeat_2(char const *const data, USize const data_size, USize const count);

/**
 * @brief Overwrite self from self_index with data and terminate right after it - the
 *        module's splice primitive, NOT a one-character replace: everything past the
 *        written bytes is cut off ("abcdef", 1, "X" -> "aX").
 * @param self Destination string. Must not be nullptr. Modified in place. Unbounded: the
 *        caller guarantees self_index + strlen(data) + 1 bytes of capacity.
 * @param self_index Index at which the overwrite starts.
 * @param data Source string to copy from. Must not be nullptr.
 */
void char_replace_1(char *const self, USize const self_index, char const *const data);

/**
 * @brief Overwrite self from self_index with data_size bytes and terminate right after
 *        them - the sized splice primitive (see char_replace_1): the tail is cut off, and
 *        nothing is bounded, so the caller guarantees self_index + data_size + 1 bytes.
 * @param self Destination string. Must not be nullptr. Modified in place.
 * @param self_index Index at which the overwrite starts.
 * @param data Source bytes. Must not be nullptr.
 * @param data_size Number of bytes to copy from data.
 * @note A data_size of 0 writes no bytes but still writes the terminator at self_index.
 */
void char_replace_2(char *const self, USize const self_index, char const *const data, USize const data_size);

/**
 * @brief Reverse a null-terminated string in place.
 * @param self String to reverse. Must not be nullptr. Modified in place.
 */
void char_reverse_1(char *const self);

/**
 * @brief Reverse a string of given size in place.
 * @param self String to reverse. Must not be nullptr. Modified in place.
 * @param self_size Number of bytes to reverse.
 */
void char_reverse_2(char *const self, USize const self_size);

/**
 * @brief Create a string slice from a given index.
 * @param self Source string. Must not be nullptr.
 * @param self_index Index to start the slice (inclusive).
 * @return Borrowed VIEW into self at self_index - NOT a copy and NOT owned.
 *         Must not be passed to char_delete; it stays valid only while self does. A
 *         self_index strictly past the end aborts (caller error); at the end it is "".
 */
char* char_slice_1(char const *const self, USize const self_index);

/**
 * @brief Create a string slice from a given index and size.
 * @param self Source string. Must not be nullptr.
 * @param self_size Number of bytes in the source string.
 * @param self_index Index to start the slice (inclusive).
 * @return Borrowed VIEW into self at self_index - NOT a copy and NOT owned.
 *         Must not be passed to char_delete; it stays valid only while self does.
 *         Use char_new_slice_* when an owned copy is wanted. A self_index strictly past
 *         self_size aborts (caller error); equal to it is "".
 */
char* char_slice_2(char const *const self, USize const self_size, USize const self_index);

/**
 * @brief Split a string by a delimiter.
 * @param self Source string. Must not be nullptr.
 * @param delimiter Delimiter string. Must not be nullptr.
 * @return Newly allocated copy of the FIRST token only - the bytes before the first
 *         delimiter, or the whole string when the delimiter is absent - or nullptr on
 *         failure. This does not return a list; use char_split_next to walk the
 *         remaining tokens. Caller must free.
 */
char* char_split_1(char const *const self, char const *const delimiter);

/**
 * @brief Split a string of given size by a delimiter.
 * @param self Source string. Must not be nullptr.
 * @param self_size Number of bytes in the source string.
 * @param delimiter Delimiter string. Must not be nullptr.
 * @return Newly allocated copy of the FIRST token only - the bytes before the first
 *         delimiter, or the whole string when the delimiter is absent - or nullptr on
 *         failure. This does not return a list; use char_split_next to walk the
 *         remaining tokens. Caller must free.
 */
char* char_split_2(char const *const self, USize const self_size, char const *const delimiter);

/**
 * @brief Split a string of given size by a delimiter of given size.
 * @param self Source string. Must not be nullptr.
 * @param self_size Number of bytes in the source string.
 * @param delimiter Delimiter string. Must not be nullptr.
 * @param delimiter_size Number of bytes in the delimiter.
 * @return Newly allocated copy of the FIRST token only - the bytes before the first
 *         delimiter, or the whole string when the delimiter is absent - or nullptr on
 *         failure. This does not return a list; use char_split_next to walk the
 *         remaining tokens. Caller must free.
 */
char* char_split_3(char const *const self, USize const self_size, char const *const delimiter, USize const delimiter_size);

/**
 * @brief Iterate the tokens of a string separated by a delimiter, without allocating.
 *
 * Initialize *index to 0 before the first call. Each call reports the next token via
 * token_from / token_size (the token is the token_size bytes at self[*token_from];
 * token_size may be 0 for empty tokens) and advances *index. Returns false when no
 * tokens remain.
 *
 * @param self Pointer to the string. Must not be nullptr.
 * @param self_size Size of the string.
 * @param delimiter Delimiter separating tokens. Must not be nullptr.
 * @param delimiter_size Number of bytes of delimiter.
 * @param index Caller-owned iteration cursor. Must not be nullptr. Set to 0 before the first call.
 * @param token_from Out: start index of the token. Must not be nullptr.
 * @param token_size Out: byte length of the token. Must not be nullptr.
 * @return true if a token was produced, false when iteration is complete.
 * @note An empty delimiter marks no split point, so the whole remaining string is
 *       returned as one final token. Splitting an empty self yields exactly one
 *       empty token, then reports iteration complete. A cursor already past self_size
 *       answers false rather than reading beyond it.
 */
bool char_split_next(char const *const self,
    USize const self_size, char const *const delimiter, USize const delimiter_size, USize *const index, USize *const token_from, USize *const token_size);

/**
 * @brief Check whether a string starts with a prefix.
 * @param self Pointer to the string. Must not be nullptr.
 * @param prefix Prefix to test. Must not be nullptr.
 * @return true if self starts with prefix, false otherwise.
 * @note An empty prefix matches every string, the empty string included: returns true.
 */
bool char_starts_with_1(char const *const self, char const *const prefix);

/**
 * @brief Check whether a string of given size starts with a prefix of given size.
 * @param self Pointer to the string. Must not be nullptr.
 * @param self_size Size of the string.
 * @param prefix Prefix to test. Must not be nullptr.
 * @param prefix_size Number of bytes of prefix.
 * @return true if self starts with prefix, false otherwise.
 * @note A prefix_size of 0 matches every string, the empty string included: returns true.
 */
bool char_starts_with_2(char const *const self, USize const self_size, char const *const prefix, USize const prefix_size);

/**
 * @brief Convert a character to lowercase if it is uppercase.
 * @param c Character to convert.
 * @return Lowercase version of c, or c if not uppercase.
 */
char char_to_lower(char const c);

/**
 * @brief Convert a numeric character ('0'-'9') to its integer value.
 * @param number Character to convert.
 * @return Integer value (0-9), or 0xFF if not a digit.
 */
U8 char_to_number(char const number);

/**
 * @brief Convert a null-terminated string to a floating-point number.
 * @param number String to convert. Must not be nullptr.
 * @return Parsed floating-point value, or 0 on error. See char_to_numbers_float_2.
 */
FSize char_to_numbers_float_1(char const *const number);

/**
 * @brief Convert a string of given size to a floating-point number.
 * @param number String to convert. Must not be nullptr.
 * @param number_size Number of bytes to parse.
 * @return Parsed floating-point value, or 0 on error.
 * @note Lossy, lenient dialect: digits, '.' and '-' only, stopping at the first other byte.
 *       A second '.' is ignored and a '-' anywhere negates ("1.2.3" -> 1.23, "1-2" -> -12).
 *       The integer part accumulates in FSize and cannot wrap (an over-long run rounds,
 *       then reaches infinity); a fraction digit is dropped once taking it would overflow the
 *       exact accumulator - the twentieth at the latest - which is below the type's resolution
 *       anyway. Use char_try_to_number_f to detect a malformed input instead.
 */
FSize char_to_numbers_float_2(char const *const number, USize const number_size);

/**
 * @brief Convert a null-terminated string to a signed integer.
 * @param number String to convert. Must not be nullptr.
 * @return Parsed signed integer value, or 0 on error.
 */
ISize char_to_numbers_int_1(char const *const number);

/**
 * @brief Convert a string of given size to a signed integer.
 * @param number String to convert. Must not be nullptr.
 * @param number_size Number of bytes to parse.
 * @return Parsed signed integer value, or 0 on error.
 * @note Lossy parse: the magnitude accumulates unsigned and saturates at ISIZE_MAX
 *       (or ISIZE_MIN when the input carries a leading minus sign) on overflow, rather
 *       than wrapping. Use char_try_to_number_i to detect overflow instead of silently
 *       absorbing it; it takes a terminated string, so copy a sized span first.
 */
ISize char_to_numbers_int_2(char const *const number, USize const number_size);

/**
 * @brief Convert a null-terminated string to an unsigned integer.
 * @param number String to convert. Must not be nullptr.
 * @return Parsed unsigned integer value, or 0 on error.
 */
USize char_to_numbers_uint_1(char const *const number);

/**
 * @brief Convert a string of given size to an unsigned integer.
 * @param number String to convert. Must not be nullptr.
 * @param number_size Number of bytes to parse.
 * @return Parsed unsigned integer value, or 0 on error.
 * @note Lossy parse: the value accumulates and saturates at USIZE_MAX on overflow,
 *       rather than wrapping. Use char_try_to_number_u to detect overflow instead
 *       of silently absorbing it; it takes a terminated string, so copy a sized span first.
 */
USize char_to_numbers_uint_2(char const *const number, USize const number_size);

/**
 * @brief Convert a character to uppercase if it is lowercase.
 * @param c Character to convert.
 * @return Uppercase version of c, or c if not lowercase.
 */
char char_to_upper(char const c);

/**
 * @brief Trim whitespace by replacing a null-terminated string.
 * @param self Pointer to the string pointer. REALLOCATED: unlike lower/upper/reverse/fill,
 *        which work in place, the old buffer is released and *self points at a new
 *        exact-size one, so *self must be heap-owned.
 */
void char_trim_1(char **const self);

/**
 * @brief Trim whitespace by replacing a string of given size.
 * @param self Pointer to the string pointer. REALLOCATED: see char_trim_1.
 * @param self_size Size of source string.
 */
void char_trim_2(char **const self, USize const self_size);

/**
 * @brief Parse a null-terminated string to a boolean, reporting failure.
 *
 * Case-insensitive: "1", "true", "yes", "on" parse true; "0", "false", "no",
 * "off" parse false. This is the framework-wide boolean dialect shared by the
 * env module and argparse.
 *
 * @param value String to parse. Must not be nullptr.
 * @param out Destination written only on success. Must not be nullptr.
 * @return true if the string is a recognized boolean word; false otherwise
 *         (empty input or any other text, in which case out is left untouched).
 */
bool char_try_to_bool(char const *const value, bool *const out);

/**
 * @brief Parse a null-terminated string to a floating-point number, reporting failure.
 * @param value String to parse. Must not be nullptr.
 * @param out Destination written only on success. Must not be nullptr.
 * @return true if the whole string is a valid number in range; false on empty input,
 *         trailing characters, or overflow (in which case out is left untouched).
 * @note strtod dialect: leading whitespace, an optional sign, "inf" / "nan" and hexadecimal
 *       floats are accepted - unlike the lossy char_to_numbers_float_*, which stop at the
 *       first byte that is not a digit, '.' or '-'. A value too small to represent is accepted
 *       as the denormal or zero strtod produced; only overflow (an infinity) fails.
 */
bool char_try_to_number_f(char const *const value, FSize *const out);

/**
 * @brief Parse a null-terminated string to a signed integer, reporting failure.
 * @param value String to parse. Must not be nullptr.
 * @param out Destination written only on success. Must not be nullptr.
 * @return true if the whole string is a valid integer in range; false on empty input,
 *         trailing characters, or overflow (in which case out is left untouched).
 * @note strtoll dialect, base 10: leading whitespace and an optional '+' or '-' are
 *       accepted; a "0x" prefix is trailing garbage, not hexadecimal.
 */
bool char_try_to_number_i(char const *const value, ISize *const out);

/**
 * @brief Parse a null-terminated string to an unsigned integer, reporting failure.
 * @param value String to parse. Must not be nullptr. A leading '-' is rejected.
 * @param out Destination written only on success. Must not be nullptr.
 * @return true if the whole string is a valid unsigned integer in range; false on empty
 *         input, a leading '-', trailing characters, or overflow (out left untouched).
 * @note strtoull dialect, base 10: leading whitespace and an optional '+' are accepted; a
 *       "0x" prefix is trailing garbage, not hexadecimal.
 */
bool char_try_to_number_u(char const *const value, USize *const out);

/**
 * @brief Convert a string to uppercase in place.
 * @param self Pointer to the string. Must not be nullptr. Modified in place.
 */
void char_upper_1(char *const self);

/**
 * @brief Convert a string of given size to uppercase in place.
 * @param self Pointer to the string. Must not be nullptr. Modified in place.
 * @param self_size Number of bytes to convert.
 */
void char_upper_2(char *const self, USize const self_size);

/**
 * @brief Word-wrap a string to a column width (heap copy, char*).
 *        Greedy reflow: whitespace-separated words are re-joined with single spaces and
 *        a newline is inserted before any word that would overflow `width`; a word longer
 *        than `width` occupies its own line (never split). Width is a byte count.
 * @param self Null-terminated string. Must not be nullptr (may be "").
 * @param width Maximum line width in bytes. Must be non-zero.
 * @return Heap-allocated wrapped copy; free with char_delete.
 */
char* char_wrap_1(char const *const self, USize const width);

/**
 * @brief Word-wrap a string of given size to a column width (heap copy, char*).
 * @param self String to wrap. Must not be nullptr (may be empty when self_size is 0).
 * @param self_size Number of bytes to read from self.
 * @param width Maximum line width in bytes. Must be non-zero; a zero width is a
 *        programming error and aborts.
 * @return Heap-allocated wrapped copy; free with char_delete.
 */
char* char_wrap_2(char const *const self, USize const self_size, USize const width);

#endif // CHAR_CHAR_H