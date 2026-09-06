#include <stdio.h>

#include <http/service/ip_block/ip_block.h>
#include <net/net.h>
#include <test/test.h>

/* ip_block reaches production through main_traymon, main_crm and main.c. This suite pins the
 * defects fixed in this module's record-array/expiry rewrite: the 0-index sentinel
 * that struck the wrong client, the U8 length that let a long string walk past a block, the
 * entry cap that was not authoritative, registration now starting at ZERO strikes (add_strike
 * auto-registers instead), lazy block expiry, and remove/clear. */

static void _test_ip_block_strike_to_block(Test *const test) {
    test_case_begin(test, "ip_block strikes accumulate into a block");

    HTTP_Service_IP_Block ip_block = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init succeeds", http_service_ip_block_init_1(&ip_block, 3));

    char const *const ip = "203.0.113.7";

    test_expect_false(test, "unknown ip is not blocked", http_service_ip_block_blocked_1(&ip_block, ip));
    test_expect_false(test, "unknown ip is not tracked", http_service_ip_block_exists_1(&ip_block, ip));

    /* Registering no longer counts as a strike: a fresh add starts at 0. */
    test_expect_true(test, "add tracks the ip", http_service_ip_block_add_1(&ip_block, ip));

    test_expect_true(test, "ip is tracked after add", http_service_ip_block_exists_1(&ip_block, ip));
    test_expect_false(test, "zero strikes is below the limit", http_service_ip_block_blocked_1(&ip_block, ip));

    http_service_ip_block_add_strike_1(&ip_block, ip);

    test_expect_false(test, "one strike is still below the limit", http_service_ip_block_blocked_1(&ip_block, ip));

    http_service_ip_block_add_strike_1(&ip_block, ip);

    test_expect_false(test, "two strikes are still below the limit", http_service_ip_block_blocked_1(&ip_block, ip));

    http_service_ip_block_add_strike_1(&ip_block, ip);

    test_expect_true(test, "the third strike reaches the limit and blocks", http_service_ip_block_blocked_1(&ip_block, ip));

    /* A different address must be unaffected - this is what the 0-sentinel bug broke. */
    test_expect_false(test, "an unrelated ip is untouched", http_service_ip_block_blocked_1(&ip_block, "198.51.100.1"));

    http_service_ip_block_uninit(&ip_block);

    test_case_end(test);
}

static void _test_ip_block_add_strike_auto_registers(Test *const test) {
    test_case_begin(test, "add_strike registers an unseen ip instead of no-op'ing");

    HTTP_Service_IP_Block ip_block = DEFAULT_INITIALIZATION;

    http_service_ip_block_init_1(&ip_block, 2);

    test_expect_false(test, "the ip starts untracked", http_service_ip_block_exists_1(&ip_block, "203.0.113.20"));

    http_service_ip_block_add_strike_1(&ip_block, "203.0.113.20");

    test_expect_true(test, "the strike registered it", http_service_ip_block_exists_1(&ip_block, "203.0.113.20"));
    test_expect_false(test, "one strike is below a limit of two", http_service_ip_block_blocked_1(&ip_block, "203.0.113.20"));

    http_service_ip_block_add_strike_1(&ip_block, "203.0.113.20");

    test_expect_true(test, "the second strike blocks", http_service_ip_block_blocked_1(&ip_block, "203.0.113.20"));

    http_service_ip_block_uninit(&ip_block);

    test_case_end(test);
}

