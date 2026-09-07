#include <http/service/auth/auth.h>

/* The decoy password never authenticates anything: it exists so that a login attempt against
 * an account that does NOT exist runs a full KDF pass and costs the same wall time as one
 * against an account with the wrong password. Without it, a missing row returns in
 * microseconds and the response latency enumerates the user table. */
#define _HTTP_SERVICE_AUTH_DECOY_PASSWORD "cfw-http-service-auth-decoy"
#define _HTTP_SERVICE_AUTH_HASH_BYTE_COUNT CRYPTO_HASH_SHA256_SIZE

/*==============================================================================
 * MARK: - Internal Implementations
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
static String _http_service_auth_decoy_init(USize const password_hash_iterations, Arena *const allocator)
#else
static String _http_service_auth_decoy_init(USize const password_hash_iterations)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    if (allocator != nullptr) {
        String const decoy = crypto_password_alloc_hash_1(_HTTP_SERVICE_AUTH_DECOY_PASSWORD, password_hash_iterations, allocator);

        if (string_empty(&decoy)) {
            log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_auth: decoy hash init failed; verify_password_2 has no timing guarantee until reinit");
        }

        trace_log_pop();

        return decoy;
    }
#endif // ARENA_IMPLEMENTATION

    String const decoy = crypto_password_hash_1(_HTTP_SERVICE_AUTH_DECOY_PASSWORD, password_hash_iterations);

    if (string_empty(&decoy)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_auth: decoy hash init failed; verify_password_2 has no timing guarantee until reinit");
    }

    trace_log_pop();

    return decoy;
}

static USize _http_service_auth_now(void) {
    trace_log_push(LOG_METADATA);

    /* datetime_now is ISize. A clock set before the epoch answers a negative value, and the
     * cast alone would turn it into a far-future timestamp - a token that never expires.
     * Clamping to 0 would fail OPEN instead: token_expired_at asks `now >= expires_at`, so a
     * reading of 0 leaves every stored record - and every token minted while the clock is
     * broken - alive. USIZE_MAX is the fail-CLOSED reading: it is at or past every expiry,
     * token_create saturates to USIZE_MAX with it, and token_expired_at reads that saturated
     * value as expired under any later clock, so a broken clock can never mint an eternal
     * token. */
    ISize const now = datetime_now();

    USize const value = now < 0 ? USIZE_MAX : (USize) now;

    trace_log_pop();

    return value;
}

static String _http_service_auth_string_init(HTTP_Service_Auth const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

#ifdef ARENA_IMPLEMENTATION
    if (!memory_empty(self->allocator)) {
        String const string = string_alloc_init_1(self->allocator);

        trace_log_pop();

        return string;
    }
#endif // ARENA_IMPLEMENTATION

    String const string = string_init_1();

    trace_log_pop();

    return string;
}

static void _http_service_auth_string_terminate(String *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    char            terminator  = '\0';
    USize   const   size        = string_get_size(self);

    string_add_last_2(self, &terminator, 1);
    string_set_size(self, size);

    trace_log_pop();
}

static bool _http_service_auth_token_verify_hash(HTTP_Service_Auth const *const self, char const *const token, char const *const hash, USize const hash_size, USize const expires_at) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "token", (void*) token);
    error_check_null(LOG_METADATA, "hash", (void*) hash);

    /* USIZE_MAX is the token_expired_at saturation sentinel, and it is also what a NEGATIVE
     * stored expires_at becomes when the call site casts its column to USize - a malicious or
     * corrupt row would otherwise read as far-future. Both refuse here. */
    if (hash_size == 0 || expires_at == USIZE_MAX || _http_service_auth_now() >= expires_at) {
        trace_log_pop();

        return false;
    }

    /* A token this service minted is EXACTLY ENCODING_HEX_SIZE(self->token_byte_count) bytes
     * of hex, so any other length cannot be one of them and is refused before a single byte
     * is hashed. That does three things the hash comparison alone did not: it makes the
     * header's empty-candidate promise true rather than accidental, it closes the chain
     * "CSPRNG fails -> caller ignores the empty token -> persists token_hash of an empty
     * value -> an empty candidate verifies", and it bounds the SHA-256 pass to a token-sized
     * input instead of whatever the request body cap allows - the same bound the password
     * path takes from password_max_length. */
    if (char_length(token) != ENCODING_HEX_SIZE(self->token_byte_count)) {
        trace_log_pop();

        return false;
    }

    String  candidate   = http_service_auth_token_hash(self, token);
    bool    success     = false;

    if (!string_empty(&candidate)) {
        success = char_compare_equal_comptime_2(string_get_data(&candidate), string_get_size(&candidate), hash, hash_size);
    }

    string_uninit(&candidate);

    trace_log_pop();

    return success;
}

