#include <container/str/str.h>
#include <env/env.h>
#include <memory/memory.h>
#include <test/test.h>

/*
 * Allocation-failure sweep for the two env entry points that used to abort on a
 * caller-sized copy: env_set_2 (via _env_set_bytes) and env_from_char_2.
 *
 * WHY. Both were converted from memory_alloc to memory_try_alloc with the
 * degradation "a MEMORY-category Result, nothing applied". That claim is
 * unverified until the allocation is actually made to fail; the branch never
 * executes under the ordinary suite.
 *
 * HOW. memory_try_alloc calls calloc DIRECTLY, so -Wl,--wrap=calloc is the seam
 * (see tests/container/str/test_oom.c for the full rationale). The injector arms
 * by SIZE: the copy under test is `size + CHAR_END_CHARACTER` bytes, and the
 * fixture values are sized so nothing else in the call takes that many. Each
 * armed case is paired with an unarmed CONTROL on the same call, so an arm size
 * that never matches cannot pass by measuring an ordinary run twice.
 *
 * ERROR_CHECK_ENABLED stays ON: memory_try_alloc returns nullptr regardless of
 * it, so this binary tests the production configuration.
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
static USize        _fail_size           = 0;
static USize        _fail_ordinal        = 0;
static USize        _fail_seen           = 0;

void* __real_calloc(USize const count, USize const size);
void __real_free(void *const pointer);

void* __wrap_calloc(USize const count, USize const size) {
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

/* Newest-first: the allocator recycles addresses, and a forward scan would read
 * the legal free of a recycled address as a double free. */
static USize _ledger_find(void const *const pointer) {
    for (USize i = _ledger_count; i > 0; i -= 1) {
        if (_ledger[i - 1].pointer == pointer) {
            return i - 1;
        }
    }

    return _ledger_count;
}

/* Foreign frees are NOT counted here, unlike the str harness: the C runtime's
 * environment table (putenv / SetEnvironmentVariable) allocates and releases
 * outside this wrapper, so a free of an unrecorded block is expected traffic. */
