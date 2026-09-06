/*==============================================================================
 * MARK: - Includes
 *============================================================================*/

#include <http/service/ip_block/ip_block.h>
#include <net/net.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

// Longest address text a record stores. No real IPv4/IPv6 literal (including a bracketed IPv6
// with a zone id) comes close; an address at or past this length is refused as UNTRACKED.
#define _HTTP_SERVICE_IP_BLOCK_KEY_SIZE 64

static_assert(_HTTP_SERVICE_IP_BLOCK_KEY_SIZE == HTTP_SERVICE_IP_BLOCK_ADDRESS_SIZE,
              "get_record copies a record's key straight into HTTP_Service_IP_Block_Record.address - the two sizes must match");

// Ceiling on tracked IPs. Not yet configurable - see ip_block.h Performance note: measured
// acceptable at this size, so a hashset backing (or a configurable cap) waits for a real
// profile that disagrees.
#define _HTTP_SERVICE_IP_BLOCK_MAX_ENTRIES 4096
// Longest normalized comparison key: an IPv6 /64 network-prefix (high 8 bytes of the 16-byte
// address, host part zeroed - see _http_service_ip_block_normalize). A plain IPv4 key uses only
// the first 4 of these bytes.
#define _HTTP_SERVICE_IP_BLOCK_NORM_SIZE 8

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/* One tracked address. Replaces the four former parallel AL_* lists (list/blocked/strikes/
 * timestamp) with one record array allocated once at init - see ip_block.h Memory Management
 * note. `key` is a fixed inline buffer rather than an owned Str, which is what makes the array a
 * single allocation: there is nothing per-entry left for an arena to fail to free on eviction. */
typedef struct {
    /** @brief Address text, NOT necessarily NUL past key_size but always NUL-terminated. Kept
     *         verbatim (never overwritten by normalization) so http_service_ip_block_get_ip_copy
     *         still returns something human-readable for an admin listing. */
    char key[_HTTP_SERVICE_IP_BLOCK_KEY_SIZE];
    /** @brief Byte length of the address text in `key`. */
    USize key_size;
    /** @brief Normalized comparison key - see _http_service_ip_block_normalize. Compared instead
     *         of `key` whenever both sides of a lookup parsed successfully. */
    Byte norm[_HTTP_SERVICE_IP_BLOCK_NORM_SIZE];
    /** @brief Byte length of `norm`: 4 (IPv4, including an unwrapped IPv4-mapped IPv6 literal),
     *         8 (IPv6 /64 bucket), or 0 when `key` did not parse as either - the record then
     *         falls back to a raw comparison of `key`/`key_size`, the pre-existing behaviour. */
    U8 norm_size;
    /** @brief Last strike/registration instant. Also the instant a block started. */
    ChronoInstant timestamp;
    /** @brief Strike count. */
    U16 strikes;
    /** @brief Block flag. */
    bool blocked;
} _HTTP_Service_IP_Block_Record;

/*==============================================================================
 * MARK: - Helpers
 *============================================================================*/

/* True when `seconds` is a real limit (0 disables it) and at least that many seconds have
 * elapsed, monotonically, since `timestamp`. chrono_elapsed reads chrono_now() internally, so
 * this never regresses on a wall-clock step backwards the way datetime_now would. */
static bool _http_service_ip_block_expired(ChronoInstant const timestamp, U32 const seconds) {
    return seconds > 0 && chrono_duration_seconds(chrono_elapsed(timestamp)) >= (F64) seconds;
}

