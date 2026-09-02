/*
 * test_all.c - Unit tests for the logging module
 *
 * Features tested:
 *   - Basic message logging functionality
 *   - Variable logging (char, int, float, pointer, string, arrays)
 *   - Debug logging with metadata
 *   - Log level filtering (asserted via buffer capture)
 *   - Stream output redirection
 *   - Buffer-based logging
 *   - Timestamp functionality
 *   - Thread safety (basic verification)
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef OS_WINDOWS
#include <io.h>
#else
#include <unistd.h>
#endif // OS_WINDOWS

#include <log/log.h>
#include <thread/thread.h>

// === Test Constants ===
#define TEST_BUFFER_SIZE 1024

// === Test Helper Functions ===
static void _test_accessor_roundtrip(void);
static void _test_basic_logging(void);
static void _test_buffer_operations(void);
static void _test_debug_logging(void);
static void _test_disabled_stream_contract(void);
static void _test_error_conditions(void);
static void _test_log_levels(void);
#ifdef LOG_THREAD_IMPLEMENTATION
static void _test_thread_safety(void);
#endif // LOG_THREAD_IMPLEMENTATION
static void _test_timestamps(void);
static void _test_variable_logging(void);
static void _test_line_cap_and_mirror_clamp(void);
static void _test_zero_capacity_buffer(void);
static void _test_uint_dump_has_no_carriage_return(void);
#ifdef LOG_THREAD_IMPLEMENTATION
static void _test_worker_first_call_is_a_try_twin(void);
#endif // LOG_THREAD_IMPLEMENTATION

// === Test Implementation ===
static void _test_accessor_roundtrip(void) {
    printf("Testing accessor round-trip functionality...\n");

    log_set_autoflush(false);
    assert(log_get_autoflush() == false);
    log_set_autoflush(true);
    assert(log_get_autoflush() == true);

    log_set_timestamp_enabled(true);
    assert(log_is_timestamp_enabled() == true);
    log_set_timestamp_enabled(false);
    assert(log_is_timestamp_enabled() == false);

    log_set_stream(stderr);
    assert(log_get_stream() == stderr);
    log_set_stream(LOG_STREAM_STDOUT);
    assert(log_get_stream() == stdout);

    log_set_level(LOG_LEVEL_WARN);
    assert(log_get_level() == LOG_LEVEL_WARN);
    log_set_level(LOG_LEVEL_DEBUG);
    assert(log_get_level() == LOG_LEVEL_DEBUG);

    printf("Accessor round-trip tests passed.\n\n");
}

static void _test_basic_logging(void) {
    printf("Testing basic logging functionality...\n");

    LogConfig const basic_config = {
        .level             = LOG_LEVEL_DEBUG,
        .stream            = LOG_STREAM_STDOUT,
        .timestamp_enabled = false,
        .autoflush         = true
    };

    log_init(basic_config);

    log_message_1(LOG_LEVEL_INFO, "Basic message test\n");
    log_message_1(LOG_LEVEL_INFO, "Message with number: %d\n", 42);
    log_message_1(LOG_LEVEL_INFO, "Message with string: %s\n", "test_string");

    printf("Basic logging tests passed.\n\n");
}

static void _test_buffer_operations(void) {
    printf("Testing buffer operations...\n");

    char test_buffer[TEST_BUFFER_SIZE] = DEFAULT_INITIALIZATION;

    log_set_buffer(test_buffer, TEST_BUFFER_SIZE);

    log_message_1(LOG_LEVEL_INFO, "This message goes to buffer\n");
    log_message_1(LOG_LEVEL_INFO, "Buffer test with number: %d\n", 123);

    assert(strlen(test_buffer) > 0);
    printf("Buffer content length: %zu\n", strlen(test_buffer));

    log_buffer_clear();
    assert(strlen(test_buffer) == 0);
    printf("Buffer cleared successfully\n");

    char *const buffer_ptr = log_get_buffer();

    assert(buffer_ptr == test_buffer);
    printf("Buffer pointer verification passed\n");

    assert(log_get_buffer_size() == 0);
    printf("Buffer size after clear verified\n");

    log_message_1(LOG_LEVEL_INFO, "Size check\n");
    assert(log_get_buffer_size() > 0);
    printf("Buffer size after write verified\n");

    log_set_buffer(nullptr, 0);
    printf("Buffer operation tests passed.\n\n");
}

static void _test_debug_logging(void) {
    printf("Testing debug logging functionality...\n");

    log_debug_step(LOG_METADATA);
    log_debug_step(LOG_METADATA);

    char const test_array[] = "Test123";

    log_debug_variable_array_char(LOG_METADATA, "test_array", test_array, strlen(test_array), true);

    char const *const str_array[] = {"first", "second", "third"};

    log_debug_variable_array_str(LOG_METADATA, "str_array", str_array, 3, true);

    USize const uint_array[] = {1, 2, 3, 4, 5};

    log_debug_variable_array_uint(LOG_METADATA, "uint_array", uint_array, 5, 3, true);

    printf("Debug logging tests passed.\n\n");
}

/* Pins the fix: _log_write used to write to stdout via the _LOG_OUT_STREAM
 * nullptr->stdout mapping even when the stream was explicitly disabled, so
 * debug dumps leaked past an operator's silence. Also pins log_set_buffer
 * now null-terminating immediately (buffer[0] = '\0').
 *
 * Checking only the previously-active stream (a tmpfile) for growth is NOT
 * enough to catch this: once log_set_stream(nullptr) runs, the buggy code
 * maps to the real process stdout, never back to the old tmpfile - so that
 * tmpfile stays untouched whether the bug is present or fixed. Confirmed by
 * temporarily reintroducing the historical bug: a tmpfile-only check still
 * passed. This test instead redirects the process's real stdout to a second
 * tmpfile for the disabled-stream probe and asserts THAT file gained zero
 * bytes. */
