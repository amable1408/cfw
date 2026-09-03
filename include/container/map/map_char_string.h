/*
 * map_char_string.h - Map from char* to String for the C Libraries Framework
 *
 * Features:
 *   - Map from string keys to String values, over parallel AL_Char and AL_String lists
 *   - Arena or heap allocation support
 *   - Add, look up, remove, and iterate key-value pairs
 *
 * Usage Examples:
 *   @code
 *   // add MOVES the value: the source String is emptied and your pointer is nulled, so a
 *   // retained alias cannot double-free. The key is a plain pointer the map adopts.
 *   Map_Char_String headers = map_char_string_init_1();
 *
 *   // string_init_static makes an OWNING copy. "static" means the opposite here from what
 *   // it means in add_static, where it is the CALLER who keeps the original.
 *   String value = string_init_static("application/json", 16);
 *   String *moving = &value;
 *
 *   // Named, not passed as a temporary: on a decline NOTHING was taken, and an unnamed
 *   // temporary is a pointer you can no longer reach to release.
 *   char *const key = char_new_2("content-type");
 *
 *   if (map_char_string_add(&headers, key, &moving)) {
 *       // Taken: moving == nullptr and `value` is empty - the buffer belongs to the map.
 *   }
 *   else {
 *       // `moving` still points at a `value` that still holds its buffer. Asserting the
 *       // move outside this branch is the mistake the branch exists to prevent.
 *       char_delete(key);
 *       string_uninit(&value);
 *   }
 *
 *   String *const found = map_char_string_at_1(&headers, "content-type");
 *
 *   map_char_string_uninit(&headers);
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
 *       add_static_2 - the handle and the String behind it are separate contracts -
 *       keys/values on the four copying constructors, allocator on the six arena entry
 *       points, and *self on delete;
 *     - an INDEX at or past map_char_string_get_size, on get_key, get_value and remove_at;
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
 *       returning false and leaving BOTH halves with the caller - including the source
 *       String, which is NOT emptied on a decline. Aborting borrows still exist one level
 *       down: in al_char / al_string reserve, init_2 and shrink, AND in init_3's value copy,
 *       which goes through string_init_2 / string_alloc_init_2. That last one matters most,
 *       because init_3 is the entry point whose contract sounds like it cannot fail - see
 *       its note. One more is reached by the idiom this header RECOMMENDS: growing a stored
 *       String through an at_* result goes through string_reserve, which borrows with
 *       allocator_borrow - the aborting path. al_string_reserve grows the array OF String
 *       structs and is a different function; naming it does not cover this one. Every borrow
 *       this module makes for ITSELF uses the non-aborting path; these are the ones it makes
 *       through somebody else;
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
 *   - THE MAP OWNS THE KEY OUTRIGHT, AND OWNS THE VALUE ONLY IF THE VALUE DID.
 *
 *     The KEY is a char* released through the KEY LIST's allocator, exactly as in
 *     map_char_char.
 *
 *     The VALUE is a String, and a String is either an OWNER or a VIEW - `owned` tells
 *     them apart (see string.h). add takes String** but stores the struct BY VALUE, so
 *     `owned` travels into the list with it, and al_string_remove's string_uninit then does
 *     the right thing automatically: it releases an owner and no-ops a view. So a view over
 *     a network buffer you do not own, or a request's interior buffer, is safe TO STORE AND
 *     TO DROP, with no separate API - the map will not free what the value never claimed.
 *     That is why this instantiation needs no value-copy helper and no allocator of its own.
 *
 *     It is NOT safe to WRITE to, and the rule is stated in full on `owned` in string.h -
 *     read it there rather than trusting a summary here. In short: a view CLAIMS capacity
 *     data_size + CHAR_END_CHARACTER, every String writer that bounds on CAPACITY rather than
 *     on SIZE may write that last byte, and it is a PRECONDITION on the buffer you viewed
 *     that the byte is yours to write. string_replace_2, string_copy, string_format and
 *     string_repeat all reach it, and so does an APPEND once string_clear / string_erase /
 *     string_remove has lowered `size`. Do not treat that as a closed list - apply the rule
 *     to any writer that bounds on capacity, not just the ones named here.
 *
 *     What that means HERE: storing a view is safe, because teardown never frees what a view
 *     did not claim - but if you stored a view over a network buffer you do not own or a
 *     slice of a request buffer, sized exactly to its bytes, then the view already violates
 *     string_init_4's contract and the mutate-through-at_* idiom below is what makes it
 *     bite. The writers that bound on size instead merely destroy the payload in place,
 *     which is bad enough when something else still reads it.
 *
 *     Nor is there a cross-allocator RELEASE hazard on the value side: string_uninit
 *     releases through the STRING's own allocator field, so a value built on one arena and
 *     stored in a map built on another still goes back to the arena it came from. What that
 *     does NOT buy you is lifetime: the value's arena must outlive the map, exactly as a
 *     view's buffer must, or at_1 hands out a pointer into reclaimed storage.
 *     (string_move_4 refuses that case, but only because it rewrites a DESTINATION
 *     String's allocator, which also governs where its struct is released. Appending a
 *     fresh struct copy into a list does not.)
 *   - add MOVES rather than aliases. It takes String** and, once both halves have landed,
 *     empties the source and nulls the caller's pointer - mirroring string_move_4, whose
 *     comment explains why the SOURCE and not merely the caller's variable must be cleared:
 *     a retained alias with a live data pointer and `owned` still set is a second owner of
 *     the same buffer, and the next string_uninit is a double free. The source's `allocator`
 *     is deliberately left alone, because it records where the STRUCT was borrowed and
 *     string_delete still needs it.
 *
 *     Do NOT call string_clear on the source afterwards: it keeps the buffer and zeroes the
 *     CONTENTS, which after a successful add is the map's data.
 *   - A DECLINED add returns false and takes nothing: the key stays the caller's, the source
 *     String is untouched and still holds its buffer, and the caller's pointer is not nulled.
 *   - KEYS MUST BE NUL-TERMINATED. Lookup measures the STORED key with char_length.
 *   - map_char_string_init_3 DEEP-COPIES both halves - the keys, and each value through
 *     string_init_6 / string_alloc_init_6. Note what that does to a VIEW: a deep copy is an
 *     OWNER, so init_3 PROMOTES a source view into a map-owned buffer. add preserves
 *     view-ness and init_3 destroys it, which is the one place the two differ in more than
 *     depth. It is the right default for a constructor whose contract is "the caller keeps
 *     everything", but it is not a detail to discover.
 *   - VALUE pointers - from at_*, get_value - point INTO the value array and are
 *     INVALIDATED BY GROWTH, and by remove, remove_at, clear and uninit. All four:
 *     al_string_remove calls string_uninit on the element before it shifts, and
 *     al_string_clear uninits every element - so an OWNING value is freed, and even a
 *     stored VIEW has its slot shifted and the tail reset, which means a held String*
 *     silently starts naming a different pair. "The String survives, only the pointer
 *     dies" is true of neither case.
 *   - The list handles from get_keys/get_values are valid for the map's whole lifetime.
 *
 * Performance Characteristics:
 *   - Lookup, contains and remove are a LINEAR SCAN - O(n) in the number of pairs.
 *   - Amortized O(1) append; O(n) remove at an arbitrary index; O(1) indexed access.
 *   - add is O(1) in the value: it moves a struct, it does not copy characters. init_3 is
 *     O(total bytes), because it does.
 *
 * Duplicate Keys:
 *   - add does NOT check for an existing key. Every lookup answers the FIRST match, and
 *     remove deletes the FIRST, so a duplicate leaves a shadowed pair behind.
 *   - TO CHANGE A VALUE, IF IT IS AN OWNER: mutate the String through an at_* result - it
 *     is the stored String, not a copy, and String's own writers handle growth. IF IT IS A
 *     VIEW, do not: see "Memory Management" above. remove_1 then add works for either.
 *
 * Family:
 *   One of the nine string-keyed maps of container/map - two parallel AL_* lists scanned
 *   linearly, one 34-function API in three value shapes; tools/map_divergence.py proves the
 *   generated scalars match their anchor and each canonical header matches its own stated
 *   counts. This is the other STRUCT-BY-MOVE shape: add takes String** and the source String
 *   is VACATED once both halves land, with `owned` traveling into the stored value. Suite:
 *   tests/container/map/test_map_char_string.c.
 *
 * Dependencies:
 *   - <container/arrayList/al_char.h> - the key half.
 *   - <container/arrayList/al_string.h> - the value half, whose elements carry both their
 *     own allocator and their own `owned` flag, which is what makes this instantiation's
 *     ownership rule self-describing rather than documented.
 *     Between them they transitively supply char/ (char_length, char_copy_3,
 *     char_compare_equal_2), allocator/ (allocator_try_borrow, allocator_release),
 *     memory/ (memory_empty) and, under ARENA_IMPLEMENTATION, arena/.
 *
 * See map_char_string.c for implementation details.
 */