/* Evicts a record to make room, swapping the last record into its slot - order was never a
 * documented contract (indices already move on every remove), so this is O(1) instead of the old
 * shift-remove. Prefers the oldest record that is NOT under a live block: dropping a live block
 * to make room would let an attacker flush their own block simply by rotating addresses, which
 * is the exact situation this service exists to handle. A block whose block_seconds have already
 * elapsed counts as unblocked here (report Mid 7) - it lifts on its next read anyway, so leaving
 * it ranked above a live visitor evicted a real client to keep a block that no longer applied.
 * Only when every tracked record is under a LIVE block - the table has filled up entirely with
 * distinct addresses all still serving their time - does this fall back to evicting the OLDEST
 * of them instead of refusing the caller's registration outright (see ip_block.h Performance
 * Characteristics for the trade-off): refusing here would fail OPEN for whichever address
 * triggered this call, since the caller (_http_service_ip_block_register) would then have
 * nowhere to record its strike at all. self->count is always > 0 when this runs (called only
 * when the table is already full), so a record to evict always exists.
 *
 * One pass, not two (report Misc 18): the two tiers are a two-level rank - "not live-blocked"
 * beats "live-blocked", and older beats newer within a tier - so a single scan carrying the best
 * rank so far answers exactly what the pair of loops did, at half the table reads. */
static void _http_service_ip_block_evict(HTTP_Service_IP_Block *const self) {
    trace_log_push(LOG_METADATA);

    _HTTP_Service_IP_Block_Record *const records = (_HTTP_Service_IP_Block_Record*) self->records;

    USize           oldest_index    = USIZE_MAX;
    ChronoInstant   oldest_time     = 0;
    bool            oldest_blocked  = true;

    for (USize i = 0; i < self->count; i += 1) {
        bool const blocked = records[i].blocked && !_http_service_ip_block_expired(records[i].timestamp, self->block_seconds);

        /* A better TIER always wins; within the same tier, the older timestamp wins. */
        if (oldest_index == USIZE_MAX                                            ||
            (oldest_blocked && !blocked)                                         ||
            (oldest_blocked == blocked && records[i].timestamp < oldest_time)) {
            oldest_index    = i;
            oldest_time     = records[i].timestamp;
            oldest_blocked  = blocked;
        }
    }

    if (oldest_index != USIZE_MAX) {
        records[oldest_index] = records[self->count - 1];
        self->count -= 1;
    }

    trace_log_pop();
}

/* Parses `ip` (address text, NOT necessarily NUL-terminated at ip_size) into a normalized
 * comparison key via net_socket_address_init_2, writing its length to *norm_size:
 *   - IPv4 literal: the plain 4-byte address.
 *   - IPv4-mapped IPv6 literal ("::ffff:a.b.c.d" - first 10 bytes zero, next 2 bytes 0xff):
 *     unwrapped to the same 4-byte form, so it unifies with a plain IPv4 literal for the same
 *     address rather than being /64-bucketed.
 *   - Any other IPv6 literal: the /64 network-prefix - the high 8 bytes of the 16-byte address,
 *     host part dropped, so an ISP rotating a client within its own /64 keeps hitting one entry.
 *   - Anything that parses as neither (malformed - untrusted network input) or is too long to
 *     even try: *norm_size = 0. Never aborts; the caller then falls back to comparing `ip`
 *     itself, the pre-existing raw-text behaviour, so a bad address string cannot crash the
 *     server or dodge tracking outright. */
