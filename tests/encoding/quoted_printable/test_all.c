/*
 * test_all.c - the encoding/quoted_printable suite.
 *
 * Quoted-Printable is a transfer encoding whose value is entirely in its edge
 * cases: the bytes that must be escaped, the whitespace that must be escaped
 * only at a line's end, the line breaks that must survive, and the 76-column
 * cap that no line may pass. Each of those is a case below, and every one of
 * them is a runtime branch rather than an error_check - a data-dependent
 * decision is never an abort primitive - so the same refusals hold in a build
 * with ERROR_CHECK_ENABLED compiled out.
 *
 * The three entry points are pinned AGAINST EACH OTHER as well as against
 * literals: encode_size must answer exactly what encode_1 writes and what
 * encode_2 appends, because they are one pass with three writers and a
 * divergence would mean a caller sizing a buffer from a different encoder than
 * the one that fills it.
 */
#include <stdio.h>

#include <encoding/quoted_printable/quoted_printable.h>
#include <log/log.h>
#include <test/test.h>

/*==============================================================================
 * MARK: - Macros
 *============================================================================*/

/* Longest input _expect_encoding accepts; its buffer is sized by the module's own bound. */
#define _EXPECT_INPUT_MAX 128

/* Longest input the invariant checker and its random loop produce. */
#define _SWEEP_INPUT_MAX 512

/*==============================================================================
 * MARK: - Helpers
 *============================================================================*/

/* One input, checked through all three entry points at once. */
static void _expect_encoding(Test *const test, char const *const name, char const *const input, USize const input_size, char const *const expected) {
    USize   const   expected_size   = char_length(expected);
    String          sink            = string_init_1();

    test_expect_true(test, name, encoding_quoted_printable_encode_2(input, input_size, &sink));
    test_expect_string(test, "encode_2 appends the expected characters", expected, string_get_data(&sink));
    test_expect_u(test, "encode_size agrees with encode_2", string_get_size(&sink), encoding_quoted_printable_encode_size(input, input_size));

    char    buffer[ENCODING_QUOTED_PRINTABLE_ENCODE_SIZE_MAX(_EXPECT_INPUT_MAX)] = DEFAULT_INITIALIZATION;
    USize   written                                                              = 0;

    test_expect_true(test, "the helper's input fits its bound-sized buffer", input_size <= _EXPECT_INPUT_MAX);
    test_expect_true(test, "encode_1 succeeds into a sized buffer",
        result_is_success(encoding_quoted_printable_encode_1(input, input_size, buffer, sizeof(buffer), &written)));
    test_expect_u(test, "encode_1 writes the same count", expected_size, written);
    test_expect_string(test, "and the same characters", expected, buffer);

    string_uninit(&sink);
}

/* Value of one UPPERCASE hex digit, or -1: lowercase is deliberately not accepted. */
static I32 _hex_value(char const c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }

    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }

    return -1;
}

static bool _same_bytes(char const *const left, char const *const right, USize const size) {
    for (USize i = 0; i < size; i += 1) {
        if (left[i] != right[i]) {
            return false;
        }
    }

    return true;
}

/* Reference decoder, TEST-LOCAL (the module has no decoder by design): "=" plus
 * CRLF is a soft break and vanishes, "=" plus two uppercase hex digits is one
 * byte, anything else is itself. Answers false on a malformed escape. */
static bool _reference_decode(char const *const encoded, USize const encoded_size, char *const decoded, USize const decoded_capacity, USize *const decoded_size) {
    USize index  = 0;
    USize length = 0;

    while (index < encoded_size) {
        if (length >= decoded_capacity) {
            return false;
        }

        if (encoded[index] != '=') {
            decoded[length] = encoded[index];
            length += 1;
            index += 1;

            continue;
        }

        if (index + 2 < encoded_size && encoded[index + 1] == '\r' && encoded[index + 2] == '\n') {
            index += 3;

            continue;
        }

        if (index + 2 >= encoded_size) {
            return false;
        }

        I32 const high = _hex_value(encoded[index + 1]);
        I32 const low  = _hex_value(encoded[index + 2]);

        if (high < 0 || low < 0) {
            return false;
        }

        decoded[length] = (char) ((high << 4) | low);
        length += 1;
        index += 3;
    }

    *decoded_size = length;

    return true;
}

