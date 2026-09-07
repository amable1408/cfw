/*
 * rate_limit.h - HTTP rate limiting service for the C Libraries Framework
 * @version 0.3.4
 *
 * Counts requests per (rule, key) over a fixed window and refuses the ones past the rule's
 * limit. Keys are caller-defined strings, so the same service throttles by IP, by user id, by
 * API token, or by anything else the caller can name.
 *
 * Algorithm:
 *   - FIXED WINDOW, lazily reset on read, anchored at the window's FIRST hit - not a sliding
 *     window and not a decaying bucket. A client can therefore spend its whole budget at the end
 *     of one window and its whole budget at the start of the next, bursting up to 2x limit
 *     across the boundary. This is stated because a caller once assumed decay.
 *   - MONOTONIC time (chrono), never the wall clock: an NTP correction or a VM resume moves the
 *     wall clock in either direction, and a forward jump of one cooldown would otherwise clear
 *     every counter in the table at once.
 *
 * Features:
 *   - A default rule (the instance's own limit/cooldown) plus named custom rules.
 *   - http_service_rate_limit_check_1/_2: allowed, limit, remaining and retry_after answered
 *     from ONE lock acquisition - what a 429 response needs to fill in Retry-After
 *     (HTTP_SERVER_STATUS_CODE_TOO_MANY_REQUESTS, http/server/http_server.h) without a second,
 *     racing query.
 *   - Per-record counters in one fixed-capacity array; the capacity is an init parameter.
 *   - Remaining-request, time-left, size, reset and clear queries.
 *
 * Counting contract (the one thing to get right at a call site):
 *   - allowed_* and check_* REGISTER a hit and then judge it. They read like a question and behave
 *     like a consuming one.
 *   - hit_* registers a hit and answers nothing.
 *   - blocked_* judges WITHOUT registering, and blocked_*, get_remaining_* and get_time_left_*
 *     write no rate-limiting state (the diagnostic log throttles still advance): an expired window
 *     is REPORTED as spent-and-due, never re-anchored at the query instant, so querying a limiter
 *     can neither loosen it nor keep an idle record from being reclaimed. Only the registering
 *     calls above open a new window.
 *   So: call allowed_* and check_* ALONE to count every request; call blocked_* first and hit_* only
 *   on the failures to count failures only; NEVER call allowed_* and then hit_* for the same
 *   request - that spends two units of a budget the caller believes it spent one of, halving
 *   every limit it configured.
 *
 * Usage Example:
 *   @code
 *   HTTP_Service_Rate_Limit limiter = DEFAULT_INITIALIZATION;
 *
 *   // 60 requests per 60 seconds by default, 8192 tracked (rule, key) records
 *   if (!http_service_rate_limit_init_2(&limiter, 60, 60)) {
 *       return; // Memory Management: false means this instance does not exist - fail startup.
 *   }
 *
 *   if (http_service_rate_limit_rule_add_1(&limiter, "login", 5, 300) == USIZE_MAX) {
 *       return; // the rule is NOT registered; requests naming it would be refused
 *   }
 *
 *   HTTP_Service_Rate_Limit_Result result = DEFAULT_INITIALIZATION;
 *
 *   if (!http_service_rate_limit_check_2(&limiter, "login", "203.0.113.7", &result)) {
 *       // answer 429 and send "Retry-After: <result.retry_after>"
 *   }
 *
 *   http_service_rate_limit_uninit(&limiter);
 *   @endcode
 *
 * Client address:
 *   - A key is OPAQUE here: this module never parses it, so an IP-keyed caller must pass an
 *     address it has already resolved AND normalized. Behind a reverse proxy the raw peer is the
 *     proxy, and throttling it throttles every visitor at once - resolve the client through the
 *     server layer's trusted-hop X-Forwarded-For walk first. Two spellings of one address
 *     ("1.2.3.4" and "::ffff:1.2.3.4"), and an IPv6 client rotating inside its own /64, are
 *     separate budgets unless the caller reduces them to one key with net_socket_address_key_2.
 *   - Use the TEXT form, net_socket_address_key_2 ("1.2.3.4", "2001:db8::/64"), not the byte form
 *     net_socket_address_key_1 that ip_block keys on: a key here is measured with char_length, so
 *     a byte key would be truncated at its first zero byte - and zero bytes are exactly what an
 *     IPv6 address is mostly made of, collapsing distinct clients onto one budget.
 *
 * Saturation, and why an attacker-chosen key makes it cheap:
 *   - The record table holds one record per (rule, key) up to its capacity, and a record AT its
 *     rule's limit is never evicted (see Performance Characteristics). Fill the table with
 *     at-limit records and no new key can be given a record at all.
 *   - Buying that state costs exactly as much as the key is expensive to invent, and a key is
 *     whatever the caller passes. Where the key is CLIENT-SUPPLIED - a registration rule keyed on
 *     the submitted email address, for instance - a single client sends capacity-many distinct
 *     values from one address and the table is saturated. The damage is not confined to that rule:
 *     saturation is a property of the shared record table, so the IP-keyed login rule loses its
 *     storage too. Prefer a key the CLIENT cannot choose freely (a normalized address), and size
 *     the capacity above the number of simultaneously active (rule, key) pairs.
 *   - What bounds the degraded state: one OVERFLOW BUCKET per rule - the rule's own limit over its
 *     own window, held beside the rule rather than in the record table - shared by every key that
 *     could not be given a record. A rule hands out at most `limit` such allowances per window in
 *     AGGREGATE instead of `limit` per unrecorded key, i.e. instead of no bound at all. Past the
 *     bucket the request is refused with a real Retry-After, and the condition is logged at most
 *     once per report window (not once per "episode": an edge-triggered flag was re-armed by every
 *     interleaved good request, which is a thing the caller chooses).
 *   - Why unrecorded keys are ALLOWED while the bucket has budget rather than refused outright -
 *     stated as the bound it is, not as "availability": the bucket concedes at most ONE extra
 *     client's budget per rule per window, a bounded trickle. Refusing every unrecorded key would
 *     instead let capacity-many attacker-chosen keys buy a TOTAL outage of every rule on the
 *     instance - strictly more damage for the same price. There is no fail-closed knob; ask for
 *     one if a deployment needs it.
 *   - THE BUCKET IS AN UNRECORDED KEY'S BUDGET ON EVERY PATH - the registering calls (allowed_*,
 *     check_*, hit_*) spend it, and the read-only ones (blocked_*, get_remaining_*,
 *     get_time_left_*) judge against it without spending it. So while a rule's bucket is spent
 *     inside a live window, a key with no record reads blocked, with 0 remaining and a real time
 *     left, exactly as check_* would answer it - which is what bounds the blocked_* + hit_*
 *     pairing this header recommends. The cost is that for the rest of one window after saturation
 *     clears, a fresh key still reads blocked although hit_* would now record it; it self-heals at
 *     the window's end, and erring toward refusal is the safe direction for a limiter.
 *
 * Error Handling:
 *   - Public functions validate non-null pointers, except `key`: a null key is refused like an
 *     empty one below, never aborted - it is reachable from an ordinary allocation failure
 *     upstream (e.g. a refused string_alloc_init), not only a programming error.
 *   - An UNKNOWN rule name is refused, not silently served by the default rule: a typo'd rule
 *     constant would otherwise turn a 5-per-300s login limit into the instance default with no
 *     diagnostic at the enforcement point. allowed_* and check_* answer false, blocked_* answers
 *     true, and the condition is logged at most once per report window - throttled by time, not by
 *     an edge, since an edge is re-armed by every interleaved good request. The refused result
 *     carries limit 0 (the rule does not exist, rather than "a limit of zero") with the INSTANCE's
 *     cooldown as its retry_after. Register every rule at startup and check rule_add's answer.
 *   - An EMPTY OR NULL key is refused the same way (fail-CLOSED). Every client whose address could not
 *     be resolved would otherwise share one budget, so one forged header could exhaust it for
 *     all of them; and an unidentifiable caller must not be the one caller that gets a free
 *     pass. A key at or past HTTP_SERVICE_RATE_LIMIT_KEY_SIZE is refused identically - hash a
 *     long token to a short key before calling rather than having it truncated into a collision.
 *   - Rule and instance limit/cooldown values must be non-zero, and this is CHECKED, not assumed:
 *     init/alloc_init answer false and rule_add answers USIZE_MAX. A zero cooldown makes every
 *     read reset the window (the limiter silently off) and a zero limit refuses everything.
 *   - A capacity past USIZE_MAX / sizeof(record) is refused for the same reason and by the same
 *     answer: the byte count would wrap, a far smaller block would be allocated, and the table
 *     would then be indexed to the capacity that was asked for - a heap overflow bought with one
 *     large number.
 *   - None of the refusals above is an error_check_* abort. Each is a decision about DATA, and a
 *     data-dependent decision is never an abort primitive: they hold identically whether
 *     ERROR_CHECK_ENABLED is defined or not (tests/http/service/rate_limit/test_unchecked.c).
 *
 * Thread Safety:
 *   - Every public operation, query included, takes this instance's own mutex, and each answers
 *     from a SINGLE acquisition - check_* exists precisely so a refusal and its Retry-After
 *     cannot be read either side of another thread's window refresh.
 *   - Instances are independent: each owns its lock, so destroying one does not affect another.
 *     The lock lives in the struct, which is why the constructors initialize in place instead of
 *     returning the service by value.
 *   - http_service_rate_limit_uninit is the one exception: it does NOT take the lock, because it
 *     destroys it. The caller must have quiesced every other user of the instance first - the
 *     usual shape is that the service outlives every request thread and is torn down after the
 *     server has stopped.
 *
 * Memory Management:
 *   - Two allocations at init - the record array and the rule table - and NOTHING per entry.
 *     Keys and rule names are fixed inline buffers, so an arena-backed instance (alloc_init_*)
 *     genuinely uses its allocator: there is no per-record allocation left for an arena to fail
 *     to free when a record is evicted.
 *   - Call http_service_rate_limit_uninit() when finished.
 *   - A false return from any init/alloc_init leaves *self ZEROED, not initialized - the mutex
 *     was never successfully constructed, only its storage cleared. Do not call ANY other
 *     function on that instance afterward. uninit is the one exception, and only because it is
 *     harmless: it no-ops on a zeroed instance, so calling it is unnecessary rather than unsafe -
 *     a cleanup path that runs it unconditionally is fine. Treat a false return as "this instance
 *     does not exist" and fail startup.
 *
 * Performance Characteristics:
 *   - Lookups are linear over the tracked records, up to the capacity given at init
 *     (HTTP_SERVICE_RATE_LIMIT_DEFAULT_CAPACITY when unspecified). In the WORST case a limiter
 *     has - the table full at 8192 records and every call presenting a key it has never seen, so
 *     each one is a full lookup scan, a full eviction scan and an insert - one allowed_1 call
 *     costs HUNDREDS OF MICROSECONDS, and roughly a TENTH of what the four-parallel-list design it
 *     replaced cost on the same machine and the same input. Only the order of magnitude and the
 *     ratio are quoted deliberately: repeated runs of the same case ranged roughly 60-500 us purely
 *     on machine state, so a single absolute figure would be noise dressed as a specification. The
 *     LIVE source is the suite's own benchmark case (tests/http/service/rate_limit/test_all.c,
 *     "allowed_1 cost with a full table and rotating keys"), which warms one pass and reports the
 *     min and median of five - run it on the machine that matters rather than trusting a number
 *     printed here.
 *   - That per-call cost is also the INSTANCE's throughput ceiling, because the scan runs under
 *     the single per-instance lock: at ~400 us per saturated call one limiter admits roughly 2.5k
 *     calls/s no matter how many cores are asking. A realistic table - hundreds of active clients,
 *     most calls HITTING an existing record - is an order of magnitude better on both figures. The
 *     deferred hashset backing and a per-rule quota are what would lift the ceiling itself; both
 *     wait for a real deployment profile that disagrees with the above.
 *   - The expiry sweep runs at most once per second, not once per insert; between sweeps the cap
 *     and its eviction hold the table's size.
 *   - Eviction trade-off: when the table is full, a new key evicts the oldest record that is
 *     EXPIRED, else the oldest IDLE (count 0) record, else the oldest record still under its
 *     limit. A record at or past its rule's limit is never evicted - discarding one would
 *     recreate it at count 0 and hand exactly the client being throttled a fresh budget, so a
 *     key-rotating attacker could flush their own limiter. When every record is at its limit the
 *     service degrades: unrecorded keys share the rule's overflow bucket, are allowed while it has
 *     budget and refused past it, and the episode is logged at most once per report window. See
 *     "Saturation" above for what that bounds and what it deliberately does not.
 *
 * Dependencies:
 *   - allocator, arena, char, chrono, thread.
 *
 * See rate_limit.c for implementation details.
 */

