#include <test/test.h>

#include <container/arrayList/al_al_char.h>

/* Coverage for AL_AL_Char, the nested instantiation that holds AL_Char sublists
 * (each of which owns a run of heap strings). Two ownership levels deep, so a
 * mistake here is a double free or a leaked backing array, not a wrong value.
 *
 * R18 regression: al_al_char_clear used to release each nested list with
 * al_char_clear (frees the strings, keeps the nested backing array) and then
 * zero the outer size - so the later al_al_char_uninit loop, bounded by that
 * now-zero size, could never reach the nested array to free it. clear() leaked
 * one backing array per nested list; clear() followed by uninit() is exactly the
 * sequence that exposed it, so that is the sequence pinned below. */

static AL_Char _strings(char const *const a, char const *const b) {
    AL_Char list = al_char_init_1();

    al_char_add_last(&list, char_new_2(a));
    al_char_add_last(&list, char_new_2(b));

    return list;
}

static void _test_clear_then_uninit_no_double_free(Test *const test) {
    test_case_begin(test, "clear() then uninit() releases each nested list exactly once (R18)");

    AL_AL_Char list = al_al_char_init_1();

    AL_Char fruits = _strings("apple", "banana");
    AL_Char cities = _strings("lima", "quito");
    AL_Char colors = _strings("red", "blue");

    al_al_char_add_last(&list, &fruits);
    al_al_char_add_last(&list, &cities);
    al_al_char_add_last(&list, &colors);

    test_expect_u(test, "size 3", 3, al_al_char_get_size(&list));

    al_al_char_clear(&list);

    test_expect_u(test, "size 0 after clear", 0, al_al_char_get_size(&list));

    // Reusable after clear: the backing array survives, only the elements were released.
    AL_Char again = _strings("one", "two");

    al_al_char_add_last(&list, &again);

    test_expect_u(test, "size 1 after reuse", 1, al_al_char_get_size(&list));
    test_expect_string(test, "reused element intact", "one", al_al_char_at_1(&list, 0)[0]);

    // The double free lived here: a second release of the SAME nested arrays clear already froze.
    al_al_char_uninit(&list);

    test_expect_u(test, "size 0 after uninit", 0, al_al_char_get_size(&list));
    test_expect_u(test, "capacity 0 after uninit", 0, al_al_char_get_capacity(&list));

    test_case_end(test);
}

static void _test_uninit_without_clear(Test *const test) {
    test_case_begin(test, "uninit alone (no prior clear) releases every nested list");

    AL_AL_Char list = al_al_char_init_1();

    AL_Char one = _strings("a", "b");
    AL_Char two = _strings("c", "d");

    al_al_char_add_last(&list, &one);
    al_al_char_add_last(&list, &two);

    test_expect_u(test, "size 2", 2, al_al_char_get_size(&list));

    al_al_char_uninit(&list);

    test_expect_u(test, "size 0 after uninit", 0, al_al_char_get_size(&list));
    test_expect_u(test, "capacity 0 after uninit", 0, al_al_char_get_capacity(&list));

    test_case_end(test);
}

static void _test_add_first_shifts_earlier_elements(Test *const test) {
    test_case_begin(test, "add_first places the new element at index 0 and shifts the rest right");

    AL_AL_Char list = al_al_char_init_1();

    AL_Char first_added  = _strings("alpha", "beta");
    AL_Char second_added = _strings("gamma", "delta");

    al_al_char_add_last(&list, &first_added);
    al_al_char_add_last(&list, &second_added);

    AL_Char newest = _strings("front", "row");

    al_al_char_add_first(&list, &newest);

    test_expect_u(test, "size 3", 3, al_al_char_get_size(&list));
    test_expect_string(test, "index 0 is the newest element", "front", al_al_char_at_1(&list, 0)[0]);
    test_expect_string(test, "index 1 is the first-added element, shifted right", "alpha", al_al_char_at_1(&list, 1)[0]);
    test_expect_string(test, "index 2 is the second-added element, shifted right", "gamma", al_al_char_at_1(&list, 2)[0]);

    al_al_char_uninit(&list);

    test_case_end(test);
}

static void _test_add_own_element_refused(Test *const test) {
    test_case_begin(test, "adding a pointer into the list's own storage is REFUSED - never adopted twice");

    AL_AL_Char list = al_al_char_init_1();

    AL_Char alpha = _strings("alpha", "one");
    AL_Char bravo = _strings("bravo", "two");

    al_al_char_add_last(&list, &alpha);
    al_al_char_add_last(&list, &bravo);

    /* al_al_char_at_2(&list, 1) is a pointer INTO self->data, not a caller-owned
     * local - exactly the alias add() checks for and declines before it can be
     * owned (and later released) twice. */
    al_al_char_add(&list, al_al_char_at_2(&list, 1), 0);

    test_expect_u(test, "refused: size unchanged", 2, al_al_char_get_size(&list));
    test_expect_string(test, "index 0 unchanged", "alpha", al_al_char_at_1(&list, 0)[0]);
    test_expect_string(test, "index 1 unchanged", "bravo", al_al_char_at_1(&list, 1)[0]);

    al_al_char_uninit(&list);

    test_expect_u(test, "uninit released each element exactly once", 0, al_al_char_get_size(&list));

    test_case_end(test);
}

static void _test_at_1_and_at_2(Test *const test) {
    test_case_begin(test, "at_1 (the raw char** array) and at_2 (the AL_Char*) agree on the same element");

    AL_AL_Char list = al_al_char_init_1();

    AL_Char only = _strings("agreement", "check");

    al_al_char_add_last(&list, &only);

    char **const raw     = al_al_char_at_1(&list, 0);
    AL_Char *const nested = al_al_char_at_2(&list, 0);

    test_expect_not_null(test, "at_1 returns the string array", raw);
    test_expect_not_null(test, "at_2 returns the nested list", nested);
    test_expect_string(test, "at_1[0] is the first string", "agreement", raw[0]);
    test_expect_string(test, "at_2's own al_char_at(0) agrees with at_1[0]", raw[0], al_char_at(nested, 0));
    test_expect_true(test, "at_1 is exactly at_2's backing array", raw == al_char_get_data(nested));
    test_expect_u(test, "at_2 reports the nested size", 2, al_char_get_size(nested));

    al_al_char_uninit(&list);

    test_case_end(test);
}

static void _test_remove_releases_the_nested_list(Test *const test) {
    test_case_begin(test, "remove releases the nested list at that index before shifting the tail down");

    AL_AL_Char list = al_al_char_init_1();

    AL_Char first  = _strings("one", "1");
    AL_Char second = _strings("two", "2");
    AL_Char third  = _strings("three", "3");

    al_al_char_add_last(&list, &first);
    al_al_char_add_last(&list, &second);
    al_al_char_add_last(&list, &third);

    al_al_char_remove(&list, 1);

    test_expect_u(test, "size 2 after removing the middle element", 2, al_al_char_get_size(&list));
    test_expect_string(test, "index 0 survives", "one", al_al_char_at_1(&list, 0)[0]);
    test_expect_string(test, "index 1 shifted down", "three", al_al_char_at_1(&list, 1)[0]);

    al_al_char_uninit(&list);

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

    Test test = test_init("tests/container/arrayList/test_al_al_char.c");

    test_suite_begin(&test, "al_al_char");
    _test_clear_then_uninit_no_double_free(&test);
    _test_uninit_without_clear(&test);
    _test_add_first_shifts_earlier_elements(&test);
    _test_add_own_element_refused(&test);
    _test_at_1_and_at_2(&test);
    _test_remove_releases_the_nested_list(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}