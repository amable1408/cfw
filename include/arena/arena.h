/*
 * arena.h - Canonical arena interface for the C Libraries Framework
 *
 * Features:
 *   - Abstracts multiple arena allocation strategies (linear, stack, pool)
 *   - Uses function pointer hooks for allocation/deallocation
 *   - Interface/prototype types and function pointer typedefs follow strict naming conventions
 *
 * Usage Examples:
 *   @code
 *   Arena arena = arena_init_1(1024, ARENA_TYPE_LINEAR);
 *   void *block = arena.allocate(arena.handler, 128);
 *   arena.deallocate(arena.handler, block);
 *   arena_uninit(&arena, ARENA_TYPE_LINEAR);
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
 *   arena_init_* allocates the backing handler; arena_uninit frees it and (with
 *   MEMORY_NON_DANGLING_POINTER) nulls the hooks and handler.
 *
 * Performance Characteristics:
 *   Dispatch is a single indirect call into the selected strategy.
 *
 * Dependencies:
 *   - The arena strategy headers under arena and the framework types.
 *
 * See arena.c for implementation details.
 */

#ifndef ARENA_H
#define ARENA_H

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/

#include <arena/arena_linear.h>
#include <arena/arena_pool.h>
#include <arena/arena_stack.h>

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief Arena type selector for allocation strategy.
 */
typedef enum {
    ARENA_TYPE_LINEAR,
    ARENA_TYPE_STACK,
    ARENA_TYPE_POOL
} ArenaType;

/**
 * @brief Type Prototype for arena handler (interface for all arena types).
 *
 * An OPAQUE handle: it only ever crosses the seam as a pointer, no _vp_ slot is
 * read through this type, and its size is NOT a layout contract implementations
 * must mirror (ArenaPool carries more fields and nothing breaks).
 */
typedef struct {
    void *_vp_0;
    void *_vp_1;
    void *_vp_2;
    void *_vp_3;
    void *_vp_4;
} TpArena;

/**
 * @brief Function pointer for arena allocation (Function Prototype).
 */
typedef void* (*FpArenaAlloc)(TpArena *const self, USize const byte_count);

/**
 * @brief Function pointer for arena deallocation (Function Prototype).
 */
typedef void (*FpArenaFree)(TpArena *const self, void *const buffer);

/**
 * @brief Arena object (strategy + handler + hooks).
 *
 * `allocate` treats exhaustion as a programmer error and ends the process;
 * `try_allocate` reports it as nullptr. Reach for try_allocate (through
 * allocator_try_borrow) whenever the requested size comes from outside the
 * program, so that hostile input degrades into a rejection instead of a crash.
 */
typedef struct {
    FpArenaAlloc allocate;
    FpArenaFree  deallocate;
    TpArena      *handler;
    FpArenaAlloc try_allocate;
} Arena;

/*==============================================================================
 * MARK: - API
 *============================================================================*/

/* Gated with the definitions: an ARENA_IMPLEMENTATION-less build must fail at
 * COMPILE time at the call site, not fall off the end of an empty non-void
 * body at run time (allocator.h precedent). */
#ifdef ARENA_IMPLEMENTATION

/**
 * @brief Initialize an Arena with a given total byte count and arena type.
 *
 * Allocates and sets up the arena handler and function pointers for the selected strategy.
 *
 * @param byte_count Total number of bytes to allocate for the arena.
 * @param arena_type The type of arena to create (ARENA_TYPE_LINEAR, ARENA_TYPE_POOL, ARENA_TYPE_STACK).
 * @return Arena object with initialized handler and hooks.
 * @note Checks for invalid arguments. Not thread-safe.
 */
Arena arena_init_1(USize const byte_count, ArenaType const arena_type);

/**
 * @brief Initialize an Arena with element size, element count, and arena type.
 *
 * Allocates and sets up the arena handler and function pointers for the selected strategy.
 *
 * @param byte_size Size of each element (in bytes).
 * @param byte_count Number of elements to allocate.
 * @param arena_type The type of arena to create (ARENA_TYPE_LINEAR, ARENA_TYPE_POOL, ARENA_TYPE_STACK).
 * @return Arena object with initialized handler and hooks.
 * @note Checks for invalid arguments. Not thread-safe.
 */
Arena arena_init_2(USize const byte_size, USize const byte_count, ArenaType const arena_type);

/**
 * @brief Clear and delete the arena handler for the given arena type.
 *
 * Frees all memory associated with the arena's handler (linear, pool, or stack).
 * After this call, the arena's handler pointer is invalid and should not be used.
 *
 * @param self Pointer to the Arena object.
 * @param arena_type The type of arena to clear (ARENA_TYPE_LINEAR, ARENA_TYPE_POOL, ARENA_TYPE_STACK).
 * @note Checks for null pointers and invalid arguments. Not thread-safe.
 */
void arena_uninit(Arena *const self, ArenaType const arena_type);
#endif // ARENA_IMPLEMENTATION

#endif // ARENA_H