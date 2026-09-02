/*
 * test_all.c - Behavioral tests for include/bits/bits.c.
 *
 * Covers the single-USize accessors (bits_at/_flip/_write/_count/_first_set/
 * _last_set), the multi-word array accessors (bits_array_set/_clear/_test/
 * _any/_count/_clear_all), rendering (bits_format, bits_print_1/_2, stdout
 * captured via dup2 of the CRT's fileno(stdout), which works on both MinGW
 * and glibc), and the abort contract of every error_check_* guard in the
 * module. A failing error_check_* aborts the process under
 * ERROR_CHECK_ENABLED, so the abort half of that contract cannot be observed
 * in-process without killing the runner - this suite spawns ITSELF as a
 * child, re-entered through the --child-* arguments below (the same pattern
 * tests/error/test_all.c and tests/process/test_all.c use), and asserts on
 * the child's exit status and captured log line rather than catching a
 * signal in-process.
 *
 * The unchecked-build counterpart (bits.c compiled WITHOUT ERROR_CHECK_ENABLED)
 * lives in test_unchecked.c, a separate binary - see that file for the inert
 * degradation pins this suite cannot observe with the checks armed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bits/bits.h>
#include <char/char.h>
#include <log/log.h>
#include <process/process.h>
#include <test/test.h>

#ifdef OS_WINDOWS
#include <io.h>
#endif // OS_WINDOWS

/*==============================================================================
 * MARK: - File Scope
 *============================================================================*/

/** Path this binary was invoked with, reused as the program every child spawn runs. */
static char const *_program = nullptr;

/*==============================================================================
 * MARK: - Print Capture Helpers
 *============================================================================*/

/**
 * @brief Strip a trailing "\r\n" or "\n" from a captured buffer, in place.
 * @param self Buffer to trim (must not be nullptr).
 */
static void _trim_trailing_newline(char *const self) {
    USize size = char_length(self);

    while (size > 0 && (self[size - 1] == '\n' || self[size - 1] == '\r')) {
        size -= 1;
        self[size] = '\0';
    }
}

/**
 * @brief Build a fixture path under the OS temp dir for this suite's stdout captures.
 * @param name      Fixture file leaf name.
 * @param buffer     Receives the full path.
 * @param buffer_size Capacity of buffer.
 */
static void _fixture_path(char const *const name, char *const buffer, USize const buffer_size) {
#ifdef OS_WINDOWS
    char const *const dir = getenv("TEMP");
#else
    char const *const dir = getenv("TMPDIR");
#endif // OS_WINDOWS

    /* A path that does not fit is refused rather than truncated: a truncated
     * path opened "wb" would empty whatever file it happened to name. */
    I32 const length = snprintf(buffer, buffer_size, "%s/%s", dir != nullptr && dir[0] != '\0' ? dir : (
#ifdef OS_WINDOWS
        "."
#else
        "/tmp"
#endif // OS_WINDOWS
    ), name);

    if (length < 0 || (USize) length >= buffer_size) {
        buffer[0] = '\0';
    }
}

/** A print to perform under redirected stdout; `context` is forwarded as given. */
typedef void (*FpCapturePrint)(void *const context);

/**
 * @brief Redirect stdout to a fixture file, run a print callback, restore stdout, and read the
 *        fixture back with its trailing CRLF/LF trimmed. Shared by bits_print_1 and bits_print_2.
 *        When the fixture cannot be opened (a refused path, an unwritable temp dir) the buffer
 *        receives a sentinel no rendering can equal, so every pin built on it fails visibly
 *        rather than an empty capture passing "prints nothing" for the wrong reason.
 *
 * @param call        Callback that performs the print under redirected stdout.
 * @param context     Opaque context forwarded to call.
 * @param buffer      Receives the trimmed captured text (must not be nullptr).
 * @param buffer_size Capacity of buffer.
 */
