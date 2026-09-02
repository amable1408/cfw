/*
 * test_all.c - Behavioral tests for include/process/process.c.
 *
 * The suite spawns ITSELF as the child, re-entered through the --child-* arguments below.
 * That keeps the tests identical on Windows and POSIX without depending on cat, sleep, or
 * any other external tool being installed - and it makes the argument round-trip case exact,
 * because the child can print back precisely what it received in its own argv.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <log/log.h>
#include <process/process.h>
#include <test/test.h>

#ifdef OS_WINDOWS
#include <fcntl.h>
#include <io.h>
#include <platform/windows/windows.h>
#else
#include <time.h>
#include <unistd.h>
#endif // OS_WINDOWS

/*==============================================================================
 * MARK: - Macros and Constants
 *============================================================================*/

/** Bytes pushed through the child in the deadlock case; far past any pipe buffer. */
#define _TEST_LARGE_SIZE (256 * 1024)

/*==============================================================================
 * MARK: - File Scope
 *============================================================================*/

/** Path this binary was invoked with, reused as the program every spawn runs. */
static char const *_program = nullptr;

/**
 * @brief Milliseconds from a monotonic source, for asserting that a call did not hang.
 *
 * Monotonic rather than wall clock: a clock adjustment mid-test would otherwise turn a
 * passing run into a failure, or hide a real hang.
 *
 * @return Current value of the monotonic millisecond counter.
 */
static U64 _test_now_milliseconds(void) {
#ifdef OS_WINDOWS
    return (U64) GetTickCount64();
#else
    struct timespec now = DEFAULT_INITIALIZATION;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }

    return ((U64) now.tv_sec * 1000U) + ((U64) now.tv_nsec / 1000000U);
#endif // OS_WINDOWS
}

/**
 * @brief Point this process's stderr - the handle a child INHERITS - at a scratch file, so the
 *        test can see what an inherited stderr received and prove that DISCARD received nothing.
 *
 * On Windows the inherited handle is the Win32 STD_ERROR_HANDLE, not the CRT's fd 2, so
 * SetStdHandle is the switch; the CRT-level _dup2 would leave the child's inheritance untouched.
 *
 * @param path Scratch file to receive stderr.
 * @param saved Receives what to restore.
 * @return true when the redirection is in place.
 */
#ifdef OS_WINDOWS
static bool _stderr_to_file_begin(char const *const path, HANDLE *const saved) {
    *saved = GetStdHandle(STD_ERROR_HANDLE);

    SECURITY_ATTRIBUTES inheritable = { .nLength = sizeof(SECURITY_ATTRIBUTES), .bInheritHandle = TRUE };
    HANDLE const file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, &inheritable, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    SetStdHandle(STD_ERROR_HANDLE, file);

    return true;
}

/**
 * @brief Restore the stderr handle _stderr_to_file_begin replaced, and close the scratch file.
 * @param saved The handle it saved.
 */
static void _stderr_to_file_end(HANDLE const saved) {
    HANDLE const file = GetStdHandle(STD_ERROR_HANDLE);

    SetStdHandle(STD_ERROR_HANDLE, saved);
    CloseHandle(file);
}
#else
static bool _stderr_to_file_begin(char const *const path, I32 *const saved) {
    /* With fd 2 already closed the open below could land ON 2, and the dup/dup2 dance would
     * then close the very descriptor it installed: refuse to redirect what does not exist. */
    if (fcntl(STDERR_FILENO, F_GETFD) < 0) {
        return false;
    }

    /* Opened first, saved second: a failed open then owes nothing, and a failed dup is caught
     * before stderr is redirected to a descriptor that could never be restored. */
    I32 const file = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);

    if (file < 0) {
        return false;
    }

    *saved = dup(STDERR_FILENO);

    if (*saved < 0) {
        close(file);

        return false;
    }

    dup2(file, STDERR_FILENO);
    close(file);

    return true;
}

/**
 * @brief Restore the stderr descriptor _stderr_to_file_begin replaced.
 * @param saved The descriptor it saved.
 */
static void _stderr_to_file_end(I32 const saved) {
    dup2(saved, STDERR_FILENO);
    close(saved);
}
#endif // OS_WINDOWS

/**
 * @brief Read a small scratch file whole.
 * @param path File to read.
 * @param buffer Receives the bytes, NUL-terminated.
 * @param buffer_size Capacity of buffer.
 */
static void _read_small_file(char const *const path, char *const buffer, USize const buffer_size) {
    buffer[0] = '\0';

    FILE *const file = fopen(path, "rb");

    if (file == nullptr) {
        return;
    }

    USize const got = fread(buffer, 1, buffer_size - 1, file);

    buffer[got] = '\0';
    fclose(file);
}

/*==============================================================================
 * MARK: - Child Modes
 *============================================================================*/

/**
 * @brief Copy stdin to stdout verbatim.
 *
 * Both streams are put into binary mode on Windows: the default text mode would translate
 * newlines on the way through and turn a byte-exact round trip into a mismatch that looks
 * like a module defect rather than a harness one.
 *
 * @return Exit code for the child.
 */
static I32 _child_echo(void) {
#ifdef OS_WINDOWS
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif // OS_WINDOWS

    char buffer[4096] = DEFAULT_INITIALIZATION;
    USize got = fread(buffer, 1, sizeof(buffer), stdin);

    while (got > 0) {
        fwrite(buffer, 1, got, stdout);

        got = fread(buffer, 1, sizeof(buffer), stdin);
    }

    fflush(stdout);

    return 0;
}

/**
 * @brief Print every remaining argument, one per line.
 *
 * @param argc Argument count as main received it.
 * @param argv Argument vector as main received it.
 * @return Exit code for the child.
 */
static I32 _child_arguments(int argc, char **argv) {
#ifdef OS_WINDOWS
    _setmode(_fileno(stdout), _O_BINARY);
#endif // OS_WINDOWS

    for (I32 i = 2; i < (I32) argc; i += 1) {
        fputs(argv[i], stdout);
        fputc('\n', stdout);
    }

    fflush(stdout);

    return 0;
}

/**
 * @brief Sleep far longer than any test is willing to wait, so the timeout must fire.
 * @return Exit code for the child, which a passing test never observes.
 */
static I32 _child_sleep(void) {
#ifdef OS_WINDOWS
    Sleep(10000);
#else
    struct timespec const requested = { .tv_sec = 10, .tv_nsec = 0 };

    nanosleep(&requested, nullptr);
#endif // OS_WINDOWS

    return 0;
}

/**
 * @brief Leave a background process holding stdout open, then exit immediately.
 *
 * This is the shape wl-copy takes to own a Wayland selection, and it is the case that breaks
 * a reader which waits for stdout to reach end of file: the grandchild inherits the pipe and
 * holds it for ten seconds after the direct child is already gone.
 *
 * @param argv Argument vector as main received it; argv[0] re-spawns this binary on Windows.
 * @return Exit code for the child.
 */
static I32 _child_daemon(char **argv) {
#ifdef OS_WINDOWS
    char command[1024] = DEFAULT_INITIALIZATION;

    snprintf(command, sizeof(command), "\"%s\" --child-sleep", argv[0]);

    STARTUPINFOA startup = DEFAULT_INITIALIZATION;

    startup.cb = sizeof(STARTUPINFOA);

    PROCESS_INFORMATION info = DEFAULT_INITIALIZATION;

    /* Inheriting handles is the whole point: the grandchild must hold this process's stdout. */
    if (CreateProcessA(nullptr, command, nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &info) != 0) {
        CloseHandle(info.hThread);
        CloseHandle(info.hProcess);
    }
#else
    /* Only the Windows branch needs the path back to this binary; fork already has it. */
    (void) argv;

    if (fork() == 0) {
        struct timespec const requested = { .tv_sec = 10, .tv_nsec = 0 };

        nanosleep(&requested, nullptr);

        _exit(0);
    }
#endif // OS_WINDOWS

    return 0;
}

