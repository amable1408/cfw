/*
 * regex.h - PCRE2-based regular expression wrapper for the C Libraries Framework
 *
 * Features:
 *   - Compile and match regular expressions using PCRE2
 *   - Supports C string, Str, and String types for patterns and subjects
 *   - Non-aborting compile_try_* family for patterns that arrive as data
 *   - Provides match data, offsets, error accessors, and callback-based match iteration
 *
 * Usage Examples:
 *   @code
 *   Regex re = regex_init();
 *   regex_compile_1(&re, "[a-z]+\\d");
 *   if (regex_match_1(&re, "abc123", 0)) {
 *       USize begin = regex_get_match_begin(&re);
 *       USize size  = regex_get_match_size(&re);
 *   }
 *   regex_uninit(&re);
 *   @endcode
 *
 * Error Handling:
 *   The compile_1..4 family aborts on a malformed pattern (checked builds) - it
 *   exists for LITERAL patterns, where failure is caller error. Patterns that
 *   arrive as DATA (user input, config rows) go through compile_try / the
 *   compile_try_2..4 twins, which return false and leave the object REFUSING:
 *   every match on it answers false and every capture accessor fails cleanly,
 *   in every build. regex_get_error_message renders the reason.
 *
 * Thread Safety:
 *   Not thread-safe. Caller must synchronize if used from multiple threads.
 *
 * Memory Management:
 *   The Regex owns its compiled code and match data; regex_uninit releases both.
 *   Recompiling into a live Regex releases the previous compilation first.
 *   PCRE2 copies the pattern during compile, so the caller's pattern buffer is
 *   never retained.
 *
 * Performance Characteristics:
 *   Compilation is the expensive step; compile once and match many times.
 *
 * Dependencies:
 *   - PCRE2 (8-bit code units)
 *   - container/string (String, Str), result.h
 *
 * See regex.c for implementation details.
 */
#ifndef REGEX_H
#define REGEX_H

#define PCRE2_CODE_UNIT_WIDTH 8

#include <pcre2.h>

#include <container/string/string.h>
/* Direct, not transitive: the copy_match_group API returns Result, but this header only
 * received the type through log.h -> thread.h -> result.h, an edge log.h has ONLY under
 * LOG_THREAD_IMPLEMENTATION - so any build without that switch (vestigo's release build
 * was the first) failed to compile regex.h at all. */
#include <result.h>

/*==============================================================================
 * MARK: - Macros and Constants
 *============================================================================*/
/** @brief Case-insensitive matching (PCRE2_CASELESS) - what vestigo's "(?i)" splice hand-rolled. */
#define REGEX_COMPILE_CASELESS PCRE2_CASELESS
/** @brief '.' also matches newlines (PCRE2_DOTALL). */
#define REGEX_COMPILE_DOTALL PCRE2_DOTALL
/** @brief '^'/'$' match at internal newlines (PCRE2_MULTILINE). */
#define REGEX_COMPILE_MULTILINE PCRE2_MULTILINE
/** @brief Buffer size that fits every PCRE2 error message. */
#define REGEX_ERROR_MESSAGE_SIZE 256

/*==============================================================================
 * MARK: - Typedefs
 *============================================================================*/
/**
 * @brief PCRE2-based regular expression object.
 */
typedef struct {
    I32                 error_code;      /**< Negative PCRE2 code of the last compile OR match error (no-match excluded) */
    PCRE2_SIZE          error_offset;    /**< Pattern offset of the last compile error */
    struct {
        USize   begin;                  /**< Start offset of match */
        USize   size;                   /**< Size of match */
        USize   end;                    /**< End offset of match */
    }                   match;
    bool                matched;         /**< True once a match SUCCEEDED on the current compilation; the capture accessors
                                              refuse until then (a compiled-but-unmatched match_data is uninitialized PCRE2 heap) */
    pcre2_match_data    *match_data;     /**< PCRE2 match data */
    pcre2_code          *re;             /**< Compiled PCRE2 regex */
} Regex;

/**
 * @brief Regex match callback type. `context` is the caller's pointer, passed
 *        through regex_match_all_* verbatim.
 */
typedef void (*FpRegexMatch)(Regex const *const self, char const *const subject, USize const subject_size, void *const context);

/*==============================================================================
 * MARK: - Compilation
 *============================================================================*/
