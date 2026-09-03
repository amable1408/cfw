#include <test/test.h>

#include <container/map/map_char_al_char.h>
#include <arena/arena.h>

/* Suite for Map_Char_AL_Char, the canonical STRUCT-valued map - the third and last shape in
 * the family, and the one whose ownership rule is genuinely different. map_char_char's suite
 * pins the shared contract and map_char_u64's pins the scalar divergences; this one pins the
 * two things that are true only here and on map_char_string:
 *
 *   - THE VALUE MOVES VIA AL_Char** - the family rule for struct values. The stored copy
 *     carries the element's OWN ALLOCATOR: al_al_char_remove calls al_char_uninit on it,
 *     releasing through that AL_Char's allocator field rather than the value list's. The
 *     rollback must still NEUTRALISE the value slot before removing it, exactly as the key
 *     side does, because the vacate runs only AFTER both halves land - on a decline the
 *     caller still owns the buffer an owning removal would free. The scalar maps' simpler
 *     rollback does not port.
 *   - init_3 DEEP-COPIES THE VALUES, element strings included. Adopting them would hand the
 *     caller a contract al_al_char cannot honour: its only public teardown releases every
 *     element, so "keep your lists but do not release their elements" would ask for a detach
 *     primitive that does not exist. That was the defect this campaign fixed on the pointer
 *     canonical, and it would have been reintroduced verbatim by a copy.
 *
 * The live consumer is include/csv/csv.c, whose own suite covers add/at_1/get_value/uninit
 * end to end; what is here is the decline and ownership surface it does not reach. */

/*==============================================================================
 * MARK: - Internal
 *============================================================================*/

static AL_Char _list_of(char const *const first, char const *const second) {
    AL_Char list = al_char_init_1();

    al_char_add_last(&list, char_new_2(first));

    if (second != nullptr) {
        al_char_add_last(&list, char_new_2(second));
    }

    return list;
}

/*==============================================================================
 * MARK: - Cases
 *============================================================================*/

static void _test_add_adopts_both_halves(Test *const test) {
    test_case_begin(test, "add adopts the key and MOVES the value; uninit releases both");

    Map_Char_AL_Char map = map_char_al_char_init_1();

    test_expect_true(test, "starts empty", map_char_al_char_empty(&map));

    AL_Char first = _list_of("a1", "a2");
    AL_Char second = _list_of("b1", nullptr);

    AL_Char *moving_first  = &first;
    AL_Char *moving_second = &second;

    test_expect_true(test, "add a", map_char_al_char_add(&map, char_new_2("alpha"), &moving_first));
    test_expect_true(test, "add b", map_char_al_char_add(&map, char_new_2("beta"), &moving_second));

    // The move's receipt, asserted where the family rule says it must hold: the handles
    // are nulled and the sources vacated, so this frame no longer claims either buffer.
    test_expect_null(test, "a's handle was nulled", moving_first);
    test_expect_null(test, "b's handle was nulled", moving_second);
    test_expect_u(test, "a's source is vacated", 0, al_char_get_size(&first));

    test_expect_u(test, "size 2", 2, map_char_al_char_get_size(&map));

    AL_Char *const found = map_char_al_char_at_1(&map, "alpha");

    test_expect_not_null(test, "alpha reads back", found);
    test_expect_u(test, "with both its cells", 2, al_char_get_size(found));
    test_expect_string(test, "cell 0", "a1", al_char_at(found, 0));
    test_expect_string(test, "cell 1", "a2", al_char_at(found, 1));
    test_expect_null(test, "an absent key answers nullptr", map_char_al_char_at_1(&map, "gamma"));

    test_expect_string(test, "indexed key 0", "alpha", map_char_al_char_get_key(&map, 0));
    test_expect_u(test, "indexed value 1 has one cell", 1, al_char_get_size(map_char_al_char_get_value(&map, 1)));

    /* Uninit of the vacated locals would be a harmless no-op now - but leaving them alone
     * still proves the sharper thing: the map's copy is the SOLE claimant, so this single
     * release is the whole teardown and a regression that stopped vacating would surface
     * here as a double free under ASan. */
    map_char_al_char_uninit(&map);

    test_expect_u(test, "size 0 after uninit", 0, map_char_al_char_get_size(&map));

    map_char_al_char_uninit(&map);

    test_expect_u(test, "uninit is idempotent", 0, map_char_al_char_get_size(&map));

    test_case_end(test);
}

