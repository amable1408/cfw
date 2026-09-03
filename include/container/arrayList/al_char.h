/*
 * al_char.h - Dynamic array list of char* for the C Libraries Framework
 *
 * Features:
 *   - Dynamic, growable array of char* string elements
 *   - Arena or heap allocation support
 *   - Add, remove, access, and manage string pointers efficiently
 *
 * Family:
 *   One of the hand-cloned typed array lists of container/arrayList; al_u64 is
 *   the canonical instantiation and tools/al_divergence.py measures each
 *   file's divergence from it. This instantiation owns its char* element
 *   buffers (released on clear/remove), at()/front()/back() return the
 *   ELEMENT (char*) itself rather than its address, and init_3 copies the
 *   caller's array by value like the rest of the family.
 *
 * Usage Examples:
 *   @code
 *   AL_Char list = al_char_init_1();
 *   al_char_add_last(&list, char_new_2("hello")); // a fresh copy: the list owns and frees it
 *   char *str = al_char_at(&list, 0);
 *   al_char_uninit(&list);
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
 *     - back() and front() answer nullptr on an empty list.
 *
 * Thread Safety:
 *   Not thread-safe. Caller must synchronize if used from multiple threads.
 *
 * Memory Management:
 *   - PRECONDITION: an element handed to this list must have come from the SAME
 *     allocator the list was constructed with. remove/clear/uninit release each
 *     element through the list's allocator, so a string borrowed from a different
 *     arena - or a string literal - is handed to the wrong deallocator.
 *   - A stored nullptr is a legal element value and is skipped on release.
 *   - The list owns its char* elements. al_char_clear and al_char_remove release the
 *     element through the list's own allocator, and al_char_uninit releases every element
 *     plus the backing array. Arena-backed lists are freed by releasing the arena.
 *
 * Performance Characteristics:
 *   - Amortized O(1) append; O(n) insert/remove at an arbitrary index; O(1) indexed access.
 *
 * Dependencies:
 *   - <char/char.h>
 *
 * See al_char.c for implementation details.
 */

#ifndef CONTAINER_ARRAYLIST_CHAR_H
#define CONTAINER_ARRAYLIST_CHAR_H

#include <char/char.h>

/**
 * @brief Dynamic array list of C strings (char*).
 */
typedef struct {
#ifdef ARENA_IMPLEMENTATION
    Arena   *allocator; /**< Arena allocator pointer (if used) */
#endif // ARENA_IMPLEMENTATION
    USize   capacity;   /**< Allocated capacity */
    char    **data;     /**< Array of C string pointers */
    USize   size;       /**< Number of elements */
} AL_Char;

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Initialize an empty array list with arena allocator.
 * @param allocator Arena pointer.
 * @return Initialized AL_Char.
 */
AL_Char al_char_alloc_init_1(Arena *allocator);

/**
 * @brief Initialize array list with capacity and arena allocator.
 * @param capacity Initial capacity.
 * @param allocator Arena pointer.
 * @return Initialized AL_Char.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
AL_Char al_char_alloc_init_2(USize const capacity, Arena *allocator);

/**
 * @brief Initialize array list from data and size with arena allocator.
 * @param data Array of C string pointers.
 * @param data_size Number of elements.
 * @param allocator Arena pointer.
 * @return Initialized AL_Char.
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
AL_Char al_char_alloc_init_3(char **const data, USize const data_size, Arena *allocator);

/**
 * @brief Allocate a new AL_Char on the arena.
 * @param allocator Arena pointer.
 * @return Pointer to new AL_Char.
 */
AL_Char* al_char_alloc_new_1(Arena *allocator);

/**
 * @brief Allocate a new AL_Char with capacity on the arena.
 * @param capacity Initial capacity.
 * @param allocator Arena pointer.
 * @return Pointer to new AL_Char.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
AL_Char* al_char_alloc_new_2(USize const capacity, Arena *allocator);

/**
 * @brief Allocate a new AL_Char from data and size on the arena.
 * @param data Array of C string pointers.
 * @param data_size Number of elements.
 * @param allocator Arena pointer.
 * @return Pointer to new AL_Char.
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
AL_Char* al_char_alloc_new_3(char **const data, USize const data_size, Arena *allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Add a string at the specified index.
 * @param self Pointer to AL_Char.
 * @param data C string pointer.
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
 * @note `data` must be memory from this list's allocator that is NOT already an
 *         element of self (a value read back through at/front/back/get_data): the
 *         list would own it twice and release it twice. Pass a fresh copy.
 */
void al_char_add(AL_Char *const self, char *const data, USize const index);

/**
 * @brief Add a string at the beginning.
 * @param self Pointer to AL_Char.
 * @param data C string pointer.
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
 * @note `data` must be memory from this list's allocator that is NOT already an
 *         element of self (a value read back through at/front/back/get_data): the
 *         list would own it twice and release it twice. Pass a fresh copy.
 */
void al_char_add_first(AL_Char *const self, char *const data);

/**
 * @brief Add a string at the end.
 * @param self Pointer to AL_Char.
 * @param data C string pointer.
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
 * @note `data` must be memory from this list's allocator that is NOT already an
 *         element of self (a value read back through at/front/back/get_data): the
 *         list would own it twice and release it twice. Pass a fresh copy.
 */
