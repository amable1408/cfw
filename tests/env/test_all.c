#include <stdio.h>

#include <env/env.h>
#include <test/test.h>

/* Full coverage of the env module: set/get/unset roundtrips, unset/empty
 * collapsing, typed getters (bool + the number_i/_u/_f family), Str/String set
 * variants (including embedded-NUL rejection), invalid names, the in-memory
 * env_from_char/_str/_string loaders, and the .env file loader (dialect and
 * parser edges, precedence, duplicates, BOM/CRLF, malformed lines incl.
 * embedded NUL bytes, a final line without a newline, missing, comment-only and
 * over-FILE_READ_BYTES_MAX files, and empty ranges with no data pointer).
 * Fixtures are written into the current working directory and removed after
 * each case; the one over-cap fixture is a sparse file in the OS temp
 * directory. */

static void _env_test_fixture_write(char const *const path, char const *const content) {
    File *file = file_open_1(path, "wb");

    /* file_write_1 rejects a zero count, and a zero-byte fixture is a real
     * case: opening in "wb" already truncated the file to empty. */
    if (char_length(content) > 0) {
        file_write_1(file, (void*) content, sizeof(char), char_length(content));
    }

    file_close(&file);
}

static void _test_env_set_get(Test *const test) {
    test_case_begin(test, "env_set_1 / env_get_1 / env_get_2 / env_exists");

    test_expect_true(test, "set roundtrip ok", result_is_success(env_set_1("ENV_TEST_BASIC", "hello")));
    test_expect_string(test, "get value", "hello", env_get_1("ENV_TEST_BASIC"));
    test_expect_true(test, "exists set", env_exists("ENV_TEST_BASIC"));

    test_expect_null(test, "get missing", env_get_1("ENV_TEST_MISSING_VAR"));
    test_expect_false(test, "exists missing", env_exists("ENV_TEST_MISSING_VAR"));

    test_expect_true(test, "set empty ok", result_is_success(env_set_1("ENV_TEST_EMPTY", "")));
    test_expect_null(test, "empty collapses to unset", env_get_1("ENV_TEST_EMPTY"));
    test_expect_false(test, "exists empty", env_exists("ENV_TEST_EMPTY"));

    test_expect_string(test, "get_2 present", "hello", env_get_2("ENV_TEST_BASIC", "fallback"));
    test_expect_string(test, "get_2 missing", "fallback", env_get_2("ENV_TEST_MISSING_VAR", "fallback"));
    test_expect_string(test, "get_2 empty", "fallback", env_get_2("ENV_TEST_EMPTY", "fallback"));
    test_expect_null(test, "get_2 null fallback", env_get_2("ENV_TEST_MISSING_VAR", nullptr));

    env_unset("ENV_TEST_BASIC");

    test_case_end(test);
}

static void _test_env_get_bool(Test *const test) {
    test_case_begin(test, "env_get_bool");

    char const *const truthy[] = { "1", "true", "yes", "on", "TrUe", "YES", "On" };
    char const *const falsy[]  = { "0", "false", "no", "off", "FALSE", "No", "OFF" };

    for (USize i = 0; i < sizeof(truthy) / sizeof(truthy[0]); i += 1) {
        env_set_1("ENV_TEST_BOOL", truthy[i]);

        test_expect_true(test, "truthy value", env_get_bool("ENV_TEST_BOOL", false));
    }

    for (USize i = 0; i < sizeof(falsy) / sizeof(falsy[0]); i += 1) {
        env_set_1("ENV_TEST_BOOL", falsy[i]);

        test_expect_false(test, "falsy value", env_get_bool("ENV_TEST_BOOL", true));
    }

    env_set_1("ENV_TEST_BOOL", "banana");

    test_expect_true(test, "garbage keeps true fallback", env_get_bool("ENV_TEST_BOOL", true));
    test_expect_false(test, "garbage keeps false fallback", env_get_bool("ENV_TEST_BOOL", false));
    test_expect_true(test, "missing keeps fallback", env_get_bool("ENV_TEST_MISSING_VAR", true));

    env_unset("ENV_TEST_BOOL");

    test_case_end(test);
}

