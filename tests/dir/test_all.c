#include <stdlib.h>
/* Gated on the compiler-defined _WIN32, not CFW's OS_WINDOWS: OS_WINDOWS arrives with
 * <types.h> via <dir/dir.h> below, so testing it here would evaluate to 0 and silently skip
 * this include - _wfopen/_wremove would then compile only because MinGW happens to also
 * declare them in <stdio.h>. */
#ifdef __linux__
#include <sys/stat.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <wchar.h>
#endif

#include <dir/dir.h>
#include <test/test.h>

/*==============================================================================
 * MARK: - Macros and Constants
 *============================================================================*/

// Bytes the fixture check reads: more than the one 'x' a fixture holds, so extra content shows.
#define _TEST_FIXTURE_READ_SIZE 4
// Room for one generated growth-file path or name.
#define _TEST_NAME_SIZE 64
#define _TEST_SCRATCH "dir_test_scratch"

/* "café_λ.txt" spelled with explicit escapes so the test does not depend on this file's own
 * encoding, and split at every escape so no hex escape can swallow the next character.
 * Neither character exists in a Western-European ANSI code page, which is the point: the
 * ANSI enumeration APIs cannot represent this name at all. */
#define _TEST_UNICODE_NAME      "caf" "\xC3\xA9" "_" "\xCE\xBB" ".txt"
#define _TEST_UNICODE_NAME_WIDE L"caf" L"\x00E9" L"_" L"\x03BB" L".txt"

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

static bool _touch(char const *const path) {
    FILE *const file = fopen(path, "w");

    if (memory_empty(file)) {
        return false;
    }

    fputs("x", file);
    fclose(file);

    return true;
}

static bool _file_has_x(char const *const path) {
    FILE *const file = fopen(path, "rb");

    if (memory_empty(file)) {
        return false;
    }

    char buffer[_TEST_FIXTURE_READ_SIZE] = DEFAULT_INITIALIZATION;

    USize const read = fread(buffer, 1, sizeof buffer, file);

    fclose(file);

    return read == 1 && buffer[0] == 'x';
}

static bool _list_contains(DirEntry const *const entries, USize const count, char const *const name) {
    for (USize i = 0; i < count; i += 1) {
        if (char_compare_equal_1(entries[i].name, name)) {
            return true;
        }
    }

    return false;
}

// Treat the result as read-only; the array stays owned by the caller's dir_list_entries call.
static DirEntry* _entry_named(DirEntry const *const entries, USize const count, char const *const name) {
    for (USize i = 0; i < count; i += 1) {
        if (char_compare_equal_1(entries[i].name, name)) {
            return (DirEntry*) &entries[i];
        }
    }

    return nullptr;
}

// The thread's last OS error, read at the call: errno on Linux, GetLastError on Windows.
static USize _last_error(void) {
#ifdef _WIN32
    return (USize) GetLastError();
#else
    return (USize) errno;
#endif
}

// "<prefix>\0x" into OUT (zeroed by the caller, so the buffer supplies the NUL); answers its size.
static USize _nul_spelling(char const *const prefix, char *const out) {
    USize const prefix_size = char_length(prefix);

    memory_copy_1(out, prefix, prefix_size);
    out[prefix_size + 1] = 'x';

    return prefix_size + 2;
}

#ifdef _WIN32
/* The "\\?\" absolute spelling of a cwd-relative ASCII name - the one spelling Win32 does not
 * trim - so the fixture can create "d." beside "d" and inspect each on its own. '/' becomes
 * '\': a verbatim path takes no forward slashes. */
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

static bool _verbatim_create_directory(char const *const name) {
    WCHAR wide[_TEST_VERBATIM_CAPACITY] = DEFAULT_INITIALIZATION;

    return _verbatim_wide(name, wide) && (CreateDirectoryW(wide, nullptr) != 0 || GetLastError() == ERROR_ALREADY_EXISTS);
}

static bool _verbatim_exists(char const *const name) {
    WCHAR wide[_TEST_VERBATIM_CAPACITY] = DEFAULT_INITIALIZATION;

    return _verbatim_wide(name, wide) && GetFileAttributesW(wide) != INVALID_FILE_ATTRIBUTES;
}

static void _verbatim_remove_directory(char const *const name) {
    WCHAR wide[_TEST_VERBATIM_CAPACITY] = DEFAULT_INITIALIZATION;

    if (_verbatim_wide(name, wide)) {
        RemoveDirectoryW(wide);
    }
}

// Children before parents, so a removal never meets a populated directory.
static void _trimmed_fixture_cleanup(void) {
    char const *const names[] = {
        "dir_trim_d/sub", "dir_trim_d./sub", "dir_trim_d", "dir_trim_d.", "dir_trim_e", "dir_trim_e.",
        "dir_trim_moved", "dir_trim_source", "dir_trim_new", "dir_trim_new.", "dir_trim_plain"
    };

    for (USize i = 0; i < sizeof(names) / sizeof(names[0]); i += 1) {
        _verbatim_remove_directory(names[i]);
    }
}
#endif // _WIN32

/*==============================================================================
 * MARK: - Test Cases
 *============================================================================*/

