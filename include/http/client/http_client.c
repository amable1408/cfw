#include <http/client/http_client.h>

#define _HTTP_CLIENT_CONNECT_TIMEOUT_DEFAULT_MS   2000
#define _HTTP_CLIENT_RESPONSE_BODY_INITIAL_SIZE   8192
#define _HTTP_CLIENT_RESPONSE_HEADER_BYTES_MAX    (256ULL * 1024ULL)
#define _HTTP_CLIENT_RESPONSE_HEADER_LINES_MAX    256
#define _HTTP_CLIENT_RESPONSE_SIZE_DEFAULT         (16ULL * 1024ULL * 1024ULL)
#define _HTTP_CLIENT_STATUS_PREFIX                "HTTP/"
#define _HTTP_CLIENT_STATUS_PREFIX_SIZE            5
#define _HTTP_CLIENT_TIMEOUT_DEFAULT_MS            20000

/* Per-handle state. CURLOPT_WRITEDATA/HEADERDATA point at `self` for the life of
 * the handle (set once in http_client_new), so the write/header callbacks reach
 * every field below without a CURLOPT_PRIVATE lookup. Nothing here is shared
 * with any other HTTP_Client - one client per thread, and two clients on one
 * thread never touch each other's state. */
struct HTTP_Client {
    CURL               *curl;
    struct curl_slist  *headers;          /* request headers, http_client_header_add/_clear */
    struct curl_slist  *response_headers; /* last response's headers, rebuilt at each status line */
    String              response_body;    /* used when a request is called with response == nullptr */
    String             *active_response;  /* target of the write callback for the in-flight request */
    char                error_buffer[CURL_ERROR_SIZE];
    USize               response_status;
    USize               max_response_size;
    USize               response_size;        /* bytes written to active_response so far */
    USize               response_header_bytes; /* bytes of response_headers so far */
    USize               response_header_lines; /* lines of response_headers so far */
    bool                body_capped;      /* max_response_size was exceeded this request */
    bool                header_capped;    /* the header budget was exceeded this request */
    bool                arena_capped;     /* an arena-backed active_response refused to grow this request */
};

static String _http_client_version = DEFAULT_INITIALIZATION;
static pthread_once_t _http_client_global_once = PTHREAD_ONCE_INIT;

/*==============================================================================
 * MARK: - Helpers
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
static HTTP_Client_Result _http_client_result_alloc_init(Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    HTTP_Client_Result const result = {
        .allocator      = allocator,
        .code           = CURLE_OK,
        .error          = string_alloc_init_1(allocator),
        .response_code  = 0,
        .status         = HTTP_CLIENT_STATUS_OK,
        .success        = false
    };

    trace_log_pop();

    return result;
}
#endif // ARENA_IMPLEMENTATION

static HTTP_Client_Result _http_client_result_init(void) {
    trace_log_push(LOG_METADATA);

    HTTP_Client_Result const result = {
#ifdef ARENA_IMPLEMENTATION
        .allocator      = nullptr,
#endif // ARENA_IMPLEMENTATION
        .code           = CURLE_OK,
        .error          = string_init_1(),
        .response_code  = 0,
        .status         = HTTP_CLIENT_STATUS_OK,
        .success        = false
    };

    trace_log_pop();

    return result;
}

static void _http_client_result_error_set(HTTP_Client_Result *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    char const *const error = memory_empty(data) ? "http client request failed" : data;

    string_uninit(&self->error);

#ifdef ARENA_IMPLEMENTATION
    if (!memory_empty(self->allocator)) {
        self->error = string_alloc_init_4((char*) error, char_length(error), self->allocator);

        trace_log_pop();

        return;
    }
#endif // ARENA_IMPLEMENTATION

    self->error = string_init_static((char*) error, char_length(error));

    trace_log_pop();
}

/* Clamps a USize down to LONG_MAX before a (long) cast - libcurl's *_MS and
 * MAXREDIRS setopts take a plain `long`, which is 32-bit on an LLP64 build
 * (this tree's Windows target); a value beyond that would otherwise narrow
 * into an implementation-defined (commonly negative) result instead of the
 * largest timeout/redirect-count curl can actually accept. */
