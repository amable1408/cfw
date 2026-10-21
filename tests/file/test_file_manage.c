#ifdef __linux__
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <container/str/str.h>
#include <container/string/string.h>
#include <file/file.h>
#include <test/test.h>

/* Coverage for the by-path management primitives added for the porto/purgo project:
 * file_copy_*, file_replace_*, file_remove_*, file_rename_*, file_set_modified_*. */

/*==============================================================================
 * MARK: - Macros and Constants
 *============================================================================*/

// Size of the 128 KiB source the mid-copy failure case writes, far past the size limit below.
#define _TEST_BIG_SOURCE_SIZE (128 * 1024)
// Bytes the directory-read probe asks for: any non-zero count provokes the read error.
#define _TEST_DIRECTORY_READ_SIZE 16
// The 8 KiB RLIMIT_FSIZE cap the mid-copy failure case writes under.
#define _TEST_FILE_SIZE_LIMIT (8 * 1024)
// Room for "<victim>\0<tail>" in the embedded-NUL case: both halves plus the NUL, with slack.
#define _TEST_NUL_PATH_SIZE 64
// A distinctive mtime the embedded-NUL case pins on its victim, so a stray set_modified shows.
#define _TEST_NUL_VICTIM_MTIME 1000000000

#ifdef _WIN32
#define _TEST_VERBATIM_CAPACITY 1024
// Length of the "\\?\" prefix, and the bytes a spelling adds to the cwd and name: that prefix,
// the separator before the name, and the terminator.
#define _TEST_VERBATIM_EXTRA_SIZE (_TEST_VERBATIM_PREFIX_SIZE + 1 + CHAR_END_CHARACTER)
#define _TEST_VERBATIM_PREFIX_SIZE 4
#endif // _WIN32

/*==============================================================================
 * MARK: - Static/Internal Functions
 *============================================================================*/

static void _write_file(char const *const path, char const *const content) {
    FILE *const handle = fopen(path, "wb");

    // Guarded like the trash, porto and purgo suites' copies: a fixture that cannot be
    // created should fail its case, not segfault the whole runner inside fwrite.
    if (!memory_empty((void*) handle)) {
        fwrite(content, 1, char_length(content), handle);
        fclose(handle);
    }
}

/* Pin one refused opener call: nullptr, with the documented EINVAL read before anything else can
 * move errno - and, should a regression hand back a handle, that handle closed, so the rows after
 * it and the teardown still see the disk. errno is cleared on the way out, so each row's EINVAL
 * is its own call's and never one a row before it left behind (the caller clears it for the
 * first). */
static void _expect_open_refused(Test *const test, char const *const label, File *file) {
    int const error = errno;

    test_expect_null(test, label, file);
    test_expect_i(test, "and errno is EINVAL, a bad name", EINVAL, error);

    if (!memory_empty((void*) file)) {
        file_close(&file);
    }

    errno = 0;
}

#ifdef _WIN32
/* The "\\?\" absolute spelling of a cwd-relative ASCII name - the one spelling Win32 does not
 * trim - so the fixture can create "keep." beside "keep" and inspect each on its own. '/'
 * becomes '\': a verbatim path takes no forward slashes. */
static bool _verbatim_wide(char const *const name, WCHAR *const out) {
    out[0] = L'\\';
    out[1] = L'\\';
    out[2] = L'?';
    out[3] = L'\\';

    DWORD const cwd_size  = GetCurrentDirectoryW(_TEST_VERBATIM_CAPACITY - _TEST_VERBATIM_PREFIX_SIZE, out + _TEST_VERBATIM_PREFIX_SIZE);
    USize const name_size = char_length(name);

    if (cwd_size == 0 || (USize) cwd_size + name_size + _TEST_VERBATIM_EXTRA_SIZE > _TEST_VERBATIM_CAPACITY) {
        out[0] = L'\0';

        return false;
    }

    USize const base = _TEST_VERBATIM_PREFIX_SIZE + (USize) cwd_size;

    out[base] = L'\\';

    for (USize i = 0; i < name_size; i += 1) {
        out[base + 1 + i] = name[i] == '/' ? L'\\' : (WCHAR) (U8) name[i];
    }

    out[base + 1 + name_size] = L'\0';

    return true;
}

// The same verbatim spelling, narrow, for handing to the API under test itself.
static bool _verbatim_narrow(char const *const name, char *const out) {
    out[0] = '\\';
    out[1] = '\\';
    out[2] = '?';
    out[3] = '\\';

    DWORD const cwd_size  = GetCurrentDirectoryA(_TEST_VERBATIM_CAPACITY - _TEST_VERBATIM_PREFIX_SIZE, out + _TEST_VERBATIM_PREFIX_SIZE);
    USize const name_size = char_length(name);

    if (cwd_size == 0 || (USize) cwd_size + name_size + _TEST_VERBATIM_EXTRA_SIZE > _TEST_VERBATIM_CAPACITY) {
        out[0] = '\0';

        return false;
    }

    USize const base = _TEST_VERBATIM_PREFIX_SIZE + (USize) cwd_size;

    out[base] = '\\';

    for (USize i = 0; i < name_size; i += 1) {
        out[base + 1 + i] = name[i] == '/' ? '\\' : name[i];
    }

    out[base + 1 + name_size] = '\0';

    return true;
}

static bool _verbatim_create_file(char const *const name, char const *const content) {
    WCHAR wide[_TEST_VERBATIM_CAPACITY] = DEFAULT_INITIALIZATION;

    if (!_verbatim_wide(name, wide)) {
        return false;
    }

    HANDLE const handle = CreateFileW(wide, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD const size = (DWORD) char_length(content);
    DWORD written    = 0;
    bool const wrote = WriteFile(handle, content, size, &written, nullptr) != 0 && written == size;

    CloseHandle(handle);

    return wrote;
}

static bool _verbatim_create_directory(char const *const name) {
    WCHAR wide[_TEST_VERBATIM_CAPACITY] = DEFAULT_INITIALIZATION;

    return _verbatim_wide(name, wide) && (CreateDirectoryW(wide, nullptr) != 0 || GetLastError() == ERROR_ALREADY_EXISTS);
}

static bool _verbatim_exists(char const *const name) {
    WCHAR wide[_TEST_VERBATIM_CAPACITY] = DEFAULT_INITIALIZATION;

    return _verbatim_wide(name, wide) && GetFileAttributesW(wide) != INVALID_FILE_ATTRIBUTES;
}

// The byte size of the named file, or USIZE_MAX when it cannot be read.
static USize _verbatim_size(char const *const name) {
    WCHAR wide[_TEST_VERBATIM_CAPACITY]  = DEFAULT_INITIALIZATION;
    WIN32_FILE_ATTRIBUTE_DATA attributes = DEFAULT_INITIALIZATION;

    if (!_verbatim_wide(name, wide) || GetFileAttributesExW(wide, GetFileExInfoStandard, &attributes) == 0) {
        return USIZE_MAX;
    }

    return (USize) attributes.nFileSizeLow;
}

static void _verbatim_remove(char const *const name) {
    WCHAR wide[_TEST_VERBATIM_CAPACITY] = DEFAULT_INITIALIZATION;

    if (!_verbatim_wide(name, wide)) {
        return;
    }

    DWORD const attributes = GetFileAttributesW(wide);

    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return;
    }

    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        RemoveDirectoryW(wide);
    }
    else {
        DeleteFileW(wide);
    }
}

