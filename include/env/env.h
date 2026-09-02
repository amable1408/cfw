/*
 * env.h - Environment variable management (dotenv-style) for the C Libraries Framework
 *
 * Features:
 *   - Process-environment-backed loading: env_load parses a .env file and applies
 *     each pair to the real process environment, so existing getenv readers and
 *     argparse env fallbacks see the values with no code changes
 *   - In-memory loading: env_from_char/_str/_string apply the same dialect,
 *     precedence and PARTIAL semantics to data already in hand (config from a
 *     service, a DB row, a test) - `load` reads a file by path, `from` consumes
 *     data handed through a variable
 *   - Read helpers that collapse unset and empty: env_get_1/_2 (raw value),
 *     env_get_bool, the env_get_number_i/_u/_f family and env_exists
 *   - Cross-platform write wrappers: env_set_1/_2/_3 and env_unset hide the
 *     _putenv_s (Windows) / setenv-unsetenv (POSIX) divergence
 *   - Parser dialect: '#' comments (full-line, and inline after whitespace),
 *     blank lines, optional "export " prefix, double quotes with \n \t \r \" \\
 *     escapes, literal single quotes, whitespace-trimmed unquoted values, LF and
 *     CRLF endings only (CR-only is not supported) and a UTF-8 BOM tolerated
 *   - File keys must match [A-Za-z_][A-Za-z0-9_]* (anything else skips the line
 *     with a warning); keys are case-sensitive as written (always use
 *     exact-case names) and the load path is used exactly as given: no
 *     parent-directory search
 *
 * Usage Examples:
 *   @code
 *   // Optional-.env startup idiom: a missing file is a normal dev/prod
 *   // difference, but a present-yet-unreadable or partially-parsed file is a
 *   // real configuration error worth surfacing.
 *   Result const loaded  = env_load_1(".env"); // existing environment wins over the file
 *   bool   const missing = result_category(loaded) == RESULT_CATEGORY_SYSTEM && result_code(loaded) == 2;
 *
 *   if (result_is_error(loaded) && !missing) {
 *       log_message_1(LOG_LEVEL_ERROR, "config: .env present but not fully loaded\n");
 *   }
 *
 *   if (env_get_bool("CRM_DEV_BOOTSTRAP", false)) {
 *       char  *const url  = env_get_2("CRM_PUBLIC_URL", "http://localhost:8090");
 *       ISize  const port = env_get_number_i("CRM_PORT", 8090);
 *   }
 *   @endcode
 *
 * Error Handling:
 *   - Never aborts on runtime conditions: a missing variable is data (NULL or the
 *     caller's fallback), and a missing or unreadable file returns the OS
 *     classification from result_from_os. Programming errors - a NULL name,
 *     path or container pointer - are error_check_* failures, which abort with
 *     a logged location under ERROR_CHECK_ENABLED and compile out otherwise;
 *     every value-dependent refusal below stays in every build
 *   - File-not-found classifies as SYSTEM category with OS code 2 on both
 *     platforms (POSIX ENOENT, Windows ERROR_FILE_NOT_FOUND); see the Usage
 *     example for the optional-.env idiom built on that
 *   - A path that opens but is not a regular file (a directory) returns an
 *     IO-category Result instead of a silent empty success; the file is opened
 *     once, in binary mode, and read from that same handle
 *   - A regular file over FILE_READ_BYTES_MAX, or one the read returned short,
 *     is refused the same way - IO category, nothing applied. The size is
 *     taken from the open handle before the read and compared with the bytes
 *     that came back, so file's own cap refusal (an empty String plus one
 *     WARN) cannot surface here as a successful empty load. Residual: a handle
 *     whose size cannot be determined reads as 0 bytes and loads as empty
 *   - env_load skips malformed lines, logs one LOG_LEVEL_WARN per skip (path,
 *     line number and key only - never the value), and returns a Result with
 *     RESULT_FLAG_PARTIAL whose code field carries the number of skipped lines
 *   - env_set/env_unset return a Result: ARGUMENT category for an invalid name
 *     (empty, containing '=', or containing control characters) or for an
 *     env_set_2/_3 value with an embedded NUL (it would silently truncate at
 *     the OS boundary); SYSTEM category for an OS-level failure
 *   - MEMORY category when the module's own copy cannot be taken - an
 *     env_set_2/_3 value or an env_from_* buffer: the variable is untouched,
 *     nothing is applied. Memory pressure has three shapes in all: that
 *     MEMORY/0; an OS refusal to store the variable, which is SYSTEM with the
 *     OS code (ENOMEM, 12 on both platforms) and carries RESULT_FLAG_CRITICAL
 *     on POSIX only, where result_from_os classifies it; and inside env_load,
 *     where a line the OS refuses is one skipped line under RESULT_FLAG_PARTIAL
 *     with the category lost
 *   - Unset and empty ("") are one condition everywhere: env_get_1 returns NULL
 *     for both, fallbacks apply to both and env_exists reports false (on
 *     Windows, setting an empty value removes the variable at the OS level).
 *     Child processes differ per platform, though: after env_set_1("X", ""), a
 *     POSIX child sees X= (getenv returns ""), a Windows child sees X absent
 *   - Values are bytes in the C runtime's narrow environment: UTF-8 round-trips
 *     through this module's own getters, but on Windows GetEnvironmentVariableW
 *     readers and children interpreting the ANSI codepage may see mojibake for
 *     non-ASCII values
 *   - env_load precedence is resolved per line: with override false a variable
 *     already set (by the OS or by an earlier line) keeps its value, so the
 *     FIRST occurrence in the file wins; with override true the LAST wins
 *   - SECURITY: treat ANY data handed to env_load or env_from_* as trusted like
 *     source code. The loader applies any well-formed key - including
 *     loader-sensitive ones such as PATH, LD_PRELOAD or LD_LIBRARY_PATH, which
 *     child processes inherit - so a .env file must never be writable by
 *     untrusted users, and network, DB or user-supplied data must never reach
 *     env_from_* without an explicit caller-side key allowlist
 *
 * Thread Safety:
 *   - Reads (env_get_*, env_exists) are safe once the environment is stable
 *   - Writes (env_load, env_set, env_unset) mutate global process state and are
 *     NOT thread-safe: call them from the main thread before worker threads
 *     spawn; a write invalidates pointers returned earlier by env_get_1/_2
 *
 * Memory Management:
 *   - env_get_1/_2 return a borrowed pointer into the process environment; treat
 *     it as read-only and never free it - it stays valid until the next
 *     env_load/env_set/env_unset
 *   - The C runtime copies name and value on write; no module-side state is kept
 *
 * Performance Characteristics:
 *   - env_load is one file read plus a single pass over its bytes; every other
 *     call is O(1) beyond the C runtime's own environment lookup
 *
 * Dependencies:
 *   - <stdlib.h> (getenv, plus _putenv_s on Windows / setenv, unsetenv on POSIX)
 *   - <file/file.h> (chains char, str, string, memory and the log/trace stack)
 *   - <result.h>
 *
 * See env.c for implementation details.
 */

