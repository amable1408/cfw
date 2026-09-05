/* ============================================================================
 *  Encoding Hex
 *  --------------------------------------------------------------------------
 *  @file    hex.h
 *  @brief   Hexadecimal codec — bytes to hex text and back.
 *  @author  CFW
 *  @date    2026-08-09
 *  @license MIT (see LICENSE file)
 *
 *  General-purpose hex codec for the framework. This is a plain data codec
 *  with no cryptographic dependency: any caller that needs bytes rendered as
 *  hex (digests, tokens, ids, debug dumps) or hex text parsed back into bytes
 *  should use it instead of hand-rolling an encoder.
 *
 *  Features:
 *    - encoding_hex_encode_1/_2: lowercase (or uppercase) hex of a byte
 *      buffer, NUL-terminated.
 *    - encoding_hex_decode_1: strict decode accepting mixed-case digits.
 *
 *  Usage Examples:
 *    @code
 *    #include <char/char.h>          // CHAR_END_CHARACTER
 *    #include <encoding/hex/hex.h>
 *
 *    U8 const digest[4]                                  = { 0xde, 0xad, 0xbe, 0xef };
 *    char hex[ENCODING_HEX_SIZE(4) + CHAR_END_CHARACTER] = DEFAULT_INITIALIZATION;
 *
 *    encoding_hex_encode_1(digest, sizeof(digest), hex); // "deadbeef"
 *    @endcode
 *
 *  Error Handling:
 *    - A null pointer is a contract violation in BOTH directions and aborts via
 *      error_check_null, even at size 0: bytes/output for encode, input/output
 *      for decode. Encoding cannot otherwise fail; every byte has a hex form.
 *    - Decoding returns RESULT_SUCCESS, or RESULT_CATEGORY_ARGUMENT as a VALUE
 *      refusal (never an abort) when output_size exceeds USIZE_MAX / 2 (the
 *      expected length would wrap), when input_size is not exactly
 *      output_size * 2, or when any character is not a hex digit; output
 *      contents are then unspecified and must not be used. The input need not
 *      be NUL-terminated - exactly input_size characters are read, and an
 *      embedded NUL is refused as a non-digit.
 *    - Size 0 is a legal value for both directions (empty input encodes to ""
 *      and decodes to zero bytes) per the framework-wide empty-value policy.
 *    - The source and destination buffers must not overlap.
 *    - With ERROR_CHECK_ENABLED compiled out the null checks disappear and a
 *      null pointer is undefined behaviour (a read or write through it), while
 *      the three decode refusals are runtime branches that hold in every build,
 *      so remote hex text still cannot make the decoder read or write past a
 *      buffer.
 *
 *  Thread Safety:
 *    - Stateless; every function is safe to call concurrently.
 *
 *  Memory Management:
 *    - No heap allocation. The caller owns every buffer; encoding needs
 *      ENCODING_HEX_SIZE(size) + 1 chars (hex digits plus the NUL).
 *
 *  Performance Characteristics:
 *    - O(n) single pass in both directions, no heap: encode indexes a 16-char
 *      digit table, decode compares each character against the digit ranges.
 *
 *  Dependencies (Deps):
 *    - error (error.h — chains tracelog → log → thread, which provide
 *      types.h).
 *    - result.h, included directly: the Result return type must not depend on
 *      log.h's LOG_THREAD_IMPLEMENTATION-only edge to thread.h.
 * ============================================================================
 */
#ifndef ENCODING_HEX_H
#define ENCODING_HEX_H

#include <result.h>
#include <error/error.h>

/*==============================================================================
 * MARK: - Macros
 *============================================================================*/

#define ENCODING_HEX_DIGITS_PER_BYTE 2                                              /**< Hex digits produced per encoded byte. */
#define ENCODING_HEX_SIZE(byte_count) ((byte_count) * ENCODING_HEX_DIGITS_PER_BYTE) /**< Hex chars for byte_count bytes (excl. NUL). */

/*==============================================================================
 * MARK: - Public Functions
 *============================================================================*/

/**
 * @brief Decode hex text into bytes (strict, mixed-case digits accepted).
 * @param input       Hex text; exactly input_size chars are read (no terminator needed).
 * @param input_size  Length of input in chars.
 * @param output      Destination buffer of output_size bytes.
 * @param output_size Number of bytes to decode.
 * @return RESULT_SUCCESS, or RESULT_CATEGORY_ARGUMENT when output_size exceeds
 *         USIZE_MAX / 2, input_size is not output_size * 2, or a character is
 *         not a hex digit (output contents are then unspecified and must not
 *         be used). A null input or output aborts; it is not a refusal.
 */
CFW_ATTR_NODISCARD
Result encoding_hex_decode_1(char const *const input, USize const input_size, U8 *const output, USize const output_size);

/**
 * @brief Encode bytes as lowercase hex, NUL-terminated.
 * @param bytes  Source bytes; non-null even when size is 0.
 * @param size   Number of bytes to encode.
 * @param output Destination; must hold ENCODING_HEX_SIZE(size) + 1 chars and not overlap bytes.
 */
void encoding_hex_encode_1(U8 const *const bytes, USize const size, char *const output);

/**
 * @brief Encode bytes as hex with selectable case, NUL-terminated.
 * @param bytes  Source bytes; non-null even when size is 0.
 * @param size   Number of bytes to encode.
 * @param output Destination; must hold ENCODING_HEX_SIZE(size) + 1 chars and not overlap bytes.
 * @param upper  true for uppercase digits, false for lowercase.
 */
void encoding_hex_encode_2(U8 const *const bytes, USize const size, char *const output, bool const upper);

#endif // ENCODING_HEX_H