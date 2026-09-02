#include <string.h>

#include <chrono/chrono.h>
#include <log/log.h>
#include <test/test.h>
#include <thread/thread.h>

/* Coverage for chrono: clock monotonicity and resolution, the duration
 * constructors/extractors including truncation and saturation, the stopwatch
 * (elapsed, pause/resume exclusion, lap baselines, restart), and the deadline
 * arithmetic including the NEVER sentinel and overflow saturation.
 *
 * Timing assertions are deliberately one-sided. This machine's wall-clock
 * measurements vary by tens of percent under load, so a test may assert that
 * time advanced at least as far as a sleep, or that one measured span is
 * smaller than another that strictly contains it, but never that an operation
 * finished within some upper bound - a loaded machine can stall arbitrarily
 * long, and such a bound would flake rather than detect anything.
 *
 * Everything that is exact is asserted exactly: unit conversions, the frozen
 * value a paused stopwatch reports, lap baselines, and every deadline
 * saturation and clamp result.
 *
 * Null-pointer cases are deliberately absent: those checks are error_check_null,
 * which aborts the process under ERROR_CHECK_ENABLED, so they cannot be
 * observed from inside the suite. */

#define TEST_PAUSE_MILLISECONDS 60
#define TEST_SLEEP_MILLISECONDS 20
#define TEST_SLEEP_TOLERANCE_MILLISECONDS 15

/*==============================================================================
 * MARK: - Clock
 *============================================================================*/

static void _test_chrono_now_advances(Test *const test) {
    test_case_begin(test, "chrono_now advances across a sleep");

    ChronoInstant const start = chrono_now();

    thread_sleep(TEST_SLEEP_MILLISECONDS);

    ChronoInstant const end = chrono_now();

    test_expect_true(test, "clock is non-zero (platform clock works)", start > 0);
    test_expect_true(test, "clock advanced", end > start);
    // Uses chrono_duration_difference rather than a raw subtraction: the assertion
    // above already proves end > start, so this is dogfooding on real clock values
    // rather than a needed guard.
    test_expect_true(test, "advanced by at least the sleep, minus tolerance",
        chrono_duration_milliseconds(chrono_duration_difference(end, start)) >= TEST_SLEEP_TOLERANCE_MILLISECONDS);

    test_case_end(test);
}

static void _test_chrono_now_never_decreases(Test *const test) {
    test_case_begin(test, "chrono_now never decreases over 100000 reads");

    ChronoInstant previous = chrono_now();
    bool monotonic         = true;

    for (USize i = 0; i < 100000; i += 1) {
        ChronoInstant const current = chrono_now();

        if (current < previous) {
            monotonic = false;

            break;
        }

        previous = current;
    }

    test_expect_true(test, "sequence is monotonic non-decreasing", monotonic);

    test_case_end(test);
}

static void _test_chrono_elapsed(Test *const test) {
    test_case_begin(test, "chrono_elapsed measures forward, clamps backward");

    ChronoInstant const start = chrono_now();

    thread_sleep(TEST_SLEEP_MILLISECONDS);

    ChronoDuration const taken = chrono_elapsed(start);

    test_expect_true(test, "elapsed covers at least the sleep, minus tolerance",
        chrono_duration_milliseconds(taken) >= TEST_SLEEP_TOLERANCE_MILLISECONDS);
    test_expect_u(test, "an instant in the future clamps to 0", 0, (USize) chrono_elapsed(chrono_now() + chrono_seconds(60)));

    test_case_end(test);
}

