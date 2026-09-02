/*
 * thread.h - Cross-platform thread and synchronization primitives for the C Libraries Framework
 *
 * Features:
 *   - Thread creation, joining, detaching, and identification
 *   - Mutex for mutual exclusion
 *   - Condition variables for thread coordination
 *   - Read-write locks for reader-writer synchronization
 *   - Barriers for thread synchronization
 *   - Cross-platform compatibility (Windows and POSIX)
 *
 * Usage Examples:
 *   @code
 *   Thread t = DEFAULT_INITIALIZATION;
 *   thread_create_1(&t, my_function, data);
 *   thread_join_1(&t);
 *
 *   ThreadMutex m = DEFAULT_INITIALIZATION;
 *   thread_mutex_init(&m);
 *   thread_mutex_lock(&m);
 *   // critical section
 *   thread_mutex_unlock(&m);
 *   thread_mutex_uninit(&m);
 *   @endcode
 *
 * Error Handling:
 *   - Every function validates its non-nullable pointer arguments first and
 *     returns a Result by value; OS failures are mapped through result_from_os.
 *   - Argument validation is self-contained: a bare abort() under
 *     ERROR_CHECK_ENABLED, nothing in release builds. This module sits below
 *     the diagnostic stack (log's thread-safety lock is a ThreadMutex), so the
 *     error_check_* helpers would be a circular dependency here — the same
 *     ruling result.h documents for result_make.
 *
 * Thread Safety:
 *   - Reentrant. Functions operate on caller-owned primitives; the caller is
 *     responsible for the lifetime of each object passed in.
 *
 * Memory Management:
 *   - Primitives are caller-allocated (stack or embedded). thread_create_2
 *     allocates a small per-thread argument record internally and frees it
 *     inside the started thread; callers never see that allocation.
 *
 * Performance Characteristics:
 *   - Thin wrappers over the native primitives; O(1) plus the underlying
 *     OS call cost.
 *
 * Dependencies:
 *   - <stdlib.h>, <result.h> (deliberately nothing higher — thread is a ground
 *     module; error/tracelog/log all sit above it); Windows: <process.h>;
 *     POSIX: <pthread.h>, <sched.h>, <sys/time.h>, <time.h>, <unistd.h>
 *   - Under a strict -std=cNN (what every build/linux makefile uses), Linux REQUIRES -D_GNU_SOURCE
 *     (or any -D_POSIX_C_SOURCE >= 200112L); short of that, thread.c fails to build outright rather
 *     than degrading. Verified on WSL Debian gcc 14.2: 200112L compiles thread.c clean; 199506L (POSIX.1-1996) and
 *     -D_XOPEN_SOURCE=500 alone both fail to build it — the latter despite compiling the header in
 *     isolation, since thread.c's own POSIX barrier calls (lines 130, 193/196, 218) have no build
 *     path to the header's alternate ThreadBarrier layout (line ~120), which is untested and
 *     unreachable through this module as shipped. Every build/linux makefile already defines
 *     -D_GNU_SOURCE, so this is a documented requirement rather than a live break.
 *
 * See thread.c for implementation details.
 */

#ifndef THREAD_H
#define THREAD_H

#include <stdlib.h>

#include <result.h>

#ifdef OS_WINDOWS
    #include <process.h>
#else
    #include <pthread.h>
    #include <sched.h>
    #include <sys/time.h>
    #include <time.h>
    #include <unistd.h>
#endif

/*==============================================================================
 * MARK: - Type Definitions
 *============================================================================*/

typedef struct {
#ifdef OS_WINDOWS
    HANDLE native_handle;
#else
    pthread_t native_handle;
#endif
} Thread;

