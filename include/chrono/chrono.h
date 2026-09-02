/* ============================================================================
 *  Chrono
 *  --------------------------------------------------------------------------
 *  @file    chrono.h
 *  @brief   Monotonic high-resolution timing — instants, durations, stopwatch,
 *           deadlines.
 *  @author  CFW
 *  @date    2026-08-15
 *  @license MIT (see LICENSE file)
 *
 *  The framework's measuring clock, and the complement to datetime: datetime
 *  answers "what time is it in the world" (wall clock, seconds, calendar),
 *  chrono answers "how long did that take" (monotonic, nanoseconds, no
 *  calendar). A monotonic clock never jumps backwards when the system clock is
 *  adjusted by NTP or by the user, which is exactly what makes it the only
 *  correct source for elapsed time, timeouts, and benchmarks.
 *
 *  Everything is normalized to nanoseconds at read time, so instants subtract
 *  directly and durations are plain integers — no frequency value ever leaks
 *  into caller code.
 *
 *  Features:
 *    - chrono_now/chrono_elapsed: monotonic nanosecond instants, no init.
 *    - Duration constructors and extractors for microseconds, milliseconds and
 *      seconds, saturating rather than wrapping on overflow.
 *    - Saturating duration arithmetic callers can use directly:
 *      chrono_duration_add (which also chains deadlines without drift) and
 *      chrono_duration_difference (an absolute difference that cannot wrap).
 *    - chrono_duration_format: human-readable rendering with an auto-chosen
 *      unit from nanoseconds to hours.
 *    - ChronoStopwatch: start, elapsed, lap, pause, resume, restart, running.
 *    - Deadlines with a CHRONO_DEADLINE_NEVER sentinel and clamped remaining
 *      time, for timeout loops that never expire early on overflow, plus
 *      chrono_deadline_remaining_milliseconds for feeding a platform wait call
 *      without busy-spinning the final fraction of a millisecond.
 *    - chrono_resolution: the granularity the clock source reports.
 *
 *  Usage Examples:
 *    @code
 *    // Bare instants
 *    ChronoInstant const start   = chrono_now();
 *    do_work();
 *    ChronoDuration const taken  = chrono_elapsed(start);
 *    F64 const seconds           = chrono_duration_seconds(taken);
 *
 *    // Stopwatch, excluding setup from the measurement
 *    ChronoStopwatch watch = chrono_stopwatch_start();
 *    simulate_frame();
 *    chrono_stopwatch_pause(&watch);
 *    write_report();                       // not counted
 *    chrono_stopwatch_resume(&watch);
 *    simulate_frame();
 *    ChronoDuration const total = chrono_stopwatch_restart(&watch);
 *
 *    // Deadline loop
 *    ChronoDeadline const deadline = chrono_deadline(chrono_milliseconds(500));
 *
 *    while (!chrono_deadline_expired(deadline)) {
 *        poll_once();
 *    }
 *
 *    // Feeding a platform wait call: one converter owns the sentinel, the clamp
 *    // and the round-up, so the loop cannot busy-spin the last fraction of a ms.
 *    ChronoDeadline const budget = chrono_deadline(chrono_seconds(timeout_seconds));
 *
 *    poll(fds, count, (I32) chrono_deadline_remaining_milliseconds(budget, I32_MAX));
 *
 *    // Duration arithmetic stays inside the saturation policy
 *    ChronoDuration const spread = chrono_duration_difference(sample, median);
 *    char text[CHRONO_FORMAT_SIZE] = DEFAULT_INITIALIZATION;
 *
 *    chrono_duration_format(spread, text, sizeof(text)); // e.g. "13.4 ms"
 *    @endcode
 *
 *  Error Handling:
 *    - Reading the clock cannot fail on any supported target: Windows
 *      guarantees QueryPerformanceCounter/Frequency succeed on XP and later,
 *      and CLOCK_MONOTONIC has been mandatory on Linux since 2.6. The API is
 *      therefore infallible by design — a Result on the most-called function in
 *      a measurement loop would guard an unreachable path and be ignored at
 *      every call site.
 *    - On the theoretical POSIX failure, chrono_now and chrono_resolution
 *      return 0, matching the fallback already used in process.c. A 0
 *      resolution is the module's own signal that the clock source is broken.
 *      Note that this fails OPEN for deadlines: with the clock stuck at 0, no
 *      deadline built with a non-zero from_now ever expires, and
 *      chrono_deadline_remaining_milliseconds keeps returning that deadline's
 *      full original duration (clamped to maximum), so a wait loop sleeps its
 *      whole timeout and never makes progress. The tell is a budget that never
 *      counts down, not a maximum-valued return. One case fails closed instead:
 *      a deadline of exactly 0 satisfies 0 >= 0 and reports expired. A timeout
 *      guarding something that must fail closed should check chrono_resolution()
 *      once at startup rather than assume the clock works.
 *    - Functions taking a pointer validate it first with error_check_null,
 *      which aborts under ERROR_CHECK_ENABLED.
 *    - Arithmetic saturates instead of wrapping. A duration or deadline built
 *      from an absurd value becomes CHRONO_DURATION_MAX / CHRONO_DEADLINE_NEVER
 *      rather than a small number: a wrapped deadline lands in the past and
 *      expires instantly, which is the precise bug this policy exists to stop.
 *
 *  Thread Safety:
 *    - chrono_now, chrono_resolution, the conversions and the deadline
 *      functions are stateless and safe to call concurrently from any thread.
 *    - Instants are comparable ACROSS threads within one process, not merely
 *      within the thread that took them: QueryPerformanceCounter and
 *      CLOCK_MONOTONIC are both cross-core consistent on every supported target,
 *      so one thread may legitimately subtract an instant another thread took.
 *    - A ChronoStopwatch is a plain value owned by its caller; it carries no
 *      lock, so a single watch shared across threads needs external
 *      synchronization. One watch per thread needs none.
 *
 *  Platform Divergence:
 *    - System suspend is counted differently per platform: Windows QPC keeps
 *      advancing through sleep, while Linux CLOCK_MONOTONIC stops, excluding
 *      suspended time. A deadline spanning a laptop lid-close therefore expires
 *      on Windows and is pushed out on Linux. Neither is wrong and chrono does
 *      not try to hide the divergence; a timeout that must behave identically
 *      across a suspend needs a wall-clock (datetime) cross-check.
 *
 *  Memory Management:
 *    - No heap allocation and no module state. A ChronoStopwatch is four
 *      integers and may live on the stack, in a struct, or in an array.
 *
 *  Performance Characteristics:
 *    - chrono_now is a single clock read plus one integer multiply-divide, on
 *      the order of tens of nanoseconds. No lock, no init, no cached state, so
 *      it is callable before main and from any thread.
 *    - Windows reads QueryPerformanceFrequency per call deliberately. The
 *      frequency is fixed at boot and served from the shared user page, not a
 *      syscall, so caching it would buy a few nanoseconds in exchange for
 *      module state and a first-call data race.
 *    - This module deliberately omits trace_log_push/pop. Its whole purpose is
 *      to measure other code with minimal overhead, and a trace frame on every
 *      clock read would put tracelog inside every measurement. The pure hot
 *      functions in math/scalar.c omit it for the same reason.
 *
 *  Dependencies (Deps):
 *    - error (error.h — chains tracelog → log → console → types.h, and log.h
 *      supplies the <time.h> that declares struct timespec). The same chain
 *      supplies the <stdio.h> that chrono_duration_format needs for snprintf,
 *      via console.h; that is the module's only use of stdio.
 *    - Windows only: platform/windows/windows.h for QueryPerformanceCounter.
 *    - Linux REQUIRES -D_GNU_SOURCE (or any _POSIX_C_SOURCE >= 199309L).
 *      <time.h> declares struct timespec under plain ISO C, but clock_gettime,
 *      clock_getres and CLOCK_MONOTONIC are POSIX extensions that stay hidden
 *      under a strict -std=c23 without it, and the build then fails outright
 *      rather than degrading. Verified: the module compiles clean on Debian
 *      gcc 14.2 with the flag and fails with four errors without it. Every
 *      build/linux makefile already defines it, matching dir and thread.
 * ============================================================================
 */
