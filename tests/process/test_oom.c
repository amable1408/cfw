/*
 * test_oom.c - Allocation-failure (fault-injection) tests for include/process/process.c.
 *
 * Every recoverable-OOM promise process.h makes is pinned here by making the allocation behind
 * it actually fail, because reading cannot verify one and an ordinary run never reaches the
 * branch. The seam is the linker's: this binary is linked with -Wl,--wrap=calloc
 * -Wl,--wrap=malloc -Wl,--wrap=free, so every calloc memory_try_alloc makes (and the one malloc
 * thread_create makes for the Windows stdin writer) passes through the ledger below, which can
 * refuse it on demand and later say exactly which blocks are still live.
 *
 * The contracts under test, one case each:
 *   - The capture buffer grows with memory_try_alloc, so a failed growth fails the call with
 *     PROCESS_RESULT_NO_MEMORY - carrying RESULT_FLAG_PARTIAL exactly when a prefix is held, kept
 *     distinct from PROCESS_RESULT_OUTPUT_TOO_LARGE - kills the child at once rather than waiting
 *     out its deadline, and leaves an outcome that holds the prefix and must still be released.
 *     Swept over EVERY growth step of a measured run, not only the first.
 *   - The owned "" a silent child gets: should that one byte fail, the Result is
 *     PROCESS_RESULT_NO_MEMORY while ended and exit_code still describe the completed run.
 *   - Windows only: the command line and the inheritable-handle attribute list are both
 *     memory_try_alloc'd before the spawn; either failing refuses the run with
 *     PROCESS_RESULT_NO_MEMORY, spawns nothing (ended UNKNOWN), and releases what was taken.
 *   - Windows only: the stdin writer thread that could not start (its ThreadArgs malloc failing)
 *     fails the run closed - the child is killed and collected (PROCESS_ENDED_KILLED), never left
 *     running against a stdin that will never be fed.
 *
 * Every count is taken over a LEDGER WINDOW - the records one process_run call made, from the
 * ledger index before it to the index right after it - never over the whole ledger. The binary
 * has other allocators of the same sizes: the CRT's startup code is linked into the image, so
 * its pre-main mallocs pass through the wrapper and stay live for the life of the process, and
 * the test framework allocates between runs. A whole-ledger count measured those, not process.
 *
 * The suite spawns ITSELF as the child through the --child-* arguments, as test_all.c does, so
 * it needs no external tool on either platform. The children are deliberately ones whose fate
 * is deterministic under a failed capture: a flooder never exits on its own, so a run whose
 * capture fails must end KILLED, never EXITED by a race with the child's own exit.
 *
 * No case pins an ABORT contract: process.h documents none for allocation failure. Its only
 * aborts are error_check_* argument checks (a null argv, argv[0] or out, a stderr_mode outside
 * the enum), which are programming errors with nothing to do with memory.
 */

/* Built WITHOUT ERROR_CHECK_ENABLED, like every OOM harness in the tree, and refused outright
 * otherwise rather than trusting a reader to notice: that define makes memory_alloc abort instead
 * of returning nullptr and arms every error_check_* in the linked modules, so a failure injected
 * here could end in a linked module's abort rather than in process.c's own recovery.
 *
 * The price, measured by mutation: process.c allocates only through memory_try_alloc, which
 * recovers in BOTH builds, so a growth that regressed to the aborting memory_alloc is invisible
 * here - in this build memory_alloc returns nullptr too, and the module's null check handles it
 * identically. Only a build with the production define sees that regression: with this guard
 * removed and ERROR_CHECK_ENABLED defined, the unmutated module still passes every case here and
 * the memory_alloc mutant aborts (0xC0000409) at its first injected growth. */
#ifdef ERROR_CHECK_ENABLED
#error "test_oom.c must be built WITHOUT ERROR_CHECK_ENABLED - see the comment above this guard."
#endif // ERROR_CHECK_ENABLED

#include <stdatomic.h>

#include <log/log.h>
#include <process/process.h>
#include <test/test.h>

#ifdef OS_WINDOWS
#include <fcntl.h>
#include <io.h>
#endif // OS_WINDOWS

/*==============================================================================
 * MARK: - Macros and Constants
 *============================================================================*/

/** Exact size of every Windows command line this suite builds (padded to it in _test_run), chosen
 *  to match no other allocation a run makes: the attribute list, the "", the growth steps. */
#define _TEST_COMMAND_LINE_BYTES 1000

/** Bytes fed to the echo child; short, so the whole of it is one write and one read. */
#define _TEST_ECHO_INPUT "oom echo"

/** Output limit for every flooding run. Large enough that a measured run grows the capture six
 *  times (4097 .. 131104), small enough that it is reached in milliseconds. */
#define _TEST_FLOOD_LIMIT 100000

/** Upper bound on the growth steps a profile may record; the measured run makes six. */
#define _TEST_GROWTH_MAX 16

/** Longest a run whose capture failed may take. The deadline is _TEST_TIMEOUT_MS, so a module
 *  that waited the child out instead of killing it at once overshoots this by a wide margin,
 *  while a kill plus the module's own bounded reap (5 s) stays well inside it. */
#define _TEST_KILL_BOUND_MS 30000

/** Allocation records the ledger can hold; the whole binary makes a few hundred. */
#define _TEST_LEDGER_CAPACITY 4096

/** Bytes per write in the flooding child: a multiple of 26, so every block continues the
 *  alphabet exactly where the previous one stopped and the capture is one unbroken pattern. */
#define _TEST_PATTERN_BLOCK (26 * 158)

/** Mirrors process.c's _PROCESS_READ_CHUNK: the first growth is this plus its terminator. */
#define _TEST_READ_CHUNK 4096

/** Deadline handed to every run; far past _TEST_KILL_BOUND_MS on purpose. */
#define _TEST_TIMEOUT_MS 60000

