/*
 * arena_stack.h - Stack arena allocator for the C Libraries Framework
 *
 * Features:
 *   - Fast LIFO allocation and deallocation from a preallocated buffer
 *   - API-compatible with arena interface (see arena.h)
 *
 * Usage Examples:
 *   @code
 *   ArenaStack *stack = arena_stack_new(1024);
 *   void *block = arena_stack_alloc(stack, 128);
 *   bool empty = arena_stack_empty(stack);
 *   arena_stack_free(stack, block);
 *   arena_stack_delete(&stack);
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
 *   arena_stack_new() owns one allocation; arena_stack_free() pops the most recent
 *   block; arena_stack_delete() releases the whole arena at once.
 *
 * Performance Characteristics:
 *   Allocation and free are O(1).
 *
 * Dependencies:
 *   - <memory/memory.h> for allocation and the framework types.
 *
 * See arena_stack.c for implementation details.
 */

#ifndef ARENA_STACK_H
#define ARENA_STACK_H

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/

#include <memory/memory.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

/* Must stay equal to the implementation's _ARENA_STACK_HEADER_SIZE. Callers size an arena with
 * this (main.c:124 budgets ARENA_STACK_HEADER_SIZE * 4), so a value smaller than what each block
 * actually consumes silently undersizes the arena, and allocator_borrow starts returning nullptr
 * into consumers that do not check it. */
#define ARENA_STACK_HEADER_SIZE MEMORY_ALIGN_UP(sizeof(USize))

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief Opaque struct for stack arena allocator.
 */
typedef struct ArenaStack ArenaStack;

/*==============================================================================
 * MARK: - API
 *============================================================================*/

/**
 * @brief Allocate a block from the stack.
 * @param self Pointer to the stack.
 * @param byte_count Number of bytes to allocate.
 * @return Pointer to allocated memory, or nullptr on failure.
 */
void* arena_stack_alloc(ArenaStack *const self, USize const byte_count);

/**
 * @brief Delete the stack and free all memory.
 * @param self Address of stack pointer. Set to nullptr after deletion.
 */
void arena_stack_delete(ArenaStack **const self);

/**
 * @brief Check if the stack is empty (no allocations made).
 * @param self Pointer to the stack.
 * @return true if empty, false otherwise.
 */
bool arena_stack_empty(ArenaStack const *const self);

/**
 * @brief Free the most recently allocated block from the stack.
 * @param self Pointer to the stack.
 * @param buffer Block to release. This arena is LIFO, so only the most recent block
 *        can be popped: any other pointer is refused and nothing is released. A nullptr
 *        buffer is ignored, as with free(NULL) - it does NOT pop the top block.
 * @note The parameter exists so the function matches FpArenaFree. Without it, arena.c
 *       cast a one-parameter function to the two-parameter pointer type - undefined
 *       behaviour, and the discarded pointer meant an out-of-order release silently
 *       popped an unrelated live block.
 * @note CAVEAT: after a pop the same address is handed to the NEXT allocation,
 *       so a stale second release of an old pointer can match the new owner's
 *       block exactly - it pops and zeroes a LIVE block. Out-of-order releases
 *       are refused; same-address reuse cannot be.
 */
void arena_stack_free(ArenaStack *const self, void *const buffer);

/**
 * @brief Create a new stack with the given capacity.
 * @param capacity Total number of bytes to preallocate.
 * @return Pointer to new stack, or nullptr on failure.
 */
ArenaStack* arena_stack_new(USize const capacity);

/**
 * @brief Allocate from the stack arena without ever aborting.
 *
 * The non-aborting twin of arena_stack_alloc: exhaustion, a zero byte_count and
 * a null arena all yield nullptr instead of ending the process. Use it whenever
 * the requested size derives from outside input; see arena_linear_try_alloc.
 *
 * @param self       Arena to allocate from; nullptr yields nullptr.
 * @param byte_count Bytes to allocate; 0 yields nullptr.
 * @return Pointer to the block, or nullptr when it cannot be satisfied.
 */
void* arena_stack_try_alloc(ArenaStack *const self, USize const byte_count);

#endif // ARENA_STACK_H