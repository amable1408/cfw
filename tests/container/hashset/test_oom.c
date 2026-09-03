#include <stdio.h>

#include <arena/arena.h>
#include <container/hashset/hashset.h>
#include <memory/memory.h>
#include <test/test.h>

/*
 * Allocation-failure sweep for hashset's HEAP borrow-decline paths that test_all.c's
 * refused/exhausted-ARENA cases cannot reach - a heap set has no arena to starve, so the only
 * way to fail one of its callocs is to fail calloc itself.
 *
 *   (a) hashset_init_2's FIRST bucket borrow (the `keys` array) fails -> the build backs out,
 *       leaving capacity 0 and size 0; add then declines with 0, same as a refused arena.
 *   (b) hashset_add_static's KEY COPY (_hashset_key_copy) fails -> add_static returns 0, size
 *       unchanged, and the ledger shows nothing left live for that size - there was no
 *       partial write to give back, unlike map's two-copy add_static.
 *   (c) _hashset_grow's doubling borrow fails, REPEATEDLY (armed with ordinal 1, which fails
 *       every matching call from the first onward, not just one): the OLD table is kept every
 *       time, so add keeps succeeding into its remaining free slots until size == capacity,
 *       and only THEN does the size == capacity check refuse it - never a spurious decline
 *       while free slots remain.
 *   (d) hashset_new_1's struct borrow fails -> nullptr, not an abort.
 *   (e) the second and third borrows of a build and of a grow (the `counts` and `owned`
 *       arrays) fail -> the earlier array of the same step is released once and the
 *       set lands in the same state as (a) or (c).
 *   (f) the struct borrow succeeds but the build behind new_1 fails -> _hashset_new_check
 *       releases the struct and answers nullptr, one failure shape; the same shape on a
 *       live arena too small for the first bucket array (alloc_new_1).
 *
 * HOW. Same seam as tests/container/map/test_oom.c and tests/container/str/test_oom.c:
 * -Wl,--wrap=calloc / --wrap=free. Arming is by SIZE, never by ordinal count across the whole
 * binary - each case arms immediately before its own call(s) and disarms immediately after.
 * An ordinal of 1 fails every matching call while armed (see __wrap_calloc: _fail_seen only
 * increases, so once it reaches the ordinal every further match fails too) - deliberate here,
 * since case (c) needs the SAME grow attempt to keep failing across several adds.
 *
 * ERROR_CHECK_ENABLED stays ON: every path here degrades through allocator_try_borrow, which
 * returns nullptr regardless of that define, so this binary tests the production
 * configuration.
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

/** @brief Arm the injector: fail the ordinal-th (and every later) calloc of exactly this size. */
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

// hashset_init_1/2's default request (_HASHSET_DEFAULT_KEYS == 16 keys) rounds up to 32
// buckets, so the `keys` array - the first of the three bucket borrows _hashset_build makes -
// is sizeof(char*) * 32 bytes: unique in this binary, and taken from sizeof rather than
// written as a number so the arm follows the pointer width if it ever changes.
#define _TEST_BUILD_CAPACITY_BUCKETS    32
#define _TEST_BUILD_KEYS_BYTES          (sizeof(char*) * _TEST_BUILD_CAPACITY_BUCKETS)

static void _test_init_2_declines_on_bucket_borrow_failure(Test *const test) {
    test_case_begin(test, "a failed FIRST bucket borrow leaves capacity 0, size 0; add declines with 0");

    HashSet control = hashset_init_1();

    test_expect_u(test, "unarmed: init_1 builds (anchor)", _TEST_BUILD_CAPACITY_BUCKETS, hashset_get_capacity(&control));

    hashset_uninit(&control);

    _test_fail_arm(_TEST_BUILD_KEYS_BYTES, 1);

    HashSet set = hashset_init_1();

    // Read before disarm, which zeroes the counter.
    USize const fired = _fail_seen;

    _test_fail_disarm();

    test_expect_u(test, "the injection fired", 1, fired);
    test_expect_u(test, "capacity is 0", 0, hashset_get_capacity(&set));
    test_expect_u(test, "size is 0", 0, hashset_size(&set));
    test_expect_true(test, "empty", hashset_empty(&set));
    test_expect_u(test, "add declines on the dead set", 0, hashset_add_static(&set, "x"));
    test_expect_u(test, "no keys-sized block was left live", 0, _live_of_size(_TEST_BUILD_KEYS_BYTES));

    hashset_uninit(&set);

    test_case_end(test);
}

