/*
 * arena_pool.h - Pool arena allocator for the C Libraries Framework
 *
 * Features:
 *   - Fast, fixed-size block allocation from a preallocated pool
 *   - Efficient reuse of freed blocks via a free list
 *   - API-compatible with arena interface (see arena.h)
 *
 * Usage Examples:
 *   @code
 *   ArenaPool *pool = arena_pool_new(64, 128);
 *   void *block = arena_pool_alloc(pool, 1);
 *   bool empty = arena_pool_empty(pool);
 *   arena_pool_delete(&pool);
 *   @endcode
 *
 * Error Handling:
 *   All functions check for null pointers and invalid arguments. If invalid, the
 *   function returns early and logs an error.
 *
 * Thread Safety:
 *   Not thread-safe. Caller must synchronize if used from multiple threads.
 *
 * Memory Management:
 *   arena_pool_new() owns one allocation holding metadata, the free list, and the
 *   block storage; arena_pool_delete() releases it all at once.
 *
 * Performance Characteristics:
 *   Allocation and free scan the free list, which is linear in the block count.
 *
 * Dependencies:
 *   - <memory/memory.h> for allocation and the framework types.
 *
 * See arena_pool.c for implementation details.
 */

#ifndef ARENA_POOL_H
#define ARENA_POOL_H

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/

#include <memory/memory.h>

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief Opaque struct for pool arena allocator.
 */
typedef struct ArenaPool ArenaPool;

/*==============================================================================
 * MARK: - API
 *============================================================================*/

/**
 * @brief Allocate one or more blocks from the pool.
 * @param self Pointer to the pool.
 * @param block_count Number of blocks to allocate.
 * @return Pointer to allocated memory, or nullptr on failure.
 */
void* arena_pool_alloc(ArenaPool *const self, USize const block_count);

/**
 * @brief BYTE-denominated aborting allocation: ceil(byte_count / block size) blocks.
 *        The Arena facade's adapter, so the facade means bytes for every arena type;
 *        direct callers use the block-denominated arena_pool_alloc.
 * @param self Pool arena.
 * @param byte_count Bytes requested (rounded up to whole blocks).
 * @return Block-aligned buffer; aborts on exhaustion like arena_pool_alloc.
 */
void* arena_pool_alloc_bytes(ArenaPool *const self, USize const byte_count);

/**
 * @brief Delete the pool and free all memory.
 * @param self Address of pool pointer. Set to nullptr after deletion.
 */
void arena_pool_delete(ArenaPool **const self);

/**
 * @brief Check if the pool is empty (no allocations made).
 * @param self Pointer to the pool.
 * @return true if empty, false otherwise.
 */
bool arena_pool_empty(ArenaPool const *const self);

/**
 * @brief Free a block back to the pool.
 * @param self Pointer to the pool.
 * @param buffer Pointer to the block to free.
 */
void arena_pool_free(ArenaPool *const self, void *const buffer);

/**
 * @brief Create a new pool with the given block size and count.
 * @param block_size Size of each block in bytes.
 * @param block_count Number of blocks to preallocate.
 * @return Pointer to new pool, or nullptr on failure.
 */
ArenaPool* arena_pool_new(USize const block_size, USize const block_count);

/**
 * @brief Allocate blocks without ever aborting.
 *
 * The non-aborting twin of arena_pool_alloc: exhaustion, a zero block_count and
 * a null arena all yield nullptr instead of ending the process. Use it whenever
 * the requested count derives from outside input; see arena_linear_try_alloc.
 *
 * @param self        Arena to allocate from; nullptr yields nullptr.
 * @param block_count Blocks to allocate; 0 yields nullptr.
 * @return Pointer to the first block, or nullptr when it cannot be satisfied.
 */
void* arena_pool_try_alloc(ArenaPool *const self, USize const block_count);

/**
 * @brief BYTE-denominated recovering allocation: ceil(byte_count / block size) blocks.
 *        See arena_pool_alloc_bytes; nullptr on refusal, never aborts.
 * @param self Pool arena.
 * @param byte_count Bytes requested (rounded up to whole blocks).
 * @return Block-aligned buffer, or nullptr.
 */
void* arena_pool_try_alloc_bytes(ArenaPool *const self, USize const byte_count);

#endif // ARENA_POOL_H