static void _test_disabled_stream_contract(void) {
    printf("Testing disabled-stream contract (nullptr stream must never leak to stdout)...\n");

    FILE *const temp_stream = tmpfile();

    assert(temp_stream != nullptr);

    log_set_autoflush(true);
    log_set_level(LOG_LEVEL_DEBUG);
    log_set_buffer(nullptr, 0);
    log_set_stream(temp_stream);

    log_debug_variable_int(LOG_METADATA, "enabled_stream_probe", 1);
    fflush(temp_stream);

    long const size_enabled = ftell(temp_stream);

    assert(size_enabled > 0);

    // Redirect the process's real stdout to a tmpfile so a leak driven by
    // the nullptr->stdout mapping is actually observable.
    fflush(stdout);

#ifdef OS_WINDOWS
    I32 const stdout_fd_backup = _dup(_fileno(stdout));
#else
    I32 const stdout_fd_backup = dup(fileno(stdout));
#endif // OS_WINDOWS

    assert(stdout_fd_backup != -1);

    FILE *const stdout_capture = tmpfile();

    assert(stdout_capture != nullptr);

#ifdef OS_WINDOWS
    I32 const redirected = _dup2(_fileno(stdout_capture), _fileno(stdout));
#else
    I32 const redirected = dup2(fileno(stdout_capture), fileno(stdout));
#endif // OS_WINDOWS

    assert(redirected != -1); // _dup2 returns 0, POSIX dup2 returns the new fd; -1 fails on both

    // Seed with non-nul garbage first so the immediate "" check below is
    // non-vacuous (a pre-zeroed array would pass even without the fix).
    char disabled_buffer[TEST_BUFFER_SIZE] = DEFAULT_INITIALIZATION;

    memset(disabled_buffer, 'X', sizeof(disabled_buffer));

    log_set_stream(nullptr);
    log_set_buffer(disabled_buffer, sizeof(disabled_buffer));
    assert(strlen(disabled_buffer) == 0);

    log_debug_variable_int(LOG_METADATA, "disabled_stream_probe", 777);

    fflush(stdout);

    long const stdout_size_after = ftell(stdout_capture);

    // Restore the real stdout before any further printf in this test binary.
#ifdef OS_WINDOWS
    _dup2(stdout_fd_backup, _fileno(stdout));
    _close(stdout_fd_backup);
#else
    dup2(stdout_fd_backup, fileno(stdout));
    close(stdout_fd_backup);
#endif // OS_WINDOWS

    fclose(stdout_capture);

    long const size_after = ftell(temp_stream);

    assert(stdout_size_after == 0);
    assert(size_after == size_enabled);
    assert(strstr(disabled_buffer, "disabled_stream_probe") != nullptr);
    assert(strstr(disabled_buffer, "777") != nullptr);

    log_set_buffer(nullptr, 0);
    log_set_stream(LOG_STREAM_STDOUT);
    fclose(temp_stream);

    printf("Disabled-stream contract tests passed.\n\n");
}

