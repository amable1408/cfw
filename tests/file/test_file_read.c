#include <file/file.h>
#include <test/test.h>

/* Reading a file that is missing, or present but empty, must REPORT rather
 * than end the process.
 *
 * Before these guards existed, `file_read_to_string` on a missing path died
 * inside file_open_1's error_check_null, and on a zero-byte file died inside
 * the String it was being read into. That made the universal caller idiom -
 *
 *     String document = file_read_to_string(path);
 *     if (string_get_size(&document) == 0) { handle the failure }
 *
 * - unreachable for exactly the case it was written to catch. A tree sweep
 * found twelve callers written against the assumed contract rather than the
 * real one, so a missing content file presented as a crash with no message
 * instead of a named error.
 *
 * The first version of this test checked ONE reader against ONE of the two
 * failure cases, and the fix it was written for left four of the six readers
 * still aborting - the str and char readers reached str_init_3 and file_read_1
 * with a zero size and died there. So the cases below are a matrix, driven off
 * a table that names every reader: a reader added to the header and not to
 * that table is the only way to escape it, and a reader that regresses fails
 * here rather than in a caller months later. */

/*==============================================================================
 * MARK: - Typedefs
 *============================================================================*/
/*
 * Reads file_name with one of the by-path readers, releases whatever that
 * reader handed over, and reports how many bytes came back.
 *
 * The readers return three different types with three different ownership
 * rules, which is why each needs its own adapter; reducing them all to a byte
 * count is what lets one table cover the whole API surface.
 */
typedef USize (*FpReadProbe)(char const *const file_name);

typedef struct {
    char const *name;   /**< Reader name, so a failure says which one broke. */
    FpReadProbe probe;  /**< Adapter around that reader. */
} Read_Probe;

/*==============================================================================
 * MARK: - Static/Internal Functions
 *============================================================================*/
static USize _probe_char(char const *const file_name) {
    char *const data = file_read_to_char(file_name);

    // Never null, for any input: the contract is an owned buffer the caller
    // frees. A null here would crash char_length below, which is the point.
    USize const size = char_length(data);

    memory_free(data);

    return size;
}

static USize _probe_char_wait(char const *const file_name) {
    char *const data = file_read_to_char_wait(file_name, 1);

    USize const size = char_length(data);

    memory_free(data);

    return size;
}

static USize _probe_str(char const *const file_name) {
    Str data = file_read_to_str(file_name);

    USize const size = str_get_size(&data);

    str_uninit(&data);

    return size;
}

static USize _probe_str_wait(char const *const file_name) {
    Str data = file_read_to_str_wait(file_name, 1);

    USize const size = str_get_size(&data);

    str_uninit(&data);

    return size;
}

static USize _probe_string(char const *const file_name) {
    String data = file_read_to_string(file_name);

    USize const size = string_get_size(&data);

    string_uninit(&data);

    return size;
}

static USize _probe_string_wait(char const *const file_name) {
    String data = file_read_to_string_wait(file_name, 1);

    USize const size = string_get_size(&data);

    string_uninit(&data);

    return size;
}

static bool _write_fixture(char const *const path, char const *const content) {
    FILE *const handle = fopen(path, "wb");

    if (memory_empty((void*) handle)) {
        return false;
    }

    if (content[0] != '\0') {
        fputs(content, handle);
    }

    fclose(handle);

    return true;
}

/*
 * Run every by-path reader against one fixture.
 *
 * Reaching the end of the loop at all is half the assertion: a reader that
 * aborts takes the process with it, so a regression shows up as a suite that
 * stops mid-run rather than as a failed expectation.
 */
static void _probe_every_reader(Test *const test, char const *const label, char const *const path, USize const expected) {
    Read_Probe const probes[] = {
        { "file_read_to_char",        _probe_char        },
        { "file_read_to_char_wait",   _probe_char_wait   },
        { "file_read_to_str",         _probe_str         },
        { "file_read_to_str_wait",    _probe_str_wait    },
        { "file_read_to_string",      _probe_string      },
        { "file_read_to_string_wait", _probe_string_wait }
    };

    for (USize index = 0; index < sizeof(probes) / sizeof(probes[0]); index += 1) {
        char message[192] = DEFAULT_INITIALIZATION;

        snprintf(message, sizeof(message), "%s %s", probes[index].name, label);

        test_expect_u(test, message, expected, probes[index].probe(path));
    }
}

