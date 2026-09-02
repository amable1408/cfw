/*
 * mat3x4.c - 3-column 4-row matrix operations for the CFW math module.
 *
 * See mat3x4.h for API documentation and usage examples.
 */

#include <math/mat3x4.h>

/*==============================================================================
 * MARK: - Mat3x4 API
 *============================================================================*/

void math_mat3x4_copy_1(FSize const *const mat, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat3x4 cm = DEFAULT_INITIALIZATION;
    mat3x4 cd = DEFAULT_INITIALIZATION;

    _math_mat3x4_raw_to_cglm(mat, cm);
    glmc_mat3x4_copy(cm, cd);
    _math_mat3x4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat3x4 math_mat3x4_copy_2(Mat3x4 const mat) {
    trace_log_push(LOG_METADATA);

    mat3x4 cm = DEFAULT_INITIALIZATION;
    mat3x4 cd = DEFAULT_INITIALIZATION;

    _math_mat3x4_to_cglm(mat, cm);
    glmc_mat3x4_copy(cm, cd);

    Mat3x4 const result = _math_mat3x4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat3x4_make_1(FSize const *const src, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "src", (void*) src);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    float cs[12] = DEFAULT_INITIALIZATION;
    mat3x4 cd = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 12; i += 1) {
        cs[i] = (float) src[i];
    }

    glmc_mat3x4_make(cs, cd);
    _math_mat3x4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat3x4 math_mat3x4_make_2(FSize const *const src) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "src", (void*) src);

    float cs[12] = DEFAULT_INITIALIZATION;
    mat3x4 cd = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 12; i += 1) {
        cs[i] = (float) src[i];
    }

    glmc_mat3x4_make(cs, cd);

    Mat3x4 const result = _math_mat3x4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat3x4_mul_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat3x4 cm1 = DEFAULT_INITIALIZATION;
    mat4x3 cm2 = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_mat3x4_raw_to_cglm(a, cm1);
    _math_mat4x3_raw_to_cglm(b, cm2);
    glmc_mat3x4_mul(cm1, cm2, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_mat3x4_mul_2(Mat3x4 const a, Mat4x3 const b) {
    trace_log_push(LOG_METADATA);

    mat3x4 cm1 = DEFAULT_INITIALIZATION;
    mat4x3 cm2 = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_mat3x4_to_cglm(a, cm1);
    _math_mat4x3_to_cglm(b, cm2);
    glmc_mat3x4_mul(cm1, cm2, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat3x4_mulv_1(FSize const *const m, FSize const *const v, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "m", (void*) m);
    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat3x4 cm = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_mat3x4_raw_to_cglm(m, cm);
    _math_vec3_raw_to_cglm(v, cv);
    glmc_mat3x4_mulv(cm, cv, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_mat3x4_mulv_2(Mat3x4 const m, Vec3 const v) {
    trace_log_push(LOG_METADATA);

    mat3x4 cm = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_mat3x4_to_cglm(m, cm);
    _math_vec3_to_cglm(v, cv);
    glmc_mat3x4_mulv(cm, cv, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat3x4_scale_1(FSize const *const mat, FSize const s, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat3x4 cm = DEFAULT_INITIALIZATION;

    _math_mat3x4_raw_to_cglm(mat, cm);
    glmc_mat3x4_scale(cm, (float) s);
    _math_mat3x4_raw_from_cglm(cm, dest);

    trace_log_pop();
}

Mat3x4 math_mat3x4_scale_2(Mat3x4 const mat, FSize const s) {
    trace_log_push(LOG_METADATA);

    mat3x4 cm = DEFAULT_INITIALIZATION;

    _math_mat3x4_to_cglm(mat, cm);
    glmc_mat3x4_scale(cm, (float) s);

    Mat3x4 const result = _math_mat3x4_from_cglm(cm);

    trace_log_pop();

    return result;
}

void math_mat3x4_transpose_1(FSize const *const mat, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat3x4 cm = DEFAULT_INITIALIZATION;
    mat4x3 cd = DEFAULT_INITIALIZATION;

    _math_mat3x4_raw_to_cglm(mat, cm);
    glmc_mat3x4_transpose(cm, cd);
    _math_mat4x3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4x3 math_mat3x4_transpose_2(Mat3x4 const mat) {
    trace_log_push(LOG_METADATA);

    mat3x4 cm = DEFAULT_INITIALIZATION;
    mat4x3 cd = DEFAULT_INITIALIZATION;

    _math_mat3x4_to_cglm(mat, cm);
    glmc_mat3x4_transpose(cm, cd);

    Mat4x3 const result = _math_mat4x3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat3x4_zero_1(FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat3x4 cd = DEFAULT_INITIALIZATION;

    glmc_mat3x4_zero(cd);
    _math_mat3x4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat3x4 math_mat3x4_zero_2(void) {
    trace_log_push(LOG_METADATA);

    mat3x4 cd = DEFAULT_INITIALIZATION;

    glmc_mat3x4_zero(cd);

    Mat3x4 const result = _math_mat3x4_from_cglm(cd);

    trace_log_pop();

    return result;
}