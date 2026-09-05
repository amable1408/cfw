/* ============================================================================
 *  Encoding Base64
 *  --------------------------------------------------------------------------
 *  @file    base64.h
 *  @brief   Base64 codec — standard (RFC 4648 §4, padded) and URL-safe
 *           (RFC 4648 §5, unpadded) alphabets.
 *  @author  CFW
 *  @date    2026-08-09
 *  @license MIT (see LICENSE file)
 *
 *  General-purpose base64 codec for the framework. This is a plain data codec
 *  with no cryptographic dependency. Two alphabets are offered: the standard
 *  padded form (`+` `/` `=`) for interchange formats and HTTP bodies, and the
 *  URL-safe unpadded form (`-` `_`, no `=`) for web-safe tokens, cookies, and
 *  URL path/query components.
 *
 *  Features:
 *    - encoding_base64_encode_1 / encoding_base64_decode_1: standard padded
 *      alphabet, strict decode.
 *    - encoding_base64_url_encode_1 / encoding_base64_url_decode_1: URL-safe
 *      unpadded alphabet, strict decode (padding rejected).
 *    - Size helpers for exact (encode) and upper-bound (decode) buffer sizing.
 *
 *  Usage Examples:
 *    @code
 *    #include <char/char.h>             // CHAR_END_CHARACTER
 *    #include <encoding/base64/base64.h>
 *
 *    U8 const key[6]                                                 = { 1, 2, 3, 4, 5, 6 };
 *    char text[ENCODING_BASE64_ENCODE_SIZE(6) + CHAR_END_CHARACTER]  = DEFAULT_INITIALIZATION;
 *
 *    encoding_base64_encode_1(key, sizeof(key), text); // "AQIDBAUG"
 *    @endcode
 *
 *  Error Handling:
 *    - A null pointer is a contract violation in BOTH directions and aborts via
 *      error_check_null, even at size 0: bytes/output for the encoders,
 *      input/output/decoded_size for the decoders. Encoding cannot otherwise
 *      fail; every byte sequence has an encoding.
 *    - Decoding returns RESULT_SUCCESS, or RESULT_CATEGORY_ARGUMENT as a VALUE
 *      refusal (never an abort) when the input length is invalid for the
 *      alphabet, a character is outside the alphabet, padding is malformed
 *      (standard) or present (URL-safe), a partial final group carries nonzero
 *      unused trailing bits (non-canonical encodings are rejected so distinct
 *      texts never decode to the same bytes), or the decoded size exceeds
 *      output_capacity; output contents are then unspecified and must not be
 *      used, and decoded_size is untouched. The input need not be
 *      NUL-terminated - exactly input_size characters are read, and an
 *      embedded NUL is refused as a character outside the alphabet.
 *    - Size 0 is a legal value for both directions (empty input encodes to ""
 *      and decodes to zero bytes) per the framework-wide empty-value policy.
 *    - The source and destination buffers must not overlap.
 *    - With ERROR_CHECK_ENABLED compiled out the null checks disappear and a
 *      null pointer is undefined behaviour, while every decode refusal is a
 *      runtime branch that holds in every build - the capacity is checked
 *      before the first byte is written - so remote text cannot make either
 *      decoder read or write past a buffer.
 *
 *  Thread Safety:
 *    - Stateless; every function is safe to call concurrently.
 *
 *  Memory Management:
 *    - No heap allocation. The caller owns every buffer; encoders need the
 *      matching *_ENCODE_SIZE macro's chars + 1 for the NUL.
 *
 *  Performance Characteristics:
 *    - O(n) single pass in both directions over small lookup tables.
 *
 *  Dependencies (Deps):
 *    - error (error.h — chains tracelog → log → thread, which provide
 *      types.h).
 *    - result.h, included directly: the Result return type must not depend on
 *      log.h's LOG_THREAD_IMPLEMENTATION-only edge to thread.h.
 * ============================================================================
 */
#ifndef ENCODING_BASE64_H
#define ENCODING_BASE64_H

#include <result.h>
#include <error/error.h>

/*==============================================================================
 * MARK: - Macros
 *============================================================================*/

/** @brief Upper bound on decoded bytes for input_size chars (both alphabets). */
#define ENCODING_BASE64_DECODE_SIZE(input_size) ((((input_size) + ENCODING_BASE64_GROUP_CHAR_COUNT - 1) / ENCODING_BASE64_GROUP_CHAR_COUNT) * ENCODING_BASE64_GROUP_BYTE_COUNT)

