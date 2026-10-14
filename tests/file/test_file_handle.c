#ifdef _WIN32
#include <platform/windows/windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif // _WIN32

#include <char/char.h>
#include <container/str/str.h>
#include <container/string/string.h>
#include <file/file.h>
#include <log/log.h>
#include <memory/memory.h>
#include <result.h>
#include <test/test.h>
#include <thread/thread.h>
#include <types.h>

#ifdef _WIN32
// FSCTL_SET_REPARSE_POINT and REPARSE_GUID_DATA_BUFFER, for the unprivileged reparse fixtures.
// After the CFW block, not before it: <winioctl.h> is not self-contained - it needs the
// <windows.h> types that arrive through <file/file.h>.
#include <winioctl.h>
#endif // _WIN32

/* Coverage for the handle-first additions of file 0.6.0: file_open_nofollow_try_1/2/3,
 * file_final_path_1/2/3, file_get_final_path and file_get_modified - the four pieces a
 * server needs to open a request-chosen path ONCE, verify the handle, and serve from it.
 *
 * Its own arm rather than rows in test_file_io.c / test_file_meta.c: it needs an on-disk fixture
 * TREE (a directory, a link to a file, a link to a directory, a FIFO, a non-ASCII name) that
 * neither of those builds, and refusal rows whose expected OS codes differ per platform.
 *
 * SKIPPED rows print a SKIPPED line at WARN, never pass silently:
 *   - the file SYMLINK rows on Windows without SeCreateSymbolicLinkPrivilege (an unelevated
 *     account outside Developer Mode cannot create one). The junction rows always run there -
 *     mklink /J needs no privilege - and cover the directory-link and linked-ANCESTOR cases.
 *   - the FIFO row on Windows (no FIFO in the filesystem namespace), and the invalid-UTF-8 and
 *     device-spelling rows on Linux (bytes ARE names there; there is nothing to refuse).
 * NOT FIXTURED: a real cloud placeholder (Dropbox / OneDrive), whose filter driver makes the
 * flag-less re-open SUCCEED. A test cannot make one, so the re-open is fixtured with a tag no
 * driver owns (it fails there), and the identity pin that guards a SUCCESSFUL re-open is proved
 * by a race instead: _test_nofollow_identity_pin swaps PLAIN files over such an entry while it
 * opens, and only the pin can answer ERROR_STOPPED_ON_SYMLINK for a non-surrogate tag. The
 * attack itself swaps in a SYMLINK; that variant is not built - the pin compares identity
 * whatever was swapped in, and no machine this suite has run on could create the link. */

/*==============================================================================
 * MARK: - Macros and Constants
 *============================================================================*/
#define _CONTENT "nofollow\r\nbytes\x1A tail"
// How often a device row's wait checks whether the open has returned.
#define _DEVICE_POLL_MS 10
// How long a device row's open may take before it counts as BLOCKED - an open that never returns.
#define _DEVICE_WAIT_MS 10000
// Bytes for a pipe's name or path: the longest, "\\localhost\pipe\cfw_nofollow_unc_" plus a 10-digit id, is 44 and a terminator.
#define _PIPE_NAME_SIZE 64
// How many opens the identity-pin race gets to land one swap: about one in twenty does.
#define _RACE_ATTEMPTS_MAX 100000
// Bytes _reads_content reads into: more than _CONTENT, so a longer file shows as a size mismatch.
#define _READ_BUFFER_SIZE 64
#define _ROOT "file_handle_fixture"
#ifdef _WIN32
// Reparse tags no filter driver owns: a placeholder (not name-surrogate) and a name surrogate.
#define _TAG_PLACEHOLDER 0x00000ABCUL
#define _TAG_SURROGATE 0x20000ABCUL
#endif // _WIN32
// Bytes past a Str's size, so a row can prove the size - not a terminator - ends the name.
#define _TAIL "XYZ"
#ifdef _WIN32
// Room the Windows directory must leave in a MAX_PATH buffer for a 4-byte prefix (\\?\ or \??\),
// the "\win.ini" suffix and the terminator - 13 bytes, rounded up.
#define _WINDOWS_PATH_HEADROOM 16
#endif // _WIN32

/*==============================================================================
 * MARK: - Static/Internal Functions
 *============================================================================*/
// file_close aborts on a nullptr handle, and several rows here EXPECT nullptr.
static void _close_if_open(File **const file) {
    if (!memory_empty((void*) *file)) {
        file_close(file);
    }
}

// The thread's OS error state, read immediately after the call under test.
static I64 _last_error(void) {
#ifdef _WIN32
    return (I64) GetLastError();
#else
    return (I64) errno;
#endif // _WIN32
}

static void _remove_directory(char const *const path) {
#ifdef _WIN32
    // Removes a junction as the LINK, never its target's contents.
    RemoveDirectoryA(path);
#else
    rmdir(path);
#endif // _WIN32
}

static bool _write_fixture(char const *const path) {
    FILE *const handle = fopen(path, "wb");

    if (memory_empty((void*) handle)) {
        return false;
    }

    fwrite(_CONTENT, 1, CHAR_STATIC_SIZE(_CONTENT), handle);
    fclose(handle);

    return true;
}