/* Encode one input and assert the four codec invariants plus size agreement,
 * each under a message naming the input (label) that broke it. */
static void _check_invariants(Test *const test, char const *const label, char const *const input, USize const input_size) {
    char    output[ENCODING_QUOTED_PRINTABLE_ENCODE_SIZE_MAX(_SWEEP_INPUT_MAX)] = DEFAULT_INITIALIZATION;
    char    decoded[_SWEEP_INPUT_MAX]                                            = DEFAULT_INITIALIZATION;
    char    name[192]                                                           = DEFAULT_INITIALIZATION;
    USize   output_size                                                         = 0;
    USize   decoded_size                                                        = 0;

    snprintf(name, sizeof(name), "%s: encodes", label);

    if (!test_expect_true(test, name, input_size <= _SWEEP_INPUT_MAX && result_is_success(encoding_quoted_printable_encode_1(input, input_size, output, sizeof(output), &output_size)))) {
        return;
    }

    bool    lines_fit           = true;
    bool    whitespace_safe     = true;
    bool    escapes_wellformed  = true;
    USize   line_length         = 0;

    for (USize i = 0; i < output_size; i += 1) {
        bool const break_here = output[i] == '\r' && i + 1 < output_size && output[i + 1] == '\n';

        if (break_here) {
            lines_fit   = lines_fit && line_length <= ENCODING_QUOTED_PRINTABLE_LINE_MAX_LENGTH;
            line_length = 0;
            i += 1;

            continue;
        }

        line_length += 1;

        if (output[i] == ' ' || output[i] == '\t') {
            whitespace_safe = whitespace_safe && i + 1 < output_size && output[i + 1] != '\r';
        }

        if (output[i] == '=') {
            bool const soft_break = i + 2 < output_size && output[i + 1] == '\r' && output[i + 2] == '\n';
            bool const hex_pair   = i + 2 < output_size && _hex_value(output[i + 1]) >= 0 && _hex_value(output[i + 2]) >= 0;

            escapes_wellformed = escapes_wellformed && (soft_break || hex_pair);
        }
    }

    lines_fit = lines_fit && line_length <= ENCODING_QUOTED_PRINTABLE_LINE_MAX_LENGTH;

    snprintf(name, sizeof(name), "%s: (a) every line <= 76 columns counting the soft-break '='", label);
    test_expect_true(test, name, lines_fit);

    snprintf(name, sizeof(name), "%s: (b) no literal SP or HT before CRLF or at the end", label);
    test_expect_true(test, name, whitespace_safe);

    snprintf(name, sizeof(name), "%s: (c) every '=' is a soft break or two uppercase hex digits", label);
    test_expect_true(test, name, escapes_wellformed);

    snprintf(name, sizeof(name), "%s: (d) decode(encode(x)) == x", label);
    test_expect_true(test, name,
        _reference_decode(output, output_size, decoded, sizeof(decoded), &decoded_size) &&
        decoded_size == input_size &&
        _same_bytes(decoded, input, input_size));

    snprintf(name, sizeof(name), "%s: encode_size agrees with encode_1", label);
    test_expect_u(test, name, output_size, encoding_quoted_printable_encode_size(input, input_size));
}

/*==============================================================================
 * MARK: - Cases
 *============================================================================*/

static void _test_literal_bytes(Test *const test) {
    test_case_begin(test, "printable US-ASCII passes through, '=' does not");

    _expect_encoding(test, "plain text is untouched", "Hello, world!", CHAR_STATIC_SIZE("Hello, world!"), "Hello, world!");
    _expect_encoding(test, "the escape character is itself escaped", "a=b", CHAR_STATIC_SIZE("a=b"), "a=3Db");
    _expect_encoding(test, "0x21 is the low edge of the literal range", "!", CHAR_STATIC_SIZE("!"), "!");
    _expect_encoding(test, "0x7E is the high edge of the literal range", "~", CHAR_STATIC_SIZE("~"), "~");

    test_case_end(test);
}

