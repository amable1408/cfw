/*
 * ray.c - Ray/geometry intersection operations for the CFW math module.
 *
 * See ray.h for API documentation and usage examples.
 */

#include <math/ray.h>

/*==============================================================================
 * MARK: - Ray API
 *============================================================================*/

void math_ray_at_1(FSize const *const origin, FSize const *const direction, FSize const t, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "origin", (void*) origin);
    error_check_null(LOG_METADATA, "direction", (void*) direction);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 corigin = DEFAULT_INITIALIZATION;
    vec3 cdir = DEFAULT_INITIALIZATION;
    vec3 cpoint = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(origin, corigin);
    _math_vec3_raw_to_cglm(direction, cdir);
    glmc_ray_at(corigin, cdir, (float) t, cpoint);
    _math_vec3_raw_from_cglm(cpoint, dest);

    trace_log_pop();
}

Vec3 math_ray_at_2(Ray const ray, FSize const t) {
    trace_log_push(LOG_METADATA);

    vec3 corigin = DEFAULT_INITIALIZATION;
    vec3 cdir = DEFAULT_INITIALIZATION;
    vec3 cpoint = DEFAULT_INITIALIZATION;

    _math_ray_to_cglm(ray, corigin, cdir);
    glmc_ray_at(corigin, cdir, (float) t, cpoint);

    Vec3 const result = _math_vec3_from_cglm(cpoint);

    trace_log_pop();

    return result;
}

bool math_ray_sphere_1(FSize const *const origin, FSize const *const direction, FSize const *const sphere, FSize *const t1, FSize *const t2) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "origin", (void*) origin);
    error_check_null(LOG_METADATA, "direction", (void*) direction);
    error_check_null(LOG_METADATA, "sphere", (void*) sphere);
    error_check_null(LOG_METADATA, "t1", (void*) t1);
    error_check_null(LOG_METADATA, "t2", (void*) t2);

    vec3 corigin = DEFAULT_INITIALIZATION;
    vec3 cdir = DEFAULT_INITIALIZATION;
    vec4 csphere = DEFAULT_INITIALIZATION;
    float ct1 = DEFAULT_INITIALIZATION;
    float ct2 = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(origin, corigin);
    _math_vec3_raw_to_cglm(direction, cdir);
    _math_vec4_raw_to_cglm(sphere, csphere);

    bool const result = glmc_ray_sphere(corigin, cdir, csphere, &ct1, &ct2);

    /* cglm writes both roots before returning false when the sphere is entirely behind the origin;
     * the contract is 0 on every miss, so a miss overrides what it left. */
    *t1 = result ? (FSize) ct1 : 0.0;
    *t2 = result ? (FSize) ct2 : 0.0;

    trace_log_pop();

    return result;
}

RaySphereHit math_ray_sphere_2(Ray const ray, Sphere const sphere) {
    trace_log_push(LOG_METADATA);

    vec3 corigin = DEFAULT_INITIALIZATION;
    vec3 cdir = DEFAULT_INITIALIZATION;
    vec4 csphere = DEFAULT_INITIALIZATION;
    float ct1 = DEFAULT_INITIALIZATION;
    float ct2 = DEFAULT_INITIALIZATION;

    _math_ray_to_cglm(ray, corigin, cdir);
    _math_sphere_to_cglm(sphere, csphere);

    /* The call is its own statement: initializer expressions are indeterminately sequenced, so
     * ct1/ct2 could otherwise be read before the call wrote them. On a miss cglm either leaves
     * them untouched (no real root) or writes two negative roots (sphere behind the origin); the
     * contract is 0 on every miss, so the miss branch does not read them. */
    bool const hit = glmc_ray_sphere(corigin, cdir, csphere, &ct1, &ct2);
    RaySphereHit const result = { hit, hit ? (FSize) ct1 : 0.0, hit ? (FSize) ct2 : 0.0 };

    trace_log_pop();

    return result;
}

bool math_ray_triangle_1(FSize const *const origin, FSize const *const direction, FSize const *const v0, FSize const *const v1, FSize const *const v2, FSize *const d) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "origin", (void*) origin);
    error_check_null(LOG_METADATA, "direction", (void*) direction);
    error_check_null(LOG_METADATA, "v0", (void*) v0);
    error_check_null(LOG_METADATA, "v1", (void*) v1);
    error_check_null(LOG_METADATA, "v2", (void*) v2);
    error_check_null(LOG_METADATA, "d", (void*) d);

    vec3 corigin = DEFAULT_INITIALIZATION;
    vec3 cdir = DEFAULT_INITIALIZATION;
    vec3 cv0 = DEFAULT_INITIALIZATION;
    vec3 cv1 = DEFAULT_INITIALIZATION;
    vec3 cv2 = DEFAULT_INITIALIZATION;
    float cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(origin, corigin);
    _math_vec3_raw_to_cglm(direction, cdir);
    _math_vec3_raw_to_cglm(v0, cv0);
    _math_vec3_raw_to_cglm(v1, cv1);
    _math_vec3_raw_to_cglm(v2, cv2);

    bool const result = glmc_ray_triangle(corigin, cdir, cv0, cv1, cv2, &cd);

    /* 0 on every miss, like the sphere: cglm leaves cd untouched or writes a sub-epsilon distance. */
    *d = result ? (FSize) cd : 0.0;

    trace_log_pop();

    return result;
}

RayTriangleHit math_ray_triangle_2(Ray const ray, Vec3 const v0, Vec3 const v1, Vec3 const v2) {
    trace_log_push(LOG_METADATA);

    vec3 corigin = DEFAULT_INITIALIZATION;
    vec3 cdir = DEFAULT_INITIALIZATION;
    vec3 cv0 = DEFAULT_INITIALIZATION;
    vec3 cv1 = DEFAULT_INITIALIZATION;
    vec3 cv2 = DEFAULT_INITIALIZATION;
    float cd = DEFAULT_INITIALIZATION;

    _math_ray_to_cglm(ray, corigin, cdir);
    _math_vec3_to_cglm(v0, cv0);
    _math_vec3_to_cglm(v1, cv1);
    _math_vec3_to_cglm(v2, cv2);

    /* Sequenced before the read of cd (see math_ray_sphere_2); 0 on every miss, like the sphere. */
    bool const hit = glmc_ray_triangle(corigin, cdir, cv0, cv1, cv2, &cd);
    RayTriangleHit const result = { hit, hit ? (FSize) cd : 0.0 };

    trace_log_pop();

    return result;
}