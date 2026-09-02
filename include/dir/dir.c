#include <dir/dir.h>

/*==============================================================================
 * MARK: - Macros and Constants
 *============================================================================*/
#define _DIR_COPY_CHUNK_SIZE           65536
#define _DIR_CURRENT_CAPACITY          4096
#define _DIR_ENTRIES_GROWTH_FACTOR     2
#define _DIR_ENTRIES_INITIAL_CAPACITY  16
// FILETIME ticks between 1601-01-01 and the Unix epoch, and ticks per second (100 ns each).
#define _DIR_EPOCH_FILETIME_TICKS      116444736000000000ULL
#define _DIR_FILETIME_TICKS_PER_SECOND 10000000ULL
/* Recursion ceiling for the two tree walks. Depth is filesystem-driven and an unprivileged
 * caller can create it with nested mkdir, so without a bound the walk's depth is attacker-chosen.
 * Windows used to be protected only accidentally, by MAX_PATH cutting paths off around 130
 * levels. 512 is far past any real tree while keeping the worst-case frame count bounded. */
#define _DIR_RECURSE_MAX_DEPTH         512
#define _DIR_REMOVE_RETRY_COUNT        10
#define _DIR_REMOVE_RETRY_MS           50
// Wide characters appended to a directory path to build a search pattern: separator, the '*'
// wildcard, and the terminator.
#define _DIR_WILDCARD_SUFFIX_SIZE      3

/*==============================================================================
 * MARK: - Private Functions
 *============================================================================*/
/* Last-error capture/restore, used to bracket a free.
 *
 * The header promises that a false return leaves the OS reason readable, and free() is expressly
 * permitted to overwrite errno / GetLastError. Every _2/_3 forwarder releases its converted path
 * AFTER the _1 form has returned, so without bracketing, the one allocation the overload itself
 * introduced would destroy the diagnostic the caller was promised - and only for the Str/String
 * overloads, making the contract silently depend on which spelling was used. */
static USize _dir_last_error(void) {
#ifdef __linux__
    return (USize) errno;
#elif OS_WINDOWS
    return (USize) GetLastError();
#endif
}

static void _dir_last_error_set(USize const reason) {
#ifdef __linux__
    errno = (int) reason;
#elif OS_WINDOWS
    SetLastError((DWORD) reason);
#endif
}

// An allocation failure has no syscall behind it to have set a reason, so the module sets one
// itself - otherwise the guarantee above would hold for every failure except running out of
// memory, which is the one a caller is least able to diagnose from the outside.
static void _dir_last_error_set_oom(void) {
#ifdef __linux__
    errno = ENOMEM;
#elif OS_WINDOWS
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
#endif
}

// Append one entry, growing the array geometrically. Returns false only when the growth
// allocation fails; the caller owns everything pushed so far and must release it.
static bool _dir_entries_push(DirEntry **const entries, USize *const count, USize *const capacity, DirEntry const *const item) {
    trace_log_push(LOG_METADATA);

    if (*count == *capacity) {
        USize const next = *capacity == 0 ? _DIR_ENTRIES_INITIAL_CAPACITY : *capacity * _DIR_ENTRIES_GROWTH_FACTOR;

        // Through a temporary: memory_realloc returns nullptr WITHOUT freeing the old block,
        // so assigning the result straight back would drop the only reference to it.
        /* memory_try_alloc, not memory_alloc: memory_alloc aborts internally on failure, which
         * would leave the false-return below reachable only through the realloc branch - the
         * contract this function documents has to hold for the FIRST push too. */
        DirEntry *const grown = *capacity == 0
            ? (DirEntry*) memory_try_alloc(next * sizeof(DirEntry))
            : (DirEntry*) memory_realloc(*entries, *capacity * sizeof(DirEntry), next * sizeof(DirEntry));

        /* Deliberately NO error_check_null here: it aborts under ERROR_CHECK_ENABLED, which
         * every build in the tree defines, so it would make the documented "returns false on
         * a failed grow" contract - and the caller's cleanup path - dead code. The entry count
         * is filesystem-driven, so a failed grow is a recoverable condition for a library
         * function, not a caller bug worth killing the process over. */
        if (memory_empty(grown)) {
            trace_log_pop();

            return false;
        }

        *entries  = grown;
        *capacity = next;
    }

    (*entries)[*count] = *item;
    *count            += 1;

    trace_log_pop();

    return true;
}

#if OS_WINDOWS
// UTF-8 copy of a counted wide name, or nullptr when it cannot be converted. The count is a
// parameter rather than re-derived here because the caller has already measured the name for
// its "." / ".." test, so passing it avoids a second scan.
static char* _dir_utf8_from_wide(WCHAR const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    // char_new_1 treats a zero size as a caller bug, and an empty name is not a real entry
    // anyway - report it as unconvertible rather than allocating a lone terminator.
    if (data_size == 0) {
        trace_log_pop();

        return nullptr;
    }

    /* WC_ERR_INVALID_CHARS, not 0. With flag 0 the CP_UTF8 conversion NEVER fails - it silently
     * substitutes U+FFFD for anything it cannot encode, so an NTFS name containing an unpaired
     * surrogate (which the filesystem permits) came back as a mangled UTF-8 string reported as a
     * successful entry. Re-converting that name for a later remove or copy then addresses a path
     * that does not exist. The flag is what makes the module's fail-don't-skip contract real;
     * without it every nullptr check below guarded a branch that could not fire. */
    int const needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, data, (int) data_size, nullptr, 0, nullptr, nullptr);

    if (needed <= 0) {
        trace_log_pop();

        return nullptr;
    }

    /* memory_try_alloc, not char_new_1: this is the per-entry name on every Windows walk, so it
     * is filesystem-driven and must fail recoverably like the entry array. The +1 is the
     * terminator char_new_1 used to add implicitly. */
    char *const utf8 = (char*) memory_try_alloc((USize) needed + 1);

    if (memory_empty(utf8)) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);

        trace_log_pop();

        return nullptr;
    }

    // The buffer is zeroed, so an unchecked failure here would silently yield an empty name
    // rather than garbage - report it instead, so the caller sees an unconvertible entry.
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, data, (int) data_size, utf8, needed, nullptr, nullptr) <= 0) {
        /* Bracketed for the same reason as _dir_wide_from_utf8's twin below: char_delete frees,
         * and a free may overwrite the ERROR_NO_UNICODE_TRANSLATION the conversion just set. The
         * listing path is inside the header's guaranteed list, so an unconvertible name would
         * otherwise surface a false with a stale reason. */
        DWORD const status = GetLastError();

        char_delete(utf8);

        SetLastError(status);

        trace_log_pop();

        return nullptr;
    }

    utf8[needed] = '\0';

    trace_log_pop();

    return utf8;
}

/* Wide copy of a UTF-8 path, or nullptr when it cannot be converted. Release with memory_free.
 *
 * GUARANTEE: every nullptr return leaves a meaningful GetLastError set. That is what lets the
 * nine Win32 wrappers below simply forward a null instead of each repeating a SetLastError
 * prologue, and it is the foundation of the module's documented last-error contract - a caller
 * that gets false from dir_* can read the real OS reason. */
static WCHAR* _dir_wide_from_utf8(char const *const path) {
    trace_log_push(LOG_METADATA);

    /* MB_ERR_INVALID_CHARS for the same reason as the reverse conversion: with flag 0 an
     * ill-formed UTF-8 path is silently repaired into U+FFFD rather than rejected, so the module
     * would operate on a path the caller never asked for. MultiByteToWideChar sets
     * ERROR_NO_UNICODE_TRANSLATION itself when this fires, so the guarantee above holds. */
    // -1 converts through the terminator, so `needed` already counts it.
    int const needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, nullptr, 0);

    if (needed <= 0) {
        trace_log_pop();

        return nullptr;
    }

    WCHAR *const wide = (WCHAR*) memory_try_alloc((USize) needed * sizeof(WCHAR));

    if (memory_empty(wide)) {
        /* ERROR_NOT_ENOUGH_MEMORY, not ERROR_INVALID_NAME. This is an allocation failure, and
         * once the header promises the last error is readable, result_from_os() would otherwise
         * classify an out-of-memory condition as a malformed path - sending a caller to fix the
         * one thing that was never wrong. */
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);

        trace_log_pop();

        return nullptr;
    }

    /* Checked for the same reason as the conversion in _dir_utf8_from_wide: a failure here
     * would leave the zeroed buffer holding up to `needed` non-NUL wide characters, which
     * GetFileAttributesW and _dir_wide_length would then both read past the allocation. It
     * takes another thread mutating the caller's path between the two calls to reach - which
     * the header already forbids - so this is defence in depth, not a live bug. */
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, needed) <= 0) {
        DWORD const status = GetLastError();

        memory_free(wide);

        SetLastError(status);

        trace_log_pop();

        return nullptr;
    }

    trace_log_pop();

    return wide;
}