static void _test_create(Test *const test) {
    test_case_begin(test, "dir_create");

    test_expect_true(test, "first create succeeds", dir_create_1(_TEST_SCRATCH));
    test_expect_true(test, "second create on existing dir succeeds", dir_create_1(_TEST_SCRATCH));
    test_expect_false(test, "create with missing parent fails", dir_create_1(_TEST_SCRATCH "/missing/child"));

    test_case_end(test);
}

static void _test_create_all(Test *const test) {
    test_case_begin(test, "dir_create_all");

    test_expect_true(test, "nested create succeeds", dir_create_all_1(_TEST_SCRATCH "/a/b/c"));
    test_expect_true(test, "nested chain exists", dir_exists_1(_TEST_SCRATCH "/a/b/c"));
    test_expect_true(test, "repeat on existing chain succeeds", dir_create_all_1(_TEST_SCRATCH "/a/b/c"));
    test_expect_true(test, "trailing separator accepted", dir_create_all_1(_TEST_SCRATCH "/t1/t2/"));
    test_expect_true(test, "trailing separator chain exists", dir_exists_1(_TEST_SCRATCH "/t1/t2"));

    test_case_end(test);
}

static void _test_exists(Test *const test) {
    test_case_begin(test, "dir_exists");

    test_expect_true(test, "existing dir detected", dir_exists_1(_TEST_SCRATCH));
    test_expect_false(test, "missing path rejected", dir_exists_1(_TEST_SCRATCH "/nope"));

    test_expect_true(test, "probe file created", _touch(_TEST_SCRATCH "/probe.txt"));
    test_expect_false(test, "plain file rejected", dir_exists_1(_TEST_SCRATCH "/probe.txt"));

    test_case_end(test);
}

static void _test_empty(Test *const test) {
    test_case_begin(test, "dir_empty");

    test_expect_true(test, "empty dir created", dir_create_1(_TEST_SCRATCH "/empty"));
    test_expect_true(test, "fresh dir is empty", dir_empty_1(_TEST_SCRATCH "/empty"));

    test_expect_true(test, "filler file created", _touch(_TEST_SCRATCH "/empty/filler.txt"));
    test_expect_false(test, "dir with a file is not empty", dir_empty_1(_TEST_SCRATCH "/empty"));

    test_expect_false(test, "missing path is not empty", dir_empty_1(_TEST_SCRATCH "/nope"));
    test_expect_false(test, "plain file is not empty", dir_empty_1(_TEST_SCRATCH "/probe.txt"));

    test_case_end(test);
}

static void _test_list(Test *const test) {
    test_case_begin(test, "dir_list");

    test_expect_true(test, "list dir created", dir_create_1(_TEST_SCRATCH "/list"));
    test_expect_true(test, "first file created", _touch(_TEST_SCRATCH "/list/one.txt"));
    test_expect_true(test, "second file created", _touch(_TEST_SCRATCH "/list/two.txt"));
    test_expect_true(test, "subdir created", dir_create_1(_TEST_SCRATCH "/list/sub"));

    DirEntry *entries = nullptr;
    USize     count   = 0;

    test_expect_true(test, "listing succeeds", dir_list_entries_1(_TEST_SCRATCH "/list", &entries, &count));
    test_expect_u(test, "three entries listed", 3, count);
    test_expect_true(test, "first file listed", _list_contains(entries, count, "one.txt"));
    test_expect_true(test, "second file listed", _list_contains(entries, count, "two.txt"));
    test_expect_true(test, "subdir listed", _list_contains(entries, count, "sub"));

    dir_list_entries_uninit(entries, count);

    DirEntry *no_entries = nullptr;
    USize     no_count   = 0;

    test_expect_true(test, "empty dir listing succeeds", dir_create_1(_TEST_SCRATCH "/list_empty") && dir_list_entries_1(_TEST_SCRATCH "/list_empty", &no_entries, &no_count));
    test_expect_u(test, "empty dir lists zero entries", 0, no_count);

    dir_list_entries_uninit(no_entries, no_count);

    DirEntry *missing       = nullptr;
    USize     missing_count = 0;

    test_expect_false(test, "missing dir listing fails", dir_list_entries_1(_TEST_SCRATCH "/nope", &missing, &missing_count));

    test_case_end(test);
}

