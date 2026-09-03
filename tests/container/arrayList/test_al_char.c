#include <test/test.h>

#include <container/arrayList/al_char.h>
#include <arena/arena.h>

/* First suite for AL_Char, the workhorse instantiation of the hand-cloned al_* generic.
 * Its point is the insert path. al_char_add used to carry a special case that appended at
 * `size` whenever index was size - 1 - so inserting before the last element silently
 * appended - and a reverse loop guarded by `i > 0`, which could never place at index 0:
 * everything shifted right, the element was dropped, size still grew, and slot 0 kept a
 * stale duplicate of its old occupant. Both shapes are pinned below with content AND
 * pointer-identity assertions, because a stale duplicate reads as a plausible string.
 *
 * Elements are OWNED: al_char_clear releases each one and al_char_remove char_deletes it,
 * so every element here is a heap copy and a mistake shows up as corruption, not as a
 * quiet pass. Appends go through al_char_add at index == size rather than al_char_add_last,
 * to keep the insert pins independent of the add_last contract exercised separately. */

static void _append(AL_Char *const list, char const *const text) {
    al_char_add(list, char_new_2(text), al_char_get_size(list));
}

static void _test_append_and_access(Test *const test) {
    test_case_begin(test, "append at index == size / at / front / back");

    AL_Char list = al_char_init_1();

    test_expect_true(test, "starts empty", al_char_empty(&list));

    _append(&list, "a");
    _append(&list, "b");
    _append(&list, "c");

    test_expect_u(test, "size 3", 3, al_char_get_size(&list));
    test_expect_false(test, "not empty", al_char_empty(&list));
    test_expect_string(test, "at 0", "a", al_char_at(&list, 0));
    test_expect_string(test, "at 1", "b", al_char_at(&list, 1));
    test_expect_string(test, "at 2", "c", al_char_at(&list, 2));
    test_expect_string(test, "front", "a", al_char_front(&list));
    test_expect_string(test, "back", "c", al_char_back(&list));

    al_char_uninit(&list);

    test_expect_u(test, "size 0 after uninit", 0, al_char_get_size(&list));
    test_expect_u(test, "capacity 0 after uninit", 0, al_char_get_capacity(&list));

    test_case_end(test);
}

static void _test_insert_at_zero(Test *const test) {
    test_case_begin(test, "insert at 0 into a non-empty list places the element");

    AL_Char list = al_char_init_1();

    _append(&list, "b");
    _append(&list, "c");

    char *const inserted = char_new_2("a");

    al_char_add(&list, inserted, 0);

    /* The old reverse loop stopped at i > 0, so index 0 was never written: the list
     * grew to 3 with slot 0 still holding "b" - a stale duplicate of slot 1 - and
     * `inserted` leaked unreferenced. */
    test_expect_u(test, "size 3", 3, al_char_get_size(&list));
    test_expect_string(test, "at 0 is the inserted element", "a", al_char_at(&list, 0));
    test_expect_string(test, "at 1", "b", al_char_at(&list, 1));
    test_expect_string(test, "at 2", "c", al_char_at(&list, 2));
    test_expect_true(test, "slot 0 holds the pointer handed in", al_char_at(&list, 0) == inserted);
    test_expect_false(test, "slot 0 is not a duplicate of slot 1", al_char_at(&list, 0) == al_char_at(&list, 1));

    al_char_uninit(&list);

    test_case_end(test);
}

static void _test_insert_before_the_last(Test *const test) {
    test_case_begin(test, "insert at size - 1 inserts, it does not append");

    AL_Char list = al_char_init_1();

    _append(&list, "a");
    _append(&list, "c");

    al_char_add(&list, char_new_2("b"), 1);

    /* The old special case treated index == size - 1 as an append, yielding a, c, b. */
    test_expect_u(test, "size 3", 3, al_char_get_size(&list));
    test_expect_string(test, "at 0", "a", al_char_at(&list, 0));
    test_expect_string(test, "at 1 is the inserted element", "b", al_char_at(&list, 1));
    test_expect_string(test, "at 2 is the old last element", "c", al_char_at(&list, 2));

    al_char_uninit(&list);

    test_case_end(test);
}