// CFW has no wide-string module, and this is the only place that needs a wide length; a
// four-line count is preferable to making dir the module that drags in <wchar.h>.
static USize _dir_wide_length(WCHAR const *const data) {
    trace_log_push(LOG_METADATA);

    USize size = 0;

    while (data[size] != L'\0') {
        size += 1;
    }

    trace_log_pop();

    return size;
}

static bool _dir_entry_name_valid_wide(WCHAR const *const name, USize const name_size) {
    trace_log_push(LOG_METADATA);

    bool const dot     = name_size == 1 && name[0] == L'.';
    bool const dot_dot = name_size == 2 && name[0] == L'.' && name[1] == L'.';

    trace_log_pop();

    return !dot && !dot_dot;
}

// FILETIME is 100 ns ticks since 1601-01-01; convert to Unix epoch seconds.
static I64 _dir_epoch_from_filetime(U64 const ticks) {
    trace_log_push(LOG_METADATA);

    /* Anything at or below the epoch reports 0 ("not collected") rather than wrapping: the
     * subtraction is unsigned, so a zeroed LastWriteTime - which some redirectors and cloud
     * file providers do return - would otherwise underflow into the year ~60000 and pin the
     * entry as permanently newest for any caller sorting or expiring by mtime. */
    I64 const seconds = ticks < _DIR_EPOCH_FILETIME_TICKS ? 0 : (I64) ((ticks - _DIR_EPOCH_FILETIME_TICKS) / _DIR_FILETIME_TICKS_PER_SECOND);

    trace_log_pop();

    return seconds;
}

/* Win32 boundary wrappers. The module keeps ONE path representation - UTF-8 char* - and only
 * converts at the OS edge, so the cross-platform logic above stays untouched while every call
 * gains the wide API's ability to express a name outside the active code page.
 *
 * Each wrapper preserves GetLastError across its own memory_free: callers here inspect the
 * error afterwards (ERROR_ALREADY_EXISTS, ERROR_SHARING_VIOLATION, ...), and free() is allowed
 * to overwrite the thread's last-error value. */
static DWORD _dir_win_attributes(char const *const path) {
    trace_log_push(LOG_METADATA);

    WCHAR *const wide = _dir_wide_from_utf8(path);

    if (memory_empty(wide)) {
        // _dir_wide_from_utf8 has already set a meaningful last error; forward it untouched.
        trace_log_pop();

        return INVALID_FILE_ATTRIBUTES;
    }

    DWORD const attributes = GetFileAttributesW(wide);
    DWORD const status     = GetLastError();

    memory_free(wide);
    SetLastError(status);

    trace_log_pop();

    return attributes;
}

static bool _dir_win_create_directory(char const *const path) {
    trace_log_push(LOG_METADATA);

    WCHAR *const wide = _dir_wide_from_utf8(path);

    if (memory_empty(wide)) {
        // _dir_wide_from_utf8 has already set a meaningful last error; forward it untouched.
        trace_log_pop();

        return false;
    }

    bool const created = CreateDirectoryW(wide, nullptr) != 0;
    DWORD const status = GetLastError();

    memory_free(wide);
    SetLastError(status);

    trace_log_pop();

    return created;
}

static bool _dir_win_remove_directory(char const *const path) {
    trace_log_push(LOG_METADATA);

    WCHAR *const wide = _dir_wide_from_utf8(path);

    if (memory_empty(wide)) {
        // _dir_wide_from_utf8 has already set a meaningful last error; forward it untouched.
        trace_log_pop();

        return false;
    }

    bool const removed = RemoveDirectoryW(wide) != 0;
    DWORD const status = GetLastError();

    memory_free(wide);
    SetLastError(status);

    trace_log_pop();

    return removed;
}

static bool _dir_win_delete_file(char const *const path) {
    trace_log_push(LOG_METADATA);

    WCHAR *const wide = _dir_wide_from_utf8(path);

    if (memory_empty(wide)) {
        // _dir_wide_from_utf8 has already set a meaningful last error; forward it untouched.
        trace_log_pop();

        return false;
    }

    bool const deleted = DeleteFileW(wide) != 0;
    DWORD const status = GetLastError();

    memory_free(wide);
    SetLastError(status);

    trace_log_pop();

    return deleted;
}

static bool _dir_win_copy_file(char const *const path, char const *const path_new) {
    trace_log_push(LOG_METADATA);

    /* Converted SEQUENTIALLY, first checked before the second is attempted. Running both
     * unconditionally meant a failure in the first had its error overwritten by whatever the
     * second left behind, which is why this used to flatten both cases to ERROR_INVALID_NAME.
     * Now that _dir_wide_from_utf8 guarantees a meaningful error, flattening would DESTROY the
     * distinction it provides - an out-of-memory would be reported as a malformed path. */
    WCHAR *const wide = _dir_wide_from_utf8(path);

    if (memory_empty(wide)) {
        trace_log_pop();

        return false;
    }

    WCHAR *const wide_new = _dir_wide_from_utf8(path_new);

    if (memory_empty(wide_new)) {
        DWORD const reason = GetLastError();

        memory_free(wide);

        SetLastError(reason);

        trace_log_pop();

        return false;
    }

    bool const copied  = CopyFileW(wide, wide_new, FALSE) != 0;
    DWORD const status = GetLastError();

    memory_free(wide);
    memory_free(wide_new);

    SetLastError(status);

    trace_log_pop();

    return copied;
}

// Directories only - dir_rename_1 gates on dir_exists_1 before calling. That gate is what makes
// MOVEFILE_COPY_ALLOWED safe: its "cross-volume copy succeeded but the source could not be
// deleted, so return success anyway" case is a FILE behaviour, and a cross-volume directory move
// fails outright instead. Point this at a file and a rename could leave a duplicate behind.
static bool _dir_win_rename(char const *const path, char const *const path_new) {
    trace_log_push(LOG_METADATA);

    // Sequential conversion for the same reason as _dir_win_copy_file: flattening both failures
    // to one error code would discard the distinction _dir_wide_from_utf8 now guarantees.
    WCHAR *const wide = _dir_wide_from_utf8(path);

    if (memory_empty(wide)) {
        trace_log_pop();

        return false;
    }

    WCHAR *const wide_new = _dir_wide_from_utf8(path_new);

    if (memory_empty(wide_new)) {
        DWORD const reason = GetLastError();

        memory_free(wide);

        SetLastError(reason);

        trace_log_pop();

        return false;
    }

    // MOVEFILE_COPY_ALLOWED so a rename across volumes still succeeds, matching what the CRT
    // rename() did on this platform.
    bool const renamed = MoveFileExW(wide, wide_new, MOVEFILE_COPY_ALLOWED) != 0;
    DWORD const status = GetLastError();

    memory_free(wide);
    memory_free(wide_new);

    SetLastError(status);

    trace_log_pop();

    return renamed;
}

static bool _dir_win_set_current(char const *const path) {
    trace_log_push(LOG_METADATA);

    WCHAR *const wide = _dir_wide_from_utf8(path);

    if (memory_empty(wide)) {
        // _dir_wide_from_utf8 has already set a meaningful last error; forward it untouched.
        trace_log_pop();

        return false;
    }

    bool const changed = SetCurrentDirectoryW(wide) != 0;
    DWORD const status = GetLastError();

    memory_free(wide);
    SetLastError(status);

    trace_log_pop();

    return changed;
}

