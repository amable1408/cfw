#include <string.h>

#include <char/char.h>
#include <encoding/base64/base64.h>
#include <log/log.h>
#include <test/test.h>

/* Coverage for encoding/base64: the full RFC 4648 §10 test-vector set in both
 * directions for the standard padded alphabet and the URL-safe unpadded
 * alphabet, a binary round trip crafted to hit both special symbols of each
 * alphabet ('+' '/' vs '-' '_'), and strict-decode rejection of every
 * documented failure mode (bad length, out-of-alphabet char, malformed or
 * forbidden padding, insufficient output_capacity, and nonzero leftover bits
 * in a partial final group) with a proof for each crafted leftover-bits
 * vector and a check that decoded_size is left untouched on failure.
 *
 * Null-pointer cases are deliberately absent: those checks are error_check_*,
 * which aborts the process under ERROR_CHECK_ENABLED, so they cannot be
 * observed from inside the suite. */

typedef struct {
    U8 const *bytes;
    USize size;
    char const *encoded;
} Base64Vector;

static U8 const _test_bytes_foobar[6] = { 'f', 'o', 'o', 'b', 'a', 'r' };

// RFC 4648 §10: "" "f" "fo" "foo" "foob" "fooba" "foobar". The URL-safe
// column is the standard column with trailing '=' stripped: none of these
// bytes are ASCII text that ever lands on the 62/63 alphabet slots, so the
// two alphabets agree on every character that survives.
static Base64Vector const _test_std_vectors[] = {
    { (U8 const*) "",             0, ""             },
    { _test_bytes_foobar,         1, "Zg=="         },
    { _test_bytes_foobar,         2, "Zm8="         },
    { _test_bytes_foobar,         3, "Zm9v"         },
    { _test_bytes_foobar,         4, "Zm9vYg=="     },
    { _test_bytes_foobar,         5, "Zm9vYmE="     },
    { _test_bytes_foobar,         6, "Zm9vYmFy"     },
};

static Base64Vector const _test_url_vectors[] = {
    { (U8 const*) "",             0, ""             },
    { _test_bytes_foobar,         1, "Zg"           },
    { _test_bytes_foobar,         2, "Zm8"          },
    { _test_bytes_foobar,         3, "Zm9v"         },
    { _test_bytes_foobar,         4, "Zm9vYg"       },
    { _test_bytes_foobar,         5, "Zm9vYmE"      },
    { _test_bytes_foobar,         6, "Zm9vYmFy"     },
};

static void _test_std_round_trip(Test *const test) {
    test_case_begin(test, "encoding_base64_encode_1/decode_1: RFC 4648 vector set");

    for (USize i = 0; i < sizeof(_test_std_vectors) / sizeof(_test_std_vectors[0]); i += 1) {
        Base64Vector const *const vector = &_test_std_vectors[i];

        char encoded[ENCODING_BASE64_ENCODE_SIZE(6) + CHAR_END_CHARACTER] = DEFAULT_INITIALIZATION;

        encoding_base64_encode_1(vector->bytes, vector->size, encoded);
        test_expect_string(test, "encode matches the RFC vector", vector->encoded, encoded);

        U8 decoded[ENCODING_BASE64_DECODE_SIZE(8)] = DEFAULT_INITIALIZATION;
        USize decoded_size                         = 0;

        Result const result = encoding_base64_decode_1(vector->encoded, strlen(vector->encoded), decoded, sizeof(decoded), &decoded_size);

        test_expect_true(test, "decode succeeds", result_is_success(result));
        test_expect_u(test, "decoded_size matches original size", vector->size, decoded_size);
        test_expect_true(test, "decoded bytes match original", memcmp(decoded, vector->bytes, vector->size) == 0);
    }

    test_case_end(test);
}

static void _test_url_round_trip(Test *const test) {
    test_case_begin(test, "encoding_base64_url_encode_1/url_decode_1: RFC 4648 vector set (unpadded)");

    for (USize i = 0; i < sizeof(_test_url_vectors) / sizeof(_test_url_vectors[0]); i += 1) {
        Base64Vector const *const vector = &_test_url_vectors[i];

        char encoded[ENCODING_BASE64_URL_ENCODE_SIZE(6) + CHAR_END_CHARACTER] = DEFAULT_INITIALIZATION;

        encoding_base64_url_encode_1(vector->bytes, vector->size, encoded);
        test_expect_string(test, "url encode matches the RFC vector", vector->encoded, encoded);

        U8 decoded[ENCODING_BASE64_DECODE_SIZE(8)] = DEFAULT_INITIALIZATION;
        USize decoded_size                         = 0;

        Result const result = encoding_base64_url_decode_1(vector->encoded, strlen(vector->encoded), decoded, sizeof(decoded), &decoded_size);

        test_expect_true(test, "url decode succeeds", result_is_success(result));
        test_expect_u(test, "url decoded_size matches original size", vector->size, decoded_size);
        test_expect_true(test, "url decoded bytes match original", memcmp(decoded, vector->bytes, vector->size) == 0);
    }

    test_case_end(test);
}

