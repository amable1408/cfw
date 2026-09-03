/*
 * al_void.h - Dynamic array list of void* for the C Libraries Framework
 *
 * Features:
 *   - Dynamic, growable array of void* pointers
 *   - Arena or heap allocation support
 *   - Add, remove, access, and manage pointer elements efficiently
 *
 * Family:
 *   One of the hand-cloned typed array lists of container/arrayList; al_u64 is
 *   the canonical instantiation and tools/al_divergence.py measures each
 *   file's divergence from it. The element here is void* rather than a fixed
 *   value type: at()/back()/front() return the address of the pointer SLOT
 *   (void**), not the pointed-to data.
 *
 * Usage Examples:
 *   @code
 *   AL_Void list = al_void_init_1();
 *   al_void_add_last(&list, some_pointer);
 *   void *value = *al_void_at(&list, 0);
 *   al_void_uninit(&list);
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
 *     - a stored nullptr element is a legal value like any other pointer - the
 *       list never releases per-element pointers, owned or not;
 *     - remove_first/remove_last are silent no-ops on an empty list (size
 *       stays 0).
 *
 * Thread Safety:
 *   Not thread-safe. Caller must synchronize if used from multiple threads.
 *
 * Memory Management:
 *   - Non-owning: only the pointer array is allocated. al_void_uninit releases that array;
 *     the pointed-to objects remain the caller's to free.
 *
 * Performance Characteristics:
 *   - Amortized O(1) append; O(n) insert/remove at an arbitrary index; O(1) indexed access.
 *
 * Dependencies:
 *   - <allocator/allocator.h>
 *
 * See al_void.c for implementation details.
 */

#ifndef CONTAINER_ARRAYLIST_VOID_H
#define CONTAINER_ARRAYLIST_VOID_H

#include <allocator/allocator.h>

/**
 * @brief Dynamic array list of void* pointers.
 */
typedef struct {
#ifdef ARENA_IMPLEMENTATION
    Arena   *allocator; /**< Arena allocator pointer (if used) */
#endif // ARENA_IMPLEMENTATION
    USize   capacity;   /**< Allocated capacity */
    void    **data;     /**< Array of void* pointers */
    USize   size;       /**< Number of elements */
} AL_Void;

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Initialize an empty array list with arena allocator.
 * @param allocator Arena pointer.
 * @return Initialized AL_Void.
 */
AL_Void al_void_alloc_init_1(Arena *allocator);

/**
 * @brief Initialize array list with capacity and arena allocator.
 * @param capacity Initial capacity.
 * @param allocator Arena pointer.
 * @return Initialized AL_Void.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
AL_Void al_void_alloc_init_2(USize const capacity, Arena *allocator);

/**
 * @brief Initialize array list from data and size with arena allocator.
 * @param data Array of void* pointers.
 * @param data_size Number of elements.
 * @param allocator Arena pointer.
 * @return Initialized AL_Void.
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
AL_Void al_void_alloc_init_3(void *const *const data, USize const data_size, Arena *allocator);

/**
 * @brief Allocate a new AL_Void on the arena.
 * @param allocator Arena pointer.
 * @return Pointer to new AL_Void.
 */
AL_Void* al_void_alloc_new_1(Arena *allocator);

/**
 * @brief Allocate a new AL_Void with capacity on the arena.
 * @param capacity Initial capacity.
 * @param allocator Arena pointer.
 * @return Pointer to new AL_Void.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
AL_Void* al_void_alloc_new_2(USize const capacity, Arena *allocator);

/**
 * @brief Allocate a new AL_Void from data and size on the arena.
 * @param data Array of void* pointers.
 * @param data_size Number of elements.
 * @param allocator Arena pointer.
 * @return Pointer to new AL_Void.
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
AL_Void* al_void_alloc_new_3(void *const *const data, USize const data_size, Arena *allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Add a pointer at the specified index.
 * @param self Pointer to AL_Void.
 * @param data Pointer to add.
 * @param index Index to insert at.
 * @note Declines silently when the allocator refuses the growth (a refused
 *         arena, or a capacity whose byte size would wrap). The list is left
 *         unchanged, so a caller that must know checks the size afterwards.
 */
void al_void_add(AL_Void *const self, void *const data, USize const index);

/**
 * @brief Add a pointer at the beginning.
 * @param self Pointer to AL_Void.
 * @param data Pointer to add.
 * @note Declines silently when the allocator refuses the growth (a refused
 *         arena, or a capacity whose byte size would wrap). The list is left
 *         unchanged, so a caller that must know checks the size afterwards.
 */
void al_void_add_first(AL_Void *const self, void *const data);

/**
 * @brief Add a pointer at the end.
 * @param self Pointer to AL_Void.
 * @param data Pointer to add.
 * @note Declines silently when the allocator refuses the growth (a refused
 *         arena, or a capacity whose byte size would wrap). The list is left
 *         unchanged, so a caller that must know checks the size afterwards.
 */
void al_void_add_last(AL_Void *const self, void *const data);

