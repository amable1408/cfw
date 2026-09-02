/*
 * test_datetime.c - Unit test runner for the CFW datetime module
 *
 * The original three suites (format, epoch, encode) were GREEN while five
 * comparison functions returned a constant, datetime_to_timestamp_time was
 * declared but DEFINED NOWHERE, the ISO parser read fixed offsets into
 * unvalidated data, and datetime_next_day could not escape February in a leap
 * year. Everything from the parse suite down pins that fix set so it cannot
 * regress:
 *
 *   1. Parser validation (the CRITICAL): a malformed, short, or out-of-range
 *      date STRING is data - it answers the INVALID Datetime (year 0), never
 *      aborts, and never reads past the size it was handed.
 *   2. A refused parse is NOT "now": year 0, and never the current year.
 *   3. The try_ family reports the same verdict as the plain family through
 *      char, Str, and String - empty containers included.
 *   4. The formerly stubbed comparisons, each with a case that FAILS if the
 *      function returns a constant. The _time forms compare TIME OF DAY ONLY,
 *      pinned with dates that order OPPOSITE to their times.
 *   5. datetime_days_operation_sub returns the real, leap-correct day count
 *      (it returned 0 unconditionally).
 *   6. datetime_to_timestamp_time - declared in the header, defined nowhere.
 *   7. datetime_next_day correctness AND TERMINATION, walked with the live
 *      caller's own `while (!equal) next_day()` shape under a step bound.
 *   8. Weekday correctness against three known anchors.
 *   9. The formatter rebase: October renders as "10" (the `< 10` pad bug) and a
 *      year below 1000 renders zero-padded with no NUL embedded mid-string.
 *  10. datetime_to_str hands back an OWNED Str (it leaked a view's buffer).
 *  11. datetime_print / datetime_print_full render whole day and month names,
 *      read back off a redirected stdout.
 *
 * This framework has no allocation counter to assert against (MEMORY_HOOKS_
 * IMPLEMENTATION defines nothing today), so ownership is pinned by the
 * observable contract - a released container reports empty and a second release
 * is safe - rather than by a leak count.
 */

/* The stdout capture below needs the POSIX descriptor calls. MSVCRT spells them
 * with a leading underscore and puts them in <io.h>; glibc uses the unprefixed
 * names from <unistd.h>. Without this split the suite does not compile on Linux
 * at all, which is how the platform lane found it. */
#ifdef _WIN32
#include <io.h>

#define _TEST_CLOSE  _close
#define _TEST_DUP    _dup
#define _TEST_DUP2   _dup2
#define _TEST_FILENO _fileno
#else
#include <unistd.h>

#define _TEST_CLOSE  close
#define _TEST_DUP    dup
#define _TEST_DUP2   dup2
#define _TEST_FILENO fileno
#endif // _WIN32

#include <stdio.h>

#include <arena/arena.h>
#include <char/char.h>
#include <container/str/str.h>
#include <container/string/string.h>
#include <datetime/datetime.h>
#include <log/log.h>
#include <memory/memory.h>
#include <test/test.h>

/*==============================================================================
 * MARK: - Format suite
 *============================================================================*/

static void _test_format(Test *const self) {
    test_suite_begin(self, "format");

    Datetime const dt = datetime_init_5(2026, 6, 23, 14, 5, 9);

    char buffer[64] = DEFAULT_INITIALIZATION;

    test_case_begin(self, "each specifier expands to its field");

    datetime_format(&dt, "%Y-%m-%d", buffer, sizeof buffer);
    test_expect_string(self, "ISO date", "2026-07-23", buffer);

    datetime_format(&dt, "%b %d", buffer, sizeof buffer);
    test_expect_string(self, "month name + day", "Jul 23", buffer);

    datetime_format(&dt, "%H:%M:%S", buffer, sizeof buffer);
    test_expect_string(self, "clock", "14:05:09", buffer);

    datetime_format(&dt, "%Y", buffer, sizeof buffer);
    test_expect_string(self, "year alone", "2026", buffer);

    test_case_end(self);

    test_case_begin(self, "literals, %% and unknown specifiers pass through");

    datetime_format(&dt, "at %H:%M %%", buffer, sizeof buffer);
    test_expect_string(self, "literal text and escaped percent", "at 14:05 %", buffer);

    datetime_format(&dt, "%Q", buffer, sizeof buffer);
    test_expect_string(self, "unknown specifier copied verbatim", "%Q", buffer);

    datetime_format(&dt, "trailing %", buffer, sizeof buffer);
    test_expect_string(self, "trailing percent is a literal", "trailing %", buffer);

    test_case_end(self);

    test_case_begin(self, "capacity bounds and truncation");

    USize const written = datetime_format(&dt, "%Y-%m-%d", buffer, sizeof buffer);
    test_expect_u(self, "returns bytes written", 10, written);

    char small[6] = DEFAULT_INITIALIZATION;
    USize const clipped = datetime_format(&dt, "%Y-%m-%d", small, sizeof small);

    test_expect_string(self, "truncated to fit with terminator", "2026-", small);
    test_expect_u(self, "5 bytes written", 5, clipped);

    USize const none = datetime_format(&dt, "%Y", buffer, 0);
    test_expect_u(self, "capacity 0 writes nothing", 0, none);

    test_case_end(self);

    test_suite_end(self);
}

/*==============================================================================
 * MARK: - Epoch suite
 *============================================================================*/

