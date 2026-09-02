/*
 * thread.c - Cross-platform thread and synchronization primitives implementation
 *
 * Provides a comprehensive, self-contained thread library that doesn't depend on
 * other framework modules. Designed to be used by core framework components like
 * logging and error handling.
 *
 * Features:
 *   - Cross-platform thread management with error checking
 *   - Comprehensive argument validation
 *
 * Usage Example:
 *   @code
 *   Thread t = DEFAULT_INITIALIZATION;
 *   thread_create_1(&t, my_function, data);
 *   thread_join_1(&t);
 *   @endcode
 *
 * Error Handling:
 *   All functions check their non-nullable pointer arguments first. The checks
 *   are self-contained (_thread_check_null below): a bare abort() under
 *   ERROR_CHECK_ENABLED, nothing in release builds — this module sits below the
 *   diagnostic stack, so the error_check_* helpers would be circular here.
 *
 * Thread Safety:
 *   Thread-safe. Functions operate on caller-owned data structures and use the
 *   appropriate synchronization primitives.
 *
 * See thread.h for API documentation and usage details.
 */

#include <thread/thread.h>

// Self-contained null guard: a bare abort() under ERROR_CHECK_ENABLED, nothing
// in release builds. This module sits below the diagnostic stack (log's
// thread-safety lock is a ThreadMutex from here), so the error_check_* helpers
// would be a circular dependency - the same ruling result.h documents for
// result_make's own checks.
static void _thread_check_null(void const *const pointer) {
#ifdef ERROR_CHECK_ENABLED
    if (pointer == nullptr) {
        abort();
    }
#else
    (void) pointer;
#endif
}

// Per-thread argument record handed to the platform thread-create call; the
// started thread owns it and frees it inside the wrapper below.
typedef struct {
    FpThreadCallback cb;
    void            *data;
} ThreadArgs;

// Platform-specific helpers (all dependency headers live in thread.h)
#ifdef OS_WINDOWS
    static DWORD WINAPI _thread_callback_wrapper(void *const data) {
        ThreadArgs *const args = (ThreadArgs*) data;

        FpThreadCallback const cb  = args->cb;
        void            *const arg = args->data;

        free(args);

        // The join contract is an I32 exit code (thread_exit's value); a
        // pointer-sized callback return deliberately truncates to its low 32
        // bits here, matching the POSIX branch's (ISize) narrowing in reverse.
        return (DWORD) (U32) (USize) cb(arg);
    }

    static U32 _ms_to_timeout(ISize const ms) {
        if (ms < 0) {
            return INFINITE;
        }

        return ms > 0xFFFFFFFE ? 0xFFFFFFFE : (U32) ms;
    }
#else
    static void* _thread_callback_wrapper(void *const data) {
        ThreadArgs *const args = (ThreadArgs*) data;

        FpThreadCallback const cb  = args->cb;
        void            *const arg = args->data;

        free(args);

        return (void*) (ISize) cb(arg);
    }

    static bool _timespec_conversion(ISize const ms, struct timespec *const out) {
        struct timespec time = DEFAULT_INITIALIZATION;

        if (clock_gettime(CLOCK_REALTIME, &time)) {
            return false;
        }

        ISize const secs  = ms / 1000;
        ISize const nsecs = (ms % 1000) * 1000000;

        time.tv_sec  += secs;
        time.tv_nsec += nsecs;

        if (time.tv_nsec >= 1000000000) {
            time.tv_sec  += 1;
            time.tv_nsec -= 1000000000;
        }

        while (time.tv_nsec < 0) {
            time.tv_sec  -= 1;
            time.tv_nsec += 1000000000;
        }

        *out = time;

        return true;
    }
#endif

/*==============================================================================
 * MARK: - Barrier Functions
 *============================================================================*/