static void _test_ip_block_block_expires(Test *const test) {
    test_case_begin(test, "block_seconds == 0 never lifts; a real window lifts the block and resets the strike count");

    HTTP_Service_IP_Block never = DEFAULT_INITIALIZATION;

    /* block_seconds == 0 disables block expiry entirely (the pre-R1 permanent-block default). */
    http_service_ip_block_init_2(&never, 1, 0, 0);
    http_service_ip_block_add_strike_1(&never, "203.0.113.30");

    test_expect_true(test, "the ip is blocked", http_service_ip_block_blocked_1(&never, "203.0.113.30"));
    test_expect_true(test, "block_seconds == 0 never lifts the block", http_service_ip_block_blocked_1(&never, "203.0.113.30"));

    http_service_ip_block_uninit(&never);

    /*
     * The other half, which only a real elapsed window can show: limit 3, strike counts expire
     * after 1 second of quiet, blocks lift after 1 second. Expiry is monotonic (chrono_elapsed),
     * so this is the one case in the suite that has to actually wait - 1100 ms, comfortably past
     * the 1-second boundary without depending on timer granularity.
     */
    HTTP_Service_IP_Block lifting = DEFAULT_INITIALIZATION;

    http_service_ip_block_init_2(&lifting, 3, 1, 1);

    http_service_ip_block_add_strike_1(&lifting, "203.0.113.31");
    http_service_ip_block_add_strike_1(&lifting, "203.0.113.31");
    http_service_ip_block_add_strike_1(&lifting, "203.0.113.31");

    test_expect_true(test, "three strikes at a limit of three blocks", http_service_ip_block_blocked_1(&lifting, "203.0.113.31"));
    test_expect_true(test, "the strike count is 3", http_service_ip_block_get_strikes(&lifting, 0) == 3);
    test_expect_true(test, "get_blocked agrees with blocked_1", http_service_ip_block_get_blocked(&lifting, 0));

    thread_sleep(1100);

    test_expect_false(test, "the block LIFTS once block_seconds has elapsed", http_service_ip_block_blocked_1(&lifting, "203.0.113.31"));
    /* Strike-count expiration: the counter cools down after `expiration` seconds of quiet, so the
     * lifted address starts over rather than re-blocking on its very next strike. */
    test_expect_true(test, "and the strike count reset with it", http_service_ip_block_get_strikes(&lifting, 0) == 0);
    test_expect_false(test, "get_blocked reports the lifted state too", http_service_ip_block_get_blocked(&lifting, 0));

    http_service_ip_block_add_strike_1(&lifting, "203.0.113.31");

    test_expect_false(test, "one strike after the reset is below the limit again", http_service_ip_block_blocked_1(&lifting, "203.0.113.31"));

    http_service_ip_block_uninit(&lifting);

    test_case_end(test);
}

#ifdef ARENA_IMPLEMENTATION
static void _test_ip_block_starved_arena_refuses(Test *const test) {
    test_case_begin(test, "alloc_init on an arena too small for the record array answers false and zeroes *self");

    /* Deterministically starved: the record array alone is thousands of records wide, so an arena
     * of 64 bytes cannot possibly satisfy it. */
    Arena arena = arena_init_1(64, ARENA_TYPE_LINEAR);

    HTTP_Service_IP_Block ip_block = DEFAULT_INITIALIZATION;

    test_expect_false(test, "alloc_init_1 refuses a starved arena", http_service_ip_block_alloc_init_1(&ip_block, 5, &arena));
    /* ip_block.h Memory Management: a false return leaves *self ZEROED, not initialized - the
     * mutex was never constructed, so no other call (uninit included) may run on it. */
    test_expect_true(test, "the refused instance is left zeroed - capacity", ip_block.capacity == 0);
    test_expect_true(test, "the refused instance is left zeroed - count", ip_block.count == 0);
    test_expect_true(test, "the refused instance is left zeroed - limit", ip_block.limit == 0);
    test_expect_true(test, "the refused instance is left zeroed - records", ip_block.records == nullptr);

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}
#endif // ARENA_IMPLEMENTATION

static void _test_ip_block_remove_and_clear(Test *const test) {
    test_case_begin(test, "remove and clear drop tracked entries");

    HTTP_Service_IP_Block ip_block = DEFAULT_INITIALIZATION;

    http_service_ip_block_init_1(&ip_block, 1);

    http_service_ip_block_add_1(&ip_block, "203.0.113.40");
    http_service_ip_block_add_1(&ip_block, "203.0.113.41");

    test_expect_true(test, "size is 2 after two adds", http_service_ip_block_get_size(&ip_block) == 2);

    http_service_ip_block_remove_1(&ip_block, "203.0.113.40");

    test_expect_false(test, "the removed ip is gone", http_service_ip_block_exists_1(&ip_block, "203.0.113.40"));
    test_expect_true(test, "the other ip survives the remove", http_service_ip_block_exists_1(&ip_block, "203.0.113.41"));
    test_expect_true(test, "size dropped to 1", http_service_ip_block_get_size(&ip_block) == 1);

    /* Removing an untracked address is a no-op, not an error. */
    http_service_ip_block_remove_1(&ip_block, "203.0.113.99");

    test_expect_true(test, "removing an absent ip changes nothing", http_service_ip_block_get_size(&ip_block) == 1);

    http_service_ip_block_clear(&ip_block);

    test_expect_true(test, "clear empties the registry", http_service_ip_block_get_size(&ip_block) == 0);
    test_expect_false(test, "a cleared ip is untracked", http_service_ip_block_exists_1(&ip_block, "203.0.113.41"));

    /* The service must stay usable after a clear. */
    http_service_ip_block_add_1(&ip_block, "203.0.113.50");

    test_expect_true(test, "the service still tracks new ips after clear", http_service_ip_block_exists_1(&ip_block, "203.0.113.50"));

    http_service_ip_block_uninit(&ip_block);

    test_case_end(test);
}

