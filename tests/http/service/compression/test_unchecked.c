#include <http/service/compression/compression.h>
#include <log/log.h>
#include <test/test.h>

/*
 * Behavioral tests for include/http/service/compression/compression.c built WITHOUT
 * ERROR_CHECK_ENABLED.
 *
 * error_check_null in this module guards only null-pointer contracts on the public entry points
 * - deliberately absent here (undefined behavior with the checks compiled out, and this file
 * never exercises them). Everything below is a VALUE-dependent refusal, coded as an ordinary
 * runtime branch and never routed through error_check, so this build proves each one refuses
 * identically whether ERROR_CHECK_ENABLED is defined or not (a data-dependent decision is never
 * an abort primitive):
 *
 *   - an out-of-range quality or level, and a max_size below min_size, at the constructors;
 *   - an empty MIME prefix at mime_type_add, which the pre-round code routed through
 *     error_check_non_value_uint and therefore ABORTED on in a checked build;
 *   - every Accept-Encoding and Content-Type refusal, because a header is untrusted network
 *     input that must never be able to abort a server whatever the build defines;
 *   - a zero size, an incompressible payload, and a payload past max_size at compress.
 */

int main(void) {
    LogConfig const log_config = {
        .level             = LOG_LEVEL_ERROR,
        .stream            = stdout,
        .timestamp_enabled = true,
        .autoflush         = true
    };

    log_init(log_config);

    Test test = test_init("tests/http/service/compression/test_unchecked.c");

    test_suite_begin(&test, "http_service_compression (unchecked)");

    test_case_begin(&test, "the constructors still refuse an out-of-range configuration");

    HTTP_Service_Compression refused = DEFAULT_INITIALIZATION;

    test_expect_false(&test, "init_2 refuses a quality past 11", http_service_compression_init_2(&refused, 1024, 0, 12, 6));
    test_expect_false(&test, "init_2 refuses a negative quality", http_service_compression_init_2(&refused, 1024, 0, -1, 6));
    test_expect_false(&test, "init_2 refuses a level past 9", http_service_compression_init_2(&refused, 1024, 0, 5, 10));
    test_expect_false(&test, "init_2 refuses a negative level", http_service_compression_init_2(&refused, 1024, 0, 5, -1));
    test_expect_false(&test, "init_2 refuses max_size below min_size", http_service_compression_init_2(&refused, 1024, 512, 5, 6));
    test_expect_u(&test, "and leaves the instance zeroed", 0, refused.min_size);

    test_case_end(&test);

    test_case_begin(&test, "an empty MIME prefix is refused, not aborted");

    HTTP_Service_Compression compression = DEFAULT_INITIALIZATION;

    test_expect_true(&test, "init_1 succeeds", http_service_compression_init_1(&compression));

    /* The pre-round code reached error_check_non_value_uint with a zero size here, which is an
     * abort in a checked build and a silent empty entry - matching every Content-Type - in this
     * one. Both are gone: the refusal is a plain branch that answers the same way in both. */
    test_expect_false(&test, "mime_type_add refuses an empty prefix", http_service_compression_mime_type_add(&compression, ""));
    test_expect_u(&test, "so image/png is still not compressible", HTTP_SERVICE_COMPRESSION_TYPE_NONE,
        http_service_compression_negotiate_1(&compression, "br", "image/png", 4096));

    test_case_end(&test);

    test_case_begin(&test, "hostile header values answer NONE with the checks compiled out");

    test_expect_u(&test, "an empty Accept-Encoding is NONE", HTTP_SERVICE_COMPRESSION_TYPE_NONE,
        http_service_compression_negotiate_1(&compression, "", "text/html", 4096));
    test_expect_u(&test, "a null Accept-Encoding is NONE", HTTP_SERVICE_COMPRESSION_TYPE_NONE,
        http_service_compression_negotiate_1(&compression, nullptr, "text/html", 4096));
    test_expect_u(&test, "a null Content-Type is NONE", HTTP_SERVICE_COMPRESSION_TYPE_NONE,
        http_service_compression_negotiate_1(&compression, "br", nullptr, 4096));
    test_expect_u(&test, "an empty Content-Type is NONE", HTTP_SERVICE_COMPRESSION_TYPE_NONE,
        http_service_compression_negotiate_1(&compression, "br", "", 4096));
    test_expect_u(&test, "punctuation soup is NONE", HTTP_SERVICE_COMPRESSION_TYPE_NONE,
        http_service_compression_negotiate_1(&compression, ";;;,,,;q=", "text/html", 4096));
    test_expect_u(&test, "an explicit q=0 is honoured", HTTP_SERVICE_COMPRESSION_TYPE_NONE,
        http_service_compression_negotiate_1(&compression, "br;q=0, gzip;q=0", "text/html", 4096));
    test_expect_u(&test, "a zero-size span is NONE", HTTP_SERVICE_COMPRESSION_TYPE_NONE,
        http_service_compression_negotiate_2(&compression, "br", 0, "text/html", 9, 4096));

    test_case_end(&test);

    test_case_begin(&test, "compress still refuses a payload it cannot shrink");

    String out = DEFAULT_INITIALIZATION;

    test_expect_false(&test, "a zero size refuses",
        http_service_compression_compress(&compression, HTTP_SERVICE_COMPRESSION_TYPE_GZIP, (Byte const*) "x", 0, &out));
    test_expect_null(&test, "leaving the EMPTY String", string_get_data(&out));
    string_uninit(&out);

    test_expect_false(&test, "type NONE refuses",
        http_service_compression_compress(&compression, HTTP_SERVICE_COMPRESSION_TYPE_NONE, (Byte const*) "hello", 5, &out));
    string_uninit(&out);

    /* Five bytes cannot shrink under either codec's framing, so this is the size-based half of
     * the "output must be smaller" rule with no allocator or codec failure involved. */
    test_expect_false(&test, "a payload smaller than its own framing refuses",
        http_service_compression_compress(&compression, HTTP_SERVICE_COMPRESSION_TYPE_BROTLI, (Byte const*) "hello", 5, &out));
    string_uninit(&out);

    http_service_compression_uninit(&compression);

    /* The max_size cap is a size comparison on caller data, so it belongs in this
     * build too - it must refuse identically with every error_check_* compiled away. */
    char payload[4096] = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < sizeof(payload); i += 1) {
        payload[i] = (char) ('a' + (i % 26));
    }

    HTTP_Service_Compression capped = DEFAULT_INITIALIZATION;

    test_expect_true(&test, "a capped service initializes", http_service_compression_init_2(&capped, 16, 2048, 5, 6));
    test_expect_false(&test, "compress refuses a payload past max_size",
        http_service_compression_compress(&capped, HTTP_SERVICE_COMPRESSION_TYPE_GZIP, (Byte const*) payload, sizeof(payload), &out));
    test_expect_null(&test, "leaving the EMPTY String", string_get_data(&out));
    string_uninit(&out);

    test_expect_true(&test, "while a payload at the cap still compresses",
        http_service_compression_compress(&capped, HTTP_SERVICE_COMPRESSION_TYPE_GZIP, (Byte const*) payload, 2048, &out));
    string_uninit(&out);

    http_service_compression_uninit(&capped);

    test_case_end(&test);

    test_suite_end(&test);

    return test_uninit(&test);
}