Result thread_barrier_init(ThreadBarrier *const self, USize const count) {
    _thread_check_null(self);

#ifdef OS_WINDOWS
    if (count == 0) {
        return result_make(RESULT_CATEGORY_ARGUMENT, 0, RESULT_FLAG_RETRYABLE);
    }

    Result result = thread_mutex_init(&self->native_handle.mutex);

    if (result_is_error(result)) {
        return result;
    }

    result = thread_cond_init(&self->native_handle.cond);

    if (result_is_error(result)) {
        thread_mutex_uninit(&self->native_handle.mutex);

        return result;
    }

    self->native_handle.capacity   = count;
    self->native_handle.size       = 0;
    self->native_handle.generation = 0;
#else
    I32 const error_code = pthread_barrier_init(&self->native_handle, nullptr, count);

    if (error_code != 0) {
        return result_from_os_code((U32) error_code);
    }
#endif

    return RESULT_SUCCESS;
}

Result thread_barrier_wait(ThreadBarrier *const self) {
    _thread_check_null(self);

#ifdef OS_WINDOWS
    Result result = thread_mutex_lock(&self->native_handle.mutex);

    if (result_is_error(result)) {
        return result;
    }

    USize const generation = self->native_handle.generation;

    self->native_handle.size += 1;

    if (self->native_handle.size == self->native_handle.capacity) {
        /* Last thread arrives: reset counter, bump generation, wake all */
        self->native_handle.size        = 0;
        self->native_handle.generation += 1;

        result = thread_cond_wake_all(&self->native_handle.cond);

        if (result_is_error(result)) {
            thread_mutex_unlock(&self->native_handle.mutex);

            return result;
        }

        result = thread_mutex_unlock(&self->native_handle.mutex);

        if (result_is_error(result)) {
            return result;
        }

        return RESULT_THREAD_BARRIER_SERIAL_CODE;
    }

    /* Wait for generation to change (handles spurious wakeups safely) */
    while (self->native_handle.generation == generation) {
        result = thread_cond_mutex_wait(&self->native_handle.cond, &self->native_handle.mutex);

        if (result_is_error(result)) {
            thread_mutex_unlock(&self->native_handle.mutex);

            return result;
        }
    }

    result = thread_mutex_unlock(&self->native_handle.mutex);

    if (result_is_error(result)) {
        return result;
    }

    return RESULT_SUCCESS;
#else
    I32 const result = pthread_barrier_wait(&self->native_handle);

    /* Normalize POSIX return value */
    if (result == PTHREAD_BARRIER_SERIAL_THREAD) {
        return RESULT_THREAD_BARRIER_SERIAL_CODE;
    }

    if (result != 0) {
        return result_from_os_code((U32) result);
    }

    return RESULT_SUCCESS;
#endif
}

Result thread_barrier_uninit(ThreadBarrier *const self) {
    _thread_check_null(self);

#ifdef OS_WINDOWS
    /* Always run both teardowns; return the first error if either fails. */
    Result const mutex_result = thread_mutex_uninit(&self->native_handle.mutex);
    Result const cond_result  = thread_cond_uninit(&self->native_handle.cond);

    return result_is_error(mutex_result) ? mutex_result : cond_result;
#else
    I32 const error_code = pthread_barrier_destroy(&self->native_handle);

    if (error_code != 0) {
        return result_from_os_code((U32) error_code);
    }

    return RESULT_SUCCESS;
#endif
}

/*==============================================================================
 * MARK: - Condition Variable Functions
 *============================================================================*/

Result thread_cond_init(ThreadCond *const self) {
    _thread_check_null(self);

#ifdef OS_WINDOWS
    InitializeConditionVariable(&self->native_handle);
#else
    I32 const error_code = pthread_cond_init(&self->native_handle, nullptr);

    if (error_code != 0) {
        return result_from_os_code((U32) error_code);
    }
#endif

    return RESULT_SUCCESS;
}

Result thread_cond_mutex_wait(ThreadCond *const self, ThreadMutex *const mutex) {
    _thread_check_null(self);
    _thread_check_null(mutex);

#ifdef OS_WINDOWS
    if (!SleepConditionVariableCS(&self->native_handle, &mutex->native_handle, INFINITE)) {
        return result_from_os();
    }
#else
    I32 const error_code = pthread_cond_wait(&self->native_handle, &mutex->native_handle);

    if (error_code != 0) {
        return result_from_os_code((U32) error_code);
    }
#endif

    return RESULT_SUCCESS;
}

