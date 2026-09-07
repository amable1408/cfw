/*
 * test_all.c - the http/service/compression suite.
 *
 * The module went a long time with no suite at all, which is exactly why its "gzip" path could
 * ship a zlib stream (RFC 1950, 78 9c) under a token that promises RFC 1952 (1f 8b) for however
 * long it did. Every case below pins one thing the module promises, and the two round trips are
 * the ones that could not have passed while that path was wrong:
 *
 *   - a gzip body carrying the 1f 8b magic and decoding through inflateInit2(31);
 *   - a Brotli body decoding through BrotliDecoderDecompress;
 *   - the min_size and max_size window, at the exact boundary values;
 *   - the MIME allow-list: case-insensitive, charset parameter tolerated, "+json"/"+xml"
 *     structured suffixes accepted without an explicit entry;
 *   - the whole Accept-Encoding matrix, q-values and wildcards included;
 *   - a zero size and an incompressible 4 KiB of noise, both refused to identity;
 *   - max_size honoured by the RAW compress tier, not only by negotiate;
 *   - an arena-backed service whose arena does NOT grow across repeated compress calls: the
 *     codec buffers are heap-owned, so a long-lived server cannot retain its compressed
 *     output in a linear arena that reclaims nothing;
 *   - the in-place bool constructors and their configuration refusals;
 *   - one live server on port 0 answering br, gzip and identity, with Content-Encoding
 *     and Vary on the wire and a decoded body byte-identical to what the route handed in,
 *     including an Accept-Encoding that is present but names no coding;
 *   - and the same live server refusing to code a 206 Partial Content reply (RFC 9110
 *     section 14.4), which still carries Vary.
 *
 * The live case is shaped after tests/http/server/test_all.c: http_server_run on port 0, the
 * ephemeral port read back with http_server_get_port, and a raw `net` socket for the client so
 * the bytes this suite decodes are the literal bytes the server wrote.
 */
#include <brotli/decode.h>
#include <zlib.h>

#include <allocator/allocator.h>
#include <arena/arena.h>
#include <http/server/http_server.h>
#include <http/service/compression/compression.h>
#include <log/log.h>
#include <net/net.h>
#include <test/test.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/
#define _ARENA_PROBE_BLOCK      64
#define _ARENA_PROBE_ROUNDS     32
#define _ARENA_PROBE_SIZE       (1024 * 1024)
#define _CLIENT_IO_TIMEOUT_MS   4000
#define _DECODED_MAX            65536
#define _GZIP_WINDOW_BITS       31
#define _HTML_TYPE              "text/html; charset=utf-8"
#define _LIVE_PAYLOAD_SIZE      4096
#define _NOISE_SIZE             4096
#define _REPLY_MAX              65536

/*==============================================================================
 * MARK: - Helpers
 *============================================================================*/

/* A compressible body of a requested size: repeated English-shaped markup, so every codec has
 * real redundancy to find and the ratio assertions below are not measuring luck. */
static void _payload_build(char *const buffer, USize const size) {
    char const *const seed = "<p class=\"row\">The quick brown fox jumps over the lazy dog.</p>";

    USize const seed_size = char_length(seed);

    for (USize i = 0; i < size; i += 1) {
        buffer[i] = seed[i % seed_size];
    }
}

/* Deterministic pseudo-random bytes: an xorshift, not rand(), so the incompressible case is the
 * same 4 KiB on every machine and every run. Compressed output larger than this input is what
 * the identity refusal has to catch. */
static void _noise_build(Byte *const buffer, USize const size) {
    U32 state = 0x2545F491u;

    for (USize i = 0; i < size; i += 1) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;

        buffer[i] = (Byte) (state & 0xFFu);
    }
}

static bool _gzip_inflate(Byte const *const data, USize const data_size, Byte *const out, USize const capacity, USize *const out_size) {
    z_stream stream = DEFAULT_INITIALIZATION;

    if (inflateInit2(&stream, _GZIP_WINDOW_BITS) != Z_OK) {
        return false;
    }

    stream.next_in      = (Bytef*) data;
    stream.avail_in     = (uInt) data_size;
    stream.next_out     = (Bytef*) out;
    stream.avail_out    = (uInt) capacity;

    int const result = inflate(&stream, Z_FINISH);

    *out_size = (USize) stream.total_out;

    inflateEnd(&stream);

    return result == Z_STREAM_END;
}

static bool _brotli_inflate(Byte const *const data, USize const data_size, Byte *const out, USize const capacity, USize *const out_size) {
    size_t decoded = capacity;

    BrotliDecoderResult const result = BrotliDecoderDecompress(data_size, (uint8_t const*) data, &decoded, (uint8_t*) out);

    *out_size = (USize) decoded;

    return result == BROTLI_DECODER_RESULT_SUCCESS;
}

/* Case-insensitive byte search, so the header assertions do not depend on how libwebsockets
 * happens to capitalize a header name it did not write itself. */
static USize _find_ignore_case(char const *const haystack, USize const haystack_size, char const *const needle) {
    USize const needle_size = char_length(needle);

    if (needle_size == 0 || needle_size > haystack_size) {
        return USIZE_MAX;
    }

    for (USize i = 0; i + needle_size <= haystack_size; i += 1) {
        if (char_compare_iequal_2(haystack + i, needle_size, needle, needle_size)) {
            return i;
        }
    }

    return USIZE_MAX;
}

/* Split a raw reply into its header block and its body. The body is BINARY - a gzip stream is
 * full of NUL bytes - so nothing here may treat the reply as a C string past the blank line. */
static bool _reply_split(Byte const *const reply, USize const reply_size, USize *const out_header_size, USize *const out_body_start) {
    for (USize i = 0; i + 4 <= reply_size; i += 1) {
        if (reply[i] == '\r' && reply[i + 1] == '\n' && reply[i + 2] == '\r' && reply[i + 3] == '\n') {
            *out_header_size = i;
            *out_body_start  = i + 4;

            return true;
        }
    }

    return false;
}

