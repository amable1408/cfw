/*
 * al_f32.h - Dynamic array list of F32 for the C Libraries Framework
 *
 * Features:
 *   - Dynamic, growable array of F32 values
 *   - Arena or heap allocation support
 *   - Add, remove, access, and manage F32 elements efficiently
 *
 * Family:
 *   One of the hand-cloned typed array lists of container/arrayList; al_u64 is
 *   the canonical instantiation and tools/al_divergence.py measures each
 *   file's divergence from it. This instantiation differs from the canonical
 *   only in its typed zero literals (0.0f) where a U64 slot would write the
 *   integer 0.
 *
 * Usage Examples:
 *   @code
 *   AL_F32 list = al_f32_init_1();
 *   al_f32_add_last(&list, 3.14f);
 *   F32 *val = al_f32_at(&list, 0);
 *   al_f32_uninit(&list);
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
 *     - there is no nullptr element for a numeric value; a size of zero is a
 *       legal state throughout, never an error;
 *     - remove_first/remove_last are silent no-ops on an empty list (size
 *       stays 0).
 *
 * Thread Safety:
 *   Not thread-safe. Caller must synchronize if used from multiple threads.
 *
 * Memory Management:
 *   - The list owns only its backing buffer; the elements are plain values with nothing to
 *     free. al_f32_uninit releases the buffer. Arena-backed lists are freed by releasing the arena.
 *
 * Performance Characteristics:
 *   - Amortized O(1) append; O(n) insert/remove at an arbitrary index; O(1) indexed access.
 *
 * Dependencies:
 *   - <allocator/allocator.h>
 *
 * See al_f32.c for implementation details.
 */

#ifndef CONTAINER_ARRAYLIST_F32_H
#define CONTAINER_ARRAYLIST_F32_H

#include <allocator/allocator.h>

/**
 * @brief Dynamic array list of F32 values.
 */
typedef struct {
#ifdef ARENA_IMPLEMENTATION
    Arena   *allocator; /**< Arena allocator pointer (if used) */
#endif // ARENA_IMPLEMENTATION
    USize   capacity;   /**< Allocated capacity */
    F32     *data;      /**< Array of F32 values */
    USize   size;       /**< Number of elements */
} AL_F32;

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Initialize an empty array list with arena allocator.
 * @param allocator Arena pointer.
 * @return Initialized AL_F32.
 */
AL_F32 al_f32_alloc_init_1(Arena *allocator);

/**
 * @brief Initialize array list with capacity and arena allocator.
 * @param capacity Initial capacity.
 * @param allocator Arena pointer.
 * @return Initialized AL_F32.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
AL_F32 al_f32_alloc_init_2(USize const capacity, Arena *allocator);

/**
 * @brief Initialize array list from data and size with arena allocator.
 * @param data Array of F32 values.
 * @param data_size Number of elements.
 * @param allocator Arena pointer.
 * @return Initialized AL_F32.
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
AL_F32 al_f32_alloc_init_3(F32 const *const data, USize const data_size, Arena *allocator);

/**
 * @brief Allocate a new AL_F32 on the arena.
 * @param allocator Arena pointer.
 * @return Pointer to new AL_F32.
 */
AL_F32* al_f32_alloc_new_1(Arena *allocator);

/**
 * @brief Allocate a new AL_F32 with capacity on the arena.
 * @param capacity Initial capacity.
 * @param allocator Arena pointer.
 * @return Pointer to new AL_F32.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
AL_F32* al_f32_alloc_new_2(USize const capacity, Arena *allocator);

/**
 * @brief Allocate a new AL_F32 from data and size on the arena.
 * @param data Array of F32 values.
 * @param data_size Number of elements.
 * @param allocator Arena pointer.
 * @return Pointer to new AL_F32.
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
AL_F32* al_f32_alloc_new_3(F32 const *const data, USize const data_size, Arena *allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Add a value at the specified index.
 * @param self Pointer to AL_F32.
 * @param data Value to add.
 * @param index Index to insert at.
 * @note Declines silently when the allocator refuses the growth (a refused
 *         arena, or a capacity whose byte size would wrap). The list is left
 *         unchanged, so a caller that must know checks the size afterwards.
 * @note The element is taken BY VALUE into a slot this list owns; where the
 *         element type owns memory of its own, that ownership transfers here and
 *         is released by remove/clear/uninit.
 */
void al_f32_add(AL_F32 *const self, F32 const data, USize const index);

/**
 * @brief Add a value at the beginning.
 * @param self Pointer to AL_F32.
 * @param data Value to add.
 * @note Declines silently when the allocator refuses the growth (a refused
 *         arena, or a capacity whose byte size would wrap). The list is left
 *         unchanged, so a caller that must know checks the size afterwards.
 * @note The element is taken BY VALUE into a slot this list owns; where the
 *         element type owns memory of its own, that ownership transfers here and
 *         is released by remove/clear/uninit.
 */
void al_f32_add_first(AL_F32 *const self, F32 const data);

/**
 * @brief Add a value at the end.
 * @param self Pointer to AL_F32.
 * @param data Value to add.
 * @note Declines silently when the allocator refuses the growth (a refused
 *         arena, or a capacity whose byte size would wrap). The list is left
 *         unchanged, so a caller that must know checks the size afterwards.
 * @note The element is taken BY VALUE into a slot this list owns; where the
 *         element type owns memory of its own, that ownership transfers here and
 *         is released by remove/clear/uninit.
 */
