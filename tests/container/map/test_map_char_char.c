#include <test/test.h>

#include <container/map/map_char_char.h>
#include <arena/arena.h>

/* Suite for Map_Char_Char, the canonical form the other eight map instantiations are
 * replicated onto. What it pins is therefore the CONTRACT, not just this file's behaviour:
 *
 *   - THE MAP ADOPTS. add() stores the caller's pointers verbatim and clear/remove/uninit
 *     release them through the lists' own allocator. Every key and value handed to add()
 *     below is a heap copy, never a literal, so a regression that stops adopting shows up
 *     as a leak or a heap abort rather than as a quiet pass.
 *   - THE SIZE IS DERIVED. There is no third counter beside the two lists' own sizes, so
 *     writing through get_keys/get_values is observable rather than desyncing.
 *   - AN EMPTY KEY IS A VALUE. A zero-length key must reach a lookup as a lookup, not as
 *     an abort: a request body of "=x" parses into one, and this module sits behind an
 *     HTTP body parser. (The convergence removed that abort from every sibling too.)
 *   - A DECLINE TAKES NOTHING. add() returns false and leaves both pointers with the caller,
 *     with no orphan half-pair left behind to mis-pair the next successful add.
 *   - ALL OR NOTHING CONSTRUCTION. init_3 and its twins either copy every pair or come
 *     back empty - a refused allocator never yields a partial map.
 *   - PER-LIST ALLOCATOR PROVENANCE. Each half's copy is borrowed from, and released to,
 *     the allocator of the list it lands in - the property the decline rollback depends
 *     on, and the reason a cross-allocator release cannot happen inside the map. */

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

/* Larger than the tight arena and than the whole pool, so pair 1's value is refused by SIZE
 * rather than by how much room the earlier copies happened to leave. */
#define _OVERSIZED_SIZE   4096

#define _POOL_BLOCK_COUNT 16
#define _POOL_BLOCK_SIZE  64

/*==============================================================================
 * MARK: - Internal
 *============================================================================*/

static USize _key_size(Map_Char_Char *const map) {
    return al_char_get_size(map_char_char_get_keys(map));
}

static USize _value_size(Map_Char_Char *const map) {
    return al_char_get_size(map_char_char_get_values(map));
}

/*==============================================================================
 * MARK: - Cases
 *============================================================================*/

static void _test_add_adopts_and_reads_back(Test *const test) {
    test_case_begin(test, "add adopts, at_1 reads back, uninit releases");

    Map_Char_Char map = map_char_char_init_1();

    test_expect_true(test, "starts empty", map_char_char_empty(&map));
    test_expect_u(test, "size 0", 0, map_char_char_get_size(&map));

    test_expect_true(test, "add a", map_char_char_add(&map, char_new_2("alpha"), char_new_2("1")));
    test_expect_true(test, "add b", map_char_char_add(&map, char_new_2("beta"), char_new_2("2")));

    test_expect_u(test, "size 2", 2, map_char_char_get_size(&map));
    test_expect_false(test, "not empty", map_char_char_empty(&map));
    test_expect_string(test, "alpha reads back", "1", map_char_char_at_1(&map, "alpha"));
    test_expect_string(test, "beta reads back", "2", map_char_char_at_1(&map, "beta"));
    test_expect_null(test, "an absent key answers nullptr", map_char_char_at_1(&map, "gamma"));

    /* No cast on either side. The return type used to be `char *const`, which -Wextra
     * flags on every includer and which body_parser cast away at the call site; this
     * assignment is the compile-time half of that fix. */
    char *const mutable_value = map_char_char_at_1(&map, "alpha");

    test_expect_not_null(test, "the value is handed back mutable", mutable_value);

    test_expect_string(test, "indexed key 0", "alpha", map_char_char_get_key(&map, 0));
    test_expect_string(test, "indexed value 0", "1", map_char_char_get_value(&map, 0));
    test_expect_string(test, "indexed key 1", "beta", map_char_char_get_key(&map, 1));
    test_expect_string(test, "indexed value 1", "2", map_char_char_get_value(&map, 1));

    /* Nothing is freed by hand here: the four strings above belong to the map now, and a
     * regression that stops adopting them turns this uninit into a double free. */
    map_char_char_uninit(&map);

    test_expect_u(test, "size 0 after uninit", 0, map_char_char_get_size(&map));
    test_expect_u(test, "capacity 0 after uninit", 0, map_char_char_get_capacity(&map));

    map_char_char_uninit(&map);

    test_expect_u(test, "uninit is idempotent", 0, map_char_char_get_size(&map));

    test_case_end(test);
}

static void _test_empty_key_is_a_value(Test *const test) {
    test_case_begin(test, "an empty key is looked up, not aborted on");

    Map_Char_Char map = map_char_char_init_1();

    /* The shape that killed a server: a form body of "=x" parses into a zero-length key,
     * and at_2 ran that length through error_check_non_value_uint. Storing one and reading
     * it back proves the guard is gone in BOTH directions - the abort would fire on the
     * lookup even when nothing empty had ever been stored. */
    test_expect_true(test, "an empty key can be stored", map_char_char_add(&map, char_new_2(""), char_new_2("x")));

    test_expect_string(test, "at_2 finds it by length 0", "x", map_char_char_at_2(&map, "", 0));
    test_expect_string(test, "at_1 finds it too", "x", map_char_char_at_1(&map, ""));
    test_expect_true(test, "contains_2 agrees", map_char_char_contains_2(&map, "", 0));

    map_char_char_clear(&map);

    /* And the lookup alone, against a map that holds no empty key at all. */
    test_expect_true(test, "add a normal pair", map_char_char_add(&map, char_new_2("k"), char_new_2("v")));
    test_expect_null(test, "an empty key simply misses", map_char_char_at_2(&map, "", 0));
    test_expect_false(test, "contains says so", map_char_char_contains_2(&map, "", 0));
    test_expect_u(test, "and the map is untouched", 1, map_char_char_get_size(&map));

    map_char_char_uninit(&map);

    test_case_end(test);
}

