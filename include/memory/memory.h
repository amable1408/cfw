/*
 * memory.h - Canonical memory management utilities for the C Libraries Framework
 *
 * Features:
 *   - Safe, flexible memory allocation, copying, and zeroing
 *   - Macros for common memory sizes (KB, MB, GB, TB)
 *   - Error-checked, metadata-rich logging for all operations
 *   - Integration with the error and math modules
 *
 * Usage Examples:
 *   @code
 *   void *buffer = memory_alloc(128);
 *   memory_set(buffer, 128, 0xFF);
 *   memory_free(buffer);
 *   @endcode
 *
 * Error Handling:
 *   All functions check for null pointers and invalid arguments. If invalid, the
 *   function returns early and logs an error.
 *
 * Thread Safety:
 *   All functions are thread-safe if the underlying allocator is thread-safe.
 *   The interception seam for instrumentation and fault injection is the
 *   LINKER (-Wl,--wrap=calloc / --wrap=realloc), not a hooks API.
 *
 * Memory Management:
 *   memory_alloc and memory_realloc return owned heap memory the caller must
 *   release with memory_free or memory_delete. memory_delete nulls the caller's
 *   pointer when MEMORY_NON_DANGLING_POINTER is defined.
 *
 * Performance Characteristics:
 *   Allocation and copy operations defer to the C runtime; memory_fit_size is
 *   logarithmic in the requested size.
 *
 * Dependencies:
 *   - <error/error.h> and <tracelog/tracelog.h> for checks and tracing.
 *   - <math/math.h>: the whole math umbrella, by RULING (2026-08-27), not by need. memory.{h,c}
 *     call no math_* function, and the integer types arrive through the error -> tracelog ->
 *     log -> console chain regardless. Severing this to <math/scalar.h> was measured at a
 *     two-module cost and REJECTED in favour of shipping math whole; the include stays.
 *   - <stddef.h> for max_align_t, which MEMORY_ALIGNMENT is defined from.
 *
 * See memory.c for implementation details.
 */

#ifndef MEMORY_H
#define MEMORY_H

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/

// max_align_t for MEMORY_ALIGNMENT. Included directly rather than relied on transitively: a
// silently wrong alignment constant is a correctness failure with no diagnostic.
#include <stddef.h>

#include <error/error.h>
#include <math/math.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

/**
 * @def MEMORY_ALIGNMENT
 * @brief Strictest fundamental alignment on this target, i.e. what memory_alloc already
 *        returns. A sub-allocator that hands out slices of one memory_alloc block must round
 *        every slice to this, or the first odd-sized slice misaligns every later one.
 */
#define MEMORY_ALIGNMENT _Alignof(max_align_t)

/**
 * @def MEMORY_ALIGN_UP
 * @brief Round a byte count up to the next MEMORY_ALIGNMENT boundary.
 * @param byte_count Size to round. EVALUATED TWICE - never pass a side-effecting expression.
 * @note Wraps on overflow, returning a value SMALLER than byte_count (0 at the extreme). A
 *       caller that aligns an untrusted size must reject `result < byte_count`, or it will pass
 *       a bound check it should have failed. All three arenas pair it with exactly that test.
 */
#define MEMORY_ALIGN_UP(byte_count) (((byte_count) + (MEMORY_ALIGNMENT - 1)) & ~(MEMORY_ALIGNMENT - 1))

#define MEMORY_SIZE_GB (MEMORY_SIZE_MB * 1024ULL)
#define MEMORY_SIZE_KB (1024ULL)
#define MEMORY_SIZE_MB (MEMORY_SIZE_KB * 1024ULL)
#define MEMORY_SIZE_TB (MEMORY_SIZE_GB * 1024ULL)

/*==============================================================================
 * MARK: - API
 *============================================================================*/

/**
 * @brief Allocate a block of memory.
 * @param byte_count Number of bytes to allocate.
 * @return Pointer to allocated memory; under ERROR_CHECK_ENABLED this ABORTS on allocation
 *         failure and on byte_count == 0 (nullptr returns happen only in unchecked builds).
 *         Use memory_try_alloc for any size derived from outside the program.
 * @note ALWAYS zeroed. This is a framework guarantee rather than a build option, so a caller
 *       may rely on a fresh allocation reading as all zero without writing it. It does NOT
 *       extend to a block that has since been written and reused - stale bytes there are the
 *       caller's own, and a terminator over a shorter rewrite still has to be written.
 * @see memory_free
 * @see memory_set
 * @code
 * void *buffer = memory_alloc(256);
 * @endcode
 */
void* memory_alloc(USize const byte_count);

/**
 * @brief Copy memory from src to dst (size in bytes).
 * @param dst Destination buffer.
 * @param src Source buffer.
 * @param src_size Number of bytes to copy.
 * @note Buffers must not overlap. A src_size of 0 is a documented no-op.
 * @code
 * memory_copy_1(dst, src, size);
 * @endcode
 */