/*==============================================================================
 * MARK: - Allocation Ledger
 *============================================================================*/

/* Every calloc and malloc is recorded and every free is matched against the record, so the
 * assertions below can ask "which blocks are still live" rather than only "did it crash".
 *
 * The ledger is LOCKED. On Windows the stdin writer is a real thread, and thread.c's callback
 * wrapper frees its ThreadArgs from that thread - through the wrapped free - while the calling
 * thread may be growing the capture through the wrapped calloc. A spinlock, not a ThreadMutex:
 * the lock guards the allocator itself, so it must not be able to allocate. */

/** Which wrapped allocator a block came from. */
typedef enum {
    LEDGER_SOURCE_CALLOC = 0,
    LEDGER_SOURCE_MALLOC
} LedgerSource;

/** One recorded allocation. */
typedef struct LedgerEntry {
    void *pointer;          /**< Block address. */
    USize size;             /**< Bytes requested. */
    LedgerSource source;    /**< Allocator that produced it. */
    bool freed;             /**< true once released; the entry is kept so a second free is visible. */
} LedgerEntry;

/** The records one run made: ledger indices [start, end). */
typedef struct LedgerWindow {
    USize start;    /**< Ledger count before the run. */
    USize end;      /**< Ledger count right after it, before any assertion could allocate. */
} LedgerWindow;

static LedgerEntry _ledger[_TEST_LEDGER_CAPACITY];
static USize _ledger_count = 0;
static atomic_flag _ledger_lock = ATOMIC_FLAG_INIT;
static USize _ledger_overflow = 0;
static USize _double_free_count = 0;
static USize _foreign_free_count = 0;

/* Injection control: fail every allocation from _fail_source of exactly _fail_size bytes, from
 * the _fail_ordinal-th onwards. SIZED rather than counted, because the linked modules allocate
 * for their own reasons; each size armed here is MEASURED first (the profile case) and checked
 * to be the only one of its kind in the run it is armed for. */
static USize _fail_ordinal = 0;
static USize _fail_seen = 0;
static USize _fail_size = 0;
static LedgerSource _fail_source = LEDGER_SOURCE_CALLOC;

void* __real_calloc(USize const count, USize const size);
void* __real_malloc(USize const size);
void __real_free(void *const pointer);

/** @brief Take the ledger lock; spins, because the lock must never allocate. */
static void _ledger_acquire(void) {
    while (atomic_flag_test_and_set_explicit(&_ledger_lock, memory_order_acquire)) {
        /* Contention lasts one record write at most. */
    }
}

/** @brief Release the ledger lock. */
static void _ledger_release(void) {
    atomic_flag_clear_explicit(&_ledger_lock, memory_order_release);
}

/**
 * @brief The shared body of both allocation wrappers: inject, allocate, record.
 *
 * @param source Which allocator is being wrapped.
 * @param count  Element count (1 for malloc).
 * @param size   Element size (the whole request for malloc).
 * @return The block, or nullptr when injected or genuinely out of memory.
 */
static void* _ledger_allocate(LedgerSource const source, USize const count, USize const size) {
    // Guarded because count * size can wrap, and a wrapped product could false-match
    // _fail_size and inject a failure into an unrelated allocation. The real allocation
    // passes count and size through untouched, so a wrap can only mislead this wrapper.
    USize const bytes = (size != 0 && count > USIZE_MAX / size) ? USIZE_MAX : count * size;

    _ledger_acquire();

    if (_fail_size != 0 && _fail_source == source && bytes == _fail_size) {
        _fail_seen += 1;

        if (_fail_seen >= _fail_ordinal) {
            _ledger_release();

            return nullptr;
        }
    }

    void *const pointer = (source == LEDGER_SOURCE_MALLOC) ? __real_malloc(size) : __real_calloc(count, size);

    if (memory_empty(pointer)) {
        _ledger_release();

        return pointer;
    }

    if (_ledger_count >= _TEST_LEDGER_CAPACITY) {
        // Untracked from here on, which would make every live count lie. Counted so the
        // last case can fail on it rather than silently under-report.
        _ledger_overflow += 1;

        _ledger_release();

        return pointer;
    }

    _ledger[_ledger_count].pointer = pointer;
    _ledger[_ledger_count].size = bytes;
    _ledger[_ledger_count].source = source;
    _ledger[_ledger_count].freed = false;
    _ledger_count += 1;

    _ledger_release();

    return pointer;
}

void* __wrap_calloc(USize const count, USize const size) {
    return _ledger_allocate(LEDGER_SOURCE_CALLOC, count, size);
}

void* __wrap_malloc(USize const size) {
    return _ledger_allocate(LEDGER_SOURCE_MALLOC, 1, size);
}

/**
 * @brief Index of the NEWEST record for an address, or _ledger_count for none.
 *
 * Backwards, because the allocator recycles addresses: a forward scan matches the stale record
 * of a block freed earlier, so the legal free of a new block at that address would count as a
 * double free and the new block would read live forever. Side-effect free so the scan direction
 * can be pinned against fabricated records. Called with the lock held, or single-threaded.
 */
static USize _ledger_find(void const *const pointer) {
    for (USize i = _ledger_count; i > 0; i -= 1) {
        if (_ledger[i - 1].pointer == pointer) {
            return i - 1;
        }
    }

    return _ledger_count;
}

void __wrap_free(void *const pointer) {
    if (memory_empty(pointer)) {
        __real_free(pointer);

        return;
    }

    _ledger_acquire();

    USize const found = _ledger_find(pointer);

    if (found != _ledger_count) {
        if (_ledger[found].freed) {
            _double_free_count += 1;
        }

        _ledger[found].freed = true;
    }
    else {
        // A free of a block this ledger never recorded. Everything linked here allocates
        // through calloc (memory_try_alloc) or malloc (thread_create, the CRT startup code),
        // and both are wrapped; nothing reaches realloc. Asserted zero in the last case, so
        // a new route shows up as a failure rather than as noise.
        _foreign_free_count += 1;
    }

    _ledger_release();

    __real_free(pointer);
}

