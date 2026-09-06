#include <http/service/permission/permission.h>
#include <test/test.h>

/* permission is the one http/service module no main_*.c consumes, so the live route lane
 * cannot reach it. That makes this suite its only verification path - including for the
 * ownership fix that replaced str_init_3(char_new_3(...)) with str_init_static, whose failure
 * mode is a leak rather than a wrong answer and so needs a sanitizer run to see. */

static void _test_permission_lists(Test *const test) {
    test_case_begin(test, "permission add / has");

    HTTP_Service_Permission permission = http_service_permission_init_1();

    test_expect_false(test, "unknown role absent", http_service_permission_role_has(&permission, "admin"));

    http_service_permission_role_add(&permission, "admin");
    http_service_permission_permission_add(&permission, "wallet.read");
    http_service_permission_scope_add(&permission, "openid");
    http_service_permission_deny_add(&permission, "wallet.write");

    test_expect_true(test, "role stored", http_service_permission_role_has(&permission, "admin"));
    test_expect_true(test, "permission stored", http_service_permission_permission_has(&permission, "wallet.read"));
    test_expect_true(test, "scope stored", http_service_permission_scope_has(&permission, "openid"));
    test_expect_true(test, "deny stored", http_service_permission_deny_has(&permission, "wallet.write"));

    // The stored key is a COPY, so a value that merely shares a prefix must not match.
    test_expect_false(test, "prefix does not match", http_service_permission_role_has(&permission, "admi"));
    test_expect_false(test, "superstring does not match", http_service_permission_role_has(&permission, "administrator"));
    test_expect_false(test, "unrelated value absent", http_service_permission_permission_has(&permission, "wallet.write"));

    http_service_permission_uninit(&permission);

    test_case_end(test);
}

static void _test_permission_duplicates(Test *const test) {
    test_case_begin(test, "permission dedupe");

    HTTP_Service_Permission permission = http_service_permission_init_1();

    for (USize i = 0; i < 32; i += 1) {
        http_service_permission_role_add(&permission, "admin");
    }

    test_expect_true(test, "repeated add still resolves", http_service_permission_role_has(&permission, "admin"));
    test_expect_false(test, "a duplicate add is success, not a refusal", http_service_permission_failed(&permission));

    http_service_permission_uninit(&permission);

    test_case_end(test);
}

static void _test_permission_allowed(Test *const test) {
    test_case_begin(test, "permission allowed");

    HTTP_Service_Permission permission = http_service_permission_init_1();

    http_service_permission_role_add(&permission, "admin");
    http_service_permission_permission_add(&permission, "wallet.read");

    HTTP_Service_Permission_Check check = http_service_permission_check_init_1();

    http_service_permission_check_role_add(&check, "admin");
    http_service_permission_check_permission_add(&check, "wallet.read");

    test_expect_true(test, "matching check allowed", http_service_permission_allowed(&permission, &check));

    http_service_permission_check_uninit(&check);

    HTTP_Service_Permission_Check missing = http_service_permission_check_init_1();

    http_service_permission_check_role_add(&missing, "root");

    test_expect_false(test, "unmatched role refused", http_service_permission_allowed(&permission, &missing));

    http_service_permission_check_uninit(&missing);

    http_service_permission_uninit(&permission);

    test_case_end(test);
}

static void _test_permission_owned_copies(Test *const test) {
    test_case_begin(test, "permission stores owned copies");

    HTTP_Service_Permission permission = http_service_permission_init_1();

    /* The value is added from a mutable buffer that is then OVERWRITTEN. If the service kept a
     * view over the caller's memory instead of copying - which str_init_3 would have done -
     * the lookup below reads clobbered bytes and fails. This is the regression guard for the
     * ownership fix. */
    char role[16] = DEFAULT_INITIALIZATION;

    char_copy_2(role, "auditor", CHAR_STATIC_SIZE("auditor"));
    role[CHAR_STATIC_SIZE("auditor")] = '\0';

    http_service_permission_role_add(&permission, role);

    char_copy_2(role, "XXXXXXX", CHAR_STATIC_SIZE("XXXXXXX"));
    role[CHAR_STATIC_SIZE("XXXXXXX")] = '\0';

    test_expect_true(test, "stored value survives caller buffer reuse", http_service_permission_role_has(&permission, "auditor"));
    test_expect_false(test, "clobbered value not present", http_service_permission_role_has(&permission, "XXXXXXX"));

    http_service_permission_uninit(&permission);

    test_case_end(test);
}

