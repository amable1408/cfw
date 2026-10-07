/*
 * test_unchecked.c - allocation-refusal and value-refusal suite for http/service/oauth,
 * built WITHOUT ERROR_CHECK_ENABLED.
 *
 * WHY A SEPARATE BINARY. Two reasons, both measured rather than assumed:
 *   - on the HEAP path memory_alloc ABORTS on a null calloc rather than returning it, so an
 *     allocation refusal is unreachable there and an exhausted ARENA is the only way to reach
 *     one at all;
 *   - with the checks ARMED, arena exhaustion aborts inside arena_linear_alloc's own bounds
 *     check before the caller can observe a refusal. The first draft of these cases lived in
 *     test_all.c and died at arena_linear.c:66.
 * The refusal is only REACHABLE in the configuration that ships.
 *
 * The second thing this build proves is that the VALUE-dependent refusals are real branches and
 * not error_check_* in disguise: an empty client_secret, a null required field, an empty
 * authorization code and an empty token to revoke must all still be refused when every
 * error_check_* compiles to nothing. That distinction is the whole of design item High 6 -
 * configuration and browser input are DATA, and data is refused, never aborted.
 *
 * WHAT THIS DOES NOT COVER: the heap path still aborts rather than degrading, and the network
 * functions need the loopback fixture, which test_all.c drives.
 */

#include <http/service/oauth/oauth.h>
#include <test/test.h>

/*==============================================================================
 * MARK: - Helpers
 *============================================================================*/

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

/* A Token the caller builds by hand, since get_profile_1 takes one but only ever reads its
 * access_token. Every String member has to exist, because profile teardown uninits them all.
 * Heap, not the arena under test: this is the harness's storage, not the module's. */
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

/*==============================================================================
 * MARK: - Cases
 *============================================================================*/