// Children before parents, so a removal never meets a populated directory.
static void _trimmed_fixture_cleanup(void) {
    char const *const names[] = {
        "trim_keep", "trim_keep.", "trim_keep ", "trim_source", "trim_moved", "trim_new", "trim_new.",
        "trim_sub/inner", "trim_sub./inner", "trim_sub", "trim_sub.", "trim_dot_segment.tmp"
    };

    for (USize i = 0; i < sizeof(names) / sizeof(names[0]); i += 1) {
        _verbatim_remove(names[i]);
    }
}
#endif // _WIN32

/*==============================================================================
 * MARK: - Test Cases
 *============================================================================*/

static void _test_copy(Test *const test) {
    test_case_begin(test, "file_copy_1: success, refuses existing destination, missing source");

    char const *const source = "file_copy_src.tmp";
    char const *const destination = "file_copy_dst.tmp";

    remove(source);
    remove(destination);

    _write_file(source, "hello copy");

    Result const copied = file_copy_1(source, destination);

    test_expect_true(test, "copy succeeds", result_is_success(copied));

    char *const destination_content = file_read_to_char(destination);

    test_expect_string(test, "destination content matches", "hello copy", destination_content);
    memory_free(destination_content);

    Result const overwrite = file_copy_1(source, destination);

    test_expect_true(test, "refuses existing destination", result_is_error(overwrite));
    test_expect_true(test, "existing-destination category is STATE", result_category(overwrite) == RESULT_CATEGORY_STATE);

    char *const kept = file_read_to_char(destination);

    test_expect_string(test, "AND THE REFUSAL PRESERVED THE DESTINATION'S CONTENT", "hello copy", kept);
    memory_free(kept);

    /* A destination under a missing DIRECTORY: not-found on both platforms - and on Windows
     * specifically ERROR_PATH_NOT_FOUND (3), the Win32 code the CRT keeps in _doserrno, where
     * filing errno (ENOENT, 2) into the Win32-shaped classifier reported the file code. */
    Result const no_directory = file_copy_1(source, "no_such_dir_for_copy/never.tmp");

    test_expect_true(test, "a destination under a missing directory is not-found", file_result_is_not_found(no_directory));
#ifdef _WIN32
    test_expect_true(test, "and carries ERROR_PATH_NOT_FOUND, the Win32 code, not errno's ENOENT", result_code(no_directory) == (U16) ERROR_PATH_NOT_FOUND);
#endif

    Result const missing = file_copy_1("file_copy_does_not_exist.tmp", "file_copy_never_created.tmp");

    test_expect_true(test, "missing source is an error", result_is_error(missing));
    test_expect_false(test, "missing source creates nothing", file_exists_1("file_copy_never_created.tmp"));

    remove(source);
    remove(destination);

    test_case_end(test);
}

static void _test_remove(Test *const test) {
    test_case_begin(test, "file_remove_1: success, missing path, refuses a directory");

    char const *const path = "file_remove_test.tmp";

    _write_file(path, "gone soon");

    test_expect_true(test, "remove succeeds", result_is_success(file_remove_1(path)));
    test_expect_false(test, "file is actually gone", file_exists_1(path));

    Result const missing = file_remove_1("file_remove_does_not_exist.tmp");

    test_expect_true(test, "removing a missing path is an error", result_is_error(missing));

    // dir_create_1 isn't linked into this test binary's SRCS; a directory guard here would
    // need it, so it is covered instead by the fact that unlink/DeleteFileA natively refuse
    // directories - documented in file.h rather than re-proven with a second fixture module.

    test_case_end(test);
}

static void _test_rename(Test *const test) {
    test_case_begin(test, "file_rename_1: move, always replaces an existing destination");

    char const *const a = "file_rename_a.tmp";
    char const *const b = "file_rename_b.tmp";

    remove(a);
    remove(b);

    _write_file(a, "content A");

    test_expect_true(test, "rename to a new path succeeds", result_is_success(file_rename_1(a, b)));
    test_expect_false(test, "source is gone after rename", file_exists_1(a));
    test_expect_true(test, "destination exists after rename", file_exists_1(b));

    _write_file(a, "content A again");

    test_expect_true(test, "rename REPLACES an existing destination", result_is_success(file_rename_1(a, b)));

    char *const b_content = file_read_to_char(b);

    test_expect_string(test, "destination now holds the source's content", "content A again", b_content);
    memory_free(b_content);

    Result const missing = file_rename_1("file_rename_does_not_exist.tmp", "file_rename_unreached.tmp");

    test_expect_true(test, "renaming a missing source is an error", result_is_error(missing));

    remove(b);

    test_case_end(test);
}

static void _test_replace(Test *const test) {
    test_case_begin(test, "file_replace_1: atomic, creates when missing, leaves destination untouched on failure");

    char const *const source = "file_replace_src.tmp";
    char const *const destination = "file_replace_dst.tmp";

    remove(source);
    remove(destination);

    _write_file(source, "version one");

    test_expect_true(test, "replace creates a missing destination", result_is_success(file_replace_1(source, destination)));

    char *const first_content = file_read_to_char(destination);

    test_expect_string(test, "destination holds version one", "version one", first_content);
    memory_free(first_content);

    _write_file(source, "version two, longer than before");

    test_expect_true(test, "replace overwrites an existing destination", result_is_success(file_replace_1(source, destination)));

    char *const second_content = file_read_to_char(destination);

    test_expect_string(test, "destination holds version two", "version two, longer than before", second_content);
    memory_free(second_content);

    // Source still holds its own content: file_replace_1 is a copy-and-replace, not a move.
    char *const source_content = file_read_to_char(source);

    test_expect_string(test, "source is untouched by replace", "version two, longer than before", source_content);
    memory_free(source_content);

    remove(source);

    Result const missing_source = file_replace_1("file_replace_does_not_exist.tmp", destination);

    test_expect_true(test, "replace with a missing source is an error", result_is_error(missing_source));

    char *const untouched_content = file_read_to_char(destination);

    test_expect_string(test, "destination is UNTOUCHED after a failed replace", "version two, longer than before", untouched_content);
    memory_free(untouched_content);

    remove(destination);

    test_case_end(test);
}

static void _test_set_modified(Test *const test) {
    test_case_begin(test, "file_set_modified_1 pairs with file_modified_1's getter");

    char const *const path = "file_set_modified_test.tmp";

    remove(path);
    _write_file(path, "time travel");

    // An arbitrary past instant, safely below "now" on any machine's clock: 2024-01-01T00:00:00Z.
    I64 const target = 1704067200;

    /* result_is_success, not a bool test. file_set_modified_* joined the Result family this
     * round, and Result is a U32 enum whose SUCCESS is 0 - so the old truthiness test would
     * now read a successful call as a failure, and its three empty-path siblings below would
     * read a refusal as success. Exactly the shape that passes for the wrong reason. */
    test_expect_true(test, "set_modified succeeds", result_is_success(file_set_modified_1(path, target)));

    I64 read_back = 0;

    test_expect_true(test, "modified_1 reads it back", file_modified_1(path, &read_back));
    test_expect_i(test, "round-trips exactly", target, read_back);

    remove(path);

    test_case_end(test);
}