/**
 * @brief Compile regex from C string (zero-terminated).
 *        For LITERAL patterns: aborts on a malformed pattern in checked builds
 *        (caller error). A pattern that arrives as data belongs in regex_compile_try.
 * @param self Regex pointer.
 * @param pattern Pattern string. An empty pattern is legal (matches everywhere).
 */
void regex_compile_1(Regex *const self, char const *const pattern);

/**
 * @brief Compile regex from C string with explicit size.
 * @param self Regex pointer.
 * @param pattern Pattern string.
 * @param pattern_size Length of pattern in bytes. 0 is legal (the empty pattern).
 */
void regex_compile_2(Regex *const self, char const *const pattern, USize const pattern_size);

/**
 * @brief Compile regex from Str object.
 * @param self Regex pointer.
 * @param pattern Str pattern (may be empty).
 */
void regex_compile_3(Regex *const self, Str const *const pattern);

/**
 * @brief Compile regex from String object.
 * @param self Regex pointer.
 * @param pattern String pattern (may be empty).
 */
void regex_compile_4(Regex *const self, String const *const pattern);

/**
 * @brief Compile regex from a C string, returning false instead of aborting when
 *        the pattern is malformed (unlike regex_compile_1). PCRE2 copies the
 *        pattern internally, so the Regex never retains or frees the caller's
 *        buffer. On false the Regex REFUSES: matching it answers false and the
 *        capture accessors fail cleanly, in every build; regex_uninit is safe.
 *        regex_get_error_message renders the failure reason.
 * @param self Regex pointer.
 * @param pattern Pattern string (zero-terminated).
 * @return True when the pattern compiled.
 */
bool regex_compile_try(Regex *const self, char const *const pattern);

/**
 * @brief Compile regex from a sized C string, non-aborting (see regex_compile_try).
 * @param self Regex pointer.
 * @param pattern Pattern string.
 * @param pattern_size Length of pattern in bytes. 0 is legal (the empty pattern).
 * @return True when the pattern compiled.
 */
bool regex_compile_try_2(Regex *const self, char const *const pattern, USize const pattern_size);

/**
 * @brief Compile regex from a Str, non-aborting (see regex_compile_try).
 * @param self Regex pointer.
 * @param pattern Str pattern (may be empty).
 * @return True when the pattern compiled.
 */
bool regex_compile_try_3(Regex *const self, Str const *const pattern);

/**
 * @brief Compile regex from a String, non-aborting (see regex_compile_try).
 * @param self Regex pointer.
 * @param pattern String pattern (may be empty).
 * @return True when the pattern compiled.
 */
bool regex_compile_try_4(Regex *const self, String const *const pattern);

/**
 * @brief Compile regex with REGEX_COMPILE_* options, non-aborting (see
 *        regex_compile_try). REGEX_COMPILE_CASELESS replaces hand-splicing
 *        "(?i)" into the pattern text.
 * @param self Regex pointer.
 * @param pattern Pattern string (zero-terminated).
 * @param options Bitwise OR of REGEX_COMPILE_* flags.
 * @return True when the pattern compiled.
 */
bool regex_compile_try_options(Regex *const self, char const *const pattern, U32 const options);

/**
 * @brief Copy the named capture group of the last match into a caller buffer.
 * @param self Regex pointer. Refuses (failure Result) when never matched or compile failed.
 * @param name Group name.
 * @param buffer Destination buffer.
 * @param buffer_size In: buffer capacity in bytes. Out: substring length.
 * @return RESULT_SUCCESS, or a failure whose code is the PCRE2 error's magnitude.
 */
Result regex_copy_match_group_name_1(Regex *const self, char const *const name, char *const buffer, USize *const buffer_size);

/**
 * @brief Copy the named capture group into a pre-sized Str.
 * @param self Regex pointer. Refuses (failure Result) when never matched or compile failed.
 * @param name Group name.
 * @param buffer Pre-sized Str; its size is the usable capacity on entry and the
 *               substring length on success (unchanged on failure).
 * @return RESULT_SUCCESS, or a failure whose code is the PCRE2 error's magnitude.
 */
Result regex_copy_match_group_name_2(Regex *const self, char const *const name, Str *const buffer);

/**
 * @brief Copy the named capture group into a String.
 * @param self Regex pointer. Refuses (failure Result) when never matched or compile failed.
 * @param buffer String; its CAPACITY bounds the copy, its size is set to the
 *               substring length on success (unchanged on failure).
 * @param name Group name.
 * @return RESULT_SUCCESS, or a failure whose code is the PCRE2 error's magnitude.
 */
