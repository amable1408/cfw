#include <regex/regex.h>

/*==============================================================================
 * MARK: - Static/Internal Functions
 *============================================================================*/
static void _regex_release(Regex *const self) {
    /* Recompiling into a live Regex used to leak the previous re + match_data;
     * every compile path funnels through here first. Safe on a zeroed object. */
    if (!memory_empty(self->match_data)) {
        pcre2_match_data_free(self->match_data);
    }

    if (!memory_empty(self->re)) {
        pcre2_code_free(self->re);
    }

    self->re         = nullptr;
    self->match_data = nullptr;
    self->matched    = false;
}

static bool _regex_compile_try(Regex *const self, char const *const pattern, USize const pattern_size, U32 const options) {
    _regex_release(self);

    /* Compile straight from the caller's buffer: PCRE2 copies the pattern into the
     * compiled code, so the Regex never retains or frees the caller's buffer
     * (fixes the stack/literal alias-then-free). An EMPTY pattern is legal - it
     * matches at every position - so no size precondition stands here. */
    self->re = pcre2_compile(
        (PCRE2_SPTR) pattern,           /* the pattern */
        pattern_size,                   /* explicit pattern length */
        options,                        /* REGEX_COMPILE_* flags, PCRE2 values */
        &self->error_code,              /* for error number */
        &self->error_offset,            /* for error offset */
        nullptr);                       /* use default compile context */

    if (memory_empty(self->re)) {
        return false;
    }

    self->match_data = pcre2_match_data_create_from_pattern(self->re, nullptr);

    /* PCRE2's own allocator failed: degrade to the refusing object rather than
     * carry a compiled pattern that cannot be matched (the deferred compile_try
     * match_data OOM item). error_code carries the honest reason. */
    if (memory_empty(self->match_data)) {
        pcre2_code_free(self->re);

        self->re         = nullptr;
        self->error_code = PCRE2_ERROR_NOMEMORY;

        return false;
    }

    return true;
}

static void _regex_compile(Regex *const self, char const *const pattern, USize const pattern_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "pattern", (void*) pattern);

    if (!_regex_compile_try(self, pattern, pattern_size, 0)) {
        PCRE2_UCHAR buffer[REGEX_ERROR_MESSAGE_SIZE] = DEFAULT_INITIALIZATION;

        pcre2_get_error_message(self->error_code, buffer, sizeof(buffer));

        log_message_1(LOG_LEVEL_ERROR, (char*) &buffer[0], "\n");

        /* The compile_1..4 family keeps its documented abort on a malformed
         * pattern - it exists for LITERAL patterns, where a failure is caller
         * error. Patterns that arrive as DATA go through the compile_try_*
         * family instead. In an unchecked build the object is left refusing
         * (null re/match_data), never half-built. */
        error_check_wrong_value(LOG_METADATA, "pcre2_compile", self->error_offset);
    }

    trace_log_pop();
}

static bool _regex_match_options(Regex *const self, char const *const subject, USize const subject_size, USize const subject_offset, U32 const options) {
    /* A failed compile_try (or a fresh regex_init) is a NORMAL runtime state -
     * patterns arrive from config rows and user input - so a null object REFUSES
     * instead of handing PCRE2 a null re/match_data (a segfault). */
    if (memory_empty(self->re) || memory_empty(self->match_data)) {
        return false;
    }

    PCRE2_SIZE *const ovector = pcre2_get_ovector_pointer(self->match_data);

    I32 const result = pcre2_match(
        self->re,               /* the compiled pattern */
        (PCRE2_SPTR) subject,   /* the subject string */
        subject_size,           /* the length of the subject */
        subject_offset,         /* start offset in the subject */
        options,                /* match options */
        self->match_data,       /* block for output data */
        nullptr);               /* use default match context */

    if (result < 0) {
        /* A real match ERROR (match limit, bad UTF, ...) is distinguishable from
         * plain no-match afterwards: error_code holds the negative PCRE2 code and
         * regex_get_error_message renders it. */
        if (result != PCRE2_ERROR_NOMATCH) {
            self->error_code = result;
        }

        return false;
    }

    self->match.begin = ovector[0];                             /* ovector[0] is the start of the match */
    self->match.end   = ovector[1];                             /* ovector[1] is the end of the match */
    self->match.size  = self->match.end - self->match.begin;    /* size of the match */
    self->matched     = true;

    return true;
}

