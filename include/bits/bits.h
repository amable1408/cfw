/*
 * bits.h - Bit manipulation utilities for the C Libraries Framework
 *
 * Features:
 *   - Single-word bits (USize): bits_at / bits_flip / bits_write, bits_count, and
 *     bits_first_set / bits_last_set
 *   - Multi-word bit arrays (bits_array_*): set / clear / test a bit at any global
 *     index across a U64 word array (word = bit / BITS_WORD_BITS, offset =
 *     bit % BITS_WORD_BITS), plus
 *     any / count / clear_all over the whole array - the primitive for scancode
 *     sets, permission and feature flags, and large enum membership
 *   - Rendering: bits_format writes a value's bits into a caller buffer, most
 *     significant first, with an optional separator every group_size digits;
 *     bits_print_1 / _2 are the stdout wrappers over it
 *
 * Usage Examples:
 *   @code
 *   USize value = 0;
 *
 *   bits_flip(&value, 3);              // value == 8
 *   bits_write(&value, 0, true);       // value == 9
 *
 *   bool const third = bits_at(value, 3);   // true
 *
 *   bits_print_1(value, 8);            // prints "00001001"
 *   bits_print_2(value, 12, '-');      // prints "0000-00001001"
 *
 *   char rendered[BITS_FORMAT_CAPACITY] = DEFAULT_INITIALIZATION;
 *
 *   bits_format(value, 4, '\0', 0, rendered, sizeof(rendered));   // "1001"
 *
 *   U64 words[2] = DEFAULT_INITIALIZATION;
 *
 *   bits_array_set(words, 2, 70);
 *
 *   bool const held = bits_array_test(words, 2, 70);   // true
 *   @endcode
 *
 * Error Handling:
 *   - Under ERROR_CHECK_ENABLED a null pointer, an index above 63 on the
 *     single-word functions, a self_size of 0 or above 64 on bits_format /
 *     bits_print_*, a buffer too small for the rendering, or a bit whose word lies
 *     at or past self_size on bits_array_* aborts through error_check_* with a
 *     logged location. Nothing in this module logs and returns
 *   - With the checks compiled out the same inputs are inert: the single-word
 *     functions leave the value unchanged and bits_at answers false for an index
 *     above 63; bits_format writes "" and returns 0, and bits_print_* prints
 *     nothing, for a self_size of 0 or above 64 or a buffer too small, and a
 *     null buffer is written nowhere (bits_format returns 0); bits_array_set /
 *     _clear do nothing and bits_array_test answers false for a bit past the
 *     array. Nothing reads or writes past a buffer in any build
 *   - bits_first_set / bits_last_set on a zero word answer BITS_INDEX_NONE: a zero
 *     word is data, not an error
 *   - Supported targets are 64-bit: USize is 64 bits wide, so the single-word
 *     family covers indices 0-63 exactly like the U64 words of the array family
 *
 * Thread Safety:
 *   - bits_at, bits_count, bits_first_set, bits_last_set and bits_format are pure
 *     (bits_format writes only the caller's buffer)
 *   - bits_flip, bits_write, bits_array_set, bits_array_clear and
 *     bits_array_clear_all mutate through the caller's pointer; callers synchronize
 *   - bits_print_1 / _2 write to stdout and share its lock
 *
 * Memory Management:
 *   - No allocation is performed. bits_print_* render into a stack buffer of
 *     BITS_FORMAT_CAPACITY bytes; bits_format renders into the caller's
 *
 * Performance Characteristics:
 *   - bits_at, bits_flip, bits_write, bits_count, bits_first_set, bits_last_set and
 *     the per-bit bits_array_* functions are constant time (count / first / last
 *     are single instructions on x86-64)
 *   - bits_array_any / _count / _clear_all are linear in the word count;
 *     bits_format and bits_print_* are linear in self_size
 *   - Every function opens a trace frame and runs its error checks; a caller in a
 *     per-element loop (al_bool) pays that per bit
 *
 * Dependencies:
 *   - <memory/memory.h> for memory_set; its chain (error -> tracelog -> log ->
 *     console) supplies the integer types, error_check_*, trace_log_*, CHAR_BIT
 *     through <limits.h>, and stdout through <stdio.h>
 *   - __builtin_popcountll / __builtin_ctzll / __builtin_clzll (gcc, clang)
 *
 * See bits.c for implementation details.
 */

#ifndef BITS_H
#define BITS_H

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/

#include <memory/memory.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

/**
 * @def BITS_FORMAT_CAPACITY
 * @brief Buffer capacity that fits any bits_format rendering: 64 digits, a
 *        separator between every pair of them (group_size 1), and the terminator.
 */
#define BITS_FORMAT_CAPACITY 128

/**
 * @def BITS_INDEX_NONE
 * @brief The index bits_first_set / bits_last_set answer for a zero word - one past
 *        the highest valid index, so it can never collide with a real bit.
 */
#define BITS_INDEX_NONE 64

/**
 * @def BITS_IN_BYTE
 * @brief Bits in a byte; equal to CHAR_BIT so the two cannot drift.
 */
#define BITS_IN_BYTE CHAR_BIT

/**
 * @def BITS_WORD_BITS
 * @brief Bits in one U64 word of a bit array: a global bit index maps to word
 *        bit / BITS_WORD_BITS, offset bit % BITS_WORD_BITS.
 */
#define BITS_WORD_BITS 64

/*==============================================================================
 * MARK: - API
 *============================================================================*/

/**
 * @brief Report whether any bit in a multi-word bit array is set.
 * @param self      Word array (U64 words); must not be NULL.
 * @param self_size Number of words; 0 answers false.
 * @return true when at least one bit is set.
 * @code
 * bool const pressed = bits_array_any(words, GUI_KEY_WORDS);
 * @endcode
 */
