#include <env/env.h>

/*==============================================================================
 * MARK: - Private Functions
 *============================================================================*/
static bool _env_key_character(char const c, bool const first) {
    bool const alpha = char_is_alpha(c) || c == '_';

    return first ? alpha : (alpha || char_is_number(c));
}

static char* _env_whitespace_skip(char *const cursor) {
    char *result = cursor;

    while (char_is_whitespace(*result)) {
        result += 1;
    }

    return result;
}

/* In place and allocation-free on purpose: `value` points into the middle of
 * the loader's buffer, so the reallocating char_trim_1/_2 (which release the
 * pointer they are given) cannot be used here. The terminator overwrites real
 * content, so it stays unconditional per the style guide's terminator rule. */
static void _env_whitespace_trim_end(char *const value) {
    USize size = char_length(value);

    while (size > 0 && char_is_whitespace(value[size - 1])) {
        size -= 1;
    }

    value[size] = '\0';
}

static bool _env_name_valid(char const *const name) {
    if (name[0] == '\0') {
        return false;
    }

    for (USize i = 0; name[i] != '\0'; i += 1) {
        if (name[i] == '=' || (U8) name[i] < 0x20) {
            return false;
        }
    }

    return true;
}

static Result _env_os_set(char const *const name, char const *const value) {
#ifdef OS_WINDOWS
    I32 const error = (I32) _putenv_s(name, value);

    return error == 0 ? RESULT_SUCCESS : result_make(RESULT_CATEGORY_SYSTEM, (U32) error, 0);
#else
    if (setenv(name, value, 1) != 0) {
        return result_from_os();
    }

    return RESULT_SUCCESS;
#endif
}

/* Shared body of env_set_2/_3: applies a sized byte range as a variable value. */
static Result _env_set_bytes(char const *const name, char const *const data, USize const size) {
    trace_log_push(LOG_METADATA);

    /* An embedded NUL would silently truncate at the OS boundary; reject it the
     * same way the loader rejects a NUL-carrying line. */
    for (USize i = 0; i < size; i += 1) {
        if (data[i] == '\0') {
            trace_log_pop();

            return result_make(RESULT_CATEGORY_ARGUMENT, 0, 0);
        }
    }

    /* try_alloc: the value is the caller's to size, and the Result already has
     * a MEMORY class - a copy that cannot be taken is reported, not fatal. */
    char *const buffer = (char*) memory_try_alloc(size + CHAR_END_CHARACTER);

    if (memory_empty(buffer)) {
        trace_log_pop();

        return result_make(RESULT_CATEGORY_MEMORY, 0, 0);
    }

    if (size > 0) {
        memory_copy_1(buffer, data, size);
    }

    Result const result = env_set_1(name, buffer);

    memory_free(buffer);

    trace_log_pop();

    return result;
}

/* Unescapes a quoted value in place. `value` points at the first character AFTER
 * the opening quote; on success the value is NUL-terminated in place and
 * *out_rest points just past the closing quote. Escapes (\n \t \r \" \\) are
 * processed for double quotes only; an unknown escape is kept literally.
 * Returns false when the closing quote is missing. */
static bool _env_value_unquote(char *const value, char const quote, char **const out_rest) {
    USize read  = 0;
    USize write = 0;

    while (value[read] != '\0' && value[read] != quote) {
        if (quote == '"' && value[read] == '\\' && value[read + 1] != '\0') {
            char const escape = value[read + 1];

            if (escape == 'n') {
                value[write] = '\n';
            }
            else if (escape == 't') {
                value[write] = '\t';
            }
            else if (escape == 'r') {
                value[write] = '\r';
            }
            else if (escape == '"' || escape == '\\') {
                value[write] = escape;
            }
            else {
                value[write] = '\\';

                write += 1;

                value[write] = escape;
            }

            read  += 2;
            write += 1;

            continue;
        }

        value[write] = value[read];

        read  += 1;
        write += 1;
    }

    if (value[read] != quote) {
        return false;
    }

    *out_rest = value + read + 1;

    value[write] = '\0';

    return true;
}

/* Parses one line and applies it to the process environment. `source` labels
 * warnings (a file path for env_load, "(data)" for the env_from family).
 * Returns false only for a malformed or failed line (counted toward the PARTIAL
 * result); blank lines, comment lines and precedence skips return true. */
