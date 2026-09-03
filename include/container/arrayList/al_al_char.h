/*
 * al_al_char.h - Dynamic array list of AL_Char for the C Libraries Framework
 *
 * Features:
 *   - Dynamic, growable array of nested AL_Char lists
 *   - Arena or heap allocation support
 *   - Add, remove, access, and manage AL_Char elements efficiently
 *
 * Family:
 *   One of the hand-cloned typed array lists of container/arrayList; al_u64 is
 *   the canonical instantiation and tools/al_divergence.py measures each
 *   file's divergence from it. This instantiation holds nested AL_Char lists
 *   (recursive ownership); add takes a pointer and refuses an own-list
 *   element, and its at/back/front accessors each come in two forms: _1
 *   reaches into the nested list's raw char** buffer, _2 returns the nested
 *   AL_Char* list itself.
 *
 * Usage Examples:
 *   @code
 *   AL_AL_Char list = al_al_char_init_1();
 *   al_al_char_add_last(&list, &some_al_char);
 *   AL_Char *sublist = al_al_char_at_2(&list, 0);
 *   al_al_char_uninit(&list);
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
 *     - a null `data` argument to add/add_first/add_last is legal and inserts
 *       a default-initialized (empty) nested list rather than refusing.
 *
 * Thread Safety:
 *   Not thread-safe. Caller must synchronize if used from multiple threads.
 *
 * Memory Management:
 *   - The list owns its nested AL_Char elements. al_al_char_clear uninits each nested list,
 *     al_al_char_uninit uninits each one and then releases the backing array. Arena-backed
 *     lists are freed by releasing the arena.
 *
 * Performance Characteristics:
 *   - Amortized O(1) append; O(n) insert/remove at an arbitrary index; O(1) indexed access.
 *
 * Dependencies:
 *   - <container/arrayList/al_char.h>
 *
 * See al_al_char.c for implementation details.
 */

#ifndef CONTAINER_ARRAYLIST_AL_CHAR_H
#define CONTAINER_ARRAYLIST_AL_CHAR_H

#include <container/arrayList/al_char.h>

/**
 * @brief Dynamic array list of AL_Char (array list of string lists).
 */
typedef struct {
#ifdef ARENA_IMPLEMENTATION
    Arena   *allocator; /**< Arena allocator pointer (if used) */
#endif // ARENA_IMPLEMENTATION
    USize   capacity;   /**< Allocated capacity */
    AL_Char *data;      /**< Array of AL_Char elements */
    USize   size;       /**< Number of elements */
} AL_AL_Char;

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Initialize an empty array list with arena allocator.
 * @param allocator Arena pointer.
 * @return Initialized AL_AL_Char.
 */
AL_AL_Char al_al_char_alloc_init_1(Arena *allocator);

/**
 * @brief Initialize array list with capacity and arena allocator.
 * @param capacity Initial capacity.
 * @param allocator Arena pointer.
 * @return Initialized AL_AL_Char.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
AL_AL_Char al_al_char_alloc_init_2(USize const capacity, Arena *allocator);

/**
 * @brief Initialize array list from data and size with arena allocator.
 * @param data Array of AL_Char elements.
 * @param data_size Number of elements.
 * @param allocator Arena pointer.
 * @return Initialized AL_AL_Char.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 * @note COPIES the caller's array into storage of its own; it does not adopt it.
 *         The caller keeps ownership of `data` and must release it. The ELEMENTS
 *         are taken by value, so ownership of what they point to transfers to
 *         this list and is released by remove/clear/uninit.
 *         A data_size of zero ABORTS rather than building an empty list: this
 *         argument is the capacity, and a zero capacity is a caller contract
 *         under the family's empty-value policy (an empty VALUE is legal
 *         everywhere; a zero capacity or allocation size is not). Construct with
 *         init_1 when you want an empty list.
 */
AL_AL_Char al_al_char_alloc_init_3(AL_Char *const data, USize const data_size, Arena *allocator);

/**
 * @brief Allocate a new AL_AL_Char on the arena.
 * @param allocator Arena pointer.
 * @return Pointer to new AL_AL_Char.
 */
AL_AL_Char* al_al_char_alloc_new_1(Arena *allocator);

