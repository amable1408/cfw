/*
 * test_all.c - Loopback-fixture suite for the CFW http/service/oauth module.
 *
 * The old suite (tests/service/oauth/) could only see registration, lookup and teardown,
 * because every other public function performs a live HTTPS call. It said so, and it was
 * right at the time: "a network round trip is not a unit test, and stubbing it would only
 * assert that the stub was called."
 *
 * That is no longer the only choice. tests/http/client/fixture.h runs a real HTTP/1.1 server
 * on 127.0.0.1 (OS-assigned port) on its own CFW thread and now takes a scripted response body
 * and Content-Type, so the module's own http_client can be pointed at it and the ENTIRE wire
 * contract pinned without a provider: the exact form body it builds, the headers it sends, the
 * fields it parses back, and every failure shape.
 *
 * Pinned here:
 *   - exchange: exact body (grant_type/code/client_id/client_secret/redirect_uri, encoded),
 *     Content-Type and Accept headers, the client secret ABSENT from the request path;
 *   - 200 JSON -> six fields + expires_in/expires_at; 400 invalid_grant -> empty access token
 *     with error and error_description; 5xx HTML -> empty, status recorded, no crash;
 *   - STALL -> the exchange returns inside the client's own budget AND a second thread's
 *     get_authorize_url does NOT block behind it (this is what pins Crit 1's snapshot);
 *   - a closed port -> UNREACHABLE or TIMEOUT, never a hang;
 *   - profile: Authorization: Bearer on the wire, 401 -> empty profile with the status, a
 *     NUMERIC id rendered as decimal (GitHub's shape);
 *   - refresh and revoke bodies, and revoke's 200 / 400 / closed-port answers;
 *   - the exact authorize URL, with reserved characters and an authorize endpoint that already
 *     carries a query;
 *   - state issue/verify/tamper/expiry/cross-provider, and the RFC 7636 Appendix B PKCE vector;
 *   - the registration, independence and teardown cases the old suite carried.
 *
 * No network beyond loopback; every port is OS-assigned; every wait is timeout-bounded.
 */
#include <stdio.h>

#include <chrono/chrono.h>
#include <http/service/oauth/oauth.h>
#include <test/test.h>

#include <fixture.h>

/*==============================================================================
 * MARK: - Helpers
 *============================================================================*/

static void _url_for(U16 const port, char const *const path, char *const out, USize const out_capacity) {
    snprintf(out, out_capacity, "http://127.0.0.1:%u%s", (unsigned) port, path);
}

static bool _start(Test *const test, Fixture_Server *const server) {
    return test_expect_true(test, "fixture started", fixture_server_start(server));
}

static bool _contains(char const *const haystack, char const *const needle) {
    USize const haystack_size = char_length(haystack);

    if (haystack_size == 0) {
        return false;
    }

    String subject = string_init_1();

    string_add_last_2(&subject, haystack, haystack_size);

    bool const found = string_find_1(&subject, 0, needle) != USIZE_MAX;

    string_uninit(&subject);

    return found;
}

/* A Token the caller builds by hand, since get_profile takes one but only ever reads its
 * access_token. Every String member has to exist, because profile teardown uninits them all. */
static HTTP_Service_OAuth_Token _token_with_access(char const *const access_token) {
    HTTP_Service_OAuth_Token token = DEFAULT_INITIALIZATION;

    token.access_token      = string_init_1();
    token.error             = string_init_1();
    token.error_description = string_init_1();
    token.id_token          = string_init_1();
    token.raw               = string_init_1();
    token.refresh_token     = string_init_1();
    token.scope             = string_init_1();
    token.token_type        = string_init_1();

    string_add_last_2(&token.access_token, access_token, char_length(access_token));

    return token;
}

static bool _body_contains(String const *const body, char const *const needle) {
    return string_get_size(body) > 0 && string_find_1(body, 0, needle) != USIZE_MAX;
}

// Registers one provider whose token/profile/revoke endpoints all point at `port`.
static bool _register_local(HTTP_Service_OAuth *const oauth, char const *const name, U16 const port) {
    static char token_url[64]   = DEFAULT_INITIALIZATION;
    static char profile_url[64] = DEFAULT_INITIALIZATION;
    static char revoke_url[64]  = DEFAULT_INITIALIZATION;

    _url_for(port, "/token", token_url, sizeof(token_url));
    _url_for(port, "/profile", profile_url, sizeof(profile_url));
    _url_for(port, "/revoke", revoke_url, sizeof(revoke_url));

    HTTP_Service_OAuth_Provider const provider = {
        .authorize_url  = "https://example.invalid/authorize",
        .client_id      = "client-id",
        .client_secret  = "s3cr3t/value",
        .name           = name,
        .profile_url    = profile_url,
        .redirect_uri   = "https://example.invalid/callback?x=1",
        .revoke_url     = revoke_url,
        .scope          = "openid email",
        .token_url      = token_url
    };

    return http_service_oauth_provider_add_1(oauth, &provider);
}

static HTTP_Service_OAuth_Provider _provider(char const *const name) {
    return (HTTP_Service_OAuth_Provider){
        .authorize_url  = "https://example.invalid/authorize",
        .client_id      = "client-id",
        .client_secret  = "client-secret",
        .name           = name,
        .profile_url    = "https://example.invalid/profile",
        .redirect_uri   = "https://example.invalid/callback",
        .revoke_url     = "https://example.invalid/revoke",
        .scope          = "openid email",
        .token_url      = "https://example.invalid/token"
    };
}

/*==============================================================================
 * MARK: - Cases - registration
 *============================================================================*/

static void _test_provider_registration(Test *const test) {
    test_case_begin(test, "oauth registers and finds providers");

    HTTP_Service_OAuth oauth = DEFAULT_INITIALIZATION;

    test_expect_true(test, "the service initializes", http_service_oauth_init_1(&oauth));

    HTTP_Service_OAuth_Provider const google = _provider("google");
    HTTP_Service_OAuth_Provider const github = _provider("github");

    test_expect_true(test, "the first provider registers", http_service_oauth_provider_add_1(&oauth, &google));
    test_expect_true(test, "the second provider registers", http_service_oauth_provider_add_1(&oauth, &github));

    /* One row per provider now, but the property is unchanged and still worth pinning:
     * registering a second provider must not disturb the first. Building each authorize URL is
     * what proves it - a skew would splice one provider's client_id onto another's endpoint. */
    String google_url = http_service_oauth_get_authorize_url_1(&oauth, "google", "state-1");
    String github_url = http_service_oauth_get_authorize_url_1(&oauth, "github", "state-2");

    test_expect_true(test, "the first provider builds a url", string_get_size(&google_url) > 0);
    test_expect_true(test, "the second provider builds a url", string_get_size(&github_url) > 0);
    test_expect_true(test, "each url carries its own state", string_find_1(&google_url, 0, "state-1") != USIZE_MAX);
    test_expect_true(test, "and the second its own", string_find_1(&github_url, 0, "state-2") != USIZE_MAX);

    String missing_url = http_service_oauth_get_authorize_url_1(&oauth, "gitlab", "state-3");

    test_expect_true(test, "an unknown provider yields an empty url", string_get_size(&missing_url) == 0);

    string_uninit(&google_url);
    string_uninit(&github_url);
    string_uninit(&missing_url);

    http_service_oauth_uninit(&oauth);

    test_case_end(test);
}

