/*
 * result.h - Packed status/error code type for CFW
 *
 * Features:
 *   - Result: a C23 U32-backed enum packing behavioral flags (bits 0-7), an
 *     error category (bits 8-15), and an OS/library code (bits 16-31), with
 *     named masks/shifts for every field
 *   - Zero-cast construction (result_make) and extraction (result_code,
 *     result_category, result_flags)
 *   - Flag predicates (result_is_critical/retryable/partial/transient/deferred)
 *     and non-mutating flag setters (result_with_flag, result_clear_flag),
 *     CFW_ATTR_NODISCARD so a discarded return value is a compile diagnostic
 *   - Cross-platform OS error classification: result_from_os_code maps a code
 *     you already hold (e.g. a pthread-style return value); result_from_os
 *     captures the thread-local OS error state (GetLastError/errno) and
 *     delegates to it
 *   - Logging helpers: result_category_name plus result_flags_format
 *     ("CRITICAL|RETRYABLE" into a caller buffer)
 *
 * Usage Examples:
 *   @code
 *   #include <result.h>
 *   Result const r = result_from_os();
 *   if (result_is_error(r)) {
 *       char flags[RESULT_FLAGS_STRING_SIZE];
 *       log_error("%s code=%u flags=%s", result_category_name(r),
 *           (U32) result_code(r), result_flags_format(r, flags, sizeof(flags)));
 *   }
 *   @endcode
 *
 * Error Handling:
 *   - The type IS the error channel; functions return a Result by value
 *   - Under ERROR_CHECK_ENABLED, result_make aborts on malformed inputs (see
 *     its comment); release builds stay branch-free
 *
 * Thread Safety:
 *   - All functions are reentrant; result_from_os reads thread-local OS error
 *     state (GetLastError/errno)
 *
 * Memory Management:
 *   - Not applicable (no allocation; result_flags_format writes only into the
 *     caller's buffer)
 *
 * Performance Characteristics:
 *   - All operations are O(1) inline bit arithmetic; result_flags_format is
 *     O(RESULT_FLAGS_STRING_SIZE)
 *
 * Dependencies:
 *   - <types.h>; Windows: <platform/windows/windows.h>, <winerror.h>;
 *     POSIX: <errno.h>; ERROR_CHECK_ENABLED builds: <stdlib.h>
 *
 * This is a header-only module; there is no result.c.
 */

#ifndef RESULT_H
#define RESULT_H

#include <types.h>

// <types.h> must precede this block: it defines OS_WINDOWS (from _WIN32),
// which gates the platform error headers below.
#ifdef OS_WINDOWS
#include <platform/windows/windows.h>
#include <winerror.h>
#else
#include <errno.h>
#endif

#ifdef ERROR_CHECK_ENABLED
#include <stdlib.h> // abort() for the result_make argument checks
#endif

/*
 * C23: Underlying type is explicitly U32.
 * All enum constants and operations are unsigned by default.
 * Layout: [Flags: 0-7] [Category: 8-15] [Code: 16-31]
 *
 * INVARIANT: a nonzero Result always carries a category — flags never travel
 * alone (result_make checks this under ERROR_CHECK_ENABLED).
 *
 * A Result routinely holds values outside the enumerator list (any
 * category | code | flags combination); with the fixed U32 underlying type
 * that is legal, well-defined C23 — not UB.
 *
 * The code field is 16 bits, so the OS classifiers are lossy: codes >= 2^16
 * truncate into the low 16 bits. GetLastError/errno values are in practice
 * always < 2^16, but any larger code collapses. NEVER feed an HRESULT through
 * the code path: its facility/severity bits live in the high half, so
 * truncation makes distinct HRESULTs ambiguous. (GetLastError never returns
 * HRESULTs; this matters only for future library integrations.)
 */
