#include <test/test.h>

#include <char/char.h>

/* Coverage for char_glob_match_1/2 ('*' / '?' wildcards, empty-safe) and
 * char_from_bytes_human_1 (byte count -> human-readable size). */

static void _test_glob(Test *const test) {
    test_case_begin(test, "char_glob_match '*' and '?'");

    test_expect_true(test, "literal match", char_glob_match_1("abc", "abc"));
    test_expect_false(test, "literal mismatch", char_glob_match_1("abc", "abd"));
    test_expect_true(test, "? matches one char", char_glob_match_1("a?c", "abc"));
    test_expect_false(test, "? requires a char", char_glob_match_1("a?c", "ac"));
    test_expect_true(test, "* trailing", char_glob_match_1("*.c", "main.c"));
    test_expect_false(test, "* trailing miss", char_glob_match_1("*.c", "main.h"));
    test_expect_true(test, "* in the middle", char_glob_match_1("a*z", "abcz"));
    test_expect_true(test, "* matches an empty run", char_glob_match_1("a*z", "az"));
    test_expect_true(test, "* matches anything", char_glob_match_1("*", "anything"));
    test_expect_true(test, "* matches empty text", char_glob_match_1("*", ""));
    test_expect_true(test, "empty pattern, empty text", char_glob_match_1("", ""));
    test_expect_false(test, "empty pattern, non-empty text", char_glob_match_1("", "x"));
    test_expect_true(test, "multiple stars", char_glob_match_1("*a*", "banana"));
    test_expect_false(test, "no match", char_glob_match_1("*.tar.gz", "file.zip"));

    char const *const name = "readme.md";
    test_expect_true(test, "sized *.md", char_glob_match_2("*.md", 4, name, char_length(name)));

    test_case_end(test);
}

static void _test_glob_regression(Test *const test) {
    test_case_begin(test, "char_glob_match_2 branch-order regression (star-in-text)");

    /* FIXED DEFECT: the star branch must be checked before the literal/'?'
     * branch. With the equality branch first, a '*' in the TEXT made the
     * pattern's '*' match it as a plain literal byte instead of taking the
     * wildcard branch, so the star's backtrack state (has_star/star_index)
     * was never armed and a later mismatch failed outright. */
    test_expect_true(test, "defect case: '*b' matches '*ab'", char_glob_match_1("*b", "*ab"));
    test_expect_true(test, "defect family: 'a*b' matches 'a*xb'", char_glob_match_1("a*b", "a*xb"));
    test_expect_true(test, "'?' matches a literal '*' char", char_glob_match_1("?", "*"));
    test_expect_true(test, "'*' matches a literal '*' char", char_glob_match_1("*", "*"));

    /* Unchanged-behavior anchors: no star in the pattern, or star already at
     * the front, so the branch order never mattered for these. */
    test_expect_false(test, "anchor: 'ab' vs '*b' (no wildcard, mismatch)", char_glob_match_1("ab", "*b"));
    test_expect_false(test, "anchor: '*.txt' vs 'file.txt.bak' (suffix miss)", char_glob_match_1("*.txt", "file.txt.bak"));

    test_case_end(test);
}

static void _test_from_bytes_human(Test *const test) {
    test_case_begin(test, "char_from_bytes_human formatting");

    char buffer[32] = DEFAULT_INITIALIZATION;

    char_from_bytes_human_1(buffer, sizeof(buffer), 0);
    test_expect_true(test, "0 -> 0 B", char_equal_1(buffer, "0 B"));

    char_from_bytes_human_1(buffer, sizeof(buffer), 512);
    test_expect_true(test, "512 -> 512 B", char_equal_1(buffer, "512 B"));

    char_from_bytes_human_1(buffer, sizeof(buffer), 1024);
    test_expect_true(test, "1024 -> 1.0 K", char_equal_1(buffer, "1.0 K"));

    char_from_bytes_human_1(buffer, sizeof(buffer), 1536);
    test_expect_true(test, "1536 -> 1.5 K", char_equal_1(buffer, "1.5 K"));

    char_from_bytes_human_1(buffer, sizeof(buffer), 1048576);
    test_expect_true(test, "1 MiB -> 1.0 M", char_equal_1(buffer, "1.0 M"));

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

    Test test = test_init("tests/char/test_char_glob.c");

    test_suite_begin(&test, "char_glob");
    _test_glob(&test);
    _test_glob_regression(&test);
    _test_from_bytes_human(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}