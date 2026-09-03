#include <container/map/map_char_char.h>
#include <memory/memory.h>
#include <test/test.h>

/*
 * Allocation-failure sweep for the four HEAP-only decline paths that test_map_char_char.c's
 * _test_new_and_delete still records as UNVERIFIED (new_2's refusal half only -
 * _test_heap_new_and_alloc_init_entry_points now defers new_1's half to this file instead), or
 * that the arena suite can pin only through an arena's own refusal rather than through the
 * allocator that actually backs it.
 *
 *   (a) new_1 answers nullptr when the sizeof(Map_Char_Char) calloc fails. new_1 borrows no
 *       arrays before that borrow, so 64 bytes (sizeof(Map_Char_Char) with ARENA_IMPLEMENTATION:
 *       two AL_Char, each allocator+capacity+data+size = 32 bytes) is a size nothing else in
 *       this binary allocates - new_2/new_3 are NOT used here, because a capacity or a source
 *       list would add allocations at other sizes and blur which decline is under test.
 *   (b) add_static_2's KEY copy declines -> false, with the ledger unchanged: nothing was ever
 *       allocated for the pair, so there is nothing to leak or to give back.
 *   (c) add_static's VALUE copy declines after the key copy already landed -> false, and the
 *       key copy is FREED - the release add_static's @note promises ("A partial copy is
 *       released before returning false, so a decline leaks nothing").
 *   (d) init_3's mid-sequence decline on the HEAP -> an EMPTY map, with the first pair's key
 *       and value both freed. test_map_char_char.c's _test_init_3_mid_sequence_rollback pins
 *       this on an ARENA (arena_linear exhaustion, checked by SIZE); the heap path goes
 *       through this file's own calloc wrap instead, and is the only way to prove the SAME
 *       rollback fires for a failing malloc.
 *
 * HOW. Same seam as tests/container/str/test_oom.c: -Wl,--wrap=calloc / --wrap=free. Arming is
 * by SIZE, never by ordinal count across the whole binary - each case arms immediately before
 * its own call and disarms immediately after, so the ordinal-1 injection always targets the
 * very next calloc of that size. ERROR_CHECK_ENABLED stays ON: every path here degrades through
 * allocator_try_borrow, which returns nullptr regardless of that define, so this binary tests
 * the production configuration.
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
    // Guarded because count * size can wrap, and a wrapped product could false-match
    // _fail_size and inject a failure into an unrelated allocation.
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

/* Newest-first, because the allocator recycles addresses: a forward scan matches a stale
 * record of a block freed earlier and reads a legal free as a double free. */
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

// The struct's own size: new_1 borrows nothing else before this borrow, so that size is
// unique to it. Taken from the type, not written as a number, so the arm follows the
// struct if its layout ever moves instead of firing on whatever else allocates 64 bytes.
#define _TEST_STRUCT_BYTES sizeof(Map_Char_Char)

static void _test_new_1_declines_on_struct_borrow_failure(Test *const test) {
    test_case_begin(test, "new_1: a failed struct calloc answers nullptr, not an abort");

    Map_Char_Char *control = map_char_char_new_1();

    test_expect_not_null(test, "unarmed: new_1 allocates (anchor)", control);

    map_char_char_delete(&control);

    USize const live_before = _live_of_size(_TEST_STRUCT_BYTES);

    _test_fail_arm(_TEST_STRUCT_BYTES, 1);

    Map_Char_Char *const declined = map_char_char_new_1();

    // Read before disarm, which zeroes the counter.
    USize const fired = _fail_seen;

    _test_fail_disarm();

    test_expect_u(test, "the injection fired", 1, fired);
    test_expect_null(test, "new_1 answers nullptr", declined);
    test_expect_u(test, "no struct-sized block was left live", live_before, _live_of_size(_TEST_STRUCT_BYTES));

    test_case_end(test);
}

/* 37, so the key copy is CHAR_END_CHARACTER + 37 = 38 bytes - a length nothing else in this
 * binary allocates, so arming it cannot false-match an unrelated calloc. */
#define _TEST_KEY_SIZE          37
#define _TEST_KEY_COPY_BYTES    (_TEST_KEY_SIZE + CHAR_END_CHARACTER)

static void _test_add_static_2_key_copy_declines(Test *const test) {
    test_case_begin(test, "add_static_2: a declined KEY copy leaves the map and the ledger unchanged");

    Map_Char_Char map = map_char_char_init_1();

    char key[_TEST_KEY_SIZE + CHAR_END_CHARACTER] = DEFAULT_INITIALIZATION;

    char_fill(key, _TEST_KEY_SIZE, 'k');

    USize const live_before = _live_of_size(_TEST_KEY_COPY_BYTES);

    _test_fail_arm(_TEST_KEY_COPY_BYTES, 1);

    bool const added = map_char_char_add_static_2(&map, key, _TEST_KEY_SIZE, "v", 1);

    USize const fired = _fail_seen;

    _test_fail_disarm();

    test_expect_u(test, "the injection fired", 1, fired);
    test_expect_false(test, "add_static_2 declined", added);
    test_expect_u(test, "nothing was stored", 0, map_char_char_get_size(&map));
    test_expect_u(test, "no key-sized block was left live - there was nothing to give back", live_before, _live_of_size(_TEST_KEY_COPY_BYTES));

    map_char_char_uninit(&map);

    test_case_end(test);
}

/* 41, so the value copy is CHAR_END_CHARACTER + 41 = 42 bytes - distinct from the key copy's
 * size below (6) and from every other size this case allocates. */