typedef enum Result : U32 {
    RESULT_SUCCESS = 0,

    /* Behavioral Flags (Bits 0-7) */
    RESULT_FLAG_CRITICAL  = 1U << 0,  /* Unrecoverable, must abort */
    RESULT_FLAG_DEFERRED  = 1U << 4,  /* Reported asynchronously */
    RESULT_FLAG_PARTIAL   = 1U << 2,  /* Operation partially succeeded */
    RESULT_FLAG_RETRYABLE = 1U << 1,  /* Transient, safe to retry */
    RESULT_FLAG_TRANSIENT = 1U << 3,  /* Temporary condition */

    /* Error Categories (Bits 8-15) */
    RESULT_CATEGORY_APPLICATION = 7U << 8,
    RESULT_CATEGORY_ARGUMENT    = 4U << 8,
    RESULT_CATEGORY_IO          = 3U << 8,
    RESULT_CATEGORY_LIBRARY     = 6U << 8,
    RESULT_CATEGORY_MEMORY      = 2U << 8,
    RESULT_CATEGORY_NETWORK     = 8U << 8,
    RESULT_CATEGORY_STATE       = 5U << 8,
    RESULT_CATEGORY_SYSTEM      = 1U << 8
} Result;

/* One definition of the field layout; result_make/result_code/result_flags
 * consume these, and external code (serializers, loggers) should too. */
#define RESULT_CATEGORY_MASK 0x0000FF00U
#define RESULT_CODE_MASK 0xFFFF0000U
#define RESULT_CODE_SHIFT 16
/* Sized for all five flag names plus separators and NUL:
 * "CRITICAL|RETRYABLE|PARTIAL|TRANSIENT|DEFERRED" = 8+9+7+9+8 + 4 + 1 = 46. */
#define RESULT_FLAGS_STRING_SIZE 46
#define RESULT_FLAG_MASK 0x000000FFU

/*==============================================================================
 * MARK: - Construction & Extraction
 *============================================================================*/

/* `category` is a pre-shifted RESULT_CATEGORY_* mask (bits 8-15), so it is OR'd
 * in directly; `code` is masked to 16 bits and shifted into bits 16-31.
 * ERROR_CHECK_ENABLED aborts on: an un-shifted/garbage category, flags outside
 * bits 0-7 (they would corrupt the category field), or flags without a category
 * (the flags-never-travel-alone invariant). The checks are self-contained
 * abort() calls with no log line: the house error_check_* helpers chain
 * tracelog -> log -> thread, and thread.h includes this header, so using them
 * here would be a circular dependency. */
CFW_ATTR_NODISCARD
static inline CFW_ATTR_CONST Result result_make(U32 const category, U32 const code, U32 const flags) {
#ifdef ERROR_CHECK_ENABLED
    if ((category & ~((U32) RESULT_CATEGORY_MASK)) != 0) {
        abort(); /* category is not a pre-shifted RESULT_CATEGORY_* value */
    }
    if (flags > RESULT_FLAG_MASK) {
        abort(); /* flags spill out of bits 0-7 */
    }
    if ((flags != 0 || code != 0) && category == 0) {
        abort(); /* a nonzero Result (flags or code) needs a category */
    }
#endif
    return (Result) (category | ((code << RESULT_CODE_SHIFT) & RESULT_CODE_MASK) | flags);
}

CFW_ATTR_CONST static inline U16 result_code(Result const result) {
    return (U16) (result >> RESULT_CODE_SHIFT);
}

/* Returns the pre-shifted category field (bits 8-15), comparable directly
 * against the RESULT_CATEGORY_* constants. */
CFW_ATTR_CONST static inline Result result_category(Result const result) {
    return (Result) (result & RESULT_CATEGORY_MASK);
}

CFW_ATTR_CONST static inline U8 result_flags(Result const result) {
    return (U8) (result & RESULT_FLAG_MASK);
}

/*==============================================================================
 * MARK: - State & Flag Checks
 *============================================================================*/

CFW_ATTR_CONST static inline bool result_is_success(Result const result) { return result == RESULT_SUCCESS; }
CFW_ATTR_CONST static inline bool result_is_error(Result const result)   { return result != RESULT_SUCCESS; }

/* Assumes single-bit RESULT_FLAG_* values: returns true if ANY bit of `flag`
 * is set in `result`, not whether all of them are. */
