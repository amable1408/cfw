/*
 * al_bool.h - Dynamic array list of bool for the C Libraries Framework
 *
 * Features:
 *   - Dynamic, growable array of bool values, bit-packed
 *   - Arena or heap allocation support
 *   - Add, remove, access, and manage bool elements efficiently
 *
 * Family:
 *   One of the hand-cloned typed array lists of container/arrayList; al_u64 is
 *   the canonical instantiation and tools/al_divergence.py measures each
 *   file's divergence from it. This instantiation is a deliberate bit-packed
 *   rewrite: there is no element array, add/at/clear are word/bit arithmetic,
 *   there is no get_data, and a refused arena or heap allocation degrades
 *   capacity to 64 (the inline word) rather than to 0.
 *
 * Usage Examples:
 *   @code
 *   AL_Bool list = al_bool_init_1();
 *   al_bool_add_last(&list, true);
 *   bool value = al_bool_at(&list, 0);
 *   al_bool_uninit(&list);
 *   @endcode
 *
 * Error Handling:
 *   Contract violations - a null self, an index past the size - go through
 *   error_check_*, which LOGS AND ABORTS the process. It does not return early:
 *   these are programming errors, not runtime conditions, and the old wording
 *   here promised a recovery that has never existed.
 *
 *   Conditions that depend on a VALUE rather than on a broken contract are
 *   refused instead, and never abort:
 *     - an allocator that declines (a refused arena, or a capacity whose byte
 *       size would wrap) leaves the list unchanged, and add() declines with it;
 *     - back() and front() answer false on an empty list (they return a value,
 *       so they cannot signal with null - check al_bool_empty to tell them apart);
 *     - a size of zero is a legal state throughout, never an error;
 *     - remove_first/remove_last are silent no-ops on an empty list (size
 *       stays 0).
 *
 * Thread Safety:
 *   Not thread-safe. Caller must synchronize if used from multiple threads.
 *
 * Memory Management:
 *   - The list owns only its bit buffer. A capacity of up to one 64-bit word is stored
 *     inline with no allocation at all; beyond that al_bool_uninit releases the word array.
 *     Arena-backed lists are freed by releasing the arena.
 *
 * A real hazard, corrected here:
 *   get_size and get_capacity return USize BY VALUE, like the rest of the family.
 *   They used to hand out a writable USize* into the struct - a second unchecked
 *   set_size by another name, and worse for capacity, which is the union
 *   discriminator: raising it through the handle made the inline bit pattern be
 *   reinterpreted as a pointer and dereferenced.
 *
 * Deliberate API differences from the rest of the al_* family:
 *   - There is no get_data and no by-reference accessor. An element here is a BIT
 *     inside a word, not an addressable object, so there is no `bool*` to hand
 *     back and no array a caller could walk. at/back/front therefore return the
 *     value rather than a pointer, and back/front report an empty list as false
 *     - check al_bool_empty first to tell that apart from a stored false.
 *   - init_3 copies from a `bool*` array, but the list never exposes one again.
 *
 * Performance Characteristics:
 *   - Amortized O(1) append; O(n) insert/remove at an arbitrary index; O(1) bit access.
 *   - One bit of storage per element, packed into USize words.
 *
 * Dependencies:
 *   - <allocator/allocator.h>
 *   - <bits/bits.h>
 *
 * See al_bool.c for implementation details.
 */

#ifndef CONTAINER_ARRAYLIST_BOOL_H
#define CONTAINER_ARRAYLIST_BOOL_H

#include <allocator/allocator.h>
#include <bits/bits.h>

/**
 * @brief Dynamic array list of bool values (bits packed 64 per word).
 *
 * @note Elements are packed bits, not addressable bytes, so there is no flat
 *       element-array accessor (unlike the other al_* containers). Read elements with
 *       al_bool_at and query length with al_bool_get_size / al_bool_get_capacity.
 */
typedef struct {
#ifdef ARENA_IMPLEMENTATION
    Arena   *allocator; /**< Arena allocator pointer (if used) */
#endif // ARENA_IMPLEMENTATION
    USize   capacity;   /**< Allocated capacity */
    union {
        USize *dyn;
        USize raw;
    } data;
    USize   size;       /**< Number of elements */
} AL_Bool;

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Initialize an empty array list with arena allocator.
 * @param allocator Arena pointer.
 * @return Initialized AL_Bool.
 */