static void _test_ip_block_index_accessors(Test *const test) {
    test_case_begin(test, "get_ip_copy/get_strikes/get_size read the record array");

    HTTP_Service_IP_Block ip_block = DEFAULT_INITIALIZATION;

    http_service_ip_block_init_1(&ip_block, 5);

    http_service_ip_block_add_1(&ip_block, "203.0.113.60");
    http_service_ip_block_add_strike_1(&ip_block, "203.0.113.60");
    http_service_ip_block_add_strike_1(&ip_block, "203.0.113.60");

    test_expect_true(test, "size is 1", http_service_ip_block_get_size(&ip_block) == 1);

    char ip_copy[64] = DEFAULT_INITIALIZATION;

    test_expect_true(test, "get_ip_copy copies the stored address",
                     http_service_ip_block_get_ip_copy(&ip_block, 0, ip_copy, sizeof(ip_copy)) && char_compare_equal_1(ip_copy, "203.0.113.60"));
    test_expect_true(test, "get_strikes reflects the two strikes", http_service_ip_block_get_strikes(&ip_block, 0) == 2);

    test_expect_false(test, "get_ip_copy is false past the end", http_service_ip_block_get_ip_copy(&ip_block, 1, ip_copy, sizeof(ip_copy)));
    test_expect_true(test, "get_strikes is 0 past the end", http_service_ip_block_get_strikes(&ip_block, 1) == 0);
    test_expect_false(test, "get_blocked is false past the end", http_service_ip_block_get_blocked(&ip_block, 1));

    /* get_record's one-lock snapshot must agree with the trio it replaces. */
    HTTP_Service_IP_Block_Record record = DEFAULT_INITIALIZATION;

    test_expect_true(test, "get_record snapshots index 0", http_service_ip_block_get_record(&ip_block, 0, &record));
    test_expect_true(test, "the snapshot's address matches get_ip_copy's", char_compare_equal_1(record.address, "203.0.113.60"));
    test_expect_true(test, "the snapshot's strikes match get_strikes'", record.strikes == 2);
    test_expect_false(test, "the snapshot's blocked matches get_blocked's", record.blocked);
    test_expect_false(test, "get_record is false past the end", http_service_ip_block_get_record(&ip_block, 1, &record));

    http_service_ip_block_uninit(&ip_block);

    test_case_end(test);
}

static void _test_ip_block_absent_sentinel(Test *const test) {
    test_case_begin(test, "ip_block reports absence as USIZE_MAX");

    HTTP_Service_IP_Block ip_block = DEFAULT_INITIALIZATION;

    http_service_ip_block_init_1(&ip_block, 2);

    /* On an EMPTY registry at_* used to answer 0 - a valid index - and reading strikes at 0
     * then aborted the process outright. USIZE_MAX is unambiguous and reachable safely. */
    test_expect_true(test, "absent on an empty registry is USIZE_MAX", http_service_ip_block_at_1(&ip_block, "203.0.113.9") == USIZE_MAX);

    http_service_ip_block_add_1(&ip_block, "203.0.113.1");

    test_expect_true(test, "the first entry really is index 0", http_service_ip_block_at_1(&ip_block, "203.0.113.1") == 0);
    test_expect_true(test, "a miss is still USIZE_MAX with entries present", http_service_ip_block_at_1(&ip_block, "203.0.113.2") == USIZE_MAX);

    /* Striking an untracked ip now REGISTERS it rather than no-op'ing - it must
     * not touch any OTHER entry while doing so. */
    http_service_ip_block_add_strike_1(&ip_block, "203.0.113.2");

    test_expect_false(test, "striking a new ip did not block entry 0", http_service_ip_block_blocked_1(&ip_block, "203.0.113.1"));
    test_expect_false(test, "and one strike alone did not block the new ip either", http_service_ip_block_blocked_1(&ip_block, "203.0.113.2"));

    http_service_ip_block_uninit(&ip_block);

    test_case_end(test);
}

static void _test_ip_block_long_ip_not_truncated(Test *const test) {
    test_case_begin(test, "ip_block does not truncate a long address within the key size");

    HTTP_Service_IP_Block ip_block = DEFAULT_INITIALIZATION;

    http_service_ip_block_init_1(&ip_block, 1);

    /* 63 bytes: the largest address the fixed key can hold (key size 64, NUL-terminated). */
    char long_ip[64] = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 63; i += 1) {
        long_ip[i] = 'a';
    }

    long_ip[63] = '\0';

    test_expect_true(test, "a 63-byte address is tracked", http_service_ip_block_add_2(&ip_block, long_ip, 63));
    test_expect_true(test, "and exists reports it", http_service_ip_block_exists_2(&ip_block, long_ip, 63));

    http_service_ip_block_add_strike_2(&ip_block, long_ip, 63);

    test_expect_true(test, "and it blocks once struck at a limit of one", http_service_ip_block_blocked_2(&ip_block, long_ip, 63));
    test_expect_true(test, "its index is a real one, not the absent marker", http_service_ip_block_at_2(&ip_block, long_ip, 63) != USIZE_MAX);

    http_service_ip_block_uninit(&ip_block);

    test_case_end(test);
}