/* The fixture is built under the OS temp directory, not beside the suite: this tree lives in a
 * Dropbox folder, whose sync client opens every new file and directory and so held the fixture
 * root delete-pending after teardown - the NEXT run then failed to recreate it. Paths stay
 * relative (the final-path rows need a relative path to resolve), so the process moves there. */
static bool _enter_scratch(void) {
#ifdef _WIN32
    char temp[MAX_PATH + CHAR_END_CHARACTER] = DEFAULT_INITIALIZATION;
    DWORD const size = GetTempPathA(sizeof(temp), temp);

    return size > 0 && size < sizeof(temp) && SetCurrentDirectoryA(temp) != 0;
#else
    char const *const temp = getenv("TMPDIR");

    return chdir(!memory_empty((void*) temp) && temp[0] != '\0' ? temp : "/tmp") == 0;
#endif // _WIN32
}

static void _fixture_teardown(void) {
    remove(_ROOT "/plain.txt");
    remove(_ROOT "/surrogate.txt");
    remove(_ROOT "/placeholder.txt");
    remove(_ROOT "/race.txt");
    remove(_ROOT "/swap.tmp");
    remove(_ROOT "/link.txt");
    remove(_ROOT "/fifo");
    remove(_ROOT "/dir/inner.txt");
    // A Linux symlink to a directory goes with remove(); a Windows junction with RemoveDirectoryA.
    remove(_ROOT "/dirlink");
    _remove_directory(_ROOT "/dirlink");
    _remove_directory(_ROOT "/dir");
#ifdef _WIN32
    _wremove(L"" _ROOT L"\\caf\u00e9.txt");
#else
    remove(_ROOT "/caf\xC3\xA9.txt");
#endif // _WIN32
    _remove_directory(_ROOT);
}

static bool _fixture_setup(void) {
    _fixture_teardown();

#ifdef _WIN32
    bool success = CreateDirectoryA(_ROOT, nullptr) && CreateDirectoryA(_ROOT "/dir", nullptr);
#else
    bool success = mkdir(_ROOT, 0700) == 0 && mkdir(_ROOT "/dir", 0700) == 0;
#endif // _WIN32

    success = success && _write_fixture(_ROOT "/plain.txt") && _write_fixture(_ROOT "/dir/inner.txt");

#ifdef _WIN32
    // Written through the WIDE API, so the name on disk is really U+00E9 and not a cp1252 twin.
    FILE *const accented = _wfopen(L"" _ROOT L"\\caf\u00e9.txt", L"wb");
#else
    FILE *const accented = fopen(_ROOT "/caf\xC3\xA9.txt", "wb");
#endif // _WIN32

    if (!memory_empty((void*) accented)) {
        fwrite(_CONTENT, 1, CHAR_STATIC_SIZE(_CONTENT), accented);
        fclose(accented);
    }

    return success && !memory_empty((void*) accented);
}

// Whether a link to a FILE could be made: false only on Windows without the privilege.
static bool _fixture_file_symlink(void) {
#ifdef _WIN32
    // 0x2 = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE: honoured under Developer Mode.
    return CreateSymbolicLinkA(_ROOT "/link.txt", "plain.txt", SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != 0;
#else
    return symlink("plain.txt", _ROOT "/link.txt") == 0;
#endif // _WIN32
}

// A link to the DIRECTORY: a junction on Windows (no privilege needed), a symlink on Linux.
static bool _fixture_directory_link(void) {
#ifdef _WIN32
    return system("mklink /J " _ROOT "\\dirlink " _ROOT "\\dir >NUL") == 0;
#else
    return symlink("dir", _ROOT "/dirlink") == 0;
#endif // _WIN32
}

#ifdef _WIN32
// Set TAG - a third-party reparse point, see _fixture_reparse_file - on the existing file PATH.
static bool _fixture_reparse_tag(char const *const path, DWORD const tag) {
    DWORD const share   = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    HANDLE const handle = CreateFileA(path, GENERIC_WRITE, share, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);

    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    REPARSE_GUID_DATA_BUFFER reparse = DEFAULT_INITIALIZATION;
    GUID const owner                 = { 0x6cf3a1d2, 0x5b7e, 0x4c11, { 0x9a, 0x3f, 0x0e, 0x21, 0x44, 0x7b, 0xc8, 0x55 } };

    reparse.ReparseTag        = tag;
    reparse.ReparseDataLength = 0;
    reparse.ReparseGuid       = owner;

    DWORD returned  = 0;
    bool const made = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, &reparse, REPARSE_GUID_DATA_BUFFER_HEADER_SIZE, nullptr, 0, &returned, nullptr) != 0;

    CloseHandle(handle);

    return made;
}

/* A third-party reparse point on a REGULAR file - which, unlike a symlink, needs no privilege.
 * TAG picks the case. With the name-surrogate bit (0x20000000) IsReparseTagNameSurrogate calls it
 * a LINK: the exact branch the symlink row covers, reachable on an unprivileged account. Without
 * the bit it stands in for a cloud placeholder: no filter driver owns the tag, so the flag-less
 * RE-OPEN the opener makes fails with ERROR_CANT_ACCESS_FILE - while the flagged open succeeds -
 * which proves the re-open branch ran. (A real placeholder's driver would serve the data there.) */