/**
 * @brief Close stdout, then stay alive well past any test's patience.
 *
 * Distinct from --child-daemon, whose direct child exits at once: here the DIRECT child
 * lingers with its stdout already at end of file, so the reader sees the pipe close while the
 * process it is waiting on is still running. That separation - end of file is not the same
 * event as the child exiting - is what the bounded wait exists to handle.
 *
 * @return Exit code for the child, which a passing test never observes.
 */
static I32 _child_mute(void) {
#ifdef OS_WINDOWS
    CloseHandle(GetStdHandle(STD_OUTPUT_HANDLE));
    Sleep(10000);
#else
    close(STDOUT_FILENO);

    struct timespec const requested = { .tv_sec = 10, .tv_nsec = 0 };

    nanosleep(&requested, nullptr);
#endif // OS_WINDOWS

    return 7;
}

/**
 * @brief Write one line to stdout and one to stderr, so the stderr modes can be told apart.
 * @return Exit code for the child.
 */
static I32 _child_stderr(void) {
    fputs("to-out\n", stdout);
    fflush(stdout);
    fputs("to-err\n", stderr);
    fflush(stderr);

    return 0;
}

/**
 * @brief Abort, so the parent can classify a signal death (or the UCRT's fast-fail status).
 * @return Never returns; the value satisfies the signature.
 */
static I32 _child_abort(void) {
    abort();

    return 0;
}

#ifndef OS_WINDOWS
/**
 * @brief Report whether this child started with SIGPIPE at its default action and SIGTERM
 *        unblocked - the spawn must reset both whatever the parent had.
 * @return 0 when clean; 3 when SIGPIPE was inherited ignored; 4 when SIGTERM was inherited
 *         blocked.
 */
static I32 _child_signal_state(void) {
    if (signal(SIGPIPE, SIG_DFL) != SIG_DFL) {
        return 3;
    }

    sigset_t blocked = DEFAULT_INITIALIZATION;

    sigprocmask(SIG_BLOCK, nullptr, &blocked);

    return sigismember(&blocked, SIGTERM) ? 4 : 0;
}
#endif // OS_WINDOWS

/**
 * @brief Write forever, so the stdout pipe is never empty and the parent's deadline check must
 *        run even while there is always something to drain.
 * @return Exit code for the child, which a passing test never observes.
 */
static I32 _child_flood(void) {
#ifdef OS_WINDOWS
    _setmode(_fileno(stdout), _O_BINARY);
#endif // OS_WINDOWS

    char block[4096] = DEFAULT_INITIALIZATION;

    memory_set(block, sizeof(block), (U8) 'f');

    while (fwrite(block, 1, sizeof(block), stdout) == sizeof(block)) {
        fflush(stdout);
    }

    return 0;
}

#ifndef OS_WINDOWS
/**
 * @brief Report whether the descriptor named in argv[2] is open in this child.
 * @param argv Argument vector as main received it.
 * @return 0 when the descriptor is closed (nothing above stderr crossed), 5 when it is open.
 */
static I32 _child_descriptor_open(char **argv) {
    I32 const descriptor = (I32) strtol(argv[2], nullptr, 10);

    return fcntl(descriptor, F_GETFD) < 0 ? 0 : 5;
}
#endif // OS_WINDOWS

/**
 * @brief Leave a flooding grandchild holding stdout, then abort - the death the deadline path
 *        must not mistake for its own kill.
 *
 * @param argv Argument vector as main received it; argv[0] re-spawns this binary on Windows.
 * @return Never returns; the value satisfies the signature.
 */
static I32 _child_crash_flooding(char **argv) {
#ifdef OS_WINDOWS
    char command[1024] = DEFAULT_INITIALIZATION;

    snprintf(command, sizeof(command), "\"%s\" --child-flood", argv[0]);

    STARTUPINFOA startup = DEFAULT_INITIALIZATION;

    startup.cb = sizeof(STARTUPINFOA);

    PROCESS_INFORMATION info = DEFAULT_INITIALIZATION;

    if (CreateProcessA(nullptr, command, nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &info) != 0) {
        CloseHandle(info.hThread);
        CloseHandle(info.hProcess);
    }

    /* Die with the pipe already FULL: the run must be mid-flood when the child goes, so that
     * the deadline (or the limit) it leaves behind is what the classification has to see past. */
    Sleep(100);

    /* Without this the UCRT hands the abort to Windows Error Reporting first, which can hold the
     * process well past a short deadline; the death must be immediate for the case to mean
     * anything. The abort then exits 3, which process_outcome_aborted accepts. */
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#else
    (void) argv;

    if (fork() == 0) {
        _child_flood();

        _exit(0);
    }

    /* Die with the pipe already FULL: the run must be mid-flood when the child goes, so that
     * the deadline (or the limit) it leaves behind is what the classification has to see past. */
    struct timespec const head_start = { .tv_sec = 0, .tv_nsec = 100000000L };

    nanosleep(&head_start, nullptr);

    /* SIGTERM rather than abort(): SIGABRT's default action dumps core, and a piped core_pattern
     * (systemd-coredump, apport) ignores RLIMIT_CORE and can hold the dying child for hundreds of
     * milliseconds - long enough for the flood to hit the limit first. A plain terminating signal
     * is immediate, and the classification under test is the same: a death the module did not
     * cause. */
    kill(getpid(), SIGTERM);
#endif // OS_WINDOWS

    abort();

    return 0;
}

/**
 * @brief Wait for the direct child to have been collected, then flood stdout forever.
 *
 * The delay is what makes the survivor case deterministic: without it the flood fills the
 * capture to its limit in a couple of milliseconds - faster than a process can exit - so the
 * limit, not the collection, would end every run.
 *
 * @return Exit code for the child, which a passing test never observes.
 */
static I32 _child_flood_delayed(void) {
#ifdef OS_WINDOWS
    Sleep(200);
#else
    struct timespec const delay = { .tv_sec = 0, .tv_nsec = 200000000L };

    nanosleep(&delay, nullptr);
#endif // OS_WINDOWS

    return _child_flood();
}

/**
 * @brief Fork a flooding grandchild and exit AT ONCE, leaving it holding stdout.
 *
 * The survivor shape without the head start _child_crash_flooding gives: the direct child is
 * collectable almost immediately, so what follows in the pipe is the grandchild's alone.
 *
 * @param argv Argument vector as main received it; argv[0] re-spawns this binary on Windows.
 * @return Exit code for the child.
 */
static I32 _child_fork_and_die(char **argv) {
#ifdef OS_WINDOWS
    char command[1024] = DEFAULT_INITIALIZATION;

    snprintf(command, sizeof(command), "\"%s\" --child-flood-delayed", argv[0]);

    STARTUPINFOA startup = DEFAULT_INITIALIZATION;

    startup.cb = sizeof(STARTUPINFOA);

    PROCESS_INFORMATION info = DEFAULT_INITIALIZATION;

    if (CreateProcessA(nullptr, command, nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &info) != 0) {
        CloseHandle(info.hThread);
        CloseHandle(info.hProcess);
    }
#else
    (void) argv;

    if (fork() == 0) {
        _child_flood_delayed();

        _exit(0);
    }
#endif // OS_WINDOWS

    return 0;
}