static void _capture_print(FpCapturePrint const call, void *const context, char *const buffer, USize const buffer_size) {
    char path[512] = DEFAULT_INITIALIZATION;

    _fixture_path("fixture_bits_print.txt", path, sizeof(path));

    /* Open before touching stdout, so a failure leaves nothing to restore. */
    FILE *const capture = path[0] != '\0' ? fopen(path, "wb") : nullptr;

    if (capture == nullptr) {
        snprintf(buffer, buffer_size, "<capture failed: %s>", path[0] != '\0' ? path : "path refused");

        return;
    }

    fflush(stdout);

#ifdef OS_WINDOWS
    I32 const saved_fd = _dup(_fileno(stdout));

    _dup2(_fileno(capture), _fileno(stdout));
#else
    I32 const saved_fd = dup(fileno(stdout));

    dup2(fileno(capture), fileno(stdout));
#endif // OS_WINDOWS

    call(context);

    fflush(stdout);
    fclose(capture);

#ifdef OS_WINDOWS
    _dup2(saved_fd, _fileno(stdout));
    _close(saved_fd);
#else
    dup2(saved_fd, fileno(stdout));
    close(saved_fd);
#endif // OS_WINDOWS

    buffer[0] = '\0';

    FILE *const readback = fopen(path, "rb");

    if (readback != nullptr) {
        USize const read_size = fread(buffer, sizeof(char), buffer_size - 1, readback);

        buffer[read_size] = '\0';

        fclose(readback);
    }

    remove(path);

    _trim_trailing_newline(buffer);
}

typedef struct {
    USize value;
    U8    self_size;
    char  separator;
} _PrintArgs;

static void _call_print_1(void *const context) {
    _PrintArgs const *const args = (_PrintArgs const*) context;

    bits_print_1(args->value, args->self_size);
}

static void _call_print_2(void *const context) {
    _PrintArgs const *const args = (_PrintArgs const*) context;

    bits_print_2(args->value, args->self_size, args->separator);
}

static void _capture_print_1(USize const value, U8 const self_size, char *const buffer, USize const buffer_size) {
    _PrintArgs args = { value, self_size, ' ' };

    _capture_print(_call_print_1, &args, buffer, buffer_size);
}

static void _capture_print_2(USize const value, U8 const self_size, char const separator, char *const buffer, USize const buffer_size) {
    _PrintArgs args = { value, self_size, separator };

    _capture_print(_call_print_2, &args, buffer, buffer_size);
}

/*==============================================================================
 * MARK: - Child Modes (death tests)
 *============================================================================*/

/** Config shared by every child mode: route the abort's log line to stdout so the parent's
 *  process_run capture can see it. */
static LogConfig _child_log_config(void) {
    return (LogConfig) {
        .level = LOG_LEVEL_ERROR,
        .stream = LOG_STREAM_STDOUT,
        .timestamp_enabled = true,
        .autoflush = true
    };
}

static I32 _child_at_out_of_bound(void) {
    log_init(_child_log_config());
    bits_at((USize) 0, (U8) 64);

    return 0;
}

static I32 _child_flip_null(void) {
    log_init(_child_log_config());
    bits_flip(nullptr, (U8) 0);

    return 0;
}

static I32 _child_flip_out_of_bound(void) {
    log_init(_child_log_config());

    USize value = 0;

    bits_flip(&value, (U8) 64);

    return 0;
}

static I32 _child_write_null(void) {
    log_init(_child_log_config());
    bits_write(nullptr, (U8) 1, true);

    return 0;
}

static I32 _child_write_out_of_bound(void) {
    log_init(_child_log_config());

    USize value = 0;

    bits_write(&value, (U8) 64, true);

    return 0;
}

static I32 _child_array_set_null(void) {
    log_init(_child_log_config());
    bits_array_set(nullptr, (USize) 0, (USize) 0);

    return 0;
}

static I32 _child_array_clear_null(void) {
    log_init(_child_log_config());
    bits_array_clear(nullptr, (USize) 0, (USize) 0);

    return 0;
}

static I32 _child_array_test_null(void) {
    log_init(_child_log_config());
    bits_array_test(nullptr, (USize) 0, (USize) 0);

    return 0;
}

static I32 _child_array_set_past(void) {
    log_init(_child_log_config());

    U64 words[3] = DEFAULT_INITIALIZATION;

    bits_array_set(words, (USize) 2, (USize) 128);

    return 0;
}

static I32 _child_array_clear_past(void) {
    log_init(_child_log_config());

    U64 words[3] = DEFAULT_INITIALIZATION;

    bits_array_clear(words, (USize) 2, (USize) 128);

    return 0;
}

static I32 _child_array_test_past(void) {
    log_init(_child_log_config());

    U64 words[3] = DEFAULT_INITIALIZATION;

    bits_array_test(words, (USize) 2, (USize) 128);

    return 0;
}