// The build's other two bucket borrows, same 32-bucket table: `counts` (U32 * 32) is the
// SECOND borrow _hashset_build makes, `owned` (bool * 32) the THIRD.
#define _TEST_BUILD_COUNTS_BYTES (sizeof(U32) * _TEST_BUILD_CAPACITY_BUCKETS)
#define _TEST_BUILD_OWNED_BYTES  (sizeof(bool) * _TEST_BUILD_CAPACITY_BUCKETS)

static void _test_init_2_declines_on_counts_borrow_failure(Test *const test) {
    test_case_begin(test, "a failed SECOND bucket borrow (counts) backs out capacity 0, releasing keys");

    USize const keys_live_before = _live_of_size(_TEST_BUILD_KEYS_BYTES);

    _test_fail_arm(_TEST_BUILD_COUNTS_BYTES, 1);

    HashSet set = hashset_init_1();

    USize const fired = _fail_seen;

    _test_fail_disarm();

    test_expect_u(test, "the injection fired", 1, fired);
    test_expect_u(test, "capacity is 0", 0, hashset_get_capacity(&set));
    test_expect_u(test, "size is 0", 0, hashset_size(&set));
    test_expect_u(test, "the already-borrowed keys array was released, not leaked", keys_live_before, _live_of_size(_TEST_BUILD_KEYS_BYTES));

    hashset_uninit(&set);

    test_case_end(test);
}

static void _test_init_2_declines_on_owned_borrow_failure(Test *const test) {
    test_case_begin(test, "a failed THIRD bucket borrow (owned) backs out capacity 0, releasing keys and counts");

    USize const keys_live_before = _live_of_size(_TEST_BUILD_KEYS_BYTES);
    USize const counts_live_before = _live_of_size(_TEST_BUILD_COUNTS_BYTES);

    _test_fail_arm(_TEST_BUILD_OWNED_BYTES, 1);

    HashSet set = hashset_init_1();

    USize const fired = _fail_seen;

    _test_fail_disarm();

    test_expect_u(test, "the injection fired", 1, fired);
    test_expect_u(test, "capacity is 0", 0, hashset_get_capacity(&set));
    test_expect_u(test, "the already-borrowed keys array was released, not leaked", keys_live_before, _live_of_size(_TEST_BUILD_KEYS_BYTES));
    test_expect_u(test, "the already-borrowed counts array was released, not leaked", counts_live_before, _live_of_size(_TEST_BUILD_COUNTS_BYTES));

    hashset_uninit(&set);

    test_case_end(test);
}

// 41, so the key copy is CHAR_END_CHARACTER + 41 == 42 bytes - a length nothing else in this
// binary allocates.
#define _TEST_KEY_SIZE          41
#define _TEST_KEY_COPY_BYTES    (_TEST_KEY_SIZE + CHAR_END_CHARACTER)

static void _test_add_static_key_copy_declines(Test *const test) {
    test_case_begin(test, "add_static: a declined key copy leaves the set and the ledger unchanged");

    HashSet set = hashset_init_1();

    char key[_TEST_KEY_SIZE + CHAR_END_CHARACTER] = DEFAULT_INITIALIZATION;

    char_fill(key, _TEST_KEY_SIZE, 'k');

    USize const live_before = _live_of_size(_TEST_KEY_COPY_BYTES);

    _test_fail_arm(_TEST_KEY_COPY_BYTES, 1);

    U32 const added = hashset_add_static(&set, key);

    USize const fired = _fail_seen;

    _test_fail_disarm();

    test_expect_u(test, "the injection fired", 1, fired);
    test_expect_u(test, "add_static declined", 0, added);
    test_expect_u(test, "nothing was stored", 0, hashset_size(&set));
    test_expect_false(test, "the key does not resolve", hashset_contains(&set, key));
    test_expect_u(test, "no key-sized block was left live - there was nothing to give back", live_before, _live_of_size(_TEST_KEY_COPY_BYTES));

    hashset_uninit(&set);

    test_case_end(test);
}

