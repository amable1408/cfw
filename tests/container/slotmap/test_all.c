#include <stdio.h>

#include <arena/arena.h>
#include <container/slotmap/slotmap.h>
#include <log/log.h>
#include <test/test.h>

/* Suite for SlotMap, the bookkeeping-only generational slot allocator. What it pins is the
 * CONTRACT the header documents, not just this file's own inputs:
 *
 *   - HANDLE LAYOUT: low SLOTMAP_INDEX_BITS bits are the slot index, high
 *     SLOTMAP_GENERATION_BITS bits are the slot's generation at mint time; 0 is always
 *     invalid; a handle must never be used as a raw index.
 *   - DETERMINISTIC REUSE: the lowest-index free slot is always chosen next.
 *   - STALE-HANDLE REJECTION with an ABA bound: a slot's generation wraps after exactly
 *     U16_MAX (65535) releases of that one slot, skipping 0.
 *   - REFUSAL, NEVER ABORT, on a VALUE-dependent condition: a refused build (dead/exhausted
 *     arena) lands the map at capacity 0 with every operation inert; a never-built
 *     (DEFAULT_INITIALIZATION) map degrades the same way.
 *   - Mutating during a walk never skips or repeats an already-in-progress slot; an add
 *     during a walk is visited this pass iff its new index is above the current one.
 *
 * The prior revision of this suite spent 140,000 of its 140,046 assertions on a 70000-cycle
 * generation-wrap loop asserting inside every iteration - tautologies that could not fail for
 * the reason the loop existed. This revision replaces that with an exact boundary pin: invalid
 * at 65534 releases, valid again at exactly 65535. */

/*==============================================================================
 * MARK: - Cases
 *============================================================================*/

static void _test_handle_layout(Test *const test) {
    test_case_begin(test, "handle layout: index low bits, generation high bits; first handle is 0x00010000");

    SlotMap map = slotmap_init(4);

    SlotMapHandle const h0 = slotmap_add(&map);
    SlotMapHandle const h1 = slotmap_add(&map);

    test_expect_u(test, "the first-ever handle is exactly 0x00010000", (USize) 0x00010000, (USize) h0);
    test_expect_u(test, "generation bits of h0 decode to 1", (USize) 1, (USize) (h0 >> SLOTMAP_INDEX_BITS));
    test_expect_u(test, "index bits of h0 decode to 0", (USize) 0, (USize) (h0 & (((SlotMapHandle) 1 << SLOTMAP_INDEX_BITS) - 1)));
    test_expect_u(test, "index bits of h1 decode to 1", (USize) 1, (USize) (h1 & (((SlotMapHandle) 1 << SLOTMAP_INDEX_BITS) - 1)));

    // A handle is never usable as a raw index: h1's own numeric value is 0x00010001, not 1.
    test_expect_true(test, "h1's raw value is not its slot index", h1 != (SlotMapHandle) slotmap_index(&map, h1));

    slotmap_uninit(&map);

    test_case_end(test);
}

static void _test_add_dense_indices(Test *const test) {
    test_case_begin(test, "add mints dense indices from 0, distinct handles");

    SlotMap map = slotmap_init(4);

    SlotMapHandle const h0 = slotmap_add(&map);
    SlotMapHandle const h1 = slotmap_add(&map);
    SlotMapHandle const h2 = slotmap_add(&map);

    test_expect_u(test, "h0 index 0", (USize) 0, slotmap_index(&map, h0));
    test_expect_u(test, "h1 index 1", (USize) 1, slotmap_index(&map, h1));
    test_expect_u(test, "h2 index 2", (USize) 2, slotmap_index(&map, h2));
    test_expect_u(test, "count is 3", (USize) 3, slotmap_get_size(&map));

    slotmap_uninit(&map);

    test_case_end(test);
}

static void _test_remove_then_reuse_lowest(Test *const test) {
    test_case_begin(test, "remove then add reuses the lowest freed index, bumping its generation");

    SlotMap map = slotmap_init(4);

    SlotMapHandle const h0 = slotmap_add(&map);
    SlotMapHandle const h1 = slotmap_add(&map);
    SlotMapHandle const h2 = slotmap_add(&map);

    test_expect_true(test, "remove h0", slotmap_remove(&map, h0));

    SlotMapHandle const reused = slotmap_add(&map);

    test_expect_u(test, "reuse takes the freed index 0", (USize) 0, slotmap_index(&map, reused));
    test_expect_true(test, "the reused handle is not h0 (generation bumped)", reused != h0);
    test_expect_false(test, "the old h0 is stale", slotmap_valid(&map, h0));
    test_expect_true(test, "h1 is unaffected", slotmap_valid(&map, h1));
    test_expect_true(test, "h2 is unaffected", slotmap_valid(&map, h2));

    slotmap_uninit(&map);

    test_case_end(test);
}

