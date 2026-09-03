#include <test/test.h>

#include <container/arrayList/al_u8.h>
#include <arena/arena.h>

/* Coverage for AL_U8, the byte instantiation of the numeric al_* family. Two
 * things are unique to al_u8.c (and al_bool) among the numeric siblings and get
 * pinned here rather than assumed from al_u64's suite:
 *
 *   - set_size CLAMPS to capacity (ruling 2026-08-23) instead of trusting the
 *     caller. `init_2(4); set_size(1 << 26); add_last(x);` used to write past a
 *     4-byte block through the public API alone; the clamp is what makes that
 *     show up as a short list instead of a wild write.
 *   - at() carries a SECOND bound against capacity, kept only in this file and
 *     al_bool as a cheap tripwire in case the set_size clamp ever regresses.
 *     Both bounds abort under ERROR_CHECK_ENABLED via error_check_out_of_bound_uint;
 *     test.h has no death-test harness (no fork/signal machinery), so - exactly
 *     like every sibling suite in this tree - the abort branch itself is not
 *     exercised here. Only the in-bounds path and the value-dependent guards
 *     (the clamp, the refusals) are provable without killing the test process. */

static void _test_basic(Test *const test) {
    test_case_begin(test, "add_last / add_first / at / get_data / remove / clear");

    AL_U8 list = al_u8_init_1();

    test_expect_true(test, "starts empty", al_u8_empty(&list));

    al_u8_add_last(&list, 10);
    al_u8_add_last(&list, 20);
    al_u8_add_first(&list, 5);

    test_expect_u(test, "size 3", 3, al_u8_get_size(&list));
    test_expect_u(test, "at 0 == 5", 5, *al_u8_at(&list, 0));
    test_expect_u(test, "at 1 == 10", 10, *al_u8_at(&list, 1));
    test_expect_u(test, "at 2 == 20", 20, *al_u8_at(&list, 2));
    test_expect_true(test, "get_data points at slot 0", al_u8_get_data(&list) == al_u8_at(&list, 0));

    al_u8_remove(&list, 1);

    test_expect_u(test, "size 2 after remove", 2, al_u8_get_size(&list));
    test_expect_u(test, "at 1 shifted to 20", 20, *al_u8_at(&list, 1));

    al_u8_clear(&list);

    test_expect_u(test, "size 0 after clear", 0, al_u8_get_size(&list));
    test_expect_true(test, "empty after clear", al_u8_empty(&list));

    al_u8_uninit(&list);

    test_case_end(test);
}

static void _test_at_in_bounds(Test *const test) {
    test_case_begin(test, "at() is bounded by size AND by capacity (in-bounds path)");

    AL_U8 list = al_u8_init_2(4);

    al_u8_add_last(&list, 1);
    al_u8_add_last(&list, 2);

    /* size == 2 < capacity == 4: both of at()'s guards - `index >= size` and
     * `index >= capacity` - pass for every index reached here. */
    test_expect_u(test, "at 0", 1, *al_u8_at(&list, 0));
    test_expect_u(test, "at 1", 2, *al_u8_at(&list, 1));

    al_u8_uninit(&list);

    test_case_end(test);
}

static void _test_set_size_clamps_to_capacity(Test *const test) {
    test_case_begin(test, "set_size clamps to capacity instead of trusting the caller");

    AL_U8 list = al_u8_init_1();

    al_u8_reserve(&list, 4);
    al_u8_set_size(&list, 1 << 20);

    test_expect_u(test, "size clamped to the 4-slot capacity", 4, al_u8_get_size(&list));

    al_u8_set_size(&list, 2);

    test_expect_u(test, "a size within capacity is not touched by the clamp", 2, al_u8_get_size(&list));

    al_u8_uninit(&list);

    test_case_end(test);

    test_case_begin(test, "set_size after a declined reserve clamps to the capacity the decline left behind");

    /* sizeof(U8) is 1, so `capacity * sizeof(U8)` can never wrap - the byte-size
     * overflow al_u64's suite uses to force a decline is unreachable for this
     * element type. A refused ARENA is the only way left to make reserve
     * decline: allocator_borrow hands back null, and the guard puts capacity
     * back to what it was before the request (0 here). */
    Arena declining = arena_init_2(0, 8, ARENA_TYPE_LINEAR);
    AL_U8 declined  = al_u8_alloc_init_1(&declining);

    al_u8_reserve(&declined, 4);

    test_expect_u(test, "capacity unchanged by the refusal", 0, al_u8_get_capacity(&declined));

    al_u8_set_size(&declined, 5);

    test_expect_u(test, "size clamped to the still-zero capacity", 0, al_u8_get_size(&declined));

    al_u8_uninit(&declined);

    test_case_end(test);
}

static void _test_init_3_hardened_bound(Test *const test) {
    test_case_begin(test, "init_3 / alloc_init_3 copy loop is bounded by BOTH capacity and data_size");

    U8 data[3] = { 7, 8, 9 };

    /* The ordinary path: init_2 sets capacity == data_size, so this exercises the
     * `i < data_size` side of the bound (all 3 elements are in range on both
     * counts) and confirms get_size reports exactly what was copied. */
    AL_U8 normal = al_u8_init_3(data, 3);

    test_expect_u(test, "capacity == data_size", 3, al_u8_get_capacity(&normal));
    test_expect_u(test, "size reflects every copied element", 3, al_u8_get_size(&normal));
    test_expect_u(test, "at 0", 7, *al_u8_at(&normal, 0));
    test_expect_u(test, "at 2", 9, *al_u8_at(&normal, 2));

    al_u8_uninit(&normal);

    /* The `i < capacity` side: a refused arena forces init_2's borrow to fail,
     * which zeros capacity while data_size stays 3 - capacity is now the SMALLER
     * of the two, so the loop must stop at capacity (0 copied), not at data_size. */
    Arena refused = arena_init_2(0, 8, ARENA_TYPE_LINEAR);
    AL_U8 starved = al_u8_alloc_init_3(data, 3, &refused);

    test_expect_u(test, "capacity forced to 0 by the refusal", 0, al_u8_get_capacity(&starved));
    test_expect_u(test, "the capacity bound stopped the copy, not data_size", 0, al_u8_get_size(&starved));

    al_u8_uninit(&starved);

    test_case_end(test);
}

int main(void) {
    LogConfig const log_config = {
        .level             = LOG_LEVEL_ERROR,
        .stream            = stdout,
        .timestamp_enabled = true,
        .autoflush         = true
    };

    log_init(log_config);

    Test test = test_init("tests/container/arrayList/test_al_u8.c");

    test_suite_begin(&test, "al_u8");
    _test_basic(&test);
    _test_at_in_bounds(&test);
    _test_set_size_clamps_to_capacity(&test);
    _test_init_3_hardened_bound(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}