static void _test_epoch(Test *const self) {
    test_suite_begin(self, "epoch");

    test_case_begin(self, "epoch seconds decode to the exact civil date");

    // 1700000000 == 2023-11-14 22:13:20 UTC (a fixed, leap-year-correct anchor).
    Datetime const dt = datetime_init_2(1700000000);

    test_expect_i(self, "year 2023", 2023, dt.year);
    test_expect_i(self, "month November (10)", 10, dt.month);
    test_expect_i(self, "date 14", 14, dt.date);
    test_expect_i(self, "hour 22", 22, dt.hours);
    test_expect_i(self, "minute 13", 13, dt.minutes);
    test_expect_i(self, "second 20", 20, dt.seconds);

    test_case_end(self);

    test_case_begin(self, "a leap day decodes correctly");

    // 1582934400 == 2020-02-29 00:00:00 UTC (leap day the old approximation missed).
    Datetime const leap = datetime_init_2(1582934400);

    test_expect_i(self, "year 2020", 2020, leap.year);
    test_expect_i(self, "month February (1)", 1, leap.month);
    test_expect_i(self, "date 29", 29, leap.date);

    test_case_end(self);

    test_case_begin(self, "the epoch instant is 1970-01-01");

    Datetime const zero = datetime_init_2(0);

    test_expect_i(self, "year 1970", 1970, zero.year);
    test_expect_i(self, "month January (0)", 0, zero.month);
    test_expect_i(self, "date 1", 1, zero.date);

    test_case_end(self);

    test_suite_end(self);
}

/*==============================================================================
 * MARK: - Encode suite
 *============================================================================*/

static void _test_encode(Test *const self) {
    test_suite_begin(self, "encode");

    test_case_begin(self, "a civil date encodes to the exact epoch");

    // 2023-11-14 22:13:20 UTC == 1700000000; its midnight == 1699920000.
    Datetime moment = datetime_init_5(2023, 10, 14, 22, 13, 20);   // month 10 = November (0-based)

    test_expect_u(self, "date-only epoch is midnight", 1699920000, datetime_to_timestamp_date(&moment));
    test_expect_u(self, "full epoch includes the time", 1700000000, datetime_to_timestamp(&moment));

    test_case_end(self);

    test_case_begin(self, "decode then encode round-trips exactly, across leap years");

    Datetime leap = datetime_init_2(1582934400);   // 2020-02-29 00:00:00 UTC

    test_expect_u(self, "a leap day re-encodes exactly", 1582934400, datetime_to_timestamp(&leap));

    Datetime origin = datetime_init_2(0);

    test_expect_u(self, "the epoch instant re-encodes to 0", 0, datetime_to_timestamp(&origin));

    // A spread of instants (incl. a 2000 leap day and non-midnight times) must
    // survive decode -> encode unchanged — the old drift-prone encode did not.
    USize const anchors[] = { 1, 86399, 86400, 951782400, 1234567890, 1893456000 };

    for (USize i = 0; i < 6; i += 1) {
        Datetime instant = datetime_init_2(anchors[i]);

        test_expect_u(self, "decode/encode round-trips", anchors[i], datetime_to_timestamp(&instant));
    }

    test_case_end(self);

    test_suite_end(self);
}

/*==============================================================================
 * MARK: - Parse validation suite
 *============================================================================*/

/* Every refusal asks the same three questions of a date STRING: it must not
 * abort, must not parse, and must not leave a day behind. */
static void _expect_refused(Test *const self, char const *const name, char const *const date, USize const date_size) {
    Datetime const parsed = datetime_from_char_4(date, date_size, "%F", CHAR_STATIC_SIZE("%F"));

    test_expect_false(self, name, datetime_is_valid(&parsed));
    test_expect_i(self, "... answers the invalid year 0", 0, parsed.year);
    test_expect_i(self, "... writes no day", 0, parsed.date);
}