bool bits_array_any(U64 const *const self, USize const self_size);

/**
 * @brief Clear the bit at a global index within a multi-word bit array.
 * @param self      Word array (U64 words); must not be NULL.
 * @param self_size Number of words; the bit's word must lie below it.
 * @param bit       Global bit index.
 * @code
 * bits_array_clear(words, 3, 130);
 * @endcode
 */
void bits_array_clear(U64 *const self, USize const self_size, USize const bit);

/**
 * @brief Clear every bit of a multi-word bit array.
 * @param self      Word array (U64 words); must not be NULL.
 * @param self_size Number of words; 0 does nothing.
 * @code
 * bits_array_clear_all(words, GUI_KEY_WORDS);
 * @endcode
 */
void bits_array_clear_all(U64 *const self, USize const self_size);

/**
 * @brief Count the set bits of a multi-word bit array.
 * @param self      Word array (U64 words); must not be NULL.
 * @param self_size Number of words; 0 answers 0.
 * @return The number of set bits across every word.
 * @code
 * USize const held = bits_array_count(words, GUI_KEY_WORDS);
 * @endcode
 */
USize bits_array_count(U64 const *const self, USize const self_size);

/**
 * @brief Set the bit at a global index within a multi-word bit array.
 * @param self      Word array (U64 words); must not be NULL.
 * @param self_size Number of words; the bit's word must lie below it.
 * @param bit       Global bit index.
 * @code
 * bits_array_set(words, 3, 130);
 * @endcode
 */
void bits_array_set(U64 *const self, USize const self_size, USize const bit);

/**
 * @brief Test the bit at a global index within a multi-word bit array.
 * @param self      Word array (U64 words); must not be NULL.
 * @param self_size Number of words; the bit's word must lie below it.
 * @param bit       Global bit index.
 * @return true when the bit is set.
 * @code
 * bool const held = bits_array_test(words, 3, 130);
 * @endcode
 */
bool bits_array_test(U64 const *const self, USize const self_size, USize const bit);

/**
 * @brief Get the bit at an index.
 * @param self  The value to query.
 * @param index The bit index (0-63).
 * @return true when the bit is set.
 * @code
 * bool const third = bits_at(value, 3);
 * @endcode
 */
bool bits_at(USize const self, U8 const index);

/**
 * @brief Count the set bits of a value.
 * @param self The value to count.
 * @return The number of set bits (0-64).
 * @code
 * U8 const set = bits_count(value);
 * @endcode
 */
U8 bits_count(USize const self);

/**
 * @brief Index of the lowest set bit.
 * @param self The value to scan.
 * @return The index (0-63), or BITS_INDEX_NONE when no bit is set.
 * @code
 * U8 const lowest = bits_first_set(value);
 * @endcode
 */
U8 bits_first_set(USize const self);

/**
 * @brief Flip the bit at an index.
 * @param self  Pointer to the value to modify.
 * @param index The bit index (0-63).
 * @code
 * bits_flip(&value, 3);
 * @endcode
 */
void bits_flip(USize *const self, U8 const index);

/**
 * @brief Render the low self_size bits of a value into a buffer, most significant
 *        bit first, with a separator every group_size digits counted from the
 *        right - so `bits_format(9, 12, '-', 8, ...)` renders "0000-00001001".
 * @param self            The value to render.
 * @param self_size       Number of bits to render (1-64): bits 0 .. self_size - 1.
 * @param separator       Character placed between groups; '\0' means none.
 * @param group_size      Digits per group; 0 means none. A group_size of 0 and a
 *                        separator of '\0' are two spellings of the same thing,
 *                        so the rendering never embeds a terminator and the
 *                        return always equals its length.
 * @param buffer          Destination; receives the rendering and a terminator.
 * @param buffer_capacity Bytes available at buffer; BITS_FORMAT_CAPACITY fits any
 *                        rendering.
 * @return The number of characters written, not counting the terminator.
 * @code
 * char rendered[BITS_FORMAT_CAPACITY] = DEFAULT_INITIALIZATION;
 *
 * USize const length = bits_format(value, 8, ' ', 4, rendered, sizeof(rendered));
 * @endcode
 */
USize bits_format(USize const self, U8 const self_size, char const separator, U8 const group_size, char *const buffer, USize const buffer_capacity);

/**
 * @brief Index of the highest set bit.
 * @param self The value to scan.
 * @return The index (0-63), or BITS_INDEX_NONE when no bit is set.
 * @code
 * U8 const highest = bits_last_set(value);
 * @endcode
 */
U8 bits_last_set(USize const self);

/**
 * @brief Print the low self_size bits of a value to stdout, most significant bit
 *        first, a space every 8 digits, followed by a newline.
 * @param self      The value to print.
 * @param self_size Number of bits to print (1-64).
 * @code
 * bits_print_1(value, 8);   // "00001001"
 * @endcode
 */
void bits_print_1(USize const self, U8 const self_size);

/**
 * @brief Print the low self_size bits of a value to stdout, most significant bit
 *        first, a custom separator every 8 digits, followed by a newline.
 * @param self      The value to print.
 * @param self_size Number of bits to print (1-64).
 * @param separator Character placed every 8 digits; '\0' prints none.
 * @code
 * bits_print_2(value, 12, '-');   // "0000-00001001"
 * @endcode
 */
void bits_print_2(USize const self, U8 const self_size, char const separator);

/**
 * @brief Write a bit at an index.
 * @param self  Pointer to the value to modify.
 * @param index The bit index (0-63).
 * @param data  The bit to write.
 * @code
 * bits_write(&value, 7, true);
 * @endcode
 */
void bits_write(USize *const self, U8 const index, bool const data);

#endif // BITS_H