static void _test_list_entries(Test *const test) {
    test_case_begin(test, "dir_list_entries");

    test_expect_true(test, "entries dir created", dir_create_1(_TEST_SCRATCH "/entries"));
    test_expect_true(test, "entry file created", _touch(_TEST_SCRATCH "/entries/one.txt"));
    test_expect_true(test, "entry subdir created", dir_create_1(_TEST_SCRATCH "/entries/sub"));

    DirEntry *entries = nullptr;
    USize count       = 0;

    test_expect_true(test, "listing entries succeeds", dir_list_entries_1(_TEST_SCRATCH "/entries", &entries, &count));
    test_expect_u(test, "two entries listed", 2, count);

    DirEntry const *const file = _entry_named(entries, count, "one.txt");
    DirEntry const *const sub  = _entry_named(entries, count, "sub");

    test_expect_not_null(test, "file entry present", (void*) file);
    test_expect_not_null(test, "subdir entry present", (void*) sub);

    if (!memory_empty((void*) file)) {
        test_expect_false(test, "file not flagged as directory", file->is_dir);
        test_expect_false(test, "file not flagged as link", file->is_link);
        test_expect_u(test, "file size gathered from the enumeration", 1, file->size);
        test_expect_true(test, "file mtime gathered from the enumeration", file->mtime > 0);
    }

    if (!memory_empty((void*) sub)) {
        test_expect_true(test, "subdir flagged as directory", sub->is_dir);
        test_expect_false(test, "subdir not flagged as link", sub->is_link);
    }

    dir_list_entries_uninit(entries, count);

    DirEntry *empty_entries = nullptr;
    USize empty_count       = 1;

    test_expect_true(test, "empty dir listing succeeds", dir_create_1(_TEST_SCRATCH "/entries_empty") && dir_list_entries_1(_TEST_SCRATCH "/entries_empty", &empty_entries, &empty_count));
    test_expect_u(test, "empty dir yields zero entries", 0, empty_count);

    dir_list_entries_uninit(empty_entries, empty_count);

    DirEntry *missing_entries = nullptr;
    USize missing_count       = 0;

    test_expect_false(test, "missing dir listing fails", dir_list_entries_1(_TEST_SCRATCH "/nope", &missing_entries, &missing_count));
    test_expect_false(test, "plain file listing fails", dir_list_entries_1(_TEST_SCRATCH "/probe.txt", &missing_entries, &missing_count));

    test_case_end(test);
}

/* Every other listing case here fits inside _dir_entries_push's first allocation, so the
 * realloc-growth branch (16 -> 32 -> 64) would never run. 40 entries forces two grows, which
 * is the path where a wrong old-size argument or a dropped temporary would corrupt the array
 * rather than merely fail. */
static void _test_list_entries_growth(Test *const test) {
    test_case_begin(test, "dir_list_entries array growth");

    test_expect_true(test, "growth dir created", dir_create_1(_TEST_SCRATCH "/growth"));

    USize const wanted = 40;
    bool created       = true;

    for (USize i = 0; i < wanted; i += 1) {
        char name[_TEST_NAME_SIZE] = DEFAULT_INITIALIZATION;

        char_format(name, sizeof name, _TEST_SCRATCH "/growth/file_%llu.txt", (unsigned long long) i);

        created = _touch(name) && created;
    }

    test_expect_true(test, "all growth files created", created);

    DirEntry *entries = nullptr;
    USize count       = 0;

    test_expect_true(test, "growth dir listed", dir_list_entries_1(_TEST_SCRATCH "/growth", &entries, &count));
    test_expect_u(test, "every entry survived the reallocs", wanted, count);

    // A corrupted grow shows up as a lost or repeated element, so check every name is present
    // exactly once rather than trusting the count alone.
    bool all_present = true;

    for (USize i = 0; i < wanted; i += 1) {
        char name[_TEST_NAME_SIZE] = DEFAULT_INITIALIZATION;

        char_format(name, sizeof name, "file_%llu.txt", (unsigned long long) i);

        USize seen = 0;

        for (USize j = 0; j < count; j += 1) {
            if (char_compare_equal_1(entries[j].name, name)) {
                seen += 1;
            }
        }

        all_present = seen == 1 && all_present;
    }

    test_expect_true(test, "each name present exactly once after growth", all_present);

    bool metadata_intact = true;

    for (USize i = 0; i < count; i += 1) {
        metadata_intact = !entries[i].is_dir && entries[i].size == 1 && metadata_intact;
    }

    test_expect_true(test, "metadata intact across the grown array", metadata_intact);

    dir_list_entries_uninit(entries, count);

    test_case_end(test);
}

static void _test_list_entries_unicode(Test *const test) {
    test_case_begin(test, "dir_list_entries unicode names");

    test_expect_true(test, "unicode dir created", dir_create_1(_TEST_SCRATCH "/uni"));

    // The file has to be created through an API that can express the name: the wide CRT on
    // Windows, plain UTF-8 bytes on Linux.
#if OS_WINDOWS
    FILE *const file = _wfopen(L"" _TEST_SCRATCH L"/uni/" _TEST_UNICODE_NAME_WIDE, L"wb");
#else
    FILE *const file = fopen(_TEST_SCRATCH "/uni/" _TEST_UNICODE_NAME, "wb");
#endif

    test_expect_not_null(test, "unicode-named file created", (void*) file);

    if (!memory_empty((void*) file)) {
        fputs("x", file);
        fclose(file);

        DirEntry *entries = nullptr;
        USize count       = 0;

        test_expect_true(test, "unicode dir listed", dir_list_entries_1(_TEST_SCRATCH "/uni", &entries, &count));
        test_expect_u(test, "one unicode entry listed", 1, count);

        if (count == 1) {
            // The whole point of the wide enumeration: the name survives byte-for-byte
            // instead of arriving as "caf?_?.txt" from an ANSI code page.
            test_expect_string(test, "unicode name round-trips as UTF-8", _TEST_UNICODE_NAME, entries[0].name);
            test_expect_u(test, "unicode entry size gathered", 1, entries[0].size);
        }

        dir_list_entries_uninit(entries, count);

        /* The module's own recursive removal must handle this name. It could not while the
         * rest of dir was ANSI: FindFirstFileA cannot even SEE a name outside the active code
         * page, so dir_remove_all returned false and stranded the file. This assertion is the
         * regression guard for that - it fails the moment any of these paths goes back to the
         * A APIs. */
        test_expect_true(test, "dir_remove_all removes a tree holding a non-ANSI name", dir_remove_all_1(_TEST_SCRATCH "/uni"));
        test_expect_false(test, "unicode tree is gone", dir_exists_1(_TEST_SCRATCH "/uni"));
    }

    test_case_end(test);
}

