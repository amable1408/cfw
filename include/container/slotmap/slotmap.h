/*
 * slotmap.h - Generational slot map for stable 32-bit handles over fixed pools
 *
 * Features:
 *   - Fixed-capacity bookkeeping (generation array, occupancy, free-slot cursor)
 *     over an element type the caller owns and indexes separately
 *   - Stable SlotMapHandle: low SLOTMAP_INDEX_BITS bits are the slot index,
 *     high SLOTMAP_GENERATION_BITS bits are the slot's current generation;
 *     handle 0 (SLOTMAP_HANDLE_INVALID) is always invalid
 *   - Stale-handle rejection: releasing and re-adding a slot bumps its
 *     generation, so a handle minted before the release fails slotmap_valid
 *   - Deterministic free-slot reuse: the lowest-index free slot is always
 *     chosen next, so slot assignment and iteration order are reproducible
 *     given the same sequence of add/remove calls - useful for any lockstep
 *     or replay-driven simulation that must reproduce the same slot
 *     assignment from the same command log
 *   - Heap or arena backed, following the CFW container convention
 *
 * Usage Examples:
 *   @code
 *   // Caller owns the parallel element storage; the map only tracks slots.
 *   SlotMap        actors       = slotmap_init(160);
 *   ActorState     storage[160] = DEFAULT_INITIALIZATION;
 *
 *   // add_2 answers the handle AND its index; a full (or never built) map answers
 *   // the invalid handle and an index equal to the capacity, so check before storing:
 *   // the sentinel index is deliberately one past the caller's array.
 *   USize const end = slotmap_get_capacity(&actors);
 *   USize index = end;
 *   SlotMapHandle const handle = slotmap_add_2(&actors, &index);
 *
 *   if (index < end) {
 *       storage[index] = (ActorState) { .health = 100 };
 *   }
 *
 *   if (slotmap_valid(&actors, handle)) {
 *       ActorState *const actor = &storage[slotmap_index(&actors, handle)];
 *   }
 *
 *   slotmap_remove(&actors, handle);
 *
 *   for (USize i = slotmap_first(&actors); i < end; i = slotmap_next(&actors, i)) {
 *       process(&storage[i]);
 *   }
 *
 *   slotmap_uninit(&actors);
 *   @endcode
 *
 * Error Handling:
 *   - Null self is checked first on every function (error_check_null aborts
 *     when ERROR_CHECK_ENABLED is defined).
 *   - slotmap_init / slotmap_alloc_init / slotmap_new / slotmap_alloc_new
 *     abort on a zero or > SLOTMAP_CAPACITY_MAX capacity - a program-chosen
 *     size, so this stays a contract check, not a refusal. With
 *     ERROR_CHECK_ENABLED off this check compiles out and an over-max
 *     capacity becomes undefined behaviour: the packed handle's index bits
 *     silently mask any index back into 0..SLOTMAP_CAPACITY_MAX - 1, so two
 *     live slots beyond the max can alias into the same handle value. There
 *     is no clamp.
 *   - Every borrow this module makes - slotmap_init, slotmap_alloc_init,
 *     slotmap_new and slotmap_alloc_new - goes through allocator_try_borrow,
 *     never the aborting allocator_borrow. A declined build (a refused
 *     arena, an exhausted heap, or an overflowing byte-count multiply) lands
 *     the map at capacity 0 with every operation inert:
 *       - slotmap_add / slotmap_add_2 answer SLOTMAP_HANDLE_INVALID (0),
 *         and add_2 writes 0 to *index;
 *       - slotmap_valid / slotmap_remove / slotmap_occupied answer false;
 *       - slotmap_first / slotmap_next answer 0 (== slotmap_get_capacity);
 *       - slotmap_get_size answers 0; slotmap_empty and slotmap_full both
 *         answer true (0 == 0);
 *       - slotmap_new / slotmap_alloc_new answer nullptr - one shape covers
 *         both a refused struct borrow and a built map that landed at
 *         capacity 0, so a caller checks one shape, not two, and nothing is
 *         leaked either way. Never hand that nullptr to slotmap_delete,
 *         which treats a null *self as a contract violation and aborts.
 *   - Every handle-consuming function is defensive against bad handles: 0,
 *     stale (wrong generation), foreign (out-of-range index), and
 *     already-removed handles are all rejected without ever touching freed
 *     memory. slotmap_index and slotmap_handle_at use USize/0 sentinels
 *     documented per-function instead of aborting on a bad handle.
 *   - Stale-handle rejection has an ABA bound: a slot's U16 generation wraps
 *     after 65535 releases of that one slot (skipping 0), at which point a
 *     handle minted 65535 generations earlier validates again. Size churn
 *     accordingly for long-lived, high-turnover pools.
 *   - Mutating during a walk: slotmap_remove of the walk's current slot, or
 *     any other slot, never skips or repeats a slot in the walk already in
 *     progress. slotmap_add during a walk is visited this pass iff its new
 *     index is above the index the walk is currently at - a slot it reuses
 *     below the current index is not revisited.
 *
 * Thread Safety:
 *   - Not thread-safe by design. Synchronize external access to a shared map.
 *
 * Memory Management:
 *   - slotmap_init / slotmap_new allocate the map's bookkeeping from the
 *     heap; slotmap_alloc_init / slotmap_alloc_new (under
 *     ARENA_IMPLEMENTATION) draw it from the given Arena instead. Release a
 *     map built with slotmap_init / slotmap_alloc_init with slotmap_uninit;
 *     release one built with slotmap_new / slotmap_alloc_new with
 *     slotmap_delete, which calls slotmap_uninit and then frees the struct.
 *   - slotmap_uninit leaves *self zeroed (capacity/count/free_cursor 0, both
 *     array pointers null) UNCONDITIONALLY - not gated on
 *     MEMORY_NON_DANGLING_POINTER. This is load-bearing, not hygiene: it is
 *     what makes a second slotmap_uninit call a safe no-op instead of a
 *     double free. Safe to call twice, and safe to overwrite with a fresh
 *     slotmap_init / slotmap_alloc_init afterward.
 *   - generations and occupied are ONE borrowed block: occupied is a pointer
 *     into the tail of the same allocation as generations, not an
 *     independently owned array. slotmap_uninit releases it with one call.
 *   - Do NOT copy a live SlotMap: both copies would share the same
 *     bookkeeping block (mutations desync them, and uninit-ing both
 *     double-releases). One owner, one slotmap_uninit / slotmap_delete.
 *   - The map never allocates or owns element storage. The caller provides a
 *     fixed-capacity array (or several parallel arrays) sized to the same
 *     capacity and indexes it with slotmap_index / the ascending indices
 *     from slotmap_first / slotmap_next. This keeps the caller's own state
 *     in caller-controlled, cache-friendly, struct-of-arrays layouts.
 *
 * Performance Characteristics:
 *   - slotmap_valid / slotmap_index / slotmap_handle_at / slotmap_occupied /
 *     slotmap_get_size / slotmap_get_capacity / slotmap_empty / slotmap_full
 *     are O(1).
 *   - slotmap_add scans forward from the lowest freed index past slots that
 *     are still occupied: O(1) when the slot nearest the cursor is free,
 *     O(capacity) worst case. The worst case is not rare: it recurs on every
 *     other add under this churn - on a full map of N, remove(0), remove
 *     (N-1), add (takes slot 0, cursor -> 1), add (rescans 1..N-2, all still
 *     occupied, takes N-1). Bounded by SLOTMAP_CAPACITY_MAX regardless.
 *   - slotmap_remove is O(1): it only ever resets the free-slot cursor
 *     backward.
 *   - slotmap_first / slotmap_next scan forward to the next occupied slot; a
 *     full pass over all occupied slots costs O(capacity) total, as expected
 *     for any dense iteration. slotmap_first is O(1) on an empty map.
 *   - slotmap_clear is O(capacity): it visits every slot to retire the ones
 *     still occupied.
 *
 * Dependencies:
 *   - <allocator/allocator.h> (which chains the arena/error/tracelog/log
 *     modules and the framework integer types)
 *
 * See slotmap.c for implementation details.
 */