static void _test_error_conditions(void) {
    printf("Testing error condition handling...\n");

    log_set_stream(nullptr);
    log_message_1(LOG_LEVEL_INFO, "This should work with nullptr stream\n");

    log_set_level(LOG_LEVEL_DEBUG);
    log_debug_variable_int(LOG_METADATA, "test_var", 42);
    log_debug_step(LOG_METADATA);

    printf("Error condition tests passed.\n\n");
}

static void _test_log_levels(void) {
    printf("Testing log level functionality...\n");

    char level_buffer[TEST_BUFFER_SIZE] = DEFAULT_INITIALIZATION;

    log_set_buffer(level_buffer, TEST_BUFFER_SIZE);

    // ERROR threshold: INFO is suppressed, ERROR passes
    log_set_level(LOG_LEVEL_ERROR);
    log_message_1(LOG_LEVEL_INFO, "suppressed at error threshold\n");
    assert(strlen(level_buffer) == 0);
    log_message_1(LOG_LEVEL_ERROR, "error passes\n");
    assert(strlen(level_buffer) > 0);
    log_buffer_clear();

    // WARN threshold: INFO is suppressed, WARN passes
    log_set_level(LOG_LEVEL_WARN);
    log_message_1(LOG_LEVEL_INFO, "suppressed at warn threshold\n");
    assert(strlen(level_buffer) == 0);
    log_message_1(LOG_LEVEL_WARN, "warn passes\n");
    assert(strlen(level_buffer) > 0);
    log_buffer_clear();

    // INFO threshold: DEBUG is suppressed, INFO passes
    log_set_level(LOG_LEVEL_INFO);
    log_message_1(LOG_LEVEL_DEBUG, "suppressed at info threshold\n");
    assert(strlen(level_buffer) == 0);
    log_message_1(LOG_LEVEL_INFO, "info passes\n");
    assert(strlen(level_buffer) > 0);
    log_buffer_clear();

    // DEBUG threshold: all levels pass
    log_set_level(LOG_LEVEL_DEBUG);
    log_message_1(LOG_LEVEL_DEBUG, "debug passes\n");
    assert(strlen(level_buffer) > 0);
    log_buffer_clear();

    log_set_buffer(nullptr, 0);
    log_set_stream(LOG_STREAM_STDOUT);
    printf("Log level tests passed.\n\n");
}

#ifdef LOG_THREAD_IMPLEMENTATION
/* Must match FpThreadCallback exactly (void* (*)(void *const), thread.h:132);
 * returning I32 made this file fail to compile, so the whole suite was dead. */
static void* _thread_logging_func(void *const arg) {
    I32 const thread_num = *(I32 const *) arg;

    /* _log is thread_local (log.c), so each thread gets its own buffer here —
     * this proves per-thread isolation with a real assertion instead of only
     * eyeballing interleaved stdout output. */
    char thread_buffer[TEST_BUFFER_SIZE] = DEFAULT_INITIALIZATION;

    log_set_buffer(thread_buffer, TEST_BUFFER_SIZE);

    for (I32 i = 0; i < 2; i += 1) {
        log_message_1(LOG_LEVEL_INFO, "Thread %d: Message %d\n", thread_num, i);
        log_debug_variable_int(LOG_METADATA, "thread_num", thread_num);
        log_debug_variable_int(LOG_METADATA, "counter", i);
    }

    char expected[64] = DEFAULT_INITIALIZATION;

    snprintf(expected, sizeof(expected), "Thread %d: Message 1", thread_num);
    assert(strstr(thread_buffer, expected) != nullptr);

    return nullptr;
}

