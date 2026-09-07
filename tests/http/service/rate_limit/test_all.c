/*
 * test_all.c - the http/service/rate_limit suite.
 *
 * rate_limit guards the login and payment routes of a server. The cases below pin its whole
 * contract: the one-lock check_2 result (allowed/limit/remaining/retry_after), an
 * unknown rule refused rather than silently served by the default rule, an empty and an
 * oversize key refused fail-closed, window expiry against a real 1-second cooldown, the record
 * cap's eviction policy - which must never discard a record that is AT its limit - the
 * in-place bool constructors and their zeroed-on-refusal contract, rule handles, and one live
 * server on port 0 proving the route -> get_client_ip -> check_2 -> 429 + Retry-After path.
 *
 * One case times four calls against a 1-second cooldown to pin that the read-only queries are
 * genuinely read-only: an expired window is reported, never re-anchored at the query instant, so
 * the anchor stays at the FIRST hit and a query cannot buy the client an early budget.
 *
 * Three cases pin the degraded-path contract: a saturated record table bounds its unrecorded keys
 * by the rule's OVERFLOW bucket instead of allowing every one of them without limit; a capacity
 * whose byte count would wrap is refused before it is allocated; and the degraded-path diagnostics
 * are throttled by TIME, so alternating a bad request with a good one can no longer log on every
 * other call.
 *
 * A fourth pins that the same bucket also bounds the READ-ONLY queries, so the blocked_* + hit_*
 * pairing this module recommends - counting failures only - is bounded under saturation instead of
 * refusing nothing at all.
 *
 * The benchmark case is the LIVE source for the header's performance claim, which is why it warms
 * one pass and reports the min and median of five rather than one cold reading - the single-shot
 * form it replaced produced a figure the header quoted and later runs could not reproduce.
 *
 * The live case is shaped after tests/http/server/test_all.c: http_server_run on port 0, the
 * ephemeral port read back with http_server_get_port, and a raw `net` socket for the client so
 * the bytes on the wire are under the test's own control.
 */
#include <stdio.h>

#include <http/server/http_server.h>
#include <http/service/rate_limit/rate_limit.h>
#include <log/log.h>
#include <net/net.h>
#include <test/test.h>
#include <thread/thread.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/
#define _BENCHMARK_KEYS         8192
#define _BENCHMARK_ROUNDS       4096
#define _BENCHMARK_SAMPLES      5
#define _CLIENT_IO_TIMEOUT_MS   4000
#define _CLIENT_REPLY_MAX       8192
#define _LOG_CAPTURE_PATH       "rate_limit_log_capture.tmp"
#define _REPORT_ROUNDS          10
#define _THREAD_COUNT           8
#define _THREAD_HITS            64

/*==============================================================================
 * MARK: - Helpers
 *============================================================================*/

/* "<prefix><number>" into a caller buffer, NUL-terminated. The suite needs thousands of distinct
 * keys and one shared spelling of them, so the cap and the benchmark measure the same table. */
static void _key_build(char *const buffer, USize const capacity, char const *const prefix, USize const number) {
    USize const prefix_size = char_length(prefix);

    char suffix[24] = DEFAULT_INITIALIZATION;

    char_from_numbers_uint_1(suffix, sizeof(suffix), number);

    USize const suffix_size = char_length(suffix);

    if (prefix_size + suffix_size + 1 > capacity) {
        buffer[0] = '\0';

        return;
    }

    char_copy_2(buffer, prefix, prefix_size);
    char_copy_2(buffer + prefix_size, suffix, suffix_size);

    buffer[prefix_size + suffix_size] = '\0';
}

/*==============================================================================
 * MARK: - Offline cases
 *============================================================================*/

static void _test_rate_limit_allows_then_refuses(Test *const test) {
    test_case_begin(test, "rate_limit refuses past the limit");

    HTTP_Service_Rate_Limit rate_limit = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 succeeds", http_service_rate_limit_init_2(&rate_limit, 3, 60));

    char const *const key = "203.0.113.7";

    test_expect_true(test, "first request allowed", http_service_rate_limit_allowed_1(&rate_limit, key));
    test_expect_true(test, "second request allowed", http_service_rate_limit_allowed_1(&rate_limit, key));
    test_expect_true(test, "third request allowed", http_service_rate_limit_allowed_1(&rate_limit, key));
    test_expect_false(test, "fourth request refused at a limit of three", http_service_rate_limit_allowed_1(&rate_limit, key));

    /* A different key has its own budget - the records are per key, not global. */
    test_expect_true(test, "an unrelated key is unaffected", http_service_rate_limit_allowed_1(&rate_limit, "198.51.100.1"));

    test_expect_u(test, "two keys are two records", 2, http_service_rate_limit_get_size(&rate_limit));

    http_service_rate_limit_uninit(&rate_limit);

    test_case_end(test);
}

static void _test_rate_limit_check_result(Test *const test) {
    test_case_begin(test, "check_2 answers limit, remaining and retry_after under one lock");

    HTTP_Service_Rate_Limit rate_limit = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 succeeds", http_service_rate_limit_init_2(&rate_limit, 60, 60));
    test_expect_true(test, "rule_add answers a handle", http_service_rate_limit_rule_add_1(&rate_limit, "login", 2, 300) != USIZE_MAX);

    HTTP_Service_Rate_Limit_Result result = DEFAULT_INITIALIZATION;

    char const *const key = "203.0.113.20";

    test_expect_true(test, "first login allowed", http_service_rate_limit_check_2(&rate_limit, "login", key, &result));
    test_expect_true(test, "result agrees with the return", result.allowed);
    test_expect_u(test, "the rule's limit, not the instance default", 2, result.limit);
    test_expect_u(test, "one hit left", 1, result.remaining);
    test_expect_u(test, "nothing to retry after while allowed", 0, result.retry_after);

    test_expect_true(test, "second login allowed", http_service_rate_limit_check_2(&rate_limit, "login", key, &result));
    test_expect_u(test, "budget spent", 0, result.remaining);
    test_expect_u(test, "still allowed, so still no retry_after", 0, result.retry_after);

    test_expect_false(test, "third login refused", http_service_rate_limit_check_2(&rate_limit, "login", key, &result));
    test_expect_false(test, "result agrees with the refusal", result.allowed);
    test_expect_u(test, "nothing remaining", 0, result.remaining);

    /* A Retry-After of 0 on a refusal tells the client to retry immediately, which is exactly
     * what the refusal exists to stop - so it is clamped to at least one second and can never
     * exceed the rule's cooldown. */
    test_expect_true(test, "retry_after is a real wait", result.retry_after > 0 && result.retry_after <= 300);

    /* The trio it replaces must agree with it, read separately. */
    test_expect_u(test, "get_remaining_2 agrees", 0, http_service_rate_limit_get_remaining_2(&rate_limit, "login", key));
    test_expect_true(test, "get_time_left_2 agrees", http_service_rate_limit_get_time_left_2(&rate_limit, "login", key) > 0);
    test_expect_true(test, "blocked_2 agrees", http_service_rate_limit_blocked_2(&rate_limit, "login", key));

    http_service_rate_limit_uninit(&rate_limit);

    test_case_end(test);
}

