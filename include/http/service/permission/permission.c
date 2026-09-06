#include <http/service/permission/permission.h>

/*==============================================================================
 * MARK: - Helpers
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
static Str _http_service_permission_char_to_str(char const *const data, Arena *const allocator)
#else
static Str _http_service_permission_char_to_str(char const *const data)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    USize const data_size = char_length(data);

#ifdef ARENA_IMPLEMENTATION
    if (allocator != nullptr) {
        Str const str = str_alloc_init_static(data, data_size, allocator);

        trace_log_pop();

        return str;
    }
#endif // ARENA_IMPLEMENTATION

    // str_init_static allocates an OWNED copy; str_init_3 would build a non-owning view over
    // the char_new_3 block, which str_uninit then skips - leaking it.
    Str const str = str_init_static(data, data_size);

    trace_log_pop();

    return str;
}

static bool _http_service_permission_list_has_2(AL_Str const *const list, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "list", (void*) list);
    error_check_null(LOG_METADATA, "data", (void*) data);

    bool success = false;

    for (USize i = 0; i < al_str_get_size(list); i += 1) {
        if (str_compare_equal_2(al_str_at(list, i), data, data_size)) {
            success = true;

            break;
        }
    }

    trace_log_pop();

    return success;
}

static bool _http_service_permission_list_has(AL_Str const *const list, char const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "list", (void*) list);
    error_check_null(LOG_METADATA, "data", (void*) data);

    bool const success = _http_service_permission_list_has_2(list, data, char_length(data));

    trace_log_pop();

    return success;
}

#ifdef ARENA_IMPLEMENTATION
static bool _http_service_permission_list_add(AL_Str *const list, char const *const data, Arena *const allocator)
#else
static bool _http_service_permission_list_add(AL_Str *const list, char const *const data)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "list", (void*) list);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Already present: the postcondition the caller asked for already holds, so this
     * is success, not a refusal. */
    if (_http_service_permission_list_has(list, data)) {
        trace_log_pop();

        return true;
    }

#ifdef ARENA_IMPLEMENTATION
    Str temp = _http_service_permission_char_to_str(data, allocator);
#else
    Str temp = _http_service_permission_char_to_str(data);
#endif // ARENA_IMPLEMENTATION

    /* str_alloc_init_static degrades to the EMPTY Str when the arena refuses the copy.
     * Storing that is worse than storing nothing: _http_service_permission_list_any
     * hands every entry of a check list to _list_has as a raw char*, and an empty Str
     * carries a null data pointer straight into char_length.
     *
     * This is also where an EMPTY VALUE is refused: "" copies to a zero-sized Str, and a
     * role, permission, scope or deny marker named "" cannot match anything a request
     * carries. memory_empty(data) is deliberately NOT tested - `data` is null-checked
     * above, so that guard was always true and said nothing. */
    if (str_get_size(&temp) == 0) {
        str_uninit(&temp);

        trace_log_pop();

        return false;
    }

    USize const stored_before = al_str_get_size(list);

    al_str_add_last(list, &temp);

    /* add_last DECLINES rather than growing when the allocator refuses, and reports it
     * by leaving the size alone. `temp` owns a fresh COPY of `data`, so a dropped node
     * puts that copy beyond the list's uninit - release it while this frame still
     * owns it. */
    if (al_str_get_size(list) == stored_before) {
        str_uninit(&temp);

        trace_log_pop();

        return false;
    }

    trace_log_pop();

    return true;
}

static bool _http_service_permission_list_any(AL_Str const *const subject, AL_Str const *const check) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "subject", (void*) subject);
    error_check_null(LOG_METADATA, "check", (void*) check);

    bool success = al_str_get_size(check) == 0;

    for (USize i = 0; i < al_str_get_size(check); i += 1) {
        Str const *const item = al_str_at(check, i);

        if (_http_service_permission_list_has_2(subject, str_get_data(item), str_get_size(item))) {
            success = true;

            break;
        }
    }

    trace_log_pop();

    return success;
}

