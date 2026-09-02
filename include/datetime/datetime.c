/*
 * datetime.c - Implementation of the date and time utilities for the C Libraries Framework
 *
 * Features implemented:
 *   - Exact civil-from-days and days-from-civil conversion (Howard Hinnant), replacing
 *     the fixed-length-year/month arithmetic that drifted a day or more per year
 *   - Leap-correct month lengths under the full Gregorian rule (1900 and 2100 are not leap)
 *   - A validating ISO "%F" parser shared by every from_* family, so a malformed or short
 *     date string is refused as data instead of reaching an aborting error_check
 *   - One bounded formatter (datetime_format) behind every string output helper
 *
 * See datetime.h for API documentation and usage examples.
 */
#include <datetime/datetime.h>

/*==============================================================================
 * MARK: - Macros and Constants
 *============================================================================*/
#define _DATETIME_ABBREVIATED_NAME_SIZE (CHAR_STATIC_SIZE("Jan") + CHAR_END_CHARACTER)
#define _DATETIME_DAY_IN_SECONDS 86400
#define _DATETIME_DAY_IN_WEEK 7
#define _DATETIME_EPOCH_DAY_INDEX 4
#define _DATETIME_FIELD_SIZE 16
#define _DATETIME_HOUR_IN_DAY 24
#define _DATETIME_HOUR_IN_SECONDS 3600
#define _DATETIME_HOUR_MAX 23
#define _DATETIME_MINUTE_IN_SECONDS 60
#define _DATETIME_MINUTE_MAX 59
#define _DATETIME_MONTH_IN_YEAR 12
#define _DATETIME_MONTH_MAX 11
#define _DATETIME_SECOND_MAX 59
#define _DATETIME_TO_DATE_FULL_SIZE CHAR_STATIC_SIZE("0000-00-00 00:00:00")
#define _DATETIME_TO_DATE_SIZE CHAR_STATIC_SIZE("0000-00-00")

/*==============================================================================
 * MARK: - Internal Implementations
 *============================================================================*/

/**
 * Days since the 1970-01-01 epoch for a civil date (month 1-12, day 1-31) - the
 * exact inverse of the Hinnant civil-from-days decode in _datetime_init. The
 * encode reads year/month/date (which both the decode and datetime_init_5 set
 * exactly), never the .days field, whose fixed-length-month accumulation drifts
 * a day across leap years. Untraced pure arithmetic, like _datetime_init.
 */
static ISize _datetime_days_from_civil(I32 const year, I32 const month, I32 const day) {
    ISize const y = year - (month <= 2 ? 1 : 0);
    ISize const era = (y >= 0 ? y : y - 399) / 400;
    ISize const year_of_era = y - era * 400;
    ISize const day_of_year = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    ISize const day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;

    return era * 146097 + day_of_era - 719468;
}

/**
 * Append up to size bytes of source into buffer, bounded so the terminator
 * always fits; returns the new written count (unchanged when nothing fits).
 */
static USize _datetime_format_append(char *const buffer, USize const capacity, USize const written, char const *const source, USize const size) {
    USize count = 0;

    while (count < size && written + count + CHAR_END_CHARACTER < capacity) {
        buffer[written + count] = source[count];
        count += 1;
    }

    return written + count;
}

/**
 * The full Gregorian rule, not the /4 approximation the module carried: 1900 and
 * 2100 are NOT leap years and the old test called them so.
 */