Result thread_cond_rwlock_wait(ThreadCond *const self, ThreadRWLock *const rwlock) {
    _thread_check_null(self);
    _thread_check_null(rwlock);

#ifdef OS_WINDOWS
    if (!SleepConditionVariableSRW(
            &self->native_handle, &rwlock->native_handle,
            INFINITE, CONDITION_VARIABLE_LOCKMODE_SHARED)) {
        return result_from_os();
    }

    return RESULT_SUCCESS;
#else
    // POSIX condition variables can only wait on a mutex, not a read-write lock.
    (void) self;
    (void) rwlock;

    return RESULT_THREAD_UNSUPPORTED_CODE;
#endif
}

Result thread_cond_mutex_timedwait(ThreadCond *const self, ThreadMutex *const mutex, ISize const ms) {
    _thread_check_null(self);
    _thread_check_null(mutex);

#ifdef OS_WINDOWS
    if (!SleepConditionVariableCS(&self->native_handle, &mutex->native_handle, (DWORD) _ms_to_timeout(ms))) {
        if (GetLastError() == ERROR_TIMEOUT) {
            return RESULT_THREAD_TIMEOUT_CODE;
        }

        return result_from_os();
    }
#else
    if (ms < 0) {
        I32 const error_code = pthread_cond_wait(&self->native_handle, &mutex->native_handle);

        if (error_code != 0) {
            return result_from_os_code((U32) error_code);
        }

        return RESULT_SUCCESS;
    }

    struct timespec time = DEFAULT_INITIALIZATION;

    if (!_timespec_conversion(ms, &time)) {
        // clock_gettime failed (extremely rare); fall back to an infinite wait
        I32 const error_code = pthread_cond_wait(&self->native_handle, &mutex->native_handle);

        if (error_code != 0) {
            return result_from_os_code((U32) error_code);
        }

        return RESULT_SUCCESS;
    }

    I32 const error_code = pthread_cond_timedwait(&self->native_handle, &mutex->native_handle, &time);

    // The named code, not result_from_os_code(ETIMEDOUT): the classifier would
    // file a cond timeout under NETWORK, and the named value is byte-identical
    // to what the Windows branch returns, so callers can compare with ==.
    if (error_code == ETIMEDOUT) {
        return RESULT_THREAD_TIMEOUT_CODE;
    }

    if (error_code != 0) {
        return result_from_os_code((U32) error_code);
    }
#endif

    return RESULT_SUCCESS;
}

Result thread_cond_rwlock_timedwait(ThreadCond *const self, ThreadRWLock *const rwlock, ISize const ms) {
    _thread_check_null(self);
    _thread_check_null(rwlock);

#ifdef OS_WINDOWS
    if (!SleepConditionVariableSRW(
            &self->native_handle, &rwlock->native_handle,
            (DWORD) _ms_to_timeout(ms), CONDITION_VARIABLE_LOCKMODE_SHARED)) {
        if (GetLastError() == ERROR_TIMEOUT) {
            return RESULT_THREAD_TIMEOUT_CODE;
        }

        return result_from_os();
    }

    return RESULT_SUCCESS;
#else
    // POSIX condition variables can only wait on a mutex, not a read-write lock.
    (void) self;
    (void) rwlock;
    (void) ms;

    return RESULT_THREAD_UNSUPPORTED_CODE;
#endif
}

Result thread_cond_wake(ThreadCond *const self) {
    _thread_check_null(self);

#ifdef OS_WINDOWS
    WakeConditionVariable(&self->native_handle);
#else
    I32 const error_code = pthread_cond_signal(&self->native_handle);

    if (error_code != 0) {
        return result_from_os_code((U32) error_code);
    }
#endif

    return RESULT_SUCCESS;
}

Result thread_cond_wake_all(ThreadCond *const self) {
    _thread_check_null(self);

#ifdef OS_WINDOWS
    WakeAllConditionVariable(&self->native_handle);
#else
    I32 const error_code = pthread_cond_broadcast(&self->native_handle);

    if (error_code != 0) {
        return result_from_os_code((U32) error_code);
    }
#endif

    return RESULT_SUCCESS;
}

Result thread_cond_uninit(ThreadCond *const self) {
    _thread_check_null(self);

    // Windows CONDITION_VARIABLE doesn't need destruction; POSIX requires it.
#ifndef OS_WINDOWS
    I32 const error_code = pthread_cond_destroy(&self->native_handle);

    if (error_code != 0) {
        return result_from_os_code((U32) error_code);
    }
#endif

    return RESULT_SUCCESS;
}