static void _test_ip_block_oversize_ip_refused(Test *const test) {
    test_case_begin(test, "an address at or past the key size is refused, not truncated");

    HTTP_Service_IP_Block ip_block = DEFAULT_INITIALIZATION;

    http_service_ip_block_init_1(&ip_block, 1);

    char long_ip[65] = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 64; i += 1) {
        long_ip[i] = 'a';
    }

    long_ip[64] = '\0';

    test_expect_false(test, "a 64-byte address is declined", http_service_ip_block_add_2(&ip_block, long_ip, 64));
    test_expect_false(test, "it is not tracked", http_service_ip_block_exists_2(&ip_block, long_ip, 64));

    /* add_strike_2's own oversize refusal (its false answer means the striker is
     * NOT being counted) was unpinned even though add_2's was. */
    test_expect_false(test, "add_strike_2 on the same oversize address is declined too", http_service_ip_block_add_strike_2(&ip_block, long_ip, 64));

    http_service_ip_block_uninit(&ip_block);

    test_case_end(test);
}

static void _test_ip_block_empty_ip_is_untracked(Test *const test) {
    test_case_begin(test, "ip_block treats an empty address as untracked");

    HTTP_Service_IP_Block ip_block = DEFAULT_INITIALIZATION;

    http_service_ip_block_init_1(&ip_block, 1);

    /* An empty ip is network-derived and must never abort - that would take the whole server
     * down for one unresolvable client. */
    test_expect_false(test, "add_2 with an empty ip declines", http_service_ip_block_add_2(&ip_block, "", 0));

    test_expect_false(test, "empty address is not tracked", http_service_ip_block_exists_2(&ip_block, "", 0));
    test_expect_false(test, "empty address is not blocked", http_service_ip_block_blocked_2(&ip_block, "", 0));
    test_expect_true(test, "empty address reports absent", http_service_ip_block_at_2(&ip_block, "", 0) == USIZE_MAX);

    http_service_ip_block_add_strike_2(&ip_block, "", 0);

    test_expect_true(test, "striking an empty address is a no-op", http_service_ip_block_at_2(&ip_block, "", 0) == USIZE_MAX);

    http_service_ip_block_uninit(&ip_block);

    test_case_end(test);
}

/*
 * Formats the `suffix`-th address of 10.a.b.c into `ip` (>= 32 bytes). Shared by the eviction
 * pins below.
 *
 * A VALID IPv4 literal, deliberately: the old form was "10.0.<suffix>" counted up to 4999, and
 * "10.0.300" / "10.0.4999" are not IPv4 at all - they failed _http_service_ip_block_normalize
 * and were tracked as raw TEXT, so pins meant to exercise a full table of normalized keys were
 * exercising the malformed-address fallback instead. Three octets carry the
 * counter, so 0..16777215 are all distinct and all real.
 */
static void _ip_block_test_ip(char *const ip, USize const suffix) {
    snprintf(ip, 32, "10.%llu.%llu.%llu", (unsigned long long) ((suffix >> 16) & 0xFF), (unsigned long long) ((suffix >> 8) & 0xFF), (unsigned long long) (suffix & 0xFF));
}

static void _test_ip_block_eviction_prefers_unblocked(Test *const test) {
    test_case_begin(test, "eviction takes the oldest UNBLOCKED record out of a mixed table, never a live block");

    HTTP_Service_IP_Block ip_block = DEFAULT_INITIALIZATION;

    /* limit 2, so a single strike registers WITHOUT blocking - that is what makes a mixed table
     * possible at all: half the records blocked, half merely tracked. */
    http_service_ip_block_init_1(&ip_block, 2);

    char ip[32] = DEFAULT_INITIALIZATION;

    /* The first 2048 addresses are struck TWICE (blocked); the rest once (tracked, unblocked). */
    for (USize i = 0; i < 4096; i += 1) {
        _ip_block_test_ip(ip, i);

        http_service_ip_block_add_strike_1(&ip_block, ip);

        if (i < 2048) {
            http_service_ip_block_add_strike_1(&ip_block, ip);
        }
    }

    test_expect_true(test, "the table is exactly full", http_service_ip_block_get_size(&ip_block) == 4096);

    _ip_block_test_ip(ip, 0);

    test_expect_true(test, "the oldest record is blocked", http_service_ip_block_blocked_1(&ip_block, ip));

    _ip_block_test_ip(ip, 4096);

    http_service_ip_block_add_strike_1(&ip_block, ip);

    test_expect_true(test, "the newcomer is tracked", http_service_ip_block_exists_1(&ip_block, ip));

    /* The point of the tier: the OLDEST record overall is blocked, and it survives - what paid
     * for the room is the oldest record that is not under a live block. */
    _ip_block_test_ip(ip, 0);

    test_expect_true(test, "the oldest BLOCKED record survived", http_service_ip_block_exists_1(&ip_block, ip));

    _ip_block_test_ip(ip, 2048);

    test_expect_false(test, "the oldest UNBLOCKED record was evicted instead", http_service_ip_block_exists_1(&ip_block, ip));

    http_service_ip_block_uninit(&ip_block);

    test_case_end(test);
}

