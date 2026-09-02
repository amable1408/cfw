/*
 * process.h - Spawn a child program with piped stdin/stdout and capture what it writes.
 *
 * Features:
 *   - Run a program from an argument vector (process_run), feeding it bytes on stdin and
 *     capturing its stdout into an owned buffer; stderr is inherited, merged into that
 *     capture, or discarded (ProcessSpec.stderr_mode).
 *   - Report how the child ended (ProcessOutcome.ended: exited, killed by a signal, timed out,
 *     killed for a failed capture, or never collected), its exit code and the signal that
 *     ended it, and kill it when a timeout elapses.
 *   - Ask whether a program is resolvable and executable without spawning it
 *     (process_exists).
 *
 * Usage Examples:
 *   @code
 *   char const *const argv[] = { "xclip", "-selection", "clipboard", nullptr };
 *   ProcessSpec const spec = {
 *       .argv = argv,
 *       .input = "hello, world",
 *       .input_size = 12,
 *       .timeout_milliseconds = 2000,
 *   };
 *
 *   ProcessOutcome outcome = DEFAULT_INITIALIZATION;
 *   Result const result = process_run(spec, &outcome);
 *
 *   if (result_is_success(result) && process_outcome_succeeded(&outcome)) {
 *       / * outcome.output holds the child's stdout * /
 *   }
 *
 *   process_outcome_uninit(&outcome);   / * on EVERY path - see Memory Management * /
 *   @endcode
 *
 *   A vector that arrives as `char **argv` (from main, or one you built) needs one cast at the
 *   call site, because C does not convert `char **` to `char const *const *` implicitly:
 *   `.argv = (char const *const *) argv`.
 *
 * Error Handling:
 *   - process_run returns a Result built from the OS status: the spawn failing, a pipe
 *     failing, the output exceeding its limit (PROCESS_RESULT_OUTPUT_TOO_LARGE), memory for
 *     the capture running out (PROCESS_RESULT_NO_MEMORY), a program that is there but cannot be
 *     run (PROCESS_RESULT_NOT_EXECUTABLE), or - on Windows - a batch-file target being refused
 *     (PROCESS_RESULT_BATCH_REFUSED). A child that runs to completion
 *     and exits non-zero is a SUCCESS Result with a non-zero outcome.exit_code - the process
 *     ran, and what it decided is the caller's business, not an OS error.
 *   - A timeout is likewise a SUCCESS Result with outcome.ended == PROCESS_ENDED_TIMED_OUT
 *     (timed_out mirrors it): the caller asked for a deadline and got it. Check the outcome,
 *     not the Result. process_outcome_succeeded answers the whole question in one call.
 *   - The output-limit refusal carries RESULT_FLAG_PARTIAL when a prefix was retained: the
 *     outcome then holds the first output_limit bytes and must still be released. It is kept
 *     distinct from PROCESS_RESULT_NO_MEMORY so a runaway child (worth logging) can be told
 *     from memory exhaustion (not the child's doing).
 *   - process_exists is a search, so it reports "not found" as false rather than a Result.
 *   - Argument validation uses error_check_*, which ABORTS under ERROR_CHECK_ENABLED: a null
 *     argv, a null argv[0], or a null out pointer is a programmer error, not a runtime
 *     condition, and so is a stderr_mode outside the enum. Everything the DATA decides - an
 *     empty program name, a missing program, a batch file, a runaway output - is refused through
 *     the Result or the outcome, never an abort.
 *
 * Security:
 *   - argv is an ARGUMENT VECTOR, never a shell command line. No shell is ever spawned, so
 *     there is no metacharacter, word-splitting, or globbing surface: a value containing
 *     `; rm -rf /` is one literal argument. This is the module's reason to exist - popen and
 *     system take a string and hand it to a shell, which is why neither is used here.
 *   - On Windows that guarantee holds only because .bat and .cmd targets are REFUSED
 *     (PROCESS_RESULT_BATCH_REFUSED; process_exists reports them as not found): CreateProcess
 *     silently runs a batch file through cmd.exe, which re-parses the command line with its
 *     own rules - `&`, `|`, `%VAR%` and `^` inside an argument would be interpreted
 *     (CVE-2024-24576). A caller that must run a batch file spawns cmd.exe explicitly and owns
 *     the quoting.
 *   - PATH resolution differs by platform. On POSIX process_run and process_exists share one
 *     walker that refuses empty PATH entries and directories, and the spawn runs the path that
 *     walker resolved (posix_spawn, never posix_spawnp's own search, which treats an empty entry
 *     as the working directory) - so an EMPTY entry never resolves a bare name out of the
 *     working directory; a `.` or a relative entry in PATH is the caller's own choice and is
 *     honoured. Windows resolution (SearchPath / CreateProcess) includes the current directory,
 *     and a directory is never "found" on either platform. Pass an absolute path when the
 *     working directory is not trusted.
 *   - On Windows the child inherits ONLY its three standard streams: the inheritable-handle
 *     list names exactly those, so a listening socket or an open log file in the parent never
 *     reaches a helper.
 *   - On POSIX every descriptor this module creates is close-on-exec, so two concurrent runs
 *     cannot hold each other's pipes open; and on glibc 2.34 or newer every parent descriptor
 *     above stderr is closed in the child as well (posix_spawn_file_actions_addclosefrom_np).
 *     On an older glibc, or another libc, a descriptor the parent opened WITHOUT FD_CLOEXEC still
 *     reaches the child - open sockets and files with O_CLOEXEC / SOCK_CLOEXEC there.
 *   - Prefer feeding untrusted or large data through spec.input rather than as an argument.
 *     Arguments are visible to every user on the machine (ps, Task Manager) and are bounded
 *     by ARG_MAX; stdin is neither.
 *   - Captured output is bounded by spec.output_limit, because a child's output size is not
 *     something the caller controls. An unbounded read of a hostile or runaway child is an
 *     out-of-memory condition, so the read stops, the child is killed, and the call fails.
 *
 * Platform Support:
 *   - Windows (CreateProcess on the image SearchPath resolved) and POSIX (posix_spawn on the
 *     path the module's own walker resolved). Both capture stdout; stderr goes where
 *     ProcessSpec.stderr_mode says. Merged stderr interleaves with stdout at pipe granularity,
 *     not line by line.
 *   - On Windows the argument vector is joined into a command line using the quoting rules
 *     CommandLineToArgvW parses, so argv[1..] round-trips into the child's argv unchanged.
 *     argv[0] is handed to CreateProcess as the image SearchPath resolved; only a name SearchPath
 *     cannot resolve falls back to CreateProcess's own image-name parser, which honours quotes
 *     but not backslash escapes - pass a plain program name or path. The join is an
 *     implementation detail; callers always pass a vector.
 *   - The Windows backend uses the ANSI entry points: argv and the captured bytes are in the
 *     active code page, so a path outside it (say an accented name on a Latin-1 system) fails
 *     to resolve or reaches the child mangled. A UTF-8 port is a separate item.
 *   - Windows has no signals. A child that crashes or aborts ends as PROCESS_ENDED_EXITED with
 *     the NTSTATUS in exit_code (abort() under the UCRT is 0xC0000409), and outcome.signal
 *     stays 0. On POSIX a PROCESS_ENDED_SIGNALED outcome names the signal.
 *   - The POSIX child starts with an empty signal mask and every catchable signal at its
 *     default action, whatever the parent had blocked or ignored - a server that ignores
 *     SIGPIPE or blocks SIGTERM does not hand that to its helpers.
 *   - The child inherits the parent's working directory and environment. There is
 *     deliberately no override for either yet; add one when a caller needs it rather than
 *     guessing at the shape.
 *
 * Limits:
 *   - The timeout kill is BEST EFFORT for a caller that ignores SIGCHLD (SIG_IGN, or an
 *     auto-reaping handler - i.e. most daemons). Without a zombie to hold the pid, a child
 *     that exits between the last wait and the kill has already released its number, and a
 *     pid-based signal cannot be made atomic against that. The window is one syscall wide and
 *     the module re-checks immediately before signalling, but it is not zero. Callers that
 *     need it to be zero should not ignore SIGCHLD.
 *   - The kill reaches the direct child only. A timed-out `make` leaves its compilers running;
 *     a process-group / Job Object kill is a deliberate non-default, because callers exist
 *     whose helper forks a survivor on purpose (wl-copy owning a selection).
 *   - The Windows clock behind timeout_milliseconds ticks at about 15.6 ms, so a timeout of 1
 *     means "somewhere between 0 and 16 ms".
 *   - At the deadline whatever the stdout pipe already holds is drained (bounded by the output
 *     limit) before the call returns, so the capture is what arrived before the deadline, and a
 *     child that ended on its own just before it is reported EXITED with its whole output. If
 *     that drain hits the output limit the run stays a timeout - the deadline was the caller's
 *     ask - and the Result stays SUCCESS: a truncation there is detected by output_size equal
 *     to the EFFECTIVE limit (spec.output_limit, or PROCESS_OUTPUT_LIMIT_DEFAULT when it is left
 *     at 0), not by a flag - a flagged success would read as an error to result_is_success, so
 *     RESULT_FLAG_PARTIAL rides only on the failing forms.
 *   - Once the direct child is collected the run ends: a final drain takes what it wrote (all
 *     of it, for a default-capacity pipe - a dead writer's bytes are all in the pipe) and stops
 *     at 64 KiB. A child that enlarged its own stdout pipe past that can have its tail dropped.
 *     A background process the child forked keeps writing to a pipe nobody reads; its flood is
 *     not the child's output and is not captured.
 *   - The POSIX walker judges "executable" with access(X_OK), which answers for the REAL user
 *     id while exec uses the EFFECTIVE one: in a setuid or setgid program the probe and the
 *     spawn can disagree. The module is not meant for such programs.
 *   - A killed child is waited for at most 5 seconds. A process pinned in an uninterruptible
 *     kernel wait (a hung filesystem driver) survives TerminateProcess and SIGKILL until that
 *     wait returns, and TerminateProcess itself can be refused; past the bound the call returns
 *     with ended TIMED_OUT or KILLED and the child may still be winding down - on POSIX it then
 *     becomes a zombie nobody reaps. TerminateProcess is asynchronous, so bytes a child writes
 *     during its own teardown can land in the deadline drain: "what arrived before the deadline"
 *     is exact to the termination latency.
 *   - On glibc older than 2.24 posix_spawn cannot report an exec failure through its return
 *     value: the child exits 127 and the Result is success. Treat exit_code 127 with no
 *     output as "not found" on such systems.
 *
 * Thread Safety:
 *   - Stateless free functions with no shared state; concurrent calls are independent, and
 *     because this module's own descriptors and handles are never inheritable, one run's pipes
 *     are invisible to another run's child.
 *   - The POSIX path reads the environment twice - PATH while resolving, environ at spawn -
 *     without copying; env's own rule - environment writes are a startup-only, main-thread
 *     activity - is what keeps those reads safe.
 *   - The POSIX path never raises SIGPIPE at the caller: stdin is a socketpair written with
 *     MSG_NOSIGNAL, so a child that exits early yields EPIPE rather than a signal that would
 *     kill the calling process. No process-wide signal disposition is installed, which is
 *     what makes this safe to call from a library and from any thread.
 *
 * Memory Management:
 *   - outcome.output is a heap buffer the caller owns; after a SUCCESS Result it is never
 *     null - a child that wrote nothing yields an owned "" so the text can be used without a
 *     null guard (should even that one byte fail to allocate, the Result is
 *     PROCESS_RESULT_NO_MEMORY while ended and exit_code still describe the completed run).
 *     Release the outcome with process_outcome_uninit on EVERY path, including a
 *     failed run: a failure can still carry partial output. process_run zeroes out without
 *     releasing any buffer it already holds, so an outcome reused across runs must be released
 *     before running again. Releasing a zeroed or already-released outcome is a no-op.
 *   - The buffer grows geometrically as the child writes and uses memory_try_alloc rather
 *     than memory_alloc: the size is driven by another process, so exhausting memory must
 *     fail the call, never abort the program.
 *
 * Performance Characteristics:
 *   - One spawn plus a readiness loop that services stdin and stdout concurrently (poll on
 *     POSIX; a peek loop plus a writer thread on Windows, whose anonymous pipes cannot be
 *     polled). Writing all input before reading any output would deadlock the moment the
 *     child filled the stdout pipe, so both directions are always in flight together.
 *
 * Dependencies:
 *   - <memory/memory.h> for memory_try_alloc / memory_delete / memory_empty;
 *     <result.h> for Result.
 *   - Windows: <thread/thread.h> for the stdin writer thread. A Windows build with
 *     TRACELOG_ENABLED also REQUIRES LOG_THREAD_IMPLEMENTATION (enforced by an #error in
 *     process.c): that thread traces, and the trace stack is per-thread only under it.
 *   - POSIX: <spawn.h>, <poll.h>, <sys/socket.h>, <sys/wait.h>, <fcntl.h>, <signal.h>,
 *     <sys/stat.h>, <time.h>, <unistd.h>. Linux REQUIRES -D_GNU_SOURCE: pipe2 and
 *     posix_spawn_file_actions_addclosefrom_np are glibc extensions (the rest -
 *     clock_gettime/CLOCK_MONOTONIC, nanosleep, kill, F_DUPFD_CLOEXEC - would be satisfied by
 *     _POSIX_C_SOURCE >= 200809L alone). Without it the build fails outright under a strict
 *     -std=c23 rather than degrading. Every build/linux makefile and the public export define it.
 *
 * Notes:
 *   - outcome.output is always NUL-terminated, so text output can be used as a C string
 *     directly. Binary output is equally valid - use outcome.output_size, which excludes that
 *     terminator.
 *
 * See process.c for implementation details.
 */

