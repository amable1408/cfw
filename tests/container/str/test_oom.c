#include <container/str/str.h>
#include <memory/memory.h>
#include <test/test.h>

/*
 * Allocation-failure sweep for the two str constructors that used to abort on a
 * caller-measured size: str_init_static (25 callers, most of them measuring from
 * the wire - json labels, regex groups, HTTP header values) and str_format.
 *
 * WHY. Both were converted from memory_alloc to memory_try_alloc, with the
 * documented degradation "the empty Str". A decline branch is unverified until an
 * allocation is actually made to fail - reading found the dead guard in
 * str_format, but only a failed calloc proves the empty Str comes back with no
 * block left live and no abort on the way out.
 *
 * HOW. memory_try_alloc calls calloc DIRECTLY (memory.c) and does not route
 * through MemoryHooks, so hook injection intercepts nothing. The linker's seam
 * does: -Wl,--wrap=calloc redirects every calloc to __wrap_calloc below, and
 * --wrap=free lets a ledger count what stays live. The injector arms by SIZE, not
 * by ordinal (tests/benchmark/test_oom.c's idiom): the modules linked here
 * allocate for their own reasons, and the copies under test are the only
 * allocations of exactly `data_size + CHAR_END_CHARACTER` bytes.
 *
 * ERROR_CHECK_ENABLED stays ON, unlike the benchmark harness: the paths under
 * test take their block with memory_try_alloc, which returns nullptr regardless
 * of that define, so this binary tests the production configuration. The
 * unarmed CONTROL cases are the anchor - they prove the same call produces the
 * owned copy when the allocation succeeds, so an arm size that never matches
 * cannot pass by measuring an ordinary run twice.
 *
 * SINGLE-THREADED BY REQUIREMENT: the counters below are plain statics.
 */

/*==============================================================================
 * MARK: - Injector
 *============================================================================*/

#define _TEST_LEDGER_CAPACITY 4096

typedef struct {
    void    *pointer;
    USize   size;
    bool    freed;
} LedgerEntry;

static LedgerEntry  _ledger[_TEST_LEDGER_CAPACITY];
static USize        _ledger_count        = 0;
static USize        _ledger_overflow     = 0;
static USize        _double_free_count   = 0;
static USize        _foreign_free_count  = 0;
static USize        _fail_size           = 0;
static USize        _fail_ordinal        = 0;
static USize        _fail_seen           = 0;

void* __real_calloc(USize const count, USize const size);
void __real_free(void *const pointer);

void* __wrap_calloc(USize const count, USize const size) {
    // Guarded because count * size can wrap, and a wrapped product could
    // false-match _fail_size and inject a failure into an unrelated allocation.
    USize const bytes = (size != 0 && count > USIZE_MAX / size) ? USIZE_MAX : count * size;

    if (_fail_size != 0 && bytes == _fail_size) {
        _fail_seen += 1;

        if (_fail_seen >= _fail_ordinal) {
            return nullptr;
        }
    }

    void *const pointer = __real_calloc(count, size);

    if (pointer == nullptr) {
        return pointer;
    }

    if (_ledger_count >= _TEST_LEDGER_CAPACITY) {
        _ledger_overflow += 1;

        return pointer;
    }

    _ledger[_ledger_count].pointer  = pointer;
    _ledger[_ledger_count].size     = bytes;
    _ledger[_ledger_count].freed    = false;
    _ledger_count                  += 1;

    return pointer;
}

/* Newest-first, because the allocator recycles addresses: a forward scan matches
 * the stale record of a block freed earlier and reads a legal free as a double
 * free. */
static USize _ledger_find(void const *const pointer) {
    for (USize i = _ledger_count; i > 0; i -= 1) {
        if (_ledger[i - 1].pointer == pointer) {
            return i - 1;
        }
    }

    return _ledger_count;
}

void __wrap_free(void *const pointer) {
    if (pointer == nullptr) {
        __real_free(pointer);

        return;
    }

    USize const found = _ledger_find(pointer);

    if (found == _ledger_count) {
        _foreign_free_count += 1;

        __real_free(pointer);

        return;
    }

    if (_ledger[found].freed) {
        _double_free_count += 1;
    }

    _ledger[found].freed = true;

    __real_free(pointer);
}

/** @brief Blocks of exactly this size allocated and not yet released. */
static USize _live_of_size(USize const bytes) {
    USize live = 0;

    for (USize i = 0; i < _ledger_count; i += 1) {
        if (!_ledger[i].freed && _ledger[i].size == bytes) {
            live += 1;
        }
    }

    return live;
}

/** @brief Arm the injector: fail the ordinal-th calloc of exactly this size. */
static void _test_fail_arm(USize const size, USize const ordinal) {
    _fail_size    = size;
    _fail_ordinal = ordinal;
    _fail_seen    = 0;
}

static void _test_fail_disarm(void) {
    _fail_size    = 0;
    _fail_ordinal = 0;
    _fail_seen    = 0;
}

/*==============================================================================
 * MARK: - Cases
 *============================================================================*/

