#include <test/test.h>

#include <allocator/allocator.h>
#include <arena/arena.h>
#include <char/char.h>
#include <container/arrayList/al_string.h>

/* First suite for AL_String, which had none: it was reached only indirectly, through
 * container/map/map_char_string. The file was a pre-sweep fossil of al_str and this
 * suite pins the three defects that made it one:
 *
 *   1. al_string_remove shifted elements FIRST and released afterwards, so the removed
 *      element's buffer leaked and data[size - 1] became a shallow duplicate of the
 *      element now at size - 2 - clearing that tail then reached through the alias and
 *      destroyed a LIVE element's characters. Every remove case below therefore reads
 *      the survivors' CONTENT and length back, not just the size.
 *   2. al_string_clear used string_clear (size reset, buffer kept) instead of
 *      string_uninit, so every element allocation leaked at end of life; uninit went
 *      through clear, freed nothing, and never reset size.
 *   3. al_string_reserve / al_string_shrink borrowed with a NULL allocator while
 *      releasing through self->allocator, so an arena-backed list regrew on the HEAP
 *      and then handed that heap block to the arena's release. The arena case measures
 *      the arena's own consumption, because content alone cannot see a heap borrow.
 *
 * Elements are OWNED copies (string_init_static / string_alloc_init_static), matching
 * al_str's documented contract, so a mistake shows up as corruption rather than as a
 * quiet pass. */

static String _owned(char const *const text) {
    return string_init_static(text, char_length(text));
}

static USize _arena_blocks_left(Arena *const arena, USize const block_size) {
    USize count = 0;

    while (allocator_try_borrow(block_size, arena) != nullptr) {
        count += 1;
    }

    return count;
}

static void _test_add_and_access(Test *const test) {
    test_case_begin(test, "add_last / at / size");

    AL_String list = al_string_init_1();

    test_expect_true(test, "starts empty", al_string_empty(&list));

    String alpha = _owned("alpha");
    String bravo = _owned("bravo");

    al_string_add_last(&list, &alpha);
    al_string_add_last(&list, &bravo);

    test_expect_u(test, "size 2", 2, al_string_get_size(&list));
    test_expect_false(test, "not empty", al_string_empty(&list));
    test_expect_string(test, "at 0", "alpha", string_get_data(al_string_at(&list, 0)));
    test_expect_string(test, "at 1", "bravo", string_get_data(al_string_at(&list, 1)));
    test_expect_true(test, "elements are owners", al_string_at(&list, 1)->owned);
    test_expect_string(test, "front", "alpha", string_get_data(al_string_front(&list)));
    test_expect_string(test, "back", "bravo", string_get_data(al_string_back(&list)));

    al_string_uninit(&list);

    test_case_end(test);
}

static void _test_insert_at_zero(Test *const test) {
    test_case_begin(test, "insert at 0 into a non-empty list places the element");

    AL_String list = al_string_init_1();
    String bravo   = _owned("bravo");
    String charlie = _owned("charlie");
    String alpha   = _owned("alpha");

    al_string_add_last(&list, &bravo);
    al_string_add_last(&list, &charlie);
    al_string_add_first(&list, &alpha);

    /* The old reverse loop stopped at i > 0 and never wrote index 0: the list grew to 3
     * with slot 0 still holding "bravo". These elements are OWNERS, so that duplicate is
     * an ALIAS and the uninit below released one buffer twice - a direct probe of the
     * unfixed code exited 0xC0000374, STATUS_HEAP_CORRUPTION. */
    test_expect_u(test, "size 3", 3, al_string_get_size(&list));
    test_expect_string(test, "at 0 is the inserted element", "alpha", string_get_data(al_string_at(&list, 0)));
    test_expect_string(test, "at 1", "bravo", string_get_data(al_string_at(&list, 1)));
    test_expect_string(test, "at 2", "charlie", string_get_data(al_string_at(&list, 2)));
    test_expect_false(test, "slot 0 is not an alias of slot 1",
        string_get_data(al_string_at(&list, 0)) == string_get_data(al_string_at(&list, 1)));

    al_string_uninit(&list);

    test_expect_u(test, "uninit after the insert survived", 0, al_string_get_size(&list));

    test_case_end(test);
}

