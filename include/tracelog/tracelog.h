/*
 * tracelog.h - Manual call-stack tracing for the C Libraries Framework
 *
 * Features:
 *   - Simple, portable function call tracing for debugging and error reporting
 *   - Self-contained recording: frames are RECORDED into a fixed thread-local
 *     stack and the text is built only by trace_log_print; push/pop never touch
 *     the log module at all
 *   - Push, pop, print, query depth, and clear the current call stack
 *   - Printing routes through log_print_raw on an initialized log: it honours
 *     the configured stream (nullptr = suppressed) AND mirrors into the log
 *     buffer; with log uninitialized it falls back to a bare stderr write
 *
 * Usage Examples:
 *   @code
 *   trace_log_push(LOG_METADATA);
 *   // ... function body ...
 *   trace_log_print();
 *   trace_log_pop();
 *   @endcode
 *
 * Error Handling:
 *   - All entry points are no-ops unless TRACELOG_ENABLED is defined at build time.
 *   - trace_log_pop ignores an empty stack instead of underflowing.
 *   - Pushes beyond the fixed depth are dropped but COUNTED, so their matching pops
 *     discard the drop rather than consuming a recorded frame, and trace_log_print
 *     reports how many frames are missing as a trailing "TRACE TRUNCATED" line. The
 *     recorded frames are kept: the deepest one is nearest whatever went wrong.
 *
 * Thread Safety:
 *   - Under LOG_THREAD_IMPLEMENTATION each thread owns its trace stack via thread_local
 *     storage, so per-thread tracing needs no synchronization.
 *   - Without LOG_THREAD_IMPLEMENTATION the stack is a single static instance; tracing is
 *     then single-thread-only.
 *
 * Memory Management:
 *   - The trace stack is a fixed-size array of frames, each holding two pointers and
 *     a line number. Nothing is allocated or freed, and nothing is copied: the only
 *     values ever pushed are __FILE__ and __func__, string literals that outlive any
 *     frame referring to them.
 *
 * Performance Characteristics:
 *   - Push is a bounds check and three stores; pop and clear are stores; depth is a
 *     read. Formatting happens once, in print, which is O(depth) - the text is only
 *     ever read when something has already gone wrong, so building it on every call
 *     charges the whole program for a diagnostic almost no call will ever need.
 *   - Zero overhead when TRACELOG_ENABLED is undefined.
 *
 * Dependencies:
 *   - <stdio.h>, <log/log.h>
 *
 * See tracelog.c for implementation details.
 */

#ifndef TRACELOG_H
#define TRACELOG_H

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/

#include <log/log.h>

/*==============================================================================
 * MARK: - Public API
 *============================================================================*/
/**
 * @brief Push a function entry onto the trace log stack.
 * @param file     Source file (__FILE__).
 * @param line     Source line (__LINE__).
 * @param function Source function (__func__).
 */
void trace_log_push(char const *const file, I32 const line, char const *const function);

/**
 * @brief Remove the most recent entry from the trace log stack.
 *
 * No effect when the stack is empty.
 */
void trace_log_pop(void);

/**
 * @brief Print the current trace log stack through the log module: the
 *        configured stream (nullptr = suppressed) plus the log buffer, via
 *        log_print_raw; falls back to a bare stderr write when log_init was
 *        never called.
 */
void trace_log_print(void);

/**
 * @brief Get the current number of RECORDED entries on the trace log stack.
 *
 * Counts what is stored, not how deep the real call stack went: frames dropped
 * past the fixed capacity are excluded, so this never exceeds that capacity.
 *
 * @return The recorded depth (0 when empty or when TRACELOG_ENABLED is undefined).
 */
USize trace_log_depth(void);

/**
 * @brief Remove every entry from the trace log stack.
 */
void trace_log_clear(void);

#endif // TRACELOG_H