/*==============================================================================
 * MARK: - Includes
 *============================================================================*/

#include <http/service/oauth/oauth.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

/** @brief Longest decimal expiry a state token can carry (2^64 has 20 digits). */
static USize const _HTTP_SERVICE_OAUTH_EXPIRY_MAX_SIZE = 20;
/** @brief Rows the registry allocates on its first provider. */
static USize const _HTTP_SERVICE_OAUTH_INITIAL_CAPACITY = 4;
/** @brief Random bytes behind a state token's nonce; rendered as 2x hex chars. */
static USize const _HTTP_SERVICE_OAUTH_NONCE_SIZE = 16;
/** @brief Random bytes behind a PKCE verifier; base64url turns 32 into 43 chars. */
static USize const _HTTP_SERVICE_OAUTH_PKCE_VERIFIER_BYTES = 32;
/** @brief RFC 7636 4.1 upper bound on a code verifier's length in chars. */
static USize const _HTTP_SERVICE_OAUTH_PKCE_VERIFIER_MAX_SIZE = 128;
/** @brief RFC 7636 4.1 lower bound on a code verifier's length in chars. */
static USize const _HTTP_SERVICE_OAUTH_PKCE_VERIFIER_MIN_SIZE = 43;

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/* ONE ROW PER PROVIDER, not nine parallel lists.
 *
 * The lists were addressed by a single index every reader recomputed, which made a partial
 * append a permanent misalignment - a lookup reading position N of a list holding N entries,
 * handed back as a client secret. The all-or-nothing append and update that fixed it needed a
 * lists[]/values[] pairing written twice, and a static_assert to pin the two arrays' WIDTH; the
 * assert could never see a reordering that kept the widths equal and swapped a secret for a
 * profile URL. A struct writes each pairing exactly once, in one place, and cannot desynchronize
 * in either dimension - so both the assert and the hazard it half-covered are gone.
 *
 * It is also what makes the snapshot in every network call one bounded operation instead of nine. */
struct HTTP_Service_OAuth_Row {
    Str authorize_url;
    Str client_id;
    Str client_secret;
    Str name;
    Str profile_url;
    Str redirect_uri;
    Str revoke_url;
    Str scope;
    Str token_url;
};

/*==============================================================================
 * MARK: - Helpers
 *============================================================================*/

/* memory_empty is null-only, and a set-but-empty environment variable is the routine way an
 * OAuth secret arrives missing on Linux. Both spellings of absent answer true here. */
static bool _http_service_oauth_char_empty(char const *const data) {
    return data == nullptr || data[0] == '\0';
}

// Null-safe view of an optional configuration field: absent and empty are the same value.
static char const* _http_service_oauth_char_text(char const *const data) {
    return data == nullptr ? "" : data;
}

/* An EMPTY Str's data pointer is null, not "". Every reader below wants a NUL-terminated
 * char* it can hand to an encoder or a header builder, so absence renders as "". */
static char const* _http_service_oauth_str_text(Str const *const self) {
    return str_get_size(self) == 0 ? "" : str_get_data(self);
}

static bool _http_service_oauth_char_has_query(char const *const data) {
    for (USize i = 0; data[i] != '\0'; i += 1) {
        if (data[i] == '?') {
            return true;
        }
    }

    return false;
}

/* An access token is DATA that arrived from a token endpoint, and it is pasted into a header
 * line. A CR or LF inside it would end that line and start a header the caller never wrote. */
static bool _http_service_oauth_char_line_break(char const *const data, USize const size) {
    if (data == nullptr) {
        return false;
    }

    for (USize i = 0; i < size; i += 1) {
        if (data[i] == '\r' || data[i] == '\n') {
            return true;
        }
    }

    return false;
}

static void* _http_service_oauth_try_borrow(HTTP_Service_OAuth const *const self, USize const byte_count) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

#ifdef ARENA_IMPLEMENTATION
    void *const buffer = allocator_try_borrow(byte_count, self->allocator);
#else
    void *const buffer = allocator_try_borrow(byte_count);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return buffer;
}

static void _http_service_oauth_release(HTTP_Service_OAuth const *const self, void *const buffer) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

#ifdef ARENA_IMPLEMENTATION
    allocator_release(buffer, self->allocator);
#else
    allocator_release(buffer);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();
}

static Str _http_service_oauth_char_to_str(HTTP_Service_OAuth const *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    USize const data_size = char_length(data);

#ifdef ARENA_IMPLEMENTATION
    if (self->allocator != nullptr) {
        // A zero-size copy is not an allocation the arena should be asked for; it is the empty Str.
        Str const value = data_size == 0 ? str_alloc_init_1(self->allocator) : str_alloc_init_static(data, data_size, self->allocator);

        trace_log_pop();

        return value;
    }
#endif // ARENA_IMPLEMENTATION

    // str_init_static allocates an OWNED copy; str_init_3 would build a non-owning view over
    // the char_new_3 block, which str_uninit then skips - leaking it.
    Str const value = data_size == 0 ? str_init_1() : str_init_static(data, data_size);

    trace_log_pop();

    return value;
}

/* Build one field WITHOUT touching anything stored.
 *
 * The arena path degrades to the EMPTY Str when it refuses the copy, and here that is not a
 * cosmetic loss: these fields hold client secrets and endpoint URLs, so an empty entry is a
 * provider configured with no credential. An empty INPUT is a different thing entirely - an
 * absent revoke endpoint or scope - and builds successfully as the empty Str. */
static bool _http_service_oauth_str_build(HTTP_Service_OAuth const *const self, char const *const data, Str *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_null(LOG_METADATA, "out", (void*) out);

    *out = _http_service_oauth_char_to_str(self, data);

    bool const built = data[0] == '\0' || str_get_size(out) != 0;

    trace_log_pop();

    return built;
}

static void _http_service_oauth_row_uninit(HTTP_Service_OAuth_Row *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    str_uninit(&self->authorize_url);
    str_uninit(&self->client_id);
    str_uninit(&self->client_secret);
    str_uninit(&self->name);
    str_uninit(&self->profile_url);
    str_uninit(&self->redirect_uri);
    str_uninit(&self->revoke_url);
    str_uninit(&self->scope);
    str_uninit(&self->token_url);

    trace_log_pop();
}

/* A borrowed char* view of a stored row, so a snapshot is built by the SAME code that builds a
 * registration - one pairing site for the nine fields instead of two. The pointers alias the
 * row's own storage and are valid only while the caller holds the lock. */
static HTTP_Service_OAuth_Provider _http_service_oauth_row_view(HTTP_Service_OAuth_Row const *const self) {
    return (HTTP_Service_OAuth_Provider){
        .authorize_url  = _http_service_oauth_str_text(&self->authorize_url),
        .client_id      = _http_service_oauth_str_text(&self->client_id),
        .client_secret  = _http_service_oauth_str_text(&self->client_secret),
        .name           = _http_service_oauth_str_text(&self->name),
        .profile_url    = _http_service_oauth_str_text(&self->profile_url),
        .redirect_uri   = _http_service_oauth_str_text(&self->redirect_uri),
        .revoke_url     = _http_service_oauth_str_text(&self->revoke_url),
        .scope          = _http_service_oauth_str_text(&self->scope),
        .token_url      = _http_service_oauth_str_text(&self->token_url)
    };
}

/* All-or-nothing by construction: every field is built into a row the caller still owns
 * privately, so a refusal costs one _row_uninit and nothing stored is ever released before its
 * replacement exists. `&& built` after each call, never before it - the build must run for every
 * field so every field is uninitializable, which short-circuiting would break. */