// "abcd" copies into 4 + CHAR_END_CHARACTER bytes; nothing else here takes 5.
#define _TEST_SOURCE        "abcd"
#define _TEST_SOURCE_SIZE   4
#define _TEST_COPY_BYTES    (_TEST_SOURCE_SIZE + CHAR_END_CHARACTER)

static void _test_init_static_control(Test *const test) {
    test_case_begin(test, "str_init_static: the unarmed copy is owned (anchor)");

    USize const live_before = _live_of_size(_TEST_COPY_BYTES);

    Str str = str_init_static(_TEST_SOURCE, _TEST_SOURCE_SIZE);

    test_expect_u(test, "size is the source size", _TEST_SOURCE_SIZE, str.size);
    test_expect_true(test, "the copy is owned", str.owned);
    test_expect_string(test, "the bytes are the source", _TEST_SOURCE, str.data);
    test_expect_u(test, "exactly one copy-sized block is live", live_before + 1, _live_of_size(_TEST_COPY_BYTES));

    str_uninit(&str);

    test_expect_u(test, "uninit released it", live_before, _live_of_size(_TEST_COPY_BYTES));

    test_case_end(test);
}

static void _test_init_static_armed(Test *const test) {
    test_case_begin(test, "str_init_static: a failed copy degrades to the empty Str");

    USize const live_before = _live_of_size(_TEST_COPY_BYTES);

    _test_fail_arm(_TEST_COPY_BYTES, 1);

    Str str = str_init_static(_TEST_SOURCE, _TEST_SOURCE_SIZE);

    // Read before disarm, which zeroes the counter.
    USize const fired = _fail_seen;

    _test_fail_disarm();

    test_expect_u(test, "the injection fired", 1, fired);
    test_expect_u(test, "size is 0 - the documented degradation", 0, str.size);
    test_expect_false(test, "nothing is owned", str.owned);
    test_expect_null(test, "no buffer is attached", str.data);
    test_expect_u(test, "no copy-sized block was left live", live_before, _live_of_size(_TEST_COPY_BYTES));

    // What every caller does with the result, owned or not: must be a safe no-op.
    str_uninit(&str);

    test_expect_u(test, "uninit on the empty Str released nothing", live_before, _live_of_size(_TEST_COPY_BYTES));

    test_case_end(test);
}

// "abcd-12" is 7 bytes; str_format sizes its block from vsnprintf's count + 1.
#define _TEST_FORMAT_EXPECTED   "abcd-12"
#define _TEST_FORMAT_SIZE       7
#define _TEST_FORMAT_BYTES      (_TEST_FORMAT_SIZE + CHAR_END_CHARACTER)

static void _test_format_control(Test *const test) {
    test_case_begin(test, "str_format: the unarmed render is owned (anchor)");

    USize const live_before = _live_of_size(_TEST_FORMAT_BYTES);

    Str str = str_format("%s-%d", _TEST_SOURCE, 12);

    test_expect_u(test, "size is the rendered size", _TEST_FORMAT_SIZE, str.size);
    test_expect_true(test, "the render is owned", str.owned);
    test_expect_string(test, "the bytes are the render", _TEST_FORMAT_EXPECTED, str.data);
    test_expect_u(test, "exactly one render-sized block is live", live_before + 1, _live_of_size(_TEST_FORMAT_BYTES));

    str_uninit(&str);

    test_expect_u(test, "uninit released it", live_before, _live_of_size(_TEST_FORMAT_BYTES));

    test_case_end(test);
}

static void _test_format_armed(Test *const test) {
    test_case_begin(test, "str_format: a failed render degrades to the empty Str");

    USize const live_before = _live_of_size(_TEST_FORMAT_BYTES);

    _test_fail_arm(_TEST_FORMAT_BYTES, 1);

    Str str = str_format("%s-%d", _TEST_SOURCE, 12);

    // Read before disarm, which zeroes the counter.
    USize const fired = _fail_seen;

    _test_fail_disarm();

    test_expect_u(test, "the injection fired", 1, fired);
    test_expect_u(test, "size is 0", 0, str.size);
    test_expect_false(test, "nothing is owned", str.owned);
    test_expect_null(test, "no buffer is attached", str.data);
    test_expect_u(test, "no render-sized block was left live", live_before, _live_of_size(_TEST_FORMAT_BYTES));

    str_uninit(&str);

    test_case_end(test);
}

static void _test_ledger_sane(Test *const test) {
    test_case_begin(test, "the ledger itself is trustworthy");

    test_expect_u(test, "no double free was observed", 0, _double_free_count);
    test_expect_u(test, "no free of an unrecorded block", 0, _foreign_free_count);
    test_expect_u(test, "the ledger never overflowed", 0, _ledger_overflow);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Main
 *============================================================================*/

int main(void) {
    LogConfig const log_config = {
        .level             = LOG_LEVEL_ERROR,
        .stream            = stdout,
        .timestamp_enabled = true,
        .autoflush         = true
    };

    log_init(log_config);

    Test test = test_init("tests/container/str/test_oom.c");

    test_suite_begin(&test, "str allocation-failure sweep");
    _test_init_static_control(&test);
    _test_init_static_armed(&test);
    _test_format_control(&test);
    _test_format_armed(&test);
    _test_ledger_sane(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}