static void _test_empty_value_list_versus_absent(Test *const test) {
    test_case_begin(test, "an empty value list is a value; contains tells it from absent");

    Map_Char_AL_Char map = map_char_al_char_init_1();

    AL_Char empty = al_char_init_1();

    AL_Char *moving_empty = &empty;

    test_expect_true(test, "store an empty list", map_char_al_char_add_static(&map, "blank", &moving_empty));

    /* at_1 answers a non-null pointer to an EMPTY list, which is a different thing from the
     * nullptr an absent key gets - but a caller checking "did I get anything" by looking at
     * the list's size cannot tell them apart. That is what contains_* is for here. */
    test_expect_not_null(test, "at finds it", map_char_al_char_at_1(&map, "blank"));
    test_expect_u(test, "and the list is empty", 0, al_char_get_size(map_char_al_char_at_1(&map, "blank")));
    test_expect_true(test, "contains says present", map_char_al_char_contains_1(&map, "blank"));
    test_expect_false(test, "and absent for a key never added", map_char_al_char_contains_1(&map, "missing"));

    map_char_al_char_uninit(&map);

    test_case_end(test);
}

static void _test_empty_key_is_a_value(Test *const test) {
    test_case_begin(test, "an empty key is looked up, not aborted on");

    Map_Char_AL_Char map = map_char_al_char_init_1();

    AL_Char list = _list_of("x", nullptr);

    /* This instantiation carried the empty-key abort until this round - at_4 ran key_size
     * through error_check_non_value_uint. A form body of "=x" parses into a zero-length key. */
    AL_Char *moving_list = &list;

    test_expect_true(test, "an empty key can be stored", map_char_al_char_add_static_2(&map, "", 0, &moving_list));

    test_expect_not_null(test, "at_2 finds it by length 0", map_char_al_char_at_2(&map, "", 0));
    test_expect_true(test, "contains_2 agrees", map_char_al_char_contains_2(&map, "", 0));

    map_char_al_char_uninit(&map);

    test_case_end(test);
}

static void _test_size_is_derived(Test *const test) {
    test_case_begin(test, "get_size follows the lists rather than a stored counter");

    Map_Char_AL_Char map = map_char_al_char_init_1();

    AL_Char first = _list_of("v", nullptr);

    AL_Char *moving_first = &first;

    test_expect_true(test, "add one", map_char_al_char_add_static(&map, "k", &moving_first));
    test_expect_u(test, "size 1", 1, map_char_al_char_get_size(&map));

    /* The `size` counter this round removed stayed put here, so get_size disagreed with the
     * storage and every indexed read past it was invisible. */
    al_char_add_last(map_char_al_char_get_keys(&map), char_new_2("k2"));

    test_expect_u(test, "a half pair does not count", 1, map_char_al_char_get_size(&map));

    AL_Char second = _list_of("w", nullptr);

    al_al_char_add_last(map_char_al_char_get_values(&map), &second);

    test_expect_u(test, "completing the pair counts it", 2, map_char_al_char_get_size(&map));
    test_expect_not_null(test, "and it is readable", map_char_al_char_at_1(&map, "k2"));

    map_char_al_char_uninit(&map);

    test_case_end(test);
}

static void _test_duplicate_keys_and_remove_at(Test *const test) {
    test_case_begin(test, "duplicates shadow; remove_at targets the index, remove_1 the first");

    Map_Char_AL_Char map = map_char_al_char_init_1();

    AL_Char one = _list_of("first", nullptr);
    AL_Char two = _list_of("other", nullptr);
    AL_Char three = _list_of("second", nullptr);

    AL_Char *moving_one   = &one;
    AL_Char *moving_two   = &two;
    AL_Char *moving_three = &three;

    test_expect_true(test, "add 0", map_char_al_char_add_static(&map, "k", &moving_one));
    test_expect_true(test, "add 1", map_char_al_char_add_static(&map, "other", &moving_two));
    test_expect_true(test, "add 2", map_char_al_char_add_static(&map, "k", &moving_three));

    test_expect_string(test, "lookup answers the first", "first", al_char_at(map_char_al_char_at_1(&map, "k"), 0));
    test_expect_string(test, "index 2 is the shadowed duplicate", "second", al_char_at(map_char_al_char_get_value(&map, 2), 0));

    map_char_al_char_remove_at(&map, 2);

    test_expect_u(test, "two pairs left", 2, map_char_al_char_get_size(&map));
    test_expect_string(test, "the FIRST k survived", "first", al_char_at(map_char_al_char_at_1(&map, "k"), 0));

    test_expect_true(test, "remove_1 takes the first", map_char_al_char_remove_1(&map, "k"));
    test_expect_false(test, "k is gone", map_char_al_char_contains_1(&map, "k"));
    test_expect_false(test, "removing again reports false", map_char_al_char_remove_1(&map, "k"));

    map_char_al_char_uninit(&map);

    test_case_end(test);
}