static void _http_service_ip_block_normalize(char const *const ip, USize const ip_size, Byte *const norm, U8 *const norm_size) {
    trace_log_push(LOG_METADATA);

    *norm_size = 0;

    if (ip_size == 0 || ip_size >= _HTTP_SERVICE_IP_BLOCK_KEY_SIZE) {
        trace_log_pop();

        return;
    }

    char literal[_HTTP_SERVICE_IP_BLOCK_KEY_SIZE] = DEFAULT_INITIALIZATION;
    memory_copy_1(literal, ip, ip_size);

    // A colon never appears in an IPv4 literal, so its presence picks the family to try -
    // net_socket_address_init_2 requires the family to match the literal's own form and does
    // not auto-detect.
    Net_Family const   family  = char_find_first_1(literal, ":") != CHAR_NPOS ? NET_FAMILY_IPV6 : NET_FAMILY_IPV4;
    Net_Socket_Address  address = DEFAULT_INITIALIZATION;

    if (result_is_error(net_socket_address_init_2(family, 0, literal, &address))) {
        trace_log_pop();

        return;
    }

    if (family == NET_FAMILY_IPV4) {
        struct sockaddr_in const *const in4 = (struct sockaddr_in const*) &address.storage;

        memory_copy_1(norm, &in4->sin_addr, sizeof(in4->sin_addr));

        *norm_size = (U8) sizeof(in4->sin_addr);

        trace_log_pop();

        return;
    }

    struct sockaddr_in6 const *const   in6     = (struct sockaddr_in6 const*) &address.storage;
    Byte const *const                  bytes   = (Byte const*) &in6->sin6_addr;
    bool                                mapped  = true;

    for (USize i = 0; i < 10 && mapped; i += 1) {
        mapped = bytes[i] == 0;
    }

    mapped = mapped && bytes[10] == 0xff && bytes[11] == 0xff;

    if (mapped) {
        memory_copy_1(norm, bytes + 12, 4);

        *norm_size = 4;

        trace_log_pop();

        return;
    }

    memory_copy_1(norm, bytes, 8);

    *norm_size = 8;

    trace_log_pop();
}

/* Unlocked lookup internal. The public at_2/exists_2 take the lock; this does not, so a caller
 * that already holds it (add_strike_2) can search without a recursive acquire - ThreadMutex is
 * not recursive, so calling the public form under the lock would deadlock outright.
 *
 * `ip`/`ip_size` is normalized once up front rather than per record. A record whose own key
 * normalized successfully is compared by normalized bytes whenever the query did too - this is
 * what unifies "::ffff:1.2.3.4" with "1.2.3.4" and buckets an IPv6 /64 to one entry. Either side
 * failing to normalize (malformed text) falls back to the original raw-text comparison, so two
 * different malformed strings never collide and a malformed query can still find a literal
 * match. */
static USize _http_service_ip_block_find(HTTP_Service_IP_Block const *const self, char const *const ip, USize const ip_size) {
    trace_log_push(LOG_METADATA);

    _HTTP_Service_IP_Block_Record const *const records = (_HTTP_Service_IP_Block_Record const*) self->records;

    Byte    query_norm[_HTTP_SERVICE_IP_BLOCK_NORM_SIZE]  = DEFAULT_INITIALIZATION;
    U8      query_norm_size                                = 0;

    _http_service_ip_block_normalize(ip, ip_size, query_norm, &query_norm_size);

    for (USize i = 0; i < self->count; i += 1) {
        if (query_norm_size > 0 && records[i].norm_size > 0) {
            if (records[i].norm_size == query_norm_size && char_compare_equal_2((char const*) records[i].norm, records[i].norm_size, (char const*) query_norm, query_norm_size)) {
                trace_log_pop();

                return i;
            }

            continue;
        }

        if (records[i].key_size == ip_size && char_compare_equal_2(records[i].key, records[i].key_size, ip, ip_size)) {
            trace_log_pop();

            return i;
        }
    }

    trace_log_pop();

    return USIZE_MAX;
}

/* Find-or-create, returning the record's index or USIZE_MAX when the address cannot be tracked
 * (oversize key only now - see _http_service_ip_block_evict for why a full table of blocked
 * entries no longer refuses). Called under the lock by every writer, including add_strike_2 -
 * which is what makes add_strike auto-register (report High 3): a strike against an unseen
 * address now registers it at 0 strikes instead of silently no-op'ing. */
