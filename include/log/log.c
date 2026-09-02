/*
 * log.c - Implementation of centralized logging and debug tracing for the C Libraries Framework
 *
 * Features implemented:
 *   - Per-thread logging via thread_local Log _log; a shared _log_default (protected by
 *     _log_default_mutex) is copied to each thread's _log on first use
 *   - Per-message level gate: messages more verbose than the threshold return before any I/O
 *   - Metadata-rich logging (timestamp, thread ID, file, line, function)
 *   - Buffer-based logging with safe vsnprintf write clamping (no overflow, no UB on negative return)
 *   - Timestamp caching: strftime is only called when the epoch second changes
 *   - Autoflush toggle: fflush after every write (default true); disable for high-throughput paths
 *   - Thread-safe localtime via localtime_s (Windows) / localtime_r (POSIX)
 *
 * Thread Safety:
 *   - Each thread's _log is thread_local; per-thread log ops need no synchronization.
 *   - _log_default is protected by _log_default_mutex. log_init MUST be called from the main
 *     thread before spawning workers - this is the module contract, not a run-time enforcement.
 *
 * See log.h for API documentation and usage examples.
 */

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/
#include <log/log.h>

/*==============================================================================
 * MARK: - Type Definitions
 *============================================================================*/
typedef struct {
    bool     autoflush;
    char     *buffer;
    USize    buffer_capacity;
    USize    buffer_size;
    USize    debug_step_counter;
    bool     initialized;
    LogLevel level;
    FILE     *stream;
    bool     timestamp_enabled;
} Log;

/*==============================================================================
 * MARK: - Macros and Constants (alphabetized)
 *============================================================================*/
#define _LOG_LINE_BUFFER_SIZE 8192
#define _LOG_LINE_TRUNCATED_MARKER "...[truncated]\n"
#define _LOG_TIMESTAMP_BUFFER_SIZE 32

/*==============================================================================
 * MARK: - Static/Internal Variables
 *============================================================================*/
#ifdef LOG_THREAD_IMPLEMENTATION
static thread_local Log    _log                = DEFAULT_INITIALIZATION;
static thread_local time_t _log_cached_time    = 0;
static thread_local char   _log_cached_time_buffer[_LOG_TIMESTAMP_BUFFER_SIZE] = DEFAULT_INITIALIZATION;
static thread_local char   _log_line_buffer[_LOG_LINE_BUFFER_SIZE] = DEFAULT_INITIALIZATION;
static thread_local USize  _log_line_size      = 0;
static thread_local bool   _log_line_truncated = false;

static          Log         _log_default                    = DEFAULT_INITIALIZATION;
static          ThreadMutex _log_default_mutex              = DEFAULT_INITIALIZATION;
static          bool        _log_default_mutex_initialized  = false;  // plain bool - log_init is single-threaded
static _Atomic  bool        _log_default_ready              = false;       // cross-thread signal
#else
static Log      _log                        = DEFAULT_INITIALIZATION;
static time_t   _log_cached_time            = 0;
static char     _log_cached_time_buffer[_LOG_TIMESTAMP_BUFFER_SIZE] = DEFAULT_INITIALIZATION;
static char     _log_line_buffer[_LOG_LINE_BUFFER_SIZE] = DEFAULT_INITIALIZATION;
static USize    _log_line_size              = 0;
static bool     _log_line_truncated         = false;
#endif // LOG_THREAD_IMPLEMENTATION

/*==============================================================================
 * MARK: - Static/Internal Functions
 *============================================================================*/
// One log line used to be up to six separate stream writes (header, metadata,
// payload fragments), and stdio locks per CALL, not per line - so concurrent
// threads sheared each other's lines mid-message exactly when a loaded server
// most needed readable diagnostics. Every emitter now assembles its full line
// (or debug block) here and commits it with a single fwrite; per-call stdio
// locking then gives whole-line atomicity for free. Output past
// _LOG_LINE_BUFFER_SIZE truncates (vsnprintf semantics) and the committed line
// ends with _LOG_LINE_TRUNCATED_MARKER so the cut is visible - the cap is a
// stated Limit in log.h, not a corruption.
static void _log_line_reset(void) {
    _log_line_size      = 0;
    _log_line_truncated = false;
    _log_line_buffer[0] = '\0';
}