// Never relock a ThreadMutex the calling thread already holds: the Windows
// CRITICAL_SECTION is recursive but the POSIX default mutex is not (EBUSY from
// a trylock, deadlock from a lock), so portable code must treat the mutex as
// non-recursive. Cross-thread contention behaves identically on both platforms.
typedef struct {
#ifdef OS_WINDOWS
    CRITICAL_SECTION native_handle;
#else
    pthread_mutex_t native_handle;
#endif
} ThreadMutex;

typedef struct {
#ifdef OS_WINDOWS
    CONDITION_VARIABLE native_handle;
#else
    pthread_cond_t native_handle;
#endif
} ThreadCond;

typedef struct {
#ifdef OS_WINDOWS
    SRWLOCK native_handle;
#else
    pthread_rwlock_t native_handle;
#endif
} ThreadRWLock;

typedef struct {
#if defined(OS_WINDOWS) || !defined(PTHREAD_BARRIER_SERIAL_THREAD)
    // Barrier implementation for Windows
    struct {
        ThreadMutex mutex;
        ThreadCond  cond;
        USize       size;
        USize       capacity;
        USize       generation;
    } native_handle;
#else
    pthread_barrier_t native_handle;
#endif
} ThreadBarrier;

#ifdef OS_WINDOWS
typedef DWORD ThreadId;
#else
typedef pthread_t ThreadId;
#endif

// Named Results this module returns, comparable directly with == (all use a
// 0/1 code field so the value is byte-identical on both platforms).
#define RESULT_THREAD_BARRIER_SERIAL_CODE result_make(RESULT_CATEGORY_IO, 0, RESULT_FLAG_TRANSIENT)
/** @def RESULT_THREAD_TIMEOUT_CODE @brief A timed wait elapsed with no signal. */
#define RESULT_THREAD_TIMEOUT_CODE result_make(RESULT_CATEGORY_SYSTEM, 0, RESULT_FLAG_RETRYABLE | RESULT_FLAG_TRANSIENT)
/** @def RESULT_THREAD_UNSUPPORTED_CODE @brief The operation does not exist on this platform (code 1 keeps it distinct from the consumed-handle STATE error). */
#define RESULT_THREAD_UNSUPPORTED_CODE result_make(RESULT_CATEGORY_STATE, 1, RESULT_FLAG_CRITICAL)

/**
 * @brief Thread callback function type
 * @param data User-provided data passed to the thread function
 * @return Exit code, surfaced by thread_join_2 (matches thread_exit's value)
 */
typedef void* (*FpThreadCallback)(void *const data);

/**
 * @brief Thread attributes structure
 */
typedef struct {
#ifdef OS_WINDOWS
    struct {
        ISize   stack_size;
        bool    detach;
    } native_handle;
#else
    pthread_attr_t native_handle;
#endif
} ThreadAttr;

/*==============================================================================
 * MARK: - Barrier Operations
 *============================================================================*/

/**
 * @brief Initialize a thread barrier
 * @param self Pointer to barrier to initialize
 * @param count Number of threads to wait for
 * @return Result indicating success or error
 */
Result thread_barrier_init(ThreadBarrier *const self, USize const count);

/**
 * @brief Wait at a thread barrier
 * @param self Pointer to barrier to wait at
 * @return Result indicating success or error
 */
Result thread_barrier_wait(ThreadBarrier *const self);

/**
 * @brief Destroy a thread barrier
 * @param self Pointer to barrier to destroy
 * @return Result indicating success or error
 */
Result thread_barrier_uninit(ThreadBarrier *const self);

/*==============================================================================
 * MARK: - Condition Variable Operations
 *============================================================================*/

/**
 * @brief Initialize a condition variable
 * @param self Pointer to condition variable to initialize
 * @return Result indicating success or error
 */
Result thread_cond_init(ThreadCond *const self);

/**
 * @brief Wait on a condition variable
 * @param self Pointer to condition variable
 * @param mutex Pointer to associated mutex (must be locked)
 * @return Result indicating success or error
 * @note The rwlock variant is Windows-only (POSIX condition variables can only
 *       pair with a mutex); on POSIX it returns RESULT_THREAD_UNSUPPORTED_CODE.
 */
