/*
 * test_unchecked.c - the http/service/auth suite, built WITHOUT ERROR_CHECK_ENABLED.
 *
 * Every error_check_* call compiles to nothing here, so a contract violation would sail
 * straight past. That makes this the only place the VALUE-dependent refusals are actually
 * proven: an empty stored hash, a malformed record, a same-length wrong candidate, an expired
 * record and a password past the ceiling must all refuse on their own logic, not because a
 * check aborted the process first.
 *
 * Null pointers are deliberately NOT exercised - those are contract violations whose defence
 * IS error_check_*, and reaching one here would simply crash.
 */
#include <http/service/auth/auth.h>
#include <test/test.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

#define _EMAIL_TTL          86400
#define _FAST_ITERATIONS    1000
#define _MAX_LENGTH         128
#define _MIN_LENGTH         12
#define _RESET_TTL          900
#define _TOKEN_BYTE_COUNT   32

/*==============================================================================
 * MARK: - Helpers
 *============================================================================*/

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
 * MARK: - Cases
 *============================================================================*/

static void _test_password_refusals(Test *const test) {
    test_case_begin(test, "password refusals without the checks armed");

    HTTP_Service_Auth auth = _auth_fast();

    char over[_MAX_LENGTH + 2] = DEFAULT_INITIALIZATION;

    for (USize index = 0; index + 1 < sizeof(over); index += 1) {
        over[index] = 'a';
    }

    test_expect_u(test, "an empty password is still TOO_SHORT", (USize) HTTP_SERVICE_AUTH_PASSWORD_TOO_SHORT, (USize) http_service_auth_password_reason(&auth, ""));
    test_expect_u(test, "an oversize password is still TOO_LONG", (USize) HTTP_SERVICE_AUTH_PASSWORD_TOO_LONG, (USize) http_service_auth_password_reason(&auth, over));
    test_expect_false(test, "and valid agrees", http_service_auth_password_valid(&auth, over));

    String absent = _string_of("");
    String malformed = _string_of("pbkdf2_sha256$abc$def");

    test_expect_false(test, "an empty stored hash refuses", http_service_auth_verify_password_1(&auth, "correct horse battery", &absent));
    test_expect_false(test, "a malformed record refuses", http_service_auth_verify_password_1(&auth, "correct horse battery", &malformed));
    test_expect_false(test, "verify_password_2 refuses an unknown account", http_service_auth_verify_password_2(&auth, "correct horse battery", &absent));
    test_expect_false(test, "verify_password_2 refuses a malformed record", http_service_auth_verify_password_2(&auth, "correct horse battery", &malformed));
    /* The char const* tier's refusals are value-dependent in exactly the same way, and its
     * empty case is a FIRST BYTE rather than a String's emptiness - with the checks compiled
     * away, only its own logic can be refusing. */
    test_expect_false(test, "verify_password_3 refuses an empty cell", http_service_auth_verify_password_3(&auth, "correct horse battery", ""));
    test_expect_false(test, "verify_password_3 refuses a malformed cell", http_service_auth_verify_password_3(&auth, "correct horse battery", string_get_data(&malformed)));
    test_expect_false(test, "an empty record never needs re-hashing", http_service_auth_password_needs_rehash(&auth, &absent));
    test_expect_false(test, "a malformed record never needs re-hashing", http_service_auth_password_needs_rehash(&auth, &malformed));

    // An empty PASSWORD is a legal value, not a contract violation: it hashes and it verifies.
    String empty_password_hash = http_service_auth_hash_password(&auth, "");

    test_expect_false(test, "an empty password still produces a hash", string_empty(&empty_password_hash));
    test_expect_true(test, "and verifies against it", http_service_auth_verify_password_1(&auth, "", &empty_password_hash));
    test_expect_false(test, "while another password does not", http_service_auth_verify_password_1(&auth, "x", &empty_password_hash));

    string_uninit(&empty_password_hash);
    string_uninit(&malformed);
    string_uninit(&absent);

    http_service_auth_uninit(&auth);

    test_case_end(test);
}