static USize _http_service_ip_block_register(HTTP_Service_IP_Block *const self, char const *const ip, USize const ip_size) {
    trace_log_push(LOG_METADATA);

    USize const existing = _http_service_ip_block_find(self, ip, ip_size);

    if (existing != USIZE_MAX) {
        trace_log_pop();

        return existing;
    }

    if (ip_size >= _HTTP_SERVICE_IP_BLOCK_KEY_SIZE) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_ip_block: address of %zu bytes exceeds the %d-byte key - not tracked", ip_size, _HTTP_SERVICE_IP_BLOCK_KEY_SIZE);

        trace_log_pop();

        return USIZE_MAX;
    }

    if (self->count >= self->capacity) {
        _http_service_ip_block_evict(self);

        /* Belt-and-braces only: evict always frees one slot when count > 0 (its loop sets
         * oldest_index on its first iteration), so this branch is unreachable today - kept
         * in case a future eviction policy declines to free a slot. */
        if (self->count >= self->capacity) {
            trace_log_pop();

            return USIZE_MAX;
        }
    }

    _HTTP_Service_IP_Block_Record *const record = &((_HTTP_Service_IP_Block_Record*) self->records)[self->count];

    memory_set(record->key, sizeof(record->key), 0);
    memory_copy_1(record->key, ip, ip_size);

    record->key_size    = ip_size;
    record->timestamp   = chrono_now();
    record->strikes     = 0;
    record->blocked     = false;

    _http_service_ip_block_normalize(ip, ip_size, record->norm, &record->norm_size);

    self->count += 1;

    trace_log_pop();

    return self->count - 1;
}

/*==============================================================================
 * MARK: - API
 *============================================================================*/

bool http_service_ip_block_add_1(HTTP_Service_IP_Block *const self, char const *const ip) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "ip", (void*) ip);

    bool const tracked = http_service_ip_block_add_2(self, ip, char_length(ip));

    trace_log_pop();

    return tracked;
}

bool http_service_ip_block_add_2(HTTP_Service_IP_Block *const self, char const *const ip, USize const ip_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* The size check comes BEFORE the ip null-check, and the order is load-bearing. An empty
     * Str carries data == nullptr, so the _3 overloads forward (nullptr, 0) - and with the
     * null-check first that aborted the process before ever reaching this guard. Empty is a
     * legal VALUE here: the resolved client address can come back empty when resolution fails,
     * and the servers call in on the first line of every request, so one unresolvable client
     * would take the whole server down. A null ip with a NON-zero size is still a caller bug
     * and still aborts, below. */
    if (ip_size == 0) {
        trace_log_pop();

        return false;
    }

    error_check_null(LOG_METADATA, "ip", (void*) ip);

    thread_mutex_lock(&self->mutex);

    USize const index = _http_service_ip_block_register(self, ip, ip_size);

    thread_mutex_unlock(&self->mutex);

    trace_log_pop();

    return index != USIZE_MAX;
}

bool http_service_ip_block_add_3(HTTP_Service_IP_Block *const self, Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    bool const tracked = http_service_ip_block_add_2(self, str_get_data(data), str_get_size(data));

    trace_log_pop();

    return tracked;
}

bool http_service_ip_block_add_strike_1(HTTP_Service_IP_Block *const self, char const *const ip) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "ip", (void*) ip);

    bool const struck = http_service_ip_block_add_strike_2(self, ip, char_length(ip));

    trace_log_pop();

    return struck;
}