static void _test_init_3_deep_copies_the_values(Test *const test) {
    test_case_begin(test, "init_3 deep-copies the value lists, element strings included");

    AL_Char keys = al_char_init_1();
    AL_AL_Char values = al_al_char_init_1();

    al_char_add_last(&keys, char_new_2("a"));
    al_char_add_last(&keys, char_new_2("b"));

    AL_Char first = _list_of("a1", "a2");
    AL_Char second = _list_of("b1", nullptr);

    al_al_char_add_last(&values, &first);
    al_al_char_add_last(&values, &second);

    Map_Char_AL_Char map = map_char_al_char_init_3(&keys, &values);

    test_expect_u(test, "both pairs copied", 2, map_char_al_char_get_size(&map));
    test_expect_string(test, "a's first cell", "a1", al_char_at(map_char_al_char_at_1(&map, "a"), 0));
    test_expect_u(test, "a's cell count", 2, al_char_get_size(map_char_al_char_at_1(&map, "a")));

    /* The strings are COPIES, which is what makes the sources still the caller's. Adopting
     * them would leave the caller unable to release its own lists without double-freeing -
     * the contract that could not be honoured, fixed on the pointer canonical and preserved
     * here rather than reintroduced by the port. */
    test_expect_true(test, "the stored cell is a different object", al_char_at(map_char_al_char_at_1(&map, "a"), 0) != al_char_at(al_al_char_at_2(&values, 0), 0));
    test_expect_true(test, "and the stored list is a different list", map_char_al_char_at_1(&map, "a") != al_al_char_at_2(&values, 0));

    map_char_al_char_uninit(&map);

    /* No hand-nulling anywhere: both sources still hold everything they ever held. */
    test_expect_u(test, "the source kept both keys", 2, al_char_get_size(&keys));
    test_expect_u(test, "and both value lists", 2, al_al_char_get_size(&values));
    test_expect_string(test, "with their cells intact", "a1", al_char_at(al_al_char_at_2(&values, 0), 0));

    al_char_uninit(&keys);
    al_al_char_uninit(&values);

    test_case_end(test);
}