AL_Bool al_bool_alloc_init_1(Arena *allocator);

/**
 * @brief Initialize array list with capacity and arena allocator.
 * @param capacity Initial capacity.
 * @param allocator Arena pointer.
 * @return Initialized AL_Bool.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 * @note A refused arena leaves the list at the inline word rather than at 0: a
 *         capacity up to one 64-bit word never needs a borrow, so the list
 *         degrades to "no allocation, but still usable up to one 64-bit word"
 *         instead of empty.
 */
AL_Bool al_bool_alloc_init_2(USize const capacity, Arena *allocator);

/**
 * @brief Initialize array list from data and size with arena allocator.
 * @param data Array of bool values.
 * @param data_size Number of elements.
 * @param allocator Arena pointer.
 * @return Initialized AL_Bool.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 *         A data_size of zero ABORTS rather than building an empty list: this
 *         argument is the capacity, and a zero capacity is a caller contract
 *         under the family's empty-value policy (an empty VALUE is legal
 *         everywhere; a zero capacity or allocation size is not). Construct with
 *         init_1 when you want an empty list.
 * @note A declined allocator TRUNCATES the copy to the capacity that survived
 *         rather than leaving the list unchanged: the module banner's blanket does
 *         not hold here. Compare al_bool_get_size against data_size if the caller
 *         needs to know - a loop to data_size would abort in at() on the size bound.
 */
AL_Bool al_bool_alloc_init_3(bool const *const data, USize const data_size, Arena *allocator);

/**
 * @brief Allocate a new AL_Bool on the arena.
 * @param allocator Arena pointer.
 * @return Pointer to new AL_Bool.
 */
AL_Bool* al_bool_alloc_new_1(Arena *allocator);

/**
 * @brief Allocate a new AL_Bool with capacity on the arena.
 * @param capacity Initial capacity.
 * @param allocator Arena pointer.
 * @return Pointer to new AL_Bool.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
AL_Bool* al_bool_alloc_new_2(USize const capacity, Arena *allocator);

/**
 * @brief Allocate a new AL_Bool from data and size on the arena.
 * @param data Array of bool values.
 * @param data_size Number of elements.
 * @param allocator Arena pointer.
 * @return Pointer to new AL_Bool.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 *         A data_size of zero ABORTS rather than building an empty list: this
 *         argument is the capacity, and a zero capacity is a caller contract
 *         under the family's empty-value policy (an empty VALUE is legal
 *         everywhere; a zero capacity or allocation size is not). Construct with
 *         init_1 when you want an empty list.
 * @note A declined allocator TRUNCATES the copy to the capacity that survived
 *         rather than leaving the list unchanged: the module banner's blanket does
 *         not hold here. Compare al_bool_get_size against data_size if the caller
 *         needs to know - a loop to data_size would abort in at() on the size bound.
 */