static I32 _child_print_2_non_value(void) {
    log_init(_child_log_config());
    bits_print_2((USize) 0, (U8) 0, ' ');

    return 0;
}

static I32 _child_print_2_over_64(void) {
    log_init(_child_log_config());
    bits_print_2((USize) 0, (U8) 65, ' ');

    return 0;
}

static I32 _child_format_buffer_too_small(void) {
    log_init(_child_log_config());

    char buffer[4] = DEFAULT_INITIALIZATION;

    bits_format((USize) 0xFF, (U8) 8, ' ', 0, buffer, sizeof(buffer));

    return 0;
}

static I32 _child_array_any_null(void) {
    log_init(_child_log_config());
    bits_array_any(nullptr, (USize) 1);

    return 0;
}

static I32 _child_array_count_null(void) {
    log_init(_child_log_config());
    bits_array_count(nullptr, (USize) 1);

    return 0;
}

static I32 _child_array_clear_all_null(void) {
    log_init(_child_log_config());
    bits_array_clear_all(nullptr, (USize) 1);

    return 0;
}

static I32 _child_format_null(void) {
    log_init(_child_log_config());

    bits_format((USize) 5, (U8) 8, ' ', 8, nullptr, (USize) 128);

    return 0;
}

static I32 _child_format_capacity_one_short(void) {
    log_init(_child_log_config());

    char buffer[9] = DEFAULT_INITIALIZATION;

    bits_format((USize) 5, (U8) 8, ' ', 4, buffer, sizeof(buffer));

    return 0;
}

/**
 * @brief Spawn this binary with flag, and assert it aborted while logging expected_substring.
 * @param test       Harness to record assertions into.
 * @param case_name  Case label.
 * @param flag       --child-* flag identifying which probe to run.
 * @param expected_substring Text the failure's log line must contain.
 */
static void _test_abort_probe(Test *const test, char const *const case_name, char const *const flag, char const *const expected_substring) {
    char const *const argv_vector[] = { _program, flag, nullptr };
    ProcessSpec  const spec = { .argv = argv_vector, .timeout_milliseconds = 5000 };
    ProcessOutcome     outcome = DEFAULT_INITIALIZATION;

    Result const result = process_run(spec, &outcome);

    test_expect_true(test, case_name, result_is_success(result) && process_outcome_aborted(&outcome));
    test_expect_true(test, "abort logged the expected tag", outcome.output != nullptr && strstr(outcome.output, expected_substring) != nullptr);

    process_outcome_uninit(&outcome);
}

/*==============================================================================
 * MARK: - Cases: bits_at
 *============================================================================*/