static bool _datetime_is_leap_year(I32 const year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

/**
 * Month is 0-based here, matching the Datetime field.
 */
static I32 _datetime_days_in_month(I32 const year, I32 const month) {
    I32 const lengths[_DATETIME_MONTH_IN_YEAR] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

    if (month < 0 || month >= _DATETIME_MONTH_IN_YEAR) {
        return 0;
    }

    return month == DATETIME_MONTH_FEBRUARY && _datetime_is_leap_year(year) ? 29 : lengths[month];
}

/**
 * Decode an epoch second into calendar fields. Untraced pure arithmetic.
 */
static Datetime _datetime_init(USize const epoch) {
    USize const epoch_days = epoch / _DATETIME_DAY_IN_SECONDS;

    // Exact civil-from-days (Howard Hinnant): shift the epoch onto the 400-year
    // cycle anchored at 0000-03-01 so leap years resolve without a branch, then
    // recover the year, month (1-12), and day of month. This replaces the old
    // fixed-length-year/month division, which drifted a day or more per year.
    USize const shifted = epoch_days + 719468;
    USize const era     = shifted / 146097;
    USize const doe     = shifted - era * 146097;
    USize const yoe     = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    USize const doy     = doe - (365 * yoe + yoe / 4 - yoe / 100);
    USize const mp      = (5 * doy + 2) / 153;
    I32   const day     = (I32) (doy - (153 * mp + 2) / 5 + 1);
    I32   const civil_month = (I32) (mp < 10 ? mp + 3 : mp - 9);

    Datetime datetime   = DEFAULT_INITIALIZATION;
    datetime.year       = (I32) (yoe + era * 400) + (civil_month <= 2 ? 1 : 0);
    datetime.month      = civil_month - 1;
    datetime.date       = day;
    datetime.hours      = (I32) (epoch / _DATETIME_HOUR_IN_SECONDS % _DATETIME_HOUR_IN_DAY);
    datetime.minutes    = (I32) (epoch / _DATETIME_MINUTE_IN_SECONDS % _DATETIME_MINUTE_IN_SECONDS);
    datetime.seconds    = (I32) (epoch % _DATETIME_MINUTE_IN_SECONDS);
    datetime.day_week   = (I32) ((epoch_days + _DATETIME_EPOCH_DAY_INDEX) % _DATETIME_DAY_IN_WEEK);

    // .days is the day of the year, matching datetime_init_5's field convention.
    // Leap-correct like every other producer: the fixed table this replaced put
    // every post-February date in a leap year one day out, so the same calendar
    // date carried a different .days depending on which constructor made it.
    for (I32 i = 0; i < datetime.month; i += 1) {
        datetime.days += _datetime_days_in_month(datetime.year, i);
    }

    datetime.days += datetime.date;

    return datetime;
}

/**
 * Parse an ISO "YYYY-MM-DD" date, VALIDATING it. The three from_* families used
 * to read fixed offsets - date[5], date[8] - without ever consulting the size
 * they were handed, so a short string read past its end, and an out-of-range
 * field (month 00, day 32) reached datetime_init_5's error_check and ENDED THE
 * PROCESS. A date string is DATA. vestigo carries a hand-written pre-validator
 * for exactly this reason (main_vestigo.c); this is that guard promoted into the
 * module, leap day included, so the consumer copy can retire.
 */
static bool _datetime_parse_iso_date(char const *const date, USize const date_size, Datetime *const out) {
    if (memory_empty((void*) date) || date_size < _DATETIME_TO_DATE_SIZE) {
        return false;
    }

    if (date[4] != '-' || date[7] != '-') {
        return false;
    }

    for (USize i = 0; i < _DATETIME_TO_DATE_SIZE; i += 1) {
        if (i == 4 || i == 7) {
            continue;
        }

        if (!char_is_number(date[i])) {
            return false;
        }
    }

    I32 const year  = (I32) char_to_numbers_uint_2(date, 4);
    I32 const month = (I32) char_to_numbers_uint_2(date + 5, 2);
    I32 const day   = (I32) char_to_numbers_uint_2(date + 8, 2);

    /* Year 0 is this module's INVALID sentinel, so the parser must not accept it:
     * otherwise the try_ form reports success for "0000-01-01" while
     * datetime_is_valid rejects the very object it just produced. */
    if (year < 1) {
        return false;
    }

    if (month < 1 || month > _DATETIME_MONTH_IN_YEAR) {
        return false;
    }

    if (day < 1 || day > _datetime_days_in_month(year, month - 1)) {
        return false;
    }

    *out = datetime_init_5(year, month - 1, day, 0, 0, 0);

    return true;
}

/*==============================================================================
 * MARK: - Public Implementations
 *============================================================================*/
#ifdef ARENA_IMPLEMENTATION
char* datetime_to_date_alloc_char_1(Datetime *const self, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    /* See datetime_to_char for why this goes through datetime_format. The old
     * body also borrowed six intermediate number strings from the arena per
     * call; the formatter needs none. */
    char *const buffer = char_alloc_new_1(_DATETIME_TO_DATE_SIZE + CHAR_END_CHARACTER, allocator);

    datetime_format(self, "%Y-%m-%d", buffer, _DATETIME_TO_DATE_SIZE + CHAR_END_CHARACTER);

    trace_log_pop();

    return buffer;
}

Str datetime_to_date_alloc_str_1(Datetime *const self, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    Str const str = str_alloc_init_3(datetime_to_date_alloc_char_1(self, allocator), _DATETIME_TO_DATE_SIZE, allocator);

    trace_log_pop();

    return str;
}

String datetime_to_date_alloc_string_1(Datetime *const self, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    /* Rendered through datetime_format, like the other six formatters. This body
     * was MISSED by that rebase and kept the exact defect the rebase existed to
     * remove: it copied fixed widths out of UNPADDED conversions, whose
     * allocations are only sign + digits + 1 - so a single-digit month read one
     * byte past its block and any year below 100 read one to two bytes past
     * its own, year 0 (this module's invalid sentinel) included. The three
     * intermediate arena borrows and their releases go with it. */
    String buffer = string_alloc_init_2(_DATETIME_TO_DATE_SIZE + CHAR_END_CHARACTER, allocator);

    USize const written = datetime_format(self, "%Y-%m-%d", string_get_data(&buffer), _DATETIME_TO_DATE_SIZE + CHAR_END_CHARACTER);

    string_set_size(&buffer, written);

    trace_log_pop();

    return buffer;
}
#endif // ARENA_IMPLEMENTATION

bool datetime_compare_equal(Datetime const *const self, Datetime const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    bool const match = self->year == data->year       &&
        self->month == data->month                    &&
        self->date == data->date                      &&
        self->hours == data->hours                    &&
        self->minutes == data->minutes                &&
        self->seconds == data->seconds;

    trace_log_pop();

    return match;
}

bool datetime_compare_equal_date(Datetime const *const self, Datetime const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    bool const match = self->year == data->year       &&
        self->month == data->month                    &&
        self->date == data->date;

    trace_log_pop();

    return match;
}

bool datetime_compare_equal_time(Datetime const *const self, Datetime const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    bool const match = self->hours == data->hours     &&
        self->minutes == data->minutes                &&
        self->seconds == data->seconds;

    trace_log_pop();

    return match;
}

bool datetime_compare_greater(Datetime const *const self, Datetime const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Compared on the SIGNED civil day count, not the unsigned timestamp:
     * datetime_to_days and datetime_to_timestamp_date cast a negative day count
     * to USize, so any pre-1970 operand wrapped and the comparison answered
     * BACKWARDS - including for this module's own invalid sentinel (year 0 is
     * day -719529), which every refused parse hands back. Two dates on the same
     * side of the epoch compared correctly, which is why this survived. */
    ISize const self_days = _datetime_days_from_civil(self->year, self->month + 1, self->date);
    ISize const data_days = _datetime_days_from_civil(data->year, data->month + 1, data->date);

    bool const match = self_days != data_days ? self_days > data_days : datetime_to_timestamp_time((Datetime*) self) > datetime_to_timestamp_time((Datetime*) data);

    trace_log_pop();

    return match;
}