bool http_service_ip_block_add_strike_2(HTTP_Service_IP_Block *const self, char const *const ip, USize const ip_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* See http_service_ip_block_add_2 for why ip_size == 0 is checked before the ip null-check. */
    if (ip_size == 0) {
        trace_log_pop();

        return false;
    }

    error_check_null(LOG_METADATA, "ip", (void*) ip);

    thread_mutex_lock(&self->mutex);

    /* Auto-registers when absent (report High 3): a strike is no longer lost against an
     * untracked address, and the caller no longer needs its own add_2-then-blocked_2 pair. */
    USize const index = _http_service_ip_block_register(self, ip, ip_size);

    if (index == USIZE_MAX) {
        thread_mutex_unlock(&self->mutex);

        trace_log_pop();

        return false;
    }

    _HTTP_Service_IP_Block_Record *const record = &((_HTTP_Service_IP_Block_Record*) self->records)[index];

    /* A block past its lifetime lifts here, as a side effect of the strike that would otherwise
     * re-block it immediately - the strike count resets first so the fresh strike below starts
     * a clean count rather than instantly re-tripping the old one. */
    if (record->blocked && _http_service_ip_block_expired(record->timestamp, self->block_seconds)) {
        record->blocked = false;
        record->strikes = 0;
    }

    /* Strike-count expiration: an established client's counter cools down after `expiration`
     * seconds of quiet rather than accumulating forever. */
    if (self->expiration > 0 && _http_service_ip_block_expired(record->timestamp, self->expiration)) {
        record->strikes = 0;
    }

    /* The timestamp is refreshed on EVERY strike, including one that lands while the record is
     * still blocked, so a block SLIDES: an address that keeps striking keeps restarting its own
     * block_seconds and stays blocked until it goes quiet for the full window. That is the
     * intended shape for an abuser who never stops - a fixed window would let one keep hammering
     * through the lift - and it is stated in ip_block.h so a caller sizing block_seconds knows
     * it is a quiet period, not a sentence length (report Mid 7). */
    record->timestamp = chrono_now();
    record->strikes  += 1;

    /* `>=`, not `==`: an exact match could be stepped over. A limit of 0 means "blocking
     * disabled" rather than "block on sight", so it is excluded explicitly. */
    if (self->limit > 0 && record->strikes >= self->limit) {
        record->blocked = true;
    }

    thread_mutex_unlock(&self->mutex);

    trace_log_pop();

    return true;
}

bool http_service_ip_block_add_strike_3(HTTP_Service_IP_Block *const self, Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    bool const struck = http_service_ip_block_add_strike_2(self, str_get_data(data), str_get_size(data));

    trace_log_pop();

    return struck;
}

#ifdef ARENA_IMPLEMENTATION
bool http_service_ip_block_alloc_init_1(HTTP_Service_IP_Block *const self, U16 const limit, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    bool const initialized = http_service_ip_block_alloc_init_2(self, limit, 0, 0, allocator);

    trace_log_pop();

    return initialized;
}

bool http_service_ip_block_alloc_init_2(HTTP_Service_IP_Block *const self, U16 const limit, U32 const expiration, U32 const block_seconds, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    *self = (HTTP_Service_IP_Block){
        .allocator      = allocator,
        .block_seconds  = block_seconds,
        .capacity       = _HTTP_SERVICE_IP_BLOCK_MAX_ENTRIES,
        .count          = 0,
        .expiration     = expiration,
        .limit          = limit,
        .records        = nullptr
    };

    /* try_borrow, not borrow: allocator_borrow ENDS THE PROCESS on an arena that cannot meet the
     * request, which made the refusal below dead code and turned a too-small arena - a caller's
     * sizing mistake, caught at startup - into an abort instead of the documented false return.
     * The record array is ~384 KiB, big enough that an arena genuinely can decline it. */
    self->records = allocator_try_borrow(sizeof(_HTTP_Service_IP_Block_Record) * self->capacity, allocator);

    if (self->records == nullptr) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_ip_block: the arena refused the record array - refusing to run without storage");

        *self = (HTTP_Service_IP_Block) DEFAULT_INITIALIZATION;

        trace_log_pop();

        return false;
    }

    /* Initialized through self, i.e. in the caller's final storage. Initializing a local and
     * returning it by value would copy an initialized CRITICAL_SECTION / pthread_mutex_t, which
     * neither platform permits. Failure here is reported to the caller rather than aborting, so
     * an ip_block that can never lock is never left running silently unlocked. */
    if (result_is_error(thread_mutex_init(&self->mutex))) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_ip_block: mutex init failed - refusing to run without locking");

        allocator_release(self->records, allocator);

        *self = (HTTP_Service_IP_Block) DEFAULT_INITIALIZATION;

        trace_log_pop();

        return false;
    }

    trace_log_pop();

    return true;
}
#endif // ARENA_IMPLEMENTATION

