/*
 * hashset.h - Open-addressing string hash set (with counts) for the CFW framework
 *
 * Features:
 *   - Hash set of NUL-terminated string keys with O(1) expected lookup/insert
 *   - Per-key occurrence count: doubles as a frequency counter / histogram, while
 *     hashset_contains gives plain-set semantics (count > 0)
 *   - Two key-ownership modes: hashset_add borrows the caller's pointer (zero-copy;
 *     the key must outlive the set), hashset_add_static / hashset_add_static_2 copy
 *     the key so the set owns a stable copy. Both may be mixed; only owned copies
 *     are freed.
 *   - Heap or arena backed, following the CFW container convention; grows and
 *     rehashes automatically past a load-factor threshold.
 *   - hashset_next walks every stored key/count pair - the sanctioned way to read
 *     the histogram back out.
 *
 * Usage Examples:
 *   @code
 *   HashSet seen = hashset_init_1();
 *   hashset_add_static(&seen, "alpha");      // owned copy
 *   if (!hashset_contains(&seen, "beta")) { ... }
 *   U32 const n = hashset_count(&seen, "alpha");
 *   hashset_uninit(&seen);
 *
 *   // arena-backed, borrowing arena-owned keys (zero-copy):
 *   HashSet hosts = hashset_alloc_init_2(2048, &arena);
 *   hashset_add(&hosts, arena_owned_host);
 *
 *   // add answers whether the key was new, so a caller stops double-probing:
 *   if (hashset_add(&hosts, url) == 1) {
 *       fetch(url);
 *   }
 *
 *   // enumerate every stored key and its count:
 *   USize cursor = 0;
 *   char const *key = nullptr;
 *   U32 count = 0;
 *
 *   while (hashset_next(&seen, &cursor, &key, &count)) {
 *       printf("%s: %u\n", key, count);
 *   }
 *   @endcode
 *
 * Error Handling:
 *   Contract violations - a null self, a null key, and a zero capacity hint to
 *   init_2 / alloc_init_2 / new_2 / alloc_new_2 - go through error_check_*, which
 *   LOGS AND ABORTS the process. hashset_get_capacity, hashset_empty and
 *   hashset_size all abort on a null self too, matching the rest of the
 *   container family.
 *
 *   Conditions that depend on a VALUE rather than on a broken contract are
 *   refused instead, and never abort:
 *     - an allocator that REFUSES - a null-handler arena, or a heap allocation
 *       that fails - leaves the set unchanged. A declined BUILD (init/alloc_init)
 *       lands at capacity 0 with every operation inert; a declined KEY COPY in
 *       add_static / add_static_2 declines that add with size unchanged and
 *       nothing leaked; a declined GROW keeps the old table, and the insert that
 *       triggered it then refuses too, at size == capacity, rather than walking a
 *       full table looking for a slot that is not there. Every borrow this
 *       module makes goes through allocator_try_borrow, so this holds on the
 *       heap AND on an exhausted arena, not only on a refused one.
 *     - an empty key - "" for the NUL-terminated entry points, or a sized key
 *       with key_size 0 for the _2 forms - is refused BY POLICY: the set never
 *       stores it, so contains/count answer miss/0 rather than occupying a slot
 *       for it. This is a data choice, not crash avoidance.
 *     - for the sized _2 forms, key_size is refused as DATA - not aborted -
 *       when it is 0, USIZE_MAX, or a slice spanning an embedded NUL:
 *       USIZE_MAX would wrap the copy's allocation size to 0 in add_static_2,
 *       and an embedded NUL would make a stored key unfindable by later probes.
 *     - hashset_add / hashset_add_static / hashset_add_static_2 return the key's
 *       post-call COUNT: 0 means declined (null/empty key, capacity 0, a
 *       declined copy, or a full table after a failed grow), 1 means newly
 *       inserted, n means an existing key's incremented count. The count
 *       saturates at U32_MAX instead of wrapping to 0, which would otherwise
 *       contradict hashset_contains (still true) with hashset_count (back to 0).
 *
 *   With ERROR_CHECK_ENABLED off these checks compile out; each one guards a
 *   real dereference immediately below it, so a violated contract becomes
 *   undefined behaviour rather than a graceful failure. The REFUSALS above are
 *   runtime branches on VALUES, not contract checks, and hold either way.
 *
 * Thread Safety:
 *   - Not thread-safe. Synchronize external access to a shared set.
 *
 * Memory Management:
 *   - The bucket arrays are drawn from the set's allocator (heap, or the arena
 *     passed to hashset_alloc_init_* / hashset_alloc_new_*). hashset_clear frees
 *     owned copies and empties the set; hashset_uninit additionally frees the
 *     bucket arrays, leaving the set DEAD until re-initialized - add on it is
 *     then a silent no-op, not a usable empty set. With an arena, frees defer to
 *     the arena's own lifetime.
 *   - hashset_add takes @p key BORROWED: the set never mutates or frees it, and
 *     the caller keeps ownership and must keep it alive for as long as the set
 *     may read it. hashset_add_static / hashset_add_static_2 copy the key
 *     instead, so the set owns a stable copy independent of the caller's buffer
 *     and the original may be freed or go out of scope immediately.
 *   - The `keys`, `counts`, `owned`, `capacity`, `size` and `seed` fields are
 *     INTERNAL bookkeeping, not a public API - a caller never reads or writes
 *     them directly. Read the count/capacity/size through hashset_count /
 *     hashset_get_capacity / hashset_size, and enumerate through hashset_next;
 *     hand-rolling a loop over the arrays is not supported and their layout may
 *     change.
 *
 * Performance Characteristics:
 *   - Expected O(1) lookup/insert; capacity is a power of two (mask, not
 *     modulo). Growth doubles capacity and rehashes when load exceeds ~70%; a
 *     duplicate add never triggers a grow it does not need - a miss probes
 *     first and only grows before inserting a genuinely new key. After a
 *     declined grow the table can fill completely; from then on a miss costs
 *     O(capacity) - probing has no empty slot to stop at, so it wraps the
 *     whole table before concluding absence - until a later grow succeeds or
 *     hashset_clear / hashset_uninit reopens slots.
 *   - hashset_next is O(capacity) for a full walk and O(1) amortized per call;
 *     zero cost when never called. Any add, clear or uninit invalidates a
 *     cursor in progress.
 *   - The hash is FNV-1a-64, carried in a U64 accumulator and folded once to
 *     USize at the end - correct on both 32-bit and 64-bit builds, unlike a
 *     32-bit accumulator seeded with the 64-bit constants (which truncates the
 *     prime and degrades probe chains on 32-bit Android ABIs). Each set XORs a
 *     per-set seed into the FNV basis at construction, drawn from a cheap
 *     non-cryptographic source (the set's own stack address mixed with a
 *     process-lifetime counter - no new dependency). This defeats an offline,
 *     precomputed multicollision set built against a fixed basis; it does NOT
 *     defend against an adaptive attacker who can observe this process's
 *     behavior and adjust keys per set, and it is not a keyed MAC.
 *
 * Dependencies:
 *   - <char/char.h> - the only direct include, and the module's real
 *     dependency surface. It transitively supplies allocator/ (allocator_try_borrow,
 *     allocator_release), memory/ (memory_empty) and, under ARENA_IMPLEMENTATION,
 *     arena/ - all of which this module uses directly.
 *
 * See hashset.c for implementation details.
 */