static void _test_parse_validation(Test *const self) {
    test_suite_begin(self, "parse validation");

    test_case_begin(self, "out-of-range calendar fields are refused, not aborted");

    _expect_refused(self, "month 00", "2024-00-15", CHAR_STATIC_SIZE("2024-00-15"));
    _expect_refused(self, "month 13", "2024-13-01", CHAR_STATIC_SIZE("2024-13-01"));
    _expect_refused(self, "day 32", "2024-01-32", CHAR_STATIC_SIZE("2024-01-32"));
    _expect_refused(self, "Feb 29 of a non-leap year", "2023-02-29", CHAR_STATIC_SIZE("2023-02-29"));

    /* Year 0 is the module's INVALID sentinel, so the parser must not produce
     * it: otherwise try_ reports success for an object datetime_is_valid then
     * rejects. */
    _expect_refused(self, "year 0000", "0000-01-01", CHAR_STATIC_SIZE("0000-01-01"));

    test_case_end(self);

    test_case_begin(self, "malformed shapes are refused");

    _expect_refused(self, "an empty date", "", 0);
    _expect_refused(self, "prose", "not-a-date", CHAR_STATIC_SIZE("not-a-date"));
    _expect_refused(self, "wrong separators", "2024/01/15", CHAR_STATIC_SIZE("2024/01/15"));
    _expect_refused(self, "a non-digit in the year", "20x4-01-15", CHAR_STATIC_SIZE("20x4-01-15"));

    test_case_end(self);

    test_case_begin(self, "a short date is refused without reading past its end");

    /* Exactly four bytes, no terminator: the old body indexed date[5] and date[8]
     * unconditionally, so this allocation is where it read. */
    char *const truncated = (char*) memory_alloc(4);

    memory_copy_2(truncated, 4, "2024", 4);

    Datetime const short_date = datetime_from_char_4(truncated, 4, "%F", CHAR_STATIC_SIZE("%F"));

    test_expect_false(self, "a 4-byte date is invalid", datetime_is_valid(&short_date));
    test_expect_i(self, "a 4-byte date yields year 0", 0, short_date.year);

    memory_free(truncated);

    /* The size is CONTRACT, not a hint. This buffer HOLDS a valid date, so a
     * parser that ignores date_size succeeds here instead of merely reading
     * garbage - which is what makes the guard's absence deterministic rather
     * than sanitizer-dependent. The size-10 read is the positive anchor: without
     * it the three refusals could all pass for the wrong reason. */
    char *const oversized = (char*) memory_alloc(16);

    memory_copy_2(oversized, 16, "2024-02-29", CHAR_STATIC_SIZE("2024-02-29"));

    Datetime const clipped_4 = datetime_from_char_4(oversized, 4, "%F", CHAR_STATIC_SIZE("%F"));
    Datetime const clipped_9 = datetime_from_char_4(oversized, 9, "%F", CHAR_STATIC_SIZE("%F"));
    Datetime const whole     = datetime_from_char_4(oversized, CHAR_STATIC_SIZE("2024-02-29"), "%F", CHAR_STATIC_SIZE("%F"));

    Datetime clipped_out = DEFAULT_INITIALIZATION;

    test_expect_false(self, "size 4 refuses a buffer that holds a valid date", datetime_is_valid(&clipped_4));
    test_expect_false(self, "size 9 refuses the same buffer", datetime_is_valid(&clipped_9));
    test_expect_true(self, "size 10 accepts it - the buffer really is valid", datetime_is_valid(&whole));
    test_expect_i(self, "... and reads the leap day", 29, whole.date);
    test_expect_false(self, "the try_ form refuses the clipped size too", datetime_from_char_try(oversized, 4, "%F", CHAR_STATIC_SIZE("%F"), &clipped_out));
    test_expect_i(self, "... leaving out zeroed", 0, clipped_out.year);

    memory_free(oversized);

    test_case_end(self);

    test_case_begin(self, "a real leap day parses");

    Datetime const leap = datetime_from_char_4("2024-02-29", CHAR_STATIC_SIZE("2024-02-29"), "%F", CHAR_STATIC_SIZE("%F"));

    test_expect_true(self, "2024-02-29 is valid", datetime_is_valid(&leap));
    test_expect_i(self, "year 2024", 2024, leap.year);
    test_expect_i(self, "month February (1)", DATETIME_MONTH_FEBRUARY, leap.month);
    test_expect_i(self, "date 29", 29, leap.date);
    test_expect_i(self, "midnight", 0, leap.hours);

    test_case_end(self);

    test_case_begin(self, "a refused parse is NOT the current time");

    /* The old body returned datetime_init_1() - today - which no caller can tell
     * from a successful parse. Comparing against the live clock is what makes
     * this pin fail if that default ever comes back. */
    Datetime const now = datetime_init_1();
    Datetime const refused = datetime_from_char_4("2024-13-01", CHAR_STATIC_SIZE("2024-13-01"), "%F", CHAR_STATIC_SIZE("%F"));

    test_expect_false(self, "the refusal is invalid", datetime_is_valid(&refused));
    test_expect_i(self, "the refusal carries year 0", 0, refused.year);
    test_expect_true(self, "the refusal is not the current year", refused.year != now.year);
    test_expect_i(self, "no month leaked through", 0, refused.month);
    test_expect_i(self, "no date leaked through", 0, refused.date);

    test_case_end(self);

    test_case_begin(self, "an unsupported format refuses rather than guessing");

    Datetime const wrong_format = datetime_from_char_4("2024-02-29", CHAR_STATIC_SIZE("2024-02-29"), "%Y", CHAR_STATIC_SIZE("%Y"));

    test_expect_false(self, "%Y is not a supported parse format", datetime_is_valid(&wrong_format));
    test_expect_i(self, "... and it yields year 0, not now", 0, wrong_format.year);

    test_case_end(self);

    test_case_begin(self, "the sized convenience forms agree");

    Datetime const from_1 = datetime_from_char_1("2024-02-29", "%F");
    Datetime const from_2 = datetime_from_char_2("2024-02-29", "%F", CHAR_STATIC_SIZE("%F"));
    Datetime const from_3 = datetime_from_char_3("2024-13-01", CHAR_STATIC_SIZE("2024-13-01"), "%F");

    test_expect_i(self, "from_char_1 parses the leap day", 2024, from_1.year);
    test_expect_i(self, "from_char_2 parses the leap day", 29, from_2.date);
    test_expect_false(self, "from_char_3 refuses month 13", datetime_is_valid(&from_3));

    test_case_end(self);

    test_suite_end(self);
}

/*==============================================================================
 * MARK: - Parse reporting (try_) suite
 *============================================================================*/