#ifndef CHRONO_H
#define CHRONO_H

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/

#include <error/error.h>

/* Windows places QueryPerformanceCounter/Frequency in <windows.h>, and nothing
 * in the chain above pulls it in unconditionally, so it is listed here. The
 * POSIX side lists nothing, because log.h already chains the only header the
 * clock path needs (<time.h>) and re-including it would be a redundant chained
 * include - but that header alone is not sufficient: see the -D_GNU_SOURCE
 * requirement in the Dependencies block above. */
#ifdef OS_WINDOWS
#include <platform/windows/windows.h>
#endif // OS_WINDOWS

/*==============================================================================
 * MARK: - Macros and Constants
 *============================================================================*/

#define CHRONO_DEADLINE_NEVER U64_MAX               /**< Deadline that never expires; remaining time stays at this value. */
#define CHRONO_DURATION_MAX U64_MAX                 /**< Largest representable duration; the saturation ceiling. */
#define CHRONO_FORMAT_SIZE 32                       /**< Buffer size that always holds a chrono_duration_format result, terminator included. */
#define CHRONO_NANOSECONDS_PER_MICROSECOND 1000U    /**< Nanoseconds in one microsecond. */
#define CHRONO_NANOSECONDS_PER_MILLISECOND 1000000U /**< Nanoseconds in one millisecond. */
#define CHRONO_NANOSECONDS_PER_SECOND 1000000000U   /**< Nanoseconds in one second. */

