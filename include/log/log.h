/*
 * log.h - Centralized logging and debug tracing for the C Libraries Framework
 *
 * Features:
 *   - Metadata-rich logging: timestamp, file, line, and function
 *   - Logs messages, typed variables (char, int, float, pointer, string, arrays), and debug steps
 *   - Per-message level gate: each log_message_* / log_print call carries its own LogLevel and is
 *     dropped before any I/O when more verbose than the active threshold
 *   - debug_* functions emit only at the LOG_LEVEL_DEBUG threshold; there is no separate toggle
 *   - Per-thread logging: each thread owns its log state via thread_local storage; a shared default
 *     is copied into each thread on first use
 *   - Optional timestamping with a per-thread second-resolution cache
 *   - Optional autoflush: fflush after every write (default true); disable for high-throughput paths
 *
 * Usage Examples:
 *   @code
 *   log_init((LogConfig){ .level = LOG_LEVEL_INFO, .stream = stdout, .timestamp_enabled = true, .autoflush = true });
 *   log_message_1(LOG_LEVEL_INFO, "Initialization complete\n");
 *   log_debug_variable_int(LOG_METADATA, "x", x);
 *   log_debug_step(LOG_METADATA);
 *   log_uninit();
 *   @endcode
 *
 * Error Handling:
 *   - If log_init has not been called, _log_require_initialized prints an error and exits. This is
 *     intentional: the module fails fast when used without initialization.
 *   - log_message_try_1/_2 are the twins for a path that must not end the process (a degradation
 *     branch that already survived an allocation refusal): while the logger is not initialized -
 *     before log_init, or after log_uninit - they return without any I/O; while it is, they are
 *     log_message_1/_2 exactly. They never swallow a failure - only the uninitialized state.
 *
 * Thread Safety:
 *   - Each thread owns its log state (thread_local Log _log); per-thread operations need no locking.
 *     Without LOG_THREAD_IMPLEMENTATION the state is a single shared static and the module is
 *     single-thread-only.
 *   - The shared default config is protected by an internal mutex and initialized once by log_init.
 *     Call log_init from the main thread before spawning workers.
 *   - Thread-default sync is one-shot: a worker copies the default into its own state on first use
 *     and never re-syncs, so later log_set_* calls affect only the calling thread.
 *
 * Memory Management:
 *   - The log buffer is caller-allocated; pass it via log_set_buffer(). The module never allocates
 *     or frees it.
 *
 * Performance Characteristics:
 *   - Suppressed messages (level > threshold) return before any I/O or time() call.
 *   - The timestamp is cached per thread at second resolution; strftime runs only when the epoch
 *     second changes.
 *   - Set autoflush=false for high-volume logging and flush on demand with log_print_time().
 *
 * Limits:
 *   - One log line - a message, or a whole debug block such as an array dump - is assembled in a
 *     per-thread 8 KB buffer and committed with a single write (whole-line atomicity). A longer
 *     line is cut at the cap and ends with "...[truncated]" so the cut is visible; an array dump
 *     that hits it has no closing brace. Split large dumps, or raise _LOG_LINE_BUFFER_SIZE in log.c.
 *   - The caller-owned mirror (log_set_buffer) keeps what fits: a line longer than the remaining
 *     capacity is cut to it, always terminated.
 *
 * Compile-time debug stripping:
 *   - To strip log_debug_* calls in release builds, define LOG_MAX_LEVEL (0=ERROR,1=WARN,2=INFO,
 *     3=DEBUG) and guard each call site with #if !defined(LOG_MAX_LEVEL) || LOG_MAX_LEVEL >= 3.
 *     CFW style prohibits macro-wrapping function names, so there is no automatic replacement.
 *
 * Dependencies:
 *   - <limits.h>, <stdarg.h>, <stdlib.h>, <string.h>, <time.h>, <console/console.h> (which brings
 *     <stdio.h> for FILE and <types.h> for the fixed-width types and CFW_ATTR_PRINTF)
 *   - LOG_THREAD_IMPLEMENTATION: <stdatomic.h>, <thread/thread.h>
 *
 * See log.c for implementation details.
 */

#ifndef UTIL_LOG_H
#define UTIL_LOG_H

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <console/console.h>

#ifdef LOG_THREAD_IMPLEMENTATION
#include <stdatomic.h>
#include <thread/thread.h>
#endif // LOG_THREAD_IMPLEMENTATION

/*==============================================================================
 * MARK: - Macros and Constants
 *============================================================================*/