static bool _env_line_parse(char *const line, bool const override, char const *const source, USize const line_number) {
    trace_log_push(LOG_METADATA);

    char *cursor = _env_whitespace_skip(line);

    if (cursor[0] == '\0' || cursor[0] == '#') {
        trace_log_pop();

        return true;
    }

    if (char_starts_with_1(cursor, "export") && char_is_whitespace(cursor[6])) {
        cursor = _env_whitespace_skip(cursor + 6);
    }

    if (!_env_key_character(cursor[0], true)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "env: %s:%llu: invalid key\n", source, (unsigned long long) line_number);

        trace_log_pop();

        return false;
    }

    char *const key = cursor;

    while (_env_key_character(cursor[0], false)) {
        cursor += 1;
    }

    char *const key_end = cursor;

    cursor = _env_whitespace_skip(cursor);

    if (cursor[0] != '=') {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "env: %s:%llu: missing '='\n", source, (unsigned long long) line_number);

        trace_log_pop();

        return false;
    }

    char *const value_raw = cursor + 1;

    cursor = _env_whitespace_skip(value_raw);

    bool const spaced = cursor != value_raw;
    char      *value  = cursor;

    *key_end = '\0';

    if (cursor[0] == '"' || cursor[0] == '\'') {
        char *rest = nullptr;

        value = cursor + 1;

        if (!_env_value_unquote(value, cursor[0], &rest)) {
            log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "env: %s:%llu: unterminated quote (key '%s')\n", source, (unsigned long long) line_number, key);

            trace_log_pop();

            return false;
        }

        rest = _env_whitespace_skip(rest);

        if (rest[0] != '\0' && rest[0] != '#') {
            log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "env: %s:%llu: unexpected text after quoted value (key '%s')\n", source, (unsigned long long) line_number, key);

            trace_log_pop();

            return false;
        }
    }
    else {
        /* Unquoted: an inline comment starts at a '#' that follows whitespace. */
        USize i = 0;

        while (value[i] != '\0') {
            bool const comment = i > 0 ? char_is_whitespace(value[i - 1]) : spaced;

            if (value[i] == '#' && comment) {
                value[i] = '\0';

                break;
            }

            i += 1;
        }

        _env_whitespace_trim_end(value);
    }

    if (!override) {
        char const *const existing = getenv(key);

        if (!memory_empty(existing) && existing[0] != '\0') {
            trace_log_pop();

            return true;
        }
    }

    Result const result = _env_os_set(key, value);

    if (result_is_error(result)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "env: %s:%llu: setting variable failed (key '%s')\n", source, (unsigned long long) line_number, key);

        trace_log_pop();

        return false;
    }

    trace_log_pop();

    return true;
}

/* Shared loader core for env_load and the env_from family: walks a byte range
 * line by line (length-aware, never terminator-driven) and applies each line.
 * `bytes` may be null only when `size` is 0. CONTRACT: index `size` must be a
 * writable terminator slot - env_load gets it from _file_read_to's
 * file_size + CHAR_END_CHARACTER allocation, env_from from its own copy. */
static Result _env_apply(char *const bytes, USize const size, bool const override, char const *const source) {
    trace_log_push(LOG_METADATA);

    USize position    = 0;
    USize line_number = 0;
    USize skipped     = 0;

    if (size >= 3 && (U8) bytes[0] == 0xEF && (U8) bytes[1] == 0xBB && (U8) bytes[2] == 0xBF) {
        position = 3;
    }

    while (position < size) {
        line_number += 1;

        USize end = position;

        while (end < size && bytes[end] != '\n') {
            end += 1;
        }

        bool  const more      = end < size;
        USize       line_size = end - position;

        if (line_size > 0 && bytes[position + line_size - 1] == '\r') {
            line_size -= 1;
        }

        bool nul_found = false;

        for (USize i = 0; i < line_size; i += 1) {
            if (bytes[position + i] == '\0') {
                nul_found = true;

                break;
            }
        }

        if (nul_found) {
            log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "env: %s:%llu: embedded NUL byte\n", source, (unsigned long long) line_number);

            skipped += 1;
        }
        else {
            /* In bounds even for a final line with no newline: index `size` is
             * the terminator slot this helper's contract requires. */
            bytes[position + line_size] = '\0';

            if (!_env_line_parse(bytes + position, override, source, line_number)) {
                skipped += 1;
            }
        }

        position = more ? end + 1 : end;
    }

    /* The Result code field is 16 bits; saturate rather than truncate. */
    U32 const skipped_code = skipped > 0xFFFF ? 0xFFFF : (U32) skipped;

    Result const result = skipped == 0 ? RESULT_SUCCESS : result_make(RESULT_CATEGORY_APPLICATION, skipped_code, RESULT_FLAG_PARTIAL);

    trace_log_pop();

    return result;
}