/**
 * @brief Get the slot at the specified index.
 * @param self Pointer to AL_Void.
 * @param index Index to access.
 * @return Address of the pointer slot at index.
 * @note Bounded by size, not capacity. The slots a clear() leaves inside the
 *         retained capacity are out of contract - reading one is a caller error,
 *         not a way to inspect a released element.
 * @note The address is valid only until the next add/add_first/add_last/reserve/shrink: any growth may move the backing array and invalidate it.
 */
void** al_void_at(AL_Void const *const self, USize const index);

/**
 * @brief Get the last slot in the list.
 * @param self Pointer to AL_Void.
 * @return Address of the last pointer slot.
 * @note Answers nullptr on an empty list rather than aborting: emptiness is a
 *         data question, not a broken contract.
 * @note The address is valid only until the next add/add_first/add_last/reserve/shrink: any growth may move the backing array and invalidate it.
 */
void** al_void_back(AL_Void const *const self);

/**
 * @brief Remove all elements from the list.
 * @param self Pointer to AL_Void.
 */
void al_void_clear(AL_Void *const self);

/**
 * @brief Delete and free the list.
 * @param self Address of AL_Void pointer.
 */
void al_void_delete(AL_Void **const self);

/**
 * @brief Check if the list is empty.
 * @param self Pointer to AL_Void.
 * @return true if empty, false otherwise.
 */
bool al_void_empty(AL_Void const *const self);

/**
 * @brief Get the first slot in the list.
 * @param self Pointer to AL_Void.
 * @return Address of the first pointer slot.
 * @note Answers nullptr on an empty list rather than aborting: emptiness is a
 *         data question, not a broken contract.
 * @note The address is valid only until the next add/add_first/add_last/reserve/shrink: any growth may move the backing array and invalidate it.
 */
void** al_void_front(AL_Void const *const self);

/**
 * @brief Get the capacity of the list.
 * @param self Pointer to AL_Void.
 * @return Allocated capacity.
 */
USize al_void_get_capacity(AL_Void const *const self);

/**
 * @brief Get the data array pointer.
 * @param self Pointer to AL_Void.
 * @return Pointer to the backing array.
 */
void** al_void_get_data(AL_Void const *const self);

/**
 * @brief Get the number of elements in the list.
 * @param self Pointer to AL_Void.
 * @return Number of elements.
 */
USize al_void_get_size(AL_Void const *const self);

/**
 * @brief Initialize an empty array list.
 * @return Initialized AL_Void.
 */
AL_Void al_void_init_1(void);

/**
 * @brief Initialize array list with capacity.
 * @param capacity Initial capacity.
 * @return Initialized AL_Void.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
AL_Void al_void_init_2(USize const capacity);

/**
 * @brief Initialize array list from data and size.
 * @param data Array of void* pointers.
 * @param data_size Number of elements.
 * @return Initialized AL_Void.
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
AL_Void al_void_init_3(void *const *const data, USize const data_size);

/**
 * @brief Allocate a new AL_Void on the heap.
 * @return Pointer to new AL_Void.
 */
AL_Void* al_void_new_1(void);

/**
 * @brief Allocate a new AL_Void with capacity on the heap.
 * @param capacity Initial capacity.
 * @return Pointer to new AL_Void.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
AL_Void* al_void_new_2(USize const capacity);

/**
 * @brief Allocate a new AL_Void from data and size on the heap.
 * @param data Array of void* pointers.
 * @param data_size Number of elements.
 * @return Pointer to new AL_Void.
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
AL_Void* al_void_new_3(void *const *const data, USize const data_size);

/**
 * @brief Remove the pointer at the specified index.
 * @param self Pointer to AL_Void.
 * @param index Index to remove.
 */
void al_void_remove(AL_Void *const self, USize const index);

/**
 * @brief Remove the first pointer in the list.
 * @param self Pointer to AL_Void.
 * @note A call on an empty list is a silent no-op (size stays 0).
 */
void al_void_remove_first(AL_Void *const self);

/**
 * @brief Remove the last pointer in the list.
 * @param self Pointer to AL_Void.
 * @note A call on an empty list is a silent no-op (size stays 0).
 */
void al_void_remove_last(AL_Void *const self);

/**
 * @brief Reserve capacity for the list.
 * @param self Pointer to AL_Void.
 * @param capacity New capacity.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
void al_void_reserve(AL_Void *const self, USize const capacity);

/**
 * @brief Shrink the list to fit its size.
 * @param self Pointer to AL_Void.
 */
void al_void_shrink(AL_Void *const self);

/**
 * @brief Release the backing array and reset the list.
 * @param self Pointer to AL_Void.
 * @note Idempotent in every build. The freed pointer is cleared unconditionally
 *         rather than under MEMORY_NON_DANGLING_POINTER, so a second uninit cannot
 *         hand a released block back to the allocator.
 */
void al_void_uninit(AL_Void *const self);

#endif // CONTAINER_ARRAYLIST_VOID_H