static bool _http_service_permission_list_all(AL_Str const *const subject, AL_Str const *const check) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "subject", (void*) subject);
    error_check_null(LOG_METADATA, "check", (void*) check);

    bool success = true;

    for (USize i = 0; i < al_str_get_size(check); i += 1) {
        Str const *const item = al_str_at(check, i);

        if (!_http_service_permission_list_has_2(subject, str_get_data(item), str_get_size(item))) {
            success = false;

            break;
        }
    }

    trace_log_pop();

    return success;
}

static bool _http_service_permission_list_intersects(AL_Str const *const left, AL_Str const *const right) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "left", (void*) left);
    error_check_null(LOG_METADATA, "right", (void*) right);

    bool success = false;

    for (USize i = 0; i < al_str_get_size(right); i += 1) {
        Str const *const item = al_str_at(right, i);

        if (_http_service_permission_list_has_2(left, str_get_data(item), str_get_size(item))) {
            success = true;

            break;
        }
    }

    trace_log_pop();

    return success;
}

static bool _http_service_permission_scope_has(AL_Str const *const subject, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "subject", (void*) subject);
    error_check_null(LOG_METADATA, "data", (void*) data);

    bool success = false;

    for (USize i = 0; i < al_str_get_size(subject); i += 1) {
        Str        const *const item            = al_str_at(subject, i);
        char       const *const pattern         = str_get_data(item);
        USize      const        pattern_size    = str_get_size(item);

        /* The wildcard is a SUFFIX only, and only on the subject side: "read:*" held by the
         * subject satisfies a required "read:users". It is never read on the check side, and
         * never for roles, permissions or deny markers - a wildcard that could widen a deny
         * marker would let a suspended subject spell its way out of the block. */
        if (pattern_size > 0 && pattern[pattern_size - 1] == HTTP_SERVICE_PERMISSION_WILDCARD) {
            USize const prefix_size = pattern_size - 1;

            if (prefix_size == 0 || (data_size >= prefix_size && char_compare_equal_comptime_2(pattern, prefix_size, data, prefix_size))) {
                success = true;

                break;
            }
        }
        else if (str_compare_equal_2(item, data, data_size)) {
            success = true;

            break;
        }
    }

    trace_log_pop();

    return success;
}

static bool _http_service_permission_scope_all(AL_Str const *const subject, AL_Str const *const check) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "subject", (void*) subject);
    error_check_null(LOG_METADATA, "check", (void*) check);

    bool success = true;

    for (USize i = 0; i < al_str_get_size(check); i += 1) {
        Str const *const item = al_str_at(check, i);

        if (!_http_service_permission_scope_has(subject, str_get_data(item), str_get_size(item))) {
            success = false;

            break;
        }
    }

    trace_log_pop();

    return success;
}

