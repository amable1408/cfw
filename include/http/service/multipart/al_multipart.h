/*
 * al_multipart.h - Dynamic array list of HTTP_Service_Multipart_Node values for the C Libraries Framework
 *
 * Features:
 *   - Dynamic, growable array of HTTP_Service_Multipart_Node values
 *   - Arena or heap allocation support
 *   - Add, remove, access, and manage HTTP_Service_Multipart_Node elements efficiently
 *
 * Family:
 *   One of the hand-cloned typed array lists of container/arrayList; al_u64 is
 *   the canonical instantiation and tools/al_divergence.py measures each
 *   file's divergence from it. This instantiation stores
 *   HTTP_Service_Multipart_Node BY VALUE and owns none of its pointers (see
 *   Memory Management below); add takes a pointer and refuses an own-list
 *   element; it lives beside its only consumer in http/service/multipart
 *   rather than in container/arrayList.
 *
 * Usage Examples:
 *   @code
 *   AL_MultiPart list = al_multipart_init_1();
 *   al_multipart_add_last(&list, &some_http_service_multipart_part);
 *   HTTP_Service_Multipart_Node *node = al_multipart_at(&list, 0);
 *   al_multipart_uninit(&list);
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
 *     - a null `data` argument to add/add_first/add_last is legal and stores a
 *       default-initialized element; the list never releases a node's fields,
 *       filled or not (see Memory Management).
 *
 * Thread Safety:
 *   Not thread-safe. Caller must synchronize if used from multiple threads.
 *
 * Memory Management:
 *   - The list owns only its backing buffer. A node's name, filename, content_type and data
 *     pointers are borrowed and stay the caller's; al_multipart_uninit releases the buffer only.
 *
 * Performance Characteristics:
 *   - Amortized O(1) append; O(n) insert/remove at an arbitrary index; O(1) indexed access.
 *
 * Dependencies:
 *   - <allocator/allocator.h>
 *
 * See al_multipart.c for implementation details.
 */

#ifndef HTTP_SERVICE_MULTIPART_AL_MULTIPART_H
#define HTTP_SERVICE_MULTIPART_AL_MULTIPART_H

#include <allocator/allocator.h>

/*
 * WHY THE TYPEDEF LIVES IN AN al_* HEADER PAIR.
 *
 * HTTP_Service_Multipart_Node is a behaviourless five-field record with no
 * module of its own, and multipart.h already includes this header - so the
 * record lives here rather than in a header of its own.
 */

/**
 * @brief Represents a single part of a multipart/form-data payload.
 */
typedef struct {
    char const *name;
    char const *filename;
    char const *content_type;
    Byte const *data;
    USize size;
} HTTP_Service_Multipart_Node;

/*
 * Ownership: this list stores nodes BY VALUE and owns none of their pointers.
 * name, filename, and content_type are allocated by the multipart service and
 * released by http_service_multipart_uninit; data points into the caller's
 * payload buffer and is never released at all. clear() and remove() therefore
 * zero a slot without freeing anything - the container cannot tell an owned
 * field from a borrowed one, so the producer stays responsible for both.
 *
 * CONSEQUENCE: never call al_multipart_remove or al_multipart_clear on a list
 * that http_service_multipart_parse filled. The service releases the strings by
 * walking the CURRENT list in http_service_multipart_uninit, so anything dropped
 * from the list first is unreachable and leaks. Uninit the service instead; if a
 * per-part removal is ever needed, it belongs in the service, which knows what
 * to release.
 */

/**
 * @brief Dynamic array list of HTTP_Service_Multipart_Node values.
 */
typedef struct {
#ifdef ARENA_IMPLEMENTATION
    Arena *allocator; /**< Arena allocator pointer (if used) */
#endif // ARENA_IMPLEMENTATION
    USize capacity;   /**< Allocated capacity */
    HTTP_Service_Multipart_Node *data; /**< Array of HTTP_Service_Multipart_Node values */
    USize size;       /**< Number of elements */
} AL_MultiPart;

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Initialize an empty array list with arena allocator.
 * @param allocator Arena pointer.
 * @return Initialized AL_MultiPart.
 */
