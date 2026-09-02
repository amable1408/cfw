/*
 * allocator.h - Canonical allocator interface for the C Libraries Framework
 *
 * Features:
 *   - Abstracts allocation and deallocation for containers and arenas
 *   - Supports both arena-backed and standard heap allocation
 *
 * Usage Examples:
 *   @code
 *   void *block = allocator_borrow(128, &arena);
 *   allocator_release(block, &arena);
 *   @endcode
 *
 * Error Handling:
 *   All functions check for null pointers and invalid arguments. If invalid, the
 *   function returns early and logs an error. A null allocate/deallocate hook is
 *   detected and the call becomes a safe no-op (borrow returns nullptr).
 *
 * Thread Safety:
 *   Not thread-safe. Caller must synchronize if used from multiple threads.
 *
 * Memory Management:
 *   With ARENA_IMPLEMENTATION and a non-null arena, memory is owned by the arena;
 *   otherwise it is heap memory the caller must release with allocator_release.
 *
 * Performance Characteristics:
 *   Dispatch is a single branch plus the backing allocator's own cost.
 *
 * Dependencies:
 *   - <arena/arena.h> for the Arena interface and framework types.
 *
 * See allocator.c for implementation details.
 */

#ifndef ALLOCATOR_ALLOCATOR_H
#define ALLOCATOR_ALLOCATOR_H

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/

#include <arena/arena.h>

/*==============================================================================
 * MARK: - API
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Borrow a block of memory from the allocator (arena or heap).
 * @param byte_count Number of bytes to allocate.
 * @param allocator Arena pointer (optional, can be nullptr for heap).
 * @return Pointer to allocated memory, or nullptr on failure.
 * @note A null HANDLER - the shape a refused arena_init_* leaves behind - is
 *       treated exactly like a null hook: borrow returns nullptr, release is a
 *       no-op, nothing aborts.
 */
void* allocator_borrow(USize const byte_count, Arena *const allocator);
#else
void* allocator_borrow(USize const byte_count);
#endif // ARENA_IMPLEMENTATION

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Release a block of memory to the allocator (arena or heap).
 * @param buffer Pointer to memory to release.
 * @param allocator Arena pointer (optional, can be nullptr for heap).
 */
void allocator_release(void *const buffer, Arena *const allocator);
#else
void allocator_release(void *const buffer);
#endif // ARENA_IMPLEMENTATION

#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Borrow a block without ever aborting.
 *
 * The non-aborting twin of allocator_borrow. allocator_borrow ends the process
 * when the request cannot be met - an exhausted arena trips an error check, and
 * the heap path goes through the aborting memory_alloc - which is the right
 * contract for sizes the program itself chose, and the wrong one for sizes that
 * arrived from outside it. Use this for any size derived from a request body, a
 * header, a file or any other external input, so that input crafted to drain the
 * allocator degrades into a rejected operation rather than a dead process.
 *
 * @param byte_count Number of bytes to allocate; 0 yields nullptr.
 * @param allocator  Arena pointer, or nullptr to use the heap.
 * @return Pointer to allocated memory, or nullptr when it cannot be satisfied.
 */
void* allocator_try_borrow(USize const byte_count, Arena *const allocator);
#else
void* allocator_try_borrow(USize const byte_count);
#endif // ARENA_IMPLEMENTATION

#endif // ALLOCATOR_ALLOCATOR_H