static bool _http_service_oauth_row_build(HTTP_Service_OAuth const *const self, HTTP_Service_OAuth_Provider const *const provider, HTTP_Service_OAuth_Row *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "provider", (void*) provider);
    error_check_null(LOG_METADATA, "out", (void*) out);

    *out = (HTTP_Service_OAuth_Row) DEFAULT_INITIALIZATION;

    /* Configuration is DATA, so a missing required field is refused, never aborted: these
     * strings come from environment variables and config files, and an empty
     * TRAYMON_OAUTH_GOOGLE_CLIENT_SECRET= used to kill the server at boot over exactly the
     * misconfiguration a refusal lets the caller log and continue past. revoke_url and scope
     * are absent from this list deliberately - see the header. */
    if (_http_service_oauth_char_empty(provider->authorize_url) ||
        _http_service_oauth_char_empty(provider->client_id)     ||
        _http_service_oauth_char_empty(provider->client_secret) ||
        _http_service_oauth_char_empty(provider->name)          ||
        _http_service_oauth_char_empty(provider->profile_url)   ||
        _http_service_oauth_char_empty(provider->redirect_uri)  ||
        _http_service_oauth_char_empty(provider->token_url)) {
        trace_log_pop();

        return false;
    }

    bool built = true;

    built = _http_service_oauth_str_build(self, provider->authorize_url, &out->authorize_url) && built;
    built = _http_service_oauth_str_build(self, provider->client_id, &out->client_id) && built;
    built = _http_service_oauth_str_build(self, provider->client_secret, &out->client_secret) && built;
    built = _http_service_oauth_str_build(self, provider->name, &out->name) && built;
    built = _http_service_oauth_str_build(self, provider->profile_url, &out->profile_url) && built;
    built = _http_service_oauth_str_build(self, provider->redirect_uri, &out->redirect_uri) && built;
    built = _http_service_oauth_str_build(self, _http_service_oauth_char_text(provider->revoke_url), &out->revoke_url) && built;
    built = _http_service_oauth_str_build(self, _http_service_oauth_char_text(provider->scope), &out->scope) && built;
    built = _http_service_oauth_str_build(self, provider->token_url, &out->token_url) && built;

    if (!built) {
        _http_service_oauth_row_uninit(out);

        *out = (HTTP_Service_OAuth_Row) DEFAULT_INITIALIZATION;
    }

    trace_log_pop();

    return built;
}

static bool _http_service_oauth_rows_reserve(HTTP_Service_OAuth *const self, USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (capacity <= self->row_capacity) {
        trace_log_pop();

        return true;
    }

    /* The byte size is a bare multiply: an absurd capacity would wrap it and hand back a tiny
     * block while row_capacity said otherwise. */
    if (capacity > USIZE_MAX / sizeof(HTTP_Service_OAuth_Row)) {
        trace_log_pop();

        return false;
    }

    HTTP_Service_OAuth_Row *const rows = (HTTP_Service_OAuth_Row*) _http_service_oauth_try_borrow(self, sizeof(HTTP_Service_OAuth_Row) * capacity);

    // try_borrow, not borrow: a refused arena answers null instead of ending the process.
    if (rows == nullptr) {
        trace_log_pop();

        return false;
    }

    memory_set(rows, sizeof(HTTP_Service_OAuth_Row) * capacity, 0);

    if (self->row_count > 0) {
        memory_copy_2(rows, sizeof(HTTP_Service_OAuth_Row) * capacity, self->rows, sizeof(HTTP_Service_OAuth_Row) * self->row_count);
    }

    _http_service_oauth_release(self, (void*) self->rows);

    self->rows          = rows;
    self->row_capacity  = capacity;

    trace_log_pop();

    return true;
}

static USize _http_service_oauth_provider_at(HTTP_Service_OAuth const *const self, char const *const provider) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "provider", (void*) provider);

    USize const provider_size = char_length(provider);
    USize       index         = USIZE_MAX;

    for (USize i = 0; i < self->row_count; i += 1) {
        if (str_compare_equal_2(&self->rows[i].name, provider, provider_size)) {
            index = i;

            break;
        }
    }

    trace_log_pop();

    return index;
}

/* Take a PRIVATE, owned copy of one provider row, so the caller can drop the lock before the
 * round trip. Every network function held the mutex across http_client_post_4 before this
 * existed, which serialized every login in the process behind one provider - and with the
 * client's default 20 s budget, a stalled endpoint froze even get_authorize_url for 20 s.
 * A borrowed Str would not do: the row it points into can be replaced by provider_add while
 * the request is in flight, and the replacement releases the old bytes.
 *
 * `found` is OPTIONAL and answers what the bool return cannot: false means the registry holds no
 * such name, true means the row EXISTS and its copy was refused. Those are two different operator
 * problems - "you never registered google" versus "this service is out of memory" - and folding
 * them into one answer sent an operator hunting a registration bug that did not exist. */
static bool _http_service_oauth_row_snapshot(HTTP_Service_OAuth *const self, char const *const provider, HTTP_Service_OAuth_Row *const out, bool *const found) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "provider", (void*) provider);
    error_check_null(LOG_METADATA, "out", (void*) out);

    *out = (HTTP_Service_OAuth_Row) DEFAULT_INITIALIZATION;

    if (found != nullptr) {
        *found = false;
    }

    thread_mutex_lock(&self->mutex);

    USize const index = _http_service_oauth_provider_at(self, provider);

    if (index == USIZE_MAX) {
        thread_mutex_unlock(&self->mutex);

        trace_log_pop();

        return false;
    }

    if (found != nullptr) {
        *found = true;
    }

    HTTP_Service_OAuth_Provider const view = _http_service_oauth_row_view(&self->rows[index]);
    bool                        const copied = _http_service_oauth_row_build(self, &view, out);

    thread_mutex_unlock(&self->mutex);

    trace_log_pop();

    return copied;
}

static void _http_service_oauth_string_add(String *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    USize const data_size = char_length(data);

    if (data_size > 0) {
        string_add_last_2(self, data, data_size);
    }

    trace_log_pop();
}

static String _http_service_oauth_string_init(HTTP_Service_OAuth const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

#ifdef ARENA_IMPLEMENTATION
    if (self->allocator != nullptr) {
        String const string = string_alloc_init_1(self->allocator);

        trace_log_pop();

        return string;
    }
#endif // ARENA_IMPLEMENTATION

    String const string = string_init_1();

    trace_log_pop();

    return string;
}

static String _http_service_oauth_string_copy(HTTP_Service_OAuth const *const self, String const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    if (string_empty(data)) {
        String const string = _http_service_oauth_string_init(self);

        trace_log_pop();

        return string;
    }

#ifdef ARENA_IMPLEMENTATION
    if (self->allocator != nullptr) {
        String const string = string_alloc_init_6(data, self->allocator);

        trace_log_pop();

        return string;
    }
#endif // ARENA_IMPLEMENTATION

    String const string = string_init_6(data);

    trace_log_pop();

    return string;
}

/* Every request this module makes is a small, known document. The client's 16 MiB default is
 * there for downloads; a token endpoint answering more than 64 KiB is broken or hostile. */
static HTTP_Client* _http_service_oauth_client_new(void) {
    trace_log_push(LOG_METADATA);

    HTTP_Client *const client = http_client_new();

    http_client_set_max_response_size(client, HTTP_SERVICE_OAUTH_MAX_RESPONSE_SIZE);

    trace_log_pop();

    return client;
}

static HTTP_Service_OAuth_Token _http_service_oauth_token_init(HTTP_Service_OAuth const *const self) {
    return (HTTP_Service_OAuth_Token) {
        .access_token       = _http_service_oauth_string_init(self),
        .error              = _http_service_oauth_string_init(self),
        .error_description  = _http_service_oauth_string_init(self),
        .expires_at         = 0,
        .expires_in         = 0,
        .id_token           = _http_service_oauth_string_init(self),
        .raw                = _http_service_oauth_string_init(self),
        .refresh_token      = _http_service_oauth_string_init(self),
        .response_code      = 0,
        .scope              = _http_service_oauth_string_init(self),
        .status             = HTTP_CLIENT_STATUS_ERROR,
        .token_type         = _http_service_oauth_string_init(self)
    };
}