static void _test_binary_round_trip(Test *const test) {
    test_case_begin(test, "binary round trip: divergence on both special symbols ('+'/'/' vs '-'/'_')");

    // Chosen so the four 6-bit groups are 62, 63, 0, 0: decoding "+/AA" by
    // hand proves it — '+'=62, '/'=63, byte1=(62<<2)|(63>>4)=0xFB,
    // byte2=((63&0xF)<<4)|(0>>2)=0xF0, byte3=((0&0x3)<<6)|0=0x00. The
    // standard alphabet must therefore encode these bytes as "+/AA" and the
    // URL-safe alphabet as "-_AA" — both special slots exercised, unlike
    // {0xfb,0xef,0xbe} which (verified by the same hand expansion) lands all
    // four groups on slot 62 and never touches '/' or '_'.
    U8 const bytes[3] = { 0xFB, 0xF0, 0x00 };

    char std_encoded[ENCODING_BASE64_ENCODE_SIZE(3) + CHAR_END_CHARACTER]     = DEFAULT_INITIALIZATION;
    char url_encoded[ENCODING_BASE64_URL_ENCODE_SIZE(3) + CHAR_END_CHARACTER] = DEFAULT_INITIALIZATION;

    encoding_base64_encode_1(bytes, sizeof(bytes), std_encoded);
    encoding_base64_url_encode_1(bytes, sizeof(bytes), url_encoded);

    test_expect_string(test, "standard alphabet uses '+' and '/'", "+/AA", std_encoded);
    test_expect_string(test, "URL-safe alphabet uses '-' and '_'", "-_AA", url_encoded);

    U8 std_decoded[ENCODING_BASE64_DECODE_SIZE(4)] = DEFAULT_INITIALIZATION;
    U8 url_decoded[ENCODING_BASE64_DECODE_SIZE(4)] = DEFAULT_INITIALIZATION;
    USize std_decoded_size                         = 0;
    USize url_decoded_size                         = 0;

    Result const std_result = encoding_base64_decode_1(std_encoded, strlen(std_encoded), std_decoded, sizeof(std_decoded), &std_decoded_size);
    Result const url_result = encoding_base64_url_decode_1(url_encoded, strlen(url_encoded), url_decoded, sizeof(url_decoded), &url_decoded_size);

    test_expect_true(test, "standard decode succeeds", result_is_success(std_result));
    test_expect_true(test, "URL-safe decode succeeds", result_is_success(url_result));
    test_expect_true(test, "standard decode round-trips", memcmp(std_decoded, bytes, sizeof(bytes)) == 0);
    test_expect_true(test, "URL-safe decode round-trips", memcmp(url_decoded, bytes, sizeof(bytes)) == 0);

    test_case_end(test);
}

static void _test_decode_rejections_std(Test *const test) {
    test_case_begin(test, "encoding_base64_decode_1: strict rejections");

    U8 output[8] = DEFAULT_INITIALIZATION;

    // Bad length: not a multiple of 4.
    USize bad_length_size = 999;
    Result const bad_length = encoding_base64_decode_1("Zg=", 3, output, sizeof(output), &bad_length_size);

    test_expect_true(test, "length not a multiple of 4 is ARGUMENT", result_category(bad_length) == RESULT_CATEGORY_ARGUMENT);
    test_expect_u(test, "decoded_size untouched after bad length", 999, bad_length_size);

    // Character outside the alphabet.
    Result const bad_char = encoding_base64_decode_1("Z!g=", 4, output, sizeof(output), &bad_length_size);

    test_expect_true(test, "out-of-alphabet char is ARGUMENT", result_category(bad_char) == RESULT_CATEGORY_ARGUMENT);

    // '=' not confined to the trailing 1-2 positions of the last group.
    Result const mid_pad = encoding_base64_decode_1("Zg=a", 4, output, sizeof(output), &bad_length_size);
    Result const lead_pad = encoding_base64_decode_1("=Zg=", 4, output, sizeof(output), &bad_length_size);

    test_expect_true(test, "'Zg=a' (mid-group '=') is ARGUMENT", result_category(mid_pad) == RESULT_CATEGORY_ARGUMENT);
    test_expect_true(test, "'=Zg=' (leading '=') is ARGUMENT", result_category(lead_pad) == RESULT_CATEGORY_ARGUMENT);

    // output_capacity smaller than the decoded size, checked before writing.
    USize capacity_size = 999;
    U8 tiny_output[2]   = DEFAULT_INITIALIZATION;
    Result const too_small = encoding_base64_decode_1("Zm9v", 4, tiny_output, 2, &capacity_size);

    test_expect_true(test, "output_capacity 2 for a 3-byte decode is ARGUMENT", result_category(too_small) == RESULT_CATEGORY_ARGUMENT);
    test_expect_u(test, "decoded_size untouched after capacity failure", 999, capacity_size);

    // Nonzero leftover bits in a partial final group must be rejected even
    // though length and padding placement are otherwise well-formed.
    // "AB==": A=0, B=1 in the alphabet; the discarded low 4 bits of B's
    // 6-bit value are 0b0001 != 0, so this input is not a canonical
    // encoding of any single byte — reject it. Contrast "AA==", whose B-slot
    // value is 0 (already covered by the "f" -> "Zg==" vector's own final
    // group, which passes): the check must not fire on genuinely zero bits.
    Result const leftover_2 = encoding_base64_decode_1("AB==", 4, output, sizeof(output), &bad_length_size);

    test_expect_true(test, "'AB==' nonzero leftover bits (2-pad group) is ARGUMENT", result_category(leftover_2) == RESULT_CATEGORY_ARGUMENT);

    // "AAB=": A=0, A=0, B=1; the discarded low 2 bits of B's 6-bit value are
    // 0b01 != 0, so this is not a canonical encoding of any 2-byte prefix.
    Result const leftover_3 = encoding_base64_decode_1("AAB=", 4, output, sizeof(output), &bad_length_size);

    test_expect_true(test, "'AAB=' nonzero leftover bits (1-pad group) is ARGUMENT", result_category(leftover_3) == RESULT_CATEGORY_ARGUMENT);

    test_case_end(test);
}

