#include <test/test.h>

#include <char/char.h>

/* Coverage for the empty-safe equality helpers char_equal_1 / char_equal_2 — the cases
 * that make them distinct from char_compare_equal_* (which abort on a zero-length input). */

static void _test_char_equal_1(Test *const test) {
    test_case_begin(test, "char_equal_1 (null-terminated, empty-safe)");

    test_expect_true(test, "equal", char_equal_1("abc", "abc"));
    test_expect_false(test, "diff same length", char_equal_1("abc", "abd"));
    test_expect_false(test, "diff length", char_equal_1("ab", "abc"));
    test_expect_true(test, "both empty", char_equal_1("", ""));
    test_expect_false(test, "left empty", char_equal_1("", "x"));
    test_expect_false(test, "right empty", char_equal_1("x", ""));

    test_case_end(test);
}

static void _test_char_equal_2(Test *const test) {
    test_case_begin(test, "char_equal_2 (sized, empty-safe)");

    test_expect_true(test, "equal", char_equal_2("abc", 3, "abc", 3));
    test_expect_false(test, "diff sizes", char_equal_2("abc", 3, "ab", 2));
    test_expect_true(test, "both size 0", char_equal_2("", 0, "", 0));
    test_expect_true(test, "size 0 ignores data", char_equal_2("abc", 0, "xyz", 0));
    test_expect_false(test, "one size 0", char_equal_2("abc", 3, "", 0));
    test_expect_true(test, "prefix compare equal", char_equal_2("abcXYZ", 3, "abcDEF", 3));
    test_expect_false(test, "prefix compare differ", char_equal_2("abX", 3, "abY", 3));

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

    Test test = test_init("tests/char/test_char_equal.c");

    test_suite_begin(&test, "char_equal");
    _test_char_equal_1(&test);
    _test_char_equal_2(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}