static HTTP_Service_OAuth_Profile _http_service_oauth_profile_init(HTTP_Service_OAuth const *const self) {
    return (HTTP_Service_OAuth_Profile) {
        .email              = _http_service_oauth_string_init(self),
        .email_verified     = HTTP_SERVICE_OAUTH_EMAIL_VERIFIED_UNKNOWN,
        .error              = _http_service_oauth_string_init(self),
        .error_description  = _http_service_oauth_string_init(self),
        .id                 = _http_service_oauth_string_init(self),
        .name               = _http_service_oauth_string_init(self),
        .picture            = _http_service_oauth_string_init(self),
        .raw                = _http_service_oauth_string_init(self),
        .response_code      = 0,
        .status             = HTTP_CLIENT_STATUS_ERROR
    };
}

static void _http_service_oauth_json_copy_string(Json const *const json, char const *const name, String *const output) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "json", (void*) json);
    error_check_null(LOG_METADATA, "name", (void*) name);
    error_check_null(LOG_METADATA, "output", (void*) output);

    Json const *const node = json_get_string_1(json, name);

    if (node != nullptr) {
        String value = json_get_value_string_4(node);

        if (!string_empty(&value)) {
            string_copy(output, &value);
        }

        string_uninit(&value);
    }

    trace_log_pop();
}

/* GitHub's user id is a JSON NUMBER, Google's `sub` is a string. Reading only strings left
 * profile.id empty for every GitHub account - a provider the registry accepted and the flow
 * could never complete. Rendered as decimal so the caller sees one id type. */
static void _http_service_oauth_json_copy_id(Json const *const json, char const *const name, String *const output) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "json", (void*) json);
    error_check_null(LOG_METADATA, "name", (void*) name);
    error_check_null(LOG_METADATA, "output", (void*) output);

    Json const *const node = json_get_1(json, name);

    if (node == nullptr) {
        trace_log_pop();

        return;
    }

    JsonType const type = json_get_type(node);

    if (type == JSON_TYPE_STRING) {
        _http_service_oauth_json_copy_string(json, name, output);

        trace_log_pop();

        return;
    }

    if (type == JSON_TYPE_INTEGER_U || type == JSON_TYPE_INTEGER_S) {
        char decimal[24] = DEFAULT_INITIALIZATION;

        /* Signed and unsigned are rendered by their own formatter: reading a negative id as
         * USize would print an 18-quintillion account number rather than the id it is. */
        if (type == JSON_TYPE_INTEGER_S) {
            char_from_numbers_int_1(decimal, sizeof(decimal), json_get_value_number_int(node));
        }
        else {
            char_from_numbers_uint_1(decimal, sizeof(decimal), json_get_value_number_uint(node));
        }

        _http_service_oauth_string_add(output, decimal);
    }

    trace_log_pop();
}

/* RFC 6749 5.2: a failed token request answers a JSON object carrying `error` and often
 * `error_description`. Dropping them left "provider unreachable", "the code was already
 * used" and "our client secret is wrong" indistinguishable at the call site. */
static void _http_service_oauth_json_copy_error(Json const *const json, String *const error, String *const error_description) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "json", (void*) json);
    error_check_null(LOG_METADATA, "error", (void*) error);
    error_check_null(LOG_METADATA, "error_description", (void*) error_description);

    _http_service_oauth_json_copy_string(json, "error", error);
    _http_service_oauth_json_copy_string(json, "error_description", error_description);

    // GitHub's REST API spells a rejection as {"message": "..."} with no `error` at all.
    if (string_empty(error_description)) {
        _http_service_oauth_json_copy_string(json, "message", error_description);
    }

    trace_log_pop();
}

static void _http_service_oauth_token_parse(HTTP_Service_OAuth_Token *const token) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "token", (void*) token);

    if (string_empty(&token->raw)) {
        trace_log_pop();

        return;
    }

    Json *json = json_from_4(&token->raw);

    if (json == nullptr) {
        trace_log_pop();

        return;
    }

    _http_service_oauth_json_copy_string(json, "access_token", &token->access_token);
    _http_service_oauth_json_copy_string(json, "refresh_token", &token->refresh_token);
    _http_service_oauth_json_copy_string(json, "id_token", &token->id_token);
    _http_service_oauth_json_copy_string(json, "token_type", &token->token_type);
    _http_service_oauth_json_copy_string(json, "scope", &token->scope);
    _http_service_oauth_json_copy_error(json, &token->error, &token->error_description);

    Json const *const expires_in = json_get_number_uint_1(json, "expires_in");

    if (expires_in != nullptr) {
        token->expires_in = json_get_value_number_uint(expires_in);
    }

    /* Some providers send expires_in as a STRING ("3600"). Read that too rather than reporting
     * a token with no lifetime, which a caller would store as "never expires". */
    if (token->expires_in == 0) {
        Json const *const expires_in_text = json_get_string_1(json, "expires_in");

        if (expires_in_text != nullptr) {
            String value = json_get_value_string_4(expires_in_text);

            if (!string_empty(&value)) {
                token->expires_in = char_to_numbers_uint_2(string_get_data(&value), string_get_size(&value));
            }

            string_uninit(&value);
        }
    }

    ISize const now = datetime_now();

    /* An absolute deadline is what a caller actually stores; computing it here means the
     * arithmetic is done once, beside the clock reading that justifies it. */
    if (token->expires_in > 0 && now > 0) {
        token->expires_at = (USize) now + token->expires_in;
    }

    json_delete(&json);

    trace_log_pop();
}

static void _http_service_oauth_profile_parse(HTTP_Service_OAuth_Profile *const profile) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "profile", (void*) profile);

    if (string_empty(&profile->raw)) {
        trace_log_pop();

        return;
    }

    Json *json = json_from_4(&profile->raw);

    if (json == nullptr) {
        trace_log_pop();

        return;
    }

    _http_service_oauth_json_copy_id(json, "id", &profile->id);

    if (string_empty(&profile->id)) {
        _http_service_oauth_json_copy_id(json, "sub", &profile->id);
    }

    _http_service_oauth_json_copy_string(json, "email", &profile->email);
    _http_service_oauth_json_copy_string(json, "name", &profile->name);
    _http_service_oauth_json_copy_string(json, "picture", &profile->picture);

    if (string_empty(&profile->picture)) {
        _http_service_oauth_json_copy_string(json, "avatar_url", &profile->picture);
    }

    /* Tri-state on purpose: json_get_number_uint_1 cannot see a boolean at all, and a bool
     * field would fold "the provider said false" into "the provider said nothing" - the exact
     * distinction a consumer gating account creation on a verified address needs. */
    Json const *const email_verified = json_get_1(json, "email_verified");

    if (email_verified != nullptr && json_is_bool(email_verified)) {
        profile->email_verified = json_get_value_bool(email_verified)
            ? HTTP_SERVICE_OAUTH_EMAIL_VERIFIED_YES
            : HTTP_SERVICE_OAUTH_EMAIL_VERIFIED_NO;
    }

    _http_service_oauth_json_copy_error(json, &profile->error, &profile->error_description);

    json_delete(&json);

    trace_log_pop();
}

/* Sends the form body and fills in the whole outcome. No lock is held here: the caller has
 * already snapshotted the row it built `payload` and `url` from. */