/**
 * @brief Read nothing and exit at once, so the parent's stdin write meets a closed reader.
 * @return Exit code for the child.
 */
static I32 _child_deaf(void) {
    return 0;
}

/*==============================================================================
 * MARK: - Cases
 *============================================================================*/

/* 1. Round-trip: what goes in on stdin comes back out on stdout. */
static void _test_round_trip(Test *const test) {
    test_case_begin(test, "round trip: stdin reaches the child and its stdout is captured");

    char const *const argv[] = { _program, "--child-echo", nullptr };
    char const *const text = "hello, world";
    ProcessSpec const spec = {
        .argv = argv,
        .input = text,
        .input_size = strlen(text),
        .timeout_milliseconds = 10000,
    };

    ProcessOutcome outcome = DEFAULT_INITIALIZATION;
    Result const result = process_run(spec, &outcome);

    test_expect_true(test, "process_run succeeds", result_is_success(result));
    test_expect_i(test, "child exits 0", 0, outcome.exit_code);
    test_expect_false(test, "child did not time out", outcome.timed_out);
    test_expect_not_null(test, "output was captured", (void*) outcome.output);
    test_expect_u(test, "output size matches the input", strlen(text), outcome.output_size);

    if (!memory_empty(outcome.output)) {
        test_expect_string(test, "output matches the input", text, outcome.output);
    }

    process_outcome_uninit(&outcome);

    test_expect_null(test, "uninit released the output", (void*) outcome.output);

    test_case_end(test);
}

/* 2. The deadlock case: a payload far larger than any pipe buffer, echoed back in full.
 *    Writing all input before reading any output would wedge here permanently. */
static void _test_large_payload(Test *const test) {
    test_case_begin(test, "large payload: 256 KiB round-trips without deadlocking");

    char *const payload = (char*) memory_try_alloc(_TEST_LARGE_SIZE + 1);

    if (memory_empty(payload)) {
        test_expect_true(test, "payload allocation succeeds", false);
        test_case_end(test);

        return;
    }

    for (USize i = 0; i < _TEST_LARGE_SIZE; i += 1) {
        /* A repeating printable pattern rather than a constant byte: a truncation that
         * happened to land on a chunk boundary would still change the tail. */
        payload[i] = (char) ('a' + (i % 26));
    }

    payload[_TEST_LARGE_SIZE] = '\0';

    char const *const argv[] = { _program, "--child-echo", nullptr };
    ProcessSpec const spec = {
        .argv = argv,
        .input = payload,
        .input_size = _TEST_LARGE_SIZE,
        .timeout_milliseconds = 30000,
    };

    ProcessOutcome outcome = DEFAULT_INITIALIZATION;
    Result const result = process_run(spec, &outcome);

    test_expect_true(test, "process_run succeeds", result_is_success(result));
    test_expect_false(test, "child did not time out", outcome.timed_out);
    test_expect_u(test, "every byte came back", (USize) _TEST_LARGE_SIZE, outcome.output_size);

    if (!memory_empty(outcome.output) && outcome.output_size == _TEST_LARGE_SIZE) {
        test_expect_true(test, "the bytes are unchanged", memcmp(payload, outcome.output, _TEST_LARGE_SIZE) == 0);
    }

    process_outcome_uninit(&outcome);
    memory_delete((void**) &payload);

    test_case_end(test);
}

/* 3. A non-zero exit is the child's decision, not an OS error: SUCCESS Result, code reported. */
static void _test_exit_code(Test *const test) {
    test_case_begin(test, "exit code: a non-zero exit is reported without failing the call");

    char const *const argv[] = { _program, "--child-exit", "42", nullptr };
    ProcessSpec const spec = {
        .argv = argv,
        .timeout_milliseconds = 10000,
    };

    ProcessOutcome outcome = DEFAULT_INITIALIZATION;
    Result const result = process_run(spec, &outcome);

    test_expect_true(test, "process_run still succeeds", result_is_success(result));
    test_expect_i(test, "the child's exit code is surfaced", 42, outcome.exit_code);
    test_expect_false(test, "not reported as a timeout", outcome.timed_out);

    process_outcome_uninit(&outcome);

    test_case_end(test);
}

/* 4. Timeout: a child that never exits is killed, and the call returns rather than hanging. */
static void _test_timeout(Test *const test) {
    test_case_begin(test, "timeout: a hung child is killed and reported");

    char const *const argv[] = { _program, "--child-sleep", nullptr };
    ProcessSpec const spec = {
        .argv = argv,
        .timeout_milliseconds = 300,
    };

    ProcessOutcome outcome = DEFAULT_INITIALIZATION;
    Result const result = process_run(spec, &outcome);

    test_expect_true(test, "process_run reports no OS error", result_is_success(result));
    test_expect_true(test, "the timeout is flagged", outcome.timed_out);
    test_expect_i(test, "a killed child has no exit code", -1, outcome.exit_code);

    process_outcome_uninit(&outcome);

    test_case_end(test);
}

/* 5. A program that does not exist fails the call rather than reporting a phantom success. */
static void _test_missing_program(Test *const test) {
    test_case_begin(test, "missing program: the spawn fails instead of reporting success");

    char const *const argv[] = { "cfw_no_such_program_9f3a", nullptr };
    ProcessSpec const spec = {
        .argv = argv,
        .timeout_milliseconds = 10000,
    };

    ProcessOutcome outcome = DEFAULT_INITIALIZATION;
    Result const result = process_run(spec, &outcome);

    test_expect_true(test, "process_run reports an error", result_is_error(result));
    test_expect_null(test, "nothing was captured", (void*) outcome.output);

    /* The outcome must still be safe to release: a failed spawn is exactly when a caller
     * is most likely to hit the cleanup path. */
    process_outcome_uninit(&outcome);

    test_case_end(test);
}

/* 6. The security contract: argv is a vector, not a command line. Shell metacharacters,
 *    quotes and backslashes must arrive in the child's argv byte-for-byte as sent. */
static void _test_arguments_are_literal(Test *const test) {
    test_case_begin(test, "arguments: metacharacters and quotes survive as literal argv entries");

    char const *const dangerous = "; rm -rf / && echo pwned";
    char const *const quoted = "a\"b\\c d";
    char const *const argv[] = { _program, "--child-args", dangerous, quoted, "", "tail", nullptr };
    ProcessSpec const spec = {
        .argv = argv,
        .timeout_milliseconds = 10000,
    };

    ProcessOutcome outcome = DEFAULT_INITIALIZATION;
    Result const result = process_run(spec, &outcome);

    test_expect_true(test, "process_run succeeds", result_is_success(result));
    test_expect_i(test, "child exits 0", 0, outcome.exit_code);

    if (!memory_empty(outcome.output)) {
        /* One line per argument, so an argument that got split by a shell would show up as
         * extra lines and an argument that got eaten would show up as missing ones. */
        char const *const expected = "; rm -rf / && echo pwned\na\"b\\c d\n\ntail\n";

        test_expect_string(test, "every argument arrived intact and unsplit", expected, outcome.output);
    }
    else {
        test_expect_true(test, "the child produced output", false);
    }

    process_outcome_uninit(&outcome);

    test_case_end(test);
}

