#include <http/service/ip_block/ip_block.h>
#include <log/log.h>
#include <test/test.h>

/*
 * Behavioral tests for include/http/service/ip_block/ip_block.c built WITHOUT
 * ERROR_CHECK_ENABLED.
 *
 * error_check_null in this module guards only null-pointer contracts on the public entry
 * points - deliberately absent here (undefined behavior with the checks compiled out, and this
 * file never exercises them). Every value-dependent refusal/eviction below - an empty/oversize
 * address, the last-resort eviction of the oldest blocked entry once a full registry has nothing
 * unblocked left, a lifted block - is coded as an ordinary runtime branch, never routed through
 * error_check, so this build proves each one refuses (or expires, or evicts) identically whether
 * ERROR_CHECK_ENABLED is defined or not, per the value-dependent-refusal standard (a
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

    Test test = test_init("tests/http/service/ip_block/test_unchecked.c");

    test_suite_begin(&test, "http_service_ip_block (unchecked)");

    test_case_begin(&test, "empty address is untracked without the checks armed");

    HTTP_Service_IP_Block ip_block = DEFAULT_INITIALIZATION;

    http_service_ip_block_init_1(&ip_block, 1);

    test_expect_false(&test, "add_2 with an empty address declines", http_service_ip_block_add_2(&ip_block, "", 0));
    test_expect_false(&test, "empty address is not blocked", http_service_ip_block_blocked_2(&ip_block, "", 0));

    http_service_ip_block_uninit(&ip_block);

    test_case_end(&test);

    test_case_begin(&test, "an oversize address is refused, not truncated");

    HTTP_Service_IP_Block oversize = DEFAULT_INITIALIZATION;

    http_service_ip_block_init_1(&oversize, 1);

    char long_ip[65] = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 64; i += 1) {
        long_ip[i] = 'a';
    }

    test_expect_false(&test, "a 64-byte address is declined", http_service_ip_block_add_2(&oversize, long_ip, 64));
    test_expect_false(&test, "and stays untracked", http_service_ip_block_exists_2(&oversize, long_ip, 64));

    http_service_ip_block_uninit(&oversize);

    test_case_end(&test);

    test_case_begin(&test, "eviction pressure evicts the oldest blocked entry, never refuses the newest");

    HTTP_Service_IP_Block cap = DEFAULT_INITIALIZATION;

    http_service_ip_block_init_1(&cap, 1);

    http_service_ip_block_add_strike_1(&cap, "10.0.0.1");

    for (USize i = 0; i < 5000; i += 1) {
        char ip[32] = DEFAULT_INITIALIZATION;

        char_from_numbers_uint_1(ip, sizeof(ip), (U32) (i + 2));
        http_service_ip_block_add_strike_1(&cap, ip);
    }

    /* "10.0.0.1" was struck FIRST, so it is the oldest once every tracked address is blocked -
     * exactly the last-resort tier this pins (see ip_block.h Performance Characteristics): it is
     * evicted outright (not merely unblocked) to make room, and the newest address is still
     * tracked and blocked rather than silently refused because the table was full. */
    test_expect_false(&test, "the oldest blocked address was evicted", http_service_ip_block_exists_1(&cap, "10.0.0.1"));

    char newest_ip[32] = DEFAULT_INITIALIZATION;

    char_from_numbers_uint_1(newest_ip, sizeof(newest_ip), 5001);

    test_expect_true(&test, "the newest address is tracked and blocked", http_service_ip_block_blocked_1(&cap, newest_ip));

    http_service_ip_block_uninit(&cap);

    test_case_end(&test);

    test_suite_end(&test);

    return test_uninit(&test);
}