static void _test_rate_limit_per_rule_budgets(Test *const test) {
    test_case_begin(test, "rate_limit keeps per-rule budgets separate");

    HTTP_Service_Rate_Limit rate_limit = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 succeeds", http_service_rate_limit_init_2(&rate_limit, 10, 60));

    USize const login  = http_service_rate_limit_rule_add_1(&rate_limit, "login", 2, 60);
    USize const search = http_service_rate_limit_rule_add_1(&rate_limit, "search", 10, 60);

    test_expect_true(test, "two rules get two distinct handles", login != USIZE_MAX && search != USIZE_MAX && login != search);

    char const *const key = "203.0.113.8";

    test_expect_true(test, "login request one allowed", http_service_rate_limit_allowed_2(&rate_limit, "login", key));
    test_expect_true(test, "login request two allowed", http_service_rate_limit_allowed_2(&rate_limit, "login", key));
    test_expect_false(test, "login request three refused by the rule limit", http_service_rate_limit_allowed_2(&rate_limit, "login", key));

    /* The same key under a different rule carries an independent counter, so exhausting the
     * strict login budget must not lock the client out of everything else. */
    test_expect_true(test, "the same key is still allowed under another rule", http_service_rate_limit_allowed_2(&rate_limit, "search", key));

    /* The reserved default name and a null rule both mean the instance's own limit. */
    test_expect_true(test, "the reserved default name is a third budget", http_service_rate_limit_allowed_2(&rate_limit, "default", key));
    test_expect_u(test, "which is the same record allowed_1 uses", 9, http_service_rate_limit_get_remaining_1(&rate_limit, key));

    http_service_rate_limit_uninit(&rate_limit);

    test_case_end(test);
}

static void _test_rate_limit_unknown_rule_refused(Test *const test) {
    test_case_begin(test, "an unknown rule is refused, never served by the default");

    HTTP_Service_Rate_Limit rate_limit = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 succeeds", http_service_rate_limit_init_2(&rate_limit, 60, 60));

    char const *const key = "203.0.113.30";

    /* Pins that a typo'd rule constant does NOT fall back to the instance's 60/60 default, which
     * would quietly replace a tight login limit with a loose one. It is a refusal at the
     * enforcement point instead, so the misconfiguration is visible immediately. */
    test_expect_false(test, "allowed_2 refuses an unregistered rule", http_service_rate_limit_allowed_2(&rate_limit, "lgoin", key));
    test_expect_true(test, "blocked_2 fails closed on the same rule", http_service_rate_limit_blocked_2(&rate_limit, "lgoin", key));
    test_expect_u(test, "get_remaining_2 answers no budget", 0, http_service_rate_limit_get_remaining_2(&rate_limit, "lgoin", key));
    test_expect_u(test, "the refusal recorded nothing", 0, http_service_rate_limit_get_size(&rate_limit));

    HTTP_Service_Rate_Limit_Result result = DEFAULT_INITIALIZATION;

    test_expect_false(test, "check_2 refuses too", http_service_rate_limit_check_2(&rate_limit, "lgoin", key, &result));
    test_expect_true(test, "and still fills a well-formed 429 answer", !result.allowed && result.remaining == 0 && result.retry_after > 0);

    /* Registering the rule makes it work, which proves the refusal was about registration and
     * not about the name. */
    test_expect_true(test, "rule_add registers it", http_service_rate_limit_rule_add_1(&rate_limit, "lgoin", 1, 60) != USIZE_MAX);
    test_expect_true(test, "and the same call is now allowed", http_service_rate_limit_allowed_2(&rate_limit, "lgoin", key));

    http_service_rate_limit_uninit(&rate_limit);

    test_case_end(test);
}

static void _test_rate_limit_key_refusals(Test *const test) {
    test_case_begin(test, "an empty or oversize key is refused fail-closed");

    HTTP_Service_Rate_Limit rate_limit = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 succeeds", http_service_rate_limit_init_2(&rate_limit, 60, 60));

    /* http_server_request_get_client_ip answers "" when it cannot resolve a client. Tracking
     * that would give every unresolvable client ONE shared budget, so a single forged
     * X-Forwarded-For could exhaust it for all of them - and waving it through would make an
     * unidentifiable caller the only caller with no limit at all. */
    test_expect_false(test, "an empty key is refused", http_service_rate_limit_allowed_1(&rate_limit, ""));
    test_expect_true(test, "and reads as blocked", http_service_rate_limit_blocked_1(&rate_limit, ""));
    test_expect_u(test, "and is not tracked", 0, http_service_rate_limit_get_size(&rate_limit));

    char oversize[HTTP_SERVICE_RATE_LIMIT_KEY_SIZE + 8] = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < HTTP_SERVICE_RATE_LIMIT_KEY_SIZE; i += 1) {
        oversize[i] = 'k';
    }

    /* Truncating instead would let two distinct long tokens share one prefix and one budget. */
    test_expect_false(test, "a key at the buffer size is refused, not truncated", http_service_rate_limit_allowed_1(&rate_limit, oversize));
    test_expect_u(test, "and is not tracked either", 0, http_service_rate_limit_get_size(&rate_limit));

    http_service_rate_limit_uninit(&rate_limit);

    test_case_end(test);
}

static void _test_rate_limit_null_key_refused(Test *const test) {
    test_case_begin(test, "a null key is refused like an empty one, on every _2 entry point");

    HTTP_Service_Rate_Limit rate_limit = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 succeeds", http_service_rate_limit_init_2(&rate_limit, 60, 60));

    /* A null key reaches these entry points from an ordinary allocation failure upstream (e.g. an
     * arena-backed string_alloc_init_2 the caller never checked), not only a programming error -
     * so each must refuse it as DATA, exactly like _key_valid already refuses an empty one, never
     * abort. */
    test_expect_true(test, "blocked_2 reads a null key as blocked", http_service_rate_limit_blocked_2(&rate_limit, nullptr, nullptr));

    HTTP_Service_Rate_Limit_Result result = DEFAULT_INITIALIZATION;

    test_expect_false(test, "check_2 refuses a null key", http_service_rate_limit_check_2(&rate_limit, nullptr, nullptr, &result));
    test_expect_true(test, "and still fills a well-formed 429 answer", !result.allowed && result.remaining == 0 && result.retry_after > 0);
    test_expect_u(test, "get_remaining_2 answers no budget", 0, http_service_rate_limit_get_remaining_2(&rate_limit, nullptr, nullptr));
    test_expect_u(test, "get_time_left_2 answers nothing to wait for", 0, http_service_rate_limit_get_time_left_2(&rate_limit, nullptr, nullptr));

    http_service_rate_limit_hit_2(&rate_limit, nullptr, nullptr);
    http_service_rate_limit_reset_2(&rate_limit, nullptr, nullptr);

    test_expect_u(test, "none of the six calls left a record behind", 0, http_service_rate_limit_get_size(&rate_limit));

    http_service_rate_limit_uninit(&rate_limit);

    test_case_end(test);
}

