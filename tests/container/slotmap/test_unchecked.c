#include <stdio.h>

#include <arena/arena.h>
#include <container/slotmap/slotmap.h>
#include <log/log.h>
#include <test/test.h>

/*
 * Behavioral tests for include/container/slotmap/slotmap.c built WITHOUT
 * ERROR_CHECK_ENABLED.
 *
 * Every error_check in this module guards either a null pointer or the capacity ARGUMENT
 * to init/alloc_init/new/alloc_new - a violated contract there is undefined behavior with
 * the checks compiled out, not something this suite can observe, and this file deliberately
 * never passes capacity 0 or capacity > SLOTMAP_CAPACITY_MAX directly: that would exercise a
 * violated contract, not a refusal.
 *
 * What survives the define is the module's OTHER class of condition - the VALUE-dependent
 * refusals coded as plain runtime `if` statements, never error_check: a refused/exhausted
 * allocator still lands a build at capacity 0 with every operation inert; a bad handle
 * (zero, stale generation, out-of-range index, never-occupied slot) is still rejected by
 * slotmap_valid/slotmap_remove/slotmap_index's own comparisons; a full map's add still
 * declines with 0. Built by the makefile's `main_unchecked.exe` target into a separate
 * object namespace (.slotmapunchecked.o) so it never shares objects with the checked
 * build.
 */

int main(void) {
    LogConfig const log_config = {
        .level             = LOG_LEVEL_ERROR,
        .stream            = stdout,
        .timestamp_enabled = true,
        .autoflush         = true
    };

    log_init(log_config);

    Test test = test_init("tests/container/slotmap/test_unchecked.c");

    test_suite_begin(&test, "slotmap (unchecked)");
    test_case_begin(&test, "a refused (dead) arena still lands capacity 0, with every op still inert");

    Arena dead = arena_init_2(0, 8, ARENA_TYPE_LINEAR);
    SlotMap refused = slotmap_alloc_init(4, &dead);

    test_expect_u(&test, "capacity is 0", (USize) 0, slotmap_get_capacity(&refused));
    test_expect_u(&test, "size is 0", (USize) 0, slotmap_get_size(&refused));
    test_expect_true(&test, "empty is true", slotmap_empty(&refused));
    test_expect_true(&test, "full is true (0 == 0)", slotmap_full(&refused));
    test_expect_u(&test, "add declines", (USize) 0, (USize) slotmap_add(&refused));
    test_expect_false(&test, "valid declines", slotmap_valid(&refused, (SlotMapHandle) 0x00010000));
    test_expect_false(&test, "remove declines", slotmap_remove(&refused, (SlotMapHandle) 0x00010000));
    test_expect_false(&test, "occupied declines", slotmap_occupied(&refused, 0));
    test_expect_u(&test, "handle_at is 0", (USize) 0, (USize) slotmap_handle_at(&refused, 0));
    test_expect_u(&test, "first is the capacity sentinel (0)", (USize) 0, slotmap_first(&refused));
    test_expect_u(&test, "next is the capacity sentinel (0)", (USize) 0, slotmap_next(&refused, 0));

    slotmap_uninit(&refused);
    arena_uninit(&dead, ARENA_TYPE_LINEAR);

    test_case_end(&test);

    test_case_begin(&test, "a never-built (DEFAULT_INITIALIZATION) map is inert - never even calls a constructor");

    SlotMap never_built = DEFAULT_INITIALIZATION;

    test_expect_u(&test, "capacity is 0", (USize) 0, slotmap_get_capacity(&never_built));
    test_expect_u(&test, "add declines", (USize) 0, (USize) slotmap_add(&never_built));
    test_expect_false(&test, "valid declines", slotmap_valid(&never_built, (SlotMapHandle) 0x00010000));
    test_expect_u(&test, "index of any handle is the capacity sentinel (0)", (USize) 0, slotmap_index(&never_built, (SlotMapHandle) 0x00010000));

    slotmap_uninit(&never_built); // must not crash on null bookkeeping arrays

    test_case_end(&test);

    test_case_begin(&test, "bad handles are still rejected by plain comparisons, not a compiled-out check");

    SlotMap map = slotmap_init(4);

    SlotMapHandle const handle = slotmap_add(&map);
    SlotMapHandle const stale_generation = handle + ((SlotMapHandle) 1 << SLOTMAP_INDEX_BITS);
    SlotMapHandle const out_of_range = (SlotMapHandle) slotmap_get_capacity(&map) | ((SlotMapHandle) 1 << SLOTMAP_INDEX_BITS);
    USize const capacity = slotmap_get_capacity(&map);

    test_expect_false(&test, "the zero handle is invalid", slotmap_valid(&map, SLOTMAP_HANDLE_INVALID));
    test_expect_false(&test, "a stale-generation handle is invalid", slotmap_valid(&map, stale_generation));
    test_expect_false(&test, "an out-of-range index is invalid", slotmap_valid(&map, out_of_range));
    test_expect_u(&test, "index() answers the capacity sentinel for a stale handle", capacity, slotmap_index(&map, stale_generation));
    test_expect_false(&test, "remove(stale) is rejected", slotmap_remove(&map, stale_generation));
    test_expect_true(&test, "the real handle is still valid throughout", slotmap_valid(&map, handle));

    slotmap_uninit(&map);

    test_case_end(&test);

    test_case_begin(&test, "add on a full map declines with 0, still a plain runtime branch");

    SlotMap full_map = slotmap_init(2);

    slotmap_add(&full_map);
    slotmap_add(&full_map);

    test_expect_true(&test, "the map is now full", slotmap_full(&full_map));
    test_expect_u(&test, "one more add declines", (USize) 0, (USize) slotmap_add(&full_map));
    test_expect_u(&test, "size stayed at capacity", slotmap_get_capacity(&full_map), slotmap_get_size(&full_map));

    slotmap_uninit(&full_map);

    test_case_end(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}