#ifndef HTTP_SERVICE_RATE_LIMIT_H
#define HTTP_SERVICE_RATE_LIMIT_H

#include <arena/arena.h>
#include <chrono/chrono.h>
#include <thread/thread.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

/** @brief Record slots allocated when a constructor is not given a capacity. */
#define HTTP_SERVICE_RATE_LIMIT_DEFAULT_CAPACITY 8192
/** @brief Default rule cooldown in seconds. */
#define HTTP_SERVICE_RATE_LIMIT_DEFAULT_COOLDOWN 60
/** @brief Default rule hit limit per cooldown. */
#define HTTP_SERVICE_RATE_LIMIT_DEFAULT_LIMIT 60
/** @brief Key buffer size, NUL included. A key at or past this length is refused, never truncated. */
#define HTTP_SERVICE_RATE_LIMIT_KEY_SIZE 128
/** @brief Number of named rules an instance can hold, beyond the default rule. */
#define HTTP_SERVICE_RATE_LIMIT_RULE_CAPACITY 32
/**
 * @brief The reserved rule name every _1 overload uses.
 *
 * Passing it, or a null rule pointer, selects the INSTANCE's own limit/cooldown rather than a
 * registered rule - so it is never "unknown" and never refused. Registering a rule under this
 * name is refused, since it could not then be told apart from the instance default.
 */
