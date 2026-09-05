#include <char/char.h>
#include <crypto/hash/hash.h>
#include <encoding/hex/hex.h>
#include <log/log.h>
#include <test/test.h>

/* Coverage for crypto/hash: the four NIST one-shot vectors (SHA-256/SHA-512
 * of "" and "abc"), the classic quick-brown-fox SHA-256 pin (byte-compatible
 * with OpenSSL's SHA256() one-shot), streaming ≡ one-shot equivalence over
 * chunked input including a zero-size update, the NIST million-'a' vector
 * fed as 1000 × 1000-byte updates, crypto_hash_size for both algorithms
 * plus the fail-closed 0 on an unknown value, init rejecting an unknown algorithm,
 * the stale-handle surface (update/final on a never-initialized context,
 * final after final, idempotent uninit), the sized-final contract (an
 * undersized buffer rejected as ARGUMENT without consuming the stream, then
 * a right-sized retry succeeding), and the abandon path (uninit on a live
 * mid-stream context, with later calls failing closed).
 *
 * Null-pointer cases are deliberately absent: those checks are error_check_*,
 * which aborts the process under ERROR_CHECK_ENABLED, so they cannot be
 * observed from inside the suite. The OpenSSL failure branch is likewise
 * absent — EVP digests cannot be made to fail without fault injection (the
 * same known gap recorded for crypto/random and crypto/kdf). */

#define _TEST_MILLION_CHUNK_COUNT   1000
#define _TEST_MILLION_CHUNK_SIZE    1000

// NIST FIPS 180 test vectors, lowercase hex.
#define _TEST_SHA256_ABC        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
#define _TEST_SHA256_EMPTY      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
#define _TEST_SHA256_FOX        "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592"
#define _TEST_SHA256_MILLION_A  "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"
#define _TEST_SHA512_ABC        "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"
#define _TEST_SHA512_EMPTY      "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e"

static bool _test_digest_hex_equal(U8 const *const digest, USize const digest_size, char const *const expected_hex) {
    char hex[ENCODING_HEX_SIZE(CRYPTO_HASH_SHA512_SIZE) + CHAR_END_CHARACTER] = DEFAULT_INITIALIZATION;

    encoding_hex_encode_1(digest, digest_size, hex);

    return char_compare_equal_1(hex, (char*) expected_hex);
}

static void _test_crypto_hash_sha256_vectors(Test *const test) {
    test_case_begin(test, "SHA-256 one-shot: NIST vectors for \"\" and \"abc\", plus the fox pin");

    char const *const abc = "abc";
    char const *const fox = "The quick brown fox jumps over the lazy dog";
    U8 digest[CRYPTO_HASH_SHA256_SIZE] = DEFAULT_INITIALIZATION;

    test_expect_true(test, "empty message digests successfully", result_is_success(crypto_hash_sha256((U8 const*) "", 0, digest)));
    test_expect_true(test, "empty digest matches the vector", _test_digest_hex_equal(digest, sizeof(digest), _TEST_SHA256_EMPTY));

    test_expect_true(test, "\"abc\" digests successfully", result_is_success(crypto_hash_sha256((U8 const*) abc, char_length(abc), digest)));
    test_expect_true(test, "\"abc\" digest matches the vector", _test_digest_hex_equal(digest, sizeof(digest), _TEST_SHA256_ABC));

    test_expect_true(test, "fox digests successfully", result_is_success(crypto_hash_sha256((U8 const*) fox, char_length(fox), digest)));
    test_expect_true(test, "matches the SHA256() one-shot value", _test_digest_hex_equal(digest, sizeof(digest), _TEST_SHA256_FOX));

    test_case_end(test);
}

static void _test_crypto_hash_sha512_vectors(Test *const test) {
    test_case_begin(test, "SHA-512 one-shot: NIST vectors for \"\" and \"abc\"");

    char const *const abc = "abc";
    U8 digest[CRYPTO_HASH_SHA512_SIZE] = DEFAULT_INITIALIZATION;

    test_expect_true(test, "empty message digests successfully", result_is_success(crypto_hash_sha512((U8 const*) "", 0, digest)));
    test_expect_true(test, "empty digest matches the vector", _test_digest_hex_equal(digest, sizeof(digest), _TEST_SHA512_EMPTY));

    test_expect_true(test, "\"abc\" digests successfully", result_is_success(crypto_hash_sha512((U8 const*) abc, char_length(abc), digest)));
    test_expect_true(test, "\"abc\" digest matches the vector", _test_digest_hex_equal(digest, sizeof(digest), _TEST_SHA512_ABC));

    test_case_end(test);
}