static void _test_permission_verdicts(Test *const test) {
    test_case_begin(test, "evaluate reports which requirement failed");

    HTTP_Service_Permission subject = http_service_permission_init_1();

    http_service_permission_role_add(&subject, "operator");
    http_service_permission_permission_add(&subject, "wallet.read");
    http_service_permission_scope_add(&subject, "openid");
    http_service_permission_deny_add(&subject, "suspended");

    HTTP_Service_Permission_Check allowed = http_service_permission_check_init_1();

    http_service_permission_check_role_add(&allowed, "operator");
    http_service_permission_check_permission_add(&allowed, "wallet.read");
    http_service_permission_check_scope_add(&allowed, "openid");

    test_expect_u(test, "a satisfied set is ALLOWED", (USize) HTTP_SERVICE_PERMISSION_VERDICT_ALLOWED, (USize) http_service_permission_evaluate(&subject, &allowed));

    HTTP_Service_Permission_Check denied = http_service_permission_check_init_1();

    http_service_permission_check_role_add(&denied, "operator");
    http_service_permission_check_deny_add(&denied, "suspended");

    /* The deny marker is matched BEFORE the allow checks, so a subject that satisfies the role
     * requirement is still refused - and the verdict says the marker, not the role. */
    test_expect_u(test, "a matched deny marker wins", (USize) HTTP_SERVICE_PERMISSION_VERDICT_DENIED_MARKER, (USize) http_service_permission_evaluate(&subject, &denied));

    HTTP_Service_Permission_Check role = http_service_permission_check_init_1();

    http_service_permission_check_role_add(&role, "admin");

    test_expect_u(test, "no accepted role held", (USize) HTTP_SERVICE_PERMISSION_VERDICT_MISSING_ROLE, (USize) http_service_permission_evaluate(&subject, &role));

    HTTP_Service_Permission_Check permission = http_service_permission_check_init_1();

    http_service_permission_check_role_add(&permission, "operator");
    http_service_permission_check_permission_add(&permission, "wallet.write");

    test_expect_u(test, "a required permission is missing", (USize) HTTP_SERVICE_PERMISSION_VERDICT_MISSING_PERMISSION, (USize) http_service_permission_evaluate(&subject, &permission));

    HTTP_Service_Permission_Check scope = http_service_permission_check_init_1();

    http_service_permission_check_role_add(&scope, "operator");
    http_service_permission_check_scope_add(&scope, "email");

    test_expect_u(test, "a required scope is missing", (USize) HTTP_SERVICE_PERMISSION_VERDICT_MISSING_SCOPE, (USize) http_service_permission_evaluate(&subject, &scope));

    http_service_permission_check_uninit(&scope);
    http_service_permission_check_uninit(&permission);
    http_service_permission_check_uninit(&role);
    http_service_permission_check_uninit(&denied);
    http_service_permission_check_uninit(&allowed);

    http_service_permission_uninit(&subject);

    test_case_end(test);
}

static void _test_permission_empty_check_fails_closed(Test *const test) {
    test_case_begin(test, "an empty requirement set permits nobody");

    HTTP_Service_Permission subject = http_service_permission_init_1();

    http_service_permission_role_add(&subject, "guest");

    HTTP_Service_Permission_Check check = http_service_permission_check_init_1();

    test_expect_true(test, "a fresh check is empty", http_service_permission_check_empty(&check));

    /* The regression guard for the memsec High: an any-of over an empty list is trivially true
     * and an all-of over one is vacuously true, so this used to answer ALLOWED for every
     * subject on earth - including a check whose every add had been refused. */
    test_expect_u(test, "an empty check is EMPTY_CHECK, not ALLOWED", (USize) HTTP_SERVICE_PERMISSION_VERDICT_EMPTY_CHECK, (USize) http_service_permission_evaluate(&subject, &check));
    test_expect_false(test, "allowed agrees", http_service_permission_allowed(&subject, &check));

    http_service_permission_check_role_add(&check, "guest");

    test_expect_false(test, "one requirement makes it non-empty", http_service_permission_check_empty(&check));
    test_expect_true(test, "and it now answers on the merits", http_service_permission_allowed(&subject, &check));

    http_service_permission_check_uninit(&check);
    http_service_permission_uninit(&subject);

    test_case_end(test);
}

