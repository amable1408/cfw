/*
 * process.c - Spawn a child program with piped stdin/stdout and capture what it writes.
 *
 * Programming errors (a null out, argv or argv[0]) abort through error_check_* in checked
 * builds; everything the DATA decides - the program name, its output volume, its fate - is
 * reported through the Result and the outcome, never an abort. See process.h.
 */

#include <process/process.h>

/*==============================================================================
 * MARK: - Macros and Constants
 *============================================================================*/

/** Bytes requested per read from the child's stdout. */
#define _PROCESS_READ_CHUNK 4096

/**
 * Longest this module waits for a child it has already killed to be collectable. A process
 * stuck in an uninterruptible kernel wait (a hung filesystem driver) survives TerminateProcess
 * and SIGKILL until that wait returns; the reap is best effort past this point, so the one
 * path a caller cannot escape is bounded like every other wait in this file.
 */
#define _PROCESS_REAP_WAIT_MS 5000

/**
 * Cap on the final drain once the direct child has been collected. Whatever the child itself
 * wrote is already in the pipe at a DEFAULT capacity (64 KiB on Linux, 4 KiB on Windows), so
 * this loses none of its output; it bounds how much of a SURVIVING grandchild's flood is read
 * into a capture the caller asked about the child. A child that enlarged its own stdout pipe
 * (F_SETPIPE_SZ) past the cap can have its tail dropped here - the header says so.
 */
#define _PROCESS_SETTLED_DRAIN_MAX (64 * 1024)

#ifdef OS_WINDOWS
#if defined(TRACELOG_ENABLED) && !defined(LOG_THREAD_IMPLEMENTATION)
#error "process.c traces from a spawned writer thread; tracelog's stack is per-thread only under LOG_THREAD_IMPLEMENTATION - build with it, or drop the trace calls in _process_write_thread"
#endif

/** Handles a child may inherit: its three standard streams, never more. */
#define _PROCESS_INHERITED_MAX 3

/**
 * Milliseconds the Windows loop sleeps when the child has produced nothing yet.
 * Anonymous pipes cannot be polled, so the loop peeks and sleeps; this trades a
 * little latency for the ability to honor a timeout at all, which a blocking
 * ReadFile would make impossible.
 */
#define _PROCESS_POLL_DELAY_MS 5
#else
/** Separator between PATH entries. */
#define _PROCESS_PATH_SEPARATOR ':'

/** Largest candidate path process_exists will assemble while searching PATH. */
#define _PROCESS_PATH_SIZE_MAX 4096

/**
 * Longest a single poll may wait before the loop re-checks whether the child exited.
 * Without a cap, poll would sleep for the whole remaining timeout on a stdout pipe that a
 * daemonizing helper holds open forever, and the child's exit would go unnoticed until the
 * deadline. Slicing costs a few wakeups a second and bounds that blindness to one slice.
 */
#define _PROCESS_POLL_SLICE_MS 20
#endif // OS_WINDOWS

/*==============================================================================
 * MARK: - File Scope
 *============================================================================*/

#ifndef OS_WINDOWS
/** The parent environment posix_spawn hands to the child. */
extern char **environ;
#endif // OS_WINDOWS

/*==============================================================================
 * MARK: - Shared Helpers
 *============================================================================*/

/**
 * @brief Append captured bytes to the outcome's growing output buffer.
 *
 * Grows geometrically with memory_try_alloc rather than memory_alloc: the total is driven by
 * how much another process decides to write, so exhausting memory must fail the call instead
 * of aborting the program.
 *
 * @param self Outcome whose output is being built.
 * @param capacity In/out allocated size of self->output, including its terminator.
 * @param data Bytes to append.
 * @param size Number of bytes to append.
 * @param limit Cap on self->output_size.
 * @return RESULT_SUCCESS when the bytes were stored; PROCESS_RESULT_OUTPUT_TOO_LARGE (with
 *         RESULT_FLAG_PARTIAL when a prefix is already held) or PROCESS_RESULT_NO_MEMORY
 *         otherwise. The one place the two reasons are told apart, so no caller re-derives it.
 */
static Result _process_output_append(ProcessOutcome *const self, USize *const capacity, char const *const data, USize const size, USize const limit) {
    trace_log_push(LOG_METADATA);

    if (size == 0) {
        trace_log_pop();

        return RESULT_SUCCESS;
    }

    /* Phrased as a subtraction so the sum can never wrap: self->output_size is already
     * known to be <= limit, so limit - output_size is the exact room remaining. What still
     * fits is kept - the header promises the first output_limit bytes - and the refusal
     * carries PARTIAL whenever anything is held. */
    USize const room = limit - self->output_size;

    if (size > room) {
        if (room > 0) {
            Result const kept = _process_output_append(self, capacity, data, room, limit);

            if (result_is_error(kept)) {
                trace_log_pop();

                return kept;
            }
        }

        Result const refused = self->output_size > 0
            ? result_with_flag(PROCESS_RESULT_OUTPUT_TOO_LARGE, RESULT_FLAG_PARTIAL)
            : PROCESS_RESULT_OUTPUT_TOO_LARGE;

        trace_log_pop();

        return refused;
    }

    USize const required = self->output_size + size + 1;

    if (required > *capacity) {
        USize capacity_new = (*capacity == 0) ? (_PROCESS_READ_CHUNK + 1) : *capacity;

        while (capacity_new < required) {
            /* Doubling past the halfway mark would wrap to a small allocation that the
             * copy below would then run straight off the end of. */
            if (capacity_new > USIZE_MAX / 2) {
                capacity_new = required;

                break;
            }

            capacity_new *= 2;
        }

        char *const grown = (char*) memory_try_alloc(capacity_new);

        if (memory_empty(grown)) {
            Result const refused = self->output_size > 0
                ? result_with_flag(PROCESS_RESULT_NO_MEMORY, RESULT_FLAG_PARTIAL)
                : PROCESS_RESULT_NO_MEMORY;

            trace_log_pop();

            return refused;
        }

        if (!memory_empty(self->output)) {
            if (self->output_size > 0) {
                memory_copy_1(grown, self->output, self->output_size);
            }

            /* Guarded: the first append has nothing to release, and memory_delete treats a
             * null target as a programmer error rather than a no-op. */
            memory_delete((void**) &self->output);
        }

        self->output = grown;
        *capacity = capacity_new;
    }

    memory_copy_1(self->output + self->output_size, data, size);

    self->output_size += size;
    self->output[self->output_size] = '\0';

    trace_log_pop();

    return RESULT_SUCCESS;
}

/**
 * @brief Milliseconds from an arbitrary fixed point, for measuring elapsed time.
 *
 * Monotonic on both platforms: a wall-clock source would let a clock adjustment mid-run
 * either expire a deadline early or hang past it.
 *
 * U64 rather than the usual USize, deliberately. Both sources already hand back 64 bits -
 * GetTickCount64 returns ULONGLONG, and clock_gettime's tv_sec is a 64-bit time_t - so
 * narrowing would discard what the OS gave us. On a 32-bit target (armeabi-v7a is a live one)
 * USize caps at 4.29e9 ms, or 49.7 days of uptime, after which it wraps; the deadline here is
 * an absolute comparison, which breaks across a wrap by either expiring instantly or never.
 * That is precisely the bug GetTickCount64 was introduced to fix, and it is not worth
 * reintroducing for the sake of the Size-variant rule.
 *
 * @return Current value of the monotonic millisecond counter.
 */
static U64 _process_now_milliseconds(void) {
#ifdef OS_WINDOWS
    return (U64) GetTickCount64();
#else
    struct timespec now = DEFAULT_INITIALIZATION;

    /* CLOCK_MONOTONIC cannot fail on Linux; the 0 would leave every deadline unexpired, so a
     * platform where it could would need a different source, not a fallback. */
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }

    return ((U64) now.tv_sec * 1000U) + ((U64) now.tv_nsec / 1000000U);
#endif // OS_WINDOWS
}

#ifdef OS_WINDOWS
/*==============================================================================
 * MARK: - Windows Backend
 *============================================================================*/

/** Hands the child's stdin bytes to a thread, so a full pipe cannot deadlock the reader. */
typedef struct {
    /** Parent end of the child's stdin pipe; the thread closes it to signal end of file. */
    HANDLE handle;

    /** Bytes to write. */
    char const *data;

    /** Number of bytes to write. */
    USize size;
} _ProcessWriter;

/**
 * @brief Report whether an argument must be quoted to survive CommandLineToArgvW.
 *
 * @param argument Argument to inspect.
 * @return true when the argument needs surrounding quotes.
 */
static bool _process_argument_quoted(char const *const argument) {
    if (argument[0] == '\0') {
        /* An empty argument would vanish entirely without quotes, silently shifting every
         * argument after it down one position in the child's argv. */
        return true;
    }

    for (USize i = 0; argument[i] != '\0'; i += 1) {
        if (argument[i] == ' ' || argument[i] == '\t' || argument[i] == '"') {
            return true;
        }
    }

    return false;
}