static HTTP_Service_OAuth_Token _http_service_oauth_send_token_request(HTTP_Service_OAuth const *const self, char const *const url, String *const payload) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "url", (void*) url);
    error_check_null(LOG_METADATA, "payload", (void*) payload);

    HTTP_Service_OAuth_Token    token    = _http_service_oauth_token_init(self);
    HTTP_Client                 *client  = _http_service_oauth_client_new();
    String                      response = _http_service_oauth_string_init(self);

    /* GitHub's token endpoint answers application/x-www-form-urlencoded unless this header is
     * present, which json_from_4 cannot parse - so the exchange "succeeded" with an empty
     * access token and no error anywhere. Google ignores it. */
    http_client_header_add(client, "Accept: application/json");
    http_client_header_add(client, "Content-Type: application/x-www-form-urlencoded");

    HTTP_Client_Result result = http_client_post_4(client, url, payload, &response);

    token.response_code = result.response_code;
    token.status        = result.status;

    /* Never the body, never the payload: one carries the access token, the other the client
     * secret. The URL, transport status and code are enough to diagnose a failure. */
    if (!result.success) {
        log_message_1(LOG_LEVEL_WARN, "oauth: token request to %s failed (status=%d, code=%d)\n", url, (I32) result.status, (I32) result.code);

        _http_service_oauth_string_add(&token.error, "transport");
    }

    http_client_result_uninit(&result);

    string_uninit(&token.raw);

    token.raw = _http_service_oauth_string_copy(self, &response);

    _http_service_oauth_token_parse(&token);

    string_uninit(&response);
    http_client_header_clear(client);

    http_client_delete(&client);

    trace_log_pop();

    return token;
}

/* A token whose request never reached the wire: an unknown provider, an empty code. Shaped
 * exactly like a transport failure (status ERROR, response_code 0) with its own error code, so
 * a caller has one branch for "no token" and one field to read for why. */
static HTTP_Service_OAuth_Token _http_service_oauth_token_refused(HTTP_Service_OAuth const *const self, char const *const error) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "error", (void*) error);

    HTTP_Service_OAuth_Token token = _http_service_oauth_token_init(self);

    _http_service_oauth_string_add(&token.error, error);

    trace_log_pop();

    return token;
}

static HTTP_Service_OAuth_Profile _http_service_oauth_profile_refused(HTTP_Service_OAuth const *const self, char const *const error) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "error", (void*) error);

    HTTP_Service_OAuth_Profile profile = _http_service_oauth_profile_init(self);

    _http_service_oauth_string_add(&profile.error, error);

    trace_log_pop();

    return profile;
}

// "provider|nonce|expiry" - the exact bytes the MAC covers, on both the issue and verify paths.
static String _http_service_oauth_state_message(HTTP_Service_OAuth const *const self, char const *const provider, char const *const nonce, USize const nonce_size,
    char const *const expiry, USize const expiry_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "provider", (void*) provider);
    error_check_null(LOG_METADATA, "nonce", (void*) nonce);
    error_check_null(LOG_METADATA, "expiry", (void*) expiry);

    /* One allocator for every String this module builds, scratch included. _string_init is the
     * service's own initializer and answers the arena when the service has one; a bare
     * string_init_1 here would put half the module's storage on the heap and half in the arena,
     * which is one allocator too many for an operator sizing that arena to reason about. */
    String message = _http_service_oauth_string_init(self);

    _http_service_oauth_string_add(&message, provider);
    _http_service_oauth_string_add(&message, "|");

    string_add_last_2(&message, nonce, nonce_size);

    _http_service_oauth_string_add(&message, "|");

    string_add_last_2(&message, expiry, expiry_size);

    trace_log_pop();

    return message;
}

static bool _http_service_oauth_construct(HTTP_Service_OAuth *const self, U8 const *const key, USize const key_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "key", (void*) key);

    /* memory_copy_2 refuses an oversized copy, so an unbounded key_size stored verbatim would
     * describe more key bytes than the array ever received and hand the HMAC readers a length
     * running off the end of it. Both public entry points already cap the size; this bounds it
     * again where the array actually lives, so the stored size cannot outrun the storage. */
    USize const stored_size = key_size < sizeof(self->state_key) ? key_size : sizeof(self->state_key);

    memory_copy_2(self->state_key, sizeof(self->state_key), key, stored_size);

    self->state_key_size = stored_size;

    /* Refuse as a whole rather than abort. pthread_mutex_init can return ENOMEM/EAGAIN, and a
     * registry mutated without mutual exclusion is the use-after-free the lock exists to
     * prevent - but killing the process is the caller's decision to make, not this module's. */
    if (result_is_error(thread_mutex_init(&self->mutex))) {
        log_message_2(LOG_LEVEL_ERROR, LOG_METADATA, "http_service_oauth: mutex init failed - refusing to run without locking");

        memory_set(self->state_key, sizeof(self->state_key), 0);

        self->state_key_size = 0;

        trace_log_pop();

        return false;
    }

    trace_log_pop();

    return true;
}

/*==============================================================================
 * MARK: - API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
bool http_service_oauth_alloc_init_1(HTTP_Service_OAuth *const self, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    U8 key[HTTP_SERVICE_OAUTH_STATE_KEY_SIZE] = DEFAULT_INITIALIZATION;

    // Fail closed: a service that cannot get randomness must not sign state with a zero key.
    if (result_is_error(crypto_random_bytes(key, sizeof(key)))) {
        log_message_2(LOG_LEVEL_ERROR, LOG_METADATA, "http_service_oauth: state key generation failed");

        trace_log_pop();

        return false;
    }

    bool const constructed = http_service_oauth_alloc_init_2(self, allocator, key, sizeof(key));

    memory_set(key, sizeof(key), 0);

    trace_log_pop();

    return constructed;
}

bool http_service_oauth_alloc_init_2(HTTP_Service_OAuth *const self, Arena *const allocator, U8 const *const key, USize const key_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);
    error_check_null(LOG_METADATA, "key", (void*) key);

    // Checked BEFORE self is touched, so a refused key size leaves the caller's struct alone.
    if (key_size == 0 || key_size > HTTP_SERVICE_OAUTH_STATE_KEY_SIZE) {
        trace_log_pop();

        return false;
    }

    /* Assigned before the mutex is initialized: the compound literal omits .mutex, so it zeroes
     * those bytes, and thread_mutex_init then runs on the zeroed member. The reverse order would
     * clobber an initialized lock. */
    *self = (HTTP_Service_OAuth){
        .allocator      = allocator,
        .row_capacity   = 0,
        .row_count      = 0,
        .rows           = nullptr,
        .state_key_size = 0
    };

    bool const constructed = _http_service_oauth_construct(self, key, key_size);

    trace_log_pop();

    return constructed;
}

bool http_service_oauth_alloc_init(HTTP_Service_OAuth *const self, Arena *const allocator) {
    return http_service_oauth_alloc_init_1(self, allocator);
}
#endif // ARENA_IMPLEMENTATION

HTTP_Service_OAuth_Token http_service_oauth_exchange_code_1(HTTP_Service_OAuth *const self, char const *const provider, char const *const code) {
    return http_service_oauth_exchange_code_2(self, provider, code, "");
}