static void _test_cursor_reset_with_gap(Test *const test) {
    test_case_begin(test, "cursor reset with a gap: add 0,1,2; remove 1; add -> 1; add -> 3");

    SlotMap map = slotmap_init(4);

    SlotMapHandle const h0 = slotmap_add(&map);
    SlotMapHandle const h1 = slotmap_add(&map);
    SlotMapHandle const h2 = slotmap_add(&map);

    test_expect_true(test, "remove h1", slotmap_remove(&map, h1));

    SlotMapHandle const fill_gap = slotmap_add(&map);

    test_expect_u(test, "the gap at index 1 is reused first", (USize) 1, slotmap_index(&map, fill_gap));

    SlotMapHandle const extend = slotmap_add(&map);

    test_expect_u(test, "the next add lands at the untouched index 3", (USize) 3, slotmap_index(&map, extend));
    test_expect_true(test, "h0 still valid throughout", slotmap_valid(&map, h0));
    test_expect_true(test, "h2 still valid throughout", slotmap_valid(&map, h2));

    slotmap_uninit(&map);

    test_case_end(test);
}

static void _test_handle_stability_after_removing_neighbours(Test *const test) {
    test_case_begin(test, "a handle's index is stable when its neighbours are removed");

    SlotMap map = slotmap_init(4);

    SlotMapHandle const h0 = slotmap_add(&map);
    SlotMapHandle const h1 = slotmap_add(&map);
    SlotMapHandle const h2 = slotmap_add(&map);

    test_expect_true(test, "remove h0 (left neighbour)", slotmap_remove(&map, h0));
    test_expect_true(test, "remove h2 (right neighbour)", slotmap_remove(&map, h2));

    test_expect_true(test, "h1 is still valid", slotmap_valid(&map, h1));
    test_expect_u(test, "h1's index did not move", (USize) 1, slotmap_index(&map, h1));

    slotmap_uninit(&map);

    test_case_end(test);
}

static void _test_remove_semantics(Test *const test) {
    test_case_begin(test, "remove: true once, false on double-remove, bad generation, bad index");

    SlotMap map = slotmap_init(4);

    SlotMapHandle const handle = slotmap_add(&map);

    test_expect_true(test, "first remove succeeds", slotmap_remove(&map, handle));
    test_expect_false(test, "double-remove is rejected", slotmap_remove(&map, handle));

    SlotMapHandle const other = slotmap_add(&map);
    SlotMapHandle const wrong_generation = other + ((SlotMapHandle) 1 << SLOTMAP_INDEX_BITS);

    test_expect_false(test, "a stale-generation handle is rejected", slotmap_remove(&map, wrong_generation));

    SlotMapHandle const out_of_range = (SlotMapHandle) slotmap_get_capacity(&map) | ((SlotMapHandle) 1 << SLOTMAP_INDEX_BITS);

    test_expect_false(test, "an out-of-range index is rejected", slotmap_remove(&map, out_of_range));
    test_expect_true(test, "the untouched handle is still valid", slotmap_valid(&map, other));

    slotmap_uninit(&map);

    test_case_end(test);
}

static void _test_valid_stale_reused_and_unreused(Test *const test) {
    test_case_begin(test, "valid: stale after reuse, stale (unreused), and never-occupied");

    SlotMap map = slotmap_init(4);

    SlotMapHandle const h0 = slotmap_add(&map);
    SlotMapHandle const h1 = slotmap_add(&map);

    test_expect_true(test, "remove h0", slotmap_remove(&map, h0));
    test_expect_true(test, "remove h1", slotmap_remove(&map, h1));

    SlotMapHandle const reused = slotmap_add(&map); // takes index 0, h1's slot (index 1) stays free

    test_expect_false(test, "h0 is stale - its slot was reused", slotmap_valid(&map, h0));
    test_expect_false(test, "h1 is stale - its slot is merely free, not reused", slotmap_valid(&map, h1));
    test_expect_true(test, "the reused handle is valid", slotmap_valid(&map, reused));

    // A handle for a slot that has never been occupied answers invalid even though its
    // generation (1) matches the seeded initial value - the occupied bit still gates it.
    SlotMapHandle const never_occupied = ((SlotMapHandle) 1 << SLOTMAP_INDEX_BITS) | 2;

    test_expect_false(test, "a never-occupied slot's would-be handle is invalid", slotmap_valid(&map, never_occupied));

    slotmap_uninit(&map);

    test_case_end(test);
}