static void _test_size_is_derived(Test *const test) {
    test_case_begin(test, "get_size follows the lists rather than a stored counter");

    Map_Char_Char map = map_char_char_init_1();

    test_expect_true(test, "add one", map_char_char_add(&map, char_new_2("k"), char_new_2("v")));
    test_expect_u(test, "size 1", 1, map_char_char_get_size(&map));

    /* Written straight through the exposed handles, which the map cannot observe. A stored
     * third counter stayed at 1 here, so get_size disagreed with the storage and every
     * indexed read past the counter was invisible. */
    al_char_add_last(map_char_char_get_keys(&map), char_new_2("k2"));

    test_expect_u(test, "a half pair does not count", 1, map_char_char_get_size(&map));
    test_expect_u(test, "though the key list did grow", 2, _key_size(&map));

    al_char_add_last(map_char_char_get_values(&map), char_new_2("v2"));

    test_expect_u(test, "completing the pair counts it", 2, map_char_char_get_size(&map));
    test_expect_string(test, "and it is readable", "v2", map_char_char_at_1(&map, "k2"));

    /* The minimum works downward too: dropping a value hides the pair again, and the key
     * left behind is still released by uninit. */
    al_char_remove(map_char_char_get_values(&map), 1);

    test_expect_u(test, "losing the value hides the pair", 1, map_char_char_get_size(&map));
    test_expect_null(test, "and its key stops resolving", map_char_char_at_1(&map, "k2"));

    map_char_char_uninit(&map);

    test_case_end(test);
}

static void _test_null_value_versus_absent(Test *const test) {
    test_case_begin(test, "a null value is stored; contains tells it from absent");

    Map_Char_Char map = map_char_char_init_1();

    /* add(key, nullptr) IS the "key with no value yet" operation - key_add was an exact
     * alias of it and is gone. On the scalar variants the alias was worse than redundant:
     * it appended 0, a value inside the domain that no lookup could tell from a real one. */
    test_expect_true(test, "add with a null value", map_char_char_add(&map, char_new_2("pending"), nullptr));

    test_expect_u(test, "the pair counts", 1, map_char_char_get_size(&map));
    test_expect_null(test, "its value is null", map_char_char_at_1(&map, "pending"));
    test_expect_true(test, "but the key is present", map_char_char_contains_1(&map, "pending"));
    test_expect_null(test, "an absent key is also null", map_char_char_at_1(&map, "missing"));
    test_expect_false(test, "and contains separates the two", map_char_char_contains_1(&map, "missing"));

    /* A stored nullptr must survive the scan rather than reach char_length. */
    test_expect_true(test, "add another after it", map_char_char_add(&map, char_new_2("done"), char_new_2("yes")));
    test_expect_string(test, "scanning past the null key works", "yes", map_char_char_at_1(&map, "done"));

    map_char_char_uninit(&map);

    test_case_end(test);
}

static void _test_add_static_copies(Test *const test) {
    test_case_begin(test, "add_static copies, leaving the original with the caller");

    Map_Char_Char map = map_char_char_init_1();

    char source[8] = "value";

    test_expect_true(test, "add_static", map_char_char_add_static(&map, "key", source));

    /* If the map had stored the caller's pointer, this write would change what it holds -
     * and uninit would then release a stack buffer and a .rodata literal. */
    source[0] = 'V';

    test_expect_string(test, "the copy is unaffected", "value", map_char_char_at_1(&map, "key"));
    test_expect_true(test, "the copy is a different object", map_char_char_at_1(&map, "key") != source);

    test_expect_true(test, "a null value is legal", map_char_char_add_static(&map, "empty", nullptr));
    test_expect_null(test, "and stays null", map_char_char_at_1(&map, "empty"));
    test_expect_true(test, "while the key is present", map_char_char_contains_1(&map, "empty"));

    test_expect_true(test, "an empty value copies too", map_char_char_add_static(&map, "blank", ""));
    test_expect_string(test, "as an empty string", "", map_char_char_at_1(&map, "blank"));

    map_char_char_uninit(&map);

    test_case_end(test);
}

static void _test_duplicate_keys(Test *const test) {
    test_case_begin(test, "a duplicate key shadows; lookup and remove both take the first");

    Map_Char_Char map = map_char_char_init_1();

    test_expect_true(test, "add first", map_char_char_add(&map, char_new_2("k"), char_new_2("first")));
    test_expect_true(test, "add duplicate", map_char_char_add(&map, char_new_2("k"), char_new_2("second")));

    test_expect_u(test, "both pairs are stored", 2, map_char_char_get_size(&map));
    test_expect_string(test, "lookup answers the first", "first", map_char_char_at_1(&map, "k"));

    test_expect_true(test, "remove reports a removal", map_char_char_remove_1(&map, "k"));
    test_expect_u(test, "one pair left", 1, map_char_char_get_size(&map));

    /* The shadowed entry surfaces rather than the key disappearing - the documented
     * consequence of there being no replacing add. */
    test_expect_string(test, "the shadowed pair surfaces", "second", map_char_char_at_1(&map, "k"));

    test_expect_true(test, "remove it too", map_char_char_remove_1(&map, "k"));
    test_expect_false(test, "now it is gone", map_char_char_contains_1(&map, "k"));
    test_expect_false(test, "and removing again reports false", map_char_char_remove_1(&map, "k"));
    test_expect_true(test, "the map is empty", map_char_char_empty(&map));

    map_char_char_uninit(&map);

    test_case_end(test);
}