#define HTTP_SERVICE_RATE_LIMIT_RULE_DEFAULT "default"
/** @brief Rule name buffer size, NUL included. */
#define HTTP_SERVICE_RATE_LIMIT_RULE_NAME_SIZE 64

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief Everything a 429 response needs about one request, read under a single lock by
 *        http_service_rate_limit_check_1/_2.
 *
 * The trio allowed_2 + get_remaining_2 + get_time_left_2 answered the same three questions from
 * three separate lock acquisitions, during which another thread's window refresh could zero the
 * counter - so a request that had just been refused could report retry_after 0.
 */
typedef struct {
    /** @brief true when the hit this call registered is within the rule's limit. */
    bool allowed;
    /** @brief The rule's limit, for a RateLimit-Limit header. */
    USize limit;
    /** @brief Hits left in the current window after this one; 0 once the limit is reached. */
    USize remaining;
    /**
     * @brief Seconds until the window refreshes, for a Retry-After header. Zero when allowed,
     *        and never zero when refused - a refusal telling a client to retry immediately is
     *        worse than no header at all, so this is clamped to at least one second.
     */
    USize retry_after;
} HTTP_Service_Rate_Limit_Result;

/**
 * @brief In-memory fixed-window rate limiter.
 */
typedef struct {
#ifdef ARENA_IMPLEMENTATION
    /** @brief Optional arena backing the two allocations below. */
    Arena *allocator;
#endif // ARENA_IMPLEMENTATION
    /** @brief Number of record slots allocated at init - the ceiling `count` can reach. */
    USize capacity;
    /** @brief Default rule cooldown in seconds. */
    USize cooldown;
    /** @brief Number of record slots currently in use. */
    USize count;
    /**
     * @brief When the degraded (table-saturated) path was last reported; 0 when never.
     *
     * The degradation is per REQUEST, so an unthrottled diagnostic on that path lets a caller
     * drive log volume at their own request rate - and the condition is one an attacker can help
     * sustain by rotating keys. Throttled by TIME, not edge-triggered: the bool flag this replaces
     * cleared on the next SUCCESS, so traffic alternating one degrading key with one ordinary key
     * - a pattern the caller chooses freely - re-armed the flag on every ordinary request and
     * logged the other one every time. Only the passage of time re-arms a timestamp.
     */
    ChronoInstant degraded_reported_at;
    /** @brief When an empty or oversize key was last reported; 0 when never. Time-throttled as above. */
    ChronoInstant key_reported_at;
    /** @brief When the expiry sweep last ran. The sweep is throttled to once per second. */
    ChronoInstant last_sweep;
    /** @brief Default rule hit limit per cooldown. */
    USize limit;
    /**
     * @brief Guards every access to `records`/`count`/`rules`.
     *
     * Per instance, not file-static. One shared lock meant uninit of ANY instance destroyed the
     * lock every other instance still used, and each init re-initialized a possibly-locked
     * mutex - undefined on CRITICAL_SECTION and pthreads alike.
     */
    ThreadMutex mutex;
    /**
     * @brief Hits registered in the DEFAULT rule's overflow bucket during its current window.
     *
     * The aggregate budget shared by every key the saturated record table could not give a record
     * of its own; see "Saturation" above. Each named rule carries its own bucket inside the rule
     * table, so one rule's overflow can never spend another's.
     */
    USize overflow_count;
    /** @brief Instant the default rule's overflow window opened - its FIRST unrecorded hit. */
    ChronoInstant overflow_timestamp;
    /** @brief Fixed-capacity record array, allocated once at init. Opaque to callers. */
    void *records;
    /** @brief Number of registered named rules. */
    USize rule_count;
    /** @brief Fixed-capacity rule table, allocated once at init. Opaque to callers. */
    void *rules;
    /** @brief When an unknown rule name was last reported; 0 when never. Time-throttled as above. */
    ChronoInstant unknown_rule_reported_at;
} HTTP_Service_Rate_Limit;