static long _http_client_long_clamp(USize const value) {
    return value > (USize) LONG_MAX ? LONG_MAX : (long) value;
}

static HTTP_Client_Status _http_client_status_from_code(CURLcode const code) {
    switch (code) {
        case CURLE_OK:
            return HTTP_CLIENT_STATUS_OK;

        case CURLE_COULDNT_CONNECT:
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_RESOLVE_PROXY:
            return HTTP_CLIENT_STATUS_UNREACHABLE;

        case CURLE_OPERATION_TIMEDOUT:
            return HTTP_CLIENT_STATUS_TIMEOUT;

        case CURLE_PEER_FAILED_VERIFICATION: /* == CURLE_SSL_CACERT, same enum value (deprecated alias) */
        case CURLE_SSL_CACERT_BADFILE:
        case CURLE_SSL_CERTPROBLEM:
        case CURLE_SSL_CIPHER:
        case CURLE_SSL_CLIENTCERT:
        case CURLE_SSL_CONNECT_ERROR:
        case CURLE_SSL_CRL_BADFILE:
        case CURLE_SSL_INVALIDCERTSTATUS:
        case CURLE_SSL_ISSUER_ERROR:
        case CURLE_SSL_PINNEDPUBKEYNOTMATCH:
            return HTTP_CLIENT_STATUS_TLS;

        default:
            return HTTP_CLIENT_STATUS_ERROR;
    }
}

/* Resets the response header list and its byte/line counters. Called at the
 * start of every request and again from the header callback at each new status
 * line, so a redirect hop or auth challenge only ever exposes its own headers. */
static void _http_client_response_headers_reset(HTTP_Client *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (!memory_empty(self->response_headers)) {
        curl_slist_free_all(self->response_headers);

        self->response_headers = nullptr;
    }

    self->response_header_bytes = 0;
    self->response_header_lines = 0;
    self->header_capped         = false;

    trace_log_pop();
}

static USize _http_client_write_callback(char *const contents, USize const size, USize const count, void *const userdata) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "contents", (void*) contents);
    error_check_null(LOG_METADATA, "userdata", (void*) userdata);

    HTTP_Client *const self       = (HTTP_Client*) userdata;
    USize const        size_total = size * count;

    /* Not an abort: a raw-handle consumer (http_client_get_handle) can drive
     * curl_easy_perform before any request has run through this module's own
     * API, in which case active_response is still nullptr. Fail the transfer
     * (CURLE_WRITE_ERROR) rather than let string_add_last_2 dereference it. */
    if (memory_empty(self->active_response)) {
        trace_log_pop();

        return 0;
    }

    /* Returning 0 here is NOT the error signal - libcurl only aborts the transfer
     * when the callback's return is LESS than size_total; 0 == size_total for an
     * empty chunk, so this is a normal, successful no-op, never a write failure. */
    if (size_total == 0) {
        trace_log_pop();

        return 0;
    }

    if (self->max_response_size > 0 && self->response_size + size_total > self->max_response_size) {
        self->body_capped = true;

        trace_log_pop();

        return 0;
    }

    /* An arena-backed active_response can silently refuse to grow (string_add_2's
     * documented refusal on a full arena) rather than abort - the append is then a
     * no-op and size stays put. Detect that here rather than trusting the call: a
     * blind size += size_total would tell curl the write succeeded while the bytes
     * were actually dropped. */
    USize const size_before = string_get_size(self->active_response);

    string_add_last_2(self->active_response, contents, size_total);

    if (string_get_size(self->active_response) - size_before != size_total) {
        self->arena_capped = true;

        trace_log_pop();

        return 0;
    }

    self->response_size += size_total;

    trace_log_pop();

    return size_total;
}

/* libcurl hands each response header line (CRLF included, the blank terminator
 * too) to this callback; keep the non-blank ones as owned, terminated copies on
 * the handle's response header list, bounded by a fixed line/byte budget so a
 * hostile server cannot grow it without limit. Returning less than the byte
 * count aborts the transfer (CURLE_WRITE_ERROR), so every path returns it in
 * full except the two budget-exceeded cases. */
