#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>

#include <thread/thread.h>

/*==============================================================================
 * MARK: - Utilities
 *============================================================================*/

// Hand-rolled counters instead of <test/test.h>, deliberately: the harness module
// chains log -> console -> string -> memory, and this suite's whole point is the
// two-TU build (thread.c + this file) proving thread depends on nothing above it.
// Pulling test.h in would silently void that ground-module guarantee.
static atomic_int _tests_passed = 0;
static atomic_int _tests_failed = 0;

#define LOG(fmt, ...) printf("[TEST] " fmt "\n", ##__VA_ARGS__)
#define PASS(fmt, ...) do { LOG("PASS: " fmt, ##__VA_ARGS__); atomic_fetch_add(&_tests_passed, 1); } while (0)
#define FAIL(fmt, ...) do { LOG("FAIL: " fmt, ##__VA_ARGS__); atomic_fetch_add(&_tests_failed, 1); } while (0)

/*==============================================================================
 * MARK: - Thread Test
 *============================================================================*/

static atomic_int _thread_ran = 0;
static ThreadMutex _thread_mutex_log = DEFAULT_INITIALIZATION;

static void* _thread_worker(void *const arg) {
    I32 const id = *(I32*) arg;
    ThreadId const tid = thread_get_id_1();

    thread_mutex_lock(&_thread_mutex_log);

    LOG("Thread %d started (self-id: %lu)", id, (unsigned long) tid);

    thread_mutex_unlock(&_thread_mutex_log);

    thread_yield();
    thread_sleep(50);

    _thread_ran = 1;

    return nullptr;
}

static void test_threads(void) {
    LOG("--- Testing Threads ---");
    thread_mutex_init(&_thread_mutex_log);

    /* Joinable thread */
    Thread t1 = DEFAULT_INITIALIZATION;
    I32 const id1 = 1;

    if (result_is_success(thread_create_1(&t1, _thread_worker, (void*) &id1))) {
        thread_join_1(&t1);

        if (_thread_ran == 1) {
            PASS("Thread executed and completed");
        }
        else {
            FAIL("Joined thread never ran");
        }
    }
    else {
        FAIL("Thread creation failed");
    }

    /* Detached thread. The id lives in static storage and the teardown below
     * waits for the worker's completion flag: a detached thread's lifetime is
     * unbounded, so a stack-local argument or an early mutex_uninit would race
     * a slow scheduler (use-after-scope / destroy-while-locked). */
    _thread_ran = 0;

    Thread t2 = DEFAULT_INITIALIZATION;
    static I32 const id2 = 2;

    if (result_is_success(thread_create_1(&t2, _thread_worker, (void*) &id2))) {
        thread_detach(&t2);

        /* Bounded completion wait (~5 s worst case) instead of one blind sleep. */
        for (I32 waited = 0; _thread_ran != 1 && waited < 100; waited += 1) {
            thread_sleep(50);
        }

        if (_thread_ran == 1) {
            PASS("Detached thread executed independently");
        }
        else {
            FAIL("Detached thread never ran (waited ~5 s)");

            // The worker may still be alive and about to lock the mutex;
            // destroying it under a live thread is UB. This path already
            // reports FAIL loudly, so leak the mutex instead of destroying it.
            return;
        }
    }
    else {
        FAIL("Detached thread creation failed");
    }

    thread_mutex_uninit(&_thread_mutex_log);
}

/*==============================================================================
 * MARK: - Mutex Test
 *============================================================================*/

static ThreadMutex _mutex_test = DEFAULT_INITIALIZATION;
static I32 _mutex_count = 0;

static Result _trylock_probe_result = RESULT_SUCCESS;

static void* _trylock_prober(void *const arg) {
    (void) arg;

    _trylock_probe_result = thread_mutex_lock_try(&_mutex_test);

    if (result_is_success(_trylock_probe_result)) {
        thread_mutex_unlock(&_mutex_test);
    }

    return nullptr;
}

static void* _mutex_worker(void *const arg) {
    (void) arg;

    for (I32 i = 0; i < 1000; i += 1) {
        thread_mutex_lock(&_mutex_test);

        _mutex_count += 1;

        thread_mutex_unlock(&_mutex_test);
    }

    return nullptr;
}

