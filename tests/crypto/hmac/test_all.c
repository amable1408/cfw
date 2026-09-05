#include <crypto/hmac/hmac.h>
#include <test/test.h>

/* Coverage for crypto/hmac: the published RFC 4231 vectors for HMAC-SHA256 and
 * HMAC-SHA512 (case 1 short binary key, case 2 ASCII key, case 3 long binary
 * message, case 6 key LONGER than the hash block size — the case that
 * exercises the key-is-hashed-first path), the pre-existing OpenSSL-CLI
 * vectors, uppercase-hex output, raw-byte output agreeing with the hex form,
 * the char / Str / String overloads, empty keys and messages as legal values
 * (including empty Str/String, whose null data must not reach the worker), and
 * the verify surface: exact match, case-insensitive match, tampered digest,
 * and wrong-length expected hex.
 *
 * Every RFC vector here was re-derived with `openssl dgst -mac HMAC` before
 * being written down, so a failure means the implementation is wrong — never
 * adjust a vector to match the code.
 *
 * Null-pointer cases are deliberately absent: those checks are error_check_*,
 * which aborts the process under ERROR_CHECK_ENABLED. The OpenSSL failure
 * branch is likewise absent — EVP_Q_mac cannot be made to fail without fault
 * injection (the same known gap recorded for crypto/random and crypto/kdf). */

#define _TEST_LONG_KEY_SIZE     131  /* RFC 4231 case 6: exceeds both algorithms' block size. */
#define _TEST_LONG_MESSAGE_SIZE 50   /* RFC 4231 case 3: 50 repeated bytes. */
#define _TEST_SHORT_KEY_SIZE    20   /* RFC 4231 cases 1 and 3. */

#define _TEST_KEY_BYTE_AA       0xaa
#define _TEST_KEY_BYTE_0B       0x0b
#define _TEST_MESSAGE_BYTE_DD   0xdd

// RFC 4231 case 1: key = 20 x 0x0b, message = "Hi There".
#define _TEST_TC1_MESSAGE   "Hi There"
#define _TEST_TC1_SHA256    "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"
#define _TEST_TC1_SHA512    "87aa7cdea5ef619d4ff0b4241a1d6cb02379f4e2ce4ec2787ad0b30545e17cdedaa833b7d6b8a702038b274eaea3f4e4be9d914eeb61f1702e696c203a126854"

// RFC 4231 case 2: key = "Jefe".
#define _TEST_TC2_KEY           "Jefe"
#define _TEST_TC2_MESSAGE       "what do ya want for nothing?"
#define _TEST_TC2_SHA256        "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"
#define _TEST_TC2_SHA256_UPPER  "5BDCC146BF60754E6A042426089575C75A003F089D2739839DEC58B964EC3843"
#define _TEST_TC2_SHA512        "164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea2505549758bf75c05a994a6d034f65f8f0e6fdcaeab1a34d4a6b4b636e070a38bce737"

// RFC 4231 case 3: key = 20 x 0xaa, message = 50 x 0xdd.
#define _TEST_TC3_SHA256 "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe"

// RFC 4231 case 6: key = 131 x 0xaa (longer than the block size).
#define _TEST_TC6_MESSAGE   "Test Using Larger Than Block-Size Key - Hash Key First"
#define _TEST_TC6_SHA256    "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"
#define _TEST_TC6_SHA512    "80b24263c7c1a3ebb71493c1dd7be8b49b46d1f41b4aeec1121b013783f8f3526b56d037e05f2598bd0fd2215d6a1e5295e64f73f63f0aec8b915a985d786598"

// Pre-existing vectors (verified with `openssl dgst -sha256 -hmac key`).
#define _TEST_KEY               "key"
#define _TEST_MESSAGE           "The quick brown fox jumps over the lazy dog"
#define _TEST_MESSAGE_SHA256    "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8"
#define _TEST_EMPTY_SHA256      "5d5d139563c95b5967b9bd9a8c9b233a9dedb45072794cd232dc1b74832607d0"

static void _test_fill(char *const buffer, USize const size, U8 const value) {
    for (USize i = 0; i < size; i += 1) {
        buffer[i] = (char) value;
    }
}

