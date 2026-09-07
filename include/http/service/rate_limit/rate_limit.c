/*==============================================================================
 * MARK: - Includes
 *============================================================================*/

#include <http/service/rate_limit/rate_limit.h>

/* IMPLEMENTATION dependencies - rate_limit.h's API names no type of either, though the module
 * still depends on both, see the header's Dependencies - so they are included here rather than
 * chained through the header. The header names Arena, so it carries arena itself. */
#include <allocator/allocator.h>
#include <char/char.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

/* A diagnostic from one of the degraded paths - an unknown rule, an unusable key, a saturated
 * record table - is emitted at most once per this many seconds. All three run ON the request
 * path, so an unthrottled diagnostic hands the caller control of log volume; and all three are
 * conditions a caller can produce deliberately. Time, not an edge: the bool flags this replaced
 * cleared on the next SUCCESS, so alternating one bad request with one good one re-armed the flag
 * every other call and logged every bad one. */
#define _HTTP_SERVICE_RATE_LIMIT_REPORT_SECONDS 60

/* The expiry sweep is throttled to once per this many seconds instead of running on every
 * insert. A sweep is O(records), and under the key-rotating traffic that fills the table every
 * request is an insert - so an unthrottled sweep turned the table's own defence into a per
 * request full scan on top of the lookup's. Between sweeps the capacity and its eviction policy
 * bound the table on their own; the sweep only reclaims slots earlier and more cheaply. */
#define _HTTP_SERVICE_RATE_LIMIT_SWEEP_SECONDS 1

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/* One tracked (rule, key) counter. Replaces the four former parallel AL_* lists (rules/keys/
 * counts/timestamps) with one record array allocated once at init - see rate_limit.h Memory
 * Management. Two consequences beyond the allocation count:
 *   - `key` is a fixed inline buffer, not an owned Str, so eviction reclaims a record's key by
 *     construction. An arena-backed instance previously could not: releasing a Str into a linear
 *     arena frees nothing, so the key list grew without limit under exactly the key-rotating
 *     client the cap existed to stop.
 *   - `rule` is an INDEX into the rule table, not a copy of the rule NAME. The name already
 *     lives in the table; copying it per record cost an allocation and turned every lookup into
 *     a string compare for no information a comparison of two USize does not carry. */
typedef struct {
    /** @brief Hits registered in the current window. */
    USize count;
    /** @brief Key text. Always NUL-terminated: the record is zero-filled and key_size < KEY_SIZE. */
    char key[HTTP_SERVICE_RATE_LIMIT_KEY_SIZE];
    /** @brief Byte length of the key text. */
    USize key_size;
    /** @brief Index into the rule table, or USIZE_MAX for the instance default rule. */
    USize rule;
    /** @brief Instant the current window opened - its FIRST hit, not its last. */
    ChronoInstant timestamp;
} _HTTP_Service_Rate_Limit_Record;

/* One registered rule. Fixed inline name for the same reason a record's key is: the table is one
 * allocation and a rule costs nothing per entry. Rules are never removed, which is what lets
 * http_service_rate_limit_rule_add_1 hand out an index as a stable public handle. */
typedef struct {
    /** @brief Cooldown in seconds. Never zero: rule_add refuses a zero. */
    USize cooldown;
    /** @brief Hit limit per cooldown. Never zero: rule_add refuses a zero. */
    USize limit;
    /** @brief Rule name. Always NUL-terminated: the entry is zero-filled and name_size < NAME_SIZE. */
    char name[HTTP_SERVICE_RATE_LIMIT_RULE_NAME_SIZE];
    /** @brief Byte length of the rule name. */
    USize name_size;
    /** @brief Hits registered in this rule's overflow bucket during its current window. */
    USize overflow_count;
    /** @brief Instant this rule's overflow window opened, or 0 while the bucket has never been used. */
    ChronoInstant overflow_timestamp;
} _HTTP_Service_Rate_Limit_Rule;

/*==============================================================================
 * MARK: - Helpers
 *============================================================================*/

/* Whole seconds from `from` to `now`, both instants from chrono_now. Monotonic in both
 * directions: `now` earlier than `from` cannot happen on a monotonic clock, and answering 0
 * rather than an unsigned wrap is what removes the wall-clock rollback guard the datetime_now
 * version needed - a backwards NTP step used to wrap this subtraction to a huge value and
 * silently clear a client's accumulated count, a rate-limit bypass triggered by a clock change. */
static USize _http_service_rate_limit_elapsed(ChronoInstant const from, ChronoInstant const now) {
    return now <= from ? 0 : (USize) chrono_duration_seconds(chrono_duration_difference(now, from));
}

/* Answer whether a degraded-path diagnostic may be emitted now, stamping `last` when it may.
 *
 * Throttled by TIME rather than by an edge. The bool flags this replaces were set on the first
 * report and cleared on the next SUCCESS, so any traffic that interleaves a failing request with
 * a succeeding one - alternating an unknown rule with a known one, an empty key with a real one,
 * a new key with a tracked one - re-armed the flag on every other call and logged the failure
 * every single time. Which request comes next is the caller's choice, so "once per episode" was a
 * bound only well-behaved callers respected. A timestamp is re-armed by nothing but the clock.
 *
 * Zero means "never reported", so the first report always fires; an instant of exactly 0 is
 * stamped as 1 so that a report can never be mistaken for silence. */
static bool _http_service_rate_limit_report_due(ChronoInstant *const last, ChronoInstant const now) {
    if (*last != 0 && _http_service_rate_limit_elapsed(*last, now) < _HTTP_SERVICE_RATE_LIMIT_REPORT_SECONDS) {
        return false;
    }

    *last = now == 0 ? 1 : now;

    return true;
}

/* Fill a caller's result, tolerating the null `out` every _1 wrapper passes when it wants only
 * the verdict. */
static void _http_service_rate_limit_result_set(HTTP_Service_Rate_Limit_Result *const out, bool const allowed, USize const limit, USize const remaining, USize const retry_after) {
    if (out == nullptr) {
        return;
    }

    out->allowed        = allowed;
    out->limit          = limit;
    out->remaining      = remaining;
    out->retry_after    = retry_after;
}

static USize _http_service_rate_limit_cooldown_at(HTTP_Service_Rate_Limit const *const self, USize const rule) {
    if (rule == USIZE_MAX) {
        return self->cooldown;
    }

    return ((_HTTP_Service_Rate_Limit_Rule const*) self->rules)[rule].cooldown;
}