static void _test_result_predicates(Test *const test) {
    test_case_begin(test, "the Result predicates answer ONE question each, on both platforms");

    char const *const source = "predicate_src.tmp";
    char const *const taken  = "predicate_taken.tmp";

    remove(source);
    remove(taken);
    _write_file(source, "content");
    _write_file(taken, "already here");

    Result const exists = file_copy_1(source, taken);

    test_expect_true(test, "a refused copy answers is_exists", file_result_is_exists(exists));

    /* THE ASSERTION THAT WOULD HAVE CAUGHT THE BUG. On Windows EEXIST and
     * ERROR_NOT_SAME_DEVICE are BOTH 17, so a predicate written as a bare code comparison
     * answers true to both questions - and the natural `if (is_exists) ... else if
     * (is_cross_device)` takes the wrong branch for every cross-device rename. Only the
     * category separates them. */
    test_expect_false(test, "and is NOT cross-device", file_result_is_cross_device(exists));
    test_expect_false(test, "and is not not-found", file_result_is_not_found(exists));

    Result const missing = file_copy_1("predicate_no_such_file.tmp", "predicate_unreached.tmp");

    test_expect_true(test, "a missing source answers is_not_found", file_result_is_not_found(missing));
    test_expect_false(test, "and is not exists", file_result_is_exists(missing));

    // A missing DIRECTORY COMPONENT is a different OS code on Windows (3, not 2) and must
    // still answer the same question, or the predicate means different things per platform.
    Result const missing_parent = file_rename_1(source, "predicate_no_such_dir/inside.tmp");

    test_expect_true(test, "a missing destination directory is also not-found", file_result_is_not_found(missing_parent));

    // Success is never any of them.
    test_expect_false(test, "success is not exists", file_result_is_exists(RESULT_SUCCESS));
    test_expect_false(test, "success is not cross-device", file_result_is_cross_device(RESULT_SUCCESS));
    test_expect_false(test, "success is not not-found", file_result_is_not_found(RESULT_SUCCESS));

    /* Mid 37: a STATE is never an OS not-found answer, even when its code equals one - no
     * module produces a STATE not-found (this one's STATE is its EEXIST refusal; trash's STATE
     * codes are refusals about the trash). Spelled with the platform's own not-found codes so
     * the pin fails if the STATE guard is dropped. */
#ifdef _WIN32
    U32 const not_found_code = (U32) ERROR_FILE_NOT_FOUND;
    U32 const path_not_found = (U32) ERROR_PATH_NOT_FOUND;
#else
    U32 const not_found_code = (U32) ENOENT;
    U32 const path_not_found = (U32) ENOENT;
#endif

    test_expect_false(test, "a STATE carrying the not-found code is NOT not-found", file_result_is_not_found(result_make(RESULT_CATEGORY_STATE, not_found_code, 0)));
    test_expect_false(test, "a STATE carrying the path-not-found code is NOT not-found", file_result_is_not_found(result_make(RESULT_CATEGORY_STATE, path_not_found, 0)));
    test_expect_true(test, "while the same code as an OS answer IS not-found", file_result_is_not_found(result_from_os_code(not_found_code)));

    remove(source);
    remove(taken);

    test_case_end(test);
}

static void _test_file_error(Test *const test) {
    test_case_begin(test, "file_error tells a clean end-of-file from a broken read");

    char const *const path = "file_error_probe.tmp";

    remove(path);
    _write_file(path, "some bytes");

    File *handle = file_open_try_1(path, "rb");

    test_expect_true(test, "the fixture opens", !memory_empty((void*) handle));

    char *const contents = file_read_all_1(handle);

    memory_free(contents);

    // Reading to the end is not an error, and this is the half that keeps the check honest:
    // a predicate that answered true here would make every successful copy report a failure.
    test_expect_false(test, "a clean read to EOF is NOT an error", file_error(handle));

    file_close(&handle);

#ifdef __linux__
    /* glibc's fopen SUCCEEDS on a directory and the first read then fails with EISDIR - the
     * one portable way to provoke a real read error without special privileges. This is also
     * the case that used to make file_copy_1 report an empty "successful" copy of a folder. */
    File *directory = file_open_try_1(".", "rb");

    test_expect_true(test, "the directory probe opened (glibc's fopen accepts a directory)", !memory_empty((void*) directory));

    if (!memory_empty((void*) directory)) {
        char buffer[_TEST_DIRECTORY_READ_SIZE] = DEFAULT_INITIALIZATION;

        file_read_1(directory, buffer, 1, sizeof(buffer));

        test_expect_true(test, "a FAILED read is reported as an error", file_error(directory));

        file_close(&directory);
    }
#endif

    remove(path);

    test_case_end(test);
}

static void _test_failure_cleanup(Test *const test) {
    test_case_begin(test, "a failed copy leaves NO partial destination behind (a REAL mid-copy failure), and a directory source is refused before anything is created");

    char const *const destination = "cleanup_partial.tmp";

    remove(destination);

    /* A directory source is refused BEFORE the destination exists (R4 Mid 18: file_regular on
     * the open handle), so this exercises the argument refusal and not the cleanup - the
     * earlier version of this case claimed to reach the mid-copy path and passed for the
     * wrong reason. */
    Result const directory = file_copy_1(".", destination);

    test_expect_true(test, "copying a directory fails", result_is_error(directory));
    test_expect_false(test, "and creates nothing", file_exists_1(destination));
#ifdef __linux__
    test_expect_true(test, "the refusal is ARGUMENT / FILE_ARGUMENT_NOT_REGULAR (glibc's fopen opens a directory; file_regular refuses it)",
        result_category(directory) == RESULT_CATEGORY_ARGUMENT && result_code(directory) == (U16) FILE_ARGUMENT_NOT_REGULAR);

    /* REGRESSION PIN (memsec HIGH, 2026-09-02): FileArgument used to be numbered 1/2/3, and
     * FILE_ARGUMENT_NOT_REGULAR (2) collided with ENOENT/ERROR_FILE_NOT_FOUND - this module's
     * OWN refusal read back as "the path is not there". Neither OS-code predicate may ever
     * answer true for this family's own ARGUMENT refusals. */
    test_expect_false(test, "a NOT_REGULAR refusal is NOT reported as not-found", file_result_is_not_found(directory));
    test_expect_false(test, "a NOT_REGULAR refusal is NOT reported as an existing destination", file_result_is_exists(directory));

    /* THE MID-COPY FAILURE, provoked for real: a 128 KiB source under an 8 KiB RLIMIT_FSIZE
     * makes the writer's fwrite short with EFBIG after the destination has been CREATED, so
     * the cleanup at the end of _file_copy_exclusive is the only thing between this case and
     * a partial left behind. SIGXFSZ is ignored so the limit reports instead of killing. */
    char const *const source   = "cleanup_big_source.tmp";
    USize const big_size       = _TEST_BIG_SOURCE_SIZE;
    char *const big            = (char*) memory_alloc(big_size + CHAR_END_CHARACTER);

    char_fill(big, big_size, 'x');

    remove(source);
    _write_file(source, big);
    memory_free(big);

    struct rlimit saved = DEFAULT_INITIALIZATION;

    test_expect_true(test, "the file-size limit could be read", getrlimit(RLIMIT_FSIZE, &saved) == 0);

    struct rlimit small = saved;

    small.rlim_cur = _TEST_FILE_SIZE_LIMIT;

    void (*const previous)(int) = signal(SIGXFSZ, SIG_IGN);

    test_expect_true(test, "the file-size limit could be lowered", setrlimit(RLIMIT_FSIZE, &small) == 0);

    Result const failed = file_copy_1(source, destination);

    setrlimit(RLIMIT_FSIZE, &saved);
    signal(SIGXFSZ, previous);

    test_expect_true(test, "the copy failed mid-stream (EFBIG)", result_is_error(failed));
    test_expect_false(test, "AND THE PARTIAL DESTINATION IS GONE", file_exists_1(destination));

    remove(source);
    remove(destination);
#else
    // Windows fopen refuses a directory outright (an OS open failure, not the ARGUMENT code),
    // and RLIMIT_FSIZE does not exist, so the mid-copy cleanup is honestly untestable here.
    test_expect_true(test, "the mid-copy cleanup is not testable on this platform", true);
#endif

    test_case_end(test);
}