static void _test_list_entries_link(Test *const test) {
    test_case_begin(test, "dir_list_entries link detection");

    test_expect_true(test, "link parent created", dir_create_1(_TEST_SCRATCH "/links"));
    test_expect_true(test, "link target created", dir_create_1(_TEST_SCRATCH "/links/target"));

    /* A directory link is what proves the reparse-tag read: is_link is not a plain attribute
     * bit, it is FILE_ATTRIBUTE_REPARSE_POINT AND IsReparseTagNameSurrogate on the tag (in
     * dwReserved0 on Windows), which is what separates a real link from a cloud placeholder
     * that also sets the reparse attribute. A junction exercises exactly that pair, and needs
     * no elevation, unlike a symbolic link. */
#if OS_WINDOWS
    bool const linked = system("cmd /c mklink /J " _TEST_SCRATCH "\\links\\alias " _TEST_SCRATCH "\\links\\target >nul 2>&1") == 0;
#else
    bool const linked = symlink("target", _TEST_SCRATCH "/links/alias") == 0;
#endif

    test_expect_true(test, "directory link created", linked);

    if (linked) {
        DirEntry *entries = nullptr;
        USize count       = 0;

        test_expect_true(test, "link parent listed", dir_list_entries_1(_TEST_SCRATCH "/links", &entries, &count));

        DirEntry const *const alias  = _entry_named(entries, count, "alias");
        DirEntry const *const target = _entry_named(entries, count, "target");

        test_expect_not_null(test, "link entry present", (void*) alias);
        test_expect_not_null(test, "target entry present", (void*) target);

        if (!memory_empty((void*) alias)) {
            test_expect_true(test, "link flagged as link", alias->is_link);
            test_expect_true(test, "link to a directory flagged as directory", alias->is_dir);
        }

        if (!memory_empty((void*) target)) {
            test_expect_false(test, "plain directory not flagged as link", target->is_link);
        }

        dir_list_entries_uninit(entries, count);
    }

    test_case_end(test);
}

static void _test_current(Test *const test) {
    test_case_begin(test, "dir_get_current / dir_set_current");

    char *const before = dir_get_current();

    test_expect_not_null(test, "current dir readable", before);

    test_expect_true(test, "change into scratch dir", dir_set_current_1(_TEST_SCRATCH));

    char *const inside = dir_get_current();

    test_expect_not_null(test, "current dir readable after change", inside);

    if (!memory_empty(inside)) {
        test_expect_string_contains(test, "current dir reflects change", inside, _TEST_SCRATCH);

        char_delete(inside);
    }

    if (!memory_empty(before)) {
        test_expect_true(test, "restore previous dir", dir_set_current_1(before));

        char_delete(before);
    }

    test_expect_false(test, "change into missing dir fails", dir_set_current_1(_TEST_SCRATCH "/nope"));

    test_case_end(test);
}

static void _test_copy_all(Test *const test) {
    test_case_begin(test, "dir_copy_all");

    test_expect_true(test, "source tree created", dir_create_all_1(_TEST_SCRATCH "/csrc/sub"));
    test_expect_true(test, "source root file created", _touch(_TEST_SCRATCH "/csrc/root.txt"));
    test_expect_true(test, "source nested file created", _touch(_TEST_SCRATCH "/csrc/sub/nested.txt"));

    test_expect_true(test, "copy succeeds", dir_copy_all_1(_TEST_SCRATCH "/csrc", _TEST_SCRATCH "/cdst"));
    test_expect_true(test, "destination dir exists", dir_exists_1(_TEST_SCRATCH "/cdst"));
    test_expect_true(test, "destination subdir exists", dir_exists_1(_TEST_SCRATCH "/cdst/sub"));
    test_expect_true(test, "root file content copied", _file_has_x(_TEST_SCRATCH "/cdst/root.txt"));
    test_expect_true(test, "nested file content copied", _file_has_x(_TEST_SCRATCH "/cdst/sub/nested.txt"));

    test_expect_true(test, "source tree untouched", _file_has_x(_TEST_SCRATCH "/csrc/sub/nested.txt"));
    test_expect_true(test, "merge into existing destination succeeds", dir_copy_all_1(_TEST_SCRATCH "/csrc", _TEST_SCRATCH "/cdst"));

    test_expect_false(test, "copy onto itself refused", dir_copy_all_1(_TEST_SCRATCH "/csrc", _TEST_SCRATCH "/csrc"));
    test_expect_false(test, "copy into own subtree refused", dir_copy_all_1(_TEST_SCRATCH "/csrc", _TEST_SCRATCH "/csrc/inner"));
    test_expect_false(test, "missing source refused", dir_copy_all_1(_TEST_SCRATCH "/nope", _TEST_SCRATCH "/cdst2"));
    test_expect_false(test, "plain file source refused", dir_copy_all_1(_TEST_SCRATCH "/probe.txt", _TEST_SCRATCH "/cdst2"));

#ifdef __linux__
    test_expect_true(test, "source symlink created", symlink("root.txt", _TEST_SCRATCH "/csrc/link") == 0);
    test_expect_true(test, "copy with symlink succeeds", dir_copy_all_1(_TEST_SCRATCH "/csrc", _TEST_SCRATCH "/clnk"));

    struct stat info = DEFAULT_INITIALIZATION;

    test_expect_true(test, "symlink recreated as link", lstat(_TEST_SCRATCH "/clnk/link", &info) == 0 && S_ISLNK(info.st_mode));
#endif

    test_case_end(test);
}