/** @brief The current ledger index - one end of a window. */
static USize _ledger_mark(void) {
    _ledger_acquire();

    USize const mark = _ledger_count;

    _ledger_release();

    return mark;
}

/** @brief Records in the window of exactly this size from this allocator, released or not. */
static USize _window_records(LedgerWindow const window, LedgerSource const source, USize const bytes) {
    USize records = 0;

    _ledger_acquire();

    for (USize i = window.start; i < window.end && i < _ledger_count; i += 1) {
        if (_ledger[i].source == source && _ledger[i].size == bytes) {
            records += 1;
        }
    }

    _ledger_release();

    return records;
}

/** @brief Records in the window of exactly this size from this allocator, not yet released. */
static USize _window_live(LedgerWindow const window, LedgerSource const source, USize const bytes) {
    USize live = 0;

    _ledger_acquire();

    for (USize i = window.start; i < window.end && i < _ledger_count; i += 1) {
        if (!_ledger[i].freed && _ledger[i].source == source && _ledger[i].size == bytes) {
            live += 1;
        }
    }

    _ledger_release();

    return live;
}

/** @brief Records in the window not yet released, whatever their size or source. */
static USize _window_live_total(LedgerWindow const window) {
    USize live = 0;

    _ledger_acquire();

    for (USize i = window.start; i < window.end && i < _ledger_count; i += 1) {
        if (!_ledger[i].freed) {
            live += 1;
        }
    }

    _ledger_release();

    return live;
}

/**
 * @brief Arm the injector: fail the ordinal-th allocation of exactly this size, and every later one.
 *
 * The seen-counter is reset HERE, never at the call site: leaving it to the caller is how an
 * earlier harness silently stopped injecting in its second case.
 *
 * @param source  Allocator to fail.
 * @param size    Exact byte count to match.
 * @param ordinal Which matching allocation fails first, counting from one.
 */
static void _test_fail_arm(LedgerSource const source, USize const size, USize const ordinal) {
    _ledger_acquire();

    _fail_source = source;
    _fail_size = size;
    _fail_ordinal = ordinal;
    _fail_seen = 0;

    _ledger_release();
}

/**
 * @brief Disarm the injector and report how many matching allocations it saw while armed.
 *
 * Returning the count is what makes "capture fired before disarm" impossible to get wrong: an
 * earlier harness read the counter AFTER disarming had zeroed it, and every armed case reported
 * an injection that never fired while its degradation assertions all passed.
 *
 * @return Matching allocations seen since the arm.
 */
static USize _test_fail_disarm(void) {
    _ledger_acquire();

    USize const seen = _fail_seen;

    _fail_size = 0;
    _fail_ordinal = 0;
    _fail_seen = 0;

    _ledger_release();

    return seen;
}

/*==============================================================================
 * MARK: - Children
 *============================================================================*/

/**
 * @brief Write the alphabet pattern forever; never exits on its own.
 *
 * Its fate under a failed capture is therefore deterministic: the run must kill it.
 *
 * @return Exit code for the child, which a passing test never observes.
 */
static I32 _child_flood(void) {
#ifdef OS_WINDOWS
    _setmode(_fileno(stdout), _O_BINARY);
#endif // OS_WINDOWS

    char block[_TEST_PATTERN_BLOCK] = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < sizeof(block); i += 1) {
        block[i] = (char) ('a' + (i % 26));
    }

    while (fwrite(block, 1, sizeof(block), stdout) == sizeof(block)) {
        fflush(stdout);
    }

    return 0;
}

/**
 * @brief Copy stdin to stdout verbatim; blocks until stdin reaches end of file.
 *
 * @return Exit code for the child.
 */
static I32 _child_echo(void) {
#ifdef OS_WINDOWS
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif // OS_WINDOWS

    char buffer[_TEST_READ_CHUNK] = DEFAULT_INITIALIZATION;
    USize got = fread(buffer, 1, sizeof(buffer), stdin);

    while (got > 0) {
        fwrite(buffer, 1, got, stdout);

        got = fread(buffer, 1, sizeof(buffer), stdin);
    }

    fflush(stdout);

    return 0;
}

/*==============================================================================
 * MARK: - Fixtures
 *============================================================================*/

/** Path this binary was invoked with, reused as the program every spawn runs. */
static char const *_program = nullptr;

/** The padding argument _test_run appends; see there. */
static char _padding[_TEST_COMMAND_LINE_BYTES];

/** Growth sizes of a measured flooding run, in order. */
static USize _growth_sizes[_TEST_GROWTH_MAX];

/** Number of entries in _growth_sizes. */
static USize _growth_count = 0;

/** Ordinal of the owned-"" byte among a silent run's 1-byte callocs. */
static USize _empty_ordinal = 0;

/** Allocations a silent run makes (the owned "" among them). */
static USize _silent_allocations = 0;

#ifdef OS_WINDOWS
/** Measured size of the Windows inheritable-handle attribute list. */
static USize _attribute_list_size = 0;
#endif // OS_WINDOWS

/** Measured size of thread_create's ThreadArgs malloc (0 where no writer thread exists). */
static USize _writer_args_size = 0;

/**
 * @brief Milliseconds from a monotonic source, for asserting that a run did not wait out its deadline.
 *
 * @return Current value of the monotonic millisecond counter.
 */
static U64 _test_now_milliseconds(void) {
#ifdef OS_WINDOWS
    return (U64) GetTickCount64();
#else
    struct timespec now = DEFAULT_INITIALIZATION;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }

    return ((U64) now.tv_sec * 1000U) + ((U64) now.tv_nsec / 1000000U);