/**
 * @def LOG_METADATA
 * @brief Inject __FILE__, __LINE__, and __func__ as the leading metadata arguments.
 */
#define LOG_METADATA __FILE__, __LINE__, __func__

/**
 * @def LOG_STREAM_STDERR
 * @brief Standard error stream, for use as a LogConfig or log_set_stream argument.
 */
#define LOG_STREAM_STDERR stderr

/**
 * @def LOG_STREAM_STDOUT
 * @brief Standard output stream, for use as a LogConfig or log_set_stream argument.
 */
#define LOG_STREAM_STDOUT stdout

/*==============================================================================
 * MARK: - Typedefs and Enums
 *============================================================================*/
/**
 * @enum LogLevel
 * @brief Verbosity threshold. A message is emitted only when its level <= the active threshold.
 *
 * Lower numeric values are less verbose; LOG_LEVEL_ERROR (0) is the most restrictive. The values
 * double as the 0-3 integers expected by LOG_MAX_LEVEL for compile-time stripping.
 */
typedef enum : U8 {
    LOG_LEVEL_ERROR = 0, /**< Error messages only.   */
    LOG_LEVEL_WARN  = 1, /**< Warnings and above.    */
    LOG_LEVEL_INFO  = 2, /**< Info and above.        */
    LOG_LEVEL_DEBUG = 3  /**< Debug and all above.   */
} LogLevel;

/**
 * @struct LogConfig
 * @brief Initialization configuration passed to log_init.
 * @var LogConfig::level             Minimum level threshold to emit (e.g. LOG_LEVEL_INFO).
 * @var LogConfig::stream            Output stream, or nullptr to disable stream output.
 * @var LogConfig::timestamp_enabled If true, each message is prefixed with a date-time stamp.
 * @var LogConfig::autoflush         If true, fflush runs after every write.
 */
typedef struct {
    LogLevel level;
    FILE     *stream;
    bool     timestamp_enabled;
    bool     autoflush;
} LogConfig;

/*==============================================================================
 * MARK: - Public API
 *============================================================================*/
/**
 * @brief Reset the log output buffer to empty. No effect when no buffer is set.
 */
void log_buffer_clear(void);

/**
 * @brief Log an auto-incrementing debug step with source metadata.
 * @param file     Source file (__FILE__).
 * @param line     Source line (__LINE__).
 * @param function Source function (__func__).
 *
 * Emits only at the LOG_LEVEL_DEBUG threshold.
 */
void log_debug_step(char const *file, I32 const line, char const *function);

/**
 * @brief Log the contents of a char array.
 * @param file     Source file (__FILE__).
 * @param line     Source line (__LINE__).
 * @param function Source function (__func__).
 * @param variable Name of the variable being logged.
 * @param value    The char array (read-only).
 * @param size     Number of elements in the array.
 * @param verbose  If true, also print the index and integer value of each element.
 */
void log_debug_variable_array_char(char const *file, I32 const line, char const *function, char const *variable, char const *const value, USize const size, bool const verbose);

/**
 * @brief Log an array of strings.
 * @param file     Source file (__FILE__).
 * @param line     Source line (__LINE__).
 * @param function Source function (__func__).
 * @param variable Name of the variable being logged.
 * @param value    The string array (read-only).
 * @param size     Number of elements in the array.
 * @param verbose  If true, also print the index of each element.
 */
void log_debug_variable_array_str(char const *file, I32 const line, char const *function, char const *variable, char const *const *const value, USize const size, bool const verbose);

/**
 * @brief Log an array of unsigned integers.
 * @param file         Source file (__FILE__).
 * @param line         Source line (__LINE__).
 * @param function     Source function (__func__).
 * @param variable     Name of the variable being logged.
 * @param value        The unsigned integer array (read-only).
 * @param size         Number of elements in the array.
 * @param element_line Elements to print per line (0 = all on one line).
 * @param verbose      If true, also print the index of each element.
 */
void log_debug_variable_array_uint(
    char const *file, I32 const line, char const *function, char const *variable,
    USize const *const value, USize const size, USize const element_line, bool const verbose);

/**
 * @brief Log a char variable with source metadata.
 * @param file     Source file (__FILE__).
 * @param line     Source line (__LINE__).
 * @param function Source function (__func__).
 * @param variable Name of the variable being logged.
 * @param value    The char value.
 */
void log_debug_variable_char(char const *file, I32 const line, char const *function, char const *variable, char const value);