#ifdef __linux__

/* R4 Mid 20's carry: the permission carry (the temp takes the destination's mode before the
 * rename) and the 0600-on-create promise had no assertion at all. */
static void _test_replace_permissions(Test *const test) {
    test_case_begin(test, "file_replace_1 carries the destination's mode (0644) onto the replacement, and a destination it CREATES is 0600");

    char const *const source      = "perm_source.tmp";
    char const *const destination = "perm_destination.tmp";
    char const *const created     = "perm_created.tmp";
    struct stat info              = DEFAULT_INITIALIZATION;

    remove(source);
    remove(destination);
    remove(created);
    _write_file(source, "new content");
    _write_file(destination, "old content");

    struct stat probe = DEFAULT_INITIALIZATION;

    /* DrvFs under WSL (a 9p mount without metadata) ACCEPTS chmod and ignores it, so the
     * assertions below cannot hold there - and it maps a write-less mode to the read-only
     * attribute, which then blocks the rename. A test that fails on a filesystem that is
     * behaving as documented is worse than none: skip, and say so. Run the suite from an
     * ext4 directory (/tmp) to exercise it. */
    if (chmod(destination, 0644) != 0 || stat(destination, &probe) != 0 || (probe.st_mode & 07777) != 0644) {
        test_expect_true(test, "SKIPPED: this filesystem does not honour mode bits", true);

        remove(source);
        remove(destination);
        remove(created);
        test_case_end(test);

        return;
    }

    test_expect_true(test, "replace succeeds", result_is_success(file_replace_1(source, destination)));
    test_expect_true(test, "the replacement can be stat'ed", stat(destination, &info) == 0);
    /* 0644, not 0600: the temp is CREATED 0600, so a destination at 0600 could not tell a
     * carried mode from an un-carried one - the injection that skipped the carry stayed
     * green until this was widened. */
    test_expect_true(test, "THE DESTINATION'S 0644 WAS CARRIED onto the replacement (the temp itself is created 0600)", (info.st_mode & 07777) == 0644);

    mode_t const previous = umask(0);

    test_expect_true(test, "replace into a fresh path succeeds", result_is_success(file_replace_1(source, created)));

    umask(previous);

    test_expect_true(test, "the created file can be stat'ed", stat(created, &info) == 0);
    test_expect_true(test, "A CREATED DESTINATION IS 0600 even under umask 0", (info.st_mode & 07777) == 0600);

    remove(source);
    remove(destination);
    remove(created);

    test_case_end(test);
}

static void _test_dangling_symlink_destination(Test *const test) {
    test_case_begin(test, "a DANGLING SYMLINK destination is refused, and its target is never created");

    char const *const link   = "dangling_dest.tmp";
    char const *const target = "dangling_target_must_not_appear.tmp";
    char const *const source = "dangling_source.tmp";

    remove(link);
    remove(target);
    remove(source);
    _write_file(source, "payload");

    if (symlink(target, link) != 0) {
        test_expect_true(test, "the platform could create a symlink", false);
        test_case_end(test);

        return;
    }

    Result const refused = file_copy_1(source, link);

    /* An existence test built on stat reports a dangling symlink as ABSENT, after which a
     * plain create follows the link and writes whatever it points at. The exclusive create
     * cannot follow one, so this is a refusal instead. */
    test_expect_true(test, "the copy is refused", result_is_error(refused));
    test_expect_true(test, "and reported as an existing destination", file_result_is_exists(refused));

    // THE POINT OF THE FIX: the link's target was never brought into being.
    test_expect_false(test, "THE LINK TARGET WAS NOT CREATED", file_exists_1(target));

    remove(link);
    remove(source);

    test_case_end(test);
}

#endif

/*
 * file_result_is_cross_device answering TRUE, which nothing asserted.
 *
 * The predicate exists for one answer and the suite only ever checked the other, so a change
 * making it return false unconditionally passed everything while silently breaking porto's
 * cross-filesystem move - the exact code path it was written for.
 */
static void _test_cross_device_true(Test *const test) {
    test_case_begin(test, "file_result_is_cross_device answers TRUE for a real cross-filesystem rename");

#ifdef __linux__
    /* /tmp is ext4 on the WSL bench while the repository is a DrvFs mount, so a rename
     * between them is genuinely EXDEV. Where they happen to be one filesystem the case skips
     * VISIBLY rather than passing hollowly - a skipped assertion someone can read beats a
     * green one that checked nothing. */
    char const *const source      = "cross_device_source.tmp";
    char const *const destination = "/tmp/cfw_cross_device_target.tmp";

    remove(source);
    remove(destination);
    _write_file(source, "payload");

    Result const renamed = file_rename_1(source, destination);

    if (result_is_success(renamed)) {
        test_expect_true(test, "SKIPPED: the two paths share a filesystem here", true);
        remove(destination);
    }
    else {
        test_expect_true(test, "THE PREDICATE ANSWERS TRUE", file_result_is_cross_device(renamed));
        // And it stays distinct from its neighbour, which shares Windows code 17.
        test_expect_false(test, "and it is not reported as an existing destination", file_result_is_exists(renamed));
        test_expect_true(test, "the source survives a failed rename", file_exists_1(source));
    }

    remove(source);
#else
    // Two drive letters would be needed, and a second one is not guaranteed on this machine.
    test_expect_true(test, "SKIPPED: needs two filesystems", true);
#endif

    test_case_end(test);
}

/*
 * A path this build cannot ADDRESS is refused, not acted on.
 *
 * Windows-only because the defect is: the by-path family goes through the ANSI entry points,
 * which read the caller's UTF-8 bytes in the process code page. For a name outside that code
 * page the two readings name DIFFERENT files, and when a mojibake twin exists the call
 * succeeded against the twin - purgo reported "deleted cafe.txt" while cafe.txt survived and
 * the other file was destroyed. Linux has no such split: paths there are bytes.
 */
