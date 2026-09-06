#include <http/service/security/security.h>

/*==============================================================================
 * MARK: - API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
bool http_service_security_alloc_init_1(HTTP_Service_Security *const self, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    self->allocator = allocator;

    bool const initialized = http_headers_security_alloc_init_1(&self->policy, allocator);

    if (!initialized) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_security_alloc_init_1: refusing the whole service - the security policy failed to initialize");

        *self = (HTTP_Service_Security) DEFAULT_INITIALIZATION;

        trace_log_pop();

        return false;
    }

    /* Rendered once, here, rather than per response (report High 5): apply() becomes a plain
     * header_add_raw of this cached block instead of one String alloc+format+free per response. */
    self->header_block = http_headers_security_create_2(&self->policy);

    if (string_empty(&self->header_block)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_security_alloc_init_1: refusing the whole service - the arena refused the rendered header block");

        http_headers_security_uninit(&self->policy);

        *self = (HTTP_Service_Security) DEFAULT_INITIALIZATION;

        trace_log_pop();

        return false;
    }

    trace_log_pop();

    return true;
}

HTTP_Service_Security http_service_security_alloc_init_2(HTTP_Headers_Security *const policy, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "policy", (void*) policy);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    HTTP_Service_Security self = {
        .allocator      = allocator,
        .header_block   = DEFAULT_INITIALIZATION,
        .policy         = *policy
    };

    /* Move, not a borrow (R1 Mid 16): *policy's Strings are now owned by self.policy, so the
     * source is zeroed to close the double-free window a stale caller-side uninit(policy) would
     * otherwise open. Zeroed as a WHOLE (report Low 12) rather than field by field: naming the
     * four String members meant a fifth one added to HTTP_Headers_Security would keep a live
     * copy in the moved-from policy and reopen exactly that window, silently. */
    *policy = (HTTP_Headers_Security) DEFAULT_INITIALIZATION;

    self.header_block = http_headers_security_create_2(&self.policy);

    if (string_empty(&self.header_block)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_security_alloc_init_2: the arena refused the rendered header block - service will serve responses with no security headers");
    }

    trace_log_pop();

    return self;
}
#endif // ARENA_IMPLEMENTATION

void http_service_security_apply(HTTP_Service_Security const *const self, HTTP_Server_Response *const response) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "response", (void*) response);

    /* Cached at construction (report High 5) - no per-response render, no per-response free. */
    if (!string_empty(&self->header_block)) {
        http_server_response_header_add_raw(response, string_get_data(&self->header_block), string_get_size(&self->header_block));
    }

    trace_log_pop();
}

HTTP_Service_Security http_service_security_init_1(void) {
    trace_log_push(LOG_METADATA);

    /* No .allocator designator: the field exists only in an arena build, and these two heap
     * constructors never set it - an omitted member is zero-initialized either way. */
    HTTP_Service_Security self = {
        .header_block   = DEFAULT_INITIALIZATION,
        .policy         = http_headers_security_init_1()
    };

    self.header_block = http_headers_security_create_2(&self.policy);

    /* The same WARN its three siblings carry (report Low 12): a refused render is the one way
     * this service silently stops emitting any security header at all, and init_1 - the default
     * every consumer in this tree uses - was the one constructor that said nothing about it. */
    if (string_empty(&self.header_block)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_security_init_1: the allocator refused the rendered header block - service will serve responses with no security headers");
    }

    trace_log_pop();

    return self;
}

HTTP_Service_Security http_service_security_init_2(HTTP_Headers_Security *const policy) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "policy", (void*) policy);

    HTTP_Service_Security self = {
        .header_block   = DEFAULT_INITIALIZATION,
        .policy         = *policy
    };

    /* Move, not a borrow (R1 Mid 16): see http_service_security_alloc_init_2, including why the
     * whole struct is zeroed rather than its String members one by one. */
    *policy = (HTTP_Headers_Security) DEFAULT_INITIALIZATION;

    self.header_block = http_headers_security_create_2(&self.policy);

    if (string_empty(&self.header_block)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_security_init_2: the allocator refused the rendered header block - service will serve responses with no security headers");
    }

    trace_log_pop();

    return self;
}

void http_service_security_uninit(HTTP_Service_Security *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    string_uninit(&self->header_block);
    http_headers_security_uninit(&self->policy);

#ifdef ARENA_IMPLEMENTATION
    self->allocator = nullptr;
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();
}