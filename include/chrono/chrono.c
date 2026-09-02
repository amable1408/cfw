/* ============================================================================
 *  Chrono Implementation
 *  --------------------------------------------------------------------------
 *  @file    chrono.c
 *  @brief   Monotonic clock reads normalized to nanoseconds, plus the stopwatch
 *           and deadline arithmetic built on them.
 * ============================================================================
 */
#include <chrono/chrono.h>

/*==============================================================================
 * MARK: - Macros and Constants
 *============================================================================*/

/* Each promote threshold is the point at which three significant digits would
 * otherwise display a rolled-over unit.
 *
 * For the 1000-based boundaries that means 999.5 of the current unit, where %.3g
 * rounds to 1000 and switches to exponential notation — promoting exactly there
 * is what stops the renderer emitting "1e+03 us" instead of "1 ms".
 *
 * For the 60-based ones (min, h) it means 59.95, where the display would roll to
 * "60 s". These two are NOT the same rule stated twice, and the difference is
 * deliberate: 999.5/1000 is exactly 0.9995, so a 1000-based arm always opens at
 * "1", but 59.95/60 is 0.99916, so the minute and hour arms open with a ~20 ms
 * and ~1.2 s sliver rendering "0.999 min" / "0.999 h". That is accepted. The
 * alternative — promoting at 59.97 so the arm opens at "1 min" — puts "60 s"
 * back on the screen, and a unit picker that prints 60 of its own unit reads as
 * broken, where a leading 0.999 merely reads as precise. */

#define _CHRONO_MAXIMUM_FRACTIONAL_HOURS 3598200000000000ULL /**< 999.5 h; above this hours print as an integer. */
#define _CHRONO_NANOSECONDS_PER_HOUR 3600000000000ULL        /**< Nanoseconds in one hour. */
#define _CHRONO_NANOSECONDS_PER_MINUTE 60000000000ULL        /**< Nanoseconds in one minute. */
#define _CHRONO_PROMOTE_TO_HOUR 3597000000000ULL             /**< 59.95 min, in nanoseconds. */
#define _CHRONO_PROMOTE_TO_MICROSECOND 1000U                 /**< 1 us: below this, nanoseconds print exact. */
#define _CHRONO_PROMOTE_TO_MILLISECOND 999500U               /**< 999.5 us, in nanoseconds. */
#define _CHRONO_PROMOTE_TO_MINUTE 59950000000ULL             /**< 59.95 s, in nanoseconds. */
#define _CHRONO_PROMOTE_TO_SECOND 999500000ULL               /**< 999.5 ms, in nanoseconds. */

/*==============================================================================
 * MARK: - Internal Implementations
 *============================================================================*/

#ifdef OS_WINDOWS
/**
 * @brief Convert a performance-counter tick count to nanoseconds.
 *
 * Split rather than computed as ticks * 1e9 / frequency, which overflows U64
 * at 18446744073 ticks - about 30 minutes of uptime on a 10 MHz QPC, and about
 * 18 seconds on a 1 GHz raw TSC. Dividing first keeps
 * the whole-seconds term exact, and the remainder term is bounded by
 * frequency * 1e9 — safe for any frequency below ~1.8e10 Hz, which covers every
 * real counter (QPC reports 10 MHz on modern Windows, and even a raw TSC
 * frequency sits near 4e9). No __int128 is involved, so the technique needs
 * nothing beyond plain 64-bit arithmetic — worth keeping in mind if this
 * conversion is ever lifted out of the Windows-only path.
 *
 * @param ticks     Raw counter value.
 * @param frequency Counter ticks per second.
 * @return Nanoseconds, or 0 when frequency is 0.
 */
static U64 _chrono_ticks_to_nanoseconds(U64 const ticks, U64 const frequency) {
    if (frequency == 0) {
        return 0;
    }

    U64 const seconds   = ticks / frequency;
    U64 const remainder = ticks % frequency;

    return (seconds * CHRONO_NANOSECONDS_PER_SECOND) + ((remainder * CHRONO_NANOSECONDS_PER_SECOND) / frequency);
}
#endif // OS_WINDOWS

