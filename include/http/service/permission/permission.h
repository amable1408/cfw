/*
 * permission.h - HTTP permission service for the C Libraries Framework
 *
 * Provides generic authorization checks over caller-defined roles,
 * permissions, scopes, and deny markers. It does not know about databases,
 * users, games, routes, or application-specific concepts.
 *
 * Features:
 *   - Subject role, permission, scope, and deny marker storage.
 *   - Requirement checks with any-role and all-permission/all-scope matching.
 *   - Explicit deny marker matching before allow checks.
 *   - A verdict enum that says WHY a subject failed, so a route can answer 401
 *     or 403 without re-deriving the reason.
 *   - Ordered-role hierarchy matching (a higher role satisfies a lower one) as a
 *     ROLE-ONLY probe that ignores deny markers and refused adds.
 *   - Suffix wildcards on scopes ("read:*" satisfies "read:users"); a bare "*"
 *     is a TOTAL grant that satisfies every required scope.
 *   - Arena and heap allocation support.
 *
 * Usage Examples:
 *   @code
 *   // Build the subject from whatever the request already proved, then evaluate.
 *   HTTP_Service_Permission subject = http_service_permission_init_1();
 *   HTTP_Service_Permission_Check check = http_service_permission_check_init_1();
 *
 *   http_service_permission_role_add(&subject, "admin");
 *   http_service_permission_scope_add(&subject, "read:*");
 *   http_service_permission_deny_add(&subject, "suspended");
 *
 *   http_service_permission_check_role_add(&check, "operator");
 *   http_service_permission_check_scope_add(&check, "read:users");
 *   http_service_permission_check_deny_add(&check, "suspended");
 *
 *   HTTP_Service_Permission_Verdict const verdict = http_service_permission_evaluate(&subject, &check);
 *
 *   // The gate, in this order: a refused add is OUR fault, not the client's, so
 *   // it is answered before the empty-subject test - an INCOMPLETE subject is
 *   // also an empty one, and a 401 would invite the client to retry credentials
 *   // that were never the problem. Then no subject at all is an authentication
 *   // failure, and anything else is an authorization failure.
 *   if (verdict == HTTP_SERVICE_PERMISSION_VERDICT_INCOMPLETE) {
 *       // respond 500 Internal Server Error
 *   }
 *   else if (http_service_permission_empty(&subject)) {
 *       // respond 401 Unauthorized
 *   }
 *   else if (verdict != HTTP_SERVICE_PERMISSION_VERDICT_ALLOWED) {
 *       // respond 403 Forbidden
 *   }
 *
 *   // A single ordered role column, the shape every CFW app actually stores.
 *   // ROLE-ONLY: it reads no deny marker and no refused add, so it accompanies
 *   // the verdict above rather than replacing it.
 *   char const *const ranks[] = { "mechanic", "operator", "admin" };
 *
 *   bool const senior = http_service_permission_role_hierarchy_has(&subject, ranks, 3, "operator");
 *
 *   http_service_permission_check_uninit(&check);
 *   http_service_permission_uninit(&subject);
 *   @endcode
 *
 * Error Handling:
 *   - Public functions validate non-null pointers.
 *   - Values must be valid null-terminated strings.
 *   - Every *_add answers false when the allocator refused, AND records the
 *     refusal on the instance. A half-built subject or check can therefore
 *     never evaluate to ALLOWED, even when its caller ignored the bool.
 *   - An EMPTY value is refused the same way: a role, permission, scope or deny
 *     marker named "" cannot match anything a request carries, so storing it
 *     would only make the set look built when it is not.
 *
 * Thread Safety:
 *   - Not thread-safe. Caller must synchronize shared instances. An
 *     arena-backed instance additionally shares one Arena, which is not
 *     thread-safe even for reads that allocate.
 *
 * Memory Management:
 *   - Values are copied into service-owned storage.
 *   - Call uninit functions when finished.
 *   - Every value arrives as a null-terminated `char const *`: this module
 *     commits to a single tier, and no `_2` sized forms exist. Sized request
 *     data (a JSON node, a Str) is terminated by the caller before it reaches
 *     this API. Adding a tier later is a MAJOR change.
 *
 * Performance Characteristics:
 *   - Lookups are linear over small in-memory lists; evaluation is
 *     O(|check| * |subject|) byte comparisons.
 *   - Building the subject is the real per-request cost, not evaluating it:
 *     every *_add copies its value. Build a per-request subject through
 *     http_service_permission_alloc_init_1() over the request arena, so the
 *     whole set is released by resetting the arena instead of by N frees.
 *
 * Dependencies:
 *   - arrayList, str.
 *
 * See permission.c for implementation details.
 */

