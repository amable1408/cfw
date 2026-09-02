/*
 * test_all.c - Unit tests for the tracelog module
 *
 * Features tested:
 *   - Push/pop symmetry (depth returns to baseline)
 *   - Print content (pushed frame's function name and TRACE tag appear)
 *   - Overflow behaviour at the fixed capacity bound (dropped-but-counted
 *     pushes, TRACE TRUNCATED reporting, balanced unwind)
 *   - Pop on an empty stack (no underflow, stack still usable afterward)
 *   - Thread-locality (a worker thread's pushes are invisible to the main
 *     thread's trace stack)
 *
 * Hand-rolled counters instead of <test/test.h>, deliberately: test.h/test.c
 * push and pop the trace stack themselves around every assertion (test.c
 * calls trace_log_push/trace_log_pop internally), which would corrupt the
 * very depth counts this suite is asserting on. Pulling it in would make the
 * harness fight the module under test.
 */
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <log/log.h>
#include <thread/thread.h>
#include <tracelog/tracelog.h>

/*==============================================================================
 * MARK: - Utilities
 *============================================================================*/
static atomic_int _tests_passed = 0;
static atomic_int _tests_failed = 0;

#define LOG(fmt, ...) printf("[TEST] " fmt "\n", ##__VA_ARGS__)
#define PASS(fmt, ...) do { LOG("PASS: " fmt, ##__VA_ARGS__); atomic_fetch_add(&_tests_passed, 1); } while (0)
#define FAIL(fmt, ...) do { LOG("FAIL: " fmt, ##__VA_ARGS__); atomic_fetch_add(&_tests_failed, 1); } while (0)

// Mirrors tracelog.c's private _TRACELOG_FRAME_CAPACITY (not exposed via the
// header). Keep this in sync if that constant ever changes.
#define _TRACELOG_TEST_CAPACITY 64

#define _CAPTURE_PATH "tracelog_capture.tmp"

// trace_log_print writes to whatever FILE* log_get_stream() returns, so
// content is captured by pointing the log module's stream at a scratch file,
// printing, then reading the file back.
static bool _capture_read(char *const buffer, USize const buffer_size) {
    FILE *const file = fopen(_CAPTURE_PATH, "rb");

    if (file == nullptr) {
        return false;
    }

    USize const read_count = fread(buffer, 1, buffer_size - 1, file);

    buffer[read_count] = '\0';
    fclose(file);

    return true;
}

/*==============================================================================
 * MARK: - Push/Pop Symmetry
 *============================================================================*/
static void test_push_pop_symmetry(void) {
    LOG("--- Testing Push/Pop Symmetry ---");
    trace_log_clear();

    if (trace_log_depth() == 0) {
        PASS("Baseline depth is 0 after clear");
    }
    else {
        FAIL("Baseline depth is %llu, expected 0", (unsigned long long) trace_log_depth());
    }

    for (I32 i = 0; i < 10; i += 1) {
        trace_log_push(LOG_METADATA);
    }

    if (trace_log_depth() == 10) {
        PASS("Depth is 10 after 10 pushes");
    }
    else {
        FAIL("Depth is %llu after 10 pushes, expected 10", (unsigned long long) trace_log_depth());
    }

    for (I32 i = 0; i < 10; i += 1) {
        trace_log_pop();
    }

    if (trace_log_depth() == 0) {
        PASS("Depth returns to baseline (0) after matching pops");
    }
    else {
        FAIL("Depth is %llu after matching pops, expected 0", (unsigned long long) trace_log_depth());
    }
}

/*==============================================================================
 * MARK: - Print Content
 *============================================================================*/
static void test_print_content(void) {
    LOG("--- Testing Print Content ---");
    trace_log_clear();

    FILE *const capture = fopen(_CAPTURE_PATH, "w+b");

    if (capture == nullptr) {
        FAIL("Could not open capture file for print content test");

        return;
    }

    log_set_stream(capture);
    trace_log_push(LOG_METADATA);
    trace_log_print();
    fflush(capture);
    fclose(capture);
    log_set_stream(LOG_STREAM_STDOUT);

    char content[1024] = DEFAULT_INITIALIZATION;

    if (_capture_read(content, sizeof(content))) {
        bool const has_function = strstr(content, __func__) != nullptr;
        bool const has_trace_tag = strstr(content, "TRACE") != nullptr;

        if (has_function && has_trace_tag) {
            PASS("Printed trace contains the pushed function name and the TRACE tag");
        }
        else {
            FAIL("Printed trace missing expected content: [%s]", content);
        }
    }
    else {
        FAIL("Could not read back the capture file");
    }

    trace_log_pop();
    trace_log_clear();
}