static USize _http_client_header_callback(char *const contents, USize const size, USize const count, void *const userdata) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "contents", (void*) contents);
    error_check_null(LOG_METADATA, "userdata", (void*) userdata);

    HTTP_Client *const self       = (HTTP_Client*) userdata;
    USize const        size_total = size * count;
    USize              line_size  = size_total;

    while (line_size > 0 && (contents[line_size - 1] == '\r' || contents[line_size - 1] == '\n')) {
        line_size -= 1;
    }

    /* A status line begins a NEW response, and only the last response's headers
     * may be visible to the caller: redirect hops, proxy CONNECT, and auth
     * challenges each deliver their own set, so drop what came before. */
    if (line_size >= _HTTP_CLIENT_STATUS_PREFIX_SIZE && char_compare_iequal_2(contents, _HTTP_CLIENT_STATUS_PREFIX_SIZE, _HTTP_CLIENT_STATUS_PREFIX, _HTTP_CLIENT_STATUS_PREFIX_SIZE)) {
        _http_client_response_headers_reset(self);
    }

    if (line_size == 0) {
        trace_log_pop();

        return size_total;
    }

    if (self->response_header_lines + 1 > _HTTP_CLIENT_RESPONSE_HEADER_LINES_MAX
        || self->response_header_bytes + line_size > _HTTP_CLIENT_RESPONSE_HEADER_BYTES_MAX) {
        self->header_capped = true;

        trace_log_pop();

        return 0;
    }

    String line = string_init_1();

    string_add_last_2(&line, contents, line_size);

    /* curl_slist_append returns null WITHOUT freeing the list it was given:
     * assigning that null straight back would drop every header captured so
     * far. */
    struct curl_slist *const appended = curl_slist_append(self->response_headers, string_get_data(&line));

    if (memory_empty((void*) appended)) {
        /* The transfer still succeeds, and `header_capped` deliberately stays false - that flag
         * means "the budget was exceeded" and selects its own error string. Without this line a
         * dropped Set-Cookie or Location would be indistinguishable from one the server never
         * sent, since response_header_find simply misses it. */
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_client: curl_slist_append failed, one response header line was dropped");
    }
    else {
        self->response_headers = appended;

        self->response_header_bytes += line_size;
        self->response_header_lines += 1;
    }

    string_uninit(&line);

    trace_log_pop();

    return size_total;
}

/* Process-wide one-time init, run exactly once via pthread_once: brings up
 * curl's global state and builds the shared default user-agent string. Both are
 * process-global and unsafe to (re)initialize per client from worker threads,
 * so they live here. See the header's curl_global_init note for the
 * refcount/compatibility contract with an owning program or websocket/client. */
static void _http_client_global_init(void) {
    trace_log_push(LOG_METADATA);

    curl_global_init(CURL_GLOBAL_ALL);

    curl_version_info_data *const curl_version = curl_version_info(CURLVERSION_NOW);

    error_check_null(LOG_METADATA, "curl_version", (void*) curl_version);

    _http_client_version = string_init_2(
        memory_fit_size(sizeof(Byte),
        CHAR_STATIC_SIZE("cfw-http-client/") + CHAR_STATIC_SIZE("00.00.00") + CHAR_END_CHARACTER));

    string_add_last_2(&_http_client_version, "cfw-http-client/", CHAR_STATIC_SIZE("cfw-http-client/"));
    string_add_last_1(&_http_client_version, (char*) curl_version->version);

    trace_log_pop();
}

static String _http_client_escape(HTTP_Client const *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Delegates to http/query rather than curl_easy_escape: byte-for-byte the
     * same output (RFC 3986 unreserved set, uppercase hex - pinned over every
     * byte 0x00-0xFF in tests/http/client/test_all.c), one pass into the
     * String this returns instead of a curl malloc + char_length + copy +
     * curl_free per call, and one encoder in the tree instead of two. `self`
     * stays in the signature - it was never consulted by libcurl either - and
     * retires with the escape family's next MAJOR.
     *
     * The refusal threshold moves with the delegate: a data_size over
     * HTTP_QUERY_ENCODE_MAX_SIZE (1 MiB) answers an empty String where the old
     * bound was INT_MAX. Both are far above any URL this client builds, and
     * both refuse rather than truncate. */
    String string = string_init_1();

    /* An empty String is the answer for BOTH an empty input and a refused one, so the
     * refusal is logged: without it a caller handed a 1 MiB+ value (or a value aliasing the
     * fresh destination, which cannot happen here) gets an empty escape that reads exactly
     * like an empty input, and the truncated URL only fails later at the server. */
    if (!http_query_encode_2(&string, data, data_size) && data_size > 0) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_client_escape: value refused by http_query_encode_2, empty String returned");
    }

    trace_log_pop();

    return string;
}

