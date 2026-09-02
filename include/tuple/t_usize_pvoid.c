/*
 * t_usize_pvoid.c - Implementation of (USize, void*) tuple for the C Libraries Framework
 *
 * See t_usize_pvoid.h for API documentation and usage examples.
 */

#include <tuple/t_usize_pvoid.h>

/*==============================================================================
 * MARK: - API
 *============================================================================*/

T_USize_PVoid t_usize_pvoid_init_1(void) {
    return t_usize_pvoid_init_4(0, nullptr);
}

T_USize_PVoid t_usize_pvoid_init_2(USize const data_0) {
    return t_usize_pvoid_init_4(data_0, nullptr);
}

T_USize_PVoid t_usize_pvoid_init_3(void *const data_1) {
    return t_usize_pvoid_init_4(0, data_1);
}

T_USize_PVoid t_usize_pvoid_init_4(USize const data_0, void *const data_1) {
    return (T_USize_PVoid) {
        ._0 = data_0,
        ._1 = data_1
    };
}