#ifndef HTTP_SERVICE_PERMISSION_H
#define HTTP_SERVICE_PERMISSION_H

#include <container/arrayList/al_str.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

#define HTTP_SERVICE_PERMISSION_WILDCARD '*'

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief Why a subject did or did not satisfy a requirement set.
 */
typedef enum {
    /** @brief The subject satisfies every requirement. */
    HTTP_SERVICE_PERMISSION_VERDICT_ALLOWED = 0,
    /** @brief The subject carries a deny marker the requirement set rejects. */
    HTTP_SERVICE_PERMISSION_VERDICT_DENIED_MARKER,
    /** @brief The requirement set carries no requirement at all. */
    HTTP_SERVICE_PERMISSION_VERDICT_EMPTY_CHECK,
    /** @brief The subject or the requirement set lost a value to a refused add. */
    HTTP_SERVICE_PERMISSION_VERDICT_INCOMPLETE,
    /** @brief A required permission is missing from the subject. */
    HTTP_SERVICE_PERMISSION_VERDICT_MISSING_PERMISSION,
    /** @brief The subject holds none of the accepted roles. */
    HTTP_SERVICE_PERMISSION_VERDICT_MISSING_ROLE,
    /** @brief A required scope is missing from the subject. */
    HTTP_SERVICE_PERMISSION_VERDICT_MISSING_SCOPE
} HTTP_Service_Permission_Verdict;

/**
 * @brief Subject permission context.
 */
typedef struct {
#ifdef ARENA_IMPLEMENTATION
    /** @brief Optional arena used by owned values. */
    Arena *allocator;
#endif // ARENA_IMPLEMENTATION
    /** @brief Explicit deny markers owned by the subject. */
    AL_Str denies;
    /** @brief Set once any add was refused; the subject can no longer be trusted. */
    bool failed;
    /** @brief Permission names owned by the subject. */
    AL_Str permissions;
    /** @brief Role names owned by the subject. */
    AL_Str roles;
    /** @brief Scope names owned by the subject. */
    AL_Str scopes;
} HTTP_Service_Permission;

/**
 * @brief Permission requirement set.
 */
typedef struct {
#ifdef ARENA_IMPLEMENTATION
    /** @brief Optional arena used by owned values. */
    Arena *allocator;
#endif // ARENA_IMPLEMENTATION
    /** @brief Deny markers that reject matching subjects. */
    AL_Str denies;
    /** @brief Set once any add was refused; the set can no longer be trusted. */
    bool failed;
    /** @brief Required permission names. All must match. */
    AL_Str permissions;
    /** @brief Accepted role names. Any match is enough. */
    AL_Str roles;
    /** @brief Required scope names. All must match. */
    AL_Str scopes;
} HTTP_Service_Permission_Check;

/*==============================================================================
 * MARK: - API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Initialize an arena-backed permission subject context.
 * @param allocator Arena allocator.
 * @return Initialized subject.
 */
HTTP_Service_Permission http_service_permission_alloc_init_1(Arena *const allocator);

/**
 * @brief Initialize an arena-backed permission requirement set.
 * @param allocator Arena allocator.
 * @return Initialized check.
 */