// Whether the path is a directory that is NOT a symlink or junction. The reparse TAG is only
// available from an enumeration record, not from GetFileAttributes, which is why this opens a
// find handle on the path itself rather than reading attributes.
static bool _dir_win_is_real_directory(char const *const path) {
    trace_log_push(LOG_METADATA);

    WCHAR *const wide = _dir_wide_from_utf8(path);

    if (memory_empty(wide)) {
        // _dir_wide_from_utf8 has already set a meaningful last error; forward it untouched.
        trace_log_pop();

        return false;
    }

    WIN32_FIND_DATAW info = DEFAULT_INITIALIZATION;

    HANDLE const find   = FindFirstFileW(wide, &info);
    DWORD  const status = GetLastError();

    memory_free(wide);

    /* Restored like every sibling wrapper. This one alone freed before reading `find`, so the
     * FindFirstFileW failure reason was destroyed by free() - which made the block comment above
     * ("each wrapper preserves GetLastError across its own memory_free") false as written. Not
     * observable through the public contract today, since only the recursive walks call this and
     * they are excluded from the guarantee, but a false invariant fails silently the moment a
     * single-operation function starts using it. */
    SetLastError(status);

    if (find == INVALID_HANDLE_VALUE) {
        trace_log_pop();

        return false;
    }

    bool const directory = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    bool const link      = (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 && IsReparseTagNameSurrogate(info.dwReserved0);

    FindClose(find);

    trace_log_pop();

    return directory && !link;
}

// Wide "<path>\*" search pattern for the enumeration loops, or nullptr. Release with memory_free.
static WCHAR* _dir_win_search_pattern(char const *const path) {
    trace_log_push(LOG_METADATA);

    WCHAR *const wide = _dir_wide_from_utf8(path);

    if (memory_empty(wide)) {
        // _dir_wide_from_utf8 has already set a meaningful last error; forward it untouched.
        trace_log_pop();

        return nullptr;
    }

    USize const wide_size = _dir_wide_length(wide);
    WCHAR *const search   = (WCHAR*) memory_try_alloc((wide_size + _DIR_WILDCARD_SUFFIX_SIZE) * sizeof(WCHAR));

    if (memory_empty(search)) {
        /* Sets the error itself, unlike the branch above which forwards one. This is the second
         * allocating helper, and the guarantee "a null return leaves a meaningful last error"
         * has to hold for BOTH of them or a caller cannot rely on it at all - here the failure
         * originates locally, so there is nothing to forward. */
        memory_free(wide);

        SetLastError(ERROR_NOT_ENOUGH_MEMORY);

        trace_log_pop();

        return nullptr;
    }

    for (USize i = 0; i < wide_size; i += 1) {
        search[i] = wide[i];
    }

    search[wide_size]     = L'\\';
    search[wide_size + 1] = L'*';
    search[wide_size + 2] = L'\0';

    memory_free(wide);

    trace_log_pop();

    return search;
}

// The wide search enumeration. This is deliberately the ANSI FindFirstFileA loop translated
// to FindFirstFileW rather than the batched GetFileInformationByHandleEx form: batching was
// measured against this on local NTFS at 1.0x (Win32 already buffers directory data
// internally), so it bought only a second code path, a fallback, and a CreateFileW handle
// that had to be guarded against being pointed at a named pipe. The wide API is what actually
// matters here - it is the only way a name outside the active code page survives.
static bool _dir_list_entries_find(WCHAR const *const search, DirEntry **const out_entries, USize *const out_count) {
    trace_log_push(LOG_METADATA);

    DirEntry *entries = nullptr;
    USize count       = 0;
    USize capacity    = 0;
    bool ok           = true;

    WIN32_FIND_DATAW data = DEFAULT_INITIALIZATION;

    HANDLE const find = FindFirstFileW(search, &data);

    if (find == INVALID_HANDLE_VALUE) {
        trace_log_pop();

        return false;
    }

    do {
        USize const name_size = _dir_wide_length(data.cFileName);

        if (!_dir_entry_name_valid_wide(data.cFileName, name_size)) {
            continue;
        }

        DirEntry item = DEFAULT_INITIALIZATION;

        item.name = _dir_utf8_from_wide(data.cFileName, name_size);

        if (memory_empty(item.name)) {
            ok = false;

            break;
        }

        item.is_dir  = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        item.is_link = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 && IsReparseTagNameSurrogate(data.dwReserved0);
        item.size    = ((USize) data.nFileSizeHigh << 32) | (USize) data.nFileSizeLow;

        ULARGE_INTEGER write_time = DEFAULT_INITIALIZATION;

        write_time.LowPart  = data.ftLastWriteTime.dwLowDateTime;
        write_time.HighPart = data.ftLastWriteTime.dwHighDateTime;

        item.mtime = _dir_epoch_from_filetime(write_time.QuadPart);

        /* A link's fields are RESOLVED to its target, matching the documented contract and the
         * Linux path. FindFirstFileW reports the link's own record: is_dir false for a directory
         * symlink, size ~0, and the link's own mtime - so arbor --size showed 0 for a symlinked
         * file on Windows and the real size on Linux, from one API that claims to follow the
         * target. Only entries that are actually links pay the extra call, which is the cost the
         * header already budgets ("plus one per link"). A broken link keeps the link's own
         * record: the target cannot be measured, and reporting zeroes would be a worse lie. */
        if (item.is_link) {
            /* Built in WIDE from the search pattern rather than by converting the UTF-8 name
             * back: `search` is already "<parent><sep>*", so dropping its trailing '*' yields the
             * parent prefix and the enumeration hands us the name as WCHAR. Round-tripping
             * through UTF-8 would add two conversions per link and a failure mode for names the
             * conversion rejects - on a path where the name is already known-good. */
            USize const prefix_size = _dir_wide_length(search) - 1;
            WCHAR *const target_path = (WCHAR*) memory_try_alloc((prefix_size + name_size + 1) * sizeof(WCHAR));

            if (!memory_empty(target_path)) {
                for (USize i = 0; i < prefix_size; i += 1) {
                    target_path[i] = search[i];
                }

                for (USize i = 0; i < name_size; i += 1) {
                    target_path[prefix_size + i] = data.cFileName[i];
                }

                target_path[prefix_size + name_size] = L'\0';

                WIN32_FILE_ATTRIBUTE_DATA target = DEFAULT_INITIALIZATION;

                // GetFileAttributesExW follows the reparse point, unlike the enumeration.
                if (GetFileAttributesExW(target_path, GetFileExInfoStandard, &target) != 0) {
                    ULARGE_INTEGER target_time = DEFAULT_INITIALIZATION;

                    target_time.LowPart  = target.ftLastWriteTime.dwLowDateTime;
                    target_time.HighPart = target.ftLastWriteTime.dwHighDateTime;

                    item.is_dir = (target.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                    item.size   = ((USize) target.nFileSizeHigh << 32) | (USize) target.nFileSizeLow;
                    item.mtime  = _dir_epoch_from_filetime(target_time.QuadPart);
                }

                memory_free(target_path);
            }
        }

        if (!_dir_entries_push(&entries, &count, &capacity, &item)) {
            char_delete(item.name);

            /* Set AFTER the free, and set at all: this exit had no error of its own, and the
             * char_delete above runs before the capture further down - so a caller reading the
             * last error after a failed listing got a stale value, possibly a SUCCESS. The Linux
             * twin already did this; the asymmetry made dir_list_entries the one function in the
             * header's guaranteed list that did not honour the guarantee on Windows. */
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);

            ok = false;

            break;
        }
    } while (FindNextFileW(find, &data));

    // Same reason as the Linux branch: FindClose and the entry release both free, and they run
    // between the failure and the caller's own bracket in dir_list_entries_1.
    USize const reason = _dir_last_error();

    FindClose(find);

    if (!ok) {
        dir_list_entries_uninit(entries, count);

        _dir_last_error_set(reason);

        trace_log_pop();

        return false;
    }

    *out_entries = entries;
    *out_count   = count;

    trace_log_pop();

    return true;
}
#endif // OS_WINDOWS

static bool _dir_entry_name_valid(char const *const name) {
    trace_log_push(LOG_METADATA);

    bool const dot     = name[0] == '.' && name[1] == '\0';
    bool const dot_dot = name[0] == '.' && name[1] == '.' && name[2] == '\0';

    trace_log_pop();

    return !dot && !dot_dot;
}

static char* _dir_path_join(char const *const parent, USize const parent_size, char const *const name, USize const name_size) {
    trace_log_push(LOG_METADATA);

    /* memory_try_alloc, not char_new_1. char_new_1 aborts internally, which put this module in
     * the position of documenting a recoverable false-return in _dir_entries_push while an
     * adjacent allocation in the SAME loop killed the process - so under identical memory
     * pressure the contract was "returns false" or "dies" depending on which line failed first.
     * One policy: every allocation here is recoverable, and callers null-check. */
    char *const path = (char*) memory_try_alloc(parent_size + 1 + name_size + 1);

    if (memory_empty(path)) {
        trace_log_pop();

        return nullptr;
    }

    char_copy_2(path, parent, parent_size);

    path[parent_size] = '/';

    char_copy_2(path + parent_size + 1, name, name_size);

    path[parent_size + 1 + name_size] = '\0';

    trace_log_pop();

    return path;
}

static bool _dir_copy_file(char const *const path, char const *const path_new) {
    trace_log_push(LOG_METADATA);

    bool copied = false;

#ifdef __linux__
    FILE *const source = fopen(path, "rb");

    if (!memory_empty(source)) {
        FILE *const destination = fopen(path_new, "wb");

        if (!memory_empty(destination)) {
            // memory_try_alloc, not char_new_1: one recoverable OOM policy across the module.
            char *const buffer = (char*) memory_try_alloc(_DIR_COPY_CHUNK_SIZE);

            if (!memory_empty(buffer)) {
                copied = true;

                for (USize read = fread(buffer, 1, _DIR_COPY_CHUNK_SIZE, source); read > 0; read = fread(buffer, 1, _DIR_COPY_CHUNK_SIZE, source)) {
                    copied = fwrite(buffer, 1, read, destination) == read && copied;
                }

                copied = ferror(source) == 0 && copied;

                memory_free(buffer);
            }

            fclose(destination);
        }

        fclose(source);
    }
#elif OS_WINDOWS
    copied = _dir_win_copy_file(path, path_new);
#endif

    trace_log_pop();

    return copied;
}

static bool _dir_copy_all_recurse(char const *const path, char const *const path_new, USize const depth) {
    trace_log_push(LOG_METADATA);

    // Refuse rather than descend: matching remove_all's posture, an over-deep tree is reported
    // as a failed copy instead of being partially copied or overflowing the stack.
    if (depth > _DIR_RECURSE_MAX_DEPTH) {
        trace_log_pop();

        return false;
    }

    USize const path_size     = char_length(path);
    USize const path_new_size = char_length(path_new);

    bool copied = dir_create_1(path_new);

#ifdef __linux__
    DIR *const dir = opendir(path);

    copied = copied && !memory_empty(dir);

    if (copied) {
        for (struct dirent const *entry = readdir(dir); !memory_empty(entry); entry = readdir(dir)) {
            if (!_dir_entry_name_valid(entry->d_name)) {
                continue;
            }

            USize const name_size = char_length(entry->d_name);

            char *const child     = _dir_path_join(path, path_size, entry->d_name, name_size);
            char *const child_new = _dir_path_join(path_new, path_new_size, entry->d_name, name_size);

            /* One recoverable OOM policy: _dir_path_join can now return null, so the entry fails
             * rather than the process dying mid-walk. Frees the sibling so a failure here does
             * not leak the join that did succeed. */
            if (memory_empty(child) || memory_empty(child_new)) {
                /* char_delete, NOT memory_free. memory_free error_checks its argument and aborts
                 * on null - deliberately, as a bug-catcher for a caller that lost a pointer - so
                 * freeing both here killed the process on every entry, since the branch is
                 * reached precisely when one of them is null. char_delete routes through
                 * allocator_release, which treats null as a no-op. */
                char_delete(child);
                char_delete(child_new);

                copied = false;

                continue;
            }

            struct stat info = DEFAULT_INITIALIZATION;

            if (lstat(child, &info) != 0) {
                copied = false;
            }
            else if (S_ISLNK(info.st_mode)) {
                /* Heap, not a 4 KiB stack array. The compiler reserves the maximum frame at
                 * function entry, so a block-scoped buffer here was paid by EVERY recursion
                 * frame whether or not the branch ran - at the old unbounded depth that reached
                 * the whole default stack. On the heap only actual links pay for it. */
                char *const target = (char*) memory_try_alloc(_DIR_CURRENT_CAPACITY);

                if (memory_empty(target)) {
                    copied = false;
                }
                else {
                    ISize const target_size = (ISize) readlink(child, target, _DIR_CURRENT_CAPACITY - 1);

                    unlink(child_new);

                    copied = target_size > 0 && symlink(target, child_new) == 0 && copied;

                    memory_free(target);
                }
            }
            else if (S_ISDIR(info.st_mode)) {
                copied = _dir_copy_all_recurse(child, child_new, depth + 1) && copied;
            }
            else if (S_ISREG(info.st_mode)) {
                copied = _dir_copy_file(child, child_new) && copied;
            }
            else {
                copied = false;
            }

            char_delete(child);
            char_delete(child_new);
        }
    }

    if (!memory_empty(dir)) {
        closedir(dir);
    }
#elif OS_WINDOWS
    WCHAR *const search = _dir_win_search_pattern(path);

    WIN32_FIND_DATAW data = DEFAULT_INITIALIZATION;

    HANDLE const find = !memory_empty(search) ? FindFirstFileW(search, &data) : INVALID_HANDLE_VALUE;

    if (!memory_empty(search)) {
        memory_free(search);
    }

    copied = copied && find != INVALID_HANDLE_VALUE;

    if (copied) {
        do {
            // Decoded to UTF-8 immediately so everything below stays on the module's single
            // char* path representation; an unconvertible name fails the copy rather than
            // being silently skipped.
            char *const name = _dir_utf8_from_wide(data.cFileName, _dir_wide_length(data.cFileName));

            if (memory_empty(name)) {
                copied = false;

                continue;
            }

            if (!_dir_entry_name_valid(name)) {
                char_delete(name);

                continue;
            }

            USize const name_size = char_length(name);

            char *const child     = _dir_path_join(path, path_size, name, name_size);
            char *const child_new = _dir_path_join(path_new, path_new_size, name, name_size);

            /* One recoverable OOM policy: _dir_path_join can now return null, so the entry fails
             * rather than the process dying mid-walk. Frees the sibling so a failure here does
             * not leak the join that did succeed. */
            if (memory_empty(child) || memory_empty(child_new)) {
                /* char_delete, NOT memory_free. memory_free error_checks its argument and aborts
                 * on null - deliberately, as a bug-catcher for a caller that lost a pointer - so
                 * freeing both here killed the process on every entry, since the branch is
                 * reached precisely when one of them is null. char_delete routes through
                 * allocator_release, which treats null as a no-op. */
                char_delete(child);
                char_delete(child_new);

                // name is freed only on the normal flow below; releasing it here too keeps the
                // OOM path from leaking exactly under the pressure it exists to survive.
                char_delete(name);

                copied = false;

                continue;
            }

            bool const directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            bool const link      = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 && IsReparseTagNameSurrogate(data.dwReserved0);

            if (link) {
                /* Recreating a symlink/junction needs elevation; report failure. */
                copied = false;
            }
            else if (directory) {
                copied = _dir_copy_all_recurse(child, child_new, depth + 1) && copied;
            }
            else {
                copied = _dir_copy_file(child, child_new) && copied;
            }

            char_delete(child);
            char_delete(child_new);
            char_delete(name);
        } while (FindNextFileW(find, &data));
    }

    if (find != INVALID_HANDLE_VALUE) {
        FindClose(find);
    }
#endif

    trace_log_pop();

    return copied;
}

#ifdef OS_WINDOWS
static bool _dir_delete_retry(char const *const path, bool const directory) {
    trace_log_push(LOG_METADATA);

    bool removed = directory ? _dir_win_remove_directory(path) : _dir_win_delete_file(path);

    /* Deletes are asynchronous on Windows and sync clients (Dropbox, OneDrive)
     * hold transient handles on fresh entries; retry briefly before reporting
     * failure. */
    for (USize i = 0; i < _DIR_REMOVE_RETRY_COUNT && !removed; i += 1) {
        DWORD const error = GetLastError();

        if (error != ERROR_DIR_NOT_EMPTY && error != ERROR_SHARING_VIOLATION && error != ERROR_ACCESS_DENIED) {
            break;
        }

        Sleep(_DIR_REMOVE_RETRY_MS);

        removed = directory ? _dir_win_remove_directory(path) : _dir_win_delete_file(path);
    }

    trace_log_pop();

    return removed;
}
#endif

static bool _dir_path_within(char const *const parent, USize const parent_size, char const *const path, USize const path_size) {
    trace_log_push(LOG_METADATA);

    bool within = parent_size > 0 && path_size >= parent_size;

    for (USize i = 0; i < parent_size && within; i += 1) {
        bool const parent_separator = parent[i] == '/' || parent[i] == '\\';
        bool const path_separator   = path[i] == '/' || path[i] == '\\';

        within = (parent_separator && path_separator) || parent[i] == path[i];
    }

    if (within && path_size > parent_size) {
        within = path[parent_size] == '/' || path[parent_size] == '\\';
    }

    trace_log_pop();

    return within;
}

/* True when any segment of the path is "." or "..".
 *
 * Both bulk operations refuse such paths, for the same underlying reason: every containment
 * decision in this module is TEXTUAL. remove_all compares roots against literal prefixes, and
 * copy_all asks whether the destination sits inside the source. A dot segment lets one directory
 * be spelled two ways, which is how `dir_copy_all_1("x", "x/../x/inside")` passed the textual
 * check and copied a tree into its own growing subtree - enumeration semantics for entries
 * created mid-scan are unspecified, so it recursed until a path limit stopped it.
 *
 * Canonicalising both roots would be stronger, but it costs a syscall and a platform split;
 * refusing the ambiguous spelling is the posture remove_all already took. */
static bool _dir_path_has_dot_segment(char const *const path, USize const path_size) {
    trace_log_push(LOG_METADATA);

    bool  found         = false;
    USize segment_start = 0;

    for (USize i = 0; i <= path_size && !found; i += 1) {
        if (i == path_size || path[i] == '/' || path[i] == '\\') {
            USize const segment_size = i - segment_start;

            bool const dot     = segment_size == 1 && path[segment_start] == '.';
            bool const dot_dot = segment_size == 2 && path[segment_start] == '.' && path[segment_start + 1] == '.';

            found = dot || dot_dot;

            segment_start = i + 1;
        }
    }

    trace_log_pop();

    return found;
}

static bool _dir_remove_all_allowed(char const *const path, USize const path_size) {
    trace_log_push(LOG_METADATA);

    bool allowed = path_size > 0;

    if (allowed) {
        bool separators_only = true;

        for (USize i = 0; i < path_size && separators_only; i += 1) {
            separators_only = path[i] == '/' || path[i] == '\\';
        }

        allowed = !separators_only;
    }

    if (allowed && path_size <= 3 && path_size >= 2 && path[1] == ':') {
        allowed = false;
    }

    if (allowed) {
        allowed = !_dir_path_has_dot_segment(path, path_size);
    }

    trace_log_pop();

    return allowed;
}

static bool _dir_remove_all_recurse(char const *const path, USize const depth) {
    trace_log_push(LOG_METADATA);

    // Same ceiling as the copy walk; refusing keeps a deep tree from exhausting the stack.
    if (depth > _DIR_RECURSE_MAX_DEPTH) {
        trace_log_pop();

        return false;
    }

    USize const path_size = char_length(path);

    bool removed = false;

#ifdef __linux__
    DIR *const dir = opendir(path);

    if (!memory_empty(dir)) {
        removed = true;

        for (struct dirent const *entry = readdir(dir); !memory_empty(entry); entry = readdir(dir)) {
            if (!_dir_entry_name_valid(entry->d_name)) {
                continue;
            }

            char *const child = _dir_path_join(path, path_size, entry->d_name, char_length(entry->d_name));

            // One recoverable OOM policy; see the copy walk. A failed join fails the entry.
            if (memory_empty(child)) {
                removed = false;

                continue;
            }

            struct stat info = DEFAULT_INITIALIZATION;

            if (lstat(child, &info) != 0) {
                removed = false;
            }
            else if (S_ISDIR(info.st_mode)) {
                removed = _dir_remove_all_recurse(child, depth + 1) && removed;
            }
            else {
                removed = unlink(child) == 0 && removed;
            }

            char_delete(child);
        }

        closedir(dir);

        removed = rmdir(path) == 0 && removed;
    }
#elif OS_WINDOWS
    WCHAR *const search = _dir_win_search_pattern(path);

    WIN32_FIND_DATAW data = DEFAULT_INITIALIZATION;

    HANDLE const find = !memory_empty(search) ? FindFirstFileW(search, &data) : INVALID_HANDLE_VALUE;

    if (!memory_empty(search)) {
        memory_free(search);
    }

    if (find != INVALID_HANDLE_VALUE) {
        removed = true;

        do {
            /* Decoded to UTF-8 here so the recursion and the delete calls below keep taking a
             * char* path. An unconvertible name FAILS the removal rather than being skipped:
             * silently leaving an entry behind while reporting success is exactly how a
             * "removed" tree keeps a file - the whole reason this module went wide. */
            char *const name = _dir_utf8_from_wide(data.cFileName, _dir_wide_length(data.cFileName));

            if (memory_empty(name)) {
                removed = false;

                continue;
            }

            if (!_dir_entry_name_valid(name)) {
                char_delete(name);

                continue;
            }

            char *const child = _dir_path_join(path, path_size, name, char_length(name));

            // One recoverable OOM policy; see the copy walk. A failed join fails the entry.
            if (memory_empty(child)) {
                // name is owned here and freed only on the normal flow below.
                char_delete(name);

                removed = false;

                continue;
            }

            bool const directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            /* Name-surrogate reparse points only (symlinks, junctions): cloud
             * placeholders (Dropbox, OneDrive) also set the reparse attribute
             * but are real directories/files that must be traversed. */
            bool const link      = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 && IsReparseTagNameSurrogate(data.dwReserved0);

            if (link) {
                removed = _dir_delete_retry(child, directory) && removed;
            }
            else if (directory) {
                removed = _dir_remove_all_recurse(child, depth + 1) && removed;
            }
            else {
                removed = _dir_delete_retry(child, false) && removed;
            }

            char_delete(child);
            char_delete(name);
        } while (FindNextFileW(find, &data));

        FindClose(find);

        removed = _dir_delete_retry(path, true) && removed;
    }
#endif

    trace_log_pop();

    return removed;
}

/*==============================================================================
 * MARK: - Public API
 *============================================================================*/
bool dir_copy_all_1(char const *const path, char const *const path_new) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);
    error_check_null(LOG_METADATA, "path_new", (void*) path_new);

    USize const path_size     = char_length(path);
    USize const path_new_size = char_length(path_new);

    /* Dot segments refused on BOTH roots before the containment test, because that test is
     * textual and a dot segment defeats it outright: "x" and "x/../x" name the same directory
     * while comparing as different prefixes. Without this, copying a tree into its own subtree
     * was reachable through a spelling. */
    bool copied = path_size > 0
        && path_new_size > 0
        && !_dir_path_has_dot_segment(path, path_size)
        && !_dir_path_has_dot_segment(path_new, path_new_size)
        && !_dir_path_within(path, path_size, path_new, path_new_size);

