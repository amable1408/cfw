/*
 * t_usize_pvoid.h - Tuple type (USize, void*) for the C Libraries Framework
 *
 * Features:
 *   - Canonical tuple struct for (USize, void*)
 *   - Multiple initialization functions for flexibility
 *   - Suitable for generic data passing, callbacks, and container utilities
 *
 * Usage Examples:
 *   @code
 *   T_USize_PVoid t1 = t_usize_pvoid_init_4(42, my_ptr);
 *   T_USize_PVoid t2 = t_usize_pvoid_init_2(123);
 *   T_USize_PVoid t3 = t_usize_pvoid_init_3(my_ptr);
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
 *     tuple performs no lifetime or bounds validation and _0 is not verified
 *     against _1.
 *
 * Performance Characteristics:
 *   - Each initializer is a constant-time struct construction.
 *
 * Dependencies:
 *   - <types.h> for the USize type.
 *
 * See t_usize_pvoid.c for implementation details.
 */

#ifndef TUPLE_USIZE_PVOID_H
#define TUPLE_USIZE_PVOID_H

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/

#include <types.h>

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief Tuple of (USize, void*)
 */
typedef struct {
    USize _0;   /**< First element (USize)  */
    void *_1;   /**< Second element (void*) */
} T_USize_PVoid;

/*==============================================================================
 * MARK: - API
 *============================================================================*/

/**
 * @brief Initialize tuple to (0, nullptr)
 * @return Initialized tuple
 */
T_USize_PVoid t_usize_pvoid_init_1(void);

/**
 * @brief Initialize tuple to (data_0, nullptr)
 * @param data_0 First element (USize)
 * @return Initialized tuple
 */
T_USize_PVoid t_usize_pvoid_init_2(USize const data_0);

/**
 * @brief Initialize tuple to (0, data_1)
 * @param data_1 Second element (void*)
 * @return Initialized tuple
 */
T_USize_PVoid t_usize_pvoid_init_3(void *const data_1);

/**
 * @brief Initialize tuple to (data_0, data_1)
 * @param data_0 First element (USize)
 * @param data_1 Second element (void*)
 * @return Initialized tuple
 */
T_USize_PVoid t_usize_pvoid_init_4(USize const data_0, void *const data_1);

#endif // TUPLE_USIZE_PVOID_H