#ifndef CONTAINER_HASHSET_HASHSET_H
#define CONTAINER_HASHSET_HASHSET_H

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/

#include <stdatomic.h>

#include <char/char.h>

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief A string-keyed counting hash set.
 * @note Every field past `allocator` is INTERNAL bookkeeping - see the header's
 *       Memory Management section. Use hashset_get_capacity / hashset_size /
 *       hashset_next rather than reading these directly.
 * @note Per-key counts are U32 and saturate at U32_MAX (see hashset_add);
 *       capacity and size are USize, matching the rest of the container family.
 */
typedef struct {
#ifdef ARENA_IMPLEMENTATION
    /** @brief Backing arena, or null for heap allocation. INTERNAL. */
    Arena *allocator;
#endif // ARENA_IMPLEMENTATION
    /** @brief Bucket keys; a null entry marks an empty slot. INTERNAL. */
    char **keys;
    /** @brief Per-bucket occurrence counts, parallel to @ref keys. INTERNAL. */
    U32 *counts;
    /** @brief Per-bucket ownership flags: true when the set must free the key. INTERNAL. */
    bool *owned;
    /** @brief Bucket count; a power of two, or 0 when unallocated. INTERNAL. */
    USize capacity;
    /** @brief Number of distinct keys stored. INTERNAL. */
    USize size;
    /** @brief Per-set hash seed, XORed into the FNV basis. INTERNAL. */
    U64 seed;
} HashSet;