/*==============================================================================
 * MARK: - Public API
 *============================================================================*/
bool env_exists(char const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "name", (void*) name);

    bool const exists = !memory_empty(env_get_1(name));

    trace_log_pop();

    return exists;
}

Result env_from_char_1(char const *const data, USize const size) {
    trace_log_push(LOG_METADATA);

    /* See env_from_char_2: data is nullable only at size 0, and an empty range
     * is answered there. */
    error_check_wrong_value(LOG_METADATA, "data", size > 0 && memory_empty(data));

    Result const result = env_from_char_2(data, size, false);

    trace_log_pop();

    return result;
}

Result env_from_char_2(char const *const data, USize const size, bool const override) {
    trace_log_push(LOG_METADATA);

    /* data is nullable only at size 0: an empty Str or String hands its data as
     * NULL and the container wrappers pass it through unchanged. A NULL that
     * comes WITH a size is the programming error, so that is what aborts; the
     * empty range itself applies nothing and succeeds. */
    error_check_wrong_value(LOG_METADATA, "data", size > 0 && memory_empty(data));

    if (size == 0) {
        trace_log_pop();

        return RESULT_SUCCESS;
    }

    /* The parser mutates in place (quote unescaping, line termination), so it
     * runs on an owned copy carrying the terminator slot _env_apply requires;
     * the caller's buffer is never touched. try_alloc: a copy that cannot be
     * taken is a MEMORY Result, not a process exit. */
    char *const buffer = (char*) memory_try_alloc(size + CHAR_END_CHARACTER);

    if (memory_empty(buffer)) {
        trace_log_pop();

        return result_make(RESULT_CATEGORY_MEMORY, 0, 0);
    }

    memory_copy_1(buffer, data, size);

    Result const result = _env_apply(buffer, size, override, "(data)");

    memory_free(buffer);

    trace_log_pop();

    return result;
}

Result env_from_str_1(Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    Result const result = env_from_str_2(data, false);

    trace_log_pop();

    return result;
}

Result env_from_str_2(Str const *const data, bool const override) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    Result const result = env_from_char_2(str_get_data(data), str_get_size(data), override);

    trace_log_pop();

    return result;
}

Result env_from_string_1(String const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    Result const result = env_from_string_2(data, false);

    trace_log_pop();

    return result;
}

Result env_from_string_2(String const *const data, bool const override) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    Result const result = env_from_char_2(string_get_data(data), string_get_size(data), override);

    trace_log_pop();

    return result;
}

char* env_get_1(char const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "name", (void*) name);

    char *const value  = getenv(name);
    char *const result = (!memory_empty(value) && value[0] != '\0') ? value : nullptr;

    trace_log_pop();

    return result;
}

char* env_get_2(char const *const name, char const *const fallback) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "name", (void*) name);

    char *const value  = env_get_1(name);
    char *const result = memory_empty(value) ? (char*) fallback : value;

    trace_log_pop();

    return result;
}

bool env_get_bool(char const *const name, bool const fallback) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "name", (void*) name);

    char const *const value  = env_get_1(name);
    bool              result = fallback;

    if (!memory_empty(value)) {
        bool parsed = false;

        if (char_try_to_bool(value, &parsed)) {
            result = parsed;
        }
    }

    trace_log_pop();

    return result;
}

FSize env_get_number_f(char const *const name, FSize const fallback) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "name", (void*) name);

    char const *const value  = env_get_1(name);
    FSize             result = fallback;

    if (!memory_empty(value)) {
        FSize parsed = 0.0;

        if (char_try_to_number_f(value, &parsed)) {
            result = parsed;
        }
    }

    trace_log_pop();

    return result;
}

