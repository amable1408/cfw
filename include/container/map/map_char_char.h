/*
 * map_char_char.h - Map from char* to char* (string-to-string map) for the C Libraries Framework
 *
 * Features:
 *   - Map from string keys to string values, over parallel AL_Char (keys) and AL_Char
 *     (values) lists
 *   - Arena or heap allocation support
 *   - Add, look up, remove, and iterate key-value pairs
 *
 * Usage Examples:
 *   @code
 *   // The map ADOPTS what you add: heap-allocated here, released by uninit.
 *   Map_Char_Char map = map_char_char_init_1();
 *
 *   // Named, not passed as temporaries: on a decline NOTHING was taken, and an unnamed
 *   // temporary is a pointer you can no longer reach to release.
 *   char *const key = char_new_2("key");
 *   char *const value = char_new_2("value");
 *
 *   if (!map_char_char_add(&map, key, value)) {
 *       char_delete(key);
 *       char_delete(value);
 *   }
 *
 *   char *const found = map_char_char_at_1(&map, "key");
 *
 *   map_char_char_uninit(&map);
 *
 *   // For keys and values you do NOT want to give up - a literal, a stack buffer, or
 *   // anything whose lifetime you need to keep - add_static copies them.
 *   Map_Char_Char defaults = map_char_char_init_1();
 *
 *   // add_static can decline too, and its return is worth checking when the pair matters -
 *   // but a decline there costs you nothing, because the originals were never yours to lose.
 *   map_char_char_add_static(&defaults, "mode", "release");
 *
 *   map_char_char_uninit(&defaults);
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
 *     - an INDEX at or past map_char_char_get_size, on get_key, get_value and remove_at;
 *     - a ZERO CAPACITY, at five sites: init_2, alloc_init_2, reserve, and new_2 and
 *       alloc_new_2, which carry their own check before forwarding.
 *
 *   A capacity is therefore a CALLER CONTRACT, not an input. Never pass an unvalidated
 *   remote length to reserve or to a capacity constructor - validate it first, or the
 *   check becomes a remote abort. This module is consumed by a parser of remote input;
 *   that is the sentence most likely to matter to whoever wires up the next one.
 *
 *   Conditions that depend on a VALUE rather than on a broken contract are
 *   refused instead, and never abort:
 *     - an allocator that REFUSES - a null-handler arena, or a try_borrow that cannot be
 *       satisfied - leaves the map unchanged, and every adding entry point declines with
 *       it: add() returns false leaving ownership with the caller, and add_static /
 *       add_static_2 return false releasing whatever partial copy they had made, so a
 *       decline never leaks the half-built pair. Note the limit of that promise: THREE
 *       borrows still take the ABORTING path, all of them inside al_char.
 *         1. Growing a list goes through al_char_reserve, so an exhausted arena or a
 *            failing heap can end the process inside an add.
 *         2. A capacity constructor's INITIAL array borrow goes through al_char_init_2 /
 *            al_char_alloc_init_2, which borrow directly rather than through reserve - so
 *            an arena with room for the struct but not for the array aborts before any add
 *            is attempted. This includes the borrow init_3 / alloc_init_3 take on the
 *            SOURCE lists' size: they are copy constructors by name but reach the same
 *            site, so the borrow warning in THIS clause applies to them too - not the zero
 *            capacity rule above, which they never trip - a zero size diverts to init_1 /
 *            alloc_init_1, each keeping the allocator its own variant was built with.
 *            Their exposure is bounded - the size counts pairs that already exist in
 *            memory - but the classification is what a reader uses to decide whether the
 *            warning applies.
 *         3. map_char_char_shrink forwards to al_char_shrink, which re-borrows the
 *            compacted array through that same path. Worth reading twice on an arena:
 *            arena_linear_free is a no-op, so shrink does not reclaim the old array, it
 *            bump-allocates a second smaller one - moving the arena TOWARD exhaustion
 *            rather than away from it. Shrinking a map is not a way to free arena space.
 *       Element copies and the struct borrows of both new_ families use the non-aborting
 *       path. All three remaining sites are al_char's to change, not this module's to
 *       claim;
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
 *   - THE MAP OWNS WHAT YOU ADD. map_char_char_add takes the caller's pointers BY
 *     VALUE, and remove/clear/uninit release them through the lists' own allocator.
 *     After a successful add, do not free the key or the value yourself, and do not
 *     add a string literal - use add_static or add_static_2, which copy.
 *   - A DECLINED add returns false and takes nothing: ownership stays with the
 *     caller, who remains responsible for releasing both pointers.
 *   - PRECONDITION: a key or value handed to add must have come from the
 *     allocator of the list it lands in - the key list for a key, the value list for
 *     a value - because that is the allocator that will release it. Those two are the
 *     same for every map built by this file's constructors. THE MISMATCH SEVERITY IS
 *     ASYMMETRIC: on an arena-backed list a wrong-allocator pointer is a LEAK (the
 *     arena's release is a no-op for it), but on a heap-backed list release reaches
 *     bare free(), so an arena-borrowed pointer is REAL CORRUPTION - surfacing at
 *     uninit, far from the add that caused it.
 *
 *     THIS RULE HOLDS FOR THIS SHAPE ONLY; see map_char_string.h and map_char_al_char.h
 *     for the other two. It holds here because an AL_Char element is a bare char* that
 *     al_char releases through the LIST's allocator. On those two files the value element
 *     carries its own allocator field instead, so a helper written to "release through
 *     the list" would borrow from one allocator and return through another - the exact
 *     cross-allocator free this module was fixed for. The KEY side is char* in all nine
 *     and replicates unchanged.
 *   - KEYS AND COPIED VALUES MUST BE NUL-TERMINATED. Lookup measures the STORED key
 *     with char_length, so an unterminated key is read past its allocation on every
 *     scan; and every string this map COPIES - add_static's two halves, and both
 *     halves of init_3/alloc_init_3 - is measured the same way to size the copy. An
 *     adopted VALUE is never measured, so only copied values are covered. Empty is
 *     fine everywhere; unterminated is not.
 *   - map_char_char_init_3 DEEP-COPIES: both the arrays and the strings. The caller
 *     keeps everything it passed in and releases it normally. It has to work this
 *     way - al_char's only public teardown releases every element, so a contract that
 *     said "keep your lists but do not release their elements" asked for a primitive
 *     that does not exist, and a caller following it double-freed every string.
 *   - A stored nullptr is a legal element value and is skipped on release.
 *   - Element pointers - from at_*, get_key, get_value - are INVALIDATED by remove,
 *     clear and uninit, which release the element itself. Growth does not invalidate
 *     them: it reallocates the array of POINTERS, not what they point to.
 *
 *     THAT LAST SENTENCE HOLDS FOR THIS SHAPE ONLY; see map_char_u64.h for the scalar
 *     shape, where `at` returns a pointer INTO the value array and growth reallocates
 *     the exact object the caller is holding - the pointer must be treated as dead
 *     after any insertion - and map_char_al_char.h / map_char_string.h for the struct
 *     shapes, where the returned AL_Char* / String* dies with the array while the
 *     buffer it points at does not. Only THIS file's values live in allocations of
 *     their own, which is why only this file's paragraph may say growth is harmless.
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
 *     remove-then-look-up can still find the shadowed second entry. There is
 *     deliberately no replacing add: replacement plus an ownership transfer would
 *     release the previous value out from under a pointer the caller may still hold.
 *   - TO CHANGE A VALUE: remove_1 then add. That costs two scans, and it is the only
 *     supported sequence. Writing through an at_* result is safe only within the stored
 *     value's own length - the pointer is mutable because the value is yours once the
 *     map holds it, not because the slot can grow.
 *
 * Family:
 *   One of the nine string-keyed maps of container/map - two parallel AL_* lists scanned
 *   linearly, one 34-function API in three value shapes; tools/map_divergence.py proves the
 *   generated scalars match their anchor and each canonical header matches its own stated
 *   counts. This is the POINTER shape: add admits a nullptr value (see the NULLPTR-VALUE
 *   IDIOM note on add) and add_static copies both halves. Suite:
 *   tests/container/map/test_map_char_char.c.
 *
 * Dependencies:
 *   - <container/arrayList/al_char.h> - the only direct include, and the substrate: both
 *     halves of the map ARE AL_Chars, and their teardown is what makes the map own what
 *     it holds. It transitively supplies char/ (char_length, char_copy_3,
 *     char_compare_equal_2), allocator/ (allocator_try_borrow, allocator_release),
 *     memory/ (memory_empty) and, under ARENA_IMPLEMENTATION, arena/ - all of which this
 *     module uses directly. One include, five couplings.
 *
 * See map_char_char.c for implementation details.
 */