/**
 * @brief Multiply a count by a per-unit nanosecond factor, saturating.
 *
 * Shared by the three duration constructors. Saturation is deliberate: a
 * wrapped duration becomes a small number, and a deadline built from it lands
 * in the past and expires immediately.
 *
 * @param value  Count of whole units.
 * @param factor Nanoseconds per unit.
 * @return value * factor, or CHRONO_DURATION_MAX when that would overflow.
 */
static ChronoDuration _chrono_scale(U64 const value, U64 const factor) {
    if (value > CHRONO_DURATION_MAX / factor) {
        return CHRONO_DURATION_MAX;
    }

    return value * factor;
}

/*==============================================================================
 * MARK: - Public Implementations
 *============================================================================*/

ChronoDeadline chrono_deadline(ChronoDuration const from_now) {
    ChronoInstant const now = chrono_now();

    // Saturate instead of wrapping. Passing CHRONO_DURATION_MAX lands here too,
    // which is what makes chrono_deadline(chrono_seconds(huge)) mean "never"
    // rather than "already expired".
    if (from_now >= CHRONO_DEADLINE_NEVER - now) {
        return CHRONO_DEADLINE_NEVER;
    }

    return now + from_now;
}

bool chrono_deadline_expired(ChronoDeadline const self) {
    if (self == CHRONO_DEADLINE_NEVER) {
        return false;
    }

    return chrono_now() >= self;
}

ChronoDuration chrono_deadline_remaining(ChronoDeadline const self) {
    if (self == CHRONO_DEADLINE_NEVER) {
        return CHRONO_DURATION_MAX;
    }

    ChronoInstant const now = chrono_now();

    // Clamped so an expired deadline reads as 0 rather than wrapping to a huge
    // duration, which would let a "wait for the remaining time" loop hang.
    if (now >= self) {
        return 0;
    }

    return self - now;
}

U64 chrono_deadline_remaining_milliseconds(ChronoDeadline const self, U64 const maximum) {
    if (self == CHRONO_DEADLINE_NEVER) {
        return maximum;
    }

    ChronoDuration const remaining = chrono_deadline_remaining(self);

    if (remaining == 0) {
        return 0;
    }

    // Round UP. Truncating here would hand a 0 timeout to the wait call for any
    // remainder below one millisecond, turning the tail of every deadline into a
    // busy-spin. The -1/+1 form is the ceiling without an overflow-prone
    // remaining + 999999.
    U64 const ceiling = ((remaining - 1) / CHRONO_NANOSECONDS_PER_MILLISECOND) + 1;

    if (ceiling > maximum) {
        return maximum;
    }

    return ceiling;
}

ChronoDuration chrono_duration_add(ChronoDuration const self, ChronoDuration const other) {
    if (self > CHRONO_DURATION_MAX - other) {
        return CHRONO_DURATION_MAX;
    }

    return self + other;
}

ChronoDuration chrono_duration_difference(ChronoDuration const self, ChronoDuration const other) {
    // Always larger minus smaller, so the unsigned subtraction cannot wrap.
    if (self > other) {
        return self - other;
    }

    return other - self;
}

