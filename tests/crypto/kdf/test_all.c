#include <string.h>

#include <char/char.h>
#include <crypto/kdf/kdf.h>
#include <log/log.h>
#include <test/test.h>

/* Coverage for crypto/kdf: crypto_kdf_pbkdf2_sha256 is checked byte-for-byte
 * against two published PBKDF2-HMAC-SHA256 test vectors (c=1 and c=80000),
 * determinism across repeated calls, a differing salt producing a differing
 * output, the dkLen-truncation property within the first SHA-256 block (16
 * and 32 bytes are valid prefixes of the 64-byte vector; bytes 33..63 are
 * NOT, since PBKDF2 only truncates within one 32-byte HMAC-SHA256 block), an
 * empty password succeeding as a legal value, every zero-size/zero-count
 * ARGUMENT rejection plus an iteration count one past the OpenSSL int
 * boundary, and password_size/salt_size/output_size one past that same
 * boundary (each rejected by the size value itself, before any buffer is
 * read, so a small real buffer is enough to exercise it).
 *
 * Null-pointer cases are deliberately absent: those checks are error_check_*,
 * which aborts the process under ERROR_CHECK_ENABLED, so they cannot be
 * observed from inside the suite. */

static bool _test_all_zero(U8 const *const data, USize const size) {
    for (USize i = 0; i < size; i += 1) {
        if (data[i] != 0) {
            return false;
        }
    }

    return true;
}

// Published PBKDF2-HMAC-SHA256 test vectors (well-known IETF test set).
static U8 const _test_kdf_vector_1_expected[64] = {
    0x55, 0xac, 0x04, 0x6e, 0x56, 0xe3, 0x08, 0x9f,
    0xec, 0x16, 0x91, 0xc2, 0x25, 0x44, 0xb6, 0x05,
    0xf9, 0x41, 0x85, 0x21, 0x6d, 0xde, 0x04, 0x65,
    0xe6, 0x8b, 0x9d, 0x57, 0xc2, 0x0d, 0xac, 0xbc,
    0x49, 0xca, 0x9c, 0xcc, 0xf1, 0x79, 0xb6, 0x45,
    0x99, 0x16, 0x64, 0xb3, 0x9d, 0x77, 0xef, 0x31,
    0x7c, 0x71, 0xb8, 0x45, 0xb1, 0xe3, 0x0b, 0xd5,
    0x09, 0x11, 0x20, 0x41, 0xd3, 0xa1, 0x97, 0x83
};

static U8 const _test_kdf_vector_2_expected[64] = {
    0x4d, 0xdc, 0xd8, 0xf6, 0x0b, 0x98, 0xbe, 0x21,
    0x83, 0x0c, 0xee, 0x5e, 0xf2, 0x27, 0x01, 0xf9,
    0x64, 0x1a, 0x44, 0x18, 0xd0, 0x4c, 0x04, 0x14,
    0xae, 0xff, 0x08, 0x87, 0x6b, 0x34, 0xab, 0x56,
    0xa1, 0xd4, 0x25, 0xa1, 0x22, 0x58, 0x33, 0x54,
    0x9a, 0xdb, 0x84, 0x1b, 0x51, 0xc9, 0xb3, 0x17,
    0x6a, 0x27, 0x2b, 0xde, 0xbb, 0xa1, 0xd0, 0x78,
    0x47, 0x8f, 0x62, 0xb3, 0x97, 0xf3, 0x3c, 0x8d
};

static void _test_crypto_kdf_vector_1(Test *const test) {
    test_case_begin(test, "PBKDF2-HMAC-SHA256 vector: passwd/salt/c=1/dkLen=64");

    char const *const password = "passwd";
    char const *const salt     = "salt";
    U8 output[64]              = DEFAULT_INITIALIZATION;

    Result const result = crypto_kdf_pbkdf2_sha256(password, char_length(password), (U8 const *) salt, char_length(salt), 1, output, sizeof(output));

    test_expect_true(test, "derivation succeeds", result_is_success(result));
    test_expect_true(test, "output matches the published vector", memcmp(output, _test_kdf_vector_1_expected, sizeof(output)) == 0);

    test_case_end(test);
}

static void _test_crypto_kdf_vector_2(Test *const test) {
    test_case_begin(test, "PBKDF2-HMAC-SHA256 vector: Password/NaCl/c=80000/dkLen=64");

    char const *const password = "Password";
    char const *const salt     = "NaCl";
    U8 output[64]              = DEFAULT_INITIALIZATION;

    Result const result = crypto_kdf_pbkdf2_sha256(password, char_length(password), (U8 const *) salt, char_length(salt), 80000, output, sizeof(output));

    test_expect_true(test, "derivation succeeds", result_is_success(result));
    test_expect_true(test, "output matches the published vector", memcmp(output, _test_kdf_vector_2_expected, sizeof(output)) == 0);

    test_case_end(test);
}

static void _test_crypto_kdf_determinism(Test *const test) {
    test_case_begin(test, "same inputs derive identical output; a different salt derives a different output");

    char const *const password   = "passwd";
    char const *const salt       = "salt";
    char const *const other_salt = "pepper";
    U8 first[32]                 = DEFAULT_INITIALIZATION;
    U8 second[32]                = DEFAULT_INITIALIZATION;
    U8 third[32]                 = DEFAULT_INITIALIZATION;

    Result const first_result  = crypto_kdf_pbkdf2_sha256(password, char_length(password), (U8 const *) salt, char_length(salt), 1, first, sizeof(first));
    Result const second_result = crypto_kdf_pbkdf2_sha256(password, char_length(password), (U8 const *) salt, char_length(salt), 1, second, sizeof(second));
    Result const third_result  = crypto_kdf_pbkdf2_sha256(password, char_length(password), (U8 const *) other_salt, char_length(other_salt), 1, third, sizeof(third));

    test_expect_true(test, "first derivation succeeds", result_is_success(first_result));
    test_expect_true(test, "second derivation succeeds", result_is_success(second_result));
    test_expect_true(test, "third derivation succeeds", result_is_success(third_result));
    test_expect_true(test, "same inputs derive identical output", memcmp(first, second, sizeof(first)) == 0);
    test_expect_true(test, "different salt derives a different output", memcmp(first, third, sizeof(first)) != 0);

    test_case_end(test);
}