static void _test_escaped_bytes(Test *const test) {
    test_case_begin(test, "controls, DEL and every byte >= 0x80 are escaped in uppercase hex");

    _expect_encoding(test, "UTF-8 is escaped byte by byte", "S\xc3\xa1""bado", 7, "S=C3=A1bado");
    _expect_encoding(test, "an embedded NUL is a byte like any other", "a\0b", 3, "a=00b");
    _expect_encoding(test, "DEL is escaped", "a\x7f""b", 3, "a=7Fb");
    _expect_encoding(test, "a tab that is not at a line end passes through", "a\tb", 3, "a\tb");
    _expect_encoding(test, "0xFF is escaped", "\xff", 1, "=FF");

    test_case_end(test);
}

static void _test_line_breaks(Test *const test) {
    test_case_begin(test, "a CRLF pair is a hard break; a lone CR or LF is not");

    _expect_encoding(test, "a CRLF pair survives verbatim", "a\r\nb", 4, "a\r\nb");
    _expect_encoding(test, "two CRLF pairs make an empty line", "a\r\n\r\nb", 6, "a\r\n\r\nb");

    /* Feeding CRLF-normalized text is the caller's job; a bare newline is a
     * control byte here, not a break, and escaping it is what stops a decoder
     * seeing a line structure the sender never wrote. */
    _expect_encoding(test, "a lone LF is escaped", "a\nb", 3, "a=0Ab");
    _expect_encoding(test, "a lone CR is escaped", "a\rb", 3, "a=0Db");
    _expect_encoding(test, "an LF CR pair is two escapes, not a break", "a\n\rb", 4, "a=0A=0Db");

    test_case_end(test);
}

static void _test_trailing_whitespace(Test *const test) {
    test_case_begin(test, "whitespace is escaped exactly where a transport could strip it");

    _expect_encoding(test, "an interior space passes through", "a b", CHAR_STATIC_SIZE("a b"), "a b");
    _expect_encoding(test, "a trailing space is escaped", "abc ", CHAR_STATIC_SIZE("abc "), "abc=20");
    _expect_encoding(test, "a trailing tab is escaped", "abc\t", 4, "abc=09");
    _expect_encoding(test, "a space before a hard break is escaped", "a \r\nb", 5, "a=20\r\nb");
    _expect_encoding(test, "a tab before a hard break is escaped", "a\t\r\nb", 5, "a=09\r\nb");
    _expect_encoding(test, "a space before a lone LF is escaped too", "a \nb", 4, "a=20=0Ab");
    _expect_encoding(test, "a lone space is escaped", " ", 1, "=20");

    test_case_end(test);
}

static void _test_soft_line_breaks(Test *const test) {
    test_case_begin(test, "no encoded line passes 76 characters");

    char    input[512]  = DEFAULT_INITIALIZATION;
    String  sink        = string_init_1();

    memory_set(input, 200, 'a');

    test_expect_true(test, "a 200-character run encodes", encoding_quoted_printable_encode_2(input, 200, &sink));

    /* 75 literal characters plus the soft break's '=' is exactly 76 columns, so
     * 200 characters is 75 + 75 + 50 with two soft breaks. */
    test_expect_u(test, "into 75 + 75 + 50 across two soft breaks", 75 + 3 + 75 + 3 + 50, string_get_size(&sink));

    char    const   *const  data        = string_get_data(&sink);
    USize   const           size        = string_get_size(&sink);
    USize                   line_length = 0;
    USize                   longest     = 0;

    for (USize i = 0; i < size; i += 1) {
        if (data[i] == '\n') {
            line_length = 0;
        }
        else if (data[i] != '\r') {
            line_length += 1;

            longest = line_length > longest ? line_length : longest;
        }
    }

    test_expect_u(test, "the longest line is exactly the cap", ENCODING_QUOTED_PRINTABLE_LINE_MAX_LENGTH, longest);
    test_expect_true(test, "each wrapped line ends with the soft break", data[75] == '=' && data[76] == '\r' && data[77] == '\n');

    string_clear(&sink);

    /* An "=XX" triplet must never be split across a soft break. */
    memory_set(input, 74, 'a');

    input[74] = (char) 0xc3;
    input[75] = (char) 0xa1;

    test_expect_true(test, "an escape triplet near the cap encodes", encoding_quoted_printable_encode_2(input, 76, &sink));
    test_expect_string(test, "moved whole onto the next line",
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa=\r\n=C3=A1",
        string_get_data(&sink));

    string_clear(&sink);

    /* A literal space is never left as a line's last character either: it stops
     * one column earlier than everything else. */
    memory_set(input, 74, 'b');

    input[74] = ' ';
    input[75] = 'c';

    test_expect_true(test, "a space near the cap encodes", encoding_quoted_printable_encode_2(input, 76, &sink));
    test_expect_string(test, "with the space moved past the soft break, never left at a line end",
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb=\r\n c",
        string_get_data(&sink));

    string_clear(&sink);

    test_expect_true(test, "a hard break restarts the column count", encoding_quoted_printable_encode_2(input, 74, &sink));
    test_expect_u(test, "so 74 characters need no soft break at all", 74, string_get_size(&sink));

    string_uninit(&sink);

    test_case_end(test);
}

