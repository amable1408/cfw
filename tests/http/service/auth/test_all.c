/*
 * test_all.c - the http/service/auth suite.
 *
 * auth has ~30 call sites across the apps but no suite of its own until now, and the public
 * export gate refuses a suite-less module (ruling 2026-08-27). The offline cases cover the
 * whole surface; one LIVE case drives a real server on port 0 so the permission verdict ->
 * 401-versus-403 mapping is pinned on the wire rather than in prose.
 *
 * Every service here is built with a SMALL iteration count. The cost of a KDF pass is
 * crypto/password's contract, not auth's, and a suite that paid the shipping 600000 rounds
 * per init would take minutes. The one exception is the decoy-timing case, which needs a cost
 * it can measure.
 */
#include <chrono/chrono.h>
#include <http/server/http_server.h>
#include <http/service/auth/auth.h>
#include <http/service/permission/permission.h>
#include <net/net.h>
#include <test/test.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

#define _CLIENT_IO_TIMEOUT_MS   4000
#define _CLIENT_RAW_MAX         8192
#define _DECOY_ITERATIONS       200000
#define _EMAIL_TTL              86400
#define _FAST_ITERATIONS        1000
#define _KDF_MINIMUM_MICROSECONDS 1000
#define _MAX_LENGTH             128
#define _MIN_LENGTH             12
#define _RESET_TTL              900
/* SHA-256 of the EMPTY string - what a caller who ignored a CSPRNG failure and hashed the
 * empty token would have persisted. The empty-candidate refusal is pinned against THIS, not
 * against a real token's hash, so the assertion cannot pass by coincidence. */
#define _SHA256_OF_EMPTY        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
#define _TOKEN_BYTE_COUNT       32

/*==============================================================================
 * MARK: - Helpers
 *============================================================================*/

/** @brief Build an owned, NUL-terminated String from a literal. An empty text yields the
 *         EMPTY String, which is exactly the "no account row was found" shape. */
static String _string_of(char const *const text) {
    String          string      = string_init_1();
    USize   const   size        = char_length(text);
    char            terminator  = '\0';

    if (size > 0) {
        string_add_last_2(&string, text, size);
    }

    string_add_last_2(&string, &terminator, 1);
    string_set_size(&string, size);

    return string;
}

static HTTP_Service_Auth _auth_fast(void) {
    return http_service_auth_init_3(_MIN_LENGTH, _MAX_LENGTH, _FAST_ITERATIONS, _TOKEN_BYTE_COUNT, _RESET_TTL, _EMAIL_TTL);
}

/*==============================================================================
 * MARK: - Offline cases
 *============================================================================*/

static void _test_policy(Test *const test) {
    test_case_begin(test, "password policy edges and reasons");

    HTTP_Service_Auth auth = _auth_fast();

    char under[_MIN_LENGTH]         = DEFAULT_INITIALIZATION;
    char exact[_MIN_LENGTH + 1]     = DEFAULT_INITIALIZATION;
    char ceiling[_MAX_LENGTH + 1]   = DEFAULT_INITIALIZATION;
    char over[_MAX_LENGTH + 2]      = DEFAULT_INITIALIZATION;

    for (USize index = 0; index + 1 < sizeof(under); index += 1) {
        under[index] = 'a';
    }

    for (USize index = 0; index + 1 < sizeof(exact); index += 1) {
        exact[index] = 'a';
    }

    for (USize index = 0; index + 1 < sizeof(ceiling); index += 1) {
        ceiling[index] = 'a';
    }

    for (USize index = 0; index + 1 < sizeof(over); index += 1) {
        over[index] = 'a';
    }

    test_expect_u(test, "empty password is TOO_SHORT", (USize) HTTP_SERVICE_AUTH_PASSWORD_TOO_SHORT, (USize) http_service_auth_password_reason(&auth, ""));
    test_expect_u(test, "one byte under the floor is TOO_SHORT", (USize) HTTP_SERVICE_AUTH_PASSWORD_TOO_SHORT, (USize) http_service_auth_password_reason(&auth, under));
    test_expect_u(test, "exactly the floor is OK", (USize) HTTP_SERVICE_AUTH_PASSWORD_OK, (USize) http_service_auth_password_reason(&auth, exact));
    test_expect_u(test, "exactly the ceiling is OK", (USize) HTTP_SERVICE_AUTH_PASSWORD_OK, (USize) http_service_auth_password_reason(&auth, ceiling));
    test_expect_u(test, "one byte over the ceiling is TOO_LONG", (USize) HTTP_SERVICE_AUTH_PASSWORD_TOO_LONG, (USize) http_service_auth_password_reason(&auth, over));

    test_expect_false(test, "valid agrees with TOO_SHORT", http_service_auth_password_valid(&auth, under));
    test_expect_true(test, "valid agrees with OK", http_service_auth_password_valid(&auth, exact));
    test_expect_false(test, "valid agrees with TOO_LONG", http_service_auth_password_valid(&auth, over));

    /* The ceiling is the DoS bound: the refusal must land before any KDF work, so a password
     * past it never reaches crypto/password at all. */
    HTTP_Service_Auth defaults = http_service_auth_init_2(_MIN_LENGTH, _FAST_ITERATIONS, _TOKEN_BYTE_COUNT, _RESET_TTL, _EMAIL_TTL);

    test_expect_u(test, "init_2 keeps the default ceiling", (USize) HTTP_SERVICE_AUTH_DEFAULT_PASSWORD_MAX_LENGTH, defaults.password_max_length);

    http_service_auth_uninit(&defaults);
    http_service_auth_uninit(&auth);

    test_case_end(test);
}