static void _test_remove_keeps_the_pairing(Test *const test) {
    test_case_begin(test, "remove drops both halves at the same index");

    Map_Char_Char map = map_char_char_init_1();

    test_expect_true(test, "add a", map_char_char_add(&map, char_new_2("a"), char_new_2("1")));
    test_expect_true(test, "add b", map_char_char_add(&map, char_new_2("b"), char_new_2("2")));
    test_expect_true(test, "add c", map_char_char_add(&map, char_new_2("c"), char_new_2("3")));

    test_expect_true(test, "remove the middle", map_char_char_remove_1(&map, "b"));

    test_expect_u(test, "size 2", 2, map_char_char_get_size(&map));
    test_expect_u(test, "keys 2", 2, _key_size(&map));
    test_expect_u(test, "values 2", 2, _value_size(&map));
    test_expect_string(test, "a still pairs", "1", map_char_char_at_1(&map, "a"));
    test_expect_string(test, "c still pairs", "3", map_char_char_at_1(&map, "c"));
    test_expect_string(test, "index 1 is now c", "c", map_char_char_get_key(&map, 1));
    test_expect_string(test, "and its value moved with it", "3", map_char_char_get_value(&map, 1));

    /* remove_2 finds by the same bytes at_2 does. */
    test_expect_true(test, "remove_2 by explicit size", map_char_char_remove_2(&map, "a", 1));
    test_expect_false(test, "gone", map_char_char_contains_1(&map, "a"));
    test_expect_false(test, "a partial key does not match", map_char_char_remove_2(&map, "cc", 2));
    test_expect_u(test, "still one pair", 1, map_char_char_get_size(&map));

    map_char_char_uninit(&map);

    test_case_end(test);
}

static void _test_capacity_is_the_smaller(Test *const test) {
    test_case_begin(test, "get_capacity reports the smaller of the two lists");

    Map_Char_Char map = map_char_char_init_2(4);

    test_expect_u(test, "both lists reserved", 4, map_char_char_get_capacity(&map));

    /* Through the get_keys handle, because the four per-list reserve/shrink wrappers are
     * gone - nothing in the tree ever called them, and they were the only public way to
     * diverge the two capacities, which is the whole reason get_capacity answers a min.
     * The handle is the documented escape hatch, so the divergence it permits is what needs
     * pinning now. */
    al_char_reserve(map_char_char_get_keys(&map), 16);

    /* The key list alone was what this used to report, so it promised twelve free slots a
     * value-side append would still have to grow into. */
    test_expect_u(test, "growing one side does not raise it", 4, map_char_char_get_capacity(&map));

    al_char_reserve(map_char_char_get_values(&map), 16);

    test_expect_u(test, "growing both does", 16, map_char_char_get_capacity(&map));

    test_expect_true(test, "add", map_char_char_add(&map, char_new_2("k"), char_new_2("v")));

    map_char_char_shrink(&map);

    test_expect_u(test, "shrink drops to the size", 1, map_char_char_get_capacity(&map));
    test_expect_string(test, "and the pair survived", "v", map_char_char_at_1(&map, "k"));

    map_char_char_clear(&map);

    test_expect_true(test, "clear empties", map_char_char_empty(&map));

    /* al_char_shrink releases and resets an empty list rather than borrowing zero bytes,
     * which memory_alloc aborts on; the map must inherit that, not re-impose a guard. */
    map_char_char_shrink(&map);

    test_expect_u(test, "shrinking an empty map is legal", 0, map_char_char_get_capacity(&map));

    map_char_char_uninit(&map);

    test_case_end(test);
}

static void _test_init_3_deep_copies_and_truncates(Test *const test) {
    test_case_begin(test, "init_3 deep-copies and stops at the shorter list");

    AL_Char keys = al_char_init_1();
    AL_Char values = al_char_init_1();

    al_char_add_last(&keys, char_new_2("a"));
    al_char_add_last(&keys, char_new_2("b"));
    al_char_add_last(&keys, char_new_2("c"));
    al_char_add_last(&values, char_new_2("1"));
    al_char_add_last(&values, char_new_2("2"));

    Map_Char_Char map = map_char_char_init_3(&keys, &values);

    test_expect_u(test, "truncated to the shorter", 2, map_char_char_get_size(&map));
    test_expect_string(test, "a pairs", "1", map_char_char_at_1(&map, "a"));
    test_expect_string(test, "b pairs", "2", map_char_char_at_1(&map, "b"));
    test_expect_null(test, "c was not taken", map_char_char_at_1(&map, "c"));

    /* The STRINGS are copied, not adopted - which is what makes the source lists still the
     * caller's to release normally. Writing through a source element must not reach the map. */
    test_expect_true(test, "the stored key is a different object", map_char_char_get_key(&map, 0) != al_char_at(&keys, 0));

    al_char_at(&values, 0)[0] = 'X';

    test_expect_string(test, "the map's copy is unaffected", "1", map_char_char_at_1(&map, "a"));

    /* The ARRAY is copied too, so appending to a source afterwards does not reach the map. */
    al_char_add_last(&values, char_new_2("3"));

    test_expect_u(test, "the map did not follow the source", 2, map_char_char_get_size(&map));

    map_char_char_uninit(&map);

    /* No hand-nulling. Both lists still hold every string they ever held, and al_char_uninit
     * releases all six. The version of this case that adopted needed the caller to null the
     * taken slots by hand - an idiom al_char has no primitive for, which is exactly why the
     * adopting contract could not be honoured. */
    test_expect_u(test, "the sources kept every key", 3, al_char_get_size(&keys));
    test_expect_u(test, "and every value", 3, al_char_get_size(&values));

    al_char_uninit(&keys);
    al_char_uninit(&values);

    test_case_end(test);
}

static void _test_mixed_allocator_add_static(Test *const test) {
    test_case_begin(test, "add_static copies each half through its OWN list's allocator");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    /* Keys on the arena, values on the heap. Nothing this file's constructors build looks
     * like this, but the struct is public and get_keys/get_values are documented mutable
     * handles, so it is reachable - and it is the only shape in which the bug shows.
     *
     * add_static used to borrow BOTH copies from the key list's allocator and then store one
     * of them in the value list, whose teardown releases through the value list's allocator.
     * With this orientation that put an arena-interior pointer into free(): an immediate
     * abort, not a quiet leak. */
    Map_Char_Char map = map_char_char_init_1();

    map.key = al_char_alloc_init_1(&arena);

    test_expect_true(test, "add_static", map_char_char_add_static(&map, "key", "value"));

    test_expect_string(test, "the key copied", "key", map_char_char_get_key(&map, 0));
    test_expect_string(test, "the value copied", "value", map_char_char_at_1(&map, "key"));

    /* Each half goes back to the allocator it came from. If the copy helper ever takes the
     * map again instead of the target list, this line is where it dies. */
    map_char_char_uninit(&map);

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_expect_u(test, "released cleanly", 0, map_char_char_get_size(&map));

    test_case_end(test);
}