/*==============================================================================
 * MARK: - API Functions
 *============================================================================*/

/**
 * @brief Insert a key (count 1) or increment its count, borrowing the caller's
 *        pointer without copying. The key must outlive the set.
 * @param self Set. Must not be nullptr.
 * @param key  Caller-owned, NUL-terminated key. Must not be nullptr; an empty
 *             key is refused by policy (see Error Handling), not aborted.
 *             BORROWED - the set never mutates or frees it; ownership and
 *             lifetime stay with the caller.
 * @return The key's count after this call: 0 when declined, 1 when newly
 *         inserted, n for an existing key's incremented (and possibly
 *         saturated) count.
 * @see hashset_add_static
 */
U32 hashset_add(HashSet *const self, char const *const key);

/**
 * @brief Insert a key (count 1) or increment its count, copying the key so the
 *        set owns a stable copy independent of the caller's buffer.
 * @param self Set. Must not be nullptr.
 * @param key  NUL-terminated key to copy in. Must not be nullptr; an empty key
 *             is refused by policy, not aborted.
 * @return The key's count after this call: 0 when declined (including a
 *         declined copy), 1 when newly inserted, n for an existing key's
 *         incremented (and possibly saturated) count.
 * @see hashset_add
 * @see hashset_add_static_2
 */
U32 hashset_add_static(HashSet *const self, char const *const key);

/**
 * @brief Insert a key of explicit size (count 1) or increment its count,
 *        copying the key so the set owns a stable, NUL-terminated copy.
 * @param self     Set. Must not be nullptr.
 * @param key      Key bytes. Must not be nullptr; NEED NOT be NUL-terminated.
 * @param key_size Key length in bytes. Refused as data: size 0, USIZE_MAX, or
 *                 a slice spanning an embedded NUL. MUST NOT exceed the
 *                 readable bytes at `key` - the size reaches the copy as a
 *                 read length, so an over-large one is a heap over-read
 *                 rather than a decline.
 * @return The key's count after this call, same meaning as hashset_add_static.
 * @note What is STORED is always NUL-terminated, whatever was passed in - this
 *       is the sized twin of add_static, for a key that is a slice of a larger
 *       buffer.
 * @see hashset_add_static
 */
U32 hashset_add_static_2(HashSet *const self, char const *const key, USize const key_size);

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Initialize an empty arena-backed set sized for 16 keys (32 buckets at
 *        the load factor).
 * @param allocator Arena the set draws from. Must not be nullptr.
 * @return Initialized set.
 */
HashSet hashset_alloc_init_1(Arena *const allocator);

/**
 * @brief Initialize an empty arena-backed set sized for at least @p capacity keys.
 * @param capacity  Number of KEYS to size for; rounded up so that many keys sit
 *                   at or under the ~70% load factor, then to a power of two
 *                   buckets. Must not be 0 - a CALLER CONTRACT, checked here.
 * @param allocator Arena the set draws from. Must not be nullptr.
 * @return Initialized set; hashset_get_capacity answers 0 if allocation or the
 *         overflow guard declined (an overlong hint rounds to a byte count
 *         that would wrap).
 */