void memory_copy_1(void *const dst, void const *const src, USize const src_size);

/**
 * @brief Copy memory with explicit destination size (safe copy).
 * @param dst Destination buffer.
 * @param dst_size Size of destination buffer.
 * @param src Source buffer.
 * @param src_size Number of bytes to copy.
 * @note An overflowing copy (src_size > dst_size) is REFUSED in every build; the error
 *       check is the checked-build diagnostic. A src_size of 0 is a documented no-op.
 * @code
 * memory_copy_2(dst, dst_size, src, src_size);
 * @endcode
 */
void memory_copy_2(void *const dst, USize const dst_size, void const *const src, USize const src_size);

/**
 * @brief Free and optionally null a pointer.
 * @param data Address of pointer to free. Set to nullptr if MEMORY_NON_DANGLING_POINTER is defined.
 * @code
 * memory_delete(&buffer);
 * @endcode
 * @note Call sites holding a typed pointer cast with `(void**) &pointer`; writing nullptr
 *       through the void** into a typed object is formally an aliasing violation the
 *       framework accepts deliberately - every supported ABI uses one uniform
 *       object-pointer representation.
 */
void memory_delete(void **const data);

/**
 * @brief Check if a pointer is empty (nullptr).
 * @param self Pointer to check.
 * @return true if the pointer is nullptr, false otherwise.
 */
bool memory_empty(void const *const self);

/**
 * @brief Compute the next power-of-two fit for a given size/count.
 * @param byte_size Size of each element.
 * @param byte_count Number of elements.
 * @return Fitted size in bytes (power-of-two, >= byte_size * byte_count) - except that a
 *         product past USIZE_MAX / 2 is returned AS-IS (not rounded up; the next power of
 *         two would overflow). Under ERROR_CHECK_ENABLED a zero operand ABORTS before the
 *         in-function guard runs, but 0 IS still reachable in checked builds when the
 *         MULTIPLICATION overflows (both operands nonzero pass the checks); zero-operand 0
 *         returns happen only in unchecked builds.
 * @code
 * USize fit = memory_fit_size(sizeof(int), 100);
 * @endcode
 */
USize memory_fit_size(USize const byte_size, USize const byte_count);

/**
 * @brief Free a memory block.
 * @param data Pointer to memory to free.
 * @code
 * memory_free(buffer);
 * @endcode
 */
void memory_free(void const *const data);

/**
 * @brief Reallocate a block of memory, zeroing whatever the block grew by.
 *
 * old_byte_count is what keeps the framework's always-zeroed guarantee true across a resize.
 * realloc preserves the existing bytes and leaves the grown tail undefined, and the allocator
 * cannot work out where that tail starts on its own - so the caller has to say. Pass the size
 * the block was actually allocated with: too large exposes real garbage, too small wipes live
 * bytes.
 *
 * @param data Pointer to memory to reallocate (can be nullptr to allocate a new block).
 * @param old_byte_count Size the block currently holds. Ignored when data is nullptr.
 * @param byte_count Number of bytes to allocate.
 * @return Pointer to reallocated memory, or nullptr on failure.
 * @note Growing zeroes only [old_byte_count, byte_count); the preserved bytes are untouched.
 *       Shrinking zeroes nothing. A nullptr data allocates fresh via the RECOVERING
 *       memory_try_alloc - the whole function reports OOM as nullptr on every path.
 * @note Ownership on failure: the ORIGINAL block is unchanged and still owned by the caller,
 *       so the idiomatic `p = memory_realloc(p, old, new)` leaks it - keep the old pointer
 *       until success. On success the passed pointer is invalid (not nulled; this function
 *       takes it by value, so MEMORY_NON_DANGLING_POINTER cannot apply).
 * @code
 * void *new_buffer = memory_realloc(buffer, old_size, new_size);
 * @endcode
 */
void* memory_realloc(void const *const data, USize const old_byte_count, USize const byte_count);

/**
 * @brief Set all bytes in a buffer to a value.
 * @param dst Buffer to set.
 * @param dst_size Number of bytes to set.
 * @param value Value to set each byte to.
 * @code
 * memory_set(buffer, 128, 0);
 * @endcode
 */
void memory_set(void *const dst, USize const dst_size, U8 const value);

/**
 * @brief Allocate a block of memory without abort-style error checks.
 * @param byte_count Number of bytes to allocate.
 * @return Pointer to allocated memory, or nullptr when byte_count is 0 or allocation fails.
 * @note Boundary-safe variant of memory_alloc: it never triggers error_check aborts, so callers
 *       guarding a host process (e.g. the JVM behind jni_bridge) can turn allocation failure
 *       into a recoverable error. ALWAYS zeroed, exactly as memory_alloc is.
 * @see memory_alloc
 * @code
 * void *buffer = memory_try_alloc(256);
 * @endcode
 */
void* memory_try_alloc(USize const byte_count);

#endif // MEMORY_H