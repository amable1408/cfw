#include <string.h>

#include <char/char.h>
#include <encoding/hex/hex.h>
#include <log/log.h>
#include <test/test.h>

/* Coverage for encoding/hex: encode_2 known vectors in both cases plus NUL
 * termination and the size-0 empty-string no-op, decode_1 round-tripping a
 * 32-byte pattern and accepting mixed-case digits, and decode_1's ARGUMENT
 * rejections (odd input_size, a length mismatch against output_size, and
 * non-hex characters) plus its size-0 success no-op.
 *
 * Null-pointer cases are deliberately absent: those checks are error_check_*,
 * which aborts the process under ERROR_CHECK_ENABLED, so they cannot be
 * observed from inside the suite. */

static void _test_encoding_hex_encode_known_vectors(Test *const test) {
    test_case_begin(test, "encode_2: 0xdeadbeef in both cases");

    U8 const bytes[4]                                     = { 0xde, 0xad, 0xbe, 0xef };
    char lower[ENCODING_HEX_SIZE(4) + CHAR_END_CHARACTER] = DEFAULT_INITIALIZATION;
    char upper[ENCODING_HEX_SIZE(4) + CHAR_END_CHARACTER] = DEFAULT_INITIALIZATION;

    encoding_hex_encode_2(bytes, sizeof(bytes), lower, false);
    encoding_hex_encode_2(bytes, sizeof(bytes), upper, true);

    test_expect_true(test, "lowercase is \"deadbeef\"", strcmp(lower, "deadbeef") == 0);
    test_expect_true(test, "uppercase is \"DEADBEEF\"", strcmp(upper, "DEADBEEF") == 0);

    test_case_end(test);
}

static void _test_encoding_hex_encode_1_delegates(Test *const test) {
    test_case_begin(test, "encode_1 delegates to lowercase encode_2");

    U8 const bytes[4] = { 0xde, 0xad, 0xbe, 0xef };
    char output[ENCODING_HEX_SIZE(4) + CHAR_END_CHARACTER] = DEFAULT_INITIALIZATION;

    encoding_hex_encode_1(bytes, sizeof(bytes), output);

    test_expect_true(test, "output is \"deadbeef\"", strcmp(output, "deadbeef") == 0);

    test_case_end(test);
}

static void _test_encoding_hex_encode_terminates(Test *const test) {
    test_case_begin(test, "encode_1 NUL-terminates at size * 2");

    U8 const bytes[2] = { 0xAB, 0xCD };
    char output[ENCODING_HEX_SIZE(2) + CHAR_END_CHARACTER] = DEFAULT_INITIALIZATION;

    encoding_hex_encode_1(bytes, sizeof(bytes), output);

    test_expect_u(test, "length is 4", 4, strlen(output));
    test_expect_true(test, "output[4] is NUL", output[4] == '\0');

    test_case_end(test);
}

static void _test_encoding_hex_encode_size_zero(Test *const test) {
    test_case_begin(test, "encode_1 size 0 yields an empty string");

    U8 const byte = 0;
    char output[ENCODING_HEX_SIZE(0) + CHAR_END_CHARACTER] = DEFAULT_INITIALIZATION;

    encoding_hex_encode_1(&byte, 0, output);

    test_expect_true(test, "output is \"\"", strcmp(output, "") == 0);

    test_case_end(test);
}

static void _test_encoding_hex_round_trip(Test *const test) {
    test_case_begin(test, "encode then decode a 32-byte pattern round-trips");

    U8 original[32] = DEFAULT_INITIALIZATION;
    char hex[ENCODING_HEX_SIZE(32) + CHAR_END_CHARACTER] = DEFAULT_INITIALIZATION;
    U8 decoded[32] = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < sizeof(original); i += 1) {
        original[i] = (U8) (i * 7 + 1);
    }

    encoding_hex_encode_1(original, sizeof(original), hex);

    Result const result = encoding_hex_decode_1(hex, strlen(hex), decoded, sizeof(decoded));

    test_expect_true(test, "decode succeeds", result_is_success(result));
    test_expect_true(test, "decoded matches original", memcmp(original, decoded, sizeof(original)) == 0);

    test_case_end(test);
}