static USize _http_service_rate_limit_limit_at(HTTP_Service_Rate_Limit const *const self, USize const rule) {
    if (rule == USIZE_MAX) {
        return self->limit;
    }

    return ((_HTTP_Service_Rate_Limit_Rule const*) self->rules)[rule].limit;
}

/* Resolve a rule NAME to a rule index, writing USIZE_MAX for the instance default rule.
 *
 * Answers false for a name that was never registered, which every caller turns into a refusal.
 * The old behaviour - fall back to the instance default - was fail-OPEN on a configuration
 * error: a typo'd rule constant, or a rule_add that had quietly failed at startup, replaced a
 * 5-per-300s login limit with the instance's 60-per-60s default at every enforcement point, with
 * nothing anywhere saying so. Logged at most once per report window rather than once per request,
 * because this runs on the request path and an unthrottled diagnostic hands a caller control of
 * log volume. A window, not an "episode": an edge-triggered flag was re-armed by every interleaved
 * good request, which is a thing the caller chooses freely. */
static bool _http_service_rate_limit_rule_resolve(HTTP_Service_Rate_Limit *const self, char const *const rule, ChronoInstant const now, USize *const out) {
    trace_log_push(LOG_METADATA);

    *out = USIZE_MAX;

    if (rule == nullptr) {
        trace_log_pop();

        return true;
    }

    USize const rule_size = char_length(rule);

    if (char_compare_equal_2(rule, rule_size, HTTP_SERVICE_RATE_LIMIT_RULE_DEFAULT, CHAR_STATIC_SIZE(HTTP_SERVICE_RATE_LIMIT_RULE_DEFAULT))) {
        trace_log_pop();

        return true;
    }

    _HTTP_Service_Rate_Limit_Rule const *const rules = (_HTTP_Service_Rate_Limit_Rule const*) self->rules;

    for (USize i = 0; i < self->rule_count; i += 1) {
        if (char_compare_equal_2(rules[i].name, rules[i].name_size, rule, rule_size)) {
            *out = i;

            trace_log_pop();

            return true;
        }
    }

    /* Length only, not the rule text itself: a caller-supplied name is logged the same way
     * _key_valid logs an unusable key, never verbatim into the log. */
    if (_http_service_rate_limit_report_due(&self->unknown_rule_reported_at, now)) {
        log_message_2(LOG_LEVEL_ERROR, LOG_METADATA,
            "http_service_rate_limit: a rule name of %zu bytes is not registered - refusing every request that names it; register it at startup and check rule_add's answer", rule_size);
    }

    trace_log_pop();

    return false;
}

/* Validate a key and answer its length. An empty key is refused fail-CLOSED, not tracked: every
 * client whose address the server could not resolve would otherwise share one bucket, so a
 * single forged X-Forwarded-For could exhaust it for all of them - and an unidentifiable caller
 * must not be the one caller a limiter waves through. An oversize key is refused for the same
 * reason truncating it would be worse: two distinct long tokens truncated to the same prefix
 * would share one budget. Logged at most once per report window, for the reason above it. */
static bool _http_service_rate_limit_key_valid(HTTP_Service_Rate_Limit *const self, char const *const key, ChronoInstant const now, USize *const out) {
    trace_log_push(LOG_METADATA);

    *out = 0;

    USize const key_size = key == nullptr ? 0 : char_length(key);

    if (key_size == 0 || key_size >= HTTP_SERVICE_RATE_LIMIT_KEY_SIZE) {
        if (_http_service_rate_limit_report_due(&self->key_reported_at, now)) {
            log_message_2(LOG_LEVEL_WARN, LOG_METADATA,
                "http_service_rate_limit: a key of %zu bytes is empty or past the %d-byte limit - refusing the request rather than tracking it", key_size, HTTP_SERVICE_RATE_LIMIT_KEY_SIZE);
        }

        trace_log_pop();

        return false;
    }

    *out = key_size;

    trace_log_pop();

    return true;
}

/* Rule index first, then key length, then key bytes: the integer compare rejects most records
 * without touching the key at all, and the length rejects most of the rest. The old form
 * evaluated a full string compare of BOTH the rule name and the key for every record before
 * combining the two answers. */
static USize _http_service_rate_limit_record_find(HTTP_Service_Rate_Limit const *const self, USize const rule, char const *const key, USize const key_size) {
    _HTTP_Service_Rate_Limit_Record const *const records = (_HTTP_Service_Rate_Limit_Record const*) self->records;

    for (USize i = 0; i < self->count; i += 1) {
        if (records[i].rule == rule && records[i].key_size == key_size && char_compare_equal_2(records[i].key, records[i].key_size, key, key_size)) {
            return i;
        }
    }

    return USIZE_MAX;
}

/* Swap-with-last removal: record order was never a contract (indices already moved on every
 * remove), so this is O(1) instead of shifting the tail down. */
static void _http_service_rate_limit_record_remove(HTTP_Service_Rate_Limit *const self, USize const index) {
    _HTTP_Service_Rate_Limit_Record *const records = (_HTTP_Service_Rate_Limit_Record*) self->records;

    records[index] = records[self->count - 1];

    self->count -= 1;
}

/* Drop every record whose own window has already elapsed. Its counter would be reset to 0 by the
 * next read anyway, so removing it changes no verdict and frees the slot early.
 *
 * Each record is judged against ITS OWN rule's cooldown, which the earlier design could not do:
 * with the rule stored only as a name per record, the sweep had to use the LARGEST configured
 * cooldown for everything, so that it could never discard a counter still live under a shorter
 * rule. A rule INDEX per record makes the exact test available, so
 * the conservative maximum is no longer needed - and short-cooldown rules now actually get
 * swept.
 *
 * Reverse iteration because removal swaps the LAST record into the removed slot: walking down
 * means the swapped-in record has already been visited, so nothing is skipped and nothing is
 * examined twice. */
static void _http_service_rate_limit_sweep(HTTP_Service_Rate_Limit *const self, ChronoInstant const now) {
    trace_log_push(LOG_METADATA);

    if (_http_service_rate_limit_elapsed(self->last_sweep, now) < _HTTP_SERVICE_RATE_LIMIT_SWEEP_SECONDS) {
        trace_log_pop();

        return;
    }

    self->last_sweep = now;

    _HTTP_Service_Rate_Limit_Record const *const records = (_HTTP_Service_Rate_Limit_Record const*) self->records;

    for (USize i = self->count; i > 0; i -= 1) {
        USize const index = i - 1;

        if (_http_service_rate_limit_elapsed(records[index].timestamp, now) >= _http_service_rate_limit_cooldown_at(self, records[index].rule)) {
            _http_service_rate_limit_record_remove(self, index);
        }
    }

    trace_log_pop();
}