AL_Bool* al_bool_alloc_new_3(bool const *const data, USize const data_size, Arena *allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Add a value at the specified index.
 * @param self Pointer to AL_Bool.
 * @param data Value to add.
 * @param index Index to insert at.
 * @note Declines silently when the allocator refuses the growth (a refused
 *         arena, or a capacity whose byte size would wrap). The list is left
 *         unchanged, so a caller that must know checks the size afterwards.
 * @note The element is taken BY VALUE into a slot this list owns; where the
 *         element type owns memory of its own, that ownership transfers here and
 *         is released by remove/clear/uninit.
 */
void al_bool_add(AL_Bool *const self, bool const data, USize const index);

/**
 * @brief Add a value at the beginning.
 * @param self Pointer to AL_Bool.
 * @param data Value to add.
 * @note Declines silently when the allocator refuses the growth (a refused
 *         arena, or a capacity whose byte size would wrap). The list is left
 *         unchanged, so a caller that must know checks the size afterwards.
 * @note The element is taken BY VALUE into a slot this list owns; where the
 *         element type owns memory of its own, that ownership transfers here and
 *         is released by remove/clear/uninit.
 */
void al_bool_add_first(AL_Bool *const self, bool const data);

/**
 * @brief Add a value at the end.
 * @param self Pointer to AL_Bool.
 * @param data Value to add.
 * @note Declines silently when the allocator refuses the growth (a refused
 *         arena, or a capacity whose byte size would wrap). The list is left
 *         unchanged, so a caller that must know checks the size afterwards.
 * @note The element is taken BY VALUE into a slot this list owns; where the
 *         element type owns memory of its own, that ownership transfers here and
 *         is released by remove/clear/uninit.
 */
void al_bool_add_last(AL_Bool *const self, bool const data);

/**
 * @brief Get the value at the specified index.
 * @param self Pointer to AL_Bool.
 * @param index Index to access.
 * @return value at index.
 * @note Bounded by size AND by capacity. The size bound is the contract - the
 *         slots a clear() leaves inside the retained capacity are out of contract,
 *         not a way to inspect a released element. The capacity bound is a second
 *         line kept only in this file and al_u8: set_size CLAMPS now, so size
 *         never exceeds capacity and this check is redundant - it is kept as the
 *         cheapest tripwire if that clamp ever regresses, and it compiles out
 *         without ERROR_CHECK_ENABLED, so the clamp is the protection and this is
 *         only the messenger. See the set_size note.
 */
bool al_bool_at(AL_Bool const *const self, USize const index);

/**
 * @brief Get the last value in the list.
 * @param self Pointer to AL_Bool.
 * @return The last stored value, or false when the list is empty.
 * @note Answers false on an empty list rather than aborting: emptiness is a
 *         data question, not a broken contract. This returns a VALUE and so
 *         cannot signal with null - callers that must tell an empty list from a
 *         stored false check al_bool_empty first.
 */
bool al_bool_back(AL_Bool const *const self);

/**
 * @brief Remove all elements from the list.
 * @param self Pointer to AL_Bool.
 */
void al_bool_clear(AL_Bool *const self);

/**
 * @brief Delete and free the list.
 * @param self Address of AL_Bool pointer.
 */
void al_bool_delete(AL_Bool **const self);

/**
 * @brief Check if the list is empty.
 * @param self Pointer to AL_Bool.
 * @return true if empty, false otherwise.
 */
bool al_bool_empty(AL_Bool const *const self);

/**
 * @brief Get the first value in the list.
 * @param self Pointer to AL_Bool.
 * @return The first stored value, or false when the list is empty.
 * @note Answers false on an empty list rather than aborting: emptiness is a
 *         data question, not a broken contract. This returns a VALUE and so
 *         cannot signal with null - callers that must tell an empty list from a
 *         stored false check al_bool_empty first.
 */
bool al_bool_front(AL_Bool const *const self);

/**
 * @brief Get the element capacity.
 * @param self Pointer to AL_Bool.
 * @return The number of elements the list can hold before it must grow.
 */
USize al_bool_get_capacity(AL_Bool const *const self);

/**
 * @brief Get the element count.
 * @param self Pointer to AL_Bool.
 * @return The number of elements currently stored.
 */
USize al_bool_get_size(AL_Bool const *const self);

/**
 * @brief Initialize an empty array list.
 * @return Initialized AL_Bool.
 */
AL_Bool al_bool_init_1(void);

/**
 * @brief Initialize array list with capacity.
 * @param capacity Initial capacity.
 * @return Initialized AL_Bool.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 * @note A refused/failed heap allocation leaves the list at the inline word
 *         rather than at 0: a capacity up to one 64-bit word never needs a
 *         borrow, so the list degrades to "no allocation, but still usable up
 *         to one 64-bit word" instead of empty.
 */
AL_Bool al_bool_init_2(USize const capacity);

/**
 * @brief Initialize array list from data and size.
 * @param data Array of bool values.
 * @param data_size Number of elements.
 * @return Initialized AL_Bool.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 *         A data_size of zero ABORTS rather than building an empty list: this
 *         argument is the capacity, and a zero capacity is a caller contract
 *         under the family's empty-value policy (an empty VALUE is legal
 *         everywhere; a zero capacity or allocation size is not). Construct with
 *         init_1 when you want an empty list.
 * @note A declined allocator TRUNCATES the copy to the capacity that survived
 *         rather than leaving the list unchanged: the module banner's blanket does
 *         not hold here. Compare al_bool_get_size against data_size if the caller
 *         needs to know - a loop to data_size would abort in at() on the size bound.
 */
AL_Bool al_bool_init_3(bool const *const data, USize const data_size);

/**
 * @brief Allocate a new AL_Bool on the heap.
 * @return Pointer to new AL_Bool.
 */
AL_Bool* al_bool_new_1(void);

/**
 * @brief Allocate a new AL_Bool with capacity on the heap.
 * @param capacity Initial capacity.
 * @return Pointer to new AL_Bool.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
AL_Bool* al_bool_new_2(USize const capacity);

/**
 * @brief Allocate a new AL_Bool from data and size on the heap.
 * @param data Array of bool values.
 * @param data_size Number of elements.
 * @return Pointer to new AL_Bool.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 *         A data_size of zero ABORTS rather than building an empty list: this
 *         argument is the capacity, and a zero capacity is a caller contract
 *         under the family's empty-value policy (an empty VALUE is legal
 *         everywhere; a zero capacity or allocation size is not). Construct with
 *         init_1 when you want an empty list.
 * @note A declined allocator TRUNCATES the copy to the capacity that survived
 *         rather than leaving the list unchanged: the module banner's blanket does
 *         not hold here. Compare al_bool_get_size against data_size if the caller
 *         needs to know - a loop to data_size would abort in at() on the size bound.
 */
AL_Bool* al_bool_new_3(bool const *const data, USize const data_size);

/**
 * @brief Remove the value at the specified index.
 * @param self Pointer to AL_Bool.
 * @param index Index to remove.
 */
void al_bool_remove(AL_Bool *const self, USize const index);

/**
 * @brief Remove the first value in the list.
 * @param self Pointer to AL_Bool.
 * @note A call on an empty list is a silent no-op (size stays 0).
 */
void al_bool_remove_first(AL_Bool *const self);

/**
 * @brief Remove the last value in the list.
 * @param self Pointer to AL_Bool.
 * @note A call on an empty list is a silent no-op (size stays 0).
 */
void al_bool_remove_last(AL_Bool *const self);

/**
 * @brief Reserve capacity for the list.
 * @param self Pointer to AL_Bool.
 * @param capacity New capacity.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
void al_bool_reserve(AL_Bool *const self, USize const capacity);

/**
 * @brief Set the element count directly.
 * @param self Pointer to AL_Bool.
 * @param size New element count.
 * @note CLAMPED to the capacity. It moves the size without touching the capacity or
 *         the elements, so raising it over slots not currently in use still exposes
 *         their contents - zeros in a fresh buffer, but whatever was last written in
 *         a reused one - and it can no longer state a size the
 *         storage cannot back, which is what let `init_2(4); set_size(1 << 26);
 *         add_last(x);` write 64 MiB past a 4-byte block through the public API.
 *         Reserve first if you need a larger size; a declined reserve then shows up
 *         as a clamped size rather than as a wild write.
 *         The clamp bounds this type's whole API, unlike al_u8's: no accessor hands
 *         out the storage (see the API-differences note above), so nothing can write
 *         the buffer THROUGH THIS API without a mutator that respects size. The
 *         struct is transparent, as everywhere in CFW, so a caller reaching into
 *         .data directly is outside that guarantee.
 */
void al_bool_set_size(AL_Bool *const self, USize const size);

/**
 * @brief Shrink the list to fit its size.
 * @param self Pointer to AL_Bool.
 */
void al_bool_shrink(AL_Bool *const self);

/**
 * @brief Release all memory and reset the list.
 * @param self Pointer to AL_Bool.
 * @note Idempotent in every build. The freed pointer is cleared unconditionally
 *         rather than under MEMORY_NON_DANGLING_POINTER, so a second uninit cannot
 *         hand a released block back to the allocator.
 */
void al_bool_uninit(AL_Bool *const self);

#endif // CONTAINER_ARRAYLIST_BOOL_H