/*==============================================================================
 * MARK: - API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
HTTP_Service_Permission http_service_permission_alloc_init_1(Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    HTTP_Service_Permission const permission = {
        .allocator      = allocator,
        .denies         = al_str_alloc_init_1(allocator),
        .failed         = false,
        .permissions    = al_str_alloc_init_1(allocator),
        .roles          = al_str_alloc_init_1(allocator),
        .scopes         = al_str_alloc_init_1(allocator)
    };

    trace_log_pop();

    return permission;
}

HTTP_Service_Permission_Check http_service_permission_check_alloc_init_1(Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    HTTP_Service_Permission_Check const check = {
        .allocator      = allocator,
        .denies         = al_str_alloc_init_1(allocator),
        .failed         = false,
        .permissions    = al_str_alloc_init_1(allocator),
        .roles          = al_str_alloc_init_1(allocator),
        .scopes         = al_str_alloc_init_1(allocator)
    };

    trace_log_pop();

    return check;
}
#endif // ARENA_IMPLEMENTATION

bool http_service_permission_allowed(HTTP_Service_Permission const *const self, HTTP_Service_Permission_Check const *const check) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "check", (void*) check);

    bool const success = http_service_permission_evaluate(self, check) == HTTP_SERVICE_PERMISSION_VERDICT_ALLOWED;

    trace_log_pop();

    return success;
}

bool http_service_permission_check_deny_add(HTTP_Service_Permission_Check *const self, char const *const deny) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "deny", (void*) deny);

#ifdef ARENA_IMPLEMENTATION
    bool const stored = _http_service_permission_list_add(&self->denies, deny, self->allocator);
#else
    bool const stored = _http_service_permission_list_add(&self->denies, deny);
#endif // ARENA_IMPLEMENTATION

    self->failed = self->failed || !stored;

    trace_log_pop();

    return stored;
}

bool http_service_permission_check_deny_has(HTTP_Service_Permission_Check const *const self, char const *const deny) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "deny", (void*) deny);

    bool const success = _http_service_permission_list_has(&self->denies, deny);

    trace_log_pop();

    return success;
}

bool http_service_permission_check_empty(HTTP_Service_Permission_Check const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    bool const empty = al_str_get_size(&self->denies) == 0 && al_str_get_size(&self->permissions) == 0 && al_str_get_size(&self->roles) == 0 && al_str_get_size(&self->scopes) == 0;

    trace_log_pop();

    return empty;
}

bool http_service_permission_check_failed(HTTP_Service_Permission_Check const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    bool const failed = self->failed;

    trace_log_pop();

    return failed;
}

HTTP_Service_Permission_Check http_service_permission_check_init_1(void) {
    trace_log_push(LOG_METADATA);

    HTTP_Service_Permission_Check const check = {
#ifdef ARENA_IMPLEMENTATION
        .allocator      = nullptr,
#endif // ARENA_IMPLEMENTATION
        .denies         = al_str_init_1(),
        .failed         = false,
        .permissions    = al_str_init_1(),
        .roles          = al_str_init_1(),
        .scopes         = al_str_init_1()
    };

    trace_log_pop();

    return check;
}

bool http_service_permission_check_permission_add(HTTP_Service_Permission_Check *const self, char const *const permission) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "permission", (void*) permission);

#ifdef ARENA_IMPLEMENTATION
    bool const stored = _http_service_permission_list_add(&self->permissions, permission, self->allocator);
#else
    bool const stored = _http_service_permission_list_add(&self->permissions, permission);
#endif // ARENA_IMPLEMENTATION

    self->failed = self->failed || !stored;

    trace_log_pop();

    return stored;
}

bool http_service_permission_check_permission_has(HTTP_Service_Permission_Check const *const self, char const *const permission) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "permission", (void*) permission);

    bool const success = _http_service_permission_list_has(&self->permissions, permission);

    trace_log_pop();

    return success;
}

bool http_service_permission_check_role_add(HTTP_Service_Permission_Check *const self, char const *const role) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "role", (void*) role);

#ifdef ARENA_IMPLEMENTATION
    bool const stored = _http_service_permission_list_add(&self->roles, role, self->allocator);
#else
    bool const stored = _http_service_permission_list_add(&self->roles, role);
#endif // ARENA_IMPLEMENTATION

    self->failed = self->failed || !stored;

    trace_log_pop();

    return stored;
}

bool http_service_permission_check_role_has(HTTP_Service_Permission_Check const *const self, char const *const role) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "role", (void*) role);

    bool const success = _http_service_permission_list_has(&self->roles, role);

    trace_log_pop();

    return success;
}

bool http_service_permission_check_scope_add(HTTP_Service_Permission_Check *const self, char const *const scope) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "scope", (void*) scope);

#ifdef ARENA_IMPLEMENTATION
    bool const stored = _http_service_permission_list_add(&self->scopes, scope, self->allocator);
#else
    bool const stored = _http_service_permission_list_add(&self->scopes, scope);
#endif // ARENA_IMPLEMENTATION

    self->failed = self->failed || !stored;

    trace_log_pop();

    return stored;
}

bool http_service_permission_check_scope_has(HTTP_Service_Permission_Check const *const self, char const *const scope) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "scope", (void*) scope);

    bool const success = _http_service_permission_list_has(&self->scopes, scope);

    trace_log_pop();

    return success;
}

void http_service_permission_check_uninit(HTTP_Service_Permission_Check *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    al_str_uninit(&self->denies);
    al_str_uninit(&self->permissions);
    al_str_uninit(&self->roles);
    al_str_uninit(&self->scopes);

    self->failed = false;
#ifdef ARENA_IMPLEMENTATION
    self->allocator = nullptr;
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();
}

bool http_service_permission_deny_add(HTTP_Service_Permission *const self, char const *const deny) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "deny", (void*) deny);

#ifdef ARENA_IMPLEMENTATION
    bool const stored = _http_service_permission_list_add(&self->denies, deny, self->allocator);
#else
    bool const stored = _http_service_permission_list_add(&self->denies, deny);
#endif // ARENA_IMPLEMENTATION

    self->failed = self->failed || !stored;

    trace_log_pop();

    return stored;
}

bool http_service_permission_deny_has(HTTP_Service_Permission const *const self, char const *const deny) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "deny", (void*) deny);

    bool const success = _http_service_permission_list_has(&self->denies, deny);

    trace_log_pop();

    return success;
}

bool http_service_permission_empty(HTTP_Service_Permission const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    bool const empty = al_str_get_size(&self->denies) == 0 && al_str_get_size(&self->permissions) == 0 && al_str_get_size(&self->roles) == 0 && al_str_get_size(&self->scopes) == 0;

    trace_log_pop();

    return empty;
}

HTTP_Service_Permission_Verdict http_service_permission_evaluate(HTTP_Service_Permission const *const self, HTTP_Service_Permission_Check const *const check) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "check", (void*) check);

    /* A refused add is otherwise INVISIBLE at evaluation time: the value simply is not in the
     * list, and a subject missing its deny marker - or a check missing its only requirement -
     * reads as clean. Fail closed on either side. */
    if (self->failed || check->failed) {
        trace_log_pop();

        return HTTP_SERVICE_PERMISSION_VERDICT_INCOMPLETE;
    }

    /* An any-of over an empty list is trivially true and an all-of over one is vacuously true,
     * so an empty requirement set used to permit every subject on earth. It is a caller
     * mistake, not a permission. */
    if (http_service_permission_check_empty(check)) {
        trace_log_pop();

        return HTTP_SERVICE_PERMISSION_VERDICT_EMPTY_CHECK;
    }

    if (_http_service_permission_list_intersects(&self->denies, &check->denies)) {
        trace_log_pop();

        return HTTP_SERVICE_PERMISSION_VERDICT_DENIED_MARKER;
    }

    if (!_http_service_permission_list_any(&self->roles, &check->roles)) {
        trace_log_pop();

        return HTTP_SERVICE_PERMISSION_VERDICT_MISSING_ROLE;
    }

    if (!_http_service_permission_list_all(&self->permissions, &check->permissions)) {
        trace_log_pop();

        return HTTP_SERVICE_PERMISSION_VERDICT_MISSING_PERMISSION;
    }

    if (!_http_service_permission_scope_all(&self->scopes, &check->scopes)) {
        trace_log_pop();

        return HTTP_SERVICE_PERMISSION_VERDICT_MISSING_SCOPE;
    }

    trace_log_pop();

    return HTTP_SERVICE_PERMISSION_VERDICT_ALLOWED;
}