CFW_ATTR_PRINTF(1, 0)
static void _log_line_vaddf(char const *const fmt, va_list args) {
    // A segment arriving after the buffer filled exactly is dropped whole - that
    // is a cut too (log_print's trailing newline, say), so the marker must say so.
    if (_log_line_size >= _LOG_LINE_BUFFER_SIZE - 1) {
        _log_line_truncated = true;

        return;
    }

    USize   const remaining = _LOG_LINE_BUFFER_SIZE - _log_line_size;
    I32     const written   = (I32) vsnprintf(_log_line_buffer + _log_line_size, remaining, fmt, args);

    if (written > 0) {
        USize const safe = (USize) written;

        // vsnprintf answers what it WANTED to write: at or past the room left,
        // the line was cut. A line that fills the buffer exactly is still whole.
        if (safe >= remaining) {
            _log_line_truncated = true;
        }

        _log_line_size += safe < remaining ? safe : remaining - 1;
    }
    else {
        // A failed vsnprintf leaves the tail unspecified; restore the
        // terminator so later reads stop where the valid content does.
        _log_line_buffer[_log_line_size] = '\0';
    }
}

CFW_ATTR_PRINTF(1, 2)
static void _log_line_addf(char const *const fmt, ...) {
    va_list args = DEFAULT_INITIALIZATION;

    va_start(args, fmt);
    _log_line_vaddf(fmt, args);
    va_end(args);
}

static void _log_line_commit(void) {
    // A line that was cut ends with a fixed marker instead of a silent cut: an
    // array dump reaches the cap with no closing brace, and a reader must be
    // able to tell a cut line from a whole one. Keyed on the flag, not on the
    // size: a line of exactly the cap is whole.
    if (_log_line_truncated) {
        USize const marker_size = sizeof(_LOG_LINE_TRUNCATED_MARKER) - 1;

        memcpy(_log_line_buffer + _LOG_LINE_BUFFER_SIZE - 1 - marker_size, _LOG_LINE_TRUNCATED_MARKER, marker_size + 1);
    }

    // nullptr stream means stream output is DISABLED (log.h contract); the
    // buffer mirror below still records.
    if (_log.stream != nullptr) {
        fwrite(_log_line_buffer, 1, _log_line_size, _log.stream);

        if (_log.autoflush) {
            fflush(_log.stream);
        }
    }

    // The mirror keeps what fits, always terminated: a bounded copy of the
    // assembled line, not a second printf pass over it.
    if (_log.buffer != nullptr && _log.buffer_size < _log.buffer_capacity) {
        USize const remaining = _log.buffer_capacity - _log.buffer_size;
        USize const copied    = _log_line_size < remaining ? _log_line_size : remaining - 1;

        memcpy(_log.buffer + _log.buffer_size, _log_line_buffer, copied);

        _log.buffer_size += copied;
        _log.buffer[_log.buffer_size] = '\0';
    }
}

static void _log_header(LogLevel const level) {
#ifdef LOG_THREAD_IMPLEMENTATION
    ThreadId const thread_id = thread_get_id_1();
#endif // LOG_THREAD_IMPLEMENTATION

    char const *time_str = "";

    if (_log.timestamp_enabled) {
        time_t const time_now = time(nullptr);

        if (time_now != _log_cached_time) {
            _log_cached_time = time_now;

            struct tm time_info = DEFAULT_INITIALIZATION;
#ifdef OS_WINDOWS
            localtime_s(&time_info, &time_now);
#else
            localtime_r(&time_now, &time_info);
#endif
            strftime(_log_cached_time_buffer, sizeof(_log_cached_time_buffer),
                "%Y-%m-%d %H:%M:%S ", &time_info);
        }

        time_str = _log_cached_time_buffer;
    }

    char const *level_str = "[     ]";
    char const *color     = CONSOLE_FORMAT_RESET;

    switch (level) {
        case LOG_LEVEL_ERROR: { level_str = "[ERROR]"; color = CONSOLE_COLOR_RED;    break; }
        case LOG_LEVEL_WARN:  { level_str = "[WARN] "; color = CONSOLE_COLOR_YELLOW; break; }
        case LOG_LEVEL_INFO:  { level_str = "[INFO] "; color = CONSOLE_COLOR_GREEN;  break; }
        case LOG_LEVEL_DEBUG: { level_str = "[DEBUG]"; color = CONSOLE_COLOR_CYAN;   break; }
    }

#ifdef LOG_THREAD_IMPLEMENTATION
    _log_line_addf(
        "[" CONSOLE_COLOR_MAGENTA "%.5ld" CONSOLE_FORMAT_RESET "] ",
        (long) thread_id);
#endif // LOG_THREAD_IMPLEMENTATION
    _log_line_addf(CONSOLE_COLOR_BRIGHT_WHITE "%s" CONSOLE_FORMAT_RESET, time_str);
    _log_line_addf("%s%s %s", color, level_str, CONSOLE_FORMAT_RESET);
}