static void _test_insert_into_empty(Test *const test) {
    test_case_begin(test, "insert into an empty list / append at index == size");

    AL_Char list = al_char_init_1();

    al_char_add(&list, char_new_2("only"), 0);

    test_expect_u(test, "size 1", 1, al_char_get_size(&list));
    test_expect_string(test, "at 0", "only", al_char_at(&list, 0));

    al_char_add(&list, char_new_2("tail"), al_char_get_size(&list));

    test_expect_u(test, "size 2", 2, al_char_get_size(&list));
    test_expect_string(test, "at 0 unmoved", "only", al_char_at(&list, 0));
    test_expect_string(test, "at 1 appended", "tail", al_char_at(&list, 1));

    al_char_uninit(&list);

    test_case_end(test);
}

static void _test_insert_at_zero_crossing_growth(Test *const test) {
    test_case_begin(test, "insert at 0 that also crosses the capacity");

    AL_Char list = al_char_init_2(2);

    test_expect_u(test, "capacity 2", 2, al_char_get_capacity(&list));

    _append(&list, "y");
    _append(&list, "z");

    test_expect_u(test, "full at capacity", al_char_get_capacity(&list), al_char_get_size(&list));

    al_char_add(&list, char_new_2("x"), 0);

    test_expect_u(test, "size 3", 3, al_char_get_size(&list));
    test_expect_true(test, "capacity grew", al_char_get_capacity(&list) >= 3);
    test_expect_string(test, "at 0", "x", al_char_at(&list, 0));
    test_expect_string(test, "at 1", "y", al_char_at(&list, 1));
    test_expect_string(test, "at 2", "z", al_char_at(&list, 2));

    al_char_uninit(&list);

    test_case_end(test);
}

static void _test_null_elements(Test *const test) {
    test_case_begin(test, "nullptr elements are stored and shifted, never released");

    AL_Char list = al_char_init_1();

    _append(&list, "a");

    /* The module folds an empty pointer to nullptr on purpose, so a null slot is a
     * legal value: clear must skip it rather than hand it to the allocator. */
    al_char_add(&list, nullptr, al_char_get_size(&list));

    test_expect_u(test, "size 2", 2, al_char_get_size(&list));
    test_expect_null(test, "at 1 is null", al_char_at(&list, 1));

    al_char_add(&list, char_new_2("z"), 0);

    test_expect_u(test, "size 3", 3, al_char_get_size(&list));
    test_expect_string(test, "at 0 inserted", "z", al_char_at(&list, 0));
    test_expect_string(test, "at 1 shifted", "a", al_char_at(&list, 1));
    test_expect_null(test, "at 2 is still null", al_char_at(&list, 2));

    al_char_uninit(&list);

    test_expect_u(test, "size 0 after uninit", 0, al_char_get_size(&list));

    test_case_end(test);
}

