/*
 * t_pvoid_usize.h - Tuple type (void*, USize) for the C Libraries Framework
 *
 * Features:
 *   - Canonical tuple struct for (void*, USize)
 *   - Multiple initialization functions for flexibility
 *   - Suitable for generic data passing, callbacks, and container utilities
 *
 * Usage Examples:
 *   @code
 *   T_PVoid_USize t1 = t_pvoid_usize_init_4(my_ptr, 42);
 *   T_PVoid_USize t2 = t_pvoid_usize_init_2(my_ptr);
 *   T_PVoid_USize t3 = t_pvoid_usize_init_3(123);
 *   @endcode
 *
 * Error Handling:
 *   - None; the initializers cannot fail and perform no validation.
 *
 * Thread Safety:
 *   - All functions are pure and thread-safe; tuples are plain values.
 *
 * Memory Management:
 *   - No allocation; the tuple stores but does not own the pointer it carries.
 *     The caller guarantees the pointer outlives every copy of the tuple; the
 *     tuple performs no lifetime or bounds validation and _1 is not verified
 *     against _0.
 *
 * Performance Characteristics:
 *   - Each initializer is a constant-time struct construction.
 *
 * Dependencies:
 *   - <types.h> for the USize type.
 *
 * See t_pvoid_usize.c for implementation details.
 */

#ifndef TUPLE_PVOID_USIZE_H
#define TUPLE_PVOID_USIZE_H

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/

#include <types.h>

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief Tuple of (void*, USize)
 */
typedef struct {
    void *_0;   /**< First element (void*)  */
    USize _1;   /**< Second element (USize) */
} T_PVoid_USize;

/*==============================================================================
 * MARK: - API
 *============================================================================*/

/**
 * @brief Initialize tuple to (nullptr, 0)
 * @return Initialized tuple
 */
T_PVoid_USize t_pvoid_usize_init_1(void);

/**
 * @brief Initialize tuple to (data_0, 0)
 * @param data_0 First element (void*)
 * @return Initialized tuple
 */
T_PVoid_USize t_pvoid_usize_init_2(void *const data_0);

/**
 * @brief Initialize tuple to (nullptr, data_1)
 * @param data_1 Second element (USize)
 * @return Initialized tuple
 */
T_PVoid_USize t_pvoid_usize_init_3(USize const data_1);

/**
 * @brief Initialize tuple to (data_0, data_1)
 * @param data_0 First element (void*)
 * @param data_1 Second element (USize)
 * @return Initialized tuple
 */
T_PVoid_USize t_pvoid_usize_init_4(void *const data_0, USize const data_1);

#endif // TUPLE_PVOID_USIZE_H