CFW_ATTR_CONST static inline bool result_has_flag(Result const result, Result const flag) { return (result & flag) != 0; }
CFW_ATTR_NODISCARD
static inline CFW_ATTR_CONST Result result_with_flag(Result const result, Result const flag)  { return (Result) (result | flag); }
CFW_ATTR_NODISCARD
static inline CFW_ATTR_CONST Result result_clear_flag(Result const result, Result const flag) { return (Result) (result & ~flag); }

CFW_ATTR_CONST static inline bool result_is_critical(Result const result)   { return result_has_flag(result, RESULT_FLAG_CRITICAL); }
CFW_ATTR_CONST static inline bool result_is_retryable(Result const result)  { return result_has_flag(result, RESULT_FLAG_RETRYABLE); }
CFW_ATTR_CONST static inline bool result_is_partial(Result const result)    { return result_has_flag(result, RESULT_FLAG_PARTIAL); }
CFW_ATTR_CONST static inline bool result_is_transient(Result const result)  { return result_has_flag(result, RESULT_FLAG_TRANSIENT); }
CFW_ATTR_CONST static inline bool result_is_deferred(Result const result)   { return result_has_flag(result, RESULT_FLAG_DEFERRED); }

/*==============================================================================
 * MARK: - OS Integration
 * result_from_os_code classifies an OS error code the caller already holds
 * (e.g. a pthread-style return value, which never touches errno);
 * result_from_os captures the current thread-local OS error state and
 * delegates to it.
 *============================================================================*/

CFW_ATTR_NODISCARD
static inline CFW_ATTR_CONST Result result_from_os_code(U32 const error) {
#ifdef OS_WINDOWS
    if (error == ERROR_SUCCESS) {
        return RESULT_SUCCESS;
    }

    U32 category = RESULT_CATEGORY_SYSTEM;
    U32 flags    = 0;

    /* WSA* codes are matched in this GetLastError-shaped switch because on
     * modern Windows WSAGetLastError() is equivalent to GetLastError() —
     * socket failures arrive through the same channel. */
    switch (error) {
        case ERROR_OUTOFMEMORY:
        case ERROR_NOT_ENOUGH_QUOTA:
        case ERROR_BAD_FORMAT:
        case ERROR_INVALID_HANDLE:
            flags = RESULT_FLAG_CRITICAL;
            break;
        case WSAEWOULDBLOCK: /* 10035 */
        case WSAETIMEDOUT:   /* 10060 */
            category = RESULT_CATEGORY_NETWORK;
            flags    = RESULT_FLAG_RETRYABLE | RESULT_FLAG_TRANSIENT;
            break;
        case ERROR_IO_PENDING: /* overlapped I/O in progress: wait, don't re-issue */
            flags = RESULT_FLAG_DEFERRED | RESULT_FLAG_TRANSIENT;
            break;
        case ERROR_TIMEOUT:
            flags = RESULT_FLAG_RETRYABLE | RESULT_FLAG_TRANSIENT;
            break;
        case ERROR_HANDLE_EOF: /* stable state: retrying a read at EOF yields EOF forever */
            category = RESULT_CATEGORY_IO;
            break;
        case ERROR_ACCESS_DENIED: /* ordinary runtime condition, not critical */
            break;
        default:
            break;
    }

    return result_make(category, error, flags); /* code truncated to 16 bits */

#else /* POSIX / Unix-like */
    if (error == 0) {
        return RESULT_SUCCESS;
    }

    U32 category = RESULT_CATEGORY_SYSTEM;
    U32 flags    = 0;

    switch (error) {
        case ENOMEM:
        case ENOSYS:
        case EFAULT:
            flags = RESULT_FLAG_CRITICAL;
            break;
        case EPERM:  /* ordinary runtime conditions (skip, prompt, degrade) — */
        case EACCES: /* not critical */
            break;
        case EAGAIN:
#if EWOULDBLOCK != EAGAIN
        case EWOULDBLOCK:
#endif
        case EINTR:
        case EMFILE: /* fd exhaustion is transient under load */
        case ENFILE:
            flags = RESULT_FLAG_RETRYABLE | RESULT_FLAG_TRANSIENT;
            break;
        /* Socket-only errnos classify as NETWORK, mirroring the Windows WSA
         * group, so category checks behave identically across platforms. */
        case ECONNRESET:   /* connection failures: retryable, but not a */
        case ECONNREFUSED: /* "temporary condition" in the TRANSIENT sense */
        case EHOSTUNREACH:
        case ENETUNREACH:
        case ENOTCONN:
        case EPIPE:
            category = RESULT_CATEGORY_NETWORK;
            flags    = RESULT_FLAG_RETRYABLE;
            break;
        case ETIMEDOUT:
            category = RESULT_CATEGORY_NETWORK;
            flags    = RESULT_FLAG_RETRYABLE | RESULT_FLAG_TRANSIENT;
            break;
        case ENOSPC:
        case EIO:
            category = RESULT_CATEGORY_IO;
            break;
        default:
            break;
    }

    /* errno is positive per POSIX, so the 16-bit truncation never wraps a sign. */
    return result_make(category, error, flags);
#endif
}