AL_MultiPart al_multipart_alloc_init_1(Arena *allocator);

/**
 * @brief Initialize array list with capacity and arena allocator.
 * @param capacity Initial capacity.
 * @param allocator Arena pointer.
 * @return Initialized AL_MultiPart.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
AL_MultiPart al_multipart_alloc_init_2(USize const capacity, Arena *allocator);

/**
 * @brief Initialize array list from data and size with arena allocator.
 * @param data Array of HTTP_Service_Multipart_Node values.
 * @param data_size Number of elements.
 * @param allocator Arena pointer.
 * @return Initialized AL_MultiPart.
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
AL_MultiPart al_multipart_alloc_init_3(HTTP_Service_Multipart_Node const *const data, USize const data_size, Arena *allocator);

/**
 * @brief Allocate a new AL_MultiPart on the arena.
 * @param allocator Arena pointer.
 * @return Pointer to new AL_MultiPart.
 */
AL_MultiPart* al_multipart_alloc_new_1(Arena *allocator);

/**
 * @brief Allocate a new AL_MultiPart with capacity on the arena.
 * @param capacity Initial capacity.
 * @param allocator Arena pointer.
 * @return Pointer to new AL_MultiPart.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
AL_MultiPart* al_multipart_alloc_new_2(USize const capacity, Arena *allocator);

/**
 * @brief Allocate a new AL_MultiPart from data and size on the arena.
 * @param data Array of HTTP_Service_Multipart_Node values.
 * @param data_size Number of elements.
 * @param allocator Arena pointer.
 * @return Pointer to new AL_MultiPart.
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
AL_MultiPart* al_multipart_alloc_new_3(HTTP_Service_Multipart_Node const *const data, USize const data_size, Arena *allocator);
#endif // ARENA_IMPLEMENTATION

/**
 * @brief Add a HTTP_Service_Multipart_Node value (copied into a slot the list owns) at the specified index.
 * @param self Pointer to AL_MultiPart.
 * @param data Pointer to the HTTP_Service_Multipart_Node to copy.
 * @param index Index to insert at.
 * @note Declines silently when the allocator refuses the growth (a refused
 *         arena, or a capacity whose byte size would wrap). The list is left
 *         unchanged, so a caller that must know checks the size afterwards.
 * @note `data` is a POINTER here, not a value: passing nullptr is legal and
 *         stores a default-initialized element rather than refusing. That is how
 *         a caller reserves a slot to fill in later; it is not an error path, and
 *         no slot is released by remove/clear, filled or not.
 * @note A `data` that is one of self's OWN elements (a pointer into this list's storage) is
 *         REFUSED as a no-op, in every build. This list owns NONE of a node's pointers -
 *         name/filename/content_type/data all stay the caller's (see Memory Management above) -
 *         so the risk here is not double ownership; it is that the growth below may move or
 *         release the buffer the source lives in before the shift copies it, turning the read
 *         into a use-after-free. Copy the node out first if a duplicate is really wanted.
 */
void al_multipart_add(AL_MultiPart *const self, HTTP_Service_Multipart_Node const *const data, USize const index);

/**
 * @brief Add a HTTP_Service_Multipart_Node value (copied into a slot the list owns) at the beginning.
 * @param self Pointer to AL_MultiPart.
 * @param data Pointer to the HTTP_Service_Multipart_Node to copy.
 * @note Declines silently when the allocator refuses the growth (a refused
 *         arena, or a capacity whose byte size would wrap). The list is left
 *         unchanged, so a caller that must know checks the size afterwards.
 * @note `data` is a POINTER here, not a value: passing nullptr is legal and
 *         stores a default-initialized element rather than refusing. That is how
 *         a caller reserves a slot to fill in later; it is not an error path, and
 *         no slot is released by remove/clear, filled or not.
 * @note A `data` that is one of self's OWN elements (a pointer into this list's storage) is
 *         REFUSED as a no-op, in every build. This list owns NONE of a node's pointers -
 *         name/filename/content_type/data all stay the caller's (see Memory Management above) -
 *         so the risk here is not double ownership; it is that the growth below may move or
 *         release the buffer the source lives in before the shift copies it, turning the read
 *         into a use-after-free. Copy the node out first if a duplicate is really wanted.
 */