/*==============================================================================
 * MARK: - Test Cases
 *============================================================================*/
static void _test_read_missing_reports(Test *const test) {
    test_case_begin(test, "file: every reader reports a missing path instead of aborting");

    char const *const path = "file_read_absent.txt";

    remove(path);

    test_expect_false(test, "the fixture really is absent", file_exists_1(path));

    _probe_every_reader(test, "reads a missing path as empty", path, 0);

    test_case_end(test);
}

static void _test_read_empty_reports(Test *const test) {
    test_case_begin(test, "file: every reader reports a zero-byte file instead of aborting");

    char const *const path = "file_read_empty.txt";

    test_expect_true(test, "the empty fixture is written", _write_fixture(path, ""));
    test_expect_true(test, "and it exists", file_exists_1(path));

    USize size = 0;

    test_expect_true(test, "with a real size of zero", file_size_1(path, &size) && size == 0);

    _probe_every_reader(test, "reads an empty file as empty", path, 0);

    remove(path);

    test_case_end(test);
}

static void _test_read_directory_reports(Test *const test) {
    test_case_begin(test, "file: every reader reports a directory instead of ending the process");

    // Windows refuses a directory in fopen, so this looked safe from here. On
    // glibc fopen SUCCEEDS on one: file_get_size then seeks to its end and
    // ftell reports LONG_MAX, so the reader asked the allocator for 2^63 bytes
    // and died - a config path pointing at a folder was a silent process death
    // on the Linux deployments and a clean empty read on the dev machine.
    _probe_every_reader(test, "reads a directory as empty", ".", 0);

    String aligned = file_align_read_to_string(".", 32);

    test_expect_u(test, "and the aligned reader agrees", 0, string_get_size(&aligned));

    string_uninit(&aligned);

    test_case_end(test);
}

static void _test_read_all_from_handle(Test *const test) {
    test_case_begin(test, "file: the by-handle readers and the regular-file guard");

    char const *const path = "file_read_all.txt";
    char const *const content = "handle-read-bytes";

    test_expect_true(test, "the fixture is written", _write_fixture(path, content));

    File *file = file_open_try_1(path, "rb");

    test_expect_true(test, "the fixture opens", !memory_empty(file));
    test_expect_true(test, "a real file is regular", file_regular(file));

    String document = file_read_all_3(file);

    test_expect_u(test, "read_all_3 reads the whole file", char_length(content), string_get_size(&document));
    test_expect_true(test, "and the bytes match", char_equal_1(string_get_data(&document), content));

    string_uninit(&document);
    file_close(&file);

    // Fresh handles per reader: file_get_size does not preserve the position on
    // every platform, so the by-handle contract is a freshly opened handle.
    file = file_open_try_1(path, "rb");

    char *const raw = file_read_all_1(file);

    test_expect_u(test, "read_all_1 reads the whole file", char_length(content), char_length(raw));

    memory_free(raw);
    file_close(&file);

    file = file_open_try_1(path, "rb");

    Str str_document = file_read_all_2(file);

    test_expect_u(test, "read_all_2 reads the whole file", char_length(content), str_get_size(&str_document));

    str_uninit(&str_document);
    file_close(&file);

    remove(path);

#ifdef __linux__
    // glibc opens directories; the guard is what tells a caller the truth.
    File *directory = file_open_try_1(".", "rb");

    test_expect_true(test, "a directory opens on glibc", !memory_empty(directory));
    test_expect_false(test, "and is not a regular file", file_regular(directory));

    String empty_document = file_read_all_3(directory);

    test_expect_u(test, "so the by-handle reader returns empty", 0, string_get_size(&empty_document));

    string_uninit(&empty_document);
    file_close(&directory);
#endif

    test_case_end(test);
}

