/*
 * al_u8.h - Dynamic array list of U8 for the C Libraries Framework
 *
 * Features:
 *   - Dynamic, growable array of U8 values
 *   - Arena or heap allocation support
 *   - Add, remove, access, and manage U8 elements efficiently
 *
 * Family:
 *   One of the hand-cloned typed array lists of container/arrayList; al_u64 is
 *   the canonical instantiation and tools/al_divergence.py measures each
 *   file's divergence from it. This instantiation adds al_u8_set_size (a
 *   silent clamp-to-capacity write of the size field) and a second, redundant
 *   capacity bound in at() beyond the canonical's API.
 *
 * Usage Examples:
 *   @code
 *   AL_U8 list = al_u8_init_1();
 *   al_u8_add_last(&list, 3);
 *   U8 *val = al_u8_at(&list, 0);
 *   al_u8_uninit(&list);
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
 *     free. al_u8_uninit releases the buffer. Arena-backed lists are freed by releasing the arena.
 *
 * Performance Characteristics:
 *   - Amortized O(1) append; O(n) insert/remove at an arbitrary index; O(1) indexed access.
 *
 * Dependencies:
 *   - <allocator/allocator.h>
 *
 * See al_u8.c for implementation details.
 */

#ifndef CONTAINER_ARRAYLIST_U8_H
#define CONTAINER_ARRAYLIST_U8_H

#include <allocator/allocator.h>

/**
 * @brief Dynamic array list of U8 values.
 */
typedef struct {
#ifdef ARENA_IMPLEMENTATION
    Arena   *allocator; /**< Arena allocator pointer (if used) */
#endif // ARENA_IMPLEMENTATION
    USize   capacity;   /**< Allocated capacity */
    U8      *data;      /**< Array of U8 values */
    USize   size;       /**< Number of elements */
} AL_U8;

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Initialize an empty array list with arena allocator.
 * @param allocator Arena pointer.
 * @return Initialized AL_U8.
 */
AL_U8 al_u8_alloc_init_1(Arena *allocator);

/**
 * @brief Initialize array list with capacity and arena allocator.
 * @param capacity Initial capacity.
 * @param allocator Arena pointer.
 * @return Initialized AL_U8.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
AL_U8 al_u8_alloc_init_2(USize const capacity, Arena *allocator);

/**
 * @brief Initialize array list from data and size with arena allocator.
 * @param data Array of U8 values.
 * @param data_size Number of elements.
 * @param allocator Arena pointer.
 * @return Initialized AL_U8.
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
AL_U8 al_u8_alloc_init_3(U8 const *const data, USize const data_size, Arena *allocator);

/**
 * @brief Allocate a new AL_U8 on the arena.
 * @param allocator Arena pointer.
 * @return Pointer to new AL_U8.
 */
AL_U8* al_u8_alloc_new_1(Arena *allocator);

/**
 * @brief Allocate a new AL_U8 with capacity on the arena.
 * @param capacity Initial capacity.
 * @param allocator Arena pointer.
 * @return Pointer to new AL_U8.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
AL_U8* al_u8_alloc_new_2(USize const capacity, Arena *allocator);

/**
 * @brief Allocate a new AL_U8 from data and size on the arena.
 * @param data Array of U8 values.
 * @param data_size Number of elements.
 * @param allocator Arena pointer.
 * @return Pointer to new AL_U8.
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
AL_U8* al_u8_alloc_new_3(U8 const *const data, USize const data_size, Arena *allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Add a value at the specified index.
 * @param self Pointer to AL_U8.
 * @param data Value to add.
 * @param index Index to insert at.
 * @note Declines silently when the allocator refuses the growth (a refused
 *         arena, or a capacity whose byte size would wrap). The list is left
 *         unchanged, so a caller that must know checks the size afterwards.
 * @note The element is taken BY VALUE into a slot this list owns; where the
 *         element type owns memory of its own, that ownership transfers here and
 *         is released by remove/clear/uninit.
 */
void al_u8_add(AL_U8 *const self, U8 const data, USize const index);

/**
 * @brief Add a value at the beginning.
 * @param self Pointer to AL_U8.
 * @param data Value to add.
 * @note Declines silently when the allocator refuses the growth (a refused
 *         arena, or a capacity whose byte size would wrap). The list is left
 *         unchanged, so a caller that must know checks the size afterwards.
 * @note The element is taken BY VALUE into a slot this list owns; where the
 *         element type owns memory of its own, that ownership transfers here and
 *         is released by remove/clear/uninit.
 */
void al_u8_add_first(AL_U8 *const self, U8 const data);

/**
 * @brief Add a value at the end.
 * @param self Pointer to AL_U8.
 * @param data Value to add.
 * @note Declines silently when the allocator refuses the growth (a refused
 *         arena, or a capacity whose byte size would wrap). The list is left
 *         unchanged, so a caller that must know checks the size afterwards.
 * @note The element is taken BY VALUE into a slot this list owns; where the
 *         element type owns memory of its own, that ownership transfers here and
 *         is released by remove/clear/uninit.
 */
void al_u8_add_last(AL_U8 *const self, U8 const data);

/**
 * @brief Get the value at the specified index.
 * @param self Pointer to AL_U8.
 * @param index Index to access.
 * @return Pointer to value at index.
 * @note Bounded by size AND by capacity. The size bound is the contract - the
 *         slots a clear() leaves inside the retained capacity are out of contract,
 *         not a way to inspect a released element. The capacity bound is a second
 *         line kept only in this file and al_bool: set_size CLAMPS now, so size
 *         never exceeds capacity and this check is redundant - it is kept as the
 *         cheapest tripwire if that clamp ever regresses, and it compiles out
 *         without ERROR_CHECK_ENABLED, so the clamp is the protection and this is
 *         only the messenger. See the set_size note.
 * @note The address is valid only until the next add/add_first/add_last/reserve/shrink: any growth may move the backing array and invalidate it.
 */