void __wrap_free(void *const pointer) {
    if (pointer != nullptr) {
        USize const found = _ledger_find(pointer);

        if (found != _ledger_count) {
            if (_ledger[found].freed) {
                _double_free_count += 1;
            }

            _ledger[found].freed = true;
        }
    }

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

#define _TEST_NAME          "CFW_OOM_ENV_SET"
#define _TEST_FIRST         "first"
#define _TEST_SECOND        "second-value"
#define _TEST_SECOND_SIZE   12
#define _TEST_SECOND_BYTES  (_TEST_SECOND_SIZE + CHAR_END_CHARACTER)

static void _test_set_2_control(Test *const test) {
    test_case_begin(test, "env_set_2: the unarmed set applies (anchor)");

    Str const first = str_init_3(_TEST_FIRST, 5);

    test_expect_true(test, "seed value set", result_is_success(env_set_2(_TEST_NAME, &first)));
    test_expect_string(test, "seed value reads back", _TEST_FIRST, env_get_1(_TEST_NAME));

    USize const live_before = _live_of_size(_TEST_SECOND_BYTES);

    Str const second = str_init_3(_TEST_SECOND, _TEST_SECOND_SIZE);

    test_expect_true(test, "second value set", result_is_success(env_set_2(_TEST_NAME, &second)));
    test_expect_string(test, "second value reads back", _TEST_SECOND, env_get_1(_TEST_NAME));
    test_expect_u(test, "the internal copy was released", live_before, _live_of_size(_TEST_SECOND_BYTES));

    test_case_end(test);
}

static void _test_set_2_armed(Test *const test) {
    test_case_begin(test, "env_set_2: a failed copy is a MEMORY Result, variable untouched");

    Str const first = str_init_3(_TEST_FIRST, 5);

    test_expect_true(test, "seed value set", result_is_success(env_set_2(_TEST_NAME, &first)));

    USize const live_before = _live_of_size(_TEST_SECOND_BYTES);

    Str const second = str_init_3(_TEST_SECOND, _TEST_SECOND_SIZE);

    _test_fail_arm(_TEST_SECOND_BYTES, 1);

    Result const result = env_set_2(_TEST_NAME, &second);

    // Read before disarm, which zeroes the counter.
    USize const fired = _fail_seen;

    _test_fail_disarm();

    test_expect_u(test, "the injection fired", 1, fired);
    test_expect_true(test, "the Result is an error", result_is_error(result));
    test_expect_u(test, "the category is MEMORY", RESULT_CATEGORY_MEMORY, result_category(result));
    test_expect_string(test, "the variable still holds the seed", _TEST_FIRST, env_get_1(_TEST_NAME));
    test_expect_u(test, "no copy-sized block was left live", live_before, _live_of_size(_TEST_SECOND_BYTES));

    test_case_end(test);
}

// One line, applied as a whole: name=value\n. 27 bytes, so the copy is 28.
#define _TEST_TEXT          "CFW_OOM_ENV_FROM=from-text\n"
#define _TEST_TEXT_SIZE     27
#define _TEST_TEXT_BYTES    (_TEST_TEXT_SIZE + CHAR_END_CHARACTER)
#define _TEST_TEXT_NAME     "CFW_OOM_ENV_FROM"
#define _TEST_TEXT_VALUE    "from-text"

static void _test_from_char_2_control(Test *const test) {
    test_case_begin(test, "env_from_char_2: the unarmed parse applies (anchor)");

    env_unset(_TEST_TEXT_NAME);

    USize const live_before = _live_of_size(_TEST_TEXT_BYTES);

    test_expect_true(test, "parse succeeds", result_is_success(env_from_char_2(_TEST_TEXT, _TEST_TEXT_SIZE, true)));
    test_expect_string(test, "the line was applied", _TEST_TEXT_VALUE, env_get_1(_TEST_TEXT_NAME));
    test_expect_u(test, "the parser's copy was released", live_before, _live_of_size(_TEST_TEXT_BYTES));

    test_case_end(test);
}

static void _test_from_char_2_armed(Test *const test) {
    test_case_begin(test, "env_from_char_2: a failed copy is a MEMORY Result, nothing applied");

    env_unset(_TEST_TEXT_NAME);

    USize const live_before = _live_of_size(_TEST_TEXT_BYTES);

    _test_fail_arm(_TEST_TEXT_BYTES, 1);

    Result const result = env_from_char_2(_TEST_TEXT, _TEST_TEXT_SIZE, true);

    // Read before disarm, which zeroes the counter.
    USize const fired = _fail_seen;

    _test_fail_disarm();

    test_expect_u(test, "the injection fired", 1, fired);
    test_expect_true(test, "the Result is an error", result_is_error(result));
    test_expect_u(test, "the category is MEMORY", RESULT_CATEGORY_MEMORY, result_category(result));
    test_expect_null(test, "the line was NOT applied", env_get_1(_TEST_TEXT_NAME));
    test_expect_u(test, "no copy-sized block was left live", live_before, _live_of_size(_TEST_TEXT_BYTES));

    test_case_end(test);
}

static void _test_ledger_sane(Test *const test) {
    test_case_begin(test, "the ledger itself is trustworthy");

    test_expect_u(test, "no double free was observed", 0, _double_free_count);
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

    Test test = test_init("tests/env/test_oom.c");

    test_suite_begin(&test, "env allocation-failure sweep");
    _test_set_2_control(&test);
    _test_set_2_armed(&test);
    _test_from_char_2_control(&test);
    _test_from_char_2_armed(&test);
    _test_ledger_sane(&test);
    test_suite_end(&test);

    env_unset(_TEST_NAME);
    env_unset(_TEST_TEXT_NAME);

    return test_uninit(&test);
}