static void _test_env_get_number(Test *const test) {
    test_case_begin(test, "env_get_number_i / _u / _f");

    env_set_1("ENV_TEST_NUMBER", "42");

    test_expect_i(test, "i positive", 42, env_get_number_i("ENV_TEST_NUMBER", -1));

    env_set_1("ENV_TEST_NUMBER", "-7");

    test_expect_i(test, "i negative", -7, env_get_number_i("ENV_TEST_NUMBER", -1));

    env_set_1("ENV_TEST_NUMBER", "12x");

    test_expect_i(test, "i trailing junk keeps fallback", -1, env_get_number_i("ENV_TEST_NUMBER", -1));
    test_expect_i(test, "i missing keeps fallback", 99, env_get_number_i("ENV_TEST_MISSING_VAR", 99));

    env_set_1("ENV_TEST_NUMBER", "100");

    test_expect_u(test, "u positive", 100, env_get_number_u("ENV_TEST_NUMBER", 7));

    env_set_1("ENV_TEST_NUMBER", "-5");

    test_expect_u(test, "u negative keeps fallback", 7, env_get_number_u("ENV_TEST_NUMBER", 7));

    env_set_1("ENV_TEST_NUMBER", "3.5");

    test_expect_f(test, "f fractional", 3.5, env_get_number_f("ENV_TEST_NUMBER", -1.0), 0.0001);
    test_expect_f(test, "f missing keeps fallback", -1.0, env_get_number_f("ENV_TEST_MISSING_VAR", -1.0), 0.0001);

    env_unset("ENV_TEST_NUMBER");

    test_case_end(test);
}

static void _test_env_set_str_string(Test *const test) {
    test_case_begin(test, "env_set_2 (Str) / env_set_3 (String)");

    Str const value_str = str_init_3((char*) "strvalue", 8);

    test_expect_true(test, "set_2 ok", result_is_success(env_set_2("ENV_TEST_STR", &value_str)));
    test_expect_string(test, "set_2 value", "strvalue", env_get_1("ENV_TEST_STR"));

    String value_string = string_init_1();

    string_add_last_1(&value_string, "stringvalue");

    test_expect_true(test, "set_3 ok", result_is_success(env_set_3("ENV_TEST_STRING", &value_string)));
    test_expect_string(test, "set_3 value", "stringvalue", env_get_1("ENV_TEST_STRING"));

    string_uninit(&value_string);

    Str const empty_str = str_init_1();

    test_expect_true(test, "set_2 empty Str ok", result_is_success(env_set_2("ENV_TEST_STR_EMPTY", &empty_str)));
    test_expect_false(test, "empty Str reads unset", env_exists("ENV_TEST_STR_EMPTY"));

    /* Aliased views over literals (never uninit'd - nothing owns the bytes). */
    Str    const nul_str    = str_init_3((char*) "a\0b", 3);
    String const nul_string = string_init_4((char*) "a\0b", 3);

    test_expect_true(test, "set_2 embedded NUL rejected", result_is_error(env_set_2("ENV_TEST_STR_NUL", &nul_str)));
    test_expect_true(test, "set_3 embedded NUL rejected", result_is_error(env_set_3("ENV_TEST_STRING_NUL", &nul_string)));
    test_expect_false(test, "NUL value never applied", env_exists("ENV_TEST_STR_NUL"));

    env_unset("ENV_TEST_STR");
    env_unset("ENV_TEST_STRING");
    env_unset("ENV_TEST_STR_EMPTY");

    test_case_end(test);
}

static void _test_env_set_invalid(Test *const test) {
    test_case_begin(test, "invalid names");

    Result const equals = env_set_1("BAD=NAME", "x");

    test_expect_true(test, "name with '=' fails", result_is_error(equals));
    test_expect_true(test, "argument category", result_category(equals) == RESULT_CATEGORY_ARGUMENT);

    Result const empty = env_set_1("", "x");

    test_expect_true(test, "empty name fails", result_is_error(empty));
    test_expect_true(test, "control char in name fails", result_is_error(env_set_1("BAD\nNAME", "x")));
    test_expect_true(test, "unset invalid name fails", result_is_error(env_unset("BAD=NAME")));

    test_case_end(test);
}

static void _test_env_unset(Test *const test) {
    test_case_begin(test, "env_unset");

    env_set_1("ENV_TEST_UNSET", "here");

    test_expect_true(test, "unset ok", result_is_success(env_unset("ENV_TEST_UNSET")));
    test_expect_null(test, "gone after unset", env_get_1("ENV_TEST_UNSET"));
    test_expect_true(test, "unset missing ok", result_is_success(env_unset("ENV_TEST_UNSET")));

    test_case_end(test);
}

