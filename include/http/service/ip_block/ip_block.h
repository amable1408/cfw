/*
 * ip_block.h - HTTP IP blocking service for the C Libraries Framework
 * @version 0.4.0
 *
 * Tracks request strikes by IP and marks IPs as blocked after a configured
 * strike limit. Optional expiration lets strike counters cool down over time,
 * and optional block expiration lets a block itself lift automatically.
 *
 * Features:
 *   - Manual IP registration; add_strike also registers on demand.
 *   - A block SLIDES: every strike refreshes the record's timestamp, blocked or not, so
 *     block_seconds is a required QUIET period, not a fixed sentence - an address that keeps
 *     striking keeps restarting its own block.
 *   - Strike counting per IP, with optional strike-count expiration.
 *   - Block-state checks, with optional block expiration (lazy, checked on read).
 *   - Manual removal (remove_1/2/3) and full reset (clear).
 *   - Owned IP storage: one fixed-capacity record array, no per-entry heap allocation.
 *
 * Usage Example:
 *   @code
 *   HTTP_Service_IP_Block block = DEFAULT_INITIALIZATION;
 *
 *   // limit 5, strikes never expire, blocks lift after 1 hour
 *   if (!http_service_ip_block_init_2(&block, 5, 0, 3600)) {
 *       // Memory Management: false means this instance does not exist - fail startup.
 *   }
 *
 *   http_service_ip_block_add_strike_1(&block, "::1"); // registers "::1" if absent, then strikes
 *
 *   if (http_service_ip_block_blocked_1(&block, "::1")) {
 *       // reject request
 *   }
 *
 *   http_service_ip_block_uninit(&block);
 *   @endcode
 *
 * Client address:
 *   - Every ip/data parameter here MUST already be the resolved CLIENT address - this module
 *     has no address story of its own. Behind a reverse proxy (Caddy et al.) the raw peer
 *     address is the proxy, not the client, and striking it blocks every visitor at once. Callers
 *     resolve the real client via the server layer's trusted-hop X-Forwarded-For walk before
 *     calling in here; this header intentionally does not duplicate that logic.
 *
 * Address semantics:
 *   - Every add/add_strike/at/blocked/exists call parses its `ip` text via
 *     net_socket_address_init_2 and compares a NORMALIZED key instead of the literal text
 *     whenever that parse succeeds: a plain IPv4 literal compares as its 4 raw address bytes; an
 *     IPv6 literal compares as its /64 network-prefix (the high 8 bytes, host part dropped) so an
 *     ISP rotating a client within its own /64 keeps hitting one entry; an IPv4-mapped IPv6
 *     literal ("::ffff:1.2.3.4") is unwrapped to the same 4-byte form as "1.2.3.4" rather than
 *     /64-bucketed, so the two unify to one entry. The original text is still kept alongside the
 *     normalized key (see http_service_ip_block_get_ip_copy) purely so an admin listing stays
 *     human-readable - it is never itself compared once normalization succeeds.
 *   - Text that parses as neither IPv4 nor IPv6 (malformed - untrusted network input, or simply
 *     too long to attempt) falls back to the pre-existing opaque byte-string comparison rather
 *     than erroring or aborting; two different malformed strings never collide with each other
 *     or with a real address. Two SPELLINGS a caller might expect to work land in that fallback:
 *     a bracketed literal ("[::1]") and a zone-id literal ("fe80::1%eth0") both fail the parse,
 *     so they are tracked as raw text and do NOT unify with the same address written plainly.
 *     Strip the brackets and the zone before calling if a caller can produce either.
 *
 * Error Handling:
 *   - Public functions validate non-null pointers.
 *   - An empty IP is treated as UNTRACKED, never an error - including an empty Str, whose
 *     data pointer is null. Network-derived input must not be able to abort the server.
 *   - An address of _HTTP_SERVICE_IP_BLOCK_KEY_SIZE bytes or longer is also treated as
 *     UNTRACKED (logged at WARN) rather than truncated or aborted - no real IPv4/IPv6 text
 *     representation is anywhere close to that long.
 *
 * Thread Safety:
 *   - Every public operation, query included, takes this instance's own mutex.
 *   - Instances are independent: each owns its lock, so destroying one does not
 *     affect another. The lock lives in the struct, which is why the constructors
 *     initialize in place instead of returning the service by value.
 *
 * Memory Management:
 *   - IP strings are copied into one fixed-capacity record array, allocated once at init and
 *     released once at uninit - not four growing parallel lists. Arena-backed init (see
 *     http_service_ip_block_alloc_init_1/_2) genuinely uses the allocator for that one
 *     allocation; there is no per-entry allocation left to defeat an arena's inability to
 *     free piecemeal.
 *   - Call http_service_ip_block_uninit() when finished.
 *   - A false return from any init/alloc_init leaves *self ZEROED, not initialized - the mutex
 *     was never successfully constructed, only its storage cleared. Do not call ANY other
 *     function on that instance afterward, uninit included: thread_mutex_uninit on a mutex that
 *     was zeroed but never thread_mutex_init'd is not the same as one that ran its full
 *     lifecycle. Treat a false return as "this instance does not exist" and fail startup.
 *
 * Performance Characteristics:
 *   - Lookups are linear over the tracked records, up to the fixed capacity
 *     (_HTTP_SERVICE_IP_BLOCK_MAX_ENTRIES, currently 4096). Measured at 22.5 us per blocked_2
 *     call with the table full at 4096 entries (decision log, 2026-09-06); revisit a hashset
 *     backing only if a real deployment's profile disagrees.
 *   - Intended for small local deny/strike lists.
 *   - Admin listing: get_record snapshots one row (address, strikes, blocked) under a single lock
 *     acquisition. The older get_ip_copy/get_strikes/get_blocked trio takes three separate
 *     acquisitions per row, during which insert/evict on another thread can move the record
 *     array, so the row a listing route assembles from the trio can mix fields from different
 *     underlying records; get_record cannot.
 *   - Eviction trade-off: registering a new address when the table is full first evicts the
 *     OLDEST record NOT under a live block - a block whose block_seconds have already elapsed
 *     counts as unblocked here, since it lifts on its next read anyway. When every tracked
 *     record is under a live block (a flood of distinct addresses all still serving their time),
 *     the table then evicts the OLDEST of them instead of refusing the new registration. This is
 *     deliberately fail-closed for the attacker currently hitting the limit (their strike is
 *     still recorded and can still block them) and never fail-open for a brand-new one (a full
 *     table of blocks never becomes a free pass just because it filled up); the cost is that the
 *     single oldest blocked entry can cycle back in unblocked once evicted.
 *
 * Dependencies:
 *   - chrono, net, str, thread.
 *
 * See ip_block.c for implementation details.
 */