static bool _regex_match(Regex *const self, char const *const subject, USize const subject_size, USize const subject_offset) {
    return _regex_match_options(self, subject, subject_size, subject_offset, 0);
}

static USize _regex_match_all(Regex *const self, char const *const subject, USize const subject_size, USize const subject_offset, FpRegexMatch const callback, void *const context) {
    USize match_count = 0;
    USize offset      = subject_offset;

    while (offset <= subject_size) {
        bool const matched = _regex_match(self, subject, subject_size, offset);

        /* Empty match: the PCRE2-documented retry. A NON-empty match can also
         * start at this position, and the old blind offset+1 bump skipped it -
         * ask again anchored and non-empty before settling for the empty one. */
        if (matched && self->match.size == 0 && self->match.begin == offset) {
            bool const retried = _regex_match_options(self, subject, subject_size, offset, PCRE2_NOTEMPTY_ATSTART | PCRE2_ANCHORED);

            if (!retried) {
                match_count += 1;

                callback(self, subject, subject_size, context);

                offset += 1;

                continue;
            }
        }

        if (!matched) {
            break;
        }

        match_count += 1;

        callback(self, subject, subject_size, context);

        if (self->match.end == offset) {
            offset += 1;
        }
        else {
            offset = self->match.end;
        }
    }

    return match_count;
}

/*==============================================================================
 * MARK: - Compilation
 *============================================================================*/
void regex_compile_1(Regex *const self, char const *const pattern) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "pattern", (void*) pattern);

    regex_compile_2(self, pattern, char_length(pattern));

    trace_log_pop();
}

void regex_compile_2(Regex *const self, char const *const pattern, USize const pattern_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "pattern", (void*) pattern);

    _regex_compile(self, pattern, pattern_size);

    trace_log_pop();
}

void regex_compile_3(Regex *const self, Str const *const pattern) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "pattern", (void*) pattern);

    /* An empty Str carries data == nullptr; substitute the valid empty source. */
    _regex_compile(self, str_get_size(pattern) == 0 ? "" : str_get_data(pattern), str_get_size(pattern));

    trace_log_pop();
}

void regex_compile_4(Regex *const self, String const *const pattern) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "pattern", (void*) pattern);

    /* An empty String carries data == nullptr; substitute the valid empty source. */
    _regex_compile(self, string_get_size(pattern) == 0 ? "" : string_get_data(pattern), string_get_size(pattern));

    trace_log_pop();
}

bool regex_compile_try(Regex *const self, char const *const pattern) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "pattern", (void*) pattern);

    bool const compiled = _regex_compile_try(self, pattern, char_length(pattern), 0);

    trace_log_pop();

    return compiled;
}

bool regex_compile_try_2(Regex *const self, char const *const pattern, USize const pattern_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "pattern", (void*) pattern);

    bool const compiled = _regex_compile_try(self, pattern, pattern_size, 0);

    trace_log_pop();

    return compiled;
}

bool regex_compile_try_3(Regex *const self, Str const *const pattern) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "pattern", (void*) pattern);

    /* An empty Str carries data == nullptr; substitute the valid empty source. */
    bool const compiled = _regex_compile_try(self, str_get_size(pattern) == 0 ? "" : str_get_data(pattern), str_get_size(pattern), 0);

    trace_log_pop();

    return compiled;
}

bool regex_compile_try_4(Regex *const self, String const *const pattern) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "pattern", (void*) pattern);

    /* An empty String carries data == nullptr; substitute the valid empty source. */
    bool const compiled = _regex_compile_try(self, string_get_size(pattern) == 0 ? "" : string_get_data(pattern), string_get_size(pattern), 0);

    trace_log_pop();

    return compiled;
}

bool regex_compile_try_options(Regex *const self, char const *const pattern, U32 const options) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "pattern", (void*) pattern);

    bool const compiled = _regex_compile_try(self, pattern, char_length(pattern), options);

    trace_log_pop();

    return compiled;
}