bool datetime_compare_greater_date(Datetime const *const self, Datetime const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Signed civil days, not the unsigned timestamp - see datetime_compare_greater. */
    ISize const self_days = _datetime_days_from_civil(self->year, self->month + 1, self->date);
    ISize const data_days = _datetime_days_from_civil(data->year, data->month + 1, data->date);

    bool const match = self_days > data_days;

    trace_log_pop();

    return match;
}

bool datetime_compare_greater_equal(Datetime const *const self, Datetime const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Compared on the SIGNED civil day count, not the unsigned timestamp:
     * datetime_to_days and datetime_to_timestamp_date cast a negative day count
     * to USize, so any pre-1970 operand wrapped and the comparison answered
     * BACKWARDS - including for this module's own invalid sentinel (year 0 is
     * day -719529), which every refused parse hands back. Two dates on the same
     * side of the epoch compared correctly, which is why this survived. */
    ISize const self_days = _datetime_days_from_civil(self->year, self->month + 1, self->date);
    ISize const data_days = _datetime_days_from_civil(data->year, data->month + 1, data->date);

    bool const match = self_days != data_days ? self_days >= data_days : datetime_to_timestamp_time((Datetime*) self) >= datetime_to_timestamp_time((Datetime*) data);

    trace_log_pop();

    return match;
}

bool datetime_compare_greater_equal_date(Datetime const *const self, Datetime const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Signed civil days, not the unsigned timestamp - see datetime_compare_greater. */
    ISize const self_days = _datetime_days_from_civil(self->year, self->month + 1, self->date);
    ISize const data_days = _datetime_days_from_civil(data->year, data->month + 1, data->date);

    bool const match = self_days >= data_days;

    trace_log_pop();

    return match;
}

bool datetime_compare_greater_equal_time(Datetime const *const self, Datetime const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Time of day only, in seconds since midnight - the date is deliberately
     * ignored, which is what distinguishes this from the plain form. */
    USize const self_time = (USize) self->hours * _DATETIME_HOUR_IN_SECONDS + (USize) self->minutes * _DATETIME_MINUTE_IN_SECONDS + (USize) self->seconds;
    USize const data_time = (USize) data->hours * _DATETIME_HOUR_IN_SECONDS + (USize) data->minutes * _DATETIME_MINUTE_IN_SECONDS + (USize) data->seconds;

    bool const match = self_time >= data_time;

    trace_log_pop();

    return match;
}