/*==============================================================================
 * MARK: - Mutex Functions
 *============================================================================*/

Result thread_mutex_init(ThreadMutex *const self) {
    _thread_check_null(self);

#ifdef OS_WINDOWS
    InitializeCriticalSection(&self->native_handle);
#else
    I32 const error_code = pthread_mutex_init(&self->native_handle, nullptr);

    if (error_code != 0) {
        return result_from_os_code((U32) error_code);
    }
#endif

    return RESULT_SUCCESS;
}

Result thread_mutex_lock(ThreadMutex *const self) {
    _thread_check_null(self);

#ifdef OS_WINDOWS
    EnterCriticalSection(&self->native_handle);
#else
    I32 const error_code = pthread_mutex_lock(&self->native_handle);

    if (error_code != 0) {
        return result_from_os_code((U32) error_code);
    }
#endif

    return RESULT_SUCCESS;
}

Result thread_mutex_lock_try(ThreadMutex *const self) {
    _thread_check_null(self);

#ifdef OS_WINDOWS
    if (!TryEnterCriticalSection(&self->native_handle)) {
        return result_make(RESULT_CATEGORY_STATE, 0, RESULT_FLAG_RETRYABLE);
    }
#else
    I32 const error_code = pthread_mutex_trylock(&self->native_handle);

    // EBUSY (held elsewhere) mirrors the Windows branch's return exactly, so
    // cross-platform callers see a single CROSS-THREAD contention contract.
    // Same-thread relocking stays platform-divergent (see ThreadMutex's doc).
    if (error_code == EBUSY) {
        return result_make(RESULT_CATEGORY_STATE, 0, RESULT_FLAG_RETRYABLE);
    }

    if (error_code != 0) {
        return result_from_os_code((U32) error_code);
    }
#endif

    return RESULT_SUCCESS;
}

Result thread_mutex_unlock(ThreadMutex *const self) {
    _thread_check_null(self);

#ifdef OS_WINDOWS
    LeaveCriticalSection(&self->native_handle);
#else
    I32 const error_code = pthread_mutex_unlock(&self->native_handle);

    if (error_code != 0) {
        return result_from_os_code((U32) error_code);
    }
#endif

    return RESULT_SUCCESS;
}

Result thread_mutex_uninit(ThreadMutex *const self) {
    _thread_check_null(self);

#ifdef OS_WINDOWS
    DeleteCriticalSection(&self->native_handle);
#else
    I32 const error_code = pthread_mutex_destroy(&self->native_handle);

    if (error_code != 0) {
        return result_from_os_code((U32) error_code);
    }
#endif

    return RESULT_SUCCESS;
}

/*==============================================================================
 * MARK: - Read-Write Lock Functions
 *============================================================================*/

Result thread_rwlock_init(ThreadRWLock *const self) {
    _thread_check_null(self);

#ifdef OS_WINDOWS
    InitializeSRWLock(&self->native_handle);
#else
    I32 const error_code = pthread_rwlock_init(&self->native_handle, nullptr);

    if (error_code != 0) {
        return result_from_os_code((U32) error_code);
    }
#endif

    return RESULT_SUCCESS;
}

Result thread_rwlock_read_lock(ThreadRWLock *const self) {
    _thread_check_null(self);

#ifdef OS_WINDOWS
    AcquireSRWLockShared(&self->native_handle);
#else
    I32 const error_code = pthread_rwlock_rdlock(&self->native_handle);

    if (error_code != 0) {
        return result_from_os_code((U32) error_code);
    }
#endif

    return RESULT_SUCCESS;
}

Result thread_rwlock_read_lock_try(ThreadRWLock *const self) {
    _thread_check_null(self);

#ifdef OS_WINDOWS
    if (!TryAcquireSRWLockShared(&self->native_handle)) {
        return result_make(RESULT_CATEGORY_STATE, 0, RESULT_FLAG_RETRYABLE);
    }
#else
    I32 const error_code = pthread_rwlock_tryrdlock(&self->native_handle);

    // EBUSY parity with the Windows branch, as in thread_mutex_lock_try.
    // EAGAIN (max concurrent readers) joins it: saturation is contention too.
    if (error_code == EBUSY || error_code == EAGAIN) {
        return result_make(RESULT_CATEGORY_STATE, 0, RESULT_FLAG_RETRYABLE);
    }

    if (error_code != 0) {
        return result_from_os_code((U32) error_code);
    }
#endif

    return RESULT_SUCCESS;
}