static void _test_env_from(Test *const test) {
    test_case_begin(test, "env_from_char / _str / _string");

    char const *const config = "ENV_TEST_FROM_A=alpha\nENV_TEST_FROM_B=\"be ta\"\n# comment\n";

    test_expect_true(test, "from_char ok", result_is_success(env_from_char_1(config, char_length(config))));
    test_expect_string(test, "from_char plain", "alpha", env_get_1("ENV_TEST_FROM_A"));
    test_expect_string(test, "from_char quoted", "be ta", env_get_1("ENV_TEST_FROM_B"));

    Str const update = str_init_2((char*) "ENV_TEST_FROM_A=changed");

    test_expect_true(test, "from_str ok", result_is_success(env_from_str_1(&update)));
    test_expect_string(test, "env wins without override", "alpha", env_get_1("ENV_TEST_FROM_A"));
    test_expect_true(test, "from_str override ok", result_is_success(env_from_str_2(&update, true)));
    test_expect_string(test, "data wins with override", "changed", env_get_1("ENV_TEST_FROM_A"));

    String config_string = string_init_1();

    string_add_last_1(&config_string, "ENV_TEST_FROM_C=gamma\nBAD LINE\n");

    Result const partial = env_from_string_1(&config_string);

    test_expect_true(test, "from_string partial", result_is_partial(partial));
    test_expect_u(test, "from_string one skip", 1, (USize) result_code(partial));
    test_expect_string(test, "from_string good line applied", "gamma", env_get_1("ENV_TEST_FROM_C"));

    string_uninit(&config_string);

    env_unset("ENV_TEST_FROM_A");
    env_unset("ENV_TEST_FROM_B");
    env_unset("ENV_TEST_FROM_C");

    test_case_end(test);
}

static void _test_env_load_basic(Test *const test) {
    test_case_begin(test, "env_load_1 dialect");

    char const *const path = "fixture_basic.env";

    _env_test_fixture_write(path,
        "# full-line comment\n"
        "\n"
        "export ENV_TEST_EXPORTED=exportvalue\n"
        "ENV_TEST_PLAIN=plain value\n"
        "ENV_TEST_LTRIM=   spaced   \n"
        "ENV_TEST_INLINE=value # comment\n"
        "ENV_TEST_HASH=val#ue\n"
        "ENV_TEST_EMPTYC= # only a comment\n"
        "ENV_TEST_DQ=\"a b\\nc \\\"q\\\" x\\\\y\"\n"
        "ENV_TEST_SQ='literal \\n $x # keep'\n"
        "ENV_TEST_SQ_TRAIL='v'   # after quote\n");

    test_expect_true(test, "load success", result_is_success(env_load_1(path)));
    test_expect_string(test, "export prefix", "exportvalue", env_get_1("ENV_TEST_EXPORTED"));
    test_expect_string(test, "plain with space", "plain value", env_get_1("ENV_TEST_PLAIN"));
    test_expect_string(test, "trimmed", "spaced", env_get_1("ENV_TEST_LTRIM"));
    test_expect_string(test, "inline comment cut", "value", env_get_1("ENV_TEST_INLINE"));
    test_expect_string(test, "hash kept without space", "val#ue", env_get_1("ENV_TEST_HASH"));
    test_expect_null(test, "comment-only value is unset", env_get_1("ENV_TEST_EMPTYC"));
    test_expect_string(test, "double-quoted escapes", "a b\nc \"q\" x\\y", env_get_1("ENV_TEST_DQ"));
    test_expect_string(test, "single-quoted literal", "literal \\n $x # keep", env_get_1("ENV_TEST_SQ"));
    test_expect_string(test, "comment after quote", "v", env_get_1("ENV_TEST_SQ_TRAIL"));

    env_unset("ENV_TEST_EXPORTED");
    env_unset("ENV_TEST_PLAIN");
    env_unset("ENV_TEST_LTRIM");
    env_unset("ENV_TEST_INLINE");
    env_unset("ENV_TEST_HASH");
    env_unset("ENV_TEST_EMPTYC");
    env_unset("ENV_TEST_DQ");
    env_unset("ENV_TEST_SQ");
    env_unset("ENV_TEST_SQ_TRAIL");

    remove(path);

    test_case_end(test);
}