#ifndef CONTAINER_MAP_CHAR_STRING_H
#define CONTAINER_MAP_CHAR_STRING_H

#include <container/arrayList/al_char.h>
#include <container/arrayList/al_string.h>

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief Map from string keys to String values.
 *
 * Two parallel lists at matching indices. There is deliberately no separate count: the
 * size is DERIVED from the lists, so it cannot disagree with them.
 */
typedef struct {
    AL_Char key;     /**< Keys, at matching indices with `value`. */
    AL_String value; /**< Values, at matching indices with `key`. */
} Map_Char_String;

/*==============================================================================
 * MARK: - API
 *============================================================================*/

/**
 * @brief Add a key-value pair, adopting the key and MOVING the value.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key string. Must not be nullptr, and must be NUL-terminated. OWNERSHIP
 *        TRANSFERS on success, so it must have been borrowed from the same allocator the
 *        map's KEY LIST uses - that is the one that will release it, and what a mismatch
 *        costs depends on WHICH list you have. Against an ARENA key list a foreign pointer
 *        is ignored or refused, so it leaks. Against a HEAP key list - anything built with
 *        init_* / new_*, whose allocator is nullptr - the release reaches bare free(), so an
 *        arena-borrowed key is a free() of an interior pointer: real corruption, and it
 *        surfaces at uninit rather than at the add. Never mix.
 * @param value Address of the source String pointer. Neither it nor *value may be nullptr.
 *        On success the source String is EMPTIED and *value is set to nullptr, so a
 *        retained alias cannot double-free. `owned` travels with the value, so moving a
 *        view yields a view the map will not release.
 *
 *        IF YOUR SOURCE STRUCT CAME FROM string_new_* / string_alloc_new_*, pass the address
 *        of a COPY of your pointer. This nulls the pointer you pass, and the struct itself is
 *        still yours to string_delete - null it here and it is unreachable. The example above
 *        uses a stack String, where the question does not arise.
 * @return true when the pair was stored; false when the allocator declined.
 * @note On false NOTHING was taken - the key is still the caller's, the source String still
 *       holds its buffer, and *value is unchanged.
 * @note Does not check for an existing key; see "Duplicate Keys" above.
 */