static void _test_permission_refused_add_is_visible(Test *const test) {
    test_case_begin(test, "a refused add cannot evaluate to ALLOWED");

    HTTP_Service_Permission subject = http_service_permission_init_1();

    http_service_permission_role_add(&subject, "operator");

    test_expect_false(test, "a healthy subject is not failed", http_service_permission_failed(&subject));

    /* An empty value is the refusal this suite can reach without exhausting an arena: it can
     * never match anything a request carries, so it is refused rather than stored. What is
     * being pinned is not the refusal but its VISIBILITY - the caller here ignores the bool,
     * exactly as production code does. */
    test_expect_false(test, "an empty deny marker is refused", http_service_permission_deny_add(&subject, ""));
    test_expect_true(test, "the refusal is recorded on the subject", http_service_permission_failed(&subject));

    HTTP_Service_Permission_Check check = http_service_permission_check_init_1();

    http_service_permission_check_role_add(&check, "operator");

    /* Without the flag this reads ALLOWED: the dropped deny marker simply is not in the list,
     * so the subject looks clean. */
    test_expect_u(test, "a failed subject is INCOMPLETE", (USize) HTTP_SERVICE_PERMISSION_VERDICT_INCOMPLETE, (USize) http_service_permission_evaluate(&subject, &check));
    test_expect_false(test, "allowed agrees", http_service_permission_allowed(&subject, &check));

    HTTP_Service_Permission clean = http_service_permission_init_1();

    http_service_permission_role_add(&clean, "operator");

    test_expect_false(test, "a healthy check is not failed", http_service_permission_check_failed(&check));
    test_expect_false(test, "an empty required role is refused", http_service_permission_check_role_add(&check, ""));
    test_expect_true(test, "the refusal is recorded on the check", http_service_permission_check_failed(&check));
    test_expect_u(test, "a failed check is INCOMPLETE too", (USize) HTTP_SERVICE_PERMISSION_VERDICT_INCOMPLETE, (USize) http_service_permission_evaluate(&clean, &check));

    http_service_permission_uninit(&clean);
    http_service_permission_check_uninit(&check);
    http_service_permission_uninit(&subject);

    test_case_end(test);
}

static void _test_permission_subject_empty(Test *const test) {
    test_case_begin(test, "an empty subject is the 401 discriminator");

    HTTP_Service_Permission anonymous = http_service_permission_init_1();

    test_expect_true(test, "a fresh subject is empty", http_service_permission_empty(&anonymous));

    HTTP_Service_Permission_Check check = http_service_permission_check_init_1();

    http_service_permission_check_role_add(&check, "admin");

    /* The gate: empty subject -> 401 (nothing was proved), non-empty subject that fails ->
     * 403 (what was proved is not enough). Both are refusals; only one invites a retry. */
    test_expect_u(test, "an empty subject still fails the check", (USize) HTTP_SERVICE_PERMISSION_VERDICT_MISSING_ROLE, (USize) http_service_permission_evaluate(&anonymous, &check));

    HTTP_Service_Permission member = http_service_permission_init_1();

    http_service_permission_role_add(&member, "guest");

    test_expect_false(test, "a subject with a role is not empty", http_service_permission_empty(&member));
    test_expect_u(test, "and fails as an authorization problem", (USize) HTTP_SERVICE_PERMISSION_VERDICT_MISSING_ROLE, (USize) http_service_permission_evaluate(&member, &check));

    http_service_permission_uninit(&member);
    http_service_permission_check_uninit(&check);
    http_service_permission_uninit(&anonymous);

    test_case_end(test);
}

