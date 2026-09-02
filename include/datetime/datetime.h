/*
 * datetime.h - Date and time utilities for the C Libraries Framework
 *
 * Features:
 *   - Datetime struct for calendar and clock values
 *   - Exact civil-date conversion in both directions (no fixed-length-year drift)
 *   - Validating parsers for ISO "%F" dates, in reporting and non-reporting forms
 *   - Comparison of full values, of dates only, and of times only
 *   - strftime-style formatting into a caller buffer, plus char/Str/String output
 *   - Arena/heap allocation support for string output
 *
 * Usage Examples:
 *   @code
 *   Datetime const now = datetime_init_1();
 *   char *const date = datetime_to_date_char(&now);
 *
 *   char_delete(date);
 *
 *   Datetime parsed = DEFAULT_INITIALIZATION;
 *
 *   if (datetime_from_char_try("2026-02-29", CHAR_STATIC_SIZE("2026-02-29"), "%F", CHAR_STATIC_SIZE("%F"), &parsed)) {
 *       datetime_print(&parsed);
 *   }
 *   @endcode
 *
 * Error Handling:
 *   Null pointers are caller errors and abort under ERROR_CHECK_ENABLED. A date
 *   STRING is data: a malformed or short one is a parse failure, not an abort.
 *   The from_* family answers the INVALID Datetime (year 0) on failure, which
 *   datetime_is_valid tests; the from_*_try twins report it as a bool instead.
 *   Earlier revisions returned the current time on a refused parse - undetectable
 *   by the caller, and the most dangerous available default.
 *
 * Time Zone:
 *   Every value is NAIVE UTC. datetime_init_1 decodes the system epoch without
 *   applying a zone offset, the to_timestamp_* functions encode back the same
 *   way, and is_dst is never written by this module. A caller that needs local
 *   time applies its own offset - and must apply the same one in both directions,
 *   because nothing here records which zone a Datetime came from.
 *
 * Epoch Types:
 *   The epoch surface is not yet type-unified: datetime_init_2 takes USize,
 *   datetime_now returns ISize, the to_timestamp_* family returns USize, and
 *   file_modified reports I64. Dates before 1970 therefore wrap through the
 *   unsigned forms rather than going negative. Unifying on I64 is a breaking
 *   change deferred to a breaking window; until then, keep pre-epoch dates out
 *   of the unsigned paths.
 *
 * Thread Safety:
 *   Not thread-safe. Caller must synchronize if used from multiple threads.
 *   datetime_init_1 and datetime_now read the system clock.
 *
 * Memory Management:
 *   A Datetime is a plain value: it owns nothing and needs no cleanup. Only the
 *   string output helpers allocate. datetime_to_char and datetime_to_date_char
 *   answer a heap buffer released with char_delete; datetime_to_str,
 *   datetime_to_string, datetime_to_date_str, and datetime_to_date_string answer
 *   owned values released with str_uninit / string_uninit. The alloc variants
 *   take an Arena and are reclaimed by resetting or freeing it. datetime_format
 *   writes into the buffer it is handed and allocates nothing.
 *
 * Performance Characteristics:
 *   The civil-date encode and decode, the timestamp conversions, and every
 *   comparison are O(1) branch-light integer arithmetic. datetime_format is O(n)
 *   in the format string and datetime_from_char_* is O(1) over the fixed ten-byte
 *   ISO date. Only the char/Str/String output helpers allocate, exactly once.
 *
 * Dependencies:
 *   - <time.h>
 *   - <container/string/string.h>
 *
 * See datetime.c for implementation details.
 */

#ifndef DATETIME_H
#define DATETIME_H

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/
#include <time.h>

#include <container/string/string.h>

/*==============================================================================
 * MARK: - Macros and Constants
 *============================================================================*/
