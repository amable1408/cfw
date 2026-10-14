#include <test/test.h>

#include <char/char.h>
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

/* One spelling and the answer a Win32 predicate must give for it. The label names rows whose
 * spelling carries non-printable bytes. */
typedef struct {
    char const  *label;
    char const  *spelling;
    bool        expected;
} WindowsSpellingCase;

/* Pure byte rules, so every row runs on every OS - no device is ever opened. */
static void _test_windows_names(Test *const test) {
    test_case_begin(test, "file_name_reserved_windows / file_path_aliased_windows");

    static WindowsSpellingCase const devices[] = {
        { "CONIN$ .txt (stem trim)", "CONIN$ .txt", true },
        { "conin$ .txt (case)", "conin$ .txt", true },
        { "AUX .html (stem trim)", "AUX .html", true },
        { "CONIN$. .txt (stem trim)", "CONIN$. .txt", true },
        { "CONIN$  .a.b (stem trim)", "CONIN$  .a.b", true },
        { "nul .txt (stem trim)", "nul .txt", true },
        { "CONOUT$ .js (stem trim)", "CONOUT$ .js", true },
        { "CON", "CON", true },
        { "con.txt", "con.txt", true },
        { "prn.", "prn.", true },
        { "COM1", "COM1", true },
        { "lpt9.x", "lpt9.x", true },
        { "COM + cp1252 0xB9", "COM\xB9", true },
        { "LPT + cp1252 0xB2 .txt", "LPT\xB2.txt", true },
        { "com + cp1252 0xB3", "com\xB3", true },
        { "COM + UTF-8 superscript 1", "COM\xC2\xB9", true },
        { "LPT + UTF-8 superscript 2 .txt", "LPT\xC2\xB2.txt", true },
        { "CON:", "CON:", true },
        { "CON:x", "CON:x", true },
        { "CON::$DATA", "CON::$DATA", true },
        { "con :x", "con :x", true },
        { "COM1 .txt (stem trim on a numbered port)", "COM1 .txt", true },
        { "con.txt.bak (stem cut at the FIRST dot)", "con.txt.bak", true },
        { "LPT + UTF-8 superscript 1, no extension", "LPT\xC2\xB9", true },
        { "x: (device false; the path rule answers true - the overlap, by design)", "x:", false },
        { "CONFIG.txt", "CONFIG.txt", false },
        { "x:CON", "x:CON", false },
        { "console.txt", "console.txt", false },
        { "COM0", "COM0", false },
        { "COM10", "COM10", false },
        { "COMX", "COMX", false },
        { "leading space CONIN$", " CONIN$", false },
        { "CONIN$ + tab", "CONIN$\t", false },
        { "NUL + 0xA0 (not COM/LPT)", "NUL\xA0", false },
        { "empty", "", false },
        { "a", "a", false }
    };

    for (USize i = 0; i < sizeof(devices) / sizeof(devices[0]); i += 1) {
        bool const actual = file_name_reserved_windows(devices[i].spelling, char_length(devices[i].spelling));

        test_expect_bool(test, devices[i].label, devices[i].expected, actual);
    }

    static WindowsSpellingCase const paths[] = {
        { "a:b (colon)", "a:b", true },
        { "x::$DATA (stream)", "x::$DATA", true },
        { "index.html:Zone.Identifier (stream)", "index.html:Zone.Identifier", true },
        { "sub./x (intermediate dot)", "sub./x", true },
        { "sub /x (intermediate space)", "sub /x", true },
        { "x. (trailing dot)", "x.", true },
        { "x  (trailing space)", "x ", true },
        { "ENV~1 (8.3)", "ENV~1", true },
        { "sub/GIT~1/config (8.3 intermediate)", "sub/GIT~1/config", true },
        { "HTACCE~1 (8.3)", "HTACCE~1", true },
        { "BACKUP~1.TXT (8.3 with extension)", "BACKUP~1.TXT", true },
        { "a\\b./c (backslash separates)", "a\\b./c", true },
        { "~1 (8.3-shaped stem)", "~1", true },
        { ". (dot segment)", ".", true },
        { ".. (dot segment)", "..", true },
        { "... (trailing dots)", "...", true },
        { "a/./b (dot segment mid-path)", "a/./b", true },
        { "//./PhysicalDrive0 (device prefix: its second component IS .)", "//./PhysicalDrive0", true },
        { "//?/C:/x (verbatim prefix)", "//?/C:/x", true },
        { "sub/.. (dot segment last)", "sub/..", true },
        { "x: (the path half of the overlap pin)", "x:", true },
        { "my-report~2.pdf (stem > 8)", "my-report~2.pdf", false },
        { "a~b (no digit)", "a~b", false },
        { "report~1.html (extension > 3)", "report~1.html", false },
        { ".well-known/x", ".well-known/x", false },
        { "normal/path.txt", "normal/path.txt", false },
        { "empty", "", false },
        { "a/b", "a/b", false },
        { "a/b/ (trailing separator)", "a/b/", false },
        { "a//b (empty component)", "a//b", false }
    };

    for (USize i = 0; i < sizeof(paths) / sizeof(paths[0]); i += 1) {
        bool const actual = file_path_aliased_windows(paths[i].spelling, char_length(paths[i].spelling));

        test_expect_bool(test, paths[i].label, paths[i].expected, actual);
    }

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
    _test_windows_names(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}