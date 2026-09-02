/*
 * ivec2.c - 2D integer vector operations for the CFW math module.
 *
 * See ivec2.h for API documentation and usage examples.
 */

#include <math/ivec2.h>

/*==============================================================================
 * MARK: - IVec2 API
 *============================================================================*/

void math_ivec2_abs_1(ISize const *const v, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec2 cv = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(v, cv);
    glmc_ivec2_abs(cv, cd);
    _math_ivec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec2 math_ivec2_abs_2(IVec2 const v) {
    trace_log_push(LOG_METADATA);

    ivec2 cv = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(v, cv);
    glmc_ivec2_abs(cv, cd);

    IVec2 const result = _math_ivec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec2_add_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(a, ca);
    _math_ivec2_raw_to_cglm(b, cb);
    glmc_ivec2_add(ca, cb, cd);
    _math_ivec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec2 math_ivec2_add_2(IVec2 const a, IVec2 const b) {
    trace_log_push(LOG_METADATA);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(a, ca);
    _math_ivec2_to_cglm(b, cb);
    glmc_ivec2_add(ca, cb, cd);

    IVec2 const result = _math_ivec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec2_addadd_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(a, ca);
    _math_ivec2_raw_to_cglm(b, cb);
    _math_ivec2_raw_to_cglm(dest, cd);
    glmc_ivec2_addadd(ca, cb, cd);
    _math_ivec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec2 math_ivec2_addadd_2(IVec2 const a, IVec2 const b, IVec2 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(a, ca);
    _math_ivec2_to_cglm(b, cb);
    _math_ivec2_to_cglm(accumulator, cd);
    glmc_ivec2_addadd(ca, cb, cd);

    IVec2 const result = _math_ivec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec2_addadds_1(ISize const *const a, ISize const s, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(a, ca);
    _math_ivec2_raw_to_cglm(dest, cd);
    glmc_ivec2_addadds(ca, (int) s, cd);
    _math_ivec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec2 math_ivec2_addadds_2(IVec2 const a, ISize const s, IVec2 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(a, ca);
    _math_ivec2_to_cglm(accumulator, cd);
    glmc_ivec2_addadds(ca, (int) s, cd);

    IVec2 const result = _math_ivec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec2_adds_1(ISize const *const v, ISize const s, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec2 cv = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(v, cv);
    glmc_ivec2_adds(cv, (int) s, cd);
    _math_ivec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec2 math_ivec2_adds_2(IVec2 const v, ISize const s) {
    trace_log_push(LOG_METADATA);

    ivec2 cv = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(v, cv);
    glmc_ivec2_adds(cv, (int) s, cd);

    IVec2 const result = _math_ivec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec2_addsub_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(a, ca);
    _math_ivec2_raw_to_cglm(b, cb);
    _math_ivec2_raw_to_cglm(dest, cd);
    glmc_ivec2_addsub(ca, cb, cd);
    _math_ivec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec2 math_ivec2_addsub_2(IVec2 const a, IVec2 const b, IVec2 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(a, ca);
    _math_ivec2_to_cglm(b, cb);
    _math_ivec2_to_cglm(accumulator, cd);
    glmc_ivec2_addsub(ca, cb, cd);

    IVec2 const result = _math_ivec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec2_addsubs_1(ISize const *const a, ISize const s, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(a, ca);
    _math_ivec2_raw_to_cglm(dest, cd);
    glmc_ivec2_addsubs(ca, (int) s, cd);
    _math_ivec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec2 math_ivec2_addsubs_2(IVec2 const a, ISize const s, IVec2 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(a, ca);
    _math_ivec2_to_cglm(accumulator, cd);
    glmc_ivec2_addsubs(ca, (int) s, cd);

    IVec2 const result = _math_ivec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec2_clamp_1(ISize const *const v, ISize const minval, ISize const maxval, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec2 cv = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(v, cv);
    glmc_ivec2_clamp(cv, _math_ivec_saturate_int(minval), _math_ivec_saturate_int(maxval));
    _math_ivec2_raw_from_cglm(cv, dest);

    trace_log_pop();
}

IVec2 math_ivec2_clamp_2(IVec2 const v, ISize const minval, ISize const maxval) {
    trace_log_push(LOG_METADATA);

    ivec2 cv = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(v, cv);
    glmc_ivec2_clamp(cv, _math_ivec_saturate_int(minval), _math_ivec_saturate_int(maxval));

    IVec2 const result = _math_ivec2_from_cglm(cv);

    trace_log_pop();

    return result;
}

void math_ivec2_copy_1(ISize const *const a, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(a, ca);
    glmc_ivec2_copy(ca, cd);
    _math_ivec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec2 math_ivec2_copy_2(IVec2 const a) {
    trace_log_push(LOG_METADATA);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(a, ca);
    glmc_ivec2_copy(ca, cd);

    IVec2 const result = _math_ivec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

ISize math_ivec2_cross_1(ISize const *const a, ISize const *const b) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(a, ca);
    _math_ivec2_raw_to_cglm(b, cb);

    ISize const result = (ISize) glmc_ivec2_cross(ca, cb);

    trace_log_pop();

    return result;
}

ISize math_ivec2_cross_2(IVec2 const a, IVec2 const b) {
    trace_log_push(LOG_METADATA);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(a, ca);
    _math_ivec2_to_cglm(b, cb);

    ISize const result = (ISize) glmc_ivec2_cross(ca, cb);

    trace_log_pop();

    return result;
}

FSize math_ivec2_distance_1(ISize const *const a, ISize const *const b) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(a, ca);
    _math_ivec2_raw_to_cglm(b, cb);

    FSize const result = (FSize) glmc_ivec2_distance(ca, cb);

    trace_log_pop();

    return result;
}

FSize math_ivec2_distance_2(IVec2 const a, IVec2 const b) {
    trace_log_push(LOG_METADATA);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(a, ca);
    _math_ivec2_to_cglm(b, cb);

    FSize const result = (FSize) glmc_ivec2_distance(ca, cb);

    trace_log_pop();

    return result;
}

ISize math_ivec2_distance2_1(ISize const *const a, ISize const *const b) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(a, ca);
    _math_ivec2_raw_to_cglm(b, cb);

    ISize const result = (ISize) glmc_ivec2_distance2(ca, cb);

    trace_log_pop();

    return result;
}

ISize math_ivec2_distance2_2(IVec2 const a, IVec2 const b) {
    trace_log_push(LOG_METADATA);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(a, ca);
    _math_ivec2_to_cglm(b, cb);

    ISize const result = (ISize) glmc_ivec2_distance2(ca, cb);

    trace_log_pop();

    return result;
}

void math_ivec2_div_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(a, ca);
    _math_ivec2_raw_to_cglm(b, cb);

    /* Integer division by zero traps (SIGFPE); a zero component of b refuses to the zeroed
     * vector, which cd already is - so the refusal is simply not calling cglm. */
    if (!(_math_ivec_div_traps(ca[0], cb[0]) || _math_ivec_div_traps(ca[1], cb[1]))) {
        glmc_ivec2_div(ca, cb, cd);
    }

    _math_ivec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec2 math_ivec2_div_2(IVec2 const a, IVec2 const b) {
    trace_log_push(LOG_METADATA);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(a, ca);
    _math_ivec2_to_cglm(b, cb);

    /* Integer division by zero traps (SIGFPE); a zero component of b refuses to the zeroed
     * vector, which cd already is - so the refusal is simply not calling cglm. */
    if (!(_math_ivec_div_traps(ca[0], cb[0]) || _math_ivec_div_traps(ca[1], cb[1]))) {
        glmc_ivec2_div(ca, cb, cd);
    }

    IVec2 const result = _math_ivec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec2_divs_1(ISize const *const v, ISize const s, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec2 cv = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(v, cv);

    /* The divisor is checked AFTER the ISize -> int conversion: a nonzero s whose low 32
     * bits are zero (2^32) truncates to a zero divisor and would trap just the same. */
    if (!(_math_ivec_div_traps(cv[0], (int) s) || _math_ivec_div_traps(cv[1], (int) s))) {
        glmc_ivec2_divs(cv, (int) s, cd);
    }

    _math_ivec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec2 math_ivec2_divs_2(IVec2 const v, ISize const s) {
    trace_log_push(LOG_METADATA);

    ivec2 cv = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(v, cv);

    /* The divisor is checked AFTER the ISize -> int conversion: a nonzero s whose low 32
     * bits are zero (2^32) truncates to a zero divisor and would trap just the same. */
    if (!(_math_ivec_div_traps(cv[0], (int) s) || _math_ivec_div_traps(cv[1], (int) s))) {
        glmc_ivec2_divs(cv, (int) s, cd);
    }

    IVec2 const result = _math_ivec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

ISize math_ivec2_dot_1(ISize const *const a, ISize const *const b) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(a, ca);
    _math_ivec2_raw_to_cglm(b, cb);

    ISize const result = (ISize) glmc_ivec2_dot(ca, cb);

    trace_log_pop();

    return result;
}

ISize math_ivec2_dot_2(IVec2 const a, IVec2 const b) {
    trace_log_push(LOG_METADATA);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(a, ca);
    _math_ivec2_to_cglm(b, cb);

    ISize const result = (ISize) glmc_ivec2_dot(ca, cb);

    trace_log_pop();

    return result;
}

bool math_ivec2_eq_1(ISize const *const v, ISize const val) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);

    ivec2 cv = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(v, cv);

    bool const result = glmc_ivec2_eq(cv, (int) val);

    trace_log_pop();

    return result;
}

bool math_ivec2_eq_2(IVec2 const v, ISize const val) {
    trace_log_push(LOG_METADATA);

    ivec2 cv = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(v, cv);

    bool const result = glmc_ivec2_eq(cv, (int) val);

    trace_log_pop();

    return result;
}

bool math_ivec2_eqv_1(ISize const *const a, ISize const *const b) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(a, ca);
    _math_ivec2_raw_to_cglm(b, cb);

    bool const result = glmc_ivec2_eqv(ca, cb);

    trace_log_pop();

    return result;
}

bool math_ivec2_eqv_2(IVec2 const a, IVec2 const b) {
    trace_log_push(LOG_METADATA);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(a, ca);
    _math_ivec2_to_cglm(b, cb);

    bool const result = glmc_ivec2_eqv(ca, cb);

    trace_log_pop();

    return result;
}

void math_ivec2_fill_1(ISize const val, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec2 cd = DEFAULT_INITIALIZATION;

    glmc_ivec2_fill(cd, (int) val);
    _math_ivec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec2 math_ivec2_fill_2(ISize const val) {
    trace_log_push(LOG_METADATA);

    ivec2 cd = DEFAULT_INITIALIZATION;

    glmc_ivec2_fill(cd, (int) val);

    IVec2 const result = _math_ivec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec2_ivec2_1(ISize const *const v, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    int cs[2] = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    cs[0] = (int) v[0];
    cs[1] = (int) v[1];
    glmc_ivec2(cs, cd);
    _math_ivec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec2 math_ivec2_ivec2_2(IVec3 const v) {
    trace_log_push(LOG_METADATA);

    ivec3 cs = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec3_to_cglm(v, cs);
    glmc_ivec2(cs, cd);

    IVec2 const result = _math_ivec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec2_maxadd_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(a, ca);
    _math_ivec2_raw_to_cglm(b, cb);
    _math_ivec2_raw_to_cglm(dest, cd);
    glmc_ivec2_maxadd(ca, cb, cd);
    _math_ivec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec2 math_ivec2_maxadd_2(IVec2 const a, IVec2 const b, IVec2 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(a, ca);
    _math_ivec2_to_cglm(b, cb);
    _math_ivec2_to_cglm(accumulator, cd);
    glmc_ivec2_maxadd(ca, cb, cd);

    IVec2 const result = _math_ivec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec2_maxsub_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(a, ca);
    _math_ivec2_raw_to_cglm(b, cb);
    _math_ivec2_raw_to_cglm(dest, cd);
    glmc_ivec2_maxsub(ca, cb, cd);
    _math_ivec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec2 math_ivec2_maxsub_2(IVec2 const a, IVec2 const b, IVec2 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(a, ca);
    _math_ivec2_to_cglm(b, cb);
    _math_ivec2_to_cglm(accumulator, cd);
    glmc_ivec2_maxsub(ca, cb, cd);

    IVec2 const result = _math_ivec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec2_maxv_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(a, ca);
    _math_ivec2_raw_to_cglm(b, cb);
    glmc_ivec2_maxv(ca, cb, cd);
    _math_ivec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec2 math_ivec2_maxv_2(IVec2 const a, IVec2 const b) {
    trace_log_push(LOG_METADATA);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(a, ca);
    _math_ivec2_to_cglm(b, cb);
    glmc_ivec2_maxv(ca, cb, cd);

    IVec2 const result = _math_ivec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec2_minadd_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(a, ca);
    _math_ivec2_raw_to_cglm(b, cb);
    _math_ivec2_raw_to_cglm(dest, cd);
    glmc_ivec2_minadd(ca, cb, cd);
    _math_ivec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec2 math_ivec2_minadd_2(IVec2 const a, IVec2 const b, IVec2 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(a, ca);
    _math_ivec2_to_cglm(b, cb);
    _math_ivec2_to_cglm(accumulator, cd);
    glmc_ivec2_minadd(ca, cb, cd);

    IVec2 const result = _math_ivec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec2_minsub_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(a, ca);
    _math_ivec2_raw_to_cglm(b, cb);
    _math_ivec2_raw_to_cglm(dest, cd);
    glmc_ivec2_minsub(ca, cb, cd);
    _math_ivec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec2 math_ivec2_minsub_2(IVec2 const a, IVec2 const b, IVec2 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(a, ca);
    _math_ivec2_to_cglm(b, cb);
    _math_ivec2_to_cglm(accumulator, cd);
    glmc_ivec2_minsub(ca, cb, cd);

    IVec2 const result = _math_ivec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec2_minv_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(a, ca);
    _math_ivec2_raw_to_cglm(b, cb);
    glmc_ivec2_minv(ca, cb, cd);
    _math_ivec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec2 math_ivec2_minv_2(IVec2 const a, IVec2 const b) {
    trace_log_push(LOG_METADATA);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(a, ca);
    _math_ivec2_to_cglm(b, cb);
    glmc_ivec2_minv(ca, cb, cd);

    IVec2 const result = _math_ivec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec2_mod_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(a, ca);
    _math_ivec2_raw_to_cglm(b, cb);

    /* `%` traps on the same divisors `/` does; refuse to the zeroed vector (cd already is). */
    if (!(_math_ivec_div_traps(ca[0], cb[0]) || _math_ivec_div_traps(ca[1], cb[1]))) {
        glmc_ivec2_mod(ca, cb, cd);
    }

    _math_ivec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec2 math_ivec2_mod_2(IVec2 const a, IVec2 const b) {
    trace_log_push(LOG_METADATA);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(a, ca);
    _math_ivec2_to_cglm(b, cb);

    /* `%` traps on the same divisors `/` does; refuse to the zeroed vector (cd already is). */
    if (!(_math_ivec_div_traps(ca[0], cb[0]) || _math_ivec_div_traps(ca[1], cb[1]))) {
        glmc_ivec2_mod(ca, cb, cd);
    }

    IVec2 const result = _math_ivec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec2_mul_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(a, ca);
    _math_ivec2_raw_to_cglm(b, cb);
    glmc_ivec2_mul(ca, cb, cd);
    _math_ivec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec2 math_ivec2_mul_2(IVec2 const a, IVec2 const b) {
    trace_log_push(LOG_METADATA);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(a, ca);
    _math_ivec2_to_cglm(b, cb);
    glmc_ivec2_mul(ca, cb, cd);

    IVec2 const result = _math_ivec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec2_muladd_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(a, ca);
    _math_ivec2_raw_to_cglm(b, cb);
    _math_ivec2_raw_to_cglm(dest, cd);
    glmc_ivec2_muladd(ca, cb, cd);
    _math_ivec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec2 math_ivec2_muladd_2(IVec2 const a, IVec2 const b, IVec2 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(a, ca);
    _math_ivec2_to_cglm(b, cb);
    _math_ivec2_to_cglm(accumulator, cd);
    glmc_ivec2_muladd(ca, cb, cd);

    IVec2 const result = _math_ivec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec2_muladds_1(ISize const *const a, ISize const s, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(a, ca);
    _math_ivec2_raw_to_cglm(dest, cd);
    glmc_ivec2_muladds(ca, (int) s, cd);
    _math_ivec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec2 math_ivec2_muladds_2(IVec2 const a, ISize const s, IVec2 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(a, ca);
    _math_ivec2_to_cglm(accumulator, cd);
    glmc_ivec2_muladds(ca, (int) s, cd);

    IVec2 const result = _math_ivec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec2_mulsub_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(a, ca);
    _math_ivec2_raw_to_cglm(b, cb);
    _math_ivec2_raw_to_cglm(dest, cd);
    glmc_ivec2_mulsub(ca, cb, cd);
    _math_ivec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec2 math_ivec2_mulsub_2(IVec2 const a, IVec2 const b, IVec2 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(a, ca);
    _math_ivec2_to_cglm(b, cb);
    _math_ivec2_to_cglm(accumulator, cd);
    glmc_ivec2_mulsub(ca, cb, cd);

    IVec2 const result = _math_ivec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec2_mulsubs_1(ISize const *const a, ISize const s, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(a, ca);
    _math_ivec2_raw_to_cglm(dest, cd);
    glmc_ivec2_mulsubs(ca, (int) s, cd);
    _math_ivec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec2 math_ivec2_mulsubs_2(IVec2 const a, ISize const s, IVec2 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(a, ca);
    _math_ivec2_to_cglm(accumulator, cd);
    glmc_ivec2_mulsubs(ca, (int) s, cd);

    IVec2 const result = _math_ivec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec2_one_1(ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec2 cd = DEFAULT_INITIALIZATION;

    glmc_ivec2_one(cd);
    _math_ivec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec2 math_ivec2_one_2(void) {
    trace_log_push(LOG_METADATA);

    ivec2 cd = DEFAULT_INITIALIZATION;

    glmc_ivec2_one(cd);

    IVec2 const result = _math_ivec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec2_scale_1(ISize const *const v, ISize const s, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec2 cv = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(v, cv);
    glmc_ivec2_scale(cv, (int) s, cd);
    _math_ivec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec2 math_ivec2_scale_2(IVec2 const v, ISize const s) {
    trace_log_push(LOG_METADATA);

    ivec2 cv = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(v, cv);
    glmc_ivec2_scale(cv, (int) s, cd);

    IVec2 const result = _math_ivec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec2_sub_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(a, ca);
    _math_ivec2_raw_to_cglm(b, cb);
    glmc_ivec2_sub(ca, cb, cd);
    _math_ivec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec2 math_ivec2_sub_2(IVec2 const a, IVec2 const b) {
    trace_log_push(LOG_METADATA);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(a, ca);
    _math_ivec2_to_cglm(b, cb);
    glmc_ivec2_sub(ca, cb, cd);

    IVec2 const result = _math_ivec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec2_subadd_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(a, ca);
    _math_ivec2_raw_to_cglm(b, cb);
    _math_ivec2_raw_to_cglm(dest, cd);
    glmc_ivec2_subadd(ca, cb, cd);
    _math_ivec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec2 math_ivec2_subadd_2(IVec2 const a, IVec2 const b, IVec2 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(a, ca);
    _math_ivec2_to_cglm(b, cb);
    _math_ivec2_to_cglm(accumulator, cd);
    glmc_ivec2_subadd(ca, cb, cd);

    IVec2 const result = _math_ivec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec2_subadds_1(ISize const *const a, ISize const s, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(a, ca);
    _math_ivec2_raw_to_cglm(dest, cd);
    glmc_ivec2_subadds(ca, (int) s, cd);
    _math_ivec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec2 math_ivec2_subadds_2(IVec2 const a, ISize const s, IVec2 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(a, ca);
    _math_ivec2_to_cglm(accumulator, cd);
    glmc_ivec2_subadds(ca, (int) s, cd);

    IVec2 const result = _math_ivec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec2_subs_1(ISize const *const v, ISize const s, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec2 cv = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(v, cv);
    glmc_ivec2_subs(cv, (int) s, cd);
    _math_ivec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec2 math_ivec2_subs_2(IVec2 const v, ISize const s) {
    trace_log_push(LOG_METADATA);

    ivec2 cv = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(v, cv);
    glmc_ivec2_subs(cv, (int) s, cd);

    IVec2 const result = _math_ivec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec2_subsub_1(ISize const *const a, ISize const *const b, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(a, ca);
    _math_ivec2_raw_to_cglm(b, cb);
    _math_ivec2_raw_to_cglm(dest, cd);
    glmc_ivec2_subsub(ca, cb, cd);
    _math_ivec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec2 math_ivec2_subsub_2(IVec2 const a, IVec2 const b, IVec2 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cb = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(a, ca);
    _math_ivec2_to_cglm(b, cb);
    _math_ivec2_to_cglm(accumulator, cd);
    glmc_ivec2_subsub(ca, cb, cd);

    IVec2 const result = _math_ivec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec2_subsubs_1(ISize const *const a, ISize const s, ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_raw_to_cglm(a, ca);
    _math_ivec2_raw_to_cglm(dest, cd);
    glmc_ivec2_subsubs(ca, (int) s, cd);
    _math_ivec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec2 math_ivec2_subsubs_2(IVec2 const a, ISize const s, IVec2 const accumulator) {
    trace_log_push(LOG_METADATA);

    ivec2 ca = DEFAULT_INITIALIZATION;
    ivec2 cd = DEFAULT_INITIALIZATION;

    _math_ivec2_to_cglm(a, ca);
    _math_ivec2_to_cglm(accumulator, cd);
    glmc_ivec2_subsubs(ca, (int) s, cd);

    IVec2 const result = _math_ivec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_ivec2_zero_1(ISize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    ivec2 cd = DEFAULT_INITIALIZATION;

    glmc_ivec2_zero(cd);
    _math_ivec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

IVec2 math_ivec2_zero_2(void) {
    trace_log_push(LOG_METADATA);

    ivec2 cd = DEFAULT_INITIALIZATION;

    glmc_ivec2_zero(cd);

    IVec2 const result = _math_ivec2_from_cglm(cd);

    trace_log_pop();

    return result;
}