/* Free one slot in a full table, answering false when no record may be taken.
 *
 * Three tiers, best-to-evict first, oldest wins within a tier: an EXPIRED record (its verdict is
 * already "fresh window"), then an IDLE one (count 0, so recreating it costs nothing), then the
 * oldest record still UNDER its limit.
 *
 * A record at or past its rule's limit is never a candidate. Evicting one recreates it at count
 * 0 on its next request, which hands a full budget back to exactly the client currently being
 * throttled - so a caller rotating keys fast enough to keep the table saturated could flush
 * their own limiter, turning the cap from a backstop into a bypass. Evicting purely by age had
 * the same effect for a subtler reason: the oldest window is by construction the one opened
 * first, i.e. the client furthest through its allowance.
 *
 * One pass, not one per tier: the tiers are a two-level rank (tier first, then age), so a single
 * scan carrying the best rank so far answers what three scans would. */
static bool _http_service_rate_limit_evict(HTTP_Service_Rate_Limit *const self, ChronoInstant const now) {
    trace_log_push(LOG_METADATA);

    _HTTP_Service_Rate_Limit_Record const *const records = (_HTTP_Service_Rate_Limit_Record const*) self->records;

    USize           best_index  = USIZE_MAX;
    USize           best_tier   = 0;
    ChronoInstant   best_time   = 0;

    for (USize i = 0; i < self->count; i += 1) {
        USize const cooldown    = _http_service_rate_limit_cooldown_at(self, records[i].rule);
        USize const limit       = _http_service_rate_limit_limit_at(self, records[i].rule);
        USize       tier        = 2;

        if (_http_service_rate_limit_elapsed(records[i].timestamp, now) >= cooldown) {
            tier = 0;
        }
        else if (records[i].count == 0) {
            tier = 1;
        }
        else if (records[i].count >= limit) {
            continue;
        }

        if (best_index == USIZE_MAX || tier < best_tier || (tier == best_tier && records[i].timestamp < best_time)) {
            best_index  = i;
            best_tier   = tier;
            best_time   = records[i].timestamp;
        }
    }

    if (best_index == USIZE_MAX) {
        trace_log_pop();

        return false;
    }

    _http_service_rate_limit_record_remove(self, best_index);

    trace_log_pop();

    return true;
}

static USize _http_service_rate_limit_record_add(HTTP_Service_Rate_Limit *const self, USize const rule, char const *const key, USize const key_size, ChronoInstant const now) {
    trace_log_push(LOG_METADATA);

    _http_service_rate_limit_sweep(self, now);

    if (self->count >= self->capacity && !_http_service_rate_limit_evict(self, now)) {
        trace_log_pop();

        return USIZE_MAX;
    }

    _HTTP_Service_Rate_Limit_Record *const record = &((_HTTP_Service_Rate_Limit_Record*) self->records)[self->count];

    memory_set(record->key, sizeof(record->key), 0);
    memory_copy_1(record->key, key, key_size);

    record->count       = 0;
    record->key_size    = key_size;
    record->rule        = rule;
    record->timestamp   = now;

    self->count += 1;

    trace_log_pop();

    return self->count - 1;
}

/* Open a fresh window when the current one has elapsed. Lazy by design: nothing sweeps counters
 * on a timer, so a record's count means nothing until this has run against the current instant. */
static void _http_service_rate_limit_refresh(_HTTP_Service_Rate_Limit_Record *const record, USize const cooldown, ChronoInstant const now) {
    if (_http_service_rate_limit_elapsed(record->timestamp, now) >= cooldown) {
        record->count       = 0;
        record->timestamp   = now;
    }
}

/* Register one hit against a rule's OVERFLOW bucket - the aggregate budget shared by every key
 * the saturated record table could not give a record of its own - and answer the verdict, filling
 * the caller's result exactly as the recorded path does.
 *
 * The bound the degraded path was missing. Before this, a hit that could not be recorded was
 * allowed and NOTHING was written down, so while saturation lasted EVERY unrecorded key was
 * allowed without any limit at all - the limiter was off for exactly the traffic that had turned
 * it off. Saturation is cheap to buy when the key is attacker-chosen: a registration rule keyed
 * on the submitted email address, say, lets capacity-many registration attempts from one address
 * fill the shared record table, and from then on an IP-keyed login rule waves every new IP through
 * unthrottled.
 *
 * Per RULE, not global, so one rule's overflow cannot spend another's; and held in the rule table
 * (or, for the instance default, in the instance itself) rather than in the record table, because
 * a record slot is precisely what saturation has run out of. The window and the limit are the
 * rule's own, so an unrecorded key is still ALLOWED while the bucket has budget: a rule hands out
 * at most `limit` such allowances per window in AGGREGATE instead of `limit` per key.
 *
 * Why allow rather than refuse outright, stated as the bound it is and not as "availability": the
 * bucket concedes at most ONE extra client's budget per rule per window. Refusing every unrecorded
 * key would instead let capacity-many attacker-chosen keys buy a TOTAL outage of every rule on the
 * instance - strictly more damage, for the same price. The trickle is the cheaper of the two, so
 * it is the public default; a fail-closed knob waits for a consumer that asks for one. */
static bool _http_service_rate_limit_overflow(HTTP_Service_Rate_Limit *const self, USize const rule, ChronoInstant const now, HTTP_Service_Rate_Limit_Result *const out) {
    USize const cooldown = _http_service_rate_limit_cooldown_at(self, rule);
    USize const limit    = _http_service_rate_limit_limit_at(self, rule);

    USize         *count     = &self->overflow_count;
    ChronoInstant *timestamp = &self->overflow_timestamp;

    if (rule != USIZE_MAX) {
        _HTTP_Service_Rate_Limit_Rule *const entry = &((_HTTP_Service_Rate_Limit_Rule*) self->rules)[rule];

        count       = &entry->overflow_count;
        timestamp   = &entry->overflow_timestamp;
    }

    /* A zero timestamp is a bucket that has never been used, not a window opened at instant 0. */
    if (*timestamp == 0 || _http_service_rate_limit_elapsed(*timestamp, now) >= cooldown) {
        *count      = 0;
        *timestamp  = now;
    }

    *count += 1;

    bool  const allowed     = *count <= limit;
    USize const elapsed     = _http_service_rate_limit_elapsed(*timestamp, now);
    USize const remaining   = *count >= limit ? 0 : limit - *count;
    USize       retry_after = 0;

    if (!allowed) {
        retry_after = elapsed >= cooldown ? 1 : cooldown - elapsed;
    }

    _http_service_rate_limit_result_set(out, allowed, limit, remaining, retry_after);

    return allowed;
}