#ifndef CONTAINER_SLOTMAP_H
#define CONTAINER_SLOTMAP_H

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/
#include <allocator/allocator.h>

/*==============================================================================
 * MARK: - Macros and Constants
 *============================================================================*/
/**
 * @def SLOTMAP_CAPACITY_MAX
 * @brief Largest capacity slotmap_init / slotmap_alloc_init / slotmap_new /
 *        slotmap_alloc_new accepts. Equal to 2^SLOTMAP_INDEX_BITS: the full
 *        range of indices (0 .. SLOTMAP_CAPACITY_MAX - 1) the packed
 *        handle's index bits can address. No index value is reserved as a
 *        sentinel inside the packed handle - SLOTMAP_HANDLE_INVALID is
 *        defined by generation 0, which no live slot ever mints, never by
 *        any particular index - so the whole index range is usable capacity.
 */
#define SLOTMAP_CAPACITY_MAX 65536
/**
 * @def SLOTMAP_GENERATION_BITS
 * @brief Width, in bits, of a SlotMapHandle's generation half (the high
 *        bits). The private generation shift derives from this constant.
 */
#define SLOTMAP_GENERATION_BITS 16
/**
 * @def SLOTMAP_HANDLE_INVALID
 * @brief The one handle value that is never valid. Slot generations start at
 *        1 and skip 0 on wraparound, so no live slot ever mints this value.
 */