#ifdef __linux__
    struct stat info = DEFAULT_INITIALIZATION;

    copied = copied && lstat(path, &info) == 0 && S_ISDIR(info.st_mode);
#elif OS_WINDOWS
    copied = copied && _dir_win_is_real_directory(path);
#endif

    copied = copied && dir_create_all_1(path_new) && _dir_copy_all_recurse(path, path_new, 0);

    trace_log_pop();

    return copied;
}

bool dir_copy_all_2(Str const *const path, Str const *const path_new) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);
    error_check_null(LOG_METADATA, "path_new", (void*) path_new);

    bool copied = false;

    if (str_get_size(path) > 0 && str_get_size(path_new) > 0) {
        char *const data     = char_new_3(str_get_data(path), str_get_size(path));
        char *const data_new = char_new_3(str_get_data(path_new), str_get_size(path_new));

        copied = dir_copy_all_1(data, data_new);

        // Bracketed: char_delete may overwrite the last error the caller is promised.
        USize const reason = _dir_last_error();

        char_delete(data);
        char_delete(data_new);

        // BOTH frees precede the restore: the sweep that added this bracket assumed one delete
        // per forwarder, so on the two-path overloads the restore landed between them and the
        // second free could still clobber the very value the header guarantees.
        _dir_last_error_set(reason);
    }

    trace_log_pop();

    return copied;
}

