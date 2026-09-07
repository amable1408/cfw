/* ============================================================================
 *  Encoding Quoted-Printable
 *  --------------------------------------------------------------------------
 *  @file    quoted_printable.h
 *  @brief   Quoted-Printable encoder — RFC 2045 §6.7, MIME body transfer
 *           encoding for mostly-text data.
 *  @author  CFW
 *  @date    2026-09-06
 *  @version 0.1.0
 *  @license MIT (see LICENSE file)
 *
 *  General-purpose Quoted-Printable encoder for the framework. It is the
 *  transfer encoding a mail body wants when the text is mostly US-ASCII: unlike
 *  base64 it stays human-readable, costs ~3 bytes only for the bytes that
 *  actually need escaping, and — the reason a mail module needs it at all — it
 *  guarantees no output line exceeds 76 characters and no line ends in a space
 *  or tab, the two shapes RFC 5322 receivers reject or silently rewrite.
 *
 *  Only the encoding direction is offered. A decoder has no caller in this tree
 *  yet, and a half-used decoder is a liability rather than a feature; it can be
 *  added as encoding_quoted_printable_decode_1 when one appears.
 *
 *  What the encoder emits, byte by byte:
 *    - Printable US-ASCII (0x21-0x7E) passes through verbatim, EXCEPT '='
 *      (0x3D) which is the escape character and is always written "=3D".
 *    - A space (0x20) or tab (0x09) passes through verbatim UNLESS it is the
 *      last byte of the input or sits immediately before a line break, in which
 *      case it is escaped ("=20" / "=09"). A receiver's transport is allowed to
 *      strip trailing whitespace, so an unescaped one is data loss.
 *    - A CR LF PAIR is a hard line break and is emitted verbatim as CR LF; the
 *      line-length count restarts. A LONE CR or LONE LF is not a line break in
 *      this encoding and is escaped ("=0D" / "=0A") — feed CRLF-normalized text
 *      if the input's bare newlines are meant to survive as breaks.
 *    - Every other byte (controls, 0x7F, and everything >= 0x80, so UTF-8 text
 *      too) is escaped as '=' plus two UPPERCASE hex digits.
 *    - A soft line break "=\r\n" is inserted whenever the next token would push
 *      the current line past ENCODING_QUOTED_PRINTABLE_LINE_MAX_LENGTH. An
 *      "=XX" triplet is never split across one, and a literal space or tab is
 *      never left as a line's last character.
 *
 *  The encoder is deterministic and stateless: the same input always produces
 *  the same output, which is what makes it usable under a golden test.
 *
 *  Usage Examples:
 *    @code
 *    #include <encoding/quoted_printable/quoted_printable.h>
 *
 *    char const *const text = "Sábado 12:00 ";
 *    USize const text_size  = char_length(text);
 *
 *    USize const needed  = encoding_quoted_printable_encode_size(text, text_size);
 *    char *const output  = (char*) memory_alloc(needed + CHAR_END_CHARACTER);
 *    USize encoded_size  = 0;
 *
 *    if (result_is_success(encoding_quoted_printable_encode_1(text, text_size, output, needed, &encoded_size))) {
 *        output[encoded_size] = '\0'; // "S=C3=A1bado 12:00=20"
 *    }
 *    @endcode
 *
 *    @code
 *    String body = string_init_1();
 *
 *    encoding_quoted_printable_encode_2("line one\r\nline two", 18, &body);
 *    @endcode
 *
 *  Error Handling:
 *    - A null pointer is a contract violation and aborts via error_check_null:
 *      input for every function, output plus encoded_size for _1, out for _2.
 *      A null input aborts even at size 0, matching encoding/base64.
 *    - Size 0 is a legal VALUE in every direction: an empty input encodes to an
 *      empty output, writes 0 to encoded_size, and answers success. Nothing in
 *      this module treats a data-dependent condition as an abort.
 *    - encoding_quoted_printable_encode_1 answers RESULT_CATEGORY_ARGUMENT as a
 *      VALUE refusal (never an abort) when output_capacity is smaller than the
 *      encoding needs. The capacity is checked BEFORE each write, so a short
 *      buffer can never be overrun; on refusal the output contents are
 *      unspecified and encoded_size is untouched. Size the buffer with
 *      encoding_quoted_printable_encode_size, which answers the EXACT count.
 *    - encoding_quoted_printable_encode_2 answers false when the String
 *      allocator refuses to grow; the sink then holds a TRUNCATED prefix of the
 *      encoding and must not be sent.
 *    - The input need not be NUL-terminated: exactly input_size bytes are read,
 *      and an embedded NUL is encoded as "=00" like any other control byte.
 *    - The input and output buffers must not overlap.
 *
 *  Thread Safety:
 *    - Stateless; every function is safe to call concurrently. _2 is safe only
 *      as far as the caller's own String is: two threads must not append to one
 *      sink.
 *
 *  Memory Management:
 *    - No heap allocation in _1: the caller owns the output buffer. _2 grows
 *      the caller's String through its own allocator (arena-backed Strings
 *      included) and never takes ownership of it.
 *
 *  Performance Characteristics:
 *    - O(n) single pass, one byte of lookahead, no allocation and no branching
 *      on anything but the current byte. encoding_quoted_printable_encode_size
 *      runs the SAME pass with the writes suppressed, so sizing and encoding
 *      can never disagree; a caller that needs both pays the input twice.
 *
 *  Dependencies (Deps):
 *    - error (error.h — chains tracelog → log → thread, which provide
 *      types.h).
 *    - result.h, included directly: the Result return type must not depend on
 *      log.h's LOG_THREAD_IMPLEMENTATION-only edge to thread.h.
 *    - container/string (string.h), for the _2 sink only.
 * ============================================================================
 */