static void _test_empty_and_capacity(Test *const test) {
    test_case_begin(test, "empty is a legal value and a short buffer refuses instead of overrunning");

    String  sink            = string_init_1();
    char    buffer[8]       = DEFAULT_INITIALIZATION;
    USize   encoded_size    = 0;

    test_expect_u(test, "an empty input encodes to nothing", 0, encoding_quoted_printable_encode_size("", 0));
    test_expect_true(test, "and appends nothing", encoding_quoted_printable_encode_2("", 0, &sink));
    test_expect_u(test, "leaving the sink empty", 0, string_get_size(&sink));
    test_expect_true(test, "encode_1 accepts it too",
        result_is_success(encoding_quoted_printable_encode_1("", 0, buffer, sizeof(buffer), &encoded_size)));
    test_expect_u(test, "writing nothing", 0, encoded_size);

    /* The capacity is checked BEFORE every write, so this refusal holds even in
     * a build with the null checks compiled out. */
    test_expect_true(test, "one character short refuses",
        result_is_error(encoding_quoted_printable_encode_1("abcdefghi", CHAR_STATIC_SIZE("abcdefghi"), buffer, sizeof(buffer), &encoded_size)));
    test_expect_u(test, "and leaves encoded_size untouched", 0, encoded_size);

    test_expect_true(test, "an exactly-sized buffer succeeds",
        result_is_success(encoding_quoted_printable_encode_1("abcdefgh", CHAR_STATIC_SIZE("abcdefgh"), buffer, sizeof(buffer), &encoded_size)));
    test_expect_u(test, "writing every character", 8, encoded_size);

    /* A triplet that would only PARTLY fit must refuse, not write two of its
     * three characters. */
    test_expect_true(test, "a buffer that fits only part of a triplet refuses",
        result_is_error(encoding_quoted_printable_encode_1("aaaaaa\xff", 7, buffer, sizeof(buffer), &encoded_size)));

    test_expect_true(test, "encode_2 appends to what the sink already holds", encoding_quoted_printable_encode_2("ab", 2, &sink));
    test_expect_true(test, "twice", encoding_quoted_printable_encode_2("cd", 2, &sink));
    test_expect_string(test, "in order", "abcd", string_get_data(&sink));

    string_uninit(&sink);

    test_case_end(test);
}

static void _test_size_bound(Test *const test) {
    test_case_begin(test, "ENCODING_QUOTED_PRINTABLE_ENCODE_SIZE_MAX bounds encode_size");

    char    worst[300]  = DEFAULT_INITIALIZATION;
    char    mixed[300]  = DEFAULT_INITIALIZATION;
    bool    worst_fits  = true;
    bool    mixed_fits  = true;

    memory_set(worst, sizeof(worst), (char) 0xff);

    for (USize i = 0; i < sizeof(mixed); i += 1) {
        mixed[i] = (char) ((i * 37 + 11) & 0xff);
    }

    for (USize n = 0; n <= 300; n += 1) {
        worst_fits = worst_fits && encoding_quoted_printable_encode_size(worst, n) <= ENCODING_QUOTED_PRINTABLE_ENCODE_SIZE_MAX(n);
        mixed_fits = mixed_fits && encoding_quoted_printable_encode_size(mixed, n) <= ENCODING_QUOTED_PRINTABLE_ENCODE_SIZE_MAX(n);
    }

    test_expect_true(test, "all-0xFF input never exceeds the bound for n in 0..300", worst_fits);
    test_expect_true(test, "mixed input never exceeds the bound for n in 0..300", mixed_fits);

    /* All-escaped input is the true worst case: 3n plus one soft break per full
     * line, 3n + 3 * (ceil(n / 25) - 1). 25 triplets fill a line exactly (75). */
    test_expect_u(test, "n = 25 is one full line with no break", 3 * 25 + 3 * ((25 + 24) / 25 - 1), encoding_quoted_printable_encode_size(worst, 25));
    test_expect_u(test, "n = 26 needs exactly one soft break", 3 * 26 + 3 * ((26 + 24) / 25 - 1), encoding_quoted_printable_encode_size(worst, 26));

    test_case_end(test);
}