static bool _fixture_reparse_file(char const *const path, DWORD const tag) {
    return _write_fixture(path) && _fixture_reparse_tag(path, tag);
}

// The identity-pin race's shared state: the racer runs until the opener sets STOP.
typedef struct {
    _Atomic bool stop;
} SwapRace;

/* The racer: renames a fresh PLAIN file over race.txt - a new file ID under the same name - then
 * tags the newcomer in place, so the entry the opener sees keeps flipping between "non-surrogate
 * reparse point" (sent to the re-open) and "plain file" with a different identity. */
static void* _race_swap(void *const data) {
    SwapRace *const race = (SwapRace*) data;

    while (!race->stop) {
        if (_write_fixture(_ROOT "/swap.tmp")) {
            MoveFileExA(_ROOT "/swap.tmp", _ROOT "/race.txt", MOVEFILE_REPLACE_EXISTING);
        }

        _fixture_reparse_tag(_ROOT "/race.txt", _TAG_PLACEHOLDER);
    }

    return nullptr;
}

/* One device row, opened on a worker thread so an open that BLOCKS (a console, a pipe) fails the
 * row instead of hanging the suite. Rows live in static storage: a worker that never returns
 * still writes into its row after the case has ended. */
typedef struct {
    char const *path;
    File *file;
    I64 error;
    _Atomic bool done;
} DeviceOpen;

static void* _device_open(void *const data) {
    DeviceOpen *const row = (DeviceOpen*) data;

    row->file  = file_open_nofollow_try_1(row->path);
    row->error = _last_error();
    row->done  = true;

    return nullptr;
}

// Whether ROW's open returned within _DEVICE_WAIT_MS. Joined only then: a blocked worker is left
// behind, and the process exit ends it.
static bool _device_open_returns(DeviceOpen *const row) {
    Thread worker = DEFAULT_INITIALIZATION;

    if (result_is_error(thread_create_1(&worker, _device_open, row))) {
        return false;
    }

    for (USize waited = 0; !row->done && waited < _DEVICE_WAIT_MS; waited += _DEVICE_POLL_MS) {
        Sleep(_DEVICE_POLL_MS);
    }

    if (!row->done) {
        return false;
    }

    thread_join_1(&worker);

    return true;
}

// Run ROW and pin the refusal: it returns, with nullptr and ERROR_INVALID_HANDLE.
static void _expect_device_refused(Test *const test, DeviceOpen *const row) {
    if (!test_expect_true(test, row->path, _device_open_returns(row))) {
        log_message_1(LOG_LEVEL_WARN, "BLOCKED: the open above did not return within the wait - the device was opened.\n");

        return;
    }

    test_expect_null(test, "-> nullptr", row->file);
    test_expect_i(test, "with ERROR_INVALID_HANDLE (the documented device code)", ERROR_INVALID_HANDLE, row->error);

    _close_if_open(&row->file);
}

// Serve a LIVE pipe named PIPE_NAME, run ROW against it, and pin that the refusal came BEFORE the
// open: the pipe's server never saw a client.
static void _expect_pipe_refused(Test *const test, DeviceOpen *const row, char const *const pipe_name) {
    HANDLE const server = CreateNamedPipeA(pipe_name, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_NOWAIT, 1, 0, 0, 0, nullptr);

    if (!test_expect_true(test, "the pipe server is made", server != INVALID_HANDLE_VALUE)) {
        return;
    }

    _expect_device_refused(test, row);

    // PIPE_NOWAIT: ERROR_PIPE_LISTENING means no client ever connected; one that connected and
    // closed - the old open-then-refuse - answers ERROR_NO_DATA instead, one still connected
    // ERROR_PIPE_CONNECTED. A call that succeeds outright is a client too, read as ERROR_SUCCESS.
    DWORD const state = ConnectNamedPipe(server, nullptr) != 0 ? ERROR_SUCCESS : GetLastError();

    test_expect_i(test, "and the pipe's server never saw a client (ERROR_PIPE_LISTENING)", ERROR_PIPE_LISTENING, state);

    CloseHandle(server);
}
#endif // _WIN32

// Whether FILE reads exactly _CONTENT - binary, so the CR and the 0x1A survive.
static bool _reads_content(File *const file) {
    char data[_READ_BUFFER_SIZE] = DEFAULT_INITIALIZATION;
    USize const read             = file_read_1(file, data, 1, sizeof(data));

    return read == CHAR_STATIC_SIZE(_CONTENT) && char_equal_2(data, read, _CONTENT, CHAR_STATIC_SIZE(_CONTENT));
}

/*==============================================================================
 * MARK: - Test Cases
 *============================================================================*/
