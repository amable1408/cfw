#include <test/test.h>

#include <char/char.h>
#include <container/string/string.h>

/* Coverage for char_wrap_1/2 and string_wrap: greedy reflow, fits-on-one-line,
 * over-long word on its own line, whitespace collapse, empty input, and the owning
 * String variant. Uses char_equal_1 for exact-match assertions. */

static void _test_char_wrap(Test *const test) {
    test_case_begin(test, "char_wrap_1 greedy reflow");

    char *const wrapped = char_wrap_1("the quick brown fox", 9);
    test_expect_true(test, "wraps at width 9", char_equal_1(wrapped, "the quick\nbrown fox"));
    char_delete(wrapped);

    char *const fits = char_wrap_1("hi there", 80);
    test_expect_true(test, "no wrap when it fits", char_equal_1(fits, "hi there"));
    char_delete(fits);

    char *const long_word = char_wrap_1("supercalifragilistic ok", 5);
    test_expect_true(test, "over-long word gets its own line", char_equal_1(long_word, "supercalifragilistic\nok"));
    char_delete(long_word);

    char *const collapsed = char_wrap_1("a   b", 80);
    test_expect_true(test, "whitespace runs collapse", char_equal_1(collapsed, "a b"));
    char_delete(collapsed);

    char *const empty = char_wrap_1("", 10);
    test_expect_true(test, "empty stays empty", char_equal_1(empty, ""));
    char_delete(empty);

    test_case_end(test);
}

static void _test_char_wrap_2(Test *const test) {
    test_case_begin(test, "char_wrap_2 sized input");

    char const *const text = "alpha beta gamma delta";
    char *const wrapped    = char_wrap_2(text, char_length(text), 11);

    test_expect_true(test, "sized wrap at width 11", char_equal_1(wrapped, "alpha beta\ngamma delta"));

    char_delete(wrapped);

    test_case_end(test);
}

static void _test_string_wrap(Test *const test) {
    test_case_begin(test, "string_wrap owned result");

    String source = string_init_1();
    string_add_last_1(&source, "one two three");

    String wrapped = string_wrap(&source, 7);

    test_expect_true(test, "wrapped text", char_equal_1(string_get_data(&wrapped), "one two\nthree"));
    test_expect_u(test, "wrapped size", 13, string_get_size(&wrapped));

    string_uninit(&wrapped);
    string_uninit(&source);

    String empty = string_init_1();
    String empty_wrapped = string_wrap(&empty, 10);

    test_expect_true(test, "empty String wraps to empty", string_empty(&empty_wrapped));

    string_uninit(&empty_wrapped);
    string_uninit(&empty);

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

    Test test = test_init("tests/char/test_char_wrap.c");

    test_suite_begin(&test, "char_wrap");
    _test_char_wrap(&test);
    _test_char_wrap_2(&test);
    _test_string_wrap(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}