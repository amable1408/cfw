#include <curl/curl.h>

#include <container/string/string.h>
#include <http/service/oauth/oauth.h>
#include <log/log.h>
#include <test/test.h>

/* Fault injection for oauth's request-header refusals (oauth.h 0.4.1/0.4.2).
 *
 * WHY A SEPARATE BINARY. Each request tier (token POST, profile GET, revoke POST) installs its
 * headers through http_client_header_add and, when libcurl's list node cannot be allocated,
 * refuses the whole request BEFORE it reaches the wire: "invalid_request" for a token or
 * profile, false for a revoke. That branch never executes in an ordinary suite - curl_slist_append
 * never fails - and it is exactly where a leak hides, because the refusal tears down a client, a
 * response String, a header String and a copied provider row by hand. A refusal branch is
 * unverified until the allocation behind it is actually made to fail.
 *
 * HOW. The seam is -Wl,--wrap=curl_slist_append: the wrapper counts calls and refuses the Nth
 * (returning nullptr WITHOUT freeing the list it was given, which is libcurl's own contract and
 * the one http_client_header_add is written against). -Wl,--wrap=curl_easy_perform is a pure
 * counter, the proof that a refused request SENDS NOTHING: http_client calls curl_easy_perform
 * exactly once per request and nowhere else. -Wl,--wrap=calloc and --wrap=free feed a ledger of
 * live blocks, which is how "the refusal released everything" is proven rather than assumed: the
 * live count after the result is uninitialised must equal the count before the call.
 *
 * THE MEASURED PROFILE (unarmed controls below re-assert it as each case's positive anchor, so a
 * new header moves the boundary loudly instead of silently): a token request installs 2 headers
 * (Accept, Content-Type), a profile request 2 (Authorization, Accept), a revoke 1 (Content-Type).
 * Both header adds of a request are always attempted - a refused first one does not skip the
 * second - so an armed request still counts every append it makes.
 *
 * COVERAGE LIMIT, stated so this file does not overclaim: the revoke tier's OTHER refusal (the
 * form body failing to encode) shares its teardown with the header refusal but cannot be reached
 * through curl_slist_append; it is covered by the oversized-value cases in test_all.c.
 *
 * The endpoints all point at 127.0.0.1:1, where nothing listens: an unarmed control reaches the
 * wire and reports a transport failure, which is how a control proves the request really went out.
 *
 * SINGLE-THREADED BY REQUIREMENT: the counters and the ledger are plain statics. */

/*==============================================================================
 * MARK: - Injector
 *============================================================================*/

#define _TEST_LEDGER_CAPACITY 8192

static void     *_ledger[_TEST_LEDGER_CAPACITY];
static USize    _ledger_count     = 0;
static USize    _ledger_overflow  = 0;
static USize    _append_calls     = 0;
static USize    _append_fail_at   = 0;
static USize    _perform_calls    = 0;

void* __real_calloc(USize const count, USize const size);
void __real_free(void *const pointer);

#ifdef _WIN32
/* On Windows libcurl is a DLL: curl.h declares its functions dllimport, so http_client.c reaches
 * them through the import-table POINTERS (__imp_curl_*), and a --wrap on the plain function name
 * would never see the call. The wrap target is the pointer itself: the linker redirects every
 * reference to __imp_X onto the variable below, which holds the wrapper, and the original pointer
 * stays reachable as the __real___imp_X variable. */
extern CURLcode (*__real___imp_curl_easy_perform)(CURL *handle);
extern struct curl_slist* (*__real___imp_curl_slist_append)(struct curl_slist *list, char const *data);
#else
CURLcode __real_curl_easy_perform(CURL *const handle);
struct curl_slist* __real_curl_slist_append(struct curl_slist *const list, char const *const data);
#endif // _WIN32

static CURLcode _perform_real(CURL *const handle) {
#ifdef _WIN32
    return __real___imp_curl_easy_perform(handle);
#else
    return __real_curl_easy_perform(handle);
#endif // _WIN32
}

static struct curl_slist* _append_real(struct curl_slist *const list, char const *const data) {
#ifdef _WIN32
    return __real___imp_curl_slist_append(list, data);
#else
    return __real_curl_slist_append(list, data);
#endif // _WIN32
}

