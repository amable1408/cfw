/*
 * tuple.h - Aggregate include for all tuple types in the C Libraries Framework
 *
 * Features:
 *   - Single include that exposes every tuple type under tuple
 *
 * Usage Examples:
 *   @code
 *   #include <tuple/tuple.h>
 *   T_Bool_USize t = t_bool_usize_init_4(true, 42);
 *   @endcode
 *
 * Error Handling:
 *   - None; this header only aggregates other headers.
 *
 * Thread Safety:
 *   - Header-only aggregation; thread safety is that of each tuple type.
 *
 * Memory Management:
 *   - No allocation; see each tuple type.
 *
 * Performance Characteristics:
 *   - None; this is a pure include aggregator.
 *
 * Dependencies:
 *   - The individual tuple headers under tuple.
 *
 * See the individual tuple headers for documentation and usage examples.
 */

#ifndef TUPLE_TUPLE_H
#define TUPLE_TUPLE_H

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/

#include <tuple/t_bool_usize.h>
#include <tuple/t_pvoid_usize.h>
#include <tuple/t_usize_pvoid.h>

#endif // TUPLE_TUPLE_H