#endif // OS_WINDOWS
}

/**
 * @brief Run this binary as a child in the given mode, recording the ledger window of the call.
 *
 * The window closes the moment process_run returns, before any assertion can allocate.
 *
 * @param mode    The --child-* argument.
 * @param input   Bytes for its stdin, or nullptr for an immediate end of file.
 * @param limit   Output limit; 0 selects the module's default.
 * @param outcome Receives the outcome.
 * @param window  Receives the ledger window of the call.
 * @return process_run's Result.
 */
static Result _test_run(char const *const mode, char const *const input, USize const limit, ProcessOutcome *const outcome, LedgerWindow *const window) {
    // The padding argument brings the Windows command line to exactly _TEST_COMMAND_LINE_BYTES
    // (argv[0], mode and padding, two separating spaces, the terminator), whatever path this
    // binary was started from. Unpadded, its size tracks the caller's working directory - and
    // a 34-character path once made it exactly the attribute list's 48 bytes, so one armed size
    // failed two different allocations. The children read only argv[1].
    USize const natural = strlen(_program) + 1 + strlen(mode) + 1 + 1;
    USize const padding = (natural < _TEST_COMMAND_LINE_BYTES) ? _TEST_COMMAND_LINE_BYTES - natural : 1;

    memory_set(_padding, padding, (U8) 'p');

    _padding[padding] = '\0';

    char const *const argv[] = { _program, mode, _padding, nullptr };
    ProcessSpec const spec = {
        .argv = argv,
        .input = input,
        .input_size = (input == nullptr) ? 0 : strlen(input),
        .output_limit = limit,
        .timeout_milliseconds = _TEST_TIMEOUT_MS,
    };

    window->start = _ledger_mark();

    Result const result = process_run(spec, outcome);

    window->end = _ledger_mark();

    return result;
}

/**
 * @brief Report whether bytes are an unbroken prefix of the flooding child's alphabet pattern.
 *
 * @param data Captured bytes.
 * @param size Number of bytes.
 * @return true when every byte is where the pattern puts it.
 */
static bool _test_pattern_matches(char const *const data, USize const size) {
    for (USize i = 0; i < size; i += 1) {
        if (data[i] != (char) ('a' + (i % 26))) {
            return false;
        }
    }

    return true;
}

/**
 * @brief Report whether an outcome died at this module's hand for a failed capture.
 *
 * On POSIX the signal is SIGKILL or SIGPIPE, both measured. The module closes the read end
 * BEFORE it signals (so a child blocked on a full pipe cannot deadlock the wait), and a flooder
 * writing at that instant dies of SIGPIPE before the SIGKILL lands; kill() on the dying child
 * still succeeds, so the run reports KILLED carrying the signal that actually ended it. Any
 * other signal, or SIGNALED / EXITED, would mean the child died of something this run did not
 * do. Windows has no signals to name, and terminates before it closes.
 *
 * @param outcome Outcome to inspect.
 * @return true when the child was killed by the module.
 */
static bool _test_killed(ProcessOutcome const *const outcome) {
#ifdef OS_WINDOWS
    return outcome->ended == PROCESS_ENDED_KILLED;
#else
    return outcome->ended == PROCESS_ENDED_KILLED && (outcome->signal == SIGKILL || outcome->signal == SIGPIPE);
#endif // OS_WINDOWS
}

/*==============================================================================
 * MARK: - Cases
 *============================================================================*/

static void _test_ledger_find_returns_the_newest_record(Test *const test) {
    test_case_begin(test, "the ledger scan returns the newest record for a recycled address");

    // Fabricated records, not real allocations: two records at ONE address is the only way to
    // express recycling deterministically. The writes bypass the wrapper's capacity check, so
    // they carry their own - _ledger_count can legitimately BE the capacity.
    USize const saved = _ledger_count;
    char first = 0;
    char other = 0;

    test_expect_true(test, "the ledger has room for the fabricated records", _ledger_count + 2 <= _TEST_LEDGER_CAPACITY);

    if (_ledger_count + 2 > _TEST_LEDGER_CAPACITY) {
        test_case_end(test);

        return;
    }

    _ledger[_ledger_count] = (LedgerEntry) { .pointer = &first, .size = 1, .source = LEDGER_SOURCE_CALLOC, .freed = true };
    _ledger_count += 1;
    _ledger[_ledger_count] = (LedgerEntry) { .pointer = &first, .size = 1, .source = LEDGER_SOURCE_CALLOC, .freed = false };
    _ledger_count += 1;

    USize const live = _ledger_find(&first);

    test_expect_u(test, "the newest record wins", _ledger_count - 1, live);
    test_expect_false(test, "the newest record is the live one", _ledger[live].freed);

    _ledger[_ledger_count - 1].freed = true;
    _ledger[_ledger_count - 2].freed = false;

    USize const stale = _ledger_find(&first);

    test_expect_u(test, "the newest record still wins when it is the freed one", _ledger_count - 1, stale);
    test_expect_true(test, "a genuine double free is still visible", _ledger[stale].freed);
    test_expect_u(test, "an address with no record reports a miss", _ledger_count, _ledger_find(&other));

    _ledger_count = saved;

    test_case_end(test);
}