ISize env_get_number_i(char const *const name, ISize const fallback) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "name", (void*) name);

    char const *const value  = env_get_1(name);
    ISize             result = fallback;

    if (!memory_empty(value)) {
        ISize parsed = 0;

        if (char_try_to_number_i(value, &parsed)) {
            result = parsed;
        }
    }

    trace_log_pop();

    return result;
}

USize env_get_number_u(char const *const name, USize const fallback) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "name", (void*) name);

    char const *const value  = env_get_1(name);
    USize             result = fallback;

    if (!memory_empty(value)) {
        USize parsed = 0;

        if (char_try_to_number_u(value, &parsed)) {
            result = parsed;
        }
    }

    trace_log_pop();

    return result;
}

Result env_load_1(char const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    Result const result = env_load_2(path, false);

    trace_log_pop();

    return result;
}

Result env_load_2(char const *const path, bool const override) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    File *file = file_open_try_1(path, "rb");

    if (memory_empty(file)) {
        /* On Windows the CRT can reject a path (e.g. an empty one) through
         * errno alone, leaving GetLastError at 0 - a failed open must never
         * classify as success. */
        Result result = result_from_os();

        if (result_is_success(result)) {
            result = result_make(RESULT_CATEGORY_IO, 0, 0);
        }

        trace_log_pop();

        return result;
    }

    if (!file_regular(file)) {
        file_close(&file);

        trace_log_pop();

        return result_make(RESULT_CATEGORY_IO, 0, 0);
    }

    /* Read from the handle in hand: one open decides everything, so nothing can
     * swap the path between classifying the file and reading it, and binary
     * mode keeps a stray 0x1A byte from truncating the read on Windows. The
     * length-aware parse then keeps an embedded NUL from silently ending it
     * early - such a line is counted as malformed instead. */
    /* The size is taken from the handle before the read and compared with what
     * came back: file_read_all_3 answers an over-FILE_READ_BYTES_MAX file with
     * an empty String after one WARN, and a short read with fewer bytes than
     * the file holds. Applied as they are, either would be the silent empty
     * success the directory branch above exists to refuse. A handle whose size
     * cannot be determined reads as 0 bytes and loads as an empty file:
     * file_get_size reports that case as 0, not as a failure. */
    USize const expected = file_get_size(file);
    String      data     = file_read_all_3(file);

    file_close(&file);

    if (string_get_size(&data) != expected) {
        string_uninit(&data);

        trace_log_pop();

        return result_make(RESULT_CATEGORY_IO, 0, 0);
    }

    Result const result = _env_apply(string_get_data(&data), string_get_size(&data), override, path);

    string_uninit(&data);

    trace_log_pop();

    return result;
}

Result env_set_1(char const *const name, char const *const value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "name", (void*) name);
    error_check_null(LOG_METADATA, "value", (void*) value);

    Result result = result_make(RESULT_CATEGORY_ARGUMENT, 0, 0);

    if (_env_name_valid(name)) {
        result = _env_os_set(name, value);
    }

    trace_log_pop();

    return result;
}

Result env_set_2(char const *const name, Str const *const value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "name", (void*) name);
    error_check_null(LOG_METADATA, "value", (void*) value);

    Result const result = _env_set_bytes(name, str_get_data(value), str_get_size(value));

    trace_log_pop();

    return result;
}

Result env_set_3(char const *const name, String const *const value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "name", (void*) name);
    error_check_null(LOG_METADATA, "value", (void*) value);

    /* Bounded by the String's size: a String carries no NUL guarantee, so the
     * data pointer must never be handed to the C runtime directly. */
    Result const result = _env_set_bytes(name, string_get_data(value), string_get_size(value));

    trace_log_pop();

    return result;
}

Result env_unset(char const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "name", (void*) name);

    Result result = result_make(RESULT_CATEGORY_ARGUMENT, 0, 0);

    if (_env_name_valid(name)) {
#ifdef OS_WINDOWS
        result = _env_os_set(name, "");
#else
        result = unsetenv(name) == 0 ? RESULT_SUCCESS : result_from_os();
#endif
    }

    trace_log_pop();

    return result;
}