static void _test_unaddressable_path_refused(Test *const test) {
    test_case_begin(test, "a path outside the process code page is REFUSED, never mis-targeted");

#ifdef _WIN32
    /* SKIPPED when the process code page IS UTF-8 (Windows 10+ can be set that way, and an
     * app manifest can request it). There the A-APIs genuinely address UTF-8, the predicate
     * correctly ACCEPTS the path below, and asserting a refusal would be asserting that a
     * correct system is broken - the kind of failing test that gets "fixed" by weakening the
     * guard it was written to protect. */
    if (GetACP() == CP_UTF8) {
        test_expect_true(test, "SKIPPED: this machine's code page is already UTF-8", true);
        test_case_end(test);

        return;
    }

    // "cafe" with U+00E9, in UTF-8: 63 61 66 C3 A9. Read as cp1252 this is a different name.
    char const *const utf8_name  = "caf\xC3\xA9_unaddressable.tmp";
    char const *const plain_name = "addressable_ascii.tmp";

    remove(plain_name);
    _write_file(plain_name, "payload");

    /* Every mutating by-path entry point refuses it. Asserted per function rather than once,
     * because the guard has to be on each of them - one missing guard is the whole defect. */
    test_expect_true(test, "file_copy_1 refuses it", result_category(file_copy_1(plain_name, utf8_name)) == RESULT_CATEGORY_ARGUMENT);
    test_expect_true(test, "file_rename_1 refuses it", result_category(file_rename_1(plain_name, utf8_name)) == RESULT_CATEGORY_ARGUMENT);
    test_expect_true(test, "file_remove_1 refuses it", result_category(file_remove_1(utf8_name)) == RESULT_CATEGORY_ARGUMENT);
    test_expect_true(test, "file_replace_1 refuses it", result_category(file_replace_1(plain_name, utf8_name)) == RESULT_CATEGORY_ARGUMENT);
    test_expect_true(test, "file_set_modified_1 refuses it", result_category(file_set_modified_1(utf8_name, 0)) == RESULT_CATEGORY_ARGUMENT);

    /* THE SOURCE HALF, which the assertions above never touched: an unaddressable source is
     * refused too, and the predicate names the refusal without a category comparison. */
    test_expect_true(test, "file_copy_1 refuses an unaddressable SOURCE, and file_result_is_unaddressable says so",
        file_result_is_unaddressable(file_copy_1(utf8_name, "addressable_copy.tmp")));
    test_expect_true(test, "file_rename_1 refuses an unaddressable SOURCE", file_result_is_unaddressable(file_rename_1(utf8_name, "addressable_copy.tmp")));
    test_expect_true(test, "the destination-side refusal carries the same code", file_result_is_unaddressable(file_remove_1(utf8_name)));
    test_expect_false(test, "an empty-path refusal is NOT the unaddressable one", file_result_is_unaddressable(result_make(RESULT_CATEGORY_ARGUMENT, FILE_ARGUMENT_EMPTY_PATH, 0)));

    // THE POINT: the refusal is not collateral damage. An ASCII path still works.
    test_expect_true(test, "AND AN ADDRESSABLE PATH STILL WORKS", result_is_success(file_copy_1(plain_name, "addressable_copy.tmp")));
    test_expect_true(test, "the source was not disturbed", file_exists_1(plain_name));

    remove("addressable_copy.tmp");
    remove(plain_name);
#else
    // Linux addresses every byte string it is given; there is nothing to refuse.
    test_expect_true(test, "not applicable on this platform", true);
#endif

    test_case_end(test);
}

/*
 * A component ending in '.' or ' ' is REFUSED, never trimmed onto its twin.
 *
 * Windows-only because the defect is: Win32 path normalisation strips a trailing '.' or ' '
 * from every component of a non-"\\?\" path, so file_remove_1("report.") deleted "report" -
 * purgo --purge 'report.' destroyed a file nobody named. Each twin pair is created through
 * "\\?\" (CreateFileW), which does not trim, and every assertion checks BOTH twins.
 */
static void _test_trimmed_name_refused(Test *const test) {
    test_case_begin(test, "a component ending in '.' or ' ' is REFUSED, never trimmed onto its twin");

#ifdef _WIN32
    _trimmed_fixture_cleanup();

    bool const fixture = _verbatim_create_file("trim_keep", "plain")
        && _verbatim_create_file("trim_keep.", "dotted")
        && _verbatim_create_file("trim_keep ", "spaced")
        && _verbatim_create_file("trim_source", "source!")
        && _verbatim_create_directory("trim_sub")
        && _verbatim_create_directory("trim_sub.")
        && _verbatim_create_file("trim_sub/inner", "plain")
        && _verbatim_create_file("trim_sub./inner", "dotted");

    test_expect_true(test, "fixture: every twin pair exists side by side", fixture);

    if (!fixture) {
        _trimmed_fixture_cleanup();
        test_case_end(test);

        return;
    }

    // Removal: the final component, its trailing-space twin, and a MIDDLE component.
    test_expect_true(test, "file_remove_1(\"trim_keep.\") is refused as unaddressable", file_result_is_unaddressable(file_remove_1("trim_keep.")));
    test_expect_true(test, "and trim_keep survives it", _verbatim_exists("trim_keep"));
    test_expect_true(test, "and trim_keep. survives it", _verbatim_exists("trim_keep."));
    test_expect_true(test, "file_remove_1(\"trim_keep \") is refused as unaddressable", file_result_is_unaddressable(file_remove_1("trim_keep ")));
    test_expect_true(test, "and trim_keep survives the trailing space too", _verbatim_exists("trim_keep"));
    test_expect_true(test, "and \"trim_keep \" survives it", _verbatim_exists("trim_keep "));
    test_expect_true(test, "file_remove_1(\"trim_sub./inner\") - a middle component - is refused", file_result_is_unaddressable(file_remove_1("trim_sub./inner")));
    test_expect_true(test, "and trim_sub/inner survives it", _verbatim_exists("trim_sub/inner"));
    test_expect_true(test, "and trim_sub./inner survives it", _verbatim_exists("trim_sub./inner"));

    // Rename: a trimmed SOURCE would move the twin; a trimmed DESTINATION would replace it.
    test_expect_true(test, "file_rename_1 refuses a trimmed source", file_result_is_unaddressable(file_rename_1("trim_keep.", "trim_moved")));
    test_expect_true(test, "and trim_keep was not moved", _verbatim_exists("trim_keep") && !_verbatim_exists("trim_moved"));
    test_expect_true(test, "file_rename_1 refuses a trimmed destination", file_result_is_unaddressable(file_rename_1("trim_source", "trim_keep.")));
    test_expect_true(test, "and the source stayed put", _verbatim_exists("trim_source"));
    test_expect_true(test, "and trim_keep was not replaced (still 5 bytes)", _verbatim_size("trim_keep") == 5);

    // Copy, replace and set_modified: the other by-path mutators inherit the same refusal.
    test_expect_true(test, "file_copy_1 refuses a trimmed destination", file_result_is_unaddressable(file_copy_1("trim_source", "trim_new.")));
    test_expect_false(test, "and trim_new was not created", _verbatim_exists("trim_new"));
    test_expect_true(test, "file_replace_1 refuses a trimmed destination", file_result_is_unaddressable(file_replace_1("trim_source", "trim_keep.")));
    test_expect_true(test, "and trim_keep was not replaced by it (still 5 bytes)", _verbatim_size("trim_keep") == 5);
    test_expect_true(test, "and trim_keep. was not replaced either (still 6 bytes)", _verbatim_size("trim_keep.") == 6);
    test_expect_true(test, "file_set_modified_1 refuses a trimmed path", file_result_is_unaddressable(file_set_modified_1("trim_keep.", 0)));

    // THE POINT: the rule is not collateral damage. "." segments and "\\?\" paths still work.
    test_expect_true(test, "fixture: a plain file for the dot-segment check", _verbatim_create_file("trim_dot_segment.tmp", "x"));
    test_expect_true(test, "a \".\" component is not a trimmed name", result_is_success(file_remove_1("./trim_dot_segment.tmp")));

    char verbatim[_TEST_VERBATIM_CAPACITY] = DEFAULT_INITIALIZATION;

    test_expect_true(test, "fixture: the narrow \\\\?\\ spelling of trim_keep.", _verbatim_narrow("trim_keep.", verbatim));
    test_expect_true(test, "a \\\\?\\ path is exempt: it removes exactly trim_keep.", result_is_success(file_remove_1(verbatim)));
    test_expect_false(test, "trim_keep. is gone", _verbatim_exists("trim_keep."));
    test_expect_true(test, "and trim_keep is untouched", _verbatim_exists("trim_keep") && _verbatim_size("trim_keep") == 5);

    _trimmed_fixture_cleanup();
#else
    // Linux trims nothing: "report." is a name of its own there, and nothing is refused.
    test_expect_true(test, "not applicable on this platform", true);
#endif

    test_case_end(test);
}

