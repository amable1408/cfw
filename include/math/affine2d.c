/*
 * affine2d.c - 2D affine (3x3) transform operations for the CFW math module.
 *
 * See affine2d.h for API documentation and usage examples.
 */

#include <math/affine2d.h>

/*==============================================================================
 * MARK: - Affine2D API
 *============================================================================*/

void math_affine2d_rotate2d_1(FSize const *const mat, FSize const angle, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat3 cm = DEFAULT_INITIALIZATION;
    mat3 cd = DEFAULT_INITIALIZATION;

    _math_mat3_raw_to_cglm(mat, cm);
    glmc_rotate2d_to(cm, (float) angle, cd);
    _math_mat3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat3 math_affine2d_rotate2d_2(Mat3 const mat, FSize const angle) {
    trace_log_push(LOG_METADATA);

    mat3 cm = DEFAULT_INITIALIZATION;
    mat3 cd = DEFAULT_INITIALIZATION;

    _math_mat3_to_cglm(mat, cm);
    glmc_rotate2d_to(cm, (float) angle, cd);

    Mat3 const result = _math_mat3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_affine2d_rotate2d_make_1(FSize const angle, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat3 cd = DEFAULT_INITIALIZATION;

    glmc_rotate2d_make(cd, (float) angle);
    _math_mat3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat3 math_affine2d_rotate2d_make_2(FSize const angle) {
    trace_log_push(LOG_METADATA);

    mat3 cd = DEFAULT_INITIALIZATION;

    glmc_rotate2d_make(cd, (float) angle);

    Mat3 const result = _math_mat3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_affine2d_scale2d_1(FSize const *const mat, FSize const *const v, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat3 cm = DEFAULT_INITIALIZATION;
    vec2 cv = DEFAULT_INITIALIZATION;
    mat3 cd = DEFAULT_INITIALIZATION;

    _math_mat3_raw_to_cglm(mat, cm);
    _math_vec2_raw_to_cglm(v, cv);
    glmc_scale2d_to(cm, cv, cd);
    _math_mat3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat3 math_affine2d_scale2d_2(Mat3 const mat, Vec2 const v) {
    trace_log_push(LOG_METADATA);

    mat3 cm = DEFAULT_INITIALIZATION;
    vec2 cv = DEFAULT_INITIALIZATION;
    mat3 cd = DEFAULT_INITIALIZATION;

    _math_mat3_to_cglm(mat, cm);
    _math_vec2_to_cglm(v, cv);
    glmc_scale2d_to(cm, cv, cd);

    Mat3 const result = _math_mat3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_affine2d_scale2d_make_1(FSize const *const v, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 cv = DEFAULT_INITIALIZATION;
    mat3 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(v, cv);
    glmc_scale2d_make(cd, cv);
    _math_mat3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat3 math_affine2d_scale2d_make_2(Vec2 const v) {
    trace_log_push(LOG_METADATA);

    vec2 cv = DEFAULT_INITIALIZATION;
    mat3 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(v, cv);
    glmc_scale2d_make(cd, cv);

    Mat3 const result = _math_mat3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_affine2d_scale2d_uni_1(FSize const *const mat, FSize const s, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat3 cm = DEFAULT_INITIALIZATION;

    _math_mat3_raw_to_cglm(mat, cm);
    glmc_scale2d_uni(cm, (float) s);
    _math_mat3_raw_from_cglm(cm, dest);

    trace_log_pop();
}

Mat3 math_affine2d_scale2d_uni_2(Mat3 const mat, FSize const s) {
    trace_log_push(LOG_METADATA);

    mat3 cm = DEFAULT_INITIALIZATION;

    _math_mat3_to_cglm(mat, cm);
    glmc_scale2d_uni(cm, (float) s);

    Mat3 const result = _math_mat3_from_cglm(cm);

    trace_log_pop();

    return result;
}

void math_affine2d_translate2d_1(FSize const *const mat, FSize const *const v, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat3 cm = DEFAULT_INITIALIZATION;
    vec2 cv = DEFAULT_INITIALIZATION;
    mat3 cd = DEFAULT_INITIALIZATION;

    _math_mat3_raw_to_cglm(mat, cm);
    _math_vec2_raw_to_cglm(v, cv);
    glmc_translate2d_to(cm, cv, cd);
    _math_mat3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat3 math_affine2d_translate2d_2(Mat3 const mat, Vec2 const v) {
    trace_log_push(LOG_METADATA);

    mat3 cm = DEFAULT_INITIALIZATION;
    vec2 cv = DEFAULT_INITIALIZATION;
    mat3 cd = DEFAULT_INITIALIZATION;

    _math_mat3_to_cglm(mat, cm);
    _math_vec2_to_cglm(v, cv);
    glmc_translate2d_to(cm, cv, cd);

    Mat3 const result = _math_mat3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_affine2d_translate2d_make_1(FSize const *const v, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 cv = DEFAULT_INITIALIZATION;
    mat3 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(v, cv);
    glmc_translate2d_make(cd, cv);
    _math_mat3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat3 math_affine2d_translate2d_make_2(Vec2 const v) {
    trace_log_push(LOG_METADATA);

    vec2 cv = DEFAULT_INITIALIZATION;
    mat3 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(v, cv);
    glmc_translate2d_make(cd, cv);

    Mat3 const result = _math_mat3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_affine2d_translate2d_x_1(FSize const *const mat, FSize const to, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat3 cm = DEFAULT_INITIALIZATION;

    _math_mat3_raw_to_cglm(mat, cm);
    glmc_translate2d_x(cm, (float) to);
    _math_mat3_raw_from_cglm(cm, dest);

    trace_log_pop();
}

Mat3 math_affine2d_translate2d_x_2(Mat3 const mat, FSize const to) {
    trace_log_push(LOG_METADATA);

    mat3 cm = DEFAULT_INITIALIZATION;

    _math_mat3_to_cglm(mat, cm);
    glmc_translate2d_x(cm, (float) to);

    Mat3 const result = _math_mat3_from_cglm(cm);

    trace_log_pop();

    return result;
}

void math_affine2d_translate2d_y_1(FSize const *const mat, FSize const to, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat3 cm = DEFAULT_INITIALIZATION;

    _math_mat3_raw_to_cglm(mat, cm);
    glmc_translate2d_y(cm, (float) to);
    _math_mat3_raw_from_cglm(cm, dest);

    trace_log_pop();
}

Mat3 math_affine2d_translate2d_y_2(Mat3 const mat, FSize const to) {
    trace_log_push(LOG_METADATA);

    mat3 cm = DEFAULT_INITIALIZATION;

    _math_mat3_to_cglm(mat, cm);
    glmc_translate2d_y(cm, (float) to);

    Mat3 const result = _math_mat3_from_cglm(cm);

    trace_log_pop();

    return result;
}