Result regex_copy_match_group_name_3(Regex *const self, char const *const name, String *const buffer);

/**
 * @brief Copy the numbered capture group into a caller buffer (0 = whole match).
 * @param self Regex pointer. Refuses (failure Result) when never matched or compile failed.
 * @param index Group number.
 * @param buffer Destination buffer.
 * @param buffer_size In: buffer capacity in bytes. Out: substring length.
 * @return RESULT_SUCCESS, or a failure whose code is the PCRE2 error's magnitude.
 */
Result regex_copy_match_group_number_1(Regex *const self, USize const index, char *const buffer, USize *const buffer_size);

/**
 * @brief Copy the numbered capture group into a pre-sized Str (see the name_2 form).
 * @param self Regex pointer.
 * @param index Group number (0 = whole match).
 * @param buffer Pre-sized Str (size = capacity in, length out).
 * @return RESULT_SUCCESS, or a failure whose code is the PCRE2 error's magnitude.
 */
Result regex_copy_match_group_number_2(Regex *const self, USize const index, Str *const buffer);

/**
 * @brief Copy the numbered capture group into a String (see the name_3 form).
 * @param self Regex pointer.
 * @param index Group number (0 = whole match).
 * @param buffer String (capacity bounds the copy, size set on success).
 * @return RESULT_SUCCESS, or a failure whose code is the PCRE2 error's magnitude.
 */
Result regex_copy_match_group_number_3(Regex *const self, USize const index, String *const buffer);

/*==============================================================================
 * MARK: - Match Data Access
 *============================================================================*/
/**
 * @brief Render the last compile or match error as text.
 * @param self Regex pointer.
 * @param buffer Destination buffer (REGEX_ERROR_MESSAGE_SIZE always fits).
 * @param buffer_size Buffer capacity in bytes.
 * @return True when a message was written.
 */
bool regex_get_error_message(Regex const *const self, char *const buffer, USize const buffer_size);

/**
 * @brief Get the pattern offset of the last compile error.
 * @param self Regex pointer.
 * @return Byte offset into the pattern; meaningful only after a failed compile.
 */
USize regex_get_error_offset(Regex const *const self);

/**
 * @brief Get start offset of last match.
 * @param self Regex pointer.
 * @return Start offset. Valid only after a regex_match_* returned true.
 */
USize regex_get_match_begin(Regex *const self);

/**
 * @brief Get end offset of last match.
 * @param self Regex pointer.
 * @return End offset (exclusive). Valid only after a regex_match_* returned true.
 */
USize regex_get_match_end(Regex *const self);

/**
 * @brief Get the captured group by name.
 * @param self Regex pointer. A never-matched or failed-compile object refuses
 *             (nullptr / the empty object).
 * @param name Name of the group.
 * @return Captured group. The char* variant returns a framework-owned heap copy
 *         the caller must free with char_delete; the Str/String variants own
 *         their buffer and are released with str_uninit/string_uninit.
 */
char* regex_get_match_group_name_1(Regex *const self, char const *const name);
Str regex_get_match_group_name_2(Regex *const self, char const *const name);
String regex_get_match_group_name_3(Regex *const self, char const *const name);

/**
 * @brief Get the captured group by number (0 = whole match).
 * @param self Regex pointer. A never-matched or failed-compile object refuses
 *             (nullptr / the empty object).
 * @param index Group number.
 * @return Captured group. Ownership is as in the name variants above (char* is a
 *         heap copy freed with char_delete; Str/String are released with
 *         str_uninit/string_uninit).
 */
char* regex_get_match_group_number_1(Regex *const self, USize const index);
Str   regex_get_match_group_number_2(Regex *const self, USize const index);
String regex_get_match_group_number_3(Regex *const self, USize const index);

/**
 * @brief Get size of last match.
 * @param self Regex pointer.
 * @return Size of match in bytes. Valid only after a regex_match_* returned true.
 */
USize regex_get_match_size(Regex *const self);

/*==============================================================================
 * MARK: - Initialization & Cleanup
 *============================================================================*/
/**
 * @brief Initialize a Regex object (zeroed fields). The fresh object REFUSES all
 *        matching until a compile succeeds.
 * @return Initialized Regex struct.
 */
Regex regex_init(void);