static void _test_two_instances_are_independent(Test *const test) {
    test_case_begin(test, "oauth instances are independent");

    /* The mutex used to be file-static: shared by every instance, re-initialized by each init
     * and destroyed by the first uninit - so tearing one service down left any other locking a
     * destroyed mutex. Each instance now carries its own lock. */
    HTTP_Service_OAuth first  = DEFAULT_INITIALIZATION;
    HTTP_Service_OAuth second = DEFAULT_INITIALIZATION;

    test_expect_true(test, "the first service initializes", http_service_oauth_init_1(&first));
    test_expect_true(test, "the second service initializes", http_service_oauth_init_1(&second));

    HTTP_Service_OAuth_Provider const google = _provider("google");

    http_service_oauth_provider_add_1(&first, &google);
    http_service_oauth_uninit(&first);

    HTTP_Service_OAuth_Provider const github = _provider("github");

    http_service_oauth_provider_add_1(&second, &github);

    String url = http_service_oauth_get_authorize_url_1(&second, "github", "state-x");

    test_expect_true(test, "the survivor still builds a url after the other is destroyed", string_get_size(&url) > 0);

    string_uninit(&url);

    String foreign = http_service_oauth_get_authorize_url_1(&second, "google", "state-y");

    test_expect_true(test, "and it does not know the other instance's provider", string_get_size(&foreign) == 0);

    string_uninit(&foreign);

    http_service_oauth_uninit(&second);

    test_case_end(test);
}

static void _test_provider_update_replaces_in_place(Test *const test) {
    test_case_begin(test, "re-registering a name updates that provider without adding a row");

    HTTP_Service_OAuth oauth = DEFAULT_INITIALIZATION;

    http_service_oauth_init_1(&oauth);

    HTTP_Service_OAuth_Provider const google = _provider("google");
    HTTP_Service_OAuth_Provider const github = _provider("github");

    test_expect_true(test, "the first registration succeeds", http_service_oauth_provider_add_1(&oauth, &google));
    test_expect_true(test, "a second, different provider registers", http_service_oauth_provider_add_1(&oauth, &github));

    HTTP_Service_OAuth_Provider updated = _provider("google");

    updated.authorize_url = "https://updated.invalid/authorize";
    updated.client_id     = "updated-client-id";

    test_expect_true(test, "re-registering the same name succeeds", http_service_oauth_provider_add_1(&oauth, &updated));

    String updated_url = http_service_oauth_get_authorize_url_1(&oauth, "google", "state-1");

    test_expect_true(test, "the url now carries the NEW endpoint", string_find_1(&updated_url, 0, "updated.invalid") != USIZE_MAX);
    test_expect_true(test, "and the NEW client id", string_find_1(&updated_url, 0, "updated-client-id") != USIZE_MAX);
    test_expect_true(test, "the old endpoint is gone", string_find_1(&updated_url, 0, "example.invalid/authorize") == USIZE_MAX);

    String github_url = http_service_oauth_get_authorize_url_1(&oauth, "github", "state-2");

    test_expect_true(test, "the untouched provider still builds its own url", string_find_1(&github_url, 0, "example.invalid/authorize") != USIZE_MAX);
    test_expect_true(test, "and still carries its own state", string_find_1(&github_url, 0, "state-2") != USIZE_MAX);

    string_uninit(&updated_url);
    string_uninit(&github_url);

    http_service_oauth_uninit(&oauth);

    test_case_end(test);
}

static void _test_optional_and_required_fields(Test *const test) {
    test_case_begin(test, "revoke_url and scope are optional; a missing required field is REFUSED, not aborted");

    HTTP_Service_OAuth oauth = DEFAULT_INITIALIZATION;

    http_service_oauth_init_1(&oauth);

    HTTP_Service_OAuth_Provider lean = _provider("github");

    lean.revoke_url = nullptr;
    lean.scope      = "";

    test_expect_true(test, "a provider with no revoke endpoint and no scope registers", http_service_oauth_provider_add_1(&oauth, &lean));

    String url = http_service_oauth_get_authorize_url_1(&oauth, "github", "st");

    // An empty scope is OMITTED, not sent as "&scope=" - which asks for no scopes at all.
    test_expect_true(test, "an empty scope is omitted from the url entirely", string_find_1(&url, 0, "scope=") == USIZE_MAX);

    string_uninit(&url);

    /* This is the case that used to ABORT the process. main_traymon guarded emptiness at the
     * call site precisely because an empty TRAYMON_OAUTH_..._SECRET= reached
     * error_check_non_value_uint and killed the server at boot. */
    HTTP_Service_OAuth_Provider broken = _provider("broken");

    broken.client_secret = "";

    test_expect_false(test, "an empty client_secret is refused, not aborted", http_service_oauth_provider_add_1(&oauth, &broken));

    HTTP_Service_OAuth_Provider missing = _provider("missing");

    missing.token_url = nullptr;

    test_expect_false(test, "a null required field is refused, not aborted", http_service_oauth_provider_add_1(&oauth, &missing));

    String broken_url = http_service_oauth_get_authorize_url_1(&oauth, "broken", "st");

    test_expect_u(test, "the refused provider was not stored at all", 0, string_get_size(&broken_url));

    string_uninit(&broken_url);

    http_service_oauth_uninit(&oauth);

    test_case_end(test);
}