/*==============================================================================
 * MARK: - Print Content (Buffer Mirror)
 *============================================================================*/
// Buffer-side twin of test_print_content: with the stream DISABLED
// (log_set_stream(nullptr)) and a buffer configured, trace_log_print must
// still land the trace somewhere an operator can read it - via log_print_raw
// mirroring into the log buffer - rather than silently discarding it.
static void test_print_buffer_mirror(void) {
    LOG("--- Testing Print Content (Buffer Mirror) ---");
    trace_log_clear();

    char buffer[1024] = DEFAULT_INITIALIZATION;

    log_set_stream(nullptr);
    log_set_buffer(buffer, sizeof(buffer));

    trace_log_push(LOG_METADATA);
    trace_log_push(LOG_METADATA);
    trace_log_print();

    bool const has_function = strstr(buffer, __func__) != nullptr;
    bool const has_trace_tag = strstr(buffer, "TRACE") != nullptr;

    if (has_function && has_trace_tag) {
        PASS("With the stream disabled, the log buffer still receives the trace (function name and TRACE tag)");
    }
    else {
        FAIL("Log buffer missing expected content with stream disabled: [%s]", buffer);
    }

    log_set_buffer(nullptr, 0);
    log_set_stream(LOG_STREAM_STDOUT);

    trace_log_pop();
    trace_log_pop();
    trace_log_clear();
}

/*==============================================================================
 * MARK: - Overflow Capacity Bound
 *============================================================================*/
static void test_overflow_capacity(void) {
    LOG("--- Testing Overflow Capacity Bound ---");
    trace_log_clear();

    I32 const overflow_count = 5;
    I32 const total_pushes   = _TRACELOG_TEST_CAPACITY + overflow_count;

    for (I32 i = 0; i < total_pushes; i += 1) {
        trace_log_push(LOG_METADATA);
    }

    if (trace_log_depth() == _TRACELOG_TEST_CAPACITY) {
        PASS("Depth caps at capacity (%d) after %d pushes", _TRACELOG_TEST_CAPACITY, total_pushes);
    }
    else {
        FAIL("Depth is %llu after %d pushes, expected cap of %d",
            (unsigned long long) trace_log_depth(), total_pushes, _TRACELOG_TEST_CAPACITY);
    }

    FILE *const capture = fopen(_CAPTURE_PATH, "w+b");

    if (capture != nullptr) {
        log_set_stream(capture);
        trace_log_print();
        fflush(capture);
        fclose(capture);
        log_set_stream(LOG_STREAM_STDOUT);

        // 64 recorded frames at ~90 bytes/line (with ANSI colour codes) plus
        // the trailing TRACE TRUNCATED line comfortably exceeds 4 KB, so the
        // buffer must be sized for the whole capture, not just a snippet.
        char content[16384] = DEFAULT_INITIALIZATION;

        if (_capture_read(content, sizeof(content))) {
            bool const has_truncated = strstr(content, "TRACE TRUNCATED") != nullptr;
            bool const has_count = strstr(content, "5 deeper frame(s) not recorded") != nullptr;

            if (has_truncated && has_count) {
                PASS("Print reports TRACE TRUNCATED with the exact overflow count (5)");
            }
            else {
                FAIL("Print did not report the expected overflow text: [%s]", content);
            }
        }
        else {
            FAIL("Could not read back the overflow capture file");
        }
    }
    else {
        FAIL("Could not open capture file for overflow test");
    }

    // Pops must consume the dropped (overflow) frames first, then the
    // recorded ones, and the whole thing must balance back to empty - this
    // pins the "a dropped push still gets a matching pop" contract.
    for (I32 i = 0; i < total_pushes; i += 1) {
        trace_log_pop();
    }

    if (trace_log_depth() == 0) {
        PASS("Depth returns to 0 after popping every push, including the overflowed ones");
    }
    else {
        FAIL("Depth is %llu after full unwind, expected 0", (unsigned long long) trace_log_depth());
    }
}

