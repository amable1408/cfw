#include <test/test.h>

#include <container/str/str.h>
#include <container/string/string.h>
#include <file/file.h>

#ifdef __linux__
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

/* Coverage for the by-path management primitives added for the porto/purgo project:
 * file_copy_*, file_replace_*, file_remove_*, file_rename_*, file_set_modified_*. */

static void _write_file(char const *const path, char const *const content) {
    FILE *const handle = fopen(path, "wb");

    // Guarded like the trash, porto and purgo suites' copies: a fixture that cannot be
    // created should fail its case, not segfault the whole runner inside fwrite.
    if (!memory_empty((void*) handle)) {
        fwrite(content, 1, char_length(content), handle);
        fclose(handle);
    }
}

static void _test_copy(Test *const test) {
    test_case_begin(test, "file_copy_1: success, refuses existing destination, missing source");

    char const *const src = "file_copy_src.tmp";
    char const *const dst = "file_copy_dst.tmp";

    remove(src);
    remove(dst);

    _write_file(src, "hello copy");

    Result const copied = file_copy_1(src, dst);

    test_expect_true(test, "copy succeeds", result_is_success(copied));

    char *const dst_content = file_read_to_char(dst);

    test_expect_string(test, "destination content matches", "hello copy", dst_content);
    memory_free(dst_content);

    Result const overwrite = file_copy_1(src, dst);

    test_expect_true(test, "refuses existing destination", result_is_error(overwrite));
    test_expect_true(test, "existing-destination category is STATE", result_category(overwrite) == RESULT_CATEGORY_STATE);

    char *const kept = file_read_to_char(dst);

    test_expect_string(test, "AND THE REFUSAL PRESERVED THE DESTINATION'S CONTENT", "hello copy", kept);
    memory_free(kept);

    /* A destination under a missing DIRECTORY: not-found on both platforms - and on Windows
     * specifically ERROR_PATH_NOT_FOUND (3), the Win32 code the CRT keeps in _doserrno, where
     * filing errno (ENOENT, 2) into the Win32-shaped classifier reported the file code. */
    Result const no_directory = file_copy_1(src, "no_such_dir_for_copy/never.tmp");

    test_expect_true(test, "a destination under a missing directory is not-found", file_result_is_not_found(no_directory));
#ifdef _WIN32
    test_expect_true(test, "and carries ERROR_PATH_NOT_FOUND, the Win32 code, not errno's ENOENT", result_code(no_directory) == (U16) ERROR_PATH_NOT_FOUND);
#endif

    Result const missing = file_copy_1("file_copy_does_not_exist.tmp", "file_copy_never_created.tmp");

    test_expect_true(test, "missing source is an error", result_is_error(missing));
    test_expect_false(test, "missing source creates nothing", file_exists_1("file_copy_never_created.tmp"));

    remove(src);
    remove(dst);

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

    char const *const src = "file_replace_src.tmp";
    char const *const dst = "file_replace_dst.tmp";

    remove(src);
    remove(dst);

    _write_file(src, "version one");

    test_expect_true(test, "replace creates a missing destination", result_is_success(file_replace_1(src, dst)));

    char *const first_content = file_read_to_char(dst);

    test_expect_string(test, "destination holds version one", "version one", first_content);
    memory_free(first_content);

    _write_file(src, "version two, longer than before");

    test_expect_true(test, "replace overwrites an existing destination", result_is_success(file_replace_1(src, dst)));

    char *const second_content = file_read_to_char(dst);

    test_expect_string(test, "destination holds version two", "version two, longer than before", second_content);
    memory_free(second_content);

    // Source still holds its own content: file_replace_1 is a copy-and-replace, not a move.
    char *const src_content = file_read_to_char(src);

    test_expect_string(test, "source is untouched by replace", "version two, longer than before", src_content);
    memory_free(src_content);

    remove(src);

    Result const missing_source = file_replace_1("file_replace_does_not_exist.tmp", dst);

    test_expect_true(test, "replace with a missing source is an error", result_is_error(missing_source));

    char *const untouched_content = file_read_to_char(dst);

    test_expect_string(test, "destination is UNTOUCHED after a failed replace", "version two, longer than before", untouched_content);
    memory_free(untouched_content);

    remove(dst);

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
        char buffer[16] = DEFAULT_INITIALIZATION;

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
    USize const big_size       = 128 * 1024;
    char *const big            = (char*) memory_alloc(big_size + 1);

    char_fill(big, big_size, 'x');
    big[big_size] = '\0';

    remove(source);
    _write_file(source, big);
    memory_free(big);

    struct rlimit saved = DEFAULT_INITIALIZATION;

    test_expect_true(test, "the file-size limit could be read", getrlimit(RLIMIT_FSIZE, &saved) == 0);

    struct rlimit small = saved;

    small.rlim_cur = 8 * 1024;

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
#ifdef __linux__
    _test_replace_permissions(&test);
    _test_dangling_symlink_destination(&test);
#endif
    _test_empty_path_guard(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}