static void _test_encoding_hex_decode_mixed_case(Test *const test) {
    test_case_begin(test, "decode_1 accepts mixed-case digits");

    char const *const input = "DeAdBeEf";
    U8 decoded[4]        = DEFAULT_INITIALIZATION;
    U8 const expected[4] = { 0xde, 0xad, 0xbe, 0xef };

    Result const result = encoding_hex_decode_1(input, strlen(input), decoded, sizeof(decoded));

    test_expect_true(test, "decode succeeds", result_is_success(result));
    test_expect_true(test, "decoded matches expected bytes", memcmp(decoded, expected, sizeof(expected)) == 0);

    test_case_end(test);
}

static void _test_encoding_hex_decode_size_zero(Test *const test) {
    test_case_begin(test, "decode_1 size 0 succeeds as a no-op");

    U8 output = 0;

    Result const result = encoding_hex_decode_1("", 0, &output, 0);

    test_expect_true(test, "decode succeeds", result_is_success(result));

    test_case_end(test);
}

static void _test_encoding_hex_decode_rejections(Test *const test) {
    test_case_begin(test, "decode_1 rejects odd length, length mismatch, and non-hex chars");

    U8 output[2] = DEFAULT_INITIALIZATION;

    Result const odd_length       = encoding_hex_decode_1("abc", 3, output, 2);
    // The empty-input pairing is the vector that genuinely pins the oversize
    // guard: without it the wrapped product (0) equals input_size, the
    // strictness check passes, and the loop would write 2^63 bytes.
    Result const oversize         = encoding_hex_decode_1("", 0, output, USIZE_MAX / 2 + 1);
    Result const length_mismatch  = encoding_hex_decode_1("abcd", 4, output, 3);
    Result const bad_char_g       = encoding_hex_decode_1("gg00", 4, output, 2);
    Result const bad_char_space   = encoding_hex_decode_1("0 00", 4, output, 2);
    Result const bad_char_nul     = encoding_hex_decode_1("0\000000", 4, output, 2);

    test_expect_true(test, "odd input_size is ARGUMENT", result_category(odd_length) == RESULT_CATEGORY_ARGUMENT);
    test_expect_true(test, "output_size beyond USIZE_MAX/2 is ARGUMENT", result_category(oversize) == RESULT_CATEGORY_ARGUMENT);
    test_expect_true(test, "length mismatch is ARGUMENT", result_category(length_mismatch) == RESULT_CATEGORY_ARGUMENT);
    test_expect_true(test, "'g' digit is ARGUMENT", result_category(bad_char_g) == RESULT_CATEGORY_ARGUMENT);
    test_expect_true(test, "space digit is ARGUMENT", result_category(bad_char_space) == RESULT_CATEGORY_ARGUMENT);
    test_expect_true(test, "NUL digit is ARGUMENT", result_category(bad_char_nul) == RESULT_CATEGORY_ARGUMENT);

    test_case_end(test);
}

int main(void) {
    log_init((LogConfig){ .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true });

    Test test = test_init("tests/encoding/hex/test_all.c");

    test_suite_begin(&test, "encoding_hex");
    _test_encoding_hex_encode_known_vectors(&test);
    _test_encoding_hex_encode_1_delegates(&test);
    _test_encoding_hex_encode_terminates(&test);
    _test_encoding_hex_encode_size_zero(&test);
    _test_encoding_hex_round_trip(&test);
    _test_encoding_hex_decode_mixed_case(&test);
    _test_encoding_hex_decode_size_zero(&test);
    _test_encoding_hex_decode_rejections(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}