/* One request/response round trip over a raw socket. Returns the byte count, because the body
 * cannot be measured with char_length. */
static bool _round_trip(U16 const port, char const *const path, char const *const accept_encoding, Byte *const reply, USize const capacity, USize *const out_size) {
    Net_Socket_Address address = DEFAULT_INITIALIZATION;
    Net_Socket         socket  = NET_SOCKET_INVALID;

    if (result_is_error(net_socket_address_init_2(NET_FAMILY_IPV4, port, "127.0.0.1", &address))
        || result_is_error(net_socket_init(&socket, NET_FAMILY_IPV4, NET_TYPE_TCP))) {
        return false;
    }

    if (result_is_error(net_socket_connect(socket, &address))) {
        net_socket_close(socket);

        return false;
    }

    net_socket_set_timeout(socket, _CLIENT_IO_TIMEOUT_MS);

    char request[512] = DEFAULT_INITIALIZATION;

    snprintf(request, sizeof(request), "GET %s HTTP/1.1\r\nHost: t\r\n%s%s%sConnection: close\r\n\r\n", path,
        accept_encoding[0] == '\0' ? "" : "Accept-Encoding: ", accept_encoding, accept_encoding[0] == '\0' ? "" : "\r\n");

    USize sent = 0;

    if (result_is_error(net_socket_send_1(socket, request, char_length(request), &sent))) {
        net_socket_close(socket);

        return false;
    }

    USize total = 0;

    while (total < capacity) {
        USize received = 0;

        if (result_is_error(net_socket_recv_1(socket, reply + total, capacity - total, &received)) || received == 0) {
            break;
        }

        total += received;
    }

    *out_size = total;

    net_socket_close(socket);

    return total > 0;
}

/*==============================================================================
 * MARK: - Offline cases
 *============================================================================*/

static void _test_compression_gzip_round_trip(Test *const test) {
    test_case_begin(test, "gzip produces a real RFC 1952 stream");

    HTTP_Service_Compression compression = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_1 succeeds", http_service_compression_init_1(&compression));

    char payload[_LIVE_PAYLOAD_SIZE] = DEFAULT_INITIALIZATION;

    _payload_build(payload, sizeof(payload));

    String compressed = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "compress succeeds",
            http_service_compression_compress(&compression, HTTP_SERVICE_COMPRESSION_TYPE_GZIP, (Byte const*) payload, sizeof(payload), &compressed))) {
        Byte const *const data = (Byte const*) string_get_data(&compressed);

        test_expect_true(test, "the body is smaller than the input", string_get_size(&compressed) < sizeof(payload));

        /* The whole point of the gzip path: zlib's compress() would put 78 9c here, and a
         * browser handed that under Content-Encoding: gzip fails the body outright. */
        test_expect_u(test, "the first magic byte is 0x1f", 0x1f, data[0]);
        test_expect_u(test, "the second magic byte is 0x8b", 0x8b, data[1]);
        test_expect_u(test, "the compression method is DEFLATE", 0x08, data[2]);

        Byte    decoded[_DECODED_MAX]   = DEFAULT_INITIALIZATION;
        USize   decoded_size            = 0;

        test_expect_true(test, "inflateInit2(31) decodes it",
            _gzip_inflate(data, string_get_size(&compressed), decoded, sizeof(decoded), &decoded_size));
        test_expect_u(test, "to the original byte count", sizeof(payload), decoded_size);
        test_expect_true(test, "and the original bytes",
            char_compare_equal_2((char const*) decoded, sizeof(payload), payload, sizeof(payload)));
    }

    string_uninit(&compressed);
    http_service_compression_uninit(&compression);

    test_case_end(test);
}

static void _test_compression_brotli_round_trip(Test *const test) {
    test_case_begin(test, "brotli round trips through BrotliDecoderDecompress");

    HTTP_Service_Compression compression = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_1 succeeds", http_service_compression_init_1(&compression));

    char payload[_LIVE_PAYLOAD_SIZE] = DEFAULT_INITIALIZATION;

    _payload_build(payload, sizeof(payload));

    String compressed = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "compress succeeds",
            http_service_compression_compress(&compression, HTTP_SERVICE_COMPRESSION_TYPE_BROTLI, (Byte const*) payload, sizeof(payload), &compressed))) {
        test_expect_true(test, "the body is smaller than the input", string_get_size(&compressed) < sizeof(payload));

        Byte    decoded[_DECODED_MAX]   = DEFAULT_INITIALIZATION;
        USize   decoded_size            = 0;

        test_expect_true(test, "BrotliDecoderDecompress decodes it",
            _brotli_inflate((Byte const*) string_get_data(&compressed), string_get_size(&compressed), decoded, sizeof(decoded), &decoded_size));
        test_expect_u(test, "to the original byte count", sizeof(payload), decoded_size);
        test_expect_true(test, "and the original bytes",
            char_compare_equal_2((char const*) decoded, sizeof(payload), payload, sizeof(payload)));
    }

    string_uninit(&compressed);
    http_service_compression_uninit(&compression);

    test_case_end(test);
}