/*==============================================================================
 * MARK: - Pop On Empty
 *============================================================================*/
static void test_pop_on_empty(void) {
    LOG("--- Testing Pop On Empty Stack ---");
    trace_log_clear();

    if (trace_log_depth() != 0) {
        FAIL("Clear did not reset depth to 0 before the pop-on-empty test");

        return;
    }

    trace_log_pop();
    trace_log_pop();
    trace_log_pop();

    if (trace_log_depth() == 0) {
        PASS("Popping an empty stack repeatedly stays at depth 0 (no underflow)");
    }
    else {
        FAIL("Depth is %llu after popping an empty stack, expected 0", (unsigned long long) trace_log_depth());
    }

    // Confirm the stack is not corrupted: a normal push/pop cycle afterward
    // still behaves correctly.
    trace_log_push(LOG_METADATA);

    if (trace_log_depth() == 1) {
        PASS("Stack still functions normally after empty-pop attempts");
    }
    else {
        FAIL("Depth is %llu after one push following empty-pops, expected 1", (unsigned long long) trace_log_depth());
    }

    trace_log_pop();
}

/*==============================================================================
 * MARK: - Thread Locality
 *============================================================================*/
static atomic_int _worker_depth = 0;
static atomic_int _worker_ran = 0;

static void* _tracelog_thread_worker(void *const arg) {
    (void) arg;

    trace_log_clear();
    trace_log_push(LOG_METADATA);
    trace_log_push(LOG_METADATA);
    trace_log_push(LOG_METADATA);

    atomic_store(&_worker_depth, (int) trace_log_depth());
    atomic_store(&_worker_ran, 1);

    return nullptr;
}

static void test_thread_locality(void) {
    LOG("--- Testing Thread Locality ---");
    trace_log_clear();
    trace_log_push(LOG_METADATA);
    trace_log_push(LOG_METADATA);

    USize const main_depth_before = trace_log_depth();

    Thread worker = DEFAULT_INITIALIZATION;

    if (result_is_success(thread_create_1(&worker, _tracelog_thread_worker, nullptr))) {
        thread_join_1(&worker);

        USize const main_depth_after = trace_log_depth();

        if (main_depth_before == 2 && main_depth_after == 2) {
            PASS("Main thread depth (2) is unaffected by the worker thread's pushes");
        }
        else {
            FAIL("Main thread depth changed: before=%llu after=%llu",
                (unsigned long long) main_depth_before, (unsigned long long) main_depth_after);
        }

        if (atomic_load(&_worker_ran) == 1 && atomic_load(&_worker_depth) == 3) {
            PASS("Worker thread recorded its own depth (3) independently of the main thread");
        }
        else {
            FAIL("Worker thread depth wrong or it never ran: ran=%d depth=%d",
                atomic_load(&_worker_ran), atomic_load(&_worker_depth));
        }
    }
    else {
        FAIL("Thread creation failed for the locality test");
    }

    trace_log_clear();
}

/*==============================================================================
 * MARK: - Main
 *============================================================================*/
I32 main(void) {
    log_init((LogConfig){
        .level             = LOG_LEVEL_INFO,
        .stream            = LOG_STREAM_STDOUT,
        .timestamp_enabled = false,
        .autoflush         = true
    });

    LOG("Starting tracelog module tests");

    test_push_pop_symmetry();
    test_print_content();
    test_print_buffer_mirror();
    test_overflow_capacity();
    test_pop_on_empty();
    test_thread_locality();

    remove(_CAPTURE_PATH);

    LOG("===============================");
    LOG("Tests Passed: %d", atomic_load(&_tests_passed));
    LOG("Tests Failed: %d", atomic_load(&_tests_failed));

    /* Zero failures alone is not success: a suite that recorded nothing at
     * all must not report green (the vacuous-pass trap). */
    bool const ok = atomic_load(&_tests_failed) == 0 && atomic_load(&_tests_passed) > 0;

    LOG("Status: %s", ok ? "SUCCESS" : "FAILURE");

    log_uninit();

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}