static void _test_uninit_without_use(Test *const test) {
    test_case_begin(test, "oauth uninit without a single provider");

    /* A service that never registered anything never allocated its row array, so `rows` is
     * null on teardown. That pointer goes to allocator_release, which treats null as a no-op -
     * a server that started and stopped without reaching an OAuth route must not die on exit. */
    HTTP_Service_OAuth oauth = DEFAULT_INITIALIZATION;

    http_service_oauth_init_1(&oauth);

    String url = http_service_oauth_get_authorize_url_1(&oauth, "google", "state-1");

    test_expect_true(test, "a fresh service resolves no provider", string_get_size(&url) == 0);

    string_uninit(&url);

    http_service_oauth_uninit(&oauth);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases - authorize URL
 *============================================================================*/

static void _test_authorize_url_exact(Test *const test) {
    test_case_begin(test, "the authorize url is exact, encoded, and honours an existing query");

    HTTP_Service_OAuth oauth = DEFAULT_INITIALIZATION;

    http_service_oauth_init_1(&oauth);

    HTTP_Service_OAuth_Provider reserved = _provider("google");

    reserved.authorize_url = "https://example.invalid/authorize";
    reserved.client_id     = "id/with+reserved chars";
    reserved.redirect_uri  = "https://example.invalid/cb?a=b&c=d";
    reserved.scope         = "openid email profile";

    http_service_oauth_provider_add_1(&oauth, &reserved);

    String url = http_service_oauth_get_authorize_url_1(&oauth, "google", "st/ate value");

    test_expect_string(test,
        "the whole url, byte for byte",
        "https://example.invalid/authorize?response_type=code&client_id=id%2Fwith%2Breserved%20chars"
        "&redirect_uri=https%3A%2F%2Fexample.invalid%2Fcb%3Fa%3Db%26c%3Dd"
        "&scope=openid%20email%20profile&state=st%2Fate%20value",
        string_get_data(&url));

    string_uninit(&url);

    /* An authorize endpoint that ALREADY carries a query must get "&", not a second "?" -
     * otherwise the whole parameter list becomes one opaque value of the first parameter. */
    HTTP_Service_OAuth_Provider queried = _provider("tenant");

    queried.authorize_url = "https://example.invalid/authorize?tenant=acme";
    queried.scope         = "";

    http_service_oauth_provider_add_1(&oauth, &queried);

    String queried_url = http_service_oauth_get_authorize_url_1(&oauth, "tenant", "s");

    test_expect_string(test,
        "an existing query gets & rather than a second ?",
        "https://example.invalid/authorize?tenant=acme&response_type=code&client_id=client-id"
        "&redirect_uri=https%3A%2F%2Fexample.invalid%2Fcallback&state=s",
        string_get_data(&queried_url));

    string_uninit(&queried_url);

    // PKCE and the provider extras Google needs before it will ever issue a refresh token.
    String pkce_url = http_service_oauth_get_authorize_url_2(&oauth, "tenant", "s", "CHALLENGE-x_y", "access_type=offline&prompt=consent");

    test_expect_true(test, "the challenge is present", string_find_1(&pkce_url, 0, "&code_challenge=CHALLENGE-x_y") != USIZE_MAX);
    test_expect_true(test, "with its method", string_find_1(&pkce_url, 0, "&code_challenge_method=S256") != USIZE_MAX);
    test_expect_true(test, "and the extra params are appended verbatim", string_find_1(&pkce_url, 0, "&access_type=offline&prompt=consent") != USIZE_MAX);

    string_uninit(&pkce_url);

    http_service_oauth_uninit(&oauth);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases - state and PKCE
 *============================================================================*/

static void _test_state_issue_and_verify(Test *const test) {
    test_case_begin(test, "state: issue -> verify, tampered, expired, cross-provider");

    HTTP_Service_OAuth oauth = DEFAULT_INITIALIZATION;

    http_service_oauth_init_1(&oauth);

    char state[HTTP_SERVICE_OAUTH_STATE_MAX_SIZE + 1] = DEFAULT_INITIALIZATION;

    test_expect_true(test, "a state token is issued", http_service_oauth_state_issue(&oauth, "google", 600, state));
    test_expect_true(test, "it verifies for the provider it was issued for", http_service_oauth_state_verify(&oauth, "google", state));

    /* The provider is INSIDE the MAC, so a state minted for one provider cannot be replayed
     * onto another's callback - which is where the code would otherwise be redeemed. */
    test_expect_false(test, "it does NOT verify for a different provider", http_service_oauth_state_verify(&oauth, "github", state));

    char tampered[HTTP_SERVICE_OAUTH_STATE_MAX_SIZE + 1] = DEFAULT_INITIALIZATION;

    memory_copy_2(tampered, sizeof(tampered), state, char_length(state) + 1);

    tampered[0] = tampered[0] == 'a' ? 'b' : 'a';

    test_expect_false(test, "a tampered nonce fails", http_service_oauth_state_verify(&oauth, "google", tampered));

    memory_copy_2(tampered, sizeof(tampered), state, char_length(state) + 1);

    tampered[char_length(tampered) - 1] = tampered[char_length(tampered) - 1] == 'a' ? 'b' : 'a';

    test_expect_false(test, "a tampered mac fails", http_service_oauth_state_verify(&oauth, "google", tampered));

    test_expect_false(test, "a malformed token fails", http_service_oauth_state_verify(&oauth, "google", "not-a-state"));
    test_expect_false(test, "an empty token fails", http_service_oauth_state_verify(&oauth, "google", ""));

    /* A one-second TTL, verified after it has elapsed. Signed correctly and still refused,
     * which is the expiry check and not the MAC check. */
    char expiring[HTTP_SERVICE_OAUTH_STATE_MAX_SIZE + 1] = DEFAULT_INITIALIZATION;

    test_expect_true(test, "a one-second token is issued", http_service_oauth_state_issue(&oauth, "google", 1, expiring));

    ChronoInstant const start = chrono_now();

    while (chrono_duration_milliseconds(chrono_elapsed(start)) < 2100) {
        thread_sleep(50);
    }

    test_expect_false(test, "and is refused once its ttl has passed", http_service_oauth_state_verify(&oauth, "google", expiring));

    test_expect_false(test, "a zero ttl is refused up front", http_service_oauth_state_issue(&oauth, "google", 0, state));

    /* A ttl within reach of USIZE_MAX wraps "now + ttl_seconds" into a SMALL expiry, so the
     * token is born already expired and every verify fails - a silently broken login rather
     * than a refused configuration. Refusing it at the ISSUE end is also what makes a 20-digit
     * expiry unmintable, and with it the verify side's char_to_numbers_uint_2 wrap. */
    test_expect_false(test, "a ttl of USIZE_MAX is refused rather than wrapped", http_service_oauth_state_issue(&oauth, "google", USIZE_MAX, state));
    test_expect_false(test, "and so is one that only just wraps", http_service_oauth_state_issue(&oauth, "google", USIZE_MAX - 10, state));

    /* The other side of the boundary, so the guard cannot pass by refusing everything: the
     * largest ttl that does NOT wrap still issues a verifiable token, 20-digit expiry and all.
     * The 60-second cushion keeps the clock ticking between the two calls from deciding it. */
    USize const largest_ttl = USIZE_MAX - (USize) datetime_now() - 60;
    char        limit[HTTP_SERVICE_OAUTH_STATE_MAX_SIZE + 1] = DEFAULT_INITIALIZATION;

    test_expect_true(test, "a ttl right up against the wrap still issues", http_service_oauth_state_issue(&oauth, "google", largest_ttl, limit));
    test_expect_true(test, "and the token it minted verifies", http_service_oauth_state_verify(&oauth, "google", limit));

    /* Two services hold two different generated keys, so one cannot mint state the other
     * accepts - which is exactly why a multi-process deployment needs init_2. */
    HTTP_Service_OAuth other = DEFAULT_INITIALIZATION;

    http_service_oauth_init_1(&other);

    char foreign[HTTP_SERVICE_OAUTH_STATE_MAX_SIZE + 1] = DEFAULT_INITIALIZATION;

    http_service_oauth_state_issue(&other, "google", 600, foreign);

    test_expect_false(test, "state from another instance's key does not verify", http_service_oauth_state_verify(&oauth, "google", foreign));

    // With a SHARED explicit key it does, which is the whole point of init_2.
    U8 const key[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
    HTTP_Service_OAuth shared_a = DEFAULT_INITIALIZATION;
    HTTP_Service_OAuth shared_b = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 accepts an explicit key", http_service_oauth_init_2(&shared_a, key, sizeof(key)));
    test_expect_true(test, "and so does its twin", http_service_oauth_init_2(&shared_b, key, sizeof(key)));

    char shared_state[HTTP_SERVICE_OAUTH_STATE_MAX_SIZE + 1] = DEFAULT_INITIALIZATION;

    http_service_oauth_state_issue(&shared_a, "google", 600, shared_state);

    test_expect_true(test, "state crosses two instances sharing one key", http_service_oauth_state_verify(&shared_b, "google", shared_state));

    HTTP_Service_OAuth refused = DEFAULT_INITIALIZATION;

    test_expect_false(test, "a zero-size key is refused", http_service_oauth_init_2(&refused, key, 0));
    test_expect_false(test, "an oversized key is refused", http_service_oauth_init_2(&refused, key, HTTP_SERVICE_OAUTH_STATE_KEY_SIZE + 1));

    http_service_oauth_uninit(&shared_a);
    http_service_oauth_uninit(&shared_b);
    http_service_oauth_uninit(&other);
    http_service_oauth_uninit(&oauth);

    test_case_end(test);
}

static void _test_pkce_rfc7636_vector(Test *const test) {
    test_case_begin(test, "PKCE S256 matches the RFC 7636 Appendix B vector");

    char challenge[HTTP_SERVICE_OAUTH_PKCE_CHALLENGE_SIZE + 1] = DEFAULT_INITIALIZATION;

    /* RFC 7636 Appendix B, verbatim. A hand-rolled S256 that base64s the HEX digest, or pads
     * the output, or uses the standard alphabet, all still "produce a challenge" - only the
     * published vector says whether a real provider would accept it. */
    test_expect_true(test,
        "the appendix B verifier derives a challenge",
        http_service_oauth_pkce_challenge_create("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk", challenge));

    test_expect_string(test, "and it is the published value", "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM", challenge);

    char verifier[HTTP_SERVICE_OAUTH_PKCE_VERIFIER_SIZE + 1] = DEFAULT_INITIALIZATION;

    test_expect_true(test, "a verifier is generated", http_service_oauth_pkce_verifier_create(verifier));
    test_expect_u(test, "at the RFC's own length", HTTP_SERVICE_OAUTH_PKCE_VERIFIER_SIZE, char_length(verifier));

    char generated_challenge[HTTP_SERVICE_OAUTH_PKCE_CHALLENGE_SIZE + 1] = DEFAULT_INITIALIZATION;

    test_expect_true(test, "a generated verifier derives a challenge", http_service_oauth_pkce_challenge_create(verifier, generated_challenge));
    test_expect_u(test, "of the challenge length", HTTP_SERVICE_OAUTH_PKCE_CHALLENGE_SIZE, char_length(generated_challenge));
    test_expect_true(test, "the challenge is not the verifier", !_contains(generated_challenge, verifier));

    char second[HTTP_SERVICE_OAUTH_PKCE_VERIFIER_SIZE + 1] = DEFAULT_INITIALIZATION;

    http_service_oauth_pkce_verifier_create(second);

    test_expect_true(test, "two verifiers differ", !char_compare_equal_1(verifier, second));

    // RFC 7636 4.1 bounds, refused by value.
    test_expect_false(test, "a too-short verifier is refused", http_service_oauth_pkce_challenge_create("short", challenge));

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases - token exchange over the fixture
 *============================================================================*/

static void _test_exchange_body_and_headers(Test *const test) {
    test_case_begin(test, "exchange: exact form body, Accept/Content-Type headers, secret never in the path");

    Fixture_Server server = DEFAULT_INITIALIZATION;

    server.script                = FIXTURE_SCRIPT_OK;
    server.response_body         = "{\"access_token\":\"at-1\",\"refresh_token\":\"rt-1\",\"id_token\":\"idt-1\","
                                   "\"token_type\":\"Bearer\",\"scope\":\"openid email\",\"expires_in\":3600}";
    server.response_content_type = "application/json";

    if (!_start(test, &server)) {
        test_case_end(test);

        return;
    }

    HTTP_Service_OAuth oauth = DEFAULT_INITIALIZATION;

    http_service_oauth_init_1(&oauth);

    test_expect_true(test, "the local provider registers", _register_local(&oauth, "google", fixture_server_port(&server)));

    HTTP_Service_OAuth_Token token = http_service_oauth_exchange_code_1(&oauth, "google", "the code/value");

    fixture_server_join(&server);

    test_expect_string(test, "the exchange is a POST", "POST", server.request_method);
    test_expect_string(test, "to the token path", "/token", server.request_path);

    /* The exact body, in the order the module writes it. Form encoding, so a space is "+" and
     * a slash is "%2F" - the wire convention, not the URL one. */
    String const body = server.request_body;

    test_expect_true(test, "grant_type first", _body_contains(&body, "grant_type=authorization_code"));
    test_expect_true(test, "the code, form-encoded", _body_contains(&body, "&code=the+code%2Fvalue"));
    test_expect_true(test, "the client id", _body_contains(&body, "&client_id=client-id"));
    test_expect_true(test, "the client secret, form-encoded", _body_contains(&body, "&client_secret=s3cr3t%2Fvalue"));
    test_expect_true(test, "the redirect uri, form-encoded", _body_contains(&body, "&redirect_uri=https%3A%2F%2Fexample.invalid%2Fcallback%3Fx%3D1"));

    test_expect_true(test, "Content-Type is the form type", _contains(server.request_headers, "Content-Type: application/x-www-form-urlencoded"));

    /* GitHub's token endpoint answers form-encoded without this header, which json_from_4
     * cannot parse - the exchange then "succeeded" with an empty access token. */
    test_expect_true(test, "Accept: application/json is sent", _contains(server.request_headers, "Accept: application/json"));

    // THE SECRET NEVER TOUCHES THE REQUEST LINE. It belongs in the body and nowhere else.
    test_expect_true(test, "the client secret is absent from the path", !_contains(server.request_path, "s3cr3t"));
    test_expect_true(test, "and absent from the header block", !_contains(server.request_headers, "s3cr3t"));

    // 200 JSON -> six fields.
    test_expect_true(test, "the exchange reports ok", http_service_oauth_token_is_ok(&token));
    test_expect_string(test, "access_token", "at-1", string_get_data(&token.access_token));
    test_expect_string(test, "refresh_token", "rt-1", string_get_data(&token.refresh_token));
    test_expect_string(test, "id_token", "idt-1", string_get_data(&token.id_token));
    test_expect_string(test, "token_type", "Bearer", string_get_data(&token.token_type));
    test_expect_string(test, "scope", "openid email", string_get_data(&token.scope));
    test_expect_u(test, "expires_in", 3600, token.expires_in);
    test_expect_true(test, "expires_at is an absolute deadline in the future", token.expires_at > (USize) datetime_now());
    test_expect_u(test, "response_code", 200, token.response_code);
    test_expect_true(test, "status is OK", token.status == HTTP_CLIENT_STATUS_OK);
    test_expect_true(test, "no error was reported", string_empty(&token.error));

    http_service_oauth_token_uninit(&token);
    http_service_oauth_uninit(&oauth);
    string_uninit(&server.request_body);

    test_case_end(test);
}

static void _test_exchange_with_pkce_verifier(Test *const test) {
    test_case_begin(test, "exchange_2 puts the PKCE verifier in the body");

    Fixture_Server server = DEFAULT_INITIALIZATION;

    server.script                = FIXTURE_SCRIPT_OK;
    server.response_body         = "{\"access_token\":\"at\"}";
    server.response_content_type = "application/json";

    if (!_start(test, &server)) {
        test_case_end(test);

        return;
    }

    HTTP_Service_OAuth oauth = DEFAULT_INITIALIZATION;

    http_service_oauth_init_1(&oauth);
    _register_local(&oauth, "google", fixture_server_port(&server));

    HTTP_Service_OAuth_Token token = http_service_oauth_exchange_code_2(&oauth, "google", "c", "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk");

    fixture_server_join(&server);

    test_expect_true(test, "code_verifier is sent", _body_contains(&server.request_body, "&code_verifier=dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"));

    http_service_oauth_token_uninit(&token);
    http_service_oauth_uninit(&oauth);
    string_uninit(&server.request_body);

    test_case_end(test);
}

static void _test_exchange_400_invalid_grant(Test *const test) {
    test_case_begin(test, "exchange: 400 invalid_grant surfaces error and error_description");

    Fixture_Server server = DEFAULT_INITIALIZATION;

    server.script                = FIXTURE_SCRIPT_STATUS;
    server.status_code           = 400;
    server.response_body         = "{\"error\":\"invalid_grant\",\"error_description\":\"Code was already redeemed\"}";
    server.response_content_type = "application/json";

    if (!_start(test, &server)) {
        test_case_end(test);

        return;
    }

    HTTP_Service_OAuth oauth = DEFAULT_INITIALIZATION;

    http_service_oauth_init_1(&oauth);
    _register_local(&oauth, "google", fixture_server_port(&server));

    HTTP_Service_OAuth_Token token = http_service_oauth_exchange_code_1(&oauth, "google", "used-code");

    fixture_server_join(&server);

    /* Before Token carried a status, this was indistinguishable from "unreachable", "5xx" and
     * "parsed fine but no access_token" - every one of them an empty access token. */
    test_expect_false(test, "the exchange is not ok", http_service_oauth_token_is_ok(&token));
    test_expect_u(test, "the http status is recorded", 400, token.response_code);
    test_expect_true(test, "the transport itself succeeded", token.status == HTTP_CLIENT_STATUS_OK);
    test_expect_string(test, "the provider's error code", "invalid_grant", string_get_data(&token.error));
    test_expect_string(test, "and its description", "Code was already redeemed", string_get_data(&token.error_description));
    test_expect_true(test, "with no access token", string_empty(&token.access_token));

    http_service_oauth_token_uninit(&token);
    http_service_oauth_uninit(&oauth);
    string_uninit(&server.request_body);

    test_case_end(test);
}

static void _test_exchange_5xx_html(Test *const test) {
    test_case_begin(test, "exchange: a 500 HTML page parses to nothing and reports the status");

    Fixture_Server server = DEFAULT_INITIALIZATION;

    server.script                = FIXTURE_SCRIPT_STATUS;
    server.status_code           = 503;
    server.response_body         = "<html><head><title>503</title></head><body>Service Unavailable</body></html>";
    server.response_content_type = "text/html";

    if (!_start(test, &server)) {
        test_case_end(test);

        return;
    }

    HTTP_Service_OAuth oauth = DEFAULT_INITIALIZATION;

    http_service_oauth_init_1(&oauth);
    _register_local(&oauth, "google", fixture_server_port(&server));

    HTTP_Service_OAuth_Token token = http_service_oauth_exchange_code_1(&oauth, "google", "c");

    fixture_server_join(&server);

    test_expect_false(test, "not ok", http_service_oauth_token_is_ok(&token));
    test_expect_u(test, "the status is recorded", 503, token.response_code);
    test_expect_true(test, "no access token", string_empty(&token.access_token));
    test_expect_true(test, "and no invented error code", string_empty(&token.error));
    test_expect_true(test, "the raw body is still available for logging", string_get_size(&token.raw) > 0);

    http_service_oauth_token_uninit(&token);
    http_service_oauth_uninit(&oauth);
    string_uninit(&server.request_body);

    test_case_end(test);
}

static void _test_exchange_closed_port(Test *const test) {
    test_case_begin(test, "exchange: a closed port answers UNREACHABLE or TIMEOUT, never a hang");

    Fixture_Server server = DEFAULT_INITIALIZATION;

    server.script = FIXTURE_SCRIPT_OK;

    if (!_start(test, &server)) {
        test_case_end(test);

        return;
    }

    U16 const port = fixture_server_port(&server);

    fixture_server_join(&server);
    string_uninit(&server.request_body);

    HTTP_Service_OAuth oauth = DEFAULT_INITIALIZATION;

    http_service_oauth_init_1(&oauth);
    _register_local(&oauth, "google", port);

    HTTP_Service_OAuth_Token token = http_service_oauth_exchange_code_1(&oauth, "google", "c");

    /* Either is correct and which one happens is the platform's business: a RST gives
     * UNREACHABLE, a dropped SYN runs out the connect budget and gives TIMEOUT. */
    test_expect_true(test,
        "status is UNREACHABLE or TIMEOUT",
        token.status == HTTP_CLIENT_STATUS_UNREACHABLE || token.status == HTTP_CLIENT_STATUS_TIMEOUT);

    test_expect_u(test, "with no http status at all", 0, token.response_code);
    test_expect_string(test, "and a transport error code", "transport", string_get_data(&token.error));
    test_expect_false(test, "not ok", http_service_oauth_token_is_ok(&token));

    http_service_oauth_token_uninit(&token);
    http_service_oauth_uninit(&oauth);

    test_case_end(test);
}

static void _test_unknown_provider_and_empty_code(Test *const test) {
    test_case_begin(test, "an unknown provider and an empty code are refused before the wire");

    HTTP_Service_OAuth oauth = DEFAULT_INITIALIZATION;

    http_service_oauth_init_1(&oauth);
    http_service_oauth_provider_add_1(&oauth, &(HTTP_Service_OAuth_Provider) {
        .authorize_url  = "https://example.invalid/authorize",
        .client_id      = "id",
        .client_secret  = "secret",
        .name           = "google",
        .profile_url    = "https://example.invalid/profile",
        .redirect_uri   = "https://example.invalid/cb",
        .token_url      = "https://example.invalid/token"
    });

    HTTP_Service_OAuth_Token unknown = http_service_oauth_exchange_code_1(&oauth, "nobody", "c");

    test_expect_string(test, "an unknown provider is named as such", "unknown_provider", string_get_data(&unknown.error));
    test_expect_u(test, "and never reached a server", 0, unknown.response_code);

    /* The code comes off a browser redirect. An empty one used to reach the provider as
     * "code=", spending a round trip to be told what this branch already knows. */
    HTTP_Service_OAuth_Token empty = http_service_oauth_exchange_code_1(&oauth, "google", "");

    test_expect_string(test, "an empty code is refused by value", "invalid_request", string_get_data(&empty.error));
    test_expect_u(test, "without a request", 0, empty.response_code);

    HTTP_Service_OAuth_Token empty_refresh = http_service_oauth_refresh(&oauth, "google", "");

    test_expect_string(test, "so is an empty refresh token", "invalid_request", string_get_data(&empty_refresh.error));

    test_expect_false(test, "and an empty token to revoke", http_service_oauth_revoke_1(&oauth, "google", ""));

    /* One half of the PKCE pair already validated: pkce_challenge_create enforces RFC 7636
     * 4.1's 43..128 chars. A verifier outside that range cannot match a challenge this module
     * derived, so sending it only buys a round trip that ends in invalid_grant. */
    HTTP_Service_OAuth_Token short_verifier = http_service_oauth_exchange_code_2(&oauth, "google", "c", "too-short");

    test_expect_string(test, "a verifier under 43 chars is refused locally", "invalid_request", string_get_data(&short_verifier.error));
    test_expect_u(test, "without a request", 0, short_verifier.response_code);

    char long_verifier[130] = DEFAULT_INITIALIZATION;

    memory_set(long_verifier, sizeof(long_verifier) - 1, 'v');

    long_verifier[sizeof(long_verifier) - 1] = '\0';

    HTTP_Service_OAuth_Token long_refused = http_service_oauth_exchange_code_2(&oauth, "google", "c", long_verifier);

    test_expect_string(test, "and one over 128 chars is refused too", "invalid_request", string_get_data(&long_refused.error));
    test_expect_u(test, "also without a request", 0, long_refused.response_code);

    http_service_oauth_token_uninit(&long_refused);
    http_service_oauth_token_uninit(&short_verifier);
    http_service_oauth_token_uninit(&unknown);
    http_service_oauth_token_uninit(&empty);
    http_service_oauth_token_uninit(&empty_refresh);
    http_service_oauth_uninit(&oauth);

    test_case_end(test);
}

/* A GET /callback with no ?code= or ?state= reads back an EMPTY String, and this tree's
 * string_get_data answers nullptr for one - so null is how absent browser input actually
 * arrives. These five parameters used to carry error_check_null, which LOGS AND ABORTS: an
 * unauthenticated request to a callback route could kill the server. They are DATA, and the
 * whole point of this case is that the refusal happens BY VALUE with the checks ARMED. If the
 * error_check_null calls come back, this binary aborts rather than failing an assertion. */
static void _test_null_data_parameters_refuse_rather_than_abort(Test *const test) {
    test_case_begin(test, "null browser input is refused by value, never aborted");

    HTTP_Service_OAuth oauth = DEFAULT_INITIALIZATION;

    http_service_oauth_init_1(&oauth);
    http_service_oauth_provider_add_1(&oauth, &(HTTP_Service_OAuth_Provider) {
        .authorize_url  = "https://example.invalid/authorize",
        .client_id      = "id",
        .client_secret  = "secret",
        .name           = "google",
        .profile_url    = "https://example.invalid/profile",
        .redirect_uri   = "https://example.invalid/cb",
        .revoke_url     = "https://example.invalid/revoke",
        .token_url      = "https://example.invalid/token"
    });

    // The empty String this tree hands back for an absent query parameter. Its data IS null.
    String absent = string_init_1();

    test_expect_true(test, "an empty String's data really is null in this tree", string_get_data(&absent) == nullptr);

    HTTP_Service_OAuth_Token null_code = http_service_oauth_exchange_code_1(&oauth, "google", string_get_data(&absent));

    test_expect_string(test, "a null authorization code is refused", "invalid_request", string_get_data(&null_code.error));
    test_expect_u(test, "without a request", 0, null_code.response_code);

    HTTP_Service_OAuth_Token null_verifier = http_service_oauth_exchange_code_2(&oauth, "google", nullptr, nullptr);

    test_expect_string(test, "and so is the _2 tier's", "invalid_request", string_get_data(&null_verifier.error));

    HTTP_Service_OAuth_Token null_refresh = http_service_oauth_refresh(&oauth, "google", nullptr);

    test_expect_string(test, "a null refresh token is refused", "invalid_request", string_get_data(&null_refresh.error));
    test_expect_u(test, "without a request", 0, null_refresh.response_code);

    test_expect_false(test, "a null token to revoke is refused", http_service_oauth_revoke_1(&oauth, "google", nullptr));
    test_expect_false(test, "and the _2 tier's, hint and all", http_service_oauth_revoke_2(&oauth, "google", nullptr, nullptr));

    test_expect_false(test, "a null state does not verify", http_service_oauth_state_verify(&oauth, "google", nullptr));
    test_expect_false(test, "and neither does an empty one", http_service_oauth_state_verify(&oauth, "google", ""));

    /* A null or empty state used to build "...&state=" - a URL a caller would happily redirect
     * a browser to, and one the provider echoes back with nothing for state_verify to check. */
    String null_state_url = http_service_oauth_get_authorize_url_1(&oauth, "google", nullptr);

    test_expect_u(test, "a null state yields no authorize URL at all", 0, string_get_size(&null_state_url));

    String empty_state_url = http_service_oauth_get_authorize_url_2(&oauth, "google", "", "", "");

    test_expect_u(test, "and neither does an empty one", 0, string_get_size(&empty_state_url));

    // The positive anchor: with a real state the same call still builds a URL.
    String good_url = http_service_oauth_get_authorize_url_1(&oauth, "google", "state-1");

    test_expect_true(test, "a real state still builds one", string_get_size(&good_url) > 0);

    string_uninit(&good_url);
    string_uninit(&empty_state_url);
    string_uninit(&null_state_url);
    string_uninit(&absent);

    http_service_oauth_token_uninit(&null_refresh);
    http_service_oauth_token_uninit(&null_verifier);
    http_service_oauth_token_uninit(&null_code);
    http_service_oauth_uninit(&oauth);

    test_case_end(test);
}

/* An access token is DATA from a token endpoint that gets pasted into an "Authorization: Bearer"
 * line, and a value over http/query's encode ceiling used to be DROPPED from a request body or
 * URL that was then sent anyway - a grant request with no code in it. */
static void _test_line_breaks_and_oversized_values_fail_the_call(Test *const test) {
    test_case_begin(test, "a CR/LF token and an oversized value fail the call, not the field");

    HTTP_Service_OAuth oauth = DEFAULT_INITIALIZATION;

    http_service_oauth_init_1(&oauth);
    http_service_oauth_provider_add_1(&oauth, &(HTTP_Service_OAuth_Provider) {
        .authorize_url  = "https://example.invalid/authorize",
        .client_id      = "id",
        .client_secret  = "secret",
        .name           = "google",
        .profile_url    = "https://example.invalid/profile",
        .redirect_uri   = "https://example.invalid/cb",
        .token_url      = "https://example.invalid/token"
    });

    HTTP_Service_OAuth_Token    split   = _token_with_access("at-1\r\nX-Injected: yes");
    HTTP_Service_OAuth_Profile  refused = http_service_oauth_get_profile(&oauth, "google", &split);

    test_expect_string(test, "a CR/LF access token is refused", "invalid_request", string_get_data(&refused.error));
    test_expect_u(test, "before any header line was built", 0, refused.response_code);

    HTTP_Service_OAuth_Token    bare = _token_with_access("at-1\nX-Injected: yes");
    HTTP_Service_OAuth_Profile  lf   = http_service_oauth_get_profile(&oauth, "google", &bare);

    test_expect_string(test, "a bare LF is refused too", "invalid_request", string_get_data(&lf.error));

    // One byte past HTTP_QUERY_ENCODE_MAX_SIZE, built a kibibyte at a time.
    String  oversized = string_init_1();
    char    chunk[1024];

    memory_set(chunk, sizeof(chunk), 'a');

    for (USize i = 0; i < 1025; i += 1) {
        string_add_last_2(&oversized, chunk, sizeof(chunk));
    }

    test_expect_true(test, "the value really is over the encode ceiling", string_get_size(&oversized) > HTTP_QUERY_ENCODE_MAX_SIZE);

    HTTP_Service_OAuth_Token huge = http_service_oauth_exchange_code_1(&oauth, "google", string_get_data(&oversized));

    test_expect_string(test, "a code that will not encode fails the whole call", "invalid_request", string_get_data(&huge.error));
    test_expect_u(test, "rather than sending a body with no code in it", 0, huge.response_code);

    String url = http_service_oauth_get_authorize_url_1(&oauth, "google", string_get_data(&oversized));

    test_expect_u(test, "and an authorize URL missing its state is not returned at all", 0, string_get_size(&url));

    /* The same defect one function away. extra_params is the ONE fragment appended without
     * re-encoding, and this URL is what a caller puts in a "Location:" header - so a CR or LF
     * inside it ends that line and starts a header nobody wrote. */
    String split_url = http_service_oauth_get_authorize_url_2(&oauth, "google", "s", "", "access_type=offline\r\nX-Injected: yes");

    test_expect_u(test, "a CR/LF extra_params fragment yields no URL at all", 0, string_get_size(&split_url));

    String lf_url = http_service_oauth_get_authorize_url_2(&oauth, "google", "s", "", "a=b\nc=d");

    test_expect_u(test, "and a bare LF is refused too", 0, string_get_size(&lf_url));

    /* The anchor: without it the two assertions above would pass just as well against a
     * builder that had stopped appending extra_params at all. */
    String clean_url = http_service_oauth_get_authorize_url_2(&oauth, "google", "s", "", "access_type=offline&prompt=consent");

    test_expect_true(test, "while a clean fragment is still appended verbatim", string_find_1(&clean_url, 0, "&access_type=offline&prompt=consent") != USIZE_MAX);

    string_uninit(&clean_url);
    string_uninit(&lf_url);
    string_uninit(&split_url);
    string_uninit(&url);
    string_uninit(&oversized);

    http_service_oauth_token_uninit(&huge);
    http_service_oauth_profile_uninit(&lf);
    http_service_oauth_token_uninit(&bare);
    http_service_oauth_profile_uninit(&refused);
    http_service_oauth_token_uninit(&split);
    http_service_oauth_uninit(&oauth);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases - the unlocked round trip (Crit 1)
 *============================================================================*/

typedef struct {
    HTTP_Service_OAuth  *oauth;
    U64                 elapsed_ms;
    bool                built;
} _Authorize_Probe;

static void* _authorize_probe_main(void *const data) {
    _Authorize_Probe *const probe = (_Authorize_Probe*) data;

    ChronoInstant const start = chrono_now();
    String              url   = http_service_oauth_get_authorize_url_1(probe->oauth, "google", "concurrent");

    probe->elapsed_ms   = chrono_duration_milliseconds(chrono_elapsed(start));
    probe->built        = string_get_size(&url) > 0;

    string_uninit(&url);

    return nullptr;
}

static void _test_stall_does_not_block_other_calls(Test *const test) {
    test_case_begin(test, "a stalled provider does not freeze every other call (the snapshot)");

    Fixture_Server server = DEFAULT_INITIALIZATION;

    server.script          = FIXTURE_SCRIPT_STALL;
    server.max_connections = 2;

    if (!_start(test, &server)) {
        test_case_end(test);

        return;
    }

    HTTP_Service_OAuth oauth = DEFAULT_INITIALIZATION;

    http_service_oauth_init_1(&oauth);
    _register_local(&oauth, "google", fixture_server_port(&server));

    _Authorize_Probe probe = DEFAULT_INITIALIZATION;

    probe.oauth = &oauth;

    Thread probe_thread = DEFAULT_INITIALIZATION;

    ChronoInstant const start = chrono_now();

    /* The exchange runs on THIS thread and will sit on the stalled socket for the client's
     * whole 20 s budget. The probe thread asks the same service for an authorize URL while it
     * does. Before the row snapshot, get_authorize_url took the same mutex the exchange held
     * across http_client_post_4, so it waited out the entire stall - every login in the
     * process serialized behind one bad provider. */
    if (!test_expect_true(test, "the probe thread starts", !result_is_error(thread_create_1(&probe_thread, _authorize_probe_main, &probe)))) {
        fixture_server_join(&server);
        string_uninit(&server.request_body);
        http_service_oauth_uninit(&oauth);

        test_case_end(test);

        return;
    }

    HTTP_Service_OAuth_Token token = http_service_oauth_exchange_code_1(&oauth, "google", "c");
    U64 const exchange_ms = chrono_duration_milliseconds(chrono_elapsed(start));

    thread_join_1(&probe_thread);

    /* The fixture holds the socket for its own 4 s and then closes, which is shorter than the
     * client's 20 s budget - so the observable is an aborted transport rather than the client's
     * own TIMEOUT. Either way the exchange was blocked on the network for seconds, which is the
     * only thing this case needs it to do. */
    test_expect_true(test, "the stalled exchange was blocked on the network for seconds", exchange_ms >= 3000);
    test_expect_true(test, "and still returned inside the client's own budget", exchange_ms < 30000);
    test_expect_false(test, "the stalled exchange is not ok", http_service_oauth_token_is_ok(&token));
    test_expect_true(test, "and it reported a transport failure", !string_empty(&token.error));

    /* THE POINT OF THE CASE. Before the row snapshot, get_authorize_url took the same mutex
     * the exchange held across http_client_post_4, so this number was the stall's number. */
    test_expect_true(test, "the concurrent authorize url was built", probe.built);
    test_expect_true(test, "and it did NOT wait behind the stalled request", probe.elapsed_ms < 1000);

    printf("  [measurement] stalled exchange %llu ms; concurrent get_authorize_url %llu ms\n",
        (unsigned long long) exchange_ms,
        (unsigned long long) probe.elapsed_ms);

    http_service_oauth_token_uninit(&token);
    fixture_server_join(&server);
    string_uninit(&server.request_body);
    http_service_oauth_uninit(&oauth);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases - profile
 *============================================================================*/

static void _test_profile_bearer_and_numeric_id(Test *const test) {
    test_case_begin(test, "profile: Bearer header on the wire, and a NUMERIC id read as decimal");

    Fixture_Server server = DEFAULT_INITIALIZATION;

    server.script                = FIXTURE_SCRIPT_OK;
    server.response_body         = "{\"id\":583231,\"login\":\"octocat\",\"name\":\"The Octocat\","
                                   "\"email\":\"octocat@example.invalid\",\"avatar_url\":\"https://example.invalid/a.png\","
                                   "\"email_verified\":true}";
    server.response_content_type = "application/json";

    if (!_start(test, &server)) {
        test_case_end(test);

        return;
    }

    HTTP_Service_OAuth oauth = DEFAULT_INITIALIZATION;

    http_service_oauth_init_1(&oauth);
    _register_local(&oauth, "github", fixture_server_port(&server));

    HTTP_Service_OAuth_Token token = _token_with_access("bearer-value");

    HTTP_Service_OAuth_Profile profile = http_service_oauth_get_profile(&oauth, "github", &token);

    fixture_server_join(&server);

    test_expect_string(test, "the profile fetch is a GET", "GET", server.request_method);
    test_expect_true(test, "carrying the bearer credential", _contains(server.request_headers, "Authorization: Bearer bearer-value"));

    /* GitHub's id is a JSON NUMBER. Reading only strings left profile.id empty for every
     * GitHub account - a provider the registry accepted and the flow could never complete. */
    test_expect_string(test, "a numeric id is rendered as decimal", "583231", string_get_data(&profile.id));
    test_expect_string(test, "the display name", "The Octocat", string_get_data(&profile.name));
    test_expect_string(test, "the email", "octocat@example.invalid", string_get_data(&profile.email));
    test_expect_string(test, "avatar_url as the picture", "https://example.invalid/a.png", string_get_data(&profile.picture));
    test_expect_true(test, "email_verified reads as YES, not as a bare bool", profile.email_verified == HTTP_SERVICE_OAUTH_EMAIL_VERIFIED_YES);
    test_expect_true(test, "the profile reports ok", http_service_oauth_profile_is_ok(&profile));

    http_service_oauth_profile_uninit(&profile);
    http_service_oauth_token_uninit(&token);
    http_service_oauth_uninit(&oauth);
    string_uninit(&server.request_body);

    test_case_end(test);
}

static void _test_profile_401(Test *const test) {
    test_case_begin(test, "profile: a 401 yields an empty profile carrying the status");

    Fixture_Server server = DEFAULT_INITIALIZATION;

    server.script                = FIXTURE_SCRIPT_STATUS;
    server.status_code           = 401;
    server.response_body         = "{\"message\":\"Bad credentials\"}";
    server.response_content_type = "application/json";

    if (!_start(test, &server)) {
        test_case_end(test);

        return;
    }

    HTTP_Service_OAuth oauth = DEFAULT_INITIALIZATION;

    http_service_oauth_init_1(&oauth);
    _register_local(&oauth, "github", fixture_server_port(&server));

    HTTP_Service_OAuth_Token token = _token_with_access("expired");

    HTTP_Service_OAuth_Profile profile = http_service_oauth_get_profile(&oauth, "github", &token);

    fixture_server_join(&server);

    test_expect_false(test, "the profile is not ok", http_service_oauth_profile_is_ok(&profile));
    test_expect_u(test, "the status is recorded", 401, profile.response_code);
    test_expect_true(test, "no id was parsed", string_empty(&profile.id));
    test_expect_string(test, "and GitHub's message becomes the description", "Bad credentials", string_get_data(&profile.error_description));
    test_expect_true(test, "email_verified stays UNKNOWN, not false", profile.email_verified == HTTP_SERVICE_OAUTH_EMAIL_VERIFIED_UNKNOWN);

    http_service_oauth_profile_uninit(&profile);
    http_service_oauth_token_uninit(&token);
    http_service_oauth_uninit(&oauth);
    string_uninit(&server.request_body);

    test_case_end(test);
}

/* profile_is_ok used to ignore `error` while token_is_ok required it empty, and _json_copy_error
 * folds GitHub's {"message": ...} in as well - so a 200 carrying a provider complaint next to an
 * id read as a successful identity. */
static void _test_profile_200_with_provider_error(Test *const test) {
    test_case_begin(test, "profile: a 200 carrying an error is NOT ok, even with an id");

    Fixture_Server server = DEFAULT_INITIALIZATION;

    server.script                = FIXTURE_SCRIPT_OK;
    server.response_body         = "{\"id\":\"583231\",\"error\":\"account_suspended\"}";
    server.response_content_type = "application/json";

    if (!_start(test, &server)) {
        test_case_end(test);

        return;
    }

    HTTP_Service_OAuth oauth = DEFAULT_INITIALIZATION;

    http_service_oauth_init_1(&oauth);
    _register_local(&oauth, "github", fixture_server_port(&server));

    HTTP_Service_OAuth_Token token = _token_with_access("bearer-value");

    HTTP_Service_OAuth_Profile profile = http_service_oauth_get_profile(&oauth, "github", &token);

    fixture_server_join(&server);

    // The three conditions that used to be the whole test all hold - which is the point.
    test_expect_u(test, "the transport really did answer 200", 200, profile.response_code);
    test_expect_string(test, "and an id really was parsed", "583231", string_get_data(&profile.id));
    test_expect_string(test, "next to the provider's own complaint", "account_suspended", string_get_data(&profile.error));

    test_expect_false(test, "so the profile is NOT ok", http_service_oauth_profile_is_ok(&profile));

    http_service_oauth_profile_uninit(&profile);
    http_service_oauth_token_uninit(&token);
    http_service_oauth_uninit(&oauth);
    string_uninit(&server.request_body);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases - refresh and revoke
 *============================================================================*/

static void _test_refresh_body(Test *const test) {
    test_case_begin(test, "refresh: the grant body, and a new token parsed back");

    Fixture_Server server = DEFAULT_INITIALIZATION;

    server.script                = FIXTURE_SCRIPT_OK;
    server.response_body         = "{\"access_token\":\"at-2\",\"expires_in\":\"7200\"}";
    server.response_content_type = "application/json";

    if (!_start(test, &server)) {
        test_case_end(test);

        return;
    }

    HTTP_Service_OAuth oauth = DEFAULT_INITIALIZATION;

    http_service_oauth_init_1(&oauth);
    _register_local(&oauth, "google", fixture_server_port(&server));

    HTTP_Service_OAuth_Token token = http_service_oauth_refresh(&oauth, "google", "rt-1");

    fixture_server_join(&server);

    test_expect_true(test, "grant_type is refresh_token", _body_contains(&server.request_body, "grant_type=refresh_token"));
    test_expect_true(test, "the refresh token is sent", _body_contains(&server.request_body, "&refresh_token=rt-1"));
    test_expect_true(test, "with the client credentials", _body_contains(&server.request_body, "&client_secret=s3cr3t%2Fvalue"));
    test_expect_true(test, "and no redirect_uri, which is not part of this grant", !_body_contains(&server.request_body, "redirect_uri"));

    test_expect_string(test, "the new access token", "at-2", string_get_data(&token.access_token));

    // A STRING expires_in used to be dropped, leaving a caller to store the token as eternal.
    test_expect_u(test, "a string-typed expires_in is still read", 7200, token.expires_in);

    http_service_oauth_token_uninit(&token);
    http_service_oauth_uninit(&oauth);
    string_uninit(&server.request_body);

    test_case_end(test);
}

static void _test_revoke_paths(Test *const test) {
    test_case_begin(test, "revoke: body and hint, 200 -> true, 400 -> false, closed port -> false");

    Fixture_Server server = DEFAULT_INITIALIZATION;

    server.script = FIXTURE_SCRIPT_OK;

    if (!_start(test, &server)) {
        test_case_end(test);

        return;
    }

    HTTP_Service_OAuth oauth = DEFAULT_INITIALIZATION;

    http_service_oauth_init_1(&oauth);
    _register_local(&oauth, "google", fixture_server_port(&server));

    test_expect_true(test, "a 200 revoke answers true", http_service_oauth_revoke_2(&oauth, "google", "at-1", "access_token"));

    fixture_server_join(&server);

    test_expect_true(test, "the token is in the body", _body_contains(&server.request_body, "token=at-1"));
    test_expect_true(test, "with the client credentials", _body_contains(&server.request_body, "&client_id=client-id"));
    test_expect_true(test, "and the RFC 7009 hint", _body_contains(&server.request_body, "&token_type_hint=access_token"));

    string_uninit(&server.request_body);
    http_service_oauth_uninit(&oauth);

    Fixture_Server rejecting = DEFAULT_INITIALIZATION;

    rejecting.script      = FIXTURE_SCRIPT_STATUS;
    rejecting.status_code = 400;

    if (!_start(test, &rejecting)) {
        test_case_end(test);

        return;
    }

    HTTP_Service_OAuth rejected = DEFAULT_INITIALIZATION;

    http_service_oauth_init_1(&rejected);
    _register_local(&rejected, "google", fixture_server_port(&rejecting));

    /* A completed transport is NOT a revocation: an endpoint answering 400 reached the server
     * fine and revoked nothing. This used to return true unconditionally. */
    test_expect_false(test, "a 400 revoke answers false", http_service_oauth_revoke_1(&rejected, "google", "at-1"));

    U16 const dead_port = fixture_server_port(&rejecting);

    fixture_server_join(&rejecting);
    string_uninit(&rejecting.request_body);
    http_service_oauth_uninit(&rejected);

    HTTP_Service_OAuth unreachable = DEFAULT_INITIALIZATION;

    http_service_oauth_init_1(&unreachable);
    _register_local(&unreachable, "google", dead_port);

    test_expect_false(test, "a closed port answers false", http_service_oauth_revoke_1(&unreachable, "google", "at-1"));

    http_service_oauth_uninit(&unreachable);

    // A provider with no revoke endpoint at all - GitHub's shape - answers false, never aborts.
    HTTP_Service_OAuth no_endpoint = DEFAULT_INITIALIZATION;

    http_service_oauth_init_1(&no_endpoint);

    HTTP_Service_OAuth_Provider lean = _provider("github");

    lean.revoke_url = "";

    http_service_oauth_provider_add_1(&no_endpoint, &lean);

    test_expect_false(test, "a provider without a revoke endpoint answers false", http_service_oauth_revoke_1(&no_endpoint, "github", "at-1"));

    http_service_oauth_uninit(&no_endpoint);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Entry point
 *============================================================================*/

int main(void) {
    LogConfig const log_config = {
        .level             = LOG_LEVEL_ERROR,
        .stream            = LOG_STREAM_STDOUT,
        .timestamp_enabled = true,
        .autoflush         = true,
    };

    log_init(log_config);

    Test test = test_init("tests/http/service/oauth/test_all.c");

    test_verbose_set(&test, false);

    test_suite_begin(&test, "http_service_oauth");

    _test_provider_registration(&test);
    _test_two_instances_are_independent(&test);
    _test_provider_update_replaces_in_place(&test);
    _test_optional_and_required_fields(&test);
    _test_uninit_without_use(&test);
    _test_authorize_url_exact(&test);
    _test_state_issue_and_verify(&test);
    _test_pkce_rfc7636_vector(&test);
    _test_exchange_body_and_headers(&test);
    _test_exchange_with_pkce_verifier(&test);
    _test_exchange_400_invalid_grant(&test);
    _test_exchange_5xx_html(&test);
    _test_exchange_closed_port(&test);
    _test_unknown_provider_and_empty_code(&test);
    _test_null_data_parameters_refuse_rather_than_abort(&test);
    _test_line_breaks_and_oversized_values_fail_the_call(&test);
    _test_stall_does_not_block_other_calls(&test);
    _test_profile_bearer_and_numeric_id(&test);
    _test_profile_401(&test);
    _test_profile_200_with_provider_error(&test);
    _test_refresh_body(&test);
    _test_revoke_paths(&test);

    test_suite_end(&test);

    return test_uninit(&test);
}