static void _test_rate_limit_window_expiry(Test *const test) {
    test_case_begin(test, "the window refreshes after a real cooldown");

    HTTP_Service_Rate_Limit rate_limit = DEFAULT_INITIALIZATION;

    /* A 1-second cooldown and a real sleep: the lazy reset was previously unpinned entirely, so
     * nothing proved a blocked client is ever let back in. */
    test_expect_true(test, "init_2 succeeds", http_service_rate_limit_init_2(&rate_limit, 1, 1));

    char const *const key = "203.0.113.40";

    test_expect_true(test, "the one allowed request lands", http_service_rate_limit_allowed_1(&rate_limit, key));
    test_expect_false(test, "the next is refused", http_service_rate_limit_allowed_1(&rate_limit, key));
    test_expect_true(test, "and reports a wait", http_service_rate_limit_get_time_left_1(&rate_limit, key) > 0);

    thread_sleep(1100);

    test_expect_false(test, "the cooldown has lifted the block", http_service_rate_limit_blocked_1(&rate_limit, key));
    test_expect_u(test, "the window is fresh", 1, http_service_rate_limit_get_remaining_1(&rate_limit, key));
    test_expect_u(test, "with nothing left to wait for", 0, http_service_rate_limit_get_time_left_1(&rate_limit, key));
    test_expect_true(test, "and the client is allowed again", http_service_rate_limit_allowed_1(&rate_limit, key));

    http_service_rate_limit_uninit(&rate_limit);

    test_case_end(test);
}

static void _test_rate_limit_read_only_query_does_not_reanchor(Test *const test) {
    test_case_begin(test, "a read-only query never re-anchors an expired window");

    HTTP_Service_Rate_Limit rate_limit = DEFAULT_INITIALIZATION;

    /* The window is anchored at its FIRST hit, so the only thing that may move the anchor is a
     * registering call. blocked_* used to refresh the record it was merely reading, opening a
     * window at the QUERY instant; the hit that followed then counted into THAT window and got its
     * next budget early, making the limiter more permissive by up to (hit - query) seconds - and a
     * periodic monitoring query kept an idle record's timestamp moving so the sweep never
     * reclaimed its slot. The timing below fails on the last assertion without the fix. */
    test_expect_true(test, "init_2 succeeds", http_service_rate_limit_init_2(&rate_limit, 1, 1));

    char const *const key = "203.0.113.41";

    test_expect_true(test, "the window opens at the first hit", http_service_rate_limit_allowed_1(&rate_limit, key));

    thread_sleep(1100);

    /* The read-only query at t=1.1: it must REPORT the expired window without re-anchoring it. */
    test_expect_false(test, "the expired window reads unblocked", http_service_rate_limit_blocked_1(&rate_limit, key));

    thread_sleep(600);

    /* t=1.7 - the first REGISTERING call since expiry, so this is where the new window opens. */
    test_expect_true(test, "the next hit is allowed and anchors the new window", http_service_rate_limit_allowed_1(&rate_limit, key));

    thread_sleep(500);

    /* t=2.2, only 0.5 s into the window anchored at 1.7. Had the query at 1.1 anchored it, the
     * window would have run [1.1, 2.1) and this second hit would land in a fresh one. */
    test_expect_false(test, "a second hit inside that window is refused", http_service_rate_limit_allowed_1(&rate_limit, key));

    http_service_rate_limit_uninit(&rate_limit);

    test_case_end(test);
}

static void _test_rate_limit_eviction_spares_at_limit(Test *const test) {
    test_case_begin(test, "eviction never clears a record at its limit");

    HTTP_Service_Rate_Limit rate_limit = DEFAULT_INITIALIZATION;

    /* Two slots, one hit each, a cooldown far longer than the test: after two keys the table is
     * saturated with records that are both AT their limit. */
    test_expect_true(test, "init_3 takes a capacity", http_service_rate_limit_init_3(&rate_limit, 1, 86400, 2));

    test_expect_true(test, "first key spends its budget", http_service_rate_limit_allowed_1(&rate_limit, "10.0.0.1"));
    test_expect_true(test, "second key spends its budget", http_service_rate_limit_allowed_1(&rate_limit, "10.0.0.2"));
    test_expect_u(test, "the table is full", 2, http_service_rate_limit_get_size(&rate_limit));

    /* A third key finds nothing evictable. Evicting either at-limit record would recreate it at
     * count 0 on its next request - handing a full budget back to a client currently being
     * throttled, so a caller rotating keys could flush their own limiter. The service degrades
     * instead - the new key spends the rule's overflow bucket rather than a record of its own -
     * and the two throttled keys STAY throttled. */
    test_expect_true(test, "the new key is allowed on the degraded path", http_service_rate_limit_allowed_1(&rate_limit, "10.0.0.3"));
    test_expect_false(test, "and the one after it is refused, not waved through", http_service_rate_limit_allowed_1(&rate_limit, "10.0.0.4"));
    test_expect_u(test, "without displacing either record", 2, http_service_rate_limit_get_size(&rate_limit));
    test_expect_true(test, "the first key is still blocked", http_service_rate_limit_blocked_1(&rate_limit, "10.0.0.1"));
    test_expect_true(test, "the second key is still blocked", http_service_rate_limit_blocked_1(&rate_limit, "10.0.0.2"));

    http_service_rate_limit_uninit(&rate_limit);

    test_case_end(test);
}

static void _test_rate_limit_overflow_bucket_bounds_saturation(Test *const test) {
    test_case_begin(test, "a saturated table bounds unrecorded keys by the rule's overflow bucket");

    HTTP_Service_Rate_Limit rate_limit = DEFAULT_INITIALIZATION;

    /* Three slots, a limit of three, a cooldown far longer than the test: spend every slot's
     * budget and the table is saturated with records that may not be evicted. */
    test_expect_true(test, "init_3 succeeds", http_service_rate_limit_init_3(&rate_limit, 3, 86400, 3));

    char key[64] = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 3; i += 1) {
        _key_build(key, sizeof(key), "10.1.0.", i);

        for (USize hit = 0; hit < 3; hit += 1) {
            http_service_rate_limit_allowed_1(&rate_limit, key);
        }
    }

    test_expect_u(test, "the table is full", 3, http_service_rate_limit_get_size(&rate_limit));
    test_expect_true(test, "and its records are at their limit", http_service_rate_limit_blocked_1(&rate_limit, "10.1.0.0"));

    /* The defect this pins: an unrecorded hit used to be allowed with NOTHING written down, so
     * every new key was allowed without any bound at all for as long as saturation lasted - and
     * saturation is cheap to buy when the key is client-supplied (a registration rule keyed on the
     * submitted email address, say, so filling the shared table also disarms an IP-keyed login
     * rule). The overflow bucket keeps the direction of the degradation - unrecorded keys are
     * still allowed - but caps a rule's unrecorded allowances at its own limit per its own window,
     * in AGGREGATE across every such key. */
    USize allowed = 0;

    for (USize i = 0; i < 64; i += 1) {
        _key_build(key, sizeof(key), "203.0.113.", i);

        if (http_service_rate_limit_allowed_1(&rate_limit, key)) {
            allowed += 1;
        }
    }

    test_expect_u(test, "64 fresh keys share exactly the rule's limit between them", 3, allowed);
    test_expect_u(test, "and not one of them was recorded", 3, http_service_rate_limit_get_size(&rate_limit));

    /* The refusal is a well-formed 429, not a bare false: a client told to retry immediately would
     * come straight back into the same degraded path. */
    HTTP_Service_Rate_Limit_Result result = DEFAULT_INITIALIZATION;

    test_expect_false(test, "the next unrecorded key is refused", http_service_rate_limit_check_1(&rate_limit, "198.51.100.77", &result));
    test_expect_u(test, "with nothing remaining", 0, result.remaining);
    test_expect_true(test, "and a real Retry-After", result.retry_after > 0 && result.retry_after <= 86400);

    /* Per RULE, not global: one rule's overflow can never spend another's. */
    test_expect_true(test, "a named rule registers", http_service_rate_limit_rule_add_1(&rate_limit, "login", 2, 86400) != USIZE_MAX);
    test_expect_true(test, "whose own bucket still has budget", http_service_rate_limit_allowed_2(&rate_limit, "login", "198.51.100.77"));
    test_expect_true(test, "for exactly its own limit", http_service_rate_limit_allowed_2(&rate_limit, "login", "198.51.100.78"));
    test_expect_false(test, "and no more", http_service_rate_limit_allowed_2(&rate_limit, "login", "198.51.100.79"));

    /* clear() forgets the buckets along with the records, so the limiter comes back undegraded -
     * otherwise the one piece of state a caller could not clear by any means would keep refusing
     * keys the emptied table has room for. */
    http_service_rate_limit_clear(&rate_limit);

    test_expect_true(test, "after clear the table records again", http_service_rate_limit_allowed_1(&rate_limit, "198.51.100.80"));
    test_expect_u(test, "as a real record, not an overflow allowance", 1, http_service_rate_limit_get_size(&rate_limit));

    http_service_rate_limit_uninit(&rate_limit);

    test_case_end(test);
}