static void _test_thread_safety(void) {
    printf("Testing thread safety functionality...\n");

    log_set_stream(LOG_STREAM_STDOUT);
    log_set_buffer(nullptr, 0);
    log_set_timestamp_enabled(true);

    constexpr I32 THREAD_COUNT = 5;
    Thread threads[THREAD_COUNT] = DEFAULT_INITIALIZATION;
    I32 thread_ids[THREAD_COUNT] = DEFAULT_INITIALIZATION;
    I32 started                  = 0;

    for (I32 i = 0; i < THREAD_COUNT; i += 1) {
        thread_ids[i] = i;

        log_message_2(LOG_LEVEL_INFO, LOG_METADATA, "TEST\n");

        bool const created = result_is_success(
            thread_create_1(&threads[i], _thread_logging_func, &thread_ids[i]));

        if (!created) {
            printf("Failed to create thread %d\n", i);

            /* break, NOT return: threads 0..i-1 are running and dereference
             * &thread_ids[...] on this frame, so returning here would leave them
             * reading a destroyed stack frame. Fall through and join them. */
            break;
        }

        started += 1;
    }

    for (I32 i = 0; i < started; i += 1) {
        if (!result_is_success(thread_join_1(&threads[i]))) {
            printf("Failed to join thread %d\n", i);
        }
    }

    printf("Thread safety tests passed.\n\n");
}
#endif // LOG_THREAD_IMPLEMENTATION

static void _test_timestamps(void) {
    printf("Testing timestamp functionality...\n");

    log_set_timestamp_enabled(true);
    log_message_1(LOG_LEVEL_INFO, "Message with timestamp\n");

    log_print_time();
    printf("\n");

    log_set_timestamp_enabled(false);
    log_message_1(LOG_LEVEL_INFO, "Message without timestamp\n");

    printf("Timestamp tests passed.\n\n");
}

static void _test_variable_logging(void) {
    printf("Testing variable logging functionality...\n");

    char const test_char = 'A';

    log_debug_variable_char(LOG_METADATA, "test_char", test_char);

    I64 const test_int = 12345;

    log_debug_variable_int(LOG_METADATA, "test_int", test_int);

    FSize const test_float = 3.14159;

    log_debug_variable_float(LOG_METADATA, "test_float", test_float);

    char const test_str[] = "Hello, World!";

    log_debug_variable_str_1(LOG_METADATA, "test_str", test_str);
    log_debug_variable_str_2(LOG_METADATA, "test_str_part", test_str, 5);

    void const *const test_ptr = &test_int;

    log_debug_variable_ptr(LOG_METADATA, "test_ptr", test_ptr);

    USize const test_uint = 999;

    log_debug_variable_uint(LOG_METADATA, "test_uint", test_uint);

    printf("Variable logging tests passed.\n\n");
}

/* Runs FIRST, before any log_init in this process: the try twins must return
 * without I/O on an uninitialized logger, where log_message_1 would exit. The
 * assertion is the suite still being alive on the next line - an exit here
 * ends the whole run, so this cannot pass vacuously. */
static void _test_try_before_init(void) {
    printf("Testing log_message_try_* before log_init (must be a silent no-op)...\n");

    assert(!log_is_initialized());

    log_message_try_1(LOG_LEVEL_ERROR, "must not appear, must not exit: %d\n", 1);
    log_message_try_2(LOG_LEVEL_ERROR, LOG_METADATA, "must not appear, must not exit: %d\n", 2);

    assert(!log_is_initialized());

    printf("try-before-init tests passed.\n\n");
}

/* After log_init the try twins are log_message_1/_2 exactly: same threshold,
 * same stream. Captured on a tmpfile so the bytes are counted, not eyeballed. */