static void _test_read_all_2_adopts_ownership(Test *const test) {
    test_case_begin(test, "file: file_read_all_2 and file_read_to_str ADOPT their buffer instead of viewing it (the Block F leak fix)");

    char const *const path = "file_read_own.txt";
    char const *const content = "adopted-not-viewed";

    test_expect_true(test, "the fixture is written", _write_fixture(path, content));

    File *file = file_open_try_1(path, "rb");

    test_expect_true(test, "the fixture opens", !memory_empty((void*) file));

    Str handle_document = file_read_all_2(file);

    // owned == true is the whole fix: before it, str_init_3 built a VIEW over a
    // String that then went out of scope still owning the block, so every call
    // stranded the buffer. This is the flag a suite CAN prove without a
    // memory-hooks/ASan lane watching the allocator underneath.
    test_expect_true(test, "file_read_all_2 owns its buffer", handle_document.owned);
    test_expect_u(test, "and its size matches the file", char_length(content), str_get_size(&handle_document));
    test_expect_true(test, "and its content matches byte for byte", char_equal_1(str_get_data(&handle_document), content));

    str_uninit(&handle_document);

    test_expect_false(test, "after uninit the ownership flag is cleared", handle_document.owned);
    test_expect_null(test, "after uninit the data pointer is cleared", (void*) handle_document.data);
    test_expect_u(test, "after uninit the size is cleared", 0, str_get_size(&handle_document));

    // A second uninit on the same Str must be a no-op rather than a double free:
    // the flag is already false, so str_uninit's own guard is what makes this safe.
    str_uninit(&handle_document);

    test_expect_false(test, "a second uninit is safe (still not owned)", handle_document.owned);

    file_close(&file);

    Str path_document = file_read_to_str(path);

    test_expect_true(test, "file_read_to_str owns its buffer too", path_document.owned);
    test_expect_u(test, "and its size matches the file", char_length(content), str_get_size(&path_document));
    test_expect_true(test, "and its content matches byte for byte", char_equal_1(str_get_data(&path_document), content));

    str_uninit(&path_document);
    str_uninit(&path_document);

    test_expect_false(test, "the by-path reader is safe under a second uninit too", path_document.owned);

    remove(path);

    test_case_end(test);
}

static void _test_read_all_2_empty_is_clean(Test *const test) {
    test_case_begin(test, "file: file_read_all_2 / file_read_to_str on a 0-byte file report empty and tolerate a double uninit");

    char const *const path = "file_read_own_empty.txt";

    test_expect_true(test, "the empty fixture is written", _write_fixture(path, ""));

    File *file = file_open_try_1(path, "rb");

    test_expect_true(test, "the empty fixture opens", !memory_empty((void*) file));

    Str handle_document = file_read_all_2(file);

    test_expect_u(test, "an empty handle read is size 0", 0, str_get_size(&handle_document));

    str_uninit(&handle_document);
    str_uninit(&handle_document);

    test_expect_u(test, "still size 0 after a double uninit", 0, str_get_size(&handle_document));

    file_close(&file);

    Str path_document = file_read_to_str(path);

    test_expect_u(test, "an empty by-path read is size 0", 0, str_get_size(&path_document));

    str_uninit(&path_document);
    str_uninit(&path_document);

    test_expect_u(test, "still size 0 after a double uninit", 0, str_get_size(&path_document));

    remove(path);

    test_case_end(test);
}

static void _test_read_content_still_works(Test *const test) {
    test_case_begin(test, "file: a real file still reads back byte for byte");

    char const *const path = "file_read_content.txt";
    char const *const content = "{\"key\":\"valor\"}";

    test_expect_true(test, "the fixture is written", _write_fixture(path, content));

    // The guards must not have cost the ordinary path anything, in any reader.
    _probe_every_reader(test, "reads the whole file", path, char_length(content));

    String document = file_read_to_string(path);

    test_expect_true(test, "the bytes match", char_equal_1(string_get_data(&document), content));

    // Not decoration: callers hand string_get_data to strlen-based APIs, and
    // the buffer used to be exactly as long as the content - so char_equal_1
    // on the line above read past the end and passed only by allocator luck.
    test_expect_true(test, "and the buffer is NUL-terminated at its size", string_get_data(&document)[string_get_size(&document)] == '\0');

    string_uninit(&document);
    remove(path);

    test_case_end(test);
}