static void _test_hash_and_verify(Test *const test) {
    test_case_begin(test, "hash / verify round trip and refusals");

    HTTP_Service_Auth auth = _auth_fast();

    String hash = http_service_auth_hash_password(&auth, "correct horse battery");

    test_expect_false(test, "a hash was produced", string_empty(&hash));
    test_expect_true(test, "the right password verifies", http_service_auth_verify_password_1(&auth, "correct horse battery", &hash));
    test_expect_false(test, "a wrong password does not", http_service_auth_verify_password_1(&auth, "correct horse batterz", &hash));
    test_expect_false(test, "an empty password does not", http_service_auth_verify_password_1(&auth, "", &hash));

    // The same password hashed twice yields different records: crypto/password salts each one.
    String again = http_service_auth_hash_password(&auth, "correct horse battery");

    test_expect_false(test, "two hashes of one password differ", char_compare_equal_1(string_get_data(&hash), string_get_data(&again)));
    test_expect_true(test, "and both still verify", http_service_auth_verify_password_1(&auth, "correct horse battery", &again));

    String absent = _string_of("");

    test_expect_true(test, "an absent record is the empty String", string_empty(&absent));
    test_expect_false(test, "an empty stored hash refuses", http_service_auth_verify_password_1(&auth, "correct horse battery", &absent));

    String malformed = _string_of("pbkdf2_sha256$notanumber$salt$digest");

    test_expect_false(test, "a malformed record refuses rather than aborting", http_service_auth_verify_password_1(&auth, "correct horse battery", &malformed));

    String garbage = _string_of("$$$$");

    test_expect_false(test, "garbage refuses too", http_service_auth_verify_password_1(&auth, "correct horse battery", &garbage));

    /* The char const* tier: a database cell arrives as a plain string, so verify_password_3
     * answers case for case what _2 answers over the same record - the empty cell included,
     * where emptiness is the FIRST BYTE and not a null pointer. */
    test_expect_true(test, "verify_password_3 accepts the record as a plain string", http_service_auth_verify_password_3(&auth, "correct horse battery", string_get_data(&hash)));
    test_expect_false(test, "verify_password_3 refuses a wrong password", http_service_auth_verify_password_3(&auth, "correct horse batterz", string_get_data(&hash)));
    test_expect_false(test, "verify_password_3 refuses an empty cell", http_service_auth_verify_password_3(&auth, "correct horse battery", ""));
    test_expect_false(test, "verify_password_3 refuses a malformed cell", http_service_auth_verify_password_3(&auth, "correct horse battery", string_get_data(&malformed)));

    string_uninit(&garbage);
    string_uninit(&malformed);
    string_uninit(&absent);
    string_uninit(&again);
    string_uninit(&hash);

    http_service_auth_uninit(&auth);

    test_case_end(test);
}

static void _test_needs_rehash(Test *const test) {
    test_case_begin(test, "needs_rehash across iteration counts");

    HTTP_Service_Auth low = _auth_fast();

    String hash = http_service_auth_hash_password(&low, "correct horse battery");

    test_expect_false(test, "a record at the configured count does not need re-hashing", http_service_auth_password_needs_rehash(&low, &hash));

    HTTP_Service_Auth high = http_service_auth_init_3(_MIN_LENGTH, _MAX_LENGTH, _FAST_ITERATIONS * 2, _TOKEN_BYTE_COUNT, _RESET_TTL, _EMAIL_TTL);

    test_expect_true(test, "a record below the configured count needs re-hashing", http_service_auth_password_needs_rehash(&high, &hash));

    /* The cost comes from the STORED RECORD, not from the service: the old record still
     * verifies under a service configured for twice the rounds. That is what makes the
     * compare-and-swap persist rule in the header workable at all. */
    test_expect_true(test, "and still verifies under the raised service", http_service_auth_verify_password_1(&high, "correct horse battery", &hash));

    String absent = _string_of("");
    String malformed = _string_of("not-a-record");

    test_expect_false(test, "an empty record never needs re-hashing", http_service_auth_password_needs_rehash(&high, &absent));
    test_expect_false(test, "a malformed record never needs re-hashing", http_service_auth_password_needs_rehash(&high, &malformed));

    string_uninit(&malformed);
    string_uninit(&absent);
    string_uninit(&hash);

    http_service_auth_uninit(&high);
    http_service_auth_uninit(&low);

    test_case_end(test);
}