void al_f32_add_last(AL_F32 *const self, F32 const data);

/**
 * @brief Get the value at the specified index.
 * @param self Pointer to AL_F32.
 * @param index Index to access.
 * @return Pointer to value at index.
 * @note Bounded by size, not capacity. The slots a clear() leaves inside the
 *         retained capacity are out of contract - reading one is a caller error,
 *         not a way to inspect a released element.
 * @note The address is valid only until the next add/add_first/add_last/reserve/shrink: any growth may move the backing array and invalidate it.
 */
F32* al_f32_at(AL_F32 const *const self, USize const index);

/**
 * @brief Get the last value in the list.
 * @param self Pointer to AL_F32.
 * @return Pointer to the last value, or nullptr when the list is empty.
 * @note Answers nullptr on an empty list rather than aborting: emptiness is a
 *         data question, not a broken contract.
 * @note The address is valid only until the next add/add_first/add_last/reserve/shrink: any growth may move the backing array and invalidate it.
 */
F32* al_f32_back(AL_F32 const *const self);

/**
 * @brief Remove all elements from the list.
 * @param self Pointer to AL_F32.
 */
void al_f32_clear(AL_F32 *const self);

/**
 * @brief Delete and free the list.
 * @param self Address of AL_F32 pointer.
 */
void al_f32_delete(AL_F32 **const self);

/**
 * @brief Check if the list is empty.
 * @param self Pointer to AL_F32.
 * @return true if empty, false otherwise.
 */
bool al_f32_empty(AL_F32 const *const self);

/**
 * @brief Get the first value in the list.
 * @param self Pointer to AL_F32.
 * @return Pointer to the first value, or nullptr when the list is empty.
 * @note Answers nullptr on an empty list rather than aborting: emptiness is a
 *         data question, not a broken contract.
 * @note The address is valid only until the next add/add_first/add_last/reserve/shrink: any growth may move the backing array and invalidate it.
 */
F32* al_f32_front(AL_F32 const *const self);

/**
 * @brief Get the element capacity.
 * @param self Pointer to AL_F32.
 * @return The number of elements the list can hold before it must grow.
 */
USize al_f32_get_capacity(AL_F32 const *const self);

/**
 * @brief Get the data array pointer.
 * @param self Pointer to AL_F32.
 * @return Pointer to data array.
 */
F32* al_f32_get_data(AL_F32 const *const self);

/**
 * @brief Get the element count.
 * @param self Pointer to AL_F32.
 * @return The number of elements currently stored.
 */
USize al_f32_get_size(AL_F32 const *const self);

/**
 * @brief Initialize an empty array list.
 * @return Initialized AL_F32.
 */
AL_F32 al_f32_init_1(void);

/**
 * @brief Initialize array list with capacity.
 * @param capacity Initial capacity.
 * @return Initialized AL_F32.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
AL_F32 al_f32_init_2(USize const capacity);

/**
 * @brief Initialize array list from data and size.
 * @param data Array of F32 values.
 * @param data_size Number of elements.
 * @return Initialized AL_F32.
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
AL_F32 al_f32_init_3(F32 const *const data, USize const data_size);

/**
 * @brief Allocate a new AL_F32 on the heap.
 * @return Pointer to new AL_F32.
 */
AL_F32* al_f32_new_1(void);

/**
 * @brief Allocate a new AL_F32 with capacity on the heap.
 * @param capacity Initial capacity.
 * @return Pointer to new AL_F32.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
AL_F32* al_f32_new_2(USize const capacity);

/**
 * @brief Allocate a new AL_F32 from data and size on the heap.
 * @param data Array of F32 values.
 * @param data_size Number of elements.
 * @return Pointer to new AL_F32.
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
AL_F32* al_f32_new_3(F32 const *const data, USize const data_size);

/**
 * @brief Remove the value at the specified index.
 * @param self Pointer to AL_F32.
 * @param index Index to remove.
 */
void al_f32_remove(AL_F32 *const self, USize const index);

/**
 * @brief Remove the first value in the list.
 * @param self Pointer to AL_F32.
 * @note A call on an empty list is a silent no-op (size stays 0).
 */
void al_f32_remove_first(AL_F32 *const self);

/**
 * @brief Remove the last value in the list.
 * @param self Pointer to AL_F32.
 * @note A call on an empty list is a silent no-op (size stays 0).
 */
void al_f32_remove_last(AL_F32 *const self);

/**
 * @brief Reserve capacity for the list.
 * @param self Pointer to AL_F32.
 * @param capacity New capacity.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
void al_f32_reserve(AL_F32 *const self, USize const capacity);

/**
 * @brief Shrink the list to fit its size.
 * @param self Pointer to AL_F32.
 */
void al_f32_shrink(AL_F32 *const self);

/**
 * @brief Release all memory and reset the list.
 * @param self Pointer to AL_F32.
 * @note Idempotent in every build. The freed pointer is cleared unconditionally
 *         rather than under MEMORY_NON_DANGLING_POINTER, so a second uninit cannot
 *         hand a released block back to the allocator.
 */
void al_f32_uninit(AL_F32 *const self);

#endif // CONTAINER_ARRAYLIST_F32_H