/**
 * @brief Log a floating-point variable with source metadata.
 * @param file     Source file (__FILE__).
 * @param line     Source line (__LINE__).
 * @param function Source function (__func__).
 * @param variable Name of the variable being logged.
 * @param value    The floating-point value.
 */
void log_debug_variable_float(char const *file, I32 const line, char const *function, char const *variable, FSize const value);

/**
 * @brief Log a signed integer variable with source metadata.
 * @param file     Source file (__FILE__).
 * @param line     Source line (__LINE__).
 * @param function Source function (__func__).
 * @param variable Name of the variable being logged.
 * @param value    The integer value.
 */
void log_debug_variable_int(char const *file, I32 const line, char const *function, char const *variable, I64 const value);

/**
 * @brief Log a pointer variable with source metadata.
 * @param file     Source file (__FILE__).
 * @param line     Source line (__LINE__).
 * @param function Source function (__func__).
 * @param variable Name of the variable being logged.
 * @param value    The pointer value (read-only).
 */
void log_debug_variable_ptr(char const *file, I32 const line, char const *function, char const *variable, void const *const value);

/**
 * @brief Log a null-terminated string variable with source metadata.
 * @param file     Source file (__FILE__).
 * @param line     Source line (__LINE__).
 * @param function Source function (__func__).
 * @param variable Name of the variable being logged.
 * @param value    The string value (read-only).
 */
void log_debug_variable_str_1(char const *file, I32 const line, char const *function, char const *variable, char const *const value);

/**
 * @brief Log the first value_size characters of a string variable with source metadata.
 * @param file       Source file (__FILE__).
 * @param line       Source line (__LINE__).
 * @param function   Source function (__func__).
 * @param variable   Name of the variable being logged.
 * @param value      The string value (read-only).
 * @param value_size Number of characters to log.
 */
void log_debug_variable_str_2(char const *file, I32 const line, char const *function, char const *variable, char const *const value, USize const value_size);

/**
 * @brief Log an unsigned integer variable with source metadata.
 * @param file     Source file (__FILE__).
 * @param line     Source line (__LINE__).
 * @param function Source function (__func__).
 * @param variable Name of the variable being logged.
 * @param value    The unsigned integer value.
 */
void log_debug_variable_uint(char const *file, I32 const line, char const *function, char const *variable, USize const value);

/**
 * @brief Get whether autoflush is enabled.
 * @return true if fflush runs after every write.
 */
bool log_get_autoflush(void);

/**
 * @brief Get the current log output buffer.
 * @return Pointer to the buffer, or nullptr if none is set. Read-only; do not free.
 */
char* log_get_buffer(void);

/**
 * @brief Get the number of bytes currently written to the log buffer.
 * @return Bytes used in the buffer (0 if no buffer is set).
 */
USize log_get_buffer_size(void);

/**
 * @brief Get the current log level threshold.
 * @return The active LogLevel.
 */
LogLevel log_get_level(void);

/**
 * @brief Get the current output stream.
 * @return The active stream. Read-only; do not close.
 */
FILE* log_get_stream(void);

/**
 * @brief Initialize the logging system.
 * @param config Level, stream, timestamp_enabled, and autoflush settings.
 *
 * Must be called before any other log function - except log_is_initialized and the
 * log_message_try_* twins, which are defined for the uninitialized state. Under LOG_THREAD_IMPLEMENTATION, call from the main
 * thread before spawning workers; workers inherit the config on first use (one-shot sync).
 */
void log_init(LogConfig const config);

/**
 * @brief Report whether log_init has run (directly, or inherited from the
 *        thread defaults under LOG_THREAD_IMPLEMENTATION).
 * @return true once initialized.
 *
 * Never exits: this is the query the diagnostic stack itself uses to keep the
 * abort path out of _log_require_initialized's fail-fast exit.
 */
bool log_is_initialized(void);

/**
 * @brief Get whether timestamping is enabled.
 * @return true if timestamps are enabled.
 */
bool log_is_timestamp_enabled(void);

/**
 * @brief Log a printf-style message at the given level.
 * @param level Severity of this message (e.g. LOG_LEVEL_INFO).
 * @param fmt   printf-style format string.
 * @param ...   Arguments for the format string.
 *
 * Dropped without any I/O when level > the active threshold.
 */
CFW_ATTR_PRINTF(2, 3)
void log_message_1(LogLevel const level, char const *fmt, ...);