static void _test_index_sentinel_on_invalid_shapes(Test *const test) {
    test_case_begin(test, "index() answers capacity (the sentinel) for every invalid handle shape");

    SlotMap map = slotmap_init(4);

    USize const capacity = slotmap_get_capacity(&map);

    SlotMapHandle const handle = slotmap_add(&map);

    test_expect_u(test, "index(0) is the sentinel", capacity, slotmap_index(&map, SLOTMAP_HANDLE_INVALID));

    SlotMapHandle const stale = handle + ((SlotMapHandle) 1 << SLOTMAP_INDEX_BITS);

    test_expect_u(test, "index(stale generation) is the sentinel", capacity, slotmap_index(&map, stale));

    SlotMapHandle const foreign = ((SlotMapHandle) 1 << SLOTMAP_INDEX_BITS) | 999;

    test_expect_u(test, "index(out-of-range index) is the sentinel", capacity, slotmap_index(&map, foreign));

    SlotMapHandle const never_occupied = ((SlotMapHandle) 1 << SLOTMAP_INDEX_BITS) | 2;

    test_expect_u(test, "index(never-occupied, in-range) is the sentinel", capacity, slotmap_index(&map, never_occupied));

    slotmap_uninit(&map);

    test_case_end(test);
}

static void _test_handle_at_occupied_empty_out_of_range(Test *const test) {
    test_case_begin(test, "handle_at: live handle when occupied, 0 when empty/out-of-range");

    SlotMap map = slotmap_init(3);

    SlotMapHandle const h0 = slotmap_add(&map);

    test_expect_u(test, "handle_at(occupied) round-trips the live handle", (USize) h0, (USize) slotmap_handle_at(&map, 0));
    test_expect_u(test, "handle_at(never-occupied, in-range) is 0", (USize) 0, (USize) slotmap_handle_at(&map, 1));
    test_expect_u(test, "handle_at(out-of-range) is 0", (USize) 0, (USize) slotmap_handle_at(&map, 3));

    test_expect_true(test, "remove h0", slotmap_remove(&map, h0));

    test_expect_u(test, "handle_at(just-removed) is 0", (USize) 0, (USize) slotmap_handle_at(&map, 0));

    slotmap_uninit(&map);

    test_case_end(test);
}

static void _test_occupied(Test *const test) {
    test_case_begin(test, "occupied: true only for a live slot, false out-of-range and after remove");

    SlotMap map = slotmap_init(3);

    SlotMapHandle const h0 = slotmap_add(&map);

    test_expect_true(test, "index 0 is occupied", slotmap_occupied(&map, 0));
    test_expect_false(test, "index 1 was never added", slotmap_occupied(&map, 1));
    test_expect_false(test, "index 3 is out of range", slotmap_occupied(&map, 3));

    slotmap_remove(&map, h0);

    test_expect_false(test, "index 0 is free again after remove", slotmap_occupied(&map, 0));

    slotmap_uninit(&map);

    test_case_end(test);
}

static void _test_first_next_full_walk(Test *const test) {
    test_case_begin(test, "first/next: ascending walk over occupied slots; capacity sentinel at both ends");

    SlotMap map = slotmap_init(6);

    USize const capacity = slotmap_get_capacity(&map);

    test_expect_u(test, "first() on a never-added map is the capacity sentinel", capacity, slotmap_first(&map));

    SlotMapHandle const h0 = slotmap_add(&map);
    SlotMapHandle const h1 = slotmap_add(&map);
    SlotMapHandle const h2 = slotmap_add(&map);
    SlotMapHandle const h3 = slotmap_add(&map);

    (void) h0;
    (void) h1;

    slotmap_remove(&map, h3); // occupied: 0, 1, 2

    USize visited[8]    = DEFAULT_INITIALIZATION;
    USize visited_count = 0;

    for (USize i = slotmap_first(&map); i < capacity; i = slotmap_next(&map, i)) {
        visited[visited_count] = i;
        visited_count         += 1;
    }

    test_expect_u(test, "3 slots visited, ascending", (USize) 3, visited_count);
    test_expect_u(test, "visited[0] == 0", (USize) 0, visited[0]);
    test_expect_u(test, "visited[1] == 1", (USize) 1, visited[1]);
    test_expect_u(test, "visited[2] == 2", (USize) 2, visited[2]);
    test_expect_u(test, "next(>= capacity) answers the same sentinel", capacity, slotmap_next(&map, capacity));

    slotmap_remove(&map, h2);

    test_expect_u(test, "next from an unoccupied index skips forward correctly", capacity, slotmap_next(&map, 2));

    slotmap_uninit(&map);

    test_case_end(test);
}