#ifndef ENCODING_QUOTED_PRINTABLE_H
#define ENCODING_QUOTED_PRINTABLE_H

#include <result.h>
#include <container/string/string.h>
#include <error/error.h>

/*==============================================================================
 * MARK: - Macros
 *============================================================================*/

/** @brief Characters an encoded line may hold, soft break included (RFC 2045). */
#define ENCODING_QUOTED_PRINTABLE_LINE_MAX_LENGTH 76

/*==============================================================================
 * MARK: - Public Functions
 *============================================================================*/

/**
 * @brief Encode bytes as Quoted-Printable into a caller buffer.
 * @param input           Bytes to encode; exactly input_size bytes are read (no
 *                        terminator needed).
 * @param input_size      Length of input in bytes; 0 is legal.
 * @param output          Destination buffer; NOT NUL-terminated by this call.
 * @param output_capacity Capacity of output in characters.
 * @param encoded_size    Receives the written character count on success;
 *                        untouched on refusal.
 * @return RESULT_SUCCESS, or RESULT_CATEGORY_ARGUMENT when output_capacity is
 *         too small for the encoding.
 */
Result encoding_quoted_printable_encode_1(char const *const input, USize const input_size, char *const output, USize const output_capacity, USize *const encoded_size);

/**
 * @brief Encode bytes as Quoted-Printable and APPEND them to a String.
 * @param input      Bytes to encode; exactly input_size bytes are read.
 * @param input_size Length of input in bytes; 0 is legal and appends nothing.
 * @param out        String the encoding is appended to; existing content is
 *                   kept, and the line-length count starts fresh at 0 (pass a
 *                   sink whose last character is a line break, or accept that
 *                   the first encoded line continues an existing one).
 * @return true when the whole encoding was appended. False means the String's
 *         allocator refused mid-way and `out` now holds a TRUNCATED encoding.
 */
bool encoding_quoted_printable_encode_2(char const *const input, USize const input_size, String *const out);

/**
 * @brief Report the EXACT character count encoding input would produce.
 * @param input      Bytes that would be encoded.
 * @param input_size Length of input in bytes; 0 answers 0.
 * @return Characters encoding_quoted_printable_encode_1 would write, excluding
 *         any terminator (add CHAR_END_CHARACTER to NUL-terminate).
 */
USize encoding_quoted_printable_encode_size(char const *const input, USize const input_size);

#endif // ENCODING_QUOTED_PRINTABLE_H