static void _test_compression_size_window(Test *const test) {
    test_case_begin(test, "the size window is honoured at both boundaries");

    HTTP_Service_Compression compression = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_1 succeeds", http_service_compression_init_1(&compression));

    test_expect_u(test, "one byte below min_size is NONE", HTTP_SERVICE_COMPRESSION_TYPE_NONE,
        http_service_compression_negotiate_1(&compression, "br", "text/html", HTTP_SERVICE_COMPRESSION_DEFAULT_MIN_SIZE - 1));
    test_expect_u(test, "exactly min_size compresses", HTTP_SERVICE_COMPRESSION_TYPE_BROTLI,
        http_service_compression_negotiate_1(&compression, "br", "text/html", HTTP_SERVICE_COMPRESSION_DEFAULT_MIN_SIZE));
    test_expect_u(test, "exactly max_size compresses", HTTP_SERVICE_COMPRESSION_TYPE_BROTLI,
        http_service_compression_negotiate_1(&compression, "br", "text/html", HTTP_SERVICE_COMPRESSION_DEFAULT_MAX_SIZE));

    /* Past the cap the body goes out uncompressed rather than allocating a
     * second copy of an arbitrarily large payload plus the codec's bound. */
    test_expect_u(test, "one byte past max_size is NONE", HTTP_SERVICE_COMPRESSION_TYPE_NONE,
        http_service_compression_negotiate_1(&compression, "br", "text/html", HTTP_SERVICE_COMPRESSION_DEFAULT_MAX_SIZE + 1));

    http_service_compression_uninit(&compression);

    HTTP_Service_Compression unbounded = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 accepts max_size 0", http_service_compression_init_2(&unbounded, 16, 0,
        HTTP_SERVICE_COMPRESSION_DEFAULT_BROTLI_QUALITY, HTTP_SERVICE_COMPRESSION_DEFAULT_GZIP_LEVEL));
    test_expect_u(test, "and then imposes no upper bound", HTTP_SERVICE_COMPRESSION_TYPE_BROTLI,
        http_service_compression_negotiate_1(&unbounded, "br", "text/html", USIZE_MAX));

    http_service_compression_uninit(&unbounded);

    test_case_end(test);
}

static void _test_compression_mime_rules(Test *const test) {
    test_case_begin(test, "the MIME allow-list matches the way real headers are written");

    HTTP_Service_Compression compression = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_1 succeeds", http_service_compression_init_1(&compression));

    USize const size = HTTP_SERVICE_COMPRESSION_DEFAULT_MIN_SIZE;

    test_expect_u(test, "text/html is compressible", HTTP_SERVICE_COMPRESSION_TYPE_BROTLI,
        http_service_compression_negotiate_1(&compression, "br", "text/html", size));

    /* Both of these once went out uncompressed - the first because
     * the compare was case-sensitive, the second because the charset parameter made the value
     * longer than the prefix it was compared against as a whole. */
    test_expect_u(test, "Text/HTML matches case-insensitively", HTTP_SERVICE_COMPRESSION_TYPE_BROTLI,
        http_service_compression_negotiate_1(&compression, "br", "Text/HTML", size));
    test_expect_u(test, "a charset parameter is tolerated", HTTP_SERVICE_COMPRESSION_TYPE_BROTLI,
        http_service_compression_negotiate_1(&compression, "br", _HTML_TYPE, size));
    test_expect_u(test, "and so is whitespace before it", HTTP_SERVICE_COMPRESSION_TYPE_BROTLI,
        http_service_compression_negotiate_1(&compression, "br", "text/html ; charset=utf-8", size));

    test_expect_u(test, "application/xml is a default", HTTP_SERVICE_COMPRESSION_TYPE_BROTLI,
        http_service_compression_negotiate_1(&compression, "br", "application/xml", size));
    test_expect_u(test, "application/wasm is a default", HTTP_SERVICE_COMPRESSION_TYPE_BROTLI,
        http_service_compression_negotiate_1(&compression, "br", "application/wasm", size));
    test_expect_u(test, "application/manifest+json is a default", HTTP_SERVICE_COMPRESSION_TYPE_BROTLI,
        http_service_compression_negotiate_1(&compression, "br", "application/manifest+json", size));

    /* The structured-suffix rule (RFC 6839): no vendor tree can be enumerated, so the suffix
     * decides instead of the list. */
    test_expect_u(test, "a vendor +json type is compressible", HTTP_SERVICE_COMPRESSION_TYPE_BROTLI,
        http_service_compression_negotiate_1(&compression, "br", "application/vnd.api+json", size));
    test_expect_u(test, "a vendor +XML type matches case-insensitively", HTTP_SERVICE_COMPRESSION_TYPE_BROTLI,
        http_service_compression_negotiate_1(&compression, "br", "application/atom+XML; charset=utf-8", size));

    test_expect_u(test, "image/png is not compressible", HTTP_SERVICE_COMPRESSION_TYPE_NONE,
        http_service_compression_negotiate_1(&compression, "br", "image/png", size));
    test_expect_u(test, "video/mp4 is not compressible", HTTP_SERVICE_COMPRESSION_TYPE_NONE,
        http_service_compression_negotiate_1(&compression, "br", "video/mp4", size));
    test_expect_u(test, "an empty Content-Type is NONE", HTTP_SERVICE_COMPRESSION_TYPE_NONE,
        http_service_compression_negotiate_1(&compression, "br", "", size));
    test_expect_u(test, "a bare parameter is NONE", HTTP_SERVICE_COMPRESSION_TYPE_NONE,
        http_service_compression_negotiate_1(&compression, "br", "; charset=utf-8", size));

    test_expect_true(test, "a custom prefix registers", http_service_compression_mime_type_add(&compression, "application/x-ndjson"));
    test_expect_u(test, "and is then compressible", HTTP_SERVICE_COMPRESSION_TYPE_BROTLI,
        http_service_compression_negotiate_1(&compression, "br", "application/x-ndjson", size));

    /* Configuration data is REFUSED, never aborted - an empty prefix would
     * match every type in existence while looking like a deliberate entry. */
    test_expect_false(test, "an empty MIME type is refused", http_service_compression_mime_type_add(&compression, ""));
    test_expect_u(test, "and image/png is still not compressible", HTTP_SERVICE_COMPRESSION_TYPE_NONE,
        http_service_compression_negotiate_1(&compression, "br", "image/png", size));

    http_service_compression_uninit(&compression);

    test_case_end(test);
}

