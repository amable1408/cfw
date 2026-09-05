#include <string.h>

#include <char/char.h>
#include <crypto/random/random.h>
#include <log/log.h>
#include <test/test.h>

/* Coverage for crypto/random: crypto_random_bytes fills distinct non-zero
 * buffers, crypto_random_hex produces well-formed lowercase-hex tokens
 * (including a size that crosses the internal 64-byte chunk boundary) and
 * distinct tokens across calls, and crypto_random_uniform stays in range
 * across many draws, covers the bound == 1 fast path, and accepts the
 * largest possible bound (exercising the rejection-sampling threshold path).
 *
 * Null-pointer cases are deliberately absent: those checks are error_check_*,
 * which aborts the process under ERROR_CHECK_ENABLED, so they cannot be
 * observed from inside the suite. Zero sizes/bounds and an over-large hex
 * byte_count travel the Result channel (RESULT_CATEGORY_ARGUMENT) and ARE
 * covered below. */

static bool _test_all_zero(U8 const *const data, USize const size) {
    for (USize i = 0; i < size; i += 1) {
        if (data[i] != 0) {
            return false;
        }
    }

    return true;
}

static bool _test_is_hex_digit(char const ch) {
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
}

static void _test_crypto_random_bytes(Test *const test) {
    test_case_begin(test, "crypto_random_bytes fills distinct non-zero buffers");

    U8 first[32]  = DEFAULT_INITIALIZATION;
    U8 second[32] = DEFAULT_INITIALIZATION;

    test_expect_true(test, "first fill succeeds", result_is_success(crypto_random_bytes(first, sizeof(first))));
    test_expect_true(test, "second fill succeeds", result_is_success(crypto_random_bytes(second, sizeof(second))));

    test_expect_true(test, "first buffer is not all-zero", !_test_all_zero(first, sizeof(first)));
    test_expect_true(test, "second buffer is not all-zero", !_test_all_zero(second, sizeof(second)));
    test_expect_true(test, "the two buffers differ", memcmp(first, second, sizeof(first)) != 0);

    test_case_end(test);
}

static void _test_crypto_random_bytes_single(Test *const test) {
    test_case_begin(test, "crypto_random_bytes accepts size 1");

    U8 byte = 0;

    test_expect_true(test, "single-byte fill succeeds", result_is_success(crypto_random_bytes(&byte, sizeof(byte))));

    test_case_end(test);
}

static void _test_crypto_random_hex_32(Test *const test) {
    test_case_begin(test, "crypto_random_hex(32) is a well-formed token");

    char output[CRYPTO_RANDOM_HEX_SIZE(32) + CHAR_END_CHARACTER] = DEFAULT_INITIALIZATION;

    test_expect_true(test, "hex fill succeeds", result_is_success(crypto_random_hex(output, 32)));
    test_expect_u(test, "length is 64", 64, strlen(output));

    bool all_hex = true;

    for (USize i = 0; i < 64; i += 1) {
        if (!_test_is_hex_digit(output[i])) {
            all_hex = false;

            break;
        }
    }

    test_expect_true(test, "every char is a lowercase hex digit", all_hex);
    test_expect_true(test, "output is NUL-terminated at 64", output[64] == '\0');

    test_case_end(test);
}

static void _test_crypto_random_hex_sizes(Test *const test) {
    test_case_begin(test, "crypto_random_hex: byte_count 1 and a chunk-crossing 100");

    char small[CRYPTO_RANDOM_HEX_SIZE(1) + CHAR_END_CHARACTER] = DEFAULT_INITIALIZATION;

    test_expect_true(test, "byte_count 1 succeeds", result_is_success(crypto_random_hex(small, 1)));
    test_expect_u(test, "byte_count 1 length is 2", 2, strlen(small));

    char large[CRYPTO_RANDOM_HEX_SIZE(100) + CHAR_END_CHARACTER] = DEFAULT_INITIALIZATION;

    test_expect_true(test, "byte_count 100 succeeds", result_is_success(crypto_random_hex(large, 100)));
    test_expect_u(test, "byte_count 100 length is 200", 200, strlen(large));

    bool all_hex = true;

    for (USize i = 0; i < 200; i += 1) {
        if (!_test_is_hex_digit(large[i])) {
            all_hex = false;

            break;
        }
    }

    test_expect_true(test, "byte_count 100 is all hex digits", all_hex);

    test_case_end(test);
}

