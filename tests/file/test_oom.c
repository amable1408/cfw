#ifdef _WIN32
#include <platform/windows/windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#endif // _WIN32

#include <char/char.h>
#include <container/str/str.h>
#include <container/string/string.h>
#include <file/file.h>
#include <log/log.h>
#include <memory/memory.h>
#include <result.h>
#include <test/test.h>

/* Fault injection for file's documented out-of-memory contracts (file.h 0.6.0).
 *
 * WHY A SEPARATE BINARY. A recover contract ("returns MEMORY / an empty buffer when the
 * allocation fails") never executes in an ordinary suite - the allocation never fails. This
 * binary is linked with -Wl,--wrap=calloc (the CFW seam: memory_try_alloc and memory_alloc both
 * call calloc) so a chosen allocation can be refused, and with --wrap=free, --wrap=malloc and
 * --wrap=realloc so a ledger can say which blocks a failure path released and which it abandoned.
 * It also wraps memory_alloc - COUNTED, never refused: in this build the aborting twin returns
 * nullptr exactly like memory_try_alloc, so a recover path that regressed to it would pass every
 * refusal below while aborting in production. Only a call count sees that regression.
 *
 * THE MEASURED PROFILE (counting on, failure off - Windows/MinGW and Debian 13/glibc agree except
 * for the pid digits inside file_replace_1's temp name):
 *   - file_open_nofollow_try_1/2/3, file_final_path_1/2/3, file_get_final_path,
 *     file_get_modified, file_open_try_1/2/3, file_open_wait_1, file_remove_1 (ASCII): ZERO
 *     allocations, success or refusal. The Windows wide path is a FILE_PATH_WIDE_MAX stack
 *     buffer, not an allocation - "none of them allocates" is the contract pinned below, under
 *     a wrapper that refuses EVERY calloc.
 *   - file_copy_1: one 64 KiB copy buffer (memory_try_alloc - recover).
 *   - file_replace_1: the temp name (char_join_1 -> memory_alloc - abort in production) and the
 *     64 KiB buffer (recover).
 *   - file_read_to_char / file_read_to_char_wait / file_read_all_1: one content block of
 *     size + 1 (memory_try_alloc - recover to an empty buffer), with a 1-byte memory_alloc
 *     fallback (abort in production).
 *   - file_read_to_string / _str / file_align_read_to_string / file_read_all_2 / _3: one
 *     content block through string_init_2 -> memory_alloc (abort in production).
 *   - every _2/_3 wrapper: one char_new_3 copy per path (memory_alloc - abort in production;
 *     file.h's "ONE EXCEPTION" paragraph documents it).
 * Each case re-asserts its row of this profile as its positive anchor, so a new allocation makes
 * the case fail instead of silently moving the boundary under test.
 *
 * DOCUMENTED ABORT CONTRACTS. They are only live under ERROR_CHECK_ENABLED, which this binary
 * refuses (see the #error). On Linux each is probed in a forked child whose memory_alloc wrapper
 * EMULATES the checked build - a refused allocation reaching the aborting twin calls abort() -
 * with an unarmed control child first, so the death is attributable to the injection. On
 * Windows there is no fork: the String readers would dereference the nullptr the twin returns
 * here and take the harness with them, so those probes print a SKIPPED line instead; the _2/_3
 * wrapper sites are still covered in-process by the twin count in the degradation section.
 *
 * SINGLE-THREADED BY REQUIREMENT. The ledger and the injector are plain statics; nothing linked
 * here spawns a thread. A binary that grows one needs a ThreadMutex around the ledger.
 *
 * RUNNING: from tests/file (.\oom.exe). The fixtures live in cfw_file_oom/ under the system temp
 * directory (the current directory when that cannot be entered) and are removed at the end. */

/* With ERROR_CHECK_ENABLED memory_alloc aborts instead of returning nullptr and every
 * error_check_* in the linked modules is armed, so an injected failure would end in an abort
 * before any recover path could be observed - and the degradation rows below would never run.
 * Refused outright rather than trusting a reader to notice. */
#ifdef ERROR_CHECK_ENABLED
#error "test_oom.c must be built WITHOUT ERROR_CHECK_ENABLED: with it, memory_alloc aborts instead of returning nullptr and the recover paths under test can never run."
#endif // ERROR_CHECK_ENABLED

/*==============================================================================
 * MARK: - Macros and Constants
 *============================================================================*/
// Mirrors file.c's private _FILE_COPY_BUFFER_BYTES. Every case arming it first asserts a record
// of exactly this size in its unarmed anchor, so a change to the constant fails loudly here.
#define _TEST_COPY_BUFFER_BYTES ((USize) 64 * 1024)
#define _TEST_DESCRIPTOR_LIMIT 1024
#define _TEST_DIRECTORY "cfw_file_oom"
#define _TEST_EPOCH_SECONDS 1000000000
#define _TEST_EXIT_ARMED_DEFECT 111
#define _TEST_LABEL_SIZE 192
#define _TEST_LEDGER_CAPACITY 4096
// Name buffers: every generated fixture name is shorter than this.
#define _TEST_NAME_SIZE 64
#define _TEST_PATH_SIZE 4096
#define _TEST_RAW_READER_INDEX 24
#define _TEST_TWIN_CAPACITY 1024

/* Every case owns its block sizes, so a block leaked by one case cannot surface in another and
 * point at the wrong code. CONTENT sizes are file lengths (the reader allocates size + 1); NAME
 * sizes are fixture-name lengths (char_new_3 allocates length + 1). All pairwise distinct once
 * the + 1 is applied, and none equals the copy buffer. */
#define _TEST_CONTENT_COPY 89
#define _TEST_CONTENT_PROBE 71
#define _TEST_CONTENT_READ_HANDLE 53
#define _TEST_CONTENT_READ_PATH 41
#define _TEST_CONTENT_READ_WAIT 67
#define _TEST_CONTENT_REPLACE_NEW 97
#define _TEST_CONTENT_REPLACE_OLD 43
#define _TEST_CONTENT_WRAPPER 12
#define _TEST_NAME_COPY2_DESTINATION 10
#define _TEST_NAME_COPY2_SOURCE 18
#define _TEST_NAME_COPY3_DESTINATION 23
#define _TEST_NAME_COPY3_SOURCE 6
#define _TEST_NAME_REMOVE2 14
#define _TEST_NAME_REMOVE3 21
#define _TEST_NAME_RENAME2_DESTINATION 9
#define _TEST_NAME_RENAME2_SOURCE 16
#define _TEST_NAME_RENAME3_DESTINATION 19
#define _TEST_NAME_RENAME3_SOURCE 8
#define _TEST_NAME_REPLACE1_DESTINATION 38
#define _TEST_NAME_REPLACE2_DESTINATION 31
#define _TEST_NAME_REPLACE2_SOURCE 29
#define _TEST_NAME_REPLACE3_DESTINATION 36
#define _TEST_NAME_REPLACE3_SOURCE 34
#define _TEST_NAME_SET_MODIFIED2 25
#define _TEST_NAME_SET_MODIFIED3 27
// The abort probes' fixtures, and file_align_read_to_string's vector width there.
#define _TEST_PROBE_COPY_DESTINATION "probe_copy_destination.txt"
#define _TEST_PROBE_PADDING 32
#define _TEST_PROBE_REMOVE "probe_remove.txt"
#define _TEST_PROBE_REPLACE_DESTINATION "probe_replace.txt"
#define _TEST_PROBE_SOURCE "probe_source.txt"

/*==============================================================================
 * MARK: - Allocation Ledger
 *============================================================================*/

/* Every allocation is recorded and every free matched against the record, so a case can ask
 * "which blocks did this call leave live" rather than only "did it crash". A WINDOW is a
 * snapshot of the counters before a call; the *_since queries count only what the call did, so
 * an earlier case's leftovers can never pass or fail a later one. */

typedef struct LedgerEntry {
    void *pointer; /**< Block address. */
    USize size;    /**< Bytes requested. */
    bool freed;    /**< true once released; the entry is kept so a second free is visible. */
} LedgerEntry;

typedef struct Window {
    USize mark;    /**< _ledger_count at open: the first record the window owns. */
    USize callocs; /**< _calloc_calls at open, refused attempts included. */
    USize twins;   /**< _twin_calls at open. */
} Window;

static LedgerEntry _ledger[_TEST_LEDGER_CAPACITY] = DEFAULT_INITIALIZATION;
static USize _ledger_count = 0;
static USize _ledger_overflow = 0;
static USize _double_free_count = 0;
static USize _foreign_free_count = 0;
static USize _calloc_calls = 0;
static USize _other_calls = 0;

/* Injection: refuse the _fail_ordinal-th calloc of exactly _fail_size bytes (and, when set, the
 * first of _fail_size_second bytes), or - _fail_any - every calloc. _fail_seen counts matches,
 * _fail_refused the nullptrs actually returned; both are captured BEFORE _test_disarm, which
 * zeroes them (reading them after the disarm made every case read "never fired" once). */
static USize _fail_size = 0;
static USize _fail_size_second = 0;
static USize _fail_ordinal = 0;
static USize _fail_seen = 0;
static USize _fail_refused = 0;
static bool _fail_any = false;

/* The aborting twin, watched. _twin_sizes[i] is the byte count of the i-th memory_alloc call, so
 * a case can ask which allocation went through it. _production_abort is set only in a forked
 * probe child: it makes a refused memory_alloc abort, as ERROR_CHECK_ENABLED's does. */
static USize _twin_calls = 0;
static USize _twin_sizes[_TEST_TWIN_CAPACITY] = DEFAULT_INITIALIZATION;
static bool _production_abort = false;

