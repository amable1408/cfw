#include <stdio.h>

#include <arena/arena.h>
#include <container/slotmap/slotmap.h>
#include <memory/memory.h>
#include <test/test.h>

/*
 * Allocation-failure sweep for slotmap's borrow-decline paths under the calloc/free ledger.
 * Most cases target the HEAP paths that test_all.c's refused/exhausted-ARENA cases cannot
 * reach - a heap map has no arena to starve, so the only way to fail its single calloc is to
 * fail calloc itself. One case (d) targets the ARENA path instead, through a genuinely
 * exhausted arena rather than a wrapped calloc, because only the ledger here can prove the
 * declined bookkeeping borrow never surfaces as a heap free of an arena-interior pointer -
 * test_all.c has no ledger and cannot observe that distinction.
 *
 * slotmap's bookkeeping is ONE block (generations + occupied packed together, per
 * slotmap.c's _slotmap_build), so there is exactly one build-time borrow to arm, not the
 * multi-array sweep hashset/map need:
 *
 *   (a) slotmap_init's single bookkeeping borrow fails -> the build backs out, leaving
 *       capacity 0 and size 0; add then declines with 0, valid answers false.
 *   (b) slotmap_new's STRUCT borrow fails -> nullptr, not an abort.
 *   (c) slotmap_new's struct borrow succeeds but the bookkeeping borrow behind it fails ->
 *       _slotmap_new_check releases the struct and answers nullptr - the ledger proves the
 *       struct was actually freed, not merely handed back as a dead capacity-0 map.
 *   (d) slotmap_alloc_new's struct borrow succeeds on a live arena too small for the
 *       bookkeeping block behind it -> the same _slotmap_new_check release path, this time
 *       proven through genuine arena exhaustion: the ledger's foreign/double-free counters
 *       stay unchanged, showing the arena-interior struct never reaches the heap free path.
 *
 * HOW. Same seam as tests/container/hashset/test_oom.c and tests/container/map/test_oom.c:
 * -Wl,--wrap=calloc / --wrap=free. Arming is by SIZE: each case arms immediately before its
 * own call and disarms immediately after. Case (d) arms nothing - its decline comes from the
 * arena's own exhaustion, so it reads the ledger's foreign/double-free counters instead.
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

// slotmap.c's _slotmap_build borrows ONE block sized capacity * (sizeof(U16) + sizeof(bool))
// (generations and occupied packed together - see slotmap.c's `stride`). A capacity of 4
// keeps the byte count (4 * 3 == 12 on this ABI) unique in this binary, distinct from
// sizeof(SlotMap) below.
#define _TEST_BUILD_CAPACITY   4
#define _TEST_BUILD_BYTES      (_TEST_BUILD_CAPACITY * (sizeof(U16) + sizeof(bool)))

static void _test_init_declines_on_build_borrow_failure(Test *const test) {
    test_case_begin(test, "a failed bookkeeping borrow leaves capacity 0, size 0; add/valid decline");

    SlotMap control = slotmap_init(_TEST_BUILD_CAPACITY);

    test_expect_u(test, "unarmed: init builds (anchor)", (USize) _TEST_BUILD_CAPACITY, slotmap_get_capacity(&control));

    slotmap_uninit(&control);

    _test_fail_arm(_TEST_BUILD_BYTES, 1);

    SlotMap map = slotmap_init(_TEST_BUILD_CAPACITY);

    // Read before disarm, which zeroes the counter.
    USize const fired = _fail_seen;

    _test_fail_disarm();

    test_expect_u(test, "the injection fired", (USize) 1, fired);
    test_expect_u(test, "capacity is 0", (USize) 0, slotmap_get_capacity(&map));
    test_expect_u(test, "size is 0", (USize) 0, slotmap_get_size(&map));
    test_expect_true(test, "empty", slotmap_empty(&map));
    test_expect_u(test, "add declines on the dead map", (USize) 0, (USize) slotmap_add(&map));
    test_expect_false(test, "valid declines too", slotmap_valid(&map, (SlotMapHandle) 0x00010000));
    test_expect_u(test, "no bookkeeping block was left live", (USize) 0, _live_of_size(_TEST_BUILD_BYTES));

    slotmap_uninit(&map);

    test_case_end(test);
}

// The struct's own size, taken from the type rather than written as a number.
#define _TEST_STRUCT_BYTES sizeof(SlotMap)

static void _test_new_declines_on_struct_borrow_failure(Test *const test) {
    test_case_begin(test, "new: a failed struct calloc answers nullptr, not an abort");

    SlotMap *control = slotmap_new(_TEST_BUILD_CAPACITY);

    test_expect_not_null(test, "unarmed: new allocates (anchor)", control);

    slotmap_delete(&control);

    USize const live_before = _live_of_size(_TEST_STRUCT_BYTES);

    _test_fail_arm(_TEST_STRUCT_BYTES, 1);

    SlotMap *const declined = slotmap_new(_TEST_BUILD_CAPACITY);

    USize const fired = _fail_seen;

    _test_fail_disarm();

    test_expect_u(test, "the injection fired", (USize) 1, fired);
    test_expect_null(test, "new answers nullptr", declined);
    test_expect_u(test, "no struct-sized block was left live", live_before, _live_of_size(_TEST_STRUCT_BYTES));

    test_case_end(test);
}

// _slotmap_new_check's release path had no executable proof: a declined bookkeeping borrow
// inside the build drives capacity to 0, and _slotmap_new_check is supposed to free the
// struct on that path rather than hand back a live pointer to a dead map.
static void _test_new_release_path_frees_struct_on_build_failure(Test *const test) {
    test_case_begin(test, "new: a declined bookkeeping borrow inside the build releases the struct too");

    USize const struct_live_before = _live_of_size(_TEST_STRUCT_BYTES);

    _test_fail_arm(_TEST_BUILD_BYTES, 1);

    SlotMap *const declined = slotmap_new(_TEST_BUILD_CAPACITY);

    USize const fired = _fail_seen;

    _test_fail_disarm();

    test_expect_u(test, "the injection fired", (USize) 1, fired);
    test_expect_null(test, "new answers nullptr", declined);
    test_expect_u(test, "no struct-sized block was left live - _slotmap_new_check released it", struct_live_before, _live_of_size(_TEST_STRUCT_BYTES));

    test_case_end(test);
}

// Same release-path proof as (c), but through allocator_try_borrow's genuine arena
// exhaustion rather than a wrapped calloc: an arena sized to hold the struct (plus a
// liveness probe) but nowhere near the bookkeeping block forces the SAME capacity-0 /
// release-the-struct path, and this time the ledger can show the struct never lands in
// the heap free path (which would land it as a foreign free, since it was arena-borrowed).
#define _TEST_ARENA_CAPACITY 64

static void _test_alloc_new_declines_on_exhausted_arena(Test *const test) {
    test_case_begin(test, "alloc_new: an arena that holds the struct but not the bookkeeping block declines with nullptr");

    // A linear arena never reclaims a release (arena_linear_free is a no-op), so the probe's
    // bytes stay spent - the arena is sized to afford the probe AND the struct up front, then
    // nothing more: far short of the bookkeeping block _slotmap_build borrows next.
    USize const tiny_capacity = MEMORY_ALIGN_UP(1) + MEMORY_ALIGN_UP(_TEST_STRUCT_BYTES);

    Arena tiny = arena_init_2(tiny_capacity, 1, ARENA_TYPE_LINEAR);

    void *const probe = allocator_try_borrow(1, &tiny);

    test_expect_not_null(test, "a 1-byte probe succeeds: the arena is live", probe);

    // Anchor, so the decline below cannot pass for the wrong reason: on a twin arena of
    // the same size the struct borrow itself SUCCEEDS after the probe, so alloc_new's
    // nullptr can only come from _slotmap_new_check releasing a built-but-empty map.
    Arena twin = arena_init_2(tiny_capacity, 1, ARENA_TYPE_LINEAR);

    void *const twin_probe = allocator_try_borrow(1, &twin);
    void *const twin_struct = allocator_try_borrow(_TEST_STRUCT_BYTES, &twin);

    test_expect_not_null(test, "anchor: the twin arena affords the probe", twin_probe);
    test_expect_not_null(test, "anchor: and the struct borrow after it - the decline is the release path", twin_struct);

    arena_uninit(&twin, ARENA_TYPE_LINEAR);

    USize const foreign_before      = _foreign_free_count;
    USize const double_free_before  = _double_free_count;

    SlotMap *const declined = slotmap_alloc_new(_TEST_ARENA_CAPACITY, &tiny);

    test_expect_null(test, "alloc_new declines: the struct fits but the bookkeeping block does not", declined);
    test_expect_u(test, "no foreign free was recorded - the arena-interior struct never reached the heap free path", foreign_before, _foreign_free_count);
    test_expect_u(test, "no double free was recorded", double_free_before, _double_free_count);

    void *const post_probe = allocator_try_borrow(1, &tiny);

    test_expect_null(test, "the arena is now genuinely exhausted - even a further 1-byte borrow declines", post_probe);

    arena_uninit(&tiny, ARENA_TYPE_LINEAR); // must uninit cleanly on an arena that never saw a real release

    test_case_end(test);
}

static void _test_ledger_sane(Test *const test) {
    test_case_begin(test, "the ledger itself is trustworthy");

    test_expect_u(test, "no double free was observed", (USize) 0, _double_free_count);
    test_expect_u(test, "no free of an unrecorded block", (USize) 0, _foreign_free_count);
    test_expect_u(test, "the ledger never overflowed", (USize) 0, _ledger_overflow);

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

    Test test = test_init("tests/container/slotmap/test_oom.c");

    test_suite_begin(&test, "slotmap allocation-failure sweep");
    _test_init_declines_on_build_borrow_failure(&test);
    _test_new_declines_on_struct_borrow_failure(&test);
    _test_new_release_path_frees_struct_on_build_failure(&test);
    _test_alloc_new_declines_on_exhausted_arena(&test);
    _test_ledger_sane(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}