void* __wrap_calloc(USize const count, USize const size) {
    void *const pointer = __real_calloc(count, size);

    if (pointer == nullptr) {
        return pointer;
    }

    if (_ledger_count >= _TEST_LEDGER_CAPACITY) {
        _ledger_overflow += 1;

        return pointer;
    }

    _ledger[_ledger_count]  = pointer;
    _ledger_count          += 1;

    return pointer;
}

CURLcode __wrap_curl_easy_perform(CURL *const handle) {
    _perform_calls += 1;

    return _perform_real(handle);
}

struct curl_slist* __wrap_curl_slist_append(struct curl_slist *const list, char const *const data) {
    _append_calls += 1;

    if (_append_fail_at != 0 && _append_calls == _append_fail_at) {
        return nullptr;
    }

    return _append_real(list, data);
}

#ifdef _WIN32
CURLcode (*__wrap___imp_curl_easy_perform)(CURL *handle) = __wrap_curl_easy_perform;
struct curl_slist* (*__wrap___imp_curl_slist_append)(struct curl_slist *list, char const *data) = __wrap_curl_slist_append;
#endif // _WIN32

void __wrap_free(void *const pointer) {
    for (USize i = _ledger_count; i > 0; i -= 1) {
        if (_ledger[i - 1] == pointer) {
            _ledger[i - 1]  = _ledger[_ledger_count - 1];
            _ledger_count  -= 1;

            break;
        }
    }

    __real_free(pointer);
}

/* Zeroes both counters and arms the refusal of the Nth curl_slist_append (0 = unarmed). */
static void _arm(USize const fail_at) {
    _append_calls    = 0;
    _append_fail_at  = fail_at;
    _perform_calls   = 0;
}

/*==============================================================================
 * MARK: - Fixture
 *============================================================================*/

static void _setup(HTTP_Service_OAuth *const oauth) {
    HTTP_Service_OAuth_Provider const provider = {
        .authorize_url  = "https://example.invalid/authorize",
        .client_id      = "client-id",
        .client_secret  = "client-secret",
        .name           = "local",
        .profile_url    = "http://127.0.0.1:1/profile",
        .redirect_uri   = "https://example.invalid/callback",
        .revoke_url     = "http://127.0.0.1:1/revoke",
        .scope          = "openid email",
        .token_url      = "http://127.0.0.1:1/token"
    };

    http_service_oauth_init_1(oauth);
    http_service_oauth_provider_add_1(oauth, &provider);
}

/*==============================================================================
 * MARK: - Cases
 *============================================================================*/

/* One token request with the Nth header append refused: answers "invalid_request" in the module's
 * local-refusal shape (status ERROR, response_code 0), makes both appends, performs nothing and
 * leaves no live block behind. */
static void _expect_token_refusal(Test *const test, HTTP_Service_OAuth *const oauth, bool const refresh, USize const fail_at, char const *const label) {
    USize const live_before = _ledger_count;

    _arm(fail_at);

    HTTP_Service_OAuth_Token token = refresh ? http_service_oauth_refresh(oauth, "local", "refresh-token") :
                                               http_service_oauth_exchange_code_1(oauth, "local", "code");

    test_expect_string(test, label, "invalid_request", string_get_data(&token.error));
    test_expect_true(test, "  status is ERROR (never reached the provider)", token.status == HTTP_CLIENT_STATUS_ERROR);
    test_expect_u(test, "  response_code is 0", 0, token.response_code);
    test_expect_u(test, "  both header appends were attempted", 2, _append_calls);
    test_expect_u(test, "  nothing was sent", 0, _perform_calls);

    http_service_oauth_token_uninit(&token);

    test_expect_u(test, "  nothing left allocated", live_before, _ledger_count);
}