static void _test_nofollow_regular(Test *const test) {
    test_case_begin(test, "file_open_nofollow_try_1/2/3: a regular file opens and reads the same bytes as file_open_try_1");

    File *plain = file_open_try_1(_ROOT "/plain.txt", "rb");
    File *file  = file_open_nofollow_try_1(_ROOT "/plain.txt");

    test_expect_not_null(test, "file_open_try_1 opens the fixture", plain);
    test_expect_not_null(test, "nofollow_try_1 opens it too", file);

    if (!memory_empty((void*) plain) && !memory_empty((void*) file)) {
        test_expect_true(test, "file_open_try_1 reads the exact bytes", _reads_content(plain));
        test_expect_true(test, "nofollow reads the same exact bytes (binary: CR and 0x1A survive)", _reads_content(file));
    }

    _close_if_open(&plain);
    _close_if_open(&file);

    // A Str that is NOT terminated at its size: the open must name the first `size` bytes only.
    char spelled[] = _ROOT "/plain.txt" _TAIL;
    Str const view = str_init_3(spelled, CHAR_STATIC_SIZE(spelled) - CHAR_STATIC_SIZE(_TAIL));

    file = file_open_nofollow_try_2(&view);

    test_expect_not_null(test, "nofollow_try_2 honours the Str size, not a terminator", file);

    if (!memory_empty((void*) file)) {
        test_expect_true(test, "and reads the fixture", _reads_content(file));
    }

    _close_if_open(&file);

    String path_string = string_init_static(_ROOT "/plain.txt", CHAR_STATIC_SIZE(_ROOT "/plain.txt"));

    file = file_open_nofollow_try_3(&path_string);

    test_expect_not_null(test, "nofollow_try_3 opens it", file);

    _close_if_open(&file);
    string_uninit(&path_string);

    test_case_end(test);
}

static void _test_nofollow_refusals(Test *const test) {
    test_case_begin(test, "file_open_nofollow_try_*: missing, directory, empty and embedded-NUL paths are refused by value");

#ifdef _WIN32
    I64 const not_found     = ERROR_FILE_NOT_FOUND;
    I64 const directory     = ERROR_ACCESS_DENIED;
    I64 const embedded_null = ERROR_INVALID_NAME;
#else
    I64 const not_found     = ENOENT;
    I64 const directory     = EISDIR;
    I64 const embedded_null = EINVAL;
#endif // _WIN32

    File *file = file_open_nofollow_try_1(_ROOT "/missing.txt");
    I64 error  = _last_error();

    test_expect_null(test, "a missing file -> nullptr", file);
    test_expect_i(test, "and the OS's not-found code is preserved", not_found, error);

    file  = file_open_nofollow_try_1(_ROOT "/dir");
    error = _last_error();

    test_expect_null(test, "a directory -> nullptr", file);
    test_expect_i(test, "with the documented directory code", directory, error);

    Str const empty_str       = str_init_1();
    String const empty_string = string_init_1();

    test_expect_null(test, "an empty Str -> nullptr (no syscall)", file_open_nofollow_try_2(&empty_str));
    test_expect_null(test, "an empty String -> nullptr (no syscall)", file_open_nofollow_try_3(&empty_string));

    // "<root>/plain.txt\0x": the OS would stop at the NUL and open plain.txt - a name never passed.
    char embedded[] = _ROOT "/plain.txt\0x";
    Str const view  = str_init_3(embedded, CHAR_STATIC_SIZE(embedded));

    file  = file_open_nofollow_try_2(&view);
    error = _last_error();

    test_expect_null(test, "a Str with an embedded NUL -> nullptr, not the shorter name", file);
    test_expect_i(test, "with the documented embedded-NUL code", embedded_null, error);

    test_case_end(test);
}