/* 7. No input means an immediate end of file, not a child left waiting on stdin forever. */
static void _test_no_input(Test *const test) {
    test_case_begin(test, "no input: the child sees end of file rather than hanging");

    char const *const argv[] = { _program, "--child-echo", nullptr };
    ProcessSpec const spec = {
        .argv = argv,
        .timeout_milliseconds = 10000,
    };

    ProcessOutcome outcome = DEFAULT_INITIALIZATION;
    Result const result = process_run(spec, &outcome);

    test_expect_true(test, "process_run succeeds", result_is_success(result));
    test_expect_false(test, "the child did not time out", outcome.timed_out);
    test_expect_i(test, "child exits 0", 0, outcome.exit_code);
    test_expect_u(test, "nothing was captured", 0, outcome.output_size);

    process_outcome_uninit(&outcome);

    test_case_end(test);
}

/* 8. The output limit is enforced: a child that outruns it fails the call rather than
 *    growing the buffer until the process dies. */
static void _test_output_limit(Test *const test) {
    test_case_begin(test, "output limit: an oversized child output fails the call");

    char const *const text = "0123456789abcdefghij";
    char const *const argv[] = { _program, "--child-echo", nullptr };
    ProcessSpec const spec = {
        .argv = argv,
        .input = text,
        .input_size = strlen(text),
        .output_limit = 4,
        .timeout_milliseconds = 10000,
    };

    ProcessOutcome outcome = DEFAULT_INITIALIZATION;
    Result const result = process_run(spec, &outcome);

    test_expect_true(test, "the call fails once the cap is passed", result_is_error(result));
    test_expect_true(test, "no more than the cap was retained", outcome.output_size <= 4);

    process_outcome_uninit(&outcome);

    test_case_end(test);
}

/* 8b. The limit must BOUND the call, not unbound it. The original suite only fed 20 bytes,
 *     which fits a pipe buffer, so the child always exited on its own and the case could not
 *     see the real defect: once the parent stops reading, a child still writing blocks on the
 *     full pipe, and waiting for it instead of killing it hung until an external watchdog
 *     stepped in. The payload here is far larger than any pipe buffer, so the child is
 *     guaranteed to still be writing when the cap trips. */
static void _test_output_limit_kills_writer(Test *const test) {
    test_case_begin(test, "output limit: a child still writing when the cap trips is killed, not waited on");

    char *const payload = (char*) memory_try_alloc(_TEST_LARGE_SIZE + 1);

    if (memory_empty(payload)) {
        test_expect_true(test, "payload allocation succeeds", false);
        test_case_end(test);

        return;
    }

    memory_set(payload, _TEST_LARGE_SIZE + 1, (U8) 'z');

    payload[_TEST_LARGE_SIZE] = '\0';

    char const *const argv[] = { _program, "--child-echo", nullptr };
    ProcessSpec const spec = {
        .argv = argv,
        .input = payload,
        .input_size = _TEST_LARGE_SIZE,
        .output_limit = 64,
        .timeout_milliseconds = 4000,
    };

    ProcessOutcome outcome = DEFAULT_INITIALIZATION;
    U64 const started = _test_now_milliseconds();
    Result const result = process_run(spec, &outcome);
    U64 const elapsed = _test_now_milliseconds() - started;

    test_expect_true(test, "the call fails once the cap is passed", result_is_error(result));
    test_expect_true(test, "no more than the cap was retained", outcome.output_size <= 64);

    /* The assertion that matters: it must come back promptly. Before the fix this sat until
     * the child was killed from outside, far past its own 4 s deadline. */
    test_expect_true(test, "it returned without hanging past the deadline", elapsed < 8000);

    process_outcome_uninit(&outcome);
    memory_delete((void**) &payload);

    test_case_end(test);
}

/* 8c. A DIRECT child that closes its own stdout and then lingers must not extend the call.
 *     End of file on the pipe is not the same event as the child exiting, and this is the
 *     shape that reaches the bounded wait - --child-daemon does not, because its direct child
 *     exits immediately and is simply reaped. */
static void _test_silent_lingering_child(Test *const test) {
    test_case_begin(test, "lingering child: closing stdout early does not outlast the timeout");

    char const *const argv[] = { _program, "--child-mute", nullptr };
    ProcessSpec const spec = {
        .argv = argv,
        .timeout_milliseconds = 1000,
    };

    ProcessOutcome outcome = DEFAULT_INITIALIZATION;
    U64 const started = _test_now_milliseconds();

    process_run(spec, &outcome);

    U64 const elapsed = _test_now_milliseconds() - started;

    test_expect_true(test, "returned near its own 1 s deadline, not the child's 10 s", elapsed < 5000);
    test_expect_true(test, "the timeout is flagged", outcome.timed_out);
    test_expect_i(test, "a killed child yields no trustworthy status", -1, outcome.exit_code);

    process_outcome_uninit(&outcome);

    test_case_end(test);
}

/* 9. process_exists: the probe agrees with what a spawn can actually resolve. */
static void _test_exists(Test *const test) {
    test_case_begin(test, "exists: a real program is found and a fictional one is not");

    test_expect_true(test, "this binary's own path resolves", process_exists(_program));
    test_expect_false(test, "a fictional program does not", process_exists("cfw_no_such_program_9f3a"));
    test_expect_false(test, "an empty name does not", process_exists(""));

    test_case_end(test);
}

/* 10. A child that leaves a background process holding stdout must not stall the run.
 *     Waiting for the pipe to reach end of file instead of for the child to exit made every
 *     wl-copy call sit until its timeout, so this pins the exit condition. */
static void _test_daemonizing_child(Test *const test) {
    test_case_begin(test, "daemonizing child: the run ends on child exit, not on pipe close");

    char const *const argv[] = { _program, "--child-daemon", nullptr };
    ProcessSpec const spec = {
        .argv = argv,
        .timeout_milliseconds = 5000,
    };

    ProcessOutcome outcome = DEFAULT_INITIALIZATION;
    Result const result = process_run(spec, &outcome);

    test_expect_true(test, "process_run succeeds", result_is_success(result));
    test_expect_false(test, "it did not need the timeout to finish", outcome.timed_out);
    test_expect_i(test, "the direct child's exit code is reported", 0, outcome.exit_code);

    process_outcome_uninit(&outcome);

    test_case_end(test);
}

/* 19. The deadline is honoured while the pipe is never empty - a child that floods stdout used
 *     to keep the Windows loop reading and peeking past its own timeout, until output_limit. */
static void _test_timeout_under_output_pressure(Test *const test) {
    test_case_begin(test, "timeout under output pressure: a child that floods stdout is killed at the deadline, not at the output limit");

    /* The limit sits far above what a fast pipe moves in 100 ms (Linux drains ~16 MiB in 50 ms),
     * so the deadline is the first thing that can end this run - which is the point. */
    char const *const argv[] = { _program, "--child-flood", nullptr };
    ProcessSpec const spec = { .argv = argv, .output_limit = 64U * 1024U * 1024U, .timeout_milliseconds = 100 };
    ProcessOutcome outcome = DEFAULT_INITIALIZATION;
    U64 const started = _test_now_milliseconds();
    Result const result = process_run(spec, &outcome);
    U64 const elapsed = _test_now_milliseconds() - started;

    test_expect_true(test, "process_run succeeds (the deadline is the caller's ask)", result_is_success(result));
    test_expect_i(test, "ended == TIMED_OUT, not KILLED at the output limit", (I32) PROCESS_ENDED_TIMED_OUT, (I32) outcome.ended);
    test_expect_true(test, "and promptly - not after 64 MiB of flood", elapsed < 1000);
    test_expect_true(test, "the capture holds what arrived before the deadline", outcome.output_size > 0);

    process_outcome_uninit(&outcome);

    test_case_end(test);
}