static void _test_bits_at(Test *const test) {
    test_case_begin(test, "bits_at");

    test_expect_true(test, "bit 0 of 1 is set", bits_at((USize) 1, (U8) 0));
    test_expect_true(test, "bit 63 of (1ULL<<63) is set", bits_at((USize) 1ULL << 63, (U8) 63));
    test_expect_false(test, "bit 0 of all-zero is clear", bits_at((USize) 0, (U8) 0));
    test_expect_false(test, "bit 63 of all-zero is clear", bits_at((USize) 0, (U8) 63));
    test_expect_true(test, "bit 0 of all-ones is set", bits_at((USize) ~0ULL, (U8) 0));
    test_expect_true(test, "bit 63 of all-ones is set", bits_at((USize) ~0ULL, (U8) 63));
    test_expect_false(test, "return is exactly false", bits_at((USize) 0, (U8) 5));
    test_expect_true(test, "return is exactly true", bits_at((USize) 0xFF, (U8) 3));

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: bits_flip
 *============================================================================*/

static void _test_bits_flip(Test *const test) {
    test_case_begin(test, "bits_flip");

    USize value = 0;

    bits_flip(&value, (U8) 3);
    test_expect_true(test, "flip once sets the bit", bits_at(value, (U8) 3));

    bits_flip(&value, (U8) 3);
    test_expect_false(test, "flip twice restores the bit", bits_at(value, (U8) 3));
    test_expect_u(test, "flip twice restores the whole value", 0, value);

    USize zero = 0;

    bits_flip(&zero, (U8) 63);
    test_expect_u(test, "flipping bit 63 of 0 gives 1ULL<<63", (USize) (1ULL << 63), zero);

    USize independence = 0;

    bits_flip(&independence, (U8) 2);
    test_expect_false(test, "flipping bit 2 leaves bit 1 clear", bits_at(independence, (U8) 1));
    test_expect_false(test, "flipping bit 2 leaves bit 3 clear", bits_at(independence, (U8) 3));
    test_expect_u(test, "flipping bit 2 sets only bit 2", (USize) 0x4, independence);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: bits_write
 *============================================================================*/

static void _test_bits_write(Test *const test) {
    test_case_begin(test, "bits_write");

    USize value = 0;

    bits_write(&value, (U8) 4, true);
    test_expect_true(test, "write true sets the bit", bits_at(value, (U8) 4));

    bits_write(&value, (U8) 4, false);
    test_expect_false(test, "write false clears the bit", bits_at(value, (U8) 4));

    bits_write(&value, (U8) 4, true);
    bits_write(&value, (U8) 4, true);
    test_expect_true(test, "writing true twice is idempotent", bits_at(value, (U8) 4));
    test_expect_u(test, "writing true twice touches only that bit", (USize) 0x10, value);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: bits_count / bits_first_set / bits_last_set
 *============================================================================*/

static void _test_bits_scan(Test *const test) {
    test_case_begin(test, "bits_count / bits_first_set / bits_last_set");

    test_expect_u(test, "count of 0 is 0", 0, bits_count((USize) 0));
    test_expect_u(test, "count of all-ones is 64", 64, bits_count((USize) ~0ULL));
    test_expect_u(test, "count of 0xFF is 8", 8, bits_count((USize) 0xFF));

    test_expect_u(test, "first_set of 0 is BITS_INDEX_NONE", BITS_INDEX_NONE, bits_first_set((USize) 0));
    test_expect_u(test, "first_set of all-ones is 0", 0, bits_first_set((USize) ~0ULL));
    test_expect_u(test, "first_set of 0x10 is 4", 4, bits_first_set((USize) 0x10));

    test_expect_u(test, "last_set of 0 is BITS_INDEX_NONE", BITS_INDEX_NONE, bits_last_set((USize) 0));
    test_expect_u(test, "last_set of all-ones is 63", 63, bits_last_set((USize) ~0ULL));
    test_expect_u(test, "last_set of 0x10 is 4", 4, bits_last_set((USize) 0x10));

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: bits_array_*
 *============================================================================*/

static void _test_bits_array(Test *const test) {
    test_case_begin(test, "bits_array_set / _clear / _test");

    U64 words[3] = DEFAULT_INITIALIZATION;

    USize const sample_bits[] = { 0, 63, 64, 65, 127, 128, 130, 191 };

    for (USize i = 0; i < sizeof(sample_bits) / sizeof(sample_bits[0]); i += 1) {
        test_expect_false(test, "fresh zeroed array reads false", bits_array_test(words, (USize) 3, sample_bits[i]));
    }

    for (USize i = 0; i < sizeof(sample_bits) / sizeof(sample_bits[0]); i += 1) {
        bits_array_set(words, (USize) 3, sample_bits[i]);
        test_expect_true(test, "set bit reads true", bits_array_test(words, (USize) 3, sample_bits[i]));
    }

    /* Cross-word independence: bit 64 lives in words[1]; words[0] must be untouched. */
    U64 cross_words[3] = DEFAULT_INITIALIZATION;

    bits_array_set(cross_words, (USize) 3, (USize) 64);
    test_expect_u(test, "setting bit 64 leaves word 0 untouched", 0, cross_words[0]);
    test_expect_true(test, "setting bit 64 reads true in word 1", bits_array_test(cross_words, (USize) 3, (USize) 64));

    bits_array_clear(cross_words, (USize) 3, (USize) 64);
    test_expect_false(test, "clearing bit 64 reads false again", bits_array_test(cross_words, (USize) 3, (USize) 64));
    test_expect_u(test, "clearing bit 64 leaves word 0 untouched", 0, cross_words[0]);

    /* Clear of an unset bit is a no-op. */
    U64 clear_noop[3] = DEFAULT_INITIALIZATION;

    bits_array_clear(clear_noop, (USize) 3, (USize) 42);
    test_expect_u(test, "clearing an unset bit leaves word 0 zero", 0, clear_noop[0]);
    test_expect_u(test, "clearing an unset bit leaves word 1 zero", 0, clear_noop[1]);
    test_expect_u(test, "clearing an unset bit leaves word 2 zero", 0, clear_noop[2]);

    test_case_end(test);
}

static void _test_bits_array_any_count_clear_all(Test *const test) {
    test_case_begin(test, "bits_array_any / _count / _clear_all");

    U64 words[3] = DEFAULT_INITIALIZATION;

    test_expect_false(test, "any is false on a fresh array", bits_array_any(words, (USize) 3));
    test_expect_u(test, "count is 0 on a fresh array", 0, bits_array_count(words, (USize) 3));

    bits_array_set(words, (USize) 3, (USize) 5);
    bits_array_set(words, (USize) 3, (USize) 70);
    bits_array_set(words, (USize) 3, (USize) 190);

    test_expect_true(test, "any is true once a bit is set", bits_array_any(words, (USize) 3));
    test_expect_u(test, "count reflects every set bit across words", 3, bits_array_count(words, (USize) 3));

    bits_array_clear_all(words, (USize) 3);

    test_expect_false(test, "any is false after clear_all", bits_array_any(words, (USize) 3));
    test_expect_u(test, "count is 0 after clear_all", 0, bits_array_count(words, (USize) 3));
    test_expect_u(test, "clear_all zeroes word 0", 0, words[0]);
    test_expect_u(test, "clear_all zeroes word 1", 0, words[1]);
    test_expect_u(test, "clear_all zeroes word 2", 0, words[2]);

    /* self_size 0: false / 0 / no-op. */
    U64 empty_array[1] = { (U64) 0xFF };

    test_expect_false(test, "any over self_size 0 is false", bits_array_any(empty_array, (USize) 0));
    test_expect_u(test, "count over self_size 0 is 0", 0, bits_array_count(empty_array, (USize) 0));

    bits_array_clear_all(empty_array, (USize) 0);
    test_expect_u(test, "clear_all over self_size 0 leaves the array untouched", 0xFF, empty_array[0]);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: bits_format
 *============================================================================*/

static void _test_bits_format(Test *const test) {
    test_case_begin(test, "bits_format");

    char buffer[BITS_FORMAT_CAPACITY] = DEFAULT_INITIALIZATION;

    /* group_size 0: no separator at all. */
    USize written = bits_format((USize) 9, (U8) 4, '-', 0, buffer, sizeof(buffer));

    test_expect_u(test, "group_size 0: return equals strlen", char_length(buffer), written);
    test_expect_string(test, "group_size 0: 1001", "1001", buffer);

    /* group_size 4, self_size 1. */
    written = bits_format((USize) 1, (U8) 1, '-', 4, buffer, sizeof(buffer));
    test_expect_u(test, "self_size 1: return equals strlen", char_length(buffer), written);
    test_expect_string(test, "self_size 1: 1", "1", buffer);

    /* group_size 8, self_size 12: "0000-00001001". */
    written = bits_format((USize) 9, (U8) 12, '-', 8, buffer, sizeof(buffer));
    test_expect_u(test, "group_size 8: return equals strlen", char_length(buffer), written);
    test_expect_string(test, "group_size 8: 0000-00001001", "0000-00001001", buffer);

    /* self_size 64, all ones: 7 separators across 8 groups of 8. */
    written = bits_format((USize) ~0ULL, (U8) 64, ' ', 8, buffer, sizeof(buffer));
    test_expect_u(test, "self_size 64: return equals strlen", char_length(buffer), written);
    test_expect_string(test, "self_size 64: eight groups of ones", "11111111 11111111 11111111 11111111 11111111 11111111 11111111 11111111", buffer);

    /* Exact-boundary capacity: buffer_capacity == length + 1 (9 chars + terminator = 10). */
    char exact_buffer[10] = DEFAULT_INITIALIZATION;

    written = bits_format((USize) 5, (U8) 8, ' ', 4, exact_buffer, sizeof(exact_buffer));
    test_expect_u(test, "exact-fit capacity: return is 9", 9, written);
    test_expect_string(test, "exact-fit capacity: 0000 0101", "0000 0101", exact_buffer);
    test_expect_u(test, "exact-fit capacity: terminator at index 9", 0, (USize) exact_buffer[9]);

    /* Worst case: self_size 64, group_size 1, buffer_capacity == BITS_FORMAT_CAPACITY. */
    char worst_buffer[BITS_FORMAT_CAPACITY] = DEFAULT_INITIALIZATION;

    written = bits_format((USize) ~0ULL, (U8) 64, '-', 1, worst_buffer, sizeof(worst_buffer));
    test_expect_u(test, "worst case: return is 127", 127, written);
    test_expect_u(test, "worst case: terminator at index 127", 0, (USize) worst_buffer[127]);

    /* All 127 characters, not a prefix: every even index a digit, every odd one
     * the separator. A prefix compare would let the separator rule drift past
     * digit 8 and still pass on the length alone. */
    char worst_expected[BITS_FORMAT_CAPACITY] = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 127; i += 1) {
        worst_expected[i] = i % 2 == 0 ? '1' : '-';
    }

    test_expect_string(test, "worst case: 64 ones joined by 63 dashes", worst_expected, worst_buffer);
    test_expect_u(test, "worst case: length matches strlen", char_length(worst_buffer), written);

    /* group_size > self_size: no group is ever completed, so no separator appears. */
    written = bits_format((USize) 5, (U8) 4, ' ', 8, buffer, sizeof(buffer));
    test_expect_u(test, "group_size > self_size: return is 4", 4, written);
    test_expect_string(test, "group_size > self_size: 0101", "0101", buffer);

    /* '\0' separator: no separator regardless of group_size, return always equals strlen. */
    written = bits_format((USize) 9, (U8) 12, '\0', 8, buffer, sizeof(buffer));
    test_expect_u(test, "'\\0' separator: return is 12", 12, written);
    test_expect_string(test, "'\\0' separator: 000000001001", "000000001001", buffer);
    test_expect_u(test, "'\\0' separator: return equals strlen", char_length(buffer), written);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: bits_print_1 / bits_print_2
 *============================================================================*/

static void _test_bits_print(Test *const test) {
    test_case_begin(test, "bits_print_1 / bits_print_2");

    char captured[256] = DEFAULT_INITIALIZATION;

    _capture_print_2((USize) 5, (U8) 8, ' ', captured, sizeof(captured));
    test_expect_string(test, "value 5, size 8, separator ' '", "00000101", captured);

    _capture_print_2((USize) 12, (U8) 9, '-', captured, sizeof(captured));
    test_expect_string(test, "value 12, size 9, separator '-'", "0-00001100", captured);

    _capture_print_2((USize) 5, (U8) 8, '\0', captured, sizeof(captured));
    test_expect_string(test, "value 5, size 8, separator '\\0'", "00000101", captured);

    _capture_print_1((USize) 5, (U8) 8, captured, sizeof(captured));
    test_expect_string(test, "print_1(5, 8)", "00000101", captured);

    /* Masking pin: bit 8 lies outside the 8 rendered bits and must not leak in. */
    _capture_print_2((USize) 0x100, (U8) 8, ' ', captured, sizeof(captured));
    test_expect_string(test, "bit 8 masked out of an 8-bit rendering", "00000000", captured);

    /* self_size 65 is now an abort under checks, covered by the death-test table below;
     * nothing to capture here in the checked build. */

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: death tests
 *============================================================================*/

static void _test_bits_abort_probes(Test *const test) {
    test_case_begin(test, "error_check_* abort contract (subprocess)");

    _test_abort_probe(test, "bits_at(value, 64) aborts", "--child-at-out-of-bound", "OUT_OF_BOUND_UINT");
    _test_abort_probe(test, "bits_flip(nullptr, 0) aborts", "--child-flip-null", "NULL_POINTER");
    _test_abort_probe(test, "bits_flip(&v, 64) aborts", "--child-flip-out-of-bound", "OUT_OF_BOUND_UINT");
    _test_abort_probe(test, "bits_write(nullptr, 1, true) aborts", "--child-write-null", "NULL_POINTER");
    _test_abort_probe(test, "bits_write(&v, 64, true) aborts", "--child-write-out-of-bound", "OUT_OF_BOUND_UINT");
    _test_abort_probe(test, "bits_array_set(nullptr, 0, 0) aborts", "--child-array-set-null", "NULL_POINTER");
    _test_abort_probe(test, "bits_array_clear(nullptr, 0, 0) aborts", "--child-array-clear-null", "NULL_POINTER");
    _test_abort_probe(test, "bits_array_test(nullptr, 0, 0) aborts", "--child-array-test-null", "NULL_POINTER");
    _test_abort_probe(test, "bits_array_set(words, 2, 128) aborts", "--child-array-set-past", "OUT_OF_BOUND_UINT");
    _test_abort_probe(test, "bits_array_clear(words, 2, 128) aborts", "--child-array-clear-past", "OUT_OF_BOUND_UINT");
    _test_abort_probe(test, "bits_array_test(words, 2, 128) aborts", "--child-array-test-past", "OUT_OF_BOUND_UINT");
    _test_abort_probe(test, "bits_print_2(v, 0, ' ') aborts", "--child-print-2-non-value", "NON_VALUE");
    _test_abort_probe(test, "bits_print_2(v, 65, ' ') aborts", "--child-print-2-over-64", "OUT_OF_BOUND_UINT");
    _test_abort_probe(test, "bits_format with a too-small buffer aborts", "--child-format-buffer-too-small", "WRONG_VALUE");
    _test_abort_probe(test, "bits_array_any(nullptr, 1) aborts", "--child-array-any-null", "NULL_POINTER");
    _test_abort_probe(test, "bits_array_count(nullptr, 1) aborts", "--child-array-count-null", "NULL_POINTER");
    _test_abort_probe(test, "bits_array_clear_all(nullptr, 1) aborts", "--child-array-clear-all-null", "NULL_POINTER");
    _test_abort_probe(test, "bits_format(nullptr buffer) aborts", "--child-format-null", "NULL_POINTER");
    _test_abort_probe(test, "bits_format with capacity one short of length + 1 aborts", "--child-format-capacity-one-short", "WRONG_VALUE");

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Entry point
 *============================================================================*/

int main(int argc, char **argv) {
    /* Child modes come first: this process was spawned by a running probe and must
     * behave as the small child that probe asked for, never touching the harness below. */
    if (argc >= 2) {
        if (strcmp(argv[1], "--child-at-out-of-bound") == 0) {
            return _child_at_out_of_bound();
        }

        if (strcmp(argv[1], "--child-flip-null") == 0) {
            return _child_flip_null();
        }

        if (strcmp(argv[1], "--child-flip-out-of-bound") == 0) {
            return _child_flip_out_of_bound();
        }

        if (strcmp(argv[1], "--child-write-null") == 0) {
            return _child_write_null();
        }

        if (strcmp(argv[1], "--child-write-out-of-bound") == 0) {
            return _child_write_out_of_bound();
        }

        if (strcmp(argv[1], "--child-array-set-null") == 0) {
            return _child_array_set_null();
        }

        if (strcmp(argv[1], "--child-array-clear-null") == 0) {
            return _child_array_clear_null();
        }

        if (strcmp(argv[1], "--child-array-test-null") == 0) {
            return _child_array_test_null();
        }

        if (strcmp(argv[1], "--child-array-set-past") == 0) {
            return _child_array_set_past();
        }

        if (strcmp(argv[1], "--child-array-clear-past") == 0) {
            return _child_array_clear_past();
        }

        if (strcmp(argv[1], "--child-array-test-past") == 0) {
            return _child_array_test_past();
        }

        if (strcmp(argv[1], "--child-print-2-non-value") == 0) {
            return _child_print_2_non_value();
        }

        if (strcmp(argv[1], "--child-print-2-over-64") == 0) {
            return _child_print_2_over_64();
        }

        if (strcmp(argv[1], "--child-format-buffer-too-small") == 0) {
            return _child_format_buffer_too_small();
        }

        if (strcmp(argv[1], "--child-array-any-null") == 0) {
            return _child_array_any_null();
        }

        if (strcmp(argv[1], "--child-array-count-null") == 0) {
            return _child_array_count_null();
        }

        if (strcmp(argv[1], "--child-array-clear-all-null") == 0) {
            return _child_array_clear_all_null();
        }

        if (strcmp(argv[1], "--child-format-null") == 0) {
            return _child_format_null();
        }

        if (strcmp(argv[1], "--child-format-capacity-one-short") == 0) {
            return _child_format_capacity_one_short();
        }
    }

    LogConfig const log_config = {
        .level             = LOG_LEVEL_ERROR,
        .stream            = stdout,
        .timestamp_enabled = true,
        .autoflush         = true
    };

    log_init(log_config);

    _program = argv[0];

    Test test = test_init("tests/bits/test_all.c");

    test_suite_begin(&test, "bits");
    _test_bits_at(&test);
    _test_bits_flip(&test);
    _test_bits_write(&test);
    _test_bits_scan(&test);
    _test_bits_array(&test);
    _test_bits_array_any_count_clear_all(&test);
    _test_bits_format(&test);
    _test_bits_print(&test);
    _test_bits_abort_probes(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}