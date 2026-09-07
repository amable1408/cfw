#include <http/service/rate_limit/rate_limit.h>
#include <log/log.h>
#include <test/test.h>

/*
 * Behavioral tests for include/http/service/rate_limit/rate_limit.c built WITHOUT
 * ERROR_CHECK_ENABLED.
 *
 * error_check_null in this module guards only null-pointer contracts on the public entry
 * points - deliberately absent here (undefined behavior with the checks compiled out, and this
 * file never exercises them). Every value-dependent refusal below - an unknown rule, an empty or
 * oversize key, a zero limit/cooldown/capacity at init, an invalid rule registration, and the
 * eviction floor that never discards a record at its limit - is coded as an ordinary runtime
 * branch, never routed through error_check, so this build proves each one refuses identically
 * whether ERROR_CHECK_ENABLED is defined or not, per the value-dependent-refusal standard (a
 * data-dependent decision is never an abort primitive).
 */

int main(void) {
    LogConfig const log_config = {
        .level             = LOG_LEVEL_ERROR,
        .stream            = stdout,
        .timestamp_enabled = true,
        .autoflush         = true
    };

    log_init(log_config);

    Test test = test_init("tests/http/service/rate_limit/test_unchecked.c");

    test_suite_begin(&test, "http_service_rate_limit (unchecked)");

    test_case_begin(&test, "the constructors still refuse a zero limit, cooldown or capacity");

    HTTP_Service_Rate_Limit refused = DEFAULT_INITIALIZATION;

    test_expect_false(&test, "init_2 refuses a zero limit", http_service_rate_limit_init_2(&refused, 0, 60));
    test_expect_false(&test, "init_2 refuses a zero cooldown", http_service_rate_limit_init_2(&refused, 60, 0));
    test_expect_false(&test, "init_3 refuses a zero capacity", http_service_rate_limit_init_3(&refused, 60, 60, 0));

    /* A capacity whose byte count overflows is refused by the same plain branch, with the checks
     * compiled out: it would otherwise allocate a wrapped, far smaller block and then be indexed
     * to the capacity that was asked for. */
    test_expect_false(&test, "init_3 refuses a capacity that would overflow the allocation", http_service_rate_limit_init_3(&refused, 60, 60, USIZE_MAX));
    test_expect_false(&test, "and one just past the representable record count", http_service_rate_limit_init_3(&refused, 60, 60, USIZE_MAX / 64));
    test_expect_true(&test, "and leaves *self zeroed", refused.records == nullptr && refused.capacity == 0);

    test_case_end(&test);

    test_case_begin(&test, "an unknown rule is refused without the checks armed");

    HTTP_Service_Rate_Limit rate_limit = DEFAULT_INITIALIZATION;

    test_expect_true(&test, "init_2 succeeds", http_service_rate_limit_init_2(&rate_limit, 60, 60));

    test_expect_false(&test, "allowed_2 refuses an unregistered rule", http_service_rate_limit_allowed_2(&rate_limit, "nosuchrule", "203.0.113.80"));
    test_expect_true(&test, "blocked_2 fails closed", http_service_rate_limit_blocked_2(&rate_limit, "nosuchrule", "203.0.113.80"));
    test_expect_u(&test, "and nothing was recorded", 0, http_service_rate_limit_get_size(&rate_limit));

    test_case_end(&test);

    test_case_begin(&test, "an empty or oversize key is refused without the checks armed");

    test_expect_false(&test, "an empty key is refused", http_service_rate_limit_allowed_1(&rate_limit, ""));
    test_expect_true(&test, "and reads as blocked", http_service_rate_limit_blocked_1(&rate_limit, ""));

    char oversize[HTTP_SERVICE_RATE_LIMIT_KEY_SIZE + 8] = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < HTTP_SERVICE_RATE_LIMIT_KEY_SIZE; i += 1) {
        oversize[i] = 'k';
    }

    test_expect_false(&test, "an oversize key is refused, not truncated", http_service_rate_limit_allowed_1(&rate_limit, oversize));
    test_expect_u(&test, "and neither was tracked", 0, http_service_rate_limit_get_size(&rate_limit));

    test_case_end(&test);

    test_case_begin(&test, "a null key is refused like an empty one without the checks armed");

    /* error_check_null on `key` was removed from the six _2 entry points: the refusal is a DATA
     * decision made by _key_valid, so it must hold identically here, with the checks compiled
     * out, or a null key reaching one of these calls (an ordinary allocation failure upstream,
     * not only a bug) would be undefined instead of refused. */
    test_expect_true(&test, "blocked_2 reads a null key as blocked", http_service_rate_limit_blocked_2(&rate_limit, nullptr, nullptr));
    test_expect_false(&test, "check_2 refuses a null key", http_service_rate_limit_check_2(&rate_limit, nullptr, nullptr, nullptr));
    test_expect_u(&test, "get_remaining_2 answers no budget", 0, http_service_rate_limit_get_remaining_2(&rate_limit, nullptr, nullptr));
    test_expect_u(&test, "get_time_left_2 answers nothing to wait for", 0, http_service_rate_limit_get_time_left_2(&rate_limit, nullptr, nullptr));

    http_service_rate_limit_hit_2(&rate_limit, nullptr, nullptr);
    http_service_rate_limit_reset_2(&rate_limit, nullptr, nullptr);

    test_expect_u(&test, "none of the six calls left a record behind", 0, http_service_rate_limit_get_size(&rate_limit));

    test_case_end(&test);

    test_case_begin(&test, "rule_add still answers USIZE_MAX for an invalid registration");

    test_expect_u(&test, "a zero limit is refused", USIZE_MAX, http_service_rate_limit_rule_add_1(&rate_limit, "zero", 0, 60));
    test_expect_u(&test, "the reserved default name is refused", USIZE_MAX, http_service_rate_limit_rule_add_1(&rate_limit, "default", 5, 60));
    test_expect_true(&test, "a valid registration still answers a handle", http_service_rate_limit_rule_add_1(&rate_limit, "login", 5, 60) != USIZE_MAX);

    http_service_rate_limit_uninit(&rate_limit);

    test_case_end(&test);

    test_case_begin(&test, "eviction still refuses to clear a record at its limit");

    HTTP_Service_Rate_Limit saturated = DEFAULT_INITIALIZATION;

    test_expect_true(&test, "init_3 succeeds", http_service_rate_limit_init_3(&saturated, 1, 86400, 2));

    test_expect_true(&test, "first key spends its budget", http_service_rate_limit_allowed_1(&saturated, "10.0.0.1"));
    test_expect_true(&test, "second key spends its budget", http_service_rate_limit_allowed_1(&saturated, "10.0.0.2"));

    /* The degraded path is a plain runtime branch, so a build with the checks compiled out takes
     * exactly the same one: allow the new key out of the rule's overflow bucket, refuse once that
     * aggregate budget is spent, and keep both throttled records either way. */
    test_expect_true(&test, "a third key is allowed on the degraded path", http_service_rate_limit_allowed_1(&saturated, "10.0.0.3"));
    test_expect_false(&test, "a fourth is refused - the overflow bucket bounds it", http_service_rate_limit_allowed_1(&saturated, "10.0.0.4"));
    test_expect_false(&test, "and so is a fifth", http_service_rate_limit_allowed_1(&saturated, "10.0.0.5"));
    test_expect_u(&test, "without displacing either record", 2, http_service_rate_limit_get_size(&saturated));
    test_expect_true(&test, "the first key is still blocked", http_service_rate_limit_blocked_1(&saturated, "10.0.0.1"));
    test_expect_true(&test, "the second key is still blocked", http_service_rate_limit_blocked_1(&saturated, "10.0.0.2"));

    http_service_rate_limit_uninit(&saturated);

    test_case_end(&test);

    test_suite_end(&test);

    return test_uninit(&test);
}