static void test_mutex(void) {
    LOG("--- Testing Mutex ---");
    thread_mutex_init(&_mutex_test);

    /* Basic contention */
    _mutex_count = 0;

    Thread t[4] = DEFAULT_INITIALIZATION;
    bool created[4] = DEFAULT_INITIALIZATION;

    for (I32 i = 0; i < 4; i += 1) {
        created[i] = result_is_success(thread_create_1(&t[i], _mutex_worker, nullptr));

        if (!created[i]) {
            FAIL("Mutex thread %d creation failed", i);
        }
    }

    for (I32 i = 0; i < 4; i += 1) {
        if (created[i]) {
            thread_join_1(&t[i]);
        }
    }

    if (_mutex_count == 4000) {
        PASS("Mutex protected counter incremented correctly");
    }
    else {
        FAIL("Mutex counter is %d, expected 4000", _mutex_count);
    }

    /* TryLock while another thread holds the mutex must fail, and must succeed
     * once it is free. Probed from a second thread: on Windows a
     * CRITICAL_SECTION is recursive, so a same-thread trylock would succeed
     * and prove nothing. */
    thread_mutex_lock(&_mutex_test);

    Thread prober = DEFAULT_INITIALIZATION;

    if (result_is_success(thread_create_1(&prober, _trylock_prober, nullptr))) {
        thread_join_1(&prober);

        /* Pin the exact contention value, not just "some error": the module
         * promises the same STATE|RETRYABLE Result on both platforms. */
        if (_trylock_probe_result == result_make(RESULT_CATEGORY_STATE, 0, RESULT_FLAG_RETRYABLE)) {
            PASS("Cross-thread trylock on a held mutex returns the contention Result");
        }
        else {
            FAIL("Cross-thread trylock returned %u, expected STATE|RETRYABLE", (U32) _trylock_probe_result);
        }
    }
    else {
        FAIL("Trylock prober creation failed");
    }

    thread_mutex_unlock(&_mutex_test);

    if (result_is_success(thread_mutex_lock_try(&_mutex_test))) {
        PASS("Trylock on a free mutex succeeds");
        thread_mutex_unlock(&_mutex_test);
    }
    else {
        FAIL("Trylock on a free mutex failed");
    }

    /* A timed wait nobody signals must elapse with the NAMED timeout Result -
     * byte-identical on both platforms, so compare with ==, never just
     * result_is_error. (timedwait requires the mutex locked on entry and
     * reacquires it on timeout, so the unlock below is balanced.) */
    thread_mutex_lock(&_mutex_test);

    ThreadCond cond = DEFAULT_INITIALIZATION;

    if (result_is_success(thread_cond_init(&cond))) {
        Result const waited = thread_cond_mutex_timedwait(&cond, &_mutex_test, 30);

        if (waited == RESULT_THREAD_TIMEOUT_CODE) {
            PASS("Unsignaled timedwait returns RESULT_THREAD_TIMEOUT_CODE");
        }
        else {
            FAIL("Unsignaled timedwait returned %u, expected RESULT_THREAD_TIMEOUT_CODE", (U32) waited);
        }

        thread_cond_uninit(&cond);
    }
    else {
        FAIL("Cond creation failed");
    }

    thread_mutex_unlock(&_mutex_test);

    thread_mutex_uninit(&_mutex_test);
}

/*==============================================================================
 * MARK: - Main
 *============================================================================*/

I32 main(void) {
    LOG("Starting Cross-Platform Sync Library Tests");

    test_threads();
    test_mutex();

    LOG("===============================");
    LOG("Tests Passed: %d", atomic_load(&_tests_passed));
    LOG("Tests Failed: %d", atomic_load(&_tests_failed));

    /* Zero failures alone is not success: a suite that recorded nothing at all
     * must not report green (the vacuous-pass trap). */
    bool const ok = atomic_load(&_tests_failed) == 0 && atomic_load(&_tests_passed) > 0;

    LOG("Status: %s", ok ? "SUCCESS" : "FAILURE");

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}