bool datetime_compare_greater_time(Datetime const *const self, Datetime const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Time of day only, in seconds since midnight - the date is deliberately
     * ignored, which is what distinguishes this from the plain form. */
    USize const self_time = (USize) self->hours * _DATETIME_HOUR_IN_SECONDS + (USize) self->minutes * _DATETIME_MINUTE_IN_SECONDS + (USize) self->seconds;
    USize const data_time = (USize) data->hours * _DATETIME_HOUR_IN_SECONDS + (USize) data->minutes * _DATETIME_MINUTE_IN_SECONDS + (USize) data->seconds;

    bool const match = self_time > data_time;

    trace_log_pop();

    return match;
}

USize datetime_days_operation_sub(Datetime const *const self, Datetime const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Whole days between the two civil dates, on the exact day count - this
     * returned 0 unconditionally. Unsigned, so the magnitude is returned
     * whichever way round the pair is given. */
    ISize const self_days = _datetime_days_from_civil(self->year, self->month + 1, self->date);
    ISize const data_days = _datetime_days_from_civil(data->year, data->month + 1, data->date);
    ISize const difference = self_days > data_days ? self_days - data_days : data_days - self_days;

    trace_log_pop();

    return (USize) difference;
}

USize datetime_format(Datetime const *const self, char const *const format, char *const buffer, USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "format", (void*) format);
    error_check_null(LOG_METADATA, "buffer", (void*) buffer);

    if (capacity == 0) {
        trace_log_pop();

        return 0;
    }

    char const months_name[_DATETIME_MONTH_IN_YEAR][_DATETIME_ABBREVIATED_NAME_SIZE] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

    USize written = 0;

    for (USize i = 0; format[i] != '\0'; i += 1) {
        if (format[i] != '%' || format[i + 1] == '\0') {
            written = _datetime_format_append(buffer, capacity, written, &format[i], 1);

            continue;
        }

        i += 1;

        char field[_DATETIME_FIELD_SIZE] = DEFAULT_INITIALIZATION;

        switch (format[i]) {
            case 'Y': {
                I32 const rendered = snprintf(field, sizeof(field), "%.4d", self->year);

                written = _datetime_format_append(buffer, capacity, written, field, rendered < 0 ? 0 : (USize) rendered);

                break;
            }
            case 'm': {
                I32 const rendered = snprintf(field, sizeof(field), "%.2d", self->month + 1);

                written = _datetime_format_append(buffer, capacity, written, field, rendered < 0 ? 0 : (USize) rendered);

                break;
            }
            case 'd': {
                I32 const rendered = snprintf(field, sizeof(field), "%.2d", self->date);

                written = _datetime_format_append(buffer, capacity, written, field, rendered < 0 ? 0 : (USize) rendered);

                break;
            }
            case 'H': {
                I32 const rendered = snprintf(field, sizeof(field), "%.2d", self->hours);

                written = _datetime_format_append(buffer, capacity, written, field, rendered < 0 ? 0 : (USize) rendered);

                break;
            }
            case 'M': {
                I32 const rendered = snprintf(field, sizeof(field), "%.2d", self->minutes);

                written = _datetime_format_append(buffer, capacity, written, field, rendered < 0 ? 0 : (USize) rendered);

                break;
            }
            case 'S': {
                I32 const rendered = snprintf(field, sizeof(field), "%.2d", self->seconds);

                written = _datetime_format_append(buffer, capacity, written, field, rendered < 0 ? 0 : (USize) rendered);

                break;
            }
            case 'b': {
                if (self->month >= 0 && self->month < _DATETIME_MONTH_IN_YEAR) {
                    written = _datetime_format_append(buffer, capacity, written, months_name[self->month], char_length(months_name[self->month]));
                }

                break;
            }
            case '%': {
                written = _datetime_format_append(buffer, capacity, written, "%", 1);

                break;
            }
            default: {
                written = _datetime_format_append(buffer, capacity, written, "%", 1);
                written = _datetime_format_append(buffer, capacity, written, &format[i], 1);

                break;
            }
        }
    }

    buffer[written] = '\0';

    trace_log_pop();

    return written;
}

Datetime datetime_from_char_1(char const *const date, char const *const format) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "date", (void*) date);
    error_check_null(LOG_METADATA, "format", (void*) format);

    Datetime const datetime = datetime_from_char_4(date, char_length(date), format, char_length(format));

    trace_log_pop();

    return datetime;
}