static void _test_empty_sources_build_an_empty_map(Test *const test) {
    test_case_begin(test, "init_3 over two empty lists builds an empty map");

    AL_Char keys = al_char_init_1();
    AL_Char values = al_char_init_1();

    /* al_char_init_3 treats a zero data_size as a caller contract and aborts on it, so the
     * map has to answer this case itself rather than forwarding it. */
    Map_Char_Char map = map_char_char_init_3(&keys, &values);

    test_expect_true(test, "empty", map_char_char_empty(&map));
    test_expect_u(test, "size 0", 0, map_char_char_get_size(&map));

    test_expect_true(test, "and it is usable", map_char_char_add(&map, char_new_2("k"), char_new_2("v")));
    test_expect_string(test, "reads back", "v", map_char_char_at_1(&map, "k"));

    map_char_char_uninit(&map);
    al_char_uninit(&keys);
    al_char_uninit(&values);

    test_case_end(test);
}

static void _test_new_and_delete(Test *const test) {
    test_case_begin(test, "new_2 / delete round trip");

    Map_Char_Char *map = map_char_char_new_2(2);

    /* The not-null half only. The refusal half of new_2's contract is unverified here, for
     * the reason set out in _test_heap_new_and_alloc_init_entry_points. */
    test_expect_not_null(test, "allocated", map);
    test_expect_u(test, "capacity honoured", 2, map_char_char_get_capacity(map));
    test_expect_true(test, "add", map_char_char_add(map, char_new_2("k"), char_new_2("v")));
    test_expect_string(test, "reads back", "v", map_char_char_at_1(map, "k"));

    map_char_char_delete(&map);

    test_expect_null(test, "the caller's pointer is nulled", map);

    test_case_end(test);
}