static void _test_token_shape(Test *const test) {
    test_case_begin(test, "token shape and TTL arithmetic");

    HTTP_Service_Auth auth = _auth_fast();

    USize const before = (USize) datetime_now();

    HTTP_Service_Auth_Token token = http_service_auth_token_create(&auth, 60);

    USize const after = (USize) datetime_now();

    test_expect_false(test, "a token value was produced", string_empty(&token.value));
    test_expect_u(test, "hex length is two characters per random byte", _TOKEN_BYTE_COUNT * 2, string_get_size(&token.value));
    test_expect_u(test, "the value is NUL terminated at its size", 0, (USize) (U8) string_get_data(&token.value)[string_get_size(&token.value)]);

    test_expect_true(test, "expiry is at or after now + ttl", token.expires_at >= before + 60);
    test_expect_true(test, "expiry is no later than the second read + ttl", token.expires_at <= after + 60);

    HTTP_Service_Auth_Token other = http_service_auth_token_create(&auth, 60);

    test_expect_false(test, "two tokens differ", char_compare_equal_1(string_get_data(&token.value), string_get_data(&other.value)));

    HTTP_Service_Auth_Token reset = http_service_auth_reset_token_create(&auth);
    HTTP_Service_Auth_Token email = http_service_auth_email_token_create(&auth);

    test_expect_true(test, "the reset token uses the reset TTL", reset.expires_at >= before + _RESET_TTL);
    test_expect_true(test, "the email token uses the email TTL", email.expires_at >= before + _EMAIL_TTL);

    http_service_auth_token_uninit(&email);
    http_service_auth_token_uninit(&reset);
    http_service_auth_token_uninit(&other);

    /* The fail-CLOSED clamp. USIZE_MAX is what token_create writes when the ttl overflows AND
     * what a pre-epoch clock reading becomes, and it has to read as EXPIRED under every clock
     * reading - a token minted while the clock was broken must not outlive the repair. Before
     * the clamp flipped from 0 to USIZE_MAX, expired_at(&saturated, 0) answered false and that
     * token was eternal. */
    HTTP_Service_Auth_Token saturated = http_service_auth_token_create(&auth, USIZE_MAX);

    test_expect_u(test, "an overflowing ttl saturates the expiry", USIZE_MAX, saturated.expires_at);
    test_expect_true(test, "a saturated expiry is expired at now = 0", http_service_auth_token_expired_at(&saturated, 0));
    test_expect_true(test, "a saturated expiry is expired at now = USIZE_MAX", http_service_auth_token_expired_at(&saturated, USIZE_MAX));
    test_expect_true(test, "a saturated expiry is expired against the real clock", http_service_auth_token_expired(&saturated));
    test_expect_false(test, "a saturated token verifies nothing", http_service_auth_token_verify_at(&saturated, string_get_data(&saturated.value), 0));

    http_service_auth_token_uninit(&saturated);

    http_service_auth_token_uninit(&token);

    test_expect_u(test, "uninit zeroes the expiry", 0, token.expires_at);
    test_expect_true(test, "uninit releases the value", string_empty(&token.value));

    http_service_auth_uninit(&auth);

    test_case_end(test);
}