static void _test_walk_remove_current_and_ahead(Test *const test) {
    test_case_begin(test, "mutating during a walk: removing the current or a later slot never skips or repeats one already passed");

    SlotMap map = slotmap_init(5);

    SlotMapHandle const h0 = slotmap_add(&map);
    SlotMapHandle const h1 = slotmap_add(&map);
    SlotMapHandle const h2 = slotmap_add(&map);
    SlotMapHandle const h3 = slotmap_add(&map);

    (void) h1;
    (void) h3;

    // Remove the CURRENT slot (0) once the walk is standing on it.
    USize cursor = slotmap_first(&map);

    test_expect_u(test, "walk starts at 0", (USize) 0, cursor);
    test_expect_true(test, "remove the current slot", slotmap_remove(&map, h0));

    cursor = slotmap_next(&map, cursor);

    test_expect_u(test, "the walk advances past the just-removed current slot to 1", (USize) 1, cursor);

    // Remove a slot AHEAD of the walk (index 3) before the walk reaches it.
    test_expect_true(test, "remove a slot ahead of the cursor", slotmap_remove(&map, h3));

    cursor = slotmap_next(&map, cursor); // -> 2

    test_expect_u(test, "the walk lands on 2", (USize) 2, cursor);

    cursor = slotmap_next(&map, cursor); // index 3 was removed ahead - skipped, not visited

    USize const capacity = slotmap_get_capacity(&map);

    test_expect_u(test, "the removed-ahead slot 3 is skipped; the walk ends", capacity, cursor);
    test_expect_true(test, "h2 (not removed) stayed valid the whole time", slotmap_valid(&map, h2));

    slotmap_uninit(&map);

    test_case_end(test);
}

static void _test_walk_remove_behind_no_effect(Test *const test) {
    test_case_begin(test, "removing a slot already passed by the walk has no effect on what remains");

    SlotMap map = slotmap_init(4);

    SlotMapHandle const h0 = slotmap_add(&map);
    SlotMapHandle const h1 = slotmap_add(&map);
    SlotMapHandle const h2 = slotmap_add(&map);

    USize cursor = slotmap_first(&map); // 0

    cursor = slotmap_next(&map, cursor); // 1 - slot 0 already passed

    test_expect_true(test, "remove the already-passed slot 0", slotmap_remove(&map, h0));

    cursor = slotmap_next(&map, cursor); // -> 2, unaffected by the behind-the-cursor remove

    test_expect_u(test, "the walk still reaches 2 normally", (USize) 2, cursor);
    test_expect_true(test, "h1 and h2 were never touched", slotmap_valid(&map, h1) && slotmap_valid(&map, h2));

    slotmap_uninit(&map);

    test_case_end(test);
}

static void _test_walk_add_below_cursor_not_revisited(Test *const test) {
    test_case_begin(test, "add during a walk landing BELOW the current index is not revisited this pass");

    SlotMap map = slotmap_init(6);

    SlotMapHandle const h0 = slotmap_add(&map);

    slotmap_add(&map); // index 1
    slotmap_add(&map); // index 2

    test_expect_true(test, "remove index 0, freeing the lowest slot", slotmap_remove(&map, h0));

    USize cursor = slotmap_first(&map); // 1 - the walk already started above the freed slot 0

    test_expect_u(test, "walk starts at 1", (USize) 1, cursor);

    slotmap_add(&map); // reuses index 0 - BELOW the current cursor (1)

    USize visited_count = 1; // index 1 already counted above

    while ((cursor = slotmap_next(&map, cursor)) < slotmap_get_capacity(&map)) {
        visited_count += 1;
    }

    test_expect_u(test, "only 2 slots visited this pass (1 and 2) - index 0 not revisited", (USize) 2, visited_count);
    test_expect_u(test, "the map nonetheless holds 3 occupied slots now", (USize) 3, slotmap_get_size(&map));

    slotmap_uninit(&map);

    test_case_end(test);
}