#ifndef CONTAINER_MAP_CHAR_CHAR_H
#define CONTAINER_MAP_CHAR_CHAR_H

#include <container/arrayList/al_char.h>

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief Map from string keys to string values.
 *
 * Two parallel lists at matching indices. There is deliberately no separate count:
 * the size is DERIVED from the lists, so it cannot disagree with them. The LISTS can
 * still be desynced through the mutable get_keys/get_values handles - what cannot
 * happen is a stale third counter disagreeing with both; a half-grown pair simply
 * does not count until its other half lands, which is why get_size takes the SMALLER
 * of the two sides.
 */
typedef struct {
    AL_Char key;   /**< Keys, at matching indices with `value`. */
    AL_Char value; /**< Values, at matching indices with `key`. */
} Map_Char_Char;

/*==============================================================================
 * MARK: - API
 *============================================================================*/

/**
 * @brief Add a key-value pair, taking ownership of both.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key string. Must not be nullptr, and must be NUL-terminated. OWNERSHIP
 *        TRANSFERS on success.
 * @param value Value string. OWNERSHIP TRANSFERS on success; may be nullptr, which is how
 *        a key is added with no value yet - a later at_* answers nullptr for it, and
 *        contains_* is what tells that apart from the key being absent.
 * @note THE NULLPTR-VALUE IDIOM IS UNIQUE TO THIS INSTANTIATION - it replicates nowhere,
 *       and the three buckets are the same three the invalidation paragraph names:
 *         - here, the value is a bare char* and this add is the only one that omits the
 *           null check on it, which is what makes the idiom expressible at all;
 *         - the six scalar variants take the value BY VALUE, so there is nothing to null
 *           and at returns a pointer into the array, making a nullptr return mean
 *           "absent" unambiguously;
 *         - map_char_string and map_char_al_char take a T** handle and MOVE out of it,
 *           and their add carries error_check_null on the handle AND the struct behind
 *           it - so passing nullptr at either level ABORTS. Do not read this note as
 *           licensing it.
 *       contains_* exists on all nine for a related reason: presence as a QUESTION,
 *       answered without touching the value - and on this file it is the only way to
 *       tell a stored nullptr from an absent key.
 * @return true when the pair was stored; false when the allocator declined.
 * @note On false NOTHING was taken - the map is unchanged and the caller still owns
 *       both pointers and must release them.
 * @note Does not check for an existing key; see "Duplicate Keys" above.
 * @see map_char_char_add_static
 */