#ifndef PROCESS_H
#define PROCESS_H

#include <memory/memory.h>
#include <result.h>

/* The platform block trails the framework includes rather than leading, because it is
 * guarded on OS_WINDOWS - which types.h defines and result.h chains in. result.h itself
 * splits on the same condition, so this must not re-list what that split already
 * provides: <platform/windows/windows.h> on the Windows side, <errno.h> on this one. */
#ifdef OS_WINDOWS
#include <thread/thread.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#endif // OS_WINDOWS

/*==============================================================================
 * MARK: - Macros and Constants
 *============================================================================*/

/** Cap applied to captured stdout when ProcessSpec.output_limit is left at 0 (16 MiB). */
#define PROCESS_OUTPUT_LIMIT_DEFAULT (16U * 1024U * 1024U)

/**
 * process_run refused a .bat / .cmd target (Windows only): it would run through cmd.exe. The
 * code is ERROR_BAD_EXE_FORMAT (193), spelled out so the macro stays platform-neutral; a non-PE
 * .exe fails the spawn with the SAME code from the OS under RESULT_CATEGORY_SYSTEM - the
 * category is what tells the two apart.
 */
#define PROCESS_RESULT_BATCH_REFUSED result_make(RESULT_CATEGORY_ARGUMENT, 193U, 0)