bool http_service_permission_failed(HTTP_Service_Permission const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    bool const failed = self->failed;

    trace_log_pop();

    return failed;
}

HTTP_Service_Permission http_service_permission_init_1(void) {
    trace_log_push(LOG_METADATA);

    HTTP_Service_Permission const permission = {
#ifdef ARENA_IMPLEMENTATION
        .allocator      = nullptr,
#endif // ARENA_IMPLEMENTATION
        .denies         = al_str_init_1(),
        .failed         = false,
        .permissions    = al_str_init_1(),
        .roles          = al_str_init_1(),
        .scopes         = al_str_init_1()
    };

    trace_log_pop();

    return permission;
}

bool http_service_permission_permission_add(HTTP_Service_Permission *const self, char const *const permission) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "permission", (void*) permission);

#ifdef ARENA_IMPLEMENTATION
    bool const stored = _http_service_permission_list_add(&self->permissions, permission, self->allocator);
#else
    bool const stored = _http_service_permission_list_add(&self->permissions, permission);
#endif // ARENA_IMPLEMENTATION

    self->failed = self->failed || !stored;

    trace_log_pop();

    return stored;
}

bool http_service_permission_permission_has(HTTP_Service_Permission const *const self, char const *const permission) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "permission", (void*) permission);

    bool const success = _http_service_permission_list_has(&self->permissions, permission);

    trace_log_pop();

    return success;
}