static void _test_parse_try(Test *const self) {
    test_suite_begin(self, "parse try");

    test_case_begin(self, "the char form reports refusal and success");

    Datetime out = DEFAULT_INITIALIZATION;

    test_expect_false(self, "month 00 refuses", datetime_from_char_try("2024-00-15", CHAR_STATIC_SIZE("2024-00-15"), "%F", CHAR_STATIC_SIZE("%F"), &out));
    test_expect_i(self, "out is zeroed on refusal", 0, out.year);

    test_expect_false(self, "Feb 29 of a non-leap year refuses", datetime_from_char_try("2023-02-29", CHAR_STATIC_SIZE("2023-02-29"), "%F", CHAR_STATIC_SIZE("%F"), &out));
    test_expect_false(self, "year 0000 refuses - try_ agrees with datetime_is_valid", datetime_from_char_try("0000-01-01", CHAR_STATIC_SIZE("0000-01-01"), "%F", CHAR_STATIC_SIZE("%F"), &out));
    test_expect_i(self, "... leaving the sentinel year", 0, out.year);
    test_expect_false(self, "prose refuses", datetime_from_char_try("not-a-date", CHAR_STATIC_SIZE("not-a-date"), "%F", CHAR_STATIC_SIZE("%F"), &out));
    test_expect_false(self, "an empty date refuses", datetime_from_char_try("", 0, "%F", CHAR_STATIC_SIZE("%F"), &out));
    test_expect_false(self, "an unsupported format refuses", datetime_from_char_try("2024-02-29", CHAR_STATIC_SIZE("2024-02-29"), "%Y", CHAR_STATIC_SIZE("%Y"), &out));

    test_expect_true(self, "the leap day is accepted", datetime_from_char_try("2024-02-29", CHAR_STATIC_SIZE("2024-02-29"), "%F", CHAR_STATIC_SIZE("%F"), &out));
    test_expect_i(self, "out year 2024", 2024, out.year);
    test_expect_i(self, "out month February (1)", DATETIME_MONTH_FEBRUARY, out.month);
    test_expect_i(self, "out date 29", 29, out.date);
    test_expect_true(self, "out is a valid Datetime", datetime_is_valid(&out));

    test_case_end(self);

    test_case_begin(self, "the Str twin agrees with the char form");

    Str refused_str = str_init_3((char*) "2024-13-01", CHAR_STATIC_SIZE("2024-13-01"));
    Str leap_str    = str_init_3((char*) "2024-02-29", CHAR_STATIC_SIZE("2024-02-29"));
    Str empty_str   = str_init_1();

    Datetime str_out = DEFAULT_INITIALIZATION;

    test_expect_false(self, "Str month 13 refuses", datetime_from_str_try(&refused_str, "%F", CHAR_STATIC_SIZE("%F"), &str_out));
    test_expect_false(self, "an EMPTY Str refuses without crashing", datetime_from_str_try(&empty_str, "%F", CHAR_STATIC_SIZE("%F"), &str_out));
    test_expect_i(self, "the empty Str leaves out zeroed", 0, str_out.year);

    test_expect_true(self, "Str leap day is accepted", datetime_from_str_try(&leap_str, "%F", CHAR_STATIC_SIZE("%F"), &str_out));
    test_expect_i(self, "Str year 2024", 2024, str_out.year);
    test_expect_i(self, "Str date 29", 29, str_out.date);

    test_case_end(self);

    test_case_begin(self, "the String twin agrees with the char form");

    String refused_string = string_init_4((char*) "2024-01-32", CHAR_STATIC_SIZE("2024-01-32"));
    String leap_string    = string_init_4((char*) "2024-02-29", CHAR_STATIC_SIZE("2024-02-29"));
    String empty_string   = string_init_1();

    Datetime string_out = DEFAULT_INITIALIZATION;

    test_expect_false(self, "String day 32 refuses", datetime_from_string_try(&refused_string, "%F", CHAR_STATIC_SIZE("%F"), &string_out));
    test_expect_false(self, "an EMPTY String refuses without crashing", datetime_from_string_try(&empty_string, "%F", CHAR_STATIC_SIZE("%F"), &string_out));
    test_expect_i(self, "the empty String leaves out zeroed", 0, string_out.year);

    test_expect_true(self, "String leap day is accepted", datetime_from_string_try(&leap_string, "%F", CHAR_STATIC_SIZE("%F"), &string_out));
    test_expect_i(self, "String year 2024", 2024, string_out.year);
    test_expect_i(self, "String date 29", 29, string_out.date);

    test_case_end(self);

    test_case_begin(self, "the plain container forms answer the invalid Datetime");

    Datetime const str_plain    = datetime_from_str_2(&leap_str, "%F", CHAR_STATIC_SIZE("%F"));
    Datetime const str_bad      = datetime_from_str_1(&refused_str, "%F");
    Datetime const str_empty    = datetime_from_str_2(&empty_str, "%F", CHAR_STATIC_SIZE("%F"));
    Datetime const string_plain = datetime_from_string_2(&leap_string, "%F", CHAR_STATIC_SIZE("%F"));
    Datetime const string_bad   = datetime_from_string_1(&refused_string, "%F");
    Datetime const string_empty = datetime_from_string_2(&empty_string, "%F", CHAR_STATIC_SIZE("%F"));

    test_expect_i(self, "from_str parses the leap day", 2024, str_plain.year);
    test_expect_false(self, "from_str refuses month 13", datetime_is_valid(&str_bad));
    test_expect_false(self, "from_str survives an EMPTY Str", datetime_is_valid(&str_empty));
    test_expect_i(self, "from_string parses the leap day", 29, string_plain.date);
    test_expect_false(self, "from_string refuses day 32", datetime_is_valid(&string_bad));
    test_expect_false(self, "from_string survives an EMPTY String", datetime_is_valid(&string_empty));

    test_case_end(self);

    string_uninit(&empty_string);

    test_suite_end(self);
}

/*==============================================================================
 * MARK: - Comparison suite
 *============================================================================*/