HTTP_Service_OAuth_Token http_service_oauth_exchange_code_2(HTTP_Service_OAuth *const self, char const *const provider, char const *const code, char const *const code_verifier) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "provider", (void*) provider);

    /* The code comes off a browser redirect, so it is DATA - and a GET /callback with no ?code=
     * hands string_get_data of an empty String, which is a null pointer in this tree. It is
     * refused BY VALUE here; an error_check_null above would have aborted the server on it.
     * An empty one used to reach the provider as "code=", spending a round trip to be told what
     * this branch already knows. */
    if (_http_service_oauth_char_empty(code)) {
        HTTP_Service_OAuth_Token const token = _http_service_oauth_token_refused(self, "invalid_request");

        trace_log_pop();

        return token;
    }

    /* One half of the PKCE pair already validates: pkce_challenge_create enforces RFC 7636 4.1's
     * 43..128 chars. A verifier outside that range cannot match a challenge this module derived,
     * so sending it spends a whole round trip to be told invalid_grant - refused here instead,
     * in the shape an empty code already gets. An empty or null verifier still means "no PKCE on
     * this exchange" and is not bounded. */
    if (!_http_service_oauth_char_empty(code_verifier)) {
        USize const verifier_size = char_length(code_verifier);

        if (verifier_size < _HTTP_SERVICE_OAUTH_PKCE_VERIFIER_MIN_SIZE || verifier_size > _HTTP_SERVICE_OAUTH_PKCE_VERIFIER_MAX_SIZE) {
            HTTP_Service_OAuth_Token const token = _http_service_oauth_token_refused(self, "invalid_request");

            trace_log_pop();

            return token;
        }
    }

    HTTP_Service_OAuth_Row  row   = DEFAULT_INITIALIZATION;
    bool                    found = false;

    if (!_http_service_oauth_row_snapshot(self, provider, &row, &found)) {
        _http_service_oauth_row_uninit(&row);

        /* "misconfigured" when the row EXISTS and its copy was refused; "unknown_provider" only
         * when the name was never registered. The header has documented both since 0.2.0 and
         * this branch used to report the second for either. */
        HTTP_Service_OAuth_Token const token = _http_service_oauth_token_refused(self, found ? "misconfigured" : "unknown_provider");

        trace_log_pop();

        return token;
    }

    String payload = _http_service_oauth_string_init(self);

    /* Every pair is checked. A value over http/query's encode ceiling, or an allocator refusal,
     * used to drop that field silently and send a grant request WITHOUT the code in it - the
     * provider then answered invalid_request and the log named the provider, not the field. */
    bool encoded = http_query_form_add_1(&payload, "grant_type", "authorization_code");

    encoded = http_query_form_add_1(&payload, "code", code) && encoded;
    encoded = http_query_form_add_1(&payload, "client_id", _http_service_oauth_str_text(&row.client_id)) && encoded;
    encoded = http_query_form_add_1(&payload, "client_secret", _http_service_oauth_str_text(&row.client_secret)) && encoded;
    encoded = http_query_form_add_1(&payload, "redirect_uri", _http_service_oauth_str_text(&row.redirect_uri)) && encoded;

    if (!_http_service_oauth_char_empty(code_verifier)) {
        encoded = http_query_form_add_1(&payload, "code_verifier", code_verifier) && encoded;
    }

    if (!encoded) {
        log_message_1(LOG_LEVEL_ERROR, "oauth: provider=%s token request body incomplete - not sent\n", provider);

        string_uninit(&payload);

        _http_service_oauth_row_uninit(&row);

        HTTP_Service_OAuth_Token const refused = _http_service_oauth_token_refused(self, "invalid_request");

        trace_log_pop();

        return refused;
    }

    HTTP_Service_OAuth_Token token = _http_service_oauth_send_token_request(self, _http_service_oauth_str_text(&row.token_url), &payload);

    string_uninit(&payload);

    _http_service_oauth_row_uninit(&row);

    trace_log_pop();

    return token;
}

HTTP_Service_OAuth_Token http_service_oauth_exchange_code(HTTP_Service_OAuth *const self, char const *const provider, char const *const code) {
    return http_service_oauth_exchange_code_1(self, provider, code);
}

String http_service_oauth_get_authorize_url_1(HTTP_Service_OAuth *const self, char const *const provider, char const *const state) {
    return http_service_oauth_get_authorize_url_2(self, provider, state, "", "");
}

String http_service_oauth_get_authorize_url_2(HTTP_Service_OAuth *const self, char const *const provider, char const *const state,
    char const *const code_challenge, char const *const extra_params) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "provider", (void*) provider);

    String                      url = _http_service_oauth_string_init(self);
    HTTP_Service_OAuth_Row      row = DEFAULT_INITIALIZATION;

    /* The state is DATA the caller usually read off a request, so an absent one is a null
     * pointer, not an abort: string_get_data of an empty String is null in this tree. An empty
     * state would build "&state=" - a URL the provider echoes back with nothing for
     * http_service_oauth_state_verify to check. Refused as the unknown provider is: empty URL. */
    if (_http_service_oauth_char_empty(state)) {
        trace_log_pop();

        return url;
    }

    /* This URL is what a caller puts in a "Location:" header, so a CR or LF inside the one
     * fragment that is NOT re-encoded ends that line and starts a header nobody wrote - the same
     * defect this module already refuses for an access token before building "Authorization:
     * Bearer", one function away. Appending the fragment verbatim is the feature; appending a
     * header terminator is not. Refused as an unknown provider is: the empty URL. */
    if (!_http_service_oauth_char_empty(extra_params) && _http_service_oauth_char_line_break(extra_params, char_length(extra_params))) {
        log_message_1(LOG_LEVEL_ERROR, "oauth: provider=%s extra params contain CR/LF - refusing to build an authorize URL\n", provider);

        trace_log_pop();

        return url;
    }

    // No `found` out-parameter: this builder has one refusal shape - the empty URL - for both.
    if (!_http_service_oauth_row_snapshot(self, provider, &row, nullptr)) {
        _http_service_oauth_row_uninit(&row);

        trace_log_pop();

        return url;
    }

    char const *const authorize_url = _http_service_oauth_str_text(&row.authorize_url);
    char const *const scope         = _http_service_oauth_str_text(&row.scope);

    _http_service_oauth_string_add(&url, authorize_url);

    /* An authorize endpoint may already carry a query - a tenant id, a locale. Appending a
     * second "?" made the whole parameter list one opaque value of the first one. Only "?" is
     * looked for: a configured endpoint carrying a "#" fragment would take these parameters
     * after the fragment, where no server ever sees them. Configuration, not data - see the
     * header's note on authorize_url. */
    _http_service_oauth_string_add(&url, _http_service_oauth_char_has_query(authorize_url) ? "&" : "?");
    _http_service_oauth_string_add(&url, "response_type=code&client_id=");

    /* Every encode is checked. A value over http/query's encode ceiling, or an allocator
     * refusal, used to drop that parameter and still return a URL - a redirect to the provider
     * missing its client_id or its state, which is exactly the URL a caller must never send. */
    bool encoded = http_query_encode_1(&url, _http_service_oauth_str_text(&row.client_id));

    _http_service_oauth_string_add(&url, "&redirect_uri=");

    encoded = http_query_encode_1(&url, _http_service_oauth_str_text(&row.redirect_uri)) && encoded;

    // An absent scope is omitted, not sent empty: "&scope=" is a request for no scopes at all.
    if (!_http_service_oauth_char_empty(scope)) {
        _http_service_oauth_string_add(&url, "&scope=");

        encoded = http_query_encode_1(&url, scope) && encoded;
    }

    _http_service_oauth_string_add(&url, "&state=");

    encoded = http_query_encode_1(&url, state) && encoded;

    if (!_http_service_oauth_char_empty(code_challenge)) {
        _http_service_oauth_string_add(&url, "&code_challenge=");

        encoded = http_query_encode_1(&url, code_challenge) && encoded;

        _http_service_oauth_string_add(&url, "&code_challenge_method=S256");
    }

    /* Appended verbatim: the caller states these are already encoded, and re-encoding would
     * turn "access_type=offline&prompt=consent" into one value called access_type. */
    if (!_http_service_oauth_char_empty(extra_params)) {
        _http_service_oauth_string_add(&url, "&");
        _http_service_oauth_string_add(&url, extra_params);
    }

    _http_service_oauth_row_uninit(&row);

    /* A half-built URL is worse than none: it is still a valid absolute URL the caller would
     * redirect a browser to. The empty answer is the same refusal an unknown provider gets. */
    if (!encoded) {
        log_message_1(LOG_LEVEL_ERROR, "oauth: provider=%s authorize URL incomplete - refusing to return it\n", provider);

        string_clear(&url);
    }

    trace_log_pop();

    return url;
}

String http_service_oauth_get_authorize_url(HTTP_Service_OAuth *const self, char const *const provider, char const *const state) {
    return http_service_oauth_get_authorize_url_1(self, provider, state);
}