static void _test_nofollow_links(Test *const test) {
    test_case_begin(test, "file_open_nofollow_try_1: a final-component link is refused; a linked ANCESTOR is followed");

#ifdef _WIN32
    I64 const link_code = ERROR_STOPPED_ON_SYMLINK;
#else
    I64 const link_code = ELOOP;
#endif // _WIN32

    if (_fixture_file_symlink()) {
        File *followed = file_open_try_1(_ROOT "/link.txt", "rb");

        test_expect_not_null(test, "the symlink is live: file_open_try_1 follows it", followed);

        _close_if_open(&followed);

        File *file      = file_open_nofollow_try_1(_ROOT "/link.txt");
        I64 const error = _last_error();

        test_expect_null(test, "a symlink to a regular file -> nullptr", file);
        test_expect_i(test, "with the documented link code", link_code, error);

        _close_if_open(&file);
    }
    else {
        log_message_1(LOG_LEVEL_WARN, "SKIPPED: nofollow symlink row - CreateSymbolicLinkA failed (no SeCreateSymbolicLinkPrivilege on this"
                                      " account/machine); this pin needs a real file symlink to refuse.\n");
    }

#ifdef _WIN32
    // The surrogate rule itself, on every account: see _fixture_reparse_file.
    if (test_expect_true(test, "the name-surrogate reparse fixture is made", _fixture_reparse_file(_ROOT "/surrogate.txt", _TAG_SURROGATE))) {
        File *file      = file_open_nofollow_try_1(_ROOT "/surrogate.txt");
        I64 const error = _last_error();

        test_expect_null(test, "a name-surrogate reparse point on a FILE -> nullptr", file);
        test_expect_i(test, "refused AS A LINK (ERROR_STOPPED_ON_SYMLINK), not by a failed re-open", link_code, error);

        _close_if_open(&file);
    }

    if (test_expect_true(test, "the non-surrogate reparse fixture is made", _fixture_reparse_file(_ROOT "/placeholder.txt", _TAG_PLACEHOLDER))) {
        File *file      = file_open_nofollow_try_1(_ROOT "/placeholder.txt");
        I64 const error = _last_error();

        test_expect_null(test, "a non-surrogate reparse point with no filter driver -> nullptr", file);
        test_expect_i(test, "from the flag-less RE-OPEN (ERROR_CANT_ACCESS_FILE): not refused as a link", ERROR_CANT_ACCESS_FILE, error);
        // The identity pin runs only after a re-open SUCCEEDS, so this row cannot show it firing;
        // it shows the pin is not consulted - and cannot misfire - on the re-open's own failure.
        test_expect_true(test, "the identity pin did not refuse an entry nobody swapped", error != ERROR_STOPPED_ON_SYMLINK);

        _close_if_open(&file);
    }
#endif // _WIN32

    if (!test_expect_true(test, "the directory link fixture is made", _fixture_directory_link())) {
        test_case_end(test);

        return;
    }

    File *file          = file_open_nofollow_try_1(_ROOT "/dirlink");
    I64 const dir_error = _last_error();

    test_expect_null(test, "a link to a directory as the final component -> nullptr", file);
#ifdef _WIN32
    /* A junction IS a directory: without BACKUP_SEMANTICS CreateFile2 refuses it as one before the
     * reparse tag is ever read, so it answers the directory's code, not the link's. */
    test_expect_i(test, "a junction answers ERROR_ACCESS_DENIED (the directory refusal), not ERROR_STOPPED_ON_SYMLINK", ERROR_ACCESS_DENIED, dir_error);
#else
    // O_NOFOLLOW refuses the final-component link before the target's type is looked at.
    test_expect_i(test, "a symlink to a directory answers ELOOP (the link refusal), not EISDIR", ELOOP, dir_error);
#endif // _WIN32

    _close_if_open(&file);

    // Only the FINAL component is bound by the flag: the ancestor is followed, and the handle's
    // final path is where a caller sees that.
    file = file_open_nofollow_try_1(_ROOT "/dirlink/inner.txt");

    test_expect_not_null(test, "through a linked ancestor the open succeeds", file);

    if (!memory_empty((void*) file)) {
        char through[1024] = DEFAULT_INITIALIZATION;
        char target[1024]  = DEFAULT_INITIALIZATION;

        test_expect_true(test, "file_get_final_path answers", file_get_final_path(file, through, sizeof(through)));
        test_expect_true(test, "file_final_path_1 of the real path answers", file_final_path_1(_ROOT "/dir/inner.txt", target, sizeof(target)));
        test_expect_string(test, "the handle names the TARGET tree, not the link", target, through);
        test_expect_false(test, "and the link's own name is nowhere in it", char_find_exists_1(through, "dirlink"));

        char by_path[1024] = DEFAULT_INITIALIZATION;

        test_expect_true(test, "file_final_path_1 THROUGH the link answers", file_final_path_1(_ROOT "/dirlink/inner.txt", by_path, sizeof(by_path)));
        test_expect_string(test, "and resolves it to the same target path", target, by_path);
    }

    _close_if_open(&file);

    test_case_end(test);
}

static void _test_nofollow_identity_pin(Test *const test) {
    test_case_begin(test, "file_open_nofollow_try_1: an entry swapped between the reparse open and the re-open is refused by the identity pin");

#ifdef _WIN32
    if (!test_expect_true(test, "the racing fixture is made", _fixture_reparse_file(_ROOT "/race.txt", _TAG_PLACEHOLDER))) {
        test_case_end(test);

        return;
    }

    SwapRace race = DEFAULT_INITIALIZATION;
    Thread racer  = DEFAULT_INITIALIZATION;

    if (!test_expect_true(test, "the racer thread starts", !result_is_error(thread_create_1(&racer, _race_swap, &race)))) {
        test_case_end(test);

        return;
    }

    USize refused = 0;

    for (USize attempt = 0; refused == 0 && attempt < _RACE_ATTEMPTS_MAX; attempt += 1) {
        File *file      = file_open_nofollow_try_1(_ROOT "/race.txt");
        I64 const error = _last_error();

        if (memory_empty((void*) file) && error == ERROR_STOPPED_ON_SYMLINK) {
            refused += 1;
        }

        _close_if_open(&file);
    }

    race.stop = true;

    thread_join_1(&racer);

    // A tag without the name-surrogate bit is never refused AS A LINK by the tag check, so this
    // code can only come from the pin: the re-open reached a file with another identity. With
    // the pin removed those attempts OPEN instead - verified by removing it.
    test_expect_true(test, "a swap that landed between the two opens was refused (ERROR_STOPPED_ON_SYMLINK)", refused > 0);
#else
    log_message_1(LOG_LEVEL_WARN, "SKIPPED: identity-pin race row - Linux opens once with O_NOFOLLOW; there is no re-open to pin.\n");
#endif // _WIN32

    test_case_end(test);
}