/* How much longer a rule's OVERFLOW bucket refuses an UNRECORDED key (0 when it does not refuse
 * one), and via `out_remaining` how much of that bucket's budget such a key would be granted right
 * now. Both are read-only twins of _http_service_rate_limit_overflow above: they read the same
 * bucket and write NOTHING but the two OUT params, because the queries that consult them must not
 * register a hit.
 *
 * The return value closes the gap where the bucket bounded the REGISTERING calls only, so the
 * pairing the header itself recommends - blocked_* first, hit_* on the failure - had no bound at
 * all under saturation: hit_* spent the bucket while blocked_* read every unrecorded key as
 * unblocked, and an enforcement point that counts failures only (a login route, say) therefore
 * refused nothing. The bucket is now an unrecorded key's budget on EVERY path.
 *
 * `out_remaining` closes a narrower gap: while the bucket is only PARTIALLY spent inside a live
 * window (0 < count < limit) the return value above is 0 - the key is not yet blocked - so a
 * caller answering "how many are left" from that alone would report the full `limit`, though a
 * registering call would grant at most `limit - count`. `out_remaining` answers that true figure
 * on every path: `limit` for an unused or expired bucket, `limit - count` inside a live and
 * partially spent one, 0 once it is fully spent (the same instant the return value turns non-zero).
 *
 * The trade-off is deliberate: for the rest of one window after saturation clears, a fresh key
 * reads blocked (or under-budget) although a hit_* would now be given a record of its own. It
 * self-heals at the window's end, and erring toward refusal is the safe direction for a limiter. */
static USize _http_service_rate_limit_overflow_left(HTTP_Service_Rate_Limit const *const self, USize const rule, ChronoInstant const now, USize const cooldown,
                                                    USize const limit, USize *const out_remaining) {
    USize         count     = self->overflow_count;
    ChronoInstant timestamp = self->overflow_timestamp;

    if (rule != USIZE_MAX) {
        _HTTP_Service_Rate_Limit_Rule const *const entry = &((_HTTP_Service_Rate_Limit_Rule const*) self->rules)[rule];

        count       = entry->overflow_count;
        timestamp   = entry->overflow_timestamp;
    }

    *out_remaining = limit;

    /* A zero timestamp is a bucket that has never been used, not a window opened at instant 0. */
    if (timestamp == 0) {
        return 0;
    }

    USize const elapsed = _http_service_rate_limit_elapsed(timestamp, now);

    /* An elapsed window is stale the same way an unused bucket is: an unrecorded key reads the
     * FULL limit, not whatever count the previous window left behind. */
    if (elapsed >= cooldown) {
        return 0;
    }

    *out_remaining = count >= limit ? 0 : limit - count;

    if (count < limit) {
        return 0;
    }

    return cooldown - elapsed;
}

/* The whole enforcement decision, made once, under the caller's already-held lock, from ONE
 * clock read. Every public entry point routes through this or its non-registering twin below, so
 * a verdict and the Retry-After that belongs to it can never come from two different windows. */
static bool _http_service_rate_limit_hit(HTTP_Service_Rate_Limit *const self, char const *const rule, char const *const key, HTTP_Service_Rate_Limit_Result *const out) {
    trace_log_push(LOG_METADATA);

    /* Taken before the first branch so that every decision below - the window, the sweep, the
     * eviction ranking, the overflow bucket and the report throttles - measures against the same
     * instant, and so that the throttles cannot be defeated by a path that skips the read. */
    ChronoInstant const now = chrono_now();

    USize rule_index = USIZE_MAX;

    /* An unknown rule has no limit and no cooldown to report, so the result says so literally:
     * limit 0 (the rule does not exist - not "a limit of zero requests"), remaining 0, and the
     * INSTANCE's cooldown as the retry_after, since a refusal still owes the client a wait and no
     * other window applies. A limit of 0 in a RateLimit-Limit header is the tell that the rule
     * name was never registered; a caller that would rather omit the header entirely should check
     * rule_add's answer at startup, where the condition is a configuration error, not here. */
    if (!_http_service_rate_limit_rule_resolve(self, rule, now, &rule_index)) {
        _http_service_rate_limit_result_set(out, false, 0, 0, self->cooldown);

        trace_log_pop();

        return false;
    }

    USize const cooldown    = _http_service_rate_limit_cooldown_at(self, rule_index);
    USize const limit       = _http_service_rate_limit_limit_at(self, rule_index);
    USize       key_size    = 0;

    if (!_http_service_rate_limit_key_valid(self, key, now, &key_size)) {
        _http_service_rate_limit_result_set(out, false, limit, 0, cooldown);

        trace_log_pop();

        return false;
    }

    USize index = _http_service_rate_limit_record_find(self, rule_index, key, key_size);

    if (index == USIZE_MAX) {
        index = _http_service_rate_limit_record_add(self, rule_index, key, key_size, now);
    }

    /* No slot could be made: every record is at its own limit, so evicting one would have handed
     * a throttled client a fresh budget. The request is judged against the rule's OVERFLOW bucket
     * instead - one aggregate budget for every key that could not be recorded - so the degradation
     * keeps allowing while there is budget but is bounded, where it used to record nothing and
     * allow every unrecorded key without limit. Reported at most once per report window, not per
     * request: this path runs on every request while the condition lasts, so an unthrottled
     * diagnostic would let a caller drive log volume at their own request rate, on the very
     * control meant to stop them. */
    if (index == USIZE_MAX) {
        bool const allowed = _http_service_rate_limit_overflow(self, rule_index, now, out);

        if (_http_service_rate_limit_report_due(&self->degraded_reported_at, now)) {
            log_message_2(LOG_LEVEL_ERROR, LOG_METADATA,
                "http_service_rate_limit: no record slot available (every tracked record is at its limit); untracked keys now share "
                "the rule's overflow bucket of %zu per %zu seconds and are refused past it", limit, cooldown);
        }

        trace_log_pop();

        return allowed;
    }

    _HTTP_Service_Rate_Limit_Record *const record = &((_HTTP_Service_Rate_Limit_Record*) self->records)[index];

    _http_service_rate_limit_refresh(record, cooldown, now);

    record->count += 1;

    bool  const allowed     = record->count <= limit;
    USize const remaining   = record->count >= limit ? 0 : limit - record->count;
    USize const elapsed     = _http_service_rate_limit_elapsed(record->timestamp, now);
    USize       retry_after = 0;

    /* Never 0 while refused: a Retry-After of 0 tells a client to retry immediately, which is
     * exactly what the refusal is trying to prevent. The `elapsed >= cooldown` arm cannot fire on
     * THIS path - _refresh above has just guaranteed elapsed < cooldown - and is kept only so the
     * clamp reads the same here as in the overflow path, where the bucket's window can be older. */
    if (!allowed) {
        retry_after = elapsed >= cooldown ? 1 : cooldown - elapsed;
    }

    _http_service_rate_limit_result_set(out, allowed, limit, remaining, retry_after);

    trace_log_pop();

    return allowed;
}