#define DATETIME_MONTH_APRIL        3
#define DATETIME_MONTH_AUGUST       7
#define DATETIME_MONTH_DECEMBER     11
#define DATETIME_MONTH_FEBRUARY     1
#define DATETIME_MONTH_JANUARY      0
#define DATETIME_MONTH_JULY         6
#define DATETIME_MONTH_JUNE         5
#define DATETIME_MONTH_MARCH        2
#define DATETIME_MONTH_MAY          4
#define DATETIME_MONTH_NOVEMBER     10
#define DATETIME_MONTH_OCTOBER      9
#define DATETIME_MONTH_SEPTEMBER    8

/*==============================================================================
 * MARK: - Typedefs and Enums
 *============================================================================*/
typedef time_t Time;

/**
 * @brief Datetime struct for calendar and clock values.
 */
typedef struct {
    I32 seconds;   /**< Seconds [0, 59] */
    I32 minutes;   /**< Minutes [0, 59] */
    I32 hours;     /**< Hours [0, 23] */
    I32 date;      /**< Day of month [1, 31] */
    I32 month;     /**< Month [0, 11] */
    I32 year;      /**< Year (full, e.g. 2025) */
    I32 day_week;  /**< Day of week [0, 6] */
    I32 days;      /**< Day of the YEAR [1, 366] - not days since the epoch, which
                        is what datetime_to_days computes. Leap-correct. */
    I32 is_dst;    /**< Daylight Saving Time flag. NEVER written by this module -
                        every value is naive UTC (see the Time Zone note above). */
} Datetime;

/*==============================================================================
 * MARK: - Public API
 *============================================================================*/
#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Format date as a C string (arena-allocated).
 * @param self Pointer to Datetime.
 * @param allocator Arena pointer.
 * @return Arena-allocated C string.
 */
char* datetime_to_date_alloc_char_1(Datetime *const self, Arena *const allocator);

/**
 * @brief Format date as a Str (arena-allocated).
 * @param self Pointer to Datetime.
 * @param allocator Arena pointer.
 * @return Arena-allocated Str.
 */
Str datetime_to_date_alloc_str_1(Datetime *const self, Arena *const allocator);

/**
 * @brief Format date as a String (arena-allocated).
 * @param self Pointer to Datetime.
 * @param allocator Arena pointer.
 * @return Arena-allocated String.
 */