static void _test_walk_add_above_cursor_revisited(Test *const test) {
    test_case_begin(test, "add during a walk landing ABOVE the current index IS visited this pass");

    SlotMap map = slotmap_init(5);

    SlotMapHandle const h0 = slotmap_add(&map);
    SlotMapHandle const h1 = slotmap_add(&map);

    (void) h0;

    test_expect_true(test, "remove index 1, freeing a slot above the walk's start", slotmap_remove(&map, h1));

    USize cursor = slotmap_first(&map); // 0

    test_expect_u(test, "walk starts at 0", (USize) 0, cursor);

    slotmap_add(&map); // reuses index 1 - ABOVE the current cursor (0)

    cursor = slotmap_next(&map, cursor);

    test_expect_u(test, "the newly reused index 1 IS visited this pass", (USize) 1, cursor);

    slotmap_uninit(&map);

    test_case_end(test);
}

#define _TEST_ABA_CYCLE_COUNT U16_MAX // generation wraps (skipping 0) after exactly this many releases of one slot

static void _test_generation_wrap_aba_boundary(Test *const test) {
    test_case_begin(test, "ABA bound: a handle re-validates after EXACTLY 65535 releases of its slot - stale at 65534");

    SlotMap map = slotmap_init(1);

    SlotMapHandle const original = slotmap_add(&map); // index 0, generation 1

    test_expect_u(test, "the original handle mints at generation 1", (USize) 1, (USize) (original >> SLOTMAP_INDEX_BITS));

    SlotMapHandle current           = original;
    bool          stale_at_boundary = false;

    for (USize i = 1; i <= _TEST_ABA_CYCLE_COUNT; i += 1) {
        slotmap_remove(&map, current);
        current = slotmap_add(&map);

        if (i == _TEST_ABA_CYCLE_COUNT - 1) {
            stale_at_boundary = !slotmap_valid(&map, original);
        }
    }

    test_expect_true(test, "still stale one release short of the wrap (65534 releases)", stale_at_boundary);
    test_expect_true(test, "valid again at EXACTLY 65535 releases", slotmap_valid(&map, original));
    test_expect_u(test, "the wrapped-around handle equals the original bit-for-bit", (USize) original, (USize) current);

    slotmap_uninit(&map);

    test_case_end(test);
}

static void _test_generation_wrap_per_slot_capacity_two(Test *const test) {
    test_case_begin(test, "generation wrap is PER-SLOT: cycling slot 1 to its wrap point leaves slot 0 untouched");

    SlotMap map = slotmap_init(2);

    SlotMapHandle const anchor = slotmap_add(&map); // index 0, generation 1 - never touched below
    SlotMapHandle       cursor = slotmap_add(&map); // index 1, generation 1

    for (USize i = 1; i <= _TEST_ABA_CYCLE_COUNT; i += 1) {
        slotmap_remove(&map, cursor);
        cursor = slotmap_add(&map); // index 0 stays occupied, so this always reuses index 1
    }

    test_expect_u(test, "slot 1's generation wrapped back to 1", (USize) 1, (USize) (slotmap_handle_at(&map, 1) >> SLOTMAP_INDEX_BITS));
    test_expect_u(test, "slot 0's generation was never touched", (USize) 1, (USize) (slotmap_handle_at(&map, 0) >> SLOTMAP_INDEX_BITS));
    test_expect_true(test, "the untouched anchor handle is still valid", slotmap_valid(&map, anchor));
    test_expect_u(test, "slot 1's wrapped handle equals its original value", (USize) 0x00010001, (USize) cursor);

    slotmap_uninit(&map);

    test_case_end(test);
}

static void _test_get_size_zero_after_removes(Test *const test) {
    test_case_begin(test, "get_size returns to 0 once every occupied slot is removed");

    SlotMap map = slotmap_init(3);

    SlotMapHandle const h0 = slotmap_add(&map);
    SlotMapHandle const h1 = slotmap_add(&map);
    SlotMapHandle const h2 = slotmap_add(&map);

    slotmap_remove(&map, h0);
    slotmap_remove(&map, h1);
    slotmap_remove(&map, h2);

    test_expect_u(test, "size is 0", (USize) 0, slotmap_get_size(&map));
    test_expect_true(test, "empty is true", slotmap_empty(&map));

    slotmap_uninit(&map);

    test_case_end(test);
}