static void _test_refused_allocator_declines(Test *const test) {
    test_case_begin(test, "a refused allocator declines the add and takes nothing");

    Arena dead = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    Map_Char_AL_Char map = map_char_al_char_alloc_init_2(4, &dead);

    test_expect_u(test, "capacity zeroed to match the storage", 0, map_char_al_char_get_capacity(&map));

    char *const key = char_new_2("key");
    AL_Char value = _list_of("cell", nullptr);

    AL_Char *moving_value = &value;

    test_expect_false(test, "add declined", map_char_al_char_add(&map, key, &moving_value));

    // The move's decline half: the handle is NOT nulled, because nothing was taken.
    test_expect_true(test, "the handle still points at the source", moving_value == &value);
    test_expect_u(test, "nothing was stored", 0, map_char_al_char_get_size(&map));

    /* "Nothing was taken" has teeth here that it does not have on the other shapes: the
     * value list must still be intact, with its own buffer and its own cell, because an
     * owning rollback would have uninit'd it. */
    test_expect_string(test, "the caller still owns the key", "key", key);
    test_expect_u(test, "and the value list is untouched", 1, al_char_get_size(&value));
    test_expect_string(test, "including its cell", "cell", al_char_at(&value, 0));

    char_delete(key);
    al_char_uninit(&value);

    test_expect_null(test, "alloc_new_1 declined", map_char_al_char_alloc_new_1(&dead));
    test_expect_null(test, "alloc_new_2 declined", map_char_al_char_alloc_new_2(4, &dead));

    map_char_al_char_uninit(&map);
    arena_uninit(&dead, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_half_refused_add_does_not_free_the_value(Test *const test) {
    test_case_begin(test, "a half-refused add rolls the VALUE back without freeing the caller's list");

    Arena dead = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    /* Built by hand from two lists with DIFFERENT allocators, the only way to reach the
     * asymmetric branch through the public surface: no constructor gives the two halves
     * different allocators, so a fixture that refuses this one and not that one cannot be
     * assembled any other way.
     *
     * KEY list on the dead arena and VALUE list on the heap - this orientation, not the
     * other one. The value rollback only runs when the value half SUCCEEDED, so a map whose
     * value list is the refused one never reaches it: the branch is guarded on the append
     * having landed. Injection proved that directly - with the value list dead, removing the
     * neutralise-before-remove left every assertion green, because the code under test was
     * never executed. */
    Map_Char_AL_Char map = map_char_al_char_init_1();

    map.key = al_char_alloc_init_1(&dead);

    char *const key = char_new_2("orphan");
    AL_Char value = _list_of("survivor", nullptr);

    AL_Char *moving_value = &value;

    test_expect_false(test, "the add declined", map_char_al_char_add(&map, key, &moving_value));

    test_expect_true(test, "the handle was not nulled", moving_value == &value);
    test_expect_u(test, "the key half never grew", 0, al_char_get_size(map_char_al_char_get_keys(&map)));
    test_expect_u(test, "and the value half was rolled back", 0, al_al_char_get_size(map_char_al_char_get_values(&map)));
    test_expect_string(test, "the key is still the caller's", "orphan", key);

    /* The assertion the whole case exists for: the caller's list still has its cell. An
     * owning rollback frees this string, and the read below is then a use-after-free that
     * ASan catches and a plain run may not. */
    test_expect_u(test, "the caller's list still holds its cell", 1, al_char_get_size(&value));
    test_expect_string(test, "and the cell is readable", "survivor", al_char_at(&value, 0));

    char_delete(key);
    al_char_uninit(&value);

    map_char_al_char_uninit(&map);
    arena_uninit(&dead, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_half_refused_add_rolls_the_key_back(Test *const test) {
    test_case_begin(test, "the mirror orientation: a refused VALUE half rolls the key back");

    Arena dead = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    /* The OTHER orientation, and it is a separate case rather than more assertions because
     * each one reaches a different branch. The rollback has two arms, guarded independently
     * on which half actually grew, so a suite with only one orientation leaves the other arm
     * unexecuted - injection showed exactly that: with only the key-on-dead-arena case,
     * deleting the KEY neutralise-before-remove left every assertion green. */
    Map_Char_AL_Char map = map_char_al_char_init_1();

    map.value = al_al_char_alloc_init_1(&dead);

    char *const key = char_new_2("orphan");
    AL_Char value = _list_of("survivor", nullptr);

    AL_Char *moving_value = &value;

    test_expect_false(test, "the add declined", map_char_al_char_add(&map, key, &moving_value));

    test_expect_u(test, "the key half was rolled back", 0, al_char_get_size(map_char_al_char_get_keys(&map)));
    test_expect_u(test, "the value half never grew", 0, al_al_char_get_size(map_char_al_char_get_values(&map)));

    /* The key must not have been released by the rollback - reading it here is the check. */
    test_expect_string(test, "the key is still the caller's", "orphan", key);
    test_expect_u(test, "and the value list is untouched", 1, al_char_get_size(&value));

    char_delete(key);
    al_char_uninit(&value);

    map_char_al_char_uninit(&map);
    arena_uninit(&dead, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_capacity_and_arena_round_trip(Test *const test) {
    test_case_begin(test, "capacity is the smaller of the two; the arena round trip holds");

    Arena arena = arena_init_1(8192, ARENA_TYPE_LINEAR);

    Map_Char_AL_Char *map = map_char_al_char_alloc_new_2(4, &arena);

    test_expect_not_null(test, "alloc_new_2 allocated", map);
    test_expect_u(test, "capacity honoured", 4, map_char_al_char_get_capacity(map));

    al_char_reserve(map_char_al_char_get_keys(map), 16);

    test_expect_u(test, "growing one side does not raise it", 4, map_char_al_char_get_capacity(map));

    map_char_al_char_reserve(map, 16);

    test_expect_u(test, "the combined reserve moves both", 16, map_char_al_char_get_capacity(map));

    AL_Char value = al_char_alloc_init_1(&arena);

    al_char_add_last(&value, char_alloc_new_2("cell", &arena));

    AL_Char *moving_value = &value;

    test_expect_true(test, "add_static", map_char_al_char_add_static(map, "k", &moving_value));
    test_expect_string(test, "reads back", "cell", al_char_at(map_char_al_char_at_1(map, "k"), 0));

    test_expect_true(test, "remove releases through the arena", map_char_al_char_remove_1(map, "k"));
    test_expect_true(test, "empty", map_char_al_char_empty(map));

    map_char_al_char_delete(&map);

    test_expect_null(test, "delete nulled the caller's pointer", map);

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_exhausted_arena_declines_the_struct(Test *const test) {
    test_case_begin(test, "a LIVE but too-small arena declines the struct borrow rather than aborting");

    AL_Char keys = al_char_init_1();
    AL_AL_Char values = al_al_char_init_1();

    al_char_add_last(&keys, char_new_2("a"));

    AL_Char first = _list_of("a1", nullptr);

    al_al_char_add_last(&values, &first);

    /* A REPLICATION TEMPLATE: an arena whose total capacity is exactly MEMORY_ALIGNMENT holds
     * strictly less than MEMORY_ALIGN_UP(sizeof(T)) for every T in the family (all 64-80
     * bytes), so the struct borrow declines regardless of which map is under test. The 1-byte
     * probe proves the arena is LIVE - not a dead null-handler - without depending on sizeof
     * this instantiation's struct. See map_char_char's suite for the full derivation. */
    Arena tiny = arena_init_2(MEMORY_ALIGNMENT, 1, ARENA_TYPE_LINEAR);

    void *const probe = allocator_try_borrow(1, &tiny);

    test_expect_not_null(test, "a 1-byte probe succeeds: the arena is live", probe);

    allocator_release(probe, &tiny);

    test_expect_null(test, "alloc_new_1 declines an EXHAUSTED arena", map_char_al_char_alloc_new_1(&tiny));
    test_expect_null(test, "alloc_new_2 too", map_char_al_char_alloc_new_2(4, &tiny));
    test_expect_null(test, "alloc_new_3 too", map_char_al_char_alloc_new_3(&keys, &values, &tiny));

    arena_uninit(&tiny, ARENA_TYPE_LINEAR);

    al_char_uninit(&keys);
    al_al_char_uninit(&values);

    test_case_end(test);
}

static void _test_embedded_nul_key_matches_by_leading_segment(Test *const test) {
    test_case_begin(test, "a key holding an embedded NUL is stored, but matches only its LEADING segment");

    Map_Char_AL_Char map = map_char_al_char_init_1();

    AL_Char list = _list_of("v", nullptr);
    AL_Char *moving = &list;

    /* "a\0b", 3 bytes: the STORED key is measured with char_length, which stops at the
     * embedded NUL - so every lookup after storage sees "a" and nothing past it. */
    test_expect_true(test, "store the 3-byte key", map_char_al_char_add_static_2(&map, "a\0b", 3, &moving));

    test_expect_not_null(test, "at_2 finds it by the LEADING segment", map_char_al_char_at_2(&map, "a", 1));
    test_expect_null(test, "at_2 does NOT find it by the full 3 bytes", map_char_al_char_at_2(&map, "a\0b", 3));
    test_expect_true(test, "remove_2 removes it by the leading segment too", map_char_al_char_remove_2(&map, "a", 1));
    test_expect_u(test, "gone", 0, map_char_al_char_get_size(&map));

    map_char_al_char_uninit(&map);

    test_case_end(test);
}

static void _test_clear_keeps_the_capacity_that_shrink_drops(Test *const test) {
    test_case_begin(test, "clear empties without shrinking; shrink drops what clear kept");

    Map_Char_AL_Char map = map_char_al_char_init_2(8);

    test_expect_u(test, "init_2 honoured the capacity", 8, map_char_al_char_get_capacity(&map));

    AL_Char first = _list_of("a1", "a2");
    AL_Char second = _list_of("b1", nullptr);

    AL_Char *moving_first  = &first;
    AL_Char *moving_second = &second;

    test_expect_true(test, "first pair", map_char_al_char_add(&map, char_new_2("a"), &moving_first));
    test_expect_true(test, "second pair", map_char_al_char_add(&map, char_new_2("b"), &moving_second));

    map_char_al_char_clear(&map);

    /* What this case can show is the map's own bookkeeping. What it CANNOT show on Windows is
     * that clear actually released the two value lists: a missed release is a leak, and a leak
     * has no observable here. That half belongs to the Linux lane, under ASan/LSan. What the
     * sequence below does rule out is the opposite defect - if clear released and the uninit
     * at the end released the same lists again, this is a double free rather than a quiet pass. */
    test_expect_u(test, "clear emptied it", 0, map_char_al_char_get_size(&map));
    test_expect_true(test, "and it reports empty", map_char_al_char_empty(&map));
    /* Both halves, read directly: get_size is the SMALLER of the two, so a clear that emptied
     * only the key list still reports 0 and the desync would surface at the next add instead
     * of here. Injection is what showed that - the case caught it three assertions late. */
    test_expect_u(test, "the key half is empty", 0, al_char_get_size(map_char_al_char_get_keys(&map)));
    test_expect_u(test, "and so is the value half", 0, al_al_char_get_size(map_char_al_char_get_values(&map)));
    test_expect_u(test, "while KEEPING the capacity", 8, map_char_al_char_get_capacity(&map));

    map_char_al_char_clear(&map);

    test_expect_true(test, "clear is idempotent", map_char_al_char_empty(&map));

    AL_Char third = _list_of("c1", nullptr);

    AL_Char *moving_third = &third;

    test_expect_true(test, "add after clear", map_char_al_char_add(&map, char_new_2("c"), &moving_third));
    test_expect_string(test, "reads back", "c1", al_char_at(map_char_al_char_at_1(&map, "c"), 0));
    test_expect_false(test, "and the cleared keys did not come back", map_char_al_char_contains_1(&map, "a"));

    map_char_al_char_clear(&map);
    map_char_al_char_shrink(&map);

    test_expect_u(test, "shrink drops what clear kept", 0, map_char_al_char_get_capacity(&map));

    map_char_al_char_uninit(&map);

    test_case_end(test);
}

static void _test_remove_2_measures_the_query_key_by_size(Test *const test) {
    test_case_begin(test, "the _2 forms take a size, so a slice of a buffer needs no copy");

    Map_Char_AL_Char map = map_char_al_char_init_1();

    /* One buffer, two keys inside it, neither terminated where its key ends. That is what the
     * _2 forms exist for: the caller never has to cut a copy out first. */
    char const *const packed = "alphabetagamma";

    AL_Char alpha = _list_of("a1", nullptr);
    AL_Char beta = _list_of("b1", nullptr);

    AL_Char *moving_alpha = &alpha;
    AL_Char *moving_beta  = &beta;

    test_expect_true(test, "add_static_2 copied the first slice", map_char_al_char_add_static_2(&map, packed, 5, &moving_alpha));
    test_expect_true(test, "and the second", map_char_al_char_add_static_2(&map, packed + 5, 4, &moving_beta));

    // add_static_2 copies the KEY and MOVES the value - both halves of the family rule.
    test_expect_null(test, "the value was still moved", moving_alpha);
    test_expect_u(test, "leaving the source empty", 0, al_char_get_size(&alpha));

    /* The COPY is terminated even though the source was not - the stored key has to be, because
     * lookup measures it with char_length. */
    test_expect_string(test, "the stored key is terminated", "alpha", map_char_al_char_get_key(&map, 0));
    test_expect_string(test, "and so is the second", "beta", map_char_al_char_get_key(&map, 1));

    test_expect_not_null(test, "at_2 finds the slice", map_char_al_char_at_2(&map, packed, 5));
    test_expect_true(test, "contains_2 agrees", map_char_al_char_contains_2(&map, packed + 5, 4));

    /* The size is the WHOLE comparison, not an upper bound on it: a prefix is not a match. */
    test_expect_false(test, "a shorter slice does not match", map_char_al_char_contains_2(&map, packed, 4));
    test_expect_false(test, "nor does a longer one", map_char_al_char_contains_2(&map, packed, 6));

    test_expect_true(test, "remove_2 removed the slice", map_char_al_char_remove_2(&map, packed + 5, 4));
    test_expect_u(test, "one pair left", 1, map_char_al_char_get_size(&map));
    test_expect_false(test, "and it is gone", map_char_al_char_contains_2(&map, packed + 5, 4));
    test_expect_false(test, "removing it again removes nothing", map_char_al_char_remove_2(&map, packed + 5, 4));
    test_expect_true(test, "the other pair is untouched", map_char_al_char_contains_2(&map, packed, 5));

    map_char_al_char_uninit(&map);

    test_case_end(test);
}

static void _test_heap_and_arena_constructors(Test *const test) {
    test_case_begin(test, "new_1/new_2/new_3 and the arena twins all build a usable map");

    Map_Char_AL_Char *heap = map_char_al_char_new_1();

    test_expect_not_null(test, "new_1 allocated", heap);
    test_expect_u(test, "and starts empty", 0, map_char_al_char_get_size(heap));

    AL_Char one = _list_of("x1", nullptr);

    AL_Char *moving_one = &one;

    test_expect_true(test, "and is usable", map_char_al_char_add(heap, char_new_2("x"), &moving_one));

    map_char_al_char_delete(&heap);

    test_expect_null(test, "delete nulled the caller's pointer", heap);

    Map_Char_AL_Char *sized = map_char_al_char_new_2(6);

    test_expect_not_null(test, "new_2 allocated", sized);
    test_expect_u(test, "with the capacity asked for", 6, map_char_al_char_get_capacity(sized));

    map_char_al_char_delete(&sized);

    /* The copying constructors, heap and arena. All four DEEP-copy, which is what lets the
     * sources below be released exactly once each - by their owner, at the end of this case. */
    AL_Char keys = al_char_init_1();
    AL_AL_Char values = al_al_char_init_1();

    al_char_add_last(&keys, char_new_2("k"));

    AL_Char value = _list_of("v1", "v2");

    al_al_char_add_last(&values, &value);

    Map_Char_AL_Char *copied = map_char_al_char_new_3(&keys, &values);

    test_expect_not_null(test, "new_3 allocated", copied);
    test_expect_string(test, "and deep-copied", "v1", al_char_at(map_char_al_char_at_1(copied, "k"), 0));

    map_char_al_char_delete(&copied);

    Arena arena = arena_init_1(8192, ARENA_TYPE_LINEAR);

    Map_Char_AL_Char bare = map_char_al_char_alloc_init_1(&arena);

    test_expect_u(test, "alloc_init_1 starts empty", 0, map_char_al_char_get_size(&bare));

    AL_Char borrowed = al_char_alloc_init_1(&arena);

    al_char_add_last(&borrowed, char_alloc_new_2("cell", &arena));

    AL_Char *moving_borrowed = &borrowed;

    test_expect_true(test, "and is usable", map_char_al_char_add(&bare, char_alloc_new_2("k", &arena), &moving_borrowed));

    map_char_al_char_uninit(&bare);

    Map_Char_AL_Char on_arena = map_char_al_char_alloc_init_3(&keys, &values, &arena);

    test_expect_string(test, "alloc_init_3 deep-copied too", "v2", al_char_at(map_char_al_char_at_1(&on_arena, "k"), 1));

    map_char_al_char_uninit(&on_arena);

    Map_Char_AL_Char *on_arena_heap = map_char_al_char_alloc_new_3(&keys, &values, &arena);

    test_expect_not_null(test, "alloc_new_3 allocated", on_arena_heap);
    test_expect_string(test, "and deep-copied", "v1", al_char_at(map_char_al_char_at_1(on_arena_heap, "k"), 0));

    map_char_al_char_delete(&on_arena_heap);

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    /* Four constructors copied out of these two sources and none of them took anything. */
    test_expect_u(test, "the source keys survived", 1, al_char_get_size(&keys));
    test_expect_string(test, "with their value cells", "v1", al_char_at(al_al_char_at_2(&values, 0), 0));

    al_char_uninit(&keys);
    al_al_char_uninit(&values);

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

    Test test = test_init("tests/container/map/test_map_char_al_char.c");

    test_suite_begin(&test, "map_char_al_char");
    _test_add_adopts_both_halves(&test);
    _test_empty_value_list_versus_absent(&test);
    _test_empty_key_is_a_value(&test);
    _test_size_is_derived(&test);
    _test_duplicate_keys_and_remove_at(&test);
    _test_init_3_deep_copies_the_values(&test);
    _test_refused_allocator_declines(&test);
    _test_half_refused_add_does_not_free_the_value(&test);
    _test_half_refused_add_rolls_the_key_back(&test);
    _test_capacity_and_arena_round_trip(&test);
    _test_exhausted_arena_declines_the_struct(&test);
    _test_embedded_nul_key_matches_by_leading_segment(&test);
    _test_clear_keeps_the_capacity_that_shrink_drops(&test);
    _test_remove_2_measures_the_query_key_by_size(&test);
    _test_heap_and_arena_constructors(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}