#ifndef HTTP_SERVICE_IP_BLOCK_H
#define HTTP_SERVICE_IP_BLOCK_H

#include <chrono/chrono.h>
#include <container/str/str.h>
#include <thread/thread.h>

/*==============================================================================
 * MARK: - Macros
 *============================================================================*/

/** @brief Buffer size http_service_ip_block_get_record's address field needs, NUL included. */
#define HTTP_SERVICE_IP_BLOCK_ADDRESS_SIZE 64

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief One record snapshot, read under a single lock by http_service_ip_block_get_record - the
 *        one-lock-per-row alternative to the get_ip_copy/get_strikes/get_blocked trio, whose three
 *        separate lock acquisitions let the record array move between them.
 */
typedef struct {
    /** @brief The tracked address, NUL-terminated. Empty when the read was out of range. */
    char address[HTTP_SERVICE_IP_BLOCK_ADDRESS_SIZE];
    /** @brief true when this record is blocked AND its block has not yet expired. */
    bool blocked;
    /** @brief Strike count. */
    U16 strikes;
} HTTP_Service_IP_Block_Record;

/**
 * @brief In-memory IP strike and block registry.
 */
typedef struct {
#ifdef ARENA_IMPLEMENTATION
    /** @brief Optional arena backing the one record-array allocation below. */
    Arena *allocator;
#endif // ARENA_IMPLEMENTATION
    /** @brief Block lifetime in seconds, checked lazily on read. Zero means blocks never expire. */
    U32 block_seconds;
    /** @brief Number of record slots allocated at init - the ceiling `count` can reach. */
    USize capacity;
    /** @brief Number of tracked records currently in use. */
    USize count;
    /** @brief Strike-count expiration time in seconds. Zero disables expiration. */
    U32 expiration;
    /** @brief Strike count needed to block an IP. */
    U16 limit;
    /**
     * @brief Guards every access to `records`/`count`.
     *
     * Per instance, not file-static. One shared lock meant uninit of ANY instance destroyed the
     * lock every other instance was still using, and each init re-initialized a possibly-locked
     * mutex - both undefined behaviour on CRITICAL_SECTION and pthreads alike.
     */
    ThreadMutex mutex;
    /** @brief Fixed-capacity record array, allocated once at init. Opaque to callers. */
    void *records;
} HTTP_Service_IP_Block;