#define SLOTMAP_HANDLE_INVALID 0
/**
 * @def SLOTMAP_INDEX_BITS
 * @brief Width, in bits, of a SlotMapHandle's index half (the low bits). The
 *        private index mask derives from this constant.
 */
#define SLOTMAP_INDEX_BITS 16

/*==============================================================================
 * MARK: - Typedefs and Enums
 *============================================================================*/
/**
 * @typedef SlotMapHandle
 * @brief Stable handle: low SLOTMAP_INDEX_BITS bits are the slot index, high
 *        SLOTMAP_GENERATION_BITS bits are the slot's generation at mint
 *        time. 0 (SLOTMAP_HANDLE_INVALID) is always invalid.
 * @warning NEVER use a SlotMapHandle as an array index. The packed index
 *          bits mean nothing until the generation half has been checked
 *          against the live slot - resolve it through slotmap_index first.
 */
typedef U32 SlotMapHandle;

/**
 * @struct SlotMap
 * @brief Bookkeeping-only generational slot allocator; owns no element data.
 * @note Every field is INTERNAL bookkeeping - use the accessor functions
 *       (slotmap_get_capacity, slotmap_get_size, slotmap_occupied,
 *       slotmap_first / slotmap_next, ...) rather than reading these
 *       directly; their layout may change.
 */
typedef struct SlotMap {
#ifdef ARENA_IMPLEMENTATION
    /** @brief Backing arena, or null for heap allocation. INTERNAL. */
    Arena *allocator;
#endif // ARENA_IMPLEMENTATION
    /** @brief Total slot count, fixed at construction. INTERNAL. */
    USize capacity;
    /** @brief Number of currently occupied slots. INTERNAL. */
    USize count;
    /** @brief Lower-bound scan hint for the next free slot. INTERNAL. */
    USize free_cursor;
    /** @brief Per-slot generation counter (never 0), size capacity - owns
     *         the single block also backing occupied. INTERNAL. */
    U16 *generations;
    /** @brief Per-slot occupancy flag, size capacity - a pointer into the
     *         tail of the same block as generations, not independently
     *         owned. INTERNAL. */
    bool *occupied;
} SlotMap;

/*==============================================================================
 * MARK: - Public API
 *============================================================================*/
