/*
 * map_char_al_char.h - Map from char* to AL_Char (string-to-list map) for the C Libraries Framework
 *
 * Features:
 *   - Map from string keys to array-list values, over parallel AL_Char and AL_AL_Char lists
 *   - Arena or heap allocation support
 *   - Add, look up, remove, and iterate key-value pairs
 *
 * Usage Examples:
 *   @code
 *   // add MOVES the value: the source list is emptied and your pointer nulled, so a
 *   // retained alias cannot double-free. The key is a plain pointer the map adopts.
 *   Map_Char_AL_Char columns = map_char_al_char_init_1();
 *
 *   AL_Char column = al_char_init_1();
 *
 *   al_char_add_last(&column, char_new_2("first cell"));  // heap: cannot decline
 *
 *   AL_Char *moving = &column;
 *
 *   // Named, not passed as a temporary: on a decline NOTHING was taken, and an unnamed
 *   // temporary is a pointer you can no longer reach to release.
 *   char *const key = char_new_2("name");
 *
 *   if (map_char_al_char_add(&columns, key, &moving)) {
 *       // Taken: moving == nullptr and `column` is empty - the buffer belongs to the map.
 *       // An al_char_uninit of the vacated local is now a harmless no-op, not a double free.
 *   }
 *   else {
 *       // `moving` still points at a `column` that still holds its buffer.
 *       char_delete(key);
 *       al_char_uninit(&column);
 *   }
 *
 *   AL_Char *const found = map_char_al_char_at_1(&columns, "name");
 *
 *   map_char_al_char_uninit(&columns);
 *   @endcode
 *
 * Error Handling:
 *   Contract violations go through error_check_*, which LOGS AND ABORTS the process.
 *   It does not return early: these are programming errors, not runtime conditions.
 *   Three classes, and the counts are exact rather than illustrative:
 *     - a NULL POINTER argument, at forty-six public sites across thirty of the
 *       thirty-four functions - self on the eighteen that do not forward it (at_1,
 *       contains_1, remove_1 and add_static check it one frame deeper, in the _2 form
 *       they call), key on the nine that take one, value AND *value on add and
 *       add_static_2 - the handle and the list behind it are separate contracts -
 *       keys/values on the four copying constructors, allocator on the six arena entry
 *       points, and *self on delete;
 *     - an INDEX at or past map_char_al_char_get_size, on get_key, get_value and remove_at;
 *     - a ZERO CAPACITY, at five sites: init_2, alloc_init_2, reserve, and new_2 and
 *       alloc_new_2, which carry their own check before forwarding.
 *
 *   A capacity is therefore a CALLER CONTRACT, not an input. Never pass an unvalidated
 *   remote length to reserve or to a capacity constructor. A count parsed from a request,
 *   a file header or a config value is remote in the sense that matters, whether or not
 *   it arrived over a socket.
 *
 *   Conditions that depend on a VALUE rather than on a broken contract are refused instead,
 *   and never abort:
 *     - an allocator that REFUSES leaves the map unchanged, and add() declines with it,
 *       returning false and leaving BOTH halves with the caller. Aborting borrows still
 *       exist one level down: in al_char / al_al_char reserve, init_2 and shrink, AND in
 *       init_3's value copy, which sizes each copied list with al_char_init_2 /
 *       al_char_alloc_init_2 - the direct-borrow capacity constructor, not reserve. Every
 *       borrow this module makes for ITSELF uses the non-aborting path; these are the ones
 *       it makes through somebody else;
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
 *   - THE MAP OWNS BOTH HALVES, BUT THEY ARE OWNED DIFFERENTLY, and this is the paragraph
 *     that does not transfer from the other instantiations.
 *
 *     The KEY is a char* released through the KEY LIST's allocator, exactly as in
 *     map_char_char.
 *
 *     The VALUE is an AL_Char taken by SHALLOW STRUCT COPY. al_al_char_remove uninits the
 *     stored element, which releases through THAT AL_Char's OWN allocator field and frees
 *     every string in it - not through the value list's allocator. So a value handed to
 *     add must be a list whose own allocator is the one that should free it, and the map
 *     must never be given a helper that copies or releases values "through the value
 *     list": that would borrow from one allocator and return through another. The value copy
 *     _map_char_al_char_copy_value DOES exist, and deliberately routes through the KEY
 *     list's allocator for exactly that reason - what this module has no helper for is
 *     copying or releasing THROUGH THE VALUE LIST, and that asymmetry is the point.
 *
 *     FAMILY RULE - STRUCT VALUES MOVE VIA T**. Both struct-valued maps (this one and
 *     map_char_string) take the value as a pointer-to-pointer, and once both halves have
 *     landed they EMPTY the source and null the caller's pointer, so at no instant do two
 *     unconditional claimants of one buffer exist - the move makes a double-free impossible
 *     by construction instead of by warning. What `owned` additionally buys map_char_string
 *     - storing a VIEW the map must not free - an AL_Char cannot express, but that is about
 *     what a String IS, not about how a struct value transfers.
 *   - A DECLINED add returns false and takes nothing: the key stays the caller's, and the
 *     source list is untouched - still holding its buffer, its elements, and the caller's
 *     pointer to it. The vacate runs only AFTER both halves land.
 *   - KEYS MUST BE NUL-TERMINATED. Lookup measures the STORED key with char_length.
 *   - map_char_al_char_init_3 DEEP-COPIES both halves - the keys, and each value list
 *     along with every string in it. The caller keeps its source lists entire and releases
 *     them normally. That is the only contract al_al_char can actually honour: its only
 *     public teardown releases every element, so an adopting constructor would ask the
 *     caller for a detach primitive that does not exist.
 *   - A STORED LIST'S OWN ALLOCATOR GOVERNS WHAT MAY BE APPENDED TO IT. at_* hands back a
 *     mutable AL_Char, and every element in it is released through THAT list's allocator,
 *     not through the map's. So a char* appended through an at_* result must have come from
 *     the same allocator the stored list carries; appending one borrowed elsewhere is the
 *     cross-allocator free this section exists to prevent, arriving through the mutation
 *     path the file endorses. A parser of remote input doing exactly this append is correct
 *     only because its own copy helper uses that allocator.
 *   - VALUE pointers - from at_*, get_value - point INTO the value array and are
 *     INVALIDATED BY GROWTH, and by remove, remove_at, clear and uninit. All four, not
 *     just growth: al_al_char_remove calls al_char_uninit on the element BEFORE it shifts
 *     (al_al_char.c), so the stored list's buffer and every string in it are freed, and
 *     clear/uninit do the same to every element. Do not read "the list survives, only the
 *     pointer dies" into this - the list does not survive a removal either.
 *   - The list handles from get_keys/get_values are valid for the map's whole lifetime.
 *
 * Performance Characteristics:
 *   - Lookup, contains and remove are a LINEAR SCAN - O(n) in the number of pairs.
 *   - Amortized O(1) append; O(n) remove at an arbitrary index; O(1) indexed access.
 *   - init_3 is O(total bytes): it copies every string in every value list.
 *
 * Duplicate Keys:
 *   - add does NOT check for an existing key. Every lookup answers the FIRST match, and
 *     remove deletes the FIRST, so a duplicate leaves a shadowed pair behind.
 *   - TO CHANGE A VALUE: mutate the AL_Char through an at_* result - it is the stored list,
 *     not a copy. remove_1 then add also works, and is what you want when the replacement is
 *     a whole new list rather than an edit to this one. (map_char_string states the same two
 *     options in the same order, with an owner-vs-view caveat that does not arise here.)
 *
 * Family:
 *   One of the nine string-keyed maps of container/map - two parallel AL_* lists scanned
 *   linearly, one 34-function API in three value shapes; tools/map_divergence.py proves the
 *   generated scalars match their anchor and each canonical header matches its own stated
 *   counts. This is one of the two STRUCT-BY-MOVE shapes: add takes AL_Char** and the source
 *   list is VACATED once both halves land. Suite: tests/container/map/test_map_char_al_char.c.
 *
 * Dependencies:
 *   - <container/arrayList/al_char.h> - the key half.
 *   - <container/arrayList/al_al_char.h> - the value half, whose elements carry their own
 *     allocators, which is what makes this instantiation's ownership rule different.
 *     Between them they transitively supply char/ (char_length, char_copy_3,
 *     char_compare_equal_2), allocator/ (allocator_try_borrow, allocator_release),
 *     memory/ (memory_empty) and, under ARENA_IMPLEMENTATION, arena/.
 *
 * See map_char_al_char.c for implementation details.
 */