static void _test_env_load_malformed(Test *const test) {
    test_case_begin(test, "env_load_1 malformed lines");

    char const *const path = "fixture_malformed.env";

    _env_test_fixture_write(path,
        "ENV_TEST_GOOD=good\n"
        "NOEQUALS\n"
        "1BAD=x\n"
        "ENV_TEST_UNTERM=\"abc\n"
        "ENV_TEST_TRAILJUNK=\"v\" junk\n"
        "ENV_TEST_ESCQ=\"abc\\\"\n"
        "ENV_TEST_BSLASH=\"x\\\n");

    Result const result = env_load_1(path);

    test_expect_true(test, "partial result", result_is_partial(result));
    test_expect_true(test, "application category", result_category(result) == RESULT_CATEGORY_APPLICATION);
    test_expect_u(test, "skipped count in code", 6, (USize) result_code(result));
    test_expect_string(test, "good line still applied", "good", env_get_1("ENV_TEST_GOOD"));
    test_expect_null(test, "unterminated not applied", env_get_1("ENV_TEST_UNTERM"));
    test_expect_null(test, "trailing junk not applied", env_get_1("ENV_TEST_TRAILJUNK"));
    test_expect_null(test, "escaped closing quote is unterminated", env_get_1("ENV_TEST_ESCQ"));
    test_expect_null(test, "trailing backslash is unterminated", env_get_1("ENV_TEST_BSLASH"));

    env_unset("ENV_TEST_GOOD");

    remove(path);

    test_case_end(test);
}

static void _test_env_load_nul_byte(Test *const test) {
    test_case_begin(test, "env_load embedded NUL byte");

    char const *const path = "fixture_nul.env";

    /* Written with an explicit byte count - char_length would stop at the NUL. */
    File *file = file_open_1(path, "wb");

    file_write_1(file, (void*) "ENV_TEST_NULA=1\nB\0AD=x\nENV_TEST_NULB=2\n", sizeof(char), CHAR_STATIC_SIZE("ENV_TEST_NULA=1\nB\0AD=x\nENV_TEST_NULB=2\n"));
    file_close(&file);

    Result const result = env_load_1(path);

    test_expect_true(test, "partial result", result_is_partial(result));
    test_expect_u(test, "one line skipped", 1, (USize) result_code(result));
    test_expect_string(test, "line before NUL applied", "1", env_get_1("ENV_TEST_NULA"));
    test_expect_string(test, "line after NUL applied", "2", env_get_1("ENV_TEST_NULB"));

    env_unset("ENV_TEST_NULA");
    env_unset("ENV_TEST_NULB");

    remove(path);

    test_case_end(test);
}

static void _test_env_load_last_line(Test *const test) {
    test_case_begin(test, "env_load final line without newline");

    char const *const path = "fixture_lastline.env";

    _env_test_fixture_write(path, "ENV_TEST_FIRSTLINE=first\nENV_TEST_LASTLINE=lastvalue");

    test_expect_true(test, "load ok", result_is_success(env_load_1(path)));
    test_expect_string(test, "first line applied", "first", env_get_1("ENV_TEST_FIRSTLINE"));
    test_expect_string(test, "unterminated last line applied", "lastvalue", env_get_1("ENV_TEST_LASTLINE"));

    env_unset("ENV_TEST_FIRSTLINE");
    env_unset("ENV_TEST_LASTLINE");

    remove(path);

    test_case_end(test);
}

static void _test_env_load_edges(Test *const test) {
    test_case_begin(test, "env_load parser edges");

    char const *const path = "fixture_edges.env";

    _env_test_fixture_write(path,
        "ENV_TEST_EQVAL=a=b\n"
        "ENV_TEST_WSKEY   =   wsvalue\n"
        "ENV_TEST_HASHIMM=#val\n"
        "export=exportself\n");

    test_expect_true(test, "load ok", result_is_success(env_load_1(path)));
    test_expect_string(test, "value keeps '='", "a=b", env_get_1("ENV_TEST_EQVAL"));
    test_expect_string(test, "whitespace around '='", "wsvalue", env_get_1("ENV_TEST_WSKEY"));
    test_expect_string(test, "hash right after '=' is literal", "#val", env_get_1("ENV_TEST_HASHIMM"));
    test_expect_string(test, "export= sets a variable named export", "exportself", env_get_1("export"));

    env_unset("ENV_TEST_EQVAL");
    env_unset("ENV_TEST_WSKEY");
    env_unset("ENV_TEST_HASHIMM");
    env_unset("export");

    remove(path);

    test_case_end(test);
}