/**
 * @brief Append one argument to a command line, measuring when buffer is null.
 *
 * Implements the backslash/quote rules CommandLineToArgvW parses, so what the caller put in
 * the vector is exactly what the child finds in its argv. Getting this wrong is not cosmetic:
 * an argument holding a quote could otherwise close the quoting early and let the rest of its
 * own text be read as further arguments.
 *
 * @param buffer Destination, or nullptr to measure only.
 * @param offset Bytes already written.
 * @param argument Argument to append.
 * @return Offset after appending.
 */
static USize _process_argument_append(char *const buffer, USize const offset, char const *const argument) {
    trace_log_push(LOG_METADATA);

    USize at = offset;
    bool const quoted = _process_argument_quoted(argument);

    if (quoted) {
        if (buffer != nullptr) {
            buffer[at] = '"';
        }

        at += 1;
    }

    USize i = 0;

    while (argument[i] != '\0') {
        USize slashes = 0;

        while (argument[i + slashes] == '\\') {
            slashes += 1;
        }

        if (argument[i + slashes] == '\0') {
            /* Trailing backslashes would otherwise escape the closing quote. Doubling them
             * leaves the intended count in the child's argv with the quote still closing. */
            USize const emit = quoted ? (slashes * 2) : slashes;

            for (USize n = 0; n < emit; n += 1) {
                if (buffer != nullptr) {
                    buffer[at + n] = '\\';
                }
            }

            at += emit;
            i += slashes;

            break;
        }

        if (argument[i + slashes] == '"') {
            for (USize n = 0; n < (slashes * 2) + 1; n += 1) {
                if (buffer != nullptr) {
                    buffer[at + n] = '\\';
                }
            }

            at += (slashes * 2) + 1;

            if (buffer != nullptr) {
                buffer[at] = '"';
            }

            at += 1;
            i += slashes + 1;

            continue;
        }

        for (USize n = 0; n < slashes; n += 1) {
            if (buffer != nullptr) {
                buffer[at + n] = '\\';
            }
        }

        at += slashes;

        if (buffer != nullptr) {
            buffer[at] = argument[i + slashes];
        }

        at += 1;
        i += slashes + 1;
    }

    if (quoted) {
        if (buffer != nullptr) {
            buffer[at] = '"';
        }

        at += 1;
    }

    trace_log_pop();

    return at;
}

/**
 * @brief Report whether a resolved image path names a batch file.
 *
 * CreateProcess runs .bat and .cmd through cmd.exe, which re-parses the command line with
 * its own rules - the one case where "no shell is spawned" would be a lie. Refused rather
 * than escaped for cmd: there is no complete escaping for that parser (CVE-2024-24576).
 *
 * @param path Path SearchPathA resolved.
 * @return true when the extension is .bat or .cmd, in any case.
 */
static bool _process_batch_image(char const *const path) {
    USize size = 0;

    while (path[size] != '\0') {
        size += 1;
    }

    if (size < 4 || path[size - 4] != '.') {
        return false;
    }

    char extension[3] = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 3; i += 1) {
        char const c = path[size - 3 + i];

        extension[i] = (c >= 'A' && c <= 'Z') ? (char) (c + ('a' - 'A')) : c;
    }

    return (extension[0] == 'b' && extension[1] == 'a' && extension[2] == 't')
        || (extension[0] == 'c' && extension[1] == 'm' && extension[2] == 'd');
}

/**
 * @brief Terminate the child unless it has already ended on its own.
 *
 * The kill and the child's own exit race; when the child got there first its status is the
 * truth and is reported as EXITED, whatever this call was about to claim.
 *
 * @param process Process handle.
 * @return true when the child was still running and has been terminated; false when it had
 *         already ended, in which case nothing was sent.
 */
static bool _process_terminate(HANDLE const process) {
    if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
        return false;
    }

    TerminateProcess(process, 1);

    return true;
}

/**
 * @brief Resolve a program the way the spawn will run it, and refuse what must not run.
 *
 * One algorithm for process_exists and process_run: SearchPathA with CreateProcess's rules,
 * then the refusals - a batch file (cmd.exe would re-parse the arguments), a directory (the
 * spawn would fail after a "found"), a path past the buffer (it cannot be classified).
 *
 * @param program Program name or path.
 * @param resolved Receives the resolved path when the search hit.
 * @param resolved_capacity Capacity of resolved.
 * @param resolved_size Receives the resolved length, or 0 when the search missed - a miss is
 *                      not a refusal: CreateProcess reports it as the OS error it is.
 * @return RESULT_SUCCESS, or the refusal.
 */
static Result _process_resolve(char const *const program, char *const resolved, DWORD const resolved_capacity, DWORD *const resolved_size) {
    trace_log_push(LOG_METADATA);

    *resolved_size = SearchPathA(nullptr, program, ".exe", resolved_capacity, resolved, nullptr);

    /* A return past the buffer is the REQUIRED size with the buffer untouched, not a hit. */
    if (*resolved_size >= resolved_capacity) {
        *resolved_size = 0;

        trace_log_pop();

        return PROCESS_RESULT_PATH_TOO_LONG;
    }

    if (*resolved_size != 0 && _process_batch_image(resolved)) {
        trace_log_pop();

        return PROCESS_RESULT_BATCH_REFUSED;
    }

    /* INVALID_FILE_ATTRIBUTES has the directory bit set, so a name whose attributes cannot be
     * read is refused the same way rather than handed to the spawn blind. */
    if (*resolved_size != 0 && (GetFileAttributesA(resolved) & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        trace_log_pop();

        return PROCESS_RESULT_NOT_EXECUTABLE;
    }

    trace_log_pop();

    return RESULT_SUCCESS;
}

/**
 * @brief Close every handle in a list, skipping the null ones.
 *
 * @param handles Handles to close.
 * @param count Number of entries.
 */
static void _process_close_handles(HANDLE const *const handles, USize const count) {
    for (USize i = 0; i < count; i += 1) {
        if (handles[i] != nullptr && handles[i] != INVALID_HANDLE_VALUE) {
            CloseHandle(handles[i]);
        }
    }
}

/**
 * @brief Join an argument vector into a command line CreateProcess accepts.
 *
 * @param argv NULL-terminated argument vector.
 * @return Owned command line (release with memory_delete), or nullptr on allocation failure.
 */
static char* _process_command_line_new(char const *const *const argv) {
    trace_log_push(LOG_METADATA);

    USize size = 0;

    for (USize i = 0; argv[i] != nullptr; i += 1) {
        if (i > 0) {
            size += 1;
        }

        size = _process_argument_append(nullptr, size, argv[i]);
    }

    char *const line = (char*) memory_try_alloc(size + 1);

    if (memory_empty(line)) {
        trace_log_pop();

        return nullptr;
    }

    USize at = 0;

    for (USize i = 0; argv[i] != nullptr; i += 1) {
        if (i > 0) {
            line[at] = ' ';
            at += 1;
        }

        at = _process_argument_append(line, at, argv[i]);
    }

    line[at] = '\0';

    trace_log_pop();

    return line;
}

/**
 * @brief The handle the child's stderr is bound to, per spec.stderr_mode.
 *
 * Every handle in the inheritable-handle list must itself be inheritable, and the parent's
 * stderr may not be (a redirected file often is not), so INHERIT hands the child an
 * inheritable DUPLICATE rather than flipping a flag on the parent's own handle. A GUI parent
 * has no stderr at all; the child then gets none either, and its writes fail silently.
 *
 * @param mode Where stderr goes.
 * @param output_write Child end of the stdout pipe, for MERGE.
 * @param handle Receives the handle to bind, or nullptr for "no stderr" (a GUI parent).
 * @param owned Set true when *handle is this module's to close after the spawn.
 * @return RESULT_SUCCESS, or the OS error when the sink or the duplicate could not be made.
 */
static Result _process_stderr_handle(ProcessStderr const mode, HANDLE const output_write, HANDLE *const handle, bool *const owned) {
    trace_log_push(LOG_METADATA);

    *handle = nullptr;
    *owned = false;

    if (mode == PROCESS_STDERR_MERGE) {
        *handle = output_write;

        trace_log_pop();

        return RESULT_SUCCESS;
    }

    SECURITY_ATTRIBUTES inheritable = DEFAULT_INITIALIZATION;

    inheritable.nLength = sizeof(SECURITY_ATTRIBUTES);
    inheritable.bInheritHandle = TRUE;

    if (mode == PROCESS_STDERR_DISCARD) {
        HANDLE const sink = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable, OPEN_EXISTING, 0, nullptr);

        if (sink == INVALID_HANDLE_VALUE) {
            Result const result = result_from_os();

            trace_log_pop();

            return result;
        }

        *handle = sink;
        *owned = true;

        trace_log_pop();

        return RESULT_SUCCESS;
    }

    HANDLE const parent = GetStdHandle(STD_ERROR_HANDLE);

    /* No stderr at all (a GUI parent) is a state, not a failure: the child gets none either. */
    if (parent == nullptr || parent == INVALID_HANDLE_VALUE) {
        trace_log_pop();

        return RESULT_SUCCESS;
    }

    HANDLE duplicate = nullptr;

    if (DuplicateHandle(GetCurrentProcess(), parent, GetCurrentProcess(), &duplicate, 0, TRUE, DUPLICATE_SAME_ACCESS) == 0) {
        Result const result = result_from_os();

        trace_log_pop();

        return result;
    }

    *handle = duplicate;
    *owned = true;

    trace_log_pop();

    return RESULT_SUCCESS;
}

