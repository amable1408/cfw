/*
 * project.c - Screen-space projection helpers for the CFW math module.
 *
 * See project.h for API documentation and usage examples.
 */

#include <math/project.h>

/*==============================================================================
 * MARK: - Project API
 *============================================================================*/

void math_project_1(FSize const *const pos, FSize const *const m, FSize const *const vp, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "pos", (void*) pos);
    error_check_null(LOG_METADATA, "m", (void*) m);
    error_check_null(LOG_METADATA, "vp", (void*) vp);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cpos = DEFAULT_INITIALIZATION;
    mat4 cm = DEFAULT_INITIALIZATION;
    vec4 cvp = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(pos, cpos);
    _math_mat4_raw_to_cglm(m, cm);
    _math_vec4_raw_to_cglm(vp, cvp);
    glmc_project_no(cpos, cm, cvp, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_project_2(Vec3 const pos, Mat4 const m, Vec4 const vp) {
    trace_log_push(LOG_METADATA);

    vec3 cpos = DEFAULT_INITIALIZATION;
    mat4 cm = DEFAULT_INITIALIZATION;
    vec4 cvp = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(pos, cpos);
    _math_mat4_to_cglm(m, cm);
    _math_vec4_to_cglm(vp, cvp);
    glmc_project_no(cpos, cm, cvp, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_project_pickmatrix_1(FSize const *const center, FSize const *const size, FSize const *const vp, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "center", (void*) center);
    error_check_null(LOG_METADATA, "size", (void*) size);
    error_check_null(LOG_METADATA, "vp", (void*) vp);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 ccenter = DEFAULT_INITIALIZATION;
    vec2 csize = DEFAULT_INITIALIZATION;
    vec4 cvp = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(center, ccenter);
    _math_vec2_raw_to_cglm(size, csize);
    _math_vec4_raw_to_cglm(vp, cvp);
    glmc_pickmatrix(ccenter, csize, cvp, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_project_pickmatrix_2(Vec2 const center, Vec2 const size, Vec4 const vp) {
    trace_log_push(LOG_METADATA);

    vec2 ccenter = DEFAULT_INITIALIZATION;
    vec2 csize = DEFAULT_INITIALIZATION;
    vec4 cvp = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(center, ccenter);
    _math_vec2_to_cglm(size, csize);
    _math_vec4_to_cglm(vp, cvp);
    glmc_pickmatrix(ccenter, csize, cvp, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_project_unproject_1(FSize const *const pos, FSize const *const m, FSize const *const vp, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "pos", (void*) pos);
    error_check_null(LOG_METADATA, "m", (void*) m);
    error_check_null(LOG_METADATA, "vp", (void*) vp);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cpos = DEFAULT_INITIALIZATION;
    mat4 cm = DEFAULT_INITIALIZATION;
    vec4 cvp = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(pos, cpos);
    _math_mat4_raw_to_cglm(m, cm);
    _math_vec4_raw_to_cglm(vp, cvp);
    glmc_unproject(cpos, cm, cvp, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_project_unproject_2(Vec3 const pos, Mat4 const m, Vec4 const vp) {
    trace_log_push(LOG_METADATA);

    vec3 cpos = DEFAULT_INITIALIZATION;
    mat4 cm = DEFAULT_INITIALIZATION;
    vec4 cvp = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(pos, cpos);
    _math_mat4_to_cglm(m, cm);
    _math_vec4_to_cglm(vp, cvp);
    glmc_unproject(cpos, cm, cvp, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_project_unprojecti_1(FSize const *const pos, FSize const *const inv_mat, FSize const *const vp, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "pos", (void*) pos);
    error_check_null(LOG_METADATA, "inv_mat", (void*) inv_mat);
    error_check_null(LOG_METADATA, "vp", (void*) vp);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cpos = DEFAULT_INITIALIZATION;
    mat4 cinv = DEFAULT_INITIALIZATION;
    vec4 cvp = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(pos, cpos);
    _math_mat4_raw_to_cglm(inv_mat, cinv);
    _math_vec4_raw_to_cglm(vp, cvp);
    glmc_unprojecti_no(cpos, cinv, cvp, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_project_unprojecti_2(Vec3 const pos, Mat4 const inv_mat, Vec4 const vp) {
    trace_log_push(LOG_METADATA);

    vec3 cpos = DEFAULT_INITIALIZATION;
    mat4 cinv = DEFAULT_INITIALIZATION;
    vec4 cvp = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(pos, cpos);
    _math_mat4_to_cglm(inv_mat, cinv);
    _math_vec4_to_cglm(vp, cvp);
    glmc_unprojecti_no(cpos, cinv, cvp, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

FSize math_project_z_1(FSize const *const pos, FSize const *const m) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "pos", (void*) pos);
    error_check_null(LOG_METADATA, "m", (void*) m);

    vec3 cpos = DEFAULT_INITIALIZATION;
    mat4 cm = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(pos, cpos);
    _math_mat4_raw_to_cglm(m, cm);

    FSize const result = (FSize) glmc_project_z_no(cpos, cm);

    trace_log_pop();

    return result;
}

FSize math_project_z_2(Vec3 const pos, Mat4 const m) {
    trace_log_push(LOG_METADATA);

    vec3 cpos = DEFAULT_INITIALIZATION;
    mat4 cm = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(pos, cpos);
    _math_mat4_to_cglm(m, cm);

    FSize const result = (FSize) glmc_project_z_no(cpos, cm);

    trace_log_pop();

    return result;
}