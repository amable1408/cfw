#include <encoding/quoted_printable/quoted_printable.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

/* The '=' of a soft break occupies a column of its own, so a normal token may
 * reach column LINE_MAX - 1 and no further. */
#define _ENCODING_QUOTED_PRINTABLE_LINE_LIMIT (ENCODING_QUOTED_PRINTABLE_LINE_MAX_LENGTH - 1)

/* A literal space or tab must never be a line's last character, so it stops one
 * column earlier than everything else: there has to be room for the byte that
 * follows it before the soft break can be taken. */
#define _ENCODING_QUOTED_PRINTABLE_LINE_LIMIT_WSP (ENCODING_QUOTED_PRINTABLE_LINE_MAX_LENGTH - 2)

#define _ENCODING_QUOTED_PRINTABLE_TRIPLET_SIZE 3

static char const _ENCODING_QUOTED_PRINTABLE_DIGITS[] = "0123456789ABCDEF";

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/* One writer drives all three public entry points. `output` and `sink` are
 * mutually exclusive and both may be null: with neither set the pass counts
 * characters without writing any, which is exactly what encode_size needs and
 * is why sizing can never disagree with encoding. */
typedef struct {
    USize   capacity;
    USize   line_length;
    char    *output;
    bool    refused;
    String  *sink;
    USize   size;
} _Encoding_Quoted_Printable_Writer;

/*==============================================================================
 * MARK: - Helpers
 *============================================================================*/

static void _encoding_quoted_printable_write(_Encoding_Quoted_Printable_Writer *const self, char const *const data, USize const size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    if (self->refused || size == 0) {
        trace_log_pop();

        return;
    }

    if (self->sink != nullptr) {
        USize const stored_before = string_get_size(self->sink);

        string_add_last_2(self->sink, (char*) data, size);

        /* string_add_last_2 DECLINES rather than growing when the allocator
         * refuses, and reports it by leaving the size alone. */
        if (string_get_size(self->sink) - stored_before != size) {
            self->refused = true;

            trace_log_pop();

            return;
        }
    } else if (self->output != nullptr) {
        /* Checked BEFORE the copy, so a short buffer refuses instead of being
         * overrun - and holds with ERROR_CHECK_ENABLED compiled out. */
        if (size > self->capacity - self->size) {
            self->refused = true;

            trace_log_pop();

            return;
        }

        memory_copy_2(self->output + self->size, self->capacity - self->size, (void*) data, size);
    }

    self->size += size;

    trace_log_pop();
}

/* The whole encoder. Every public entry point is this pass with a different
 * writer, so the size a caller is told and the bytes it later gets are produced
 * by one piece of code rather than two that must be kept in agreement. */
static void _encoding_quoted_printable_run(_Encoding_Quoted_Printable_Writer *const self, char const *const input, USize const input_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "input", (void*) input);

    USize index = 0;

    while (index < input_size && !self->refused) {
        U8 const c = (U8) input[index];

        /* A CRLF PAIR is the only line break this encoding recognizes; it goes
         * out verbatim and restarts the column count. */
        if (c == '\r' && index + 1 < input_size && input[index + 1] == '\n') {
            _encoding_quoted_printable_write(self, "\r\n", CHAR_STATIC_SIZE("\r\n"));

            self->line_length = 0;
            index += 2;

            continue;
        }

        bool const whitespace = c == ' ' || c == '\t';
        bool const printable  = c >= 0x21 && c <= 0x7e && c != '=';
        bool       trailing   = false;

        if (whitespace) {
            USize const next = index + 1;

            trailing = next >= input_size || input[next] == '\r' || input[next] == '\n';
        }

        bool    const   escaped = !printable && !(whitespace && !trailing);
        USize   const   needed  = escaped ? _ENCODING_QUOTED_PRINTABLE_TRIPLET_SIZE : 1;
        USize   const   limit   = !escaped && whitespace
                                ? _ENCODING_QUOTED_PRINTABLE_LINE_LIMIT_WSP
                                : _ENCODING_QUOTED_PRINTABLE_LINE_LIMIT;

        if (self->line_length + needed > limit) {
            _encoding_quoted_printable_write(self, "=\r\n", CHAR_STATIC_SIZE("=\r\n"));

            self->line_length = 0;
        }

        if (escaped) {
            char const triplet[_ENCODING_QUOTED_PRINTABLE_TRIPLET_SIZE] = {
                '=',
                _ENCODING_QUOTED_PRINTABLE_DIGITS[(c >> 4) & 0x0f],
                _ENCODING_QUOTED_PRINTABLE_DIGITS[c & 0x0f]
            };

            _encoding_quoted_printable_write(self, triplet, _ENCODING_QUOTED_PRINTABLE_TRIPLET_SIZE);
        } else {
            _encoding_quoted_printable_write(self, input + index, 1);
        }

        self->line_length += needed;
        index += 1;
    }

    trace_log_pop();
}

/*==============================================================================
 * MARK: - Public Functions
 *============================================================================*/

Result encoding_quoted_printable_encode_1(char const *const input, USize const input_size, char *const output, USize const output_capacity, USize *const encoded_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "input", (void*) input);
    error_check_null(LOG_METADATA, "output", (void*) output);
    error_check_null(LOG_METADATA, "encoded_size", (void*) encoded_size);

    _Encoding_Quoted_Printable_Writer writer = {
        .capacity       = output_capacity,
        .line_length    = 0,
        .output         = output,
        .refused        = false,
        .sink           = nullptr,
        .size           = 0
    };

    _encoding_quoted_printable_run(&writer, input, input_size);

    if (writer.refused) {
        trace_log_pop();

        return result_make(RESULT_CATEGORY_ARGUMENT, 0, 0);
    }

    *encoded_size = writer.size;

    trace_log_pop();

    return RESULT_SUCCESS;
}

bool encoding_quoted_printable_encode_2(char const *const input, USize const input_size, String *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "input", (void*) input);
    error_check_null(LOG_METADATA, "out", (void*) out);

    _Encoding_Quoted_Printable_Writer writer = {
        .capacity       = 0,
        .line_length    = 0,
        .output         = nullptr,
        .refused        = false,
        .sink           = out,
        .size           = 0
    };

    _encoding_quoted_printable_run(&writer, input, input_size);

    trace_log_pop();

    return !writer.refused;
}

USize encoding_quoted_printable_encode_size(char const *const input, USize const input_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "input", (void*) input);

    _Encoding_Quoted_Printable_Writer writer = {
        .capacity       = 0,
        .line_length    = 0,
        .output         = nullptr,
        .refused        = false,
        .sink           = nullptr,
        .size           = 0
    };

    _encoding_quoted_printable_run(&writer, input, input_size);

    trace_log_pop();

    return writer.size;
}