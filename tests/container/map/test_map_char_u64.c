#include <test/test.h>

#include <container/map/map_char_u64.h>
#include <arena/arena.h>

/* Suite for Map_Char_U64, the canonical SCALAR-valued map - the form the other five scalar
 * instantiations (u8, u16, u32, f32, f64) are generated from. map_char_char's suite pins the
 * contract they share; this one pins what the scalar shape does DIFFERENTLY, because that is
 * where a port from the pointer-valued canonical goes wrong:
 *
 *   - ONLY THE KEY IS OWNED. The value is a scalar held by value, so the append rollback
 *     neutralises the key slot but not the value slot - al_u64_remove releases nothing.
 *     That asymmetry is the one thing that does not survive the port unchanged.
 *   - at_* RETURNS A POINTER INTO THE VALUE ARRAY. Growth invalidates it, which is the
 *     OPPOSITE of the key rule, and the opposite of what map_char_char's header says about
 *     its own values. Writing through it is the supported way to change a value.
 *   - THERE IS NO NULLPTR-VALUE IDIOM. Nothing to null, so no "present with no value"
 *     state - and a stored ZERO is an ordinary value that only contains_* can tell from an
 *     absent key. That is why contains_* survives the port when key_add does not.
 *   - A ZERO IS NOT A SENTINEL. The removed key_add appended one, which no lookup could
 *     distinguish from a real zero. */

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

/* Larger than the tight arena, so a copy is refused by SIZE rather than by how much room
 * earlier copies happened to leave. */
#define _OVERSIZED_SIZE   4096

#define _POOL_BLOCK_COUNT 16
#define _POOL_BLOCK_SIZE  64

/*==============================================================================
 * MARK: - Internal
 *============================================================================*/

static USize _key_size(Map_Char_U64 *const map) {
    return al_char_get_size(map_char_u64_get_keys(map));
}

static USize _value_size(Map_Char_U64 *const map) {
    return al_u64_get_size(map_char_u64_get_values(map));
}

/*==============================================================================
 * MARK: - Cases
 *============================================================================*/

static void _test_add_adopts_the_key_only(Test *const test) {
    test_case_begin(test, "add adopts the key, holds the value, uninit releases the key");

    Map_Char_U64 map = map_char_u64_init_1();

    test_expect_true(test, "starts empty", map_char_u64_empty(&map));

    test_expect_true(test, "add a", map_char_u64_add(&map, char_new_2("alpha"), 1));
    test_expect_true(test, "add b", map_char_u64_add(&map, char_new_2("beta"), 2));

    test_expect_u(test, "size 2", 2, map_char_u64_get_size(&map));
    test_expect_u(test, "alpha reads back", 1, *map_char_u64_at_1(&map, "alpha"));
    test_expect_u(test, "beta reads back", 2, *map_char_u64_at_1(&map, "beta"));
    test_expect_null(test, "an absent key answers nullptr", map_char_u64_at_1(&map, "gamma"));

    test_expect_string(test, "indexed key 0", "alpha", map_char_u64_get_key(&map, 0));
    test_expect_u(test, "indexed value 0", 1, *map_char_u64_get_value(&map, 0));

    /* Only the two KEYS are freed by uninit; the values were never allocated. A regression
     * that tried to release the value side would abort here rather than pass quietly. */
    map_char_u64_uninit(&map);

    test_expect_u(test, "size 0 after uninit", 0, map_char_u64_get_size(&map));

    map_char_u64_uninit(&map);

    test_expect_u(test, "uninit is idempotent", 0, map_char_u64_get_size(&map));

    test_case_end(test);
}

static void _test_zero_is_a_value_not_a_sentinel(Test *const test) {
    test_case_begin(test, "a stored zero is a value; only contains tells it from absent");

    Map_Char_U64 map = map_char_u64_init_1();

    test_expect_true(test, "store a zero", map_char_u64_add_static(&map, "zero", 0));

    /* The whole reason contains_* replicates onto the scalar variants while key_add does
     * not. key_add appended 0 as a "no value yet" marker, which is INSIDE the domain: this
     * pair and a key_add'd one were indistinguishable through every accessor. */
    test_expect_not_null(test, "at finds it", map_char_u64_at_1(&map, "zero"));
    test_expect_u(test, "and the value is zero", 0, *map_char_u64_at_1(&map, "zero"));
    test_expect_true(test, "contains says present", map_char_u64_contains_1(&map, "zero"));

    test_expect_null(test, "an absent key is nullptr", map_char_u64_at_1(&map, "missing"));
    test_expect_false(test, "and contains says absent", map_char_u64_contains_1(&map, "missing"));

    map_char_u64_uninit(&map);

    test_case_end(test);
}