bool dir_copy_all_3(String const *const path, String const *const path_new) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);
    error_check_null(LOG_METADATA, "path_new", (void*) path_new);

    bool copied = false;

    if (string_get_size(path) > 0 && string_get_size(path_new) > 0) {
        char *const data     = char_new_3(string_get_data(path), string_get_size(path));
        char *const data_new = char_new_3(string_get_data(path_new), string_get_size(path_new));

        copied = dir_copy_all_1(data, data_new);

        // Bracketed: char_delete may overwrite the last error the caller is promised.
        USize const reason = _dir_last_error();

        char_delete(data);
        char_delete(data_new);

        // BOTH frees precede the restore: the sweep that added this bracket assumed one delete
        // per forwarder, so on the two-path overloads the restore landed between them and the
        // second free could still clobber the very value the header guarantees.
        _dir_last_error_set(reason);
    }

    trace_log_pop();

    return copied;
}

bool dir_create_1(char const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    bool created = false;

    /* The already-exists branch confirms it is a DIRECTORY. Both EEXIST and ERROR_ALREADY_EXISTS
     * also fire when a plain FILE occupies the path, so accepting them outright returned true
     * while the contract ("true when the directory exists after the call") was false. That is
     * live: crm.c gates a file open on this result, so a stray file at an upload path produced
     * a success here and then an unexplained write failure one line later. */
#ifdef __linux__
    created = mkdir(path, 0755) == 0 || (errno == EEXIST && dir_exists_1(path));
#elif OS_WINDOWS
    created = _dir_win_create_directory(path) || (GetLastError() == ERROR_ALREADY_EXISTS && dir_exists_1(path));
#endif

    trace_log_pop();

    return created;
}