// A grow always doubles the CURRENT capacity: starting from the default 32 buckets, the
// doubling's `keys` array is sizeof(char*) * 64 bytes - distinct from the initial build's
// 32-bucket keys array above.
#define _TEST_GROW_CAPACITY_BUCKETS    64
#define _TEST_GROW_KEYS_BYTES          (sizeof(char*) * _TEST_GROW_CAPACITY_BUCKETS)

static void _test_grow_decline_keeps_the_old_table_until_full(Test *const test) {
    test_case_begin(test, "a doubling that keeps failing keeps the OLD table; add refuses only once it is genuinely full");

    HashSet set = hashset_init_1(); // 32 buckets, per the build case above

    // 22 distinct inserts stay under the ~70% threshold on 32 buckets ((size+1)*10 <=
    // 32*7 == 224 through size 22), so the table has not grown yet - the SAME state
    // test_all.c's duplicate-at-threshold case reaches, on the same default capacity.
    for (USize i = 0; i < 22; i += 1) {
        char key[8] = DEFAULT_INITIALIZATION;

        sprintf(key, "g%zu", i);
        test_expect_u(test, "unarmed unique insert succeeds", 1, hashset_add_static(&set, key));
    }

    test_expect_u(test, "still 32 buckets before arming", _TEST_BUILD_CAPACITY_BUCKETS, hashset_get_capacity(&set));

    // Armed with ordinal 1: EVERY subsequent grow attempt at this size fails, so the table
    // can never actually double for the rest of this case.
    _test_fail_arm(_TEST_GROW_KEYS_BYTES, 1);

    // The 23rd through 32nd distinct keys each trigger a (failing) grow attempt, but the OLD
    // 32-bucket table still has free slots, so every one of these adds still SUCCEEDS.
    for (USize i = 22; i < 32; i += 1) {
        char key[8] = DEFAULT_INITIALIZATION;

        sprintf(key, "g%zu", i);
        test_expect_u(test, "insert still succeeds into the old table's free slots", 1, hashset_add_static(&set, key));
        test_expect_u(test, "capacity never moved - the grow keeps declining", _TEST_BUILD_CAPACITY_BUCKETS, hashset_get_capacity(&set));
    }

    test_expect_u(test, "the table is now genuinely full", _TEST_BUILD_CAPACITY_BUCKETS, hashset_size(&set));

    // The 33rd distinct key: size == capacity already, so THIS is where the documented
    // "refuses at size == capacity" decline fires - not one insert earlier.
    U32 const overflow_result = hashset_add_static(&set, "overflow");

    // The size == capacity refusal in _hashset_add fires identically for a BORROWED key -
    // tried here, STILL ARMED, since disarming would let this genuinely new key's grow
    // attempt succeed for real and the insert would go through, proving nothing about the
    // declined-grow path this case exists to cover.
    char *const borrowed_overflow = char_new_2("borrowed-overflow");
    U32 const borrowed_result = hashset_add(&set, borrowed_overflow);

    USize const fired = _fail_seen;

    _test_fail_disarm();

    test_expect_true(test, "the injection fired at least once", fired > 0);
    test_expect_u(test, "the 33rd distinct key is refused", 0, overflow_result);
    test_expect_u(test, "a borrow-path add on the same full table also refuses with 0", 0, borrowed_result);
    test_expect_u(test, "size stayed at 32, not 33 or 34", _TEST_BUILD_CAPACITY_BUCKETS, hashset_size(&set));
    test_expect_u(test, "capacity is still the old 32 buckets", _TEST_BUILD_CAPACITY_BUCKETS, hashset_get_capacity(&set));
    test_expect_false(test, "the refused static key was never stored", hashset_contains(&set, "overflow"));
    test_expect_false(test, "the refused borrowed key was never stored", hashset_contains(&set, "borrowed-overflow"));

    char_delete(borrowed_overflow);

    hashset_uninit(&set);

    test_case_end(test);
}

// The grow's other two bucket borrows, same 64-bucket doubling: `counts` (U32 * 64) is the
// SECOND borrow _hashset_grow makes, `owned` (bool * 64) the THIRD.
#define _TEST_GROW_COUNTS_BYTES (sizeof(U32) * _TEST_GROW_CAPACITY_BUCKETS)
#define _TEST_GROW_OWNED_BYTES  (sizeof(bool) * _TEST_GROW_CAPACITY_BUCKETS)