static void _test_at_points_into_the_array(Test *const test) {
    test_case_begin(test, "at returns a pointer INTO the value array; writing through it works");

    Map_Char_U64 map = map_char_u64_init_2(4);

    test_expect_true(test, "add", map_char_u64_add_static(&map, "k", 10));

    U64 *const slot = map_char_u64_at_1(&map, "k");

    test_expect_true(test, "the pointer is inside the value array", slot == al_u64_get_data(map_char_u64_get_values(&map)));

    /* The documented way to change a value on a scalar map, and the one place this shape is
     * easier to use than map_char_char - a U64 cannot outgrow its slot, so there is no
     * remove-then-add dance. */
    *slot = 99;

    test_expect_u(test, "the write landed", 99, *map_char_u64_at_1(&map, "k"));
    test_expect_u(test, "and through the index accessor too", 99, *map_char_u64_get_value(&map, 0));

    map_char_u64_uninit(&map);

    test_case_end(test);
}

static void _test_empty_key_is_a_value(Test *const test) {
    test_case_begin(test, "an empty key is looked up, not aborted on");

    Map_Char_U64 map = map_char_u64_init_1();

    /* The shape that killed a server on the pointer-valued canonical: a form body of "=x"
     * parses into a zero-length key, and at_2 ran that length through
     * error_check_non_value_uint. Two of this family's members still carry that abort. */
    test_expect_true(test, "an empty key can be stored", map_char_u64_add_static_2(&map, "", 0, 7));

    test_expect_u(test, "at_2 finds it by length 0", 7, *map_char_u64_at_2(&map, "", 0));
    test_expect_u(test, "at_1 finds it too", 7, *map_char_u64_at_1(&map, ""));
    test_expect_true(test, "contains_2 agrees", map_char_u64_contains_2(&map, "", 0));

    map_char_u64_clear(&map);

    test_expect_true(test, "add a normal pair", map_char_u64_add_static(&map, "k", 1));
    test_expect_null(test, "an empty key simply misses", map_char_u64_at_2(&map, "", 0));
    test_expect_u(test, "and the map is untouched", 1, map_char_u64_get_size(&map));

    map_char_u64_uninit(&map);

    test_case_end(test);
}

static void _test_size_is_derived(Test *const test) {
    test_case_begin(test, "get_size follows the lists rather than a stored counter");

    Map_Char_U64 map = map_char_u64_init_1();

    test_expect_true(test, "add one", map_char_u64_add_static(&map, "k", 1));
    test_expect_u(test, "size 1", 1, map_char_u64_get_size(&map));

    /* Written straight through the exposed handles, which the map cannot observe. The
     * `items` counter this round removed stayed at 1 here, so get_size disagreed with the
     * storage and every indexed read past the counter was invisible. */
    al_char_add_last(map_char_u64_get_keys(&map), char_new_2("k2"));

    test_expect_u(test, "a half pair does not count", 1, map_char_u64_get_size(&map));
    test_expect_u(test, "though the key list did grow", 2, _key_size(&map));

    al_u64_add_last(map_char_u64_get_values(&map), 2);

    test_expect_u(test, "completing the pair counts it", 2, map_char_u64_get_size(&map));
    test_expect_u(test, "and it is readable", 2, *map_char_u64_at_1(&map, "k2"));

    map_char_u64_uninit(&map);

    test_case_end(test);
}