static void _test_clear_retires_every_handle(Test *const test) {
    test_case_begin(test, "clear retires every handle by bumping generations, not by re-initializing");

    SlotMap map = slotmap_init(3);

    SlotMapHandle const h0 = slotmap_add(&map);
    SlotMapHandle const h1 = slotmap_add(&map);

    USize const capacity_before = slotmap_get_capacity(&map);

    slotmap_clear(&map);

    test_expect_u(test, "size is 0 after clear", (USize) 0, slotmap_get_size(&map));
    test_expect_u(test, "capacity is unchanged by clear", capacity_before, slotmap_get_capacity(&map));
    test_expect_false(test, "h0 is now stale", slotmap_valid(&map, h0));
    test_expect_false(test, "h1 is now stale", slotmap_valid(&map, h1));

    SlotMapHandle const after_clear = slotmap_add(&map);

    test_expect_u(test, "the fresh add reuses index 0", (USize) 0, slotmap_index(&map, after_clear));
    test_expect_u(test, "its generation is bumped (2), not re-minted at the init value (1)",
        (USize) 2, (USize) (after_clear >> SLOTMAP_INDEX_BITS));

    slotmap_uninit(&map);

    test_case_end(test);
}

static void _test_empty_and_full(Test *const test) {
    test_case_begin(test, "empty/full track occupancy against capacity");

    SlotMap map = slotmap_init(2);

    test_expect_true(test, "a fresh map is empty", slotmap_empty(&map));
    test_expect_false(test, "a fresh map is not full", slotmap_full(&map));

    SlotMapHandle const h0 = slotmap_add(&map);

    test_expect_false(test, "one slot in: not empty, not full", slotmap_empty(&map) || slotmap_full(&map));

    slotmap_add(&map);

    test_expect_true(test, "at capacity: full", slotmap_full(&map));
    test_expect_false(test, "at capacity: not empty", slotmap_empty(&map));

    slotmap_remove(&map, h0);

    test_expect_false(test, "one slot freed: not full again", slotmap_full(&map));

    slotmap_uninit(&map);

    test_case_end(test);
}

static void _test_add_on_full_map(Test *const test) {
    test_case_begin(test, "add on a full map returns 0; add_2 writes capacity as the out-param index");

    SlotMap map = slotmap_init(2);

    slotmap_add(&map);
    slotmap_add(&map);

    test_expect_true(test, "the map is now full", slotmap_full(&map));

    SlotMapHandle const overflow = slotmap_add(&map);

    test_expect_u(test, "add on a full map returns 0", (USize) 0, (USize) overflow);
    test_expect_u(test, "size unchanged by the decline", (USize) 2, slotmap_get_size(&map));

    USize                index          = 999;
    SlotMapHandle const  overflow_2     = slotmap_add_2(&map, &index);
    USize const          capacity       = slotmap_get_capacity(&map);

    test_expect_u(test, "add_2 on a full map also returns 0", (USize) 0, (USize) overflow_2);
    test_expect_u(test, "add_2's out-param index is the capacity sentinel when declined", capacity, index);

    slotmap_uninit(&map);

    test_case_end(test);
}

static void _test_max_capacity_build(Test *const test) {
    test_case_begin(test, "a SLOTMAP_CAPACITY_MAX build works: add and remove at least one slot");

    SlotMap map = slotmap_init(SLOTMAP_CAPACITY_MAX);

    test_expect_u(test, "capacity is the max", (USize) SLOTMAP_CAPACITY_MAX, slotmap_get_capacity(&map));

    SlotMapHandle const handle = slotmap_add(&map);

    test_expect_true(test, "add succeeds on the max-capacity build", handle != SLOTMAP_HANDLE_INVALID);
    test_expect_u(test, "it lands at index 0", (USize) 0, slotmap_index(&map, handle));
    test_expect_true(test, "remove succeeds too", slotmap_remove(&map, handle));

    slotmap_uninit(&map);

    test_case_end(test);
}

static void _test_double_uninit_safe(Test *const test) {
    test_case_begin(test, "uninit is idempotent: safe to call twice, leaves the map inert");

    SlotMap map = slotmap_init(2);

    slotmap_add(&map);

    test_expect_u(test, "one slot stored before uninit", (USize) 1, slotmap_get_size(&map));

    slotmap_uninit(&map);

    test_expect_u(test, "capacity 0 after uninit", (USize) 0, slotmap_get_capacity(&map));

    slotmap_uninit(&map); // double uninit must be safe, not a double free

    test_expect_u(test, "still capacity 0 after a second uninit", (USize) 0, slotmap_get_capacity(&map));
    test_expect_u(test, "add on the dead map declines", (USize) 0, (USize) slotmap_add(&map));

    test_case_end(test);
}