static void _test_chrono_resolution(Test *const test) {
    test_case_begin(test, "chrono_resolution reports a usable granularity");

    ChronoDuration const resolution = chrono_resolution();

    test_expect_true(test, "resolution is positive (clock source is alive)", resolution > 0);
    test_expect_true(test, "resolution is at most 1 ms", resolution <= CHRONO_NANOSECONDS_PER_MILLISECOND);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Deadlines
 *============================================================================*/

static void _test_chrono_deadline_future(Test *const test) {
    test_case_begin(test, "a future deadline is pending with bounded remaining time");

    ChronoDuration const window   = chrono_milliseconds(500);
    ChronoDeadline const deadline = chrono_deadline(window);

    ChronoDuration const remaining = chrono_deadline_remaining(deadline);

    test_expect_false(test, "not expired yet", chrono_deadline_expired(deadline));
    test_expect_true(test, "remaining is positive", remaining > 0);
    // Exact by construction, not a timing bound: remaining is deadline - now,
    // and deadline was built as now + window, so it can never exceed window.
    test_expect_true(test, "remaining never exceeds the window", remaining <= window);

    test_case_end(test);
}

static void _test_chrono_deadline_expired(Test *const test) {
    test_case_begin(test, "an elapsed deadline expires and clamps remaining to 0");

    ChronoDeadline const deadline = chrono_deadline(0);

    thread_sleep(TEST_SLEEP_MILLISECONDS);

    test_expect_true(test, "expired", chrono_deadline_expired(deadline));
    test_expect_u(test, "remaining clamps to exactly 0", 0, (USize) chrono_deadline_remaining(deadline));

    test_case_end(test);
}

static void _test_chrono_deadline_never(Test *const test) {
    test_case_begin(test, "CHRONO_DEADLINE_NEVER never expires");

    test_expect_false(test, "NEVER is not expired", chrono_deadline_expired(CHRONO_DEADLINE_NEVER));
    test_expect_u(test, "NEVER reports maximum remaining", (USize) CHRONO_DURATION_MAX, (USize) chrono_deadline_remaining(CHRONO_DEADLINE_NEVER));

    test_case_end(test);
}

static void _test_chrono_deadline_saturates(Test *const test) {
    test_case_begin(test, "deadline overflow saturates to NEVER, not into the past");

    ChronoDeadline const from_maximum = chrono_deadline(CHRONO_DURATION_MAX);
    ChronoDeadline const from_huge    = chrono_deadline(chrono_seconds(U64_MAX));

    test_expect_u(test, "CHRONO_DURATION_MAX yields NEVER", (USize) CHRONO_DEADLINE_NEVER, (USize) from_maximum);
    test_expect_u(test, "a saturated duration yields NEVER", (USize) CHRONO_DEADLINE_NEVER, (USize) from_huge);
    // The wrap this guards against would place the deadline in the past, so the
    // saturated value must also behave as pending.
    test_expect_false(test, "the saturated deadline is not already expired", chrono_deadline_expired(from_maximum));

    test_case_end(test);
}

static void _test_chrono_deadline_remaining_milliseconds(Test *const test) {
    test_case_begin(test, "the wait converter rounds up, clamps, and owns the sentinel");

    // The round-up, stated as an exact value without asserting any upper time
    // bound. The half-millisecond offset is load-bearing: a whole 500 ms deadline
    // is useless as a detector, because two adjacent clock reads usually land in
    // the same 100 ns QPC tick, leaving remaining at exactly 500000000 ns where
    // truncation and rounding up agree. At 500.5 ms they diverge - truncation
    // says 500, rounding up says 501.
    //
    // remaining is monotonically non-increasing, so a read taken AFTER the
    // conversion bounds what the conversion itself saw. If that later read is
    // still above 500 ms, the conversion's input was inside (500, 500.5] ms and
    // 501 is forced. If it is not, this sample was simply too slow: retake it
    // rather than assert a deadline on our own execution, which is the upper
    // bound this suite forswears.
    bool verified = false;

    for (USize attempt = 0; attempt < 8 && !verified; attempt += 1) {
        ChronoDeadline const window = chrono_deadline(chrono_milliseconds(500) + 500000);

        U64 const wait             = chrono_deadline_remaining_milliseconds(window, U64_MAX);
        ChronoDuration const after = chrono_deadline_remaining(window);

        if (after > chrono_milliseconds(500)) {
            test_expect_u(test, "500.5 ms rounds up to 501, never down to 500", 501, (USize) wait);

            verified = true;
        }
    }

    test_expect_true(test, "a round-up sample was taken inside the slack", verified);

    test_expect_u(test, "NEVER yields the caller's maximum", 4096, (USize) chrono_deadline_remaining_milliseconds(CHRONO_DEADLINE_NEVER, 4096));

    // A deadline of its own, with slack no plausible stall can eat. Reusing the
    // retry loop's 500.5 ms window here would make the clamp exact only while
    // that window still had more than 10 ms left - an upper bound on this suite's
    // own execution, which is the very thing the file header forswears.
    ChronoDeadline const generous = chrono_deadline(chrono_seconds(60));

    test_expect_u(test, "a long deadline clamps to the maximum", 10, (USize) chrono_deadline_remaining_milliseconds(generous, 10));

    ChronoDeadline const past = chrono_deadline(0);

    thread_sleep(TEST_SLEEP_MILLISECONDS);

    test_expect_u(test, "an expired deadline yields exactly 0", 0, (USize) chrono_deadline_remaining_milliseconds(past, U64_MAX));

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Durations
 *============================================================================*/

static void _test_chrono_duration_add(Test *const test) {
    test_case_begin(test, "duration addition saturates instead of wrapping");

    test_expect_u(test, "1 ms + 2 ms is 3 ms", 3000000, (USize) chrono_duration_add(chrono_milliseconds(1), chrono_milliseconds(2)));
    test_expect_u(test, "adding 0 is identity", 500, (USize) chrono_duration_add(500, 0));
    test_expect_u(test, "overflow saturates", (USize) CHRONO_DURATION_MAX, (USize) chrono_duration_add(CHRONO_DURATION_MAX, 1));
    test_expect_u(test, "both maximal saturates", (USize) CHRONO_DURATION_MAX, (USize) chrono_duration_add(CHRONO_DURATION_MAX, CHRONO_DURATION_MAX));
    // The documented dual use: a NEVER deadline chained forward stays NEVER.
    test_expect_u(test, "chaining a period onto NEVER stays NEVER", (USize) CHRONO_DEADLINE_NEVER, (USize) chrono_duration_add(CHRONO_DEADLINE_NEVER, chrono_seconds(1)));
    // A sum landing exactly ON the ceiling. Pins the boundary, but note it cannot
    // detect early saturation on its own: an implementation that saturated one
    // step too soon would return CHRONO_DURATION_MAX here too.
    test_expect_u(test, "a sum landing exactly on the ceiling is exact", (USize) CHRONO_DURATION_MAX, (USize) chrono_duration_add(CHRONO_DURATION_MAX - 5, 5));
    // This is the one that detects early saturation: the true sum is one below
    // the ceiling, so a saturating implementation returns MAX and fails here.
    test_expect_u(test, "a sum one below the ceiling is not saturated", (USize) (CHRONO_DURATION_MAX - 1), (USize) chrono_duration_add(CHRONO_DURATION_MAX - 6, 5));

    test_case_end(test);
}

static void _test_chrono_duration_difference(Test *const test) {
    test_case_begin(test, "absolute difference is symmetric and cannot wrap");

    ChronoDuration const larger  = chrono_milliseconds(50);
    ChronoDuration const smaller = chrono_milliseconds(20);

    test_expect_u(test, "larger minus smaller", 30000000, (USize) chrono_duration_difference(larger, smaller));
    // The wrap this function exists to prevent: the reversed order must give the
    // same answer, not ~1.8e19.
    test_expect_u(test, "smaller minus larger gives the same magnitude", 30000000, (USize) chrono_duration_difference(smaller, larger));
    test_expect_u(test, "equal values differ by 0", 0, (USize) chrono_duration_difference(larger, larger));
    test_expect_u(test, "difference from 0 is the value", (USize) larger, (USize) chrono_duration_difference(larger, 0));
    test_expect_u(test, "the extreme pair does not wrap", (USize) CHRONO_DURATION_MAX, (USize) chrono_duration_difference(0, CHRONO_DURATION_MAX));

    test_case_end(test);
}

static void _test_chrono_duration_format(Test *const test) {
    test_case_begin(test, "format picks a unit by magnitude and terminates");

    char text[CHRONO_FORMAT_SIZE] = DEFAULT_INITIALIZATION;

    chrono_duration_format(999, text, sizeof(text));
    test_expect_string(test, "sub-microsecond renders exact nanoseconds", "999 ns", text);

    chrono_duration_format(chrono_microseconds(5), text, sizeof(text));
    test_expect_string(test, "5 us renders as microseconds", "5 us", text);

    chrono_duration_format(13400000, text, sizeof(text));
    test_expect_string(test, "13.4 ms renders as milliseconds", "13.4 ms", text);

    chrono_duration_format(chrono_milliseconds(1250), text, sizeof(text));
    test_expect_string(test, "1250 ms renders as seconds", "1.25 s", text);

    chrono_duration_format(0, text, sizeof(text));
    test_expect_string(test, "zero renders as nanoseconds", "0 ns", text);

    USize const written = chrono_duration_format(chrono_milliseconds(1), text, sizeof(text));

    test_expect_u(test, "returns the length excluding the terminator", written, strlen(text));

    // Boundary regression pins. Every one of these rendered as exponential
    // notation before the promote thresholds existed: %.3g switches to "1e+03"
    // as soon as the scaled value rounds to 1000, which is precisely where a
    // unit ladder must promote instead.
    chrono_duration_format(999999, text, sizeof(text));
    test_expect_true(test, "999999 ns never renders as exponential", strchr(text, 'e') == NULL);
    test_expect_string(test, "999999 ns promotes to 1 ms", "1 ms", text);

    chrono_duration_format(999499, text, sizeof(text));
    test_expect_string(test, "999499 ns is the last value that stays in us", "999 us", text);

    chrono_duration_format(999999999, text, sizeof(text));
    test_expect_string(test, "999999999 ns promotes to 1 s", "1 s", text);

    // The minute arm. Without these three the arm is entirely uncovered: every
    // other pin lands below 59.95 s or above 59.95 min, so a mis-set
    // _CHRONO_PROMOTE_TO_MINUTE passes the rest of the suite untouched.
    chrono_duration_format(59900000000ULL, text, sizeof(text));
    test_expect_string(test, "59.9 s is the last value that stays in seconds", "59.9 s", text);

    chrono_duration_format(chrono_seconds(90), text, sizeof(text));
    test_expect_string(test, "90 s promotes to minutes", "1.5 min", text);

    chrono_duration_format(3596000000000ULL, text, sizeof(text));
    test_expect_string(test, "59.93 min is the last value that stays in minutes", "59.9 min", text);

    // The accepted consequence of promoting at 59.95 rather than 59.97: minutes
    // and hours roll at 60, so their arms open with a sliver below 1. Pinned mid
    // sliver, not at the endpoint, where the division can round either way.
    chrono_duration_format(59960000000ULL, text, sizeof(text));
    test_expect_string(test, "the minute arm's opening sliver renders as 0.999 min", "0.999 min", text);

    chrono_duration_format(chrono_seconds(7200), text, sizeof(text));
    test_expect_string(test, "two hours renders as hours, not 7.2e+03 s", "2 h", text);

    // Exact, not merely non-exponential: a fallback wrongly dividing by the
    // minute constant would print "307445734 min" and satisfy a looser check.
    chrono_duration_format(CHRONO_DURATION_MAX, text, sizeof(text));
    test_expect_string(test, "the maximum duration renders as exact integer hours", "5124095 h", text);
    // Also pins that the longest possible output fits CHRONO_FORMAT_SIZE.
    test_expect_true(test, "the maximum duration fits the documented buffer", strlen(text) < CHRONO_FORMAT_SIZE);

    // Too-small buffers must fail closed with an empty string, never a partial
    // number that would read as a different duration.
    char cramped[4] = DEFAULT_INITIALIZATION;

    test_expect_u(test, "a too-small buffer writes nothing", 0, chrono_duration_format(chrono_seconds(1), cramped, sizeof(cramped)));
    test_expect_true(test, "the too-small buffer holds an empty string", cramped[0] == '\0');

    test_case_end(test);
}

static void _test_chrono_duration_constructors(Test *const test) {
    test_case_begin(test, "constructors produce exact nanosecond counts");

    test_expect_u(test, "1 microsecond is 1000 ns", 1000, (USize) chrono_microseconds(1));
    test_expect_u(test, "1 millisecond is 1000000 ns", 1000000, (USize) chrono_milliseconds(1));
    test_expect_u(test, "2 seconds is 2000000000 ns", 2000000000, (USize) chrono_seconds(2));
    test_expect_u(test, "0 of any unit is 0 ns", 0, (USize) chrono_milliseconds(0));

    test_case_end(test);
}

static void _test_chrono_duration_extractors(Test *const test) {
    test_case_begin(test, "extractors round-trip and truncate toward zero");

    ChronoDuration const one_and_a_half = chrono_milliseconds(1500);

    test_expect_u(test, "1500 ms round-trips as milliseconds", 1500, (USize) chrono_duration_milliseconds(one_and_a_half));
    test_expect_u(test, "1500 ms round-trips as microseconds", 1500000, (USize) chrono_duration_microseconds(one_and_a_half));
    test_expect_f(test, "1500 ms is 1.5 s", 1.5, chrono_duration_seconds(one_and_a_half), 0.000001);

    // Truncation, not rounding: 1999999 ns is 1.999999 ms and must read as 1.
    test_expect_u(test, "1999999 ns truncates to 1 ms", 1, (USize) chrono_duration_milliseconds(1999999));
    test_expect_u(test, "999 ns truncates to 0 microseconds", 0, (USize) chrono_duration_microseconds(999));

    test_case_end(test);
}

static void _test_chrono_duration_saturates(Test *const test) {
    test_case_begin(test, "constructor overflow saturates instead of wrapping");

    test_expect_u(test, "seconds overflow saturates", (USize) CHRONO_DURATION_MAX, (USize) chrono_seconds(U64_MAX));
    test_expect_u(test, "milliseconds overflow saturates", (USize) CHRONO_DURATION_MAX, (USize) chrono_milliseconds(U64_MAX));
    test_expect_u(test, "microseconds overflow saturates", (USize) CHRONO_DURATION_MAX, (USize) chrono_microseconds(U64_MAX));

    // The value just below the ceiling must still convert exactly - saturation
    // must not kick in early.
    U64 const largest_exact_seconds = CHRONO_DURATION_MAX / CHRONO_NANOSECONDS_PER_SECOND;

    test_expect_u(test, "the largest exact second count does not saturate",
        (USize) (largest_exact_seconds * CHRONO_NANOSECONDS_PER_SECOND), (USize) chrono_seconds(largest_exact_seconds));

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Stopwatch
 *============================================================================*/

static void _test_chrono_stopwatch_measures(Test *const test) {
    test_case_begin(test, "a running stopwatch accumulates time");

    ChronoStopwatch watch = chrono_stopwatch_start();

    thread_sleep(TEST_SLEEP_MILLISECONDS);

    ChronoDuration const measured = chrono_stopwatch_elapsed(&watch);

    test_expect_true(test, "starts running", chrono_stopwatch_running(&watch));
    test_expect_true(test, "measured at least the sleep, minus tolerance",
        chrono_duration_milliseconds(measured) >= TEST_SLEEP_TOLERANCE_MILLISECONDS);

    test_case_end(test);
}

static void _test_chrono_stopwatch_pause_freezes(Test *const test) {
    test_case_begin(test, "a paused stopwatch reports a bit-for-bit constant value");

    ChronoStopwatch watch = chrono_stopwatch_start();

    thread_sleep(TEST_SLEEP_MILLISECONDS);
    chrono_stopwatch_pause(&watch);

    ChronoDuration const first = chrono_stopwatch_elapsed(&watch);

    thread_sleep(TEST_PAUSE_MILLISECONDS);

    ChronoDuration const second = chrono_stopwatch_elapsed(&watch);

    thread_sleep(TEST_PAUSE_MILLISECONDS);

    ChronoDuration const third = chrono_stopwatch_elapsed(&watch);

    test_expect_false(test, "watch reports not running", chrono_stopwatch_running(&watch));
    // Exact equality, not a tolerance: elapsed reads the clock only while
    // running, so a paused watch must return the identical value forever.
    test_expect_true(test, "second read equals the first exactly", second == first);
    test_expect_true(test, "third read equals the first exactly", third == first);

    test_case_end(test);
}

static void _test_chrono_stopwatch_excludes_pause(Test *const test) {
    test_case_begin(test, "the paused interval is excluded from the total");

    // Measured against a raw instant spanning the same region, so the assertion
    // needs no absolute upper bound: the watch's intervals are a strict subset
    // of the wall span, short by exactly the paused stretch.
    ChronoInstant const wall_start = chrono_now();
    ChronoStopwatch watch          = chrono_stopwatch_start();

    thread_sleep(TEST_SLEEP_MILLISECONDS);
    chrono_stopwatch_pause(&watch);
    thread_sleep(TEST_PAUSE_MILLISECONDS);
    chrono_stopwatch_resume(&watch);
    thread_sleep(TEST_SLEEP_MILLISECONDS);

    ChronoDuration const measured = chrono_stopwatch_elapsed(&watch);
    ChronoDuration const wall     = chrono_elapsed(wall_start);

    test_expect_true(test, "measured is strictly less than the wall span", measured < wall);
    test_expect_true(test, "the gap covers the paused stretch, minus tolerance",
        chrono_duration_milliseconds(wall - measured) >= TEST_PAUSE_MILLISECONDS - TEST_SLEEP_TOLERANCE_MILLISECONDS);

    test_case_end(test);
}

static void _test_chrono_stopwatch_pause_resume_idempotent(Test *const test) {
    test_case_begin(test, "repeated pause and resume calls are no-ops");

    ChronoStopwatch watch = chrono_stopwatch_start();

    thread_sleep(TEST_SLEEP_MILLISECONDS);
    chrono_stopwatch_pause(&watch);

    ChronoDuration const after_pause = chrono_stopwatch_elapsed(&watch);

    // A second pause must not bank the segment twice.
    chrono_stopwatch_pause(&watch);

    test_expect_true(test, "double pause does not change the total", chrono_stopwatch_elapsed(&watch) == after_pause);

    chrono_stopwatch_resume(&watch);

    // White-box on purpose: started_at has no accessor and none is wanted, but
    // pinning it is the only way to prove a second resume does not silently
    // restart the current segment and lose the time already in it.
    ChronoInstant const resumed_at = watch.started_at;

    // A second resume must not restart the current segment and lose time.
    chrono_stopwatch_resume(&watch);

    test_expect_true(test, "watch is running after resume", chrono_stopwatch_running(&watch));
    test_expect_true(test, "double resume does not reset the segment start", watch.started_at == resumed_at);

    test_case_end(test);
}

static void _test_chrono_stopwatch_lap(Test *const test) {
    test_case_begin(test, "laps split the total against the previous lap");

    ChronoStopwatch watch = chrono_stopwatch_start();

    thread_sleep(TEST_SLEEP_MILLISECONDS);
    chrono_stopwatch_pause(&watch);

    // Pausing first makes the lap arithmetic exact: with the clock frozen, the
    // expected values are determined rather than timing-dependent.
    ChronoDuration const total       = chrono_stopwatch_elapsed(&watch);
    ChronoDuration const first_lap   = chrono_stopwatch_lap(&watch);
    ChronoDuration const second_lap  = chrono_stopwatch_lap(&watch);

    test_expect_true(test, "the first lap equals the whole total so far", first_lap == total);
    test_expect_u(test, "a lap with no running time in between is exactly 0", 0, (USize) second_lap);
    test_expect_true(test, "lapping does not stop the total", chrono_stopwatch_elapsed(&watch) == total);

    chrono_stopwatch_resume(&watch);
    thread_sleep(TEST_SLEEP_MILLISECONDS);

    ChronoDuration const third_lap = chrono_stopwatch_lap(&watch);

    test_expect_true(test, "a lap after resuming covers the new segment",
        chrono_duration_milliseconds(third_lap) >= TEST_SLEEP_TOLERANCE_MILLISECONDS);
    // The lap baseline is accumulated running time, so the paused stretch that
    // preceded this segment must not appear in it.
    test_expect_true(test, "the lap excludes time banked before it", third_lap < chrono_stopwatch_elapsed(&watch));

    test_case_end(test);
}

static void _test_chrono_stopwatch_saturates(Test *const test) {
    test_case_begin(test, "a hand-built watch saturates instead of wrapping");

    // White-box on purpose, and deterministic where a real watch could never be:
    // reaching this state through the public API needs ~584 years of running
    // time, but the members are public, so the saturation the header promises has
    // to hold here too. With a bare + the accumulated total wraps to roughly the
    // current uptime; chrono_duration_add clamps at the ceiling.
    ChronoStopwatch watch = {
        .started_at     = 0,
        .accumulated    = CHRONO_DURATION_MAX,
        .last_lap_total = 0,
        .running        = true,
    };

    test_expect_u(test, "elapsed clamps at the ceiling rather than wrapping", (USize) CHRONO_DURATION_MAX, (USize) chrono_stopwatch_elapsed(&watch));

    // Pause banks the same sum, so it needs the same guarantee.
    chrono_stopwatch_pause(&watch);

    test_expect_u(test, "pause banks the clamped total, not a wrapped one", (USize) CHRONO_DURATION_MAX, (USize) chrono_stopwatch_elapsed(&watch));

    test_case_end(test);
}

static void _test_chrono_stopwatch_zero_initialized(Test *const test) {
    test_case_begin(test, "a zero-initialized watch is a valid paused watch at zero");

    // Documented contract, not an accident of layout: a watch embedded in a
    // struct must be usable without calling chrono_stopwatch_start first.
    ChronoStopwatch watch = DEFAULT_INITIALIZATION;

    test_expect_false(test, "starts paused", chrono_stopwatch_running(&watch));
    test_expect_u(test, "reports exactly 0 elapsed", 0, (USize) chrono_stopwatch_elapsed(&watch));

    // Pause on an already-paused zero watch must stay a no-op.
    chrono_stopwatch_pause(&watch);

    test_expect_u(test, "pausing it again changes nothing", 0, (USize) chrono_stopwatch_elapsed(&watch));

    chrono_stopwatch_resume(&watch);
    thread_sleep(TEST_SLEEP_MILLISECONDS);

    test_expect_true(test, "resume starts it counting", chrono_stopwatch_running(&watch));
    test_expect_true(test, "it accumulates from zero after resume",
        chrono_duration_milliseconds(chrono_stopwatch_elapsed(&watch)) >= TEST_SLEEP_TOLERANCE_MILLISECONDS);

    test_case_end(test);
}

static void _test_chrono_stopwatch_restart(Test *const test) {
    test_case_begin(test, "restart reports the total and resumes from zero");

    ChronoStopwatch watch = chrono_stopwatch_start();

    thread_sleep(TEST_SLEEP_MILLISECONDS);
    chrono_stopwatch_pause(&watch);

    ChronoDuration const before   = chrono_stopwatch_elapsed(&watch);
    ChronoDuration const reported = chrono_stopwatch_restart(&watch);

    test_expect_true(test, "restart returns the total measured before it", reported == before);
    test_expect_true(test, "a paused watch comes back running", chrono_stopwatch_running(&watch));
    // White-box on purpose: these two have no accessors, and reading them is how
    // the test proves restart clears the baseline rather than merely appearing to.
    test_expect_u(test, "the lap baseline is cleared", 0, (USize) watch.last_lap_total);
    test_expect_u(test, "banked time is cleared", 0, (USize) watch.accumulated);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Runner
 *============================================================================*/

int main(void) {
    log_init((LogConfig){ .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true });

    Test test = test_init("tests/chrono/test_all.c");

    test_suite_begin(&test, "chrono");

    test_section_begin(&test, "clock");
    _test_chrono_now_advances(&test);
    _test_chrono_now_never_decreases(&test);
    _test_chrono_elapsed(&test);
    _test_chrono_resolution(&test);
    test_section_end(&test);

    test_section_begin(&test, "durations");
    _test_chrono_duration_constructors(&test);
    _test_chrono_duration_extractors(&test);
    _test_chrono_duration_saturates(&test);
    _test_chrono_duration_add(&test);
    _test_chrono_duration_difference(&test);
    _test_chrono_duration_format(&test);
    test_section_end(&test);

    test_section_begin(&test, "stopwatch");
    _test_chrono_stopwatch_measures(&test);
    _test_chrono_stopwatch_pause_freezes(&test);
    _test_chrono_stopwatch_excludes_pause(&test);
    _test_chrono_stopwatch_pause_resume_idempotent(&test);
    _test_chrono_stopwatch_lap(&test);
    _test_chrono_stopwatch_restart(&test);
    _test_chrono_stopwatch_zero_initialized(&test);
    _test_chrono_stopwatch_saturates(&test);
    test_section_end(&test);

    test_section_begin(&test, "deadlines");
    _test_chrono_deadline_future(&test);
    _test_chrono_deadline_expired(&test);
    _test_chrono_deadline_never(&test);
    _test_chrono_deadline_saturates(&test);
    _test_chrono_deadline_remaining_milliseconds(&test);
    test_section_end(&test);

    test_suite_end(&test);

    return test_uninit(&test);
}