bool map_char_string_add(Map_Char_String *const self, char *const key, String **const value);

/**
 * @brief Add a key-value pair by COPYING the key and MOVING the value.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key string. Must not be nullptr, and must be NUL-terminated. Copied; the
 *        caller keeps the original.
 * @param value Address of the source String pointer; moved exactly as in add. "Static"
 *        names what happens to the KEY - the value is never copied by this call.
 * @return true when the pair was stored; false when the copy or the allocator declined.
 */
bool map_char_string_add_static(Map_Char_String *const self, char const *const key, String **const value);

/**
 * @brief Add a key-value pair by COPYING a key of explicit size and MOVING the value.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key bytes. Must not be nullptr; NEED NOT be NUL-terminated.
 * @param key_size Key length in bytes. 0 is a legal key. MUST NOT exceed the readable
 *        bytes at `key` - the size reaches a copy as a read length, so an over-large one
 *        is a heap over-read rather than a decline. Same contract as at_2.
 * @param value Address of the source String pointer; moved exactly as in add.
 * @return true when the pair was stored; false when the copy or the allocator declined.
 * @note The sized form is the PRIMITIVE - add_static measures and forwards here. The _2
 *       suffix encodes the KEY argument and nothing else.
 */
bool map_char_string_add_static_2(Map_Char_String *const self, char const *const key, USize const key_size, String **const value);

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Initialize an empty map with an arena allocator.
 * @param allocator Arena pointer. Must not be nullptr.
 * @return Initialized Map_Char_String.
 */
Map_Char_String map_char_string_alloc_init_1(Arena *allocator);