#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Allocate a slot map's bookkeeping for a fixed capacity, from an arena.
 * @param capacity  Total slot count; must be > 0 and <= SLOTMAP_CAPACITY_MAX.
 * @param allocator Arena the map draws from. Must not be nullptr.
 * @return Initialized SlotMap, empty (count 0), ready for slotmap_add;
 *         slotmap_get_capacity answers 0 if the arena declined the borrow.
 */
SlotMap slotmap_alloc_init(USize const capacity, Arena *const allocator);

/**
 * @brief Allocate and initialize an arena-backed slot map on the arena.
 * @param capacity  Total slot count; must be > 0 and <= SLOTMAP_CAPACITY_MAX.
 * @param allocator Arena the map (and the struct) draws from. Must not be nullptr.
 * @return New map, or nullptr - one shape covers both the struct borrow
 *         being refused and the built map landing at capacity 0; the latter
 *         releases the struct before answering nullptr. Free with slotmap_delete.
 * @note A nullptr must NOT be handed to slotmap_delete, which treats a null
 *       *self as a contract violation and aborts. Check before deleting.
 */
SlotMap* slotmap_alloc_new(USize const capacity, Arena *const allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Mint a handle for the lowest-index free slot.
 * @param self Slot map to allocate from.
 * @return New handle, or SLOTMAP_HANDLE_INVALID (0) when the map is full
 *         (slotmap_get_size == slotmap_get_capacity) OR was never built
 *         (capacity 0) - both shapes answer the same sentinel.
 */
SlotMapHandle slotmap_add(SlotMap *const self);

/**
 * @brief Mint a handle for the lowest-index free slot, also reporting its index.
 * @param self  Slot map to allocate from.
 * @param index Out: the minted slot's index, or slotmap_get_capacity(self)
 *              when declined (full or never built). Must not be nullptr.
 * @return Same as slotmap_add.
 * @see slotmap_add
 */
SlotMapHandle slotmap_add_2(SlotMap *const self, USize *const index);

/**
 * @brief Retire every occupied slot, keeping the map's capacity.
 * @param self Slot map to clear.
 * @note Bumps every occupied slot's generation (so outstanding handles go
 *       stale) rather than re-initializing - a re-init would re-mint the
 *       first handle (0x00010000) and silently revalidate an old one.
 *       Leaves count and the free-slot cursor at 0.
 */
void slotmap_clear(SlotMap *const self);

/**
 * @brief Release a slot map allocated by slotmap_new / slotmap_alloc_new.
 * @param self Address of the map pointer. Must not be nullptr, and neither may *self.
 * @note Releases the bookkeeping block (via slotmap_uninit), then the struct
 *       itself. Nulls the caller's pointer only under
 *       MEMORY_NON_DANGLING_POINTER, matching the rest of the family.
 */
void slotmap_delete(SlotMap **const self);

/**
 * @brief Report whether the map holds no occupied slots.
 * @param self Slot map to query.
 * @return true when slotmap_get_size(self) == 0. A never-built (capacity 0)
 *         map is empty.
 */
bool slotmap_empty(SlotMap const *const self);

/**
 * @brief Get the first occupied slot index, ascending order.
 * @param self Slot map to scan.
 * @return Lowest occupied slot index, or slotmap_get_capacity(self) when empty.
 */
USize slotmap_first(SlotMap const *const self);

/**
 * @brief Report whether every slot is occupied.
 * @param self Slot map to query.
 * @return true when slotmap_get_size(self) == slotmap_get_capacity(self). A
 *         never-built (capacity 0) map answers true (0 == 0) - check
 *         slotmap_get_capacity to tell it apart from a genuinely full pool.
 */
bool slotmap_full(SlotMap const *const self);

/**
 * @brief Get the total slot count.
 * @param self Slot map to query.
 * @return Capacity passed to slotmap_init / slotmap_alloc_init, or 0 for a
 *         map that was declined at build time or never built.
 */
USize slotmap_get_capacity(SlotMap const *const self);

/**
 * @brief Get the number of currently occupied slots.
 * @param self Slot map to query.
 * @return Live handle count.
 */
USize slotmap_get_size(SlotMap const *const self);

/**
 * @brief Get the live handle currently occupying a slot index.
 * @param self  Slot map to query.
 * @param index Slot index to look up.
 * @return Current handle for that slot, or 0 when index is out of range or the
 *         slot is not occupied.
 */
SlotMapHandle slotmap_handle_at(SlotMap const *const self, USize const index);

/**
 * @brief Resolve a handle to its slot index, for indexing caller-owned storage.
 * @param self   Slot map the handle belongs to.
 * @param handle Handle to resolve.
 * @return Slot index when the handle is valid, otherwise slotmap_get_capacity(self)
 *         (an index that is always out of range for the caller's own arrays,
 *         sized to the same capacity) as the invalid sentinel.
 */
USize slotmap_index(SlotMap const *const self, SlotMapHandle const handle);

/**
 * @brief Allocate a slot map's bookkeeping arrays for a fixed capacity, on the heap.
 * @param capacity Total slot count; must be > 0 and <= SLOTMAP_CAPACITY_MAX.
 * @return Initialized SlotMap, empty (count 0), ready for slotmap_add;
 *         slotmap_get_capacity answers 0 if the heap borrow was declined.
 */
SlotMap slotmap_init(USize const capacity);

/**
 * @brief Allocate and initialize a heap-backed slot map.
 * @param capacity Total slot count; must be > 0 and <= SLOTMAP_CAPACITY_MAX.
 * @return New map, or nullptr - one shape covers both the struct borrow
 *         being refused and the built map landing at capacity 0; the latter
 *         releases the struct before answering nullptr. Free with slotmap_delete.
 * @note A nullptr must NOT be handed to slotmap_delete, which treats a null
 *       *self as a contract violation and aborts. Check before deleting.
 */
SlotMap* slotmap_new(USize const capacity);

/**
 * @brief Get the next occupied slot index after a given index, ascending order.
 * @param self  Slot map to scan.
 * @param index Index to start searching after (typically the previous result
 *              of slotmap_first / slotmap_next). Accepts slotmap_get_capacity(self)
 *              (the "no more slots" sentinel) and simply answers it back.
 * @return Next occupied slot index, or slotmap_get_capacity(self) when there is
 *         none.
 */
USize slotmap_next(SlotMap const *const self, USize const index);

/**
 * @brief Check whether a slot index is currently occupied.
 * @param self  Slot map to query.
 * @param index Slot index to check.
 * @return true when index is in range and that slot holds a live handle;
 *         false otherwise, including index >= slotmap_get_capacity(self).
 */
bool slotmap_occupied(SlotMap const *const self, USize const index);

/**
 * @brief Release a handle's slot, making it eligible for reuse.
 * @param self   Slot map that owns the handle.
 * @param handle Handle to release.
 * @return true when the handle was valid and its slot was released, false for
 *         0, a stale generation, an out-of-range index, or a slot that was
 *         already removed (double-remove is safely rejected, not double-freed).
 */
bool slotmap_remove(SlotMap *const self, SlotMapHandle const handle);

/**
 * @brief Release a slot map's bookkeeping block.
 * @param self Slot map to release; left zeroed (capacity/count/free_cursor 0,
 *             both array pointers null) UNCONDITIONALLY - see Memory
 *             Management. Safe to call twice, and safe to overwrite with a
 *             fresh slotmap_init / slotmap_alloc_init afterward (the struct
 *             itself is caller-owned and is never freed by this call).
 */
void slotmap_uninit(SlotMap *const self);

/**
 * @brief Check whether a handle currently refers to a live slot.
 * @param self   Slot map that owns the handle.
 * @param handle Handle to check.
 * @return true when handle is non-zero, its index is in range, that slot is
 *         occupied, and its generation matches the handle's - false otherwise.
 */
bool slotmap_valid(SlotMap const *const self, SlotMapHandle const handle);

#endif // CONTAINER_SLOTMAP_H