static void _test_nofollow_fifo(Test *const test) {
    test_case_begin(test, "file_open_nofollow_try_1: a FIFO with no writer is refused and the call RETURNS");

#ifdef _WIN32
    log_message_1(LOG_LEVEL_WARN, "SKIPPED: nofollow FIFO row - Windows has no FIFO in the filesystem namespace.\n");
#else
    if (test_expect_true(test, "the FIFO fixture is made", mkfifo(_ROOT "/fifo", 0600) == 0)) {
        // WATCHDOG: a regression that drops O_NONBLOCK blocks this open forever with no writer.
        // SIGALRM's default action ends the process, failing the suite instead of hanging it.
        alarm(10);

        File *const file = file_open_nofollow_try_1(_ROOT "/fifo");
        I64 const error  = _last_error();

        alarm(0);

        test_expect_null(test, "a FIFO -> nullptr", file);
        test_expect_i(test, "with ENXIO (not a regular file)", ENXIO, error);
    }
#endif // _WIN32

    test_case_end(test);
}

static void _test_nofollow_devices(Test *const test) {
    test_case_begin(test, "file_open_nofollow_try_1: a Windows device or UNC path is refused on its spelling, BEFORE any open, and the call RETURNS");

#ifdef _WIN32
    // Each row's name opens a device, a pipe or the device namespace - never the file it seems.
    static DeviceOpen console     = { .path = "CONIN$" };
    static DeviceOpen extension   = { .path = "con.txt" };
    static DeviceOpen trimmed     = { .path = "sub/AUX .html" };
    static DeviceOpen drive       = { .path = "C:nul" };
    static DeviceOpen trailing    = { .path = "sub\\nul\\" };
    static DeviceOpen device      = { .path = "\\\\.\\NUL" };
    static DeviceOpen absent_pipe = { .path = "//./pipe/x" };
    static DeviceOpen verbatim    = DEFAULT_INITIALIZATION;
    static DeviceOpen passthrough = DEFAULT_INITIALIZATION;
    static DeviceOpen live        = DEFAULT_INITIALIZATION;

    _expect_device_refused(test, &console);
    _expect_device_refused(test, &extension);
    _expect_device_refused(test, &trimmed);
    _expect_device_refused(test, &drive);
    _expect_device_refused(test, &trailing);
    _expect_device_refused(test, &device);
    _expect_device_refused(test, &absent_pipe);

    // A UNC path - two leading separators, any mix of '\' and '/' - is refused before the open
    // would reach the named host (SMB, NTLM) or, through the loopback, a local named pipe.
    static DeviceOpen unc_localhost = { .path = "\\\\localhost\\pipe\\x" };
    static DeviceOpen unc_ipv4      = { .path = "\\\\127.0.0.1\\pipe\\x" };
    static DeviceOpen unc_slashed   = { .path = "//localhost/pipe/x" };
    static DeviceOpen unc_ipv6      = { .path = "\\\\[::1]\\pipe\\x" };
    static DeviceOpen unc_mixed     = { .path = "\\/localhost/share/f" };
    static DeviceOpen unc_mixed_too = { .path = "/\\server\\share\\f" };
    static DeviceOpen unc_share     = { .path = "\\\\server\\share\\f.txt" };
    static DeviceOpen live_unc      = DEFAULT_INITIALIZATION;

    _expect_device_refused(test, &unc_localhost);
    _expect_device_refused(test, &unc_ipv4);
    _expect_device_refused(test, &unc_slashed);
    _expect_device_refused(test, &unc_ipv6);
    _expect_device_refused(test, &unc_mixed);
    _expect_device_refused(test, &unc_mixed_too);
    _expect_device_refused(test, &unc_share);

    // The prefix is refused even for a REAL file: proved real by its plain spelling opening.
    static char verbatim_path[MAX_PATH]    = DEFAULT_INITIALIZATION;
    static char passthrough_path[MAX_PATH] = DEFAULT_INITIALIZATION;

    char windows[MAX_PATH]  = DEFAULT_INITIALIZATION;
    UINT const windows_size = GetWindowsDirectoryA(windows, sizeof(windows));

    if (test_expect_true(test, "the Windows directory is known", windows_size > 0 && windows_size < sizeof(windows) - _WINDOWS_PATH_HEADROOM)) {
        char plain_path[MAX_PATH] = DEFAULT_INITIALIZATION;

        snprintf(plain_path, sizeof(plain_path), "%s\\win.ini", windows);
        snprintf(verbatim_path, sizeof(verbatim_path), "\\\\?\\%s\\win.ini", windows);
        snprintf(passthrough_path, sizeof(passthrough_path), "\\??\\%s\\win.ini", windows);

        File *plain = file_open_nofollow_try_1(plain_path);

        test_expect_not_null(test, "win.ini opens by its plain spelling", plain);

        _close_if_open(&plain);

        verbatim.path    = verbatim_path;
        passthrough.path = passthrough_path;

        _expect_device_refused(test, &verbatim);
        _expect_device_refused(test, &passthrough);
    }

    // A LIVE pipe, spelled with '/' and then reached through the loopback (where SMB would carry
    // the open to it): refused before the open, so its server never sees a client.
    static char pipe_path[_PIPE_NAME_SIZE]     = DEFAULT_INITIALIZATION;
    static char unc_pipe_path[_PIPE_NAME_SIZE] = DEFAULT_INITIALIZATION;

    char pipe_name[_PIPE_NAME_SIZE]     = DEFAULT_INITIALIZATION;
    char unc_pipe_name[_PIPE_NAME_SIZE] = DEFAULT_INITIALIZATION;

    snprintf(pipe_name, sizeof(pipe_name), "\\\\.\\pipe\\cfw_nofollow_%lu", GetCurrentProcessId());
    snprintf(pipe_path, sizeof(pipe_path), "//./pipe/cfw_nofollow_%lu", GetCurrentProcessId());
    snprintf(unc_pipe_name, sizeof(unc_pipe_name), "\\\\.\\pipe\\cfw_nofollow_unc_%lu", GetCurrentProcessId());
    snprintf(unc_pipe_path, sizeof(unc_pipe_path), "\\\\localhost\\pipe\\cfw_nofollow_unc_%lu", GetCurrentProcessId());

    live.path     = pipe_path;
    live_unc.path = unc_pipe_path;

    _expect_pipe_refused(test, &live, pipe_name);
    _expect_pipe_refused(test, &live_unc, unc_pipe_name);

    // The basename split reads '\\' too, and does not misfire on a real file.
    File *file = file_open_nofollow_try_1(_ROOT "\\plain.txt");

    test_expect_not_null(test, "a regular file, '\\'-separated, still opens", file);

    _close_if_open(&file);
#else
    log_message_1(LOG_LEVEL_WARN, "SKIPPED: device and UNC spelling rows - Linux has no device names and '//' is an ordinary path; O_NONBLOCK + fstat refuse a device.\n");
#endif // _WIN32

    test_case_end(test);
}