Result regex_copy_match_group_name_1(Regex *const self, char const *const name, char *const buffer, USize *const buffer_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);
    error_check_null(LOG_METADATA, "buffer", (void*) buffer);
    error_check_null(LOG_METADATA, "buffer_size", (void*) buffer_size);

    /* A never-matched, compiled-but-unmatched, or failed-compile object refuses.
     * The null check alone was NOT enough: after a successful compile, match_data
     * is non-null but VIRGIN - pcre2_match_data_create does not zero it, and the
     * substring accessors read fields only pcre2_match assigns (a probe segfaulted
     * deterministically). The matched flag is the real gate. */
    if (!self->matched || memory_empty(self->match_data)) {
        trace_log_pop();

        return result_make(RESULT_CATEGORY_APPLICATION, (U32) -PCRE2_ERROR_NULL, 0);
    }

    I32 const code = pcre2_substring_copy_byname(self->match_data, (PCRE2_SPTR) name, (PCRE2_UCHAR*) buffer, (PCRE2_SIZE*) buffer_size);

    trace_log_pop();

    /* Negative PCRE2 codes are stored by magnitude - the raw negative used to be
     * cast through U32 into the 16-bit code field as noise, flagged RETRYABLE
     * unconditionally (NOSUBSTRING is not transient). */
    return code < 0 ? result_make(RESULT_CATEGORY_APPLICATION, (U32) -code, 0) : RESULT_SUCCESS;
}

Result regex_copy_match_group_name_2(Regex *const self, char const *const name, Str *const buffer) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);
    error_check_null(LOG_METADATA, "buffer", (void*) buffer);

    if (!self->matched || memory_empty(self->match_data)) {
        trace_log_pop();

        return result_make(RESULT_CATEGORY_APPLICATION, (U32) -PCRE2_ERROR_NULL, 0);
    }

    /* In/out through a LOCAL: the old code handed &buffer->size straight to PCRE2,
     * so a failure (NOMEMORY, NOSUBSTRING) could leave size describing bytes that
     * were never written - an invariant break on a live Str. The Str's size is the
     * usable capacity on entry (the caller pre-sizes it) and the substring length
     * on success. */
    PCRE2_SIZE size_inout = buffer->size;

    I32 const code = pcre2_substring_copy_byname(self->match_data, (PCRE2_SPTR) name, (PCRE2_UCHAR*) buffer->data, &size_inout);

    if (code < 0) {
        trace_log_pop();

        return result_make(RESULT_CATEGORY_APPLICATION, (U32) -code, 0);
    }

    buffer->size = size_inout;

    trace_log_pop();

    return RESULT_SUCCESS;
}

Result regex_copy_match_group_name_3(Regex *const self, char const *const name, String *const buffer) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);
    error_check_null(LOG_METADATA, "buffer", (void*) buffer);

    if (!self->matched || memory_empty(self->match_data)) {
        trace_log_pop();

        return result_make(RESULT_CATEGORY_APPLICATION, (U32) -PCRE2_ERROR_NULL, 0);
    }

    /* The String's CAPACITY is the input bound (its size was the old input, which
     * was 0 on any fresh String - an instant NOMEMORY); the substring length
     * becomes the size on success. */
    PCRE2_SIZE size_inout = buffer->capacity;

    I32 const code = pcre2_substring_copy_byname(self->match_data, (PCRE2_SPTR) name, (PCRE2_UCHAR*) buffer->data, &size_inout);

    if (code < 0) {
        trace_log_pop();

        return result_make(RESULT_CATEGORY_APPLICATION, (U32) -code, 0);
    }

    string_set_size(buffer, size_inout);

    trace_log_pop();

    return RESULT_SUCCESS;
}

Result regex_copy_match_group_number_1(Regex *const self, USize const index, char *const buffer, USize *const buffer_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "buffer", (void*) buffer);
    error_check_null(LOG_METADATA, "buffer_size", (void*) buffer_size);

    if (!self->matched || memory_empty(self->match_data)) {
        trace_log_pop();

        return result_make(RESULT_CATEGORY_APPLICATION, (U32) -PCRE2_ERROR_NULL, 0);
    }

    I32 const code = pcre2_substring_copy_bynumber(self->match_data, (U32) index, (PCRE2_UCHAR*) buffer, (PCRE2_SIZE*) buffer_size);

    trace_log_pop();

    return code < 0 ? result_make(RESULT_CATEGORY_APPLICATION, (U32) -code, 0) : RESULT_SUCCESS;
}