static void _test_grow_declines_on_counts_borrow_failure(Test *const test) {
    test_case_begin(test, "a grow whose SECOND borrow (counts) fails keeps the old table, releasing the fresh keys array");

    HashSet set = hashset_init_1(); // 32 buckets

    for (USize i = 0; i < 22; i += 1) {
        char key[8] = DEFAULT_INITIALIZATION;

        sprintf(key, "gc%zu", i);
        test_expect_u(test, "unarmed unique insert succeeds", 1, hashset_add_static(&set, key));
    }

    USize const old_capacity = hashset_get_capacity(&set);
    USize const grow_keys_live_before = _live_of_size(_TEST_GROW_KEYS_BYTES);

    _test_fail_arm(_TEST_GROW_COUNTS_BYTES, 1);

    // The 23rd distinct key triggers a grow whose counts borrow fails; the old table still
    // has a free slot, so the insert that triggered the (failing) grow still succeeds into it.
    U32 const added = hashset_add_static(&set, "gc22");

    USize const fired = _fail_seen;

    _test_fail_disarm();

    test_expect_u(test, "the injection fired", 1, fired);
    test_expect_u(test, "the insert still succeeded into the old table", 1, added);
    test_expect_u(test, "capacity never moved - the grow declined", old_capacity, hashset_get_capacity(&set));
    test_expect_u(test, "the fresh keys array was released, not leaked", grow_keys_live_before, _live_of_size(_TEST_GROW_KEYS_BYTES));

    hashset_uninit(&set);

    test_case_end(test);
}

static void _test_grow_declines_on_owned_borrow_failure(Test *const test) {
    test_case_begin(test, "a grow whose THIRD borrow (owned) fails keeps the old table, releasing the fresh keys and counts arrays");

    HashSet set = hashset_init_1(); // 32 buckets

    for (USize i = 0; i < 22; i += 1) {
        char key[8] = DEFAULT_INITIALIZATION;

        sprintf(key, "go%zu", i);
        test_expect_u(test, "unarmed unique insert succeeds", 1, hashset_add_static(&set, key));
    }

    USize const old_capacity = hashset_get_capacity(&set);
    USize const grow_keys_live_before = _live_of_size(_TEST_GROW_KEYS_BYTES);
    USize const grow_counts_live_before = _live_of_size(_TEST_GROW_COUNTS_BYTES);

    _test_fail_arm(_TEST_GROW_OWNED_BYTES, 1);

    U32 const added = hashset_add_static(&set, "go22");

    USize const fired = _fail_seen;

    _test_fail_disarm();

    test_expect_u(test, "the injection fired", 1, fired);
    test_expect_u(test, "the insert still succeeded into the old table", 1, added);
    test_expect_u(test, "capacity never moved - the grow declined", old_capacity, hashset_get_capacity(&set));
    test_expect_u(test, "the fresh keys array was released, not leaked", grow_keys_live_before, _live_of_size(_TEST_GROW_KEYS_BYTES));
    test_expect_u(test, "the fresh counts array was released, not leaked", grow_counts_live_before, _live_of_size(_TEST_GROW_COUNTS_BYTES));

    hashset_uninit(&set);

    test_case_end(test);
}

// The struct's own size, taken from the type rather than written as a number.
#define _TEST_STRUCT_BYTES sizeof(HashSet)

static void _test_new_1_declines_on_struct_borrow_failure(Test *const test) {
    test_case_begin(test, "new_1: a failed struct calloc answers nullptr, not an abort");

    HashSet *control = hashset_new_1();

    test_expect_not_null(test, "unarmed: new_1 allocates (anchor)", control);

    hashset_delete(&control);

    USize const live_before = _live_of_size(_TEST_STRUCT_BYTES);

    _test_fail_arm(_TEST_STRUCT_BYTES, 1);

    HashSet *const declined = hashset_new_1();

    USize const fired = _fail_seen;

    _test_fail_disarm();

    test_expect_u(test, "the injection fired", 1, fired);
    test_expect_null(test, "new_1 answers nullptr", declined);
    test_expect_u(test, "no struct-sized block was left live", live_before, _live_of_size(_TEST_STRUCT_BYTES));

    test_case_end(test);
}