/** @brief Standard padded chars for byte_count bytes (excl. NUL). */
#define ENCODING_BASE64_ENCODE_SIZE(byte_count) ((((byte_count) + ENCODING_BASE64_GROUP_BYTE_COUNT - 1) / ENCODING_BASE64_GROUP_BYTE_COUNT) * ENCODING_BASE64_GROUP_CHAR_COUNT)

#define ENCODING_BASE64_GROUP_BYTE_COUNT 3 /**< Raw bytes per base64 group. */
#define ENCODING_BASE64_GROUP_CHAR_COUNT 4 /**< Encoded chars per base64 group. */

/** @brief URL-safe unpadded chars for byte_count bytes (excl. NUL). */
/* Divides before multiplying: whole groups give 4 chars each, a 1- or 2-byte
 * tail gives one char more than its bytes. The multiply-first form wrapped past
 * USIZE_MAX / 4 (1 GiB on a 32-bit ABI); this one wraps only above 3 * 2^62
 * bytes, an unallocatable size. This macro evaluates its argument three times
 * (the other two once), so pass a side-effect-free expression. */
#define ENCODING_BASE64_URL_ENCODE_SIZE(byte_count)                                        \
    ((((byte_count) / ENCODING_BASE64_GROUP_BYTE_COUNT) * ENCODING_BASE64_GROUP_CHAR_COUNT) \
     + ((byte_count) % ENCODING_BASE64_GROUP_BYTE_COUNT == 0                                \
            ? 0                                                                             \
            : (byte_count) % ENCODING_BASE64_GROUP_BYTE_COUNT + 1))

/*==============================================================================
 * MARK: - Public Functions
 *============================================================================*/

/**
 * @brief Decode standard padded base64 text into bytes (strict).
 * @param input           Base64 text; exactly input_size chars are read (no
 *                        terminator needed); the length must be a multiple of
 *                        4 with 0, 1 or 2 trailing '='.
 * @param input_size      Length of input in chars.
 * @param output          Destination buffer.
 * @param output_capacity Capacity of output in bytes.
 * @param decoded_size    Receives the decoded byte count on success;
 *                        untouched on failure.
 * @return RESULT_SUCCESS, or RESULT_CATEGORY_ARGUMENT on invalid length,
 *         character, padding, nonzero unused trailing bits in a partial final
 *         group, or insufficient capacity (output contents are then unspecified
 *         and must not be used). A null pointer aborts; it is not a refusal.
 */
CFW_ATTR_NODISCARD
Result encoding_base64_decode_1(char const *const input, USize const input_size, U8 *const output, USize const output_capacity, USize *const decoded_size);

/**
 * @brief Encode bytes as standard padded base64, NUL-terminated.
 * @param bytes  Source bytes; non-null even when size is 0.
 * @param size   Number of bytes to encode.
 * @param output Destination; must hold ENCODING_BASE64_ENCODE_SIZE(size) + 1 chars and not overlap bytes.
 */
void encoding_base64_encode_1(U8 const *const bytes, USize const size, char *const output);

/**
 * @brief Decode URL-safe unpadded base64 text into bytes (strict; padding rejected).
 * @param input           Base64url text; exactly input_size chars are read (no
 *                        terminator needed); the length mod 4 must not be 1.
 * @param input_size      Length of input in chars.
 * @param output          Destination buffer.
 * @param output_capacity Capacity of output in bytes.
 * @param decoded_size    Receives the decoded byte count on success;
 *                        untouched on failure.
 * @return RESULT_SUCCESS, or RESULT_CATEGORY_ARGUMENT on invalid length,
 *         character (including '=' padding), nonzero unused trailing bits in
 *         a partial final group, or insufficient capacity (output contents are
 *         then unspecified and must not be used). A null pointer aborts.
 */
CFW_ATTR_NODISCARD
Result encoding_base64_url_decode_1(char const *const input, USize const input_size, U8 *const output, USize const output_capacity, USize *const decoded_size);

/**
 * @brief Encode bytes as URL-safe unpadded base64, NUL-terminated.
 * @param bytes  Source bytes; non-null even when size is 0.
 * @param size   Number of bytes to encode.
 * @param output Destination; must hold ENCODING_BASE64_URL_ENCODE_SIZE(size) + 1 chars and not overlap bytes.
 */
void encoding_base64_url_encode_1(U8 const *const bytes, USize const size, char *const output);

#endif // ENCODING_BASE64_H