#ifndef ENV_H
#define ENV_H

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/
#include <file/file.h>
#include <result.h>

/*==============================================================================
 * MARK: - Public API
 *============================================================================*/
/**
 * @brief Report whether a variable is set to a non-empty value.
 * @param name Variable name (exact case).
 * @return true when the variable is set AND non-empty; false otherwise.
 */
bool env_exists(char const *const name);

/**
 * @brief Apply .env-dialect data from a buffer (char*); the environment wins.
 *
 * Identical dialect, precedence, warning and PARTIAL semantics to env_load_1 -
 * the source is a byte range instead of a file. The data is parsed from an
 * internal copy; the caller's buffer is never modified. Warnings label the
 * source as "(data)" since there is no path.
 *
 * @param data Bytes in the .env dialect (need not be NUL-terminated). May be
 *             NULL only when size is 0, which applies nothing and succeeds.
 * @param size Number of bytes to parse.
 * @return As env_load_1, minus the file error class; plus a MEMORY-category
 *         Result when the internal copy cannot be taken (nothing is applied).
 */
Result env_from_char_1(char const *const data, USize const size);

/**
 * @brief Apply .env-dialect data from a buffer (char*) with a precedence policy.
 * @param data     Bytes in the .env dialect (need not be NUL-terminated). May be
 *                 NULL only when size is 0, which applies nothing and succeeds.
 * @param size     Number of bytes to parse.
 * @param override When true the data wins over already-set variables.
 * @return As env_from_char_1.
 */
Result env_from_char_2(char const *const data, USize const size, bool const override);

/**
 * @brief Apply .env-dialect data from a Str; the environment wins.
 * @param data Data in the .env dialect.
 * @return As env_from_char_1.
 */
Result env_from_str_1(Str const *const data);

/**
 * @brief Apply .env-dialect data from a Str with a precedence policy.
 * @param data     Data in the .env dialect.
 * @param override When true the data wins over already-set variables.
 * @return As env_from_char_1.
 */
Result env_from_str_2(Str const *const data, bool const override);

/**
 * @brief Apply .env-dialect data from a String; the environment wins.
 * @param data Data in the .env dialect.
 * @return As env_from_char_1.
 */
Result env_from_string_1(String const *const data);

/**
 * @brief Apply .env-dialect data from a String with a precedence policy.
 * @param data     Data in the .env dialect.
 * @param override When true the data wins over already-set variables.
 * @return As env_from_char_1.
 */
Result env_from_string_2(String const *const data, bool const override);