/**
 * @brief Drain whatever the stdout pipe holds right now, without waiting for more.
 *
 * Used at the deadline: the bytes already in the pipe arrived before it and belong to the
 * caller, whether the child was just killed or had ended on its own a moment earlier. Bounded
 * by the output limit, so a pipe a grandchild keeps refilling cannot turn this into a wait.
 *
 * @param output_read Parent end of the stdout pipe.
 * @param out Outcome whose output is being built.
 * @param capacity In/out allocated size of out->output.
 * @param limit Cap on out->output_size.
 * @param chunk Scratch buffer of _PROCESS_READ_CHUNK bytes.
 * @param settled_max Cap on how much this drain may read, or 0 for "until the pipe is empty".
 *                    Used once the child is collected, to bound a surviving grandchild's flood.
 * @return RESULT_SUCCESS, or the append's refusal.
 */
static Result _process_drain(HANDLE const output_read, ProcessOutcome *const out, USize *const capacity, USize const limit, char *const chunk, USize const settled_max) {
    trace_log_push(LOG_METADATA);

    Result result = RESULT_SUCCESS;
    USize drained = 0;

    while (settled_max == 0 || drained < settled_max) {
        DWORD available = 0;

        if (PeekNamedPipe(output_read, nullptr, 0, nullptr, &available, nullptr) == 0 || available == 0) {
            break;
        }

        DWORD want = (available > (DWORD) _PROCESS_READ_CHUNK) ? (DWORD) _PROCESS_READ_CHUNK : available;

        /* Clamped to the cap, not merely tested against it: the loop's condition alone would let a
         * final read overshoot by most of a chunk. */
        if (settled_max != 0 && (USize) want > settled_max - drained) {
            want = (DWORD) (settled_max - drained);
        }

        DWORD moved = 0;

        if (ReadFile(output_read, chunk, want, &moved, nullptr) == 0 || moved == 0) {
            break;
        }

        drained += (USize) moved;
        result = _process_output_append(out, capacity, chunk, (USize) moved, limit);

        if (result_is_error(result)) {
            break;
        }
    }

    trace_log_pop();

    return result;
}

/**
 * @brief Feed the child's stdin from a thread, then close it to signal end of file.
 *
 * Windows anonymous pipes have no non-blocking mode, so writing from the reading thread
 * would deadlock as soon as the child filled its stdout pipe while we were still blocked
 * writing its stdin.
 *
 * @param data The _ProcessWriter to service.
 * @return Always nullptr; the write result is not surfaced, because a child that stops
 *         reading early is its own decision rather than a failure of this call.
 */
static void* _process_write_thread(void *const data) {
    /* Safe from this thread: under LOG_THREAD_IMPLEMENTATION each thread owns its own
     * trace stack in thread-local storage, so this shares nothing with the caller's. */
    trace_log_push(LOG_METADATA);

    _ProcessWriter *const writer = (_ProcessWriter*) data;
    USize written = 0;

    while (written < writer->size) {
        USize const remaining = writer->size - written;
        DWORD const want = (remaining > (USize) MAXDWORD) ? MAXDWORD : (DWORD) remaining;
        DWORD moved = 0;

        if (WriteFile(writer->handle, writer->data + written, want, &moved, nullptr) == 0 || moved == 0) {
            break;
        }

        written += (USize) moved;
    }

    CloseHandle(writer->handle);

    trace_log_pop();

    return nullptr;
}

/**
 * @brief Windows implementation of process_run.
 *
 * @param spec What to run.
 * @param out Receives the outcome; already zeroed by the caller.
 * @return Result of spawning and communicating with the child.
 */
