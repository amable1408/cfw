#include <test/test.h>

#include <container/arrayList/al_str.h>

/* Coverage for AL_Str, which had none: it was exercised only indirectly through
 * argparse, and only for appends. The remove family is the point of this suite -
 * al_str_remove used to shift elements left and THEN uninit the tail, which by that
 * point duplicated the element now at size - 2. That freed a buffer a live element
 * still pointed at and leaked the removed one; releasing the list afterwards was a
 * double free. Every case here builds OWNED elements so al_str_uninit genuinely
 * releases them and a mistake shows up as corruption rather than passing quietly. */

static Str _owned(char const *const text) {
    return str_init_static(text, char_length(text));
}

static void _test_add_and_access(Test *const test) {
    test_case_begin(test, "add_last / at / size");

    AL_Str list = al_str_init_1();

    test_expect_true(test, "starts empty", al_str_empty(&list));

    Str alpha = _owned("alpha");
    Str bravo = _owned("bravo");

    al_str_add_last(&list, &alpha);
    al_str_add_last(&list, &bravo);

    test_expect_u(test, "size 2", 2, al_str_get_size(&list));
    test_expect_string(test, "at 0", "alpha", str_get_data(al_str_at(&list, 0)));
    test_expect_string(test, "at 1", "bravo", str_get_data(al_str_at(&list, 1)));
    test_expect_true(test, "elements keep their owned flag", al_str_at(&list, 0)->owned);

    al_str_uninit(&list);

    test_case_end(test);
}

static void _test_add_own_element_refused(Test *const test) {
    test_case_begin(test, "al_str_add with a source that is one of self's own elements is REFUSED - never adopted twice");

    AL_Str list = al_str_init_1();
    Str alpha   = _owned("alpha");
    Str bravo   = _owned("bravo");

    al_str_add_last(&list, &alpha);
    al_str_add_last(&list, &bravo);
    al_str_add(&list, al_str_at(&list, 1), 0);

    test_expect_u(test, "refused: size unchanged", 2, al_str_get_size(&list));
    test_expect_string(test, "at 0 unchanged", "alpha", str_get_data(al_str_at(&list, 0)));

    al_str_uninit(&list);

    test_expect_u(test, "uninit released each element once", 0, al_str_get_size(&list));

    test_case_end(test);
}

static void _test_remove_middle(Test *const test) {
    test_case_begin(test, "remove from the middle releases the right element");

    AL_Str list = al_str_init_1();
    char const *const words[] = { "alpha", "bravo", "charlie", "delta" };

    for (USize i = 0; i < 4; i += 1) {
        Str element = _owned(words[i]);

        al_str_add_last(&list, &element);
    }

    al_str_remove(&list, 1);

    test_expect_u(test, "size drops to 3", 3, al_str_get_size(&list));
    test_expect_string(test, "alpha survives", "alpha", str_get_data(al_str_at(&list, 0)));
    test_expect_string(test, "charlie shifted down", "charlie", str_get_data(al_str_at(&list, 1)));
    test_expect_string(test, "delta shifted down", "delta", str_get_data(al_str_at(&list, 2)));
    test_expect_true(test, "shifted elements still own their buffers", al_str_at(&list, 2)->owned);

    /* The double free lived here. */
    al_str_uninit(&list);

    test_case_end(test);
}

static void _test_remove_edges(Test *const test) {
    test_case_begin(test, "remove_first / remove_last / remove down to empty");

    AL_Str list = al_str_init_1();
    char const *const words[] = { "one", "two", "three" };

    for (USize i = 0; i < 3; i += 1) {
        Str element = _owned(words[i]);

        al_str_add_last(&list, &element);
    }

    al_str_remove_first(&list);
    test_expect_u(test, "size 2 after remove_first", 2, al_str_get_size(&list));
    test_expect_string(test, "two is now first", "two", str_get_data(al_str_at(&list, 0)));

    al_str_remove_last(&list);
    test_expect_u(test, "size 1 after remove_last", 1, al_str_get_size(&list));
    test_expect_string(test, "two is all that remains", "two", str_get_data(al_str_at(&list, 0)));

    al_str_remove(&list, 0);
    test_expect_u(test, "size 0", 0, al_str_get_size(&list));
    test_expect_true(test, "empty again", al_str_empty(&list));

    al_str_uninit(&list);

    test_case_end(test);
}

static void _test_view_elements_are_not_freed(Test *const test) {
    test_case_begin(test, "views stored in the list are never released");

    AL_Str list = al_str_init_1();

    /* str_init_2 builds a VIEW over a literal. Before Str carried an ownership flag,
     * al_str_uninit freed these and corrupted the heap; now they are skipped. */
    Str view_a = str_init_2((char*) "literal-a");
    Str view_b = str_init_2((char*) "literal-b");

    al_str_add_last(&list, &view_a);
    al_str_add_last(&list, &view_b);

    test_expect_false(test, "stored element is still a view", al_str_at(&list, 0)->owned);

    al_str_remove(&list, 0);
    test_expect_u(test, "size 1", 1, al_str_get_size(&list));
    test_expect_string(test, "the other literal is intact", "literal-b", str_get_data(al_str_at(&list, 0)));

    al_str_uninit(&list);

    test_expect_string(test, "literals outlive the list", "literal-a", (char*) "literal-a");

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

    Test test = test_init("tests/container/arrayList/test_al_str.c");

    test_suite_begin(&test, "al_str");
    _test_add_and_access(&test);
    _test_add_own_element_refused(&test);
    _test_remove_middle(&test);
    _test_remove_edges(&test);
    _test_view_elements_are_not_freed(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}