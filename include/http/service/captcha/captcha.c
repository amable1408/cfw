#include <http/service/captcha/captcha.h>

#include <http/query/query.h>
#include <json/json.h>

/*==============================================================================
 * MARK: - Helpers
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
static String _http_service_captcha_string_init(Arena *const allocator)
#else
static String _http_service_captcha_string_init(void)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    if (allocator != nullptr) {
        String const string = string_alloc_init_1(allocator);

        trace_log_pop();

        return string;
    }
#endif // ARENA_IMPLEMENTATION

    String const string = string_init_1();

    trace_log_pop();

    return string;
}

#ifdef ARENA_IMPLEMENTATION
static String _http_service_captcha_char_to_string(char const *const data, Arena *const allocator)
#else
static String _http_service_captcha_char_to_string(char const *const data)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    USize const data_size = char_length(data);

    // An OWNED copy, not string_init_4's VIEW: every caller here hands over an environment
    // string, a literal, or a caller stack buffer, and a view would leave verify_url and secret
    // dangling the moment that buffer went out of scope.
#ifdef ARENA_IMPLEMENTATION
    if (allocator != nullptr) {
        String const string = string_alloc_init_static(data, data_size, allocator);

        trace_log_pop();

        return string;
    }
#endif // ARENA_IMPLEMENTATION

    String const string = string_init_static(data, data_size);

    trace_log_pop();

    return string;
}

// Answers whether the whole of `data` actually landed. A refused allocator degrades
// string_alloc_init_static to the plain EMPTY String (string.c:544-551), so a caller that stores
// an expectation this way would otherwise install nothing and never learn of it.
#ifdef ARENA_IMPLEMENTATION
static bool _http_service_captcha_string_set(String *const self, char const *const data, Arena *const allocator)
#else
static bool _http_service_captcha_string_set(String *const self, char const *const data)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    USize const data_size = char_length(data);

    string_uninit(self);

#ifdef ARENA_IMPLEMENTATION
    *self = _http_service_captcha_char_to_string(data, allocator);
#else
    *self = _http_service_captcha_char_to_string(data);
#endif // ARENA_IMPLEMENTATION

    bool const stored = string_get_size(self) == data_size;

    trace_log_pop();

    return stored;
}

// memory_empty is a NULL check ONLY (memory.c:94), so it never sees an empty string: a "" token
// sailed straight past the guard it looked like it was standing behind. Every emptiness test on
// request-derived text in this module goes through here instead.
static bool _http_service_captcha_char_empty(char const *const data) {
    return data == nullptr || data[0] == '\0';
}

// The enum's whole domain. A provider value is DATA when it arrives from an environment
// variable through a cast, so a constructor refuses an out-of-range one instead of storing it
// and letting provider_url's switch fall through to an empty endpoint later.
static bool _http_service_captcha_provider_valid(HTTP_Service_Captcha_Provider const provider) {
    switch (provider) {
        case HTTP_SERVICE_CAPTCHA_PROVIDER_NONE:      { return true; }
        case HTTP_SERVICE_CAPTCHA_PROVIDER_TURNSTILE: { return true; }
        case HTTP_SERVICE_CAPTCHA_PROVIDER_HCAPTCHA:  { return true; }
        case HTTP_SERVICE_CAPTCHA_PROVIDER_RECAPTCHA: { return true; }
    }

    return false;
}

// The secret travels in the POST body, so a plaintext endpoint puts the deployment's only
// credential on the wire. Scheme comparison is ASCII case-insensitive per RFC 3986.
static bool _http_service_captcha_url_secure(String const *const url) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "url", (void*) url);

    USize const url_size = string_get_size(url);

    if (url_size < CHAR_STATIC_SIZE("https://")) {
        trace_log_pop();

        return false;
    }

    bool const secure = char_compare_iequal_2(string_get_data(url), CHAR_STATIC_SIZE("https://"), "https://", CHAR_STATIC_SIZE("https://"));

    trace_log_pop();

    return secure;
}

#ifdef ARENA_IMPLEMENTATION
static HTTP_Service_Captcha_Result _http_service_captcha_result_init(Arena *const allocator)
#else
static HTTP_Service_Captcha_Result _http_service_captcha_result_init(void)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    HTTP_Service_Captcha_Result const result = {
#ifdef ARENA_IMPLEMENTATION
        .allocator          = allocator,
        .action             = _http_service_captcha_string_init(allocator),
#else
        .action             = _http_service_captcha_string_init(),
#endif // ARENA_IMPLEMENTATION
        .client_status      = HTTP_CLIENT_STATUS_ERROR,
        .code               = 0,
#ifdef ARENA_IMPLEMENTATION
        .error              = _http_service_captcha_string_init(allocator),
        .error_codes        = _http_service_captcha_string_init(allocator),
        .hostname           = _http_service_captcha_string_init(allocator),
#else
        .error              = _http_service_captcha_string_init(),
        .error_codes        = _http_service_captcha_string_init(),
        .hostname           = _http_service_captcha_string_init(),
#endif // ARENA_IMPLEMENTATION
        .provider_success   = false,
#ifdef ARENA_IMPLEMENTATION
        .response           = _http_service_captcha_string_init(allocator),
#else
        .response           = _http_service_captcha_string_init(),
#endif // ARENA_IMPLEMENTATION
        .response_code      = 0,
        .score              = 0.0,
        .status             = HTTP_SERVICE_CAPTCHA_STATUS_ERROR,
        .success            = false
    };

    trace_log_pop();

    return result;
}

#ifdef ARENA_IMPLEMENTATION
static void _http_service_captcha_result_error_set(HTTP_Service_Captcha_Result *const self, char const *const data, Arena *const allocator)
#else
static void _http_service_captcha_result_error_set(HTTP_Service_Captcha_Result *const self, char const *const data)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

#ifdef ARENA_IMPLEMENTATION
    _http_service_captcha_string_set(&self->error, data, allocator);
#else
    _http_service_captcha_string_set(&self->error, data);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();
}

// Copies one string-typed member of the siteverify object. A missing member, a null literal,
// and a member of the wrong type all leave the destination empty rather than erroring: the
// answer comes from the network and cannot be allowed to abort.
#ifdef ARENA_IMPLEMENTATION
static void _http_service_captcha_json_string_copy(String *const self, Json const *const root, char const *const search, Arena *const allocator)
#else
static void _http_service_captcha_json_string_copy(String *const self, Json const *const root, char const *const search)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "root", (void*) root);
    error_check_null(LOG_METADATA, "search", (void*) search);

    Json *const node = json_at_1(root, search);

    if (memory_empty(node) || json_get_type(node) != JSON_TYPE_STRING) {
        trace_log_pop();

        return;
    }

    char const *const value = json_get_value_string_1(node);

    if (_http_service_captcha_char_empty(value)) {
        trace_log_pop();

        return;
    }

#ifdef ARENA_IMPLEMENTATION
    _http_service_captcha_string_set(self, value, allocator);
#else
    _http_service_captcha_string_set(self, value);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();
}

// Joins the provider's "error-codes" array into one comma-separated String. A non-array member,
// an empty array, and non-string elements all leave the destination empty.
static void _http_service_captcha_json_error_codes_copy(String *const self, Json const *const root) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "root", (void*) root);

    Json *const codes = json_at_1(root, "['error-codes']");

    if (memory_empty(codes) || json_get_type(codes) != JSON_TYPE_ARRAY) {
        trace_log_pop();

        return;
    }

    USize const code_count = json_array_get_size(codes);

    for (USize index = 0; index < code_count; index = index + 1) {
        Json *const code = json_array_at(codes, index);

        if (memory_empty(code) || json_get_type(code) != JSON_TYPE_STRING) {
            continue;
        }

        char const *const value = json_get_value_string_1(code);

        if (_http_service_captcha_char_empty(value)) {
            continue;
        }

        if (!string_empty(self)) {
            string_add_last_2(self, ",", CHAR_STATIC_SIZE(","));
        }

        string_add_last_1(self, value);
    }

    trace_log_pop();
}

// Reads the whole siteverify object, not just "success". Answers false when the body is not one:
// an unparseable body, and a well-formed JSON value with no "success" member (an array, a bare
// string, an empty object), are both MALFORMED_RESPONSE to the caller and never a pass.
static bool _http_service_captcha_result_parse(HTTP_Service_Captcha_Result *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (string_empty(&self->response)) {
        trace_log_pop();

        return false;
    }

    Json *json = json_from_4(&self->response);

    if (memory_empty(json)) {
        trace_log_pop();

        return false;
    }

    Json *const success = json_at_1(json, "['success']");

    if (memory_empty(success) || !json_is_bool(success)) {
        json_delete(&json);

        trace_log_pop();

        return false;
    }

    self->provider_success = json_get_value_bool(success);

#ifdef ARENA_IMPLEMENTATION
    _http_service_captcha_json_string_copy(&self->action, json, "['action']", self->allocator);
    _http_service_captcha_json_string_copy(&self->hostname, json, "['hostname']", self->allocator);
#else
    _http_service_captcha_json_string_copy(&self->action, json, "['action']");
    _http_service_captcha_json_string_copy(&self->hostname, json, "['hostname']");
#endif // ARENA_IMPLEMENTATION

    _http_service_captcha_json_error_codes_copy(&self->error_codes, json);

    Json *const score = json_at_1(json, "['score']");

    if (!memory_empty(score)) {
        JsonType const score_type = json_get_type(score);

        // A whole score serializes as an integer node ("score":1), not a real one, and the float
        // getter answers 0.0 for it - which would read as "no score" and fail a minimum.
        if (score_type == JSON_TYPE_REAL) {
            self->score = json_get_value_number_float(score);
        }
        else if (score_type == JSON_TYPE_INTEGER_S || score_type == JSON_TYPE_INTEGER_U) {
            self->score = (FSize) json_get_value_number_int(score);
        }
    }

    json_delete(&json);

    trace_log_pop();

    return true;
}

// Whether the provider's answer satisfies every expectation the deployment configured. Each
// failure names itself in the result's error text so a log reader can tell a wrong-site token
// from a low-score one without the raw body.
static bool _http_service_captcha_expectations_hold(HTTP_Service_Captcha const *const self, HTTP_Service_Captcha_Result *const result) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "result", (void*) result);

    // A DNS name is case-insensitive, so "Example.com" from the provider satisfies "example.com"
    // here; the ACTION below stays byte-exact, being an application-chosen label rather than a
    // hostname. Comparing the hostname exactly would reject a legitimate token on nothing but the
    // provider's choice of capitalization.
    if (!string_empty(&self->expected_hostname) && !string_compare_iequal_4(&self->expected_hostname, &result->hostname)) {
#ifdef ARENA_IMPLEMENTATION
        _http_service_captcha_result_error_set(result, "captcha hostname does not match the expected hostname", self->allocator);
#else
        _http_service_captcha_result_error_set(result, "captcha hostname does not match the expected hostname");
#endif // ARENA_IMPLEMENTATION

        trace_log_pop();

        return false;
    }

    if (!string_empty(&self->expected_action) && !string_compare_equal_4(&self->expected_action, &result->action)) {
#ifdef ARENA_IMPLEMENTATION
        _http_service_captcha_result_error_set(result, "captcha action does not match the expected action", self->allocator);
#else
        _http_service_captcha_result_error_set(result, "captcha action does not match the expected action");
#endif // ARENA_IMPLEMENTATION

        trace_log_pop();

        return false;
    }

    if (self->min_score > 0.0 && result->score < self->min_score) {
#ifdef ARENA_IMPLEMENTATION
        _http_service_captcha_result_error_set(result, "captcha score is below the configured minimum", self->allocator);
#else
        _http_service_captcha_result_error_set(result, "captcha score is below the configured minimum");
#endif // ARENA_IMPLEMENTATION

        trace_log_pop();

        return false;
    }

    trace_log_pop();

    return true;
}

// Translates the transport's own vocabulary into this service's. Only reached when the client
// reported failure, so HTTP_CLIENT_STATUS_OK cannot arrive here.
//
// A client status this switch does not name folds into ERROR - honest today, since ERROR is what
// http/client itself answers for "something else went wrong". When http/client grows a status of
// its own (a TOO_LARGE for the response cap is the obvious next one), give it a case here rather
// than leaving it flattened: http_service_captcha_status_is_outage already calls ERROR an outage,
// so the classification survives, but the log line stops saying which one.
static HTTP_Service_Captcha_Status _http_service_captcha_status_from_client(HTTP_Client_Status const status) {
    switch (status) {
        case HTTP_CLIENT_STATUS_TIMEOUT:     { return HTTP_SERVICE_CAPTCHA_STATUS_TIMEOUT; }
        case HTTP_CLIENT_STATUS_TLS:         { return HTTP_SERVICE_CAPTCHA_STATUS_TLS; }
        case HTTP_CLIENT_STATUS_UNREACHABLE: { return HTTP_SERVICE_CAPTCHA_STATUS_UNREACHABLE; }
        case HTTP_CLIENT_STATUS_ERROR:       { return HTTP_SERVICE_CAPTCHA_STATUS_ERROR; }
        case HTTP_CLIENT_STATUS_OK:          { return HTTP_SERVICE_CAPTCHA_STATUS_ERROR; }
    }

    return HTTP_SERVICE_CAPTCHA_STATUS_ERROR;
}

/*==============================================================================
 * MARK: - API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
bool http_service_captcha_alloc_init_1(HTTP_Service_Captcha *const self, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    bool const success = http_service_captcha_alloc_init_2(self, HTTP_SERVICE_CAPTCHA_PROVIDER_NONE, "", allocator);

    trace_log_pop();

    return success;
}

bool http_service_captcha_alloc_init_2(HTTP_Service_Captcha *const self, HTTP_Service_Captcha_Provider const provider, char const *const secret, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "secret", (void*) secret);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    memory_set(self, sizeof(HTTP_Service_Captcha), 0);

    if (!_http_service_captcha_provider_valid(provider)) {
        trace_log_pop();

        return false;
    }

    char const *const url = http_service_captcha_provider_url(provider);

    self->allocator          = allocator;
    self->client             = nullptr;
    self->connect_timeout_ms = HTTP_SERVICE_CAPTCHA_DEFAULT_CONNECT_TIMEOUT_MS;
    self->enabled            = provider != HTTP_SERVICE_CAPTCHA_PROVIDER_NONE;
    self->expected_action    = _http_service_captcha_string_init(allocator);
    self->expected_hostname  = _http_service_captcha_string_init(allocator);
    self->min_score          = 0.0;
    self->provider           = provider;
    self->secret             = _http_service_captcha_char_to_string(secret, allocator);
    self->timeout_ms         = HTTP_SERVICE_CAPTCHA_DEFAULT_TIMEOUT_MS;
    self->token_max_size     = HTTP_SERVICE_CAPTCHA_TOKEN_MAX_SIZE;
    self->verify_tls         = true;
    self->verify_url         = _http_service_captcha_char_to_string(url, allocator);

    trace_log_pop();

    return true;
}
#endif // ARENA_IMPLEMENTATION

bool http_service_captcha_enabled(HTTP_Service_Captcha const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    bool const enabled = self->enabled;

    trace_log_pop();

    return enabled;
}

bool http_service_captcha_init_1(HTTP_Service_Captcha *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    bool const success = http_service_captcha_init_2(self, HTTP_SERVICE_CAPTCHA_PROVIDER_NONE, "");

    trace_log_pop();

    return success;
}

bool http_service_captcha_init_2(HTTP_Service_Captcha *const self, HTTP_Service_Captcha_Provider const provider, char const *const secret) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "secret", (void*) secret);

    memory_set(self, sizeof(HTTP_Service_Captcha), 0);

    if (!_http_service_captcha_provider_valid(provider)) {
        trace_log_pop();

        return false;
    }

    char const *const url = http_service_captcha_provider_url(provider);

#ifdef ARENA_IMPLEMENTATION
    self->allocator          = nullptr;
#endif // ARENA_IMPLEMENTATION
    self->client             = nullptr;
    self->connect_timeout_ms = HTTP_SERVICE_CAPTCHA_DEFAULT_CONNECT_TIMEOUT_MS;
    self->enabled            = provider != HTTP_SERVICE_CAPTCHA_PROVIDER_NONE;
    self->min_score          = 0.0;
    self->provider           = provider;
    self->timeout_ms         = HTTP_SERVICE_CAPTCHA_DEFAULT_TIMEOUT_MS;
    self->token_max_size     = HTTP_SERVICE_CAPTCHA_TOKEN_MAX_SIZE;
    self->verify_tls         = true;

#ifdef ARENA_IMPLEMENTATION
    self->expected_action    = _http_service_captcha_string_init(nullptr);
    self->expected_hostname  = _http_service_captcha_string_init(nullptr);
    self->secret             = _http_service_captcha_char_to_string(secret, nullptr);
    self->verify_url         = _http_service_captcha_char_to_string(url, nullptr);
#else
    self->expected_action    = _http_service_captcha_string_init();
    self->expected_hostname  = _http_service_captcha_string_init();
    self->secret             = _http_service_captcha_char_to_string(secret);
    self->verify_url         = _http_service_captcha_char_to_string(url);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return true;
}

HTTP_Service_Captcha_Provider http_service_captcha_provider_from_string(char const *const data) {
    trace_log_push(LOG_METADATA);

    HTTP_Service_Captcha_Provider provider = HTTP_SERVICE_CAPTCHA_PROVIDER_NONE;

    // The lenient tier: an unrecognized name folds into NONE rather than being reported. Kept
    // because it reads well in a one-line constructor call, but every caller that can fail
    // startup should be asking the parser instead.
    http_service_captcha_provider_parse(data, &provider);

    trace_log_pop();

    return provider;
}

bool http_service_captcha_provider_parse(char const *const data, HTTP_Service_Captcha_Provider *const provider_out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "provider_out", (void*) provider_out);

    // Null and empty are the unset variable, "none"/"off" the deliberate disable: all four are a
    // legitimate configuration and answer true. Only a name that was MEANT to be a provider and
    // is not one answers false - that is the case a deployment must never fail open on.
    if (_http_service_captcha_char_empty(data)) {
        *provider_out = HTTP_SERVICE_CAPTCHA_PROVIDER_NONE;

        trace_log_pop();

        return true;
    }

    USize const data_size = char_length(data);

    if (char_compare_iequal_2(data, data_size, "hcaptcha", CHAR_STATIC_SIZE("hcaptcha"))) {
        *provider_out = HTTP_SERVICE_CAPTCHA_PROVIDER_HCAPTCHA;

        trace_log_pop();

        return true;
    }

    if (char_compare_iequal_2(data, data_size, "none", CHAR_STATIC_SIZE("none")) ||
        char_compare_iequal_2(data, data_size, "off", CHAR_STATIC_SIZE("off"))) {
        *provider_out = HTTP_SERVICE_CAPTCHA_PROVIDER_NONE;

        trace_log_pop();

        return true;
    }

    if (char_compare_iequal_2(data, data_size, "recaptcha", CHAR_STATIC_SIZE("recaptcha"))) {
        *provider_out = HTTP_SERVICE_CAPTCHA_PROVIDER_RECAPTCHA;

        trace_log_pop();

        return true;
    }

    if (char_compare_iequal_2(data, data_size, "turnstile", CHAR_STATIC_SIZE("turnstile"))) {
        *provider_out = HTTP_SERVICE_CAPTCHA_PROVIDER_TURNSTILE;

        trace_log_pop();

        return true;
    }

    // *provider_out is left alone: a caller that pre-set a default keeps it, and one that reads
    // the variable anyway after ignoring false does not get a fabricated NONE.
    trace_log_pop();

    return false;
}

char* http_service_captcha_provider_url(HTTP_Service_Captcha_Provider const provider) {
    trace_log_push(LOG_METADATA);

    switch (provider) {
        case HTTP_SERVICE_CAPTCHA_PROVIDER_TURNSTILE: { trace_log_pop(); return HTTP_SERVICE_CAPTCHA_TURNSTILE_URL; }
        case HTTP_SERVICE_CAPTCHA_PROVIDER_HCAPTCHA:  { trace_log_pop(); return HTTP_SERVICE_CAPTCHA_HCAPTCHA_URL; }
        case HTTP_SERVICE_CAPTCHA_PROVIDER_RECAPTCHA: { trace_log_pop(); return HTTP_SERVICE_CAPTCHA_RECAPTCHA_URL; }
        case HTTP_SERVICE_CAPTCHA_PROVIDER_NONE:      { trace_log_pop(); return ""; }
    }

    trace_log_pop();

    return "";
}

void http_service_captcha_result_uninit(HTTP_Service_Captcha_Result *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    string_uninit(&self->action);
    string_uninit(&self->error);
    string_uninit(&self->error_codes);
    string_uninit(&self->hostname);
    string_uninit(&self->response);

#ifdef ARENA_IMPLEMENTATION
    self->allocator         = nullptr;
#endif // ARENA_IMPLEMENTATION
    self->client_status     = HTTP_CLIENT_STATUS_ERROR;
    self->code              = 0;
    self->provider_success  = false;
    self->response_code     = 0;
    self->score             = 0.0;
    self->status            = HTTP_SERVICE_CAPTCHA_STATUS_ERROR;
    self->success           = false;

    trace_log_pop();
}

void http_service_captcha_set_enabled(HTTP_Service_Captcha *const self, bool const enabled) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    self->enabled = enabled;

    trace_log_pop();
}

// An expectation that fails to store is the dangerous shape: the service stays valid() and keeps
// verifying, only without the check the deployment asked for. The outcome propagates so a caller
// can fail startup instead of running an unguarded service.
bool http_service_captcha_set_expected_action(HTTP_Service_Captcha *const self, char const *const action) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "action", (void*) action);

#ifdef ARENA_IMPLEMENTATION
    bool const stored = _http_service_captcha_string_set(&self->expected_action, action, self->allocator);
#else
    bool const stored = _http_service_captcha_string_set(&self->expected_action, action);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return stored;
}

bool http_service_captcha_set_expected_hostname(HTTP_Service_Captcha *const self, char const *const hostname) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "hostname", (void*) hostname);

#ifdef ARENA_IMPLEMENTATION
    bool const stored = _http_service_captcha_string_set(&self->expected_hostname, hostname, self->allocator);
#else
    bool const stored = _http_service_captcha_string_set(&self->expected_hostname, hostname);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return stored;
}

void http_service_captcha_set_min_score(HTTP_Service_Captcha *const self, FSize const min_score) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    self->min_score = min_score;

    trace_log_pop();
}

bool http_service_captcha_set_provider(HTTP_Service_Captcha *const self, HTTP_Service_Captcha_Provider const provider) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    // The same environment-cast path the constructors already refuse. Storing 42 here would leave
    // an instance that is valid() and POSTs to "" - failing closed only by accident, one request
    // later and one layer down, in http_client rather than here.
    if (!_http_service_captcha_provider_valid(provider)) {
        trace_log_pop();

        return false;
    }

    self->provider = provider;
    self->enabled  = provider != HTTP_SERVICE_CAPTCHA_PROVIDER_NONE;

#ifdef ARENA_IMPLEMENTATION
    bool const stored = _http_service_captcha_string_set(&self->verify_url, http_service_captcha_provider_url(provider), self->allocator);
#else
    bool const stored = _http_service_captcha_string_set(&self->verify_url, http_service_captcha_provider_url(provider));
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return stored;
}

void http_service_captcha_set_secret(HTTP_Service_Captcha *const self, char const *const secret) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "secret", (void*) secret);

#ifdef ARENA_IMPLEMENTATION
    _http_service_captcha_string_set(&self->secret, secret, self->allocator);
#else
    _http_service_captcha_string_set(&self->secret, secret);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();
}

void http_service_captcha_set_timeouts(HTTP_Service_Captcha *const self, USize const connect_timeout_ms, USize const timeout_ms) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_non_value_uint(LOG_METADATA, "connect_timeout_ms", connect_timeout_ms);
    error_check_non_value_uint(LOG_METADATA, "timeout_ms", timeout_ms);

    self->connect_timeout_ms = connect_timeout_ms;
    self->timeout_ms         = timeout_ms;

    trace_log_pop();
}

void http_service_captcha_set_token_max_size(HTTP_Service_Captcha *const self, USize const token_max_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    self->token_max_size = token_max_size;

    trace_log_pop();
}

bool http_service_captcha_set_url(HTTP_Service_Captcha *const self, char const *const url) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "url", (void*) url);

    if (_http_service_captcha_char_empty(url)) {
        trace_log_pop();

        return false;
    }

    USize const url_size = char_length(url);
    bool const  secure   = url_size >= CHAR_STATIC_SIZE("https://") && char_compare_iequal_2(url, CHAR_STATIC_SIZE("https://"), "https://", CHAR_STATIC_SIZE("https://"));

    if (!secure && self->verify_tls) {
        trace_log_pop();

        return false;
    }

#ifdef ARENA_IMPLEMENTATION
    _http_service_captcha_string_set(&self->verify_url, url, self->allocator);
#else
    _http_service_captcha_string_set(&self->verify_url, url);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return true;
}

void http_service_captcha_set_verify_tls(HTTP_Service_Captcha *const self, bool const verify_tls) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    self->verify_tls = verify_tls;

    trace_log_pop();
}

// The switch is deliberately exhaustive with no default: a status added to the enum later stops
// this build until somebody decides which side of the 403/503 line it falls on, which is exactly
// what a hand-rolled set at each call site could never do.
bool http_service_captcha_status_is_outage(HTTP_Service_Captcha_Status const status) {
    switch (status) {
        case HTTP_SERVICE_CAPTCHA_STATUS_ERROR:              { return true; }
        case HTTP_SERVICE_CAPTCHA_STATUS_MALFORMED_RESPONSE: { return true; }
        case HTTP_SERVICE_CAPTCHA_STATUS_MISCONFIGURED:      { return true; }
        case HTTP_SERVICE_CAPTCHA_STATUS_OK:                 { return false; }
        case HTTP_SERVICE_CAPTCHA_STATUS_PROVIDER_ERROR:     { return true; }
        case HTTP_SERVICE_CAPTCHA_STATUS_SKIPPED:            { return false; }
        case HTTP_SERVICE_CAPTCHA_STATUS_TIMEOUT:            { return true; }
        case HTTP_SERVICE_CAPTCHA_STATUS_TLS:                { return true; }
        case HTTP_SERVICE_CAPTCHA_STATUS_TOKEN_INVALID:      { return false; }
        case HTTP_SERVICE_CAPTCHA_STATUS_TOKEN_REJECTED:     { return false; }
        case HTTP_SERVICE_CAPTCHA_STATUS_TOKEN_TOO_LONG:     { return false; }
        case HTTP_SERVICE_CAPTCHA_STATUS_UNREACHABLE:        { return true; }
    }

    // An out-of-range status can only arrive through a cast, and the safer half of the answer is
    // "this is ours, not the client's": a 503 misattributes blame, a 403 accuses the user.
    return true;
}

void http_service_captcha_uninit(HTTP_Service_Captcha *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (self->client != nullptr) {
        http_client_delete(&self->client);
    }

    string_uninit(&self->expected_action);
    string_uninit(&self->expected_hostname);
    string_uninit(&self->secret);
    string_uninit(&self->verify_url);

#ifdef ARENA_IMPLEMENTATION
    self->allocator          = nullptr;
#endif // ARENA_IMPLEMENTATION
    self->client             = nullptr;
    self->connect_timeout_ms = 0;
    self->enabled            = false;
    self->min_score          = 0.0;
    self->provider           = HTTP_SERVICE_CAPTCHA_PROVIDER_NONE;
    self->timeout_ms         = 0;
    self->token_max_size     = 0;
    self->verify_tls         = false;

    trace_log_pop();
}

bool http_service_captcha_valid(HTTP_Service_Captcha const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    // The timeouts are deliberately NOT re-checked here: http_service_captcha_set_timeouts
    // aborts on zero and every constructor sets a non-zero default, so a zero could only reach
    // this point through undefined behaviour a runtime branch cannot honestly report on.
    bool const success = self->enabled                       &&
        self->provider != HTTP_SERVICE_CAPTCHA_PROVIDER_NONE &&
        !string_empty(&self->secret)                         &&
        !string_empty(&self->verify_url)                     &&
        (!self->verify_tls || _http_service_captcha_url_secure(&self->verify_url));

    trace_log_pop();

    return success;
}

HTTP_Service_Captcha_Result http_service_captcha_verify(HTTP_Service_Captcha *const self, char const *const token, char const *const remote_ip) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* `token` is request data - a JSON field that is not a string reads as nullptr - so it is
     * never error_checked: _http_service_captcha_char_empty below answers null and empty the
     * same way, as TOKEN_INVALID. */