static void _log_metadata(char const *file, I32 const line, char const *function) {
    _log_line_addf(
            CONSOLE_COLOR_GREEN         "%s"    CONSOLE_FORMAT_RESET
            CONSOLE_COLOR_BRIGHT_WHITE  ":%.5d" CONSOLE_FORMAT_RESET " "
                                        "<"                          " "
            CONSOLE_COLOR_YELLOW        "%s"    CONSOLE_FORMAT_RESET " "
                                        "<"                          " ",
            file != nullptr ? file : "(null)", line, function != nullptr ? function : "(null)");
}

#ifdef LOG_THREAD_IMPLEMENTATION
static void _log_sync_thread_default(void) {
    if (_log.initialized || !atomic_load(&_log_default_ready)) {
        return;
    }

    // A failed lock (e.g. EINVAL after log_uninit destroyed the mutex on
    // POSIX) must not copy half-torn defaults - return without copying and
    // without unlocking a lock this thread does not hold.
    if (result_is_error(thread_mutex_lock(&_log_default_mutex))) {
        return;
    }

    _log.autoflush         = _log_default.autoflush;
    _log.initialized       = true;
    _log.level             = _log_default.level;
    _log.stream            = _log_default.stream;
    _log.timestamp_enabled = _log_default.timestamp_enabled;
    
    thread_mutex_unlock(&_log_default_mutex);
}
#else
static void _log_sync_thread_default(void) {
}
#endif // LOG_THREAD_IMPLEMENTATION

static void _log_require_initialized(void) {
    _log_sync_thread_default();

    if (!_log.initialized) {
        _log.autoflush         = true;
        _log.level             = LOG_LEVEL_ERROR;
        _log.stream            = stderr;
        _log.timestamp_enabled = true;

        _log_line_reset();
        _log_header(LOG_LEVEL_ERROR);
        _log_metadata(LOG_METADATA);
        _log_line_addf("[WRONG_VALUE] > log_initialized > %d\n", _log.initialized);
        _log_line_commit();

        exit(EXIT_FAILURE);
    }
}

/* Shared body of the four message entry points, past their initialization
 * gate. A null file means "no metadata segment" - the _1 shape. */
static void _log_message_vformat(LogLevel const level, char const *const file, I32 const line, char const *const function, char const *const fmt, va_list args) {
    if (level > _log.level) {
        return;
    }

    _log_line_reset();
    _log_header(level);

    /* Plain comparison: log sits below memory in the layering and cannot use
     * memory_empty; _log_metadata guards the same way. */
    if (file != nullptr) {
        _log_metadata(file, line, function);
    }

    _log_line_vaddf(fmt, args);
    _log_line_commit();
}

/*==============================================================================
 * MARK: - Public API
 *============================================================================*/
void log_buffer_clear(void) {
    _log_require_initialized();

    if (_log.buffer != nullptr) {
        memset(_log.buffer, 0, _log.buffer_size);
        _log.buffer_size = 0;
    }
}

void log_debug_step(char const *file, I32 const line, char const *function) {
    _log_require_initialized();

    if (_log.level >= LOG_LEVEL_DEBUG) {
        _log_line_reset();
        _log_header(LOG_LEVEL_DEBUG);
        _log_metadata(file, line, function);

        _log_line_addf("[STEP] > " CONSOLE_COLOR_BLUE "%llu\n" CONSOLE_FORMAT_RESET,
            (unsigned long long) _log.debug_step_counter);

        _log_line_commit();

        _log.debug_step_counter += 1;
    }
}

