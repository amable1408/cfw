/*
 * curve.c - Cubic spline/curve evaluation for the CFW math module.
 *
 * See curve.h for API documentation and usage examples.
 */

#include <math/curve.h>

/*==============================================================================
 * MARK: - Curve API
 *============================================================================*/

FSize math_curve_smc_1(FSize const s, FSize const *const m, FSize const *const c) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "m", (void*) m);
    error_check_null(LOG_METADATA, "c", (void*) c);

    mat4 cm = DEFAULT_INITIALIZATION;
    vec4 cc = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(m, cm);
    _math_vec4_raw_to_cglm(c, cc);

    FSize const result = (FSize) glmc_smc((float) s, cm, cc);

    trace_log_pop();

    return result;
}

FSize math_curve_smc_2(FSize const s, Mat4 const m, Vec4 const c) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;
    vec4 cc = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(m, cm);
    _math_vec4_to_cglm(c, cc);

    FSize const result = (FSize) glmc_smc((float) s, cm, cc);

    trace_log_pop();

    return result;
}