static void _test_nofollow_encoding(Test *const test) {
    test_case_begin(test, "file_open_nofollow_try_1: UTF-8 paths - a non-ASCII name opens, invalid UTF-8 is refused (Windows)");

    char const *const accented = _ROOT "/caf\xC3\xA9.txt";
    File *file                 = file_open_nofollow_try_1(accented);

    test_expect_not_null(test, "the UTF-8 name 'caf\xC3\xA9.txt' opens", file);

    if (!memory_empty((void*) file)) {
        test_expect_true(test, "and reads the fixture", _reads_content(file));

        char final_path[1024] = DEFAULT_INITIALIZATION;

        test_expect_true(test, "file_get_final_path answers for it", file_get_final_path(file, final_path, sizeof(final_path)));
        test_expect_true(test, "and spells the name back in UTF-8", char_ends_with_1(final_path, "caf\xC3\xA9.txt"));
    }

    _close_if_open(&file);

#ifdef _WIN32
    // The asymmetry the wide opener exists for: the CRT's fopen reads the bytes in the code page.
    if (GetACP() != CP_UTF8) {
        File *ansi = file_open_try_1(accented, "rb");

        test_expect_null(test, "file_open_try_1 cannot reach the same name under a non-UTF-8 code page", ansi);

        _close_if_open(&ansi);
    }
    else {
        log_message_1(LOG_LEVEL_WARN, "SKIPPED: ANSI asymmetry row - the process code page is UTF-8, so file_open_try_1 reaches it too.\n");
    }

    file            = file_open_nofollow_try_1(_ROOT "/bad\xFF.txt");
    I64 const error = _last_error();

    test_expect_null(test, "invalid UTF-8 -> nullptr", file);
    test_expect_i(test, "with ERROR_NO_UNICODE_TRANSLATION", ERROR_NO_UNICODE_TRANSLATION, error);
#else
    log_message_1(LOG_LEVEL_WARN, "SKIPPED: invalid-UTF-8 row - Linux paths are bytes; there is nothing to refuse.\n");
#endif // _WIN32

    test_case_end(test);
}