/** An allocation for the captured output failed (RESULT_FLAG_PARTIAL when a prefix was retained). */
#define PROCESS_RESULT_NO_MEMORY result_make(RESULT_CATEGORY_MEMORY, (U32) ENOMEM, 0)

/** The program is there but cannot be run: a directory, or a file without the execute bit.
 *  Both backends refuse it the same way; the OS's own code (ERROR_ACCESS_DENIED / EACCES) rides
 *  inside. A name that is simply absent keeps its native ENOENT / ERROR_FILE_NOT_FOUND. */
#ifdef OS_WINDOWS
#define PROCESS_RESULT_NOT_EXECUTABLE result_make(RESULT_CATEGORY_ARGUMENT, 5U, 0)
#else
#define PROCESS_RESULT_NOT_EXECUTABLE result_make(RESULT_CATEGORY_ARGUMENT, (U32) EACCES, 0)
#endif // OS_WINDOWS

/** The child produced more than ProcessSpec.output_limit allows (RESULT_FLAG_PARTIAL when a
 *  prefix was retained). */
#define PROCESS_RESULT_OUTPUT_TOO_LARGE result_make(RESULT_CATEGORY_SYSTEM, (U32) EFBIG, 0)

/** The program's resolved path does not fit the platform's path buffer, so it could not be
 *  classified (Windows). The code is ERROR_FILENAME_EXCED_RANGE (206). */