#ifdef ARENA_IMPLEMENTATION
    HTTP_Service_Captcha_Result result = _http_service_captcha_result_init(self->allocator);
#else
    HTTP_Service_Captcha_Result result = _http_service_captcha_result_init();
#endif // ARENA_IMPLEMENTATION

    if (!self->enabled) {
        // provider_success stays false on purpose: no provider spoke, so there is no raw
        // provider value to report. `status` is what says why this passed.
        result.status   = HTTP_SERVICE_CAPTCHA_STATUS_SKIPPED;
        result.success  = true;

        trace_log_pop();

        return result;
    }

    if (!http_service_captcha_valid(self)) {
        result.status = HTTP_SERVICE_CAPTCHA_STATUS_MISCONFIGURED;

#ifdef ARENA_IMPLEMENTATION
        _http_service_captcha_result_error_set(&result, "captcha service is not configured for verification", self->allocator);
#else
        _http_service_captcha_result_error_set(&result, "captcha service is not configured for verification");
#endif // ARENA_IMPLEMENTATION

        trace_log_pop();

        return result;
    }

    if (_http_service_captcha_char_empty(token)) {
        result.status = HTTP_SERVICE_CAPTCHA_STATUS_TOKEN_INVALID;

#ifdef ARENA_IMPLEMENTATION
        _http_service_captcha_result_error_set(&result, "captcha token is empty", self->allocator);
#else
        _http_service_captcha_result_error_set(&result, "captcha token is empty");
#endif // ARENA_IMPLEMENTATION

        trace_log_pop();

        return result;
    }

    // Before the payload is built and before any socket is opened: an unbounded client token
    // must not become an unbounded POST to a third party on this deployment's timeout budget.
    if (self->token_max_size > 0 && char_length(token) > self->token_max_size) {
        result.status = HTTP_SERVICE_CAPTCHA_STATUS_TOKEN_TOO_LONG;

#ifdef ARENA_IMPLEMENTATION
        _http_service_captcha_result_error_set(&result, "captcha token exceeds the configured maximum size", self->allocator);
#else
        _http_service_captcha_result_error_set(&result, "captcha token exceeds the configured maximum size");
#endif // ARENA_IMPLEMENTATION

        trace_log_pop();

        return result;
    }

    if (self->client == nullptr) {
        self->client = http_client_new();
    }