/**
 * @brief Allocate a new AL_AL_Char with capacity on the arena.
 * @param capacity Initial capacity.
 * @param allocator Arena pointer.
 * @return Pointer to new AL_AL_Char.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
AL_AL_Char* al_al_char_alloc_new_2(USize const capacity, Arena *allocator);

/**
 * @brief Allocate a new AL_AL_Char from data and size on the arena.
 * @param data Array of AL_Char elements.
 * @param data_size Number of elements.
 * @param allocator Arena pointer.
 * @return Pointer to new AL_AL_Char.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 * @note COPIES the caller's array into storage of its own; it does not adopt it.
 *         The caller keeps ownership of `data` and must release it. The ELEMENTS
 *         are taken by value, so ownership of what they point to transfers to
 *         this list and is released by remove/clear/uninit.
 *         A data_size of zero ABORTS rather than building an empty list: this
 *         argument is the capacity, and a zero capacity is a caller contract
 *         under the family's empty-value policy (an empty VALUE is legal
 *         everywhere; a zero capacity or allocation size is not). Construct with
 *         init_1 when you want an empty list.
 */
AL_AL_Char* al_al_char_alloc_new_3(AL_Char *const data, USize const data_size, Arena *allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Add an AL_Char at the specified index.
 * @param self Pointer to AL_AL_Char.
 * @param data Pointer to AL_Char.
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
void al_al_char_add(AL_AL_Char *const self, AL_Char *const data, USize const index);

/**
 * @brief Add an AL_Char at the beginning.
 * @param self Pointer to AL_AL_Char.
 * @param data Pointer to AL_Char.
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
void al_al_char_add_first(AL_AL_Char *const self, AL_Char *const data);

/**
 * @brief Add an AL_Char at the end.
 * @param self Pointer to AL_AL_Char.
 * @param data Pointer to AL_Char.
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
void al_al_char_add_last(AL_AL_Char *const self, AL_Char *const data);

/**
 * @brief Get the string array at the specified index.
 * @param self Pointer to AL_AL_Char.
 * @param index Index to access.
 * @return C string array pointer at index.
 * @note The address is the nested list's own buffer: that list's add/reserve/shrink invalidates it, the outer list's growth does not, and the outer list's remove/clear/uninit free it.
 */
char** al_al_char_at_1(AL_AL_Char const *const self, USize const index);

/**
 * @brief Get the AL_Char at the specified index.
 * @param self Pointer to AL_AL_Char.
 * @param index Index to access.
 * @return Pointer to AL_Char at index.
 * @note The address is valid only until the next add/add_first/add_last/reserve/shrink: any growth may move the backing array and invalidate it.
 */
AL_Char* al_al_char_at_2(AL_AL_Char const *const self, USize const index);

/**
 * @brief Get the last string array in the list.
 * @param self Pointer to AL_AL_Char.
 * @return C string array pointer to last element, or nullptr when the list is empty.
 * @note Answers nullptr on an empty list rather than aborting: emptiness is a
 *         data question, not a broken contract.
 * @note The address is the nested list's own buffer: that list's add/reserve/shrink invalidates it, the outer list's growth does not, and the outer list's remove/clear/uninit free it.
 */
char** al_al_char_back_1(AL_AL_Char const *const self);

/**
 * @brief Get the last AL_Char in the list.
 * @param self Pointer to AL_AL_Char.
 * @return Pointer to last AL_Char, or nullptr when the list is empty.
 * @note Answers nullptr on an empty list rather than aborting: emptiness is a
 *         data question, not a broken contract.
 * @note The address is valid only until the next add/add_first/add_last/reserve/shrink: any growth may move the backing array and invalidate it.
 */
AL_Char* al_al_char_back_2(AL_AL_Char const *const self);

/**
 * @brief Remove all elements from the list.
 * @param self Pointer to AL_AL_Char.
 */
void al_al_char_clear(AL_AL_Char *const self);

/**
 * @brief Delete and free the list.
 * @param self Address of AL_AL_Char pointer.
 */
void al_al_char_delete(AL_AL_Char **const self);

/**
 * @brief Check if the list is empty.
 * @param self Pointer to AL_AL_Char.
 * @return true if empty, false otherwise.
 */
bool al_al_char_empty(AL_AL_Char const *const self);

/**
 * @brief Get the first string array in the list.
 * @param self Pointer to AL_AL_Char.
 * @return C string array pointer to first element, or nullptr when the list is empty.
 * @note Answers nullptr on an empty list rather than aborting: emptiness is a
 *         data question, not a broken contract.
 * @note The address is the nested list's own buffer: that list's add/reserve/shrink invalidates it, the outer list's growth does not, and the outer list's remove/clear/uninit free it.
 */
char** al_al_char_front_1(AL_AL_Char const *const self);

/**
 * @brief Get the first AL_Char in the list.
 * @param self Pointer to AL_AL_Char.
 * @return Pointer to first AL_Char, or nullptr when the list is empty.
 * @note Answers nullptr on an empty list rather than aborting: emptiness is a
 *         data question, not a broken contract.
 * @note The address is valid only until the next add/add_first/add_last/reserve/shrink: any growth may move the backing array and invalidate it.
 */
AL_Char* al_al_char_front_2(AL_AL_Char const *const self);

/**
 * @brief Get the element capacity.
 * @param self Pointer to AL_AL_Char.
 * @return The number of elements the list can hold before it must grow.
 */
USize al_al_char_get_capacity(AL_AL_Char const *const self);

/**
 * @brief Get the data array pointer.
 * @param self Pointer to AL_AL_Char.
 * @return Pointer to the element array. The elements are nested AL_Char lists
 *         owned by this list, so the caller reads them but never frees one.
 */
AL_Char* al_al_char_get_data(AL_AL_Char const *const self);

/**
 * @brief Get the element count.
 * @param self Pointer to AL_AL_Char.
 * @return The number of elements currently stored.
 */
USize al_al_char_get_size(AL_AL_Char const *const self);

/**
 * @brief Initialize an empty array list.
 * @return Initialized AL_AL_Char.
 */
AL_AL_Char al_al_char_init_1(void);

/**
 * @brief Initialize array list with capacity.
 * @param capacity Initial capacity.
 * @return Initialized AL_AL_Char.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
AL_AL_Char al_al_char_init_2(USize const capacity);

/**
 * @brief Initialize array list from data and size.
 * @param data Array of AL_Char elements.
 * @param data_size Number of elements.
 * @return Initialized AL_AL_Char.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 * @note COPIES the caller's array into storage of its own; it does not adopt it.
 *         The caller keeps ownership of `data` and must release it. The ELEMENTS
 *         are taken by value, so ownership of what they point to transfers to
 *         this list and is released by remove/clear/uninit.
 *         A data_size of zero ABORTS rather than building an empty list: this
 *         argument is the capacity, and a zero capacity is a caller contract
 *         under the family's empty-value policy (an empty VALUE is legal
 *         everywhere; a zero capacity or allocation size is not). Construct with
 *         init_1 when you want an empty list.
 */
AL_AL_Char al_al_char_init_3(AL_Char *const data, USize const data_size);

/**
 * @brief Allocate a new AL_AL_Char on the heap.
 * @return Pointer to new AL_AL_Char.
 */
AL_AL_Char* al_al_char_new_1(void);

/**
 * @brief Allocate a new AL_AL_Char with capacity on the heap.
 * @param capacity Initial capacity.
 * @return Pointer to new AL_AL_Char.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
AL_AL_Char* al_al_char_new_2(USize const capacity);

/**
 * @brief Allocate a new AL_AL_Char from data and size on the heap.
 * @param data Array of AL_Char elements.
 * @param data_size Number of elements.
 * @return Pointer to new AL_AL_Char.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 * @note COPIES the caller's array into storage of its own; it does not adopt it.
 *         The caller keeps ownership of `data` and must release it. The ELEMENTS
 *         are taken by value, so ownership of what they point to transfers to
 *         this list and is released by remove/clear/uninit.
 *         A data_size of zero ABORTS rather than building an empty list: this
 *         argument is the capacity, and a zero capacity is a caller contract
 *         under the family's empty-value policy (an empty VALUE is legal
 *         everywhere; a zero capacity or allocation size is not). Construct with
 *         init_1 when you want an empty list.
 */
AL_AL_Char* al_al_char_new_3(AL_Char *const data, USize const data_size);

/**
 * @brief Remove the AL_Char at the specified index.
 * @param self Pointer to AL_AL_Char.
 * @param index Index to remove.
 */
void al_al_char_remove(AL_AL_Char *const self, USize const index);

/**
 * @brief Remove the first AL_Char in the list.
 * @param self Pointer to AL_AL_Char.
 * @note A call on an empty list is a silent no-op (size stays 0).
 */
void al_al_char_remove_first(AL_AL_Char *const self);

/**
 * @brief Remove the last AL_Char in the list.
 * @param self Pointer to AL_AL_Char.
 * @note A call on an empty list is a silent no-op (size stays 0).
 */
void al_al_char_remove_last(AL_AL_Char *const self);

/**
 * @brief Reserve capacity for the list.
 * @param self Pointer to AL_AL_Char.
 * @param capacity New capacity.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
void al_al_char_reserve(AL_AL_Char *const self, USize const capacity);

/**
 * @brief Shrink the list to fit its size.
 * @param self Pointer to AL_AL_Char.
 */
void al_al_char_shrink(AL_AL_Char *const self);

/**
 * @brief Release all memory and reset the list.
 * @param self Pointer to AL_AL_Char.
 * @note Idempotent in every build. The freed pointer is cleared unconditionally
 *         rather than under MEMORY_NON_DANGLING_POINTER, so a second uninit cannot
 *         hand a released block back to the allocator.
 */
void al_al_char_uninit(AL_AL_Char *const self);

#endif // CONTAINER_ARRAYLIST_AL_CHAR_H