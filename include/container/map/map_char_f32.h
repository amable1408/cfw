/*
 * map_char_f32.h - Map from char* to F32 (string-to-scalar map) for the C Libraries Framework
 *
 * Features:
 *   - Map from string keys to F32 values, over parallel AL_Char and AL_F32 lists
 *   - Arena or heap allocation support
 *   - Add, look up, remove, and iterate key-value pairs
 *
 * Usage Examples:
 *   @code
 *   // The map ADOPTS its keys: heap-allocated here, released by uninit. The VALUE is a
 *   // scalar held by value - there is nothing to own on that side.
 *   Map_Char_F32 counts = map_char_f32_init_1();
 *
 *   map_char_f32_add(&counts, char_new_2("retries"), 3);
 *
 *   F32 *const retries = map_char_f32_at_1(&counts, "retries");
 *
 *   map_char_f32_uninit(&counts);
 *
 *   // For a key you do NOT want to give up - a literal, a stack buffer - add_static copies it.
 *   Map_Char_F32 limits = map_char_f32_init_1();
 *
 *   map_char_f32_add_static(&limits, "timeout", 250);
 *
 *   map_char_f32_uninit(&limits);
 *   @endcode
 *
 * Error Handling:
 *   Contract violations go through error_check_*, which LOGS AND ABORTS the process.
 *   It does not return early: these are programming errors, not runtime conditions.
 *   Three classes, and the counts are exact rather than illustrative:
 *     - a NULL POINTER argument, at forty-two public sites across thirty of the
 *       thirty-four functions - self on every function that does not forward it (at_1,
 *       contains_1, remove_1 and add_static check it one frame deeper, in the _2 form
 *       they call), key on the nine functions that take one, keys/values on the four
 *       copying constructors, allocator on the six arena entry points, and *self on
 *       delete;
 *     - an INDEX at or past map_char_f32_get_size, on get_key, get_value and remove_at;
 *     - a ZERO CAPACITY, at five sites: init_2, alloc_init_2, reserve, and new_2 and
 *       alloc_new_2, which carry their own check before forwarding.
 *
 *   A capacity is therefore a CALLER CONTRACT, not an input. Never pass an unvalidated
 *   remote length to reserve or to a capacity constructor - validate it first, or the
 *   check becomes a remote abort. A count parsed from a request, a file header or a config
 *   value is remote in the sense that matters, whether or not it arrived over a socket.
 *
 *   Conditions that depend on a VALUE rather than on a broken contract are
 *   refused instead, and never abort:
 *     - an allocator that REFUSES - a null-handler arena, or a try_borrow that cannot be
 *       satisfied - leaves the map unchanged, and add() declines with it, returning false
 *       and leaving the key with the caller. Note the limit of that promise: THREE kinds
 *       of borrow still take the ABORTING path, and each is reachable through EITHER list,
 *       so re-derive them per instantiation rather than trusting this list.
 *         1. Growing a list goes through al_char_reserve / al_f32_reserve, so an
 *            exhausted arena or a failing heap can end the process inside an add.
 *         2. A capacity constructor's INITIAL array borrow goes through al_char_init_2 /
 *            al_f32_init_2 and their alloc_ twins, which borrow directly rather than
 *            through reserve - so an arena with room for the struct but not for the
 *            arrays aborts before any add is attempted. This includes the borrow
 *            init_3 / alloc_init_3 take on the SOURCE lists' size.
 *         3. map_char_f32_shrink forwards to al_char_shrink / al_f32_shrink, which
 *            re-borrow the compacted arrays through that same path. Worth reading twice
 *            on an arena: arena_linear_free is a no-op, so shrink does not reclaim the
 *            old arrays, it bump-allocates smaller ones - moving the arena TOWARD
 *            exhaustion rather than away from it. Shrinking a map is not a way to free
 *            arena space.
 *       Key copies and the struct borrows of both new_ families use the non-aborting path.
 *     - a lookup for an absent key answers nullptr;
 *     - an empty key is a legal key, and a stored nullptr key is a legal element.
 *
 *   With ERROR_CHECK_ENABLED off these checks compile out. The NULL-POINTER and INDEX
 *   checks each guard a real dereference or array index immediately below them, so a
 *   violated one becomes undefined behaviour, not a graceful failure. The ZERO-CAPACITY
 *   checks guard no dereference here - the underlying lists already treat a capacity of 0
 *   as a bootstrap/no-op case - so an unchecked zero capacity degrades to an ordinary
 *   empty map instead of corrupting anything. The REFUSALS above (an absent key, a
 *   declined allocator, an over-long copy) are runtime branches on VALUES, not contract
 *   checks, and hold either way.
 *
 * Thread Safety:
 *   Not thread-safe. Caller must synchronize if used from multiple threads.
 *
 * Memory Management:
 *   - THE MAP OWNS THE KEYS YOU ADD, AND ONLY THE KEYS. map_char_f32_add takes the
 *     caller's key pointer BY VALUE, and remove/clear/uninit release it through the key
 *     list's own allocator. After a successful add, do not free the key yourself, and do
 *     not add a string literal - use add_static or add_static_2, which copy.
 *   - THE VALUE IS A SCALAR held by value. There is nothing to own, nothing to release,
 *     and no way to express "present with no value" - which is why this instantiation has
 *     no key-only add. ANY BIT PATTERN IS A VALUE here - including zero, and for a float
 *     value, NaN and -0.0 as well - indistinguishable from any other, because the
 *     map never compares values. map_char_f32_contains_1 exists precisely for that: it is
 *     what tells a stored zero from an absent key.
 *   - A DECLINED add returns false and takes nothing: the key stays the caller's, and the
 *     caller remains responsible for releasing it.
 *   - PRECONDITION: a key handed to add must have come from the KEY LIST's allocator,
 *     because that is the allocator that will release it. THE MISMATCH SEVERITY IS
 *     ASYMMETRIC: on an arena-backed key list a wrong-allocator key is a LEAK, but on a
 *     heap-backed one release reaches bare free(), so an arena-borrowed key is REAL
 *     CORRUPTION - surfacing at uninit, far from the add that caused it.
 *   - KEYS MUST BE NUL-TERMINATED. Lookup measures the STORED key with char_length, so an
 *     unterminated key is read past its allocation on every scan; and a key this map
 *     COPIES is measured the same way to size the copy. A key may be EMPTY; it may not be
 *     unterminated.
 *   - map_char_f32_init_3 DEEP-COPIES the keys and copies the values. The caller keeps
 *     both lists and every string in them, and releases them normally - al_char_uninit
 *     and al_f32_uninit on each is correct.
 *   - A stored nullptr key is a legal element and is skipped on release and on lookup.
 *   - KEY pointers - from get_key - are INVALIDATED by remove, clear and uninit, which
 *     release the key itself. Growth does not invalidate them: it reallocates the array
 *     of POINTERS, not what they point to.
 *   - VALUE pointers - from at_1, at_2 and get_value - are INVALIDATED BY GROWTH **and**
 *     by remove, remove_at, clear and uninit. Growth reallocates the array under them;
 *     remove and remove_at SHIFT it, so a held pointer silently starts naming a different
 *     pair's value; clear and uninit free it outright. Treat a F32* from this map as dead
 *     after any of those, and copy the value out if you need it to survive one.
 *
 *     Read the contrast with the key rule narrowly: what differs is that GROWTH
 *     invalidates a value pointer and not a key pointer. It does NOT mean the removal
 *     rules are lifted here - they apply to both halves, and a write through a F32*
 *     after uninit is a write-after-free.
 *   - The list handles from get_keys/get_values are valid for the map's whole lifetime.
 *
 * Performance Characteristics:
 *   - Lookup, contains and remove are a LINEAR SCAN - O(n) in the number of pairs,
 *     O(n * key length) in the worst case. This is an association vector, not a
 *     hash table; for large or hot key sets, use container/hashset.
 *   - Amortized O(1) append; O(n) remove at an arbitrary index; O(1) indexed access.
 *
 * Duplicate Keys:
 *   - add does NOT check for an existing key. Adding the same key twice stores two
 *     pairs; every lookup answers the FIRST, and remove deletes the FIRST, so a
 *     remove-then-look-up can still find the shadowed second entry.
 *   - TO CHANGE A VALUE: write through the at_* result. That is safe and complete here -
 *     the value lives in the array and a F32 cannot outgrow its slot. This is not a
 *     convenience of this file but a consequence of the family's ONE STRUCTURAL FORK: at_*
 *     answers the ADDRESS OF THE SLOT on eight of the nine instantiations and the STORED
 *     VALUE on map_char_char, whose values must therefore be remove-then-add. See
 *     map_char_f32_at_1.
 *
 * Family:
 *   One of the nine string-keyed maps of container/map - two parallel AL_* lists scanned
 *   linearly, one 34-function API in three value shapes; tools/map_divergence.py proves the
 *   generated scalars match their anchor and each canonical header matches its own stated
 *   counts. This is the SCALAR shape - the value is stored BY VALUE, so at_* returns the
 *   address of the slot. See "Generated Instantiations" below for which of the six scalar
 *   maps this file is, and for its suite.
 *
 * Dependencies:
 *   - <container/arrayList/al_char.h> - the key half, and what makes the map own its keys.
 *   - <container/arrayList/al_f32.h> - the value half, which owns nothing.
 *     Between them they transitively supply char/ (char_length, char_copy_3,
 *     char_compare_equal_2), allocator/ (allocator_try_borrow, allocator_release),
 *     memory/ (memory_empty) and, under ARENA_IMPLEMENTATION, arena/.
 *
 * Generated Instantiations:
 *   SIX SCALAR MAPS, ONE SOURCE. map_char_{u8,u16,u32,u64,f32,f64} share one text: the u64
 *   pair is the hand-maintained anchor, the other five are rewritten from it by
 *   tools/map_generate.py and checked by tools/map_divergence.py. If the @file line above
 *   does not say u64, you are reading a generated copy - every fix goes into the u64 pair,
 *   then
 *
 *       python3 tools/map_generate.py      # rewrite the five from the u64 pair
 *       python3 tools/map_divergence.py    # confirm the substitution inverted
 *
 *   The one suite, under tests/container/map/, is the u64 suite and covers all six. The gate
 *   also re-derives the abort counts stated above, for the u64 pair and for the other three
 *   canonical instantiations. Those numbers being IDENTICAL to map_char_char's is a
 *   coincidence of the two files' shapes, not something that holds by construction - the two
 *   struct instantiations can state a different number from each other - so re-derive rather
 *   than copy when either moves.
 *
 * See map_char_f32.c for implementation details.
 */