String datetime_to_date_alloc_string_1(Datetime *const self, Arena *const allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Compare two datetimes for full equality.
 * @param self Pointer to first Datetime.
 * @param data Pointer to second Datetime.
 * @return true if equal, false otherwise.
 */
bool datetime_compare_equal(Datetime const *const self, Datetime const *const data);

/**
 * @brief Compare two datetimes for date equality (ignores time).
 * @param self Pointer to first Datetime.
 * @param data Pointer to second Datetime.
 * @return true if dates are equal, false otherwise.
 */
bool datetime_compare_equal_date(Datetime const *const self, Datetime const *const data);

/**
 * @brief Compare two datetimes for time equality (ignores date).
 * @param self Pointer to first Datetime.
 * @param data Pointer to second Datetime.
 * @return true if times are equal, false otherwise.
 */
bool datetime_compare_equal_time(Datetime const *const self, Datetime const *const data);

/**
 * @brief Return true if self > data (full comparison).
 * @param self Pointer to first Datetime.
 * @param data Pointer to second Datetime.
 * @return true if self > data, false otherwise.
 */
bool datetime_compare_greater(Datetime const *const self, Datetime const *const data);

/**
 * @brief Return true if self > data (date only).
 * @param self Pointer to first Datetime.
 * @param data Pointer to second Datetime.
 * @return true if self > data by date, false otherwise.
 */
bool datetime_compare_greater_date(Datetime const *const self, Datetime const *const data);

/**
 * @brief Return true if self >= data (full comparison).
 * @param self Pointer to first Datetime.
 * @param data Pointer to second Datetime.
 * @return true if self >= data, false otherwise.
 */
bool datetime_compare_greater_equal(Datetime const *const self, Datetime const *const data);

/**
 * @brief Return true if self >= data (date only).
 * @param self Pointer to first Datetime.
 * @param data Pointer to second Datetime.
 * @return true if self >= data by date, false otherwise.
 */
bool datetime_compare_greater_equal_date(Datetime const *const self, Datetime const *const data);

/**
 * @brief Return true if self >= data (time only).
 * @param self Pointer to first Datetime.
 * @param data Pointer to second Datetime.
 * @return true if self >= data by time, false otherwise.
 */
bool datetime_compare_greater_equal_time(Datetime const *const self, Datetime const *const data);

/**
 * @brief Return true if self > data (time only).
 * @param self Pointer to first Datetime.
 * @param data Pointer to second Datetime.
 * @return true if self > data by time, false otherwise.
 */
bool datetime_compare_greater_time(Datetime const *const self, Datetime const *const data);

/**
 * @brief Subtract two datetimes, returning the whole days between their dates.
 * @param self Pointer to first Datetime.
 * @param data Pointer to second Datetime.
 * @return The MAGNITUDE of the difference in days - the return is unsigned, so
 *         the operands may be given in either order and the answer is the same.
 *         Times of day are ignored; only the civil dates are compared.
 */
USize datetime_days_operation_sub(Datetime const *const self, Datetime const *const data);

/**
 * @brief Format a Datetime into a caller buffer using a strftime-style pattern.
 *
 * The output complement of the datetime_from_char parsers: writes the fields
 * selected by the format string into buffer, bounded by capacity and always
 * NUL-terminated when capacity is nonzero, with no allocation. Recognized
 * specifiers are %Y (full year), %m (2-digit month 01-12), %b (abbreviated
 * English month name, e.g. "Jul"), %d (2-digit day of month), %H (2-digit hour
 * 00-23), %M (2-digit minute), %S (2-digit second), and %% (a literal percent
 * sign); any other character (and an unrecognized specifier) is copied through
 * verbatim.
 *
 * @param self Pointer to Datetime (must not be NULL).
 * @param format NUL-terminated format string (must not be NULL).
 * @param buffer Destination buffer (must not be NULL).
 * @param capacity Capacity of buffer in bytes, including the terminator.
 * @return Bytes written, excluding the terminator.
 */
USize datetime_format(Datetime const *const self, char const *const format, char *const buffer, USize const capacity);

/**
 * @brief Parse a Datetime from a C string and format.
 * @param date C string date.
 * @param format C string format.
 * @return Parsed Datetime, or the invalid Datetime when the date is refused.
 */
Datetime datetime_from_char_1(char const *const date, char const *const format);

/**
 * @brief Parse a Datetime from a C string and format with explicit format size.
 * @param date C string date.
 * @param format C string format.
 * @param format_size Size of format string.
 * @return Parsed Datetime, or the invalid Datetime when the date is refused.
 */
Datetime datetime_from_char_2(char const *const date, char const *const format, USize const format_size);

/**
 * @brief Parse a Datetime from a C string with explicit date size and format.
 * @param date C string date.
 * @param date_size Size of date string.
 * @param format C string format.
 * @return Parsed Datetime, or the invalid Datetime when the date is refused.
 */
Datetime datetime_from_char_3(char const *const date, USize const date_size, char const *const format);

/**
 * @brief Parse a Datetime from a C string with explicit date and format sizes.
 * @param date C string date.
 * @param date_size Size of date string.
 * @param format C string format.
 * @param format_size Size of format string.
 * @return Parsed Datetime, or the invalid Datetime when the date is refused.
 */
Datetime datetime_from_char_4(char const *const date, USize const date_size, char const *const format, USize const format_size);

/**
 * @brief Parse a date, reporting success instead of encoding it in the result.
 *        The plain from_char family answers the invalid Datetime on failure,
 *        which a caller has to remember to test; this form cannot be ignored.
 * @param date Date string. Must not be nullptr; may be short or malformed.
 * @param date_size Bytes available in date. A size below 10 is a parse failure.
 * @param format Format string. Only "%F" (YYYY-MM-DD) is recognised today.
 * @param format_size Bytes of format.
 * @param out Receives the parsed value on success, the invalid Datetime on failure.
 * @return True when the date parsed and every field was in range (leap day included).
 */
bool datetime_from_char_try(char const *const date, USize const date_size, char const *const format, USize const format_size, Datetime *const out);

/**
 * @brief Parse a Datetime from a Str and format.
 * @param date Str date.
 * @param format C string format.
 * @return Parsed Datetime, or the invalid Datetime when the date is refused.
 */
Datetime datetime_from_str_1(Str *const date, char const *const format);

/**
 * @brief Parse a Datetime from a Str and format with explicit format size.
 * @param date Str date.
 * @param format C string format.
 * @param format_size Size of format string.
 * @return Parsed Datetime, or the invalid Datetime when the date is refused.
 */
Datetime datetime_from_str_2(Str *const date, char const *const format, USize const format_size);

/**
 * @brief Parse a date from a Str, reporting success (see datetime_from_char_try).
 * @param date Str date (may be empty).
 * @param format Format string. Only "%F" is recognised today.
 * @param format_size Bytes of format.
 * @param out Receives the parsed value on success, the invalid Datetime on failure.
 * @return True when the date parsed.
 */
bool datetime_from_str_try(Str *const date, char const *const format, USize const format_size, Datetime *const out);

/**
 * @brief Parse a Datetime from a String and format.
 * @param date String date.
 * @param format C string format.
 * @return Parsed Datetime, or the invalid Datetime when the date is refused.
 */
Datetime datetime_from_string_1(String *const date, char const *const format);

/**
 * @brief Parse a Datetime from a String and format with explicit format size.
 * @param date String date.
 * @param format C string format.
 * @param format_size Size of format string.
 * @return Parsed Datetime, or the invalid Datetime when the date is refused.
 */
Datetime datetime_from_string_2(String *const date, char const *const format, USize const format_size);

/**
 * @brief Parse a date from a String, reporting success (see datetime_from_char_try).
 * @param date String date (may be empty).
 * @param format Format string. Only "%F" is recognised today.
 * @param format_size Bytes of format.
 * @param out Receives the parsed value on success, the invalid Datetime on failure.
 * @return True when the date parsed.
 */
bool datetime_from_string_try(String *const date, char const *const format, USize const format_size, Datetime *const out);

/**
 * @brief Initialize Datetime to current time.
 * @return Datetime set to current time.
 */
Datetime datetime_init_1(void);

/**
 * @brief Initialize Datetime from epoch value.
 * @param epoch Epoch time value.
 * @return Datetime set to epoch.
 */
Datetime datetime_init_2(USize const epoch);

/**
 * @brief Initialize Datetime from year, month, and day.
 * @param year Year value.
 * @param month Month value.
 * @param day Day value.
 * @return Datetime set to specified date.
 */
Datetime datetime_init_3(I32 const year, I32 const month, I32 const day);

/**
 * @brief Initialize Datetime to the current date with the given time of day.
 * @param hours Hours value.
 * @param minutes Minutes value.
 * @param seconds Seconds value.
 * @return Datetime set to today with the specified time.
 */
Datetime datetime_init_4(I32 const hours, I32 const minutes, I32 const seconds);

/**
 * @brief Initialize Datetime from full date and time.
 * @param year Year value. Must be >= 1: year 0 is the module's invalid sentinel.
 * @param month Month value [0, 11].
 * @param day Day value, bounded by the given month IN THE GIVEN YEAR - so 29
 *            February is accepted only in a leap year.
 * @param hours Hours value [0, 23].
 * @param minutes Minutes value [0, 59].
 * @param seconds Seconds value [0, 59].
 * @return Datetime set to specified date and time.
 * @note ABORTS under ERROR_CHECK_ENABLED on any out-of-range field - this is the
 *       asserting form, for values the caller controls. Values that arrive as
 *       DATA belong in the from_*_try family, which refuses instead. The bounds
 *       are exact (an earlier revision accepted 24:60:60 and 31 February), so a
 *       caller that was passing an out-of-range field now aborts where it
 *       previously produced a silently shifted instant.
 */
Datetime datetime_init_5(I32 const year, I32 const month, I32 const day, I32 const hours, I32 const minutes, I32 const seconds);

/**
 * @brief Test whether a Datetime holds a real calendar date.
 * @param self Datetime pointer.
 * @return True when the value is a date AND time this module could have
 *         produced. The from_* parsers answer the INVALID Datetime (all fields
 *         zero) when a date string is malformed, and year 0 is the sentinel: no
 *         constructor produces it. Also rejects a hand-assembled struct whose
 *         month or day is out of range for its year, or whose hours, minutes or
 *         seconds fall outside 0-23 / 0-59 / 0-59.
 */
bool datetime_is_valid(Datetime const *const self);

/**
 * @brief Advance to the next day.
 * @param self Pointer to Datetime.
 */
void datetime_next_day(Datetime *const self);

/**
 * @brief Read the system clock as seconds since the 1970-01-01 epoch.
 * @return Current epoch seconds.
 */
ISize datetime_now(void);

/**
 * @brief Print the date (short format).
 * @param self Pointer to Datetime.
 */
void datetime_print(Datetime *const self);

/**
 * @brief Print the date and time (full format).
 * @param self Pointer to Datetime.
 */
void datetime_print_full(Datetime *const self);

/**
 * @brief Format date and time as a C string (heap-allocated).
 * @param self Pointer to Datetime.
 * @return Heap-allocated C string, released with char_delete.
 */
char* datetime_to_char(Datetime *const self);

/**
 * @brief Format date as a C string (heap-allocated).
 * @param self Pointer to Datetime.
 * @return Heap-allocated C string, released with char_delete.
 */
char* datetime_to_date_char(Datetime *const self);

/**
 * @brief Format date as a Str (heap-allocated).
 * @param self Pointer to Datetime.
 * @return Heap-allocated Str, released with str_uninit.
 */
Str datetime_to_date_str(Datetime *const self);

/**
 * @brief Format date as a String (heap-allocated).
 * @param self Pointer to Datetime.
 * @return Heap-allocated String, released with string_uninit.
 */
String datetime_to_date_string(Datetime *const self);

/**
 * @brief Convert Datetime to days since epoch.
 * @param self Pointer to Datetime.
 * @return Days since epoch.
 */
USize datetime_to_days(Datetime *const self);

/**
 * @brief Format date and time as a Str (heap-allocated).
 * @param self Pointer to Datetime.
 * @return Heap-allocated Str, released with str_uninit.
 */
Str datetime_to_str(Datetime *const self);

/**
 * @brief Format date and time as a String (heap-allocated).
 * @param self Pointer to Datetime.
 * @return Heap-allocated String, released with string_uninit.
 */
String datetime_to_string(Datetime *const self);

/**
 * @brief Convert Datetime to timestamp (full).
 * @param self Pointer to Datetime.
 * @return Timestamp value.
 */
USize datetime_to_timestamp(Datetime *const self);

/**
 * @brief Convert Datetime to timestamp (date only).
 * @param self Pointer to Datetime.
 * @return Timestamp value (date only).
 */
USize datetime_to_timestamp_date(Datetime *const self);

/**
 * @brief Convert Datetime to timestamp (time only).
 * @param self Pointer to Datetime.
 * @return Timestamp value (time only).
 */
USize datetime_to_timestamp_time(Datetime *const self);

#endif // DATETIME_H