#define PROCESS_RESULT_PATH_TOO_LONG result_make(RESULT_CATEGORY_ARGUMENT, 206U, 0)

/** Module version (SemVer). 0.y until cwd/environment control and a UTF-8 Windows backend land. */
#define PROCESS_VERSION "0.9.0"

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief How the child ended - the fact process_run collected, independent of the Result.
 */
typedef enum {
    /** Never collected: the spawn failed, or the child was auto-reaped (SIGCHLD ignored). */
    PROCESS_ENDED_UNKNOWN = 0,

    /** Ran to completion; exit_code is its status. On Windows this covers crashes too. */
    PROCESS_ENDED_EXITED,

    /** Killed by a signal this module did not send (POSIX); ProcessOutcome.signal names it. */
    PROCESS_ENDED_SIGNALED,

    /** Killed by this module because the deadline elapsed. */
    PROCESS_ENDED_TIMED_OUT,

    /** Killed by this module because the run failed around it: the capture (output limit,
     *  memory, a read error) or, on Windows, the stdin writer that could not start. */
    PROCESS_ENDED_KILLED
} ProcessEnded;

/* A kill races the child's own exit on both platforms. When the child got there first its
 * status is the truth: it is reported as EXITED with that status, whatever the Result says -
 * a capture failure is still a failed Result, but the fate recorded is the child's. */

