/*
 * arena_linear.h - Linear arena allocator for the C Libraries Framework
 *
 * Features:
 *   - Fast, linear allocation from a preallocated buffer
 *   - No per-allocation metadata or fragmentation
 *   - API-compatible with arena interface (see arena.h)
 *
 * Usage Examples:
 *   @code
 *   ArenaLinear *arena = arena_linear_new(1024);
 *   void *block1 = arena_linear_alloc(arena, 128);
 *   void *block2 = arena_linear_alloc(arena, 256);
 *   arena_linear_clear(arena);
 *   bool empty = arena_linear_empty(arena);
 *   arena_linear_delete(&arena);
 *   @endcode
 *
 * Error Handling:
 *   - Functions validate non-null inputs and non-zero sizes through the framework
 *     error system.
 *
 * Thread Safety:
 *   - Not thread-safe. Caller must synchronize shared arena access.
 *
 * Memory Management:
 *   - arena_linear_new() owns one allocation containing metadata and data.
 *   - arena_linear_clear() zeroes the data buffer and resets the allocation offset.
 *   - arena_linear_delete() releases the whole arena at once.
 *   - arena_linear_free() is a no-op for allocator-interface compatibility.
 *
 * Performance Characteristics:
 *   - Allocation is O(1).
 *   - Individual frees are no-ops.
 *   - No per-block metadata is stored.
 *
 * Dependencies:
 *   - <memory/memory.h> for allocation and the framework types.
 *
 * See arena_linear.c for implementation details.
 */

#ifndef ARENA_LINEAR_H
#define ARENA_LINEAR_H

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/

#include <memory/memory.h>

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief Opaque linear arena allocator.
 *
 * The implementation stores one contiguous memory buffer and advances an internal
 * offset on each allocation.
 */
typedef struct ArenaLinear ArenaLinear;

/*==============================================================================
 * MARK: - API
 *============================================================================*/

/**
 * @brief Allocate a block of memory from the linear arena.
 * @param self Arena instance.
 * @param byte_count Number of bytes to allocate.
 * @return Pointer to allocated memory. Under ERROR_CHECK_ENABLED this ABORTS
 *         when the request cannot be satisfied - exhaustion included, and
 *         byte_count values large enough to overflow the alignment round-up.
 *         Use arena_linear_try_alloc for the recovering variant.
 * @note Individual blocks cannot be freed.
 */
void* arena_linear_alloc(ArenaLinear *const self, USize const byte_count);

/**
 * @brief Clear all arena data and reset the allocation offset.
 * @param self Arena instance.
 */
void arena_linear_clear(ArenaLinear *const self);

/**
 * @brief Delete the linear arena and free all memory.
 * @param self Address of arena pointer.
 */
void arena_linear_delete(ArenaLinear **const self);

/**
 * @brief Check whether no bytes have been allocated from the arena.
 * @param self Arena instance.
 * @return true when no allocations were made.
 */
bool arena_linear_empty(ArenaLinear const *const self);

/**
 * @brief No-op free function for allocator-interface compatibility.
 * @param self Arena instance.
 * @param buffer Ignored block pointer.
 */
void arena_linear_free(ArenaLinear *const self, void *const buffer);

/**
 * @brief Create a new linear arena with the given capacity.
 * @param capacity Number of bytes to preallocate.
 * @return Pointer to new arena, or nullptr on failure.
 */
ArenaLinear* arena_linear_new(USize const capacity);

/**
 * @brief Allocate from the arena without ever aborting.
 *
 * The non-aborting twin of arena_linear_alloc: exhaustion, a zero byte_count and
 * a null arena are all reported as nullptr instead of ending the process. Use
 * this wherever the requested size comes from outside the program - a request
 * body, a header, a file - so that input sized to drain the arena degrades into
 * a rejected request rather than a dead process. arena_linear_alloc keeps the
 * stricter contract for sizes the program itself chose.
 *
 * @param self       Arena to allocate from; nullptr yields nullptr.
 * @param byte_count Bytes to allocate; 0 yields nullptr.
 * @return Pointer to the block, or nullptr when it cannot be satisfied.
 */
void* arena_linear_try_alloc(ArenaLinear *const self, USize const byte_count);

#endif // ARENA_LINEAR_H