Result thread_cond_mutex_wait(ThreadCond *const self, ThreadMutex *const mutex);
Result thread_cond_rwlock_wait(ThreadCond *const self, ThreadRWLock *const rwlock);

/**
 * @brief Wait on a condition variable with timeout
 * @param self Pointer to condition variable
 * @param mutex Pointer to associated mutex (must be locked)
 * @param ms Timeout in milliseconds
 * @return Result indicating success or error; RESULT_THREAD_TIMEOUT_CODE (same
 *         value on both platforms) when the wait elapsed with no signal
 * @note The rwlock variant is Windows-only, as with thread_cond_rwlock_wait.
 */
Result thread_cond_mutex_timedwait(ThreadCond *const self, ThreadMutex *const mutex, ISize const ms);
Result thread_cond_rwlock_timedwait(ThreadCond *const self, ThreadRWLock *const rwlock, ISize const ms);

/**
 * @brief Signal a condition variable (wake one waiter)
 * @param self Pointer to condition variable
 * @return Result indicating success or error
 */
Result thread_cond_wake(ThreadCond *const self);

/**
 * @brief Broadcast a condition variable (wake all waiters)
 * @param self Pointer to condition variable
 * @return Result indicating success or error
 */
Result thread_cond_wake_all(ThreadCond *const self);

/**
 * @brief Destroy a condition variable
 * @param self Pointer to condition variable to destroy
 * @return Result indicating success or error
 */
Result thread_cond_uninit(ThreadCond *const self);

/*==============================================================================
 * MARK: - Mutex Operations
 *============================================================================*/

/**
 * @brief Initialize a mutex
 * @param self Pointer to mutex to initialize
 * @return Result indicating success or error
 */
Result thread_mutex_init(ThreadMutex *const self);

/**
 * @brief Lock a mutex (blocking)
 * @param self Pointer to mutex to lock
 * @return Result indicating success or error
 */
Result thread_mutex_lock(ThreadMutex *const self);

/**
 * @brief Try to lock a mutex (non-blocking)
 * @param self Pointer to mutex to try-lock
 * @return Result indicating success or error
 */
Result thread_mutex_lock_try(ThreadMutex *const self);

/**
 * @brief Unlock a mutex
 * @param self Pointer to mutex to unlock
 * @return Result indicating success or error
 */
Result thread_mutex_unlock(ThreadMutex *const self);

/**
 * @brief Destroy a mutex
 * @param self Pointer to mutex to destroy
 * @return Result indicating success or error
 */
Result thread_mutex_uninit(ThreadMutex *const self);

/*==============================================================================
 * MARK: - Read-Write Lock Operations
 *============================================================================*/

/**
 * @brief Initialize a read-write lock
 * @param self Pointer to read-write lock to initialize
 * @return Result indicating success or error
 */
Result thread_rwlock_init(ThreadRWLock *const self);

/**
 * @brief Acquire read lock
 * @param self Pointer to read-write lock
 * @return Result indicating success or error
 */
Result thread_rwlock_read_lock(ThreadRWLock *const self);

/**
 * @brief Try to acquire read lock (non-blocking)
 * @param self Pointer to read-write lock
 * @return Result indicating success or error
 */
Result thread_rwlock_read_lock_try(ThreadRWLock *const self);

/**
 * @brief Acquire write lock
 * @param self Pointer to read-write lock
 * @return Result indicating success or error
 */
Result thread_rwlock_write_lock(ThreadRWLock *const self);

/**
 * @brief Try to acquire write lock (non-blocking)
 * @param self Pointer to read-write lock
 * @return Result indicating success or error
 */
Result thread_rwlock_write_lock_try(ThreadRWLock *const self);

/**
 * @brief Unlock a read-write lock
 * @param self Pointer to read-write lock
 * @return Result indicating success or error
 */