static void _http_client_default_options_set(HTTP_Client *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    curl_easy_setopt(self->curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(self->curl, CURLOPT_CONNECTTIMEOUT_MS, (long) _HTTP_CLIENT_CONNECT_TIMEOUT_DEFAULT_MS);
    curl_easy_setopt(self->curl, CURLOPT_ERRORBUFFER, self->error_buffer);
    curl_easy_setopt(self->curl, CURLOPT_EXPECT_100_TIMEOUT_MS, 0L);
    curl_easy_setopt(self->curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(self->curl, CURLOPT_HEADERDATA, (void*) self);
    curl_easy_setopt(self->curl, CURLOPT_HEADERFUNCTION, _http_client_header_callback);
    curl_easy_setopt(self->curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
    curl_easy_setopt(self->curl, CURLOPT_HTTP_VERSION, (long) CURL_HTTP_VERSION_2TLS);
    curl_easy_setopt(self->curl, CURLOPT_MAXREDIRS, 0L);
    curl_easy_setopt(self->curl, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
    curl_easy_setopt(self->curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(self->curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(self->curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(self->curl, CURLOPT_TIMEOUT_MS, (long) _HTTP_CLIENT_TIMEOUT_DEFAULT_MS);
    curl_easy_setopt(self->curl, CURLOPT_UNRESTRICTED_AUTH, 0L);
    curl_easy_setopt(self->curl, CURLOPT_USERAGENT, string_get_data(&_http_client_version));
    curl_easy_setopt(self->curl, CURLOPT_WRITEDATA, (void*) self);
    curl_easy_setopt(self->curl, CURLOPT_WRITEFUNCTION, _http_client_write_callback);

    trace_log_pop();
}

/* Shared perform path for every request method: the caller has already set the
 * method-specific options (HTTPGET/POST+POSTFIELDS/CUSTOMREQUEST/NOBODY) on
 * self->curl. Builds the Result from the CURLcode and response status, and
 * names the response/header cap explicitly when that is what failed the
 * transfer. `response` may be nullptr to use the handle's own body. */
static HTTP_Client_Result _http_client_perform(HTTP_Client *const self, char const *const url, String *const response) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "url", (void*) url);

    String *const target = response != nullptr ? response : &self->response_body;

#ifdef ARENA_IMPLEMENTATION
    HTTP_Client_Result result = !memory_empty(target->allocator) ?
        _http_client_result_alloc_init(target->allocator) :
        _http_client_result_init();
#else
    HTTP_Client_Result result = _http_client_result_init();
#endif // ARENA_IMPLEMENTATION

    string_clear(target);

    self->active_response  = target;
    self->response_size    = 0;
    self->body_capped      = false;
    self->arena_capped     = false;
    self->error_buffer[0]  = '\0';

    _http_client_response_headers_reset(self);

    /* Re-assert every option a raw-handle consumer (http_client_get_handle) may
     * have overridden - the header promises these are restored by the next call
     * made through this module's own API, so do it unconditionally rather than
     * trusting a prior state. */
    curl_easy_setopt(self->curl, CURLOPT_ERRORBUFFER, self->error_buffer);
    curl_easy_setopt(self->curl, CURLOPT_HEADERDATA, (void*) self);
    curl_easy_setopt(self->curl, CURLOPT_HEADERFUNCTION, _http_client_header_callback);
    curl_easy_setopt(self->curl, CURLOPT_WRITEDATA, (void*) self);
    curl_easy_setopt(self->curl, CURLOPT_WRITEFUNCTION, _http_client_write_callback);
    curl_easy_setopt(self->curl, CURLOPT_HTTPHEADER, self->headers);
    curl_easy_setopt(self->curl, CURLOPT_URL, url);

    CURLcode const code          = curl_easy_perform(self->curl);
    long           response_code = 0;

    curl_easy_getinfo(self->curl, CURLINFO_RESPONSE_CODE, &response_code);

    self->response_status = response_code > 0 ? (USize) response_code : 0;

    result.code           = code;
    result.response_code  = self->response_status;
    result.status         = _http_client_status_from_code(code);
    result.success        = code == CURLE_OK;

    if (code != CURLE_OK) {
        char const *error = self->error_buffer[0] != '\0' ? self->error_buffer : curl_easy_strerror(code);

        if (code == CURLE_WRITE_ERROR && self->body_capped) {
            error = "response body exceeded the configured max_response_size cap";
        } else if (code == CURLE_WRITE_ERROR && self->header_capped) {
            error = "response header budget exceeded (too many lines or bytes)";
        } else if (code == CURLE_WRITE_ERROR && self->arena_capped) {
            error = "response body arena refused to grow (arena capacity exceeded)";
        }

        _http_client_result_error_set(&result, error);
    }

    /* Never leave the handle holding a pointer into a caller's stack/local String
     * (active_response) or a caller's payload buffer (POSTFIELDS) past the end of
     * this call: a raw-handle consumer (http_client_get_handle) driving
     * curl_easy_perform itself, without going through this module's request
     * functions again, must not have curl read or write through either. */
    self->active_response = nullptr;

    curl_easy_setopt(self->curl, CURLOPT_POSTFIELDS, (char*) nullptr);
    curl_easy_setopt(self->curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t) 0);

    /* CURLOPT_POSTFIELDS "implies CURLOPT_POST" per libcurl's own docs - the
     * setopt above flips the handle's internal method to POST regardless of the
     * nullptr pointer value, so without this line every handle that has ever
     * POSTed or PUT sits in POST mode afterward. Rest it as GET so the NEXT
     * request's own method setup (HTTPGET/POST/CUSTOMREQUEST/NOBODY) starts from
     * a known state instead of one that depends on this handle's history. */
    curl_easy_setopt(self->curl, CURLOPT_HTTPGET, 1L);

    trace_log_pop();

    return result;
}

static HTTP_Client_Result _http_client_post_send(HTTP_Client *const self, char const *const url, char const *const payload, USize const payload_size, String *const response) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "payload", (void*) payload);

    curl_easy_setopt(self->curl, CURLOPT_CUSTOMREQUEST, (char*) nullptr);
    curl_easy_setopt(self->curl, CURLOPT_NOBODY, 0L);
    curl_easy_setopt(self->curl, CURLOPT_POST, 1L);
    curl_easy_setopt(self->curl, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(self->curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t) payload_size);

    HTTP_Client_Result const result = _http_client_perform(self, url, response);

    trace_log_pop();

    return result;
}

/*==============================================================================
 * MARK: - API
 *============================================================================*/

void http_client_delete(HTTP_Client **const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "*self", (void*) *self);

    string_uninit(&(*self)->response_body);
    http_client_header_clear(*self);
    _http_client_response_headers_reset(*self);

    curl_easy_cleanup((*self)->curl);
    memory_free((void*) *self);

#ifdef MEMORY_NON_DANGLING_POINTER
    *self = nullptr;
#endif // MEMORY_NON_DANGLING_POINTER

    trace_log_pop();
}

String http_client_escape_1(HTTP_Client const *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    String const string = _http_client_escape(self, data, char_length(data));

    trace_log_pop();

    return string;
}

String http_client_escape_3(HTTP_Client const *const self, String const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty String's data pointer is nullptr (never allocated), which would
     * otherwise trip _http_client_escape's own error_check_null - an empty
     * String is a legal value here, not a contract violation. */
    if (string_get_size(data) == 0) {
        String const string = string_init_1();

        trace_log_pop();

        return string;
    }

    String const string = _http_client_escape(self, string_get_data(data), string_get_size(data));

    trace_log_pop();

    return string;
}

HTTP_Client_Result http_client_get(HTTP_Client *const self, char const *const url, String *const response) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "url", (void*) url);

    /* No POSTFIELDS clear here: CURLOPT_POSTFIELDS "implies CURLOPT_POST" per
     * libcurl's own docs regardless of the pointer value (even nullptr), so
     * setting it here would silently flip this GET back to POST. The perform
     * tail already clears POSTFIELDS/POSTFIELDSIZE_LARGE and rests the handle
     * on HTTPGET after every call - nothing left over for this request to clear. */
    curl_easy_setopt(self->curl, CURLOPT_CUSTOMREQUEST, (char*) nullptr);
    curl_easy_setopt(self->curl, CURLOPT_NOBODY, 0L);
    curl_easy_setopt(self->curl, CURLOPT_HTTPGET, 1L);

    HTTP_Client_Result const result = _http_client_perform(self, url, response);

    trace_log_pop();

    return result;
}

