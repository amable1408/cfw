/*
 * mat3x2.c - 3x2 matrix operations for the CFW math module.
 *
 * See mat3x2.h for API documentation and usage examples.
 */

#include <math/mat3x2.h>

/*==============================================================================
 * MARK: - Mat3x2 API
 *============================================================================*/

void math_mat3x2_copy_1(FSize const *const mat, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat3x2 cs = DEFAULT_INITIALIZATION;
    mat3x2 cd = DEFAULT_INITIALIZATION;

    _math_mat3x2_raw_to_cglm(mat, cs);
    glmc_mat3x2_copy(cs, cd);
    _math_mat3x2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat3x2 math_mat3x2_copy_2(Mat3x2 const mat) {
    trace_log_push(LOG_METADATA);

    mat3x2 cs = DEFAULT_INITIALIZATION;
    mat3x2 cd = DEFAULT_INITIALIZATION;

    _math_mat3x2_to_cglm(mat, cs);
    glmc_mat3x2_copy(cs, cd);

    Mat3x2 const result = _math_mat3x2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat3x2_make_1(FSize const *const src, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "src", (void*) src);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    float cs[6] = DEFAULT_INITIALIZATION;
    mat3x2 cd = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 6; i += 1) {
        cs[i] = (float) src[i];
    }

    glmc_mat3x2_make(cs, cd);
    _math_mat3x2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat3x2 math_mat3x2_make_2(FSize const *const src) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "src", (void*) src);

    float cs[6] = DEFAULT_INITIALIZATION;
    mat3x2 cd = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 6; i += 1) {
        cs[i] = (float) src[i];
    }

    glmc_mat3x2_make(cs, cd);

    Mat3x2 const result = _math_mat3x2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat3x2_mul_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat3x2 cm1 = DEFAULT_INITIALIZATION;
    mat2x3 cm2 = DEFAULT_INITIALIZATION;
    mat2 cd = DEFAULT_INITIALIZATION;

    _math_mat3x2_raw_to_cglm(a, cm1);
    _math_mat2x3_raw_to_cglm(b, cm2);
    glmc_mat3x2_mul(cm1, cm2, cd);
    _math_mat2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat2 math_mat3x2_mul_2(Mat3x2 const a, Mat2x3 const b) {
    trace_log_push(LOG_METADATA);

    mat3x2 cm1 = DEFAULT_INITIALIZATION;
    mat2x3 cm2 = DEFAULT_INITIALIZATION;
    mat2 cd = DEFAULT_INITIALIZATION;

    _math_mat3x2_to_cglm(a, cm1);
    _math_mat2x3_to_cglm(b, cm2);
    glmc_mat3x2_mul(cm1, cm2, cd);

    Mat2 const result = _math_mat2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat3x2_mulv_1(FSize const *const m, FSize const *const v, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "m", (void*) m);
    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat3x2 cm = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_mat3x2_raw_to_cglm(m, cm);
    _math_vec3_raw_to_cglm(v, cv);
    glmc_mat3x2_mulv(cm, cv, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_mat3x2_mulv_2(Mat3x2 const m, Vec3 const v) {
    trace_log_push(LOG_METADATA);

    mat3x2 cm = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_mat3x2_to_cglm(m, cm);
    _math_vec3_to_cglm(v, cv);
    glmc_mat3x2_mulv(cm, cv, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat3x2_scale_1(FSize const *const mat, FSize const s, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat3x2 cm = DEFAULT_INITIALIZATION;

    _math_mat3x2_raw_to_cglm(mat, cm);
    glmc_mat3x2_scale(cm, (float) s);
    _math_mat3x2_raw_from_cglm(cm, dest);

    trace_log_pop();
}

Mat3x2 math_mat3x2_scale_2(Mat3x2 const mat, FSize const s) {
    trace_log_push(LOG_METADATA);

    mat3x2 cm = DEFAULT_INITIALIZATION;

    _math_mat3x2_to_cglm(mat, cm);
    glmc_mat3x2_scale(cm, (float) s);

    Mat3x2 const result = _math_mat3x2_from_cglm(cm);

    trace_log_pop();

    return result;
}

void math_mat3x2_transpose_1(FSize const *const mat, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat3x2 cs = DEFAULT_INITIALIZATION;
    mat2x3 cd = DEFAULT_INITIALIZATION;

    _math_mat3x2_raw_to_cglm(mat, cs);
    glmc_mat3x2_transpose(cs, cd);
    _math_mat2x3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat2x3 math_mat3x2_transpose_2(Mat3x2 const mat) {
    trace_log_push(LOG_METADATA);

    mat3x2 cs = DEFAULT_INITIALIZATION;
    mat2x3 cd = DEFAULT_INITIALIZATION;

    _math_mat3x2_to_cglm(mat, cs);
    glmc_mat3x2_transpose(cs, cd);

    Mat2x3 const result = _math_mat2x3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat3x2_zero_1(FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat3x2 cd = DEFAULT_INITIALIZATION;

    glmc_mat3x2_zero(cd);
    _math_mat3x2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat3x2 math_mat3x2_zero_2(void) {
    trace_log_push(LOG_METADATA);

    mat3x2 cd = DEFAULT_INITIALIZATION;

    glmc_mat3x2_zero(cd);

    Mat3x2 const result = _math_mat3x2_from_cglm(cd);

    trace_log_pop();

    return result;
}