static void _test_add_own_element_refused(Test *const test) {
    test_case_begin(test, "al_string_add with a source that is one of self's own elements is REFUSED - it used to adopt the"
        " element twice (double release at uninit) and, across a growth, read it from the released buffer");

    AL_String list = al_string_init_2(3);
    String alpha   = _owned("alpha");
    String bravo   = _owned("bravo");
    String charlie = _owned("charlie");

    al_string_add_last(&list, &alpha);
    al_string_add_last(&list, &bravo);
    al_string_add_last(&list, &charlie);

    test_expect_u(test, "precondition: full at capacity 3, so the insert would grow", 3, al_string_get_capacity(&list));

    al_string_add(&list, al_string_at(&list, 2), 0);

    test_expect_u(test, "refused: size unchanged", 3, al_string_get_size(&list));
    test_expect_u(test, "refused: no growth happened", 3, al_string_get_capacity(&list));
    test_expect_string(test, "at 0 unchanged", "alpha", string_get_data(al_string_at(&list, 0)));
    test_expect_string(test, "at 2 unchanged", "charlie", string_get_data(al_string_at(&list, 2)));

    al_string_add_first(&list, al_string_at(&list, 1));

    test_expect_u(test, "add_first with an own element: refused too", 3, al_string_get_size(&list));

    al_string_uninit(&list);

    test_expect_u(test, "uninit released each element once", 0, al_string_get_size(&list));

    test_case_end(test);
}

static void _test_remove_first_keeps_survivors(Test *const test) {
    test_case_begin(test, "remove index 0 leaves the survivors' characters intact");

    AL_String list = al_string_init_1();
    String alpha   = _owned("alpha");
    String beta    = _owned("beta");
    String gamma   = _owned("gamma");

    al_string_add_last(&list, &alpha);
    al_string_add_last(&list, &beta);
    al_string_add_last(&list, &gamma);

    al_string_remove(&list, 0);

    /* Shift-then-release aliased data[size - 1] onto the element now at size - 2, so
     * clearing the tail freed gamma's buffer out from under the live element. */
    test_expect_u(test, "size 2", 2, al_string_get_size(&list));
    test_expect_string(test, "beta shifted down whole", "beta", string_get_data(al_string_at(&list, 0)));
    test_expect_u(test, "beta keeps its length", 4, string_get_size(al_string_at(&list, 0)));
    test_expect_string(test, "gamma shifted down whole", "gamma", string_get_data(al_string_at(&list, 1)));
    test_expect_u(test, "gamma keeps its length", 5, string_get_size(al_string_at(&list, 1)));
    test_expect_true(test, "survivors still own their buffers", al_string_at(&list, 1)->owned);

    al_string_uninit(&list);

    test_case_end(test);
}

static void _test_remove_middle_keeps_survivors(Test *const test) {
    test_case_begin(test, "remove from the middle leaves the survivors' characters intact");

    AL_String list = al_string_init_1();
    String alpha   = _owned("alpha");
    String bravo   = _owned("bravo");
    String charlie = _owned("charlie");
    String delta   = _owned("delta");

    al_string_add_last(&list, &alpha);
    al_string_add_last(&list, &bravo);
    al_string_add_last(&list, &charlie);
    al_string_add_last(&list, &delta);

    al_string_remove(&list, 1);

    test_expect_u(test, "size 3", 3, al_string_get_size(&list));
    test_expect_string(test, "alpha survives", "alpha", string_get_data(al_string_at(&list, 0)));
    test_expect_string(test, "charlie shifted down", "charlie", string_get_data(al_string_at(&list, 1)));
    test_expect_u(test, "charlie keeps its length", 7, string_get_size(al_string_at(&list, 1)));
    test_expect_string(test, "delta shifted down", "delta", string_get_data(al_string_at(&list, 2)));
    test_expect_u(test, "delta keeps its length", 5, string_get_size(al_string_at(&list, 2)));

    al_string_uninit(&list);

    test_case_end(test);
}