static void _test_compression_accept_encoding_matrix(Test *const test) {
    test_case_begin(test, "Accept-Encoding is tokenized, not substring-matched");

    HTTP_Service_Compression compression = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_1 succeeds", http_service_compression_init_1(&compression));

    USize const size = HTTP_SERVICE_COMPRESSION_DEFAULT_MIN_SIZE;

    test_expect_u(test, "\"gzip\" picks gzip", HTTP_SERVICE_COMPRESSION_TYPE_GZIP,
        http_service_compression_negotiate_1(&compression, "gzip", _HTML_TYPE, size));
    test_expect_u(test, "\"br\" picks brotli", HTTP_SERVICE_COMPRESSION_TYPE_BROTLI,
        http_service_compression_negotiate_1(&compression, "br", _HTML_TYPE, size));
    test_expect_u(test, "\"gzip, deflate, br\" prefers brotli on equal weights", HTTP_SERVICE_COMPRESSION_TYPE_BROTLI,
        http_service_compression_negotiate_1(&compression, "gzip, deflate, br", _HTML_TYPE, size));
    test_expect_u(test, "\"GZIP\" matches case-insensitively", HTTP_SERVICE_COMPRESSION_TYPE_GZIP,
        http_service_compression_negotiate_1(&compression, "GZIP", _HTML_TYPE, size));
    test_expect_u(test, "\"x-gzip\" is the same coding", HTTP_SERVICE_COMPRESSION_TYPE_GZIP,
        http_service_compression_negotiate_1(&compression, "x-gzip", _HTML_TYPE, size));

    /* The substring matcher compressed all four of these anyway, handing a body the client had
     * explicitly said it could not decode. */
    test_expect_u(test, "\"gzip;q=0\" excludes gzip", HTTP_SERVICE_COMPRESSION_TYPE_NONE,
        http_service_compression_negotiate_1(&compression, "gzip;q=0", _HTML_TYPE, size));
    test_expect_u(test, "\"br;q=0, gzip\" falls back to gzip", HTTP_SERVICE_COMPRESSION_TYPE_GZIP,
        http_service_compression_negotiate_1(&compression, "br;q=0, gzip", _HTML_TYPE, size));
    test_expect_u(test, "\"gzip;q=0, br;q=0\" is NONE", HTTP_SERVICE_COMPRESSION_TYPE_NONE,
        http_service_compression_negotiate_1(&compression, "gzip;q=0, br;q=0", _HTML_TYPE, size));
    test_expect_u(test, "\"br;q=0.000\" is NONE", HTTP_SERVICE_COMPRESSION_TYPE_NONE,
        http_service_compression_negotiate_1(&compression, "br;q=0.000", _HTML_TYPE, size));

    /* Highest weight wins, so a client that prefers gzip GETS gzip. */
    test_expect_u(test, "\"br;q=0.5, gzip;q=1\" picks gzip", HTTP_SERVICE_COMPRESSION_TYPE_GZIP,
        http_service_compression_negotiate_1(&compression, "br;q=0.5, gzip;q=1", _HTML_TYPE, size));
    test_expect_u(test, "\"br;q=1.0, gzip;q=0.5\" picks brotli", HTTP_SERVICE_COMPRESSION_TYPE_BROTLI,
        http_service_compression_negotiate_1(&compression, "br;q=1.0, gzip;q=0.5", _HTML_TYPE, size));
    test_expect_u(test, "whitespace around the weight is OWS", HTTP_SERVICE_COMPRESSION_TYPE_GZIP,
        http_service_compression_negotiate_1(&compression, "  br ; q=0.2 ,  gzip ; q=0.9  ", _HTML_TYPE, size));

    test_expect_u(test, "\"*\" alone yields brotli", HTTP_SERVICE_COMPRESSION_TYPE_BROTLI,
        http_service_compression_negotiate_1(&compression, "*", _HTML_TYPE, size));
    test_expect_u(test, "\"*;q=0\" yields NONE", HTTP_SERVICE_COMPRESSION_TYPE_NONE,
        http_service_compression_negotiate_1(&compression, "*;q=0", _HTML_TYPE, size));
    test_expect_u(test, "an explicit q=0 is not resurrected by \"*\"", HTTP_SERVICE_COMPRESSION_TYPE_GZIP,
        http_service_compression_negotiate_1(&compression, "br;q=0, *", _HTML_TYPE, size));

    test_expect_u(test, "\"identity\" alone is NONE", HTTP_SERVICE_COMPRESSION_TYPE_NONE,
        http_service_compression_negotiate_1(&compression, "identity", _HTML_TYPE, size));
    test_expect_u(test, "an empty header is NONE", HTTP_SERVICE_COMPRESSION_TYPE_NONE,
        http_service_compression_negotiate_1(&compression, "", _HTML_TYPE, size));
    test_expect_u(test, "a null header is NONE", HTTP_SERVICE_COMPRESSION_TYPE_NONE,
        http_service_compression_negotiate_1(&compression, nullptr, _HTML_TYPE, size));

    /* Hostile and malformed values answer NONE instead of aborting: this is request data. */
    test_expect_u(test, "garbage is NONE", HTTP_SERVICE_COMPRESSION_TYPE_NONE,
        http_service_compression_negotiate_1(&compression, ";;;,,,;q=", _HTML_TYPE, size));
    test_expect_u(test, "an unparsable weight excludes the coding", HTTP_SERVICE_COMPRESSION_TYPE_NONE,
        http_service_compression_negotiate_1(&compression, "br;q=high", _HTML_TYPE, size));
    test_expect_u(test, "a weight above 1 excludes the coding", HTTP_SERVICE_COMPRESSION_TYPE_NONE,
        http_service_compression_negotiate_1(&compression, "br;q=1.001", _HTML_TYPE, size));
    test_expect_u(test, "\"brotli\" is not \"br\"", HTTP_SERVICE_COMPRESSION_TYPE_NONE,
        http_service_compression_negotiate_1(&compression, "brotli", _HTML_TYPE, size));
    test_expect_u(test, "\"gzipped\" is not \"gzip\"", HTTP_SERVICE_COMPRESSION_TYPE_NONE,
        http_service_compression_negotiate_1(&compression, "gzipped", _HTML_TYPE, size));

    http_service_compression_uninit(&compression);

    test_case_end(test);
}