#ifndef CONTAINER_MAP_CHAR_F32_H
#define CONTAINER_MAP_CHAR_F32_H

#include <container/arrayList/al_char.h>
#include <container/arrayList/al_f32.h>

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief Map from string keys to F32 values.
 *
 * Two parallel lists at matching indices. There is deliberately no separate count:
 * the size is DERIVED from the lists, so it cannot disagree with them. The LISTS can
 * still be desynced through the mutable get_keys/get_values handles - what cannot
 * happen is a stale third counter disagreeing with both; a half-grown pair simply
 * does not count until its other half lands, which is why get_size takes the SMALLER
 * of the two sides.
 */
typedef struct {
    AL_Char key;  /**< Keys, at matching indices with `value`. */
    AL_F32 value; /**< Values, at matching indices with `key`. */
} Map_Char_F32;

/*==============================================================================
 * MARK: - API
 *============================================================================*/

/**
 * @brief Add a key-value pair, taking ownership of the key.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key string. Must not be nullptr, and must be NUL-terminated. OWNERSHIP
 *        TRANSFERS on success.
 * @param value Value, held by value. Any bit pattern is legal - including zero, and for a
 *        float value, NaN and -0.0 as well. The map never inspects it.
 * @return true when the pair was stored; false when the allocator declined.
 * @note On false NOTHING was taken - the map is unchanged and the caller still owns the
 *       key and must release it.
 * @note Does not check for an existing key; see "Duplicate Keys" above.
 * @see map_char_f32_add_static
 */