/*==============================================================================
 * MARK: - API
 *============================================================================*/

/**
 * @brief Register an IP using a null-terminated string, without striking it.
 * @param self Service instance.
 * @param ip Null-terminated IP string.
 * @return true when the IP is tracked after the call (newly registered or already present);
 *         false when declined (empty or oversize address). Never declines solely because the
 *         table is full - see ip_block.h Performance Characteristics for the eviction trade-off
 *         that makes room instead. Mirrors hashset's counting add.
 */
bool http_service_ip_block_add_1(HTTP_Service_IP_Block *const self, char const *const ip);

/**
 * @brief Register an IP using explicit byte length, without striking it.
 * @param self Service instance.
 * @param ip IP string data.
 * @param ip_size IP string size.
 * @return See http_service_ip_block_add_1.
 */
bool http_service_ip_block_add_2(HTTP_Service_IP_Block *const self, char const *const ip, USize const ip_size);

/**
 * @brief Register an IP from a Str value, without striking it.
 * @param self Service instance.
 * @param data IP string.
 * @return See http_service_ip_block_add_1.
 */
bool http_service_ip_block_add_3(HTTP_Service_IP_Block *const self, Str const *const data);

/**
 * @brief Add one strike to an IP using a null-terminated string, registering it first if absent.
 * @param self Service instance.
 * @param ip Null-terminated IP string.
 * @return true when the strike was recorded, false when the address could not be tracked at all -
 *         an empty address, or one at or past the key size. A false
 *         answer means the striker is NOT being counted, which is the one case a caller may want
 *         to log; add_* already answered the same way, and this closes the gap where an oversize
 *         address's strike vanished silently.
 */
bool http_service_ip_block_add_strike_1(HTTP_Service_IP_Block *const self, char const *const ip);

/**
 * @brief Add one strike to an IP using explicit byte length, registering it first if absent.
 * @param self Service instance.
 * @param ip IP string data.
 * @param ip_size IP string size.
 * @return true when the strike was recorded, false when the address could not be tracked at all -
 *         an empty address, or one at or past the key size. A false
 *         answer means the striker is NOT being counted, which is the one case a caller may want
 *         to log; add_* already answered the same way, and this closes the gap where an oversize
 *         address's strike vanished silently.
 */
bool http_service_ip_block_add_strike_2(HTTP_Service_IP_Block *const self, char const *const ip, USize const ip_size);

/**
 * @brief Add one strike to an IP from a Str value, registering it first if absent.
 * @param self Service instance.
 * @param data IP string.
 * @return true when the strike was recorded, false when the address could not be tracked at all -
 *         an empty address, or one at or past the key size. A false
 *         answer means the striker is NOT being counted, which is the one case a caller may want
 *         to log; add_* already answered the same way, and this closes the gap where an oversize
 *         address's strike vanished silently.
 */
bool http_service_ip_block_add_strike_3(HTTP_Service_IP_Block *const self, Str const *const data);

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Initialize an arena-backed IP block service without expiration.
 * @param self Service to initialize IN PLACE.
 * @param limit Strike count that marks an IP as blocked.
 * @param allocator Arena allocator, genuinely backing the one record-array allocation.
 * @return true when initialized; false when the arena refused the allocation or the mutex could
 *         not be initialized - in both cases *self is ZEROED and must NOT be used again, not
 *         even passed to uninit (see ip_block.h Memory Management); the caller should fail
 *         startup rather than run with no IP-blocking storage or no locking.
 * @note Writes through self rather than returning the service, because it owns a ThreadMutex
 *       and neither CRITICAL_SECTION nor pthread_mutex_t may be copied once initialized - a
 *       by-value return would hand the caller a copy of an initialized lock.
 */
bool http_service_ip_block_alloc_init_1(HTTP_Service_IP_Block *const self, U16 const limit, Arena *const allocator);

/**
 * @brief Initialize an arena-backed IP block service with expiration.
 * @param self Service to initialize IN PLACE.
 * @param limit Strike count that marks an IP as blocked.
 * @param expiration Strike-count expiration time in seconds; zero disables it.
 * @param block_seconds Block lifetime in seconds; zero means a block never lifts on its own.
 * @param allocator Arena allocator.
 * @return true when initialized; see http_service_ip_block_alloc_init_1 for the false case.
 * @note Writes through self; see http_service_ip_block_alloc_init_1 for why.
 */