void log_debug_variable_array_char(char const *file, I32 const line, char const *function, char const *variable, char const *const value, USize const size, bool const verbose) {
    _log_require_initialized();

    if (value == nullptr) {
        return;
    }

    if (_log.level >= LOG_LEVEL_DEBUG) {
        _log_line_reset();
        _log_header(LOG_LEVEL_DEBUG);
        _log_metadata(file, line, function);

        _log_line_addf(
            "[" CONSOLE_COLOR_BRIGHT_WHITE "%s"     CONSOLE_FORMAT_RESET "] > "
                CONSOLE_COLOR_MAGENTA      "%llu\n" CONSOLE_FORMAT_RESET "{\n\t",
            variable != nullptr ? variable : "(null)", (unsigned long long) size);

        for (USize i = 0; i < size; i += 1) {
            if (verbose) {
                _log_line_addf("[%3llu] = %3d -> ", (unsigned long long) i, value[i]);
            }

            switch (value[i]) {
                case '\0': { _log_line_addf("\\0");          break; }
                case '\n': { _log_line_addf("\\n");          break; }
                case '\t': { _log_line_addf("\\t");          break; }
                case '\r': { _log_line_addf("\\r");          break; }
                default:   { _log_line_addf("%c", value[i]); break; }
            }

            if (i + 1 < size) {
                _log_line_addf(",\n\t");
            }
        }

        _log_line_addf("\n}\n");

        _log_line_commit();
    }
}

void log_debug_variable_array_str(char const *file, I32 const line, char const *function, char const *variable, char const *const *const value, USize const size, bool const verbose) {
    _log_require_initialized();

    if (value == nullptr) {
        return;
    }

    if (_log.level >= LOG_LEVEL_DEBUG) {
        _log_line_reset();
        _log_header(LOG_LEVEL_DEBUG);
        _log_metadata(file, line, function);

        _log_line_addf(
            "[" CONSOLE_COLOR_BRIGHT_WHITE "%s"     CONSOLE_FORMAT_RESET "] "
                CONSOLE_COLOR_MAGENTA      "%llu\n" CONSOLE_FORMAT_RESET "{\n\t",
            variable != nullptr ? variable : "(null)", (unsigned long long) size);

        for (USize i = 0; i < size; i += 1) {
            if (verbose) {
                _log_line_addf("[%3llu] = ", (unsigned long long) i);
            }

            _log_line_addf("%s", value[i] != nullptr ? value[i] : "(null)");

            if (i + 1 < size) {
                _log_line_addf(",\n\t");
            }
        }

        _log_line_addf("\n}\n");

        _log_line_commit();
    }
}

void log_debug_variable_array_uint(
    char const *file, I32 const line, char const *function, char const *variable,
    USize const *const value, USize const size, USize const element_line, bool const verbose) {
    _log_require_initialized();

    if (value == nullptr) {
        return;
    }

    if (_log.level >= LOG_LEVEL_DEBUG) {
        USize const effective_line = element_line > 0 ? element_line : size;

        _log_line_reset();
        _log_header(LOG_LEVEL_DEBUG);
        _log_metadata(file, line, function);

        _log_line_addf(
            "[" CONSOLE_COLOR_BRIGHT_WHITE "%s"     CONSOLE_FORMAT_RESET "] "
                CONSOLE_COLOR_MAGENTA      "%llu\n" CONSOLE_FORMAT_RESET "{\n\t",
            variable != nullptr ? variable : "(null)", (unsigned long long) size);

        for (USize i = 0; i < size; i += 1) {
            if (verbose) {
                _log_line_addf("[%3llu] = ", (unsigned long long) i);
            }

            _log_line_addf("%3llu", (unsigned long long) value[i]);

            if (i + 1 < size) {
                _log_line_addf(", ");
            }

            /* A row break only BETWEEN rows: breaking after the last element left a
             * dangling tab that a carriage return then "erased" - a terminal trick that
             * leaked a raw CR into file streams and the mirror, and overprinted the brace
             * when size was not a row multiple. */
            if ((i + 1) % effective_line == 0 && i + 1 < size) {
                _log_line_addf("\n\t");
            }
        }

        _log_line_addf("\n}\n");

        _log_line_commit();
    }
}