HTTP_Service_Permission_Check http_service_permission_check_alloc_init_1(Arena *const allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Check whether a subject satisfies a requirement set.
 * @param self Subject permission context.
 * @param check Requirement set.
 * @return true when allowed. The bool wrapper over
 *         http_service_permission_evaluate(); use that when the caller must
 *         report which requirement failed.
 */
bool http_service_permission_allowed(HTTP_Service_Permission const *const self, HTTP_Service_Permission_Check const *const check);

/**
 * @brief Add a deny marker to a requirement set.
 * @param self Requirement set.
 * @param deny Deny marker.
 * @return true when stored. FALSE MEANS THE MARKER IS NOT IN THE SET - the set
 *         is marked failed so evaluation can no longer answer ALLOWED, but a
 *         caller that wants to report the refusal must read this bool.
 */
bool http_service_permission_check_deny_add(HTTP_Service_Permission_Check *const self, char const *const deny);

/**
 * @brief Check whether a requirement set has a deny marker.
 * @param self Requirement set.
 * @param deny Deny marker.
 * @return true when present.
 */
bool http_service_permission_check_deny_has(HTTP_Service_Permission_Check const *const self, char const *const deny);

/**
 * @brief Check whether a requirement set carries no requirement at all.
 * @param self Requirement set.
 * @return true when every list is empty.
 * @note An empty set is REFUSED, not vacuously satisfied. An any-of over an
 *       empty list is trivially true and an all-of over one is vacuously true,
 *       so a set whose adds were all refused would otherwise permit every
 *       subject on earth. Evaluation answers
 *       HTTP_SERVICE_PERMISSION_VERDICT_EMPTY_CHECK instead.
 */
bool http_service_permission_check_empty(HTTP_Service_Permission_Check const *const self);

/**
 * @brief Report whether a requirement set lost a value to a refused add.
 * @param self Requirement set.
 * @return true when any add on this set answered false.
 */
bool http_service_permission_check_failed(HTTP_Service_Permission_Check const *const self);

/**
 * @brief Initialize a permission requirement set.
 * @return Initialized check.
 */
HTTP_Service_Permission_Check http_service_permission_check_init_1(void);

/**
 * @brief Add a required permission to a requirement set.
 * @param self Requirement set.
 * @param permission Permission name.
 * @return true when stored; false when the allocator refused, leaving the set
 *         unchanged and marked failed.
 */
bool http_service_permission_check_permission_add(HTTP_Service_Permission_Check *const self, char const *const permission);

/**
 * @brief Check whether a requirement set has a required permission.
 * @param self Requirement set.
 * @param permission Permission name.
 * @return true when present.
 */
bool http_service_permission_check_permission_has(HTTP_Service_Permission_Check const *const self, char const *const permission);

/**
 * @brief Add an accepted role to a requirement set.
 * @param self Requirement set.
 * @param role Role name.
 * @return true when stored; false when the allocator refused, leaving the set
 *         unchanged and marked failed.
 */
bool http_service_permission_check_role_add(HTTP_Service_Permission_Check *const self, char const *const role);

/**
 * @brief Check whether a requirement set has an accepted role.
 * @param self Requirement set.
 * @param role Role name.
 * @return true when present.
 */
bool http_service_permission_check_role_has(HTTP_Service_Permission_Check const *const self, char const *const role);

/**
 * @brief Add a required scope to a requirement set.
 * @param self Requirement set.
 * @param scope Scope name.
 * @return true when stored; false when the allocator refused, leaving the set
 *         unchanged and marked failed.
 */
bool http_service_permission_check_scope_add(HTTP_Service_Permission_Check *const self, char const *const scope);

/**
 * @brief Check whether a requirement set has a required scope.
 * @param self Requirement set.
 * @param scope Scope name.
 * @return true when present. Exact match only - the wildcard is interpreted at
 *         evaluation time, never by a has probe.
 */
bool http_service_permission_check_scope_has(HTTP_Service_Permission_Check const *const self, char const *const scope);

/**
 * @brief Release requirement set storage.
 * @param self Requirement set.
 */
void http_service_permission_check_uninit(HTTP_Service_Permission_Check *const self);

/**
 * @brief Add a deny marker to a subject.
 * @param self Subject permission context.
 * @param deny Deny marker.
 * @return true when stored. FALSE MEANS THE MARKER IS NOT ON THE SUBJECT - the
 *         subject is marked failed so evaluation can no longer answer ALLOWED,
 *         because a dropped deny marker is exactly what makes a suspended
 *         subject look clean.
 */
bool http_service_permission_deny_add(HTTP_Service_Permission *const self, char const *const deny);

/**
 * @brief Check whether a subject has a deny marker.
 * @param self Subject permission context.
 * @param deny Deny marker.
 * @return true when present.
 */
bool http_service_permission_deny_has(HTTP_Service_Permission const *const self, char const *const deny);

/**
 * @brief Check whether a subject carries nothing at all.
 * @param self Subject permission context.
 * @return true when every list is empty.
 * @note This is the 401-versus-403 discriminator. An empty subject is a request
 *       that proved no identity, which is an AUTHENTICATION failure (401
 *       Unauthorized). A non-empty subject that fails evaluation proved an
 *       identity that is not enough, which is an AUTHORIZATION failure (403
 *       Forbidden). Sending the wrong one either invites a client to retry with
 *       the same credentials forever or leaks that the resource exists.
 * @note Test HTTP_SERVICE_PERMISSION_VERDICT_INCOMPLETE FIRST. A subject whose
 *       every add was refused is both empty and failed, and it is a server-side
 *       fault: answering 401 tells the client to retry credentials that were
 *       never the problem, forever. Incomplete first, then empty, then the
 *       verdict.
 */
bool http_service_permission_empty(HTTP_Service_Permission const *const self);

/**
 * @brief Evaluate a subject against a requirement set and report why.
 * @param self Subject permission context.
 * @param check Requirement set.
 * @return HTTP_SERVICE_PERMISSION_VERDICT_ALLOWED, or the first requirement the
 *         subject fails. Order: a refused add on either side (INCOMPLETE), an
 *         empty requirement set (EMPTY_CHECK), a deny marker (DENIED_MARKER),
 *         roles (MISSING_ROLE), permissions (MISSING_PERMISSION), scopes
 *         (MISSING_SCOPE).
 * @note The verdict says which requirement failed, never which value. Do not
 *       put it in a client-visible message: "MISSING_SCOPE" tells an attacker
 *       the role and permission checks passed.
 */
HTTP_Service_Permission_Verdict http_service_permission_evaluate(HTTP_Service_Permission const *const self, HTTP_Service_Permission_Check const *const check);

/**
 * @brief Report whether a subject lost a value to a refused add.
 * @param self Subject permission context.
 * @return true when any add on this subject answered false.
 */
bool http_service_permission_failed(HTTP_Service_Permission const *const self);

/**
 * @brief Initialize a permission subject context.
 * @return Initialized subject.
 */
HTTP_Service_Permission http_service_permission_init_1(void);

/**
 * @brief Add a permission to a subject.
 * @param self Subject permission context.
 * @param permission Permission name.
 * @return true when stored; false when the allocator refused, leaving the
 *         subject unchanged and marked failed.
 */
bool http_service_permission_permission_add(HTTP_Service_Permission *const self, char const *const permission);

/**
 * @brief Check whether a subject has a permission.
 * @param self Subject permission context.
 * @param permission Permission name.
 * @return true when present.
 */
bool http_service_permission_permission_has(HTTP_Service_Permission const *const self, char const *const permission);

/**
 * @brief Add a role to a subject.
 * @param self Subject permission context.
 * @param role Role name.
 * @return true when stored; false when the allocator refused, leaving the
 *         subject unchanged and marked failed.
 */
bool http_service_permission_role_add(HTTP_Service_Permission *const self, char const *const role);

/**
 * @brief Check whether a subject has a role.
 * @param self Subject permission context.
 * @param role Role name.
 * @return true when present.
 */
bool http_service_permission_role_has(HTTP_Service_Permission const *const self, char const *const role);

/**
 * @brief Probe a subject's ROLES against an ORDERED role column.
 * @param self Subject permission context.
 * @param ordered Role names from least to most privileged. Must not be null.
 * @param ordered_size Number of entries in ordered.
 * @param required Minimum role the subject must reach.
 * @return true when the subject holds any role whose rank is at or above
 *         required's; false when it holds none of them, and false when required
 *         is not itself in ordered (an unknown requirement fails closed rather
 *         than admitting everyone).
 * @note This is the model the apps actually store: one role column with an
 *       implicit order (admin > operator > mechanic). The any-of role list on
 *       HTTP_Service_Permission_Check is flat and cannot express it without
 *       naming every senior role at every call site.
 * @note NOT A GATE. This reads ROLES ONLY — it is named _has, not _allowed, for
 *       that reason. It ignores the subject's deny markers, so a suspended
 *       administrator answers true, and it ignores
 *       http_service_permission_failed(), so a subject whose deny add was
 *       refused answers true as well. Both are cases every verdict-shaped entry
 *       point here fails closed on. Pair it with
 *       http_service_permission_evaluate() (or at least
 *       http_service_permission_deny_has() plus
 *       http_service_permission_failed()) and let those decide the response.
 */
bool http_service_permission_role_hierarchy_has(
    HTTP_Service_Permission const *const self, char const *const *const ordered, USize const ordered_size, char const *const required);

/**
 * @brief Add a scope to a subject.
 * @param self Subject permission context.
 * @param scope Scope name.
 * @return true when stored; false when the allocator refused, leaving the
 *         subject unchanged and marked failed.
 * @note A subject scope may end in HTTP_SERVICE_PERMISSION_WILDCARD ('*'), in
 *       which case it satisfies every required scope sharing the prefix before
 *       it: "read:*" satisfies "read:users". A bare "*" satisfies every required
 *       scope. The wildcard is honoured only on the SUBJECT side and only for
 *       scopes - roles, permissions and deny markers match exactly, so a
 *       wildcard can never widen a deny.
 */
bool http_service_permission_scope_add(HTTP_Service_Permission *const self, char const *const scope);

/**
 * @brief Check whether a subject has a scope.
 * @param self Subject permission context.
 * @param scope Scope name.
 * @return true when present. Exact match only - a stored "read:*" is NOT found
 *         by a probe for "read:users"; use http_service_permission_evaluate()
 *         for wildcard-aware matching.
 */
bool http_service_permission_scope_has(HTTP_Service_Permission const *const self, char const *const scope);

/**
 * @brief Release subject storage.
 * @param self Subject permission context.
 */
void http_service_permission_uninit(HTTP_Service_Permission *const self);

#endif // HTTP_SERVICE_PERMISSION_H