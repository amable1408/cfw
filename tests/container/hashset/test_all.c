#include <stdio.h>
#include <string.h>

#include <container/hashset/hashset.h>
#include <log/log.h>
#include <test/test.h>
#include <arena/arena.h>

/* Suite for HashSet, the string-keyed counting hash set. What it pins is the CONTRACT the
 * header documents, not just this file's own inputs:
 *
 *   - TWO OWNERSHIP MODES. hashset_add borrows the caller's pointer (the set never frees it);
 *     hashset_add_static / hashset_add_static_2 copy the key, so the set owns a stable copy
 *     independent of the caller's buffer. Every borrowed key below is a heap allocation freed
 *     by hand after the set is gone, so a regression that starts (or stops) owning it shows up
 *     as a double free / leak, not as a quiet pass.
 *   - add's RETURN VALUE IS THE POST-CALL COUNT: 0 declined, 1 newly inserted, n an existing
 *     key's incremented (and possibly saturated) count.
 *   - REFUSAL, NEVER ABORT, on a VALUE-dependent condition: an empty key, or an allocator that
 *     declines a build/copy/grow. A refused BUILD lands at capacity 0 with every op inert; a
 *     refused GROW keeps the old table and the triggering insert then declines at
 *     size == capacity.
 *   - CAPACITY IS BUCKETS, sized from a KEY-count hint at ~70% load factor; a duplicate add
 *     never grows the table, because a miss probes first.
 *   - hashset_next IS THE SANCTIONED ITERATOR - the only public way to walk every stored pair. */

/*==============================================================================
 * MARK: - Internal
 *============================================================================*/

/* White-box slot lookup, precedented by the map suite's own direct field access (its
 * `map.key = al_char_alloc_init_1(&arena)` setups): HashSet's fields are fully declared in
 * the public header, only documented as "not a public API" for a RUNNING program, not off
 * limits to a test that needs to fabricate a state 4 billion real adds cannot reach. */
static USize _hashset_test_find_slot(HashSet const *const self, char const *const key) {
    for (USize i = 0; i < self->capacity; i += 1) {
        if (self->keys[i] != nullptr && char_compare_equal_1(self->keys[i], key)) {
            return i;
        }
    }

    return self->capacity;
}

/*==============================================================================
 * MARK: - Cases
 *============================================================================*/