void log_debug_variable_char(char const *file, I32 const line, char const *function, char const *variable, char const value) {
    _log_require_initialized();

    if (_log.level >= LOG_LEVEL_DEBUG) {
        _log_line_reset();
        _log_header(LOG_LEVEL_DEBUG);
        _log_metadata(file, line, function);

        _log_line_addf(
                CONSOLE_COLOR_BRIGHT_RED   "["    CONSOLE_FORMAT_RESET
                CONSOLE_COLOR_BRIGHT_WHITE "%s"   CONSOLE_FORMAT_RESET
                CONSOLE_COLOR_BRIGHT_RED   "]"    CONSOLE_FORMAT_RESET
                                           " -> "
                CONSOLE_COLOR_MAGENTA      "%c"   CONSOLE_FORMAT_RESET
                                           " : "
                CONSOLE_COLOR_MAGENTA      "%d\n" CONSOLE_FORMAT_RESET,
            variable != nullptr ? variable : "(null)", value, value);

        _log_line_commit();
    }
}

void log_debug_variable_float(char const *file, I32 const line, char const *function, char const *variable, FSize const value) {
    _log_require_initialized();

    if (_log.level >= LOG_LEVEL_DEBUG) {
        _log_line_reset();
        _log_header(LOG_LEVEL_DEBUG);
        _log_metadata(file, line, function);

        _log_line_addf(
            CONSOLE_COLOR_BRIGHT_RED   "["    CONSOLE_FORMAT_RESET
            CONSOLE_COLOR_BRIGHT_WHITE "%s"   CONSOLE_FORMAT_RESET
            CONSOLE_COLOR_BRIGHT_RED   "]"    CONSOLE_FORMAT_RESET
                                       " -> "
            CONSOLE_COLOR_MAGENTA      "%f\n" CONSOLE_FORMAT_RESET,
            variable != nullptr ? variable : "(null)", value);

        _log_line_commit();
    }
}

void log_debug_variable_int(char const *file, I32 const line, char const *function, char const *variable, I64 const value) {
    _log_require_initialized();

    if (_log.level >= LOG_LEVEL_DEBUG) {
        _log_line_reset();
        _log_header(LOG_LEVEL_DEBUG);
        _log_metadata(file, line, function);

        _log_line_addf(
            CONSOLE_COLOR_BRIGHT_RED   "["      CONSOLE_FORMAT_RESET
            CONSOLE_COLOR_BRIGHT_WHITE "%s"     CONSOLE_FORMAT_RESET
            CONSOLE_COLOR_BRIGHT_RED   "]"      CONSOLE_FORMAT_RESET
                                       " -> "
            CONSOLE_COLOR_MAGENTA      "%lld\n" CONSOLE_FORMAT_RESET,
            variable != nullptr ? variable : "(null)", (long long) value);

        _log_line_commit();
    }
}

void log_debug_variable_ptr(char const *file, I32 const line, char const *function, char const *variable, void const *const value) {
    _log_require_initialized();

    if (_log.level >= LOG_LEVEL_DEBUG) {
        _log_line_reset();
        _log_header(LOG_LEVEL_DEBUG);
        _log_metadata(file, line, function);

        _log_line_addf(
                CONSOLE_COLOR_BRIGHT_RED   "["    CONSOLE_FORMAT_RESET
                CONSOLE_COLOR_BRIGHT_WHITE "%s"   CONSOLE_FORMAT_RESET
                CONSOLE_COLOR_BRIGHT_RED   "]"    CONSOLE_FORMAT_RESET
                                           " -> "
                CONSOLE_COLOR_MAGENTA      "%p\n" CONSOLE_FORMAT_RESET,
            variable != nullptr ? variable : "(null)", (void*) value);

        _log_line_commit();
    }
}

void log_debug_variable_str_1(char const *file, I32 const line, char const *function, char const *variable, char const *const value) {
    _log_require_initialized();

    if (_log.level >= LOG_LEVEL_DEBUG) {
        _log_line_reset();
        _log_header(LOG_LEVEL_DEBUG);
        _log_metadata(file, line, function);

        _log_line_addf(
            CONSOLE_COLOR_BRIGHT_RED   "["    CONSOLE_FORMAT_RESET
            CONSOLE_COLOR_BRIGHT_WHITE "%s"   CONSOLE_FORMAT_RESET
            CONSOLE_COLOR_BRIGHT_RED   "]"    CONSOLE_FORMAT_RESET
                                       " -> "
            CONSOLE_COLOR_MAGENTA      "%s\n" CONSOLE_FORMAT_RESET,
            variable != nullptr ? variable : "(null)", value != nullptr ? value : "(null)");

        _log_line_commit();
    }
}