static void _test_ip_block_entry_cap_preserves_blocks(Test *const test) {
    test_case_begin(test, "a full table of blocked entries evicts the OLDEST to keep tracking new abusers, never refuses");

    HTTP_Service_IP_Block ip_block = DEFAULT_INITIALIZATION;

    /* limit 1: every tracked address blocks on its first strike, so the registry fills entirely
     * with blocked entries - the state where the "prefer unblocked" eviction tier has nothing to
     * evict and the last-resort "oldest blocked" tier takes over (see ip_block.h Performance
     * Characteristics). 5000 addresses against a 4096 capacity guarantees that tier runs. */
    http_service_ip_block_init_1(&ip_block, 1);

    char ip[32] = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 5000; i += 1) {
        _ip_block_test_ip(ip, i);

        http_service_ip_block_add_strike_1(&ip_block, ip);
    }

    /* Fail-closed for the CURRENT flood, never fail-open for a NEW one: the table never grows
     * past its cap, and the address that just triggered registration is tracked and blocked
     * rather than silently let through because the table happened to be full. */
    test_expect_true(test, "the table never grows past its cap", http_service_ip_block_get_size(&ip_block) == 4096);

    _ip_block_test_ip(ip, 4999);

    test_expect_true(test, "the newest address is still blocked", http_service_ip_block_blocked_1(&ip_block, ip));

    /* Accepted trade-off, pinned: the single OLDEST blocked address paid for that room and is no
     * longer tracked at all - not "unblocked", gone. Rotating back to this exact address would
     * start it over at zero strikes. */
    _ip_block_test_ip(ip, 0);

    test_expect_false(test, "the oldest blocked address was evicted to make room", http_service_ip_block_exists_1(&ip_block, ip));

    http_service_ip_block_uninit(&ip_block);

    test_case_end(test);
}

static void _test_ip_block_last_resort_eviction_is_exactly_one(Test *const test) {
    test_case_begin(test, "the last-resort eviction drops exactly the single oldest blocked record");

    HTTP_Service_IP_Block ip_block = DEFAULT_INITIALIZATION;

    http_service_ip_block_init_1(&ip_block, 1);

    char ip[32] = DEFAULT_INITIALIZATION;

    /* Fill the table to EXACTLY its documented capacity (ip_block.h Performance Characteristics:
     * _HTTP_SERVICE_IP_BLOCK_MAX_ENTRIES, currently 4096) with distinct blocked addresses, then
     * strike exactly one more. */
    for (USize i = 0; i < 4096; i += 1) {
        _ip_block_test_ip(ip, i);

        http_service_ip_block_add_strike_1(&ip_block, ip);
    }

    test_expect_true(test, "the table is exactly full", http_service_ip_block_get_size(&ip_block) == 4096);

    _ip_block_test_ip(ip, 4096);

    http_service_ip_block_add_strike_1(&ip_block, ip);

    test_expect_true(test, "size stays capped after the one-more strike", http_service_ip_block_get_size(&ip_block) == 4096);
    test_expect_true(test, "the new address was registered and blocked", http_service_ip_block_blocked_1(&ip_block, ip));

    _ip_block_test_ip(ip, 0);

    test_expect_false(test, "the single oldest address was evicted", http_service_ip_block_exists_1(&ip_block, ip));

    _ip_block_test_ip(ip, 1);

    test_expect_true(test, "the SECOND-oldest address survived - only one eviction happened", http_service_ip_block_blocked_1(&ip_block, ip));

    http_service_ip_block_uninit(&ip_block);

    test_case_end(test);
}