static Result _process_run_windows(ProcessSpec const spec, ProcessOutcome *const out) {
    trace_log_push(LOG_METADATA);

    /* Resolved BEFORE anything is allocated, by the same resolver process_exists uses, so what
     * the probe refuses the spawn refuses too. A name it cannot resolve is left for
     * CreateProcess to report as the OS error it is. */
    char resolved[MAX_PATH] = DEFAULT_INITIALIZATION;
    DWORD resolved_size = 0;
    Result const resolution = _process_resolve(spec.argv[0], resolved, (DWORD) sizeof(resolved), &resolved_size);

    if (result_is_error(resolution)) {
        trace_log_pop();

        return resolution;
    }

    SECURITY_ATTRIBUTES attributes = DEFAULT_INITIALIZATION;

    attributes.nLength = sizeof(SECURITY_ATTRIBUTES);
    attributes.bInheritHandle = TRUE;

    HANDLE input_read = nullptr;
    HANDLE input_write = nullptr;

    if (CreatePipe(&input_read, &input_write, &attributes, 0) == 0) {
        Result const result = result_from_os();

        trace_log_pop();

        return result;
    }

    HANDLE output_read = nullptr;
    HANDLE output_write = nullptr;

    if (CreatePipe(&output_read, &output_write, &attributes, 0) == 0) {
        Result const result = result_from_os();
        HANDLE const opened[] = { input_read, input_write };

        _process_close_handles(opened, 2);

        trace_log_pop();

        return result;
    }

    HANDLE pipes[] = { input_read, input_write, output_read, output_write };

    /* The parent ends are named nowhere in the inheritable-handle list below, so the child
     * cannot receive them; clearing the flag as well keeps that true even for a spawn that
     * ever falls back to plain inheritance. A child holding its own copy of the stdout write
     * handle would keep the pipe from ever reporting end of file. */
    if (SetHandleInformation(input_write, HANDLE_FLAG_INHERIT, 0) == 0
        || SetHandleInformation(output_read, HANDLE_FLAG_INHERIT, 0) == 0) {
        Result const result = result_from_os();

        _process_close_handles(pipes, 4);

        trace_log_pop();

        return result;
    }

    char *command_line = _process_command_line_new(spec.argv);

    if (memory_empty(command_line)) {
        _process_close_handles(pipes, 4);

        trace_log_pop();

        return PROCESS_RESULT_NO_MEMORY;
    }

    bool error_owned = false;
    HANDLE error_handle = nullptr;
    Result const stderr_result = _process_stderr_handle(spec.stderr_mode, output_write, &error_handle, &error_owned);

    if (result_is_error(stderr_result)) {
        memory_delete((void**) &command_line);
        _process_close_handles(pipes, 4);

        trace_log_pop();

        return stderr_result;
    }

    /* The child inherits EXACTLY these. Without the list every inheritable handle in the
     * parent - a listening socket, an open log, another run's pipe ends - would cross over,
     * and a hung helper would keep the parent's port bound. */
    HANDLE inherited[_PROCESS_INHERITED_MAX] = { input_read, output_write, nullptr };
    USize inherited_count = 2;

    if (error_handle != nullptr && error_handle != output_write) {
        inherited[inherited_count] = error_handle;
        inherited_count += 1;
    }

    /* Sized by a call that fails with ERROR_INSUFFICIENT_BUFFER by design. */
    SIZE_T list_size = 0;

    InitializeProcThreadAttributeList(nullptr, 1, 0, &list_size);

    LPPROC_THREAD_ATTRIBUTE_LIST list = (LPPROC_THREAD_ATTRIBUTE_LIST) memory_try_alloc((USize) list_size);
    bool list_ready = false;
    Result spawn_result = RESULT_SUCCESS;

    if (memory_empty(list)) {
        spawn_result = PROCESS_RESULT_NO_MEMORY;
    }
    else if (InitializeProcThreadAttributeList(list, 1, 0, &list_size) == 0) {
        spawn_result = result_from_os();
    }
    else {
        list_ready = true;

        if (UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, inherited_count * sizeof(HANDLE), nullptr, nullptr) == 0) {
            spawn_result = result_from_os();
        }
    }

    PROCESS_INFORMATION info = DEFAULT_INITIALIZATION;

    if (result_is_success(spawn_result)) {
        STARTUPINFOEXA startup = DEFAULT_INITIALIZATION;

        startup.StartupInfo.cb = sizeof(STARTUPINFOEXA);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = input_read;
        startup.StartupInfo.hStdOutput = output_write;
        startup.StartupInfo.hStdError = error_handle;
        startup.lpAttributeList = list;

        /* The image SearchPathA resolved - and this module classified - is the image run, so
         * CreateProcess performs no second search of its own; only an unresolved name is left
         * to it, to report the OS error. argv[0] still round-trips through the command line. */
        char const *const application = resolved_size != 0 ? resolved : nullptr;

        if (CreateProcessA(application, command_line, nullptr, nullptr, TRUE, EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &startup.StartupInfo, &info) == 0) {
            spawn_result = result_from_os();
        }
    }

    if (list_ready) {
        DeleteProcThreadAttributeList(list);
    }

    if (!memory_empty(list)) {
        memory_delete((void**) &list);
    }

    if (error_owned) {
        CloseHandle(error_handle);
    }

    memory_delete((void**) &command_line);

    /* The child now owns its ends; holding on to them would keep the pipes open. */
    CloseHandle(input_read);
    CloseHandle(output_write);

    if (result_is_error(spawn_result)) {
        CloseHandle(input_write);
        CloseHandle(output_read);

        trace_log_pop();

        return spawn_result;
    }

    _ProcessWriter writer = DEFAULT_INITIALIZATION;
    Thread writer_thread = DEFAULT_INITIALIZATION;
    bool writing = false;

    if (spec.input != nullptr && spec.input_size > 0) {
        writer.handle = input_write;
        writer.data = spec.input;
        writer.size = spec.input_size;

        Result const started = thread_create_1(&writer_thread, _process_write_thread, &writer);

        if (result_is_error(started)) {
            /* Fail closed: the caller's input was never delivered, and a success Result would say
             * it was. The child is killed and collected, never left running against an empty
             * stdin - the same shape as a refused capture. */
            bool const terminated = _process_terminate(info.hProcess);
            bool const collected = WaitForSingleObject(info.hProcess, _PROCESS_REAP_WAIT_MS) == WAIT_OBJECT_0;
            DWORD status = 0;

            /* Classified, not assumed: a child that had already exited is reported with its own
             * status, the same rule the main tail follows. */
            if (terminated) {
                out->ended = PROCESS_ENDED_KILLED;
            }
            else if (collected && GetExitCodeProcess(info.hProcess, &status) != 0) {
                out->ended = PROCESS_ENDED_EXITED;
                out->exit_code = (I32) status;
            }

            HANDLE const remaining[] = { input_write, output_read, info.hThread, info.hProcess };

            _process_close_handles(remaining, 4);

            trace_log_pop();

            return started;
        }

        writing = true;
    }

    if (!writing) {
        /* Nothing to send: close stdin so the child sees end of file immediately rather than
         * blocking on input that will never arrive. */
        CloseHandle(input_write);
    }

    USize capacity = 0;
    USize const requested = (spec.output_limit > 0) ? spec.output_limit : PROCESS_OUTPUT_LIMIT_DEFAULT;

    /* Clamped so output_size + size + 1 cannot wrap inside the append: the limit is caller-set,
     * and USIZE_MAX would make that sum reach zero and skip the growth branch entirely. */
    USize const limit = (requested > USIZE_MAX - 1) ? (USIZE_MAX - 1) : requested;
    U64 const deadline = _process_now_milliseconds() + (U64) spec.timeout_milliseconds;
    Result result = RESULT_SUCCESS;
    bool killed = false;

    /* Hoisted out of the loop: initializing it per iteration would zero four kilobytes on
     * every read for no benefit, since the read overwrites exactly what it reports. */
    char chunk[_PROCESS_READ_CHUNK] = DEFAULT_INITIALIZATION;

    while (true) {
        /* First, every iteration: a child that keeps the pipe non-empty would otherwise loop
         * read-and-peek forever and never reach a check placed after the drain. What the pipe
         * already holds arrived before the deadline and is drained (bounded) before the break,
         * so a child that ended on its own a moment ago is reported with its whole output. */
        if (spec.timeout_milliseconds > 0 && _process_now_milliseconds() >= deadline) {
            out->timed_out = _process_terminate(info.hProcess);

            /* The deadline is the caller's ask, so it wins over the drain's limit: a grandchild
             * still flooding the pipe fills the capture to the cap and the run stays a
             * timeout, not OUTPUT_TOO_LARGE. Memory exhaustion is still reported. */
            Result const drained = _process_drain(output_read, out, &capacity, limit, chunk, 0);

            if (result_is_error(drained) && result_clear_flag(drained, RESULT_FLAG_PARTIAL) != PROCESS_RESULT_OUTPUT_TOO_LARGE) {
                result = drained;
            }

            break;
        }

        /* Second, before the drain: once the direct child is collected the run is over. What it
         * wrote is already in the pipe, so a bounded final drain takes all of it - and stops,
         * rather than reading a surviving grandchild's flood into a capture the caller asked
         * about the child (wl-copy's survivor is the shape this protects). */
        if (WaitForSingleObject(info.hProcess, 0) == WAIT_OBJECT_0) {
            /* A refusal here is real: no deadline is being met, so an over-limit capture is the
             * failure it always was. */
            Result const drained = _process_drain(output_read, out, &capacity, limit, chunk, _PROCESS_SETTLED_DRAIN_MAX);

            if (result_is_error(drained)) {
                result = drained;
            }

            break;
        }

        DWORD available = 0;

        if (PeekNamedPipe(output_read, nullptr, 0, nullptr, &available, nullptr) == 0) {
            /* The child closed its end: end of file, not an error. Any other failure is one,
             * and must not read as a clean end of output. */
            if (GetLastError() != ERROR_BROKEN_PIPE) {
                result = result_from_os();
                killed = _process_terminate(info.hProcess);
            }

            break;
        }

        if (available > 0) {
            DWORD const want = (available > (DWORD) sizeof(chunk)) ? (DWORD) sizeof(chunk) : available;
            DWORD moved = 0;

            if (ReadFile(output_read, chunk, want, &moved, nullptr) == 0) {
                /* Broken pipe is end of file; anything else is a failure and must not read as
                 * a clean end of output. */
                if (GetLastError() != ERROR_BROKEN_PIPE) {
                    result = result_from_os();
                    killed = _process_terminate(info.hProcess);
                }

                break;
            }

            if (moved == 0) {
                break;
            }

            Result const appended = _process_output_append(out, &capacity, chunk, (USize) moved, limit);

            if (result_is_error(appended)) {
                /* The call has failed; nothing the child does from here matters, and waiting
                 * out its deadline would turn a refused capture into a hang. Killed now, and
                 * reported as KILLED rather than as a timeout it never reached - unless it had
                 * already ended, in which case its own status stands. */
                result = appended;
                killed = _process_terminate(info.hProcess);

                break;
            }

            continue;
        }

        Sleep(_PROCESS_POLL_DELAY_MS);
    }

    /* Closed BEFORE the wait. While the parent still holds the read end and has stopped
     * draining it, a child that fills the pipe blocks in WriteFile forever - so waiting on it
     * here is a deadlock, not a wait. Dropping the handle turns the child's next write into a
     * broken pipe and lets it exit. Reached only once the loop has decided to stop, so no
     * output is lost that we still wanted. */
    CloseHandle(output_read);

    if (!killed && !out->timed_out) {
        /* Bounded, and killed if it overruns. The loop can exit with the child still alive -
         * having closed its own stdout and lingered - and an INFINITE wait on that path is
         * exactly how a call with a deadline becomes a call without one. */
        U64 const settled = _process_now_milliseconds();
        DWORD wait_span = INFINITE;

        if (spec.timeout_milliseconds > 0) {
            wait_span = (settled >= deadline) ? 0 : (DWORD) (deadline - settled);

            /* INFINITE is itself 0xFFFFFFFF, so a remaining span that happens to equal it
             * would silently turn this bounded wait back into an unbounded one. */
            if (wait_span == INFINITE) {
                wait_span = INFINITE - 1;
            }
        }

        if (WaitForSingleObject(info.hProcess, wait_span) != WAIT_OBJECT_0) {
            out->timed_out = _process_terminate(info.hProcess);
        }
    }

    /* A terminated child is collected, never abandoned: TerminateProcess is asynchronous, and
     * the exit code is only trustworthy once the object is signalled. Bounded, because a
     * process pinned in a kernel wait survives TerminateProcess until that wait returns. */
    bool const collected = WaitForSingleObject(info.hProcess, _PROCESS_REAP_WAIT_MS) == WAIT_OBJECT_0;
    DWORD status = 0;

    if (out->timed_out) {
        out->ended = PROCESS_ENDED_TIMED_OUT;
    }
    else if (killed) {
        out->ended = PROCESS_ENDED_KILLED;
    }
    else if (collected && GetExitCodeProcess(info.hProcess, &status) != 0) {
        /* Windows has no signals: a crash or an abort is an exit whose status is the NTSTATUS,
         * and the header says so. */
        out->ended = PROCESS_ENDED_EXITED;
        out->exit_code = (I32) status;
    }

    if (writing) {
        /* The writer can still be blocked in WriteFile even though the child has gone: a
         * grandchild that inherited the child's stdin keeps that pipe's read end open, and
         * the child exiting does not close the duplicate. Cancelling the blocked I/O is what
         * bounds this - it was the last unbounded wait left on this path.
         *
         * The join must stay. Detaching instead would be a use-after-return: `writer` is a
         * stack local of this frame, so a thread outliving it would read freed stack. */
        /* Looped, not fired once: CancelSynchronousIo only cancels I/O already in flight, so a
         * single call landing between two WriteFile calls leaves the next one free to block
         * and the join unbounded again. Retry until the thread actually signals. */
        while (WaitForSingleObject(writer_thread.native_handle, _PROCESS_POLL_DELAY_MS) == WAIT_TIMEOUT) {
            CancelSynchronousIo(writer_thread.native_handle);
        }

        thread_join_1(&writer_thread);
    }

    /* output_read was already closed above, before the wait. */
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);

    trace_log_pop();

    return result;
}
#else
/*==============================================================================
 * MARK: - POSIX Backend
 *============================================================================*/