bool map_char_char_add(Map_Char_Char *const self, char *const key, char *const value);

/**
 * @brief Add a key-value pair by COPYING both, leaving the originals with the caller.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key string. Must not be nullptr, and must be NUL-terminated. Copied; the
 *        caller keeps the original.
 * @param value Value string. Copied, so it must be NUL-terminated too; may be nullptr,
 *        making this the copying twin of add(self, key, nullptr).
 * @return true when the pair was stored; false when a copy or the allocator declined.
 * @note This is the entry point for string literals and for any key or value whose
 *       lifetime the caller needs to keep. A partial copy is released before
 *       returning false, so a decline leaks nothing.
 * @note Each half is copied through the allocator of the LIST it lands in, not through
 *       one allocator for both, so the copy always goes back to where it came from.
 * @note FAMILY RULE: "static" names what happens to the KEY, and only to the key. That it
 *       copies the VALUE as well is a coincidence of this file's value being a char*,
 *       not part of the definition - the same discipline that made the _N suffix encode
 *       the key argument alone. On the other eight, add_static copies the key and then
 *       does whatever that file's add does with the value: the six scalar variants take
 *       it BY VALUE, and map_char_al_char / map_char_string take a T** handle and MOVE
 *       out of it, vacating the caller's local. Carrying "copies both" to a sibling is
 *       wrong in both directions - a scalar has nothing to double-copy, and a vacated
 *       struct handle's own uninit is a harmless no-op, not a double free.
 * @see map_char_char_add
 */
