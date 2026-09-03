/*
 * al_string.h - Dynamic array list of String values for the C Libraries Framework
 *
 * Features:
 *   - Dynamic, growable array of String values
 *   - Arena or heap allocation support
 *   - Add, remove, access, and manage String elements efficiently
 *
 * Family:
 *   One of the hand-cloned typed array lists of container/arrayList; al_u64 is
 *   the canonical instantiation and tools/al_divergence.py measures each
 *   file's divergence from it. This instantiation owns its String elements
 *   (copy on add, uninit on release); add takes a pointer and refuses an
 *   own-list element.
 *
 * Usage Examples:
 *   @code
 *   AL_String list = al_string_init_1();
 *   al_string_add_last(&list, &some_string);
 *   String *str = al_string_at(&list, 0);
 *   al_string_uninit(&list);
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
 *     - back() and front() answer nullptr on an empty list;
 *     - a stored nullptr element is a legal value, skipped on release.
 *
 * Thread Safety:
 *   Not thread-safe. Caller must synchronize if used from multiple threads.
 *
 * Memory Management:
 *   - The list owns its String elements. add stores a copy of the value, remove and uninit
 *     release the element's buffer before the slot is reused, and uninit also releases the
 *     backing array. Arena-backed lists are freed by releasing the arena.
 *
 * Performance Characteristics:
 *   - Amortized O(1) append; O(n) insert/remove at an arbitrary index; O(1) indexed access.
 *
 * Dependencies:
 *   - <container/string/string.h>
 *
 * See al_string.c for implementation details.
 */

#ifndef CONTAINER_ARRAYLIST_STRING_H
#define CONTAINER_ARRAYLIST_STRING_H

#include <container/string/string.h>

/**
 * @brief Dynamic array list of String values.
 */
typedef struct {
#ifdef ARENA_IMPLEMENTATION
    Arena   *allocator; /**< Arena allocator pointer (if used) */
#endif // ARENA_IMPLEMENTATION
    USize   capacity;   /**< Allocated capacity */
    String  *data;      /**< Array of String values */
    USize   size;       /**< Number of elements */
} AL_String;

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Initialize an empty array list with arena allocator.
 * @param allocator Arena pointer.
 * @return Initialized AL_String.
 */
AL_String al_string_alloc_init_1(Arena *allocator);

/**
 * @brief Initialize array list with capacity and arena allocator.
 * @param capacity Initial capacity.
 * @param allocator Arena pointer.
 * @return Initialized AL_String.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
AL_String al_string_alloc_init_2(USize const capacity, Arena *allocator);

/**
 * @brief Initialize array list from data and size with arena allocator.
 * @param data Array of String values.
 * @param data_size Number of elements.
 * @param allocator Arena pointer.
 * @return Initialized AL_String.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 *         A data_size of zero ABORTS rather than building an empty list: this
 *         argument is the capacity, and a zero capacity is a caller contract
 *         under the family's empty-value policy (an empty VALUE is legal
 *         everywhere; a zero capacity or allocation size is not). Construct with
 *         init_1 when you want an empty list.
 */
AL_String al_string_alloc_init_3(String *const data, USize const data_size, Arena *allocator);

/**
 * @brief Allocate a new AL_String on the arena.
 * @param allocator Arena pointer.
 * @return Pointer to new AL_String.
 */
AL_String* al_string_alloc_new_1(Arena *allocator);

/**
 * @brief Allocate a new AL_String with capacity on the arena.
 * @param capacity Initial capacity.
 * @param allocator Arena pointer.
 * @return Pointer to new AL_String.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
AL_String* al_string_alloc_new_2(USize const capacity, Arena *allocator);

/**
 * @brief Allocate a new AL_String from data and size on the arena.
 * @param data Array of String values.
 * @param data_size Number of elements.
 * @param allocator Arena pointer.
 * @return Pointer to new AL_String.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 *         A data_size of zero ABORTS rather than building an empty list: this
 *         argument is the capacity, and a zero capacity is a caller contract
 *         under the family's empty-value policy (an empty VALUE is legal
 *         everywhere; a zero capacity or allocation size is not). Construct with
 *         init_1 when you want an empty list.
 */