static void _test_remove_then_reuse(Test *const test) {
    test_case_begin(test, "appending after a remove does not disturb the survivors");

    AL_String list = al_string_init_1();
    String one     = _owned("one");
    String two     = _owned("two");
    String three   = _owned("three");

    al_string_add_last(&list, &one);
    al_string_add_last(&list, &two);
    al_string_add_last(&list, &three);

    al_string_remove(&list, 0);

    /* A fresh allocation here is the likeliest claimant of a buffer the remove
     * released by mistake, which is what turns the alias into visible corruption. */
    String four = _owned("four");
    String five = _owned("five");

    al_string_add_last(&list, &four);
    al_string_add_last(&list, &five);

    test_expect_u(test, "size 4", 4, al_string_get_size(&list));
    test_expect_string(test, "two still whole", "two", string_get_data(al_string_at(&list, 0)));
    test_expect_string(test, "three still whole", "three", string_get_data(al_string_at(&list, 1)));
    test_expect_string(test, "four appended", "four", string_get_data(al_string_at(&list, 2)));
    test_expect_string(test, "five appended", "five", string_get_data(al_string_at(&list, 3)));

    al_string_remove_last(&list);
    test_expect_u(test, "size 3 after remove_last", 3, al_string_get_size(&list));
    test_expect_string(test, "four is the new last", "four", string_get_data(al_string_back(&list)));

    al_string_remove_first(&list);
    test_expect_u(test, "size 2 after remove_first", 2, al_string_get_size(&list));
    test_expect_string(test, "three is the new first", "three", string_get_data(al_string_front(&list)));

    al_string_uninit(&list);

    test_case_end(test);
}

static void _test_clear_releases_and_reuses(Test *const test) {
    test_case_begin(test, "clear empties the list and leaves it usable");

    AL_String list = al_string_init_1();
    String alpha   = _owned("alpha");
    String bravo   = _owned("bravo");
    String charlie = _owned("charlie");

    al_string_add_last(&list, &alpha);
    al_string_add_last(&list, &bravo);
    al_string_add_last(&list, &charlie);

    al_string_clear(&list);

    test_expect_u(test, "size 0 after clear", 0, al_string_get_size(&list));
    test_expect_true(test, "clear keeps the backing array", al_string_get_capacity(&list) > 0);
    test_expect_true(test, "empty after clear", al_string_empty(&list));

    /* Slot 0 is deliberately NOT read here. at() is now bounded by size rather
     * than capacity, so reading a slot inside the retained capacity but past the
     * size is out of contract - which is what this tightening is for. That the
     * slot was reset rather than left aliasing the released element is proven by
     * the re-add below instead. */

    String again = _owned("again");

    al_string_add_last(&list, &again);

    test_expect_u(test, "reusable after clear", 1, al_string_get_size(&list));
    test_expect_string(test, "at 0 after clear", "again", string_get_data(al_string_at(&list, 0)));

    al_string_uninit(&list);

    test_case_end(test);
}

static void _test_uninit_resets_and_repeats(Test *const test) {
    test_case_begin(test, "uninit resets size and capacity and is idempotent");

    AL_String list = al_string_init_1();
    String alpha   = _owned("alpha");
    String bravo   = _owned("bravo");

    al_string_add_last(&list, &alpha);
    al_string_add_last(&list, &bravo);

    al_string_uninit(&list);

    test_expect_u(test, "size 0 after uninit", 0, al_string_get_size(&list));
    test_expect_u(test, "capacity 0 after uninit", 0, al_string_get_capacity(&list));
    test_expect_true(test, "empty after uninit", al_string_empty(&list));
    test_expect_null(test, "data released", al_string_get_data(&list));

    al_string_uninit(&list);

    test_expect_u(test, "second uninit keeps size 0", 0, al_string_get_size(&list));
    test_expect_u(test, "second uninit keeps capacity 0", 0, al_string_get_capacity(&list));

    /* A list that never grew has data == nullptr; uninit must not hand that on. */
    AL_String never_grown = al_string_init_1();

    al_string_uninit(&never_grown);

    test_expect_u(test, "never-grown uninit survived", 0, al_string_get_size(&never_grown));

    test_case_end(test);
}