static void _test_rate_limit_overflow_bucket_bounds_the_read_only_queries(Test *const test) {
    test_case_begin(test, "a spent overflow bucket blocks an unrecorded key on the read-only path too");

    HTTP_Service_Rate_Limit rate_limit = DEFAULT_INITIALIZATION;

    /* Two slots and a limit of two: four hits saturate the table with records that may not be
     * evicted, which is the state the whole degraded path exists for. */
    test_expect_true(test, "init_3 succeeds", http_service_rate_limit_init_3(&rate_limit, 60, 86400, 2));
    test_expect_true(test, "the login rule registers", http_service_rate_limit_rule_add_1(&rate_limit, "login", 2, 86400) != USIZE_MAX);

    http_service_rate_limit_hit_2(&rate_limit, "login", "10.2.0.1");
    http_service_rate_limit_hit_2(&rate_limit, "login", "10.2.0.1");
    http_service_rate_limit_hit_2(&rate_limit, "login", "10.2.0.2");
    http_service_rate_limit_hit_2(&rate_limit, "login", "10.2.0.2");

    test_expect_u(test, "the table is full", 2, http_service_rate_limit_get_size(&rate_limit));
    test_expect_true(test, "with both records at the rule's limit", http_service_rate_limit_blocked_2(&rate_limit, "login", "10.2.0.1"));

    /* Saturated but the bucket is untouched: a fresh key is genuinely unblocked, and the fix must
     * not turn saturation ALONE into a blanket refusal. */
    test_expect_false(test, "a fresh key is unblocked while the bucket is unspent", http_service_rate_limit_blocked_2(&rate_limit, "login", "203.0.113.11"));

    /* The defect this pins. An enforcement point that counts FAILURES only (a login route, say)
     * reads blocked_2 and counts with hit_2 - the pairing this header recommends. hit_2 spent
     * the rule's overflow bucket while blocked_2 read every unrecorded key as unblocked, so under
     * saturation that pairing refused NOTHING: the bucket bounded allowed_/check_ callers and left
     * the recommended one with no bound at all. Two hits fill a two-per-window bucket. */
    http_service_rate_limit_hit_2(&rate_limit, "login", "203.0.113.11");

    /* Pins the PARTIALLY spent case: with one of the bucket's two allowances taken, a fresh
     * unrecorded key is not yet blocked, but its real remaining budget is 1, not the rule's full
     * limit of 2 - get_remaining_2 must answer what a registering call would actually grant next,
     * not the untouched-bucket answer. */
    test_expect_false(test, "a fresh key is not yet blocked with the bucket half-spent", http_service_rate_limit_blocked_2(&rate_limit, "login", "203.0.113.20"));
    test_expect_u(test, "but its remaining budget reflects the partial spend", 1, http_service_rate_limit_get_remaining_2(&rate_limit, "login", "203.0.113.20"));

    http_service_rate_limit_hit_2(&rate_limit, "login", "203.0.113.12");

    test_expect_u(test, "not one unrecorded hit took a slot", 2, http_service_rate_limit_get_size(&rate_limit));
    test_expect_true(test, "a fresh key now reads BLOCKED off the spent bucket", http_service_rate_limit_blocked_2(&rate_limit, "login", "203.0.113.13"));
    test_expect_u(test, "with nothing remaining, not the full limit", 0, http_service_rate_limit_get_remaining_2(&rate_limit, "login", "203.0.113.13"));

    USize const time_left = http_service_rate_limit_get_time_left_2(&rate_limit, "login", "203.0.113.13");

    test_expect_true(test, "and a real wait, not 0", time_left > 0 && time_left <= 86400);

    /* Per RULE on this path as well: the default rule's own bucket was never spent. */
    test_expect_false(test, "the default rule's bucket is untouched", http_service_rate_limit_blocked_1(&rate_limit, "203.0.113.13"));

    /* clear() forgets the buckets with the records, so the queries come back undegraded. */
    http_service_rate_limit_clear(&rate_limit);

    test_expect_false(test, "after clear the same key reads unblocked again", http_service_rate_limit_blocked_2(&rate_limit, "login", "203.0.113.13"));
    test_expect_u(test, "with its full budget back", 2, http_service_rate_limit_get_remaining_2(&rate_limit, "login", "203.0.113.13"));

    http_service_rate_limit_uninit(&rate_limit);

    test_case_end(test);
}

static void _test_rate_limit_eviction_prefers_unblocked(Test *const test) {
    test_case_begin(test, "eviction takes the record still under its limit");

    HTTP_Service_Rate_Limit rate_limit = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_3 succeeds", http_service_rate_limit_init_3(&rate_limit, 5, 86400, 2));

    for (USize i = 0; i < 5; i += 1) {
        http_service_rate_limit_hit_1(&rate_limit, "10.0.1.1");
    }

    http_service_rate_limit_hit_1(&rate_limit, "10.0.1.2");

    test_expect_true(test, "the first key is at its limit", http_service_rate_limit_blocked_1(&rate_limit, "10.0.1.1"));
    test_expect_false(test, "the second key is not", http_service_rate_limit_blocked_1(&rate_limit, "10.0.1.2"));

    /* The only evictable record is the one still under its limit, so the throttled key survives
     * and the lightly-used one is the one recreated. */
    test_expect_true(test, "a third key is admitted", http_service_rate_limit_allowed_1(&rate_limit, "10.0.1.3"));
    test_expect_u(test, "the table stayed at capacity", 2, http_service_rate_limit_get_size(&rate_limit));
    test_expect_true(test, "the at-limit key survived eviction", http_service_rate_limit_blocked_1(&rate_limit, "10.0.1.1"));
    test_expect_u(test, "the evicted key comes back with a full budget", 5, http_service_rate_limit_get_remaining_1(&rate_limit, "10.0.1.2"));

    http_service_rate_limit_uninit(&rate_limit);

    test_case_end(test);
}