#ifndef CONTAINER_MAP_CHAR_AL_CHAR_H
#define CONTAINER_MAP_CHAR_AL_CHAR_H

#include <container/arrayList/al_al_char.h>
#include <container/arrayList/al_char.h>

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief Map from string keys to AL_Char values.
 *
 * Two parallel lists at matching indices. There is deliberately no separate count: the
 * size is DERIVED from the lists, so it cannot disagree with them.
 */
typedef struct {
    AL_Char key;      /**< Keys, at matching indices with `value`. */
    AL_AL_Char value; /**< Values, at matching indices with `key`. */
} Map_Char_AL_Char;

/*==============================================================================
 * MARK: - API
 *============================================================================*/

/**
 * @brief Add a key-value pair - adopting the key, MOVING the value.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key string. Must not be nullptr, and must be NUL-terminated. OWNERSHIP
 *        TRANSFERS on success.
 * @param value Address of the caller's AL_Char pointer. Neither it nor the list it points
 *        at may be nullptr. On success the SOURCE IS VACATED (empty, no buffer) and
 *        *value is set to nullptr, so a retained alias cannot double-free and cannot go
 *        stale when the stored copy grows. The source's `allocator` field is left intact.
 * @return true when the pair was stored; false when the allocator declined.
 * @note On false NOTHING was taken - the key is still the caller's, and the source list
 *       still holds its buffer with *value still pointing at it. The vacate runs only
 *       after both halves land; the rollback neutralises the tail slot before removing it
 *       rather than letting al_al_char_remove free a buffer the caller still holds.
 * @see map_char_string_add - the same move protocol on the other struct-valued map.
 */
