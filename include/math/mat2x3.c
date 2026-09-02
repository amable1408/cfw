/*
 * mat2x3.c - 2x3 (2-column, 3-row) matrix operations for the CFW math module.
 *
 * See mat2x3.h for API documentation and usage examples.
 */

#include <math/mat2x3.h>

/*==============================================================================
 * MARK: - Mat2x3 API
 *============================================================================*/

void math_mat2x3_copy_1(FSize const *const mat, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat2x3 cm = DEFAULT_INITIALIZATION;
    mat2x3 cd = DEFAULT_INITIALIZATION;

    _math_mat2x3_raw_to_cglm(mat, cm);
    glmc_mat2x3_copy(cm, cd);
    _math_mat2x3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat2x3 math_mat2x3_copy_2(Mat2x3 const mat) {
    trace_log_push(LOG_METADATA);

    mat2x3 cm = DEFAULT_INITIALIZATION;
    mat2x3 cd = DEFAULT_INITIALIZATION;

    _math_mat2x3_to_cglm(mat, cm);
    glmc_mat2x3_copy(cm, cd);

    Mat2x3 const result = _math_mat2x3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat2x3_make_1(FSize const *const src, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "src", (void*) src);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    float cs[6] = DEFAULT_INITIALIZATION;
    mat2x3 cd = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 6; i += 1) {
        cs[i] = (float) src[i];
    }

    glmc_mat2x3_make(cs, cd);
    _math_mat2x3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat2x3 math_mat2x3_make_2(FSize const *const src) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "src", (void*) src);

    float cs[6] = DEFAULT_INITIALIZATION;
    mat2x3 cd = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 6; i += 1) {
        cs[i] = (float) src[i];
    }

    glmc_mat2x3_make(cs, cd);

    Mat2x3 const result = _math_mat2x3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat2x3_mul_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat2x3 cm1 = DEFAULT_INITIALIZATION;
    mat3x2 cm2 = DEFAULT_INITIALIZATION;
    mat3 cd = DEFAULT_INITIALIZATION;

    _math_mat2x3_raw_to_cglm(a, cm1);
    _math_mat3x2_raw_to_cglm(b, cm2);
    glmc_mat2x3_mul(cm1, cm2, cd);
    _math_mat3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat3 math_mat2x3_mul_2(Mat2x3 const a, Mat3x2 const b) {
    trace_log_push(LOG_METADATA);

    mat2x3 cm1 = DEFAULT_INITIALIZATION;
    mat3x2 cm2 = DEFAULT_INITIALIZATION;
    mat3 cd = DEFAULT_INITIALIZATION;

    _math_mat2x3_to_cglm(a, cm1);
    _math_mat3x2_to_cglm(b, cm2);
    glmc_mat2x3_mul(cm1, cm2, cd);

    Mat3 const result = _math_mat3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat2x3_mulv_1(FSize const *const m, FSize const *const v, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "m", (void*) m);
    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat2x3 cm = DEFAULT_INITIALIZATION;
    vec2 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_mat2x3_raw_to_cglm(m, cm);
    _math_vec2_raw_to_cglm(v, cv);
    glmc_mat2x3_mulv(cm, cv, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_mat2x3_mulv_2(Mat2x3 const m, Vec2 const v) {
    trace_log_push(LOG_METADATA);

    mat2x3 cm = DEFAULT_INITIALIZATION;
    vec2 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_mat2x3_to_cglm(m, cm);
    _math_vec2_to_cglm(v, cv);
    glmc_mat2x3_mulv(cm, cv, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat2x3_scale_1(FSize const *const mat, FSize const s, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat2x3 cm = DEFAULT_INITIALIZATION;

    _math_mat2x3_raw_to_cglm(mat, cm);
    glmc_mat2x3_scale(cm, (float) s);
    _math_mat2x3_raw_from_cglm(cm, dest);

    trace_log_pop();
}

Mat2x3 math_mat2x3_scale_2(Mat2x3 const mat, FSize const s) {
    trace_log_push(LOG_METADATA);

    mat2x3 cm = DEFAULT_INITIALIZATION;

    _math_mat2x3_to_cglm(mat, cm);
    glmc_mat2x3_scale(cm, (float) s);

    Mat2x3 const result = _math_mat2x3_from_cglm(cm);

    trace_log_pop();

    return result;
}

void math_mat2x3_transpose_1(FSize const *const mat, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat2x3 cm = DEFAULT_INITIALIZATION;
    mat3x2 cd = DEFAULT_INITIALIZATION;

    _math_mat2x3_raw_to_cglm(mat, cm);
    glmc_mat2x3_transpose(cm, cd);
    _math_mat3x2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat3x2 math_mat2x3_transpose_2(Mat2x3 const mat) {
    trace_log_push(LOG_METADATA);

    mat2x3 cm = DEFAULT_INITIALIZATION;
    mat3x2 cd = DEFAULT_INITIALIZATION;

    _math_mat2x3_to_cglm(mat, cm);
    glmc_mat2x3_transpose(cm, cd);

    Mat3x2 const result = _math_mat3x2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat2x3_zero_1(FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat2x3 cd = DEFAULT_INITIALIZATION;

    glmc_mat2x3_zero(cd);
    _math_mat2x3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat2x3 math_mat2x3_zero_2(void) {
    trace_log_push(LOG_METADATA);

    mat2x3 cd = DEFAULT_INITIALIZATION;

    glmc_mat2x3_zero(cd);

    Mat2x3 const result = _math_mat2x3_from_cglm(cd);

    trace_log_pop();

    return result;
}