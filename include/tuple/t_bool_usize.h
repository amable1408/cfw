/*
 * t_bool_usize.h - Tuple type (bool, USize) for the C Libraries Framework
 *
 * Features:
 *   - Canonical tuple struct for (bool, USize)
 *   - Multiple initialization functions for flexibility
 *   - Suitable for generic data passing, callbacks, and container utilities
 *
 * Usage Examples:
 *   @code
 *   T_Bool_USize t1 = t_bool_usize_init_4(true, 42);
 *   T_Bool_USize t2 = t_bool_usize_init_2(false);
 *   T_Bool_USize t3 = t_bool_usize_init_3(123);
 *   @endcode
 *
 * Error Handling:
 *   - None; the initializers cannot fail and perform no validation.
 *
 * Thread Safety:
 *   - All functions are pure and thread-safe; tuples are plain values.
 *
 * Memory Management:
 *   - No allocation; tuples are returned by value and own nothing. Struct
 *     padding between members is indeterminate — compare tuples field-wise,
 *     never byte-wise (memcmp) or serialize them raw.
 *
 * Performance Characteristics:
 *   - Each initializer is a constant-time struct construction.
 *
 * Dependencies:
 *   - <types.h> for the bool and USize types.
 *
 * See t_bool_usize.c for implementation details.
 */

#ifndef TUPLE_BOOL_USIZE_H
#define TUPLE_BOOL_USIZE_H

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/

#include <types.h>

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/**
 * @brief Tuple of (bool, USize)
 */
typedef struct {
    bool  _0;   /**< First element (bool)  */
    USize _1;   /**< Second element (USize) */
} T_Bool_USize;

/*==============================================================================
 * MARK: - API
 *============================================================================*/

/**
 * @brief Initialize tuple to (false, 0)
 * @return Initialized tuple
 */
T_Bool_USize t_bool_usize_init_1(void);

/**
 * @brief Initialize tuple to (data_0, 0)
 * @param data_0 First element (bool)
 * @return Initialized tuple
 */
T_Bool_USize t_bool_usize_init_2(bool const data_0);

/**
 * @brief Initialize tuple to (false, data_1)
 * @param data_1 Second element (USize)
 * @return Initialized tuple
 */
T_Bool_USize t_bool_usize_init_3(USize const data_1);

/**
 * @brief Initialize tuple to (data_0, data_1)
 * @param data_0 First element (bool)
 * @param data_1 Second element (USize)
 * @return Initialized tuple
 */
T_Bool_USize t_bool_usize_init_4(bool const data_0, USize const data_1);

#endif // TUPLE_BOOL_USIZE_H