USize http_service_ip_block_at_1(HTTP_Service_IP_Block *const self, char const *const ip) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "ip", (void*) ip);

    USize const index = http_service_ip_block_at_2(self, ip, char_length(ip));

    trace_log_pop();

    return index;
}

USize http_service_ip_block_at_2(HTTP_Service_IP_Block *const self, char const *const ip, USize const ip_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (ip_size == 0) {
        trace_log_pop();

        return USIZE_MAX;
    }

    error_check_null(LOG_METADATA, "ip", (void*) ip);

    /* Locked like the writers: eviction and remove swap the last record into a freed slot, so
     * an unlocked reader could compare against a record mid-swap. */
    thread_mutex_lock(&self->mutex);

    USize const index = _http_service_ip_block_find(self, ip, ip_size);

    thread_mutex_unlock(&self->mutex);

    trace_log_pop();

    return index;
}

USize http_service_ip_block_at_3(HTTP_Service_IP_Block *const self, Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    USize const index = http_service_ip_block_at_2(self, str_get_data(data), str_get_size(data));

    trace_log_pop();

    return index;
}

bool http_service_ip_block_blocked_1(HTTP_Service_IP_Block *const self, char const *const ip) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "ip", (void*) ip);

    bool const value = http_service_ip_block_blocked_2(self, ip, char_length(ip));

    trace_log_pop();

    return value;
}

bool http_service_ip_block_blocked_2(HTTP_Service_IP_Block *const self, char const *const ip, USize const ip_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (ip_size == 0) {
        trace_log_pop();

        return false;
    }

    error_check_null(LOG_METADATA, "ip", (void*) ip);

    thread_mutex_lock(&self->mutex);

    USize const index = _http_service_ip_block_find(self, ip, ip_size);
    bool        value = false;

    if (index != USIZE_MAX) {
        _HTTP_Service_IP_Block_Record *const record = &((_HTTP_Service_IP_Block_Record*) self->records)[index];

        /* Lazy expiry (report Critical 2): a block past `block_seconds` lifts on this read
         * rather than needing a background sweep. The timestamp is left as-is (report Misc 8),
         * matching add_strike's own lift (:396-399): refreshing it to now would make a
         * just-lifted, otherwise-idle record rank NEWEST for eviction and outlive records
         * belonging to real, currently-active visitors. */
        if (record->blocked && _http_service_ip_block_expired(record->timestamp, self->block_seconds)) {
            record->blocked = false;
            record->strikes = 0;
        }

        /* Strike-count expiration applies here too (report Misc 9), mirroring add_strike's own
         * check (:403-405) - without it, get_strikes on a quiet record kept reporting the stale
         * pre-expiry count until its NEXT strike, which is dishonest for an admin listing read
         * through blocked_2/get_strikes rather than a fresh strike. */
        if (self->expiration > 0 && _http_service_ip_block_expired(record->timestamp, self->expiration)) {
            record->strikes = 0;
        }

        value = record->blocked;
    }

    thread_mutex_unlock(&self->mutex);

    trace_log_pop();

    return value;
}

bool http_service_ip_block_blocked_3(HTTP_Service_IP_Block *const self, Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    bool const value = http_service_ip_block_blocked_2(self, str_get_data(data), str_get_size(data));

    trace_log_pop();

    return value;
}

void http_service_ip_block_clear(HTTP_Service_IP_Block *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    thread_mutex_lock(&self->mutex);

    self->count = 0;

    thread_mutex_unlock(&self->mutex);

    trace_log_pop();
}

bool http_service_ip_block_exists_1(HTTP_Service_IP_Block *const self, char const *const ip) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "ip", (void*) ip);

    bool const value = http_service_ip_block_exists_2(self, ip, char_length(ip));

    trace_log_pop();

    return value;
}