static void _test_profile(Test *const test) {
    test_case_begin(test, "allocation profile of one run, counting on and failure off");

    // MEASURED, never guessed: every size and ordinal the cases below arm comes from here, and
    // each is checked against what the module's source says it allocates, so a module change
    // that moves an allocation fails HERE, by name, instead of turning a later case vacuous.

    // 1. A silent child: Windows takes the command line and the attribute list, then the
    //    owned "" after the run; POSIX takes only the "".
    ProcessOutcome silent = DEFAULT_INITIALIZATION;
    LedgerWindow silent_window = DEFAULT_INITIALIZATION;
    Result const silent_result = _test_run("--child-silent", nullptr, 0, &silent, &silent_window);

    _silent_allocations = silent_window.end - silent_window.start;
    _empty_ordinal = _window_records(silent_window, LEDGER_SOURCE_CALLOC, 1);

    test_expect_true(test, "the silent run succeeded", result_is_success(silent_result));
    test_expect_true(test, "the silent child exited 0", process_outcome_succeeded(&silent));
    test_expect_true(test, "the silent run owns a non-null \"\"", !memory_empty(silent.output) && silent.output_size == 0);
    test_expect_u(test, "exactly one 1-byte allocation in a silent run (the owned \"\")", 1, _empty_ordinal);

#ifdef OS_WINDOWS
    test_expect_u(test, "a silent Windows run allocates three blocks", 3, _silent_allocations);

    if (_silent_allocations >= 2) {
        _attribute_list_size = _ledger[silent_window.start + 1].size;
    }

    // The command line is argv joined by spaces plus a terminator, padded to an exact size.
    // argv[0] needs quoting only for a space, tab or quote, which would add bytes; that is
    // checked rather than assumed, so the anchor cannot silently weaken.
    test_expect_true(test, "this binary's own path needs no quoting", strpbrk(_program, " \t\"") == nullptr);
    test_expect_u(test, "the first block is the padded command line", _TEST_COMMAND_LINE_BYTES, _ledger[silent_window.start].size);
    test_expect_true(test, "the attribute list size is distinct from every other armed size",
        _attribute_list_size > 1 && _attribute_list_size != _TEST_COMMAND_LINE_BYTES && _attribute_list_size < _TEST_READ_CHUNK + 1);
#else
    test_expect_u(test, "a silent POSIX run allocates only the owned \"\"", 1, _silent_allocations);
#endif // OS_WINDOWS

    process_outcome_uninit(&silent);

    test_expect_u(test, "uninit released everything the silent run took", 0, _window_live_total(silent_window));

    // 2. A flooding child against _TEST_FLOOD_LIMIT: the growth sequence, compared with the
    //    geometric one process.c documents (first chunk plus terminator, doubling).
    ProcessOutcome flood = DEFAULT_INITIALIZATION;
    LedgerWindow flood_window = DEFAULT_INITIALIZATION;
    Result const flood_result = _test_run("--child-flood", nullptr, _TEST_FLOOD_LIMIT, &flood, &flood_window);

    test_expect_true(test, "the flood was refused as too large, with a prefix", flood_result == result_with_flag(PROCESS_RESULT_OUTPUT_TOO_LARGE, RESULT_FLAG_PARTIAL));
    test_expect_u(test, "the flood kept exactly the limit", _TEST_FLOOD_LIMIT, flood.output_size);
    test_expect_true(test, "the flood's capture is the pattern, unbroken", !memory_empty(flood.output) && _test_pattern_matches(flood.output, flood.output_size));

#ifdef OS_WINDOWS
    USize const growth_start = flood_window.start + 2;
#else
    USize const growth_start = flood_window.start;
#endif // OS_WINDOWS

    _growth_count = 0;

    for (USize i = growth_start; i < flood_window.end && _growth_count < _TEST_GROWTH_MAX; i += 1) {
        _growth_sizes[_growth_count] = _ledger[i].size;
        _growth_count += 1;
    }

    USize expected_count = 0;
    bool sequence_matches = true;

    for (USize capacity = _TEST_READ_CHUNK + 1; expected_count < _TEST_GROWTH_MAX; capacity *= 2) {
        if (expected_count >= _growth_count || _growth_sizes[expected_count] != capacity) {
            sequence_matches = false;
        }

        expected_count += 1;

        if (capacity >= (USize) _TEST_FLOOD_LIMIT + 1) {
            break;
        }
    }

    test_expect_u(test, "the flood grew the capture exactly six times", 6, _growth_count);
    test_expect_u(test, "the measured growth count matches the geometric one", expected_count, _growth_count);
    test_expect_true(test, "every growth step is the documented size, in order", sequence_matches);
    test_expect_u(test, "only the last growth block is still live (the outcome's)", 1, _window_live_total(flood_window));

    process_outcome_uninit(&flood);

    test_expect_u(test, "uninit released the last growth block", 0, _window_live_total(flood_window));

    // 3. The echo child with input: on Windows that starts the writer thread, whose ThreadArgs
    //    is the run's one malloc. POSIX feeds stdin from the polling loop and mallocs nothing.
    ProcessOutcome echo = DEFAULT_INITIALIZATION;
    LedgerWindow echo_window = DEFAULT_INITIALIZATION;
    Result const echo_result = _test_run("--child-echo", _TEST_ECHO_INPUT, 0, &echo, &echo_window);

    test_expect_true(test, "the echo run succeeded", result_is_success(echo_result) && process_outcome_succeeded(&echo));
    test_expect_true(test, "the echo child returned its input", !memory_empty(echo.output) && strcmp(echo.output, _TEST_ECHO_INPUT) == 0);

    USize mallocs = 0;

    for (USize i = echo_window.start; i < echo_window.end; i += 1) {
        if (_ledger[i].source == LEDGER_SOURCE_MALLOC) {
            mallocs += 1;
            _writer_args_size = _ledger[i].size;
        }
    }

#ifdef OS_WINDOWS
    test_expect_u(test, "exactly one malloc in an echo run (the writer's ThreadArgs)", 1, mallocs);
    test_expect_u(test, "the ThreadArgs block is two pointers", 2 * sizeof(void*), _writer_args_size);
    test_expect_u(test, "the writer thread released its ThreadArgs", 0, _window_live(echo_window, LEDGER_SOURCE_MALLOC, _writer_args_size));
#else
    test_expect_u(test, "a POSIX echo run mallocs nothing", 0, mallocs);
#endif // OS_WINDOWS

    process_outcome_uninit(&echo);

    test_expect_u(test, "uninit released everything the echo run took", 0, _window_live_total(echo_window));

    test_case_end(test);
}