CURL* http_client_get_handle(HTTP_Client *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->curl;
}

struct curl_slist* http_client_get_headers(HTTP_Client const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->headers;
}

void http_client_global_uninit(void) {
    trace_log_push(LOG_METADATA);

    string_uninit(&_http_client_version);

    curl_global_cleanup();

    trace_log_pop();
}

HTTP_Client_Result http_client_head(HTTP_Client *const self, char const *const url, String *const response) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "url", (void*) url);

    /* No POSTFIELDS clear here - see http_client_get for why; CURLOPT_NOBODY 1
     * wins the wire method regardless (curl sends HEAD), so this was never the
     * observable bug, but the same setopt would still leave POSTFIELDS pointing
     * at nothing useful for the NEXT call to rely on. */
    curl_easy_setopt(self->curl, CURLOPT_CUSTOMREQUEST, (char*) nullptr);
    curl_easy_setopt(self->curl, CURLOPT_NOBODY, 1L);

    HTTP_Client_Result const result = _http_client_perform(self, url, response);

    /* No NOBODY reset here: the perform tail's CURLOPT_HTTPGET 1 already implies
     * NOBODY 0 (curl's own "setting HTTPGET clears NOBODY" behavior) - restating
     * it here was redundant. */

    trace_log_pop();

    return result;
}