/**
 * @brief Initialize a map with a starting capacity and an arena allocator.
 * @param capacity Initial capacity for both lists. Must not be 0.
 * @param allocator Arena pointer. Must not be nullptr.
 * @return Initialized Map_Char_String.
 * @note A zero capacity ABORTS. Use alloc_init_1 for empty.
 */
Map_Char_String map_char_string_alloc_init_2(USize const capacity, Arena *allocator);

/**
 * @brief Initialize a map by DEEP-COPYING key and value lists, with an arena allocator.
 * @param keys Source keys. Must not be nullptr.
 * @param values Source values. Must not be nullptr.
 * @param allocator Arena pointer. Must not be nullptr.
 * @return Initialized Map_Char_String.
 * @note DEEP-COPIES both halves, so the caller keeps its sources entire and releases them
 *       normally. A copied VIEW becomes an OWNER - see "Memory Management".
 * @note ALL OR NOTHING for a REFUSED allocator: the map comes back empty rather than
 *       partial. A live-but-EXHAUSTED arena is different - the backing arrays (through
 *       al_char_alloc_init_2 / al_string_alloc_init_2) and the value copy (through
 *       string_alloc_init_2) all borrow on the aborting path, so this constructor can end
 *       the process where add would merely return false. Do not hand init_3 request-derived
 *       lists on a sized arena; build the map with add instead.
 * @note Pairs beyond the shorter of the two lists are not represented.
 */
Map_Char_String map_char_string_alloc_init_3(AL_Char const *const keys, AL_String const *const values, Arena *allocator);

/**
 * @brief Allocate and initialize an empty map with an arena allocator.
 * @param allocator Arena pointer. Must not be nullptr.
 * @return New map, or nullptr when the allocator declined. Free with map_char_string_delete.
 * @note A nullptr must NOT be handed to map_char_string_delete, which treats a null *self as
 *       a contract violation and aborts. Check before deleting.
 */
Map_Char_String* map_char_string_alloc_new_1(Arena *allocator);

/**
 * @brief Allocate and initialize a map with a capacity and an arena allocator.
 * @param capacity Initial capacity for both lists. Must not be 0.
 * @param allocator Arena pointer. Must not be nullptr.
 * @return New map, or nullptr when the allocator declined. Free with map_char_string_delete.
 * @note A nullptr must NOT be handed to map_char_string_delete, which treats a null *self as
 *       a contract violation and aborts. Check before deleting.
 */
Map_Char_String* map_char_string_alloc_new_2(USize const capacity, Arena *allocator);

/**
 * @brief Allocate a map by DEEP-COPYING key and value lists, with an arena allocator.
 * @param keys Source keys. Must not be nullptr.
 * @param values Source values. Must not be nullptr.
 * @param allocator Arena pointer. Must not be nullptr.
 * @return New map, or nullptr when the allocator declined. Free with map_char_string_delete.
 * @note A nullptr must NOT be handed to map_char_string_delete, which treats a null *self as
 *       a contract violation and aborts. Check before deleting.
 */
