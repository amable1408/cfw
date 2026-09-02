/*
 * vec2.c - 2D vector operations for the CFW math module.
 *
 * See vec2.h for API documentation and usage examples.
 */

#include <math/vec2.h>

/*==============================================================================
 * MARK: - Vec2 API
 *============================================================================*/

void math_vec2_abs_1(FSize const *const v, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 cv = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(v, cv);
    glmc_vec2_abs(cv, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_abs_2(Vec2 const v) {
    trace_log_push(LOG_METADATA);

    vec2 cv = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(v, cv);
    glmc_vec2_abs(cv, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_add_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(a, ca);
    _math_vec2_raw_to_cglm(b, cb);
    glmc_vec2_add(ca, cb, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_add_2(Vec2 const a, Vec2 const b) {
    trace_log_push(LOG_METADATA);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(a, ca);
    _math_vec2_to_cglm(b, cb);
    glmc_vec2_add(ca, cb, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_addadd_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(a, ca);
    _math_vec2_raw_to_cglm(b, cb);
    _math_vec2_raw_to_cglm(dest, cd);
    glmc_vec2_addadd(ca, cb, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_addadd_2(Vec2 const a, Vec2 const b, Vec2 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(a, ca);
    _math_vec2_to_cglm(b, cb);
    _math_vec2_to_cglm(accumulator, cd);
    glmc_vec2_addadd(ca, cb, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_adds_1(FSize const *const v, FSize const s, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 cv = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(v, cv);
    glmc_vec2_adds(cv, (float) s, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_adds_2(Vec2 const v, FSize const s) {
    trace_log_push(LOG_METADATA);

    vec2 cv = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(v, cv);
    glmc_vec2_adds(cv, (float) s, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_addsub_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(a, ca);
    _math_vec2_raw_to_cglm(b, cb);
    _math_vec2_raw_to_cglm(dest, cd);
    glmc_vec2_addsub(ca, cb, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_addsub_2(Vec2 const a, Vec2 const b, Vec2 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(a, ca);
    _math_vec2_to_cglm(b, cb);
    _math_vec2_to_cglm(accumulator, cd);
    glmc_vec2_addsub(ca, cb, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_center_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(a, ca);
    _math_vec2_raw_to_cglm(b, cb);
    glmc_vec2_center(ca, cb, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_center_2(Vec2 const a, Vec2 const b) {
    trace_log_push(LOG_METADATA);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(a, ca);
    _math_vec2_to_cglm(b, cb);
    glmc_vec2_center(ca, cb, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_clamp_1(FSize const *const v, FSize const minval, FSize const maxval, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 cv = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(v, cv);
    glmc_vec2_clamp(cv, (float) minval, (float) maxval);
    _math_vec2_raw_from_cglm(cv, dest);

    trace_log_pop();
}

Vec2 math_vec2_clamp_2(Vec2 const v, FSize const minval, FSize const maxval) {
    trace_log_push(LOG_METADATA);

    vec2 cv = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(v, cv);
    glmc_vec2_clamp(cv, (float) minval, (float) maxval);

    Vec2 const result = _math_vec2_from_cglm(cv);

    trace_log_pop();

    return result;
}

void math_vec2_complex_conjugate_1(FSize const *const a, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(a, ca);
    glmc_vec2_complex_conjugate(ca, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_complex_conjugate_2(Vec2 const a) {
    trace_log_push(LOG_METADATA);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(a, ca);
    glmc_vec2_complex_conjugate(ca, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_complex_div_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(a, ca);
    _math_vec2_raw_to_cglm(b, cb);
    glmc_vec2_complex_div(ca, cb, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_complex_div_2(Vec2 const a, Vec2 const b) {
    trace_log_push(LOG_METADATA);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(a, ca);
    _math_vec2_to_cglm(b, cb);
    glmc_vec2_complex_div(ca, cb, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_complex_mul_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(a, ca);
    _math_vec2_raw_to_cglm(b, cb);
    glmc_vec2_complex_mul(ca, cb, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_complex_mul_2(Vec2 const a, Vec2 const b) {
    trace_log_push(LOG_METADATA);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(a, ca);
    _math_vec2_to_cglm(b, cb);
    glmc_vec2_complex_mul(ca, cb, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_copy_1(FSize const *const a, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(a, ca);
    glmc_vec2_copy(ca, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_copy_2(Vec2 const a) {
    trace_log_push(LOG_METADATA);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(a, ca);
    glmc_vec2_copy(ca, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

FSize math_vec2_cross_1(FSize const *const a, FSize const *const b) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(a, ca);
    _math_vec2_raw_to_cglm(b, cb);

    FSize const result = (FSize) glmc_vec2_cross(ca, cb);

    trace_log_pop();

    return result;
}

FSize math_vec2_cross_2(Vec2 const a, Vec2 const b) {
    trace_log_push(LOG_METADATA);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(a, ca);
    _math_vec2_to_cglm(b, cb);

    FSize const result = (FSize) glmc_vec2_cross(ca, cb);

    trace_log_pop();

    return result;
}

FSize math_vec2_distance_1(FSize const *const a, FSize const *const b) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(a, ca);
    _math_vec2_raw_to_cglm(b, cb);

    FSize const result = (FSize) glmc_vec2_distance(ca, cb);

    trace_log_pop();

    return result;
}

FSize math_vec2_distance_2(Vec2 const a, Vec2 const b) {
    trace_log_push(LOG_METADATA);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(a, ca);
    _math_vec2_to_cglm(b, cb);

    FSize const result = (FSize) glmc_vec2_distance(ca, cb);

    trace_log_pop();

    return result;
}

FSize math_vec2_distance2_1(FSize const *const a, FSize const *const b) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(a, ca);
    _math_vec2_raw_to_cglm(b, cb);

    FSize const result = (FSize) glmc_vec2_distance2(ca, cb);

    trace_log_pop();

    return result;
}

FSize math_vec2_distance2_2(Vec2 const a, Vec2 const b) {
    trace_log_push(LOG_METADATA);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(a, ca);
    _math_vec2_to_cglm(b, cb);

    FSize const result = (FSize) glmc_vec2_distance2(ca, cb);

    trace_log_pop();

    return result;
}

void math_vec2_div_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(a, ca);
    _math_vec2_raw_to_cglm(b, cb);
    glmc_vec2_div(ca, cb, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_div_2(Vec2 const a, Vec2 const b) {
    trace_log_push(LOG_METADATA);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(a, ca);
    _math_vec2_to_cglm(b, cb);
    glmc_vec2_div(ca, cb, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_divs_1(FSize const *const v, FSize const s, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 cv = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(v, cv);
    glmc_vec2_divs(cv, (float) s, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_divs_2(Vec2 const v, FSize const s) {
    trace_log_push(LOG_METADATA);

    vec2 cv = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(v, cv);
    glmc_vec2_divs(cv, (float) s, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

FSize math_vec2_dot_1(FSize const *const a, FSize const *const b) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(a, ca);
    _math_vec2_raw_to_cglm(b, cb);

    FSize const result = (FSize) glmc_vec2_dot(ca, cb);

    trace_log_pop();

    return result;
}

FSize math_vec2_dot_2(Vec2 const a, Vec2 const b) {
    trace_log_push(LOG_METADATA);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(a, ca);
    _math_vec2_to_cglm(b, cb);

    FSize const result = (FSize) glmc_vec2_dot(ca, cb);

    trace_log_pop();

    return result;
}

bool math_vec2_eq_1(FSize const *const v, FSize const val) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);

    vec2 cv = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(v, cv);

    bool const result = glmc_vec2_eq(cv, (float) val);

    trace_log_pop();

    return result;
}

bool math_vec2_eq_2(Vec2 const v, FSize const val) {
    trace_log_push(LOG_METADATA);

    vec2 cv = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(v, cv);

    bool const result = glmc_vec2_eq(cv, (float) val);

    trace_log_pop();

    return result;
}

bool math_vec2_eqv_1(FSize const *const a, FSize const *const b) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(a, ca);
    _math_vec2_raw_to_cglm(b, cb);

    bool const result = glmc_vec2_eqv(ca, cb);

    trace_log_pop();

    return result;
}

bool math_vec2_eqv_2(Vec2 const a, Vec2 const b) {
    trace_log_push(LOG_METADATA);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(a, ca);
    _math_vec2_to_cglm(b, cb);

    bool const result = glmc_vec2_eqv(ca, cb);

    trace_log_pop();

    return result;
}

void math_vec2_fill_1(FSize const val, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 cd = DEFAULT_INITIALIZATION;

    glmc_vec2_fill(cd, (float) val);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_fill_2(FSize const val) {
    trace_log_push(LOG_METADATA);

    vec2 cd = DEFAULT_INITIALIZATION;

    glmc_vec2_fill(cd, (float) val);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_floor_1(FSize const *const v, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 cv = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(v, cv);
    glmc_vec2_floor(cv, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_floor_2(Vec2 const v) {
    trace_log_push(LOG_METADATA);

    vec2 cv = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(v, cv);
    glmc_vec2_floor(cv, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_fract_1(FSize const *const v, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 cv = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(v, cv);
    glmc_vec2_fract(cv, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_fract_2(Vec2 const v) {
    trace_log_push(LOG_METADATA);

    vec2 cv = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(v, cv);
    glmc_vec2_fract(cv, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_lerp_1(FSize const *const from, FSize const *const to, FSize const t, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "from", (void*) from);
    error_check_null(LOG_METADATA, "to", (void*) to);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(from, ca);
    _math_vec2_raw_to_cglm(to, cb);
    glmc_vec2_lerp(ca, cb, (float) t, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_lerp_2(Vec2 const from, Vec2 const to, FSize const t) {
    trace_log_push(LOG_METADATA);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(from, ca);
    _math_vec2_to_cglm(to, cb);
    glmc_vec2_lerp(ca, cb, (float) t, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_make_1(FSize const *const src, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "src", (void*) src);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 cs = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(src, cs);
    glmc_vec2_make(cs, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_make_2(FSize const *const src) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "src", (void*) src);

    vec2 cs = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(src, cs);
    glmc_vec2_make(cs, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_maxadd_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(a, ca);
    _math_vec2_raw_to_cglm(b, cb);
    _math_vec2_raw_to_cglm(dest, cd);
    glmc_vec2_maxadd(ca, cb, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_maxadd_2(Vec2 const a, Vec2 const b, Vec2 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(a, ca);
    _math_vec2_to_cglm(b, cb);
    _math_vec2_to_cglm(accumulator, cd);
    glmc_vec2_maxadd(ca, cb, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_maxsub_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(a, ca);
    _math_vec2_raw_to_cglm(b, cb);
    _math_vec2_raw_to_cglm(dest, cd);
    glmc_vec2_maxsub(ca, cb, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_maxsub_2(Vec2 const a, Vec2 const b, Vec2 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(a, ca);
    _math_vec2_to_cglm(b, cb);
    _math_vec2_to_cglm(accumulator, cd);
    glmc_vec2_maxsub(ca, cb, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_maxv_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(a, ca);
    _math_vec2_raw_to_cglm(b, cb);
    glmc_vec2_maxv(ca, cb, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_maxv_2(Vec2 const a, Vec2 const b) {
    trace_log_push(LOG_METADATA);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(a, ca);
    _math_vec2_to_cglm(b, cb);
    glmc_vec2_maxv(ca, cb, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_minadd_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(a, ca);
    _math_vec2_raw_to_cglm(b, cb);
    _math_vec2_raw_to_cglm(dest, cd);
    glmc_vec2_minadd(ca, cb, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_minadd_2(Vec2 const a, Vec2 const b, Vec2 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(a, ca);
    _math_vec2_to_cglm(b, cb);
    _math_vec2_to_cglm(accumulator, cd);
    glmc_vec2_minadd(ca, cb, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_minsub_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(a, ca);
    _math_vec2_raw_to_cglm(b, cb);
    _math_vec2_raw_to_cglm(dest, cd);
    glmc_vec2_minsub(ca, cb, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_minsub_2(Vec2 const a, Vec2 const b, Vec2 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(a, ca);
    _math_vec2_to_cglm(b, cb);
    _math_vec2_to_cglm(accumulator, cd);
    glmc_vec2_minsub(ca, cb, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_minv_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(a, ca);
    _math_vec2_raw_to_cglm(b, cb);
    glmc_vec2_minv(ca, cb, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_minv_2(Vec2 const a, Vec2 const b) {
    trace_log_push(LOG_METADATA);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(a, ca);
    _math_vec2_to_cglm(b, cb);
    glmc_vec2_minv(ca, cb, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_mods_1(FSize const *const v, FSize const s, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 cv = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(v, cv);
    glmc_vec2_mods(cv, (float) s, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_mods_2(Vec2 const v, FSize const s) {
    trace_log_push(LOG_METADATA);

    vec2 cv = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(v, cv);
    glmc_vec2_mods(cv, (float) s, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_mul_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(a, ca);
    _math_vec2_raw_to_cglm(b, cb);
    glmc_vec2_mul(ca, cb, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_mul_2(Vec2 const a, Vec2 const b) {
    trace_log_push(LOG_METADATA);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(a, ca);
    _math_vec2_to_cglm(b, cb);
    glmc_vec2_mul(ca, cb, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_muladd_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(a, ca);
    _math_vec2_raw_to_cglm(b, cb);
    _math_vec2_raw_to_cglm(dest, cd);
    glmc_vec2_muladd(ca, cb, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_muladd_2(Vec2 const a, Vec2 const b, Vec2 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(a, ca);
    _math_vec2_to_cglm(b, cb);
    _math_vec2_to_cglm(accumulator, cd);
    glmc_vec2_muladd(ca, cb, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_muladds_1(FSize const *const a, FSize const s, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(a, ca);
    _math_vec2_raw_to_cglm(dest, cd);
    glmc_vec2_muladds(ca, (float) s, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_muladds_2(Vec2 const a, FSize const s, Vec2 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(a, ca);
    _math_vec2_to_cglm(accumulator, cd);
    glmc_vec2_muladds(ca, (float) s, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_mulsub_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(a, ca);
    _math_vec2_raw_to_cglm(b, cb);
    _math_vec2_raw_to_cglm(dest, cd);
    glmc_vec2_mulsub(ca, cb, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_mulsub_2(Vec2 const a, Vec2 const b, Vec2 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(a, ca);
    _math_vec2_to_cglm(b, cb);
    _math_vec2_to_cglm(accumulator, cd);
    glmc_vec2_mulsub(ca, cb, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_mulsubs_1(FSize const *const a, FSize const s, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(a, ca);
    _math_vec2_raw_to_cglm(dest, cd);
    glmc_vec2_mulsubs(ca, (float) s, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_mulsubs_2(Vec2 const a, FSize const s, Vec2 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(a, ca);
    _math_vec2_to_cglm(accumulator, cd);
    glmc_vec2_mulsubs(ca, (float) s, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_negate_1(FSize const *const v, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 cv = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(v, cv);
    glmc_vec2_negate_to(cv, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_negate_2(Vec2 const v) {
    trace_log_push(LOG_METADATA);

    vec2 cv = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(v, cv);
    glmc_vec2_negate_to(cv, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

FSize math_vec2_norm_1(FSize const *const v) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);

    vec2 cv = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(v, cv);

    FSize const result = (FSize) glmc_vec2_norm(cv);

    trace_log_pop();

    return result;
}

FSize math_vec2_norm_2(Vec2 const v) {
    trace_log_push(LOG_METADATA);

    vec2 cv = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(v, cv);

    FSize const result = (FSize) glmc_vec2_norm(cv);

    trace_log_pop();

    return result;
}

FSize math_vec2_norm2_1(FSize const *const v) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);

    vec2 cv = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(v, cv);

    FSize const result = (FSize) glmc_vec2_norm2(cv);

    trace_log_pop();

    return result;
}

FSize math_vec2_norm2_2(Vec2 const v) {
    trace_log_push(LOG_METADATA);

    vec2 cv = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(v, cv);

    FSize const result = (FSize) glmc_vec2_norm2(cv);

    trace_log_pop();

    return result;
}

void math_vec2_normalize_1(FSize const *const v, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 cv = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(v, cv);
    glmc_vec2_normalize_to(cv, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_normalize_2(Vec2 const v) {
    trace_log_push(LOG_METADATA);

    vec2 cv = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(v, cv);
    glmc_vec2_normalize_to(cv, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_one_1(FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 cd = DEFAULT_INITIALIZATION;

    glmc_vec2_one(cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_one_2(void) {
    trace_log_push(LOG_METADATA);

    vec2 cd = DEFAULT_INITIALIZATION;

    glmc_vec2_one(cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_reflect_1(FSize const *const v, FSize const *const n, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "n", (void*) n);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 cv = DEFAULT_INITIALIZATION;
    vec2 cn = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(v, cv);
    _math_vec2_raw_to_cglm(n, cn);
    glmc_vec2_reflect(cv, cn, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_reflect_2(Vec2 const v, Vec2 const n) {
    trace_log_push(LOG_METADATA);

    vec2 cv = DEFAULT_INITIALIZATION;
    vec2 cn = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(v, cv);
    _math_vec2_to_cglm(n, cn);
    glmc_vec2_reflect(cv, cn, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

bool math_vec2_refract_1(FSize const *const v, FSize const *const n, FSize const eta, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "n", (void*) n);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 cv = DEFAULT_INITIALIZATION;
    vec2 cn = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(v, cv);
    _math_vec2_raw_to_cglm(n, cn);

    /* A NaN or non-finite eta makes cglm report a NaN vector as a SUCCESS; a non-positive one is
     * not a ratio of indices. All three are data, refused to false and the zeroed vector: the
     * bounded conversion yields 0.0f outside float range, and the float is then tested because an
     * F64 below float range arrives as 0 too. */
    float const ceta = _math_fsize_to_float_bounded(eta);
    bool const result = ceta > 0.0f && glmc_vec2_refract(cv, cn, ceta, cd);

    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();

    return result;
}

Vec2Refraction math_vec2_refract_2(Vec2 const v, Vec2 const n, FSize const eta) {
    trace_log_push(LOG_METADATA);

    vec2 cv = DEFAULT_INITIALIZATION;
    vec2 cn = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(v, cv);
    _math_vec2_to_cglm(n, cn);

    /* eta refused as in math_vec2_refract_1 (bounded conversion, then the float tested): { false, zero }. */
    float const ceta = _math_fsize_to_float_bounded(eta);
    bool const refracted = ceta > 0.0f && glmc_vec2_refract(cv, cn, ceta, cd);
    Vec2Refraction const result = { refracted, _math_vec2_from_cglm(cd) };

    trace_log_pop();

    return result;
}

void math_vec2_rotate_1(FSize const *const v, FSize const angle, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 cv = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(v, cv);
    glmc_vec2_rotate(cv, (float) angle, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_rotate_2(Vec2 const v, FSize const angle) {
    trace_log_push(LOG_METADATA);

    vec2 cv = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(v, cv);
    glmc_vec2_rotate(cv, (float) angle, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_scale_1(FSize const *const v, FSize const s, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 cv = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(v, cv);
    glmc_vec2_scale(cv, (float) s, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_scale_2(Vec2 const v, FSize const s) {
    trace_log_push(LOG_METADATA);

    vec2 cv = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(v, cv);
    glmc_vec2_scale(cv, (float) s, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_scale_as_1(FSize const *const v, FSize const s, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 cv = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(v, cv);
    glmc_vec2_scale_as(cv, (float) s, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_scale_as_2(Vec2 const v, FSize const s) {
    trace_log_push(LOG_METADATA);

    vec2 cv = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(v, cv);
    glmc_vec2_scale_as(cv, (float) s, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_step_1(FSize const *const edge, FSize const *const x, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "edge", (void*) edge);
    error_check_null(LOG_METADATA, "x", (void*) x);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 ce = DEFAULT_INITIALIZATION;
    vec2 cx = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(edge, ce);
    _math_vec2_raw_to_cglm(x, cx);
    glmc_vec2_step(ce, cx, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_step_2(Vec2 const edge, Vec2 const x) {
    trace_log_push(LOG_METADATA);

    vec2 ce = DEFAULT_INITIALIZATION;
    vec2 cx = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(edge, ce);
    _math_vec2_to_cglm(x, cx);
    glmc_vec2_step(ce, cx, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_steps_1(FSize const edge, FSize const *const x, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "x", (void*) x);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 cx = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(x, cx);
    glmc_vec2_steps((float) edge, cx, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_steps_2(FSize const edge, Vec2 const x) {
    trace_log_push(LOG_METADATA);

    vec2 cx = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(x, cx);
    glmc_vec2_steps((float) edge, cx, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_stepr_1(FSize const *const edge, FSize const x, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "edge", (void*) edge);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 ce = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(edge, ce);
    glmc_vec2_stepr(ce, (float) x, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_stepr_2(Vec2 const edge, FSize const x) {
    trace_log_push(LOG_METADATA);

    vec2 ce = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(edge, ce);
    glmc_vec2_stepr(ce, (float) x, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_sub_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(a, ca);
    _math_vec2_raw_to_cglm(b, cb);
    glmc_vec2_sub(ca, cb, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_sub_2(Vec2 const a, Vec2 const b) {
    trace_log_push(LOG_METADATA);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(a, ca);
    _math_vec2_to_cglm(b, cb);
    glmc_vec2_sub(ca, cb, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_subadd_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(a, ca);
    _math_vec2_raw_to_cglm(b, cb);
    _math_vec2_raw_to_cglm(dest, cd);
    glmc_vec2_subadd(ca, cb, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_subadd_2(Vec2 const a, Vec2 const b, Vec2 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(a, ca);
    _math_vec2_to_cglm(b, cb);
    _math_vec2_to_cglm(accumulator, cd);
    glmc_vec2_subadd(ca, cb, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_subs_1(FSize const *const v, FSize const s, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 cv = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(v, cv);
    glmc_vec2_subs(cv, (float) s, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_subs_2(Vec2 const v, FSize const s) {
    trace_log_push(LOG_METADATA);

    vec2 cv = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(v, cv);
    glmc_vec2_subs(cv, (float) s, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_subsub_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(a, ca);
    _math_vec2_raw_to_cglm(b, cb);
    _math_vec2_raw_to_cglm(dest, cd);
    glmc_vec2_subsub(ca, cb, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_subsub_2(Vec2 const a, Vec2 const b, Vec2 const accumulator) {
    trace_log_push(LOG_METADATA);

    vec2 ca = DEFAULT_INITIALIZATION;
    vec2 cb = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(a, ca);
    _math_vec2_to_cglm(b, cb);
    _math_vec2_to_cglm(accumulator, cd);
    glmc_vec2_subsub(ca, cb, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_swizzle_1(FSize const *const v, ISize const mask, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 cv = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(v, cv);

    if (_math_swizzle_mask_fits(mask, 2)) {
        glmc_vec2_swizzle(cv, (int) mask, cd);
    }

    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_swizzle_2(Vec2 const v, ISize const mask) {
    trace_log_push(LOG_METADATA);

    vec2 cv = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(v, cv);

    if (_math_swizzle_mask_fits(mask, 2)) {
        glmc_vec2_swizzle(cv, (int) mask, cd);
    }

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_vec2_1(FSize const *const v, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    float cv[2] = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(v, cv);
    glmc_vec2(cv, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_vec2_2(Vec3 const v) {
    trace_log_push(LOG_METADATA);

    vec3 cv = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(v, cv);
    glmc_vec2(cv, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_vec2_zero_1(FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 cd = DEFAULT_INITIALIZATION;

    glmc_vec2_zero(cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_vec2_zero_2(void) {
    trace_log_push(LOG_METADATA);

    vec2 cd = DEFAULT_INITIALIZATION;

    glmc_vec2_zero(cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}