bool http_service_ip_block_alloc_init_2(HTTP_Service_IP_Block *const self, U16 const limit, U32 const expiration, U32 const block_seconds, Arena *const allocator);
#endif // ARENA_IMPLEMENTATION

/*
 * The query functions below - at_*, blocked_*, exists_*, get_* - take a NON-const self on
 * purpose. They read no more than they ever did, but each one now acquires this instance's
 * mutex, and locking mutates. Declaring them const and casting the qualifier away internally
 * would be the usual "logically const" dodge; it is rejected here because the honest signature
 * costs nothing (no caller in the tree holds a const service) and the cast would be genuine
 * undefined behaviour the day someone does declare one const.
 */

/**
 * @brief Find an IP index using a null-terminated string.
 * @param self Service instance. Non-const: the lookup takes the instance lock.
 * @param ip Null-terminated IP string.
 * @return IP index, or USIZE_MAX when not found. Zero is a VALID index, so it can never
 *         double as the absent marker; this matches string_find_*'s SIZE_MAX convention.
 *         Indices are NOT stable across add/remove/clear - removal swaps the last record into
 *         the removed slot, so callers must not cache one across a mutating call.
 */
USize http_service_ip_block_at_1(HTTP_Service_IP_Block *const self, char const *const ip);

/**
 * @brief Find an IP index using explicit byte length.
 * @param self Service instance.
 * @param ip IP string data.
 * @param ip_size IP string size.
 * @return See http_service_ip_block_at_1.
 */
USize http_service_ip_block_at_2(HTTP_Service_IP_Block *const self, char const *const ip, USize const ip_size);

/**
 * @brief Find an IP index from a Str value.
 * @param self Service instance.
 * @param data IP string.
 * @return See http_service_ip_block_at_1.
 */
USize http_service_ip_block_at_3(HTTP_Service_IP_Block *const self, Str const *const data);

/**
 * @brief Check whether an IP is blocked using a null-terminated string.
 * @param self Service instance.
 * @param ip Null-terminated IP string.
 * @return true when blocked. A block past `block_seconds` old lifts as a side effect of this
 *         call (strikes reset to zero) rather than needing a separate sweep.
 */
bool http_service_ip_block_blocked_1(HTTP_Service_IP_Block *const self, char const *const ip);

/**
 * @brief Check whether an IP is blocked using explicit byte length.
 * @param self Service instance.
 * @param ip IP string data.
 * @param ip_size IP string size.
 * @return See http_service_ip_block_blocked_1.
 */
bool http_service_ip_block_blocked_2(HTTP_Service_IP_Block *const self, char const *const ip, USize const ip_size);

/**
 * @brief Check whether an IP is blocked from a Str value.
 * @param self Service instance.
 * @param data IP string.
 * @return See http_service_ip_block_blocked_1.
 */
bool http_service_ip_block_blocked_3(HTTP_Service_IP_Block *const self, Str const *const data);

/**
 * @brief Remove every tracked IP, keeping the allocated storage for reuse.
 * @param self Service instance.
 */
void http_service_ip_block_clear(HTTP_Service_IP_Block *const self);

/**
 * @brief Check whether an IP exists using a null-terminated string.
 * @param self Service instance.
 * @param ip Null-terminated IP string.
 * @return true when tracked.
 */
bool http_service_ip_block_exists_1(HTTP_Service_IP_Block *const self, char const *const ip);

/**
 * @brief Check whether an IP exists using explicit byte length.
 * @param self Service instance.
 * @param ip IP string data.
 * @param ip_size IP string size.
 * @return true when tracked.
 */
bool http_service_ip_block_exists_2(HTTP_Service_IP_Block *const self, char const *const ip, USize const ip_size);

/**
 * @brief Check whether an IP exists from a Str value.
 * @param self Service instance.
 * @param data IP string.
 * @return true when tracked.
 */
bool http_service_ip_block_exists_3(HTTP_Service_IP_Block *const self, Str const *const data);

/**
 * @brief Read the block state stored at a record index.
 * @param self Service instance.
 * @param index Record index in [0, http_service_ip_block_get_size(self)).
 * @return true when that record is blocked AND its block has not yet expired, false when it is
 *         not blocked, its block has lapsed, or index is out of range. Completes the
 *         admin-listing trio with get_ip_copy and get_strikes: without it a listing route could
 *         show who had struck but not who was actually being refused.
 */