/* 20. process_outcome_aborted is the death-test predicate, in one call on both platforms. */
static void _test_aborted_predicate(Test *const test) {
    test_case_begin(test, "aborted(): true for an abort(), false for a clean exit and for a non-zero exit");

    char const *const argv_abort[] = { _program, "--child-abort", nullptr };
    ProcessSpec const spec_abort = { .argv = argv_abort, .stderr_mode = PROCESS_STDERR_DISCARD, .timeout_milliseconds = 5000 };
    ProcessOutcome outcome = DEFAULT_INITIALIZATION;

    process_run(spec_abort, &outcome);

    test_expect_true(test, "an abort() is aborted", process_outcome_aborted(&outcome));
    test_expect_false(test, "and not success", process_outcome_succeeded(&outcome));

    process_outcome_uninit(&outcome);

    char const *const argv_exit[] = { _program, "--child-exit", "1", nullptr };
    ProcessSpec const spec_exit = { .argv = argv_exit, .timeout_milliseconds = 5000 };

    process_run(spec_exit, &outcome);

    test_expect_false(test, "a plain exit(1) is not aborted", process_outcome_aborted(&outcome));

    process_outcome_uninit(&outcome);

    test_expect_false(test, "a released outcome is not aborted", process_outcome_aborted(&outcome));

    test_case_end(test);
}

/* 21. A directory is not a program: the probe says no rather than "found" before an EACCES. */
static void _test_exists_refuses_directory(Test *const test) {
    test_case_begin(test, "exists: a directory on the path is not a program");

#ifdef OS_WINDOWS
    test_expect_false(test, "C:\\Windows is a directory, not a program", process_exists("C:\\Windows"));

    char const *const argv[] = { "C:\\Windows", nullptr };
    ProcessSpec const spec = { .argv = argv, .timeout_milliseconds = 5000 };
    ProcessOutcome outcome = DEFAULT_INITIALIZATION;
    Result const result = process_run(spec, &outcome);

    test_expect_true(test, "process_run refuses it under the shared macro, never spawning", result == PROCESS_RESULT_NOT_EXECUTABLE);

    process_outcome_uninit(&outcome);
#else
    test_expect_false(test, "/ is a directory, not a program", process_exists("/"));
    test_expect_false(test, "/tmp is a directory, not a program", process_exists("/tmp"));

    char const *const argv_directory[] = { "/tmp", nullptr };
    ProcessSpec const spec_directory = { .argv = argv_directory, .timeout_milliseconds = 5000 };
    ProcessOutcome outcome = DEFAULT_INITIALIZATION;
    Result const refused = process_run(spec_directory, &outcome);

    test_expect_true(test, "process_run refuses a directory under the same macro as Windows", refused == PROCESS_RESULT_NOT_EXECUTABLE);

    process_outcome_uninit(&outcome);

    char const *const argv_missing[] = { "/cfw/no/such/program", nullptr };
    ProcessSpec const spec_missing = { .argv = argv_missing, .timeout_milliseconds = 5000 };
    Result const missing = process_run(spec_missing, &outcome);

    test_expect_true(test, "and ENOENT for a path with nothing there", missing == result_from_os_code((U32) ENOENT));

    process_outcome_uninit(&outcome);
#endif // OS_WINDOWS

    test_case_end(test);
}

/* 22. POSIX: the spawn resolves through the SAME walker as the probe. With PATH holding only an
 *     empty entry (= the working directory), posix_spawnp would have run ./cfw_process_probe.sh;
 *     process_run must refuse it exactly as process_exists does. */
static void _test_spawn_refuses_working_directory(Test *const test) {
    test_case_begin(test, "resolution (POSIX): an empty PATH entry never resolves a bare name out of the working directory - for the spawn as for the probe");

#ifdef OS_WINDOWS
    test_expect_true(test, "not applicable on Windows (SearchPath semantics are documented instead)", true);
#else
    char const *const name = "cfw_process_probe.sh";
    FILE *const file = fopen(name, "wb");

    if (file == nullptr) {
        test_expect_true(test, "probe script created", false);
        test_case_end(test);

        return;
    }

    fputs("#!/bin/sh\necho reached\n", file);
    fclose(file);
    chmod(name, 0700);

    /* Copied whole before setenv can move it: a fixed buffer would silently truncate a long PATH
     * on the restore, for every case that follows. */
    char const *const previous_path = getenv("PATH");
    char *saved_path = nullptr;

    if (previous_path != nullptr) {
        USize const size = strlen(previous_path) + 1;

        saved_path = (char*) memory_try_alloc(size);

        if (memory_empty(saved_path)) {
            test_expect_true(test, "PATH copy allocated", false);
            remove(name);
            test_case_end(test);

            return;
        }

        memory_copy_1(saved_path, previous_path, size);
    }

    setenv("PATH", ":", 1);

    char const *const argv[] = { name, nullptr };
    ProcessSpec const spec = { .argv = argv, .timeout_milliseconds = 5000 };
    ProcessOutcome outcome = DEFAULT_INITIALIZATION;
    Result const result = process_run(spec, &outcome);
    bool const probe = process_exists(name);

    if (saved_path != nullptr) {
        setenv("PATH", saved_path, 1);
        memory_delete((void**) &saved_path);
    }
    else {
        unsetenv("PATH");
    }

    test_expect_true(test, "the spawn is refused (it used to run ./cfw_process_probe.sh through posix_spawnp)", result_is_error(result));
    test_expect_true(test, "nothing ran: no output", outcome.output == nullptr || outcome.output_size == 0);
    test_expect_false(test, "the probe agrees", probe);

    process_outcome_uninit(&outcome);
    remove(name);
#endif // OS_WINDOWS

    test_case_end(test);
}

/* 23. POSIX (glibc 2.34+): a descriptor the parent opened WITHOUT close-on-exec does not reach
 *     the child - nothing above stderr crosses. */
static void _test_descriptor_isolation(Test *const test) {
    test_case_begin(test, "isolation (POSIX, glibc 2.34+): a parent descriptor opened without FD_CLOEXEC is closed in the child");

#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34))
    I32 leaky[2] = { -1, -1 };

    if (pipe(leaky) != 0) {
        test_expect_true(test, "leaky pipe created", false);
        test_case_end(test);

        return;
    }

    char number[16] = DEFAULT_INITIALIZATION;

    snprintf(number, sizeof(number), "%d", leaky[1]);

    char const *const argv[] = { _program, "--child-descriptor-open", number, nullptr };
    ProcessSpec const spec = { .argv = argv, .timeout_milliseconds = 5000 };
    ProcessOutcome outcome = DEFAULT_INITIALIZATION;
    Result const result = process_run(spec, &outcome);

    test_expect_true(test, "process_run succeeds", result_is_success(result));
    test_expect_i(test, "the child found the descriptor CLOSED (0; 5 would mean it crossed)", 0, outcome.exit_code);

    process_outcome_uninit(&outcome);
    close(leaky[0]);
    close(leaky[1]);
#else
    test_expect_true(test, "not applicable here (documented: such descriptors reach the child)", true);
#endif // glibc >= 2.34

    test_case_end(test);
}

/* 24. A child that dies of its OWN signal while a grandchild keeps the pipe full is reported as
 *     that death - the deadline (or the output limit) it left behind is not a kill this module
 *     sent. Windows has no signals: the abort is EXITED with the fast-fail status. */