/*==============================================================================
 * MARK: - Typedefs and Enums
 *============================================================================*/

/**
 * @typedef ChronoDeadline
 * @brief Instant at which an operation's time is up, in monotonic nanoseconds.
 *
 * Built by chrono_deadline and compared with chrono_deadline_expired. The value
 * CHRONO_DEADLINE_NEVER means "no deadline" and never expires.
 */
typedef U64 ChronoDeadline;

/**
 * @typedef ChronoDuration
 * @brief A span of time in nanoseconds.
 *
 * An absolute quantity, meaningful on its own and across machines: the
 * difference between two ChronoInstant values, or a span built with
 * chrono_milliseconds and friends.
 *
 * The nanosecond count is the value itself — there is deliberately no
 * chrono_nanoseconds constructor or chrono_duration_nanoseconds extractor,
 * because each would be the identity function.
 */
typedef U64 ChronoDuration;

/**
 * @typedef ChronoInstant
 * @brief A point on the monotonic clock, in nanoseconds.
 *
 * Meaningful only relative to another instant taken by the same process: the
 * zero point is unspecified and differs between processes and reboots. Never
 * store one, print one as a date, or compare one across machines — subtract two
 * to get a ChronoDuration, which is what carries meaning.
 */
typedef U64 ChronoInstant;

/**
 * @struct ChronoStopwatch
 * @brief Accumulating timer with pause, resume and lap support.
 *
 * Create with chrono_stopwatch_start and pass by pointer thereafter. Laps are
 * measured against accumulated running time rather than wall instants, so time
 * spent paused never inflates a lap. Treat the members as private; read the
 * watch through chrono_stopwatch_elapsed and chrono_stopwatch_running.
 *
 * A zero-initialized watch ({0} or DEFAULT_INITIALIZATION) is also valid: it is
 * paused at zero, reports 0 elapsed, and starts counting on the first
 * chrono_stopwatch_resume. Use that form when the watch is embedded in a struct
 * that is initialized before timing should begin. This is a contract, not an
 * accident of the current layout — a future representation must preserve it.
 */