static void _test_decode_rejections_url(Test *const test) {
    test_case_begin(test, "encoding_base64_url_decode_1: strict rejections");

    U8 output[8] = DEFAULT_INITIALIZATION;

    // Bad length: input_size % 4 == 1.
    USize bad_length_size = 999;
    Result const bad_length = encoding_base64_url_decode_1("Zm9vY", 5, output, sizeof(output), &bad_length_size);

    test_expect_true(test, "length % 4 == 1 is ARGUMENT", result_category(bad_length) == RESULT_CATEGORY_ARGUMENT);
    test_expect_u(test, "decoded_size untouched after bad length", 999, bad_length_size);

    // Character outside the alphabet.
    Result const bad_char = encoding_base64_url_decode_1("Zm9!", 4, output, sizeof(output), &bad_length_size);

    test_expect_true(test, "out-of-alphabet char is ARGUMENT", result_category(bad_char) == RESULT_CATEGORY_ARGUMENT);

    // '=' is never legal in the URL-safe alphabet.
    Result const has_pad = encoding_base64_url_decode_1("Zg8=", 4, output, sizeof(output), &bad_length_size);

    test_expect_true(test, "'=' present is ARGUMENT", result_category(has_pad) == RESULT_CATEGORY_ARGUMENT);

    // output_capacity smaller than the decoded size, checked before writing.
    USize capacity_size = 999;
    U8 tiny_output[2]   = DEFAULT_INITIALIZATION;
    Result const too_small = encoding_base64_url_decode_1("Zm9v", 4, tiny_output, 2, &capacity_size);

    test_expect_true(test, "output_capacity 2 for a 3-byte decode is ARGUMENT", result_category(too_small) == RESULT_CATEGORY_ARGUMENT);
    test_expect_u(test, "decoded_size untouched after capacity failure", 999, capacity_size);

    // Nonzero leftover bits, same proof as the standard-alphabet case but
    // unpadded: "AB" (2 chars, A=0, B=1) discards B's low 4 bits (0b0001).
    Result const leftover_2 = encoding_base64_url_decode_1("AB", 2, output, sizeof(output), &bad_length_size);

    test_expect_true(test, "'AB' nonzero leftover bits (2-char group) is ARGUMENT", result_category(leftover_2) == RESULT_CATEGORY_ARGUMENT);

    // "AAB" (3 chars, A=0, A=0, B=1) discards B's low 2 bits (0b01).
    Result const leftover_3 = encoding_base64_url_decode_1("AAB", 3, output, sizeof(output), &bad_length_size);

    test_expect_true(test, "'AAB' nonzero leftover bits (3-char group) is ARGUMENT", result_category(leftover_3) == RESULT_CATEGORY_ARGUMENT);

    test_case_end(test);
}

int main(void) {
    log_init((LogConfig){ .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true });

    Test test = test_init("tests/encoding/base64/test_all.c");

    test_suite_begin(&test, "encoding_base64");
    _test_std_round_trip(&test);
    _test_url_round_trip(&test);
    _test_binary_round_trip(&test);
    _test_decode_rejections_std(&test);
    _test_decode_rejections_url(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}