/*==============================================================================
 * MARK: - Public Implementations
 *============================================================================*/
#ifdef ARENA_IMPLEMENTATION
HTTP_Service_Auth http_service_auth_alloc_init_1(Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    HTTP_Service_Auth const auth = http_service_auth_alloc_init_3(
        HTTP_SERVICE_AUTH_DEFAULT_PASSWORD_MIN_LENGTH, HTTP_SERVICE_AUTH_DEFAULT_PASSWORD_MAX_LENGTH, HTTP_SERVICE_AUTH_DEFAULT_HASH_ITERATIONS,
        HTTP_SERVICE_AUTH_DEFAULT_TOKEN_BYTE_COUNT, HTTP_SERVICE_AUTH_DEFAULT_RESET_TOKEN_TTL, HTTP_SERVICE_AUTH_DEFAULT_EMAIL_TOKEN_TTL, allocator);

    trace_log_pop();

    return auth;
}

HTTP_Service_Auth http_service_auth_alloc_init_2(
    USize const password_min_length, USize const password_hash_iterations, USize const token_byte_count, USize const reset_token_ttl, USize const email_token_ttl, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    HTTP_Service_Auth const auth = http_service_auth_alloc_init_3(
        password_min_length, HTTP_SERVICE_AUTH_DEFAULT_PASSWORD_MAX_LENGTH, password_hash_iterations, token_byte_count, reset_token_ttl, email_token_ttl, allocator);

    trace_log_pop();

    return auth;
}

HTTP_Service_Auth http_service_auth_alloc_init_3(
    USize const password_min_length, USize const password_max_length, USize const password_hash_iterations, USize const token_byte_count, USize const reset_token_ttl,
    USize const email_token_ttl, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);
    error_check_non_value_uint(LOG_METADATA, "password_min_length", password_min_length);
    error_check_non_value_uint(LOG_METADATA, "password_max_length", password_max_length);
    error_check_wrong_value(LOG_METADATA, "password_max_length", password_max_length < HTTP_SERVICE_AUTH_PASSWORD_MAX_LENGTH_FLOOR);
    error_check_wrong_value(LOG_METADATA, "password_max_length", password_max_length > HTTP_SERVICE_AUTH_PASSWORD_MAX_LENGTH_CEILING);
    /* Named as the PAIR: init_2 and alloc_init_2 do not take a maximum, so a bare
     * "password_max_length" in the abort message would name a parameter their caller never
     * passed. What they actually broke is a password_min_length above the default ceiling. */
    error_check_wrong_value(LOG_METADATA, "password_max_length < password_min_length", password_max_length < password_min_length);
    error_check_non_value_uint(LOG_METADATA, "password_hash_iterations", password_hash_iterations);
    error_check_wrong_value(LOG_METADATA, "password_hash_iterations", password_hash_iterations > CRYPTO_PASSWORD_ITERATIONS_MAX);
    error_check_non_value_uint(LOG_METADATA, "token_byte_count", token_byte_count);
    error_check_non_value_uint(LOG_METADATA, "reset_token_ttl", reset_token_ttl);
    error_check_non_value_uint(LOG_METADATA, "email_token_ttl", email_token_ttl);

    HTTP_Service_Auth const auth = {
        .allocator                  = allocator,
        .decoy_hash                 = _http_service_auth_decoy_init(password_hash_iterations, allocator),
        .email_token_ttl            = email_token_ttl,
        .password_hash_iterations   = password_hash_iterations,
        .password_max_length        = password_max_length,
        .password_min_length        = password_min_length,
        .reset_token_ttl            = reset_token_ttl,
        .token_byte_count           = token_byte_count
    };

    trace_log_pop();

    return auth;
}
#endif // ARENA_IMPLEMENTATION