bool http_service_ip_block_get_blocked(HTTP_Service_IP_Block *const self, USize const index);

/**
 * @brief Copy the address text stored at a record index into a caller-owned buffer (for an admin
 *        listing route).
 * @param self Service instance.
 * @param index Record index in [0, http_service_ip_block_get_size(self)).
 * @param buffer Destination buffer.
 * @param capacity Destination buffer capacity.
 * @return true when index was in range and the address (NUL included) fit within capacity, in
 *         which case buffer now holds it; false when index is out of range or the address did
 *         not fit - buffer is left untouched in both failure cases. Replaces a prior
 *         get_ip that returned a pointer BORROWED into this instance's storage: valid only
 *         until the next mutating call, which a caller could easily hold past after the lock
 *         this call took internally was released. Copying out under the lock removes that trap.
 */
bool http_service_ip_block_get_ip_copy(HTTP_Service_IP_Block *const self, USize const index, char *const buffer, USize const capacity);

/**
 * @brief Snapshot one record - address, strikes, and block state - under a single lock
 *        acquisition (report Misc 10). Prefer this over the get_ip_copy/get_strikes/get_blocked
 *        trio for an admin listing route: reading a row through three separate calls lets the
 *        record array move (insert/evict) between them, so the address, strike count and block
 *        state in the caller's assembled row can each come from a DIFFERENT underlying record.
 * @param self Service instance.
 * @param index Record index in [0, http_service_ip_block_get_size(self)).
 * @param out Destination snapshot; untouched when index is out of range.
 * @return true when index was in range and *out now holds the snapshot; false otherwise.
 */
bool http_service_ip_block_get_record(HTTP_Service_IP_Block *const self, USize const index, HTTP_Service_IP_Block_Record *const out);

/**
 * @brief Number of tracked records.
 * @param self Service instance.
 * @return Current record count.
 */
USize http_service_ip_block_get_size(HTTP_Service_IP_Block *const self);

/**
 * @brief Read the strike count stored at a record index.
 * @param self Service instance.
 * @param index Record index in [0, http_service_ip_block_get_size(self)).
 * @return Strike count, or 0 when index is out of range.
 */
U16 http_service_ip_block_get_strikes(HTTP_Service_IP_Block *const self, USize const index);

/**
 * @brief Initialize an IP block service without expiration.
 * @param self Service to initialize IN PLACE.
 * @param limit Strike count that marks an IP as blocked.
 * @return true when initialized; see http_service_ip_block_alloc_init_1 for the false case.
 * @note Writes through self; see http_service_ip_block_alloc_init_1 for why it cannot return
 *       the service by value.
 */
bool http_service_ip_block_init_1(HTTP_Service_IP_Block *const self, U16 const limit);

/**
 * @brief Initialize an IP block service with expiration.
 * @param self Service to initialize IN PLACE.
 * @param limit Strike count that marks an IP as blocked.
 * @param expiration Strike-count expiration time in seconds; zero disables it.
 * @param block_seconds Block lifetime in seconds; zero means a block never lifts on its own.
 * @return true when initialized; see http_service_ip_block_alloc_init_1 for the false case.
 * @note Writes through self; see http_service_ip_block_alloc_init_1 for why.
 */
bool http_service_ip_block_init_2(HTTP_Service_IP_Block *const self, U16 const limit, U32 const expiration, U32 const block_seconds);

/**
 * @brief Remove a tracked IP using a null-terminated string. A no-op when untracked.
 * @param self Service instance.
 * @param ip Null-terminated IP string.
 */
void http_service_ip_block_remove_1(HTTP_Service_IP_Block *const self, char const *const ip);

/**
 * @brief Remove a tracked IP using explicit byte length. A no-op when untracked.
 * @param self Service instance.
 * @param ip IP string data.
 * @param ip_size IP string size.
 */
void http_service_ip_block_remove_2(HTTP_Service_IP_Block *const self, char const *const ip, USize const ip_size);

/**
 * @brief Remove a tracked IP from a Str value. A no-op when untracked.
 * @param self Service instance.
 * @param data IP string.
 */
void http_service_ip_block_remove_3(HTTP_Service_IP_Block *const self, Str const *const data);

/**
 * @brief Release all service storage.
 * @param self Service instance.
 */
void http_service_ip_block_uninit(HTTP_Service_IP_Block *const self);

#endif // HTTP_SERVICE_IP_BLOCK_H