Datetime datetime_from_char_2(char const *const date, char const *const format, USize const format_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "date", (void*) date);
    error_check_null(LOG_METADATA, "format", (void*) format);

    Datetime const datetime = datetime_from_char_4(date, char_length(date), format, format_size);

    trace_log_pop();

    return datetime;
}

Datetime datetime_from_char_3(char const *const date, USize const date_size, char const *const format) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "date", (void*) date);
    error_check_null(LOG_METADATA, "format", (void*) format);

    Datetime const datetime = datetime_from_char_4(date, date_size, format, char_length(format));

    trace_log_pop();

    return datetime;
}

Datetime datetime_from_char_4(char const *const date, USize const date_size, char const *const format, USize const format_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "date", (void*) date);
    error_check_null(LOG_METADATA, "format", (void*) format);

    /* Sizes are DATA here, not caller contract: an empty or short date string is
     * a parse failure, answered by the invalid Datetime below.
     *
     * The INVALID datetime, not "now": a refused parse used to return the current
     * time, which the caller cannot detect and is the most dangerous possible
     * default. datetime_is_valid answers it; the try_ twins report it directly. */
    Datetime datetime = DEFAULT_INITIALIZATION;

    if (char_compare_equal_2(format, format_size, "%F", CHAR_STATIC_SIZE("%F"))) {
        _datetime_parse_iso_date(date, date_size, &datetime);
    }

    trace_log_pop();

    return datetime;
}

bool datetime_from_char_try(char const *const date, USize const date_size, char const *const format, USize const format_size, Datetime *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "date", (void*) date);
    error_check_null(LOG_METADATA, "format", (void*) format);
    error_check_null(LOG_METADATA, "out", (void*) out);

    /* The reporting form: the plain from_* family answers an invalid Datetime,
     * which a caller has to remember to test. */
    *out = (Datetime) DEFAULT_INITIALIZATION;

    if (!char_compare_equal_2(format, format_size, "%F", CHAR_STATIC_SIZE("%F"))) {
        trace_log_pop();

        return false;
    }

    bool const parsed = _datetime_parse_iso_date(date, date_size, out);

    trace_log_pop();

    return parsed;
}

Datetime datetime_from_str_1(Str *const date, char const *const format) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "date", (void*) date);
    error_check_null(LOG_METADATA, "format", (void*) format);

    Datetime const datetime = datetime_from_str_2(date, format, char_length(format));

    trace_log_pop();

    return datetime;
}

Datetime datetime_from_str_2(Str *const date, char const *const format, USize const format_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "date", (void*) date);
    error_check_null(LOG_METADATA, "format", (void*) format);

    /* The INVALID datetime, not "now": a refused parse used to return the current
     * time, which the caller cannot detect and is the most dangerous possible
     * default. datetime_is_valid answers it; the try_ twins report it directly. */
    Datetime datetime = DEFAULT_INITIALIZATION;

    if (char_compare_equal_2(format, format_size, "%F", CHAR_STATIC_SIZE("%F"))) {
        _datetime_parse_iso_date(str_get_data(date), str_get_size(date), &datetime);
    }

    trace_log_pop();

    return datetime;
}

bool datetime_from_str_try(Str *const date, char const *const format, USize const format_size, Datetime *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "date", (void*) date);
    error_check_null(LOG_METADATA, "format", (void*) format);
    error_check_null(LOG_METADATA, "out", (void*) out);

    bool const parsed = datetime_from_char_try(str_get_size(date) == 0 ? "" : str_get_data(date), str_get_size(date), format, format_size, out);

    trace_log_pop();

    return parsed;
}

Datetime datetime_from_string_1(String *const date, char const *const format) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "date", (void*) date);
    error_check_null(LOG_METADATA, "format", (void*) format);

    Datetime const datetime = datetime_from_string_2(date, format, char_length(format));

    trace_log_pop();

    return datetime;
}

Datetime datetime_from_string_2(String *const date, char const *const format, USize const format_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "date", (void*) date);
    error_check_null(LOG_METADATA, "format", (void*) format);

    /* The INVALID datetime, not "now": a refused parse used to return the current
     * time, which the caller cannot detect and is the most dangerous possible
     * default. datetime_is_valid answers it; the try_ twins report it directly. */
    Datetime datetime = DEFAULT_INITIALIZATION;

    if (char_compare_equal_2(format, format_size, "%F", CHAR_STATIC_SIZE("%F"))) {
        _datetime_parse_iso_date(string_get_data(date), string_get_size(date), &datetime);
    }

    trace_log_pop();

    return datetime;
}