/**
 * @brief Where the child's stderr goes.
 */
typedef enum {
    /** The parent's stderr (the default): diagnostics reach the terminal, not the capture. */
    PROCESS_STDERR_INHERIT = 0,

    /** The same pipe as stdout: captured, interleaved at pipe granularity. */
    PROCESS_STDERR_MERGE,

    /** The null device. */
    PROCESS_STDERR_DISCARD
} ProcessStderr;

/**
 * @brief What to run, what to feed it, and how long to wait for it.
 */
typedef struct {
    /** NULL-terminated argument vector; argv[0] is the program. Never a shell command line. */
    char const *const *argv;

    /** Bytes written to the child's stdin, or nullptr to hand it an immediate end of file. */
    char const *input;

    /** Size of input in bytes. Ignored when input is nullptr. */
    USize input_size;

    /** Cap on captured stdout in bytes; 0 selects PROCESS_OUTPUT_LIMIT_DEFAULT. */
    USize output_limit;

    /** Where stderr goes; the zero value inherits the parent's. */
    ProcessStderr stderr_mode;

    /** Milliseconds before the child is killed; 0 waits for it indefinitely. */
    U32 timeout_milliseconds;
} ProcessSpec;

/**
 * @brief What the child wrote, and how it ended.
 */
typedef struct {
    /** Captured stdout, NUL-terminated; caller owns it. Released by process_outcome_uninit. */
    char *output;

    /** Bytes in output, excluding the terminator. */
    USize output_size;

    /** How the child ended. Read this before exit_code: only EXITED makes exit_code a status. */
    ProcessEnded ended;

    /**
     * The child's exit status when ended == PROCESS_ENDED_EXITED, otherwise -1. On Windows a
     * crash or abort is an EXITED status holding the NTSTATUS. A child may itself exit with a
     * value that maps to -1, which is why `ended`, not this field, says whether it exited.
     */
    I32 exit_code;

    /** The signal that ended the child when ended == PROCESS_ENDED_SIGNALED, TIMED_OUT or
     *  KILLED on POSIX - 0 when a kill overran the reap bound and nothing was collected, and
     *  always 0 on Windows. */
    I32 signal;

    /** true when ended == PROCESS_ENDED_TIMED_OUT; kept for callers written against it. */
    bool timed_out;
} ProcessOutcome;