static void _test_compare(Test *const self) {
    test_suite_begin(self, "compare");

    /* Every assertion below is asserted in BOTH directions, so a body that
     * returns a constant - which five of these did - fails one of the pair. */
    Datetime const early = datetime_init_5(2020, DATETIME_MONTH_JANUARY, 1, 0, 0, 0);
    Datetime const late  = datetime_init_5(2024, DATETIME_MONTH_JUNE, 15, 12, 30, 0);
    Datetime const twin  = datetime_init_5(2024, DATETIME_MONTH_JUNE, 15, 12, 30, 0);

    Datetime const same_day_other_time = datetime_init_5(2024, DATETIME_MONTH_JUNE, 15, 23, 59, 59);

    test_case_begin(self, "equality across the three granularities");

    test_expect_true(self, "identical values are equal", datetime_compare_equal(&late, &twin));
    test_expect_false(self, "different values are not equal", datetime_compare_equal(&late, &early));
    test_expect_true(self, "same date, different time is date-equal", datetime_compare_equal_date(&late, &same_day_other_time));
    test_expect_false(self, "different dates are not date-equal", datetime_compare_equal_date(&late, &early));
    test_expect_false(self, "different times are not time-equal", datetime_compare_equal_time(&late, &same_day_other_time));
    test_expect_true(self, "identical times are time-equal", datetime_compare_equal_time(&late, &twin));

    test_case_end(self);

    test_case_begin(self, "greater is a real comparison, both directions");

    test_expect_false(self, "2020-01-01 is NOT greater than 2024-06-15", datetime_compare_greater(&early, &late));
    test_expect_true(self, "2024-06-15 IS greater than 2020-01-01", datetime_compare_greater(&late, &early));
    test_expect_false(self, "equal values are not greater", datetime_compare_greater(&late, &twin));

    test_expect_false(self, "the earlier date is not date-greater", datetime_compare_greater_date(&early, &late));
    test_expect_true(self, "the later date is date-greater", datetime_compare_greater_date(&late, &early));
    test_expect_false(self, "the same date is not date-greater", datetime_compare_greater_date(&late, &same_day_other_time));

    test_case_end(self);

    test_case_begin(self, "greater_equal admits equality and refuses the lesser");

    test_expect_true(self, "equal values are greater-equal", datetime_compare_greater_equal(&late, &twin));
    test_expect_false(self, "a lesser value is not greater-equal", datetime_compare_greater_equal(&early, &late));
    test_expect_true(self, "a greater value is greater-equal", datetime_compare_greater_equal(&late, &early));

    test_expect_true(self, "the same date is date-greater-equal", datetime_compare_greater_equal_date(&late, &same_day_other_time));
    test_expect_false(self, "an earlier date is not date-greater-equal", datetime_compare_greater_equal_date(&early, &late));
    test_expect_true(self, "a later date is date-greater-equal", datetime_compare_greater_equal_date(&late, &early));

    test_case_end(self);

    test_case_begin(self, "the _time forms compare TIME OF DAY ONLY");

    Datetime const morning = datetime_init_5(2024, DATETIME_MONTH_JUNE, 15, 9, 0, 0);
    Datetime const evening = datetime_init_5(2024, DATETIME_MONTH_JUNE, 15, 21, 0, 0);

    test_expect_true(self, "21:00 is time-greater than 09:00 on the same date", datetime_compare_greater_time(&evening, &morning));
    test_expect_false(self, "09:00 is not time-greater than 21:00 on the same date", datetime_compare_greater_time(&morning, &evening));

    /* The case that proves the DATE is ignored: the EARLIER date carries the
     * LATER clock, so a body that fell through to the full timestamp - or that
     * returned a constant - answers the opposite of this pair. */
    Datetime const old_evening = datetime_init_5(2020, DATETIME_MONTH_JANUARY, 1, 23, 0, 0);
    Datetime const new_morning = datetime_init_5(2024, DATETIME_MONTH_JUNE, 15, 1, 0, 0);

    test_expect_true(self, "23:00 in 2020 is time-greater than 01:00 in 2024", datetime_compare_greater_time(&old_evening, &new_morning));
    test_expect_false(self, "01:00 in 2024 is not time-greater than 23:00 in 2020", datetime_compare_greater_time(&new_morning, &old_evening));
    test_expect_true(self, "and the FULL comparison still orders by date", datetime_compare_greater(&new_morning, &old_evening));

    test_expect_true(self, "equal clocks are time-greater-equal", datetime_compare_greater_equal_time(&late, &twin));
    test_expect_true(self, "23:00 in 2020 is time-greater-equal to 01:00 in 2024", datetime_compare_greater_equal_time(&old_evening, &new_morning));
    test_expect_false(self, "01:00 in 2024 is not time-greater-equal to 23:00 in 2020", datetime_compare_greater_equal_time(&new_morning, &old_evening));

    Datetime const same_clock_other_date = datetime_init_5(1999, DATETIME_MONTH_DECEMBER, 31, 12, 30, 0);

    test_expect_true(self, "a 1999 12:30 is time-greater-equal to a 2024 12:30", datetime_compare_greater_equal_time(&same_clock_other_date, &late));
    test_expect_false(self, "... and not strictly time-greater", datetime_compare_greater_time(&same_clock_other_date, &late));

    test_case_end(self);

    test_suite_end(self);
}

/*==============================================================================
 * MARK: - Day arithmetic suite
 *============================================================================*/

static void _test_days_operation(Test *const self) {
    test_suite_begin(self, "days operation");

    /* This returned 0 unconditionally. January (31) + February (29 in 2024, 28
     * in 2023) is the difference a leap year has to show. */
    Datetime const leap_start  = datetime_init_3(2024, DATETIME_MONTH_JANUARY, 1);
    Datetime const leap_end    = datetime_init_3(2024, DATETIME_MONTH_MARCH, 1);
    Datetime const plain_start = datetime_init_3(2023, DATETIME_MONTH_JANUARY, 1);
    Datetime const plain_end   = datetime_init_3(2023, DATETIME_MONTH_MARCH, 1);

    test_case_begin(self, "the span is the real, leap-correct day count");

    test_expect_u(self, "2024-01-01 to 2024-03-01 is 60 days", 60, datetime_days_operation_sub(&leap_end, &leap_start));
    test_expect_u(self, "2023-01-01 to 2023-03-01 is 59 days", 59, datetime_days_operation_sub(&plain_end, &plain_start));

    Datetime const next_day_over = datetime_init_3(2024, DATETIME_MONTH_JANUARY, 2);

    test_expect_u(self, "2024-01-01 to 2024-01-02 is 1 day", 1, datetime_days_operation_sub(&next_day_over, &leap_start));

    test_case_end(self);

    test_case_begin(self, "the span is symmetric and zero for identical dates");

    test_expect_u(self, "reversed operands give the same magnitude", 60, datetime_days_operation_sub(&leap_start, &leap_end));
    test_expect_u(self, "identical dates span 0 days", 0, datetime_days_operation_sub(&leap_start, &leap_start));

    test_case_end(self);

    test_case_begin(self, "whole years distinguish leap from common");

    Datetime const year_2025 = datetime_init_3(2025, DATETIME_MONTH_JANUARY, 1);
    Datetime const year_2026 = datetime_init_3(2026, DATETIME_MONTH_JANUARY, 1);

    test_expect_u(self, "2024 is 366 days long", 366, datetime_days_operation_sub(&year_2025, &leap_start));
    test_expect_u(self, "2025 is 365 days long", 365, datetime_days_operation_sub(&year_2026, &year_2025));

    test_case_end(self);

    test_suite_end(self);
}

/*==============================================================================
 * MARK: - Time-of-day timestamp suite
 *============================================================================*/