static void _test_compression_negotiate_tiers(Test *const test) {
    test_case_begin(test, "the sized and Str negotiate tiers agree with the char one");

    HTTP_Service_Compression compression = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_1 succeeds", http_service_compression_init_1(&compression));

    USize const size = HTTP_SERVICE_COMPRESSION_DEFAULT_MIN_SIZE;

    /* The sized tier reads a span, so it must ignore whatever follows it - the header_copy
     * buffer this exists for is a fixed 256 bytes of mostly nothing. */
    test_expect_u(test, "negotiate_2 reads only the span it was given", HTTP_SERVICE_COMPRESSION_TYPE_BROTLI,
        http_service_compression_negotiate_2(&compression, "br,gzip", 2, _HTML_TYPE, CHAR_STATIC_SIZE(_HTML_TYPE), size));
    test_expect_u(test, "a zero-size header is NONE", HTTP_SERVICE_COMPRESSION_TYPE_NONE,
        http_service_compression_negotiate_2(&compression, "br", 0, _HTML_TYPE, CHAR_STATIC_SIZE(_HTML_TYPE), size));
    test_expect_u(test, "a zero-size Content-Type is NONE", HTTP_SERVICE_COMPRESSION_TYPE_NONE,
        http_service_compression_negotiate_2(&compression, "br", 2, _HTML_TYPE, 0, size));

    Str accept_encoding = str_init_static("gzip", CHAR_STATIC_SIZE("gzip"));
    Str content_type    = str_init_static(_HTML_TYPE, CHAR_STATIC_SIZE(_HTML_TYPE));
    Str empty           = str_init_1();

    test_expect_u(test, "negotiate_3 reads a Str pair", HTTP_SERVICE_COMPRESSION_TYPE_GZIP,
        http_service_compression_negotiate_3(&compression, &accept_encoding, &content_type, size));
    test_expect_u(test, "and answers NONE for the EMPTY Str", HTTP_SERVICE_COMPRESSION_TYPE_NONE,
        http_service_compression_negotiate_3(&compression, &empty, &content_type, size));

    str_uninit(&accept_encoding);
    str_uninit(&content_type);
    str_uninit(&empty);

    http_service_compression_uninit(&compression);

    test_case_end(test);
}

static void _test_compression_refusals(Test *const test) {
    test_case_begin(test, "compress refuses what it cannot usefully shrink");

    HTTP_Service_Compression compression = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_1 succeeds", http_service_compression_init_1(&compression));

    String out = DEFAULT_INITIALIZATION;

    /* A zero size is the EMPTY String and a false return, on both codecs. */
    test_expect_false(test, "a zero size refuses under gzip",
        http_service_compression_compress(&compression, HTTP_SERVICE_COMPRESSION_TYPE_GZIP, (Byte const*) "x", 0, &out));
    test_expect_null(test, "leaving the EMPTY String", string_get_data(&out));
    string_uninit(&out);

    test_expect_false(test, "a zero size refuses under brotli",
        http_service_compression_compress(&compression, HTTP_SERVICE_COMPRESSION_TYPE_BROTLI, (Byte const*) "x", 0, &out));
    string_uninit(&out);

    test_expect_false(test, "a null payload refuses",
        http_service_compression_compress(&compression, HTTP_SERVICE_COMPRESSION_TYPE_GZIP, nullptr, 16, &out));
    string_uninit(&out);

    test_expect_false(test, "type NONE refuses",
        http_service_compression_compress(&compression, HTTP_SERVICE_COMPRESSION_TYPE_NONE, (Byte const*) "hello", 5, &out));
    string_uninit(&out);

    /* 4 KiB of noise expands under both codecs, and sending a LARGER body plus a
     * decode costs the client twice. min_size cannot catch this - the body is big enough. */
    Byte noise[_NOISE_SIZE] = DEFAULT_INITIALIZATION;

    _noise_build(noise, sizeof(noise));

    test_expect_false(test, "incompressible noise refuses under gzip",
        http_service_compression_compress(&compression, HTTP_SERVICE_COMPRESSION_TYPE_GZIP, noise, sizeof(noise), &out));
    string_uninit(&out);

    test_expect_false(test, "incompressible noise refuses under brotli",
        http_service_compression_compress(&compression, HTTP_SERVICE_COMPRESSION_TYPE_BROTLI, noise, sizeof(noise), &out));
    string_uninit(&out);

    http_service_compression_uninit(&compression);

    /* max_size is documented as the cap that keeps a large body from being
     * held twice, and the RAW tier used to ignore it - so a caller reaching past the seam
     * allocated the codec's bound for exactly the payload the service forbids. */
    char payload[_LIVE_PAYLOAD_SIZE] = DEFAULT_INITIALIZATION;

    _payload_build(payload, sizeof(payload));

    HTTP_Service_Compression capped = DEFAULT_INITIALIZATION;

    test_expect_true(test, "a capped service initializes",
        http_service_compression_init_2(&capped, 16, sizeof(payload) - 1, 5, 6));
    test_expect_false(test, "compress refuses a payload past max_size",
        http_service_compression_compress(&capped, HTTP_SERVICE_COMPRESSION_TYPE_GZIP, (Byte const*) payload, sizeof(payload), &out));
    test_expect_null(test, "leaving the EMPTY String there too", string_get_data(&out));
    string_uninit(&out);

    test_expect_false(test, "and refuses it under brotli",
        http_service_compression_compress(&capped, HTTP_SERVICE_COMPRESSION_TYPE_BROTLI, (Byte const*) payload, sizeof(payload), &out));
    string_uninit(&out);

    test_expect_true(test, "while max_size itself still compresses",
        http_service_compression_compress(&capped, HTTP_SERVICE_COMPRESSION_TYPE_GZIP, (Byte const*) payload, sizeof(payload) - 1, &out));
    string_uninit(&out);

    http_service_compression_uninit(&capped);

    /* max_size 0 means NO upper bound - the cap must not fire on the "unbounded" spelling. */
    HTTP_Service_Compression unbounded = DEFAULT_INITIALIZATION;

    test_expect_true(test, "an unbounded service initializes", http_service_compression_init_2(&unbounded, 16, 0, 5, 6));
    test_expect_true(test, "and max_size 0 caps nothing",
        http_service_compression_compress(&unbounded, HTTP_SERVICE_COMPRESSION_TYPE_GZIP, (Byte const*) payload, sizeof(payload), &out));
    string_uninit(&out);

    http_service_compression_uninit(&unbounded);

    test_case_end(test);
}