static void _test_refused_update_leaves_the_provider_intact(Test *const test) {
    test_case_begin(test, "a refused update leaves the existing provider exactly as it was");

    Arena arena = arena_init_1(16384, ARENA_TYPE_LINEAR);
    HTTP_Service_OAuth oauth = DEFAULT_INITIALIZATION;

    test_expect_true(test, "the arena-backed service initializes", http_service_oauth_alloc_init_1(&oauth, &arena));

    HTTP_Service_OAuth_Provider const google = _provider("google");

    // Positive anchor: if this failed, every assertion below would pass for the wrong reason.
    test_expect_true(test, "the initial registration succeeds", http_service_oauth_provider_add_1(&oauth, &google));

    char oversized[32768] = DEFAULT_INITIALIZATION;

    memory_set(oversized, sizeof(oversized) - 1, 'u');

    oversized[sizeof(oversized) - 1] = '\0';

    HTTP_Service_OAuth_Provider refused = _provider("google");

    /* client_id, not client_secret, and deliberately so. The defect this case exists to catch
     * blanks a stored value, and the only non-network observable this service has is
     * get_authorize_url - which reads client_id, redirect_uri and scope, but NOT client_secret.
     * Oversizing the secret would leave every assertion below passing whether the fix is
     * present or not. The code path is identical for all nine fields. */
    refused.client_id = oversized;

    /* A DISTINCT authorize_url, and this is what makes the case prove all-or-nothing rather
     * than merely "nothing was blanked". authorize_url is the FIRST field the row builder
     * writes, so it is built before client_id's copy is refused - precisely the slot a broken
     * commit would leak through. Left identical to the stored value a partial commit would
     * have written the same bytes over themselves and gone unseen. */
    refused.authorize_url = "https://partial.invalid/authorize";

    // Larger than the whole arena, so the copy cannot fit however much room is left.
    test_expect_false(test, "the oversized update is refused", http_service_oauth_provider_add_1(&oauth, &refused));

    /* THE ASSERTIONS THIS SUITE EXISTS FOR. Before the fix the refusal had already destroyed
     * the stored values, so this url would have been built from a null client_id - or the
     * process would have died on the way to it. */
    String url = http_service_oauth_get_authorize_url_1(&oauth, "google", "state-after");

    test_expect_true(test, "the provider still builds a url after the refusal", string_get_size(&url) > 0);
    test_expect_true(test, "and still carries its ORIGINAL client id", string_find_1(&url, 0, "client-id") != USIZE_MAX);
    test_expect_true(test, "and its original endpoint", string_find_1(&url, 0, "example.invalid/authorize") != USIZE_MAX);
    test_expect_true(test, "the refused value is nowhere in it", string_find_1(&url, 0, "uuuu") == USIZE_MAX);
    test_expect_true(test, "the field built BEFORE the refusal was not committed", string_find_1(&url, 0, "partial.invalid") == USIZE_MAX);

    string_uninit(&url);

    http_service_oauth_uninit(&oauth);
    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_refused_registration_stores_no_partial_row(Test *const test) {
    test_case_begin(test, "a refused NEW registration leaves no partial provider behind");

    Arena arena = arena_init_1(16384, ARENA_TYPE_LINEAR);
    HTTP_Service_OAuth oauth = DEFAULT_INITIALIZATION;

    http_service_oauth_alloc_init_1(&oauth, &arena);

    HTTP_Service_OAuth_Provider const google = _provider("google");

    test_expect_true(test, "the first provider registers", http_service_oauth_provider_add_1(&oauth, &google));

    char oversized[32768] = DEFAULT_INITIALIZATION;

    memory_set(oversized, sizeof(oversized) - 1, 'u');

    oversized[sizeof(oversized) - 1] = '\0';

    HTTP_Service_OAuth_Provider refused = _provider("github");

    // Observable for the same reason as above.
    refused.client_id = oversized;

    test_expect_false(test, "the oversized registration is refused", http_service_oauth_provider_add_1(&oauth, &refused));

    /* The row never reaches the registry at all: it is built into the caller's own struct and
     * only appended once every field exists. A partial row would have carried the name
     * "github" with an empty credential beside it - a provider that resolves and cannot
     * authenticate. An empty url is the observable proof that nothing was kept. */
    String refused_url = http_service_oauth_get_authorize_url_1(&oauth, "github", "state-x");

    test_expect_u(test, "the refused provider is not registered at all", 0, string_get_size(&refused_url));

    String google_url = http_service_oauth_get_authorize_url_1(&oauth, "google", "state-y");

    test_expect_true(test, "the existing provider still builds its url", string_get_size(&google_url) > 0);
    test_expect_true(test, "with its own client id", string_find_1(&google_url, 0, "client-id") != USIZE_MAX);

    string_uninit(&refused_url);
    string_uninit(&google_url);

    http_service_oauth_uninit(&oauth);
    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_value_refusals_hold_without_error_checks(Test *const test) {
    test_case_begin(test, "configuration and browser input are refused by VALUE, not by error_check");

    Arena arena = arena_init_1(64 * 1024, ARENA_TYPE_LINEAR);
    HTTP_Service_OAuth oauth = DEFAULT_INITIALIZATION;

    http_service_oauth_alloc_init_1(&oauth, &arena);

    /* This is the case that used to kill a server at boot. error_check_non_value_uint on the
     * field sizes ABORTED on an empty client_secret - which is how a set-but-empty
     * TRAYMON_OAUTH_GOOGLE_CLIENT_SECRET= arrives on Linux, and precisely the misconfiguration
     * a refusal lets the caller log and continue past. With the checks compiled out it would
     * instead have been a null dereference. It is now a plain branch, in both builds. */
    HTTP_Service_OAuth_Provider empty_secret = _provider("google");

    empty_secret.client_secret = "";

    test_expect_false(test, "an empty client_secret is refused", http_service_oauth_provider_add_1(&oauth, &empty_secret));

    HTTP_Service_OAuth_Provider null_url = _provider("google");

    null_url.token_url = nullptr;

    test_expect_false(test, "a null required field is refused", http_service_oauth_provider_add_1(&oauth, &null_url));

    // The optional pair, in the same build: absent is a legal value, not a missing field.
    HTTP_Service_OAuth_Provider lean = _provider("github");

    lean.revoke_url = nullptr;
    lean.scope      = nullptr;

    test_expect_true(test, "a provider with no revoke endpoint and no scope still registers", http_service_oauth_provider_add_1(&oauth, &lean));

    /* Browser input, refused before the wire. Without this branch an empty code reached the
     * provider as "code=" and an empty token as "token=". */
    HTTP_Service_OAuth_Token empty_code = http_service_oauth_exchange_code_1(&oauth, "github", "");

    test_expect_string(test, "an empty authorization code is refused", "invalid_request", string_get_data(&empty_code.error));

    HTTP_Service_OAuth_Token unknown = http_service_oauth_exchange_code_1(&oauth, "nobody", "c");

    test_expect_string(test, "and an unknown provider is named", "unknown_provider", string_get_data(&unknown.error));

    test_expect_false(test, "an empty token to revoke is refused", http_service_oauth_revoke_1(&oauth, "github", ""));
    test_expect_false(test, "and a provider with no revoke endpoint answers false", http_service_oauth_revoke_1(&oauth, "github", "at"));

    /* RFC 7636 4.1's 43..128 chars, refused by value on the exchange side too - the bound
     * pkce_challenge_create has always enforced on the other half of the pair. */
    HTTP_Service_OAuth_Token short_verifier = http_service_oauth_exchange_code_2(&oauth, "github", "c", "too-short");

    test_expect_string(test, "a PKCE verifier under 43 chars is refused", "invalid_request", string_get_data(&short_verifier.error));

    http_service_oauth_token_uninit(&short_verifier);
    http_service_oauth_token_uninit(&empty_code);
    http_service_oauth_token_uninit(&unknown);

    http_service_oauth_uninit(&oauth);
    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

/* The same five DATA parameters as test_all.c's null case, with the checks COMPILED OUT. This
 * is where it matters most: with ERROR_CHECK_ENABLED off an error_check_null is not a loud
 * abort but nothing at all, so a null would run straight into char_length and the encoders as
 * undefined behaviour. Every refusal below has to be a real branch on the value. */
static void _test_null_data_parameters_are_real_branches(Test *const test) {
    test_case_begin(test, "null browser input is refused by a real branch, not by error_check");

    Arena arena = arena_init_1(64 * 1024, ARENA_TYPE_LINEAR);
    HTTP_Service_OAuth oauth = DEFAULT_INITIALIZATION;

    test_expect_true(test, "the arena-backed service initializes", http_service_oauth_alloc_init_1(&oauth, &arena));

    HTTP_Service_OAuth_Provider const google = _provider("google");

    // Positive anchor: without a registered provider every refusal below could be the wrong one.
    test_expect_true(test, "the provider registers", http_service_oauth_provider_add_1(&oauth, &google));

    HTTP_Service_OAuth_Token null_code = http_service_oauth_exchange_code_1(&oauth, "google", nullptr);

    test_expect_string(test, "a null authorization code is refused", "invalid_request", string_get_data(&null_code.error));

    HTTP_Service_OAuth_Token null_refresh = http_service_oauth_refresh(&oauth, "google", nullptr);

    test_expect_string(test, "a null refresh token is refused", "invalid_request", string_get_data(&null_refresh.error));

    test_expect_false(test, "a null token to revoke is refused", http_service_oauth_revoke_1(&oauth, "google", nullptr));
    test_expect_false(test, "an empty token to revoke is refused", http_service_oauth_revoke_1(&oauth, "google", ""));
    test_expect_false(test, "a null state does not verify", http_service_oauth_state_verify_1(&oauth, "google", nullptr));
    test_expect_false(test, "an empty state does not verify", http_service_oauth_state_verify_1(&oauth, "google", ""));

    HTTP_Service_OAuth_Profile null_profile = http_service_oauth_get_profile_2(&oauth, "google", nullptr);

    test_expect_string(test, "a null access token is refused by get_profile_2", "invalid_request", string_get_data(&null_profile.error));

    http_service_oauth_profile_uninit(&null_profile);

    /* A CR/LF provider name is refused by value at every entry point that logs the name before
     * the registry lookup - a real branch, so it holds with the checks compiled out. */
    char const *const forged = "google\r\nforged";

    HTTP_Service_OAuth_Token forged_code = http_service_oauth_exchange_code_2(&oauth, forged, "c", nullptr);

    test_expect_string(test, "a CR/LF provider is refused at exchange_code_2", "invalid_request", string_get_data(&forged_code.error));

    HTTP_Service_OAuth_Profile forged_profile = http_service_oauth_get_profile_2(&oauth, forged, "at");

    test_expect_string(test, "and at get_profile_2", "invalid_request", string_get_data(&forged_profile.error));

    HTTP_Service_OAuth_Token forged_refresh = http_service_oauth_refresh(&oauth, forged, "rt");

    test_expect_string(test, "and at refresh", "invalid_request", string_get_data(&forged_refresh.error));
    test_expect_false(test, "and at revoke_2", http_service_oauth_revoke_2(&oauth, forged, "at", ""));

    http_service_oauth_token_uninit(&forged_refresh);
    http_service_oauth_profile_uninit(&forged_profile);
    http_service_oauth_token_uninit(&forged_code);

    String null_state_url = http_service_oauth_get_authorize_url_1(&oauth, "google", nullptr);

    test_expect_u(test, "a null state yields no authorize URL", 0, string_get_size(&null_state_url));

    String empty_state_url = http_service_oauth_get_authorize_url_1(&oauth, "google", "");

    test_expect_u(test, "and neither does an empty one", 0, string_get_size(&empty_state_url));

    String good_url = http_service_oauth_get_authorize_url_1(&oauth, "google", "state-1");

    test_expect_true(test, "a real state still builds one", string_get_size(&good_url) > 0);

    string_uninit(&good_url);
    string_uninit(&empty_state_url);
    string_uninit(&null_state_url);

    http_service_oauth_token_uninit(&null_refresh);
    http_service_oauth_token_uninit(&null_code);

    http_service_oauth_uninit(&oauth);
    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

/* An exhausted arena is the only way to reach an allocator refusal in this tree, and it is what
 * makes the checked form_add/encode outcomes observable: the body cannot be completed, so the
 * whole call is refused rather than a request going out with a field missing from it. */
static void _test_an_unbuildable_body_fails_the_call(Test *const test) {
    test_case_begin(test, "a body that cannot be built is refused, not sent short a field");

    Arena arena = arena_init_1(16384, ARENA_TYPE_LINEAR);
    HTTP_Service_OAuth oauth = DEFAULT_INITIALIZATION;

    test_expect_true(test, "the arena-backed service initializes", http_service_oauth_alloc_init_1(&oauth, &arena));

    HTTP_Service_OAuth_Provider const google = _provider("google");

    test_expect_true(test, "the provider registers", http_service_oauth_provider_add_1(&oauth, &google));

    // A code twice the size of the whole arena: the payload String has nowhere to put it.
    char oversized[32768] = DEFAULT_INITIALIZATION;

    memory_set(oversized, sizeof(oversized) - 1, 'c');

    oversized[sizeof(oversized) - 1] = '\0';

    HTTP_Service_OAuth_Token starved = http_service_oauth_exchange_code_1(&oauth, "google", oversized);

    test_expect_string(test, "an unbuildable grant body fails the call", "invalid_request", string_get_data(&starved.error));
    test_expect_u(test, "without reaching a server", 0, starved.response_code);

    // The same shape through the URL builder: a half-built authorize URL is never returned.
    String url = http_service_oauth_get_authorize_url_1(&oauth, "google", oversized);

    test_expect_u(test, "and an unbuildable authorize URL is empty, not partial", 0, string_get_size(&url));

    string_uninit(&url);

    http_service_oauth_token_uninit(&starved);

    http_service_oauth_uninit(&oauth);
    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

/* "misconfigured" was documented on Profile.error from the day the field existed and emitted
 * NOWHERE: the row snapshot answered false for BOTH "no such provider" and "the allocator
 * refused the row copy", and every caller reported unknown_provider. Under an exhausted arena an
 * operator was sent hunting a registration bug that did not exist.
 *
 * The arena is sized so that ONE row fits and a SECOND copy of it does not, which is exactly the
 * state a snapshot hits: the row is registered, and there is no room to copy it out. Enough is
 * left over for the refused token's own small Strings, or the answer could not be read back. */
static void _test_a_refused_row_copy_reports_misconfigured(Test *const test) {
    test_case_begin(test, "a refused row copy reports misconfigured, not unknown_provider");

    char big_url[6000] = DEFAULT_INITIALIZATION;

    memory_set(big_url, sizeof(big_url) - 1, 'u');

    big_url[sizeof(big_url) - 1] = '\0';

    Arena arena = arena_init_1(10240, ARENA_TYPE_LINEAR);
    HTTP_Service_OAuth oauth = DEFAULT_INITIALIZATION;

    test_expect_true(test, "the arena-backed service initializes", http_service_oauth_alloc_init_1(&oauth, &arena));

    HTTP_Service_OAuth_Provider google = _provider("google");

    google.authorize_url = big_url;

    // Positive anchor: one row fits, so what fails below is the COPY and not the registration.
    test_expect_true(test, "the fat provider registers", http_service_oauth_provider_add_1(&oauth, &google));

    HTTP_Service_OAuth_Token starved = http_service_oauth_exchange_code_1(&oauth, "google", "the-code");

    test_expect_string(test, "a registered provider whose row will not copy is misconfigured", "misconfigured", string_get_data(&starved.error));
    test_expect_u(test, "and it never reached a server", 0, starved.response_code);

    /* The distinction this case exists for. Same service, same exhausted arena, different
     * answer - without this the assertion above would pass against a version that had simply
     * renamed unknown_provider. */
    HTTP_Service_OAuth_Token unknown = http_service_oauth_exchange_code_1(&oauth, "nobody", "the-code");

    test_expect_string(test, "while a name that was never registered stays unknown_provider", "unknown_provider", string_get_data(&unknown.error));

    HTTP_Service_OAuth_Token   bearer  = _token_with_access("at-1");
    HTTP_Service_OAuth_Profile profile = http_service_oauth_get_profile_1(&oauth, "google", &bearer);

    test_expect_string(test, "get_profile reports the same two answers apart", "misconfigured", string_get_data(&profile.error));

    http_service_oauth_profile_uninit(&profile);
    http_service_oauth_token_uninit(&bearer);
    http_service_oauth_token_uninit(&unknown);
    http_service_oauth_token_uninit(&starved);

    http_service_oauth_uninit(&oauth);
    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_state_and_pkce_refuse_rather_than_abort(Test *const test) {
    test_case_begin(test, "state and PKCE refuse bad values with the checks compiled out");

    Arena arena = arena_init_1(64 * 1024, ARENA_TYPE_LINEAR);
    HTTP_Service_OAuth oauth = DEFAULT_INITIALIZATION;

    http_service_oauth_alloc_init_1(&oauth, &arena);

    char state[HTTP_SERVICE_OAUTH_STATE_MAX_SIZE + 1] = DEFAULT_INITIALIZATION;

    test_expect_false(test, "a zero ttl is refused", http_service_oauth_state_issue_1(&oauth, "google", 0, state));
    test_expect_false(test, "a ttl that would wrap the expiry is refused", http_service_oauth_state_issue_1(&oauth, "google", USIZE_MAX, state));
    test_expect_false(test, "an empty provider is refused", http_service_oauth_state_issue_1(&oauth, "", 600, state));
    test_expect_true(test, "a well-formed request still succeeds", http_service_oauth_state_issue_1(&oauth, "google", 600, state));
    test_expect_true(test, "and verifies", http_service_oauth_state_verify_1(&oauth, "google", state));

    // Shapes a hand-written parser would run off the end of.
    test_expect_false(test, "a state with no separators is refused", http_service_oauth_state_verify_1(&oauth, "google", "aaaa"));
    test_expect_false(test, "a state with one separator is refused", http_service_oauth_state_verify_1(&oauth, "google", "aaaa.bbbb"));
    test_expect_false(test, "a state with three is refused", http_service_oauth_state_verify_1(&oauth, "google", "a.b.c.d"));
    test_expect_false(test, "empty fields are refused", http_service_oauth_state_verify_1(&oauth, "google", ".."));
    char const *const non_numeric = "abcd.notanumber.0123456789012345678901234567890123456789012345678901234567890123";

    test_expect_false(test, "a non-numeric expiry is refused", http_service_oauth_state_verify_1(&oauth, "google", non_numeric));

    /* The binding is request data (a cookie, a session id), so with the checks compiled out a
     * null one claiming a size, or an oversized one, must be a real branch and not a read off
     * the end of nothing. */
    char bound[HTTP_SERVICE_OAUTH_STATE_MAX_SIZE + 1] = DEFAULT_INITIALIZATION;

    test_expect_false(test, "a null binding with a size is refused at issue", http_service_oauth_state_issue_2(&oauth, "google", nullptr, 4, 600, bound));
    test_expect_true(test, "a real binding issues", http_service_oauth_state_issue_2(&oauth, "google", "cookie", 6, 600, bound));
    test_expect_false(test, "a null binding with a size is refused at verify", http_service_oauth_state_verify_2(&oauth, "google", nullptr, 4, bound));
    test_expect_false(test, "an oversized binding is refused at verify", http_service_oauth_state_verify_2(&oauth, "google", bound, HTTP_SERVICE_OAUTH_STATE_BINDING_MAX_SIZE + 1, bound));
    test_expect_false(test, "another binding is refused", http_service_oauth_state_verify_2(&oauth, "google", "cookie2", 7, bound));
    test_expect_false(test, "and so is the _1 tier's empty one", http_service_oauth_state_verify_1(&oauth, "google", bound));
    test_expect_true(test, "the matching binding verifies", http_service_oauth_state_verify_2(&oauth, "google", "cookie", 6, bound));

    /* The EMPTY binding is the _1 tier's alone: a _2 issue or verify with size 0 is a refusal,
     * not a fall-through to the _1 answer - a callback that lost its cookie must fail closed. */
    test_expect_false(test, "an empty binding is refused at _2 verify", http_service_oauth_state_verify_2(&oauth, "google", "", 0, state));
    test_expect_false(test, "and a null one with size 0", http_service_oauth_state_verify_2(&oauth, "google", nullptr, 0, state));
    test_expect_false(test, "and at _2 issue", http_service_oauth_state_issue_2(&oauth, "google", nullptr, 0, 600, bound));

    // The provider is bounded by value on the state paths too, so a route parameter never sizes the MAC scratch.
    char long_provider[HTTP_SERVICE_OAUTH_STATE_BINDING_MAX_SIZE + 2] = DEFAULT_INITIALIZATION;

    memory_set(long_provider, sizeof(long_provider) - 1, 'p');

    long_provider[sizeof(long_provider) - 1] = '\0';

    test_expect_false(test, "an over-long provider is refused at issue", http_service_oauth_state_issue_1(&oauth, long_provider, 600, bound));
    test_expect_false(test, "and at verify", http_service_oauth_state_verify_2(&oauth, long_provider, "cookie", 6, bound));

    /* The provider is length-prefixed in the MAC message (0.3.0): ("x", "3:abc|0:") and
     * ("x|8:3:abc", empty) spelled the same bytes before. Pinned here too, on the arena path. */
    char collision[HTTP_SERVICE_OAUTH_STATE_MAX_SIZE + 1] = DEFAULT_INITIALIZATION;

    test_expect_true(test, "a state issues under a separator-carrying pair", http_service_oauth_state_issue_2(&oauth, "x", "3:abc|0:", 8, 600, collision));
    test_expect_true(test, "and verifies under it", http_service_oauth_state_verify_2(&oauth, "x", "3:abc|0:", 8, collision));
    test_expect_false(test, "and is REFUSED under the pair whose old bytes collided", http_service_oauth_state_verify_1(&oauth, "x|8:3:abc", collision));

    char challenge[HTTP_SERVICE_OAUTH_PKCE_CHALLENGE_SIZE + 1] = DEFAULT_INITIALIZATION;

    test_expect_false(test, "an empty verifier is refused", http_service_oauth_pkce_challenge_create("", challenge));
    test_expect_true(test, "the RFC vector still derives", http_service_oauth_pkce_challenge_create("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk", challenge));
    test_expect_string(test, "to the published value", "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM", challenge);

    http_service_oauth_uninit(&oauth);
    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_success_path_is_unaffected(Test *const test) {
    test_case_begin(test, "with room to spare registration and update both still work");

    Arena arena = arena_init_1(64 * 1024, ARENA_TYPE_LINEAR);
    HTTP_Service_OAuth oauth = DEFAULT_INITIALIZATION;

    http_service_oauth_alloc_init_1(&oauth, &arena);

    HTTP_Service_OAuth_Provider const google = _provider("google");

    test_expect_true(test, "registration succeeds", http_service_oauth_provider_add_1(&oauth, &google));

    HTTP_Service_OAuth_Provider updated = _provider("google");

    updated.authorize_url = "https://updated.invalid/authorize";

    test_expect_true(test, "the update succeeds", http_service_oauth_provider_add_1(&oauth, &updated));

    String url = http_service_oauth_get_authorize_url_1(&oauth, "google", "state-1");

    test_expect_true(test, "the url carries the updated endpoint", string_find_1(&url, 0, "updated.invalid") != USIZE_MAX);

    string_uninit(&url);

    /* Past the row array's initial capacity, so the growth path runs too - a refused reserve
     * must leave the already-built row unstored rather than writing past the end. */
    for (USize i = 0; i < 8; i += 1) {
        char name[16] = DEFAULT_INITIALIZATION;

        char_from_numbers_uint_1(name, sizeof(name), i);

        HTTP_Service_OAuth_Provider const extra = _provider(name);

        test_expect_true(test, "a provider past the initial capacity registers", http_service_oauth_provider_add_1(&oauth, &extra));
    }

    String eighth = http_service_oauth_get_authorize_url_1(&oauth, "7", "state-8");

    test_expect_true(test, "and resolves after the array grew", string_get_size(&eighth) > 0);

    String first = http_service_oauth_get_authorize_url_1(&oauth, "google", "state-9");

    test_expect_true(test, "while the first row survived every growth", string_find_1(&first, 0, "updated.invalid") != USIZE_MAX);

    string_uninit(&eighth);
    string_uninit(&first);

    http_service_oauth_uninit(&oauth);
    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Entry point
 *============================================================================*/

I32 main(void) {
    LogConfig const log_config = {
        .level             = LOG_LEVEL_ERROR,
        .stream            = LOG_STREAM_STDOUT,
        .timestamp_enabled = true,
        .autoflush         = true,
    };

    log_init(log_config);

    Test test = test_init("tests/http/service/oauth/test_unchecked.c");

    test_verbose_set(&test, false);

    test_suite_begin(&test, "http_service_oauth refusals (unchecked build)");

    _test_refused_update_leaves_the_provider_intact(&test);
    _test_refused_registration_stores_no_partial_row(&test);
    _test_value_refusals_hold_without_error_checks(&test);
    _test_null_data_parameters_are_real_branches(&test);
    _test_an_unbuildable_body_fails_the_call(&test);
    _test_a_refused_row_copy_reports_misconfigured(&test);
    _test_state_and_pkce_refuse_rather_than_abort(&test);
    _test_success_path_is_unaffected(&test);

    test_suite_end(&test);

    return test_uninit(&test);
}