/*==============================================================================
 * MARK: - API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Initialize an arena-backed limiter using the default limit, cooldown and capacity.
 * @param self Service to initialize IN PLACE.
 * @param allocator Arena allocator, genuinely backing the record array and rule table.
 * @return true when initialized; false when the arena refused an allocation or the mutex could
 *         not be initialized - in both cases *self is ZEROED and must NOT be used again (see
 *         rate_limit.h Memory Management; passing it to uninit is harmless but unnecessary, since
 *         uninit no-ops on a zeroed instance); the caller should fail startup rather than run with
 *         no limiter storage or no locking.
 * @note Writes through self rather than returning the service, because it owns a ThreadMutex and
 *       neither CRITICAL_SECTION nor pthread_mutex_t may be copied once initialized - a by-value
 *       return would hand the caller a copy of an initialized lock.
 */
bool http_service_rate_limit_alloc_init_1(HTTP_Service_Rate_Limit *const self, Arena *const allocator);

/**
 * @brief Initialize an arena-backed limiter with custom default rule values.
 * @param self Service to initialize IN PLACE.
 * @param limit Default allowed hits per cooldown; zero is refused.
 * @param cooldown Default cooldown in seconds; zero is refused.
 * @param allocator Arena allocator.
 * @return true when initialized; see http_service_rate_limit_alloc_init_1 for the false case,
 *         which now also covers a zero limit or cooldown.
 * @note Writes through self; see http_service_rate_limit_alloc_init_1 for why.
 */