// Deliberately not CFW_ATTR_NODISCARD: the rendered buffer is the product and
// the length is auxiliary, so ignoring the return is the normal call shape. Same
// rationale as lap/restart below — do not "fix" in an attribute sweep.
USize chrono_duration_format(ChronoDuration const self, char *const buffer, USize const buffer_size) {
    error_check_null(LOG_METADATA, "buffer", (void*) buffer);

    if (buffer_size == 0) {
        return 0;
    }

    buffer[0] = '\0';

    if (buffer_size < CHRONO_FORMAT_SIZE) {
        return 0;
    }

    I32 written = 0;

    if (self < _CHRONO_PROMOTE_TO_MICROSECOND) {
        // Nanoseconds print exact: at this magnitude a fractional form would add
        // digits the clock cannot actually resolve.
        written = snprintf(buffer, buffer_size, "%llu ns", (unsigned long long) self);
    }
    else if (self < _CHRONO_PROMOTE_TO_MILLISECOND) {
        written = snprintf(buffer, buffer_size, "%.3g us", (F64) self / (F64) CHRONO_NANOSECONDS_PER_MICROSECOND);
    }
    else if (self < _CHRONO_PROMOTE_TO_SECOND) {
        written = snprintf(buffer, buffer_size, "%.3g ms", (F64) self / (F64) CHRONO_NANOSECONDS_PER_MILLISECOND);
    }
    else if (self < _CHRONO_PROMOTE_TO_MINUTE) {
        written = snprintf(buffer, buffer_size, "%.3g s", chrono_duration_seconds(self));
    }
    else if (self < _CHRONO_PROMOTE_TO_HOUR) {
        written = snprintf(buffer, buffer_size, "%.3g min", (F64) self / (F64) _CHRONO_NANOSECONDS_PER_MINUTE);
    }
    else if (self < _CHRONO_MAXIMUM_FRACTIONAL_HOURS) {
        written = snprintf(buffer, buffer_size, "%.3g h", (F64) self / (F64) _CHRONO_NANOSECONDS_PER_HOUR);
    }
    else {
        // Every finite ladder goes exponential at its own top, so the largest
        // unit ends in an integer print rather than another %.3g. CHRONO_DURATION_MAX
        // lands here as "5124095 h".
        written = snprintf(buffer, buffer_size, "%llu h", (unsigned long long) (self / _CHRONO_NANOSECONDS_PER_HOUR));
    }

    if (written < 0) {
        buffer[0] = '\0';

        return 0;
    }

    return (USize) written;
}

U64 chrono_duration_microseconds(ChronoDuration const self) {
    return self / CHRONO_NANOSECONDS_PER_MICROSECOND;
}

U64 chrono_duration_milliseconds(ChronoDuration const self) {
    return self / CHRONO_NANOSECONDS_PER_MILLISECOND;
}

F64 chrono_duration_seconds(ChronoDuration const self) {
    return (F64) self / (F64) CHRONO_NANOSECONDS_PER_SECOND;
}

ChronoDuration chrono_elapsed(ChronoInstant const self) {
    ChronoInstant const now = chrono_now();

    // A monotonic clock cannot run backwards, so this only triggers on an
    // instant that did not come from chrono_now (a hand-built value, or one
    // carried over from another process).
    if (now <= self) {
        return 0;
    }

    return now - self;
}

ChronoDuration chrono_microseconds(U64 const value) {
    return _chrono_scale(value, CHRONO_NANOSECONDS_PER_MICROSECOND);
}

ChronoDuration chrono_milliseconds(U64 const value) {
    return _chrono_scale(value, CHRONO_NANOSECONDS_PER_MILLISECOND);
}

ChronoInstant chrono_now(void) {
#ifdef OS_WINDOWS
    LARGE_INTEGER frequency = DEFAULT_INITIALIZATION;
    LARGE_INTEGER counter   = DEFAULT_INITIALIZATION;

    // Neither call can fail on XP or later, so the BOOL results are not checked.
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);

    // QuadPart is a signed LONGLONG, so this rejects a negative frequency as
    // well as a zero one. Casting a negative value to U64 would yield ~1.8e19,
    // which sails past a bare == 0 test and then wraps the conversion's
    // remainder term. Same guard as chrono_resolution, deliberately.
    if (frequency.QuadPart <= 0) {
        return 0;
    }

    return _chrono_ticks_to_nanoseconds((U64) counter.QuadPart, (U64) frequency.QuadPart);
#else
    struct timespec now = DEFAULT_INITIALIZATION;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }

    return ((U64) now.tv_sec * CHRONO_NANOSECONDS_PER_SECOND) + (U64) now.tv_nsec;
#endif // OS_WINDOWS
}