void http_client_header_add(HTTP_Client *const self, char const *const header) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "header", (void*) header);

    /* curl_slist_append returns null WITHOUT freeing the list it was given:
     * assigning that null straight back would drop every header added so far.
     * On failure the new header is lost and a WARN says so; the list stays. */
    struct curl_slist *const appended = curl_slist_append(self->headers, header);

    if (memory_empty((void*) appended)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_client_header_add: curl_slist_append failed, header dropped");
    }
    else {
        self->headers = appended;
    }

    trace_log_pop();
}

void http_client_header_clear(HTTP_Client *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (!memory_empty(self->headers)) {
        curl_slist_free_all(self->headers);

        self->headers = nullptr;
    }

    trace_log_pop();
}

HTTP_Client* http_client_new(void) {
    trace_log_push(LOG_METADATA);

    pthread_once(&_http_client_global_once, _http_client_global_init);

    CURL *const curl = curl_easy_init();

    error_check_null(LOG_METADATA, "curl", (void*) curl);

    HTTP_Client *const self = (HTTP_Client*) memory_alloc(sizeof(HTTP_Client));

    *self = (HTTP_Client) {
        .curl                   = curl,
        .headers                = nullptr,
        .response_headers       = nullptr,
        .response_body          = string_init_2(_HTTP_CLIENT_RESPONSE_BODY_INITIAL_SIZE),
        .active_response        = nullptr,
        .response_status        = 0,
        .max_response_size      = _HTTP_CLIENT_RESPONSE_SIZE_DEFAULT,
        .response_size          = 0,
        .response_header_bytes  = 0,
        .response_header_lines  = 0,
        .body_capped            = false,
        .header_capped          = false,
        .arena_capped           = false
    };

    self->error_buffer[0] = '\0';

    _http_client_default_options_set(self);

    trace_log_pop();

    return self;
}