static void _test_token_verify_memory(Test *const test) {
    test_case_begin(test, "in-memory token verification");

    HTTP_Service_Auth auth = _auth_fast();

    HTTP_Service_Auth_Token token = http_service_auth_token_create(&auth, 60);

    char candidate[(_TOKEN_BYTE_COUNT * 2) + 1] = DEFAULT_INITIALIZATION;

    char_copy_3(candidate, sizeof(candidate), string_get_data(&token.value), string_get_size(&token.value));

    candidate[string_get_size(&token.value)] = '\0';

    test_expect_true(test, "the exact value verifies", http_service_auth_token_verify_1(&token, candidate));

    // Same length, one byte different: the constant-time compare must still refuse.
    candidate[0] = candidate[0] == 'a' ? 'b' : 'a';

    test_expect_false(test, "a same-length mismatch refuses", http_service_auth_token_verify_1(&token, candidate));

    test_expect_false(test, "a short candidate refuses", http_service_auth_token_verify_1(&token, "deadbeef"));
    test_expect_false(test, "an empty candidate refuses", http_service_auth_token_verify_1(&token, ""));

    /* The deterministic forms. The boundary is INCLUSIVE - expires_at itself is expired -
     * which is what http_service_session_token_expired has always done; auth used to be one
     * second more generous. */
    test_expect_false(test, "not expired one second before", http_service_auth_token_expired_at(&token, token.expires_at - 1));
    test_expect_true(test, "expired exactly at expires_at", http_service_auth_token_expired_at(&token, token.expires_at));
    test_expect_true(test, "expired after expires_at", http_service_auth_token_expired_at(&token, token.expires_at + 1));
    test_expect_false(test, "a live token is not expired now", http_service_auth_token_expired(&token));

    char_copy_3(candidate, sizeof(candidate), string_get_data(&token.value), string_get_size(&token.value));

    candidate[string_get_size(&token.value)] = '\0';

    test_expect_true(test, "verify_at accepts before expiry", http_service_auth_token_verify_at(&token, candidate, token.expires_at - 1));
    test_expect_false(test, "verify_at refuses at expiry", http_service_auth_token_verify_at(&token, candidate, token.expires_at));

    HTTP_Service_Auth_Token empty = { .expires_at = token.expires_at, .value = string_init_1() };

    test_expect_false(test, "a token with no value verifies nothing", http_service_auth_token_verify_1(&empty, candidate));

    http_service_auth_token_uninit(&empty);
    http_service_auth_token_uninit(&token);

    http_service_auth_uninit(&auth);

    test_case_end(test);
}