/**
 * @brief Mark a descriptor close-on-exec.
 *
 * Every descriptor this module creates is the parent's alone: the child gets its standard
 * streams through dup2 (which clears the flag on the target) and nothing else. Without this,
 * a concurrent run's child would inherit this run's pipe ends and hold them open past the
 * end of file, and any inheritable descriptor in the parent - a listening socket - would
 * reach the helper.
 *
 * The FALLBACK: where the platform can create the descriptor already flagged (SOCK_CLOEXEC,
 * pipe2) _process_pipe and the socketpair below do, so no window exists in which another
 * thread's spawn could inherit a bare descriptor; this closes it after the fact elsewhere.
 *
 * @param descriptor Descriptor to mark.
 */
static void _process_cloexec(I32 const descriptor) {
    I32 const flags = fcntl(descriptor, F_GETFD, 0);

    if (flags >= 0) {
        fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC);
    }
}

/**
 * @brief Create the stdout pipe close-on-exec from birth where the platform allows it.
 *
 * @param descriptors Receives the read and write ends.
 * @return 0 on success, -1 with errno set otherwise.
 */
static I32 _process_pipe(I32 *const descriptors) {
#ifdef __linux__
    return pipe2(descriptors, O_CLOEXEC);
#else
    if (pipe(descriptors) != 0) {
        return -1;
    }

    _process_cloexec(descriptors[0]);
    _process_cloexec(descriptors[1]);

    return 0;
#endif // __linux__
}

/**
 * @brief Move a descriptor clear of the standard stream numbers.
 *
 * A pipe handed fd 0 or 1 - which happens whenever the parent has closed its own stdio, as a
 * daemon does - collides with the dup2 targets used below. The close that tidies away a dup
 * SOURCE would then destroy the stream an earlier dup2 had just established, silently: the
 * child ends up with no stdout, the call returns success, and the caller reads an empty
 * result. Lifting every descriptor above stderr removes that whole class rather than trying
 * to guard each ordering of it.
 *
 * @param descriptor Descriptor to move; closed once moved.
 * @return The raised descriptor, or the original when it was already clear or could not move.
 */
static I32 _process_raise(I32 const descriptor) {
    if (descriptor > STDERR_FILENO) {
        return descriptor;
    }

    I32 const raised = fcntl(descriptor, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);

    if (raised < 0) {
        return descriptor;
    }

    close(descriptor);

    return raised;
}

/**
 * @brief Put a descriptor into non-blocking mode.
 *
 * poll reporting a descriptor ready means at least one byte may move, not that the whole
 * request will. A blocking write of more than the socket buffer would then stall inside
 * send while the child waits on us to drain its stdout - the exact deadlock the poll loop
 * exists to avoid.
 *
 * @param descriptor Descriptor to modify.
 */
static void _process_nonblocking(I32 const descriptor) {
    I32 const flags = fcntl(descriptor, F_GETFL, 0);

    if (flags >= 0) {
        fcntl(descriptor, F_SETFL, flags | O_NONBLOCK);
    }
}

/**
 * @brief Close both ends of two descriptor pairs.
 *
 * @param input_pair The stdin socketpair.
 * @param output_pipe The stdout pipe.
 */
static void _process_close_pairs(I32 const *const input_pair, I32 const *const output_pipe) {
    close(input_pair[0]);
    close(input_pair[1]);
    close(output_pipe[0]);
    close(output_pipe[1]);
}

/**
 * @brief Report whether a path names a regular file this process may execute.
 *
 * A directory on PATH would pass access(X_OK) and then fail the spawn with EACCES; a probe
 * that said "found" for it would be lying.
 *
 * @param path Candidate path.
 * @return 0 when it is an executable regular file; ENOENT when nothing is there; EACCES when
 *         something is there that cannot be run (a directory, a file without the x bit).
 */
static I32 _process_executable(char const *const path) {
    struct stat info = DEFAULT_INITIALIZATION;

    if (stat(path, &info) != 0) {
        return ENOENT;
    }

    /* What exec would say: a directory or a file without the x bit is there but not runnable. */
    return (S_ISREG(info.st_mode) && access(path, X_OK) == 0) ? 0 : EACCES;
}

/**
 * @brief Resolve a program the way process_exists reports it: a name with a slash is taken
 *        as a path; a bare name walks PATH, skipping EMPTY entries.
 *
 * One algorithm serves the probe and the spawn, which is what makes the header's claim
 * true: posix_spawnp treats an empty PATH entry as the working directory, so handing it a
 * bare name would let `PATH=:/usr/bin` run a planted `./helper` that the probe refused.
 *
 * getenv rather than CFW's env_get_1: env pulls in the file module for its dotenv loading,
 * and a low-level spawn primitive must not drag a higher layer in behind it for one read of
 * PATH.
 *
 * @param program Program name or path.
 * @param resolved Receives the resolved path, NUL-terminated.
 * @param resolved_size Capacity of resolved.
 * @param error Receives the reason on a miss: ENOENT when nothing of that name exists on the
 *              walk, EACCES when something does but cannot be run - what exec would report -
 *              and ENAMETOOLONG for an explicit path the buffer cannot hold.
 * @return true when an executable regular file was found and fits in resolved.
 */
static bool _process_resolve(char const *const program, char *const resolved, USize const resolved_size, I32 *const error) {
    trace_log_push(LOG_METADATA);

    USize const program_size = strlen(program);
    bool separated = false;

    *error = ENOENT;

    /* An empty name would make every PATH entry yield the candidate "dir/", which stats as the
     * directory itself: missing, not "there but not runnable". */
    if (program_size == 0) {
        trace_log_pop();

        return false;
    }

    for (USize i = 0; i < program_size; i += 1) {
        if (program[i] == '/') {
            separated = true;

            break;
        }
    }

    if (separated) {
        /* A path that cannot fit the buffer is not "missing": ENAMETOOLONG, the Windows
         * PATH_TOO_LONG refusal's twin. */
        I32 const verdict = program_size + 1 <= resolved_size ? _process_executable(program) : ENAMETOOLONG;

        if (verdict == 0) {
            memory_copy_1(resolved, program, program_size);

            resolved[program_size] = '\0';
        }
        else {
            *error = verdict;
        }

        trace_log_pop();

        return verdict == 0;
    }

    /* Read once and walked in place: environment writes are a startup-only, main-thread
     * activity by env's own rule (see the header's Thread Safety), which is what keeps a
     * borrowed pointer into the environment block valid for the length of this walk. */
    char const *const path = getenv("PATH");

    if (path == nullptr) {
        trace_log_pop();

        return false;
    }

    USize at = 0;

    while (path[at] != '\0') {
        USize end = at;

        while (path[end] != '\0' && path[end] != _PROCESS_PATH_SEPARATOR) {
            end += 1;
        }

        USize const directory_size = end - at;

        /* An empty entry ("::" or a leading colon) means the working directory in PATH's
         * grammar. Skipped deliberately: resolving a helper out of whatever directory the
         * process happens to sit in is how a planted binary gets run. */
        if (directory_size > 0 && directory_size + program_size + 2 <= resolved_size) {
            memory_copy_1(resolved, path + at, directory_size);

            resolved[directory_size] = '/';

            memory_copy_1(resolved + directory_size + 1, program, program_size);

            resolved[directory_size + 1 + program_size] = '\0';

            I32 const verdict = _process_executable(resolved);

            if (verdict == 0) {
                trace_log_pop();

                return true;
            }

            /* A candidate that exists but cannot run outranks "nothing found" as the reason, as
             * execvp reports it. */
            if (verdict == EACCES) {
                *error = EACCES;
            }
        }

        at = (path[end] == '\0') ? end : end + 1;
    }

    resolved[0] = '\0';

    trace_log_pop();

    return false;
}

