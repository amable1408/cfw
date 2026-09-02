/*
 * noise.c - Perlin noise wrappers over cglm's compiled glmc_perlin_* API.
 *
 * See noise.h for API documentation and usage examples.
 */

#include <math/noise.h>

/*==============================================================================
 * MARK: - Noise API
 *============================================================================*/

FSize math_noise_perlin_vec2_1(FSize const *const point) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "point", (void*) point);

    vec2 cp = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(point, cp);

    FSize const result = (FSize) glmc_perlin_vec2(cp);

    trace_log_pop();

    return result;
}

FSize math_noise_perlin_vec2_2(Vec2 const point) {
    trace_log_push(LOG_METADATA);

    vec2 cp = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(point, cp);

    FSize const result = (FSize) glmc_perlin_vec2(cp);

    trace_log_pop();

    return result;
}

FSize math_noise_perlin_vec3_1(FSize const *const point) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "point", (void*) point);

    vec3 cp = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(point, cp);

    FSize const result = (FSize) glmc_perlin_vec3(cp);

    trace_log_pop();

    return result;
}

FSize math_noise_perlin_vec3_2(Vec3 const point) {
    trace_log_push(LOG_METADATA);

    vec3 cp = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(point, cp);

    FSize const result = (FSize) glmc_perlin_vec3(cp);

    trace_log_pop();

    return result;
}

FSize math_noise_perlin_vec4_1(FSize const *const point) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "point", (void*) point);

    vec4 cp = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(point, cp);

    FSize const result = (FSize) glmc_perlin_vec4(cp);

    trace_log_pop();

    return result;
}

FSize math_noise_perlin_vec4_2(Vec4 const point) {
    trace_log_push(LOG_METADATA);

    vec4 cp = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(point, cp);

    FSize const result = (FSize) glmc_perlin_vec4(cp);

    trace_log_pop();

    return result;
}