bool map_char_al_char_add(Map_Char_AL_Char *const self, char *const key, AL_Char **const value);

/**
 * @brief Add a key-value pair by COPYING the key, MOVING the value.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key string. Must not be nullptr, and must be NUL-terminated. Copied; the
 *        caller keeps the original.
 * @param value Moved exactly as in add: vacated and nulled on success, untouched on a
 *        decline. "Static" names what happens to the KEY only - there is no copying form
 *        of a value here, because copying an AL_Char means copying every string in it and
 *        that is what init_3 is for.
 * @return true when the pair was stored; false when the copy or the allocator declined.
 */
bool map_char_al_char_add_static(Map_Char_AL_Char *const self, char const *const key, AL_Char **const value);

/**
 * @brief Add a key-value pair by COPYING a key of explicit size, MOVING the value.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key bytes. Must not be nullptr; NEED NOT be NUL-terminated.
 * @param key_size Key length in bytes. 0 is a legal key. MUST NOT exceed the readable
 *        bytes at `key`.
 * @param value Moved exactly as in add: vacated and nulled on success, untouched on a
 *        decline (a declined key copy leaves it untouched too).
 * @return true when the pair was stored; false when the copy or the allocator declined.
 * @note The sized form is the PRIMITIVE - add_static measures and forwards here. The _2
 *       suffix encodes the KEY argument and nothing else.
 */
bool map_char_al_char_add_static_2(Map_Char_AL_Char *const self, char const *const key, USize const key_size, AL_Char **const value);

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Initialize an empty map with an arena allocator.
 * @param allocator Arena pointer. Must not be nullptr.
 * @return Initialized Map_Char_AL_Char.
 */
Map_Char_AL_Char map_char_al_char_alloc_init_1(Arena *allocator);

/**
 * @brief Initialize a map with a starting capacity and an arena allocator.
 * @param capacity Initial capacity for both lists. Must not be 0.
 * @param allocator Arena pointer. Must not be nullptr.
 * @return Initialized Map_Char_AL_Char.
 * @note A zero capacity ABORTS. Use alloc_init_1 for empty.
 */
Map_Char_AL_Char map_char_al_char_alloc_init_2(USize const capacity, Arena *allocator);