CFW_ATTR_NODISCARD
static inline Result result_from_os(void) {
#ifdef OS_WINDOWS
    return result_from_os_code(GetLastError());
#else
    return result_from_os_code((U32) errno);
#endif
}

/*==============================================================================
 * MARK: - Debug / Logging Helpers
 *============================================================================*/

CFW_ATTR_CONST static inline char const* result_category_name(Result const result) {
    switch (result_category(result)) {
        case RESULT_SUCCESS:              return "SUCCESS";
        case RESULT_CATEGORY_SYSTEM:      return "SYSTEM";
        case RESULT_CATEGORY_MEMORY:      return "MEMORY";
        case RESULT_CATEGORY_IO:          return "IO";
        case RESULT_CATEGORY_ARGUMENT:    return "ARGUMENT";
        case RESULT_CATEGORY_STATE:       return "STATE";
        case RESULT_CATEGORY_LIBRARY:     return "LIBRARY";
        case RESULT_CATEGORY_APPLICATION: return "APPLICATION";
        case RESULT_CATEGORY_NETWORK:     return "NETWORK";
        default:                          return "UNKNOWN";
    }
}

/* Formats the set behavioral flags as "CRITICAL|RETRYABLE" into `buffer` and
 * returns it ("NONE" when no flag is set). Size the buffer with
 * RESULT_FLAGS_STRING_SIZE to fit every combination; a smaller buffer
 * truncates (always NUL-terminated). Returns "" without touching `buffer`
 * when it is NULL or `size` is 0. */
static inline char const* result_flags_format(Result const result, char *const buffer, USize const size) {
    if (buffer == NULL || size == 0) {
        return "";
    }

    struct { U32 flag; char const *name; } const flag_names[] = {
        { RESULT_FLAG_CRITICAL,  "CRITICAL"  },
        { RESULT_FLAG_RETRYABLE, "RETRYABLE" },
        { RESULT_FLAG_PARTIAL,   "PARTIAL"   },
        { RESULT_FLAG_TRANSIENT, "TRANSIENT" },
        { RESULT_FLAG_DEFERRED,  "DEFERRED"  },
    };
    USize at = 0;

    for (USize i = 0; i < sizeof(flag_names) / sizeof(flag_names[0]); ++i) {
        if ((result & flag_names[i].flag) == 0) {
            continue;
        }
        if (at > 0 && at < size - 1) {
            buffer[at++] = '|';
        }
        for (char const *ch = flag_names[i].name; *ch != '\0' && at < size - 1; ++ch) {
            buffer[at++] = *ch;
        }
    }
    if (at == 0) {
        for (char const *ch = "NONE"; *ch != '\0' && at < size - 1; ++ch) {
            buffer[at++] = *ch;
        }
    }
    buffer[at] = '\0';

    return buffer;
}

#endif /* RESULT_H */