#define _TEST_ADD_STATIC_KEY           "abcde"
#define _TEST_ADD_STATIC_KEY_SIZE      5
#define _TEST_ADD_STATIC_KEY_BYTES     (_TEST_ADD_STATIC_KEY_SIZE + CHAR_END_CHARACTER)
#define _TEST_VALUE_SIZE                41
#define _TEST_VALUE_COPY_BYTES         (_TEST_VALUE_SIZE + CHAR_END_CHARACTER)

static void _test_add_static_value_copy_declines_and_frees_the_key(Test *const test) {
    test_case_begin(test, "add_static: a declined VALUE copy still frees the KEY copy it already made");

    Map_Char_Char map = map_char_char_init_1();

    char value[_TEST_VALUE_SIZE + CHAR_END_CHARACTER] = DEFAULT_INITIALIZATION;

    char_fill(value, _TEST_VALUE_SIZE, 'v');

    USize const key_live_before = _live_of_size(_TEST_ADD_STATIC_KEY_BYTES);
    USize const value_live_before = _live_of_size(_TEST_VALUE_COPY_BYTES);

    _test_fail_arm(_TEST_VALUE_COPY_BYTES, 1);

    bool const added = map_char_char_add_static(&map, _TEST_ADD_STATIC_KEY, value);

    USize const fired = _fail_seen;

    _test_fail_disarm();

    test_expect_u(test, "the injection fired", 1, fired);
    test_expect_false(test, "add_static declined", added);
    test_expect_u(test, "nothing was stored", 0, map_char_char_get_size(&map));

    /* The pin the case exists for: the KEY copy landed (it is a different, unarmed size), and
     * the header's "a partial copy is released before returning false" promise means it must
     * be freed again by the time add_static returns - not left live as an orphan. */
    test_expect_u(test, "no value-sized block was left live", value_live_before, _live_of_size(_TEST_VALUE_COPY_BYTES));
    test_expect_u(test, "and the key copy was given back too", key_live_before, _live_of_size(_TEST_ADD_STATIC_KEY_BYTES));

    map_char_char_uninit(&map);

    test_case_end(test);
}

/* Pair 0 lands at sizes nothing else in this file allocates (3 and 7); pair 1's KEY is armed
 * at a fourth, equally unique size (54), so the decline happens on the SECOND pair rather than
 * the first - the mid-sequence case the arena suite already covers, pinned here on the heap. */
#define _TEST_PAIR0_KEY             "k0"
#define _TEST_PAIR0_KEY_BYTES       3
#define _TEST_PAIR0_VALUE           "value0"
#define _TEST_PAIR0_VALUE_BYTES     7
#define _TEST_PAIR1_KEY_SIZE        53
#define _TEST_PAIR1_KEY_BYTES       (_TEST_PAIR1_KEY_SIZE + CHAR_END_CHARACTER)

static void _test_init_3_mid_sequence_decline_frees_the_landed_pair(Test *const test) {
    test_case_begin(test, "init_3 on the heap: a decline AFTER a pair has landed rolls the whole map back");

    AL_Char keys = al_char_init_1();
    AL_Char values = al_char_init_1();

    char pair1_key[_TEST_PAIR1_KEY_SIZE + CHAR_END_CHARACTER] = DEFAULT_INITIALIZATION;

    char_fill(pair1_key, _TEST_PAIR1_KEY_SIZE, 'z');

    al_char_add_last(&keys, char_new_2(_TEST_PAIR0_KEY));
    al_char_add_last(&keys, char_new_2(pair1_key));
    al_char_add_last(&values, char_new_2(_TEST_PAIR0_VALUE));
    al_char_add_last(&values, char_new_2("v1"));

    USize const pair0_key_live_before = _live_of_size(_TEST_PAIR0_KEY_BYTES);
    USize const pair0_value_live_before = _live_of_size(_TEST_PAIR0_VALUE_BYTES);

    _test_fail_arm(_TEST_PAIR1_KEY_BYTES, 1);

    Map_Char_Char map = map_char_char_init_3(&keys, &values);

    USize const fired = _fail_seen;

    _test_fail_disarm();

    test_expect_u(test, "the injection fired", 1, fired);
    test_expect_true(test, "the map came back empty, not holding the one pair that landed", map_char_char_empty(&map));
    test_expect_u(test, "size 0", 0, map_char_char_get_size(&map));
    test_expect_null(test, "the landed pair is gone too", map_char_char_at_1(&map, _TEST_PAIR0_KEY));

    /* Whether the rollback FREED pair 0's key and value, not merely detached them. */
    test_expect_u(test, "pair 0's key copy was freed", pair0_key_live_before, _live_of_size(_TEST_PAIR0_KEY_BYTES));
    test_expect_u(test, "pair 0's value copy was freed", pair0_value_live_before, _live_of_size(_TEST_PAIR0_VALUE_BYTES));

    map_char_char_uninit(&map);

    /* Positive control: the SAME sources, unarmed, build a whole two-pair map - proof the
     * decline above came from the injected failure and not from anything wrong with the data. */
    Map_Char_Char whole = map_char_char_init_3(&keys, &values);

    test_expect_u(test, "unarmed, both pairs land", 2, map_char_char_get_size(&whole));
    test_expect_string(test, "including the pair that failed above", "v1", map_char_char_at_1(&whole, pair1_key));

    map_char_char_uninit(&whole);

    al_char_uninit(&keys);
    al_char_uninit(&values);

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

    Test test = test_init("tests/container/map/test_oom.c");

    test_suite_begin(&test, "map_char_char allocation-failure sweep");
    _test_new_1_declines_on_struct_borrow_failure(&test);
    _test_add_static_2_key_copy_declines(&test);
    _test_add_static_value_copy_declines_and_frees_the_key(&test);
    _test_init_3_mid_sequence_decline_frees_the_landed_pair(&test);
    _test_ledger_sane(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}