bool dir_create_2(Str const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    bool created = false;

    if (str_get_size(path) > 0) {
        char *const data = char_new_3(str_get_data(path), str_get_size(path));

        created = dir_create_1(data);

        // Bracketed: char_delete may overwrite the last error the caller is promised.
        USize const reason = _dir_last_error();

        char_delete(data);

        _dir_last_error_set(reason);
    }

    trace_log_pop();

    return created;
}

bool dir_create_3(String const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    bool created = false;

    if (string_get_size(path) > 0) {
        char *const data = char_new_3(string_get_data(path), string_get_size(path));

        created = dir_create_1(data);

        // Bracketed: char_delete may overwrite the last error the caller is promised.
        USize const reason = _dir_last_error();

        char_delete(data);

        _dir_last_error_set(reason);
    }

    trace_log_pop();

    return created;
}

bool dir_create_all_1(char const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    USize const path_size = char_length(path);

    bool created = path_size > 0;

    if (created) {
        /* memory_try_alloc, not char_new_3. This is an INTERNAL working buffer, not a forwarder's
         * conversion of the caller's own string - and dir_create_all is both in the header's
         * guaranteed list and reachable from _dir_copy_all_recurse, so an abort here killed a
         * recursive copy that the module documents as returning false. The allocation-failure
         * sweep found this; three reading rounds and 257 assertions did not, because nothing had
         * ever made an allocation fail. */
        char *const prefix = (char*) memory_try_alloc(path_size + 1);

        if (memory_empty(prefix)) {
            _dir_last_error_set_oom();

            trace_log_pop();

            return false;
        }

        char_copy_2(prefix, path, path_size);

        prefix[path_size] = '\0';

        for (USize i = 1; i < path_size && created; i += 1) {
            bool const separator = prefix[i] == '/' || prefix[i] == '\\';
            bool const boundary  = separator && prefix[i - 1] != '/' && prefix[i - 1] != '\\' && prefix[i - 1] != ':';

            if (boundary) {
                prefix[i] = '\0';

                created = dir_create_1(prefix);

                prefix[i] = path[i];
            }
        }

        bool const trailing = path[path_size - 1] == '/' || path[path_size - 1] == '\\' || path[path_size - 1] == ':';

        if (created && !trailing) {
            created = dir_create_1(prefix);
        }

        /* Bracketed: dir_create_all is inside the header's guaranteed list, and this free runs
         * after the last dir_create_1 has set the error the caller is promised. */
        USize const reason = _dir_last_error();

        char_delete(prefix);

        _dir_last_error_set(reason);
    }

    trace_log_pop();

    return created;
}

bool dir_create_all_2(Str const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    bool created = false;

    if (str_get_size(path) > 0) {
        char *const data = char_new_3(str_get_data(path), str_get_size(path));

        created = dir_create_all_1(data);

        // Bracketed: char_delete may overwrite the last error the caller is promised.
        USize const reason = _dir_last_error();

        char_delete(data);

        _dir_last_error_set(reason);
    }

    trace_log_pop();

    return created;
}

bool dir_create_all_3(String const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    bool created = false;

    if (string_get_size(path) > 0) {
        char *const data = char_new_3(string_get_data(path), string_get_size(path));

        created = dir_create_all_1(data);

        // Bracketed: char_delete may overwrite the last error the caller is promised.
        USize const reason = _dir_last_error();

        char_delete(data);

        _dir_last_error_set(reason);
    }

    trace_log_pop();

    return created;
}

bool dir_empty_1(char const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    bool empty = false;

#ifdef __linux__
    DIR *const dir = opendir(path);

    if (!memory_empty(dir)) {
        empty = true;

        for (struct dirent const *entry = readdir(dir); !memory_empty(entry) && empty; entry = readdir(dir)) {
            empty = !_dir_entry_name_valid(entry->d_name);
        }

        closedir(dir);
    }
#elif OS_WINDOWS
    USize const path_size = char_length(path);

    if (path_size > 0 && dir_exists_1(path)) {
        WCHAR *const search = _dir_win_search_pattern(path);

        WIN32_FIND_DATAW data = DEFAULT_INITIALIZATION;

        HANDLE const find = !memory_empty(search) ? FindFirstFileW(search, &data) : INVALID_HANDLE_VALUE;

        /* Bracketed like the forwarders: dir_empty is inside the header's guaranteed list, and
         * this free sits between FindFirstFileW and the caller's chance to read the error. */
        if (!memory_empty(search)) {
            USize const reason = _dir_last_error();

            memory_free(search);

            _dir_last_error_set(reason);
        }

        if (find != INVALID_HANDLE_VALUE) {
            empty = true;

            // Emptiness only needs the "." / ".." test, and those are ASCII in every locale -
            // so this compares the wide name directly rather than decoding each entry.
            do {
                empty = !_dir_entry_name_valid_wide(data.cFileName, _dir_wide_length(data.cFileName));
            } while (empty && FindNextFileW(find, &data));

            FindClose(find);
        }
    }
#endif

    trace_log_pop();

    return empty;
}