static void _test_overloads(Test *const test) {
    test_case_begin(test, "Str / String overloads");

    Str scratch_2      = str_init_static(_TEST_SCRATCH, char_length(_TEST_SCRATCH));
    Str overload_2     = str_init_static(_TEST_SCRATCH "/ov2", char_length(_TEST_SCRATCH "/ov2"));
    String overload_3  = string_init_static(_TEST_SCRATCH "/ov3", char_length(_TEST_SCRATCH "/ov3"));

    test_expect_true(test, "create via Str", dir_create_2(&overload_2));
    test_expect_true(test, "exists via Str", dir_exists_2(&overload_2));
    test_expect_true(test, "empty via Str", dir_empty_2(&overload_2));
    test_expect_true(test, "remove via Str", dir_remove_2(&overload_2));
    test_expect_false(test, "removed dir gone via Str", dir_exists_2(&overload_2));

    test_expect_true(test, "create via String", dir_create_3(&overload_3));
    test_expect_true(test, "exists via String", dir_exists_3(&overload_3));
    test_expect_true(test, "remove all via String", dir_remove_all_3(&overload_3));
    test_expect_false(test, "removed dir gone via String", dir_exists_3(&overload_3));

    DirEntry *entries = nullptr;
    USize     count   = 0;

    test_expect_true(test, "list via Str", dir_list_entries_2(&scratch_2, &entries, &count));
    test_expect_true(test, "listing not empty", count > 0);

    dir_list_entries_uninit(entries, count);

    Str const blank = str_init_1();

    test_expect_false(test, "empty Str path returns false", dir_exists_2(&blank));
    test_expect_false(test, "empty Str create returns false", dir_create_2(&blank));

    str_uninit(&scratch_2);
    str_uninit(&overload_2);
    string_uninit(&overload_3);

    test_case_end(test);
}

static void _test_rename(Test *const test) {
    test_case_begin(test, "dir_rename");

    test_expect_true(test, "source dir created", dir_create_1(_TEST_SCRATCH "/old"));
    test_expect_true(test, "source file created", _touch(_TEST_SCRATCH "/old/keep.txt"));

    test_expect_true(test, "rename succeeds", dir_rename_1(_TEST_SCRATCH "/old", _TEST_SCRATCH "/new"));
    test_expect_false(test, "old name gone", dir_exists_1(_TEST_SCRATCH "/old"));
    test_expect_true(test, "new name exists", dir_exists_1(_TEST_SCRATCH "/new"));

    test_expect_false(test, "rename of missing dir fails", dir_rename_1(_TEST_SCRATCH "/old", _TEST_SCRATCH "/older"));

    test_case_end(test);
}

static void _test_remove(Test *const test) {
    test_case_begin(test, "dir_remove");

    test_expect_true(test, "removable dir created", dir_create_1(_TEST_SCRATCH "/gone"));
    test_expect_true(test, "empty dir removed", dir_remove_1(_TEST_SCRATCH "/gone"));
    test_expect_false(test, "removed dir no longer exists", dir_exists_1(_TEST_SCRATCH "/gone"));

    test_expect_false(test, "non-empty dir refused", dir_remove_1(_TEST_SCRATCH "/list"));
    test_expect_false(test, "missing dir refused", dir_remove_1(_TEST_SCRATCH "/nope"));

    test_case_end(test);
}

static void _test_remove_all(Test *const test) {
    test_case_begin(test, "dir_remove_all");

    test_expect_false(test, "empty path refused", dir_remove_all_1(""));
    test_expect_false(test, "root refused", dir_remove_all_1("/"));
    test_expect_false(test, "dot refused", dir_remove_all_1("."));
    test_expect_false(test, "dot dot refused", dir_remove_all_1(".."));
    test_expect_false(test, "drive root refused", dir_remove_all_1("C:/"));
    test_expect_false(test, "dot dot segment refused", dir_remove_all_1(_TEST_SCRATCH "/../" _TEST_SCRATCH));
    test_expect_false(test, "missing dir refused", dir_remove_all_1(_TEST_SCRATCH "/nope"));

#ifdef __linux__
    test_expect_true(test, "symlink target created", dir_create_all_1(_TEST_SCRATCH "/target"));
    test_expect_true(test, "symlink target file created", _touch(_TEST_SCRATCH "/target/keep.txt"));
    test_expect_true(test, "linked tree created", dir_create_1(_TEST_SCRATCH "/linked"));
    test_expect_true(test, "symlink created", symlink("../target", _TEST_SCRATCH "/linked/link") == 0);

    test_expect_true(test, "linked tree removed", dir_remove_all_1(_TEST_SCRATCH "/linked"));
    test_expect_true(test, "symlink target survives", dir_exists_1(_TEST_SCRATCH "/target"));
    test_expect_false(test, "symlink to dir itself refused",
        dir_create_1(_TEST_SCRATCH "/linked") && symlink("../target", _TEST_SCRATCH "/linked/link") == 0 && dir_remove_all_1(_TEST_SCRATCH "/linked/link"));
    test_expect_true(test, "linked tree cleanup", dir_remove_all_1(_TEST_SCRATCH "/linked"));
#endif

    test_expect_true(test, "whole scratch tree removed", dir_remove_all_1(_TEST_SCRATCH));
    test_expect_false(test, "scratch tree gone", dir_exists_1(_TEST_SCRATCH));

    test_case_end(test);
}