void al_char_add_last(AL_Char *const self, char *const data);

/**
 * @brief Get the string at the specified index.
 * @param self Pointer to AL_Char.
 * @param index Index to access.
 * @return C string pointer at index.
 * @note Bounded by size, not capacity. The slots a clear() leaves inside the
 *         retained capacity are out of contract - reading one is a caller error,
 *         not a way to inspect a released element.
 * @note The returned string is OWNED by the list: remove/remove_first/remove_last/clear/uninit release it, and the pointer dangles after that. Growth does not move it.
 */
char* al_char_at(AL_Char const *const self, USize const index);

/**
 * @brief Get the last string in the list.
 * @param self Pointer to AL_Char.
 * @return C string pointer to last element.
 * @note Answers nullptr on an empty list rather than aborting: emptiness is a
 *         data question, not a broken contract.
 * @note The returned string is OWNED by the list: remove/remove_first/remove_last/clear/uninit release it, and the pointer dangles after that. Growth does not move it.
 */
char* al_char_back(AL_Char const *const self);

/**
 * @brief Remove all elements from the list.
 * @param self Pointer to AL_Char.
 */
void al_char_clear(AL_Char *const self);

/**
 * @brief Delete and free the list.
 * @param self Address of AL_Char pointer.
 */
void al_char_delete(AL_Char **const self);

/**
 * @brief Check if the list is empty.
 * @param self Pointer to AL_Char.
 * @return true if empty, false otherwise.
 */
bool al_char_empty(AL_Char const *const self);

/**
 * @brief Get the first string in the list.
 * @param self Pointer to AL_Char.
 * @return C string pointer to first element.
 * @note Answers nullptr on an empty list rather than aborting: emptiness is a
 *         data question, not a broken contract.
 * @note The returned string is OWNED by the list: remove/remove_first/remove_last/clear/uninit release it, and the pointer dangles after that. Growth does not move it.
 */
char* al_char_front(AL_Char const *const self);

/**
 * @brief Get the element capacity.
 * @param self Pointer to AL_Char.
 * @return The number of elements the list can hold before it must grow.
 */
USize al_char_get_capacity(AL_Char const *const self);

/**
 * @brief Get the data array pointer.
 * @param self Pointer to AL_Char.
 * @return Pointer to data array.
 */
char** al_char_get_data(AL_Char const *const self);

/**
 * @brief Get the element count.
 * @param self Pointer to AL_Char.
 * @return The number of elements currently stored.
 */
USize al_char_get_size(AL_Char const *const self);

/**
 * @brief Initialize an empty array list.
 * @return Initialized AL_Char.
 */
AL_Char al_char_init_1(void);

/**
 * @brief Initialize array list with capacity.
 * @param capacity Initial capacity.
 * @return Initialized AL_Char.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
AL_Char al_char_init_2(USize const capacity);

/**
 * @brief Initialize array list from data and size.
 * @param data Array of C string pointers.
 * @param data_size Number of elements.
 * @return Initialized AL_Char.
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
AL_Char al_char_init_3(char **const data, USize const data_size);

/**
 * @brief Allocate a new AL_Char on the heap.
 * @return Pointer to new AL_Char.
 */
AL_Char* al_char_new_1(void);

/**
 * @brief Allocate a new AL_Char with capacity on the heap.
 * @param capacity Initial capacity.
 * @return Pointer to new AL_Char.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
AL_Char* al_char_new_2(USize const capacity);

/**
 * @brief Allocate a new AL_Char from data and size on the heap.
 * @param data Array of C string pointers.
 * @param data_size Number of elements.
 * @return Pointer to new AL_Char.
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
AL_Char* al_char_new_3(char **const data, USize const data_size);

/**
 * @brief Remove the string at the specified index.
 * @param self Pointer to AL_Char.
 * @param index Index to remove.
 */
void al_char_remove(AL_Char *const self, USize const index);

/**
 * @brief Remove the first string in the list.
 * @param self Pointer to AL_Char.
 * @note A call on an empty list is a silent no-op (size stays 0).
 */
void al_char_remove_first(AL_Char *const self);

/**
 * @brief Remove the last string in the list.
 * @param self Pointer to AL_Char.
 * @note A call on an empty list is a silent no-op (size stays 0).
 */
void al_char_remove_last(AL_Char *const self);

/**
 * @brief Reserve capacity for the list.
 * @param self Pointer to AL_Char.
 * @param capacity New capacity.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
void al_char_reserve(AL_Char *const self, USize const capacity);

/**
 * @brief Shrink the list to fit its size.
 * @param self Pointer to AL_Char.
 */
void al_char_shrink(AL_Char *const self);

/**
 * @brief Release all memory and reset the list.
 * @param self Pointer to AL_Char.
 * @note Idempotent in every build. The freed pointer is cleared unconditionally
 *         rather than under MEMORY_NON_DANGLING_POINTER, so a second uninit cannot
 *         hand a released block back to the allocator.
 */
void al_char_uninit(AL_Char *const self);

#endif // CONTAINER_ARRAYLIST_CHAR_H