static void _test_remove_and_reuse(Test *const test) {
    test_case_begin(test, "remove family shifts down and the list stays usable");

    AL_Char list = al_char_init_1();

    _append(&list, "one");
    _append(&list, "two");
    _append(&list, "three");

    al_char_remove(&list, 1);

    test_expect_u(test, "size 2", 2, al_char_get_size(&list));
    test_expect_string(test, "at 0 survives", "one", al_char_at(&list, 0));
    test_expect_string(test, "at 1 shifted down", "three", al_char_at(&list, 1));

    _append(&list, "four");

    test_expect_u(test, "size 3 after reuse", 3, al_char_get_size(&list));
    test_expect_string(test, "at 0 still intact", "one", al_char_at(&list, 0));
    test_expect_string(test, "at 1 still intact", "three", al_char_at(&list, 1));
    test_expect_string(test, "at 2 appended", "four", al_char_at(&list, 2));

    al_char_remove_first(&list);

    test_expect_u(test, "size 2 after remove_first", 2, al_char_get_size(&list));
    test_expect_string(test, "three is now first", "three", al_char_at(&list, 0));

    al_char_remove_last(&list);

    test_expect_u(test, "size 1 after remove_last", 1, al_char_get_size(&list));
    test_expect_string(test, "three is all that remains", "three", al_char_at(&list, 0));

    al_char_clear(&list);

    test_expect_u(test, "size 0 after clear", 0, al_char_get_size(&list));
    test_expect_true(test, "clear keeps the buffer", al_char_get_capacity(&list) > 0);

    _append(&list, "again");

    test_expect_u(test, "reusable after clear", 1, al_char_get_size(&list));
    test_expect_string(test, "at 0 after clear", "again", al_char_at(&list, 0));

    al_char_uninit(&list);

    test_expect_u(test, "capacity 0 after uninit", 0, al_char_get_capacity(&list));

    test_case_end(test);
}

static void _test_add_last_appends(Test *const test) {
    test_case_begin(test, "add_last appends at the end");

    AL_Char list = al_char_init_1();

    /* add_last forwards `size`, matching every other instantiation. It used to
     * forward `size - 1`, an index that was only ever correct against the broken
     * add whose special case rewrote it into an append; against a repaired add
     * that means "insert before the last element". This case pins the documented
     * contract, "add at the end". */
    al_char_add_last(&list, char_new_2("first"));
    al_char_add_last(&list, char_new_2("second"));
    al_char_add_last(&list, char_new_2("third"));

    test_expect_u(test, "size 3", 3, al_char_get_size(&list));
    test_expect_string(test, "at 0", "first", al_char_at(&list, 0));
    test_expect_string(test, "at 1", "second", al_char_at(&list, 1));
    test_expect_string(test, "at 2", "third", al_char_at(&list, 2));
    test_expect_string(test, "back is the last added", "third", al_char_back(&list));

    al_char_uninit(&list);

    test_case_end(test);
}

/* R7 convergence: init_3 used to ADOPT the caller's array, so uninit released
 * memory the list never borrowed - a free of a stack array, or of a block from a
 * different allocator, decided by whoever called it. It copies now. */
static void _test_init_3_copies(Test *const test) {
    test_case_begin(test, "init_3 copies the caller's array instead of adopting it");

    /* NOT const, despite Rule 4: `char *const source[3]` decays to `char *const *`,
     * and al_char_init_3 takes `char **const data` - so the const would be
     * discarded at the call and gcc warns. The array is not modified here; the
     * qualifier simply cannot be expressed against this API. */
    char *source[3] = { char_new_2("one"), char_new_2("two"), char_new_2("three") };

    AL_Char list = al_char_init_3(source, 3);

    test_expect_u(test, "size 3", 3, al_char_get_size(&list));
    test_expect_string(test, "element 0", "one", al_char_at(&list, 0));
    test_expect_string(test, "element 2", "three", al_char_at(&list, 2));

    /* The array is the list's OWN now, so it must not be the caller's. Adoption
     * is exactly what this compares against: the old body assigned `data`
     * straight into the struct. */
    test_expect_true(test, "backing array is not the caller's", al_char_get_data(&list) != source);

    /* The ELEMENTS still transfer by value, matching add(), so the list frees the
     * strings and the caller must not. Only `source` itself stays the caller's,
     * and it is a stack array here - which is precisely what uninit would have
     * tried to free under the old adopting body. */
    al_char_uninit(&list);

    test_case_end(test);
}

/* The constructors were left out of the refusal chain that reserve/shrink got:
 * they stored the requested capacity and never looked at what the borrow
 * returned, so a refused arena left capacity = n with data = null and every
 * later guard - add()'s capacity re-read, shrink, at(), the init_3 copy guard -
 * keyed off a capacity that described storage the list did not have. */