HTTP_Service_Auth_Token http_service_auth_email_token_create(HTTP_Service_Auth const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    HTTP_Service_Auth_Token const token = http_service_auth_token_create(self, self->email_token_ttl);

    trace_log_pop();

    return token;
}

String http_service_auth_hash_password(HTTP_Service_Auth const *const self, char const *const password) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "password", (void*) password);

    /* Same DoS bound password_reason enforces: refuse before ANY crypto_password_* call, so a
     * multi-megabyte password never reaches the KDF just because a caller skipped policy. */
    if (char_length(password) > self->password_max_length) {
        trace_log_pop();

        return (String) DEFAULT_INITIALIZATION;
    }

#ifdef ARENA_IMPLEMENTATION
    if (!memory_empty(self->allocator)) {
        String const string = crypto_password_alloc_hash_1(password, self->password_hash_iterations, self->allocator);

        trace_log_pop();

        return string;
    }
#endif // ARENA_IMPLEMENTATION

    String const string = crypto_password_hash_1(password, self->password_hash_iterations);

    trace_log_pop();

    return string;
}

HTTP_Service_Auth http_service_auth_init_1(void) {
    trace_log_push(LOG_METADATA);

    HTTP_Service_Auth const auth = http_service_auth_init_3(
        HTTP_SERVICE_AUTH_DEFAULT_PASSWORD_MIN_LENGTH, HTTP_SERVICE_AUTH_DEFAULT_PASSWORD_MAX_LENGTH, HTTP_SERVICE_AUTH_DEFAULT_HASH_ITERATIONS,
        HTTP_SERVICE_AUTH_DEFAULT_TOKEN_BYTE_COUNT, HTTP_SERVICE_AUTH_DEFAULT_RESET_TOKEN_TTL, HTTP_SERVICE_AUTH_DEFAULT_EMAIL_TOKEN_TTL);

    trace_log_pop();

    return auth;
}

HTTP_Service_Auth http_service_auth_init_2(
    USize const password_min_length, USize const password_hash_iterations, USize const token_byte_count, USize const reset_token_ttl, USize const email_token_ttl) {
    trace_log_push(LOG_METADATA);

    HTTP_Service_Auth const auth = http_service_auth_init_3(
        password_min_length, HTTP_SERVICE_AUTH_DEFAULT_PASSWORD_MAX_LENGTH, password_hash_iterations, token_byte_count, reset_token_ttl, email_token_ttl);

    trace_log_pop();

    return auth;
}

HTTP_Service_Auth http_service_auth_init_3(
    USize const password_min_length, USize const password_max_length, USize const password_hash_iterations, USize const token_byte_count, USize const reset_token_ttl,
    USize const email_token_ttl) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "password_min_length", password_min_length);
    error_check_non_value_uint(LOG_METADATA, "password_max_length", password_max_length);
    error_check_wrong_value(LOG_METADATA, "password_max_length", password_max_length < HTTP_SERVICE_AUTH_PASSWORD_MAX_LENGTH_FLOOR);
    error_check_wrong_value(LOG_METADATA, "password_max_length", password_max_length > HTTP_SERVICE_AUTH_PASSWORD_MAX_LENGTH_CEILING);
    /* Named as the PAIR: init_2 and alloc_init_2 do not take a maximum, so a bare
     * "password_max_length" in the abort message would name a parameter their caller never
     * passed. What they actually broke is a password_min_length above the default ceiling. */
    error_check_wrong_value(LOG_METADATA, "password_max_length < password_min_length", password_max_length < password_min_length);
    error_check_non_value_uint(LOG_METADATA, "password_hash_iterations", password_hash_iterations);
    error_check_wrong_value(LOG_METADATA, "password_hash_iterations", password_hash_iterations > CRYPTO_PASSWORD_ITERATIONS_MAX);
    error_check_non_value_uint(LOG_METADATA, "token_byte_count", token_byte_count);
    error_check_non_value_uint(LOG_METADATA, "reset_token_ttl", reset_token_ttl);
    error_check_non_value_uint(LOG_METADATA, "email_token_ttl", email_token_ttl);

    HTTP_Service_Auth const auth = {
#ifdef ARENA_IMPLEMENTATION
        .allocator                  = nullptr,
        .decoy_hash                 = _http_service_auth_decoy_init(password_hash_iterations, nullptr),
#else
        .decoy_hash                 = _http_service_auth_decoy_init(password_hash_iterations),
#endif // ARENA_IMPLEMENTATION
        .email_token_ttl            = email_token_ttl,
        .password_hash_iterations   = password_hash_iterations,
        .password_max_length        = password_max_length,
        .password_min_length        = password_min_length,
        .reset_token_ttl            = reset_token_ttl,
        .token_byte_count           = token_byte_count
    };

    trace_log_pop();

    return auth;
}