void al_multipart_add_first(AL_MultiPart *const self, HTTP_Service_Multipart_Node const *const data);

/**
 * @brief Add a HTTP_Service_Multipart_Node value (copied into a slot the list owns) at the end.
 * @param self Pointer to AL_MultiPart.
 * @param data Pointer to the HTTP_Service_Multipart_Node to copy.
 * @note Declines silently when the allocator refuses the growth (a refused
 *         arena, or a capacity whose byte size would wrap). The list is left
 *         unchanged, so a caller that must know checks the size afterwards.
 * @note `data` is a POINTER here, not a value: passing nullptr is legal and
 *         stores a default-initialized element rather than refusing. That is how
 *         a caller reserves a slot to fill in later; it is not an error path, and
 *         no slot is released by remove/clear, filled or not.
 * @note A `data` that is one of self's OWN elements (a pointer into this list's storage) is
 *         REFUSED as a no-op, in every build. This list owns NONE of a node's pointers -
 *         name/filename/content_type/data all stay the caller's (see Memory Management above) -
 *         so the risk here is not double ownership; it is that the growth below may move or
 *         release the buffer the source lives in before the shift copies it, turning the read
 *         into a use-after-free. Copy the node out first if a duplicate is really wanted.
 */
void al_multipart_add_last(AL_MultiPart *const self, HTTP_Service_Multipart_Node const *const data);

/**
 * @brief Get the address of the HTTP_Service_Multipart_Node at the specified index.
 * @param self Pointer to AL_MultiPart.
 * @param index Index to access.
 * @return Address of the HTTP_Service_Multipart_Node at index.
 * @note Bounded by size, not capacity. The slots a clear() leaves inside the
 *         retained capacity are out of contract - reading one is a caller error,
 *         not a way to inspect a released element.
 * @note The address is valid only until the next add/add_first/add_last/reserve/shrink: any growth may move the backing array and invalidate it.
 */
HTTP_Service_Multipart_Node* al_multipart_at(AL_MultiPart const *const self, USize const index);

/**
 * @brief Get the address of the last HTTP_Service_Multipart_Node in the list.
 * @param self Pointer to AL_MultiPart.
 * @return Address of the last HTTP_Service_Multipart_Node.
 * @note Answers nullptr on an empty list rather than aborting: emptiness is a
 *         data question, not a broken contract.
 * @note The address is valid only until the next add/add_first/add_last/reserve/shrink: any growth may move the backing array and invalidate it.
 */
HTTP_Service_Multipart_Node* al_multipart_back(AL_MultiPart const *const self);

/**
 * @brief Remove all elements from the list.
 * @param self Pointer to AL_MultiPart.
 */
void al_multipart_clear(AL_MultiPart *const self);

/**
 * @brief Delete and free the list.
 * @param self Address of AL_MultiPart pointer.
 */
void al_multipart_delete(AL_MultiPart **const self);

/**
 * @brief Check if the list is empty.
 * @param self Pointer to AL_MultiPart.
 * @return true if empty, false otherwise.
 */
bool al_multipart_empty(AL_MultiPart const *const self);

/**
 * @brief Get the address of the first HTTP_Service_Multipart_Node in the list.
 * @param self Pointer to AL_MultiPart.
 * @return Address of the first HTTP_Service_Multipart_Node.
 * @note Answers nullptr on an empty list rather than aborting: emptiness is a
 *         data question, not a broken contract.
 * @note The address is valid only until the next add/add_first/add_last/reserve/shrink: any growth may move the backing array and invalidate it.
 */
HTTP_Service_Multipart_Node* al_multipart_front(AL_MultiPart const *const self);