bool map_char_f32_add(Map_Char_F32 *const self, char *const key, F32 const value);

/**
 * @brief Add a key-value pair by COPYING the key, leaving the original with the caller.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key string. Must not be nullptr, and must be NUL-terminated. Copied; the
 *        caller keeps the original.
 * @param value Value, held by value.
 * @return true when the pair was stored; false when the copy or the allocator declined.
 * @note This is the entry point for string literals and for any key whose lifetime the
 *       caller needs to keep. A key copy is released before returning false, so a decline
 *       leaks nothing.
 */
bool map_char_f32_add_static(Map_Char_F32 *const self, char const *const key, F32 const value);

/**
 * @brief Add a key-value pair by COPYING a key of explicit size.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key bytes. Must not be nullptr; NEED NOT be NUL-terminated.
 * @param key_size Key length in bytes. 0 is a legal key, not an error. MUST NOT exceed
 *        the readable bytes at `key` - the size reaches a copy as a read length, so an
 *        over-large one is a heap over-read rather than a decline. Same contract as at_2.
 * @param value Value, held by value.
 * @return true when the pair was stored; false when the copy or the allocator declined.
 * @note The sized form is the PRIMITIVE - add_static measures and forwards here. It is the
 *       counterpart to at_2: a key that is a slice of a larger buffer can be stored as
 *       well as looked up, without copying it out first.
 * @note What is STORED is always NUL-terminated, whatever was passed in.
 * @note There is no value_size and there must not be: the value is a scalar, and the _2
 *       suffix encodes the KEY argument and nothing else.
 */