static void _test_rate_limit_record_cap_holds(Test *const test) {
    test_case_begin(test, "rate_limit record table stays bounded");

    HTTP_Service_Rate_Limit rate_limit = DEFAULT_INITIALIZATION;

    /* A long cooldown so the expiry sweep never fires: this exercises the CAP specifically,
     * which is the only thing standing between a key-rotating client and unbounded growth. */
    test_expect_true(test, "init_2 succeeds", http_service_rate_limit_init_2(&rate_limit, 5, 86400));

    char key[64] = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 9000; i += 1) {
        _key_build(key, sizeof(key), "10.0.", i);

        http_service_rate_limit_allowed_1(&rate_limit, key);
    }

    /* 9000 distinct keys against a default capacity of 8192. The service must still answer
     * correctly rather than having grown a record per key - the point of the cap is that the
     * per-request linear scan stays bounded too. */
    test_expect_true(test, "the service still answers after key rotation", http_service_rate_limit_allowed_1(&rate_limit, "203.0.113.200"));
    test_expect_true(test, "and never exceeded its capacity", http_service_rate_limit_get_size(&rate_limit) <= HTTP_SERVICE_RATE_LIMIT_DEFAULT_CAPACITY);

    http_service_rate_limit_uninit(&rate_limit);

    test_case_end(test);
}

static void _test_rate_limit_hit_counts(Test *const test) {
    test_case_begin(test, "rate_limit hit registers against the budget");

    HTTP_Service_Rate_Limit rate_limit = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 succeeds", http_service_rate_limit_init_2(&rate_limit, 2, 60));

    char const *const key = "203.0.113.9";

    /* hit_* records without asking; allowed_* both records AND checks. Mixing them is how a
     * caller double-counts and halves its own limit, so the two must compose predictably. */
    http_service_rate_limit_hit_1(&rate_limit, key);

    test_expect_true(test, "one hit leaves budget for one more", http_service_rate_limit_allowed_1(&rate_limit, key));
    test_expect_false(test, "the budget is then spent", http_service_rate_limit_allowed_1(&rate_limit, key));

    /* blocked_* judges without registering, so asking twice cannot change the answer. */
    test_expect_true(test, "blocked_1 reports the block", http_service_rate_limit_blocked_1(&rate_limit, key));
    test_expect_true(test, "and asking again does not consume anything", http_service_rate_limit_blocked_1(&rate_limit, key));

    http_service_rate_limit_uninit(&rate_limit);

    test_case_end(test);
}

static void _test_rate_limit_reset_and_clear(Test *const test) {
    test_case_begin(test, "reset removes one record, clear removes them all");

    HTTP_Service_Rate_Limit rate_limit = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 succeeds", http_service_rate_limit_init_2(&rate_limit, 1, 3600));

    test_expect_true(test, "first key spends its budget", http_service_rate_limit_allowed_1(&rate_limit, "203.0.113.50"));
    test_expect_true(test, "second key spends its budget", http_service_rate_limit_allowed_1(&rate_limit, "203.0.113.51"));
    test_expect_u(test, "two records", 2, http_service_rate_limit_get_size(&rate_limit));

    /* reset REMOVES the record rather than zeroing it, so it stops occupying a capacity slot. */
    http_service_rate_limit_reset_1(&rate_limit, "203.0.113.50");

    test_expect_u(test, "the reset record is gone, not merely zeroed", 1, http_service_rate_limit_get_size(&rate_limit));
    test_expect_false(test, "and the key is unblocked", http_service_rate_limit_blocked_1(&rate_limit, "203.0.113.50"));
    test_expect_true(test, "while the other key keeps its block", http_service_rate_limit_blocked_1(&rate_limit, "203.0.113.51"));

    http_service_rate_limit_clear(&rate_limit);

    test_expect_u(test, "clear empties the table", 0, http_service_rate_limit_get_size(&rate_limit));
    test_expect_false(test, "so every key starts fresh", http_service_rate_limit_blocked_1(&rate_limit, "203.0.113.51"));

    http_service_rate_limit_uninit(&rate_limit);

    test_case_end(test);
}

static void _test_rate_limit_rule_add_contract(Test *const test) {
    test_case_begin(test, "rule_add answers a handle, refuses bad registrations");

    HTTP_Service_Rate_Limit rate_limit = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 succeeds", http_service_rate_limit_init_2(&rate_limit, 60, 60));

    USize const first = http_service_rate_limit_rule_add_1(&rate_limit, "login", 5, 300);

    test_expect_true(test, "a new rule answers a handle", first != USIZE_MAX);

    /* Re-registering updates in place and keeps the handle, so a startup that reconfigures a
     * rule does not consume a second slot or invalidate a stored index. */
    test_expect_u(test, "re-registering keeps the same handle", first, http_service_rate_limit_rule_add_1(&rate_limit, "login", 1, 300));

    char const *const key = "203.0.113.60";

    test_expect_true(test, "the updated limit is what enforces", http_service_rate_limit_allowed_2(&rate_limit, "login", key));
    test_expect_false(test, "at one hit, not five", http_service_rate_limit_allowed_2(&rate_limit, "login", key));

    /* Every refusal is a VALUE answer, never an abort - a caller can check it and fail startup. */
    test_expect_u(test, "a zero limit is refused", USIZE_MAX, http_service_rate_limit_rule_add_1(&rate_limit, "zero_limit", 0, 60));
    test_expect_u(test, "a zero cooldown is refused", USIZE_MAX, http_service_rate_limit_rule_add_1(&rate_limit, "zero_cooldown", 5, 0));
    test_expect_u(test, "an empty name is refused", USIZE_MAX, http_service_rate_limit_rule_add_1(&rate_limit, "", 5, 60));
    test_expect_u(test, "the reserved default name is refused", USIZE_MAX, http_service_rate_limit_rule_add_1(&rate_limit, "default", 5, 60));

    char long_name[HTTP_SERVICE_RATE_LIMIT_RULE_NAME_SIZE + 8] = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < HTTP_SERVICE_RATE_LIMIT_RULE_NAME_SIZE; i += 1) {
        long_name[i] = 'r';
    }

    test_expect_u(test, "an oversize name is refused", USIZE_MAX, http_service_rate_limit_rule_add_1(&rate_limit, long_name, 5, 60));

    /* Filling the table is a refusal too, not a silent overwrite. */
    char name[32] = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < HTTP_SERVICE_RATE_LIMIT_RULE_CAPACITY; i += 1) {
        _key_build(name, sizeof(name), "rule_", i);

        http_service_rate_limit_rule_add_1(&rate_limit, name, 5, 60);
    }

    test_expect_u(test, "a full rule table refuses the next registration", USIZE_MAX, http_service_rate_limit_rule_add_1(&rate_limit, "one_too_many", 5, 60));

    http_service_rate_limit_uninit(&rate_limit);

    test_case_end(test);
}

static void _test_rate_limit_init_refusals(Test *const test) {
    test_case_begin(test, "the constructors refuse a zero limit, cooldown or capacity");

    HTTP_Service_Rate_Limit rate_limit = DEFAULT_INITIALIZATION;

    /* The header always claimed these had to be non-zero; nothing enforced it. A cooldown of 0
     * makes every read open a fresh window - the limiter silently OFF - and a limit of 0 refuses
     * every request. */
    test_expect_false(test, "init_2 refuses a zero limit", http_service_rate_limit_init_2(&rate_limit, 0, 60));
    test_expect_false(test, "init_2 refuses a zero cooldown", http_service_rate_limit_init_2(&rate_limit, 60, 0));
    test_expect_false(test, "init_3 refuses a zero capacity", http_service_rate_limit_init_3(&rate_limit, 60, 60, 0));

    /* A false return leaves *self ZEROED, which is what makes "do not use this instance" a state
     * a caller can actually see. */
    test_expect_u(test, "the refused instance is left zeroed", 0, rate_limit.capacity);
    test_expect_true(test, "with no storage attached", rate_limit.records == nullptr);

    test_case_end(test);
}