bool http_service_auth_password_needs_rehash(HTTP_Service_Auth const *const self, String const *const hash) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "hash", (void*) hash);

    if (string_empty(hash)) {
        trace_log_pop();

        return false;
    }

    bool const success = crypto_password_needs_rehash(string_get_data(hash), self->password_hash_iterations);

    if (success) {
        /* INFO, not WARN: during an iteration-count migration EVERY legacy login takes this
         * branch, so a warning per login is the expected path reported as a fault. */
        log_message_2(LOG_LEVEL_INFO, LOG_METADATA, "stored password record's iteration count differs from the configured one; it will re-hash on the next successful login");
    }

    trace_log_pop();

    return success;
}

HTTP_Service_Auth_Password_Reason http_service_auth_password_reason(HTTP_Service_Auth const *const self, char const *const password) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "password", (void*) password);

    /* char_length counts BYTES. A UTF-8 password of twelve bytes can be three characters, so
     * this floor is a storage bound, not a strength one - documented in the header. */
    USize const size = char_length(password);

    if (size < self->password_min_length) {
        trace_log_pop();

        return HTTP_SERVICE_AUTH_PASSWORD_TOO_SHORT;
    }

    /* The ceiling is the denial-of-service bound. PBKDF2 pre-hashes anything past the block
     * size, but a multi-megabyte body still costs a SHA-256 pass per login attempt, so the
     * refusal must land BEFORE any KDF work. */
    if (size > self->password_max_length) {
        trace_log_pop();

        return HTTP_SERVICE_AUTH_PASSWORD_TOO_LONG;
    }

    trace_log_pop();

    return HTTP_SERVICE_AUTH_PASSWORD_OK;
}

bool http_service_auth_password_valid(HTTP_Service_Auth const *const self, char const *const password) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "password", (void*) password);

    bool const value = http_service_auth_password_reason(self, password) == HTTP_SERVICE_AUTH_PASSWORD_OK;

    trace_log_pop();

    return value;
}

HTTP_Service_Auth_Token http_service_auth_reset_token_create(HTTP_Service_Auth const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    HTTP_Service_Auth_Token const token = http_service_auth_token_create(self, self->reset_token_ttl);

    trace_log_pop();

    return token;
}

HTTP_Service_Auth_Token http_service_auth_token_create(HTTP_Service_Auth const *const self, USize const ttl) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_non_value_uint(LOG_METADATA, "ttl", ttl);

#ifdef ARENA_IMPLEMENTATION
    char *const buffer = (char*) allocator_borrow(CRYPTO_RANDOM_HEX_SIZE(self->token_byte_count) + CHAR_END_CHARACTER, nullptr);
#else
    char *const buffer = (char*) allocator_borrow(CRYPTO_RANDOM_HEX_SIZE(self->token_byte_count) + CHAR_END_CHARACTER);
#endif // ARENA_IMPLEMENTATION

    USize const now = _http_service_auth_now();

    HTTP_Service_Auth_Token token = {
        /* Saturate rather than wrap: a ttl large enough to overflow would otherwise land the
         * expiry in the past. USIZE_MAX is the token_expired_at sentinel, so a saturated
         * expiry - whether the ttl overflowed or the clock was broken - is permanently
         * expired rather than eternal. */
        .expires_at = ttl > USIZE_MAX - now ? USIZE_MAX : now + ttl,
        .value      = _http_service_auth_string_init(self)
    };

    // allocator_borrow is the aborting variant, so buffer is never null here;
    // the only remaining failure is the CSPRNG, which leaves the token empty.
    if (result_is_success(crypto_random_hex(buffer, self->token_byte_count))) {
        string_add_last_2(&token.value, buffer, CRYPTO_RANDOM_HEX_SIZE(self->token_byte_count));
        _http_service_auth_string_terminate(&token.value);
    }