Map_Char_String* map_char_string_alloc_new_3(AL_Char const *const keys, AL_String const *const values, Arena *allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Look up a value by a null-terminated key.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key string. Must not be nullptr; may be empty.
 * @return The stored String at the first matching key, or nullptr when the key is absent.
 * @note THE RETURNED POINTER POINTS INTO THE VALUE ARRAY, dead after growth, remove,
 *       remove_at, clear or uninit. Mutating through it is safe only on an OWNING value -
 *       check `owned` if unsure, and see "Memory Management" above for the exact
 *       write-safety rule on a view.
 * @note FAMILY FORK: at_* answers the ADDRESS OF THE SLOT here, and on u64 and al_char.
 *       map_char_char_at_1 alone answers the stored value itself, so its change-a-value
 *       idiom is remove_1 then add rather than a mutation through this pointer. One
 *       fork, two idioms - see map_char_char_at_1.
 */
String* map_char_string_at_1(Map_Char_String const *const self, char const *const key);

/**
 * @brief Look up a value by a key of explicit size.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key bytes. Must not be nullptr; may be empty.
 * @param key_size Key length in bytes. 0 is a legal key, not an error. MUST NOT exceed the
 *        readable bytes at `key`: it is forwarded to char_compare_equal_2, which reads that
 *        many bytes whenever a stored key has the same length, so an over-large size is a
 *        heap over-read rather than a miss. contains_2 and remove_2 share the path and the
 *        contract.
 * @return The stored String at the first matching key, or nullptr when absent.
 */
String* map_char_string_at_2(Map_Char_String const *const self, char const *const key, USize const key_size);

/**
 * @brief Remove every pair, releasing each key and each owning value.
 * @param self Map pointer. Must not be nullptr.
 * @note Keeps the allocated capacity; see map_char_string_shrink to release it.
 */
void map_char_string_clear(Map_Char_String *const self);

/**
 * @brief Report whether a null-terminated key is present.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key string. Must not be nullptr; may be empty.
 * @return true when the key is present, whatever its value.
 * @note Tells a present-but-EMPTY String apart from an absent key, which at_1 cannot: an
 *       empty String is a legal value.
 */
bool map_char_string_contains_1(Map_Char_String const *const self, char const *const key);

/**
 * @brief Report whether a key of explicit size is present.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key bytes. Must not be nullptr; may be empty.
 * @param key_size Key length in bytes. 0 is a legal key, not an error.
 * @return true when the key is present, whatever its value.
 * @note Same read-extent contract as at_2.
 */
bool map_char_string_contains_2(Map_Char_String const *const self, char const *const key, USize const key_size);

/**
 * @brief Release a map allocated by map_char_string_new_* or _alloc_new_*.
 * @param self Address of the map pointer. Must not be nullptr, and neither may *self.
 * @note Releases every key and every owning value, then the map itself through the key
 *       list's allocator. Nulls the caller's pointer only under
 *       MEMORY_NON_DANGLING_POINTER.
 */
void map_char_string_delete(Map_Char_String **const self);

/**
 * @brief Report whether the map holds no pairs.
 * @param self Map pointer. Must not be nullptr.
 * @return true when there are no pairs.
 */
bool map_char_string_empty(Map_Char_String const *const self);

/**
 * @brief Report the capacity available for pairs.
 * @param self Map pointer. Must not be nullptr.
 * @return The smaller of the two lists' capacities, since a pair needs a slot in both.
 */
USize map_char_string_get_capacity(Map_Char_String const *const self);

/**
 * @brief Get the key at an index.
 * @param self Map pointer. Must not be nullptr.
 * @param index Index. Must be below map_char_string_get_size.
 * @return The key, which may be nullptr if one was stored.
 */
char* map_char_string_get_key(Map_Char_String const *const self, USize const index);

/**
 * @brief Get the keys list.
 * @param self Map pointer. Must not be nullptr.
 * @return The keys list. The ESCAPE HATCH, not the iteration API - see get_key/get_value.
 */
AL_Char* map_char_string_get_keys(Map_Char_String *const self);

/**
 * @brief Report the number of pairs.
 * @param self Map pointer. Must not be nullptr.
 * @return The smaller of the two lists' sizes. DERIVED rather than stored.
 */
USize map_char_string_get_size(Map_Char_String const *const self);

/**
 * @brief Get the value at an index.
 * @param self Map pointer. Must not be nullptr.
 * @param index Index. Must be below map_char_string_get_size.
 * @return The stored String. Same invalidation rule as at_1, and the same read-only
 *         caveat if the stored value is a view.
 */
String* map_char_string_get_value(Map_Char_String const *const self, USize const index);

/**
 * @brief Get the values list.
 * @param self Map pointer. Must not be nullptr.
 * @return The values list. The ESCAPE HATCH; see get_keys.
 */
AL_String* map_char_string_get_values(Map_Char_String *const self);

/**
 * @brief Initialize an empty map (heap).
 * @return Initialized Map_Char_String.
 */
Map_Char_String map_char_string_init_1(void);

/**
 * @brief Initialize a map with a starting capacity (heap).
 * @param capacity Initial capacity for both lists. Must not be 0.
 * @return Initialized Map_Char_String.
 * @note A zero capacity ABORTS. Use init_1 for empty.
 */
Map_Char_String map_char_string_init_2(USize const capacity);

/**
 * @brief Initialize a map by DEEP-COPYING key and value lists (heap).
 * @param keys Source keys. Must not be nullptr.
 * @param values Source values. Must not be nullptr.
 * @return Initialized Map_Char_String.
 * @note DEEP-COPIES both halves. A copied VIEW becomes an OWNER - see "Memory Management".
 * @note ALL OR NOTHING for a REFUSED allocator: the map comes back empty rather than
 *       partial. A failing HEAP is different - the value copy borrows through string_init_2,
 *       which aborts rather than declining. See alloc_init_3.
 * @note Pairs beyond the shorter of the two lists are not represented.
 * @note init_3(&other.key, &other.value) IS THE COPY CONSTRUCTOR for an existing map:
 *       deep-copies both lists in one call, no separate init_4 needed.
 */
Map_Char_String map_char_string_init_3(AL_Char const *const keys, AL_String const *const values);

/**
 * @brief Allocate and initialize an empty map (heap).
 * @return New map, or nullptr when the allocator declined. Free with map_char_string_delete.
 * @note A nullptr must NOT be handed to map_char_string_delete, which treats a null *self as
 *       a contract violation and aborts. Check before deleting.
 */
Map_Char_String* map_char_string_new_1(void);

/**
 * @brief Allocate and initialize a map with a capacity (heap).
 * @param capacity Initial capacity for both lists. Must not be 0.
 * @return New map, or nullptr when the allocator declined. Free with map_char_string_delete.
 * @note A nullptr must NOT be handed to map_char_string_delete, which treats a null *self as
 *       a contract violation and aborts. Check before deleting.
 */
Map_Char_String* map_char_string_new_2(USize const capacity);

/**
 * @brief Allocate a map by DEEP-COPYING key and value lists (heap).
 * @param keys Source keys. Must not be nullptr.
 * @param values Source values. Must not be nullptr.
 * @return New map, or nullptr when the allocator declined. Free with map_char_string_delete.
 * @note A nullptr must NOT be handed to map_char_string_delete, which treats a null *self as
 *       a contract violation and aborts. Check before deleting.
 */
Map_Char_String* map_char_string_new_3(AL_Char const *const keys, AL_String const *const values);

/**
 * @brief Remove the first pair with a null-terminated key.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key string. Must not be nullptr; may be empty.
 * @return true when a pair was removed.
 * @note Releases the stored key and uninits the stored value - which releases it only if
 *       it was an owner. Removes only the FIRST match.
 */
bool map_char_string_remove_1(Map_Char_String *const self, char const *const key);

/**
 * @brief Remove the first pair with a key of explicit size.
 * @param self Map pointer. Must not be nullptr.
 * @param key Key bytes. Must not be nullptr; may be empty.
 * @param key_size Key length in bytes. 0 is a legal key, not an error.
 * @return true when a pair was removed.
 * @note Same read-extent contract as at_2.
 */
bool map_char_string_remove_2(Map_Char_String *const self, char const *const key, USize const key_size);

/**
 * @brief Remove the pair at an index.
 * @param self Map pointer. Must not be nullptr.
 * @param index Index. Must be below map_char_string_get_size.
 * @note The indexed counterpart to get_key/get_value, and the only correct way to delete
 *       something found by iterating - a keyed remove takes the FIRST match, which is a
 *       different pair whenever the key is duplicated.
 */
void map_char_string_remove_at(Map_Char_String *const self, USize const index);

/**
 * @brief Reserve capacity in both lists.
 * @param self Map pointer. Must not be nullptr.
 * @param capacity Minimum capacity. Must not be 0.
 * @note A zero capacity ABORTS.
 */
void map_char_string_reserve(Map_Char_String *const self, USize const capacity);

/**
 * @brief Shrink both lists to their sizes.
 * @param self Map pointer. Must not be nullptr.
 * @note ON AN ARENA THIS DOES NOT FREE ANYTHING, and costs memory: arena_linear_free is a
 *       no-op, so the shrink bump-allocates smaller arrays and abandons the old ones,
 *       moving the arena TOWARD exhaustion. Those borrows can also abort.
 */
void map_char_string_shrink(Map_Char_String *const self);

/**
 * @brief Release all map storage and reset to the empty state.
 * @param self Map pointer. Must not be nullptr.
 * @note Releases every key, and uninits every stored value - which releases an owner and
 *       no-ops a view, through the STRING's own allocator rather than the map's. Safe to
 *       call twice.
 */
void map_char_string_uninit(Map_Char_String *const self);

#endif // CONTAINER_MAP_CHAR_STRING_H