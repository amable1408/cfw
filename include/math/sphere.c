/*
 * sphere.c - Bounding-sphere operations for the CFW math module.
 *
 * See sphere.h for API documentation and usage examples.
 */

#include <math/sphere.h>

/*==============================================================================
 * MARK: - Sphere API
 *============================================================================*/

void math_sphere_merge_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 cs1 = DEFAULT_INITIALIZATION;
    vec4 cs2 = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(a, cs1);
    _math_vec4_raw_to_cglm(b, cs2);
    glmc_sphere_merge(cs1, cs2, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Sphere math_sphere_merge_2(Sphere const a, Sphere const b) {
    trace_log_push(LOG_METADATA);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_sphere_to_cglm(a, ca);
    _math_sphere_to_cglm(b, cb);
    glmc_sphere_merge(ca, cb, cd);

    Sphere const result = _math_sphere_from_cglm(cd);

    trace_log_pop();

    return result;
}

bool math_sphere_point_1(FSize const *const sphere, FSize const *const point) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "sphere", (void*) sphere);
    error_check_null(LOG_METADATA, "point", (void*) point);

    vec4 cs = DEFAULT_INITIALIZATION;
    vec3 cp = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(sphere, cs);
    _math_vec3_raw_to_cglm(point, cp);

    bool const result = glmc_sphere_point(cs, cp);

    trace_log_pop();

    return result;
}

bool math_sphere_point_2(Sphere const sphere, Vec3 const point) {
    trace_log_push(LOG_METADATA);

    vec4 cs = DEFAULT_INITIALIZATION;
    vec3 cp = DEFAULT_INITIALIZATION;

    _math_sphere_to_cglm(sphere, cs);
    _math_vec3_to_cglm(point, cp);

    bool const result = glmc_sphere_point(cs, cp);

    trace_log_pop();

    return result;
}

FSize math_sphere_radii_1(FSize const *const sphere) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "sphere", (void*) sphere);

    vec4 cs = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(sphere, cs);

    FSize const result = (FSize) glmc_sphere_radii(cs);

    trace_log_pop();

    return result;
}

FSize math_sphere_radii_2(Sphere const sphere) {
    trace_log_push(LOG_METADATA);

    vec4 cs = DEFAULT_INITIALIZATION;

    _math_sphere_to_cglm(sphere, cs);

    FSize const result = (FSize) glmc_sphere_radii(cs);

    trace_log_pop();

    return result;
}

bool math_sphere_sphere_1(FSize const *const a, FSize const *const b) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);

    vec4 cs1 = DEFAULT_INITIALIZATION;
    vec4 cs2 = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(a, cs1);
    _math_vec4_raw_to_cglm(b, cs2);

    bool const result = glmc_sphere_sphere(cs1, cs2);

    trace_log_pop();

    return result;
}

bool math_sphere_sphere_2(Sphere const a, Sphere const b) {
    trace_log_push(LOG_METADATA);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;

    _math_sphere_to_cglm(a, ca);
    _math_sphere_to_cglm(b, cb);

    bool const result = glmc_sphere_sphere(ca, cb);

    trace_log_pop();

    return result;
}

void math_sphere_transform_1(FSize const *const sphere, FSize const *const m, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "sphere", (void*) sphere);
    error_check_null(LOG_METADATA, "m", (void*) m);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 cs = DEFAULT_INITIALIZATION;
    mat4 cm = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(sphere, cs);
    _math_mat4_raw_to_cglm(m, cm);
    glmc_sphere_transform(cs, cm, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Sphere math_sphere_transform_2(Sphere const sphere, Mat4 const m) {
    trace_log_push(LOG_METADATA);

    vec4 cs = DEFAULT_INITIALIZATION;
    mat4 cm = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_sphere_to_cglm(sphere, cs);
    _math_mat4_to_cglm(m, cm);
    glmc_sphere_transform(cs, cm, cd);

    Sphere const result = _math_sphere_from_cglm(cd);

    trace_log_pop();

    return result;
}