static void _test_env_load_precedence(Test *const test) {
    test_case_begin(test, "env_load precedence and override");

    char const *const path = "fixture_precedence.env";

    _env_test_fixture_write(path, "ENV_TEST_PRESET=fromfile\n");
    env_set_1("ENV_TEST_PRESET", "original");

    test_expect_true(test, "default load ok", result_is_success(env_load_1(path)));
    test_expect_string(test, "environment wins", "original", env_get_1("ENV_TEST_PRESET"));

    test_expect_true(test, "override load ok", result_is_success(env_load_2(path, true)));
    test_expect_string(test, "file wins with override", "fromfile", env_get_1("ENV_TEST_PRESET"));

    env_unset("ENV_TEST_PRESET");

    remove(path);

    test_case_end(test);
}

static void _test_env_load_duplicate(Test *const test) {
    test_case_begin(test, "env_load duplicate keys");

    char const *const path = "fixture_duplicate.env";

    _env_test_fixture_write(path,
        "ENV_TEST_DUPL=first\n"
        "ENV_TEST_DUPL=second\n");

    test_expect_true(test, "default load ok", result_is_success(env_load_1(path)));
    test_expect_string(test, "first occurrence wins", "first", env_get_1("ENV_TEST_DUPL"));

    test_expect_true(test, "override load ok", result_is_success(env_load_2(path, true)));
    test_expect_string(test, "last wins with override", "second", env_get_1("ENV_TEST_DUPL"));

    env_unset("ENV_TEST_DUPL");

    remove(path);

    test_case_end(test);
}

static void _test_env_load_bom_crlf(Test *const test) {
    test_case_begin(test, "env_load BOM and CRLF");

    char const *const path = "fixture_bom.env";

    _env_test_fixture_write(path, "\xEF\xBB\xBF" "ENV_TEST_BOM=bomvalue\r\nENV_TEST_CRLF=crlfvalue\r\nENV_TEST_QCRLF=\"qv\"\r\n");

    test_expect_true(test, "load ok", result_is_success(env_load_1(path)));
    test_expect_string(test, "BOM stripped from first key", "bomvalue", env_get_1("ENV_TEST_BOM"));
    test_expect_string(test, "CRLF stripped", "crlfvalue", env_get_1("ENV_TEST_CRLF"));
    test_expect_string(test, "quoted value with CRLF ending", "qv", env_get_1("ENV_TEST_QCRLF"));

    env_unset("ENV_TEST_BOM");
    env_unset("ENV_TEST_CRLF");
    env_unset("ENV_TEST_QCRLF");

    remove(path);

    test_case_end(test);
}

static void _test_env_load_missing(Test *const test) {
    test_case_begin(test, "env_load missing file");

    Result const result = env_load_1("fixture_definitely_missing.env");

    test_expect_true(test, "missing file is an error", result_is_error(result));
    test_expect_false(test, "missing file is not partial", result_is_partial(result));

    test_case_end(test);
}

static void _test_env_load_directory(Test *const test) {
    test_case_begin(test, "env_load directory path");

    /* Windows: fopen refuses the directory (OS classification); POSIX: the open
     * succeeds and file_regular rejects it (IO category). Either way an error,
     * never a silent empty success. */
    Result const result = env_load_1(".");

    test_expect_true(test, "directory is an error", result_is_error(result));
    test_expect_false(test, "directory is not partial", result_is_partial(result));

    test_case_end(test);
}

static void _test_env_load_empty(Test *const test) {
    test_case_begin(test, "env_load comment-only and zero-byte files");

    char const *const path = "fixture_empty.env";

    _env_test_fixture_write(path, "\n\n# nothing but comments\n\n");

    test_expect_true(test, "comment-only file succeeds", result_is_success(env_load_1(path)));

    _env_test_fixture_write(path, "");

    test_expect_true(test, "zero-byte file succeeds", result_is_success(env_load_1(path)));

    remove(path);

    test_case_end(test);
}