bool http_service_ip_block_exists_2(HTTP_Service_IP_Block *const self, char const *const ip, USize const ip_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (ip_size == 0) {
        trace_log_pop();

        return false;
    }

    error_check_null(LOG_METADATA, "ip", (void*) ip);

    thread_mutex_lock(&self->mutex);

    bool const value = _http_service_ip_block_find(self, ip, ip_size) != USIZE_MAX;

    thread_mutex_unlock(&self->mutex);

    trace_log_pop();

    return value;
}

bool http_service_ip_block_exists_3(HTTP_Service_IP_Block *const self, Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    bool const value = http_service_ip_block_exists_2(self, str_get_data(data), str_get_size(data));

    trace_log_pop();

    return value;
}

bool http_service_ip_block_get_blocked(HTTP_Service_IP_Block *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    thread_mutex_lock(&self->mutex);

    _HTTP_Service_IP_Block_Record const *const records = (_HTTP_Service_IP_Block_Record const*) self->records;

    /* Reports the same answer blocked_2 would, expiry included, rather than the raw flag: a
     * listing that showed a lapsed block as live would contradict the service's own refusals.
     * Unlike blocked_2 this does NOT clear the flag - a read by index is a listing, not a
     * request, and clearing here would let an admin page lift blocks by being refreshed. */
    bool const value = index < self->count                                                              &&
                       records[index].blocked                                                           &&
                       !_http_service_ip_block_expired(records[index].timestamp, self->block_seconds);

    thread_mutex_unlock(&self->mutex);

    trace_log_pop();

    return value;
}

bool http_service_ip_block_get_ip_copy(HTTP_Service_IP_Block *const self, USize const index, char *const buffer, USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "buffer", (void*) buffer);

    thread_mutex_lock(&self->mutex);

    bool copied = false;

    if (index < self->count) {
        _HTTP_Service_IP_Block_Record const *const record = &((_HTTP_Service_IP_Block_Record const*) self->records)[index];

        if (record->key_size < capacity) {
            memory_copy_1(buffer, record->key, record->key_size);

            buffer[record->key_size] = '\0';
            copied = true;
        }
    }

    thread_mutex_unlock(&self->mutex);

    trace_log_pop();

    return copied;
}

bool http_service_ip_block_get_record(HTTP_Service_IP_Block *const self, USize const index, HTTP_Service_IP_Block_Record *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "out", (void*) out);

    thread_mutex_lock(&self->mutex);

    bool copied = false;

    if (index < self->count) {
        _HTTP_Service_IP_Block_Record const *const record = &((_HTTP_Service_IP_Block_Record const*) self->records)[index];

        memory_set(out->address, sizeof(out->address), 0);
        memory_copy_1(out->address, record->key, record->key_size);

        /* Same expiry rule get_blocked uses (report Misc 10): a lapsed block reads as unblocked
         * here too, so a listing built from this snapshot cannot contradict the service's own
         * refusals. */
        out->blocked = record->blocked && !_http_service_ip_block_expired(record->timestamp, self->block_seconds);
        out->strikes = record->strikes;

        copied = true;
    }

    thread_mutex_unlock(&self->mutex);

    trace_log_pop();

    return copied;
}

USize http_service_ip_block_get_size(HTTP_Service_IP_Block *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    thread_mutex_lock(&self->mutex);

    USize const value = self->count;

    thread_mutex_unlock(&self->mutex);

    trace_log_pop();

    return value;
}

U16 http_service_ip_block_get_strikes(HTTP_Service_IP_Block *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    thread_mutex_lock(&self->mutex);

    U16 const value = index < self->count ? ((_HTTP_Service_IP_Block_Record*) self->records)[index].strikes : 0;

    thread_mutex_unlock(&self->mutex);

    trace_log_pop();

    return value;
}