/* The non-registering read shared by blocked_*, get_remaining_* and get_time_left_*: resolves
 * the rule and key and answers the record index plus its rule's limit and cooldown, WITHOUT
 * writing to the record. Answers false when the request is REFUSED outright (unknown rule or
 * unusable key) - each caller turns that into its own fail-closed answer.
 *
 * Read-only is a contract, not an optimisation. This used to call _refresh, so an expired
 * record was re-anchored at the QUERY instant: with a 1-second cooldown a hit at t=0 followed by
 * a blocked_* at t=1.1 opened the window [1.1, 2.1), and a hit at t=1.7 then counted into THAT
 * window and got a fresh budget at t=2.1 - 0.4 s earlier than first-hit anchoring (rate_limit.h
 * "anchored at the window's FIRST hit") allows. A query therefore made the limiter more permissive
 * by up to (hit - query) seconds, and contradicted the counting contract's "blocked_* judges
 * WITHOUT registering"; a periodic monitoring query also kept an idle record's timestamp moving
 * so the sweep never reclaimed its slot.
 *
 * `count` is the record's EFFECTIVE count - its stored count while the window is live, and 0 once
 * the window has elapsed, which is what the next registering call will reset it to. Callers judge
 * against this and never against the stored field, so they read exactly what _refresh would have
 * left behind without anyone having written it. It is 0 whenever `index` is USIZE_MAX.
 *
 * `now` is READ HERE, once, and handed back so a caller that measures a record's window uses the
 * same instant this call judged it against rather than taking a second reading. It is written
 * before any refusal, so it is usable on every path.
 *
 * `overflow_left` is how much longer the rule's overflow bucket refuses a key that has NO record,
 * and 0 whenever it does not - so every read-only query judges an unrecorded key against the same
 * bucket the registering calls spend, instead of reading it as unlimited while the table is
 * saturated. It is only ever non-zero together with an index of USIZE_MAX.
 *
 * `overflow_remaining` is how much of that same bucket's budget an unrecorded key would be
 * granted right now - `limit` while the bucket is unused, `limit - count` while it is live and
 * only partially spent, 0 once `overflow_left` above turns non-zero - so get_remaining_* answers
 * the figure a registering call would actually grant instead of overstating a partially spent
 * bucket as a full budget. */
static bool _http_service_rate_limit_read(HTTP_Service_Rate_Limit *const self, char const *const rule, char const *const key, USize *const count, USize *const index,
                                          USize *const limit, USize *const cooldown, ChronoInstant *const now, USize *const overflow_left, USize *const overflow_remaining) {
    trace_log_push(LOG_METADATA);

    *cooldown           = 0;
    *count              = 0;
    *index              = USIZE_MAX;
    *limit              = 0;
    *now                = chrono_now();
    *overflow_left      = 0;
    *overflow_remaining = 0;

    USize rule_index = USIZE_MAX;

    if (!_http_service_rate_limit_rule_resolve(self, rule, *now, &rule_index)) {
        trace_log_pop();

        return false;
    }

    *cooldown = _http_service_rate_limit_cooldown_at(self, rule_index);
    *limit = _http_service_rate_limit_limit_at(self, rule_index);

    USize key_size = 0;

    if (!_http_service_rate_limit_key_valid(self, key, *now, &key_size)) {
        trace_log_pop();

        return false;
    }

    *index = _http_service_rate_limit_record_find(self, rule_index, key, key_size);

    if (*index != USIZE_MAX) {
        _HTTP_Service_Rate_Limit_Record const *const record = &((_HTTP_Service_Rate_Limit_Record const*) self->records)[*index];

        /* The read-only twin of _refresh: the same expiry test, answered instead of written. */
        *count = _http_service_rate_limit_elapsed(record->timestamp, *now) >= *cooldown ? 0 : record->count;
    }
    else {
        *overflow_left = _http_service_rate_limit_overflow_left(self, rule_index, *now, *cooldown, *limit, overflow_remaining);
    }

    trace_log_pop();

    return true;
}

/* The shared constructor body. `allocator` is null for the non-arena forms, which is exactly
 * what allocator_try_borrow's own arena-aware signature expects. */
static bool _http_service_rate_limit_init(HTTP_Service_Rate_Limit *const self, USize const limit, USize const cooldown, USize const capacity,
#ifdef ARENA_IMPLEMENTATION
                                          Arena *const allocator
#else
                                          void *const allocator
#endif // ARENA_IMPLEMENTATION
                                          ) {
    trace_log_push(LOG_METADATA);

    *self = (HTTP_Service_Rate_Limit) DEFAULT_INITIALIZATION;

    /* Checked, not assumed: the header promised these were non-zero and nothing enforced it. A
     * cooldown of 0 makes every read open a fresh window, so the limiter is silently OFF; a
     * limit of 0 refuses every request. Both are configuration errors worth failing startup for,
     * and neither is an error_check_* abort - a value-dependent refusal must hold with the
     * checks compiled out too. */
    if (limit == 0 || cooldown == 0 || capacity == 0) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA,
            "http_service_rate_limit: limit, cooldown and capacity must all be non-zero (got %zu, %zu, %zu) - refusing to initialize", limit, cooldown, capacity);

        trace_log_pop();

        return false;
    }

    /* Checked before the multiplication that would wrap it. sizeof(record) * capacity overflowing
     * USize allocates a block far smaller than asked for, and the table is then indexed all the
     * way to `capacity` on the request path - a heap overflow bought with one large configuration
     * number. A value decision like the one above, so it is refused, not aborted. */
    if (capacity > USIZE_MAX / sizeof(_HTTP_Service_Rate_Limit_Record)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA,
            "http_service_rate_limit: a capacity of %zu records would overflow the allocation size - refusing to initialize", capacity);

        trace_log_pop();

        return false;
    }

    self->capacity  = capacity;
    self->cooldown  = cooldown;
    self->limit     = limit;