bool map_char_f32_add_static_2(Map_Char_F32 *const self, char const *const key, USize const key_size, F32 const value);

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Initialize an empty map with an arena allocator.
 * @param allocator Arena pointer. Must not be nullptr.
 * @return Initialized Map_Char_F32.
 */
Map_Char_F32 map_char_f32_alloc_init_1(Arena *allocator);

/**
 * @brief Initialize a map with a starting capacity and an arena allocator.
 * @param capacity Initial capacity for both lists. Must not be 0.
 * @param allocator Arena pointer. Must not be nullptr.
 * @return Initialized Map_Char_F32.
 * @note A zero capacity ABORTS, matching al_char_init_2. Use alloc_init_1 for empty.
 */
Map_Char_F32 map_char_f32_alloc_init_2(USize const capacity, Arena *allocator);

/**
 * @brief Initialize a map by COPYING key and value lists, with an arena allocator.
 * @param keys Source keys. Must not be nullptr.
 * @param values Source values. Must not be nullptr.
 * @param allocator Arena pointer. Must not be nullptr.
 * @return Initialized Map_Char_F32.
 * @note DEEP-COPIES the keys and copies the values. The caller keeps both lists and every
 *       string in them, and releases them normally.
 * @note ALL OR NOTHING. A declined copy leaves an EMPTY map rather than a partial one.
 * @note Every key copied is measured with char_length, so the source keys must be
 *       NUL-terminated. A stored nullptr key is fine and copies as itself.
 * @note Pairs beyond the shorter of the two lists are not represented.
 */
Map_Char_F32 map_char_f32_alloc_init_3(AL_Char const *const keys, AL_F32 const *const values, Arena *allocator);

/**
 * @brief Allocate and initialize an empty map with an arena allocator.
 * @param allocator Arena pointer. Must not be nullptr.
 * @return New map, or nullptr when the allocator declined. Free with map_char_f32_delete.
 * @note A nullptr must NOT be handed to map_char_f32_delete, which treats a null *self as
 *       a contract violation and aborts. Check before deleting.
 */
Map_Char_F32* map_char_f32_alloc_new_1(Arena *allocator);

/**
 * @brief Allocate and initialize a map with a capacity and an arena allocator.
 * @param capacity Initial capacity for both lists. Must not be 0.
 * @param allocator Arena pointer. Must not be nullptr.
 * @return New map, or nullptr when the allocator declined. Free with map_char_f32_delete.
 * @note A zero capacity ABORTS, checked here before forwarding. Use alloc_new_1 for empty.
 * @note A nullptr must NOT be handed to map_char_f32_delete, which treats a null *self as
 *       a contract violation and aborts. Check before deleting.
 */