AL_String* al_string_alloc_new_3(String *const data, USize const data_size, Arena *allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Add a String value (copied into a slot the list owns) at the specified index.
 * @param self Pointer to AL_String.
 * @param data Pointer to the String to copy.
 * @param index Index to insert at.
 * @note Declines silently when the allocator refuses the growth (a refused
 *         arena, or a capacity whose byte size would wrap). The list is left
 *         unchanged, so a caller that must know checks the size afterwards.
 * @note The element is taken BY VALUE into a slot this list owns; where the
 *         element type owns memory of its own, that ownership transfers here and
 *         is released by remove/clear/uninit.
 * @note `data` is a POINTER here, not a value: passing nullptr is legal and
 *         stores a default-initialized element rather than refusing. That is how
 *         a caller reserves a slot to fill in later; it is not an error path, and
 *         it is why remove/clear skip an empty slot instead of releasing it.
 * @note A `data` that is one of self's OWN elements (a pointer into this list's storage) is
 *         REFUSED as a no-op, in every build: the element would be owned twice and released
 *         twice, and the growth may move it before it is read. Copy it out first if a
 *         duplicate is really wanted - a shallow copy still shares owned members.
 */
void al_string_add(AL_String *const self, String *const data, USize const index);

/**
 * @brief Add a String value (copied into a slot the list owns) at the beginning.
 * @param self Pointer to AL_String.
 * @param data Pointer to the String to copy.
 * @note Declines silently when the allocator refuses the growth (a refused
 *         arena, or a capacity whose byte size would wrap). The list is left
 *         unchanged, so a caller that must know checks the size afterwards.
 * @note The element is taken BY VALUE into a slot this list owns; where the
 *         element type owns memory of its own, that ownership transfers here and
 *         is released by remove/clear/uninit.
 * @note `data` is a POINTER here, not a value: passing nullptr is legal and
 *         stores a default-initialized element rather than refusing. That is how
 *         a caller reserves a slot to fill in later; it is not an error path, and
 *         it is why remove/clear skip an empty slot instead of releasing it.
 * @note A `data` that is one of self's OWN elements (a pointer into this list's storage) is
 *         REFUSED as a no-op, in every build: the element would be owned twice and released
 *         twice, and the growth may move it before it is read. Copy it out first if a
 *         duplicate is really wanted - a shallow copy still shares owned members.
 */
void al_string_add_first(AL_String *const self, String *const data);

/**
 * @brief Add a String value (copied into a slot the list owns) at the end.
 * @param self Pointer to AL_String.
 * @param data Pointer to the String to copy.
 * @note Declines silently when the allocator refuses the growth (a refused
 *         arena, or a capacity whose byte size would wrap). The list is left
 *         unchanged, so a caller that must know checks the size afterwards.
 * @note The element is taken BY VALUE into a slot this list owns; where the
 *         element type owns memory of its own, that ownership transfers here and
 *         is released by remove/clear/uninit.
 * @note `data` is a POINTER here, not a value: passing nullptr is legal and
 *         stores a default-initialized element rather than refusing. That is how
 *         a caller reserves a slot to fill in later; it is not an error path, and
 *         it is why remove/clear skip an empty slot instead of releasing it.
 * @note A `data` that is one of self's OWN elements (a pointer into this list's storage) is
 *         REFUSED as a no-op, in every build: the element would be owned twice and released
 *         twice, and the growth may move it before it is read. Copy it out first if a
 *         duplicate is really wanted - a shallow copy still shares owned members.
 */
void al_string_add_last(AL_String *const self, String *const data);

/**
 * @brief Get the address of the String at the specified index.
 * @param self Pointer to AL_String.
 * @param index Index to access.
 * @return Address of the String at index.
 * @note Bounded by size, not capacity. The slots a clear() leaves inside the
 *         retained capacity are out of contract - reading one is a caller error,
 *         not a way to inspect a released element.
 * @note The address is valid only until the next add/add_first/add_last/reserve/shrink: any growth may move the backing array and invalidate it.
 */
String* al_string_at(AL_String const *const self, USize const index);

/**
 * @brief Get the address of the last String in the list.
 * @param self Pointer to AL_String.
 * @return Address of the last String.
 * @note Answers nullptr on an empty list rather than aborting: emptiness is a
 *         data question, not a broken contract.
 * @note The address is valid only until the next add/add_first/add_last/reserve/shrink: any growth may move the backing array and invalidate it.
 */
String* al_string_back(AL_String const *const self);

/**
 * @brief Remove all elements from the list.
 * @param self Pointer to AL_String.
 */