static void _test_final_path(Test *const test) {
    test_case_begin(test, "file_final_path_1/2/3 and file_get_final_path: absolute, no \\\\?\\, never truncated");

    char by_path[1024]   = DEFAULT_INITIALIZATION;
    char by_handle[1024] = DEFAULT_INITIALIZATION;

    test_expect_true(test, "final_path_1 resolves a relative path", file_final_path_1(_ROOT "/plain.txt", by_path, sizeof(by_path)));
#ifdef _WIN32
    test_expect_true(test, "to an absolute one (drive or UNC)", by_path[1] == ':' || char_starts_with_1(by_path, "\\\\"));
#else
    test_expect_true(test, "to an absolute one", by_path[0] == '/');
#endif // _WIN32
    test_expect_false(test, "with no \\\\?\\ prefix", char_find_exists_1(by_path, "\\\\?\\"));
    test_expect_true(test, "ending in the file's name", char_ends_with_1(by_path, "plain.txt"));

    File *file = file_open_nofollow_try_1(_ROOT "/plain.txt");

    test_expect_not_null(test, "the fixture opens", file);

    if (!memory_empty((void*) file)) {
        test_expect_true(test, "get_final_path answers", file_get_final_path(file, by_handle, sizeof(by_handle)));
        test_expect_string(test, "get_final_path == final_path_1 of the same plain path", by_path, by_handle);

        char small[4] = { 'X', 'X', 'X', 'X' };

        test_expect_false(test, "get_final_path into 4 bytes -> false", file_get_final_path(file, small, sizeof(small)));
        test_expect_true(test, "and the buffer is EMPTY, not truncated", small[0] == '\0');

        small[0] = 'X';

        test_expect_false(test, "capacity 0 -> false", file_get_final_path(file, small, 0));
        test_expect_true(test, "and the buffer is untouched", small[0] == 'X');
    }

    _close_if_open(&file);

    char small[4] = { 'X', 'X', 'X', 'X' };

    test_expect_false(test, "final_path_1 into 4 bytes -> false", file_final_path_1(_ROOT "/plain.txt", small, sizeof(small)));
    test_expect_true(test, "and the buffer is EMPTY, not truncated", small[0] == '\0');

    small[0] = 'X';

    test_expect_false(test, "final_path_1 of a missing path -> false", file_final_path_1(_ROOT "/missing.txt", small, sizeof(small)));
    test_expect_true(test, "and the buffer is EMPTY", small[0] == '\0');

    char directory[1024] = DEFAULT_INITIALIZATION;

    test_expect_true(test, "final_path_1 of a DIRECTORY answers (a root is asked this way)", file_final_path_1(_ROOT, directory, sizeof(directory)));
    test_expect_true(test, "and the file's final path lives under it", char_starts_with_1(by_path, directory));

    char spelled[]    = _ROOT "/plain.txt" _TAIL;
    Str const view    = str_init_3(spelled, CHAR_STATIC_SIZE(spelled) - CHAR_STATIC_SIZE(_TAIL));
    char by_str[1024] = DEFAULT_INITIALIZATION;

    test_expect_true(test, "final_path_2 honours the Str size", file_final_path_2(&view, by_str, sizeof(by_str)));
    test_expect_string(test, "final_path_2 == final_path_1", by_path, by_str);

    String path_string   = string_init_static(_ROOT "/plain.txt", CHAR_STATIC_SIZE(_ROOT "/plain.txt"));
    char by_string[1024] = DEFAULT_INITIALIZATION;

    test_expect_true(test, "final_path_3 answers", file_final_path_3(&path_string, by_string, sizeof(by_string)));
    test_expect_string(test, "final_path_3 == final_path_1", by_path, by_string);

    string_uninit(&path_string);

    Str const empty_str = str_init_1();

    small[0] = 'X';

    test_expect_false(test, "final_path_2 of an empty Str -> false", file_final_path_2(&empty_str, small, sizeof(small)));
    test_expect_true(test, "and the buffer is EMPTY", small[0] == '\0');

    test_case_end(test);
}

static void _test_get_modified(Test *const test) {
    test_case_begin(test, "file_get_modified == file_modified_1 for the same file");

    I64 by_path   = 0;
    I64 by_handle = -1;

    test_expect_true(test, "file_modified_1 answers", file_modified_1(_ROOT "/plain.txt", &by_path));

    File *file = file_open_nofollow_try_1(_ROOT "/plain.txt");

    test_expect_not_null(test, "the fixture opens", file);

    if (!memory_empty((void*) file)) {
        test_expect_true(test, "file_get_modified answers", file_get_modified(file, &by_handle));
        test_expect_i(test, "by handle == by path", by_path, by_handle);
        test_expect_true(test, "and is a plausible epoch (after 2020)", by_handle > 1577836800);
    }

    _close_if_open(&file);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Entry Point
 *============================================================================*/
int main(void) {
    LogConfig const log_config = {
        .level             = LOG_LEVEL_WARN,
        .stream            = LOG_STREAM_STDOUT,
        .timestamp_enabled = false,
        .autoflush         = true
    };

    log_init(log_config);

    Test test = test_init("tests/file/test_file_handle.c");

    test_suite_begin(&test, "file handle");

    test_case_begin(&test, "fixture: a plain file, a directory, and a non-ASCII name");

    bool const ready = test_expect_true(&test, "the scratch directory is entered", _enter_scratch()) &&
        test_expect_true(&test, "the fixture tree is built", _fixture_setup());

    test_case_end(&test);

    if (ready) {
        _test_nofollow_regular(&test);
        _test_nofollow_refusals(&test);
        _test_nofollow_links(&test);
        _test_nofollow_identity_pin(&test);
        _test_nofollow_fifo(&test);
        _test_nofollow_devices(&test);
        _test_nofollow_encoding(&test);
        _test_final_path(&test);
        _test_get_modified(&test);
    }

    _fixture_teardown();

    test_suite_end(&test);

    return test_uninit(&test);
}