static void _test_first_growth_failure(Test *const test) {
    test_case_begin(test, "a failed FIRST growth reports NO_MEMORY without PARTIAL and kills the child at once");

    USize const size = _TEST_READ_CHUNK + 1;
    ProcessOutcome outcome = DEFAULT_INITIALIZATION;
    LedgerWindow window = DEFAULT_INITIALIZATION;
    U64 const started = _test_now_milliseconds();

    _test_fail_arm(LEDGER_SOURCE_CALLOC, size, 1);

    Result const result = _test_run("--child-flood", nullptr, _TEST_FLOOD_LIMIT, &outcome, &window);
    USize const fired = _test_fail_disarm();
    U64 const elapsed = _test_now_milliseconds() - started;

    test_expect_true(test, "the injection fired", fired >= 1);
    test_expect_true(test, "the run failed with NO_MEMORY, no PARTIAL flag", result == PROCESS_RESULT_NO_MEMORY);
    test_expect_true(test, "the failure is not reported as an output overrun", result_clear_flag(result, RESULT_FLAG_PARTIAL) != PROCESS_RESULT_OUTPUT_TOO_LARGE);
    test_expect_true(test, "no capture is held", memory_empty(outcome.output));
    test_expect_u(test, "no bytes are claimed", 0, outcome.output_size);
    test_expect_true(test, "the flooding child was killed by the module", _test_killed(&outcome));
    test_expect_false(test, "a capture failure is not a timeout", outcome.timed_out);
    test_expect_true(test, "the child was killed at once, not waited out", elapsed < _TEST_KILL_BOUND_MS);
    test_expect_u(test, "the failure landed on the first growth (no block of that size was taken)", 0, _window_records(window, LEDGER_SOURCE_CALLOC, size));
    test_expect_u(test, "nothing the run took is still live", 0, _window_live_total(window));

    process_outcome_uninit(&outcome);

    // Positive anchor: the same run, disarmed, works again and captures exactly to the limit.
    ProcessOutcome retry = DEFAULT_INITIALIZATION;
    LedgerWindow retry_window = DEFAULT_INITIALIZATION;
    Result const retried = _test_run("--child-flood", nullptr, _TEST_FLOOD_LIMIT, &retry, &retry_window);

    test_expect_true(test, "the retry reaches the output limit", retried == result_with_flag(PROCESS_RESULT_OUTPUT_TOO_LARGE, RESULT_FLAG_PARTIAL));
    test_expect_u(test, "the retry captured exactly the limit", _TEST_FLOOD_LIMIT, retry.output_size);
    test_expect_u(test, "the retry took the first growth block", 1, _window_records(retry_window, LEDGER_SOURCE_CALLOC, size));

    process_outcome_uninit(&retry);

    test_expect_u(test, "uninit released everything the retry took", 0, _window_live_total(retry_window));

    test_case_end(test);
}

static void _test_later_growth_failure_sweep(Test *const test) {
    test_case_begin(test, "a failed LATER growth keeps the prefix, flags PARTIAL, and must still be released");

    // Every growth step after the first, from the measured profile. Each keeps the buffer the
    // step before it allocated: that block must stay live (owned by the outcome) until uninit,
    // every block before it must have been taken AND already released, and the prefix must be
    // the child's bytes.
    test_expect_true(test, "the profile measured more than one growth step", _growth_count >= 2);

    for (USize step = 1; step < _growth_count; step += 1) {
        USize const size = _growth_sizes[step];
        USize const held = _growth_sizes[step - 1];
        USize const floor = (step >= 2) ? _growth_sizes[step - 2] : 1;
        ProcessOutcome outcome = DEFAULT_INITIALIZATION;
        LedgerWindow window = DEFAULT_INITIALIZATION;
        U64 const started = _test_now_milliseconds();

        _test_fail_arm(LEDGER_SOURCE_CALLOC, size, 1);

        Result const result = _test_run("--child-flood", nullptr, _TEST_FLOOD_LIMIT, &outcome, &window);
        USize const fired = _test_fail_disarm();
        U64 const elapsed = _test_now_milliseconds() - started;

        test_expect_true(test, "the injection fired", fired >= 1);
        test_expect_true(test, "the run failed with NO_MEMORY | PARTIAL", result == result_with_flag(PROCESS_RESULT_NO_MEMORY, RESULT_FLAG_PARTIAL));
        test_expect_true(test, "the prefix is held", !memory_empty(outcome.output));
        test_expect_true(test, "the prefix fills past the step before and fits the held buffer", outcome.output_size >= floor && outcome.output_size < held);
        test_expect_true(test, "the prefix is the child's bytes, unbroken", !memory_empty(outcome.output) && _test_pattern_matches(outcome.output, outcome.output_size));
        test_expect_true(test, "the prefix is NUL-terminated", !memory_empty(outcome.output) && outcome.output_size < held && outcome.output[outcome.output_size] == '\0');
        test_expect_u(test, "the failure landed on this growth step", 0, _window_records(window, LEDGER_SOURCE_CALLOC, size));
        test_expect_u(test, "the held buffer is live, owned by the outcome", 1, _window_live(window, LEDGER_SOURCE_CALLOC, held));
        test_expect_u(test, "the held buffer is the ONLY live block", 1, _window_live_total(window));
        test_expect_true(test, "the child was killed at once", _test_killed(&outcome) && elapsed < _TEST_KILL_BOUND_MS);

        USize earlier_taken = 0;

        for (USize i = 0; i < step; i += 1) {
            earlier_taken += _window_records(window, LEDGER_SOURCE_CALLOC, _growth_sizes[i]);
        }

        test_expect_u(test, "every earlier growth step was taken once", step, earlier_taken);

        process_outcome_uninit(&outcome);

        test_expect_u(test, "uninit released the held buffer", 0, _window_live_total(window));
    }

    test_case_end(test);
}