static void _test_add_static_copies_the_key(Test *const test) {
    test_case_begin(test, "add_static copies the key, leaving the original with the caller");

    Map_Char_U64 map = map_char_u64_init_1();

    char source[8] = "value";

    test_expect_true(test, "add_static", map_char_u64_add_static(&map, source, 5));

    /* If the map had stored the caller's pointer, this write would change what it holds -
     * and uninit would then release a stack buffer. */
    source[0] = 'V';

    test_expect_true(test, "the copy is unaffected", map_char_u64_contains_1(&map, "value"));
    test_expect_true(test, "the copy is a different object", map_char_u64_get_key(&map, 0) != source);

    /* The sized form, storing a slice of a literal that is not terminated at that length. */
    char const *const pair = "colour=green";

    test_expect_true(test, "add_static_2 stores a slice", map_char_u64_add_static_2(&map, pair, 6, 42));
    test_expect_string(test, "terminated on the way in", "colour", map_char_u64_get_key(&map, 1));
    test_expect_u(test, "and reads back", 42, *map_char_u64_at_1(&map, "colour"));

    map_char_u64_uninit(&map);

    test_case_end(test);
}

static void _test_duplicate_keys_and_remove_at(Test *const test) {
    test_case_begin(test, "duplicates shadow; remove_at targets the index, remove_1 the first");

    Map_Char_U64 map = map_char_u64_init_1();

    test_expect_true(test, "add 0", map_char_u64_add_static(&map, "k", 1));
    test_expect_true(test, "add 1", map_char_u64_add_static(&map, "other", 2));
    test_expect_true(test, "add 2", map_char_u64_add_static(&map, "k", 3));

    test_expect_u(test, "lookup answers the first", 1, *map_char_u64_at_1(&map, "k"));
    test_expect_u(test, "index 2 is the shadowed duplicate", 3, *map_char_u64_get_value(&map, 2));

    map_char_u64_remove_at(&map, 2);

    test_expect_u(test, "two pairs left", 2, map_char_u64_get_size(&map));
    test_expect_u(test, "the FIRST k survived", 1, *map_char_u64_at_1(&map, "k"));

    test_expect_true(test, "remove_1 takes the first", map_char_u64_remove_1(&map, "k"));
    test_expect_false(test, "k is gone", map_char_u64_contains_1(&map, "k"));
    test_expect_false(test, "removing again reports false", map_char_u64_remove_1(&map, "k"));
    test_expect_u(test, "keys and values stay paired", _key_size(&map), _value_size(&map));

    map_char_u64_uninit(&map);

    test_case_end(test);
}

static void _test_capacity_is_the_smaller(Test *const test) {
    test_case_begin(test, "get_capacity reports the smaller of the two lists");

    Map_Char_U64 map = map_char_u64_init_2(4);

    test_expect_u(test, "both lists reserved", 4, map_char_u64_get_capacity(&map));

    al_char_reserve(map_char_u64_get_keys(&map), 16);

    test_expect_u(test, "growing one side does not raise it", 4, map_char_u64_get_capacity(&map));

    map_char_u64_reserve(&map, 16);

    test_expect_u(test, "the combined reserve moves both", 16, map_char_u64_get_capacity(&map));

    test_expect_true(test, "add", map_char_u64_add_static(&map, "k", 1));

    map_char_u64_shrink(&map);

    test_expect_u(test, "shrink drops to the size", 1, map_char_u64_get_capacity(&map));
    test_expect_u(test, "and the pair survived", 1, *map_char_u64_at_1(&map, "k"));

    map_char_u64_clear(&map);
    map_char_u64_shrink(&map);

    test_expect_u(test, "shrinking an empty map is legal", 0, map_char_u64_get_capacity(&map));

    map_char_u64_uninit(&map);

    test_case_end(test);
}

static void _test_init_3_deep_copies_the_keys(Test *const test) {
    test_case_begin(test, "init_3 deep-copies the keys and stops at the shorter list");

    AL_Char keys = al_char_init_1();
    AL_U64 values = al_u64_init_1();

    al_char_add_last(&keys, char_new_2("a"));
    al_char_add_last(&keys, char_new_2("b"));
    al_char_add_last(&keys, char_new_2("c"));
    al_u64_add_last(&values, 1);
    al_u64_add_last(&values, 2);

    Map_Char_U64 map = map_char_u64_init_3(&keys, &values);

    test_expect_u(test, "truncated to the shorter", 2, map_char_u64_get_size(&map));
    test_expect_u(test, "a pairs", 1, *map_char_u64_at_1(&map, "a"));
    test_expect_u(test, "b pairs", 2, *map_char_u64_at_1(&map, "b"));
    test_expect_null(test, "c was not taken", map_char_u64_at_1(&map, "c"));

    test_expect_true(test, "the stored key is a different object", map_char_u64_get_key(&map, 0) != al_char_at(&keys, 0));

    map_char_u64_uninit(&map);

    /* No hand-nulling: the keys were COPIED, so both source lists still hold everything
     * they ever held and release it normally. */
    test_expect_u(test, "the source kept every key", 3, al_char_get_size(&keys));

    al_char_uninit(&keys);
    al_u64_uninit(&values);

    test_case_end(test);
}