/**
 * @brief Log a printf-style message at the given level with source metadata.
 * @param level    Severity of this message.
 * @param file     Source file (__FILE__).
 * @param line     Source line (__LINE__).
 * @param function Source function (__func__).
 * @param fmt      printf-style format string.
 * @param ...      Arguments for the format string.
 *
 * Dropped without any I/O when level > the active threshold.
 */
CFW_ATTR_PRINTF(5, 6)
void log_message_2(LogLevel const level, char const *file, I32 const line, char const *function, char const *fmt, ...);

/**
 * @brief Log a printf-style message at the given level, or do nothing while the
 *        logger is not initialized (before log_init, or after log_uninit).
 * @param level Severity of this message (e.g. LOG_LEVEL_INFO).
 * @param fmt   printf-style format string.
 * @param ...   Arguments for the format string.
 *
 * The twin of log_message_1 for a path that must not end the process - a
 * degradation branch that has just survived a refused allocation. While the logger
 * is not initialized (before log_init, or after log_uninit) it returns without any
 * I/O instead of the fail-fast exit; while it is, it is log_message_1 exactly (same
 * threshold, same stream).
 */
CFW_ATTR_PRINTF(2, 3)
void log_message_try_1(LogLevel const level, char const *fmt, ...);

/**
 * @brief Log a printf-style message with source metadata, or do nothing while the
 *        logger is not initialized (before log_init, or after log_uninit).
 * @param level    Severity of this message.
 * @param file     Source file (__FILE__).
 * @param line     Source line (__LINE__).
 * @param function Source function (__func__).
 * @param fmt      printf-style format string.
 * @param ...      Arguments for the format string.
 *
 * The twin of log_message_2 with log_message_try_1's contract: no I/O before
 * log_init, identical to log_message_2 after it.
 */
CFW_ATTR_PRINTF(5, 6)
void log_message_try_2(LogLevel const level, char const *file, I32 const line, char const *function, char const *fmt, ...);

/**
 * @brief Log a printf-style message at the given level with source metadata and a trailing newline.
 * @param level    Severity of this message.
 * @param file     Source file (__FILE__).
 * @param line     Source line (__LINE__).
 * @param function Source function (__func__).
 * @param fmt      printf-style format string (no trailing newline needed).
 * @param ...      Arguments for the format string.
 *
 * Dropped without any I/O when level > the active threshold.
 */
CFW_ATTR_PRINTF(5, 6)
void log_print(LogLevel const level, char const *file, I32 const line, char const *function, char const *fmt, ...);

/**
 * @brief Write a printf-style line verbatim: no header, no metadata, no level
 *        gate. Honors the configured stream (nullptr = disabled) and mirrors
 *        into the log buffer.
 * @param fmt printf-style format string.
 * @param ... Arguments for the format string.
 *
 * The diagnostic stack's escape hatch: tracelog routes its abort-path trace
 * through this so a disabled stream with a live buffer still captures it.
 */
CFW_ATTR_PRINTF(1, 2)
void log_print_raw(char const *const fmt, ...);

/**
 * @brief Write the current system time to the configured stream and buffer.
 */
void log_print_time(void);

/**
 * @brief Enable or disable fflush after every write.
 * @param autoflush true to flush after every write (default), false to defer.
 */
void log_set_autoflush(bool const autoflush);

/**
 * @brief Set the caller-owned output buffer.
 * @param buffer          The buffer to write into; pass nullptr to disable buffering.
 * @param buffer_capacity Capacity of the buffer in bytes (log_get_buffer_size answers the bytes
 *                        USED). Zero disables buffering whatever buffer is - a zero-capacity
 *                        buffer would otherwise be handed to log_get_buffer unterminated.
 */
void log_set_buffer(char *const buffer, USize const buffer_capacity);

/**
 * @brief Set the minimum log level threshold.
 * @param level The lowest LogLevel to emit; set LOG_LEVEL_DEBUG to enable debug_* output.
 */
void log_set_level(LogLevel const level);

/**
 * @brief Set the output stream.
 * @param stream The stream to use (stdout, stderr, or an open FILE*); nullptr disables stream output.
 */
void log_set_stream(FILE *const stream);

/**
 * @brief Enable or disable the timestamp prefix.
 * @param enabled true to enable timestamps, false to disable.
 */
void log_set_timestamp_enabled(bool const enabled);

/**
 * @brief Shut down the logging system.
 *
 * Flushes the current stream, releases the shared default mutex (under LOG_THREAD_IMPLEMENTATION),
 * and resets the thread-local log state. Call from the main thread after all workers have exited.
 */
void log_uninit(void);

#endif // UTIL_LOG_H