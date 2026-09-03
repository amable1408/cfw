#include <test/test.h>

#include <container/arrayList/al_u64.h>

/* Regression coverage for the al_u64_delete fixes (representative of the numeric al_*
 * family, whose delete bodies are identical): the write-after-free is gone and deleting
 * a never-grown (empty) list must not abort on a null data buffer. */

static void _test_basic(Test *const test) {
    test_case_begin(test, "add_last / at / size");

    AL_U64 list = al_u64_init_1();

    al_u64_add_last(&list, 10);
    al_u64_add_last(&list, 20);

    test_expect_u(test, "size 2", 2, al_u64_get_size(&list));
    test_expect_u(test, "at 0 == 10", 10, *al_u64_at(&list, 0));
    test_expect_u(test, "at 1 == 20", 20, *al_u64_at(&list, 1));

    al_u64_uninit(&list);

    test_case_end(test);
}

static void _test_insert(Test *const test) {
    test_case_begin(test, "add_first / add-at-index preserve order (no element loss)");

    AL_U64 list = al_u64_init_1();

    al_u64_add_last(&list, 1);
    al_u64_add_last(&list, 2);
    al_u64_add_last(&list, 3);

    // add_first with size > 1 used to LOSE the element (the family insert-at-0 bug).
    al_u64_add_first(&list, 9);

    test_expect_u(test, "size 4", 4, al_u64_get_size(&list));
    test_expect_u(test, "front 9", 9, *al_u64_front(&list));
    test_expect_u(test, "at 1 == 1", 1, *al_u64_at(&list, 1));
    test_expect_u(test, "at 2 == 2", 2, *al_u64_at(&list, 2));
    test_expect_u(test, "back 3", 3, *al_u64_back(&list));

    // Insert in the middle: [9, 1, 2, 3] -> [9, 1, 7, 2, 3].
    al_u64_add(&list, 7, 2);

    test_expect_u(test, "size 5", 5, al_u64_get_size(&list));
    test_expect_u(test, "at 2 == 7", 7, *al_u64_at(&list, 2));
    test_expect_u(test, "at 3 == 2", 2, *al_u64_at(&list, 3));
    test_expect_u(test, "back still 3", 3, *al_u64_back(&list));

    al_u64_uninit(&list);

    test_case_end(test);
}

static void _test_delete(Test *const test) {
    test_case_begin(test, "new / delete nulls the handle (no write-after-free)");

    AL_U64 *list = al_u64_new_1();

    al_u64_add_last(list, 42);
    al_u64_delete(&list);

    /* delete's handle clear is gated on MEMORY_NON_DANGLING_POINTER - that gated
     * form is the style guide's own canonical example - so asserting it
     * unconditionally made this suite unbuildable in the other configuration.
     * uninit's clear is NOT gated (it is load-bearing) and is asserted plainly
     * in _test_refusals; this one has to follow the flag it depends on. */
#ifdef MEMORY_NON_DANGLING_POINTER
    test_expect_null(test, "handle nulled after delete", list);
#endif // MEMORY_NON_DANGLING_POINTER

    // Delete of a never-grown (empty) list must not abort on null data.
    AL_U64 *empty = al_u64_new_1();

    al_u64_delete(&empty);

#ifdef MEMORY_NON_DANGLING_POINTER
    test_expect_null(test, "empty delete nulled", empty);
#endif // MEMORY_NON_DANGLING_POINTER

    test_case_end(test);
}

/* Every refusal this round introduced, exercised through the PUBLIC API only.
 * Each of these was a crash or a silent corruption before the fix, so a suite
 * that never reaches them proves nothing about the guards. */
static void _test_refusals(Test *const test) {
    test_case_begin(test, "back and front answer null on an empty list");

    AL_U64 empty = al_u64_init_1();

    /* `self->size - 1` underflowed to USIZE_MAX here and formed a wild address
     * the caller then dereferenced. */
    test_expect_null(test, "back of an empty list", al_u64_back(&empty));
    test_expect_null(test, "front of an empty list", al_u64_front(&empty));

    /* A list that HAS a buffer but no elements. Without this the front
     * assertion above passes for the wrong reason: &data[0] on a never-allocated
     * list is null regardless of the guard, so removing the guard left it green.
     * Here data is a real address, so only the guard can return null. */
    AL_U64 allocated = al_u64_init_2(4);

    test_expect_true(test, "allocated but empty", al_u64_get_capacity(&allocated) > 0 && al_u64_empty(&allocated));
    test_expect_null(test, "front of an allocated but empty list", al_u64_front(&allocated));
    test_expect_null(test, "back of an allocated but empty list", al_u64_back(&allocated));

    al_u64_uninit(&allocated);

    al_u64_add_last(&empty, 7);

    test_expect_u(test, "back once non-empty", 7, *al_u64_back(&empty));
    test_expect_u(test, "front once non-empty", 7, *al_u64_front(&empty));

    al_u64_uninit(&empty);

    test_case_end(test);

    test_case_begin(test, "shrink on an allocated but empty list releases instead of borrowing zero");

    /* memory_alloc aborts on a zero byte count, so this pure public sequence -
     * reserve a capacity, never add - killed the process at the shrink. */
    AL_U64 reserved = al_u64_init_2(8);

    al_u64_shrink(&reserved);

    test_expect_u(test, "capacity dropped to 0", 0, al_u64_get_capacity(&reserved));
    test_expect_u(test, "still empty", 0, al_u64_get_size(&reserved));
    test_expect_true(test, "usable after the shrink", al_u64_empty(&reserved));

    al_u64_add_last(&reserved, 3);

    test_expect_u(test, "grows again after the shrink", 3, *al_u64_at(&reserved, 0));

    al_u64_uninit(&reserved);

    test_case_end(test);

    test_case_begin(test, "reserve refuses a capacity whose byte size would wrap");

    AL_U64 wrapping = al_u64_init_1();

    /* sizeof(U64) * capacity wrapped to 8 bytes while the stored capacity stayed
     * at ~2.3e18, so every later add wrote past an 8-byte block - silent heap
     * corruption in a fully ERROR_CHECK_ENABLED build. */
    al_u64_reserve(&wrapping, USIZE_MAX / sizeof(U64) + 2);

    test_expect_u(test, "capacity unchanged by the refusal", 0, al_u64_get_capacity(&wrapping));
    test_expect_true(test, "list still empty", al_u64_empty(&wrapping));

    al_u64_add_last(&wrapping, 5);

    test_expect_u(test, "still usable at a sane capacity", 5, *al_u64_at(&wrapping, 0));

    al_u64_uninit(&wrapping);

    test_case_end(test);

    test_case_begin(test, "uninit is idempotent without MEMORY_NON_DANGLING_POINTER");

    AL_U64 twice = al_u64_init_1();

    al_u64_add_last(&twice, 1);

    al_u64_uninit(&twice);

    /* The freed pointer used to stay in the struct unless the build defined
     * MEMORY_NON_DANGLING_POINTER, so whether this second call was a double free
     * depended on a build flag rather than on the code. */
    al_u64_uninit(&twice);

    test_expect_u(test, "capacity 0 after a second uninit", 0, al_u64_get_capacity(&twice));
    test_expect_u(test, "size 0 after a second uninit", 0, al_u64_get_size(&twice));

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

    Test test = test_init("tests/container/arrayList/test_al_u64.c");

    test_suite_begin(&test, "al_u64");
    _test_basic(&test);
    _test_insert(&test);
    _test_delete(&test);
    _test_refusals(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}