bool dir_empty_2(Str const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    bool empty = false;

    if (str_get_size(path) > 0) {
        char *const data = char_new_3(str_get_data(path), str_get_size(path));

        empty = dir_empty_1(data);

        // Bracketed: char_delete may overwrite the last error the caller is promised.
        USize const reason = _dir_last_error();

        char_delete(data);

        _dir_last_error_set(reason);
    }

    trace_log_pop();

    return empty;
}

bool dir_empty_3(String const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    bool empty = false;

    if (string_get_size(path) > 0) {
        char *const data = char_new_3(string_get_data(path), string_get_size(path));

        empty = dir_empty_1(data);

        // Bracketed: char_delete may overwrite the last error the caller is promised.
        USize const reason = _dir_last_error();

        char_delete(data);

        _dir_last_error_set(reason);
    }

    trace_log_pop();

    return empty;
}

bool dir_exists_1(char const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    bool exists = false;

#ifdef __linux__
    struct stat info = DEFAULT_INITIALIZATION;

    exists = stat(path, &info) == 0 && S_ISDIR(info.st_mode);
#elif OS_WINDOWS
    DWORD const attributes = _dir_win_attributes(path);

    exists = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#endif

    trace_log_pop();

    return exists;
}

bool dir_exists_2(Str const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    bool exists = false;

    if (str_get_size(path) > 0) {
        char *const data = char_new_3(str_get_data(path), str_get_size(path));

        exists = dir_exists_1(data);

        // Bracketed: char_delete may overwrite the last error the caller is promised.
        USize const reason = _dir_last_error();

        char_delete(data);

        _dir_last_error_set(reason);
    }

    trace_log_pop();

    return exists;
}

bool dir_exists_3(String const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    bool exists = false;

    if (string_get_size(path) > 0) {
        char *const data = char_new_3(string_get_data(path), string_get_size(path));

        exists = dir_exists_1(data);

        // Bracketed: char_delete may overwrite the last error the caller is promised.
        USize const reason = _dir_last_error();

        char_delete(data);

        _dir_last_error_set(reason);
    }

    trace_log_pop();

    return exists;
}

char* dir_get_current(void) {
    trace_log_push(LOG_METADATA);

    char *current = nullptr;

#ifdef __linux__
    char buffer[_DIR_CURRENT_CAPACITY] = DEFAULT_INITIALIZATION;

    if (!memory_empty(getcwd(buffer, sizeof buffer))) {
        current = char_new_2(buffer);
    }
#elif OS_WINDOWS
    // Fetched wide and converted back, so a working directory containing characters outside
    // the active code page is returned faithfully instead of with '?' substitutions.
    WCHAR buffer[_DIR_CURRENT_CAPACITY] = DEFAULT_INITIALIZATION;

    DWORD const size = GetCurrentDirectoryW(_DIR_CURRENT_CAPACITY, buffer);

    if (size > 0 && size < _DIR_CURRENT_CAPACITY) {
        current = _dir_utf8_from_wide(buffer, (USize) size);
    }
#endif

    trace_log_pop();

    return current;
}

bool dir_list_entries_1(char const *const path, DirEntry **const out_entries, USize *const out_count) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);
    error_check_null(LOG_METADATA, "out_entries", (void*) out_entries);
    error_check_null(LOG_METADATA, "out_count", (void*) out_count);

    bool listed       = false;
    DirEntry *entries = nullptr;
    USize count       = 0;

#ifdef __linux__
    // Windows accumulates inside _dir_list_entries_find, which owns its own capacity;
    // only the Linux branch grows the array here.
    USize capacity = 0;

    DIR *const dir = opendir(path);

    if (!memory_empty(dir)) {
        // Every stat below is resolved relative to this descriptor, so the kernel never
        // re-walks the parent path and no joined child path has to be built per entry.
        int const dir_fd = dirfd(dir);

        listed = true;

        for (struct dirent const *entry = readdir(dir); listed && !memory_empty(entry); entry = readdir(dir)) {
            if (!_dir_entry_name_valid(entry->d_name)) {
                continue;
            }

            DirEntry item = DEFAULT_INITIALIZATION;

            /* memory_try_alloc, not char_new_3: this allocation is filesystem-driven, the same
             * class as the entry array _dir_entries_push documents as recoverable. Leaving it
             * aborting meant the loop's OOM behaviour depended on which of two adjacent lines
             * failed first. Forwarder conversions still use char_new_3 and still abort - those
             * are sized by the CALLER's own string, not by the directory. */
            USize const name_length = char_length(entry->d_name);

            item.name = (char*) memory_try_alloc(name_length + 1);

            if (memory_empty(item.name)) {
                errno = ENOMEM;

                listed = false;

                break;
            }

            char_copy_2(item.name, entry->d_name, name_length);

            item.name[name_length] = '\0';

            // One lstat covers non-links; a link needs a follow-up stat to resolve the
            // target's type and size (falling back to the link's own mtime if broken).
            struct stat link_info = DEFAULT_INITIALIZATION;

            if (fstatat(dir_fd, entry->d_name, &link_info, AT_SYMLINK_NOFOLLOW) == 0) {
                item.is_link = S_ISLNK(link_info.st_mode);

                if (!item.is_link) {
                    item.is_dir = S_ISDIR(link_info.st_mode);
                    item.size   = (USize) link_info.st_size;
                    item.mtime  = (I64) link_info.st_mtime;
                }
                else {
                    struct stat target_info = DEFAULT_INITIALIZATION;

                    if (fstatat(dir_fd, entry->d_name, &target_info, 0) == 0) {
                        item.is_dir = S_ISDIR(target_info.st_mode);
                        item.size   = (USize) target_info.st_size;
                        item.mtime  = (I64) target_info.st_mtime;
                    }
                    else {
                        item.mtime = (I64) link_info.st_mtime;
                    }
                }
            }

            if (!_dir_entries_push(&entries, &count, &capacity, &item)) {
                char_delete(item.name);

                // Both OOM exits in this loop set a reason; the header guarantees a readable
                // error after a false, and a failed grow is the one failure with no syscall
                // behind it to have set one.
                errno = ENOMEM;

                listed = false;
            }
        }

        /* Captured BEFORE the cleanup. closedir frees internally and dir_list_entries_uninit
         * frees every entry, and under the model this module adopted (a free may overwrite
         * errno) that batch sits between the failure and the caller's chance to read it - so
         * the ENOMEM set above would be handed straight to the frees without this. */
        USize const reason = _dir_last_error();

        closedir(dir);

        if (!listed) {
            dir_list_entries_uninit(entries, count);

            entries = nullptr;
            count   = 0;

            _dir_last_error_set(reason);
        }
    }
#elif OS_WINDOWS
    USize const path_size = char_length(path);

    if (path_size > 0) {
        WCHAR *const wide = _dir_wide_from_utf8(path);

        // The documented "must be a directory" contract, and what the ANSI form got from its
        // dir_exists_1 guard - taken on the wide path already built rather than through
        // dir_exists_1, which would repeat the UTF-8 conversion.
        DWORD const attributes = !memory_empty(wide) ? GetFileAttributesW(wide) : INVALID_FILE_ATTRIBUTES;

        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            USize const wide_size = _dir_wide_length(wide);
            WCHAR *const search   = (WCHAR*) memory_try_alloc((wide_size + _DIR_WILDCARD_SUFFIX_SIZE) * sizeof(WCHAR));

            if (memory_empty(search)) {
                // This OOM return set no error at all, so a caller reading the last error after
                // the resulting false saw whatever the thread happened to do previously.
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            }
            else {
                for (USize i = 0; i < wide_size; i += 1) {
                    search[i] = wide[i];
                }

                search[wide_size]     = L'\\';
                search[wide_size + 1] = L'*';
                search[wide_size + 2] = L'\0';

                listed = _dir_list_entries_find(search, &entries, &count);

                // Bracketed: dir_list_entries is inside the header's guaranteed list.
                USize const reason = _dir_last_error();

                memory_free(search);

                _dir_last_error_set(reason);
            }
        }

        /* Outside the attribute check on purpose: the wide path is allocated before it and must
         * be released whether or not the path turned out to be a directory. Bracketed for the
         * same reason as the search free above. */
        if (!memory_empty(wide)) {
            USize const reason = _dir_last_error();

            memory_free(wide);

            _dir_last_error_set(reason);
        }
    }
