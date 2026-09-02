/*
 * t_bool_usize.c - Implementation of (bool, USize) tuple for the C Libraries Framework
 *
 * See t_bool_usize.h for API documentation and usage examples.
 */

#include <tuple/t_bool_usize.h>

/*==============================================================================
 * MARK: - API
 *============================================================================*/

T_Bool_USize t_bool_usize_init_1(void) {
    return t_bool_usize_init_4(false, 0);
}

T_Bool_USize t_bool_usize_init_2(bool const data_0) {
    return t_bool_usize_init_4(data_0, 0);
}

T_Bool_USize t_bool_usize_init_3(USize const data_1) {
    return t_bool_usize_init_4(false, data_1);
}

T_Bool_USize t_bool_usize_init_4(bool const data_0, USize const data_1) {
    return (T_Bool_USize) {
        ._0 = data_0,
        ._1 = data_1
    };
}