static void _test_crypto_kdf_dklen_prefix(Test *const test) {
    test_case_begin(test, "dkLen 16 and 32 are prefixes of the first SHA-256 block (vector-1 inputs)");

    char const *const password = "passwd";
    char const *const salt     = "salt";
    U8 short_output[16]        = DEFAULT_INITIALIZATION;
    U8 medium_output[32]       = DEFAULT_INITIALIZATION;

    Result const short_result  = crypto_kdf_pbkdf2_sha256(password, char_length(password), (U8 const *) salt, char_length(salt), 1, short_output, sizeof(short_output));
    Result const medium_result = crypto_kdf_pbkdf2_sha256(password, char_length(password), (U8 const *) salt, char_length(salt), 1, medium_output, sizeof(medium_output));

    test_expect_true(test, "16-byte derivation succeeds", result_is_success(short_result));
    test_expect_true(test, "32-byte derivation succeeds", result_is_success(medium_result));
    test_expect_true(test, "16-byte output is a prefix of the vector", memcmp(short_output, _test_kdf_vector_1_expected, sizeof(short_output)) == 0);
    test_expect_true(test, "32-byte output is a prefix of the vector", memcmp(medium_output, _test_kdf_vector_1_expected, sizeof(medium_output)) == 0);

    test_case_end(test);
}

static void _test_crypto_kdf_empty_password(Test *const test) {
    test_case_begin(test, "an empty password is a legal value");

    char const *const password = "";
    char const *const salt     = "salt";
    U8 output[32]              = DEFAULT_INITIALIZATION;

    Result const result = crypto_kdf_pbkdf2_sha256(password, char_length(password), (U8 const *) salt, char_length(salt), 1, output, sizeof(output));

    test_expect_true(test, "derivation succeeds", result_is_success(result));
    test_expect_true(test, "output is not all-zero", !_test_all_zero(output, sizeof(output)));

    test_case_end(test);
}

static void _test_crypto_kdf_argument_rejection(Test *const test) {
    test_case_begin(test, "zero salt_size/iterations/output_size and over-INT_MAX password_size/salt_size/iterations/output_size return ARGUMENT");

    char const *const password = "passwd";
    U8 const salt[4]           = { 's', 'a', 'l', 't' };
    U8 output[32]              = DEFAULT_INITIALIZATION;
    USize const password_size  = char_length(password);

    Result const salt_zero       = crypto_kdf_pbkdf2_sha256(password, password_size, salt, 0, 1, output, sizeof(output));
    Result const iterations_zero = crypto_kdf_pbkdf2_sha256(password, password_size, salt, sizeof(salt), 0, output, sizeof(output));
    Result const output_zero     = crypto_kdf_pbkdf2_sha256(password, password_size, salt, sizeof(salt), 1, output, 0);
    Result const iterations_over = crypto_kdf_pbkdf2_sha256(password, password_size, salt, sizeof(salt), (USize) INT_MAX + 1, output, sizeof(output));
    Result const password_over   = crypto_kdf_pbkdf2_sha256(password, (USize) INT_MAX + 1, salt, sizeof(salt), 1, output, sizeof(output));
    Result const salt_over       = crypto_kdf_pbkdf2_sha256(password, password_size, salt, (USize) INT_MAX + 1, 1, output, sizeof(output));
    Result const output_over     = crypto_kdf_pbkdf2_sha256(password, password_size, salt, sizeof(salt), 1, output, (USize) INT_MAX + 1);

    test_expect_true(test, "salt_size 0 is an ARGUMENT error", result_category(salt_zero) == RESULT_CATEGORY_ARGUMENT);
    test_expect_true(test, "iterations 0 is an ARGUMENT error", result_category(iterations_zero) == RESULT_CATEGORY_ARGUMENT);
    test_expect_true(test, "output_size 0 is an ARGUMENT error", result_category(output_zero) == RESULT_CATEGORY_ARGUMENT);
    test_expect_true(test, "iterations above INT_MAX is an ARGUMENT error", result_category(iterations_over) == RESULT_CATEGORY_ARGUMENT);
    test_expect_true(test, "password_size above INT_MAX is an ARGUMENT error", result_category(password_over) == RESULT_CATEGORY_ARGUMENT);
    test_expect_true(test, "salt_size above INT_MAX is an ARGUMENT error", result_category(salt_over) == RESULT_CATEGORY_ARGUMENT);
    test_expect_true(test, "output_size above INT_MAX is an ARGUMENT error", result_category(output_over) == RESULT_CATEGORY_ARGUMENT);

    test_case_end(test);
}

int main(void) {
    log_init((LogConfig){ .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true });

    Test test = test_init("tests/crypto/kdf/test_all.c");

    test_suite_begin(&test, "crypto_kdf");
    _test_crypto_kdf_vector_1(&test);
    _test_crypto_kdf_vector_2(&test);
    _test_crypto_kdf_determinism(&test);
    _test_crypto_kdf_dklen_prefix(&test);
    _test_crypto_kdf_empty_password(&test);
    _test_crypto_kdf_argument_rejection(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}