/**
 * @brief Drain whatever the stdout pipe holds right now, without waiting for more.
 *
 * The POSIX twin of the Windows drain, used at the deadline: the descriptor is non-blocking,
 * so the loop ends on the first read that would wait (EAGAIN), on end of file, on an error, or
 * when the output limit refuses - a pipe a grandchild keeps refilling cannot turn this into a
 * wait.
 *
 * @param descriptor Parent end of the stdout pipe, already non-blocking.
 * @param out Outcome whose output is being built.
 * @param capacity In/out allocated size of out->output.
 * @param limit Cap on out->output_size.
 * @param chunk Scratch buffer of _PROCESS_READ_CHUNK bytes.
 * @param settled_max Cap on how much this drain may read, or 0 for "until the pipe is empty".
 *                    Used once the child is collected, to bound a surviving grandchild's flood.
 * @return RESULT_SUCCESS, or the append's refusal.
 */
static Result _process_drain(I32 const descriptor, ProcessOutcome *const out, USize *const capacity, USize const limit, char *const chunk, USize const settled_max) {
    trace_log_push(LOG_METADATA);

    Result result = RESULT_SUCCESS;
    USize drained = 0;

    while (settled_max == 0 || drained < settled_max) {
        /* Clamped to the cap, not merely tested against it: the loop's condition alone would let a
         * final read overshoot by most of a chunk. */
        USize const want = (settled_max != 0 && settled_max - drained < _PROCESS_READ_CHUNK) ? settled_max - drained : (USize) _PROCESS_READ_CHUNK;
        ISize const got = read(descriptor, chunk, want);

        if (got <= 0) {
            /* End of file, would-block, or an error: nothing more is available right now. */
            break;
        }

        drained += (USize) got;
        result = _process_output_append(out, capacity, chunk, (USize) got, limit);

        if (result_is_error(result)) {
            break;
        }
    }

    trace_log_pop();

    return result;
}

/**
 * @brief Give the child a clean signal state: nothing blocked, everything catchable at default.
 *
 * A parent that blocks SIGTERM or ignores SIGPIPE would otherwise hand that to every helper,
 * which then cannot be stopped the way its own author expects. SIGKILL and SIGSTOP are left
 * out because they cannot be set and sigaction would refuse them.
 *
 * @param attributes Spawn attributes to fill; initialized by this call.
 * @param initialized Set true once posix_spawnattr_init succeeded - the caller's cue that
 *                    destroy is owed, whatever happened after.
 * @return 0 on success, or the error posix_spawnattr_* reported.
 */