ChronoDuration chrono_resolution(void) {
#ifdef OS_WINDOWS
    LARGE_INTEGER frequency = DEFAULT_INITIALIZATION;

    QueryPerformanceFrequency(&frequency);

    if (frequency.QuadPart <= 0) {
        return 0;
    }

    ChronoDuration const nanoseconds = CHRONO_NANOSECONDS_PER_SECOND / (U64) frequency.QuadPart;

    // A counter faster than 1 GHz would floor to 0; report the 1 ns floor
    // instead, so a positive return always means the clock works.
    if (nanoseconds == 0) {
        return 1;
    }

    return nanoseconds;
#else
    struct timespec resolution = DEFAULT_INITIALIZATION;

    if (clock_getres(CLOCK_MONOTONIC, &resolution) != 0) {
        return 0;
    }

    return ((U64) resolution.tv_sec * CHRONO_NANOSECONDS_PER_SECOND) + (U64) resolution.tv_nsec;
#endif // OS_WINDOWS
}

ChronoDuration chrono_seconds(U64 const value) {
    return _chrono_scale(value, CHRONO_NANOSECONDS_PER_SECOND);
}

ChronoDuration chrono_stopwatch_elapsed(ChronoStopwatch const *const self) {
    error_check_null(LOG_METADATA, "self", (void*) self);

    // The clock is read only while running. That is what makes a paused watch
    // return the identical value on every call.
    if (!self->running) {
        return self->accumulated;
    }

    // Through chrono_duration_add rather than a bare +, so the module's stated
    // saturation policy holds at the stopwatch members too. Unreachable from the
    // public API (it needs ~584 years of running time) but the members are
    // public, and a hand-built watch must not be the one place that wraps.
    return chrono_duration_add(self->accumulated, chrono_elapsed(self->started_at));
}

// Deliberately not CFW_ATTR_NODISCARD, unlike the pure queries: calling lap
// purely to move the baseline to "now" and ignoring the span is a legitimate
// use. Same for restart. Do not "fix" this in an attribute sweep.
ChronoDuration chrono_stopwatch_lap(ChronoStopwatch *const self) {
    error_check_null(LOG_METADATA, "self", (void*) self);

    // Laps are differences of accumulated running time, not of wall instants,
    // so a pause between two laps does not stretch the second one.
    ChronoDuration const total = chrono_stopwatch_elapsed(self);

    // total >= last_lap_total holds through every path of this API, so the clamp
    // is unreachable from public use. It is here because the struct's members
    // are public: a hand-built or patched watch with last_lap_total > total
    // would otherwise wrap to a ~584-year lap, the exact never-ending duration
    // this module's saturation policy exists to prevent.
    if (total <= self->last_lap_total) {
        self->last_lap_total = total;

        return 0;
    }

    ChronoDuration const lap = total - self->last_lap_total;

    self->last_lap_total = total;

    return lap;
}

void chrono_stopwatch_pause(ChronoStopwatch *const self) {
    error_check_null(LOG_METADATA, "self", (void*) self);

    if (!self->running) {
        return;
    }

    self->accumulated = chrono_duration_add(self->accumulated, chrono_elapsed(self->started_at));
    self->running     = false;
}

ChronoDuration chrono_stopwatch_restart(ChronoStopwatch *const self) {
    error_check_null(LOG_METADATA, "self", (void*) self);

    ChronoDuration const total = chrono_stopwatch_elapsed(self);

    *self = chrono_stopwatch_start();

    return total;
}

void chrono_stopwatch_resume(ChronoStopwatch *const self) {
    error_check_null(LOG_METADATA, "self", (void*) self);

    if (self->running) {
        return;
    }

    self->started_at = chrono_now();
    self->running    = true;
}

bool chrono_stopwatch_running(ChronoStopwatch const *const self) {
    error_check_null(LOG_METADATA, "self", (void*) self);

    return self->running;
}

ChronoStopwatch chrono_stopwatch_start(void) {
    return (ChronoStopwatch){
        .started_at      = chrono_now(),
        .accumulated     = 0,
        .last_lap_total  = 0,
        .running         = true,
    };
}