static void _test_empty_path_guard(Test *const test) {
    test_case_begin(test, "empty Str/String path returns an error (no abort)");

    Str empty_str       = str_init_1();
    String empty_string = string_init_1();

    test_expect_true(test, "copy_2 empty is an error", result_is_error(file_copy_2(&empty_str, &empty_str)));
    test_expect_true(test, "copy_3 empty is an error", result_is_error(file_copy_3(&empty_string, &empty_string)));
    test_expect_true(test, "remove_2 empty is an error", result_is_error(file_remove_2(&empty_str)));
    test_expect_true(test, "remove_3 empty is an error", result_is_error(file_remove_3(&empty_string)));
    test_expect_true(test, "rename_2 empty is an error", result_is_error(file_rename_2(&empty_str, &empty_str)));
    test_expect_true(test, "rename_3 empty is an error", result_is_error(file_rename_3(&empty_string, &empty_string)));
    test_expect_true(test, "replace_2 empty is an error", result_is_error(file_replace_2(&empty_str, &empty_str)));
    test_expect_true(test, "replace_3 empty is an error", result_is_error(file_replace_3(&empty_string, &empty_string)));
    test_expect_true(test, "set_modified_2 empty is ARGUMENT / FILE_ARGUMENT_EMPTY_PATH", result_code(file_set_modified_2(&empty_str, 0)) == (U16) FILE_ARGUMENT_EMPTY_PATH);
    test_expect_true(test, "set_modified_3 empty is ARGUMENT / FILE_ARGUMENT_EMPTY_PATH", result_code(file_set_modified_3(&empty_string, 0)) == (U16) FILE_ARGUMENT_EMPTY_PATH);
    test_expect_true(test, "copy_2 empty carries the same code", result_code(file_copy_2(&empty_str, &empty_str)) == (U16) FILE_ARGUMENT_EMPTY_PATH);
    test_expect_false(test, "and it is not the unaddressable refusal", file_result_is_unaddressable(file_copy_2(&empty_str, &empty_str)));

    /* REGRESSION PIN (memsec HIGH, 2026-09-02): FILE_ARGUMENT_EMPTY_PATH (formerly 3) collided
     * with ERROR_PATH_NOT_FOUND. */
    test_expect_false(test, "an EMPTY_PATH refusal is NOT reported as not-found", file_result_is_not_found(file_copy_2(&empty_str, &empty_str)));
    test_expect_false(test, "an EMPTY_PATH refusal is NOT reported as an existing destination", file_result_is_exists(file_copy_2(&empty_str, &empty_str)));

    /* memsec LOW, 2026-09-02: file_exists_2/_3 used to hand an EMPTY Str/String's nullptr
     * buffer straight to char_new_3, which aborts on it - the abort primitive the rest of
     * this family already refuses instead of. */
    test_expect_false(test, "exists_2 on an empty Str is false, not an abort", file_exists_2(&empty_str));
    test_expect_false(test, "exists_3 on an empty String is false, not an abort", file_exists_3(&empty_string));

    /* memsec LOW, 2026-09-02: file_open_try_2/_3 and file_open_wait_2/_3 are documented
     * (file.h) to report failure by nullptr, never to abort - the same shape as
     * file_exists_2/_3, and the same missing guard. */
    test_expect_null(test, "open_try_2 on an empty Str is nullptr, not an abort", file_open_try_2(&empty_str, "rb"));
    test_expect_null(test, "open_try_3 on an empty String is nullptr, not an abort", file_open_try_3(&empty_string, "rb"));
    test_expect_null(test, "open_wait_2 on an empty Str is nullptr, not an abort", file_open_wait_2(&empty_str, "rb", 1));
    test_expect_null(test, "open_wait_3 on an empty String is nullptr, not an abort", file_open_wait_3(&empty_string, "rb", 1));

    str_uninit(&empty_str);
    string_uninit(&empty_string);

    test_case_end(test);
}

/*
 * memsec MED (2026-10-18): every _2/_3 wrapper copied its sized path whole with char_new_3 and
 * handed the copy to the _1 form, which reads only up to the first NUL - so "<victim>\0x"
 * removed, renamed, overwrote or re-dated "<victim>", a file the caller never named, and
 * reported SUCCESS. The fixture is that prefix, so the defect is its disappearance or change.
 */