#ifdef ARENA_IMPLEMENTATION
    String payload = _http_service_captcha_string_init(self->allocator);
#else
    String payload = _http_service_captcha_string_init();
#endif // ARENA_IMPLEMENTATION

    // An empty remote_ip sends NO remoteip field, rather than an empty one: providers treat a
    // present-but-empty address differently from an absent one.
    bool const payload_success = http_query_form_add_1(&payload, "secret", string_get_data(&self->secret)) &&
        http_query_form_add_1(&payload, "response", token)                                                 &&
        (_http_service_captcha_char_empty(remote_ip) || http_query_form_add_1(&payload, "remoteip", remote_ip));

    if (!payload_success) {
        result.status = HTTP_SERVICE_CAPTCHA_STATUS_ERROR;

#ifdef ARENA_IMPLEMENTATION
        _http_service_captcha_result_error_set(&result, "captcha payload encoding failed", self->allocator);
#else
        _http_service_captcha_result_error_set(&result, "captcha payload encoding failed");
#endif // ARENA_IMPLEMENTATION
        string_uninit(&payload);

        trace_log_pop();

        return result;
    }

    // The handle is REUSED across verifications, so its request header list has to be cleared
    // first - otherwise each call appends another Content-Type to the same list.
    http_client_header_clear(self->client);
    http_client_set_connect_timeout_ms(self->client, self->connect_timeout_ms);
    http_client_set_max_response_size(self->client, HTTP_SERVICE_CAPTCHA_MAX_RESPONSE_SIZE);
    http_client_set_timeout_ms(self->client, self->timeout_ms);
    http_client_set_verify_tls(self->client, self->verify_tls);
    http_client_header_add(self->client, "Content-Type: application/x-www-form-urlencoded");

    HTTP_Client_Result client_result = http_client_post_4(self->client, string_get_data(&self->verify_url), &payload, &result.response);

    result.client_status    = client_result.status;
    result.code             = (I32) client_result.code;
    result.response_code    = client_result.response_code;

    if (!client_result.success) {
        result.status = _http_service_captcha_status_from_client(client_result.status);

        // An EMPTY String's data pointer is null, and the error setter's error_check_null would
        // ABORT on it - a transport failure that happened to carry no text must not take the
        // process down.
        //
        // The text is the backend's VERBATIM, and it can name the endpoint ("Could not resolve
        // host: ..."). That is harmless precisely because the secret travels in the body and
        // never in the URL - keep it that way, or this field starts leaking it into logs.
        char const *const client_error = string_empty(&client_result.error) ? "captcha transport failed" : string_get_data(&client_result.error);

#ifdef ARENA_IMPLEMENTATION
        _http_service_captcha_result_error_set(&result, client_error, self->allocator);
#else
        _http_service_captcha_result_error_set(&result, client_error);
#endif // ARENA_IMPLEMENTATION
    }
    else if (result.response_code < 200 || result.response_code >= 300) {
        result.status = HTTP_SERVICE_CAPTCHA_STATUS_PROVIDER_ERROR;

#ifdef ARENA_IMPLEMENTATION
        _http_service_captcha_result_error_set(&result, "captcha provider answered a non-2xx status", self->allocator);
#else
        _http_service_captcha_result_error_set(&result, "captcha provider answered a non-2xx status");
#endif // ARENA_IMPLEMENTATION
    }
    else if (!_http_service_captcha_result_parse(&result)) {
        result.status = HTTP_SERVICE_CAPTCHA_STATUS_MALFORMED_RESPONSE;

#ifdef ARENA_IMPLEMENTATION
        _http_service_captcha_result_error_set(&result, "captcha provider answer is not a siteverify object", self->allocator);
#else
        _http_service_captcha_result_error_set(&result, "captcha provider answer is not a siteverify object");
#endif // ARENA_IMPLEMENTATION
    }
    else if (!result.provider_success) {
        result.status = HTTP_SERVICE_CAPTCHA_STATUS_TOKEN_REJECTED;

#ifdef ARENA_IMPLEMENTATION
        _http_service_captcha_result_error_set(&result, "captcha provider rejected the token", self->allocator);
#else
        _http_service_captcha_result_error_set(&result, "captcha provider rejected the token");
#endif // ARENA_IMPLEMENTATION
    }
    else if (!_http_service_captcha_expectations_hold(self, &result)) {
        result.status = HTTP_SERVICE_CAPTCHA_STATUS_TOKEN_REJECTED;
    }
    else {
        result.status   = HTTP_SERVICE_CAPTCHA_STATUS_OK;
        result.success  = true;
    }

    http_client_result_uninit(&client_result);
    string_uninit(&payload);

    trace_log_pop();

    return result;
}