static void _test_refused_arena_declines_the_add(Test *const test) {
    test_case_begin(test, "a refused allocator declines the add and takes nothing");

    Arena refused = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    Map_Char_Char map = map_char_char_alloc_init_2(4, &refused);

    test_expect_u(test, "capacity zeroed to match the storage", 0, map_char_char_get_capacity(&map));

    /* Heap strings, not literals: if the decline ever regresses into a success, the map
     * would go on to release these through the ARENA. An allocated pair makes that show up
     * as the assertion below failing rather than as an allocator abort. */
    char *const key = char_new_2("key");
    char *const value = char_new_2("value");

    test_expect_false(test, "add declined", map_char_char_add(&map, key, value));

    test_expect_u(test, "nothing was stored", 0, map_char_char_get_size(&map));
    test_expect_u(test, "no orphan key", 0, _key_size(&map));
    test_expect_u(test, "no orphan value", 0, _value_size(&map));

    /* "Nothing was taken" is only true if these are still the caller's to read and free. */
    test_expect_string(test, "the caller still owns the key", "key", key);
    test_expect_string(test, "and the value", "value", value);

    char_delete(key);
    char_delete(value);

    test_expect_false(test, "add_static declines as well", map_char_char_add_static(&map, "k", "v"));
    test_expect_u(test, "still nothing stored", 0, map_char_char_get_size(&map));

    map_char_char_uninit(&map);
    arena_uninit(&refused, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_half_refused_add_rolls_back(Test *const test) {
    test_case_begin(test, "an add whose value half is refused leaves no orphan key");

    /* A linear arena that could not take its own buffer. Every borrow from it answers
     * nullptr, and it never reaches arena_linear_alloc, whose exhaustion check aborts. */
    Arena dead = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    /* Built by hand from two lists with DIFFERENT allocators, which is the only way to
     * reach the asymmetric branch through the public surface: one half of the add can
     * succeed while the other is refused. A map built the ordinary way shares one
     * allocator, so both halves always decline together. */
    Map_Char_Char map = map_char_char_init_1();

    map.value = al_char_alloc_init_1(&dead);

    char *const key = char_new_2("orphan");

    /* The key append succeeds on the heap while the value append is refused by the dead
     * arena. Without the rollback the key stays behind at index 0, and the NEXT successful
     * add then pairs key[1] against value[0] - a lookup answering another entry's value. */
    test_expect_false(test, "the add declined", map_char_char_add(&map, key, nullptr));

    test_expect_u(test, "the key was rolled back", 0, _key_size(&map));
    test_expect_u(test, "the value list is untouched", 0, _value_size(&map));
    test_expect_u(test, "so the map is still empty", 0, map_char_char_get_size(&map));
    test_expect_string(test, "and the key is still the caller's", "orphan", key);

    /* Positive control. Without it the case would pass just as happily if the KEY half
     * were the one being refused - or if both were - and would then be pinning nothing
     * about the rollback at all. */
    al_char_add_last(map_char_char_get_keys(&map), key);

    test_expect_u(test, "the key half could have grown", 1, _key_size(&map));
    test_expect_u(test, "while the value half still cannot", 0, _value_size(&map));

    map_char_char_uninit(&map);
    arena_uninit(&dead, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_arena_round_trip(Test *const test) {
    test_case_begin(test, "an arena-backed map stores, reads and releases");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    Map_Char_Char map = map_char_char_alloc_init_2(4, &arena);

    test_expect_u(test, "capacity honoured", 4, map_char_char_get_capacity(&map));

    /* char_alloc_new_2, not char_new_2: the map releases what it holds through its OWN
     * allocator, so a heap string here would be handed to the arena on uninit. */
    test_expect_true(test, "add", map_char_char_add(&map, char_alloc_new_2("k", &arena), char_alloc_new_2("v", &arena)));
    test_expect_string(test, "reads back", "v", map_char_char_at_1(&map, "k"));

    /* add_static must copy through the same allocator, which is the whole reason the map
     * carries no allocator of its own and reads the key list's. */
    test_expect_true(test, "add_static", map_char_char_add_static(&map, "s", "t"));
    test_expect_string(test, "reads back", "t", map_char_char_at_1(&map, "s"));

    test_expect_true(test, "remove releases through the arena", map_char_char_remove_1(&map, "s"));
    test_expect_u(test, "size 1", 1, map_char_char_get_size(&map));

    map_char_char_uninit(&map);
    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_add_static_2_stores_a_slice(Test *const test) {
    test_case_begin(test, "add_static_2 stores a key that is a slice of a larger buffer");

    Map_Char_Char map = map_char_char_init_1();

    /* The counterpart to at_2. Before it existed a caller holding "name=value" had to copy the
     * key out before storing it, even though the lookup side had taken a size since round 1. */
    char const *const pair = "colour=green";

    test_expect_true(test, "store the slice", map_char_char_add_static_2(&map, pair, 6, pair + 7, 5));

    test_expect_string(test, "the key was terminated on the way in", "colour", map_char_char_get_key(&map, 0));
    test_expect_string(test, "and so was the value", "green", map_char_char_at_1(&map, "colour"));
    test_expect_true(test, "so a terminated lookup finds it", map_char_char_contains_1(&map, "colour"));

    /* An empty key is a legal key here too, and a null value is still legal. */
    test_expect_true(test, "an empty key", map_char_char_add_static_2(&map, "", 0, "v", 1));
    test_expect_string(test, "reads back", "v", map_char_char_at_2(&map, "", 0));
    test_expect_true(test, "a null value", map_char_char_add_static_2(&map, "k", 1, nullptr, 0));
    test_expect_null(test, "stays null", map_char_char_at_1(&map, "k"));
    test_expect_true(test, "while the key is present", map_char_char_contains_1(&map, "k"));

    map_char_char_uninit(&map);

    test_case_end(test);
}

static void _test_embedded_nul_key_matches_by_leading_segment(Test *const test) {
    test_case_begin(test, "a key holding an embedded NUL is stored, but matches only its LEADING segment");

    Map_Char_Char map = map_char_char_init_1();

    /* "a\0b", 3 bytes: the STORED key is measured with char_length, which stops at the
     * embedded NUL - so every lookup after storage sees "a" and nothing past it, exactly the
     * header's remove_2 note. */
    test_expect_true(test, "store the 3-byte key", map_char_char_add_static_2(&map, "a\0b", 3, "v", 1));

    test_expect_string(test, "at_2 finds it by the LEADING segment", "v", map_char_char_at_2(&map, "a", 1));
    test_expect_null(test, "at_2 does NOT find it by the full 3 bytes", map_char_char_at_2(&map, "a\0b", 3));
    test_expect_true(test, "remove_2 removes it by the leading segment too", map_char_char_remove_2(&map, "a", 1));
    test_expect_u(test, "gone", 0, map_char_char_get_size(&map));

    map_char_char_uninit(&map);

    test_case_end(test);
}

static void _test_remove_at_targets_the_index(Test *const test) {
    test_case_begin(test, "remove_at deletes the pair the index names, duplicates and all");

    Map_Char_Char map = map_char_char_init_1();

    test_expect_true(test, "add 0", map_char_char_add_static(&map, "k", "first"));
    test_expect_true(test, "add 1", map_char_char_add_static(&map, "other", "x"));
    test_expect_true(test, "add 2", map_char_char_add_static(&map, "k", "second"));

    /* The bug remove_at exists for: iterate to index 2, then delete what you found. remove_1
     * on that key deletes index 0 instead, because a keyed remove takes the FIRST match - a
     * silent wrong answer reachable straight from the documented iteration idiom. */
    test_expect_string(test, "index 2 is the shadowed duplicate", "second", map_char_char_get_value(&map, 2));

    map_char_char_remove_at(&map, 2);

    test_expect_u(test, "two pairs left", 2, map_char_char_get_size(&map));
    test_expect_string(test, "the FIRST k survived", "first", map_char_char_at_1(&map, "k"));
    test_expect_string(test, "and its neighbour is untouched", "x", map_char_char_at_1(&map, "other"));

    /* Removing the middle shifts both halves together, as the keyed removes do. */
    map_char_char_remove_at(&map, 0);

    test_expect_u(test, "one pair left", 1, map_char_char_get_size(&map));
    test_expect_string(test, "index 0 is now the neighbour", "other", map_char_char_get_key(&map, 0));
    test_expect_string(test, "paired with its own value", "x", map_char_char_get_value(&map, 0));
    test_expect_false(test, "k is gone", map_char_char_contains_1(&map, "k"));

    map_char_char_uninit(&map);

    test_case_end(test);
}

static void _test_arena_new_and_delete(Test *const test) {
    test_case_begin(test, "the arena constructors allocate, and delete releases through the arena");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    Map_Char_Char *map = map_char_char_alloc_new_1(&arena);

    test_expect_not_null(test, "alloc_new_1 allocated", map);
    test_expect_true(test, "empty", map_char_char_empty(map));
    test_expect_true(test, "add", map_char_char_add_static(map, "k", "v"));
    test_expect_string(test, "reads back", "v", map_char_char_at_1(map, "k"));

    /* The ordering pin. delete reads (*self)->key.allocator BEFORE uninit, because the
     * struct has to go back to the same arena the lists came from and the read belongs
     * next to its use. It is NOT that reading it afterwards would be unsafe - uninit
     * releases the lists' backing arrays and never touches the struct's own block, so the
     * field would still be there. A correct ordering justified by a hazard that does not
     * exist is the kind of reasoning that gets copied into eight files and relied on. */
    map_char_char_delete(&map);

    test_expect_null(test, "delete nulled the caller's pointer", map);

    map = map_char_char_alloc_new_2(4, &arena);

    test_expect_not_null(test, "alloc_new_2 allocated", map);
    test_expect_u(test, "capacity honoured", 4, map_char_char_get_capacity(map));

    /* The combined reserve is now the ONLY reserve on the map's own surface, so this is
     * what proves it moves both lists rather than just the one get_capacity reports. */
    map_char_char_reserve(map, 16);

    test_expect_u(test, "reserve moved both lists", 16, map_char_char_get_capacity(map));

    map_char_char_delete(&map);

    test_expect_null(test, "nulled again", map);

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_heap_new_and_alloc_init_entry_points(Test *const test) {
    test_case_begin(test, "new_1 / new_3 and alloc_init_1 / alloc_init_3 as entry points");

    /* new_1's struct-borrow decline IS covered - by tests/container/map/test_oom.c
     * (_test_new_1_declines_on_struct_borrow_failure), which wraps calloc/free to force the
     * exact failure this suite has no way to make malloc produce on its own. new_2 and new_3
     * stay uncovered by design, not by gap: both borrow an array in the same call (a capacity
     * for new_2, a source list's size for new_3), and arming a fault at that array's size
     * would add an allocation at a size nothing else here uses to isolate, blurring which
     * decline is under test - see test_oom.c's own note on why they are excluded there too.
     *
     * Note this is STRONGER than alloc_new_*, not level with it: those decline a refused
     * arena and, since the struct borrow moved to try_borrow, an exhausted one too - but
     * growing either list still reaches al_char_reserve's aborting borrow, so an add can
     * still end the process where new_1 now returns nullptr. */
    Map_Char_Char *empty = map_char_char_new_1();

    test_expect_not_null(test, "new_1 allocated", empty);
    test_expect_true(test, "and is empty", map_char_char_empty(empty));

    map_char_char_delete(&empty);

    AL_Char keys = al_char_init_1();
    AL_Char values = al_char_init_1();

    al_char_add_last(&keys, char_new_2("a"));
    al_char_add_last(&values, char_new_2("1"));

    Map_Char_Char *copied = map_char_char_new_3(&keys, &values);

    test_expect_not_null(test, "new_3 allocated", copied);
    test_expect_u(test, "one pair", 1, map_char_char_get_size(copied));
    test_expect_string(test, "deep-copied", "1", map_char_char_at_1(copied, "a"));

    map_char_char_delete(&copied);

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    Map_Char_Char bare = map_char_char_alloc_init_1(&arena);

    test_expect_true(test, "alloc_init_1 is empty", map_char_char_empty(&bare));
    test_expect_true(test, "and usable", map_char_char_add_static(&bare, "k", "v"));

    map_char_char_uninit(&bare);

    Map_Char_Char from_lists = map_char_char_alloc_init_3(&keys, &values, &arena);

    test_expect_u(test, "alloc_init_3 copied the pair", 1, map_char_char_get_size(&from_lists));
    test_expect_string(test, "and reads back", "1", map_char_char_at_1(&from_lists, "a"));
    test_expect_true(test, "the copy is its own object", map_char_char_get_key(&from_lists, 0) != al_char_at(&keys, 0));

    map_char_char_uninit(&from_lists);
    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    /* The sources are wholly the caller's under the deep-copy contract. */
    al_char_uninit(&keys);
    al_char_uninit(&values);

    test_case_end(test);
}

static void _test_refused_arena_declines_the_struct(Test *const test) {
    test_case_begin(test, "a refused arena declines the STRUCT borrow rather than writing through null");

    Arena dead = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    AL_Char keys = al_char_init_1();
    AL_Char values = al_char_init_1();

    al_char_add_last(&keys, char_new_2("a"));
    al_char_add_last(&values, char_new_2("1"));

    /* The struct borrow, not the element buffers. Without the guard each of these writes a
     * whole Map_Char_Char through a null pointer - the defect al_char was fixed for, and the
     * reason the alloc_new_* trio's "a refused arena leaves nothing behind" promise was
     * false there before it was fixed. */
    test_expect_null(test, "alloc_new_1 declined", map_char_char_alloc_new_1(&dead));
    test_expect_null(test, "alloc_new_2 declined", map_char_char_alloc_new_2(4, &dead));
    test_expect_null(test, "alloc_new_3 declined", map_char_char_alloc_new_3(&keys, &values, &dead));

    al_char_uninit(&keys);
    al_char_uninit(&values);
    arena_uninit(&dead, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_exhausted_arena_declines_the_struct(Test *const test) {
    test_case_begin(test, "a LIVE but too-small arena declines the struct borrow rather than aborting");

    AL_Char keys = al_char_init_1();
    AL_Char values = al_char_init_1();

    al_char_add_last(&keys, char_new_2("a"));
    al_char_add_last(&values, char_new_2("1"));

    /* The branch the try_borrow fix ACTUALLY moved, which is why this is its own case rather
     * than more assertions in the one above. A dead arena declines at the null handler and
     * never reaches the arena at all, so those three passed before the fix too. A LIVE arena
     * that is merely too small runs the real exhaustion check - which allocator_borrow turned
     * into an abort, and try_borrow answers with a nullptr.
     *
     * A REPLICATION TEMPLATE, unlike an arena sized from a fraction of sizeof(T): an arena
     * whose total capacity is exactly MEMORY_ALIGNMENT holds strictly less than
     * MEMORY_ALIGN_UP(sizeof(T)) for every T in this family (all 64-80 bytes), so the struct
     * borrow declines regardless of which map is under test. The 1-byte probe below proves
     * the arena is LIVE - not a dead null-handler - without depending on any per-T size. */
    Arena tiny = arena_init_2(MEMORY_ALIGNMENT, 1, ARENA_TYPE_LINEAR);

    void *const probe = allocator_try_borrow(1, &tiny);

    test_expect_not_null(test, "a 1-byte probe succeeds: the arena is live", probe);

    allocator_release(probe, &tiny);

    test_expect_null(test, "alloc_new_1 declines an EXHAUSTED arena", map_char_char_alloc_new_1(&tiny));
    test_expect_null(test, "alloc_new_2 too", map_char_char_alloc_new_2(4, &tiny));
    test_expect_null(test, "alloc_new_3 too", map_char_char_alloc_new_3(&keys, &values, &tiny));

    arena_uninit(&tiny, ARENA_TYPE_LINEAR);

    al_char_uninit(&keys);
    al_char_uninit(&values);

    test_case_end(test);
}

static void _test_init_3_is_all_or_nothing(Test *const test) {
    test_case_begin(test, "a declined copy leaves init_3 with an EMPTY map, not a partial one");

    Arena dead = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    AL_Char keys = al_char_init_1();
    AL_Char values = al_char_init_1();

    al_char_add_last(&keys, char_new_2("a"));
    al_char_add_last(&keys, char_new_2("b"));
    al_char_add_last(&values, char_new_2("1"));
    al_char_add_last(&values, char_new_2("2"));

    /* Non-empty sources against an allocator that cannot satisfy the FIRST copy, so no pair
     * is ever appended before the decline. That covers the simple half of the contract; the
     * mid-sequence half - a decline AFTER a pair has landed - is
     * _test_init_3_mid_sequence_rollback below. */
    Map_Char_Char map = map_char_char_alloc_init_3(&keys, &values, &dead);

    test_expect_true(test, "the map is empty", map_char_char_empty(&map));
    test_expect_u(test, "size 0", 0, map_char_char_get_size(&map));
    test_expect_null(test, "nothing resolves", map_char_char_at_1(&map, "a"));

    map_char_char_uninit(&map);

    /* Nothing was taken, so both sources still hold everything and release it normally. */
    test_expect_u(test, "the source keys are untouched", 2, al_char_get_size(&keys));
    test_expect_string(test, "and still readable", "a", al_char_at(&keys, 0));
    test_expect_u(test, "the source values too", 2, al_char_get_size(&values));

    al_char_uninit(&keys);
    al_char_uninit(&values);
    arena_uninit(&dead, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_add_static_partial_copy_rollback(Test *const test) {
    test_case_begin(test, "add_static gives back the key copy when the value copy is refused");

    /* The last release path nothing exercised. _test_refused_arena_declines_the_add reaches
     * add_static against an allocator that refuses EVERYTHING, so the key copy fails first and
     * there is nothing to give back; this is the half where the key copy has already succeeded.
     * A pool again, because arena_linear_free is a no-op and would swallow the release. */
    Arena pool = arena_init_2(_POOL_BLOCK_SIZE, _POOL_BLOCK_COUNT, ARENA_TYPE_POOL);

    void *const baseline = allocator_try_borrow(_POOL_BLOCK_SIZE * _POOL_BLOCK_COUNT, &pool);

    test_expect_not_null(test, "the pool starts whole", baseline);

    allocator_release(baseline, &pool);

    char *const oversized = char_new_1(_OVERSIZED_SIZE);

    char_fill(oversized, _OVERSIZED_SIZE, 'x');

    // Belt only: char_new_1(N) zeroes N+1 bytes, so the terminator already reads 0.
    oversized[_OVERSIZED_SIZE] = '\0';

    Map_Char_Char map = map_char_char_alloc_init_1(&pool);

    /* A short key the pool can serve, and a value larger than the pool holds. */
    test_expect_true(test, "a short pair fits", map_char_char_add_static(&map, "fits", "yes"));
    test_expect_false(test, "the oversized value is refused", map_char_char_add_static(&map, "k", oversized));

    test_expect_u(test, "and nothing was added for it", 1, map_char_char_get_size(&map));
    test_expect_null(test, "the refused key is absent", map_char_char_at_1(&map, "k"));
    test_expect_string(test, "the earlier pair is untouched", "yes", map_char_char_at_1(&map, "fits"));

    map_char_char_uninit(&map);

    void *const after = allocator_try_borrow(_POOL_BLOCK_SIZE * _POOL_BLOCK_COUNT, &pool);

    test_expect_not_null(test, "the pool is whole again - the key copy was given back", after);

    allocator_release(after, &pool);
    arena_uninit(&pool, ARENA_TYPE_POOL);

    char_delete(oversized);

    test_case_end(test);
}

static void _test_init_3_mid_sequence_rollback(Test *const test) {
    test_case_begin(test, "a decline AFTER a pair has landed rolls the whole map back");

    AL_Char keys = al_char_init_1();
    AL_Char values = al_char_init_1();

    /* Pair 1's value is larger than either arena below, so the refusal is by SIZE and needs
     * no arithmetic about how much room the earlier copies consumed. Pair 0 is two bytes a
     * side and always lands, which is what puts the fill mid-sequence when pair 1 fails. */
    char *const oversized = char_new_1(_OVERSIZED_SIZE);

    char_fill(oversized, _OVERSIZED_SIZE, 'x');

    // Belt only: char_new_1(N) zeroes N+1 bytes, so the terminator already reads 0.
    oversized[_OVERSIZED_SIZE] = '\0';

    al_char_add_last(&keys, char_new_2("a"));
    al_char_add_last(&keys, char_new_2("b"));
    al_char_add_last(&values, char_new_2("1"));
    al_char_add_last(&values, oversized);

    /* The positive control FIRST: the same two pairs against room enough for both. Without
     * it the case below would pass just as happily if pair 0 were the one being refused,
     * and would then pin nothing about a MID-SEQUENCE rollback. */
    Arena roomy = arena_init_1(_OVERSIZED_SIZE * 4, ARENA_TYPE_LINEAR);
    Map_Char_Char whole = map_char_char_alloc_init_3(&keys, &values, &roomy);

    test_expect_u(test, "with room, both pairs land", 2, map_char_char_get_size(&whole));
    test_expect_string(test, "including the oversized value", oversized, map_char_char_at_1(&whole, "b"));

    map_char_char_uninit(&whole);
    arena_uninit(&roomy, ARENA_TYPE_LINEAR);

    /* Now the same sources against an arena that fits pair 0 and cannot fit pair 1's value.
     * _map_char_char_copy borrows through allocator_try_borrow, which reaches
     * arena_linear_try_alloc - deliberately check-free, so exhaustion answers nullptr rather
     * than aborting. Only alloc_init_2's two ARRAY borrows take the aborting path, and they
     * run before the fill. */
    Arena tight = arena_init_1(1024, ARENA_TYPE_LINEAR);
    Map_Char_Char rolled = map_char_char_alloc_init_3(&keys, &values, &tight);

    test_expect_true(test, "the map came back empty", map_char_char_empty(&rolled));
    test_expect_u(test, "size 0, not the one pair that landed", 0, map_char_char_get_size(&rolled));
    test_expect_null(test, "the landed pair is gone too", map_char_char_at_1(&rolled, "a"));

    map_char_char_uninit(&rolled);
    arena_uninit(&tight, ARENA_TYPE_LINEAR);

    /* Whether the rollback RELEASED the key copy it had already taken is invisible above -
     * arena_linear_free is a no-op. A pool's free is real, so run it again there and then ask
     * the pool for everything it has: that borrow succeeds only if every block came back. */
    Arena pool = arena_init_2(_POOL_BLOCK_SIZE, _POOL_BLOCK_COUNT, ARENA_TYPE_POOL);

    void *const baseline = allocator_try_borrow(_POOL_BLOCK_SIZE * _POOL_BLOCK_COUNT, &pool);

    test_expect_not_null(test, "the pool starts whole", baseline);

    allocator_release(baseline, &pool);

    Map_Char_Char pooled = map_char_char_alloc_init_3(&keys, &values, &pool);

    test_expect_true(test, "the pooled map came back empty too", map_char_char_empty(&pooled));

    map_char_char_uninit(&pooled);

    void *const after = allocator_try_borrow(_POOL_BLOCK_SIZE * _POOL_BLOCK_COUNT, &pool);

    test_expect_not_null(test, "and the pool is whole again - nothing was leaked", after);

    allocator_release(after, &pool);
    arena_uninit(&pool, ARENA_TYPE_POOL);

    /* Deep-copy contract: everything handed in is still the caller's, oversized value included. */
    test_expect_u(test, "the sources kept both keys", 2, al_char_get_size(&keys));
    test_expect_u(test, "and both values", 2, al_char_get_size(&values));

    al_char_uninit(&keys);
    al_char_uninit(&values);

    test_case_end(test);
}

static void _test_null_key_element_survives_the_copy(Test *const test) {
    test_case_begin(test, "a null key element carried in by init_3 is skipped, not dereferenced");

    AL_Char keys = al_char_init_1();
    AL_Char values = al_char_init_1();

    al_char_add_last(&keys, char_new_2("a"));
    al_char_add_last(&keys, nullptr);
    al_char_add_last(&keys, char_new_2("c"));
    al_char_add_last(&values, char_new_2("1"));
    al_char_add_last(&values, char_new_2("2"));
    al_char_add_last(&values, char_new_2("3"));

    /* add and add_static both refuse a null key, so the copying constructors are the ONLY
     * way a stored nullptr key can exist - which meant _map_char_char_index's null-key skip
     * passed purely because nothing ever reached it. Handing char_length a null key is a
     * dereference, so the skip has to be real, not decorative. */
    Map_Char_Char map = map_char_char_init_3(&keys, &values);

    test_expect_u(test, "all three pairs copied", 3, map_char_char_get_size(&map));
    test_expect_null(test, "the null key stayed null", map_char_char_get_key(&map, 1));
    test_expect_string(test, "its value came through", "2", map_char_char_get_value(&map, 1));

    /* Every one of these scans past the null key. */
    test_expect_string(test, "a resolves", "1", map_char_char_at_1(&map, "a"));
    test_expect_string(test, "c resolves past it", "3", map_char_char_at_1(&map, "c"));
    test_expect_true(test, "contains scans past it", map_char_char_contains_1(&map, "c"));
    test_expect_false(test, "and a miss scans the whole list", map_char_char_contains_1(&map, "zzz"));
    test_expect_true(test, "remove scans past it too", map_char_char_remove_1(&map, "c"));
    test_expect_u(test, "two pairs left", 2, map_char_char_get_size(&map));

    map_char_char_uninit(&map);

    al_char_uninit(&keys);
    al_char_uninit(&values);

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

    Test test = test_init("tests/container/map/test_map_char_char.c");

    test_suite_begin(&test, "map_char_char");
    _test_add_adopts_and_reads_back(&test);
    _test_empty_key_is_a_value(&test);
    _test_size_is_derived(&test);
    _test_null_value_versus_absent(&test);
    _test_add_static_copies(&test);
    _test_mixed_allocator_add_static(&test);
    _test_add_static_partial_copy_rollback(&test);
    _test_duplicate_keys(&test);
    _test_remove_keeps_the_pairing(&test);
    _test_add_static_2_stores_a_slice(&test);
    _test_embedded_nul_key_matches_by_leading_segment(&test);
    _test_remove_at_targets_the_index(&test);
    _test_capacity_is_the_smaller(&test);
    _test_init_3_deep_copies_and_truncates(&test);
    _test_empty_sources_build_an_empty_map(&test);
    _test_new_and_delete(&test);
    _test_refused_arena_declines_the_add(&test);
    _test_half_refused_add_rolls_back(&test);
    _test_arena_round_trip(&test);
    _test_arena_new_and_delete(&test);
    _test_heap_new_and_alloc_init_entry_points(&test);
    _test_refused_arena_declines_the_struct(&test);
    _test_exhausted_arena_declines_the_struct(&test);
    _test_init_3_is_all_or_nothing(&test);
    _test_init_3_mid_sequence_rollback(&test);
    _test_null_key_element_survives_the_copy(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}