#ifdef ARENA_IMPLEMENTATION
    allocator_release(buffer, nullptr);
#else
    allocator_release(buffer);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return token;
}

bool http_service_auth_token_expired(HTTP_Service_Auth_Token const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    bool const expired = http_service_auth_token_expired_at(self, _http_service_auth_now());

    trace_log_pop();

    return expired;
}

bool http_service_auth_token_expired_at(HTTP_Service_Auth_Token const *const self, USize const now) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* Inclusive boundary, matching http_service_session_token_expired: a token whose
     * expires_at equals the current second is expired. The two services used to disagree by
     * one second, which is a live token session would have refused.
     *
     * USIZE_MAX is the saturation sentinel token_create writes when the ttl overflows or the
     * clock is broken. It has to read as expired under EVERY clock reading, not just under
     * USIZE_MAX itself: a token minted while the clock was pre-epoch would otherwise outlive
     * the repair and become the eternal token the clamp exists to prevent. */
    bool const expired = self->expires_at == USIZE_MAX || now >= self->expires_at;

    trace_log_pop();

    return expired;
}

String http_service_auth_token_hash(HTTP_Service_Auth const *const self, char const *const token) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "token", (void*) token);

    /* The same exact-length gate the verification path applies, and for the same reason: this
     * service hashes only the tokens it mints. Refusing here is what stops the empty String a
     * CSPRNG failure returns from ever being hashed and persisted as a real-looking row - a
     * row an empty candidate would then verify against. */
    if (char_length(token) != ENCODING_HEX_SIZE(self->token_byte_count)) {
        trace_log_pop();

        return (String) DEFAULT_INITIALIZATION;
    }

    U8      hash[_HTTP_SERVICE_AUTH_HASH_BYTE_COUNT]                                        = DEFAULT_INITIALIZATION;
    char    hex[ENCODING_HEX_SIZE(_HTTP_SERVICE_AUTH_HASH_BYTE_COUNT) + CHAR_END_CHARACTER]  = DEFAULT_INITIALIZATION;

    if (result_is_error(crypto_hash_sha256((U8 const*) token, char_length(token), hash))) {
        // Fail closed with an empty String - every persist site checks string_empty before
        // writing the hash, and verify_2 refuses an empty stored hash.
        trace_log_pop();

        return (String) DEFAULT_INITIALIZATION;
    }

    encoding_hex_encode_1(hash, _HTTP_SERVICE_AUTH_HASH_BYTE_COUNT, hex);

    String string = _http_service_auth_string_init(self);

    string_add_last_2(&string, hex, ENCODING_HEX_SIZE(_HTTP_SERVICE_AUTH_HASH_BYTE_COUNT));
    _http_service_auth_string_terminate(&string);

    trace_log_pop();

    return string;
}

void http_service_auth_token_uninit(HTTP_Service_Auth_Token *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    string_uninit(&self->value);

    self->expires_at = 0;

    trace_log_pop();
}

bool http_service_auth_token_verify_1(HTTP_Service_Auth_Token const *const self, char const *const token) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "token", (void*) token);

    bool const success = http_service_auth_token_verify_at(self, token, _http_service_auth_now());

    trace_log_pop();

    return success;
}

bool http_service_auth_token_verify_2(HTTP_Service_Auth const *const self, char const *const token, String const *const hash, USize const expires_at) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "token", (void*) token);
    error_check_null(LOG_METADATA, "hash", (void*) hash);

    /* An empty String's data pointer is NULL, so the emptiness is settled here rather than
     * inside the shared helper, which takes a non-null buffer. */
    if (string_empty(hash)) {
        trace_log_pop();

        return false;
    }

    bool const success = _http_service_auth_token_verify_hash(self, token, string_get_data(hash), string_get_size(hash), expires_at);

    trace_log_pop();

    return success;
}

bool http_service_auth_token_verify_3(HTTP_Service_Auth const *const self, char const *const token, char const *const hash, USize const expires_at) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "token", (void*) token);
    error_check_null(LOG_METADATA, "hash", (void*) hash);

    bool const success = _http_service_auth_token_verify_hash(self, token, hash, char_length(hash), expires_at);

    trace_log_pop();

    return success;
}