/**
 * @brief Get the element capacity.
 * @param self Pointer to AL_MultiPart.
 * @return The number of elements the list can hold before it must grow.
 */
USize al_multipart_get_capacity(AL_MultiPart const *const self);

/**
 * @brief Get the data array pointer.
 * @param self Pointer to AL_MultiPart.
 * @return Pointer to data array.
 */
HTTP_Service_Multipart_Node* al_multipart_get_data(AL_MultiPart const *const self);

/**
 * @brief Get the element count.
 * @param self Pointer to AL_MultiPart.
 * @return The number of elements currently stored.
 */
USize al_multipart_get_size(AL_MultiPart const *const self);

/**
 * @brief Initialize an empty array list.
 * @return Initialized AL_MultiPart.
 */
AL_MultiPart al_multipart_init_1(void);

/**
 * @brief Initialize array list with capacity.
 * @param capacity Initial capacity.
 * @return Initialized AL_MultiPart.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
AL_MultiPart al_multipart_init_2(USize const capacity);

/**
 * @brief Initialize array list from data and size.
 * @param data Array of HTTP_Service_Multipart_Node values.
 * @param data_size Number of elements.
 * @return Initialized AL_MultiPart.
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
AL_MultiPart al_multipart_init_3(HTTP_Service_Multipart_Node const *const data, USize const data_size);

/**
 * @brief Allocate a new AL_MultiPart on the heap.
 * @return Pointer to new AL_MultiPart.
 */
AL_MultiPart* al_multipart_new_1(void);

/**
 * @brief Allocate a new AL_MultiPart with capacity on the heap.
 * @param capacity Initial capacity.
 * @return Pointer to new AL_MultiPart.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
AL_MultiPart* al_multipart_new_2(USize const capacity);

/**
 * @brief Allocate a new AL_MultiPart from data and size on the heap.
 * @param data Array of HTTP_Service_Multipart_Node values.
 * @param data_size Number of elements.
 * @return Pointer to new AL_MultiPart.
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
AL_MultiPart* al_multipart_new_3(HTTP_Service_Multipart_Node const *const data, USize const data_size);

/**
 * @brief Remove the HTTP_Service_Multipart_Node value at the specified index.
 * @param self Pointer to AL_MultiPart.
 * @param index Index to remove.
 */
void al_multipart_remove(AL_MultiPart *const self, USize const index);

/**
 * @brief Remove the first HTTP_Service_Multipart_Node value in the list.
 * @param self Pointer to AL_MultiPart.
 * @note A call on an empty list is a silent no-op (size stays 0).
 */
void al_multipart_remove_first(AL_MultiPart *const self);

/**
 * @brief Remove the last HTTP_Service_Multipart_Node value in the list.
 * @param self Pointer to AL_MultiPart.
 * @note A call on an empty list is a silent no-op (size stays 0).
 */
void al_multipart_remove_last(AL_MultiPart *const self);

/**
 * @brief Reserve capacity for the list.
 * @param self Pointer to AL_MultiPart.
 * @param capacity New capacity.
 * @note capacity and data_size are CALLER contracts: zero, or a value large
 *         enough to wrap the byte size, is checked as a programming error. Never
 *         pass an unvalidated remote length here - validate it first, or the
 *         check becomes a remote abort.
 */
void al_multipart_reserve(AL_MultiPart *const self, USize const capacity);

/**
 * @brief Shrink the list to fit its size.
 * @param self Pointer to AL_MultiPart.
 */
void al_multipart_shrink(AL_MultiPart *const self);

/**
 * @brief Release all memory and reset the list.
 * @param self Pointer to AL_MultiPart.
 * @note Idempotent in every build. The freed pointer is cleared unconditionally
 *         rather than under MEMORY_NON_DANGLING_POINTER, so a second uninit cannot
 *         hand a released block back to the allocator.
 */
void al_multipart_uninit(AL_MultiPart *const self);

#endif // HTTP_SERVICE_MULTIPART_AL_MULTIPART_H