bool http_service_rate_limit_alloc_init_2(HTTP_Service_Rate_Limit *const self, USize const limit, USize const cooldown, Arena *const allocator);

/**
 * @brief Initialize an arena-backed limiter with custom default rule values and capacity.
 * @param self Service to initialize IN PLACE.
 * @param limit Default allowed hits per cooldown; zero is refused.
 * @param cooldown Default cooldown in seconds; zero is refused.
 * @param capacity Tracked (rule, key) record slots; zero is refused, and so is a value past
 *        USIZE_MAX / sizeof(record), whose byte count would wrap. Size it above the number of
 *        simultaneously active clients TIMES the number of rules they exercise - past the cap,
 *        eviction is what bounds the table, and it is a backstop, not a routine path.
 * @param allocator Arena allocator.
 * @return true when initialized; see http_service_rate_limit_alloc_init_1 for the false case.
 * @note Writes through self; see http_service_rate_limit_alloc_init_1 for why.
 */
bool http_service_rate_limit_alloc_init_3(HTTP_Service_Rate_Limit *const self, USize const limit, USize const cooldown, USize const capacity, Arena *const allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Register a hit for the default rule and judge it.
 * @param self Service instance.
 * @param key Caller-defined key.
 * @return true when the hit is within the limit. REGISTERS the hit - see the counting contract
 *         in rate_limit.h; never pair this with hit_* for the same request.
 */
bool http_service_rate_limit_allowed_1(HTTP_Service_Rate_Limit *const self, char const *const key);

/**
 * @brief Register a hit for a named rule and judge it.
 * @param self Service instance.
 * @param rule Rule name; null or HTTP_SERVICE_RATE_LIMIT_RULE_DEFAULT selects the instance
 *        default. An unregistered name is REFUSED, not defaulted.
 * @param key Caller-defined key.
 * @return true when the hit is within the limit; false when refused, including for an unknown
 *         rule, an empty key, an oversize key, and a key that the saturated record table could
 *         not track once the rule's overflow bucket is spent. REGISTERS the hit - see the
 *         counting contract.
 */
bool http_service_rate_limit_allowed_2(HTTP_Service_Rate_Limit *const self, char const *const rule, char const *const key);

/**
 * @brief Check whether a key has reached the default rule's limit, WITHOUT registering a hit.
 * @param self Service instance.
 * @param key Caller-defined key.
 * @return true when the key has reached the current window's limit.
 */
bool http_service_rate_limit_blocked_1(HTTP_Service_Rate_Limit *const self, char const *const key);

/**
 * @brief Check whether a key has reached a named rule's limit, WITHOUT registering a hit.
 * @param self Service instance.
 * @param rule Rule name; null or HTTP_SERVICE_RATE_LIMIT_RULE_DEFAULT selects the instance default.
 * @param key Caller-defined key; null is refused like an empty key.
 * @return true when the key has reached the current window's limit. Also true - fail CLOSED -
 *         for an unknown rule, an empty key or an oversize key, matching allowed_*'s refusal; and
 *         true for a key with NO record while the rule's overflow bucket is spent inside a live
 *         window, since that bucket is an unrecorded key's budget (see rate_limit.h Saturation).
 */
bool http_service_rate_limit_blocked_2(HTTP_Service_Rate_Limit *const self, char const *const rule, char const *const key);

/**
 * @brief Register a hit for the default rule and answer everything a 429 needs about it.
 * @param self Service instance.
 * @param key Caller-defined key.
 * @param out Receives allowed/limit/remaining/retry_after; may be null to ignore the detail.
 * @return out->allowed, so the call reads as a condition. REGISTERS the hit.
 */
bool http_service_rate_limit_check_1(HTTP_Service_Rate_Limit *const self, char const *const key, HTTP_Service_Rate_Limit_Result *const out);

/**
 * @brief Register a hit for a named rule and answer everything a 429 needs about it.
 *
 * The one-lock form of allowed_2 + get_remaining_2 + get_time_left_2, which allowed_2 is now a
 * thin wrapper over. Use it at any site that answers HTTP_SERVER_STATUS_CODE_TOO_MANY_REQUESTS:
 * out->retry_after is the Retry-After value for exactly the refusal this call made.
 *
 * @param self Service instance.
 * @param rule Rule name; null or HTTP_SERVICE_RATE_LIMIT_RULE_DEFAULT selects the instance default.
 * @param key Caller-defined key; null is refused like an empty key.
 * @param out Receives allowed/limit/remaining/retry_after; may be null to ignore the detail. On
 *        every refusal path - unknown rule, empty key, oversize key, a spent overflow bucket - it
 *        is filled with allowed=false, remaining=0 and a non-zero retry_after, so a caller that
 *        only reads `out` still answers a well-formed 429.
 * @return out->allowed. REGISTERS the hit - see the counting contract in rate_limit.h.
 */
bool http_service_rate_limit_check_2(HTTP_Service_Rate_Limit *const self, char const *const rule, char const *const key, HTTP_Service_Rate_Limit_Result *const out);

/**
 * @brief Forget every tracked record and every overflow bucket, keeping the registered rules.
 * @param self Service instance.
 * @note Every key starts a fresh window afterwards, the degraded path's aggregate budgets
 *       included. Intended for administrative reset and for tests that need a deterministic
 *       table, not for the request path.
 */
void http_service_rate_limit_clear(HTTP_Service_Rate_Limit *const self);

/**
 * @brief Get hits left for a key under the default rule.
 * @param self Service instance.
 * @param key Caller-defined key.
 * @return Hits left in the current window; the full limit for an untracked key.
 */
USize http_service_rate_limit_get_remaining_1(HTTP_Service_Rate_Limit *const self, char const *const key);

/**
 * @brief Get hits left for a key under a named rule.
 * @param self Service instance.
 * @param rule Rule name; null or HTTP_SERVICE_RATE_LIMIT_RULE_DEFAULT selects the instance default.
 * @param key Caller-defined key; null is refused like an empty key.
 * @return Hits left in the current window; the full limit for an untracked key while its rule's
 *         overflow bucket is unused. Once that bucket has taken any hits inside a live window, an
 *         untracked key's real budget is whatever the bucket has left - down to 0 once it is fully
 *         spent (see rate_limit.h Saturation) - never the full limit, which would overstate a
 *         partially spent bucket as untouched. 0 for an unknown rule or an unusable key, matching
 *         the refusal allowed_* would make.
 */
USize http_service_rate_limit_get_remaining_2(HTTP_Service_Rate_Limit *const self, char const *const rule, char const *const key);

/**
 * @brief Get the number of tracked (rule, key) records.
 * @param self Service instance.
 * @return Records in use, never more than the capacity given at init.
 */
USize http_service_rate_limit_get_size(HTTP_Service_Rate_Limit *const self);

/**
 * @brief Get the seconds left before a blocked key's default-rule window refreshes.
 * @param self Service instance.
 * @param key Caller-defined key.
 * @return Seconds until the window refreshes, or 0 when the key is not blocked.
 */
USize http_service_rate_limit_get_time_left_1(HTTP_Service_Rate_Limit *const self, char const *const key);

/**
 * @brief Get the seconds left before a blocked key's named-rule window refreshes.
 * @param self Service instance.
 * @param rule Rule name; null or HTTP_SERVICE_RATE_LIMIT_RULE_DEFAULT selects the instance default.
 * @param key Caller-defined key; null is refused like an empty key.
 * @return Seconds until the window refreshes, or 0 when the key is not blocked. For a key with
 *         NO record refused by the rule's spent overflow bucket, the seconds left on THAT bucket's
 *         window - the same wait check_2 reports as its retry_after. Prefer
 *         http_service_rate_limit_check_2, which answers this together with the verdict it
 *         belongs to instead of in a second, racing lock acquisition.
 */
USize http_service_rate_limit_get_time_left_2(HTTP_Service_Rate_Limit *const self, char const *const rule, char const *const key);

/**
 * @brief Register a hit for the default rule without judging it.
 * @param self Service instance.
 * @param key Caller-defined key.
 */
void http_service_rate_limit_hit_1(HTTP_Service_Rate_Limit *const self, char const *const key);

/**
 * @brief Register a hit for a named rule without judging it.
 * @param self Service instance.
 * @param rule Rule name; null or HTTP_SERVICE_RATE_LIMIT_RULE_DEFAULT selects the instance default.
 * @param key Caller-defined key; null is refused like an empty key.
 * @note Pair this with blocked_* to count only failures; never with allowed_* and check_*, which
 *       count the request themselves.
 */
void http_service_rate_limit_hit_2(HTTP_Service_Rate_Limit *const self, char const *const rule, char const *const key);

/**
 * @brief Initialize a limiter using the default limit, cooldown and capacity.
 * @param self Service to initialize IN PLACE.
 * @return true when initialized; false when an allocation failed or the mutex could not be
 *         initialized - *self is then ZEROED and must NOT be used again; uninit on it is harmless
 *         but unnecessary.
 * @note Writes through self; see http_service_rate_limit_alloc_init_1 for why.
 */
bool http_service_rate_limit_init_1(HTTP_Service_Rate_Limit *const self);

/**
 * @brief Initialize a limiter with custom default rule values.
 * @param self Service to initialize IN PLACE.
 * @param limit Default allowed hits per cooldown; zero is refused.
 * @param cooldown Default cooldown in seconds; zero is refused.
 * @return true when initialized; see http_service_rate_limit_init_1 for the false case.
 * @note Writes through self; see http_service_rate_limit_alloc_init_1 for why.
 */
bool http_service_rate_limit_init_2(HTTP_Service_Rate_Limit *const self, USize const limit, USize const cooldown);

/**
 * @brief Initialize a limiter with custom default rule values and capacity.
 * @param self Service to initialize IN PLACE.
 * @param limit Default allowed hits per cooldown; zero is refused.
 * @param cooldown Default cooldown in seconds; zero is refused.
 * @param capacity Tracked (rule, key) record slots; zero is refused. See
 *        http_service_rate_limit_alloc_init_3 for how to size it.
 * @return true when initialized; see http_service_rate_limit_init_1 for the false case.
 * @note Writes through self; see http_service_rate_limit_alloc_init_1 for why.
 */
bool http_service_rate_limit_init_3(HTTP_Service_Rate_Limit *const self, USize const limit, USize const cooldown, USize const capacity);

/**
 * @brief Forget a key's record under the default rule.
 * @param self Service instance.
 * @param key Caller-defined key.
 */
void http_service_rate_limit_reset_1(HTTP_Service_Rate_Limit *const self, char const *const key);

/**
 * @brief Forget a key's record under a named rule.
 * @param self Service instance.
 * @param rule Rule name; null or HTTP_SERVICE_RATE_LIMIT_RULE_DEFAULT selects the instance default.
 * @param key Caller-defined key; null is refused like an empty key.
 * @note The record is REMOVED, not merely zeroed, so it stops occupying a capacity slot. The
 *       key's next hit opens a fresh window, which is what zeroing did too.
 * @note It cannot clear the rule's OVERFLOW bucket, which is per rule and not per key (see
 *       Saturation): while that bucket is spent, the reset key is still refused as an unrecorded
 *       one until the bucket's window turns over. Only http_service_rate_limit_clear empties it.
 */
void http_service_rate_limit_reset_2(HTTP_Service_Rate_Limit *const self, char const *const rule, char const *const key);

/**
 * @brief Register a named rule, or update one already registered.
 * @param self Service instance.
 * @param name Rule name; must be non-empty, shorter than HTTP_SERVICE_RATE_LIMIT_RULE_NAME_SIZE,
 *        and not HTTP_SERVICE_RATE_LIMIT_RULE_DEFAULT.
 * @param limit Allowed hits per cooldown; zero is refused.
 * @param cooldown Cooldown in seconds; zero is refused.
 * @return A SUCCESS TOKEN, and incidentally the rule's stable index - stable for the instance's
 *         life, since rules are never removed - or USIZE_MAX when the rule was NOT registered (a
 *         bad name or value, or the rule table already full). Check it at startup: an unregistered
 *         rule is not silently replaced by the instance default, it is refused at every enforcement
 *         point, so a server that ignores this answer refuses every request naming that rule.
 * @note NO function takes this handle today - every enforcement call resolves the rule by NAME, and
 *       nothing is lost by that: resolution costs at most HTTP_SERVICE_RATE_LIMIT_RULE_CAPACITY
 *       short compares, which the record scan beside it dwarfs. Compare it against USIZE_MAX and
 *       discard it. A handle-taking overload will exist when a consumer MEASURES the resolve and
 *       finds it matters, not before.
 */
USize http_service_rate_limit_rule_add_1(HTTP_Service_Rate_Limit *const self, char const *const name, USize const limit, USize const cooldown);

/**
 * @brief Release all service storage.
 * @param self Service instance.
 * @note Does NOT take the instance lock - it destroys it. Quiesce every other user first; see
 *       rate_limit.h Thread Safety.
 * @note A second call on an already-uninitialized instance is a harmless no-op - it does not
 *       destroy the mutex twice.
 */
void http_service_rate_limit_uninit(HTTP_Service_Rate_Limit *const self);

#endif // HTTP_SERVICE_RATE_LIMIT_H