/**
 * @brief Initialize a map by DEEP-COPYING key and value lists, with an arena allocator.
 * @param keys Source keys. Must not be nullptr.
 * @param values Source values. Must not be nullptr.
 * @param allocator Arena pointer. Must not be nullptr.
 * @return Initialized Map_Char_AL_Char.
 * @note DEEP-COPIES both halves, including every string inside every value list, so the
 *       caller keeps its sources entire and releases them normally.
 * @note ALL OR NOTHING for a REFUSED allocator: the map comes back empty rather than
 *       partial. A live-but-EXHAUSTED arena is different - the value copy sizes each list
 *       with al_char_alloc_init_2, which aborts rather than declining, so this constructor
 *       can end the process where add would merely return false.
 * @note Pairs beyond the shorter of the two lists are not represented.
 */
Map_Char_AL_Char map_char_al_char_alloc_init_3(AL_Char const *const keys, AL_AL_Char const *const values, Arena *allocator);

/**
 * @brief Allocate and initialize an empty map with an arena allocator.
 * @param allocator Arena pointer. Must not be nullptr.
 * @return New map, or nullptr when the allocator declined. Free with map_char_al_char_delete.
 * @note A nullptr must NOT be handed to map_char_al_char_delete, which treats a null *self as
 *       a contract violation and aborts. Check before deleting.
 */
Map_Char_AL_Char* map_char_al_char_alloc_new_1(Arena *allocator);

/**
 * @brief Allocate and initialize a map with a capacity and an arena allocator.
 * @param capacity Initial capacity for both lists. Must not be 0.
 * @param allocator Arena pointer. Must not be nullptr.
 * @return New map, or nullptr when the allocator declined. Free with map_char_al_char_delete.
 * @note A nullptr must NOT be handed to map_char_al_char_delete, which treats a null *self as
 *       a contract violation and aborts. Check before deleting.
 */
Map_Char_AL_Char* map_char_al_char_alloc_new_2(USize const capacity, Arena *allocator);

/**
 * @brief Allocate a map by DEEP-COPYING key and value lists, with an arena allocator.
 * @param keys Source keys. Must not be nullptr.
 * @param values Source values. Must not be nullptr.
 * @param allocator Arena pointer. Must not be nullptr.
 * @return New map, or nullptr when the allocator declined. Free with map_char_al_char_delete.
 * @note A nullptr must NOT be handed to map_char_al_char_delete, which treats a null *self as
 *       a contract violation and aborts. Check before deleting.
 */