typedef struct ChronoStopwatch {
    ChronoInstant started_at;      /**< Instant the current running segment began; stale while paused. */
    ChronoDuration accumulated;    /**< Running time banked by segments completed before the current one. */
    ChronoDuration last_lap_total; /**< Total elapsed at the previous lap, the baseline for the next one. */
    bool running;                  /**< true while the watch is counting. */
} ChronoStopwatch;

/*==============================================================================
 * MARK: - Public Functions
 *============================================================================*/

/**
 * @brief Build a deadline a given duration from now.
 * @param from_now Duration until the deadline expires.
 * @return The deadline instant, or CHRONO_DEADLINE_NEVER when the sum would
 *         overflow (which includes passing CHRONO_DURATION_MAX).
 */
CFW_ATTR_NODISCARD
ChronoDeadline chrono_deadline(ChronoDuration const from_now);

/**
 * @brief Test whether a deadline has passed.
 * @param self The deadline to test.
 * @return true when the monotonic clock has reached self; always false for
 *         CHRONO_DEADLINE_NEVER.
 */
CFW_ATTR_NODISCARD
bool chrono_deadline_expired(ChronoDeadline const self);

/**
 * @brief Time left before a deadline expires.
 * @param self The deadline to measure against.
 * @return Remaining duration, 0 once expired (never a wrapped huge value), or
 *         CHRONO_DURATION_MAX for CHRONO_DEADLINE_NEVER.
 *
 * @warning Do not narrow this value for a platform wait call. It truncates
 *          rather than rounds up, and the CHRONO_DEADLINE_NEVER case overflows
 *          every fixed-width timeout. Use chrono_deadline_remaining_milliseconds,
 *          which owns both problems.
 */
CFW_ATTR_NODISCARD
ChronoDuration chrono_deadline_remaining(ChronoDeadline const self);

/**
 * @brief Remaining time as a millisecond count safe to hand to a wait call.
 * @param self    The deadline to measure against.
 * @param maximum Largest value the target wait API accepts (for example I32_MAX
 *                for poll, or INFINITE - 1 for WaitForSingleObject).
 * @return 0 once expired; maximum for CHRONO_DEADLINE_NEVER; otherwise the
 *         remaining time rounded UP to whole milliseconds and clamped to
 *         maximum.
 *
 * Rounding up is the whole point of this function. Truncating a final fraction
 * of a millisecond yields a 0 timeout, so the wait returns immediately and the
 * caller busy-spins until expiry; rounding up wakes at most one millisecond
 * late, which the expired-check loop absorbs. Truncation is right for reporting
 * elapsed time and wrong for deciding how long to sleep, so the two directions
 * are separate functions rather than one shared conversion.
 *
 * A caller that wants a genuinely infinite wait should test for
 * CHRONO_DEADLINE_NEVER itself and pass its API's own infinite sentinel; this
 * function returns maximum instead, which waits as long as the API allows and
 * then re-checks.
 */
CFW_ATTR_NODISCARD
U64 chrono_deadline_remaining_milliseconds(ChronoDeadline const self, U64 const maximum);

/**
 * @brief Sum of two durations, saturating at CHRONO_DURATION_MAX.
 * @param self  First duration.
 * @param other Second duration.
 * @return self + other, or CHRONO_DURATION_MAX when the sum would overflow.
 *
 * Also chains deadlines without drift: adding a period to the previous deadline,
 * rather than building each one from chrono_now, keeps a fixed-rate loop from
 * accumulating its own processing jitter into the period. A CHRONO_DEADLINE_NEVER
 * input saturates back to CHRONO_DEADLINE_NEVER, so a never-expiring chain stays
 * never-expiring.
 */
CFW_ATTR_NODISCARD CFW_ATTR_CONST
ChronoDuration chrono_duration_add(ChronoDuration const self, ChronoDuration const other);

