#include <tuple/tuple.h>
#include <test/test.h>

/* tuple.h is a pure aggregator: including it and calling functions from all three underlying
 * types in one translation unit is itself the check that the aggregate include compiles clean
 * with no name collisions between t_bool_usize, t_pvoid_usize, and t_usize_pvoid. */

static void _test_bool_usize(Test *const test) {
    test_case_begin(test, "T_Bool_USize init variants");

    T_Bool_USize const defaulted = t_bool_usize_init_1();

    test_expect_false(test, "init_1 _0 defaults to false", defaulted._0);
    test_expect_u(test, "init_1 _1 defaults to 0", 0, defaulted._1);

    T_Bool_USize const only_bool = t_bool_usize_init_2(true);

    test_expect_true(test, "init_2 sets _0", only_bool._0);
    test_expect_u(test, "init_2 leaves _1 at 0", 0, only_bool._1);

    T_Bool_USize const only_usize = t_bool_usize_init_3(42);

    test_expect_false(test, "init_3 leaves _0 at false", only_usize._0);
    test_expect_u(test, "init_3 sets _1", 42, only_usize._1);

    T_Bool_USize const both = t_bool_usize_init_4(true, 99);

    test_expect_true(test, "init_4 sets _0", both._0);
    test_expect_u(test, "init_4 sets _1", 99, both._1);

    test_case_end(test);
}

static void _test_pvoid_usize(Test *const test) {
    test_case_begin(test, "T_PVoid_USize init variants");

    USize marker = 7;
    void *const sentinel = &marker;

    T_PVoid_USize const defaulted = t_pvoid_usize_init_1();

    test_expect_null(test, "init_1 _0 defaults to nullptr", defaulted._0);
    test_expect_u(test, "init_1 _1 defaults to 0", 0, defaulted._1);

    T_PVoid_USize const only_ptr = t_pvoid_usize_init_2(sentinel);

    test_expect_true(test, "init_2 sets _0", only_ptr._0 == sentinel);
    test_expect_u(test, "init_2 leaves _1 at 0", 0, only_ptr._1);

    T_PVoid_USize const only_usize = t_pvoid_usize_init_3(42);

    test_expect_null(test, "init_3 leaves _0 at nullptr", only_usize._0);
    test_expect_u(test, "init_3 sets _1", 42, only_usize._1);

    T_PVoid_USize const both = t_pvoid_usize_init_4(sentinel, 99);

    test_expect_true(test, "init_4 sets _0", both._0 == sentinel);
    test_expect_u(test, "init_4 sets _1", 99, both._1);

    test_case_end(test);
}

static void _test_usize_pvoid(Test *const test) {
    test_case_begin(test, "T_USize_PVoid init variants");

    USize marker = 7;
    void *const sentinel = &marker;

    T_USize_PVoid const defaulted = t_usize_pvoid_init_1();

    test_expect_u(test, "init_1 _0 defaults to 0", 0, defaulted._0);
    test_expect_null(test, "init_1 _1 defaults to nullptr", defaulted._1);

    T_USize_PVoid const only_usize = t_usize_pvoid_init_2(42);

    test_expect_u(test, "init_2 sets _0", 42, only_usize._0);
    test_expect_null(test, "init_2 leaves _1 at nullptr", only_usize._1);

    T_USize_PVoid const only_ptr = t_usize_pvoid_init_3(sentinel);

    test_expect_u(test, "init_3 leaves _0 at 0", 0, only_ptr._0);
    test_expect_true(test, "init_3 sets _1", only_ptr._1 == sentinel);

    T_USize_PVoid const both = t_usize_pvoid_init_4(99, sentinel);

    test_expect_u(test, "init_4 sets _0", 99, both._0);
    test_expect_true(test, "init_4 sets _1", both._1 == sentinel);

    test_case_end(test);
}

int main(void) {
    log_init((LogConfig){ .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true });

    Test test = test_init("./test_all.c");

    test_verbose_set(&test, false);

    test_suite_begin(&test, "tuple");
    _test_bool_usize(&test);
    _test_pvoid_usize(&test);
    _test_usize_pvoid(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}