#endif

    if (listed) {
        *out_entries = entries;
        *out_count   = count;
    }

    trace_log_pop();

    return listed;
}

bool dir_list_entries_2(Str const *const path, DirEntry **const out_entries, USize *const out_count) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    bool listed = false;

    if (str_get_size(path) > 0) {
        char *const data = char_new_3(str_get_data(path), str_get_size(path));

        listed = dir_list_entries_1(data, out_entries, out_count);

        // Bracketed: char_delete may overwrite the last error the caller is promised.
        USize const reason = _dir_last_error();

        char_delete(data);

        _dir_last_error_set(reason);
    }

    trace_log_pop();

    return listed;
}

bool dir_list_entries_3(String const *const path, DirEntry **const out_entries, USize *const out_count) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    bool listed = false;

    if (string_get_size(path) > 0) {
        char *const data = char_new_3(string_get_data(path), string_get_size(path));

        listed = dir_list_entries_1(data, out_entries, out_count);

        // Bracketed: char_delete may overwrite the last error the caller is promised.
        USize const reason = _dir_last_error();

        char_delete(data);

        _dir_last_error_set(reason);
    }

    trace_log_pop();

    return listed;
}

void dir_list_entries_uninit(DirEntry *const entries, USize const count) {
    trace_log_push(LOG_METADATA);

    if (memory_empty(entries)) {
        trace_log_pop();

        return;
    }

    for (USize i = 0; i < count; i += 1) {
        if (!memory_empty(entries[i].name)) {
            char_delete(entries[i].name);
        }
    }

    memory_free(entries);

    trace_log_pop();
}

bool dir_remove_1(char const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    bool removed = false;

#ifdef __linux__
    removed = rmdir(path) == 0;
#elif OS_WINDOWS
    removed = _dir_win_remove_directory(path);
#endif

    trace_log_pop();

    return removed;
}

bool dir_remove_2(Str const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    bool removed = false;

    if (str_get_size(path) > 0) {
        char *const data = char_new_3(str_get_data(path), str_get_size(path));

        removed = dir_remove_1(data);

        // Bracketed: char_delete may overwrite the last error the caller is promised.
        USize const reason = _dir_last_error();

        char_delete(data);

        _dir_last_error_set(reason);
    }

    trace_log_pop();

    return removed;
}

bool dir_remove_3(String const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    bool removed = false;

    if (string_get_size(path) > 0) {
        char *const data = char_new_3(string_get_data(path), string_get_size(path));

        removed = dir_remove_1(data);

        // Bracketed: char_delete may overwrite the last error the caller is promised.
        USize const reason = _dir_last_error();

        char_delete(data);

        _dir_last_error_set(reason);
    }

    trace_log_pop();

    return removed;
}

bool dir_remove_all_1(char const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    USize const path_size = char_length(path);

    bool removed = _dir_remove_all_allowed(path, path_size);

#ifdef __linux__
    struct stat info = DEFAULT_INITIALIZATION;

    removed = removed && lstat(path, &info) == 0 && S_ISDIR(info.st_mode);
#elif OS_WINDOWS
    removed = removed && _dir_win_is_real_directory(path);
#endif

    removed = removed && _dir_remove_all_recurse(path, 0);

    trace_log_pop();

    return removed;
}

bool dir_remove_all_2(Str const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    bool removed = false;

    if (str_get_size(path) > 0) {
        char *const data = char_new_3(str_get_data(path), str_get_size(path));

        removed = dir_remove_all_1(data);

        // Bracketed: char_delete may overwrite the last error the caller is promised.
        USize const reason = _dir_last_error();

        char_delete(data);

        _dir_last_error_set(reason);
    }

    trace_log_pop();

    return removed;
}

bool dir_remove_all_3(String const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    bool removed = false;

    if (string_get_size(path) > 0) {
        char *const data = char_new_3(string_get_data(path), string_get_size(path));

        removed = dir_remove_all_1(data);

        // Bracketed: char_delete may overwrite the last error the caller is promised.
        USize const reason = _dir_last_error();

        char_delete(data);

        _dir_last_error_set(reason);
    }

    trace_log_pop();

    return removed;
}

bool dir_rename_1(char const *const path, char const *const path_new) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);
    error_check_null(LOG_METADATA, "path_new", (void*) path_new);

    /* Windows takes the wide path like every other operation here. The CRT rename() would
     * reinterpret the same UTF-8 bytes in the active code page, so for a non-ASCII path it
     * resolves a DIFFERENT object than the dir_exists_1 that just validated it - a check/use
     * mismatch, and under best-fit mapping it could rename something other than what was
     * checked. */
#if OS_WINDOWS
    bool const renamed = dir_exists_1(path) && _dir_win_rename(path, path_new);
#else
    bool const renamed = dir_exists_1(path) && rename(path, path_new) == 0;
#endif

    trace_log_pop();

    return renamed;
}

bool dir_rename_2(Str const *const path, Str const *const path_new) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);
    error_check_null(LOG_METADATA, "path_new", (void*) path_new);

    bool renamed = false;

    if (str_get_size(path) > 0 && str_get_size(path_new) > 0) {
        char *const data     = char_new_3(str_get_data(path), str_get_size(path));
        char *const data_new = char_new_3(str_get_data(path_new), str_get_size(path_new));

        renamed = dir_rename_1(data, data_new);

        // Bracketed: char_delete may overwrite the last error the caller is promised.
        USize const reason = _dir_last_error();

        char_delete(data);
        char_delete(data_new);

        // BOTH frees precede the restore: the sweep that added this bracket assumed one delete
        // per forwarder, so on the two-path overloads the restore landed between them and the
        // second free could still clobber the very value the header guarantees.
        _dir_last_error_set(reason);
    }

    trace_log_pop();

    return renamed;
}

bool dir_rename_3(String const *const path, String const *const path_new) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);
    error_check_null(LOG_METADATA, "path_new", (void*) path_new);

    bool renamed = false;

    if (string_get_size(path) > 0 && string_get_size(path_new) > 0) {
        char *const data     = char_new_3(string_get_data(path), string_get_size(path));
        char *const data_new = char_new_3(string_get_data(path_new), string_get_size(path_new));

        renamed = dir_rename_1(data, data_new);

        // Bracketed: char_delete may overwrite the last error the caller is promised.
        USize const reason = _dir_last_error();

        char_delete(data);
        char_delete(data_new);

        // BOTH frees precede the restore: the sweep that added this bracket assumed one delete
        // per forwarder, so on the two-path overloads the restore landed between them and the
        // second free could still clobber the very value the header guarantees.
        _dir_last_error_set(reason);
    }

    trace_log_pop();

    return renamed;
}

bool dir_set_current_1(char const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    bool changed = false;

#ifdef __linux__
    changed = chdir(path) == 0;
#elif OS_WINDOWS
    changed = _dir_win_set_current(path);
#endif

    trace_log_pop();

    return changed;
}

bool dir_set_current_2(Str const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    bool changed = false;

    if (str_get_size(path) > 0) {
        char *const data = char_new_3(str_get_data(path), str_get_size(path));

        changed = dir_set_current_1(data);

        // Bracketed: char_delete may overwrite the last error the caller is promised.
        USize const reason = _dir_last_error();

        char_delete(data);

        _dir_last_error_set(reason);
    }

    trace_log_pop();

    return changed;
}

bool dir_set_current_3(String const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    bool changed = false;

    if (string_get_size(path) > 0) {
        char *const data = char_new_3(string_get_data(path), string_get_size(path));

        changed = dir_set_current_1(data);

        // Bracketed: char_delete may overwrite the last error the caller is promised.
        USize const reason = _dir_last_error();

        char_delete(data);

        _dir_last_error_set(reason);
    }

    trace_log_pop();

    return changed;
}