static void _test_constructor_refusals(Test *const test) {
    test_case_begin(test, "init_2 refuses a capacity whose byte size would wrap");

    AL_Char wrapping = al_char_init_2(USIZE_MAX / sizeof(char*) + 2);

    /* sizeof(char*) * capacity wrapped to 8 bytes behind a stored capacity of
     * ~2.3e18, and eight appends then wrote 64 bytes into an 8-byte block. */
    test_expect_u(test, "capacity refused", 0, al_char_get_capacity(&wrapping));
    test_expect_true(test, "no buffer borrowed", al_char_get_data(&wrapping) == nullptr);

    /* Refusing must leave the list USABLE, not broken: it grows normally after. */
    al_char_add_last(&wrapping, char_new_2("after"));

    test_expect_u(test, "usable after the refusal", 1, al_char_get_size(&wrapping));
    test_expect_string(test, "and holds the element", "after", al_char_at(&wrapping, 0));

    al_char_uninit(&wrapping);

    test_case_end(test);

    test_case_begin(test, "a refused arena leaves no list claiming capacity it lacks");

    Arena refused = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    AL_Char borrowed = al_char_alloc_init_2(4, &refused);

    /* capacity stayed at 4 with data null, so add()'s `size == capacity` re-read
     * was false and it wrote through the null - the segfault the reserve-side
     * guard was supposed to have made impossible. */
    test_expect_u(test, "capacity zeroed to match the storage", 0, al_char_get_capacity(&borrowed));

    /* char_new_2, not a literal: this list releases its elements, so if the
     * refusal guard above ever regresses the add SUCCEEDS - and a literal would
     * then be handed to the allocator. An allocated string makes that regression
     * show up as the assertion below failing rather than as a heap abort. */
    char *const declined = char_new_2("borrowed");

    al_char_add_last(&borrowed, declined);

    test_expect_u(test, "add declined instead of writing through null", 0, al_char_get_size(&borrowed));

    /* The add DECLINED, so the list never took this string and nothing will ever
     * release it - held in a named local precisely so the test can. Without this
     * the suite leaks it, which LeakSanitizer reports against al_char.c on Linux
     * even though the module is blameless. */
    char_delete(declined);

    test_case_end(test);

    test_case_begin(test, "init_3 declines when the arena refuses the copy");

    /* Allocated, not literals, for the same reason as the add_last case above:
     * AL_Char releases its elements, so if the refusal guard regresses the copy
     * SUCCEEDS and uninit hands .rodata to the allocator - a heap abort that
     * masks the regression instead of failing as an assertion. */
    /* NOT const, for the same reason as `source` above: the qualifier would be
     * discarded at the al_char_alloc_init_3 call. */
    char *elements[2] = { char_new_2("one"), char_new_2("two") };

    /* The R7 guard tested `capacity < data_size`, but init_2 assigns
     * capacity = data_size unconditionally - so the test was `n < n`, never
     * true, and the copy loop ran through a null data. A guard that read as
     * protection and provided none. */
    AL_Char copied = al_char_alloc_init_3(elements, 2, &refused);

    test_expect_u(test, "nothing copied", 0, al_char_get_size(&copied));
    test_expect_u(test, "capacity honest", 0, al_char_get_capacity(&copied));

    /* A no-op at capacity 0, but it would be the leak site if the guard regressed. */
    al_char_uninit(&copied);

    /* Nothing was copied, so the list owns neither element and uninit released
     * neither - the test allocated them, so the test frees them. */
    char_delete(elements[0]);
    char_delete(elements[1]);

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

    Test test = test_init("tests/container/arrayList/test_al_char.c");

    test_suite_begin(&test, "al_char");
    _test_append_and_access(&test);
    _test_insert_at_zero(&test);
    _test_insert_before_the_last(&test);
    _test_insert_into_empty(&test);
    _test_insert_at_zero_crossing_growth(&test);
    _test_null_elements(&test);
    _test_remove_and_reuse(&test);
    _test_add_last_appends(&test);
    _test_init_3_copies(&test);
    _test_constructor_refusals(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}