HTTP_Service_OAuth_Profile http_service_oauth_get_profile(HTTP_Service_OAuth *const self, char const *const provider, HTTP_Service_OAuth_Token const *const token) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "provider", (void*) provider);
    error_check_null(LOG_METADATA, "token", (void*) token);

    if (string_empty(&token->access_token)) {
        HTTP_Service_OAuth_Profile const profile = _http_service_oauth_profile_refused(self, "invalid_request");

        trace_log_pop();

        return profile;
    }

    /* The access token is pasted into an "Authorization: Bearer ..." line. A CR or LF inside it
     * terminates that line, and everything after becomes a header the caller never wrote - the
     * classic response-splitting shape, aimed here at the request. Refused before the line is
     * built, not sanitized: a token containing a line break is not a token this module minted. */
    if (_http_service_oauth_char_line_break(string_get_data(&token->access_token), string_get_size(&token->access_token))) {
        log_message_1(LOG_LEVEL_ERROR, "oauth: provider=%s access token contains CR/LF - refusing to build a header from it\n", provider);

        HTTP_Service_OAuth_Profile const profile = _http_service_oauth_profile_refused(self, "invalid_request");

        trace_log_pop();

        return profile;
    }

    HTTP_Service_OAuth_Row  row   = DEFAULT_INITIALIZATION;
    bool                    found = false;

    if (!_http_service_oauth_row_snapshot(self, provider, &row, &found)) {
        _http_service_oauth_row_uninit(&row);

        // See exchange_code_2: a refused row copy is "misconfigured", not a missing registration.
        HTTP_Service_OAuth_Profile const profile = _http_service_oauth_profile_refused(self, found ? "misconfigured" : "unknown_provider");

        trace_log_pop();

        return profile;
    }

    HTTP_Service_OAuth_Profile  profile     = _http_service_oauth_profile_init(self);
    HTTP_Client                 *client     = _http_service_oauth_client_new();
    String                      header      = _http_service_oauth_string_init(self);
    String                      response    = _http_service_oauth_string_init(self);

    // A Bearer token is assumed; token.token_type is not consulted. See the header's Known gaps.
    _http_service_oauth_string_add(&header, "Authorization: Bearer ");
    _http_service_oauth_string_add(&header, string_get_data(&token->access_token));

    http_client_header_add(client, string_get_data(&header));
    http_client_header_add(client, "Accept: application/json");

    HTTP_Client_Result result = http_client_get(client, _http_service_oauth_str_text(&row.profile_url), &response);

    profile.response_code   = result.response_code;
    profile.status          = result.status;

    /* One prefix and one severity across the module: an operator greps "oauth: provider=", and a
     * failed profile fetch is a WARN - the same severity send_token_request gives a failed token
     * request, which is the same event on a different endpoint. */
    if (!result.success) {
        log_message_1(  LOG_LEVEL_WARN,
                        "oauth: provider=%s profile request failed (status=%d, code=%d)\n",
                        provider,
                        (I32) result.status,
                        (I32) result.code);

        _http_service_oauth_string_add(&profile.error, "transport");
    }

    /* Never log the provider's body verbatim (it can carry tokens/PII), and never %s an empty
     * String's data pointer (nullptr, not "") - status and size are enough to diagnose a
     * rejection. The body is still PARSED, because a 4xx is where error_description lives. */
    if (result.success && (result.response_code < 200 || result.response_code >= 300)) {
        log_message_1(  LOG_LEVEL_WARN,
                        "oauth: provider=%s profile rejected (http=%zu, bytes=%zu)\n",
                        provider,
                        result.response_code,
                        string_get_size(&response));
    }

    http_client_result_uninit(&result);

    string_uninit(&profile.raw);

    profile.raw = _http_service_oauth_string_copy(self, &response);

    _http_service_oauth_profile_parse(&profile);

    string_uninit(&response);
    string_uninit(&header);
    http_client_header_clear(client);
    http_client_delete(&client);

    _http_service_oauth_row_uninit(&row);

    trace_log_pop();

    return profile;
}

bool http_service_oauth_init_1(HTTP_Service_OAuth *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    U8 key[HTTP_SERVICE_OAUTH_STATE_KEY_SIZE] = DEFAULT_INITIALIZATION;

    // Fail closed; see http_service_oauth_alloc_init_1.
    if (result_is_error(crypto_random_bytes(key, sizeof(key)))) {
        log_message_2(LOG_LEVEL_ERROR, LOG_METADATA, "http_service_oauth: state key generation failed");

        trace_log_pop();

        return false;
    }

    bool const constructed = http_service_oauth_init_2(self, key, sizeof(key));

    memory_set(key, sizeof(key), 0);

    trace_log_pop();

    return constructed;
}

bool http_service_oauth_init_2(HTTP_Service_OAuth *const self, U8 const *const key, USize const key_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "key", (void*) key);

    if (key_size == 0 || key_size > HTTP_SERVICE_OAUTH_STATE_KEY_SIZE) {
        trace_log_pop();

        return false;
    }

    *self = (HTTP_Service_OAuth){
#ifdef ARENA_IMPLEMENTATION
        .allocator      = nullptr,
#endif // ARENA_IMPLEMENTATION
        .row_capacity   = 0,
        .row_count      = 0,
        .rows           = nullptr,
        .state_key_size = 0
    };

    bool const constructed = _http_service_oauth_construct(self, key, key_size);

    trace_log_pop();

    return constructed;
}

bool http_service_oauth_init(HTTP_Service_OAuth *const self) {
    return http_service_oauth_init_1(self);
}

bool http_service_oauth_pkce_challenge_create(char const *const verifier, char *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "verifier", (void*) verifier);
    error_check_null(LOG_METADATA, "out", (void*) out);

    USize const verifier_size = char_length(verifier);

    // RFC 7636 4.1 bounds. Refused by value: the verifier is the caller's data, not a contract.
    if (verifier_size < _HTTP_SERVICE_OAUTH_PKCE_VERIFIER_MIN_SIZE || verifier_size > _HTTP_SERVICE_OAUTH_PKCE_VERIFIER_MAX_SIZE) {
        trace_log_pop();

        return false;
    }

    U8 digest[CRYPTO_HASH_SHA256_SIZE] = DEFAULT_INITIALIZATION;

    if (result_is_error(crypto_hash_sha256((U8 const*) verifier, verifier_size, digest))) {
        trace_log_pop();

        return false;
    }

    // RFC 7636 4.2: BASE64URL-ENCODE(SHA256(ASCII(verifier))), unpadded - exactly what this is.
    encoding_base64_url_encode_1(digest, sizeof(digest), out);

    trace_log_pop();

    return true;
}

bool http_service_oauth_pkce_verifier_create(char *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "out", (void*) out);

    U8 bytes[32] = DEFAULT_INITIALIZATION;

    static_assert(sizeof(bytes) == 32, "a PKCE verifier is 32 random bytes; see _PKCE_VERIFIER_BYTES");

    /* Nothing is written on failure, so a caller that ignores the answer sends its
     * zero-initialized buffer rather than a predictable verifier this function invented. */
    if (result_is_error(crypto_random_bytes(bytes, _HTTP_SERVICE_OAUTH_PKCE_VERIFIER_BYTES))) {
        trace_log_pop();

        return false;
    }

    encoding_base64_url_encode_1(bytes, _HTTP_SERVICE_OAUTH_PKCE_VERIFIER_BYTES, out);

    memory_set(bytes, sizeof(bytes), 0);

    trace_log_pop();

    return true;
}

bool http_service_oauth_profile_is_ok(HTTP_Service_OAuth_Profile const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* The empty `error` is what token_is_ok has always required, and a profile needs it for the
     * same reason: _json_copy_error folds GitHub's {"message": ...} in as well, so a 200 that
     * carries a provider complaint alongside an id used to read as ok. Google's userinfo 200
     * carries no error field, so no real success answer changes. */
    bool const ok = self->status == HTTP_CLIENT_STATUS_OK &&
                    self->response_code >= 200            &&
                    self->response_code < 300             &&
                    string_empty(&self->error)            &&
                    !string_empty(&self->id);

    trace_log_pop();

    return ok;
}

void http_service_oauth_profile_uninit(HTTP_Service_OAuth_Profile *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    string_uninit(&self->email);
    string_uninit(&self->error);
    string_uninit(&self->error_description);
    string_uninit(&self->id);
    string_uninit(&self->name);
    string_uninit(&self->picture);
    string_uninit(&self->raw);

    trace_log_pop();
}