/**
 * @brief Read a variable (borrowed).
 * @param name Variable name (exact case).
 * @return Borrowed read-only pointer into the process environment, or NULL when
 *         the variable is unset or empty. Never free it; it stays valid until
 *         the next env_load/env_set/env_unset.
 */
char* env_get_1(char const *const name);

/**
 * @brief Read a variable (borrowed) with a fallback.
 * @param name     Variable name (exact case).
 * @param fallback Returned as-is when the variable is unset or empty (NULL allowed).
 * @return Borrowed read-only pointer as env_get_1, or `fallback`.
 */
char* env_get_2(char const *const name, char const *const fallback);

/**
 * @brief Read a variable as a boolean.
 *
 * Case-insensitive: "1", "true", "yes", "on" are true; "0", "false", "no",
 * "off" are false. Anything else - including unset or empty - is the fallback.
 *
 * @param name     Variable name (exact case).
 * @param fallback Value returned when the variable is unset, empty or unrecognized.
 * @return The parsed boolean or `fallback`.
 */
bool env_get_bool(char const *const name, bool const fallback);

/**
 * @brief Read a variable as a floating-point number (strict parse).
 * @param name     Variable name (exact case).
 * @param fallback Value returned when the variable is unset, empty or not a
 *                 valid number (trailing text rejects the parse).
 * @return The parsed number or `fallback`.
 */
FSize env_get_number_f(char const *const name, FSize const fallback);

/**
 * @brief Read a variable as a signed integer (strict parse).
 * @param name     Variable name (exact case).
 * @param fallback Value returned when the variable is unset, empty or not a
 *                 whole base-10 integer (trailing text rejects the parse).
 * @return The parsed number or `fallback`.
 */
ISize env_get_number_i(char const *const name, ISize const fallback);

/**
 * @brief Read a variable as an unsigned integer (strict parse).
 * @param name     Variable name (exact case).
 * @param fallback Value returned when the variable is unset, empty, negative or
 *                 not a whole base-10 integer (trailing text rejects the parse).
 * @return The parsed number or `fallback`.
 */
USize env_get_number_u(char const *const name, USize const fallback);

/**
 * @brief Load a .env file; the existing environment wins over the file.
 * @param path File path, used exactly as given.
 * @return RESULT_SUCCESS; the OS classification when the file is missing or
 *         unreadable (never SUCCESS on a failed open); an IO-category Result
 *         when the path opens but is not a regular file (a directory); a
 *         RESULT_FLAG_PARTIAL Result (APPLICATION category, code = skipped line
 *         count) when malformed lines were skipped.
 */
Result env_load_1(char const *const path);

/**
 * @brief Load a .env file with an explicit precedence policy.
 * @param path     File path, used exactly as given.
 * @param override When true the file wins over already-set variables; when
 *                 false, already-set variables keep their values.
 * @return As env_load_1.
 */
Result env_load_2(char const *const path, bool const override);

/**
 * @brief Set a variable (char*). Main-thread only; see Thread Safety.
 * @param name  Variable name; must be non-empty and contain no '=' and no
 *              control characters.
 * @param value Value to set; the C runtime stores a copy. An empty value reads
 *              back as unset (and removes the variable on Windows).
 * @return RESULT_SUCCESS, ARGUMENT category for an invalid name, or SYSTEM
 *         category for an OS-level failure.
 */
Result env_set_1(char const *const name, char const *const value);

/**
 * @brief Set a variable (Str). Main-thread only; see Thread Safety.
 * @param name  Variable name; must be non-empty and contain no '=' and no
 *              control characters.
 * @param value Value to set; copied to a NUL-terminated buffer internally. A
 *              value containing an embedded NUL is rejected (ARGUMENT).
 * @return As env_set_1; plus a MEMORY-category Result when the internal copy
 *         cannot be taken (the variable is left as it was).
 */
Result env_set_2(char const *const name, Str const *const value);

/**
 * @brief Set a variable (String). Main-thread only; see Thread Safety.
 * @param name  Variable name; must be non-empty and contain no '=' and no
 *              control characters.
 * @param value Value to set; the first string_get_size bytes are copied to a
 *              NUL-terminated buffer internally (a String carries no NUL
 *              guarantee of its own). A value containing an embedded NUL is
 *              rejected (ARGUMENT).
 * @return As env_set_2, including the MEMORY-category Result when the
 *         internal copy cannot be taken.
 */
Result env_set_3(char const *const name, String const *const value);

/**
 * @brief Remove a variable. Main-thread only; see Thread Safety.
 *
 * Removing a variable that is not set succeeds.
 *
 * @param name Variable name; must be non-empty and contain no '=' and no
 *             control characters.
 * @return RESULT_SUCCESS, ARGUMENT category for an invalid name, or SYSTEM
 *         category for an OS-level failure.
 */
Result env_unset(char const *const name);

#endif // ENV_H