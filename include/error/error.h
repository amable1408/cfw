/*
 * error.h - Centralized error checking utilities for the C Libraries Framework
 *
 * Features:
 *   - Functions for null, bounds, value, and format checks
 *   - All error output is routed through the log module (metadata-rich, colorized)
 *   - The current trace stack is printed before the program aborts
 *
 * Usage Examples:
 *   @code
 *   error_check_null(LOG_METADATA, "ptr", (void*) ptr);
 *   error_check_out_of_bound_int(LOG_METADATA, "i", i, "max", max, "i >= max", i >= max);
 *   @endcode
 *
 * Error Handling:
 *   - Every check is a no-op unless ERROR_CHECK_ENABLED is defined at build time.
 *   - When a check fails (or error_fail_* is called) it logs at LOG_LEVEL_ERROR and calls
 *     abort(); the checks do not return on failure.
 *
 * Thread Safety:
 *   - Output is routed through the log module, which keeps per-thread state. A failing
 *     check terminates the process.
 *
 * Memory Management:
 *   - Nothing is allocated or freed.
 *
 * Performance Characteristics:
 *   - Each check is O(1) and compiles to nothing when ERROR_CHECK_ENABLED is undefined.
 *
 * Dependencies:
 *   - <tracelog/tracelog.h> (which chains the log module and the standard headers used here)
 *
 * See error.c for implementation details.
 */

#ifndef ERROR_H
#define ERROR_H

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/
#include <tracelog/tracelog.h>

/*==============================================================================
 * MARK: - Public API
 *============================================================================*/
/**
 * @brief Abort with a message when cond is true.
 * @param file     Source file (__FILE__).
 * @param line     Source line (__LINE__).
 * @param function Source function (__func__).
 * @param cond     If true, the error is logged and the process aborts.
 * @param value    Message to log with the error.
 */
void error_check_message(char const *const file, I32 const line, char const *const function, bool const cond, char const *const value);

/**
 * @brief Abort when a floating-point value equals zero.
 * @param file     Source file (__FILE__).
 * @param line     Source line (__LINE__).
 * @param function Source function (__func__).
 * @param variable Name of the variable being checked.
 * @param value    Floating-point value to check.
 */
void error_check_non_value_float(char const *const file, I32 const line, char const *const function, char const *const variable, FSize const value);

/**
 * @brief Abort when a signed integer value equals zero.
 * @param file     Source file (__FILE__).
 * @param line     Source line (__LINE__).
 * @param function Source function (__func__).
 * @param variable Name of the variable being checked.
 * @param value    Integer value to check.
 */
void error_check_non_value_int(char const *const file, I32 const line, char const *const function, char const *const variable, ISize const value);

/**
 * @brief Abort when an unsigned integer value equals zero.
 * @param file     Source file (__FILE__).
 * @param line     Source line (__LINE__).
 * @param function Source function (__func__).
 * @param variable Name of the variable being checked.
 * @param value    Unsigned integer value to check.
 */
void error_check_non_value_uint(char const *const file, I32 const line, char const *const function, char const *const variable, USize const value);

/**
 * @brief Abort when value is a null pointer.
 * @param file     Source file (__FILE__).
 * @param line     Source line (__LINE__).
 * @param function Source function (__func__).
 * @param variable Name of the variable being checked.
 * @param value    Pointer value to check.
 */
void error_check_null(char const *const file, I32 const line, char const *const function, char const *const variable, void const *const value);

/**
 * @brief Abort when a signed out-of-bounds condition holds.
 * @param file        Source file (__FILE__).
 * @param line        Source line (__LINE__).
 * @param function    Source function (__func__).
 * @param var_name_0  Name of the first variable.
 * @param var_value_0 Value of the first variable.
 * @param var_name_1  Name of the second variable.
 * @param var_value_1 Value of the second variable.
 * @param cond        Condition string (e.g. "i >= max").
 * @param value       Condition result (true if out of bounds).
 */
void error_check_out_of_bound_int(
    char const *const file, I32 const line, char const *const function,
    char const *const var_name_0, ISize const var_value_0, char const *const var_name_1, ISize const var_value_1,
    char const *const cond, bool const value);

/**
 * @brief Abort when an unsigned out-of-bounds condition holds.
 * @param file        Source file (__FILE__).
 * @param line        Source line (__LINE__).
 * @param function    Source function (__func__).
 * @param var_name_0  Name of the first variable.
 * @param var_value_0 Value of the first variable.
 * @param var_name_1  Name of the second variable.
 * @param var_value_1 Value of the second variable.
 * @param cond        Condition string (e.g. "i >= max").
 * @param value       Condition result (true if out of bounds).
 */
void error_check_out_of_bound_uint(
    char const *const file, I32 const line, char const *const function,
    char const *const var_name_0, USize const var_value_0, char const *const var_name_1, USize const var_value_1,
    char const *const cond, bool const value);

/**
 * @brief Abort when a wrong-value condition holds.
 * @param file     Source file (__FILE__).
 * @param line     Source line (__LINE__).
 * @param function Source function (__func__).
 * @param variable Name of the variable being checked.
 * @param value    Condition result (true aborts).
 */
void error_check_wrong_value(char const *const file, I32 const line, char const *const function, char const *const variable, bool const value);

/**
 * @brief Abort unconditionally with a bad-format error - in EVERY build.
 *        Unlike the error_check_* family this is a deliberate fatal, so only
 *        the pretty (log/tracelog) reporting is gated by ERROR_CHECK_ENABLED;
 *        release builds still write a bare stderr line and abort().
 * @param file     Source file (__FILE__).
 * @param line     Source line (__LINE__).
 * @param function Source function (__func__).
 * @param variable Name of the variable being reported.
 * @param value    Value that caused the error.
 */
void error_fail_bad_format(char const *const file, I32 const line, char const *const function, char const *const variable, char const *const value);

#endif // ERROR_H