static void _test_timestamp_time(Test *const self) {
    test_suite_begin(self, "timestamp time");

    /* Declared in the header and indexed in the API docs, but DEFINED NOWHERE -
     * the first caller would have been a link error. */
    Datetime afternoon = datetime_init_5(2024, DATETIME_MONTH_JANUARY, 15, 13, 45, 9);
    Datetime midnight  = datetime_init_5(2024, DATETIME_MONTH_JANUARY, 15, 0, 0, 0);
    Datetime last      = datetime_init_5(2024, DATETIME_MONTH_JANUARY, 15, 23, 59, 59);

    test_case_begin(self, "seconds since midnight");

    test_expect_u(self, "13:45:09 is 49509 seconds", 49509, datetime_to_timestamp_time(&afternoon));
    test_expect_u(self, "midnight is 0", 0, datetime_to_timestamp_time(&midnight));
    test_expect_u(self, "23:59:59 is 86399 seconds", 86399, datetime_to_timestamp_time(&last));

    test_case_end(self);

    test_case_begin(self, "the time part is independent of the date");

    // 2024-01-15 00:00:00 UTC == 1705276800; +49509 == 1705326309.
    test_expect_u(self, "the date part is midnight", 1705276800, datetime_to_timestamp_date(&afternoon));
    test_expect_u(self, "the full timestamp carries the clock", 1705326309, datetime_to_timestamp(&afternoon));

    Datetime clock_only = DEFAULT_INITIALIZATION;

    clock_only.hours   = 13;
    clock_only.minutes = 45;
    clock_only.seconds = 9;

    test_expect_u(self, "a dateless value reports the same time", 49509, datetime_to_timestamp_time(&clock_only));

    test_case_end(self);

    test_suite_end(self);
}

/*==============================================================================
 * MARK: - next_day suite
 *============================================================================*/

static void _test_next_day(Test *const self) {
    test_suite_begin(self, "next day");

    test_case_begin(self, "February resolves by the full Gregorian rule");

    Datetime leap = datetime_init_3(2024, DATETIME_MONTH_FEBRUARY, 28);

    datetime_next_day(&leap);

    test_expect_i(self, "2024-02-28 steps to the 29th", 29, leap.date);
    test_expect_i(self, "... still February", DATETIME_MONTH_FEBRUARY, leap.month);
    test_expect_i(self, "... day of year 60", 60, leap.days);

    datetime_next_day(&leap);

    test_expect_i(self, "2024-02-29 steps to March", DATETIME_MONTH_MARCH, leap.month);
    test_expect_i(self, "... the 1st", 1, leap.date);
    test_expect_i(self, "... still 2024", 2024, leap.year);

    Datetime plain = datetime_init_3(2023, DATETIME_MONTH_FEBRUARY, 28);

    datetime_next_day(&plain);

    test_expect_i(self, "2023-02-28 steps straight to March", DATETIME_MONTH_MARCH, plain.month);
    test_expect_i(self, "... the 1st", 1, plain.date);

    /* The century rule, which the old (year - 1972) % 4 test got wrong in both
     * directions. */
    Datetime century = datetime_init_3(1900, DATETIME_MONTH_FEBRUARY, 28);

    datetime_next_day(&century);

    test_expect_i(self, "1900 is NOT a leap year - straight to March", DATETIME_MONTH_MARCH, century.month);
    test_expect_i(self, "... the 1st", 1, century.date);

    Datetime quadricentennial = datetime_init_3(2000, DATETIME_MONTH_FEBRUARY, 28);

    datetime_next_day(&quadricentennial);

    test_expect_i(self, "2000 IS a leap year - the 29th exists", 29, quadricentennial.date);
    test_expect_i(self, "... still February", DATETIME_MONTH_FEBRUARY, quadricentennial.month);

    test_case_end(self);

    test_case_begin(self, "the year rolls over");

    Datetime year_end = datetime_init_3(2024, DATETIME_MONTH_DECEMBER, 31);

    datetime_next_day(&year_end);

    test_expect_i(self, "2024-12-31 steps to 2025", 2025, year_end.year);
    test_expect_i(self, "... January", DATETIME_MONTH_JANUARY, year_end.month);
    test_expect_i(self, "... the 1st", 1, year_end.date);
    test_expect_i(self, "... day of year 1", 1, year_end.days);
    test_expect_i(self, "... a Wednesday", 3, year_end.day_week);

    test_case_end(self);

    test_case_begin(self, "a walk out of a leap February TERMINATES");

    /* The live caller's own shape - `while (!equal) next_day()` - under a step
     * bound. The old body incremented .date past 29 without ever reaching March,
     * so the walk never met its target; exhausting the bound is what that looks
     * like from here. */
    Datetime cursor = datetime_init_3(2024, DATETIME_MONTH_FEBRUARY, 28);
    Datetime const target = datetime_init_3(2024, DATETIME_MONTH_APRIL, 1);

    USize steps = 0;

    while (!datetime_compare_equal_date(&cursor, &target) && steps < 400) {
        datetime_next_day(&cursor);

        steps += 1;
    }

    test_expect_true(self, "the walk escaped February inside the bound", steps < 400);
    test_expect_u(self, "2024-02-28 to 2024-04-01 is 33 steps", 33, steps);
    test_expect_i(self, "the cursor landed in April", DATETIME_MONTH_APRIL, cursor.month);
    test_expect_i(self, "... on the 1st", 1, cursor.date);
    test_expect_i(self, "... in 2024", 2024, cursor.year);
    test_expect_i(self, "... a Monday", 1, cursor.day_week);

    test_case_end(self);

    test_suite_end(self);
}

/*==============================================================================
 * MARK: - Weekday suite
 *============================================================================*/

static void _test_weekday(Test *const self) {
    test_suite_begin(self, "weekday");

    test_case_begin(self, "known anchors resolve to the right day of the week");

    Datetime const monday   = datetime_init_3(2024, DATETIME_MONTH_JANUARY, 15);
    Datetime const saturday = datetime_init_3(2000, DATETIME_MONTH_JANUARY, 1);
    Datetime const thursday = datetime_init_3(1970, DATETIME_MONTH_JANUARY, 1);

    test_expect_i(self, "2024-01-15 was a Monday", 1, monday.day_week);
    test_expect_i(self, "2000-01-01 was a Saturday", 6, saturday.day_week);
    test_expect_i(self, "1970-01-01 was a Thursday", 4, thursday.day_week);

    test_case_end(self);

    test_case_begin(self, "the day of the year is leap-correct");

    Datetime const leap_march  = datetime_init_3(2024, DATETIME_MONTH_MARCH, 1);
    Datetime const plain_march = datetime_init_3(2023, DATETIME_MONTH_MARCH, 1);

    test_expect_i(self, "2024-03-01 is day 61", 61, leap_march.days);
    test_expect_i(self, "2023-03-01 is day 60", 60, plain_march.days);

    test_case_end(self);

    test_suite_end(self);
}