static I32 _process_signal_defaults(posix_spawnattr_t *const attributes, bool *const initialized) {
    trace_log_push(LOG_METADATA);

    *initialized = false;

    I32 error = posix_spawnattr_init(attributes);

    if (error != 0) {
        trace_log_pop();

        return error;
    }

    *initialized = true;

    sigset_t mask = DEFAULT_INITIALIZATION;
    sigset_t defaults = DEFAULT_INITIALIZATION;

    sigemptyset(&mask);
    sigfillset(&defaults);
    sigdelset(&defaults, SIGKILL);
    sigdelset(&defaults, SIGSTOP);

    error = posix_spawnattr_setsigmask(attributes, &mask);

    if (error == 0) {
        error = posix_spawnattr_setsigdefault(attributes, &defaults);
    }

    if (error == 0) {
        error = posix_spawnattr_setflags(attributes, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
    }

    trace_log_pop();

    return error;
}

/**
 * @brief Kill the child and collect it, unless it is already gone.
 *
 * One last poll immediately before signalling: with SIGCHLD ignored there is no zombie holding
 * the pid, so a child that exited since the previous poll has already released its number -
 * this narrows the stale-pid window from a whole slice to the kernel's own atomicity. It
 * cannot be closed completely with pid-based signalling, which is why the header documents
 * the kill as best effort for callers that ignore SIGCHLD. The reap is bounded like every
 * other wait here: a child pinned in an uninterruptible kernel wait survives SIGKILL until
 * that wait returns.
 *
 * @param child The child's pid.
 * @param status In/out wait status.
 * @param reaped In/out: true once waitpid collected the child.
 * @param vanished In/out: true once waitpid reported ECHILD - the pid is no longer ours to
 *                 signal, so nothing is sent.
 * @return true when SIGKILL was sent; false when the child was already collected or gone, in
 *         which case whatever ended it is its own doing and is reported as such.
 */
static bool _process_kill_collect(pid_t const child, I32 *const status, bool *const reaped, bool *const vanished) {
    trace_log_push(LOG_METADATA);

    bool sent = false;

    if (!*reaped && !*vanished) {
        pid_t const settled = waitpid(child, status, WNOHANG);

        if (settled == child) {
            *reaped = true;
        }
        else if (settled < 0 && errno == ECHILD) {
            *vanished = true;
        }
    }

    if (!*reaped && !*vanished) {
        /* ESRCH means it was auto-reaped between the poll above and this call: nothing was
         * delivered, so nothing may be claimed. */
        sent = kill(child, SIGKILL) == 0;

        U64 const reap_deadline = _process_now_milliseconds() + _PROCESS_REAP_WAIT_MS;

        while (!*reaped) {
            pid_t const settled = waitpid(child, status, WNOHANG);

            if (settled == child) {
                *reaped = true;
            }
            else if (settled < 0 && errno != EINTR) {
                /* Auto-reaped inside the kill window - the same ECHILD the pre-kill poll watches for,
                 * and the same conclusion: nothing was collected, so nothing may be claimed. */
                *vanished = errno == ECHILD;

                break;
            }
            else if (_process_now_milliseconds() >= reap_deadline) {
                break;
            }
            else {
                struct timespec const slice = { .tv_sec = 0, .tv_nsec = _PROCESS_POLL_SLICE_MS * 1000000L };

                nanosleep(&slice, nullptr);
            }
        }
    }

    trace_log_pop();

    return sent;
}

/**
 * @brief POSIX implementation of process_run.
 *
 * @param spec What to run.
 * @param out Receives the outcome; already zeroed by the caller.
 * @return Result of spawning and communicating with the child.
 */
static Result _process_run_posix(ProcessSpec const spec, ProcessOutcome *const out) {
    trace_log_push(LOG_METADATA);

    /* Resolved BEFORE anything is allocated, by the same walker process_exists uses, so what
     * the probe refuses the spawn refuses too. */
    char resolved[_PROCESS_PATH_SIZE_MAX] = DEFAULT_INITIALIZATION;
    I32 resolve_error = 0;

    if (!_process_resolve(spec.argv[0], resolved, sizeof(resolved), &resolve_error)) {
        /* "There but not runnable" is a classification refusal, the same on both backends and
         * under the same macro; a name that is simply absent, or too long, keeps its OS code. */
        Result const refused = resolve_error == EACCES ? PROCESS_RESULT_NOT_EXECUTABLE : result_from_os_code((U32) resolve_error);

        trace_log_pop();

        return refused;
    }

    I32 input_pair[2] = { -1, -1 };

    /* A socketpair rather than a pipe for stdin, so the write below can pass MSG_NOSIGNAL.
     * Writing to a pipe whose reader has exited raises SIGPIPE, whose default action would
     * kill the calling process; a library must not install a process-wide signal handler to
     * protect itself, and this makes that unnecessary. */
#ifdef SOCK_CLOEXEC
    I32 const socket_type = SOCK_STREAM | SOCK_CLOEXEC;
#else
    I32 const socket_type = SOCK_STREAM;
#endif // SOCK_CLOEXEC

    if (socketpair(AF_UNIX, socket_type, 0, input_pair) != 0) {
        Result const result = result_from_os();

        trace_log_pop();

        return result;
    }

    I32 output_pipe[2] = { -1, -1 };

    if (_process_pipe(output_pipe) != 0) {
        /* Captured before the closes: close() can overwrite errno, which would turn this
         * failure into whichever unrelated status the cleanup happened to leave behind. */
        Result const result = result_from_os();

        close(input_pair[0]);
        close(input_pair[1]);

        trace_log_pop();

        return result;
    }

    /* The fallback for a platform without SOCK_CLOEXEC: on Linux the pair was born flagged and
     * these are no-ops. The dup2 targets (0 and 1) lose the flag in the child, and nothing else
     * crosses. */
    _process_cloexec(input_pair[0]);
    _process_cloexec(input_pair[1]);

    /* Raised before any file action references them, so no pipe descriptor can collide with
     * the STDIN/STDOUT numbers the dup2s below target. */
    input_pair[0] = _process_raise(input_pair[0]);
    input_pair[1] = _process_raise(input_pair[1]);
    output_pipe[0] = _process_raise(output_pipe[0]);
    output_pipe[1] = _process_raise(output_pipe[1]);

    posix_spawn_file_actions_t actions = DEFAULT_INITIALIZATION;

    if (posix_spawn_file_actions_init(&actions) != 0) {
        Result const result = result_from_os();

        _process_close_pairs(input_pair, output_pipe);

        trace_log_pop();

        return result;
    }

    /* adddup2 leaves the SOURCE descriptor open at its original number, so without these
     * closes the child keeps a second copy of its own stdout - and a child that then closes
     * fd 1 never produces end of file on the pipe, because its own duplicate still holds the
     * write end. Checked rather than assumed: if an add fails the child would silently
     * inherit the parent's stdio instead of the pipes, and we would wait for output that can
     * never arrive on a pipe nobody is writing to. */
    I32 action_error = posix_spawn_file_actions_adddup2(&actions, input_pair[1], STDIN_FILENO);

    if (action_error == 0) {
        action_error = posix_spawn_file_actions_adddup2(&actions, output_pipe[1], STDOUT_FILENO);
    }

    if (action_error == 0 && spec.stderr_mode == PROCESS_STDERR_MERGE) {
        action_error = posix_spawn_file_actions_adddup2(&actions, output_pipe[1], STDERR_FILENO);
    }

    if (action_error == 0 && spec.stderr_mode == PROCESS_STDERR_DISCARD) {
        action_error = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    }

    if (action_error == 0) {
        action_error = posix_spawn_file_actions_addclose(&actions, input_pair[0]);
    }

    if (action_error == 0) {
        action_error = posix_spawn_file_actions_addclose(&actions, output_pipe[0]);
    }

    /* Both stream numbers are tested, not just each descriptor's own dup2 target: file
     * actions run in order, so closing a source that happens to hold the OTHER stream's
     * number would undo a dup2 already performed. _process_raise makes that unreachable, and
     * these guards keep it unreachable even if the raise could not move a descriptor. */
    if (action_error == 0 && input_pair[1] != STDIN_FILENO && input_pair[1] != STDOUT_FILENO && input_pair[1] != STDERR_FILENO) {
        action_error = posix_spawn_file_actions_addclose(&actions, input_pair[1]);
    }

    if (action_error == 0 && output_pipe[1] != STDIN_FILENO && output_pipe[1] != STDOUT_FILENO && output_pipe[1] != STDERR_FILENO) {
        action_error = posix_spawn_file_actions_addclose(&actions, output_pipe[1]);
    }

#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34))
    /* LAST, after the dup2s and the explicit closes above (file actions run in order): every
     * descriptor above stderr that the PARENT opened without FD_CLOEXEC - a listening socket,
     * an open log - would otherwise reach the child through posix_spawn's plain inheritance. */
    if (action_error == 0) {
        action_error = posix_spawn_file_actions_addclosefrom_np(&actions, STDERR_FILENO + 1);
    }
#endif // glibc >= 2.34

    posix_spawnattr_t attributes = DEFAULT_INITIALIZATION;
    bool attributes_ready = false;

    if (action_error == 0) {
        action_error = _process_signal_defaults(&attributes, &attributes_ready);
    }

    if (action_error != 0) {
        if (attributes_ready) {
            posix_spawnattr_destroy(&attributes);
        }

        posix_spawn_file_actions_destroy(&actions);
        _process_close_pairs(input_pair, output_pipe);

        trace_log_pop();

        return result_from_os_code((U32) action_error);
    }

    pid_t child = 0;

    /* The const cast is what the POSIX signature forces; posix_spawn does not modify argv.
     * posix_spawn on the RESOLVED path, not posix_spawnp on the name: the library's own PATH
     * walk treats an empty entry as the working directory, and the probe above refused that.
     * argv itself is handed over untouched, so argv[0] stays what the caller wrote. */
    I32 const spawned = posix_spawn(&child, resolved, &actions, &attributes, (char *const *) spec.argv, environ);

    posix_spawnattr_destroy(&attributes);
    posix_spawn_file_actions_destroy(&actions);

    /* The child holds its own copies now; keeping ours open would stop the stdout pipe from
     * ever reporting end of file. */
    close(input_pair[1]);
    close(output_pipe[1]);

    if (spawned != 0) {
        close(input_pair[0]);
        close(output_pipe[0]);

        /* posix_spawn returns the error directly and leaves errno untouched, so the code
         * has to be handed over rather than read back from errno. */
        trace_log_pop();

        return result_from_os_code((U32) spawned);
    }

    _process_nonblocking(input_pair[0]);
    _process_nonblocking(output_pipe[0]);

    USize capacity = 0;
    USize written = 0;
    USize const requested = (spec.output_limit > 0) ? spec.output_limit : PROCESS_OUTPUT_LIMIT_DEFAULT;

    /* Clamped so output_size + size + 1 cannot wrap inside the append: the limit is caller-set,
     * and USIZE_MAX would make that sum reach zero and skip the growth branch entirely. */
    USize const limit = (requested > USIZE_MAX - 1) ? (USIZE_MAX - 1) : requested;
    bool writing = spec.input != nullptr && spec.input_size > 0;
    bool reading = true;
    bool reaped = false;
    bool killed = false;
    I32 status = 0;
    Result result = RESULT_SUCCESS;
    U64 const deadline = _process_now_milliseconds() + (U64) spec.timeout_milliseconds;

    if (!writing) {
        close(input_pair[0]);

        input_pair[0] = -1;
    }

    /* Hoisted out of the loop: initializing it per iteration would zero four kilobytes on
     * every poll wake for no benefit, since the read overwrites exactly what it reports. */
    char chunk[_PROCESS_READ_CHUNK] = DEFAULT_INITIALIZATION;

    while (writing || reading) {
        struct pollfd descriptors[2] = DEFAULT_INITIALIZATION;
        nfds_t count = 0;
        I32 write_at = -1;
        I32 read_at = -1;

        if (writing) {
            descriptors[count].fd = input_pair[0];
            descriptors[count].events = POLLOUT;
            write_at = (I32) count;
            count += 1;
        }

        if (reading) {
            descriptors[count].fd = output_pipe[0];
            descriptors[count].events = POLLIN;
            read_at = (I32) count;
            count += 1;
        }

        if (!reaped && waitpid(child, &status, WNOHANG) == child) {
            reaped = true;
        }

        I32 wait_milliseconds = _PROCESS_POLL_SLICE_MS;

        /* The deadline is consulted on every pass, reaped or not: a grandchild that keeps the
         * write end busy would otherwise hold the drain open until the output limit. What the
         * pipe holds is drained (bounded) in the tail, once the child is dead. */
        if (spec.timeout_milliseconds > 0) {
            U64 const now = _process_now_milliseconds();

            if (now >= deadline) {
                out->timed_out = true;

                break;
            }

            U64 const remaining = deadline - now;

            if (remaining < (U64) _PROCESS_POLL_SLICE_MS) {
                wait_milliseconds = (I32) remaining;
            }
        }

        if (reaped) {
            /* The direct child is gone, so the run is over. What it wrote is already in the pipe;
             * a bounded final drain takes all of it and stops, rather than reading on while a
             * background process the child forked keeps the write end busy - wl-copy daemonizes
             * exactly like this to keep serving the selection, and its flood is not the child's
             * output. */
            /* A refusal here is real: no deadline is being met, so an over-limit capture is the
             * failure it always was. */
            Result const drained = _process_drain(output_pipe[0], out, &capacity, limit, chunk, _PROCESS_SETTLED_DRAIN_MAX);

            if (result_is_error(drained)) {
                result = drained;
            }

            break;
        }

        I32 const ready = poll(descriptors, count, wait_milliseconds);

        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }

            result = result_from_os();
            killed = true;

            break;
        }

        if (ready == 0) {
            /* A slice elapsed with nothing to move. Loop so the child-exit check at the top
             * runs again; the deadline check there is what ends a genuinely stuck run. */
            continue;
        }

        if (write_at >= 0 && descriptors[write_at].revents != 0) {
            ISize const sent = send(input_pair[0], spec.input + written, spec.input_size - written, MSG_NOSIGNAL);

            if (sent > 0) {
                written += (USize) sent;
            }

            if (written >= spec.input_size || (sent < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) {
                /* Either everything was delivered, or the child stopped reading (EPIPE).
                 * A child that does not want the rest of its input is not an error here -
                 * it is xclip having taken what it needed. Close so it sees end of file. */
                close(input_pair[0]);

                input_pair[0] = -1;
                writing = false;
            }
        }

        if (read_at >= 0 && descriptors[read_at].revents != 0) {
            ISize const got = read(output_pipe[0], chunk, sizeof(chunk));

            if (got > 0) {
                Result const appended = _process_output_append(out, &capacity, chunk, (USize) got, limit);

                if (result_is_error(appended)) {
                    /* The call has failed; the child is killed below rather than waited out
                     * to its deadline, and reported as KILLED, not as a timeout. */
                    result = appended;
                    killed = true;

                    break;
                }
            }
            else if (got == 0) {
                reading = false;
            }
            else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
                result = result_from_os();
                killed = true;

                break;
            }
        }
    }

    if (input_pair[0] >= 0) {
        close(input_pair[0]);
    }

    bool vanished = false;
    bool sent = false;
    bool const deadline_in_loop = out->timed_out;

    /* The deadline path kills FIRST and drains SECOND: the child must be dead before the pipe
     * is read without waiting, or a flooding child would keep the drain alive to the output
     * limit. What is read is what it wrote up to the kill - the capture to the deadline, as
     * on Windows - and a child that had already ended is collected with its whole output. */
    if (deadline_in_loop) {
        sent = _process_kill_collect(child, &status, &reaped, &vanished);

        /* The deadline wins over the drain's limit, as on Windows: a grandchild still flooding
         * the pipe fills the capture to the cap and the run stays a timeout. */
        if (reading) {
            Result const drained = _process_drain(output_pipe[0], out, &capacity, limit, chunk, 0);

            if (result_is_error(drained) && result_clear_flag(drained, RESULT_FLAG_PARTIAL) != PROCESS_RESULT_OUTPUT_TOO_LARGE) {
                result = drained;
            }
        }
    }

    /* Closed BEFORE the wait, for the same reason as the Windows path: a child still writing
     * to a pipe nobody drains blocks forever, so waiting on it first would be a deadlock
     * rather than a wait. With the read end gone its next write fails instead. */
    close(output_pipe[0]);

    if (!reaped && !killed && !out->timed_out) {
        /* Bounded, not a blocking waitpid. The loop can exit with the child still alive -
         * having closed its own stdout and lingered - and waiting forever on that is how a
         * call with a deadline becomes a call without one. A killed or timed-out child skips
         * this: it is being collected, not waited for. */
        while (true) {
            pid_t const settled = waitpid(child, &status, WNOHANG);

            if (settled == child) {
                reaped = true;

                break;
            }

            if (settled < 0 && errno != EINTR) {
                /* ECHILD does not mean "still running" - it means the child is ALREADY GONE,
                 * auto-reaped by the kernel because this process ignores SIGCHLD, which is
                 * exactly what a daemon does. `child` is a stale pid from here on, so it must
                 * never be signalled: the number may already belong to something else. */
                vanished = (errno == ECHILD);

                break;
            }

            if (spec.timeout_milliseconds > 0 && _process_now_milliseconds() >= deadline) {
                out->timed_out = true;

                break;
            }

            struct timespec const slice = { .tv_sec = 0, .tv_nsec = _PROCESS_POLL_SLICE_MS * 1000000L };

            nanosleep(&slice, nullptr);
        }
    }

    /* It overran the bounded wait, or its capture failed: kill and collect - no path may leave
     * a child running or a zombie behind. A deadline met inside the loop did this above, before
     * its drain; one met during the bounded wait comes through here. The helper is a no-op for a
     * child already collected or vanished. */
    if (!deadline_in_loop) {
        sent = _process_kill_collect(child, &status, &reaped, &vanished);
    }

    /* `reaped` gates this deliberately. status starts at 0, and WIFEXITED(0)/WEXITSTATUS(0)
     * read as "exited cleanly with 0" - so a wait that never collected the child would report
     * success for a child whose fate is unknown. That is the failure mode a daemon hits:
     * with SIGCHLD ignored or auto-reaped, waitpid returns ECHILD and every caller gating on
     * exit_code == 0 would be told the child succeeded. Fail closed instead: UNKNOWN. */
    if (reaped && WIFEXITED(status)) {
        /* It got there on its own, even if a deadline or a kill was on its way: the status is
         * the child's, so it is reported, and no kill is claimed. */
        out->ended = PROCESS_ENDED_EXITED;
        out->exit_code = (I32) WEXITSTATUS(status);
        out->timed_out = false;
    }
    else if (reaped && WIFSIGNALED(status)) {
        out->signal = (I32) WTERMSIG(status);

        /* Only a SIGKILL this module SENT is its kill. A child that died of its own signal while
         * a deadline or a capture failure was pending is reported as that death - the truth, as
         * the header promises - not as a kill that never happened. */
        if (sent) {
            out->ended = out->timed_out ? PROCESS_ENDED_TIMED_OUT : PROCESS_ENDED_KILLED;
        }
        else {
            out->ended = PROCESS_ENDED_SIGNALED;
            out->timed_out = false;
        }
    }
    else if (vanished) {
        /* Auto-reaped by a parent that ignores SIGCHLD: nothing was collected and nothing was
         * sent, so no deadline or kill this module intended may be claimed. UNKNOWN, as the
         * header documents that case. */
        out->ended = PROCESS_ENDED_UNKNOWN;
        out->timed_out = false;
    }
    else if (out->timed_out) {
        out->ended = PROCESS_ENDED_TIMED_OUT;
    }
    else if (killed) {
        out->ended = PROCESS_ENDED_KILLED;
    }

    trace_log_pop();

    return result;
}
#endif // OS_WINDOWS

