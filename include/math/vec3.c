/*
 * vec3.c - 3D vector operations for the CFW math module.
 *
 * See vec3.h for API documentation and usage examples.
 */

#include <math/vec3.h>

/*==============================================================================
 * MARK: - Vec3 API
 *============================================================================*/

void math_vec3_abs_1(FSize const *const v, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(v, cv);
    glmc_vec3_abs(cv, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_abs_2(Vec3 const v) {
    trace_log_push(LOG_METADATA);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(v, cv);
    glmc_vec3_abs(cv, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_add_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(a, ca);
    _math_vec3_raw_to_cglm(b, cb);
    glmc_vec3_add(ca, cb, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_add_2(Vec3 const a, Vec3 const b) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(a, ca);
    _math_vec3_to_cglm(b, cb);
    glmc_vec3_add(ca, cb, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_addadd_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(a, ca);
    _math_vec3_raw_to_cglm(b, cb);
    _math_vec3_raw_to_cglm(dest, cd);
    glmc_vec3_addadd(ca, cb, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_addadd_2(Vec3 const a, Vec3 const b, Vec3 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(a, ca);
    _math_vec3_to_cglm(b, cb);
    _math_vec3_to_cglm(accumulator, cd);
    glmc_vec3_addadd(ca, cb, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_adds_1(FSize const *const v, FSize const s, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(v, cv);
    glmc_vec3_adds(cv, (float) s, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_adds_2(Vec3 const v, FSize const s) {
    trace_log_push(LOG_METADATA);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(v, cv);
    glmc_vec3_adds(cv, (float) s, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_addsub_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(a, ca);
    _math_vec3_raw_to_cglm(b, cb);
    _math_vec3_raw_to_cglm(dest, cd);
    glmc_vec3_addsub(ca, cb, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_addsub_2(Vec3 const a, Vec3 const b, Vec3 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(a, ca);
    _math_vec3_to_cglm(b, cb);
    _math_vec3_to_cglm(accumulator, cd);
    glmc_vec3_addsub(ca, cb, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

FSize math_vec3_angle_1(FSize const *const a, FSize const *const b) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(a, ca);
    _math_vec3_raw_to_cglm(b, cb);

    FSize const result = (FSize) glmc_vec3_angle(ca, cb);

    trace_log_pop();

    return result;
}

FSize math_vec3_angle_2(Vec3 const a, Vec3 const b) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(a, ca);
    _math_vec3_to_cglm(b, cb);

    FSize const result = (FSize) glmc_vec3_angle(ca, cb);

    trace_log_pop();

    return result;
}

void math_vec3_broadcast_1(FSize const val, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cd = DEFAULT_INITIALIZATION;

    glmc_vec3_broadcast((float) val, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_broadcast_2(FSize const val) {
    trace_log_push(LOG_METADATA);

    vec3 cd = DEFAULT_INITIALIZATION;

    glmc_vec3_broadcast((float) val, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_center_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(a, ca);
    _math_vec3_raw_to_cglm(b, cb);
    glmc_vec3_center(ca, cb, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_center_2(Vec3 const a, Vec3 const b) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(a, ca);
    _math_vec3_to_cglm(b, cb);
    glmc_vec3_center(ca, cb, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_clamp_1(FSize const *const v, FSize const minval, FSize const maxval, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(v, cv);
    glmc_vec3_clamp(cv, (float) minval, (float) maxval);
    _math_vec3_raw_from_cglm(cv, dest);

    trace_log_pop();
}

Vec3 math_vec3_clamp_2(Vec3 const v, FSize const minval, FSize const maxval) {
    trace_log_push(LOG_METADATA);

    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(v, cv);
    glmc_vec3_clamp(cv, (float) minval, (float) maxval);

    Vec3 const result = _math_vec3_from_cglm(cv);

    trace_log_pop();

    return result;
}

void math_vec3_copy_1(FSize const *const a, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(a, ca);
    glmc_vec3_copy(ca, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_copy_2(Vec3 const a) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(a, ca);
    glmc_vec3_copy(ca, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_cross_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(a, ca);
    _math_vec3_raw_to_cglm(b, cb);
    glmc_vec3_cross(ca, cb, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_cross_2(Vec3 const a, Vec3 const b) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(a, ca);
    _math_vec3_to_cglm(b, cb);
    glmc_vec3_cross(ca, cb, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_crossn_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(a, ca);
    _math_vec3_raw_to_cglm(b, cb);
    glmc_vec3_crossn(ca, cb, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_crossn_2(Vec3 const a, Vec3 const b) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(a, ca);
    _math_vec3_to_cglm(b, cb);
    glmc_vec3_crossn(ca, cb, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

FSize math_vec3_distance_1(FSize const *const a, FSize const *const b) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(a, ca);
    _math_vec3_raw_to_cglm(b, cb);

    FSize const result = (FSize) glmc_vec3_distance(ca, cb);

    trace_log_pop();

    return result;
}

FSize math_vec3_distance_2(Vec3 const a, Vec3 const b) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(a, ca);
    _math_vec3_to_cglm(b, cb);

    FSize const result = (FSize) glmc_vec3_distance(ca, cb);

    trace_log_pop();

    return result;
}

FSize math_vec3_distance2_1(FSize const *const a, FSize const *const b) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(a, ca);
    _math_vec3_raw_to_cglm(b, cb);

    FSize const result = (FSize) glmc_vec3_distance2(ca, cb);

    trace_log_pop();

    return result;
}

FSize math_vec3_distance2_2(Vec3 const a, Vec3 const b) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(a, ca);
    _math_vec3_to_cglm(b, cb);

    FSize const result = (FSize) glmc_vec3_distance2(ca, cb);

    trace_log_pop();

    return result;
}

void math_vec3_div_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(a, ca);
    _math_vec3_raw_to_cglm(b, cb);
    glmc_vec3_div(ca, cb, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_div_2(Vec3 const a, Vec3 const b) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(a, ca);
    _math_vec3_to_cglm(b, cb);
    glmc_vec3_div(ca, cb, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_divs_1(FSize const *const v, FSize const s, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(v, cv);
    glmc_vec3_divs(cv, (float) s, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_divs_2(Vec3 const v, FSize const s) {
    trace_log_push(LOG_METADATA);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(v, cv);
    glmc_vec3_divs(cv, (float) s, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

FSize math_vec3_dot_1(FSize const *const a, FSize const *const b) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(a, ca);
    _math_vec3_raw_to_cglm(b, cb);

    FSize const result = (FSize) glmc_vec3_dot(ca, cb);

    trace_log_pop();

    return result;
}

FSize math_vec3_dot_2(Vec3 const a, Vec3 const b) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(a, ca);
    _math_vec3_to_cglm(b, cb);

    FSize const result = (FSize) glmc_vec3_dot(ca, cb);

    trace_log_pop();

    return result;
}

bool math_vec3_eq_1(FSize const *const v, FSize const val) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);

    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(v, cv);

    bool const result = glmc_vec3_eq(cv, (float) val);

    trace_log_pop();

    return result;
}

bool math_vec3_eq_2(Vec3 const v, FSize const val) {
    trace_log_push(LOG_METADATA);

    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(v, cv);

    bool const result = glmc_vec3_eq(cv, (float) val);

    trace_log_pop();

    return result;
}

bool math_vec3_eq_all_1(FSize const *const v) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);

    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(v, cv);

    bool const result = glmc_vec3_eq_all(cv);

    trace_log_pop();

    return result;
}

bool math_vec3_eq_all_2(Vec3 const v) {
    trace_log_push(LOG_METADATA);

    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(v, cv);

    bool const result = glmc_vec3_eq_all(cv);

    trace_log_pop();

    return result;
}

bool math_vec3_eq_eps_1(FSize const *const v, FSize const val) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);

    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(v, cv);

    bool const result = glmc_vec3_eq_eps(cv, (float) val);

    trace_log_pop();

    return result;
}

bool math_vec3_eq_eps_2(Vec3 const v, FSize const val) {
    trace_log_push(LOG_METADATA);

    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(v, cv);

    bool const result = glmc_vec3_eq_eps(cv, (float) val);

    trace_log_pop();

    return result;
}

bool math_vec3_eqv_1(FSize const *const a, FSize const *const b) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(a, ca);
    _math_vec3_raw_to_cglm(b, cb);

    bool const result = glmc_vec3_eqv(ca, cb);

    trace_log_pop();

    return result;
}

bool math_vec3_eqv_2(Vec3 const a, Vec3 const b) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(a, ca);
    _math_vec3_to_cglm(b, cb);

    bool const result = glmc_vec3_eqv(ca, cb);

    trace_log_pop();

    return result;
}

bool math_vec3_eqv_eps_1(FSize const *const a, FSize const *const b) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(a, ca);
    _math_vec3_raw_to_cglm(b, cb);

    bool const result = glmc_vec3_eqv_eps(ca, cb);

    trace_log_pop();

    return result;
}

bool math_vec3_eqv_eps_2(Vec3 const a, Vec3 const b) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(a, ca);
    _math_vec3_to_cglm(b, cb);

    bool const result = glmc_vec3_eqv_eps(ca, cb);

    trace_log_pop();

    return result;
}

void math_vec3_faceforward_1(FSize const *const n, FSize const *const v, FSize const *const nref, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "n", (void*) n);
    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "nref", (void*) nref);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cn = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cnref = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(n, cn);
    _math_vec3_raw_to_cglm(v, cv);
    _math_vec3_raw_to_cglm(nref, cnref);
    glmc_vec3_faceforward(cn, cv, cnref, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_faceforward_2(Vec3 const n, Vec3 const v, Vec3 const nref) {
    trace_log_push(LOG_METADATA);

    vec3 cn = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cnref = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(n, cn);
    _math_vec3_to_cglm(v, cv);
    _math_vec3_to_cglm(nref, cnref);
    glmc_vec3_faceforward(cn, cv, cnref, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_fill_1(FSize const val, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cd = DEFAULT_INITIALIZATION;

    glmc_vec3_fill(cd, (float) val);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_fill_2(FSize const val) {
    trace_log_push(LOG_METADATA);

    vec3 cd = DEFAULT_INITIALIZATION;

    glmc_vec3_fill(cd, (float) val);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_floor_1(FSize const *const v, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(v, cv);
    glmc_vec3_floor(cv, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_floor_2(Vec3 const v) {
    trace_log_push(LOG_METADATA);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(v, cv);
    glmc_vec3_floor(cv, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_fract_1(FSize const *const v, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(v, cv);
    glmc_vec3_fract(cv, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_fract_2(Vec3 const v) {
    trace_log_push(LOG_METADATA);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(v, cv);
    glmc_vec3_fract(cv, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

FSize math_vec3_hadd_1(FSize const *const v) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);

    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(v, cv);

    FSize const result = (FSize) glmc_vec3_hadd(cv);

    trace_log_pop();

    return result;
}

FSize math_vec3_hadd_2(Vec3 const v) {
    trace_log_push(LOG_METADATA);

    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(v, cv);

    FSize const result = (FSize) glmc_vec3_hadd(cv);

    trace_log_pop();

    return result;
}

bool math_vec3_isinf_1(FSize const *const v) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);

    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(v, cv);

    bool const result = glmc_vec3_isinf(cv);

    trace_log_pop();

    return result;
}

bool math_vec3_isinf_2(Vec3 const v) {
    trace_log_push(LOG_METADATA);

    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(v, cv);

    bool const result = glmc_vec3_isinf(cv);

    trace_log_pop();

    return result;
}

bool math_vec3_isnan_1(FSize const *const v) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);

    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(v, cv);

    bool const result = glmc_vec3_isnan(cv);

    trace_log_pop();

    return result;
}

bool math_vec3_isnan_2(Vec3 const v) {
    trace_log_push(LOG_METADATA);

    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(v, cv);

    bool const result = glmc_vec3_isnan(cv);

    trace_log_pop();

    return result;
}

bool math_vec3_isvalid_1(FSize const *const v) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);

    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(v, cv);

    bool const result = glmc_vec3_isvalid(cv);

    trace_log_pop();

    return result;
}

bool math_vec3_isvalid_2(Vec3 const v) {
    trace_log_push(LOG_METADATA);

    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(v, cv);

    bool const result = glmc_vec3_isvalid(cv);

    trace_log_pop();

    return result;
}

void math_vec3_lerp_1(FSize const *const from, FSize const *const to, FSize const t, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "from", (void*) from);
    error_check_null(LOG_METADATA, "to", (void*) to);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(from, ca);
    _math_vec3_raw_to_cglm(to, cb);
    glmc_vec3_lerp(ca, cb, (float) t, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_lerp_2(Vec3 const from, Vec3 const to, FSize const t) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(from, ca);
    _math_vec3_to_cglm(to, cb);
    glmc_vec3_lerp(ca, cb, (float) t, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_lerpc_1(FSize const *const from, FSize const *const to, FSize const t, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "from", (void*) from);
    error_check_null(LOG_METADATA, "to", (void*) to);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(from, ca);
    _math_vec3_raw_to_cglm(to, cb);
    glmc_vec3_lerpc(ca, cb, (float) t, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_lerpc_2(Vec3 const from, Vec3 const to, FSize const t) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(from, ca);
    _math_vec3_to_cglm(to, cb);
    glmc_vec3_lerpc(ca, cb, (float) t, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_make_1(FSize const *const src, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "src", (void*) src);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    float cs[3] = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(src, cs);
    glmc_vec3_make(cs, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_make_2(FSize const *const src) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "src", (void*) src);

    float cs[3] = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(src, cs);
    glmc_vec3_make(cs, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

FSize math_vec3_max_1(FSize const *const v) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);

    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(v, cv);

    FSize const result = (FSize) glmc_vec3_max(cv);

    trace_log_pop();

    return result;
}

FSize math_vec3_max_2(Vec3 const v) {
    trace_log_push(LOG_METADATA);

    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(v, cv);

    FSize const result = (FSize) glmc_vec3_max(cv);

    trace_log_pop();

    return result;
}

void math_vec3_maxadd_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(a, ca);
    _math_vec3_raw_to_cglm(b, cb);
    _math_vec3_raw_to_cglm(dest, cd);
    glmc_vec3_maxadd(ca, cb, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_maxadd_2(Vec3 const a, Vec3 const b, Vec3 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(a, ca);
    _math_vec3_to_cglm(b, cb);
    _math_vec3_to_cglm(accumulator, cd);
    glmc_vec3_maxadd(ca, cb, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_maxsub_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(a, ca);
    _math_vec3_raw_to_cglm(b, cb);
    _math_vec3_raw_to_cglm(dest, cd);
    glmc_vec3_maxsub(ca, cb, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_maxsub_2(Vec3 const a, Vec3 const b, Vec3 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(a, ca);
    _math_vec3_to_cglm(b, cb);
    _math_vec3_to_cglm(accumulator, cd);
    glmc_vec3_maxsub(ca, cb, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_maxv_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(a, ca);
    _math_vec3_raw_to_cglm(b, cb);
    glmc_vec3_maxv(ca, cb, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_maxv_2(Vec3 const a, Vec3 const b) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(a, ca);
    _math_vec3_to_cglm(b, cb);
    glmc_vec3_maxv(ca, cb, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

FSize math_vec3_min_1(FSize const *const v) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);

    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(v, cv);

    FSize const result = (FSize) glmc_vec3_min(cv);

    trace_log_pop();

    return result;
}

FSize math_vec3_min_2(Vec3 const v) {
    trace_log_push(LOG_METADATA);

    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(v, cv);

    FSize const result = (FSize) glmc_vec3_min(cv);

    trace_log_pop();

    return result;
}

void math_vec3_minadd_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(a, ca);
    _math_vec3_raw_to_cglm(b, cb);
    _math_vec3_raw_to_cglm(dest, cd);
    glmc_vec3_minadd(ca, cb, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_minadd_2(Vec3 const a, Vec3 const b, Vec3 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(a, ca);
    _math_vec3_to_cglm(b, cb);
    _math_vec3_to_cglm(accumulator, cd);
    glmc_vec3_minadd(ca, cb, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_minsub_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(a, ca);
    _math_vec3_raw_to_cglm(b, cb);
    _math_vec3_raw_to_cglm(dest, cd);
    glmc_vec3_minsub(ca, cb, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_minsub_2(Vec3 const a, Vec3 const b, Vec3 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(a, ca);
    _math_vec3_to_cglm(b, cb);
    _math_vec3_to_cglm(accumulator, cd);
    glmc_vec3_minsub(ca, cb, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_minv_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(a, ca);
    _math_vec3_raw_to_cglm(b, cb);
    glmc_vec3_minv(ca, cb, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_minv_2(Vec3 const a, Vec3 const b) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(a, ca);
    _math_vec3_to_cglm(b, cb);
    glmc_vec3_minv(ca, cb, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_mods_1(FSize const *const v, FSize const s, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(v, cv);
    glmc_vec3_mods(cv, (float) s, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_mods_2(Vec3 const v, FSize const s) {
    trace_log_push(LOG_METADATA);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(v, cv);
    glmc_vec3_mods(cv, (float) s, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_mul_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(a, ca);
    _math_vec3_raw_to_cglm(b, cb);
    glmc_vec3_mul(ca, cb, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_mul_2(Vec3 const a, Vec3 const b) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(a, ca);
    _math_vec3_to_cglm(b, cb);
    glmc_vec3_mul(ca, cb, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_muladd_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(a, ca);
    _math_vec3_raw_to_cglm(b, cb);
    _math_vec3_raw_to_cglm(dest, cd);
    glmc_vec3_muladd(ca, cb, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_muladd_2(Vec3 const a, Vec3 const b, Vec3 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(a, ca);
    _math_vec3_to_cglm(b, cb);
    _math_vec3_to_cglm(accumulator, cd);
    glmc_vec3_muladd(ca, cb, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_muladds_1(FSize const *const a, FSize const s, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(a, ca);
    _math_vec3_raw_to_cglm(dest, cd);
    glmc_vec3_muladds(ca, (float) s, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_muladds_2(Vec3 const a, FSize const s, Vec3 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(a, ca);
    _math_vec3_to_cglm(accumulator, cd);
    glmc_vec3_muladds(ca, (float) s, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_mulsub_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(a, ca);
    _math_vec3_raw_to_cglm(b, cb);
    _math_vec3_raw_to_cglm(dest, cd);
    glmc_vec3_mulsub(ca, cb, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_mulsub_2(Vec3 const a, Vec3 const b, Vec3 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(a, ca);
    _math_vec3_to_cglm(b, cb);
    _math_vec3_to_cglm(accumulator, cd);
    glmc_vec3_mulsub(ca, cb, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_mulsubs_1(FSize const *const a, FSize const s, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(a, ca);
    _math_vec3_raw_to_cglm(dest, cd);
    glmc_vec3_mulsubs(ca, (float) s, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_mulsubs_2(Vec3 const a, FSize const s, Vec3 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(a, ca);
    _math_vec3_to_cglm(accumulator, cd);
    glmc_vec3_mulsubs(ca, (float) s, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_mulv_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(a, ca);
    _math_vec3_raw_to_cglm(b, cb);
    glmc_vec3_mulv(ca, cb, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_mulv_2(Vec3 const a, Vec3 const b) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(a, ca);
    _math_vec3_to_cglm(b, cb);
    glmc_vec3_mulv(ca, cb, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_negate_1(FSize const *const v, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(v, cv);
    glmc_vec3_negate_to(cv, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_negate_2(Vec3 const v) {
    trace_log_push(LOG_METADATA);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(v, cv);
    glmc_vec3_negate_to(cv, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

FSize math_vec3_norm_1(FSize const *const v) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);

    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(v, cv);

    FSize const result = (FSize) glmc_vec3_norm(cv);

    trace_log_pop();

    return result;
}

FSize math_vec3_norm_2(Vec3 const v) {
    trace_log_push(LOG_METADATA);

    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(v, cv);

    FSize const result = (FSize) glmc_vec3_norm(cv);

    trace_log_pop();

    return result;
}

FSize math_vec3_norm2_1(FSize const *const v) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);

    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(v, cv);

    FSize const result = (FSize) glmc_vec3_norm2(cv);

    trace_log_pop();

    return result;
}

FSize math_vec3_norm2_2(Vec3 const v) {
    trace_log_push(LOG_METADATA);

    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(v, cv);

    FSize const result = (FSize) glmc_vec3_norm2(cv);

    trace_log_pop();

    return result;
}

FSize math_vec3_norm_inf_1(FSize const *const v) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);

    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(v, cv);

    FSize const result = (FSize) glmc_vec3_norm_inf(cv);

    trace_log_pop();

    return result;
}

FSize math_vec3_norm_inf_2(Vec3 const v) {
    trace_log_push(LOG_METADATA);

    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(v, cv);

    FSize const result = (FSize) glmc_vec3_norm_inf(cv);

    trace_log_pop();

    return result;
}

FSize math_vec3_norm_one_1(FSize const *const v) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);

    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(v, cv);

    FSize const result = (FSize) glmc_vec3_norm_one(cv);

    trace_log_pop();

    return result;
}

FSize math_vec3_norm_one_2(Vec3 const v) {
    trace_log_push(LOG_METADATA);

    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(v, cv);

    FSize const result = (FSize) glmc_vec3_norm_one(cv);

    trace_log_pop();

    return result;
}

void math_vec3_normalize_1(FSize const *const v, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(v, cv);
    glmc_vec3_normalize_to(cv, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_normalize_2(Vec3 const v) {
    trace_log_push(LOG_METADATA);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(v, cv);
    glmc_vec3_normalize_to(cv, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_one_1(FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cd = DEFAULT_INITIALIZATION;

    glmc_vec3_one(cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_one_2(void) {
    trace_log_push(LOG_METADATA);

    vec3 cd = DEFAULT_INITIALIZATION;

    glmc_vec3_one(cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_ortho_1(FSize const *const v, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(v, cv);
    glmc_vec3_ortho(cv, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_ortho_2(Vec3 const v) {
    trace_log_push(LOG_METADATA);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(v, cv);
    glmc_vec3_ortho(cv, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_proj_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(a, ca);
    _math_vec3_raw_to_cglm(b, cb);
    glmc_vec3_proj(ca, cb, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_proj_2(Vec3 const a, Vec3 const b) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(a, ca);
    _math_vec3_to_cglm(b, cb);
    glmc_vec3_proj(ca, cb, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_reflect_1(FSize const *const v, FSize const *const n, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "n", (void*) n);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cn = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(v, cv);
    _math_vec3_raw_to_cglm(n, cn);
    glmc_vec3_reflect(cv, cn, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_reflect_2(Vec3 const v, Vec3 const n) {
    trace_log_push(LOG_METADATA);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cn = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(v, cv);
    _math_vec3_to_cglm(n, cn);
    glmc_vec3_reflect(cv, cn, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

bool math_vec3_refract_1(FSize const *const v, FSize const *const n, FSize const eta, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "n", (void*) n);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cn = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(v, cv);
    _math_vec3_raw_to_cglm(n, cn);

    /* A NaN or non-finite eta makes cglm report a NaN vector as a SUCCESS; a non-positive one is
     * not a ratio of indices. All three are data, refused to false and the zeroed vector: the
     * bounded conversion yields 0.0f outside float range, and the float is then tested because an
     * F64 below float range arrives as 0 too. */
    float const ceta = _math_fsize_to_float_bounded(eta);
    bool const result = ceta > 0.0f && glmc_vec3_refract(cv, cn, ceta, cd);

    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();

    return result;
}

Vec3Refraction math_vec3_refract_2(Vec3 const v, Vec3 const n, FSize const eta) {
    trace_log_push(LOG_METADATA);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cn = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(v, cv);
    _math_vec3_to_cglm(n, cn);

    /* eta refused as in math_vec3_refract_1 (bounded conversion, then the float tested): { false, zero }. */
    float const ceta = _math_fsize_to_float_bounded(eta);
    bool const refracted = ceta > 0.0f && glmc_vec3_refract(cv, cn, ceta, cd);
    Vec3Refraction const result = { refracted, _math_vec3_from_cglm(cd) };

    trace_log_pop();

    return result;
}

void math_vec3_rotate_1(FSize const *const v, FSize const angle, FSize const *const axis, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "axis", (void*) axis);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 caxis = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(v, cv);
    _math_vec3_raw_to_cglm(axis, caxis);
    glmc_vec3_rotate(cv, (float) angle, caxis);
    _math_vec3_raw_from_cglm(cv, dest);

    trace_log_pop();
}

Vec3 math_vec3_rotate_2(Vec3 const v, FSize const angle, Vec3 const axis) {
    trace_log_push(LOG_METADATA);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 caxis = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(v, cv);
    _math_vec3_to_cglm(axis, caxis);
    glmc_vec3_rotate(cv, (float) angle, caxis);

    Vec3 const result = _math_vec3_from_cglm(cv);

    trace_log_pop();

    return result;
}

void math_vec3_rotate_m3_1(FSize const *const m, FSize const *const v, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "m", (void*) m);
    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat3 cm = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_mat3_raw_to_cglm(m, cm);
    _math_vec3_raw_to_cglm(v, cv);
    glmc_vec3_rotate_m3(cm, cv, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_rotate_m3_2(Mat3 const m, Vec3 const v) {
    trace_log_push(LOG_METADATA);

    mat3 cm = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_mat3_to_cglm(m, cm);
    _math_vec3_to_cglm(v, cv);
    glmc_vec3_rotate_m3(cm, cv, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_rotate_m4_1(FSize const *const m, FSize const *const v, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "m", (void*) m);
    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(m, cm);
    _math_vec3_raw_to_cglm(v, cv);
    glmc_vec3_rotate_m4(cm, cv, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_rotate_m4_2(Mat4 const m, Vec3 const v) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(m, cm);
    _math_vec3_to_cglm(v, cv);
    glmc_vec3_rotate_m4(cm, cv, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_scale_1(FSize const *const v, FSize const s, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(v, cv);
    glmc_vec3_scale(cv, (float) s, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_scale_2(Vec3 const v, FSize const s) {
    trace_log_push(LOG_METADATA);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(v, cv);
    glmc_vec3_scale(cv, (float) s, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_scale_as_1(FSize const *const v, FSize const s, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(v, cv);
    glmc_vec3_scale_as(cv, (float) s, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_scale_as_2(Vec3 const v, FSize const s) {
    trace_log_push(LOG_METADATA);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(v, cv);
    glmc_vec3_scale_as(cv, (float) s, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_sign_1(FSize const *const v, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(v, cv);
    glmc_vec3_sign(cv, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_sign_2(Vec3 const v) {
    trace_log_push(LOG_METADATA);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(v, cv);
    glmc_vec3_sign(cv, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_smoothinterp_1(FSize const *const from, FSize const *const to, FSize const t, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "from", (void*) from);
    error_check_null(LOG_METADATA, "to", (void*) to);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(from, ca);
    _math_vec3_raw_to_cglm(to, cb);
    glmc_vec3_smoothinterp(ca, cb, (float) t, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_smoothinterp_2(Vec3 const from, Vec3 const to, FSize const t) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(from, ca);
    _math_vec3_to_cglm(to, cb);
    glmc_vec3_smoothinterp(ca, cb, (float) t, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_smoothinterpc_1(FSize const *const from, FSize const *const to, FSize const t, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "from", (void*) from);
    error_check_null(LOG_METADATA, "to", (void*) to);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(from, ca);
    _math_vec3_raw_to_cglm(to, cb);
    glmc_vec3_smoothinterpc(ca, cb, (float) t, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_smoothinterpc_2(Vec3 const from, Vec3 const to, FSize const t) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(from, ca);
    _math_vec3_to_cglm(to, cb);
    glmc_vec3_smoothinterpc(ca, cb, (float) t, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_smoothstep_1(FSize const *const edge0, FSize const *const edge1, FSize const *const x, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "edge0", (void*) edge0);
    error_check_null(LOG_METADATA, "edge1", (void*) edge1);
    error_check_null(LOG_METADATA, "x", (void*) x);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ce0 = DEFAULT_INITIALIZATION;
    vec3 ce1 = DEFAULT_INITIALIZATION;
    vec3 cx = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(edge0, ce0);
    _math_vec3_raw_to_cglm(edge1, ce1);
    _math_vec3_raw_to_cglm(x, cx);
    glmc_vec3_smoothstep(ce0, ce1, cx, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_smoothstep_2(Vec3 const edge0, Vec3 const edge1, Vec3 const x) {
    trace_log_push(LOG_METADATA);

    vec3 ce0 = DEFAULT_INITIALIZATION;
    vec3 ce1 = DEFAULT_INITIALIZATION;
    vec3 cx = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(edge0, ce0);
    _math_vec3_to_cglm(edge1, ce1);
    _math_vec3_to_cglm(x, cx);
    glmc_vec3_smoothstep(ce0, ce1, cx, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_smoothstep_uni_1(FSize const edge0, FSize const edge1, FSize const *const x, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "x", (void*) x);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cx = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(x, cx);
    glmc_vec3_smoothstep_uni((float) edge0, (float) edge1, cx, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_smoothstep_uni_2(FSize const edge0, FSize const edge1, Vec3 const x) {
    trace_log_push(LOG_METADATA);

    vec3 cx = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(x, cx);
    glmc_vec3_smoothstep_uni((float) edge0, (float) edge1, cx, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_sqrt_1(FSize const *const v, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(v, cv);
    glmc_vec3_sqrt(cv, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_sqrt_2(Vec3 const v) {
    trace_log_push(LOG_METADATA);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(v, cv);
    glmc_vec3_sqrt(cv, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_step_1(FSize const *const edge, FSize const *const x, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "edge", (void*) edge);
    error_check_null(LOG_METADATA, "x", (void*) x);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ce = DEFAULT_INITIALIZATION;
    vec3 cx = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(edge, ce);
    _math_vec3_raw_to_cglm(x, cx);
    glmc_vec3_step(ce, cx, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_step_2(Vec3 const edge, Vec3 const x) {
    trace_log_push(LOG_METADATA);

    vec3 ce = DEFAULT_INITIALIZATION;
    vec3 cx = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(edge, ce);
    _math_vec3_to_cglm(x, cx);
    glmc_vec3_step(ce, cx, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_steps_1(FSize const edge, FSize const *const x, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "x", (void*) x);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cx = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(x, cx);
    glmc_vec3_steps((float) edge, cx, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_steps_2(FSize const edge, Vec3 const x) {
    trace_log_push(LOG_METADATA);

    vec3 cx = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(x, cx);
    glmc_vec3_steps((float) edge, cx, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_stepr_1(FSize const *const edge, FSize const x, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "edge", (void*) edge);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ce = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(edge, ce);
    glmc_vec3_stepr(ce, (float) x, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_stepr_2(Vec3 const edge, FSize const x) {
    trace_log_push(LOG_METADATA);

    vec3 ce = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(edge, ce);
    glmc_vec3_stepr(ce, (float) x, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_sub_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(a, ca);
    _math_vec3_raw_to_cglm(b, cb);
    glmc_vec3_sub(ca, cb, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_sub_2(Vec3 const a, Vec3 const b) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(a, ca);
    _math_vec3_to_cglm(b, cb);
    glmc_vec3_sub(ca, cb, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_subadd_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(a, ca);
    _math_vec3_raw_to_cglm(b, cb);
    _math_vec3_raw_to_cglm(dest, cd);
    glmc_vec3_subadd(ca, cb, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_subadd_2(Vec3 const a, Vec3 const b, Vec3 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(a, ca);
    _math_vec3_to_cglm(b, cb);
    _math_vec3_to_cglm(accumulator, cd);
    glmc_vec3_subadd(ca, cb, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_subs_1(FSize const *const v, FSize const s, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(v, cv);
    glmc_vec3_subs(cv, (float) s, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_subs_2(Vec3 const v, FSize const s) {
    trace_log_push(LOG_METADATA);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(v, cv);
    glmc_vec3_subs(cv, (float) s, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_subsub_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(a, ca);
    _math_vec3_raw_to_cglm(b, cb);
    _math_vec3_raw_to_cglm(dest, cd);
    glmc_vec3_subsub(ca, cb, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_subsub_2(Vec3 const a, Vec3 const b, Vec3 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(a, ca);
    _math_vec3_to_cglm(b, cb);
    _math_vec3_to_cglm(accumulator, cd);
    glmc_vec3_subsub(ca, cb, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_swizzle_1(FSize const *const v, ISize const mask, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(v, cv);

    if (_math_swizzle_mask_fits(mask, 3)) {
        glmc_vec3_swizzle(cv, (int) mask, cd);
    }

    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_swizzle_2(Vec3 const v, ISize const mask) {
    trace_log_push(LOG_METADATA);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(v, cv);

    if (_math_swizzle_mask_fits(mask, 3)) {
        glmc_vec3_swizzle(cv, (int) mask, cd);
    }

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_vec3_1(FSize const *const v, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(v, cv);
    glmc_vec3(cv, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_vec3_2(Vec4 const v) {
    trace_log_push(LOG_METADATA);

    vec4 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(v, cv);
    glmc_vec3(cv, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec3_zero_1(FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cd = DEFAULT_INITIALIZATION;

    glmc_vec3_zero(cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec3_zero_2(void) {
    trace_log_push(LOG_METADATA);

    vec3 cd = DEFAULT_INITIALIZATION;

    glmc_vec3_zero(cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}