Result regex_copy_match_group_number_2(Regex *const self, USize const index, Str *const buffer) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "buffer", (void*) buffer);

    if (!self->matched || memory_empty(self->match_data)) {
        trace_log_pop();

        return result_make(RESULT_CATEGORY_APPLICATION, (U32) -PCRE2_ERROR_NULL, 0);
    }

    /* See regex_copy_match_group_name_2 for the in/out-through-a-local rationale. */
    PCRE2_SIZE size_inout = buffer->size;

    I32 const code = pcre2_substring_copy_bynumber(self->match_data, (U32) index, (PCRE2_UCHAR*) buffer->data, &size_inout);

    if (code < 0) {
        trace_log_pop();

        return result_make(RESULT_CATEGORY_APPLICATION, (U32) -code, 0);
    }

    buffer->size = size_inout;

    trace_log_pop();

    return RESULT_SUCCESS;
}

Result regex_copy_match_group_number_3(Regex *const self, USize const index, String *const buffer) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "buffer", (void*) buffer);

    if (!self->matched || memory_empty(self->match_data)) {
        trace_log_pop();

        return result_make(RESULT_CATEGORY_APPLICATION, (U32) -PCRE2_ERROR_NULL, 0);
    }

    /* See regex_copy_match_group_name_3 for the capacity-as-input rationale. */
    PCRE2_SIZE size_inout = buffer->capacity;

    I32 const code = pcre2_substring_copy_bynumber(self->match_data, (U32) index, (PCRE2_UCHAR*) buffer->data, &size_inout);

    if (code < 0) {
        trace_log_pop();

        return result_make(RESULT_CATEGORY_APPLICATION, (U32) -code, 0);
    }

    string_set_size(buffer, size_inout);

    trace_log_pop();

    return RESULT_SUCCESS;
}

/*==============================================================================
 * MARK: - Match Data Access
 *============================================================================*/
bool regex_get_error_message(Regex const *const self, char *const buffer, USize const buffer_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "buffer", (void*) buffer);
    error_check_non_value_uint(LOG_METADATA, "buffer_size", buffer_size);

    I32 const written = pcre2_get_error_message(self->error_code, (PCRE2_UCHAR*) buffer, buffer_size);

    trace_log_pop();

    return written >= 0;
}

USize regex_get_error_offset(Regex const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->error_offset;
}

USize regex_get_match_begin(Regex *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->match.begin;
}

USize regex_get_match_end(Regex *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->match.end;
}

char* regex_get_match_group_name_1(Regex *const self, char const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    /* A never-matched, compiled-but-unmatched, or failed-compile object refuses.
     * The null check alone was NOT enough: after a successful compile, match_data
     * is non-null but VIRGIN - pcre2_match_data_create does not zero it, and the
     * substring accessors read fields only pcre2_match assigns (a probe segfaulted
     * deterministically). The matched flag is the real gate. */
    if (!self->matched || memory_empty(self->match_data)) {
        trace_log_pop();

        return nullptr;
    }

    PCRE2_UCHAR *buffer      = nullptr;
    PCRE2_SIZE   buffer_size = 0;

    I32 const code = pcre2_substring_get_byname(self->match_data, (PCRE2_SPTR) name, &buffer, &buffer_size);

    if (code < 0) {
        trace_log_pop();

        return nullptr;
    }

    /* A participating group that matched empty returns success with size 0; the
     * copy constructors abort on size 0, so hand back a freeable empty string. */
    char *group = nullptr;

    if (buffer_size == 0) {
        group = char_new_1(1);
        group[0] = '\0';
    }
    else {
        group = char_new_3((char*) buffer, buffer_size);
    }

    pcre2_substring_free(buffer);

    trace_log_pop();

    return group;
}