typedef struct {
    char const  *name;
    char const  *tail;
    USize       tail_size;
} TailCase;

static void _test_boundary_sweep(Test *const test) {
    test_case_begin(test, "invariants hold across every prefix length and tail shape at the 76-column edge");

    TailCase const tails[] = {
        { "literal b",  "b",        1 },
        { "SP then b",  " b",       2 },
        { "HT then b",  "\tb",      2 },
        { "0xFF",       "\xff",     1 },
        { "'='",        "=",        1 },
        { "SP+0xFF",    " \xff",    2 },
        { "SP at end",  " ",        1 },
        { "HT at end",  "\t",       1 },
        { "HT+CRLF",    "\t\r\n",   3 },
        { "CRLF",       "\r\n",     2 },
        { "lone LF",    "\n",       1 }
    };

    for (USize prefix = 70; prefix <= 78; prefix += 1) {
        for (USize t = 0; t < sizeof(tails) / sizeof(tails[0]); t += 1) {
            char    input[_SWEEP_INPUT_MAX]  = DEFAULT_INITIALIZATION;
            char    label[96]               = DEFAULT_INITIALIZATION;

            memory_set(input, prefix, 'a');
            memory_copy_1(input + prefix, tails[t].tail, tails[t].tail_size);
            snprintf(label, sizeof(label), "prefix %zu + %s", (size_t) prefix, tails[t].name);

            _check_invariants(test, label, input, prefix + tails[t].tail_size);
        }
    }

    test_case_end(test);
}

static void _test_random_inputs(Test *const test) {
    test_case_begin(test, "invariants hold for fixed-seed pseudo-random inputs");

    static char const alphabet[8] = { 'a', 'a', 'b', ' ', '=', '\t', '\r', '\n' };

    U64 state = 0x243F6A8885A308D3ull;

    for (USize round = 0; round < 300; round += 1) {
        char    input[_SWEEP_INPUT_MAX]  = DEFAULT_INITIALIZATION;
        char    label[64]               = DEFAULT_INITIALIZATION;

        state = state * 6364136223846793005ull + 1442695040888963407ull;

        USize const size = (USize) ((state >> 33) % 401);
        USize const mode = round % 3;

        for (USize i = 0; i < size; i += 1) {
            state = state * 6364136223846793005ull + 1442695040888963407ull;

            U8 const r = (U8) (state >> 56);

            if (mode == 0) {
                input[i] = (char) r;
            }
            else if (mode == 1) {
                input[i] = (r & 3) == 0 ? (char) (state >> 40) : alphabet[(r >> 2) & 7];
            }
            else {
                input[i] = (r & 15) == 0 ? alphabet[(r >> 4) & 7] : 'a';
            }
        }

        snprintf(label, sizeof(label), "random round %zu (size %zu, mode %zu)", (size_t) round, (size_t) size, (size_t) mode);

        _check_invariants(test, label, input, size);
    }

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Entry
 *============================================================================*/

I32 main(void) {
    LogConfig const log_config = { .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true };

    log_init(log_config);

    Test test = test_init("encoding_quoted_printable");

    test_suite_begin(&test, "encoding_quoted_printable");

    _test_literal_bytes(&test);
    _test_escaped_bytes(&test);
    _test_line_breaks(&test);
    _test_trailing_whitespace(&test);
    _test_soft_line_breaks(&test);
    _test_empty_and_capacity(&test);
    _test_size_bound(&test);
    _test_boundary_sweep(&test);
    _test_random_inputs(&test);

    test_suite_end(&test);

    return test_uninit(&test);
}