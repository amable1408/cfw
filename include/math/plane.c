/*
 * plane.c - Plane operations for the CFW math module.
 *
 * See plane.h for API documentation and usage examples.
 */

#include <math/plane.h>

/*==============================================================================
 * MARK: - Plane API
 *============================================================================*/

void math_plane_normalize_1(FSize const *const plane, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "plane", (void*) plane);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 cp = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(plane, cp);
    glmc_plane_normalize(cp);
    _math_vec4_raw_from_cglm(cp, dest);

    trace_log_pop();
}

Plane math_plane_normalize_2(Plane const plane) {
    trace_log_push(LOG_METADATA);

    vec4 cp = { (float) plane.x, (float) plane.y, (float) plane.z, (float) plane.w };

    glmc_plane_normalize(cp);

    Plane const result = { (FSize) cp[0], (FSize) cp[1], (FSize) cp[2], (FSize) cp[3] };

    trace_log_pop();

    return result;
}