void* __real_calloc(USize const count, USize const size);
void* __real_malloc(USize const size);
void* __real_realloc(void *const pointer, USize const size);
void __real_free(void *const pointer);
void* __real_memory_alloc(USize const byte_count);

static void _ledger_record(void *const pointer, USize const size) {
    if (_ledger_count >= _TEST_LEDGER_CAPACITY) {
        // Untracked from here on, which would make every live count lie - asserted zero last.
        _ledger_overflow += 1;

        return;
    }

    _ledger[_ledger_count].pointer = pointer;
    _ledger[_ledger_count].size    = size;
    _ledger[_ledger_count].freed   = false;
    _ledger_count                 += 1;
}

/**
 * @brief Index of the NEWEST record for an address, or _ledger_count for none.
 *
 * Backwards, because the allocator recycles addresses: a forward scan matches the stale record of
 * a block freed earlier, counts the legal free of the new block as a double free, and leaves the
 * new block reading live forever. Side-effect free so the scan direction can be pinned against
 * fabricated rows.
 */
static USize _ledger_find(void const *const pointer) {
    for (USize i = _ledger_count; i > 0; i -= 1) {
        if (_ledger[i - 1].pointer == pointer) {
            return i - 1;
        }
    }

    return _ledger_count;
}

// Mark the newest record of POINTER released; false when the ledger never recorded it.
static bool _ledger_release(void const *const pointer) {
    USize const found = _ledger_find(pointer);

    if (found == _ledger_count) {
        return false;
    }

    if (_ledger[found].freed) {
        _double_free_count += 1;
    }

    _ledger[found].freed = true;

    return true;
}

// Only the NAMES are fixed by -Wl,--wrap; the parameters keep the project's const style.
void* __wrap_calloc(USize const count, USize const size) {
    // Guarded: a wrapped product could false-match _fail_size and refuse an unrelated block. The
    // real call below gets count and size untouched, so a wrap can only mislead this wrapper.
    USize const bytes = (size != 0 && count > USIZE_MAX / size) ? USIZE_MAX : count * size;

    _calloc_calls += 1;

    if (_fail_any) {
        _fail_seen    += 1;
        _fail_refused += 1;

        return nullptr;
    }

    if (_fail_size_second != 0 && bytes == _fail_size_second) {
        // Single-shot: only the first allocation of the second size is refused.
        _fail_size_second  = 0;
        _fail_refused     += 1;

        return nullptr;
    }

    if (_fail_size != 0 && bytes == _fail_size) {
        _fail_seen += 1;

        if (_fail_seen == _fail_ordinal) {
            _fail_refused += 1;

            return nullptr;
        }
    }

    void *const pointer = __real_calloc(count, size);

    if (!memory_empty(pointer)) {
        _ledger_record(pointer, bytes);
    }

    return pointer;
}

// Recorded, never refused: malloc is not the CFW seam. Nothing linked here calls it (measured).
void* __wrap_malloc(USize const size) {
    _other_calls += 1;

    void *const pointer = __real_malloc(size);

    if (!memory_empty(pointer)) {
        _ledger_record(pointer, size);
    }

    return pointer;
}

// Recorded, never refused, for the same reason. A moved block releases its old record.
void* __wrap_realloc(void *const pointer, USize const size) {
    _other_calls += 1;

    void *const moved = __real_realloc(pointer, size);

    if (!memory_empty(moved)) {
        if (!memory_empty(pointer)) {
            _ledger_release(pointer);
        }

        _ledger_record(moved, size);
    }

    return moved;
}

void __wrap_free(void *const pointer) {
    if (!memory_empty(pointer) && !_ledger_release(pointer)) {
        // With calloc, malloc and realloc all recorded, the only source left is a block a C
        // runtime DLL allocated and a wrapped object released - asserted zero in the last case.
        _foreign_free_count += 1;
    }

    __real_free(pointer);
}

void* __wrap_memory_alloc(USize const byte_count) {
    if (_twin_calls < _TEST_TWIN_CAPACITY) {
        _twin_sizes[_twin_calls] = byte_count;
    }

    _twin_calls += 1;

    void *const buffer = __real_memory_alloc(byte_count);

    if (_production_abort && memory_empty(buffer)) {
        abort();
    }

    return buffer;
}

/**
 * @brief Arm the injector: refuse the ordinal-th calloc of exactly this size.
 *
 * The counters are reset HERE, never at the call site - a caller that forgot was how an earlier
 * harness stopped injecting in its second case and measured an ordinary successful run.
 */
static void _test_arm(USize const size, USize const ordinal) {
    _fail_size         = size;
    _fail_size_second  = 0;
    _fail_ordinal      = ordinal;
    _fail_seen         = 0;
    _fail_refused      = 0;
    _fail_any          = false;
}

// Arm two sizes at once: the first block of each is refused (a recover path and its fallback).
static void _test_arm_pair(USize const size, USize const second) {
    _test_arm(size, 1);

    _fail_size_second = second;
}

// Refuse EVERY calloc - the oracle for "allocates nothing on any path".
static void _test_arm_any(void) {
    _test_arm(0, 0);

    _fail_any = true;
}

static void _test_disarm(void) {
    _fail_size         = 0;
    _fail_size_second  = 0;
    _fail_ordinal      = 0;
    _fail_seen         = 0;
    _fail_refused      = 0;
    _fail_any          = false;
}

static Window _window_open(void) {
    return (Window) { .mark = _ledger_count, .callocs = _calloc_calls, .twins = _twin_calls };
}

// Calloc attempts since the window opened, refused ones included.
static USize _window_callocs(Window const window) {
    return _calloc_calls - window.callocs;
}

// memory_alloc calls since the window opened.
static USize _window_twins(Window const window) {
    return _twin_calls - window.twins;
}

// Whether the aborting twin was asked for exactly BYTES since the window opened.
static bool _window_twin_saw(Window const window, USize const bytes) {
    for (USize i = window.twins; i < _twin_calls && i < _TEST_TWIN_CAPACITY; i += 1) {
        if (_twin_sizes[i] == bytes) {
            return true;
        }
    }

    return false;
}

// Records of exactly BYTES since the window opened, released or not.
static USize _window_records(Window const window, USize const bytes) {
    USize records = 0;

    for (USize i = window.mark; i < _ledger_count; i += 1) {
        records += _ledger[i].size == bytes ? 1 : 0;
    }

    return records;
}

// Records of exactly BYTES since the window opened that are still live.
static USize _window_live(Window const window, USize const bytes) {
    USize live = 0;

    for (USize i = window.mark; i < _ledger_count; i += 1) {
        live += !_ledger[i].freed && _ledger[i].size == bytes ? 1 : 0;
    }

    return live;
}

// Every record since the window opened that is still live, whatever its size.
static USize _window_live_any(Window const window) {
    USize live = 0;

    for (USize i = window.mark; i < _ledger_count; i += 1) {
        live += !_ledger[i].freed ? 1 : 0;
    }

    return live;
}

/*==============================================================================
 * MARK: - Fixtures
 *============================================================================*/

static char _label_buffer[_TEST_LABEL_SIZE] = DEFAULT_INITIALIZATION;

// "<row>: <what>" in one shared buffer - test_expect_* reports the name before it returns.
static char const* _label(char const *const row, char const *const what) {
    snprintf(_label_buffer, sizeof(_label_buffer), "%s: %s", row, what);

    return _label_buffer;
}

// PREFIX padded with 'x' and ".txt" to exactly LENGTH bytes - the length IS the block size - 1.
static void _name(char *const out, char const *const prefix, USize const length) {
    USize const prefix_size = char_length(prefix);
    USize const suffix_size = CHAR_STATIC_SIZE(".txt");
    USize i                 = 0;

    // The prefix yields to the suffix, so every name keeps ".txt" and its exact length.
    for (; i < prefix_size && i + suffix_size < length; i += 1) {
        out[i] = prefix[i];
    }

    for (; i + suffix_size < length; i += 1) {
        out[i] = 'x';
    }

    for (USize j = 0; i < length; i += 1, j += 1) {
        out[i] = ".txt"[j];
    }

    out[length] = '\0';
}

// Byte i of a fixture seeded with SEED: printable, no CR or 0x1A, so text mode reads it verbatim.
static char _pattern(USize const i, char const seed) {
    return (char) ('a' + (USize) (seed + i) % 26);
}

static bool _fixture_write(char const *const path, USize const size, char const seed) {
    FILE *const stream = fopen(path, "wb");

    if (memory_empty((void*) stream)) {
        return false;
    }

    bool written = true;

    for (USize i = 0; i < size && written; i += 1) {
        written = fputc(_pattern(i, seed), stream) != EOF;
    }

    return fclose(stream) == 0 && written;
}

// Whether PATH holds exactly SIZE pattern bytes of SEED - nothing more, nothing less.
static bool _fixture_matches(char const *const path, USize const size, char const seed) {
    FILE *const stream = fopen(path, "rb");

    if (memory_empty((void*) stream)) {
        return false;
    }

    bool matches = true;

    for (USize i = 0; i < size && matches; i += 1) {
        matches = fgetc(stream) == (U8) _pattern(i, seed);
    }

    matches = matches && fgetc(stream) == EOF;

    fclose(stream);

    return matches;
}

// Whether the content is the pattern - for a reader's returned buffer.
static bool _buffer_matches(char const *const buffer, USize const size, char const seed) {
    for (USize i = 0; i < size; i += 1) {
        if (buffer[i] != _pattern(i, seed)) {
            return false;
        }
    }

    return buffer[size] == '\0';
}