Str regex_get_match_group_name_2(Regex *const self, char const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    Str str = str_init_1();

    if (!self->matched || memory_empty(self->match_data)) {
        trace_log_pop();

        return str;
    }

    PCRE2_UCHAR *buffer      = nullptr;
    PCRE2_SIZE   buffer_size = 0;

    I32 const code = pcre2_substring_get_byname(self->match_data, (PCRE2_SPTR) name, &buffer, &buffer_size);

    if (code < 0) {
        trace_log_pop();

        return str;
    }

    /* size 0 (empty match) keeps the empty str_init_1 above; str_init_static aborts on 0 */
    if (buffer_size != 0) {
        str = str_init_static((char*) buffer, buffer_size);
    }

    pcre2_substring_free(buffer);

    trace_log_pop();

    return str;
}

String regex_get_match_group_name_3(Regex *const self, char const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    String string = string_init_1();

    if (!self->matched || memory_empty(self->match_data)) {
        trace_log_pop();

        return string;
    }

    PCRE2_UCHAR *buffer      = nullptr;
    PCRE2_SIZE   buffer_size = 0;

    I32 const code = pcre2_substring_get_byname(self->match_data, (PCRE2_SPTR) name, &buffer, &buffer_size);

    if (code < 0) {
        trace_log_pop();

        return string;
    }

    /* size 0 (empty match) keeps the empty string_init_1 above; string_init_static aborts on 0 */
    if (buffer_size != 0) {
        string = string_init_static((char*) buffer, buffer_size);
    }

    pcre2_substring_free(buffer);

    trace_log_pop();

    return string;
}

char* regex_get_match_group_number_1(Regex *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (!self->matched || memory_empty(self->match_data)) {
        trace_log_pop();

        return nullptr;
    }

    PCRE2_UCHAR *buffer      = nullptr;
    PCRE2_SIZE   buffer_size = 0;

    I32 const code = pcre2_substring_get_bynumber(self->match_data, (U32) index, &buffer, &buffer_size);

    if (code < 0) {
        trace_log_pop();

        return nullptr;
    }

    /* A participating group that matched empty returns success with size 0; the
     * copy constructors abort on size 0, so hand back a freeable empty string. */
    char *group = nullptr;

    if (buffer_size == 0) {
        group = char_new_1(1);
        group[0] = '\0';
    }
    else {
        group = char_new_3((char*) buffer, buffer_size);
    }

    pcre2_substring_free(buffer);

    trace_log_pop();

    return group;
}

Str regex_get_match_group_number_2(Regex *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    Str str = str_init_1();

    if (!self->matched || memory_empty(self->match_data)) {
        trace_log_pop();

        return str;
    }

    PCRE2_UCHAR *buffer      = nullptr;
    PCRE2_SIZE   buffer_size = 0;

    I32 const code = pcre2_substring_get_bynumber(self->match_data, (U32) index, &buffer, &buffer_size);

    if (code < 0) {
        trace_log_pop();

        return str;
    }

    /* size 0 (empty match) keeps the empty str_init_1 above; str_init_static aborts on 0 */
    if (buffer_size != 0) {
        str = str_init_static((char*) buffer, buffer_size);
    }

    pcre2_substring_free(buffer);

    trace_log_pop();

    return str;
}

String regex_get_match_group_number_3(Regex *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    String string = string_init_1();

    if (!self->matched || memory_empty(self->match_data)) {
        trace_log_pop();

        return string;
    }

    PCRE2_UCHAR *buffer      = nullptr;
    PCRE2_SIZE   buffer_size = 0;

    I32 const code = pcre2_substring_get_bynumber(self->match_data, (U32) index, &buffer, &buffer_size);

    if (code < 0) {
        trace_log_pop();

        return string;
    }

    /* size 0 (empty match) keeps the empty string_init_1 above; string_init_static aborts on 0 */
    if (buffer_size != 0) {
        string = string_init_static((char*) buffer, buffer_size);
    }

    pcre2_substring_free(buffer);

    trace_log_pop();

    return string;
}

USize regex_get_match_size(Regex *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->match.size;
}

/*==============================================================================
 * MARK: - Initialization & Cleanup
 *============================================================================*/
Regex regex_init(void) {
    return (Regex) DEFAULT_INITIALIZATION;
}

void regex_uninit(Regex *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    _regex_release(self);

    trace_log_pop();
}

/*==============================================================================
 * MARK: - Single Match
 *============================================================================*/