/*
 * A component ending in '.' or ' ' is REFUSED, never trimmed onto its twin.
 *
 * Windows-only because the defect is: Win32 path normalisation strips a trailing '.' or ' '
 * from every component of a non-"\\?\" path, so RemoveDirectoryW("d.") removed "d". Each twin
 * pair is created through "\\?\" (CreateDirectoryW), which does not trim, and every assertion
 * checks BOTH twins.
 */
static void _test_trimmed_name_refused(Test *const test) {
    test_case_begin(test, "a component ending in '.' or ' ' is REFUSED, never trimmed onto its twin");

#ifdef _WIN32
    _trimmed_fixture_cleanup();

    // dir_trim_e / dir_trim_e. are EMPTY twins (dir_remove_1 removes only an empty directory);
    // dir_trim_d / dir_trim_d. each hold a "sub", for the middle-component and recursive rows.
    bool const fixture = _verbatim_create_directory("dir_trim_e")
        && _verbatim_create_directory("dir_trim_e.")
        && _verbatim_create_directory("dir_trim_d")
        && _verbatim_create_directory("dir_trim_d.")
        && _verbatim_create_directory("dir_trim_d/sub")
        && _verbatim_create_directory("dir_trim_d./sub")
        && _verbatim_create_directory("dir_trim_source");

    test_expect_true(test, "fixture: every twin pair exists side by side", fixture);

    if (!fixture) {
        _trimmed_fixture_cleanup();
        test_case_end(test);

        return;
    }

    // Removal: the final component, a trailing space, and a MIDDLE component.
    // Captured at the call: the assertion's own output may overwrite the thread's last error.
    bool const removed_dotted  = dir_remove_1("dir_trim_e.");
    DWORD const removed_reason = GetLastError();

    test_expect_false(test, "dir_remove_1(\"dir_trim_e.\") is refused", removed_dotted);
    test_expect_true(test, "with ERROR_INVALID_NAME", removed_reason == ERROR_INVALID_NAME);
    test_expect_true(test, "and dir_trim_e survives it", _verbatim_exists("dir_trim_e"));
    test_expect_true(test, "and dir_trim_e. survives it", _verbatim_exists("dir_trim_e."));
    test_expect_false(test, "dir_remove_1(\"dir_trim_e \") is refused", dir_remove_1("dir_trim_e "));
    test_expect_true(test, "and dir_trim_e survives the trailing space too", _verbatim_exists("dir_trim_e"));
    test_expect_false(test, "dir_remove_1(\"dir_trim_d./sub\") - a middle component - is refused", dir_remove_1("dir_trim_d./sub"));
    test_expect_true(test, "and dir_trim_d/sub survives it", _verbatim_exists("dir_trim_d/sub"));
    test_expect_true(test, "and dir_trim_d./sub survives it", _verbatim_exists("dir_trim_d./sub"));

    // Recursive removal: a trimmed root would walk and delete the OTHER tree.
    test_expect_false(test, "dir_remove_all_1(\"dir_trim_d.\") is refused", dir_remove_all_1("dir_trim_d."));
    test_expect_true(test, "and dir_trim_d's tree survives it", _verbatim_exists("dir_trim_d/sub"));
    test_expect_true(test, "and dir_trim_d.'s tree survives it", _verbatim_exists("dir_trim_d./sub"));

    // Rename: a trimmed SOURCE would move the twin; a trimmed DESTINATION would land on its spelling.
    test_expect_false(test, "dir_rename_1 refuses a trimmed source", dir_rename_1("dir_trim_e.", "dir_trim_moved"));
    test_expect_true(test, "and dir_trim_e was not moved", _verbatim_exists("dir_trim_e") && !_verbatim_exists("dir_trim_moved"));
    test_expect_true(test, "and dir_trim_e. was not moved", _verbatim_exists("dir_trim_e."));

    bool const renamed_onto    = dir_rename_1("dir_trim_source", "dir_trim_new.");
    DWORD const renamed_reason = GetLastError();

    test_expect_false(test, "dir_rename_1 refuses a trimmed destination", renamed_onto);
    test_expect_true(test, "with ERROR_INVALID_NAME", renamed_reason == ERROR_INVALID_NAME);
    test_expect_true(test, "and the source stayed put", _verbatim_exists("dir_trim_source"));
    test_expect_false(test, "and dir_trim_new was not created", _verbatim_exists("dir_trim_new"));

    // THE POINT: the rule is not collateral damage. "." segments and "\\?\" paths still work.
    test_expect_true(test, "a \".\" component is not a trimmed name (create)", dir_create_1("./dir_trim_plain"));
    test_expect_true(test, "a \".\" component is not a trimmed name (remove)", dir_remove_1("./dir_trim_plain"));

    char verbatim[_TEST_VERBATIM_CAPACITY] = DEFAULT_INITIALIZATION;

    test_expect_true(test, "fixture: the narrow \\\\?\\ spelling of dir_trim_e.", _verbatim_narrow("dir_trim_e.", verbatim));
    test_expect_true(test, "a \\\\?\\ path is exempt: it removes exactly dir_trim_e.", dir_remove_1(verbatim));
    test_expect_false(test, "dir_trim_e. is gone", _verbatim_exists("dir_trim_e."));
    test_expect_true(test, "and dir_trim_e is untouched", _verbatim_exists("dir_trim_e"));

    _trimmed_fixture_cleanup();
#else
    // Linux trims nothing: "d." is a name of its own there, and nothing is refused.
    test_expect_true(test, "not applicable on this platform", true);
#endif

    test_case_end(test);
}