HashSet hashset_alloc_init_2(USize const capacity, Arena *const allocator);

/**
 * @brief Allocate and initialize an empty arena-backed set on the arena.
 * @param allocator Arena the set (and the struct) draws from. Must not be nullptr.
 * @return New set, or nullptr - one shape covers both the struct borrow being
 *         refused and the built set landing at capacity 0 (a declined bucket
 *         allocation); the latter releases the struct before answering
 *         nullptr, so nothing is leaked either way. Free with hashset_delete.
 * @note A nullptr must NOT be handed to hashset_delete, which treats a null
 *       *self as a contract violation and aborts. Check before deleting.
 */
HashSet* hashset_alloc_new_1(Arena *const allocator);

/**
 * @brief Allocate and initialize an arena-backed set sized for @p capacity keys.
 * @param capacity  Number of keys to size for. Must not be 0.
 * @param allocator Arena the set (and the struct) draws from. Must not be nullptr.
 * @return New set, or nullptr - one shape covers both the struct borrow being
 *         refused and the built set landing at capacity 0 (a declined bucket
 *         allocation); the latter releases the struct before answering
 *         nullptr, so nothing is leaked either way. Free with hashset_delete.
 * @note A nullptr must NOT be handed to hashset_delete, which treats a null
 *       *self as a contract violation and aborts. Check before deleting.
 */