#ifdef ARENA_IMPLEMENTATION
    self->allocator = allocator;
    self->records   = allocator_try_borrow(sizeof(_HTTP_Service_Rate_Limit_Record) * capacity, allocator);
    self->rules     = allocator_try_borrow(sizeof(_HTTP_Service_Rate_Limit_Rule) * HTTP_SERVICE_RATE_LIMIT_RULE_CAPACITY, allocator);
#else
    (void) allocator;

    self->records   = allocator_try_borrow(sizeof(_HTTP_Service_Rate_Limit_Record) * capacity);
    self->rules     = allocator_try_borrow(sizeof(_HTTP_Service_Rate_Limit_Rule) * HTTP_SERVICE_RATE_LIMIT_RULE_CAPACITY);
#endif // ARENA_IMPLEMENTATION

    /* try_borrow, not borrow: allocator_borrow ENDS THE PROCESS on a heap that cannot meet the
     * request, which would make the refusal below dead code. */
    if (self->records == nullptr || self->rules == nullptr) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_rate_limit: storage allocation failed - refusing to run without a record table");

#ifdef ARENA_IMPLEMENTATION
        allocator_release(self->records, allocator);
        allocator_release(self->rules, allocator);
#else
        allocator_release(self->records);
        allocator_release(self->rules);
#endif // ARENA_IMPLEMENTATION

        *self = (HTTP_Service_Rate_Limit) DEFAULT_INITIALIZATION;

        trace_log_pop();

        return false;
    }

    /* Initialized through self, i.e. in the caller's final storage. Initializing a local and
     * returning it by value would copy an initialized CRITICAL_SECTION / pthread_mutex_t, which
     * neither platform permits.
     *
     * Reported rather than fatal, unlike the version this replaces: pthread_mutex_init can
     * return ENOMEM/EAGAIN, and abort() on that path took a whole server down over one failed
     * allocation. The caller now decides, and the false-return contract says *self is zeroed and
     * unusable - so a caller that ignores the answer cannot end up running unlocked either. */
    if (result_is_error(thread_mutex_init(&self->mutex))) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_rate_limit: mutex init failed - refusing to run without locking");

#ifdef ARENA_IMPLEMENTATION
        allocator_release(self->records, allocator);
        allocator_release(self->rules, allocator);
#else
        allocator_release(self->records);
        allocator_release(self->rules);
#endif // ARENA_IMPLEMENTATION

        *self = (HTTP_Service_Rate_Limit) DEFAULT_INITIALIZATION;

        trace_log_pop();

        return false;
    }

    self->last_sweep = chrono_now();

    trace_log_pop();

    return true;
}

/*==============================================================================
 * MARK: - API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
bool http_service_rate_limit_alloc_init_1(HTTP_Service_Rate_Limit *const self, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    bool const value = http_service_rate_limit_alloc_init_3(self,
        HTTP_SERVICE_RATE_LIMIT_DEFAULT_LIMIT, HTTP_SERVICE_RATE_LIMIT_DEFAULT_COOLDOWN, HTTP_SERVICE_RATE_LIMIT_DEFAULT_CAPACITY, allocator);

    trace_log_pop();

    return value;
}

bool http_service_rate_limit_alloc_init_2(HTTP_Service_Rate_Limit *const self, USize const limit, USize const cooldown, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    bool const value = http_service_rate_limit_alloc_init_3(self, limit, cooldown, HTTP_SERVICE_RATE_LIMIT_DEFAULT_CAPACITY, allocator);

    trace_log_pop();

    return value;
}

bool http_service_rate_limit_alloc_init_3(HTTP_Service_Rate_Limit *const self, USize const limit, USize const cooldown, USize const capacity, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    bool const value = _http_service_rate_limit_init(self, limit, cooldown, capacity, allocator);

    trace_log_pop();

    return value;
}
#endif // ARENA_IMPLEMENTATION

bool http_service_rate_limit_allowed_1(HTTP_Service_Rate_Limit *const self, char const *const key) {
    trace_log_push(LOG_METADATA);

    bool const value = http_service_rate_limit_check_2(self, nullptr, key, nullptr);

    trace_log_pop();

    return value;
}

bool http_service_rate_limit_allowed_2(HTTP_Service_Rate_Limit *const self, char const *const rule, char const *const key) {
    trace_log_push(LOG_METADATA);

    bool const value = http_service_rate_limit_check_2(self, rule, key, nullptr);

    trace_log_pop();

    return value;
}

bool http_service_rate_limit_blocked_1(HTTP_Service_Rate_Limit *const self, char const *const key) {
    trace_log_push(LOG_METADATA);

    bool const value = http_service_rate_limit_blocked_2(self, nullptr, key);

    trace_log_pop();

    return value;
}

bool http_service_rate_limit_blocked_2(HTTP_Service_Rate_Limit *const self, char const *const rule, char const *const key) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    thread_mutex_lock(&self->mutex);

    ChronoInstant now                = 0;
    USize         count              = 0;
    USize         index              = USIZE_MAX;
    USize         limit              = 0;
    USize         cooldown           = 0;
    USize         overflow_left      = 0;
    USize         overflow_remaining = 0;

    /* No error_check on `key`: _read routes to _key_valid, which already refuses a null key like
     * an empty one (fail-CLOSED), a data decision rather than an abort - see rate_limit.h Error
     * Handling. Fail CLOSED on a refusal: an unknown rule or an unusable key reads as BLOCKED, matching
     * the false that allowed_* would have answered for the same request. */
    bool value = true;

    if (_http_service_rate_limit_read(self, rule, key, &count, &index, &limit, &cooldown, &now, &overflow_left, &overflow_remaining)) {
        /* No record does NOT mean a full budget: while the table is saturated an unrecorded key's
         * budget IS the rule's overflow bucket, which check_* and hit_* judge it against.
         * Reading unblocked here left the header's own blocked_* + hit_* pairing unbounded. */
        value = overflow_left != 0;

        if (index != USIZE_MAX) {
            value = count >= limit;
        }
    }

    thread_mutex_unlock(&self->mutex);

    trace_log_pop();

    return value;
}

bool http_service_rate_limit_check_1(HTTP_Service_Rate_Limit *const self, char const *const key, HTTP_Service_Rate_Limit_Result *const out) {
    trace_log_push(LOG_METADATA);

    bool const value = http_service_rate_limit_check_2(self, nullptr, key, out);

    trace_log_pop();

    return value;
}

