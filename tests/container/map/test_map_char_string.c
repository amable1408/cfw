#include <test/test.h>

#include <container/map/map_char_string.h>
#include <arena/arena.h>

/* Suite for Map_Char_String, the second STRUCT-valued map - same shape as map_char_al_char,
 * but its element carries an `owned` flag as well as an allocator, and that changes the
 * ownership contract from documented to self-describing. What this suite pins:
 *
 *   - add MOVES. The source String is emptied and the caller's pointer is nulled, so a
 *     retained alias cannot double-free. This is the FAMILY RULE for struct values - both
 *     struct maps take T** and vacate (map_char_al_char converged onto it after shipping a
 *     week as an alias with a "do not touch your local afterwards" caveat, the family's last
 *     caller-visible double-free surface). The transfer is structural, and the tests read
 *     the vacated source directly to prove it.
 *   - `owned` TRAVELS. Store an OWNER and the map frees it; store a VIEW over borrowed
 *     memory and teardown correctly leaves it alone. One API, both cases, no flag on the map.
 *   - A DECLINE LEAVES THE SOURCE WHOLE. The vacate happens only after both halves land, so
 *     a refused add must leave the caller's String holding its buffer and its pointer
 *     unchanged.
 *   - init_3 DEEP-COPIES, and a copied view is PROMOTED to an owner. That is the one place
 *     init_3 differs from add in more than depth. */

/*==============================================================================
 * MARK: - Cases
 *============================================================================*/

static void _test_add_moves_the_value(Test *const test) {
    test_case_begin(test, "add empties the source String and nulls the caller's pointer");

    Map_Char_String map = map_char_string_init_1();

    String value = string_init_static("application/json", 16);
    String *moving = &value;

    test_expect_true(test, "the source starts as an owner", value.owned);
    test_expect_u(test, "holding its bytes", 16, string_get_size(&value));

    test_expect_true(test, "add", map_char_string_add(&map, char_new_2("content-type"), &moving));

    /* The transfer, checked on the object rather than taken on trust. */
    test_expect_null(test, "the caller's pointer is nulled", moving);
    test_expect_u(test, "and the source is emptied", 0, string_get_size(&value));
    test_expect_false(test, "no longer claiming ownership", value.owned);
    test_expect_null(test, "and holding no buffer", value.data);

    /* Meanwhile the map has the real thing. */
    String *const stored = map_char_string_at_1(&map, "content-type");

    test_expect_not_null(test, "the map holds it", stored);
    test_expect_u(test, "with its bytes", 16, string_get_size(stored));
    test_expect_string(test, "readable", "application/json", string_get_data(stored));
    test_expect_true(test, "and claiming ownership", stored->owned);

    /* Exactly one claimant, so this is a single release rather than a double free. A
     * regression that vacated on the wrong side, or not at all, shows up here. */
    map_char_string_uninit(&map);

    test_expect_u(test, "size 0 after uninit", 0, map_char_string_get_size(&map));

    map_char_string_uninit(&map);

    test_expect_u(test, "uninit is idempotent", 0, map_char_string_get_size(&map));

    test_case_end(test);
}

static void _test_owned_travels_a_view_is_not_freed(Test *const test) {
    test_case_begin(test, "a stored VIEW is dropped, not freed - `owned` travels with the value");

    Map_Char_String map = map_char_string_init_1();

    /* A writable buffer this frame owns, viewed rather than copied - the shape an lws
     * payload or a request's interior buffer takes. string_init_4 is a VIEW constructor. */
    char borrowed[32] = "borrowed bytes";

    String view = string_init_4(borrowed, 14);
    String *moving = &view;

    test_expect_false(test, "the source is a view", view.owned);

    test_expect_true(test, "add", map_char_string_add(&map, char_new_2("k"), &moving));

    String *const stored = map_char_string_at_1(&map, "k");

    test_expect_not_null(test, "the map holds it", stored);
    test_expect_false(test, "still a view", stored->owned);
    test_expect_true(test, "pointing at the caller's buffer", string_get_data(stored) == borrowed);

    /* The point of the case: uninit must NOT free `borrowed`. It is a stack array, so a
     * release would abort or corrupt rather than pass quietly - which is what makes this a
     * real check and not a formality. */
    map_char_string_uninit(&map);

    test_expect_string(test, "the caller's buffer survived", "borrowed bytes", borrowed);

    test_case_end(test);
}