static void _test_ip_block_expired_block_evicts_before_live_visitor(Test *const test) {
    test_case_begin(test, "an expired block joins the unblocked eviction tier - it goes before a live visitor added after it, not after");

    HTTP_Service_IP_Block ip_block = DEFAULT_INITIALIZATION;

    /* limit 1 blocks every struck address on its first strike; block_seconds 1 is short enough
     * to expire inside the test after the sleep below. */
    http_service_ip_block_init_2(&ip_block, 1, 0, 1);

    char ip[32] = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 4095; i += 1) {
        _ip_block_test_ip(ip, i);

        http_service_ip_block_add_strike_1(&ip_block, ip);
    }

    /* The last slot: a LIVE visitor, registered but never struck - never blocked, and the
     * newest (highest-timestamp) record in the table. */
    _ip_block_test_ip(ip, 4095);

    http_service_ip_block_add_1(&ip_block, ip);

    test_expect_true(test, "the table is exactly full", http_service_ip_block_get_size(&ip_block) == 4096);

    thread_sleep(1100);

    _ip_block_test_ip(ip, 4096);

    http_service_ip_block_add_strike_1(&ip_block, ip);

    test_expect_true(test, "size stays capped after the one-more strike", http_service_ip_block_get_size(&ip_block) == 4096);

    _ip_block_test_ip(ip, 0);

    test_expect_false(test, "the oldest, now-EXPIRED block was evicted", http_service_ip_block_exists_1(&ip_block, ip));

    _ip_block_test_ip(ip, 4095);

    test_expect_true(test, "the live visitor - never struck, newest in the table - survived", http_service_ip_block_exists_1(&ip_block, ip));

    http_service_ip_block_uninit(&ip_block);

    test_case_end(test);
}

static void _test_ip_block_two_instances_are_independent(Test *const test) {
    test_case_begin(test, "ip_block instances are independent");

    HTTP_Service_IP_Block first  = DEFAULT_INITIALIZATION;
    HTTP_Service_IP_Block second = DEFAULT_INITIALIZATION;

    http_service_ip_block_init_1(&first, 1);
    http_service_ip_block_init_1(&second, 1);

    http_service_ip_block_add_strike_1(&first, "203.0.113.5");

    test_expect_true(test, "the first service blocked its address", http_service_ip_block_blocked_1(&first, "203.0.113.5"));
    test_expect_false(test, "the second service knows nothing of it", http_service_ip_block_exists_1(&second, "203.0.113.5"));

    /* Tearing the first down must leave the second fully operational. */
    http_service_ip_block_uninit(&first);

    http_service_ip_block_add_strike_1(&second, "198.51.100.9");

    test_expect_true(test, "the survivor still works after the other is destroyed", http_service_ip_block_blocked_1(&second, "198.51.100.9"));

    http_service_ip_block_uninit(&second);

    test_case_end(test);
}

static void _test_ip_block_empty_str_overloads(Test *const test) {
    test_case_begin(test, "ip_block survives an empty Str on every _3 overload");

    HTTP_Service_IP_Block ip_block = DEFAULT_INITIALIZATION;

    http_service_ip_block_init_1(&ip_block, 1);

    /* An EMPTY Str is not the same input as the "" literal the case above uses: "" is a valid
     * non-null pointer, whereas an empty Str carries data == nullptr. */
    Str empty = str_init_1();

    test_expect_false(test, "add_3 with an empty Str tracked nothing", http_service_ip_block_add_3(&ip_block, &empty));
    test_expect_false(test, "and exists_3 confirms it", http_service_ip_block_exists_3(&ip_block, &empty));
    test_expect_false(test, "blocked_3 with an empty Str answers false", http_service_ip_block_blocked_3(&ip_block, &empty));
    test_expect_true(test, "at_3 with an empty Str reports absent", http_service_ip_block_at_3(&ip_block, &empty) == USIZE_MAX);

    http_service_ip_block_add_strike_3(&ip_block, &empty);

    test_expect_true(test, "add_strike_3 with an empty Str is a no-op", http_service_ip_block_at_3(&ip_block, &empty) == USIZE_MAX);

    /* A real address must still work afterwards - the guard must reject the empty value, not
     * put the service into a refusing state. */
    http_service_ip_block_add_strike_1(&ip_block, "203.0.113.42");

    test_expect_true(test, "a real address still blocks after the empty ones", http_service_ip_block_blocked_1(&ip_block, "203.0.113.42"));

    str_uninit(&empty);

    http_service_ip_block_uninit(&ip_block);

    test_case_end(test);
}