bool datetime_from_string_try(String *const date, char const *const format, USize const format_size, Datetime *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "date", (void*) date);
    error_check_null(LOG_METADATA, "format", (void*) format);
    error_check_null(LOG_METADATA, "out", (void*) out);

    bool const parsed = datetime_from_char_try(string_get_size(date) == 0 ? "" : string_get_data(date), string_get_size(date), format, format_size, out);

    trace_log_pop();

    return parsed;
}

Datetime datetime_init_1(void) {
    trace_log_push(LOG_METADATA);

    Datetime const datetime = _datetime_init((USize) time(nullptr));

    trace_log_pop();

    return datetime;
}

Datetime datetime_init_2(USize const epoch) {
    trace_log_push(LOG_METADATA);

    Datetime const datetime = _datetime_init(epoch);

    trace_log_pop();

    return datetime;
}

Datetime datetime_init_3(I32 const year, I32 const month, I32 const day) {
    trace_log_push(LOG_METADATA);

    Datetime const datetime = datetime_init_5(year, month, day, 0, 0, 0);

    trace_log_pop();

    return datetime;
}

Datetime datetime_init_4(I32 const hours, I32 const minutes, I32 const seconds) {
    trace_log_push(LOG_METADATA);

    /* The REAL ranges - see datetime_init_5. */
    error_check_wrong_value(LOG_METADATA, "hours < 0", hours < 0);
    error_check_wrong_value(LOG_METADATA, "hours > 23", hours > _DATETIME_HOUR_MAX);
    error_check_wrong_value(LOG_METADATA, "minutes < 0", minutes < 0);
    error_check_wrong_value(LOG_METADATA, "minutes > 59", minutes > _DATETIME_MINUTE_MAX);
    error_check_wrong_value(LOG_METADATA, "seconds < 0", seconds < 0);
    error_check_wrong_value(LOG_METADATA, "seconds > 59", seconds > _DATETIME_SECOND_MAX);

    /* Today's date with the caller's time of day. datetime_init_1 already decodes
     * the calendar EXACTLY (the Hinnant civil-from-days kernel), so the fields it
     * produces are kept - the old body overwrote year/month/days/date with
     * average-seconds-per-year arithmetic, the very drift _datetime_init was
     * rewritten to remove, and left .date as a day-of-year minus fixed month
     * lengths. Only the time of day is the caller's to set. */
    Datetime datetime = datetime_init_1();

    datetime.hours   = hours;
    datetime.minutes = minutes;
    datetime.seconds = seconds;

    trace_log_pop();

    return datetime;
}

Datetime datetime_init_5(I32 const year, I32 const month, I32 const day, I32 const hours, I32 const minutes, I32 const seconds) {
    trace_log_push(LOG_METADATA);

    /* The REAL ranges: hours 0-23, minutes and seconds 0-59, and a day bounded by
     * its OWN month rather than a blanket 31. The old checks accepted 24:60:60
     * and 31 February, which to_timestamp then rendered a full day past the
     * intended instant. Year 0 is refused because it is this module's invalid
     * sentinel - the header claims no constructor produces it, and until now
     * datetime_init_5(0, ...) did. */
    error_check_wrong_value(LOG_METADATA, "year < 1", year < 1);
    error_check_wrong_value(LOG_METADATA, "month < 0", month < 0);
    error_check_wrong_value(LOG_METADATA, "month > 11", month > _DATETIME_MONTH_MAX);
    error_check_wrong_value(LOG_METADATA, "day < 1", day < 1);
    error_check_wrong_value(LOG_METADATA, "day > days in month", day > _datetime_days_in_month(year, month));
    error_check_wrong_value(LOG_METADATA, "hours < 0", hours < 0);
    error_check_wrong_value(LOG_METADATA, "hours > 23", hours > _DATETIME_HOUR_MAX);
    error_check_wrong_value(LOG_METADATA, "minutes < 0", minutes < 0);
    error_check_wrong_value(LOG_METADATA, "minutes > 59", minutes > _DATETIME_MINUTE_MAX);
    error_check_wrong_value(LOG_METADATA, "seconds < 0", seconds < 0);
    error_check_wrong_value(LOG_METADATA, "seconds > 59", seconds > _DATETIME_SECOND_MAX);

    /* A ZEROED base, not datetime_init_1(): seeding from "now" meant .days
     * accumulated on top of today's day-of-year and .day_week was derived from
     * wall-clock state, so both fields were garbage for any constructed date. */
    Datetime datetime = DEFAULT_INITIALIZATION;

    datetime.year      = year;
    datetime.month     = month;
    datetime.date      = day;
    datetime.hours     = hours;
    datetime.minutes   = minutes;
    datetime.seconds   = seconds;

    /* Day of the year, leap-correct. */
    for (I32 i = 0; i < month; i += 1) {
        datetime.days += _datetime_days_in_month(year, i);
    }

    datetime.days += day;

    /* Weekday from the exact day count, not the fixed-length-year approximation
     * the old expression used (which drifted with every leap year). The epoch
     * 1970-01-01 was a Thursday, index 4. */
    ISize const epoch_days = _datetime_days_from_civil(year, month + 1, day);
    ISize const weekday = (epoch_days + _DATETIME_EPOCH_DAY_INDEX) % _DATETIME_DAY_IN_WEEK;

    datetime.day_week = (I32) (weekday < 0 ? weekday + _DATETIME_DAY_IN_WEEK : weekday);

    trace_log_pop();

    return datetime;
}