static void _test_own_death_not_mistaken_for_kill(Test *const test) {
    test_case_begin(test, "own death: a child that dies of its own signal while its grandchild floods stdout is reported as that death, never TIMED_OUT or KILLED");

    char const *const argv[] = { _program, "--child-crash-flooding", nullptr };
    ProcessSpec const spec = { .argv = argv, .stderr_mode = PROCESS_STDERR_DISCARD, .output_limit = 64U * 1024U * 1024U, .timeout_milliseconds = 500 };
    ProcessOutcome outcome = DEFAULT_INITIALIZATION;

    process_run(spec, &outcome);

    test_expect_false(test, "not a timeout", outcome.timed_out);
#ifdef OS_WINDOWS
    test_expect_true(test, "the child's own abort is what is reported", process_outcome_aborted(&outcome));
    test_expect_i(test, "ended == EXITED (Windows has no signals)", (I32) PROCESS_ENDED_EXITED, (I32) outcome.ended);
#else
    test_expect_i(test, "ended == SIGNALED - its own death, not KILLED at the limit the flood then hit", (I32) PROCESS_ENDED_SIGNALED, (I32) outcome.ended);
    test_expect_i(test, "the signal is the child's own SIGTERM, not this module's SIGKILL", (I32) SIGTERM, outcome.signal);
#endif // OS_WINDOWS

    process_outcome_uninit(&outcome);

    test_case_end(test);
}

/* 25. Once the direct child is collected the run is over: a background process it forked keeps
 *     flooding a pipe nobody asked about, and that flood must neither extend the call nor be
 *     reported as the child's output over the limit. */
static void _test_survivor_flood_is_not_the_childs_output(Test *const test) {
    test_case_begin(test, "surviving grandchild: the run ends when the direct child is collected - the survivor's flood is bounded, not captured as the child's output over the limit");

    char const *const argv[] = { _program, "--child-fork-and-die", nullptr };
    ProcessSpec const spec = {
        .argv = argv,
        .stderr_mode = PROCESS_STDERR_DISCARD,
        .output_limit = 1024U * 1024U,
        .timeout_milliseconds = 0,
    };

    ProcessOutcome outcome = DEFAULT_INITIALIZATION;
    U64 const started = _test_now_milliseconds();
    Result const result = process_run(spec, &outcome);
    U64 const elapsed = _test_now_milliseconds() - started;

    /* No deadline at all: before the fix this ran until the grandchild had pushed 1 MiB through
     * and came back OUTPUT_TOO_LARGE for a child that wrote nothing and exited 0. */
    test_expect_true(test, "the run succeeds - nothing the child did failed", result_is_success(result));
    test_expect_false(test, "not a timeout (there was no deadline to meet)", outcome.timed_out);
    test_expect_true(test, "the child's own clean exit is what is reported", process_outcome_succeeded(&outcome));
    test_expect_true(test, "the survivor's flood is not in the capture", outcome.output_size <= 64U * 1024U);
    /* Generous on purpose: the size assertion above is the proof, and this only has to stay well
     * under a flood-to-the-limit run (seconds). A tight bound flakes when a previous case's
     * detached survivor is still competing for the machine. */
    test_expect_true(test, "and it returned as soon as the child was collected, not when the flood hit the limit", elapsed < 1000);

    process_outcome_uninit(&outcome);

    test_case_end(test);
}

/* 26. An empty argv[0] is a name that does not exist, not a program that cannot be run: the PATH
 *     walk would otherwise build "dir/" candidates, which stat as the directories themselves. */
static void _test_empty_program_name(Test *const test) {
    test_case_begin(test, "empty argv[0]: reported as missing (ENOENT), never as \"there but not runnable\"");

    char const *const argv[] = { "", nullptr };
    ProcessSpec const spec = { .argv = argv, .timeout_milliseconds = 5000 };
    ProcessOutcome outcome = DEFAULT_INITIALIZATION;
    Result const result = process_run(spec, &outcome);

    test_expect_true(test, "the spawn is refused", result_is_error(result));
#ifndef OS_WINDOWS
    test_expect_true(test, "as ENOENT, not the not-executable refusal", result == result_from_os_code((U32) ENOENT));
#endif // OS_WINDOWS
    test_expect_false(test, "and process_exists agrees", process_exists(""));

    process_outcome_uninit(&outcome);

    test_case_end(test);
}

/* 27. A child that exits without reading its input is not an error - the parent's write meets a
 *     closed reader (EPIPE / ERROR_BROKEN_PIPE) and the run still succeeds. */
static void _test_child_stops_reading(Test *const test) {
    test_case_begin(test, "child stops reading: a large stdin meeting a child that exits at once is not a failure");

    char *const payload = (char*) memory_try_alloc(_TEST_LARGE_SIZE + 1);

    if (memory_empty(payload)) {
        test_expect_true(test, "payload allocation succeeds", false);
        test_case_end(test);

        return;
    }

    memory_set(payload, _TEST_LARGE_SIZE + 1, (U8) 'd');

    payload[_TEST_LARGE_SIZE] = '\0';

    char const *const argv[] = { _program, "--child-deaf", nullptr };
    ProcessSpec const spec = {
        .argv = argv,
        .input = payload,
        .input_size = _TEST_LARGE_SIZE,
        .timeout_milliseconds = 5000,
    };

    ProcessOutcome outcome = DEFAULT_INITIALIZATION;
    Result const result = process_run(spec, &outcome);

    test_expect_true(test, "process_run succeeds", result_is_success(result));
    test_expect_true(test, "the child exited cleanly", process_outcome_succeeded(&outcome));
    test_expect_false(test, "no timeout", outcome.timed_out);

    process_outcome_uninit(&outcome);
    memory_delete((void**) &payload);

    test_case_end(test);
}