bool http_service_permission_role_add(HTTP_Service_Permission *const self, char const *const role) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "role", (void*) role);

#ifdef ARENA_IMPLEMENTATION
    bool const stored = _http_service_permission_list_add(&self->roles, role, self->allocator);
#else
    bool const stored = _http_service_permission_list_add(&self->roles, role);
#endif // ARENA_IMPLEMENTATION

    self->failed = self->failed || !stored;

    trace_log_pop();

    return stored;
}

bool http_service_permission_role_has(HTTP_Service_Permission const *const self, char const *const role) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "role", (void*) role);

    bool const success = _http_service_permission_list_has(&self->roles, role);

    trace_log_pop();

    return success;
}

bool http_service_permission_role_hierarchy_has(
    HTTP_Service_Permission const *const self, char const *const *const ordered, USize const ordered_size, char const *const required) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "ordered", (void*) ordered);
    error_check_null(LOG_METADATA, "required", (void*) required);
    error_check_non_value_uint(LOG_METADATA, "ordered_size", ordered_size);

    /* A role-only PROBE, which is why it is named _has and not _allowed: it reads self->roles
     * and nothing else. A subject carrying a deny marker, or one whose add was refused
     * (self->failed), still answers true here - only http_service_permission_evaluate fails
     * closed on those. Pair the two; never gate a route on this alone. */
    USize   const   required_size   = char_length(required);
    USize           rank            = ordered_size;

    for (USize i = 0; i < ordered_size; i += 1) {
        if (ordered[i] != nullptr && char_compare_equal_comptime_2(ordered[i], char_length(ordered[i]), required, required_size)) {
            rank = i;

            break;
        }
    }

    /* A requirement that names a role the column does not carry is a caller mistake. Answering
     * true would admit everyone; answering false only locks the route. */
    if (rank == ordered_size) {
        trace_log_pop();

        return false;
    }

    bool success = false;

    for (USize i = rank; i < ordered_size; i += 1) {
        if (ordered[i] != nullptr && _http_service_permission_list_has(&self->roles, ordered[i])) {
            success = true;

            break;
        }
    }

    trace_log_pop();

    return success;
}

bool http_service_permission_scope_add(HTTP_Service_Permission *const self, char const *const scope) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "scope", (void*) scope);

#ifdef ARENA_IMPLEMENTATION
    bool const stored = _http_service_permission_list_add(&self->scopes, scope, self->allocator);
#else
    bool const stored = _http_service_permission_list_add(&self->scopes, scope);
#endif // ARENA_IMPLEMENTATION

    self->failed = self->failed || !stored;

    trace_log_pop();

    return stored;
}

bool http_service_permission_scope_has(HTTP_Service_Permission const *const self, char const *const scope) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "scope", (void*) scope);

    bool const success = _http_service_permission_list_has(&self->scopes, scope);

    trace_log_pop();

    return success;
}

void http_service_permission_uninit(HTTP_Service_Permission *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    al_str_uninit(&self->denies);
    al_str_uninit(&self->permissions);
    al_str_uninit(&self->roles);
    al_str_uninit(&self->scopes);

    self->failed = false;
#ifdef ARENA_IMPLEMENTATION
    self->allocator = nullptr;
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();
}