static void _test_token_hash_and_verify_2(Test *const test) {
    test_case_begin(test, "token_hash and verification against a stored hash");

    HTTP_Service_Auth auth = _auth_fast();

    HTTP_Service_Auth_Token token = http_service_auth_token_create(&auth, 60);

    char value[(_TOKEN_BYTE_COUNT * 2) + 1] = DEFAULT_INITIALIZATION;

    char_copy_3(value, sizeof(value), string_get_data(&token.value), string_get_size(&token.value));

    value[string_get_size(&token.value)] = '\0';

    String stored = http_service_auth_token_hash(&auth, value);

    test_expect_false(test, "a hash was produced", string_empty(&stored));
    test_expect_u(test, "SHA-256 hex is 64 characters", 64, string_get_size(&stored));
    test_expect_false(test, "the stored hash is not the token", char_compare_equal_1(string_get_data(&stored), value));

    String again = http_service_auth_token_hash(&auth, value);

    test_expect_true(test, "hashing is deterministic", char_compare_equal_1(string_get_data(&stored), string_get_data(&again)));

    test_expect_true(test, "the token verifies against its stored hash", http_service_auth_token_verify_2(&auth, value, &stored, token.expires_at));

    char wrong[sizeof(value)] = DEFAULT_INITIALIZATION;

    char_copy_3(wrong, sizeof(wrong), value, string_get_size(&token.value));

    wrong[string_get_size(&token.value)] = '\0';
    wrong[0] = wrong[0] == 'a' ? 'b' : 'a';

    test_expect_false(test, "a same-length mismatch refuses", http_service_auth_token_verify_2(&auth, wrong, &stored, token.expires_at));
    test_expect_false(test, "a short candidate refuses", http_service_auth_token_verify_2(&auth, "deadbeef", &stored, token.expires_at));

    /* Expiry is checked in the SAME call, so a caller cannot verify the hash and forget the
     * clock. A stored expiry already in the past refuses a candidate that otherwise matches. */
    test_expect_false(test, "an expired record refuses a matching candidate", http_service_auth_token_verify_2(&auth, value, &stored, 1));

    /* auth.h promises false for an empty candidate, and it is a LENGTH gate that keeps the
     * promise now, not the hash comparison. Against the real stored hash the old pin could
     * only ever hold by accident - "" hashes to something that hash does not equal - so the
     * honest pin hands verify_3 the SHA-256 of the empty string ITSELF, the exact row a
     * caller who persisted an empty token would have written. Nothing but the gate refuses
     * this one: without it the candidate hashes to precisely the stored value and verifies. */
    test_expect_false(test, "an empty candidate refuses against sha256(\"\") itself", http_service_auth_token_verify_3(&auth, "", _SHA256_OF_EMPTY, token.expires_at));
    test_expect_false(test, "an empty candidate refuses against the real stored hash too", http_service_auth_token_verify_2(&auth, "", &stored, token.expires_at));

    /* The gate is on LENGTH, not on emptiness, so a NON-empty candidate of the wrong size is
     * refused before any hashing too: a one-byte-short prefix of the real token and a
     * one-byte-long extension of it. */
    char short_candidate[sizeof(value)]     = DEFAULT_INITIALIZATION;
    char long_candidate[sizeof(value) + 1]  = DEFAULT_INITIALIZATION;

    char_copy_3(short_candidate, sizeof(short_candidate), value, string_get_size(&token.value) - 1);
    char_copy_3(long_candidate, sizeof(long_candidate), value, string_get_size(&token.value));

    short_candidate[string_get_size(&token.value) - 1] = '\0';
    long_candidate[string_get_size(&token.value)] = value[0];
    long_candidate[string_get_size(&token.value) + 1] = '\0';

    test_expect_false(test, "a candidate one byte short refuses", http_service_auth_token_verify_2(&auth, short_candidate, &stored, token.expires_at));
    test_expect_false(test, "a candidate one byte long refuses", http_service_auth_token_verify_2(&auth, long_candidate, &stored, token.expires_at));

    /* The other half of the proof: a candidate of exactly the RIGHT length and the wrong
     * bytes still reaches the comparison and still refuses. Without this the gate could be
     * refusing everything and every assertion above would still pass. */
    test_expect_false(test, "a right-length wrong candidate reaches the comparison and refuses", http_service_auth_token_verify_3(&auth, wrong, string_get_data(&stored), token.expires_at));

    /* Hashing carries the same gate, which is what stops the corrupt row being written at
     * all: token_hash of an empty or wrong-length token answers the empty String. */
    String empty_hash = http_service_auth_token_hash(&auth, "");
    String short_hash = http_service_auth_token_hash(&auth, short_candidate);

    test_expect_true(test, "token_hash refuses the empty token", string_empty(&empty_hash));
    test_expect_true(test, "token_hash refuses a wrong-length token", string_empty(&short_hash));

    string_uninit(&short_hash);
    string_uninit(&empty_hash);

    /* USIZE_MAX is the saturation sentinel, and also what a NEGATIVE stored column becomes
     * when a call site casts it to USize - a far-future record either way if it were honoured. */
    test_expect_false(test, "a saturated stored expiry refuses", http_service_auth_token_verify_2(&auth, value, &stored, USIZE_MAX));

    /* The char const* twin: a database cell arrives as a plain string, and should not have to
     * be wrapped in a String just to be compared. Same answers as _2, case for case. */
    test_expect_true(test, "verify_3 accepts the matching candidate", http_service_auth_token_verify_3(&auth, value, string_get_data(&stored), token.expires_at));
    test_expect_false(test, "verify_3 refuses a same-length mismatch", http_service_auth_token_verify_3(&auth, wrong, string_get_data(&stored), token.expires_at));
    test_expect_false(test, "verify_3 refuses a short candidate", http_service_auth_token_verify_3(&auth, "deadbeef", string_get_data(&stored), token.expires_at));
    test_expect_false(test, "verify_3 refuses an empty candidate", http_service_auth_token_verify_3(&auth, "", string_get_data(&stored), token.expires_at));
    test_expect_false(test, "verify_3 refuses an empty stored hash", http_service_auth_token_verify_3(&auth, value, "", token.expires_at));
    test_expect_false(test, "verify_3 refuses an expired record", http_service_auth_token_verify_3(&auth, value, string_get_data(&stored), 1));
    test_expect_false(test, "verify_3 refuses a saturated stored expiry", http_service_auth_token_verify_3(&auth, value, string_get_data(&stored), USIZE_MAX));

    String absent = _string_of("");

    test_expect_false(test, "an empty stored hash refuses", http_service_auth_token_verify_2(&auth, value, &absent, token.expires_at));

    string_uninit(&absent);
    string_uninit(&again);
    string_uninit(&stored);

    http_service_auth_token_uninit(&token);

    http_service_auth_uninit(&auth);

    test_case_end(test);
}