Result thread_rwlock_write_lock(ThreadRWLock *const self) {
    _thread_check_null(self);

#ifdef OS_WINDOWS
    AcquireSRWLockExclusive(&self->native_handle);
#else
    I32 const error_code = pthread_rwlock_wrlock(&self->native_handle);

    if (error_code != 0) {
        return result_from_os_code((U32) error_code);
    }
#endif

    return RESULT_SUCCESS;
}

Result thread_rwlock_write_lock_try(ThreadRWLock *const self) {
    _thread_check_null(self);

#ifdef OS_WINDOWS
    if (!TryAcquireSRWLockExclusive(&self->native_handle)) {
        return result_make(RESULT_CATEGORY_STATE, 0, RESULT_FLAG_RETRYABLE);
    }
#else
    I32 const error_code = pthread_rwlock_trywrlock(&self->native_handle);

    // EBUSY parity with the Windows branch, as in thread_mutex_lock_try.
    if (error_code == EBUSY) {
        return result_make(RESULT_CATEGORY_STATE, 0, RESULT_FLAG_RETRYABLE);
    }

    if (error_code != 0) {
        return result_from_os_code((U32) error_code);
    }
#endif

    return RESULT_SUCCESS;
}

Result thread_rwlock_read_unlock(ThreadRWLock *const self) {
    _thread_check_null(self);

#ifdef OS_WINDOWS
    ReleaseSRWLockShared(&self->native_handle);
#else
    I32 const error_code = pthread_rwlock_unlock(&self->native_handle);

    if (error_code != 0) {
        return result_from_os_code((U32) error_code);
    }
#endif

    return RESULT_SUCCESS;
}

Result thread_rwlock_write_unlock(ThreadRWLock *const self) {
    _thread_check_null(self);

#ifdef OS_WINDOWS
    ReleaseSRWLockExclusive(&self->native_handle);
#else
    I32 const error_code = pthread_rwlock_unlock(&self->native_handle);

    if (error_code != 0) {
        return result_from_os_code((U32) error_code);
    }
#endif

    return RESULT_SUCCESS;
}

Result thread_rwlock_uninit(ThreadRWLock *const self) {
    _thread_check_null(self);

    // Windows SRWLOCK doesn't need destruction; POSIX requires it.
#ifndef OS_WINDOWS
    I32 const error_code = pthread_rwlock_destroy(&self->native_handle);

    if (error_code != 0) {
        return result_from_os_code((U32) error_code);
    }
#endif

    return RESULT_SUCCESS;
}

/*==============================================================================
 * MARK: - Thread Attribute Functions
 *============================================================================*/

Result thread_attr_init_1(ThreadAttr *const self) {
    _thread_check_null(self);

#ifdef OS_WINDOWS
    self->native_handle.stack_size = 0;
    self->native_handle.detach     = false;
#else
    I32 const error_code = pthread_attr_init(&self->native_handle);

    if (error_code != 0) {
        return result_from_os_code((U32) error_code);
    }
#endif

    return RESULT_SUCCESS;
}

Result thread_attr_init_2(ThreadAttr *const self, ISize const stack_size) {
    _thread_check_null(self);

    Result result = thread_attr_init_1(self);

    if (result_is_error(result)) {
        return result;
    }

    result = thread_attr_set_stacksize(self, stack_size);

    if (result_is_error(result)) {
        thread_attr_uninit(self);
    }

    return result;
}

Result thread_attr_init_3(ThreadAttr *const self, bool const detach) {
    _thread_check_null(self);

    Result result = thread_attr_init_1(self);

    if (result_is_error(result)) {
        return result;
    }

    result = thread_attr_set_detach(self, detach);

    if (result_is_error(result)) {
        thread_attr_uninit(self);
    }

    return result;
}