static void _test_compression_constructors(Test *const test) {
    test_case_begin(test, "the in-place constructors refuse whole");

    HTTP_Service_Compression compression = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 accepts the whole legal range", http_service_compression_init_2(&compression, 1, 0,
        HTTP_SERVICE_COMPRESSION_MAX_BROTLI_QUALITY, HTTP_SERVICE_COMPRESSION_MAX_GZIP_LEVEL));
    test_expect_u(test, "storing the quality", HTTP_SERVICE_COMPRESSION_MAX_BROTLI_QUALITY, (USize) compression.brotli_quality);
    test_expect_u(test, "and the level", HTTP_SERVICE_COMPRESSION_MAX_GZIP_LEVEL, (USize) compression.gzip_level);

    http_service_compression_uninit(&compression);

    test_expect_u(test, "uninit zeroes min_size", 0, compression.min_size);
    test_expect_u(test, "and max_size", 0, compression.max_size);

    HTTP_Service_Compression refused = DEFAULT_INITIALIZATION;

    test_expect_false(test, "a quality past 11 is refused", http_service_compression_init_2(&refused, 1024, 0, 12, 6));
    test_expect_false(test, "a negative quality is refused", http_service_compression_init_2(&refused, 1024, 0, -1, 6));
    test_expect_false(test, "a level past 9 is refused", http_service_compression_init_2(&refused, 1024, 0, 5, 10));
    test_expect_false(test, "a negative level is refused", http_service_compression_init_2(&refused, 1024, 0, 5, -1));
    test_expect_false(test, "max_size below min_size is refused", http_service_compression_init_2(&refused, 1024, 512, 5, 6));

    /* A refused constructor leaves an instance that DOES NOT EXIST, so the
     * caller can tell "startup failed" from "startup built something that compresses nothing". */
    test_expect_u(test, "and the instance is left zeroed", 0, refused.min_size);
    test_expect_null(test, "with no list behind it", al_str_get_data(&refused.mime_types));

    test_expect_string(test, "type_name spells brotli", "br", http_service_compression_type_name(HTTP_SERVICE_COMPRESSION_TYPE_BROTLI));
    test_expect_string(test, "type_name spells gzip", "gzip", http_service_compression_type_name(HTTP_SERVICE_COMPRESSION_TYPE_GZIP));
    test_expect_string(test, "type_name spells NONE as identity", "identity", http_service_compression_type_name(HTTP_SERVICE_COMPRESSION_TYPE_NONE));

    test_case_end(test);
}

#ifdef ARENA_IMPLEMENTATION
static void _test_compression_arena_backed(Test *const test) {
    test_case_begin(test, "an arena-backed service compresses the same way");

    Arena arena = arena_init_1(1024 * 1024, ARENA_TYPE_LINEAR);

    HTTP_Service_Compression compression = DEFAULT_INITIALIZATION;

    test_expect_true(test, "alloc_init_1 succeeds", http_service_compression_alloc_init_1(&compression, &arena));

    char payload[_LIVE_PAYLOAD_SIZE] = DEFAULT_INITIALIZATION;

    _payload_build(payload, sizeof(payload));

    String compressed = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "compress succeeds out of the arena",
            http_service_compression_compress(&compression, HTTP_SERVICE_COMPRESSION_TYPE_BROTLI, (Byte const*) payload, sizeof(payload), &compressed))) {
        Byte    decoded[_DECODED_MAX]   = DEFAULT_INITIALIZATION;
        USize   decoded_size            = 0;

        test_expect_true(test, "and the body decodes",
            _brotli_inflate((Byte const*) string_get_data(&compressed), string_get_size(&compressed), decoded, sizeof(decoded), &decoded_size));
        test_expect_u(test, "to the original byte count", sizeof(payload), decoded_size);
    }

    string_uninit(&compressed);
    http_service_compression_uninit(&compression);
    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

/*
 * Arena carries no public "used bytes" accessor, so the pin below measures what is LEFT: it
 * drains an arena in fixed blocks and counts them. Two arenas built the same way, one of which
 * ran compress calls and one of which did not, must drain to the same count.
 */
static USize _test_arena_blocks_left(Arena *const arena) {
    USize blocks = 0;

    while (allocator_try_borrow(_ARENA_PROBE_BLOCK, arena) != nullptr) {
        blocks = blocks + 1;
    }

    return blocks;
}