static void _test_token_refusals(Test *const test) {
    test_case_begin(test, "token refusals without the checks armed");

    HTTP_Service_Auth auth = _auth_fast();

    HTTP_Service_Auth_Token token = http_service_auth_token_create(&auth, 60);

    char value[(_TOKEN_BYTE_COUNT * 2) + 1] = DEFAULT_INITIALIZATION;
    char wrong[(_TOKEN_BYTE_COUNT * 2) + 1] = DEFAULT_INITIALIZATION;

    char_copy_3(value, sizeof(value), string_get_data(&token.value), string_get_size(&token.value));
    char_copy_3(wrong, sizeof(wrong), string_get_data(&token.value), string_get_size(&token.value));

    value[string_get_size(&token.value)] = '\0';
    wrong[string_get_size(&token.value)] = '\0';
    wrong[0] = wrong[0] == 'a' ? 'b' : 'a';

    test_expect_true(test, "the exact value verifies", http_service_auth_token_verify_1(&token, value));
    test_expect_false(test, "a same-length mismatch refuses", http_service_auth_token_verify_1(&token, wrong));
    test_expect_false(test, "an empty candidate refuses", http_service_auth_token_verify_1(&token, ""));
    test_expect_true(test, "the boundary is inclusive", http_service_auth_token_expired_at(&token, token.expires_at));
    test_expect_false(test, "an expired token verifies nothing", http_service_auth_token_verify_at(&token, value, token.expires_at));

    String stored = http_service_auth_token_hash(&auth, value);
    String absent = _string_of("");

    test_expect_true(test, "the stored hash verifies", http_service_auth_token_verify_2(&auth, value, &stored, token.expires_at));
    test_expect_false(test, "a same-length mismatch refuses against the hash", http_service_auth_token_verify_2(&auth, wrong, &stored, token.expires_at));
    test_expect_false(test, "an empty stored hash refuses", http_service_auth_token_verify_2(&auth, value, &absent, token.expires_at));
    test_expect_false(test, "an expired stored record refuses", http_service_auth_token_verify_2(&auth, value, &stored, 1));

    /* The value-dependent halves of the fail-closed clamp and of the char const* twin, with
     * every error_check_* compiled away: a saturated expiry, an empty candidate, and an empty
     * stored hash are VALUES, so they must refuse without any contract check behind them. */
    test_expect_false(test, "a saturated stored expiry refuses", http_service_auth_token_verify_2(&auth, value, &stored, USIZE_MAX));
    test_expect_false(test, "an empty candidate refuses against the hash", http_service_auth_token_verify_2(&auth, "", &stored, token.expires_at));

    /* The candidate LENGTH gate is a value-dependent refusal too, so it belongs here: the
     * empty candidate refuses even against the SHA-256 of the empty string - the row it would
     * otherwise verify against - and a wrong-length non-empty candidate refuses against the
     * real stored hash. token_hash carries the same gate, which is what stops that row from
     * being written at all. */
    test_expect_false(
        test, "an empty candidate refuses against sha256(\"\") itself",
        http_service_auth_token_verify_3(&auth, "", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", token.expires_at));

    char short_candidate[sizeof(value)] = DEFAULT_INITIALIZATION;

    char_copy_3(short_candidate, sizeof(short_candidate), value, string_get_size(&token.value) - 1);

    short_candidate[string_get_size(&token.value) - 1] = '\0';

    test_expect_false(test, "a candidate one byte short refuses", http_service_auth_token_verify_2(&auth, short_candidate, &stored, token.expires_at));

    String empty_hash = http_service_auth_token_hash(&auth, "");
    String short_hash = http_service_auth_token_hash(&auth, short_candidate);

    test_expect_true(test, "token_hash refuses the empty token", string_empty(&empty_hash));
    test_expect_true(test, "token_hash refuses a wrong-length token", string_empty(&short_hash));

    string_uninit(&short_hash);
    string_uninit(&empty_hash);

    test_expect_true(test, "verify_3 accepts the stored hash as a plain string", http_service_auth_token_verify_3(&auth, value, string_get_data(&stored), token.expires_at));
    test_expect_false(test, "verify_3 refuses a same-length mismatch", http_service_auth_token_verify_3(&auth, wrong, string_get_data(&stored), token.expires_at));
    test_expect_false(test, "verify_3 refuses an empty stored hash", http_service_auth_token_verify_3(&auth, value, "", token.expires_at));
    test_expect_false(test, "verify_3 refuses a saturated stored expiry", http_service_auth_token_verify_3(&auth, value, string_get_data(&stored), USIZE_MAX));

    HTTP_Service_Auth_Token saturated = http_service_auth_token_create(&auth, USIZE_MAX);

    test_expect_u(test, "an overflowing ttl saturates", USIZE_MAX, saturated.expires_at);
    test_expect_true(test, "and a saturated expiry is expired at now = 0", http_service_auth_token_expired_at(&saturated, 0));

    http_service_auth_token_uninit(&saturated);

    string_uninit(&absent);
    string_uninit(&stored);

    http_service_auth_token_uninit(&token);

    http_service_auth_uninit(&auth);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Entry point
 *============================================================================*/

I32 main(void) {
    LogConfig const log_config = { .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true };

    log_init(log_config);

    Test test = test_init("http_service_auth unchecked");

    test_verbose_set(&test, false);

    test_suite_begin(&test, "http_service_auth unchecked");

    _test_password_refusals(&test);
    _test_token_refusals(&test);

    test_suite_end(&test);

    return test_uninit(&test);
}