static void _test_ip_block_add_is_idempotent(Test *const test) {
    test_case_begin(test, "ip_block add is idempotent");

    HTTP_Service_IP_Block ip_block = DEFAULT_INITIALIZATION;

    http_service_ip_block_init_1(&ip_block, 2);

    char const *const first  = "203.0.113.60";
    char const *const second = "203.0.113.61";

    /* Registering the same address repeatedly must create exactly ONE entry. */
    test_expect_true(test, "first add tracks it", http_service_ip_block_add_1(&ip_block, first));
    test_expect_true(test, "re-add still reports tracked", http_service_ip_block_add_1(&ip_block, first));
    test_expect_true(test, "a third re-add is still fine", http_service_ip_block_add_1(&ip_block, first));

    /* A second, distinct address must land at index 1 - which it can only do if the three calls
     * above produced one entry rather than three. This is the assertion that counts. */
    http_service_ip_block_add_1(&ip_block, second);

    test_expect_true(test, "the repeated address holds index 0", http_service_ip_block_at_1(&ip_block, first) == 0);
    test_expect_true(test, "the next address lands at index 1, so no duplicates were made", http_service_ip_block_at_1(&ip_block, second) == 1);

    /* And the surviving entry still behaves: a re-add must not have reset its strike counter. */
    http_service_ip_block_add_strike_1(&ip_block, first);
    http_service_ip_block_add_strike_1(&ip_block, first);

    test_expect_true(test, "the repeated address still blocks normally", http_service_ip_block_blocked_1(&ip_block, first));
    test_expect_false(test, "the other address is untouched", http_service_ip_block_blocked_1(&ip_block, second));

    http_service_ip_block_uninit(&ip_block);

    test_case_end(test);
}

static void _test_ip_block_ipv6_64_bucket_collides(Test *const test) {
    test_case_begin(test, "two IPv6 literals in the same /64 hit one entry");

    HTTP_Service_IP_Block ip_block = DEFAULT_INITIALIZATION;

    http_service_ip_block_init_1(&ip_block, 1);

    /* Same first 4 hextets (the /64 network-prefix), different low bits - the kind of rotation
     * any ISP hands a client within its own /64. */
    char const *const first  = "2001:db8:1234:5678::1";
    char const *const second = "2001:db8:1234:5678::2";

    http_service_ip_block_add_strike_1(&ip_block, first);

    test_expect_true(test, "striking the first address blocks it", http_service_ip_block_blocked_1(&ip_block, first));
    test_expect_true(test, "a second address in the same /64 reads as the same, already-blocked entry", http_service_ip_block_blocked_1(&ip_block, second));
    test_expect_true(test, "exactly one record was created, not two", http_service_ip_block_get_size(&ip_block) == 1);

    http_service_ip_block_uninit(&ip_block);

    test_case_end(test);
}

static void _test_ip_block_ipv6_different_64_does_not_collide(Test *const test) {
    test_case_begin(test, "a genuinely different /64 prefix does not collide");

    HTTP_Service_IP_Block ip_block = DEFAULT_INITIALIZATION;

    http_service_ip_block_init_1(&ip_block, 1);

    char const *const first  = "2001:db8:1234:5678::1";
    char const *const other  = "2001:db8:1234:9999::1"; // 4th hextet differs - a different /64

    /* Register both so a false collision would show up as one record instead of two, not just
     * as blocked_1 disagreeing (blocked_1 alone never registers an untracked address). */
    http_service_ip_block_add_1(&ip_block, first);
    http_service_ip_block_add_strike_1(&ip_block, other);

    test_expect_true(test, "the different /64 created its own record", http_service_ip_block_get_size(&ip_block) == 2);
    test_expect_false(test, "the first /64 is untouched by the other's strike", http_service_ip_block_blocked_1(&ip_block, first));
    test_expect_true(test, "the other /64 is blocked by its own strike", http_service_ip_block_blocked_1(&ip_block, other));

    http_service_ip_block_uninit(&ip_block);

    test_case_end(test);
}

static void _test_ip_block_ipv4_mapped_unifies_with_ipv4(Test *const test) {
    test_case_begin(test, "an IPv4-mapped IPv6 literal unifies with the plain IPv4 form");

    HTTP_Service_IP_Block ip_block = DEFAULT_INITIALIZATION;

    http_service_ip_block_init_1(&ip_block, 1);

    char const *const mapped = "::ffff:1.2.3.4";
    char const *const plain  = "1.2.3.4";

    http_service_ip_block_add_strike_1(&ip_block, mapped);

    test_expect_true(test, "the mapped literal blocks", http_service_ip_block_blocked_1(&ip_block, mapped));
    test_expect_true(test, "the plain IPv4 form reads as the same, already-blocked entry", http_service_ip_block_blocked_1(&ip_block, plain));
    test_expect_true(test, "exactly one record was created, not two", http_service_ip_block_get_size(&ip_block) == 1);

    http_service_ip_block_uninit(&ip_block);

    test_case_end(test);
}