static void _test_try_after_init(void) {
    printf("Testing log_message_try_* after log_init (must behave as log_message_*)...\n");

    FILE *const capture = tmpfile();

    assert(capture != nullptr);

    log_set_autoflush(true);
    log_set_level(LOG_LEVEL_INFO);
    log_set_buffer(nullptr, 0);
    log_set_stream(capture);

    log_message_try_1(LOG_LEVEL_INFO, "try-one landed: %d\n", 1);
    fflush(capture);

    long const size_after_one = ftell(capture);

    assert(size_after_one > 0);

    log_message_try_2(LOG_LEVEL_INFO, LOG_METADATA, "try-two landed: %d\n", 2);
    fflush(capture);

    long const size_after_two = ftell(capture);

    assert(size_after_two > size_after_one);

    // Threshold still applies: a DEBUG message under an INFO level writes nothing.
    log_message_try_1(LOG_LEVEL_DEBUG, "below threshold, must not land\n");
    fflush(capture);

    assert(ftell(capture) == size_after_two);

    log_set_stream(LOG_STREAM_STDOUT);
    fclose(capture);

    printf("try-after-init tests passed.\n\n");
}

// === Main Test Runner ===
/* The per-line cap (8 KB) and the mirror clamp are the only branches between an
 * over-long payload and a write past a thread_local array; the rest of this suite
 * never reaches them (1 KB mirrors, short messages). A 9 KB payload into a 16 KB
 * mirror lands the capped line (8191 bytes, ending with the marker); a 64-byte
 * mirror keeps 63 of them, terminated. */
static void _test_line_cap_and_mirror_clamp(void) {
    printf("Testing the 8 KB line cap and the mirror clamp...\n");

    static char payload[9 * 1024] = DEFAULT_INITIALIZATION;
    static char mirror[16 * 1024] = DEFAULT_INITIALIZATION;

    memset(payload, 'x', sizeof(payload) - 1);
    payload[sizeof(payload) - 1] = '\0';

    log_set_stream(nullptr);
    log_set_timestamp_enabled(false);
    log_set_buffer(mirror, sizeof(mirror));

    log_message_1(LOG_LEVEL_INFO, "%s\n", payload);

    assert(log_get_buffer_size() == 8191);
    assert(mirror[8191] == '\0');
    assert(strlen(mirror) == 8191);
    assert(strcmp(mirror + 8191 - (sizeof("...[truncated]\n") - 1), "...[truncated]\n") == 0);

    char small[64] = DEFAULT_INITIALIZATION;

    log_set_buffer(small, sizeof(small));
    log_message_1(LOG_LEVEL_INFO, "%s\n", payload);

    assert(log_get_buffer_size() == 63);
    assert(small[63] == '\0');
    assert(strlen(small) == 63);

    // A short line after the clamp: the mirror is full, so nothing more is kept.
    log_message_1(LOG_LEVEL_INFO, "after\n");

    assert(log_get_buffer_size() == 63);

    log_set_buffer(nullptr, 0);
    log_set_stream(LOG_STREAM_STDOUT);

    printf("Line cap and mirror clamp tests passed.\n\n");
}

/* A zero capacity is no buffer at all: the old code kept the pointer and handed
 * log_get_buffer an array it never terminated. */
static void _test_zero_capacity_buffer(void) {
    printf("Testing log_set_buffer with a zero capacity...\n");

    char untouched[8] = "garbage";

    log_set_buffer(untouched, 0);

    assert(log_get_buffer() == nullptr);
    assert(log_get_buffer_size() == 0);
    assert(strcmp(untouched, "garbage") == 0);

    log_message_1(LOG_LEVEL_INFO, "nowhere to go\n");

    assert(strcmp(untouched, "garbage") == 0);

    log_set_buffer(nullptr, 0);

    printf("Zero-capacity buffer tests passed.\n\n");
}

/* The uint dump used to close its block with "\r}" - a terminal trick that put a
 * raw carriage return into file streams and the mirror, and overprinted the brace
 * whenever size was not a row multiple (5 elements, rows of 3). */