Map_Char_F32* map_char_f32_alloc_new_2(USize const capacity, Arena *allocator);

/**
 * @brief Allocate a map by COPYING key and value lists, with an arena allocator.
 * @param keys Source keys. Must not be nullptr.
 * @param values Source values. Must not be nullptr.
 * @param allocator Arena pointer. Must not be nullptr.
 * @return New map, or nullptr when the allocator declined. Free with map_char_f32_delete.
 * @note A nullptr must NOT be handed to map_char_f32_delete, which treats a null *self as
 *       a contract violation and aborts. Check before deleting.
 */
Map_Char_F32* map_char_f32_alloc_new_3(AL_Char const *const keys, AL_F32 const *const values, Arena *allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Look up a value by a null-terminated key.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key string. Must not be nullptr; may be empty.
 * @return A pointer to the value at the first matching key, or nullptr when the key is
 *         absent. A nullptr means absent and nothing else - unlike the pointer-valued
 *         instantiations, there is no present-with-no-value state to confuse it with.
 * @note THE RETURNED POINTER POINTS INTO THE VALUE ARRAY and dies on the next growth.
 *       Writing through it is the supported way to change a value; keeping it across an
 *       add is not. See "Memory Management".
 * @note FAMILY FORK: at_* answers the ADDRESS OF THE SLOT here, and on al_char and
 *       string. map_char_char_at_1 alone answers the stored value itself, so its
 *       change-a-value idiom is remove_1 then add rather than a write through this
 *       pointer. One fork, two idioms - see map_char_char_at_1.
 */
F32* map_char_f32_at_1(Map_Char_F32 const *const self, char const *const key);

/**
 * @brief Look up a value by a key of explicit size.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key bytes. Must not be nullptr; may be empty.
 * @param key_size Key length in bytes. 0 is a legal key, not an error. MUST NOT exceed the
 *        readable bytes at `key`: it is forwarded to char_compare_equal_2, which reads that
 *        many bytes whenever a stored key has the same length, so an over-large size is a
 *        heap over-read rather than a miss. contains_2 and remove_2 share the path and the
 *        contract.
 * @return A pointer to the value at the first matching key, or nullptr when absent.
 * @note Takes a size so a key that is a slice of a larger buffer can be looked up
 *       without copying it first. Same invalidation rule as at_1.
 */
F32* map_char_f32_at_2(Map_Char_F32 const *const self, char const *const key, USize const key_size);

/**
 * @brief Remove every pair, releasing each key.
 * @param self Map pointer. Must not be nullptr.
 * @note Keeps the allocated capacity; see map_char_f32_shrink to release it.
 */
void map_char_f32_clear(Map_Char_F32 *const self);

/**
 * @brief Report whether a null-terminated key is present.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key string. Must not be nullptr; may be empty.
 * @return true when the key is present, whatever its value.
 * @note This is how a stored ZERO is told apart from an absent key. On a scalar map that
 *       is the whole reason contains_* exists - at_1 already answers absence with a
 *       nullptr, but it cannot tell you that the value it found happens to be 0.
 */
bool map_char_f32_contains_1(Map_Char_F32 const *const self, char const *const key);

/**
 * @brief Report whether a key of explicit size is present.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key bytes. Must not be nullptr; may be empty.
 * @param key_size Key length in bytes. 0 is a legal key, not an error.
 * @return true when the key is present, whatever its value.
 * @note Same read-extent contract as at_2.
 */
bool map_char_f32_contains_2(Map_Char_F32 const *const self, char const *const key, USize const key_size);

/**
 * @brief Release a map allocated by map_char_f32_new_* or map_char_f32_alloc_new_*.
 * @param self Address of the map pointer. Must not be nullptr, and neither may *self.
 * @note Releases every key, then the map itself. The STRUCT goes back to the key list's
 *       allocator, which is exact for every map these constructors build; a map assembled
 *       by hand from two lists with different allocators must not be deleted through this.
 *       Nulls the caller's pointer only under MEMORY_NON_DANGLING_POINTER, matching
 *       al_char_delete.
 */
void map_char_f32_delete(Map_Char_F32 **const self);

/**
 * @brief Report whether the map holds no pairs.
 * @param self Map pointer. Must not be nullptr.
 * @return true when there are no pairs.
 * @note "Empty" means no pairs, never "no allocation" - a map with reserved capacity
 *       and nothing in it is empty.
 */
bool map_char_f32_empty(Map_Char_F32 const *const self);

/**
 * @brief Report the capacity available for pairs.
 * @param self Map pointer. Must not be nullptr.
 * @return The smaller of the two lists' capacities, since a pair needs a slot in both.
 */
USize map_char_f32_get_capacity(Map_Char_F32 const *const self);

/**
 * @brief Get the key at an index.
 * @param self Map pointer. Must not be nullptr.
 * @param index Index. Must be below map_char_f32_get_size.
 * @return The key, which may be nullptr if one was stored.
 */
char* map_char_f32_get_key(Map_Char_F32 const *const self, USize const index);

/**
 * @brief Get the keys list.
 * @param self Map pointer. Must not be nullptr.
 * @return The keys list.
 * @note Takes a mutable map because the returned handle is mutable: writing through
 *       it changes the map, and pairing is the caller's responsibility from then on.
 * @note NOT the iteration API - get_key/get_value iterate over a const map and cannot
 *       desync it. This exists as the ESCAPE HATCH: it is what lets one list's capacity
 *       be tuned independently (al_char_reserve on the handle) without the map carrying
 *       per-list wrappers that nothing in the tree ever called.
 */
AL_Char* map_char_f32_get_keys(Map_Char_F32 *const self);

/**
 * @brief Report the number of pairs.
 * @param self Map pointer. Must not be nullptr.
 * @return The smaller of the two lists' sizes, since a pair needs both halves.
 * @note DERIVED rather than stored, so it cannot disagree with the lists.
 */
USize map_char_f32_get_size(Map_Char_F32 const *const self);

/**
 * @brief Get the value at an index.
 * @param self Map pointer. Must not be nullptr.
 * @param index Index. Must be below map_char_f32_get_size.
 * @return A pointer to the value. Same invalidation rule as at_1 - it points into the
 *         value array and dies on the next growth.
 */
F32* map_char_f32_get_value(Map_Char_F32 const *const self, USize const index);

/**
 * @brief Get the values list.
 * @param self Map pointer. Must not be nullptr.
 * @return The values list.
 * @note Takes a mutable map because the returned handle is mutable; see get_keys, which
 *       also explains why this is an escape hatch rather than the iteration API.
 */
AL_F32* map_char_f32_get_values(Map_Char_F32 *const self);

/**
 * @brief Initialize an empty map (heap).
 * @return Initialized Map_Char_F32.
 */
Map_Char_F32 map_char_f32_init_1(void);

/**
 * @brief Initialize a map with a starting capacity (heap).
 * @param capacity Initial capacity for both lists. Must not be 0.
 * @return Initialized Map_Char_F32.
 * @note A zero capacity ABORTS, matching al_char_init_2. Use init_1 for empty.
 */
Map_Char_F32 map_char_f32_init_2(USize const capacity);

/**
 * @brief Initialize a map by COPYING key and value lists (heap).
 * @param keys Source keys. Must not be nullptr.
 * @param values Source values. Must not be nullptr.
 * @return Initialized Map_Char_F32.
 * @note DEEP-COPIES the keys and copies the values. The caller keeps both lists and every
 *       string in them, and releases them normally.
 * @note ALL OR NOTHING. A declined copy leaves an EMPTY map rather than a partial one.
 * @note Every key copied is measured with char_length, so the source keys must be
 *       NUL-terminated. A stored nullptr key is fine and copies as itself.
 * @note Pairs beyond the shorter of the two lists are not represented.
 * @note init_3(&other.key, &other.value) IS THE COPY CONSTRUCTOR for an existing map:
 *       deep-copies both lists in one call, no separate init_4 needed.
 */
Map_Char_F32 map_char_f32_init_3(AL_Char const *const keys, AL_F32 const *const values);

/**
 * @brief Allocate and initialize an empty map (heap).
 * @return New map, or nullptr when the allocator declined. Free with map_char_f32_delete.
 * @note A nullptr must NOT be handed to map_char_f32_delete, which treats a null *self as
 *       a contract violation and aborts. Check before deleting.
 */
Map_Char_F32* map_char_f32_new_1(void);

/**
 * @brief Allocate and initialize a map with a capacity (heap).
 * @param capacity Initial capacity for both lists. Must not be 0.
 * @return New map, or nullptr when the allocator declined. Free with map_char_f32_delete.
 * @note A zero capacity ABORTS, checked here before forwarding. Use new_1 for empty.
 * @note A nullptr must NOT be handed to map_char_f32_delete, which treats a null *self as
 *       a contract violation and aborts. Check before deleting.
 */
Map_Char_F32* map_char_f32_new_2(USize const capacity);

/**
 * @brief Allocate a map by COPYING key and value lists (heap).
 * @param keys Source keys. Must not be nullptr.
 * @param values Source values. Must not be nullptr.
 * @return New map, or nullptr when the allocator declined. Free with map_char_f32_delete.
 * @note A nullptr must NOT be handed to map_char_f32_delete, which treats a null *self as
 *       a contract violation and aborts. Check before deleting.
 */
Map_Char_F32* map_char_f32_new_3(AL_Char const *const keys, AL_F32 const *const values);

/**
 * @brief Remove the first pair with a null-terminated key.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key string. Must not be nullptr; may be empty.
 * @return true when a pair was removed.
 * @note Releases the stored key. Removes only the FIRST match, so a duplicate key leaves
 *       the shadowed pair in place.
 */
bool map_char_f32_remove_1(Map_Char_F32 *const self, char const *const key);

/**
 * @brief Remove the first pair with a key of explicit size.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key bytes. Must not be nullptr; may be empty.
 * @param key_size Key length in bytes. 0 is a legal key, not an error.
 * @return true when a pair was removed.
 * @note Same read-extent contract as at_2. Note what that means for a key holding an
 *       embedded NUL: the STORED key is measured with char_length, so only its leading
 *       segment ever matches.
 */
bool map_char_f32_remove_2(Map_Char_F32 *const self, char const *const key, USize const key_size);

/**
 * @brief Remove the pair at an index.
 * @param self Map pointer. Must not be nullptr.
 * @param index Index. Must be below map_char_f32_get_size.
 * @note The indexed counterpart to get_key/get_value, and the only correct way to delete
 *       something found by iterating. remove_1(map_char_f32_get_key(map, 3)) removes the
 *       FIRST pair with that key, which is a different pair whenever key 3 is a duplicate.
 */
void map_char_f32_remove_at(Map_Char_F32 *const self, USize const index);

/**
 * @brief Reserve capacity in both lists.
 * @param self Map pointer. Must not be nullptr.
 * @param capacity Minimum capacity. Must not be 0.
 * @note A zero capacity ABORTS, matching al_char_reserve.
 */
void map_char_f32_reserve(Map_Char_F32 *const self, USize const capacity);

/**
 * @brief Shrink both lists to their sizes.
 * @param self Map pointer. Must not be nullptr.
 * @note ON AN ARENA THIS DOES NOT FREE ANYTHING, and costs memory. The shrink borrows
 *       smaller arrays and releases the old ones, but arena_linear_free is a no-op - so
 *       the arena's high-water mark RISES and the map moves toward exhaustion. Shrinking
 *       to reclaim arena space does the opposite of what it looks like.
 * @note Those borrows are also aborting site 3 under Error Handling: shrinking a large
 *       map into a nearly-full arena can end the process.
 */
void map_char_f32_shrink(Map_Char_F32 *const self);

/**
 * @brief Release all map storage and reset to the empty state.
 * @param self Map pointer. Must not be nullptr.
 * @note Releases every key through the key list's allocator. Safe to call twice.
 */
void map_char_f32_uninit(Map_Char_F32 *const self);

#endif // CONTAINER_MAP_CHAR_F32_H