static void _test_token_header_refusals(Test *const test) {
    test_case_begin(test, "token request: a refused header releases everything and sends nothing");

    HTTP_Service_OAuth oauth = DEFAULT_INITIALIZATION;

    _setup(&oauth);

    _arm(0);

    HTTP_Service_OAuth_Token control = http_service_oauth_exchange_code_1(&oauth, "local", "code");

    test_expect_string(test, "control: unarmed, the request reaches the wire and fails as a transport", "transport", string_get_data(&control.error));
    test_expect_u(test, "control: a token request installs 2 headers", 2, _append_calls);
    test_expect_u(test, "control: and performs once", 1, _perform_calls);

    http_service_oauth_token_uninit(&control);

    _expect_token_refusal(test, &oauth, false, 1, "exchange: Accept refused");
    _expect_token_refusal(test, &oauth, false, 2, "exchange: Content-Type refused");
    _expect_token_refusal(test, &oauth, true, 1, "refresh: Accept refused");

    http_service_oauth_uninit(&oauth);

    test_case_end(test);
}

static void _expect_profile_refusal(Test *const test, HTTP_Service_OAuth *const oauth, USize const fail_at, char const *const label) {
    USize const live_before = _ledger_count;

    _arm(fail_at);

    HTTP_Service_OAuth_Profile profile = http_service_oauth_get_profile_2(oauth, "local", "access-token");

    test_expect_string(test, label, "invalid_request", string_get_data(&profile.error));
    test_expect_true(test, "  status is ERROR (never reached the provider)", profile.status == HTTP_CLIENT_STATUS_ERROR);
    test_expect_u(test, "  response_code is 0", 0, profile.response_code);
    test_expect_u(test, "  both header appends were attempted", 2, _append_calls);
    test_expect_u(test, "  nothing was sent", 0, _perform_calls);

    http_service_oauth_profile_uninit(&profile);

    test_expect_u(test, "  nothing left allocated", live_before, _ledger_count);
}

static void _test_profile_header_refusals(Test *const test) {
    test_case_begin(test, "profile request: a refused header releases everything and sends nothing");

    HTTP_Service_OAuth oauth = DEFAULT_INITIALIZATION;

    _setup(&oauth);

    _arm(0);

    HTTP_Service_OAuth_Profile control = http_service_oauth_get_profile_2(&oauth, "local", "access-token");

    test_expect_string(test, "control: unarmed, the request reaches the wire and fails as a transport", "transport", string_get_data(&control.error));
    test_expect_u(test, "control: a profile request installs 2 headers", 2, _append_calls);
    test_expect_u(test, "control: and performs once", 1, _perform_calls);

    http_service_oauth_profile_uninit(&control);

    _expect_profile_refusal(test, &oauth, 1, "Authorization refused");
    _expect_profile_refusal(test, &oauth, 2, "Accept refused");

    http_service_oauth_uninit(&oauth);

    test_case_end(test);
}

static void _test_revoke_header_refusal(Test *const test) {
    test_case_begin(test, "revoke request: a refused header releases everything and sends nothing");

    HTTP_Service_OAuth oauth = DEFAULT_INITIALIZATION;

    _setup(&oauth);

    _arm(0);

    test_expect_false(test, "control: unarmed, the request reaches the wire and is not acknowledged", http_service_oauth_revoke_1(&oauth, "local", "token"));
    test_expect_u(test, "control: a revoke request installs 1 header", 1, _append_calls);
    test_expect_u(test, "control: and performs once", 1, _perform_calls);

    USize const live_before = _ledger_count;

    _arm(1);

    test_expect_false(test, "Content-Type refused: false", http_service_oauth_revoke_1(&oauth, "local", "token"));
    test_expect_u(test, "  the header append was attempted", 1, _append_calls);
    test_expect_u(test, "  nothing was sent", 0, _perform_calls);
    test_expect_u(test, "  nothing left allocated", live_before, _ledger_count);

    http_service_oauth_uninit(&oauth);

    test_case_end(test);
}

static void _test_ledger_sane(Test *const test) {
    test_case_begin(test, "the ledger itself is trustworthy");

    test_expect_u(test, "the ledger never overflowed", 0, _ledger_overflow);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Main
 *============================================================================*/

int main(void) {
    LogConfig const log_config = {
        .level             = LOG_LEVEL_ERROR,
        .stream            = stdout,
        .timestamp_enabled = true,
        .autoflush         = true
    };

    log_init(log_config);

    Test test = test_init("tests/http/service/oauth/test_oom.c");

    test_suite_begin(&test, "http_service_oauth header-refusal sweep");
    _test_token_header_refusals(&test);
    _test_profile_header_refusals(&test);
    _test_revoke_header_refusal(&test);
    _test_ledger_sane(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}