static void _test_add_return_values(Test *const test) {
    test_case_begin(test, "add's return value: 1 new, n on a duplicate, 0 when a refused arena declines it");

    HashSet set = hashset_init_1();

    test_expect_u(test, "a new key returns 1", 1, hashset_add_static(&set, "r"));
    test_expect_u(test, "a duplicate returns its incremented count", 2, hashset_add_static(&set, "r"));

    hashset_uninit(&set);

    Arena dead = arena_init_2(0, 8, ARENA_TYPE_LINEAR);
    HashSet refused = hashset_alloc_init_2(4, &dead);

    test_expect_u(test, "a refused arena declines the add", 0, hashset_add_static(&refused, "r"));

    hashset_uninit(&refused);
    arena_uninit(&dead, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_add_borrows_add_static_copies(Test *const test) {
    test_case_begin(test, "add borrows the caller's pointer; add_static copies it");

    HashSet set = hashset_init_1();

    // A heap key handed to add(): if the set ever took ownership, uninit would free it and
    // the char_delete below would double-free.
    char *const borrowed = char_new_2("borrowed-key");

    test_expect_u(test, "add borrows it", 1, hashset_add(&set, borrowed));
    test_expect_true(test, "present via the borrow", hashset_contains(&set, "borrowed-key"));

    hashset_uninit(&set);

    // If uninit had freed the borrowed key, this read (and the free below) would be a
    // use-after-free / double-free.
    test_expect_string(test, "the caller still owns it, unfreed", "borrowed-key", borrowed);

    char_delete(borrowed);

    // add_static copies: mutating the caller's buffer afterward must not reach the set.
    HashSet set2 = hashset_init_1();
    char buffer[16] = DEFAULT_INITIALIZATION;

    strcpy(buffer, "static-key");

    test_expect_u(test, "add_static copies it", 1, hashset_add_static(&set2, buffer));

    buffer[0] = 'X';

    test_expect_true(test, "the stored copy is unaffected by the mutation", hashset_contains(&set2, "static-key"));
    test_expect_false(test, "the mutated buffer reads as a different key", hashset_contains(&set2, "Xtatic-key"));

    hashset_uninit(&set2);

    test_case_end(test);
}

static void _test_add_static_from_block_scoped_buffer(Test *const test) {
    test_case_begin(test, "add_static from a block-scoped buffer survives past the block");

    HashSet set = hashset_init_1();

    {
        char scoped[16] = DEFAULT_INITIALIZATION;

        strcpy(scoped, "scoped-key");

        test_expect_u(test, "add_static copies out of the block", 1, hashset_add_static(&set, scoped));
    } // `scoped` is dead from here on; only a copy the set owns can answer the lookup below.

    test_expect_true(test, "the key is still found after the buffer's scope ended", hashset_contains(&set, "scoped-key"));
    test_expect_u(test, "count still 1", 1, hashset_count(&set, "scoped-key"));

    hashset_uninit(&set);

    test_case_end(test);
}

static void _test_clear_semantics(Test *const test) {
    test_case_begin(test, "clear empties the set, resets counts, frees owned copies, leaves borrowed keys alone");

    HashSet set = hashset_init_1();

    char *const borrowed = char_new_2("borrowed");

    test_expect_u(test, "borrow it", 1, hashset_add(&set, borrowed));
    test_expect_u(test, "own a copy too", 1, hashset_add_static(&set, "owned"));
    test_expect_u(test, "size 2 before clear", 2, hashset_size(&set));

    USize const capacity_before = hashset_get_capacity(&set);

    hashset_clear(&set);

    test_expect_u(test, "size 0 after clear", 0, hashset_size(&set));
    test_expect_true(test, "empty after clear", hashset_empty(&set));
    test_expect_false(test, "the borrowed key no longer resolves", hashset_contains(&set, "borrowed"));
    test_expect_false(test, "neither does the owned copy", hashset_contains(&set, "owned"));
    test_expect_u(test, "clear keeps the CAPACITY - slots stay reusable", capacity_before, hashset_get_capacity(&set));

    // Counts reset, not merely hidden: re-adding "owned" starts back at 1, not 3.
    test_expect_u(test, "a fresh add after clear starts the count over at 1", 1, hashset_add_static(&set, "owned"));

    hashset_uninit(&set);

    // clear/uninit never freed the borrowed key - a double free here would abort the run.
    test_expect_string(test, "the borrowed key is still the caller's to free", "borrowed", borrowed);

    char_delete(borrowed);

    test_case_end(test);
}

static void _test_empty_key_refused_by_policy(Test *const test) {
    test_case_begin(test, "an empty key is refused by policy on every entry point, not aborted");

    HashSet set = hashset_init_1();

    test_expect_u(test, "add(\"\") declines", 0, hashset_add(&set, ""));
    test_expect_u(test, "add_static(\"\") declines", 0, hashset_add_static(&set, ""));
    test_expect_u(test, "add_static_2 with size 0 declines", 0, hashset_add_static_2(&set, "x", 0));
    test_expect_false(test, "contains(\"\") is false", hashset_contains(&set, ""));
    test_expect_false(test, "contains_2 size 0 is false", hashset_contains_2(&set, "x", 0));
    test_expect_u(test, "count(\"\") is 0", 0, hashset_count(&set, ""));
    test_expect_u(test, "count_2 size 0 is 0", 0, hashset_count_2(&set, "x", 0));
    test_expect_true(test, "nothing was stored", hashset_empty(&set));
    test_expect_u(test, "size stays 0", 0, hashset_size(&set));

    hashset_uninit(&set);

    test_case_end(test);
}

static void _test_refused_arena_declines_the_build(Test *const test) {
    test_case_begin(test, "a refused (dead) arena builds a capacity-0 set; add declines with 0");

    Arena dead = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    HashSet set = hashset_alloc_init_2(4, &dead);

    test_expect_u(test, "capacity is 0", 0, hashset_get_capacity(&set));
    test_expect_true(test, "empty", hashset_empty(&set));

    char *const key = char_new_2("orphan");

    test_expect_u(test, "add declines", 0, hashset_add(&set, key));
    test_expect_u(test, "add_static declines too", 0, hashset_add_static(&set, "orphan"));
    test_expect_u(test, "still capacity 0", 0, hashset_get_capacity(&set));
    test_expect_true(test, "the caller still owns the borrowed key", char_compare_equal_1(key, "orphan"));

    char_delete(key);
    hashset_uninit(&set);
    arena_uninit(&dead, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_exhausted_live_arena_declines_the_build(Test *const test) {
    test_case_begin(test, "a LIVE but too-small arena declines the build too; add still declines");

    /* Same replication template as the map suite: an arena whose total capacity is exactly
     * MEMORY_ALIGNMENT holds far less than the bucket arrays any requested capacity needs, so
     * the build declines - but the 1-byte probe below proves the arena is LIVE, not a dead
     * null-handler, before the set is even built. */
    Arena tiny = arena_init_2(MEMORY_ALIGNMENT, 1, ARENA_TYPE_LINEAR);

    void *const probe = allocator_try_borrow(1, &tiny);

    test_expect_not_null(test, "a 1-byte probe succeeds: the arena is live", probe);

    allocator_release(probe, &tiny);

    HashSet set = hashset_alloc_init_2(64, &tiny);

    test_expect_u(test, "capacity is 0 - the arena could not hold the bucket arrays", 0, hashset_get_capacity(&set));
    test_expect_u(test, "add declines", 0, hashset_add_static(&set, "x"));

    hashset_uninit(&set);
    arena_uninit(&tiny, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_capacity_zero_set_is_inert(Test *const test) {
    test_case_begin(test, "contains/count/next/clear on a capacity-0 (dead) set answer false/0/false/no-op");

    Arena dead = arena_init_2(0, 8, ARENA_TYPE_LINEAR);
    HashSet set = hashset_alloc_init_2(4, &dead);

    test_expect_u(test, "capacity is 0", 0, hashset_get_capacity(&set));

    test_expect_false(test, "contains answers false", hashset_contains(&set, "x"));
    test_expect_false(test, "contains_2 answers false", hashset_contains_2(&set, "x", 1));
    test_expect_u(test, "count answers 0", 0, hashset_count(&set, "x"));
    test_expect_u(test, "count_2 answers 0", 0, hashset_count_2(&set, "x", 1));

    USize cursor = 0;
    char const *key = nullptr;
    U32 count = 0;

    test_expect_false(test, "next answers false immediately", hashset_next(&set, &cursor, &key, &count));

    hashset_clear(&set); // must not crash on the null bucket arrays

    test_expect_u(test, "clear is a no-op: size stays 0", 0, hashset_size(&set));
    test_expect_u(test, "clear is a no-op: capacity stays 0", 0, hashset_get_capacity(&set));

    hashset_uninit(&set);
    arena_uninit(&dead, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_counts_over_one_survive_a_grow(Test *const test) {
    test_case_begin(test, "a count > 1 survives a grow/rehash triggered by later distinct keys");

    HashSet set = hashset_init_1(); // DEFAULT_KEYS == 16 keys, rounds up to 32 buckets

    test_expect_u(test, "k inserted", 1, hashset_add_static(&set, "k"));
    test_expect_u(test, "k incremented", 2, hashset_add_static(&set, "k"));
    test_expect_u(test, "k incremented again", 3, hashset_add_static(&set, "k"));

    USize const capacity_before = hashset_get_capacity(&set);

    // Enough DISTINCT keys to cross the ~70% load factor on the default 32-bucket table (a
    // default hashset_init_1() sizes for _HASHSET_DEFAULT_KEYS == 16 KEYS, which rounds up to
    // 32 buckets - not 16) and force a grow: only a genuinely new key reaches the growth
    // check, so filler keys - not more "k" duplicates - are what has to trigger it.
    for (USize i = 0; i < 30; i += 1) {
        char filler[16] = DEFAULT_INITIALIZATION;

        sprintf(filler, "filler%zu", i);
        hashset_add_static(&set, filler);
    }

    test_expect_true(test, "the table actually grew", hashset_get_capacity(&set) > capacity_before);
    test_expect_u(test, "k's count survived the rehash", 3, hashset_count(&set, "k"));
    test_expect_true(test, "k is still present", hashset_contains(&set, "k"));
    test_expect_u(test, "31 distinct keys total", 31, hashset_size(&set));

    hashset_uninit(&set);

    test_case_end(test);
}

static void _test_duplicate_add_at_threshold_does_not_grow(Test *const test) {
    test_case_begin(test, "a duplicate add at the load-factor threshold never grows the table");

    // hashset_init_1() sizes for _HASHSET_DEFAULT_KEYS == 16 KEYS, which rounds up to 32
    // buckets (16*10/7+1 == 23, and 32 is the smallest power of two >= 23) - not 16.
    HashSet set = hashset_init_1();

    // 22 distinct inserts keep (size+1)*10 <= capacity*7 (== 224) at every step, so the table
    // is still 32 buckets afterward: the 23rd DISTINCT key would be the one to grow it.
    for (USize i = 0; i < 22; i += 1) {
        char key[8] = DEFAULT_INITIALIZATION;

        sprintf(key, "d%zu", i);
        test_expect_u(test, "unique insert succeeds", 1, hashset_add_static(&set, key));
    }

    test_expect_u(test, "still the default 32 buckets", 32, hashset_get_capacity(&set));

    // A DUPLICATE of an already-stored key at this exact threshold: a probe-first hit never
    // reaches the growth check at all.
    test_expect_u(test, "duplicate increments, does not grow", 2, hashset_add_static(&set, "d0"));
    test_expect_u(test, "capacity unchanged by the duplicate", 32, hashset_get_capacity(&set));
    test_expect_u(test, "size still 22 distinct keys", 22, hashset_size(&set));

    hashset_uninit(&set);

    test_case_end(test);
}

static void _test_capacity_holds_n_keys_without_growing(Test *const test) {
    test_case_begin(test, "init_2(n) sizes so n keys fit without growing");

    /* capacity hint 50: target = 50*10/7+1 = 72, rounded up to the next power of two is 128 -
     * the same integer arithmetic hashset.c's _hashset_build performs, not a bare magic
     * number. */
    HashSet set = hashset_init_2(50);

    USize const capacity_after_init = hashset_get_capacity(&set);

    test_expect_u(test, "rounds up to 128 buckets", 128, capacity_after_init);

    for (USize i = 0; i < 50; i += 1) {
        char key[8] = DEFAULT_INITIALIZATION;

        sprintf(key, "n%zu", i);
        hashset_add_static(&set, key);
    }

    test_expect_u(test, "all 50 keys landed", 50, hashset_size(&set));
    test_expect_u(test, "capacity never moved", capacity_after_init, hashset_get_capacity(&set));

    hashset_uninit(&set);

    test_case_end(test);
}

static void _test_sized_forms_slice_and_terminator(Test *const test) {
    test_case_begin(test, "add_static_2 stores a slice; contains_2/count_2 work terminated or not");

    HashSet set = hashset_init_1();

    // A slice of a larger buffer, NOT itself NUL-terminated at the cut point ('f' follows).
    char const *const source = "typefoo";

    test_expect_u(test, "add_static_2 stores the 4-byte slice", 1, hashset_add_static_2(&set, source, 4));

    // WITHOUT a terminator: reading key_size bytes straight out of the larger buffer.
    test_expect_true(test, "contains_2 finds it via the untouched slice", hashset_contains_2(&set, source, 4));
    test_expect_u(test, "count_2 agrees", 1, hashset_count_2(&set, source, 4));

    // WITH a terminator: a separate, properly NUL-terminated buffer holding the same bytes.
    char terminated[5] = DEFAULT_INITIALIZATION;

    char_copy_2(terminated, source, 4);
    terminated[4] = '\0';

    test_expect_true(test, "contains_2 finds it via the terminated copy too", hashset_contains_2(&set, terminated, 4));
    test_expect_u(test, "count_2 agrees on the terminated copy", 1, hashset_count_2(&set, terminated, 4));
    test_expect_true(test, "hashset_contains (NUL form) agrees too", hashset_contains(&set, terminated));

    // A different slice length off the same buffer is a DIFFERENT key.
    test_expect_u(test, "a 3-byte slice is a distinct key", 1, hashset_add_static_2(&set, source, 3));
    test_expect_u(test, "two distinct keys now", 2, hashset_size(&set));

    // A slice that spans an embedded NUL would be stored under its C-string length and
    // never found again, so the sized forms refuse it as data on every entry point.
    char const spanning[] = "ab\0cd";

    test_expect_u(test, "add_static_2 refuses a slice spanning a NUL", 0, hashset_add_static_2(&set, spanning, 5));
    test_expect_false(test, "contains_2 refuses it too", hashset_contains_2(&set, spanning, 5));
    test_expect_u(test, "count_2 answers 0 for it", 0, hashset_count_2(&set, spanning, 5));
    test_expect_u(test, "still two keys", 2, hashset_size(&set));

    hashset_uninit(&set);

    test_case_end(test);
}

static void _test_sized_forms_refuse_key_size_usize_max(Test *const test) {
    test_case_begin(test, "add_static_2/contains_2/count_2 refuse key_size == USIZE_MAX as data - safe to call since "
        "_hashset_sized_key_refused returns before any read, so the size lying about the buffer never matters");

    HashSet set = hashset_init_1();
    char const *const key = "x";

    test_expect_u(test, "add_static_2 refuses USIZE_MAX", 0, hashset_add_static_2(&set, key, USIZE_MAX));
    test_expect_false(test, "contains_2 refuses USIZE_MAX", hashset_contains_2(&set, key, USIZE_MAX));
    test_expect_u(test, "count_2 refuses USIZE_MAX", 0, hashset_count_2(&set, key, USIZE_MAX));
    test_expect_u(test, "size unchanged", 0, hashset_size(&set));

    hashset_uninit(&set);

    test_case_end(test);
}

static void _test_hashset_next_iterates_every_pair_once(Test *const test) {
    test_case_begin(test, "hashset_next walks every key exactly once, false at the end");

    HashSet empty_set = hashset_init_1();
    USize cursor = 0;
    char const *key = nullptr;
    U32 count = 0;

    test_expect_false(test, "an empty set yields nothing", hashset_next(&empty_set, &cursor, &key, &count));

    hashset_uninit(&empty_set);

    HashSet set = hashset_init_1();

    hashset_add_static(&set, "alpha");
    hashset_add_static(&set, "beta");
    hashset_add_static(&set, "beta"); // count 2
    hashset_add_static(&set, "gamma");

    test_expect_u(test, "three distinct keys", 3, hashset_size(&set));

    bool seen_alpha = false;
    bool seen_beta = false;
    bool seen_gamma = false;
    USize visits = 0;

    cursor = 0;

    while (hashset_next(&set, &cursor, &key, &count)) {
        visits += 1;

        if (char_compare_equal_1(key, "alpha")) {
            test_expect_false(test, "alpha visited once", seen_alpha);
            seen_alpha = true;
            test_expect_u(test, "alpha count 1", 1, count);
        } else if (char_compare_equal_1(key, "beta")) {
            test_expect_false(test, "beta visited once", seen_beta);
            seen_beta = true;
            test_expect_u(test, "beta count 2", 2, count);
        } else if (char_compare_equal_1(key, "gamma")) {
            test_expect_false(test, "gamma visited once", seen_gamma);
            seen_gamma = true;
            test_expect_u(test, "gamma count 1", 1, count);
        }
    }

    test_expect_u(test, "exactly 3 pairs visited", 3, visits);
    test_expect_true(test, "alpha was visited", seen_alpha);
    test_expect_true(test, "beta was visited", seen_beta);
    test_expect_true(test, "gamma was visited", seen_gamma);
    test_expect_false(test, "one more call after exhaustion answers false", hashset_next(&set, &cursor, &key, &count));

    hashset_uninit(&set);

    test_case_end(test);
}

static void _test_new_and_delete_round_trips(Test *const test) {
    test_case_begin(test, "new_1 / new_2 / delete round trip on the heap");

    HashSet *set1 = hashset_new_1();

    test_expect_not_null(test, "new_1 allocated", set1);
    test_expect_true(test, "empty", hashset_empty(set1));
    test_expect_u(test, "add via the pointer", 1, hashset_add_static(set1, "x"));

    hashset_delete(&set1);

    test_expect_null(test, "delete nulled the caller's pointer", set1);

    HashSet *set2 = hashset_new_2(10);

    test_expect_not_null(test, "new_2 allocated", set2);
    test_expect_u(test, "capacity honoured", 16, hashset_get_capacity(set2));

    hashset_delete(&set2);

    test_expect_null(test, "delete nulled the caller's pointer again", set2);

    test_case_end(test);
}

static void _test_alloc_new_round_trips_and_refusal(Test *const test) {
    test_case_begin(test, "alloc_new_1 / alloc_new_2 round trip on an arena; a refused arena answers nullptr");

    Arena arena = arena_init_1(8192, ARENA_TYPE_LINEAR);

    HashSet *set1 = hashset_alloc_new_1(&arena);

    test_expect_not_null(test, "alloc_new_1 allocated", set1);
    test_expect_true(test, "empty", hashset_empty(set1));
    test_expect_u(test, "add via the pointer", 1, hashset_add_static(set1, "y"));

    hashset_delete(&set1);

    test_expect_null(test, "delete nulled the caller's pointer", set1);

    HashSet *set2 = hashset_alloc_new_2(20, &arena);

    test_expect_not_null(test, "alloc_new_2 allocated", set2);
    test_expect_u(test, "capacity honoured", 32, hashset_get_capacity(set2));

    hashset_delete(&set2);

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    Arena dead = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    test_expect_null(test, "alloc_new_1 declines a refused arena", hashset_alloc_new_1(&dead));
    test_expect_null(test, "alloc_new_2 declines a refused arena", hashset_alloc_new_2(4, &dead));

    arena_uninit(&dead, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_uninit_on_arena_set_and_double_uninit(Test *const test) {
    test_case_begin(test, "hashset_uninit works on an arena-backed set and is safe to call twice");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);
    HashSet set = hashset_alloc_init_2(8, &arena);

    hashset_add_static(&set, "z");

    test_expect_u(test, "one key stored", 1, hashset_size(&set));

    hashset_uninit(&set);

    test_expect_u(test, "size 0 after uninit", 0, hashset_size(&set));
    test_expect_u(test, "capacity 0 after uninit", 0, hashset_get_capacity(&set));
    test_expect_true(test, "empty after uninit", hashset_empty(&set));

    hashset_uninit(&set); // double uninit must be safe

    test_expect_u(test, "still 0 after a second uninit", 0, hashset_size(&set));
    test_expect_u(test, "add on a DEAD set is now a silent no-op", 0, hashset_add_static(&set, "z"));

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_count_saturates_at_u32_max(Test *const test) {
    test_case_begin(test, "count saturates at U32_MAX rather than wrapping (header contract, no 4-billion-add loop)");

    HashSet set = hashset_init_1();

    test_expect_u(test, "new key starts at 1", 1, hashset_add_static(&set, "sat"));

    USize const slot = _hashset_test_find_slot(&set, "sat");

    test_expect_true(test, "the key's slot was found", slot != set.capacity);

    /* Direct field write, fabricating near-saturation without 4 billion real adds, then
     * proving the header's documented contract - saturate at U32_MAX, never wrap to 0 -
     * through the REAL add path (_hashset_add's `if (counts[existing] < U32_MAX)` guard). */
    set.counts[slot] = U32_MAX - 1;

    test_expect_u(test, "one more add reaches U32_MAX", U32_MAX, hashset_add_static(&set, "sat"));
    test_expect_u(test, "hashset_count agrees", U32_MAX, hashset_count(&set, "sat"));
    test_expect_true(test, "hashset_contains still true", hashset_contains(&set, "sat"));

    test_expect_u(test, "one more add STAYS at U32_MAX, does not wrap to 0", U32_MAX, hashset_add_static(&set, "sat"));
    test_expect_true(test, "contains still true after the would-be wrap", hashset_contains(&set, "sat"));

    hashset_uninit(&set);

    test_case_end(test);
}

static void _test_arena_backed_add_and_contains(Test *const test) {
    test_case_begin(test, "an arena-backed set stores both a static copy and a borrowed key");

    Arena arena = arena_init_1(8192, ARENA_TYPE_LINEAR);
    HashSet set = hashset_alloc_init_2(64, &arena);

    test_expect_u(test, "capacity honoured", 128, hashset_get_capacity(&set));
    test_expect_u(test, "static copy added", 1, hashset_add_static(&set, "x"));
    test_expect_u(test, "borrowed key added", 1, hashset_add(&set, "y"));

    test_expect_true(test, "arena copy present", hashset_contains(&set, "x"));
    test_expect_true(test, "arena borrow present", hashset_contains(&set, "y"));
    test_expect_u(test, "arena size", 2, hashset_size(&set));

    arena_uninit(&arena, ARENA_TYPE_LINEAR); // reclaims everything at once

    test_case_end(test);
}

static void _test_growth_retains_every_key(Test *const test) {
    test_case_begin(test, "growing across many inserts retains every distinct key and its count");

    HashSet set = hashset_init_2(8);

    for (USize i = 0; i < 200; i += 1) {
        char key[16] = DEFAULT_INITIALIZATION;

        sprintf(key, "key%zu", i);
        hashset_add_static(&set, key);
    }

    test_expect_u(test, "all 200 distinct keys retained", 200, hashset_size(&set));

    bool all_present = true;
    bool counts_ok = true;

    for (USize i = 0; i < 200; i += 1) {
        char key[16] = DEFAULT_INITIALIZATION;

        sprintf(key, "key%zu", i);

        if (!hashset_contains(&set, key)) {
            all_present = false;
        }

        if (hashset_count(&set, key) != 1) {
            counts_ok = false;
        }
    }

    test_expect_true(test, "every key present after rehash", all_present);
    test_expect_true(test, "every count preserved after rehash", counts_ok);

    hashset_uninit(&set);

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

    Test test = test_init("tests/container/hashset/test_all.c");

    test_suite_begin(&test, "hashset");
    _test_add_return_values(&test);
    _test_add_borrows_add_static_copies(&test);
    _test_add_static_from_block_scoped_buffer(&test);
    _test_clear_semantics(&test);
    _test_empty_key_refused_by_policy(&test);
    _test_refused_arena_declines_the_build(&test);
    _test_exhausted_live_arena_declines_the_build(&test);
    _test_capacity_zero_set_is_inert(&test);
    _test_counts_over_one_survive_a_grow(&test);
    _test_duplicate_add_at_threshold_does_not_grow(&test);
    _test_capacity_holds_n_keys_without_growing(&test);
    _test_sized_forms_slice_and_terminator(&test);
    _test_sized_forms_refuse_key_size_usize_max(&test);
    _test_hashset_next_iterates_every_pair_once(&test);
    _test_new_and_delete_round_trips(&test);
    _test_alloc_new_round_trips_and_refusal(&test);
    _test_uninit_on_arena_set_and_double_uninit(&test);
    _test_count_saturates_at_u32_max(&test);
    _test_arena_backed_add_and_contains(&test);
    _test_growth_retains_every_key(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}