static void _test_align_read_pads(Test *const test) {
    test_case_begin(test, "file: the aligned reader leaves a whole spare block past the content");

    char const *const path = "file_read_align.txt";

    // A SIMD scanner walks ceil(bytes / 32) chunks and its last load reads a
    // full vector past the final byte, so the sizes that matter are the ones
    // that end exactly on a 32-byte boundary - there the round-up alone buys
    // nothing and only the extra block keeps the read in bounds.
    USize const sizes[] = { 1, 31, 32, 33, 63, 64, 65 };

    for (USize index = 0; index < sizeof(sizes) / sizeof(sizes[0]); index += 1) {
        char content[128] = DEFAULT_INITIALIZATION;

        for (USize byte = 0; byte < sizes[index]; byte += 1) {
            content[byte] = 'x';
        }

        test_expect_true(test, "the sized fixture is written", _write_fixture(path, content));

        String document = file_align_read_to_string(path, 32);

        char message[192] = DEFAULT_INITIALIZATION;

        snprintf(message, sizeof(message), "a %llu-byte file still reports %llu bytes", (unsigned long long) sizes[index], (unsigned long long) sizes[index]);

        test_expect_u(test, message, sizes[index], string_get_size(&document));

        snprintf(message, sizeof(message), "and carries a spare 32-byte block after %llu bytes", (unsigned long long) sizes[index]);

        test_expect_true(test, message, string_get_capacity(&document) >= string_get_size(&document) + 32);

        // The slack is zeroed, so a load past the content cannot see a stale
        // delimiter left behind by whatever held the memory before.
        bool slack_clear = true;

        for (USize byte = string_get_size(&document); byte < string_get_capacity(&document); byte += 1) {
            if (string_get_data(&document)[byte] != '\0') {
                slack_clear = false;
            }
        }

        test_expect_true(test, "and the slack is zeroed", slack_clear);

        string_uninit(&document);
        remove(path);
    }

    remove(path);

    String missing = file_align_read_to_string("file_read_align_absent.txt", 32);

    test_expect_u(test, "a missing path aligns to empty rather than aborting", 0, string_get_size(&missing));

    // The aligned reader was the ONE reader not collapsed onto the shared
    // helpers, and it was the one that still aborted - on the empty case only,
    // which is exactly the half this matrix did not cover for it. A padded
    // buffer, not a bare empty String: a caller scanning an empty document
    // still issues one vector load, and it has to land somewhere valid.
    test_expect_true(test, "and still carries a padded buffer to load from", string_get_capacity(&missing) >= 32);

    string_uninit(&missing);

    test_expect_true(test, "the empty fixture is written", _write_fixture(path, ""));

    String blank = file_align_read_to_string(path, 32);

    test_expect_u(test, "a zero-byte file aligns to empty rather than aborting", 0, string_get_size(&blank));
    test_expect_true(test, "and it too carries a padded buffer", string_get_capacity(&blank) >= 32);

    bool blank_clear = true;

    for (USize byte = 0; byte < string_get_capacity(&blank); byte += 1) {
        if (string_get_data(&blank)[byte] != '\0') {
            blank_clear = false;
        }
    }

    test_expect_true(test, "whose every byte is zeroed", blank_clear);

    string_uninit(&blank);
    remove(path);

    test_case_end(test);
}

static void _test_open_try_reports(Test *const test) {
    test_case_begin(test, "file: file_open_try_1 returns null where file_open_1 would abort");

    char const *const path = "file_read_absent.txt";

    remove(path);

    File *missing = file_open_try_1(path, "rb");

    // The dead `if (file == nullptr)` after file_open_1 in three request
    // handlers is what this function exists to make live: a range request for
    // a file that is not there arrives from unauthenticated clients.
    test_expect_true(test, "a missing path opens as null", memory_empty((void*) missing));

    test_expect_true(test, "the fixture is written", _write_fixture(path, "x"));

    missing = file_open_try_1(path, "rb");

    test_expect_true(test, "and a real path still opens", !memory_empty((void*) missing));

    if (!memory_empty((void*) missing)) {
        file_close(&missing);
    }

    remove(path);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Entry Point
 *============================================================================*/
int main(void) {
    LogConfig const log_config = {
        .level             = LOG_LEVEL_ERROR,
        .stream            = LOG_STREAM_STDOUT,
        .timestamp_enabled = false,
        .autoflush         = true
    };

    log_init(log_config);

    Test test = test_init("tests/file/test_file_read.c");

    test_suite_begin(&test, "file read");

    _test_read_missing_reports(&test);
    _test_read_empty_reports(&test);
    _test_read_directory_reports(&test);
    _test_read_all_from_handle(&test);
    _test_read_all_2_adopts_ownership(&test);
    _test_read_all_2_empty_is_clean(&test);
    _test_read_content_still_works(&test);
    _test_align_read_pads(&test);
    _test_open_try_reports(&test);

    test_suite_end(&test);

    return test_uninit(&test);
}