static void _test_rfc4231_short_keys(Test *const test) {
    test_case_begin(test, "RFC 4231 cases 1-3: short binary key, ASCII key, long binary message");

    char    key[_TEST_SHORT_KEY_SIZE]                                   = DEFAULT_INITIALIZATION;
    char    message[_TEST_LONG_MESSAGE_SIZE]                            = DEFAULT_INITIALIZATION;
    char    hex[CRYPTO_HMAC_SHA512_HEX_SIZE + CHAR_END_CHARACTER]       = DEFAULT_INITIALIZATION;

    _test_fill(key, sizeof(key), _TEST_KEY_BYTE_0B);

    test_expect_true(test, "case 1 sha256 computes", result_is_success(crypto_hmac_sha256_hex_1(key, sizeof(key), _TEST_TC1_MESSAGE, char_length(_TEST_TC1_MESSAGE), hex)));
    test_expect_string(test, "case 1 sha256 digest", _TEST_TC1_SHA256, hex);
    test_expect_true(test, "case 1 sha512 computes", result_is_success(crypto_hmac_sha512_hex_1(key, sizeof(key), _TEST_TC1_MESSAGE, char_length(_TEST_TC1_MESSAGE), hex)));
    test_expect_string(test, "case 1 sha512 digest", _TEST_TC1_SHA512, hex);

    test_expect_true(test, "case 2 sha256 computes", result_is_success(crypto_hmac_sha256_hex_1(_TEST_TC2_KEY, char_length(_TEST_TC2_KEY), _TEST_TC2_MESSAGE, char_length(_TEST_TC2_MESSAGE), hex)));
    test_expect_string(test, "case 2 sha256 digest", _TEST_TC2_SHA256, hex);
    test_expect_true(test, "case 2 sha512 computes", result_is_success(crypto_hmac_sha512_hex_1(_TEST_TC2_KEY, char_length(_TEST_TC2_KEY), _TEST_TC2_MESSAGE, char_length(_TEST_TC2_MESSAGE), hex)));
    test_expect_string(test, "case 2 sha512 digest", _TEST_TC2_SHA512, hex);

    _test_fill(key, sizeof(key), _TEST_KEY_BYTE_AA);
    _test_fill(message, sizeof(message), _TEST_MESSAGE_BYTE_DD);

    test_expect_true(test, "case 3 sha256 computes", result_is_success(crypto_hmac_sha256_hex_1(key, sizeof(key), message, sizeof(message), hex)));
    test_expect_string(test, "case 3 sha256 digest", _TEST_TC3_SHA256, hex);

    test_case_end(test);
}

static void _test_rfc4231_long_key(Test *const test) {
    test_case_begin(test, "RFC 4231 case 6: key longer than the block size is hashed first");

    char key[_TEST_LONG_KEY_SIZE]                               = DEFAULT_INITIALIZATION;
    char hex[CRYPTO_HMAC_SHA512_HEX_SIZE + CHAR_END_CHARACTER]  = DEFAULT_INITIALIZATION;

    _test_fill(key, sizeof(key), _TEST_KEY_BYTE_AA);

    test_expect_true(test, "sha256 computes", result_is_success(crypto_hmac_sha256_hex_1(key, sizeof(key), _TEST_TC6_MESSAGE, char_length(_TEST_TC6_MESSAGE), hex)));
    test_expect_string(test, "sha256 digest", _TEST_TC6_SHA256, hex);
    test_expect_true(test, "sha512 computes", result_is_success(crypto_hmac_sha512_hex_1(key, sizeof(key), _TEST_TC6_MESSAGE, char_length(_TEST_TC6_MESSAGE), hex)));
    test_expect_string(test, "sha512 digest", _TEST_TC6_SHA512, hex);
    test_expect_true(test, "sha256 verify accepts the vector", crypto_hmac_sha256_verify_1(key, sizeof(key), _TEST_TC6_MESSAGE, char_length(_TEST_TC6_MESSAGE), _TEST_TC6_SHA256));
    test_expect_true(test, "sha512 verify accepts the vector", crypto_hmac_sha512_verify_1(key, sizeof(key), _TEST_TC6_MESSAGE, char_length(_TEST_TC6_MESSAGE), _TEST_TC6_SHA512));

    test_case_end(test);
}