static void _test_null_key_element_survives_the_copy(Test *const test) {
    test_case_begin(test, "a null key element carried in by init_3 is skipped, not dereferenced");

    AL_Char keys = al_char_init_1();
    AL_U64 values = al_u64_init_1();

    al_char_add_last(&keys, char_new_2("a"));
    al_char_add_last(&keys, nullptr);
    al_char_add_last(&keys, char_new_2("c"));
    al_u64_add_last(&values, 1);
    al_u64_add_last(&values, 2);
    al_u64_add_last(&values, 3);

    /* add and add_static both refuse a null key, so the copying constructors are the ONLY
     * way a stored nullptr key can exist - which means _map_char_u64_index's null-key skip
     * passes purely because nothing reaches it otherwise. Handing char_length a null key is
     * a dereference, so the skip has to be real. */
    Map_Char_U64 map = map_char_u64_init_3(&keys, &values);

    test_expect_u(test, "all three pairs copied", 3, map_char_u64_get_size(&map));
    test_expect_null(test, "the null key stayed null", map_char_u64_get_key(&map, 1));
    test_expect_u(test, "its value came through", 2, *map_char_u64_get_value(&map, 1));

    test_expect_u(test, "a resolves", 1, *map_char_u64_at_1(&map, "a"));
    test_expect_u(test, "c resolves past it", 3, *map_char_u64_at_1(&map, "c"));
    test_expect_false(test, "and a miss scans the whole list", map_char_u64_contains_1(&map, "zzz"));

    map_char_u64_uninit(&map);

    al_char_uninit(&keys);
    al_u64_uninit(&values);

    test_case_end(test);
}

