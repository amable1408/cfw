/*
 * t_pvoid_usize.c - Implementation of (void*, USize) tuple for the C Libraries Framework
 *
 * See t_pvoid_usize.h for API documentation and usage examples.
 */

#include <tuple/t_pvoid_usize.h>

/*==============================================================================
 * MARK: - API
 *============================================================================*/

T_PVoid_USize t_pvoid_usize_init_1(void) {
    return t_pvoid_usize_init_4(nullptr, 0);
}

T_PVoid_USize t_pvoid_usize_init_2(void *const data_0) {
    return t_pvoid_usize_init_4(data_0, 0);
}

T_PVoid_USize t_pvoid_usize_init_3(USize const data_1) {
    return t_pvoid_usize_init_4(nullptr, data_1);
}

T_PVoid_USize t_pvoid_usize_init_4(void *const data_0, USize const data_1) {
    return (T_PVoid_USize) {
        ._0 = data_0,
        ._1 = data_1
    };
}