bool datetime_is_valid(Datetime const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* Year 0 is the module's invalid sentinel: no constructor produces it, and a
     * refused parse zeroes the whole object. The remaining tests catch a struct
     * that was hand-assembled or memset rather than constructed. */
    bool const valid = self->year >= 1                                  &&
        self->month >= 0                                                &&
        self->month <= _DATETIME_MONTH_MAX                              &&
        self->date >= 1                                                 &&
        self->date <= _datetime_days_in_month(self->year, self->month)  &&
        self->hours >= 0                                                &&
        self->hours <= _DATETIME_HOUR_MAX                               &&
        self->minutes >= 0                                              &&
        self->minutes <= _DATETIME_MINUTE_MAX                           &&
        self->seconds >= 0                                              &&
        self->seconds <= _DATETIME_SECOND_MAX;

    trace_log_pop();

    return valid;
}

void datetime_next_day(Datetime *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* Advance the civil date, then re-derive the dependent fields. The old body
     * tested February with a plain (year - 1972) % 4, so after Feb 29 in a leap
     * year the date incremented WITHOUT BOUND and never reached March - and its
     * one live caller loops `while (!equal) next_day()`, i.e. forever. The
     * century rule was missing too. */

    /* An out-of-range month is NORMALIZED rather than returned from: the caller
     * idiom is `while (!equal) next_day()`, so a call that changes nothing spins
     * forever. Only a hand-assembled struct (or an unchecked build) reaches it. */
    if (self->month < 0 || self->month >= _DATETIME_MONTH_IN_YEAR) {
        self->month = 0;
        self->date  = 1;
    }

    I32 const days_in_month = _datetime_days_in_month(self->year, self->month);

    if (self->date < 1 || self->date > days_in_month) {
        self->date = 1;
    }

    if (self->date < days_in_month) {
        self->date += 1;
    }
    else if (self->month < _DATETIME_MONTH_MAX) {
        self->month += 1;
        self->date   = 1;
    }
    else {
        self->year += 1;
        self->month = 0;
        self->date  = 1;
    }

    /* Day of year and weekday re-derived from the new civil date, so neither can
     * drift away from it. */
    self->days = 0;

    for (I32 i = 0; i < self->month; i += 1) {
        self->days += _datetime_days_in_month(self->year, i);
    }

    self->days += self->date;

    ISize const epoch_days = _datetime_days_from_civil(self->year, self->month + 1, self->date);
    ISize const weekday = (epoch_days + _DATETIME_EPOCH_DAY_INDEX) % _DATETIME_DAY_IN_WEEK;

    self->day_week = (I32) (weekday < 0 ? weekday + _DATETIME_DAY_IN_WEEK : weekday);

    trace_log_pop();
}

ISize datetime_now(void) {
    return (ISize) time(nullptr);
}

void datetime_print(Datetime *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    printf("%d-%.2d-%.2d %.2d:%.2d:%.2d", self->year, self->month + 1, self->date, self->hours, self->minutes, self->seconds);

    trace_log_pop();
}

void datetime_print_full(Datetime *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    char const days_name[_DATETIME_DAY_IN_WEEK][_DATETIME_ABBREVIATED_NAME_SIZE] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    char const months_name[_DATETIME_MONTH_IN_YEAR][_DATETIME_ABBREVIATED_NAME_SIZE] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

    /* Bounds-checked before indexing: Datetime is a public struct with public
     * fields, so a hand-assembled value can carry an out-of-range month or
     * day_week, and an unguarded index here is a stack over-read that printf
     * then walks to the first NUL. datetime_format's %b case already guards the
     * identical lookup; the two were inconsistent. */
    bool const day_in_range   = self->day_week >= 0 && self->day_week < _DATETIME_DAY_IN_WEEK;
    bool const month_in_range = self->month >= 0 && self->month < _DATETIME_MONTH_IN_YEAR;

    printf("%s %s %.2d %d %.2d:%.2d:%.2d", day_in_range ? days_name[self->day_week] : "???", month_in_range ? months_name[self->month] : "???",
        self->date, self->year, self->hours, self->minutes, self->seconds);

    trace_log_pop();
}

