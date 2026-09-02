#include <test/test.h>

#include <container/str/str.h>
#include <container/string/string.h>
#include <file/file.h>

/* Coverage for the by-path metadata primitives file_size_1/2/3 and file_modified_1/2/3,
 * including the empty-Str/String guard in the _2/_3 converters, which answer false for an
 * empty path by choice (char_new_3 yields "" for a zero-length copy, so this is a
 * value-dependent refusal, not an abort avoided). */

static void _test_metadata(Test *const test) {
    test_case_begin(test, "file_size / file_modified via char* / Str / String");

    char path[] = "file_meta_test.tmp";
    FILE *const handle = fopen(path, "wb");

    test_expect_true(test, "temp opened", handle != nullptr);

    fwrite("hello", 1, 5, handle);
    fclose(handle);

    USize size = 0;
    I64 mtime  = 0;

    test_expect_true(test, "size_1 ok", file_size_1(path, &size));
    test_expect_u(test, "size_1 == 5", 5, size);
    test_expect_true(test, "modified_1 ok", file_modified_1(path, &mtime));
    test_expect_true(test, "modified_1 > 0", mtime > 0);

    Str const path_str = str_init_2(path);
    USize size_str     = 0;
    I64 mtime_str      = 0;

    test_expect_true(test, "size_2 ok", file_size_2(&path_str, &size_str));
    test_expect_u(test, "size_2 == 5", 5, size_str);
    test_expect_true(test, "modified_2 ok", file_modified_2(&path_str, &mtime_str));

    String const path_string = string_init_3(path);
    USize size_string        = 0;
    I64 mtime_string         = 0;

    test_expect_true(test, "size_3 ok", file_size_3(&path_string, &size_string));
    test_expect_u(test, "size_3 == 5", 5, size_string);
    test_expect_true(test, "modified_3 ok", file_modified_3(&path_string, &mtime_string));

    remove(path);

    test_case_end(test);
}

static void _test_empty_path_guard(Test *const test) {
    test_case_begin(test, "empty Str/String path returns false (no abort)");

    Str empty_str       = str_init_1();
    String empty_string = string_init_1();

    USize size = 0;
    I64 mtime  = 0;

    test_expect_false(test, "size_2 empty is false", file_size_2(&empty_str, &size));
    test_expect_false(test, "modified_2 empty is false", file_modified_2(&empty_str, &mtime));
    test_expect_false(test, "size_3 empty is false", file_size_3(&empty_string, &size));
    test_expect_false(test, "modified_3 empty is false", file_modified_3(&empty_string, &mtime));

    str_uninit(&empty_str);
    string_uninit(&empty_string);

    test_case_end(test);
}

static void _test_extension(Test *const test) {
    test_case_begin(test, "file_extension_1");

    test_expect_true(test, "simple extension", char_equal_1(file_extension_1("file.txt"), "txt"));
    test_expect_true(test, "double extension takes last", char_equal_1(file_extension_1("archive.tar.gz"), "gz"));
    test_expect_true(test, "extension within a path", char_equal_1(file_extension_1("a/b/main.c"), "c"));
    test_expect_true(test, "no extension", char_equal_1(file_extension_1("README"), ""));
    test_expect_true(test, "leading dot is not an extension", char_equal_1(file_extension_1(".gitignore"), ""));
    test_expect_true(test, "leading dot in a path component", char_equal_1(file_extension_1("dir/.hidden"), ""));
    test_expect_true(test, "dotfile with a real extension", char_equal_1(file_extension_1("dir/.config.json"), "json"));

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

    Test test = test_init("tests/file/test_file_meta.c");

    test_suite_begin(&test, "file_meta");
    _test_metadata(&test);
    _test_empty_path_guard(&test);
    _test_extension(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}