/**
 * @brief Free all resources associated with a Regex object.
 * @param self Regex pointer.
 */
void regex_uninit(Regex *const self);

/*==============================================================================
 * MARK: - Single Match
 *============================================================================*/
/**
 * @brief Match regex against C string (zero-terminated).
 * @param self Regex pointer. A failed-compile or fresh object answers false.
 * @param subject Subject string. Must not be nullptr; may be empty.
 * @param subject_offset Offset to start matching. Past-the-end answers false
 *                       (it is data - routinely a previous match's end).
 * @return true if match found, false otherwise.
 */
bool regex_match_1(Regex *const self, char const *const subject, USize const subject_offset);

/**
 * @brief Match regex against C string with explicit size.
 * @param self Regex pointer. A failed-compile or fresh object answers false.
 * @param subject Subject string. Must not be nullptr; size 0 is legal.
 * @param subject_size Length of subject.
 * @param subject_offset Offset to start matching. Past-the-end answers false.
 * @return true if match found, false otherwise.
 */
bool regex_match_2(Regex *const self, char const *const subject, USize const subject_size, USize const subject_offset);

/**
 * @brief Match regex against Str object.
 * @param self Regex pointer. A failed-compile or fresh object answers false.
 * @param subject Str subject (may be empty).
 * @param subject_offset Offset to start matching. Past-the-end answers false.
 * @return true if match found, false otherwise.
 */
bool regex_match_3(Regex *const self, Str *const subject, USize const subject_offset);

/**
 * @brief Match regex against String object.
 * @param self Regex pointer. A failed-compile or fresh object answers false.
 * @param subject String subject (may be empty).
 * @param subject_offset Offset to start matching. Past-the-end answers false.
 * @return true if match found, false otherwise.
 */
bool regex_match_4(Regex *const self, String *const subject, USize const subject_offset);

/*==============================================================================
 * MARK: - Match All (Callback Iteration)
 *============================================================================*/
/**
 * @brief Match all occurrences in C string (zero-terminated), invoking callback for each.
 *        After a zero-length match the PCRE2-documented anchored non-empty retry
 *        runs before advancing, so a non-empty match at the same position is not skipped.
 * @param self Regex pointer. A failed-compile or fresh object answers 0.
 * @param subject Subject string. Must not be nullptr; may be empty.
 * @param subject_offset Offset to start matching. Past-the-end answers 0.
 * @param callback Callback function.
 * @param context Caller pointer handed to the callback verbatim (may be nullptr).
 * @return Number of matches found.
 */
USize regex_match_all_1(Regex *const self, char const *const subject, USize const subject_offset, FpRegexMatch const callback, void *const context);

/**
 * @brief Match all occurrences in C string with explicit size, invoking callback for each.
 * @param self Regex pointer. A failed-compile or fresh object answers 0.
 * @param subject Subject string. Must not be nullptr; size 0 is legal.
 * @param subject_size Length of subject.
 * @param subject_offset Offset to start matching. Past-the-end answers 0.
 * @param callback Callback function.
 * @param context Caller pointer handed to the callback verbatim (may be nullptr).
 * @return Number of matches found.
 */
USize regex_match_all_2(Regex *const self, char const *const subject, USize const subject_size, USize const subject_offset, FpRegexMatch const callback, void *const context);

/**
 * @brief Match all occurrences in Str object, invoking callback for each.
 * @param self Regex pointer. A failed-compile or fresh object answers 0.
 * @param subject Str subject (may be empty).
 * @param subject_offset Offset to start matching. Past-the-end answers 0.
 * @param callback Callback function.
 * @param context Caller pointer handed to the callback verbatim (may be nullptr).
 * @return Number of matches found.
 */
USize regex_match_all_3(Regex *const self, Str *const subject, USize const subject_offset, FpRegexMatch const callback, void *const context);

/**
 * @brief Match all occurrences in String object, invoking callback for each.
 * @param self Regex pointer. A failed-compile or fresh object answers 0.
 * @param subject String subject (may be empty).
 * @param subject_offset Offset to start matching. Past-the-end answers 0.
 * @param callback Callback function.
 * @param context Caller pointer handed to the callback verbatim (may be nullptr).
 * @return Number of matches found.
 */
USize regex_match_all_4(Regex *const self, String *const subject, USize const subject_offset, FpRegexMatch const callback, void *const context);

#endif // REGEX_H