static void _test_empty_output_failure(Test *const test) {
    test_case_begin(test, "a failed owned \"\" reports NO_MEMORY while the outcome still describes the run");

    test_expect_u(test, "the profile found the owned \"\"", 1, _empty_ordinal);

    ProcessOutcome outcome = DEFAULT_INITIALIZATION;
    LedgerWindow window = DEFAULT_INITIALIZATION;

    _test_fail_arm(LEDGER_SOURCE_CALLOC, 1, _empty_ordinal);

    Result const result = _test_run("--child-silent", nullptr, 0, &outcome, &window);
    USize const fired = _test_fail_disarm();

    test_expect_true(test, "the injection fired", fired >= _empty_ordinal);
    test_expect_true(test, "the run failed with NO_MEMORY", result == PROCESS_RESULT_NO_MEMORY);
    test_expect_true(test, "no output is held", memory_empty(outcome.output));
    test_expect_u(test, "no bytes are claimed", 0, outcome.output_size);

    // The positive anchors: the run itself completed - everything before the "" was taken, as
    // in the profile - and the outcome still says how the child ended.
    test_expect_u(test, "the run took everything but the \"\"", _silent_allocations - 1, window.end - window.start);
    test_expect_u(test, "the child is still reported as exited", PROCESS_ENDED_EXITED, outcome.ended);
    test_expect_i(test, "its exit code is still reported", 0, outcome.exit_code);
    test_expect_u(test, "nothing the run took is still live", 0, _window_live_total(window));

    process_outcome_uninit(&outcome);

    ProcessOutcome retry = DEFAULT_INITIALIZATION;
    LedgerWindow retry_window = DEFAULT_INITIALIZATION;
    Result const retried = _test_run("--child-silent", nullptr, 0, &retry, &retry_window);

    test_expect_true(test, "the retry succeeds", result_is_success(retried) && process_outcome_succeeded(&retry));
    test_expect_u(test, "the retry owns its \"\"", 1, _window_live(retry_window, LEDGER_SOURCE_CALLOC, 1));

    process_outcome_uninit(&retry);

    test_expect_u(test, "uninit released the \"\"", 0, _window_live_total(retry_window));

    test_case_end(test);
}

#ifdef OS_WINDOWS
static void _test_command_line_failure(Test *const test) {
    test_case_begin(test, "a failed command line refuses the run with NO_MEMORY and spawns nothing");

    ProcessOutcome outcome = DEFAULT_INITIALIZATION;
    LedgerWindow window = DEFAULT_INITIALIZATION;

    _test_fail_arm(LEDGER_SOURCE_CALLOC, _TEST_COMMAND_LINE_BYTES, 1);

    Result const result = _test_run("--child-silent", nullptr, 0, &outcome, &window);
    USize const fired = _test_fail_disarm();

    test_expect_true(test, "the injection fired", fired >= 1);
    test_expect_true(test, "the run failed with NO_MEMORY", result == PROCESS_RESULT_NO_MEMORY);
    test_expect_u(test, "no child was collected", PROCESS_ENDED_UNKNOWN, outcome.ended);
    test_expect_i(test, "no exit code is claimed", -1, outcome.exit_code);
    test_expect_true(test, "no output is held", memory_empty(outcome.output));

    // The command line is the run's FIRST allocation, so a run that stopped there took nothing.
    test_expect_u(test, "the run stopped at the command line (nothing was taken)", 0, window.end - window.start);

    process_outcome_uninit(&outcome);

    ProcessOutcome retry = DEFAULT_INITIALIZATION;
    LedgerWindow retry_window = DEFAULT_INITIALIZATION;
    Result const retried = _test_run("--child-silent", nullptr, 0, &retry, &retry_window);

    test_expect_true(test, "the retry runs the child", result_is_success(retried) && process_outcome_succeeded(&retry));
    test_expect_u(test, "the retry took the command line", 1, _window_records(retry_window, LEDGER_SOURCE_CALLOC, _TEST_COMMAND_LINE_BYTES));

    process_outcome_uninit(&retry);

    test_expect_u(test, "uninit released everything the retry took", 0, _window_live_total(retry_window));

    test_case_end(test);
}

static void _test_attribute_list_failure(Test *const test) {
    test_case_begin(test, "a failed attribute list refuses the run and releases the command line");

    ProcessOutcome outcome = DEFAULT_INITIALIZATION;
    LedgerWindow window = DEFAULT_INITIALIZATION;

    _test_fail_arm(LEDGER_SOURCE_CALLOC, _attribute_list_size, 1);

    Result const result = _test_run("--child-silent", nullptr, 0, &outcome, &window);
    USize const fired = _test_fail_disarm();

    test_expect_true(test, "the injection fired", fired >= 1);
    test_expect_true(test, "the run failed with NO_MEMORY", result == PROCESS_RESULT_NO_MEMORY);
    test_expect_u(test, "no child was collected", PROCESS_ENDED_UNKNOWN, outcome.ended);
    test_expect_true(test, "no output is held", memory_empty(outcome.output));

    // Positive anchor: the command line WAS taken on this run, so the zero-live check below is
    // about a block that existed, not one that never did.
    test_expect_u(test, "the command line was taken", 1, _window_records(window, LEDGER_SOURCE_CALLOC, _TEST_COMMAND_LINE_BYTES));
    test_expect_u(test, "it was the only thing taken", 1, window.end - window.start);
    test_expect_u(test, "the command line was released on the failure path", 0, _window_live_total(window));

    process_outcome_uninit(&outcome);

    ProcessOutcome retry = DEFAULT_INITIALIZATION;
    LedgerWindow retry_window = DEFAULT_INITIALIZATION;
    Result const retried = _test_run("--child-silent", nullptr, 0, &retry, &retry_window);

    test_expect_true(test, "the retry runs the child", result_is_success(retried) && process_outcome_succeeded(&retry));

    process_outcome_uninit(&retry);

    test_expect_u(test, "uninit released everything the retry took", 0, _window_live_total(retry_window));

    test_case_end(test);
}

