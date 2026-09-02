/*
 * vec4.c - 4D vector operations for the CFW math module.
 *
 * See vec4.h for API documentation and usage examples.
 */

#include <math/vec4.h>

/*==============================================================================
 * MARK: - Vec4 API
 *============================================================================*/

void math_vec4_abs_1(FSize const *const v, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 cv = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(v, cv);
    glmc_vec4_abs(cv, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_abs_2(Vec4 const v) {
    trace_log_push(LOG_METADATA);

    vec4 cv = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(v, cv);
    glmc_vec4_abs(cv, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_add_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(a, ca);
    _math_vec4_raw_to_cglm(b, cb);
    glmc_vec4_add(ca, cb, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_add_2(Vec4 const a, Vec4 const b) {
    trace_log_push(LOG_METADATA);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(a, ca);
    _math_vec4_to_cglm(b, cb);
    glmc_vec4_add(ca, cb, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_addadd_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(a, ca);
    _math_vec4_raw_to_cglm(b, cb);
    _math_vec4_raw_to_cglm(dest, cd);
    glmc_vec4_addadd(ca, cb, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_addadd_2(Vec4 const a, Vec4 const b, Vec4 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(a, ca);
    _math_vec4_to_cglm(b, cb);
    _math_vec4_to_cglm(accumulator, cd);
    glmc_vec4_addadd(ca, cb, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_adds_1(FSize const *const v, FSize const s, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 cv = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(v, cv);
    glmc_vec4_adds(cv, (float) s, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_adds_2(Vec4 const v, FSize const s) {
    trace_log_push(LOG_METADATA);

    vec4 cv = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(v, cv);
    glmc_vec4_adds(cv, (float) s, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_addsub_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(a, ca);
    _math_vec4_raw_to_cglm(b, cb);
    _math_vec4_raw_to_cglm(dest, cd);
    glmc_vec4_addsub(ca, cb, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_addsub_2(Vec4 const a, Vec4 const b, Vec4 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(a, ca);
    _math_vec4_to_cglm(b, cb);
    _math_vec4_to_cglm(accumulator, cd);
    glmc_vec4_addsub(ca, cb, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_broadcast_1(FSize const val, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 cd = DEFAULT_INITIALIZATION;

    glmc_vec4_broadcast((float) val, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_broadcast_2(FSize const val) {
    trace_log_push(LOG_METADATA);

    vec4 cd = DEFAULT_INITIALIZATION;

    glmc_vec4_broadcast((float) val, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_clamp_1(FSize const *const v, FSize const minval, FSize const maxval, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 cv = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(v, cv);
    glmc_vec4_clamp(cv, (float) minval, (float) maxval);
    _math_vec4_raw_from_cglm(cv, dest);

    trace_log_pop();
}

Vec4 math_vec4_clamp_2(Vec4 const v, FSize const minval, FSize const maxval) {
    trace_log_push(LOG_METADATA);

    vec4 cv = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(v, cv);
    glmc_vec4_clamp(cv, (float) minval, (float) maxval);

    Vec4 const result = _math_vec4_from_cglm(cv);

    trace_log_pop();

    return result;
}

void math_vec4_copy_1(FSize const *const a, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(a, ca);
    glmc_vec4_copy(ca, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_copy_2(Vec4 const a) {
    trace_log_push(LOG_METADATA);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(a, ca);
    glmc_vec4_copy(ca, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_copy3_1(FSize const *const a, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(a, ca);
    glmc_vec4_copy3(ca, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_vec4_copy3_2(Vec4 const a) {
    trace_log_push(LOG_METADATA);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(a, ca);
    glmc_vec4_copy3(ca, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_cubic_1(FSize const s, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 cd = DEFAULT_INITIALIZATION;

    glmc_vec4_cubic((float) s, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_cubic_2(FSize const s) {
    trace_log_push(LOG_METADATA);

    vec4 cd = DEFAULT_INITIALIZATION;

    glmc_vec4_cubic((float) s, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

FSize math_vec4_distance_1(FSize const *const a, FSize const *const b) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(a, ca);
    _math_vec4_raw_to_cglm(b, cb);

    FSize const result = (FSize) glmc_vec4_distance(ca, cb);

    trace_log_pop();

    return result;
}

FSize math_vec4_distance_2(Vec4 const a, Vec4 const b) {
    trace_log_push(LOG_METADATA);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(a, ca);
    _math_vec4_to_cglm(b, cb);

    FSize const result = (FSize) glmc_vec4_distance(ca, cb);

    trace_log_pop();

    return result;
}

FSize math_vec4_distance2_1(FSize const *const a, FSize const *const b) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(a, ca);
    _math_vec4_raw_to_cglm(b, cb);

    FSize const result = (FSize) glmc_vec4_distance2(ca, cb);

    trace_log_pop();

    return result;
}

FSize math_vec4_distance2_2(Vec4 const a, Vec4 const b) {
    trace_log_push(LOG_METADATA);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(a, ca);
    _math_vec4_to_cglm(b, cb);

    FSize const result = (FSize) glmc_vec4_distance2(ca, cb);

    trace_log_pop();

    return result;
}

void math_vec4_div_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(a, ca);
    _math_vec4_raw_to_cglm(b, cb);
    glmc_vec4_div(ca, cb, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_div_2(Vec4 const a, Vec4 const b) {
    trace_log_push(LOG_METADATA);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(a, ca);
    _math_vec4_to_cglm(b, cb);
    glmc_vec4_div(ca, cb, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_divs_1(FSize const *const v, FSize const s, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 cv = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(v, cv);
    glmc_vec4_divs(cv, (float) s, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_divs_2(Vec4 const v, FSize const s) {
    trace_log_push(LOG_METADATA);

    vec4 cv = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(v, cv);
    glmc_vec4_divs(cv, (float) s, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

FSize math_vec4_dot_1(FSize const *const a, FSize const *const b) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(a, ca);
    _math_vec4_raw_to_cglm(b, cb);

    FSize const result = (FSize) glmc_vec4_dot(ca, cb);

    trace_log_pop();

    return result;
}

FSize math_vec4_dot_2(Vec4 const a, Vec4 const b) {
    trace_log_push(LOG_METADATA);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(a, ca);
    _math_vec4_to_cglm(b, cb);

    FSize const result = (FSize) glmc_vec4_dot(ca, cb);

    trace_log_pop();

    return result;
}

bool math_vec4_eq_1(FSize const *const v, FSize const val) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);

    vec4 cv = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(v, cv);

    bool const result = glmc_vec4_eq(cv, (float) val);

    trace_log_pop();

    return result;
}

bool math_vec4_eq_2(Vec4 const v, FSize const val) {
    trace_log_push(LOG_METADATA);

    vec4 cv = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(v, cv);

    bool const result = glmc_vec4_eq(cv, (float) val);

    trace_log_pop();

    return result;
}

bool math_vec4_eq_all_1(FSize const *const v) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);

    vec4 cv = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(v, cv);

    bool const result = glmc_vec4_eq_all(cv);

    trace_log_pop();

    return result;
}

bool math_vec4_eq_all_2(Vec4 const v) {
    trace_log_push(LOG_METADATA);

    vec4 cv = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(v, cv);

    bool const result = glmc_vec4_eq_all(cv);

    trace_log_pop();

    return result;
}

bool math_vec4_eq_eps_1(FSize const *const v, FSize const val) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);

    vec4 cv = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(v, cv);

    bool const result = glmc_vec4_eq_eps(cv, (float) val);

    trace_log_pop();

    return result;
}

bool math_vec4_eq_eps_2(Vec4 const v, FSize const val) {
    trace_log_push(LOG_METADATA);

    vec4 cv = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(v, cv);

    bool const result = glmc_vec4_eq_eps(cv, (float) val);

    trace_log_pop();

    return result;
}

bool math_vec4_eqv_1(FSize const *const a, FSize const *const b) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(a, ca);
    _math_vec4_raw_to_cglm(b, cb);

    bool const result = glmc_vec4_eqv(ca, cb);

    trace_log_pop();

    return result;
}

bool math_vec4_eqv_2(Vec4 const a, Vec4 const b) {
    trace_log_push(LOG_METADATA);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(a, ca);
    _math_vec4_to_cglm(b, cb);

    bool const result = glmc_vec4_eqv(ca, cb);

    trace_log_pop();

    return result;
}

bool math_vec4_eqv_eps_1(FSize const *const a, FSize const *const b) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(a, ca);
    _math_vec4_raw_to_cglm(b, cb);

    bool const result = glmc_vec4_eqv_eps(ca, cb);

    trace_log_pop();

    return result;
}

bool math_vec4_eqv_eps_2(Vec4 const a, Vec4 const b) {
    trace_log_push(LOG_METADATA);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(a, ca);
    _math_vec4_to_cglm(b, cb);

    bool const result = glmc_vec4_eqv_eps(ca, cb);

    trace_log_pop();

    return result;
}

void math_vec4_fill_1(FSize const val, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 cd = DEFAULT_INITIALIZATION;

    glmc_vec4_fill(cd, (float) val);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_fill_2(FSize const val) {
    trace_log_push(LOG_METADATA);

    vec4 cd = DEFAULT_INITIALIZATION;

    glmc_vec4_fill(cd, (float) val);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_floor_1(FSize const *const v, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 cv = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(v, cv);
    glmc_vec4_floor(cv, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_floor_2(Vec4 const v) {
    trace_log_push(LOG_METADATA);

    vec4 cv = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(v, cv);
    glmc_vec4_floor(cv, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_fract_1(FSize const *const v, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 cv = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(v, cv);
    glmc_vec4_fract(cv, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_fract_2(Vec4 const v) {
    trace_log_push(LOG_METADATA);

    vec4 cv = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(v, cv);
    glmc_vec4_fract(cv, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

FSize math_vec4_hadd_1(FSize const *const v) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);

    vec4 cv = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(v, cv);

    FSize const result = (FSize) glmc_vec4_hadd(cv);

    trace_log_pop();

    return result;
}

FSize math_vec4_hadd_2(Vec4 const v) {
    trace_log_push(LOG_METADATA);

    vec4 cv = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(v, cv);

    FSize const result = (FSize) glmc_vec4_hadd(cv);

    trace_log_pop();

    return result;
}

bool math_vec4_isinf_1(FSize const *const v) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);

    vec4 cv = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(v, cv);

    bool const result = glmc_vec4_isinf(cv);

    trace_log_pop();

    return result;
}

bool math_vec4_isinf_2(Vec4 const v) {
    trace_log_push(LOG_METADATA);

    vec4 cv = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(v, cv);

    bool const result = glmc_vec4_isinf(cv);

    trace_log_pop();

    return result;
}

bool math_vec4_isnan_1(FSize const *const v) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);

    vec4 cv = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(v, cv);

    bool const result = glmc_vec4_isnan(cv);

    trace_log_pop();

    return result;
}

bool math_vec4_isnan_2(Vec4 const v) {
    trace_log_push(LOG_METADATA);

    vec4 cv = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(v, cv);

    bool const result = glmc_vec4_isnan(cv);

    trace_log_pop();

    return result;
}

bool math_vec4_isvalid_1(FSize const *const v) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);

    vec4 cv = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(v, cv);

    bool const result = glmc_vec4_isvalid(cv);

    trace_log_pop();

    return result;
}

bool math_vec4_isvalid_2(Vec4 const v) {
    trace_log_push(LOG_METADATA);

    vec4 cv = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(v, cv);

    bool const result = glmc_vec4_isvalid(cv);

    trace_log_pop();

    return result;
}

void math_vec4_lerp_1(FSize const *const from, FSize const *const to, FSize const t, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "from", (void*) from);
    error_check_null(LOG_METADATA, "to", (void*) to);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(from, ca);
    _math_vec4_raw_to_cglm(to, cb);
    glmc_vec4_lerp(ca, cb, (float) t, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_lerp_2(Vec4 const from, Vec4 const to, FSize const t) {
    trace_log_push(LOG_METADATA);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(from, ca);
    _math_vec4_to_cglm(to, cb);
    glmc_vec4_lerp(ca, cb, (float) t, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_lerpc_1(FSize const *const from, FSize const *const to, FSize const t, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "from", (void*) from);
    error_check_null(LOG_METADATA, "to", (void*) to);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(from, ca);
    _math_vec4_raw_to_cglm(to, cb);
    glmc_vec4_lerpc(ca, cb, (float) t, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_lerpc_2(Vec4 const from, Vec4 const to, FSize const t) {
    trace_log_push(LOG_METADATA);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(from, ca);
    _math_vec4_to_cglm(to, cb);
    glmc_vec4_lerpc(ca, cb, (float) t, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_make_1(FSize const *const src, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "src", (void*) src);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    float cs[4] = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(src, cs);
    glmc_vec4_make(cs, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_make_2(FSize const *const src) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "src", (void*) src);

    float cs[4] = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(src, cs);
    glmc_vec4_make(cs, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

FSize math_vec4_max_1(FSize const *const v) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);

    vec4 cv = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(v, cv);

    FSize const result = (FSize) glmc_vec4_max(cv);

    trace_log_pop();

    return result;
}

FSize math_vec4_max_2(Vec4 const v) {
    trace_log_push(LOG_METADATA);

    vec4 cv = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(v, cv);

    FSize const result = (FSize) glmc_vec4_max(cv);

    trace_log_pop();

    return result;
}

void math_vec4_maxadd_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(a, ca);
    _math_vec4_raw_to_cglm(b, cb);
    _math_vec4_raw_to_cglm(dest, cd);
    glmc_vec4_maxadd(ca, cb, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_maxadd_2(Vec4 const a, Vec4 const b, Vec4 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(a, ca);
    _math_vec4_to_cglm(b, cb);
    _math_vec4_to_cglm(accumulator, cd);
    glmc_vec4_maxadd(ca, cb, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_maxsub_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(a, ca);
    _math_vec4_raw_to_cglm(b, cb);
    _math_vec4_raw_to_cglm(dest, cd);
    glmc_vec4_maxsub(ca, cb, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_maxsub_2(Vec4 const a, Vec4 const b, Vec4 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(a, ca);
    _math_vec4_to_cglm(b, cb);
    _math_vec4_to_cglm(accumulator, cd);
    glmc_vec4_maxsub(ca, cb, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_maxv_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(a, ca);
    _math_vec4_raw_to_cglm(b, cb);
    glmc_vec4_maxv(ca, cb, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_maxv_2(Vec4 const a, Vec4 const b) {
    trace_log_push(LOG_METADATA);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(a, ca);
    _math_vec4_to_cglm(b, cb);
    glmc_vec4_maxv(ca, cb, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

FSize math_vec4_min_1(FSize const *const v) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);

    vec4 cv = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(v, cv);

    FSize const result = (FSize) glmc_vec4_min(cv);

    trace_log_pop();

    return result;
}

FSize math_vec4_min_2(Vec4 const v) {
    trace_log_push(LOG_METADATA);

    vec4 cv = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(v, cv);

    FSize const result = (FSize) glmc_vec4_min(cv);

    trace_log_pop();

    return result;
}

void math_vec4_minadd_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(a, ca);
    _math_vec4_raw_to_cglm(b, cb);
    _math_vec4_raw_to_cglm(dest, cd);
    glmc_vec4_minadd(ca, cb, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_minadd_2(Vec4 const a, Vec4 const b, Vec4 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(a, ca);
    _math_vec4_to_cglm(b, cb);
    _math_vec4_to_cglm(accumulator, cd);
    glmc_vec4_minadd(ca, cb, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_minsub_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(a, ca);
    _math_vec4_raw_to_cglm(b, cb);
    _math_vec4_raw_to_cglm(dest, cd);
    glmc_vec4_minsub(ca, cb, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_minsub_2(Vec4 const a, Vec4 const b, Vec4 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(a, ca);
    _math_vec4_to_cglm(b, cb);
    _math_vec4_to_cglm(accumulator, cd);
    glmc_vec4_minsub(ca, cb, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_minv_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(a, ca);
    _math_vec4_raw_to_cglm(b, cb);
    glmc_vec4_minv(ca, cb, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_minv_2(Vec4 const a, Vec4 const b) {
    trace_log_push(LOG_METADATA);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(a, ca);
    _math_vec4_to_cglm(b, cb);
    glmc_vec4_minv(ca, cb, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_mods_1(FSize const *const v, FSize const s, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 cv = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(v, cv);
    glmc_vec4_mods(cv, (float) s, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_mods_2(Vec4 const v, FSize const s) {
    trace_log_push(LOG_METADATA);

    vec4 cv = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(v, cv);
    glmc_vec4_mods(cv, (float) s, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_mul_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(a, ca);
    _math_vec4_raw_to_cglm(b, cb);
    glmc_vec4_mul(ca, cb, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_mul_2(Vec4 const a, Vec4 const b) {
    trace_log_push(LOG_METADATA);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(a, ca);
    _math_vec4_to_cglm(b, cb);
    glmc_vec4_mul(ca, cb, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_muladd_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(a, ca);
    _math_vec4_raw_to_cglm(b, cb);
    _math_vec4_raw_to_cglm(dest, cd);
    glmc_vec4_muladd(ca, cb, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_muladd_2(Vec4 const a, Vec4 const b, Vec4 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(a, ca);
    _math_vec4_to_cglm(b, cb);
    _math_vec4_to_cglm(accumulator, cd);
    glmc_vec4_muladd(ca, cb, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_muladds_1(FSize const *const a, FSize const s, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(a, ca);
    _math_vec4_raw_to_cglm(dest, cd);
    glmc_vec4_muladds(ca, (float) s, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_muladds_2(Vec4 const a, FSize const s, Vec4 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(a, ca);
    _math_vec4_to_cglm(accumulator, cd);
    glmc_vec4_muladds(ca, (float) s, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_mulsub_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(a, ca);
    _math_vec4_raw_to_cglm(b, cb);
    _math_vec4_raw_to_cglm(dest, cd);
    glmc_vec4_mulsub(ca, cb, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_mulsub_2(Vec4 const a, Vec4 const b, Vec4 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(a, ca);
    _math_vec4_to_cglm(b, cb);
    _math_vec4_to_cglm(accumulator, cd);
    glmc_vec4_mulsub(ca, cb, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_mulsubs_1(FSize const *const a, FSize const s, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(a, ca);
    _math_vec4_raw_to_cglm(dest, cd);
    glmc_vec4_mulsubs(ca, (float) s, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_mulsubs_2(Vec4 const a, FSize const s, Vec4 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(a, ca);
    _math_vec4_to_cglm(accumulator, cd);
    glmc_vec4_mulsubs(ca, (float) s, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_mulv_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(a, ca);
    _math_vec4_raw_to_cglm(b, cb);
    glmc_vec4_mulv(ca, cb, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_mulv_2(Vec4 const a, Vec4 const b) {
    trace_log_push(LOG_METADATA);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(a, ca);
    _math_vec4_to_cglm(b, cb);
    glmc_vec4_mulv(ca, cb, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_negate_1(FSize const *const v, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 cv = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(v, cv);
    glmc_vec4_negate_to(cv, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_negate_2(Vec4 const v) {
    trace_log_push(LOG_METADATA);

    vec4 cv = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(v, cv);
    glmc_vec4_negate_to(cv, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

FSize math_vec4_norm_1(FSize const *const v) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);

    vec4 cv = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(v, cv);

    FSize const result = (FSize) glmc_vec4_norm(cv);

    trace_log_pop();

    return result;
}

FSize math_vec4_norm_2(Vec4 const v) {
    trace_log_push(LOG_METADATA);

    vec4 cv = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(v, cv);

    FSize const result = (FSize) glmc_vec4_norm(cv);

    trace_log_pop();

    return result;
}

FSize math_vec4_norm2_1(FSize const *const v) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);

    vec4 cv = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(v, cv);

    FSize const result = (FSize) glmc_vec4_norm2(cv);

    trace_log_pop();

    return result;
}

FSize math_vec4_norm2_2(Vec4 const v) {
    trace_log_push(LOG_METADATA);

    vec4 cv = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(v, cv);

    FSize const result = (FSize) glmc_vec4_norm2(cv);

    trace_log_pop();

    return result;
}

FSize math_vec4_norm_inf_1(FSize const *const v) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);

    vec4 cv = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(v, cv);

    FSize const result = (FSize) glmc_vec4_norm_inf(cv);

    trace_log_pop();

    return result;
}

FSize math_vec4_norm_inf_2(Vec4 const v) {
    trace_log_push(LOG_METADATA);

    vec4 cv = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(v, cv);

    FSize const result = (FSize) glmc_vec4_norm_inf(cv);

    trace_log_pop();

    return result;
}

FSize math_vec4_norm_one_1(FSize const *const v) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);

    vec4 cv = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(v, cv);

    FSize const result = (FSize) glmc_vec4_norm_one(cv);

    trace_log_pop();

    return result;
}

FSize math_vec4_norm_one_2(Vec4 const v) {
    trace_log_push(LOG_METADATA);

    vec4 cv = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(v, cv);

    FSize const result = (FSize) glmc_vec4_norm_one(cv);

    trace_log_pop();

    return result;
}

void math_vec4_normalize_1(FSize const *const v, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 cv = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(v, cv);
    glmc_vec4_normalize_to(cv, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_normalize_2(Vec4 const v) {
    trace_log_push(LOG_METADATA);

    vec4 cv = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(v, cv);
    glmc_vec4_normalize_to(cv, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_one_1(FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 cd = DEFAULT_INITIALIZATION;

    glmc_vec4_one(cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_one_2(void) {
    trace_log_push(LOG_METADATA);

    vec4 cd = DEFAULT_INITIALIZATION;

    glmc_vec4_one(cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_reflect_1(FSize const *const v, FSize const *const n, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "n", (void*) n);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 cv = DEFAULT_INITIALIZATION;
    vec4 cn = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(v, cv);
    _math_vec4_raw_to_cglm(n, cn);
    glmc_vec4_reflect(cv, cn, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_reflect_2(Vec4 const v, Vec4 const n) {
    trace_log_push(LOG_METADATA);

    vec4 cv = DEFAULT_INITIALIZATION;
    vec4 cn = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(v, cv);
    _math_vec4_to_cglm(n, cn);
    glmc_vec4_reflect(cv, cn, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

bool math_vec4_refract_1(FSize const *const v, FSize const *const n, FSize const eta, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "n", (void*) n);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 cv = DEFAULT_INITIALIZATION;
    vec4 cn = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(v, cv);
    _math_vec4_raw_to_cglm(n, cn);

    /* A NaN or non-finite eta makes cglm report a NaN vector as a SUCCESS; a non-positive one is
     * not a ratio of indices. All three are data, refused to false and the zeroed vector: the
     * bounded conversion yields 0.0f outside float range, and the float is then tested because an
     * F64 below float range arrives as 0 too. */
    float const ceta = _math_fsize_to_float_bounded(eta);
    bool const result = ceta > 0.0f && glmc_vec4_refract(cv, cn, ceta, cd);

    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();

    return result;
}

Vec4Refraction math_vec4_refract_2(Vec4 const v, Vec4 const n, FSize const eta) {
    trace_log_push(LOG_METADATA);

    vec4 cv = DEFAULT_INITIALIZATION;
    vec4 cn = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(v, cv);
    _math_vec4_to_cglm(n, cn);

    /* eta refused as in math_vec4_refract_1 (bounded conversion, then the float tested): { false, zero }. */
    float const ceta = _math_fsize_to_float_bounded(eta);
    bool const refracted = ceta > 0.0f && glmc_vec4_refract(cv, cn, ceta, cd);
    Vec4Refraction const result = { refracted, _math_vec4_from_cglm(cd) };

    trace_log_pop();

    return result;
}

void math_vec4_scale_1(FSize const *const v, FSize const s, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 cv = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(v, cv);
    glmc_vec4_scale(cv, (float) s, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_scale_2(Vec4 const v, FSize const s) {
    trace_log_push(LOG_METADATA);

    vec4 cv = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(v, cv);
    glmc_vec4_scale(cv, (float) s, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_scale_as_1(FSize const *const v, FSize const s, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 cv = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(v, cv);
    glmc_vec4_scale_as(cv, (float) s, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_scale_as_2(Vec4 const v, FSize const s) {
    trace_log_push(LOG_METADATA);

    vec4 cv = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(v, cv);
    glmc_vec4_scale_as(cv, (float) s, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_sign_1(FSize const *const v, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 cv = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(v, cv);
    glmc_vec4_sign(cv, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_sign_2(Vec4 const v) {
    trace_log_push(LOG_METADATA);

    vec4 cv = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(v, cv);
    glmc_vec4_sign(cv, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_smoothinterp_1(FSize const *const from, FSize const *const to, FSize const t, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "from", (void*) from);
    error_check_null(LOG_METADATA, "to", (void*) to);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(from, ca);
    _math_vec4_raw_to_cglm(to, cb);
    glmc_vec4_smoothinterp(ca, cb, (float) t, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_smoothinterp_2(Vec4 const from, Vec4 const to, FSize const t) {
    trace_log_push(LOG_METADATA);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(from, ca);
    _math_vec4_to_cglm(to, cb);
    glmc_vec4_smoothinterp(ca, cb, (float) t, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_smoothinterpc_1(FSize const *const from, FSize const *const to, FSize const t, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "from", (void*) from);
    error_check_null(LOG_METADATA, "to", (void*) to);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(from, ca);
    _math_vec4_raw_to_cglm(to, cb);
    glmc_vec4_smoothinterpc(ca, cb, (float) t, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_smoothinterpc_2(Vec4 const from, Vec4 const to, FSize const t) {
    trace_log_push(LOG_METADATA);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(from, ca);
    _math_vec4_to_cglm(to, cb);
    glmc_vec4_smoothinterpc(ca, cb, (float) t, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_smoothstep_1(FSize const *const edge0, FSize const *const edge1, FSize const *const x, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "edge0", (void*) edge0);
    error_check_null(LOG_METADATA, "edge1", (void*) edge1);
    error_check_null(LOG_METADATA, "x", (void*) x);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 c0 = DEFAULT_INITIALIZATION;
    vec4 c1 = DEFAULT_INITIALIZATION;
    vec4 cx = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(edge0, c0);
    _math_vec4_raw_to_cglm(edge1, c1);
    _math_vec4_raw_to_cglm(x, cx);
    glmc_vec4_smoothstep(c0, c1, cx, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_smoothstep_2(Vec4 const edge0, Vec4 const edge1, Vec4 const x) {
    trace_log_push(LOG_METADATA);

    vec4 c0 = DEFAULT_INITIALIZATION;
    vec4 c1 = DEFAULT_INITIALIZATION;
    vec4 cx = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(edge0, c0);
    _math_vec4_to_cglm(edge1, c1);
    _math_vec4_to_cglm(x, cx);
    glmc_vec4_smoothstep(c0, c1, cx, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_smoothstep_uni_1(FSize const edge0, FSize const edge1, FSize const *const x, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "x", (void*) x);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 cx = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(x, cx);
    glmc_vec4_smoothstep_uni((float) edge0, (float) edge1, cx, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_smoothstep_uni_2(FSize const edge0, FSize const edge1, Vec4 const x) {
    trace_log_push(LOG_METADATA);

    vec4 cx = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(x, cx);
    glmc_vec4_smoothstep_uni((float) edge0, (float) edge1, cx, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_sqrt_1(FSize const *const v, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 cv = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(v, cv);
    glmc_vec4_sqrt(cv, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_sqrt_2(Vec4 const v) {
    trace_log_push(LOG_METADATA);

    vec4 cv = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(v, cv);
    glmc_vec4_sqrt(cv, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_step_1(FSize const *const edge, FSize const *const x, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "edge", (void*) edge);
    error_check_null(LOG_METADATA, "x", (void*) x);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 ce = DEFAULT_INITIALIZATION;
    vec4 cx = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(edge, ce);
    _math_vec4_raw_to_cglm(x, cx);
    glmc_vec4_step(ce, cx, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_step_2(Vec4 const edge, Vec4 const x) {
    trace_log_push(LOG_METADATA);

    vec4 ce = DEFAULT_INITIALIZATION;
    vec4 cx = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(edge, ce);
    _math_vec4_to_cglm(x, cx);
    glmc_vec4_step(ce, cx, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_steps_1(FSize const edge, FSize const *const x, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "x", (void*) x);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 cx = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(x, cx);
    glmc_vec4_steps((float) edge, cx, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_steps_2(FSize const edge, Vec4 const x) {
    trace_log_push(LOG_METADATA);

    vec4 cx = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(x, cx);
    glmc_vec4_steps((float) edge, cx, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_stepr_1(FSize const *const edge, FSize const x, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "edge", (void*) edge);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 ce = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(edge, ce);
    glmc_vec4_stepr(ce, (float) x, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_stepr_2(Vec4 const edge, FSize const x) {
    trace_log_push(LOG_METADATA);

    vec4 ce = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(edge, ce);
    glmc_vec4_stepr(ce, (float) x, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_sub_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(a, ca);
    _math_vec4_raw_to_cglm(b, cb);
    glmc_vec4_sub(ca, cb, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_sub_2(Vec4 const a, Vec4 const b) {
    trace_log_push(LOG_METADATA);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(a, ca);
    _math_vec4_to_cglm(b, cb);
    glmc_vec4_sub(ca, cb, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_subadd_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(a, ca);
    _math_vec4_raw_to_cglm(b, cb);
    _math_vec4_raw_to_cglm(dest, cd);
    glmc_vec4_subadd(ca, cb, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_subadd_2(Vec4 const a, Vec4 const b, Vec4 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(a, ca);
    _math_vec4_to_cglm(b, cb);
    _math_vec4_to_cglm(accumulator, cd);
    glmc_vec4_subadd(ca, cb, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_subs_1(FSize const *const v, FSize const s, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 cv = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(v, cv);
    glmc_vec4_subs(cv, (float) s, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_subs_2(Vec4 const v, FSize const s) {
    trace_log_push(LOG_METADATA);

    vec4 cv = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(v, cv);
    glmc_vec4_subs(cv, (float) s, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_subsub_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(a, ca);
    _math_vec4_raw_to_cglm(b, cb);
    _math_vec4_raw_to_cglm(dest, cd);
    glmc_vec4_subsub(ca, cb, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_subsub_2(Vec4 const a, Vec4 const b, Vec4 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cb = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(a, ca);
    _math_vec4_to_cglm(b, cb);
    _math_vec4_to_cglm(accumulator, cd);
    glmc_vec4_subsub(ca, cb, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_swizzle_1(FSize const *const v, ISize const mask, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 cv = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(v, cv);

    if (_math_swizzle_mask_fits(mask, 4)) {
        glmc_vec4_swizzle(cv, (int) mask, cd);
    }

    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_swizzle_2(Vec4 const v, ISize const mask) {
    trace_log_push(LOG_METADATA);

    vec4 cv = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(v, cv);

    if (_math_swizzle_mask_fits(mask, 4)) {
        glmc_vec4_swizzle(cv, (int) mask, cd);
    }

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_ucopy_1(FSize const *const a, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(a, ca);
    glmc_vec4_ucopy(ca, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_ucopy_2(Vec4 const a) {
    trace_log_push(LOG_METADATA);

    vec4 ca = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(a, ca);
    glmc_vec4_ucopy(ca, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_vec4_1(FSize const *const v, FSize const last, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(v, cv);
    glmc_vec4(cv, (float) last, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_vec4_2(Vec3 const v, FSize const last) {
    trace_log_push(LOG_METADATA);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(v, cv);
    glmc_vec4(cv, (float) last, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec4_zero_1(FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 cd = DEFAULT_INITIALIZATION;

    glmc_vec4_zero(cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_vec4_zero_2(void) {
    trace_log_push(LOG_METADATA);

    vec4 cd = DEFAULT_INITIALIZATION;

    glmc_vec4_zero(cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}