HTTP_Client_Result http_client_post_1(HTTP_Client *const self, char const *const url, char const *const payload, String *const response) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "payload", (void*) payload);

    HTTP_Client_Result const result = _http_client_post_send(self, url, payload, char_length(payload), response);

    trace_log_pop();

    return result;
}

HTTP_Client_Result http_client_post_2(HTTP_Client *const self, char const *const url, char const *const payload, USize const payload_size, String *const response) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "payload", (void*) payload);

    HTTP_Client_Result const result = _http_client_post_send(self, url, payload, payload_size, response);

    trace_log_pop();

    return result;
}

HTTP_Client_Result http_client_post_3(HTTP_Client *const self, char const *const url, Str const *const payload, String *const response) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "payload", (void*) payload);

    /* An empty Str's data pointer is nullptr, not "" - see http_client_post_4. */
    char const *const data = str_get_data(payload) != nullptr ? str_get_data(payload) : "";

    HTTP_Client_Result const result = _http_client_post_send(self, url, data, str_get_size(payload), response);

    trace_log_pop();

    return result;
}

HTTP_Client_Result http_client_post_4(HTTP_Client *const self, char const *const url, String const *const payload, String *const response) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "payload", (void*) payload);

    /* An empty String's data pointer is nullptr, not "" - _http_client_post_send's
     * own error_check_null on the char* payload would then abort on a perfectly
     * legal empty body. Substitute the literal so an empty payload sends an empty
     * body instead of aborting. */
    char const *const data = string_get_data(payload) != nullptr ? string_get_data(payload) : "";

    HTTP_Client_Result const result = _http_client_post_send(self, url, data, string_get_size(payload), response);

    trace_log_pop();

    return result;
}

HTTP_Client_Result http_client_put_1(HTTP_Client *const self, char const *const url, char const *const payload, String *const response) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "payload", (void*) payload);

    HTTP_Client_Result const result = http_client_put_2(self, url, payload, char_length(payload), response);

    trace_log_pop();

    return result;
}

HTTP_Client_Result http_client_put_2(HTTP_Client *const self, char const *const url, char const *const payload, USize const payload_size, String *const response) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "payload", (void*) payload);

    curl_easy_setopt(self->curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(self->curl, CURLOPT_NOBODY, 0L);
    curl_easy_setopt(self->curl, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(self->curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t) payload_size);

    HTTP_Client_Result const result = _http_client_perform(self, url, response);

    curl_easy_setopt(self->curl, CURLOPT_CUSTOMREQUEST, (char*) nullptr);

    trace_log_pop();

    return result;
}

HTTP_Client_Result http_client_put_3(HTTP_Client *const self, char const *const url, Str const *const payload, String *const response) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "payload", (void*) payload);

    /* An empty Str's data pointer is nullptr, not "" - see http_client_post_4. */
    char const *const data = str_get_data(payload) != nullptr ? str_get_data(payload) : "";

    HTTP_Client_Result const result = http_client_put_2(self, url, data, str_get_size(payload), response);

    trace_log_pop();

    return result;
}

HTTP_Client_Result http_client_put_4(HTTP_Client *const self, char const *const url, String const *const payload, String *const response) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "payload", (void*) payload);

    /* An empty String's data pointer is nullptr, not "" - see http_client_post_4. */
    char const *const data = string_get_data(payload) != nullptr ? string_get_data(payload) : "";

    HTTP_Client_Result const result = http_client_put_2(self, url, data, string_get_size(payload), response);

    trace_log_pop();

    return result;
}

HTTP_Client_Result http_client_request_delete(HTTP_Client *const self, char const *const url, String *const response) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "url", (void*) url);

    /* HTTPGET first: a handle that just POSTed/PUT is still in POST mode (see
     * the perform tail's note) - CURLOPT_HTTPGET resets that GET/body-less
     * baseline before CUSTOMREQUEST renames the verb on the wire, so no stale
     * POSTFIELDS/Content-Length survives from a prior request on this handle. */
    curl_easy_setopt(self->curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(self->curl, CURLOPT_NOBODY, 0L);
    curl_easy_setopt(self->curl, CURLOPT_CUSTOMREQUEST, "DELETE");

    HTTP_Client_Result const result = _http_client_perform(self, url, response);

    curl_easy_setopt(self->curl, CURLOPT_CUSTOMREQUEST, (char*) nullptr);

    trace_log_pop();

    return result;
}