Result thread_attr_init_4(ThreadAttr *const self, bool const detach, ISize const stack_size) {
    _thread_check_null(self);

    Result result = thread_attr_init_1(self);

    if (result_is_error(result)) {
        return result;
    }

    result = thread_attr_set_detach(self, detach);

    if (result_is_error(result)) {
        thread_attr_uninit(self);

        return result;
    }

    result = thread_attr_set_stacksize(self, stack_size);

    if (result_is_error(result)) {
        thread_attr_uninit(self);
    }

    return result;
}

Result thread_attr_set_stacksize(ThreadAttr *const self, ISize const stack_size) {
    _thread_check_null(self);

#ifdef OS_WINDOWS
    if (stack_size < 0) {
        return result_make(RESULT_CATEGORY_ARGUMENT, 0, RESULT_FLAG_RETRYABLE);
    }

    self->native_handle.stack_size = stack_size;
#else
    if (stack_size < PTHREAD_STACK_MIN) {
        return result_make(RESULT_CATEGORY_ARGUMENT, 0, RESULT_FLAG_RETRYABLE);
    }

    I32 const error_code = pthread_attr_setstacksize(&self->native_handle, stack_size);

    if (error_code != 0) {
        return result_from_os_code((U32) error_code);
    }
#endif

    return RESULT_SUCCESS;
}

Result thread_attr_set_detach(ThreadAttr *const self, bool const detach) {
    _thread_check_null(self);

#ifdef OS_WINDOWS
    self->native_handle.detach = detach;
#else
    I32 const state = detach ? PTHREAD_CREATE_DETACHED : PTHREAD_CREATE_JOINABLE;

    I32 const error_code = pthread_attr_setdetachstate(&self->native_handle, state);

    if (error_code != 0) {
        return result_from_os_code((U32) error_code);
    }
#endif

    return RESULT_SUCCESS;
}

Result thread_attr_uninit(ThreadAttr *const self) {
    _thread_check_null(self);

#ifndef OS_WINDOWS
    I32 const error_code = pthread_attr_destroy(&self->native_handle);

    if (error_code != 0) {
        return result_from_os_code((U32) error_code);
    }
#endif

    return RESULT_SUCCESS;
}

/*==============================================================================
 * MARK: - Thread Management Functions
 *============================================================================*/

Result thread_create_1(Thread *const self, FpThreadCallback const cb, void *const data) {
    return thread_create_2(self, nullptr, cb, data);
}

Result thread_create_2(Thread *const self, ThreadAttr const *const attr, FpThreadCallback const cb, void *const data) {
    _thread_check_null(self);
    _thread_check_null((void*) cb);

#ifdef OS_WINDOWS
    if (attr != nullptr && attr->native_handle.stack_size < 0) {
        return result_make(RESULT_CATEGORY_ARGUMENT, 0, RESULT_FLAG_RETRYABLE);
    }

    ThreadArgs *const args = (ThreadArgs*) malloc(sizeof(ThreadArgs));

    if (args == nullptr) {
        return result_make(RESULT_CATEGORY_MEMORY, 0, RESULT_FLAG_CRITICAL);
    }

    args->cb   = cb;
    args->data = data;

    USize const stack_size = (attr == nullptr) ? 0 : (USize) attr->native_handle.stack_size;

    self->native_handle = CreateThread(nullptr, (SIZE_T) stack_size, _thread_callback_wrapper, args, 0, nullptr);

    if (self->native_handle == nullptr) {
        free(args);

        return result_from_os();
    }

    if (attr != nullptr && attr->native_handle.detach) {
        // On Windows, detaching is CloseHandle: it both releases our handle and
        // performs the detach, so propagate its result directly (no leak).
        return thread_detach(self);
    }

    return RESULT_SUCCESS;
#else
    ThreadArgs *const args = (ThreadArgs*) malloc(sizeof(ThreadArgs));

    if (args == nullptr) {
        return result_make(RESULT_CATEGORY_MEMORY, 0, RESULT_FLAG_CRITICAL);
    }

    args->cb   = cb;
    args->data = data;

    if (attr == nullptr) {
        I32 const error_code = pthread_create(&self->native_handle, nullptr, _thread_callback_wrapper, args);

        if (error_code != 0) {
            free(args);

            return result_from_os_code((U32) error_code);
        }

        return RESULT_SUCCESS;
    }

    I32 const error_code = pthread_create(&self->native_handle, &attr->native_handle, _thread_callback_wrapper, args);

    if (error_code != 0) {
        free(args);

        return result_from_os_code((U32) error_code);
    }

    // The caller retains ownership of attr and is responsible for uninit'ing it.
    return RESULT_SUCCESS;
#endif
}