Map_Char_AL_Char* map_char_al_char_alloc_new_3(AL_Char const *const keys, AL_AL_Char const *const values, Arena *allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Look up a value list by a null-terminated key.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key string. Must not be nullptr; may be empty.
 * @return The stored list at the first matching key, or nullptr when the key is absent.
 * @note THE RETURNED POINTER POINTS INTO THE VALUE ARRAY. It dies on the next growth, and
 *       also on remove, remove_at, clear and uninit - those RELEASE the list it names, not
 *       merely the slot holding it. Mutating the list through it is supported until then;
 *       it is the stored list, not a copy.
 * @note To reach a char**, use al_char_get_data on this result.
 * @note FAMILY FORK: at_* answers the ADDRESS OF THE SLOT here, and on u64 and string.
 *       map_char_char_at_1 alone answers the stored value itself, so its change-a-value
 *       idiom is remove_1 then add rather than a mutation through this pointer. One
 *       fork, two idioms - see map_char_char_at_1.
 */
AL_Char* map_char_al_char_at_1(Map_Char_AL_Char const *const self, char const *const key);

/**
 * @brief Look up a value list by a key of explicit size.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key bytes. Must not be nullptr; may be empty.
 * @param key_size Key length in bytes. 0 is a legal key, not an error. MUST NOT exceed the
 *        readable bytes at `key`: it is forwarded to char_compare_equal_2, which reads that
 *        many bytes whenever a stored key has the same length, so an over-large size is a
 *        heap over-read rather than a miss. contains_2 and remove_2 share the path and the
 *        contract.
 * @return The stored list at the first matching key, or nullptr when absent.
 */
AL_Char* map_char_al_char_at_2(Map_Char_AL_Char const *const self, char const *const key, USize const key_size);

/**
 * @brief Remove every pair, releasing each key and each value list.
 * @param self Map pointer. Must not be nullptr.
 * @note Keeps the allocated capacity; see map_char_al_char_shrink to release it.
 */
void map_char_al_char_clear(Map_Char_AL_Char *const self);

/**
 * @brief Report whether a null-terminated key is present.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key string. Must not be nullptr; may be empty.
 * @return true when the key is present, whatever its value.
 * @note Tells a present-but-EMPTY value list apart from an absent key, which at_1 cannot:
 *       an empty AL_Char is a legal value.
 */
bool map_char_al_char_contains_1(Map_Char_AL_Char const *const self, char const *const key);

/**
 * @brief Report whether a key of explicit size is present.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key bytes. Must not be nullptr; may be empty.
 * @param key_size Key length in bytes. 0 is a legal key, not an error.
 * @return true when the key is present, whatever its value.
 * @note Same read-extent contract as at_2.
 */
bool map_char_al_char_contains_2(Map_Char_AL_Char const *const self, char const *const key, USize const key_size);

/**
 * @brief Release a map allocated by map_char_al_char_new_* or _alloc_new_*.
 * @param self Address of the map pointer. Must not be nullptr, and neither may *self.
 * @note Releases every key and every value list, then the map itself through the key
 *       list's allocator. Nulls the caller's pointer only under
 *       MEMORY_NON_DANGLING_POINTER.
 */
void map_char_al_char_delete(Map_Char_AL_Char **const self);

/**
 * @brief Report whether the map holds no pairs.
 * @param self Map pointer. Must not be nullptr.
 * @return true when there are no pairs.
 */
bool map_char_al_char_empty(Map_Char_AL_Char const *const self);

/**
 * @brief Report the capacity available for pairs.
 * @param self Map pointer. Must not be nullptr.
 * @return The smaller of the two lists' capacities, since a pair needs a slot in both.
 */
USize map_char_al_char_get_capacity(Map_Char_AL_Char const *const self);

/**
 * @brief Get the key at an index.
 * @param self Map pointer. Must not be nullptr.
 * @param index Index. Must be below map_char_al_char_get_size.
 * @return The key, which may be nullptr if one was stored.
 */
char* map_char_al_char_get_key(Map_Char_AL_Char const *const self, USize const index);

/**
 * @brief Get the keys list.
 * @param self Map pointer. Must not be nullptr.
 * @return The keys list. The ESCAPE HATCH, not the iteration API - see get_key/get_value.
 */
AL_Char* map_char_al_char_get_keys(Map_Char_AL_Char *const self);

/**
 * @brief Report the number of pairs.
 * @param self Map pointer. Must not be nullptr.
 * @return The smaller of the two lists' sizes. DERIVED rather than stored.
 */
USize map_char_al_char_get_size(Map_Char_AL_Char const *const self);

/**
 * @brief Get the value list at an index.
 * @param self Map pointer. Must not be nullptr.
 * @param index Index. Must be below map_char_al_char_get_size.
 * @return The stored list. Same invalidation rule as at_1 - growth moves it, and any
 *         removal releases it.
 */
AL_Char* map_char_al_char_get_value(Map_Char_AL_Char const *const self, USize const index);

/**
 * @brief Get the values list.
 * @param self Map pointer. Must not be nullptr.
 * @return The values list. The ESCAPE HATCH; see get_keys.
 */
AL_AL_Char* map_char_al_char_get_values(Map_Char_AL_Char *const self);

/**
 * @brief Initialize an empty map (heap).
 * @return Initialized Map_Char_AL_Char.
 */
Map_Char_AL_Char map_char_al_char_init_1(void);

/**
 * @brief Initialize a map with a starting capacity (heap).
 * @param capacity Initial capacity for both lists. Must not be 0.
 * @return Initialized Map_Char_AL_Char.
 * @note A zero capacity ABORTS. Use init_1 for empty.
 */
Map_Char_AL_Char map_char_al_char_init_2(USize const capacity);

/**
 * @brief Initialize a map by DEEP-COPYING key and value lists (heap).
 * @param keys Source keys. Must not be nullptr.
 * @param values Source values. Must not be nullptr.
 * @return Initialized Map_Char_AL_Char.
 * @note DEEP-COPIES both halves, including every string inside every value list.
 * @note ALL OR NOTHING for a REFUSED allocator: the map comes back empty rather than
 *       partial. A failing HEAP is different - the value copy sizes each list with
 *       al_char_init_2, which aborts rather than declining, so this constructor can end
 *       the process where add would merely return false. (No arena is reachable from this
 *       constructor: a heap-built map's allocator is null, so the copy takes the heap
 *       branch. See alloc_init_3 for the arena twin.)
 * @note Pairs beyond the shorter of the two lists are not represented.
 * @note init_3(&other.key, &other.value) IS THE COPY CONSTRUCTOR for an existing map:
 *       deep-copies both lists in one call, no separate init_4 needed.
 */
Map_Char_AL_Char map_char_al_char_init_3(AL_Char const *const keys, AL_AL_Char const *const values);

/**
 * @brief Allocate and initialize an empty map (heap).
 * @return New map, or nullptr when the allocator declined. Free with map_char_al_char_delete.
 * @note A nullptr must NOT be handed to map_char_al_char_delete, which treats a null *self as
 *       a contract violation and aborts. Check before deleting.
 */
Map_Char_AL_Char* map_char_al_char_new_1(void);

/**
 * @brief Allocate and initialize a map with a capacity (heap).
 * @param capacity Initial capacity for both lists. Must not be 0.
 * @return New map, or nullptr when the allocator declined. Free with map_char_al_char_delete.
 * @note A nullptr must NOT be handed to map_char_al_char_delete, which treats a null *self as
 *       a contract violation and aborts. Check before deleting.
 */
Map_Char_AL_Char* map_char_al_char_new_2(USize const capacity);

/**
 * @brief Allocate a map by DEEP-COPYING key and value lists (heap).
 * @param keys Source keys. Must not be nullptr.
 * @param values Source values. Must not be nullptr.
 * @return New map, or nullptr when the allocator declined. Free with map_char_al_char_delete.
 * @note A nullptr must NOT be handed to map_char_al_char_delete, which treats a null *self as
 *       a contract violation and aborts. Check before deleting.
 */
Map_Char_AL_Char* map_char_al_char_new_3(AL_Char const *const keys, AL_AL_Char const *const values);

/**
 * @brief Remove the first pair with a null-terminated key.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key string. Must not be nullptr; may be empty.
 * @return true when a pair was removed.
 * @note Releases the stored key and uninits the stored value list. Removes only the FIRST
 *       match.
 */
bool map_char_al_char_remove_1(Map_Char_AL_Char *const self, char const *const key);

/**
 * @brief Remove the first pair with a key of explicit size.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key bytes. Must not be nullptr; may be empty.
 * @param key_size Key length in bytes. 0 is a legal key, not an error.
 * @return true when a pair was removed.
 * @note Same read-extent contract as at_2.
 */
bool map_char_al_char_remove_2(Map_Char_AL_Char *const self, char const *const key, USize const key_size);

/**
 * @brief Remove the pair at an index.
 * @param self Map pointer. Must not be nullptr.
 * @param index Index. Must be below map_char_al_char_get_size.
 * @note The indexed counterpart to get_key/get_value, and the only correct way to delete
 *       something found by iterating - a keyed remove takes the FIRST match, which is a
 *       different pair whenever the key is duplicated.
 */
void map_char_al_char_remove_at(Map_Char_AL_Char *const self, USize const index);

/**
 * @brief Reserve capacity in both lists.
 * @param self Map pointer. Must not be nullptr.
 * @param capacity Minimum capacity. Must not be 0.
 * @note A zero capacity ABORTS.
 */
void map_char_al_char_reserve(Map_Char_AL_Char *const self, USize const capacity);

/**
 * @brief Shrink both lists to their sizes.
 * @param self Map pointer. Must not be nullptr.
 * @note ON AN ARENA THIS DOES NOT FREE ANYTHING, and costs memory: arena_linear_free is a
 *       no-op, so the shrink bump-allocates smaller arrays and abandons the old ones,
 *       moving the arena TOWARD exhaustion. Those borrows can also abort.
 */
void map_char_al_char_shrink(Map_Char_AL_Char *const self);

/**
 * @brief Release all map storage and reset to the empty state.
 * @param self Map pointer. Must not be nullptr.
 * @note Releases every key, and uninits every stored value list - which releases each
 *       through ITS OWN allocator, not the map's. Safe to call twice.
 */
void map_char_al_char_uninit(Map_Char_AL_Char *const self);

#endif // CONTAINER_MAP_CHAR_AL_CHAR_H