static void _test_upper_hex(Test *const test) {
    test_case_begin(test, "uppercase hex matches the lowercase vector, upper-cased");

    char hex[CRYPTO_HMAC_SHA512_HEX_SIZE + CHAR_END_CHARACTER] = DEFAULT_INITIALIZATION;

    test_expect_true(test, "sha256 upper computes",
        result_is_success(crypto_hmac_sha256_hex_upper_1(_TEST_TC2_KEY, char_length(_TEST_TC2_KEY), _TEST_TC2_MESSAGE, char_length(_TEST_TC2_MESSAGE), hex)));
    test_expect_string(test, "sha256 upper digest", _TEST_TC2_SHA256_UPPER, hex);
    test_expect_true(test, "sha512 upper computes",
        result_is_success(crypto_hmac_sha512_hex_upper_1(_TEST_TC2_KEY, char_length(_TEST_TC2_KEY), _TEST_TC2_MESSAGE, char_length(_TEST_TC2_MESSAGE), hex)));
    test_expect_true(test, "sha512 upper digest is the vector, case-insensitively", char_compare_iequal_1(hex, (char*) _TEST_TC2_SHA512));
    test_expect_true(test, "verify accepts uppercase hex (case-insensitive)",
        crypto_hmac_sha256_verify_1(_TEST_TC2_KEY, char_length(_TEST_TC2_KEY), _TEST_TC2_MESSAGE, char_length(_TEST_TC2_MESSAGE), _TEST_TC2_SHA256_UPPER));

    test_case_end(test);
}

static void _test_byte_forms(Test *const test) {
    test_case_begin(test, "raw-byte output hex-encodes to the same digest as the hex form");

    U8      sha256_bytes[CRYPTO_HMAC_SHA256_SIZE]                   = DEFAULT_INITIALIZATION;
    U8      sha512_bytes[CRYPTO_HMAC_SHA512_SIZE]                   = DEFAULT_INITIALIZATION;
    char    hex[CRYPTO_HMAC_SHA512_HEX_SIZE + CHAR_END_CHARACTER]   = DEFAULT_INITIALIZATION;

    // The _bytes forms are byte-domain (U8 in, U8 out); these vectors are text
    // literals, hence the casts.
    test_expect_true(test, "sha256 bytes computes",
        result_is_success(crypto_hmac_sha256_bytes_1((U8 const*) _TEST_TC2_KEY, char_length(_TEST_TC2_KEY), (U8 const*) _TEST_TC2_MESSAGE, char_length(_TEST_TC2_MESSAGE), sha256_bytes)));
    encoding_hex_encode_1(sha256_bytes, sizeof(sha256_bytes), hex);
    test_expect_string(test, "sha256 bytes match the vector", _TEST_TC2_SHA256, hex);

    test_expect_true(test, "sha512 bytes computes",
        result_is_success(crypto_hmac_sha512_bytes_1((U8 const*) _TEST_TC2_KEY, char_length(_TEST_TC2_KEY), (U8 const*) _TEST_TC2_MESSAGE, char_length(_TEST_TC2_MESSAGE), sha512_bytes)));
    encoding_hex_encode_1(sha512_bytes, sizeof(sha512_bytes), hex);
    test_expect_string(test, "sha512 bytes match the vector", _TEST_TC2_SHA512, hex);

    test_case_end(test);
}

static void _test_overloads(Test *const test) {
    test_case_begin(test, "char / Str / String overloads agree on hex and verify");

    char const *const   key             = _TEST_KEY;
    char const *const   message         = _TEST_MESSAGE;
    USize const         key_size        = char_length(key);
    USize const         message_size    = char_length(message);

    char hex[CRYPTO_HMAC_SHA512_HEX_SIZE + CHAR_END_CHARACTER] = DEFAULT_INITIALIZATION;

    test_expect_true(test, "hex_1 computes", result_is_success(crypto_hmac_sha256_hex_1(key, key_size, message, message_size, hex)));
    test_expect_string(test, "hex_1 digest", _TEST_MESSAGE_SHA256, hex);

    Str const message_str = str_init_2((char*) message);

    test_expect_true(test, "hex_2 computes", result_is_success(crypto_hmac_sha256_hex_2(key, key_size, &message_str, hex)));
    test_expect_string(test, "hex_2 digest", _TEST_MESSAGE_SHA256, hex);
    test_expect_true(test, "verify_2 accepts", crypto_hmac_sha256_verify_2(key, key_size, &message_str, _TEST_MESSAGE_SHA256));

    String message_string = string_init_1();

    string_add_last_1(&message_string, (char*) message);

    test_expect_true(test, "hex_3 computes", result_is_success(crypto_hmac_sha256_hex_3(key, key_size, &message_string, hex)));
    test_expect_string(test, "hex_3 digest", _TEST_MESSAGE_SHA256, hex);
    test_expect_true(test, "verify_3 accepts", crypto_hmac_sha256_verify_3(key, key_size, &message_string, _TEST_MESSAGE_SHA256));
    test_expect_true(test, "sha512 hex_3 computes", result_is_success(crypto_hmac_sha512_hex_3(key, key_size, &message_string, hex)));
    test_expect_true(test, "sha512 verify_3 accepts its own digest", crypto_hmac_sha512_verify_3(key, key_size, &message_string, hex));

    string_uninit(&message_string);

    test_case_end(test);
}

