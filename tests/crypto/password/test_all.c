#include <char/char.h>
#include <crypto/password/password.h>
#include <log/log.h>
#include <test/test.h>

/* Coverage for crypto/password: the locked golden-vector compatibility
 * contract, char/Str/String round-trips through hash_1/_2/_3 and
 * verify_1/_2/_3, distinct salts per hash, fail-closed hashing on an
 * out-of-range iteration count, and the malformed-record surface for both
 * verify (never true, never a crash) and needs_rehash (never true).
 *
 * Iteration counts are kept low (1000) everywhere except the golden vector,
 * which must run at its real recorded count (210000) to be a faithful
 * compatibility check. */

#define _TEST_GOLDEN_STORED     "pbkdf2_sha256$210000$000102030405060708090a0b0c0d0e0f$18465cade096c185b5f60765891fca3f74477e23fd9b6fc88faf7831a91753ac"
#define _TEST_GOLDEN_PASSWORD   "correct horse battery staple"
#define _TEST_GOLDEN_SALT_HEX   "000102030405060708090a0b0c0d0e0f"
#define _TEST_GOLDEN_HASH_HEX   "18465cade096c185b5f60765891fca3f74477e23fd9b6fc88faf7831a91753ac"
#define _TEST_LOW_ITERATIONS    1000
#define _TEST_REHASH_ITERATIONS 600000

static void _test_golden_vector(Test *const test) {
    test_case_begin(test, "golden vector: locked stored format");

    test_expect_true(test, "correct password verifies",
        crypto_password_verify_1(_TEST_GOLDEN_PASSWORD, _TEST_GOLDEN_STORED));
    test_expect_false(test, "wrong password fails",
        crypto_password_verify_1("wrong horse", _TEST_GOLDEN_STORED));
    test_expect_false(test, "needs_rehash false at matching iterations",
        crypto_password_needs_rehash(_TEST_GOLDEN_STORED, 210000));
    test_expect_true(test, "needs_rehash true at a different iteration count",
        crypto_password_needs_rehash(_TEST_GOLDEN_STORED, _TEST_REHASH_ITERATIONS));

    test_case_end(test);
}

static void _test_round_trip_char(Test *const test) {
    test_case_begin(test, "hash_1/verify_1/_2/_3 round-trip");

    char const *const password = "hunter2 but longer";
    String stored              = crypto_password_hash_1(password, _TEST_LOW_ITERATIONS);

    test_expect_false(test, "hash is not empty", string_empty(&stored));
    test_expect_true(test, "hash carries the scheme/iterations prefix",
        char_starts_with_1(string_get_data(&stored), "pbkdf2_sha256$1000$"));
    test_expect_true(test, "verify_1 accepts the right password",
        crypto_password_verify_1(password, string_get_data(&stored)));
    test_expect_false(test, "verify_1 rejects the wrong password",
        crypto_password_verify_1("wrong password entirely", string_get_data(&stored)));

    Str const password_str = str_init_2((char*) password);

    test_expect_true(test, "verify_2 (Str) accepts the right password",
        crypto_password_verify_2(&password_str, string_get_data(&stored)));

    String password_string = string_init_1();

    string_add_last_1(&password_string, (char*) password);
    test_expect_true(test, "verify_3 (String) accepts the right password",
        crypto_password_verify_3(&password_string, string_get_data(&stored)));

    string_uninit(&password_string);
    string_uninit(&stored);

    test_case_end(test);
}

static void _test_distinct_salts(Test *const test) {
    test_case_begin(test, "two hashes of the same password differ but both verify");

    char const *const password = "same password twice";
    String first               = crypto_password_hash_1(password, _TEST_LOW_ITERATIONS);
    String second              = crypto_password_hash_1(password, _TEST_LOW_ITERATIONS);

    test_expect_true(test, "the two stored records differ",
        !char_compare_equal_1(string_get_data(&first), string_get_data(&second)));
    test_expect_true(test, "first record verifies",
        crypto_password_verify_1(password, string_get_data(&first)));
    test_expect_true(test, "second record verifies",
        crypto_password_verify_1(password, string_get_data(&second)));

    string_uninit(&second);
    string_uninit(&first);

    test_case_end(test);
}

static void _test_hash_2_hash_3(Test *const test) {
    test_case_begin(test, "hash_2 (Str) and hash_3 (String) round-trip once each");

    char const *const password = "another test password";
    Str const password_str     = str_init_2((char*) password);
    String stored_from_str     = crypto_password_hash_2(&password_str, _TEST_LOW_ITERATIONS);

    test_expect_false(test, "hash_2 result is not empty", string_empty(&stored_from_str));
    test_expect_true(test, "hash_2 result verifies via verify_1",
        crypto_password_verify_1(password, string_get_data(&stored_from_str)));
    string_uninit(&stored_from_str);

    String password_string = string_init_1();

    string_add_last_1(&password_string, (char*) password);

    String stored_from_string = crypto_password_hash_3(&password_string, _TEST_LOW_ITERATIONS);

    test_expect_false(test, "hash_3 result is not empty", string_empty(&stored_from_string));
    test_expect_true(test, "hash_3 result verifies via verify_1",
        crypto_password_verify_1(password, string_get_data(&stored_from_string)));

    string_uninit(&stored_from_string);
    string_uninit(&password_string);

    test_case_end(test);
}