static void _test_crypto_hash_streaming_equivalence(Test *const test) {
    test_case_begin(test, "streaming chunked \"abc\" (with a zero-size update) equals the one-shot vector, both algorithms");

    Crypto_Hash hash = DEFAULT_INITIALIZATION;
    U8 digest[CRYPTO_HASH_SHA512_SIZE] = DEFAULT_INITIALIZATION;

    test_expect_true(test, "sha256 init succeeds", result_is_success(crypto_hash_init(&hash, CRYPTO_HASH_ALGORITHM_SHA256)));
    test_expect_true(test, "update \"a\" succeeds", result_is_success(crypto_hash_update(&hash, (U8 const*) "a", 1)));
    test_expect_true(test, "zero-size update is legal", result_is_success(crypto_hash_update(&hash, (U8 const*) "", 0)));
    test_expect_true(test, "update \"bc\" succeeds", result_is_success(crypto_hash_update(&hash, (U8 const*) "bc", 2)));
    test_expect_true(test, "final succeeds into an over-sized buffer", result_is_success(crypto_hash_final(&hash, digest, sizeof(digest))));
    test_expect_true(test, "chunked sha256 digest matches the one-shot vector", _test_digest_hex_equal(digest, CRYPTO_HASH_SHA256_SIZE, _TEST_SHA256_ABC));

    test_expect_true(test, "sha512 init succeeds", result_is_success(crypto_hash_init(&hash, CRYPTO_HASH_ALGORITHM_SHA512)));
    test_expect_true(test, "update \"ab\" succeeds", result_is_success(crypto_hash_update(&hash, (U8 const*) "ab", 2)));
    test_expect_true(test, "update \"c\" succeeds", result_is_success(crypto_hash_update(&hash, (U8 const*) "c", 1)));
    test_expect_true(test, "final succeeds", result_is_success(crypto_hash_final(&hash, digest, sizeof(digest))));
    test_expect_true(test, "chunked sha512 digest matches the one-shot vector", _test_digest_hex_equal(digest, CRYPTO_HASH_SHA512_SIZE, _TEST_SHA512_ABC));

    test_case_end(test);
}

static void _test_crypto_hash_streaming_million(Test *const test) {
    test_case_begin(test, "NIST million-'a' vector via 1000 x 1000-byte updates");

    Crypto_Hash hash = DEFAULT_INITIALIZATION;
    U8 chunk[_TEST_MILLION_CHUNK_SIZE];
    U8 digest[CRYPTO_HASH_SHA256_SIZE] = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < sizeof(chunk); i += 1) {
        chunk[i] = (U8) 'a';
    }

    test_expect_true(test, "init succeeds", result_is_success(crypto_hash_init(&hash, CRYPTO_HASH_ALGORITHM_SHA256)));

    bool updates_ok = true;

    for (USize i = 0; i < _TEST_MILLION_CHUNK_COUNT && updates_ok; i += 1) {
        updates_ok = result_is_success(crypto_hash_update(&hash, chunk, sizeof(chunk)));
    }

    test_expect_true(test, "all 1000 updates succeed", updates_ok);
    test_expect_true(test, "final succeeds", result_is_success(crypto_hash_final(&hash, digest, sizeof(digest))));
    test_expect_true(test, "digest matches the million-'a' vector", _test_digest_hex_equal(digest, sizeof(digest), _TEST_SHA256_MILLION_A));

    test_case_end(test);
}

static void _test_crypto_hash_size(Test *const test) {
    test_case_begin(test, "crypto_hash_size: 32/64 for the algorithms, fail-closed 0 on an unknown value");

    test_expect_true(test, "sha256 size is 32", crypto_hash_size(CRYPTO_HASH_ALGORITHM_SHA256) == CRYPTO_HASH_SHA256_SIZE);
    test_expect_true(test, "sha512 size is 64", crypto_hash_size(CRYPTO_HASH_ALGORITHM_SHA512) == CRYPTO_HASH_SHA512_SIZE);
    test_expect_true(test, "unknown algorithm sizes to 0", crypto_hash_size((Crypto_Hash_Algorithm) 999) == 0);

    test_case_end(test);
}