static void _test_writer_thread_failure(Test *const test) {
    test_case_begin(test, "a stdin writer that cannot start fails closed: the child is killed, not left running");

    test_expect_true(test, "the profile measured the ThreadArgs block", _writer_args_size != 0);

    ProcessOutcome outcome = DEFAULT_INITIALIZATION;
    LedgerWindow window = DEFAULT_INITIALIZATION;
    U64 const started = _test_now_milliseconds();

    _test_fail_arm(LEDGER_SOURCE_MALLOC, _writer_args_size, 1);

    // The echo child blocks reading a stdin nobody will feed, so it cannot exit on its own: a
    // module that skipped the kill would hang here until the deadline.
    Result const result = _test_run("--child-echo", _TEST_ECHO_INPUT, 0, &outcome, &window);
    USize const fired = _test_fail_disarm();
    U64 const elapsed = _test_now_milliseconds() - started;

    test_expect_true(test, "the injection fired", fired >= 1);
    test_expect_true(test, "the run failed", result_is_error(result));
    test_expect_true(test, "the failure is a memory failure", result_category(result) == RESULT_CATEGORY_MEMORY);
    test_expect_u(test, "the child was killed by the module", PROCESS_ENDED_KILLED, outcome.ended);
    test_expect_true(test, "the kill was at once, not at the deadline", elapsed < _TEST_KILL_BOUND_MS);
    test_expect_true(test, "no output is held", memory_empty(outcome.output));
    test_expect_u(test, "no ThreadArgs block was taken", 0, _window_records(window, LEDGER_SOURCE_MALLOC, _writer_args_size));
    test_expect_u(test, "the run's command line was taken", 1, _window_records(window, LEDGER_SOURCE_CALLOC, _TEST_COMMAND_LINE_BYTES));
    test_expect_u(test, "the attribute list was taken", 1, _window_records(window, LEDGER_SOURCE_CALLOC, _attribute_list_size));
    test_expect_u(test, "both were released on the failure path", 0, _window_live_total(window));

    process_outcome_uninit(&outcome);

    ProcessOutcome retry = DEFAULT_INITIALIZATION;
    LedgerWindow retry_window = DEFAULT_INITIALIZATION;
    Result const retried = _test_run("--child-echo", _TEST_ECHO_INPUT, 0, &retry, &retry_window);

    test_expect_true(test, "the retry delivers the input", result_is_success(retried) && !memory_empty(retry.output) && strcmp(retry.output, _TEST_ECHO_INPUT) == 0);

    process_outcome_uninit(&retry);

    test_expect_u(test, "uninit released everything the retry took", 0, _window_live_total(retry_window));

    test_case_end(test);
}
#endif // OS_WINDOWS

static void _test_instrument_totals(Test *const test) {
    test_case_begin(test, "the instrument itself stayed sound across the whole binary");

    // Last, so these cumulative counters cover every allocation the binary made. An overflowed
    // ledger would make every live count under-report, turning each zero-live check above into
    // a vacuous pass.
    test_expect_u(test, "the ledger never overflowed", 0, _ledger_overflow);
    test_expect_u(test, "no double free anywhere", 0, _double_free_count);
    test_expect_u(test, "no free of an untracked block", 0, _foreign_free_count);
    test_expect_true(test, "the ledger recorded the runs", _ledger_mark() > 0);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Runner
 *============================================================================*/

int main(int argc, char **argv) {
    if (argc >= 2) {
        if (strcmp(argv[1], "--child-flood") == 0) {
            return _child_flood();
        }

        if (strcmp(argv[1], "--child-echo") == 0) {
            return _child_echo();
        }

        if (strcmp(argv[1], "--child-silent") == 0) {
            return 0;
        }
    }

    LogConfig const log_config = {
        .level = LOG_LEVEL_ERROR,
        .stream = LOG_STREAM_STDOUT,
        .timestamp_enabled = true,
        .autoflush = true,
    };

    log_init(log_config);

    _program = argv[0];

    Test test = test_init("tests/process/test_oom.c");

    test_suite_begin(&test, "process allocation failure");

    // The ledger's own scan comes first: every assertion below trusts it.
    test_section_begin(&test, "the ledger itself");
    _test_ledger_find_returns_the_newest_record(&test);
    test_section_end(&test);

    test_section_begin(&test, "measured profile");
    _test_profile(&test);
    test_section_end(&test);

    test_section_begin(&test, "recoverable out of memory: the capture");
    _test_first_growth_failure(&test);
    _test_later_growth_failure_sweep(&test);
    _test_empty_output_failure(&test);
    test_section_end(&test);

#ifdef OS_WINDOWS
    test_section_begin(&test, "recoverable out of memory: the spawn (Windows)");
    _test_command_line_failure(&test);
    _test_attribute_list_failure(&test);
    _test_writer_thread_failure(&test);
    test_section_end(&test);
#else
    printf("SKIP: the command line, attribute list and stdin-writer-thread cases are Windows-only - the POSIX backend makes none of those allocations (the profile case measures that)\n");
#endif // OS_WINDOWS

    test_section_begin(&test, "the instrument");
    _test_instrument_totals(&test);
    test_section_end(&test);

    test_suite_end(&test);

    return test_uninit(&test);
}