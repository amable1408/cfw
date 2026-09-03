#include <stdio.h>

#include <container/arrayList/al_bool.h>
#include <test/test.h>

/* Exercises AL_Bool across the raw (capacity <= 64, single word) / dyn (capacity > 64,
 * heap word array) boundary, which is where its historical bugs lived: add of a false
 * value, raw->dyn growth, reserve/shrink union transitions, and insert/remove shifting. */

static void _fill_last(AL_Bool *const list, bool const *const pattern, USize const count) {
    for (USize i = 0; i < count; ++i) {
        al_bool_add_last(list, pattern[i]);
    }
}

static void _verify(Test *const test, char const *const label, AL_Bool const *const list, bool const *const pattern, USize const count) {
    char name[128] = DEFAULT_INITIALIZATION;

    sprintf(name, "%s: size", label);
    test_expect_u(test, name, count, al_bool_get_size(list));

    for (USize i = 0; i < count; ++i) {
        sprintf(name, "%s: bit[%zu]", label, i);
        test_expect_bool(test, name, pattern[i], al_bool_at(list, i));
    }
}

static void _test_raw_basic(Test *const test) {
    test_case_begin(test, "raw add_last incl. false values");

    bool const pattern[10] = { true, false, true, true, false, false, true, false, true, true };

    AL_Bool list = al_bool_init_2(64);
    _fill_last(&list, pattern, 10);

    _verify(test, "raw", &list, pattern, 10);
    test_api_pass(test, "al_bool_add_last");
    test_api_pass(test, "al_bool_at");

    al_bool_uninit(&list);

    test_case_end(test);
}

static void _test_grow_raw_to_dyn(Test *const test) {
    test_case_begin(test, "grow from empty across the 64-bit boundary");

    bool pattern[100] = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 100; ++i) {
        pattern[i] = (i % 3) == 0;
    }

    AL_Bool list = al_bool_init_1();
    _fill_last(&list, pattern, 100);

    _verify(test, "grow", &list, pattern, 100);
    test_api_pass(test, "al_bool_init_1");

    al_bool_uninit(&list);

    test_case_end(test);
}

static void _test_reserve_raw_to_dyn(Test *const test) {
    test_case_begin(test, "reserve raw->dyn preserves bits");

    bool pattern[40] = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 40; ++i) {
        pattern[i] = (i % 2) == 0;
    }

    AL_Bool list = al_bool_init_2(50);
    _fill_last(&list, pattern, 40);

    al_bool_reserve(&list, 200);

    test_expect_true(test, "capacity grew", al_bool_get_capacity(&list) >= 200);
    _verify(test, "reserve", &list, pattern, 40);
    test_api_pass(test, "al_bool_reserve");

    al_bool_uninit(&list);

    test_case_end(test);
}

static void _test_remove_shift(Test *const test) {
    test_case_begin(test, "remove shifts remaining bits down (dyn)");

    bool pattern[130] = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 130; ++i) {
        pattern[i] = (i % 5) == 0;
    }

    AL_Bool list = al_bool_init_1();
    _fill_last(&list, pattern, 130);

    /* remove index 5; everything after shifts down one. */
    al_bool_remove(&list, 5);

    bool expected[129] = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 5; ++i) {
        expected[i] = pattern[i];
    }

    for (USize i = 5; i < 129; ++i) {
        expected[i] = pattern[i + 1];
    }

    _verify(test, "remove", &list, expected, 129);
    test_api_pass(test, "al_bool_remove");

    al_bool_uninit(&list);

    test_case_end(test);
}

static void _test_insert_middle(Test *const test) {
    test_case_begin(test, "insert in the middle shifts bits up");

    bool const start[3] = { true, true, true };

    AL_Bool list = al_bool_init_2(64);
    _fill_last(&list, start, 3);

    /* insert false at index 1: [T,T,T] -> [T,F,T,T] */
    al_bool_add(&list, false, 1);

    bool const expected[4] = { true, false, true, true };
    _verify(test, "insert", &list, expected, 4);
    test_api_pass(test, "al_bool_add");

    al_bool_uninit(&list);

    test_case_end(test);
}

static void _test_shrink_dyn_to_raw(Test *const test) {
    test_case_begin(test, "shrink dyn->raw preserves bits");

    bool pattern[30] = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 30; ++i) {
        pattern[i] = (i % 4) == 0;
    }

    AL_Bool list = al_bool_init_2(200);
    _fill_last(&list, pattern, 30);

    al_bool_shrink(&list);

    test_expect_u(test, "capacity shrank to size", 30, al_bool_get_capacity(&list));
    _verify(test, "shrink", &list, pattern, 30);
    test_api_pass(test, "al_bool_shrink");

    al_bool_uninit(&list);

    test_case_end(test);
}

static void _test_init_3_and_clear(Test *const test) {
    test_case_begin(test, "init_3 from array + clear");

    bool source[70] = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 70; ++i) {
        source[i] = (i % 7) == 0;
    }

    AL_Bool list = al_bool_init_3(source, 70);

    _verify(test, "init_3", &list, source, 70);
    test_api_pass(test, "al_bool_init_3");

    al_bool_clear(&list);
    test_expect_u(test, "cleared size", 0, al_bool_get_size(&list));
    test_expect_true(test, "cleared is empty", al_bool_empty(&list));
    test_api_pass(test, "al_bool_clear");

    al_bool_uninit(&list);

    test_case_end(test);
}

int main(void) {
    log_init((LogConfig){ .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true });

    Test test = test_init("tests/container/arrayList/test_al_bool.c");

    test_suite_begin(&test, "al_bool");

    test_api_begin(&test, "al_bool", 9);

    _test_raw_basic(&test);
    _test_grow_raw_to_dyn(&test);
    _test_reserve_raw_to_dyn(&test);
    _test_remove_shift(&test);
    _test_insert_middle(&test);
    _test_shrink_dyn_to_raw(&test);
    _test_init_3_and_clear(&test);

    test_api_end(&test);

    test_suite_end(&test);

    return test_uninit(&test);
}