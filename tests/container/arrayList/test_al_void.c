#include <test/test.h>

#include <container/arrayList/al_void.h>

/* Coverage for the AL_Void pointer list: append + growth, indexed insert, the remove
 * family, reserve/shrink, and a regression for uninit on a never-grown (empty) list. */

static void _test_basic(Test *const test) {
    test_case_begin(test, "add_last / access / size");

    I32 a = 0;
    I32 b = 0;
    I32 c = 0;
    AL_Void list = al_void_init_1();

    test_expect_true(test, "starts empty", al_void_empty(&list));

    al_void_add_last(&list, &a);
    al_void_add_last(&list, &b);
    al_void_add_last(&list, &c);

    test_expect_u(test, "size 3", 3, al_void_get_size(&list));
    test_expect_false(test, "not empty", al_void_empty(&list));
    test_expect_true(test, "front a", *al_void_front(&list) == &a);
    test_expect_true(test, "back c", *al_void_back(&list) == &c);
    test_expect_true(test, "at 1 b", *al_void_at(&list, 1) == &b);

    al_void_uninit(&list);

    test_case_end(test);
}

static void _test_growth(Test *const test) {
    test_case_begin(test, "growth preserves order");

    void *sentinels[100] = DEFAULT_INITIALIZATION;
    AL_Void list         = al_void_init_1();

    for (USize i = 0; i < 100; i += 1) {
        sentinels[i] = &sentinels[i];

        al_void_add_last(&list, sentinels[i]);
    }

    test_expect_u(test, "size 100", 100, al_void_get_size(&list));
    test_expect_true(test, "capacity >= size", al_void_get_capacity(&list) >= 100);

    bool ordered = true;

    for (USize i = 0; i < 100; i += 1) {
        if (*al_void_at(&list, i) != sentinels[i]) {
            ordered = false;

            break;
        }
    }

    test_expect_true(test, "all retained in order", ordered);

    al_void_uninit(&list);

    test_case_end(test);
}

static void _test_insert_remove(Test *const test) {
    test_case_begin(test, "insert at index / remove variants");

    I32 a = 0;
    I32 b = 0;
    I32 c = 0;
    I32 d = 0;
    AL_Void list = al_void_init_2(2);

    al_void_add_last(&list, &a);
    al_void_add_last(&list, &c);
    al_void_add(&list, &b, 1);

    test_expect_u(test, "size 3", 3, al_void_get_size(&list));
    test_expect_true(test, "at 0 a", *al_void_at(&list, 0) == &a);
    test_expect_true(test, "at 1 b", *al_void_at(&list, 1) == &b);
    test_expect_true(test, "at 2 c", *al_void_at(&list, 2) == &c);

    al_void_add_first(&list, &d);

    test_expect_true(test, "front d", *al_void_front(&list) == &d);
    test_expect_true(test, "at 1 a", *al_void_at(&list, 1) == &a);

    al_void_remove_first(&list);
    test_expect_true(test, "front a", *al_void_front(&list) == &a);

    al_void_remove_last(&list);
    test_expect_true(test, "back b", *al_void_back(&list) == &b);

    al_void_remove(&list, 0);
    test_expect_u(test, "size 1", 1, al_void_get_size(&list));
    test_expect_true(test, "only b", *al_void_front(&list) == &b);

    al_void_clear(&list);
    test_expect_true(test, "empty after clear", al_void_empty(&list));

    al_void_uninit(&list);

    test_case_end(test);
}

static void _test_reserve_shrink_empty(Test *const test) {
    test_case_begin(test, "reserve / shrink / empty uninit");

    I32 a = 0;
    AL_Void list = al_void_init_1();

    al_void_reserve(&list, 16);
    test_expect_u(test, "capacity 16", 16, al_void_get_capacity(&list));
    test_expect_u(test, "size 0", 0, al_void_get_size(&list));

    al_void_add_last(&list, &a);
    al_void_shrink(&list);
    test_expect_u(test, "capacity 1 after shrink", 1, al_void_get_capacity(&list));
    test_expect_true(test, "still holds a", *al_void_front(&list) == &a);

    al_void_uninit(&list);

    // Regression: uninit on a never-grown (empty) list must not abort on null data.
    AL_Void empty = al_void_init_1();
    al_void_uninit(&empty);
    test_expect_true(test, "empty uninit survived", true);

    test_case_end(test);
}

static void _test_delete(Test *const test) {
    test_case_begin(test, "new / delete nulls the handle (no write-after-free)");

    I32 x = 0;
    AL_Void *list = al_void_new_1();

    al_void_add_last(list, &x);
    al_void_delete(&list);

    test_expect_null(test, "handle nulled after delete", list);

    // Delete of a never-grown (empty) list must not abort on null data.
    AL_Void *empty = al_void_new_1();

    al_void_delete(&empty);

    test_expect_null(test, "empty delete nulled", empty);

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

    Test test = test_init("tests/container/arrayList/test_al_void.c");

    test_suite_begin(&test, "al_void");
    _test_basic(&test);
    _test_growth(&test);
    _test_insert_remove(&test);
    _test_reserve_shrink_empty(&test);
    _test_delete(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}