static void _test_rate_limit_normalized_text_keys(Test *const test) {
    test_case_begin(test, "clients keyed through net_socket_address_key_2 are distinct budgets");

    HTTP_Service_Rate_Limit rate_limit = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 succeeds", http_service_rate_limit_init_2(&rate_limit, 1, 3600));

    char first[NET_SOCKET_ADDRESS_KEY_TEXT_SIZE]  = DEFAULT_INITIALIZATION;
    char second[NET_SOCKET_ADDRESS_KEY_TEXT_SIZE] = DEFAULT_INITIALIZATION;

    /* The TEXT form, not the byte form ip_block keys on: a key here is measured with char_length,
     * and an IPv6 address is mostly zero bytes, so a byte key would be cut at its first one and
     * collapse unrelated clients onto one budget - or be refused outright as empty. */
    USize const first_size  = net_socket_address_key_2("::1", 3, first, sizeof(first));
    USize const second_size = net_socket_address_key_2("10.0.0.1", 8, second, sizeof(second));

    test_expect_true(test, "an IPv6 client yields a non-empty text key", first_size > 0 && char_length(first) == first_size);
    test_expect_true(test, "an IPv4 client yields a non-empty text key", second_size > 0 && char_length(second) == second_size);
    test_expect_false(test, "and the two keys differ", char_compare_equal_2(first, first_size, second, second_size));

    test_expect_true(test, "check_2 accepts the IPv6 key", http_service_rate_limit_allowed_1(&rate_limit, first));
    test_expect_true(test, "and the IPv4 key, on its own budget", http_service_rate_limit_allowed_1(&rate_limit, second));
    test_expect_u(test, "two clients are two records", 2, http_service_rate_limit_get_size(&rate_limit));

    test_expect_false(test, "the IPv6 client is then spent", http_service_rate_limit_allowed_1(&rate_limit, first));
    test_expect_false(test, "and so is the IPv4 client, separately", http_service_rate_limit_allowed_1(&rate_limit, second));

    http_service_rate_limit_uninit(&rate_limit);

    test_case_end(test);
}

static void _test_rate_limit_capacity_overflow_refused(Test *const test) {
    test_case_begin(test, "a capacity whose byte count would wrap is refused");

    HTTP_Service_Rate_Limit rate_limit = DEFAULT_INITIALIZATION;

    /* sizeof(record) * capacity is computed in USize. Past USIZE_MAX / sizeof(record) it wraps,
     * so a block far smaller than asked for would be allocated and then indexed all the way to
     * the requested capacity on the request path - a heap overflow bought with one large
     * configuration number. Refused as a VALUE, like the zero checks beside it. */
    test_expect_false(test, "init_3 refuses USIZE_MAX", http_service_rate_limit_init_3(&rate_limit, 60, 60, USIZE_MAX));
    test_expect_false(test, "and a capacity past the representable record count", http_service_rate_limit_init_3(&rate_limit, 60, 60, USIZE_MAX / 64));
    test_expect_true(test, "leaving *self zeroed either way", rate_limit.records == nullptr && rate_limit.capacity == 0);

    /* A capacity that merely cannot be satisfied does not wrap; it is caught one check later, by
     * the allocation, and leaves the instance in the same unusable state. */
    test_expect_false(test, "an unsatisfiable capacity is refused too", http_service_rate_limit_init_3(&rate_limit, 60, 60, USIZE_MAX / 1024));
    test_expect_true(test, "and still leaves *self zeroed", rate_limit.records == nullptr && rate_limit.capacity == 0);

    test_case_end(test);
}