static void _test_arena_growth_keeps_content(Test *const test) {
    test_case_begin(test, "arena-backed growth and shrink keep the elements");

    Arena arena     = arena_init_1(8192, ARENA_TYPE_LINEAR);
    AL_String list  = al_string_alloc_init_2(2, &arena);

    test_expect_u(test, "capacity 2", 2, al_string_get_capacity(&list));

    String alpha    = string_alloc_init_static("alpha", char_length("alpha"), &arena);
    String bravo    = string_alloc_init_static("bravo", char_length("bravo"), &arena);
    String charlie  = string_alloc_init_static("charlie", char_length("charlie"), &arena);

    al_string_add_last(&list, &alpha);
    al_string_add_last(&list, &bravo);
    al_string_add_last(&list, &charlie);

    test_expect_u(test, "size 3", 3, al_string_get_size(&list));
    test_expect_true(test, "capacity crossed", al_string_get_capacity(&list) >= 3);
    test_expect_string(test, "at 0 across the growth", "alpha", string_get_data(al_string_at(&list, 0)));
    test_expect_string(test, "at 1 across the growth", "bravo", string_get_data(al_string_at(&list, 1)));
    test_expect_string(test, "at 2 across the growth", "charlie", string_get_data(al_string_at(&list, 2)));

    al_string_reserve(&list, 32);

    test_expect_u(test, "capacity 32 after reserve", 32, al_string_get_capacity(&list));
    test_expect_string(test, "at 0 across the reserve", "alpha", string_get_data(al_string_at(&list, 0)));
    test_expect_string(test, "at 2 across the reserve", "charlie", string_get_data(al_string_at(&list, 2)));

    al_string_shrink(&list);

    test_expect_u(test, "capacity 3 after shrink", 3, al_string_get_capacity(&list));
    test_expect_string(test, "at 0 across the shrink", "alpha", string_get_data(al_string_at(&list, 0)));
    test_expect_string(test, "at 2 across the shrink", "charlie", string_get_data(al_string_at(&list, 2)));

    al_string_uninit(&list);
    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_expect_u(test, "teardown left the list empty", 0, al_string_get_size(&list));

    test_case_end(test);
}

static void _test_arena_growth_borrows_from_the_arena(Test *const test) {
    test_case_begin(test, "reserve on an arena-backed list borrows from that arena");

    /* Content cannot see this defect: a heap borrow copies just as faithfully, and a
     * linear arena's release of a foreign block is a no-op. What it CAN see is that
     * the arena never shrank. The control list borrows its initial array and nothing
     * else, so the difference below is the growth itself. */
    Arena control_arena     = arena_init_1(4096, ARENA_TYPE_LINEAR);
    AL_String control_list  = al_string_alloc_init_2(2, &control_arena);
    USize const control_left = _arena_blocks_left(&control_arena, 128);

    al_string_uninit(&control_list);
    arena_uninit(&control_arena, ARENA_TYPE_LINEAR);

    test_expect_true(test, "control arena still had room to measure", control_left > 0);

    Arena arena     = arena_init_1(4096, ARENA_TYPE_LINEAR);
    AL_String list  = al_string_alloc_init_2(2, &arena);

    /* HEAP elements on purpose: only the list's own array should account for the
     * arena space the probe measures. */
    String one   = _owned("one");
    String two   = _owned("two");
    String three = _owned("three");

    al_string_add_last(&list, &one);
    al_string_add_last(&list, &two);
    al_string_add_last(&list, &three);

    al_string_reserve(&list, 64);

    test_expect_u(test, "capacity 64 after reserve", 64, al_string_get_capacity(&list));
    test_expect_string(test, "content survived the growth", "one", string_get_data(al_string_at(&list, 0)));
    test_expect_string(test, "content survived the growth 2", "three", string_get_data(al_string_at(&list, 2)));

    USize const grown_left = _arena_blocks_left(&arena, 128);

    test_expect_true(test, "the growth came out of the arena", grown_left < control_left);

    al_string_uninit(&list);
    arena_uninit(&arena, ARENA_TYPE_LINEAR);

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

    Test test = test_init("tests/container/arrayList/test_al_string.c");

    test_suite_begin(&test, "al_string");
    _test_add_and_access(&test);
    _test_insert_at_zero(&test);
    _test_add_own_element_refused(&test);
    _test_remove_first_keeps_survivors(&test);
    _test_remove_middle_keeps_survivors(&test);
    _test_remove_then_reuse(&test);
    _test_clear_releases_and_reuses(&test);
    _test_uninit_resets_and_repeats(&test);
    _test_arena_growth_keeps_content(&test);
    _test_arena_growth_borrows_from_the_arena(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}