static void _test_empty_values(Test *const test) {
    test_case_begin(test, "empty keys, messages, Str and String are legal values, never abort");

    char    hex[CRYPTO_HMAC_SHA512_HEX_SIZE + CHAR_END_CHARACTER]   = DEFAULT_INITIALIZATION;
    Str     const   empty_str                                       = str_init_2((char*) "");
    String          empty_string                                    = string_init_1();

    test_expect_true(test, "empty message computes", result_is_success(crypto_hmac_sha256_hex_1(_TEST_KEY, char_length(_TEST_KEY), "", 0, hex)));
    test_expect_string(test, "empty message digest", _TEST_EMPTY_SHA256, hex);

    // Regression pin: an empty Str/String carries null data, which would reach
    // the worker's null check and abort without the empty mapping.
    test_expect_true(test, "empty Str computes", result_is_success(crypto_hmac_sha256_hex_2(_TEST_KEY, char_length(_TEST_KEY), &empty_str, hex)));
    test_expect_string(test, "empty Str digest matches the empty-message vector", _TEST_EMPTY_SHA256, hex);
    test_expect_true(test, "empty String computes", result_is_success(crypto_hmac_sha256_hex_3(_TEST_KEY, char_length(_TEST_KEY), &empty_string, hex)));
    test_expect_string(test, "empty String digest matches the empty-message vector", _TEST_EMPTY_SHA256, hex);
    test_expect_true(test, "verify_2 on an empty Str", crypto_hmac_sha256_verify_2(_TEST_KEY, char_length(_TEST_KEY), &empty_str, _TEST_EMPTY_SHA256));
    test_expect_true(test, "verify_3 on an empty String", crypto_hmac_sha256_verify_3(_TEST_KEY, char_length(_TEST_KEY), &empty_string, _TEST_EMPTY_SHA256));

    // An empty key is a legal (if useless) value: it must compute, not abort.
    test_expect_true(test, "empty key computes", result_is_success(crypto_hmac_sha256_hex_1("", 0, _TEST_MESSAGE, char_length(_TEST_MESSAGE), hex)));
    test_expect_true(test, "empty key digest is not empty", char_length(hex) == CRYPTO_HMAC_SHA256_HEX_SIZE);

    string_uninit(&empty_string);

    test_case_end(test);
}

static void _test_verify_rejections(Test *const test) {
    test_case_begin(test, "verify rejects tampered digests and wrong-length expectations");

    char const *const   key             = _TEST_KEY;
    char const *const   message         = _TEST_MESSAGE;
    USize const         key_size        = char_length(key);
    USize const         message_size    = char_length(message);
    char const *const   tampered        = "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd9";

    test_expect_true(test, "verify accepts the true digest", crypto_hmac_sha256_verify_1(key, key_size, message, message_size, _TEST_MESSAGE_SHA256));
    test_expect_false(test, "verify rejects a one-nibble tamper", crypto_hmac_sha256_verify_1(key, key_size, message, message_size, tampered));
    test_expect_false(test, "verify rejects a short expected hex", crypto_hmac_sha256_verify_1(key, key_size, message, message_size, "deadbeef"));
    test_expect_false(test, "verify rejects an empty expected hex", crypto_hmac_sha256_verify_1(key, key_size, message, message_size, ""));
    test_expect_false(test, "sha512 verify rejects a sha256-length expected hex", crypto_hmac_sha512_verify_1(key, key_size, message, message_size, _TEST_MESSAGE_SHA256));
    test_expect_false(test, "verify rejects the right digest under a different key", crypto_hmac_sha256_verify_1("wrong key", char_length("wrong key"), message, message_size, _TEST_MESSAGE_SHA256));

    test_case_end(test);
}

int main(void) {
    log_init((LogConfig){ .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true });

    Test test = test_init("tests/crypto/hmac/test_all.c");

    test_suite_begin(&test, "crypto_hmac");
    _test_rfc4231_short_keys(&test);
    _test_rfc4231_long_key(&test);
    _test_upper_hex(&test);
    _test_byte_forms(&test);
    _test_overloads(&test);
    _test_empty_values(&test);
    _test_verify_rejections(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}