static void _test_compression_arena_retains_nothing(Test *const test) {
    test_case_begin(test, "an arena-backed service's arena does not grow across repeated compress calls");

    /* A codec buffer is per-RESPONSE and as wide as max_size, while the allocator handed to
     * alloc_init_* is SERVICE-lifetime: a linear arena reclaims nothing on string_uninit, so a
     * codec routed through it would leave a long-lived server holding every body it ever
     * compressed - and two concurrent calls would bump one Arena that is not thread-safe, which
     * the header's "safe on ONE shared const service" sentence forbids. The buffers and the
     * String compress writes are heap-owned on both tiers, and this is what says so. */
    Arena quiet = arena_init_1(_ARENA_PROBE_SIZE, ARENA_TYPE_LINEAR);
    Arena busy  = arena_init_1(_ARENA_PROBE_SIZE, ARENA_TYPE_LINEAR);

    HTTP_Service_Compression quiet_service = DEFAULT_INITIALIZATION;
    HTTP_Service_Compression busy_service  = DEFAULT_INITIALIZATION;

    test_expect_true(test, "the arena that compresses nothing initializes", http_service_compression_alloc_init_1(&quiet_service, &quiet));
    test_expect_true(test, "and so does its twin, which will run 128 compress calls", http_service_compression_alloc_init_1(&busy_service, &busy));

    char payload[_LIVE_PAYLOAD_SIZE] = DEFAULT_INITIALIZATION;

    _payload_build(payload, sizeof(payload));

    USize successes = 0;

    for (USize round = 0; round < _ARENA_PROBE_ROUNDS; round += 1) {
        String out = DEFAULT_INITIALIZATION;

        if (http_service_compression_compress(&busy_service, HTTP_SERVICE_COMPRESSION_TYPE_BROTLI, (Byte const*) payload, sizeof(payload), &out)) {
            successes += 1;
        }

        string_uninit(&out);

        if (http_service_compression_compress(&busy_service, HTTP_SERVICE_COMPRESSION_TYPE_GZIP, (Byte const*) payload, sizeof(payload), &out)) {
            successes += 1;
        }

        string_uninit(&out);

        /* The two refusal paths write the EMPTY String into `out`, which must not come from the
         * arena either - NONE is not a codec, and a zero size never reaches one. */
        if (!http_service_compression_compress(&busy_service, HTTP_SERVICE_COMPRESSION_TYPE_NONE, (Byte const*) payload, sizeof(payload), &out)) {
            successes += 1;
        }

        string_uninit(&out);

        if (!http_service_compression_compress(&busy_service, HTTP_SERVICE_COMPRESSION_TYPE_BROTLI, (Byte const*) payload, 0, &out)) {
            successes += 1;
        }

        string_uninit(&out);
    }

    test_expect_u(test, "all 128 arena-tier calls answered correctly", 128, successes);

    USize const quiet_left = _test_arena_blocks_left(&quiet);
    USize const busy_left  = _test_arena_blocks_left(&busy);

    test_expect_true(test, "the probe has room to measure, so the comparison is not vacuously 0 == 0", quiet_left > 0);
    test_expect_u(test, "and the busy arena has exactly as much left as the one that compressed nothing", quiet_left, busy_left);

    http_service_compression_uninit(&busy_service);
    http_service_compression_uninit(&quiet_service);

    arena_uninit(&busy, ARENA_TYPE_LINEAR);
    arena_uninit(&quiet, ARENA_TYPE_LINEAR);

    test_case_end(test);
}
#endif // ARENA_IMPLEMENTATION

/*==============================================================================
 * MARK: - Live case
 *============================================================================*/

static HTTP_Service_Compression  _live_compression = DEFAULT_INITIALIZATION;
static char                      _live_payload[_LIVE_PAYLOAD_SIZE] = DEFAULT_INITIALIZATION;

/* The reference shape for every consumer: one call, and the seam decides whether the body goes
 * out compressed. */
static void _on_document(HTTP_Server_Route *const route) {
    HTTP_Server_Holder *const holder = http_server_route_get_holder(route);

    http_service_compression_send(&_live_compression, holder->request, holder->response, (Byte const*) _live_payload,
        sizeof(_live_payload), _HTML_TYPE, HTTP_SERVER_STATUS_CODE_OK);
}

/* The 206 shape a Range-serving caller writes: a Content-Range that indexes the IDENTITY
 * representation, and the same seam call. The seam has to refuse the coding
 * here on its own, because a caller who got this wrong would emit a slice no client can put
 * back together (RFC 9110 section 14.4). */
static void _on_slice(HTTP_Server_Route *const route) {
    HTTP_Server_Holder *const holder = http_server_route_get_holder(route);

    http_server_response_header_add(holder->response, "Content-Range", "bytes 0-4095/8192");

    http_service_compression_send(&_live_compression, holder->request, holder->response, (Byte const*) _live_payload,
        sizeof(_live_payload), _HTML_TYPE, HTTP_SERVER_STATUS_CODE_PARTIAL_CONTENT);
}

static void _handle(void *const context, HTTP_Server_Request *const request, HTTP_Server_Response *const response) {
    HTTP_Server        *const server = (HTTP_Server*) context;
    HTTP_Server_Holder         holder = { .request = request, .response = response, .arena = nullptr };

    if (!http_server_router_dispatch_2(server->router, http_server_request_get_path_1(request), http_server_request_get_path_size(request), &holder)) {
        http_server_response_send_empty(response, HTTP_SERVER_STATUS_CODE_NOT_FOUND);
    }
}

/* One live exchange: fire the request, split the reply, assert the coding headers, and decode
 * the body back to the exact bytes the route handed the seam. */