bool http_service_auth_token_verify_at(HTTP_Service_Auth_Token const *const self, char const *const token, USize const now) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "token", (void*) token);

    if (http_service_auth_token_expired_at(self, now)) {
        trace_log_pop();

        return false;
    }

    USize   const   value_size  = string_get_size(&self->value);
    USize   const   token_size  = char_length(token);
    bool            success     = false;

    if (!string_empty(&self->value)) {
        char const *const value = string_get_data(&self->value);

        success = char_compare_equal_comptime_2(value, value_size, token, token_size);
    }

    trace_log_pop();

    return success;
}

void http_service_auth_uninit(HTTP_Service_Auth *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    string_uninit(&self->decoy_hash);

    self->email_token_ttl           = 0;
#ifdef ARENA_IMPLEMENTATION
    self->allocator                 = nullptr;
#endif // ARENA_IMPLEMENTATION
    self->password_hash_iterations  = 0;
    self->password_max_length       = 0;
    self->password_min_length       = 0;
    self->reset_token_ttl           = 0;
    self->token_byte_count          = 0;

    trace_log_pop();
}

bool http_service_auth_verify_password_1(HTTP_Service_Auth const *const self, char const *const password, String const *const hash) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "password", (void*) password);
    error_check_null(LOG_METADATA, "hash", (void*) hash);

    if (string_empty(hash)) {
        trace_log_pop();

        return false;
    }

    /* Same DoS bound as hash_password: refuse before crypto_password_verify_1 runs. */
    if (char_length(password) > self->password_max_length) {
        trace_log_pop();

        return false;
    }

    /* self is validated but does not gate verification: the iteration count comes from the
     * STORED RECORD, which is what lets an old record still verify after the service's
     * configured count is raised. */
    bool const success = crypto_password_verify_1(password, string_get_data(hash));

    trace_log_pop();

    return success;
}

bool http_service_auth_verify_password_2(HTTP_Service_Auth const *const self, char const *const password, String const *const hash) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "password", (void*) password);
    error_check_null(LOG_METADATA, "hash", (void*) hash);

    /* Same DoS bound as hash_password: refuse before EITHER crypto_password_verify_1 call
     * below runs, decoy included - the ceiling exists so an oversize body never reaches a KDF
     * at all, not even the decoy one that would otherwise burn real wall time on it. */
    if (char_length(password) > self->password_max_length) {
        trace_log_pop();

        return false;
    }

    if (string_empty(hash)) {
        /* No account row. Burn the same KDF cost the real path would, then refuse. The result
         * is deliberately discarded: the decoy password is never the caller's, so this can
         * only ever be false, and the call exists purely for its wall time. */
        if (!string_empty(&self->decoy_hash)) {
            (void) crypto_password_verify_1(password, string_get_data(&self->decoy_hash));
        }

        trace_log_pop();

        return false;
    }

    bool const success = crypto_password_verify_1(password, string_get_data(hash));

    trace_log_pop();

    return success;
}

bool http_service_auth_verify_password_3(HTTP_Service_Auth const *const self, char const *const password, char const *const hash) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "password", (void*) password);
    error_check_null(LOG_METADATA, "hash", (void*) hash);

    /* Same DoS bound as hash_password: refuse before EITHER crypto_password_verify_1 call
     * below runs, decoy included - the ceiling exists so an oversize body never reaches a KDF
     * at all, not even the decoy one that would otherwise burn real wall time on it. */
    if (char_length(password) > self->password_max_length) {
        trace_log_pop();

        return false;
    }

    /* A database cell's emptiness is its FIRST BYTE, not memory_empty: the column read hands
     * back a live buffer either way, so only the terminator separates "no account row" from a
     * stored record. */
    if (hash[0] == '\0') {
        /* No account row. Burn the same KDF cost the real path would, then refuse - see
         * http_service_auth_verify_password_2() for why the result is discarded. */
        if (!string_empty(&self->decoy_hash)) {
            (void) crypto_password_verify_1(password, string_get_data(&self->decoy_hash));
        }

        trace_log_pop();

        return false;
    }

    bool const success = crypto_password_verify_1(password, hash);

    trace_log_pop();

    return success;
}