void log_debug_variable_str_2(char const *file, I32 const line, char const *function, char const *variable, char const *const value, USize const value_size) {
    _log_require_initialized();

    if (_log.level >= LOG_LEVEL_DEBUG) {
        _log_line_reset();
        _log_header(LOG_LEVEL_DEBUG);
        _log_metadata(file, line, function);

        USize const clamped_size = value_size <= (USize) INT_MAX ? value_size : (USize) INT_MAX;

        _log_line_addf(
            CONSOLE_COLOR_BRIGHT_RED   "["       CONSOLE_FORMAT_RESET
            CONSOLE_COLOR_BRIGHT_WHITE "%s"      CONSOLE_FORMAT_RESET
            CONSOLE_COLOR_BRIGHT_RED   "]"       CONSOLE_FORMAT_RESET
                                       " -> "
            CONSOLE_COLOR_MAGENTA      "%.*s\n"  CONSOLE_FORMAT_RESET,
            variable != nullptr ? variable : "(null)", value != nullptr ? (int) clamped_size : (int) sizeof("(null)") - 1, value != nullptr ? value : "(null)");

        _log_line_commit();
    }
}

void log_debug_variable_uint(char const *file, I32 const line, char const *function, char const *variable, USize const value) {
    _log_require_initialized();

    if (_log.level >= LOG_LEVEL_DEBUG) {
        _log_line_reset();
        _log_header(LOG_LEVEL_DEBUG);
        _log_metadata(file, line, function);

        _log_line_addf(
            CONSOLE_COLOR_BRIGHT_RED   "["      CONSOLE_FORMAT_RESET
            CONSOLE_COLOR_BRIGHT_WHITE "%s"     CONSOLE_FORMAT_RESET
            CONSOLE_COLOR_BRIGHT_RED   "]"      CONSOLE_FORMAT_RESET
                                       " -> "
            CONSOLE_COLOR_MAGENTA      "%llu\n" CONSOLE_FORMAT_RESET,
            variable != nullptr ? variable : "(null)", (unsigned long long) value);

        _log_line_commit();
    }
}

bool log_get_autoflush(void) {
    _log_require_initialized();

    return _log.autoflush;
}

char* log_get_buffer(void) {
    _log_require_initialized();

    return _log.buffer;
}

USize log_get_buffer_size(void) {
    _log_require_initialized();

    return _log.buffer_size;
}

LogLevel log_get_level(void) {
    _log_require_initialized();

    return _log.level;
}

FILE* log_get_stream(void) {
    _log_require_initialized();

    return _log.stream;
}

void log_init(LogConfig const config) {
    _log.initialized = true;
    log_set_autoflush(config.autoflush);
    log_set_buffer(nullptr, 0);
    log_set_level(config.level);
    log_set_stream(config.stream);
    log_set_timestamp_enabled(config.timestamp_enabled);

#ifdef LOG_THREAD_IMPLEMENTATION
    if (!_log_default_mutex_initialized) {
        // Without the mutex the shared defaults cannot be published safely.
        // This thread still logs (its own _log is configured above), which is
        // what makes the message safe; workers never see _log_default_ready
        // and take the fail-fast path, so main must be told why - at ERROR,
        // the one level no configured threshold can gate away.
        if (result_is_error(thread_mutex_init(&_log_default_mutex))) {
            log_message_1(LOG_LEVEL_ERROR, "log: the shared-default mutex could not be created - worker threads will not inherit this configuration\n");

            return;
        }

        _log_default_mutex_initialized = true;
    }

    if (result_is_error(thread_mutex_lock(&_log_default_mutex))) {
        log_message_1(LOG_LEVEL_ERROR, "log: the shared-default mutex could not be locked - worker threads will not inherit this configuration\n");

        return;
    }

    _log_default.autoflush         = _log.autoflush;
    _log_default.initialized       = true;
    _log_default.level             = _log.level;
    _log_default.stream            = _log.stream;
    _log_default.timestamp_enabled = _log.timestamp_enabled;
    thread_mutex_unlock(&_log_default_mutex);

    atomic_store(&_log_default_ready, true);
#endif // LOG_THREAD_IMPLEMENTATION
}

bool log_is_initialized(void) {
    _log_sync_thread_default();

    return _log.initialized;
}

bool log_is_timestamp_enabled(void) {
    _log_require_initialized();

    return _log.timestamp_enabled;
}