static void _test_crypto_random_hex_distinct(Test *const test) {
    test_case_begin(test, "crypto_random_hex: two tokens differ");

    char first[CRYPTO_RANDOM_HEX_SIZE(32) + CHAR_END_CHARACTER]  = DEFAULT_INITIALIZATION;
    char second[CRYPTO_RANDOM_HEX_SIZE(32) + CHAR_END_CHARACTER] = DEFAULT_INITIALIZATION;

    test_expect_true(test, "first token succeeds", result_is_success(crypto_random_hex(first, 32)));
    test_expect_true(test, "second token succeeds", result_is_success(crypto_random_hex(second, 32)));
    test_expect_true(test, "the two tokens differ", strcmp(first, second) != 0);

    test_case_end(test);
}

static void _test_crypto_random_uniform_bound_one(Test *const test) {
    test_case_begin(test, "crypto_random_uniform: bound 1 always yields 0");

    U64 value = 42;

    test_expect_true(test, "draw succeeds", result_is_success(crypto_random_uniform(1, &value)));
    test_expect_u(test, "value is 0", 0, (USize) value);

    test_case_end(test);
}

static void _test_crypto_random_uniform_range(Test *const test) {
    test_case_begin(test, "crypto_random_uniform: 1000 draws stay in [0, 6) with spread");

    U64 const bound = 6;
    bool in_range   = true;
    bool seen[6]    = DEFAULT_INITIALIZATION;
    USize distinct  = 0;

    for (USize i = 0; i < 1000; i += 1) {
        U64 value = 0;

        if (result_is_error(crypto_random_uniform(bound, &value))) {
            in_range = false;

            break;
        }

        if (value >= bound) {
            in_range = false;

            break;
        }

        seen[value] = true;
    }

    for (USize i = 0; i < bound; i += 1) {
        if (seen[i]) {
            distinct += 1;
        }
    }

    test_expect_true(test, "every draw is below the bound", in_range);
    test_expect_true(test, "at least two distinct values appeared", distinct >= 2);

    test_case_end(test);
}

static void _test_crypto_random_argument_rejection(Test *const test) {
    test_case_begin(test, "zero sizes/bounds and over-large hex byte_count return ARGUMENT");

    U8 byte     = 0;
    char hex[4] = DEFAULT_INITIALIZATION;
    U64 value   = 42;

    Result const bytes_zero   = crypto_random_bytes(&byte, 0);
    Result const hex_zero     = crypto_random_hex(hex, 0);
    Result const hex_overflow = crypto_random_hex(hex, USIZE_MAX);
    Result const bound_zero   = crypto_random_uniform(0, &value);

    test_expect_true(test, "bytes size 0 is an ARGUMENT error", result_category(bytes_zero) == RESULT_CATEGORY_ARGUMENT);
    test_expect_true(test, "hex byte_count 0 is an ARGUMENT error", result_category(hex_zero) == RESULT_CATEGORY_ARGUMENT);
    test_expect_true(test, "hex byte_count USIZE_MAX is an ARGUMENT error", result_category(hex_overflow) == RESULT_CATEGORY_ARGUMENT);
    test_expect_true(test, "uniform bound 0 is an ARGUMENT error", result_category(bound_zero) == RESULT_CATEGORY_ARGUMENT);
    test_expect_u(test, "uniform value untouched on failure", 42, (USize) value);

    test_case_end(test);
}

static void _test_crypto_random_uniform_max_bound(Test *const test) {
    test_case_begin(test, "crypto_random_uniform: bound U64_MAX exercises the threshold path");

    U64 value = 0;

    test_expect_true(test, "draw succeeds", result_is_success(crypto_random_uniform(U64_MAX, &value)));

    test_case_end(test);
}

int main(void) {
    log_init((LogConfig){ .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true });

    Test test = test_init("tests/crypto/random/test_all.c");

    test_suite_begin(&test, "crypto_random");
    _test_crypto_random_bytes(&test);
    _test_crypto_random_bytes_single(&test);
    _test_crypto_random_hex_32(&test);
    _test_crypto_random_hex_sizes(&test);
    _test_crypto_random_hex_distinct(&test);
    _test_crypto_random_uniform_bound_one(&test);
    _test_crypto_random_uniform_range(&test);
    _test_crypto_random_argument_rejection(&test);
    _test_crypto_random_uniform_max_bound(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}