static bool _present(char const *const path) {
    FILE *const stream = fopen(path, "rb");

    if (memory_empty((void*) stream)) {
        return false;
    }

    fclose(stream);

    return true;
}

static USize _process_id(void) {
#ifdef _WIN32
    return (USize) GetCurrentProcessId();
#else
    return (USize) getpid();
#endif // _WIN32
}

// file_replace_1's first temp name for DESTINATION in THIS process: <destination>.tmp.<pid>.0.
static USize _replace_temp_name(char *const out, USize const capacity, char const *const destination) {
    int const written = snprintf(out, capacity, "%s.tmp.%llu.0", destination, (unsigned long long) _process_id());

    return written > 0 ? (USize) written : 0;
}

/* Open OS handles (Windows) or descriptors (Linux). A failure path that forgets to close the
 * reader or the writer shows up as a difference across the call. */
static USize _open_handles(void) {
#ifdef _WIN32
    DWORD count = 0;

    GetProcessHandleCount(GetCurrentProcess(), &count);

    return (USize) count;
#else
    USize count = 0;

    for (int descriptor = 0; descriptor < _TEST_DESCRIPTOR_LIMIT; descriptor += 1) {
        count += fcntl(descriptor, F_GETFD) != -1 ? 1 : 0;
    }

    return count;
#endif // _WIN32
}

static bool _make_directory(char const *const path) {
#ifdef _WIN32
    return CreateDirectoryA(path, nullptr) != 0;
#else
    return mkdir(path, 0700) == 0;
#endif // _WIN32
}

static void _remove_directory(char const *const path) {
#ifdef _WIN32
    RemoveDirectoryA(path);
#else
    rmdir(path);
#endif // _WIN32
}

/* Into <temp>/cfw_file_oom, falling back to the current directory: the dir harness found the
 * temp lookup unreliable from a --wrap'd binary once, and a fixture in the repository still works
 * (it only exposes the deletes to a sync client's transient handles). */
static bool _enter_fixture(void) {
#ifdef _WIN32
    char temp[MAX_PATH + CHAR_END_CHARACTER] = DEFAULT_INITIALIZATION;
    DWORD const size                          = GetTempPathA(sizeof(temp), temp);

    if (size == 0 || size >= sizeof(temp) || SetCurrentDirectoryA(temp) == 0) {
        log_message_1(LOG_LEVEL_WARN, "file oom: temp directory unavailable, fixtures go in the current directory\n");
    }
#else
    char const *const temp = getenv("TMPDIR");

    if (chdir(!memory_empty((void*) temp) && temp[0] != '\0' ? temp : "/tmp") != 0) {
        log_message_1(LOG_LEVEL_WARN, "file oom: temp directory unavailable, fixtures go in the current directory\n");
    }
#endif // _WIN32

    _make_directory(_TEST_DIRECTORY);

#ifdef _WIN32
    return SetCurrentDirectoryA(_TEST_DIRECTORY) != 0;
#else
    return chdir(_TEST_DIRECTORY) == 0;
#endif // _WIN32
}

// Back out of the fixture directory and remove it - every case removes its own files.
static void _leave_fixture(void) {
#ifdef _WIN32
    bool const left = SetCurrentDirectoryA("..") != 0;
#else
    bool const left = chdir("..") == 0;
#endif // _WIN32

    if (left) {
        _remove_directory(_TEST_DIRECTORY);
    }
}

/*==============================================================================
 * MARK: - Cases: the instrument
 *============================================================================*/

static void _test_ledger_find_returns_the_newest_record(Test *const test) {
    test_case_begin(test, "the ledger scan returns the newest record for a recycled address");

    // Fabricated rows at ONE address - the only deterministic way to express recycling. These
    // two writes bypass _ledger_record's capacity check, so they carry their own.
    USize const saved = _ledger_count;
    char        first = 0;
    char        other = 0;

    test_expect_true(test, "the ledger has room for the fabricated records", _ledger_count + 2 <= _TEST_LEDGER_CAPACITY);

    if (_ledger_count + 2 > _TEST_LEDGER_CAPACITY) {
        test_case_end(test);

        return;
    }

    _ledger[_ledger_count] = (LedgerEntry) { .pointer = &first, .size = 1, .freed = true };
    _ledger_count         += 1;
    _ledger[_ledger_count] = (LedgerEntry) { .pointer = &first, .size = 1, .freed = false };
    _ledger_count         += 1;

    USize const live = _ledger_find(&first);

    test_expect_u(test, "the newest record wins", _ledger_count - 1, live);
    test_expect_false(test, "the newest record is the live one", _ledger[live].freed);
    test_expect_u(test, "an address with no record reports a miss", _ledger_count, _ledger_find(&other));

    _ledger_count = saved;

    test_case_end(test);
}