bool map_char_char_add_static(Map_Char_Char *const self, char const *const key, char const *const value);

/**
 * @brief Add a key-value pair by COPYING bytes of explicit size.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key bytes. Must not be nullptr; NEED NOT be NUL-terminated.
 * @param key_size Key length in bytes. 0 is a legal key, not an error. MUST NOT exceed the
 *        readable bytes at `key` - the size reaches a copy as a read length, so an over-large
 *        one is a heap over-read rather than a decline. Same contract as at_2.
 * @param value Value bytes; may be nullptr.
 * @param value_size Value length in bytes, under the same read-extent contract as `key_size`.
 *        Ignored when `value` is nullptr.
 * @return true when the pair was stored; false when a copy or the allocator declined.
 * @note The sized form is the PRIMITIVE - add_static measures and forwards here. It is the
 *       counterpart to at_2: a key that is a slice of a larger buffer can now be stored as
 *       well as looked up, without copying it out first.
 * @note What is STORED is always NUL-terminated, whatever was passed in, so the terminated
 *       precondition the module states for keys is satisfied by construction here.
 * @note FAMILY SHAPE: the six scalar variants take their value by value, so their
 *       add_static_2 is add_static_2(self, key, key_size, value) - no value_size. The _2
 *       suffix encodes the KEY argument and nothing else; a shorter parameter list does
 *       not change what it means.
 */
bool map_char_char_add_static_2(Map_Char_Char *const self, char const *const key, USize const key_size, char const *const value, USize const value_size);

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Initialize an empty map with an arena allocator.
 * @param allocator Arena pointer. Must not be nullptr.
 * @return Initialized Map_Char_Char.
 */
Map_Char_Char map_char_char_alloc_init_1(Arena *allocator);

/**
 * @brief Initialize a map with a starting capacity and an arena allocator.
 * @param capacity Initial capacity for both lists. Must not be 0.
 * @param allocator Arena pointer. Must not be nullptr.
 * @return Initialized Map_Char_Char.
 * @note A zero capacity ABORTS, matching al_char_init_2. Use alloc_init_1 for empty.
 */
Map_Char_Char map_char_char_alloc_init_2(USize const capacity, Arena *allocator);

/**
 * @brief Initialize a map by COPYING key and value lists, with an arena allocator.
 * @param keys Source keys. Must not be nullptr.
 * @param values Source values. Must not be nullptr.
 * @param allocator Arena pointer. Must not be nullptr.
 * @return Initialized Map_Char_Char.
 * @note DEEP-COPIES: the arrays AND the strings. The caller keeps both lists and every
 *       string in them, and releases them normally - al_char_uninit on each is correct.
 * @note Every string copied is measured with char_length, so both lists' elements must be
 *       NUL-terminated. A stored nullptr is fine and copies as itself.
 * @note ALL OR NOTHING. A declined copy leaves an EMPTY map rather than a partial one,
 *       so the result is either a copy of the first N pairs or nothing at all. Check
 *       map_char_char_get_size against your own count if the distinction matters.
 * @note Pairs beyond the shorter of the two lists are not represented.
 */
Map_Char_Char map_char_char_alloc_init_3(AL_Char const *const keys, AL_Char const *const values, Arena *allocator);

/**
 * @brief Allocate and initialize an empty map with an arena allocator.
 * @param allocator Arena pointer. Must not be nullptr.
 * @return New map, or nullptr when the allocator declined. Free with map_char_char_delete.
 * @note A nullptr must NOT be handed to map_char_char_delete, which treats a null *self as
 *       a contract violation and aborts. Check before deleting.
 */
Map_Char_Char* map_char_char_alloc_new_1(Arena *allocator);

/**
 * @brief Allocate and initialize a map with a capacity and an arena allocator.
 * @param capacity Initial capacity for both lists. Must not be 0.
 * @param allocator Arena pointer. Must not be nullptr.
 * @return New map, or nullptr when the allocator declined. Free with map_char_char_delete.
 * @note A zero capacity ABORTS, checked here before forwarding. Use alloc_new_1 for empty.
 * @note A nullptr must NOT be handed to map_char_char_delete, which treats a null *self as
 *       a contract violation and aborts. Check before deleting.
 */