static void _test_ip_block_malformed_address_falls_back_to_raw_compare(Test *const test) {
    test_case_begin(test, "a malformed address still tracks/strikes/blocks via the raw-compare fallback");

    HTTP_Service_IP_Block ip_block = DEFAULT_INITIALIZATION;

    http_service_ip_block_init_1(&ip_block, 1);

    /* Neither a valid IPv4 nor IPv6 literal - net_socket_address_init_2 refuses to parse it, so
     * this must fall back to the pre-existing opaque byte-string comparison rather than crash. */
    char const *const bad = "not-an-ip-address";

    test_expect_true(test, "add tracks the malformed text", http_service_ip_block_add_1(&ip_block, bad));
    test_expect_true(test, "and exists reports it", http_service_ip_block_exists_1(&ip_block, bad));

    http_service_ip_block_add_strike_1(&ip_block, bad);

    test_expect_true(test, "it blocks once struck at a limit of one", http_service_ip_block_blocked_1(&ip_block, bad));

    /* A different malformed string must never collide with it. */
    test_expect_false(test, "a different malformed address is unaffected", http_service_ip_block_blocked_1(&ip_block, "also-not-an-ip"));

    http_service_ip_block_uninit(&ip_block);

    test_case_end(test);
}

static void _test_ip_block_normalizer_is_net_socket_address_key_1(Test *const test) {
    test_case_begin(test, "tracking agrees with net_socket_address_key_1's own answer");

    /* The /64 bucketing and the IPv4-mapped unwrap are not this module's own code: the private
     * helper delegates to net_socket_address_key_1, so what an address IS has one definition.
     * This pins that the two halves still agree - whatever net says shares a key, ip_block
     * tracks as one entry, and whatever net refuses to normalize, ip_block tracks by raw text. */
    Byte first[NET_SOCKET_ADDRESS_KEY_SIZE]  = DEFAULT_INITIALIZATION;
    Byte second[NET_SOCKET_ADDRESS_KEY_SIZE] = DEFAULT_INITIALIZATION;

    USize const first_size  = net_socket_address_key_1("2001:db8::1", 11, first, sizeof(first));
    USize const second_size = net_socket_address_key_1("2001:db8::2", 11, second, sizeof(second));

    test_expect_true(test, "net answers one key for both literals",
        first_size == second_size && first_size > 0 && char_compare_equal_2((char const*) first, first_size, (char const*) second, second_size));

    HTTP_Service_IP_Block ip_block = DEFAULT_INITIALIZATION;

    http_service_ip_block_init_1(&ip_block, 1);

    test_expect_true(test, "adding the first literal", http_service_ip_block_add_1(&ip_block, "2001:db8::1"));
    test_expect_true(test, "the second literal is the same entry", http_service_ip_block_exists_1(&ip_block, "2001:db8::2"));
    test_expect_u(test, "so only one record exists", 1, http_service_ip_block_get_size(&ip_block));

    /* Text net cannot normalize (it answers 0) stays a distinct raw-text entry. */
    test_expect_u(test, "net refuses to normalize a bracketed literal", 0, net_socket_address_key_1("[2001:db8::1]", 13, first, sizeof(first)));
    test_expect_false(test, "so ip_block does not unify it with the plain form", http_service_ip_block_exists_1(&ip_block, "[2001:db8::1]"));

    http_service_ip_block_uninit(&ip_block);

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

    Test test = test_init("./test_all.c");

    test_verbose_set(&test, false);

    test_suite_begin(&test, "http_service_ip_block");
    _test_ip_block_strike_to_block(&test);
    _test_ip_block_add_strike_auto_registers(&test);
    _test_ip_block_block_expires(&test);
    _test_ip_block_eviction_prefers_unblocked(&test);
    _test_ip_block_remove_and_clear(&test);
    _test_ip_block_index_accessors(&test);
    _test_ip_block_absent_sentinel(&test);
    _test_ip_block_long_ip_not_truncated(&test);
    _test_ip_block_oversize_ip_refused(&test);
    _test_ip_block_empty_ip_is_untracked(&test);
    _test_ip_block_empty_str_overloads(&test);
    _test_ip_block_add_is_idempotent(&test);
    _test_ip_block_entry_cap_preserves_blocks(&test);
    _test_ip_block_last_resort_eviction_is_exactly_one(&test);
    _test_ip_block_expired_block_evicts_before_live_visitor(&test);
#ifdef ARENA_IMPLEMENTATION
    _test_ip_block_starved_arena_refuses(&test);
#endif // ARENA_IMPLEMENTATION
    _test_ip_block_two_instances_are_independent(&test);
    _test_ip_block_ipv6_64_bucket_collides(&test);
    _test_ip_block_ipv6_different_64_does_not_collide(&test);
    _test_ip_block_ipv4_mapped_unifies_with_ipv4(&test);
    _test_ip_block_malformed_address_falls_back_to_raw_compare(&test);
    _test_ip_block_normalizer_is_net_socket_address_key_1(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}