static void _test_uint_dump_has_no_carriage_return(void) {
    printf("Testing that the uint array dump carries no carriage return...\n");

    char mirror[TEST_BUFFER_SIZE] = DEFAULT_INITIALIZATION;
    USize const values[] = {1, 2, 3, 4, 5};

    log_set_stream(nullptr);
    log_set_level(LOG_LEVEL_DEBUG);
    log_set_buffer(mirror, sizeof(mirror));

    log_debug_variable_array_uint(LOG_METADATA, "values", values, 5, 3, false);

    assert(strchr(mirror, '\r') == nullptr);
    assert(strstr(mirror, "  5\n}\n") != nullptr);
    assert(strstr(mirror, "\t}") == nullptr);

    log_buffer_clear();
    log_debug_variable_array_uint(LOG_METADATA, "values", values, 3, 3, false);

    assert(strchr(mirror, '\r') == nullptr);
    assert(strstr(mirror, "  3\n}\n") != nullptr);

    // Misc 14: a null string value prints (null) in the sized form as in the plain one.
    log_buffer_clear();
    log_debug_variable_str_2(LOG_METADATA, "missing", nullptr, 0);

    assert(strstr(mirror, "(null)") != nullptr);

    log_set_buffer(nullptr, 0);
    log_set_level(LOG_LEVEL_INFO);
    log_set_stream(LOG_STREAM_STDOUT);

    printf("Carriage-return tests passed.\n\n");
}

#ifdef LOG_THREAD_IMPLEMENTATION
static FILE *_worker_capture = nullptr;

/* The worker's FIRST log call is a try twin: it must inherit the shared default
 * (the capture stream) through log_is_initialized's sync, not stay silent. A
 * future "optimization" to a bare _log.initialized read would break exactly this. */
static void* _worker_try_first(void *const arg) {
    (void) arg;

    log_message_try_1(LOG_LEVEL_INFO, "worker-try-first\n");

    return nullptr;
}

static void _test_worker_first_call_is_a_try_twin(void) {
    printf("Testing that a worker whose first call is a try twin inherits the defaults...\n");

    _worker_capture = tmpfile();

    assert(_worker_capture != nullptr);

    // Re-publish the defaults with the capture as the stream: log_init is the
    // publisher, and the worker copies from it.
    LogConfig const worker_config = { .level = LOG_LEVEL_INFO, .stream = _worker_capture, .timestamp_enabled = false, .autoflush = true };

    log_init(worker_config);

    Thread worker = DEFAULT_INITIALIZATION;

    assert(result_is_success(thread_create_1(&worker, _worker_try_first, nullptr)));
    assert(result_is_success(thread_join_1(&worker)));

    fflush(_worker_capture);

    assert(ftell(_worker_capture) > 0);

    // Re-publish the SHARED default too: log_set_stream only touches this thread's own
    // _log, and log_init above left _log_default.stream pointing at _worker_capture -
    // leaving it there would hand any later not-yet-synced thread a closed FILE*.
    LogConfig const restored_config = { .level = LOG_LEVEL_INFO, .stream = LOG_STREAM_STDOUT, .timestamp_enabled = false, .autoflush = true };

    log_init(restored_config);
    fclose(_worker_capture);
    _worker_capture = nullptr;

    printf("Worker try-twin inheritance tests passed.\n\n");
}
#endif // LOG_THREAD_IMPLEMENTATION

int main(void) {
    printf("Starting log module unit tests...\n\n");

    console_init();

    _test_try_before_init();
    _test_basic_logging();
    _test_try_after_init();
    _test_accessor_roundtrip();
    _test_variable_logging();
    _test_debug_logging();
    _test_log_levels();
    _test_buffer_operations();
    _test_timestamps();
    _test_disabled_stream_contract();
    _test_error_conditions();
    _test_line_cap_and_mirror_clamp();
    _test_zero_capacity_buffer();
    _test_uint_dump_has_no_carriage_return();
#ifdef LOG_THREAD_IMPLEMENTATION
    _test_thread_safety();
    _test_worker_first_call_is_a_try_twin();
#endif // LOG_THREAD_IMPLEMENTATION

    printf("All log module tests completed successfully!\n");

    log_uninit();
    console_uninit();

    return 0;
}