static void _test_crypto_hash_stale_handles(Test *const test) {
    test_case_begin(test, "stale handles fail closed: unknown-algorithm init, use before init, use after final, idempotent uninit");

    Crypto_Hash hash = DEFAULT_INITIALIZATION;
    U8 digest[CRYPTO_HASH_SHA256_SIZE] = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init with an unknown algorithm is an ARGUMENT error", result_category(crypto_hash_init(&hash, (Crypto_Hash_Algorithm) 999)) == RESULT_CATEGORY_ARGUMENT);
    test_expect_true(test, "update on a never-initialized context is an ARGUMENT error", result_category(crypto_hash_update(&hash, (U8 const*) "a", 1)) == RESULT_CATEGORY_ARGUMENT);
    test_expect_true(test, "final on a never-initialized context is an ARGUMENT error", result_category(crypto_hash_final(&hash, digest, sizeof(digest))) == RESULT_CATEGORY_ARGUMENT);

    test_expect_true(test, "real init succeeds", result_is_success(crypto_hash_init(&hash, CRYPTO_HASH_ALGORITHM_SHA256)));
    test_expect_true(test, "final succeeds", result_is_success(crypto_hash_final(&hash, digest, sizeof(digest))));
    test_expect_true(test, "the context is released after final", hash.context == nullptr);
    test_expect_true(test, "a second final is an ARGUMENT error", result_category(crypto_hash_final(&hash, digest, sizeof(digest))) == RESULT_CATEGORY_ARGUMENT);

    crypto_hash_uninit(&hash);
    crypto_hash_uninit(&hash);
    test_expect_true(test, "uninit is idempotent on a released context", hash.context == nullptr);

    test_case_end(test);
}

static void _test_crypto_hash_final_sizing(Test *const test) {
    test_case_begin(test, "an undersized final buffer is rejected without consuming the stream");

    Crypto_Hash hash = DEFAULT_INITIALIZATION;
    U8 small[CRYPTO_HASH_SHA256_SIZE]  = DEFAULT_INITIALIZATION;
    U8 digest[CRYPTO_HASH_SHA512_SIZE] = DEFAULT_INITIALIZATION;

    test_expect_true(test, "sha512 init succeeds", result_is_success(crypto_hash_init(&hash, CRYPTO_HASH_ALGORITHM_SHA512)));
    test_expect_true(test, "update \"abc\" succeeds", result_is_success(crypto_hash_update(&hash, (U8 const*) "abc", 3)));
    test_expect_true(test, "a 32-byte buffer for a sha512 final is an ARGUMENT error", result_category(crypto_hash_final(&hash, small, sizeof(small))) == RESULT_CATEGORY_ARGUMENT);
    test_expect_true(test, "the rejection left the stream intact", hash.context != nullptr);
    test_expect_true(test, "a right-sized retry succeeds", result_is_success(crypto_hash_final(&hash, digest, sizeof(digest))));
    test_expect_true(test, "the retried digest matches the vector", _test_digest_hex_equal(digest, sizeof(digest), _TEST_SHA512_ABC));

    test_case_end(test);
}

static void _test_crypto_hash_abandon(Test *const test) {
    test_case_begin(test, "uninit on a live mid-stream context releases it and later calls fail closed");

    Crypto_Hash hash = DEFAULT_INITIALIZATION;
    U8 digest[CRYPTO_HASH_SHA256_SIZE] = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init succeeds", result_is_success(crypto_hash_init(&hash, CRYPTO_HASH_ALGORITHM_SHA256)));
    test_expect_true(test, "update succeeds", result_is_success(crypto_hash_update(&hash, (U8 const*) "abc", 3)));

    crypto_hash_uninit(&hash);
    test_expect_true(test, "the live context is released", hash.context == nullptr);
    test_expect_true(test, "update after abandon is an ARGUMENT error", result_category(crypto_hash_update(&hash, (U8 const*) "a", 1)) == RESULT_CATEGORY_ARGUMENT);
    test_expect_true(test, "final after abandon is an ARGUMENT error", result_category(crypto_hash_final(&hash, digest, sizeof(digest))) == RESULT_CATEGORY_ARGUMENT);

    test_case_end(test);
}

int main(void) {
    log_init((LogConfig){ .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true });

    Test test = test_init("tests/crypto/hash/test_all.c");

    test_suite_begin(&test, "crypto_hash");
    _test_crypto_hash_sha256_vectors(&test);
    _test_crypto_hash_sha512_vectors(&test);
    _test_crypto_hash_streaming_equivalence(&test);
    _test_crypto_hash_streaming_million(&test);
    _test_crypto_hash_size(&test);
    _test_crypto_hash_stale_handles(&test);
    _test_crypto_hash_final_sizing(&test);
    _test_crypto_hash_abandon(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}