// _hashset_new_check's release path (the struct itself, not just the bucket arrays) had no
// executable proof: a declined FIRST bucket borrow inside the build drives capacity to 0, and
// _hashset_new_check is supposed to free the struct on that path rather than hand back a live
// pointer to a dead set.
static void _test_new_1_release_path_frees_struct_on_build_failure(Test *const test) {
    test_case_begin(test, "new_1: a declined FIRST bucket borrow inside the build releases the struct too, per _hashset_new_check");

    USize const struct_live_before = _live_of_size(_TEST_STRUCT_BYTES);

    _test_fail_arm(_TEST_BUILD_KEYS_BYTES, 1);

    HashSet *const declined = hashset_new_1();

    USize const fired = _fail_seen;

    _test_fail_disarm();

    test_expect_u(test, "the injection fired", 1, fired);
    test_expect_null(test, "new_1 answers nullptr", declined);
    test_expect_u(test, "no struct-sized block was left live - _hashset_new_check released it", struct_live_before, _live_of_size(_TEST_STRUCT_BYTES));

    test_case_end(test);
}

// Same release-path proof on the ARENA side, but through allocator_try_borrow's genuine
// exhaustion rather than a wrapped calloc: an arena sized to hold the struct (plus a liveness
// probe) but nowhere near the 32-bucket keys array (_TEST_BUILD_KEYS_BYTES, 256 bytes on a
// 64-bit pointer) forces the SAME capacity-0 / release-the-struct path.
static void _test_alloc_new_1_declines_on_exhausted_arena(Test *const test) {
    test_case_begin(test, "alloc_new_1: an arena that holds the struct but not the first bucket array declines with nullptr");

    // A linear arena never reclaims a release (arena_linear_free is a no-op), so the probe's
    // bytes stay spent - the arena is sized to afford the probe AND the struct up front, then
    // nothing more: far short of the 256-byte keys array _hashset_build borrows next.
    USize const tiny_capacity = MEMORY_ALIGN_UP(1) + MEMORY_ALIGN_UP(_TEST_STRUCT_BYTES);

    Arena tiny = arena_init_2(tiny_capacity, 1, ARENA_TYPE_LINEAR);

    void *const probe = allocator_try_borrow(1, &tiny);

    test_expect_not_null(test, "a 1-byte probe succeeds: the arena is live", probe);

    // Anchor, so the decline below cannot pass for the wrong reason: on a twin arena of
    // the same size the struct borrow itself SUCCEEDS after the probe, so alloc_new_1's
    // nullptr can only come from _hashset_new_check releasing a built-but-empty set.
    Arena twin = arena_init_2(tiny_capacity, 1, ARENA_TYPE_LINEAR);

    void *const twin_probe = allocator_try_borrow(1, &twin);
    void *const twin_struct = allocator_try_borrow(_TEST_STRUCT_BYTES, &twin);

    test_expect_not_null(test, "anchor: the twin arena affords the probe", twin_probe);
    test_expect_not_null(test, "anchor: and the struct borrow after it - the decline is the release path", twin_struct);

    arena_uninit(&twin, ARENA_TYPE_LINEAR);

    HashSet *const declined = hashset_alloc_new_1(&tiny);

    test_expect_null(test, "alloc_new_1 declines: the struct fits but the bucket array does not", declined);

    void *const post_probe = allocator_try_borrow(1, &tiny);

    test_expect_null(test, "the arena is now genuinely exhausted - even a further 1-byte borrow declines", post_probe);

    arena_uninit(&tiny, ARENA_TYPE_LINEAR); // must uninit cleanly on an arena that never saw a real release

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

    Test test = test_init("tests/container/hashset/test_oom.c");

    test_suite_begin(&test, "hashset allocation-failure sweep");
    _test_init_2_declines_on_bucket_borrow_failure(&test);
    _test_init_2_declines_on_counts_borrow_failure(&test);
    _test_init_2_declines_on_owned_borrow_failure(&test);
    _test_add_static_key_copy_declines(&test);
    _test_grow_decline_keeps_the_old_table_until_full(&test);
    _test_grow_declines_on_counts_borrow_failure(&test);
    _test_grow_declines_on_owned_borrow_failure(&test);
    _test_new_1_declines_on_struct_borrow_failure(&test);
    _test_new_1_release_path_frees_struct_on_build_failure(&test);
    _test_alloc_new_1_declines_on_exhausted_arena(&test);
    _test_ledger_sane(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}