static void _test_decline_leaves_the_source_whole(Test *const test) {
    test_case_begin(test, "a declined add leaves the source String and the caller's pointer intact");

    Arena dead = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    Map_Char_String map = map_char_string_alloc_init_2(4, &dead);

    test_expect_u(test, "capacity zeroed to match the storage", 0, map_char_string_get_capacity(&map));

    char *const key = char_new_2("key");

    String value = string_init_static("payload", 7);
    String *moving = &value;

    test_expect_false(test, "add declined", map_char_string_add(&map, key, &moving));

    test_expect_u(test, "nothing was stored", 0, map_char_string_get_size(&map));

    /* The vacate runs only after BOTH halves land, so a decline must leave every one of
     * these true. This is the assertion that would catch a vacate hoisted too early. */
    test_expect_true(test, "the caller's pointer is unchanged", moving == &value);
    test_expect_u(test, "the source still holds its bytes", 7, string_get_size(&value));
    test_expect_true(test, "and still claims ownership", value.owned);
    test_expect_string(test, "with its data readable", "payload", string_get_data(&value));
    test_expect_string(test, "and the key is still the caller's", "key", key);

    char_delete(key);
    string_uninit(&value);

    test_expect_null(test, "alloc_new_1 declined", map_char_string_alloc_new_1(&dead));

    map_char_string_uninit(&map);
    arena_uninit(&dead, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_half_refused_rolls_the_value_back(Test *const test) {
    test_case_begin(test, "a refused KEY half rolls the value back without releasing it");

    Arena dead = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    /* Built by hand from two lists with DIFFERENT allocators, the only way to reach the
     * asymmetric branch through the public surface: no constructor gives the two halves
     * different allocators, so a fixture that refuses this one and not that one cannot be
     * assembled any other way.
     *
     * KEY list on the dead arena, VALUE list on the heap. The value rollback arm is guarded
     * on the value append having LANDED, so only this orientation reaches it - the mirror
     * case below covers the other arm. One fixture reaches one arm. */
    Map_Char_String map = map_char_string_init_1();

    map.key = al_char_alloc_init_1(&dead);

    char *const key = char_new_2("orphan");

    String value = string_init_static("survivor", 8);
    String *moving = &value;

    test_expect_false(test, "the add declined", map_char_string_add(&map, key, &moving));

    test_expect_u(test, "the key half never grew", 0, al_char_get_size(map_char_string_get_keys(&map)));
    test_expect_u(test, "and the value half was rolled back", 0, al_string_get_size(map_char_string_get_values(&map)));

    /* The assertion the case exists for: the rollback neutralised the value slot before
     * removing it, so string_uninit did not run on the caller's buffer. Reading it here is
     * a use-after-free if that neutralise is ever dropped. */
    test_expect_u(test, "the caller's String still holds its bytes", 8, string_get_size(&value));
    test_expect_string(test, "and they are readable", "survivor", string_get_data(&value));
    test_expect_true(test, "the caller's pointer is unchanged", moving == &value);

    char_delete(key);
    string_uninit(&value);

    map_char_string_uninit(&map);
    arena_uninit(&dead, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_half_refused_rolls_the_key_back(Test *const test) {
    test_case_begin(test, "the mirror orientation: a refused VALUE half rolls the key back");

    Arena dead = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    Map_Char_String map = map_char_string_init_1();

    map.value = al_string_alloc_init_1(&dead);

    char *const key = char_new_2("orphan");

    String value = string_init_static("survivor", 8);
    String *moving = &value;

    test_expect_false(test, "the add declined", map_char_string_add(&map, key, &moving));

    test_expect_u(test, "the key half was rolled back", 0, al_char_get_size(map_char_string_get_keys(&map)));
    test_expect_u(test, "the value half never grew", 0, al_string_get_size(map_char_string_get_values(&map)));

    /* The key must not have been released by its rollback - reading it is the check. */
    test_expect_string(test, "the key is still the caller's", "orphan", key);
    test_expect_u(test, "and the value is untouched", 8, string_get_size(&value));

    char_delete(key);
    string_uninit(&value);

    map_char_string_uninit(&map);
    arena_uninit(&dead, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_empty_key_and_empty_value(Test *const test) {
    test_case_begin(test, "an empty key and an empty String are both legal values");

    Map_Char_String map = map_char_string_init_1();

    String value = string_init_static("x", 1);
    String *moving = &value;

    /* This instantiation carried the empty-key abort until the family converged - at_2 ran
     * key_size through error_check_non_value_uint. A form body of "=x" parses into one. */
    test_expect_true(test, "an empty key can be stored", map_char_string_add_static_2(&map, "", 0, &moving));

    test_expect_not_null(test, "at_2 finds it by length 0", map_char_string_at_2(&map, "", 0));
    test_expect_true(test, "contains_2 agrees", map_char_string_contains_2(&map, "", 0));

    /* An empty String is a value too, and only contains_* tells it from an absent key. */
    String empty = string_init_1();
    String *empty_moving = &empty;

    test_expect_true(test, "an empty String stores", map_char_string_add_static(&map, "blank", &empty_moving));
    test_expect_not_null(test, "at finds it", map_char_string_at_1(&map, "blank"));
    test_expect_u(test, "and it is empty", 0, string_get_size(map_char_string_at_1(&map, "blank")));
    test_expect_true(test, "contains says present", map_char_string_contains_1(&map, "blank"));
    test_expect_false(test, "and absent for a key never added", map_char_string_contains_1(&map, "missing"));

    map_char_string_uninit(&map);

    test_case_end(test);
}

static void _test_size_is_derived_and_remove_at(Test *const test) {
    test_case_begin(test, "size is derived; duplicates shadow; remove_at targets the index");

    Map_Char_String map = map_char_string_init_1();

    String one = string_init_static("first", 5);
    String two = string_init_static("other", 5);
    String three = string_init_static("second", 6);

    String *p = &one;
    String *q = &two;
    String *r = &three;

    test_expect_true(test, "add 0", map_char_string_add_static(&map, "k", &p));
    test_expect_true(test, "add 1", map_char_string_add_static(&map, "other", &q));
    test_expect_true(test, "add 2", map_char_string_add_static(&map, "k", &r));

    test_expect_string(test, "lookup answers the first", "first", string_get_data(map_char_string_at_1(&map, "k")));
    test_expect_string(test, "index 2 is the shadowed duplicate", "second", string_get_data(map_char_string_get_value(&map, 2)));

    map_char_string_remove_at(&map, 2);

    test_expect_u(test, "two pairs left", 2, map_char_string_get_size(&map));
    test_expect_string(test, "the FIRST k survived", "first", string_get_data(map_char_string_at_1(&map, "k")));

    /* Written straight through the exposed handles, which the map cannot observe. */
    al_char_add_last(map_char_string_get_keys(&map), char_new_2("k3"));

    test_expect_u(test, "a half pair does not count", 2, map_char_string_get_size(&map));

    map_char_string_uninit(&map);

    test_case_end(test);
}

static void _test_init_3_deep_copies_and_promotes_a_view(Test *const test) {
    test_case_begin(test, "init_3 deep-copies, and a copied VIEW becomes an OWNER");

    AL_Char keys = al_char_init_1();
    AL_String values = al_string_init_1();

    al_char_add_last(&keys, char_new_2("a"));
    al_char_add_last(&keys, char_new_2("b"));

    String owner = string_init_static("owned value", 11);

    char borrowed[32] = "viewed value";
    String view = string_init_4(borrowed, 12);

    test_expect_true(test, "source 0 is an owner", owner.owned);
    test_expect_false(test, "source 1 is a view", view.owned);

    al_string_add_last(&values, &owner);
    al_string_add_last(&values, &view);

    Map_Char_String map = map_char_string_init_3(&keys, &values);

    test_expect_u(test, "both pairs copied", 2, map_char_string_get_size(&map));
    test_expect_string(test, "a's bytes", "owned value", string_get_data(map_char_string_at_1(&map, "a")));
    test_expect_string(test, "b's bytes", "viewed value", string_get_data(map_char_string_at_1(&map, "b")));

    /* The promotion the header calls out: init_3 copies, and a copy OWNS - so the stored
     * value for "b" is an owner even though its source was a view, and it no longer points
     * at the caller's buffer. add would have preserved view-ness; init_3 does not. */
    test_expect_true(test, "the copied view was PROMOTED to an owner", map_char_string_at_1(&map, "b")->owned);
    test_expect_true(test, "and no longer aliases the caller's buffer", string_get_data(map_char_string_at_1(&map, "b")) != borrowed);

    /* Deep copy means the sources are still wholly the caller's. */
    map_char_string_uninit(&map);

    test_expect_u(test, "the source kept both keys", 2, al_char_get_size(&keys));
    test_expect_u(test, "and both values", 2, al_string_get_size(&values));
    test_expect_string(test, "with the owner's bytes intact", "owned value", string_get_data(al_string_at(&values, 0)));
    test_expect_string(test, "and the view's buffer untouched", "viewed value", borrowed);

    al_char_uninit(&keys);
    al_string_uninit(&values);

    test_case_end(test);
}

static void _test_capacity_and_arena_round_trip(Test *const test) {
    test_case_begin(test, "capacity is the smaller of the two; the arena round trip holds");

    Arena arena = arena_init_1(8192, ARENA_TYPE_LINEAR);

    Map_Char_String *map = map_char_string_alloc_new_2(4, &arena);

    test_expect_not_null(test, "alloc_new_2 allocated", map);
    test_expect_u(test, "capacity honoured", 4, map_char_string_get_capacity(map));

    al_char_reserve(map_char_string_get_keys(map), 16);

    test_expect_u(test, "growing one side does not raise it", 4, map_char_string_get_capacity(map));

    map_char_string_reserve(map, 16);

    test_expect_u(test, "the combined reserve moves both", 16, map_char_string_get_capacity(map));

    String value = string_alloc_init_2(16, &arena);
    String *moving = &value;

    test_expect_true(test, "add_static", map_char_string_add_static(map, "k", &moving));
    test_expect_null(test, "the source was moved from", moving);

    test_expect_true(test, "remove releases through the value's own allocator", map_char_string_remove_1(map, "k"));
    test_expect_true(test, "empty", map_char_string_empty(map));

    map_char_string_delete(&map);

    test_expect_null(test, "delete nulled the caller's pointer", map);

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_exhausted_arena_declines_the_struct(Test *const test) {
    test_case_begin(test, "a LIVE but too-small arena declines the struct borrow rather than aborting");

    AL_Char keys = al_char_init_1();
    AL_String values = al_string_init_1();

    al_char_add_last(&keys, char_new_2("a"));

    String value = string_init_static("1", 1);

    al_string_add_last(&values, &value);

    /* A REPLICATION TEMPLATE: an arena whose total capacity is exactly MEMORY_ALIGNMENT holds
     * strictly less than MEMORY_ALIGN_UP(sizeof(T)) for every T in the family (all 64-80
     * bytes), so the struct borrow declines regardless of which map is under test. The 1-byte
     * probe proves the arena is LIVE - not a dead null-handler - without depending on sizeof
     * this instantiation's struct. See map_char_char's suite for the full derivation. */
    Arena tiny = arena_init_2(MEMORY_ALIGNMENT, 1, ARENA_TYPE_LINEAR);

    void *const probe = allocator_try_borrow(1, &tiny);

    test_expect_not_null(test, "a 1-byte probe succeeds: the arena is live", probe);

    allocator_release(probe, &tiny);

    test_expect_null(test, "alloc_new_1 declines an EXHAUSTED arena", map_char_string_alloc_new_1(&tiny));
    test_expect_null(test, "alloc_new_2 too", map_char_string_alloc_new_2(4, &tiny));
    test_expect_null(test, "alloc_new_3 too", map_char_string_alloc_new_3(&keys, &values, &tiny));

    arena_uninit(&tiny, ARENA_TYPE_LINEAR);

    al_char_uninit(&keys);
    al_string_uninit(&values);

    test_case_end(test);
}

static void _test_embedded_nul_key_matches_by_leading_segment(Test *const test) {
    test_case_begin(test, "a key holding an embedded NUL is stored, but matches only its LEADING segment");

    Map_Char_String map = map_char_string_init_1();

    String value = string_init_static("v", 1);
    String *moving = &value;

    /* "a\0b", 3 bytes: the STORED key is measured with char_length, which stops at the
     * embedded NUL - so every lookup after storage sees "a" and nothing past it. */
    test_expect_true(test, "store the 3-byte key", map_char_string_add_static_2(&map, "a\0b", 3, &moving));

    test_expect_not_null(test, "at_2 finds it by the LEADING segment", map_char_string_at_2(&map, "a", 1));
    test_expect_null(test, "at_2 does NOT find it by the full 3 bytes", map_char_string_at_2(&map, "a\0b", 3));
    test_expect_true(test, "remove_2 removes it by the leading segment too", map_char_string_remove_2(&map, "a", 1));
    test_expect_u(test, "gone", 0, map_char_string_get_size(&map));

    map_char_string_uninit(&map);

    test_case_end(test);
}

static void _test_clear_keeps_the_capacity_that_shrink_drops(Test *const test) {
    test_case_begin(test, "clear empties without shrinking, and leaves a stored VIEW alone");

    Map_Char_String map = map_char_string_init_2(8);

    test_expect_u(test, "init_2 honoured the capacity", 8, map_char_string_get_capacity(&map));

    /* One OWNER and one VIEW, because clear has to treat them differently: string_uninit
     * releases the first and no-ops the second, and the borrowed buffer below is the thing
     * that must survive. A clear that released unconditionally corrupts `borrowed`. */
    char borrowed[32] = "borrowed-payload";

    String owner = string_init_static("owned-value", 11);
    String view = string_init_4(borrowed, 16);
    String *moving_owner = &owner;
    String *moving_view = &view;

    test_expect_true(test, "the owner claims ownership", owner.owned);
    test_expect_false(test, "and the view does not", view.owned);

    test_expect_true(test, "owner stored", map_char_string_add(&map, char_new_2("owner"), &moving_owner));
    test_expect_true(test, "view stored", map_char_string_add(&map, char_new_2("view"), &moving_view));

    map_char_string_clear(&map);

    /* What this case can show is the map's bookkeeping and the view's buffer. What it CANNOT
     * show on Windows is that clear released the OWNER: a missed release is a leak, and a leak
     * has no observable here - that half belongs to the Linux lane under ASan/LSan. The
     * opposite defect is covered: a clear that released and a uninit that released again would
     * be a double free rather than a quiet pass. */
    test_expect_u(test, "clear emptied it", 0, map_char_string_get_size(&map));
    test_expect_true(test, "and it reports empty", map_char_string_empty(&map));
    /* Both halves, read directly: get_size is the SMALLER of the two, so a clear that emptied
     * only the key list still reports 0 and the desync would surface at the next add instead
     * of here. Injection is what showed that - the case caught it three assertions late. */
    test_expect_u(test, "the key half is empty", 0, al_char_get_size(map_char_string_get_keys(&map)));
    test_expect_u(test, "and so is the value half", 0, al_string_get_size(map_char_string_get_values(&map)));
    test_expect_u(test, "while KEEPING the capacity", 8, map_char_string_get_capacity(&map));
    test_expect_string(test, "the borrowed buffer is untouched", "borrowed-payload", borrowed);

    map_char_string_clear(&map);

    test_expect_true(test, "clear is idempotent", map_char_string_empty(&map));

    String third = string_init_static("third", 5);
    String *moving_third = &third;

    test_expect_true(test, "add after clear", map_char_string_add(&map, char_new_2("c"), &moving_third));
    test_expect_string(test, "reads back", "third", string_get_data(map_char_string_at_1(&map, "c")));
    test_expect_false(test, "and the cleared keys did not come back", map_char_string_contains_1(&map, "owner"));

    map_char_string_clear(&map);
    map_char_string_shrink(&map);

    test_expect_u(test, "shrink drops what clear kept", 0, map_char_string_get_capacity(&map));

    map_char_string_uninit(&map);

    test_case_end(test);
}

static void _test_remove_2_measures_the_query_key_by_size(Test *const test) {
    test_case_begin(test, "the _2 forms take a size, so a slice of a buffer needs no copy");

    Map_Char_String map = map_char_string_init_1();

    /* One buffer, two keys inside it, neither terminated where its key ends. */
    char const *const packed = "alphabetagamma";

    String alpha = string_init_static("a1", 2);
    String beta = string_init_static("b1", 2);
    String *moving_alpha = &alpha;
    String *moving_beta = &beta;

    test_expect_true(test, "add_static_2 copied the first slice", map_char_string_add_static_2(&map, packed, 5, &moving_alpha));
    test_expect_true(test, "and the second", map_char_string_add_static_2(&map, packed + 5, 4, &moving_beta));

    /* add_static_2 copies the KEY and MOVES the value - the family rule, both halves of it
     * in one call. */
    test_expect_null(test, "the value was still moved", moving_alpha);
    test_expect_u(test, "leaving the source empty", 0, string_get_size(&alpha));

    /* The COPY is terminated even though the source was not: lookup measures the STORED key
     * with char_length, so it has to be. */
    test_expect_string(test, "the stored key is terminated", "alpha", map_char_string_get_key(&map, 0));
    test_expect_string(test, "and so is the second", "beta", map_char_string_get_key(&map, 1));

    test_expect_not_null(test, "at_2 finds the slice", map_char_string_at_2(&map, packed, 5));
    test_expect_true(test, "contains_2 agrees", map_char_string_contains_2(&map, packed + 5, 4));

    /* The size is the WHOLE comparison, not an upper bound on it: a prefix is not a match. */
    test_expect_false(test, "a shorter slice does not match", map_char_string_contains_2(&map, packed, 4));
    test_expect_false(test, "nor does a longer one", map_char_string_contains_2(&map, packed, 6));

    test_expect_true(test, "remove_2 removed the slice", map_char_string_remove_2(&map, packed + 5, 4));
    test_expect_u(test, "one pair left", 1, map_char_string_get_size(&map));
    test_expect_false(test, "and it is gone", map_char_string_contains_2(&map, packed + 5, 4));
    test_expect_false(test, "removing it again removes nothing", map_char_string_remove_2(&map, packed + 5, 4));
    test_expect_true(test, "the other pair is untouched", map_char_string_contains_2(&map, packed, 5));

    map_char_string_uninit(&map);

    test_case_end(test);
}

static void _test_heap_and_arena_constructors(Test *const test) {
    test_case_begin(test, "new_1/new_2/new_3 and the arena twins all build a usable map");

    Map_Char_String *heap = map_char_string_new_1();

    test_expect_not_null(test, "new_1 allocated", heap);
    test_expect_u(test, "and starts empty", 0, map_char_string_get_size(heap));

    String one = string_init_static("x1", 2);
    String *moving_one = &one;

    test_expect_true(test, "and is usable", map_char_string_add(heap, char_new_2("x"), &moving_one));

    map_char_string_delete(&heap);

    test_expect_null(test, "delete nulled the caller's pointer", heap);

    Map_Char_String *sized = map_char_string_new_2(6);

    test_expect_not_null(test, "new_2 allocated", sized);
    test_expect_u(test, "with the capacity asked for", 6, map_char_string_get_capacity(sized));

    map_char_string_delete(&sized);

    /* The copying constructors, heap and arena. All four DEEP-copy through string_init_6, so
     * the sources below are released exactly once each - by their owner, at the end. */
    AL_Char keys = al_char_init_1();
    AL_String values = al_string_init_1();

    al_char_add_last(&keys, char_new_2("k"));

    String value = string_init_static("v1", 2);

    al_string_add_last(&values, &value);

    Map_Char_String *copied = map_char_string_new_3(&keys, &values);

    test_expect_not_null(test, "new_3 allocated", copied);
    test_expect_string(test, "and deep-copied", "v1", string_get_data(map_char_string_at_1(copied, "k")));
    test_expect_true(test, "into a different buffer", string_get_data(map_char_string_at_1(copied, "k")) != string_get_data(al_string_at(&values, 0)));

    map_char_string_delete(&copied);

    Arena arena = arena_init_1(8192, ARENA_TYPE_LINEAR);

    Map_Char_String bare = map_char_string_alloc_init_1(&arena);

    test_expect_u(test, "alloc_init_1 starts empty", 0, map_char_string_get_size(&bare));

    String borrowed = string_alloc_init_1(&arena);
    String *moving_borrowed = &borrowed;

    test_expect_true(test, "and is usable", map_char_string_add(&bare, char_alloc_new_2("k", &arena), &moving_borrowed));

    map_char_string_uninit(&bare);

    Map_Char_String on_arena = map_char_string_alloc_init_3(&keys, &values, &arena);

    test_expect_string(test, "alloc_init_3 deep-copied too", "v1", string_get_data(map_char_string_at_1(&on_arena, "k")));

    map_char_string_uninit(&on_arena);

    Map_Char_String *on_arena_heap = map_char_string_alloc_new_3(&keys, &values, &arena);

    test_expect_not_null(test, "alloc_new_3 allocated", on_arena_heap);
    test_expect_string(test, "and deep-copied", "v1", string_get_data(map_char_string_at_1(on_arena_heap, "k")));

    map_char_string_delete(&on_arena_heap);

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    /* Four constructors copied out of these two sources and none of them took anything. */
    test_expect_u(test, "the source keys survived", 1, al_char_get_size(&keys));
    test_expect_string(test, "with their value bytes", "v1", string_get_data(al_string_at(&values, 0)));

    al_char_uninit(&keys);
    al_string_uninit(&values);

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

    Test test = test_init("tests/container/map/test_map_char_string.c");

    test_suite_begin(&test, "map_char_string");
    _test_add_moves_the_value(&test);
    _test_owned_travels_a_view_is_not_freed(&test);
    _test_decline_leaves_the_source_whole(&test);
    _test_half_refused_rolls_the_value_back(&test);
    _test_half_refused_rolls_the_key_back(&test);
    _test_empty_key_and_empty_value(&test);
    _test_size_is_derived_and_remove_at(&test);
    _test_init_3_deep_copies_and_promotes_a_view(&test);
    _test_capacity_and_arena_round_trip(&test);
    _test_exhausted_arena_declines_the_struct(&test);
    _test_embedded_nul_key_matches_by_leading_segment(&test);
    _test_clear_keeps_the_capacity_that_shrink_drops(&test);
    _test_remove_2_measures_the_query_key_by_size(&test);
    _test_heap_and_arena_constructors(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}