bool http_service_rate_limit_check_2(HTTP_Service_Rate_Limit *const self, char const *const rule, char const *const key, HTTP_Service_Rate_Limit_Result *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* No error_check on `key`: _hit routes to _key_valid, which already refuses a null key like
     * an empty one (fail-CLOSED) - see rate_limit.h Error Handling. A null key is reachable from
     * an ordinary allocation failure upstream, not only a programming error. */

    thread_mutex_lock(&self->mutex);

    bool const value = _http_service_rate_limit_hit(self, rule, key, out);

    thread_mutex_unlock(&self->mutex);

    trace_log_pop();

    return value;
}

void http_service_rate_limit_clear(HTTP_Service_Rate_Limit *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    thread_mutex_lock(&self->mutex);

    self->count                 = 0;
    self->overflow_count        = 0;
    self->overflow_timestamp    = 0;

    /* The overflow buckets go with the records. They are counters over the same windows, so a
     * "forget everything" that left them standing would keep refusing the keys a cleared table
     * has room for again - the one state a caller could not clear by any other means. */
    _HTTP_Service_Rate_Limit_Rule *const rules = (_HTTP_Service_Rate_Limit_Rule*) self->rules;

    for (USize i = 0; i < self->rule_count; i += 1) {
        rules[i].overflow_count     = 0;
        rules[i].overflow_timestamp = 0;
    }

    thread_mutex_unlock(&self->mutex);

    trace_log_pop();
}

USize http_service_rate_limit_get_remaining_1(HTTP_Service_Rate_Limit *const self, char const *const key) {
    trace_log_push(LOG_METADATA);

    USize const value = http_service_rate_limit_get_remaining_2(self, nullptr, key);

    trace_log_pop();

    return value;
}

USize http_service_rate_limit_get_remaining_2(HTTP_Service_Rate_Limit *const self, char const *const rule, char const *const key) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* No error_check on `key`: _read routes to _key_valid, which already refuses a null key like
     * an empty one (fail-CLOSED) - see rate_limit.h Error Handling. */

    thread_mutex_lock(&self->mutex);

    ChronoInstant now                = 0;
    USize         count              = 0;
    USize         index              = USIZE_MAX;
    USize         limit              = 0;
    USize         cooldown           = 0;
    USize         overflow_left      = 0;
    USize         overflow_remaining = 0;
    USize         value              = 0;

    if (_http_service_rate_limit_read(self, rule, key, &count, &index, &limit, &cooldown, &now, &overflow_left, &overflow_remaining)) {
        /* A key with no record has a full budget only while the rule's overflow bucket is unused.
         * Once it has taken any hits inside a live window, that key's real budget is whatever the
         * bucket has left - `overflow_remaining`, down to zero once the bucket is fully spent (the
         * same answer check_* would give it) - so reporting a flat `limit` here would overstate a
         * partially spent bucket as untouched. */
        value = overflow_remaining;

        if (index != USIZE_MAX) {
            value = count >= limit ? 0 : limit - count;
        }
    }

    thread_mutex_unlock(&self->mutex);

    trace_log_pop();

    return value;
}

USize http_service_rate_limit_get_size(HTTP_Service_Rate_Limit *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    thread_mutex_lock(&self->mutex);

    USize const value = self->count;

    thread_mutex_unlock(&self->mutex);

    trace_log_pop();

    return value;
}

USize http_service_rate_limit_get_time_left_1(HTTP_Service_Rate_Limit *const self, char const *const key) {
    trace_log_push(LOG_METADATA);

    USize const value = http_service_rate_limit_get_time_left_2(self, nullptr, key);

    trace_log_pop();

    return value;
}

USize http_service_rate_limit_get_time_left_2(HTTP_Service_Rate_Limit *const self, char const *const rule, char const *const key) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* No error_check on `key`: _read routes to _key_valid, which already refuses a null key like
     * an empty one (fail-CLOSED) - see rate_limit.h Error Handling. */

    thread_mutex_lock(&self->mutex);

    ChronoInstant now                = 0;
    USize         count              = 0;
    USize         index              = USIZE_MAX;
    USize         limit              = 0;
    USize         cooldown           = 0;
    USize         overflow_left      = 0;
    USize         overflow_remaining = 0;
    USize         value              = 0;

    /* One clock read for the whole answer, and now actually one: _read takes the instant, judges
     * the window against it and hands it back, so the elapsed below is measured from the very
     * instant that decided `count`. The previous form took a SECOND reading here - which the
     * comment claiming one read did not describe. */
    if (_http_service_rate_limit_read(self, rule, key, &count, &index, &limit, &cooldown, &now, &overflow_left, &overflow_remaining)) {
        /* An unrecorded key refused by the rule's overflow bucket has a real wait, not zero: it
         * is refused until that bucket's window turns over, which is what check_* answers as its
         * retry_after. Answering 0 here sent a client straight back into the degraded path. */
        value = overflow_left;

        if (index != USIZE_MAX) {
            _HTTP_Service_Rate_Limit_Record const *const record = &((_HTTP_Service_Rate_Limit_Record const*) self->records)[index];

            value = 0;

            /* `count` is the EFFECTIVE count, so an elapsed window reads 0 here and answers no
             * wait at all - the record is not refused, it is simply due to be re-anchored by the
             * next hit. The clamp below therefore only ever measures a LIVE window; it keeps the
             * `elapsed >= cooldown` arm so it reads the same as the overflow path's clamp. */
            if (count >= limit) {
                USize const elapsed = _http_service_rate_limit_elapsed(record->timestamp, now);

                value = elapsed >= cooldown ? 1 : cooldown - elapsed;
            }
        }
    }

    thread_mutex_unlock(&self->mutex);

    trace_log_pop();

    return value;
}

void http_service_rate_limit_hit_1(HTTP_Service_Rate_Limit *const self, char const *const key) {
    trace_log_push(LOG_METADATA);

    http_service_rate_limit_hit_2(self, nullptr, key);

    trace_log_pop();
}

void http_service_rate_limit_hit_2(HTTP_Service_Rate_Limit *const self, char const *const rule, char const *const key) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* No error_check on `key`: _hit routes to _key_valid, which already refuses a null key like
     * an empty one (fail-CLOSED) - see rate_limit.h Error Handling. */

    thread_mutex_lock(&self->mutex);

    (void) _http_service_rate_limit_hit(self, rule, key, nullptr);

    thread_mutex_unlock(&self->mutex);

    trace_log_pop();
}

bool http_service_rate_limit_init_1(HTTP_Service_Rate_Limit *const self) {
    trace_log_push(LOG_METADATA);

    bool const value = http_service_rate_limit_init_3(self, HTTP_SERVICE_RATE_LIMIT_DEFAULT_LIMIT, HTTP_SERVICE_RATE_LIMIT_DEFAULT_COOLDOWN, HTTP_SERVICE_RATE_LIMIT_DEFAULT_CAPACITY);

    trace_log_pop();

    return value;
}