bool http_service_oauth_provider_add_1(HTTP_Service_OAuth *const self, HTTP_Service_OAuth_Provider const *const provider) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "provider", (void*) provider);

    thread_mutex_lock(&self->mutex);

    HTTP_Service_OAuth_Row row = DEFAULT_INITIALIZATION;

    /* The WHOLE replacement is built before anything stored is released. The update path used
     * to release each value and then build its replacement in place, so a refused copy left a
     * client secret holding the degraded empty Str - a null data pointer the next exchange
     * handed to the payload builder. Nine fields are also ONE credential set: half a row pairs
     * a new client_id with the old secret, which authenticates as nobody. */
    if (!_http_service_oauth_row_build(self, provider, &row)) {
        _http_service_oauth_row_uninit(&row);

        thread_mutex_unlock(&self->mutex);

        trace_log_pop();

        return false;
    }

    USize const index = _http_service_oauth_provider_at(self, provider->name);

    if (index != USIZE_MAX) {
        _http_service_oauth_row_uninit(&self->rows[index]);

        self->rows[index] = row;

        thread_mutex_unlock(&self->mutex);

        trace_log_pop();

        return true;
    }

    USize const capacity = self->row_capacity == 0
        ? _HTTP_SERVICE_OAUTH_INITIAL_CAPACITY
        : self->row_capacity * 2;

    if (self->row_count == self->row_capacity && !_http_service_oauth_rows_reserve(self, capacity)) {
        _http_service_oauth_row_uninit(&row);

        thread_mutex_unlock(&self->mutex);

        trace_log_pop();

        return false;
    }

    self->rows[self->row_count] = row;
    self->row_count += 1;

    thread_mutex_unlock(&self->mutex);

    trace_log_pop();

    return true;
}

bool http_service_oauth_provider_add_2(HTTP_Service_OAuth *const self,
    char const *const name, char const *const client_id, char const *const client_secret, char const *const redirect_uri, char const *const authorize_url, char const *const token_url,
    char const *const profile_url, char const *const revoke_url, char const *const scope) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    HTTP_Service_OAuth_Provider const provider = {
        .authorize_url  = authorize_url,
        .client_id      = client_id,
        .client_secret  = client_secret,
        .name           = name,
        .profile_url    = profile_url,
        .redirect_uri   = redirect_uri,
        .revoke_url     = revoke_url,
        .scope          = scope,
        .token_url      = token_url
    };

    bool const registered = http_service_oauth_provider_add_1(self, &provider);

    trace_log_pop();

    return registered;
}

HTTP_Service_OAuth_Token http_service_oauth_refresh(HTTP_Service_OAuth *const self, char const *const provider, char const *const refresh_token) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "provider", (void*) provider);

    /* DATA, refused by value: a stored refresh token read back absent arrives as the null
     * string_get_data of an empty String, and an abort here would be reachable from a request. */
    if (_http_service_oauth_char_empty(refresh_token)) {
        HTTP_Service_OAuth_Token const token = _http_service_oauth_token_refused(self, "invalid_request");

        trace_log_pop();

        return token;
    }

    HTTP_Service_OAuth_Row  row   = DEFAULT_INITIALIZATION;
    bool                    found = false;

    if (!_http_service_oauth_row_snapshot(self, provider, &row, &found)) {
        _http_service_oauth_row_uninit(&row);

        // See exchange_code_2: a refused row copy is "misconfigured", not a missing registration.
        HTTP_Service_OAuth_Token const token = _http_service_oauth_token_refused(self, found ? "misconfigured" : "unknown_provider");

        trace_log_pop();

        return token;
    }

    String payload = _http_service_oauth_string_init(self);

    // Checked for the same reason exchange_code_2's body is: a dropped field must not be sent.
    bool encoded = http_query_form_add_1(&payload, "grant_type", "refresh_token");

    encoded = http_query_form_add_1(&payload, "refresh_token", refresh_token) && encoded;
    encoded = http_query_form_add_1(&payload, "client_id", _http_service_oauth_str_text(&row.client_id)) && encoded;
    encoded = http_query_form_add_1(&payload, "client_secret", _http_service_oauth_str_text(&row.client_secret)) && encoded;

    if (!encoded) {
        log_message_1(LOG_LEVEL_ERROR, "oauth: provider=%s refresh request body incomplete - not sent\n", provider);

        string_uninit(&payload);

        _http_service_oauth_row_uninit(&row);

        HTTP_Service_OAuth_Token const refused = _http_service_oauth_token_refused(self, "invalid_request");

        trace_log_pop();

        return refused;
    }

    HTTP_Service_OAuth_Token token = _http_service_oauth_send_token_request(self, _http_service_oauth_str_text(&row.token_url), &payload);

    string_uninit(&payload);

    _http_service_oauth_row_uninit(&row);

    trace_log_pop();

    return token;
}

bool http_service_oauth_revoke_1(HTTP_Service_OAuth *const self, char const *const provider, char const *const token) {
    return http_service_oauth_revoke_2(self, provider, token, "");
}

bool http_service_oauth_revoke_2(HTTP_Service_OAuth *const self, char const *const provider, char const *const token, char const *const token_type_hint) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "provider", (void*) provider);

    /* DATA, refused by value: the token to revoke is whatever the caller has on hand, and an
     * absent one is the null string_get_data of an empty String rather than a contract breach. */
    if (_http_service_oauth_char_empty(token)) {
        trace_log_pop();

        return false;
    }

    HTTP_Service_OAuth_Row row = DEFAULT_INITIALIZATION;

    // No `found` out-parameter: revoke answers a bare bool, so both refusals have one shape.
    if (!_http_service_oauth_row_snapshot(self, provider, &row, nullptr)) {
        _http_service_oauth_row_uninit(&row);

        trace_log_pop();

        return false;
    }

    char const *const revoke_url = _http_service_oauth_str_text(&row.revoke_url);

    /* GitHub and Discord have no RFC 7009 endpoint, so revoke_url is optional configuration -
     * and a provider registered without one answers false here rather than being unregisterable. */
    if (_http_service_oauth_char_empty(revoke_url)) {
        log_message_1(LOG_LEVEL_WARN, "oauth: provider=%s has no revoke endpoint configured - nothing revoked\n", provider);

        _http_service_oauth_row_uninit(&row);

        trace_log_pop();

        return false;
    }

    HTTP_Client *client   = _http_service_oauth_client_new();
    String      payload   = _http_service_oauth_string_init(self);
    String      response  = _http_service_oauth_string_init(self);

    // Checked for the same reason exchange_code_2's body is: a dropped token revokes nothing.
    bool encoded = http_query_form_add_1(&payload, "token", token);

    encoded = http_query_form_add_1(&payload, "client_id", _http_service_oauth_str_text(&row.client_id)) && encoded;
    encoded = http_query_form_add_1(&payload, "client_secret", _http_service_oauth_str_text(&row.client_secret)) && encoded;

    if (!_http_service_oauth_char_empty(token_type_hint)) {
        encoded = http_query_form_add_1(&payload, "token_type_hint", token_type_hint) && encoded;
    }

    if (!encoded) {
        log_message_1(LOG_LEVEL_ERROR, "oauth: provider=%s revoke request body incomplete - not sent\n", provider);

        string_uninit(&response);
        string_uninit(&payload);
        http_client_delete(&client);

        _http_service_oauth_row_uninit(&row);

        trace_log_pop();

        return false;
    }

    http_client_header_add(client, "Content-Type: application/x-www-form-urlencoded");

    HTTP_Client_Result result = http_client_post_4(client, revoke_url, &payload, &response);

    /* Requires an actual 2xx, not just a completed transport: a revoke endpoint answering
     * 400/401 reached the server fine but did NOT revoke. */
    bool const value = http_client_result_is_ok(&result);

    http_client_result_uninit(&result);
    string_uninit(&response);
    string_uninit(&payload);
    http_client_header_clear(client);
    http_client_delete(&client);

    _http_service_oauth_row_uninit(&row);

    trace_log_pop();

    return value;
}