static void _test_permission_scope_wildcard(Test *const test) {
    test_case_begin(test, "subject scope wildcards");

    HTTP_Service_Permission subject = http_service_permission_init_1();

    http_service_permission_role_add(&subject, "service");
    http_service_permission_scope_add(&subject, "read:*");

    HTTP_Service_Permission_Check check = http_service_permission_check_init_1();

    http_service_permission_check_role_add(&check, "service");
    http_service_permission_check_scope_add(&check, "read:users");

    test_expect_true(test, "read:* satisfies read:users", http_service_permission_allowed(&subject, &check));

    // The has probe is exact: the wildcard is an evaluation-time rule, never a storage one.
    test_expect_false(test, "has does not expand the wildcard", http_service_permission_scope_has(&subject, "read:users"));
    test_expect_true(test, "has finds the literal", http_service_permission_scope_has(&subject, "read:*"));

    HTTP_Service_Permission_Check other = http_service_permission_check_init_1();

    http_service_permission_check_role_add(&other, "service");
    http_service_permission_check_scope_add(&other, "write:users");

    test_expect_false(test, "read:* does not satisfy write:users", http_service_permission_allowed(&subject, &other));

    HTTP_Service_Permission total = http_service_permission_init_1();

    http_service_permission_role_add(&total, "service");
    http_service_permission_scope_add(&total, "*");

    HTTP_Service_Permission_Check reads = http_service_permission_check_init_1();

    http_service_permission_check_role_add(&reads, "service");
    http_service_permission_check_scope_add(&reads, "read:x");

    // A bare "*" is a TOTAL grant - prefix_size is 0, so every required scope matches it.
    test_expect_true(test, "a bare * satisfies read:x", http_service_permission_allowed(&total, &reads));

    /* The CHECK side never expands a wildcard: a required "read:*" is a literal scope name.
     * Honouring it there would let a route accidentally demand less than it wrote down. */
    HTTP_Service_Permission literal_subject = http_service_permission_init_1();

    http_service_permission_role_add(&literal_subject, "service");
    http_service_permission_scope_add(&literal_subject, "read:users");

    HTTP_Service_Permission_Check literal_check = http_service_permission_check_init_1();

    http_service_permission_check_role_add(&literal_check, "service");
    http_service_permission_check_scope_add(&literal_check, "read:*");

    test_expect_u(
        test, "a required read:* is a literal, not a pattern", (USize) HTTP_SERVICE_PERMISSION_VERDICT_MISSING_SCOPE,
        (USize) http_service_permission_evaluate(&literal_subject, &literal_check));

    /* The two prefix-length edges of the subject-side match. "read:*" carries the prefix
     * "read:", five bytes: a required scope SHORTER than it cannot match, and one exactly
     * equal to it does. */
    HTTP_Service_Permission_Check shorter = http_service_permission_check_init_1();

    http_service_permission_check_role_add(&shorter, "service");
    http_service_permission_check_scope_add(&shorter, "read");

    test_expect_u(
        test, "a required scope shorter than the prefix misses", (USize) HTTP_SERVICE_PERMISSION_VERDICT_MISSING_SCOPE,
        (USize) http_service_permission_evaluate(&subject, &shorter));

    HTTP_Service_Permission_Check exact = http_service_permission_check_init_1();

    http_service_permission_check_role_add(&exact, "service");
    http_service_permission_check_scope_add(&exact, "read:");

    test_expect_u(
        test, "a required scope equal to the prefix matches", (USize) HTTP_SERVICE_PERMISSION_VERDICT_ALLOWED,
        (USize) http_service_permission_evaluate(&subject, &exact));

    http_service_permission_check_uninit(&exact);
    http_service_permission_check_uninit(&shorter);
    http_service_permission_check_uninit(&literal_check);
    http_service_permission_uninit(&literal_subject);

    http_service_permission_check_uninit(&reads);
    http_service_permission_uninit(&total);

    HTTP_Service_Permission denied = http_service_permission_init_1();

    http_service_permission_role_add(&denied, "service");
    http_service_permission_deny_add(&denied, "suspend*");

    HTTP_Service_Permission_Check block = http_service_permission_check_init_1();

    http_service_permission_check_role_add(&block, "service");
    http_service_permission_check_deny_add(&block, "suspended");

    /* Deny markers match EXACTLY. A wildcard honoured here would let a subject spell its way
     * out of a block it carries. */
    test_expect_true(test, "a wildcard never widens a deny marker", http_service_permission_allowed(&denied, &block));

    http_service_permission_check_uninit(&block);
    http_service_permission_uninit(&denied);
    http_service_permission_check_uninit(&other);
    http_service_permission_check_uninit(&check);
    http_service_permission_uninit(&subject);

    test_case_end(test);
}