static void _test_env_load_over_cap(Test *const test) {
    test_case_begin(test, "env_load refuses a file over FILE_READ_BYTES_MAX");

    /* The refusal is decided on the handle's size before a byte is read, so the
     * fixture needs only its SIZE, and the load costs one fstat, never 256 MiB
     * of I/O. ftruncate leaves a hole on POSIX; SetEndOfFile on NTFS reserves
     * the extent without writing it, and the file is removed a moment later.
     * It lives in the OS temp directory rather than the cwd - this tree is
     * synced, and an entry that size would ripple through the sync for the
     * instant it exists. Where the filesystem will not extend without writing,
     * or the temp path would not fit, the case skips visibly instead of passing
     * hollowly: a truncated path opened "wb" would empty whatever file it
     * happened to name. */
#ifdef _WIN32
    char const *const directory = env_get_2("TEMP", ".");
#else
    char const *const directory = env_get_2("TMPDIR", "/tmp");
#endif
    char const *const file_name = "/cfw_env_over_cap.env";
    char              path[1024] = DEFAULT_INITIALIZATION;

    if (char_length(directory) + char_length(file_name) >= sizeof(path)) {
        test_expect_true(test, "SKIPPED: the temp directory path is too long for the fixture", true);

        test_case_end(test);

        return;
    }

    char_format(path, sizeof(path), "%s%s", directory, file_name);

    File *file = file_open_try_1(path, "wb");

    if (memory_empty(file)) {
        test_expect_true(test, "SKIPPED: the temp directory is not writable here", true);

        test_case_end(test);

        return;
    }

#ifdef _WIN32
    LARGE_INTEGER end = DEFAULT_INITIALIZATION;

    end.QuadPart = (LONGLONG) FILE_READ_BYTES_MAX + 1;

    HANDLE const handle   = (HANDLE) _get_osfhandle(fileno(file));
    bool   const extended = SetFilePointerEx(handle, end, nullptr, FILE_BEGIN) && SetEndOfFile(handle);
#else
    bool const extended = ftruncate(fileno(file), (off_t) FILE_READ_BYTES_MAX + 1) == 0;
#endif

    file_close(&file);

    if (extended) {
        Result const result = env_load_1(path);

        test_expect_true(test, "over-cap file is an error", result_is_error(result));
        test_expect_true(test, "over-cap file is IO category", result_category(result) == RESULT_CATEGORY_IO);
        test_expect_false(test, "over-cap file is not partial", result_is_partial(result));
    }
    else {
        test_expect_true(test, "SKIPPED: this filesystem will not extend a file without writing it", true);
    }

    remove(path);

    test_case_end(test);
}

static void _test_env_from_empty(Test *const test) {
    test_case_begin(test, "env_from_* with an empty range and no pointer");

    /* str_init_1 and string_init_1 both hand their data as NULL at size 0 (a
     * view over "" would not - its pointer is real); the wrappers pass that
     * through and the char entries decide on the size before the pointer. */
    Str    const empty_str    = str_init_1();
    String       empty_string = string_init_1();

    test_expect_true(test, "from_char_1 NULL at size 0 succeeds", result_is_success(env_from_char_1(nullptr, 0)));
    test_expect_true(test, "from_char_2 NULL at size 0 succeeds", result_is_success(env_from_char_2(nullptr, 0, true)));
    test_expect_true(test, "from_str empty succeeds", result_is_success(env_from_str_1(&empty_str)));
    test_expect_true(test, "from_string empty succeeds", result_is_success(env_from_string_1(&empty_string)));

    string_uninit(&empty_string);

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

    Test test = test_init("tests/env/test_all.c");

    test_suite_begin(&test, "env");
    _test_env_set_get(&test);
    _test_env_get_bool(&test);
    _test_env_get_number(&test);
    _test_env_set_str_string(&test);
    _test_env_set_invalid(&test);
    _test_env_unset(&test);
    _test_env_from(&test);
    _test_env_from_empty(&test);
    _test_env_load_basic(&test);
    _test_env_load_malformed(&test);
    _test_env_load_nul_byte(&test);
    _test_env_load_last_line(&test);
    _test_env_load_edges(&test);
    _test_env_load_precedence(&test);
    _test_env_load_duplicate(&test);
    _test_env_load_bom_crlf(&test);
    _test_env_load_missing(&test);
    _test_env_load_directory(&test);
    _test_env_load_over_cap(&test);
    _test_env_load_empty(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}