String const* http_client_response_body(HTTP_Client const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return &self->response_body;
}

char const* http_client_response_header_find(HTTP_Client const *const self, char const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    char const *value     = nullptr;
    USize const name_size = char_length(name);

    for (struct curl_slist const *node = self->response_headers; !memory_empty((void*) node) && memory_empty((void*) value); node = node->next) {
        char const *const line = node->data;

        /* "Name:" prefix, then the value with its leading blanks skipped. */
        if (name_size > 0 && char_length(line) > name_size && line[name_size] == ':' && char_compare_iequal_2(line, name_size, name, name_size)) {
            value = line + name_size + 1;

            while (*value == ' ' || *value == '\t') {
                value += 1;
            }
        }
    }

    trace_log_pop();

    return value;
}

USize http_client_response_status(HTTP_Client const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->response_status;
}

bool http_client_result_is_ok(HTTP_Client_Result const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->success && self->response_code >= 200 && self->response_code < 300;
}

void http_client_result_uninit(HTTP_Client_Result *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    string_uninit(&self->error);

#ifdef ARENA_IMPLEMENTATION
#ifdef MEMORY_NON_DANGLING_POINTER
    self->allocator     = nullptr;
#endif // MEMORY_NON_DANGLING_POINTER
#endif // ARENA_IMPLEMENTATION
    self->code           = CURLE_OK;
    self->response_code  = 0;
    self->status         = HTTP_CLIENT_STATUS_OK;
    self->success        = false;

    trace_log_pop();
}

void http_client_set_ca_file(HTTP_Client *const self, char const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "path", (void*) path);

    curl_easy_setopt(self->curl, CURLOPT_CAINFO, path);

    trace_log_pop();
}

void http_client_set_ca_path(HTTP_Client *const self, char const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "path", (void*) path);

    curl_easy_setopt(self->curl, CURLOPT_CAPATH, path);

    trace_log_pop();
}

void http_client_set_connect_timeout_ms(HTTP_Client *const self, USize const timeout_ms) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    curl_easy_setopt(self->curl, CURLOPT_CONNECTTIMEOUT_MS, _http_client_long_clamp(timeout_ms));

    trace_log_pop();
}

void http_client_set_follow_redirects(HTTP_Client *const self, bool const follow, USize const max_redirects) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    curl_easy_setopt(self->curl, CURLOPT_FOLLOWLOCATION, follow ? 1L : 0L);
    curl_easy_setopt(self->curl, CURLOPT_MAXREDIRS, follow ? _http_client_long_clamp(max_redirects) : 0L);

    trace_log_pop();
}

void http_client_set_max_response_size(HTTP_Client *const self, USize const max_bytes) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    self->max_response_size = max_bytes;

    trace_log_pop();
}

void http_client_set_proxy(HTTP_Client *const self, char const *const url) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "url", (void*) url);

    curl_easy_setopt(self->curl, CURLOPT_PROXY, url);

    trace_log_pop();
}

void http_client_set_timeout_ms(HTTP_Client *const self, USize const timeout_ms) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    curl_easy_setopt(self->curl, CURLOPT_TIMEOUT_MS, _http_client_long_clamp(timeout_ms));

    trace_log_pop();
}

void http_client_set_user_agent(HTTP_Client *const self, char const *const user_agent) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "user_agent", (void*) user_agent);

    curl_easy_setopt(self->curl, CURLOPT_USERAGENT, user_agent);

    trace_log_pop();
}

void http_client_set_verify_tls(HTTP_Client *const self, bool const verify_tls) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    curl_easy_setopt(self->curl, CURLOPT_SSL_VERIFYHOST, verify_tls ? 2L : 0L);
    curl_easy_setopt(self->curl, CURLOPT_SSL_VERIFYPEER, verify_tls ? 1L : 0L);

    trace_log_pop();
}