/*==============================================================================
 * MARK: - Process
 *============================================================================*/

/**
 * @brief Report whether a program can be resolved and executed.
 *
 * Searches PATH when the name has no directory separator, and checks the path directly when
 * it does. Inherently racy - the program may vanish before a following run - so treat it as a
 * capability probe, not a guarantee. Agrees with process_run: a .bat / .cmd file that
 * process_run would refuse is reported as not found here.
 *
 * @param program Program name or path (must not be nullptr).
 * @return true when an executable of that name was found.
 */
bool process_exists(char const *const program);

/**
 * @brief Report whether the child died the way abort() dies on this platform.
 *
 * The death-test predicate: on POSIX `ended == PROCESS_ENDED_SIGNALED && signal == SIGABRT`;
 * on Windows, which has no signals, an EXITED status of 0xC0000409 (the UCRT's fast-fail) or
 * 3 (the legacy msvcrt's abort, which a child could also exit with on purpose).
 *
 * @param self Outcome to inspect (must not be nullptr).
 * @return true when the child aborted.
 */
bool process_outcome_aborted(ProcessOutcome const *const self);

/**
 * @brief Report whether the child ran to completion and exited 0.
 *
 * The one-call form of `ended == PROCESS_ENDED_EXITED && exit_code == 0`. Pair it with the
 * Result: a failed spawn has nothing to succeed at.
 *
 * @param self Outcome to inspect (must not be nullptr).
 * @return true when the child exited with status 0.
 */
bool process_outcome_succeeded(ProcessOutcome const *const self);

/**
 * @brief Release an outcome's captured output and reset it.
 *
 * A released outcome reads as never collected (ended UNKNOWN, exit_code -1), so it cannot be
 * mistaken for a child that exited cleanly. Safe on a zeroed outcome, on one from a failed
 * run, and on one already released.
 *
 * @param self Outcome to release (must not be nullptr).
 */
void process_outcome_uninit(ProcessOutcome *const self);

/**
 * @brief Run a program to completion, feeding it stdin and capturing its stdout.
 *
 * Both pipe directions are serviced together, so an input larger than the pipe buffer and an
 * output larger than the pipe buffer are both safe. How the child ended is reported through
 * out, not through the Result - see the header's Error Handling section. A child still
 * running when the capture fails is killed at once, not waited out.
 *
 * @param spec What to run (spec.argv and spec.argv[0] must not be nullptr; spec.stderr_mode
 *             must be one of the enum's values - anything else is a programming error).
 * @param out Receives the outcome; zeroed first, so it is always safe to pass to
 *            process_outcome_uninit afterwards (must not be nullptr). A buffer it already
 *            held is NOT released - see Memory Management.
 * @return Result of spawning and communicating with the child.
 */
Result process_run(ProcessSpec const spec, ProcessOutcome *const out);

#endif // PROCESS_H