/*==============================================================================
 * MARK: - Process
 *============================================================================*/

bool process_exists(char const *const program) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "program", (void*) program);

    if (program[0] == '\0') {
        trace_log_pop();

        return false;
    }

    bool found = false;

#ifdef OS_WINDOWS
    char resolved[MAX_PATH] = DEFAULT_INITIALIZATION;
    DWORD resolved_size = 0;

    /* The same resolver process_run spawns through: a true here means the spawn will run this
     * file, and everything it refuses (a batch file, a directory) is "not found" here. */
    found = result_is_success(_process_resolve(program, resolved, (DWORD) sizeof(resolved), &resolved_size)) && resolved_size != 0;
#else
    char resolved[_PROCESS_PATH_SIZE_MAX] = DEFAULT_INITIALIZATION;

    /* The same walker process_run spawns through, so a true here means the spawn will find
     * the same file - and refuse the same empty PATH entries. */
    I32 resolve_error = 0;

    found = _process_resolve(program, resolved, sizeof(resolved), &resolve_error);
#endif // OS_WINDOWS

    trace_log_pop();

    return found;
}

bool process_outcome_aborted(ProcessOutcome const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

#ifdef OS_WINDOWS
    /* Windows has no signals. abort() is a fast-fail under the UCRT, reported as the NTSTATUS
     * STATUS_STACK_BUFFER_OVERRUN; under the legacy msvcrt it is a plain exit(3), which a child
     * could also choose on purpose - the price of the older runtime, and the header says so. */
    bool const aborted = self->ended == PROCESS_ENDED_EXITED && (self->exit_code == (I32) 0xC0000409u || self->exit_code == 3);
#else
    bool const aborted = self->ended == PROCESS_ENDED_SIGNALED && self->signal == SIGABRT;
#endif // OS_WINDOWS

    trace_log_pop();

    return aborted;
}

bool process_outcome_succeeded(ProcessOutcome const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    bool const succeeded = self->ended == PROCESS_ENDED_EXITED && self->exit_code == 0;

    trace_log_pop();

    return succeeded;
}

void process_outcome_uninit(ProcessOutcome *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (!memory_empty(self->output)) {
        memory_delete((void**) &self->output);
    }

    /* Reset to "never collected", not to a clean exit: a released outcome must not read as a
     * child that succeeded. */
    self->output = nullptr;
    self->output_size = 0;
    self->ended = PROCESS_ENDED_UNKNOWN;
    self->exit_code = -1;
    self->signal = 0;
    self->timed_out = false;

    trace_log_pop();
}

Result process_run(ProcessSpec const spec, ProcessOutcome *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "out", (void*) out);
    error_check_null(LOG_METADATA, "spec.argv", (void*) spec.argv);
    error_check_null(LOG_METADATA, "spec.argv[0]", (void*) (spec.argv != nullptr ? spec.argv[0] : nullptr));
    error_check_out_of_bound_uint(LOG_METADATA, "spec.stderr_mode", (USize) spec.stderr_mode, "PROCESS_STDERR_DISCARD", (USize) PROCESS_STDERR_DISCARD,
        "spec.stderr_mode > PROCESS_STDERR_DISCARD", spec.stderr_mode > PROCESS_STDERR_DISCARD);

    /* Zeroed before anything can fail, so every path out - including the early returns in
     * the backends - leaves an outcome that is safe to hand to process_outcome_uninit. The
     * fate starts as "unknown" with no status, never as a clean exit. */
    memory_set(out, sizeof(ProcessOutcome), 0);

    out->exit_code = -1;

#ifdef OS_WINDOWS
    Result result = _process_run_windows(spec, out);
#else
    Result result = _process_run_posix(spec, out);
#endif // OS_WINDOWS

    /* A child that wrote nothing yields an owned "" rather than nullptr, so a successful
     * run's text never needs a null guard. Only after success: a failed run keeps whatever
     * partial capture it holds, which may honestly be nothing. */
    if (result_is_success(result) && memory_empty(out->output)) {
        out->output = (char*) memory_try_alloc(1);

        if (memory_empty(out->output)) {
            result = PROCESS_RESULT_NO_MEMORY;
        }
    }

    trace_log_pop();

    return result;
}