U8* al_u8_at(AL_U8 const *const self, USize const index);

/**
 * @brief Get the last value in the list.
 * @param self Pointer to AL_U8.
 * @return Pointer to the last value, or nullptr when the list is empty.
 * @note Answers nullptr on an empty list rather than aborting: emptiness is a
 *         data question, not a broken contract.
 * @note The address is valid only until the next add/add_first/add_last/reserve/shrink: any growth may move the backing array and invalidate it.
 */
U8* al_u8_back(AL_U8 const *const self);

/**
 * @brief Remove all elements from the list.
 * @param self Pointer to AL_U8.
 */
void al_u8_clear(AL_U8 *const self);

/**
 * @brief Delete and free the list.
 * @param self Address of AL_U8 pointer.
 */
void al_u8_delete(AL_U8 **const self);

/**
 * @brief Check if the list is empty.
 * @param self Pointer to AL_U8.
 * @return true if empty, false otherwise.
 */
bool al_u8_empty(AL_U8 const *const self);

/**
 * @brief Get the first value in the list.
 * @param self Pointer to AL_U8.
 * @return Pointer to the first value, or nullptr when the list is empty.
 * @note Answers nullptr on an empty list rather than aborting: emptiness is a
 *         data question, not a broken contract.
 * @note The address is valid only until the next add/add_first/add_last/reserve/shrink: any growth may move the backing array and invalidate it.
 */
U8* al_u8_front(AL_U8 const *const self);

/**
 * @brief Get the element capacity.
 * @param self Pointer to AL_U8.
 * @return The number of elements the list can hold before it must grow.
 */
USize al_u8_get_capacity(AL_U8 const *const self);

/**
 * @brief Get the data array pointer.
 * @param self Pointer to AL_U8.
 * @return Pointer to data array.
 */
U8* al_u8_get_data(AL_U8 const *const self);

/**
 * @brief Get the element count.
 * @param self Pointer to AL_U8.
 * @return The number of elements currently stored.
 */
USize al_u8_get_size(AL_U8 const *const self);

/**
 * @brief Initialize an empty array list.
 * @return Initialized AL_U8.
 */
AL_U8 al_u8_init_1(void);

/**
 * @brief Initialize array list with capacity.
 * @param capacity Initial capacity.
 * @return Initialized AL_U8.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
AL_U8 al_u8_init_2(USize const capacity);

/**
 * @brief Initialize array list from data and size.
 * @param data Array of U8 values.
 * @param data_size Number of elements.
 * @return Initialized AL_U8.
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
AL_U8 al_u8_init_3(U8 const *const data, USize const data_size);

/**
 * @brief Allocate a new AL_U8 on the heap.
 * @return Pointer to new AL_U8.
 */
AL_U8* al_u8_new_1(void);

/**
 * @brief Allocate a new AL_U8 with capacity on the heap.
 * @param capacity Initial capacity.
 * @return Pointer to new AL_U8.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
AL_U8* al_u8_new_2(USize const capacity);

/**
 * @brief Allocate a new AL_U8 from data and size on the heap.
 * @param data Array of U8 values.
 * @param data_size Number of elements.
 * @return Pointer to new AL_U8.
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
AL_U8* al_u8_new_3(U8 const *const data, USize const data_size);

/**
 * @brief Remove the value at the specified index.
 * @param self Pointer to AL_U8.
 * @param index Index to remove.
 */
void al_u8_remove(AL_U8 *const self, USize const index);

/**
 * @brief Remove the first value in the list.
 * @param self Pointer to AL_U8.
 * @note A call on an empty list is a silent no-op (size stays 0).
 */
void al_u8_remove_first(AL_U8 *const self);

/**
 * @brief Remove the last value in the list.
 * @param self Pointer to AL_U8.
 * @note A call on an empty list is a silent no-op (size stays 0).
 */
void al_u8_remove_last(AL_U8 *const self);

/**
 * @brief Reserve capacity for the list.
 * @param self Pointer to AL_U8.
 * @param capacity New capacity.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
void al_u8_reserve(AL_U8 *const self, USize const capacity);

/**
 * @brief Set the element count directly.
 * @param self Pointer to AL_U8.
 * @param size New element count.
 * @note CLAMPED to the capacity. It moves the size without touching the capacity or
 *         the elements, so raising it over slots not currently in use still exposes
 *         their contents - zeros in a fresh buffer, but whatever was last written in
 *         a reused one - and it can no longer state a size the
 *         storage cannot back, which is what let `init_2(4); set_size(1 << 26);
 *         add_last(x);` write 64 MiB past a 4-byte block through the public API.
 *         Reserve first if you need a larger size; a declined reserve then shows up
 *         as a clamped size rather than as a wild write.
 *         The clamp bounds THIS LIST'S OWN accessors. When the buffer is filled by
 *         anything else - an external writer that fills get_data() directly - that
 *         writer does not consult size, so check the capacity yourself before
 *         trusting a reserve.
 */
void al_u8_set_size(AL_U8 *const self, USize const size);

/**
 * @brief Shrink the list to fit its size.
 * @param self Pointer to AL_U8.
 */
void al_u8_shrink(AL_U8 *const self);

/**
 * @brief Release all memory and reset the list.
 * @param self Pointer to AL_U8.
 * @note Idempotent in every build. The freed pointer is cleared unconditionally
 *         rather than under MEMORY_NON_DANGLING_POINTER, so a second uninit cannot
 *         hand a released block back to the allocator.
 */
void al_u8_uninit(AL_U8 *const self);

#endif // CONTAINER_ARRAYLIST_U8_H