Result thread_rwlock_read_unlock(ThreadRWLock *const self);
Result thread_rwlock_write_unlock(ThreadRWLock *const self);

/**
 * @brief Destroy a read-write lock
 * @param self Pointer to read-write lock to destroy
 * @return Result indicating success or error
 */
Result thread_rwlock_uninit(ThreadRWLock *const self);

/*==============================================================================
 * MARK: - Thread Management
 *============================================================================*/

/**
 * @brief Initialize thread attributes
 * @param self Thread attributes structure to initialize
 * @return Result indicating success or error
 */
Result thread_attr_init_1(ThreadAttr *const self);
Result thread_attr_init_2(ThreadAttr *const self, ISize const stack_size);
Result thread_attr_init_3(ThreadAttr *const self, bool const detach);
Result thread_attr_init_4(ThreadAttr *const self, bool const detach, ISize const stack_size);

/**
 * @brief Set thread stack size
 * @param self Thread attributes structure
 * @param stack_size Stack size in bytes
 * @return Result indicating success or error
 */
Result thread_attr_set_stacksize(ThreadAttr *const self, ISize const stack_size);

/**
 * @brief Set thread detach state
 * @param self Thread attributes structure
 * @param detach True for detached thread, false for joinable
 * @return Result indicating success or error
 */
Result thread_attr_set_detach(ThreadAttr *const self, bool const detach);

/**
 * @brief Destroy thread attributes
 * @param self Thread attributes structure to destroy
 * @return Result indicating success or error
 */
Result thread_attr_uninit(ThreadAttr *const self);

/**
 * @brief Create a new thread
 * @param self Pointer to thread handle to initialize
 * @param cb Function to execute in the new thread
 * @param data Data to pass to the thread function
 * @return Result indicating success or error
 */
Result thread_create_1(Thread *const self, FpThreadCallback const cb, void *const data);

/**
 * @brief Create a new thread
 * @param self Pointer to thread handle to initialize
 * @param attr Thread attributes (can be NULL for defaults)
 * @param cb Function to execute in the new thread
 * @param data Data to pass to the thread function
 * @return Result indicating success or error
 */
Result thread_create_2(Thread *const self, ThreadAttr const *const attr, FpThreadCallback const cb, void *const data);

/**
 * @brief Wait for a thread to finish
 * @param self Thread to join
 * @param code Pointer to store thread result (can be NULL)
 * @return Result indicating success or error
 * @note On Windows the Thread is consumed once joined (its handle is closed);
 *       joining or detaching it again returns a STATE error.
 */
Result thread_join_1(Thread *const self);
Result thread_join_2(Thread *const self, I32 *const code);

/**
 * @brief Detach a thread (run independently)
 * @param self Thread to detach
 * @return Result indicating success or error
 * @note On Windows the Thread is consumed once detached; joining or detaching
 *       it again returns a STATE error.
 */
Result thread_detach(Thread *const self);

/**
 * @brief Get current thread ID
 * @return ThreadId of current thread
 */
ThreadId thread_get_id_1(void);
ThreadId thread_get_id_2(Thread const *const self);

/**
 * @brief Compare two threads for equality
 * @param self First thread
 * @param source Second thread
 * @return true if threads are equal, false otherwise
 */
bool thread_equal(Thread const *const self, Thread const *const source);

/**
 * @brief Yield the current thread to scheduler
 * @return true if the CPU was yielded to another thread; false if no other
 *         thread was ready to run (this is not an error)
 */
bool thread_yield(void);

/**
 * @brief Exit the current thread
 * @param result Thread exit value
 */
void thread_exit(I32 const result);

/**
 * @brief Sleep for specified milliseconds
 * @param ms Time to sleep in milliseconds (clamped to ~49 days on Windows)
 */
void thread_sleep(USize const ms);

#endif // THREAD_H