Result thread_join_1(Thread *const self) {
    return thread_join_2(self, nullptr);
}

Result thread_join_2(Thread *const self, I32 *const code) {
    _thread_check_null(self);

#ifdef OS_WINDOWS
    if (self->native_handle == nullptr) {
        return result_make(RESULT_CATEGORY_STATE, 0, RESULT_FLAG_CRITICAL);
    }

    if (WaitForSingleObject(self->native_handle, INFINITE) == WAIT_FAILED) {
        Result const result = result_from_os();

        CloseHandle(self->native_handle);
        self->native_handle = nullptr;

        return result;
    }

    if (code != nullptr) {
        U32 exit_code = 0;

        if (!GetExitCodeThread(self->native_handle, (LPDWORD) &exit_code)) {
            Result const result = result_from_os();

            CloseHandle(self->native_handle);
            self->native_handle = nullptr;

            return result;
        }

        *code = (I32) exit_code;
    }

    if (!CloseHandle(self->native_handle)) {
        Result const result = result_from_os();

        self->native_handle = nullptr;

        return result;
    }

    self->native_handle = nullptr;
#else
    void *retval = nullptr;

    I32 const error_code = pthread_join(self->native_handle, code != nullptr ? &retval : nullptr);

    if (error_code != 0) {
        return result_from_os_code((U32) error_code);
    }

    if (code != nullptr) {
        *code = (I32) (ISize) retval;
    }
#endif

    return RESULT_SUCCESS;
}

Result thread_detach(Thread *const self) {
    _thread_check_null(self);

#ifdef OS_WINDOWS
    if (self->native_handle == nullptr) {
        return result_make(RESULT_CATEGORY_STATE, 0, RESULT_FLAG_CRITICAL);
    }

    if (!CloseHandle(self->native_handle)) {
        Result const result = result_from_os();

        self->native_handle = nullptr;

        return result;
    }

    self->native_handle = nullptr;
#else
    I32 const error_code = pthread_detach(self->native_handle);

    if (error_code != 0) {
        return result_from_os_code((U32) error_code);
    }
#endif

    return RESULT_SUCCESS;
}

ThreadId thread_get_id_1(void) {
#ifdef OS_WINDOWS
    return GetCurrentThreadId();
#else
    return pthread_self();
#endif
}

ThreadId thread_get_id_2(Thread const *const self) {
    if (self == nullptr) {
        return 0;
    }

#ifdef OS_WINDOWS
    if (self->native_handle == nullptr) {
        return 0;
    }

    return GetThreadId(self->native_handle);
#else
    return self->native_handle;
#endif
}

bool thread_equal(Thread const *const self, Thread const *const source) {
    if (self == nullptr || source == nullptr) {
        return false;
    }

#ifdef OS_WINDOWS
    if (self->native_handle == nullptr || source->native_handle == nullptr) {
        return false;
    }

    return GetThreadId(self->native_handle) == GetThreadId(source->native_handle);
#else
    return pthread_equal(self->native_handle, source->native_handle) != 0;
#endif
}

bool thread_yield(void) {
#ifdef OS_WINDOWS
    return SwitchToThread();
#else
    return sched_yield() == 0;
#endif
}

void thread_exit(I32 const result) {
#ifdef OS_WINDOWS
    ExitThread((DWORD) result);
#else
    pthread_exit((void*) (ISize) result);
#endif
}

void thread_sleep(USize const ms) {
#ifdef OS_WINDOWS
    U32 const timeout = ms > 0xFFFFFFFE ? 0xFFFFFFFE : (U32) ms;

    Sleep((DWORD) timeout);
#else
    struct timespec ts = DEFAULT_INITIALIZATION;

    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;

    // nanosleep can be interrupted by signals (EINTR)
    // Loop until it succeeds or fails with a non-interrupt error
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
        // ts now contains remaining time; retry
    }
#endif
}