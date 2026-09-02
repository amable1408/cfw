/*
 * ivec4.c - 4D integer vector operations for the CFW math module.
 *
 * See ivec4.h for API documentation and usage examples.
 */

#include <math/ivec4.h>

/*==============================================================================
 * MARK: - IVec4 API
 *============================================================================*/

void math_ivec4_abs_1(ISize const *const v, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec4 cv = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_raw_to_cglm(v, cv);
    glmc_ivec4_abs(cv, cd);
    _math_ivec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec4 math_ivec4_abs_2(IVec4 const v) {
    trace_log_push(LOG_METADATA);

    ivec4 cv = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_to_cglm(v, cv);
    glmc_ivec4_abs(cv, cd);

    IVec4 const result = _math_ivec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec4_add_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cb = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_raw_to_cglm(a, ca);
    _math_ivec4_raw_to_cglm(b, cb);
    glmc_ivec4_add(ca, cb, cd);
    _math_ivec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec4 math_ivec4_add_2(IVec4 const a, IVec4 const b) {
    trace_log_push(LOG_METADATA);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cb = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_to_cglm(a, ca);
    _math_ivec4_to_cglm(b, cb);
    glmc_ivec4_add(ca, cb, cd);

    IVec4 const result = _math_ivec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec4_addadd_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cb = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_raw_to_cglm(a, ca);
    _math_ivec4_raw_to_cglm(b, cb);
    _math_ivec4_raw_to_cglm(dest, cd);
    glmc_ivec4_addadd(ca, cb, cd);
    _math_ivec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec4 math_ivec4_addadd_2(IVec4 const a, IVec4 const b, IVec4 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cb = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_to_cglm(a, ca);
    _math_ivec4_to_cglm(b, cb);
    _math_ivec4_to_cglm(accumulator, cd);
    glmc_ivec4_addadd(ca, cb, cd);

    IVec4 const result = _math_ivec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec4_addadds_1(ISize const *const a, ISize const s, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_raw_to_cglm(a, ca);
    _math_ivec4_raw_to_cglm(dest, cd);
    glmc_ivec4_addadds(ca, (int) s, cd);
    _math_ivec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec4 math_ivec4_addadds_2(IVec4 const a, ISize const s, IVec4 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_to_cglm(a, ca);
    _math_ivec4_to_cglm(accumulator, cd);
    glmc_ivec4_addadds(ca, (int) s, cd);

    IVec4 const result = _math_ivec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec4_adds_1(ISize const *const v, ISize const s, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec4 cv = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_raw_to_cglm(v, cv);
    glmc_ivec4_adds(cv, (int) s, cd);
    _math_ivec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec4 math_ivec4_adds_2(IVec4 const v, ISize const s) {
    trace_log_push(LOG_METADATA);

    ivec4 cv = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_to_cglm(v, cv);
    glmc_ivec4_adds(cv, (int) s, cd);

    IVec4 const result = _math_ivec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec4_addsub_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cb = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_raw_to_cglm(a, ca);
    _math_ivec4_raw_to_cglm(b, cb);
    _math_ivec4_raw_to_cglm(dest, cd);
    glmc_ivec4_addsub(ca, cb, cd);
    _math_ivec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec4 math_ivec4_addsub_2(IVec4 const a, IVec4 const b, IVec4 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cb = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_to_cglm(a, ca);
    _math_ivec4_to_cglm(b, cb);
    _math_ivec4_to_cglm(accumulator, cd);
    glmc_ivec4_addsub(ca, cb, cd);

    IVec4 const result = _math_ivec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec4_addsubs_1(ISize const *const a, ISize const s, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_raw_to_cglm(a, ca);
    _math_ivec4_raw_to_cglm(dest, cd);
    glmc_ivec4_addsubs(ca, (int) s, cd);
    _math_ivec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec4 math_ivec4_addsubs_2(IVec4 const a, ISize const s, IVec4 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_to_cglm(a, ca);
    _math_ivec4_to_cglm(accumulator, cd);
    glmc_ivec4_addsubs(ca, (int) s, cd);

    IVec4 const result = _math_ivec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec4_clamp_1(ISize const *const v, ISize const minval, ISize const maxval, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec4 cv = DEFAULT_INITIALIZATION;

    _math_ivec4_raw_to_cglm(v, cv);
    glmc_ivec4_clamp(cv, _math_ivec_saturate_int(minval), _math_ivec_saturate_int(maxval));
    _math_ivec4_raw_from_cglm(cv, dest);

    trace_log_pop();
}

IVec4 math_ivec4_clamp_2(IVec4 const v, ISize const minval, ISize const maxval) {
    trace_log_push(LOG_METADATA);

    ivec4 cv = DEFAULT_INITIALIZATION;

    _math_ivec4_to_cglm(v, cv);
    glmc_ivec4_clamp(cv, _math_ivec_saturate_int(minval), _math_ivec_saturate_int(maxval));

    IVec4 const result = _math_ivec4_from_cglm(cv);

    trace_log_pop();

    return result;
}

void math_ivec4_copy_1(ISize const *const a, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_raw_to_cglm(a, ca);
    glmc_ivec4_copy(ca, cd);
    _math_ivec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec4 math_ivec4_copy_2(IVec4 const a) {
    trace_log_push(LOG_METADATA);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_to_cglm(a, ca);
    glmc_ivec4_copy(ca, cd);

    IVec4 const result = _math_ivec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

FSize math_ivec4_distance_1(ISize const *const a, ISize const *const b) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cb = DEFAULT_INITIALIZATION;

    _math_ivec4_raw_to_cglm(a, ca);
    _math_ivec4_raw_to_cglm(b, cb);

    FSize const result = (FSize) glmc_ivec4_distance(ca, cb);

    trace_log_pop();

    return result;
}

FSize math_ivec4_distance_2(IVec4 const a, IVec4 const b) {
    trace_log_push(LOG_METADATA);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cb = DEFAULT_INITIALIZATION;

    _math_ivec4_to_cglm(a, ca);
    _math_ivec4_to_cglm(b, cb);

    FSize const result = (FSize) glmc_ivec4_distance(ca, cb);

    trace_log_pop();

    return result;
}

ISize math_ivec4_distance2_1(ISize const *const a, ISize const *const b) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cb = DEFAULT_INITIALIZATION;

    _math_ivec4_raw_to_cglm(a, ca);
    _math_ivec4_raw_to_cglm(b, cb);

    ISize const result = (ISize) glmc_ivec4_distance2(ca, cb);

    trace_log_pop();

    return result;
}

ISize math_ivec4_distance2_2(IVec4 const a, IVec4 const b) {
    trace_log_push(LOG_METADATA);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cb = DEFAULT_INITIALIZATION;

    _math_ivec4_to_cglm(a, ca);
    _math_ivec4_to_cglm(b, cb);

    ISize const result = (ISize) glmc_ivec4_distance2(ca, cb);

    trace_log_pop();

    return result;
}

void math_ivec4_ivec4_1(ISize const *const v, ISize const last, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec3 cv = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(v, cv);
    glmc_ivec4(cv, (int) last, cd);
    _math_ivec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec4 math_ivec4_ivec4_2(IVec3 const v, ISize const last) {
    trace_log_push(LOG_METADATA);

    ivec3 cv = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(v, cv);
    glmc_ivec4(cv, (int) last, cd);

    IVec4 const result = _math_ivec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec4_maxadd_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cb = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_raw_to_cglm(a, ca);
    _math_ivec4_raw_to_cglm(b, cb);
    _math_ivec4_raw_to_cglm(dest, cd);
    glmc_ivec4_maxadd(ca, cb, cd);
    _math_ivec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec4 math_ivec4_maxadd_2(IVec4 const a, IVec4 const b, IVec4 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cb = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_to_cglm(a, ca);
    _math_ivec4_to_cglm(b, cb);
    _math_ivec4_to_cglm(accumulator, cd);
    glmc_ivec4_maxadd(ca, cb, cd);

    IVec4 const result = _math_ivec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec4_maxsub_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cb = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_raw_to_cglm(a, ca);
    _math_ivec4_raw_to_cglm(b, cb);
    _math_ivec4_raw_to_cglm(dest, cd);
    glmc_ivec4_maxsub(ca, cb, cd);
    _math_ivec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec4 math_ivec4_maxsub_2(IVec4 const a, IVec4 const b, IVec4 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cb = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_to_cglm(a, ca);
    _math_ivec4_to_cglm(b, cb);
    _math_ivec4_to_cglm(accumulator, cd);
    glmc_ivec4_maxsub(ca, cb, cd);

    IVec4 const result = _math_ivec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec4_maxv_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cb = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_raw_to_cglm(a, ca);
    _math_ivec4_raw_to_cglm(b, cb);
    glmc_ivec4_maxv(ca, cb, cd);
    _math_ivec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec4 math_ivec4_maxv_2(IVec4 const a, IVec4 const b) {
    trace_log_push(LOG_METADATA);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cb = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_to_cglm(a, ca);
    _math_ivec4_to_cglm(b, cb);
    glmc_ivec4_maxv(ca, cb, cd);

    IVec4 const result = _math_ivec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec4_minadd_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cb = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_raw_to_cglm(a, ca);
    _math_ivec4_raw_to_cglm(b, cb);
    _math_ivec4_raw_to_cglm(dest, cd);
    glmc_ivec4_minadd(ca, cb, cd);
    _math_ivec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec4 math_ivec4_minadd_2(IVec4 const a, IVec4 const b, IVec4 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cb = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_to_cglm(a, ca);
    _math_ivec4_to_cglm(b, cb);
    _math_ivec4_to_cglm(accumulator, cd);
    glmc_ivec4_minadd(ca, cb, cd);

    IVec4 const result = _math_ivec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec4_minsub_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cb = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_raw_to_cglm(a, ca);
    _math_ivec4_raw_to_cglm(b, cb);
    _math_ivec4_raw_to_cglm(dest, cd);
    glmc_ivec4_minsub(ca, cb, cd);
    _math_ivec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec4 math_ivec4_minsub_2(IVec4 const a, IVec4 const b, IVec4 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cb = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_to_cglm(a, ca);
    _math_ivec4_to_cglm(b, cb);
    _math_ivec4_to_cglm(accumulator, cd);
    glmc_ivec4_minsub(ca, cb, cd);

    IVec4 const result = _math_ivec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec4_minv_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cb = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_raw_to_cglm(a, ca);
    _math_ivec4_raw_to_cglm(b, cb);
    glmc_ivec4_minv(ca, cb, cd);
    _math_ivec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec4 math_ivec4_minv_2(IVec4 const a, IVec4 const b) {
    trace_log_push(LOG_METADATA);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cb = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_to_cglm(a, ca);
    _math_ivec4_to_cglm(b, cb);
    glmc_ivec4_minv(ca, cb, cd);

    IVec4 const result = _math_ivec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec4_mul_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cb = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_raw_to_cglm(a, ca);
    _math_ivec4_raw_to_cglm(b, cb);
    glmc_ivec4_mul(ca, cb, cd);
    _math_ivec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec4 math_ivec4_mul_2(IVec4 const a, IVec4 const b) {
    trace_log_push(LOG_METADATA);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cb = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_to_cglm(a, ca);
    _math_ivec4_to_cglm(b, cb);
    glmc_ivec4_mul(ca, cb, cd);

    IVec4 const result = _math_ivec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec4_muladd_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cb = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_raw_to_cglm(a, ca);
    _math_ivec4_raw_to_cglm(b, cb);
    _math_ivec4_raw_to_cglm(dest, cd);
    glmc_ivec4_muladd(ca, cb, cd);
    _math_ivec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec4 math_ivec4_muladd_2(IVec4 const a, IVec4 const b, IVec4 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cb = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_to_cglm(a, ca);
    _math_ivec4_to_cglm(b, cb);
    _math_ivec4_to_cglm(accumulator, cd);
    glmc_ivec4_muladd(ca, cb, cd);

    IVec4 const result = _math_ivec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec4_muladds_1(ISize const *const a, ISize const s, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_raw_to_cglm(a, ca);
    _math_ivec4_raw_to_cglm(dest, cd);
    glmc_ivec4_muladds(ca, (int) s, cd);
    _math_ivec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec4 math_ivec4_muladds_2(IVec4 const a, ISize const s, IVec4 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_to_cglm(a, ca);
    _math_ivec4_to_cglm(accumulator, cd);
    glmc_ivec4_muladds(ca, (int) s, cd);

    IVec4 const result = _math_ivec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec4_mulsub_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cb = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_raw_to_cglm(a, ca);
    _math_ivec4_raw_to_cglm(b, cb);
    _math_ivec4_raw_to_cglm(dest, cd);
    glmc_ivec4_mulsub(ca, cb, cd);
    _math_ivec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec4 math_ivec4_mulsub_2(IVec4 const a, IVec4 const b, IVec4 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cb = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_to_cglm(a, ca);
    _math_ivec4_to_cglm(b, cb);
    _math_ivec4_to_cglm(accumulator, cd);
    glmc_ivec4_mulsub(ca, cb, cd);

    IVec4 const result = _math_ivec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec4_mulsubs_1(ISize const *const a, ISize const s, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_raw_to_cglm(a, ca);
    _math_ivec4_raw_to_cglm(dest, cd);
    glmc_ivec4_mulsubs(ca, (int) s, cd);
    _math_ivec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec4 math_ivec4_mulsubs_2(IVec4 const a, ISize const s, IVec4 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_to_cglm(a, ca);
    _math_ivec4_to_cglm(accumulator, cd);
    glmc_ivec4_mulsubs(ca, (int) s, cd);

    IVec4 const result = _math_ivec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec4_one_1(ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec4 cd = DEFAULT_INITIALIZATION;

    glmc_ivec4_one(cd);
    _math_ivec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec4 math_ivec4_one_2(void) {
    trace_log_push(LOG_METADATA);

    ivec4 cd = DEFAULT_INITIALIZATION;

    glmc_ivec4_one(cd);

    IVec4 const result = _math_ivec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec4_scale_1(ISize const *const v, ISize const s, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec4 cv = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_raw_to_cglm(v, cv);
    glmc_ivec4_scale(cv, (int) s, cd);
    _math_ivec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec4 math_ivec4_scale_2(IVec4 const v, ISize const s) {
    trace_log_push(LOG_METADATA);

    ivec4 cv = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_to_cglm(v, cv);
    glmc_ivec4_scale(cv, (int) s, cd);

    IVec4 const result = _math_ivec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec4_sub_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cb = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_raw_to_cglm(a, ca);
    _math_ivec4_raw_to_cglm(b, cb);
    glmc_ivec4_sub(ca, cb, cd);
    _math_ivec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec4 math_ivec4_sub_2(IVec4 const a, IVec4 const b) {
    trace_log_push(LOG_METADATA);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cb = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_to_cglm(a, ca);
    _math_ivec4_to_cglm(b, cb);
    glmc_ivec4_sub(ca, cb, cd);

    IVec4 const result = _math_ivec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec4_subadd_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cb = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_raw_to_cglm(a, ca);
    _math_ivec4_raw_to_cglm(b, cb);
    _math_ivec4_raw_to_cglm(dest, cd);
    glmc_ivec4_subadd(ca, cb, cd);
    _math_ivec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec4 math_ivec4_subadd_2(IVec4 const a, IVec4 const b, IVec4 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cb = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_to_cglm(a, ca);
    _math_ivec4_to_cglm(b, cb);
    _math_ivec4_to_cglm(accumulator, cd);
    glmc_ivec4_subadd(ca, cb, cd);

    IVec4 const result = _math_ivec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec4_subadds_1(ISize const *const a, ISize const s, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_raw_to_cglm(a, ca);
    _math_ivec4_raw_to_cglm(dest, cd);
    glmc_ivec4_subadds(ca, (int) s, cd);
    _math_ivec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec4 math_ivec4_subadds_2(IVec4 const a, ISize const s, IVec4 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_to_cglm(a, ca);
    _math_ivec4_to_cglm(accumulator, cd);
    glmc_ivec4_subadds(ca, (int) s, cd);

    IVec4 const result = _math_ivec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec4_subs_1(ISize const *const v, ISize const s, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec4 cv = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_raw_to_cglm(v, cv);
    glmc_ivec4_subs(cv, (int) s, cd);
    _math_ivec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec4 math_ivec4_subs_2(IVec4 const v, ISize const s) {
    trace_log_push(LOG_METADATA);

    ivec4 cv = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_to_cglm(v, cv);
    glmc_ivec4_subs(cv, (int) s, cd);

    IVec4 const result = _math_ivec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec4_subsub_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cb = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_raw_to_cglm(a, ca);
    _math_ivec4_raw_to_cglm(b, cb);
    _math_ivec4_raw_to_cglm(dest, cd);
    glmc_ivec4_subsub(ca, cb, cd);
    _math_ivec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec4 math_ivec4_subsub_2(IVec4 const a, IVec4 const b, IVec4 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cb = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_to_cglm(a, ca);
    _math_ivec4_to_cglm(b, cb);
    _math_ivec4_to_cglm(accumulator, cd);
    glmc_ivec4_subsub(ca, cb, cd);

    IVec4 const result = _math_ivec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec4_subsubs_1(ISize const *const a, ISize const s, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_raw_to_cglm(a, ca);
    _math_ivec4_raw_to_cglm(dest, cd);
    glmc_ivec4_subsubs(ca, (int) s, cd);
    _math_ivec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec4 math_ivec4_subsubs_2(IVec4 const a, ISize const s, IVec4 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec4 ca = DEFAULT_INITIALIZATION;
    ivec4 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_to_cglm(a, ca);
    _math_ivec4_to_cglm(accumulator, cd);
    glmc_ivec4_subsubs(ca, (int) s, cd);

    IVec4 const result = _math_ivec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec4_zero_1(ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec4 cd = DEFAULT_INITIALIZATION;

    glmc_ivec4_zero(cd);
    _math_ivec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec4 math_ivec4_zero_2(void) {
    trace_log_push(LOG_METADATA);

    ivec4 cd = DEFAULT_INITIALIZATION;

    glmc_ivec4_zero(cd);

    IVec4 const result = _math_ivec4_from_cglm(cd);

    trace_log_pop();

    return result;
}