static void _test_embedded_nul_refused(Test *const test) {
    test_case_begin(test, "a Str / String path holding a NUL byte is refused, never cut down to its prefix");

    char const *const victim = "file_nul_victim.tmp";
    char const *const source = "file_nul_source.tmp";
    char const *const other  = "file_nul_other.tmp";
    char const *const fresh  = "file_nul_fresh.tmp";

    remove(victim);
    remove(source);
    remove(other);
    remove(fresh);
    _write_file(victim, "victim");
    _write_file(source, "source content");

    bool const dated = result_is_success(file_set_modified_1(victim, _TEST_NUL_VICTIM_MTIME));

    test_expect_true(test, "fixture: the victim exists", file_exists_1(victim));
    test_expect_true(test, "fixture: the victim carries the pinned mtime", dated);

    USize const victim_size         = char_length(victim);
    char const tail[]               = "x";
    char named[_TEST_NUL_PATH_SIZE] = DEFAULT_INITIALIZATION;

    // "<victim>\0x": the zero-initialised buffer supplies the NUL between the two halves.
    memory_copy_1(named, victim, victim_size);
    memory_copy_1(named + victim_size + 1, tail, sizeof(tail) - 1);

    // Owned deep copies (str_init_static copies its whole size), so the NUL travels with them.
    USize const named_size = victim_size + 1 + sizeof(tail) - 1;
    Str view               = str_init_static(named, named_size);
    String buffer          = string_init_static(named, named_size);
    Str source_view        = str_init_static(source, char_length(source));
    String source_buffer   = string_init_static(source, char_length(source));
    Str other_view         = str_init_static(other, char_length(other));
    String other_buffer    = string_init_static(other, char_length(other));

    // The queries: without the guard each answers about the victim, which is there.
    USize size = 0;
    I64 mtime  = 0;

    test_expect_false(test, "exists_2 answers false, not the victim's true", file_exists_2(&view));
    test_expect_false(test, "exists_3 answers false, not the victim's true", file_exists_3(&buffer));
    test_expect_false(test, "size_2 answers false, not the victim's size", file_size_2(&view, &size));
    test_expect_false(test, "size_3 answers false, not the victim's size", file_size_3(&buffer, &size));
    test_expect_false(test, "modified_2 answers false, not the victim's mtime", file_modified_2(&view, &mtime));
    test_expect_false(test, "modified_3 answers false, not the victim's mtime", file_modified_3(&buffer, &mtime));

    /* The openers (memsec MED, 2026-10-18): they handed the Str's own buffer to fopen, which stops
     * at the NUL - so each answered a handle on the victim, and a write mode truncated it to
     * nothing. file_open_2/3 are not called here: in this checked build their refusal ends the
     * process, so test_oom.c (unchecked) pins them. */
    errno = 0;

    _expect_open_refused(test, "open_try_2 (rb) is nullptr, not a handle on the victim", file_open_try_2(&view, "rb"));
    _expect_open_refused(test, "open_try_3 (rb) is nullptr, not a handle on the victim", file_open_try_3(&buffer, "rb"));
    _expect_open_refused(test, "open_wait_2 (rb) is nullptr, not a handle on the victim", file_open_wait_2(&view, "rb", 1));
    _expect_open_refused(test, "open_wait_3 (rb) is nullptr, not a handle on the victim", file_open_wait_3(&buffer, "rb", 1));
    _expect_open_refused(test, "open_try_2 (wb) is nullptr", file_open_try_2(&view, "wb"));
    _expect_open_refused(test, "open_try_3 (wb) is nullptr", file_open_try_3(&buffer, "wb"));
    _expect_open_refused(test, "open_wait_2 (wb) is nullptr", file_open_wait_2(&view, "wb", 1));
    _expect_open_refused(test, "open_wait_3 (wb) is nullptr", file_open_wait_3(&buffer, "wb", 1));

    char *const after_open = file_read_to_char(victim);

    test_expect_string(test, "and the victim was not truncated", "victim", after_open);
    memory_free(after_open);

    // A prefix that is NOT there: a write-mode open of the cut-down name would CREATE it.
    USize const fresh_size                = char_length(fresh);
    char fresh_named[_TEST_NUL_PATH_SIZE] = DEFAULT_INITIALIZATION;

    memory_copy_1(fresh_named, fresh, fresh_size);
    memory_copy_1(fresh_named + fresh_size + 1, tail, sizeof(tail) - 1);

    USize const fresh_named_size = fresh_size + 1 + sizeof(tail) - 1;
    Str fresh_view               = str_init_static(fresh_named, fresh_named_size);
    String fresh_buffer          = string_init_static(fresh_named, fresh_named_size);

    _expect_open_refused(test, "open_try_2 (wb) of a missing prefix is nullptr", file_open_try_2(&fresh_view, "wb"));
    _expect_open_refused(test, "open_try_3 (ab) of a missing prefix is nullptr", file_open_try_3(&fresh_buffer, "ab"));
    test_expect_false(test, "and the prefix was not created", file_exists_1(fresh));

    str_uninit(&fresh_view);
    string_uninit(&fresh_buffer);

    // Copy: as the source, the victim would be read into other; as the destination, the copy
    // refuses an existing file anyway, so the CODE is what tells the guard from that refusal.
    Result const copy_from_2 = file_copy_2(&view, &other_view);
    Result const copy_from_3 = file_copy_3(&buffer, &other_buffer);
    Result const copy_onto_2 = file_copy_2(&source_view, &view);
    Result const copy_onto_3 = file_copy_3(&source_buffer, &buffer);

    test_expect_u(test, "copy_2 (source) is EMBEDDED_NUL", (USize) FILE_ARGUMENT_EMBEDDED_NUL, (USize) result_code(copy_from_2));
    test_expect_u(test, "copy_3 (source) is EMBEDDED_NUL", (USize) FILE_ARGUMENT_EMBEDDED_NUL, (USize) result_code(copy_from_3));
    test_expect_u(test, "copy_2 (destination) is EMBEDDED_NUL", (USize) FILE_ARGUMENT_EMBEDDED_NUL, (USize) result_code(copy_onto_2));
    test_expect_u(test, "copy_3 (destination) is EMBEDDED_NUL", (USize) FILE_ARGUMENT_EMBEDDED_NUL, (USize) result_code(copy_onto_3));
    test_expect_true(test, "and both are ARGUMENT", result_category(copy_from_2) == RESULT_CATEGORY_ARGUMENT && result_category(copy_onto_3) == RESULT_CATEGORY_ARGUMENT);
    test_expect_false(test, "and the victim was not copied anywhere", file_exists_1(other));

    // Replace: as the destination, the victim's content would become the source's.
    Result const replace_onto_2 = file_replace_2(&source_view, &view);
    Result const replace_onto_3 = file_replace_3(&source_buffer, &buffer);
    Result const replace_from_2 = file_replace_2(&view, &other_view);
    Result const replace_from_3 = file_replace_3(&buffer, &other_buffer);

    test_expect_u(test, "replace_2 (destination) is EMBEDDED_NUL", (USize) FILE_ARGUMENT_EMBEDDED_NUL, (USize) result_code(replace_onto_2));
    test_expect_u(test, "replace_3 (destination) is EMBEDDED_NUL", (USize) FILE_ARGUMENT_EMBEDDED_NUL, (USize) result_code(replace_onto_3));
    test_expect_u(test, "replace_2 (source) is EMBEDDED_NUL", (USize) FILE_ARGUMENT_EMBEDDED_NUL, (USize) result_code(replace_from_2));
    test_expect_u(test, "replace_3 (source) is EMBEDDED_NUL", (USize) FILE_ARGUMENT_EMBEDDED_NUL, (USize) result_code(replace_from_3));

    char *const after_replace = file_read_to_char(victim);

    test_expect_string(test, "and the victim's content is its own", "victim", after_replace);
    memory_free(after_replace);

    test_expect_false(test, "and nothing was replaced into other", file_exists_1(other));

    // Set modified: the victim would be re-dated to the epoch.
    Result const dated_2 = file_set_modified_2(&view, 0);
    Result const dated_3 = file_set_modified_3(&buffer, 0);
    I64 victim_mtime     = 0;

    test_expect_u(test, "set_modified_2 is EMBEDDED_NUL", (USize) FILE_ARGUMENT_EMBEDDED_NUL, (USize) result_code(dated_2));
    test_expect_u(test, "set_modified_3 is EMBEDDED_NUL", (USize) FILE_ARGUMENT_EMBEDDED_NUL, (USize) result_code(dated_3));
    test_expect_true(test, "and the victim keeps its mtime", file_modified_1(victim, &victim_mtime) && victim_mtime == _TEST_NUL_VICTIM_MTIME);

    // Rename: as the destination, the source would REPLACE the victim; as the source, it would move.
    Result const rename_onto_2 = file_rename_2(&source_view, &view);
    Result const rename_onto_3 = file_rename_3(&source_buffer, &buffer);

    test_expect_u(test, "rename_2 (destination) is EMBEDDED_NUL", (USize) FILE_ARGUMENT_EMBEDDED_NUL, (USize) result_code(rename_onto_2));
    test_expect_u(test, "rename_3 (destination) is EMBEDDED_NUL", (USize) FILE_ARGUMENT_EMBEDDED_NUL, (USize) result_code(rename_onto_3));
    test_expect_true(test, "and the source stayed put", file_exists_1(source));

    char *const after_rename = file_read_to_char(victim);

    test_expect_string(test, "and the victim was not replaced by it", "victim", after_rename);
    memory_free(after_rename);

    Result const rename_from_2 = file_rename_2(&view, &other_view);

    test_expect_u(test, "rename_2 (source) is EMBEDDED_NUL", (USize) FILE_ARGUMENT_EMBEDDED_NUL, (USize) result_code(rename_from_2));
    test_expect_true(test, "and the victim was not moved (rename_2)", file_exists_1(victim) && !file_exists_1(other));

    Result const rename_from_3 = file_rename_3(&buffer, &other_buffer);

    test_expect_u(test, "rename_3 (source) is EMBEDDED_NUL", (USize) FILE_ARGUMENT_EMBEDDED_NUL, (USize) result_code(rename_from_3));
    test_expect_true(test, "and the victim was not moved (rename_3)", file_exists_1(victim) && !file_exists_1(other));

    // Remove, last: a regression here takes the victim with it.
    Result const removed_2 = file_remove_2(&view);

    test_expect_true(test, "remove_2 is ARGUMENT", result_category(removed_2) == RESULT_CATEGORY_ARGUMENT);
    test_expect_u(test, "remove_2 is EMBEDDED_NUL", (USize) FILE_ARGUMENT_EMBEDDED_NUL, (USize) result_code(removed_2));
    test_expect_true(test, "remove_2: THE VICTIM SURVIVES", file_exists_1(victim));

    Result const removed_3 = file_remove_3(&buffer);

    test_expect_true(test, "remove_3 is ARGUMENT", result_category(removed_3) == RESULT_CATEGORY_ARGUMENT);
    test_expect_u(test, "remove_3 is EMBEDDED_NUL", (USize) FILE_ARGUMENT_EMBEDDED_NUL, (USize) result_code(removed_3));
    test_expect_true(test, "remove_3: THE VICTIM SURVIVES", file_exists_1(victim));

    // A plain call mistake, not the refusal the trash path is the remedy for, and no OS answer.
    test_expect_false(test, "EMBEDDED_NUL is not the unaddressable refusal", file_result_is_unaddressable(removed_2));
    test_expect_false(test, "EMBEDDED_NUL is not reported as not-found", file_result_is_not_found(removed_2));
    test_expect_false(test, "EMBEDDED_NUL is not reported as an existing destination", file_result_is_exists(removed_2));

    str_uninit(&view);
    string_uninit(&buffer);
    str_uninit(&source_view);
    string_uninit(&source_buffer);
    str_uninit(&other_view);
    string_uninit(&other_buffer);
    remove(victim);
    remove(source);
    remove(other);
    remove(fresh);

    test_case_end(test);
}