static void _test_never_built_map_is_inert(Test *const test) {
    test_case_begin(test, "every read on a never-built (DEFAULT_INITIALIZATION) map is inert");

    SlotMap map = DEFAULT_INITIALIZATION;

    test_expect_u(test, "capacity is 0", (USize) 0, slotmap_get_capacity(&map));
    test_expect_u(test, "size is 0", (USize) 0, slotmap_get_size(&map));
    test_expect_true(test, "empty is true", slotmap_empty(&map));
    test_expect_true(test, "full is true (0 == 0)", slotmap_full(&map));
    test_expect_u(test, "add declines", (USize) 0, (USize) slotmap_add(&map));

    USize index = 999;

    test_expect_u(test, "add_2 declines too", (USize) 0, (USize) slotmap_add_2(&map, &index));
    test_expect_u(test, "add_2's index out-param is 0 (== capacity)", (USize) 0, index);
    test_expect_false(test, "valid is false for any handle", slotmap_valid(&map, (SlotMapHandle) 0x00010000));
    test_expect_false(test, "remove is false", slotmap_remove(&map, (SlotMapHandle) 0x00010000));
    test_expect_false(test, "occupied is false", slotmap_occupied(&map, 0));
    test_expect_u(test, "handle_at is 0", (USize) 0, (USize) slotmap_handle_at(&map, 0));
    test_expect_u(test, "first is the capacity sentinel (0)", (USize) 0, slotmap_first(&map));
    test_expect_u(test, "next is the capacity sentinel (0)", (USize) 0, slotmap_next(&map, 0));

    slotmap_clear(&map); // must not crash on null bookkeeping arrays

    test_expect_u(test, "clear is a no-op: size stays 0", (USize) 0, slotmap_get_size(&map));

    slotmap_uninit(&map); // must not crash either

    test_expect_u(test, "uninit on a never-built map is safe: capacity stays 0", (USize) 0, slotmap_get_capacity(&map));

    test_case_end(test);
}