bool http_service_ip_block_init_1(HTTP_Service_IP_Block *const self, U16 const limit) {
    trace_log_push(LOG_METADATA);

    bool const initialized = http_service_ip_block_init_2(self, limit, 0, 0);

    trace_log_pop();

    return initialized;
}

bool http_service_ip_block_init_2(HTTP_Service_IP_Block *const self, U16 const limit, U32 const expiration, U32 const block_seconds) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    *self = (HTTP_Service_IP_Block){
#ifdef ARENA_IMPLEMENTATION
        .allocator      = nullptr,
#endif // ARENA_IMPLEMENTATION
        .block_seconds  = block_seconds,
        .capacity       = _HTTP_SERVICE_IP_BLOCK_MAX_ENTRIES,
        .count          = 0,
        .expiration     = expiration,
        .limit          = limit,
        .records        = nullptr
    };

    /* try_borrow, not borrow: allocator_borrow ENDS THE PROCESS on a heap that cannot meet the
     * request, which would make the refusal below dead code, matching alloc_init_2's rationale
     * (:469-472) for the arena form. */
#ifdef ARENA_IMPLEMENTATION
    self->records = allocator_try_borrow(sizeof(_HTTP_Service_IP_Block_Record) * self->capacity, nullptr);
#else
    self->records = allocator_try_borrow(sizeof(_HTTP_Service_IP_Block_Record) * self->capacity);
#endif // ARENA_IMPLEMENTATION

    if (self->records == nullptr) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_ip_block: record array allocation failed - refusing to run without storage");

        *self = (HTTP_Service_IP_Block) DEFAULT_INITIALIZATION;

        trace_log_pop();

        return false;
    }

    /* In the caller's final storage, and reported rather than fatal; see
     * http_service_ip_block_alloc_init_2. */
    if (result_is_error(thread_mutex_init(&self->mutex))) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "http_service_ip_block: mutex init failed - refusing to run without locking");

#ifdef ARENA_IMPLEMENTATION
        allocator_release(self->records, nullptr);
#else
        allocator_release(self->records);
#endif // ARENA_IMPLEMENTATION

        *self = (HTTP_Service_IP_Block) DEFAULT_INITIALIZATION;

        trace_log_pop();

        return false;
    }

    trace_log_pop();

    return true;
}

void http_service_ip_block_remove_1(HTTP_Service_IP_Block *const self, char const *const ip) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "ip", (void*) ip);

    http_service_ip_block_remove_2(self, ip, char_length(ip));

    trace_log_pop();
}

void http_service_ip_block_remove_2(HTTP_Service_IP_Block *const self, char const *const ip, USize const ip_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (ip_size == 0) {
        trace_log_pop();

        return;
    }

    error_check_null(LOG_METADATA, "ip", (void*) ip);

    thread_mutex_lock(&self->mutex);

    USize const index = _http_service_ip_block_find(self, ip, ip_size);

    if (index != USIZE_MAX) {
        _HTTP_Service_IP_Block_Record *const records = (_HTTP_Service_IP_Block_Record*) self->records;

        records[index] = records[self->count - 1];
        self->count   -= 1;
    }

    thread_mutex_unlock(&self->mutex);

    trace_log_pop();
}

void http_service_ip_block_remove_3(HTTP_Service_IP_Block *const self, Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    http_service_ip_block_remove_2(self, str_get_data(data), str_get_size(data));

    trace_log_pop();
}

void http_service_ip_block_uninit(HTTP_Service_IP_Block *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

#ifdef ARENA_IMPLEMENTATION
    allocator_release(self->records, self->allocator);

    self->allocator = nullptr;
#else
    allocator_release(self->records);
#endif // ARENA_IMPLEMENTATION

    self->records   = nullptr;
    self->count     = 0;
    self->capacity  = 0;

    thread_mutex_uninit(&self->mutex);

    trace_log_pop();
}