/*
 * memsec MED (2026-10-18): every _2/_3 overload copied its sized path whole with char_new_3 and
 * handed the copy to the _1 form, which reads only up to the first NUL - so
 * dir_remove_all_2("important\0junk") removed "important", a tree the caller never named, and
 * reported success. Each fixture is that prefix, so the defect is its disappearance or change.
 */
static void _test_embedded_nul_refused(Test *const test) {
    test_case_begin(test, "a Str / String path holding a NUL byte is refused, never cut down to its prefix");

#ifdef _WIN32
    USize const refused_code = ERROR_INVALID_NAME;
#else
    USize const refused_code = EINVAL;
#endif

    dir_remove_all_1("dir_nul_victim");
    dir_remove_all_1("dir_nul_tree");
    dir_remove_all_1("dir_nul_source");
    dir_remove_all_1("dir_nul_other");
    dir_remove_all_1("dir_nul_created");

    bool const fixture = dir_create_1("dir_nul_victim")
        && dir_create_all_1("dir_nul_tree/inner")
        && _touch("dir_nul_tree/inner/keep.txt")
        && dir_create_1("dir_nul_source")
        && _touch("dir_nul_source/source.txt");

    test_expect_true(test, "fixture: the empty victim, the populated tree and a source exist", fixture);

    char victim_named[_TEST_NAME_SIZE]  = DEFAULT_INITIALIZATION;
    char tree_named[_TEST_NAME_SIZE]    = DEFAULT_INITIALIZATION;
    char created_named[_TEST_NAME_SIZE] = DEFAULT_INITIALIZATION;

    USize const victim_size  = _nul_spelling("dir_nul_victim", victim_named);
    USize const tree_size    = _nul_spelling("dir_nul_tree", tree_named);
    USize const created_size = _nul_spelling("dir_nul_created", created_named);

    // Owned deep copies (str_init_static copies its whole size), so the NUL travels with them.
    Str victim_view       = str_init_static(victim_named, victim_size);
    String victim_buffer  = string_init_static(victim_named, victim_size);
    Str tree_view         = str_init_static(tree_named, tree_size);
    String tree_buffer    = string_init_static(tree_named, tree_size);
    Str created_view      = str_init_static(created_named, created_size);
    String created_buffer = string_init_static(created_named, created_size);
    Str source_view       = str_init_static("dir_nul_source", char_length("dir_nul_source"));
    String source_buffer  = string_init_static("dir_nul_source", char_length("dir_nul_source"));
    Str other_view        = str_init_static("dir_nul_other", char_length("dir_nul_other"));
    String other_buffer   = string_init_static("dir_nul_other", char_length("dir_nul_other"));

    // Remove: the victim is EMPTY, so without the guard dir_remove_1 would take it.
    bool const removed_2       = dir_remove_2(&victim_view);
    USize const removed_2_code = _last_error();

    test_expect_false(test, "remove_2 is refused", removed_2);
    test_expect_u(test, "with the invalid-name code", refused_code, removed_2_code);
    test_expect_true(test, "remove_2: THE VICTIM SURVIVES", dir_exists_1("dir_nul_victim"));

    bool const removed_3       = dir_remove_3(&victim_buffer);
    USize const removed_3_code = _last_error();

    test_expect_false(test, "remove_3 is refused", removed_3);
    test_expect_u(test, "with the invalid-name code", refused_code, removed_3_code);
    test_expect_true(test, "remove_3: THE VICTIM SURVIVES", dir_exists_1("dir_nul_victim"));

    // Recursive remove: the tree would be walked and deleted.
    bool const removed_all_2       = dir_remove_all_2(&tree_view);
    USize const removed_all_2_code = _last_error();

    test_expect_false(test, "remove_all_2 is refused", removed_all_2);
    test_expect_u(test, "with the invalid-name code", refused_code, removed_all_2_code);
    test_expect_true(test, "remove_all_2: THE TREE SURVIVES", _file_has_x("dir_nul_tree/inner/keep.txt"));
    test_expect_false(test, "remove_all_3 is refused", dir_remove_all_3(&tree_buffer));
    test_expect_true(test, "remove_all_3: THE TREE SURVIVES", _file_has_x("dir_nul_tree/inner/keep.txt"));

    // Rename: as the source the victim would move; as the destination the source would land on it.
    test_expect_false(test, "rename_2 (source) is refused", dir_rename_2(&victim_view, &other_view));
    test_expect_false(test, "rename_3 (source) is refused", dir_rename_3(&victim_buffer, &other_buffer));
    test_expect_true(test, "and the victim was not moved", dir_exists_1("dir_nul_victim") && !dir_exists_1("dir_nul_other"));

    bool const renamed_onto       = dir_rename_2(&source_view, &victim_view);
    USize const renamed_onto_code = _last_error();

    test_expect_false(test, "rename_2 (destination) is refused", renamed_onto);
    test_expect_u(test, "with the invalid-name code", refused_code, renamed_onto_code);
    test_expect_false(test, "rename_3 (destination) is refused", dir_rename_3(&source_buffer, &victim_buffer));
    test_expect_true(test, "and the source stayed put", _file_has_x("dir_nul_source/source.txt"));
    test_expect_true(test, "and the victim is still empty", dir_empty_1("dir_nul_victim"));

    // Copy: as the source the tree would be copied out; as the destination it would be filled.
    test_expect_false(test, "copy_all_2 (source) is refused", dir_copy_all_2(&tree_view, &other_view));
    test_expect_false(test, "copy_all_3 (source) is refused", dir_copy_all_3(&tree_buffer, &other_buffer));
    test_expect_false(test, "and nothing was copied out", dir_exists_1("dir_nul_other"));

    bool const copied_onto       = dir_copy_all_2(&source_view, &victim_view);
    USize const copied_onto_code = _last_error();

    test_expect_false(test, "copy_all_2 (destination) is refused", copied_onto);
    test_expect_u(test, "with the invalid-name code", refused_code, copied_onto_code);
    test_expect_false(test, "copy_all_3 (destination) is refused", dir_copy_all_3(&source_buffer, &victim_buffer));
    test_expect_true(test, "and the victim was not filled", dir_empty_1("dir_nul_victim"));

    // Create: the prefix would be created.
    bool const created_2       = dir_create_2(&created_view);
    USize const created_2_code = _last_error();

    test_expect_false(test, "create_2 is refused", created_2);
    test_expect_u(test, "with the invalid-name code", refused_code, created_2_code);
    test_expect_false(test, "create_3 is refused", dir_create_3(&created_buffer));
    test_expect_false(test, "create_all_2 is refused", dir_create_all_2(&created_view));
    test_expect_false(test, "create_all_3 is refused", dir_create_all_3(&created_buffer));
    test_expect_false(test, "and the prefix was never created", dir_exists_1("dir_nul_created"));

    // The queries: without the guard each answers about the victim, which is there and empty.
    DirEntry *entries = nullptr;
    USize count       = 0;

    test_expect_false(test, "exists_2 answers false, not the victim's true", dir_exists_2(&victim_view));
    test_expect_false(test, "exists_3 answers false, not the victim's true", dir_exists_3(&victim_buffer));
    test_expect_false(test, "empty_2 answers false, not the victim's true", dir_empty_2(&victim_view));
    test_expect_false(test, "empty_3 answers false, not the victim's true", dir_empty_3(&victim_buffer));
    test_expect_false(test, "list_entries_2 is refused", dir_list_entries_2(&victim_view, &entries, &count));
    test_expect_false(test, "list_entries_3 is refused", dir_list_entries_3(&victim_buffer, &entries, &count));

    // Last: a regression here would move the process into the victim.
    bool const entered_2 = dir_set_current_2(&victim_view);
    bool const entered_3 = !entered_2 && dir_set_current_3(&victim_buffer);

    test_expect_false(test, "set_current_2 is refused", entered_2);
    test_expect_false(test, "set_current_3 is refused", entered_3);

    if (entered_2 || entered_3) {
        dir_set_current_1("..");
    }

    str_uninit(&victim_view);
    string_uninit(&victim_buffer);
    str_uninit(&tree_view);
    string_uninit(&tree_buffer);
    str_uninit(&created_view);
    string_uninit(&created_buffer);
    str_uninit(&source_view);
    string_uninit(&source_buffer);
    str_uninit(&other_view);
    string_uninit(&other_buffer);

    dir_remove_all_1("dir_nul_victim");
    dir_remove_all_1("dir_nul_tree");
    dir_remove_all_1("dir_nul_source");
    dir_remove_all_1("dir_nul_other");
    dir_remove_all_1("dir_nul_created");

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Entry Point
 *============================================================================*/

I32 main(void) {
    LogConfig const log_config = { .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true };

    log_init(log_config);

    dir_remove_all_1(_TEST_SCRATCH); /* stale scratch from an aborted earlier run */

    Test test = test_init("./test_all.c");

    test_verbose_set(&test, false);

    test_suite_begin(&test, "dir");
    _test_create(&test);
    _test_create_all(&test);
    _test_exists(&test);
    _test_empty(&test);
    _test_list(&test);
    _test_list_entries(&test);
    _test_list_entries_growth(&test);
    _test_list_entries_unicode(&test);
    _test_list_entries_link(&test);
    _test_current(&test);
    _test_copy_all(&test);
    _test_overloads(&test);
    _test_rename(&test);
    _test_remove(&test);
    _test_remove_all(&test);
    _test_trimmed_name_refused(&test);
    _test_embedded_nul_refused(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}