static void _test_rate_limit_arena_refusal(Test *const test) {
    test_case_begin(test, "alloc_init on an arena too small answers false and zeroes self");

    /* The arena forms genuinely allocate the record array through the allocator, so an arena
     * that cannot meet the request is the one real allocation failure this module can be shown.
     * It must refuse rather than run with no table. */
    Arena arena = arena_init_1(64, ARENA_TYPE_LINEAR);

    HTTP_Service_Rate_Limit rate_limit = DEFAULT_INITIALIZATION;

    test_expect_false(test, "alloc_init_3 refuses an arena that cannot hold the table",
        http_service_rate_limit_alloc_init_3(&rate_limit, 60, 60, HTTP_SERVICE_RATE_LIMIT_DEFAULT_CAPACITY, &arena));
    test_expect_true(test, "and leaves *self zeroed", rate_limit.records == nullptr && rate_limit.capacity == 0);

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_rate_limit_diagnostics_are_time_throttled(Test *const test) {
    test_case_begin(test, "alternating good and bad keys log at most once per window");

    HTTP_Service_Rate_Limit rate_limit = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 succeeds", http_service_rate_limit_init_2(&rate_limit, 60, 60));

    FILE *const stream = fopen(_LOG_CAPTURE_PATH, "w+");

    if (!test_expect_true(test, "a capture stream opens", stream != nullptr)) {
        http_service_rate_limit_uninit(&rate_limit);

        test_case_end(test);

        return;
    }

    LogConfig const captured = {
        .level             = LOG_LEVEL_ERROR,
        .stream            = stream,
        .timestamp_enabled = false,
        .autoflush         = true
    };

    log_init(captured);

    /* The throttle used to be an EDGE: a bool set on the first report and cleared on the next
     * SUCCESS. Alternating one unknown rule with one known one therefore re-armed it on every
     * other call and logged all ten refusals - and which request comes next is the caller's
     * choice, so "once per episode" bounded only well-behaved callers. Time cannot be re-armed
     * that way. */
    for (USize i = 0; i < _REPORT_ROUNDS; i += 1) {
        http_service_rate_limit_allowed_2(&rate_limit, "nosuchrule", "203.0.113.90");
        http_service_rate_limit_allowed_1(&rate_limit, "203.0.113.90");
    }

    LogConfig const restored = {
        .level             = LOG_LEVEL_ERROR,
        .stream            = stdout,
        .timestamp_enabled = true,
        .autoflush         = true
    };

    log_init(restored);

    char captured_text[_CLIENT_REPLY_MAX] = DEFAULT_INITIALIZATION;

    rewind(stream);

    USize const read_size = fread(captured_text, 1, sizeof(captured_text) - 1, stream);

    captured_text[read_size] = '\0';

    fclose(stream);
    remove(_LOG_CAPTURE_PATH);

    test_expect_u(test, "ten refusals interleaved with ten successes logged once", 1, char_find_count_1(captured_text, "is not registered"));
    test_expect_true(test, "and every one of them still refused", !http_service_rate_limit_allowed_2(&rate_limit, "nosuchrule", "203.0.113.90"));

    http_service_rate_limit_uninit(&rate_limit);

    test_case_end(test);
}

static void _test_rate_limit_two_instances_are_independent(Test *const test) {
    test_case_begin(test, "rate_limit instances are independent");

    /* The mutex used to be file-static: shared across instances, re-initialized by every init
     * and destroyed by the first uninit, so tearing one limiter down left the others locking a
     * destroyed mutex. Each instance now owns its lock. */
    HTTP_Service_Rate_Limit first  = DEFAULT_INITIALIZATION;
    HTTP_Service_Rate_Limit second = DEFAULT_INITIALIZATION;

    test_expect_true(test, "the first limiter initializes", http_service_rate_limit_init_2(&first, 1, 60));
    test_expect_true(test, "the second limiter initializes", http_service_rate_limit_init_2(&second, 1, 60));

    char const *const key = "203.0.113.10";

    test_expect_true(test, "first limiter allows one", http_service_rate_limit_allowed_1(&first, key));
    test_expect_false(test, "first limiter is then spent", http_service_rate_limit_allowed_1(&first, key));

    test_expect_true(test, "the second limiter has its own budget for the same key", http_service_rate_limit_allowed_1(&second, key));

    /* Destroying one must leave the other fully operational. */
    http_service_rate_limit_uninit(&first);

    test_expect_false(test, "the survivor keeps its own state after the other is destroyed", http_service_rate_limit_allowed_1(&second, key));

    http_service_rate_limit_uninit(&second);

    test_case_end(test);
}

static void _test_rate_limit_uninit_without_use(Test *const test) {
    test_case_begin(test, "rate_limit uninit without a single request");

    /* A limiter that never saw a request has an allocated but empty table. Teardown must survive
     * that, and must survive a second instance built and torn down after it. */
    HTTP_Service_Rate_Limit rate_limit = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_1 succeeds", http_service_rate_limit_init_1(&rate_limit));
    test_expect_u(test, "with an empty table", 0, http_service_rate_limit_get_size(&rate_limit));

    http_service_rate_limit_uninit(&rate_limit);

    test_expect_true(test, "teardown of an unused limiter survives", true);

    test_case_end(test);
}

static void _test_rate_limit_uninit_twice_is_noop(Test *const test) {
    test_case_begin(test, "a second uninit on the same instance is a harmless no-op");

    HTTP_Service_Rate_Limit rate_limit = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_1 succeeds", http_service_rate_limit_init_1(&rate_limit));

    http_service_rate_limit_uninit(&rate_limit);
    http_service_rate_limit_uninit(&rate_limit);

    test_expect_true(test, "the second call did not destroy the mutex twice", true);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Concurrency
 *============================================================================*/

typedef struct {
    HTTP_Service_Rate_Limit *rate_limit;
    USize allowed;
} _Worker;

static void* _worker_run(void *const data) {
    _Worker *const worker = (_Worker*) data;

    for (USize i = 0; i < _THREAD_HITS; i += 1) {
        if (http_service_rate_limit_allowed_1(worker->rate_limit, "203.0.113.70")) {
            worker->allowed += 1;
        }
    }

    return nullptr;
}

static void _test_rate_limit_threads_total_exactly_the_limit(Test *const test) {
    test_case_begin(test, "N threads on one key total exactly the limit");

    HTTP_Service_Rate_Limit rate_limit = DEFAULT_INITIALIZATION;

    USize const limit = 100;

    test_expect_true(test, "init_2 succeeds", http_service_rate_limit_init_2(&rate_limit, limit, 3600));

    Thread  threads[_THREAD_COUNT] = DEFAULT_INITIALIZATION;
    _Worker workers[_THREAD_COUNT] = DEFAULT_INITIALIZATION;
    bool    created[_THREAD_COUNT] = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < _THREAD_COUNT; i += 1) {
        workers[i].rate_limit = &rate_limit;
        created[i]            = result_is_success(thread_create_1(&threads[i], _worker_run, &workers[i]));

        test_expect_true(test, "worker thread starts", created[i]);
    }

    USize allowed = 0;

    /* Join only the threads that were actually created: joining a Thread thread_create_1 never
     * populated is undefined, not merely a wasted wait. */
    for (USize i = 0; i < _THREAD_COUNT; i += 1) {
        if (!created[i]) {
            continue;
        }

        thread_join_1(&threads[i]);

        allowed += workers[i].allowed;
    }

    /* 8 threads x 64 attempts = 512 against a limit of 100. Under-counting would let a client
     * past its limit; over-counting would refuse a client that was inside it. Exactly the limit
     * is the only correct total, and it is what the single per-instance lock buys. */
    test_expect_u(test, "exactly the limit was allowed across every thread", limit, allowed);
    test_expect_true(test, "and the key is blocked afterwards", http_service_rate_limit_blocked_1(&rate_limit, "203.0.113.70"));

    http_service_rate_limit_uninit(&rate_limit);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Measurement
 *============================================================================*/

static void _test_rate_limit_benchmark(Test *const test) {
    test_case_begin(test, "allowed_1 cost with a full table and rotating keys");

    HTTP_Service_Rate_Limit rate_limit = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 succeeds", http_service_rate_limit_init_2(&rate_limit, 5, 86400));

    char key[64] = DEFAULT_INITIALIZATION;

    // Saturate the table first, so the measured calls scan a FULL 8192-record array.
    for (USize i = 0; i < _BENCHMARK_KEYS; i += 1) {
        _key_build(key, sizeof(key), "192.168.", i);

        http_service_rate_limit_allowed_1(&rate_limit, key);
    }

    /* One WARM pass, discarded. A cold run pays for first-touch page faults across the record
     * array and an empty cache, none of which the lookup will pay again in a running server -
     * measuring it once and quoting the answer is how a figure lands in a header and then fails to
     * reproduce: four runs of an earlier single-shot form spread 365-472 us against a documented
     * 60-102. */
    for (USize i = 0; i < _BENCHMARK_ROUNDS; i += 1) {
        _key_build(key, sizeof(key), "172.16.", i);

        http_service_rate_limit_allowed_1(&rate_limit, key);
    }

    F64 samples[_BENCHMARK_SAMPLES] = DEFAULT_INITIALIZATION;

    /* The worst case a rate limiter has: every call presents a key the table has never seen, so
     * each one is a full scan, an eviction scan, and an insert. Five samples, reported as MIN and
     * MEDIAN: the min is the closest this machine gets to the cost with nothing else running, the
     * median is what a noisy machine actually delivers, and the two together say how much of any
     * single reading was the limiter and how much was the machine. */
    for (USize sample = 0; sample < _BENCHMARK_SAMPLES; sample += 1) {
        ChronoInstant const start = chrono_now();

        for (USize i = 0; i < _BENCHMARK_ROUNDS; i += 1) {
            _key_build(key, sizeof(key), "10.240.", sample * _BENCHMARK_ROUNDS + i);

            http_service_rate_limit_allowed_1(&rate_limit, key);
        }

        F64 const seconds = chrono_duration_seconds(chrono_elapsed(start));

        samples[sample] = seconds * 1000000.0 / (F64) _BENCHMARK_ROUNDS;
    }

    /* Insertion sort over five doubles: the median needs an order, and five elements do not earn
     * a dependency. */
    for (USize i = 1; i < _BENCHMARK_SAMPLES; i += 1) {
        F64 const value = samples[i];
        USize     j     = i;

        while (j > 0 && samples[j - 1] > value) {
            samples[j] = samples[j - 1];
            j -= 1;
        }

        samples[j] = value;
    }

    printf("    allowed_1, %d rotating keys against a full %d-record table, %d samples: min %.2f us/call, median %.2f us/call\n",
           _BENCHMARK_ROUNDS, _BENCHMARK_KEYS, _BENCHMARK_SAMPLES, samples[0], samples[_BENCHMARK_SAMPLES / 2]);

    test_expect_true(test, "the benchmark produced a positive median", samples[_BENCHMARK_SAMPLES / 2] > 0.0);

    test_expect_true(test, "the benchmark completed with the table still bounded", http_service_rate_limit_get_size(&rate_limit) <= HTTP_SERVICE_RATE_LIMIT_DEFAULT_CAPACITY);

    http_service_rate_limit_uninit(&rate_limit);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Live server
 *============================================================================*/

static HTTP_Service_Rate_Limit _live_rate_limit = DEFAULT_INITIALIZATION;

/* The reference shape for every consumer: resolve the client, ask check_2 once, and answer 429
 * with the Retry-After that came back from the SAME lock acquisition - a check_1/_2-then-query
 * pairing across two lock acquisitions can race a window refresh and send no Retry-After, or a
 * stale one, instead. */
static void _on_limited(HTTP_Server_Route *const route) {
    HTTP_Server_Holder *const holder = http_server_route_get_holder(route);

    char client[HTTP_SERVER_IP_MAX_LENGTH] = DEFAULT_INITIALIZATION;

    if (!http_server_request_get_client_ip(holder->request, 0, client, sizeof(client))) {
        http_server_response_send_empty(holder->response, HTTP_SERVER_STATUS_CODE_TOO_MANY_REQUESTS);

        return;
    }

    HTTP_Service_Rate_Limit_Result result = DEFAULT_INITIALIZATION;

    if (!http_service_rate_limit_check_2(&_live_rate_limit, "api", client, &result)) {
        char retry_after[24] = DEFAULT_INITIALIZATION;

        char_from_numbers_uint_1(retry_after, sizeof(retry_after), result.retry_after);

        http_server_response_header_add(holder->response, "Retry-After", retry_after);
        http_server_response_send_empty(holder->response, HTTP_SERVER_STATUS_CODE_TOO_MANY_REQUESTS);

        return;
    }

    http_server_response_send_1(holder->response, "ok", HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_OK);
}

static void _handle(void *const context, HTTP_Server_Request *const request, HTTP_Server_Response *const response) {
    HTTP_Server        *const server = (HTTP_Server*) context;
    HTTP_Server_Holder         holder = { .request = request, .response = response, .arena = nullptr };

    if (!http_server_router_dispatch_2(server->router, http_server_request_get_path_1(request), http_server_request_get_path_size(request), &holder)) {
        http_server_response_send_empty(response, HTTP_SERVER_STATUS_CODE_NOT_FOUND);
    }
}

/* One request/response round trip over a raw socket, so the status line and headers this suite
 * asserts on are the literal bytes the server wrote. */
static bool _client_round_trip(U16 const port, char *const reply, USize const capacity) {
    Net_Socket_Address address = DEFAULT_INITIALIZATION;
    Net_Socket         socket  = NET_SOCKET_INVALID;

    if (result_is_error(net_socket_address_init_2(NET_FAMILY_IPV4, port, "127.0.0.1", &address))
        || result_is_error(net_socket_init(&socket, NET_FAMILY_IPV4, NET_TYPE_TCP))) {
        return false;
    }

    if (result_is_error(net_socket_connect(socket, &address))) {
        net_socket_close(socket);

        return false;
    }

    net_socket_set_timeout(socket, _CLIENT_IO_TIMEOUT_MS);

    char const *const request = "GET /limited HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n";

    USize sent = 0;

    if (result_is_error(net_socket_send_1(socket, request, char_length(request), &sent))) {
        net_socket_close(socket);

        return false;
    }

    USize total = 0;

    while (total + 1 < capacity) {
        USize received = 0;

        if (result_is_error(net_socket_recv_1(socket, (Byte*) reply + total, capacity - total - 1, &received)) || received == 0) {
            break;
        }

        total += received;
    }

    reply[total] = '\0';

    net_socket_close(socket);

    return total > 0;
}

static void _test_rate_limit_live_429(Test *const test) {
    test_case_begin(test, "a live route answers 429 with Retry-After");

    test_expect_true(test, "the limiter initializes", http_service_rate_limit_init_2(&_live_rate_limit, 60, 60));
    test_expect_true(test, "the api rule registers", http_service_rate_limit_rule_add_1(&_live_rate_limit, "api", 1, 120) != USIZE_MAX);

    HTTP_Server server = DEFAULT_INITIALIZATION;

    test_expect_true(test, "http_server_init succeeds", http_server_init(&server));

    http_server_set_handler(&server, _handle, &server);

    test_expect_true(test, "the route registers", http_server_route_add(&server, "/limited", _on_limited));
    test_expect_true(test, "http_server_run succeeds on port 0", result_is_success(http_server_run(&server, 0, true)));

    U16 const port = http_server_get_port(&server);

    test_expect_true(test, "an ephemeral port was assigned", port != 0);

    char reply[_CLIENT_REPLY_MAX] = DEFAULT_INITIALIZATION;

    if (test_expect_true(test, "the first request round trips", _client_round_trip(port, reply, sizeof(reply)))) {
        test_expect_true(test, "and is answered 200", char_find_first_1(reply, "200") != CHAR_NPOS);
    }

    if (test_expect_true(test, "the second request round trips", _client_round_trip(port, reply, sizeof(reply)))) {
        test_expect_true(test, "and is refused 429", char_find_first_1(reply, "429") != CHAR_NPOS);
        test_expect_true(test, "carrying a Retry-After header", char_find_first_1(reply, "retry-after") != CHAR_NPOS || char_find_first_1(reply, "Retry-After") != CHAR_NPOS);
    }

    http_server_stop(&server);
    http_server_uninit(&server);

    http_service_rate_limit_uninit(&_live_rate_limit);

    test_case_end(test);
}

int main(void) {
    LogConfig const log_config = {
        .level             = LOG_LEVEL_ERROR,
        .stream            = stdout,
        .timestamp_enabled = true,
        .autoflush         = true
    };

    log_init(log_config);

    Test test = test_init("tests/http/service/rate_limit/test_all.c");

    test_verbose_set(&test, false);

    test_suite_begin(&test, "http_service_rate_limit");
    _test_rate_limit_allows_then_refuses(&test);
    _test_rate_limit_check_result(&test);
    _test_rate_limit_per_rule_budgets(&test);
    _test_rate_limit_unknown_rule_refused(&test);
    _test_rate_limit_key_refusals(&test);
    _test_rate_limit_null_key_refused(&test);
    _test_rate_limit_window_expiry(&test);
    _test_rate_limit_read_only_query_does_not_reanchor(&test);
    _test_rate_limit_eviction_spares_at_limit(&test);
    _test_rate_limit_overflow_bucket_bounds_saturation(&test);
    _test_rate_limit_overflow_bucket_bounds_the_read_only_queries(&test);
    _test_rate_limit_eviction_prefers_unblocked(&test);
    _test_rate_limit_record_cap_holds(&test);
    _test_rate_limit_hit_counts(&test);
    _test_rate_limit_reset_and_clear(&test);
    _test_rate_limit_rule_add_contract(&test);
    _test_rate_limit_init_refusals(&test);
    _test_rate_limit_normalized_text_keys(&test);
    _test_rate_limit_capacity_overflow_refused(&test);
    _test_rate_limit_arena_refusal(&test);
    _test_rate_limit_diagnostics_are_time_throttled(&test);
    _test_rate_limit_two_instances_are_independent(&test);
    _test_rate_limit_uninit_without_use(&test);
    _test_rate_limit_uninit_twice_is_noop(&test);
    _test_rate_limit_threads_total_exactly_the_limit(&test);
    _test_rate_limit_benchmark(&test);
    _test_rate_limit_live_429(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}