static void _test_refused_arena_capacity_zero_inert(Test *const test) {
    test_case_begin(test, "a refused (dead) arena builds a capacity-0 map; every op is inert");

    Arena dead = arena_init_2(0, 8, ARENA_TYPE_LINEAR);
    SlotMap map = slotmap_alloc_init(4, &dead);

    test_expect_u(test, "capacity is 0", (USize) 0, slotmap_get_capacity(&map));
    test_expect_true(test, "empty", slotmap_empty(&map));
    test_expect_u(test, "add declines", (USize) 0, (USize) slotmap_add(&map));
    test_expect_false(test, "valid declines", slotmap_valid(&map, (SlotMapHandle) 0x00010000));

    slotmap_uninit(&map);
    arena_uninit(&dead, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_live_but_small_arena_declines(Test *const test) {
    test_case_begin(test, "a LIVE but too-small arena declines the build too");

    /* Same replication template as the hashset/map suites: an arena whose total capacity is
     * exactly MEMORY_ALIGNMENT holds far less than the bookkeeping block any requested
     * capacity needs, so the build declines - but the 1-byte probe proves the arena is LIVE,
     * not a dead null-handler, before the map is even built. */
    Arena tiny = arena_init_2(MEMORY_ALIGNMENT, 1, ARENA_TYPE_LINEAR);

    void *const probe = allocator_try_borrow(1, &tiny);

    test_expect_not_null(test, "a 1-byte probe succeeds: the arena is live", probe);

    allocator_release(probe, &tiny);

    SlotMap map = slotmap_alloc_init(64, &tiny);

    test_expect_u(test, "capacity is 0 - the arena could not hold the bookkeeping block", (USize) 0, slotmap_get_capacity(&map));
    test_expect_u(test, "add declines", (USize) 0, (USize) slotmap_add(&map));

    slotmap_uninit(&map);
    arena_uninit(&tiny, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_alloc_new_on_refused_arena_nullptr(Test *const test) {
    test_case_begin(test, "alloc_new on a refused arena answers nullptr, not a dead pointer");

    Arena dead = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    SlotMap *const declined = slotmap_alloc_new(4, &dead);

    test_expect_null(test, "alloc_new declines", declined);

    arena_uninit(&dead, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

// The release path a dead arena never reaches: the struct borrow SUCCEEDS and the
// bookkeeping block is what declines, so _slotmap_new_check has to give the
// struct back to the arena it came from (captured before uninit zeroes the
// field) and answer nullptr. Under ASan this is the case that would report a
// release through the wrong allocator.
static void _test_alloc_new_on_live_but_small_arena_nullptr(Test *const test) {
    test_case_begin(test, "alloc_new on an arena that fits the struct but not the bookkeeping answers nullptr");

    // Room for a 1-byte liveness probe and one struct, then nothing: a 60000-slot
    // bookkeeping block (180000 bytes) cannot follow.
    USize const small_capacity = MEMORY_ALIGN_UP(1) + MEMORY_ALIGN_UP(sizeof(SlotMap));

    Arena small = arena_init_2(small_capacity, 1, ARENA_TYPE_LINEAR);

    void *const probe = allocator_try_borrow(1, &small);

    test_expect_not_null(test, "a 1-byte probe succeeds: the arena is live", probe);

    // Anchor on a twin arena of the same size: the struct borrow itself succeeds
    // after the probe, so the decline below is the bookkeeping block, not the struct.
    Arena twin = arena_init_2(small_capacity, 1, ARENA_TYPE_LINEAR);

    void *const twin_probe = allocator_try_borrow(1, &twin);
    void *const twin_struct = allocator_try_borrow(sizeof(SlotMap), &twin);

    test_expect_not_null(test, "anchor: the twin arena affords the probe", twin_probe);
    test_expect_not_null(test, "anchor: and the struct borrow after it", twin_struct);

    arena_uninit(&twin, ARENA_TYPE_LINEAR);

    SlotMap *const declined = slotmap_alloc_new(60000, &small);

    test_expect_null(test, "alloc_new declines: the struct fit but the block did not", declined);

    arena_uninit(&small, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_new_and_delete_round_trip(Test *const test) {
    test_case_begin(test, "new / delete round trip on the heap");

    SlotMap *map = slotmap_new(4);

    test_expect_not_null(test, "new allocated", map);
    test_expect_true(test, "empty", slotmap_empty(map));

    SlotMapHandle const handle = slotmap_add(map);

    test_expect_true(test, "add via the pointer", handle != SLOTMAP_HANDLE_INVALID);

    slotmap_delete(&map);

    test_expect_null(test, "delete nulled the caller's pointer", map);

    test_case_end(test);
}

static void _test_alloc_new_and_delete_round_trip(Test *const test) {
    test_case_begin(test, "alloc_new / delete round trip on an arena");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    SlotMap *map = slotmap_alloc_new(4, &arena);

    test_expect_not_null(test, "alloc_new allocated", map);
    test_expect_true(test, "empty", slotmap_empty(map));

    SlotMapHandle const handle = slotmap_add(map);

    test_expect_true(test, "add via the pointer", handle != SLOTMAP_HANDLE_INVALID);

    slotmap_delete(&map);

    test_expect_null(test, "delete nulled the caller's pointer", map);

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Entry Point
 *============================================================================*/

int main(void) {
    LogConfig const log_config = {
        .level             = LOG_LEVEL_ERROR,
        .stream            = stdout,
        .timestamp_enabled = true,
        .autoflush         = true
    };

    log_init(log_config);

    Test test = test_init("tests/container/slotmap/test_all.c");

    test_suite_begin(&test, "slotmap");
    _test_handle_layout(&test);
    _test_add_dense_indices(&test);
    _test_remove_then_reuse_lowest(&test);
    _test_cursor_reset_with_gap(&test);
    _test_handle_stability_after_removing_neighbours(&test);
    _test_remove_semantics(&test);
    _test_valid_stale_reused_and_unreused(&test);
    _test_index_sentinel_on_invalid_shapes(&test);
    _test_handle_at_occupied_empty_out_of_range(&test);
    _test_occupied(&test);
    _test_first_next_full_walk(&test);
    _test_walk_remove_current_and_ahead(&test);
    _test_walk_remove_behind_no_effect(&test);
    _test_walk_add_below_cursor_not_revisited(&test);
    _test_walk_add_above_cursor_revisited(&test);
    _test_generation_wrap_aba_boundary(&test);
    _test_generation_wrap_per_slot_capacity_two(&test);
    _test_get_size_zero_after_removes(&test);
    _test_clear_retires_every_handle(&test);
    _test_empty_and_full(&test);
    _test_add_on_full_map(&test);
    _test_max_capacity_build(&test);
    _test_double_uninit_safe(&test);
    _test_never_built_map_is_inert(&test);
    _test_refused_arena_capacity_zero_inert(&test);
    _test_live_but_small_arena_declines(&test);
    _test_alloc_new_on_refused_arena_nullptr(&test);
    _test_alloc_new_on_live_but_small_arena_nullptr(&test);
    _test_new_and_delete_round_trip(&test);
    _test_alloc_new_and_delete_round_trip(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}