/*==============================================================================
 * MARK: - Render suite
 *============================================================================*/

static void _test_render(Test *const self) {
    test_suite_begin(self, "render");

    test_case_begin(self, "two-digit months render whole");

    /* The pin for the `< 10` pad: the old body took the SINGLE-digit conversion
     * path for month INDEX 9 - October - and rendered it as "01". September
     * (index 8) brackets the boundary from the other side. */
    Datetime october   = datetime_init_3(2024, DATETIME_MONTH_OCTOBER, 5);
    Datetime september = datetime_init_3(2024, DATETIME_MONTH_SEPTEMBER, 5);
    Datetime december  = datetime_init_3(2024, DATETIME_MONTH_DECEMBER, 5);

    char *const october_text   = datetime_to_date_char(&october);
    char *const september_text = datetime_to_date_char(&september);
    char *const december_text  = datetime_to_date_char(&december);

    test_expect_string(self, "October renders as 10", "2024-10-05", october_text);
    test_expect_string(self, "September renders as 09", "2024-09-05", september_text);
    test_expect_string(self, "December renders as 12", "2024-12-05", december_text);
    test_expect_u(self, "October's text is 10 characters", 10, char_length(october_text));

    char_delete(december_text);
    char_delete(september_text);
    char_delete(october_text);

    test_case_end(self);

    test_case_begin(self, "a year below 1000 renders padded, with no NUL mid-string");

    /* The old body copied a FIXED four bytes out of an UNPADDED conversion, so
     * 999 left a terminator inside the buffer and char_length reported 3. */
    Datetime small_year = datetime_init_3(999, DATETIME_MONTH_JANUARY, 1);

    char *const small_text = datetime_to_date_char(&small_year);

    test_expect_string(self, "999 renders as 0999", "0999-01-01", small_text);
    test_expect_u(self, "the whole string is reachable", 10, char_length(small_text));

    char_delete(small_text);

    test_case_end(self);

    test_case_begin(self, "the full form carries the clock");

    Datetime moment = datetime_init_5(2024, DATETIME_MONTH_OCTOBER, 5, 13, 45, 9);

    char *const full_text = datetime_to_char(&moment);

    test_expect_string(self, "full render", "2024-10-05 13:45:09", full_text);
    test_expect_u(self, "19 characters", 19, char_length(full_text));

    char_delete(full_text);

    test_case_end(self);

    test_case_begin(self, "the String forms report an honest size");

    String full_string = datetime_to_string(&moment);

    test_expect_string(self, "String render", "2024-10-05 13:45:09", string_get_data(&full_string));
    test_expect_u(self, "size is 19", 19, string_get_size(&full_string));
    test_expect_u(self, "size equals char_length of the data", char_length(string_get_data(&full_string)), string_get_size(&full_string));

    string_uninit(&full_string);

    String date_string = datetime_to_date_string(&moment);

    test_expect_string(self, "date-only String render", "2024-10-05", string_get_data(&date_string));
    test_expect_u(self, "size equals char_length of the data", char_length(string_get_data(&date_string)), string_get_size(&date_string));

    string_uninit(&date_string);

    test_case_end(self);

    test_suite_end(self);
}

/*==============================================================================
 * MARK: - Ownership suite
 *============================================================================*/

static void _test_ownership(Test *const self) {
    test_suite_begin(self, "ownership");

    /* str_init_2 built a VIEW over the fresh heap buffer, so every call leaked
     * it. There is no allocation counter to assert against here; what IS
     * observable is that the Str owns its bytes and releases them idempotently. */
    Datetime moment = datetime_init_5(2024, DATETIME_MONTH_OCTOBER, 5, 13, 45, 9);

    test_case_begin(self, "datetime_to_str hands back an OWNED Str");

    Str rendered = datetime_to_str(&moment);

    test_expect_not_null(self, "the Str carries data", str_get_data(&rendered));
    test_expect_string(self, "the Str carries the full render", "2024-10-05 13:45:09", str_get_data(&rendered));
    test_expect_u(self, "size is 19", 19, str_get_size(&rendered));

    str_uninit(&rendered);

    test_expect_null(self, "release drops the buffer", str_get_data(&rendered));
    test_expect_u(self, "release empties the Str", 0, str_get_size(&rendered));

    str_uninit(&rendered);

    test_expect_null(self, "a second release is safe", str_get_data(&rendered));
    test_expect_u(self, "... and leaves it empty", 0, str_get_size(&rendered));

    test_case_end(self);

    test_case_begin(self, "datetime_to_date_str owns its bytes too");

    Str date_only = datetime_to_date_str(&moment);

    test_expect_string(self, "date-only render", "2024-10-05", str_get_data(&date_only));
    test_expect_u(self, "size is 10", 10, str_get_size(&date_only));

    str_uninit(&date_only);

    test_expect_null(self, "release drops the buffer", str_get_data(&date_only));

    str_uninit(&date_only);

    test_expect_u(self, "a second release is safe", 0, str_get_size(&date_only));

    test_case_end(self);

    test_suite_end(self);
}

/*==============================================================================
 * MARK: - Print suite
 *============================================================================*/

#define _CAPTURE_PATH "_datetime_print_capture.txt"

typedef void (*FpDatetimePrint)(Datetime *const self);

/* datetime_print/_full write straight to stdout, so the only way to read them
 * back is to redirect the descriptor and put it back afterwards. The saved
 * descriptor is what restores the console - freopen alone would leave the rest
 * of the suite's own output inside the capture file. */
static void _capture_print(FpDatetimePrint const printer, Datetime *const value, char *const buffer, USize const capacity) {
    buffer[0] = '\0';

    fflush(stdout);

    I32 const saved = _TEST_DUP(_TEST_FILENO(stdout));
    FILE *const redirected = freopen(_CAPTURE_PATH, "w", stdout);

    if (redirected != nullptr) {
        printer(value);

        fflush(stdout);
    }

    _TEST_DUP2(saved, _TEST_FILENO(stdout));
    _TEST_CLOSE(saved);

    FILE *const capture = fopen(_CAPTURE_PATH, "r");

    if (capture != nullptr) {
        if (fgets(buffer, (I32) capacity, capture) == nullptr) {
            buffer[0] = '\0';
        }

        fclose(capture);
    }

    remove(_CAPTURE_PATH);
}