static void _live_exchange(Test *const test, U16 const port, char const *const accept_encoding, char const *const expected_encoding) {
    Byte    reply[_REPLY_MAX]   = DEFAULT_INITIALIZATION;
    USize   reply_size          = 0;

    if (!test_expect_true(test, "the request round trips", _round_trip(port, "/document", accept_encoding, reply, sizeof(reply), &reply_size))) {
        return;
    }

    USize header_size = 0;
    USize body_start  = 0;

    if (!test_expect_true(test, "the reply has a header block", _reply_split(reply, reply_size, &header_size, &body_start))) {
        return;
    }

    char const *const headers = (char const*) reply;

    test_expect_true(test, "answered 200", _find_ignore_case(headers, header_size, "200") != USIZE_MAX);

    /* Vary goes out on the identity path too, or a shared cache can serve one
     * client's stored body to a client that would have taken the other one. */
    test_expect_true(test, "Vary: Accept-Encoding is present", _find_ignore_case(headers, header_size, "vary: accept-encoding") != USIZE_MAX);

    USize const body_size = reply_size - body_start;

    Byte    decoded[_DECODED_MAX]   = DEFAULT_INITIALIZATION;
    USize   decoded_size            = 0;

    if (expected_encoding[0] == '\0') {
        test_expect_true(test, "no Content-Encoding is sent", _find_ignore_case(headers, header_size, "content-encoding") == USIZE_MAX);
        test_expect_u(test, "and the body is the payload itself", sizeof(_live_payload), body_size);
        test_expect_true(test, "byte for byte",
            char_compare_equal_2((char const*) reply + body_start, sizeof(_live_payload), _live_payload, sizeof(_live_payload)));

        return;
    }

    char expected_header[64] = DEFAULT_INITIALIZATION;

    snprintf(expected_header, sizeof(expected_header), "content-encoding: %s", expected_encoding);

    test_expect_true(test, "the negotiated Content-Encoding is sent", _find_ignore_case(headers, header_size, expected_header) != USIZE_MAX);
    test_expect_true(test, "and the body is smaller than the payload", body_size < sizeof(_live_payload));

    bool const decoded_ok = char_compare_equal_1(expected_encoding, "br")
        ? _brotli_inflate(reply + body_start, body_size, decoded, sizeof(decoded), &decoded_size)
        : _gzip_inflate(reply + body_start, body_size, decoded, sizeof(decoded), &decoded_size);

    test_expect_true(test, "the body decodes", decoded_ok);
    test_expect_u(test, "to the payload's byte count", sizeof(_live_payload), decoded_size);
    test_expect_true(test, "and the payload's bytes",
        char_compare_equal_2((char const*) decoded, sizeof(_live_payload), _live_payload, sizeof(_live_payload)));
}

/* The 206 exchange: a client that asked for br, a route that hands the seam a Range slice, and a
 * reply that must come back UNCODED - Vary included, because the representation still varies. */
static void _live_slice_exchange(Test *const test, U16 const port) {
    Byte    reply[_REPLY_MAX]   = DEFAULT_INITIALIZATION;
    USize   reply_size          = 0;

    if (!test_expect_true(test, "the slice request round trips", _round_trip(port, "/slice", "br", reply, sizeof(reply), &reply_size))) {
        return;
    }

    USize header_size = 0;
    USize body_start  = 0;

    if (!test_expect_true(test, "the slice reply has a header block", _reply_split(reply, reply_size, &header_size, &body_start))) {
        return;
    }

    char const *const headers = (char const*) reply;

    test_expect_true(test, "answered 206", _find_ignore_case(headers, header_size, "206") != USIZE_MAX);
    test_expect_true(test, "carrying the Content-Range it was given",
        _find_ignore_case(headers, header_size, "content-range: bytes 0-4095/8192") != USIZE_MAX);
    test_expect_true(test, "no Content-Encoding on a 206", _find_ignore_case(headers, header_size, "content-encoding") == USIZE_MAX);
    test_expect_true(test, "but Vary: Accept-Encoding still goes out",
        _find_ignore_case(headers, header_size, "vary: accept-encoding") != USIZE_MAX);

    USize const body_size = reply_size - body_start;

    test_expect_u(test, "and the slice is the identity bytes", sizeof(_live_payload), body_size);
    test_expect_true(test, "byte for byte",
        char_compare_equal_2((char const*) reply + body_start, sizeof(_live_payload), _live_payload, sizeof(_live_payload)));
}

static void _test_compression_live_route(Test *const test) {
    test_case_begin(test, "a live route answers br, gzip and identity");

    test_expect_true(test, "the service initializes", http_service_compression_init_1(&_live_compression));

    _payload_build(_live_payload, sizeof(_live_payload));

    HTTP_Server server = DEFAULT_INITIALIZATION;

    test_expect_true(test, "http_server_init succeeds", http_server_init(&server));

    http_server_set_handler(&server, _handle, &server);

    test_expect_true(test, "the route registers", http_server_route_add(&server, "/document", _on_document));
    test_expect_true(test, "the Range route registers", http_server_route_add(&server, "/slice", _on_slice));
    test_expect_true(test, "http_server_run succeeds on port 0", result_is_success(http_server_run(&server, 0, true)));

    U16 const port = http_server_get_port(&server);

    test_expect_true(test, "an ephemeral port was assigned", port != 0);

    _live_exchange(test, port, "br", "br");
    _live_exchange(test, port, "gzip", "gzip");
    _live_exchange(test, port, "gzip, deflate", "gzip");
    _live_exchange(test, port, "identity", "");
    _live_exchange(test, port, "", "");
    _live_exchange(test, port, "br;q=0, gzip;q=0", "");

    /* A header the client DID send, that names no coding at all. The seam reads the
     * Accept-Encoding tokenizer directly rather than re-entering negotiate_2 - which would
     * rerun the size and MIME test the seam already ran - so this pins that the direct call
     * still answers identity for a syntactically present but coding-free value. */
    _live_exchange(test, port, ",", "");
    _live_slice_exchange(test, port);

    http_server_stop(&server);
    http_server_uninit(&server);

    http_service_compression_uninit(&_live_compression);

    test_case_end(test);
}

int main(void) {
    LogConfig const log_config = {
        .level             = LOG_LEVEL_ERROR,
        .stream            = stdout,
        .timestamp_enabled = true,
        .autoflush         = true
    };

    log_init(log_config);

    Test test = test_init("tests/http/service/compression/test_all.c");

    test_verbose_set(&test, false);

    test_suite_begin(&test, "http_service_compression");
    _test_compression_gzip_round_trip(&test);
    _test_compression_brotli_round_trip(&test);
    _test_compression_size_window(&test);
    _test_compression_mime_rules(&test);
    _test_compression_accept_encoding_matrix(&test);
    _test_compression_negotiate_tiers(&test);
    _test_compression_refusals(&test);
    _test_compression_constructors(&test);
#ifdef ARENA_IMPLEMENTATION
    _test_compression_arena_backed(&test);
    _test_compression_arena_retains_nothing(&test);
#endif // ARENA_IMPLEMENTATION
    _test_compression_live_route(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}