/**
 * @brief Absolute difference of two durations, in either order.
 * @param self  First duration.
 * @param other Second duration.
 * @return |self - other|.
 *
 * Cannot wrap: the subtraction is always performed larger-minus-smaller. This is
 * what a deviation-from-median computation needs, since durations are unsigned
 * and a plain self - other yields roughly 1.8e19 whenever other is the larger.
 */
CFW_ATTR_NODISCARD CFW_ATTR_CONST
ChronoDuration chrono_duration_difference(ChronoDuration const self, ChronoDuration const other);

/**
 * @brief Render a duration into a human-readable string with an auto-chosen unit.
 * @param self        Duration to render.
 * @param buffer      Destination for the NUL-terminated string.
 * @param buffer_size Capacity of buffer; CHRONO_FORMAT_SIZE always suffices.
 * @return Characters written, excluding the terminator, or 0 when
 *         buffer_size < CHRONO_FORMAT_SIZE (buffer then holds an empty string;
 *         the check is against the constant, not the rendered length, so a call
 *         that works for one duration works for all).
 *
 * Picks the unit by magnitude — ns, us, ms, s, min, h — with three significant
 * digits on the fractional forms ("13.4 ms", "1.25 s", "2 h") and exact integers
 * at both ends of the ladder. Each unit promotes at the point where three
 * significant digits would otherwise display a rolled-over unit, so no input
 * renders as "1e+03 us" or as "60 s". One consequence is visible: because
 * minutes and hours roll at 60 rather than 1000, a narrow band at the bottom of
 * those two units renders as "0.999 min" and "0.999 h" rather than "1 min" and
 * "1 h". That is the deliberate trade — a leading 0.999 reads as precise, while
 * a "60 s" reads as the unit picker having failed.
 *
 * This is the one place in the module where F64 formatting is correct, because
 * the output is display by definition and never feeds further arithmetic.
 *
 * The decimal separator follows the C locale, which CFW never changes: no module
 * calls setlocale, so the separator is always '.'. A program that calls
 * setlocale(LC_ALL, "") for its own display purposes would see "13,4 ms" under a
 * European locale.
 */
USize chrono_duration_format(ChronoDuration const self, char *const buffer, USize const buffer_size);

/**
 * @brief Whole microseconds in a duration.
 * @param self Duration in nanoseconds.
 * @return Microseconds, truncated toward zero.
 */
CFW_ATTR_NODISCARD CFW_ATTR_CONST
U64 chrono_duration_microseconds(ChronoDuration const self);

/**
 * @brief Whole milliseconds in a duration.
 * @param self Duration in nanoseconds.
 * @return Milliseconds, truncated toward zero.
 */
CFW_ATTR_NODISCARD CFW_ATTR_CONST
U64 chrono_duration_milliseconds(ChronoDuration const self);

/**
 * @brief Fractional seconds in a duration, for display.
 * @param self Duration in nanoseconds.
 * @return Seconds as a double (e.g. 0.0134 for 13.4 ms). Use the integer
 *         extractors when the value feeds further arithmetic. Durations beyond
 *         2^53 ns (about 104 days) lose nanosecond precision to the F64
 *         mantissa, which is harmless for display and the reason this is the
 *         only floating-point extractor.
 */
CFW_ATTR_NODISCARD CFW_ATTR_CONST
F64 chrono_duration_seconds(ChronoDuration const self);

/**
 * @brief Time elapsed since an instant.
 * @param self Instant taken earlier by chrono_now in this same process.
 * @return Duration from self to now, or 0 if self is in the future (which a
 *         monotonic clock only permits when self did not come from chrono_now).
 */
CFW_ATTR_NODISCARD
ChronoDuration chrono_elapsed(ChronoInstant const self);

/**
 * @brief Build a duration from microseconds.
 * @param value Count of microseconds.
 * @return The duration in nanoseconds, saturating at CHRONO_DURATION_MAX.
 */
