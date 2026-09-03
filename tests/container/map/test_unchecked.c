/*
 * test_unchecked.c - Behavioral tests for include/container/map/map_char_char.c (and the
 * al_char/char substrate it builds on) compiled WITHOUT ERROR_CHECK_ENABLED.
 *
 * Every error_check in this module guards a dereference or an index - a violated contract
 * is UB with the checks compiled out, not something this suite can observe. What survives
 * the define is the module's OTHER class of condition: the value-dependent refusals the
 * header repeatedly distinguishes from contract violations, coded as plain runtime `if`
 * statements rather than error_check. Pinned here: an absent key still answers nullptr, an
 * empty key is still a legal lookup, a declined allocator still makes add and the arena
 * constructors decline rather than write through a null borrow, and char_copy_3's
 * over-long-source refusal (the substrate _map_char_char_copy relies on) still leaves a
 * terminated, empty destination instead of writing past it. Built by the makefile's
 * `unchecked` target into a separate object namespace (.maptestunchecked.o) so it never
 * shares objects with the checked build.
 */
#include <test/test.h>

#include <container/map/map_char_char.h>
#include <arena/arena.h>

int main(void) {
    LogConfig const log_config = {
        .level             = LOG_LEVEL_ERROR,
        .stream            = stdout,
        .timestamp_enabled = true,
        .autoflush         = true
    };

    log_init(log_config);

    Test test = test_init("tests/container/map/test_unchecked.c");

    test_suite_begin(&test, "map_char_char (unchecked)");
    test_case_begin(&test, "an absent key still answers nullptr, not a stray abort");

    Map_Char_Char map = map_char_char_init_1();

    test_expect_true(&test, "add a normal pair", map_char_char_add_static(&map, "k", "v"));
    test_expect_null(&test, "an absent key is nullptr", map_char_char_at_1(&map, "missing"));
    test_expect_false(&test, "and contains agrees", map_char_char_contains_1(&map, "missing"));

    test_case_end(&test);

    test_case_begin(&test, "an empty key is still a legal lookup, never a compiled-away guard");

    test_expect_true(&test, "an empty key can be stored", map_char_char_add_static_2(&map, "", 0, "x", 1));
    test_expect_string(&test, "at_2 finds it by length 0", "x", map_char_char_at_2(&map, "", 0));
    test_expect_true(&test, "contains_2 agrees", map_char_char_contains_2(&map, "", 0));

    map_char_char_uninit(&map);

    test_case_end(&test);

    test_case_begin(&test, "a declined allocator makes add and the arena constructors decline, not crash");

    Arena dead = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    Map_Char_Char arena_map = map_char_char_alloc_init_2(4, &dead);

    test_expect_u(&test, "capacity zeroed to match the storage", 0, map_char_char_get_capacity(&arena_map));

    char *const key = char_new_2("key");
    char *const value = char_new_2("value");

    test_expect_false(&test, "add declined", map_char_char_add(&arena_map, key, value));
    test_expect_u(&test, "nothing was stored", 0, map_char_char_get_size(&arena_map));

    char_delete(key);
    char_delete(value);

    map_char_char_uninit(&arena_map);

    test_expect_null(&test, "alloc_new_1 declines a refused arena", map_char_char_alloc_new_1(&dead));
    test_expect_null(&test, "alloc_new_2 too", map_char_char_alloc_new_2(4, &dead));

    arena_uninit(&dead, ARENA_TYPE_LINEAR);

    test_case_end(&test);

    test_case_begin(&test, "with checks off, a zero capacity degrades to an ordinary empty map");

    Map_Char_Char zero = map_char_char_init_2(0);

    test_expect_u(&test, "capacity is 0 right after init_2(0)", 0, map_char_char_get_capacity(&zero));
    test_expect_u(&test, "get_size is 0, not a stray capacity", 0, map_char_char_get_size(&zero));
    test_expect_false(&test, "contains is false on the empty map", map_char_char_contains_1(&zero, "k"));
    test_expect_true(&test, "add still works - bootstrap growth, not the zero capacity", map_char_char_add_static(&zero, "k", "v"));
    test_expect_string(&test, "and the pair is findable", "v", map_char_char_at_1(&zero, "k"));

    USize const capacity_after_add = map_char_char_get_capacity(&zero);

    map_char_char_reserve(&zero, 0);

    test_expect_u(&test, "reserve(self, 0) is a no-op - capacity unchanged", capacity_after_add, map_char_char_get_capacity(&zero));

    map_char_char_uninit(&zero);

    test_expect_u(&test, "uninit is clean - size 0 after", 0, map_char_char_get_size(&zero));

    test_case_end(&test);

    test_case_begin(&test, "the other three zero-capacity constructors the header paragraph names degrade the same way");

    Arena live = arena_init_1(4096, ARENA_TYPE_LINEAR);

    Map_Char_Char alloc_init_zero = map_char_char_alloc_init_2(0, &live);

    test_expect_u(&test, "alloc_init_2(0, live) - size 0", 0, map_char_char_get_size(&alloc_init_zero));
    test_expect_u(&test, "alloc_init_2(0, live) - capacity 0", 0, map_char_char_get_capacity(&alloc_init_zero));
    test_expect_false(&test, "alloc_init_2(0, live) - contains false", map_char_char_contains_1(&alloc_init_zero, "k"));

    map_char_char_uninit(&alloc_init_zero);

    Map_Char_Char *new_zero = map_char_char_new_2(0);

    /* A declined borrow answers nullptr; assert it before reading through it so a
     * decline reads as a failed pin, not a crash of the unchecked build. */
    if (test_expect_not_null(&test, "new_2(0) answers a map", new_zero)) {
        test_expect_u(&test, "new_2(0) - size 0", 0, map_char_char_get_size(new_zero));
        test_expect_u(&test, "new_2(0) - capacity 0", 0, map_char_char_get_capacity(new_zero));
        test_expect_false(&test, "new_2(0) - contains false", map_char_char_contains_1(new_zero, "k"));

        map_char_char_delete(&new_zero);
    }

    Map_Char_Char *alloc_new_zero = map_char_char_alloc_new_2(0, &live);

    if (test_expect_not_null(&test, "alloc_new_2(0, live) answers a map", alloc_new_zero)) {
        test_expect_u(&test, "alloc_new_2(0, live) - size 0", 0, map_char_char_get_size(alloc_new_zero));
        test_expect_u(&test, "alloc_new_2(0, live) - capacity 0", 0, map_char_char_get_capacity(alloc_new_zero));
        test_expect_false(&test, "alloc_new_2(0, live) - contains false", map_char_char_contains_1(alloc_new_zero, "k"));

        map_char_char_delete(&alloc_new_zero);
    }

    arena_uninit(&live, ARENA_TYPE_LINEAR);

    test_case_end(&test);

    test_case_begin(&test, "char_copy_3's over-long-source refusal is a plain if, not error_check");

    char buffer[4] = DEFAULT_INITIALIZATION;

    char_copy_3(buffer, sizeof(buffer), "this string is far too long for the buffer", 42);

    test_expect_string(&test, "the refusal leaves a terminated EMPTY destination", "", buffer);

    char_copy_3(buffer, sizeof(buffer), "abc", 3);

    test_expect_string(&test, "a fitting copy still lands normally", "abc", buffer);

    test_case_end(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}