bool regex_match_1(Regex *const self, char const *const subject, USize const subject_offset) {
    trace_log_push(LOG_METADATA);

    /* subject is read by char_length below, so the check must run HERE - the _2
     * body's checks sit after the deref. */
    error_check_null(LOG_METADATA, "subject", (void*) subject);

    bool const match = regex_match_2(self, subject, char_length(subject), subject_offset);

    trace_log_pop();

    return match;
}

bool regex_match_2(Regex *const self, char const *const subject, USize const subject_size, USize const subject_offset) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "subject", (void*) subject);

    /* An EMPTY subject is a value ("a*" matches it), and the offset is routinely
     * a previous match's end - both are data, so out-of-range refuses rather
     * than aborts. */
    if (subject_offset > subject_size) {
        trace_log_pop();

        return false;
    }

    bool const match = _regex_match(self, subject, subject_size, subject_offset);

    trace_log_pop();

    return match;
}

bool regex_match_3(Regex *const self, Str *const subject, USize const subject_offset) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "subject", (void*) subject);

    if (subject_offset > str_get_size(subject)) {
        trace_log_pop();

        return false;
    }

    /* An empty Str carries data == nullptr; substitute the valid empty source. */
    bool const match = _regex_match(self, str_get_size(subject) == 0 ? "" : str_get_data(subject), str_get_size(subject), subject_offset);

    trace_log_pop();

    return match;
}

bool regex_match_4(Regex *const self, String *const subject, USize const subject_offset) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "subject", (void*) subject);

    if (subject_offset > string_get_size(subject)) {
        trace_log_pop();

        return false;
    }

    /* An empty String carries data == nullptr; substitute the valid empty source. */
    bool const match = _regex_match(self, string_get_size(subject) == 0 ? "" : string_get_data(subject), string_get_size(subject), subject_offset);

    trace_log_pop();

    return match;
}

/*==============================================================================
 * MARK: - Match All (Callback Iteration)
 *============================================================================*/
USize regex_match_all_1(Regex *const self, char const *const subject, USize const subject_offset, FpRegexMatch const callback, void *const context) {
    trace_log_push(LOG_METADATA);

    /* subject is read by char_length below, so the check must run HERE. */
    error_check_null(LOG_METADATA, "subject", (void*) subject);

    USize const match_count = regex_match_all_2(self, subject, char_length(subject), subject_offset, callback, context);

    trace_log_pop();

    return match_count;
}

USize regex_match_all_2(Regex *const self, char const *const subject, USize const subject_size, USize const subject_offset, FpRegexMatch const callback, void *const context) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "subject", (void*) subject);
    error_check_null(LOG_METADATA, "callback", (void*) callback);

    /* Out-of-range offsets and the empty subject are data; refuse with 0 matches. */
    if (subject_offset > subject_size) {
        trace_log_pop();

        return 0;
    }

    USize const match_count = _regex_match_all(self, subject, subject_size, subject_offset, callback, context);

    trace_log_pop();

    return match_count;
}

USize regex_match_all_3(Regex *const self, Str *const subject, USize const subject_offset, FpRegexMatch const callback, void *const context) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "subject", (void*) subject);
    error_check_null(LOG_METADATA, "callback", (void*) callback);

    if (subject_offset > str_get_size(subject)) {
        trace_log_pop();

        return 0;
    }

    /* An empty Str carries data == nullptr; substitute the valid empty source. */
    USize const match_count = _regex_match_all(self, str_get_size(subject) == 0 ? "" : str_get_data(subject), str_get_size(subject), subject_offset, callback, context);

    trace_log_pop();

    return match_count;
}

USize regex_match_all_4(Regex *const self, String *const subject, USize const subject_offset, FpRegexMatch const callback, void *const context) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "subject", (void*) subject);
    error_check_null(LOG_METADATA, "callback", (void*) callback);

    if (subject_offset > string_get_size(subject)) {
        trace_log_pop();

        return 0;
    }

    /* An empty String carries data == nullptr; substitute the valid empty source. */
    USize const match_count = _regex_match_all(self, string_get_size(subject) == 0 ? "" : string_get_data(subject), string_get_size(subject), subject_offset, callback, context);

    trace_log_pop();

    return match_count;
}