static void _test_print(Test *const self) {
    test_suite_begin(self, "print");

    char captured[128] = DEFAULT_INITIALIZATION;

    test_case_begin(self, "print_full renders whole day and month names");

    Datetime moment = datetime_init_5(2024, DATETIME_MONTH_OCTOBER, 5, 13, 45, 9);

    _capture_print(datetime_print_full, &moment, captured, sizeof captured);

    // 2024-10-05 was a Saturday. The names are three characters each and used to
    // come back beheaded.
    test_expect_string(self, "full render", "Sat Oct 05 2024 13:45:09", captured);

    Datetime new_year = datetime_init_5(2025, DATETIME_MONTH_JANUARY, 1, 0, 0, 0);

    _capture_print(datetime_print_full, &new_year, captured, sizeof captured);

    test_expect_string(self, "January on a Wednesday", "Wed Jan 01 2025 00:00:00", captured);

    test_case_end(self);

    test_case_begin(self, "print renders the padded ISO form");

    _capture_print(datetime_print, &moment, captured, sizeof captured);

    test_expect_string(self, "ISO render", "2024-10-05 13:45:09", captured);

    test_case_end(self);

    test_case_begin(self, "every month abbreviation survives the formatter table");

    char const *const names[DATETIME_MONTH_DECEMBER + 1] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

    char field[8] = DEFAULT_INITIALIZATION;

    for (I32 month = DATETIME_MONTH_JANUARY; month <= DATETIME_MONTH_DECEMBER; month += 1) {
        Datetime const each = datetime_init_3(2024, month, 1);

        datetime_format(&each, "%b", field, sizeof field);

        test_expect_string(self, "month abbreviation is whole", names[month], field);
    }

    test_case_end(self);

    test_suite_end(self);
}

/*==============================================================================
 * MARK: - Arena formatter suite
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
static void _test_arena_render(Test *const self) {
    test_suite_begin(self, "arena render");

    /* Sized generously on purpose: the arena allocator ABORTS on exhaustion, so
     * a tight arena would turn any render defect into a process death instead of
     * a failed assertion. */
    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    Datetime october = datetime_init_3(2024, DATETIME_MONTH_OCTOBER, 5);

    test_case_begin(self, "the arena char form renders like the heap form");

    char *const arena_text = datetime_to_date_alloc_char_1(&october, &arena);

    test_expect_string(self, "October renders as 10", "2024-10-05", arena_text);
    test_expect_u(self, "10 characters", 10, char_length(arena_text));

    test_case_end(self);

    test_case_begin(self, "the arena String form - the one body the rebase missed");

    /* This formatter kept the exact defect the rebase existed to remove: fixed
     * widths copied out of UNPADDED conversions, whose allocations are only
     * sign + digits + 1. char_length is the discriminating assertion, because
     * the old output ("999\0-01-01") compares equal on its first bytes. */
    String arena_string = datetime_to_date_alloc_string_1(&october, &arena);

    test_expect_string(self, "String render", "2024-10-05", string_get_data(&arena_string));
    test_expect_u(self, "size is 10", 10, string_get_size(&arena_string));
    test_expect_u(self, "size equals char_length of the data", char_length(string_get_data(&arena_string)), string_get_size(&arena_string));

    test_case_end(self);

    test_case_begin(self, "the short years that used to be read past");

    I32 const short_years[] = { 999, 99, 9 };
    char const *const expected[] = { "0999-01-01", "0099-01-01", "0009-01-01" };

    for (USize i = 0; i < 3; i += 1) {
        Datetime small = datetime_init_3(short_years[i], DATETIME_MONTH_JANUARY, 1);

        char *const small_char = datetime_to_date_alloc_char_1(&small, &arena);
        String small_string = datetime_to_date_alloc_string_1(&small, &arena);

        test_expect_string(self, "arena char pads the year", expected[i], small_char);
        test_expect_u(self, "arena char is 10 characters - no NUL mid-string", 10, char_length(small_char));
        test_expect_string(self, "arena String pads the year", expected[i], string_get_data(&small_string));
        test_expect_u(self, "arena String is 10 characters - no NUL mid-string", 10, char_length(string_get_data(&small_string)));
        test_expect_u(self, "... and reports that size", 10, string_get_size(&small_string));
    }

    test_case_end(self);

    test_case_begin(self, "the arena Str is a VIEW the arena reclaims");

    Str arena_str = datetime_to_date_alloc_str_1(&october, &arena);

    test_expect_string(self, "Str render", "2024-10-05", str_get_data(&arena_str));
    test_expect_u(self, "size is 10", 10, str_get_size(&arena_str));
    test_expect_false(self, "the Str does NOT own its buffer", arena_str.owned);

    test_case_end(self);

    /* Teardown is the arena's alone - releasing a view through str_uninit would
     * hand arena memory to free(). */
    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_suite_end(self);
}
#endif // ARENA_IMPLEMENTATION

/*==============================================================================
 * MARK: - Runner
 *============================================================================*/

int main(void) {
    LogConfig const log_config = {
        .level             = LOG_LEVEL_ERROR,
        .stream            = stdout,
        .timestamp_enabled = false,
        .autoflush         = false
    };

    log_init(log_config);

    Test test = test_init("tests/datetime/test_datetime.c");

    test_suite_begin(&test, "datetime");

    _test_format(&test);
    _test_epoch(&test);
    _test_encode(&test);
    _test_parse_validation(&test);
    _test_parse_try(&test);
    _test_compare(&test);
    _test_days_operation(&test);
    _test_timestamp_time(&test);
    _test_next_day(&test);
    _test_weekday(&test);
    _test_render(&test);
    _test_ownership(&test);
    _test_print(&test);

#ifdef ARENA_IMPLEMENTATION
    _test_arena_render(&test);
#endif // ARENA_IMPLEMENTATION

    test_suite_end(&test);

    return test_uninit(&test);
}