char* datetime_to_char(Datetime *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* Rendered through datetime_format rather than the hand-rolled concatenation
     * this replaces. That version had two latent defects the rebase removes
     * structurally: it padded the month with `< 10` instead of `< 9` on the
     * non-arena path (so October rendered as "01"), and it copied the year as a
     * fixed 4 bytes from an UNPADDED conversion (so a year below 1000 embedded a
     * NUL mid-string and a year below 100 read past the allocation). */
    char *const buffer = char_new_1(_DATETIME_TO_DATE_FULL_SIZE + CHAR_END_CHARACTER);

    datetime_format(self, "%Y-%m-%d %H:%M:%S", buffer, _DATETIME_TO_DATE_FULL_SIZE + CHAR_END_CHARACTER);

    trace_log_pop();

    return buffer;
}

char* datetime_to_date_char(Datetime *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* See datetime_to_char for why this goes through datetime_format. */
    char *const buffer = char_new_1(_DATETIME_TO_DATE_SIZE + CHAR_END_CHARACTER);

    datetime_format(self, "%Y-%m-%d", buffer, _DATETIME_TO_DATE_SIZE + CHAR_END_CHARACTER);

    trace_log_pop();

    return buffer;
}

Str datetime_to_date_str(Datetime *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* datetime_to_date_char hands back a fresh heap buffer, and str_init_3 would wrap it as a
     * non-owning VIEW - so str_uninit would skip it and the buffer would leak on every call.
     * Copy into an owned Str and release the original. */
    char *const buffer = datetime_to_date_char(self);

    Str const str = str_init_static(buffer, _DATETIME_TO_DATE_SIZE);

    char_delete(buffer);

    trace_log_pop();

    return str;
}

String datetime_to_date_string(Datetime *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* See datetime_to_char for why this goes through datetime_format. */
    String buffer = string_init_2(_DATETIME_TO_DATE_SIZE + CHAR_END_CHARACTER);

    USize const written = datetime_format(self, "%Y-%m-%d", string_get_data(&buffer), _DATETIME_TO_DATE_SIZE + CHAR_END_CHARACTER);

    string_set_size(&buffer, written);

    trace_log_pop();

    return buffer;
}

USize datetime_to_days(Datetime *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    USize const days = (USize) _datetime_days_from_civil(self->year, self->month + 1, self->date);

    trace_log_pop();

    return days;
}

Str datetime_to_str(Datetime *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* str_init_2 builds a VIEW, so the fresh heap buffer leaked on every call -
     * the same defect datetime_to_date_str already fixes above. Copy into an
     * owned Str and release the original. */
    char *const rendered = datetime_to_char(self);

    Str const buffer = str_init_static(rendered, char_length(rendered));

    char_delete(rendered);

    trace_log_pop();

    return buffer;
}

String datetime_to_string(Datetime *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* See datetime_to_char for why this goes through datetime_format. */
    String buffer = string_init_2(_DATETIME_TO_DATE_FULL_SIZE + CHAR_END_CHARACTER);

    USize const written = datetime_format(self, "%Y-%m-%d %H:%M:%S", string_get_data(&buffer), _DATETIME_TO_DATE_FULL_SIZE + CHAR_END_CHARACTER);

    string_set_size(&buffer, written);

    trace_log_pop();

    return buffer;
}

USize datetime_to_timestamp(Datetime *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    USize const timestamp = datetime_to_timestamp_date(self) + (USize) self->hours * _DATETIME_HOUR_IN_SECONDS + (USize) self->minutes * _DATETIME_MINUTE_IN_SECONDS + (USize) self->seconds;

    trace_log_pop();

    return timestamp;
}

USize datetime_to_timestamp_date(Datetime *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    USize const timestamp = (USize) (_datetime_days_from_civil(self->year, self->month + 1, self->date) * _DATETIME_DAY_IN_SECONDS);

    trace_log_pop();

    return timestamp;
}

USize datetime_to_timestamp_time(Datetime *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* Seconds since midnight. This was declared in the header and indexed in the
     * API docs but DEFINED NOWHERE - any caller was a link error waiting. */
    USize const timestamp = (USize) self->hours * _DATETIME_HOUR_IN_SECONDS + (USize) self->minutes * _DATETIME_MINUTE_IN_SECONDS + (USize) self->seconds;

    trace_log_pop();

    return timestamp;
}