static void _test_decoy_timing(Test *const test) {
    test_case_begin(test, "verify_password_2 pays the KDF on an unknown account");

    /* A real cost, not the suite's usual fast one: what is being pinned is that the empty-hash
     * path RUNS the KDF, and that is only observable when the KDF costs something. */
    HTTP_Service_Auth auth = http_service_auth_init_3(_MIN_LENGTH, _MAX_LENGTH, _DECOY_ITERATIONS, _TOKEN_BYTE_COUNT, _RESET_TTL, _EMAIL_TTL);

    String hash = http_service_auth_hash_password(&auth, "correct horse battery");
    String absent = _string_of("");

    ChronoInstant const known_start = chrono_now();

    test_expect_false(test, "a wrong password against a known account refuses", http_service_auth_verify_password_2(&auth, "wrong horse battery", &hash));

    U64 const known = chrono_duration_microseconds(chrono_elapsed(known_start));

    ChronoInstant const unknown_start = chrono_now();

    test_expect_false(test, "an unknown account refuses", http_service_auth_verify_password_2(&auth, "wrong horse battery", &absent));

    U64 const unknown = chrono_duration_microseconds(chrono_elapsed(unknown_start));

    ChronoInstant const fast_start = chrono_now();

    test_expect_false(test, "verify_password still refuses an empty hash", http_service_auth_verify_password_1(&auth, "wrong horse battery", &absent));

    U64 const fast = chrono_duration_microseconds(chrono_elapsed(fast_start));

    /* A COARSE bound on purpose. The exact ratio is scheduler noise; what must hold is that
     * the unknown-account path is the same ORDER as a real verification and nothing like the
     * microsecond return _1 gives, which is the enumeration oracle this closes. */
    test_expect_true(test, "the unknown-account path costs at least a quarter of a real verify", unknown * 4 >= known);

    /* `unknown >= fast * 8` used to stand here and pinned NOTHING: `fast` measures a
     * microsecond refusal, which reads as 0 us on a fast box, and every U64 is >= 0. Pin the
     * decoy path against an ABSOLUTE floor instead - a 200000-iteration PBKDF2 pass cannot
     * finish inside a millisecond on any machine that runs this suite - and put `fast` on the
     * multiplied side, so the comparison also requires `unknown` to be non-zero. */
    test_expect_true(test, "the unknown-account path really ran a KDF pass", unknown >= _KDF_MINIMUM_MICROSECONDS);
    test_expect_true(test, "and the non-decoy form costs a small fraction of it", fast * 8 < unknown);

    /* verify_password_3 is the same body over a char const* record, so it must pay the decoy
     * too: an empty CELL is the absent-record case exactly as the empty String is. */
    ChronoInstant const unknown_3_start = chrono_now();

    test_expect_false(test, "verify_password_3 refuses an empty cell", http_service_auth_verify_password_3(&auth, "wrong horse battery", ""));

    U64 const unknown_3 = chrono_duration_microseconds(chrono_elapsed(unknown_3_start));

    test_expect_true(test, "and the char const* tier ran the decoy KDF as well", unknown_3 >= _KDF_MINIMUM_MICROSECONDS);

    char over[_MAX_LENGTH + 2] = DEFAULT_INITIALIZATION;

    for (USize index = 0; index + 1 < sizeof(over); index += 1) {
        over[index] = 'a';
    }

    /* The ceiling refusal must land BEFORE any crypto_password_* call, decoy included, so an
     * over-ceiling candidate against this real-cost service answers as fast as the non-decoy
     * refusal above - nowhere near a real KDF pass - on every one of the three call sites the
     * header promises the bound for. */
    ChronoInstant const over_verify2_start = chrono_now();

    test_expect_false(test, "verify_password_2 refuses an over-ceiling password against a known hash", http_service_auth_verify_password_2(&auth, over, &hash));

    U64 const over_verify2 = chrono_duration_microseconds(chrono_elapsed(over_verify2_start));

    ChronoInstant const over_verify2_absent_start = chrono_now();

    test_expect_false(test, "verify_password_2 refuses an over-ceiling password against an unknown account", http_service_auth_verify_password_2(&auth, over, &absent));

    U64 const over_verify2_absent = chrono_duration_microseconds(chrono_elapsed(over_verify2_absent_start));

    ChronoInstant const over_verify_start = chrono_now();

    test_expect_false(test, "verify_password refuses an over-ceiling password", http_service_auth_verify_password_1(&auth, over, &hash));

    U64 const over_verify = chrono_duration_microseconds(chrono_elapsed(over_verify_start));

    ChronoInstant const over_hash_start = chrono_now();

    String over_hash = http_service_auth_hash_password(&auth, over);

    U64 const over_hash_us = chrono_duration_microseconds(chrono_elapsed(over_hash_start));

    test_expect_true(test, "hash_password answers empty for an over-ceiling password", string_empty(&over_hash));

    test_expect_true(test, "verify_password_2 skips the KDF for a known hash too", over_verify2 * 4 < known);
    test_expect_true(test, "verify_password_2 skips the decoy KDF for an unknown account", over_verify2_absent * 4 < known);
    test_expect_true(test, "verify_password skips the KDF", over_verify * 4 < known);
    test_expect_true(test, "hash_password skips the KDF", over_hash_us * 4 < known);

    string_uninit(&over_hash);

    test_expect_true(test, "the right password against a known account still passes", http_service_auth_verify_password_2(&auth, "correct horse battery", &hash));

    string_uninit(&absent);
    string_uninit(&hash);

    http_service_auth_uninit(&auth);

    test_case_end(test);
}

static void _test_uninit_releases_the_decoy(Test *const test) {
    test_case_begin(test, "uninit releases the service-owned decoy");

    HTTP_Service_Auth auth = _auth_fast();

    test_expect_false(test, "the service owns a decoy hash after init", string_empty(&auth.decoy_hash));

    http_service_auth_uninit(&auth);

    test_expect_true(test, "uninit releases it", string_empty(&auth.decoy_hash));
    test_expect_u(test, "and zeroes the ceiling", 0, auth.password_max_length);
    test_expect_u(test, "and the floor", 0, auth.password_min_length);
    test_expect_u(test, "and the iteration count", 0, auth.password_hash_iterations);

    test_case_end(test);
}