bool http_service_oauth_state_issue(HTTP_Service_OAuth *const self, char const *const provider, USize const ttl_seconds, char *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "provider", (void*) provider);
    error_check_null(LOG_METADATA, "out", (void*) out);

    if (_http_service_oauth_char_empty(provider) || ttl_seconds == 0) {
        trace_log_pop();

        return false;
    }

    char nonce[(16 * 2) + 1] = DEFAULT_INITIALIZATION;

    static_assert(sizeof(nonce) == (16 * 2) + 1, "the nonce buffer must hold 2 hex chars per random byte plus a terminator");

    // Fail closed: without randomness there is no unguessable state, and a fixed one is worse.
    if (result_is_error(crypto_random_hex(nonce, _HTTP_SERVICE_OAUTH_NONCE_SIZE))) {
        trace_log_pop();

        return false;
    }

    ISize const now = datetime_now();

    if (now <= 0) {
        trace_log_pop();

        return false;
    }

    /* A ttl within reach of USIZE_MAX wraps "now + ttl_seconds" to a SMALL expiry, so the token
     * is born already expired and every verify fails - a silently broken login rather than a
     * refused configuration. Refusing here also removes the 20-digit expiry from the set of
     * strings this service can MINT, which makes the verify side's char_to_numbers_uint_2 wrap
     * unreachable by construction rather than merely gated behind the MAC. */
    if ((USize) now > USIZE_MAX - ttl_seconds) {
        trace_log_pop();

        return false;
    }

    char expiry[21] = DEFAULT_INITIALIZATION;

    char_from_numbers_uint_1(expiry, sizeof(expiry), (USize) now + ttl_seconds);

    /* state_key and state_key_size are read WITHOUT the mutex, here and in state_verify, and
     * that is deliberate: both are written once by _construct before any thread can reach the
     * service and zeroed once by uninit, and provider_add never touches either. Every other
     * member access in this module is locked, so the one exception is worth naming. */
    String      message = _http_service_oauth_state_message(self, provider, nonce, char_length(nonce), expiry, char_length(expiry));
    char        mac[CRYPTO_HMAC_SHA256_HEX_SIZE + 1] = DEFAULT_INITIALIZATION;
    Result const signature = crypto_hmac_sha256_hex_1((char const*) self->state_key, self->state_key_size, string_get_data(&message), string_get_size(&message), mac);

    string_uninit(&message);

    if (result_is_error(signature)) {
        trace_log_pop();

        return false;
    }

    String state = _http_service_oauth_string_init(self);

    _http_service_oauth_string_add(&state, nonce);
    _http_service_oauth_string_add(&state, ".");
    _http_service_oauth_string_add(&state, expiry);
    _http_service_oauth_string_add(&state, ".");
    _http_service_oauth_string_add(&state, mac);

    USize const state_size = string_get_size(&state);

    /* The documented buffer is the only thing this can write into, so an over-long token is a
     * refusal rather than a truncated one a verify would reject days later. */
    if (state_size == 0 || state_size > HTTP_SERVICE_OAUTH_STATE_MAX_SIZE) {
        string_uninit(&state);

        trace_log_pop();

        return false;
    }

    memory_copy_2(out, HTTP_SERVICE_OAUTH_STATE_MAX_SIZE + 1, string_get_data(&state), state_size);

    out[state_size] = '\0';

    string_uninit(&state);

    trace_log_pop();

    return true;
}

bool http_service_oauth_state_verify(HTTP_Service_OAuth *const self, char const *const provider, char const *const state) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "provider", (void*) provider);

    /* The state arrives on the callback URL, so it is DATA in the most literal sense: a
     * GET /callback with no ?state= yields the null string_get_data of an empty String, and an
     * error_check_null here would let any unauthenticated request abort the server. */
    if (_http_service_oauth_char_empty(provider) || _http_service_oauth_char_empty(state)) {
        trace_log_pop();

        return false;
    }

    USize const state_size = char_length(state);

    if (state_size > HTTP_SERVICE_OAUTH_STATE_MAX_SIZE) {
        trace_log_pop();

        return false;
    }

    USize first  = USIZE_MAX;
    USize second = USIZE_MAX;
    bool  shaped = true;

    for (USize i = 0; i < state_size; i += 1) {
        if (state[i] != '.') {
            continue;
        }

        if (first == USIZE_MAX) {
            first = i;
        }
        else if (second == USIZE_MAX) {
            second = i;
        }
        else {
            shaped = false;

            break;
        }
    }

    if (!shaped || first == USIZE_MAX || second == USIZE_MAX) {
        trace_log_pop();

        return false;
    }

    char const *const nonce       = state;
    USize      const  nonce_size  = first;
    char const *const expiry      = state + first + 1;
    USize      const  expiry_size = second - first - 1;
    char const *const mac         = state + second + 1;
    USize      const  mac_size    = state_size - second - 1;

    if (nonce_size == 0 || expiry_size == 0 || expiry_size > _HTTP_SERVICE_OAUTH_EXPIRY_MAX_SIZE || mac_size != CRYPTO_HMAC_SHA256_HEX_SIZE) {
        trace_log_pop();

        return false;
    }

    for (USize i = 0; i < expiry_size; i += 1) {
        if (expiry[i] < '0' || expiry[i] > '9') {
            trace_log_pop();

            return false;
        }
    }

    /* The MAC covers the PROVIDER as well as the nonce and expiry, so a state minted for
     * "github" cannot be replayed onto "google" - the callback that redeems a code has to be
     * the same provider's callback. */
    // state_key is read unlocked here for the reason state_issue states.
    String message = _http_service_oauth_state_message(self, provider, nonce, nonce_size, expiry, expiry_size);

    // Constant-time, and checked BEFORE the expiry so the two failures take the same shape.
    bool const verified = crypto_hmac_sha256_verify_1((char const*) self->state_key, self->state_key_size, string_get_data(&message), string_get_size(&message), mac);

    string_uninit(&message);

    if (!verified) {
        trace_log_pop();

        return false;
    }

    ISize const now = datetime_now();

    if (now <= 0) {
        trace_log_pop();

        return false;
    }

    bool const live = char_to_numbers_uint_2(expiry, expiry_size) > (USize) now;

    trace_log_pop();

    return live;
}

bool http_service_oauth_token_is_ok(HTTP_Service_OAuth_Token const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    bool const ok = self->status == HTTP_CLIENT_STATUS_OK &&
                    self->response_code >= 200            &&
                    self->response_code < 300             &&
                    string_empty(&self->error)            &&
                    !string_empty(&self->access_token);

    trace_log_pop();

    return ok;
}

void http_service_oauth_token_uninit(HTTP_Service_OAuth_Token *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    string_uninit(&self->access_token);
    string_uninit(&self->error);
    string_uninit(&self->error_description);
    string_uninit(&self->id_token);
    string_uninit(&self->raw);
    string_uninit(&self->refresh_token);
    string_uninit(&self->scope);
    string_uninit(&self->token_type);

    trace_log_pop();
}

void http_service_oauth_uninit(HTTP_Service_OAuth *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    for (USize i = 0; i < self->row_count; i += 1) {
        _http_service_oauth_row_uninit(&self->rows[i]);
    }

    _http_service_oauth_release(self, (void*) self->rows);

    self->rows          = nullptr;
    self->row_capacity  = 0;
    self->row_count     = 0;

#ifdef ARENA_IMPLEMENTATION
    self->allocator = nullptr;
#endif // ARENA_IMPLEMENTATION

    // The key signs state tokens; a released service should not leave it lying in the struct.
    memory_set(self->state_key, sizeof(self->state_key), 0);

    self->state_key_size = 0;

    thread_mutex_uninit(&self->mutex);

    trace_log_pop();
}