static void _test_permission_role_hierarchy(Test *const test) {
    test_case_begin(test, "ordered role column");

    char const *const ranks[] = { "mechanic", "operator", "admin" };

    HTTP_Service_Permission admin = http_service_permission_init_1();

    http_service_permission_role_add(&admin, "admin");

    test_expect_true(test, "admin reaches mechanic", http_service_permission_role_hierarchy_has(&admin, ranks, 3, "mechanic"));
    test_expect_true(test, "admin reaches operator", http_service_permission_role_hierarchy_has(&admin, ranks, 3, "operator"));
    test_expect_true(test, "admin reaches admin", http_service_permission_role_hierarchy_has(&admin, ranks, 3, "admin"));

    HTTP_Service_Permission mechanic = http_service_permission_init_1();

    http_service_permission_role_add(&mechanic, "mechanic");

    test_expect_true(test, "mechanic reaches mechanic", http_service_permission_role_hierarchy_has(&mechanic, ranks, 3, "mechanic"));
    test_expect_false(test, "mechanic does not reach operator", http_service_permission_role_hierarchy_has(&mechanic, ranks, 3, "operator"));
    test_expect_false(test, "mechanic does not reach admin", http_service_permission_role_hierarchy_has(&mechanic, ranks, 3, "admin"));

    // A requirement the column does not carry fails closed rather than admitting everyone.
    test_expect_false(test, "an unknown requirement is refused", http_service_permission_role_hierarchy_has(&admin, ranks, 3, "root"));

    HTTP_Service_Permission stranger = http_service_permission_init_1();

    http_service_permission_role_add(&stranger, "auditor");

    test_expect_false(test, "a role outside the column reaches nothing", http_service_permission_role_hierarchy_has(&stranger, ranks, 3, "mechanic"));

    /* It is a ROLE-ONLY probe - which is what the _has name says. A suspended administrator
     * still reaches every rank here, and only evaluate() fails closed on the deny marker. Any
     * route that gated on this alone would let a blocked subject through. */
    HTTP_Service_Permission suspended = http_service_permission_init_1();

    http_service_permission_role_add(&suspended, "admin");
    http_service_permission_deny_add(&suspended, "suspended");

    HTTP_Service_Permission_Check block = http_service_permission_check_init_1();

    http_service_permission_check_role_add(&block, "admin");
    http_service_permission_check_deny_add(&block, "suspended");

    test_expect_true(test, "a suspended admin still reaches operator", http_service_permission_role_hierarchy_has(&suspended, ranks, 3, "operator"));
    test_expect_u(
        test, "while evaluate refuses it on the deny marker", (USize) HTTP_SERVICE_PERMISSION_VERDICT_DENIED_MARKER,
        (USize) http_service_permission_evaluate(&suspended, &block));

    /* Same story for a refused add: the probe reads self->roles and nothing else. */
    HTTP_Service_Permission half_built = http_service_permission_init_1();

    http_service_permission_role_add(&half_built, "admin");

    test_expect_false(test, "an empty deny marker is refused", http_service_permission_deny_add(&half_built, ""));
    test_expect_true(test, "the subject is marked failed", http_service_permission_failed(&half_built));
    test_expect_true(test, "the probe ignores the refused add", http_service_permission_role_hierarchy_has(&half_built, ranks, 3, "admin"));
    test_expect_u(
        test, "while evaluate answers INCOMPLETE", (USize) HTTP_SERVICE_PERMISSION_VERDICT_INCOMPLETE,
        (USize) http_service_permission_evaluate(&half_built, &block));

    http_service_permission_uninit(&half_built);
    http_service_permission_check_uninit(&block);
    http_service_permission_uninit(&suspended);
    http_service_permission_uninit(&stranger);
    http_service_permission_uninit(&mechanic);
    http_service_permission_uninit(&admin);

    test_case_end(test);
}

static void _test_permission_arena(Test *const test) {
    test_case_begin(test, "arena-backed subject and check");

    Arena arena = arena_init_1(65536, ARENA_TYPE_LINEAR);

    HTTP_Service_Permission subject = http_service_permission_alloc_init_1(&arena);
    HTTP_Service_Permission_Check check = http_service_permission_check_alloc_init_1(&arena);

    test_expect_true(test, "arena role stored", http_service_permission_role_add(&subject, "admin"));
    test_expect_true(test, "arena scope stored", http_service_permission_scope_add(&subject, "read:*"));
    test_expect_true(test, "arena required role stored", http_service_permission_check_role_add(&check, "admin"));
    test_expect_true(test, "arena required scope stored", http_service_permission_check_scope_add(&check, "read:users"));

    test_expect_u(test, "arena instances evaluate the same", (USize) HTTP_SERVICE_PERMISSION_VERDICT_ALLOWED, (USize) http_service_permission_evaluate(&subject, &check));

    http_service_permission_check_uninit(&check);
    http_service_permission_uninit(&subject);

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

I32 main(void) {
    LogConfig const log_config = { .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true };

    log_init(log_config);

    Test test = test_init("./test_all.c");

    test_verbose_set(&test, false);

    test_suite_begin(&test, "http_service_permission");
    _test_permission_lists(&test);
    _test_permission_duplicates(&test);
    _test_permission_allowed(&test);
    _test_permission_owned_copies(&test);
    _test_permission_verdicts(&test);
    _test_permission_empty_check_fails_closed(&test);
    _test_permission_refused_add_is_visible(&test);
    _test_permission_subject_empty(&test);
    _test_permission_scope_wildcard(&test);
    _test_permission_role_hierarchy(&test);
    _test_permission_arena(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}