static void _test_empty_password_values(Test *const test) {
    test_case_begin(test, "empty Str/String passwords are legal values, never aborts");

    // Regression pin for the memsec MED: an empty Str/String carries null
    // data, which previously reached the worker's null check and aborted.
    Str const empty_str        = str_init_2((char*) "");
    String empty_string        = string_init_1();
    String stored              = crypto_password_hash_1("", _TEST_LOW_ITERATIONS);

    test_expect_false(test, "empty char* password hashes to a record", string_empty(&stored));
    test_expect_true(test, "empty verifies via verify_1", crypto_password_verify_1("", string_get_data(&stored)));
    test_expect_true(test, "empty verifies via verify_2", crypto_password_verify_2(&empty_str, string_get_data(&stored)));
    test_expect_true(test, "empty verifies via verify_3", crypto_password_verify_3(&empty_string, string_get_data(&stored)));

    String stored_from_str = crypto_password_hash_2(&empty_str, _TEST_LOW_ITERATIONS);

    test_expect_false(test, "hash_2 of empty Str yields a record", string_empty(&stored_from_str));
    test_expect_true(test, "that record verifies via verify_1", crypto_password_verify_1("", string_get_data(&stored_from_str)));

    string_uninit(&stored_from_str);
    string_uninit(&stored);
    string_uninit(&empty_string);

    test_case_end(test);
}

static void _test_fail_closed_hashing(Test *const test) {
    test_case_begin(test, "hashing fails closed on out-of-range iteration counts");

    String zero_iterations = crypto_password_hash_1("some password", 0);

    test_expect_true(test, "iterations 0 yields an empty String", string_empty(&zero_iterations));
    string_uninit(&zero_iterations);

    String over_cap = crypto_password_hash_1("some password", CRYPTO_PASSWORD_ITERATIONS_MAX + 1);

    test_expect_true(test, "iterations above the cap yields an empty String", string_empty(&over_cap));
    string_uninit(&over_cap);

    test_case_end(test);
}

static void _test_malformed_records(Test *const test) {
    test_case_begin(test, "malformed stored records verify false and never need rehash");

    char const *const malformed[] = {
        "",
        "garbage",
        "pbkdf2_sha256",
        "pbkdf2_sha256$1000$00$00",
        _TEST_GOLDEN_STORED "$",
        "pbkdf2_sha512$1000$" _TEST_GOLDEN_SALT_HEX "$" _TEST_GOLDEN_HASH_HEX,
        "pbkdf2_sha256$0$" _TEST_GOLDEN_SALT_HEX "$" _TEST_GOLDEN_HASH_HEX,
        "pbkdf2_sha256$10000001$" _TEST_GOLDEN_SALT_HEX "$" _TEST_GOLDEN_HASH_HEX,
        "pbkdf2_sha256$99x9$" _TEST_GOLDEN_SALT_HEX "$" _TEST_GOLDEN_HASH_HEX,
        "pbkdf2_sha256$1000$" _TEST_GOLDEN_SALT_HEX "$" _TEST_GOLDEN_HASH_HEX "$extra",
        "pbkdf2_sha256$210000$000102030405060708090a0b0c0d0e0f$18465cade096c185b5f60765891fca3f74477e23fd9b6fc88faf7831a91753ad",
    };
    USize const malformed_count = sizeof(malformed) / sizeof(malformed[0]);

    for (USize i = 0; i < malformed_count; i += 1) {
        test_expect_false(test, "verify_1 rejects a malformed/tampered record",
            crypto_password_verify_1(_TEST_GOLDEN_PASSWORD, malformed[i]));
        test_expect_false(test, "needs_rehash rejects a malformed/tampered record",
            crypto_password_needs_rehash(malformed[i], 210000));
    }

    test_case_end(test);
}

int main(void) {
    log_init((LogConfig){ .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true });

    Test test = test_init("tests/crypto/password/test_all.c");

    test_suite_begin(&test, "crypto_password");
    _test_golden_vector(&test);
    _test_round_trip_char(&test);
    _test_distinct_salts(&test);
    _test_hash_2_hash_3(&test);
    _test_empty_password_values(&test);
    _test_fail_closed_hashing(&test);
    _test_malformed_records(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}