/* Open one sized VIEW of "victim.bak" in "wb" and pin what it reached: the named "victim" exists
 * and was truncated from its "stale" fixture, and the longer "victim.bak" kept its content. */
static void _expect_view_open_named(Test *const test, char const *const label, File *file) {
    test_expect_true(test, label, !memory_empty((void*) file));

    if (!memory_empty((void*) file)) {
        file_close(&file);
    }

    USize named_size = 1;
    bool const sized = file_size_1("victim", &named_size);
    char *const kept = file_read_to_char("victim.bak");

    test_expect_true(test, "  and it created / truncated the named \"victim\"", sized && named_size == 0);
    test_expect_string(test, "  and \"victim.bak\" survived with its content", "keep me", kept);
    memory_free(kept);
    _write_file("victim", "stale");
}

/*
 * design Mid (2026-10-18): file_open_try_2/3 and file_open_wait_2/3 handed the Str/String's own
 * buffer to fopen, which reads to a terminator - so a VIEW of "victim.bak" sized 6 opened, and in
 * "wb" truncated, "victim.bak", a file the caller never named. They now open a terminated copy of
 * exactly the named bytes, as file_exists_2 does.
 */
static void _test_sized_view_opens_its_size(Test *const test) {
    test_case_begin(test, "a sized Str / String VIEW opens exactly its size, never the longer buffer");

    remove("victim");
    remove("victim.bak");
    _write_file("victim.bak", "keep me");
    _write_file("victim", "stale");

    char spelled[]           = "victim.bak";
    USize const named_size   = char_length("victim");
    Str const view           = str_init_3(spelled, named_size);
    String const buffer_view = string_init_4(spelled, named_size);

    _expect_view_open_named(test, "open_try_2 (wb) of the 6-byte view opens", file_open_try_2(&view, "wb"));
    _expect_view_open_named(test, "open_try_3 (wb) of the 6-byte view opens", file_open_try_3(&buffer_view, "wb"));
    _expect_view_open_named(test, "open_wait_2 (wb) of the 6-byte view opens", file_open_wait_2(&view, "wb", 1));
    _expect_view_open_named(test, "open_wait_3 (wb) of the 6-byte view opens", file_open_wait_3(&buffer_view, "wb", 1));

    remove("victim");
    remove("victim.bak");

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Entry Point
 *============================================================================*/

int main(void) {
    LogConfig const log_config = {
        .level             = LOG_LEVEL_ERROR,
        .stream            = stdout,
        .timestamp_enabled = true,
        .autoflush         = true
    };

    log_init(log_config);

    Test test = test_init("tests/file/test_file_manage.c");

    test_suite_begin(&test, "file_manage");
    _test_copy(&test);
    _test_remove(&test);
    _test_rename(&test);
    _test_replace(&test);
    _test_set_modified(&test);
    _test_result_predicates(&test);
    _test_file_error(&test);
    _test_failure_cleanup(&test);
    _test_cross_device_true(&test);
    _test_unaddressable_path_refused(&test);
    _test_trimmed_name_refused(&test);
#ifdef __linux__
    _test_replace_permissions(&test);
    _test_dangling_symlink_destination(&test);
#endif
    _test_empty_path_guard(&test);
    _test_embedded_nul_refused(&test);
    _test_sized_view_opens_its_size(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}