Map_Char_Char* map_char_char_alloc_new_2(USize const capacity, Arena *allocator);

/**
 * @brief Allocate a map by COPYING key and value lists, with an arena allocator.
 * @param keys Source keys. Must not be nullptr.
 * @param values Source values. Must not be nullptr.
 * @param allocator Arena pointer. Must not be nullptr.
 * @return New map, or nullptr when the allocator declined. Free with map_char_char_delete.
 * @note A nullptr must NOT be handed to map_char_char_delete, which treats a null *self as
 *       a contract violation and aborts. Check before deleting.
 */
Map_Char_Char* map_char_char_alloc_new_3(AL_Char const *const keys, AL_Char const *const values, Arena *allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Look up a value by a null-terminated key.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key string. Must not be nullptr; may be empty.
 * @return The value at the first matching key, or nullptr when the key is absent.
 * @note A nullptr return is ambiguous when a key was stored with a nullptr value - use
 *       map_char_char_contains_1 to tell them apart. That ambiguity is specific to THIS
 *       instantiation - the only pointer-valued one; see map_char_char_add.
 * @note FAMILY FORK: this is the ONE instantiation whose at_* returns the stored VALUE
 *       rather than the ADDRESS OF THE SLOT holding it. The signature hides it, because
 *       for a char* value a slot address and the value itself are both "a pointer".
 *       Returning char** would expose the slot and re-open replacing a value with
 *       ownership transfer, which is deliberately not offered here - which is why
 *       changing a value is remove_1 then add on this file, and a write through the
 *       at_* result on the other eight. One fork, two idioms, not two unrelated rules.
 */
char* map_char_char_at_1(Map_Char_Char const *const self, char const *const key);

/**
 * @brief Look up a value by a key of explicit size.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key bytes. Must not be nullptr; may be empty.
 * @param key_size Key length in bytes. 0 is a legal key, not an error. MUST NOT exceed the
 *        readable bytes at `key`: it is forwarded to char_compare_equal_2, which reads that
 *        many bytes whenever a stored key has the same length, so an over-large size is a
 *        heap over-read rather than a miss. contains_2 and remove_2 share the path and the
 *        contract.
 * @return The value at the first matching key, or nullptr when the key is absent.
 * @note Takes a size so a key that is a slice of a larger buffer can be looked up
 *       without copying it first.
 * @note A nullptr return carries at_1's ambiguity - stored-nullptr vs absent - and
 *       map_char_char_contains_2 is the sized form that resolves it.
 */
char* map_char_char_at_2(Map_Char_Char const *const self, char const *const key, USize const key_size);

/**
 * @brief Remove every pair, releasing each key and value.
 * @param self Map pointer. Must not be nullptr.
 * @note Keeps the allocated capacity; see map_char_char_shrink to release it.
 */
void map_char_char_clear(Map_Char_Char *const self);

/**
 * @brief Report whether a null-terminated key is present.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key string. Must not be nullptr; may be empty.
 * @return true when the key is present, whatever its value.
 * @note This is how a present-but-nullptr value is told apart from an absent key.
 */
bool map_char_char_contains_1(Map_Char_Char const *const self, char const *const key);

/**
 * @brief Report whether a key of explicit size is present.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key bytes. Must not be nullptr; may be empty.
 * @param key_size Key length in bytes. 0 is a legal key, not an error.
 * @return true when the key is present, whatever its value.
 * @note This is at_2's disambiguator, exactly as contains_1 is at_1's: present-but-nullptr
 *       and absent both answer nullptr from the lookup, and only this tells them apart.
 * @note Same read-extent contract as at_2.
 */
bool map_char_char_contains_2(Map_Char_Char const *const self, char const *const key, USize const key_size);

/**
 * @brief Release a map allocated by map_char_char_new_* or map_char_char_alloc_new_*.
 * @param self Address of the map pointer. Must not be nullptr, and neither may *self.
 * @note Releases every key and value, then the map itself. The STRUCT goes back to the
 *       key list's allocator, which is exact for every map these constructors build; a
 *       map assembled by hand from two lists with different allocators must not be
 *       deleted through this. Nulls the caller's pointer only under
 *       MEMORY_NON_DANGLING_POINTER, matching al_char_delete.
 */
void map_char_char_delete(Map_Char_Char **const self);

/**
 * @brief Report whether the map holds no pairs.
 * @param self Map pointer. Must not be nullptr.
 * @return true when there are no pairs.
 * @note "Empty" means no pairs, never "no allocation" - a map with reserved capacity
 *       and nothing in it is empty.
 */
bool map_char_char_empty(Map_Char_Char const *const self);

/**
 * @brief Report the capacity available for pairs.
 * @param self Map pointer. Must not be nullptr.
 * @return The smaller of the two lists' capacities, since a pair needs a slot in both.
 */
USize map_char_char_get_capacity(Map_Char_Char const *const self);

/**
 * @brief Get the key at an index.
 * @param self Map pointer. Must not be nullptr.
 * @param index Index. Must be below map_char_char_get_size.
 * @return The key, which may be nullptr if one was stored.
 */
char* map_char_char_get_key(Map_Char_Char const *const self, USize const index);

/**
 * @brief Get the keys list.
 * @param self Map pointer. Must not be nullptr.
 * @return The keys list.
 * @note Takes a mutable map because the returned handle is mutable: writing through
 *       it changes the map, and pairing is the caller's responsibility from then on.
 * @note NOT the iteration API - get_key/get_value iterate over a const map and cannot
 *       desync it. This exists as the ESCAPE HATCH: it is what lets one list's capacity
 *       be tuned independently (al_char_reserve on the handle) without the map carrying
 *       four per-list wrappers that nothing in the tree ever called.
 */
AL_Char* map_char_char_get_keys(Map_Char_Char *const self);

/**
 * @brief Report the number of pairs.
 * @param self Map pointer. Must not be nullptr.
 * @return The smaller of the two lists' sizes, since a pair needs both halves.
 * @note DERIVED rather than stored, so it cannot disagree with the lists.
 */
USize map_char_char_get_size(Map_Char_Char const *const self);

/**
 * @brief Get the value at an index.
 * @param self Map pointer. Must not be nullptr.
 * @param index Index. Must be below map_char_char_get_size.
 * @return The value, which may be nullptr if one was stored.
 */
char* map_char_char_get_value(Map_Char_Char const *const self, USize const index);

/**
 * @brief Get the values list.
 * @param self Map pointer. Must not be nullptr.
 * @return The values list.
 * @note Takes a mutable map because the returned handle is mutable; see get_keys, which
 *       also explains why this is an escape hatch rather than the iteration API.
 */
AL_Char* map_char_char_get_values(Map_Char_Char *const self);

/**
 * @brief Initialize an empty map (heap).
 * @return Initialized Map_Char_Char.
 */
Map_Char_Char map_char_char_init_1(void);

/**
 * @brief Initialize a map with a starting capacity (heap).
 * @param capacity Initial capacity for both lists. Must not be 0.
 * @return Initialized Map_Char_Char.
 * @note A zero capacity ABORTS, matching al_char_init_2. Use init_1 for empty.
 */
Map_Char_Char map_char_char_init_2(USize const capacity);

/**
 * @brief Initialize a map by COPYING key and value lists (heap).
 * @param keys Source keys. Must not be nullptr.
 * @param values Source values. Must not be nullptr.
 * @return Initialized Map_Char_Char.
 * @note DEEP-COPIES: the arrays AND the strings. The caller keeps both lists and every
 *       string in them, and releases them normally - al_char_uninit on each is correct.
 * @note Every string copied is measured with char_length, so both lists' elements must be
 *       NUL-terminated. A stored nullptr is fine and copies as itself.
 * @note ALL OR NOTHING. A declined copy leaves an EMPTY map rather than a partial one,
 *       so the result is either a copy of the first N pairs or nothing at all. Check
 *       map_char_char_get_size against your own count if the distinction matters.
 * @note Pairs beyond the shorter of the two lists are not represented.
 * @note init_3(&other.key, &other.value) IS THE COPY CONSTRUCTOR for an existing map:
 *       deep-copies both lists in one call, no separate init_4 needed.
 */
Map_Char_Char map_char_char_init_3(AL_Char const *const keys, AL_Char const *const values);

/**
 * @brief Allocate and initialize an empty map (heap).
 * @return New map, or nullptr when the allocator declined. Free with map_char_char_delete.
 * @note A nullptr must NOT be handed to map_char_char_delete, which treats a null *self as
 *       a contract violation and aborts. Check before deleting.
 */
Map_Char_Char* map_char_char_new_1(void);

/**
 * @brief Allocate and initialize a map with a capacity (heap).
 * @param capacity Initial capacity for both lists. Must not be 0.
 * @return New map, or nullptr when the allocator declined. Free with map_char_char_delete.
 * @note A zero capacity ABORTS, checked here before forwarding. Use new_1 for empty.
 * @note A nullptr must NOT be handed to map_char_char_delete, which treats a null *self as
 *       a contract violation and aborts. Check before deleting.
 */
Map_Char_Char* map_char_char_new_2(USize const capacity);

/**
 * @brief Allocate a map by COPYING key and value lists (heap).
 * @param keys Source keys. Must not be nullptr.
 * @param values Source values. Must not be nullptr.
 * @return New map, or nullptr when the allocator declined. Free with map_char_char_delete.
 * @note A nullptr must NOT be handed to map_char_char_delete, which treats a null *self as
 *       a contract violation and aborts. Check before deleting.
 */
Map_Char_Char* map_char_char_new_3(AL_Char const *const keys, AL_Char const *const values);

/**
 * @brief Remove the first pair with a null-terminated key.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key string. Must not be nullptr; may be empty.
 * @return true when a pair was removed.
 * @note Releases the stored key and value. Removes only the FIRST match, so a
 *       duplicate key leaves the shadowed pair in place.
 */
bool map_char_char_remove_1(Map_Char_Char *const self, char const *const key);

/**
 * @brief Remove the first pair with a key of explicit size.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key bytes. Must not be nullptr; may be empty.
 * @param key_size Key length in bytes. 0 is a legal key, not an error.
 * @return true when a pair was removed.
 * @note Same read-extent contract as at_2. Note what that means for a key holding an
 *       embedded NUL: the STORED key is measured with char_length, so only its leading
 *       segment ever matches. Such a key is findable and removable by that segment, never
 *       by its full byte string.
 */
bool map_char_char_remove_2(Map_Char_Char *const self, char const *const key, USize const key_size);

/**
 * @brief Remove the pair at an index.
 * @param self Map pointer. Must not be nullptr.
 * @param index Index. Must be below map_char_char_get_size.
 * @note The indexed counterpart to get_key/get_value, and the only correct way to delete
 *       something found by iterating. remove_1(map_char_char_get_key(map, 3)) removes the
 *       FIRST pair with that key, which is a different pair whenever key 3 is a duplicate -
 *       a silent wrong answer reachable straight from the documented iteration idiom.
 */
void map_char_char_remove_at(Map_Char_Char *const self, USize const index);

/**
 * @brief Reserve capacity in both lists.
 * @param self Map pointer. Must not be nullptr.
 * @param capacity Minimum capacity. Must not be 0.
 * @note A zero capacity ABORTS, matching al_char_reserve.
 */
void map_char_char_reserve(Map_Char_Char *const self, USize const capacity);

/**
 * @brief Shrink both lists to their sizes.
 * @param self Map pointer. Must not be nullptr.
 * @note ON AN ARENA THIS DOES NOT FREE ANYTHING, and costs memory. al_char_shrink borrows
 *       a smaller array and releases the old one, but arena_linear_free is a no-op - so
 *       the arena's high-water mark RISES and the map moves toward exhaustion. Shrinking
 *       to reclaim arena space does the opposite of what it looks like.
 * @note That borrow is also one of the three aborting sites listed under Error Handling:
 *       shrinking a large map into a nearly-full arena can end the process.
 */
void map_char_char_shrink(Map_Char_Char *const self);

/**
 * @brief Release all map storage and reset to the empty state.
 * @param self Map pointer. Must not be nullptr.
 * @note Releases every key and value through the lists' allocator. Safe to call twice.
 * @note The allocator survives: after uninit the map is a valid EMPTY map on the same
 *       backing, and may be reused without re-initialization.
 */
void map_char_char_uninit(Map_Char_Char *const self);

#endif // CONTAINER_MAP_CHAR_CHAR_H