/*
 * ivec3.c - 3D integer vector operations for the CFW math module.
 *
 * See ivec3.h for API documentation and usage examples.
 */

#include <math/ivec3.h>

/*==============================================================================
 * MARK: - IVec3 API
 *============================================================================*/

void math_ivec3_abs_1(ISize const *const v, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec3 cv = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(v, cv);
    glmc_ivec3_abs(cv, cd);
    _math_ivec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec3 math_ivec3_abs_2(IVec3 const v) {
    trace_log_push(LOG_METADATA);

    ivec3 cv = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(v, cv);
    glmc_ivec3_abs(cv, cd);

    IVec3 const result = _math_ivec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec3_add_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(a, ca);
    _math_ivec3_raw_to_cglm(b, cb);
    glmc_ivec3_add(ca, cb, cd);
    _math_ivec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec3 math_ivec3_add_2(IVec3 const a, IVec3 const b) {
    trace_log_push(LOG_METADATA);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(a, ca);
    _math_ivec3_to_cglm(b, cb);
    glmc_ivec3_add(ca, cb, cd);

    IVec3 const result = _math_ivec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec3_addadd_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(a, ca);
    _math_ivec3_raw_to_cglm(b, cb);
    _math_ivec3_raw_to_cglm(dest, cd);
    glmc_ivec3_addadd(ca, cb, cd);
    _math_ivec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec3 math_ivec3_addadd_2(IVec3 const a, IVec3 const b, IVec3 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(a, ca);
    _math_ivec3_to_cglm(b, cb);
    _math_ivec3_to_cglm(accumulator, cd);
    glmc_ivec3_addadd(ca, cb, cd);

    IVec3 const result = _math_ivec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec3_addadds_1(ISize const *const a, ISize const s, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(a, ca);
    _math_ivec3_raw_to_cglm(dest, cd);
    glmc_ivec3_addadds(ca, (int) s, cd);
    _math_ivec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec3 math_ivec3_addadds_2(IVec3 const a, ISize const s, IVec3 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(a, ca);
    _math_ivec3_to_cglm(accumulator, cd);
    glmc_ivec3_addadds(ca, (int) s, cd);

    IVec3 const result = _math_ivec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec3_adds_1(ISize const *const v, ISize const s, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec3 cv = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(v, cv);
    glmc_ivec3_adds(cv, (int) s, cd);
    _math_ivec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec3 math_ivec3_adds_2(IVec3 const v, ISize const s) {
    trace_log_push(LOG_METADATA);

    ivec3 cv = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(v, cv);
    glmc_ivec3_adds(cv, (int) s, cd);

    IVec3 const result = _math_ivec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec3_addsub_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(a, ca);
    _math_ivec3_raw_to_cglm(b, cb);
    _math_ivec3_raw_to_cglm(dest, cd);
    glmc_ivec3_addsub(ca, cb, cd);
    _math_ivec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec3 math_ivec3_addsub_2(IVec3 const a, IVec3 const b, IVec3 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(a, ca);
    _math_ivec3_to_cglm(b, cb);
    _math_ivec3_to_cglm(accumulator, cd);
    glmc_ivec3_addsub(ca, cb, cd);

    IVec3 const result = _math_ivec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec3_addsubs_1(ISize const *const a, ISize const s, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(a, ca);
    _math_ivec3_raw_to_cglm(dest, cd);
    glmc_ivec3_addsubs(ca, (int) s, cd);
    _math_ivec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec3 math_ivec3_addsubs_2(IVec3 const a, ISize const s, IVec3 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(a, ca);
    _math_ivec3_to_cglm(accumulator, cd);
    glmc_ivec3_addsubs(ca, (int) s, cd);

    IVec3 const result = _math_ivec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec3_clamp_1(ISize const *const v, ISize const minval, ISize const maxval, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec3 cv = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(v, cv);
    glmc_ivec3_clamp(cv, _math_ivec_saturate_int(minval), _math_ivec_saturate_int(maxval));
    _math_ivec3_raw_from_cglm(cv, dest);

    trace_log_pop();
}

IVec3 math_ivec3_clamp_2(IVec3 const v, ISize const minval, ISize const maxval) {
    trace_log_push(LOG_METADATA);

    ivec3 cv = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(v, cv);
    glmc_ivec3_clamp(cv, _math_ivec_saturate_int(minval), _math_ivec_saturate_int(maxval));

    IVec3 const result = _math_ivec3_from_cglm(cv);

    trace_log_pop();

    return result;
}

void math_ivec3_copy_1(ISize const *const a, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(a, ca);
    glmc_ivec3_copy(ca, cd);
    _math_ivec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec3 math_ivec3_copy_2(IVec3 const a) {
    trace_log_push(LOG_METADATA);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(a, ca);
    glmc_ivec3_copy(ca, cd);

    IVec3 const result = _math_ivec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

FSize math_ivec3_distance_1(ISize const *const a, ISize const *const b) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(a, ca);
    _math_ivec3_raw_to_cglm(b, cb);

    FSize const result = (FSize) glmc_ivec3_distance(ca, cb);

    trace_log_pop();

    return result;
}

FSize math_ivec3_distance_2(IVec3 const a, IVec3 const b) {
    trace_log_push(LOG_METADATA);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(a, ca);
    _math_ivec3_to_cglm(b, cb);

    FSize const result = (FSize) glmc_ivec3_distance(ca, cb);

    trace_log_pop();

    return result;
}

ISize math_ivec3_distance2_1(ISize const *const a, ISize const *const b) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(a, ca);
    _math_ivec3_raw_to_cglm(b, cb);

    ISize const result = (ISize) glmc_ivec3_distance2(ca, cb);

    trace_log_pop();

    return result;
}

ISize math_ivec3_distance2_2(IVec3 const a, IVec3 const b) {
    trace_log_push(LOG_METADATA);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(a, ca);
    _math_ivec3_to_cglm(b, cb);

    ISize const result = (ISize) glmc_ivec3_distance2(ca, cb);

    trace_log_pop();

    return result;
}

void math_ivec3_div_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(a, ca);
    _math_ivec3_raw_to_cglm(b, cb);

    /* Integer division by zero traps (SIGFPE); a zero component of b refuses to the zeroed
     * vector, which cd already is - so the refusal is simply not calling cglm. */
    if (!(_math_ivec_div_traps(ca[0], cb[0]) || _math_ivec_div_traps(ca[1], cb[1]) || _math_ivec_div_traps(ca[2], cb[2]))) {
        glmc_ivec3_div(ca, cb, cd);
    }

    _math_ivec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec3 math_ivec3_div_2(IVec3 const a, IVec3 const b) {
    trace_log_push(LOG_METADATA);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(a, ca);
    _math_ivec3_to_cglm(b, cb);

    /* Integer division by zero traps (SIGFPE); a zero component of b refuses to the zeroed
     * vector, which cd already is - so the refusal is simply not calling cglm. */
    if (!(_math_ivec_div_traps(ca[0], cb[0]) || _math_ivec_div_traps(ca[1], cb[1]) || _math_ivec_div_traps(ca[2], cb[2]))) {
        glmc_ivec3_div(ca, cb, cd);
    }

    IVec3 const result = _math_ivec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec3_divs_1(ISize const *const v, ISize const s, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec3 cv = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(v, cv);

    /* The divisor is checked AFTER the ISize -> int conversion: a nonzero s whose low 32
     * bits are zero (2^32) truncates to a zero divisor and would trap just the same. */
    if (!(_math_ivec_div_traps(cv[0], (int) s) || _math_ivec_div_traps(cv[1], (int) s) || _math_ivec_div_traps(cv[2], (int) s))) {
        glmc_ivec3_divs(cv, (int) s, cd);
    }

    _math_ivec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec3 math_ivec3_divs_2(IVec3 const v, ISize const s) {
    trace_log_push(LOG_METADATA);

    ivec3 cv = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(v, cv);

    /* The divisor is checked AFTER the ISize -> int conversion: a nonzero s whose low 32
     * bits are zero (2^32) truncates to a zero divisor and would trap just the same. */
    if (!(_math_ivec_div_traps(cv[0], (int) s) || _math_ivec_div_traps(cv[1], (int) s) || _math_ivec_div_traps(cv[2], (int) s))) {
        glmc_ivec3_divs(cv, (int) s, cd);
    }

    IVec3 const result = _math_ivec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

ISize math_ivec3_dot_1(ISize const *const a, ISize const *const b) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(a, ca);
    _math_ivec3_raw_to_cglm(b, cb);

    ISize const result = (ISize) glmc_ivec3_dot(ca, cb);

    trace_log_pop();

    return result;
}

ISize math_ivec3_dot_2(IVec3 const a, IVec3 const b) {
    trace_log_push(LOG_METADATA);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(a, ca);
    _math_ivec3_to_cglm(b, cb);

    ISize const result = (ISize) glmc_ivec3_dot(ca, cb);

    trace_log_pop();

    return result;
}

bool math_ivec3_eq_1(ISize const *const v, ISize const val) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);

    ivec3 cv = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(v, cv);

    bool const result = glmc_ivec3_eq(cv, (int) val);

    trace_log_pop();

    return result;
}

bool math_ivec3_eq_2(IVec3 const v, ISize const val) {
    trace_log_push(LOG_METADATA);

    ivec3 cv = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(v, cv);

    bool const result = glmc_ivec3_eq(cv, (int) val);

    trace_log_pop();

    return result;
}

bool math_ivec3_eqv_1(ISize const *const a, ISize const *const b) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(a, ca);
    _math_ivec3_raw_to_cglm(b, cb);

    bool const result = glmc_ivec3_eqv(ca, cb);

    trace_log_pop();

    return result;
}

bool math_ivec3_eqv_2(IVec3 const a, IVec3 const b) {
    trace_log_push(LOG_METADATA);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(a, ca);
    _math_ivec3_to_cglm(b, cb);

    bool const result = glmc_ivec3_eqv(ca, cb);

    trace_log_pop();

    return result;
}

void math_ivec3_fill_1(ISize const val, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec3 cd = DEFAULT_INITIALIZATION;

    glmc_ivec3_fill(cd, (int) val);
    _math_ivec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec3 math_ivec3_fill_2(ISize const val) {
    trace_log_push(LOG_METADATA);

    ivec3 cd = DEFAULT_INITIALIZATION;

    glmc_ivec3_fill(cd, (int) val);

    IVec3 const result = _math_ivec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec3_ivec3_1(ISize const *const v, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec4 cv = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_raw_to_cglm(v, cv);
    glmc_ivec3(cv, cd);
    _math_ivec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec3 math_ivec3_ivec3_2(IVec4 const v) {
    trace_log_push(LOG_METADATA);

    ivec4 cv = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec4_to_cglm(v, cv);
    glmc_ivec3(cv, cd);

    IVec3 const result = _math_ivec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec3_maxadd_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(a, ca);
    _math_ivec3_raw_to_cglm(b, cb);
    _math_ivec3_raw_to_cglm(dest, cd);
    glmc_ivec3_maxadd(ca, cb, cd);
    _math_ivec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec3 math_ivec3_maxadd_2(IVec3 const a, IVec3 const b, IVec3 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(a, ca);
    _math_ivec3_to_cglm(b, cb);
    _math_ivec3_to_cglm(accumulator, cd);
    glmc_ivec3_maxadd(ca, cb, cd);

    IVec3 const result = _math_ivec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec3_maxsub_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(a, ca);
    _math_ivec3_raw_to_cglm(b, cb);
    _math_ivec3_raw_to_cglm(dest, cd);
    glmc_ivec3_maxsub(ca, cb, cd);
    _math_ivec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec3 math_ivec3_maxsub_2(IVec3 const a, IVec3 const b, IVec3 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(a, ca);
    _math_ivec3_to_cglm(b, cb);
    _math_ivec3_to_cglm(accumulator, cd);
    glmc_ivec3_maxsub(ca, cb, cd);

    IVec3 const result = _math_ivec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec3_maxv_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(a, ca);
    _math_ivec3_raw_to_cglm(b, cb);
    glmc_ivec3_maxv(ca, cb, cd);
    _math_ivec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec3 math_ivec3_maxv_2(IVec3 const a, IVec3 const b) {
    trace_log_push(LOG_METADATA);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(a, ca);
    _math_ivec3_to_cglm(b, cb);
    glmc_ivec3_maxv(ca, cb, cd);

    IVec3 const result = _math_ivec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec3_minadd_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(a, ca);
    _math_ivec3_raw_to_cglm(b, cb);
    _math_ivec3_raw_to_cglm(dest, cd);
    glmc_ivec3_minadd(ca, cb, cd);
    _math_ivec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec3 math_ivec3_minadd_2(IVec3 const a, IVec3 const b, IVec3 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(a, ca);
    _math_ivec3_to_cglm(b, cb);
    _math_ivec3_to_cglm(accumulator, cd);
    glmc_ivec3_minadd(ca, cb, cd);

    IVec3 const result = _math_ivec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec3_minsub_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(a, ca);
    _math_ivec3_raw_to_cglm(b, cb);
    _math_ivec3_raw_to_cglm(dest, cd);
    glmc_ivec3_minsub(ca, cb, cd);
    _math_ivec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec3 math_ivec3_minsub_2(IVec3 const a, IVec3 const b, IVec3 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(a, ca);
    _math_ivec3_to_cglm(b, cb);
    _math_ivec3_to_cglm(accumulator, cd);
    glmc_ivec3_minsub(ca, cb, cd);

    IVec3 const result = _math_ivec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec3_minv_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(a, ca);
    _math_ivec3_raw_to_cglm(b, cb);
    glmc_ivec3_minv(ca, cb, cd);
    _math_ivec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec3 math_ivec3_minv_2(IVec3 const a, IVec3 const b) {
    trace_log_push(LOG_METADATA);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(a, ca);
    _math_ivec3_to_cglm(b, cb);
    glmc_ivec3_minv(ca, cb, cd);

    IVec3 const result = _math_ivec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec3_mod_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(a, ca);
    _math_ivec3_raw_to_cglm(b, cb);

    /* `%` traps on the same divisors `/` does; refuse to the zeroed vector (cd already is). */
    if (!(_math_ivec_div_traps(ca[0], cb[0]) || _math_ivec_div_traps(ca[1], cb[1]) || _math_ivec_div_traps(ca[2], cb[2]))) {
        glmc_ivec3_mod(ca, cb, cd);
    }

    _math_ivec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec3 math_ivec3_mod_2(IVec3 const a, IVec3 const b) {
    trace_log_push(LOG_METADATA);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(a, ca);
    _math_ivec3_to_cglm(b, cb);

    /* `%` traps on the same divisors `/` does; refuse to the zeroed vector (cd already is). */
    if (!(_math_ivec_div_traps(ca[0], cb[0]) || _math_ivec_div_traps(ca[1], cb[1]) || _math_ivec_div_traps(ca[2], cb[2]))) {
        glmc_ivec3_mod(ca, cb, cd);
    }

    IVec3 const result = _math_ivec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec3_mul_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(a, ca);
    _math_ivec3_raw_to_cglm(b, cb);
    glmc_ivec3_mul(ca, cb, cd);
    _math_ivec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec3 math_ivec3_mul_2(IVec3 const a, IVec3 const b) {
    trace_log_push(LOG_METADATA);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(a, ca);
    _math_ivec3_to_cglm(b, cb);
    glmc_ivec3_mul(ca, cb, cd);

    IVec3 const result = _math_ivec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec3_muladd_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(a, ca);
    _math_ivec3_raw_to_cglm(b, cb);
    _math_ivec3_raw_to_cglm(dest, cd);
    glmc_ivec3_muladd(ca, cb, cd);
    _math_ivec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec3 math_ivec3_muladd_2(IVec3 const a, IVec3 const b, IVec3 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(a, ca);
    _math_ivec3_to_cglm(b, cb);
    _math_ivec3_to_cglm(accumulator, cd);
    glmc_ivec3_muladd(ca, cb, cd);

    IVec3 const result = _math_ivec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec3_muladds_1(ISize const *const a, ISize const s, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(a, ca);
    _math_ivec3_raw_to_cglm(dest, cd);
    glmc_ivec3_muladds(ca, (int) s, cd);
    _math_ivec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec3 math_ivec3_muladds_2(IVec3 const a, ISize const s, IVec3 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(a, ca);
    _math_ivec3_to_cglm(accumulator, cd);
    glmc_ivec3_muladds(ca, (int) s, cd);

    IVec3 const result = _math_ivec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec3_mulsub_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(a, ca);
    _math_ivec3_raw_to_cglm(b, cb);
    _math_ivec3_raw_to_cglm(dest, cd);
    glmc_ivec3_mulsub(ca, cb, cd);
    _math_ivec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec3 math_ivec3_mulsub_2(IVec3 const a, IVec3 const b, IVec3 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(a, ca);
    _math_ivec3_to_cglm(b, cb);
    _math_ivec3_to_cglm(accumulator, cd);
    glmc_ivec3_mulsub(ca, cb, cd);

    IVec3 const result = _math_ivec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec3_mulsubs_1(ISize const *const a, ISize const s, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(a, ca);
    _math_ivec3_raw_to_cglm(dest, cd);
    glmc_ivec3_mulsubs(ca, (int) s, cd);
    _math_ivec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec3 math_ivec3_mulsubs_2(IVec3 const a, ISize const s, IVec3 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(a, ca);
    _math_ivec3_to_cglm(accumulator, cd);
    glmc_ivec3_mulsubs(ca, (int) s, cd);

    IVec3 const result = _math_ivec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

ISize math_ivec3_norm_1(ISize const *const v) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);

    ivec3 cv = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(v, cv);

    ISize const result = (ISize) glmc_ivec3_norm(cv);

    trace_log_pop();

    return result;
}

ISize math_ivec3_norm_2(IVec3 const v) {
    trace_log_push(LOG_METADATA);

    ivec3 cv = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(v, cv);

    ISize const result = (ISize) glmc_ivec3_norm(cv);

    trace_log_pop();

    return result;
}

ISize math_ivec3_norm2_1(ISize const *const v) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);

    ivec3 cv = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(v, cv);

    ISize const result = (ISize) glmc_ivec3_norm2(cv);

    trace_log_pop();

    return result;
}

ISize math_ivec3_norm2_2(IVec3 const v) {
    trace_log_push(LOG_METADATA);

    ivec3 cv = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(v, cv);

    ISize const result = (ISize) glmc_ivec3_norm2(cv);

    trace_log_pop();

    return result;
}

void math_ivec3_one_1(ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec3 cd = DEFAULT_INITIALIZATION;

    glmc_ivec3_one(cd);
    _math_ivec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec3 math_ivec3_one_2(void) {
    trace_log_push(LOG_METADATA);

    ivec3 cd = DEFAULT_INITIALIZATION;

    glmc_ivec3_one(cd);

    IVec3 const result = _math_ivec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec3_scale_1(ISize const *const v, ISize const s, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec3 cv = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(v, cv);
    glmc_ivec3_scale(cv, (int) s, cd);
    _math_ivec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec3 math_ivec3_scale_2(IVec3 const v, ISize const s) {
    trace_log_push(LOG_METADATA);

    ivec3 cv = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(v, cv);
    glmc_ivec3_scale(cv, (int) s, cd);

    IVec3 const result = _math_ivec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec3_sub_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(a, ca);
    _math_ivec3_raw_to_cglm(b, cb);
    glmc_ivec3_sub(ca, cb, cd);
    _math_ivec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec3 math_ivec3_sub_2(IVec3 const a, IVec3 const b) {
    trace_log_push(LOG_METADATA);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(a, ca);
    _math_ivec3_to_cglm(b, cb);
    glmc_ivec3_sub(ca, cb, cd);

    IVec3 const result = _math_ivec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec3_subadd_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(a, ca);
    _math_ivec3_raw_to_cglm(b, cb);
    _math_ivec3_raw_to_cglm(dest, cd);
    glmc_ivec3_subadd(ca, cb, cd);
    _math_ivec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec3 math_ivec3_subadd_2(IVec3 const a, IVec3 const b, IVec3 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(a, ca);
    _math_ivec3_to_cglm(b, cb);
    _math_ivec3_to_cglm(accumulator, cd);
    glmc_ivec3_subadd(ca, cb, cd);

    IVec3 const result = _math_ivec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec3_subadds_1(ISize const *const a, ISize const s, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(a, ca);
    _math_ivec3_raw_to_cglm(dest, cd);
    glmc_ivec3_subadds(ca, (int) s, cd);
    _math_ivec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec3 math_ivec3_subadds_2(IVec3 const a, ISize const s, IVec3 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(a, ca);
    _math_ivec3_to_cglm(accumulator, cd);
    glmc_ivec3_subadds(ca, (int) s, cd);

    IVec3 const result = _math_ivec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec3_subs_1(ISize const *const v, ISize const s, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec3 cv = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(v, cv);
    glmc_ivec3_subs(cv, (int) s, cd);
    _math_ivec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec3 math_ivec3_subs_2(IVec3 const v, ISize const s) {
    trace_log_push(LOG_METADATA);

    ivec3 cv = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(v, cv);
    glmc_ivec3_subs(cv, (int) s, cd);

    IVec3 const result = _math_ivec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec3_subsub_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(a, ca);
    _math_ivec3_raw_to_cglm(b, cb);
    _math_ivec3_raw_to_cglm(dest, cd);
    glmc_ivec3_subsub(ca, cb, cd);
    _math_ivec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec3 math_ivec3_subsub_2(IVec3 const a, IVec3 const b, IVec3 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cb = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(a, ca);
    _math_ivec3_to_cglm(b, cb);
    _math_ivec3_to_cglm(accumulator, cd);
    glmc_ivec3_subsub(ca, cb, cd);

    IVec3 const result = _math_ivec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec3_subsubs_1(ISize const *const a, ISize const s, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_raw_to_cglm(a, ca);
    _math_ivec3_raw_to_cglm(dest, cd);
    glmc_ivec3_subsubs(ca, (int) s, cd);
    _math_ivec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec3 math_ivec3_subsubs_2(IVec3 const a, ISize const s, IVec3 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec3 ca = DEFAULT_INITIALIZATION;
    ivec3 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(a, ca);
    _math_ivec3_to_cglm(accumulator, cd);
    glmc_ivec3_subsubs(ca, (int) s, cd);

    IVec3 const result = _math_ivec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec3_zero_1(ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec3 cd = DEFAULT_INITIALIZATION;

    glmc_ivec3_zero(cd);
    _math_ivec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec3 math_ivec3_zero_2(void) {
    trace_log_push(LOG_METADATA);

    ivec3 cd = DEFAULT_INITIALIZATION;

    glmc_ivec3_zero(cd);

    IVec3 const result = _math_ivec3_from_cglm(cd);

    trace_log_pop();

    return result;
}