CFW_ATTR_NODISCARD CFW_ATTR_CONST
ChronoDuration chrono_microseconds(U64 const value);

/**
 * @brief Build a duration from milliseconds.
 * @param value Count of milliseconds.
 * @return The duration in nanoseconds, saturating at CHRONO_DURATION_MAX.
 */
CFW_ATTR_NODISCARD CFW_ATTR_CONST
ChronoDuration chrono_milliseconds(U64 const value);

/**
 * @brief Read the monotonic clock.
 * @return Current instant in nanoseconds. Comparable only with other instants
 *         from this process; 0 only if the platform clock failed.
 */
CFW_ATTR_NODISCARD
ChronoInstant chrono_now(void);

/**
 * @brief Granularity the underlying clock reports.
 * @return Smallest interval the clock source claims to distinguish, in
 *         nanoseconds (100 on a typical Windows QPC, 1 on Linux), or 0 if the
 *         clock source failed.
 *
 * This is the reported tick period, not the observed read-to-read delta, which
 * is dominated by the cost of the read itself (tens of nanoseconds). Linux
 * reports 1 ns and is nowhere near that precise in practice. Code that needs the
 * effective granularity — a benchmark sizing its minimum sample — should measure
 * consecutive chrono_now deltas rather than trust this value.
 */
CFW_ATTR_NODISCARD
ChronoDuration chrono_resolution(void);

/**
 * @brief Build a duration from seconds.
 * @param value Count of seconds.
 * @return The duration in nanoseconds, saturating at CHRONO_DURATION_MAX.
 */
CFW_ATTR_NODISCARD CFW_ATTR_CONST
ChronoDuration chrono_seconds(U64 const value);

/**
 * @brief Total running time measured by a stopwatch.
 * @param self Pointer to the stopwatch.
 * @return Accumulated running time, excluding every paused interval. Stays
 *         bit-for-bit constant while the watch is paused.
 */
CFW_ATTR_NODISCARD
ChronoDuration chrono_stopwatch_elapsed(ChronoStopwatch const *const self);

/**
 * @brief Take a split time without stopping the watch.
 * @param self Pointer to the stopwatch.
 * @return Running time since the previous lap, or since the watch started for
 *         the first lap. Paused intervals are excluded.
 */
ChronoDuration chrono_stopwatch_lap(ChronoStopwatch *const self);

/**
 * @brief Stop counting, keeping the time measured so far.
 * @param self Pointer to the stopwatch.
 *
 * A no-op on an already-paused watch, so pause/resume pairs need no state
 * tracking at the call site.
 */
void chrono_stopwatch_pause(ChronoStopwatch *const self);

/**
 * @brief Report the total and reset the watch to a fresh running state.
 * @param self Pointer to the stopwatch.
 * @return Total running time measured before the reset. The watch resumes from
 *         zero and is running when this returns, even if it was paused.
 */
ChronoDuration chrono_stopwatch_restart(ChronoStopwatch *const self);

/**
 * @brief Continue counting after a pause.
 * @param self Pointer to the stopwatch.
 *
 * A no-op on an already-running watch, so a stray resume cannot double-count.
 */
void chrono_stopwatch_resume(ChronoStopwatch *const self);

/**
 * @brief Whether the watch is currently counting.
 * @param self Pointer to the stopwatch.
 * @return true while running; false while paused.
 *
 * The question elapsed cannot answer: a paused watch and one that has been
 * running for zero nanoseconds both report 0.
 */
CFW_ATTR_NODISCARD
bool chrono_stopwatch_running(ChronoStopwatch const *const self);

/**
 * @brief Create a stopwatch that is already running.
 * @return A started stopwatch, by value. The struct holds only integers and no
 *         pointer into itself, so returning it by value is safe.
 */
CFW_ATTR_NODISCARD
ChronoStopwatch chrono_stopwatch_start(void);

#endif // CHRONO_H