static void _test_refused_allocator_declines(Test *const test) {
    test_case_begin(test, "a refused allocator declines the add and takes nothing");

    Arena dead = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    Map_Char_U64 map = map_char_u64_alloc_init_2(4, &dead);

    test_expect_u(test, "capacity zeroed to match the storage", 0, map_char_u64_get_capacity(&map));

    /* A heap key, so a regressed decline that actually stored it would hand a heap pointer
     * to the ARENA on uninit rather than passing quietly. */
    char *const key = char_new_2("key");

    test_expect_false(test, "add declined", map_char_u64_add(&map, key, 5));
    test_expect_u(test, "nothing was stored", 0, map_char_u64_get_size(&map));
    test_expect_u(test, "no orphan key", 0, _key_size(&map));
    test_expect_u(test, "no orphan value", 0, _value_size(&map));
    test_expect_string(test, "the caller still owns the key", "key", key);

    char_delete(key);

    test_expect_false(test, "add_static declines too", map_char_u64_add_static(&map, "k", 1));

    /* The struct borrow, not the element buffers. */
    test_expect_null(test, "alloc_new_1 declined", map_char_u64_alloc_new_1(&dead));
    test_expect_null(test, "alloc_new_2 declined", map_char_u64_alloc_new_2(4, &dead));

    map_char_u64_uninit(&map);
    arena_uninit(&dead, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_half_refused_add_rolls_back(Test *const test) {
    test_case_begin(test, "an add whose value half is refused leaves no orphan key");

    Arena dead = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    /* Built by hand from two lists with DIFFERENT allocators, the only way to reach the
     * asymmetric branch through the public surface. This orientation reaches the KEY arm
     * only - the value arm is guarded on the value append having landed, which a dead value
     * list never does. The mirror case below covers it. */
    Map_Char_U64 map = map_char_u64_init_1();

    map.value = al_u64_alloc_init_1(&dead);

    char *const key = char_new_2("orphan");

    test_expect_false(test, "the add declined", map_char_u64_add(&map, key, 1));

    test_expect_u(test, "the key was rolled back", 0, _key_size(&map));
    test_expect_u(test, "the value list is untouched", 0, _value_size(&map));
    test_expect_string(test, "and the key is still the caller's", "orphan", key);

    /* Positive control: without it the case would pass just as well if the KEY half were the
     * refused one, and would pin nothing about the rollback. */
    al_char_add_last(map_char_u64_get_keys(&map), key);

    test_expect_u(test, "the key half could have grown", 1, _key_size(&map));
    test_expect_u(test, "while the value half still cannot", 0, _value_size(&map));

    map_char_u64_uninit(&map);
    arena_uninit(&dead, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_half_refused_add_rolls_the_value_back(Test *const test) {
    test_case_begin(test, "the mirror orientation: a refused KEY half rolls the value back");

    Arena dead = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    /* KEY list on the dead arena, value list on the heap - the opposite of the case above,
     * and a separate case because each orientation reaches a different ARM of the rollback.
     * The arms are guarded independently on which half actually grew, so a suite with only
     * one orientation leaves the other unexecuted: with the dead arena on the VALUE list,
     * map_char_u64.c's value arm never runs, and deleting it leaves the suite green while
     * its comment still explains what it does. */
    Map_Char_U64 map = map_char_u64_init_1();

    map.key = al_char_alloc_init_1(&dead);

    /* add, NOT add_static. add_static copies the key through the KEY list's allocator, which
     * here is the dead arena - so it declines at the copy and returns before _append is ever
     * called, and the rollback is never reached. The first version of this case did exactly
     * that: it asserted both sizes were 0, which they were, for a reason that had nothing to
     * do with the arm it claimed to pin. Injection caught it - deleting the value arm left
     * the case green. A caller-supplied key skips the copy and reaches the append. */
    char *const key = char_new_2("orphan");

    test_expect_false(test, "the add declined", map_char_u64_add(&map, key, 7));

    test_expect_u(test, "the key half never grew", 0, _key_size(&map));
    test_expect_u(test, "and the value half was rolled back", 0, _value_size(&map));
    test_expect_u(test, "so the map is empty", 0, map_char_u64_get_size(&map));

    /* Positive control: the value list CAN grow, so the decline came from the key side and
     * the value arm is what undid it. Without this the case would pass identically if both
     * halves were refused, and would pin nothing about the arm it exists for. */
    al_u64_add_last(map_char_u64_get_values(&map), 1);

    test_expect_u(test, "the value half could have grown", 1, _value_size(&map));
    test_expect_u(test, "while the key half still cannot", 0, _key_size(&map));

    /* The decline took nothing, so the key is still this frame's to release. */
    char_delete(key);

    map_char_u64_uninit(&map);
    arena_uninit(&dead, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_add_static_key_copy_refused(Test *const test) {
    test_case_begin(test, "add_static declines when the key copy is refused, leaking nothing");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    Map_Char_U64 map = map_char_u64_alloc_init_1(&arena);

    char *const oversized = char_new_1(_OVERSIZED_SIZE);

    char_fill(oversized, _OVERSIZED_SIZE, 'x');

    // Belt only: char_new_1(N) zeroes N+1 bytes, so the terminator already reads 0.
    oversized[_OVERSIZED_SIZE] = '\0';

    test_expect_true(test, "a short key fits", map_char_u64_add_static(&map, "fits", 1));

    /* Refused by SIZE: the key copy is larger than the whole arena, so try_borrow answers
     * nullptr and add_static declines before appending anything. */
    test_expect_false(test, "the oversized key is refused", map_char_u64_add_static(&map, oversized, 2));

    test_expect_u(test, "nothing was added for it", 1, map_char_u64_get_size(&map));
    test_expect_u(test, "and the earlier pair is untouched", 1, *map_char_u64_at_1(&map, "fits"));

    char_delete(oversized);

    map_char_u64_uninit(&map);
    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_add_static_append_refused_releases_the_copy(Test *const test) {
    test_case_begin(test, "add_static gives back the key copy when the APPEND is refused");

    /* The other half of add_static's decline contract, and the half the copy-refused case
     * above cannot reach: there the copy never succeeds, so there is nothing to give back.
     * Here the copy succeeds and the APPEND declines, which is the only path on which
     * add_static owns a block it must release.
     *
     * A pool for the key side, because arena_linear_free is a no-op and would swallow the
     * release being measured; a dead arena for the value side, so the append declines. */
    Arena pool = arena_init_2(_POOL_BLOCK_SIZE, _POOL_BLOCK_COUNT, ARENA_TYPE_POOL);
    Arena dead = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    void *const baseline = allocator_try_borrow(_POOL_BLOCK_SIZE * _POOL_BLOCK_COUNT, &pool);

    test_expect_not_null(test, "the pool starts whole", baseline);

    allocator_release(baseline, &pool);

    Map_Char_U64 map = map_char_u64_init_1();

    map.key = al_char_alloc_init_1(&pool);
    map.value = al_u64_alloc_init_1(&dead);

    test_expect_false(test, "add_static declined", map_char_u64_add_static(&map, "k", 1));
    test_expect_u(test, "nothing was stored", 0, map_char_u64_get_size(&map));
    test_expect_u(test, "and no orphan key", 0, _key_size(&map));

    /* uninit first, so the key list hands its own array back; what remains outstanding is
     * then exactly the key copy, if it was leaked. */
    map_char_u64_uninit(&map);

    void *const after = allocator_try_borrow(_POOL_BLOCK_SIZE * _POOL_BLOCK_COUNT, &pool);

    test_expect_not_null(test, "the pool is whole again - the key copy was given back", after);

    allocator_release(after, &pool);
    arena_uninit(&pool, ARENA_TYPE_POOL);
    arena_uninit(&dead, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_arena_round_trip(Test *const test) {
    test_case_begin(test, "an arena-backed map stores, reads, deletes");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    Map_Char_U64 *map = map_char_u64_alloc_new_2(4, &arena);

    test_expect_not_null(test, "alloc_new_2 allocated", map);
    test_expect_u(test, "capacity honoured", 4, map_char_u64_get_capacity(map));

    /* add_static copies the key through the map's OWN allocator, which is the whole reason
     * the copy helper takes the key list rather than the map. */
    test_expect_true(test, "add_static", map_char_u64_add_static(map, "k", 7));
    test_expect_u(test, "reads back", 7, *map_char_u64_at_1(map, "k"));

    test_expect_true(test, "remove releases through the arena", map_char_u64_remove_1(map, "k"));
    test_expect_true(test, "empty", map_char_u64_empty(map));

    map_char_u64_delete(&map);

    test_expect_null(test, "delete nulled the caller's pointer", map);

    Map_Char_U64 *heap = map_char_u64_new_1();

    test_expect_not_null(test, "new_1 allocated", heap);
    test_expect_true(test, "and is usable", map_char_u64_add_static(heap, "h", 1));

    map_char_u64_delete(&heap);

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_remove_2_measures_the_query_key_by_size(Test *const test) {
    test_case_begin(test, "the _2 forms take a size, so a slice of a buffer needs no copy");

    Map_Char_U64 map = map_char_u64_init_1();

    /* One buffer, two keys inside it, neither terminated where its key ends. */
    char const *const packed = "alphabetagamma";

    test_expect_true(test, "add_static_2 copied the first slice", map_char_u64_add_static_2(&map, packed, 5, 1));
    test_expect_true(test, "and the second", map_char_u64_add_static_2(&map, packed + 5, 4, 2));

    /* The COPY is terminated even though the source was not: lookup measures the STORED key
     * with char_length, so it has to be. */
    test_expect_string(test, "the stored key is terminated", "alpha", map_char_u64_get_key(&map, 0));
    test_expect_string(test, "and so is the second", "beta", map_char_u64_get_key(&map, 1));

    U64 *const found = map_char_u64_at_2(&map, packed, 5);

    /* Guarded rather than dereferenced inline: this case exists to catch a regression in the
     * _2 forms, and a regression that returns nullptr should print a red assertion here, not
     * segfault three lines into the suite. Same reasoning below. */
    test_expect_not_null(test, "at_2 finds the slice", found);
    test_expect_u(test, "with the value that was stored", 1, memory_empty(found) ? 0 : *found);
    test_expect_true(test, "contains_2 agrees", map_char_u64_contains_2(&map, packed + 5, 4));

    /* The size is the WHOLE comparison, not an upper bound on it: a prefix is not a match. */
    test_expect_false(test, "a shorter slice does not match", map_char_u64_contains_2(&map, packed, 4));
    test_expect_false(test, "nor does a longer one", map_char_u64_contains_2(&map, packed, 6));

    test_expect_true(test, "remove_2 removed the slice", map_char_u64_remove_2(&map, packed + 5, 4));
    test_expect_u(test, "one pair left", 1, map_char_u64_get_size(&map));
    test_expect_false(test, "and it is gone", map_char_u64_contains_2(&map, packed + 5, 4));
    test_expect_false(test, "removing it again removes nothing", map_char_u64_remove_2(&map, packed + 5, 4));
    test_expect_true(test, "the other pair is untouched", map_char_u64_contains_2(&map, packed, 5));

    map_char_u64_uninit(&map);

    test_case_end(test);
}

static void _test_exhausted_arena_declines_the_struct(Test *const test) {
    test_case_begin(test, "a LIVE but too-small arena declines the struct borrow rather than aborting");

    AL_Char keys = al_char_init_1();
    AL_U64 values = al_u64_init_1();

    al_char_add_last(&keys, char_new_2("a"));
    al_u64_add_last(&values, 1);

    /* A REPLICATION TEMPLATE: an arena whose total capacity is exactly MEMORY_ALIGNMENT holds
     * strictly less than MEMORY_ALIGN_UP(sizeof(T)) for every T in the family (all 64-80
     * bytes), so the struct borrow declines regardless of which map is under test. The 1-byte
     * probe proves the arena is LIVE - not a dead null-handler - without depending on sizeof
     * this instantiation's struct. See map_char_char's suite for the full derivation. */
    Arena tiny = arena_init_2(MEMORY_ALIGNMENT, 1, ARENA_TYPE_LINEAR);

    void *const probe = allocator_try_borrow(1, &tiny);

    test_expect_not_null(test, "a 1-byte probe succeeds: the arena is live", probe);

    allocator_release(probe, &tiny);

    test_expect_null(test, "alloc_new_1 declines an EXHAUSTED arena", map_char_u64_alloc_new_1(&tiny));
    test_expect_null(test, "alloc_new_2 too", map_char_u64_alloc_new_2(4, &tiny));
    test_expect_null(test, "alloc_new_3 too", map_char_u64_alloc_new_3(&keys, &values, &tiny));

    arena_uninit(&tiny, ARENA_TYPE_LINEAR);

    al_char_uninit(&keys);
    al_u64_uninit(&values);

    test_case_end(test);
}

static void _test_embedded_nul_key_matches_by_leading_segment(Test *const test) {
    test_case_begin(test, "a key holding an embedded NUL is stored, but matches only its LEADING segment");

    Map_Char_U64 map = map_char_u64_init_1();

    /* "a\0b", 3 bytes: the STORED key is measured with char_length, which stops at the
     * embedded NUL - so every lookup after storage sees "a" and nothing past it. */
    test_expect_true(test, "store the 3-byte key", map_char_u64_add_static_2(&map, "a\0b", 3, 42));

    test_expect_u(test, "at_2 finds it by the LEADING segment", 42, *map_char_u64_at_2(&map, "a", 1));
    test_expect_null(test, "at_2 does NOT find it by the full 3 bytes", map_char_u64_at_2(&map, "a\0b", 3));
    test_expect_true(test, "remove_2 removes it by the leading segment too", map_char_u64_remove_2(&map, "a", 1));
    test_expect_u(test, "gone", 0, map_char_u64_get_size(&map));

    map_char_u64_uninit(&map);

    test_case_end(test);
}

static void _test_heap_and_arena_copy_constructors(Test *const test) {
    test_case_begin(test, "new_2/new_3 and the arena copy twins all build a usable map");

    Map_Char_U64 *sized = map_char_u64_new_2(6);

    test_expect_not_null(test, "new_2 allocated", sized);
    test_expect_u(test, "with the capacity asked for", 6, map_char_u64_get_capacity(sized));
    test_expect_true(test, "and is usable", map_char_u64_add_static(sized, "k", 7));

    U64 *const stored = map_char_u64_at_1(sized, "k");

    test_expect_not_null(test, "reading back", stored);
    test_expect_u(test, "the value", 7, memory_empty(stored) ? 0 : *stored);

    map_char_u64_delete(&sized);

    test_expect_null(test, "delete nulled the caller's pointer", sized);

    /* The copying constructors. The KEYS are deep-copied and the values are scalars, so the
     * sources below stay whole and are released exactly once - by their owner, at the end. */
    AL_Char keys = al_char_init_1();
    AL_U64 values = al_u64_init_1();

    al_char_add_last(&keys, char_new_2("k"));
    al_u64_add_last(&values, 42);

    Map_Char_U64 *copied = map_char_u64_new_3(&keys, &values);

    test_expect_not_null(test, "new_3 allocated", copied);

    U64 *const from_new_3 = map_char_u64_at_1(copied, "k");

    test_expect_not_null(test, "and carries the key", from_new_3);
    test_expect_u(test, "and copied the pair", 42, memory_empty(from_new_3) ? 0 : *from_new_3);
    test_expect_true(test, "into a different key buffer", map_char_u64_get_key(copied, 0) != al_char_at(&keys, 0));

    map_char_u64_delete(&copied);

    Arena arena = arena_init_1(8192, ARENA_TYPE_LINEAR);

    Map_Char_U64 on_arena = map_char_u64_alloc_init_3(&keys, &values, &arena);

    U64 *const from_alloc_init_3 = map_char_u64_at_1(&on_arena, "k");

    test_expect_not_null(test, "alloc_init_3 carries the key", from_alloc_init_3);
    test_expect_u(test, "and copied the pair", 42, memory_empty(from_alloc_init_3) ? 0 : *from_alloc_init_3);
    test_expect_true(test, "into a different key buffer", map_char_u64_get_key(&on_arena, 0) != al_char_at(&keys, 0));

    map_char_u64_uninit(&on_arena);

    Map_Char_U64 *on_arena_heap = map_char_u64_alloc_new_3(&keys, &values, &arena);

    test_expect_not_null(test, "alloc_new_3 allocated", on_arena_heap);

    U64 *const from_alloc_new_3 = map_char_u64_at_1(on_arena_heap, "k");

    test_expect_not_null(test, "and carries the key", from_alloc_new_3);
    test_expect_u(test, "and copied the pair", 42, memory_empty(from_alloc_new_3) ? 0 : *from_alloc_new_3);

    map_char_u64_delete(&on_arena_heap);

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    /* Three constructors copied out of these two sources and none of them took anything. */
    test_expect_u(test, "the source keys survived", 1, al_char_get_size(&keys));
    test_expect_string(test, "readable", "k", al_char_at(&keys, 0));
    test_expect_u(test, "and the source values", 42, memory_empty(al_u64_at(&values, 0)) ? 0 : *al_u64_at(&values, 0));

    al_char_uninit(&keys);
    al_u64_uninit(&values);

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

    Test test = test_init("tests/container/map/test_map_char_u64.c");

    test_suite_begin(&test, "map_char_u64");
    _test_add_adopts_the_key_only(&test);
    _test_zero_is_a_value_not_a_sentinel(&test);
    _test_at_points_into_the_array(&test);
    _test_empty_key_is_a_value(&test);
    _test_size_is_derived(&test);
    _test_add_static_copies_the_key(&test);
    _test_duplicate_keys_and_remove_at(&test);
    _test_capacity_is_the_smaller(&test);
    _test_init_3_deep_copies_the_keys(&test);
    _test_null_key_element_survives_the_copy(&test);
    _test_refused_allocator_declines(&test);
    _test_half_refused_add_rolls_back(&test);
    _test_half_refused_add_rolls_the_value_back(&test);
    _test_add_static_key_copy_refused(&test);
    _test_add_static_append_refused_releases_the_copy(&test);
    _test_arena_round_trip(&test);
    _test_exhausted_arena_declines_the_struct(&test);
    _test_embedded_nul_key_matches_by_leading_segment(&test);
    _test_remove_2_measures_the_query_key_by_size(&test);
    _test_heap_and_arena_copy_constructors(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}