void log_message_1(LogLevel const level, char const *fmt, ...) {
    _log_require_initialized();

    va_list args = DEFAULT_INITIALIZATION;

    va_start(args, fmt);
    _log_message_vformat(level, nullptr, 0, nullptr, fmt, args);
    va_end(args);
}

void log_message_2(LogLevel const level, char const *file, I32 const line, char const *function, char const *fmt, ...) {
    _log_require_initialized();

    va_list args = DEFAULT_INITIALIZATION;

    va_start(args, fmt);
    _log_message_vformat(level, file, line, function, fmt, args);
    va_end(args);
}

void log_message_try_1(LogLevel const level, char const *fmt, ...) {
    /* The one difference from log_message_1: an uninitialized logger is a
     * no-op here, never the fail-fast exit - this is the entry point for a
     * branch whose whole purpose is not ending the process. */
    if (!log_is_initialized()) {
        return;
    }

    va_list args = DEFAULT_INITIALIZATION;

    va_start(args, fmt);
    _log_message_vformat(level, nullptr, 0, nullptr, fmt, args);
    va_end(args);
}

void log_message_try_2(LogLevel const level, char const *file, I32 const line, char const *function, char const *fmt, ...) {
    if (!log_is_initialized()) {
        return;
    }

    va_list args = DEFAULT_INITIALIZATION;

    va_start(args, fmt);
    _log_message_vformat(level, file, line, function, fmt, args);
    va_end(args);
}

void log_print(LogLevel const level, char const *file, I32 const line, char const *function, char const *fmt, ...) {
    _log_require_initialized();

    if (level > _log.level) {
        return;
    }

    _log_line_reset();
    _log_header(level);
    _log_metadata(file, line, function);

    va_list args = DEFAULT_INITIALIZATION;

    va_start(args, fmt);
    _log_line_vaddf(fmt, args);
    va_end(args);

    _log_line_addf("\n");
    _log_line_commit();
}

void log_print_raw(char const *const fmt, ...) {
    _log_require_initialized();

    _log_line_reset();

    va_list args = DEFAULT_INITIALIZATION;

    va_start(args, fmt);
    _log_line_vaddf(fmt, args);
    va_end(args);

    _log_line_commit();
}

void log_print_time(void) {
    _log_require_initialized();

    time_t const time_now = time(nullptr);
    struct tm time_info = DEFAULT_INITIALIZATION;
#ifdef OS_WINDOWS
    localtime_s(&time_info, &time_now);
#else
    localtime_r(&time_now, &time_info);
#endif

    char buffer[_LOG_TIMESTAMP_BUFFER_SIZE] = DEFAULT_INITIALIZATION;

    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S ", &time_info);

    _log_line_reset();
    _log_line_addf(CONSOLE_COLOR_BRIGHT_WHITE "%s" CONSOLE_FORMAT_RESET, buffer);
    _log_line_commit();
}

void log_set_autoflush(bool const autoflush) {
    _log_require_initialized();

    _log.autoflush = autoflush;
}

void log_set_buffer(char *const buffer, USize const buffer_capacity) {
    _log_require_initialized();

    // A zero capacity is no buffer: keeping the pointer handed log_get_buffer
    // an array this module never terminated.
    _log.buffer          = buffer_capacity > 0 ? buffer : nullptr;
    _log.buffer_capacity = buffer_capacity;
    _log.buffer_size     = 0;

    // Terminate immediately: log_get_buffer readers must never see the
    // caller's stale pre-buffer bytes as a string.
    if (_log.buffer != nullptr) {
        _log.buffer[0] = '\0';
    }
}

void log_set_level(LogLevel const level) {
    _log_require_initialized();

    _log.level = level;
}

void log_set_stream(FILE *const stream) {
    _log_require_initialized();

    _log.stream = stream;
}

void log_set_timestamp_enabled(bool const enabled) {
    _log_require_initialized();

    _log.timestamp_enabled = enabled;
}

void log_uninit(void) {
    if (_log.stream != nullptr) {
        fflush(_log.stream);
    }

#ifdef LOG_THREAD_IMPLEMENTATION
    if (_log_default_mutex_initialized) {
        atomic_store(&_log_default_ready, false);
        thread_mutex_uninit(&_log_default_mutex);
        _log_default_mutex_initialized = false;
        memset(&_log_default, 0, sizeof _log_default);
    }
#endif // LOG_THREAD_IMPLEMENTATION

    memset(&_log, 0, sizeof _log);
}