static void _test_arena(Test *const test) {
    test_case_begin(test, "arena-backed service");

    Arena arena = arena_init_1(1 << 20, ARENA_TYPE_LINEAR);

    HTTP_Service_Auth auth = http_service_auth_alloc_init_3(_MIN_LENGTH, _MAX_LENGTH, _FAST_ITERATIONS, _TOKEN_BYTE_COUNT, _RESET_TTL, _EMAIL_TTL, &arena);

    test_expect_false(test, "the arena instance owns a decoy too", string_empty(&auth.decoy_hash));

    String hash = http_service_auth_hash_password(&auth, "correct horse battery");

    test_expect_true(test, "an arena hash verifies", http_service_auth_verify_password_2(&auth, "correct horse battery", &hash));

    HTTP_Service_Auth_Token token = http_service_auth_token_create(&auth, 60);

    test_expect_u(test, "an arena token has the same shape", _TOKEN_BYTE_COUNT * 2, string_get_size(&token.value));

    char value[(_TOKEN_BYTE_COUNT * 2) + 1] = DEFAULT_INITIALIZATION;

    char_copy_3(value, sizeof(value), string_get_data(&token.value), string_get_size(&token.value));

    value[string_get_size(&token.value)] = '\0';

    String stored = http_service_auth_token_hash(&auth, value);

    test_expect_true(test, "an arena token verifies against its arena hash", http_service_auth_token_verify_2(&auth, value, &stored, token.expires_at));

    String absent = _string_of("");

    test_expect_false(test, "and an unknown account still refuses", http_service_auth_verify_password_2(&auth, "correct horse battery", &absent));

    string_uninit(&absent);
    string_uninit(&stored);

    http_service_auth_token_uninit(&token);
    string_uninit(&hash);

    http_service_auth_uninit(&auth);

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Raw client
 *============================================================================*/

static char _raw[_CLIENT_RAW_MAX];

static bool _client_open(U16 const port, Net_Socket *const out) {
    Net_Socket_Address address = DEFAULT_INITIALIZATION;

    if (result_is_error(net_socket_address_init_2(NET_FAMILY_IPV4, port, "127.0.0.1", &address))) {
        return false;
    }

    if (result_is_error(net_socket_init(out, NET_FAMILY_IPV4, NET_TYPE_TCP))) {
        return false;
    }

    if (result_is_error(net_socket_connect(*out, &address))) {
        net_socket_close(*out);

        return false;
    }

    net_socket_set_timeout(*out, _CLIENT_IO_TIMEOUT_MS);
    net_socket_set_nodelay(*out, true);

    return true;
}

/** @brief Fire one GET and answer the status code, or 0 when the exchange failed. */
static U16 _client_status(U16 const port, char const *const path) {
    Net_Socket socket = DEFAULT_INITIALIZATION;

    if (!_client_open(port, &socket)) {
        return 0;
    }

    char    request[512]    = DEFAULT_INITIALIZATION;
    I32     written         = snprintf(request, sizeof(request), "GET %s HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n", path);

    if (written <= 0 || (USize) written >= sizeof(request)) {
        net_socket_close(socket);

        return 0;
    }

    USize   sent    = 0;
    Result  result  = net_socket_send_1(socket, (Byte const*) request, (USize) written, &sent);

    if (result_is_error(result) || sent == 0) {
        net_socket_close(socket);

        return 0;
    }

    USize raw_size = 0;

    while (raw_size + 1 < sizeof(_raw)) {
        USize received = 0;

        if (result_is_error(net_socket_recv_1(socket, _raw + raw_size, sizeof(_raw) - 1 - raw_size, &received)) || received == 0) {
            break;
        }

        raw_size += received;
        _raw[raw_size] = '\0';

        if (char_find_slice_5(_raw, raw_size, 0, "\r\n\r\n", 4) != nullptr) {
            break;
        }
    }

    net_socket_close(socket);

    if (raw_size < 12) {
        return 0;
    }

    return (U16) (((_raw[9] - '0') * 100) + ((_raw[10] - '0') * 10) + (_raw[11] - '0'));
}

/*==============================================================================
 * MARK: - Live gate
 *============================================================================*/

/* One route per subject shape. The gate itself is the three-step rule the header documents,
 * IN ORDER: a refused add is a server-side fault, which is 500 - and it is tested first
 * because such a subject is ALSO empty, so an empty-subject test placed ahead of it would
 * answer 401 and tell the client to retry credentials that were never the problem. Then an
 * EMPTY subject proved no identity, which is 401; and a non-empty subject that fails
 * evaluation proved an identity that is not enough, which is 403. */
static void _gate(HTTP_Server_Route *const route, char const *const role) {
    HTTP_Server_Holder *const holder = http_server_route_get_holder(route);

    HTTP_Service_Permission subject = http_service_permission_init_1();

    if (role != nullptr) {
        http_service_permission_role_add(&subject, role);
    }

    HTTP_Service_Permission_Check check = http_service_permission_check_init_1();

    http_service_permission_check_role_add(&check, "admin");

    HTTP_Service_Permission_Verdict const verdict = http_service_permission_evaluate(&subject, &check);

    if (verdict == HTTP_SERVICE_PERMISSION_VERDICT_INCOMPLETE) {
        http_server_response_send_empty(holder->response, HTTP_SERVER_STATUS_CODE_INTERNAL_SERVER_ERROR);
    }
    else if (http_service_permission_empty(&subject)) {
        http_server_response_send_empty(holder->response, HTTP_SERVER_STATUS_CODE_UNAUTHORIZED);
    }
    else if (verdict != HTTP_SERVICE_PERMISSION_VERDICT_ALLOWED) {
        http_server_response_send_empty(holder->response, HTTP_SERVER_STATUS_CODE_FORBIDDEN);
    }
    else {
        http_server_response_send_1(holder->response, "ok", HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_OK);
    }

    http_service_permission_check_uninit(&check);
    http_service_permission_uninit(&subject);
}

static void _route_anonymous(HTTP_Server_Route *const route) {
    _gate(route, nullptr);
}

static void _route_member(HTTP_Server_Route *const route) {
    _gate(route, "guest");
}

static void _route_admin(HTTP_Server_Route *const route) {
    _gate(route, "admin");
}

/* An EMPTY role value is refused by role_add, which marks the subject failed. The resulting
 * subject is empty AND incomplete - the exact shape the ordering above exists for. */
static void _route_incomplete(HTTP_Server_Route *const route) {
    _gate(route, "");
}

static void _handler(void *context, HTTP_Server_Request *request, HTTP_Server_Response *response) {
    HTTP_Server         *const  server  = (HTTP_Server*) context;
    HTTP_Server_Holder          holder  = { .request = request, .response = response, .arena = nullptr };

    if (!http_server_router_dispatch_2(server->router, http_server_request_get_path_1(request), http_server_request_get_path_size(request), &holder)) {
        http_server_response_send_empty(response, HTTP_SERVER_STATUS_CODE_NOT_FOUND);
    }
}

static void _test_gate_over_the_wire(Test *const test) {
    test_case_begin(test, "401 versus 403 on the wire");

    HTTP_Server *server = http_server_new();

    test_expect_true(test, "route_add /gate/anonymous", http_server_route_add(server, "/gate/anonymous", _route_anonymous));
    test_expect_true(test, "route_add /gate/member", http_server_route_add(server, "/gate/member", _route_member));
    test_expect_true(test, "route_add /gate/admin", http_server_route_add(server, "/gate/admin", _route_admin));
    test_expect_true(test, "route_add /gate/incomplete", http_server_route_add(server, "/gate/incomplete", _route_incomplete));

    http_server_set_handler(server, _handler, server);

    Result const result = http_server_run(server, 0, true);

    test_expect_true(test, "http_server_run succeeds on port 0", result_is_success(result));

    U16 const port = http_server_get_port(server);

    test_expect_true(test, "an ephemeral port was assigned", port != 0);

    test_expect_u(test, "no subject at all is 401", 401, _client_status(port, "/gate/anonymous"));
    test_expect_u(test, "a subject that is not enough is 403", 403, _client_status(port, "/gate/member"));
    test_expect_u(test, "a subject that satisfies the check is 200", 200, _client_status(port, "/gate/admin"));
    test_expect_u(test, "a subject whose add was refused is 500, not 401", 500, _client_status(port, "/gate/incomplete"));

    http_server_stop(server);
    http_server_delete(&server);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Entry point
 *============================================================================*/

I32 main(void) {
    LogConfig const log_config = { .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true };

    log_init(log_config);

    /* Route libwebsockets' own diagnostics into the CFW log rather than bare stderr. */
    http_server_set_log_level(LLL_ERR);

    Test test = test_init("http_service_auth");

    test_verbose_set(&test, false);

    test_suite_begin(&test, "http_service_auth offline");

    _test_policy(&test);
    _test_hash_and_verify(&test);
    _test_needs_rehash(&test);
    _test_token_shape(&test);
    _test_token_verify_memory(&test);
    _test_token_hash_and_verify_2(&test);
    _test_decoy_timing(&test);
    _test_uninit_releases_the_decoy(&test);
    _test_arena(&test);

    test_suite_end(&test);

    test_suite_begin(&test, "http_service_auth live");

    _test_gate_over_the_wire(&test);

    test_suite_end(&test);

    return test_uninit(&test);
}