HashSet* hashset_alloc_new_2(USize const capacity, Arena *const allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Free owned copies and empty the set, keeping its capacity.
 * @param self Set. Must not be nullptr.
 */
void hashset_clear(HashSet *const self);

/**
 * @brief Report whether a NUL-terminated key is present (count > 0).
 * @param self Set. Must not be nullptr.
 * @param key  Key to look up. Must not be nullptr; an empty key answers false
 *             by policy, not by aborting.
 * @return true when the key is present.
 */
bool hashset_contains(HashSet const *const self, char const *const key);

/**
 * @brief Report whether a key of explicit size is present (count > 0).
 * @param self     Set. Must not be nullptr.
 * @param key      Key bytes. Must not be nullptr; NEED NOT be NUL-terminated.
 * @param key_size Key length in bytes. Refused as data: size 0, USIZE_MAX, or
 *                 a slice spanning an embedded NUL. Same read-extent contract
 *                 as add_static_2.
 * @return true when the key is present.
 */
bool hashset_contains_2(HashSet const *const self, char const *const key, USize const key_size);

/**
 * @brief Read the occurrence count stored for a NUL-terminated key.
 * @param self Set. Must not be nullptr.
 * @param key  Key to look up. Must not be nullptr; an empty key answers 0 by
 *             policy, not by aborting.
 * @return The key's count, or 0 when absent.
 */
U32 hashset_count(HashSet const *const self, char const *const key);

/**
 * @brief Read the occurrence count stored for a key of explicit size.
 * @param self     Set. Must not be nullptr.
 * @param key      Key bytes. Must not be nullptr; NEED NOT be NUL-terminated.
 * @param key_size Key length in bytes. Refused as data: size 0, USIZE_MAX, or
 *                 a slice spanning an embedded NUL. Same read-extent contract
 *                 as add_static_2.
 * @return The key's count, or 0 when absent.
 */
U32 hashset_count_2(HashSet const *const self, char const *const key, USize const key_size);

/**
 * @brief Release a set allocated by hashset_new_* or hashset_alloc_new_*.
 * @param self Address of the set pointer. Must not be nullptr, and neither may *self.
 * @note Releases every owned key and the bucket arrays (via hashset_uninit),
 *       then the struct itself. Nulls the caller's pointer only under
 *       MEMORY_NON_DANGLING_POINTER, matching the rest of the family.
 */
void hashset_delete(HashSet **const self);

/**
 * @brief Report whether the set holds no keys.
 * @param self Set. Must not be nullptr.
 * @return true when there are no keys.
 * @note "Empty" means no keys, never "no allocation" - a set with reserved
 *       capacity and nothing in it is empty.
 */
bool hashset_empty(HashSet const *const self);

/**
 * @brief Report the set's bucket capacity.
 * @param self Set. Must not be nullptr.
 * @return The bucket count (a power of two), or 0 for an unallocated set.
 */
USize hashset_get_capacity(HashSet const *const self);

/**
 * @brief Initialize an empty heap-backed set sized for 16 keys (32 buckets at
 *        the load factor).
 * @return Initialized set.
 */
HashSet hashset_init_1(void);

/**
 * @brief Initialize an empty heap-backed set sized for at least @p capacity keys.
 * @param capacity Number of KEYS to size for; rounded up so that many keys sit
 *                  at or under the ~70% load factor, then to a power of two
 *                  buckets. Must not be 0 - a CALLER CONTRACT, checked here.
 * @return Initialized set; hashset_get_capacity answers 0 if allocation or the
 *         overflow guard declined (an overlong hint rounds to a byte count
 *         that would wrap).
 */
HashSet hashset_init_2(USize const capacity);

/**
 * @brief Allocate and initialize an empty heap-backed set.
 * @return New set, or nullptr - one shape covers both the struct borrow being
 *         refused and the built set landing at capacity 0 (a declined bucket
 *         allocation); the latter releases the struct before answering
 *         nullptr, so nothing is leaked either way. Free with hashset_delete.
 * @note A nullptr must NOT be handed to hashset_delete, which treats a null
 *       *self as a contract violation and aborts. Check before deleting.
 */
HashSet* hashset_new_1(void);

/**
 * @brief Allocate and initialize a heap-backed set sized for @p capacity keys.
 * @param capacity Number of keys to size for. Must not be 0.
 * @return New set, or nullptr - one shape covers both the struct borrow being
 *         refused and the built set landing at capacity 0 (a declined bucket
 *         allocation); the latter releases the struct before answering
 *         nullptr, so nothing is leaked either way. Free with hashset_delete.
 * @note A nullptr must NOT be handed to hashset_delete, which treats a null
 *       *self as a contract violation and aborts. Check before deleting.
 */
HashSet* hashset_new_2(USize const capacity);

/**
 * @brief Walk every stored key/count pair, one call per pair.
 * @param self   Set. Must not be nullptr.
 * @param cursor Iteration state. Must not be nullptr. Caller sets it to 0
 *               before the first call; hashset_next advances it each call.
 * @param key    Out: the stored key, valid until the next mutating call. Must
 *               not be nullptr.
 * @param count  Out: the key's occurrence count. Must not be nullptr.
 * @return true when a pair was produced (@p key / @p count are valid); false
 *         once every pair has been visited, leaving @p key / @p count untouched.
 * @note Any hashset_add / hashset_add_static / hashset_add_static_2 / hashset_clear
 *       / hashset_uninit invalidates a cursor in progress - restart from 0 after
 *       mutating the set.
 * @note This is the sanctioned way to enumerate a set built as a frequency
 *       counter; the internal bucket arrays are not a public API.
 */
bool hashset_next(HashSet const *const self, USize *const cursor, char const **const key, U32 *const count);

/**
 * @brief Number of distinct keys in the set.
 * @param self Set. Must not be nullptr.
 * @return Distinct key count.
 */
USize hashset_size(HashSet const *const self);

/**
 * @brief Free owned copies and the bucket arrays, leaving the set DEAD.
 * @param self Set. Must not be nullptr.
 * @note The set is unusable until re-initialized: capacity drops to 0 and
 *       add/add_static/add_static_2 become silent no-ops, not a usable empty
 *       set. Safe to call twice.
 */
void hashset_uninit(HashSet *const self);

#endif // CONTAINER_HASHSET_HASHSET_H