void al_string_clear(AL_String *const self);

/**
 * @brief Delete and free the list.
 * @param self Address of AL_String pointer.
 */
void al_string_delete(AL_String **const self);

/**
 * @brief Check if the list is empty.
 * @param self Pointer to AL_String.
 * @return true if empty, false otherwise.
 */
bool al_string_empty(AL_String const *const self);

/**
 * @brief Get the address of the first String in the list.
 * @param self Pointer to AL_String.
 * @return Address of the first String.
 * @note Answers nullptr on an empty list rather than aborting: emptiness is a
 *         data question, not a broken contract.
 * @note The address is valid only until the next add/add_first/add_last/reserve/shrink: any growth may move the backing array and invalidate it.
 */
String* al_string_front(AL_String const *const self);

/**
 * @brief Get the element capacity.
 * @param self Pointer to AL_String.
 * @return The number of elements the list can hold before it must grow.
 */
USize al_string_get_capacity(AL_String const *const self);

/**
 * @brief Get the data array pointer.
 * @param self Pointer to AL_String.
 * @return Pointer to data array.
 */
String* al_string_get_data(AL_String const *const self);

/**
 * @brief Get the element count.
 * @param self Pointer to AL_String.
 * @return The number of elements currently stored.
 */
USize al_string_get_size(AL_String const *const self);

/**
 * @brief Initialize an empty array list.
 * @return Initialized AL_String.
 */
AL_String al_string_init_1(void);

/**
 * @brief Initialize array list with capacity.
 * @param capacity Initial capacity.
 * @return Initialized AL_String.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
AL_String al_string_init_2(USize const capacity);

/**
 * @brief Initialize array list from data and size.
 * @param data Array of String values.
 * @param data_size Number of elements.
 * @return Initialized AL_String.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 *         A data_size of zero ABORTS rather than building an empty list: this
 *         argument is the capacity, and a zero capacity is a caller contract
 *         under the family's empty-value policy (an empty VALUE is legal
 *         everywhere; a zero capacity or allocation size is not). Construct with
 *         init_1 when you want an empty list.
 */
AL_String al_string_init_3(String *const data, USize const data_size);

/**
 * @brief Allocate a new AL_String on the heap.
 * @return Pointer to new AL_String.
 */
AL_String* al_string_new_1(void);

/**
 * @brief Allocate a new AL_String with capacity on the heap.
 * @param capacity Initial capacity.
 * @return Pointer to new AL_String.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
AL_String* al_string_new_2(USize const capacity);

/**
 * @brief Allocate a new AL_String from data and size on the heap.
 * @param data Array of String values.
 * @param data_size Number of elements.
 * @return Pointer to new AL_String.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 *         A data_size of zero ABORTS rather than building an empty list: this
 *         argument is the capacity, and a zero capacity is a caller contract
 *         under the family's empty-value policy (an empty VALUE is legal
 *         everywhere; a zero capacity or allocation size is not). Construct with
 *         init_1 when you want an empty list.
 */
AL_String* al_string_new_3(String *const data, USize const data_size);

/**
 * @brief Remove the String value at the specified index.
 * @param self Pointer to AL_String.
 * @param index Index to remove.
 */
void al_string_remove(AL_String *const self, USize const index);

/**
 * @brief Remove the first String value in the list.
 * @param self Pointer to AL_String.
 * @note A call on an empty list is a silent no-op (size stays 0).
 */
void al_string_remove_first(AL_String *const self);

/**
 * @brief Remove the last String value in the list.
 * @param self Pointer to AL_String.
 * @note A call on an empty list is a silent no-op (size stays 0).
 */
void al_string_remove_last(AL_String *const self);

/**
 * @brief Reserve capacity for the list.
 * @param self Pointer to AL_String.
 * @param capacity New capacity.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
void al_string_reserve(AL_String *const self, USize const capacity);

/**
 * @brief Shrink the list to fit its size.
 * @param self Pointer to AL_String.
 */
void al_string_shrink(AL_String *const self);

/**
 * @brief Release all memory and reset the list.
 * @param self Pointer to AL_String.
 * @note Idempotent in every build. The freed pointer is cleared unconditionally
 *         rather than under MEMORY_NON_DANGLING_POINTER, so a second uninit cannot
 *         hand a released block back to the allocator.
 */
void al_string_uninit(AL_String *const self);

#endif // CONTAINER_ARRAYLIST_STRING_H