/* 11. Releasing is safe on a zeroed outcome and on one already released. */
static void _test_uninit_is_safe(Test *const test) {
    test_case_begin(test, "uninit: safe on a zeroed outcome and on a repeat release");

    ProcessOutcome outcome = DEFAULT_INITIALIZATION;

    process_outcome_uninit(&outcome);

    test_expect_null(test, "still null after releasing a zeroed outcome", (void*) outcome.output);

    char const *const argv[] = { _program, "--child-echo", nullptr };
    char const *const text = "twice";
    ProcessSpec const spec = {
        .argv = argv,
        .input = text,
        .input_size = strlen(text),
        .timeout_milliseconds = 10000,
    };

    process_run(spec, &outcome);
    process_outcome_uninit(&outcome);
    process_outcome_uninit(&outcome);

    test_expect_null(test, "still null after a repeat release", (void*) outcome.output);
    test_expect_u(test, "size was reset", 0, outcome.output_size);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Entry point
 *============================================================================*/

/* 12. `ended` says how the child ended; exit_code is a status only under EXITED. */
static void _test_ended_classification(Test *const test) {
    test_case_begin(test, "ended: a normal exit is EXITED with its status, and succeeded() reads it in one call");

    char const *const argv_three[] = { _program, "--child-exit", "3", nullptr };
    ProcessSpec const spec_three = { .argv = argv_three, .timeout_milliseconds = 5000 };
    ProcessOutcome outcome = DEFAULT_INITIALIZATION;
    Result const result = process_run(spec_three, &outcome);

    test_expect_true(test, "process_run succeeds", result_is_success(result));
    test_expect_i(test, "ended == EXITED", (I32) PROCESS_ENDED_EXITED, (I32) outcome.ended);
    test_expect_i(test, "exit_code is the child's status", 3, outcome.exit_code);
    test_expect_i(test, "no signal", 0, outcome.signal);
    test_expect_false(test, "a non-zero exit is not success", process_outcome_succeeded(&outcome));
    test_expect_true(test, "a child that wrote nothing still yields an owned \"\"", outcome.output != nullptr && outcome.output_size == 0 && outcome.output[0] == '\0');

    process_outcome_uninit(&outcome);

    test_expect_i(test, "uninit resets the fate to UNKNOWN", (I32) PROCESS_ENDED_UNKNOWN, (I32) outcome.ended);
    test_expect_i(test, "and exit_code to -1, never a clean 0", -1, outcome.exit_code);
    test_expect_false(test, "a released outcome is not success", process_outcome_succeeded(&outcome));

    char const *const argv_zero[] = { _program, "--child-exit", "0", nullptr };
    ProcessSpec const spec_zero = { .argv = argv_zero, .timeout_milliseconds = 5000 };

    process_run(spec_zero, &outcome);

    test_expect_true(test, "exit 0 is success", process_outcome_succeeded(&outcome));

    process_outcome_uninit(&outcome);

    test_case_end(test);
}

/* 13. A deadline kill is TIMED_OUT, and only a deadline kill is. */
static void _test_ended_timed_out(Test *const test) {
    test_case_begin(test, "ended: a hung child killed at the deadline is TIMED_OUT, not success");

    char const *const argv[] = { _program, "--child-sleep", nullptr };
    ProcessSpec const spec = { .argv = argv, .timeout_milliseconds = 300 };
    ProcessOutcome outcome = DEFAULT_INITIALIZATION;
    Result const result = process_run(spec, &outcome);

    test_expect_true(test, "process_run succeeds (the deadline is the caller's ask)", result_is_success(result));
    test_expect_i(test, "ended == TIMED_OUT", (I32) PROCESS_ENDED_TIMED_OUT, (I32) outcome.ended);
    test_expect_true(test, "timed_out mirrors it", outcome.timed_out);
    test_expect_i(test, "no trustworthy status", -1, outcome.exit_code);
    test_expect_false(test, "not success", process_outcome_succeeded(&outcome));

    process_outcome_uninit(&outcome);

    test_case_end(test);
}

/* 14. A failed capture kills the child AT ONCE - it used to sit out the deadline and then be
 *     reported as a timeout. The 4 s deadline here must never be reached. */
static void _test_capture_failure_kills_at_once(Test *const test) {
    test_case_begin(test, "capture failure: a child over the output limit is killed immediately and reported KILLED with a PARTIAL result, never as a timeout");

    char *const payload = (char*) memory_try_alloc(_TEST_LARGE_SIZE + 1);

    if (memory_empty(payload)) {
        test_expect_true(test, "payload allocation succeeds", false);
        test_case_end(test);

        return;
    }

    memory_set(payload, _TEST_LARGE_SIZE + 1, (U8) 'z');

    payload[_TEST_LARGE_SIZE] = '\0';

    char const *const argv[] = { _program, "--child-echo", nullptr };
    ProcessSpec const spec = {
        .argv = argv,
        .input = payload,
        .input_size = _TEST_LARGE_SIZE,
        .output_limit = 64,
        .timeout_milliseconds = 4000,
    };

    ProcessOutcome outcome = DEFAULT_INITIALIZATION;
    U64 const started = _test_now_milliseconds();
    Result const result = process_run(spec, &outcome);
    U64 const elapsed = _test_now_milliseconds() - started;

    test_expect_true(test, "the call fails", result_is_error(result));
    test_expect_true(test, "with the public too-large code", result_clear_flag(result, RESULT_FLAG_PARTIAL) == PROCESS_RESULT_OUTPUT_TOO_LARGE);
    test_expect_true(test, "flagged PARTIAL: a prefix was retained", result_is_partial(result) && outcome.output_size > 0);
    test_expect_i(test, "ended == KILLED", (I32) PROCESS_ENDED_KILLED, (I32) outcome.ended);
    test_expect_false(test, "NOT reported as a timeout", outcome.timed_out);
    test_expect_true(test, "killed at once, far inside the 4 s deadline", elapsed < 2000);

    process_outcome_uninit(&outcome);
    memory_delete((void**) &payload);

    test_case_end(test);
}

/* 15. stderr goes where the spec says - observed at BOTH ends: the capture, and the parent's
 *     own stderr (redirected to a scratch file), so DISCARD is distinguishable from INHERIT. */
static void _test_stderr_modes(Test *const test) {
    test_case_begin(test, "stderr_mode: INHERIT reaches the parent's stderr and not the capture, MERGE folds it into the capture, DISCARD reaches neither");

    char const *const scratch = "cfw_process_stderr_probe.txt";
    char const *const argv[] = { _program, "--child-stderr", nullptr };
    ProcessStderr const modes[] = { PROCESS_STDERR_INHERIT, PROCESS_STDERR_MERGE, PROCESS_STDERR_DISCARD };
    char const *const names[] = { "INHERIT", "MERGE", "DISCARD" };

    for (USize i = 0; i < 3; i += 1) {
#ifdef OS_WINDOWS
        HANDLE saved = nullptr;
#else
        I32 saved = -1;
#endif // OS_WINDOWS
        bool const redirected = _stderr_to_file_begin(scratch, &saved);
        ProcessSpec const spec = { .argv = argv, .stderr_mode = modes[i], .timeout_milliseconds = 5000 };
        ProcessOutcome outcome = DEFAULT_INITIALIZATION;
        Result const result = process_run(spec, &outcome);

        if (redirected) {
            _stderr_to_file_end(saved);
        }

        char parent_stderr[256] = DEFAULT_INITIALIZATION;

        _read_small_file(scratch, parent_stderr, sizeof(parent_stderr));

        bool const has_out = outcome.output != nullptr && strstr(outcome.output, "to-out") != nullptr;
        bool const captured_err = outcome.output != nullptr && strstr(outcome.output, "to-err") != nullptr;
        bool const parent_err = strstr(parent_stderr, "to-err") != nullptr;
        char label[128] = DEFAULT_INITIALIZATION;

        snprintf(label, sizeof label, "%s: the run succeeds", names[i]);
        test_expect_true(test, label, result_is_success(result) && process_outcome_succeeded(&outcome));
        snprintf(label, sizeof label, "%s: stdout is captured", names[i]);
        test_expect_true(test, label, has_out);
        snprintf(label, sizeof label, "%s: stderr %s the capture", names[i], modes[i] == PROCESS_STDERR_MERGE ? "is in" : "is out of");
        test_expect_true(test, label, captured_err == (modes[i] == PROCESS_STDERR_MERGE));
        snprintf(label, sizeof label, "%s: stderr %s the parent's stderr", names[i], modes[i] == PROCESS_STDERR_INHERIT ? "reached" : "did not reach");
        test_expect_true(test, label, redirected && parent_err == (modes[i] == PROCESS_STDERR_INHERIT));

        process_outcome_uninit(&outcome);
    }

    remove(scratch);

    test_case_end(test);
}

/* 16. A death by signal is SIGNALED with the signal named - the death-test primitive the
 *     foundation suites rely on. Windows has no signals: the UCRT's abort() is an EXITED
 *     status carrying the fast-fail NTSTATUS. */
static void _test_abort_is_classified(Test *const test) {
    test_case_begin(test, "abort: the child's death is classified exactly (SIGNALED + SIGABRT on POSIX; EXITED + 0xC0000409 on Windows)");

    char const *const argv[] = { _program, "--child-abort", nullptr };
    ProcessSpec const spec = { .argv = argv, .stderr_mode = PROCESS_STDERR_DISCARD, .timeout_milliseconds = 5000 };
    ProcessOutcome outcome = DEFAULT_INITIALIZATION;
    Result const result = process_run(spec, &outcome);

    test_expect_true(test, "process_run succeeds", result_is_success(result));
    test_expect_false(test, "an abort is not success", process_outcome_succeeded(&outcome));
    test_expect_true(test, "and process_outcome_aborted says so", process_outcome_aborted(&outcome));
#ifdef OS_WINDOWS
    test_expect_i(test, "ended == EXITED (no signals on Windows)", (I32) PROCESS_ENDED_EXITED, (I32) outcome.ended);
    test_expect_i(test, "exit_code carries the fast-fail NTSTATUS", (I32) 0xC0000409u, outcome.exit_code);
    test_expect_i(test, "signal stays 0", 0, outcome.signal);
#else
    test_expect_i(test, "ended == SIGNALED", (I32) PROCESS_ENDED_SIGNALED, (I32) outcome.ended);
    test_expect_i(test, "the signal is SIGABRT", (I32) SIGABRT, outcome.signal);
    test_expect_i(test, "exit_code is -1", -1, outcome.exit_code);
#endif // OS_WINDOWS

    process_outcome_uninit(&outcome);

    test_case_end(test);
}

/* 17. Windows: a batch file is refused, because it would run through cmd.exe. */
static void _test_batch_refused(Test *const test) {
    test_case_begin(test, "batch (Windows): a .bat target is REFUSED by process_run and reported absent by process_exists");

#ifdef OS_WINDOWS
    char const *const name = "cfw_process_probe.bat";
    FILE *const file = fopen(name, "wb");

    if (file == nullptr) {
        test_expect_true(test, "probe file created", false);
        test_case_end(test);

        return;
    }

    fputs("@echo off\r\necho reached\r\n", file);
    fclose(file);

    char const *const argv[] = { name, "a&b", nullptr };
    ProcessSpec const spec = { .argv = argv, .timeout_milliseconds = 5000 };
    ProcessOutcome outcome = DEFAULT_INITIALIZATION;
    Result const result = process_run(spec, &outcome);

    test_expect_true(test, "refused with PROCESS_RESULT_BATCH_REFUSED", result == PROCESS_RESULT_BATCH_REFUSED);
    test_expect_true(test, "nothing ran: no output", outcome.output == nullptr || outcome.output_size == 0);
    test_expect_i(test, "fate UNKNOWN", (I32) PROCESS_ENDED_UNKNOWN, (I32) outcome.ended);
    test_expect_false(test, "process_exists agrees", process_exists(name));

    process_outcome_uninit(&outcome);
    remove(name);
#else
    test_expect_true(test, "not applicable on POSIX (no batch semantics)", true);
#endif // OS_WINDOWS

    test_case_end(test);
}

/* 18. POSIX: the child starts with a clean signal state, whatever the parent had. */
static void _test_signal_defaults(Test *const test) {
    test_case_begin(test, "signals (POSIX): an ignored SIGPIPE and a blocked SIGTERM in the parent do not reach the child");

#ifdef OS_WINDOWS
    test_expect_true(test, "not applicable on Windows", true);
#else
    void (*const previous_pipe)(int) = signal(SIGPIPE, SIG_IGN);
    sigset_t block = DEFAULT_INITIALIZATION;
    sigset_t saved = DEFAULT_INITIALIZATION;

    sigemptyset(&block);
    sigaddset(&block, SIGTERM);
    sigprocmask(SIG_BLOCK, &block, &saved);

    char const *const argv[] = { _program, "--child-signal-state", nullptr };
    ProcessSpec const spec = { .argv = argv, .timeout_milliseconds = 5000 };
    ProcessOutcome outcome = DEFAULT_INITIALIZATION;
    Result const result = process_run(spec, &outcome);

    sigprocmask(SIG_SETMASK, &saved, nullptr);
    signal(SIGPIPE, previous_pipe);

    test_expect_true(test, "process_run succeeds", result_is_success(result));
    test_expect_i(test, "the child saw SIGPIPE at default and SIGTERM unblocked (0)", 0, outcome.exit_code);

    process_outcome_uninit(&outcome);
#endif // OS_WINDOWS

    test_case_end(test);
}

int main(int argc, char **argv) {
    /* Child modes come first and never initialize the test harness: this process was spawned
     * by a running case and must behave as the small helper that case asked for. */
    if (argc >= 2) {
        if (strcmp(argv[1], "--child-echo") == 0) {
            return _child_echo();
        }

        if (strcmp(argv[1], "--child-args") == 0) {
            return _child_arguments(argc, argv);
        }

        if (strcmp(argv[1], "--child-sleep") == 0) {
            return _child_sleep();
        }

        if (strcmp(argv[1], "--child-daemon") == 0) {
            return _child_daemon(argv);
        }

        if (strcmp(argv[1], "--child-mute") == 0) {
            return _child_mute();
        }

        if (strcmp(argv[1], "--child-stderr") == 0) {
            return _child_stderr();
        }

        if (strcmp(argv[1], "--child-abort") == 0) {
            return _child_abort();
        }

        if (strcmp(argv[1], "--child-flood") == 0) {
            return _child_flood();
        }

        if (strcmp(argv[1], "--child-flood-delayed") == 0) {
            return _child_flood_delayed();
        }

        if (strcmp(argv[1], "--child-crash-flooding") == 0) {
            return _child_crash_flooding(argv);
        }

        if (strcmp(argv[1], "--child-fork-and-die") == 0) {
            return _child_fork_and_die(argv);
        }

        if (strcmp(argv[1], "--child-deaf") == 0) {
            return _child_deaf();
        }

#ifndef OS_WINDOWS
        if (strcmp(argv[1], "--child-signal-state") == 0) {
            return _child_signal_state();
        }

        if (strcmp(argv[1], "--child-descriptor-open") == 0 && argc >= 3) {
            return _child_descriptor_open(argv);
        }
#endif // OS_WINDOWS

        if (strcmp(argv[1], "--child-exit") == 0 && argc >= 3) {
            return (I32) strtol(argv[2], nullptr, 10);
        }
    }

    LogConfig const log_config = {
        .level = LOG_LEVEL_ERROR,
        .stream = LOG_STREAM_STDOUT,
        .timestamp_enabled = true,
        .autoflush = true,
    };

    log_init(log_config);

    _program = argv[0];

    Test test = test_init("./process.test.c");

    test_suite_begin(&test, "process");

    _test_round_trip(&test);
    _test_large_payload(&test);
    _test_exit_code(&test);
    _test_timeout(&test);
    _test_missing_program(&test);
    _test_arguments_are_literal(&test);
    _test_no_input(&test);
    _test_output_limit(&test);
    _test_output_limit_kills_writer(&test);
    _test_silent_lingering_child(&test);
    _test_exists(&test);
    _test_daemonizing_child(&test);
    _test_uninit_is_safe(&test);
    _test_ended_classification(&test);
    _test_ended_timed_out(&test);
    _test_capture_failure_kills_at_once(&test);
    _test_stderr_modes(&test);
    _test_abort_is_classified(&test);
    _test_batch_refused(&test);
    _test_signal_defaults(&test);
    _test_timeout_under_output_pressure(&test);
    _test_aborted_predicate(&test);
    _test_exists_refuses_directory(&test);
    _test_spawn_refuses_working_directory(&test);
    _test_descriptor_isolation(&test);
    _test_own_death_not_mistaken_for_kill(&test);
    _test_survivor_flood_is_not_the_childs_output(&test);
    _test_empty_program_name(&test);
    _test_child_stops_reading(&test);

    test_suite_end(&test);

    return test_uninit(&test);
}