bool http_service_rate_limit_init_2(HTTP_Service_Rate_Limit *const self, USize const limit, USize const cooldown) {
    trace_log_push(LOG_METADATA);

    bool const value = http_service_rate_limit_init_3(self, limit, cooldown, HTTP_SERVICE_RATE_LIMIT_DEFAULT_CAPACITY);

    trace_log_pop();

    return value;
}

bool http_service_rate_limit_init_3(HTTP_Service_Rate_Limit *const self, USize const limit, USize const cooldown, USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    bool const value = _http_service_rate_limit_init(self, limit, cooldown, capacity, nullptr);

    trace_log_pop();

    return value;
}

void http_service_rate_limit_reset_1(HTTP_Service_Rate_Limit *const self, char const *const key) {
    trace_log_push(LOG_METADATA);

    http_service_rate_limit_reset_2(self, nullptr, key);

    trace_log_pop();
}

void http_service_rate_limit_reset_2(HTTP_Service_Rate_Limit *const self, char const *const rule, char const *const key) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* No error_check on `key`: _key_valid already refuses a null key like an empty one
     * (fail-CLOSED) - see rate_limit.h Error Handling. */

    thread_mutex_lock(&self->mutex);

    ChronoInstant const now = chrono_now();

    USize rule_index = USIZE_MAX;
    USize key_size   = 0;

    if (_http_service_rate_limit_rule_resolve(self, rule, now, &rule_index) && _http_service_rate_limit_key_valid(self, key, now, &key_size)) {
        USize const index = _http_service_rate_limit_record_find(self, rule_index, key, key_size);

        /* Removed, not zeroed: a zeroed record kept occupying a capacity slot on behalf of a
         * client with nothing left to remember. Its next hit re-creates it with a fresh window,
         * which is what a zeroed record would have given it anyway. */
        if (index != USIZE_MAX) {
            _http_service_rate_limit_record_remove(self, index);
        }
    }

    thread_mutex_unlock(&self->mutex);

    trace_log_pop();
}

USize http_service_rate_limit_rule_add_1(HTTP_Service_Rate_Limit *const self, char const *const name, USize const limit, USize const cooldown) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    thread_mutex_lock(&self->mutex);

    USize const name_size   = char_length(name);
    USize       value       = USIZE_MAX;

    /* Every refusal is a VALUE decision, answered rather than aborted, and every one of them is
     * security-relevant: a rule that is not registered is refused at every enforcement point, so
     * a startup that ignores this answer refuses every request naming it. The reserved default
     * name is refused because a rule registered under it could never be told apart from the
     * instance's own limit. The name is logged VERBATIM here, unlike _rule_resolve's length-only
     * report, because a registration name is STARTUP configuration and never client data. */
    if (name_size == 0 || name_size >= HTTP_SERVICE_RATE_LIMIT_RULE_NAME_SIZE || limit == 0 || cooldown == 0
        || char_compare_equal_2(name, name_size, HTTP_SERVICE_RATE_LIMIT_RULE_DEFAULT, CHAR_STATIC_SIZE(HTTP_SERVICE_RATE_LIMIT_RULE_DEFAULT))) {
        log_message_2(LOG_LEVEL_ERROR, LOG_METADATA,
            "http_service_rate_limit: rule '%s' (limit %zu, cooldown %zu) is not a valid registration - it will be REFUSED at every enforcement point", name, limit, cooldown);

        thread_mutex_unlock(&self->mutex);

        trace_log_pop();

        return USIZE_MAX;
    }

    _HTTP_Service_Rate_Limit_Rule *const rules = (_HTTP_Service_Rate_Limit_Rule*) self->rules;

    for (USize i = 0; i < self->rule_count; i += 1) {
        if (char_compare_equal_2(rules[i].name, rules[i].name_size, name, name_size)) {
            rules[i].cooldown   = cooldown;
            rules[i].limit      = limit;
            value               = i;

            break;
        }
    }

    if (value == USIZE_MAX && self->rule_count >= HTTP_SERVICE_RATE_LIMIT_RULE_CAPACITY) {
        log_message_2(LOG_LEVEL_ERROR, LOG_METADATA,
            "http_service_rate_limit: the rule table is full at %d entries - rule '%s' is NOT registered and every request naming it will be refused",
            HTTP_SERVICE_RATE_LIMIT_RULE_CAPACITY, name);
    }
    else if (value == USIZE_MAX) {
        _HTTP_Service_Rate_Limit_Rule *const rule = &rules[self->rule_count];

        memory_set(rule->name, sizeof(rule->name), 0);
        memory_copy_1(rule->name, name, name_size);

        /* The overflow bucket is zeroed explicitly rather than left to the allocator: a fresh
         * rule must not inherit a count, and a timestamp of 0 is what marks a bucket as never
         * used. Re-registering an EXISTING rule above deliberately leaves its bucket alone, so a
         * startup that reconfigures a limit cannot be used to clear a degraded window. */
        rule->cooldown           = cooldown;
        rule->limit              = limit;
        rule->name_size          = name_size;
        rule->overflow_count     = 0;
        rule->overflow_timestamp = 0;

        self->rule_count += 1;

        value = self->rule_count - 1;
    }

    thread_mutex_unlock(&self->mutex);

    trace_log_pop();

    return value;
}

void http_service_rate_limit_uninit(HTTP_Service_Rate_Limit *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* A second call is a harmless no-op: `records` is exactly what the first call zeroed, and
     * destroying an already-destroyed mutex is undefined on CRITICAL_SECTION and pthreads alike. */
    if (self->records == nullptr) {
        trace_log_pop();

        return;
    }

    /* No lock taken - this destroys it. See rate_limit.h Thread Safety: the caller must have
     * quiesced every other user of the instance before calling. Taking the lock here would not
     * help either way; a thread still inside a public call would be holding it, and unlocking
     * before thread_mutex_uninit would reopen the same window. */
#ifdef ARENA_IMPLEMENTATION
    allocator_release(self->records, self->allocator);
    allocator_release(self->rules, self->allocator);

    self->allocator = nullptr;
#else
    allocator_release(self->records);
    allocator_release(self->rules);
#endif // ARENA_IMPLEMENTATION

    self->capacity      = 0;
    self->count         = 0;
    self->records       = nullptr;
    self->rule_count    = 0;
    self->rules         = nullptr;

    thread_mutex_uninit(&self->mutex);

    trace_log_pop();
}