static void _test_injector_fires(Test *const test) {
    test_case_begin(test, "the injector refuses the armed size and nothing else");

    // memory_try_alloc is a direct calloc; if the seam were not wired this would succeed.
    _test_arm(_TEST_COPY_BUFFER_BYTES + 1, 1);

    void *const refused   = memory_try_alloc(_TEST_COPY_BUFFER_BYTES + 1);
    void *const unrelated = memory_try_alloc(_TEST_COPY_BUFFER_BYTES + 2);
    USize const fired     = _fail_refused;

    _test_disarm();

    test_expect_u(test, "exactly one allocation was refused", 1, fired);
    test_expect_true(test, "the armed size came back nullptr", memory_empty(refused));
    test_expect_true(test, "a different size was served", !memory_empty(unrelated));

    if (!memory_empty(unrelated)) {
        memory_free(unrelated);
    }

    // The twin counter is wired: memory_alloc is an undefined reference in this object too.
    Window const window = _window_open();
    void *const twin    = memory_alloc(7);

    test_expect_u(test, "the aborting twin is counted", 1, _window_twins(window));
    test_expect_true(test, "the aborting twin's size is recorded", _window_twin_saw(window, 7));

    if (!memory_empty(twin)) {
        memory_free(twin);
    }

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: allocates nothing
 *============================================================================*/

/* file.h: file_open_nofollow_try_*, file_final_path_*, file_get_final_path and
 * file_get_modified "abort only on a nullptr CONTRACT pointer ... nothing is allocated on any
 * path". Proved by refusing EVERY calloc for the whole block and requiring each call to answer
 * exactly as it would with memory available - success where the fixture is there, the
 * documented OS code where it is not - while the wrapper sees zero attempts. The try openers and
 * file_open_wait_1 ride along: they are pure CRT calls, and the by-path readers build on them. */
static void _test_handle_first_family_allocates_nothing(Test *const test) {
    test_case_begin(test, "the handle-first family and the try openers allocate nothing");

    char const *const name = "handle_first.txt";
    char path_text[]       = "handle_first.txt";
    Str const path_str     = str_init_3(path_text, CHAR_STATIC_SIZE("handle_first.txt"));
    String const path_string = string_init_4(path_text, CHAR_STATIC_SIZE("handle_first.txt"));

    _remove_directory("handle_first_dir");

    test_expect_true(test, "the fixture is written", _fixture_write(name, _TEST_CONTENT_READ_PATH, 'h') && _make_directory("handle_first_dir"));

    I64 expected_modified = 0;

    test_expect_true(test, "the by-path mtime is readable", file_modified_1(name, &expected_modified));

    char final_by_handle[_TEST_PATH_SIZE] = DEFAULT_INITIALIZATION;
    char final_by_path[_TEST_PATH_SIZE]   = DEFAULT_INITIALIZATION;
    char final_by_str[_TEST_PATH_SIZE]    = DEFAULT_INITIALIZATION;
    char final_by_string[_TEST_PATH_SIZE] = DEFAULT_INITIALIZATION;
    char final_too_small[4]               = { 'z', 'z', 'z', '\0' };
    I64 modified                          = 0;
    USize const handles                   = _open_handles();
    Window const window                   = _window_open();

    _test_arm_any();

    File *nofollow           = file_open_nofollow_try_1(name);
    bool const got_final     = !memory_empty((void*) nofollow) && file_get_final_path(nofollow, final_by_handle, sizeof(final_by_handle));
    bool const got_modified  = !memory_empty((void*) nofollow) && file_get_modified(nofollow, &modified);
    int const first_byte     = !memory_empty((void*) nofollow) ? fgetc(nofollow) : EOF;
    File *nofollow_str       = file_open_nofollow_try_2(&path_str);
    File *nofollow_string    = file_open_nofollow_try_3(&path_string);
    bool const final_path    = file_final_path_1(name, final_by_path, sizeof(final_by_path));
    bool const final_str     = file_final_path_2(&path_str, final_by_str, sizeof(final_by_str));
    bool const final_string  = file_final_path_3(&path_string, final_by_string, sizeof(final_by_string));
    bool const final_small   = file_final_path_1(name, final_too_small, sizeof(final_too_small));

    File *const missing = file_open_nofollow_try_1("handle_first_missing.txt");
#ifdef _WIN32
    DWORD const missing_error = GetLastError();
#else
    int const missing_error = errno;
#endif // _WIN32

    File *const directory = file_open_nofollow_try_1("handle_first_dir");
#ifdef _WIN32
    DWORD const directory_error = GetLastError();

    File *const device        = file_open_nofollow_try_1("nul");
    DWORD const device_error  = GetLastError();
    File *const unc           = file_open_nofollow_try_1("\\\\localhost\\share\\x.txt");
    DWORD const unc_error     = GetLastError();
#else
    int const directory_error = errno;
#endif // _WIN32

    File *try_plain        = file_open_try_1(name, "rb");
    File *try_str          = file_open_try_2(&path_str, "rb");
    File *try_string       = file_open_try_3(&path_string, "rb");
    File *const try_absent = file_open_try_1("handle_first_missing.txt", "rb");
    int const try_error    = errno;
    File *waited           = file_open_wait_1(name, "rb", 1);

    // Captured BEFORE the disarm, which zeroes them.
    USize const attempts = _fail_seen;
    USize const callocs  = _window_callocs(window);
    USize const twins    = _window_twins(window);

    _test_disarm();

    test_expect_u(test, "no calloc was even attempted across the whole family", 0, attempts);
    test_expect_u(test, "the wrapper saw zero allocations", 0, callocs);
    test_expect_u(test, "the aborting twin was never called", 0, twins);

    // Positive anchors: every call answered as it would with memory to spare.
    test_expect_true(test, "nofollow_try_1 opened the fixture", !memory_empty((void*) nofollow));
    test_expect_true(test, "the opened stream reads the fixture's first byte", first_byte == (U8) _pattern(0, 'h'));
    test_expect_true(test, "get_final_path answered", got_final);
    test_expect_true(test, "the handle's final path names the fixture", char_ends_with_1(final_by_handle, name));
    test_expect_true(test, "get_modified answered", got_modified);
    test_expect_true(test, "the by-handle mtime equals the by-path mtime", modified == expected_modified);
    test_expect_true(test, "nofollow_try_2 opened the fixture", !memory_empty((void*) nofollow_str));
    test_expect_true(test, "nofollow_try_3 opened the fixture", !memory_empty((void*) nofollow_string));
    test_expect_true(test, "final_path_1 answered with the fixture", final_path && char_ends_with_1(final_by_path, name));
    test_expect_true(test, "final_path_2 answered with the fixture", final_str && char_ends_with_1(final_by_str, name));
    test_expect_true(test, "final_path_3 answered with the fixture", final_string && char_ends_with_1(final_by_string, name));
    test_expect_true(test, "the handle and the path agree", char_compare_equal_1(final_by_handle, final_by_path));
    test_expect_false(test, "a final path that does not fit answers false", final_small);
    test_expect_true(test, "and leaves the buffer EMPTY, never truncated", final_too_small[0] == '\0');

    // The refusals keep their documented reasons - an allocation failure did not replace them.
    test_expect_true(test, "a missing path answers nullptr", memory_empty((void*) missing));
    test_expect_true(test, "a directory answers nullptr", memory_empty((void*) directory));
#ifdef _WIN32
    test_expect_u(test, "the missing path keeps ERROR_FILE_NOT_FOUND", ERROR_FILE_NOT_FOUND, missing_error);
    test_expect_u(test, "the directory answers ERROR_ACCESS_DENIED", ERROR_ACCESS_DENIED, directory_error);
    test_expect_true(test, "a device spelling answers nullptr", memory_empty((void*) device));
    test_expect_u(test, "the device spelling answers ERROR_INVALID_HANDLE", ERROR_INVALID_HANDLE, device_error);
    test_expect_true(test, "a UNC path answers nullptr", memory_empty((void*) unc));
    test_expect_u(test, "the UNC path answers ERROR_INVALID_HANDLE", ERROR_INVALID_HANDLE, unc_error);
#else
    test_expect_u(test, "the missing path keeps ENOENT", ENOENT, (USize) missing_error);
    test_expect_u(test, "the directory answers EISDIR", EISDIR, (USize) directory_error);
#endif // _WIN32

    test_expect_true(test, "file_open_try_1 opened the fixture", !memory_empty((void*) try_plain));
    test_expect_true(test, "file_open_try_2 opened the fixture", !memory_empty((void*) try_str));
    test_expect_true(test, "file_open_try_3 opened the fixture", !memory_empty((void*) try_string));
    test_expect_true(test, "file_open_try_1 on a missing path answers nullptr", memory_empty((void*) try_absent));
    test_expect_u(test, "and keeps the open's own ENOENT", ENOENT, (USize) try_error);
    test_expect_true(test, "file_open_wait_1 opened the fixture", !memory_empty((void*) waited));

    File **const opened[] = { &nofollow, &nofollow_str, &nofollow_string, &try_plain, &try_str, &try_string, &waited };

    for (USize i = 0; i < sizeof(opened) / sizeof(opened[0]); i += 1) {
        if (!memory_empty((void*) *opened[i])) {
            file_close(opened[i]);
        }
    }

    test_expect_u(test, "every handle the family opened was closed by its stream", handles, _open_handles());
    test_expect_u(test, "nothing the family touched is left live", 0, _window_live_any(window));

    remove(name);
    _remove_directory("handle_first_dir");

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: recoverable out of memory
 *============================================================================*/

/* file_copy_1: the 64 KiB buffer is memory_try_alloc, and its refusal is a MEMORY Result. "A
 * FAILURE REMOVES WHAT IT WROTE": the destination the call created is gone, both streams are
 * closed, the source is untouched - and the aborting twin is never reached. */
static void _test_copy_buffer_refused(Test *const test) {
    test_case_begin(test, "file_copy_1: a refused copy buffer is a MEMORY Result with nothing left behind");

    char const *const source      = "copy_source.txt";
    char const *const destination = "copy_destination.txt";

    remove(destination);

    test_expect_true(test, "the source is written", _fixture_write(source, _TEST_CONTENT_COPY, 'c'));

    // Anchor: the measured profile row - one 64 KiB block, through the recovering allocator.
    Window const anchor        = _window_open();
    Result const anchor_result = file_copy_1(source, destination);

    test_expect_true(test, "anchor: an unarmed copy succeeds", result_is_success(anchor_result));
    test_expect_u(test, "anchor: the copy allocates exactly once", 1, _window_callocs(anchor));
    test_expect_u(test, "anchor: that block is the 64 KiB buffer", 1, _window_records(anchor, _TEST_COPY_BUFFER_BYTES));
    test_expect_u(test, "anchor: the copy never calls the aborting twin", 0, _window_twins(anchor));
    test_expect_u(test, "anchor: the buffer is released", 0, _window_live_any(anchor));
    test_expect_true(test, "anchor: the destination is a full copy", _fixture_matches(destination, _TEST_CONTENT_COPY, 'c'));

    remove(destination);

    USize const handles = _open_handles();
    Window const window = _window_open();

    _test_arm(_TEST_COPY_BUFFER_BYTES, 1);

    Result const result = file_copy_1(source, destination);
    USize const fired   = _fail_refused;

    _test_disarm();

    test_expect_u(test, "the buffer allocation was refused", 1, fired);
    test_expect_u(test, "the failure is RESULT_CATEGORY_MEMORY", RESULT_CATEGORY_MEMORY, result_category(result));
    test_expect_u(test, "the recover path never reached the aborting twin", 0, _window_twins(window));
    test_expect_false(test, "the destination the call created was removed", _present(destination));
    test_expect_true(test, "the source is untouched", _fixture_matches(source, _TEST_CONTENT_COPY, 'c'));
    test_expect_u(test, "the reader and the writer were both closed", handles, _open_handles());
    test_expect_u(test, "nothing allocated by the failed call is live", 0, _window_live_any(window));

    // And a retry with memory back works: the failure left nothing that blocks it (an exists-
    // refusal on a leftover destination would make this permanent).
    Result const retry = file_copy_1(source, destination);

    test_expect_true(test, "a retry once memory is back succeeds", result_is_success(retry));

    remove(destination);
    remove(source);

    test_case_end(test);
}

/* file_replace_1: "A failure while writing the temporary file leaves the destination untouched
 * and removes the temporary." The buffer refusal is the recover case; the temp NAME before it is
 * a documented-abort site in production (char_join_1 -> memory_alloc), pinned in the
 * degradation section and the abort probes. */
static void _test_replace_buffer_refused(Test *const test) {
    test_case_begin(test, "file_replace_1: a refused copy buffer leaves the destination and the directory as they were");

    char const *const source      = "replace_source.txt";
    char const *const destination = "replace_destination.txt";
    char temp[_TEST_NAME_SIZE * 2] = DEFAULT_INITIALIZATION;
    USize const temp_size          = _replace_temp_name(temp, sizeof(temp), destination) + CHAR_END_CHARACTER;

    test_expect_true(test, "the fixtures are written", _fixture_write(source, _TEST_CONTENT_REPLACE_NEW, 'n') && _fixture_write(destination, _TEST_CONTENT_REPLACE_OLD, 'o'));

    // Anchor: two blocks - the temp name through the twin, the buffer through the recovering form.
    Window const anchor        = _window_open();
    Result const anchor_result = file_replace_1(source, destination);

    test_expect_true(test, "anchor: an unarmed replace succeeds", result_is_success(anchor_result));
    test_expect_u(test, "anchor: the replace allocates exactly twice", 2, _window_callocs(anchor));
    test_expect_u(test, "anchor: one block is the temp name", 1, _window_records(anchor, temp_size));
    test_expect_u(test, "anchor: one block is the 64 KiB buffer", 1, _window_records(anchor, _TEST_COPY_BUFFER_BYTES));
    test_expect_u(test, "anchor: only the temp name goes through the aborting twin", 1, _window_twins(anchor));
    test_expect_true(test, "anchor: and it is the temp name", _window_twin_saw(anchor, temp_size));
    test_expect_u(test, "anchor: both are released", 0, _window_live_any(anchor));
    test_expect_true(test, "anchor: the destination carries the new content", _fixture_matches(destination, _TEST_CONTENT_REPLACE_NEW, 'n'));

    test_expect_true(test, "the old destination is restored", _fixture_write(destination, _TEST_CONTENT_REPLACE_OLD, 'o'));

    USize const handles = _open_handles();
    Window const window = _window_open();

    _test_arm(_TEST_COPY_BUFFER_BYTES, 1);

    Result const result = file_replace_1(source, destination);
    USize const fired   = _fail_refused;

    _test_disarm();

    test_expect_u(test, "the buffer allocation was refused", 1, fired);
    test_expect_u(test, "the failure is RESULT_CATEGORY_MEMORY", RESULT_CATEGORY_MEMORY, result_category(result));
    test_expect_false(test, "the buffer never went through the aborting twin", _window_twin_saw(window, _TEST_COPY_BUFFER_BYTES));
    test_expect_true(test, "the destination keeps its OLD content", _fixture_matches(destination, _TEST_CONTENT_REPLACE_OLD, 'o'));
    test_expect_false(test, "the temporary was removed", _present(temp));
    test_expect_true(test, "the source is untouched", _fixture_matches(source, _TEST_CONTENT_REPLACE_NEW, 'n'));
    test_expect_u(test, "the temp name was built (the failure landed AFTER it)", 1, _window_records(window, temp_size));
    test_expect_u(test, "and released", 0, _window_live_any(window));
    test_expect_u(test, "the reader and the temp's writer were both closed", handles, _open_handles());

    remove(temp);
    remove(destination);
    remove(source);

    test_case_end(test);
}

typedef char* (*FpRawReader)(char const *const path);

static char* _read_by_path(char const *const path) {
    return file_read_to_char(path);
}

static char* _read_by_path_wait(char const *const path) {
    return file_read_to_char_wait(path, 1);
}

static char* _read_by_handle(char const *const path) {
    File *file = file_open_try_1(path, "rb");

    if (memory_empty((void*) file)) {
        return nullptr;
    }

    char *const buffer = file_read_all_1(file);

    file_close(&file);

    return buffer;
}

/* One raw-reader row. The content block is memory_try_alloc; its refusal still owes the caller
 * the documented non-null empty buffer, which comes from a 1-byte memory_alloc fallback. The
 * twin is therefore called exactly once - for 1 byte, never for the content - and that 1-byte
 * block is the one live block the call hands back. */
static void _raw_reader_row(Test *const test, char const *const row, char const *const prefix, FpRawReader const reader, USize const size, char const seed) {
    char name[_TEST_NAME_SIZE] = DEFAULT_INITIALIZATION;

    _name(name, prefix, _TEST_RAW_READER_INDEX);

    test_expect_true(test, _label(row, "the fixture is written"), _fixture_write(name, size, seed));

    Window const anchor = _window_open();
    char *const content = reader(name);

    test_expect_true(test, _label(row, "anchor: the unarmed read returns the content"), !memory_empty(content) && _buffer_matches(content, size, seed));
    test_expect_u(test, _label(row, "anchor: one allocation"), 1, _window_callocs(anchor));
    test_expect_u(test, _label(row, "anchor: of size + 1"), 1, _window_records(anchor, size + CHAR_END_CHARACTER));
    test_expect_u(test, _label(row, "anchor: through the recovering allocator"), 0, _window_twins(anchor));

    if (!memory_empty(content)) {
        memory_free(content);
    }

    USize const handles = _open_handles();
    Window const window = _window_open();

    _test_arm(size + CHAR_END_CHARACTER, 1);

    char *const empty   = reader(name);
    USize const fired   = _fail_refused;

    _test_disarm();

    test_expect_u(test, _label(row, "the content block was refused"), 1, fired);
    test_expect_true(test, _label(row, "the reader still returns a buffer, never nullptr"), !memory_empty(empty));
    test_expect_true(test, _label(row, "and it is the empty string"), !memory_empty(empty) && empty[0] == '\0');
    test_expect_u(test, _label(row, "the aborting twin was called once"), 1, _window_twins(window));
    test_expect_true(test, _label(row, "for the 1-byte fallback"), _window_twin_saw(window, CHAR_END_CHARACTER));
    test_expect_false(test, _label(row, "and never for the content"), _window_twin_saw(window, size + CHAR_END_CHARACTER));
    test_expect_u(test, _label(row, "the fallback is the one live block"), 1, _window_live(window, CHAR_END_CHARACTER));
    test_expect_u(test, _label(row, "nothing else is live"), 1, _window_live_any(window));
    test_expect_u(test, _label(row, "the stream was closed"), handles, _open_handles());

    if (!memory_empty(empty)) {
        memory_free(empty);
    }

    test_expect_u(test, _label(row, "freeing the empty buffer releases it"), 0, _window_live_any(window));

    remove(name);
}

static void _test_raw_readers_degrade_to_empty(Test *const test) {
    test_case_begin(test, "the raw whole-file readers degrade to a non-null empty buffer");

    _raw_reader_row(test, "file_read_to_char", "rp", _read_by_path, _TEST_CONTENT_READ_PATH, 'p');
    _raw_reader_row(test, "file_read_all_1", "rh", _read_by_handle, _TEST_CONTENT_READ_HANDLE, 'q');
    _raw_reader_row(test, "file_read_to_char_wait", "rw", _read_by_path_wait, _TEST_CONTENT_READ_WAIT, 'w');

    test_case_end(test);
}

/* _file_path_addressable (Windows): the one Windows conversion helper that allocates - two
 * UTF-16 copies of a non-ASCII path, through memory_try_alloc - and "this predicate only ever
 * fails closed" - and, since a refused buffer says nothing about the NAME, reports
 * RESULT_CATEGORY_MEMORY rather than the unaddressable refusal whose remedy is the trash path.
 * Reachable only when the UTF-8 and code-page readings have the SAME length, which for a
 * non-ASCII name means only when the process code page IS UTF-8 - so the makefile links oom.exe
 * (and only oom.exe) with an activeCodePage=UTF-8 manifest; the SKIPPED branch remains for a
 * Windows older than 1903, which ignores that manifest entry. */
static void _test_addressable_conversion_fails_closed(Test *const test) {
    test_case_begin(test, "the Windows addressability check fails closed on a refused conversion");

#ifdef _WIN32
    // cafe with an acute e, as UTF-8.
    char const *const name = "oom_caf\xC3\xA9.txt";

    if (GetACP() != CP_UTF8) {
        Window const window = _window_open();
        Result const result = file_remove_1(name);

        test_expect_u(test, "a non-ASCII name is refused before converting anything", 0, _window_callocs(window));
        test_expect_true(test, "with the documented unaddressable reason", file_result_is_unaddressable(result));

        log_message_1(LOG_LEVEL_WARN, "SKIPPED: refused-conversion row - the process code page is %u, not UTF-8 (65001); a non-ASCII UTF-8 name is two or more code-page units "
            "and one UTF-16 unit, so the lengths always differ and _file_path_addressable refuses before its allocation.\n", (unsigned) GetACP());
    }
    else {
        int const units   = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, nullptr, 0);
        USize const bytes = (USize) units * sizeof(WCHAR);

        test_expect_true(test, "the fixture is written", _fixture_write(name, _TEST_CONTENT_WRAPPER, 'u'));

        /* Ordinal 1 refuses the first copy (the second is still taken, then released); ordinal 2
         * serves the first and refuses the second - both mixed exits of the release block. */
        for (USize ordinal = 1; ordinal <= 2; ordinal += 1) {
            char const *const row = ordinal == 1 ? "first buffer refused" : "second buffer refused";
            Window const window   = _window_open();

            _test_arm(bytes, ordinal);

            Result const result = file_remove_1(name);
            USize const fired   = _fail_refused;

            _test_disarm();

            test_expect_u(test, _label(row, "the conversion buffer was refused"), 1, fired);
            test_expect_true(test, _label(row, "reported as RESULT_CATEGORY_MEMORY"), result_category(result) == RESULT_CATEGORY_MEMORY);
            test_expect_false(test, _label(row, "never as the unaddressable refusal"), file_result_is_unaddressable(result));
            test_expect_true(test, _label(row, "fail closed: the file was not removed"), _fixture_matches(name, _TEST_CONTENT_WRAPPER, 'u'));
            test_expect_u(test, _label(row, "the other buffer was taken"), 1, _window_records(window, bytes));
            test_expect_u(test, _label(row, "and released"), 0, _window_live_any(window));
        }

        // Positive anchor: unarmed, the same call takes both buffers, finds the name addressable and removes it.
        Window const control = _window_open();
        Result const removed = file_remove_1(name);

        test_expect_true(test, "anchor: unarmed, the removal succeeds", result_is_success(removed));
        test_expect_u(test, "anchor: through both conversion buffers", 2, _window_records(control, bytes));
        test_expect_u(test, "anchor: both released", 0, _window_live_any(control));

        // Cleanup only for a failed anchor: a passing one already removed the fixture.
        remove(name);
    }
#else
    log_message_1(LOG_LEVEL_WARN, "SKIPPED: refused-conversion row - Linux paths are bytes; _file_path_addressable compiles to `return RESULT_SUCCESS` and allocates nothing.\n");
#endif // _WIN32

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: documented-abort sites, unchecked degradation
 *============================================================================*/

/* file.h, Error Handling, "ONE EXCEPTION": the _2/_3 wrappers copy their Str/String path through
 * char_new_3, whose memory_alloc ends the process under ERROR_CHECK_ENABLED. That abort is the
 * production contract and the probes below pin it. In THIS build memory_alloc returns nullptr and
 * the wrappers' own guards run - code that exists precisely so an unchecked build degrades
 * instead of passing nullptr on. Each row asserts that degradation AND that the refused
 * allocation went through the aborting twin (the production abort), plus: the sibling copy, when
 * one was made, was taken and released; nothing is left live; nothing on disk changed. */

typedef struct WrapperFixture {
    char copy2_source[_TEST_NAME_SIZE];
    char copy2_destination[_TEST_NAME_SIZE];
    char copy3_source[_TEST_NAME_SIZE];
    char copy3_destination[_TEST_NAME_SIZE];
    char rename2_source[_TEST_NAME_SIZE];
    char rename2_destination[_TEST_NAME_SIZE];
    char rename3_source[_TEST_NAME_SIZE];
    char rename3_destination[_TEST_NAME_SIZE];
    char remove2[_TEST_NAME_SIZE];
    char remove3[_TEST_NAME_SIZE];
    char set_modified2[_TEST_NAME_SIZE];
    char set_modified3[_TEST_NAME_SIZE];
    char replace1_destination[_TEST_NAME_SIZE];
    char replace2_source[_TEST_NAME_SIZE];
    char replace2_destination[_TEST_NAME_SIZE];
    char replace3_source[_TEST_NAME_SIZE];
    char replace3_destination[_TEST_NAME_SIZE];
} WrapperFixture;

static WrapperFixture _wrapper = DEFAULT_INITIALIZATION;

static void _wrapper_names(void) {
    _name(_wrapper.copy2_source, "c2s", _TEST_NAME_COPY2_SOURCE);
    _name(_wrapper.copy2_destination, "c2d", _TEST_NAME_COPY2_DESTINATION);
    _name(_wrapper.copy3_source, "c", _TEST_NAME_COPY3_SOURCE);
    _name(_wrapper.copy3_destination, "c3d", _TEST_NAME_COPY3_DESTINATION);
    _name(_wrapper.rename2_source, "n2s", _TEST_NAME_RENAME2_SOURCE);
    _name(_wrapper.rename2_destination, "n2d", _TEST_NAME_RENAME2_DESTINATION);
    _name(_wrapper.rename3_source, "n3s", _TEST_NAME_RENAME3_SOURCE);
    _name(_wrapper.rename3_destination, "n3d", _TEST_NAME_RENAME3_DESTINATION);
    _name(_wrapper.remove2, "m2", _TEST_NAME_REMOVE2);
    _name(_wrapper.remove3, "m3", _TEST_NAME_REMOVE3);
    _name(_wrapper.set_modified2, "t2", _TEST_NAME_SET_MODIFIED2);
    _name(_wrapper.set_modified3, "t3", _TEST_NAME_SET_MODIFIED3);
    _name(_wrapper.replace1_destination, "p1d", _TEST_NAME_REPLACE1_DESTINATION);
    _name(_wrapper.replace2_source, "p2s", _TEST_NAME_REPLACE2_SOURCE);
    _name(_wrapper.replace2_destination, "p2d", _TEST_NAME_REPLACE2_DESTINATION);
    _name(_wrapper.replace3_source, "p3s", _TEST_NAME_REPLACE3_SOURCE);
    _name(_wrapper.replace3_destination, "p3d", _TEST_NAME_REPLACE3_DESTINATION);
}

static Str _view_str(char *const name) {
    return str_init_3(name, char_length(name));
}

static String _view_string(char *const name) {
    return string_init_4(name, char_length(name));
}

/**
 * @brief Assert one wrapper row's degradation.
 * @param armed   Block size that was refused (one path's length + 1).
 * @param sibling The other path's block size, or 0 when the call copies only one path.
 * @param intact  Whether the disk is exactly as before the call.
 */
static void _wrapper_expect(Test *const test, char const *const row, Window const window, Result const result, USize const fired,
    USize const handles, USize const armed, USize const sibling, bool const intact) {
    test_expect_u(test, _label(row, "the path copy was refused"), 1, fired);
    test_expect_u(test, _label(row, "the failure is RESULT_CATEGORY_MEMORY"), RESULT_CATEGORY_MEMORY, result_category(result));
    test_expect_true(test, _label(row, "the refused copy went through the aborting twin (production aborts here)"), _window_twin_saw(window, armed));

    if (sibling != 0) {
        test_expect_u(test, _label(row, "the sibling path copy was taken"), 1, _window_records(window, sibling));
    }

    test_expect_u(test, _label(row, "nothing is left live"), 0, _window_live_any(window));
    test_expect_true(test, _label(row, "the disk is untouched"), intact);
    test_expect_u(test, _label(row, "no handle was left open"), handles, _open_handles());
}

static void _test_wrapper_copy_degrades(Test *const test) {
    test_case_begin(test, "file_copy_2/_3: a refused path copy degrades to MEMORY");

    test_expect_true(test, "the sources are written", _fixture_write(_wrapper.copy2_source, _TEST_CONTENT_WRAPPER, 'a') && _fixture_write(_wrapper.copy3_source, _TEST_CONTENT_WRAPPER, 'b'));

    remove(_wrapper.copy2_destination);
    remove(_wrapper.copy3_destination);

    {
        // The DESTINATION (second) copy refused: the source copy before it must be released.
        Str const source      = _view_str(_wrapper.copy2_source);
        Str const destination = _view_str(_wrapper.copy2_destination);
        USize const handles   = _open_handles();
        Window const window   = _window_open();

        _test_arm(_TEST_NAME_COPY2_DESTINATION + CHAR_END_CHARACTER, 1);

        Result const result = file_copy_2(&source, &destination);
        USize const fired   = _fail_refused;

        _test_disarm();

        _wrapper_expect(test, "file_copy_2", window, result, fired, handles, _TEST_NAME_COPY2_DESTINATION + CHAR_END_CHARACTER, _TEST_NAME_COPY2_SOURCE + CHAR_END_CHARACTER,
            !_present(_wrapper.copy2_destination) && _fixture_matches(_wrapper.copy2_source, _TEST_CONTENT_WRAPPER, 'a'));
    }

    {
        // The SOURCE (first) copy refused: the destination copy is still made - and must be freed.
        String const source      = _view_string(_wrapper.copy3_source);
        String const destination = _view_string(_wrapper.copy3_destination);
        USize const handles      = _open_handles();
        Window const window      = _window_open();

        _test_arm(_TEST_NAME_COPY3_SOURCE + CHAR_END_CHARACTER, 1);

        Result const result = file_copy_3(&source, &destination);
        USize const fired   = _fail_refused;

        _test_disarm();

        _wrapper_expect(test, "file_copy_3", window, result, fired, handles, _TEST_NAME_COPY3_SOURCE + CHAR_END_CHARACTER, _TEST_NAME_COPY3_DESTINATION + CHAR_END_CHARACTER,
            !_present(_wrapper.copy3_destination) && _fixture_matches(_wrapper.copy3_source, _TEST_CONTENT_WRAPPER, 'b'));
    }

    remove(_wrapper.copy2_source);
    remove(_wrapper.copy3_source);

    test_case_end(test);
}

static void _test_wrapper_rename_degrades(Test *const test) {
    test_case_begin(test, "file_rename_2/_3: a refused path copy degrades to MEMORY and renames nothing");

    test_expect_true(test, "the sources are written",
        _fixture_write(_wrapper.rename2_source, _TEST_CONTENT_WRAPPER, 'c') && _fixture_write(_wrapper.rename3_source, _TEST_CONTENT_WRAPPER, 'd'));

    remove(_wrapper.rename2_destination);
    remove(_wrapper.rename3_destination);

    {
        Str const source      = _view_str(_wrapper.rename2_source);
        Str const destination = _view_str(_wrapper.rename2_destination);
        USize const handles   = _open_handles();
        Window const window   = _window_open();

        _test_arm(_TEST_NAME_RENAME2_DESTINATION + CHAR_END_CHARACTER, 1);

        Result const result = file_rename_2(&source, &destination);
        USize const fired   = _fail_refused;

        _test_disarm();

        _wrapper_expect(test, "file_rename_2", window, result, fired, handles, _TEST_NAME_RENAME2_DESTINATION + CHAR_END_CHARACTER, _TEST_NAME_RENAME2_SOURCE + CHAR_END_CHARACTER,
            !_present(_wrapper.rename2_destination) && _fixture_matches(_wrapper.rename2_source, _TEST_CONTENT_WRAPPER, 'c'));
    }

    {
        String const source      = _view_string(_wrapper.rename3_source);
        String const destination = _view_string(_wrapper.rename3_destination);
        USize const handles      = _open_handles();
        Window const window      = _window_open();

        _test_arm(_TEST_NAME_RENAME3_SOURCE + CHAR_END_CHARACTER, 1);

        Result const result = file_rename_3(&source, &destination);
        USize const fired   = _fail_refused;

        _test_disarm();

        _wrapper_expect(test, "file_rename_3", window, result, fired, handles, _TEST_NAME_RENAME3_SOURCE + CHAR_END_CHARACTER, _TEST_NAME_RENAME3_DESTINATION + CHAR_END_CHARACTER,
            !_present(_wrapper.rename3_destination) && _fixture_matches(_wrapper.rename3_source, _TEST_CONTENT_WRAPPER, 'd'));
    }

    remove(_wrapper.rename2_source);
    remove(_wrapper.rename3_source);

    test_case_end(test);
}

static void _test_wrapper_remove_and_set_modified_degrade(Test *const test) {
    test_case_begin(test, "file_remove_2/_3 and file_set_modified_2/_3: a refused path copy degrades to MEMORY");

    test_expect_true(test, "the fixtures are written", _fixture_write(_wrapper.remove2, _TEST_CONTENT_WRAPPER, 'e') && _fixture_write(_wrapper.remove3, _TEST_CONTENT_WRAPPER, 'f') &&
        _fixture_write(_wrapper.set_modified2, _TEST_CONTENT_WRAPPER, 'g') && _fixture_write(_wrapper.set_modified3, _TEST_CONTENT_WRAPPER, 'h'));

    {
        Str const path      = _view_str(_wrapper.remove2);
        USize const handles = _open_handles();
        Window const window = _window_open();

        _test_arm(_TEST_NAME_REMOVE2 + CHAR_END_CHARACTER, 1);

        Result const result = file_remove_2(&path);
        USize const fired   = _fail_refused;

        _test_disarm();

        _wrapper_expect(test, "file_remove_2", window, result, fired, handles, _TEST_NAME_REMOVE2 + CHAR_END_CHARACTER, 0, _fixture_matches(_wrapper.remove2, _TEST_CONTENT_WRAPPER, 'e'));
    }

    {
        String const path   = _view_string(_wrapper.remove3);
        USize const handles = _open_handles();
        Window const window = _window_open();

        _test_arm(_TEST_NAME_REMOVE3 + CHAR_END_CHARACTER, 1);

        Result const result = file_remove_3(&path);
        USize const fired   = _fail_refused;

        _test_disarm();

        _wrapper_expect(test, "file_remove_3", window, result, fired, handles, _TEST_NAME_REMOVE3 + CHAR_END_CHARACTER, 0, _fixture_matches(_wrapper.remove3, _TEST_CONTENT_WRAPPER, 'f'));
    }

    // An epoch far from "now", so an mtime the refused call DID set would be visible.
    I64 const target = _TEST_EPOCH_SECONDS;

    {
        I64 before = 0;
        I64 after  = 0;

        file_modified_1(_wrapper.set_modified2, &before);

        Str const path      = _view_str(_wrapper.set_modified2);
        USize const handles = _open_handles();
        Window const window = _window_open();

        _test_arm(_TEST_NAME_SET_MODIFIED2 + CHAR_END_CHARACTER, 1);

        Result const result = file_set_modified_2(&path, target);
        USize const fired   = _fail_refused;

        _test_disarm();

        bool const read_back = file_modified_1(_wrapper.set_modified2, &after);

        _wrapper_expect(test, "file_set_modified_2", window, result, fired, handles, _TEST_NAME_SET_MODIFIED2 + CHAR_END_CHARACTER, 0, read_back && after == before && after != target);
    }

    {
        I64 before = 0;
        I64 after  = 0;

        file_modified_1(_wrapper.set_modified3, &before);

        String const path   = _view_string(_wrapper.set_modified3);
        USize const handles = _open_handles();
        Window const window = _window_open();

        _test_arm(_TEST_NAME_SET_MODIFIED3 + CHAR_END_CHARACTER, 1);

        Result const result = file_set_modified_3(&path, target);
        USize const fired   = _fail_refused;

        _test_disarm();

        bool const read_back = file_modified_1(_wrapper.set_modified3, &after);

        _wrapper_expect(test, "file_set_modified_3", window, result, fired, handles, _TEST_NAME_SET_MODIFIED3 + CHAR_END_CHARACTER, 0, read_back && after == before && after != target);
    }

    remove(_wrapper.remove2);
    remove(_wrapper.remove3);
    remove(_wrapper.set_modified2);
    remove(_wrapper.set_modified3);

    test_case_end(test);
}

static void _test_wrapper_replace_degrades(Test *const test) {
    test_case_begin(test, "file_replace_1/_2/_3: a refused path or temp-name copy degrades to MEMORY");

    char temp[_TEST_NAME_SIZE * 2] = DEFAULT_INITIALIZATION;
    USize const temp_size          = _replace_temp_name(temp, sizeof(temp), _wrapper.replace1_destination) + CHAR_END_CHARACTER;

    test_expect_true(test, "the fixtures are written",
        _fixture_write(_wrapper.replace2_source, _TEST_CONTENT_WRAPPER, 'i') && _fixture_write(_wrapper.replace2_destination, _TEST_CONTENT_WRAPPER, 'j') &&
        _fixture_write(_wrapper.replace3_source, _TEST_CONTENT_WRAPPER, 'k') && _fixture_write(_wrapper.replace3_destination, _TEST_CONTENT_WRAPPER, 'l') &&
        _fixture_write(_wrapper.replace1_destination, _TEST_CONTENT_WRAPPER, 'm'));

    {
        Str const source      = _view_str(_wrapper.replace2_source);
        Str const destination = _view_str(_wrapper.replace2_destination);
        USize const handles   = _open_handles();
        Window const window   = _window_open();

        _test_arm(_TEST_NAME_REPLACE2_DESTINATION + CHAR_END_CHARACTER, 1);

        Result const result = file_replace_2(&source, &destination);
        USize const fired   = _fail_refused;

        _test_disarm();

        _wrapper_expect(test, "file_replace_2", window, result, fired, handles, _TEST_NAME_REPLACE2_DESTINATION + CHAR_END_CHARACTER, _TEST_NAME_REPLACE2_SOURCE + CHAR_END_CHARACTER,
            _fixture_matches(_wrapper.replace2_destination, _TEST_CONTENT_WRAPPER, 'j') && _fixture_matches(_wrapper.replace2_source, _TEST_CONTENT_WRAPPER, 'i'));
    }

    {
        String const source      = _view_string(_wrapper.replace3_source);
        String const destination = _view_string(_wrapper.replace3_destination);
        USize const handles      = _open_handles();
        Window const window      = _window_open();

        _test_arm(_TEST_NAME_REPLACE3_SOURCE + CHAR_END_CHARACTER, 1);

        Result const result = file_replace_3(&source, &destination);
        USize const fired   = _fail_refused;

        _test_disarm();

        _wrapper_expect(test, "file_replace_3", window, result, fired, handles, _TEST_NAME_REPLACE3_SOURCE + CHAR_END_CHARACTER, _TEST_NAME_REPLACE3_DESTINATION + CHAR_END_CHARACTER,
            _fixture_matches(_wrapper.replace3_destination, _TEST_CONTENT_WRAPPER, 'l') && _fixture_matches(_wrapper.replace3_source, _TEST_CONTENT_WRAPPER, 'k'));
    }

    {
        // The temp NAME refused inside file_replace_1 itself - an internal working buffer, so the
        // same ONE EXCEPTION: it aborts in production, and here the 1 returns MEMORY before any
        // file is created.
        USize const handles = _open_handles();
        Window const window = _window_open();

        _test_arm(temp_size, 1);

        Result const result = file_replace_1(_wrapper.replace2_source, _wrapper.replace1_destination);
        USize const fired   = _fail_refused;

        _test_disarm();

        _wrapper_expect(test, "file_replace_1 temp name", window, result, fired, handles, temp_size, 0,
            _fixture_matches(_wrapper.replace1_destination, _TEST_CONTENT_WRAPPER, 'm') && !_present(temp));
        test_expect_u(test, "file_replace_1 temp name: the copy buffer was never reached", 0, _window_records(window, _TEST_COPY_BUFFER_BYTES));
    }

    remove(_wrapper.replace1_destination);
    remove(_wrapper.replace2_source);
    remove(_wrapper.replace2_destination);
    remove(_wrapper.replace3_source);
    remove(_wrapper.replace3_destination);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: documented abort contracts
 *============================================================================*/

// One probe: arms its own sizes when ARMED (the replace probe's temp name holds the CHILD's pid,
// so only the child can size it), then makes the call.
typedef void (*FpProbe)(bool const armed);

static void _probe_copy_2(bool const armed) {
    char source_text[]      = _TEST_PROBE_SOURCE;
    char destination_text[] = _TEST_PROBE_COPY_DESTINATION;
    Str const source        = _view_str(source_text);
    Str const destination   = _view_str(destination_text);

    if (armed) {
        _test_arm(CHAR_STATIC_SIZE(_TEST_PROBE_COPY_DESTINATION) + CHAR_END_CHARACTER, 1);
    }

    file_copy_2(&source, &destination);
}

static void _probe_remove_3(bool const armed) {
    char path_text[]  = _TEST_PROBE_REMOVE;
    String const path = _view_string(path_text);

    if (armed) {
        _test_arm(CHAR_STATIC_SIZE(_TEST_PROBE_REMOVE) + CHAR_END_CHARACTER, 1);
    }

    file_remove_3(&path);
}

static void _probe_set_modified_2(bool const armed) {
    char path_text[] = _TEST_PROBE_SOURCE;
    Str const path   = _view_str(path_text);

    if (armed) {
        _test_arm(CHAR_STATIC_SIZE(_TEST_PROBE_SOURCE) + CHAR_END_CHARACTER, 1);
    }

    file_set_modified_2(&path, _TEST_EPOCH_SECONDS);
}

static void _probe_replace_temp_name(bool const armed) {
    char temp[_TEST_NAME_SIZE * 2] = DEFAULT_INITIALIZATION;

    if (armed) {
        _test_arm(_replace_temp_name(temp, sizeof(temp), _TEST_PROBE_REPLACE_DESTINATION) + CHAR_END_CHARACTER, 1);
    }

    file_replace_1(_TEST_PROBE_SOURCE, _TEST_PROBE_REPLACE_DESTINATION);
}

static void _probe_read_to_char_fallback(bool const armed) {
    if (armed) {
        // The content block (recover) AND the 1-byte fallback behind it (abort).
        _test_arm_pair(_TEST_CONTENT_PROBE + CHAR_END_CHARACTER, CHAR_END_CHARACTER);
    }

    char *const content = file_read_to_char(_TEST_PROBE_SOURCE);

    if (!memory_empty(content)) {
        memory_free(content);
    }
}

static void _probe_read_to_string(bool const armed) {
    if (armed) {
        _test_arm(_TEST_CONTENT_PROBE + CHAR_END_CHARACTER, 1);
    }

    String content = file_read_to_string(_TEST_PROBE_SOURCE);

    string_uninit(&content);
}

static void _probe_read_to_str(bool const armed) {
    if (armed) {
        _test_arm(_TEST_CONTENT_PROBE + CHAR_END_CHARACTER, 1);
    }

    Str content = file_read_to_str(_TEST_PROBE_SOURCE);

    str_uninit(&content);
}

static void _probe_read_all_3(bool const armed) {
    File *file = file_open_try_1(_TEST_PROBE_SOURCE, "rb");

    if (armed) {
        _test_arm(_TEST_CONTENT_PROBE + CHAR_END_CHARACTER, 1);
    }

    String content = file_read_all_3(file);

    string_uninit(&content);
    file_close(&file);
}

static void _probe_align_read(bool const armed) {
    // _file_align_read_to: size + (padding - size % padding) + padding.
    USize const size = _TEST_CONTENT_PROBE + (_TEST_PROBE_PADDING - _TEST_CONTENT_PROBE % _TEST_PROBE_PADDING) + _TEST_PROBE_PADDING;

    if (armed) {
        _test_arm(size, 1);
    }

    String content = file_align_read_to_string(_TEST_PROBE_SOURCE, _TEST_PROBE_PADDING);

    string_uninit(&content);
}

static void _probe_exists_2(bool const armed) {
    char path_text[] = _TEST_PROBE_SOURCE;
    Str const path   = _view_str(path_text);

    if (armed) {
        _test_arm(CHAR_STATIC_SIZE(_TEST_PROBE_SOURCE) + CHAR_END_CHARACTER, 1);
    }

    file_exists_2(&path);
}

static void _probe_size_3(bool const armed) {
    char path_text[]  = _TEST_PROBE_SOURCE;
    String const path = _view_string(path_text);
    USize size        = 0;

    if (armed) {
        _test_arm(CHAR_STATIC_SIZE(_TEST_PROBE_SOURCE) + CHAR_END_CHARACTER, 1);
    }

    file_size_3(&path, &size);
}

static void _probe_modified_2(bool const armed) {
    char path_text[] = _TEST_PROBE_SOURCE;
    Str const path   = _view_str(path_text);
    I64 modified     = 0;

    if (armed) {
        _test_arm(CHAR_STATIC_SIZE(_TEST_PROBE_SOURCE) + CHAR_END_CHARACTER, 1);
    }

    file_modified_2(&path, &modified);
}

typedef struct ProbeRow {
    char const *name;     /**< Function under probe. */
    char const *contract; /**< Where the abort is documented. */
    FpProbe probe;        /**< The call. */
} ProbeRow;

static ProbeRow const _probes[] = {
    { "file_copy_2", "file.h Error Handling, ONE EXCEPTION: char_new_3", _probe_copy_2 },
    { "file_remove_3", "file.h Error Handling, ONE EXCEPTION: char_new_3", _probe_remove_3 },
    { "file_set_modified_2", "file.h Error Handling, ONE EXCEPTION: char_new_3", _probe_set_modified_2 },
    { "file_replace_1 temp name", "file.h Error Handling, ONE EXCEPTION: an internal working buffer", _probe_replace_temp_name },
    { "file_read_to_char 1-byte fallback", "file.c _file_read_to_raw: the fallback is memory_alloc on purpose", _probe_read_to_char_fallback },
    { "file_read_to_string", "FILE_READ_BYTES_MAX: the allocation behind the readers aborts", _probe_read_to_string },
    { "file_read_to_str", "FILE_READ_BYTES_MAX: the allocation behind the readers aborts", _probe_read_to_str },
    { "file_read_all_3", "FILE_READ_BYTES_MAX: the allocation behind the readers aborts", _probe_read_all_3 },
    { "file_align_read_to_string", "file.c _file_align_read_to: string_init_2 is the aborting memory_alloc", _probe_align_read },
    { "file_exists_2", "no recover contract: CFW's default memory_alloc abort", _probe_exists_2 },
    { "file_size_3", "no recover contract: CFW's default memory_alloc abort", _probe_size_3 },
    { "file_modified_2", "no recover contract: CFW's default memory_alloc abort", _probe_modified_2 },
};

#ifdef __linux__
/* Run PROBE in a child with the checked build's memory_alloc emulated. The UNARMED control must
 * exit 0 - the site is reachable and healthy - so the armed child's SIGABRT is attributable to
 * the refused allocation reaching the aborting twin, not to anything else on the path. */
static void _probe_expect_abort(Test *const test, ProbeRow const *const row) {
    int statuses[2] = { -1, -1 };

    for (USize armed = 0; armed < 2; armed += 1) {
        // Flushed so the child cannot replay the parent's buffered transcript on its way out.
        fflush(nullptr);

        pid_t const child = fork();

        if (child == 0) {
            _production_abort = true;

            row->probe(armed == 1);

            _exit(armed == 1 ? _TEST_EXIT_ARMED_DEFECT : 0); // the armed child reaching here is the defect.
        }

        if (child > 0) {
            waitpid(child, &statuses[armed], 0);
        }
    }

    test_expect_true(test, _label(row->name, "the unarmed control runs to completion"), WIFEXITED(statuses[0]) && WEXITSTATUS(statuses[0]) == 0);
    test_expect_true(test, _label(row->name, "the refused allocation aborts (SIGABRT)"), WIFSIGNALED(statuses[1]) && WTERMSIG(statuses[1]) == SIGABRT);
}
#endif // __linux__

static void _test_abort_contracts(Test *const test) {
    test_case_begin(test, "documented abort contracts end the process (checked build emulated)");

    test_expect_true(test, "the probe source is written", _fixture_write(_TEST_PROBE_SOURCE, _TEST_CONTENT_PROBE, 'z') && _fixture_write(_TEST_PROBE_REMOVE, _TEST_CONTENT_PROBE, 'y'));

    for (USize i = 0; i < sizeof(_probes) / sizeof(_probes[0]); i += 1) {
#ifdef __linux__
        _probe_expect_abort(test, &_probes[i]);

        // A control child's copy or replace changed the disk; reset for the next row.
        remove(_TEST_PROBE_COPY_DESTINATION);
        remove(_TEST_PROBE_REPLACE_DESTINATION);
        _fixture_write(_TEST_PROBE_REMOVE, _TEST_CONTENT_PROBE, 'y');
#else
        // The _2/_3 and temp-name rows are still pinned in-process by the degradation section's
        // twin count; the reader rows have no in-process twin at all.
        log_message_1(LOG_LEVEL_WARN, "SKIPPED: abort probe %s (%s) - no fork() on Windows to contain the death, and in this unchecked build the twin returns nullptr "
            "instead of aborting (a String reader would then dereference it).\n", _probes[i].name, _probes[i].contract);
#endif // __linux__
    }

    remove(_TEST_PROBE_SOURCE);
    remove(_TEST_PROBE_REMOVE);
    remove(_TEST_PROBE_COPY_DESTINATION);
    remove(_TEST_PROBE_REPLACE_DESTINATION);

    test_case_end(test);
}

static void _test_ledger_invariants(Test *const test) {
    test_case_begin(test, "the instrument stayed honest for the whole run");

    // Last case, so these cover every allocation the binary made. An overflowed ledger would make
    // every zero-live check above a vacuous pass.
    test_expect_u(test, "the ledger never overflowed", 0, _ledger_overflow);
    test_expect_u(test, "no double free anywhere", 0, _double_free_count);
    test_expect_u(test, "no free of an untracked block", 0, _foreign_free_count);
    test_expect_u(test, "nothing linked here allocated outside calloc", 0, _other_calls);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Runner
 *============================================================================*/

int main(void) {
    /* MinGW's crtexe startup object is linked INTO this binary, so its argv copy (two mallocs,
     * measured: argc + 1 pointers, then the program path) reaches the wrapper before main runs.
     * Not traffic of anything under test - counting starts here. */
    _other_calls = 0;

    LogConfig const log_config = {
        .level             = LOG_LEVEL_WARN,
        .stream            = LOG_STREAM_STDOUT,
        .timestamp_enabled = false,
        .autoflush         = true
    };

    log_init(log_config);

    Test test = test_init("tests/file/test_oom.c");

    test_suite_begin(&test, "file allocation failure");

    test_section_begin(&test, "the instrument");
    _test_ledger_find_returns_the_newest_record(&test);
    _test_injector_fires(&test);
    test_section_end(&test);

    test_case_begin(&test, "fixture: the scratch directory");

    bool const ready = test_expect_true(&test, "the fixture directory is entered", _enter_fixture());

    test_case_end(&test);

    if (ready) {
        _wrapper_names();

        test_section_begin(&test, "allocates nothing");
        _test_handle_first_family_allocates_nothing(&test);
        test_section_end(&test);

        test_section_begin(&test, "recoverable out of memory");
        _test_copy_buffer_refused(&test);
        _test_replace_buffer_refused(&test);
        _test_raw_readers_degrade_to_empty(&test);
        _test_addressable_conversion_fails_closed(&test);
        test_section_end(&test);

        test_section_begin(&test, "documented-abort sites: unchecked degradation");
        _test_wrapper_copy_degrades(&test);
        _test_wrapper_rename_degrades(&test);
        _test_wrapper_remove_and_set_modified_degrade(&test);
        _test_wrapper_replace_degrades(&test);
        test_section_end(&test);

        test_section_begin(&test, "unrecoverable out of memory (abort contracts)");
        _test_abort_contracts(&test);
        test_section_end(&test);

        _leave_fixture();
    }

    test_section_begin(&test, "the instrument, afterwards");
    _test_ledger_invariants(&test);
    test_section_end(&test);

    test_suite_end(&test);

    return test_uninit(&test);
}